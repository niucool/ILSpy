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

#include "Decompiler/Disassembler/ILParser.hpp"
#include "Decompiler/Disassembler/OpCodeInfo.hpp"
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <cstdio>
#include <string>

namespace ILSpy::Decompiler::Disassembler {

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
    // minimum width, so the two-byte opcodes render all 4 hex digits.
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X %02X", static_cast<unsigned>(offset),
        static_cast<unsigned>(opCode));
    output_.Write(buf);
    int appendSpaces = static_cast<unsigned>(opCode) > 0xFF ? 14 : 16;
    while (pos < tmp) {
        char byteBuf[4];
        std::snprintf(byteBuf, sizeof(byteBuf), "%02X", static_cast<unsigned>(base[pos++]));
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
    std::uint32_t entityToken, bool spaceBefore)
{
    ReflectionDisassembler::WriteMetadataToken(output_, module, entityToken,
        /*spaceAfter=*/false, spaceBefore, ShowMetadataTokens, ShowMetadataTokensInBase10);
}

}  // namespace ILSpy::Decompiler::Disassembler
