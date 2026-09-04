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

// Port of ICSharpCode.Decompiler/Disassembler/DisassemblerHelpers.cs (the static
// helper class MethodBodyDisassembler and ReflectionDisassembler consume): the
// IL-name-syntax enum, the IL_xxxx offset labels, the identifier escape rules
// (the ILAsm keyword set from Metadata.ILOpCodeExtensions.ILKeywords), the
// parameter/variable local references, the operand writers (the checked ILDasm
// literal spellings for floats/doubles), the string escaper, and the primitive
// type-name table.
//
// C#-to-C++ porting decisions:
//  * The C# `WriteParameterReference(ITextOutput, MetadataReader,
//    MethodDefinitionHandle, int)` reads the method definition's Static flag and
//    walks the Param rows by SequenceNumber; the port's MetadataFile hides the
//    winmd reader behind token accessors, so the signature adapts to
//    (ITextOutput, const MetadataFile&, methodToken, int index) -- the
//    MetadataAttributes per-row-accessor convention. The Static flag reads
//    through GetMethodAttributes (the II.23.1.10 column) and the parameter name
//    through GetParameterNames (which applies the same SequenceNumber-to-index
//    mapping the C# walk performs); the IL argument index maps to the
//    declared-parameter index by the C#'s rule (static: index, instance:
//    index - 1, the implicit `this` occupying IL index 0).
//  * The C# `WriteLocalReference(text, referenceObject)` opaque reference tokens
//    (a boxed int for the offsets, a fresh "param_N"/"loc_N" string per call)
//    port to the ITextOutput `const void*` token as the integer value
//    reinterpreted (iteration 134's `object reference token as a const void*`
//    convention); PlainTextOutput ignores the token, and the rich-text outputs
//    are not ported.
//  * The C# `WriteOperand(ITextOutput, object operand)` dispatch ports over
//    `const std::any&` (the port's boxed-value convention): the C# null ->
//    std::invalid_argument (the ArgumentNullException analog), the string/char/
//    float/double/bool arms dispatch on the held type, the integral arms render
//    the invariant decimal digits (the C# IConvertible.ToString(Invariant)
//    path), and an unknown held type renders its type name -- the analog of the
//    C# `value.ToString()` default (which returns the runtime type's name; the
//    realistic operand set never reaches it).
//  * `float`/`double.ToString("R", Invariant)` -> the .NET FormatGeneral rule
//    over std::to_chars' scientific form (the Ryu shortest round-trip digits --
//    the same digits .NET's Grisu3/Dragon4 path produces): fixed notation
//    while the decimal scale stays within [-3, MaxRoundTripDigits] (17 for
//    double, 9 for float), scientific otherwise, the exponent spelled 'E', a
//    sign, and a minimum of two digits; the 0.0/-0.0 and infinity/NaN special
//    cases (the "(XX XX XX XX)" byte dumps) are ported verbatim over the
//    little-endian byte order.
//  * `EscapeString`/`IsValidIdentifier` iterate the C# UTF-16 `char` loop; the
//    port decodes the UTF-8 text to code points (the ILAmbience EscapeName
//    convention): the named escapes fire on the exact code points, a non-BMP
//    code point emits the two `\udXXX` surrogate halves (each `IsSurrogate`
//    half escapes separately in the C#), control characters and non-space
//    whitespace escape as `\uXXXX`, and everything else passes through raw.
//    `char.IsLetterOrDigit`/`char.IsDigit` classify the ASCII range faithfully;
//    non-ASCII is treated as letters (a documented divergence -- the C#
//    consults the Unicode category database, which the port does not carry;
//    for the ASCII identifiers real metadata uses the two agree).
//  * The ExceptionRegion `WriteTo` extension is DEFERRED: it needs the
//    ExceptionRegion model (unported), MetadataGenericContext (now ported --
//    Metadata/MetadataGenericContext.hpp), and the ReflectionDisassembler
//    catch-type writer (unported); it lands with the ReflectionDisassembler
//    method-body disassembly region.

