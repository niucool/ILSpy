// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// CustomAttributeDecoder.cs -- see the header's port conventions.

#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"

#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The parameterless `new BadImageFormatException()` Message (gold-pinned: the
// .NET parameterless-ctor text, distinct from every named-message arm).
constexpr const char* kBadImageFormat =
    "Format of the executable (.exe) or library (.dll) is invalid.";
// The SRM Throw.InvalidSerializedString / InvalidCompressedInteger texts.
constexpr const char* kInvalidSerializedString = "Invalid serialized string.";
constexpr const char* kInvalidCompressedInteger = "Invalid compressed integer.";
// The SRM MemoryBlock.CheckBounds text (every fixed-size / bounds-checked
// read past the end of a blob).
constexpr const char* kReadOutOfBounds = "Read out of bounds.";
// The System.SR.NotTypeDefOrRefHandle text.
constexpr const char* kNotTypeDefOrRef =
    "Specified handle is not a TypeDefinitionHandle or TypeReferenceHandle.";

[[noreturn]] void ThrowBadImageFormat() {
    throw std::invalid_argument(kBadImageFormat);
}

// The C# `BlobReader.ReadUInt16()` (little-endian).
std::uint16_t ReadUInt16(CustomAttributeDecoder::Reader& r) {
    if (r.size - r.pos < 2)
        throw std::invalid_argument(kReadOutOfBounds);
    std::uint16_t v = static_cast<std::uint16_t>(r.data[r.pos])
        | (static_cast<std::uint16_t>(r.data[r.pos + 1]) << 8);
    r.pos += 2;
    return v;
}

// The C# `BlobReader.ReadByte()`.
std::uint8_t ReadByteRaw(CustomAttributeDecoder::Reader& r) {
    if (r.pos >= r.size)
        throw std::invalid_argument(kReadOutOfBounds);
    return r.data[r.pos++];
}

// The C# `BlobReader.ReadCompressedIntegerOrInvalid()` -- the decompiled
// MemoryBlock.PeekCompressedInteger semantics: a 1-byte form for < 0x80, a
// 2-byte form under the 0x80-0xBF prefix (when 2 bytes remain), a 4-byte form
// under 0xC0-0xDF (when 4 remain); everything else (the reserved 0xE0-0xFF
// prefixes, truncation, end-of-blob) reads INVALID with ZERO bytes consumed
// (the cursor does not move -- the caller re-reads the byte, which is how
// ReadSerializedString's 0xFF null form works).
int ReadCompressedIntegerOrInvalid(CustomAttributeDecoder::Reader& r,
    int& bytesRead) {
    bytesRead = 0;
    if (r.pos >= r.size)
        return 0x7FFFFFFF;  // int.MaxValue
    std::uint8_t b = r.data[r.pos];
    long long remaining = static_cast<long long>(r.size - r.pos);
    if ((b & 0x80) == 0) {
        r.pos += 1;
        bytesRead = 1;
        return b;
    }
    if ((b & 0x40) == 0) {
        if (remaining >= 2) {
            r.pos += 2;
            bytesRead = 2;
            return (static_cast<int>(b & 0x3F) << 8) | r.data[r.pos - 1];
        }
    } else if ((b & 0x20) == 0 && remaining >= 4) {
        r.pos += 4;
        bytesRead = 4;
        return (static_cast<int>(b & 0x1F) << 24)
            | (static_cast<int>(r.data[r.pos - 3]) << 16)
            | (static_cast<int>(r.data[r.pos - 2]) << 8)
            | static_cast<int>(r.data[r.pos - 1]);
    }
    return 0x7FFFFFFF;
}

// The C# `BlobReader.ReadCompressedInteger()` -- the throwing form.
int ReadCompressedInteger(CustomAttributeDecoder::Reader& r) {
    int bytesRead;
    int value = ReadCompressedIntegerOrInvalid(r, bytesRead);
    if (value == 0x7FFFFFFF)
        throw std::invalid_argument(kInvalidCompressedInteger);
    return value;
}

