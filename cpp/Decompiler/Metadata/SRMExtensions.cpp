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

#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <stdexcept>
#include <string>

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
        case 0x1B:  // HandleKind.TypeSpecification
            // The C# decodes the TypeSpec signature blob through
            // FullTypeNameSignatureDecoder (not yet ported); the arm throws
            // rather than returning a wrong name -- the TypeDef/TypeRef arms
            // already cover every catch-clause and member-name type except
            // instantiated generics.
            throw std::logic_error("GetFullTypeName: the TypeSpec arm is not yet ported");
        default:
            // The C# `throw new ArgumentOutOfRangeException()`.
            throw std::out_of_range("GetFullTypeName: unsupported handle kind");
    }
}

} // namespace ILSpy::Decompiler::Metadata
