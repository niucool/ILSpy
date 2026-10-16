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

// The static member-shape helpers and the statement-scan state machine of
// TransformCollectionAndObjectInitializers (see the header). The statement-
// transform fold body is deferred; Run throws.

#include "Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.hpp"

#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The C# `stloc.Value.Descendants.OfType<IInstructionWithVariableOperand>()
// .Any(ld => ld.Variable == target && (ld is LdLoc || ld is LdLoca))`: whether
// any STRICT descendant (Descendants excludes the instruction itself) loads
// or takes the address of the target variable. The second copy of the
// AccessPathElement.cpp helper (the copied-next-to-second-consumer
// convention).
bool DescendantsLoadTarget(const ILInstruction* inst, const ILVariable* target) {
    for (int i = 0; i < inst->ChildCount(); ++i) {
        const ILInstruction* child = inst->GetChild(i);
        if (child == nullptr) continue;
        if (auto* ld = dynamic_cast<const LdLoc*>(child)) {
            if (ld->Variable.get() == target) return true;
        } else if (auto* lda = dynamic_cast<const LdLoca*>(child)) {
            if (lda->Variable.get() == target) return true;
        }
        if (DescendantsLoadTarget(child, target)) return true;
    }
    return false;
}

} // namespace

bool TransformCollectionAndObjectInitializers::TypeContainsInitOnlyProperties(
    const TypeSystem::ITypeDefinition* typeDefinition) {
    if (typeDefinition == nullptr)
        return false;
    for (const TypeSystem::IProperty* property : typeDefinition->Properties()) {
        if (property == nullptr)
            continue;
        const TypeSystem::IMethod* setter = property->Setter();
        if (setter != nullptr && setter->IsInitOnly())
            return true;
    }
    return false;
}

bool TransformCollectionAndObjectInitializers::IsRecordCloneMethodCall(const Call* ci) {
    const TypeSystem::IMethod* method = ci->Method.get();
    if (method == nullptr)
        return false;
    const TypeSystem::ITypeDefinition* declaringType =
        method->DeclaringTypeDefinition();
    if (declaringType == nullptr || !declaringType->IsRecord())
        return false;
    if (method->Name() != "<Clone>$")
        return false;
    if (ci->Arguments.size() != 1)
        return false;
    return true;
}

bool TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
    const ILInstruction* inst, const ILVariable* variable) {
    if (MatchLdLocRef(inst, variable))
        return true;
    if (const auto* call = dynamic_cast<const Call*>(inst)) {
        // The C# `!call.Method.IsStatic` gate. The port's reader leaves Call
        // ::Method null on the seed path, so an unresolved method falls back to
        // the reader's IsInstanceCall flag (the NamedArgumentTransform
        // null-Method fallback convention).
        bool instanceCall = call->Method ? !call->Method->IsStatic()
                                         : call->IsInstanceCall;
        if (!call->Arguments.empty() && instanceCall)
            return IsMethodCallOnVariable(call->Arguments[0].get(), variable);
    }
    ILInstruction* target = nullptr;
    const TypeSystem::IField* field = nullptr;
    ILInstruction* value = nullptr;
    if (MatchLdFld(inst, target, field) || MatchStFld(inst, target, field, value)
        || MatchLdFlda(inst, target, field)) {
        return IsMethodCallOnVariable(target, variable);
    }
    return false;
}

bool TransformCollectionAndObjectInitializers::IsValidObjectInitializerTarget(
    const std::vector<AccessPathElement>& path) {
    if (path.empty())
        return true;
    const AccessPathElement& element = path.back();
    // The C# `var previous = path.SkipLast(1).LastOrDefault()` plus the
    // `previous != default` test: the walk never produces a value-initialized
    // element, so the default comparison is exactly "there is a previous
    // element".
    const AccessPathElement* previous =
        path.size() >= 2 ? &path[path.size() - 2] : nullptr;
    const auto* p = dynamic_cast<const TypeSystem::IProperty*>(element.Member);
    if (p == nullptr)
        return true;
    if (!p->IsIndexer())
        return true;
    if (previous != nullptr) {
        // The C# EquivalentTypes(previous.Member.ReturnType,
        // element.Member.DeclaringType) NREs on a null declaring type; the port
        // treats it as not equivalent (a member without a declaring type
        // cannot be proven to be the container).
        TypeSystem::ITypePtr declaringType = element.Member->DeclaringType();
        if (declaringType == nullptr)
            return false;
        // The port's EquivalentTypes takes non-const references (the
        // const_cast convention TranslatedExpression uses for the same call).
        TypeSystem::IType& previousReturnType =
            const_cast<TypeSystem::IType&>(previous->Member->ReturnType());
        return TypeSystem::NormalizeTypeVisitor::IgnoreNullabilityAndTuples()
            .EquivalentTypes(previousReturnType, *declaringType);
    }
    return false;
}

void TransformCollectionAndObjectInitializers::ResetInitializerScanState() {
    // The C# Run's per-scan reset (lines 106-110).
    possibleIndexVariables.clear();
    currentPath.clear();
    isCollection = false;
    pathStack.clear();
    pathStack.push_back({});
}

void TransformCollectionAndObjectInitializers::MarkUsedIndices(
    const std::vector<ILVariable*>& usedIndices) {
    // The C# `possibleIndexVariables[index] = (-1, item.Value)` (the tuple
    // reassignment keeping the recorded Value).
    for (ILVariable* index : usedIndices) {
        auto it = possibleIndexVariables.find(index);
        if (it != possibleIndexVariables.end())
            it->second.Index = -1;
    }
}

