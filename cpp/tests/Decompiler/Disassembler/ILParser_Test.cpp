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

// Tests for `ILParser` (cpp/Decompiler/Disassembler/ILParser.hpp -- the port of
// ICSharpCode.Decompiler/Disassembler/ILParser.cs): the method-body header
// size, the branch/switch operand decoders, the operand skipper, the variable
// index decoders, and the SetBranchTargets pre-pass that MethodBodyDisassembler
// uses to blank lines in front of branch targets. The reader is the C#
// `ref BlobReader` flattened to (base, size, pos) -- pos mutates in place, the
// way Metadata::DecodeOpCode already models it.

#include "Decompiler/Disassembler/ILParser.hpp"
#include <cstdlib>
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/ILDisassembler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Util/BitSet.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::Disassembler;
namespace MD = ILSpy::Decompiler::Metadata;
namespace Util = ILSpy::Decompiler::Util;

namespace {

const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

std::uint32_t FindType(MD::MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// GetHeaderSize (ILParser.cs lines 162-175): tiny (flags 0x2) -> 1; fat
// (flags 0x3) -> Size field (bits 12-15 of the 16-bit LE header word) * 4.
// ---------------------------------------------------------------------------

TEST(ILParserTest, GetHeaderSizeTinyIsOne) {
    const std::uint8_t body[] = {0x16};  // (5 << 2) | 0x02
    EXPECT_EQ(GetHeaderSize(body, sizeof(body)), 1);
}

TEST(ILParserTest, GetHeaderSizeFatScalesByFour) {
    const std::uint8_t sizeOne[] = {0x03, 0x10};  // Flags 0x003 | Size 1 << 12
    EXPECT_EQ(GetHeaderSize(sizeOne, sizeof(sizeOne)), 4);
    const std::uint8_t sizeThree[] = {0x03, 0x30};  // Flags 0x003 | Size 3 << 12
    EXPECT_EQ(GetHeaderSize(sizeThree, sizeof(sizeThree)), 12);
}

// ---------------------------------------------------------------------------
// DecodeOpCode -- the ILParser entry is shared with the ported
// Metadata::DecodeOpCode; pin the two-byte 0xFE prefix shape here.
// ---------------------------------------------------------------------------

TEST(ILParserTest, DecodeOpCodeFePrefixReadsSecondByte) {
    const std::uint8_t body[] = {0xFE, 0x06};  // ldftn
    std::size_t pos = 0;
    EXPECT_EQ(MD::DecodeOpCode(body, sizeof(body), pos), MD::ILOpCode::Ldftn);
    EXPECT_EQ(pos, std::size_t{2});
}

// ---------------------------------------------------------------------------
// DecodeBranchTarget (ILParser.cs lines 105-121): signed offset relative to
// the position AFTER the operand; a truncated operand yields int.MinValue.
// ---------------------------------------------------------------------------

TEST(ILParserTest, DecodeBranchTargetShortForwardAndBackward) {
    const std::uint8_t body[] = {0x2B, 0x05};  // br.s +5
    std::size_t pos = 1;  // the cursor sits after DecodeOpCode
    EXPECT_EQ(DecodeBranchTarget(body, sizeof(body), pos, MD::ILOpCode::Br_s), 7);
    EXPECT_EQ(pos, std::size_t{2});

    const std::uint8_t back[] = {0x2B, 0xF6};  // br.s -10
    pos = 1;
    EXPECT_EQ(DecodeBranchTarget(back, sizeof(back), pos, MD::ILOpCode::Br_s), -8);
    EXPECT_EQ(pos, std::size_t{2});
}

TEST(ILParserTest, DecodeBranchTargetLong) {
    const std::uint8_t body[] = {0x38, 0x10, 0x00, 0x00, 0x00};  // br +0x10
    std::size_t pos = 1;  // after the one-byte opcode
    EXPECT_EQ(DecodeBranchTarget(body, sizeof(body), pos, MD::ILOpCode::Br), 21);
    EXPECT_EQ(pos, std::size_t{5});
}

TEST(ILParserTest, DecodeBranchTargetTruncatedOperandIsIntMin) {
    const std::uint8_t body[] = {0x2B};  // br.s without its operand byte
    std::size_t pos = 1;  // after the opcode
    EXPECT_EQ(DecodeBranchTarget(body, sizeof(body), pos, MD::ILOpCode::Br_s),
        std::numeric_limits<int>::min());
    EXPECT_EQ(pos, std::size_t{1});
}

// ---------------------------------------------------------------------------
// DecodeSwitchTargets (ILParser.cs lines 123-149): n dwords of deltas
// relative to the position after the whole operand; a short stream yields an
// empty list; an oversized count clamps to the available dwords and skips to
// the end.
// ---------------------------------------------------------------------------

TEST(ILParserTest, DecodeSwitchTargetsDecodesDeltas) {
    // switch (n=2); deltas -8 and -4; operand ends at offset 12.
    const std::uint8_t body[] = {0x02, 0x00, 0x00, 0x00,
        0xF8, 0xFF, 0xFF, 0xFF, 0xFC, 0xFF, 0xFF, 0xFF};
    std::size_t pos = 0;
    auto targets = DecodeSwitchTargets(body, sizeof(body), pos);
    ASSERT_EQ(targets.size(), std::size_t{2});
    EXPECT_EQ(targets[0], 4);
    EXPECT_EQ(targets[1], 8);
    EXPECT_EQ(pos, std::size_t{12});
}

TEST(ILParserTest, DecodeSwitchTargetsShortStreamIsEmptyAndConsumesAll) {
    const std::uint8_t body[] = {0x01, 0x02};  // fewer than the 4-byte count
    std::size_t pos = 0;
    auto targets = DecodeSwitchTargets(body, sizeof(body), pos);
    EXPECT_TRUE(targets.empty());
    EXPECT_EQ(pos, std::size_t{2});
}

TEST(ILParserTest, DecodeSwitchTargetsOverflowingCountClampsToAvailable) {
    // count = 0xFFFFFFFF but only one target dword follows: clamp to 1, the
    // delta is relative to 8 (count dword + 1 target dword), and the cursor
    // ends at the stream end.
    const std::uint8_t body[] = {0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00};
    std::size_t pos = 0;
    auto targets = DecodeSwitchTargets(body, sizeof(body), pos);
    ASSERT_EQ(targets.size(), std::size_t{1});
    EXPECT_EQ(targets[0], 8);
    EXPECT_EQ(pos, std::size_t{8});
}

// ---------------------------------------------------------------------------
// SkipOperand (ILParser.cs lines 78-103): fixed-size operands skip their
// OperandSize; switch skips 4 + 4n; a truncated operand (any kind) skips to
// the stream end.
// ---------------------------------------------------------------------------

TEST(ILParserTest, SkipOperandFixedSizes) {
    const std::uint8_t body[] = {0x20, 0x01, 0x02, 0x03, 0x04};  // ldc.i4 <i32>
    std::size_t pos = 1;  // the cursor sits after DecodeOpCode
    SkipOperand(body, sizeof(body), pos, MD::ILOpCode::Ldc_i4);
    EXPECT_EQ(pos, std::size_t{5});
}

TEST(ILParserTest, SkipOperandSwitchSkipsCountAndTargets) {
    const std::uint8_t body[] = {0x45, 0x03, 0x00, 0x00, 0x00,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};  // switch; n=3 + 3 dwords
    std::size_t pos = 1;  // after the one-byte opcode
    SkipOperand(body, sizeof(body), pos, MD::ILOpCode::Switch);
    EXPECT_EQ(pos, std::size_t{17});
}

TEST(ILParserTest, SkipOperandTruncatedSwitchSkipsToEnd) {
    const std::uint8_t body[] = {0x45, 0x01};  // switch opcode then 1 byte only
    std::size_t pos = 1;  // after the opcode
    SkipOperand(body, sizeof(body), pos, MD::ILOpCode::Switch);
    EXPECT_EQ(pos, std::size_t{2});
}

// ---------------------------------------------------------------------------
// DecodeIndex (ILParser.cs lines 155-167) + IsReturn (lines 169-173).
// ---------------------------------------------------------------------------

TEST(ILParserTest, DecodeIndexReadsByteOrWord) {
    const std::uint8_t shortIdx[] = {0x07};
    std::size_t pos = 0;
    EXPECT_EQ(DecodeIndex(shortIdx, sizeof(shortIdx), pos, MD::ILOpCode::Ldloc_s), 7);
    EXPECT_EQ(pos, std::size_t{1});

    const std::uint8_t longIdx[] = {0x34, 0x12};
    pos = 0;
    EXPECT_EQ(DecodeIndex(longIdx, sizeof(longIdx), pos, MD::ILOpCode::Ldloc), 0x1234);
    EXPECT_EQ(pos, std::size_t{2});
}

TEST(ILParserTest, DecodeIndexRejectsNonVariableOpcodes) {
    const std::uint8_t body[] = {0x00};
    std::size_t pos = 0;
    EXPECT_THROW((void)DecodeIndex(body, sizeof(body), pos, MD::ILOpCode::Nop),
        std::invalid_argument);
}

TEST(ILParserTest, IsReturnCoversRetEndfilterEndfinally) {
    EXPECT_TRUE(IsReturn(MD::ILOpCode::Ret));
    EXPECT_TRUE(IsReturn(MD::ILOpCode::Endfilter));
    EXPECT_TRUE(IsReturn(MD::ILOpCode::Endfinally));
    EXPECT_FALSE(IsReturn(MD::ILOpCode::Nop));
    EXPECT_FALSE(IsReturn(MD::ILOpCode::Br_s));
}

// ---------------------------------------------------------------------------
// SetBranchTargets (ILParser.cs lines 177-201): marks every in-range branch
// and switch target, walks the whole stream.
// ---------------------------------------------------------------------------

TEST(ILParserTest, SetBranchTargetsMarksShortBranchTarget) {
    const std::uint8_t body[] = {0x2B, 0x02, 0x00, 0x00, 0x2A};  // br.s +2; nop; nop; ret
    Util::BitSet branchTargets(static_cast<int>(sizeof(body)));
    std::size_t pos = 0;
    SetBranchTargets(body, sizeof(body), pos, branchTargets);
    EXPECT_EQ(pos, std::size_t{5});
    EXPECT_TRUE(branchTargets[4]);
    EXPECT_FALSE(branchTargets[0]);
}

TEST(ILParserTest, SetBranchTargetsMarksSwitchTargets) {
    // switch (n=1) delta 0 -> target 9 (after the opcode + count + 1 dword);
    // one extra byte keeps the target in range; stream length 10.
    const std::uint8_t body[] = {0x45, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x2A};
    Util::BitSet branchTargets(static_cast<int>(sizeof(body)));
    std::size_t pos = 0;
    SetBranchTargets(body, sizeof(body), pos, branchTargets);
    EXPECT_EQ(pos, std::size_t{10});
    EXPECT_TRUE(branchTargets[9]);
}

// ---------------------------------------------------------------------------
// Real-fixture smoke: SetBranchTargets must walk real mscorlib method bodies
// cleanly (cursor ends exactly at the IL end) and DecodeUserString resolves
// the ldstr operand of some method to a non-empty string.
// ---------------------------------------------------------------------------

TEST(ILParserTest, SetBranchTargetsWalksRealBodiesCleanly) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int walked = 0;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto body = f.GetMethodBody(m.RVA);
            if (!body.IsValid() || body.IL().empty()) continue;
            auto il = body.IL();
            Util::BitSet branchTargets(static_cast<int>(il.size()));
            std::size_t pos = 0;
            SetBranchTargets(il.data(), il.size(), pos, branchTargets);
            EXPECT_EQ(pos, il.size()) << "unclean walk in " << t.Name << "::" << m.Name;
            for (int i = 0; i < static_cast<int>(il.size()); i++) {
                if (branchTargets[i]) {
                    ASSERT_GE(i, 0);
                    ASSERT_LT(i, static_cast<int>(il.size()));
                }
            }
            if (++walked >= 500) return;
        }
    }
    ASSERT_GT(walked, 0) << "no method bodies walked";
}

TEST(ILParserTest, DecodeUserStringResolvesLdstrOperand) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MD::MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto body = f.GetMethodBody(m.RVA);
            if (!body.IsValid() || body.IL().empty()) continue;
            auto il = body.IL();
            auto dis = MD::DisassembleIL(il);
            if (!dis.WalkedClean) continue;
            for (const auto& instr : dis.Instructions) {
                if (instr.OpCode != MD::ILOpCode::Ldstr) continue;
                std::size_t pos = instr.Offset + instr.Length - 4;
                std::string text =
                    DecodeUserString(il.data(), il.size(), pos, f);
                EXPECT_FALSE(text.empty()) << t.Name << "::" << m.Name;
                EXPECT_EQ(pos, std::size_t{instr.Offset + instr.Length});
                return;
            }
        }
    }
    FAIL() << "no ldstr instruction found in the walked methods";
}
