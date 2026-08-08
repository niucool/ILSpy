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

// Token-resolution + IL text-emitter tests. ResolveTokenToString maps
// metadata tokens to display strings (the shared operand-resolution helper
// for the disassembler and the Phase 3 IL reader), and DisassembleILText emits
// a readable IL listing from a real method body. This is the core of Phase 6's
// ReflectionDisassembler and an end-to-end exerciser of the metadata + opcode
// work.

#include "Decompiler/Metadata/ILTextEmitter.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>

using namespace ILSpy::Decompiler::Metadata;

static const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

static std::uint32_t FindType(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

TEST(ResolveToken, TypeDefAndMethodDefAndField) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    // System.Object TypeDef token -> "System.Object".
    auto objectTok = FindType(f, "System", "Object");
    ASSERT_NE(objectTok, 0u);
    EXPECT_EQ(f.ResolveTokenToString(objectTok), "System.Object");

    // A MethodDef on System.Object -> "System.Object::<Name>".
    auto methods = f.GetMethods(objectTok);
    ASSERT_FALSE(methods.empty());
    bool sawEquals = false;
    for (const auto& m : methods) {
        auto s = f.ResolveTokenToString(m.Token);
        EXPECT_EQ(s.substr(0, s.find("::")), "System.Object");
        if (m.Name == "Equals") { EXPECT_EQ(s, "System.Object::Equals"); sawEquals = true; }
    }
    EXPECT_TRUE(sawEquals);

    // A Field on System.String -> "System.String::<Name>".
    auto stringTok = FindType(f, "System", "String");
    ASSERT_NE(stringTok, 0u);
    auto fields = f.GetFields(stringTok);
    ASSERT_FALSE(fields.empty());
    for (const auto& fld : fields) {
        auto s = f.ResolveTokenToString(fld.Token);
        EXPECT_EQ(s.substr(0, s.find("::")), "System.String");
    }
}

TEST(ResolveToken, FallbackForUnknownAndOutOfRange) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    // Unknown table and out-of-range rows emit the raw hex token.
    EXPECT_EQ(f.ResolveTokenToString(0x2B000000u), "0x2B000000");
    EXPECT_EQ(f.ResolveTokenToString(0x06000000u), "0x06000000");  // row 0
    EXPECT_EQ(f.ResolveTokenToString(0x06FFFFFFu), "0x06FFFFFF"); // huge row
}

TEST(ILTextEmitter, EmitsReadableListingForMethodBodies) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int emitted = 0;
    bool sawResolvedCall = false;
    bool sawRet = false;
    bool sawBranch = false;
    std::set<std::string> mnemonicsSeen;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = f.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        auto text = DisassembleILText(body.IL(),
            [&](std::uint32_t t) { return f.ResolveTokenToString(t); });
        if (text.empty()) continue;
        ++emitted;
        // Every line starts with an IL_xxxx label.
        EXPECT_NE(text.find("IL_0000"), std::string::npos);
        // Method bodies end in a transfer (ret/leave/throw); at least ret appears
        // across the corpus.
        if (text.find("\nret\n") != std::string::npos ||
            text.find("ret\n") == 0 ||
            (text.size() >= 4 && text.compare(text.size() - 4, 4, "ret\n") == 0)) {
            sawRet = true;
        }
        if (text.find("call System.") != std::string::npos ||
            text.find("callvirt System.") != std::string::npos) {
            sawResolvedCall = true;
        }
        if (text.find("brtrue IL_") != std::string::npos ||
            text.find("brfalse IL_") != std::string::npos ||
            text.find("br.s IL_") != std::string::npos ||
            text.find("beq IL_") != std::string::npos) {
            sawBranch = true;
        }
        if (emitted > 3000) break;
    }
    ASSERT_GT(emitted, 1000);
    EXPECT_TRUE(sawRet) << "no ret in emitted IL";
    EXPECT_TRUE(sawResolvedCall) << "no resolved call token in emitted IL";
    EXPECT_TRUE(sawBranch) << "no branch with an IL_ target in emitted IL";
}

TEST(ILTextEmitter, BranchTargetsAreInBounds) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    // For one method, every IL_xxxx branch target label must be within the
    // method's CodeSize -- a wrong target computation would land outside.
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = f.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        auto text = DisassembleILText(body.IL(),
            [&](std::uint32_t t) { return f.ResolveTokenToString(t); });
        // Scan lines that reference IL_xxxx targets after a branch mnemonic.
        std::size_t i = 0;
        while (i < text.size()) {
            std::size_t eol = text.find('\n', i);
            if (eol == std::string::npos) eol = text.size();
            std::string line = text.substr(i, eol - i);
            i = eol + 1;
            // Branch mnemonics carry " IL_xxxx" as the operand.
            auto isBranchLine = line.find(" IL_") != std::string::npos &&
                (line.find("br") != std::string::npos || line.find("beq") != std::string::npos ||
                 line.find("bge") != std::string::npos || line.find("bgt") != std::string::npos ||
                 line.find("ble") != std::string::npos || line.find("blt") != std::string::npos ||
                 line.find("bne") != std::string::npos);
            if (!isBranchLine) continue;
            // The target label is the last IL_xxxx in the line.
            auto pos = line.rfind("IL_");
            if (pos == std::string::npos) continue;
            std::uint32_t target = 0;
            for (std::size_t k = pos + 3; k < line.size() && line[k] >= '0' && line[k] <= '9'; ++k) {
                target = target * 16 + (line[k] - '0');
            }
            EXPECT_LT(target, body.CodeSize()) << "branch target out of bounds in: " << line;
        }
        break; // one method is enough for the in-bounds check
    }
}
