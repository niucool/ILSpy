// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The MetadataTypeDefinition Methods/GetMethods/IsRecord test suite
// (gold-pinned against the real
// ICSharpCode.Decompiler 11.0 via the C:/temp-probe/MmEnumProbe gold probe):
//   * Section A (SWEEP): the Methods() enumeration over EVERY TypeDef row of
//     mscorlib 4.8 + System.dll + .NET 10 CoreLib -- the whole-corpus
//     FNV-1a-64 digests over the per-type headers and the per-method renders
//     (the concrete class, the kind, the name, the accessibility, the flag
//     matrix, the decoded parameters + return type), the per-file counts,
//     and the dummy-constructor census (the FakeMethod rows the enumeration
//     appends to structs/enums without a parameterless constructor);
//   * Section B (RECORD): the IsRecord whole-corpus digests (all-false over
//     the three BCL corpora) plus the REAL record fixtures -- the nine
//     IsRecord=true types of the Roslyn Microsoft.CodeAnalysis.dll shipped
//     in the SDK (glob-gated like the analyzer-PDB fixture);
//   * Section E (GETMETHODS): the curated GetMethods arm drives (the Void
//     early exit, the IgnoreInheritedMembers declared arm, the inherited
//     walk, the typeArguments overload, the accessor-dropping census).

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataMethod.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;
namespace TI = ILSpy::Decompiler::TypeSystem::Implementation;

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

const char* CoreLibPath() {
#if defined(_WIN32)
    return "C:\\Program Files\\dotnet\\shared\\Microsoft.NETCore.App\\10.0.8\\"
           "System.Private.CoreLib.dll";
#else
    return "";
#endif
}

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

bool MscorlibAvailable() { return FileExists(MscorlibPath()); }
bool SystemAvailable() { return FileExists(SystemPath()); }
bool CoreLibAvailable() { return FileExists(CoreLibPath()); }

