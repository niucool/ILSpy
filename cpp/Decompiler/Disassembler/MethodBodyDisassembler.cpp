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

// The MethodBodyDisassembler implementation -- see the header for the port
// rationale and the deferred members.

#include "Decompiler/Disassembler/MethodBodyDisassembler.hpp"

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"
#include "Decompiler/Disassembler/ILParser.hpp"
#include "Decompiler/Disassembler/ILStructure.hpp"
#include "Decompiler/IL/InstructionOutputExtensions.hpp"
#include "Decompiler/Disassembler/OpCodeInfo.hpp"
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MethodBody.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Util/BitSet.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace ILSpy::Decompiler::Disassembler {

namespace {

// MetadataTokenHelpers.TryAsEntityHandle: the EntityHandle table set -- every
// row table through 0x2B except the pointer tables (0x03/0x05/0x07/0x13/0x16);
// the 0x70 UserString heap is not an EntityHandle.
bool TryAsEntityHandle(std::uint32_t metadataToken)
{
    switch (metadataToken >> 24) {
        case 0x00: case 0x01: case 0x02: case 0x04: case 0x06: case 0x08:
        case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E:
        case 0x0F: case 0x10: case 0x11: case 0x12: case 0x14: case 0x15:
        case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B: case 0x1C:
        case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
        case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A: case 0x2B:
            return true;
        default:
            return false;
    }
}

// The little-endian int32 read (the C# blob.ReadInt32()).
std::int32_t ReadI32(const std::uint8_t* base, std::size_t size, std::size_t& pos)
{
    std::uint32_t v = static_cast<std::uint32_t>(base[pos])
        | (static_cast<std::uint32_t>(base[pos + 1]) << 8)
        | (static_cast<std::uint32_t>(base[pos + 2]) << 16)
        | (static_cast<std::uint32_t>(base[pos + 3]) << 24);
    pos += 4;
    return static_cast<std::int32_t>(v);
}

// The C# `output.Write($".emitbyte 0x{(byte)value:x}")` spelling -- the full
// byte value in lowercase hex (0x24 renders "24", 0x0A renders "a").
std::string EmitByteHex(int value)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), ".emitbyte 0x%x", value & 0xFF);
    return buf;
}

}  // namespace

MethodBodyDisassembler::MethodBodyDisassembler(Output::ITextOutput& output)
    : output_(output)
{
}

// ---------------------------------------------------------------------------
// WriteOpCode (MethodBodyDisassembler.cs lines 569-590).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::WriteOpCode(Metadata::ILOpCode opCode)
{
    OpCodeInfo opCodeInfo(opCode, std::string{ Metadata::GetDisplayName(opCode) });
    std::string index;
    switch (opCode) {
        case Metadata::ILOpCode::Ldarg_0:
        case Metadata::ILOpCode::Ldarg_1:
        case Metadata::ILOpCode::Ldarg_2:
        case Metadata::ILOpCode::Ldarg_3:
            output_.WriteReference(opCodeInfo, /*omitSuffix=*/true);
            // The C# `opCodeInfo.Name.Substring(6)` after "ldarg.".
            index = std::string(opCodeInfo.Name()).substr(6);
            // The C# passes the fresh "param_" + index string as the opaque
            // reference token; the port reinterprets the index (the
            // iteration-134 const void* convention -- PlainTextOutput ignores
            // it).
            output_.WriteLocalReference(index,
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                    static_cast<unsigned int>(std::stoul(index)))));
            break;
        case Metadata::ILOpCode::Ldloc_0:
        case Metadata::ILOpCode::Ldloc_1:
        case Metadata::ILOpCode::Ldloc_2:
        case Metadata::ILOpCode::Ldloc_3:
        case Metadata::ILOpCode::Stloc_0:
        case Metadata::ILOpCode::Stloc_1:
        case Metadata::ILOpCode::Stloc_2:
        case Metadata::ILOpCode::Stloc_3:
            output_.WriteReference(opCodeInfo, /*omitSuffix=*/true);
            // The C# `opCodeInfo.Name.Substring(6)` after "ldloc."/"stloc.".
            index = std::string(opCodeInfo.Name()).substr(6);
            output_.WriteLocalReference(index,
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                    static_cast<unsigned int>(std::stoul(index)))));
            break;
        default:
            output_.WriteReference(opCodeInfo);
            break;
    }
}