#pragma once

#include <any>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

#include "Decompiler/Metadata/MethodBody.hpp"          // ExceptionHandlerClause
#include "Decompiler/Metadata/MetadataGenericContext.hpp"

namespace ILSpy::Decompiler::Output {
class ITextOutput;
}

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
}

namespace ILSpy::Decompiler::Disassembler {

// The C# `public enum ILNameSyntax` -- how a type name is rendered in IL text.
enum class ILNameSyntax : std::uint8_t {
	// class/valuetype + TypeName (built-in types use keyword syntax)
	Signature,
	// Like signature, but always refers to type parameters using their position
	SignatureNoNamedTypeParameters,
	// [assembly]Full.Type.Name (even for built-in types)
	TypeName,
	// Name (but built-in types use keyword syntax)
	ShortTypeName,
};

// The C# `public static string OffsetToString(int offset)` -- the "IL_xxxx"
// offset label (lowercase hex, zero-padded to at least four digits).
inline std::string OffsetToString(int offset) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "IL_%04x", static_cast<unsigned int>(offset));
	return std::string(buf);
}

// The C# `public static string OffsetToString(long offset)` -- the 64-bit
// overload (the C# `long` is a signed 64-bit integer).
inline std::string OffsetToString(std::int64_t offset) {
	char buf[32];
	std::snprintf(buf, sizeof(buf), "IL_%04llx",
		static_cast<unsigned long long>(static_cast<std::uint64_t>(offset)));
	return std::string(buf);
}

// The C# `public static void WriteOffsetReference(ITextOutput writer, int? offset)`
// -- "null" for a missing offset, or the IL_xxxx label as a local reference.
void WriteOffsetReference(Output::ITextOutput& writer, std::optional<int> offset);

// The C# `public static string Escape(string identifier)` -- an identifier that
// is already valid ILAsm passes through unchanged; anything else is wrapped in
// single quotes with ' escaped as \' (ILDasm's convention, not the ECMA octal
// escape -- see the C# comment).
std::string Escape(std::string_view identifier);

// The C# `public static void WriteTo(this ExceptionRegion exceptionHandler,
// MetadataFile module, MetadataGenericContext context, ITextOutput writer)`
// (DisassemblerHelpers.cs line 72) -- the exception-region-to-text writer the
// MethodBodyDisassembler's WriteExceptionHandlers consumes:
// ".try <try> <kind> [<filter> handler ][<catch-type> ]<handler>" where the
// catch type renders through the IL InstructionOutputExtensions EntityHandle.
// WriteTo and the filter/handler spacing is the C# verbatim shape (the
// filter arm's " handler " plus the unconditional pre-handler space doubles
// for filter clauses, and finally/fault carry the doubled space too). The C#
// FilterOffset == -1 sentinel ports to the clause's Kind == Filter
// discriminant, and the C# CatchType.IsNil guard ports to a zero catch token.
void WriteTo(const Metadata::ExceptionHandlerClause& exceptionHandler,
    const Metadata::MetadataFile& module, const Metadata::MetadataGenericContext& context,
    Output::ITextOutput& writer);

// The C# private `static bool IsValidIdentifier(string identifier)` -- widened to
// public for direct TDD per the file's convention. The ILAsm identifier rules:
// non-empty, not starting with a digit, ".ctor"/".cctor" the only dot-starting
// forms, no "..", not an IL keyword (ILOpCodeExtensions.ILKeywords), and every
// character a letter/digit or one of _ $ @ ? ` .
bool IsValidIdentifier(std::string_view identifier);

// The C# `public static void WriteParameterReference(ITextOutput writer,
// MetadataReader metadata, MethodDefinitionHandle handle, int index)` -- the
// parameter-name local reference (see the header note for the token-based
// signature adaptation). A parameter the Param table does not name falls back to
// the bare index.
void WriteParameterReference(Output::ITextOutput& writer,
	const Metadata::MetadataFile& module, std::uint32_t methodToken, int index);

