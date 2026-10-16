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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/ILAmbience.cs -- the IL-view ambience, the
// second `IAmbience` implementation (after CSharpAmbience, the Phase-5 C#-syntax
// rendering). It renders a type-system symbol to its IL declaration syntax
// (`.field public static initonly ...`, `.class interface public abstract ...`)
// and a type to its IL signature syntax (`int32[]`, `method unmanaged cdecl
// int32 *(int32)`) -- the spellings ILSpy's IL view shows in tooltips and
// member lists.
//
// The symbol rendering is metadata-driven: each flag prefix is read from the
// owning module's metadata (the `MetadataFile` per-row attribute-flags reads the
// D-iteration GetTypeDefAttributes/GetFieldAttributes/GetMethodAttributes/
// GetPropertyAttributes/GetEventAttributes surface), then rendered through the
// ReflectionDisassembler `WriteEnum`/`WriteFlags` attribute-name machinery over
// the same tables the `.method`/`.field`/`.class` IL headers use. When the
// symbol's owning module has no metadata (a stale entity pinned in navigation
// history after its assembly was unloaded/reloaded), the class falls back to the
// symbol's bare name so callers still get sensible display text.
//
// C#-to-C++ porting decisions:
//  * `public class ILAmbience : IAmbience` -> derives the ported
//    `::ILSpy::Decompiler::Output::IAmbience` abstract base (the CSharpAmbience
//    convention). The `ConversionFlags ConversionFlags { get; set; }` property
//    overrides the interface's pure-virtual getter/setter pair; the backing
//    field defaults to `ConversionFlags.None` (the C# default enum value). The
//    class-scope `CF` alias carries the enum past the member-function-hides-the-
//    enum-name collision (the D472 `NameLookupMode` precedent).
//  * The C# `StringWriter` ports to `std::ostringstream` (the
//    PlainTextOutput external ctor takes the stream directly; `sw.ToString()`
//    -> `sw.str()`).
//  * `var metadata = entity?.ParentModule?.MetadataFile?.Metadata` -- the C#
//    `MetadataReader` reached through the module's `MetadataFile` -- ports to
//    the port's `const MetadataFile*` itself: the port's `MetadataFile` IS the
//    metadata reader (the per-row attribute reads live on it). `entity ==
//    null` / a null `ParentModule` / a null `MetadataFile` all reduce to the
//    `metadata == nullptr` stale-entity fallback.
//  * The C# `metadata.GetFieldDefinition((FieldDefinitionHandle)token)` +
//    `.Attributes` row reads port to the token-based attribute getters
//    (a wrong-table/out-of-range token yields 0 -- the C# cast shape never
//    occurs for a well-formed symbol). The `System.Reflection` attribute enums
//    the C# reads are the Disassembler `ReflectionAttributes.hpp` stand-ins;
//    the raw `std::uint32_t` flags columns widen to them with `static_cast`
//    (the BCL enum reading a row widens to int, the same direction).
//  * `ReflectionDisassembler.WriteEnum`/`WriteFlags` are the already-ported
//    Disassembler free templates over the attribute-name tables
//    (EnumNameCollection.hpp); the per-symbol flag masks
//    (`FieldAccessMask`, the field `hasXAttributes` hide-list,
//    `MemberAccessMask`, the type `masks` composition) are ported verbatim.
//  * The C# `switch (symbol)` pattern cases port to a dynamic_cast if/else
//    chain in the C# case order (`IField` / `IMethod` / `IProperty` / `IEvent` /
//    `ITypeDefinition`). The four return-type switch cases
//    (`f.ReturnType` / `m.ReturnType` / ... ) all read the same
//    `IMember::ReturnType()` virtual every one of the four interfaces
//    re-exposes, so the port reads it once through the `IMember` cast (the
//    four-arm switch and the member-level read dispatch identically).
//  * The C# local functions `WriteTypeDefinition` / `WriteTypeParameters`
//    (nested inside `ConvertSymbol`, capturing the writer and the flags) port
//    to member methods taking the writer (C++ has no capturing local
//    functions), widened to public for direct TDD ahead of the consumers (the
//    CSharpResolver `TryConvert` widening convention).
//  * The `ILAmbience.TypeToStringVisitor` nested class (the `TypeVisitor`
//    subclass `ConvertType` drives) ports to a namespace-scope class in the
//    .cpp (the BaseListNameabilityVisitor precedent -- a private nested
//    visitor with no direct-construction need lands .cpp-internal; the public
//    `ConvertType` entry is the testable surface). Its `builder` is the C#
//    `StringBuilder` -> `std::string`; the `return type` reference semantics
//    port through `shared_from_this` (the visitor never changes a type -- every
//    override returns its input, so the port's reconstructed-vs-original
//    distinction is unreachable).
//  * `string ConvertConstantValue(object constantValue)` throws
//    `NotImplementedException` unconditionally in the C# -- ports to
//    `throw std::logic_error` (the NotSupportedException convention, the
//    `OperatorMethod::Specialize` precedent).
//  * `EscapeName(StringBuilder, string)` / `EscapeName(string)` -- the escape
//    of characters that cannot be displayed in the UI (`\\u{0:x4}` per
//    whitespace/control/surrogate character). The C# iterates UTF-16 code
//    units; the port decodes its UTF-8 `std::string` text convention to code
//    points and escapes a non-BMP code point as the TWO `\\udXXX`/`\\uXXXX`
//    surrogate halves (the same output the C# per-half escaping produces).
//    `static`, per the C#.

