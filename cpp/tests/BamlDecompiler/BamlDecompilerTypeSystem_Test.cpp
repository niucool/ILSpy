// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the BamlDecompilerTypeSystem port (ICSharpCode.BamlDecompiler/
// BamlDecompilerTypeSystem.cs) -- every expectation is gold-pinned against
// the REAL class driven over the SAME fixed-map stub IAssemblyResolver and
// the SAME local fixtures by the C:/temp-probe/BdtsProbe probe:
//  * S1 mscorlib main + the full WPF map: the six default references resolve
//    real (no synthetics, no MinimalCorlib), and PresentationFramework's
//    forwarders queue the transitive System.Xaml reference (unresolved --
//    no module appears for it),
//  * S2 mscorlib main + System only: the five unresolved WPF defaults
//    become SyntheticWpfModule stand-ins in the default-reference order,
//  * S3 tiny.netmodule main + the empty map: all seven defaults become
//    synthetics, HasType fails, and MinimalCorlib ("corlib") is appended
//    last -- FindType resolves Void/Int32 through it,
//  * S4 the GAC System.Runtime facade main: the facade's own AssemblyRef
//    rows dedup the mscorlib/System defaults (a single Resolve call each)
//    and the unresolved WPF defaults become synthetics,
//  * S5 the crafted BdtsSynth manifest: the M: module-ref path (the
//    metadata-bearing File match), the ContainsMetadata filter, the
//    unmatched-ModuleRef skip, the duplicate-AssemblyRef dedup, the
//    transitive forwarder skip (the re-queued mscorlib row dedups the
//    default), the transitive AssemblyFile arm (BdtsDep's linked.mod), the
//    resolved-name-based synthetic skip (System.Xml resolved as DepOne's
//    file), and HasType through the referenced mscorlib,
//  * S6 the raw File-table rows (the metadata.AssemblyFiles surface the
//    ModuleRef matching reads: mscorlib's five no-metadata .nlp rows, the
//    crafted manifest's metadata/no-metadata pair),
//  * the WithOptions unit drive: MetadataFile.WithOptions returns the
//    deferred-resolution reference that constructs the MetadataModule for
//    (compilation, file, options) -- the module's identity, options, and
//    per-adapter distinctness through a SimpleCompilation.

#include "BamlDecompiler/BamlDecompilerTypeSystem.hpp"
#include "BamlDecompiler/SyntheticWpfModule.hpp"

#include "TestFixtures/BdtsFixtures.hpp"
#include "TestFixtures/BdtsGold.hpp"
#include "TestFixtures/TinyNetModule.hpp"

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace Metadata = ILSpy::Decompiler::Metadata;
namespace TS = ILSpy::Decompiler::TypeSystem;
using ILSpy::BamlDecompiler::BamlDecompilerTypeSystem;
using ILSpy::BamlDecompiler::SyntheticWpfModule;

// The .NET Framework 4.8 reference assemblies (the probe's fixed paths).
constexpr const char* Fx = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319";
constexpr const char* Wpf = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\WPF";
constexpr const char* Facade =
    "C:\\Windows\\Microsoft.NET\\assembly\\GAC_MSIL\\System.Runtime\\"
    "v4.0_4.0.0.0__b03f5f7f11d50a3a\\System.Runtime.dll";

// The probe's FixedResolver: a case-insensitive name -> MetadataFile map
// plus the call log (the port's IAssemblyResolver has no async pair -- the
// C# stub's async members just delegate to the sync ones and are never
// driven by the ctor).
class FixedResolver final : public Metadata::IAssemblyResolver {
public:
    void Add(const std::string& name, const std::string& path) {
        for (auto& entry : files_) {
            if (ignoreCase_.Equals(entry.first, name)) {
                entry.second = std::make_unique<Metadata::MetadataFile>(path);
                return;
            }
        }
        files_.emplace_back(name,
            std::make_unique<Metadata::MetadataFile>(path));
    }

    const Metadata::MetadataFile* Resolve(
        const Metadata::IAssemblyReference& reference) const override {
        calls_.push_back("Resolve|" + reference.FullName());
        return Lookup(reference.Name());
    }

    const Metadata::MetadataFile* ResolveModule(
        const Metadata::MetadataFile&, const std::string& moduleName)
        const override {
        calls_.push_back("ResolveModule|" + moduleName);
        return Lookup(moduleName);
    }

