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

#include "Decompiler/CSharp/RequiredNamespaceCollector.hpp"

#include "Decompiler/Metadata/CodeMappingInfo.hpp"
#include "Decompiler/Metadata/MethodBody.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Disassembler/ILParser.hpp"
#include <cstring>
#include <vector>
#include <optional>
#include <set>
#include <string>
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MethodSemanticsLookup.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <any>
#include <optional>
#include <stdexcept>

namespace ILSpy::Decompiler::CSharp {
namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace MD = ::ILSpy::Decompiler::Metadata;
namespace Dis = ::ILSpy::Decompiler::Disassembler;

// The C# `static readonly GenericContext genericContext = default`.
TS::GenericContext DefaultGenericContext() {
    return TS::GenericContext();
}

// The C# `void HandleTypeParameters(IEnumerable<ITypeParameter>)`.
void HandleTypeParameters(RequiredNamespaceCollector& collector,
                          const std::vector<const TS::ITypeParameter*>& typeParameters) {
    for (const TS::ITypeParameter* typeParam : typeParameters) {
        if (typeParam == nullptr) continue;
        collector.HandleAttributes(typeParam->GetAttributes());
        for (const TS::ITypePtr& constraint : typeParam->DirectBaseTypes()) {
            collector.CollectTypeReference(constraint.get());
        }
    }
}

// The C# `void CollectNamespacesFromMethodBody(MethodBodyBlock, MetadataModule)`:
// the local signatures, the exception-handler catch types, and the
// Field/Method/Sig/Tok/Type operand walk. The port drives the cursor over the
// MethodBody IL() span (the C# BlobReader `RemainingBytes` port).
void CollectNamespacesFromMethodBody(RequiredNamespaceCollector& collector,
                                     TS::MetadataModule& module,
                                     const MD::MetadataFile& metadata,
                                     const MD::MethodBody& body) {
    if (body.LocalVarSigToken() != 0) {
        // The C# `DecodeLocalSignature` (the Issue #1211 catch: invalid
        // signatures are ignored).
        try {
            for (const TS::ITypePtr& type :
                 module.DecodeLocalSignature(body.LocalVarSigToken(),
                                             DefaultGenericContext())) {
                collector.CollectTypeReference(type.get());
            }
        } catch (const std::invalid_argument&) {
            // Issue #1211: ignore invalid local signatures
        }
    }
    for (const MD::ExceptionHandlerClause& region : body.Handlers()) {
        if (region.Kind != MD::ExceptionHandlerKind::Catch) continue;
        if (region.ClassTokenOrFilterOffset == 0) continue;
        try {
            TS::ITypePtr ty = module.ResolveType(region.ClassTokenOrFilterOffset,
                                                 DefaultGenericContext());
            collector.CollectTypeReference(ty.get());
        } catch (const std::invalid_argument&) {
            continue;
        }
    }
    const std::uint8_t* base = body.IL().data();
    const std::size_t size = body.IL().size();
    std::size_t pos = 0;
    while (pos < size) {
        MD::ILOpCode opCode = MD::DecodeOpCode(base, size, pos);
        switch (MD::GetOperandType(opCode)) {
            case MD::OperandType::Field:
            case MD::OperandType::Method:
            case MD::OperandType::Sig:
            case MD::OperandType::Tok:
            case MD::OperandType::Type: {
                if (pos + 4 > size) return;
                std::uint32_t raw = 0;
                std::memcpy(&raw, base + pos, 4);
                pos += 4;
                if (raw == 0) continue;
                const std::uint32_t table = raw >> 24;
                if (table == 0x02 || table == 0x01 || table == 0x1B) {
                    // TypeDefinition / TypeReference / TypeSpecification
                    try {
                        TS::ITypePtr type = module.ResolveType(
                            raw, DefaultGenericContext());
                        collector.CollectTypeReference(type.get());
                    } catch (const std::invalid_argument&) {
                        continue;
                    }
                } else if (table == 0x04 || table == 0x06 ||
                           table == 0x2B || table == 0x0A) {
                    // FieldDefinition / MethodDefinition /
                    // MethodSpecification / MemberReference
                    try {
                        const TS::IEntity* member =
                            module.ResolveEntity(raw, DefaultGenericContext());
                        if (member == nullptr) continue;
                        if (const auto* field =
                                dynamic_cast<const TS::IField*>(member)) {
                            collector.CollectTypeReference(
                                field->DeclaringType().get());
                            collector.CollectTypeReference(&field->ReturnType());
                        } else if (const auto* method =
                                       dynamic_cast<const TS::IMethod*>(member)) {
                            collector.CollectTypeReference(
                                method->DeclaringType().get());
                            collector.CollectTypeReference(&method->ReturnType());
                            for (const TS::IParameter* param :
                                 method->Parameters()) {
                                collector.CollectTypeReference(&param->Type());
                            }
                            for (const TS::ITypePtr& arg :
                                 method->TypeArguments()) {
                                collector.CollectTypeReference(arg.get());
                            }
                        }
                    } catch (const std::invalid_argument&) {
                        continue;
                    }
                } else if (table == 0x11) {
                    // StandaloneSignature: the method-signature shape's
                    // function-pointer type contributes its namespaces.
                    try {
                        TS::MetadataModule::DecodedStandaloneMethodSignature
                            decoded = module.DecodeMethodSignature(
                                raw, DefaultGenericContext());
                        if (decoded.Type != nullptr) {
                            collector.CollectTypeReference(decoded.Type.get());
                        }
                    } catch (const std::invalid_argument&) {
                        continue;
                    }
                } else {
                    Dis::SkipOperand(base, size, pos, opCode);
                }
                break;
            }
            default:
                Dis::SkipOperand(base, size, pos, opCode);
                break;
        }
    }
}

} // namespace

