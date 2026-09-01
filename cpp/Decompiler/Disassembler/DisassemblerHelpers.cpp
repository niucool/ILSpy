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

// Port of ICSharpCode.Decompiler/Disassembler/DisassemblerHelpers.cs -- see
// DisassemblerHelpers.hpp for the porting decisions.

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"

#include "Decompiler/Disassembler/ReflectionAttributes.hpp"
#include "Decompiler/IL/InstructionOutputExtensions.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

#include <any>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Disassembler {

namespace {

// ---------------------------------------------------------------------------
// File-local UTF-8 / character-classification helpers (the ILAmbience.cpp
// convention: the port's text convention is UTF-8 where the C# iterates UTF-16
// code units; each production file carries its own copies).
// ---------------------------------------------------------------------------

// Decode one UTF-8 sequence in `str` starting at `index` to a code point;
// returns the code point and the number of bytes consumed. For an
// invalid/overlong/surrogate sequence, returns the offending lead byte as a
// code point and consumes 1 byte (the ILAmbience.cpp DecodeUtf8 shape).
std::pair<char32_t, std::size_t> DecodeUtf8(std::string_view str, std::size_t index) {
	if (index >= str.size())
		return {char32_t(0), 0};
	unsigned char b0 = static_cast<unsigned char>(str[index]);
	if (b0 < 0x80)
		return {char32_t(b0), 1};
	char32_t cp = 0;
	std::size_t n = 0;
	if ((b0 & 0xE0) == 0xC0) { n = 2; cp = char32_t(b0 & 0x1F); }
	else if ((b0 & 0xF0) == 0xE0) { n = 3; cp = char32_t(b0 & 0x0F); }
	else if ((b0 & 0xF8) == 0xF0) { n = 4; cp = char32_t(b0 & 0x07); }
	else
		return {char32_t(b0), 1};
	for (std::size_t i = 1; i < n; ++i) {
		if (index + i >= str.size())
			return {char32_t(b0), 1};
		unsigned char b = static_cast<unsigned char>(str[index + i]);
		if ((b & 0xC0) != 0x80)
			return {char32_t(b0), 1};
		cp = (cp << 6) | char32_t(b & 0x3F);
	}
	if (n == 2 && cp < 0x80) return {char32_t(b0), 1};           // overlong
	if (n == 3 && cp < 0x800) return {char32_t(b0), 1};          // overlong
	if (n == 3 && cp >= 0xD800 && cp <= 0xDFFF) return {char32_t(b0), 1};  // surrogate
	if (n == 4 && (cp < 0x10000 || cp > 0x10FFFF)) return {char32_t(b0), 1};  // out of range
	return {cp, n};
}

// The C# `char.IsControl(char)` -- 0x0000-0x001F and 0x007F-0x009F.
bool IsUtf16Control(char32_t ch) {
	return (ch >= 0x0000 && ch <= 0x001F) || (ch >= 0x007F && ch <= 0x009F);
}

// The C# `char.IsWhiteSpace(char)` -- the exact .NET Framework UTF-16 code-unit
// set: the ASCII set 0x09-0x0D, 0x20, plus the separators 0x85, 0xA0, 0x1680,
// the 0x2000-0x200A block, 0x2028, 0x2029, 0x202F, 0x205F, 0x3000.
bool IsUtf16WhiteSpace(char32_t ch) {
	if (ch >= 0x2000 && ch <= 0x200A)
		return true;
	switch (ch) {
	case 0x0009: case 0x000A: case 0x000B: case 0x000C: case 0x000D: case 0x0020:
	case 0x0085: case 0x00A0: case 0x1680: case 0x2028: case 0x2029:
	case 0x202F: case 0x205F: case 0x3000:
		return true;
	default:
		return false;
	}
}

// The C# `char.IsSurrogate(char)` -- the 0xD800-0xDFFF block.
bool IsUtf16Surrogate(char32_t ch) {
	return ch >= 0xD800 && ch <= 0xDFFF;
}

// The C# `"\\u{0:x4}"` -- a four-digit lowercase-hex Unicode escape.
void AppendUnicodeEscape4(std::string& sb, std::uint32_t cp) {
	char buf[8];
	std::snprintf(buf, sizeof(buf), "\\u%04x", cp);
	sb.append(buf);
}

// The C# `static bool IsValidIdentifierCharacter(char c)` -- the ASCII letter/
// digit range classified faithfully; non-ASCII treated as letters (the
// documented divergence -- see the header note).
bool IsValidIdentifierCharacter(char32_t cp) {
	if (cp < 0x80) {
		if ((cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') ||
			(cp >= '0' && cp <= '9'))
			return true;
		switch (cp) {
		case '_': case '$': case '@': case '?': case '`': case '.':
			return true;
		default:
			return false;
		}
	}
	return true;
}

// The C# `static bool IsDigitStart(char c)` -- char.IsDigit for the first
// character: the ASCII digits faithfully; non-ASCII treated as letters (the
// same divergence direction as IsValidIdentifierCharacter).
bool IsDigitStart(char32_t cp) {
	return cp < 0x80 && cp >= '0' && cp <= '9';
}

// The C# `static string ToInvariantCultureString(object value)` -- the
// IConvertible invariant rendering for the integral operand types (decimal
// digits); the float/double/bool/char/string arms are handled before this in
// the C# dispatch.
template <typename T>
std::string IntegralToString(T value) {
	return std::to_string(value);
}

} // namespace

// ---------------------------------------------------------------------------
// WriteOffsetReference (DisassemblerHelpers.cs lines 66-72).
// ---------------------------------------------------------------------------
void WriteOffsetReference(Output::ITextOutput& writer, std::optional<int> offset)
{
	if (!offset.has_value()) {
		writer.Write("null");
	} else {
		// The C# passes the boxed offset as the opaque reference token; the
		// port reinterprets the integer (the iteration-134 const void*
		// convention -- PlainTextOutput ignores it).
		writer.WriteLocalReference(OffsetToString(*offset),
			reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
				static_cast<unsigned int>(*offset))));
	}
}

