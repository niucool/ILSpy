// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
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

// The ResolveMethod / ResolveEntity / GetOverrides test suite -- the gold
// reference is the real ICSharpCode.Decompiler 11.0 driven through the
// public `MetadataModule.ResolveMethod` / `ResolveEntity` surface over the
// same fixtures (the C:/temp-probe/RmProbe dump; the generated
// TestFixtures/ResolveMethod_Gold.hpp carries every digest / count /
// curated line). The whole-corpus digests pin the member-reference
// resolution end-to-end: every memberref row of mscorlib (single-module)
// and System.dll (paired with the mscorlib reference), every MethodSpec
// row, and every method carrying MethodImpl rows -- the same drives the
// probe made, folded through the established FNV form.
//
// The not-yet-landed MetadataProperty / MetadataEntity slice shows up as
// the documented deferral partition: accessor-named member references whose
// declaring type RESOLVES throw (the accessor search routes through
// `MetadataTypeDefinition::GetAccessors` -> `Properties()`), while the
// UNRESOLVABLE declaring types take the fake-method path in both engines
// (the CreateFakeMethod + GuessFakeMethodAccessor gold, pinned byte-exact
// over real System.Configuration metadata).

#include "Decompiler/Metadata/MetadataFile.hpp"
#include <cstdlib>
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataField.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedMethod.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedField.hpp"
#include "Decompiler/TypeSystem/VarArgInstanceMethod.hpp"
#include "TestFixtures/ResolveMethod_Gold.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;
namespace TI = ILSpy::Decompiler::TypeSystem::Implementation;
namespace G = ILSpy::Tests;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
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

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

bool MscorlibAvailable() { return FileExists(MscorlibPath()); }

// A module reference resolving to an externally-owned module (the
// MetadataField_Test fixture precedent).
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

// The port's SimpleCompilation with the protected Init exposed.
class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                   std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

// The two probe compilation shapes: mscorlib single-module (every memberref
// parent is a TypeSpec into mscorlib itself) and System.dll paired with the
// mscorlib reference (the cross-module memberref drives, the unresolvable
// System.Configuration parents exercising the fake-method fallback).
struct RmFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    TM::MetadataFile systemFile{ SystemPath() };

    TestCompilation compMscSingle;
    TS::MetadataModule mscSingle{ compMscSingle, &mscorlibFile,
                                  TS::TypeSystemOptions::Default };
    FixedModuleRef mscSingleRef{ &mscSingle };

    TestCompilation compSysPaired;
    TS::MetadataModule sysPaired{ compSysPaired, &systemFile,
                                  TS::TypeSystemOptions::Default };
    TS::MetadataModule mscForSys{ compSysPaired, &mscorlibFile,
                                  TS::TypeSystemOptions::Default };
    FixedModuleRef sysPairedRef{ &sysPaired };
    FixedModuleRef mscForSysRef{ &mscForSys };

    RmFixture() {
        compMscSingle.Initialize(mscSingleRef, {});
        compSysPaired.Initialize(sysPairedRef, { &mscForSysRef });
    }
};

// The established FNV-1a-64 form (the MetadataField_Test / MetadataMethod_Test
// Fnv64 -- the xor-then-prime per byte plus the 0xff line terminator).
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

