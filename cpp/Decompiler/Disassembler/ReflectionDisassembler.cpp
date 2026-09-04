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

// The ReflectionDisassembler instance implementation -- see the header for
// the porting decisions.

#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Disassembler/ReflectionAttributes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/ITextOutput.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <any>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Disassembler {

namespace {

// The C# `blob.ReadByte()` past the end of the blob -- the
// BadImageFormatException path ports to std::out_of_range (the ILParser
// truncated-operand convention).
std::uint8_t ReadByte(const std::uint8_t* base, std::size_t size,
    std::size_t& pos)
{
    if (pos >= size) {
        throw std::out_of_range("native type blob read past end");
    }
    return base[pos++];
}

std::size_t RemainingBytes(std::size_t size, std::size_t pos)
{
    return size - pos;
}

// The C# `BlobReader.ReadCompressedInteger()`: the II.23.2 compressed
// integer -- the raw unsigned payload (1 byte 0x00-0x7F, 2 bytes with the
// 0x80-0xBF prefix and 14 payload bits, 4 bytes with the 0xC0-0xDF prefix
// and 29 payload bits). The payload is unsigned because it is what
// BlobBuilder.WriteCompressedInteger encodes (the SerString length
// round-trips through this reader), so the II.23.2.2 signed form's sign
// bits are never set by an encoder that produced a valid blob; the reserved
// 0xE0-0xFF prefixes throw (the C# BadImageFormatException -- loud rather
// than wrong).
int ReadCompressedInteger(const std::uint8_t* base, std::size_t size,
    std::size_t& pos)
{
    std::uint8_t b0 = ReadByte(base, size, pos);
    if ((b0 & 0x80) == 0) {
        return b0;
    }
    if ((b0 & 0xC0) == 0x80) {
        return (static_cast<int>(b0 & 0x3F) << 8) | ReadByte(base, size, pos);
    }
    if ((b0 & 0xE0) == 0xC0) {
        return (static_cast<int>(b0 & 0x1F) << 24)
            | (static_cast<int>(ReadByte(base, size, pos)) << 16)
            | (static_cast<int>(ReadByte(base, size, pos)) << 8)
            | static_cast<int>(ReadByte(base, size, pos));
    }
    throw std::out_of_range("invalid compressed integer");
}

// The C# `BlobReader.TryReadCompressedInteger(out int value)`: false when
// the blob is exhausted (the C# `value ? value : -1` fallbacks in the array
// arm). A truncated multi-byte prefix is also a failure -- the port skips
// to the end of the blob (the C# throws BadImageFormatException there; the
// Try contract cannot throw, a divergence confined to corrupt blobs).
bool TryReadCompressedInteger(const std::uint8_t* base, std::size_t size,
    std::size_t& pos, int& value)
{
    if (pos >= size) {
        value = 0;
        return false;
    }
    try {
        value = ReadCompressedInteger(base, size, pos);
        return true;
    } catch (const std::out_of_range&) {
        // Skip whatever partial prefix was consumed -- the Try contract
        // cannot leave the cursor mid-form (a divergence confined to
        // corrupt blobs).
        pos = size;
        value = 0;
        return false;
    }
}

// The C# `BlobReader.ReadSerializedString()`: the II.23.3 SerString -- a
// compressed length prefix then that many bytes. The C# returns null for a
// zero length; the port collapses null and empty to "" (every consumer goes
// through IsNullOrEmpty, which cannot distinguish them).
std::string ReadSerializedString(const std::uint8_t* base, std::size_t size,
    std::size_t& pos)
{
    int length = ReadCompressedInteger(base, size, pos);
    if (length == 0) {
        return {};
    }
    if (length < 0 || RemainingBytes(size, pos) < static_cast<std::size_t>(length)) {
        throw std::out_of_range("serialized string read past end of blob");
    }
    std::string out(reinterpret_cast<const char*>(base + pos),
        static_cast<std::size_t>(length));
    pos += static_cast<std::size_t>(length);
    return out;
}

constexpr const char* kGuidEmpty = "00000000-0000-0000-0000-000000000000";

// The C# `object.GetType().FullName` over a constant value the
// DisassemblerHelpers.PrimitiveTypeName input reads -- the BCL type name of
// the value SRM's ReadConstant handed back (System.Int32 &c.). The empty
// any is the C# null the WriteConstant value-is-string test then crashes on
// (the NullReferenceException path -- unreachable through WriteConstant,
// whose NullReference arm returns before reading a value); the port keeps
// it loud, a std::runtime_error.
std::string_view ValueFullName(const std::any& value)
{
    if (std::any_cast<bool>(&value)) return "System.Boolean";
    if (std::any_cast<char16_t>(&value)) return "System.Char";
    if (std::any_cast<std::int8_t>(&value)) return "System.SByte";
    if (std::any_cast<std::uint8_t>(&value)) return "System.Byte";
    if (std::any_cast<std::int16_t>(&value)) return "System.Int16";
    if (std::any_cast<std::uint16_t>(&value)) return "System.UInt16";
    if (std::any_cast<std::int32_t>(&value)) return "System.Int32";
    if (std::any_cast<std::uint32_t>(&value)) return "System.UInt32";
    if (std::any_cast<std::int64_t>(&value)) return "System.Int64";
    if (std::any_cast<std::uint64_t>(&value)) return "System.UInt64";
    if (std::any_cast<float>(&value)) return "System.Single";
    if (std::any_cast<double>(&value)) return "System.Double";
    throw std::runtime_error("constant value is null");
}

// The SRM `BlobReader.ReadConstant(ConstantTypeCode)` over the constant's
// value blob (the .NET 10 System.Reflection.Metadata the repo pins):
// the little-endian numeric reads, the Boolean's nonzero test, the whole
// remaining blob as UTF-16 for String (an odd trailing byte drops --
// ReadUTF16 takes byteCount/2 chars), and the uint32-zero check for the
// NullReference code (a nonzero payload is the C# BadImageFormatException;
// unreachable through WriteConstant, which never passes 0x12 here -- kept
// distinct from the out-of-range throws so the caller's catch does not
// swallow it). Unknown codes and reads past the end of the blob throw
// std::out_of_range (the C# ArgumentOutOfRangeException).
std::any ReadConstantValue(std::uint8_t typeCode, const std::uint8_t* base,
    std::size_t size)
{
    auto need = [&](std::size_t n) {
        if (n > size) {
            throw std::out_of_range("constant blob read past end");
        }
    };
    switch (typeCode) {
        case 0x02: {  // Boolean
            need(1);
            return std::any{static_cast<bool>(base[0] != 0)};
        }
        case 0x03: {  // Char
            need(2);
            return std::any{static_cast<char16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x04: {  // SByte
            need(1);
            return std::any{static_cast<std::int8_t>(base[0])};
        }
        case 0x05: {  // Byte
            need(1);
            return std::any{static_cast<std::uint8_t>(base[0])};
        }
        case 0x06: {  // Int16
            need(2);
            return std::any{static_cast<std::int16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x07: {  // UInt16
            need(2);
            return std::any{static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x08: {  // Int32
            need(4);
            std::uint32_t v = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            return std::any{static_cast<std::int32_t>(v)};
        }
        case 0x09: {  // UInt32
            need(4);
            return std::any{static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24)};
        }
        case 0x0A: {  // Int64
            need(8);
            std::uint64_t v = 0;
            for (int i = 7; i >= 0; --i)
                v = (v << 8) | static_cast<std::uint64_t>(base[i]);
            return std::any{static_cast<std::int64_t>(v)};
        }
        case 0x0B: {  // UInt64
            need(8);
            std::uint64_t v = 0;
            for (int i = 7; i >= 0; --i)
                v = (v << 8) | static_cast<std::uint64_t>(base[i]);
            return std::any{v};
        }
        case 0x0C: {  // Single
            need(4);
            std::uint32_t bits = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return std::any{f};
        }
        case 0x0D: {  // Double
            need(8);
            std::uint64_t bits = 0;
            for (int i = 7; i >= 0; --i)
                bits = (bits << 8) | static_cast<std::uint64_t>(base[i]);
            double d;
            std::memcpy(&d, &bits, sizeof(d));
            return std::any{d};
        }
        case 0x0E: {  // String: the remaining blob as UTF-16.
            std::size_t chars = size / 2;
            std::u16string utf16(chars, u'\0');
            for (std::size_t i = 0; i < chars; ++i) {
                utf16[i] = static_cast<char16_t>(
                    static_cast<std::uint16_t>(base[2 * i])
                    | (static_cast<std::uint16_t>(base[2 * i + 1]) << 8));
            }
            return std::any{Util::Utf16ToUtf8(utf16)};
        }
        case 0x12: {  // NullReference (the ELEMENT_TYPE_CLASS slot)
            need(4);
            std::uint32_t v = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            if (v != 0) {
                // The C# BadImageFormatException -- NOT the
                // ArgumentOutOfRangeException the WriteConstant catch takes.
                throw std::runtime_error("invalid constant value");
            }
            return std::any{};  // the C# null
        }
        default:
            throw std::out_of_range("invalid constant type code");
    }
}

// The C# `new Guid(string)` + `guid.ToString()` pair over the custom
// marshaler's GUID string: parse the N/D/B/P forms (leading and trailing
// whitespace, {} or () wrapping, 32 hex digits with or without the
// 8-4-4-4-12 dashes) and render the lowercase "D" form. A malformed string
// is the C# FormatException (uncaught on this path) -- the port throws
// std::runtime_error.
std::string GuidFromString(const std::string& raw)
{
    // Trim whitespace.
    std::size_t begin = raw.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        throw std::runtime_error("invalid GUID string");
    }
    std::size_t end = raw.find_last_not_of(" \t\r\n");
    std::string s = raw.substr(begin, end - begin + 1);
    // Strip the {} (B) or () (P) wrappers.
    if (s.size() >= 2 && ((s.front() == '{' && s.back() == '}')
            || (s.front() == '(' && s.back() == ')'))) {
        s = s.substr(1, s.size() - 2);
    }
    // The D form (dashes at 8/13/18/23, 36 chars) or the N form (32 chars).
    if (s.size() == 36) {
        if (s[8] != '-' || s[13] != '-' || s[18] != '-' || s[23] != '-') {
            throw std::runtime_error("invalid GUID string");
        }
        s.erase(8, 1);
        s.erase(12, 1);
        s.erase(16, 1);
        s.erase(20, 1);
    }
    if (s.size() != 32) {
        throw std::runtime_error("invalid GUID string");
    }
    unsigned char bytes[16] = {};
    for (std::size_t i = 0; i < 32; ++i) {
        char c = s[i];
        int v;
        if (c >= '0' && c <= '9') {
            v = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            v = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            v = c - 'A' + 10;
        } else {
            throw std::runtime_error("invalid GUID string");
        }
        bytes[i / 2] = static_cast<unsigned char>((bytes[i / 2] << 4) | v);
    }
    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5], bytes[6],
        bytes[7], bytes[8], bytes[9], bytes[10], bytes[11], bytes[12],
        bytes[13], bytes[14], bytes[15]);
    return buf;
}

}  // namespace

// ---------------------------------------------------------------------------
// The constructors (ReflectionDisassembler.cs lines 87-99).
// ---------------------------------------------------------------------------
ReflectionDisassembler::ReflectionDisassembler(Output::ITextOutput& output,
    MethodBodyDisassembler& methodBodyDisassembler)
    : output_(output),
      ownedMethodBodyDisassembler_(),
      methodBodyDisassembler_(&methodBodyDisassembler)
{
}

ReflectionDisassembler::ReflectionDisassembler(Output::ITextOutput& output)
    : output_(output),
      ownedMethodBodyDisassembler_(std::make_unique<MethodBodyDisassembler>(output)),
      methodBodyDisassembler_(ownedMethodBodyDisassembler_.get())
{
}

// ---------------------------------------------------------------------------
// The delegating flag properties (ReflectionDisassembler.cs lines 55-74).
// ---------------------------------------------------------------------------
bool ReflectionDisassembler::DetectControlStructure() const
{
    return methodBodyDisassembler_->DetectControlStructure;
}

void ReflectionDisassembler::DetectControlStructure(bool value)
{
    methodBodyDisassembler_->DetectControlStructure = value;
}

bool ReflectionDisassembler::ShowSequencePoints() const
{
    return methodBodyDisassembler_->ShowSequencePoints;
}

void ReflectionDisassembler::ShowSequencePoints(bool value)
{
    methodBodyDisassembler_->ShowSequencePoints = value;
}

bool ReflectionDisassembler::ShowMetadataTokens() const
{
    return methodBodyDisassembler_->ShowMetadataTokens;
}

void ReflectionDisassembler::ShowMetadataTokens(bool value)
{
    methodBodyDisassembler_->ShowMetadataTokens = value;
}

bool ReflectionDisassembler::ShowMetadataTokensInBase10() const
{
    return methodBodyDisassembler_->ShowMetadataTokensInBase10;
}

void ReflectionDisassembler::ShowMetadataTokensInBase10(bool value)
{
    methodBodyDisassembler_->ShowMetadataTokensInBase10 = value;
}

bool ReflectionDisassembler::ShowRawRVAOffsetAndBytes() const
{
    return methodBodyDisassembler_->ShowRawRVAOffsetAndBytes;
}

void ReflectionDisassembler::ShowRawRVAOffsetAndBytes(bool value)
{
    methodBodyDisassembler_->ShowRawRVAOffsetAndBytes = value;
}

// ---------------------------------------------------------------------------
// WriteMetadataToken (ReflectionDisassembler.cs -- the internal static).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::WriteMetadataToken(Output::ITextOutput& output,
    const Metadata::MetadataFile& module, std::uint32_t handleToken,
    std::uint32_t metadataToken, bool spaceAfter, bool spaceBefore,
    bool showMetadataTokens, bool base10)
{
    // The C# `handle can be null in case of errors, if that's the case, we
    // always want to print a comment, with the metadataToken`: handleToken 0
    // is the C# null handle.
    if (showMetadataTokens || handleToken == 0) {
        if (spaceBefore) {
            output.Write(' ');
        }
        output.Write("/* ");
        char buf[16];
        if (base10) {
            // The C# `metadataToken.ToString(null)` -- plain decimal. The raw
            // metadata tokens reaching this path are non-negative int32s.
            std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(metadataToken));
        } else {
            std::snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(metadataToken));
        }
        // The C# `if (handle == null || !handle.Value.IsEntityHandle())
        // output.Write(token) else output.WriteReference(module, handle,
        // token, "metadata")` -- the port's entity tables are every table id
        // the disassembler sees except the 0x70 UserString heap (and the null
        // handle itself).
        if (handleToken == 0 || (handleToken >> 24) == 0x70) {
            output.Write(buf);
        } else {
            output.WriteReference(module, handleToken, buf, "metadata");
        }
        output.Write(" */");
        if (spaceAfter) {
            output.Write(' ');
        }
    } else if (spaceBefore && spaceAfter) {
        output.Write(' ');
    }
}

