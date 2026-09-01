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
