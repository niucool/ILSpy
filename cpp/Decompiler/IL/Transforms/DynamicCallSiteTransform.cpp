// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The dynamic-callsite transform (see the .hpp for the port adaptations).

#include "Decompiler/IL/Transforms/DynamicCallSiteTransform.hpp"

#include "Decompiler/IL/ControlFlow/YieldReturnDecompiler.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Transforms/TransformArrayInitializers.hpp"
#include "Decompiler/IL/Transforms/TransformExpressionTrees.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cstdio>
#include <set>

namespace ILSpy::Decompiler::IL {

namespace {

constexpr const char* kCallSiteTypeName =
    "System.Runtime.CompilerServices.CallSite";
constexpr const char* kCSharpBinderTypeName =
    "Microsoft.CSharp.RuntimeBinder.Binder";
constexpr const char* kCSharpArgumentInfoTypeName =
    "Microsoft.CSharp.RuntimeBinder.CSharpArgumentInfo";

// The method-name tail of a reader-resolved call.
std::string MethodNameTail(const std::string& methodName) {
    std::size_t pos = methodName.rfind("::");
    return pos == std::string::npos ? methodName
                                    : methodName.substr(pos + 2);
}

// The metadata type name without the arity suffix (the `NameWithoutArity`
// helper shape).
std::string NameWithoutArity(const std::string& name) {
    std::size_t pos = name.rfind('`');
    if (pos == std::string::npos) return name;
    for (std::size_t i = pos + 1; i < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9')
            return name;  // not an arity suffix
    }
    return name.substr(0, pos);
}

// The C# `IType.FullName` (AbstractType.FullName: namespace + "." + Name,
// with the arity tick stripped from the name): the port's name-only types
// carry the tick in Name().
std::string TypeFullName(const TypeSystem::IType* type) {
    if (type == nullptr) return {};
    std::string ns = type->Namespace();
    std::string name = NameWithoutArity(type->Name());
    return ns.empty() ? name : ns + "." + name;
}

// The C# `IType.TypeArguments` (an empty list on non-parameterized types);
// the port carries the list on ParameterizedType only.
const std::vector<TypeSystem::ITypePtr>& TypeArgumentsView(
    const TypeSystem::IType* type) {
    static const std::vector<TypeSystem::ITypePtr> kEmpty;
    if (auto* parameterized =
            const_cast<TypeSystem::ParameterizedType*>(
                dynamic_cast<const TypeSystem::ParameterizedType*>(type)))
        return parameterized->TypeArguments();
    return kEmpty;
}

// The C# resolves the callsite delegate through the type system (a real
// Func/Action/... definition, Kind == Delegate); this port's deferred-
// resolution types are name-only, so the Roslyn callsite delegate families
// (Func/Action -- the delegate types the compiler-generated CallSite<T>
// caches carry) are recognized by arity-stripped name.
bool IsDelegateType(const TypeSystem::IType* type) {
    if (type == nullptr) return false;
    if (type->Kind() == TypeSystem::TypeKind::Delegate) return true;
    std::string name = NameWithoutArity(type->Name());
    if (type->Namespace() == "System" &&
        (name == "Func" || name == "Action" || name == "Predicate" ||
         name == "Comparison" || name == "Converter" || name == "EventHandler"))
        return true;
    return false;
}

// File-local out-form matchers (the shared forms are match-against; the
// D66/D94 discipline).
bool MatchLdLocOut(ILInstruction* inst, ILVariable*& variable) {
    variable = nullptr;
    if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
        variable = ldloc->Variable.get();
        return true;
    }
    return false;
}

bool MatchLdcI4Out(ILInstruction* inst, int& value) {
    value = 0;
    if (auto* ldc = dynamic_cast<LdcI4*>(inst)) {
        value = ldc->Value;
        return true;
    }
    return false;
}

// The C# `MatchStLoc(variable, out value)` match-against-variable form.
bool MatchStLocOf(ILInstruction* inst, const ILVariable* variable,
                  ILInstruction*& value) {
    value = nullptr;
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc == nullptr || stloc->Variable.get() != variable)
        return false;
    value = stloc->Value.get();
    return true;
}

// The C# `MatchStLoc(out variable, out value)` report form.
bool MatchStLocReport(ILInstruction* inst, ILVariable*& variable,
                      ILInstruction*& value) {
    variable = nullptr;
    value = nullptr;
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc == nullptr) return false;
    variable = stloc->Variable.get();
    value = stloc->Value.get();
    return true;
}

bool MatchLdStrLocal(ILInstruction* inst, std::string& value) {
    value.clear();
    if (auto* ldstr = dynamic_cast<LdStr*>(inst)) {
        value = ldstr->Value;
        return true;
    }
    return false;
}

// The C# `IsSingleDefinitionTemporary` (lines 172-182): a single-definition
// stack slot, or a single-definition local spilled from a state-machine
// field (the async hoisting shape).
bool IsSingleDefinitionTemporary(const ILVariable* variable) {
    if (variable == nullptr) return false;
    if (!variable->IsSingleDefinition()) return false;
    return variable->Kind == VariableKind::StackSlot ||
           (variable->Kind == VariableKind::Local &&
            variable->StateMachineField != nullptr);
}

