
// C++-only replacement for the slice of System.Reflection.Metadata the
// DisassemblerSignatureTypeProvider port consumes (the SRM gap fill, per the
// PORT_PLAN.md section 5.1 methodology): the SRM `ISignatureTypeProvider<
// TType, TGenericContext>` contract, the `SignatureDecoder<TType,
// TGenericContext>` II.23.2 blob walker that drives it, and the value types
// the contract names (`PrimitiveTypeCode`, `SignatureCallingConvention`,
// `SignatureHeader`, `ArrayShape`, `MethodSignature<TType>`).
//
// The C# contract IS generic over TType, and the port follows: both the
// contract and the walker are class templates. The walker deduces everything
// from the provider (`SignatureTypeProviderDecoder decoder(provider, module)`
// -- CTAD; TProvider::TType is the provider's result type). Two providers
// instantiate the contract today: the writer provider
// (Disassembler/DisassemblerSignatureTypeProvider, TType = the deferred text
// writer below -- every ILDasm render) and the size decoder
// (FieldValueSizeDecoder, TType = int -- SRMExtensions.GetInitialValue's
// FieldValueSizeDecoder, the byte size of a HasFieldRVA field's initial
// value).
//
// The writer TType model is `SignatureTypeWriter` -- a deferred text writer the
// caller invokes with the ILNameSyntax to render at (the C# `Action<
// ILNameSyntax>` the DisassemblerSignatureTypeProvider instantiates the
// contract with). Writers compose by wrapping (a `SZArray<int32>` writer
// invokes its element writer then appends `[]`), so the decode produces a
// tree of deferred writers that render exactly like ILDasm when called.
//
// The C++-only walker mirrors the structural coverage of the IType-returning
// SignatureDecoder (Metadata/SignatureDecoder.cpp, validated at 100% mscorlib
// decode coverage): all ELEMENT_TYPE primitives, Ptr/ByRef, Class/ValueType
// (TypeDefOrRefEncoded coded indices), Var/MVar, SZArray, Array (with shape),
// GenericInst (the generic definition itself may be a TypeSpec), FnPtr (an
// embedded method signature), Object, TypedReference, and the CModReqd/
// CModOpt/Pinned prefixes -- which here CALL the provider (the C# behavior
// the IType walker skips, because the text rendering needs the wrapper).
// Blob access for nested TypeSpec rows goes through a MetadataFile public
// read (the pimpl constraint: the winmd database stays inside
// MetadataFile.cpp), so the walker only ever holds raw blob bytes.

#pragma once

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"  // ILNameSyntax

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// The C# `System.Reflection.Metadata.PrimitiveTypeCode` (ECMA-335 II.23.1.16
// element types that need no further bytes) -- the provider's GetPrimitiveType
// source domain. The port carries the 19 codes a signature blob can carry
// (the raw byte remaps onto this enum; the code values match the ECMA bytes).
enum class PrimitiveTypeCode : std::uint8_t {
    Void = 0x01,
    Boolean = 0x02,
    Char = 0x03,
    SByte = 0x04,
    Byte = 0x05,
    Int16 = 0x06,
    UInt16 = 0x07,
    Int32 = 0x08,
    UInt32 = 0x09,
    Int64 = 0x0A,
    UInt64 = 0x0B,
    Single = 0x0C,
    Double = 0x0D,
    String = 0x0E,
    TypedReference = 0x16,
    IntPtr = 0x18,
    UIntPtr = 0x19,
    Object = 0x1C,
};

// The C# `System.Reflection.Metadata.SignatureCallingConvention` (ECMA-335
// II.23.2.1 calling-convention bytes 0x00-0x05 plus the 0x09 unmanaged
// generic marker's convention form; the GENERIC bit 0x10 and the has-this
// bits 0x20/0x40 are carried on SignatureHeader, not here).
enum class SignatureCallingConvention : std::uint8_t {
    Default = 0x00,
    CDecl = 0x01,
    StdCall = 0x02,
    ThisCall = 0x03,
    FastCall = 0x04,
    VarArgs = 0x05,
    Unmanaged = 0x09,
};

