// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE, NONINFRINGEMENT, OR AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the SecurityDeclarationDecoder nested class of
// ICSharpCode.Decompiler/Disassembler/ReflectionDisassembler.cs (lines
// 487-675) -- the ICustomAttributeTypeProvider<(PrimitiveTypeCode Code,
// string Name)> implementation the custom-attribute-blob decode
// (WriteDecodedCustomAttributeBlob) and the permission-set decode
// (TryDecodeSecurityDeclaration) instantiate the CustomAttributeDecoderT
// template with. The C# nests the class inside ReflectionDisassembler;
// the port makes it a separate class in the Disassembler namespace (the
// C++-nested-class-in-header-only convention is awkward, the
// OverloadResolution.Candidate D506 precedent).
//
// The provider's TType is the C# `(PrimitiveTypeCode Code, string Name)`
// tuple: the underlying primitive code (0 for a non-primitive / unresolved
// type) plus the nullable display name ("enum <full name>" for a resolved
// enum, the full / assembly-qualified type name otherwise, "type" for the
// System.Type placeholder).
//
// C#-to-C++ porting decisions:
//  * The C# tuple ports to the SecurityDeclarationType struct (Code / Name
//    members over the same fields; the null string Name is the primitive
//    marker -- the port's std::optional<std::string>).
//  * The ctor's `output` parameter is kept although the C# never reads the
//    field (the C# keeps `readonly ITextOutput output;` unused; the
//    TryDecodeSecurityDeclaration construction site passes it).
//  * `GetTypeFromDefinition` / `GetTypeFromReference` receive the row's own
//    module (the C# reader parameter) -- the SecurityDeclaration paths
//    always decode the attribute rows of the ctor's module, but the
//    reader-parameterized shape is the ICustomAttributeTypeProvider
//    contract the decoder template drives.
//  * `ResolveType`'s tuple return ports to the ResolvedType struct (the
//    module pointer is null for the unresolved assembly arm; the
//    TypeDef token 0 is the nil handle).
//  * `FindType`'s `goto restart` ports to the restart bool loop -- after
//    switching the candidate list to a match's nested types the SAME name
//    segment is matched again against the nested list (the C#'s shipped
//    behavior: `typeDefinitions = typeDef.GetNestedTypes(); goto restart;`
//    re-enters the foreach with the same identifier).
//  * The `metadata.StringComparer.Equals` namespace-name comparison is the
//    ordinal case-sensitive == over the resolved strings.

#pragma once

#include "Decompiler/Metadata/SignatureTypeProvider.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Output { class ITextOutput; }

namespace ILSpy::Decompiler::Metadata {
class IAssemblyResolver;
class MetadataFile;
}

namespace ILSpy::Decompiler::Disassembler {

// The C# `(PrimitiveTypeCode Code, string Name)` tuple the decoder
// instantiates the SecurityDeclarationDecoder with (see the header comment).
struct SecurityDeclarationType {
    // The C# `Code` -- the underlying primitive code; 0 (kNoCode) for a
    // non-primitive / unresolved type.
    Metadata::PrimitiveTypeCode Code = static_cast<Metadata::PrimitiveTypeCode>(0);

    // The C# `Name` -- the nullable display name; nullopt is the primitive
    // marker (GetPrimitiveType's pair).
    std::optional<std::string> Name;

    bool operator==(const SecurityDeclarationType& other) const {
        return Code == other.Code && Name == other.Name;
    }
};

// The C# `ReflectionDisassembler.PrimitiveTypeCodeToString` (lines 835-866)
// -- the ILDasm primitive spellings the SZArray element-name composition and
// the WriteValue/WriteSimpleValue renderers consume. A private static of the
// ReflectionDisassembler in C#; a free function here so the
// SecurityDeclarationDecoder (a separate class in the port) and the
// ReflectionDisassembler members share the one spelling table. Any code the
// C# switch does not name renders "unknown" (the default arm).
std::string PrimitiveTypeCodeToString(Metadata::PrimitiveTypeCode typeCode);

// The C# `class SecurityDeclarationDecoder :
// ICustomAttributeTypeProvider<(PrimitiveTypeCode Code, string Name)>`
// (ReflectionDisassembler.cs lines 489-675).
class SecurityDeclarationDecoder {
public:
    using TType = SecurityDeclarationType;