// The Roslyn Microsoft.CodeAnalysis.dll shipped in the SDK (the newest
// installed SDK's bincore copy -- the real-record fixture, glob-gated like
// the analyzer-PDB fixture).
std::string RoslynPath() {
    namespace fs = std::filesystem;
    const char* root = "C:\\Program Files\\dotnet\\sdk";
    std::error_code ec;
    std::string best;
    for (fs::directory_iterator it(root, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::string candidate
            = it->path().string() + "\\Roslyn\\bincore\\Microsoft.CodeAnalysis.dll";
        if (fs::exists(candidate, ec))
            best = candidate;
    }
    return best;
}

class FixedModuleRef : public TS::IModuleReference {
public:
    explicit FixedModuleRef(const TS::IModule* module = nullptr)
        : module_(module) {}

    const TS::IModule* Resolve(
        const TS::ITypeResolveContext&) const override {
        return module_;
    }

private:
    const TS::IModule* module_;
};

class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                   std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

// ---------------------------------------------------------------------------
// The render helpers -- each mirrors the gold probe's Program.cs shapes
// byte-for-byte (the RenderMethod / RenderType / Fnv trio).
// ---------------------------------------------------------------------------

const char* AccessibilitySpelling(TS::Accessibility a) {
    switch (a) {
        case TS::Accessibility::None: return "None";
        case TS::Accessibility::Private: return "Private";
        case TS::Accessibility::ProtectedAndInternal:
            return "ProtectedAndInternal";
        case TS::Accessibility::Internal: return "Internal";
        case TS::Accessibility::Protected: return "Protected";
        case TS::Accessibility::ProtectedOrInternal:
            return "ProtectedOrInternal";
        case TS::Accessibility::Public: return "Public";
    }
    return "<unknown>";
}

const char* ReferenceKindSpelling(TS::ReferenceKind r) {
    switch (r) {
        case TS::ReferenceKind::None: return "None";
        case TS::ReferenceKind::Out: return "Out";
        case TS::ReferenceKind::Ref: return "Ref";
        case TS::ReferenceKind::In: return "In";
        case TS::ReferenceKind::RefReadOnly: return "RefReadOnly";
    }
    return "<unknown>";
}

// The C# `TypeKind.ToString()` spelling (the probe's RenderType `td.Kind`) --
// the kinds a TypeDef row reaches (the port's TypeKind carries no `Module`
// member; the BCL corpus never reaches it).
const char* TypeKindSpelling(TS::TypeKind k) {
    switch (k) {
        case TS::TypeKind::Class: return "Class";
        case TS::TypeKind::Interface: return "Interface";
        case TS::TypeKind::Struct: return "Struct";
        case TS::TypeKind::Enum: return "Enum";
        case TS::TypeKind::Delegate: return "Delegate";
        case TS::TypeKind::Void: return "Void";
        case TS::TypeKind::Unknown: return "Unknown";
        default: return "<other>";
    }
}

// The probe's `m.GetType().Name` -- the concrete class discriminator (the
// corpus's Methods() enumerations produce exactly these two).
std::string MethodClassName(const TS::IMethod* m) {
    if (dynamic_cast<const TI::MetadataMethod*>(m) != nullptr)
        return "MetadataMethod";
    if (dynamic_cast<const TI::FakeMethod*>(m) != nullptr)
        return "FakeMethod";
    return "<other>";
}

std::string RenderMethod(const TS::IMethod* m) {
    std::string b = MethodClassName(m);
    b += '|';
    b += std::to_string(static_cast<int>(m->SymbolKind()));
    b += '=';
    b += m->IsConstructor() ? "C" : m->IsOperator() ? "O"
        : m->IsAccessor() ? "A" : "M";
    b += '|';
    b += m->Name();
    b += '|';
    b += AccessibilitySpelling(m->Accessibility());
    b += '|';
    b += m->IsStatic() ? "s" : "-";
    b += m->IsAbstract() ? "a" : "-";
    b += m->IsSealed() ? "x" : "-";
    b += m->IsVirtual() ? "v" : "-";
    b += m->IsOverride() ? "o" : "-";
    b += m->IsOverridable() ? "r" : "-";
    b += m->IsExtensionMethod() ? "e" : "-";
    b += '|';
    b += std::to_string(m->TypeParameters().size());
    b += '|';
    b += m->ReturnType().ReflectionName();
    b += '|';
    std::vector<const TS::IParameter*> params = m->Parameters();
    b += std::to_string(params.size());
    b += '[';
    bool first = true;
    for (const TS::IParameter* p : params) {
        if (!first)
            b += ',';
        b += p->Name();
        b += ':';
        b += p->Type().ReflectionName();
        b += ':';
        b += ReferenceKindSpelling(p->ReferenceKind());
        if (p->IsOptional())
            b += ":opt";
        first = false;
    }
    b += ']';
    b += '|';
    b += m->HasBody() ? "body" : "nobody";
    return b;
}

std::string RenderType(const TS::ITypeDefinition* td) {
    char token[16];
    std::snprintf(token, sizeof(token), "0x%08x", td->MetadataToken());
    return std::string(token) + "|" + TypeKindSpelling(td->Kind()) + "|"
        + std::to_string(td->TypeParameterCount()) + "|"
        + td->ReflectionName();
}

// The probe's FNV-1a-64 (the MetadataMethod_Test form: per-byte XOR+MUL,
// then the XOR(0xff)+MUL terminator per Add).
class Fnv64 {
public:
    void Add(const std::string& s) {
        for (char ch : s) {
            fnv_ ^= static_cast<std::uint8_t>(ch);
            fnv_ *= 0x100000001b3ULL;
        }
        fnv_ ^= 0xff;
        fnv_ *= 0x100000001b3ULL;
    }
    std::uint64_t Digest() const { return fnv_; }

private:
    std::uint64_t fnv_ = 0xcbf29ce484222325ULL;
};

// ---------------------------------------------------------------------------
// The fixture: one single-module compilation per corpus (the probe's
// `new SimpleCompilation(new PEFile(path))` sweep shape).
// ---------------------------------------------------------------------------
struct EnumFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    TM::MetadataFile systemFile{ SystemPath() };
    TM::MetadataFile coreLibFile{ CoreLibPath() };
    std::optional<TM::MetadataFile> roslynFile;

    TestCompilation compMsc;
    TS::MetadataModule mscSingle{ compMsc, &mscorlibFile,
                                   TS::TypeSystemOptions::Default };
    FixedModuleRef mscSingleRef{ &mscSingle };

    TestCompilation compSys;
    TS::MetadataModule sysSingle{ compSys, &systemFile,
                                  TS::TypeSystemOptions::Default };
    FixedModuleRef sysSingleRef{ &sysSingle };

    TestCompilation compCore;
    TS::MetadataModule core{ compCore, &coreLibFile,
                             TS::TypeSystemOptions::Default };
    FixedModuleRef coreRef{ &core };

    TestCompilation compRoslyn;
    std::optional<TS::MetadataModule> roslyn;
    FixedModuleRef roslynRef{ nullptr };

    EnumFixture() {
        compMsc.Initialize(mscSingleRef, {});
        compSys.Initialize(sysSingleRef, {});
        compCore.Initialize(coreRef, {});
        std::string roslynPath = RoslynPath();
        if (!roslynPath.empty()) {
            roslynFile.emplace(roslynPath);
            roslyn.emplace(compRoslyn, &roslynFile.value(),
                           TS::TypeSystemOptions::Default);
            roslynRef = FixedModuleRef{ &roslyn.value() };
            compRoslyn.Initialize(roslynRef, {});
        }
    }
};

class MetadataTypeDefinitionMethodsTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!MscorlibAvailable())
            GTEST_SKIP() << "mscorlib fixture not available";
    }
};

// ---------------------------------------------------------------------------
// Section A: the whole-corpus Methods() sweeps.
// ---------------------------------------------------------------------------

// The sweep helper: walks every TypeDef row in table order (the C#
// `module.TypeDefinitions` shape), rendering per-type headers and per-method
// lines into the digest; returns the counts + the dummy census.
struct SweepResult {
    std::uint32_t typeCount = 0;
    std::uint32_t methodCount = 0;
    std::uint32_t dummyCount = 0;
    std::uint64_t fnv = 0;
    std::string firstDummy;
    std::string lastDummy;
};

SweepResult SweepMethods(const TS::MetadataModule& module) {
    Fnv64 fnv;
    SweepResult result;
    const TM::MetadataFile* file = module.MetadataFile();
    std::uint32_t rowCount
        = file->CorTableRowCount(TM::CorTableIndex::TypeDef);
    for (std::uint32_t row = 1; row <= rowCount; row++) {
        const TS::ITypeDefinition* td
            = module.GetDefinition(0x02000000u | row);
        // (EXPECT + the null guard -- the ASSERT_ void-return trap in a
        // non-void helper.)
        if (td == nullptr) {
            EXPECT_NE(td, nullptr) << "typedef row " << row;
            break;
        }
        result.typeCount++;
        fnv.Add("T:" + RenderType(td));
        std::vector<const TS::IMethod*> methods = td->Methods();
        result.methodCount += static_cast<std::uint32_t>(methods.size());
        fnv.Add("N:" + std::to_string(methods.size()));
        for (const TS::IMethod* m : methods) {
            std::string line = RenderMethod(m);
            fnv.Add(line);
            if (dynamic_cast<const TI::FakeMethod*>(m) != nullptr) {
                result.dummyCount++;
                std::string dummyLine = RenderType(td) + " => " + line;
                if (result.firstDummy.empty())
                    result.firstDummy = dummyLine;
                result.lastDummy = dummyLine;
            }
        }
    }
    result.fnv = fnv.Digest();
    return result;
}