// ---------------------------------------------------------------------------
// WriteRVA (MethodBodyDisassembler.cs lines 592-610).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::WriteRVA(const std::uint8_t* base, std::size_t size,
    std::size_t pos, std::uint32_t offset, Metadata::ILOpCode opCode)
{
    if (!ShowRawRVAOffsetAndBytes) return;
    // The C# BlobReader is a value type -- the operand walk is local to the
    // comment and never touches the caller's cursor.
    std::size_t tmp = pos;
    if (opCode == Metadata::ILOpCode::Switch) {
        if (tmp + 4 <= size) tmp += 4;
    } else {
        Disassembler::SkipOperand(base, size, tmp, opCode);
    }
    output_.Write("/* ");
    // The C# `output.Write($"0x{offset:X8} {(ushort)opCode:X2}")` -- X2 is a
    // minimum width, so the two-byte opcodes render all four hex digits.
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X %02X", static_cast<unsigned>(offset),
        static_cast<unsigned>(opCode));
    output_.Write(buf);
    int appendSpaces = static_cast<unsigned>(opCode) > 0xFF ? 14 : 16;
    while (pos < tmp) {
        char byteBuf[4];
        std::snprintf(byteBuf, sizeof(byteBuf), "%02X",
            static_cast<unsigned>(base[pos++]));
        output_.Write(byteBuf);
        appendSpaces -= 2;
    }
    if (appendSpaces > 0) {
        output_.Write(std::string(static_cast<std::size_t>(appendSpaces), ' '));
    }
    output_.Write(" */ ");
}

// ---------------------------------------------------------------------------
// WriteMetadataToken (MethodBodyDisassembler.cs lines 628-639).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::WriteMetadataToken(const Metadata::MetadataFile& module,
    std::uint32_t handleToken, std::uint32_t metadataToken, bool spaceBefore)
{
    ReflectionDisassembler::WriteMetadataToken(output_, module, handleToken,
        metadataToken, /*spaceAfter=*/false, spaceBefore, ShowMetadataTokens,
        ShowMetadataTokensInBase10);
}