// The C# `BlobReader.ReadSerializationTypeCode()`: the compressed integer as
// a type code -- a value over 255 (including INVALID) reads as Invalid.
int ReadSerializationTypeCode(CustomAttributeDecoder::Reader& r) {
    int bytesRead;
    int value = ReadCompressedIntegerOrInvalid(r, bytesRead);
    if (value > 255)
        return 0;  // SerializationTypeCode.Invalid
    return value;
}

// The C# `BlobReader.ReadSignatureTypeCode()`: the compressed integer as a
// signature element type -- the raw 17/18 (CLASS/VALUETYPE) both map to
// TypeHandle (64), a value over 255 (including INVALID) reads as Invalid
// (-1 here, an out-of-range int the caller's switch rejects).
int ReadSignatureTypeCode(CustomAttributeDecoder::Reader& r) {
    int bytesRead;
    int value = ReadCompressedIntegerOrInvalid(r, bytesRead);
    if (value == 17 || value == 18)
        return 64;  // SignatureTypeCode.TypeHandle
    if (value > 255)
        return -1;  // SignatureTypeCode.Invalid
    return value;
}

// The C# `BlobReader.ReadTypeHandle()`: the TypeDefOrRefOrSpec coded index.
// Returns the raw token (0 = the nil handle: tag 3, a zero row, or an
// invalid compressed integer).
std::uint32_t ReadTypeHandle(CustomAttributeDecoder::Reader& r) {
    int bytesRead;
    std::uint32_t num =
        static_cast<std::uint32_t>(ReadCompressedIntegerOrInvalid(r, bytesRead));
    static constexpr std::uint32_t kTables[4] = {
        0x02000000,  // TypeDef (tag 0)
        0x01000000,  // TypeRef (tag 1)
        0x1B000000,  // TypeSpec (tag 2)
        0,           // nil (tag 3)
    };
    std::uint32_t table = kTables[num & 3];
    if (num == 0x7FFFFFFF || table == 0)
        return 0;
    return table | (num >> 2);
}

// The C# `BlobReader.ReadSerializedString()`: the SerString -- a compressed
// length then that many UTF-8 bytes; a single 0xFF byte (an invalid compressed
// integer the Try rejects without consuming) reads as NULL.
std::optional<std::string> ReadSerializedString(
    CustomAttributeDecoder::Reader& r) {
    int bytesRead;
    int length = ReadCompressedIntegerOrInvalid(r, bytesRead);
    if (length != 0x7FFFFFFF) {
        if (length < 0
            || r.size - r.pos < static_cast<std::size_t>(length)) {
            // The C# ReadUTF8's bounds check.
            throw std::invalid_argument(kReadOutOfBounds);
        }
        std::string out(reinterpret_cast<const char*>(r.data + r.pos),
            static_cast<std::size_t>(length));
        r.pos += static_cast<std::size_t>(length);
        return out;
    }
    if (ReadByteRaw(r) != 0xFF)
        throw std::invalid_argument(kInvalidSerializedString);
    return std::nullopt;
}

} // namespace

CustomAttributeDecoder::CustomAttributeDecoder(const MetadataFile& metadata,
    TypeSystem::TypeProvider& provider, bool provideBoxingTypeInfo)
    : metadata_(metadata),
      provider_(provider),
      provideBoxingTypeInfo_(provideBoxingTypeInfo) {}