// ---------------------------------------------------------------------------
// WriteBlob (ReflectionDisassembler.cs lines 1926-1948).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::WriteBlob(const std::uint8_t* base,
    std::size_t size)
{
    output_.Write("(");
    if (size > 0) {
        output_.Indent();
        for (std::size_t i = 0; i < size; ++i) {
            // The line break fires before each 16th byte except the last;
            // every other byte is preceded by a space.
            if (i % 16 == 0 && i < size - 1) {
                output_.WriteLine();
            } else {
                output_.Write(' ');
            }
            char buf[3];
            std::snprintf(buf, sizeof(buf), "%02x", base[i]);
            output_.Write(buf);
        }
        output_.WriteLine();
        output_.Unindent();
    }
    output_.Write(")");
}

// ---------------------------------------------------------------------------
// OpenBlock/CloseBlock (ReflectionDisassembler.cs lines 1952-1969).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::OpenBlock(bool defaultCollapsed)
{
    output_.MarkFoldStart("...", /*defaultCollapsed=*/!ExpandMemberDefinitions && defaultCollapsed,
        /*isDefinition=*/true);
    output_.WriteLine();
    Output::WriteLine(output_, "{");
    output_.Indent();
}

void ReflectionDisassembler::CloseBlock(const char* comment)
{
    output_.Unindent();
    output_.Write("}");
    // The C# `if (comment != null)` -- nullptr is the default null comment.
    if (comment != nullptr) {
        output_.Write(" // ");
        output_.Write(comment);
    }
    output_.MarkFoldEnd();
    output_.WriteLine();
}

