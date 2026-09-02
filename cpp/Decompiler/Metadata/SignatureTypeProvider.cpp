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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The SignatureTypeProviderDecoder implementation -- see the header for the
// SRM-replacement rationale. The element-type coverage mirrors the IType-
// returning SignatureDecoder (Metadata/SignatureDecoder.cpp) structurally;
// the divergence is that cmod/pinned prefixes CALL the provider instead of
// being skipped (the text rendering needs the wrappers).

#include "Decompiler/Metadata/SignatureTypeProvider.hpp"

#include <cstring>

namespace ILSpy::Decompiler::Metadata {

namespace {

constexpr std::uint8_t kSentinel = 0x41;   // ELEMENT_TYPE_SENTINEL (vararg)
constexpr std::uint8_t kByRef = 0x10;      // ELEMENT_TYPE_BYREF
constexpr std::uint8_t kPtr = 0x0F;        // ELEMENT_TYPE_PTR
constexpr std::uint8_t kValueType = 0x11;  // ELEMENT_TYPE_VALUETYPE
constexpr std::uint8_t kClass = 0x12;      // ELEMENT_TYPE_CLASS
constexpr std::uint8_t kVar = 0x13;        // ELEMENT_TYPE_VAR
constexpr std::uint8_t kArray = 0x14;      // ELEMENT_TYPE_ARRAY
constexpr std::uint8_t kGenericInst = 0x15;
constexpr std::uint8_t kTypedByRef = 0x16;
constexpr std::uint8_t kIntPtr = 0x18;
constexpr std::uint8_t kUIntPtr = 0x19;
constexpr std::uint8_t kFnPtr = 0x1B;
constexpr std::uint8_t kObject = 0x1C;
constexpr std::uint8_t kSzArray = 0x1D;
constexpr std::uint8_t kMVar = 0x1E;
constexpr std::uint8_t kCModReqd = 0x1F;
constexpr std::uint8_t kCModOpt = 0x20;
constexpr std::uint8_t kPinned = 0x45;

// The PrimitiveTypeCode for an element-type byte that maps onto the enum
// (else nullopt -- the caller handles the composite cases).
std::optional<PrimitiveTypeCode> PrimitiveCode(std::uint8_t et) {
    switch (et) {
        case 0x01: return PrimitiveTypeCode::Void;
        case 0x02: return PrimitiveTypeCode::Boolean;
        case 0x03: return PrimitiveTypeCode::Char;
        case 0x04: return PrimitiveTypeCode::SByte;
        case 0x05: return PrimitiveTypeCode::Byte;
        case 0x06: return PrimitiveTypeCode::Int16;
        case 0x07: return PrimitiveTypeCode::UInt16;
        case 0x08: return PrimitiveTypeCode::Int32;
        case 0x09: return PrimitiveTypeCode::UInt32;
        case 0x0A: return PrimitiveTypeCode::Int64;
        case 0x0B: return PrimitiveTypeCode::UInt64;
        case 0x0C: return PrimitiveTypeCode::Single;
        case 0x0D: return PrimitiveTypeCode::Double;
        case 0x0E: return PrimitiveTypeCode::String;
        case 0x16: return PrimitiveTypeCode::TypedReference;
        case 0x18: return PrimitiveTypeCode::IntPtr;
        case 0x19: return PrimitiveTypeCode::UIntPtr;
        case 0x1C: return PrimitiveTypeCode::Object;
        default: return std::nullopt;
    }
}

} // namespace

[[noreturn]] void SignatureTypeProviderDecoder::Fail(const char* what) {
    throw std::logic_error(std::string("SignatureTypeProviderDecoder: ") + what);
}

std::uint8_t SignatureTypeProviderDecoder::Byte() {
    if (cur_ >= end_) Fail("blob truncated");
    return *cur_++;
}

std::uint8_t SignatureTypeProviderDecoder::PeekByte() {
    if (cur_ >= end_) Fail("blob truncated");
    return *cur_;
}

std::uint32_t SignatureTypeProviderDecoder::CompressedUnsigned() {
    std::uint8_t b0 = Byte();
    if ((b0 & 0x80) == 0) return b0;
    if ((b0 & 0xC0) == 0x80)
        return (static_cast<std::uint32_t>(b0 & 0x3F) << 8) | Byte();
    if ((b0 & 0xE0) != 0xC0) Fail("bad compressed integer");
    return (static_cast<std::uint32_t>(b0 & 0x1F) << 24) |
           (static_cast<std::uint32_t>(Byte()) << 16) |
           (static_cast<std::uint32_t>(Byte()) << 8) |
           static_cast<std::uint32_t>(Byte());
}

// The ECMA-335 II.23.2 compressed signed integer (the two's-complement of the
// unsigned decode, per II.23.2: a 1/2/4-byte encoding sign-extends from the
// first byte's high bit).
std::int32_t SignatureTypeProviderDecoder::CompressedSigned() {
    std::uint8_t b0 = Byte();
    if ((b0 & 0x80) == 0) {
        // 1-byte form carries 7 bits; the sign bit is bit 6.
        if (b0 & 0x40) return static_cast<std::int32_t>(b0) - 0x80;
        return b0;
    }
    if ((b0 & 0xC0) == 0x80) {
        std::uint32_t v = (static_cast<std::uint32_t>(b0 & 0x3F) << 8) | Byte();
        if (b0 & 0x20) return static_cast<std::int32_t>(v) - 0x2000;
        return static_cast<std::int32_t>(v);
    }
    if ((b0 & 0xE0) != 0xC0) Fail("bad compressed integer");
    std::uint32_t v = (static_cast<std::uint32_t>(b0 & 0x1F) << 24) |
                      (static_cast<std::uint32_t>(Byte()) << 16) |
                      (static_cast<std::uint32_t>(Byte()) << 8) |
                      static_cast<std::uint32_t>(Byte());
    if (b0 & 0x10) return static_cast<std::int32_t>(v) - 0x20000000;
    return static_cast<std::int32_t>(v);
}

std::vector<std::uint8_t> SignatureTypeProviderDecoder::ReadTypeSpecBlob(
        std::uint32_t typeSpecToken) {
    auto blob = module_.GetTypeSpecSignatureBlob(typeSpecToken);
    if (!blob) Fail("invalid TypeSpec token");
    return std::move(*blob);
}

// The C# `DecodeTypeHandle` / the TypeDefOrRefEncoded resolution: tag in the
// low 2 bits (0=TypeDef, 1=TypeRef, 2=TypeSpec), row in the high bits. A
// TypeSpec recurses: the row's signature blob decodes through the provider
// with the CURRENT generic context (the C# DecodeSpecification calls back
// through GetTypeFromSpecification, which the concrete provider implements
// over the same recursion).
SignatureTypeWriter SignatureTypeProviderDecoder::DecodeTypeDefOrRefEncoded(
        std::uint8_t rawTypeKind) {
    std::uint32_t coded = CompressedUnsigned();
    std::uint32_t tag = coded & 3;
    std::uint32_t row = coded >> 2;  // 1-based
    if (tag == 0 && row != 0)
        return provider_.GetTypeFromDefinition((0x02u << 24) | row, rawTypeKind);
    if (tag == 1 && row != 0)
        return provider_.GetTypeFromReference((0x01u << 24) | row, rawTypeKind);
    if (tag == 2 && row != 0)
        return provider_.GetTypeFromSpecification((0x1Bu << 24) | row, rawTypeKind,
            *context_);
    Fail("bad TypeDefOrRefEncoded");
}

// The C# `DecodeType`: the cmod/pinned prefixes (each modifier is a full
// type, itself possibly a TypeSpec) then the base element type. The C#
// SignatureDecoder reads the modifiers into a list and folds them around the
// base right-to-left, so the FIRST blob modifier is the outermost wrapper
// (ECMA-335 II.23.2.7 semantics).
SignatureTypeWriter SignatureTypeProviderDecoder::DecodeTypeWithPrefixes() {
    std::vector<std::pair<SignatureTypeWriter, bool>> modifiers;  // (writer, required)
    for (;;) {
        std::uint8_t b = PeekByte();
        if (b == kCModReqd || b == kCModOpt) {
            Byte();
            // The modifier type carries no class/valuetype prefix of its
            // own here (rawTypeKind 0 -- the C# decodes cmods via
            // DecodeTypeHandle with no ELEMENT_TYPE_CLASS/VALUETYPE byte).
            modifiers.emplace_back(DecodeTypeDefOrRefEncoded(0x00), b == kCModReqd);
            continue;
        }
        if (b == kPinned) {
            Byte();
            SignatureTypeWriter inner = DecodeTypeWithPrefixes();
            return provider_.GetPinnedType(std::move(inner));
        }
        break;
    }
    std::uint8_t et = Byte();
    if (auto prim = PrimitiveCode(et)) {
        SignatureTypeWriter base = provider_.GetPrimitiveType(*prim);
        return FoldModifiers(std::move(modifiers), std::move(base));
    }
    SignatureTypeWriter base;
    switch (et) {
        case kPtr:
            base = provider_.GetPointerType(DecodeTypeWithPrefixes());
            break;
        case kByRef:
            base = provider_.GetByReferenceType(DecodeTypeWithPrefixes());
            break;
        case kValueType:
        case kClass:
            base = DecodeTypeDefOrRefEncoded(et);
            break;
        case kVar:
            base = provider_.GetGenericTypeParameter(*context_,
                static_cast<int>(CompressedUnsigned()));
            break;
        case kMVar:
            base = provider_.GetGenericMethodParameter(*context_,
                static_cast<int>(CompressedUnsigned()));
            break;
        case kSzArray:
            base = provider_.GetSZArrayType(DecodeTypeWithPrefixes());
            break;
        case kArray: {
            SignatureTypeWriter elem = DecodeTypeWithPrefixes();
            ArrayShape shape;
            shape.Rank = CompressedUnsigned();
            std::uint32_t numSizes = CompressedUnsigned();
            shape.Sizes.reserve(numSizes);
            for (std::uint32_t i = 0; i < numSizes; i++)
                shape.Sizes.push_back(static_cast<std::int32_t>(CompressedUnsigned()));
            std::uint32_t numLoBounds = CompressedUnsigned();
            shape.LowerBounds.reserve(numLoBounds);
            for (std::uint32_t i = 0; i < numLoBounds; i++)
                shape.LowerBounds.push_back(CompressedSigned());
            base = provider_.GetArrayType(std::move(elem), shape);
            break;
        }
        case kGenericInst: {
            // The generic definition's class/valuetype marker is the def's
            // rawTypeKind (the C# passes it to DecodeTypeHandle).
            std::uint8_t marker = Byte();
            if (marker != kClass && marker != kValueType)
                Fail("bad GenericInst marker");
            SignatureTypeWriter def = DecodeTypeDefOrRefEncoded(marker);
            std::uint32_t argCount = CompressedUnsigned();
            std::vector<SignatureTypeWriter> args;
            args.reserve(argCount);
            for (std::uint32_t i = 0; i < argCount; i++)
                args.push_back(DecodeTypeWithPrefixes());
            base = provider_.GetGenericInstantiation(std::move(def), std::move(args));
            break;
        }
        case kFnPtr: {
            // The C# SignatureDecoder parses the embedded method signature
            // from the SAME cursor (no blob boundary).
            MethodSignatureT sig = DecodeMethodSignatureBody();
            base = provider_.GetFunctionPointerType(sig);
            break;
        }
        default:
            Fail("unrecognized ELEMENT_TYPE");
    }
    return FoldModifiers(std::move(modifiers), std::move(base));
}

// The C# `DecodeTypeOrByRef` (the return/parameter position of a method
// signature): a leading ELEMENT_TYPE_BYREF wraps the decoded type.
SignatureTypeWriter SignatureTypeProviderDecoder::DecodeTypeOrByRef() {
    if (PeekByte() == kByRef) {
        Byte();
        return provider_.GetByReferenceType(DecodeTypeWithPrefixes());
    }
    return DecodeTypeWithPrefixes();
}

// Fold the cmod wrappers around the base right-to-left (the last blob
// modifier becomes the innermost), so the FIRST blob modifier is the
// outermost -- the ECMA-335 II.23.2.7 semantics the C# SignatureDecoder's
// reverse fold produces.
SignatureTypeWriter SignatureTypeProviderDecoder::FoldModifiers(
        std::vector<std::pair<SignatureTypeWriter, bool>>& modifiers,
        SignatureTypeWriter base) {
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
        base = provider_.GetModifiedType(std::move(it->first), std::move(base),
            it->second);
    }
    return base;
}

