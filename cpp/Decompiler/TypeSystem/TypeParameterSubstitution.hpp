// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without including limitation, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/TypeParameterSubstitution.cs -- the first
// concrete `TypeVisitor` (D406). A `TypeParameterSubstitution` substitutes class and
// method type parameters with the supplied type arguments: `VisitTypeParameter` swaps
// the parameter at its `Index` for the matching entry in `ClassTypeArguments` (when the
// owner is a `TypeDefinition`) or `MethodTypeArguments` (when the owner is a `Method`),
// returning `SpecialType.UnknownType` for an out-of-range index, and leaving the
// parameter unchanged (the base identity) when the matching list is absent (kept
// unmodified). `null` / an absent list means "keep this kind of type parameter
// unmodified"; an empty list means "substitute with nothing" (every index is out of
// range -> `UnknownType`). The `Identity` singleton passes both lists as null, so it
// leaves every type parameter unchanged (the identity function).
//
// This is the concrete `TypeVisitor` the `IMember` long-pole references: `IMember`
// (D387) declares `Substitution` / `Specialize` / `Equals` over `const
// TypeParameterSubstitution*` / `const TypeVisitor*` (forward-declared there, deferred);
// a specialized member's `Specialize(substitution)` runs each of its types through
// `substitution` via `IType::AcceptVisitor`. Landing this class makes the
// `TypeParameterSubstitution` type a real `TypeVisitor` the member family can reference.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IReadOnlyList<IType> classTypeArguments` / `methodTypeArguments` (nullable
//      reference lists -- `null` = keep unmodified) port to `std::optional<std::vector<ITypePtr>>`
//      -- `std::nullopt` = keep unmodified (the C# `null`), a present (possibly empty) vector
//      = the substitute list. An empty list is distinct from `nullopt` (an empty list
//      substitutes every index out of range -> `UnknownType`; `nullopt` leaves the
//      parameter unchanged). `std::optional` is the faithful C++ representation of the
//      nullable `IReadOnlyList<IType>` reference: the `has_value()` test is the C# `!= null`
//      test.
//  (b) The `Identity` singleton (the C# `static readonly Identity = new
//      TypeParameterSubstitution(null, null)`) ports to a function-local static (the
//      Meyers-singleton pattern, the `StringComparer::Ordinal` D398 / `KnownAttributeTypeNames`
//      D378 precedent), returning `const TypeParameterSubstitution&` -- the C++ counterpart
//      of the C# `static readonly` reference-type field; the same instance is returned across
//      calls (stable singleton, pointer-identity).
//  (c) `Compose(g, f)` is the function-composition helper (`t.AcceptVisitor(Compose(g, f))`
//      equals `t.AcceptVisitor(f).AcceptVisitor(g)`). The C# reference-type parameters
//      (`g` / `f`, both nullable, returning a reference) port to non-const nullable raw
//      pointers and a by-value return: a fresh `TypeParameterSubstitution` (the C# heap
//      allocation realized as a value). `Compose(nullptr, nullptr)` returns the `Identity`
//      (a by-value copy of it) -- behaviorally equivalent to the C# `null` result (both
//      leave every type parameter unchanged), since the C++ by-value return cannot express
//      null. The pointers are NON-const because `Compose` runs `input[i].AcceptVisitor(*g)`
//      and `AcceptVisitor(TypeVisitor&)` takes a non-const reference (the D406 non-const
//      TypeVisitor convention) -- the C# params are not readonly.
//  (d) The `VisitTypeParameter` body reads `type.Index()` / `type.OwnerType()` (the
//      `ITypeParameter` interface D383 accessors), so it is OUT-OF-LINE in the .cpp (which
//      includes `ITypeParameter.hpp`), mirroring the `TypeVisitor.cpp` out-of-line
//      `VisitTypeParameter` default -- the header forward-declares `ITypeParameter` (already
//      forward-declared by `TypeVisitor.hpp`) and declares only the override signature.
//  (e) `GetHashCode`'s `element.GetHashCode()` (the C# `object.GetHashCode` identity hash
//      that `AbstractType.GetHashCode` returns via `RuntimeHelpers.GetHashCode(this)`) ports
//      to `std::hash<IType*>{}(element.get())` cast to `int` (the identity hash of the
//      shared `IType` object's address) -- the faithful C++ counterpart of the .NET
//      identity-hash; the C++ minimal `IType` port has no `GetHashCode` member, so the
//      pointer-identity hash is the faithful substitute. The `unchecked` wraparound
//      arithmetic ports to `unsigned int` accumulation + `static_cast<int>` return (the
//      D400 `FullTypeNameComparer.GetHashCode` unchecked-wraparound convention).
//  (f) The C# `VisitNullabilityAnnotatedType` override checks `type is
//      NullabilityAnnotatedTypeParameter` (the nested subclass that wraps an
//      `ITypeParameter`, D402 deferred). The minimal port DEFERS this override -- the base
//      `VisitNullabilityAnnotatedType` (the `VisitChildren` reconstruct-the-wrapper default
//      D406) handles a plain `NullabilityAnnotatedType`; the `NullabilityAnnotatedTypeParameter`
//      nested subclass lands with the rest of Phase 2.
//  (g) The C# `Equals(object)` overrides `object.Equals` (no C++ base); the port provides a
//      standalone `bool Equals(const TypeParameterSubstitution* other) const` (nullable
//      pointer, the C# `obj as TypeParameterSubstitution` + `if (other == null) return
//      false` check), distinguished from `Equals(other, normalization)` by parameter count.

#pragma once

