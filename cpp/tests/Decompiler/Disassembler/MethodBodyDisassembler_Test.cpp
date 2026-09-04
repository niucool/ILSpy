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
// ShowRawRVAOffsetAndBytes `/* ... */ ` comment geometry, the
// WriteMetadataToken comment/space matrix (show flags, base10, the null-token
// error path, the UserString non-entity path), the WriteInstruction operand
// switch, and the Disassemble flat and structured paths (the header/footer/
// blank-line shapes over synthetic streams, the .try/catch and loop structures
// and the flat-instruction-lines invariant over real mscorlib bodies).

#include "Decompiler/Disassembler/ILParser.hpp"
#include "Decompiler/Disassembler/ILStructure.hpp"
#include "Decompiler/Disassembler/MethodBodyDisassembler.hpp"
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/IL/InstructionOutputExtensions.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/ILDisassembler.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::Disassembler;
namespace MD = ILSpy::Decompiler::Metadata;
namespace OUT = ILSpy::Decompiler::Output;
namespace ILD = ILSpy::Decompiler::IL;
namespace Util = ILSpy::Decompiler::Util;

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

// ---------------------------------------------------------------------------
// WriteStructureHeader / WriteStructureBody / WriteStructureFooter
// (MethodBodyDisassembler.cs lines 216-268, 270-303, 305-325) and the
// DetectControlStructure structured branch of Disassemble (lines 131-138):
// synthetic IL streams pinning the exact header/footer/blank-line shapes,
// real mscorlib bodies pinning the .try/catch and loop structures end-to-end.
// ---------------------------------------------------------------------------

namespace {

std::string DisassembleStructured(MD::MetadataFile& f, std::uint32_t token) {
    std::ostringstream stream;
    OUT::PlainTextOutput out(stream);
    MethodBodyDisassembler d(out);
    // DetectControlStructure defaults to true: the structured branch.
    d.Disassemble(f, token);
    return stream.str();
}

// The text's lines with the leading indentation stripped: the structured
// path indents inside its blocks, the flat path renders at indent 0.
std::vector<std::string> StrippedLines(const std::string& text) {
    std::vector<std::string> result;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find("\r\n", pos);
        std::string line = text.substr(pos,
            (end == std::string::npos ? text.size() : end) - pos);
        pos = end == std::string::npos ? text.size() : end + 2;
        std::size_t content = line.find_first_not_of('\t');
        result.push_back(content == std::string::npos ? std::string()
            : line.substr(content));
    }
    return result;
}

// The "IL_xxxx: ..." instruction lines -- the per-instruction render the
// flat and structured paths must agree on (the structured walk emits every
// instruction exactly once, in offset order).
std::vector<std::string> InstructionLines(const std::string& text) {
    std::vector<std::string> result;
    for (const std::string& line : StrippedLines(text)) {
        if (line.rfind("IL_", 0) == 0) result.push_back(line);
    }
    return result;
}

// A root structure over a synthetic IL stream (no handlers, so the tree is
// just the loop structures the root ctor detects).
ILStructure MakeRootOver(MD::MetadataFile& f, const std::uint8_t* il,
    std::size_t size) {
    return ILStructure(f, 0x06000001u,
        MD::MetadataGenericContext::ForMethod(0x06000001u, f),
        Util::Span<const std::uint8_t>(il, size),
        Util::Span<const MD::ExceptionHandlerClause>());
}

}  // namespace

TEST(MethodBodyDisassemblerTest, DisassembleStructuredPathRendersBody) {
    // String.Copy with the default flags (DetectControlStructure = true): no
    // throw, the RVA header block, and the body through the structure tree.
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    std::uint32_t stringType = FindTypeDefTokenIn(f, "System", "String");
    ASSERT_NE(stringType, 0u);
    std::uint32_t copy = FindMethodIn(f, stringType, "Copy");
    ASSERT_NE(copy, 0u);
    std::uint32_t rva = f.GetMethodRVA(copy);
    ASSERT_NE(rva, 0u);
    auto body = f.GetMethodBody(rva);
    ASSERT_TRUE(body.IsValid());
    char buf[128];
    std::snprintf(buf, sizeof(buf), "// Method begins at RVA 0x%x\r\n// Header size: %u\r\n",
        rva, body.HeaderSize());
    std::string expected(buf);
    std::snprintf(buf, sizeof(buf), "// Code size: %u (0x%x)\r\n.maxstack %u\r\n",
        body.CodeSize(), body.CodeSize(), body.MaxStack());
    expected += buf;
    std::string text = DisassembleStructured(f, copy);
    EXPECT_TRUE(text.rfind(expected, 0) == 0) << text;
    EXPECT_NE(text.find("IL_0000: "), std::string::npos) << text;
    EXPECT_TRUE(text.size() > 4 && text.substr(text.size() - 5) == "ret\r\n") << text;
}

