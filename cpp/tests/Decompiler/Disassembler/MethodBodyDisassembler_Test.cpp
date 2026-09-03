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
#include "Decompiler/Metadata/ILDisassembler.hpp"
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
        d.WriteMetadataToken(f, 0x06000001u, 0x06000001u, /*spaceBefore=*/true);
    }), "");

    // With the show flag on: the comment with the leading space.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.ShowMetadataTokens = true;
        d.WriteMetadataToken(f, 0x06000001u, 0x06000001u, true);
    }), " /* 06000001 */");

    // Base10 formatting.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.ShowMetadataTokens = true;
        d.ShowMetadataTokensInBase10 = true;
        d.WriteMetadataToken(f, 0x06000001u, 0x06000001u, true);
    }), " /* 100663297 */");

    // The null-handle error path prints the comment even without the flag --
    // and prints the metadataToken itself.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        MethodBodyDisassembler d(out);
        d.WriteMetadataToken(f, 0u, 0x70000001u, true);
    }), " /* 70000001 */");
}

TEST(MethodBodyDisassemblerTest, WriteMetadataTokenStaticMatrix) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());

    // Both spaces (the direct static's full shape).
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        ReflectionDisassembler::WriteMetadataToken(out, f, 0x01000001u, 0x01000001u,
            /*spaceAfter=*/true, /*spaceBefore=*/true, /*showMetadataTokens=*/true,
            /*base10=*/false);
    }), " /* 01000001 */ ");

    // A UserString token (0x70 -- not an entity handle) writes plainly.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        ReflectionDisassembler::WriteMetadataToken(out, f, 0x70000001u, 0x70000001u,
            false, true, true, false);
    }), " /* 70000001 */");

    // Without the flags and without a null handle: only the both-spaces case
    // writes a single space.
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        ReflectionDisassembler::WriteMetadataToken(out, f, 0x01000001u, 0x01000001u,
            true, true, false, false);
    }), " ");
    EXPECT_EQ(Render([&](OUT::ITextOutput& out) {
        ReflectionDisassembler::WriteMetadataToken(out, f, 0x01000001u, 0x01000001u,
            false, true, false, false);
    }), "");
}

// ---------------------------------------------------------------------------
// WriteInstruction (MethodBodyDisassembler.cs lines 327-567) -- synthetic
// streams over the real writers, plus a real-body smoke.
// ---------------------------------------------------------------------------

namespace {

std::string RenderInstruction(MD::MetadataFile& f, std::uint32_t methodToken,
    const std::uint8_t* base, std::size_t size, std::uint32_t rva = 0) {
    std::ostringstream stream;
    OUT::PlainTextOutput output(stream);
    MethodBodyDisassembler d(output);
    std::size_t pos = 0;
    d.WriteInstruction(f, methodToken, base, size, pos, rva);
    return stream.str();
}

}  // namespace

TEST(MethodBodyDisassemblerTest, WriteInstructionRet) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    const std::uint8_t body[] = {0x2A};
    EXPECT_EQ(RenderInstruction(f, 0x06000001u, body, sizeof(body)), "IL_0000: ret\r\n");
}

TEST(MethodBodyDisassemblerTest, WriteInstructionBranchTarget) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    const std::uint8_t body[] = {0x2B, 0x00};  // br.s +0
    EXPECT_EQ(RenderInstruction(f, 0x06000001u, body, sizeof(body)),
        "IL_0000: br.s IL_0002\r\n");
}

TEST(MethodBodyDisassemblerTest, WriteInstructionCallWithTokenArm) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = 0;
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == "System" && t.Name == "String") {
            stringType = t.Token;
            break;
        }
    }
    ASSERT_NE(stringType, 0u);
    std::uint32_t copy = 0;
    for (const auto& m : f.GetMethods(stringType)) {
        if (m.Name == "Copy") {
            copy = m.Token;
            break;
        }
    }
    ASSERT_NE(copy, 0u);
    const std::uint8_t body[] = {0x28,
        static_cast<std::uint8_t>(copy), static_cast<std::uint8_t>(copy >> 8),
        static_cast<std::uint8_t>(copy >> 16), static_cast<std::uint8_t>(copy >> 24)};
    EXPECT_EQ(RenderInstruction(f, 0x06000001u, body, sizeof(body)),
        "IL_0000: call string System.String::Copy(string)\r\n");
}

