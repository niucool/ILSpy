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

#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <any>
#include <cstddef>
#include <optional>
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

// The ExportedType row reader body (SRMExtensions.cs
// `GetFullTypeName(this ExportedType, MetadataReader)`): split the row's
// name, then either compose the top-level name or recurse into the outer
// ExportedType the Implementation column targets and nest. The C# recurses
// unconditionally on a cyclic Implementation chain and stack-overflows; the
// port stops at the family's depth cap and treats the current row as
// top-level -- a documented divergence confined to corrupt metadata.
FullTypeName GetFullTypeNameFromExportedTypeImpl(const MetadataFile& metadata,
                                                  std::uint32_t exportedTypeToken,
                                                  int depth) {
    auto row = metadata.GetExportedType(exportedTypeToken);
    if (!row)
        throw std::out_of_range(
            "GetFullTypeNameFromExportedType: invalid token");
    int typeParameterCount = 0;
    std::string name = TypeSystem::SplitTypeParameterCountFromReflectionName(
        row->Name, typeParameterCount);
    // HandleKind.ExportedType == 0x27: the nested-forwarder chain.
    if ((row->ImplementationToken >> 24) == 0x27
        && depth < kMaxNestingWalkDepth) {
        return GetFullTypeNameFromExportedTypeImpl(
                   metadata, row->ImplementationToken, depth + 1)
            .NestedType(name, typeParameterCount);
    }
    return FullTypeName(TopLevelTypeName(row->Namespace, name, typeParameterCount));
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

FullTypeName GetFullTypeNameFromExportedType(const MetadataFile& metadata,
                                            std::uint32_t exportedTypeToken) {
    // The C# overload takes the ExportedType ROW struct (never nil); the
    // port's token-shaped entry carries the reader-family nil contract
    // itself (the GetFullTypeNameFromReference/Definition convention).
    if (exportedTypeToken == 0)
        throw std::invalid_argument(
            "GetFullTypeNameFromExportedType: nil token");
    return GetFullTypeNameFromExportedTypeImpl(metadata, exportedTypeToken, 0);
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
// The known-type / kind predicate family (SRMExtensions.cs lines 59-135 and
// 246-375): IsKnownType over an EntityHandle (the TypeRef/TypeDef/TypeSpec
// arms with the nested/scope rejections), SignatureIsKnownType (the TypeSpec
// blob walk), GetAttributeType / HasKnownAttribute (the attribute-class
// classification), and IsValueType / IsEnum / IsDelegate (the TypeDef kind
// predicates MetadataTypeDefinition's ctor consumes).
// ---------------------------------------------------------------------------

namespace {

// The .NET 10 `HandleKind.ToString()` spelling for a token's table byte --
// the kind ids equal the II.24.2.6 table ids with the *Ptr tables skipped
// (probed from the 10.0.8 runtime: 0=ModuleDefinition, 4=FieldDefinition,
// 6=MethodDefinition, ...); a byte outside the enum renders its decimal
// value (the .NET enum ToString fallback).
std::string HandleKindName(std::uint32_t kind) {
    switch (kind) {
        case 0: return "ModuleDefinition";
        case 1: return "TypeReference";
        case 2: return "TypeDefinition";
        case 4: return "FieldDefinition";
        case 6: return "MethodDefinition";
        case 8: return "Parameter";
        case 9: return "InterfaceImplementation";
        case 10: return "MemberReference";
        case 11: return "Constant";
        case 12: return "CustomAttribute";
        case 14: return "DeclarativeSecurityAttribute";
        case 17: return "StandaloneSignature";
        case 20: return "EventDefinition";
        case 23: return "PropertyDefinition";
        case 25: return "MethodImplementation";
        case 26: return "ModuleReference";
        case 27: return "TypeSpecification";
        case 32: return "AssemblyDefinition";
        case 35: return "AssemblyReference";
        case 38: return "AssemblyFile";
        case 39: return "ExportedType";
        case 40: return "ManifestResource";
        case 42: return "GenericParameter";
        case 43: return "MethodSpecification";
        case 44: return "GenericParameterConstraint";
        case 48: return "Document";
        case 49: return "MethodDebugInformation";
        case 50: return "LocalScope";
        case 51: return "LocalVariable";
        case 52: return "LocalConstant";
        case 53: return "ImportScope";
        case 55: return "CustomDebugInformation";
        default: return std::to_string(kind);
    }
}

// The C# `TopLevelTypeName.IsKnownType(KnownTypeCode)` extension
// (TypeSystemExtensions.cs line 391): `typeName ==
// KnownTypeReference.Get(knownType).TypeName`. The none-code arm is
// unreachable for every caller (the primitive/Enum/ValueType/MulticastDelegate
// codes are all real); the null guard mirrors the C# null table row.
bool IsKnownTypeName(const TypeSystem::TopLevelTypeName& knownType,
                     TypeSystem::KnownTypeCode knownTypeCode) {
    const TypeSystem::KnownTypeReference* reference =
        TypeSystem::KnownTypeReference::Get(knownTypeCode);
    return reference != nullptr && reference->TypeName() == knownType;
}

// The C# `private static bool IsKnownType(EntityHandle handle, MetadataReader
// reader, TopLevelTypeName knownType)` (SRMExtensions.cs line 258): the core
// behind both public overloads. The whole row-read switch sits inside the C#
// `try { } catch (BadImageFormatException) { return false; }`, so a corrupt
// row is FALSE -- the port maps the reader's throws (std::out_of_range /
// std::invalid_argument, the winmd seek/row family) to the same false. (The
// C# reads the name/namespace HANDLES in the try and compares them outside;
// a handle whose string read throws propagates out of the C# -- the port's
// never-throw name reads make that shape false instead, a divergence
// confined to a corrupt name column with a readable row.)
bool IsKnownTypeCore(const MetadataFile& metadata, std::uint32_t entityToken,
                     const TypeSystem::TopLevelTypeName& knownType);

// The C# `private static bool SignatureIsKnownType(MetadataReader reader,
// TopLevelTypeName knownType, ref BlobReader blob)` (SRMExtensions.cs line
// 316): the ELEMENT_TYPE walk over a TypeSpec signature blob -- the primitive
// comparisons, the never-matching pointer/byref/array/fnptr/var arms, the
// cmod skip-then-recurse, the GENERICINST head recursion, and the
// CLASS/VALUETYPE coded-index recursion into the IsKnownType core. The entry
// read is the C# TryReadCompressedInteger (FALSE at end-of-blob); every later
// read is the throwing form whose BadImageFormatException the caller's catch
// takes as false.
bool SignatureIsKnownType(const MetadataFile& metadata, TypeNameBlobCursor& cursor,
                          const TypeSystem::TopLevelTypeName& knownType) {
    std::uint32_t typeCode;
    try {
        typeCode = cursor.CompressedUnsigned();
    } catch (const std::out_of_range&) {
        return false;  // the C# `!blob.TryReadCompressedInteger(...)`
    }
    switch (typeCode) {
        case 0x01:  // ELEMENT_TYPE_VOID
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Void);
        case 0x02:  // ELEMENT_TYPE_BOOLEAN
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Boolean);
        case 0x03:  // ELEMENT_TYPE_CHAR
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Char);
        case 0x04:  // ELEMENT_TYPE_I1
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::SByte);
        case 0x05:  // ELEMENT_TYPE_U1
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Byte);
        case 0x06:  // ELEMENT_TYPE_I2
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Int16);
        case 0x07:  // ELEMENT_TYPE_U2
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::UInt16);
        case 0x08:  // ELEMENT_TYPE_I4
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Int32);
        case 0x09:  // ELEMENT_TYPE_U4
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::UInt32);
        case 0x0A:  // ELEMENT_TYPE_I8
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Int64);
        case 0x0B:  // ELEMENT_TYPE_U8
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::UInt64);
        case 0x0C:  // ELEMENT_TYPE_R4
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Single);
        case 0x0D:  // ELEMENT_TYPE_R8
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Double);
        case 0x0E:  // ELEMENT_TYPE_STRING
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::String);
        case 0x16:  // ELEMENT_TYPE_TYPEDBYREF
            return IsKnownTypeName(knownType,
                                    TypeSystem::KnownTypeCode::TypedReference);
        case 0x18:  // ELEMENT_TYPE_I
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::IntPtr);
        case 0x19:  // ELEMENT_TYPE_U
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::UIntPtr);
        case 0x1C:  // ELEMENT_TYPE_OBJECT
            return IsKnownTypeName(knownType, TypeSystem::KnownTypeCode::Object);
        case 0x0F:  // ELEMENT_TYPE_PTR
        case 0x10:  // ELEMENT_TYPE_BYREF
        case 0x45:  // ELEMENT_TYPE_PINNED
        case 0x1D:  // ELEMENT_TYPE_SZARRAY
        case 0x1B:  // ELEMENT_TYPE_FNPTR
        case 0x14:  // ELEMENT_TYPE_ARRAY
        case 0x13:  // ELEMENT_TYPE_VAR
        case 0x1E:  // ELEMENT_TYPE_MVAR
            return false;
        case 0x1F:  // ELEMENT_TYPE_CMOD_REQD
        case 0x20:  // ELEMENT_TYPE_CMOD_OPT
            // The C# `blob.ReadTypeHandle(); // skip modifier` then recurse.
            ReadTypeDefOrRefEncoded(cursor);
            return SignatureIsKnownType(metadata, cursor, knownType);
        case 0x15:  // ELEMENT_TYPE_GENERICINST
            // The C# recurses into the generic type's own signature.
            return SignatureIsKnownType(metadata, cursor, knownType);
        case 0x11:  // ELEMENT_TYPE_VALUETYPE
        case 0x12:  // ELEMENT_TYPE_CLASS
            // The C# `IsKnownType(blob.ReadTypeHandle(), reader, knownType)`
            // -- a fresh IsKnownType invocation with its own try/catch.
            return IsKnownTypeCore(metadata, ReadTypeDefOrRefEncoded(cursor),
                                   knownType);
        default:
            return false;
    }
}

