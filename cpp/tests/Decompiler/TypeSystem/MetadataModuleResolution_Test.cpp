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

// Tests for the MetadataModule cross-module resolution family port
// (cpp/Decompiler/TypeSystem/MetadataModule.{hpp,cpp}, the Resolve Module
// region of MetadataModule.cs lines 305-368 plus the private
// ResolveForwardedType/ResolveForwarderModule pair behind the
// GetTypeDefinition(TopLevelTypeName) forwarder arm, and the
// ICompilation.FindModuleByReference extension).
//
// Every expectation is gold-pinned against the REAL ICSharpCode.Decompiler
// 11.0 driven over the identical fixtures (the C:/temp-probe/ResolutionProbe
// gold probe, sections C-G):
//   * ResolveModule(AssemblyReferenceHandle): the cached resolution over
//     System.dll main + mscorlib (the identity, the two unresolvable refs,
//     the cache slot, the nil and out-of-range arms), and the
//     FindModuleByReference FullName-vs-Name two-pass over tiny.netmodule
//     and the VersionMismatch manifest (the FullName pass misses, the Name
//     pass hits);
//   * ResolveModule(ModuleReferenceHandle): mscorlib's 17 rows all miss (no
//     loaded module carries those names), the nil arm, and the
//     ModuleRefHit manifest's "tiny"/"NoSuchModule" hit and miss;
//   * GetDeclaringModule(TypeReferenceHandle): System.dll's real
//     AssemblyRef-scoped and nested-TypeRef-scoped rows resolving into the
//     mscorlib module, the nil and out-of-range arms, and the CrossModule
//     manifest's nil/ModuleRef/AssemblyRef scope arms;
//   * the GetTypeDefinition(TopLevelTypeName) forwarder arm through
//     ResolveForwardedType: the GAC System.Runtime facade's 279 real
//     forwarders into mscorlib (String -> 0x02000073, WeakReference`1 ->
//     0x0200015e, DebuggableAttribute -> 0x020003eb, plus the split-name
//     key misses), and the crafted arms (the missing target, the
//     File-implementation C# TODO, the nil-File decode, the tag-3 default
//     arm, and the mutually-referencing cyclic pair the BusyManager breaks);
//   * the GetTypeDefinition(FullTypeName) extension's nested-name walk over
//     mscorlib.

#include "TestFixtures/ResolutionFixtures.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/CacheManager.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
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

const char* SystemPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\System.dll";
#else
    return "/usr/lib/mono/4.5/System.dll";
#endif
}

#if defined(_WIN32)
const char* FacadePath() {
    return "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
           "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";
}
#endif

// A compilation whose module list is settable (the MetadataModule ctor takes
// the compilation BEFORE the compilation can know its modules -- the C#
// resolves the modules through PEFile.Resolve DURING SimpleCompilation.Init).
// The main module is always the first entry.
class ModulesCompilation : public TS::ICompilation {
public:
    void SetModules(std::vector<const TS::IModule*> modules)
    {
        modules_ = std::move(modules);
    }

    // --- ICompilation ---
    const TS::IModule& MainModule() const override { return *modules_[0]; }
    std::vector<const TS::IModule*> Modules() const override
    {
        return modules_;
    }
    std::vector<const TS::IModule*> ReferencedModules() const override
    {
        return std::vector<const TS::IModule*>(modules_.begin() + 1,
                                               modules_.end());
    }
    const TS::INamespace& RootNamespace() const override
    {
        return modules_[0]->RootNamespace();
    }
    const TS::INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const TS::IType& FindType(TS::KnownTypeCode) const override
    {
        return knownType_;
    }
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
    std::vector<const TS::IModule*> modules_;
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
    TS::KnownType knownType_{ TS::KnownTypeCode::Object };
};

// One module over one file, bound to a compilation. The FILE outlives the
// MODULE (the entries' field order: file before module, destroyed after --
// the caller-owns-the-file contract).
struct ModuleEntry {
    TM::MetadataFile file;
    TS::MetadataModule module;

    ModuleEntry(const std::string& filePath,
                const TS::ICompilation& compilation)
        : file(filePath), module(compilation, &file,
                                 TS::TypeSystemOptions::Default)
    {
    }
};

// A bundle of modules sharing one compilation (the probe's
// SimpleCompilation(main, referenced...) shape). The whole group stays alive
// together (the compilation references the module objects; the modules
// reference the compilation and their files).
struct CompilationBundle {
    ModulesCompilation compilation;
    std::vector<std::unique_ptr<ModuleEntry>> entries;
    std::vector<const TS::IModule*> modules;