// The C# `DecodeMethodSignature` body: header, optional generic count,
// param count, return type, then the parameters (Sentinel before the
// optional tail sets RequiredParameterCount).
MethodSignatureT SignatureTypeProviderDecoder::DecodeMethodSignatureBody() {
    MethodSignatureT sig;
    sig.Header = SignatureHeader::Decode(Byte());
    if (sig.Header.IsGeneric)
        (void)CompressedUnsigned();  // generic parameter count (not consumed here)
    std::uint32_t paramCount = CompressedUnsigned();
    sig.ReturnType = DecodeTypeOrByRef();
    sig.RequiredParameterCount = paramCount;
    sig.ParameterTypes.reserve(paramCount);
    for (std::uint32_t i = 0; i < paramCount; i++) {
        if (PeekByte() == kSentinel) {
            Byte();
            sig.RequiredParameterCount = i;
        }
        sig.ParameterTypes.push_back(DecodeTypeOrByRef());
    }
    return sig;
}

SignatureTypeWriter SignatureTypeProviderDecoder::DecodeType(
        const std::uint8_t* data, std::size_t size,
        const MetadataGenericContext& genericContext) {
    cur_ = data;
    end_ = data + size;
    context_ = &genericContext;
    SignatureTypeWriter t = DecodeTypeWithPrefixes();
    if (cur_ != end_) Fail("trailing bytes after the type");
    context_ = nullptr;
    return t;
}

