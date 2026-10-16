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

// GetFieldSignature over MemberRef field references: the relinked
// same-module assemblies carry `ldfld` through fieldrefs (MemberRef rows),
// whose signature blob is the row's own (not the Field table's). The VAR
// scope is the parent type's definition -- a TypeSpec parent's GENERICINST
// blob names it. The LazyList`1 fields of the de4dot dnlib fixture are the
// corpus case (listener/list/id).

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <string>

using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

const char* DnlibPath() {
    return "/home/jim/source/de4dot/Release/netcoreapp3.1/dnlib.dll";
}

bool DnlibPresent() { return std::filesystem::exists(DnlibPath()); }

// LazyList`1::Set_NoLock's `ldfld listener` -- a MemberRef row whose parent
// is the TypeSpec `GENERICINST LazyList`1<!0>`. The raw body bytes carry the
// token; the fixed MemberRef path decodes the field type through the row's
// own blob with the parent definition's generic-parameter names.
std::uint32_t FirstLdfldToken(const MetadataFile& file, std::uint32_t methodToken) {
    auto body = file.GetMethodBody(file.GetMethodRVA(methodToken));
    if (!body.IsValid()) return 0;
    auto il = body.IL();
    for (std::size_t i = 0; i + 5 <= il.size(); ++i) {
        if (il[i] == 0x7B)  // ldfld
            return il[i + 1] | (il[i + 2] << 8) | (il[i + 3] << 16) |
                   (static_cast<std::uint32_t>(il[i + 4]) << 24);
    }
    return 0;
}

}  // namespace

TEST(MemberRefFieldSignature, DecodesTheMemberRefBlobWithTheParentVarScope) {
    if (!DnlibPresent()) GTEST_SKIP() << "fixture not present";
    MetadataFile file(DnlibPath());
    ASSERT_TRUE(file.IsValid());
    // Find LazyList`1::Set_NoLock (any token scan range covering it).
    std::uint32_t methodToken = 0;
    for (std::uint32_t tok = 0x06000001; tok <= 0x06000FFF; ++tok) {
        if (!file.GetMethodSignature(tok)) continue;
        if (file.GetMethodRVA(tok) == 0) continue;
        if (file.ResolveTokenToString(tok, 0).find("Set_NoLock") != std::string::npos) {
            methodToken = tok;
            break;
        }
    }
    ASSERT_NE(methodToken, 0u) << "LazyList`1::Set_NoLock not found";
    std::uint32_t fieldToken = FirstLdfldToken(file, methodToken);
    ASSERT_NE(fieldToken, 0u);
    // The MemberRef table byte: the relinked dnlib carries the field access
    // through a fieldref.
    ASSERT_EQ(fieldToken >> 24, 0x0Au);
    auto t = file.GetFieldSignature(fieldToken);
    ASSERT_NE(t, nullptr);
    // Before the MemberRef path: the rid masked into the Field table read
    // some unrelated field's signature (System.UInt32 for the listener).
    EXPECT_EQ(t->ReflectionName(), "dnlib.Utils.IListListener`1[[TValue]]");

    // The reader threads the decoded type into the LdObj (the null-typed
    // condition render reads it).
    auto fn = ILSpy::Decompiler::IL::ReadIL(file, methodToken,
                                            file.GetMethodRVA(methodToken));
    ASSERT_NE(fn, nullptr);
    std::string tree = fn->ToString();
    EXPECT_NE(tree.find("ldobj(dnlib.Utils.IListListener`1[[TValue]], ldflda("),
              std::string::npos)
        << "the reader carries the MemberRef field type:\n"
        << tree.substr(0, 400);
    EXPECT_EQ(tree.find("ldobj(System.UInt32, ldflda("), std::string::npos)
        << "not the scrambled Field-table row:\n"
        << tree.substr(0, 400);
}