// ---------------------------------------------------------------------------
// WriteTo(this ExceptionRegion, ...) (DisassemblerHelpers.cs lines 72-95).
// ---------------------------------------------------------------------------
void WriteTo(const Metadata::ExceptionHandlerClause& exceptionHandler,
	const Metadata::MetadataFile& module, const Metadata::MetadataGenericContext& context,
	Output::ITextOutput& writer)
{
	writer.Write(".try ");
	WriteOffsetReference(writer, exceptionHandler.TryOffset);
	writer.Write('-');
	WriteOffsetReference(writer, exceptionHandler.TryOffset + exceptionHandler.TryLength);
	writer.Write(' ');
	// The C# `exceptionHandler.Kind.ToString().ToLowerInvariant()` -- the
	// four ExceptionRegionKind names lower-cased.
	switch (exceptionHandler.Kind) {
		case Metadata::ExceptionHandlerKind::Catch: writer.Write("catch"); break;
		case Metadata::ExceptionHandlerKind::Filter: writer.Write("filter"); break;
		case Metadata::ExceptionHandlerKind::Finally: writer.Write("finally"); break;
		case Metadata::ExceptionHandlerKind::Fault: writer.Write("fault"); break;
	}
	if (exceptionHandler.Kind == Metadata::ExceptionHandlerKind::Filter) {
		// The C# `if (exceptionHandler.FilterOffset != -1)` arm -- the port's
		// Kind == Filter discriminant stands in for the -1 sentinel.
		writer.Write(' ');
		WriteOffsetReference(writer, exceptionHandler.ClassTokenOrFilterOffset);
		writer.Write(" handler ");
	}
	if (exceptionHandler.Kind == Metadata::ExceptionHandlerKind::Catch
	    && exceptionHandler.ClassTokenOrFilterOffset != 0) {
		// The C# `if (!exceptionHandler.CatchType.IsNil)` arm; the zero
		// catch token is the port's nil handle.
		writer.Write(' ');
		IL::WriteTo(module, writer, context, exceptionHandler.ClassTokenOrFilterOffset);
	}
	writer.Write(' ');
	WriteOffsetReference(writer, exceptionHandler.HandlerOffset);
	writer.Write('-');
	WriteOffsetReference(writer, exceptionHandler.HandlerOffset + exceptionHandler.HandlerLength);
}

