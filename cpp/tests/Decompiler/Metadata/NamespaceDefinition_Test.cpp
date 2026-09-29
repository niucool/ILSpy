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

// The namespace-definition-tree tests (the port's stand-in for System
// .Reflection.Metadata's NamespaceCache): every expectation is pinned
// byte-for-byte against the REAL .NET 10 System.Reflection.Metadata driven
// over the identical fixtures (C:/temp-probe/NsDefProbe -- the established
// gold-probe technique, here over the public GetNamespaceDefinitionRoot /
// GetNamespaceDefinition / GetString(NamespaceDefinitionHandle) surface plus
// a reflection dump of the private handle table). The embedded gold lives in
// TestFixtures/NamespaceTreeGold.hpp:
//   - the synthetic manifest (MetadataBuilder/ManagedPEBuilder) covering
//     every shape no real file pins together: the empty namespace, three
//     types on one "Q.R" heap location, the "P.Q.R" virtual-intermediate
//     chain, the IsNested skip, the mixed typedef/forwarder namespace, the
//     root forwarder, and all three ExportedType implementation kinds;
//   - the byte-patched merge variant pinning the duplicate-full-name merge;
//   - mscorlib 4.8 / System.dll 4.8 / .NET 10 CoreLib / the GAC
//     System.Runtime facade (the 279-forwarder table) / tiny.netmodule.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include <cstdlib>
#include "Decompiler/Metadata/NamespaceDefinition.hpp"
#include "TestFixtures/NamespaceTreeGold.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Metadata::NamespaceDefinition;
using ILSpy::Decompiler::Metadata::NamespaceDefinitionHandle;
using ILSpy::Tests::NsDefGold::kGoldCorelib;
using ILSpy::Tests::NsDefGold::kGoldFacade;
using ILSpy::Tests::NsDefGold::kGoldMerge;
using ILSpy::Tests::NsDefGold::kGoldMscorlib;
using ILSpy::Tests::NsDefGold::kGoldSynth;
using ILSpy::Tests::NsDefGold::kGoldSystem;
using ILSpy::Tests::NsDefGold::kGoldTiny;
using ILSpy::Tests::NsDefGold::kNsDefMergeHex;
using ILSpy::Tests::NsDefGold::kNsDefSynthHex;

namespace {

// A 0x%08x token render (the probe's string.Format form).
std::string Hex8(std::uint32_t value) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08x", value);
    return buffer;
}

// The gold literals are raw string literals, so their line endings are the
// fixture header's CRLF (the repo's Windows EOL discipline); the port's
// dump renders \n. The comparison strips the carriage returns (the probe
// generation's own CRLF normalization).
std::string NormalizeGold(const char* gold) {
    std::string result;
    for (const char* p = gold; *p; p++)
        if (*p != '\r') result.push_back(*p);
    return result;
}

std::string NodeLine(const char* kind, std::uint32_t handleValue,
                     const NamespaceDefinition& ns) {
    std::string parent = ns.Parent.IsNil()
        ? "nil" : std::to_string(ns.Parent.Value);
    return std::string(kind) + " handle=" + std::to_string(handleValue)
        + " name='" + ns.Name + "' full='" + ns.FullName + "' parent="
        + parent + " children="
        + std::to_string(ns.NamespaceDefinitions.size()) + " types="
        + std::to_string(ns.TypeDefinitions.size()) + " exported="
        + std::to_string(ns.ExportedTypes.size());
}

// The probe's DFS: one N line per node, the T/E token lines, then the
// children in order.
void DumpNode(const MetadataFile& file, const NamespaceDefinitionHandle& handle,
              int depth, bool emitTokens, std::string& out) {
    const NamespaceDefinition& ns = file.GetNamespaceDefinition(handle);
    out.append(static_cast<std::size_t>(2 * depth), ' ');
    out += NodeLine("N", handle.Value, ns) + "\n";
    if (emitTokens) {
        for (std::uint32_t token : ns.TypeDefinitions) {
            out.append(static_cast<std::size_t>(2 * depth), ' ');
            out += "T " + Hex8(token) + "\n";
        }
        for (std::uint32_t token : ns.ExportedTypes) {
            out.append(static_cast<std::size_t>(2 * depth), ' ');
            out += "E " + Hex8(token) + "\n";
        }
    }
    for (const NamespaceDefinitionHandle& child : ns.NamespaceDefinitions)
        DumpNode(file, child, depth + 1, emitTokens, out);
}

// The probe's per-fixture dump: the TREE walk plus the handle table in
// insertion order.
std::string DumpTree(const MetadataFile& file, bool emitTokens) {
    std::string out = "TREE\n";
    DumpNode(file, NamespaceDefinitionHandle{}, 0, emitTokens, out);
    out += "HANDLES\n";
    for (const NamespaceDefinitionHandle& handle :
         file.GetNamespaceDefinitionHandlesInTableOrder()) {
        out += NodeLine("H", handle.Value,
                        file.GetNamespaceDefinition(handle)) + "\n";
    }
    return out;
}

