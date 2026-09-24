// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files ("the Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The MetadataProperty / MetadataEvent test suite (gold-pinned against the
// real ICSharpCode.Decompiler 11.0 via the C:/temp-probe/PeProbe gold
// probe):
//   * the whole-corpus FNV-1a-64 digests over EVERY property (5011 mscorlib
//     / 4089 System.dll / 5581 CoreLib) and EVERY event (33 / 115 / 32) of
//     the three corpora -- every name, symbolKind, accessibility, flag
//     matrix, decoded return type and parameters (with the reference kinds),
//     the accessor tokens, the AccessorOwner round-trip, and the explicitly
//     implemented interface members byte-exact;
//   * the 84-line curated gold block (TestFixtures/PeGold.hpp): the
//     [DefaultMember] indexers, List`1's explicit-interface property set,
//     System.dll's fake-property dotted names, the attribute lists and the
//     HasAttribute / GetAttribute matrices, the base-copy accessibility arm,
//     the ToString / Equals / Specialize identity contracts, the event
//     drives, and the GetFilteredAccessors composition;
//   * the entity-cache identities (GetDefinitionProperty / GetDefinitionEvent
//     twice, the nil and out-of-range arms, the Uncached variant, Equals'
//     handle + module-file identity, the ResolveEntity 0x14 / 0x17 arms, the
//     MetadataMethod::AccessorOwner routing).

#include "Decompiler/Metadata/MetadataFile.hpp"
#include <cstdlib>
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataEvent.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataProperty.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "TestFixtures/PeGold.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <utility>
#include <optional>
#include <stdexcept>
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

// A module reference resolving to an externally-owned module (the C#
// `PEFile : IModuleReference` shape the gold probe drives -- the
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

// The port's SimpleCompilation with the protected Init exposed (the
// TestCompilation pattern).
class TestCompilation : public TS::SimpleCompilation {
public:
    TestCompilation() = default;
    void Initialize(const TS::IModuleReference& main,
                   std::vector<const TS::IModuleReference*> refs) {
        Init(main, std::move(refs));
    }
};

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

std::string Quote(const std::string& utf8) {
    std::u16string utf16 = ILSpy::Decompiler::Util::Utf8ToUtf16(utf8);
    std::string out = "\"";
    char buffer[16];
    for (char16_t ch : utf16) {
        if (ch == u'\\') {
            out += "\\\\";
        } else if (ch == u'"') {
            out += "\\\"";
        } else if (ch == u'\n') {
            out += "\\n";
        } else if (ch == u'\r') {
            out += "\\r";
        } else if (ch == u'\t') {
            out += "\\t";
        } else if (ch < 32 || ch > 126) {
            std::snprintf(buffer, sizeof(buffer), "\\u%04x",
                static_cast<unsigned>(ch));
            out += buffer;
        } else {
            out += static_cast<char>(ch);
        }
    }
    return out + "\"";
}

// The C# `bool.ToString()` spelling (a std::string so the line
// concatenations compose).
std::string BoolSpelling(bool b) { return b ? "True" : "False"; }

// The C# `Accessibility.ToString()` spelling.
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

// The C# `SymbolKind.ToString()` spelling (every member here is a Property
// or an Indexer).
const char* SymbolKindSpelling(TS::SymbolKind k) {
    if (k == TS::SymbolKind::Indexer)
        return "Indexer";
    if (k == TS::SymbolKind::Property)
        return "Property";
    return "<other>";
}

// The C# `ReferenceKind.ToString()` spelling.
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

// The C# `MethodSemanticsAttributes.ToString()` spellings the corpus
// reaches (the BCL enum has no None member -- the .NET ToString for the
// value 0 renders the decimal "0").
const char* AccessorKindSpelling(TS::MethodSemanticsAttributes k) {
    switch (k) {
        case TS::MethodSemanticsAttributes::None: return "0";
        case TS::MethodSemanticsAttributes::Setter: return "Setter";
        case TS::MethodSemanticsAttributes::Getter: return "Getter";
        case TS::MethodSemanticsAttributes::Other: return "Other";
        case TS::MethodSemanticsAttributes::Adder: return "Adder";
        case TS::MethodSemanticsAttributes::Remover: return "Remover";
        case TS::MethodSemanticsAttributes::Raiser: return "Raiser";
    }
    return "<unknown>";
}

std::string TokenString(std::uint32_t token) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", token);
    return buffer;
}

// The probe's `GetType().Name` render for the AccessorOwner OTHER arm (the
// common concrete classes; the corpus digests never reach it -- the census
// pinned zero AccessorOwner round-trip violations).
std::string MemberClassName(const TS::IMember* m) {
    if (dynamic_cast<const TI::MetadataProperty*>(m) != nullptr)
        return "MetadataProperty";
    if (dynamic_cast<const TI::MetadataEvent*>(m) != nullptr)
        return "MetadataEvent";
    if (dynamic_cast<const TI::FakeProperty*>(m) != nullptr)
        return "FakeProperty";
    if (dynamic_cast<const TI::FakeEvent*>(m) != nullptr)
        return "FakeEvent";
    return "Member";
}