// ---------------------------------------------------------------------------
// WriteInstruction (MethodBodyDisassembler.cs lines 327-567).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::WriteInstruction(Metadata::MetadataFile& module,
    std::uint32_t methodToken, const std::uint8_t* base, std::size_t size,
    std::size_t& pos, std::uint32_t methodRva)
{
    std::size_t offset = pos;
    Metadata::ILOpCode opCode = Metadata::DecodeOpCode(base, size, pos);
    auto opType = Metadata::GetOperandType(opCode);
    auto genericContext = Metadata::MetadataGenericContext::ForMethod(methodToken, module);
    if (Metadata::IsDefined(opCode)
        && static_cast<std::size_t>(Metadata::OperandFixedSize(opType)) <= size - pos) {
        WriteRVA(base, size, pos, static_cast<std::uint32_t>(offset) + methodRva, opCode);
        output_.WriteLocalReference(OffsetToString(static_cast<int>(offset)),
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset)));
        output_.Write(": ");
        WriteOpCode(opCode);
        switch (opType) {
            case Metadata::OperandType::BrTarget:
            case Metadata::OperandType::ShortBrTarget:
            {
                output_.Write(' ');
                int targetOffset = DecodeBranchTarget(base, size, pos, opCode);
                char buf[8];
                std::snprintf(buf, sizeof(buf), "IL_%04x",
                    static_cast<unsigned>(targetOffset));
                output_.WriteLocalReference(buf,
                    reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                        static_cast<unsigned int>(targetOffset))));
                break;
            }
            case Metadata::OperandType::Field:
            case Metadata::OperandType::Method:
            case Metadata::OperandType::Sig:
            case Metadata::OperandType::Type:
            {
                output_.Write(' ');
                std::uint32_t metadataToken = static_cast<std::uint32_t>(ReadI32(base, size, pos));
                bool ok = TryAsEntityHandle(metadataToken);
                if (ok) {
                    // The C# `catch (BadImageFormatException) { handle = null; }`.
                    try {
                        IL::WriteTo(module, output_, genericContext, metadataToken);
                    } catch (const std::exception&) {
                        ok = false;
                    }
                }
                WriteMetadataToken(module, ok ? metadataToken : 0, metadataToken, /*spaceBefore=*/true);
                break;
            }
            case Metadata::OperandType::Tok:
            {
                output_.Write(' ');
                std::uint32_t metadataToken = static_cast<std::uint32_t>(ReadI32(base, size, pos));
                bool ok = TryAsEntityHandle(metadataToken);
                if (ok) {
                    // The C# member-reference `method `/`field ` prefixes (the
                    // C# switches on the handle kind; a MemberReference's kind
                    // comes from its signature-blob nibble).
                    switch (metadataToken >> 24) {
                        case 0x0A:
                        {
                            auto blob = module.GetSignatureBlob(metadataToken);
                            if (blob && !blob->empty() && ((*blob)[0] & 0x0F) == 0x00) {
                                output_.Write("method ");
                            } else {
                                output_.Write("field ");
                            }
                            break;
                        }
                        case 0x04:
                            output_.Write("field ");
                            break;
                        case 0x06:
                            output_.Write("method ");
                            break;
                    }
                    try {
                        IL::WriteTo(module, output_, genericContext, metadataToken);
                    } catch (const std::exception&) {
                        ok = false;
                    }
                }
                WriteMetadataToken(module, ok ? metadataToken : 0, metadataToken, /*spaceBefore=*/true);
                break;
            }
            case Metadata::OperandType::ShortI:
            {
                output_.Write(' ');
                Disassembler::WriteOperand(output_, static_cast<std::int64_t>(
                    static_cast<std::int8_t>(base[pos++])));
                break;
            }
            case Metadata::OperandType::I:
            {
                output_.Write(' ');
                Disassembler::WriteOperand(output_, static_cast<std::int64_t>(
                    ReadI32(base, size, pos)));
                break;
            }
            case Metadata::OperandType::I8:
            {
                output_.Write(' ');
                std::uint64_t raw = 0;
                for (int i = 0; i < 8; i++) {
                    raw |= static_cast<std::uint64_t>(base[pos + i]) << (8 * i);
                }
                pos += 8;
                Disassembler::WriteOperand(output_, static_cast<std::int64_t>(raw));
                break;
            }
            case Metadata::OperandType::ShortR:
            {
                output_.Write(' ');
                std::uint32_t raw = static_cast<std::uint32_t>(ReadI32(base, size, pos));
                float value;
                std::memcpy(&value, &raw, sizeof(value));
                Disassembler::WriteOperand(output_, value);
                break;
            }
            case Metadata::OperandType::R:
            {
                output_.Write(' ');
                std::uint64_t raw = 0;
                for (int i = 0; i < 8; i++) {
                    raw |= static_cast<std::uint64_t>(base[pos + i]) << (8 * i);
                }
                pos += 8;
                double value;
                std::memcpy(&value, &raw, sizeof(value));
                Disassembler::WriteOperand(output_, value);
                break;
            }
            case Metadata::OperandType::String:
            {
                std::uint32_t metadataToken = static_cast<std::uint32_t>(ReadI32(base, size, pos));
                output_.Write(' ');
                // The C# `metadata.GetUserString(userString.Value)` under a
                // BadImageFormatException catch: a null handle renders no
                // operand and the always-on token comment.
                auto text = module.TryGetUserString(metadataToken);
                if (text.has_value()) {
                    Disassembler::WriteOperand(output_, std::string_view(*text));
                }
                WriteMetadataToken(module, text.has_value() ? metadataToken : 0, metadataToken,
                    /*spaceBefore=*/true);
                break;
            }
            case Metadata::OperandType::Switch:
            {
                std::size_t tmp = pos;
                auto targets = DecodeSwitchTargets(base, size, pos);
                if (ShowRawRVAOffsetAndBytes) {
                    Output::WriteLine(output_, " (");
                } else {
                    output_.Write(" (");
                }
                tmp += 4;
                for (std::size_t i = 0; i < targets.size(); i++) {
                    if (i > 0) {
                        if (ShowRawRVAOffsetAndBytes) {
                            Output::WriteLine(output_, ",");
                        } else {
                            output_.Write(", ");
                        }
                    }
                    if (ShowRawRVAOffsetAndBytes) {
                        output_.Write("/*              ");
                        char rawBuf[12];
                        std::snprintf(rawBuf, sizeof(rawBuf), "%02X%02X%02X%02X",
                            static_cast<unsigned>(base[tmp]),
                            static_cast<unsigned>(base[tmp + 1]),
                            static_cast<unsigned>(base[tmp + 2]),
                            static_cast<unsigned>(base[tmp + 3]));
                        output_.Write(rawBuf);
                        output_.Write("         */ ");
                    }
                    if (ShowRawRVAOffsetAndBytes) {
                        output_.Write("                 ");
                    }
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "IL_%04x",
                        static_cast<unsigned>(targets[i]));
                    output_.WriteLocalReference(buf,
                        reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                            static_cast<unsigned int>(targets[i]))));
                    tmp += 4;
                }
                output_.Write(")");
                break;
            }
            case Metadata::OperandType::Variable:
            {
                output_.Write(' ');
                std::uint32_t index = static_cast<std::uint32_t>(base[pos])
                    | (static_cast<std::uint32_t>(base[pos + 1]) << 8);
                pos += 2;
                if (opCode == Metadata::ILOpCode::Ldloc || opCode == Metadata::ILOpCode::Ldloca
                    || opCode == Metadata::ILOpCode::Stloc) {
                    Disassembler::WriteVariableReference(output_,
                        static_cast<int>(index));
                } else {
                    Disassembler::WriteParameterReference(output_, module, methodToken,
                        static_cast<int>(index));
                }
                break;
            }
            case Metadata::OperandType::ShortVariable:
            {
                output_.Write(' ');
                int index = base[pos++];
                if (opCode == Metadata::ILOpCode::Ldloc_s
                    || opCode == Metadata::ILOpCode::Ldloca_s
                    || opCode == Metadata::ILOpCode::Stloc_s) {
                    Disassembler::WriteVariableReference(output_, index);
                } else {
                    Disassembler::WriteParameterReference(output_, module, methodToken, index);
                }
                break;
            }
            case Metadata::OperandType::None:
            case Metadata::OperandType::Undefined:
                break;
        }
    } else {
        // The C# `.emitbyte` fallback for an undefined opcode or a truncated
        // operand: the stream restarts from the instruction offset.
        pos = offset;
        std::uint16_t opCodeValue = static_cast<std::uint16_t>(
            Metadata::DecodeOpCode(base, size, pos));
        if (opCodeValue > 0xFF) {
            if (ShowRawRVAOffsetAndBytes) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "/* 0x%08X %02X                 */ ",
                    static_cast<unsigned>(offset + methodRva),
                    static_cast<unsigned>(opCodeValue >> 8));
                output_.Write(buf);
            }
            output_.WriteLocalReference(OffsetToString(static_cast<int>(offset)),
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset)));
            output_.Write(": ");
            // split 16-bit value into two emitbyte directives
            Output::WriteLine(output_, EmitByteHex(static_cast<int>(opCodeValue >> 8)));
            if (ShowRawRVAOffsetAndBytes) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "/* 0x%08X %02X                 */ ",
                    static_cast<unsigned>(offset + methodRva + 1),
                    static_cast<unsigned>(opCodeValue & 0xFF));
                output_.Write(buf);
            }
            // add label
            output_.WriteLocalReference(OffsetToString(static_cast<int>(offset) + 1),
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                    static_cast<unsigned int>(static_cast<int>(offset) + 1))));
            output_.Write(": ");
            output_.Write(EmitByteHex(static_cast<int>(opCodeValue & 0xFF)));
        } else {
            if (ShowRawRVAOffsetAndBytes) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "/* 0x%08X %02X                 */ ",
                    static_cast<unsigned>(offset + methodRva),
                    static_cast<unsigned>(opCodeValue & 0xFF));
                output_.Write(buf);
            }
            output_.WriteLocalReference(OffsetToString(static_cast<int>(offset)),
                reinterpret_cast<const void*>(static_cast<std::uintptr_t>(offset)));
            output_.Write(": ");
            output_.Write(EmitByteHex(static_cast<int>(opCodeValue & 0xFF)));
        }
    }
    output_.WriteLine();
}

