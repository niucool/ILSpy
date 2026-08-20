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

// Port of ICSharpCode.Decompiler/TypeSystem/Accessibility.cs -- the Accessibility
// enum (the visibility of a type-system entity: None/Private/.../Public) and the
// AccessibilityExtensions partial-order helpers (LessThanOrEqual / Intersect /
// Union). Accessibility is a leaf dependency of TypeSystemAstBuilder: its static
// ModifierFromAccessibility switches on the value to map a symbol's visibility to
// the Syntax Modifiers bits, and ConvertAccessor/NeedsAccessibility compare an
// accessor's Accessibility against its owner's.
//
// The C# enum is `: byte` and the helpers depend on the underlying integer values
// being sorted similar to the partial order (the C# comment:
//   none -> private -> protected_and_internal -> protected -> protected_or_internal -> public;
//   none -> private -> protected_and_internal -> internal  -> protected_or_internal -> public;
// ). `protected` and `internal` are the only pair for which the integer order does
// NOT match the partial order (they are incomparable), so the helpers carry an
// explicit correction for that pair. The enum values None..Public are therefore
// pinned to 0..6 in declaration order -- do not reorder them.
//
// The C# `AccessibilityExtensions` static class (extension methods) ports to free
// functions in this namespace. The fourth member, `EffectiveAccessibility(IEntity)`,
// walks the entity's DeclaringTypeDefinition chain and is deferred until the
// IEntity / ITypeDefinition interfaces land; it is documented at the bottom.

#pragma once

#include <cassert>
#include <cstdint>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

enum class Accessibility : std::uint8_t {
    None,
    Private,
    ProtectedAndInternal,
    Protected,
    Internal,
    ProtectedOrInternal,
    Public,
};

// The C# built-in enum comparison (`a <= b`, `a > b`) compares the underlying
// byte values, which the partial-order helpers below rely on. A C++ `enum class`
// has `==`/`!=` but no relational operators, so these free functions mirror the
// C# built-in comparison on the underlying uint8_t values.
constexpr bool operator<(Accessibility a, Accessibility b) noexcept {
    return static_cast<std::uint8_t>(a) < static_cast<std::uint8_t>(b);
}
constexpr bool operator>(Accessibility a, Accessibility b) noexcept { return b < a; }
constexpr bool operator<=(Accessibility a, Accessibility b) noexcept { return !(b < a); }
constexpr bool operator>=(Accessibility a, Accessibility b) noexcept { return !(a < b); }

// LessThanOrEqual: true iff b is accessible everywhere a is accessible (the
// partial order). The protected-vs-internal pair is the sole correction to the
// raw integer order: the integers have Protected(3) < Internal(4), but in the
// partial order the two are incomparable, so LessThanOrEqual(Protected,
// Internal) is false even though 3 <= 4.
constexpr bool LessThanOrEqual(Accessibility a, Accessibility b) noexcept {
    return a <= b && !(a == Accessibility::Protected && b == Accessibility::Internal);
}

// Intersect: the result is accessible from a point iff both a and b are. After
// ordering so a <= b the result is the more-restrictive (smaller) value, except
// the protected-vs-internal pair intersects to ProtectedAndInternal ("private
// protected").
inline Accessibility Intersect(Accessibility a, Accessibility b) noexcept {
    if (a > b) {
        std::swap(a, b);
    }
    if (a == Accessibility::Protected && b == Accessibility::Internal) {
        return Accessibility::ProtectedAndInternal;
    }
    assert(!(a == Accessibility::Internal && b == Accessibility::Protected));
    return a;
}

// Union: the result is accessible from a point iff at least one of a or b is.
// After ordering so a <= b the result is the less-restrictive (larger) value,
// except the protected-vs-internal pair unions to ProtectedOrInternal
// ("protected internal").
inline Accessibility Union(Accessibility a, Accessibility b) noexcept {
    if (a > b) {
        std::swap(a, b);
    }
    if (a == Accessibility::Protected && b == Accessibility::Internal) {
        return Accessibility::ProtectedOrInternal;
    }
    assert(!(a == Accessibility::Internal && b == Accessibility::Protected));
    return b;
}

// Deferred: AccessibilityExtensions.EffectiveAccessibility(IEntity entity) walks
// entity.DeclaringTypeDefinition, intersecting the entity's Accessibility with
// each enclosing type definition's. It lands with the IEntity / ITypeDefinition
// interfaces (the next TypeSystem pieces TypeSystemAstBuilder needs).

} // namespace ILSpy::Decompiler::TypeSystem
