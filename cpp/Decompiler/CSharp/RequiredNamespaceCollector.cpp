// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/CSharp/RequiredNamespaceCollector.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"

#include <optional>

namespace ILSpy::Decompiler::CSharp {
namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

} // namespace

RequiredNamespaceCollector::RequiredNamespaceCollector(
    std::unordered_set<std::string>& namespaces,
    bool seedKnownTypeNamespaces, bool minimalUsingSet)
    : namespaces_(namespaces),
      minimalUsingSet_(minimalUsingSet) {
    // The C# ctor loop: every known type's namespace is a `using` seed (the
    // System / System.Collections.Generic / ... roots the emitted file may
    // reference through known-type spellings). The C#
    // `KnownTypeReference.Get((KnownTypeCode)i)` walks the table indices and
    // skips the null slots; the port's AllKnownTypes snapshot is the same
    // walk minus the nulls. The unseeded form serves the flat -t render's
    // minimal using set (the C#'s candidate pool exists for the resolver's
    // IntroduceUsingDeclarations filtering; without a resolver the walk's
    // own references ARE the required set).
    if (!seedKnownTypeNamespaces)
        return;
    for (const TS::KnownTypeReference* ktr : TS::KnownTypeReference::AllKnownTypes()) {
        namespaces_.emplace(std::string(ktr->Namespace()));
    }
}

// The C# `void CollectNamespacesForTypeReference(IType type)` (private): the
// pre-order walk over the type graph with the visited-types gate. The C#
// switch arms map onto the port's concrete IType hierarchy (ParameterizedType,
// the TypeWithElementType family = ArrayType/ByReferenceType/PointerType/
// ModifiedType, TupleType, FunctionPointerType; the default adds the
// namespace). Every arm terminates with the base-type namespace sweep.
void RequiredNamespaceCollector::CollectTypeReference(const TS::IType* type) {
    if (type == nullptr) return;
    if (!visitedTypes_.insert(type).second) return;
    // The C# `case ParameterizedType`: the namespace + the generic type + the
    // type arguments, and an early return ("no need to collect base types
    // again" -- the generic type's own base sweep covers them).
    const auto* parameterized = dynamic_cast<const TS::ParameterizedType*>(type);
    if (parameterized != nullptr) {
        namespaces_.emplace(parameterized->Namespace());
        CollectTypeReference(parameterized->GenericType().get());
        for (const TS::ITypePtr& arg : parameterized->TypeArguments()) {
            CollectTypeReference(arg.get());
        }
        return;
    }
    // The C# `case TypeWithElementType`: arrays / byrefs / pointers / modified
    // types recurse into the element type and FALL THROUGH to the base-type
    // sweep.
    if (const auto* arrayType = dynamic_cast<const TS::ArrayType*>(type)) {
        CollectTypeReference(arrayType->Element().get());
    } else if (const auto* byRefType =
                   dynamic_cast<const TS::ByReferenceType*>(type)) {
        CollectTypeReference(byRefType->Element().get());
    } else if (const auto* pointerType =
                   dynamic_cast<const TS::PointerType*>(type)) {
        CollectTypeReference(pointerType->Element().get());
    }
    // The C# `case TupleType`: the element types recurse (the fall-through to
    // the base sweep applies too -- a TupleType's underlying ValueTuple chain
    // contributes its namespaces).
    if (const auto* fnPtr = dynamic_cast<const TS::FunctionPointerType*>(type)) {
        CollectTypeReference(fnPtr->ReturnType().get());
        for (const TS::ITypePtr& paramType : fnPtr->ParameterTypes()) {
            CollectTypeReference(paramType.get());
        }
        return;
    }
    // The default arm + the base-type sweep.
    namespaces_.emplace(type->Namespace());
    if (!minimalUsingSet_) {
        // The candidate-pool behavior (the C#'s resolver filters the
        // pool against the rendered names); the minimal using set skips
        // the sweep -- a referenced type's base types never render in the
        // referencing position, so their namespaces are not required.
        for (const TS::ITypePtr& baseType : type->DirectBaseTypes()) {
            namespaces_.emplace(baseType->Namespace());
        }
    }
}

} // namespace ILSpy::Decompiler::CSharp