TEST(MethodBodyDisassemblerTest, WriteInstructionShortIntegerArm) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    const std::uint8_t body[] = {0x15};  // ldc.i4.m1
    EXPECT_EQ(RenderInstruction(f, 0x06000001u, body, sizeof(body)),
        "IL_0000: ldc.i4.m1\r\n");
    const std::uint8_t ldarg[] = {0x0E, 0x03};  // ldarg.s 3
    EXPECT_EQ(RenderInstruction(f, 0x06000001u, ldarg, sizeof(ldarg)),
        "IL_0000: ldarg.s 3\r\n");
}

TEST(MethodBodyDisassemblerTest, WriteInstructionLdstrWithRealToken) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // Take a real ldstr operand from a real mscorlib body and render it
    // against the same #US row.
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto mb = f.GetMethodBody(m.RVA);
            if (!mb.IsValid() || mb.IL().empty()) continue;
            auto il = mb.IL();
            auto dis = MD::DisassembleIL(il);
            if (!dis.WalkedClean) continue;
            for (const auto& instr : dis.Instructions) {
                if (instr.OpCode != MD::ILOpCode::Ldstr) continue;
                std::size_t operandPos = instr.Offset + instr.Length - 4;
                std::uint32_t token = static_cast<std::uint32_t>(il[operandPos])
                    | (static_cast<std::uint32_t>(il[operandPos + 1]) << 8)
                    | (static_cast<std::uint32_t>(il[operandPos + 2]) << 16)
                    | (static_cast<std::uint32_t>(il[operandPos + 3]) << 24);
                auto text = f.TryGetUserString(token);
                ASSERT_TRUE(text.has_value()) << std::hex << token;
                const std::uint8_t body[] = {0x72,
                    static_cast<std::uint8_t>(token), static_cast<std::uint8_t>(token >> 8),
                    static_cast<std::uint8_t>(token >> 16), static_cast<std::uint8_t>(token >> 24)};
                EXPECT_EQ(RenderInstruction(f, 0x06000001u, body, sizeof(body)),
                    "IL_0000: ldstr \"" + *text + "\"\r\n");
                return;
            }
        }
    }
    FAIL() << "no ldstr instruction found";
}

TEST(MethodBodyDisassemblerTest, WriteInstructionInvalidTokenRendersComment) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // A UserString token on the call arm is not an entity handle: the handle
    // is null, the comment always prints (the C# error-path comment), and the
    // WriteTo call is skipped.
    const std::uint8_t body[] = {0x28, 0x01, 0x00, 0x00, 0x70};
    EXPECT_EQ(RenderInstruction(f, 0x06000001u, body, sizeof(body)),
        "IL_0000: call  /* 70000001 */\r\n");
}

TEST(MethodBodyDisassemblerTest, WriteInstructionSwitchTargetList) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // switch(1 target, delta 0 -> target 9); one filler byte.
    const std::uint8_t body[] = {0x45, 0x01, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x2A};
    EXPECT_EQ(RenderInstruction(f, 0x06000001u, body, sizeof(body)),
        "IL_0000: switch (IL_0009)\r\n");
}

TEST(MethodBodyDisassemblerTest, WriteInstructionUndefinedOpcodeFallsBackToEmitbyte) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    const std::uint8_t body[] = {0x24};  // not a one-byte opcode
    EXPECT_EQ(RenderInstruction(f, 0x06000001u, body, sizeof(body)),
        "IL_0000: .emitbyte 0x24\r\n");
}

// ---------------------------------------------------------------------------
// Disassemble / DisassembleLocalsBlock / WriteExceptionHandlers
// (MethodBodyDisassembler.cs lines 111-157, 158-196, 198-212) -- the flat
// path over real mscorlib bodies.
// ---------------------------------------------------------------------------

namespace {

std::uint32_t FindTypeDefTokenIn(const MD::MetadataFile& f, std::string_view ns,
    std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

std::uint32_t FindMethodIn(const MD::MetadataFile& f, std::uint32_t typeToken,
    std::string_view name) {
    for (const auto& m : f.GetMethods(typeToken)) {
        if (m.Name == name) return m.Token;
    }
    return 0;
}

std::string DisassembleFlat(MD::MetadataFile& f,
    std::uint32_t token) {
    std::ostringstream stream;
    OUT::PlainTextOutput out(stream);
    MethodBodyDisassembler d(out);
    d.DetectControlStructure = false;
    d.Disassemble(f, token);
    return stream.str();
}

}  // namespace

TEST(MethodBodyDisassemblerTest, DisassembleZeroRvaEarlyOut) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // An abstract method (RVA 0) -- e.g. String's CompareTo overloads? Any
    // RVA-0 method shows the early-out block verbatim.
    std::uint32_t zeroRva = 0;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) {
                zeroRva = m.Token;
                break;
            }
        }
        if (zeroRva != 0) break;
    }
    ASSERT_NE(zeroRva, 0u);
    std::ostringstream stream;
    OUT::PlainTextOutput out(stream);
    MethodBodyDisassembler d(out);
    d.DetectControlStructure = false;
    d.Disassemble(f, zeroRva);
    EXPECT_EQ(stream.str(),
        "// Method begins at RVA 0x0\r\n"
        "// Header size: 0\r\n"
        "// Code size: 0 (0x0)\r\n"
        ".maxstack 0\r\n"
        "\r\n");
}