TEST_F(MetadataTypeDefinitionMethodsTest, MscorlibWholeCorpusMethodsSweep)
{
    EnumFixture fx;
    SweepResult r = SweepMethods(fx.mscSingle);
    // A: mscorlib types=3356 methods=24194 dummies=923 fnv=D06654F6F78E0552
    EXPECT_EQ(r.typeCount, 3356u);
    EXPECT_EQ(r.methodCount, 24194u);
    EXPECT_EQ(r.dummyCount, 923u);
    EXPECT_EQ(r.fnv, 0xD06654F6F78E0552ULL);
    // A: mscorlib firstDummy (the first struct/enum row without a ctor --
    // the RegistryHive enum) / lastDummy (the nested EventCacheEntry
    // struct).
    EXPECT_EQ(r.firstDummy,
        "0x02000011|Enum|0|Microsoft.Win32.RegistryHive => "
        "FakeMethod|9=C|.ctor|Public|-------|0|System.Void|0[]|nobody");
    EXPECT_EQ(r.lastDummy,
        "0x02000d19|Struct|0|System.Runtime.InteropServices.WindowsRuntime."
        "WindowsRuntimeMarshal+NativeOrStaticEventRegistrationImpl+"
        "EventCacheEntry => "
        "FakeMethod|9=C|.ctor|Public|-------|0|System.Void|0[]|nobody");
}

TEST_F(MetadataTypeDefinitionMethodsTest, SystemWholeCorpusMethodsSweep)
{
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    EnumFixture fx;
    SweepResult r = SweepMethods(fx.sysSingle);
    // A: system types=2365 methods=13385 dummies=622 fnv=8102E68F4856114D
    EXPECT_EQ(r.typeCount, 2365u);
    EXPECT_EQ(r.methodCount, 13385u);
    EXPECT_EQ(r.dummyCount, 622u);
    EXPECT_EQ(r.fnv, 0x8102E68F4856114DULL);
}

TEST_F(MetadataTypeDefinitionMethodsTest, CoreLibWholeCorpusMethodsSweep)
{
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    EnumFixture fx;
    SweepResult r = SweepMethods(fx.core);
    // A: corelib types=2898 methods=37261 dummies=1024 fnv=91E8D2523265046A
    EXPECT_EQ(r.typeCount, 2898u);
    EXPECT_EQ(r.methodCount, 37261u);
    EXPECT_EQ(r.dummyCount, 1024u);
    EXPECT_EQ(r.fnv, 0x91E8D2523265046AULL);
}

TEST_F(MetadataTypeDefinitionMethodsTest, MethodsCacheAndUncachedVariants)
{
    // The LazyInit cache: two reads return the same snapshot; the Uncached
    // option bypasses the cache (the fresh list per read) while the module's
    // per-row entity cache keeps the method identities stable.
    EnumFixture fx;
    const TS::ITypeDefinition* int32 = fx.mscSingle.GetTypeDefinition(
        TS::TopLevelTypeName("System", "Int32"));
    ASSERT_NE(int32, nullptr);
    std::vector<const TS::IMethod*> a = int32->Methods();
    std::vector<const TS::IMethod*> b = int32->Methods();
    ASSERT_EQ(a.size(), 32u);  // E: int32 methods=32 (31 rows + the dummy)
    EXPECT_EQ(a, b);
    // The dummy is LAST (the append order) and its identity is stable across
    // the cached reads.
    ASSERT_NE(a.back(), nullptr);
    EXPECT_TRUE(dynamic_cast<const TI::FakeMethod*>(a.back()) != nullptr);
    EXPECT_EQ(a.back()->Name(), ".ctor");

    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    TestCompilation compUnc;
    TS::MetadataModule mscUnc{ compUnc, &mscorlibFile,
                               TS::TypeSystemOptions::Default
                                   | TS::TypeSystemOptions::Uncached };
    FixedModuleRef mscUncRef{ &mscUnc };
    compUnc.Initialize(mscUncRef, {});
    const TS::ITypeDefinition* int32Unc = mscUnc.GetTypeDefinition(
        TS::TopLevelTypeName("System", "Int32"));
    ASSERT_NE(int32Unc, nullptr);
    std::vector<const TS::IMethod*> c = int32Unc->Methods();
    std::vector<const TS::IMethod*> d = int32Unc->Methods();
    ASSERT_EQ(c.size(), 32u);
    EXPECT_EQ(d.size(), 32u);
    // The fresh dummies are distinct instances (the C# fresh-per-read); the
    // previously returned raw pointers stay alive through the keep-alive
    // registry (the port's GC stand-in).
    EXPECT_NE(c.back(), d.back());
    EXPECT_NE(c.back(), nullptr);
    EXPECT_EQ(c.back()->Name(), ".ctor");
}