// The probe's AccessorOwner render for one accessor: "-" for a null
// accessor or a null owner, "self" when the owner is the member itself.
std::string AoRender(const TS::IMethod* accessor, const TS::IMember* self) {
    if (accessor == nullptr)
        return "-";
    const TS::IMember* owner = accessor->AccessorOwner();
    if (owner == nullptr)
        return "-";
    if (owner == self)
        return "self";
    return "OTHER:" + MemberClassName(owner);
}

// The probe's EII render: "n=<count>" plus ":<ReflectionName>" per member.
std::string EimRender(const TS::IMember* member) {
    std::vector<const TS::IMember*> ms
        = member->ExplicitlyImplementedInterfaceMembers();
    std::string out = "n=" + std::to_string(ms.size());
    for (const TS::IMember* m : ms) {
        out += ":<";
        out += m == nullptr ? std::string("null") : m->ReflectionName();
        out += ">";
    }
    return out;
}

// The probe's OutProp: one line per property with the full surface.
std::string PropLine(const TS::IProperty* p, const std::string& id) {
    std::string dt = p->DeclaringType() != nullptr
        ? p->DeclaringType()->ReflectionName()
        : std::string("<null>");
    std::string ps;
    for (const TS::IParameter* par : p->Parameters()) {
        ps += par->Name();
        ps += "|";
        ps += ReferenceKindSpelling(par->ReferenceKind());
        ps += "|<";
        ps += par->Type().ReflectionName();
        ps += ">;";
    }
    std::string gt = p->Getter() != nullptr
        ? TokenString(p->Getter()->MetadataToken())
        : std::string("-");
    std::string st = p->Setter() != nullptr
        ? TokenString(p->Setter()->MetadataToken())
        : std::string("-");
    std::string ao = AoRender(p->Getter(), p) + "/"
        + AoRender(p->Setter(), p);
    return "P " + id + " dt=<" + dt + "> n=" + Quote(p->Name())
        + " kind=" + SymbolKindSpelling(p->SymbolKind())
        + " acc=" + AccessibilitySpelling(p->Accessibility())
        + " get=" + BoolSpelling(p->CanGet())
        + " set=" + BoolSpelling(p->CanSet())
        + " st=" + BoolSpelling(p->IsStatic())
        + " ab=" + BoolSpelling(p->IsAbstract())
        + " sd=" + BoolSpelling(p->IsSealed())
        + " vi=" + BoolSpelling(p->IsVirtual())
        + " ov=" + BoolSpelling(p->IsOverride())
        + " ob=" + BoolSpelling(p->IsOverridable())
        + " ix=" + BoolSpelling(p->IsIndexer())
        + " rro=" + BoolSpelling(p->ReturnTypeIsRefReadOnly())
        + " eii=" + BoolSpelling(p->IsExplicitInterfaceImplementation())
        + " ret=<" + p->ReturnType().ReflectionName() + "> ps=[" + ps + "]"
        + " gt=" + gt + " st2=" + st + " ao=" + ao
        + " eim=" + EimRender(p);
}

// The probe's OutEvent: one line per event with the full surface.
std::string EventLine(const TS::IEvent* e, const std::string& id) {
    std::string dt = e->DeclaringType() != nullptr
        ? e->DeclaringType()->ReflectionName()
        : std::string("<null>");
    std::string at = e->AddAccessor() != nullptr
        ? TokenString(e->AddAccessor()->MetadataToken())
        : std::string("-");
    std::string rt = e->RemoveAccessor() != nullptr
        ? TokenString(e->RemoveAccessor()->MetadataToken())
        : std::string("-");
    std::string it = e->InvokeAccessor() != nullptr
        ? TokenString(e->InvokeAccessor()->MetadataToken())
        : std::string("-");
    std::string ao = AoRender(e->AddAccessor(), e) + "/"
        + AoRender(e->RemoveAccessor(), e);
    return "E " + id + " dt=<" + dt + "> n=" + Quote(e->Name())
        + " acc=" + AccessibilitySpelling(e->Accessibility())
        + " add=" + BoolSpelling(e->CanAdd())
        + " rem=" + BoolSpelling(e->CanRemove())
        + " inv=" + BoolSpelling(e->CanInvoke())
        + " st=" + BoolSpelling(e->IsStatic())
        + " ab=" + BoolSpelling(e->IsAbstract())
        + " sd=" + BoolSpelling(e->IsSealed())
        + " vi=" + BoolSpelling(e->IsVirtual())
        + " ov=" + BoolSpelling(e->IsOverride())
        + " ob=" + BoolSpelling(e->IsOverridable())
        + " eii=" + BoolSpelling(e->IsExplicitInterfaceImplementation())
        + " ret=<" + e->ReturnType().ReflectionName() + "> at=" + at
        + " rt=" + rt + " it=" + it + " ao=" + ao + " eim=" + EimRender(e);
}