// The port's position reader (the C# instruction list includes the
// terminators; the port carries the terminator in the FinalInstruction slot).
ILInstruction* BlockInstructionAt(Block* block, int pos) {
    if (block == nullptr) return nullptr;
    if (pos >= 0 && pos < static_cast<int>(block->Instructions.size()))
        return block->Instructions[static_cast<std::size_t>(pos)].get();
    if (pos == static_cast<int>(block->Instructions.size()))
        return block->FinalInstruction.get();
    return nullptr;
}

// The block's next block in its container (the implicit fall-through).
Block* NextBlock(Block* block) {
    auto* parent = dynamic_cast<BlockContainer*>(block->Parent);
    if (parent == nullptr || block->ChildIndex < 0) return nullptr;
    std::size_t next =
        static_cast<std::size_t>(block->ChildIndex) + 1;
    if (next >= parent->Blocks.size()) return nullptr;
    return parent->Blocks[next].get();
}

} // namespace

void DynamicCallSiteTransform::Run(ILFunction& function,
                                    ILTransformContext& context) {
    if (!context.Settings.Dynamic)
        return;

    context_ = &context;
    callsites_.clear();

    // The port's deferred-resolution convention: the matchers read field and
    // method identities from the driver-decoded body, so the reader surfaces
    // resolve first (the CreateILAst / AsyncAwait precedent).
    YieldReturnDecompiler::ResolveReaderSurfaces(function, context);

    std::vector<Block*> blocks;
    {
        std::vector<ILInstruction*> stack{function.Body.get()};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* b = dynamic_cast<Block*>(node))
                blocks.push_back(b);
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
    for (Block* block : blocks)
        FindDynamicCallSitesInBlock(block);

    std::vector<BlockContainer*> modifiedContainers;
    if (auto* body = dynamic_cast<BlockContainer*>(function.Body.get()))
        TransformCallSites(body, modifiedContainers);

    for (auto* container : modifiedContainers)
        container->SortBlocks(true);
    if (context_ != nullptr) {
    }
}

void DynamicCallSiteTransform::FindDynamicCallSitesInBlock(Block* block) {
    // Check if we deal with a callsite cache field null check:
    // if (comp(ldsfld <>p__3 == ldnull)) br IL_000c
    // br IL_002b
    // The C# keeps the if in the instruction list with an explicit
    // fall-through branch after it; this port's reader carries the if in the
    // FinalInstruction slot with the fall-through to the next block implicit.
    if (block->FinalInstruction == nullptr ||
        block->Instructions.size() + 1 < 2)
        return;
    auto* ifInst = dynamic_cast<IfInstruction*>(block->FinalInstruction.get());
    if (ifInst == nullptr)
        return;
    const TypeSystem::IField* callSiteCacheField = nullptr;
    TypeSystem::ITypePtr callSiteDelegate;
    bool invertBranches = false;
    if (!MatchCallSiteCacheNullCheck(ifInst->Condition.get(),
                                     callSiteCacheField, callSiteDelegate,
                                     invertBranches)) {
        return;
    }
    auto* trueBranch = dynamic_cast<Branch*>(ifInst->TrueInst.get());
    if (trueBranch == nullptr) {
        return;
    }
    Block* trueBlock = trueBranch->TargetBlock;
    // The C#'s `branchAfterInit` (the explicit branch after the if) is this
    // port's fall-through: the next block in the container.
    Block* nextBlock = NextBlock(block);
    if (nextBlock == nullptr) {
        return;
    }
    Block* callSiteInitBlock = nullptr;
    Block* targetBlockAfterInit = nullptr;
    if (invertBranches) {
        callSiteInitBlock = nextBlock;
        targetBlockAfterInit = trueBlock;
    } else {
        callSiteInitBlock = trueBlock;
        targetBlockAfterInit = nextBlock;
    }
    CallSiteInfo callSiteInfo;
    Block* blockAfterInit = nullptr;
    if (!ScanCallSiteInitBlock(callSiteInitBlock, callSiteCacheField,
                               callSiteDelegate, callSiteInfo,
                               blockAfterInit)) {
        return;
    }
    if (targetBlockAfterInit != blockAfterInit) {
        return;
    }
    callSiteInfo.DelegateType = callSiteDelegate;
    // The C# stores the IfInstruction (the conditional jump into the init
    // block); the port's reader carries it as the block's final.
    callSiteInfo.ConditionalJumpToInit =
        dynamic_cast<Branch*>(ifInst->TrueInst.get());
    (void)trueBranch;
    callSiteInfo.IfFinal = ifInst;
    callSiteInfo.Inverted = invertBranches;
    callSiteInfo.InitBlock = callSiteInitBlock;
    callsites_[callSiteCacheField] = callSiteInfo;
}

