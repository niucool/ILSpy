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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of the `System.Reflection.MethodSemanticsAttributes` BCL `[Flags]` enum (the ECMA-335
// II.22.15 MethodSemantics-table flags), the type `IMethod.AccessorKind` returns
// (ICSharpCode.Decompiler/TypeSystem/IMethod.cs line 100). The flags name the role a method
// plays as a property/event accessor: `Setter`/`Getter` (a property get/set), `Adder`/
// `Remover`/`Raiser` (an event add/remove/raise), or `Other` (any other accessor the metadata
// records but the decompiler does not model). `None` (0) marks a plain method that is no
// accessor. The `MethodSemanticsLookup` (ICSharpCode.Decompiler/Metadata/MethodSemanticsLookup.cs)
// builds the accessor->association map from the metadata, and `MetadataMethod.AccessorKind`
// (MetadataMethod.cs line 75) stores the resolved flag.
//
// Consumers: `TypeSystemAstBuilder.ConvertAccessor` (TypeSystemAstBuilder.cs lines 2207-2232)
// switches on the single-valued flag to map it to the AST `AccessorKind`
// (`Getter`/`Setter`/`Adder`/`Remover` -> the matching `AccessorKind`; `Other`/`Raiser` fall
// through), and the IL transforms (`AssignVariableNames`, `ILInlining`, `IndexRangeTransform`,
// `NullPropagationTransform`, `PatternMatchingTransform`, `SwitchOnStringTransform`,
// `StatementBuilder`, `DeconstructInstruction`, `MatchInstruction`) compare
// `method.AccessorKind == MethodSemanticsAttributes.Getter`/`Setter`/... to detect accessors. The
// `MethodSemanticsLookup` ctor ALSO uses the enum as a real `[Flags]` mask: the
// `csharpAccessors = Getter | Setter | Adder | Remover` composite filters which semantics to
// record, and `(filter & Other) != 0` / `(semantics & filter) == 0` are bit tests. So unlike
// `Nullability` (D380, a pure equality-tested enum with no operators) this enum IS combined and
// masked, and the `[Flags]` bitwise operators must be ported (the `TypeSystemOptions` D376 /
// `ConversionFlags` D372 precedent), not just the named values.
//
// The enum is a self-contained value type (no external dependencies), the same shape as the D376
// `TypeSystemOptions`: a `std::uint32_t`-backed `enum class` (the C# `[Flags] : int` underlying
// semantics; the largest member `Raiser = 0x20` fits trivially). The `|`/`&`/`^`/`~` bitwise
// operators the C# `[Flags]` compiler generates implicitly are defined here as free functions
// (the C++ `enum class` does NOT have them), the `TypeSystemOptions` / `ConversionFlags` /
// `Modifiers` precedents. The member order and values mirror the ECMA-335 flags / the BCL
// declaration exactly (each member's numeric value is its C# literal: `None=0`, `Setter=1`,
// `Getter=2`, `Other=4`, `Adder=8`, `Remover=16`, `Raiser=32`); the metadata reader maps the raw
// ECMA flags directly, so reordering would silently remap every accessor.
//
// NAMESPACE: the BCL enum lives in `System.Reflection`, but the C++ port has NO `System::
// Reflection` namespace (no BCL-namespace fidelity for helper types), and `IMethod.AccessorKind`
// which returns it lives in `ILSpy::Decompiler::TypeSystem`. The faithful port therefore places
// the enum in `ILSpy::Decompiler::TypeSystem`, the same convention the D381 `IEntity.MetadataToken`
// port followed (the `System.Reflection.Metadata.EntityHandle` BCL value struct was folded into a
// raw `std::uint32_t` in the `TypeSystem` namespace rather than introducing a `System::Reflection::
// Metadata` namespace) -- BCL helper types are absorbed into the `TypeSystem` namespace where
// their consumers live, not re-homed under a BCL-namespace mirror.

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `[Flags] public enum MethodSemanticsAttributes` (`System.Reflection`). The `enum class`
// (scoped, no implicit conversion to/from `int`, matching the C# enum's typed usage). The
// `std::uint32_t` underlying type mirrors the C# `: int` `[Flags]` semantics. The member order
// and values mirror the ECMA-335 II.22.15 flags / the BCL declaration exactly (each member's
// numeric value is its C# literal); the values are the raw ECMA flags the metadata reader stores
// verbatim in `MetadataMethod.AccessorKind`, so they are load-bearing for the accessor->association
// mapping and must not be reordered.
enum class MethodSemanticsAttributes : std::uint32_t {
	None = 0,
	Setter = 1,
	Getter = 2,
	Other = 4,
	Adder = 8,
	Remover = 16,
	Raiser = 32,
};

// The `[Flags]` bitwise operators (the C# `[Flags]` enum has them implicitly; the C++ `enum class`
// does NOT, so they are defined here). Part of the value type's core semantics -- constructing a
// combined mask (`Getter | Setter | Adder | Remover`, the `MethodSemanticsLookup.csharpAccessors`
// composite) and testing a bit (`(filter & Other) != 0` / `(semantics & filter) == 0`, the
// `MethodSemanticsLookup` ctor patterns), NOT an output-stage helper, so they land now.
inline MethodSemanticsAttributes operator|(MethodSemanticsAttributes a,
										   MethodSemanticsAttributes b) {
	return static_cast<MethodSemanticsAttributes>(
		static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline MethodSemanticsAttributes operator&(MethodSemanticsAttributes a,
										   MethodSemanticsAttributes b) {
	return static_cast<MethodSemanticsAttributes>(
		static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline MethodSemanticsAttributes operator^(MethodSemanticsAttributes a,
										   MethodSemanticsAttributes b) {
	return static_cast<MethodSemanticsAttributes>(
		static_cast<std::uint32_t>(a) ^ static_cast<std::uint32_t>(b));
}
inline MethodSemanticsAttributes operator~(MethodSemanticsAttributes a) {
	return static_cast<MethodSemanticsAttributes>(~static_cast<std::uint32_t>(a));
}

} // namespace ILSpy::Decompiler::TypeSystem