// Writes one of the embedded hex fixtures to a temp file (the
// WriteResTestDll pattern; MetadataFile needs a real file).
std::string WriteTempAssembly(const char* hex, const char* fileName) {
    std::string bytes;
    ILSpy::Tests::NsDefGold::HexToBytes(hex, bytes);
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / fileName;
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return "";
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    return path.string();
}

std::string SynthPath() {
    return WriteTempAssembly(kNsDefSynthHex, "ilspy_nsdef_synth.dll");
}

std::string MergePath() {
    return WriteTempAssembly(kNsDefMergeHex, "ilspy_nsdef_merge.dll");
}

// The direct child of a node with a given full name, or null.
const NamespaceDefinition* FindChild(const MetadataFile& file,
                                     const NamespaceDefinition& parent,
                                     const char* fullName) {
    for (const NamespaceDefinitionHandle& handle :
         parent.NamespaceDefinitions) {
        const NamespaceDefinition& candidate = file.GetNamespaceDefinition(handle);
        if (candidate.FullName == fullName) return &candidate;
    }
    return nullptr;
}

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// The corpus convention (see PORT_LOG_BAML.md): when ILSPY_TEST_MSCORLIB
// points into the .NET Framework 4.8 reference-assembly corpus, System.dll
// is the mscorlib's sibling and the System.Runtime facade lives under its
// Facades/ subdirectory.
std::string SystemDllPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System\\"
           "v4.0_4.0.0.0__b77a5c561934e089\\System.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB");
        env != nullptr && std::filesystem::exists(env)) {
        std::filesystem::path dir = std::filesystem::path(env).parent_path();
        return (dir / "System.dll").string();
    }
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

std::string FacadePath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
           "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB");
        env != nullptr && std::filesystem::exists(env)) {
        std::filesystem::path dir = std::filesystem::path(env).parent_path();
        return (dir / "Facades" / "System.Runtime.dll").string();
    }
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
           "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
#endif
}

#if defined(_WIN32)
// The .NET 10 shared-runtime CoreLib (the MetadataFileLookups_Test glob
// pattern: the newest installed Microsoft.NETCore.App).
std::string CoreLibPath() {
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App";
    std::error_code ec;
    std::string best;
    int bestMinor = -1;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        // The golds pin the .NET 10 metadata shape: only 10.x matches.
        const std::string name = it->path().filename().string();
        if (name.rfind("10.", 0) != 0) continue;
        int minor = 0;
        try {
            minor = std::stoi(name.substr(3));
        } catch (const std::logic_error&) {
            continue;
        }
        std::string candidate = it->path().string() + "\\System.Private.CoreLib.dll";
        if (minor > bestMinor && fs::exists(candidate, ec)) {
            best = candidate;
            bestMinor = minor;
        }
    }
    return best;
}
#endif

} // namespace

// The synthetic manifest: every namespace shape at once, full token lines.
TEST(NamespaceDefinitionTest, SynthTreeMatchesGold) {
    MetadataFile file(SynthPath());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(DumpTree(file, true), NormalizeGold(kGoldSynth));
}

// The byte-patched merge variant: the duplicate-full-name merge moves the
// middle "Q.R" row into the root's type list (appended after the root's own
// types, not in row order) and maps the second "" handle to the root's node.
TEST(NamespaceDefinitionTest, MergeTreeMatchesGold) {
    MetadataFile file(MergePath());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(DumpTree(file, true), NormalizeGold(kGoldMerge));
}

TEST(NamespaceDefinitionTest, MscorlibTreeMatchesGold) {
    MetadataFile file(MscorlibPath());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(DumpTree(file, false), NormalizeGold(kGoldMscorlib));
}

TEST(NamespaceDefinitionTest, SystemTreeMatchesGold) {
    MetadataFile file(SystemDllPath());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(DumpTree(file, false), NormalizeGold(kGoldSystem));
}

#if defined(_WIN32)
TEST(NamespaceDefinitionTest, CoreLibTreeMatchesGold) {
    std::string path = CoreLibPath();
    ASSERT_FALSE(path.empty());
    MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(DumpTree(file, false), NormalizeGold(kGoldCorelib));
}
#endif

TEST(NamespaceDefinitionTest, FacadeTreeMatchesGold) {
    if (!std::filesystem::exists(FacadePath()))
        GTEST_SKIP() << "the GAC System.Runtime facade is Windows-only";
    MetadataFile file(FacadePath());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(DumpTree(file, true), NormalizeGold(kGoldFacade));
}

TEST(NamespaceDefinitionTest, TinyNetModuleTreeMatchesGold) {
    MetadataFile file(WriteTinyNetModule());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(DumpTree(file, true), NormalizeGold(kGoldTiny));
}