// The `SymbolKind.ToString()` spelling (the VarArgInstanceMethod.cpp
// SpellSymbolKind table, mirrored for the render -- the function is
// file-local there).
std::string SpellSymbolKind(TS::SymbolKind kind)
{
    switch (kind) {
    case TS::SymbolKind::None: return "None";
    case TS::SymbolKind::Module: return "Module";
    case TS::SymbolKind::TypeDefinition: return "TypeDefinition";
    case TS::SymbolKind::Field: return "Field";
    case TS::SymbolKind::Property: return "Property";
    case TS::SymbolKind::Indexer: return "Indexer";
    case TS::SymbolKind::Event: return "Event";
    case TS::SymbolKind::Method: return "Method";
    case TS::SymbolKind::Operator: return "Operator";
    case TS::SymbolKind::Constructor: return "Constructor";
    case TS::SymbolKind::Destructor: return "Destructor";
    case TS::SymbolKind::Accessor: return "Accessor";
    case TS::SymbolKind::Namespace: return "Namespace";
    case TS::SymbolKind::Variable: return "Variable";
    case TS::SymbolKind::Parameter: return "Parameter";
    case TS::SymbolKind::TypeParameter: return "TypeParameter";
    case TS::SymbolKind::Constraint: return "Constraint";
    }
    return "<unknown>";
}

// The probe's line prefix form: "<K> <token:X8> " (K = M / F / S / D).
std::string LinePrefix(char kind, std::uint32_t token) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%c %08X ", kind, token);
    return buffer;
}

// ---------------------------------------------------------------------------
// The render helpers -- each mirrors the gold probe's Program.cs shapes
// byte-for-byte, so the gold lines compare directly.
// ---------------------------------------------------------------------------

// The C# `m.GetType().Name` -- the concrete class name.
std::string MethodClassName(const TS::IMethod* m) {
    if (dynamic_cast<const TS::VarArgInstanceMethod*>(m) != nullptr)
        return "VarArgInstanceMethod";
    if (dynamic_cast<const TI::SpecializedMethod*>(m) != nullptr)
        return "SpecializedMethod";
    if (dynamic_cast<const TI::FakeMethod*>(m) != nullptr)
        return "FakeMethod";
    if (dynamic_cast<const TI::MetadataMethod*>(m) != nullptr)
        return "MetadataMethod";
    return "<unknown>";
}

std::string FieldClassName(const TS::IField* f) {
    if (dynamic_cast<const TI::SpecializedField*>(f) != nullptr)
        return "SpecializedField";
    if (dynamic_cast<const TI::FakeField*>(f) != nullptr)
        return "FakeField";
    if (dynamic_cast<const TI::MetadataField*>(f) != nullptr)
        return "MetadataField";
    return "<unknown>";
}

std::string EntityClassName(const TS::IEntity* e) {
    if (auto* m = dynamic_cast<const TS::IMethod*>(e)) {
        // IMethod is the most-derived view here; dispatch through the
        // method-class chain.
        if (dynamic_cast<const TS::VarArgInstanceMethod*>(m) != nullptr)
            return "VarArgInstanceMethod";
        if (dynamic_cast<const TI::SpecializedMethod*>(m) != nullptr)
            return "SpecializedMethod";
        if (dynamic_cast<const TI::FakeMethod*>(m) != nullptr)
            return "FakeMethod";
        if (dynamic_cast<const TI::MetadataMethod*>(m) != nullptr)
            return "MetadataMethod";
        return "<unknown>";
    }
    if (auto* f = dynamic_cast<const TS::IField*>(e))
        return FieldClassName(f);
    if (dynamic_cast<const TI::MetadataTypeDefinition*>(e) != nullptr)
        return "MetadataTypeDefinition";
    return "<unknown>";
}

std::string RenderType(const TS::IType* t) {
    return t == nullptr ? std::string("<null>") : t->ReflectionName();
}

