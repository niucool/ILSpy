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

// SecurityDeclarationDecoder.cs -- see the header's port conventions.

#include "Decompiler/Disassembler/SecurityDeclarationDecoder.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

namespace ILSpy::Decompiler::Disassembler {

std::string PrimitiveTypeCodeToString(Metadata::PrimitiveTypeCode typeCode) {
    switch (typeCode) {
        case Metadata::PrimitiveTypeCode::Boolean:
            return "bool";
        case Metadata::PrimitiveTypeCode::Byte:
            return "uint8";
        case Metadata::PrimitiveTypeCode::SByte:
            return "int8";
        case Metadata::PrimitiveTypeCode::Char:
            return "char";
        case Metadata::PrimitiveTypeCode::Int16:
            return "int16";
        case Metadata::PrimitiveTypeCode::UInt16:
            return "uint16";
        case Metadata::PrimitiveTypeCode::Int32:
            return "int32";
        case Metadata::PrimitiveTypeCode::UInt32:
            return "uint32";
        case Metadata::PrimitiveTypeCode::Int64:
            return "int64";
        case Metadata::PrimitiveTypeCode::UInt64:
            return "uint64";
        case Metadata::PrimitiveTypeCode::Single:
            return "float32";
        case Metadata::PrimitiveTypeCode::Double:
            return "float64";
        case Metadata::PrimitiveTypeCode::String:
            return "string";
        case Metadata::PrimitiveTypeCode::Object:
            return "object";
        default:
            return "unknown";
    }
}

SecurityDeclarationDecoder::SecurityDeclarationDecoder(
    Output::ITextOutput& output, Metadata::IAssemblyResolver* resolver,
    const Metadata::MetadataFile& module)
    : output_(output),
      resolver_(resolver),
      module_(module) {}

SecurityDeclarationDecoder::TType
SecurityDeclarationDecoder::GetPrimitiveType(
    Metadata::PrimitiveTypeCode typeCode) {
    return TType{typeCode, std::nullopt};
}

SecurityDeclarationDecoder::TType
SecurityDeclarationDecoder::GetSystemType() {
    return TType{static_cast<Metadata::PrimitiveTypeCode>(0),
        std::string("type")};
}

SecurityDeclarationDecoder::TType
SecurityDeclarationDecoder::GetSZArrayType(TType elementType) {
    // The C# `(elementType.Item2 ?? PrimitiveTypeCodeToString(
    // elementType.Item1)) + "[]"`.
    return TType{elementType.Code,
        (elementType.Name ? *elementType.Name
                          : PrimitiveTypeCodeToString(elementType.Code))
            + "[]"};
}

SecurityDeclarationDecoder::TType
SecurityDeclarationDecoder::GetTypeFromDefinition(
    const Metadata::MetadataFile& reader, std::uint32_t typeDefToken,
    std::uint8_t) {
    std::string fullTypeName =
        Metadata::GetFullTypeNameFromDefinition(reader, typeDefToken)
            .FullName();
    Metadata::PrimitiveTypeCode typeCode;
    if (Metadata::IsEnum(reader, typeDefToken, typeCode))
        return TType{typeCode, "enum " + fullTypeName};
    return TType{static_cast<Metadata::PrimitiveTypeCode>(0), fullTypeName};
}

SecurityDeclarationDecoder::TType
SecurityDeclarationDecoder::GetTypeFromReference(
    const Metadata::MetadataFile& reader, std::uint32_t typeRefToken,
    std::uint8_t) {
    std::string fullTypeName =
        Metadata::GetFullTypeNameFromReference(reader, typeRefToken)
            .FullName();
    std::optional<std::string> containingModule =
        GetDeclaringModule(reader, typeRefToken);

    std::string assemblyQualifiedTypeName =
        containingModule ? fullTypeName + ", " + *containingModule
                         : fullTypeName;

    Metadata::PrimitiveTypeCode typeCode =
        static_cast<Metadata::PrimitiveTypeCode>(0);
    ResolvedType resolvedType =
        ResolveType(assemblyQualifiedTypeName, module_);
    if (resolvedType.Module != nullptr) {
        if (!Metadata::IsEnum(*resolvedType.Module, resolvedType.TypeDefToken,
                typeCode)) {
            typeCode = static_cast<Metadata::PrimitiveTypeCode>(0);
        } else {
            assemblyQualifiedTypeName = "enum " + assemblyQualifiedTypeName;
        }
    }

    return TType{typeCode, assemblyQualifiedTypeName};
}

SecurityDeclarationDecoder::TType
SecurityDeclarationDecoder::GetTypeFromSerializedName(const std::string& name) {
    if (resolver_ == nullptr)
        throw Metadata::EnumUnderlyingTypeResolveException();
    ResolvedType resolvedType = ResolveType(name, module_);
    if (resolvedType.TypeDefToken == 0)
        throw Metadata::EnumUnderlyingTypeResolveException();
    Metadata::PrimitiveTypeCode typeCode;
    if (Metadata::IsEnum(*resolvedType.Module, resolvedType.TypeDefToken,
            typeCode))
        return TType{typeCode, "enum " + name};
    return TType{static_cast<Metadata::PrimitiveTypeCode>(0), name};
}

Metadata::PrimitiveTypeCode
SecurityDeclarationDecoder::GetUnderlyingEnumType(TType type) {
    return type.Code;
}

bool SecurityDeclarationDecoder::IsSystemType(TType type) {
    return type.Name == std::optional<std::string>("type");
}

SecurityDeclarationDecoder::ResolvedType
SecurityDeclarationDecoder::ResolveType(const std::string& typeName,
    const Metadata::MetadataFile& module) {
    // The C# `typeName.Split(new[] { ", " }, 2,
    // StringSplitOptions.None)` -- at most two parts; `nameParts[0]` is the
    // dotted type name, `nameParts[1]` (when present) the assembly name.
    std::string typeNamePart = typeName;
    std::optional<std::string> assemblyNamePart;
    if (std::size_t comma = typeName.find(", ");
        comma != std::string::npos) {
        typeNamePart = typeName.substr(0, comma);
        assemblyNamePart = typeName.substr(comma + 2);
    }
    // The C# `nameParts[0].Split('.')` -- the empty-string segments between
    // adjacent dots included (a leading/trailing/double dot yields them).
    std::vector<std::string> typeNameParts;
    std::size_t start = 0;
    while (true) {
        std::size_t dot = typeNamePart.find('.', start);
        if (dot == std::string::npos) {
            typeNameParts.push_back(typeNamePart.substr(start));
            break;
        }
        typeNameParts.push_back(typeNamePart.substr(start, dot - start));
        start = dot + 1;
    }

    const Metadata::MetadataFile* containingModule = nullptr;
    std::uint32_t typeDefToken = 0;
    // if we deal with an assembly-qualified name, resolve the assembly
    if (assemblyNamePart) {
        containingModule = resolver_->Resolve(
            Metadata::AssemblyNameReference::Parse(*assemblyNamePart));
    }
    if (containingModule != nullptr) {
        // try to find the type in the assembly
        typeDefToken = FindType(*containingModule, typeNameParts);
    } else {
        // just fully-qualified name, try current assembly
        typeDefToken = FindType(module, typeNameParts);
        containingModule = &module;
        if (typeDefToken == 0) {
            const Metadata::MetadataFile* mscorlib = nullptr;
            // otherwise try mscorlib
            if (TryResolveMscorlib(mscorlib)) {
                typeDefToken = FindType(*mscorlib, typeNameParts);
                containingModule = mscorlib;
            }
        }
    }

    return ResolvedType{containingModule, typeDefToken};
}

std::uint32_t SecurityDeclarationDecoder::FindType(
    const Metadata::MetadataFile& currentModule,
    const std::vector<std::string>& name) {
    const Metadata::NamespaceDefinition* currentNamespace =
        &currentModule.GetNamespaceDefinitionRoot();
    bool typeDefinitionsSet = false;  // the C# `!typeDefinitions.IsDefault`
    std::vector<std::uint32_t> typeDefinitions;

    for (int i = 0; i < static_cast<int>(name.size()); i++) {
        const std::string& identifier = name[static_cast<std::size_t>(i)];
        if (typeDefinitionsSet) {
            // The C# `goto restart` loop: scan the candidate list; a match
            // either returns (the last segment) or switches the list to the
            // match's nested types and re-scans with the SAME identifier
            // (the C#'s shipped shape).
            bool restart = true;
            while (restart) {
                restart = false;
                for (std::uint32_t type : typeDefinitions) {
                    auto typeDef = currentModule.GetTypeDefNameInfo(type);
                    if (!typeDef)
                        continue;
                    if (identifier == typeDef->Name) {
                        if (i + 1 == static_cast<int>(name.size()))
                            return type;
                        typeDefinitions =
                            currentModule.GetNestedTypes(type);
                        restart = true;
                        break;
                    }
                }
            }
        } else {
            // The C# `currentNamespace.NamespaceDefinitions.FirstOrDefault(
            // ns => metadata.StringComparer.Equals(...Name, identifier))`.
            bool foundNamespace = false;
            for (const auto& ns : currentNamespace->NamespaceDefinitions) {
                const Metadata::NamespaceDefinition& candidate =
                    currentModule.GetNamespaceDefinition(ns);
                if (candidate.Name == identifier) {
                    currentNamespace = &candidate;
                    foundNamespace = true;
                    break;
                }
            }
            if (foundNamespace) {
                continue;
            }
            typeDefinitions = currentNamespace->TypeDefinitions;
            typeDefinitionsSet = true;
            i--;  // match this segment against the type list
        }
    }
    return 0;
}

std::optional<std::string> SecurityDeclarationDecoder::GetDeclaringModule(
    const Metadata::MetadataFile& reader, std::uint32_t typeRefToken) {
    auto scope = reader.GetTypeRefScopeInfo(typeRefToken);
    if (!scope)
        return std::nullopt;
    switch (scope->Scope) {
        case Metadata::TypeRefScopeInfo::Kind::TypeRef:
            // The C# `GetDeclaringModule((TypeReferenceHandle)
            // tr.ResolutionScope)` -- the declaring TypeRef recurses.
            return GetDeclaringModule(reader, scope->ScopeToken);
        case Metadata::TypeRefScopeInfo::Kind::AssemblyRef:
            // The C# `asmRef.TryGetFullAssemblyName(reader, out var
            // assemblyName) ? assemblyName : null`.
            return Metadata::TryGetFullAssemblyName(reader,
                scope->ScopeToken);
        case Metadata::TypeRefScopeInfo::Kind::ModuleRef:
            // The C# `reader.GetString(modRef.Name)` -- the scope row's name
            // column (the port's TypeRefScopeInfo::Name).
            return scope->Name;
        default:
            return std::nullopt;
    }
}

bool SecurityDeclarationDecoder::TryResolveMscorlib(
    const Metadata::MetadataFile*& mscorlib) {
    mscorlib = nullptr;
    if (mscorlib_ != nullptr) {
        mscorlib = mscorlib_;
        return true;
    }
    if (resolver_ == nullptr) {
        return false;
    }
    mscorlib_ = resolver_->Resolve(
        Metadata::AssemblyNameReference::Parse("mscorlib"));
    mscorlib = mscorlib_;
    return mscorlib_ != nullptr;
}

} // namespace ILSpy::Decompiler::Disassembler
