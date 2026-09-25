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

// Port of ICSharpCode.Decompiler/IL/Transforms/DynamicCallSiteTransform.cs:
// transforms the CallSite cache-field initialization pattern (the compiler-
// generated `if (cache == null) { cache = CallSite<T>.Create(Binder.X(...));
// } ... cache.Target.Invoke(cache, args)`) into the dynamic ILAst nodes.
//
// PORT ADAPTATIONS (the deferred-resolution convention -- the reader's
// name-only types cannot decide the C# read-time facts):
//   * The C# ILReader normalizes `brtrue(obj)` to comp(ne, obj, ldnull) using
//     the resolved field type (a CallSite is a reference type); this port's
//     reader leaves the bare callsite load when the type's stack kind is
//     unknown (an external TypeSpec). The cache-null check matcher accepts
//     the bare load as the inverted (non-null) arm -- the same condition the
//     C# comp form encodes.
//   * The C# `IType.FullName` (namespace + the arity-stripped name) is
//     computed locally; the port's name-only types carry the arity tick in
//     their Name().
//   * The delegate-kind checks (`Kind == TypeKind.Delegate`) also accept
//     the well-known Func/Action callsite delegate families by arity-stripped
//     name, and the argument-info compile-time types fall back to the
//     delegate's type arguments (the callsite delegates' first parameter is
//     always the CallSite itself, so invoke-parameter i+1 is type argument
//     i+1) when the invoke method cannot resolve.
//   * The C# block shape [.., if (null-check) br init, br after-init] keeps
//     the if in the instruction list with an explicit fall-through branch
//     after it; this port's reader carries the if in the FinalInstruction
//     slot with the fall-through implicit, so the "branch after init" is the
//     next block in the container and the inverted rewrite takes the if's
//     true arm as the block's new final.
//
// The InvokeConstructor type-argument arm needs TransformExpressionTrees'
// MatchGetTypeFromHandle and the array-initializer argument blocks need
// TransformArrayInitializers' HandleSimpleArrayInitializer (both ported).
// The async state-machine entry point (RunOnBasicBlock) is deferred with the
// async dynamic arms.

#pragma once

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/DynamicInstructions.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <map>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class IfInstruction;
class Call;
class ILVariable;

// The C# `internal enum BinderMethodKind` (DynamicCallSiteTransform.cs lines
// 636-650): the Binder.* factory the callsite init block calls.
enum class BinderMethodKind {
    BinaryOperation,
    Convert,
    GetIndex,
    GetMember,
    Invoke,
    InvokeConstructor,
    InvokeMember,
    IsEvent,
    SetIndex,
    SetMember,
    UnaryOperation,
};

// The C# `internal struct CallSiteInfo` (DynamicCallSiteTransform.cs lines
// 616-634): everything one callsite cache field's pattern carries.
struct CallSiteInfo {
    bool Inverted = false;
    // The port's block model: the C# BranchAfterInit (the explicit branch
    // after the null-check if) is the null-check block's fall-through, so
    // only the target block is carried; the inverted rewrite takes the if's
    // true arm as the block's new final.
    // The port's reader carries the C# `ConditionalJumpToInit` IfInstruction
    // as the null-check block's FinalInstruction; IfFinal holds it (the
    // C# field name is kept for the branch arm).
    Branch* ConditionalJumpToInit = nullptr;
    IfInstruction* IfFinal = nullptr;
    Block* InitBlock = nullptr;
    TypeSystem::ITypePtr DelegateType;
    BinderMethodKind Kind = BinderMethodKind::Invoke;
    CSharpBinderFlags Flags = CSharpBinderFlags::None;
    ExpressionType Operation = ExpressionType::Add;
    TypeSystem::ITypePtr Context;
    TypeSystem::ITypePtr ConvertTargetType;
    std::vector<TypeSystem::ITypePtr> TypeArguments;
    std::vector<CSharpArgumentInfo> ArgumentInfos;
    std::string MemberName;
};

class DynamicCallSiteTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // The C# `internal static void RunOnBasicBlock(Block block,
    // ILTransformContext context)` (lines 54-67): the per-block entry the
    // async state-machine analysis drives while it iterates the
    // container's blocks (deleting the now-unreachable callsite-init blocks
    // is deferred to the container's SortBlocks, so removing blocks here
    // would corrupt that loop).
    static void RunOnBasicBlock(Block* block, ILTransformContext& context);

private:
    ILTransformContext* context_ = nullptr;
    std::map<const TypeSystem::IField*, CallSiteInfo> callsites_;

    // The C# `TransformCallSites` (lines 76-136): rewrite every delegate
    // invoke whose first argument flows from a recognized cache field.
    void TransformCallSites(BlockContainer* parent,
                            std::vector<BlockContainer*>& modifiedContainers);
    // The C# `FindDynamicCallSitesInBlock` (lines 138-175).
    void FindDynamicCallSitesInBlock(Block* block);
    // The C# `MatchCallSiteCacheNullCheck` (lines 594-616).
    bool MatchCallSiteCacheNullCheck(ILInstruction* condition,
                                     const TypeSystem::IField*& callSiteCacheField,
                                     TypeSystem::ITypePtr& callSiteDelegate,
                                     bool& invertBranches);
    // The C# `ScanCallSiteInitBlock` (lines 177-502).
    bool ScanCallSiteInitBlock(Block* callSiteInitBlock,
                               const TypeSystem::IField* callSiteCacheField,
                               const TypeSystem::ITypePtr& callSiteDelegateType,
                               CallSiteInfo& callSiteInfo,
                               Block*& blockAfterInit);
    // The C# `ExtractArgumentInfo` (lines 504-548).
    bool ExtractArgumentInfo(ILInstruction* value, CallSiteInfo& callSiteInfo,
                            int instructionOffset, ILVariable* variable);
    // The C# `MakeDynamicInstruction` (lines 195-322).
    ILInstruction* MakeDynamicInstruction(
        const CallSiteInfo& callsite, Call* targetInvokeCall,
        std::vector<ILInstruction*>& deadArguments);
};

} // namespace ILSpy::Decompiler::IL
