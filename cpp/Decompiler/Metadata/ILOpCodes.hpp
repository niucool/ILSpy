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

// Port of ICSharpCode.Decompiler/Metadata/ILOpCodes.cs. The ILOpCode enum mirrors
// System.Reflection.Metadata.ILOpCode (single-byte 0x00..0xFE, two-byte 0xFE00+);
// the operand-type and display-name tables are ported verbatim from the C#.
// The tables are indexed by (((v & 0x200) >> 1) | (v & 0xff)) so a single 512-entry
// array covers both opcode forms.

#pragma once

#include "Decompiler/Metadata/OperandType.hpp"

#include <cstdint>
#include <string_view>

namespace ILSpy::Decompiler::Metadata {

// IL opcode values, matching System.Reflection.Metadata.ILOpCode. Two-byte
// opcodes are 0xFE00 + low byte.
enum class ILOpCode : std::uint16_t {
    Nop = 0x00, Break = 0x01,
    Ldarg_0 = 0x02, Ldarg_1 = 0x03, Ldarg_2 = 0x04, Ldarg_3 = 0x05,
    Ldloc_0 = 0x06, Ldloc_1 = 0x07, Ldloc_2 = 0x08, Ldloc_3 = 0x09,
    Stloc_0 = 0x0A, Stloc_1 = 0x0B, Stloc_2 = 0x0C, Stloc_3 = 0x0D,
    Ldarg_s = 0x0E, Ldarga_s = 0x0F, Starg_s = 0x10, Ldloc_s = 0x11, Ldloca_s = 0x12, Stloc_s = 0x13,
    Ldnull = 0x14, Ldc_i4_m1 = 0x15, Ldc_i4_0 = 0x16, Ldc_i4_1 = 0x17, Ldc_i4_2 = 0x18,
    Ldc_i4_3 = 0x19, Ldc_i4_4 = 0x1A, Ldc_i4_5 = 0x1B, Ldc_i4_6 = 0x1C, Ldc_i4_7 = 0x1D,
    Ldc_i4_8 = 0x1E, Ldc_i4_s = 0x1F, Ldc_i4 = 0x20, Ldc_i8 = 0x21, Ldc_r4 = 0x22, Ldc_r8 = 0x23,
    Dup = 0x25, Pop = 0x26,
    Jmp = 0x27, Call = 0x28, Calli = 0x29, Ret = 0x2A,
    Br_s = 0x2B, Brfalse_s = 0x2C, Brtrue_s = 0x2D, Beq_s = 0x2E, Bge_s = 0x2F, Bgt_s = 0x30,
    Ble_s = 0x31, Blt_s = 0x32, Bne_un_s = 0x33, Bge_un_s = 0x34, Bgt_un_s = 0x35, Ble_un_s = 0x36, Blt_un_s = 0x37,
    Br = 0x38, Brfalse = 0x39, Brtrue = 0x3A, Beq = 0x3B, Bge = 0x3C, Bgt = 0x3D, Ble = 0x3E, Blt = 0x3F,
    Bne_un = 0x40, Bge_un = 0x41, Bgt_un = 0x42, Ble_un = 0x43, Blt_un = 0x44,
    Switch = 0x45,
    Ldind_i1 = 0x46, Ldind_u1 = 0x47, Ldind_i2 = 0x48, Ldind_u2 = 0x49, Ldind_i4 = 0x4A, Ldind_u4 = 0x4B,
    Ldind_i8 = 0x4C, Ldind_i = 0x4D, Ldind_r4 = 0x4E, Ldind_r8 = 0x4F, Ldind_ref = 0x50,
    Stind_ref = 0x51, Stind_i1 = 0x52, Stind_i2 = 0x53, Stind_i4 = 0x54, Stind_i8 = 0x55, Stind_r4 = 0x56, Stind_r8 = 0x57,
    Add = 0x58, Sub = 0x59, Mul = 0x5A, Div = 0x5B, Div_un = 0x5C, Rem = 0x5D, Rem_un = 0x5E,
    And = 0x5F, Or = 0x60, Xor = 0x61, Shl = 0x62, Shr = 0x63, Shr_un = 0x64, Neg = 0x65, Not = 0x66,
    Conv_i1 = 0x67, Conv_i2 = 0x68, Conv_i4 = 0x69, Conv_i8 = 0x6A, Conv_r4 = 0x6B, Conv_r8 = 0x6C, Conv_u4 = 0x6D, Conv_u8 = 0x6E,
    Callvirt = 0x6F, Cpobj = 0x70, Ldobj = 0x71, Ldstr = 0x72, Newobj = 0x73, Castclass = 0x74, Isinst = 0x75, Conv_r_un = 0x76,
    Unbox = 0x79, Throw = 0x7A,
    Ldfld = 0x7B, Ldflda = 0x7C, Stfld = 0x7D, Ldsfld = 0x7E, Ldsflda = 0x7F, Stsfld = 0x80, Stobj = 0x81,
    Conv_ovf_i1_un = 0x82, Conv_ovf_i2_un = 0x83, Conv_ovf_i4_un = 0x84, Conv_ovf_i8_un = 0x85,
    Conv_ovf_u1_un = 0x86, Conv_ovf_u2_un = 0x87, Conv_ovf_u4_un = 0x88, Conv_ovf_u8_un = 0x89,
    Conv_ovf_i_un = 0x8A, Conv_ovf_u_un = 0x8B,
    Box = 0x8C, Newarr = 0x8D, Ldlen = 0x8E, Ldelema = 0x8F,
    Ldelem_i1 = 0x90, Ldelem_u1 = 0x91, Ldelem_i2 = 0x92, Ldelem_u2 = 0x93, Ldelem_i4 = 0x94, Ldelem_u4 = 0x95,
    Ldelem_i8 = 0x96, Ldelem_i = 0x97, Ldelem_r4 = 0x98, Ldelem_r8 = 0x99, Ldelem_ref = 0x9A,
    Stelem_i = 0x9B, Stelem_i1 = 0x9C, Stelem_i2 = 0x9D, Stelem_i4 = 0x9E, Stelem_i8 = 0x9F,
    Stelem_r4 = 0xA0, Stelem_r8 = 0xA1, Stelem_ref = 0xA2, Ldelem = 0xA3, Stelem = 0xA4, Unbox_any = 0xA5,
    Conv_ovf_i1 = 0xB3, Conv_ovf_u1 = 0xB4, Conv_ovf_i2 = 0xB5, Conv_ovf_u2 = 0xB6,
    Conv_ovf_i4 = 0xB7, Conv_ovf_u4 = 0xB8, Conv_ovf_i8 = 0xB9, Conv_ovf_u8 = 0xBA,
    Refanyval = 0xC2, Ckfinite = 0xC3, Mkrefany = 0xC6,
    Ldtoken = 0xD0, Conv_u2 = 0xD1, Conv_u1 = 0xD2, Conv_i = 0xD3, Conv_ovf_i = 0xD4, Conv_ovf_u = 0xD5,
    Add_ovf = 0xD6, Add_ovf_un = 0xD7, Mul_ovf = 0xD8, Mul_ovf_un = 0xD9, Sub_ovf = 0xDA, Sub_ovf_un = 0xDB,
    Endfinally = 0xDC, Leave = 0xDD, Leave_s = 0xDE, Stind_i = 0xDF, Conv_u = 0xE0,
    // Two-byte opcodes (0xFE00 + low byte).
    Arglist = 0xFE00, Ceq = 0xFE01, Cgt = 0xFE02, Cgt_un = 0xFE03, Clt = 0xFE04, Clt_un = 0xFE05,
    Ldftn = 0xFE06, Ldvirtftn = 0xFE07,
    Ldarg = 0xFE09, Ldarga = 0xFE0A, Starg = 0xFE0B, Ldloc = 0xFE0C, Ldloca = 0xFE0D, Stloc = 0xFE0E, Localloc = 0xFE0F,
    Endfilter = 0xFE11, Unaligned = 0xFE12, Volatile = 0xFE13, Tail = 0xFE14, Initobj = 0xFE15,
    Constrained = 0xFE16, Cpblk = 0xFE17, Initblk = 0xFE18,
    Rethrow = 0xFE1A, Sizeof = 0xFE1C, Refanytype = 0xFE1D, Readonly = 0xFE1E,
};

// Decode a one- or two-byte opcode from an IL byte stream. Advances `pos` past
// the opcode bytes and returns the ILOpCode, or ILOpCode::Nop with pos unchanged
// at end-of-stream. (Nop is 0x00, so a trailing 0x00 is ambiguous; the caller
// checks remaining bytes.)
ILOpCode DecodeOpCode(const std::uint8_t* base, std::size_t size, std::size_t& pos);

// Operand type for an opcode (from the ported table), or Undefined for an
// unknown opcode.
OperandType GetOperandType(ILOpCode opCode);

// Display name (e.g. "ldarg.0", "ret"), or "" for an unknown opcode.
std::string_view GetDisplayName(ILOpCode opCode);

// True if the opcode is a defined entry in the table.
bool IsDefined(ILOpCode opCode);

// True for branch opcodes (BrTarget/ShortBrTarget/Switch operands).
bool IsBranch(ILOpCode opCode);
bool IsConditionalBranch(ILOpCode opCode);

} // namespace ILSpy::Decompiler::Metadata
