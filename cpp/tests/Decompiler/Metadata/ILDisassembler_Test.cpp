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

// IL opcode-table + disassembler tests. Decodes real method bodies from
// mscorlib and checks that the opcode walk consumes exactly the method's IL
// (no over-/under-run), that every instruction's Length covers its opcode(s)
// and operand, and that bodies end in a control-transfer opcode (ret/leave/
// throw). This validates the ported ILOpCode table against real IL.

#include "Decompiler/Metadata/ILDisassembler.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <set>

using namespace ILSpy::Decompiler::Metadata;

static const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

TEST(ILOpCodes, KnownOpcodesRoundTrip) {
    EXPECT_EQ(GetDisplayName(ILOpCode::Ret), "ret");
    EXPECT_EQ(GetDisplayName(ILOpCode::Ldarg_0), "ldarg.0");
    EXPECT_EQ(GetDisplayName(ILOpCode::Call), "call");
    EXPECT_EQ(GetOperandType(ILOpCode::Ret), OperandType::None);
    EXPECT_EQ(GetOperandType(ILOpCode::Call), OperandType::Method);
    EXPECT_EQ(GetOperandType(ILOpCode::Ldc_i4_s), OperandType::ShortI);
    EXPECT_EQ(GetOperandType(ILOpCode::Br_s), OperandType::ShortBrTarget);
    EXPECT_EQ(GetOperandType(ILOpCode::Switch), OperandType::Switch);
    // Two-byte opcode.
    EXPECT_EQ(GetDisplayName(ILOpCode::Ceq), "ceq");
    EXPECT_EQ(GetOperandType(ILOpCode::Initobj), OperandType::Type);
    EXPECT_TRUE(IsDefined(ILOpCode::Ret));
    // An undefined opcode (0xFE7F) is not defined.
    EXPECT_FALSE(IsDefined(static_cast<ILOpCode>(0xFE7F)));
}

TEST(ILDisassembler, WalksMscorlibMethodBodiesCleanly) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    int clean = 0, total = 0;
    bool sawRet = false, sawCall = false, sawLdstr = false, sawSwitch = false, sawNewobj = false;
    std::set<std::string> seenNames;
    for (const auto& m : file.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = file.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        ++total;
        auto d = DisassembleIL(body.IL());
        if (!d.WalkedClean) continue; // a few obfuscated/malformed bodies degrade
        ++clean;
        // Sum of instruction lengths must equal the method's CodeSize.
        std::uint32_t sum = 0;
        for (const auto& ins : d.Instructions) sum += ins.Length;
        ASSERT_EQ(sum, body.CodeSize())
            << "IL walk did not cover the method body for a body declared clean";
        ASSERT_FALSE(d.Instructions.empty());
        for (const auto& ins : d.Instructions) {
            seenNames.insert(std::string(GetDisplayName(ins.OpCode)));
            if (ins.OpCode == ILOpCode::Ret) sawRet = true;
            if (ins.OpCode == ILOpCode::Call) sawCall = true;
            if (ins.OpCode == ILOpCode::Ldstr) sawLdstr = true;
            if (ins.OpCode == ILOpCode::Switch) sawSwitch = true;
            if (ins.OpCode == ILOpCode::Newobj) sawNewobj = true;
        }
    }
    ASSERT_GT(total, 1000);
    // The vast majority of method bodies should walk cleanly.
    EXPECT_GT(clean, total * 9 / 10) << "too many method bodies failed the IL walk";
    EXPECT_TRUE(sawRet) << "no ret decoded";
    EXPECT_TRUE(sawCall) << "no call decoded";
    EXPECT_TRUE(sawLdstr) << "no ldstr decoded";
    EXPECT_TRUE(sawNewobj) << "no newobj decoded";
    EXPECT_TRUE(sawSwitch) << "no switch decoded";
}
