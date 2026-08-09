// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation, rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom
// the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Tests for the IField metadata foundation the next in-order transform
// (CachedReadOnlySpanInitialization) and the deferred field-cached delegate
// shapes consume: the LdFlda/LdsFlda::IsCompilerGeneratedField flag (resolved
// by the IL reader from the field token's [CompilerGenerated] custom attribute,
// or its declaring type's, via MetadataFile::IsFieldCompilerGeneratedOrIn-
// CompilerGeneratedClass), the FieldToken the reader carries so the flag can be
// re-resolved, and the ArrayInitializers setting that gates the transform. This
// is a tested-but-not-yet-wired foundation (the transform itself lands in a
// follow-up iteration, once the block-model shape of the lazy-cache if/then is
// probed); the mscorlib sweep exercises the helper and the reader's population
// of the flag on real field-access sites.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

ILVariablePtr MakeLocal(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

std::uint32_t FindType(MetadataFile& f, std::string_view ns, std::string_view name) {
    for (const auto& t : f.TypeDefs()) {
        if (t.Namespace == ns && t.Name == name) return t.Token;
    }
    return 0;
}

}  // namespace

// The LdsFlda/LdFlda nodes carry the IsCompilerGeneratedField flag and the
// FieldToken the reader resolved the name from; both default to false/0 and
// are settable, and the strict-tree invariant still holds with them set.
TEST(CompilerGeneratedField, NodeCarriesFlagAndToken) {
    auto v = MakeLocal("v");
    auto target = std::make_unique<LdLoc>(v);
    auto ldflda = std::make_unique<LdFlda>(std::move(target), "System.Foo::bar");
    EXPECT_FALSE(ldflda->IsCompilerGeneratedField);
    EXPECT_EQ(ldflda->FieldToken, 0u);
    ldflda->FieldToken = 0x04000007u;
    ldflda->IsCompilerGeneratedField = true;
    EXPECT_EQ(ldflda->FieldToken, 0x04000007u);
    EXPECT_TRUE(ldflda->IsCompilerGeneratedField);

    auto ldsflda = std::make_unique<LdsFlda>("System.Foo::bar");
    EXPECT_FALSE(ldsflda->IsCompilerGeneratedField);
    EXPECT_EQ(ldsflda->FieldToken, 0u);
    ldsflda->FieldToken = 0x04000008u;
    ldsflda->IsCompilerGeneratedField = true;
    EXPECT_EQ(ldsflda->FieldToken, 0x04000008u);
    EXPECT_TRUE(ldsflda->IsCompilerGeneratedField);
}

// The ArrayInitializers setting (which gates CachedReadOnlySpanInitialization)
// defaults true, matching DecompilerSettings.
TEST(CompilerGeneratedField, ArrayInitializersSettingDefault) {
    ILTransformContext ctx;
    EXPECT_TRUE(ctx.Settings.ArrayInitializers);
}

// The helper degrades gracefully for an out-of-range or unknown-table token.
TEST(CompilerGeneratedField, InvalidTokenIsGraceful) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    EXPECT_FALSE(f.IsFieldCompilerGeneratedOrInCompilerGeneratedClass(0x04000000u));  // row 0
    EXPECT_FALSE(f.IsFieldCompilerGeneratedOrInCompilerGeneratedClass(0x04FFFFFFu));  // huge row
    EXPECT_FALSE(f.IsFieldCompilerGeneratedOrInCompilerGeneratedClass(0x99000000u));  // unknown table
    EXPECT_FALSE(f.IsFieldCompilerGeneratedOrInCompilerGeneratedClass(0x02000001u));  // TypeDef, not Field
}

// A regular (non-compiler-generated) field -- System.String's first instance
// field -- is reported as not compiler-generated.
TEST(CompilerGeneratedField, NonCompilerGeneratedField) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    auto tok = FindType(f, "System", "String");
    ASSERT_NE(tok, 0u);
    auto fields = f.GetFields(tok);
    ASSERT_FALSE(fields.empty());
    // System.String's fields (m_firstChar, m_stringLength, ...) are plain
    // instance fields, not compiler-generated.
    EXPECT_FALSE(f.IsFieldCompilerGeneratedOrInCompilerGeneratedClass(fields[0].Token));
}

// The .NET Framework 4 (legacy csc) mscorlib corpus carries compiler-generated
// fields (cached-delegate static fields, display-class fields, ...), so the
// helper must find some; each one's [CompilerGenerated] is on the field itself
// or on a declaring type in its nesting chain.
TEST(CompilerGeneratedField, MscorlibHasCompilerGeneratedFields) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());
    int cgFieldCount = 0;
    int totalFields = 0;
    int typesWithCGField = 0;
    for (const auto& t : f.TypeDefs()) {
        bool typeHadCG = false;
        for (const auto& fld : f.GetFields(t.Token)) {
            ++totalFields;
            if (f.IsFieldCompilerGeneratedOrInCompilerGeneratedClass(fld.Token)) {
                ++cgFieldCount;
                typeHadCG = true;
            }
        }
        if (typeHadCG) ++typesWithCGField;
        if (totalFields > 200000) break;  // safety cap on a huge assembly
    }
    EXPECT_GT(totalFields, 1000) << "expected mscorlib to expose many fields";
    // mscorlib has cached-delegate / display-class compiler-generated fields.
    EXPECT_GT(cgFieldCount, 0) << "expected at least one compiler-generated field";
    EXPECT_GT(typesWithCGField, 0);
}

// The IL reader populates IsCompilerGeneratedField and FieldToken on every
// LdsFlda/LdFlda it emits, and the flag is consistent with a fresh call to the
// helper on the carried token. The ILAst invariant holds across the corpus.
TEST(CompilerGeneratedField, ReaderPopulatesFlag) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int fieldAccessCount = 0;          // LdsFlda + LdFlda nodes seen
    int cgFlagCount = 0;               // of those, IsCompilerGeneratedField == true
    int crossChecked = 0;              // flag matched a fresh helper call on the token
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (!inst) return;
            if (inst->Op != OpCode::LdsFlda && inst->Op != OpCode::LdFlda) return;
            ++fieldAccessCount;
            bool nodeFlag = (inst->Op == OpCode::LdsFlda)
                ? static_cast<LdsFlda*>(inst)->IsCompilerGeneratedField
                : static_cast<LdFlda*>(inst)->IsCompilerGeneratedField;
            std::uint32_t tok = (inst->Op == OpCode::LdsFlda)
                ? static_cast<LdsFlda*>(inst)->FieldToken
                : static_cast<LdFlda*>(inst)->FieldToken;
            if (nodeFlag) ++cgFlagCount;
            // The carried token is non-zero (the reader resolved a real field).
            ASSERT_NE(tok, 0u);
            // The flag must match a fresh helper call on the carried token --
            // the reader's pre-resolution is faithful.
            EXPECT_EQ(nodeFlag, f.IsFieldCompilerGeneratedOrInCompilerGeneratedClass(tok));
            ++crossChecked;
        });
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(fieldAccessCount, 0);
    EXPECT_GT(cgFlagCount, 0) << "expected the reader to flag some compiler-generated fields";
    EXPECT_EQ(crossChecked, fieldAccessCount);
}