#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// A class/method type-parameter substitution (the first concrete TypeVisitor). Override
// of `VisitTypeParameter` swaps each type parameter for the matching entry in the
// supplied class/method type-argument list; the `Identity` singleton leaves every
// parameter unchanged. `Compose` composes two substitutions (function composition).
class TypeParameterSubstitution : public TypeVisitor {
public:
    // The identity function (the C# `static readonly Identity = new
    // TypeParameterSubstitution(null, null)`). A function-local static singleton returned
    // by const reference (the Meyers-singleton pattern).
    static const TypeParameterSubstitution& Identity();

    // The C# `TypeParameterSubstitution(IReadOnlyList<IType> classTypeArguments,
    // IReadOnlyList<IType> methodTypeArguments)`. Pass `std::nullopt` to keep that kind of
    // type parameter unmodified (the C# `null`); a present (possibly empty) vector
    // substitutes. The vectors are moved into the members.
    TypeParameterSubstitution(std::optional<std::vector<ITypePtr>> classTypeArguments,
                              std::optional<std::vector<ITypePtr>> methodTypeArguments)
        : classTypeArguments_(std::move(classTypeArguments)),
          methodTypeArguments_(std::move(methodTypeArguments)) {}

    // The C# `IReadOnlyList<IType> ClassTypeArguments { get; }` -- `std::nullopt` means
    // "keeps class type parameters unmodified" (the C# null). Returned by const reference
    // (the member holds the list; the caller reads it, no copy).
    const std::optional<std::vector<ITypePtr>>& ClassTypeArguments() const noexcept {
        return classTypeArguments_;
    }
    // The C# `IReadOnlyList<IType> MethodTypeArguments { get; }` -- `std::nullopt` means
    // "keeps method type parameters unmodified".
    const std::optional<std::vector<ITypePtr>>& MethodTypeArguments() const noexcept {
        return methodTypeArguments_;
    }

    // The C# `public static TypeParameterSubstitution Compose(TypeParameterSubstitution g,
    // TypeParameterSubstitution f)`. Function composition: for all types `t`,
    // `t.AcceptVisitor(Compose(g, f))` equals `t.AcceptVisitor(f).AcceptVisitor(g)`. Both
    // pointers are nullable (non-const -- `AcceptVisitor` takes a non-const reference); the
    // result is a fresh by-value `TypeParameterSubstitution` (the C# heap allocation).
    static TypeParameterSubstitution Compose(TypeParameterSubstitution* g,
                                             TypeParameterSubstitution* f);

    // The C# `public bool Equals(TypeParameterSubstitution other, TypeVisitor
    // normalization)`. Compares the two lists element-wise, normalizing each element
    // through `normalization` before comparing (the C# `a[i].AcceptVisitor(normalization)`
    // vs `b[i].AcceptVisitor(normalization)`). `other` is a nullable pointer (the C# `if
    // (other == null) return false`); `normalization` is a non-const reference
    // (`AcceptVisitor` takes a non-const reference).
    bool Equals(const TypeParameterSubstitution* other, TypeVisitor& normalization) const;

    // The C# `public override bool Equals(object obj)`. Compares the two lists
    // element-wise by `IType::Equals` (no normalization). `other` is a nullable pointer
    // (the C# `obj as TypeParameterSubstitution` + null check).
    bool Equals(const TypeParameterSubstitution* other) const;

    // The C# `public override int GetHashCode()`.
    int GetHashCode() const;

    // The C# `public override IType VisitTypeParameter(ITypeParameter type)`. Out-of-line
    // (the body reads `type.Index()` / `type.OwnerType()`, needing `ITypeParameter`
    // complete -- included by the .cpp, not the header). Returns the substituted type
    // argument, `UnknownType` for an out-of-range index, or the base identity when the
    // matching list is absent.
    ITypePtr VisitTypeParameter(ITypeParameter& type) override;

    // The C# `public override string ToString()` -- the ECMA-335 backtick form (`0 ->
    // ReflectionName for class type args, ``0 -> ReflectionName for method type args,
    // [] for an empty list, omitted for an absent list).
    std::string ToString() const;

private:
    std::optional<std::vector<ITypePtr>> classTypeArguments_;
    std::optional<std::vector<ITypePtr>> methodTypeArguments_;

    // The C# `static bool TypeListEquals(a, b)` (no normalization) -- both absent -> equal
    // (both keep unmodified); one absent -> not equal; otherwise element-wise by
    // `IType::Equals`. (The C# reference-equality `a == b` short-circuit has no value-semantic
    // counterpart and is folded into the element-wise compare.)
    static bool TypeListEquals(const std::optional<std::vector<ITypePtr>>& a,
                                const std::optional<std::vector<ITypePtr>>& b);

    // The C# `static bool TypeListEquals(a, b, TypeVisitor normalization)` -- same, but
    // normalizes each element through `normalization` before comparing.
    static bool TypeListEquals(const std::optional<std::vector<ITypePtr>>& a,
                                const std::optional<std::vector<ITypePtr>>& b,
                                TypeVisitor& normalization);

    // The C# `static int TypeListHashCode(IReadOnlyList<IType> obj)` -- 0 for an absent
    // list; otherwise `hashCode = hashCode * 27 + element.GetHashCode()` (the identity
    // hash of each element) per element, starting at 1.
    static int TypeListHashCode(const std::optional<std::vector<ITypePtr>>& obj);

    // The C# `static IReadOnlyList<IType> GetComposedTypeArguments(input, substitution)` --
    // applies `substitution` to each element of `input` via `AcceptVisitor`.
    static std::vector<ITypePtr> GetComposedTypeArguments(const std::vector<ITypePtr>& input,
                                                          TypeParameterSubstitution& substitution);
};

} // namespace ILSpy::Decompiler::TypeSystem