bool DynamicCallSiteTransform::MatchCallSiteCacheNullCheck(
    ILInstruction* condition, const TypeSystem::IField*& callSiteCacheField,
    TypeSystem::ITypePtr& callSiteDelegate, bool& invertBranches) {
    callSiteCacheField = nullptr;
    callSiteDelegate = nullptr;
    invertBranches = false;
    ILInstruction* argument = nullptr;
    // The C# matches comp(x == ldnull) / comp(x != ldnull); the port's reader
    // normalizes brtrue(obj) to comp(ne, x, ldnull) only when the loaded
    // type's stack kind is known -- an external TypeSpec (the CallSite cache
    // field) stays a bare load, which encodes the same non-null test, so the
    // bare load is accepted as the inverted arm.
    if (condition != nullptr && condition->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(condition);
        bool equals = comp->Kind == ComparisonKind::Equality;
        bool notEquals = comp->Kind == ComparisonKind::Inequality;
        if (equals || notEquals) {
            if (comp->Right != nullptr && comp->Right->Op == OpCode::LdNull) {
                argument = comp->Left.get();
            } else if (comp->Left != nullptr &&
                       comp->Left->Op == OpCode::LdNull) {
                argument = comp->Right.get();
            }
            if (argument != nullptr) {
                invertBranches = notEquals;
            } else {
                return false;
            }
        } else {
            return false;
        }
    } else if (condition != nullptr) {
        // The bare truthiness test (see the comment above): non-null.
        argument = condition;
        invertBranches = true;
    } else {
        return false;
    }
    if (!MatchLdsFld(argument, callSiteCacheField)) {
        return false;
    }
    if (callSiteCacheField == nullptr) {
        return false;
    }
    const TypeSystem::IType& cacheType = callSiteCacheField->ReturnType();
    if (TypeArgumentsView(&cacheType).size() != 1 ||
        TypeFullName(&cacheType) != kCallSiteTypeName)
        return false;
    callSiteDelegate = TypeArgumentsView(&cacheType)[0];
    if (!IsDelegateType(callSiteDelegate.get()))
        return false;
    return true;
}

// The C# binder-argument read: `arg.MatchLdLoc(variable)` + `initBlock[pos]
// .MatchStLoc(variable, out value)` (the Roslyn stores the flags/name/context
// into locals the binder call reads). This port's inliner folds single-use
// scalar stores across instructions (the C# inliner only inlines into the
// immediately-following instruction), so the inlined shapes read the value
// directly and consume no block slot; the array-initializer arguments keep
// the store chain (an array initializer cannot inline).
bool ReadBinderValue(Block* initBlock, ILInstruction* arg, int& pos,
                     ILInstruction*& value) {
    value = nullptr;
    ILVariable* argVar = nullptr;
    if (MatchLdLocOut(arg, argVar) && argVar != nullptr) {
        ILInstruction* chained = nullptr;
        if (MatchStLocOf(BlockInstructionAt(initBlock, pos), argVar,
                         chained)) {
            value = chained;
            pos++;
            return true;
        }
    }
    value = arg;
    return true;
}

