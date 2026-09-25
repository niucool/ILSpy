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
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/SplitVariables.hpp"
#include "Decompiler/Metadata/CodeMappingInfo.hpp"  // IsCompilerGeneratorEnumerator
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // IsKnownType

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

// The C# `internal static ILFunction CreateILAst(MethodDefinitionHandle
// method, ILTransformContext context)`: the body decode + the early
// transform list (the aggressivelyDuplicateReturnBlocks form) + the port's
// field resolution. The decode routes through the context's
// DelegateBodyResolver hook (the CreateILReader bridge).
std::unique_ptr<ILFunction> CreateILAst(std::uint32_t method,
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
    // The field resolution (the reader's deferred surface; see the helper).
    if (context.TypeSystem != nullptr) {
        if (const auto* module = dynamic_cast<const TypeSystem::MetadataModule*>(
                &context.TypeSystem->MainModule())) {
            if (il->Body != nullptr)
                ResolveFields(il->Body.get(), *module);
        }
    }
    return il;
}

} // namespace

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

    if (!MatchEnumeratorCreationPattern(function, context))
        return;
    try {
        AnalyzeCtor(context);
        AnalyzeCurrentProperty(context);
        ResolveIEnumerableIEnumeratorFieldMapping(context);
        ConstructExceptionTable(context);
    } catch (const ControlFlow::SymbolicAnalysisFailedException&) {
        // The C# adds a warning and leaves the state machine as-is.
        return;
    }
    // SLICE STATE (parts 3-4): AnalyzeMoveNext / the body rewrite / the
    // try-finally reconstruction are not ported yet; the transform has
    // matched and analyzed but does not rewrite.
}

bool YieldReturnDecompiler::MatchEnumeratorCreationPattern(
    ILFunction& function, ILTransformContext& context) {
    Block* body = SingleBlock(function.Body.get());
    if (body == nullptr || body->Instructions.empty()) {
        return false;
    }

    ILInstruction* newObj = nullptr;
    if (body->Instructions.size() == 1) {
        // No parameters passed to enumerator (not even 'this'):
        // ret(newobj(...))
        ILInstruction* value = nullptr;
        if (!MatchReturn(body->Instructions[0].get(), value))
            return false;
        newObj = value;
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

    std::size_t pos = 0;

    // stloc(var_1, newobj(..))
    ILVariable* var1 = nullptr;
    if (!MatchStLoc(body->Instructions[pos].get(), var1, newObj))
        return false;
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
        if (MatchLdLoc(value, parameter) &&
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

    // In debug builds, the compiler may copy the var1 into another variable
    // (var2) before returning it.
    ILVariable* var2 = nullptr;
    {
        ILVariable* v2 = nullptr;
        ILInstruction* ldlocForStloc2 = nullptr;
        if (MatchStLoc(body->Instructions[pos].get(), v2, ldlocForStloc2) &&
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
        if (MatchStFld(body->Instructions[pos].get(), target, field, value) &&
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
    if (MatchReturn(body->Instructions[pos].get(), retVal) &&
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
    if (newObj->Method == nullptr)
        return false;
    std::uint32_t handle = newObj->Method->MetadataToken();
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
    if (newObj->Method == nullptr)
        return false;
    std::uint32_t handle = newObj->Method->MetadataToken();
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
    auto il = CreateILAst(enumeratorCtor_, context);
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
            MatchLdThis(target) && MatchLdLoc(value, arg) &&
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
    auto il = CreateILAst(getCurrentMethod, context);
    Block* body = SingleBlock(il->Body.get());
    if (body == nullptr)
        throw ControlFlow::SymbolicAnalysisFailedException(
            "get_Current has no body");
    if (body->Instructions.size() == 1) {
        // release builds directly return the current field
        // ret(ldfld F(ldloc(this)))
        ILInstruction* retVal = nullptr;
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        if (MatchReturn(body->Instructions[0].get(), retVal) &&
            MatchLdFld(retVal, target, field) && MatchLdThis(target)) {
            currentField_ = field != nullptr
                                ? static_cast<const TypeSystem::IField*>(
                                      field->MemberDefinition())
                                : nullptr;
        }
    } else if (body->Instructions.size() == 2) {
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
            MatchReturn(body->Instructions[1].get(), retVal) &&
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
    auto function = CreateILAst(getEnumeratorMethod, context);
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
    auto function = CreateILAst(disposeMethod_, context);

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

} // namespace ILSpy::Decompiler::IL