// The C# `public static void WriteVariableReference(ITextOutput writer,
// MetadataReader metadata, MethodDefinitionHandle handle, int index)` -- the
// local-variable index reference ("loc_N" is the reference token; the visible
// text is the bare index).
void WriteVariableReference(Output::ITextOutput& writer, int index);

// The C# `public static void WriteOperand(ITextOutput writer, object operand)`
// -- the boxed-value dispatch (see the header note for the std::any mapping).
void WriteOperand(Output::ITextOutput& writer, const std::any& operand);

// The C# `public static void WriteOperand(ITextOutput writer, long val)`.
void WriteOperand(Output::ITextOutput& writer, std::int64_t val);

// The C# `public static void WriteOperand(ITextOutput writer, float val)` -- the
// ILDasm literal spelling: 0.0 / -0.0, the "(XX XX XX XX)" byte dump for
// infinity/NaN, else the round-trip format.
void WriteOperand(Output::ITextOutput& writer, float val);

// The C# `public static void WriteOperand(ITextOutput writer, double val)`.
void WriteOperand(Output::ITextOutput& writer, double val);

// The `double.ToString("R", InvariantCulture)` / `float.ToString("R", ...)`
// rendering -- the .NET FormatGeneral rule over the shortest round-trip
// digits (the same rule WriteFloatingOperand renders through; see the
// WriteOperand note): FIXED notation while the decimal scale stays within
// [-3, MaxRoundTripDigits] (17 double / 9 float), scientific otherwise.
// Unlike the IL operand spellings, the plain "R" rendering is reachable
// for the special values (the XAML path/point renders hit them), and .NET
// spells them as symbols: zero renders "0" and negative zero "-0" (the
// sign is kept), NaN renders "NaN" (its sign bit is dropped), and the
// infinities render "Infinity" / "-Infinity".
std::string FormatRoundTrip(double value);
std::string FormatRoundTrip(float value);

// The C# `public static void WriteOperand(ITextOutput writer, string operand)`
// -- the double-quoted escaped-string literal.
void WriteOperand(Output::ITextOutput& writer, std::string_view operand);

// The C# `public static string EscapeString(string str)` -- the ILDasm string
// escape: the named escapes (\" \\ \0 \a \b \f \n \r \t \v), then control
// characters, surrogates, and non-space whitespace as \uXXXX.
std::string EscapeString(std::string_view str);

// The C# `public static string PrimitiveTypeName(string fullName)` -- the IL
// keyword for a BCL primitive's full name ("System.Int32" -> "int32",
// "System.IntPtr" -> "native int"), or nullptr where the C# returns null.
const char* PrimitiveTypeName(std::string_view fullName);

// The System.Reflection.PortableExecutable `Subsystem.ToString()` semantics
// the WriteModuleHeader `.subsystem` comment renders: the member name for a
// named value, the decimal value for an unnamed one (the enum is
// ushort-backed, so the decimal is never negative). Not a C#
// DisassemblerHelpers member -- the C# renders the BCL enum's ToString inline;
// the port factors the spelling so it is directly testable.
std::string SubsystemToString(std::uint16_t subsystem);

// The System.Reflection.PortableExecutable `CorFlags.ToString()` semantics
// the WriteModuleHeader `.corflags` comment renders ([Flags]): the member
// names of an exact union of named flags joined with ", " in ascending value
// order ("ILOnly, StrongNameSigned"); an unnamed leftover bit discards the
// names and renders the FULL value in decimal (the .NET flags-format
// fallback); 0 renders "0" (CorFlags carries no None member).
std::string CorFlagsToString(std::uint32_t flags);

} // namespace ILSpy::Decompiler::Disassembler