// ---------------------------------------------------------------------------
// IsValidIdentifier + Escape (DisassemblerHelpers.cs lines 105-134 + 125).
// ---------------------------------------------------------------------------
bool IsValidIdentifier(std::string_view identifier)
{
	if (identifier.empty())
		return false;

	// The C# checks identifier[0] over the first UTF-16 unit; the port decodes
	// the first code point.
	std::size_t i = 0;
	const auto first = DecodeUtf8(identifier, 0);
	const char32_t firstCp = first.first;
	i = first.second;
	if (IsDigitStart(firstCp))
		return false;

	// As a special case, .ctor and .cctor are valid despite starting with a dot
	if (firstCp == '.')
		return identifier == ".ctor" || identifier == ".cctor";

	if (identifier.find("..") != std::string_view::npos)
		return false;

	if (Metadata::ILKeywords().count(std::string(identifier)) > 0)
		return false;

	while (i < identifier.size()) {
		const auto decoded = DecodeUtf8(identifier, i);
		const char32_t cp = decoded.first;
		const std::size_t n = decoded.second;
		if (!IsValidIdentifierCharacter(cp))
			return false;
		i += n;
	}
	return true;
}

std::string Escape(std::string_view identifier)
{
	if (IsValidIdentifier(identifier)) {
		return std::string(identifier);
	}

	// The ECMA specification says that ' inside SQString should be escaped
	// using an octal escape sequence, but we follow Microsoft's ILDasm and
	// use \'.
	std::string escaped = EscapeString(identifier);
	std::string result = "'";
	for (char c : escaped) {
		if (c == '\'')
			result.append("\\'");
		else
			result.push_back(c);
	}
	result.push_back('\'');
	return result;
}

// ---------------------------------------------------------------------------
// WriteParameterReference / WriteVariableReference (lines 136-188).
// ---------------------------------------------------------------------------
void WriteParameterReference(Output::ITextOutput& writer,
	const Metadata::MetadataFile& module, std::uint32_t methodToken, int index)
{
	// The C# reads the method definition's Static flag (the II.23.1.10 column)
	// and walks the Param rows by SequenceNumber -- the row with the exact
	// sequence (static: index + 1, instance: index, because IL index 0 is the
	// implicit `this`) names the parameter; a nil name or a missing row yields
	// the bare-index fallback. GetParameterNames applies the same
	// sequence-to-declared-index mapping (result[seq - 1] over the declared
	// parameters), so the port maps the IL argument index to the
	// declared-parameter index and reads the pre-mapped name.
	std::string name;
	const std::uint32_t attributes = module.GetMethodAttributes(methodToken);
	const bool isStatic = (attributes &
		static_cast<std::uint32_t>(MethodAttributes::Static)) ==
		static_cast<std::uint32_t>(MethodAttributes::Static);
	const int declaredIndex = isStatic ? index : index - 1;
	if (declaredIndex >= 0) {
		const std::vector<std::string> names = module.GetParameterNames(methodToken);
		if (declaredIndex < static_cast<int>(names.size()) && !names[declaredIndex].empty()) {
			// The C# escapes the raw metadata name before writing it.
			name = Escape(names[declaredIndex]);
		}
	}

	// The C# passes the fresh "param_" + index string as the opaque reference
	// token; the port reinterprets the index (see the header note).
	const void* token = reinterpret_cast<const void*>(
		static_cast<std::uintptr_t>(static_cast<unsigned int>(index)));
	if (name.empty()) {
		writer.WriteLocalReference(std::to_string(index), token);
	} else {
		writer.WriteLocalReference(name, token);
	}
}