// ---------------------------------------------------------------------------
// Section E: the curated GetMethods arm drives.
// ---------------------------------------------------------------------------

TEST_F(MetadataTypeDefinitionMethodsTest, GetMethodsArmsOverString)
{
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    EnumFixture fx;
    const TS::ITypeDefinition* td = fx.mscSingle.GetTypeDefinition(
        TS::TopLevelTypeName("System", "String"));
    ASSERT_NE(td, nullptr);
    std::vector<const TS::IMethod*> methods = td->Methods();
    std::vector<const TS::IMethod*> iim = td->GetMethods(
        nullptr, TS::GetMemberOptions::IgnoreInheritedMembers);
    std::vector<const TS::IMethod*> inherited = td->GetMethods(
        nullptr, TS::GetMemberOptions::None);
    // E: string methods=191 iim=183 inherited=194
    EXPECT_EQ(methods.size(), 191u);
    EXPECT_EQ(iim.size(), 183u);
    EXPECT_EQ(inherited.size(), 194u);
    // E: string iim-hasctor=0 inherited-hasobjectequals=11 -- the declared
    // arm drops every constructor; the inherited walk adds Object's 11
    // non-ctor methods (the runtime-support FieldSetter/FieldGetter/
    // GetFieldInfo trio included).
    EXPECT_EQ(std::count_if(iim.begin(), iim.end(),
                   [](const TS::IMethod* m) { return m->IsConstructor(); }),
        0u);
    EXPECT_EQ(std::count_if(inherited.begin(), inherited.end(),
                   [](const TS::IMethod* m) {
                       return m->DeclaringType() != nullptr
                           && m->DeclaringType()->ReflectionName()
                               == "System.Object";
                   }),
        11u);
    // E: filtered=1 Copy
    std::vector<const TS::IMethod*> filtered = td->GetMethods(
        [](const TS::IMethod* m) { return m->Name() == "Copy"; },
        TS::GetMemberOptions::IgnoreInheritedMembers);
    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0]->Name(), "Copy");
}