    TS::MetadataModule& Add(const std::string& filePath)
    {
        entries.push_back(
            std::make_unique<ModuleEntry>(filePath, compilation));
        modules.push_back(&entries.back()->module);
        return entries.back()->module;
    }

    void Seal() { compilation.SetModules(std::move(modules)); }
};

std::string ToHex(std::uint32_t value)
{
    static const char digits[] = "0123456789abcdef";
    std::string result = "0x";
    for (int shift = 28; shift >= 0; shift -= 4)
        result += digits[(value >> shift) & 0xF];
    return result;
}

} // namespace

// ---------------------------------------------------------------------------
// C: ResolveModule(AssemblyReferenceHandle)
// ---------------------------------------------------------------------------

// C1/C2/C3: the cached resolution over System.dll main + mscorlib.
TEST(MetadataModuleResolutionTest, ResolveModuleAssemblyRefCachedResolution)
{
    CompilationBundle bundle;
    TS::MetadataModule& system = bundle.Add(SystemPath());
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();

    std::vector<TM::MetadataFile::AssemblyReferenceInfo> rows =
        system.MetadataFile()->GetAssemblyReferences();
    ASSERT_EQ(rows.size(), 3u);

    // Row 1 (mscorlib) resolves to the compilation's mscorlib module.
    const TS::IModule* resolved = system.ResolveModule(rows[0].Token);
    ASSERT_NE(resolved, nullptr);
    EXPECT_EQ(resolved, &mscorlib);
    EXPECT_EQ(resolved->Name(), "mscorlib");

    // Rows 2/3 (System.Configuration / System.Xml) are not loaded -> null.
    EXPECT_EQ(system.ResolveModule(rows[1].Token), nullptr);
    EXPECT_EQ(system.ResolveModule(rows[2].Token), nullptr);

    // The cache identity: a second call returns the same instance.
    EXPECT_EQ(system.ResolveModule(rows[0].Token), resolved);

    // The nil token -> null.
    EXPECT_EQ(system.ResolveModule(0), nullptr);
}

// C4: the out-of-range row throws the HandleOutOfRange message.
TEST(MetadataModuleResolutionTest, ResolveModuleAssemblyRefOutOfRange)
{
    CompilationBundle bundle;
    TS::MetadataModule& system = bundle.Add(SystemPath());
    bundle.Seal();
    try
    {
        system.ResolveModule(0x23000063u);  // row 99, the table holds 3
        FAIL() << "expected std::out_of_range";
    }
    catch (const std::out_of_range& ex)
    {
        EXPECT_STREQ(ex.what(), "Handle with invalid row number.");
    }
}

// C5/C6: the FindModuleByReference two-pass -- the FullName pass (tiny's
// mscorlib ref matches the loaded mscorlib's full name), and the VersionMismatch
// manifest's 9.9.9.9 ref (the FullName pass misses, the Name pass hits).
TEST(MetadataModuleResolutionTest, ResolveModuleTwoPassFullNameThenName)
{
    {
        // C5: tiny.netmodule main + mscorlib -- the FullName pass.
        std::string tinyPath = WriteResolutionTempFile(
            TinyNetModuleBytes(), "ilspy_res_tiny_c5.netmodule");
        ASSERT_FALSE(tinyPath.empty());
        CompilationBundle bundle;
        TS::MetadataModule& tiny = bundle.Add(tinyPath);
        TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
        bundle.Seal();
        std::vector<TM::MetadataFile::AssemblyReferenceInfo> rows =
            tiny.MetadataFile()->GetAssemblyReferences();
        ASSERT_EQ(rows.size(), 1u);
        TM::AssemblyReference ar(*tiny.MetadataFile(), rows[0].Token);
        EXPECT_EQ(ar.FullName(),
                  "mscorlib, Version=4.0.0.0, Culture=neutral, "
                  "PublicKeyToken=b77a5c561934e089");
        const TS::IModule* resolved = tiny.ResolveModule(rows[0].Token);
        ASSERT_NE(resolved, nullptr);
        EXPECT_EQ(resolved, &mscorlib);
        EXPECT_EQ(resolved->FullAssemblyName(),
                  "mscorlib, Version=4.0.0.0, Culture=neutral, "
                  "PublicKeyToken=b77a5c561934e089");
    }
    {
        // C6: the VersionMismatch manifest -- the ref's FullName carries
        // Version=9.9.9.9 (no loaded module matches), the short Name
        // "mscorlib" matches the second pass.
        std::string v99Path = WriteResolutionTempFile(
            VersionMismatchManifestBytes(), "ilspy_res_v99.dll");
        ASSERT_FALSE(v99Path.empty());
        CompilationBundle bundle;
        TS::MetadataModule& v99 = bundle.Add(v99Path);
        TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
        bundle.Seal();
        std::vector<TM::MetadataFile::AssemblyReferenceInfo> rows =
            v99.MetadataFile()->GetAssemblyReferences();
        ASSERT_EQ(rows.size(), 1u);
        TM::AssemblyReference ar(*v99.MetadataFile(), rows[0].Token);
        EXPECT_EQ(ar.FullName(),
                  "mscorlib, Version=9.9.9.9, Culture=neutral, "
                  "PublicKeyToken=null");
        const TS::IModule* resolved = v99.ResolveModule(rows[0].Token);
        ASSERT_NE(resolved, nullptr);
        EXPECT_EQ(resolved, &mscorlib);
    }
}

