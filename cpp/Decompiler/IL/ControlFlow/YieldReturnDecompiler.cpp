// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/ControlFlow/YieldReturnDecompiler.hpp"

#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"  // StObj
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"  // LdFlda
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include <algorithm>
#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Instructions/InvalidInstructions.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/YieldReturn.hpp"
#include "Decompiler/IL/Transforms/CopyPropagation.hpp"
#include "Decompiler/IL/Transforms/SplitVariables.hpp"
#include "Decompiler/Metadata/CodeMappingInfo.hpp"  // IsCompilerGeneratorEnumerator
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"


namespace ILSpy::Decompiler::IL {

namespace {

// The MethodDef token's table check (the raw-token convention: the high byte
// is the table, 0x06 is MethodDef).
bool IsMethodDefToken(std::uint32_t token) {
    return token != 0 && (token >> 24) == 0x06;
}

// File-local MatchLdcI4 out-form (the C# `MatchLdcI4(out int value)`,
// Instructions.cs): an LdcI4 reporting its value. Kept file-local because a
// shared out-form would be ambiguous with the shared match-against-v
// overload for an int lvalue (the D66/D94 precedent -- the
// NullPropagationTransform file-local helper documents the same trap); the
// literal-argument compare calls still resolve to the shared form.
bool MatchLdcI4(ILInstruction* inst, int& value) {
    auto* ldc = dynamic_cast<LdcI4*>(inst);
    if (ldc == nullptr) {
        value = 0;
        return false;
    }
    value = ldc->Value;
    return true;
}

// File-local MatchLdLoc out-form (the C# `MatchLdLoc(out ILVariable
// variable)`, PatternMatching.cs lines 100-113): an LdLoc reporting its
// variable. Same file-local discipline as MatchLdcI4 above -- the shared
// MatchLdLoc overload is the match-against-variable form, and a shared
// out-form would hijack it (the D66/D94 trap).
bool MatchLdLocOut(ILInstruction* inst, ILVariable*& variable) {
    auto* ldloc = dynamic_cast<LdLoc*>(inst);
    if (ldloc == nullptr) {
        variable = nullptr;
        return false;
    }
    variable = ldloc->Variable.get();
    return true;
}

// The C# `bool IsMethod(MethodDefinitionHandle method, string name)`
// (YieldReturnDecompiler.cs lines 1535-1558): the method's own name, then the
// MethodImpl rows -- an explicit interface implementation matches its
// declaration's name (a MethodDef or a MemberRef).
bool IsMethod(const Metadata::MetadataFile& metadata, std::uint32_t method,
              const std::string& name) {
    if (method == 0)
        return false;
    if (metadata.GetMethodName(method) == name)
        return true;
    for (const auto& impl : metadata.GetMethodImplementations(method)) {
        std::uint32_t decl = impl.MethodDeclarationToken;
        if (decl == 0)
            continue;
        if ((decl >> 24) == 0x06) {
            // MethodDef
            if (metadata.GetMethodName(decl) == name)
                return true;
        } else if ((decl >> 24) == 0x0A) {
            // MemberRef
            auto mr = metadata.GetMemberReference(decl);
            if (mr.has_value() && mr->Name == name)
                return true;
        }
    }
    return false;
}

// The C# `GetMethods().FirstOrDefault(m => IsMethod(m, name))` over a type.
std::uint32_t FindMethod(const Metadata::MetadataFile& metadata,
                         std::uint32_t typeToken, const std::string& name) {
    for (const auto& m : metadata.GetMethods(typeToken)) {
        if (IsMethod(metadata, m.Token, name))
            return m.Token;
    }
    return 0;
}

// The field-resolution pass: the port's IL reader records the raw
// FieldToken/name on each LdFlda and leaves the resolved IField null (the
// reader's deferred-metadata convention); the analyses read the resolved
// identity through MatchStFld/MatchLdFld, so the decoded bodies get their
// in-module FieldDef tokens resolved through the compilation's main module
// (the C# reader resolves at read time).
void ResolveFields(ILInstruction* node, const TypeSystem::MetadataModule& module) {
    if (auto* ldflda = dynamic_cast<LdFlda*>(node)) {
        if (ldflda->Field == nullptr && ldflda->FieldToken != 0 &&
            (ldflda->FieldToken >> 24) == 0x04) {
            ldflda->Field = std::const_pointer_cast<TypeSystem::IField>(
                std::shared_ptr<const TypeSystem::IField>(
                    std::shared_ptr<const TypeSystem::IField>(),
                    module.GetDefinitionField(ldflda->FieldToken)));
        }
    }
    for (int i = 0; i < node->ChildCount(); i++) {
        if (ILInstruction* child = node->GetChild(i))
            ResolveFields(child, module);
    }
}

// The method-resolution pass: the same deferred-resolution convention for
// Call nodes -- the reader leaves `Method` null and carries the raw token;
// the analyses read method identities (the finally-method table, the fault-
// block dispose check), so in-module MethodDef tokens borrow the module's
// interned definition through the AliasMethod surface (the no-op-deleter
// shared handle).
void ResolveCalls(ILInstruction* node, const TypeSystem::MetadataModule& module) {
    if (auto* call = dynamic_cast<Call*>(node)) {
        if (call->Method == nullptr && call->MethodToken != 0 &&
            (call->MethodToken >> 24) == 0x06) {
            const TypeSystem::IMethod* method =
                module.GetDefinitionMethod(call->MethodToken);
            if (method != nullptr)
                call->Method = TypeSystem::AliasMethod(method);
        }
    }
    for (int i = 0; i < node->ChildCount(); i++) {
        if (ILInstruction* child = node->GetChild(i))
            ResolveCalls(child, module);
    }
}

// The C# `internal static ILFunction CreateILAstLocal(MethodDefinitionHandle
// method, ILTransformContext context)`: the body decode + the early
// transform list (the aggressivelyDuplicateReturnBlocks form) + the port's
// field resolution. The decode routes through the context's
// DelegateBodyResolver hook (the CreateILReader bridge). Named ...Local so
// the public member wrapper can call it without the member name shadowing
// it inside the wrapper's body.
std::unique_ptr<ILFunction> CreateILAstLocal(std::uint32_t method,
                                             ILTransformContext& context) {
    if (method == 0)
        throw ControlFlow::SymbolicAnalysisFailedException("Method not found");
    const Metadata::MetadataFile& metadata = *context.Metadata;
    std::uint32_t rva = metadata.GetMethodRVA(method);
    if (rva == 0) {
        throw ControlFlow::SymbolicAnalysisFailedException(
            "Method has no body");
    }
    if (context.DelegateBodyResolver == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException("No IL reader");
    auto il = context.DelegateBodyResolver(method, rva);
    if (il == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException(
            "Method body did not decode");
    // The early list (the C# EarlyILTransforms(true)): the aggressive
    // return-block duplication form.
    ILTransformContext nestedContext = context;
    ControlFlowSimplification cfs;
    cfs.AggressivelyDuplicateReturnBlocks = true;
    cfs.Run(*il, nestedContext);
    SplitVariables().Run(*il, nestedContext);
    ILInlining().Run(*il, nestedContext);
    // The field/method resolution (the reader's deferred surface; see the
    // file-local helpers above).
    if (context.TypeSystem != nullptr) {
        if (const auto* module = dynamic_cast<const TypeSystem::MetadataModule*>(
                &context.TypeSystem->MainModule())) {
            if (il->Body != nullptr) {
                ResolveFields(il->Body.get(), *module);
                ResolveCalls(il->Body.get(), *module);
            }
        }
    }
    return il;
}

} // namespace

std::unique_ptr<ILFunction> YieldReturnDecompiler::CreateILAst(
    std::uint32_t method, ILTransformContext& context) {
    return CreateILAstLocal(method, context);
}

void YieldReturnDecompiler::ResolveReaderSurfaces(ILFunction& function,
                                                   ILTransformContext& context) {
    // The field/method resolution (the reader's deferred surface; the
    // file-local helpers live above, so this re-implements their walk
    // through the same metadata module bridge).
    if (context.TypeSystem == nullptr || function.Body == nullptr)
        return;
    const auto* module = dynamic_cast<const TypeSystem::MetadataModule*>(
        &context.TypeSystem->MainModule());
    if (module == nullptr)
        return;
    std::vector<ILInstruction*> stack{function.Body.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* ldflda = dynamic_cast<LdFlda*>(node)) {
            if (ldflda->Field == nullptr && ldflda->FieldToken != 0 &&
                (ldflda->FieldToken >> 24) == 0x04) {
                ldflda->Field = std::const_pointer_cast<TypeSystem::IField>(
                    std::shared_ptr<const TypeSystem::IField>(
                        std::shared_ptr<const TypeSystem::IField>(),
                        module->GetDefinitionField(ldflda->FieldToken)));
            }
        } else if (auto* ldsflda = dynamic_cast<LdsFlda*>(node)) {
            // The static-field address carries the same deferred Field
            // surface (the DynamicCallSiteTransform's cache-field reads).
            if (ldsflda->Field == nullptr && ldsflda->FieldToken != 0 &&
                (ldsflda->FieldToken >> 24) == 0x04) {
                ldsflda->Field =
                    std::const_pointer_cast<TypeSystem::IField>(
                        std::shared_ptr<const TypeSystem::IField>(
                            std::shared_ptr<const TypeSystem::IField>(),
                            module->GetDefinitionField(
                                ldsflda->FieldToken)));
            }
        } else if (auto* call = dynamic_cast<Call*>(node)) {
            if (call->Method == nullptr && call->MethodToken != 0 &&
                (call->MethodToken >> 24) == 0x06) {
                const TypeSystem::IMethod* method =
                    module->GetDefinitionMethod(call->MethodToken);
                if (method != nullptr)
                    call->Method = TypeSystem::AliasMethod(method);
            }
        }
        for (int i = 0; i < node->ChildCount(); i++) {
            if (ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
}

void YieldReturnDecompiler::Run(ILFunction& function,
                                ILTransformContext& context) {
    if (!context.Settings.YieldReturn)
        return;  // abort if enumerator decompilation is disabled
    context_ = &context;
    metadata_ = context.Metadata;
    if (metadata_ == nullptr)
        return;
    // The current type: the function's method's declaring type (the C#
    // metadata.GetMethodDefinition(...).GetDeclaringType()).
    if (function.Method == nullptr ||
        !IsMethodDefToken(function.Method->MetadataToken())) {
        return;
    }
    currentType_ = metadata_->GetMethodDeclaringTypeToken(
        function.Method->MetadataToken());
    if (currentType_ == 0)
        return;

    enumeratorType_ = 0;
    enumeratorCtor_ = 0;
    isCompiledWithMono_ = false;
    isCompiledWithVisualBasic_ = false;
    isCompiledWithLegacyVisualBasic_ = false;
    stateField_ = nullptr;
    currentField_ = nullptr;
    disposingField_ = nullptr;
    fieldToParameterMap_.clear();
    finallyMethodToStateRange_.clear();
    hasFinallyMethodToStateRange_ = false;

    // The C# reader resolves the field references during the decode; this
    // port's reader defers them (the raw token surfaces), and the pipeline
    // pass that resolves them (the DynamicCallSiteTransform's slot) has
    // not run yet at the yield-return transform's position. Resolve here
    // so the creation-pattern's field identity (the
    // fieldToParameterMap's keys -- the MemberDefinition pointers) and
    // the later field translations agree.
    ResolveReaderSurfaces(function, context);

    if (!MatchEnumeratorCreationPattern(function, context)) {
        return;
    }
    try {
        AnalyzeCtor(context);
        AnalyzeCurrentProperty(context);
        ResolveIEnumerableIEnumeratorFieldMapping(context);
        ConstructExceptionTable(context);
    } catch (const ControlFlow::SymbolicAnalysisFailedException& ex) {
        // The C# adds a warning and leaves the state machine as-is.
        return;
    }
    context.StepOnce("Replacing body with MoveNext() body");
    std::unique_ptr<BlockContainer> newBody;
    try {
        newBody = AnalyzeMoveNext(function, context);
    } catch (const ControlFlow::SymbolicAnalysisFailedException& ex) {
        return;
    } catch (const std::exception& ex) {
        return;
    }
    if (newBody == nullptr)
        return;
    function.IsIterator = true;
    auto oldBody = std::move(function.Body);
    function.Body = std::move(newBody);
    function.Body->Parent = &function;
    function.Body->ChildIndex = 0;
    // register any locals used in the new body (the C#
    // Variables.AddRange(newBody.Descendants.OfType<IStoreInstruction>)):
    // the walk collects every instruction's variable.
    {
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* stloc = dynamic_cast<StLoc*>(node)) {
                if (stloc->Variable != nullptr)
                    function.RegisterExistingVariable(stloc->Variable);
            } else if (auto* ldloc = dynamic_cast<LdLoc*>(node)) {
                if (ldloc->Variable != nullptr)
                    function.RegisterExistingVariable(ldloc->Variable);
            } else if (auto* ldloca = dynamic_cast<LdLoca*>(node)) {
                if (ldloca->Variable != nullptr)
                    function.RegisterExistingVariable(ldloca->Variable);
            }
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
    // Add state machine field meta-data to parameter ILVariables (the C#
    // foreach over fieldToParameterMap).
    for (auto& entry : fieldToParameterMap_) {
        if (entry.second != nullptr)
            entry.second->StateMachineField = entry.first;
    }

    context.StepOnce("Delete unreachable blocks");
    // The state-dispatch copies the ConvertBody left behind (and any other
    // unreachable block) drop out here; the C# relies on the sort's
    // deleteUnreachableBlocks.
    if (auto* newBodyContainer =
            dynamic_cast<BlockContainer*>(function.Body.get())) {
        newBodyContainer->SortBlocks(/*deleteUnreachableBlocks=*/true);
    }

    // The C# branch dispatch (lines 199-216): the Mono/VB cleanup arms are
    // deferred with the Mono/VB discriminators (ValidateConstructor is
    // unported; a no-arg ctor is treated as Mono, so the flags stay false
    // on the Roslyn fixtures); the MS arm reconstructs the try-finally
    // structure, rolling the whole conversion back on failure (the C#
    // reverts the body + the iterator flag + the variables).
    {
        std::size_t variableCountBefore = function.Variables.size();
        try {
            DecompileFinallyBlocks(context);
            ReconstructTryFinallyBlocks(function, context);
        } catch (const ControlFlow::SymbolicAnalysisFailedException&) {
            // Revert the yield-return transformation
            context.StepOnce("Transform failed, roll it back");
            function.IsIterator = false;
            function.Body = std::move(oldBody);
            function.Body->Parent = &function;
            function.Body->ChildIndex = 0;
            // The variables the new body registered drop out with it (the
            // C# Variables.RemoveDead()).
            if (function.Variables.size() > variableCountBefore)
                function.Variables.resize(variableCountBefore);
            return;
        }
    }
    context.StepOnce("Translate fields to local accesses");
    if (function.Body != nullptr) {
        TranslateFieldsToLocalAccess(function, function.Body.get(),
                                     fieldToParameterMap_,
                                     isCompiledWithMono_);
    }
    // On mono, we still need to remove traces of the state variable(s):
    if (isCompiledWithMono_ || isCompiledWithVisualBasic_) {
        // The C# reads stateField's StoreInstructions through the
        // field-to-parameter map; the port collects the state-variable's
        // stores by the walk (the missing per-variable list, D11/D62/D68).
        // (Deferred with the Mono arms -- the no-VB-discriminator staging
        // leaves the state stores in place; part 4's cleanups handle them.)
    }
    if (!returnStores_.empty()) {
        context.StepOnce("Remove temporaries");
        for (StLoc* store : returnStores_) {
            if (store->Variable != nullptr && store->Variable->LoadCount == 0 &&
                store->Variable->AddressCount == 0) {
                if (auto* block = dynamic_cast<Block*>(store->Parent)) {
                    for (auto it = block->Instructions.begin();
                         it != block->Instructions.end(); ++it) {
                        if (it->get() == store) {
                            block->Instructions.erase(it);
                            break;
                        }
                    }
                }
            }
        }
        returnStores_.clear();
    }
    // Re-run control flow simplification over the newly constructed set of
    // gotos, and inlining because TranslateFieldsToLocalAccess might have
    // opened up new inlining opportunities (the C# EarlyILTransforms()).
    {
        ControlFlowSimplification cfs;
        cfs.Run(function, context);
        SplitVariables().Run(function, context);
        ILInlining().Run(function, context);
    }
}

bool YieldReturnDecompiler::MatchEnumeratorCreationPattern(
    ILFunction& function, ILTransformContext& context) {
    Block* body = SingleBlock(function.Body.get());
    if (body == nullptr)
        return false;

    ILInstruction* newObj = nullptr;
    // The C# 1-instruction path (`ret(newobj(...))`): the C# reader puts the
    // terminator in the Instructions list; the port carries it in the final
    // slot, so the shape is an empty instruction list with a final that is
    // the return. (A 1-element list whose element is the return covers the
    // hand-built shapes.)
    bool oneInstructionReturn = false;
    if (body->Instructions.size() == 1) {
        ILInstruction* value = nullptr;
        if (MatchReturn(body->Instructions[0].get(), value)) {
            newObj = value;
            oneInstructionReturn = true;
        }
    } else if (body->Instructions.empty() &&
               body->FinalInstruction != nullptr) {
        ILInstruction* value = nullptr;
        if (MatchReturn(body->FinalInstruction.get(), value)) {
            newObj = value;
            oneInstructionReturn = true;
        }
    }
    if (oneInstructionReturn) {
        // No parameters passed to enumerator (not even 'this'):
        // ret(newobj(...))
        if (MatchEnumeratorCreationNewObj(newObj, *metadata_, currentType_,
                                          enumeratorCtor_, enumeratorType_)) {
            return true;
        } else if (MatchMonoEnumeratorCreationNewObj(
                       newObj, *metadata_, currentType_, enumeratorCtor_,
                       enumeratorType_)) {
            isCompiledWithMono_ = true;
            return true;
        } else {
            return false;
        }
    }

    // If there's parameters passed to the helper class, the class instance
    // is first stored in a variable, then the parameters are copied over,
    // then the instance is returned.

    // The C# bails on an empty instruction list (its count includes the
    // terminator; a Count of 0 is no body at all). The port's list excludes
    // the final slot, so the equivalent bail is an empty list whose final
    // is not the return shape above.
    if (body->Instructions.empty())
        return false;

    std::size_t pos = 0;

    // stloc(var_1, newobj(..))
    ILVariable* var1 = nullptr;
    if (!MatchStLoc(body->Instructions[pos].get(), var1, newObj)) {
        return false;
    }
    if (MatchEnumeratorCreationNewObj(newObj, *metadata_, currentType_,
                                      enumeratorCtor_, enumeratorType_)) {
        pos++;  // OK
    } else if (MatchMonoEnumeratorCreationNewObj(newObj, *metadata_,
                                                 currentType_, enumeratorCtor_,
                                                 enumeratorType_)) {
        pos++;
        // The C# distinguishes Mono from legacy Visual Basic through
        // TransformDisplayClassUsage.ValidateConstructor (the ctor's
        // parameter shapes); that surface is not ported, so the no-arg-ctor
        // shape is treated as Mono. The legacy-VB arms of the later phases
        // are deferred with it.
        isCompiledWithMono_ = true;
    } else {
        return false;
    }

    bool stateFieldInitialized = false;
    for (; pos < body->Instructions.size(); pos++) {
        // stfld(..., ldloc(var_1), ldloc(parameter))
        // or (in structs): stfld(..., ldloc(var_1), ldobj(ldloc(this)))
        ILInstruction* ldloc = nullptr;
        const TypeSystem::IField* storedField = nullptr;
        ILInstruction* value = nullptr;
        if (!MatchStFld(body->Instructions[pos].get(), ldloc, storedField,
                        value))
            break;
        if (!MatchLdLoc(ldloc, var1)) {
            return false;
        }
        const TypeSystem::IField* fieldDefinition =
            storedField != nullptr ? static_cast<const TypeSystem::IField*>(
                                         storedField->MemberDefinition())
                                   : nullptr;
        ILVariable* parameter = nullptr;
        if (MatchLdLocOut(value, parameter) &&
            parameter->Kind == VariableKind::Parameter) {
            fieldToParameterMap_[fieldDefinition] = parameter;
        } else if (auto* ldobj = dynamic_cast<LdObj*>(value);
                   ldobj != nullptr && MatchLdThis(ldobj->Target.get())) {
            // copy of 'this' in struct
            auto* ldlocThis = dynamic_cast<LdLoc*>(ldobj->Target.get());
            fieldToParameterMap_[fieldDefinition] =
                ldlocThis != nullptr ? ldlocThis->Variable.get() : nullptr;
        } else if ((isCompiledWithMono_ || isCompiledWithLegacyVisualBasic_) &&
                   (MatchLdcI4(value, -2) || MatchLdcI4(value, -1) ||
                    MatchLdcI4(value, 0))) {
            stateField_ = fieldDefinition;
            stateFieldInitialized = true;
        } else {
            return false;
        }
    }

    // The instruction at position p, falling back to the final slot (the
    // port's terminator convention; the C# carries the return in the list).
    auto instAt = [&](std::size_t p) -> ILInstruction* {
        if (p < body->Instructions.size())
            return body->Instructions[p].get();
        if (p == body->Instructions.size() && body->FinalInstruction)
            return body->FinalInstruction.get();
        return nullptr;
    };
    // In debug builds, the compiler may copy the var1 into another variable
    // (var2) before returning it.
    ILVariable* var2 = nullptr;
    {
        ILVariable* v2 = nullptr;
        ILInstruction* ldlocForStloc2 = nullptr;
        ILInstruction* at = instAt(pos);
        if (at != nullptr && MatchStLoc(at, v2, ldlocForStloc2) &&
            MatchLdLoc(ldlocForStloc2, var1)) {
            // stloc(var_2, ldloc(var_1))
            var2 = v2;
            pos++;
        }
    }
    if (isCompiledWithMono_ && !stateFieldInitialized) {
        // Mono initializes the state field separately:
        // (but not if it's left at the default value 0)
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        ILInstruction* value = nullptr;
        ILInstruction* at = instAt(pos);
        if (at != nullptr &&
            MatchStFld(at, target, field, value) &&
            MatchLdLoc(target, var2 != nullptr ? var2 : var1) &&
            (MatchLdcI4(value, -2) || MatchLdcI4(value, 0))) {
            stateField_ = field != nullptr
                              ? static_cast<const TypeSystem::IField*>(
                                    field->MemberDefinition())
                              : nullptr;
            pos++;
        }
    }
    ILInstruction* retVal = nullptr;
    ILInstruction* at = instAt(pos);
    if (at != nullptr && MatchReturn(at, retVal) &&
        MatchLdLoc(retVal, var2 != nullptr ? var2 : var1)) {
        // ret(ldloc(var_2))
        return true;
    } else {
        return false;
    }
}

Block* YieldReturnDecompiler::SingleBlock(ILInstruction* body) {
    auto* block = dynamic_cast<Block*>(body);
    if (auto* blockContainer = dynamic_cast<BlockContainer*>(body)) {
        if (blockContainer->Blocks.size() == 1) {
            block = blockContainer->Blocks[0].get();
        }
    }
    return block;
}

bool YieldReturnDecompiler::MatchEnumeratorCreationNewObj(
    ILInstruction* inst, const Metadata::MetadataFile& metadata,
    std::uint32_t currentType, std::uint32_t& enumeratorCtor,
    std::uint32_t& enumeratorType) {
    enumeratorCtor = 0;
    enumeratorType = 0;
    // newobj(CurrentType/...::.ctor, ldc.i4(-2))
    auto* newObj = dynamic_cast<Call*>(inst);
    if (newObj == nullptr || !newObj->IsNewObj)
        return false;
    if (newObj->Arguments.size() != 1)
        return false;
    int initialState = 0;
    bool ldcOk = MatchLdcI4(newObj->Arguments[0].get(), initialState);
    if (!ldcOk)
        return false;
    if (!(initialState == -2 || initialState == 0))
        return false;
    if (newObj->Method == nullptr && newObj->MethodToken == 0)
        return false;
    // The reader's deferred-resolution convention: a decoded call may carry
    // only the raw token (Method null), so the identity falls back to it.
    std::uint32_t handle = newObj->Method != nullptr
                               ? newObj->Method->MetadataToken()
                               : newObj->MethodToken;
    enumeratorCtor =
        IsMethodDefToken(handle) ? handle : 0;
    enumeratorType = enumeratorCtor != 0
                         ? metadata.GetMethodDeclaringTypeToken(enumeratorCtor)
                         : 0;
    std::uint32_t declaring =
        enumeratorType != 0
            ? metadata.GetTypeDefNameInfo(enumeratorType)
                      .value_or(Metadata::TypeDefNameInfo{})
                      .DeclaringTypeToken
            : 0;
    bool isEnum = Metadata::IsCompilerGeneratorEnumerator(metadata,
                                                           enumeratorType);
    return declaring == currentType && isEnum;
}

bool YieldReturnDecompiler::MatchMonoEnumeratorCreationNewObj(
    ILInstruction* inst, const Metadata::MetadataFile& metadata,
    std::uint32_t currentType, std::uint32_t& enumeratorCtor,
    std::uint32_t& enumeratorType) {
    // mcs generates iterators that take no parameters in the ctor
    auto* newObj = dynamic_cast<Call*>(inst);
    if (newObj == nullptr || !newObj->IsNewObj)
        return false;
    if (newObj->Arguments.size() != 0)
        return false;
    if (newObj->Method == nullptr && newObj->MethodToken == 0)
        return false;
    // The reader's deferred-resolution convention (see the Roslyn-form
    // match above).
    std::uint32_t handle = newObj->Method != nullptr
                               ? newObj->Method->MetadataToken()
                               : newObj->MethodToken;
    enumeratorCtor = IsMethodDefToken(handle) ? handle : 0;
    enumeratorType = enumeratorCtor != 0
                         ? metadata.GetMethodDeclaringTypeToken(enumeratorCtor)
                         : 0;
    std::uint32_t declaring =
        enumeratorType != 0
            ? metadata.GetTypeDefNameInfo(enumeratorType)
                      .value_or(Metadata::TypeDefNameInfo{})
                      .DeclaringTypeToken
            : 0;
    return declaring == currentType &&
           Metadata::IsCompilerGeneratorEnumerator(metadata, enumeratorType);
}

void YieldReturnDecompiler::AnalyzeCtor(ILTransformContext& context) {
    auto il = CreateILAstLocal(enumeratorCtor_, context);
    Block* body = SingleBlock(il->Body.get());
    if (body == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException(
            "Missing enumeratorCtor.Body");
    for (auto& inst : body->Instructions) {
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        ILInstruction* value = nullptr;
        ILVariable* arg = nullptr;
        if (MatchStFld(inst.get(), target, field, value) &&
            MatchLdThis(target) && MatchLdLocOut(value, arg) &&
            arg->Kind == VariableKind::Parameter && arg->Index == 0) {
            stateField_ = field != nullptr
                              ? static_cast<const TypeSystem::IField*>(
                                    field->MemberDefinition())
                              : nullptr;
        }
    }
    if (stateField_ == nullptr && !isCompiledWithMono_)
        throw ControlFlow::SymbolicAnalysisFailedException(
            "Could not find stateField");
}

void YieldReturnDecompiler::AnalyzeCurrentProperty(ILTransformContext& context) {
    std::uint32_t getCurrentMethod = 0;
    getCurrentMethod = FindMethod(*metadata_, enumeratorType_, "get_Current");
    auto il = CreateILAstLocal(getCurrentMethod, context);
    Block* body = SingleBlock(il->Body.get());
    if (body == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException(
            "get_Current has no body");
    // The port's terminator convention carries the sole `ret` in the
    // FinalInstruction slot (the C# reads the same instruction from the
    // instructions list); MatchReturn accepts the leave carrying the value.
    ILInstruction* final = body->FinalInstruction.get();
    if (body->Instructions.empty() && final != nullptr) {
        // release builds directly return the current field
        // ret(ldfld F(ldloc(this)))
        ILInstruction* retVal = nullptr;
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        if (MatchReturn(final, retVal) &&
            MatchLdFld(retVal, target, field) && MatchLdThis(target)) {
            currentField_ = field != nullptr
                                ? static_cast<const TypeSystem::IField*>(
                                      field->MemberDefinition())
                                : nullptr;
        }
    } else if (body->Instructions.size() == 1 && final != nullptr) {
        // debug builds store the return value in a temporary
        // stloc V = ldfld F(ldloc(this))
        // ret(ldloc V)
        ILVariable* v = nullptr;
        ILInstruction* ldfld = nullptr;
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        ILInstruction* retVal = nullptr;
        if (MatchStLoc(body->Instructions[0].get(), v, ldfld) &&
            MatchLdFld(ldfld, target, field) && MatchLdThis(target) &&
            MatchReturn(final, retVal) &&
            MatchLdLoc(retVal, v)) {
            currentField_ = field != nullptr
                                ? static_cast<const TypeSystem::IField*>(
                                      field->MemberDefinition())
                                : nullptr;
        }
    }
    if (currentField_ == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException(
            "Could not find currentField");
}

void YieldReturnDecompiler::ResolveIEnumerableIEnumeratorFieldMapping(
    ILTransformContext& context) {
    std::uint32_t getEnumeratorMethod = 0;
    getEnumeratorMethod = FindMethod(*metadata_, enumeratorType_,
                                      "GetEnumerator");
    ResolveIEnumerableIEnumeratorFieldMapping(*metadata_, context,
                                              fieldToParameterMap_,
                                              getEnumeratorMethod);
}

void YieldReturnDecompiler::ResolveIEnumerableIEnumeratorFieldMapping(
    const Metadata::MetadataFile& metadata, ILTransformContext& context,
    std::map<const TypeSystem::IField*, ILVariable*>& fieldToParameterMap,
    std::uint32_t getEnumeratorMethod) {
    if (getEnumeratorMethod == 0)
        return;  // no mappings (maybe it's just an IEnumerator implementation?)
    auto function = CreateILAstLocal(getEnumeratorMethod, context);
    // The C# `function.Descendants.OfType<Block>()` walk.
    std::vector<Block*> worklist;
    std::vector<ILInstruction*> stack{function.get()};
    while (!stack.empty()) {
        ILInstruction* node = stack.back();
        stack.pop_back();
        if (auto* block = dynamic_cast<Block*>(node)) {
            worklist.push_back(block);
        }
        for (int i = node->ChildCount() - 1; i >= 0; i--) {
            if (ILInstruction* child = node->GetChild(i))
                stack.push_back(child);
        }
    }
    for (Block* block : worklist) {
        for (auto& inst : block->Instructions) {
            // storeTarget.storeField = this.loadField;
            ILInstruction* storeTarget = nullptr;
            const TypeSystem::IField* storeField = nullptr;
            ILInstruction* storeValue = nullptr;
            ILInstruction* loadTarget = nullptr;
            const TypeSystem::IField* loadField = nullptr;
            if (MatchStFld(inst.get(), storeTarget, storeField, storeValue) &&
                MatchLdFld(storeValue, loadTarget, loadField) &&
                MatchLdThis(loadTarget)) {
                const TypeSystem::IField* storeDefinition =
                    storeField != nullptr
                        ? static_cast<const TypeSystem::IField*>(
                              storeField->MemberDefinition())
                        : nullptr;
                const TypeSystem::IField* loadDefinition =
                    loadField != nullptr
                        ? static_cast<const TypeSystem::IField*>(
                              loadField->MemberDefinition())
                        : nullptr;
                auto it = fieldToParameterMap.find(loadDefinition);
                if (it != fieldToParameterMap.end())
                    fieldToParameterMap[storeDefinition] = it->second;
            }
        }
    }
}

void YieldReturnDecompiler::ConstructExceptionTable(ILTransformContext& context) {
    disposeMethod_ = 0;
    disposeMethod_ = FindMethod(*metadata_, enumeratorType_, "Dispose");
    auto function = CreateILAstLocal(disposeMethod_, context);

    if (!isCompiledWithVisualBasic_ && !isCompiledWithMono_) {
        auto* body = dynamic_cast<BlockContainer*>(function->Body.get());
        if (body != nullptr) {
            for (auto& block : body->Blocks) {
                for (auto& instr : block->Instructions) {
                    // The C# `call.Arguments.Count == 1 && MatchLdThis &&
                    // IsMethod(call.Method.MetadataToken, "MoveNext")` -- the
                    // VB discriminator (a Dispose that calls MoveNext).
                    auto* call = dynamic_cast<Call*>(instr.get());
                    if (call != nullptr && !call->IsNewObj &&
                        call->Arguments.size() == 1 &&
                        MatchLdThis(call->Arguments[0].get()) &&
                        call->Method != nullptr &&
                        IsMethodDefToken(call->Method->MetadataToken()) &&
                        IsMethod(*metadata_,
                                 call->Method->MetadataToken(),
                                 "MoveNext")) {
                        isCompiledWithVisualBasic_ = true;
                        break;
                    }
                }
                if (isCompiledWithVisualBasic_) break;
            }
        }
    }

    if (isCompiledWithMono_ || isCompiledWithVisualBasic_) {
        auto* body = dynamic_cast<BlockContainer*>(function->Body.get());
        if (body != nullptr) {
            Block* entry = body->EntryPoint();
            for (std::size_t i = 0;
                 i < entry->Instructions.size() &&
                 dynamic_cast<Branch*>(entry->Instructions[i].get()) == nullptr;
                 i++) {
                auto* stobj = dynamic_cast<StObj*>(entry->Instructions[i].get());
                ILInstruction* target = nullptr;
                const TypeSystem::IField* field = nullptr;
                ILInstruction* value = nullptr;
                if (stobj != nullptr &&
                    MatchStFld(stobj, target, field, value) &&
                    MatchLdThis(target) && value != nullptr &&
                    MatchLdcI4(value, 1)) {
                    // The C# also checks `field.Type.IsKnownType(Boolean)`;
                    // the port's LdFlda carries the resolved field only when
                    // the resolution pass ran (it did, in CreateILAst), so
                    // the type check reads the resolved field's type.
                    const TypeSystem::IField* definition =
                        field != nullptr
                            ? static_cast<const TypeSystem::IField*>(
                                  field->MemberDefinition())
                            : nullptr;
                    bool isBool =
                        definition != nullptr &&
                        TypeSystem::IsKnownType(
                            definition->ReturnType(),
                        TypeSystem::KnownTypeCode::Boolean);
                    if (isBool) {
                        disposingField_ = definition;
                        break;
                    }
                }
            }
        }
        // On mono and VB, we don't need to analyse Dispose() to reconstruct
        // the try-finally structure.
        hasFinallyMethodToStateRange_ = false;
    } else {
        // Non-Mono/Non-VB: analyze try-finally structure in Dispose()
        ControlFlow::StateRangeAnalysis rangeAnalysis(
            ControlFlow::StateRangeAnalysisMode::IteratorDispose, stateField_);
        rangeAnalysis.AssignStateRanges(function->Body.get(),
                                        Util::LongSet::Universe());
        finallyMethodToStateRange_ = rangeAnalysis.FinallyMethodToStateRange();
        hasFinallyMethodToStateRange_ = true;
    }
}


std::unique_ptr<BlockContainer> YieldReturnDecompiler::AnalyzeMoveNext(
    ILFunction& function, ILTransformContext& context) {
    context.StepOnce("AnalyzeMoveNext");
    std::uint32_t moveNextMethod =
        FindMethod(*metadata_, enumeratorType_, "MoveNext");
    auto moveNextFunction = CreateILAstLocal(moveNextMethod, context);
    if (moveNextFunction == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException(
            "MoveNext did not decode");

    // Copy-propagate temporaries holding a copy of 'this' (the old
    // pre-Roslyn compiler likes to store 'this' in temporary variables),
    // then stack slots holding a 32 bit integer. The C# iterates
    // Descendants.OfType<StLoc>() snapshots; the port collects first.
    {
        std::vector<StLoc*> thisCopies;
        std::vector<StLoc*> intSlots;
        std::vector<ILInstruction*> stack{moveNextFunction.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* stloc = dynamic_cast<StLoc*>(node)) {
                if (stloc->Variable != nullptr &&
                    stloc->Variable->IsSingleDefinition() &&
                    MatchLdThis(stloc->Value.get()))
                    thisCopies.push_back(stloc);
                else if (stloc->Variable != nullptr &&
                         stloc->Variable->Kind == VariableKind::StackSlot &&
                         stloc->Variable->IsSingleDefinition() &&
                         stloc->Value != nullptr &&
                         stloc->Value->Op == OpCode::LdcI4)
                    intSlots.push_back(stloc);
            }
            for (int i = node->ChildCount() - 1; i >= 0; i--) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
        for (StLoc* stloc : thisCopies)
            CopyPropagation::Propagate(stloc, context);
        for (StLoc* stloc : intSlots)
            CopyPropagation::Propagate(stloc, context);
    }
    // The C# `block.Instructions.RemoveAll(inst => OpCode == LdcI4)` over
    // every block.
    {
        std::vector<Block*> blocks;
        std::vector<ILInstruction*> stack{moveNextFunction.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* block = dynamic_cast<Block*>(node))
                blocks.push_back(block);
            for (int i = node->ChildCount() - 1; i >= 0; i--) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
        for (Block* block : blocks) {
            block->Instructions.erase(
                std::remove_if(block->Instructions.begin(),
                               block->Instructions.end(),
                               [](const std::unique_ptr<ILInstruction>& inst) {
                                   return inst->Op == OpCode::LdcI4;
                               }),
                block->Instructions.end());
        }
    }

    auto* body = dynamic_cast<BlockContainer*>(moveNextFunction->Body.get());
    if (body == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException(
            "MoveNext body is not a container");
    // The TryFault unwrap (the Roslyn debug-build shape).
    if (body->Blocks.size() == 1 &&
        body->Blocks[0]->Instructions.size() == 1) {
        if (auto* tryFault =
                dynamic_cast<TryFault*>(body->Blocks[0]->Instructions[0].get())) {
            body = dynamic_cast<BlockContainer*>(tryFault->TryBlock.get());
            auto* faultBlockContainer =
                dynamic_cast<BlockContainer*>(tryFault->FaultBlock.get());
            if (faultBlockContainer == nullptr ||
                faultBlockContainer->Blocks.size() != 1)
                throw ControlFlow::SymbolicAnalysisFailedException(
                    "Unexpected number of blocks in MoveNext() fault block");
            Block* faultBlock = faultBlockContainer->Blocks[0].get();
            // The port's terminator convention: the C# reads [call, leave]
            // from the instructions list; the port carries the leave in the
            // FinalInstruction slot.
            bool ok = faultBlock->Instructions.size() == 1 &&
                      dynamic_cast<Call*>(faultBlock->Instructions[0].get()) !=
                          nullptr &&
                      faultBlock->FinalInstruction != nullptr;
            if (ok) {
                auto* call =
                    dynamic_cast<Call*>(faultBlock->Instructions[0].get());
                // The reader's deferred-resolution convention: the decoded
                // call may carry only the raw token (Method null).
                std::uint32_t callToken =
                    call->Method != nullptr
                        ? call->Method->MetadataToken()
                        : call->MethodToken;
                ok = callToken == disposeMethod_ &&
                     call->Arguments.size() == 1 &&
                     MatchLdThis(call->Arguments[0].get()) &&
                     MatchLeave(faultBlock->FinalInstruction.get(),
                                faultBlockContainer);
            }
            if (!ok)
                throw ControlFlow::SymbolicAnalysisFailedException(
                    "Unexpected fault block contents in MoveNext()");
        }
    }
    // The legacyVB pre-shape is deferred with the VB discriminator.

    if (stateField_ == nullptr) {
        // With mono-compiled state machines, the state field may be
        // implicitly initialized to 0; discover it from MoveNext's first
        // instruction.
        if (!body->EntryPoint()->Instructions.empty() &&
            body->EntryPoint()->Instructions[0]->Op == OpCode::StLoc) {
            auto* stloc =
                dynamic_cast<StLoc*>(
                    body->EntryPoint()->Instructions[0].get());
            ILInstruction* target = nullptr;
            const TypeSystem::IField* field = nullptr;
            if (stloc != nullptr && MatchLdFld(stloc->Value.get(), target, field) &&
                MatchLdThis(target) && field != nullptr &&
                TypeSystem::IsKnownType(field->ReturnType(),
                                        TypeSystem::KnownTypeCode::Int32)) {
                stateField_ = static_cast<const TypeSystem::IField*>(
                    field->MemberDefinition());
            }
        }
        if (stateField_ == nullptr)
            throw ControlFlow::SymbolicAnalysisFailedException(
                "Could not find state field.");
    }

    skipFinallyBodies_ = nullptr;
    if (isCompiledWithMono_) {
        // Mono uses skipFinallyBodies; find out which variable that is.
        std::vector<TryFinally*> tryFinalies;
        std::vector<ILInstruction*> stack{body};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* tf = dynamic_cast<TryFinally*>(node))
                tryFinalies.push_back(tf);
            for (int i = node->ChildCount() - 1; i >= 0; i--) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
        for (TryFinally* tryFinally : tryFinalies) {
            auto* finallyContainer =
                dynamic_cast<BlockContainer*>(tryFinally->FinallyBlock.get());
            if (finallyContainer == nullptr ||
                finallyContainer->EntryPoint()->Instructions.empty())
                continue;
            auto* ifInst = dynamic_cast<IfInstruction*>(
                finallyContainer->EntryPoint()->Instructions[0].get());
            if (ifInst == nullptr)
                continue;
            ILVariable* v = nullptr;
            // The file-local MatchLogicNot shape (the
            // PatternMatchingTransform precedent): comp(arg == 0) -- the
            // negated-condition form the Mono finally guards use.
            ILInstruction* notArg = nullptr;
            bool isNot = false;
            if (auto* comp =
                    dynamic_cast<Comp*>(ifInst->Condition.get())) {
                if (comp->Kind == ComparisonKind::Equality) {
                    auto* rhs = dynamic_cast<LdcI4*>(comp->Right.get());
                    if (rhs != nullptr && rhs->Value == 0) {
                        notArg = comp->Left.get();
                        isNot = true;
                    }
                }
            }
            if (isNot && MatchLdLocOut(notArg, v) && v->Type != nullptr &&
                TypeSystem::IsKnownType(*v->Type,
                                         TypeSystem::KnownTypeCode::Boolean)) {
                bool isInitializedInEntryBlock = false;
                for (int i = 0; i < 3; i++) {
                    if (i >= static_cast<int>(
                                 body->EntryPoint()->Instructions.size()))
                        break;
                    auto* stloc = dynamic_cast<StLoc*>(
                        body->EntryPoint()
                            ->Instructions[static_cast<std::size_t>(i)]
                            .get());
                    if (stloc != nullptr && stloc->Variable.get() == v &&
                        MatchLdcI4(stloc->Value.get(), 0)) {
                        isInitializedInEntryBlock = true;
                        break;
                    }
                }
                if (isInitializedInEntryBlock) {
                    skipFinallyBodies_ = v;
                    break;
                }
            }
        }
    }

    PropagateCopiesOfFields(*body);

    // Note: body may contain try-catch or try-finally statements that have
    // nested block containers, but those cannot contain any yield
    // statements. So for reconstructing the control flow, we only consider
    // the blocks directly within body.

    ControlFlow::StateRangeAnalysis rangeAnalysis(
        ControlFlow::StateRangeAnalysisMode::IteratorMoveNext, stateField_,
        /*cachedStateVar=*/nullptr, isCompiledWithLegacyVisualBasic_);
    rangeAnalysis.skipFinallyBodies = skipFinallyBodies_;
    rangeAnalysis.doFinallyBodies = doFinallyBodies_;
    rangeAnalysis.AssignStateRanges(body, Util::LongSet::Universe());
    cachedStateVars_ = rangeAnalysis.CachedStateVars();

    auto newBody = ConvertBody(*body, rangeAnalysis);
    moveNextFunction->Variables.clear();
    // (The C# ReleaseRef drops the old function's references to the moved
    // instructions; the port's unique_ptr tree owns them and the move into
    // newBody already transferred ownership.)
    // The old tree stays alive on the converted function: the clones the
    // ConvertBody produced keep their un-retargeted branch targets (the
    // branches inside cloned nested containers) pointing at this tree's
    // blocks -- the C# GC holds it; the port's KeepAliveFunctions owns it.
    function.KeepAliveFunctions.push_back(std::move(moveNextFunction));
    return newBody;
}

void YieldReturnDecompiler::PropagateCopiesOfFields(BlockContainer& body) {
    // Roslyn may optimize MoveNext() by copying fields from the iterator
    // class into local variables at the beginning of MoveNext(). Undo this
    // optimization. The C# collects the mutable fields
    // (Descendants.OfType<LdFlda> not under an LdObj); the port walks.
    std::vector<const TypeSystem::IField*> mutableFields;
    {
        std::vector<ILInstruction*> stack{&body};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* ldflda = dynamic_cast<LdFlda*>(node)) {
                if (ldflda->Parent == nullptr ||
                    ldflda->Parent->Op != OpCode::LdObj) {
                    if (ldflda->Field != nullptr)
                        mutableFields.push_back(
                            static_cast<const TypeSystem::IField*>(
                                ldflda->Field->MemberDefinition()));
                }
            }
            for (int i = node->ChildCount() - 1; i >= 0; i--) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
    auto isMutable = [&mutableFields](const TypeSystem::IField* f) {
        return std::find(mutableFields.begin(), mutableFields.end(), f) !=
               mutableFields.end();
    };
    // The C# reads `store.Variable.LoadInstructions` (the per-variable load
    // list the port does not maintain, D11/D62/D68): the loads are
    // collected by a walk over the body.
    for (std::size_t i = 0; i < body.EntryPoint()->Instructions.size(); i++) {
        auto* store = dynamic_cast<StLoc*>(
            body.EntryPoint()->Instructions[i].get());
        if (store == nullptr || store->Variable == nullptr ||
            !store->Variable->IsSingleDefinition())
            break;
        auto* ldobj = dynamic_cast<LdObj*>(store->Value.get());
        auto* ldflda =
            ldobj != nullptr && ldobj->Target != nullptr
                ? dynamic_cast<LdFlda*>(ldobj->Target.get())
                : nullptr;
        if (ldflda == nullptr || !MatchLdThis(ldflda->Target.get()))
            break;  // unknown instruction
        const TypeSystem::IField* field =
            ldflda->Field != nullptr
                ? static_cast<const TypeSystem::IField*>(
                      ldflda->Field->MemberDefinition())
                : nullptr;
        if (!isMutable(field)) {
            // perform copy propagation: (unlike
            // CopyPropagation.Propagate(), copy the ldobj arguments as
            // well) -- the loads of the stored variable are replaced with
            // clones of the store's value.
            std::vector<LdLoc*> loads;
            std::vector<ILInstruction*> stack{&body};
            while (!stack.empty()) {
                ILInstruction* node = stack.back();
                stack.pop_back();
                if (auto* ldloc = dynamic_cast<LdLoc*>(node)) {
                    if (ldloc->Variable.get() == store->Variable.get())
                        loads.push_back(ldloc);
                    continue;
                }
                for (int j = node->ChildCount() - 1; j >= 0; j--) {
                    if (ILInstruction* child = node->GetChild(j))
                        stack.push_back(child);
                }
            }
            for (LdLoc* expr : loads) {
                if (expr->Parent != nullptr && store->Value != nullptr) {
                    expr->Parent->SetChild(expr->ChildIndex,
                                           store->Value->Clone());
                }
            }
            body.EntryPoint()->Instructions.erase(
                body.EntryPoint()->Instructions.begin() +
                static_cast<std::ptrdiff_t>(i));
            i--;
        } else if (stateField_ != nullptr && field != nullptr &&
                   field->MemberDefinition() ==
                       stateField_->MemberDefinition()) {
            continue;
        } else {
            break;  // unsupported: load of mutable field (other than state
                   // field)
        }
    }
}


namespace {

// The shared_ptr for a raw variable pointer (the raw views the matchers
// hand back; the owning handle lives in the function's Variables list).
ILVariablePtr FindVariableHandle(ILFunction& function, ILVariable* raw) {
    for (const ILVariablePtr& v : function.Variables) {
        if (v.get() == raw) return v;
    }
    return nullptr;
}

// The ConvertBody state (the C# local functions close over these).
struct ConvertBodyContext {
    YieldReturnDecompiler& self;
    BlockContainer& oldBody;
    std::unique_ptr<BlockContainer> newBody;
    Util::LongDict<Block*> blockStateMap;
    std::vector<const TypeSystem::IMethod*>* finallyMethods = nullptr;
    const TypeSystem::IField* stateField = nullptr;
    const TypeSystem::IField* currentField = nullptr;
    const TypeSystem::IField* disposingField = nullptr;
    bool isCompiledWithMono = false;
    ILVariable* skipFinallyBodies = nullptr;
    ILVariable* doFinallyBodies = nullptr;
    std::vector<StLoc*>* returnStores = nullptr;
    ILTransformContext* context = nullptr;
    // The current block being filled (the C# `newBlock` local the loop
    // reassigns).
    Block* newBlock = nullptr;
};

// The C# `void ReportError(ILInstruction inst)`: ConvertBody is still
// called within the try-catch, so throwing suppresses the conversion.
void ReportError(ILInstruction* inst) {
    std::string message = "ConvertBody error";
    if (auto* invalidBranch = dynamic_cast<InvalidBranch*>(inst))
        message = invalidBranch->Message.value_or(message);
    else if (auto* invalidExpr = dynamic_cast<InvalidExpression*>(inst))
        message = invalidExpr->Message.value_or(message);
    throw ControlFlow::SymbolicAnalysisFailedException(message);
}

// The C# `ILInstruction MakeGoTo(int v)`.
ILInstruction* MakeGoTo(ConvertBodyContext& ctx, int v) {
    Block* targetBlock = nullptr;
    if (ctx.blockStateMap.TryGetValue(v, targetBlock) &&
        targetBlock != nullptr) {
        if (targetBlock->Parent == &ctx.oldBody) {
            auto& blocks = ctx.newBody->Blocks;
            std::size_t idx = static_cast<std::size_t>(
                targetBlock->ChildIndex);
            if (idx < blocks.size())
                return new Branch(blocks[idx].get());
            return new InvalidBranch("Could not find block for state");
        }
        return new Branch(targetBlock);
    }
    ReportError(new InvalidBranch("Could not find block for state " +
                                  std::to_string(v)));
    return nullptr;  // unreachable
}

// The C# `void UpdateBranchTargets(ILInstruction inst)`.
void UpdateBranchTargets(ConvertBodyContext& ctx, ILInstruction* inst) {
    if (auto* branch = dynamic_cast<Branch*>(inst)) {
        if (branch->TargetContainer() == &ctx.oldBody) {
            auto& blocks = ctx.newBody->Blocks;
            std::size_t idx = static_cast<std::size_t>(
                branch->TargetBlock->ChildIndex);
            if (idx < blocks.size())
                branch->TargetBlock = blocks[idx].get();
        }
    } else if (auto* leave = dynamic_cast<Leave*>(inst)) {
        ILInstruction* value = nullptr;
        if (MatchReturn(leave, value)) {
            bool validYieldBreak = false;
            if (value != nullptr && MatchLdcI4(value, 0)) {
                validYieldBreak = true;
            } else if (value != nullptr) {
                ILVariable* v = nullptr;
                if (MatchLdLocOut(value, v) && v != nullptr &&
                    (v->Kind == VariableKind::Local ||
                     v->Kind == VariableKind::StackSlot)) {
                    // The C# checks all of v's stores are `stloc v(0)` and
                    // collects them as return stores (the missing
                    // per-variable list, D11/D62/D68 -- a walk over the old
                    // body collects them).
                    bool allZero = true;
                    std::vector<StLoc*> stores;
                    std::vector<ILInstruction*> stack{&ctx.oldBody};
                    while (!stack.empty()) {
                        ILInstruction* node = stack.back();
                        stack.pop_back();
                        if (auto* stloc = dynamic_cast<StLoc*>(node)) {
                            if (stloc->Variable.get() == v)
                                stores.push_back(stloc);
                            continue;
                        }
                        for (int i = node->ChildCount() - 1; i >= 0; i--) {
                            if (ILInstruction* child = node->GetChild(i))
                                stack.push_back(child);
                        }
                    }
                    for (StLoc* store : stores) {
                        if (store->Value == nullptr ||
                            !MatchLdcI4(store->Value.get(), 0)) {
                            allZero = false;
                            break;
                        }
                    }
                    if (allZero && !stores.empty()) {
                        validYieldBreak = true;
                        ctx.returnStores->insert(ctx.returnStores->end(),
                                                 stores.begin(), stores.end());
                    }
                }
            }
            if (validYieldBreak) {
                // yield break
                leave->Parent->SetChild(
                    leave->ChildIndex,
                    std::unique_ptr<ILInstruction>(
                        new Leave(ctx.newBody.get())));
            } else {
                // don't treat this as an error, it might just be
                // unreachable code that will be removed soon
                leave->Parent->SetChild(
                    leave->ChildIndex,
                    std::unique_ptr<ILInstruction>(new InvalidBranch(
                        "Unexpected return in MoveNext()")));
            }
            // The C# keeps iterating the original leave's children (the GC
            // keeps it alive across the replacement); the port's SetChild
            // destroys the replaced node, so the walk must stop here. The
            // replaced node's only child is the return value (an ldc/ldloc),
            // which never carries branch targets -- skipping it is
            // semantically identical to the C# recursion.
            return;
        } else {
            if (leave->TargetContainer == &ctx.oldBody) {
                leave->TargetContainer = ctx.newBody.get();
            }
        }
    }
    for (int i = 0; i < inst->ChildCount(); i++) {
        if (ILInstruction* child = inst->GetChild(i))
            UpdateBranchTargets(ctx, child);
    }
}

// The C# `Block SplitBlock(Block newBlock, ILInstruction oldInst)`.
Block* SplitBlock(ConvertBodyContext& ctx, ILInstruction* oldInst) {
    if (ctx.newBlock->Instructions.size() > 0) {
        auto newBlock2 = std::make_unique<Block>();
        Block* result = newBlock2.get();
        ctx.newBody->Blocks.push_back(std::move(newBlock2));
        ctx.newBlock->SetFinal(
            std::unique_ptr<ILInstruction>(new Branch(result)));
        ctx.newBlock = result;
    }
    return ctx.newBlock;
}

// The port's position reader: the C# reads the block's instruction list
// (where the reader puts every instruction, terminators included); the
// port's reader carries the terminator in the FinalInstruction slot, so a
// position at the end of the list reads the final.
ILInstruction* InstructionAt(Block* block, int pos) {
    if (block == nullptr)
        return nullptr;
    if (pos >= 0 && pos < static_cast<int>(block->Instructions.size()))
        return block->Instructions[static_cast<std::size_t>(pos)].get();
    if (pos == static_cast<int>(block->Instructions.size()))
        return block->FinalInstruction.get();
    return nullptr;
}

// The C# `void ConvertBranchAfterYieldReturn(Block newBlock, Block
// oldBlock, int pos)`.
void ConvertBranchAfterYieldReturn(ConvertBodyContext& ctx,
                                    Block* oldBlock, int pos) {
    Block* targetBlock = nullptr;
    if (ctx.isCompiledWithMono && ctx.disposingField != nullptr) {
        // Mono skips over the state assignment if 'this.disposing' is set.
        ILInstruction* cond = nullptr;
        ILInstruction* unusedTrue = nullptr;
        ILInstruction* unusedFalse = nullptr;
        if (pos < static_cast<int>(oldBlock->Instructions.size()) &&
            MatchIfInstruction(oldBlock->Instructions[static_cast<std::size_t>(
                                   pos)].get(), cond, unusedTrue, unusedFalse)) {
            ILInstruction* condTarget = nullptr;
            const TypeSystem::IField* condField = nullptr;
            if (MatchLdFld(cond, condTarget, condField) &&
                MatchLdThis(condTarget) && condField != nullptr &&
                condField->MemberDefinition() ==
                    ctx.disposingField->MemberDefinition() &&
                pos + 1 < static_cast<int>(oldBlock->Instructions.size()) &&
                MatchBranch(oldBlock->Instructions[static_cast<std::size_t>(
                                pos + 1)].get(), targetBlock) &&
                targetBlock->Parent == oldBlock->Parent) {
                oldBlock = targetBlock;
                pos = 0;
            }
        }
    }

    // Visual Basic Compiler emits additional stores to variables.
    // (Deferred with the VB arms.)
    int localNewState = 0;
    bool hasLocalNewState = false;
    if (pos < static_cast<int>(oldBlock->Instructions.size())) {
        if (MatchLdcI4(oldBlock->Instructions[static_cast<std::size_t>(pos)]
                           .get(),
                       localNewState)) {
            hasLocalNewState = true;
            pos++;
        }
    }

    int newState = 0;
    if (pos < static_cast<int>(oldBlock->Instructions.size())) {
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        ILInstruction* value = nullptr;
        ILVariable* var = nullptr;
        if (MatchStLoc(oldBlock->Instructions[static_cast<std::size_t>(pos)]
                           .get(), var, value) &&
            MatchLdcI4(value, localNewState) && hasLocalNewState) {
            // the VB local-state form (deferred; unreachable in the
            // non-VB staging)
            pos++;
        } else if (MatchStFld(oldBlock->Instructions[
                                   static_cast<std::size_t>(pos)]
                                   .get(),
                               target, field, value) &&
                   MatchLdThis(target) && field != nullptr &&
                   field->MemberDefinition() ==
                       ctx.stateField->MemberDefinition() &&
                   MatchLdcI4(value, newState) &&
                   (!hasLocalNewState || localNewState == newState)) {
            pos++;
        } else {
            ctx.newBlock->Add(std::unique_ptr<ILInstruction>(
                new InvalidBranch(
                    "Unable to find new state assignment for yield return")));
            ReportError(ctx.newBlock->Instructions.back().get());
            return;
        }
    } else {
        ctx.newBlock->Add(std::unique_ptr<ILInstruction>(
            new InvalidBranch(
                "Unable to find new state assignment for yield return")));
        ReportError(ctx.newBlock->Instructions.back().get());
        return;
    }
    // Mono may have 'br setSkipFinallyBodies' here, so follow the branch
    {
        ILInstruction* at = InstructionAt(oldBlock, pos);
        if (at != nullptr && MatchBranch(at, targetBlock) &&
            targetBlock->Parent == oldBlock->Parent) {
            oldBlock = targetBlock;
            pos = 0;
        }
    }
    {
        ILInstruction* at = InstructionAt(oldBlock, pos);
        ILVariable* var = nullptr;
        ILInstruction* value = nullptr;
        if (at != nullptr && MatchStLoc(at, var, value) &&
            var == ctx.skipFinallyBodies) {
            if (!MatchLdcI4(value, 1)) {
                ReportError(new InvalidBranch(
                    "Unexpected assignment to skipFinallyBodies"));
            }
            pos++;
        }
    }
    {
        ILInstruction* at = InstructionAt(oldBlock, pos);
        ILVariable* var = nullptr;
        ILInstruction* value = nullptr;
        if (at != nullptr && MatchStLoc(at, var, value) &&
            var != nullptr && var->Kind == VariableKind::Local &&
            ctx.doFinallyBodies != nullptr &&
            var->Index == ctx.doFinallyBodies->Index) {
            if (!MatchLdcI4(value, 0)) {
                ReportError(new InvalidBranch(
                    "Unexpected assignment to doFinallyBodies"));
            }
            pos++;
        }
    }

    bool found = false;
    {
        ILInstruction* at = InstructionAt(oldBlock, pos);
        ILInstruction* retVal = nullptr;
        if (at != nullptr && MatchReturn(at, retVal) &&
            MatchLdcI4(retVal, 1)) {
            found = true;  // OK, found return directly after state assignment
        } else if (at != nullptr && MatchBranch(at, targetBlock) &&
                   targetBlock->Parent == oldBlock->Parent) {
            // The C# reads the target's first instruction; the port's
            // convention may hold the ret in the target's FinalInstruction.
            ILInstruction* targetAt =
                !targetBlock->Instructions.empty()
                    ? targetBlock->Instructions[0].get()
                    : targetBlock->FinalInstruction.get();
            ILInstruction* retVal2 = nullptr;
            if (targetAt != nullptr && MatchReturn(targetAt, retVal2) &&
                MatchLdcI4(retVal2, 1)) {
                found = true;  // OK, jump to common return block (e.g. on Mono)
            }
        }
    }
    if (!found) {
        ctx.newBlock->Add(std::unique_ptr<ILInstruction>(new InvalidBranch(
            "Unable to find 'return true' for yield return")));
        ReportError(ctx.newBlock->Instructions.back().get());
        return;
    }
    ctx.newBlock->SetFinal(
        std::unique_ptr<ILInstruction>(MakeGoTo(ctx, newState)));
}

} // namespace

std::unique_ptr<BlockContainer> YieldReturnDecompiler::ConvertBody(
    BlockContainer& oldBody, ControlFlow::StateRangeAnalysis& rangeAnalysis) {
    ConvertBodyContext ctx{*this, oldBody, nullptr,
                           rangeAnalysis.GetBlockStateSetMapping(oldBody),
                           nullptr, stateField_, currentField_, disposingField_,
                           isCompiledWithMono_, skipFinallyBodies_,
                           doFinallyBodies_, &returnStores_, context_};
    ctx.newBody = std::make_unique<BlockContainer>();
    // create all new blocks so that they can be referenced by gotos
    for (std::size_t blockIndex = 0; blockIndex < oldBody.Blocks.size();
         blockIndex++) {
        auto block = std::make_unique<Block>();
        ctx.newBody->Blocks.push_back(std::move(block));
    }
    // convert contents of blocks
    for (std::size_t i = 0; i < oldBody.Blocks.size(); i++) {
        Block* oldBlock = oldBody.Blocks[i].get();
        ctx.newBlock = ctx.newBody->Blocks[i].get();
        // Set when the yield-return arm consumed the block's remainder (the
        // terminator included): its final must not be cloned (the arm itself
        // installs the branch to the next state as the final).
        bool brokeEarly = false;
        for (auto& oldInst : oldBlock->Instructions) {
            // The state-field store / current-field store / finally-method
            // call arms.
            ILInstruction* target = nullptr;
            const TypeSystem::IField* field = nullptr;
            ILInstruction* value = nullptr;
            if (MatchStFld(oldInst.get(), target, field, value) &&
                MatchLdThis(target)) {
                if (stateField_ != nullptr && field != nullptr &&
                    field->MemberDefinition() ==
                        stateField_->MemberDefinition()) {
                    int newState = 0;
                    if (MatchLdcI4(value, newState)) {
                        // On state change, break up the block: (this
                        // allows us to consider each block individually for
                        // try-finally reconstruction)
                        ctx.newBlock = SplitBlock(ctx, oldInst.get());
                    } else {
                        ctx.newBlock->Add(std::unique_ptr<ILInstruction>(
                            new InvalidExpression(
                                "Assigned non-constant to iterator.state "
                                "field")));
                        ReportError(ctx.newBlock->Instructions.back().get());
                        continue;  // don't copy over this instruction
                    }
                } else if (currentField_ != nullptr && field != nullptr &&
                           field->MemberDefinition() ==
                               currentField_->MemberDefinition()) {
                    // create yield return
                    ctx.newBlock->Add(std::unique_ptr<ILInstruction>(
                        new YieldReturn(value != nullptr ? value->Clone()
                                                         : nullptr)));
                    ConvertBranchAfterYieldReturn(
                        ctx, oldBlock, oldInst->ChildIndex + 1);
                    brokeEarly = true;
                    break;  // we're done with this basic block
                }
            } else if (auto* call = dynamic_cast<Call*>(oldInst.get())) {
                bool isFinallyMethod = false;
                if (!call->IsNewObj && call->Arguments.size() == 1 &&
                    MatchLdThis(call->Arguments[0].get()) &&
                    call->Method != nullptr) {
                    const TypeSystem::IMethod* def =
                        dynamic_cast<const TypeSystem::IMethod*>(
                            call->Method->MemberDefinition());
                    isFinallyMethod = def != nullptr &&
                                      finallyMethodToStateRange_.count(def) != 0;
                }
                if (isFinallyMethod) {
                    // Break up the basic block on a call to a finally
                    // method (this allows us to consider each block
                    // individually for try-finally reconstruction)
                    ctx.newBlock = SplitBlock(ctx, oldInst.get());
                }
            }
            // (The Mono/VB try-finally recursion arm is deferred with the
            // Mono/VB discriminators; the legacyVB if-arm likewise.)
            // copy over the instruction to the new block
            ctx.newBlock->Add(oldInst->Clone());
            UpdateBranchTargets(ctx, ctx.newBlock->Instructions.back().get());
        }
        // The C# clones the terminator from the block's instruction list
        // (the C# reader puts it in the list); the port's convention carries
        // it in the FinalInstruction slot, so a block whose loop completed
        // (the yield-return path breaks out earlier) also clones its final
        // -- the state-dispatch switch, the `return false` leaves, and the
        // intra-state branches all ride here.
        if (!brokeEarly && oldBlock->FinalInstruction != nullptr) {
            auto clonedFinal = oldBlock->FinalInstruction->Clone();
            ILInstruction* clonedFinalRaw = clonedFinal.get();
            ctx.newBlock->SetFinal(std::move(clonedFinal));
            UpdateBranchTargets(ctx, clonedFinalRaw);
        }
    }

    // Insert new artificial block as entry point, and jump to the initial
    // state. This causes the method to start directly at the first user
    // code, and the whole compiler-generated state-dispatching logic
    // becomes unreachable code and gets deleted.
    int initialState = isCompiledWithLegacyVisualBasic_ ? -1 : 0;
    {
        auto entryBlock = std::make_unique<Block>();
        entryBlock->SetFinal(
            std::unique_ptr<ILInstruction>(MakeGoTo(ctx, initialState)));
        ctx.newBody->Blocks.insert(ctx.newBody->Blocks.begin(),
                                   std::move(entryBlock));
        // The raw vector insert shifts every block's position; keep the
        // ChildIndex back-pointers consistent (the C# BlockCollection
        // maintains them on insert) -- the try-finally reconstruction
        // indexes the per-block state array by ChildIndex.
        for (std::size_t bi = 0; bi < ctx.newBody->Blocks.size(); bi++) {
            ctx.newBody->Blocks[bi]->Parent = ctx.newBody.get();
            ctx.newBody->Blocks[bi]->ChildIndex = static_cast<int>(bi);
        }
    }
    return std::move(ctx.newBody);
}

// The C# `bool IsStateAssignment(ILInstruction inst)` (line 1376): a store
// of a constant to the state field.
bool YieldReturnDecompiler::IsStateAssignment(ILInstruction* inst) const {
    ILInstruction* target = nullptr;
    const TypeSystem::IField* field = nullptr;
    ILInstruction* value = nullptr;
    if (!MatchStFld(inst, target, field, value) || !MatchLdThis(target))
        return false;
    return field != nullptr &&
           field->MemberDefinition() == stateField_;
}

// The C# `int? GetNewState(Block block)` (lines 1380-1396): the state the
// block transitions to -- a leading state store, or (for a nested
// try-finally) a call to an already-decompiled finally method.
std::optional<int> YieldReturnDecompiler::GetNewState(Block* block) const {
    if (block->Instructions.empty())
        return std::nullopt;
    ILInstruction* first = block->Instructions[0].get();
    ILInstruction* target = nullptr;
    const TypeSystem::IField* field = nullptr;
    ILInstruction* value = nullptr;
    int newState = 0;
    if (MatchStFld(first, target, field, value) && MatchLdThis(target) &&
        field != nullptr &&
        field->MemberDefinition() == stateField_ &&
        value != nullptr && MatchLdcI4(value, newState)) {
        return newState;
    }
    if (auto* call = dynamic_cast<Call*>(first)) {
        if (call->Arguments.size() == 1 &&
            MatchLdThis(call->Arguments[0].get()) &&
            call->Method != nullptr) {
            const TypeSystem::IMethod* def = dynamic_cast<
                const TypeSystem::IMethod*>(call->Method->MemberDefinition());
            auto it = decompiledFinallyMethods_.find(def);
            if (it != decompiledFinallyMethods_.end())
                return it->second.first;
        }
    }
    return std::nullopt;
}

// The C# `void DecompileFinallyBlocks()` (lines 1211-1232).
void YieldReturnDecompiler::DecompileFinallyBlocks(
    ILTransformContext& context) {
    for (auto& entry : finallyMethodToStateRange_) {
        const TypeSystem::IMethod* method = entry.first;
        auto function = CreateILAstLocal(method->MetadataToken(), context);
        auto* body = dynamic_cast<BlockContainer*>(function->Body.get());
        if (body == nullptr)
            throw ControlFlow::SymbolicAnalysisFailedException(
                "finally method body is not a container");
        std::optional<int> newState = GetNewState(body->EntryPoint());
        if (newState.has_value())
            body->EntryPoint()->RemoveInstructionAt(0);
        // Avoid yield-return decompilation if there are unrecognized state
        // assignments in a finally method.
        std::vector<ILInstruction*> stack{function->Body.get()};
        while (!stack.empty()) {
            ILInstruction* inst = stack.back();
            stack.pop_back();
            if (IsStateAssignment(inst))
                throw ControlFlow::SymbolicAnalysisFailedException(
                    "Unknown state transition in finally at IL_" +
                    Disassembler::OffsetToString(
                        static_cast<int>(inst->StartILOffset)));
            for (int i = 0; i < inst->ChildCount(); i++) {
                if (ILInstruction* child = inst->GetChild(i))
                    stack.push_back(child);
            }
        }
        decompiledFinallyMethods_[method] = {newState, std::move(function)};
    }
}

// The C# FindFinallyMethod local (lines 1359-1374): the finally method whose
// state range contains the state.
const TypeSystem::IMethod* YieldReturnDecompiler::FindFinallyMethod(
    int state) const {
    const TypeSystem::IMethod* foundMethod = nullptr;
    for (auto& entry : finallyMethodToStateRange_) {
        std::string rangesStr;
        for (const auto& iv : entry.second.Intervals())
            rangesStr += "[" + std::to_string(iv.Start) + ".." +
                        std::to_string(iv.InclusiveEnd()) + "]";
        if (entry.second.Contains(state)) {
            if (foundMethod == nullptr)
                foundMethod = entry.first;
            else
                return nullptr;  // ambiguous (the C# Debug.Fail)
        }
    }
    return foundMethod;
}

// The C# `void ReconstructTryFinallyBlocks(ILFunction iteratorFunction)`
// (lines 1237-1374). Precondition: the blocks in newBody are topologically
// sorted (the C# SortBlocks(deleteUnreachableBlocks: true) ran before this).
void YieldReturnDecompiler::ReconstructTryFinallyBlocks(
    ILFunction& iteratorFunction, ILTransformContext& context) {
    (void)context;
    auto* newBody = dynamic_cast<BlockContainer*>(iteratorFunction.Body.get());
    if (newBody == nullptr)
        return;
    context_ != nullptr ? (void)0 : (void)0;

    // stateToContainer lives across the CreateTryBlock closure (the C#
    // local function pair); the port passes it explicitly.
    std::map<int, BlockContainer*> stateToContainer;

    // The C# CreateTryBlock local (lines 1328-1357): wrap the block's
    // contents in a TryFinally over the matching finally body.
    auto CreateTryBlock = [&](Block* block, int state) {
        const TypeSystem::IMethod* finallyMethod = FindFinallyMethod(state);
        if (finallyMethod != nullptr) {
            // remove the method so that it doesn't cause ambiguity when
            // processing nested try-finally blocks
            finallyMethodToStateRange_.erase(finallyMethod);
        }

        auto tryBlock = std::make_unique<Block>();
        tryBlock->StartILOffset = block->StartILOffset;
        tryBlock->EndILOffset = block->EndILOffset;
        for (auto& inst : block->Instructions)
            tryBlock->Add(std::move(inst));
        // The C# AddRange copies the whole instruction list (the terminator
        // included -- the C# list holds it); the port's convention carries
        // it in the FinalInstruction slot, so it moves with the rest.
        if (block->FinalInstruction != nullptr)
            tryBlock->SetFinal(std::move(block->FinalInstruction));
        auto tryBlockContainer = std::make_unique<BlockContainer>();
        BlockContainer* tryContainerRaw = tryBlockContainer.get();
        tryBlockContainer->AddBlock(std::move(tryBlock));
        stateToContainer.emplace(state, tryContainerRaw);

        std::unique_ptr<ILInstruction> finallyBlock;
        if (finallyMethod == nullptr) {
            finallyBlock = std::unique_ptr<ILInstruction>(new InvalidBranch(
                "Could not find finallyMethod for state=" +
                std::to_string(state) +
                ".\nPossibly this method is affected by a C# compiler bug "
                "that causes the finally body\nnot to run in case of an "
                "exception or early 'break;' out of a loop consuming this "
                "iterable."));
        } else {
            auto it = decompiledFinallyMethods_.find(finallyMethod);
            if (it != decompiledFinallyMethods_.end()) {
                // The C# splices the decompiled function's body out (the
                // function keeps its variables; they move to the iterator
                // function).
                ILFunction& finallyFunction = *it->second.second;
                finallyBlock = std::move(finallyFunction.Body);
                for (auto& v : finallyFunction.Variables)
                    iteratorFunction.RegisterExistingVariable(v);
                finallyFunction.Variables.clear();
            } else {
                finallyBlock = std::unique_ptr<ILInstruction>(
                    new InvalidBranch("Missing decompiledFinallyMethod"));
            }
        }

        block->Instructions.clear();
        block->Add(std::make_unique<TryFinally>(std::move(tryBlockContainer),
                                                std::move(finallyBlock)));
        block->RenumberChildren();
    };

    std::vector<int> blockState(newBody->Blocks.size(), 0);
    blockState[0] = -1;
    stateToContainer.emplace(-1, newBody);
    // First, analyse the newBody: for each block, determine the active state
    // number.
    for (std::size_t i = 0; i < newBody->Blocks.size(); i++) {
        Block* block = newBody->Blocks[i].get();
        int oldState = blockState[static_cast<std::size_t>(block->ChildIndex)];
        BlockContainer* container = nullptr;  // new container for the block
        int newState = 0;
        std::optional<int> newStateOpt = GetNewState(block);
        if (newStateOpt.has_value()) {
            // OK, state change
            // Remove the state-changing instruction
            block->RemoveInstructionAt(0);
            newState = *newStateOpt;
            auto it = stateToContainer.find(newState);
            if (it == stateToContainer.end()) {
                // First time we see this state.
                // This means we just found the entry point of a try block.
                CreateTryBlock(block, newState);
                // CreateTryBlock() wraps the contents of 'block' with a
                // TryFinally. We thus need to put the block (which now
                // contains the whole TryFinally) into the parent container.
                // Assuming a state transition never enters more than one
                // state at once, we can use stateToContainer[oldState] as
                // parent.
                container = stateToContainer[oldState];
            } else {
                container = it->second;
            }
        } else {
            // Because newBody is topologically sorted we because we just
            // removed unreachable code, we can assume that blockState[] was
            // already set for this block.
            newState = oldState;
            container = stateToContainer[oldState];
        }
        if (container != nullptr && container != newBody) {
            // Move the block into the container. The C# BlockCollection.Add
            // re-parents; the port's raw push_back must do it explicitly
            // (the C# RemoveAll then drops the moved blocks by the Parent
            // check).
            for (auto& owned : newBody->Blocks) {
                if (owned.get() == block) {
                    block->Parent = container;
                    block->ChildIndex =
                        static_cast<int>(container->Blocks.size());
                    container->Blocks.push_back(std::move(owned));
                    break;
                }
            }
            // Keep the stale reference in newBody.Blocks for now, to avoid
            // changing the ChildIndex of the other blocks while we use it
            // to index the blockState array.
        }
        // Propagate newState to successor blocks
        std::vector<ILInstruction*> stack{block};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* branch = dynamic_cast<Branch*>(node)) {
                if (branch->TargetBlock != nullptr &&
                    branch->TargetBlock->Parent == newBody) {
                    int stateAfterBranch = newState;
                    // The C# `Block.GetPredecessor(branch) is Call call`:
                    // pre-roslyn compiles "yield break;" into "Dispose();
                    // goto return_false;", so convert the dispose call into
                    // a state transition to the final state.
                    ILInstruction* pred = nullptr;
                    if (branch->Parent != nullptr &&
                        dynamic_cast<Block*>(branch->Parent) != nullptr &&
                        branch->ChildIndex > 0) {
                        pred = dynamic_cast<Block*>(branch->Parent)
                                   ->Instructions[static_cast<std::size_t>(
                                       branch->ChildIndex - 1)]
                                   .get();
                    }
                    if (auto* call = dynamic_cast<Call*>(pred)) {
                        std::uint32_t callToken =
                            call->Method != nullptr
                                ? call->Method->MetadataToken()
                                : call->MethodToken;
                        if (call->Arguments.size() == 1 &&
                            MatchLdThis(call->Arguments[0].get()) &&
                            callToken == disposeMethod_) {
                            stateAfterBranch = -1;
                            call->ReplaceWith(std::unique_ptr<ILInstruction>(
                                new Nop()));
                        }
                    }
                    blockState[static_cast<std::size_t>(
                        branch->TargetBlock->ChildIndex)] = stateAfterBranch;
                }
            }
            for (int ci = 0; ci < node->ChildCount(); ci++) {
                if (ILInstruction* child = node->GetChild(ci))
                    stack.push_back(child);
            }
        }
    }
    // newBody.Blocks.RemoveAll(b => b.Parent != newBody)
    newBody->Blocks.erase(
        std::remove_if(newBody->Blocks.begin(), newBody->Blocks.end(),
                       [newBody](const std::unique_ptr<Block>& b) {
                           return !b || b->Parent != newBody;
                       }),
        newBody->Blocks.end());
    // The moved blocks kept stale (null) unique_ptrs in newBody.Blocks; the
    // C# RemoveAll drops them by the Parent check -- the port's moved-from
    // slots are null.
    for (std::size_t i = 0; i < newBody->Blocks.size(); i++) {
        newBody->Blocks[i]->Parent = newBody;
        newBody->Blocks[i]->ChildIndex = static_cast<int>(i);
    }
}

void YieldReturnDecompiler::TranslateFieldsToLocalAccess(
    ILFunction& function, ILInstruction* inst,
    std::map<const TypeSystem::IField*, ILVariable*>& fieldToVariableMap,
    bool isCompiledWithMono) {
    auto* ldflda = dynamic_cast<LdFlda*>(inst);
    if (ldflda != nullptr && MatchLdThis(ldflda->Target.get())) {
        const TypeSystem::IField* fieldDef =
            ldflda->Field != nullptr
                ? static_cast<const TypeSystem::IField*>(
                      ldflda->Field->MemberDefinition())
                : nullptr;
        if (fieldDef == nullptr)
            return;
        auto it = fieldToVariableMap.find(fieldDef);
        ILVariable* v = nullptr;
        if (it != fieldToVariableMap.end()) {
            v = it->second;
        } else {
            std::string name;
            const std::string& fieldName = fieldDef->Name();
            if (!fieldName.empty() && fieldName[0] == '<') {
                std::size_t pos = fieldName.find('>');
                if (pos > 1)
                    name = fieldName.substr(1, pos - 1);
            }
            ILVariablePtr registered = function.RegisterVariable(
                VariableKind::Local, nullptr, name);
            // The C# reads ldflda.Field.ReturnType for the variable's
            // type; the resolved field's type rides the variable
            // registration through the type override below.
            registered->InitialValueIsInitialized = true;
            registered->UsesInitialValue = true;
            registered->StateMachineField = ldflda->Field.get();
            v = registered.get();
            function.RegisterExistingVariable(registered);
            fieldToVariableMap.emplace(fieldDef, v);
        }
        if (v != nullptr && inst->Parent != nullptr) {
            ILVariablePtr shared = FindVariableHandle(function, v);
            if (shared != nullptr) {
                if (v->StackType() == StackType::Ref) {
                    inst->Parent->SetChild(
                        inst->ChildIndex,
                        std::unique_ptr<ILInstruction>(new LdLoc(shared)));
                } else {
                    inst->Parent->SetChild(
                        inst->ChildIndex,
                        std::unique_ptr<ILInstruction>(new LdLoca(shared)));
                }
            }
        }
    } else if (!isCompiledWithMono && MatchLdThis(inst)) {
        if (inst->Parent != nullptr) {
            auto replacement =
                std::make_unique<InvalidExpression>("stateMachine");
            replacement->ExpectedResultType = inst->ResultType();
            inst->Parent->SetChild(inst->ChildIndex,
                                   std::move(replacement));
        }
    } else {
        for (int i = 0; i < inst->ChildCount(); i++) {
            if (ILInstruction* child = inst->GetChild(i))
                TranslateFieldsToLocalAccess(function, child,
                                              fieldToVariableMap,
                                              isCompiledWithMono);
        }
        auto* ldobj = dynamic_cast<LdObj*>(inst);
        if (ldobj != nullptr && ldobj->Target != nullptr) {
            if (auto* ldloca =
                    dynamic_cast<LdLoca*>(ldobj->Target.get())) {
                if (ldloca->Variable != nullptr &&
                    ldloca->Variable->StateMachineField != nullptr) {
                    inst->Parent->SetChild(
                        inst->ChildIndex,
                        std::unique_ptr<ILInstruction>(
                            new LdLoc(ldloca->Variable)));
                }
            }
        } else if (auto* stobj = dynamic_cast<StObj*>(inst)) {
            if (stobj->Target != nullptr) {
                if (auto* ldloca2 =
                        dynamic_cast<LdLoca*>(stobj->Target.get())) {
                    if (ldloca2->Variable != nullptr &&
                        ldloca2->Variable->StateMachineField != nullptr) {
                        inst->Parent->SetChild(
                            inst->ChildIndex,
                            std::unique_ptr<ILInstruction>(new StLoc(
                                ldloca2->Variable, std::move(stobj->Value))));
                    }
                }
            }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