TEST_F(MetadataTypeDefinitionMethodsTest, Int32HasTheDummyConstructorInMethodsOnly)
{
    EnumFixture fx;
    const TS::ITypeDefinition* int32 = fx.mscSingle.GetTypeDefinition(
        TS::TopLevelTypeName("System", "Int32"));
    ASSERT_NE(int32, nullptr);
    std::vector<const TS::IMethod*> iim = int32->GetMethods(
        nullptr, TS::GetMemberOptions::IgnoreInheritedMembers);
    // E: int32 iim=31 iim-dummies=0 -- the declared arm's !IsConstructor
    // filter drops the dummy (and Int32 declares no .ctor row of its own).
    EXPECT_EQ(iim.size(), 31u);
    EXPECT_EQ(std::count_if(iim.begin(), iim.end(),
                   [](const TS::IMethod* m) {
                       return dynamic_cast<const TI::FakeMethod*>(m)
                           != nullptr;
                   }),
        0u);
    // E: int32 iim names (the exact declared order).
    std::string names;
    for (const TS::IMethod* m : iim) {
        if (!names.empty())
            names += ',';
        names += m->Name();
    }
    EXPECT_EQ(names,
        "CompareTo,CompareTo,Equals,Equals,GetHashCode,ToString,ToString,"
        "ToString,ToString,Parse,Parse,Parse,Parse,TryParse,TryParse,"
        "GetTypeCode,System.IConvertible.ToBoolean,System.IConvertible."
        "ToChar,System.IConvertible.ToSByte,System.IConvertible.ToByte,"
        "System.IConvertible.ToInt16,System.IConvertible.ToUInt16,"
        "System.IConvertible.ToInt32,System.IConvertible.ToUInt32,"
        "System.IConvertible.ToInt64,System.IConvertible.ToUInt64,"
        "System.IConvertible.ToSingle,System.IConvertible.ToDouble,"
        "System.IConvertible.ToDecimal,System.IConvertible.ToDateTime,"
        "System.IConvertible.ToType");
}

TEST_F(MetadataTypeDefinitionMethodsTest, InheritedWalkOverNullableAndObject)
{
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    EnumFixture fx;
    // The probe's objmethods drive: Object's own Methods() (12 rows: the
    // ctor + the 8 public/protected + the 3 runtime-support methods).
    const TS::ITypeDefinition* obj = fx.mscSingle.GetTypeDefinition(
        TS::TopLevelTypeName("System", "Object"));
    ASSERT_NE(obj, nullptr);
    std::vector<const TS::IMethod*> objMethods = obj->Methods();
    ASSERT_EQ(objMethods.size(), 12u);
    EXPECT_EQ(objMethods[0]->Name(), ".ctor");
    EXPECT_EQ(objMethods[8]->Name(), "MemberwiseClone");
    EXPECT_EQ(objMethods[9]->Name(), "FieldSetter");
    EXPECT_EQ(objMethods[10]->Name(), "FieldGetter");
    EXPECT_EQ(objMethods[11]->Name(), "GetFieldInfo");
    EXPECT_TRUE(dynamic_cast<const TI::FakeMethod*>(objMethods.back()) == nullptr);

    // E: nullable`1 inherited=24 base-methods-in=17
    const TS::ITypeDefinition* nullable = fx.mscSingle.GetTypeDefinition(
        TS::TopLevelTypeName("System", "Nullable", 1));
    ASSERT_NE(nullable, nullptr);
    std::vector<const TS::IMethod*> nb = nullable->GetMethods(
        nullptr, TS::GetMemberOptions::None);
    EXPECT_EQ(nb.size(), 24u);
    EXPECT_EQ(std::count_if(nb.begin(), nb.end(),
                   [](const TS::IMethod* m) {
                       return m->DeclaringType() != nullptr
                           && m->DeclaringType()->ReflectionName()
                               != "System.Nullable`1";
                   }),
        17u);
}

TEST_F(MetadataTypeDefinitionMethodsTest, VoidEarlyExit)
{
    EnumFixture fx;
    const TS::ITypeDefinition* voidT = fx.mscSingle.GetTypeDefinition(
        TS::TopLevelTypeName("System", "Void"));
    ASSERT_NE(voidT, nullptr);
    // E: void getmethods=0 iim=0 / methods-enum=0
    EXPECT_TRUE(voidT->GetMethods(nullptr, TS::GetMemberOptions::None).empty());
    EXPECT_TRUE(voidT->GetMethods(
        nullptr, TS::GetMemberOptions::IgnoreInheritedMembers).empty());
    EXPECT_TRUE(voidT->Methods().empty());
}

