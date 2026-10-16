// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `TypeConstraint` readonly struct co-located in
// ICSharpCode.Decompiler/TypeSystem/ITypeParameter.cs. A `TypeConstraint` is one element of
// `ITypeParameter.TypeConstraints` -- a single constraint type (with its own attributes)
// applied to a generic type parameter, e.g. the `Base` in `where T : Base, IInterface`. The
// C# `readonly struct TypeConstraint` (a value type) carries the constraint `Type` (a
// non-null `IType`, the constructor throws `ArgumentNullException` on null), the constraint's
// `Attributes` (a possibly-empty `IReadOnlyList<IAttribute>`, defaulting to empty), and a
// computed `SymbolKind` property that always returns `SymbolKind.Constraint`.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `SymbolKind SymbolKind => SymbolKind.Constraint;` is a computed property. In
//      C++ the member function `SymbolKind()` returning `SymbolKind` (the enum) compiles for
//      the return type (parsed before the declarator, so the unqualified `SymbolKind`
//      resolves to the namespace-scope enum -- `TypeConstraint` does NOT derive from
//      `ISymbol`, so there is no inherited `SymbolKind()` member function to hide the enum,
//      unlike the D372 cross-scope name-hiding crux). In the BODY the member function
//      `SymbolKind()` IS in scope and would hide the enum, so the returned value is
//      globally qualified to reach the enum unambiguously.
//  (b) The C# `IType Type { get; }` (non-null, the ctor throws on null) ports to `ITypePtr`
//      (the D271 `std::shared_ptr<IType>` owned-type handle); a null `shared_ptr` is the
//      faithful representation of a constraint with no type, and the constructor asserts
//      non-null (the C# `ArgumentNullException` port -- the C++ port uses `assert` because
//      `TypeConstraint` is a TypeSystem-layer value type reached via the gtest suite, not a
//      decompiler path with structured exception handling).
//  (c) The C# `IReadOnlyList<IAttribute> Attributes` ports to `std::vector<const IAttribute*>`
//      (a non-owning snapshot -- the element type is a complete pointer type regardless of
//      the pointee's completeness, so the vector instantiates with only `IAttribute`
//      forward-declared, the `IEntity::GetAttributes` / `IParameter::GetAttributes`
//      precedent). The C# `attributes ?? EmptyList<IAttribute>.Instance` default ports to
//      an empty vector (the ctor's default argument).

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <cassert>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of the not-yet-ported `IAttribute` (the attribute interface). The
// `Attributes` collection holds `const IAttribute*` (a complete pointer type regardless of
// the pointee's completeness), so `IAttribute` needs only a forward declaration here (the
// `IEntity::GetAttributes` / `IParameter::GetAttributes` precedent).
class IAttribute;

// A single type constraint on a generic type parameter (one element of
// `ITypeParameter.TypeConstraints`). Carries the constraint `Type` (non-null), the
// constraint's `Attributes` (a non-owning snapshot), and a computed `SymbolKind` that is
// always `SymbolKind::Constraint`. A value type (the C# `readonly struct`); default
// construction is NOT provided (the C# ctor requires a non-null type).
struct TypeConstraint {
    // The C# `TypeConstraint(IType type, IReadOnlyList<IAttribute>? attributes = null)`.
    // The type must be non-null (the C# throws `ArgumentNullException`); the C++ port
    // asserts. The attributes default to an empty vector (the C# `?? EmptyList`).
    TypeConstraint(ITypePtr type, std::vector<const IAttribute*> attributes = {})
        : type_(std::move(type)), attributes_(std::move(attributes))
    {
        assert(type_ && "TypeConstraint: type must not be null");
    }

    // The C# `SymbolKind SymbolKind => SymbolKind.Constraint;` -- a computed property that
    // is always `SymbolKind::Constraint`. The return type `SymbolKind` resolves to the
    // namespace-scope enum (parsed before the declarator; `TypeConstraint` does not derive
    // from `ISymbol`, so no inherited `SymbolKind()` hides the enum). The body qualifies the
    // value globally because the member function `SymbolKind()` is in scope in the body and
    // would otherwise hide the enum.
    SymbolKind SymbolKind() const
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Constraint;
    }

    // The C# `IType Type { get; }` -- the constraint type (non-null). A nullable
    // `ITypePtr` (the shared, cached `IType` handle); the constructor asserts non-null.
    ITypePtr Type() const { return type_; }

    // The C# `IReadOnlyList<IAttribute> Attributes { get; }` -- the attributes on this
    // constraint (a non-owning snapshot; the constraint owns its attributes, the caller
    // holds raw pointers). Empty by default.
    std::vector<const IAttribute*> Attributes() const { return attributes_; }

private:
    ITypePtr type_;
    std::vector<const IAttribute*> attributes_;
};

} // namespace ILSpy::Decompiler::TypeSystem