bool TransformCollectionAndObjectInitializers::IsPartOfInitializer(
    const std::vector<std::unique_ptr<ILInstruction>>& instructions, int pos,
    ILVariable* target, const TypeSystem::IType* rootType,
    BlockKind& blockKind, bool& initializerContainsInitOnlyItems,
    StatementTransformContext& context) {
    // The index-variable arm (C# lines 266-278): a store to a single-definition
    // local that does not reference the initializer variable is recorded as a
    // possible dictionary-initializer index variable. A StLoc that fails the
    // shape (a stack-slot or multi-definition store) falls through to the
    // access-path walk (which rejects it -- StLoc is not a path instruction).
    if (auto* stloc = dynamic_cast<StLoc*>(instructions[pos].get());
        stloc != nullptr && stloc->Variable != nullptr
        && stloc->Variable->Kind == VariableKind::Local
        && stloc->Variable->IsSingleDefinition()) {
        // The C# consults `context.Settings.DictionaryInitializers` -- the
        // same full settings object the GetAccessPath call below passes. The
        // port reads the threaded C#-layer settings when present and falls
        // back to the IL-layer subset field (the D78 convention: the subset
        // IS the IL-layer callers' settings).
        const bool dictionaryInitializers =
            context.Base.CSharpSettings != nullptr
                ? context.Base.CSharpSettings->DictionaryInitializers()
                : context.Base.Settings.DictionaryInitializers;
        if (!dictionaryInitializers)
            return false;
        if (stloc->Value != nullptr
            && DescendantsLoadTarget(stloc->Value.get(), target))
            return false;
        // The C# Dictionary.Add throws on a duplicate key; unreachable through
        // real metadata (a single-definition variable has exactly one store,
        // and the scan visits each instruction once), so the port overwrites.
        possibleIndexVariables[stloc->Variable.get()] =
            PossibleIndexVariableInfo{stloc->ChildIndex, stloc->Value.get()};
        return true;
    }
    auto info = AccessPathElement::GetAccessPath(
        instructions[pos].get(), rootType,
        context.Base.CSharpSettings, context.Base.Resolver);
    if (info.Kind == AccessPathKind::Invalid || target != info.Target)
        return false;
    std::vector<AccessPathElement> newPath = std::move(info.Path);
    std::optional<std::vector<ILInstruction*>> values = std::move(info.Values);
    // Treat the last element separately (C# lines 284-285): it is either an
    // Add method call or the property/field being assigned. GetAccessPath
    // assigns the Setter/Adder kinds only in the arms that insert at least one
    // path element, so the list is never empty on a non-Invalid kind; the
    // empty guard maps the C# List.Last() InvalidOperationException to a
    // scan rejection instead of undefined behavior on a hand-built shape.
    if (newPath.empty())
        return false;
    AccessPathElement lastElement = newPath.back();
    newPath.pop_back();
    // Compare the new path with the current path (C# lines 287-289).
    int minLen = static_cast<int>(std::min(currentPath.size(), newPath.size()));
    int firstDifferenceIndex = 0;
    while (firstDifferenceIndex < minLen
           && newPath[firstDifferenceIndex] == currentPath[firstDifferenceIndex])
        firstDifferenceIndex++;
    // Pop the diverged suffix (C# lines 290-295): every pop also resets the
    // collection mode (leaving a nested collection re-opens the enclosing
    // object-initializer level).
    while (static_cast<int>(currentPath.size()) > firstDifferenceIndex) {
        isCollection = false;
        currentPath.pop_back();
        pathStack.pop_back();
    }
    // Push the new path elements (C# lines 296-303): each level enters the
    // sibling set of the enclosing level (a duplicate sibling member, or a
    // member below an already-entered collection, ends the scan).
    while (currentPath.size() < newPath.size()) {
        AccessPathElement newElement = newPath[currentPath.size()];
        currentPath.push_back(newElement);
        if (isCollection || !pathStack.back().insert(newElement).second)
            return false;
        pathStack.push_back({});
    }
    switch (info.Kind) {
        case AccessPathKind::Adder:
            isCollection = true;
            if (!pathStack.back().empty())
                return false;
            MarkUsedIndices(info.UsedIndexVariables);
            return true;
        case AccessPathKind::Setter:
            if (isCollection || !pathStack.back().insert(lastElement).second)
                return false;
            // The C# `values?.Count != 1` (null or not exactly one value)
            // rejects the statement.
            if (!values.has_value() || values->size() != 1
                || !IsValidObjectInitializerTarget(currentPath))
                return false;
            if (blockKind != BlockKind::ObjectInitializer
                && blockKind != BlockKind::WithInitializer)
                blockKind = BlockKind::ObjectInitializer;
            // The C# `initializerContainsInitOnlyItems |= lastElement.Member
            // is IProperty { Setter.IsInitOnly: true }`.
            if (const auto* property =
                    dynamic_cast<const TypeSystem::IProperty*>(lastElement.Member)) {
                const TypeSystem::IMethod* setter = property->Setter();
                if (setter != nullptr && setter->IsInitOnly())
                    initializerContainsInitOnlyItems = true;
            }
            MarkUsedIndices(info.UsedIndexVariables);
            return true;
        default:
            return false;
    }
}

void TransformCollectionAndObjectInitializers::Run(
    Block& block, int pos, StatementTransformContext& context) {
    // Deferred with the statement-transform body (see the header's Run
    // declaration for the prerequisite list).
    (void)block;
    (void)pos;
    (void)context;
    throw std::logic_error(
        "TransformCollectionAndObjectInitializers::Run is not ported yet");
}

} // namespace ILSpy::Decompiler::IL
