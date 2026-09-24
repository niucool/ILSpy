// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/Metadata/CodeMappingInfo.hpp"

#include "Decompiler/Disassembler/ILParser.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MethodSemanticsLookup.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cstring>
#include <stdexcept>

namespace ILSpy::Decompiler::Metadata {
namespace {

// The C# `YieldReturnDecompiler.IsCompilerGeneratorEnumerator(TypeDefinition
// handle, MetadataReader)`: a nested compiler-generated type implementing
// System.Collections.IEnumerator.
bool IsCompilerGeneratedEnumeratorType(const MetadataFile& metadata,
                                       std::uint32_t typeDefToken) {
    // The C# `type.IsCompilerGeneratedOrIsInCompilerGeneratedClass(metadata)`
    // -- the token-level gate (the port's HasKnownAttribute attribute read;
    // the C# NRExtensions entity-level twin is the TypeSystemExtensions port).
    if (typeDefToken == 0) return false;
    std::optional<TypeDefNameInfo> selfInfo =
        metadata.GetTypeDefNameInfo(typeDefToken);
    if (!selfInfo.has_value()) return false;
    // The C# `IsCompilerGeneratedOrIsInCompilerGeneratedClass`: the type
    // itself or its declaring type carries [CompilerGenerated].
    const bool compilerGenerated =
        HasKnownAttribute(metadata, typeDefToken,
                          TypeSystem::KnownAttribute::CompilerGenerated) ||
        (selfInfo->DeclaringTypeToken != 0 &&
         HasKnownAttribute(metadata, selfInfo->DeclaringTypeToken,
                           TypeSystem::KnownAttribute::CompilerGenerated));
    if (!compilerGenerated) return false;
    // The C# `(td = ...).GetDeclaringType().IsNil` gate -- a NESTED type only.
    if (selfInfo->DeclaringTypeToken == 0) return false;
    // The interface-implementation scan: System.Collections.IEnumerator.
    for (const MetadataFile::InterfaceImplementationInfo& impl :
         metadata.GetInterfaceImplementations(typeDefToken)) {
        if (impl.InterfaceToken == 0) continue;
        try {
            TypeSystem::FullTypeName name =
                GetFullTypeName(metadata, impl.InterfaceToken);
            if (!name.IsNested() &&
                name.GetTopLevelTypeName().Namespace() == "System.Collections" &&
                name.GetTopLevelTypeName().Name() == "IEnumerator") {
                return true;
            }
        } catch (const std::exception&) {
            continue;  // the C# GetFullTypeName BadImageFormat surfaces as an
                       // exception the caller's try wraps; the enumerator
                       // probe skips undecodable rows
        }
    }
    return false;
}

} // namespace

// The C# `AsyncAwaitDecompiler.IsCompilerGeneratedStateMachine` (a public
// static; the AsyncAwaitDecompiler/YieldReturnDecompiler containers are not
// ported, so the predicates land here beside their only current consumer).
bool IsCompilerGeneratedStateMachine(const MetadataFile& metadata,
                                     std::uint32_t typeDefToken) {
    if (typeDefToken == 0) return false;
    std::optional<TypeDefNameInfo> typeInfo =
        metadata.GetTypeDefNameInfo(typeDefToken);
    if (!typeInfo.has_value()) return false;
    // The C# `td.GetDeclaringType().IsNil` -- a nested type only.
    if (typeInfo->DeclaringTypeToken == 0) return false;
    if (!HasKnownAttribute(metadata, typeDefToken,
                           TypeSystem::KnownAttribute::CompilerGenerated) &&
        !HasKnownAttribute(metadata, typeInfo->DeclaringTypeToken,
                           TypeSystem::KnownAttribute::CompilerGenerated)) {
        return false;
    }
    for (const MetadataFile::InterfaceImplementationInfo& impl :
         metadata.GetInterfaceImplementations(typeDefToken)) {
        if (impl.InterfaceToken == 0) continue;
        try {
            TypeSystem::FullTypeName name =
                GetFullTypeName(metadata, impl.InterfaceToken);
            if (!name.IsNested() &&
                name.GetTopLevelTypeName().Namespace() ==
                    "System.Runtime.CompilerServices" &&
                name.GetTopLevelTypeName().Name() == "IAsyncStateMachine") {
                return true;
            }
        } catch (const std::exception&) {
            continue;
        }
    }
    return false;
}

// The C# `YieldReturnDecompiler.IsCompilerGeneratorEnumerator` -- re-exported
// over the file-local helper (the C# static is consumed by AsyncAwaitDecompiler
// too; the port keeps the single implementation).
bool IsCompilerGeneratorEnumerator(const MetadataFile& metadata,
                                   std::uint32_t typeDefToken) {
    if (typeDefToken == 0) return false;
    // The C# `!type.IsCompilerGeneratedOrIsInCompilerGeneratedClass(metadata)`
    // gate: the TYPE ITSELF or its nesting chain is compiler-generated (a
    // deeper probe than the sibling's own-only check).
    if (!HasKnownAttribute(metadata, typeDefToken,
                           TypeSystem::KnownAttribute::CompilerGenerated)) {
        return false;
    }
    return IsCompilerGeneratedEnumeratorType(metadata, typeDefToken);
}

CodeMappingInfo::CodeMappingInfo(const MetadataFile* module,
                                 std::uint32_t typeDefinitionToken)
    : module_(module), typeDefinition_(typeDefinitionToken) {}

void CodeMappingInfo::GetMethodParts(std::uint32_t method,
                                     std::vector<std::uint32_t>& result) const {
    auto it = parts_.find(method);
    if (it != parts_.end()) {
        result.insert(result.end(), it->second.begin(), it->second.end());
    } else {
        result.push_back(method);
    }
}

std::uint32_t CodeMappingInfo::GetParentMethod(std::uint32_t method) const {
    auto it = parents_.find(method);
    return it != parents_.end() ? it->second : method;
}

void CodeMappingInfo::AddMapping(std::uint32_t parent, std::uint32_t part) {
    if (parents_.find(part) != parents_.end()) return;
    parents_.emplace(part, parent);
    parts_[parent].push_back(part);
}

} // namespace ILSpy::Decompiler::Metadata
namespace ILSpy::Decompiler::Metadata {
namespace {

namespace Dis = ::ILSpy::Decompiler::Disassembler;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The C# `local function name` regex `^<(.*)>g__([^\|]*)\|{0,1}\d+(_\d+)?$`
// (LocalFunctionDecompiler.functionNameRegex): a manual parser over the same
// shape (the port's regex surface is not in scope here; the pattern is
// anchored at both ends).
bool ParseLocalFunctionName(const std::string& name) {
    // `^<(.*)>g__`
    if (name.size() < 6 || name.front() != '<') return false;
    const std::size_t close = name.find(">g__", 1);
    if (close == std::string::npos) return false;
    if (close + 4 >= name.size()) return false;
    const std::size_t rest = close + 4;
    // `([^\|]*)` -- the function name up to the `|` ordinal suffix (or end).
    std::size_t ordinalStart = rest;
    while (ordinalStart < name.size() && name[ordinalStart] != '|')
        ordinalStart++;
    if (ordinalStart == rest) return false;  // an empty function name
    // `\|{0,1}\d+(_\d+)?$` -- an optional `|` then a decimal ordinal with an
    // optional `_<n>` suffix, running to the end.
    std::size_t pos = ordinalStart;
    if (pos < name.size() && name[pos] == '|') pos++;
    const std::size_t digitsStart = pos;
    while (pos < name.size() &&
           name[pos] >= '0' && name[pos] <= '9')
        pos++;
    if (pos == digitsStart) return false;  // no ordinal digits
    if (pos < name.size() && name[pos] == '_') {
        pos++;
        const std::size_t suffixStart = pos;
        while (pos < name.size() && name[pos] >= '0' && name[pos] <= '9')
            pos++;
        if (pos == suffixStart) return false;  // `_<n>` requires digits
    }
    return pos == name.size();
}

// The C# `LocalFunctionDecompiler.IsLocalFunctionMethod(MetadataFile,
// MethodDefinitionHandle)` (the module-local shape; the C# context gate is
// dropped -- the port passes the module directly).
bool IsLocalFunctionMethod(const MetadataFile& module,
                           std::uint32_t methodToken) {
    if (methodToken == 0) return false;
    const std::string name = module.ResolveTokenToString(
        methodToken, /*ownerMethodToken=*/0);
    if (name.empty()) return false;
    if (!ParseLocalFunctionName(name)) return false;
    const std::uint32_t attributes = module.GetMethodAttributes(methodToken);
    // The C# `(method.Attributes & MethodAttributes.Assembly) == 0` gate
    // (MethodAttributes.Assembly = 0x0004) plus the CompilerGenerated chain.
    constexpr std::uint32_t kMethodAttributesAssembly = 0x0004;
    if ((attributes & kMethodAttributesAssembly) == 0) return false;
    const std::uint32_t declaringType =
        module.GetMethodDeclaringTypeToken(methodToken);
    if (HasKnownAttribute(module, methodToken,
                          TS::KnownAttribute::CompilerGenerated)) {
        return true;
    }
    if (declaringType != 0 &&
        HasKnownAttribute(module, declaringType,
                          TS::KnownAttribute::CompilerGenerated)) {
        return true;
    }
    return false;
}

// The C# `TryGetExtensionImplementation(MetadataReader, MethodDefinitionHandle,
// out MethodDefinitionHandle)` (CSharpDecompiler.cs): the extension-group /
// marker-type shape resolution (`<>E__` / `<G>$` containers, `<M>$` groups).
bool TryGetExtensionImplementation(const MetadataFile& metadata,
                                   std::uint32_t definitionPart,
                                   std::uint32_t& implementationPart) {
    implementationPart = 0;
    const std::string methodName = metadata.ResolveTokenToString(
        definitionPart, /*ownerMethodToken=*/0);
    const std::uint32_t declaringType =
        metadata.GetMethodDeclaringTypeToken(definitionPart);
    if (declaringType == 0) return false;
    std::optional<TypeDefNameInfo> declTypeInfo =
        metadata.GetTypeDefNameInfo(declaringType);
    if (!declTypeInfo.has_value()) return false;
    const std::uint32_t containerHandle = declTypeInfo->DeclaringTypeToken;
    if (containerHandle == 0) return false;

    const auto startsWith = [](const std::string& s, const char* prefix) {
        return s.rfind(prefix, 0) == 0;
    };
    // The C# `FindImplementations(GetMethods(...))` lambda: scan the container
    // type's methods for the same name.
    const auto findImplementations =
        [&](std::uint32_t containerTypeToken) -> std::uint32_t {
        for (const MethodInfo& m : metadata.GetMethods(containerTypeToken)) {
            if (m.Name == methodName) return m.Token;
        }
        return 0;
    };

    if (startsWith(declTypeInfo->Name, "<>E__") ||
        startsWith(declTypeInfo->Name, "<G>$")) {
        implementationPart = findImplementations(containerHandle);
    } else if (startsWith(declTypeInfo->Name, "<M>$")) {
        const std::uint32_t groupHandle =
            metadata.GetTypeDefNameInfo(containerHandle).has_value()
                ? metadata.GetTypeDefNameInfo(containerHandle)
                      ->DeclaringTypeToken
                : 0;
        if (groupHandle == 0) return false;
        implementationPart = findImplementations(groupHandle);
    } else {
        return false;
    }
    return implementationPart != 0;
}

// The C# `MethodDefinitionHandle GetDeclaringTypeOfMember(EntityHandle)` --
// the Newobj/Stfld arm's state-machine type resolution: dispatch on the
// referenced token's kind and return the owning TypeDef token (0 when not a
// state-machine candidate).
std::uint32_t GetDeclaringTypeOfToken(const MetadataFile& metadata,
                                      std::uint32_t token) {
    // The C# `token.Kind`: 0x06 (MethodDef) -> GetDeclaringType; 0x04
    // (FieldDef) -> GetDeclaringType; 0x0A (MemberRef) -> the member ref's
    // Parent (a TypeDef/TypeRef token).
    if (token == 0) return 0;
    const std::uint32_t table = token >> 24;
    if (table == 0x06) return metadata.GetMethodDeclaringTypeToken(token);
    if (table == 0x04) return metadata.GetFieldDeclaringTypeToken(token);
    if (table == 0x0A) {
        auto memberRef = metadata.GetMemberReference(token);
        if (!memberRef.has_value()) return 0;
        // The C# `ExtractDeclaringType(memberRef)`: the TypeRef/TypeDef/TypeSpec
        // parent's TypeDef resolution; a nil or TypeRef parent yields nil (the
        // C# default).
        const std::uint32_t parentToken = memberRef->ParentToken;
        const std::uint32_t parentTable = parentToken >> 24;
        if (parentTable == 0x02) return parentToken;
        if (parentTable == 0x01) {
            // The C# ExtractDeclaringType returns `default` for a TypeReference
            // parent ("this should never happen in normal code").
            return 0;
        }
        if (parentTable == 0x1B) {
            // The C# TypeSpecification arm: the generic type head only.
            try {
                TypeSystem::FullTypeName name =
                    GetFullTypeName(metadata, parentToken);
                if (name.IsNested()) {
                    // The C# walks the GenericType; the port's FullTypeName
                    // keeps the top-level form only for TypeSpecs (the
                    // GetFullTypeNameFromSpecification shrinking decode); the
                    // nested-shape check is the load-bearing gate.
                    return 0;
                }
                return 0;  // the C# also yields the TypeDefinitionHandle via a
                           // type-resolution step the port defers
            } catch (const std::exception&) {
                return 0;
            }
        }
        return 0;
    }
    return 0;
}

} // namespace

std::shared_ptr<CodeMappingInfo> GetCodeMappingInfo(const MetadataFile& module,
                                                    std::uint32_t memberToken) {
    // The C# `declaringType = member.GetDeclaringType(module.Metadata)`.
    std::uint32_t declaringType = 0;
    if (memberToken != 0) {
        const std::uint32_t table = memberToken >> 24;
        if (table == 0x02) {
            declaringType = memberToken;
        } else {
            try {
                declaringType = GetDeclaringType(module, memberToken);
            } catch (const std::exception&) {
                declaringType = 0;
            }
        }
    }
    auto info = std::make_shared<CodeMappingInfo>(&module, declaringType);
    if (declaringType == 0) return info;

    // The C# walk over the TypeDef's methods.
    for (const MethodInfo& methodInfo : module.GetMethods(declaringType)) {
        const std::uint32_t parent = methodInfo.Token;
        std::uint32_t part = methodInfo.Token;

        std::vector<std::uint32_t> connectedMethods;
        std::vector<std::uint32_t> processedMethods;
        std::vector<std::uint32_t> processedNestedTypes;
        connectedMethods.push_back(part);

        while (!connectedMethods.empty()) {
            part = connectedMethods.back();
            connectedMethods.pop_back();
            bool alreadyProcessed = false;
            for (std::uint32_t seen : processedMethods) {
                if (seen == part) {
                    alreadyProcessed = true;
                    break;
                }
            }
            if (alreadyProcessed) continue;
            processedMethods.push_back(part);
            try {
                // The C# `TryGetExtensionImplementation` arm.
                std::uint32_t implPart = 0;
                if (TryGetExtensionImplementation(module, part, implPart)) {
                    connectedMethods.push_back(implPart);
                }
                // The C# `ReadCodeMappingInfo` inline: the IL-body scan.
                if (module.GetMethodAttributes(part) != 0) {
                    // (attributes are not the body gate; the RVA check below
                    // is the C# `md.HasBody()` port)
                }
                std::uint32_t rva = 0;
                for (const MethodInfo& mi : module.GetMethods(declaringType)) {
                    if (mi.Token == part) {
                        rva = mi.RVA;
                        break;
                    }
                }
                if (rva != 0) {
                    MethodBody body = module.GetMethodBody(rva);
                    if (body.IsValid() && body.CodeSize() > 0) {
                        const std::uint8_t* base = body.IL().data();
                        const std::size_t size = body.IL().size();
                        std::size_t pos = 0;
                        while (pos < size) {
                            ILOpCode code = DecodeOpCode(base, size, pos);
                            // The C# reads the operand token for Newobj/
                            // Stfld/Ldftn/Call/Callvirt; everything else
                            // skips.
                            if (code == ILOpCode::Newobj ||
                                code == ILOpCode::Stfld ||
                                code == ILOpCode::Ldftn ||
                                code == ILOpCode::Call ||
                                code == ILOpCode::Callvirt) {
                                if (pos + 4 > size) break;
                                std::uint32_t raw = 0;
                                std::memcpy(&raw, base + pos, 4);
                                pos += 4;
                                // The C# little-endian EntityHandleOrNil: a
                                // 0 token is nil.
                                if (raw == 0) continue;
                                if (code == ILOpCode::Newobj ||
                                    code == ILOpCode::Stfld) {
                                    // async and yield fsms
                                    const std::uint32_t fsmTypeDef =
                                        GetDeclaringTypeOfToken(module, raw);
                                    if (fsmTypeDef == 0) continue;
                                    // Must be a nested type of the
                                    // containing type.
                                    std::optional<TypeDefNameInfo> fsmInfo =
                                        module.GetTypeDefNameInfo(fsmTypeDef);
                                    if (!fsmInfo.has_value()) continue;
                                    if (fsmInfo->DeclaringTypeToken !=
                                        declaringType)
                                        continue;
                                    const bool isEnumerator =
                                        IsCompilerGeneratorEnumerator(
                                            module, fsmTypeDef);
                                    const bool isStateMachine =
                                        IsCompilerGeneratedStateMachine(
                                            module, fsmTypeDef);
                                    if (!isEnumerator && !isStateMachine)
                                        continue;
                                    bool nestedSeen = false;
                                    for (std::uint32_t seen :
                                         processedNestedTypes) {
                                        if (seen == fsmTypeDef) {
                                            nestedSeen = true;
                                            break;
                                        }
                                    }
                                    if (nestedSeen) continue;
                                    processedNestedTypes.push_back(fsmTypeDef);
                                    MethodSemanticsLookup semanticsLookup(
                                        module);
                                        for (const MethodInfo& h :
                                             module.GetMethods(fsmTypeDef)) {
                                        // The C# `GetSemantics(h).Item2 != 0`
                                        // -- a property/event accessor is
                                        // not enqueued.
                                            if (semanticsLookup.GetSemantics(h.Token)
                                                    .Semantics !=
                                                TypeSystem::MethodSemanticsAttributes::
                                                    None) {
                                                continue;
                                            }
                                            if (!HasKnownAttribute(
                                                    module, h.Token,
                                                    TS::KnownAttribute::
                                                        DebuggerHidden)) {
                                                connectedMethods.push_back(
                                                    h.Token);
                                            }
                                        }
                                } else if (code == ILOpCode::Ldftn) {
                                    // lambdas
                                    const std::uint32_t token = raw;
                                    const std::uint32_t tokenTable =
                                        token >> 24;
                                    if (tokenTable == 0x06) {
                                        if (HasKnownAttribute(
                                                module, token,
                                                TS::KnownAttribute::
                                                    CompilerGenerated)) {
                                            connectedMethods.push_back(token);
                                        }
                                        continue;
                                    }
                                    if (tokenTable == 0x0A) {
                                        auto memberRef =
                                            module.GetMemberReference(token);
                                        if (!memberRef.has_value()) continue;
                                        const std::uint32_t closureTypeHandle =
                                            GetDeclaringTypeOfToken(
                                                module, token);
                                        if (closureTypeHandle == 0) continue;
                                        if (closureTypeHandle !=
                                            declaringType) {
                                            // Must be a nested type of the
                                            // containing type.
                                            std::optional<TypeDefNameInfo>
                                                closureInfo =
                                                    module.GetTypeDefNameInfo(
                                                        closureTypeHandle);
                                            if (!closureInfo.has_value() ||
                                                closureInfo
                                                        ->DeclaringTypeToken !=
                                                    declaringType)
                                                continue;
                                            bool closureSeen = false;
                                            for (std::uint32_t seen :
                                                 processedNestedTypes) {
                                                if (seen ==
                                                    closureTypeHandle) {
                                                    closureSeen = true;
                                                    break;
                                                }
                                            }
                                            if (closureSeen) continue;
                                            processedNestedTypes.push_back(
                                                closureTypeHandle);
                                            for (const MethodInfo& m :
                                                 module.GetMethods(
                                                     closureTypeHandle)) {
                                                connectedMethods.push_back(
                                                    m.Token);
                                            }
                                        } else {
                                            // Delegate body declared in the
                                            // same type.
                                            for (const MethodInfo& m :
                                                 module.GetMethods(
                                                     closureTypeHandle)) {
                                                if (m.Name ==
                                                        memberRef->Name &&
                                                    HasKnownAttribute(
                                                        module, m.Token,
                                                        TS::KnownAttribute::
                                                            CompilerGenerated)) {
                                                    connectedMethods
                                                        .push_back(m.Token);
                                                }
                                            }
                                        }
                                        continue;
                                    }
                                    continue;
                                } else {
                                    // Call/Callvirt: local function
                                    // invocations
                                    std::uint32_t token = raw;
                                    const std::uint32_t tokenTable =
                                        token >> 24;
                                    if (tokenTable == 0x06) {
                                        // a direct MethodDef token
                                    } else if (tokenTable == 0x2B) {
                                        auto methodSpec =
                                            module.GetMethodSpecification(
                                                token);
                                        if (!methodSpec.has_value() ||
                                            methodSpec->MethodToken == 0 ||
                                            (methodSpec->MethodToken >> 24) !=
                                                0x06)
                                            continue;
                                        token = methodSpec->MethodToken;
                                    } else {
                                        continue;
                                    }
                                    if (IsLocalFunctionMethod(module,
                                                              token)) {
                                        connectedMethods.push_back(token);
                                    }
                                }
                            } else {
                                Dis::SkipOperand(base, size, pos, code);
                            }
                        }
                    }
                }
                info->AddMapping(parent, part);
            } catch (const std::exception&) {
                // The C# `catch (BadImageFormatException)` -- ignore invalid
                // IL (the mapping for this part is still recorded upstream).
                info->AddMapping(parent, part);
            }
        }
    }
    return info;
}

} // namespace ILSpy::Decompiler::Metadata
