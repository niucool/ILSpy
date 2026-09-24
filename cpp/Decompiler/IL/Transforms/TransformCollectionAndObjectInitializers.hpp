// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.cs
// (subset). The statement transform folds the `stloc v(newobj ...)` +
// `callvirt Add(ldloc v, ...)` / `stobj(ldflda(ldloc v, ...))` statement
// sequences back into a CollectionInitializer/ObjectInitializer block so the
// C# renderer can print `new List<int> { 1, 2 }` / `new Data { A = 1 }`.
//
// Ported: the Run driver (the NewObj / DefaultValue / Activator.CreateInstance
// / record-clone arms and the with-initializer record arm), the
// IsPartOfInitializer scan with the access-path comparison state
// (currentPath / pathStack / isCollection), the dictionary-index-variable
// bookkeeping (possibleIndexVariables + MarkUsedIndices), the access-path
// walker (AccessPathElement::GetAccessPath over Call / LdObj+LdFlda / StObj+
// LdFlda / LdLoc / LdLoca / LdFlda), IsMethodCallOnVariable,
// IsValidObjectInitializerTarget, TypeContainsInitOnlyProperties,
// IsRecordCloneMethodCall, and the CopyPropagation.Propagate ldloca-source
// subset (un-inlining is a no-op for the ldloca value the C# gate matches, so
// the port clones the ldloca per load, removes the store and re-runs the
// inliner -- the same observable effect as the C# DoPropagate for that value).
//
// Deferred with their unported surfaces (documented at the use sites):
// - The CSharpResolver-dependent arms of the C# GetAccessPath: the port's
//   ILTransformContext carries no CSharpResolver, so GetAccessPath runs with
//   resolver == null. The resolver-independent parts of IsMethodApplicable
//   (the static check, the accessor-owner shortcut, the "Add" name check and
//   the IEnumerable base-type check) run unconditionally; the extension-method
//   transformability and the generic-Add TypeInference checks need the
//   resolver and are deferred (extension methods are rejected conservatively,
//   generic Add methods are accepted without inference). CanBeUsedInInitializer
//   resolves to the C# null-resolver form (CanSet || kind != Setter).
// - The LdObjIfRef case of GetAccessPath (the node is not ported; the C# uses
//   it for ldnull-protected field chains).
// - The ldflda.Field.IsReadOnly gate inside the LdObj case (the port's LdFlda
//   carries the field identity, not an IField with IsReadOnly; the C# rejects
//   read-only fields on a Setter path, the port accepts them).
// - The previous-element IField.ReturnType check inside
//   IsValidObjectInitializerTarget (a field-typed access-path element has no
//   IType on this port; the nested-field indexer case aborts conservatively).
// - The `currentMethod.IsCompilerGeneratedOrIsInCompilerGeneratedClass()` gate
//   (the port's ILFunction carries no IMethod handle) and the
//   TransformDisplayClassUsage.IsPotentialClosure(context, newObjInst) gate
//   (the port's IsPotentialClosure needs the decompiled type definition, which
//   the same missing handle implies; the check evaluates false).
// - The DelegateConstruction.MatchDelegateConstruction and
//   TransformDisplayClassUsage IsPotentialClosure closure gates are ported to
//   the extent the port's DelegateConstruction::MatchDelegateConstruction
//   goes; the remaining display-class cases are covered by the deferral above.

#pragma once

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

enum class AccessPathKind {
    Invalid,
    Setter,
    Adder,
};

// One element of an access path (the C# `struct AccessPathElement`). The C#
// stores a single `IMember Member` slot (the method for a non-accessor call,
// the accessor owner (IProperty) for an accessor call, or the IField for an
// ldflda/stobj chain). The port splits the slot by kind: `method` for the
// non-accessor call element, `property` for the accessor-owner element, and
// the field identity (name + token, the port's LdFlda surface) for the
// field-chain elements. `indices` holds the accessor's indexer arguments as
// raw (non-owning) pointers into the live tree, compared structurally like the
// C# ILInstructionMatchComparer (pointer-equal, or both pure and structurally
// equal); the C# GC references map to the same raw pointers.
struct AccessPathElement {
    OpCode op = OpCode::InvalidExpression;
    const TypeSystem::IMethod* method = nullptr;
    const TypeSystem::IProperty* property = nullptr;
    std::string fieldName;
    std::uint32_t fieldToken = 0;
    std::vector<ILInstruction*> indices;

    // The C# `bool Equals(AccessPathElement other)`: member identity (the
    // specialized-member Equals reduces to reference equality for the
    // decompilation-lifetime members this port handles) and the structural
    // index comparison.
    bool Equals(const AccessPathElement& other) const;
    bool operator==(const AccessPathElement& other) const { return Equals(other); }
};