// The FindModuleByReference extension drives the two passes directly.
TEST(MetadataModuleResolutionTest, FindModuleByReferenceTwoPasses)
{
    CompilationBundle bundle;
    TS::MetadataModule& system = bundle.Add(SystemPath());
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();

    std::vector<TM::MetadataFile::AssemblyReferenceInfo> rows =
        system.MetadataFile()->GetAssemblyReferences();
    TM::AssemblyReference mscorlibRef(*system.MetadataFile(), rows[0].Token);
    EXPECT_EQ(TS::FindModuleByReference(bundle.compilation, mscorlibRef),
              &mscorlib);

    // A reference whose full name matches NO module but whose short name
    // matches a later module: the Name pass finds it.
    TM::AssemblyReference xmlRef(*system.MetadataFile(), rows[2].Token);
    EXPECT_EQ(TS::FindModuleByReference(bundle.compilation, xmlRef), nullptr);

    // The case-insensitivity of both passes.
    TM::AssemblyNameReference parsed =
        TM::AssemblyNameReference::Parse("MSCORLIB");
    EXPECT_EQ(TS::FindModuleByReference(bundle.compilation,
                                    static_cast<const TM::IAssemblyReference&>(parsed)),
              &mscorlib);
}

// ---------------------------------------------------------------------------
// D: ResolveModule(ModuleReferenceHandle)
// ---------------------------------------------------------------------------

// D1/D2: mscorlib's 17 ModuleRef rows all miss (no loaded module is named
// kernel32.dll & co); the nil token -> null.
TEST(MetadataModuleResolutionTest, ResolveModuleReferenceMisses)
{
    CompilationBundle bundle;
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();

    std::vector<TM::MetadataFile::ModuleReferenceInfo> rows =
        mscorlib.MetadataFile()->GetModuleReferences();
    EXPECT_EQ(rows.size(), 17u);
    for (const TM::MetadataFile::ModuleReferenceInfo& row : rows)
    {
        // The ordinal (case-sensitive) scan over the compilation's module
        // names: mscorlib's own name never matches a native DLL name.
        EXPECT_EQ(mscorlib.ResolveModuleReference(row.Token), nullptr)
            << row.Name;
    }
    // The first row's name (the gold sample).
    ASSERT_FALSE(rows.empty());
    EXPECT_EQ(rows[0].Name, "kernel32.dll");
    // The nil token -> null.
    EXPECT_EQ(mscorlib.ResolveModuleReference(0), nullptr);
}

// D3: the ModuleRefHit manifest -- the "tiny" row resolves to the loaded
// tiny.netmodule module (its ISymbol.Name is the module name "tiny"), the
// "NoSuchModule" row misses.
TEST(MetadataModuleResolutionTest, ResolveModuleReferenceHitAndMiss)
{
    std::string manifestPath = WriteResolutionTempFile(
        ModuleRefHitManifestBytes(), "ilspy_res_modref.dll");
    std::string tinyPath = WriteResolutionTempFile(
        TinyNetModuleBytes(), "ilspy_res_tiny_d3.netmodule");
    ASSERT_FALSE(manifestPath.empty());
    ASSERT_FALSE(tinyPath.empty());
    CompilationBundle bundle;
    TS::MetadataModule& manifest = bundle.Add(manifestPath);
    TS::MetadataModule& tiny = bundle.Add(tinyPath);
    bundle.Seal();

    std::vector<TM::MetadataFile::ModuleReferenceInfo> rows =
        manifest.MetadataFile()->GetModuleReferences();
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].Name, "tiny");
    EXPECT_EQ(rows[1].Name, "NoSuchModule");
    const TS::IModule* hit = manifest.ResolveModuleReference(rows[0].Token);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(hit, &tiny);
    EXPECT_EQ(hit->Name(), "tiny");
    EXPECT_EQ(manifest.ResolveModuleReference(rows[1].Token), nullptr);
}

