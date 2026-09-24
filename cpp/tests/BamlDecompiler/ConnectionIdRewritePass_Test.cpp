// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The ConnectionIdRewritePass tests: the pass driven end to end through
// ResourceExtensions::DecompileBaml over the ConnIdResFixtures fixture
// assembly (connid_res.dll -- a real csc-compiled library with the WPF
// stand-in shapes and the MyApp.Page1 code-behind Connect method), whose
// every render is byte-exact against the real ilspycmd 11.0 --resource
// output over the identical fixture bytes.

#include "TestFixtures/ConnIdResFixtures.hpp"

#include "BamlDecompiler/BamlDecompilationResult.hpp"
#include "BamlDecompiler/BamlDecompilerSettings.hpp"
#include "BamlDecompiler/BamlDecompilerTypeSystem.hpp"
#include "BamlDecompiler/XamlDecompiler.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "ILSpyCmd/ResourceExtensions.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace ILSpy;
using ILSpy::BamlDecompiler::BamlDecompilerSettings;

std::string BamlHexToBytes(const char* hex)
{
    std::string bytes;
    bytes.reserve(std::char_traits<char>::length(hex) / 2);
    for (const char* p = hex; p[0] && p[1]; p += 2) {
        int hi = p[0] <= '9' ? p[0] - '0' : (p[0] | 32) - 'a' + 10;
        int lo = p[1] <= '9' ? p[1] - '0' : (p[1] | 32) - 'a' + 10;
        bytes.push_back(static_cast<char>(hi * 16 + lo));
    }
    return bytes;
}

// The .NET Framework 4.x mscorlib the pass's type system needs on every
// host (the repo-wide ILSPY_TEST_MSCORLIB convention): KnownThings resolves
// the "mscorlib" default reference through the resolver, and only a real
// mscorlib gives System.Boolean its KnownTypeCode (the synthetic
// stand-in's Boolean has none, so _contentLoaded would never register).
std::string MscorlibPath()
{
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// Points the fixture's resolver at the mscorlib's directory so the
// assembly-resolution walk finds it (the resolver only searches the main
// assembly's directory, the machine GAC, and the dotnet shared framework
// -- none of which carry a .NET Framework mscorlib on a POSIX host).
void AddMscorlibSearchDirectory(
    Decompiler::Metadata::UniversalAssemblyResolver& resolver)
{
    const std::string path = MscorlibPath();
    if (!std::filesystem::exists(path))
        return;
    resolver.AddSearchDirectory(
        std::filesystem::path(path).parent_path().string());
}

} // namespace

// The gold end to end: the five connection-id children render exactly the
// real tool's XAML -- x:Name (public -> x:FieldModifier), the bare field
// assignment, the attached-event attribute, the Style target's EventSetter
// child, and the unknown-id comment.
TEST(ConnectionIdRewritePassTest, GoldRender)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    auto value = ILSpy::ILSpyCmd::TryGetResource(module, "page.xaml.baml");
    ASSERT_TRUE(value.has_value());
    ASSERT_EQ(value->kind,
        Decompiler::Util::ResourceValue::Kind::ByteArray);

    Decompiler::Metadata::UniversalAssemblyResolver resolver(
        path, false,
        Decompiler::Metadata::DetectTargetFrameworkId(module, std::nullopt));
    AddMscorlibSearchDirectory(resolver);
    BamlDecompilerSettings settings;
    auto xaml = ILSpy::ILSpyCmd::DecompileBaml(module, resolver,
        value->bytes.data(), value->bytes.size(), settings);
    ASSERT_NE(xaml, nullptr);
    EXPECT_EQ(xaml->ToString(), ILSpy::Tests::ConnIdGoldXaml());
}

// The Decompile result's GeneratedMembers: the Connect method, the
// InitializeComponent, and the _contentLoaded field tokens the pass
// registers (the field-assignment field tokens are the fourth family --
// every id's stfld target field). Driven through XamlDecompiler directly
// because DecompileBaml drops the result.
TEST(ConnectionIdRewritePassTest, GeneratedMembersRegistered)
{
    const std::string mscorlib = MscorlibPath();
    if (!std::filesystem::exists(mscorlib)) {
        GTEST_SKIP() << "mscorlib fixture " << mscorlib
                     << " not present on this host";
    }
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    auto value = ILSpy::ILSpyCmd::TryGetResource(module, "page.xaml.baml");
    ASSERT_TRUE(value.has_value());

    Decompiler::Metadata::UniversalAssemblyResolver resolver(
        path, false,
        Decompiler::Metadata::DetectTargetFrameworkId(module, std::nullopt));
    AddMscorlibSearchDirectory(resolver);
    BamlDecompilerSettings settings;
    BamlDecompiler::BamlDecompilerTypeSystem typeSystem(module, resolver);
    BamlDecompiler::XamlDecompiler decompiler(typeSystem, &settings);
    BamlDecompiler::BamlDecompilationResult result =
        decompiler.Decompile(value->bytes.data(), value->bytes.size());

    // The page's type tokens: the MainModule Page1 type definition's members
    // must all appear (Connect + InitializeComponent + _contentLoaded) plus
    // the three assigned fields (the public + the assembly one; the shared
    // Button1 fields of ids 1/2).
    const std::vector<std::uint32_t>& members = result.GeneratedMembers();
    ASSERT_GE(members.size(), 5u);

    // The code-behind type's member tokens via the metadata surface.
    auto* metaModule = dynamic_cast<const Decompiler::TypeSystem::MetadataModule*>(
        &typeSystem.MainModule());
    ASSERT_NE(metaModule, nullptr);
    const Decompiler::TypeSystem::ITypePtr pageType = Decompiler::TypeSystem::FindType(
        typeSystem,
        Decompiler::TypeSystem::FullTypeName(
            Decompiler::TypeSystem::TopLevelTypeName("MyApp", "Page1")));
    ASSERT_NE(pageType, nullptr);
    const Decompiler::TypeSystem::ITypeDefinition* pageDef = pageType->GetDefinition();
    ASSERT_NE(pageDef, nullptr);

    // The pass registers the Connect + InitializeComponent method tokens and
    // every field token (the _contentLoaded field plus each assigned button
    // field); the compiler-generated .ctor is not a generated member.
    std::vector<std::uint32_t> expected;
    for (const Decompiler::TypeSystem::IMethod* m : pageDef->Methods()) {
        if (m && (m->Name() == "Connect" || m->Name() == "InitializeComponent"))
            expected.push_back(m->MetadataToken());
    }
    for (const Decompiler::TypeSystem::IField* f : pageDef->GetFields(nullptr)) {
        if (f)
            expected.push_back(f->MetadataToken());
    }
    ASSERT_FALSE(expected.empty());
    for (std::uint32_t token : expected) {
        bool contains = false;
        for (std::uint32_t m : members) {
            if (m == token) {
                contains = true;
                break;
            }
        }
        EXPECT_TRUE(contains) << "missing token 0x" << std::hex << token;
        (void)contains;
    }
}