CustomAttributeValue CustomAttributeDecoder::DecodeValue(
    std::uint32_t constructorToken, const std::uint8_t* valueData,
    std::size_t valueSize) const {
    // The C# ctor-signature dispatch: a MethodDef ctor's Signature column, a
    // MemberRef ctor's Signature column plus (when the parent is a TypeSpec)
    // the instantiation blob that scopes the ctor's VAR references.
    std::optional<std::vector<std::uint8_t>> signature;
    std::optional<std::vector<std::uint8_t>> genericContextBlob;
    const std::uint8_t table = static_cast<std::uint8_t>(constructorToken >> 24);
    if (table == 0x06) {  // HandleKind.MethodDefinition
        signature = metadata_.GetSignatureBlob(constructorToken);
        if (!signature)
            ThrowBadImageFormat();
    } else if (table == 0x0A) {  // HandleKind.MemberReference
        auto memberReference = metadata_.GetMemberReference(constructorToken);
        if (!memberReference)
            ThrowBadImageFormat();
        signature = metadata_.GetSignatureBlob(constructorToken);
        if (!signature)
            ThrowBadImageFormat();
        if (memberReference->ParentToken != 0
            && (memberReference->ParentToken >> 24) == 0x1B) {
            genericContextBlob =
                metadata_.GetTypeSpecSignatureBlob(memberReference->ParentToken);
            if (!genericContextBlob)
                ThrowBadImageFormat();
        }
    } else {
        ThrowBadImageFormat();
    }

    Reader valueReader{valueData, valueSize, 0};
    if (ReadUInt16(valueReader) != 0x0001)
        ThrowBadImageFormat();

    Reader signatureReader{signature->data(), signature->size(), 0};
    // The C# `ReadSignatureHeader()` + the Kind/IsGeneric checks
    // (SignatureKind.Method is the 0 convention byte).
    std::uint8_t headerByte = ReadByteRaw(signatureReader);
    if ((headerByte & 0x0F) != 0x00 || (headerByte & 0x10) != 0)
        ThrowBadImageFormat();
    int count = ReadCompressedInteger(signatureReader);
    if (ReadSignatureTypeCode(signatureReader) != 0x01)  // Void
        ThrowBadImageFormat();

    Reader genericContext{};
    if (genericContextBlob) {
        genericContext = Reader{genericContextBlob->data(),
            genericContextBlob->size(), 0};
        if (ReadSignatureTypeCode(genericContext) == 21) {  // GenericTypeInstance
            // The CLASS (18) / VALUETYPE (17) marker, read as a RAW
            // compressed integer (not through the TypeHandle mapping).
            int num = ReadCompressedInteger(genericContext);
            if (num != 18 && num != 17)
                ThrowBadImageFormat();
            ReadTypeHandle(genericContext);
        } else {
            genericContext = Reader{};  // the C# `default(BlobReader)`
        }
    }

    auto fixedArguments =
        DecodeFixedArguments(signatureReader, valueReader, count, genericContext);
    auto namedArguments = DecodeNamedArguments(valueReader);
    return CustomAttributeValue{std::move(fixedArguments),
        std::move(namedArguments)};
}

std::vector<TypeSystem::CustomAttributeNamedArgument>
CustomAttributeDecoder::DecodeNamedArguments(const std::uint8_t* base,
    std::size_t size, std::size_t& pos, int count) const {
    Reader valueReader{base, size, pos};
    auto result = DecodeNamedArgumentsLoop(valueReader, count);
    pos = valueReader.pos;
    return result;
}

std::vector<TypeSystem::CustomAttributeTypedArgument>
CustomAttributeDecoder::DecodeFixedArguments(Reader& signatureReader,
    Reader& valueReader, int count, Reader& genericContextReader) const {
    if (count == 0)
        return {};
    std::vector<TypeSystem::CustomAttributeTypedArgument> arguments;
    for (int i = 0; i < count; i++) {
        ArgumentTypeInfo info = DecodeFixedArgumentType(signatureReader,
            genericContextReader, false);
        arguments.push_back(DecodeArgument(valueReader, info));
    }
    return arguments;
}

std::vector<TypeSystem::CustomAttributeNamedArgument>
CustomAttributeDecoder::DecodeNamedArguments(Reader& valueReader) const {
    // The SRM private form: the count is read from the blob (a UInt16).
    int num = ReadUInt16(valueReader);
    if (num == 0)
        return {};
    return DecodeNamedArgumentsLoop(valueReader, num);
}