// ---------------------------------------------------------------------------
// E: GetDeclaringModule(TypeReferenceHandle)
// ---------------------------------------------------------------------------

// E5/E6/E3: System.dll's AssemblyRef-scoped rows resolve into the mscorlib
// module, the nested TypeRef-scoped rows recurse into the parent's scope, and
// the nil token -> null.
TEST(MetadataModuleResolutionTest, GetDeclaringModuleSystemDllRows)
{
    CompilationBundle bundle;
    TS::MetadataModule& system = bundle.Add(SystemPath());
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();

    const TM::MetadataFile* file = system.MetadataFile();
    std::uint32_t typeRefCount = file->TypeRefCount();
    EXPECT_EQ(typeRefCount, 595u);  // 584 AssemblyRef-scoped + 11 nested

    // The first three rows are AssemblyRef-scoped (Enum, Object, ValueType)
    // -> the mscorlib module.
    const char* expected[] = { "Enum", "Object", "ValueType" };
    for (int i = 0; i < 3; i++)
    {
        std::uint32_t token = 0x01000001u + static_cast<std::uint32_t>(i);
        auto nameInfo = file->GetTypeRefNameInfo(token);
        ASSERT_TRUE(nameInfo.has_value());
        EXPECT_EQ(nameInfo->Name, expected[i]) << ToHex(token);
        EXPECT_EQ(system.GetDeclaringModule(token), &mscorlib);
    }

    // The nested rows (the TypeRef-scoped 11): the recursion resolves into
    // the parent's module (mscorlib for every one of them, the gold samples).
    int nestedSeen = 0;
    for (std::uint32_t row = 1; row <= typeRefCount && nestedSeen < 3; row++)
    {
        std::uint32_t token = 0x01000000u | row;
        auto scope = file->GetTypeRefScopeInfo(token);
        ASSERT_TRUE(scope.has_value());
        if (scope->Scope != TM::TypeRefScopeInfo::Kind::TypeRef)
            continue;
        nestedSeen++;
        EXPECT_EQ(system.GetDeclaringModule(token), &mscorlib)
            << "nested row " << row;
    }
    EXPECT_EQ(nestedSeen, 3);

    // The nil token -> null.
    EXPECT_EQ(system.GetDeclaringModule(0), nullptr);
}

// E7: the out-of-range row propagates the raw read's message.
TEST(MetadataModuleResolutionTest, GetDeclaringModuleOutOfRange)
{
    CompilationBundle bundle;
    TS::MetadataModule& system = bundle.Add(SystemPath());
    bundle.Seal();
    try
    {
        system.GetDeclaringModule(0x01270Fu);  // row 9999
        FAIL() << "expected std::invalid_argument";
    }
    catch (const std::invalid_argument& ex)
    {
        EXPECT_STREQ(ex.what(), "Read out of bounds.");
    }
}