bool DynamicCallSiteTransform::ScanCallSiteInitBlock(
    Block* callSiteInitBlock, const TypeSystem::IField* callSiteCacheField,
    const TypeSystem::ITypePtr& callSiteDelegateType,
    CallSiteInfo& callSiteInfo, Block*& blockAfterInit) {
    blockAfterInit = nullptr;
    const int instCount = static_cast<int>(callSiteInitBlock->Instructions.size()) +
                         (callSiteInitBlock->FinalInstruction != nullptr ? 1 : 0);
    if (callSiteInitBlock->IncomingEdgeCount != 1 || instCount < 2) {
        return false;
    }
    auto* initBranch =
        dynamic_cast<Branch*>(BlockInstructionAt(callSiteInitBlock, instCount - 1));
    if (initBranch == nullptr)
        return false;
    blockAfterInit = initBranch->TargetBlock;
    const TypeSystem::IField* field = nullptr;
    ILInstruction* value = nullptr;
    if (!MatchStsFld(BlockInstructionAt(callSiteInitBlock, instCount - 2), field,
                     value) ||
        field != callSiteCacheField)
        return false;
    auto* createBinderCall = dynamic_cast<Call*>(value);
    if (createBinderCall == nullptr ||
        createBinderCall->TypeArgumentsCount != 0 ||
        createBinderCall->Arguments.size() != 1 ||
        MethodNameTail(createBinderCall->MethodName) != "Create") {
        return false;
    }
    if (createBinderCall->DeclaringType == nullptr ||
        TypeFullName(createBinderCall->DeclaringType.get()) !=
            kCallSiteTypeName ||
        TypeArgumentsView(createBinderCall->DeclaringType.get()).size() != 1) {
        return false;
    }
    auto* binderCall = dynamic_cast<Call*>(createBinderCall->Arguments[0].get());
    if (binderCall == nullptr ||
        TypeFullName(binderCall->DeclaringType.get()) != kCSharpBinderTypeName ||
        binderCall->DeclaringType->TypeParameterCount() != 0) {
        return false;
    }
    callSiteInfo.DelegateType = callSiteDelegateType;
    callSiteInfo.InitBlock = callSiteInitBlock;
    std::string binderName = MethodNameTail(binderCall->MethodName);
    if (binderCall->Method != nullptr)
        binderName = binderCall->Method->Name();
    ILVariable* variable = nullptr;
    int binderFlagsInteger = 0;
    std::string name;
    TypeSystem::ITypePtr contextType;
    if (binderName == "IsEvent") {
        callSiteInfo.Kind = BinderMethodKind::IsEvent;
        // In the case of Binder.IsEvent all arguments should already be
        // properly inlined, as there is no array initializer.
        if (binderCall->Arguments.size() != 3)
            return false;
        if (!MatchLdcI4Out(binderCall->Arguments[0].get(), binderFlagsInteger))
            return false;
        callSiteInfo.Flags = static_cast<CSharpBinderFlags>(binderFlagsInteger);
        if (!MatchLdStrLocal(binderCall->Arguments[1].get(), name))
            return false;
        callSiteInfo.MemberName = name;
        if (!TransformExpressionTrees::MatchGetTypeFromHandle(
                binderCall->Arguments[2].get(), contextType))
            return false;
        callSiteInfo.Context = contextType;
        return true;
    }
    if (binderName == "Convert") {
        callSiteInfo.Kind = BinderMethodKind::Convert;
        if (binderCall->Arguments.size() != 3)
            return false;
        if (!MatchLdcI4Out(binderCall->Arguments[0].get(), binderFlagsInteger))
            return false;
        callSiteInfo.Flags = static_cast<CSharpBinderFlags>(binderFlagsInteger);
        TypeSystem::ITypePtr targetType;
        if (!TransformExpressionTrees::MatchGetTypeFromHandle(
                binderCall->Arguments[1].get(), targetType))
            return false;
        callSiteInfo.ConvertTargetType = targetType;
        if (!TransformExpressionTrees::MatchGetTypeFromHandle(
                binderCall->Arguments[2].get(), contextType))
            return false;
        callSiteInfo.Context = contextType;
        return true;
    }
    if (binderName == "InvokeMember") {
        callSiteInfo.Kind = BinderMethodKind::InvokeMember;
        if (binderCall->Arguments.size() != 5)
            return false;
        int pos = 0;
        // First argument: binder flags
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[0].get(),
                             pos, value) ||
            !MatchLdcI4Out(value, binderFlagsInteger)) {
            return false;
        }
        callSiteInfo.Flags = static_cast<CSharpBinderFlags>(binderFlagsInteger);
        // Second argument: method name
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[1].get(),
                             pos, value) ||
            !MatchLdStrLocal(value, name)) {
            return false;
        }
        callSiteInfo.MemberName = name;
        // Third argument: type arguments -- ldnull or an array initializer
        // (the array chain keeps the C# stloc linkage). The inlined ldnull
        // shape (no generic arguments) reads the value directly.
        ILVariable* variableOrTemporary = nullptr;
        variable = nullptr;
        if (MatchLdLocOut(binderCall->Arguments[2].get(), variable) &&
            variable != nullptr) {
            if (!MatchStLocReport(BlockInstructionAt(callSiteInitBlock, pos),
                                  variableOrTemporary, value))
                return false;
        } else {
            value = binderCall->Arguments[2].get();
            variable = nullptr;
            // The inlined shape has no store of its own: consume a store
            // slot only when it holds the same null the call reads
            // directly (a leftover chain store); the fully-inlined shape
            // consumes nothing.
            ILVariable* chainVar = nullptr;
            ILInstruction* chainValue = nullptr;
            if (value != nullptr && value->Op == OpCode::LdNull &&
                MatchStLocReport(BlockInstructionAt(callSiteInitBlock, pos),
                                 chainVar, chainValue) &&
                chainValue != nullptr &&
                chainValue->Op == OpCode::LdNull) {
                pos++;
            }
        }
        int numberOfTypeArguments = 0;
        if (value != nullptr && value->Op != OpCode::LdNull) {
            auto* typeArgsNewArr = dynamic_cast<NewArr*>(value);
            int count = 0;
            if (typeArgsNewArr == nullptr ||
                typeArgsNewArr->Type == nullptr ||
                !TypeSystem::IsKnownType(*typeArgsNewArr->Type,
                                         TypeSystem::KnownTypeCode::Type) ||
                typeArgsNewArr->Indices.size() != 1 ||
                !MatchLdcI4Out(typeArgsNewArr->Indices[0].get(), count))
                return false;
            numberOfTypeArguments = count;
            std::vector<TransformArrayInitializersElement> elements;
            int instructionsToRemove = 0;
            if (!TransformArrayInitializersHandleSimple(
                    callSiteInitBlock, pos + 1, variableOrTemporary,
                    std::vector<int>{numberOfTypeArguments}, elements,
                    instructionsToRemove))
                return false;
            callSiteInfo.TypeArguments.clear();
            for (auto& element : elements) {
                TypeSystem::ITypePtr type;
                if (element.shell == nullptr ||
                    element.shell->Value == nullptr)
                    return false;
                if (!TransformExpressionTrees::MatchGetTypeFromHandle(
                        element.shell->Value.get(), type))
                    return false;
                callSiteInfo.TypeArguments.push_back(type);
            }
            pos += 1 + instructionsToRemove;
        }
        int typeArgumentsOffset = numberOfTypeArguments;
        // Special case for csc array initializers:
        if (variableOrTemporary != variable) {
            // store temporary from array initializer in variable
            if (!MatchStLocOf(
                    BlockInstructionAt(callSiteInitBlock, pos),
                    variable, value))
                return false;
            if (!MatchLdLocOut(value, variableOrTemporary))
                return false;
            typeArgumentsOffset++;
            pos++;
        }
        // Fourth argument: context type
        if (!ReadBinderValue(
                callSiteInitBlock, binderCall->Arguments[3].get(), pos,
                value) ||
            !TransformExpressionTrees::MatchGetTypeFromHandle(value,
                                                              contextType)) {
            return false;
        }
        callSiteInfo.Context = contextType;
        // Fifth argument: call parameter info (the array chain)
        if (!MatchLdLocOut(binderCall->Arguments[4].get(), variable)) {
            return false;
        }
        // The C# "special case for csc array initializers" shape: the call
        // may read the array through a copy local (`stloc copy(ldloc
        // array)`), which this port's reader materializes for the
        // stack-held reference. Resolve the copy chain to the array
        // variable.
        if (variable != nullptr) {
            for (auto& inst : callSiteInitBlock->Instructions) {
                auto* stloc = dynamic_cast<StLoc*>(inst.get());
                if (stloc == nullptr ||
                    stloc->Variable.get() != variable)
                    continue;
                ILVariable* copied = nullptr;
                if (MatchLdLocOut(stloc->Value.get(), copied) &&
                    copied != nullptr) {
                    variable = copied;
                }
                break;
            }
        }
        if (!MatchStLocOf(BlockInstructionAt(callSiteInitBlock, pos), variable,
                          value))
            return false;
        if (!ExtractArgumentInfo(value, callSiteInfo, pos + 1, variable)) {
            return false;
        }
        return true;
    }
    if (binderName == "GetMember" || binderName == "SetMember") {
        callSiteInfo.Kind = binderName == "GetMember"
                                ? BinderMethodKind::GetMember
                                : BinderMethodKind::SetMember;
        if (binderCall->Arguments.size() != 4) {
            return false;
        }
        int pos = 0;
        // First argument: binder flags
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[0].get(),
                             pos, value) ||
            !MatchLdcI4Out(value, binderFlagsInteger)) {
            return false;
        }
        callSiteInfo.Flags = static_cast<CSharpBinderFlags>(binderFlagsInteger);
        // Second argument: method name
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[1].get(),
                             pos, value) ||
            !MatchLdStrLocal(value, name)) {
            return false;
        }
        callSiteInfo.MemberName = name;
        // Third argument: context type
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[2].get(),
                             pos, value) ||
            !TransformExpressionTrees::MatchGetTypeFromHandle(value,
                                                              contextType)) {
            return false;
        }
        callSiteInfo.Context = contextType;
        // Fourth argument: call parameter info (the array-initializer
        // chain; keeps the C# stloc linkage)
        if (!MatchLdLocOut(binderCall->Arguments[3].get(), variable)) {
            return false;
        }
        if (!MatchStLocOf(BlockInstructionAt(callSiteInitBlock, pos), variable,
                          value)) {
            return false;
        }
        if (!ExtractArgumentInfo(value, callSiteInfo, pos + 1, variable)) {
            return false;
        }
        return true;
    }
    if (binderName == "GetIndex" || binderName == "SetIndex" ||
        binderName == "InvokeConstructor" || binderName == "Invoke") {
        callSiteInfo.Kind =
            binderName == "GetIndex" ? BinderMethodKind::GetIndex
            : binderName == "SetIndex" ? BinderMethodKind::SetIndex
            : binderName == "InvokeConstructor"
                ? BinderMethodKind::InvokeConstructor
                : BinderMethodKind::Invoke;
        if (binderCall->Arguments.size() != 3)
            return false;
        int pos = 0;
        // First argument: binder flags
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[0].get(),
                             pos, value) ||
            !MatchLdcI4Out(value, binderFlagsInteger))
            return false;
        callSiteInfo.Flags = static_cast<CSharpBinderFlags>(binderFlagsInteger);
        // Second argument: context type
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[1].get(),
                             pos, value) ||
            !TransformExpressionTrees::MatchGetTypeFromHandle(value,
                                                              contextType))
            return false;
        callSiteInfo.Context = contextType;
        // Third argument: call parameter info (the array chain)
        if (!MatchLdLocOut(binderCall->Arguments[2].get(), variable))
            return false;
        if (!MatchStLocOf(BlockInstructionAt(callSiteInitBlock, pos), variable,
                          value))
            return false;
        if (!ExtractArgumentInfo(value, callSiteInfo, pos + 1, variable))
            return false;
        return true;
    }
    if (binderName == "UnaryOperation" || binderName == "BinaryOperation") {
        callSiteInfo.Kind = binderName == "BinaryOperation"
                                ? BinderMethodKind::BinaryOperation
                                : BinderMethodKind::UnaryOperation;
        if (binderCall->Arguments.size() != 4)
            return false;
        int pos = 0;
        // First argument: binder flags
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[0].get(),
                             pos, value) ||
            !MatchLdcI4Out(value, binderFlagsInteger))
            return false;
        callSiteInfo.Flags = static_cast<CSharpBinderFlags>(binderFlagsInteger);
        // Second argument: operation
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[1].get(),
                             pos, value))
            return false;
        int operation = 0;
        if (!MatchLdcI4Out(value, operation))
            return false;
        callSiteInfo.Operation = static_cast<ExpressionType>(operation);
        // Third argument: context type
        if (!ReadBinderValue(callSiteInitBlock, binderCall->Arguments[2].get(),
                             pos, value) ||
            !TransformExpressionTrees::MatchGetTypeFromHandle(value,
                                                              contextType))
            return false;
        callSiteInfo.Context = contextType;
        // Fourth argument: call parameter info (the array chain)
        if (!MatchLdLocOut(binderCall->Arguments[3].get(), variable))
            return false;
        if (!MatchStLocOf(BlockInstructionAt(callSiteInitBlock, pos), variable,
                          value))
            return false;
        if (!ExtractArgumentInfo(value, callSiteInfo, pos + 1, variable))
            return false;
        return true;
    }
    return false;
}

