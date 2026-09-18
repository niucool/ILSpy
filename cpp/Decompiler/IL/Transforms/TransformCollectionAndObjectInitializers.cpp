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

// The static member-shape helpers of TransformCollectionAndObjectInitializers
// (see the header). The statement-transform body is deferred; Run throws.

#include "Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.hpp"

#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::IL {

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