std::vector<TypeSystem::CustomAttributeNamedArgument>
CustomAttributeDecoder::DecodeNamedArgumentsLoop(Reader& valueReader,
    int count) const {
    std::vector<TypeSystem::CustomAttributeNamedArgument> arguments;
    for (int i = 0; i < count; i++) {
        int kindByte = ReadSerializationTypeCode(valueReader);
        auto kind = static_cast<TypeSystem::CustomAttributeNamedArgumentKind>(
            kindByte);
        if (kind != TypeSystem::CustomAttributeNamedArgumentKind::Field
            && kind != TypeSystem::CustomAttributeNamedArgumentKind::Property) {
            ThrowBadImageFormat();
        }
        ArgumentTypeInfo info = DecodeNamedArgumentType(valueReader, false);
        std::optional<std::string> name = ReadSerializedString(valueReader);
        TypeSystem::CustomAttributeTypedArgument argument =
            DecodeArgument(valueReader, info);
        arguments.emplace_back(name.value_or(""), kind, argument.Type(),
            argument.Value());
    }
    return arguments;
}

CustomAttributeDecoder::ArgumentTypeInfo
CustomAttributeDecoder::DecodeFixedArgumentType(Reader& signatureReader,
    Reader& genericContextReader, bool isElementType) const {
    int code = ReadSignatureTypeCode(signatureReader);
    ArgumentTypeInfo result;
    result.TypeCode = static_cast<SerializationTypeCode>(code);
    switch (code) {
        case 2:   // Boolean
        case 3:   // Char
        case 4:   // SByte
        case 5:   // Byte
        case 6:   // Int16
        case 7:   // UInt16
        case 8:   // Int32
        case 9:   // UInt32
        case 10:  // Int64
        case 11:  // UInt64
        case 12:  // Single
        case 13:  // Double
        case 14:  // String
            result.Type =
                provider_.GetPrimitiveType(static_cast<PrimitiveTypeCode>(code));
            break;
        case 28: {  // Object
            result.TypeCode = SerializationTypeCode::TaggedObject;
            result.Type = provider_.GetPrimitiveType(PrimitiveTypeCode::Object);
            break;
        }
        case 64: {  // TypeHandle (the raw CLASS/VALUETYPE element)
            std::uint32_t handle = ReadTypeHandle(signatureReader);
            result.Type = GetTypeFromHandle(handle);
            result.TypeCode = provider_.IsSystemType(*result.Type)
                ? SerializationTypeCode::Type
                : static_cast<SerializationTypeCode>(
                    provider_.GetUnderlyingEnumType(*result.Type));
            break;
        }
        case 29: {  // SZArray
            if (isElementType) {
                // jagged arrays are not allowed.
                ThrowBadImageFormat();
            }
            ArgumentTypeInfo elementInfo =
                DecodeFixedArgumentType(signatureReader, genericContextReader,
                    true);
            result.ElementType = elementInfo.Type;
            result.ElementTypeCode = elementInfo.TypeCode;
            result.Type = provider_.GetSZArrayType(result.ElementType);
            break;
        }
        case 19: {  // GenericTypeParameter (VAR)
            if (genericContextReader.size == 0) {
                ThrowBadImageFormat();
            }
            int num = ReadCompressedInteger(signatureReader);
            int num2 = ReadCompressedInteger(genericContextReader);
            if (num >= num2) {
                ThrowBadImageFormat();
            }
            while (num > 0) {
                SkipType(genericContextReader);
                num--;
            }
            // A fresh empty generic context: a nested generic-parameter
            // decode throws BadImageFormatException rather than re-reading
            // the outer context (the C# passes a default BlobReader here).
            Reader emptyContext;
            return DecodeFixedArgumentType(genericContextReader, emptyContext,
                true);
        }
        default:
            ThrowBadImageFormat();
    }
    return result;
}