    const std::vector<std::string>& Calls() const { return calls_; }

private:
    const Metadata::MetadataFile* Lookup(const std::string& name) const {
        for (const auto& entry : files_) {
            if (ignoreCase_.Equals(entry.first, name))
                return entry.second.get();
        }
        return nullptr;
    }

    const TS::StringComparer& ignoreCase_ = TS::StringComparer::OrdinalIgnoreCase();
    std::vector<std::pair<std::string,
        std::unique_ptr<Metadata::MetadataFile>>> files_;
    mutable std::vector<std::string> calls_;
};

// The probe's Dump drive: the resolver call log, the module list, the typed
// MainModule identity, and the FindType results, rendered as the exact
// lines the gold blocks carry.
template <std::size_t N>
void DriveAndCompare(const std::string& mainPath,
                    const std::function<void(FixedResolver&)>& configure,
                    const std::array<const char*, N>& gold) {
    FixedResolver resolver;
    configure(resolver);
    Metadata::MetadataFile mainModule(mainPath);
    BamlDecompilerTypeSystem typeSystem(mainModule, resolver);

    std::vector<std::string> lines;
    for (const std::string& call : resolver.Calls())
        lines.push_back("RESOLVE|" + call);
    std::vector<const TS::IModule*> modules = typeSystem.Modules();
    lines.push_back("MODS|" + std::to_string(modules.size()));
    for (std::size_t i = 0; i < modules.size(); i++) {
        const TS::IModule* module = modules[i];
        std::string typeName = "MetadataModule";
        if (dynamic_cast<const SyntheticWpfModule*>(module))
            typeName = "SyntheticWpfModule";
        else if (dynamic_cast<
                     const TS::Implementation::MinimalCorlib*>(module))
            typeName = "MinimalCorlib";
        lines.push_back("MOD|" + std::to_string(i) + "|Name=" + module->Name()
            + "|Asm=" + module->AssemblyName()
            + "|Full=" + module->FullAssemblyName()
            + "|Type=" + typeName);
    }
    const TS::MetadataModule& typedMain = typeSystem.MainModule();
    lines.push_back(std::string("MAIN|Type=MetadataModule")
        + "|SameAsMods0="
        + ((&typedMain == modules[0]) ? "True" : "False")
        + "|IsMainModule=" + (typedMain.IsMainModule() ? "True" : "False"));
    lines.push_back("FIND|Void|" + typeSystem.FindType(TS::KnownTypeCode::Void).ReflectionName());
    lines.push_back("FIND|Int32|" + typeSystem.FindType(TS::KnownTypeCode::Int32).ReflectionName());

    ASSERT_EQ(lines.size(), gold.size());
    for (std::size_t i = 0; i < lines.size(); i++)
        EXPECT_EQ(lines[i], gold[i]) << "gold line " << i;
}

TEST(BamlDecompilerTypeSystemTest, FullWpfMapResolvesAllDefaultsReal) {
    DriveAndCompare(std::string(Fx) + "\\mscorlib.dll", [](FixedResolver& r) {
        r.Add("System", std::string(Fx) + "\\System.dll");
        r.Add("WindowsBase", std::string(Wpf) + "\\WindowsBase.dll");
        r.Add("PresentationCore", std::string(Wpf) + "\\PresentationCore.dll");
        r.Add("PresentationFramework", std::string(Wpf) + "\\PresentationFramework.dll");
        r.Add("PresentationUI", std::string(Wpf) + "\\PresentationUI.dll");
        r.Add("System.Xml", std::string(Fx) + "\\System.XML.dll");
    }, ILSpy::Tests::kBdtsGold_S1);
}

TEST(BamlDecompilerTypeSystemTest, SystemOnlyMapSubstitutesWpfSynthetics) {
    DriveAndCompare(std::string(Fx) + "\\mscorlib.dll", [](FixedResolver& r) {
        r.Add("System", std::string(Fx) + "\\System.dll");
    }, ILSpy::Tests::kBdtsGold_S2);
}

TEST(BamlDecompilerTypeSystemTest, EmptyMapFallsBackToMinimalCorlib) {
    DriveAndCompare(WriteTinyNetModule(), [](FixedResolver&) {
    }, ILSpy::Tests::kBdtsGold_S3);
}