#pragma once

#include "Decompiler/Output/IAmbience.hpp"

#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
class IEntity;
class ITypeDefinition;
class ITypeParameter;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::IL {

using TypeSystem::IEntity;
using TypeSystem::ITypeDefinition;
using TypeSystem::ITypeParameter;

// The C# `public class ILAmbience : IAmbience` (ILAmbience.cs line 36) -- the
// IL-view ambience. `ConvertSymbol` composes the metadata-driven flag prefixes
// (the Disassembler attribute tables), the member/type name (with `IL` arity
// backticks and the optional `<T, U>` list), the parameter list, and the return
// type before/after the name, each piece gated on the `ConversionFlags` mask;
// `ConvertType` renders a type's IL signature spelling through the
// `TypeToStringVisitor`.
class ILAmbience : public ::ILSpy::Decompiler::Output::IAmbience {
private:
	// The class-scope alias for the flags enum: the inherited member function
	// `ConversionFlags()` hides the `Output::ConversionFlags` enum name for the
	// whole class body (the `NameLookupMode` precedent).
	using CF = ::ILSpy::Decompiler::Output::ConversionFlags;

public:
	// The C# `ConversionFlags ConversionFlags { get; set; }` -- which flags the
	// ambience honors.
	CF ConversionFlags() const override { return conversionFlags_; }
	void ConversionFlags(::ILSpy::Decompiler::Output::ConversionFlags value) override {
		conversionFlags_ = value;
	}

	// The C# `public string ConvertConstantValue(object constantValue)` (line 40)
	// -- throws `NotImplementedException` unconditionally (no IL-view constant
	// rendering exists in the C# source).
	std::string ConvertConstantValue(const CSharp::Syntax::PrimitiveValue& constantValue) override;

	// The C# `public string ConvertSymbol(ISymbol symbol)` (line 45) -- render a
	// symbol to its IL declaration text.
	std::string ConvertSymbol(const TypeSystem::ISymbol& symbol) override;

	// The C# `void ConvertSymbol(StringWriter writer, ISymbol symbol)` (line 54)
	// -- the writer-driven core (private in the C#, widened to public for direct
	// TDD ahead of the consumers). Writes the flag-driven prefix, the name, the
	// parameter list, and the before/after return type into `writer`.
	void ConvertSymbol(std::ostringstream& writer, const TypeSystem::ISymbol& symbol);

	// The C# `public string ConvertType(IType type)` (line 304) -- render a type
	// to its IL signature text. NOTE the port's `IType::AcceptVisitor` is
	// non-const (D406); the interface's const reference is `const_cast` away at
	// the visit call site (the D515 convention -- the underlying type-system
	// objects are mutable, the accessor's const is the contract).
	std::string ConvertType(const TypeSystem::IType& type) override;

	// The C# `public string WrapComment(string comment)` (line 504) -- the
	// `"// "` prefix.
	std::string WrapComment(std::string_view comment) override;

	// The C# local function `void WriteTypeDefinition(ITypeDefinition typeDef)`
	// (line 179, nested in ConvertSymbol) -- the recursive declaring-type /
	// namespace-qualified type-name renderer ending in the arity backticks.
	void WriteTypeDefinition(std::ostringstream& writer, const TypeSystem::ITypeDefinition& typeDef);

	// The C# local function `void WriteTypeParameters(IReadOnlyList<ITypeParameter>
	// typeParameters, IEntity owner)` (line 198) -- the IL arity marker (`1 for a
	// type's own parameters, ``1 for a method's, minus the declaring type's
	// count) plus the optional `<T, U>` list with variance signs.
	void WriteTypeParameters(std::ostringstream& writer,
		const std::vector<const ITypeParameter*>& typeParameters, const IEntity& owner);

	// The C# `public static StringBuilder EscapeName(StringBuilder sb, string name)`
	// (line 512) -- append the name to `sb`, escaping every whitespace/control/
	// surrogate character as `\u{0:x4}` (lowercase hex, 4 digits); returns `sb`
	// (the C# returns the builder for chaining).
	static std::string& EscapeName(std::string& sb, std::string_view name);

	// The C# `public static string EscapeName(string name)` (line 527) -- the
	// convenience form over a fresh string.
	static std::string EscapeName(std::string_view name);

private:
	// The C# `ConversionFlags.HasFlag(...)` calls throughout the class -- the
	// `[Flags]` bit test over the backing field (the `ConversionFlags` operators
	// the `IAmbience.hpp` enum carries).
	bool HasFlag(CF flag) const { return (conversionFlags_ & flag) == flag; }

	// The C# `ConversionFlags` backing field (the property's default is the
	// `None` enum value).
	CF conversionFlags_ = CF::None;
};

} // namespace ILSpy::Decompiler::IL
