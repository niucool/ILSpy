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
#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"
#include "Decompiler/Disassembler/EnumNameCollection.hpp"
#include "Decompiler/Disassembler/ReflectionAttributes.hpp"
#include "Decompiler/IL/InstructionOutputExtensions.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Output/ITextOutput.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
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

const DebugInfo::IDebugInfoProvider* ReflectionDisassembler::DebugInfo() const
{
    return methodBodyDisassembler_->DebugInfo;
}

void ReflectionDisassembler::DebugInfo(const DebugInfo::IDebugInfoProvider* value)
{
    methodBodyDisassembler_->DebugInfo = value;
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

// The C# `public IAssemblyResolver AssemblyResolver { get; set; }` -- the
// port's pointer getter/setter over the caller-owned resolver.
Metadata::IAssemblyResolver* ReflectionDisassembler::AssemblyResolver() const {
    return assemblyResolver_;
}

void ReflectionDisassembler::AssemblyResolver(
    Metadata::IAssemblyResolver* value) {
    assemblyResolver_ = value;
}

// The C# `void WriteValue(ITextOutput output, (PrimitiveTypeCode Code,
// string Name) type, object value)` -- see the header comment.
void ReflectionDisassembler::WriteValue(Output::ITextOutput& output,
    const SecurityDeclarationType& type, const std::any& value) {
    using TypedArgument = TypeSystem::CustomAttributeTypedArgumentT<
        SecurityDeclarationType>;
    if (const auto* boxedValue = std::any_cast<TypedArgument>(&value)) {
        output.Write("object(");
        WriteValue(output, boxedValue->Type(), boxedValue->Value());
        output.Write(")");
        return;
    }
    if (const auto* arrayValue =
            std::any_cast<std::vector<TypedArgument>>(&value)) {
        // The C# `type.Name != null && !type.Name.StartsWith("enum ",
        // StringComparison.Ordinal) ? type.Name.Remove(type.Name.Length -
        // 2) : PrimitiveTypeCodeToString(type.Code)`.
        std::string elementType;
        if (type.Name && type.Name->rfind("enum ", 0) != 0
            && type.Name->size() >= 2) {
            elementType = type.Name->substr(0, type.Name->size() - 2);
        } else {
            elementType = PrimitiveTypeCodeToString(type.Code);
        }

        output.Write(elementType);
        output.Write("[");
        output.Write(std::to_string(arrayValue->size()));
        output.Write("](");
        bool first = true;
        for (const auto& item : *arrayValue) {
            if (!first)
                output.Write(" ");
            // `Value()` returns the std::any BY VALUE: materialize it
            // before taking its address (a pointer into the call temporary
            // dangles at the end of the condition's full expression).
            const std::any itemValue = item.Value();
            if (const auto* boxedItem =
                    std::any_cast<TypedArgument>(&itemValue)) {
                WriteValue(output, boxedItem->Type(), boxedItem->Value());
            } else {
                WriteSimpleValue(output, itemValue, elementType);
            }
            first = false;
        }
        output.Write(")");
        return;
    }
    // The C# `type.Name != null && !type.Name.StartsWith("enum ", ...) ?
    // type.Name : PrimitiveTypeCodeToString(type.Code)`.
    std::string typeName;
    if (type.Name && type.Name->rfind("enum ", 0) != 0) {
        typeName = *type.Name;
    } else {
        typeName = PrimitiveTypeCodeToString(type.Code);
    }

    output.Write(typeName);
    output.Write("(");
    WriteSimpleValue(output, value, typeName);
    output.Write(")");
}

// The C# `private static void WriteSimpleValue(ITextOutput output, object
// value, string typeName)` -- see the header comment.
void ReflectionDisassembler::WriteSimpleValue(Output::ITextOutput& output,
    const std::any& value, const std::string& typeName) {
    if (typeName == "string") {
        // The C# "'" + EscapeString(value.ToString()).Replace("'", "\\'")
        // + "'": the escaped text with every embedded single quote
        // backslash-escaped.
        std::string text = std::any_cast<std::string>(value);
        std::string escaped = EscapeString(text);
        std::string withQuotes;
        for (char ch : escaped) {
            if (ch == '\'') {
                withQuotes += "\\";
            }
            withQuotes += ch;
        }
        output.Write("'");
        output.Write(withQuotes);
        output.Write("'");
        return;
    }
    if (typeName == "type") {
        // The value is the (Code, Name) pair; an "enum "-prefixed name
        // renders without the prefix.
        SecurityDeclarationType info =
            std::any_cast<SecurityDeclarationType>(value);
        if (info.Name) {
            if (info.Name->rfind("enum ", 0) == 0)
                output.Write(info.Name->substr(5));
            else
                output.Write(*info.Name);
        }
        return;
    }
    WriteOperand(output, value);
}

// The C# `void WriteDecodedCustomAttributeBlob(CustomAttribute attr,
// MetadataFile module)` -- see the header comment.
void ReflectionDisassembler::WriteDecodedCustomAttributeBlob(
    const Metadata::MetadataFile& module,
    const Metadata::CustomAttributeRowInfo& attr) {
    Metadata::CustomAttributeValueT<SecurityDeclarationType> value;
    try {
        // The C# `var provider = new SecurityDeclarationDecoder(output,
        // AssemblyResolver, module); value = attr.DecodeValue(provider);`
        // -- one decoder per row over the resolver-backed provider.
        SecurityDeclarationDecoder provider(output_, AssemblyResolver(),
            module);
        Metadata::CustomAttributeDecoderT<SecurityDeclarationDecoder> decoder(module,
            provider);
        value = decoder.DecodeValue(attr.ConstructorToken,
            attr.ValueBlob ? attr.ValueBlob->data() : nullptr,
            attr.ValueBlob ? attr.ValueBlob->size() : 0);
    } catch (const std::invalid_argument&) {
        // The C# catch (BadImageFormatException): the comment plus the raw
        // blob dump. (The C# catches ONLY BadImageFormatException -- the
        // EnumUnderlyingTypeResolveException and any other failure
        // propagate.)
        output_.Write("/* Could not decode attribute value */ ");
        WriteBlob(attr.ValueBlob ? attr.ValueBlob->data() : nullptr,
            attr.ValueBlob ? attr.ValueBlob->size() : 0);
        return;
    }

    output_.Write("{");
    output_.Indent();

    for (const auto& arg : value.FixedArguments) {
        output_.WriteLine();
        WriteValue(output_, arg.Type(), arg.Value());
    }

    for (const auto& arg : value.NamedArguments) {
        output_.WriteLine();
        switch (arg.Kind()) {
            case TypeSystem::CustomAttributeNamedArgumentKind::Field:
                output_.Write("field ");
                break;
            case TypeSystem::CustomAttributeNamedArgumentKind::Property:
                output_.Write("property ");
                break;
        }

        // The C# `arg.Type.Name ?? PrimitiveTypeCodeToString(arg.Type.Code)`.
        output_.Write(arg.Type().Name
                ? *arg.Type().Name
                : PrimitiveTypeCodeToString(arg.Type().Code));
        output_.Write(" " + Escape(arg.Name()) + " = ");
        WriteValue(output_, arg.Type(), arg.Value());
    }

    output_.WriteLine();
    output_.Unindent();
    output_.Write("}");
}

// The C# `void TryDecodeSecurityDeclaration(TextOutputWithRollback output,
// BlobReader blob, MetadataFile module)` -- see the header comment.
void ReflectionDisassembler::TryDecodeSecurityDeclaration(
    Output::TextOutputWithRollback& output, Metadata::BlobReader blob,
    const Metadata::MetadataFile& module) {
    output.Write(" = {");
    output.WriteLine();
    output.Indent();

    std::string currentAssemblyName;
    std::string currentFullAssemblyName;
    if (auto assemblyDefinition = module.GetAssemblyDefinition()) {
        currentAssemblyName = assemblyDefinition->Name;
    } else {
        // The C# `catch (BadImageFormatException)` arm of the
        // GetAssemblyDefinition().Name read (the port's read never throws;
        // the malformed-assembly arm renders the same fallback).
        currentAssemblyName = "<ERR: invalid assembly name>";
    }
    if (auto full = Metadata::TryGetFullAssemblyName(module)) {
        currentFullAssemblyName = *full;
    } else {
        currentFullAssemblyName = "<ERR: invalid assembly name>";
    }
    int count = blob.ReadCompressedInteger();
    for (int i = 0; i < count; i++) {
        std::optional<std::string> fullTypeNameOpt = blob.ReadSerializedString();
        // The C# `fullTypeName.Split(new[] { ", " },
        // StringSplitOptions.None)` -- every part (not a max-2 split, the
        // ResolveType shape).
        std::string fullTypeName = fullTypeNameOpt.value_or("");
        std::vector<std::string> nameParts;
        std::size_t start = 0;
        while (true) {
            std::size_t comma = fullTypeName.find(", ", start);
            if (comma == std::string::npos) {
                nameParts.push_back(fullTypeName.substr(start));
                break;
            }
            nameParts.push_back(fullTypeName.substr(start, comma - start));
            start = comma + 2;
        }
        if (nameParts.size() < 2 || nameParts[1] == currentAssemblyName) {
            output.Write("class ");
            output.Write(Escape(fullTypeName));
        } else {
            output.Write('[');
            output.Write(nameParts[1]);
            output.Write(']');
            output.Write(nameParts[0]);
        }
        output.Write(" = {");
        blob.ReadCompressedInteger();  // ?
        // The specification seems to be incorrect here, so I'm using the
        // logic from Cecil instead.
        int argCount = blob.ReadCompressedInteger();

        SecurityDeclarationDecoder provider(output_, AssemblyResolver(),
            module);
        Metadata::CustomAttributeDecoderT<SecurityDeclarationDecoder> decoder(
            module, provider, /*provideBoxingTypeInfo=*/true);
        auto arguments = decoder.DecodeNamedArguments(blob.data, blob.size,
            blob.pos, argCount);

        if (argCount > 0) {
            output.WriteLine();
            output.Indent();
        }

        for (const auto& argument : arguments) {
            switch (argument.Kind()) {
                case TypeSystem::CustomAttributeNamedArgumentKind::Field:
                    output.Write("field ");
                    break;
                case TypeSystem::CustomAttributeNamedArgumentKind::Property:
                    output.Write("property ");
                    break;
            }

            output.Write(argument.Type().Name
                    ? *argument.Type().Name
                    : PrimitiveTypeCodeToString(argument.Type().Code));
            output.Write(" " + Escape(argument.Name()) + " = ");

            WriteValue(output, argument.Type(), argument.Value());
            output.WriteLine();
        }

        if (argCount > 0) {
            output.Unindent();
        }

        output.Write('}');

        if (i + 1 < count)
            output.Write(',');
        output.WriteLine();
    }

    output.Unindent();
    output.Write("}");
    output.WriteLine();
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

// The C# `void WriteAttributes(MetadataFile module,
// CustomAttributeHandleCollection attributes)` (ReflectionDisassembler.cs
// lines 1851-1872): the ".custom" line per attribute. See the header for
// the porting decisions.
void ReflectionDisassembler::WriteAttributes(const Metadata::MetadataFile& module,
    const std::vector<std::uint32_t>& attributeTokens)
{
    for (std::uint32_t a : Process(module, attributeTokens,
             ProcessedEntityKind::CustomAttribute)) {
        output_.Write(".custom ");
        WriteMetadataToken(output_, module, a, a, /*spaceAfter=*/true,
            /*spaceBefore=*/false, ShowMetadataTokens(),
            ShowMetadataTokensInBase10());
        auto attr = module.GetCustomAttribute(a);
        if (!attr.has_value()) {
            // The C# metadata.GetCustomAttribute(handle) throws for an
            // out-of-range row -- loud rather than wrong.
            throw std::out_of_range("custom attribute handle out of range");
        }
        // The C# `attr.Constructor.WriteTo(module, output, default)` -- the
        // default generic context (an attribute constructor never carries
        // VAR/MVAR) at the default Signature syntax.
        IL::WriteTo(module, output_, Metadata::MetadataGenericContext::Nil(),
            attr->ConstructorToken);
        if (attr->ValueBlob.has_value()) {
            output_.Write(" = ");
            if (DecodeCustomAttributeBlobs) {
                WriteDecodedCustomAttributeBlob(module, *attr);
            } else {
                WriteBlob(attr->ValueBlob->data(), attr->ValueBlob->size());
            }
        }
        output_.WriteLine();
    }
}

// The C# `void WriteGenericParametersAndAttributes(MetadataFile module,
// MetadataGenericContext context, GenericParameterHandle handle)`
// (ReflectionDisassembler.cs lines 1175-1200): the ".param type" block over
// the generic parameter's own attributes and the ".param constraint" blocks
// over its constraint rows' attributes. See the header.
void ReflectionDisassembler::WriteGenericParametersAndAttributes(
    const Metadata::MetadataFile& module,
    const Metadata::MetadataGenericContext& context,
    std::uint32_t genericParameterToken)
{
    auto p = module.GetGenericParameterByToken(genericParameterToken);
    if (!p.has_value()) {
        // The C# metadata.GetGenericParameter(handle) throws for an invalid
        // handle -- loud rather than wrong.
        throw std::out_of_range("generic parameter handle out of range");
    }
    auto attributes = module.GetCustomAttributeTokens(genericParameterToken);
    if (!attributes.empty()) {
        output_.Write(".param type ");
        output_.Write(p->Name);
        output_.WriteLine();
        output_.Indent();
        WriteAttributes(module, attributes);
        output_.Unindent();
    }
    for (const auto& constraint : module.GetGenericParameterConstraints(
             genericParameterToken)) {
        auto constraintAttributes = module.GetCustomAttributeTokens(
            constraint.Token);
        if (constraintAttributes.empty())
            continue;
        output_.Write(".param constraint ");
        output_.Write(p->Name);
        output_.Write(", ");
        IL::WriteTo(module, output_, context, constraint.TypeToken,
            ILNameSyntax::TypeName);
        output_.WriteLine();
        output_.Indent();
        WriteAttributes(module, constraintAttributes);
        output_.Unindent();
    }
}

// The C# `void WriteParameterAttributes(MetadataFile module,
// ParameterHandle handle)` (ReflectionDisassembler.cs lines 1202-1218): the
// ".param [N]" line with its optional constant tail and attribute block.
// See the header.
void ReflectionDisassembler::WriteParameterAttributes(
    const Metadata::MetadataFile& module,
    const Metadata::ParameterInfo& parameter)
{
    auto constant = module.GetConstant(parameter.Token);
    auto attributes = module.GetCustomAttributeTokens(parameter.Token);
    // The C# `if (p.GetDefaultValue().IsNil && p.GetCustomAttributes().Count
    // == 0) return;` -- the fused GetConstant read's nullopt is the nil
    // ConstantHandle.
    if (!constant.has_value() && attributes.empty())
        return;
    output_.Write(".param [");
    output_.Write(std::to_string(parameter.SequenceNumber));
    output_.Write("]");
    if (constant.has_value()) {
        output_.Write(" = ");
        WriteConstant(*constant);
    }
    output_.WriteLine();
    output_.Indent();
    WriteAttributes(module, attributes);
    output_.Unindent();
}

// ---------------------------------------------------------------------------
// The method chain (ReflectionDisassembler.cs lines 153-170, 172-318, 354-389
// and 390-467, 1755-1802).
// ---------------------------------------------------------------------------

// The C# `public void DisassembleMethod(MetadataFile module,
// MethodDefinitionHandle handle)`: the ".method" reference, the header, and
// the body block. See the header for the porting decisions.
void ReflectionDisassembler::DisassembleMethod(Metadata::MetadataFile& module,
    std::uint32_t methodToken)
{
    // The C# `new MetadataGenericContext(handle, module)` -- the method
    // context (VAR names the method's declaring type's parameters, MVAR the
    // method's own).
    auto genericContext =
        Metadata::MetadataGenericContext::ForMethod(methodToken, module);
    // write method header
    output_.WriteReference(module, methodToken, ".method", "decompile",
        /*isDefinition=*/true);
    output_.Write(" ");
    DisassembleMethodHeaderInternal(module, methodToken, genericContext);
    DisassembleMethodBlock(module, methodToken, genericContext);
}

// The C# `public void DisassembleMethodHeader(MetadataFile module,
// MethodDefinitionHandle handle)`: the ".method" reference and the header,
// no body block.
void ReflectionDisassembler::DisassembleMethodHeader(Metadata::MetadataFile& module,
    std::uint32_t methodToken)
{
    auto genericContext =
        Metadata::MetadataGenericContext::ForMethod(methodToken, module);
    output_.WriteReference(module, methodToken, ".method", "decompile",
        /*isDefinition=*/true);
    output_.Write(" ");
    DisassembleMethodHeaderInternal(module, methodToken, genericContext);
}

// The C# `void DisassembleMethodHeaderInternal(MetadataFile module,
// MethodDefinitionHandle handle, MetadataGenericContext genericContext)`.
// See the header for the porting decisions.
void ReflectionDisassembler::DisassembleMethodHeaderInternal(
    Metadata::MetadataFile& module, std::uint32_t methodToken,
    const Metadata::MetadataGenericContext& genericContext)
{
    WriteMetadataToken(output_, module, methodToken, methodToken,
        /*spaceAfter=*/true, /*spaceBefore=*/false, ShowMetadataTokens(),
        ShowMetadataTokensInBase10());
    std::uint32_t attributes = module.GetMethodAttributes(methodToken);
    std::string name = module.GetMethodName(methodToken);

    //    .method public hidebysig  specialname
    //               instance default class [mscorlib]System.IO.TextWriter get_BaseWriter ()  cil managed
    //
    //emit flags
    WriteEnum(
        static_cast<MethodAttributes>(attributes
            & static_cast<std::uint32_t>(MethodAttributes::MemberAccessMask)),
        methodVisibility, output_);
    WriteFlags(static_cast<MethodAttributes>(
                    attributes & ~static_cast<std::uint32_t>(
                        MethodAttributes::MemberAccessMask)),
        methodAttributeFlags, output_);
    // The C# isCompilerControlled check: PrivateScope (the MemberAccessMask
    // zero value) has no table entry -- the header spells it separately
    // ("privatescope " here and the $PST name suffix below).
    bool isCompilerControlled =
        (attributes & static_cast<std::uint32_t>(
             MethodAttributes::MemberAccessMask))
        == static_cast<std::uint32_t>(MethodAttributes::PrivateScope);
    if (isCompilerControlled)
        output_.Write("privatescope ");

    if ((attributes & static_cast<std::uint32_t>(MethodAttributes::PinvokeImpl))
        != 0) {
        output_.Write("pinvokeimpl");
        auto info = module.GetMethodImport(methodToken);
        if (info.has_value() && info->ModuleRefToken != 0) {
            // The C# `metadata.GetModuleReference(info.Module).Name` -- the
            // raw ModuleRef Name, escaped into the quoted spelling.
            auto moduleRefName =
                module.GetModuleReferenceName(info->ModuleRefToken);
            output_.Write("(\"");
            output_.Write(EscapeString(moduleRefName.value_or("")));
            output_.Write("\"");

            // The C# `!info.Name.IsNil && metadata.GetString(info.Name) !=
            // metadata.GetString(methodDefinition.Name)` -- the alias is
            // rendered only when the imported name differs from the
            // declared one (compared raw, before escaping).
            if (info->Name.has_value() && *info->Name != name) {
                output_.Write(" as \"");
                output_.Write(EscapeString(*info->Name));
                output_.Write("\"");
            }

            auto importAttributes =
                static_cast<MethodImportAttributes>(info->Attributes);
            if ((importAttributes & MethodImportAttributes::ExactSpelling)
                == MethodImportAttributes::ExactSpelling) {
                output_.Write(" nomangle");
            }

            switch (static_cast<std::uint32_t>(
                importAttributes & MethodImportAttributes::CharSetMask)) {
                case static_cast<std::uint32_t>(MethodImportAttributes::CharSetAnsi):
                    output_.Write(" ansi");
                    break;
                case static_cast<std::uint32_t>(MethodImportAttributes::CharSetAuto):
                    output_.Write(" autochar");
                    break;
                case static_cast<std::uint32_t>(MethodImportAttributes::CharSetUnicode):
                    output_.Write(" unicode");
                    break;
            }

            if ((importAttributes & MethodImportAttributes::SetLastError)
                == MethodImportAttributes::SetLastError) {
                output_.Write(" lasterr");
            }

            switch (static_cast<std::uint32_t>(
                importAttributes & MethodImportAttributes::CallingConventionMask)) {
                case static_cast<std::uint32_t>(MethodImportAttributes::CallingConventionCDecl):
                    output_.Write(" cdecl");
                    break;
                case static_cast<std::uint32_t>(MethodImportAttributes::CallingConventionFastCall):
                    output_.Write(" fastcall");
                    break;
                case static_cast<std::uint32_t>(MethodImportAttributes::CallingConventionStdCall):
                    output_.Write(" stdcall");
                    break;
                case static_cast<std::uint32_t>(MethodImportAttributes::CallingConventionThisCall):
                    output_.Write(" thiscall");
                    break;
                case static_cast<std::uint32_t>(MethodImportAttributes::CallingConventionWinApi):
                    output_.Write(" winapi");
                    break;
            }

            output_.Write(')');
        }
        output_.Write(' ');
    }

    output_.WriteLine();
    output_.Indent();
    // The C# assigns `var declaringType = methodDefinition.GetDeclaringType()`
    // here and never reads it -- the block's close comment re-reads the
    // declaring type itself.
    std::optional<Metadata::MethodSignatureT> signature;
    // The C# `new DisassemblerSignatureTypeProvider(module, output)` is a
    // HEAP object whose delegates stay live past the try block -- the
    // signature's deferred parameter-type writers run in WriteParameters
    // BELOW it. The port must heap-allocate the provider for the same
    // reason: a stack local scoped to the try block would dangle (the
    // provider-outlives-writers contract; the writers die with the
    // signature at function exit, so the unique_ptr suffices).
    auto provider = std::make_unique<DisassemblerSignatureTypeProvider>(
        module, output_);
    try {
        auto blob = module.GetSignatureBlob(methodToken);
        if (!blob.has_value())
            throw std::logic_error("missing method signature blob");
        DisassemblerSignatureTypeProvider& providerRef = *provider;
        Metadata::SignatureTypeProviderDecoder decoder(providerRef, module);
        signature = decoder.DecodeMethodSignature(blob->data(), blob->size(),
            genericContext);
        if (signature->Header.HasExplicitThis) {
            output_.Write("instance explicit ");
        } else if (signature->Header.IsInstance()) {
            output_.Write("instance ");
        }

        //call convention
        // The port's MethodSignatureT carries the Metadata-namespace
        // convention enum; the callingConvention table is over the
        // TypeSystem one (the byte-backed mirrors of the same C# enum).
        WriteEnum(static_cast<TypeSystem::SignatureCallingConvention>(
                      signature->Header.CallingConvention),
            callingConvention, output_);

        //return type
        signature->ReturnType(ILNameSyntax::Signature);
    } catch (const std::exception&) {
        // The C# BadImageFormatException catch: whatever the decode managed
        // to write stays on the output, then the marker -- and the parameter
        // block below is skipped entirely.
        signature = std::nullopt;
        output_.Write("<bad signature>");
    }
    output_.Write(' ');

    auto parameters = module.GetParameters(methodToken);
    // The seq-0 return-value row may carry the RETURN type's marshalling
    // descriptor (the C# firstParam.GetMarshallingDescriptor() -- rendered
    // ahead of the method name, never inside the parameter list).
    if (!parameters.empty()) {
        const auto& firstParam = parameters.front();
        if (firstParam.SequenceNumber == 0
            && firstParam.MarshallingDescriptor.has_value()) {
            WriteMarshalInfo(firstParam.MarshallingDescriptor->data(),
                firstParam.MarshallingDescriptor->size());
        }
    }

    if (isCompilerControlled) {
        // The C# `name + "$PST" + MetadataTokens.GetToken(handle).ToString("X8")`
        // -- the ILDasm compiler-controlled spelling, all of it escaped as
        // one identifier.
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%08X", methodToken);
        output_.Write(Escape(name + "$PST" + buf));
    } else {
        output_.Write(Escape(name));
    }

    WriteTypeParameters(output_, module, genericContext,
        module.GetGenericParameters(methodToken));

    //( params )
    output_.Write(" (");
    if (signature.has_value() && !signature->ParameterTypes.empty()) {
        output_.WriteLine();
        output_.Indent();
        WriteParameters(parameters, *signature);
        output_.Unindent();
    }
    output_.Write(") ");
    //cil managed
    std::uint32_t implAttributes = module.GetMethodImplAttributes(methodToken);
    WriteEnum(
        static_cast<MethodImplAttributes>(implAttributes
            & static_cast<std::uint32_t>(MethodImplAttributes::CodeTypeMask)),
        methodCodeType, output_);
    if ((implAttributes
            & static_cast<std::uint32_t>(MethodImplAttributes::ManagedMask))
        == static_cast<std::uint32_t>(MethodImplAttributes::Managed)) {
        output_.Write("managed ");
    } else {
        output_.Write("unmanaged ");
    }
    WriteFlags(static_cast<MethodImplAttributes>(
                    implAttributes
                    & ~(static_cast<std::uint32_t>(
                            MethodImplAttributes::CodeTypeMask)
                        | static_cast<std::uint32_t>(
                            MethodImplAttributes::ManagedMask))),
        methodImpl, output_);

    output_.Unindent();
}

// The C# `void DisassembleMethodBlock(MetadataFile module,
// MethodDefinitionHandle handle, MetadataGenericContext genericContext)`.
// See the header for the porting decisions.
void ReflectionDisassembler::DisassembleMethodBlock(Metadata::MetadataFile& module,
    std::uint32_t methodToken,
    const Metadata::MetadataGenericContext& genericContext)
{
    OpenBlock(/*defaultCollapsed=*/isInType_);
    WriteAttributes(module, module.GetCustomAttributeTokens(methodToken));
    for (const auto& impl : module.GetMethodImplementations(methodToken)) {
        output_.Write(".override method ");
        // The C# `impl.MethodDeclaration.WriteTo(module, output,
        // genericContext)` -- the default Signature syntax.
        IL::WriteTo(module, output_, genericContext,
            impl.MethodDeclarationToken);
        output_.WriteLine();
    }

    for (const auto& p : module.GetGenericParameters(methodToken)) {
        WriteGenericParametersAndAttributes(module, genericContext, p.Token);
    }
    for (const auto& p : module.GetParameters(methodToken)) {
        WriteParameterAttributes(module, p);
    }
    WriteSecurityDeclarations(module,
        module.GetDeclarativeSecurityAttributes(methodToken));

    // The C# `methodDefinition.HasBody()` -- RelativeVirtualAddress != 0
    // (0 for abstract/extern/pinvoke-only methods).
    if (module.GetMethodRVA(methodToken) != 0) {
        methodBodyDisassembler_->Disassemble(module, methodToken);
    }
    // The C# close comment: the declaring type's short NAME, not the full
    // name (the C# `declaringType.Name`), joined over the escaped method
    // name.
    auto declaringTypeToken = module.GetMethodDeclaringTypeToken(methodToken);
    auto declaringTypeName =
        module.GetTypeDefNameInfo(declaringTypeToken).value_or(
            Metadata::TypeDefNameInfo{}).Name;
    std::string comment = "end of method " + Escape(declaringTypeName)
        + "::" + Escape(module.GetMethodName(methodToken));
    CloseBlock(comment.c_str());
}

// The C# `void WriteSecurityDeclarations(MetadataFile module,
// DeclarativeSecurityAttributeHandleCollection secDeclProvider)`. See the
// header for the porting decisions.
void ReflectionDisassembler::WriteSecurityDeclarations(Metadata::MetadataFile& module,
    const std::vector<Metadata::MetadataFile::DeclarativeSecurityInfo>&
        securityDeclarations)
{
    if (securityDeclarations.empty())
        return;
    for (const auto& secdecl : securityDeclarations) {
        output_.Write(".permissionset ");
        // The C# switch over `(ushort)secdecl.Action` with the fifteen
        // DeclarativeSecurityAction spellings; the default arm is the C# enum
        // ToString ("None" for 0, the decimal for unnamed values).
        switch (secdecl.Action) {
            case 1:  // DeclarativeSecurityAction.Request
                output_.Write("request");
                break;
            case 2:  // DeclarativeSecurityAction.Demand
                output_.Write("demand");
                break;
            case 3:  // DeclarativeSecurityAction.Assert
                output_.Write("assert");
                break;
            case 4:  // DeclarativeSecurityAction.Deny
                output_.Write("deny");
                break;
            case 5:  // DeclarativeSecurityAction.PermitOnly
                output_.Write("permitonly");
                break;
            case 6:  // DeclarativeSecurityAction.LinkDemand
                output_.Write("linkcheck");
                break;
            case 7:  // DeclarativeSecurityAction.InheritDemand
                output_.Write("inheritcheck");
                break;
            case 8:  // DeclarativeSecurityAction.RequestMinimum
                output_.Write("reqmin");
                break;
            case 9:  // DeclarativeSecurityAction.RequestOptional
                output_.Write("reqopt");
                break;
            case 10:  // DeclarativeSecurityAction.RequestRefuse
                output_.Write("reqrefuse");
                break;
            case 11:  // DeclarativeSecurityAction.PreJitGrant
                output_.Write("prejitgrant");
                break;
            case 12:  // DeclarativeSecurityAction.PreJitDeny
                output_.Write("prejitdeny");
                break;
            case 13:  // DeclarativeSecurityAction.NonCasDemand
                output_.Write("noncasdemand");
                break;
            case 14:  // DeclarativeSecurityAction.NonCasLinkDemand
                output_.Write("noncaslinkdemand");
                break;
            case 15:  // DeclarativeSecurityAction.NonCasInheritance
                output_.Write("noncasinheritance");
                break;
            default:
                // The C# default arm is the enum ToString: "None" for 0, the
                // decimal for unnamed values (the enum is short-backed, so
                // a raw value above 0x7FFF renders as the negative int16).
                output_.Write(secdecl.Action == 0 ? "None"
                    : std::to_string(
                        static_cast<std::int16_t>(secdecl.Action)));
                break;
        }
        const std::uint8_t* blobData = secdecl.PermissionSet.data();
        std::size_t blobSize = secdecl.PermissionSet.size();
        Metadata::BlobReader blob{blobData, blobSize, 0};
        if (AssemblyResolver() == nullptr) {
            // The C# AssemblyResolver == null path: the raw blob dump (the
            // CLI's shape -- the CLI never sets a resolver).
            output_.Write(" = ");
            WriteBlob(blobData, blobSize);
            output_.WriteLine();
        } else if (static_cast<char>(blob.ReadByte()) != '.') {
            // The C# `else if ((char)blob.ReadByte() != '.')`: the
            // indented "bytearray" + raw dump (an XML-form permission set
            // -- the pre-.NET-2.0 form). The marker byte is read OUTSIDE
            // the try below: an empty blob with a resolver set throws the
            // BadImageFormatException family out of this method, exactly
            // as in the C#.
            output_.WriteLine();
            output_.Indent();
            output_.Write("bytearray");
            WriteBlob(blobData, blobSize);
            output_.WriteLine();
            output_.Unindent();
        } else {
            Output::TextOutputWithRollback outputWithRollback(output_);
            try {
                // The C# passes the blob reader positioned AFTER the '.'
                // marker (the gate's ReadByte advanced it; the by-value
                // copy starts at the entry count).
                TryDecodeSecurityDeclaration(outputWithRollback, blob,
                    module);
                outputWithRollback.Commit();
            } catch (const std::invalid_argument&) {
                // The C# `catch (Exception ex) when (ex is
                // BadImageFormatException || ex is
                // EnumUnderlyingTypeResolveException)`: the raw dump.
                output_.Write(" = ");
                WriteBlob(blobData, blobSize);
                output_.WriteLine();
            } catch (const Metadata::EnumUnderlyingTypeResolveException&) {
                output_.Write(" = ");
                WriteBlob(blobData, blobSize);
                output_.WriteLine();
            }
        }
    }
}

// The C# `void WriteTypeParameters(ITextOutput output, MetadataFile module,
// MetadataGenericContext context, GenericParameterHandleCollection p)`.
// See the header for the porting decisions.
void ReflectionDisassembler::WriteTypeParameters(Output::ITextOutput& output,
    const Metadata::MetadataFile& module,
    const Metadata::MetadataGenericContext& context,
    const std::vector<Metadata::GenericParameterInfo>& parameters)
{
    if (parameters.empty())
        return;
    output.Write('<');
    for (std::size_t i = 0; i < parameters.size(); i++) {
        if (i > 0)
            output.Write(", ");
        const auto& gp = parameters[i];
        // The raw Flags column as the GenericParameterAttributes bits (the
        // modern BCL enum carries the raw ECMA values -- see
        // ReflectionAttributes.hpp).
        auto attributes = static_cast<GenericParameterAttributes>(gp.Flags);
        if ((attributes & GenericParameterAttributes::ReferenceTypeConstraint)
            == GenericParameterAttributes::ReferenceTypeConstraint) {
            output.Write("class ");
        } else if ((attributes
                & GenericParameterAttributes::NotNullableValueTypeConstraint)
            == GenericParameterAttributes::NotNullableValueTypeConstraint) {
            output.Write("valuetype ");
        }
        if ((attributes & GenericParameterAttributes::AllowByRefLike)
            == GenericParameterAttributes::AllowByRefLike) {
            output.Write("byreflike ");
        }
        if ((attributes & GenericParameterAttributes::DefaultConstructorConstraint)
            == GenericParameterAttributes::DefaultConstructorConstraint) {
            output.Write(".ctor ");
        }
        auto constraints = module.GetGenericParameterConstraints(gp.Token);
        if (!constraints.empty()) {
            output.Write('(');
            for (std::size_t j = 0; j < constraints.size(); j++) {
                if (j > 0)
                    output.Write(", ");
                // The C# `constraint.Type.WriteTo(module, output, context,
                // ILNameSyntax.TypeName)`.
                IL::WriteTo(module, output, context,
                    constraints[j].TypeToken, ILNameSyntax::TypeName);
            }
            output.Write(") ");
        }
        if ((attributes & GenericParameterAttributes::Contravariant)
            == GenericParameterAttributes::Contravariant) {
            output.Write('-');
        } else if ((attributes & GenericParameterAttributes::Covariant)
            == GenericParameterAttributes::Covariant) {
            output.Write('+');
        }
        output.Write(Escape(gp.Name));
    }
    output.Write('>');
}

// ---------------------------------------------------------------------------
// The field member renderer (ReflectionDisassembler.cs lines 1270-1435:
// DisassembleField / DisassembleFieldHeader / DisassembleFieldHeaderInternal
// / GetRVASectionPrefix).
// ---------------------------------------------------------------------------

// The C# `public void DisassembleField(MetadataFile module,
// FieldDefinitionHandle handle)` (lines 1288-1343). See the header for the
// porting decisions.
void ReflectionDisassembler::DisassembleField(Metadata::MetadataFile& module,
    std::uint32_t fieldToken)
{
    // The header ends mid-line; this WriteLine terminates it (the constant
    // tail or the flags leave no newline of their own).
    char sectionPrefix = DisassembleFieldHeaderInternal(module, fieldToken);
    output_.WriteLine();

    // The C# `attributes.Count > 0` fold pair around the attribute lines --
    // no braces or extra indent: a field has no body block.
    auto attributeTokens = module.GetCustomAttributeTokens(fieldToken);
    if (!attributeTokens.empty()) {
        output_.MarkFoldStart();
        WriteAttributes(module, attributeTokens);
        output_.MarkFoldEnd();
    }

    // The C# `fieldDefinition.HasFlag(FieldAttributes.HasFieldRVA)`.
    constexpr std::uint32_t kHasFieldRVA =
        static_cast<std::uint32_t>(FieldAttributes::HasFieldRVA);
    std::uint32_t attributes = module.GetFieldAttributes(fieldToken);
    if ((attributes & kHasFieldRVA) != 0) {
        std::uint32_t rva = module.GetFieldRVA(fieldToken);
        int sectionIndex = module.GetContainingSectionIndex(rva);
        if (sectionIndex < 0) {
            // The C# $"// RVA {rva:X8} invalid (not in any section)" --
            // composed as a string: the C# interpolation is unbounded, so no
            // fixed snprintf buffer can size it by inspection.
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%08X", rva);
            Output::WriteLine(output_,
                "// RVA " + std::string(buf) + " invalid (not in any section)");
        } else {
            std::vector<std::uint8_t> initVal;
            try {
                initVal = module.GetFieldInitialValue(fieldToken);
            } catch (const std::exception& ex) {
                // The C# `catch (BadImageFormatException ex)`: the
                // failed-read comment line (the exact message), composed as
                // a string: the exception text is unbounded.
                initVal.clear();
                char prefix[16];
                std::snprintf(prefix, sizeof(prefix), "%c_%08X",
                    sectionPrefix, rva);
                Output::WriteLine(output_,
                    "// .data " + std::string(prefix) + " = " + ex.what());
            }
            if (!initVal.empty()) {
                // The C# `module.SectionHeaders[sectionIndex].Name` walk.
                auto sectionName = module.GetSectionName(sectionIndex);
                output_.Write(".data ");
                if (sectionName == ".text") {
                    output_.Write("cil ");
                } else if (sectionName == ".tls") {
                    output_.Write("tls ");
                } else if (sectionName != ".data") {
                    // The C# `sectionHeader.Name is not (null or ".data")`:
                    // the name is never null off a PE header, so this is
                    // every other section.
                    output_.Write("/* " + sectionName + " */ ");
                }
                // The C# $"{sectionPrefix}_{rva:X8} = bytearray ".
                char buf[24];
                std::snprintf(buf, sizeof(buf), "%c_%08X = bytearray ",
                    sectionPrefix, rva);
                output_.Write(buf);
                WriteBlob(initVal.data(), initVal.size());
                output_.WriteLine();
            }
        }
    }
}

// The C# `public void DisassembleFieldHeader(MetadataFile module,
// FieldDefinitionHandle handle)` (lines 1346-1351).
void ReflectionDisassembler::DisassembleFieldHeader(
    Metadata::MetadataFile& module, std::uint32_t fieldToken)
{
    DisassembleFieldHeaderInternal(module, fieldToken);
}

// The C# `private char DisassembleFieldHeaderInternal(MetadataFile module,
// FieldDefinitionHandle handle, MetadataReader metadata, FieldDefinition
// fieldDefinition)` (lines 1353-1417). See the header for the porting
// decisions.
char ReflectionDisassembler::DisassembleFieldHeaderInternal(
    Metadata::MetadataFile& module, std::uint32_t fieldToken)
{
    output_.WriteReference(module, fieldToken, ".field", "decompile",
        /*isDefinition=*/true);
    WriteMetadataToken(output_, module, fieldToken, fieldToken,
        /*spaceAfter=*/true, /*spaceBefore=*/true, ShowMetadataTokens(),
        ShowMetadataTokensInBase10());
    std::uint32_t attributes = module.GetFieldAttributes(fieldToken);

    // The C# `int offset = fieldDefinition.GetOffset(); if (offset > -1)`.
    std::int32_t offset = module.GetFieldOffset(fieldToken);
    if (offset > -1) {
        output_.Write("[" + std::to_string(offset) + "] ");
    }

    //emit flags -- the visibility WriteEnum + the attribute WriteFlags split;
    // the HasDefault/HasFieldMarshal/HasFieldRVA bits are masked out (they
    // render through the constant/marshal/at-<rva> arms below).
    WriteEnum(
        static_cast<FieldAttributes>(attributes
            & static_cast<std::uint32_t>(FieldAttributes::FieldAccessMask)),
        fieldVisibility, output_);
    constexpr std::uint32_t kHasXAttributes =
        static_cast<std::uint32_t>(FieldAttributes::HasDefault)
        | static_cast<std::uint32_t>(FieldAttributes::HasFieldMarshal)
        | static_cast<std::uint32_t>(FieldAttributes::HasFieldRVA);
    WriteFlags(static_cast<FieldAttributes>(
                    attributes
                    & ~(static_cast<std::uint32_t>(
                            FieldAttributes::FieldAccessMask)
                        | kHasXAttributes)),
        fieldAttributes, output_);

    // The C# `fieldDefinition.DecodeSignature(new
    // DisassemblerSignatureTypeProvider(module, output), new
    // MetadataGenericContext(fieldDefinition.GetDeclaringType(), module))`:
    // the field signature's kind nibble must be Field (0x06) -- the SRM
    // DecodeFieldSignature header check (the IL field-arm convention) --
    // then one full type decode; the VAR (!N) context scopes to the
    // declaring TypeDef. A malformed blob throws (no catch in the field
    // header -- the C# BadImageFormatException escapes).
    auto blob = module.GetSignatureBlob(fieldToken);
    if (!blob || blob->empty() || ((*blob)[0] & 0x0F) != 0x06)
        throw std::logic_error("field signature");
    DisassemblerSignatureTypeProvider provider(module, output_);
    Metadata::SignatureTypeProviderDecoder decoder(provider, module);
    Metadata::SignatureTypeWriter signature = decoder.DecodeType(
        blob->data() + 1, blob->size() - 1,
        Metadata::MetadataGenericContext::ForType(
            module.GetFieldDeclaringTypeToken(fieldToken), module));

    // The marshalling descriptor renders BETWEEN the flags and the type
    // ("marshal(lpwstr) string Name").
    auto marshallingDescriptor = module.GetFieldMarshallingDescriptor(fieldToken);
    if (marshallingDescriptor.has_value()) {
        WriteMarshalInfo(marshallingDescriptor->data(),
            marshallingDescriptor->size());
    }

    signature(ILNameSyntax::Signature);
    output_.Write(' ');
    output_.Write(Escape(module.GetFieldName(fieldToken)));

    char sectionPrefix = 'D';
    if ((attributes & static_cast<std::uint32_t>(FieldAttributes::HasFieldRVA))
        != 0) {
        std::uint32_t rva = module.GetFieldRVA(fieldToken);
        sectionPrefix = GetRVASectionPrefix(module, rva);
        // The C# `output.Write(" at {1}_{0:X8}", rva, sectionPrefix)`.
        char buf[24];
        std::snprintf(buf, sizeof(buf), " at %c_%08X", sectionPrefix, rva);
        output_.Write(buf);
    }

    // The C# `defaultValue = fieldDefinition.GetDefaultValue()` -- the
    // Constant-table row parented by the field (the fused GetConstant).
    auto defaultValue = module.GetConstant(fieldToken);
    if (defaultValue.has_value()) {
        output_.Write(" = ");
        WriteConstant(*defaultValue);
    }

    return sectionPrefix;
}

// The C# `private char GetRVASectionPrefix(MetadataFile module, int rva)`
// (lines 1419-1435). See the header for the porting decisions.
char ReflectionDisassembler::GetRVASectionPrefix(
    const Metadata::MetadataFile& module, std::uint32_t rva)
{
    // The C# `module is not PEFile peFile` NotSupportedException arm: the
    // port's MetadataFile is always the PE-backed file (the winmd reader).
    int sectionIndex = module.GetContainingSectionIndex(rva);
    if (sectionIndex < 0)
        return 'D';
    auto name = module.GetSectionName(sectionIndex);
    if (name == ".tls")
        return 'T';
    if (name == ".text")
        return 'I';
    return 'D';
}

// ---------------------------------------------------------------------------
// The property member renderer (ReflectionDisassembler.cs lines
// 1424-1488).
// ---------------------------------------------------------------------------

// The C# `public void DisassembleProperty(MetadataFile module,
// PropertyDefinitionHandle property)`. See the header for the porting
// decisions.
void ReflectionDisassembler::DisassembleProperty(
    Metadata::MetadataFile& module, std::uint32_t propertyToken)
{
    auto accessors = DisassemblePropertyHeaderInternal(module, propertyToken);

    // The C# `OpenBlock(false)` -- hard-coded false (a property block never
    // collapses, unlike a method's isInType folding).
    OpenBlock(/*defaultCollapsed=*/false);
    WriteAttributes(module, module.GetCustomAttributeTokens(propertyToken));
    WriteNestedMethod(".get", module, accessors.GetterToken);
    WriteNestedMethod(".set", module, accessors.SetterToken);
    for (std::uint32_t method : accessors.OtherTokens) {
        WriteNestedMethod(".other", module, method);
    }
    CloseBlock();
}

// The C# `public void DisassemblePropertyHeader(MetadataFile module,
// PropertyDefinitionHandle property)`.
void ReflectionDisassembler::DisassemblePropertyHeader(
    Metadata::MetadataFile& module, std::uint32_t propertyToken)
{
    DisassemblePropertyHeaderInternal(module, propertyToken);
}

// The C# `private PropertyAccessors DisassemblePropertyHeaderInternal(...)`.
// See the header for the porting decisions.
Metadata::MetadataFile::PropertyAccessorsInfo
ReflectionDisassembler::DisassemblePropertyHeaderInternal(
    Metadata::MetadataFile& module, std::uint32_t propertyToken)
{
    output_.WriteReference(module, propertyToken, ".property", "decompile",
        /*isDefinition=*/true);
    WriteMetadataToken(output_, module, propertyToken, propertyToken,
        /*spaceAfter=*/true, /*spaceBefore=*/true, ShowMetadataTokens(),
        ShowMetadataTokensInBase10());
    std::uint32_t attributes = module.GetPropertyAttributes(propertyToken);
    WriteFlags(static_cast<PropertyAttributes>(attributes),
        propertyAttributes, output_);

    auto accessors = module.GetPropertyAccessors(propertyToken);
    // The C# `accessors.GetAny()` (the ILSpy SRMExtensions): the getter,
    // else the setter. A property with neither (all-nil accessors) resolves
    // its declaring type through the nil row -- the port's graceful 0 (the
    // C# GetMethodDefinition(nil) walks the nil row and yields the nil
    // declaring type, not an exception).
    std::uint32_t anyToken = accessors.GetterToken != 0
        ? accessors.GetterToken
        : accessors.SetterToken;
    std::uint32_t declaringTypeToken =
        module.GetMethodDeclaringTypeToken(anyToken);

    // The C# `propertyDefinition.DecodeSignature(new
    // DisassemblerSignatureTypeProvider(module, output), new
    // MetadataGenericContext(declaringType, module))`: the property blob
    // decodes as a METHOD signature over its 0x08/0x28 header (the SRM
    // PropertyDefinition.DecodeSignature calls DecodeMethodSignature -- no
    // kind check), with the VAR context scoped to the declaring TypeDef.
    // The provider is a local and the deferred type writers run entirely
    // within this scope (the provider-outlives-writers contract). A
    // malformed blob throws (the property header has no catch -- the C#
    // BadImageFormatException escapes to the caller).
    auto blob = module.GetSignatureBlob(propertyToken);
    if (!blob.has_value())
        throw std::logic_error("missing property signature blob");
    DisassemblerSignatureTypeProvider provider(module, output_);
    Metadata::SignatureTypeProviderDecoder decoder(provider, module);
    auto signature = decoder.DecodeMethodSignature(blob->data(), blob->size(),
        Metadata::MetadataGenericContext::ForType(declaringTypeToken, module));

    if (signature.Header.IsInstance())
        output_.Write("instance ");
    signature.ReturnType(ILNameSyntax::Signature);
    output_.Write(' ');
    output_.Write(Escape(module.GetPropertyName(propertyToken)));

    output_.Write('(');
    if (!signature.ParameterTypes.empty()) {
        // The C# accessor's Param rows sliced to `parametersCount`
        // (`count - 1` when there is no getter -- the setter's trailing
        // value parameter drops); Take(count) clamps above the row count
        // and throws below zero (the port's std::out_of_range for the C#
        // Enumerable.Take ArgumentOutOfRangeException).
        auto parameters = module.GetParameters(anyToken);
        int parametersCount = accessors.GetterToken == 0
            ? static_cast<int>(parameters.size()) - 1
            : static_cast<int>(parameters.size());
        if (parametersCount < 0)
            throw std::out_of_range("parameter count");
        auto takeEnd = parameters.begin()
            + std::min<std::size_t>(parametersCount, parameters.size());
        std::vector<Metadata::ParameterInfo> sliced(parameters.begin(),
            takeEnd);

        output_.WriteLine();
        output_.Indent();
        WriteParameters(sliced, signature);
        output_.Unindent();
    }
    output_.Write(')');
    return accessors;
}

// The C# `void WriteNestedMethod(string keyword, MetadataFile module,
// MethodDefinitionHandle method)`. See the header for the porting
// decisions.
void ReflectionDisassembler::WriteNestedMethod(const char* keyword,
    Metadata::MetadataFile& module, std::uint32_t methodToken)
{
    // The C# `if (method.IsNil) return;` -- nothing at all renders.
    if (methodToken == 0)
        return;
    output_.Write(keyword);
    output_.Write(' ');
    // The C# `((EntityHandle)method).WriteTo(module, output, default)` --
    // the default generic context and the default Signature syntax.
    IL::WriteTo(module, output_, Metadata::MetadataGenericContext::Nil(),
        methodToken);
    output_.WriteLine();
}

// ---------------------------------------------------------------------------
// The event member renderer (ReflectionDisassembler.cs lines
// 1497-1562).
// ---------------------------------------------------------------------------

// The C# `public void DisassembleEvent(MetadataFile module,
// EventDefinitionHandle handle)`. See the header for the porting
// decisions.
void ReflectionDisassembler::DisassembleEvent(Metadata::MetadataFile& module,
    std::uint32_t eventToken)
{
    auto accessors = module.GetEventAccessors(eventToken);
    DisassembleEventHeaderInternal(module, eventToken, accessors);

    // The C# `OpenBlock(false)` -- hard-coded false (like the property
    // block; only methods take the isInType folding).
    OpenBlock(/*defaultCollapsed=*/false);
    WriteAttributes(module, module.GetCustomAttributeTokens(eventToken));
    WriteNestedMethod(".addon", module, accessors.AdderToken);
    WriteNestedMethod(".removeon", module, accessors.RemoverToken);
    WriteNestedMethod(".fire", module, accessors.RaiserToken);
    for (std::uint32_t method : accessors.OtherTokens) {
        WriteNestedMethod(".other", module, method);
    }
    CloseBlock();
}

// The C# `public void DisassembleEventHeader(MetadataFile module,
// EventDefinitionHandle handle)`.
void ReflectionDisassembler::DisassembleEventHeader(
    Metadata::MetadataFile& module, std::uint32_t eventToken)
{
    auto accessors = module.GetEventAccessors(eventToken);
    DisassembleEventHeaderInternal(module, eventToken, accessors);
}

// The C# `private void DisassembleEventHeaderInternal(...)`. See the
// header for the porting decisions.
void ReflectionDisassembler::DisassembleEventHeaderInternal(
    Metadata::MetadataFile& module, std::uint32_t eventToken,
    const Metadata::MetadataFile::EventAccessorsInfo& accessors)
{
    // The C# declaringType: the adder's, else the remover's, else the
    // raiser's (nil rows walk the nil row -- the graceful C# path; the
    // value only feeds the TypeSpec arm's generic context).
    std::uint32_t accessorForType = accessors.AdderToken != 0
        ? accessors.AdderToken
        : (accessors.RemoverToken != 0 ? accessors.RemoverToken
                                       : accessors.RaiserToken);
    std::uint32_t declaringTypeToken =
        module.GetMethodDeclaringTypeToken(accessorForType);

    output_.WriteReference(module, eventToken, ".event", "decompile",
        /*isDefinition=*/true);
    WriteMetadataToken(output_, module, eventToken, eventToken,
        /*spaceAfter=*/true, /*spaceBefore=*/true, ShowMetadataTokens(),
        ShowMetadataTokensInBase10());
    std::uint32_t attributes = module.GetEventAttributes(eventToken);
    WriteFlags(static_cast<EventAttributes>(attributes), eventAttributes,
        output_);

    // The C# `switch (eventDefinition.Type.Kind)`: the delegate type
    // rendered directly through the provider -- the rawTypeKind argument
    // is the C# 0 (no class/valuetype prefix input), and the TypeSpec arm
    // carries the declaring type's generic context. A nil or unknown kind
    // is the C# BadImageFormatException (the port's std::out_of_range).
    std::uint32_t typeToken = module.GetEventTypeToken(eventToken);
    DisassemblerSignatureTypeProvider provider(module, output_);
    Metadata::SignatureTypeWriter signature;
    switch (typeToken >> 24) {
        case 0x02:  // HandleKind.TypeDefinition
            signature = provider.GetTypeFromDefinition(typeToken, 0);
            break;
        case 0x01:  // HandleKind.TypeReference
            signature = provider.GetTypeFromReference(typeToken, 0);
            break;
        case 0x1B:  // HandleKind.TypeSpecification
            signature = provider.GetTypeFromSpecification(typeToken, 0,
                Metadata::MetadataGenericContext::ForType(declaringTypeToken,
                    module));
            break;
        default:
            throw std::out_of_range(
                "Expected a TypeDef, TypeRef or TypeSpec handle!");
    }
    signature(ILNameSyntax::TypeName);
    output_.Write(' ');
    output_.Write(Escape(module.GetEventName(eventToken)));
}

// The C# `public void DisassembleType(MetadataFile module,
// TypeDefinitionHandle type)`. See the header for the porting decisions.
void ReflectionDisassembler::DisassembleType(Metadata::MetadataFile& module,
    std::uint32_t typeToken)
{
    auto genericContext = Metadata::MetadataGenericContext::ForType(
        typeToken, module);

    DisassembleTypeHeaderInternal(module, typeToken, genericContext);

    // The C# `Process(module, typeDefinition.GetInterfaceImplementations())`
    // -- the InterfaceImpl ROW tokens (the .interfaceimpl blocks below
    // re-read each row).
    std::vector<std::uint32_t> interfaces;
    for (const auto& impl : module.GetInterfaceImplementations(typeToken))
        interfaces.push_back(impl.Token);
    interfaces = Process(module, interfaces,
        ProcessedEntityKind::InterfaceImplementation);
    if (!interfaces.empty()) {
        output_.Indent();
        bool first = true;
        for (std::uint32_t i : interfaces) {
            if (!first)
                Output::WriteLine(output_, ",");
            if (first)
                output_.Write("implements ");
            else
                output_.Write("           ");
            first = false;
            auto iface = module.GetInterfaceImplementation(i);
            // The C# `iface.Interface.WriteTo(module, output,
            // genericContext, ILNameSyntax.TypeName)`.
            IL::WriteTo(module, output_, genericContext, iface->InterfaceToken,
                ILNameSyntax::TypeName);
        }
        output_.WriteLine();
        output_.Unindent();
    }

    Output::WriteLine(output_, "{");
    output_.Indent();
    bool oldIsInType = isInType_;
    isInType_ = true;
    WriteAttributes(module, module.GetCustomAttributeTokens(typeToken));
    WriteSecurityDeclarations(module,
        module.GetDeclarativeSecurityAttributes(typeToken));
    for (const auto& tp : module.GetGenericParameters(typeToken)) {
        WriteGenericParametersAndAttributes(module, genericContext, tp.Token);
    }
    auto layout = module.GetTypeLayout(typeToken);
    if (!layout.IsDefault()) {
        Output::WriteLine(output_,
            ".pack " + std::to_string(layout.PackingSize));
        Output::WriteLine(output_,
            ".size " + std::to_string(layout.ClassSize));
        output_.WriteLine();
    }
    for (std::uint32_t ifaceHandle : interfaces) {
        auto iface = module.GetInterfaceImplementation(ifaceHandle);
        auto customAttributes = module.GetCustomAttributeTokens(
            iface->Token);
        if (!customAttributes.empty()) {
            output_.Write(".interfaceimpl type ");
            IL::WriteTo(module, output_, genericContext,
                iface->InterfaceToken, ILNameSyntax::TypeName);
            output_.WriteLine();
            output_.Indent();
            WriteAttributes(module, customAttributes);
            output_.Unindent();
            output_.WriteLine();
        }
    }
    auto nestedTypes = Process(module, module.GetNestedTypes(typeToken),
        ProcessedEntityKind::TypeDefinition);
    if (!nestedTypes.empty()) {
        Output::WriteLine(output_, "// Nested Types");
        for (std::uint32_t nestedType : nestedTypes) {
            DisassembleType(module, nestedType);
            output_.WriteLine();
        }
        output_.WriteLine();
    }
    std::vector<std::uint32_t> fields;
    for (const auto& fd : module.GetFields(typeToken))
        fields.push_back(fd.Token);
    fields = Process(module, fields, ProcessedEntityKind::FieldDefinition);
    if (!fields.empty()) {
        Output::WriteLine(output_, "// Fields");
        for (std::uint32_t field : fields) {
            DisassembleField(module, field);
        }
        output_.WriteLine();
    }
    std::vector<std::uint32_t> methods;
    for (const auto& m : module.GetMethods(typeToken))
        methods.push_back(m.Token);
    methods = Process(module, methods, ProcessedEntityKind::MethodDefinition);
    if (!methods.empty()) {
        Output::WriteLine(output_, "// Methods");
        for (std::uint32_t m : methods) {
            DisassembleMethod(module, m);
            output_.WriteLine();
        }
    }
    std::vector<std::uint32_t> events;
    for (const auto& ev : module.GetEvents(typeToken))
        events.push_back(ev.Token);
    events = Process(module, events, ProcessedEntityKind::EventDefinition);
    if (!events.empty()) {
        Output::WriteLine(output_, "// Events");
        for (std::uint32_t ev : events) {
            DisassembleEvent(module, ev);
            output_.WriteLine();
        }
        output_.WriteLine();
    }
    std::vector<std::uint32_t> properties;
    for (const auto& prop : module.GetProperties(typeToken))
        properties.push_back(prop.Token);
    properties = Process(module, properties,
        ProcessedEntityKind::PropertyDefinition);
    if (!properties.empty()) {
        Output::WriteLine(output_, "// Properties");
        for (std::uint32_t prop : properties) {
            DisassembleProperty(module, prop);
        }
        output_.WriteLine();
    }
    // The C# close comment: the nested type's short NAME, else the full
    // type name (the FullTypeName.ToString == ReflectionName).
    std::string name;
    auto nameInfo = module.GetTypeDefNameInfo(typeToken);
    if (nameInfo && nameInfo->DeclaringTypeToken != 0)
        name = nameInfo->Name;
    else
        name = Metadata::GetFullTypeNameFromDefinition(module, typeToken)
                   .ReflectionName();
    CloseBlock(("end of class " + name).c_str());
    isInType_ = oldIsInType;
}

// The C# `public void DisassembleTypeHeader(MetadataFile module,
// TypeDefinitionHandle type)`.
void ReflectionDisassembler::DisassembleTypeHeader(
    Metadata::MetadataFile& module, std::uint32_t typeToken)
{
    DisassembleTypeHeaderInternal(module, typeToken,
        Metadata::MetadataGenericContext::ForType(typeToken, module));
}

// The C# `private void DisassembleTypeHeaderInternal(...)`. See the
// header for the porting decisions.
void ReflectionDisassembler::DisassembleTypeHeaderInternal(
    Metadata::MetadataFile& module, std::uint32_t typeToken,
    const Metadata::MetadataGenericContext& genericContext)
{
    output_.WriteReference(module, typeToken, ".class", "decompile",
        /*isDefinition=*/true);
    WriteMetadataToken(output_, module, typeToken, typeToken,
        /*spaceAfter=*/true, /*spaceBefore=*/true, ShowMetadataTokens(),
        ShowMetadataTokensInBase10());
    std::uint32_t attributes = module.GetTypeDefAttributes(typeToken);
    auto typeAttributesValue = static_cast<TypeAttributes>(attributes);
    // The C# `(typeDefinition.Attributes & TypeAttributes.ClassSemanticsMask)
    // == TypeAttributes.Interface`.
    if ((typeAttributesValue & TypeAttributes::ClassSemanticsMask)
        == TypeAttributes::Interface)
        output_.Write("interface ");
    WriteEnum(typeAttributesValue & TypeAttributes::VisibilityMask,
        typeVisibility, output_);
    WriteEnum(typeAttributesValue & TypeAttributes::LayoutMask, typeLayout,
        output_);
    WriteEnum(typeAttributesValue & TypeAttributes::StringFormatMask,
        typeStringFormat, output_);
    const auto masks = TypeAttributes::ClassSemanticsMask
        | TypeAttributes::VisibilityMask | TypeAttributes::LayoutMask
        | TypeAttributes::StringFormatMask;
    WriteFlags(typeAttributesValue & ~masks, typeAttributes, output_);

    // The C# name: the full IL name for a top-level type, the escaped
    // short name for a nested one (the header's nested render carries no
    // declaring chain).
    auto nameInfo = module.GetTypeDefNameInfo(typeToken);
    if (nameInfo && nameInfo->DeclaringTypeToken != 0)
        output_.Write(Escape(nameInfo->Name));
    else
        output_.Write(Metadata::ToILNameString(
            Metadata::GetFullTypeNameFromDefinition(module, typeToken)));
    WriteTypeParameters(output_, module, genericContext,
        module.GetGenericParameters(typeToken));
    output_.MarkFoldStart("...", /*defaultCollapsed=*/
        !ExpandMemberDefinitions && isInType_,
        /*isDefinition=*/isInType_);
    output_.WriteLine();

    std::uint32_t baseType = module.GetBaseTypeToken(typeToken);
    if (baseType != 0) {
        output_.Indent();
        output_.Write("extends ");
        IL::WriteTo(module, output_, genericContext, baseType,
            ILNameSyntax::TypeName);
        output_.WriteLine();
        output_.Unindent();
    }
}

// ---------------------------------------------------------------------------
// DisassembleNamespace (ReflectionDisassembler.cs lines 2034-2054).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::DisassembleNamespace(const std::string& nameSpace,
    Metadata::MetadataFile& module,
    const std::vector<std::uint32_t>& typeTokens)
{
    // The C# `string.IsNullOrEmpty` -- an empty namespace renders the types
    // bare with no wrapper block (and, faithfully, leaves isInType set --
    // the C# restores it only inside the non-empty arm).
    bool hasNamespace = !nameSpace.empty();
    if (hasNamespace) {
        output_.Write(".namespace " + Escape(nameSpace));
        OpenBlock(false);
    }
    bool oldIsInType = isInType_;
    isInType_ = true;
    for (std::uint32_t td : typeTokens) {
        // The C# `cancellationToken.ThrowIfCancellationRequested()` defers
        // with the cancellation-token type (the CLI never cancels a render).
        DisassembleType(module, td);
        output_.WriteLine();
    }
    if (hasNamespace) {
        CloseBlock();
        isInType_ = oldIsInType;
    }
}

// ---------------------------------------------------------------------------
// WriteAssemblyHeader (ReflectionDisassembler.cs lines 2056-2091).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::WriteAssemblyHeader(Metadata::MetadataFile& module)
{
    // The C# `if (!metadata.IsAssembly) return;` -- an empty Assembly table
    // (a netmodule) renders nothing.
    auto asmDef = module.GetAssemblyDefinition();
    if (!asmDef.has_value())
        return;
    output_.Write(".assembly ");
    if ((asmDef->Flags & static_cast<std::uint32_t>(
            AssemblyAttributes::WindowsRuntime)) != 0) {
        output_.Write("windowsruntime ");
    }
    output_.Write(Escape(asmDef->Name));
    OpenBlock(false);
    // The C# `asm.GetCustomAttributes()` / `asm.GetDeclarativeSecurity
    // Attributes()` -- the assembly-manifest parent is the 0x20000001 row
    // token.
    WriteAttributes(module, module.GetCustomAttributeTokens(asmDef->Token));
    WriteSecurityDeclarations(module,
        module.GetDeclarativeSecurityAttributes(asmDef->Token));
    if (!asmDef->PublicKey.empty()) {
        output_.Write(".publickey = ");
        WriteBlob(asmDef->PublicKey.data(), asmDef->PublicKey.size());
        output_.WriteLine();
    }
    if (asmDef->HashAlgorithm != static_cast<std::uint32_t>(
            AssemblyHashAlgorithm::None)) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08x", asmDef->HashAlgorithm);
        output_.Write(".hash algorithm " + std::string(buf));
        if (asmDef->HashAlgorithm == static_cast<std::uint32_t>(
                AssemblyHashAlgorithm::Sha1)) {
            output_.Write(" // SHA1");
        }
        output_.WriteLine();
    }
    // The C# `Version v = asm.Version` is never null over the SRM row (the
    // four 2-byte columns are always there) -- the null check never fires.
    Output::WriteLine(output_, ".ver " + std::to_string(asmDef->MajorVersion)
        + ":" + std::to_string(asmDef->MinorVersion)
        + ":" + std::to_string(asmDef->BuildNumber)
        + ":" + std::to_string(asmDef->RevisionNumber));
    CloseBlock();
}