// ---------------------------------------------------------------------------
// WriteExceptionHandlers (MethodBodyDisassembler.cs lines 198-212).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::WriteExceptionHandlers(const Metadata::MetadataFile& module,
    std::uint32_t methodToken, const Metadata::MethodBody& body)
{
    if (body.Handlers().empty()) return;
    output_.WriteLine();
    auto genericContext = Metadata::MetadataGenericContext::ForMethod(methodToken, module);
    for (const auto& eh : body.Handlers()) {
        Disassembler::WriteTo(eh, module, genericContext, output_);
        output_.WriteLine();
    }
}

// ---------------------------------------------------------------------------
// DisassembleLocalsBlock (MethodBodyDisassembler.cs lines 158-196).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::DisassembleLocalsBlock(const Metadata::MetadataFile& module,
    std::uint32_t methodToken, const Metadata::MethodBody& body)
{
    if (body.LocalVarSigToken() == 0) return;
    output_.Write(".locals");
    WriteMetadataToken(module, body.LocalVarSigToken(), body.LocalVarSigToken(),
        /*spaceBefore=*/true);
    if (body.InitLocals()) output_.Write(" init");
    auto blob = module.GetStandaloneSignatureBlob(body.LocalVarSigToken());
    std::vector<Metadata::SignatureTypeWriter> signature;
    // The C# `signatureDecoder` field: assigned at the top of Disassemble
    // and alive through the whole render. The port heap-allocates the
    // provider for the same reason -- the local-type deferred writers run
    // in the loop BELOW, past the decode's try scope, so a stack local
    // scoped to the try block would dangle (the provider-outlives-writers
    // contract; the writers die with the signature vector at function
    // exit, so the unique_ptr suffices).
    auto provider = std::make_unique<Disassembler::DisassemblerSignatureTypeProvider>(
        module, output_);
    if (blob) {
        // The C# `blob.GetKind() == StandaloneSignatureKind.LocalVariables`
        // else the " /* wrong signature kind */" comment; the decode's
        // BadImageFormatException catch degrades to a message comment.
        if (((*blob)[0] & 0x0F) == 0x07) {
            try {
                Metadata::SignatureTypeProviderDecoder decoder(*provider, module);
                signature = decoder.DecodeLocalSignature(blob->data(), blob->size(),
                    Metadata::MetadataGenericContext::ForMethod(methodToken, module));
            } catch (const std::logic_error& ex) {
                output_.Write(std::string(" /* ") + ex.what() + " */");
            }
        } else {
            output_.Write(" /* wrong signature kind */");
        }
    }
    output_.Write(' ');
    Output::WriteLine(output_, "(");
    output_.Indent();
    for (std::size_t index = 0; index < signature.size(); index++) {
        output_.WriteLocalReference("[" + std::to_string(index) + "]",
            reinterpret_cast<const void*>(static_cast<std::uintptr_t>(index)),
            /*isDefinition=*/true);
        output_.Write(' ');
        signature[index](Disassembler::ILNameSyntax::TypeName);
        // The C# DebugInfo.TryGetName name suffix defers with the provider.
        if (index + 1 < signature.size()) output_.Write(',');
        output_.WriteLine();
    }
    output_.Unindent();
    Output::WriteLine(output_, ")");
}

