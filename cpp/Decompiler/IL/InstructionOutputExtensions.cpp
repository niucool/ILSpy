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

// The InstructionOutputExtensions implementation -- see the header for the
// port rationale and the deferred member arms.

#include "Decompiler/IL/InstructionOutputExtensions.hpp"

#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

#include <cstdio>
#include <stdexcept>
#include <vector>

namespace ILSpy::Decompiler::IL {

using Metadata::MetadataFile;
using Metadata::MetadataGenericContext;
using Output::ITextOutput;

void WriteTo(const Metadata::SignatureHeader& header, ITextOutput& output) {
    if (header.HasExplicitThis) {
        output.Write("instance explicit ");
    } else if (header.IsInstance()) {
        output.Write("instance ");
    }
    if (header.CallingConvention != Metadata::SignatureCallingConvention::Default) {
        output.Write(Metadata::ToILSyntax(header.CallingConvention));
        output.Write(' ');
    }
}

void WriteTypeParameterList(ITextOutput& output, Disassembler::ILNameSyntax syntax,
    const std::vector<Metadata::SignatureTypeWriter>& substitution) {
    output.Write('<');
    for (std::size_t i = 0; i < substitution.size(); i++) {
        if (i > 0) output.Write(", ");
        substitution[i](syntax);
    }
    output.Write('>');
}

void WriteParameterList(ITextOutput& output, const Metadata::MethodSignatureT& methodSignature) {
    output.Write("(");
    for (std::size_t i = 0; i < methodSignature.ParameterTypes.size(); ++i) {
        if (i > 0)
            output.Write(", ");
        if (i == methodSignature.RequiredParameterCount)
            output.Write("..., ");
        methodSignature.ParameterTypes[i](Disassembler::ILNameSyntax::SignatureNoNamedTypeParameters);
    }
    output.Write(")");
}

// The C# `static void WriteParent(ITextOutput output, MetadataFile
// metadataFile, EntityHandle parentHandle, MetadataGenericContext
// genericContext, ILNameSyntax syntax)`: a MemberRef's parent entity. A
// MethodDefinition parent renders its DECLARING type (the nested-type
// member-reference case); a ModuleReference parent the unescaped "[name]";
// the type handles recurse through WriteTo; anything else writes nothing.
void WriteParent(ITextOutput& output, const MetadataFile& module,
    std::uint32_t parentToken, const MetadataGenericContext& genericContext,
    Disassembler::ILNameSyntax syntax) {
    switch (parentToken >> 24) {
        case 0x06:  // HandleKind.MethodDefinition
        {
            std::uint32_t declaringType = module.GetMethodDeclaringTypeToken(parentToken);
            WriteTo(module, output, genericContext, declaringType, syntax);
            break;
        }
        case 0x1A:  // HandleKind.ModuleReference
        {
            output.Write('[');
            output.Write(module.GetModuleReferenceName(parentToken).value_or(""));
            output.Write(']');
            break;
        }
        case 0x02:  // HandleKind.TypeDefinition
        case 0x01:  // HandleKind.TypeReference
        case 0x1B:  // HandleKind.TypeSpecification
            WriteTo(module, output, genericContext, parentToken, syntax);
            break;
    }
}

void WriteTo(const MetadataFile& module, ITextOutput& output,
    const MetadataGenericContext& genericContext, std::uint32_t entityToken,
    Disassembler::ILNameSyntax syntax) {
    // The C# `if (entity.IsNil)` arm.
    if (entityToken == 0) {
        output.Write("<nil>");
        return;
    }
    // The C# `if (module == null) throw new ArgumentNullException`.
    std::uint32_t table = entityToken >> 24;
    switch (table) {
        case 0x02:  // HandleKind.TypeDefinition
        {
            // The C# `td.GetFullTypeName(metadata).ToILNameString()`; the
            // syntax parameter is unused for the type-definition arm (the C#
            // body ignores it).
            (void)syntax;
            TypeSystem::FullTypeName name = Metadata::GetFullTypeNameFromDefinition(
                module, entityToken);
            output.WriteReference(module, entityToken, Metadata::ToILNameString(name));
            break;
        }
        case 0x01:  // HandleKind.TypeReference
        {
            (void)syntax;
            // The C# resolution-scope prefix: walk to the OUTERMOST TypeRef,
            // then name the module/assembly the row resolves into.
            auto scope = module.GetTypeRefScopeInfo(entityToken);
            if (scope && scope->Scope != Metadata::TypeRefScopeInfo::Kind::None) {
                std::uint32_t currentTypeRef = entityToken;
                auto currentScope = module.GetTypeRefScopeInfo(currentTypeRef);
                while (currentScope && currentScope->Scope == Metadata::TypeRefScopeInfo::Kind::TypeRef) {
                    currentTypeRef = currentScope->ScopeToken;
                    currentScope = module.GetTypeRefScopeInfo(currentTypeRef);
                    if (!currentScope)
                        break;
                }
                if (currentScope) {
                    output.Write("[");
                    switch (currentScope->Scope) {
                        case Metadata::TypeRefScopeInfo::Kind::Module:
                            // The C# `metadata.GetModuleDefinition()` name.
                            output.Write(Disassembler::Escape(currentScope->Name));
                            break;
                        case Metadata::TypeRefScopeInfo::Kind::ModuleRef:
                            // The C# writes nothing for a ModuleReference
                            // scope (the prefix is just "[]").
                            break;
                        case Metadata::TypeRefScopeInfo::Kind::AssemblyRef:
                            output.Write(Disassembler::Escape(currentScope->Name));
                            break;
                        case Metadata::TypeRefScopeInfo::Kind::None:
                        case Metadata::TypeRefScopeInfo::Kind::TypeRef:
                            break;
                    }
                    output.Write("]");
                }
            }
            TypeSystem::FullTypeName name = Metadata::GetFullTypeNameFromReference(
                module, entityToken);
            output.WriteReference(module, entityToken, Metadata::ToILNameString(name));
            break;
        }
        case 0x1B:  // HandleKind.TypeSpecification
        {
            // The C# `ts.DecodeSignature(new DisassemblerSignatureTypeProvider(
            // module, output), genericContext); signature(syntax);`
            Disassembler::DisassemblerSignatureTypeProvider provider(module, output);
            Metadata::SignatureTypeWriter signature =
                provider.GetTypeFromSpecification(entityToken, 0x00, genericContext);
            signature(syntax);
            break;
        }
        case 0x04:  // HandleKind.FieldDefinition
        {
            // The C# `fd.DecodeSignature(new DisassemblerSignatureTypeProvider(
            // module, output), new MetadataGenericContext(fd.GetDeclaringType(),
            // metadata))`: the field sig's kind nibble must be Field (0x6) --
            // the SRM DecodeFieldSignature header check -- then one full type
            // decode; the VAR (!N) context scopes to the declaring TypeDef.
            auto blob = module.GetSignatureBlob(entityToken);
            if (!blob || blob->empty() || ((*blob)[0] & 0x0F) != 0x06)
                throw std::logic_error("field signature");
            std::uint32_t declaringType = module.GetFieldDeclaringTypeToken(entityToken);
            Disassembler::DisassemblerSignatureTypeProvider provider(module, output);
            Metadata::SignatureTypeProviderDecoder decoder(provider, module);
            Metadata::SignatureTypeWriter signature = decoder.DecodeType(
                blob->data() + 1, blob->size() - 1,
                Metadata::MetadataGenericContext::ForType(declaringType, module));
            signature(Disassembler::ILNameSyntax::SignatureNoNamedTypeParameters);
            output.Write(' ');
            WriteTo(module, output, Metadata::MetadataGenericContext{}, declaringType,
                Disassembler::ILNameSyntax::TypeName);
            output.Write("::");
            output.WriteReference(module, entityToken,
                Disassembler::Escape(module.GetFieldName(entityToken)));
            break;
        }
        case 0x06:  // HandleKind.MethodDefinition
        {
            // The C# `md.DecodeSignature(new DisassemblerSignatureTypeProvider(
            // module, output), new MetadataGenericContext((MethodDefinitionHandle)
            // entity, metadata))`, then the header/return-type prefix, the
            // declaring-type::name body (compiler-controlled names carry the
            // $PST token suffix), the generic-parameter block, and the
            // parameter list.
            auto blob = module.GetSignatureBlob(entityToken);
            if (!blob)
                throw std::logic_error("method signature");
            Metadata::MetadataGenericContext methodContext =
                Metadata::MetadataGenericContext::ForMethod(entityToken, module);
            Disassembler::DisassemblerSignatureTypeProvider provider(module, output);
            Metadata::SignatureTypeProviderDecoder decoder(provider, module);
            Metadata::MethodSignatureT methodSignature =
                decoder.DecodeMethodSignature(blob->data(), blob->size(), methodContext);
            WriteTo(methodSignature.Header, output);
            methodSignature.ReturnType(Disassembler::ILNameSyntax::SignatureNoNamedTypeParameters);
            output.Write(' ');
            std::uint32_t declaringType = module.GetMethodDeclaringTypeToken(entityToken);
            if (declaringType != 0) {
                WriteTo(module, output, genericContext, declaringType,
                    Disassembler::ILNameSyntax::TypeName);
                output.Write("::");
            }
            // The C# `bool isCompilerControlled =
            // (md.Attributes & MethodAttributes.MemberAccessMask) ==
            // MethodAttributes.PrivateScope`.
            bool isCompilerControlled =
                (module.GetMethodAttributes(entityToken) & 0x0007u) == 0x0000u;
            std::string name = module.GetMethodName(entityToken);
            if (isCompilerControlled) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "$PST%08X",
                    static_cast<unsigned>(entityToken));
                output.WriteReference(module, entityToken,
                    Disassembler::Escape(name + buf));
            } else {
                output.WriteReference(module, entityToken, Disassembler::Escape(name));
            }
            auto genericParameters = module.GetGenericParameters(entityToken);
            if (!genericParameters.empty()) {
                output.Write('<');
                for (std::size_t i = 0; i < genericParameters.size(); i++) {
                    if (i > 0) output.Write(", ");
                    const auto& gp = genericParameters[i];
                    // The C# GenericParameterAttributes flag spellings:
                    // ReferenceTypeConstraint (0x0004) / NotNullableValueType-
                    // Constraint (0x0008) mutually exclusive, then the
                    // DefaultConstructorConstraint (0x0010) prefix.
                    constexpr std::uint16_t kReferenceTypeConstraint = 0x0004;
                    constexpr std::uint16_t kNotNullableValueTypeConstraint = 0x0008;
                    constexpr std::uint16_t kDefaultConstructorConstraint = 0x0010;
                    constexpr std::uint16_t kContravariant = 0x0001;
                    constexpr std::uint16_t kCovariant = 0x0002;
                    if ((gp.Flags & kReferenceTypeConstraint) == kReferenceTypeConstraint) {
                        output.Write("class ");
                    } else if ((gp.Flags & kNotNullableValueTypeConstraint)
                        == kNotNullableValueTypeConstraint) {
                        output.Write("valuetype ");
                    }
                    if ((gp.Flags & kDefaultConstructorConstraint)
                        == kDefaultConstructorConstraint) {
                        output.Write(".ctor ");
                    }
                    auto constraints = module.GetGenericParameterConstraintTokens(gp.Token);
                    if (!constraints.empty()) {
                        output.Write('(');
                        for (std::size_t j = 0; j < constraints.size(); j++) {
                            if (j > 0) output.Write(", ");
                            // The C# `constraint.Type.WriteTo(module, output,
                            // new MetadataGenericContext((MethodDefinitionHandle)
                            // entity, metadata), ILNameSyntax.TypeName)`.
                            WriteTo(module, output, methodContext, constraints[j],
                                Disassembler::ILNameSyntax::TypeName);
                        }
                        output.Write(") ");
                    }
                    if ((gp.Flags & kContravariant) == kContravariant) {
                        output.Write('-');
                    } else if ((gp.Flags & kCovariant) == kCovariant) {
                        output.Write('+');
                    }
                    output.Write(Disassembler::Escape(gp.Name));
                }
                output.Write('>');
            }
            WriteParameterList(output, methodSignature);
            break;
        }
        case 0x0A:  // HandleKind.MemberReference
        {
            // The C# `mr.GetKind()` splits on the decompiled .NET 10
            // `SignatureHeader.Kind` rule: the low nibble <= 5 or == 9 is a
            // METHOD signature (the DEFAULT/C/STDCALL/THISCALL/FASTCALL/
            // VARARG/UNMANAGED calling conventions -- a vararg memberref has
            // nibble 5), 6 is a field; the method arm renders the parent
            // through WriteParent, the field arm a bare type decode with no
            // parameter list.
            auto blob = module.GetSignatureBlob(entityToken);
            if (!blob || blob->empty())
                throw std::logic_error("member signature");
            std::string memberName = module.GetMemberReference(entityToken)->Name;
            int memberKindNibble = (*blob)[0] & 0x0F;
            if (memberKindNibble <= 5 || memberKindNibble == 9) {  // method kind
                Metadata::MetadataGenericContext outerContext(genericContext);
                Disassembler::DisassemblerSignatureTypeProvider provider(module, output);
                Metadata::SignatureTypeProviderDecoder decoder(provider, module);
                Metadata::MethodSignatureT methodSignature =
                    decoder.DecodeMethodSignature(blob->data(), blob->size(), outerContext);
                WriteTo(methodSignature.Header, output);
                methodSignature.ReturnType(
                    Disassembler::ILNameSyntax::SignatureNoNamedTypeParameters);
                output.Write(' ');
                std::uint32_t parent = module.GetMemberReference(entityToken)->ParentToken;
                WriteParent(output, module, parent, genericContext, syntax);
                output.Write("::");
                output.WriteReference(module, entityToken, Disassembler::Escape(memberName));
                WriteParameterList(output, methodSignature);
            } else {  // the C# `case MemberReferenceKind.Field`
                if (((*blob)[0] & 0x0F) != 0x06)
                    throw std::logic_error("field signature");
                Disassembler::DisassemblerSignatureTypeProvider provider(module, output);
                Metadata::SignatureTypeProviderDecoder decoder(provider, module);
                Metadata::SignatureTypeWriter fieldSignature = decoder.DecodeType(
                    blob->data() + 1, blob->size() - 1, genericContext);
                fieldSignature(Disassembler::ILNameSyntax::SignatureNoNamedTypeParameters);
                output.Write(' ');
                std::uint32_t parent = module.GetMemberReference(entityToken)->ParentToken;
                WriteParent(output, module, parent, genericContext, syntax);
                output.Write("::");
                output.WriteReference(module, entityToken, Disassembler::Escape(memberName));
            }
            break;
        }
        case 0x2B:  // HandleKind.MethodSpecification
        {
            // The C# `ms.DecodeSignature(...)` substitution plus the target's
            // own method rendering: a MethodDef target writes the escaped
            // (compiler-controlled-aware) name, a MemberRef target the
            // WriteParent::name shape -- both followed by the substitution
            // block and the target's parameter list.
            auto blob = module.GetMethodSpecificationInstantiationBlob(entityToken);
            if (!blob)
                throw std::logic_error("method specification");
            Disassembler::DisassemblerSignatureTypeProvider provider(module, output);
            Metadata::SignatureTypeProviderDecoder decoder(provider, module);
            std::vector<Metadata::SignatureTypeWriter> substitution =
                decoder.DecodeMethodSpecSignature(blob->data(), blob->size(), genericContext);
            std::uint32_t methodToken = module.GetMethodSpecification(entityToken)->MethodToken;
            auto blobMethod = module.GetSignatureBlob(methodToken);
            if (!blobMethod)
                throw std::logic_error("method signature");
            Metadata::MethodSignatureT methodSignature =
                decoder.DecodeMethodSignature(blobMethod->data(), blobMethod->size(),
                    genericContext);
            WriteTo(methodSignature.Header, output);
            methodSignature.ReturnType(Disassembler::ILNameSyntax::SignatureNoNamedTypeParameters);
            output.Write(' ');
            if ((methodToken >> 24) == 0x06) {  // the C# `case HandleKind.MethodDefinition`
                std::string methodName = module.GetMethodName(methodToken);
                std::uint32_t declaringType = module.GetMethodDeclaringTypeToken(methodToken);
                if (declaringType != 0) {
                    WriteTo(module, output, genericContext, declaringType,
                        Disassembler::ILNameSyntax::TypeName);
                    output.Write("::");
                }
                bool isCompilerControlled =
                    (module.GetMethodAttributes(methodToken) & 0x0007u) == 0x0000u;
                if (isCompilerControlled) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "$PST%08X",
                        static_cast<unsigned>(methodToken));
                    output.Write(Disassembler::Escape(methodName + buf));
                } else {
                    output.Write(Disassembler::Escape(methodName));
                }
            } else {  // the C# `case HandleKind.MemberReference`
                auto mr = module.GetMemberReference(methodToken);
                if (!mr)
                    throw std::logic_error("member reference");
                std::string memberName = mr->Name;
                WriteParent(output, module, mr->ParentToken, genericContext, syntax);
                output.Write("::");
                output.Write(Disassembler::Escape(memberName));
            }
            WriteTypeParameterList(output, syntax, substitution);
            WriteParameterList(output, methodSignature);
            break;
        }
        case 0x11:  // HandleKind.StandaloneSignature
        {
            // The C# `header.Kind == SignatureKind.Method` decode; every other
            // kind falls into the `@token /* signature <Kind> */` spelling.
            auto blob = module.GetStandaloneSignatureBlob(entityToken);
            if (!blob || blob->empty())
                throw std::logic_error("standalone signature");
            std::uint8_t rawKind = (*blob)[0] & 0x0F;
            if (rawKind == 0x00) {  // SignatureKind.Method
                Disassembler::DisassemblerSignatureTypeProvider provider(module, output);
                Metadata::SignatureTypeProviderDecoder decoder(provider, module);
                Metadata::MethodSignatureT methodSignature =
                    decoder.DecodeMethodSignature(blob->data(), blob->size(), genericContext);
                WriteTo(methodSignature.Header, output);
                methodSignature.ReturnType(
                    Disassembler::ILNameSyntax::SignatureNoNamedTypeParameters);
                WriteParameterList(output, methodSignature);
            } else {
                const char* kindName;
                switch (rawKind) {
                    case 0x06: kindName = "Field"; break;
                    case 0x07: kindName = "LocalVariables"; break;
                    case 0x08: kindName = "Property"; break;
                    case 0x0A: kindName = "FunctionPointer"; break;
                    default: kindName = nullptr; break;
                }
                char buf[48];
                if (kindName) {
                    std::snprintf(buf, sizeof(buf), "@%08X /* signature %s */",
                        static_cast<unsigned>(entityToken), kindName);
                } else {
                    std::snprintf(buf, sizeof(buf), "@%08X /* signature %u */",
                        static_cast<unsigned>(entityToken), static_cast<unsigned>(rawKind));
                }
                output.Write(buf);
            }
            break;
        }
        default:
            // The C# `output.Write($"@{MetadataTokens.GetToken(entity):X8}")`
            // default arm; the member-table arms the full C# switch carries
            // are deferred (they throw so a missing port is loud).
            char buf[16];
            std::snprintf(buf, sizeof(buf), "@%08X", static_cast<unsigned>(entityToken));
            output.Write(buf);
            break;
    }
}

} // namespace ILSpy::Decompiler::IL