// The C# `System.Reflection.Metadata.SignatureHeader` -- one byte split into
// the calling convention (low 4 bits), the generic marker (0x10), and the
// has-this (0x20) / explicit-this (0x40) flags.
struct SignatureHeader {
    SignatureCallingConvention CallingConvention = SignatureCallingConvention::Default;
    bool HasThis = false;
    bool HasExplicitThis = false;
    bool IsGeneric = false;

    // The C# `byte RawValue => _rawValue` -- the reconstructed header byte
    // (every bit maps to a field: the low nibble is the calling
    // convention, 0x10 the generic marker, 0x20/0x40 the has-this flags).
    std::uint8_t RawValue() const
    {
        return static_cast<std::uint8_t>(
            static_cast<std::uint8_t>(CallingConvention)
            | (IsGeneric ? 0x10 : 0x00)
            | (HasThis ? 0x20 : 0x00)
            | (HasExplicitThis ? 0x40 : 0x00));
    }

    // The ECMA-335 II.23.2.1 header-byte decode (the C# ctor over the raw
    // byte). An unknown convention byte keeps Default (the C# would throw on
    // an out-of-range cast; corrupt blobs are not expected through the
    // disassembler path -- a documented divergence confined to bad metadata).
    static SignatureHeader Decode(std::uint8_t raw) {
        SignatureHeader h;
        std::uint8_t cc = raw & 0x0F;
        if (cc <= 0x05) {
            h.CallingConvention = static_cast<SignatureCallingConvention>(cc);
        } else if (cc == 0x09) {
            h.CallingConvention = SignatureCallingConvention::Unmanaged;
        } else {
            h.CallingConvention = SignatureCallingConvention::Default;
        }
        h.IsGeneric = (raw & 0x10) != 0;
        h.HasThis = (raw & 0x20) != 0;
        h.HasExplicitThis = (raw & 0x40) != 0;
        return h;
    }

    // The C# `bool IsInstance` -- `HasThis && !HasExplicitThis`.
    bool IsInstance() const { return HasThis && !HasExplicitThis; }
};

// The C# `System.Reflection.Metadata.ArrayShape` -- the multi-dimensional
// array shape decoded after the element type of an ELEMENT_TYPE_ARRAY.
struct ArrayShape {
    std::uint32_t Rank = 0;
    std::vector<std::int32_t> Sizes;
    std::vector<std::int32_t> LowerBounds;
};

// The deferred text writer -- the writer provider's TType for the
// signature-provider contract (the C# `Action<ILNameSyntax>`).
using SignatureTypeWriter = std::function<void(Disassembler::ILNameSyntax)>;

// The C# `System.Reflection.Metadata.MethodSignature<TType>` -- the
// provider-contract method-signature value type (the FnPtr arm decodes one;
// DecodeMethodSignature returns one), generic over the provider's result
// type. The writer instantiation keeps the port's established
// `MethodSignatureT` name (the alias below); the size provider
// (FieldValueSizeDecoder) instantiates the contract at TType = int.
template <typename TType>
struct ProviderMethodSignature {
    SignatureHeader Header;
    TType ReturnType;
    std::vector<TType> ParameterTypes;
    std::uint32_t RequiredParameterCount = 0;
    std::uint32_t GenericParameterCount = 0;
};

// The writer instantiation -- the C# `MethodSignature<
// Action<ILNameSyntax>>`. The parameter list and return type hold the
// deferred writers; `RequiredParameterCount` distinguishes the vararg
// prefix (the Sentinel byte position, or the full count for non-vararg);
// `GenericParameterCount` carries the arity the header's GENERIC flag
// prefixes (the compressed count between the header byte and the parameter
// count -- the SortByNameProcessor method sort key renders it as `N).
using MethodSignatureT = ProviderMethodSignature<SignatureTypeWriter>;

// The C# `System.Reflection.Metadata.ISignatureTypeProvider<TType,
// TGenericContext>` -- one pure-virtual per C# member, with the generic
// context always the provider's generic-context type -- the C#
// `ISignatureTypeProvider<TType, TGenericContext>` is generic over BOTH; the
// port carries the context type as the SECOND template parameter, defaulting
// to the port's `MetadataGenericContext` value type so the writer/size
// providers (the disassembler paths) keep their single-argument
// instantiations unchanged. A provider exposes its result type as the member
// typedef `TType` and its context type as `TGenericContext` (the template
// parameters the C# spells at the instantiation). The concrete writer
// implementation is the DisassemblerSignatureTypeProvider (Disassembler/);
// the size implementation is the FieldValueSizeDecoder (SRMExtensions); the
// type-system implementation is the TypeProvider (TypeSystem/, context = the
// GenericContext VAR/MVAR scope).
template <typename TType, typename TGenericContext = MetadataGenericContext>
class ISignatureTypeProvider {
public:
    virtual ~ISignatureTypeProvider() = default;

