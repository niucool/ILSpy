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

// The DisassemblerSignatureTypeProvider implementation -- see the header for
// the port rationale. Each Get* returns a writer lambda faithful to the C#
// method body; the `syntaxForElementTypes` demotion
// (SignatureNoNamedTypeParameters propagates through wrappers) is kept on
// every composite so a NoNamedTypeParameters request renders type arguments
// positionally inside the wrapper too.

#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/IL/InstructionOutputExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::Disassembler {

using Metadata::SignatureTypeWriter;
using Disassembler::ILNameSyntax;

DisassemblerSignatureTypeProvider::DisassemblerSignatureTypeProvider(
    const Metadata::MetadataFile& module, Output::ITextOutput& output)
    : module_(module), output_(output) {}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetArrayType(
        SignatureTypeWriter elementType, const Metadata::ArrayShape& shape) {
    // The C# body: the element type with the demoted syntax, then the
    // bracketed per-dimension shape (`[0..., 0...]`-style bounds).
    return [this, elementType = std::move(elementType), shape](ILNameSyntax syntax) {
        auto syntaxForElementTypes = syntax == ILNameSyntax::SignatureNoNamedTypeParameters
            ? syntax : ILNameSyntax::Signature;
        elementType(syntaxForElementTypes);
        output_.Write('[');
        for (std::uint32_t i = 0; i < shape.Rank; i++) {
            if (i > 0)
                output_.Write(", ");
            if (i < shape.LowerBounds.size() || i < shape.Sizes.size()) {
                std::int32_t lower = 0;
                if (i < shape.LowerBounds.size()) {
                    lower = shape.LowerBounds[i];
                    output_.Write(std::to_string(lower));
                }
                output_.Write("...");
                if (i < shape.Sizes.size())
                    output_.Write(std::to_string(lower + shape.Sizes[i] - 1));
            }
        }
        output_.Write(']');
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetByReferenceType(
        SignatureTypeWriter elementType) {
    return [this, elementType = std::move(elementType)](ILNameSyntax syntax) {
        auto syntaxForElementTypes = syntax == ILNameSyntax::SignatureNoNamedTypeParameters
            ? syntax : ILNameSyntax::Signature;
        elementType(syntaxForElementTypes);
        output_.Write('&');
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetFunctionPointerType(
        const Metadata::MethodSignatureT& signature) {
    // The C# `public Action<ILNameSyntax> GetFunctionPointerType(
    // MethodSignature<Action<ILNameSyntax>> signature)`: "method " + the
    // header + the return type + " *(" + the parameters + ")".
    return [this, signature](ILNameSyntax syntax) {
        output_.Write("method ");
        IL::WriteTo(signature.Header, output_);
        signature.ReturnType(syntax);
        output_.Write(" *(");
        for (std::size_t i = 0; i < signature.ParameterTypes.size(); i++) {
            if (i > 0)
                output_.Write(", ");
            signature.ParameterTypes[i](syntax);
        }
        output_.Write(')');
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetGenericInstantiation(
        SignatureTypeWriter genericType, std::vector<SignatureTypeWriter> typeArguments) {
    return [this, genericType = std::move(genericType),
            typeArguments = std::move(typeArguments)](ILNameSyntax syntax) {
        auto syntaxForElementTypes = syntax == ILNameSyntax::SignatureNoNamedTypeParameters
            ? syntax : ILNameSyntax::Signature;
        genericType(syntaxForElementTypes);
        output_.Write('<');
        for (std::size_t i = 0; i < typeArguments.size(); i++) {
            if (i > 0)
                output_.Write(", ");
            typeArguments[i](syntaxForElementTypes);
        }
        output_.Write('>');
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetGenericMethodParameter(
        const Metadata::MetadataGenericContext& genericContext, int index) {
    return [this, genericContext, index](ILNameSyntax syntax) {
        output_.Write("!!");
        WriteTypeParameter(genericContext.GetGenericMethodTypeParameterHandleOrNull(index),
            index, syntax);
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetGenericTypeParameter(
        const Metadata::MetadataGenericContext& genericContext, int index) {
    return [this, genericContext, index](ILNameSyntax syntax) {
        output_.Write("!");
        WriteTypeParameter(genericContext.GetGenericTypeParameterHandleOrNull(index),
            index, syntax);
    };
}

void DisassemblerSignatureTypeProvider::WriteTypeParameter(std::uint32_t genericParamToken,
    int index, ILNameSyntax syntax) {
    // The C# body: nil handle or the no-named-parameters syntax -> the bare
    // index; else the row's name (escaped) or its position when unnamed.
    if (genericParamToken == 0 || syntax == ILNameSyntax::SignatureNoNamedTypeParameters) {
        output_.Write(std::to_string(index));
        return;
    }
    auto row = module_.GetGenericParameterByToken(genericParamToken);
    if (!row) {
        // The C# SRM row fetch would throw on a dangling handle; the port's
        // never-throw read falls back to the position (a documented
        // divergence confined to corrupt metadata).
        output_.Write(std::to_string(index));
        return;
    }
    if (row->Name.empty()) {
        output_.Write(std::to_string(row->Number));
        return;
    }
    output_.Write(Escape(row->Name));
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetModifiedType(
        SignatureTypeWriter modifier, SignatureTypeWriter unmodifiedType, bool isRequired) {
    return [this, modifier = std::move(modifier),
            unmodifiedType = std::move(unmodifiedType), isRequired](ILNameSyntax syntax) {
        unmodifiedType(syntax);
        if (isRequired)
            output_.Write(" modreq");
        else
            output_.Write(" modopt");
        output_.Write('(');
        modifier(ILNameSyntax::TypeName);
        output_.Write(')');
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetPinnedType(
        SignatureTypeWriter elementType) {
    return [this, elementType = std::move(elementType)](ILNameSyntax syntax) {
        auto syntaxForElementTypes = syntax == ILNameSyntax::SignatureNoNamedTypeParameters
            ? syntax : ILNameSyntax::Signature;
        elementType(syntaxForElementTypes);
        output_.Write(" pinned");
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetPointerType(
        SignatureTypeWriter elementType) {
    return [this, elementType = std::move(elementType)](ILNameSyntax syntax) {
        auto syntaxForElementTypes = syntax == ILNameSyntax::SignatureNoNamedTypeParameters
            ? syntax : ILNameSyntax::Signature;
        elementType(syntaxForElementTypes);
        output_.Write('*');
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetPrimitiveType(
        Metadata::PrimitiveTypeCode typeCode) {
    // The C# switch: each primitive gets a writer that emits its ILDasm
    // spelling (a bare string write, no leading/trailing space).
    switch (typeCode) {
        case Metadata::PrimitiveTypeCode::SByte:
            return [this](ILNameSyntax) { output_.Write("int8"); };
        case Metadata::PrimitiveTypeCode::Int16:
            return [this](ILNameSyntax) { output_.Write("int16"); };
        case Metadata::PrimitiveTypeCode::Int32:
            return [this](ILNameSyntax) { output_.Write("int32"); };
        case Metadata::PrimitiveTypeCode::Int64:
            return [this](ILNameSyntax) { output_.Write("int64"); };
        case Metadata::PrimitiveTypeCode::Byte:
            return [this](ILNameSyntax) { output_.Write("uint8"); };
        case Metadata::PrimitiveTypeCode::UInt16:
            return [this](ILNameSyntax) { output_.Write("uint16"); };
        case Metadata::PrimitiveTypeCode::UInt32:
            return [this](ILNameSyntax) { output_.Write("uint32"); };
        case Metadata::PrimitiveTypeCode::UInt64:
            return [this](ILNameSyntax) { output_.Write("uint64"); };
        case Metadata::PrimitiveTypeCode::Single:
            return [this](ILNameSyntax) { output_.Write("float32"); };
        case Metadata::PrimitiveTypeCode::Double:
            return [this](ILNameSyntax) { output_.Write("float64"); };
        case Metadata::PrimitiveTypeCode::Void:
            return [this](ILNameSyntax) { output_.Write("void"); };
        case Metadata::PrimitiveTypeCode::Boolean:
            return [this](ILNameSyntax) { output_.Write("bool"); };
        case Metadata::PrimitiveTypeCode::String:
            return [this](ILNameSyntax) { output_.Write("string"); };
        case Metadata::PrimitiveTypeCode::Char:
            return [this](ILNameSyntax) { output_.Write("char"); };
        case Metadata::PrimitiveTypeCode::Object:
            return [this](ILNameSyntax) { output_.Write("object"); };
        case Metadata::PrimitiveTypeCode::IntPtr:
            return [this](ILNameSyntax) { output_.Write("native int"); };
        case Metadata::PrimitiveTypeCode::UIntPtr:
            return [this](ILNameSyntax) { output_.Write("native uint"); };
        case Metadata::PrimitiveTypeCode::TypedReference:
            return [this](ILNameSyntax) { output_.Write("typedref"); };
    }
    // The C# `throw new ArgumentOutOfRangeException()`.
    throw std::out_of_range("DisassemblerSignatureTypeProvider: unknown PrimitiveTypeCode");
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetSZArrayType(
        SignatureTypeWriter elementType) {
    return [this, elementType = std::move(elementType)](ILNameSyntax syntax) {
        auto syntaxForElementTypes = syntax == ILNameSyntax::SignatureNoNamedTypeParameters
            ? syntax : ILNameSyntax::Signature;
        elementType(syntaxForElementTypes);
        output_.Write('[');
        output_.Write(']');
    };
}

namespace {

// The C# `switch (rawTypeKind)` shared by GetTypeFromDefinition/Reference:
// 0x00 none, 0x11 "valuetype ", 0x12 "class ", anything else throws (the C#
// BadImageFormatException ports to std::logic_error per the decoder
// convention).
void WriteRawTypeKindPrefix(Output::ITextOutput& output, std::uint8_t rawTypeKind) {
    switch (rawTypeKind) {
        case 0x00:
            break;
        case 0x11:
            output.Write("valuetype ");
            break;
        case 0x12:
            output.Write("class ");
            break;
        default:
            throw std::logic_error("DisassemblerSignatureTypeProvider: unexpected rawTypeKind");
    }
}

} // namespace

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetTypeFromDefinition(
        std::uint32_t typeDefToken, std::uint8_t rawTypeKind) {
    // The C# `return syntax => { switch ...; ((EntityHandle)handle).WriteTo(
    // module, output, default); }` -- the DEFAULT generic context (nil), not
    // the provider's caller context (the C# passes `default` here).
    return [this, typeDefToken, rawTypeKind](ILNameSyntax) {
        WriteRawTypeKindPrefix(output_, rawTypeKind);
        IL::WriteTo(module_, output_, Metadata::MetadataGenericContext{}, typeDefToken);
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetTypeFromReference(
        std::uint32_t typeRefToken, std::uint8_t rawTypeKind) {
    return [this, typeRefToken, rawTypeKind](ILNameSyntax) {
        WriteRawTypeKindPrefix(output_, rawTypeKind);
        IL::WriteTo(module_, output_, Metadata::MetadataGenericContext{}, typeRefToken);
    };
}

Metadata::SignatureTypeWriter DisassemblerSignatureTypeProvider::GetTypeFromSpecification(
        std::uint32_t typeSpecToken, std::uint8_t rawTypeKind,
        const Metadata::MetadataGenericContext& genericContext) {
    // The C# `return reader.GetTypeSpecification(handle).DecodeSignature(this,
    // genericContext)` -- the TypeSpec row's signature blob decoded through
    // this provider. The rawTypeKind is consumed by the row's inner
    // CLASS/VALUETYPE marker in the blob (the C# TypeSpecification.
    // DecodeSignature never consults it), so it is dropped here faithfully.
    (void)rawTypeKind;
    auto blob = module_.GetTypeSpecSignatureBlob(typeSpecToken);
    if (!blob)
        throw std::out_of_range("DisassemblerSignatureTypeProvider: invalid TypeSpec token");
    Metadata::SignatureTypeProviderDecoder decoder(*this, module_);
    return decoder.DecodeType(blob->data(), blob->size(), genericContext);
}

} // namespace ILSpy::Decompiler::Disassembler