// The merge arm's exact behavior, pinned field by field so a dump mismatch
// stays debuggable: the root's type list is [row 1, row 3] (the root's own
// row first, the merged row appended), the "Q.R" namespace keeps [2, 4], and
// the second "" handle resolves to the root node.
TEST(NamespaceDefinitionTest, MergeArmMovesRowIntoRoot) {
    MetadataFile file(MergePath());
    ASSERT_TRUE(file.IsValid());
    const NamespaceDefinition& root = file.GetNamespaceDefinitionRoot();
    ASSERT_EQ(root.TypeDefinitions.size(), 2u);
    EXPECT_EQ(root.TypeDefinitions[0], 0x02000001u);
    EXPECT_EQ(root.TypeDefinitions[1], 0x02000003u);
    const NamespaceDefinition* q =
        FindChild(file, root, "Q");
    ASSERT_NE(q, nullptr);
    const NamespaceDefinition* qr = FindChild(file, *q, "Q.R");
    ASSERT_NE(qr, nullptr);
    ASSERT_EQ(qr->TypeDefinitions.size(), 2u);
    EXPECT_EQ(qr->TypeDefinitions[0], 0x02000002u);
    EXPECT_EQ(qr->TypeDefinitions[1], 0x02000004u);
    // The second "" handle (163 on this fixture: the "Other\0" NUL byte) maps
    // to the root's own node.
    NamespaceDefinitionHandle secondRoot{};
    bool found = false;
    for (const NamespaceDefinitionHandle& handle :
         file.GetNamespaceDefinitionHandlesInTableOrder()) {
        if (handle.Value != 0
            && &file.GetNamespaceDefinition(handle) == &root) {
            secondRoot = handle;
            found = true;
        }
    }
    ASSERT_TRUE(found);
    EXPECT_EQ(secondRoot.Value, 163u);
}

// The unknown-handle arm: SRM's Throw.InvalidHandle() -- the port maps the
// BadImageFormatException to std::out_of_range with the exact message.
TEST(NamespaceDefinitionTest, UnknownHandleThrows) {
    MetadataFile file(SynthPath());
    ASSERT_TRUE(file.IsValid());
    try {
        file.GetNamespaceDefinition(
            NamespaceDefinitionHandle::FromFullNameOffset(0x7000));
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Invalid handle.");
    }
}

// The nil-handle quirk: the root's full-name offset 0 doubles as the nil
// handle value, so the nil handle resolves to the root's own node (the same
// cached instance GetNamespaceDefinitionRoot returns).
TEST(NamespaceDefinitionTest, NilHandleIsTheRoot) {
    MetadataFile file(SynthPath());
    ASSERT_TRUE(file.IsValid());
    const NamespaceDefinition& root = file.GetNamespaceDefinitionRoot();
    EXPECT_EQ(&file.GetNamespaceDefinition(NamespaceDefinitionHandle{}), &root);
    EXPECT_EQ(root.Name, "");
    EXPECT_EQ(root.FullName, "");
    EXPECT_EQ(root.NamespaceDefinitions.size(), 6u);
    EXPECT_EQ(root.TypeDefinitions.size(), 1u);
    EXPECT_EQ(root.ExportedTypes.size(), 1u);
}

// The C# MetadataReader.GetString(NamespaceDefinitionHandle) surface: the
// full name of a synthesized virtual namespace.
TEST(NamespaceDefinitionTest, GetNamespaceStringOfVirtualHandle) {
    MetadataFile file(SynthPath());
    ASSERT_TRUE(file.IsValid());
    EXPECT_EQ(file.GetNamespaceString(
                  NamespaceDefinitionHandle::FromVirtualIndex(1)),
        "Q");
    EXPECT_EQ(file.GetNamespaceString(NamespaceDefinitionHandle{}), "");
}

// An invalid file degrades to the root-only tree instead of throwing (the
// never-throw surface convention; NamespaceDefinition.hpp documents the
// divergence from the C# reader, which never constructs).
TEST(NamespaceDefinitionTest, InvalidFileDegradesToRootOnly) {
    std::string bytes(64, 'x');
    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "ilspy_nsdef_garbage.dll";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    ASSERT_NE(out, nullptr);
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    MetadataFile file(path.string());
    ASSERT_FALSE(file.IsValid());
    const NamespaceDefinition& root = file.GetNamespaceDefinitionRoot();
    EXPECT_EQ(root.Name, "");
    EXPECT_EQ(root.FullName, "");
    EXPECT_TRUE(root.NamespaceDefinitions.empty());
    EXPECT_TRUE(root.TypeDefinitions.empty());
    EXPECT_TRUE(root.ExportedTypes.empty());
    // The degraded table still maps the root's own handle (0 / nil) to the
    // root node -- the C# table always contains the root entry.
    auto handles = file.GetNamespaceDefinitionHandlesInTableOrder();
    ASSERT_EQ(handles.size(), 1u);
    EXPECT_EQ(handles[0].Value, 0u);
    EXPECT_EQ(&file.GetNamespaceDefinition(handles[0]), &root);
}
