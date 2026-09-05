// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the MetadataModule skeleton + MetadataNamespace port
// (TypeSystem/MetadataModule.{hpp,cpp} + TypeSystem/Implementation/
// MetadataNamespace.{hpp,cpp}): the ctor's assembly-identity arms and the
// IModule identity surface over the real .NET Framework 4.8 mscorlib, the
// tiny.netmodule (the netmodule arm), and the iteration-63 corrupt-Name
// manifest (the BadImageFormatException catch arm), plus the per-module
// namespace tree over the iteration-62 namespace cache -- every expectation
// gold-dumped from the REAL ICSharpCode.Decompiler 11.0 driven over the
// identical fixtures (C:/temp-probe/MetaModProbe: a SimpleCompilation whose
// PEFile main module resolves to the real MetadataModule, the root namespace
// a real MetadataNamespace; the full dump in
// C:/temp-probe/MetaModProbe/gold_raw.txt).
//
// The entity-resolving members (GetDefinition's entity construction,
// TypeDefinitions, the attribute snapshots, the InternalsVisibleTo friend
// list) are the loud deferrals of the skeleton slice: their tests pin the
// deferral contracts (which member throws, from which routed call) alongside
// the plumbing that IS landed (the nil / row-range checks).

#include "TestFixtures/AssemblyIdentityFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/Version.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// A compilation whose main module is settable: the MetadataModule ctor takes the
// compilation BEFORE the compilation can know its main module (the C# resolves
// the module through PEFile.Resolve DURING SimpleCompilation.Init -- the ctor
// only stores the compilation, so the module exists before it can be registered
// as the main module). The stub routes RootNamespace through the main module
// (the SimpleCompilation CreateRootNamespace shape) and holds a KnownType for
// the non-null FindType contract.
class MainModuleCompilation : public TS::ICompilation {
public:
    MainModuleCompilation() = default;

    void SetMainModule(const TS::IModule* module) { mainModule_ = module; }

    // --- ICompilation ---
    const TS::IModule& MainModule() const override { return *mainModule_; }
    std::vector<const TS::IModule*> Modules() const override
    {
        return std::vector<const TS::IModule*>{ mainModule_ };
    }
    std::vector<const TS::IModule*> ReferencedModules() const override { return {}; }
    const TS::INamespace& RootNamespace() const override
    {
        return mainModule_->RootNamespace();
    }
    const TS::INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const TS::IType& FindType(TS::KnownTypeCode) const override { return knownType_; }
    const TS::StringComparer& NameComparer() const override
    {
        return TS::StringComparer::Ordinal();
    }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    TS::TypeSystemOptions TypeSystemOptions() const override
    {
        return TS::TypeSystemOptions::Default;
    }

private:
    const TS::IModule* mainModule_ = nullptr;
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
    TS::KnownType knownType_{ TS::KnownTypeCode::Object };
};

// The mscorlib fixture: the real file, the module over it, and a compilation
// whose main module it becomes (the probe's SimpleCompilation shape).
struct MscorlibFixture {
    TM::MetadataFile file{ MscorlibPath() };
    MainModuleCompilation compilation;
    TS::MetadataModule module{ compilation, &file, TS::TypeSystemOptions::Default };

    MscorlibFixture() { compilation.SetMainModule(&module); }
};

// The netmodule / corrupt-manifest fixtures (the temp-file writers from the
// shared fixtures headers).
std::string WriteCorruptManifest()
{
    return WriteTempAssembly(CorruptNameManifestBytes(),
                             "ilspy_metamodule_corrupt_test.dll");
}

