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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of the `[Flags] public enum Modifiers` in
// ICSharpCode.Decompiler/CSharp/Syntax/Modifiers.cs -- the member-modifier bitmask shared by
// every declaration node that carries modifiers (`VariableDeclarationStatement`, the
// `TypeMember` hierarchy -- `MethodDeclaration`/`FieldDeclaration`/... -- and the
// `EntityDeclaration` base). The first ported `[Flags]` enum and the first ported enum used as a
// SCALAR (not a `[Slot]`) on a node: the `VariableDeclarationStatement.Modifiers` property is a
// plain settable `Modifiers`-typed property (no `[Slot]` attribute), so the generator adds it to
// `MembersToMatch` as a `MatchAny` term (the enum declares an `Any` member) and to the ctor params
// (a settable enum-typed scalar is a required ctor param, the generator's line-198 rule).
//
// The C# default underlying type is `int`; the `Any` member is `unchecked((int)0x80000000)` -- the
// sign bit, a negative `int` (the `unchecked` suppresses the overflow check that would otherwise
// reject the literal). The port uses an UNSIGNED `std::uint32_t` underlying type so `Any`'s
// `0x80000000` fits as a plain value rather than relying on the implementation-defined behaviour
// of a signed `0x80000000` literal. The bit values `None`..`Required` are all positive and fit in
// either representation, so the change is observable only on `Any` (the wildcard sentinel).
//
// The generator's `DoMatchTerm` (DecompilerSyntaxTreeGenerator.cs) detects the `Any` member BY NAME
// (it iterates the enum's `GetMembers()` and tests `_.Name == "Any"`), NOT via the `[Flags]`
// attribute, and emits the `Any`-wildcard term `(this.{member} == {typeName}.Any ||
// this.{member} == o.{member})` -- the same shape as `BinaryOperatorType.Any` (D229) /
// `UnaryOperatorType.Any` (D231) / `AssignmentOperatorType.Any` (D230). The `[Flags]` attribute
// does not change the generator's behaviour: the term is the plain `==` value equality, NOT a
// bitmask test -- a pattern with `Modifiers = Static` matches only a candidate whose `Modifiers`
// is EXACTLY `Static` (not `Static | Public`), and `Any` matches any candidate regardless of its
// modifier bits. So the `[Flags]` semantics (combinable bits) are a construction-time concern
// (a node is built with `Modifiers::Static | Modifiers::Public`), while the pattern match is an
// exact-value comparison with an `Any` escape hatch.
//
// The `CSharpModifiers` static helper (`GetModifierName`/`AllModifiers`, the output-order
// lookup table consumed by the output visitor) is ported below the enum + operators, completing
// the D270 deferral now that the output stage (D316+) has landed. It is a behaviour helper, not
// part of the value type's core semantics.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_MODIFIERS_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_MODIFIERS_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `[Flags] public enum Modifiers`. The `enum class` (scoped, no implicit conversion to/
// from `int`, matching the C# enum's typed usage). The `std::uint32_t` underlying type lets
// `Any = 0x80000000` fit as a plain unsigned value. `VisibilityMask` is the bitwise OR of the
// four visibility members (`Private | Internal | Protected | Public == 0x000F`); the other
// members are the individual modifier bits in their C# order.
enum class Modifiers : std::uint32_t {
    None = 0,
    Private = 0x0001,
    Internal = 0x0002,
    Protected = 0x0004,
    Public = 0x0008,
    Abstract = 0x0010,
    Virtual = 0x0020,
    Sealed = 0x0040,
    Static = 0x0080,
    Override = 0x0100,
    Readonly = 0x0200,
    Const = 0x0400,
    New = 0x0800,
    Partial = 0x1000,
    Extern = 0x2000,
    Volatile = 0x4000,
    Unsafe = 0x8000,
    Async = 0x10000,
    Ref = 0x20000,
    Required = 0x40000,
    // The visibility bitmask (Private | Internal | Protected | Public == 0x000F).
    VisibilityMask = Private | Internal | Protected | Public,
    // The pattern-matching wildcard (matches any candidate `Modifiers` value). The high bit; a
    // plain `0x80000000` on the unsigned underlying type.
    Any = 0x80000000,
};