// ---------------------------------------------------------------------------
// Disassemble (MethodBodyDisassembler.cs lines 111-157) -- the flat and
// structured paths.
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::Disassemble(Metadata::MetadataFile& module,
    std::uint32_t methodToken)
{
    std::uint32_t rva = module.GetMethodRVA(methodToken);
    char buf[64];
    // The C# `output.WriteLine("// Method begins at RVA 0x{0:x4}", rva)`.
    std::snprintf(buf, sizeof(buf), "// Method begins at RVA 0x%x", rva);
    Output::WriteLine(output_, buf);
    if (rva == 0) {
        Output::WriteLine(output_, "// Header size: 0");
        Output::WriteLine(output_, "// Code size: 0 (0x0)");
        Output::WriteLine(output_, ".maxstack 0");
        output_.WriteLine();
        return;
    }
    auto body = module.GetMethodBody(rva);
    if (!body.IsValid()) {
        // The C# `catch (BadImageFormatException ex) { output.WriteLine("// {0}",
        // ex.Message); }` -- the port's graceful reader cannot recover the
        // exception text, so the comment names the outcome.
        Output::WriteLine(output_, "// Invalid method body");
        return;
    }
    std::uint32_t codeSize = body.CodeSize();
    std::snprintf(buf, sizeof(buf), "// Header size: %u", body.HeaderSize());
    Output::WriteLine(output_, buf);
    std::snprintf(buf, sizeof(buf), "// Code size: %u (0x%x)", codeSize, codeSize);
    Output::WriteLine(output_, buf);
    std::snprintf(buf, sizeof(buf), ".maxstack %u", body.MaxStack());
    Output::WriteLine(output_, buf);

    if (methodToken == module.GetEntryPointToken()) {
        Output::WriteLine(output_, ".entrypoint");
    }

    DisassembleLocalsBlock(module, methodToken, body);
    output_.WriteLine();

    // The C# sequence-point assignment defers with the DebugInfo provider.
    auto il = body.IL();
    if (DetectControlStructure && !il.empty()) {
        // The C# structured branch: mark the branch/switch targets, then
        // render the body through the ILStructure tree -- the exception
        // handlers become .try/catch/finally blocks instead of the flat
        // trailing clauses. The C# passes RelativeVirtualAddress +
        // headerSize as the structured method RVA (the flat path passes the
        // bare RelativeVirtualAddress).
        Util::BitSet branchTargets(static_cast<int>(il.size()));
        std::size_t pos = 0;
        SetBranchTargets(il.data(), il.size(), pos, branchTargets);
        pos = 0;
        auto genericContext = Metadata::MetadataGenericContext::ForMethod(
            methodToken, module);
        ILStructure structure(module, methodToken, genericContext, il,
            body.Handlers());
        WriteStructureBody(module, structure, branchTargets, il.data(), il.size(),
            pos, rva + body.HeaderSize());
    } else {
        std::size_t pos = 0;
        while (pos < il.size()) {
            WriteInstruction(module, methodToken, il.data(), il.size(), pos, rva);
        }
        WriteExceptionHandlers(module, methodToken, body);
    }
    // The C# `sequencePoints = null`.
}