CustomAttributeDecoder::ArgumentTypeInfo
CustomAttributeDecoder::DecodeNamedArgumentType(Reader& valueReader,
    bool isElementType) const {
    ArgumentTypeInfo result;
    int code = ReadSerializationTypeCode(valueReader);
    result.TypeCode = static_cast<SerializationTypeCode>(code);
    switch (code) {
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
            result.Type =
                provider_.GetPrimitiveType(static_cast<PrimitiveTypeCode>(code));
            break;
        case 80:  // Type
            result.Type = provider_.GetSystemType();
            break;
        case 81:  // TaggedObject
            result.Type = provider_.GetPrimitiveType(PrimitiveTypeCode::Object);
            break;
        case 29: {  // SZArray
            if (isElementType) {
                // jagged arrays are not allowed.
                ThrowBadImageFormat();
            }
            ArgumentTypeInfo elementInfo =
                DecodeNamedArgumentType(valueReader, true);
            result.ElementType = elementInfo.Type;
            result.ElementTypeCode = elementInfo.TypeCode;
            result.Type = provider_.GetSZArrayType(result.ElementType);
            break;
        }
        case 85: {  // Enum
            std::optional<std::string> typeName =
                ReadSerializedString(valueReader);
            if (!typeName) {
                // The C# GetTypeFromSerializedName(null) returns null, and
                // GetUnderlyingEnumType(null) NREs at the extension call --
                // the faithful message (the BamlNode NRE convention).
                throw std::runtime_error(
                    "Object reference not set to an instance of an object.");
            }
            result.Type = provider_.GetTypeFromSerializedName(*typeName);
            result.TypeCode = static_cast<SerializationTypeCode>(
                provider_.GetUnderlyingEnumType(*result.Type));
            break;
        }
        default:
            ThrowBadImageFormat();
    }
    return result;
}

