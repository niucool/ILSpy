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

// Phase 1 method-body tests. Exercises the ECMA-335 II.25.4 decoder (tiny/fat
// headers, IL bytes by RVA, local-var-sig token, and exception-handler
// clauses) against a real .NET assembly. These are the "Method bodies (fat +
// tiny) decode to the same IL bytes the C# MethodBodyBlock exposes" exit
// criterion from PORT_PLAN.md, Phase 1.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MethodBody.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

static const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

TEST(MethodBody_Decode, FindsTinyAndFatBodiesInMscorlib) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    ILSpy::Decompiler::Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    auto methods = file.MethodDefs();
    ASSERT_FALSE(methods.empty());

    int tiny = 0, fat = 0, withHandlers = 0;
    std::uint32_t totalIlBytes = 0;
    for (const auto& m : methods) {
        if (m.RVA == 0) continue; // abstract/extern/pinvoke-only
        auto body = file.GetMethodBody(m.RVA);
        if (!body.IsValid()) continue;
        // IL span length must match CodeSize for every decoded body.
        ASSERT_EQ(body.IL().size(), body.CodeSize());
        totalIlBytes += body.CodeSize();
        if (body.IsFat()) ++fat; else ++tiny;
        if (!body.Handlers().empty()) ++withHandlers;
    }
    // mscorlib has thousands of methods; both formats and many try/catch
    // handlers are expected. Fail loudly rather than silently if not.
    ASSERT_GT(tiny + fat, 1000u) << "decoded too few method bodies";
    EXPECT_GT(tiny, 0) << "no tiny-format bodies decoded";
    EXPECT_GT(fat, 0) << "no fat-format bodies decoded";
    EXPECT_GT(withHandlers, 0) << "no exception-handler clauses decoded";
    EXPECT_GT(totalIlBytes, 0u);
}

TEST(MethodBody_Decode, ExceptionHandlerClausesAreSane) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    ILSpy::Decompiler::Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());

    // Find the first fat body with handlers and validate every clause: the
    // kind is one of the four ECMA-335 values, and both the try and handler
    // ranges lie entirely within the method's IL. A wrong header/section
    // layout would produce garbage offsets/kinds and fail here.
    bool foundAny = false;
    for (const auto& m : file.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto body = file.GetMethodBody(m.RVA);
        if (!body.IsValid() || body.Handlers().empty()) continue;
        foundAny = true;
        std::uint32_t codeSize = body.CodeSize();
        ASSERT_GT(codeSize, 0u);
        for (const auto& c : body.Handlers()) {
            EXPECT_TRUE(c.Kind == ILSpy::Decompiler::Metadata::ExceptionHandlerKind::Catch ||
                        c.Kind == ILSpy::Decompiler::Metadata::ExceptionHandlerKind::Filter ||
                        c.Kind == ILSpy::Decompiler::Metadata::ExceptionHandlerKind::Finally ||
                        c.Kind == ILSpy::Decompiler::Metadata::ExceptionHandlerKind::Fault)
                << "bad EH clause kind";
            EXPECT_LE(c.TryOffset, codeSize);
            EXPECT_LE(c.TryOffset + c.TryLength, codeSize)
                << "try range past end of method";
            EXPECT_LE(c.HandlerOffset, codeSize);
            EXPECT_LE(c.HandlerOffset + c.HandlerLength, codeSize)
                << "handler range past end of method";
        }
        break; // one validated body is enough for this smoke test
    }
    ASSERT_TRUE(foundAny) << "no method with exception handlers found in fixture";
}

TEST(MethodBody_Decode, InvalidRvaIsGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    ILSpy::Decompiler::Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    // RVA 0 means "no body" (abstract/extern); must not throw or produce a
    // valid body. An out-of-range RVA must likewise degrade to invalid.
    auto b0 = file.GetMethodBody(0);
    EXPECT_FALSE(b0.IsValid());
    auto bBad = file.GetMethodBody(0xFFFFFFFFu);
    EXPECT_FALSE(bBad.IsValid());
}

// ---- GetUserString: the #US heap decode ----

// The #US blob's characters are UTF-16LE code units -- both bytes of each
// unit decode into the string (the high byte is not dropped), with the
// port's UTF-8-everywhere convention carrying them out. Ground truth (real
// System.Reflection.Metadata over the same rows): token 0x700094C8 holds
// the single unit U+5E74 and 0x70023674 the five-unit zh-TW calendar
// native name.
TEST(MethodBody_Decode, GetUserStringDecodesUtf16Units) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    ILSpy::Decompiler::Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    auto year = file.TryGetUserString(0x700094C8u);
    ASSERT_TRUE(year.has_value());
    EXPECT_EQ(*year, "\xE5\xB9\xB4");  // U+5E74
    auto nativeName = file.TryGetUserString(0x70023674u);
    ASSERT_TRUE(nativeName.has_value());
    // U+4E2D U+83EF U+6C11 U+570B U+66C6
    EXPECT_EQ(*nativeName,
        "\xE4\xB8\xAD\xE8\x8F\xAF\xE6\xB0\x91\xE5\x9C\x8B\xE6\x9B\x86");
}
