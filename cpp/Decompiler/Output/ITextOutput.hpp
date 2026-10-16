// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Output/ITextOutput.cs -- the `ITextOutput` interface
// plus the `TextOutputExtensions` static class. This is the write-side abstraction the
// whole Phase-6 IL-text stack funnels through: `ReflectionDisassembler` /
// `MethodBodyDisassembler` / the IL view's `ILAmbience` all write through an
// `ITextOutput` sink, and the port's own `ILTextEmitter` seed feeds the same pipeline.
// The concrete C# implementations are `PlainTextOutput` (plain text; lands together
// with this interface) and the rich-text outputs (ILSpy UI, deferred with the GUI).
//
// C#-to-C++ porting decisions:
//  * The C# file lives in `Output/` but declares `namespace ICSharpCode.Decompiler`
//    (the ROOT C# namespace). The port keeps its file home (`cpp/Decompiler/Output/`)
//    and uses the port's directory-matching `ILSpy::Decompiler::Output` namespace (the
//    port's directory = namespace convention; the C# root namespace has no port
//    precedent -- every ported class lives in a sub-namespace).
//  * `public interface ITextOutput` -> a C++ abstract base with a virtual destructor
//    and one pure-virtual per C# member (the `IType` / `IAmbience` convention).
//  * `string IndentationString { get; set; }` -> the same-name overload pair
//    (`IndentationString() const` getter + `IndentationString(std::string)` setter),
//    the `IAmbience::ConversionFlags` property-pair convention.
//  * `WriteReference(MetadataFile metadata, Handle handle, string text, string protocol,
//    bool isDefinition)` -- the System.Reflection.Metadata `Handle` (a row handle)
//    ports as the raw `std::uint32_t` handle value (the port models metadata handles
//    as plain integers, the `IEntity::MetadataToken` convention); `protocol` and
//    `isDefinition` are hyperlink hints consumed by the rich-text outputs (plain text
//    ignores them) and port as read-only `std::string_view` / `bool`.
//  * `WriteLocalReference(string text, object reference, bool isDefinition, bool
//    isHoverOnly)` -- the C# `object reference` is an opaque identity token for the
//    rich-text outputs' local-variable hyperlinks; the port models it as a nullable
//    `const void*` (plain text ignores it).
//  * `MarkFoldStart(string collapsedText = "...", bool defaultCollapsed = false, bool
//    isDefinition = false)` / `MarkDefinitionStart()` / `MarkFoldEnd()` -- the folding
//    markers of the rich-text outputs; default arguments live on the interface
//    declarations (a C++ override may not restate them, so callers reaching for the
//    defaults go through the `ITextOutput&` view).
//  * `TextOutputExtensions` -> the single-argument `WriteLine(ITextOutput, string)`
//    extension as an inline free function. The C# `Write`/`WriteLine` format-args
//    overloads are `string.Format` shims the port has no analog for; they are
//    documentedly deferred (the future ReflectionDisassembler arms that use them --
//    e.g. the `flags({0:x4})` fallbacks -- format inline instead).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Forward declarations (a reference parameter needs only a forward declaration).
namespace ILSpy::Decompiler::Disassembler { class OpCodeInfo; }
namespace ILSpy::Decompiler::Metadata { class MetadataFile; }
namespace ILSpy::Decompiler::TypeSystem {
class IType;
class IMember;
}

namespace ILSpy::Decompiler::Output {

using Disassembler::OpCodeInfo;
using Metadata::MetadataFile;
using TypeSystem::IMember;
using TypeSystem::IType;

// The C# `public interface ITextOutput` -- the text sink the disassembler-style output
// stages write through. A plain-text implementation renders the raw characters; the
// rich-text implementations additionally turn the reference overloads into hyperlinks.
class ITextOutput {
public:
	virtual ~ITextOutput() = default;

	// The C# `string IndentationString { get; set; }` -- the text prepended once per
	// indent level when a line begins.
	virtual std::string IndentationString() const = 0;
	virtual void IndentationString(std::string value) = 0;

	// The C# `void Indent()` / `void Unindent()`.
	virtual void Indent() = 0;
	virtual void Unindent() = 0;

	// The C# `void Write(char ch)` / `void Write(string text)` / `void WriteLine()`.
	virtual void Write(char ch) = 0;
	virtual void Write(std::string_view text) = 0;
	virtual void WriteLine() = 0;

	// The C# `void WriteReference(Disassembler.OpCodeInfo opCode, bool omitSuffix =
	// false)` -- emit an opcode (as a hyperlink target in the rich-text outputs). The
	// `omitSuffix` flag drops the operand-size suffix so `stloc.0`/`stloc.1` render as
	// the shared `stloc.` prefix.
	virtual void WriteReference(const OpCodeInfo& opCode, bool omitSuffix = false) = 0;

	// The C# `void WriteReference(MetadataFile metadata, Handle handle, string text,
	// string protocol = "decompile", bool isDefinition = false)` -- emit a metadata
	// entity reference (the raw `std::uint32_t` handle stands in for the
	// System.Reflection.Metadata `Handle`).
	virtual void WriteReference(const MetadataFile& metadata, std::uint32_t handle,
		std::string_view text, std::string_view protocol = "decompile",
		bool isDefinition = false) = 0;

	// The C# `void WriteReference(IType type, string text, bool isDefinition = false)`.
	virtual void WriteReference(const IType& type, std::string_view text,
		bool isDefinition = false) = 0;

	// The C# `void WriteReference(IMember member, string text, bool isDefinition =
	// false)`.
	virtual void WriteReference(const IMember& member, std::string_view text,
		bool isDefinition = false) = 0;

	// The C# `void WriteLocalReference(string text, object reference, bool
	// isDefinition = false, bool isHoverOnly = false)` -- emit a local-variable
	// reference (the `const void*` is the opaque identity token the rich-text outputs
	// hyperlink by).
	virtual void WriteLocalReference(std::string_view text, const void* reference = nullptr,
		bool isDefinition = false, bool isHoverOnly = false) = 0;

	// The C# `void MarkFoldStart(string collapsedText = "...", bool defaultCollapsed =
	// false, bool isDefinition = false)` -- begin a collapsible region (a no-op for
	// plain text).
	virtual void MarkFoldStart(std::string_view collapsedText = "...",
		bool defaultCollapsed = false, bool isDefinition = false) = 0;

	// The C# `void MarkDefinitionStart()` -- marks where an entity declaration begins
	// (so leading attributes/docs fold with it).
	virtual void MarkDefinitionStart() = 0;

	// The C# `void MarkFoldEnd()` -- end the collapsible region.
	virtual void MarkFoldEnd() = 0;
};

// The C# `TextOutputExtensions.WriteLine(this ITextOutput output, string text)` --
// write the text, then the line break. (The C# format-args Write/WriteLine extensions
// are documentedly deferred: the port has no `string.Format`, and the disassembler arms
// that would use them format inline.)
inline void WriteLine(ITextOutput& output, std::string_view text) {
	output.Write(text);
	output.WriteLine();
}

} // namespace ILSpy::Decompiler::Output