    // The C# `SecurityDeclarationDecoder(ITextOutput output,
    // IAssemblyResolver resolver, MetadataFile module)`.
    SecurityDeclarationDecoder(Output::ITextOutput& output,
        Metadata::IAssemblyResolver* resolver,
        const Metadata::MetadataFile& module);

    // The C# `GetPrimitiveType(PrimitiveTypeCode typeCode)`:
    // `(typeCode, null)`.
    TType GetPrimitiveType(Metadata::PrimitiveTypeCode typeCode);

    // The C# `GetSystemType()` -- the System.Type placeholder pair.
    TType GetSystemType();

    // The C# `GetSZArrayType((Code, Name) elementType)`:
    // `(elementType.Code, (elementType.Name ?? PrimitiveTypeCodeToString(
    // elementType.Code)) + "[]")`.
    TType GetSZArrayType(TType elementType);

    // The C# `GetTypeFromDefinition(MetadataReader reader,
    // TypeDefinitionHandle handle, byte rawTypeKind)`: the full type name,
    // with the enum arm ("enum " + the name, the underlying code) over the
    // SRMExtensions IsEnum read.
    TType GetTypeFromDefinition(const Metadata::MetadataFile& reader,
        std::uint32_t typeDefToken, std::uint8_t rawTypeKind);

    // The C# `GetTypeFromReference(MetadataReader reader,
    // TypeReferenceHandle handle, byte rawTypeKind)`: the full type name
    // composed with the declaring module (the resolution-scope walk's
    // assembly-qualified form), then resolved -- the resolved enum gets the
    // "enum " prefix and the underlying code; the unresolved module keeps
    // the composed name and the zero code.
    TType GetTypeFromReference(const Metadata::MetadataFile& reader,
        std::uint32_t typeRefToken, std::uint8_t rawTypeKind);

    // The C# `GetTypeFromSerializedName(string name)`: the null resolver or
    // the unresolved name throws EnumUnderlyingTypeResolveException; the
    // resolved enum gets the "enum " prefix and the underlying code; any
    // other resolved type returns the ORIGINAL name with the zero code.
    TType GetTypeFromSerializedName(const std::string& name);

    // The C# `GetUnderlyingEnumType((Code, Name) type)`: `type.Code`.
    Metadata::PrimitiveTypeCode GetUnderlyingEnumType(TType type);

    // The C# `IsSystemType((Code, Name) type)`: `"type" == type.Name`.
    bool IsSystemType(TType type);

private:
    // The C# `(MetadataFile, TypeDefinitionHandle) ResolveType(string
    // typeName, MetadataFile module)` tuple return.
    struct ResolvedType {
        const Metadata::MetadataFile* Module = nullptr;
        std::uint32_t TypeDefToken = 0;  // 0 is the nil handle
    };

    ResolvedType ResolveType(const std::string& typeName,
        const Metadata::MetadataFile& module);
    // The C# `FindType(MetadataFile currentModule, string[] name)` local: the
    // namespace walk (dot-separated segments against the namespace tree),
    // then the type walk (the segment against the namespace's types, nested
    // segments through the match's nested types, the restart quirk above).
    static std::uint32_t FindType(const Metadata::MetadataFile& currentModule,
        const std::vector<std::string>& name);
    // The C# `GetDeclaringModule(TypeReferenceHandle handle)` local of
    // GetTypeFromReference: the resolution-scope walk -- the declaring
    // TypeRef recurses, the AssemblyRef yields the full assembly name, the
    // ModuleRef the module name, anything else null.
    static std::optional<std::string> GetDeclaringModule(
        const Metadata::MetadataFile& reader, std::uint32_t typeRefToken);
    // The C# `TryResolveMscorlib(out MetadataFile mscorlib)` local: the
    // cached resolver answer (a miss is not cached).
    bool TryResolveMscorlib(const Metadata::MetadataFile*& mscorlib);

    Output::ITextOutput& output_;  // the C# field; never read (see the header)
    Metadata::IAssemblyResolver* resolver_;
    const Metadata::MetadataFile& module_;
    const Metadata::MetadataFile* mscorlib_ = nullptr;
};

} // namespace ILSpy::Decompiler::Disassembler