bool DynamicCallSiteTransform::ExtractArgumentInfo(
    ILInstruction* value, CallSiteInfo& callSiteInfo, int instructionOffset,
    ILVariable* variable) {
    auto* newArr = dynamic_cast<NewArr*>(value);
    int numberOfArguments = 0;
    if (newArr == nullptr || newArr->Type == nullptr ||
        TypeFullName(newArr->Type.get()) != kCSharpArgumentInfoTypeName ||
        newArr->Indices.size() != 1 ||
        !MatchLdcI4Out(newArr->Indices[0].get(), numberOfArguments))
        return false;
    std::vector<TransformArrayInitializersElement> elements;
    int instructionsToRemove = 0;
    if (!TransformArrayInitializersHandleSimple(
            callSiteInfo.InitBlock, instructionOffset, variable,
            std::vector<int>{numberOfArguments}, elements,
            instructionsToRemove))
        return false;
    callSiteInfo.ArgumentInfos.clear();
    // The C# fills each argument's compile-time type from the delegate's
    // invoke method (parameter i+1: the first parameter is the callsite
    // itself); this port's name-only delegate types carry no invoke method,
    // so the type argument at i+1 stands in (the same shape for the
    // Func/Action callsite delegates), and a missing one stays null.
    const std::vector<TypeSystem::ITypePtr>* delegateTypeArguments = nullptr;
    if (callSiteInfo.DelegateType != nullptr)
        delegateTypeArguments = &TypeArgumentsView(callSiteInfo.DelegateType.get());
    for (std::size_t i = 0; i < elements.size(); i++) {
        if (elements[i].shell == nullptr || elements[i].shell->Value == nullptr)
            return false;
        auto* createCall = dynamic_cast<Call*>(elements[i].shell->Value.get());
        if (createCall == nullptr)
            return false;
        if (MethodNameTail(createCall->MethodName) != "Create" ||
            createCall->Arguments.size() != 2)
            return false;
        if (createCall->Method != nullptr &&
            createCall->Method->Name() != "Create")
            return false;
        int argumentInfoFlags = 0;
        if (!MatchLdcI4Out(createCall->Arguments[0].get(),
                           argumentInfoFlags))
            return false;
        std::string argumentName;
        if (!MatchLdStrLocal(createCall->Arguments[1].get(), argumentName)) {
            if (createCall->Arguments[1] == nullptr ||
                createCall->Arguments[1]->Op != OpCode::LdNull)
                return false;
        }
        CSharpArgumentInfo info;
        info.Flags = static_cast<CSharpArgumentInfoFlags>(argumentInfoFlags);
        info.Name = argumentName;
        if (delegateTypeArguments != nullptr &&
            i + 1 < delegateTypeArguments->size())
            info.CompileTimeType = (*delegateTypeArguments)[i + 1];
        callSiteInfo.ArgumentInfos.push_back(info);
    }
    return true;
}