MethodSignatureT SignatureTypeProviderDecoder::DecodeMethodSignature(
        const std::uint8_t* data, std::size_t size,
        const MetadataGenericContext& genericContext) {
    cur_ = data;
    end_ = data + size;
    context_ = &genericContext;
    MethodSignatureT sig = DecodeMethodSignatureBody();
    if (cur_ != end_) Fail("trailing bytes after the method signature");
    context_ = nullptr;
    return sig;
}

// The SRM `MethodSpecification.DecodeSignature` shape: the leading 0x0A
// GENERICINST marker (ECMA-335 II.23.2.15), the compressed type-argument
// count, then that many full types.
std::vector<SignatureTypeWriter> SignatureTypeProviderDecoder::DecodeMethodSpecSignature(
        const std::uint8_t* data, std::size_t size,
        const MetadataGenericContext& genericContext) {
    cur_ = data;
    end_ = data + size;
    context_ = &genericContext;
    if (Byte() != 0x0A) Fail("bad method specification marker");
    std::uint32_t count = CompressedUnsigned();
    std::vector<SignatureTypeWriter> result;
    result.reserve(count);
    for (std::uint32_t i = 0; i < count; i++) {
        result.push_back(DecodeTypeWithPrefixes());
    }
    if (cur_ != end_) Fail("trailing bytes after the method specification");
    context_ = nullptr;
    return result;
}

} // namespace ILSpy::Decompiler::Metadata