// The probe's RenderArg over the attribute-argument box shapes (the
// AttributeListBuilder_Test recipe; the curated block only reaches the
// string arm).
std::string RenderArg(const std::any& v) {
    if (!v.has_value())
        return "null";
    if (auto b = std::any_cast<bool>(&v))
        return *b ? "bool:True" : "bool:False";
    if (auto s = std::any_cast<std::string>(&v))
        return "str:" + Quote(*s);
    if (auto t = std::any_cast<TS::ITypePtr>(&v)) {
        if (!*t)
            return "null";
        return "type:<" + (*t)->ReflectionName() + ">";
    }
    if (auto arr
        = std::any_cast<std::vector<TS::CustomAttributeTypedArgument>>(&v)) {
        std::string out = "arr:n=" + std::to_string(arr->size()) + ":[";
        for (std::size_t i = 0; i < arr->size(); i++) {
            if (i > 0)
                out += "|";
            out += RenderArg((*arr)[i].Value());
        }
        out += "]";
        return out;
    }
    if (auto boxed = std::any_cast<TS::CustomAttributeTypedArgument>(&v)) {
        std::string type = boxed->Type() != nullptr
            ? boxed->Type()->ReflectionName()
            : std::string("<null>");
        return "boxed:type=<" + type + ">:val=" + RenderArg(boxed->Value());
    }
    if (auto u8 = std::any_cast<std::uint8_t>(&v))
        return "num:Byte:" + std::to_string(*u8);
    if (auto i8 = std::any_cast<std::int8_t>(&v))
        return "num:SByte:" + std::to_string(*i8);
    if (auto i16 = std::any_cast<std::int16_t>(&v))
        return "num:Int16:" + std::to_string(*i16);
    if (auto u16 = std::any_cast<std::uint16_t>(&v))
        return "num:UInt16:" + std::to_string(*u16);
    if (auto i32 = std::any_cast<std::int32_t>(&v))
        return "num:Int32:" + std::to_string(*i32);
    if (auto u32 = std::any_cast<std::uint32_t>(&v))
        return "num:UInt32:" + std::to_string(*u32);
    if (auto i64 = std::any_cast<std::int64_t>(&v))
        return "num:Int64:" + std::to_string(*i64);
    if (auto u64 = std::any_cast<std::uint64_t>(&v))
        return "num:UInt64:" + std::to_string(*u64);
    return "other:";
}

// The C# exception-type name for a port exception (the message
// distinguishes the .NET type; the AttributeListBuilder_Test recipe).
std::string ExText(const std::exception& ex) {
    const std::string message = ex.what();
    if (message.rfind("Value cannot be null.", 0) == 0)
        return "EXCEPTION:ArgumentNullException:" + Quote(message);
    return "EXCEPTION:" + Quote(message);
}

// The probe's AttrLines over one attribute (the exact GA: form).
std::vector<std::string> AttrLines(const std::string& prefix,
                                   const TS::IAttribute* a) {
    std::vector<std::string> lines;
    std::string ctor = "<null>";
    if (a->Constructor() != nullptr)
        ctor = a->Constructor()->ReflectionName();
    std::vector<TS::CustomAttributeTypedArgument> fixedArgs
        = a->FixedArguments();
    std::vector<TS::CustomAttributeNamedArgument> namedArgs
        = a->NamedArguments();
    lines.push_back(prefix + "type=<" + a->AttributeType().ReflectionName()
        + ">:ctor=<" + ctor + ">:err="
        + BoolSpelling(a->HasDecodeErrors()) + ":F="
        + std::to_string(fixedArgs.size()) + ":N="
        + std::to_string(namedArgs.size()));
    for (std::size_t i = 0; i < fixedArgs.size(); i++) {
        const auto& arg = fixedArgs[i];
        lines.push_back(prefix + "F" + std::to_string(i) + ":type=<"
            + (arg.Type() != nullptr ? arg.Type()->ReflectionName()
                                     : std::string("null"))
            + ">:val=" + RenderArg(arg.Value()));
    }
    for (std::size_t i = 0; i < namedArgs.size(); i++) {
        const auto& arg = namedArgs[i];
        lines.push_back(prefix + "N" + std::to_string(i) + ":name="
            + arg.Name() + ":kind=0:type=<"
            + (arg.Type() != nullptr ? arg.Type()->ReflectionName()
                                     : std::string("null"))
            + ">:val=" + RenderArg(arg.Value()));
    }
    return lines;
}

// The plain ToString member needs the concrete cast (the
// mfirstHashOf precedent).
std::string ToStringOf(const TS::IMember* m)
{
    if (auto* p = dynamic_cast<const TI::MetadataProperty*>(m))
        return p->ToString();
    if (auto* e = dynamic_cast<const TI::MetadataEvent*>(m))
        return e->ToString();
    return "";
}

// The KnownAttribute subset the HasMatrix drives cover (the C#
// KnownAttribute.ToString() spellings; the AttributeListBuilder_Test
// recipe).
const std::pair<TS::KnownAttribute, const char*> kAttrSubset[] = {
    {TS::KnownAttribute::None, "None"},
    {TS::KnownAttribute::SpecialName, "SpecialName"},
    {TS::KnownAttribute::Obsolete, "Obsolete"},
    {TS::KnownAttribute::CompilerGenerated, "CompilerGenerated"},
    {TS::KnownAttribute::IndexerName, "IndexerName"},
    {TS::KnownAttribute::Nullable, "Nullable"},
    {TS::KnownAttribute::NullableContext, "NullableContext"},
    {TS::KnownAttribute::Serializable, "Serializable"},
    {TS::KnownAttribute::Extension, "Extension"},
};