TEST(MethodBodyDisassemblerTest, WriteStructureHeaderMatrix) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    auto ctx = MD::MetadataGenericContext::ForMethod(0x06000001u, f);
    std::uint32_t exceptionType = FindTypeDefTokenIn(f, "System", "Exception");
    ASSERT_NE(exceptionType, 0u);

    auto renderHandler = [&](ILStructureType type,
        const MD::ExceptionHandlerClause& handler) {
        std::ostringstream stream;
        OUT::PlainTextOutput out(stream);
        MethodBodyDisassembler d(out);
        ILStructure s(f, 0x06000001u, ctx, type, 0, 5, handler);
        d.WriteStructureHeader(s);
        return stream.str();
    };
    auto renderLoop = [&](int loopEntryPoint) {
        std::ostringstream stream;
        OUT::PlainTextOutput out(stream);
        MethodBodyDisassembler d(out);
        ILStructure s(f, 0x06000001u, ctx, ILStructureType::Loop, 0, 5,
            loopEntryPoint);
        d.WriteStructureHeader(s);
        return stream.str();
    };

    // The loop header: "// loop start" with the entry-point head when one
    // is recorded, bare otherwise (LoopEntryPointOffset < 0).
    EXPECT_EQ(renderLoop(3), "// loop start (head: IL_0003)\r\n");
    EXPECT_EQ(renderLoop(-1), "// loop start\r\n");

    EXPECT_EQ(renderHandler(ILStructureType::Try, MD::ExceptionHandlerClause{}),
        ".try\r\n{\r\n");
    EXPECT_EQ(renderHandler(ILStructureType::Filter, MD::ExceptionHandlerClause{}),
        "filter\r\n{\r\n");

    // The catch header: "catch" plus the catch type at TypeName syntax
    // (through the structure's module and generic context), or bare
    // "catch" when the catch type is nil.
    MD::ExceptionHandlerClause catchWithType{};
    catchWithType.Kind = MD::ExceptionHandlerKind::Catch;
    catchWithType.ClassTokenOrFilterOffset = exceptionType;
    EXPECT_EQ(renderHandler(ILStructureType::Handler, catchWithType),
        "catch System.Exception\r\n{\r\n");

    MD::ExceptionHandlerClause catchNil{};
    catchNil.Kind = MD::ExceptionHandlerKind::Catch;
    EXPECT_EQ(renderHandler(ILStructureType::Handler, catchNil),
        "catch\r\n{\r\n");

    MD::ExceptionHandlerClause finally{};
    finally.Kind = MD::ExceptionHandlerKind::Finally;
    EXPECT_EQ(renderHandler(ILStructureType::Handler, finally),
        "finally\r\n{\r\n");

    MD::ExceptionHandlerClause fault{};
    fault.Kind = MD::ExceptionHandlerKind::Fault;
    EXPECT_EQ(renderHandler(ILStructureType::Handler, fault),
        "fault\r\n{\r\n");

    // The handler block of a filter block has no header.
    MD::ExceptionHandlerClause filterHandler{};
    filterHandler.Kind = MD::ExceptionHandlerKind::Filter;
    EXPECT_EQ(renderHandler(ILStructureType::Handler, filterHandler),
        "{\r\n");

    // The root structure is never a written header.
    EXPECT_THROW(renderHandler(ILStructureType::Root, MD::ExceptionHandlerClause{}),
        std::out_of_range);
}

TEST(MethodBodyDisassemblerTest, WriteStructureFooterMatrix) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    auto ctx = MD::MetadataGenericContext::ForMethod(0x06000001u, f);
    auto render = [&](ILStructureType type) {
        std::ostringstream stream;
        OUT::PlainTextOutput out(stream);
        MethodBodyDisassembler d(out);
        out.Indent();  // the footer unindents first
        ILStructure s(f, 0x06000001u, ctx, type, 0, 5,
            MD::ExceptionHandlerClause{});
        d.WriteStructureFooter(s);
        return stream.str();
    };
    EXPECT_EQ(render(ILStructureType::Loop), "// end loop\r\n");
    EXPECT_EQ(render(ILStructureType::Try), "} // end .try\r\n");
    EXPECT_EQ(render(ILStructureType::Handler), "} // end handler\r\n");
    EXPECT_EQ(render(ILStructureType::Filter), "} // end filter\r\n");
    EXPECT_THROW(render(ILStructureType::Root), std::out_of_range);
}

TEST(MethodBodyDisassemblerTest, WriteStructureBodyBlankLinePlacement) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // br.s at 0 -> IL_0004 (marked as a branch target); the ret at 4 renders
    // after the nop at 3 and in front of the branch-target bit.
    const std::uint8_t il[] = {0x2B, 0x02, 0x00, 0x00, 0x2A};
    Util::BitSet branchTargets(static_cast<int>(sizeof(il)));
    std::size_t pos = 0;
    SetBranchTargets(il, sizeof(il), pos, branchTargets);
    pos = 0;
    std::ostringstream stream;
    OUT::PlainTextOutput out(stream);
    MethodBodyDisassembler d(out);
    ILStructure root = MakeRootOver(f, il, sizeof(il));
    d.WriteStructureBody(f, root, branchTargets, il, sizeof(il), pos, 0);
    EXPECT_EQ(stream.str(),
        "IL_0000: br.s IL_0004\r\n"
        "\r\n"
        "IL_0002: nop\r\n"
        "IL_0003: nop\r\n"
        "\r\n"
        "IL_0004: ret\r\n");
}

