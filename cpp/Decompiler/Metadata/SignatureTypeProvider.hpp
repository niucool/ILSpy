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

// C++-only replacement for the slice of System.Reflection.Metadata the
// DisassemblerSignatureTypeProvider port consumes (the SRM gap fill, per the
// PORT_PLAN.md section 5.1 methodology): the SRM `ISignatureTypeProvider<
// TType, TGenericContext>` contract, the `SignatureDecoder<TType,
// TGenericContext>` II.23.2 blob walker that drives it, and the value types
// the contract names (`PrimitiveTypeCode`, `SignatureCallingConvention`,
// `SignatureHeader`, `ArrayShape`, `MethodSignature<TType>`).
//
// The TType model is `SignatureTypeWriter` -- a deferred text writer the
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

// The deferred text writer -- the port's TType for the signature-provider
// contract (the C# `Action<ILNameSyntax>`).
using SignatureTypeWriter = std::function<void(Disassembler::ILNameSyntax)>;

// The C# `System.Reflection.Metadata.MethodSignature<TType>` instantiated at
// TType = SignatureTypeWriter. The parameter list and return type hold the
// deferred writers; `RequiredParameterCount` distinguishes the vararg
// prefix (the Sentinel byte position, or the full count for non-vararg).
struct MethodSignatureT {
    SignatureHeader Header;
    SignatureTypeWriter ReturnType;
    std::vector<SignatureTypeWriter> ParameterTypes;
    std::uint32_t RequiredParameterCount = 0;
};

// The C# `System.Reflection.Metadata.ISignatureTypeProvider<TType,
// TGenericContext>` at TType = SignatureTypeWriter -- one pure-virtual per
// C# member, with the generic context always the port's `MetadataGenericContext`
// value type. The concrete implementation is the
// DisassemblerSignatureTypeProvider (Disassembler/).
class ISignatureTypeProvider {
public:
    virtual ~ISignatureTypeProvider() = default;

    virtual SignatureTypeWriter GetPrimitiveType(PrimitiveTypeCode typeCode) = 0;
    // The C# `(MetadataReader, TypeDefinitionHandle, byte rawTypeKind)`; the
    // handle ports as the raw 0x02 token.
    virtual SignatureTypeWriter GetTypeFromDefinition(std::uint32_t typeDefToken,
        std::uint8_t rawTypeKind) = 0;
    // The C# `(MetadataReader, TypeReferenceHandle, byte rawTypeKind)`; the
    // handle ports as the raw 0x01 token.
    virtual SignatureTypeWriter GetTypeFromReference(std::uint32_t typeRefToken,
        std::uint8_t rawTypeKind) = 0;
    // The C# `(MetadataReader, MetadataGenericContext, TypeSpecificationHandle,
    // byte rawTypeKind)` -- the arm that decodes the TypeSpec row's signature
    // blob through the provider again.
    virtual SignatureTypeWriter GetTypeFromSpecification(std::uint32_t typeSpecToken,
        std::uint8_t rawTypeKind, const MetadataGenericContext& genericContext) = 0;
    virtual SignatureTypeWriter GetSZArrayType(SignatureTypeWriter elementType) = 0;
    virtual SignatureTypeWriter GetPointerType(SignatureTypeWriter elementType) = 0;
    virtual SignatureTypeWriter GetByReferenceType(SignatureTypeWriter elementType) = 0;
    virtual SignatureTypeWriter GetPinnedType(SignatureTypeWriter elementType) = 0;
    virtual SignatureTypeWriter GetArrayType(SignatureTypeWriter elementType,
        const ArrayShape& shape) = 0;
    virtual SignatureTypeWriter GetGenericInstantiation(SignatureTypeWriter genericType,
        std::vector<SignatureTypeWriter> typeArguments) = 0;
    virtual SignatureTypeWriter GetGenericTypeParameter(
        const MetadataGenericContext& genericContext, int index) = 0;
    virtual SignatureTypeWriter GetGenericMethodParameter(
        const MetadataGenericContext& genericContext, int index) = 0;
    virtual SignatureTypeWriter GetModifiedType(SignatureTypeWriter modifier,
        SignatureTypeWriter unmodifiedType, bool isRequired) = 0;
    virtual SignatureTypeWriter GetFunctionPointerType(
        const MethodSignatureT& signature) = 0;
};