// ---------------------------------------------------------------------------
// WriteStructureHeader (MethodBodyDisassembler.cs lines 216-268).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::WriteStructureHeader(const ILStructure& s)
{
    switch (s.Type) {
        case ILStructureType::Loop: {
            output_.Write("// loop start");
            if (s.LoopEntryPointOffset >= 0) {
                output_.Write(" (head: ");
                Disassembler::WriteOffsetReference(output_, s.LoopEntryPointOffset);
                output_.Write(')');
            }
            output_.WriteLine();
            break;
        }
        case ILStructureType::Try:
            Output::WriteLine(output_, ".try");
            Output::WriteLine(output_, "{");
            break;
        case ILStructureType::Handler: {
            switch (s.ExceptionHandler.Kind) {
                case Metadata::ExceptionHandlerKind::Filter:
                    // handler block of filter block has no header
                    break;
                case Metadata::ExceptionHandlerKind::Catch:
                    output_.Write("catch");
                    // The C# `!s.ExceptionHandler.CatchType.IsNil`: the zero
                    // catch token stands in for the nil handle.
                    if (s.ExceptionHandler.ClassTokenOrFilterOffset != 0) {
                        output_.Write(' ');
                        IL::WriteTo(*s.Module, output_, s.GenericContext,
                            s.ExceptionHandler.ClassTokenOrFilterOffset,
                            ILNameSyntax::TypeName);
                    }
                    output_.WriteLine();
                    break;
                case Metadata::ExceptionHandlerKind::Finally:
                    Output::WriteLine(output_, "finally");
                    break;
                case Metadata::ExceptionHandlerKind::Fault:
                    Output::WriteLine(output_, "fault");
                    break;
                default:
                    // The C# `throw new ArgumentOutOfRangeException()`.
                    throw std::out_of_range(
                        "MethodBodyDisassembler: unknown ExceptionHandlerKind");
            }
            Output::WriteLine(output_, "{");
            break;
        }
        case ILStructureType::Filter:
            Output::WriteLine(output_, "filter");
            Output::WriteLine(output_, "{");
            break;
        default:
            // The C# `throw new ArgumentOutOfRangeException()`.
            throw std::out_of_range("MethodBodyDisassembler: unknown ILStructureType");
    }
    output_.Indent();
}