std::string RenderMethod(const TS::IMethod* m) {
    if (m == nullptr)
        return "<null>";
    std::string b = MethodClassName(m);
    b += "|" + SpellSymbolKind(m->SymbolKind());
    b += "|" + m->Name();
    b += std::string("|") + (m->IsStatic() ? "True" : "False");
    b += "|" + std::to_string(m->TypeParameters().size());
    b += "|" + RenderType(m->DeclaringType().get());
    b += "|" + m->ReturnType().ReflectionName();
    b += "|" + std::to_string(m->Parameters().size());
    for (const TS::IParameter* p : m->Parameters())
        b += "|" + p->Name() + ":" + p->Type().ReflectionName();
    b += "|" + std::to_string(
        static_cast<int>(m->AccessorKind()));
    // The port's `AccessorOwner` is the loud MetadataProperty/MetadataEvent
    // deferral; the gold lines' owner fields ("Property:X") pin the
    // not-yet-landed property entities -- the deferred drives compare with
    // the marker field.
    bool ownerDeferred = false;
    const TS::IMember* owner = nullptr;
    try {
        owner = m->AccessorOwner();
    } catch (const std::logic_error&) {
        ownerDeferred = true;
    }
    if (ownerDeferred) {
        b += "|<deferred>";
    } else if (owner != nullptr) {
        b += "|" + SpellSymbolKind(owner->SymbolKind())
            + ":" + owner->Name();
    } else {
        b += "|<null>";
    }
    b += "|" + m->Substitution()->ToString();
    return b;
}

std::string RenderField(const TS::IField* f) {
    if (f == nullptr)
        return "<null>";
    std::string b = FieldClassName(f);
    b += "|" + f->Name();
    b += "|" + RenderType(f->DeclaringType().get());
    b += "|" + f->ReturnType().ReflectionName();
    b += std::string("|") + (f->IsStatic() ? "True" : "False");
    return b;
}

std::string RenderEntity(const TS::IEntity* e) {
    if (e == nullptr)
        return "<null>";
    return EntityClassName(e) + "|" + SpellSymbolKind(e->SymbolKind())
        + "|" + e->Name() + "|" + RenderType(e->DeclaringType().get());
}

// The probe's `AccessorShaped` name-form test.
bool AccessorShaped(const std::string& name) {
    return name.rfind("get_", 0) == 0 || name.rfind("set_", 0) == 0
        || name.rfind("add_", 0) == 0 || name.rfind("remove_", 0) == 0
        || name.rfind("raise_", 0) == 0;
}

// The probe's memberref-kind rule (the decompiled .NET 10
// SignatureHeader.Kind): nibble <= 5 or == 9 -> method, 6 -> field.
bool MemberRefIsMethodKind(const TM::MetadataFile& file,
                           std::uint32_t token) {
    auto blob = file.GetSignatureBlob(token);
    if (!blob || blob->empty())
        return false;
    int low = (*blob)[0] & 0x0F;
    return low <= 5 || low == 9;
}

// Renders the MethodImpl drive line for one method (the probe's
// SectionMethodImpls shape): "<bodyToken:X8> <name> isExplicit=<bool>
// members=<n>" then a "\n    " + RenderEntity line per member.
std::string ImplDriveLine(std::uint32_t bodyToken,
                          const TS::IMethod* method) {
    char prefix[16];
    std::snprintf(prefix, sizeof(prefix), "%08X ", bodyToken);
    std::string line = prefix + method->Name() + " isExplicit="
        + (method->IsExplicitInterfaceImplementation() ? "True" : "False")
        + " members="
        + std::to_string(
            method->ExplicitlyImplementedInterfaceMembers().size());
    for (const TS::IMember* mem :
        method->ExplicitlyImplementedInterfaceMembers())
        line += "\n    " + RenderEntity(mem);
    return line;
}

} // namespace

// ---------------------------------------------------------------------------
// The test suite.
// ---------------------------------------------------------------------------
class ResolveMethodTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!MscorlibAvailable())
            GTEST_SKIP() << "mscorlib fixture not available";
    }
};

