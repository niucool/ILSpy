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

// Port of ICSharpCode.Decompiler/Disassembler/MethodBodyDisassembler.cs (in
// progress -- the flat-path chain lands across iterations): the option flags
// and the opcode/token/raw-bytes writers the WriteInstruction operand switch
// and the Disassemble scaffolding consume. The C# private members WriteOpCode
// and WriteRVA and the WriteMetadataToken wrapper are the port's public
// members so the tests can drive them before Disassemble itself exists.
//
// The C# DebugInfo / sequence-point machinery (ShowSequencePoints +
// IDebugInfoProvider) is deferred with the provider type -- the flag exists,
// the sequence-point rendering comes with DebugInfo.

#pragma once

#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

#include <cstdint>

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
class MethodBody;
}

namespace ILSpy::Decompiler::Disassembler {

class MethodBodyDisassembler {
public:
    explicit MethodBodyDisassembler(Output::ITextOutput& output);

    // The C# `public bool DetectControlStructure { get; set; } = true`:
    // show .try/finally as blocks in IL code; indent loops.
    bool DetectControlStructure = true;

    // The C# `public bool ShowSequencePoints { get; set; }`.
    bool ShowSequencePoints = false;

    // The C# `public bool ShowMetadataTokens { get; set; }`: show metadata
    // tokens for instructions with token operands.
    bool ShowMetadataTokens = false;

    // The C# `public bool ShowMetadataTokensInBase10 { get; set; }`.
    bool ShowMetadataTokensInBase10 = false;

    // The C# `public bool ShowRawRVAOffsetAndBytes { get; set; }`: show the
    // raw RVA offset and bytes before each instruction.
    bool ShowRawRVAOffsetAndBytes = false;

    // The C# `private void WriteOpCode(ILOpCode opCode)`: the display-name
    // reference, with the ldarg.0-3 / ldloc.0-3 / stloc.0-3 shorthand forms
    // writing their suffix as a local reference (param_N / loc_N).
    void WriteOpCode(Metadata::ILOpCode opCode);

    // The C# `private void WriteRVA(BlobReader blob, int offset, ILOpCode
    // opCode)`: the ShowRawRVAOffsetAndBytes `/* 0xNNNNNNNN <opcode hex>
    // <operand bytes> */ ` prefix. The C# BlobReader is a value type, so the
    // read cursor is local to the comment (the caller's position is
    // untouched); a switch operand shows the count dword only -- the target
    // bytes live in the operand arm.
    void WriteRVA(const std::uint8_t* base, std::size_t size, std::size_t pos,
        std::uint32_t offset, Metadata::ILOpCode opCode);

    // The C# `private void WriteMetadataToken(Handle? handle, int
    // metadataToken, bool spaceBefore)` wrapper: calls the ReflectionDisassembler
    // static with spaceAfter=false and the ShowMetadataTokens flags. The
    // handle ports as the raw metadata token -- handleToken 0 is the C# null
    // (the comment prints even without ShowMetadataTokens in that error case,
    // and prints the metadataToken itself, which may be non-zero).
    void WriteMetadataToken(const Metadata::MetadataFile& module,
        std::uint32_t handleToken, std::uint32_t metadataToken, bool spaceBefore);

    // The C# `protected virtual void WriteInstruction(ITextOutput output,
    // MetadataFile metadataFile, MethodDefinitionHandle methodHandle, ref
    // BlobReader blob, int methodRva)` -- the full operand switch (branch
    // targets, the Field/Method/Sig/Type/Tok entity-token arms, the numeric
    // literal arms, the String arm, the switch-target list, and the
    // variable/short-variable arms), the `.emitbyte` fallback for an
    // undefined opcode or a truncated operand, and the terminating newline.
    // The C# output field ports as the constructor-bound member; the generic
    // context is the method context the C# Disassemble builds; the handle
    // ports as the raw method token. The reader is the (base, size, pos)
    // cursor -- pos mutates in place. Public so the tests can drive it
    // before Disassemble itself exists (the C# member is protected).
    void WriteInstruction(Metadata::MetadataFile& module, std::uint32_t methodToken,
        const std::uint8_t* base, std::size_t size, std::size_t& pos,
        std::uint32_t methodRva);

    // The C# `public virtual void Disassemble(MetadataFile module,
    // MethodDefinitionHandle handle)` -- the flat-path assembly: the RVA
    // header comments, the zero-RVA early-out, the .maxstack/.entrypoint
    // lines, the locals block, the flat instruction loop, and the exception
    // handlers. The handle ports as the raw method token. The C#
    // DetectControlStructure structured branch (the ILStructure-based
    // WriteStructureHeader/Body/Footer recursion) is not yet ported and
    // throws std::logic_error when the flag is set -- loud rather than wrong;
    // the flat path runs when the flag is false.
    void Disassemble(Metadata::MetadataFile& module, std::uint32_t methodToken);

    // The C# `void DisassembleLocalsBlock(MethodDefinitionHandle method,
    // MethodBodyBlock body)` (private): the `.locals <token> [init] (...)`
    // block over the body's local-variable signature, one `[_N] <type>`
    // line per local (the DebugInfo name suffix defers with the provider).
    // The method token scopes the signature's MVAR (!!N) names.
    void DisassembleLocalsBlock(const Metadata::MetadataFile& module,
        std::uint32_t methodToken, const Metadata::MethodBody& body);

    // The C# `internal void WriteExceptionHandlers(MetadataFile module,
    // MethodDefinitionHandle handle, MethodBodyBlock body)`: a blank line,
    // then one `eh.WriteTo(module, genericContext, output)` + newline per
    // clause. The method token scopes the generic context.
    void WriteExceptionHandlers(const Metadata::MetadataFile& module,
        std::uint32_t methodToken, const Metadata::MethodBody& body);

private:
    Output::ITextOutput& output_;
};

}  // namespace ILSpy::Decompiler::Disassembler