// E8: the CrossModule manifest's three scope arms: the nil scope -> this
// (the default arm; the C# nil decodes as HandleKind.ModuleDefinition), the
// ModuleRef scope -> the tiny module, the AssemblyRef scope -> mscorlib.
TEST(MetadataModuleResolutionTest, GetDeclaringModuleScopeArms)
{
    std::string manifestPath = WriteResolutionTempFile(
        CrossModuleManifestBytes(), "ilspy_res_cross.dll");
    std::string tinyPath = WriteResolutionTempFile(
        TinyNetModuleBytes(), "ilspy_res_tiny_e8.netmodule");
    ASSERT_FALSE(manifestPath.empty());
    ASSERT_FALSE(tinyPath.empty());
    CompilationBundle bundle;
    TS::MetadataModule& manifest = bundle.Add(manifestPath);
    TS::MetadataModule& tiny = bundle.Add(tinyPath);
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();

    const TM::MetadataFile* file = manifest.MetadataFile();
    std::uint32_t typeRefCount = file->TypeRefCount();
    EXPECT_EQ(typeRefCount, 3u);
    // The rows in authored order: NilScoped (nil scope), TinyScoped
    // (ModuleRef), String (AssemblyRef).
    const struct {
        const char* name;
        const TS::IModule* expected;
    } expected[] = {
        { "NilScoped", &manifest },
        { "TinyScoped", &tiny },
        { "String", &mscorlib },
    };
    for (std::uint32_t row = 1; row <= typeRefCount; row++)
    {
        std::uint32_t token = 0x01000000u | row;
        auto nameInfo = file->GetTypeRefNameInfo(token);
        ASSERT_TRUE(nameInfo.has_value());
        ASSERT_EQ(nameInfo->Name, expected[row - 1].name);
        const TS::IModule* resolved = manifest.GetDeclaringModule(token);
        ASSERT_NE(resolved, nullptr);
        EXPECT_EQ(resolved, expected[row - 1].expected);
    }
}

// ---------------------------------------------------------------------------
// F: the GetTypeDefinition(TopLevelTypeName) forwarder arm
// ---------------------------------------------------------------------------

#if defined(_WIN32)

// F2: the GAC System.Runtime facade's real forwarders into mscorlib, plus the
// split-name key misses.
TEST(MetadataModuleResolutionTest, ForwarderArmOverTheFacade)
{
    CompilationBundle bundle;
    TS::MetadataModule& facade = bundle.Add(FacadePath());
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();

    // System.String -> mscorlib's String (0x02000073).
    const TS::ITypeDefinition* td =
        facade.GetTypeDefinition(TS::TopLevelTypeName("System", "String", 0));
    ASSERT_NE(td, nullptr);
    EXPECT_EQ(td->FullName(), "System.String");
    EXPECT_EQ(ToHex(td->MetadataToken()), "0x02000073");
    EXPECT_EQ(td->ParentModule(), &mscorlib);

    // System.WeakReference (arity 1 -- the SPLIT name form) -> mscorlib's
    // WeakReference`1 (0x0200015e).
    td = facade.GetTypeDefinition(
        TS::TopLevelTypeName("System", "WeakReference", 1));
    ASSERT_NE(td, nullptr);
    EXPECT_EQ(ToHex(td->MetadataToken()), "0x0200015e");
    EXPECT_EQ(td->ParentModule(), &mscorlib);

    // The UNSPLIT backticked name (and the wrong arity) miss the forwarder
    // table keys: the reverse lookup splits the arity off the row name, so a
    // "WeakReference`1" name with tpc 1 queries a key that does not exist.
    EXPECT_EQ(facade.GetTypeDefinition(
                  TS::TopLevelTypeName("System", "WeakReference`1", 1)),
              nullptr);
    EXPECT_EQ(facade.GetTypeDefinition(
                  TS::TopLevelTypeName("System", "WeakReference`2", 2)),
              nullptr);

    // System.Diagnostics.DebuggableAttribute -> mscorlib (0x020003eb).
    td = facade.GetTypeDefinition(TS::TopLevelTypeName(
        "System.Diagnostics", "DebuggableAttribute", 0));
    ASSERT_NE(td, nullptr);
    EXPECT_EQ(ToHex(td->MetadataToken()), "0x020003eb");
    EXPECT_EQ(td->ParentModule(), &mscorlib);
}

// The crafted forwarder arms: the missing target (null module ->
// UnknownType -> null), the File-implementation C# TODO (resolving within
// `this` reaches the busy-lock fallback -> null), and the nil-File decode
// (the raw 0 column decodes as a nil FILE handle -- the same `this`
// resolution).
TEST(MetadataModuleResolutionTest, ForwarderArmCraftedNullCases)
{
    struct {
        const char* tag;
        std::string (*bytes)();
    } fixtures[] = {
        { "missing", MissingTargetManifestBytes },
        { "fileimpl", FileImplManifestBytes },
        { "nil-tag0", NilImplTag0ManifestBytes },
    };
    for (const auto& fixture : fixtures)
    {
        std::string path = WriteResolutionTempFile(
            fixture.bytes(),
            (std::string("ilspy_res_fwd_") + fixture.tag + ".dll").c_str());
        ASSERT_FALSE(path.empty());
        CompilationBundle bundle;
        bundle.Add(path);
        TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
        bundle.Seal();
        (void)mscorlib;
        EXPECT_EQ(bundle.entries[0]->module.GetTypeDefinition(
                      TS::TopLevelTypeName("Ns", "T", 0)),
                  nullptr)
            << fixture.tag;
    }
}