// The mscorlib single-module whole-corpus memberref digests: the non-accessor
// method memberrefs through ResolveMethod, the field memberrefs through
// ResolveEntity, the accessor-shaped ones split into the deferral throw
// (every one of mscorlib's 824 has a resolvable TypeSpec parent) -- no
// accessor fakes exist in a self-contained mscorlib.
TEST_F(ResolveMethodTest, MscSingleMemberRefDigests)
{
    RmFixture fx;
    const TM::MetadataFile* file = fx.mscSingle.MetadataFile();
    TS::GenericContext context({}, {});
    Fnv64 methodFnv, fieldFnv, accFakeFnv, accRealFnv;
    int methodRows = 0, fieldRows = 0, accReal = 0, accFake = 0;
    for (const TM::MetadataFile::MemberRefInfo& row : file->MemberRefs()) {
        bool methodKind = MemberRefIsMethodKind(*file, row.Token);
        if (methodKind) {
            if (AccessorShaped(row.Name)) {
                // The resolvable declaring type: the methods search misses,
                // the accessor search hits (the Properties/Events
                // enumerations -- the MetadataProperty/MetadataEvent slice
                // landed it; every one of mscorlib's 824 resolves to a REAL
                // accessor method, both engines byte-exact).
                const TS::IMethod* acc =
                    fx.mscSingle.ResolveMethod(row.Token, context);
                std::string accLine =
                    LinePrefix('M', row.Token) + RenderMethod(acc);
                if (accLine.find("FakeMethod") != std::string::npos) {
                    accFakeFnv.Add(accLine);
                    accFake++;
                } else {
                    accRealFnv.Add(accLine);
                    accReal++;
                }
                continue;
            }
            const TS::IMethod* m =
                fx.mscSingle.ResolveMethod(row.Token, context);
            methodFnv.Add(LinePrefix('M', row.Token) + RenderMethod(m));
            methodRows++;
        } else {
            const TS::IEntity* e =
                fx.mscSingle.ResolveEntity(row.Token, context);
            const auto* f = dynamic_cast<const TS::IField*>(e);
            fieldFnv.Add(LinePrefix('F', row.Token) + RenderField(f));
            fieldRows++;
        }
    }
    EXPECT_EQ(methodRows, G::kMscMethodRows);
    EXPECT_EQ(fieldRows, G::kMscFieldRows);
    EXPECT_EQ(accReal, G::kMscAccRealCount);
    EXPECT_EQ(accRealFnv.Digest(), G::kMscAccRealDigest);
    EXPECT_EQ(accFake, G::kMscAccFakeCount);
    EXPECT_EQ(methodFnv.Digest(), G::kMscMethodDigest);
    EXPECT_EQ(fieldFnv.Digest(), G::kMscFieldDigest);
}

// The mscorlib MethodSpec rows: every one resolves through the
// ResolveMethodSpecification path (the MethodDef-target Specialize arm).
TEST_F(ResolveMethodTest, MscSingleMethodSpecDigest)
{
    RmFixture fx;
    const TM::MetadataFile* file = fx.mscSingle.MetadataFile();
    TS::GenericContext context({}, {});
    Fnv64 fnv;
    int rows = 0;
    for (const TM::MetadataFile::MethodSpecInfo& row : file->MethodSpecs()) {
        const TS::IMethod* m =
            fx.mscSingle.ResolveMethod(row.Token, context);
        fnv.Add(LinePrefix('S', row.Token) + RenderMethod(m));
        rows++;
    }
    EXPECT_EQ(rows, G::kMscSpecRows);
    EXPECT_EQ(fnv.Digest(), G::kMscSpecDigest);
}

