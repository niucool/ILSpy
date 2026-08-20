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

// Port of ICSharpCode.Decompiler/Output/IAmbience.cs -- the `[Flags] ConversionFlags`
// enum and the `IAmbience` interface. Ambiences convert type-system symbols and types to
// display text (editor tooltips, member overviews, ...); the two C# implementations are
// `CSharpAmbience` (the C#-syntax rendering, the next in-order output-stage file) and
// `ILAmbience` (the IL view, Phase 6). The interface is a leaf `Output`-namespace
// dependency of `CSharpAmbience` (which `: IAmbience` and exposes `ConversionFlags`), the
// next in-order output-stage file after the D322-D371 `CSharpOutputVisitor` /
// `InsertParenthesesVisitor` / `DepthFirstAstVisitorBool` / `GenericGrammarAmbiguityVisitor`
// set; the concrete `CSharpAmbience` lands once its remaining `TypeSystemAstBuilder`
// dependency (which needs the full resolver) is unblocked.
//
// C#-to-C++ porting decisions:
//  * `[Flags] public enum ConversionFlags` -> a `std::uint32_t`-backed `enum class` (the
//    largest member `SupportExtensionDeclarations = 0x400000` fits as a plain unsigned
//    value; the C# `: int` / `[Flags]` underlying semantics). The `|`/`&`/`^`/`~` bitwise
//    operators the C# `[Flags]` compiler generates implicitly are defined here as free
//    functions (the C++ `enum class` does NOT have them), the `Modifiers` D271 precedent.
//    `StandardConversionFlags` is the bitwise OR of its constituent members (the C#
//    `ShowParameterNames | ShowAccessibility | ...`), expressed with the `|` operator in
//    the enumerator initializer (the same form the `Modifiers::VisibilityMask` initializer
//    uses, which compiles in this MSVC build); `All = 0x1fffff` is the C# literal verbatim
//    (it does NOT include the high `UsePrivateProtectedAccessibility`/`Support*` bits -- a
//    quirk of the C# source, ported faithfully).
//  * `interface IAmbience` -> a C++ abstract base with a virtual destructor and one
//    pure-virtual per C# member, the established C#-interface-to-C++-abstract-base
//    convention (the `IType` / `IAstVisitor` precedents).
//  * `ConversionFlags ConversionFlags { get; set; }` -> a pure-virtual const getter
//    `ConversionFlags()` plus a pure-virtual setter `ConversionFlags(value)`. The getter
//    return type is parsed before its own declarator, so the unqualified `ConversionFlags`
//    resolves to the enum (the same-namespace type); the setter's parameter type is parsed
//    AFTER the getter is in scope, where an unqualified `ConversionFlags` would resolve to
//    the (non-type) getter member and fail (`error C2061`, verified), so the setter
//    parameter is fully qualified `::ILSpy::Decompiler::Output::ConversionFlags`.
//  * `string ConvertSymbol(ISymbol)` / `ConvertType(IType)` -> `std::string` taking
//    `const ISymbol&` / `const IType&` (the C# passes the reference types by value, which
//    is by-reference under the C# reference-semantics; the C++ takes a const reference,
//    non-owning). `ISymbol` (this iteration's D372) and `IType` are forward-declared (a
//    reference parameter needs only a forward declaration), so no `ISymbol.hpp` /
//    `IType.hpp` include is pulled here.
//  * `string ConvertConstantValue(object constantValue)` -> `std::string` taking
//    `const PrimitiveValue&`. The C# `object` is a boxed literal value; the only non-throwing
//    implementation (`CSharpAmbience`) delegates to `TextWriterTokenWriter.PrintPrimitiveValue`,
//    whose C++ signature takes `const PrimitiveValue&` (the D228 faithful boxed-literal
//    `std::variant`); `PrimitiveValue` is a `using`-alias for the variant (not forward-
//    declarable), so `PrimitiveExpression.hpp` is included here.
//  * `string WrapComment(string comment)` -> `std::string` taking `std::string_view`
//    (zero-copy; the only implementation prepends `"// "`, the established read-only-string
//    convention).

#pragma once

#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"  // PrimitiveValue