// ---------------------------------------------------------------------------
// WriteMarshalInfo/WriteNativeType (ReflectionDisassembler.cs lines
// 875-1103).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::WriteMarshalInfo(const std::uint8_t* base,
    std::size_t size)
{
    output_.Write("marshal(");
    std::size_t pos = 0;
    WriteNativeType(base, size, pos);
    output_.Write(") ");
}

void ReflectionDisassembler::WriteNativeType(const std::uint8_t* base,
    std::size_t size, std::size_t& pos)
{
    std::uint8_t type = ReadByte(base, size, pos);
    switch (type) {
        case 0x66:  // None
        case 0x50:  // Max
            break;
        case 0x02:  // NATIVE_TYPE_BOOLEAN
            output_.Write("bool");
            break;
        case 0x03:  // NATIVE_TYPE_I1
            output_.Write("int8");
            break;
        case 0x04:  // NATIVE_TYPE_U1
            output_.Write("unsigned int8");
            break;
        case 0x05:  // NATIVE_TYPE_I2
            output_.Write("int16");
            break;
        case 0x06:  // NATIVE_TYPE_U2
            output_.Write("unsigned int16");
            break;
        case 0x07:  // NATIVE_TYPE_I4
            output_.Write("int32");
            break;
        case 0x08:  // NATIVE_TYPE_U4
            output_.Write("unsigned int32");
            break;
        case 0x09:  // NATIVE_TYPE_I8
            output_.Write("int64");
            break;
        case 0x0A:  // NATIVE_TYPE_U8
            output_.Write("unsigned int64");
            break;
        case 0x0B:  // NATIVE_TYPE_R4
            output_.Write("float32");
            break;
        case 0x0C:  // NATIVE_TYPE_R8
            output_.Write("float64");
            break;
        case 0x14:  // NATIVE_TYPE_LPSTR
            output_.Write("lpstr");
            break;
        case 0x1F:  // NATIVE_TYPE_INT
            output_.Write("int");
            break;
        case 0x20:  // NATIVE_TYPE_UINT
            output_.Write("unsigned int");
            break;
        case 0x26:  // NATIVE_TYPE_FUNC
            output_.Write("Func");
            break;
        case 0x2A: {  // NATIVE_TYPE_ARRAY
            if (RemainingBytes(size, pos) > 0) {
                WriteNativeType(base, size, pos);
            }
            output_.Write('[');
            int value = 0;
            int sizeParameterIndex = TryReadCompressedInteger(base, size, pos, value) ? value : -1;
            int arraySize = TryReadCompressedInteger(base, size, pos, value) ? value : -1;
            int sizeParameterMultiplier = TryReadCompressedInteger(base, size, pos, value) ? value : -1;
            if (arraySize >= 0) {
                output_.Write(std::to_string(arraySize));
            }
            if (sizeParameterIndex >= 0 && sizeParameterMultiplier != 0) {
                output_.Write(" + ");
                output_.Write(std::to_string(sizeParameterIndex));
            }
            output_.Write(']');
            break;
        }
        case 0x0F:  // Currency
            output_.Write("currency");
            break;
        case 0x13:  // BStr
            output_.Write("bstr");
            break;
        case 0x15:  // LPWStr
            output_.Write("lpwstr");
            break;
        case 0x16:  // LPTStr
            output_.Write("lptstr");
            break;
        case 0x17: {  // FixedSysString
            output_.Write("fixed sysstring[");
            output_.Write(std::to_string(ReadCompressedInteger(base, size, pos)));
            output_.Write("]");
            break;
        }
        case 0x19:  // IUnknown
            output_.Write("iunknown");
            break;
        case 0x1A:  // IDispatch
            output_.Write("idispatch");
            break;
        case 0x1B:  // Struct
            output_.Write("struct");
            break;
        case 0x1C:  // IntF
            output_.Write("interface");
            break;
        case 0x1D: {  // SafeArray
            output_.Write("safearray ");
            if (RemainingBytes(size, pos) > 0) {
                std::uint8_t elementType = ReadByte(base, size, pos);
                switch (elementType) {
                    case 0:  // None
                        break;
                    case 2:  // I2
                        output_.Write("int16");
                        break;
                    case 3:  // I4
                        output_.Write("int32");
                        break;
                    case 4:  // R4
                        output_.Write("float32");
                        break;
                    case 5:  // R8
                        output_.Write("float64");
                        break;
                    case 6:  // Currency
                        output_.Write("currency");
                        break;
                    case 7:  // Date
                        output_.Write("date");
                        break;
                    case 8:  // BStr
                        output_.Write("bstr");
                        break;
                    case 9:  // Dispatch
                        output_.Write("idispatch");
                        break;
                    case 10:  // Error
                        output_.Write("error");
                        break;
                    case 11:  // Bool
                        output_.Write("bool");
                        break;
                    case 12:  // Variant
                        output_.Write("variant");
                        break;
                    case 13:  // Unknown
                        output_.Write("iunknown");
                        break;
                    case 14:  // Decimal
                        output_.Write("decimal");
                        break;
                    case 16:  // I1
                        output_.Write("int8");
                        break;
                    case 17:  // UI1
                        output_.Write("unsigned int8");
                        break;
                    case 18:  // UI2
                        output_.Write("unsigned int16");
                        break;
                    case 19:  // UI4
                        output_.Write("unsigned int32");
                        break;
                    case 22:  // Int
                        output_.Write("int");
                        break;
                    case 23:  // UInt
                        output_.Write("unsigned int");
                        break;
                    default:
                        output_.Write(std::to_string(elementType));
                        break;
                }
            }
            break;
        }
        case 0x1E: {  // FixedArray
            output_.Write("fixed array");
            output_.Write("[");
            int value = 0;
            output_.Write(std::to_string(
                TryReadCompressedInteger(base, size, pos, value) ? value : 0));
            output_.Write("]");
            if (RemainingBytes(size, pos) > 0) {
                output_.Write(' ');
                WriteNativeType(base, size, pos);
            }
            break;
        }
        case 0x22:  // ByValStr
            output_.Write("byvalstr");
            break;
        case 0x23:  // ANSIBStr
            output_.Write("ansi bstr");
            break;
        case 0x24:  // TBStr
            output_.Write("tbstr");
            break;
        case 0x25:  // VariantBool
            output_.Write("variant bool");
            break;
        case 0x28:  // ASAny
            output_.Write("as any");
            break;
        case 0x2B:  // LPStruct
            output_.Write("lpstruct");
            break;
        case 0x2C: {  // CustomMarshaler
            std::string guidValue = ReadSerializedString(base, size, pos);
            std::string unmanagedType = ReadSerializedString(base, size, pos);
            std::string managedType = ReadSerializedString(base, size, pos);
            std::string cookie = ReadSerializedString(base, size, pos);
            // The C# `new Guid(guidValue) : Guid.Empty`; the all-zeros
            // rendering IS Guid.Empty (the `guid != Guid.Empty` test below).
            std::string guid = guidValue.empty() ? kGuidEmpty : GuidFromString(guidValue);
            output_.Write("custom(\"");
            output_.Write(EscapeString(managedType));
            output_.Write("\", \"");
            output_.Write(EscapeString(cookie));
            output_.Write("\"");
            if (guid != kGuidEmpty || !unmanagedType.empty()) {
                output_.Write(", \"");
                output_.Write(guid);
                output_.Write("\", \"");
                output_.Write(EscapeString(unmanagedType));
                output_.Write("\"");
            }
            output_.Write(')');
            break;
        }
        case 0x2D:  // Error
            output_.Write("error");
            break;
        default:
            // The byte's decimal value (int.ToString()).
            output_.Write(std::to_string(type));
            break;
    }
}