void WriteVariableReference(Output::ITextOutput& writer, int index)
{
	// The C# passes the fresh "loc_" + index string as the reference token.
	writer.WriteLocalReference(std::to_string(index),
		reinterpret_cast<const void*>(
			static_cast<std::uintptr_t>(static_cast<unsigned int>(index))));
}

// ---------------------------------------------------------------------------
// WriteOperand (DisassemblerHelpers.cs lines 190-292).
// ---------------------------------------------------------------------------
void WriteOperand(Output::ITextOutput& writer, const std::any& operand)
{
	if (!operand.has_value())
		throw std::invalid_argument("operand");

	if (const auto* s = std::any_cast<std::string>(&operand)) {
		WriteOperand(writer, std::string_view(*s));
	} else if (const auto* c = std::any_cast<char16_t>(&operand)) {
		// The C# writes the char's UTF-16 code unit as a decimal number.
		writer.Write(std::to_string(static_cast<int>(static_cast<std::uint32_t>(*c))));
	} else if (const auto* f = std::any_cast<float>(&operand)) {
		WriteOperand(writer, *f);
	} else if (const auto* d = std::any_cast<double>(&operand)) {
		WriteOperand(writer, *d);
	} else if (const auto* b = std::any_cast<bool>(&operand)) {
		writer.Write(*b ? "true" : "false");
	} else if (const auto* v = std::any_cast<std::int8_t>(&operand)) {
		writer.Write(IntegralToString(static_cast<int>(*v)));
	} else if (const auto* v = std::any_cast<std::uint8_t>(&operand)) {
		writer.Write(IntegralToString(static_cast<int>(*v)));
	} else if (const auto* v = std::any_cast<std::int16_t>(&operand)) {
		writer.Write(IntegralToString(static_cast<int>(*v)));
	} else if (const auto* v = std::any_cast<std::uint16_t>(&operand)) {
		writer.Write(IntegralToString(static_cast<int>(*v)));
	} else if (const auto* v = std::any_cast<std::int32_t>(&operand)) {
		writer.Write(IntegralToString(static_cast<long long>(*v)));
	} else if (const auto* v = std::any_cast<std::uint32_t>(&operand)) {
		writer.Write(IntegralToString(static_cast<unsigned long long>(*v)));
	} else if (const auto* v = std::any_cast<std::int64_t>(&operand)) {
		writer.Write(IntegralToString(static_cast<long long>(*v)));
	} else if (const auto* v = std::any_cast<std::uint64_t>(&operand)) {
		writer.Write(IntegralToString(static_cast<unsigned long long>(*v)));
	} else {
		// The C# fallback is ToInvariantCultureString(operand), whose default
		// object.ToString() returns the runtime type's name; the port renders
		// the held type's name (the realistic operand set never reaches it).
		writer.Write(operand.type().name());
	}
}

void WriteOperand(Output::ITextOutput& writer, std::int64_t val)
{
	writer.Write(IntegralToString(static_cast<long long>(val)));
}

// The shared float/double rendering: the zero/negative-zero special case, the
// infinity/NaN byte dump, and the round-trip format (the TextWriterTokenWriter
// FormatFloatRoundTrip convention: std::to_chars with 'e' upper-cased to 'E',
// matching the C# "R" format).
template <typename T>
void WriteFloatingOperand(Output::ITextOutput& writer, T val) {
	if (val == 0) {
		if (1 / val == -std::numeric_limits<T>::infinity()) {
			// negative zero is a special case
			writer.Write('-');
		}
		writer.Write("0.0");
	} else if (std::isinf(val) || std::isnan(val)) {
		unsigned char data[sizeof(T)];
		std::memcpy(data, &val, sizeof(T));
		writer.Write('(');
		char buf[4];
		for (std::size_t i = 0; i < sizeof(T); i++) {
			if (i > 0)
				writer.Write(' ');
			std::snprintf(buf, sizeof(buf), "%02X", data[i]);
			writer.Write(buf);
		}
		writer.Write(')');
	} else {
		char buf[64];
		auto res = std::to_chars(buf, buf + sizeof(buf), val);
		std::string s(buf, res.ptr);
		for (char& c : s) if (c == 'e') c = 'E';
		writer.Write(s);
	}
}

