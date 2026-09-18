// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformCollectionAndObject
// Initializers.cs -- the per-statement transform that folds
//
//   stloc v(newobj T(...)); call set_P(ldloc v, ...); call Add(ldloc v, ...)
//
// into a BlockKind.ObjectInitializer/CollectionInitializer block over an
// InitializerTarget variable (the shape ExpressionBuilder.
// TranslateObjectAndCollectionInitializer renders as `new T { P = ..., ... }`).
//
// STATUS: the four static member-shape helpers are ported; the statement
// transform body (Run/IsPartOfInitializer and its path-stack state) is still
// deferred -- see the Run declaration below for the remaining prerequisites.

#pragma once

#include "Decompiler/IL/Transforms/AccessPathElement.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"

#include <vector>

namespace ILSpy::Decompiler::IL {

class Call;
class ILInstruction;
class ILVariable;

class TransformCollectionAndObjectInitializers : public IStatementTransform {
public:
    // The C# `private static bool TypeContainsInitOnlyProperties(
    // ITypeDefinition? typeDefinition)` (lines 217-227): whether any property of
    // the type has an init-only setter (the C# 9 `init` accessor). Such a type
    // must keep its object-initializer statements even on the statement level
    // (an init-only property cannot be assigned after construction), so Run's
    // local-variable-preference early-out declines for it. A null definition
    // has no properties (the C# `typeDefinition?.Properties ?? []`).
    static bool TypeContainsInitOnlyProperties(
        const TypeSystem::ITypeDefinition* typeDefinition);

    // The C# `internal static bool IsRecordCloneMethodCall(CallInstruction ci)`
    // (lines 229-239): whether the call is a record's compiler-generated
    // `<Clone>$` method invoked on its single receiver -- the shape Run's
    // WithInitializer arm takes as the initializer target (`new ... { ... }
    // = original with { ... }`). A call without a resolved method is not one.
    static bool IsRecordCloneMethodCall(const Call* ci);

    // The C# `private bool IsMethodCallOnVariable(ILInstruction inst,
    // ILVariable variable)` (lines 241-249): whether `inst` USES the variable
    // in a load/receiver position -- a reference-typed load of it (either the
    // ldloc/ldloca shape MatchLdLocRef accepts), the receiver of an instance
    // call, or the target of a field load/store/address. Run consults it on the
    // instruction AFTER a possible initializer to refuse converting the
    // statements when the variable is still used directly (e.g. passed to a
    // method) rather than only through access-path members.
    static bool IsMethodCallOnVariable(const ILInstruction* inst,
                                       const ILVariable* variable);

    // The C# `private bool IsValidObjectInitializerTarget(
    // List<AccessPathElement> path)` (lines 432-451): whether a Setter access
    // path may end at `path`'s last element. Empty is valid; a non-property or
    // non-indexer element is valid; an indexer element requires the PREVIOUS
    // element's return type to be equivalent to the indexer's declaring type
    // (the C# 6 nested dictionary-initializer rule -- `[key] = { value }` needs
    // the container element to BE the indexer's declaring type so the nested
    // `Add` calls resolve to it); a first-and-only indexer element is invalid
    // (nothing establishes the indexer's declaring type).
    static bool IsValidObjectInitializerTarget(
        const std::vector<AccessPathElement>& path);

    // IStatementTransform: the statement-level fold itself. DEFERRED: the C#
    // body needs the per-transform path-stack state (possibleIndexVariables /
    // currentPath / isCollection / pathStack) and the IsPartOfInitializer
    // state machine over AccessPathElement::GetAccessPath with the C#-layer
    // settings and resolver threading (GetAccessPath takes the full
    // DecompilerSettings + an optional CSharpResolver while the IL-layer
    // StatementTransformContext carries the ILTransformSettings subset) plus
    // the remaining head-shape pieces (MatchCastClass, the Call node's
    // ILStackWasEmpty, the context-shaped TransformDisplayClassUsage
    // IsPotentialClosure overload, TupleTransform.MatchTupleConstruction).
    // ILFunction::RegisterVariable, ILInlining::InlineIfPossible, CopyPropagation
    // ::Propagate and DelegateConstruction.MatchDelegateConstruction -- the
    // other named prerequisites -- are ported. Throws std::logic_error until
    // then.
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
