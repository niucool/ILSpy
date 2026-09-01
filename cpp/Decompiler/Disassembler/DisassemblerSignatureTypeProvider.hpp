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

// Port of ICSharpCode.Decompiler/Disassembler/DisassemblerSignatureTypeProvider.cs
// -- the ISignatureTypeProvider<Action<ILNameSyntax>, MetadataGenericContext>
// implementation that renders signature types as ILDasm text while they decode.
// The C# provider's TType is a deferred text callback; the port instantiates the
// C++-only SignatureTypeProvider contract (Metadata/SignatureTypeProvider.hpp, the
// SRM SignatureDecoder gap fill) at TType = the same deferred writer, so the
// decode produces a tree of writers that compose exactly like the C# closures.

#pragma once

#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

#include <string>

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
}

namespace ILSpy::Decompiler::Disassembler {

// The C# `public class DisassemblerSignatureTypeProvider :
// ISignatureTypeProvider<Action<ILNameSyntax>, MetadataGenericContext>`. The
// module/output pair are the C# constructor's non-null dependencies (the C#
// ArgumentNullException ports to std::invalid_argument). Every returned writer
// captures `this` (the C# captures the provider's `output` field), so the
// provider must outlive the writers it produced -- the same liveness contract
// the C# reference-type field has.
class DisassemblerSignatureTypeProvider final : public Metadata::ISignatureTypeProvider {
public:
    DisassemblerSignatureTypeProvider(const Metadata::MetadataFile& module,
        Output::ITextOutput& output);

    Metadata::SignatureTypeWriter GetPrimitiveType(Metadata::PrimitiveTypeCode typeCode) override;
    Metadata::SignatureTypeWriter GetTypeFromDefinition(std::uint32_t typeDefToken,
        std::uint8_t rawTypeKind) override;
    Metadata::SignatureTypeWriter GetTypeFromReference(std::uint32_t typeRefToken,
        std::uint8_t rawTypeKind) override;
    Metadata::SignatureTypeWriter GetTypeFromSpecification(std::uint32_t typeSpecToken,
        std::uint8_t rawTypeKind,
        const Metadata::MetadataGenericContext& genericContext) override;
    Metadata::SignatureTypeWriter GetSZArrayType(Metadata::SignatureTypeWriter elementType) override;
    Metadata::SignatureTypeWriter GetPointerType(Metadata::SignatureTypeWriter elementType) override;
    Metadata::SignatureTypeWriter GetByReferenceType(Metadata::SignatureTypeWriter elementType) override;
    Metadata::SignatureTypeWriter GetPinnedType(Metadata::SignatureTypeWriter elementType) override;
    Metadata::SignatureTypeWriter GetArrayType(Metadata::SignatureTypeWriter elementType,
        const Metadata::ArrayShape& shape) override;
    Metadata::SignatureTypeWriter GetGenericInstantiation(Metadata::SignatureTypeWriter genericType,
        std::vector<Metadata::SignatureTypeWriter> typeArguments) override;
    Metadata::SignatureTypeWriter GetGenericTypeParameter(
        const Metadata::MetadataGenericContext& genericContext, int index) override;
    Metadata::SignatureTypeWriter GetGenericMethodParameter(
        const Metadata::MetadataGenericContext& genericContext, int index) override;
    Metadata::SignatureTypeWriter GetModifiedType(Metadata::SignatureTypeWriter modifier,
        Metadata::SignatureTypeWriter unmodifiedType, bool isRequired) override;
    Metadata::SignatureTypeWriter GetFunctionPointerType(
        const Metadata::MethodSignatureT& signature) override;

private:
    const Metadata::MetadataFile& module_;
    Output::ITextOutput& output_;

    // The C# `void WriteTypeParameter(GenericParameterHandle paramRef, int index,
    // ILNameSyntax syntax)` -- the bare index for a nil handle or the
    // SignatureNoNamedTypeParameters syntax, else the row's escaped name (or its
    // position when the row carries no name).
    void WriteTypeParameter(std::uint32_t genericParamToken, int index,
        ILNameSyntax syntax);
};

} // namespace ILSpy::Decompiler::Disassembler
