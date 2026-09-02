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

// Tests for the MethodBodyDisassembler writers (cpp/Decompiler/Disassembler/
// MethodBodyDisassembler.{hpp,cpp} + the ReflectionDisassembler token static):
// the WriteOpCode display-name/local-reference spellings, the
// ShowRawRVAOffsetAndBytes `/* ... */ ` comment geometry, and the
// WriteMetadataToken comment/space matrix (show flags, base10, the null-token
// error path, the UserString non-entity path).

#include "Decompiler/Disassembler/MethodBodyDisassembler.hpp"
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <sstream>
#include <string>

using namespace ILSpy::Decompiler::Disassembler;
namespace MD = ILSpy::Decompiler::Metadata;
namespace OUT = ILSpy::Decompiler::Output;

namespace {

#if defined(_WIN32)
const char* MscorlibPath() { return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll"; }
#else
const char* MscorlibPath() { return "/usr/lib/mono/4.5/mscorlib.dll"; }
#endif

std::string Render(const std::function<void(OUT::ITextOutput&)>& body) {
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    body(output);
    return stream.str();
}

MethodBodyDisassembler MakeDisassembler(OUT::PlainTextOutput& output) {
    return MethodBodyDisassembler(output);
}

}  // namespace

// ---------------------------------------------------------------------------
// WriteOpCode (MethodBodyDisassembler.cs lines 569-590).
// ---------------------------------------------------------------------------

TEST(MethodBodyDisassemblerTest, WriteOpCodeRendersDisplayName) {
    EXPECT_EQ(Render([](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteOpCode(MD::ILOpCode::Call);
    }), "call");
    EXPECT_EQ(Render([](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteOpCode(MD::ILOpCode::Nop);
    }), "nop");
    EXPECT_EQ(Render([](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteOpCode(MD::ILOpCode::Brtrue_s);
    }), "brtrue.s");
}

TEST(MethodBodyDisassemblerTest, WriteOpCodeShorthandFormsWriteLocalRefs) {
    // The omitSuffix reference collapses "ldarg.0" to "ldarg." and the local
    // reference re-writes the "0" suffix -- PlainTextOutput renders both.
    EXPECT_EQ(Render([](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteOpCode(MD::ILOpCode::Ldarg_0);
    }), "ldarg.0");
    EXPECT_EQ(Render([](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteOpCode(MD::ILOpCode::Ldloc_1);
    }), "ldloc.1");
    EXPECT_EQ(Render([](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteOpCode(MD::ILOpCode::Stloc_3);
    }), "stloc.3");
}

// ---------------------------------------------------------------------------
// WriteRVA (MethodBodyDisassembler.cs lines 592-610).
// ---------------------------------------------------------------------------

TEST(MethodBodyDisassemblerTest, WriteRVACommentShowsOpcodeAndOperandBytes) {
    // ldc.i4 <0x01020304>: the cursor sits at the operand (after DecodeOpCode);
    // 1-byte opcode -> 16-char budget, 8 hex chars -> 8 trailing spaces.
    const std::uint8_t body[] = {0x20, 0x01, 0x02, 0x03, 0x04};
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.ShowRawRVAOffsetAndBytes = true;
        d.WriteRVA(body, sizeof(body), 1, 0x00102030u, MD::ILOpCode::Ldc_i4);
    }), "/* 0x00102030 2001020304         */ ");
}

TEST(MethodBodyDisassemblerTest, WriteRVACommentTwoByteOpcodeUses14CharBudget) {
    const std::uint8_t body[] = {0xFE, 0x06, 0x01, 0x02, 0x03, 0x04};  // ldftn
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.ShowRawRVAOffsetAndBytes = true;
        d.WriteRVA(body, sizeof(body), 2, 0x00000064u, MD::ILOpCode::Ldftn);
    }), "/* 0x00000064 FE0601020304       */ ");
}

TEST(MethodBodyDisassemblerTest, WriteRVACommentSwitchShowsCountOnly) {
    // The switch count dword is part of the comment; the target dwords live
    // in the operand arm.
    const std::uint8_t body[] = {0x45, 0x02, 0x00, 0x00, 0x00,
        0xAA, 0xBB, 0xCC, 0xDD};
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.ShowRawRVAOffsetAndBytes = true;
        d.WriteRVA(body, sizeof(body), 1, 0x00000001u, MD::ILOpCode::Switch);
    }), "/* 0x00000001 4502000000         */ ");
}

TEST(MethodBodyDisassemblerTest, WriteRVACommentWritesNothingWhenHidden) {
    const std::uint8_t body[] = {0x2B, 0x05};
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteRVA(body, sizeof(body), 1, 0u, MD::ILOpCode::Br_s);
    }), "");
}

// ---------------------------------------------------------------------------
// WriteMetadataToken (MethodBodyDisassembler.cs lines 628-639 + the
// ReflectionDisassembler internal static).
// ---------------------------------------------------------------------------

TEST(MethodBodyDisassemblerTest, WriteMetadataTokenWrapperMatrix) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // A valid entity token with the show flag off: nothing is written (the
    // wrapper always passes spaceAfter=false, so not even a space).
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteMetadataToken(f, 0x06000001u, /*spaceBefore=*/true);
    }), "");

    // With the show flag on: the comment with the leading space.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.ShowMetadataTokens = true;
        d.WriteMetadataToken(f, 0x06000001u, true);
    }), " /* 06000001 */");

    // Base10 formatting.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.ShowMetadataTokens = true;
        d.ShowMetadataTokensInBase10 = true;
        d.WriteMetadataToken(f, 0x06000001u, true);
    }), " /* 100663297 */");

    // The null-token error path prints the comment even without the flag.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteMetadataToken(f, 0u, true);
    }), " /* 00000000 */");
}

TEST(MethodBodyDisassemblerTest, WriteMetadataTokenStaticMatrix) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // Both spaces (the direct static's full shape).
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        ReflectionDisassembler::WriteMetadataToken(out, f, 0x01000001u,
            /*spaceAfter=*/true, /*spaceBefore=*/true, /*showMetadataTokens=*/true,
            /*base10=*/false);
    }), " /* 01000001 */ ");

    // A UserString token (0x70 -- not an entity handle) writes plainly.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        ReflectionDisassembler::WriteMetadataToken(out, f, 0x70000001u,
            false, true, true, false);
    }), " /* 70000001 */");

    // Without the flags and without a null handle: only the both-spaces case
    // writes a single space.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        ReflectionDisassembler::WriteMetadataToken(out, f, 0x01000001u,
            true, true, false, false);
    }), " ");
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        ReflectionDisassembler::WriteMetadataToken(out, f, 0x01000001u,
            false, true, false, false);
    }), "");
}