bool IsKnownTypeCore(const MetadataFile& metadata, std::uint32_t entityToken,
                     const TypeSystem::TopLevelTypeName& knownType) {
    if (entityToken == 0)
        return false;  // the C# `if (handle.IsNil) return false;`
    std::string name;
    std::string ns;
    try {
        switch (entityToken >> 24) {
            case 0x01: {  // HandleKind.TypeReference
                auto info = metadata.GetTypeRefNameInfo(entityToken);
                if (!info)
                    return false;  // the C# row-fetch BadImageFormatException
                // The C#: `if (tr.ResolutionScope.IsNil ||
                // tr.ResolutionScope.Kind == HandleKind.TypeReference)
                // return false;` -- ignore exported and nested types. The
                // scope read distinguishes the nil scope (Kind::None) from
                // every non-TypeRef scope.
                auto scope = metadata.GetTypeRefScopeInfo(entityToken);
                if (!scope)
                    return false;
                if (scope->Scope == TypeRefScopeInfo::Kind::None)
                    return false;
                if (scope->Scope == TypeRefScopeInfo::Kind::TypeRef)
                    return false;
                name = info->Name;
                ns = info->Namespace;
                break;
            }
            case 0x02: {  // HandleKind.TypeDefinition
                // The C# `if (td.IsNested) return false;` -- the
                // TypeAttributesExtensions bit trick: (flags & 0x6) != 0 is
                // true for every nested visibility (2..7) and false for
                // NotPublic/Public (the iteration-62 decompiled-SRM pin).
                if ((metadata.GetTypeDefAttributes(entityToken) & 0x6u) != 0)
                    return false;
                auto info = metadata.GetTypeDefNameInfo(entityToken);
                if (!info)
                    return false;
                name = info->Name;
                ns = info->Namespace;
                break;
            }
            case 0x1B: {  // HandleKind.TypeSpecification
                auto blob = metadata.GetTypeSpecSignatureBlob(entityToken);
                if (!blob)
                    return false;
                TypeNameBlobCursor cursor(blob->data(), blob->size());
                return SignatureIsKnownType(metadata, cursor, knownType);
            }
            default:
                return false;
        }
    } catch (const std::out_of_range&) {
        return false;  // the C# catch (BadImageFormatException)
    } catch (const std::invalid_argument&) {
        return false;
    }
    // The C# `reader.StringComparer.Equals(nameHandle, knownType.Name)`
    // with the NULL name of the default-entry `TopLevelTypeName`
    // (`KnownAttribute.None` -- the C# `default(TopLevelTypeName)` carries
    // null strings) throws ArgumentNullException ("Value cannot be null.
    // (Parameter 'value')") -- the observable shape of driving the
    // None/sentinel entry through the row classification. The port's
    // `TopLevelTypeName` cannot carry null; the default-entry SHAPE (both
    // strings empty -- no real table entry is nameless) is the stand-in.
    // The check sits AFTER the kind dispatch (exactly where the C# compare
    // fires), so the nested/nil-scope early returns stay ahead of it.
    if (knownType.Name().empty() && knownType.Namespace().empty())
        throw std::invalid_argument(
            "Value cannot be null. (Parameter 'value')");
    // The name/namespace comparison: a 0-arity known type compares the row
    // name ordinally; a generic known type splits the row name's backtick
    // arity and compares name AND count.
    if (knownType.TypeParameterCount() == 0) {
        if (name != knownType.Name())
            return false;
    } else {
        int typeParameterCount = 0;
        std::string splitName =
            TypeSystem::SplitTypeParameterCountFromReflectionName(
                name, typeParameterCount);
        if (typeParameterCount != knownType.TypeParameterCount()
            || splitName != knownType.Name())
            return false;
    }
    // The nil-vs-empty namespace distinction is not observable through the
    // result: the C# nil arm tests `knownType.Namespace.Length == 0` and the
    // non-nil arm compares the string -- both reduce to the row namespace
    // ("" for a nil column) equaling the known namespace (the port's name
    // read already collapsed the distinction).
    return ns == knownType.Namespace();
}

} // namespace