// The C# `void CollectNamespaces(IEntity entity, MetadataModule module,
// CodeMappingInfo? mappingInfo)` (the private walk).
void CollectNamespacesEntity(RequiredNamespaceCollector& collector,
                             const TS::IEntity* entity,
                             TS::MetadataModule& module,
                             MD::CodeMappingInfo* mappingInfo,
                             bool skipImplicitBaseTypes) {
    if (entity == nullptr || entity->MetadataToken() == 0) return;
    const MD::MetadataFile* metadata = module.MetadataFile();
    if (metadata == nullptr) return;
    std::shared_ptr<MD::CodeMappingInfo> ownedMapping;
    if (mappingInfo == nullptr) {
        ownedMapping = MD::GetCodeMappingInfo(*metadata, entity->MetadataToken());
        mappingInfo = ownedMapping.get();
    }
    if (const auto* td = dynamic_cast<const TS::ITypeDefinition*>(entity)) {
        collector.Namespaces().emplace(td->Namespace());
        collector.HandleAttributes(td->GetAttributes());
        HandleTypeParameters(collector, td->TypeParameters());
        for (const TS::ITypePtr& baseType : td->DirectBaseTypes()) {
            if (skipImplicitBaseTypes && baseType != nullptr &&
                (TS::IsKnownType(*baseType, TS::KnownTypeCode::Object) ||
                 TS::IsKnownType(*baseType, TS::KnownTypeCode::ValueType) ||
                 TS::IsKnownType(*baseType, TS::KnownTypeCode::Enum))) {
                continue;
            }
            collector.CollectTypeReference(baseType.get());
        }
        for (const TS::ITypeDefinition* nested : td->NestedTypes()) {
            CollectNamespacesEntity(collector, nested, module, mappingInfo,
                                    skipImplicitBaseTypes);
        }
        for (const TS::IField* field : td->Fields()) {
            // The minimal using set: the enum's special value__ field
            // never renders (the enum member list skips it), so its type
            // contributes no using.
            if (skipImplicitBaseTypes && field->Name() == "value__")
                continue;
            CollectNamespacesEntity(collector, field, module, mappingInfo,
                                    skipImplicitBaseTypes);
        }
        for (const TS::IProperty* property : td->Properties()) {
            CollectNamespacesEntity(collector, property, module, mappingInfo,
                                    skipImplicitBaseTypes);
        }
        for (const TS::IEvent* event : td->Events()) {
            CollectNamespacesEntity(collector, event, module, mappingInfo,
                                    skipImplicitBaseTypes);
        }
        for (const TS::IMethod* method : td->Methods()) {
            // The minimal using set: the enum's compiler-emitted .ctor (a
            // void instance constructor no C# enum declaration carries)
            // never renders.
            if (skipImplicitBaseTypes &&
                td->Kind() == TS::TypeKind::Enum &&
                method->Name() == ".ctor")
                continue;
            CollectNamespacesEntity(collector, method, module, mappingInfo,
                                    skipImplicitBaseTypes);
        }
        return;
    }
    if (const auto* field = dynamic_cast<const TS::IField*>(entity)) {
        collector.HandleAttributes(field->GetAttributes());
        collector.CollectTypeReference(&field->ReturnType());
        return;
    }
    if (const auto* method = dynamic_cast<const TS::IMethod*>(entity)) {
        std::vector<std::uint32_t> parts;
        mappingInfo->GetMethodParts(method->MetadataToken(), parts);
        for (std::uint32_t part : parts) {
            const TS::IMethod* partMethod =
                module.ResolveMethod(part, DefaultGenericContext());
            if (partMethod == nullptr) continue;
            collector.HandleAttributes(partMethod->GetAttributes());
            collector.HandleAttributes(partMethod->GetReturnTypeAttributes());
            collector.CollectTypeReference(&partMethod->ReturnType());
            for (const TS::IParameter* param : partMethod->Parameters()) {
                collector.HandleAttributes(param->GetAttributes());
                collector.CollectTypeReference(&param->Type());
            }
            HandleTypeParameters(collector, partMethod->TypeParameters());
            // The C# `HandleOverrides(part.GetMethodImplementations(...))`.
            if (method->HasBody()) {
                // The C# `module.MetadataFile.GetMethodBody(
                // methodDef.RelativeVirtualAddress)`. The port resolves the
                // part's body through the metadata layer's MethodDef table
                // walk (the part token IS a MethodDef token).
                try {
                    // The metadata layer's token->RVA lookup (a map probe);
                    // the earlier linear scan over the module's whole
                    // MethodDef table made the using-set walk quadratic in
                    // the module's method count.
                    std::uint32_t rva = metadata->GetMethodRVA(part);
                    if (rva == 0) continue;
                    const MD::MethodBody body = metadata->GetMethodBody(rva);
                    CollectNamespacesFromMethodBody(collector, module,
                                                    *metadata, body);
                } catch (const std::exception&) {
                    // The C# `catch (BadImageFormatException)` -- the body is
                    // skipped.
                }
            }
        }
        return;
    }
    if (const auto* property = dynamic_cast<const TS::IProperty*>(entity)) {
        collector.HandleAttributes(property->GetAttributes());
        collector.CollectTypeReference(&property->ReturnType());
        if (property->Getter() != nullptr) {
            CollectNamespacesEntity(collector, property->Getter(), module,
                                    mappingInfo, skipImplicitBaseTypes);
        }
        if (property->Setter() != nullptr) {
            CollectNamespacesEntity(collector, property->Setter(), module,
                                    mappingInfo, skipImplicitBaseTypes);
        }
        return;
    }
    if (const auto* event = dynamic_cast<const TS::IEvent*>(entity)) {
        collector.HandleAttributes(event->GetAttributes());
        collector.CollectTypeReference(&event->ReturnType());
        if (event->AddAccessor() != nullptr) {
            CollectNamespacesEntity(collector, event->AddAccessor(), module,
                                    mappingInfo, skipImplicitBaseTypes);
        }
        if (event->RemoveAccessor() != nullptr) {
            CollectNamespacesEntity(collector, event->RemoveAccessor(), module,
                                    mappingInfo, skipImplicitBaseTypes);
        }
        return;
    }
}

