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

// Port of ICSharpCode.Decompiler/Metadata/ILOpCodes.cs. The operand-type and
// display-name tables are extracted verbatim from the C# source (same length and order);
// both are indexed by (((v & 0x200) >> 1) | (v & 0xff)). The C# guards an out-of-range
// index with (OperandType)255 / ""; the port guards with Undefined / "".

#include "Decompiler/Metadata/ILOpCodes.hpp"

#include <cstddef>

namespace ILSpy::Decompiler::Metadata {

namespace {
constexpr OperandType kOperandTypes[287] = {
    OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::ShortVariable, OperandType::ShortVariable,
    OperandType::ShortVariable, OperandType::ShortVariable, OperandType::ShortVariable, OperandType::ShortVariable, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::ShortI,
    OperandType::I, OperandType::I8, OperandType::ShortR, OperandType::R, OperandType::Undefined, OperandType::None, OperandType::None, OperandType::Method, OperandType::Method, OperandType::Sig, OperandType::None, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget,
    OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::ShortBrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget,
    OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::BrTarget, OperandType::Switch, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None,
    OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None,
    OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::Method,
    OperandType::Type, OperandType::Type, OperandType::String, OperandType::Method, OperandType::Type, OperandType::Type, OperandType::None, OperandType::Undefined, OperandType::Undefined, OperandType::Type, OperandType::None, OperandType::Field, OperandType::Field, OperandType::Field, OperandType::Field, OperandType::Field,
    OperandType::Field, OperandType::Type, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::Type, OperandType::Type, OperandType::None, OperandType::Type,
    OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None,
    OperandType::None, OperandType::None, OperandType::None, OperandType::Type, OperandType::Type, OperandType::Type, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined,
    OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined,
    OperandType::Undefined, OperandType::Undefined, OperandType::Type, OperandType::None, OperandType::Undefined, OperandType::Undefined, OperandType::Type, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined,
    OperandType::Tok, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::BrTarget, OperandType::ShortBrTarget, OperandType::None,
    OperandType::None, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined,
    OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::Undefined, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None,
    OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::None, OperandType::Method, OperandType::Method, OperandType::Undefined, OperandType::Variable, OperandType::Variable, OperandType::Variable, OperandType::Variable, OperandType::Variable, OperandType::Variable, OperandType::None,
    OperandType::Undefined, OperandType::None, OperandType::ShortI, OperandType::None, OperandType::None, OperandType::Type, OperandType::Type, OperandType::None, OperandType::None, OperandType::Undefined, OperandType::None, OperandType::Undefined, OperandType::Type, OperandType::None, OperandType::None,
};

constexpr const char* kOperandNames[287] = {
    "nop", "break", "ldarg.0", "ldarg.1", "ldarg.2", "ldarg.3", "ldloc.0", "ldloc.1",
    "ldloc.2", "ldloc.3", "stloc.0", "stloc.1", "stloc.2", "stloc.3", "ldarg.s", "ldarga.s",
    "starg.s", "ldloc.s", "ldloca.s", "stloc.s", "ldnull", "ldc.i4.m1", "ldc.i4.0", "ldc.i4.1",
    "ldc.i4.2", "ldc.i4.3", "ldc.i4.4", "ldc.i4.5", "ldc.i4.6", "ldc.i4.7", "ldc.i4.8", "ldc.i4.s",
    "ldc.i4", "ldc.i8", "ldc.r4", "ldc.r8", "", "dup", "pop", "jmp",
    "call", "calli", "ret", "br.s", "brfalse.s", "brtrue.s", "beq.s", "bge.s",
    "bgt.s", "ble.s", "blt.s", "bne.un.s", "bge.un.s", "bgt.un.s", "ble.un.s", "blt.un.s",
    "br", "brfalse", "brtrue", "beq", "bge", "bgt", "ble", "blt",
    "bne.un", "bge.un", "bgt.un", "ble.un", "blt.un", "switch", "ldind.i1", "ldind.u1",
    "ldind.i2", "ldind.u2", "ldind.i4", "ldind.u4", "ldind.i8", "ldind.i", "ldind.r4", "ldind.r8",
    "ldind.ref", "stind.ref", "stind.i1", "stind.i2", "stind.i4", "stind.i8", "stind.r4", "stind.r8",
    "add", "sub", "mul", "div", "div.un", "rem", "rem.un", "and",
    "or", "xor", "shl", "shr", "shr.un", "neg", "not", "conv.i1",
    "conv.i2", "conv.i4", "conv.i8", "conv.r4", "conv.r8", "conv.u4", "conv.u8", "callvirt",
    "cpobj", "ldobj", "ldstr", "newobj", "castclass", "isinst", "conv.r.un", "",
    "", "unbox", "throw", "ldfld", "ldflda", "stfld", "ldsfld", "ldsflda",
    "stsfld", "stobj", "conv.ovf.i1.un", "conv.ovf.i2.un", "conv.ovf.i4.un", "conv.ovf.i8.un", "conv.ovf.u1.un", "conv.ovf.u2.un",
    "conv.ovf.u4.un", "conv.ovf.u8.un", "conv.ovf.i.un", "conv.ovf.u.un", "box", "newarr", "ldlen", "ldelema",
    "ldelem.i1", "ldelem.u1", "ldelem.i2", "ldelem.u2", "ldelem.i4", "ldelem.u4", "ldelem.i8", "ldelem.i",
    "ldelem.r4", "ldelem.r8", "ldelem.ref", "stelem.i", "stelem.i1", "stelem.i2", "stelem.i4", "stelem.i8",
    "stelem.r4", "stelem.r8", "stelem.ref", "ldelem", "stelem", "unbox.any", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "conv.ovf.i1", "conv.ovf.u1", "conv.ovf.i2", "conv.ovf.u2", "conv.ovf.i4",
    "conv.ovf.u4", "conv.ovf.i8", "conv.ovf.u8", "", "", "", "", "",
    "", "", "refanyval", "ckfinite", "", "", "mkrefany", "",
    "", "", "", "", "", "", "", "",
    "ldtoken", "conv.u2", "conv.u1", "conv.i", "conv.ovf.i", "conv.ovf.u", "add.ovf", "add.ovf.un",
    "mul.ovf", "mul.ovf.un", "sub.ovf", "sub.ovf.un", "endfinally", "leave", "leave.s", "stind.i",
    "conv.u", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "", "", "", "", "", "", "", "",
    "prefix7", "prefix6", "prefix5", "prefix4", "prefix3", "prefix2", "prefix1", "prefixref",
    "arglist", "ceq", "cgt", "cgt.un", "clt", "clt.un", "ldftn", "ldvirtftn",
    "", "ldarg", "ldarga", "starg", "ldloc", "ldloca", "stloc", "localloc",
    "", "endfilter", "unaligned.", "volatile.", "tail.", "initobj", "constrained.", "cpblk",
    "initblk", "", "rethrow", "", "sizeof", "refanytype", "readonly.",
};

inline std::uint16_t TableIndex(ILOpCode opCode) {
    auto v = static_cast<std::uint16_t>(opCode);
    return static_cast<std::uint16_t>(((v & 0x200) >> 1) | (v & 0xff));
}
} // namespace

ILOpCode DecodeOpCode(const std::uint8_t* base, std::size_t size, std::size_t& pos) {
    if (pos >= size) return ILOpCode::Nop;
    std::uint8_t b0 = base[pos++];
    if (b0 == 0xFE) {
        if (pos >= size) return ILOpCode::Nop;
        std::uint8_t b1 = base[pos++];
        return static_cast<ILOpCode>(0xFE00u + b1);
    }
    return static_cast<ILOpCode>(b0);
}

OperandType GetOperandType(ILOpCode opCode) {
    auto i = TableIndex(opCode);
    return i < 287 ? kOperandTypes[i] : OperandType::Undefined;
}

std::string_view GetDisplayName(ILOpCode opCode) {
    auto i = TableIndex(opCode);
    return i < 287 ? std::string_view(kOperandNames[i]) : std::string_view();
}

bool IsDefined(ILOpCode opCode) {
    return !GetDisplayName(opCode).empty();
}

bool IsBranch(ILOpCode opCode) {
    switch (GetOperandType(opCode)) {
        case OperandType::BrTarget:
        case OperandType::ShortBrTarget:
        case OperandType::Switch:
            return true;
        default:
            return false;
    }
}

bool IsConditionalBranch(ILOpCode opCode) {
    switch (opCode) {
        case ILOpCode::Brtrue_s: case ILOpCode::Brfalse_s:
        case ILOpCode::Brtrue: case ILOpCode::Brfalse:
        case ILOpCode::Beq_s: case ILOpCode::Bge_s: case ILOpCode::Bgt_s:
        case ILOpCode::Ble_s: case ILOpCode::Blt_s: case ILOpCode::Bne_un_s:
        case ILOpCode::Bge_un_s: case ILOpCode::Bgt_un_s: case ILOpCode::Ble_un_s: case ILOpCode::Blt_un_s:
        case ILOpCode::Beq: case ILOpCode::Bge: case ILOpCode::Bgt:
        case ILOpCode::Ble: case ILOpCode::Blt: case ILOpCode::Bne_un:
        case ILOpCode::Bge_un: case ILOpCode::Bgt_un: case ILOpCode::Ble_un: case ILOpCode::Blt_un:
            return true;
        default:
            return false;
    }
}

} // namespace ILSpy::Decompiler::Metadata