// The mscorlib MethodImpl drives: every method carrying rows, partitioned by
// the declaration kind -- the same-module MethodDef declarations (resolved
// directly through the entity cache) and the cross-module MemberRef
// declarations split into the plain-name resolves and the accessor-named
// deferral throws.
TEST_F(ResolveMethodTest, MscMethodImplDrives)
{
    RmFixture fx;
    const TM::MetadataFile* file = fx.mscSingle.MetadataFile();
    // The whole-table grouping (the probe's byMethod dictionary).
    std::map<std::uint32_t, std::vector<std::uint32_t>> byMethod;
    for (const TM::MetadataFile::MethodImplRowInfo& row :
        file->MethodImplRows()) {
        byMethod[row.MethodBodyToken].push_back(
            row.MethodDeclarationToken);
    }
    int implMethods = 0, sameDecl = 0, memberRefDecl = 0, accessorDecl = 0;
    int accDeclFake = 0, accDeclReal = 0;
    Fnv64 sameFnv, crossPlainFnv, accDeclFakeFnv, accDeclRealFnv;
    for (const auto& kv : byMethod) {
        const TS::IMethod* method =
            fx.mscSingle.GetDefinitionMethod(kv.first);
        ASSERT_NE(method, nullptr) << "body token " << std::hex << kv.first;
        implMethods++;
        bool allSameModule = true;
        bool anyAccessor = false;
        for (std::uint32_t decl : kv.second) {
            if ((decl & 0xFF000000u) != 0x06000000u) {
                allSameModule = false;
                if ((decl & 0xFF000000u) == 0x0A000000u) {
                    auto memberRef =
                        file->GetMemberReference(decl);
                    if (memberRef && AccessorShaped(memberRef->Name))
                        anyAccessor = true;
                }
            }
        }
        if (allSameModule) {
            sameDecl++;
            sameFnv.Add(ImplDriveLine(kv.first, method));
        } else {
            memberRefDecl++;
            if (anyAccessor) {
                // The cross-assembly accessor declaration: the resolution
                // lands in the accessor-search arm (over the declaring
                // type's Properties/Events enumerations) -- either the
                // REAL accessor method or the FakeMethod the
                // unresolvable declaring type builds (both engines agree
                // on both subsets; the partition mirrors the probe's
                // FakeMethod-line test).
                std::string accLine = ImplDriveLine(kv.first, method);
                if (accLine.find("FakeMethod") != std::string::npos) {
                    accDeclFakeFnv.Add(accLine);
                    accDeclFake++;
                } else {
                    accDeclRealFnv.Add(accLine);
                    accDeclReal++;
                }
                accessorDecl++;
            } else {
                crossPlainFnv.Add(ImplDriveLine(kv.first, method));
            }
        }
    }
    EXPECT_EQ(accDeclFake, G::kMscAccDeclFakeCount);
    EXPECT_EQ(accDeclFakeFnv.Digest(), G::kMscAccDeclFakeDigest);
    EXPECT_EQ(accDeclReal, G::kMscAccDeclRealCount);
    EXPECT_EQ(accDeclRealFnv.Digest(), G::kMscAccDeclRealDigest);
    EXPECT_EQ(implMethods, G::kMscImplMethods);
    EXPECT_EQ(sameDecl, G::kMscSameDeclMethods);
    EXPECT_EQ(memberRefDecl, G::kMscMemberRefDeclMethods);
    EXPECT_EQ(accessorDecl, G::kMscAccessorDeclMethods);
    EXPECT_EQ(sameFnv.Digest(), G::kMscSameDigest);
    EXPECT_EQ(crossPlainFnv.Digest(), G::kMscCrossPlainDigest);
}

// The sampled MethodDef drives (the probe's vararg + every-2500th sample):
// the plain definition resolves, the vararg methods expand into the
// VarArgInstanceMethod form.
TEST_F(ResolveMethodTest, MscMethodDefSamples)
{
    RmFixture fx;
    TS::GenericContext context({}, {});
    int methoddefRows = 0;
    for (int i = 0; i < G::kMscMethoddefLinesCount; i++) {
        std::uint32_t token = std::stoul(
            G::kMscMethoddefLines[i].first, nullptr, 16);
        const TS::IMethod* m = fx.mscSingle.ResolveMethod(token, context);
        // The gold lines carry the real engine's AccessorOwner renders
        // ("Property:message" -- four of the samples carry accessor
        // owners), byte-exact since the MetadataProperty/MetadataEvent
        // slice landed (the owner field is the second-to-last
        // '|'-separated field).
        EXPECT_EQ(RenderMethod(m), std::string(
            G::kMscMethoddefLines[i].second))
            << "token " << std::hex << token;
        methoddefRows++;
    }
    EXPECT_EQ(methoddefRows, G::kMscMethoddefRows);
}