TEST(MethodBodyDisassemblerTest, WriteStructureBodyRendersLoopChild) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // Six nops, a backward br.s to IL_0001 (the loop entry point: the nop at
    // 0 precedes it), and the ret after the loop: the root ctor detects the
    // Loop child [1, 8) and the walk renders it indented between the
    // root-level IL_0000 and IL_0008 instructions.
    const std::uint8_t il[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2B, 0xF9, 0x2A};
    Util::BitSet branchTargets(static_cast<int>(sizeof(il)));
    std::size_t pos = 0;
    SetBranchTargets(il, sizeof(il), pos, branchTargets);
    pos = 0;
    std::ostringstream stream;
    OUT::PlainTextOutput out(stream);
    MethodBodyDisassembler d(out);
    ILStructure root = MakeRootOver(f, il, sizeof(il));
    d.WriteStructureBody(f, root, branchTargets, il, sizeof(il), pos, 0);
    EXPECT_EQ(stream.str(),
        "IL_0000: nop\r\n"
        "// loop start (head: IL_0001)\r\n"
        "\tIL_0001: nop\r\n"
        "\tIL_0002: nop\r\n"
        "\tIL_0003: nop\r\n"
        "\tIL_0004: nop\r\n"
        "\tIL_0005: nop\r\n"
        "\tIL_0006: br.s IL_0001\r\n"
        "// end loop\r\n"
        "IL_0008: ret\r\n");
}

TEST(MethodBodyDisassemblerTest, DisassembleStructuredRendersTryCatchBlocks) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto mb = f.GetMethodBody(m.RVA);
            if (!mb.IsValid() || mb.Handlers().empty()) continue;
            const MD::ExceptionHandlerClause* catchEh = nullptr;
            for (const auto& eh : mb.Handlers()) {
                if (eh.Kind == MD::ExceptionHandlerKind::Catch
                    && eh.ClassTokenOrFilterOffset != 0) {
                    catchEh = &eh;
                    break;
                }
            }
            if (catchEh == nullptr) continue;
            std::string text = DisassembleStructured(f, m.Token);
            std::vector<std::string> lines = StrippedLines(text);
            // The catch type rendered through the same EntityHandle.WriteTo
            // the header uses, at the method's generic context.
            std::ostringstream catchStream;
            OUT::PlainTextOutput catchOut(catchStream);
            ILD::WriteTo(f, catchOut,
                MD::MetadataGenericContext::ForMethod(m.Token, f),
                catchEh->ClassTokenOrFilterOffset, ILNameSyntax::TypeName);
            std::string catchLine = "catch " + catchStream.str();
            EXPECT_NE(std::find(lines.begin(), lines.end(), ".try"), lines.end()) << text;
            EXPECT_NE(std::find(lines.begin(), lines.end(), catchLine), lines.end())
                << text << "\nexpected catch line: " << catchLine;
            EXPECT_NE(std::find(lines.begin(), lines.end(), "{"), lines.end()) << text;
            EXPECT_NE(std::find(lines.begin(), lines.end(), "} // end handler"),
                lines.end()) << text;
            EXPECT_NE(std::find(lines.begin(), lines.end(), "} // end .try"),
                lines.end()) << text;
            // The flat trailing EH clause lines are gone in the structured
            // path (the handlers render as blocks instead).
            EXPECT_EQ(text.find(".try IL_"), std::string::npos) << text;
            return;
        }
    }
    FAIL() << "no method with a catch handler found";
}

TEST(MethodBodyDisassemblerTest, DisassembleStructuredRendersLoopBlocks) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto mb = f.GetMethodBody(m.RVA);
            if (!mb.IsValid() || mb.IL().empty()) continue;
            std::string text = DisassembleStructured(f, m.Token);
            if (text.find("// loop start") == std::string::npos) continue;
            EXPECT_NE(text.find("// loop start (head: IL_"), std::string::npos) << text;
            EXPECT_NE(text.find("// end loop"), std::string::npos) << text;
            return;
        }
    }
    FAIL() << "no method with a detected loop found";
}

TEST(MethodBodyDisassemblerTest, DisassembleStructuredMatchesFlatInstructionLines) {
    MD::MetadataFile f(MscorlibPath());
    ASSERT_TRUE(f.IsValid());
    // The structured walk emits every instruction exactly once, in offset
    // order, with the same per-instruction render as the flat path (modulo
    // the indentation and the structure header/footer lines).
    int compared = 0;
    for (const auto& t : f.TypeDefs()) {
        for (const auto& m : f.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            auto mb = f.GetMethodBody(m.RVA);
            if (!mb.IsValid() || mb.IL().empty()) continue;
            std::string flat = DisassembleFlat(f, m.Token);
            std::string structured = DisassembleStructured(f, m.Token);
            EXPECT_EQ(InstructionLines(structured), InstructionLines(flat)) << m.Name;
            if (++compared >= 300) return;
        }
    }
    ASSERT_GT(compared, 0) << "no method bodies compared";
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