void DynamicCallSiteTransform::TransformCallSites(
    BlockContainer* parent, std::vector<BlockContainer*>& modifiedContainers) {
    std::vector<StLoc*> storesToRemove;
    std::set<BlockContainer*> modified;

    std::vector<Call*> invokeCalls;
    {
        std::vector<ILInstruction*> stack{parent};
        while (!stack.empty()) {
            ILInstruction* node = stack.back();
            stack.pop_back();
            if (auto* call = dynamic_cast<Call*>(node)) {
                if (!call->IsNewObj)
                    invokeCalls.push_back(call);
            }
            for (int i = 0; i < node->ChildCount(); i++) {
                if (ILInstruction* child = node->GetChild(i))
                    stack.push_back(child);
            }
        }
    }
    for (Call* invokeCall : invokeCalls) {
        // The C# `invokeCall.Method.DeclaringType.Kind != TypeKind.Delegate ||
        // invokeCall.Method.Name != "Invoke"` -- the port's resolved surface
        // may be null (an external MemberRef), so the delegate check falls
        // back to the reader's declaring-type surface (the name-only
        // Func/Action family recognition).
        if (MethodNameTail(invokeCall->MethodName) != "Invoke" ||
            invokeCall->Arguments.empty()) {
            continue;
        }
        if (invokeCall->Method != nullptr) {
            if (invokeCall->Method->Name() != "Invoke")
                continue;
            if (invokeCall->Method->DeclaringType() == nullptr ||
                invokeCall->Method->DeclaringType()->Kind() !=
                    TypeSystem::TypeKind::Delegate)
                continue;
        } else if (!IsDelegateType(invokeCall->DeclaringType.get())) {
            continue;
        }
        ILInstruction* firstArgument = invokeCall->Arguments[0].get();
        ILVariable* stackSlot = nullptr;
        if (MatchLdLocOut(firstArgument, stackSlot) &&
            IsSingleDefinitionTemporary(stackSlot) &&
            !stackSlot->StoreInstructions.empty()) {
            if (auto* store = dynamic_cast<StLoc*>(
                    stackSlot->StoreInstructions[0]))
                firstArgument = store->Value.get();
        }
        ILInstruction* cacheFieldLoad = nullptr;
        const TypeSystem::IField* targetField = nullptr;
        if (!MatchLdFld(firstArgument, cacheFieldLoad, targetField)) {
            continue;
        }
        const TypeSystem::IField* cacheField = nullptr;
        if (!MatchLdsFld(cacheFieldLoad, cacheField)) {
            continue;
        }
        auto callsiteIt = callsites_.find(cacheField);
        if (callsiteIt == callsites_.end()) {
            continue;
        }
        const CallSiteInfo& callsite = callsiteIt->second;
        context_->StepOnce("Transform callsite");
        // The C# reads the dead arguments (the cache loads at the invoke's
        // argument head) after the replacement, over the GC-live nodes;
        // the port detaches the operands in MakeDynamicInstruction and the
        // invoke dies with the replacement, so the dead-store bookkeeping
        // (the variables and their single stores) is captured first.
        std::vector<ILInstruction*> deadArguments;
        ILInstruction* replacement =
            MakeDynamicInstruction(callsite, invokeCall, deadArguments);
        if (replacement == nullptr)
            continue;
        struct DeadStoreCandidate {
            StLoc* store;
            ILInstruction* value;
        };
        std::vector<DeadStoreCandidate> deadStoreCandidates;
        for (ILInstruction* arg : deadArguments) {
            ILVariable* temporary = nullptr;
            if (MatchLdLocOut(arg, temporary) &&
                IsSingleDefinitionTemporary(temporary) &&
                temporary->LoadCount == 0 &&
                !temporary->StoreInstructions.empty()) {
                auto* stLoc = dynamic_cast<StLoc*>(
                    temporary->StoreInstructions[0]);
                if (stLoc != nullptr)
                    deadStoreCandidates.push_back(
                        {stLoc, stLoc->Value.get()});
            }
        }
        invokeCall->ReplaceWith(std::unique_ptr<ILInstruction>(replacement));
        auto* block = dynamic_cast<Block*>(callsite.IfFinal->Parent);
        if (block == nullptr)
            continue;
        if (callsite.IfFinal == nullptr)
            continue;
        if (callsite.Inverted) {
            // The C# removes the if and replaces the trailing branch with
            // the if's true instruction; the port's trailing branch is the
            // fall-through, so the if-final is replaced by its own true arm
            // (the branch to the after-init block).
            IfInstruction* ifFinal = callsite.IfFinal;
            if (ifFinal->TrueInst != nullptr) {
                // The if's slot order is condition / true / false (the C#
                // IfInstruction slots); the true arm becomes the block's
                // new final (the branch to the after-init block).
                auto trueBranch = ifFinal->TakeChild(1);
                block->SetFinal(std::move(trueBranch));
            } else {
                block->FinalInstruction.reset();
            }
        } else {
            // The C# removes the if, leaving the explicit branch after it;
            // the port's fall-through (the next block) is the same
            // continuation, so clearing the final suffices.
            block->FinalInstruction.reset();
        }
        for (const DeadStoreCandidate& candidate : deadStoreCandidates) {
            auto* stLoc = candidate.store;
            ILInstruction* value = candidate.value;
            if (auto* storeParentBlock =
                    dynamic_cast<Block*>(stLoc->Parent)) {
                (void)storeParentBlock;
                const TypeSystem::IField* cacheFieldCopy = nullptr;
                if (MatchLdsFld(value, cacheFieldCopy) &&
                    cacheFieldCopy == cacheField) {
                    storesToRemove.push_back(stLoc);
                }
                ILInstruction* cacheLoadCopy = nullptr;
                const TypeSystem::IField* targetFieldCopy = nullptr;
                if (MatchLdFld(value, cacheLoadCopy, targetFieldCopy) &&
                    MatchLdsFld(cacheLoadCopy, cacheFieldCopy) &&
                    cacheField == cacheFieldCopy &&
                    targetField == targetFieldCopy) {
                    storesToRemove.push_back(stLoc);
                }
            }
        }
        if (auto* container = dynamic_cast<BlockContainer*>(block->Parent))
            modified.insert(container);
    }

    for (StLoc* inst : storesToRemove) {
        if (auto* parentBlock = dynamic_cast<Block*>(inst->Parent)) {
            if (inst->ChildIndex >= 0 &&
                inst->ChildIndex <
                    static_cast<int>(parentBlock->Instructions.size())) {
                parentBlock->Instructions.erase(
                    parentBlock->Instructions.begin() + inst->ChildIndex);
            }
        }
    }

    modifiedContainers.insert(modifiedContainers.end(),
                              modified.begin(), modified.end());
}

