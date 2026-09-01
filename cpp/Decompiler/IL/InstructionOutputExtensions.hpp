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

// Port of ICSharpCode.Decompiler/IL/InstructionOutputExtensions.cs -- the
// `EntityHandle.WriteTo` family the ExceptionRegion catch-type writer (the
// gnhf-139..142 deferral) and the DisassemblerSignatureTypeProvider's
// GetTypeFromDefinition/Reference arms consume. The port models an EntityHandle
// as the raw 32-bit metadata token (the MetadataFile convention).
//
// This iteration ports the arms the type-name rendering path needs:
//   * WriteTo(SignatureHeader)     -- the "instance explicit "/"instance " +
//     calling-convention prefix writer (the FnPtr arm consumes it now);
//   * WriteParameterList           -- the "(...)" parameter writer;
//   * WriteTo(entity) nil/TypeDefinition/TypeReference/TypeSpecification and
//     the `@{token:X8}` default -- the TypeSpec arm decodes its signature
//     through the DisassemblerSignatureTypeProvider.
// The member arms (FieldDefinition/MethodDefinition/MemberReference/
// MethodSpecification/StandaloneSignature -- the reflection-disassembler
// member-name rendering) are DEFERRED with the MethodBodyDisassembler
// region and throw std::logic_error.

#pragma once

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"  // ILNameSyntax
#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"    // MethodSignatureT

#include <cstdint>

namespace ILSpy::Decompiler::Output {
class ITextOutput;
}

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
}

namespace ILSpy::Decompiler::IL {

// The C# `internal static void WriteTo(this in SignatureHeader header,
// ITextOutput output)`: "instance explicit "/"instance " per the flags, then
// the non-default convention via Metadata::ToILSyntax followed by a space.
void WriteTo(const Metadata::SignatureHeader& header, Output::ITextOutput& output);

// The C# `internal static void WriteParameterList(ITextOutput output,
// MethodSignature<Action<ILNameSyntax>> methodSignature)`: the parenthesized
// parameter types at SignatureNoNamedTypeParameters, with the "..., "
// separator inserted at the vararg RequiredParameterCount position.
void WriteParameterList(Output::ITextOutput& output,
    const Metadata::MethodSignatureT& methodSignature);

// The C# `public static void WriteTo(this EntityHandle entity, MetadataFile
// module, ITextOutput output, MetadataGenericContext genericContext,
// ILNameSyntax syntax = ILNameSyntax.Signature)`. The handle ports as the raw
// metadata token. The nil token renders "<nil>" (the C# `if (entity.IsNil)`),
// TypeDefinition/TypeReference render the resolved full type name (the
// TypeReference arm keeps the `[assembly]` scope prefix), TypeSpecification
// decodes its signature blob through the DisassemblerSignatureTypeProvider,
// and every other kind renders `@{token:X8}` (the C# default arm; the
// member-table arms throw "not yet ported" instead of falling into it, so a
// missing port is loud rather than a wrong name).
void WriteTo(const Metadata::MetadataFile& module, Output::ITextOutput& output,
    const Metadata::MetadataGenericContext& genericContext, std::uint32_t entityToken,
    Disassembler::ILNameSyntax syntax = Disassembler::ILNameSyntax::Signature);

} // namespace ILSpy::Decompiler::IL