std::string Join(const std::vector<const TS::INamespace*>& namespaces)
{
    std::string result;
    for (const TS::INamespace* ns : namespaces)
    {
        if (!result.empty())
            result += ",";
        result += ns->FullName();
    }
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// MetadataModuleTest: the ctor's identity arms + the IModule surface
// ---------------------------------------------------------------------------

TEST(MetadataModuleTest, CtorComputesAssemblyIdentityOverMscorlib)
{
    MscorlibFixture f;
    EXPECT_EQ(f.module.AssemblyName(), "mscorlib");
    EXPECT_EQ(f.module.AssemblyVersion(), TS::Version(4, 0, 0, 0));
    EXPECT_EQ(f.module.AssemblyVersion().ToString(), "4.0.0.0");
    EXPECT_EQ(f.module.FullAssemblyName(),
              "mscorlib, Version=4.0.0.0, Culture=neutral, "
              "PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(f.module.MetadataFile(), &f.file);
    EXPECT_EQ(&f.module.Compilation(), &f.compilation);
    EXPECT_EQ(f.module.TypeSystemOptions(), TS::TypeSystemOptions::Default);
}

TEST(MetadataModuleTest, NetmoduleArmComputesTheModuleIdentity)
{
    std::string tinyPath = WriteTinyNetModule();
    ASSERT_FALSE(tinyPath.empty());
    TM::MetadataFile file(tinyPath);
    MainModuleCompilation compilation;
    TS::MetadataModule module(compilation, &file, TS::TypeSystemOptions::Default);
    compilation.SetMainModule(&module);

    // The netmodule arm: the Module row's name as both names, and the null
    // AssemblyVersion as the default-constructed Version stand-in.
    EXPECT_EQ(module.AssemblyName(), "tiny");
    EXPECT_EQ(module.FullAssemblyName(), "tiny");
    EXPECT_EQ(module.AssemblyVersion(), TS::Version());
    // The netmodule's namespace tree: every type in the empty namespace, so
    // the root carries no child namespaces.
    EXPECT_TRUE(module.RootNamespace().ChildNamespaces().empty());
}

TEST(MetadataModuleTest, CorruptAssemblyRowYieldsTheErrorNames)
{
    std::string corruptPath = WriteCorruptManifest();
    ASSERT_FALSE(corruptPath.empty());
    TM::MetadataFile file(corruptPath);
    MainModuleCompilation compilation;
    TS::MetadataModule module(compilation, &file, TS::TypeSystemOptions::Default);
    compilation.SetMainModule(&module);

    // The C# catch (BadImageFormatException) arms: both rendered names become
    // the error marker and AssemblyVersion keeps its null stand-in.
    EXPECT_EQ(module.AssemblyName(), "<ERR: invalid assembly name>");
    EXPECT_EQ(module.FullAssemblyName(), "<ERR: invalid assembly name>");
    EXPECT_EQ(module.AssemblyVersion(), TS::Version());
    // The manifest's types all sit in the empty namespace: no child
    // namespaces, and a top-level miss through the root still resolves null.
    EXPECT_TRUE(module.RootNamespace().ChildNamespaces().empty());
    EXPECT_EQ(module.GetTypeDefinition(TS::TopLevelTypeName("", "NoSuchType", 0)),
              nullptr);
}

TEST(MetadataModuleTest, IsMainModuleComparesAgainstTheCompilationMainModule)
{
    MscorlibFixture f;
    EXPECT_TRUE(f.module.IsMainModule());

    // A second module over the same compilation is not the main module.
    std::string tinyPath = WriteTinyNetModule();
    ASSERT_FALSE(tinyPath.empty());
    TM::MetadataFile file(tinyPath);
    TS::MetadataModule other(f.compilation, &file, TS::TypeSystemOptions::Default);
    EXPECT_FALSE(other.IsMainModule());
    // And the main module through the other module's own compilation: the
    // tiny module registered as ITS compilation's main module is its own
    // main module.
    MainModuleCompilation otherCompilation;
    TS::MetadataModule tinyModule(otherCompilation, &file,
                                   TS::TypeSystemOptions::Default);
    otherCompilation.SetMainModule(&tinyModule);
    EXPECT_TRUE(tinyModule.IsMainModule());
}

TEST(MetadataModuleTest, SymbolSurfaceMatchesGold)
{
    MscorlibFixture f;
    EXPECT_EQ(f.module.Name(), "mscorlib");
    EXPECT_EQ(f.module.SymbolKind(), TS::SymbolKind::Module);

    // The ISymbol dispatch through the base reference.
    const TS::ISymbol& symbol = f.module;
    EXPECT_EQ(symbol.Name(), "mscorlib");
    EXPECT_EQ(symbol.SymbolKind(), TS::SymbolKind::Module);

    // The corrupt manifest's ISymbol.Name is the error marker (the C#
    // explicit `string ISymbol.Name => AssemblyName`).
    std::string corruptPath = WriteCorruptManifest();
    ASSERT_FALSE(corruptPath.empty());
    TM::MetadataFile corruptFile(corruptPath);
    MainModuleCompilation corruptCompilation;
    TS::MetadataModule corrupt(corruptCompilation, &corruptFile,
                               TS::TypeSystemOptions::Default);
    corruptCompilation.SetMainModule(&corrupt);
    EXPECT_EQ(corrupt.Name(), "<ERR: invalid assembly name>");
}

TEST(MetadataModuleTest, GetTypeDefinitionMissArmsReturnNull)
{
    MscorlibFixture f;
    EXPECT_EQ(f.module.GetTypeDefinition(
                  TS::TopLevelTypeName("System", "NoSuchType", 0)),
              nullptr);
    EXPECT_EQ(f.module.GetTypeDefinition(
                  TS::TopLevelTypeName("System", "String", 1)),
              nullptr);
    EXPECT_EQ(f.module.GetTypeDefinition(
                  TS::TopLevelTypeName("NoSuch.Namespace", "String", 0)),
              nullptr);
    EXPECT_EQ(f.module.GetTypeDefinition(
                  TS::TopLevelTypeName("", "NoSuchType", 0)),
              nullptr);
}

TEST(MetadataModuleTest, GetTypeDefinitionHitThrowsTheEntityDeferral)
{
    MscorlibFixture f;
    // The reverse lookup hits System.String (0x02000073); the hit routes
    // through GetDefinition, whose entity construction is the next slice.
    EXPECT_THROW(f.module.GetTypeDefinition(
                     TS::TopLevelTypeName("System", "String", 0)),
                 std::logic_error);
}

TEST(MetadataModuleTest, TypeEnumerationsThrowTheEntityDeferral)
{
    MscorlibFixture f;
    EXPECT_THROW(f.module.TypeDefinitions(), std::logic_error);
    EXPECT_THROW(f.module.TopLevelTypeDefinitions(), std::logic_error);
}

TEST(MetadataModuleTest, AttributeAndIvtDeferrals)
{
    MscorlibFixture f;
    EXPECT_THROW(f.module.GetAssemblyAttributes(), std::logic_error);
    EXPECT_THROW(f.module.GetModuleAttributes(), std::logic_error);

    // The self arm returns true before the friend-list decode (which is the
    // deferred GetInternalsVisibleTo).
    EXPECT_TRUE(f.module.InternalsVisibleTo(f.module));

    std::string tinyPath = WriteTinyNetModule();
    ASSERT_FALSE(tinyPath.empty());
    TM::MetadataFile file(tinyPath);
    MainModuleCompilation tinyCompilation;
    TS::MetadataModule tiny(tinyCompilation, &file, TS::TypeSystemOptions::Default);
    EXPECT_THROW(f.module.InternalsVisibleTo(tiny), std::logic_error);
}

TEST(MetadataModuleTest, GetDefinitionPlumbing)
{
    MscorlibFixture f;
    // The nil token (row 0) returns null.
    EXPECT_EQ(f.module.GetDefinition(0x02000000u), nullptr);
    // The <Module> row (row 1) hits the entity deferral.
    EXPECT_THROW(f.module.GetDefinition(0x02000001u), std::logic_error);
    // The last in-range row (3356 = the full TypeDef row count) still hits the
    // deferral, not the range check.
    EXPECT_THROW(f.module.GetDefinition(0x02000000u + 3356), std::logic_error);
    // A row past the table end throws the exact C# message.
    try
    {
        f.module.GetDefinition(0x02000000u + 4096);
        FAIL() << "GetDefinition past the table end must throw";
    }
    catch (const std::out_of_range& ex)
    {
        EXPECT_STREQ(ex.what(), "Handle with invalid row number.");
    }
}

// ---------------------------------------------------------------------------
// MetadataNamespaceTest: the per-module namespace tree
// ---------------------------------------------------------------------------

TEST(MetadataNamespaceTest, RootNamespaceSurfaceMatchesGold)
{
    MscorlibFixture f;
    const TS::INamespace& root = f.module.RootNamespace();
    EXPECT_EQ(root.FullName(), "");
    EXPECT_EQ(root.Name(), "");
    EXPECT_EQ(root.ParentNamespace(), nullptr);
    EXPECT_EQ(root.ExternAlias(), "");
    EXPECT_EQ(root.SymbolKind(), TS::SymbolKind::Namespace);

    // The single contributing module is the module itself.
    std::vector<const TS::IModule*> contributing = root.ContributingModules();
    ASSERT_EQ(contributing.size(), 1u);
    EXPECT_EQ(contributing[0], &f.module);

    // The compilation routes through the module's compilation.
    EXPECT_EQ(&root.Compilation(), &f.compilation);

    // The ISymbol dispatch through the base reference.
    const TS::ISymbol& symbol = root;
    EXPECT_EQ(symbol.SymbolKind(), TS::SymbolKind::Namespace);
}

TEST(MetadataNamespaceTest, RootChildrenOrderMatchesGold)
{
    MscorlibFixture f;
    // The gold root order: the SRM tree's insertion order (Microsoft, Windows,
    // System -- the synthesized virtual intermediates first).
    EXPECT_EQ(Join(f.module.RootNamespace().ChildNamespaces()),
              "Microsoft,Windows,System");
    // The root's own GetChildNamespace lookups.
    EXPECT_EQ(f.module.RootNamespace().GetChildNamespace("Microsoft")->FullName(),
              "Microsoft");
    EXPECT_EQ(f.module.RootNamespace().GetChildNamespace("Windows")->FullName(),
              "Windows");
    EXPECT_EQ(f.module.RootNamespace().GetChildNamespace("System")->FullName(),
              "System");
    EXPECT_EQ(f.module.RootNamespace().GetChildNamespace("NoSuchNamespace"),
              nullptr);
}

TEST(MetadataNamespaceTest, SystemNamespaceSurfaceMatchesGold)
{
    MscorlibFixture f;
    const TS::INamespace& root = f.module.RootNamespace();
    const TS::INamespace* system = root.GetChildNamespace("System");
    ASSERT_NE(system, nullptr);
    EXPECT_EQ(system->Name(), "System");
    EXPECT_EQ(system->FullName(), "System");
    EXPECT_EQ(system->ParentNamespace(), &root);
    EXPECT_EQ(system->ExternAlias(), "");
    EXPECT_EQ(system->SymbolKind(), TS::SymbolKind::Namespace);
    EXPECT_EQ(&system->Compilation(), &f.compilation);

    // The System children in the gold order.
    EXPECT_EQ(Join(system->ChildNamespaces()),
              "System.Configuration,System.IO,System.Security,System.Numerics,"
              "System.Resources,System.Globalization,System.Diagnostics,"
              "System.Collections,System.Threading,System.StubHelpers,"
              "System.Reflection,System.Deployment,System.Runtime,System.Text");
    EXPECT_EQ(system->GetChildNamespace("Collections")->FullName(),
              "System.Collections");
    EXPECT_EQ(system->GetChildNamespace("NoSuchNamespace"), nullptr);

    // The INamespace lookup miss arms.
    EXPECT_EQ(system->GetTypeDefinition("NoSuchType", 0), nullptr);
    EXPECT_EQ(system->GetTypeDefinition("String", 1), nullptr);
}

TEST(MetadataNamespaceTest, VirtualNamespacesCarryNoDirectTypes)
{
    MscorlibFixture f;
    const TS::INamespace& root = f.module.RootNamespace();

    // The SRM-synthesized virtual intermediates carry no direct types: the
    // empty TypeDefinitions list yields the empty snapshot without reaching
    // the entity deferral (the gold: Microsoft 0 / Windows 0 direct types).
    const TS::INamespace* microsoft = root.GetChildNamespace("Microsoft");
    ASSERT_NE(microsoft, nullptr);
    EXPECT_EQ(microsoft->Name(), "Microsoft");
    EXPECT_EQ(microsoft->FullName(), "Microsoft");
    EXPECT_EQ(microsoft->ParentNamespace(), &root);
    EXPECT_TRUE(microsoft->Types().empty());
    EXPECT_EQ(Join(microsoft->ChildNamespaces()),
              "Microsoft.Win32,Microsoft.Runtime,Microsoft.Reflection");
    EXPECT_EQ(microsoft->GetChildNamespace("Collections"), nullptr);

    const TS::INamespace* windows = root.GetChildNamespace("Windows");
    ASSERT_NE(windows, nullptr);
    EXPECT_EQ(windows->Name(), "Windows");
    EXPECT_EQ(windows->FullName(), "Windows");
    EXPECT_TRUE(windows->Types().empty());
    EXPECT_EQ(Join(windows->ChildNamespaces()), "Windows.Foundation");
}

TEST(MetadataNamespaceTest, DirectTypeNamespaceThrowsTheEntityDeferral)
{
    MscorlibFixture f;
    // The System namespace carries 312 direct types (the gold count): its
    // Types() enumeration routes the first token through GetDefinition, which
    // throws the MetadataTypeDefinition deferral.
    const TS::INamespace* system =
        f.module.RootNamespace().GetChildNamespace("System");
    ASSERT_NE(system, nullptr);
    EXPECT_THROW(system->Types(), std::logic_error);
}

TEST(MetadataNamespaceTest, NestedChainMatchesGold)
{
    MscorlibFixture f;
    const TS::INamespace& root = f.module.RootNamespace();
    const TS::INamespace* system = root.GetChildNamespace("System");
    ASSERT_NE(system, nullptr);
    const TS::INamespace* collections = system->GetChildNamespace("Collections");
    ASSERT_NE(collections, nullptr);
    EXPECT_EQ(collections->Name(), "Collections");
    EXPECT_EQ(collections->FullName(), "System.Collections");
    EXPECT_EQ(collections->ParentNamespace(), system);

    EXPECT_EQ(Join(collections->ChildNamespaces()),
              "System.Collections.Concurrent,System.Collections.ObjectModel,"
              "System.Collections.Generic");
    const TS::INamespace* generic = collections->GetChildNamespace("Generic");
    ASSERT_NE(generic, nullptr);
    EXPECT_EQ(generic->Name(), "Generic");
    EXPECT_EQ(generic->FullName(), "System.Collections.Generic");
    EXPECT_EQ(generic->ParentNamespace(), collections);
    // A real namespace with direct types but no child namespaces.
    EXPECT_TRUE(generic->ChildNamespaces().empty());
}

TEST(MetadataNamespaceTest, ChildCacheIsStable)
{
    MscorlibFixture f;
    // The LazyInit-cached child array: the same child instances across calls
    // (the C# reference-identity of the cached array).
    std::vector<const TS::INamespace*> first =
        f.module.RootNamespace().ChildNamespaces();
    std::vector<const TS::INamespace*> second =
        f.module.RootNamespace().ChildNamespaces();
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); i++)
    {
        EXPECT_EQ(first[i], second[i]);
    }
    // GetChildNamespace resolves through the same cached instances.
    EXPECT_EQ(f.module.RootNamespace().GetChildNamespace("System"), first[2]);
    EXPECT_EQ(f.module.RootNamespace().GetChildNamespace("Microsoft"), first[0]);
}