TEST(BamlDecompilerTypeSystemTest, FacadeDedupsOwnAssemblyRefsWithDefaults) {
    DriveAndCompare(Facade, [](FixedResolver& r) {
        r.Add("mscorlib", std::string(Fx) + "\\mscorlib.dll");
        r.Add("System", std::string(Fx) + "\\System.dll");
        r.Add("System.Core", std::string(Fx) + "\\System.Core.dll");
    }, ILSpy::Tests::kBdtsGold_S4);
}

TEST(BamlDecompilerTypeSystemTest, CraftedManifestDrivesEveryQueueArm) {
    DriveAndCompare(ILSpy::Tests::WriteBdtsSynthDll(), [](FixedResolver& r) {
        r.Add("DepOne", std::string(Fx) + "\\System.XML.dll");
        r.Add("DepTwo", ILSpy::Tests::WriteBdtsDepDll());
        r.Add("sub.mod", std::string(Fx) + "\\System.dll");
        r.Add("linked.mod", std::string(Fx) + "\\mscorlib.dll");
    }, ILSpy::Tests::kBdtsGold_S5);
}

TEST(BamlDecompilerTypeSystemTest, FileTableRowsMatchGold) {
    // The S6 gold: mscorlib's five .nlp rows are resource files (no
    // metadata), the crafted manifest's sub.mod carries metadata and
    // nometa.mod does not.
    Metadata::MetadataFile mscorlib(std::string(Fx) + "\\mscorlib.dll");
    auto rows = mscorlib.GetAssemblyFiles();
    ASSERT_EQ(rows.size(), 5u);
    const std::array<const char*, 7> gold = ILSpy::Tests::kBdtsGold_S6;
    for (std::size_t i = 0; i < 5; i++) {
        EXPECT_EQ("FILE|mscorlib|" + rows[i].Name + "|"
                + (rows[i].ContainsMetadata ? "True" : "False"),
            gold[i]);
    }

    Metadata::MetadataFile synth(ILSpy::Tests::WriteBdtsSynthDll());
    rows = synth.GetAssemblyFiles();
    ASSERT_EQ(rows.size(), 2u);
    for (std::size_t i = 0; i < 2; i++) {
        EXPECT_EQ("FILE|BdtsSynth|" + rows[i].Name + "|"
                + (rows[i].ContainsMetadata ? "True" : "False"),
            gold[5 + i]);
    }
}

TEST(BamlDecompilerTypeSystemTest, WithOptionsResolvesToTheMetadataModule) {
    // MetadataFile.WithOptions returns the deferred-resolution reference:
    // a SimpleCompilation over it resolves the reference through its
    // SimpleTypeResolveContext and the adapter constructs the MetadataModule
    // for (compilation, file, options).
    Metadata::MetadataFile file(std::string(Fx) + "\\mscorlib.dll");
    std::unique_ptr<TS::IModuleReference> reference =
        file.WithOptions(TS::TypeSystemOptions::Default);
    TS::SimpleCompilation compilation(*reference, {});

    const TS::IModule& module = compilation.MainModule();
    const auto* metadataModule = dynamic_cast<const TS::MetadataModule*>(&module);
    ASSERT_NE(metadataModule, nullptr);
    EXPECT_EQ(metadataModule->Name(), "mscorlib");
    EXPECT_EQ(metadataModule->FullAssemblyName(),
        "mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089");
    EXPECT_EQ(metadataModule->TypeSystemOptions(), TS::TypeSystemOptions::Default);
    EXPECT_EQ(metadataModule->MetadataFile(), &file);
    EXPECT_TRUE(metadataModule->IsMainModule());
}

TEST(BamlDecompilerTypeSystemTest, WithOptionsAdaptersCreateDistinctModules) {
    // Every WithOptions call returns a distinct reference, and each
    // reference constructs its own MetadataModule (the C# `new MetadataModule`
    // per Resolve -- the port's adapter owns every module it creates).
    Metadata::MetadataFile file(std::string(Fx) + "\\mscorlib.dll");
    auto referenceA = file.WithOptions(TS::TypeSystemOptions::Default);
    auto referenceB = file.WithOptions(TS::TypeSystemOptions::Default);
    TS::SimpleCompilation compilationA(*referenceA, {});
    TS::SimpleCompilation compilationB(*referenceB, {});
    EXPECT_NE(&compilationA.MainModule(), &compilationB.MainModule());
    EXPECT_EQ(compilationA.MainModule().Name(), compilationB.MainModule().Name());
}

} // namespace