TEST_F(MetadataTypeDefinitionMethodsTest, TypeArgumentsOverloadOverListGeneric)
{
    EnumFixture fx;
    const TS::ITypeDefinition* listT = fx.mscSingle.GetTypeDefinition(
        TS::TopLevelTypeName("System.Collections.Generic", "List", 1));
    ASSERT_NE(listT, nullptr);
    const TS::IType& stringType = fx.compMsc.FindType(
        TS::KnownTypeCode::String);
    TS::ITypePtr stringPtr(const_cast<TS::IType*>(&stringType),
                            [](TS::IType*) {
                                // no-op: the compilation owns the known type
                            });
    std::vector<TS::ITypePtr> typeArguments{ stringPtr };
    // E: list`1 ta-count=1 (the definitions arm: ConvertAll, the only
    // method with exactly one method type parameter).
    std::vector<const TS::IMethod*> ta = listT->GetMethods(
        typeArguments, nullptr,
        TS::GetMemberOptions::IgnoreInheritedMembers
            | TS::GetMemberOptions::ReturnMemberDefinitions);
    ASSERT_EQ(ta.size(), 1u);
    EXPECT_EQ(ta[0]->Name(), "ConvertAll");
    EXPECT_EQ(ta[0]->ReturnType().ReflectionName(),
        "System.Collections.Generic.List`1[[``0]]");
    // E: list`1 taSpec-count=1 -- the SpecializedMethod arm (the substituted
    // parameter and return types).
    std::vector<const TS::IMethod*> taSpec = listT->GetMethods(
        typeArguments, nullptr,
        TS::GetMemberOptions::IgnoreInheritedMembers);
    ASSERT_EQ(taSpec.size(), 1u);
    EXPECT_EQ(taSpec[0]->Name(), "ConvertAll");
    EXPECT_EQ(taSpec[0]->ReturnType().ReflectionName(),
        "System.Collections.Generic.List`1[[System.String]]");
    EXPECT_EQ(taSpec[0]->Parameters().size(), 1u);
    EXPECT_EQ(taSpec[0]->Parameters()[0]->Type().ReflectionName(),
        "System.Converter`2[[`0],[System.String]]");
}

// ---------------------------------------------------------------------------
// Section B: the IsRecord corpus digests + the Roslyn record fixtures.
// ---------------------------------------------------------------------------

std::uint64_t RecordDigest(const TS::MetadataModule& module,
                           std::uint32_t* typeCount,
                           std::uint32_t* trueCount,
                           std::vector<std::string>* trueHits) {
    Fnv64 fnv;
    std::uint32_t types = 0;
    std::uint32_t hits = 0;
    const TM::MetadataFile* file = module.MetadataFile();
    std::uint32_t rowCount
        = file->CorTableRowCount(TM::CorTableIndex::TypeDef);
    for (std::uint32_t row = 1; row <= rowCount; row++) {
        const TS::ITypeDefinition* td
            = module.GetDefinition(0x02000000u | row);
        if (td == nullptr) {
            EXPECT_NE(td, nullptr) << "typedef row " << row;
            break;
        }
        types++;
        bool r = td->IsRecord();
        fnv.Add((r ? std::string("R:") : std::string("r:"))
            + td->ReflectionName());
        if (r) {
            hits++;
            if (trueHits != nullptr && trueHits->size() < 20)
                trueHits->push_back(RenderType(td));
        }
    }
    *typeCount = types;
    *trueCount = hits;
    return fnv.Digest();
}

TEST_F(MetadataTypeDefinitionMethodsTest, IsRecordCorpusDigests)
{
    EnumFixture fx;
    {
        std::uint32_t types = 0, hits = 0;
        std::uint64_t digest = RecordDigest(fx.mscSingle, &types, &hits, nullptr);
        // B: mscorlib types=3356 records=0 fnv=75995CABC7007430
        EXPECT_EQ(types, 3356u);
        EXPECT_EQ(hits, 0u);
        EXPECT_EQ(digest, 0x75995CABC7007430ULL);
    }
    if (SystemAvailable()) {
        std::uint32_t types = 0, hits = 0;
        std::uint64_t digest = RecordDigest(fx.sysSingle, &types, &hits, nullptr);
        // B: system types=2365 records=0 fnv=71C9B47072C6044A
        EXPECT_EQ(types, 2365u);
        EXPECT_EQ(hits, 0u);
        EXPECT_EQ(digest, 0x71C9B47072C6044AULL);
    }
    if (CoreLibAvailable()) {
        std::uint32_t types = 0, hits = 0;
        std::uint64_t digest = RecordDigest(fx.core, &types, &hits, nullptr);
        // B: corelib types=2898 records=0 fnv=68F304DB8C6B889E
        EXPECT_EQ(types, 2898u);
        EXPECT_EQ(hits, 0u);
        EXPECT_EQ(digest, 0x68F304DB8C6B889EULL);
    }
}