void WriteOperand(Output::ITextOutput& writer, float val)
{
	WriteFloatingOperand(writer, val);
}

void WriteOperand(Output::ITextOutput& writer, double val)
{
	WriteFloatingOperand(writer, val);
}

void WriteOperand(Output::ITextOutput& writer, std::string_view operand)
{
	writer.Write('"');
	writer.Write(EscapeString(operand));
	writer.Write('"');
}

// ---------------------------------------------------------------------------
// EscapeString (DisassemblerHelpers.cs lines 294-335).
// ---------------------------------------------------------------------------
std::string EscapeString(std::string_view str)
{
	std::string sb;
	std::size_t i = 0;
	while (i < str.size()) {
		const auto decoded = DecodeUtf8(str, i);
		const char32_t cp = decoded.first;
		const std::size_t n = decoded.second;
		switch (cp) {
		case '"':
			sb.append("\\\"");
			break;
		case '\\':
			sb.append("\\\\");
			break;
		case 0x00:
			sb.append("\\0");
			break;
		case 0x07:
			sb.append("\\a");
			break;
		case 0x08:
			sb.append("\\b");
			break;
		case 0x0C:
			sb.append("\\f");
			break;
		case 0x0A:
			sb.append("\\n");
			break;
		case 0x0D:
			sb.append("\\r");
			break;
		case 0x09:
			sb.append("\\t");
			break;
		case 0x0B:
			sb.append("\\v");
			break;
		default:
			// print control characters and uncommon white spaces as numbers
			if (cp >= 0x10000) {
				// A non-BMP code point iterates as TWO surrogate halves over
				// the C#'s UTF-16 `char` loop, each `IsSurrogate` half
				// escaping separately -- the port emits the same pair.
				const std::uint32_t offset = static_cast<std::uint32_t>(cp - 0x10000);
				AppendUnicodeEscape4(sb, 0xD800u + (offset >> 10));
				AppendUnicodeEscape4(sb, 0xDC00u + (offset & 0x3FFu));
			} else if (IsUtf16Surrogate(cp) || IsUtf16Control(cp) ||
				(IsUtf16WhiteSpace(cp) && cp != ' ')) {
				AppendUnicodeEscape4(sb, static_cast<std::uint32_t>(cp));
			} else {
				sb.append(str.substr(i, n));
			}
			break;
		}
		i += n;
	}
	return sb;
}

// ---------------------------------------------------------------------------
// PrimitiveTypeName (DisassemblerHelpers.cs lines 337-373).
// ---------------------------------------------------------------------------
const char* PrimitiveTypeName(std::string_view fullName)
{
	if (fullName == "System.SByte") return "int8";
	if (fullName == "System.Int16") return "int16";
	if (fullName == "System.Int32") return "int32";
	if (fullName == "System.Int64") return "int64";
	if (fullName == "System.Byte") return "uint8";
	if (fullName == "System.UInt16") return "uint16";
	if (fullName == "System.UInt32") return "uint32";
	if (fullName == "System.UInt64") return "uint64";
	if (fullName == "System.Single") return "float32";
	if (fullName == "System.Double") return "float64";
	if (fullName == "System.Void") return "void";
	if (fullName == "System.Boolean") return "bool";
	if (fullName == "System.String") return "string";
	if (fullName == "System.Char") return "char";
	if (fullName == "System.Object") return "object";
	if (fullName == "System.IntPtr") return "native int";
	return nullptr;
}

} // namespace ILSpy::Decompiler::Disassembler