// ---------------------------------------------------------------------------
// WriteStructureBody (MethodBodyDisassembler.cs lines 270-303).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::WriteStructureBody(Metadata::MetadataFile& module,
    const ILStructure& s, const Util::BitSet& branchTargets,
    const std::uint8_t* base, std::size_t size, std::size_t& pos,
    std::uint32_t methodRva)
{
    bool isFirstInstructionInStructure = true;
    bool prevInstructionWasBranch = false;
    std::size_t childIndex = 0;
    while (pos < size && pos < static_cast<std::size_t>(s.EndOffset)) {
        int offset = static_cast<int>(pos);
        if (childIndex < s.Children.size()
            && s.Children[childIndex]->StartOffset <= offset
            && offset < s.Children[childIndex]->EndOffset) {
            const ILStructure& child = *s.Children[childIndex++];
            WriteStructureHeader(child);
            WriteStructureBody(module, child, branchTargets, base, size, pos,
                methodRva);
            WriteStructureFooter(child);
        } else {
            // put an empty line after branches, and in front of branch targets
            if (!isFirstInstructionInStructure
                && (prevInstructionWasBranch || branchTargets[offset])) {
                output_.WriteLine();
            }
            Metadata::ILOpCode currentOpCode = Metadata::DecodeOpCode(base, size, pos);
            pos = static_cast<std::size_t>(offset); // reset IL stream
            WriteInstruction(module, s.MethodHandle, base, size, pos, methodRva);
            prevInstructionWasBranch = Metadata::IsBranch(currentOpCode)
                || IsReturn(currentOpCode)
                || currentOpCode == Metadata::ILOpCode::Throw
                || currentOpCode == Metadata::ILOpCode::Rethrow
                || currentOpCode == Metadata::ILOpCode::Switch;
        }
        isFirstInstructionInStructure = false;
    }
}

// ---------------------------------------------------------------------------
// WriteStructureFooter (MethodBodyDisassembler.cs lines 305-325).
// ---------------------------------------------------------------------------
void MethodBodyDisassembler::WriteStructureFooter(const ILStructure& s)
{
    output_.Unindent();
    switch (s.Type) {
        case ILStructureType::Loop:
            Output::WriteLine(output_, "// end loop");
            break;
        case ILStructureType::Try:
            Output::WriteLine(output_, "} // end .try");
            break;
        case ILStructureType::Handler:
            Output::WriteLine(output_, "} // end handler");
            break;
        case ILStructureType::Filter:
            Output::WriteLine(output_, "} // end filter");
            break;
        default:
            // The C# `throw new ArgumentOutOfRangeException()`.
            throw std::out_of_range("MethodBodyDisassembler: unknown ILStructureType");
    }
}

}  // namespace ILSpy::Decompiler::Disassembler