// The ResolveEntity spot drives: the nil token, a TypeDef, a MethodDef (the
// un-expanded definition -- ResolveEntity never expands varargs), and a
// FieldDef.
TEST_F(ResolveMethodTest, MscEntitySpotDrives)
{
    RmFixture fx;
    TS::GenericContext context({}, {});
    EXPECT_EQ(fx.mscSingle.ResolveEntity(0, context), nullptr);
    // The gold lines: "<form> <token> -> <render>" (the first is the nil
    // form "nil -> True", covered by the nullptr check above).
    for (int i = 0; i < G::kMscEntityLineCount; i++) {
        std::string gold = G::kMscEntityLines[i];
        if (gold.rfind("nil ", 0) == 0)
            continue;
        std::size_t arrow = gold.find(" -> ");
        ASSERT_NE(arrow, std::string::npos);
        std::string head = gold.substr(0, arrow);
        std::size_t space = head.find(' ');
        ASSERT_NE(space, std::string::npos);
        std::string form = head.substr(0, space);
        std::uint32_t token = std::stoul(head.substr(space + 1),
            nullptr, 16);
        const TS::IEntity* e = fx.mscSingle.ResolveEntity(token, context);
        EXPECT_EQ(RenderEntity(e), gold.substr(arrow + 4))
            << form << " token " << std::hex << token;
    }
}

// The curated memberref / methodspec exact lines (the probe's first N rows
// per kind -- byte-exact beyond the digests).
TEST_F(ResolveMethodTest, MscCuratedLines)
{
    RmFixture fx;
    TS::GenericContext context({}, {});
    for (int i = 0; i < G::kMscFirstMethodLinesCount; i++) {
        std::uint32_t token = std::stoul(
            G::kMscFirstMethodLines[i].first, nullptr, 16);
        const TS::IMethod* m = fx.mscSingle.ResolveMethod(token, context);
        EXPECT_EQ(RenderMethod(m), G::kMscFirstMethodLines[i].second)
            << "token " << std::hex << token;
    }
    for (int i = 0; i < G::kMscFirstFieldLinesCount; i++) {
        std::uint32_t token = std::stoul(
            G::kMscFirstFieldLines[i].first, nullptr, 16);
        const TS::IEntity* e =
            fx.mscSingle.ResolveEntity(token, context);
        const auto* f = dynamic_cast<const TS::IField*>(e);
        EXPECT_EQ(RenderField(f), G::kMscFirstFieldLines[i].second)
            << "token " << std::hex << token;
    }
    for (int i = 0; i < G::kMscFirstSpecLinesCount; i++) {
        std::uint32_t token = std::stoul(
            G::kMscFirstSpecLines[i].first, nullptr, 16);
        const TS::IMethod* m = fx.mscSingle.ResolveMethod(token, context);
        EXPECT_EQ(RenderMethod(m), G::kMscFirstSpecLines[i].second)
            << "token " << std::hex << token;
    }
}