// The C# `System.Reflection.Metadata.SignatureDecoder<TType, TGenericContext>`
// at TType = SignatureTypeWriter -- the provider-driven II.23.2 blob walker.
// Stateful over one blob (the byte cursor) and reusable across blobs (each
// Decode* call resets the cursor). Malformed blobs throw std::logic_error
// (the C# throws BadImageFormatException; the disassembler path treats both
// as fatal-for-the-blob, a documented divergence confined to bad metadata).
class SignatureTypeProviderDecoder {
public:
    // The provider callbacks the walker drives, and the module the nested
    // TypeSpec blob reads go through (the C# passes the MetadataReader).
    SignatureTypeProviderDecoder(ISignatureTypeProvider& provider,
        const MetadataFile& module)
        : provider_(provider), module_(module) {}

    // The C# `DecodeType(ref BlobReader, TGenericContext)` entry -- decodes
    // exactly one full type (cmod/pinned prefixes included) from the blob.
    // The generic context scopes Var/MVar naming and the TypeSpec recursion.
    SignatureTypeWriter DecodeType(const std::uint8_t* data, std::size_t size,
        const MetadataGenericContext& genericContext);
    // The C# `DecodeMethodSignature(ref BlobReader, TGenericContext)` entry.
    MethodSignatureT DecodeMethodSignature(const std::uint8_t* data, std::size_t size,
        const MetadataGenericContext& genericContext);
    // The SRM `MethodSpecification.DecodeSignature` shape: a compressed
    // type-argument count followed by that many full types (the MethodSpec
    // Instantiation blob). Trailing bytes throw std::logic_error (the same
    // strict-blob convention as the other entries).
    std::vector<SignatureTypeWriter> DecodeMethodSpecSignature(
        const std::uint8_t* data, std::size_t size,
        const MetadataGenericContext& genericContext);

private:
    ISignatureTypeProvider& provider_;
    const MetadataFile& module_;

    // The open blob (reset per Decode* call; nested decodes save/restore).
    const std::uint8_t* cur_ = nullptr;
    const std::uint8_t* end_ = nullptr;
    const MetadataGenericContext* context_ = nullptr;

    [[noreturn]] void Fail(const char* what);
    std::uint8_t Byte();
    std::uint8_t PeekByte();
    std::uint32_t CompressedUnsigned();
    std::int32_t CompressedSigned();

    // The C# DecodeTypeHandle family: a TypeDefOrRefEncoded coded index
    // resolved to a writer (a TypeSpec tag recurses through the module's
    // blob read and GetTypeFromSpecification).
    SignatureTypeWriter DecodeTypeDefOrRefEncoded(std::uint8_t rawTypeKind);
    // The C# DecodeType + DecodeTypeFromSignature: cmod/pinned prefixes then
    // the base element type.
    SignatureTypeWriter DecodeTypeWithPrefixes();
    // The C# DecodeTypeOrByRef -- a leading ELEMENT_TYPE_BYREF wraps.
    SignatureTypeWriter DecodeTypeOrByRef();
    // The C# DecodeMethodSignature body (used by the entry and by FnPtr).
    MethodSignatureT DecodeMethodSignatureBody();
    // The TypeSpec row's raw signature blob (the MetadataFile read the
    // pimpl constraint requires).
    std::vector<std::uint8_t> ReadTypeSpecBlob(std::uint32_t typeSpecToken);

    // Fold the cmod wrappers around the base type right-to-left, so the
    // FIRST blob modifier is the outermost wrapper (ECMA-335 II.23.2.7
    // semantics -- the C# SignatureDecoder reads the modifiers into a list
    // and folds them in reverse).
    SignatureTypeWriter FoldModifiers(
        std::vector<std::pair<SignatureTypeWriter, bool>>& modifiers,
        SignatureTypeWriter base);
};

} // namespace ILSpy::Decompiler::Metadata