// ---------------------------------------------------------------------------
// WriteAssemblyReferences (ReflectionDisassembler.cs lines 2093-2120).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::WriteAssemblyReferences(
    const Metadata::MetadataFile& module)
{
    for (const auto& mref : module.GetModuleReferences()) {
        Output::WriteLine(output_,
            ".module extern " + Escape(mref.Name));
    }
    for (const auto& aref : module.GetAssemblyReferences()) {
        output_.Write(".assembly extern ");
        if ((aref.Flags & static_cast<std::uint32_t>(
                AssemblyAttributes::WindowsRuntime)) != 0) {
            output_.Write("windowsruntime ");
        }
        output_.Write(Escape(aref.Name));
        OpenBlock(false);
        if (!aref.PublicKeyOrToken.empty()) {
            output_.Write(".publickeytoken = ");
            WriteBlob(aref.PublicKeyOrToken.data(),
                aref.PublicKeyOrToken.size());
            output_.WriteLine();
        }
        // Same never-null Version contract as WriteAssemblyHeader.
        Output::WriteLine(output_, ".ver " + std::to_string(aref.MajorVersion)
            + ":" + std::to_string(aref.MinorVersion)
            + ":" + std::to_string(aref.BuildNumber)
            + ":" + std::to_string(aref.RevisionNumber));
        CloseBlock();
    }
}