// The System.dll paired compilation: the cross-module memberref drives --
// the mscorlib-parent rows resolve into the real mscorlib entities, the
// unresolvable System.Configuration parents take the fake-method fallback,
// and the accessor-shaped rows split into the 630 resolvable-parent
// deferral throws and the 74 fake-guess resolves (the
// CreateFakeMethod + GuessFakeMethodAccessor gold over real metadata).
TEST_F(ResolveMethodTest, SysPairedMemberRefDigests)
{
    RmFixture fx;
    const TM::MetadataFile* file = fx.sysPaired.MetadataFile();
    TS::GenericContext context({}, {});
    Fnv64 methodFnv, fieldFnv, accFakeFnv, accRealFnv;
    int methodRows = 0, fieldRows = 0, accReal = 0, accFake = 0;
    for (const TM::MetadataFile::MemberRefInfo& row : file->MemberRefs()) {
        bool methodKind = MemberRefIsMethodKind(*file, row.Token);
        if (methodKind) {
            if (AccessorShaped(row.Name)) {
                // The accessor-search arm: the REAL accessor methods for
                // the resolvable parents, the FakeMethod the accessor-kind
                // guess builds for the unresolvable System.Configuration
                // parents (both engines agree on both subsets; the digest
                // over each subset is byte-exact).
                const TS::IMethod* m =
                    fx.sysPaired.ResolveMethod(row.Token, context);
                std::string accLine =
                    LinePrefix('M', row.Token) + RenderMethod(m);
                if (accLine.find("FakeMethod") != std::string::npos) {
                    accFakeFnv.Add(accLine);
                    accFake++;
                } else {
                    accRealFnv.Add(accLine);
                    accReal++;
                }
                continue;
            }
            const TS::IMethod* m =
                fx.sysPaired.ResolveMethod(row.Token, context);
            methodFnv.Add(LinePrefix('M', row.Token) + RenderMethod(m));
            methodRows++;
        } else {
            const TS::IEntity* e =
                fx.sysPaired.ResolveEntity(row.Token, context);
            const auto* f = dynamic_cast<const TS::IField*>(e);
            fieldFnv.Add(LinePrefix('F', row.Token) + RenderField(f));
            fieldRows++;
        }
    }
    EXPECT_EQ(methodRows, G::kSysMethodRows);
    EXPECT_EQ(fieldRows, G::kSysFieldRows);
    EXPECT_EQ(accReal, G::kSysAccRealCount);
    EXPECT_EQ(accRealFnv.Digest(), G::kSysAccRealDigest);
    EXPECT_EQ(accFake, G::kSysAccFakeCount);
    EXPECT_EQ(methodFnv.Digest(), G::kSysMethodDigest);
    EXPECT_EQ(fieldFnv.Digest(), G::kSysFieldDigest);
    EXPECT_EQ(accFakeFnv.Digest(), G::kSysAccFakeDigest);
}

// The accessor-shaped fake lines (the GuessFakeMethodAccessor gold): the
// guessed property / indexer / event accessor kinds over the unresolvable
// System.Configuration parents, byte-exact against the real engine.
TEST_F(ResolveMethodTest, SysPairedAccessorFakes)
{
    RmFixture fx;
    TS::GenericContext context({}, {});
    for (int i = 0; i < G::kSysAccFakeLinesCount; i++) {
        std::uint32_t token = std::stoul(
            G::kSysAccFakeLines[i].first, nullptr, 16);
        const TS::IMethod* m =
            fx.sysPaired.ResolveMethod(token, context);
        ASSERT_NE(m, nullptr) << "token " << std::hex << token;
        EXPECT_EQ(RenderMethod(m), G::kSysAccFakeLines[i].second)
            << "token " << std::hex << token;
    }
}