#include <cstdint>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::TypeSystem {
class ISymbol;  // forward declaration (a reference parameter needs only this)
class IType;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::Output {

using TypeSystem::ISymbol;
using TypeSystem::IType;
using CSharp::Syntax::PrimitiveValue;

// The C# `[Flags] public enum ConversionFlags`. The `enum class` (scoped, no implicit
// conversion to/from `int`, matching the C# enum's typed usage). The `std::uint32_t`
// underlying type lets `SupportExtensionDeclarations = 0x400000` fit as a plain unsigned
// value. The member order and values mirror the C# exactly (each individual flag's numeric
// value is its declaration index / the C# literal); `StandardConversionFlags` is the
// bitwise OR of its constituents; `All = 0x1fffff` is the C# literal verbatim.
enum class ConversionFlags : std::uint32_t {
	None = 0,
	ShowParameterList = 1,
	ShowParameterNames = 2,
	ShowAccessibility = 4,
	ShowDefinitionKeyword = 8,
	ShowDeclaringType = 0x10,
	ShowModifiers = 0x20,
	ShowReturnType = 0x40,
	UseFullyQualifiedTypeNames = 0x80,
	ShowTypeParameterList = 0x100,
	ShowBody = 0x200,
	UseFullyQualifiedEntityNames = 0x400,
	PlaceReturnTypeAfterParameterList = 0x800,
	ShowTypeParameterVarianceModifier = 0x1000,
	ShowParameterModifiers = 0x2000,
	ShowParameterDefaultValues = 0x4000,
	UseNullableSpecifierForValueTypes = 0x8000,
	SupportInitAccessors = 0x10000,
	SupportRecordClasses = 0x20000,
	SupportRecordStructs = 0x40000,
	SupportUnsignedRightShift = 0x80000,
	SupportOperatorChecked = 0x100000,
	UsePrivateProtectedAccessibility = 0x200000,
	SupportExtensionDeclarations = 0x400000,
	// The C# `StandardConversionFlags` composite (the bitwise OR of the listed constituents,
	// the same `|`-in-initializer form `Modifiers::VisibilityMask` uses).
	StandardConversionFlags = ShowParameterNames |
		ShowAccessibility |
		UsePrivateProtectedAccessibility |
		ShowParameterList |
		ShowParameterModifiers |
		ShowParameterDefaultValues |
		UseNullableSpecifierForValueTypes |
		ShowReturnType |
		ShowModifiers |
		ShowTypeParameterList |
		ShowTypeParameterVarianceModifier |
		ShowDefinitionKeyword |
		ShowBody,
	// The C# `All = 0x1fffff` literal verbatim (note: it does NOT include the high
	// `UsePrivateProtectedAccessibility` (0x200000) / `SupportExtensionDeclarations`
	// (0x400000) bits -- a quirk of the C# source, ported faithfully).
	All = 0x1fffff,
};

// The `[Flags]` bitwise operators (the C# `[Flags]` enum has them implicitly; the C++
// `enum class` does NOT, so they are defined here). Part of the value type's core
// semantics -- constructing a combined mask (`ShowParameterList | ShowReturnType`) and
// testing a bit (`(flags & ShowReturnType) == ShowReturnType`, the pattern
// `CSharpAmbience` uses throughout), NOT an output-stage helper, so they land now.
inline ConversionFlags operator|(ConversionFlags a, ConversionFlags b) {
	return static_cast<ConversionFlags>(
		static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline ConversionFlags operator&(ConversionFlags a, ConversionFlags b) {
	return static_cast<ConversionFlags>(
		static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline ConversionFlags operator^(ConversionFlags a, ConversionFlags b) {
	return static_cast<ConversionFlags>(
		static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
}
inline ConversionFlags operator~(ConversionFlags a) {
	return static_cast<ConversionFlags>(~static_cast<std::uint32_t>(a));
}

// The C# `public interface IAmbience`. Ambiences convert type-system symbols and types to
// display text. A concrete ambience subclasses `IAmbience` and overrides the
// `ConversionFlags` property (which flags to honor) plus the four `Convert*`/`WrapComment`
// methods.
class IAmbience {
public:
	virtual ~IAmbience() = default;

	// The C# `ConversionFlags ConversionFlags { get; set; }` -- which flags the ambience
	// honors. The getter return type resolves to the enum (parsed before this declarator);
	// the setter parameter is fully qualified because an unqualified `ConversionFlags` in
	// the setter would resolve to the (non-type) getter member (the C2061 collision).
	virtual ConversionFlags ConversionFlags() const = 0;
	virtual void ConversionFlags(::ILSpy::Decompiler::Output::ConversionFlags value) = 0;

	// The C# `string ConvertSymbol(ISymbol symbol)` -- render a symbol to text.
	virtual std::string ConvertSymbol(const ISymbol& symbol) = 0;
	// The C# `string ConvertType(IType type)` -- render a type to text.
	virtual std::string ConvertType(const IType& type) = 0;
	// The C# `string ConvertConstantValue(object constantValue)` -- render a boxed
	// literal value to text (the `PrimitiveValue` variant is the C++ faithful equivalent
	// of the C# `object` boxed literal).
	virtual std::string ConvertConstantValue(const PrimitiveValue& constantValue) = 0;
	// The C# `string WrapComment(string comment)` -- wrap a comment (the C# prepends
	// `"// "`).
	virtual std::string WrapComment(std::string_view comment) = 0;
};

} // namespace ILSpy::Decompiler::Output
