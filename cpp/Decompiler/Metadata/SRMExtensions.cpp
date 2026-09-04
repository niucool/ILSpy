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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "Decompiler/Metadata/SRMExtensions.hpp"

#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

namespace {

using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;

// Recursion depth guard for the declaring-type walks: a malformed cyclic
// chain (A's declaring TypeRef is B, B's is A) would recurse forever; the C#
// stack-overflows on such metadata. The port stops the walk at 64 levels
// (the IsFieldCompilerGeneratedOrInCompilerGeneratedClass guard depth) and
// treats the current row as top-level -- a documented deviation confined to
// corrupt metadata (well-formed chains are 1-3 levels deep).
constexpr int kMaxNestingWalkDepth = 64;

// The TypeReferenceHandle reader body (SRMExtensions.cs
// `GetFullTypeName(this TypeReferenceHandle, MetadataReader)`): split the
// row's name, then either compose the top-level name or recurse into the
// declaring TypeRef and nest. The C# BadImageFormatException catch arms (the
// name -> "TR{token:x8}" and declaringTypeHandle -> default fallbacks) are
// unreachable through the port's winmd-validated never-throw row reads -- an
// unreadable row surfaces as nullopt -> the out_of_range below, a documented
// divergence confined to corrupt metadata.
FullTypeName GetFullTypeNameFromReferenceImpl(const MetadataFile& metadata,
                                               std::uint32_t typeRefToken, int depth) {
    auto info = metadata.GetTypeRefNameInfo(typeRefToken);
    if (!info)
        throw std::out_of_range("GetFullTypeNameFromReference: invalid TypeRef token");
    int typeParameterCount = 0;
    std::string name = TypeSystem::SplitTypeParameterCountFromReflectionName(
        info->Name, typeParameterCount);
    if (info->DeclaringTypeRefToken == 0 || depth >= kMaxNestingWalkDepth) {
        return FullTypeName(TopLevelTypeName(info->Namespace, name, typeParameterCount));
    }
    return GetFullTypeNameFromReferenceImpl(metadata, info->DeclaringTypeRefToken, depth + 1)
        .NestedType(name, typeParameterCount);
}

// The TypeDefinitionHandle reader body (SRMExtensions.cs
// `GetFullTypeName(this TypeDefinitionHandle, MetadataReader)` /
// `GetFullTypeName(this TypeDefinition, MetadataReader)` -- the port collapses
// the two C# overloads, which share the row reads and differ only in the
// handle-vs-row entry): split the row's name, then either compose the
// top-level name or recurse into the declaring TypeDef and nest.
FullTypeName GetFullTypeNameFromDefinitionImpl(const MetadataFile& metadata,
                                               std::uint32_t typeDefToken, int depth) {
    auto info = metadata.GetTypeDefNameInfo(typeDefToken);
    if (!info)
        throw std::out_of_range("GetFullTypeNameFromDefinition: invalid TypeDef token");
    int typeParameterCount = 0;
    std::string name = TypeSystem::SplitTypeParameterCountFromReflectionName(
        info->Name, typeParameterCount);
    if (info->DeclaringTypeToken == 0 || depth >= kMaxNestingWalkDepth) {
        return FullTypeName(TopLevelTypeName(info->Namespace, name, typeParameterCount));
    }
    return GetFullTypeNameFromDefinitionImpl(metadata, info->DeclaringTypeToken, depth + 1)
        .NestedType(name, typeParameterCount);
}

} // namespace

// The TypeSpec blob walker (the FullTypeNameSignatureDecoder shrinking
// semantics over the II.23.2 element-type grammar -- see the header).
namespace {

// A one-byte-at-a-time cursor over the TypeSpec row's signature blob (the
// ILParser/BlobReader cursor convention). Reads past the end throw
// std::out_of_range (the C# BlobReader overflow the SignatureDecoder throws
// as BadImageFormatException).
class TypeNameBlobCursor {
public:
    TypeNameBlobCursor(const std::uint8_t* data, std::size_t size)
        : data_(data), size_(size) {}

    std::uint32_t Byte() {
        if (pos_ >= size_)
            throw std::out_of_range("TypeSpec blob: truncated");
        return data_[pos_++];
    }