    virtual TType GetPrimitiveType(PrimitiveTypeCode typeCode) = 0;
    // The C# `(MetadataReader, TypeDefinitionHandle, byte rawTypeKind)`; the
    // handle ports as the raw 0x02 token.
    virtual TType GetTypeFromDefinition(std::uint32_t typeDefToken,
        std::uint8_t rawTypeKind) = 0;
    // The C# `(MetadataReader, TypeReferenceHandle, byte rawTypeKind)`; the
    // handle ports as the raw 0x01 token.
    virtual TType GetTypeFromReference(std::uint32_t typeRefToken,
        std::uint8_t rawTypeKind) = 0;
    // The C# `(MetadataReader, TGenericContext, TypeSpecificationHandle,
    // byte rawTypeKind)` -- the arm that decodes the TypeSpec row's signature
    // blob through the provider again.
    virtual TType GetTypeFromSpecification(std::uint32_t typeSpecToken,
        std::uint8_t rawTypeKind, const TGenericContext& genericContext) = 0;
    virtual TType GetSZArrayType(TType elementType) = 0;
    virtual TType GetPointerType(TType elementType) = 0;
    virtual TType GetByReferenceType(TType elementType) = 0;
    virtual TType GetPinnedType(TType elementType) = 0;
    virtual TType GetArrayType(TType elementType,
        const ArrayShape& shape) = 0;
    virtual TType GetGenericInstantiation(TType genericType,
        std::vector<TType> typeArguments) = 0;
    virtual TType GetGenericTypeParameter(
        const TGenericContext& genericContext, int index) = 0;
    virtual TType GetGenericMethodParameter(
        const TGenericContext& genericContext, int index) = 0;
    virtual TType GetModifiedType(TType modifier,
        TType unmodifiedType, bool isRequired) = 0;
    virtual TType GetFunctionPointerType(
        const ProviderMethodSignature<TType>& signature) = 0;
};

// The element-type bytes the walker switches over (ECMA-335 II.23.1.16).
constexpr std::uint8_t kSentinel = 0x41;   // ELEMENT_TYPE_SENTINEL (vararg)
constexpr std::uint8_t kByRef = 0x10;      // ELEMENT_TYPE_BYREF
constexpr std::uint8_t kPtr = 0x0F;        // ELEMENT_TYPE_PTR
constexpr std::uint8_t kValueType = 0x11;  // ELEMENT_TYPE_VALUETYPE
constexpr std::uint8_t kClass = 0x12;      // ELEMENT_TYPE_CLASS
constexpr std::uint8_t kVar = 0x13;        // ELEMENT_TYPE_VAR
constexpr std::uint8_t kArray = 0x14;      // ELEMENT_TYPE_ARRAY
constexpr std::uint8_t kGenericInst = 0x15;
constexpr std::uint8_t kFnPtr = 0x1B;
constexpr std::uint8_t kSzArray = 0x1D;
constexpr std::uint8_t kMVar = 0x1E;
constexpr std::uint8_t kCModReqd = 0x1F;
constexpr std::uint8_t kCModOpt = 0x20;
constexpr std::uint8_t kPinned = 0x45;