// The C# `public static bool IsKnownType(this EntityHandle handle,
// MetadataReader reader, KnownTypeCode knownType)` (line 246).
bool IsKnownType(const MetadataFile& metadata, std::uint32_t entityToken,
                 TypeSystem::KnownTypeCode knownType) {
    const TypeSystem::KnownTypeReference* reference =
        TypeSystem::KnownTypeReference::Get(knownType);
    if (reference == nullptr)
        throw std::runtime_error(
            "Object reference not set to an instance of an object.");
    return IsKnownTypeCore(metadata, entityToken, reference->TypeName());
}

// The C# `internal static bool IsKnownType(this EntityHandle handle,
// MetadataReader reader, KnownAttribute knownType)` (line 252).
bool IsKnownType(const MetadataFile& metadata, std::uint32_t entityToken,
                 TypeSystem::KnownAttribute knownAttribute) {
    return IsKnownTypeCore(metadata, entityToken,
                           TypeSystem::GetTypeName(knownAttribute));
}

// The C# `public static EntityHandle GetAttributeType(this SRM.CustomAttribute
// attribute, MetadataReader reader)` (line 606): the constructor's declaring
// type. The port takes the CustomAttribute ROW token (the C# takes the row
// struct); a bogus row token throws std::out_of_range (the reader-family
// row-fetch convention).
std::uint32_t GetAttributeType(const MetadataFile& metadata,
                               std::uint32_t attributeToken) {
    auto row = metadata.GetCustomAttribute(attributeToken);
    if (!row)
        throw std::out_of_range("GetAttributeType: invalid CustomAttribute token");
    switch (row->ConstructorToken >> 24) {
        case 0x06:  // HandleKind.MethodDefinition
            // The C# `md.GetDeclaringType()` throws BadImageFormatException
            // for a bogus row; the port's never-throw read yields the nil
            // token instead (the GetCustomAttribute nil-tag precedent).
            return metadata.GetMethodDeclaringTypeToken(row->ConstructorToken);
        case 0x0A: {  // HandleKind.MemberReference
            auto memberRef = metadata.GetMemberReference(row->ConstructorToken);
            if (!memberRef)
                throw std::out_of_range(
                    "GetAttributeType: invalid MemberRef token");
            return memberRef->ParentToken;
        }
        default:
            // The C# `throw new BadImageFormatException("Unexpected token
            // kind for attribute constructor: " + attribute.Constructor.Kind)`
            // -- the HandleKind name for the token's table byte (the
            // II.24.2.6 kind ids equal the table ids), decimal for a kind
            // outside the enum (the .NET ToString fallback).
            throw std::out_of_range(
                "Unexpected token kind for attribute constructor: "
                + HandleKindName(row->ConstructorToken >> 24));
    }
}