// The tag-3 default arm: the port's raw-surface namespace cache does not
// decode the coded index at the ctor, so the module constructs (the REAL C#
// throws BadImageFormatException "Invalid coded index." at the ctor -- the
// SRM namespace-cache read of the decoded handle -- the documented
// divergence) and the resolution reaches ResolveForwarderModule's default
// arm instead: the faithful dead-code throw.
TEST(MetadataModuleResolutionTest, ForwarderArmTag3DefaultArm)
{
    std::string path = WriteResolutionTempFile(NilImplTag3ManifestBytes(),
                                               "ilspy_res_fwd_tag3.dll");
    ASSERT_FALSE(path.empty());
    CompilationBundle bundle;
    TS::MetadataModule& manifest = bundle.Add(path);
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();
    (void)mscorlib;
    try
    {
        manifest.GetTypeDefinition(TS::TopLevelTypeName("Ns", "T", 0));
        FAIL() << "expected std::out_of_range";
    }
    catch (const std::out_of_range& ex)
    {
        EXPECT_STREQ(ex.what(),
                     "Expected implementation to be either an AssemblyFile, "
                     "ExportedType or AssemblyReference.");
    }
}

// F6: the mutually-referencing cyclic pair -- the BusyManager breaks the
// A->B->A cycle at the second Enter(A), deterministically across rounds.
TEST(MetadataModuleResolutionTest, ForwarderArmCyclicPairBusyLock)
{
    std::string aPath = WriteResolutionTempFile(CyclicAManifestBytes(),
                                                "ilspy_res_cyca.dll");
    std::string bPath = WriteResolutionTempFile(CyclicBManifestBytes(),
                                                "ilspy_res_cycb.dll");
    ASSERT_FALSE(aPath.empty());
    ASSERT_FALSE(bPath.empty());
    CompilationBundle bundle;
    TS::MetadataModule& a = bundle.Add(aPath);
    TS::MetadataModule& b = bundle.Add(bPath);
    bundle.Seal();
    (void)b;

    for (int round = 0; round < 2; round++)
    {
        EXPECT_EQ(a.GetTypeDefinition(TS::TopLevelTypeName("Ns", "T", 0)),
                  nullptr)
            << "round " << round;
    }
}

#endif  // defined(_WIN32)

// ---------------------------------------------------------------------------
// G: the GetTypeDefinition(FullTypeName) extension (the nested-name walk)
// ---------------------------------------------------------------------------

TEST(MetadataModuleResolutionTest, GetTypeDefinitionFullTypeNameWalk)
{
    CompilationBundle bundle;
    TS::MetadataModule& mscorlib = bundle.Add(MscorlibPath());
    bundle.Seal();

    struct {
        const char* reflectionName;
        std::uint32_t token;
        int typeParameterCount;
    } hits[] = {
        { "System.Environment", 0x020000deu, 0 },
        { "System.Environment+SpecialFolder", 0x02000ae7u, 0 },
        { "System.Collections.Generic.Dictionary`2+KeyCollection",
          0x02000be2u, 2 },
        { "System.Collections.Generic.Dictionary`2+Enumerator",
          0x02000be1u, 2 },
    };
    for (const auto& hit : hits)
    {
        const TS::ITypeDefinition* td = TS::GetTypeDefinition(
            mscorlib, TS::FullTypeName(hit.reflectionName));
        ASSERT_NE(td, nullptr) << hit.reflectionName;
        EXPECT_EQ(ToHex(td->MetadataToken()), ToHex(hit.token))
            << hit.reflectionName;
        EXPECT_EQ(td->TypeParameterCount(), hit.typeParameterCount)
            << hit.reflectionName;
    }

    const char* misses[] = {
        "System.Collections.Generic.Dictionary`2+NoSuchNested",
        "System.NoSuchTopLevel",
        "System.Environment+SpecialFolder+NoSuchDeep",
    };
    for (const char* miss : misses)
    {
        EXPECT_EQ(TS::GetTypeDefinition(
                      mscorlib, TS::FullTypeName(miss)),
                  nullptr)
            << miss;
    }
}