TEST(MethodBodyDisassemblerTest, DisassembleFlatRendersPassThroughMethod) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    std::uint32_t rva = f.GetMethodRVA(copy);
    ASSERT_NE(rva, 0u);
    // The .NET 4.8 String.Copy body is argument-checked (not a bare
    // pass-through), so the header block is built from the decoded body model
    // and the instruction spelling is pinned by the structural head/tail.
    auto body = f.GetMethodBody(rva);
    ASSERT_TRUE(body.IsValid());
    char buf[128];
    std::snprintf(buf, sizeof(buf), "// Method begins at RVA 0x%x\r\n// Header size: %u\r\n",
        rva, body.HeaderSize());
    std::string expected(buf);
    std::snprintf(buf, sizeof(buf), "// Code size: %u (0x%x)\r\n.maxstack %u\r\n",
        body.CodeSize(), body.CodeSize(), body.MaxStack());
    expected += buf;
    std::string text = DisassembleFlat(f, copy);
    EXPECT_TRUE(text.rfind(expected, 0) == 0) << text;
    EXPECT_NE(text.find("\r\nIL_0000: ldarg.0"), std::string::npos) << text;
    EXPECT_TRUE(text.size() > 4 && text.substr(text.size() - 5) == "ret\r\n")
        << text;

}

TEST(MethodBodyDisassemblerTest, DisassembleLocalsBlockRendersLocalTypes) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    bool found = false;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto mb = f.GetMethodBody(m.RVA);
            if (!mb.IsValid() || mb.LocalVarSigToken() == 0) continue;
            found = true;
            std::string text = DisassembleFlat(f, m.Token);
            auto localsAt = text.find(".locals");
            ASSERT_NE(localsAt, std::string::npos) << text;
            // The ShowMetadataTokens-off spelling: ".locals" (+ the init flag).
            EXPECT_TRUE(text.find(".locals") != std::string::npos) << text;
            auto parens = text.find('(', localsAt);
            auto close = text.find(')', parens);
            ASSERT_NE(parens, std::string::npos) << text;
            ASSERT_NE(close, std::string::npos) << text;
            std::string block = text.substr(localsAt, close - localsAt);
            // One "[N] <type>" line per local; the [0] definition is present.
            EXPECT_NE(block.find("[0]"), std::string::npos) << text;
            return;
        }
    }
    FAIL() << "no method with locals found";
}

TEST(MethodBodyDisassemblerTest, DisassembleRendersExceptionHandlers) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    bool found = false;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto mb = f.GetMethodBody(m.RVA);
            if (!mb.IsValid() || mb.Handlers().empty()) continue;
            found = true;
            std::string text = DisassembleFlat(f, m.Token);
            // The blank line then the .try clause lines (one per handler).
            EXPECT_NE(text.find("\r\n.try "), std::string::npos) << text;
            for (std::size_t i = 0; i < mb.Handlers().size(); i++) {
                auto next = text.find(".try ", text.find(".try ") + 1);
                if (i + 1 == mb.Handlers().size()) break;
                EXPECT_NE(next, std::string::npos) << text;
            }
            return;
        }
    }
    FAIL() << "no method with exception handlers found";
}

TEST(MethodBodyDisassemblerTest, DisassembleStructuredPathThrows) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    std::ostringstream stream;
    OUT::PlainTextOutput out(stream);
    MethodBodyDisassembler d(out);
    EXPECT_THROW(d.Disassemble(f, copy), std::logic_error);
}

TEST(MethodBodyDisassemblerTest, DisassembleSmokeOverManyMethods) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    int disassembled = 0;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto mb = f.GetMethodBody(m.RVA);
            if (!mb.IsValid() || mb.IL().empty()) continue;
            std::string text = DisassembleFlat(f, m.Token);
            EXPECT_TRUE(text.rfind("// Method begins at RVA 0x", 0) == 0) << text;
            EXPECT_NE(text.find("// Code size:"), std::string::npos) << text;
            EXPECT_NE(text.find(".maxstack"), std::string::npos) << text;
            if (++disassembled >= 500) return;
        }
    }
    ASSERT_GT(disassembled, 0) << "no method bodies disassembled";
}