// The C# `AccessPathElement.GetAccessPath` result tuple. `values` holds the
// raw (non-owning) pointers to the value expressions still owned by the tree;
// `target` is the variable the access chain reads from (null when absent);
// `usedIndexVariables` collects the variables used as indexer arguments.
struct AccessPath {
    AccessPathKind kind = AccessPathKind::Invalid;
    std::vector<AccessPathElement> path;
    std::vector<ILInstruction*> values;
    ILVariable* target = nullptr;
    std::vector<ILVariable*> usedIndexVariables;
};

// The C# `ILInstructionMatchComparer` subset: two instructions are "matching"
// when both are pure and structurally equal (the C# `x.Match(y).Success`).
// Used by the AccessPathElement index comparison.
bool AccessPathInstructionsMatch(const ILInstruction* a, const ILInstruction* b);

// A total order over AccessPathElement for the pathStack sets (the C# uses
// HashSet<AccessPathElement> with the Equals/GetHashCode pair; the port's set
// comparator orders by the member identity and index list).
bool AccessPathElementLess(const AccessPathElement& a, const AccessPathElement& b);

class TransformCollectionAndObjectInitializers : public IStatementTransform {
public:
    // The C# `void IStatementTransform.Run(Block block, int pos,
    // StatementTransformContext context)`.
    void Run(Block& block, int pos, StatementTransformContext& context) override;

    // The C# `bool IsMethodCallOnVariable(ILInstruction inst, ILVariable variable)`
    // (private instance method; state-free, exposed for the tests).
    static bool IsMethodCallOnVariable(ILInstruction* inst, ILVariable* variable);

    // The C# `internal static bool IsRecordCloneMethodCall(CallInstruction ci)`.
    static bool IsRecordCloneMethodCall(const TypeSystem::IMethod* method, std::size_t argumentCount);

private:
    // The C# `bool IsPartOfInitializer(InstructionCollection instructions,
    // int pos, ILVariable target, IType rootType, ref BlockKind blockKind,
    // ref bool initializerContainsInitOnlyItems, StatementTransformContext context)`
    // -- the settings come along (the C# reads them off the context).
    bool IsPartOfInitializer(Block& block, int pos, ILVariable* target,
                             const TypeSystem::IType& rootType, BlockKind& blockKind,
                             bool& initializerContainsInitOnlyItems,
                             const ILTransformSettings& settings);

    // The C# `bool IsValidObjectInitializerTarget(List<AccessPathElement> path)`.
    static bool IsValidObjectInitializerTarget(const std::vector<AccessPathElement>& path);

    // The C# `static bool TypeContainsInitOnlyProperties(ITypeDefinition? typeDefinition)`.
    static bool TypeContainsInitOnlyProperties(const TypeSystem::ITypeDefinition* typeDefinition);

    // The C# local function `void MarkUsedIndices()` inside
    // IsPartOfInitializer (the C# closure reads the `usedIndexVariables` list
    // from the enclosing GetAccessPath call; the port passes it in).
    void MarkUsedIndices(const std::vector<ILVariable*>& usedIndices);

    // The C# `static bool IsMethodApplicable(IMethod method, ..., CSharpResolver
    // resolver, DecompilerSettings settings)`: the resolver-independent subset
    // (the static check, the accessor-owner shortcut, the "Add" name check,
    // the IEnumerable base-type check). The extension-method and
    // generic-Add-inference arms are deferred with the resolver surface.
    static bool IsMethodApplicable(const TypeSystem::IMethod& method,
                                   const std::vector<ILInstruction*>& arguments,
                                   const TypeSystem::IType& rootType,
                                   const ILTransformSettings& settings);

    // The C# `static (kind, path, values, target, usedIndexVariables)
    // AccessPathElement.GetAccessPath(ILInstruction instruction, IType rootType,
    // DecompilerSettings? settings, CSharpResolver? resolver)` -- a free
    // function in the cpp (the C# nests it in the struct).
    friend AccessPath AccessPathElementGetAccessPath(ILInstruction* instruction,
                                                     const TypeSystem::IType& rootType,
                                                     const ILTransformSettings& settings);

    // The C# member state (fields of the transform instance):
    // `readonly Dictionary<ILVariable, (int Index, ILInstruction Value)>
    // possibleIndexVariables` -- keyed by pointer identity (the C# reference
    // equality); the value is (ChildIndex of the stloc, raw non-owning pointer
    // to its value -- never read after, kept for the C# tuple shape).
    std::map<ILVariable*, std::pair<int, ILInstruction*>, std::less<>>
        possibleIndexVariables;
    // The C# `readonly List<AccessPathElement> currentPath`.
    std::vector<AccessPathElement> currentPath;
    // The C# `bool isCollection`.
    bool isCollection = false;
    // The C# `readonly Stack<HashSet<AccessPathElement>> pathStack` -- the
    // port uses a vector of sets with the comparator above.
    std::vector<std::set<AccessPathElement, bool (*)(const AccessPathElement&,
                                                     const AccessPathElement&)>>
        pathStack;
};

} // namespace ILSpy::Decompiler::IL