// The System.dll MethodSpec + MethodImpl drives.
TEST_F(ResolveMethodTest, SysPairedSpecAndImplDigests)
{
    RmFixture fx;
    const TM::MetadataFile* file = fx.sysPaired.MetadataFile();
    TS::GenericContext context({}, {});
    {
        Fnv64 fnv;
        int rows = 0;
        for (const TM::MetadataFile::MethodSpecInfo& row :
            file->MethodSpecs()) {
            const TS::IMethod* m =
                fx.sysPaired.ResolveMethod(row.Token, context);
            fnv.Add(LinePrefix('S', row.Token) + RenderMethod(m));
            rows++;
        }
        EXPECT_EQ(rows, G::kSysSpecRows);
        EXPECT_EQ(fnv.Digest(), G::kSysSpecDigest);
    }
    std::map<std::uint32_t, std::vector<std::uint32_t>> byMethod;
    for (const TM::MetadataFile::MethodImplRowInfo& row :
        file->MethodImplRows()) {
        byMethod[row.MethodBodyToken].push_back(
            row.MethodDeclarationToken);
    }
    int implMethods = 0, sameDecl = 0, memberRefDecl = 0, accessorDecl = 0;
    int accDeclFake = 0, accDeclReal = 0;
    Fnv64 sameFnv, crossPlainFnv, accDeclFakeFnv, accDeclRealFnv;
    for (const auto& kv : byMethod) {
        const TS::IMethod* method =
            fx.sysPaired.GetDefinitionMethod(kv.first);
        ASSERT_NE(method, nullptr);
        implMethods++;
        bool allSameModule = true;
        bool anyAccessor = false;
        for (std::uint32_t decl : kv.second) {
            if ((decl & 0xFF000000u) != 0x06000000u) {
                allSameModule = false;
                if ((decl & 0xFF000000u) == 0x0A000000u) {
                    auto memberRef = file->GetMemberReference(decl);
                    if (memberRef && AccessorShaped(memberRef->Name))
                        anyAccessor = true;
                }
            }
        }
        if (allSameModule) {
            sameDecl++;
            sameFnv.Add(ImplDriveLine(kv.first, method));
        } else {
            memberRefDecl++;
            if (anyAccessor) {
                // The accessor-search partition (the msc body's note).
                std::string accLine = ImplDriveLine(kv.first, method);
                if (accLine.find("FakeMethod") != std::string::npos) {
                    accDeclFakeFnv.Add(accLine);
                    accDeclFake++;
                } else {
                    accDeclRealFnv.Add(accLine);
                    accDeclReal++;
                }
                accessorDecl++;
            } else {
                crossPlainFnv.Add(ImplDriveLine(kv.first, method));
            }
        }
    }
    EXPECT_EQ(accDeclFake, G::kSysAccDeclFakeCount);
    EXPECT_EQ(accDeclFakeFnv.Digest(), G::kSysAccDeclFakeDigest);
    EXPECT_EQ(accDeclReal, G::kSysAccDeclRealCount);
    EXPECT_EQ(accDeclRealFnv.Digest(), G::kSysAccDeclRealDigest);
    EXPECT_EQ(implMethods, G::kSysImplMethods);
    EXPECT_EQ(sameDecl, G::kSysSameDeclMethods);
    EXPECT_EQ(memberRefDecl, G::kSysMemberRefDeclMethods);
    EXPECT_EQ(accessorDecl, G::kSysAccessorDeclMethods);
    EXPECT_EQ(sameFnv.Digest(), G::kSysSameDigest);
    EXPECT_EQ(crossPlainFnv.Digest(), G::kSysCrossPlainDigest);
}

// The MethodImpl sample drives (the real engine's exact lines, " ~ "
// separating the member lines) -- the same-module declarations resolve
// end-to-end (the MethodDef declaration arm of ResolveMethod).
TEST_F(ResolveMethodTest, MscMethodImplSamples)
{
    RmFixture fx;
    for (int i = 0; i < G::kMscImplSampleCount; i++) {
        std::string sample = G::kMscImplSamples[i];
        std::string tokenText = sample.substr(0, 8);
        std::uint32_t token = std::stoul(tokenText, nullptr, 16);
        const TS::IMethod* method =
            fx.mscSingle.GetDefinitionMethod(token);
        ASSERT_NE(method, nullptr);
        std::string line = ImplDriveLine(token, method);
        // The probe prints the member lines with " ~ " as the separator
        // (every newline replaced).
        std::size_t sep;
        while ((sep = line.find('\n')) != std::string::npos)
            line = line.replace(sep, 1, " ~ ");
        EXPECT_EQ(line, sample) << "sample " << i;
    }
}