// The `[Flags]` bitwise operators. The C# `[Flags]` enum has them implicitly (the C# compiler
// defines `|`/`&`/`^`/`~` on any `enum`); the C++ `enum class` does NOT, so they are defined here.
// Part of the value type's core semantics -- constructing a combined mask (e.g.
// `Modifiers::Static | Modifiers::Public`) and testing a bit (e.g.
// `(mods & Modifiers::Static) != Modifiers::None`) -- NOT an output-stage helper, so they land now
// (the `CSharpModifiers` lookup table is the deferred part). The `|`/`&`/`^`/`~` overloads return
// the enum type (preserving the typed surface); the compound assignments (`|=`/`&=`/`^=`) are
// generated implicitly from the binary operators on lvalues.
inline Modifiers operator|(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline Modifiers operator&(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline Modifiers operator^(Modifiers a, Modifiers b) {
    return static_cast<Modifiers>(
        static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
}
inline Modifiers operator~(Modifiers a) {
    return static_cast<Modifiers>(~static_cast<std::uint32_t>(a));
}

// The C# `public static class CSharpModifiers` -- the output-order modifier lookup table the
// `CSharpOutputVisitor.WriteModifiers` iterates (`AllModifiers`) and the per-modifier keyword
// name it writes (`GetModifierName`). A C# `static class` ports as a namespace of free
// functions/variables (the `FormattingOptionsFactory` D317 / `Tokens` D323 precedent). The C#
// `ImmutableArray<Modifiers> AllModifiers` ports as an `inline const std::array<Modifiers, N>`
// (one definition shared across TUs, the established `inline`-namespace-variable convention).
// `GetModifierName` returns a `const char*` (a string literal with static storage duration, so
// the pointer is valid for the program lifetime and binds directly to the `std::string_view`
// parameter of `TokenWriter::WriteKeyword` without an allocation). The C# default `throw new
// NotSupportedException` ports to `std::invalid_argument` (an invalid enum value is a bad
// argument, the D316 `ArgumentNullException` -> `std::invalid_argument` precedent).
namespace CSharpModifiers {

// The C# `AllModifiers` -- the modifiers in the order they should be output when generating
// code (the C# `ImmutableArray.Create(...)` list, verbatim). 20 entries (the 19 real modifiers
// plus the `Any` pattern-matching wildcard, which the comment in the C# source notes 'needs to be
// in this list to be usable in the AST').
inline const std::array<Modifiers, 20> AllModifiers{{
    Modifiers::Public, Modifiers::Private, Modifiers::Protected, Modifiers::Internal,
    Modifiers::New,
    Modifiers::Unsafe,
    Modifiers::Static, Modifiers::Abstract, Modifiers::Virtual, Modifiers::Sealed, Modifiers::Override,
    Modifiers::Required, Modifiers::Readonly, Modifiers::Volatile,
    Modifiers::Ref,
    Modifiers::Extern, Modifiers::Partial, Modifiers::Const,
    Modifiers::Async,
    Modifiers::Any,
}};

// The C# `GetModifierName(Modifiers modifier)` -- the lowercase keyword name for a modifier.
inline const char* GetModifierName(Modifiers modifier) {
    switch (modifier) {
        case Modifiers::Private:    return "private";
        case Modifiers::Internal:   return "internal";
        case Modifiers::Protected:  return "protected";
        case Modifiers::Public:     return "public";
        case Modifiers::Abstract:   return "abstract";
        case Modifiers::Virtual:    return "virtual";
        case Modifiers::Sealed:     return "sealed";
        case Modifiers::Static:     return "static";
        case Modifiers::Override:    return "override";
        case Modifiers::Readonly:   return "readonly";
        case Modifiers::Const:     return "const";
        case Modifiers::New:        return "new";
        case Modifiers::Partial:    return "partial";
        case Modifiers::Extern:     return "extern";
        case Modifiers::Volatile:   return "volatile";
        case Modifiers::Unsafe:     return "unsafe";
        case Modifiers::Async:      return "async";
        case Modifiers::Ref:        return "ref";
        case Modifiers::Required:   return "required";
        case Modifiers::Any:        return "any";
        default:
            throw std::invalid_argument("Invalid value for Modifiers");
    }
}

} // namespace CSharpModifiers

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_MODIFIERS_HPP