void RequiredNamespaceCollector::HandleAttributes(
    const std::vector<const TS::IAttribute*>& attributes) {
    for (const TS::IAttribute* attr : attributes) {
        if (attr == nullptr) continue;
        if (minimalUsingSet_) {
            // The stripped attribute families (the auto-property's
            // backing-field pair, the state machine attributes the
            // de-sugar removes, the closure debugger attributes) never
            // render, so a using for their namespaces is not required.
            static const std::set<std::string> kStripped = {
                "System.Runtime.CompilerServices.CompilerGeneratedAttribute",
                "System.Diagnostics.DebuggerBrowsableAttribute",
                "System.Diagnostics.DebuggerHiddenAttribute",
                "System.Diagnostics.DebuggerStepThroughAttribute",
                "System.Diagnostics.DebuggerDisplayAttribute",
                "System.Runtime.CompilerServices.AsyncStateMachineAttribute",
                "System.Runtime.CompilerServices.IteratorStateMachineAttribute",
                "System.Runtime.CompilerServices."
                "AsyncIteratorStateMachineAttribute",
            };
            if (kStripped.count(attr->AttributeType().ReflectionName()) != 0)
                continue;
        }
        namespaces_.emplace(attr->AttributeType().Namespace());
        for (const TS::CustomAttributeTypedArgument& arg :
             attr->FixedArguments()) {
            HandleAttributeValue(arg.Type().get(), arg.Value());
        }
        for (const TS::CustomAttributeNamedArgument& arg :
             attr->NamedArguments()) {
            HandleAttributeValue(arg.Type().get(), arg.Value());
        }
    }
}