TEST_F(MetadataTypeDefinitionMethodsTest, RoslynRecordFixtures)
{
    EnumFixture fx;
    if (!fx.roslyn.has_value())
        GTEST_SKIP() << "the Roslyn Microsoft.CodeAnalysis.dll fixture "
                        "is not available";
    std::uint32_t types = 0, hits = 0;
    std::vector<std::string> trueHits;
    RecordDigest(fx.roslyn.value(), &types, &hits, &trueHits);
    // B: scan Microsoft.CodeAnalysis.dll types=2374 hits=9
    EXPECT_EQ(types, 2374u);
    EXPECT_EQ(hits, 9u);
    // The nine record fixtures in declaration order (the probe's scan list).
    ASSERT_EQ(trueHits.size(), 9u);
    EXPECT_EQ(trueHits[0],
        "0x02000122|Struct|0|Microsoft.CodeAnalysis.DiagnosticDescriptor"
        "ErrorLoggerInfo");
    EXPECT_EQ(trueHits[1],
        "0x02000219|Struct|1|Microsoft.CodeAnalysis.NodeStateEntry`1");
    EXPECT_EQ(trueHits[2],
        "0x020004ea|Struct|0|Microsoft.CodeAnalysis.Emit.AnonymousDelegate"
        "WithIndexedNamePartialKey");
    EXPECT_EQ(trueHits[3],
        "0x02000560|Struct|0|Microsoft.CodeAnalysis.CodeGen.AwaitDebugId");
    EXPECT_EQ(trueHits[4],
        "0x02000562|Struct|0|Microsoft.CodeAnalysis.CodeGen."
        "ClosureDebugInfo");
    EXPECT_EQ(trueHits[5],
        "0x02000565|Struct|0|Microsoft.CodeAnalysis.CodeGen.DebugId");
    EXPECT_EQ(trueHits[6],
        "0x02000569|Struct|0|Microsoft.CodeAnalysis.CodeGen."
        "LambdaDebugInfo");
    EXPECT_EQ(trueHits[7],
        "0x020006ac|Class|0|Microsoft.CodeAnalysis.SourceGeneratorAdaptor"
        "+GeneratorContextBuilder");
    EXPECT_EQ(trueHits[8],
        "0x020008d5|Struct|0|Microsoft.CodeAnalysis.SarifV2ErrorLogger+"
        "DiagnosticDescriptorSet+DescriptorInfoWithIndex");
    // The record-class fixture carries the <Clone>$ + PrintMembers pair the
    // IsRecord scan keys on (Methods() drops the get_EqualityContract
    // accessor -- the raw scan is what classifies it).
    const TS::ITypeDefinition* recordClass = fx.roslyn->GetDefinition(
        0x020006acu);
    ASSERT_NE(recordClass, nullptr);
    bool hasClone = false, hasPrintMembers = false;
    for (const TS::IMethod* m : recordClass->Methods()) {
        hasClone |= m->Name() == "<Clone>$";
        hasPrintMembers |= m->Name() == "PrintMembers";
    }
    EXPECT_TRUE(hasClone);
    EXPECT_TRUE(hasPrintMembers);
    EXPECT_TRUE(recordClass->IsRecord());
}

} // namespace