// The C# `internal static bool IsKnownAttribute(this SRM.CustomAttribute attr,
// MetadataReader metadata, KnownAttribute attrType)` (line 634):
// `attr.GetAttributeType(metadata).IsKnownType(metadata, attrType)` -- the
// GetAttributeType throw propagates (no catch here). Exported (moved out of
// the file-local anonymous namespace): the DecimalConstantHelper row walk
// drives it directly (the C# internal-extension surface the port exposes
// alongside HasKnownAttribute).
// The C# `public static Nullability? GetNullableContext(...)`
// (SRMExtensions.cs line 640): see the header.
std::optional<TypeSystem::Nullability> GetNullableContext(
    const MetadataFile& metadata, std::uint32_t entityToken) {
    for (std::uint32_t attributeToken :
         metadata.GetCustomAttributeTokens(entityToken)) {
        if (!IsKnownAttribute(metadata, attributeToken,
                              TypeSystem::KnownAttribute::NullableContext))
            continue;
        std::optional<Metadata::CustomAttributeRowInfo> row =
            metadata.GetCustomAttribute(attributeToken);
        if (!row)
            continue;
        try {
            // The C# `customAttribute.DecodeValue(Metadata.MetadataExtensions.
            // MinimalAttributeTypeProvider)` -- the minimal provider (the
            // MinimalCorlibTypeProvider wiring).
            Metadata::CustomAttributeDecoder decoder(
                metadata,
                Metadata::MinimalAttributeTypeProvider());
            Metadata::CustomAttributeValue value = decoder.DecodeValue(
                row->ConstructorToken,
                row->ValueBlob ? row->ValueBlob->data() : nullptr,
                row->ValueBlob ? row->ValueBlob->size() : 0);
            if (value.FixedArguments.size() == 1) {
                // The C# `value.FixedArguments[0].Value is byte b && b <= 2`
                // -- a real byte box (the decode-error rows are skipped by
                // the catch arms above).
                std::any boxed = value.FixedArguments[0].Value();
                if (auto b = std::any_cast<std::uint8_t>(&boxed)) {
                    if (*b <= 2)
                        return static_cast<TypeSystem::Nullability>(*b);
                }
            }
        } catch (const EnumUnderlyingTypeResolveException&) {
            // The C# `catch (EnumUnderlyingTypeResolveException) { continue; }`.
            continue;
        } catch (const std::invalid_argument&) {
            // The C# `catch (BadImageFormatException) { continue; }`.
            continue;
        }
    }
    return std::nullopt;
}

