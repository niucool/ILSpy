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

// Port of ICSharpCode.ILSpyX/Abstractions/ILanguage.cs: the decompiler
// abstraction every language surface (the C# language, the IL language,
// plugins) implements for its host.
//
// C#-to-C++ porting decisions:
//  * `interface ILanguage` -> a C++ abstract base with a virtual destructor
//    and one pure-virtual per C# member (the IAmbience convention).
//  * The C# `EntityHandle` ports as the raw metadata token
//    (std::uint32_t) -- the port's handle convention.
//  * The C# `TypeToString(IType, ConversionFlags conversionFlags =
//    UseFullyQualifiedEntityNames | UseFullyQualifiedTypeNames)`: the
//    default argument sits on the interface's own declaration exactly as
//    the C# declares it (C++ binds default arguments statically, which
//    here IS the C# behavior -- every call through the interface gets
//    the interface's value); implementations must never repeat a
//    default.
//
// The sibling files of the C# Abstractions/ folder are documented
// deferrals, recorded in PORT_LOG_BAML.md: IResourceFileHandler (its
// WriteResourceToFile signature and ResourceFileHandlerContext belong to
// the whole-project-export path -- Phase 10's
// BamlAwareWholeProjectDecompiler -- and take the not-yet-ported
// LoadedAssembly), and IResourceNodeFactory / ITreeNode (the GUI tree
// model the Phase 8 sub-scoping drops).

#pragma once

#include "Decompiler/Metadata/CodeMappingInfo.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/IAmbience.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::ILSpyX::Abstractions {

// The C# `TypeToString` default argument:
// `ConversionFlags.UseFullyQualifiedEntityNames |
// ConversionFlags.UseFullyQualifiedTypeNames`.
// (The port's ConversionFlags operators are non-constexpr free functions,
// so the OR is spelled over the enum's raw values.)
constexpr Decompiler::Output::ConversionFlags
    kDefaultTypeToStringConversionFlags = static_cast<
        Decompiler::Output::ConversionFlags>(
        static_cast<std::uint32_t>(
            Decompiler::Output::ConversionFlags::UseFullyQualifiedEntityNames)
        | static_cast<std::uint32_t>(
            Decompiler::Output::ConversionFlags::UseFullyQualifiedTypeNames));

// The C# `public interface ILanguage`.
class ILanguage {
public:
    virtual ~ILanguage() = default;

    virtual bool ShowMember(const Decompiler::TypeSystem::IEntity& member) const = 0;
    virtual std::shared_ptr<Decompiler::Metadata::CodeMappingInfo> GetCodeMappingInfo(
        const Decompiler::Metadata::MetadataFile& module,
        std::uint32_t memberToken) const = 0;
    virtual std::string GetEntityName(
        const Decompiler::Metadata::MetadataFile& module,
        std::uint32_t token, bool fullName, bool omitGenerics) const = 0;
    virtual std::string GetTooltip(
        const Decompiler::TypeSystem::IEntity& entity) const = 0;
    virtual std::string TypeToString(const Decompiler::TypeSystem::IType& type,
        Decompiler::Output::ConversionFlags conversionFlags =
            kDefaultTypeToStringConversionFlags) const = 0;
    virtual std::string EntityToString(
        const Decompiler::TypeSystem::IEntity& entity,
        Decompiler::Output::ConversionFlags conversionFlags) const = 0;
};

}  // namespace ILSpy::ILSpyX::Abstractions