TypeSystem::CustomAttributeTypedArgument
CustomAttributeDecoder::DecodeArgument(Reader& valueReader,
    const ArgumentTypeInfo& info) const {
    ArgumentTypeInfo outer = info;
    ArgumentTypeInfo decoded = info;
    if (decoded.TypeCode == SerializationTypeCode::TaggedObject) {
        decoded = DecodeNamedArgumentType(valueReader, false);
    }
    std::any value;
    switch (decoded.TypeCode) {
        case SerializationTypeCode::Boolean:
            value = ReadByteRaw(valueReader) != 0;
            break;
        case SerializationTypeCode::Byte:
            value = ReadByteRaw(valueReader);
            break;
        case SerializationTypeCode::Char: {
            std::uint16_t raw = ReadUInt16(valueReader);
            value = static_cast<char16_t>(raw);
            break;
        }
        case SerializationTypeCode::Double: {
            if (valueReader.size - valueReader.pos < 8)
                throw std::invalid_argument(kReadOutOfBounds);
            std::uint64_t bits = 0;
            for (int i = 0; i < 8; i++)
                bits |= static_cast<std::uint64_t>(
                    valueReader.data[valueReader.pos + i]) << (8 * i);
            valueReader.pos += 8;
            double d;
            std::memcpy(&d, &bits, sizeof(d));
            value = d;
            break;
        }
        case SerializationTypeCode::Int16: {
            std::uint16_t raw = ReadUInt16(valueReader);
            value = static_cast<std::int16_t>(raw);
            break;
        }
        case SerializationTypeCode::Int32: {
            if (valueReader.size - valueReader.pos < 4)
                throw std::invalid_argument(kReadOutOfBounds);
            std::uint32_t raw = 0;
            for (int i = 0; i < 4; i++)
                raw |= static_cast<std::uint32_t>(
                    valueReader.data[valueReader.pos + i]) << (8 * i);
            valueReader.pos += 4;
            value = static_cast<std::int32_t>(raw);
            break;
        }
        case SerializationTypeCode::Int64: {
            if (valueReader.size - valueReader.pos < 8)
                throw std::invalid_argument(kReadOutOfBounds);
            std::uint64_t bits = 0;
            for (int i = 0; i < 8; i++)
                bits |= static_cast<std::uint64_t>(
                    valueReader.data[valueReader.pos + i]) << (8 * i);
            valueReader.pos += 8;
            value = static_cast<std::int64_t>(bits);
            break;
        }
        case SerializationTypeCode::SByte:
            value = static_cast<std::int8_t>(ReadByteRaw(valueReader));
            break;
        case SerializationTypeCode::Single: {
            if (valueReader.size - valueReader.pos < 4)
                throw std::invalid_argument(kReadOutOfBounds);
            std::uint32_t bits = 0;
            for (int i = 0; i < 4; i++)
                bits |= static_cast<std::uint32_t>(
                    valueReader.data[valueReader.pos + i]) << (8 * i);
            valueReader.pos += 4;
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            value = f;
            break;
        }
        case SerializationTypeCode::UInt16:
            value = ReadUInt16(valueReader);
            break;
        case SerializationTypeCode::UInt32: {
            if (valueReader.size - valueReader.pos < 4)
                throw std::invalid_argument(kReadOutOfBounds);
            std::uint32_t raw = 0;
            for (int i = 0; i < 4; i++)
                raw |= static_cast<std::uint32_t>(
                    valueReader.data[valueReader.pos + i]) << (8 * i);
            valueReader.pos += 4;
            value = raw;
            break;
        }
        case SerializationTypeCode::UInt64: {
            if (valueReader.size - valueReader.pos < 8)
                throw std::invalid_argument(kReadOutOfBounds);
            std::uint64_t bits = 0;
            for (int i = 0; i < 8; i++)
                bits |= static_cast<std::uint64_t>(
                    valueReader.data[valueReader.pos + i]) << (8 * i);
            valueReader.pos += 8;
            value = bits;
            break;
        }
        case SerializationTypeCode::String: {
            // A null SerString boxes as a null object (the empty std::any).
            auto s = ReadSerializedString(valueReader);
            if (s)
                value = std::move(*s);
            break;
        }
        case SerializationTypeCode::Type: {
            std::optional<std::string> name = ReadSerializedString(valueReader);
            // The C# `GetTypeFromSerializedName(name)` returns null for a
            // null name -- the null ITypePtr, no provider call.
            if (name)
                value = provider_.GetTypeFromSerializedName(*name);
            break;
        }
        case SerializationTypeCode::SZArray: {
            auto array = DecodeArrayArgument(valueReader, decoded);
            if (array)
                value = std::move(*array);
            break;
        }
        default:
            ThrowBadImageFormat();
    }
    if (provideBoxingTypeInfo_
        && outer.TypeCode == SerializationTypeCode::TaggedObject) {
        return TypeSystem::CustomAttributeTypedArgument(outer.Type,
            TypeSystem::CustomAttributeTypedArgument(decoded.Type, value));
    }
    return TypeSystem::CustomAttributeTypedArgument(decoded.Type, value);
}

std::optional<std::vector<TypeSystem::CustomAttributeTypedArgument>>
CustomAttributeDecoder::DecodeArrayArgument(Reader& valueReader,
    const ArgumentTypeInfo& info) const {
    if (valueReader.size - valueReader.pos < 4)
        throw std::invalid_argument(kReadOutOfBounds);
    std::uint32_t raw = 0;
    for (int i = 0; i < 4; i++)
        raw |= static_cast<std::uint32_t>(
            valueReader.data[valueReader.pos + i]) << (8 * i);
    valueReader.pos += 4;
    std::int32_t count = static_cast<std::int32_t>(raw);
    if (count == -1) {
        return std::nullopt;
    }
    if (count == 0) {
        return std::vector<TypeSystem::CustomAttributeTypedArgument>{};
    }
    if (count < 0) {
        ThrowBadImageFormat();
    }
    ArgumentTypeInfo elementInfo;
    elementInfo.Type = info.ElementType;
    elementInfo.TypeCode = info.ElementTypeCode;
    std::vector<TypeSystem::CustomAttributeTypedArgument> array;
    for (int i = 0; i < count; i++) {
        array.push_back(DecodeArgument(valueReader, elementInfo));
    }
    return array;
}