// ---------------------------------------------------------------------------
// WriteModuleHeader (ReflectionDisassembler.cs lines 2119-2197).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::WriteModuleHeader(
    Metadata::MetadataFile& module, bool skipMVID)
{
    using ExportedTypeInfo = Metadata::MetadataFile::ExportedTypeInfo;

    // The C# local `void WriteExportedType(ExportedType exportedType)`: the
    // escaped namespace + '.' prefix when the Namespace column is not nil,
    // then the escaped name. A nil column is not the same as an empty-string
    // column: the latter keeps the dot.
    auto writeExportedType = [&](const ExportedTypeInfo& exportedType) {
        if (!exportedType.NamespaceNil) {
            output_.Write(Escape(exportedType.Namespace));
            output_.Write(".");
        }
        output_.Write(Escape(exportedType.Name));
    };

    for (const auto& exportedType : module.GetExportedTypes()) {
        output_.Write(".class extern ");
        // The C# `exportedType.IsForwarder` -- the SRM property fuses the
        // ForwarderType flag with an AssemblyReference implementation (a
        // forwarder row always points into another assembly).
        constexpr std::uint32_t kForwarderType = 0x00200000u;
        bool isForwarder =
            (exportedType.Attributes & kForwarderType) != 0
            && (exportedType.ImplementationToken >> 24) == 0x23u;
        if (isForwarder)
            output_.Write("forwarder ");
        writeExportedType(exportedType);
        OpenBlock(false);
        switch (exportedType.ImplementationToken >> 24) {
            case 0x26u: {  // HandleKind.AssemblyFile
                auto name = module.GetAssemblyFileName(
                    exportedType.ImplementationToken);
                if (!name)
                    throw std::out_of_range("Invalid AssemblyFileHandle.");
                Output::WriteLine(output_, ".file " + *name);
                if (exportedType.TypeDefinitionId != 0) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "0x%08x",
                        exportedType.TypeDefinitionId);
                    Output::WriteLine(output_, ".class " + std::string(buf));
                }
                break;
            }
            case 0x27u: {  // HandleKind.ExportedType
                output_.Write(".class extern ");
                // The declaring-type chain walk: each declaring row's
                // namespace.name, with NO separator between chain links (the
                // C# while-loop calls WriteExportedType once per link) -- a
                // doubly nested exported type renders the links
                // concatenated.
                std::uint32_t implementationToken =
                    exportedType.ImplementationToken;
                while (true) {
                    auto declaringType = module.GetExportedType(
                        implementationToken);
                    if (!declaringType)
                        throw std::out_of_range("Invalid ExportedTypeHandle.");
                    writeExportedType(*declaringType);
                    if ((declaringType->ImplementationToken >> 24) == 0x27u) {
                        implementationToken =
                            declaringType->ImplementationToken;
                    } else {
                        break;
                    }
                }
                output_.WriteLine();
                break;
            }
            case 0x23u: {  // HandleKind.AssemblyReference
                output_.Write(".assembly extern ");
                auto name = module.GetAssemblyReferenceName(
                    exportedType.ImplementationToken);
                if (!name)
                    throw std::out_of_range("Invalid AssemblyReferenceHandle.");
                output_.Write(Escape(*name));
                output_.WriteLine();
                break;
            }
            default:
                throw std::runtime_error(
                    "Implementation must either be an index into the File, "
                    "ExportedType or AssemblyRef table.");
        }
        CloseBlock();
    }
    auto moduleDefinition = module.GetModuleDefinition();
    if (!moduleDefinition)
        throw std::out_of_range("Invalid ModuleDefinitionHandle.");

    // The C# `.module {0}` -- the module name is NOT escaped (unlike the
    // exported-type names).
    Output::WriteLine(output_, ".module " + moduleDefinition->Name);
    if (!skipMVID) {
        // The C# `GetGuid(Mvid).ToString("B").ToUpperInvariant()`: the
        // "B" brace form -- the first three groups little-endian, the
        // last ten bytes in order -- uppercased.
        const auto& g = moduleDefinition->Mvid;
        std::uint32_t a = static_cast<std::uint32_t>(g[0])
            | (static_cast<std::uint32_t>(g[1]) << 8)
            | (static_cast<std::uint32_t>(g[2]) << 16)
            | (static_cast<std::uint32_t>(g[3]) << 24);
        std::uint32_t b = static_cast<std::uint32_t>(g[4])
            | (static_cast<std::uint32_t>(g[5]) << 8);
        std::uint32_t c = static_cast<std::uint32_t>(g[6])
            | (static_cast<std::uint32_t>(g[7]) << 8);
        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "// MVID: {%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
            a, b, c, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
        Output::WriteLine(output_, buf);
    }

    // The C# `module is PEFile peFile` arm -- the port's MetadataFile is
    // always PE-backed, so the gate is the PE parse having succeeded.
    if (auto headers = module.GetPeHeaderInfo()) {
        char buf[64];
        // `{0:x8}` over the ulong members: a MINIMUM width of eight hex
        // digits (a PE32+ ImageBase renders all its digits).
        std::snprintf(buf, sizeof(buf), "0x%08llx",
            static_cast<unsigned long long>(headers->ImageBase));
        Output::WriteLine(output_, std::string(".imagebase ") + buf);
        std::snprintf(buf, sizeof(buf), "0x%08x", headers->FileAlignment);
        Output::WriteLine(output_, std::string(".file alignment ") + buf);
        std::snprintf(buf, sizeof(buf), "0x%08llx",
            static_cast<unsigned long long>(headers->SizeOfStackReserve));
        Output::WriteLine(output_, std::string(".stackreserve ") + buf);
        // `{0:x}` over the BCL enums pads to the underlying type's full
        // width: ushort Subsystem -> four digits, int32 CorFlags -> eight.
        std::snprintf(buf, sizeof(buf), "0x%04x // %s",
            headers->Subsystem,
            SubsystemToString(headers->Subsystem).c_str());
        Output::WriteLine(output_, std::string(".subsystem ") + buf);
        std::snprintf(buf, sizeof(buf), "0x%08x // %s",
            headers->CorFlags, CorFlagsToString(headers->CorFlags).c_str());
        Output::WriteLine(output_, std::string(".corflags ") + buf);
    }

    // The module's own custom attributes (the ModuleDefinition token is the
    // Module table's row 1).
    WriteAttributes(module,
        module.GetCustomAttributeTokens(0x00000001u));
}

// ---------------------------------------------------------------------------
// WriteModuleContents (ReflectionDisassembler.cs lines 2199-2206).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::WriteModuleContents(
    Metadata::MetadataFile& module)
{
    for (std::uint32_t handle : Process(module,
             module.GetTopLevelTypeDefinitions(),
             ProcessedEntityKind::TypeDefinition)) {
        DisassembleType(module, handle);
        output_.WriteLine();
    }
}
}  // namespace ILSpy::Decompiler::Disassembler
