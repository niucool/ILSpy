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

// Port of ICSharpCode.Decompiler/Disassembler/MethodBodyDisassembler.cs:
// the option flags, the DebugInfo provider member and the sequence-point
// state it drives, the opcode/token/raw-bytes writers, the WriteInstruction
// operand switch, and the Disassemble assembly with both body paths -- the
// flat instruction loop and the DetectControlStructure structured branch
// through the ILStructure tree (WriteStructureHeader/Body/Footer). The C#
// private members are the port's public members so the tests can drive them
// directly.
//
// The real PDB implementation of the provider (the portable-PDB debug tables
// behind the CLI's --il-sequence-points) is the Phase-8 PdbProvider; this
// class carries the consumer-side contract and rendering.

#pragma once

#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

#include <cstdint>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
class MetadataFile;
class MethodBody;
}

namespace ILSpy::Decompiler::Util {
class BitSet;
}

namespace ILSpy::Decompiler::Disassembler {

class ILStructure;

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

    // The C# `public IDebugInfoProvider DebugInfo { get; set; }`: the
    // caller-owned debug-info provider (null for none -- the C# default).
    // Drives the ShowSequencePoints `// sequence point:` lines and the
    // debug names appended to the .locals block.
    const DebugInfo::IDebugInfoProvider* DebugInfo = nullptr;

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
    // MethodDefinitionHandle handle)`: the RVA header comments, the zero-RVA
    // early-out, the .maxstack/.entrypoint lines, the locals block, and the
    // body -- through the ILStructure tree when DetectControlStructure is
    // set and the body is non-empty (the C# passes RelativeVirtualAddress +
    // headerSize as the structured method RVA), or the flat instruction loop
    // plus the trailing exception-handler clauses otherwise. The handle
    // ports as the raw method token.
    void Disassemble(Metadata::MetadataFile& module, std::uint32_t methodToken);

    // The C# `void DisassembleLocalsBlock(MethodDefinitionHandle method,
    // MethodBodyBlock body)` (private): the `.locals <token> [init] (...)`
    // block over the body's local-variable signature, one `[_N] <type>` line
    // per local with the DebugInfo debug-name suffix when the provider
    // answers TryGetName. The method token scopes the signature's MVAR (!!N)
    // names.
    void DisassembleLocalsBlock(const Metadata::MetadataFile& module,
        std::uint32_t methodToken, const Metadata::MethodBody& body);

    // The C# `internal void WriteExceptionHandlers(MetadataFile module,
    // MethodDefinitionHandle handle, MethodBodyBlock body)`: a blank line,
    // then one `eh.WriteTo(module, genericContext, output)` + newline per
    // clause. The method token scopes the generic context.
    void WriteExceptionHandlers(const Metadata::MetadataFile& module,
        std::uint32_t methodToken, const Metadata::MethodBody& body);

    // The C# `void WriteStructureHeader(ILStructure s)` (private): the
    // header lines for a structure, then Indent -- "// loop start" (with the
    // " (head: IL_xxxx)" entry-point part when one is recorded),
    // ".try"/"{" for a try block, "filter"/"{" for a filter block, and for
    // a handler block "catch [<type>]" (the catch type through the
    // structure's module and generic context at TypeName syntax; a nil
    // catch type writes bare "catch"), "finally", "fault", or nothing at
    // all for a filter's handler block. The Root structure is never a
    // written header (the C# ArgumentOutOfRangeException ports as
    // std::out_of_range). Public so the tests can drive synthetic structure
    // trees.
    void WriteStructureHeader(const ILStructure& s);

    // The C# `void WriteStructureBody(ILStructure s, BitSet branchTargets,
    // ref BlobReader body, int methodRva)` (private): the recursive structure
    // walk -- a child structure containing the next offset renders
    // header/body/footer and consumes the walk through its range, an
    // instruction renders with a blank line before it when the previous
    // instruction was a branch/return/throw/rethrow/switch or the offset is a
    // marked branch target (the C# blank-line comment), and the walk stops at
    // the structure's end offset or the stream end. The module is the
    // Disassemble caller's (the C# field); the instructions render at
    // s.MethodHandle.
    void WriteStructureBody(Metadata::MetadataFile& module, const ILStructure& s,
        const Util::BitSet& branchTargets, const std::uint8_t* base,
        std::size_t size, std::size_t& pos, std::uint32_t methodRva);

    // The C# `void WriteStructureFooter(ILStructure s)` (private): Unindent,
    // then the footer line -- "// end loop", "} // end .try",
    // "} // end handler", "} // end filter". Public so the tests can drive
    // synthetic structure trees.
    void WriteStructureFooter(const ILStructure& s);

private:
    Output::ITextOutput& output_;

    // The C# `IList<DebugInfo.SequencePoint> sequencePoints` and `int
    // nextSequencePointIndex` fields: assigned by Disassemble (the C#
    // DebugInfo?.GetSequencePoints(handle) ?? EmptyList pair -- the null
    // provider collapses to an empty list) and consumed one-per-instruction
    // by the WriteInstruction sequence-point render. The C# sets the field
    // back to null at the end of Disassemble; `hasSequencePoints_` carries
    // that null (WriteInstruction's `sequencePoints?.Count` null check -- a
    // directly-driven WriteInstruction renders no sequence points).
    std::vector<DebugInfo::SequencePoint> sequencePoints_;
    bool hasSequencePoints_ = false;
    int nextSequencePointIndex_ = 0;
};

}  // namespace ILSpy::Decompiler::Disassembler