// ---------------------------------------------------------------------------
// The fixture: one single-module compilation per corpus (the probe's
// `new SimpleCompilation(new PEFile(path))` sweep shape).
// ---------------------------------------------------------------------------
struct PeFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };
    TM::MetadataFile systemFile{ SystemPath() };
    TM::MetadataFile coreLibFile{ CoreLibPath() };

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

    PeFixture() {
        compMsc.Initialize(mscSingleRef, {});
        compSys.Initialize(sysSingleRef, {});
        compCore.Initialize(coreRef, {});
    }
};

class MetadataPropertyEventTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!MscorlibAvailable())
            GTEST_SKIP() << "mscorlib fixture not available";
    }
};

// The property sweep: walks every TypeDef row in table order, rendering one
// PropLine per property into the digest.
struct PropSweepResult {
    std::uint32_t propCount = 0;
    std::uint64_t fnv = 0;
};

PropSweepResult SweepProperties(const TS::MetadataModule& module,
                                const std::string& tag) {
    Fnv64 fnv;
    PropSweepResult result;
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
        for (const TS::IProperty* p : td->Properties()) {
            fnv.Add(PropLine(p, tag + ":" + TokenString(p->MetadataToken())));
            result.propCount++;
        }
    }
    result.fnv = fnv.Digest();
    return result;
}

// The event sweep: the same walk over every event.
struct EventSweepResult {
    std::uint32_t eventCount = 0;
    std::uint64_t fnv = 0;
};

EventSweepResult SweepEvents(const TS::MetadataModule& module,
                             const std::string& tag) {
    Fnv64 fnv;
    EventSweepResult result;
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
        for (const TS::IEvent* e : td->Events()) {
            fnv.Add(EventLine(e, tag + ":" + TokenString(e->MetadataToken())));
            result.eventCount++;
        }
    }
    result.fnv = fnv.Digest();
    return result;
}

// ---------------------------------------------------------------------------
// Section P / E: the whole-corpus digests.
// ---------------------------------------------------------------------------

TEST_F(MetadataPropertyEventTest, MscorlibWholeCorpusPropertiesSweep)
{
    PeFixture fx;
    PropSweepResult r = SweepProperties(fx.mscSingle, "msc");
    // DIGEST Pmsc lines=5011 fnv=62EE1A831C73E83E
    EXPECT_EQ(r.propCount, 5011u);
    EXPECT_EQ(r.fnv, 0x62EE1A831C73E83EULL);
}

TEST_F(MetadataPropertyEventTest, MscorlibWholeCorpusEventsSweep)
{
    PeFixture fx;
    EventSweepResult r = SweepEvents(fx.mscSingle, "msc");
    // DIGEST Emsc lines=33 fnv=B2D12A22A8DB6792
    EXPECT_EQ(r.eventCount, 33u);
    EXPECT_EQ(r.fnv, 0xB2D12A22A8DB6792ULL);
}

TEST_F(MetadataPropertyEventTest, SystemWholeCorpusPropertiesSweep)
{
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    PeFixture fx;
    PropSweepResult r = SweepProperties(fx.sysSingle, "sys");
    // DIGEST Psys lines=4089 fnv=58D199770B7E551F
    EXPECT_EQ(r.propCount, 4089u);
    EXPECT_EQ(r.fnv, 0x58D199770B7E551FULL);
}

TEST_F(MetadataPropertyEventTest, SystemWholeCorpusEventsSweep)
{
    if (!SystemAvailable())
        GTEST_SKIP() << "System.dll fixture not available";
    PeFixture fx;
    EventSweepResult r = SweepEvents(fx.sysSingle, "sys");
    // DIGEST Esys lines=115 fnv=A71DE505E79AB5A3
    EXPECT_EQ(r.eventCount, 115u);
    EXPECT_EQ(r.fnv, 0xA71DE505E79AB5A3ULL);
}

TEST_F(MetadataPropertyEventTest, CoreLibWholeCorpusPropertiesSweep)
{
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    PeFixture fx;
    PropSweepResult r = SweepProperties(fx.core, "core");
    // DIGEST Pcore lines=5581 fnv=F547F6B62E63A40F
    EXPECT_EQ(r.propCount, 5581u);
    EXPECT_EQ(r.fnv, 0xF547F6B62E63A40FULL);
}

TEST_F(MetadataPropertyEventTest, CoreLibWholeCorpusEventsSweep)
{
    if (!CoreLibAvailable())
        GTEST_SKIP() << "CoreLib fixture not available";
    PeFixture fx;
    EventSweepResult r = SweepEvents(fx.core, "core");
    // DIGEST Ecore lines=32 fnv=B072B25D0E7C66CC
    EXPECT_EQ(r.eventCount, 32u);
    EXPECT_EQ(r.fnv, 0xB072B25D0E7C66CCULL);
}

// ---------------------------------------------------------------------------
// The curated gold block: every C1-C12 line byte-exact.
// ---------------------------------------------------------------------------

// The probe's `FindType` (the TopLevelTypeName lookup over the module).
const TS::ITypeDefinition* FindType(const TS::MetadataModule& module,
                                    const char* ns, const char* name,
                                    int tpc = 0) {
    return module.GetTypeDefinition(
        TS::TopLevelTypeName(ns, name, tpc));
}

