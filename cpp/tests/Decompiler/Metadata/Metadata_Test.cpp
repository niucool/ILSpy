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

// Phase 1 smoke test: prove the vendored microsoft/winmd baseline parses a real
// .NET assembly through the MetadataFile adapter. This is not the full Phase 1
// exit criteria (method bodies, debug tables, signatures, reference resolution
// come as the gap fills land); it is the first green rung of the metadata phase.

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

static const char* FixturePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

TEST(Metadata_Smoke, OpensMscorlibAndCountsTypeDefs) {
    const char* path = FixturePath();
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "fixture " << path << " not present on this host";
    }
    ILSpy::Decompiler::Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid())
        << "opened " << path << " but it was not recognised as a CLI assembly";
    // mscorlib defines thousands of types; the first TypeDef row is the
    // <Module> pseudo-type, so scan the whole table rather than a small window.
    ASSERT_GT(file.TypeDefCount(), 1000u);

    auto names = file.TopTypeNames(file.TypeDefCount());
    ASSERT_FALSE(names.empty());
    bool foundObject = false;
    for (auto& n : names) {
        if (n == "Object") foundObject = true;
    }
    EXPECT_TRUE(foundObject)
        << "System.Object not found among " << names.size()
        << " TypeDefs of " << path
        << "; first few: " << names[0] << ", " << names[1] << ", " << names[2];
}

TEST(Metadata_Smoke, RejectsNonCliFile) {
    // A plain text file is not a PE image, so is_database() must reject it and
    // the adapter must report invalid rather than throwing.
    const char* path = "this-is-not-an-assembly.txt";
    ILSpy::Decompiler::Metadata::MetadataFile file(path);
    EXPECT_FALSE(file.IsValid());
    EXPECT_EQ(file.TypeDefCount(), 0u);
    EXPECT_TRUE(file.TopTypeNames(4).empty());
}