    // The C# BlobReader.ReadCompressedInteger (II.23.2 unsigned compressed
    // integers: 1 byte for 0x00-0x7F, 2 for 0x80-0x3FFF, 4 for the 0xC0 form).
    std::uint32_t CompressedUnsigned() {
        std::uint32_t first = Byte();
        if ((first & 0x80) == 0) return first;
        if ((first & 0xC0) == 0x80)
            return ((first & 0x3Fu) << 8) | Byte();
        if ((first & 0xE0) == 0xC0)
            return ((first & 0x1Fu) << 24) | (Byte() << 16) | (Byte() << 8)
                | Byte();
        throw std::out_of_range("TypeSpec blob: invalid compressed integer");
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t pos_ = 0;
};

// The C# SignatureDecoder.ReadTypeHandle: a compressed TypeDefOrRefEncoded
// index -- 2 tag bits (TypeDef 0 / TypeRef 1 / TypeSpec 2), the rest the
// 1-based row. Returns 0 for the nil coding.
std::uint32_t ReadTypeDefOrRefEncoded(TypeNameBlobCursor& cursor) {
    std::uint32_t raw = cursor.CompressedUnsigned();
    std::uint32_t rid = raw >> 2;
    if (rid == 0) return 0;
    switch (raw & 0x3u) {
        case 0: return (0x02u << 24) | rid;
        case 1: return (0x01u << 24) | rid;
        case 2: return (0x1Bu << 24) | rid;
        default: return 0;
    }
}

FullTypeName DecodeTypeNameBlob(const MetadataFile& metadata,
                                 TypeNameBlobCursor& cursor);

// One element-type step of the shrinking walk: the cmod/pinned wrappers and
// the array/pointer/byref/szarray wrappers strip to their element type, a
// generic instantiation keeps only its generic head (the type arguments are
// decoded and discarded by the C# -- nothing follows them in a TypeSpec blob,
// so the result is identical), VAR/MVAR/FNPTR decode to the empty
// FullTypeName, and the primitives resolve through the known-type table.
FullTypeName DecodeTypeNameBlob(const MetadataFile& metadata,
                                 TypeNameBlobCursor& cursor) {
    std::uint8_t elementType = static_cast<std::uint8_t>(cursor.Byte());
    switch (elementType) {
        case 0x1F:  // ELEMENT_TYPE_CMOD_REQD
        case 0x20:  // ELEMENT_TYPE_CMOD_OPT
            // GetModifiedType returns the unmodified type; the modifier's
            // TypeDefOrRefEncoded index is consumed.
            (void)ReadTypeDefOrRefEncoded(cursor);
            return DecodeTypeNameBlob(metadata, cursor);
        case 0x45:  // ELEMENT_TYPE_PINNED (GetPinnedType)
            return DecodeTypeNameBlob(metadata, cursor);
        case 0x0F:  // ELEMENT_TYPE_PTR (GetPointerType -> element)
        case 0x10:  // ELEMENT_TYPE_BYREF (GetByReferenceType -> element)
        case 0x1D:  // ELEMENT_TYPE_SZARRAY (GetSZArrayType -> element)
            return DecodeTypeNameBlob(metadata, cursor);
        case 0x14: {  // ELEMENT_TYPE_ARRAY: element, then the shape (consumed
                      // and discarded -- GetArrayType returns the element)
            FullTypeName element = DecodeTypeNameBlob(metadata, cursor);
            std::uint32_t rank = cursor.CompressedUnsigned();
            (void)rank;
            std::uint32_t numSizes = cursor.CompressedUnsigned();
            for (std::uint32_t i = 0; i < numSizes; i++)
                (void)cursor.CompressedUnsigned();
            std::uint32_t numLoBounds = cursor.CompressedUnsigned();
            for (std::uint32_t i = 0; i < numLoBounds; i++)
                (void)cursor.CompressedUnsigned();
            return element;
        }
        case 0x15: {  // ELEMENT_TYPE_GENERICINST: the raw kind byte, the
                      // generic head, the argument count -- GetGeneric-
                      // Instantiation returns the genericType only
            (void)cursor.Byte();  // rawTypeKind: 0x11 valuetype / 0x12 class
            std::uint32_t generic = ReadTypeDefOrRefEncoded(cursor);
            std::uint32_t argumentCount = cursor.CompressedUnsigned();
            (void)argumentCount;  // the arguments are decoded and discarded
            return GetFullTypeName(metadata, generic);
        }
        case 0x11:  // ELEMENT_TYPE_VALUETYPE
        case 0x12:  // ELEMENT_TYPE_CLASS
            return GetFullTypeName(metadata, ReadTypeDefOrRefEncoded(cursor));
        case 0x13:  // ELEMENT_TYPE_VAR (GetGenericTypeParameter: default)
        case 0x1E:  // ELEMENT_TYPE_MVAR (GetGenericMethodParameter: default)
        case 0x1B:  // ELEMENT_TYPE_FNPTR (GetFunctionPointerType: default)
            return FullTypeName{};
        default: {
            // The primitive element types (0x01-0x0E, 0x16, 0x18, 0x19, 0x1C):
            // FullTypeNameSignatureDecoder.GetPrimitiveType resolves the code
            // through KnownTypeReference.Get(typeCode.ToKnownTypeCode()) and
            // an unknown code decodes to the default (an empty name).
            auto code = static_cast<PrimitiveTypeCode>(elementType);
            switch (code) {
                case PrimitiveTypeCode::Void:
                case PrimitiveTypeCode::Boolean:
                case PrimitiveTypeCode::Char:
                case PrimitiveTypeCode::SByte:
                case PrimitiveTypeCode::Byte:
                case PrimitiveTypeCode::Int16:
                case PrimitiveTypeCode::UInt16:
                case PrimitiveTypeCode::Int32:
                case PrimitiveTypeCode::UInt32:
                case PrimitiveTypeCode::Int64:
                case PrimitiveTypeCode::UInt64:
                case PrimitiveTypeCode::Single:
                case PrimitiveTypeCode::Double:
                case PrimitiveTypeCode::String:
                case PrimitiveTypeCode::TypedReference:
                case PrimitiveTypeCode::IntPtr:
                case PrimitiveTypeCode::UIntPtr:
                case PrimitiveTypeCode::Object: {
                    const auto* ktr = TypeSystem::KnownTypeReference::Get(
                        ToKnownTypeCode(code));
                    if (ktr == nullptr) return FullTypeName{};
                    return FullTypeName(TopLevelTypeName(
                        std::string(ktr->Namespace()), std::string(ktr->Name()),
                        ktr->TypeParameterCount()));
                }
                default:
                    throw std::logic_error(
                        "TypeSpec blob: unrecognized ELEMENT_TYPE");
            }
        }
    }
}

} // namespace

std::string ToILSyntax(SignatureCallingConvention callConv) {
    switch (callConv) {
        case SignatureCallingConvention::Default:
            return "default";
        case SignatureCallingConvention::CDecl:
            return "unmanaged cdecl";
        case SignatureCallingConvention::StdCall:
            return "unmanaged stdcall";
        case SignatureCallingConvention::ThisCall:
            return "unmanaged thiscall";
        case SignatureCallingConvention::FastCall:
            return "unmanaged fastcall";
        case SignatureCallingConvention::VarArgs:
            return "vararg";
        case SignatureCallingConvention::Unmanaged:
            return "unmanaged";
    }
    // Unreachable for the seven ported members (a fresh convention byte maps
    // to one of them in the header decode); kept as the C# fallback arm.
    return "default";
}

FullTypeName GetFullTypeNameFromReference(const MetadataFile& metadata,
                                          std::uint32_t typeRefToken) {
    // The C# `if (handle.IsNil) throw new ArgumentNullException` -- the nil
    // handle is the port's zero token.
    if (typeRefToken == 0)
        throw std::invalid_argument("GetFullTypeNameFromReference: nil token");
    return GetFullTypeNameFromReferenceImpl(metadata, typeRefToken, 0);
}

FullTypeName GetFullTypeNameFromDefinition(const MetadataFile& metadata,
                                           std::uint32_t typeDefToken) {
    if (typeDefToken == 0)
        throw std::invalid_argument("GetFullTypeNameFromDefinition: nil token");
    return GetFullTypeNameFromDefinitionImpl(metadata, typeDefToken, 0);
}

FullTypeName GetFullTypeName(const MetadataFile& metadata, std::uint32_t entityToken) {
    if (entityToken == 0)
        throw std::invalid_argument("GetFullTypeName: nil token");
    switch (entityToken >> 24) {
        case 0x02:  // HandleKind.TypeDefinition
            return GetFullTypeNameFromDefinition(metadata, entityToken);
        case 0x01:  // HandleKind.TypeReference
            return GetFullTypeNameFromReference(metadata, entityToken);
        case 0x1B: {  // HandleKind.TypeSpecification: the row's signature
            // blob through the shrinking decoder below.
            auto blob = metadata.GetTypeSpecSignatureBlob(entityToken);
            if (!blob)
                throw std::out_of_range(
                    "GetFullTypeName: invalid TypeSpec token");
            return GetFullTypeNameFromSpecification(
                metadata, blob->data(), blob->size());
        }
        default:
            // The C# `throw new ArgumentOutOfRangeException()`.
            throw std::out_of_range("GetFullTypeName: unsupported handle kind");
    }
}

FullTypeName GetFullTypeNameFromSpecification(const MetadataFile& metadata,
    const std::uint8_t* data, std::size_t size) {
    TypeNameBlobCursor cursor(data, size);
    return DecodeTypeNameBlob(metadata, cursor);
}

std::uint32_t GetDeclaringType(const MetadataFile& metadata,
    std::uint32_t entityToken) {
    if (entityToken == 0)
        throw std::invalid_argument("GetDeclaringType: nil token");
    switch (entityToken >> 24) {
        case 0x02: {  // TypeDefinition: the NestedClass-table declaring type
            auto info = metadata.GetTypeDefNameInfo(entityToken);
            if (!info)
                throw std::out_of_range(
                    "GetDeclaringType: invalid TypeDef token");
            return info->DeclaringTypeToken;  // 0 = top-level (the nil handle)
        }
        case 0x01: {  // TypeReference: the resolution-scope walk (a TypeRef
            // scope is the declaring TypeRef; every other scope is nil)
            auto info = metadata.GetTypeRefNameInfo(entityToken);
            if (!info)
                throw std::out_of_range(
                    "GetDeclaringType: invalid TypeRef token");
            return info->DeclaringTypeRefToken;  // 0 = top-level
        }
        case 0x04:  // FieldDefinition
            return metadata.GetFieldDeclaringTypeToken(entityToken);
        case 0x06:  // MethodDefinition
            return metadata.GetMethodDeclaringTypeToken(entityToken);
        case 0x0A: {  // MemberReference: mr.Parent
            auto mr = metadata.GetMemberReference(entityToken);
            if (!mr)
                throw std::out_of_range(
                    "GetDeclaringType: invalid MemberRef token");
            return mr->ParentToken;
        }
        case 0x2B: {  // MethodSpecification: recurse into the method
            auto ms = metadata.GetMethodSpecification(entityToken);
            if (!ms || ms->MethodToken == 0)
                throw std::out_of_range(
                    "GetDeclaringType: invalid MethodSpec token");
            return GetDeclaringType(metadata, ms->MethodToken);
        }
        default:
            // The C# default arm (ArgumentOutOfRangeException); the deferred
            // TypeSpec/Event/Property arms land here too (see the header).
            throw std::out_of_range(
                "GetDeclaringType: unsupported handle kind");
    }
}

// ---------------------------------------------------------------------------
// FieldValueSizeDecoder (SRMExtensions.cs -- the sealed class nested behind
// GetInitialValue): the provider that decodes a FIELD signature to the byte
// size of the field's initial value.
// ---------------------------------------------------------------------------

FieldValueSizeDecoder::FieldValueSizeDecoder(const MetadataFile& module)
    : module_(module) {
    // The C# ctor's null-typeSystem shape (the ReflectionDisassembler
    // DisassembleField caller): `module` is null, so the pointer size is
    // IntPtr.Size -- the x64 process (the ilspycmd CLI runs x64, and the
    // typeSystem-bearing arm defers with the type system).
}

// The C# switch: Boolean/Byte/SByte 1, Char/Int16/UInt16 2, Int32/UInt32/
// Single 4, Int64/UInt64/Double 8, IntPtr/UIntPtr the pointer size; Void,
// String, TypedReference, and Object fall to the default 0 (the reference
// types have no inline initial value).
int FieldValueSizeDecoder::GetPrimitiveType(PrimitiveTypeCode typeCode) {
    switch (typeCode) {
        case PrimitiveTypeCode::Boolean:
        case PrimitiveTypeCode::Byte:
        case PrimitiveTypeCode::SByte:
            return 1;
        case PrimitiveTypeCode::Char:
        case PrimitiveTypeCode::Int16:
        case PrimitiveTypeCode::UInt16:
            return 2;
        case PrimitiveTypeCode::Int32:
        case PrimitiveTypeCode::UInt32:
        case PrimitiveTypeCode::Single:
            return 4;
        case PrimitiveTypeCode::Int64:
        case PrimitiveTypeCode::UInt64:
        case PrimitiveTypeCode::Double:
            return 8;
        case PrimitiveTypeCode::IntPtr:
        case PrimitiveTypeCode::UIntPtr:
            return pointerSize_;
        default:
            return 0;
    }
}

int FieldValueSizeDecoder::GetTypeFromDefinition(std::uint32_t typeDefToken,
    std::uint8_t rawTypeKind) {
    // The C# `reader.GetTypeDefinition(handle).GetLayout().Size` -- the
    // ClassLayout ClassSize (0 for a type without a layout row).
    return static_cast<int>(module_.GetTypeLayoutSize(typeDefToken));
}

int FieldValueSizeDecoder::GetTypeFromReference(std::uint32_t typeRefToken,
    std::uint8_t rawTypeKind) {
    // The C# `module?.ResolveType(handle, new GenericContext())` -- null under
    // the null-typeSystem shape, so the arm reads 0 (the typeSystem-bearing
    // resolution defers with the type system).
    return 0;
}

int FieldValueSizeDecoder::GetTypeFromSpecification(std::uint32_t typeSpecToken,
    std::uint8_t rawTypeKind, const MetadataGenericContext& genericContext) {
    // The C# `reader.GetTypeSpecification(handle).DecodeSignature(this,
    // genericContext)` -- the TypeSpec row's signature blob decoded through
    // this same provider (a fresh decoder over it).
    auto blob = module_.GetTypeSpecSignatureBlob(typeSpecToken);
    if (!blob)
        throw std::out_of_range("FieldValueSizeDecoder: invalid TypeSpec token");
    SignatureTypeProviderDecoder<FieldValueSizeDecoder> decoder(*this, module_);
    return decoder.DecodeType(blob->data(), blob->size(), genericContext);
}

// The C# `GetSZArrayType(int elementType) => GetPrimitiveType(
// PrimitiveTypeCode.Object)` -- an SZArray has no inline initial value.
int FieldValueSizeDecoder::GetSZArrayType(int elementType) {
    return GetPrimitiveType(PrimitiveTypeCode::Object);
}

int FieldValueSizeDecoder::GetPointerType(int elementType) {
    return pointerSize_;
}

int FieldValueSizeDecoder::GetByReferenceType(int elementType) {
    return pointerSize_;
}

// The C# `GetPinnedType(int elementType) => elementType`.
int FieldValueSizeDecoder::GetPinnedType(int elementType) {
    return elementType;
}

// The C# `GetArrayType(int elementType, ArrayShape shape) =>
// GetPrimitiveType(PrimitiveTypeCode.Object)`.
int FieldValueSizeDecoder::GetArrayType(int elementType, const ArrayShape& shape) {
    return GetPrimitiveType(PrimitiveTypeCode::Object);
}

// The C# `GetGenericInstantiation(int genericType, ImmutableArray<int>
// typeArguments) => genericType` -- the generic head's own size.
int FieldValueSizeDecoder::GetGenericInstantiation(int genericType,
    std::vector<int> typeArguments) {
    return genericType;
}

// The C# `GetGenericMethodParameter/GetGenericTypeParameter(..., int index)
// => 0`.
int FieldValueSizeDecoder::GetGenericTypeParameter(
    const MetadataGenericContext& genericContext, int index) {
    return 0;
}

int FieldValueSizeDecoder::GetGenericMethodParameter(
    const MetadataGenericContext& genericContext, int index) {
    return 0;
}

// The C# `GetModifiedType(int modifier, int unmodifiedType, bool isRequired)
// => unmodifiedType`.
int FieldValueSizeDecoder::GetModifiedType(int modifier, int unmodifiedType,
    bool isRequired) {
    return unmodifiedType;
}

// The C# `GetFunctionPointerType(MethodSignature<int> signature) =>
// pointerSize` -- the signature's contents are irrelevant to the size.
int FieldValueSizeDecoder::GetFunctionPointerType(
    const ProviderMethodSignature<int>& signature) {
    return pointerSize_;
}

} // namespace ILSpy::Decompiler::Metadata