// The probe's DefaultMemberName render: Quote(name) or "<null>" quoted.
std::string DmnRender(const TS::ITypeDefinition* td) {
    const auto* mtd
        = dynamic_cast<const TS::Implementation::MetadataTypeDefinition*>(
            td);
    std::optional<std::string> dmn
        = mtd != nullptr ? mtd->DefaultMemberName() : std::nullopt;
    return Quote(dmn.has_value() ? *dmn : std::string("<null>"));
}

// The probe's attribute-block render (EntityAttrs).
std::vector<std::string> EntityAttrLines(const std::string& id,
                                         const TS::IMember* m) {
    std::vector<std::string> lines;
    std::vector<const TS::IAttribute*> attrs = m->GetAttributes();
    lines.push_back("A " + id + " n=" + std::to_string(attrs.size()));
    for (std::size_t i = 0; i < attrs.size(); i++)
        for (const std::string& l : AttrLines("A" + std::to_string(i) + ":",
                                              attrs[i]))
            lines.push_back(l);
    return lines;
}

TEST_F(MetadataPropertyEventTest, CuratedGoldBlock)
{
    PeFixture fx;
    std::vector<std::string> out;

    // C1: String.Chars -- the [DefaultMember] indexer.
    const TS::ITypeDefinition* str
        = FindType(fx.mscSingle, "System", "String");
    ASSERT_NE(str, nullptr);
    out.push_back("C1 String.DefaultMemberName=" + DmnRender(str));
    for (const TS::IProperty* p : str->Properties())
        if (p->Name() == "Chars")
            out.push_back(PropLine(p, "C1"));

    // C2: StringBuilder.Chars.
    const TS::ITypeDefinition* sb
        = FindType(fx.mscSingle, "System.Text", "StringBuilder");
    ASSERT_NE(sb, nullptr);
    out.push_back("C2 StringBuilder.DefaultMemberName=" + DmnRender(sb));
    for (const TS::IProperty* p : sb->Properties())
        if (p->Name() == "Chars" || p->Name() == "Item")
            out.push_back(PropLine(p, "C2"));

    // C3: List`1's full property set.
    const TS::ITypeDefinition* list
        = FindType(fx.mscSingle, "System.Collections.Generic", "List", 1);
    ASSERT_NE(list, nullptr);
    for (const TS::IProperty* p : list->Properties())
        out.push_back(PropLine(p, "C3"));

    // C4: System.dll's first 3 dotted+eii properties in table order.
    {
        const TM::MetadataFile* file = fx.sysSingle.MetadataFile();
        std::uint32_t rowCount
            = file->CorTableRowCount(TM::CorTableIndex::TypeDef);
        int nFake = 0;
        for (std::uint32_t row = 1; row <= rowCount && nFake < 3; row++) {
            const TS::ITypeDefinition* td
                = fx.sysSingle.GetDefinition(0x02000000u | row);
            if (td == nullptr)
                break;
            for (const TS::IProperty* p : td->Properties()) {
                if (nFake < 3 && p->Name().find('.') != std::string::npos
                    && p->IsExplicitInterfaceImplementation()) {
                    out.push_back(PropLine(p, "C4"));
                    nFake++;
                }
            }
        }
    }

    // C5: the attribute lists + the HasMatrix over String.Chars.
    const TS::IProperty* chars = nullptr;
    for (const TS::IProperty* p : str->Properties())
        if (p->Name() == "Chars")
            chars = p;
    ASSERT_NE(chars, nullptr);
    const TS::IProperty* listEii = nullptr;
    for (const TS::IProperty* p : list->Properties())
        if (p->Name().find('.') != std::string::npos) {
            listEii = p;
            break;
        }
    ASSERT_NE(listEii, nullptr);
    for (const std::string& l : EntityAttrLines("C5:msc:Chars", chars))
        out.push_back(l);
    for (const std::string& l : EntityAttrLines("C5:msc:ListEii", listEii))
        out.push_back(l);
    {
        for (const auto& [ka, spelling] : kAttrSubset) {
            try {
                out.push_back("H C5:msc:Chars " + std::string(spelling)
                    + "=" + BoolSpelling(chars->HasAttribute(ka)));
            } catch (const std::invalid_argument& ex) {
                out.push_back("H C5:msc:Chars " + std::string(spelling)
                    + "=" + ExText(ex));
            }
        }
        for (const auto& [ka, spelling] : kAttrSubset) {
            const TS::IAttribute* a = nullptr;
            try {
                a = chars->GetAttribute(ka);
            } catch (const std::invalid_argument& ex) {
                out.push_back("G C5:msc:Chars " + std::string(spelling)
                    + " " + ExText(ex));
                continue;
            }
            if (a == nullptr) {
                out.push_back("G C5:msc:Chars " + std::string(spelling)
                    + " null");
                continue;
            }
            out.push_back("G C5:msc:Chars " + std::string(spelling)
                + " found");
            for (const std::string& l : AttrLines("GA:", a))
                out.push_back(l);
        }
    }

    // C6: the first IsOverride single-accessor property of mscorlib.
    {
        const TM::MetadataFile* file = fx.mscSingle.MetadataFile();
        std::uint32_t rowCount
            = file->CorTableRowCount(TM::CorTableIndex::TypeDef);
        for (std::uint32_t row = 1; row <= rowCount; row++) {
            const TS::ITypeDefinition* td
                = fx.mscSingle.GetDefinition(0x02000000u | row);
            if (td == nullptr)
                break;
            bool found = false;
            for (const TS::IProperty* p : td->Properties()) {
                if (p->IsOverride()
                    && (p->Getter() == nullptr || p->Setter() == nullptr)) {
                    out.push_back(PropLine(p, "C6"));
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
    }

    // C7: the ToString renders.
    {
        const TS::IProperty* length = nullptr;
        for (const TS::IProperty* p : str->Properties())
            if (p->Name() == "Length")
                length = p;
        ASSERT_NE(length, nullptr);
        out.push_back("C7 " + ToStringOf(chars));
        out.push_back("C7 " + ToStringOf(length));
        out.push_back("C7 " + ToStringOf(list->Properties().front()));
    }

    // C8: the Equals identity (a second compilation over a FRESH file
    // instance of the same path -- the C# `new PEFile(path)`: the
    // module-file identity is the OBJECT, not the path).
    {
        TM::MetadataFile mscorlibFile2{ MscorlibPath() };
        TestCompilation comp2;
        TS::MetadataModule msc2{ comp2, &mscorlibFile2,
                                  TS::TypeSystemOptions::Default };
        FixedModuleRef msc2Ref{ &msc2 };
        comp2.Initialize(msc2Ref, {});
        const TS::ITypeDefinition* str2
            = FindType(msc2, "System", "String");
        ASSERT_NE(str2, nullptr);
        const TS::IProperty* chars2 = nullptr;
        for (const TS::IProperty* p : str2->Properties())
            if (p->Name() == "Chars")
                chars2 = p;
        ASSERT_NE(chars2, nullptr);
        const auto* mc1 = dynamic_cast<const TI::MetadataProperty*>(chars);
        const auto* mc2 = dynamic_cast<const TI::MetadataProperty*>(chars2);
        ASSERT_NE(mc1, nullptr);
        ASSERT_NE(mc2, nullptr);
        out.push_back("C8 self=" + BoolSpelling(mc1->Equals(mc1))
            + " cross=" + BoolSpelling(mc1->Equals(mc2))
            + " crossType=False");
    }

    // C9: the Identity Specialize same-instance contract.
    {
        const TS::IMember* sp
            = chars->Specialize(&TS::TypeParameterSubstitution::Identity());
        out.push_back("C9 spIdentity="
            + BoolSpelling(sp == chars)
            + " spType=" + (sp == chars
                ? std::string("MetadataProperty")
                : MemberClassName(sp)));
    }

    // C10: the first event of mscorlib (table order) with its attributes,
    // matrix, ToString, and Specialize.
    const TS::IEvent* firstEvent = nullptr;
    {
        const TM::MetadataFile* file = fx.mscSingle.MetadataFile();
        std::uint32_t rowCount
            = file->CorTableRowCount(TM::CorTableIndex::TypeDef);
        for (std::uint32_t row = 1; row <= rowCount; row++) {
            const TS::ITypeDefinition* td
                = fx.mscSingle.GetDefinition(0x02000000u | row);
            if (td == nullptr)
                break;
            if (!td->Events().empty()) {
                firstEvent = td->Events().front();
                break;
            }
        }
    }
    ASSERT_NE(firstEvent, nullptr);
    out.push_back(EventLine(firstEvent, "C10"));
    for (const std::string& l : EntityAttrLines("C10:msc:firstEvent",
                                                firstEvent))
        out.push_back(l);
    {
        for (const auto& [ka, spelling] : kAttrSubset) {
            try {
                out.push_back("H C10:msc:firstEvent "
                    + std::string(spelling) + "="
                    + BoolSpelling(firstEvent->HasAttribute(ka)));
            } catch (const std::invalid_argument& ex) {
                out.push_back("H C10:msc:firstEvent " + std::string(spelling)
                    + "=" + ExText(ex));
            }
        }
        for (const auto& [ka, spelling] : kAttrSubset) {
            const TS::IAttribute* a = firstEvent->GetAttribute(ka);
            if (a == nullptr) {
                out.push_back("G C10:msc:firstEvent " + std::string(spelling)
                    + " null");
                continue;
            }
            out.push_back("G C10:msc:firstEvent " + std::string(spelling)
                + " found");
            for (const std::string& l : AttrLines("GA:", a))
                out.push_back(l);
        }
    }
    out.push_back("C10 " + ToStringOf(firstEvent));
    {
        const TS::IMember* spe = firstEvent->Specialize(
            &TS::TypeParameterSubstitution::Identity());
        out.push_back("C10 spIdentity=" + BoolSpelling(spe == firstEvent)
            + " spType=" + (spe == firstEvent
                ? std::string("MetadataEvent")
                : MemberClassName(spe)));
    }

    // C10sys: the first event of System.dll.
    {
        const TM::MetadataFile* file = fx.sysSingle.MetadataFile();
        std::uint32_t rowCount
            = file->CorTableRowCount(TM::CorTableIndex::TypeDef);
        for (std::uint32_t row = 1; row <= rowCount; row++) {
            const TS::ITypeDefinition* td
                = fx.sysSingle.GetDefinition(0x02000000u | row);
            if (td == nullptr)
                break;
            if (!td->Events().empty()) {
                out.push_back(EventLine(td->Events().front(), "C10sys"));
                break;
            }
        }
    }

    // C11: the DefaultMemberName drives + ArrayList's property set.
    const TS::ITypeDefinition* obj
        = FindType(fx.mscSingle, "System", "Object");
    ASSERT_NE(obj, nullptr);
    const TS::ITypeDefinition* arrayList
        = FindType(fx.mscSingle, "System.Collections", "ArrayList");
    ASSERT_NE(arrayList, nullptr);
    out.push_back("C11 msc:String=" + DmnRender(str));
    out.push_back("C11 msc:StringBuilder=" + DmnRender(sb));
    out.push_back("C11 msc:Object=" + DmnRender(obj));
    out.push_back("C11 msc:ArrayList=" + DmnRender(arrayList));
    for (const TS::IProperty* p : arrayList->Properties())
        out.push_back(PropLine(p, "C11"));

    // C12: the GetFilteredAccessors composition.
    {
        std::vector<const TS::IMethod*> accessors
            = list->GetAccessors(nullptr,
                TS::GetMemberOptions::IgnoreInheritedMembers);
        std::string names;
        for (const TS::IMethod* m : accessors) {
            if (!names.empty())
                names += ";";
            names += m->Name() + ":" + AccessorKindSpelling(m->AccessorKind());
        }
        out.push_back("C12 list accessors=[" + names + "]");
    }
    {
        const TS::ITypeDefinition* td = firstEvent->DeclaringTypeDefinition();
        ASSERT_NE(td, nullptr);
        std::vector<const TS::IMethod*> accessors
            = td->GetAccessors(nullptr,
                TS::GetMemberOptions::IgnoreInheritedMembers);
        std::string names;
        for (const TS::IMethod* m : accessors) {
            if (!names.empty())
                names += ";";
            names += m->Name() + ":" + AccessorKindSpelling(m->AccessorKind());
        }
        out.push_back("C12 " + td->ReflectionName() + " accessors=[" + names
            + "]");
    }

    // The comparison: every line byte-exact against the gold block.
    ASSERT_EQ(out.size(), ILSpy::Tests::PeGoldLines.size())
        << "curated line count";
    for (std::size_t i = 0; i < out.size(); i++) {
        EXPECT_EQ(out[i], std::string(ILSpy::Tests::PeGoldLines[i]))
            << "curated line " << i << ": " << out[i];
    }
}

// ---------------------------------------------------------------------------
// The entity-cache identities, the ResolveEntity arms, the AccessorOwner
// routing.
// ---------------------------------------------------------------------------

TEST_F(MetadataPropertyEventTest, EntityCacheAndIdentity)
{
    PeFixture fx;
    // The cached arm: two lookups hand the same instance.
    const TS::IProperty* p1 = fx.mscSingle.GetDefinitionProperty(0x17000002);
    const TS::IProperty* p2 = fx.mscSingle.GetDefinitionProperty(0x17000002);
    ASSERT_NE(p1, nullptr);
    EXPECT_EQ(p1, p2);
    // The nil token -> null.
    EXPECT_EQ(fx.mscSingle.GetDefinitionProperty(0), nullptr);
    EXPECT_EQ(fx.mscSingle.GetDefinitionEvent(0), nullptr);
    // The out-of-range arm: 'Handle with invalid row number.'
    std::uint32_t propRows = fx.mscSingle.MetadataFile()->CorTableRowCount(
        TM::CorTableIndex::Property);
    std::uint32_t eventRows = fx.mscSingle.MetadataFile()->CorTableRowCount(
        TM::CorTableIndex::Event);
    EXPECT_THROW(fx.mscSingle.GetDefinitionProperty(
                     0x17000000u | (propRows + 1)),
                 std::out_of_range);
    EXPECT_THROW(fx.mscSingle.GetDefinitionEvent(
                     0x14000000u | (eventRows + 1)),
                 std::out_of_range);
    // The event cached arm.
    const TS::IEvent* e1 = fx.mscSingle.GetDefinitionEvent(0x14000001);
    const TS::IEvent* e2 = fx.mscSingle.GetDefinitionEvent(0x14000001);
    ASSERT_NE(e1, nullptr);
    EXPECT_EQ(e1, e2);
    // The Uncached arm: fresh instances per call, both kept alive.
    {
        TestCompilation comp;
        TS::MetadataModule unc{ comp, &fx.mscorlibFile,
                                TS::TypeSystemOptions::Uncached };
        FixedModuleRef uncRef{ &unc };
        comp.Initialize(uncRef, {});
        const TS::IProperty* u1 = unc.GetDefinitionProperty(0x17000002);
        const TS::IProperty* u2 = unc.GetDefinitionProperty(0x17000002);
        ASSERT_NE(u1, nullptr);
        EXPECT_NE(u1, u2);
        EXPECT_EQ(u1->Name(), u2->Name());
        const TS::IEvent* v1 = unc.GetDefinitionEvent(0x14000001);
        const TS::IEvent* v2 = unc.GetDefinitionEvent(0x14000001);
        ASSERT_NE(v1, nullptr);
        EXPECT_NE(v1, v2);
        EXPECT_EQ(v1->Name(), v2->Name());
    }
    // Equals: the handle + module-file identity (the same row in a DIFFERENT
    // MetadataFile instance is not equal).
    {
        TM::MetadataFile mscorlibFile2{ MscorlibPath() };
        TestCompilation comp2;
        TS::MetadataModule msc2{ comp2, &mscorlibFile2,
                                 TS::TypeSystemOptions::Default };
        FixedModuleRef msc2Ref{ &msc2 };
        comp2.Initialize(msc2Ref, {});
        const TS::IProperty* pOther = msc2.GetDefinitionProperty(0x17000002);
        const TS::IEvent* eOther = msc2.GetDefinitionEvent(0x14000001);
        const auto* mp = dynamic_cast<const TI::MetadataProperty*>(p1);
        const auto* mpOther
            = dynamic_cast<const TI::MetadataProperty*>(pOther);
        const auto* me = dynamic_cast<const TI::MetadataEvent*>(e1);
        const auto* meOther = dynamic_cast<const TI::MetadataEvent*>(eOther);
        ASSERT_NE(mp, nullptr);
        ASSERT_NE(mpOther, nullptr);
        ASSERT_NE(me, nullptr);
        ASSERT_NE(meOther, nullptr);
        EXPECT_TRUE(mp->Equals(mp));
        EXPECT_FALSE(mp->Equals(mpOther));
        EXPECT_TRUE(me->Equals(me));
        EXPECT_FALSE(me->Equals(meOther));
    }
    // ToString: the '%08X DeclaringType.ReflectionName.Name' render.
    const auto* mp = dynamic_cast<const TI::MetadataProperty*>(p1);
    ASSERT_NE(mp, nullptr);
    EXPECT_EQ(mp->ToString(), "17000002 Microsoft.Win32.RegistryKey.SubKeyCount");
    const auto* me = dynamic_cast<const TI::MetadataEvent*>(e1);
    ASSERT_NE(me, nullptr);
    EXPECT_EQ(me->ToString(), "14000001 System.Exception.SerializeObjectState");
    // MemberDefinition / Substitution.
    EXPECT_EQ(p1->MemberDefinition(), p1);
    EXPECT_EQ(e1->MemberDefinition(), e1);
    EXPECT_EQ(p1->Substitution(), &TS::TypeParameterSubstitution::Identity());
    EXPECT_EQ(e1->Substitution(), &TS::TypeParameterSubstitution::Identity());
}

TEST_F(MetadataPropertyEventTest, ResolveEntityPropertyEventArms)
{
    PeFixture fx;
    // The property arm: the same entity the cache hands back.
    const TS::IProperty* p = fx.mscSingle.GetDefinitionProperty(0x17000002);
    ASSERT_NE(p, nullptr);
    const TS::IEntity* resolved = fx.mscSingle.ResolveEntity(0x17000002,
        TS::GenericContext());
    EXPECT_EQ(resolved, p);
    // The event arm.
    const TS::IEvent* e = fx.mscSingle.GetDefinitionEvent(0x14000001);
    ASSERT_NE(e, nullptr);
    resolved = fx.mscSingle.ResolveEntity(0x14000001, TS::GenericContext());
    EXPECT_EQ(resolved, e);
}

TEST_F(MetadataPropertyEventTest, AccessorOwnerRouting)
{
    PeFixture fx;
    // The property: both accessors' AccessorOwner is the property itself,
    // with the matching AccessorKind.
    const TS::IProperty* p = fx.mscSingle.GetDefinitionProperty(0x17000002);
    ASSERT_NE(p, nullptr);
    ASSERT_NE(p->Getter(), nullptr);
    EXPECT_EQ(p->Getter()->AccessorOwner(), p);
    EXPECT_EQ(p->Getter()->AccessorKind(),
              TS::MethodSemanticsAttributes::Getter);
    EXPECT_TRUE(p->Getter()->IsAccessor());
    // The event: the adder / remover pair routes to the event.
    const TS::IEvent* e = fx.mscSingle.GetDefinitionEvent(0x14000001);
    ASSERT_NE(e, nullptr);
    ASSERT_NE(e->AddAccessor(), nullptr);
    ASSERT_NE(e->RemoveAccessor(), nullptr);
    EXPECT_EQ(e->AddAccessor()->AccessorOwner(), e);
    EXPECT_EQ(e->AddAccessor()->AccessorKind(),
              TS::MethodSemanticsAttributes::Adder);
    EXPECT_EQ(e->RemoveAccessor()->AccessorOwner(), e);
    EXPECT_EQ(e->RemoveAccessor()->AccessorKind(),
              TS::MethodSemanticsAttributes::Remover);
    // A non-accessor method answers null without touching the caches.
    const TS::ITypeDefinition* str
        = FindType(fx.mscSingle, "System", "String");
    ASSERT_NE(str, nullptr);
    for (const TS::IMethod* m : str->Methods()) {
        if (m->Name() == "Copy") {
            EXPECT_EQ(m->AccessorOwner(), nullptr);
            EXPECT_FALSE(m->IsAccessor());
        }
    }
}

} // namespace