bool IsKnownAttribute(const MetadataFile& metadata,
                      std::uint32_t attributeToken,
                      TypeSystem::KnownAttribute attribute) {
    return IsKnownTypeCore(metadata, GetAttributeType(metadata, attributeToken),
                           TypeSystem::GetTypeName(attribute));
}

// The C# `public static bool HasKnownAttribute(this
// CustomAttributeHandleCollection customAttributes, MetadataReader metadata,
// KnownAttribute type)` (line 622). The port takes the PARENT token (the
// GetCustomAttributeTokens composition).
bool HasKnownAttribute(const MetadataFile& metadata, std::uint32_t entityToken,
                       TypeSystem::KnownAttribute attribute) {
    for (std::uint32_t attributeToken :
         metadata.GetCustomAttributeTokens(entityToken)) {
        if (IsKnownAttribute(metadata, attributeToken, attribute))
            return true;
    }
    return false;
}

// The C# `public static bool IsValueType(this TypeDefinition typeDefinition,
// MetadataReader reader)` (line 64).
bool IsValueType(const MetadataFile& metadata, std::uint32_t typeDefToken) {
    std::uint32_t baseType = metadata.GetBaseTypeToken(typeDefToken);
    if (baseType == 0)
        return false;  // the C# `if (baseType.IsNil) return false;`
    if (IsKnownType(metadata, baseType, TypeSystem::KnownTypeCode::Enum))
        return true;
    if (!IsKnownType(metadata, baseType, TypeSystem::KnownTypeCode::ValueType))
        return false;
    // The C# `var thisType = typeDefinition.GetFullTypeName(reader); return
    // !thisType.IsKnownType(KnownTypeCode.Enum);` -- the FullTypeName ==
    // TopLevelTypeName comparison over the known type's top-level name.
    TypeSystem::FullTypeName thisType =
        GetFullTypeNameFromDefinition(metadata, typeDefToken);
    return !(thisType == TypeSystem::FullTypeName(
                             TypeSystem::KnownTypeReference::Get(
                                 TypeSystem::KnownTypeCode::Enum)->TypeName()));
}