ILInstruction* DynamicCallSiteTransform::MakeDynamicInstruction(
    const CallSiteInfo& callsite, Call* targetInvokeCall,
    std::vector<ILInstruction*>& deadArguments) {
    // The C# takes ownership of the operand subtrees from the invoke's
    // argument list (the invoke is replaced as a whole afterwards, which
    // would destroy the originals); the port detaches each operand first.
    auto takeArgument = [targetInvokeCall](std::size_t index) {
        if (index >= targetInvokeCall->Arguments.size())
            return std::unique_ptr<ILInstruction>();
        ILInstruction* arg = targetInvokeCall->Arguments[index].get();
        if (arg == nullptr || arg->Parent != targetInvokeCall)
            return std::unique_ptr<ILInstruction>();
        return targetInvokeCall->TakeChild(
            static_cast<int>(arg->ChildIndex));
    };
    auto argumentsFrom = [&](std::size_t start) {
        std::vector<std::unique_ptr<ILInstruction>> result;
        for (std::size_t i = start; i < targetInvokeCall->Arguments.size();
             i++)
            result.push_back(takeArgument(i));
        return result;
    };
    switch (callsite.Kind) {
        case BinderMethodKind::BinaryOperation:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicBinaryOperatorInstruction(
                callsite.Flags, callsite.Operation, callsite.Context,
                callsite.ArgumentInfos.size() > 0
                    ? callsite.ArgumentInfos[0]
                    : CSharpArgumentInfo{},
                takeArgument(2),
                callsite.ArgumentInfos.size() > 1
                    ? callsite.ArgumentInfos[1]
                    : CSharpArgumentInfo{},
                takeArgument(3));
        case BinderMethodKind::Convert: {
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicConvertInstruction(
                callsite.Flags, callsite.ConvertTargetType, callsite.Context,
                takeArgument(2));
        }
        case BinderMethodKind::GetIndex:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicGetIndexInstruction(
                callsite.Flags, callsite.Context, callsite.ArgumentInfos,
                argumentsFrom(2));
        case BinderMethodKind::GetMember:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicGetMemberInstruction(
                callsite.Flags, callsite.MemberName, callsite.Context,
                callsite.ArgumentInfos.size() > 0
                    ? callsite.ArgumentInfos[0]
                    : CSharpArgumentInfo{},
                takeArgument(2));
        case BinderMethodKind::Invoke:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicInvokeInstruction(
                callsite.Flags, callsite.Context, callsite.ArgumentInfos,
                argumentsFrom(2));
        case BinderMethodKind::InvokeConstructor: {
            auto arguments = argumentsFrom(2);
            // Extract type information from targetInvokeCall:
            // Must either be an inlined type or
            // a reference to a variable that is initialized with a type.
            TypeSystem::ITypePtr type;
            if (!arguments.empty() && arguments[0] != nullptr) {
                if (!TransformExpressionTrees::MatchGetTypeFromHandle(
                        arguments[0].get(), type)) {
                    ILVariable* temp = nullptr;
                    if (!(MatchLdLocOut(arguments[0].get(), temp) &&
                          temp != nullptr && temp->IsSingleDefinition() &&
                          !temp->StoreInstructions.empty()))
                        return nullptr;
                    auto* initStore = dynamic_cast<StLoc*>(
                        temp->StoreInstructions[0]);
                    if (initStore == nullptr ||
                        !TransformExpressionTrees::MatchGetTypeFromHandle(
                            initStore->Value.get(), type))
                        return nullptr;
                }
            }
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicInvokeConstructorInstruction(
                callsite.Flags, type, callsite.Context,
                callsite.ArgumentInfos, std::move(arguments));
        }
        case BinderMethodKind::InvokeMember:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicInvokeMemberInstruction(
                callsite.Flags, callsite.MemberName, callsite.TypeArguments,
                callsite.Context, callsite.ArgumentInfos, argumentsFrom(2));
        case BinderMethodKind::IsEvent:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicIsEventInstruction(
                callsite.Flags, callsite.MemberName, callsite.Context,
                takeArgument(2));
        case BinderMethodKind::SetIndex:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicSetIndexInstruction(
                callsite.Flags, callsite.Context, callsite.ArgumentInfos,
                argumentsFrom(2));
        case BinderMethodKind::SetMember:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicSetMemberInstruction(
                callsite.Flags, callsite.MemberName, callsite.Context,
                callsite.ArgumentInfos.size() > 0
                    ? callsite.ArgumentInfos[0]
                    : CSharpArgumentInfo{},
                takeArgument(2),
                callsite.ArgumentInfos.size() > 1
                    ? callsite.ArgumentInfos[1]
                    : CSharpArgumentInfo{},
                takeArgument(3));
        case BinderMethodKind::UnaryOperation:
            for (std::size_t i = 0; i < 2 && i < targetInvokeCall->Arguments.size(); i++)
                deadArguments.push_back(targetInvokeCall->Arguments[i].get());
            return new DynamicUnaryOperatorInstruction(
                callsite.Flags, callsite.Operation, callsite.Context,
                callsite.ArgumentInfos.size() > 0
                    ? callsite.ArgumentInfos[0]
                    : CSharpArgumentInfo{},
                takeArgument(2));
    }
    return nullptr;
}

} // namespace ILSpy::Decompiler::IL