// The C# `void WriteConstant(MetadataReader metadata, Constant constant)`
// (ReflectionDisassembler.cs lines 1220-1268). See the header.
void ReflectionDisassembler::WriteConstant(const Metadata::ConstantInfo& constant)
{
    switch (constant.TypeCode) {
        case 0x12:  // ConstantTypeCode.NullReference (the raw ELEMENT_TYPE_CLASS
                    // slot -- the C# arm writes the spelling without reading
                    // the blob)
            output_.Write("nullref");
            break;
        default: {
            std::any value;
            try {
                value = ReadConstantValue(constant.TypeCode,
                    constant.Value.data(), constant.Value.size());
            } catch (const std::out_of_range&) {
                // The C# ArgumentOutOfRangeException catch: the unknown code
                // and the too-short blob render the same comment (the C# catch
                // conflates them; the raw value is the enum's ToString of an
                // unnamed member -- the decimal number).
                char buf[64];
                std::snprintf(buf, sizeof(buf),
                    "/* Constant with invalid typecode: %u */",
                    static_cast<unsigned>(constant.TypeCode));
                output_.Write(buf);
                return;
            }
            if (const auto* s = std::any_cast<std::string>(&value)) {
                // The C# `value is string`: the quoted escaped literal, no
                // type wrapper.
                WriteOperand(output_, std::string_view(*s));
                break;
            }
            // The C# `PrimitiveTypeName(value.GetType().FullName)` (a null
            // result writes nothing -- the C# ITextOutput.Write(null) no-op;
            // unreachable for the ReadConstant value set).
            const char* typeName = PrimitiveTypeName(ValueFullName(value));
            if (typeName != nullptr) {
                output_.Write(typeName);
            }
            output_.Write('(');
            if (const auto* cf = std::any_cast<float>(&value)) {
                // The C# `float.IsNaN || float.IsInfinity` bit-pattern render.
                if (std::isnan(*cf) || std::isinf(*cf)) {
                    std::uint32_t bits;
                    std::memcpy(&bits, cf, sizeof(bits));
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08x", bits);
                    output_.Write(buf);
                } else {
                    WriteOperand(output_, *cf);
                }
            } else if (const auto* cd = std::any_cast<double>(&value)) {
                // The C# `double.IsNaN || double.IsInfinity` bit-pattern render.
                if (std::isnan(*cd) || std::isinf(*cd)) {
                    std::uint64_t bits;
                    std::memcpy(&bits, cd, sizeof(bits));
                    char buf[24];
                    std::snprintf(buf, sizeof(buf), "0x%016llx",
                        static_cast<unsigned long long>(bits));
                    output_.Write(buf);
                } else {
                    WriteOperand(output_, *cd);
                }
            } else {
                WriteOperand(output_, value);
            }
            output_.Write(')');
            break;
        }
    }
}