TypeSystem::ITypePtr CustomAttributeDecoder::GetTypeFromHandle(
    std::uint32_t token) const {
    const std::uint8_t table = static_cast<std::uint8_t>(token >> 24);
    if (table == 0x02) {  // TypeDefinition
        return provider_.GetTypeFromDefinition(metadata_, token, 0);
    }
    if (table == 0x01) {  // TypeReference
        return provider_.GetTypeFromReference(metadata_, token, 0);
    }
    throw std::invalid_argument(kNotTypeDefOrRef);
}

// The C# `SkipType` -- the decompiled SRM switch over the RAW compressed
// element-type code, reproduced exactly (the header's convention (f)): the
// no-operand codes; the wrapper codes (Pointer/ByReference/SZArray/Sentinel/
// Pinned) recursing; FunctionPointer's header/count/return/parameters walk;
// Array's element + shape; the CModReqd/CModOpt handle + wrapped type;
// GenericTypeInstance's marker-skip + count + arguments; VAR's compressed
// index; and the CLASS/VALUETYPE (17/18) quirk -- skipped AS IF the coded
// index that follows were a nested type.
void CustomAttributeDecoder::SkipType(Reader& reader) {
    switch (ReadCompressedInteger(reader)) {
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
        case 22:
        case 24:
        case 25:
        case 28:
            break;
        case 15:
        case 16:
        case 29:
        case 69:
            SkipType(reader);
            break;
        case 27: {
            // FunctionPointer: an optional generic count, the parameter
            // count, the return type, then each parameter type.
            std::uint8_t header = ReadByteRaw(reader);
            if ((header & 0x10) != 0) {
                ReadCompressedInteger(reader);
            }
            int num2 = ReadCompressedInteger(reader);
            SkipType(reader);
            for (int j = 0; j < num2; j++) {
                SkipType(reader);
            }
            break;
        }
        case 20: {
            // Array: the element type, the rank, the sizes, the lower bounds.
            SkipType(reader);
            ReadCompressedInteger(reader);
            int num3 = ReadCompressedInteger(reader);
            for (int k = 0; k < num3; k++) {
                ReadCompressedInteger(reader);
            }
            int num4 = ReadCompressedInteger(reader);
            for (int l = 0; l < num4; l++) {
                // ReadCompressedSignedInteger: the compressed payload with
                // the II.23.2.2 sign bits rotated off (only the skip matters).
                int bytesRead;
                int v = ReadCompressedIntegerOrInvalid(reader, bytesRead);
                if (v == 0x7FFFFFFF)
                    throw std::invalid_argument(kInvalidCompressedInteger);
                bool sign = (v & 1) != 0;
                v >>= 1;
                if (sign) {
                    switch (bytesRead) {
                        case 1:
                            v |= -64;
                            break;
                        case 2:
                            v |= -8192;
                            break;
                        default:
                            v |= -268435456;
                            break;
                    }
                }
            }
            break;
        }
        case 31:
        case 32:
            ReadTypeHandle(reader);
            SkipType(reader);
            break;
        case 21: {
            // GenericTypeInstance: the CLASS/VALUETYPE marker is skipped as
            // a type (the 17/18 quirk consumes the marker's coded index as
            // the "nested type"), then the argument count and arguments.
            SkipType(reader);
            int num = ReadCompressedInteger(reader);
            for (int i = 0; i < num; i++) {
                SkipType(reader);
            }
            break;
        }
        case 19:
            ReadCompressedInteger(reader);
            break;
        case 17:
        case 18:
            SkipType(reader);
            break;
        default:
            ThrowBadImageFormat();
    }
}

} // namespace ILSpy::Decompiler::Metadata
