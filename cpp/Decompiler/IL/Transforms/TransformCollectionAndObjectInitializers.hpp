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
// STATUS: the four static member-shape helpers are ported, and so is the
// statement-scan state machine (IsPartOfInitializer with its per-scan state:
// the possible-index-variable map, the current access path, the collection
// flag, and the path stack). The Run body (the fold that consumes the scan)
// is still deferred -- see the Run declaration below for the remaining
// prerequisites.

#pragma once

#include "Decompiler/IL/BlockKind.hpp"
#include "Decompiler/IL/Transforms/AccessPathElement.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"

#include <cstddef>
#include <unordered_map>
#include <unordered_set>
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

    // ---- The statement-scan state machine (C# lines 63-67, 262-323) ----

    // The C# `readonly Dictionary<ILVariable, (int Index, ILInstruction Value)>
    // possibleIndexVariables` (line 63): the single-definition local stores the
    // scan accepted as possible dictionary-initializer index variables,
    // mapping the variable to its init store's ChildIndex and the stored value
    // instruction. MarkUsedIndices flips Index to -1 (used); Run later takes
    // the Min over the surviving indices to bound the initializer.
    struct PossibleIndexVariableInfo {
        int Index = -1;
        ILInstruction* Value = nullptr;
    };

    // The C# `readonly List<AccessPathElement> currentPath` (line 64) -- the
    // member path shared by the statements scanned so far (the pushed prefix,
    // never holding the per-statement last element).
    // The C# `bool isCollection` (line 65) -- whether the scan has entered
    // collection-initializer mode (an Adder was accepted).
    // The C# `readonly Stack<HashSet<AccessPathElement>> pathStack` (line 66)
    // -- one member-set per currentPath level: the set of siblings already
    // written at that level (a duplicate member at the same level ends the
    // scan). The vector is used as a stack (back = top).
    //
    // The C# fields are private; the port exposes them (and the hash functor)
    // for tests -- the established testability convention -- while Run remains
    // the only production writer.
    std::unordered_map<ILVariable*, PossibleIndexVariableInfo> possibleIndexVariables;
    std::vector<AccessPathElement> currentPath;
    bool isCollection = false;
    struct AccessPathElementHash {
        std::size_t operator()(const AccessPathElement& e) const {
            return e.GetHashCode();
        }
    };
    std::vector<std::unordered_set<AccessPathElement, AccessPathElementHash>> pathStack;

    // The C# Run's per-scan state reset (lines 106-110: the four Clear calls
    // plus the initial empty-set push). Run calls it before the statement scan;
    // tests call it before driving IsPartOfInitializer directly.
    void ResetInitializerScanState();

    // The C# `private bool IsPartOfInitializer(InstructionCollection<ILInstruction>
    // instructions, int pos, ILVariable target, IType rootType, ref BlockKind
    // blockKind, ref bool initializerContainsInitOnlyItems, StatementTransformContext
    // context)` (lines 262-323): whether the statement at instructions[pos] is
    // part of the initializer being scanned over `target` (a possible
    // dictionary index-variable store, or an access-path store/Add rooted at
    // target), maintaining the per-scan state: the index-variable map, the
    // shared-path push/pop against the path stack (an isCollection reset on
    // every pop, a duplicate sibling member ending the scan), the collection
    // flag, and the used-index marking. The C# private member is public in the
    // port (the testability convention) so the state machine is verifiable
    // before Run lands. `instructions` maps the C# InstructionCollection to the
    // port's Block::Instructions vector; `rootType` is nullable only for the
    // port's GetAccessPath signature (Run always passes the init type).
    bool IsPartOfInitializer(
        const std::vector<std::unique_ptr<ILInstruction>>& instructions, int pos,
        ILVariable* target, const TypeSystem::IType* rootType,
        BlockKind& blockKind, bool& initializerContainsInitOnlyItems,
        StatementTransformContext& context);

    // The C# `void MarkUsedIndices()` local function (lines 310-319): flips the
    // recorded index of every used index variable to -1 (the tuple is
    // reassigned keeping the Value).
    void MarkUsedIndices(const std::vector<ILVariable*>& usedIndices);

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
    // StatementTransformContext carries the ILTransformSettings subset).
    // The head-shape prerequisites are ported: MatchCastClass, the Call /
    // StLoc / DefaultValue ILStackWasEmpty fields (reader-populated),
    // TransformDisplayClassUsage::IsPotentialClosure (the context-shaped
    // overload), DelegateConstruction.MatchDelegateConstruction,
    // TupleTransform.MatchTupleConstruction, ILInlining::InlineIfPossible and
    // CopyPropagation::Propagate, and -- with this slice -- the IsPartOfInitializer
    // state machine itself plus the ILTransformContext's C#-layer handles
    // (CSharpSettings / Resolver) the scan consults. Throws std::logic_error
    // until the body lands.
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