// The C# `void HandleAttributeValue(IType type, object? value)`.
void RequiredNamespaceCollector::HandleAttributeValue(
    const TS::IType* type, const std::any& value) {
    CollectTypeReference(type);
    // The C# `if (value is IType typeofType)` -- a typeof-type value recurses.
    if (const auto* typeofType = std::any_cast<const TS::IType*>(&value)) {
        CollectTypeReference(*typeofType);
    }
    if (const auto* arr =
            std::any_cast<std::vector<TS::CustomAttributeTypedArgument>>(
                &value)) {
        for (const TS::CustomAttributeTypedArgument& element : *arr) {
            HandleAttributeValue(element.Type().get(), element.Value());
        }
    }
}

// The C# `public static void CollectNamespaces(IEntity entity, MetadataModule
// module, HashSet<string> namespaces)` -- the third overload (the entity
// entry point).
void CollectNamespaces(const TS::IEntity& entity, TS::MetadataModule& module,
                       std::unordered_set<std::string>& namespaces) {
    RequiredNamespaceCollector collector(namespaces);
    CollectNamespacesEntity(collector, &entity, module, nullptr,
                            /*skipImplicitBaseTypes=*/false);
}

// The flat -t render's minimal using set (see the header).
void CollectRequiredNamespaces(
    const TS::IEntity& entity, TS::MetadataModule& module,
    std::unordered_set<std::string>& namespaces) {
    RequiredNamespaceCollector collector(
        namespaces, /*seedKnownTypeNamespaces=*/false,
        /*minimalUsingSet=*/true);
    CollectNamespacesEntity(collector, &entity, module, nullptr,
                            /*skipImplicitBaseTypes=*/true);
}

// The module-wide minimal using set (see the header).
void CollectRequiredNamespaces(
    TS::MetadataModule& module, std::unordered_set<std::string>& namespaces) {
    RequiredNamespaceCollector collector(
        namespaces, /*seedKnownTypeNamespaces=*/false,
        /*minimalUsingSet=*/true);
    for (const TS::ITypeDefinition* type : module.TypeDefinitions()) {
        if (type == nullptr)
            continue;
        CollectNamespacesEntity(collector, type, module, nullptr,
                                /*skipImplicitBaseTypes=*/true);
    }
    collector.HandleAttributes(module.GetAssemblyAttributes());
    collector.HandleAttributes(module.GetModuleAttributes());
}


// HashSet<string>)`: the assembly + module attribute sweep only.
void CollectAttributeNamespaces(TS::MetadataModule& module,
                                std::unordered_set<std::string>& namespaces) {
    RequiredNamespaceCollector collector(namespaces);
    collector.HandleAttributes(module.GetAssemblyAttributes());
    collector.HandleAttributes(module.GetModuleAttributes());
}

// The C# `public static void CollectNamespaces(MetadataModule module,
// HashSet<string> namespaces)` -- the FIRST overload: every type definition
// plus the assembly/module attributes.
void CollectNamespaces(TS::MetadataModule& module,
                       std::unordered_set<std::string>& namespaces) {
    RequiredNamespaceCollector collector(namespaces);
    for (const TS::ITypeDefinition* type : module.TypeDefinitions()) {
        if (type == nullptr) continue;
        CollectNamespacesEntity(collector, type, module, nullptr,
                                /*skipImplicitBaseTypes=*/false);
    }
    collector.HandleAttributes(module.GetAssemblyAttributes());
    collector.HandleAttributes(module.GetModuleAttributes());
}

} // namespace ILSpy::Decompiler::CSharp