// The C# `public static bool IsEnum(this TypeDefinition typeDefinition,
// MetadataReader reader)` (line 82).
bool IsEnum(const MetadataFile& metadata, std::uint32_t typeDefToken) {
    std::uint32_t baseType = metadata.GetBaseTypeToken(typeDefToken);
    return baseType != 0
        && IsKnownType(metadata, baseType, TypeSystem::KnownTypeCode::Enum);
}

// The C# `public static bool IsEnum(this TypeDefinition typeDefinition,
// MetadataReader reader, out PrimitiveTypeCode underlyingType)` (line 96).
bool IsEnum(const MetadataFile& metadata, std::uint32_t typeDefToken,
            PrimitiveTypeCode& underlyingType) {
    underlyingType = static_cast<PrimitiveTypeCode>(0);  // the C# `= 0`
    std::uint32_t baseType = metadata.GetBaseTypeToken(typeDefToken);
    if (baseType == 0)
        return false;
    if (!IsKnownType(metadata, baseType, TypeSystem::KnownTypeCode::Enum))
        return false;
    for (const FieldInfo& field : metadata.GetFields(typeDefToken)) {
        // The C# `if ((field.Attributes & FieldAttributes.Static) != 0)
        // continue;` -- Static = 0x10 (II.23.1.5).
        if ((metadata.GetFieldAttributes(field.Token) & 0x10u) != 0)
            continue;
        auto blob = metadata.GetSignatureBlob(field.Token);
        if (!blob)
            throw std::out_of_range("IsEnum: invalid field signature blob");
        // `blob.ReadSignatureHeader().Kind != SignatureKind.Field` -- the
        // field calling-convention nibble is 0x06 (II.23.2).
        if (blob->empty() || ((*blob)[0] & 0x0Fu) != 0x06u)
            return false;
        // `underlyingType = (PrimitiveTypeCode)blob.ReadByte()` -- a
        // truncated byte read is the C# BadImageFormatException (the throw
        // propagates out of IsEnum; the port maps it to std::out_of_range).
        if (blob->size() < 2)
            throw std::out_of_range("IsEnum: truncated field signature blob");
        underlyingType = static_cast<PrimitiveTypeCode>((*blob)[1]);
        return true;
    }
    return false;
}

// The C# `public static bool IsDelegate(this TypeDefinition typeDefinition,
// MetadataReader reader)` (line 124).
bool IsDelegate(const MetadataFile& metadata, std::uint32_t typeDefToken) {
    std::uint32_t baseType = metadata.GetBaseTypeToken(typeDefToken);
    return baseType != 0
        && IsKnownType(metadata, baseType,
                      TypeSystem::KnownTypeCode::MulticastDelegate);
}

// The C# `internal static bool IsGeneratedName(string name)` (line 517).
bool IsGeneratedName(const std::string& name) {
    // The C# `name.StartsWith("<", StringComparison.Ordinal) ||
    // name.Contains("$")` -- ordinal in both cases (no culture fallback).
    return name.rfind("<", 0) == 0 || name.find('$') != std::string::npos;
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
