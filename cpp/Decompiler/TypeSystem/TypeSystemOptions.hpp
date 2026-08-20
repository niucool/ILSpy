// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation, the rights to use, copy, modify, merge,
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

// Port of the `TypeSystemOptions` `[Flags]` enum (the second half of
// ICSharpCode.Decompiler/TypeSystem/DecompilerTypeSystem.cs, lines 39-141). Options that
// control how metadata is represented in the type system: whether `dynamic`/tuple/
// extension-method/nint/function-pointer/ref-readonly/params/etc. language constructs are
// materialized from their attributes or left as attributes, whether only the public API is
// loaded, whether the type system caches entities, and so on. `DecompilerTypeSystem.GetOptions`
// builds the mask from a `DecompilerSettings`; the `ICompilation.TypeSystemOptions` property
// (the next `TypeSystem` leaf, `ICompilation`, the D375-noted dependency chain toward
// `ICompilationProvider` -> `IEntity` -> `TypeSystemAstBuilder` -> `CSharpAmbience`) exposes the
// resolved mask to every type-system consumer.
//
// The enum is a self-contained `[Flags]` value type (no external dependencies), the same
// shape as the D372 `ConversionFlags`: a `std::uint32_t`-backed `enum class` (the largest
// member `RuntimeAsync = 0x100000` fits as a plain unsigned value; the C# `: int` /
// `[Flags]` underlying semantics). The `|`/`&`/`^`/`~` bitwise operators the C# `[Flags]`
// compiler generates implicitly are defined here as free functions (the C++ `enum class`
// does NOT have them), the `ConversionFlags` / `Modifiers` precedents. `Default` is the
// bitwise OR of its 19 constituents (the C# `Dynamic | Tuple | ... | RuntimeAsync`), the
// typical decompiler settings with all C# language features enabled; it does NOT include
// `OnlyPublicAPI` (0x8), `Uncached` (0x10), or `KeepModifiers` (0x40) -- the three flags the
// C# `Default` deliberately omits, ported faithfully.

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `[Flags] public enum TypeSystemOptions`. The `enum class` (scoped, no implicit
// conversion to/from `int`, matching the C# enum's typed usage). The `std::uint32_t`
// underlying type lets `RuntimeAsync = 0x100000` fit as a plain unsigned value. The member
// order and values mirror the C# exactly (each individual flag's numeric value is the C#
// literal); `Default` is the bitwise OR of its 19 constituents (the C#
// `Dynamic | Tuple | ExtensionMethods | ... | RuntimeAsync` composite), expressed with the
// `|` operator in the enumerator initializer (the same form `ConversionFlags::
// StandardConversionFlags` and `Modifiers::VisibilityMask` use, which compiles in this MSVC
// build).
enum class TypeSystemOptions : std::uint32_t {
	None = 0,
	Dynamic = 1,
	Tuple = 2,
	ExtensionMethods = 4,
	OnlyPublicAPI = 8,
	Uncached = 0x10,
	DecimalConstants = 0x20,
	KeepModifiers = 0x40,
	ReadOnlyStructsAndParameters = 0x80,
	RefStructs = 0x100,
	UnmanagedConstraints = 0x200,
	NullabilityAnnotations = 0x400,
	ReadOnlyMethods = 0x800,
	NativeIntegers = 0x1000,
	FunctionPointers = 0x2000,
	ScopedRef = 0x4000,
	NativeIntegersWithoutAttribute = 0x8000,
	RefReadOnlyParameters = 0x10000,
	ParamsCollections = 0x20000,
	FirstClassSpanTypes = 0x40000,
	ExtensionMembers = 0x80000,
	RuntimeAsync = 0x100000,
	// The C# `Default` composite (the bitwise OR of the 19 listed constituents, the same
	// `|`-in-initializer form `ConversionFlags::StandardConversionFlags` uses). It does NOT
	// include `OnlyPublicAPI` (0x8), `Uncached` (0x10), or `KeepModifiers` (0x40) -- the three
	// flags the C# `Default` deliberately omits; the computed value is 0x1FFFA7 (pinned
	// independently in the test).
	Default = Dynamic |
		Tuple |
		ExtensionMethods |
		DecimalConstants |
		ReadOnlyStructsAndParameters |
		RefStructs |
		UnmanagedConstraints |
		NullabilityAnnotations |
		ReadOnlyMethods |
		NativeIntegers |
		FunctionPointers |
		ScopedRef |
		NativeIntegersWithoutAttribute |
		RefReadOnlyParameters |
		ParamsCollections |
		FirstClassSpanTypes |
		ExtensionMembers |
		RuntimeAsync,
};

// The `[Flags]` bitwise operators (the C# `[Flags]` enum has them implicitly; the C++
// `enum class` does NOT, so they are defined here). Part of the value type's core semantics
// -- constructing a combined mask (`Dynamic | Tuple`) and testing a bit
// (`(options & Dynamic) == Dynamic`, the pattern `DecompilerTypeSystem.GetOptions` and the
// type-system consumers use), NOT an output-stage helper, so they land now.
inline TypeSystemOptions operator|(TypeSystemOptions a, TypeSystemOptions b) {
	return static_cast<TypeSystemOptions>(
		static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline TypeSystemOptions operator&(TypeSystemOptions a, TypeSystemOptions b) {
	return static_cast<TypeSystemOptions>(
		static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline TypeSystemOptions operator^(TypeSystemOptions a, TypeSystemOptions b) {
	return static_cast<TypeSystemOptions>(
		static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
}
inline TypeSystemOptions operator~(TypeSystemOptions a) {
	return static_cast<TypeSystemOptions>(~static_cast<std::uint32_t>(a));
}

} // namespace ILSpy::Decompiler::TypeSystem