// The C# "param_" + index local-reference token (the fresh string object the
// C# passes as the WriteLocalReference identity) -- the port's reinterpret of
// the integer (the WriteParameterReference convention).
const void* ParamReferenceToken(std::size_t index)
{
    return reinterpret_cast<const void*>(index);
}

// The C# `void WriteParameters(MetadataReader metadata,
// IEnumerable<ParameterHandle> parameters,
// MethodSignature<Action<ILNameSyntax>> signature)`// (ReflectionDisassembler.cs lines 1107-1160). See the header.
void ReflectionDisassembler::WriteParameters(
    const std::vector<Metadata::ParameterInfo>& parameters,
    const Metadata::MethodSignatureT& signature)
{
    // The C# `int i` / `int offset`: the declared-parameter cursor and the
    // implicit-`this` shift of the "param_N" references (IL index 0 is this).
    std::size_t i = 0;
    std::size_t offset = signature.Header.IsInstance() ? 1 : 0;

    for (const auto& p : parameters) {
        // skip return type parameter handle
        if (p.SequenceNumber == 0)
            continue;

        // fill gaps in parameter list
        while (i + 1 < p.SequenceNumber) {
            if (i > 0) {
                output_.Write(',');
                output_.WriteLine();
            }
            signature.ParameterTypes.at(i)(ILNameSyntax::Signature);
            output_.Write(' ');
            output_.WriteLocalReference("''", ParamReferenceToken(i + offset),
                /*isDefinition=*/true);
            i++;
        }

        // separator
        if (i > 0) {
            output_.Write(',');
            output_.WriteLine();
        }

        // print parameter
        if ((p.Attributes
                & static_cast<std::uint32_t>(ParameterAttributes::In))
            == static_cast<std::uint32_t>(ParameterAttributes::In)) {
            output_.Write("[in] ");
        }
        if ((p.Attributes
                & static_cast<std::uint32_t>(ParameterAttributes::Out))
            == static_cast<std::uint32_t>(ParameterAttributes::Out)) {
            output_.Write("[out] ");
        }
        if ((p.Attributes
                & static_cast<std::uint32_t>(ParameterAttributes::Optional))
            == static_cast<std::uint32_t>(ParameterAttributes::Optional)) {
            output_.Write("[opt] ");
        }
        signature.ParameterTypes.at(i)(ILNameSyntax::Signature);
        output_.Write(' ');
        if (p.MarshallingDescriptor.has_value()) {
            WriteMarshalInfo(p.MarshallingDescriptor->data(),
                p.MarshallingDescriptor->size());
        }
        output_.WriteLocalReference(Escape(p.Name),
            ParamReferenceToken(i + offset), /*isDefinition=*/true);
        i++;
    }

    // add remaining parameter types as unnamed parameters
    while (i < signature.RequiredParameterCount) {
        if (i > 0) {
            output_.Write(',');
            output_.WriteLine();
        }
        signature.ParameterTypes.at(i)(ILNameSyntax::Signature);
        output_.Write(' ');
        output_.WriteLocalReference("''", ParamReferenceToken(i + offset),
            /*isDefinition=*/true);
        i++;
    }

    output_.WriteLine();
}

// The C# `public IEntityProcessor EntityProcessor { get; set; }` -- the
// port's pointer getter/setter over the caller-owned processor.
IEntityProcessor* ReflectionDisassembler::EntityProcessor() const {
    return entityProcessor_;
}

void ReflectionDisassembler::EntityProcessor(IEntityProcessor* value) {
    entityProcessor_ = value;
}

// The C# private `Process` overloads (`EntityProcessor?.Process(module,
// items) ?? items`): the unprocessed collection when no processor is set.
std::vector<std::uint32_t> ReflectionDisassembler::Process(
    const Metadata::MetadataFile& module,
    const std::vector<std::uint32_t>& items,
    ProcessedEntityKind kind) const {
    if (entityProcessor_ == nullptr) return items;
    return entityProcessor_->Process(module, items, kind);
}

}  // namespace ILSpy::Decompiler::Disassembler