// The PrimitiveTypeCode for an element-type byte that maps onto the enum
// (else nullopt -- the caller handles the composite cases).
inline std::optional<PrimitiveTypeCode> PrimitiveCode(std::uint8_t et) {
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

// The C# `System.Reflection.Metadata.SignatureDecoder<TType, TGenericContext>`
// -- the provider-driven II.23.2 blob walker, generic over the provider (the
// C# is generic over TType; the provider determines it -- `TProvider::TType`).
// Stateful over one blob (the byte cursor) and reusable across blobs (each
// Decode* call resets the cursor). Malformed blobs throw std::logic_error
// (the C# throws BadImageFormatException; the disassembler path treats both
// as fatal-for-the-blob, a documented divergence confined to bad metadata).
// Header-only: the template bodies are inline (every provider is a local of
// its driving caller -- the provider-outlives-writers contract).
template <typename TProvider>
class SignatureTypeProviderDecoder {
public:
    // The provider's result type (the C# TType the walker is generic over;
    // the provider exposes it -- `TProvider::TType`, the contract's template
    // parameter, spelled where the C# names TType at the instantiation).
    using TType = typename TProvider::TType;
    // The provider's generic-context type (the C# TGenericContext; the
    // provider exposes it -- every provider declares the context it scopes
    // VAR/MVAR and the TypeSpec recursion through).
    using TGenericContext = typename TProvider::TGenericContext;

    // The provider callbacks the walker drives, and the module the nested
    // TypeSpec blob reads go through (the C# passes the MetadataReader).
    SignatureTypeProviderDecoder(TProvider& provider,
        const MetadataFile& module)
        : provider_(provider), module_(module) {}

    // The C# `DecodeType(ref BlobReader, TGenericContext)` entry -- decodes
    // exactly one full type (cmod/pinned prefixes included) from the blob.
    // The generic context scopes Var/MVar naming and the TypeSpec recursion.
    TType DecodeType(const std::uint8_t* data, std::size_t size,
        const TGenericContext& genericContext);
    // The C# `DecodeMethodSignature(ref BlobReader, TGenericContext)` entry.
    ProviderMethodSignature<TType> DecodeMethodSignature(const std::uint8_t* data,
        std::size_t size, const TGenericContext& genericContext);
    // The SRM `MethodSpecification.DecodeSignature` shape: a compressed
    // type-argument count followed by that many full types (the MethodSpec
    // Instantiation blob). Trailing bytes throw std::logic_error (the same
    // strict-blob convention as the other entries).
    std::vector<TType> DecodeMethodSpecSignature(
        const std::uint8_t* data, std::size_t size,
        const TGenericContext& genericContext);
    // The SRM `StandaloneSignature.DecodeLocalSignature` shape: the Local-
    // Variables signature-kind nibble (0x7), then the compressed local count,
    // then that many full types (the C# caller checks GetKind first and
    // renders the " /* wrong signature kind */" comment itself).
    std::vector<TType> DecodeLocalSignature(
        const std::uint8_t* data, std::size_t size,
        const TGenericContext& genericContext);

private:
    TProvider& provider_;
    const MetadataFile& module_;

    // The open blob (reset per Decode* call; nested decodes save/restore).
    const std::uint8_t* cur_ = nullptr;
    const std::uint8_t* end_ = nullptr;
    const TGenericContext* context_ = nullptr;

    [[noreturn]] void Fail(const char* what);
    std::uint8_t Byte();
    std::uint8_t PeekByte();
    std::uint32_t CompressedUnsigned();
    std::int32_t CompressedSigned();

    // The C# DecodeTypeHandle family: a TypeDefOrRefEncoded coded index
    // resolved to a result (a TypeSpec tag recurses through the module's
    // blob read and GetTypeFromSpecification).
    TType DecodeTypeDefOrRefEncoded(std::uint8_t rawTypeKind);
    // The C# DecodeType + DecodeTypeFromSignature: cmod/pinned prefixes then
    // the base element type. The C# SignatureDecoder reads the modifiers into
    // a list and folds them around the base right-to-left, so the FIRST blob
    // modifier is the outermost wrapper (ECMA-335 II.23.2.7 semantics).
    TType DecodeTypeWithPrefixes();
    // The C# DecodeTypeOrByRef -- a leading ELEMENT_TYPE_BYREF wraps.
    TType DecodeTypeOrByRef();
    // The C# DecodeMethodSignature body (used by the entry and by FnPtr).
    ProviderMethodSignature<TType> DecodeMethodSignatureBody();
    // The TypeSpec row's raw signature blob (the MetadataFile read the
    // pimpl constraint requires).
    std::vector<std::uint8_t> ReadTypeSpecBlob(std::uint32_t typeSpecToken);

    // Fold the cmod wrappers around the base right-to-left, so the
    // FIRST blob modifier is the outermost wrapper (ECMA-335 II.23.2.7
    // semantics -- the C# SignatureDecoder reads the modifiers into a list
    // and folds them in reverse).
    TType FoldModifiers(
        std::vector<std::pair<TType, bool>>&& modifiers,
        TType base);
};

template <typename TProvider>
void SignatureTypeProviderDecoder<TProvider>::Fail(const char* what) {
    throw std::logic_error(std::string("SignatureTypeProviderDecoder: ") + what);
}

template <typename TProvider>
std::uint8_t SignatureTypeProviderDecoder<TProvider>::Byte() {
    if (cur_ >= end_) Fail("blob truncated");
    return *cur_++;
}

template <typename TProvider>
std::uint8_t SignatureTypeProviderDecoder<TProvider>::PeekByte() {
    if (cur_ >= end_) Fail("blob truncated");
    return *cur_;
}

template <typename TProvider>
std::uint32_t SignatureTypeProviderDecoder<TProvider>::CompressedUnsigned() {
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
template <typename TProvider>
std::int32_t SignatureTypeProviderDecoder<TProvider>::CompressedSigned() {
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

template <typename TProvider>
std::vector<std::uint8_t> SignatureTypeProviderDecoder<TProvider>::ReadTypeSpecBlob(
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
template <typename TProvider>
typename SignatureTypeProviderDecoder<TProvider>::TType
SignatureTypeProviderDecoder<TProvider>::DecodeTypeDefOrRefEncoded(
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

// The C# `DecodeType`: the cmod/pinned prefixes (each modifier is a full type,
// itself possibly a TypeSpec) then the base element type. The C#
// SignatureDecoder reads the modifiers into a list and folds them around the
// base right-to-left, so the FIRST blob modifier is the outermost wrapper
// (ECMA-335 II.23.2.7 semantics).
template <typename TProvider>
typename SignatureTypeProviderDecoder<TProvider>::TType
SignatureTypeProviderDecoder<TProvider>::DecodeTypeWithPrefixes() {
    std::vector<std::pair<TType, bool>> modifiers;  // (result, required)
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
            TType inner = DecodeTypeWithPrefixes();
            // The C# decode folds the already-read cmod modifiers AROUND the
            // pinned element (the net065 locals render
            // "uint8& pinned modopt(IsExplicitlyDereferenced)"): the pinned
            // arm is not a leaf -- discarding the modifiers here dropped
            // every cmod that preceded the pinned marker.
            return FoldModifiers(std::move(modifiers),
                provider_.GetPinnedType(std::move(inner)));
        }
        break;
    }
    std::uint8_t et = Byte();
    if (auto prim = PrimitiveCode(et)) {
        TType base = provider_.GetPrimitiveType(*prim);
        return FoldModifiers(std::move(modifiers), std::move(base));
    }
    TType base;
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
            TType elem = DecodeTypeWithPrefixes();
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
            TType def = DecodeTypeDefOrRefEncoded(marker);
            std::uint32_t argCount = CompressedUnsigned();
            std::vector<TType> args;
            args.reserve(argCount);
            for (std::uint32_t i = 0; i < argCount; i++)
                args.push_back(DecodeTypeWithPrefixes());
            base = provider_.GetGenericInstantiation(std::move(def), std::move(args));
            break;
        }
        case kFnPtr: {
            // The C# SignatureDecoder parses the embedded method signature
            // from the SAME cursor (no blob boundary).
            ProviderMethodSignature<TType> sig = DecodeMethodSignatureBody();
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
template <typename TProvider>
typename SignatureTypeProviderDecoder<TProvider>::TType
SignatureTypeProviderDecoder<TProvider>::DecodeTypeOrByRef() {
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
template <typename TProvider>
typename SignatureTypeProviderDecoder<TProvider>::TType
SignatureTypeProviderDecoder<TProvider>::FoldModifiers(
        std::vector<std::pair<TType, bool>>&& modifiers,
        TType base) {
    for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
        base = provider_.GetModifiedType(std::move(it->first), std::move(base),
            it->second);
    }
    return base;
}

// The C# `DecodeMethodSignature` body: header, optional generic count,
// param count, return type, then the parameters (Sentinel before the
// optional tail sets RequiredParameterCount).
template <typename TProvider>
ProviderMethodSignature<typename SignatureTypeProviderDecoder<TProvider>::TType>
SignatureTypeProviderDecoder<TProvider>::DecodeMethodSignatureBody() {
    ProviderMethodSignature<TType> sig;
    sig.Header = SignatureHeader::Decode(Byte());
    if (sig.Header.IsGeneric)
        sig.GenericParameterCount = CompressedUnsigned();
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

template <typename TProvider>
typename SignatureTypeProviderDecoder<TProvider>::TType
SignatureTypeProviderDecoder<TProvider>::DecodeType(
        const std::uint8_t* data, std::size_t size,
        const TGenericContext& genericContext) {
    cur_ = data;
    end_ = data + size;
    context_ = &genericContext;
    TType t = DecodeTypeWithPrefixes();
    if (cur_ != end_) Fail("trailing bytes after the type");
    context_ = nullptr;
    return t;
}

template <typename TProvider>
ProviderMethodSignature<typename SignatureTypeProviderDecoder<TProvider>::TType>
SignatureTypeProviderDecoder<TProvider>::DecodeMethodSignature(
        const std::uint8_t* data, std::size_t size,
        const TGenericContext& genericContext) {
    cur_ = data;
    end_ = data + size;
    context_ = &genericContext;
    ProviderMethodSignature<TType> sig = DecodeMethodSignatureBody();
    if (cur_ != end_) Fail("trailing bytes after the method signature");
    context_ = nullptr;
    return sig;
}

// The SRM `MethodSpecification.DecodeSignature` shape: the leading 0x0A
// GENERICINST marker (ECMA-335 II.23.2.15), the compressed type-argument
// count, then that many full types.
template <typename TProvider>
std::vector<typename SignatureTypeProviderDecoder<TProvider>::TType>
SignatureTypeProviderDecoder<TProvider>::DecodeMethodSpecSignature(
        const std::uint8_t* data, std::size_t size,
        const TGenericContext& genericContext) {
    cur_ = data;
    end_ = data + size;
    context_ = &genericContext;
    if (Byte() != 0x0A) Fail("bad method specification marker");
    std::uint32_t count = CompressedUnsigned();
    // The C# `DecodeTypeSequence`: `if (num == 0) throw new
    // BadImageFormatException(System.SR.
    // SignatureTypeSequenceMustHaveAtLeastOneElement)` -- a zero-count
    // type sequence is rejected before any element decode (the exact .NET
    // message; a std::logic_error so the existing malformed-blob catch
    // conventions keep working).
    if (count == 0)
        throw std::logic_error(
            "Signature type sequence must have at least one element.");
    std::vector<TType> result;
    result.reserve(count);
    for (std::uint32_t i = 0; i < count; i++) {
        result.push_back(DecodeTypeWithPrefixes());
    }
    if (cur_ != end_) Fail("trailing bytes after the method specification");
    context_ = nullptr;
    return result;
}

// The SRM `StandaloneSignature.DecodeLocalSignature` shape: the
// LocalVariables kind nibble, the compressed local count, then that many
// full types.
template <typename TProvider>
std::vector<typename SignatureTypeProviderDecoder<TProvider>::TType>
SignatureTypeProviderDecoder<TProvider>::DecodeLocalSignature(
        const std::uint8_t* data, std::size_t size,
        const TGenericContext& genericContext) {
    cur_ = data;
    end_ = data + size;
    context_ = &genericContext;
    std::uint8_t header = Byte();
    if ((header & 0x0F) != 0x07) Fail("bad local signature kind");
    std::uint32_t count = CompressedUnsigned();
    // The C# `DecodeTypeSequence` zero-count rejection (the exact .NET
    // message -- the DecodeMethodSpecSignature note above): a LOCAL_SIG
    // with zero locals throws where an empty vector would silently
    // decompile as a locals-free body.
    if (count == 0)
        throw std::logic_error(
            "Signature type sequence must have at least one element.");
    std::vector<TType> result;
    result.reserve(count);
    for (std::uint32_t i = 0; i < count; i++) {
        result.push_back(DecodeTypeWithPrefixes());
    }
    if (cur_ != end_) Fail("trailing bytes after the local signature");
    context_ = nullptr;
    return result;
}

} // namespace ILSpy::Decompiler::Metadata
