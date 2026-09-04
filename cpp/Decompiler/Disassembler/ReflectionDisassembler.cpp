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
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

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

}  // namespace ILSpy::Decompiler::Disassembler
