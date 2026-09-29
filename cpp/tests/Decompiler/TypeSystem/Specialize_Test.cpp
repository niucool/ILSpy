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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// The owning-Specialize test suite (gold-pinned against the real
// ICSharpCode.Decompiler 11.0 via the C:/temp-probe/SpzProbe gold probe):
//   * the SpecializedMethod::Create arm matrix over MetadataMethod.Specialize
//     (the identity / declaring-tpc-0 same-instance arms, the class-only fresh
//     arm, the method-args-dropped arm, the generic-method
//     specialized-type-parameter machinery, the static-generic-on-non-generic
//     shape, the re-Specialize compose path, the 2-arg class substitution,
//     the two-fresh-calls distinct instances);
//   * the Equals shapes (the same def+substitution instances equal, the def
//     and a different-substitution instance not equal);
//   * the MetadataField.Specialize matrix (identity / tpc-0 / the substituted
//     field type);
//   * the FakeMethod/FakeField/FakeProperty/FakeEvent general arms (incl. the
//     ArrayType declaring-type Create arm only a fake can reach, and the null
//     DeclaringType NRE);
//   * the FNV-1a-64 digests over every method + field of List`1 /
//     Dictionary`2 / String specialized with the full class substitution.
// Every expected line is byte-identical to the probe's gold.txt output.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include <cstdlib>
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include "LookupStubs.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace TM = ILSpy::Decompiler::Metadata;
namespace TI = ILSpy::Decompiler::TypeSystem::Implementation;
namespace LS = ILSpy::Decompiler::TypeSystem::TestSupport;

const char* MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    if (const char* env = std::getenv("ILSPY_TEST_MSCORLIB"); env != nullptr)
        return env;
    return "/usr/lib/mono/4.5/mscorlib.dll";
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
// The fixture (the probe's shape: the single-mscorlib compilation).
// ---------------------------------------------------------------------------
struct SpzFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };

    TestCompilation comp;
    TS::MetadataModule msc{ comp, &mscorlibFile,
                            TS::TypeSystemOptions::Default };
    FixedModuleRef mscRef{ &msc };

    SpzFixture() { comp.Initialize(mscRef, {}); }

    const TS::ITypeDefinition* Type(const char* ns, const char* name,
                                    int arity = 0) {
        const TS::ITypeDefinition* d = msc.GetTypeDefinition(
            TS::TopLevelTypeName(ns, name, arity));
        EXPECT_NE(d, nullptr)
            << "fixture type " << ns << "." << name << "`" << arity;
        return d;
    }

    // The probe's `GetMethod`: the n-th `GetMethods(filter,
    // IgnoreInheritedMembers)` entry with the given name (the
    // accessor-dropped, constructor-filtered enumeration).
    const TS::IMethod* GetMethod(const TS::ITypeDefinition* type,
                                 const char* name, int index, int tpc = -1) {
        auto methods = type->GetMethods(
            [name, tpc](const TS::IMethod* m) {
                return m->Name() == name
                    && (tpc < 0
                        || static_cast<int>(m->TypeParameters().size())
                            == tpc);
            },
            TS::GetMemberOptions::IgnoreInheritedMembers);
        EXPECT_LT(static_cast<std::size_t>(index), methods.size())
            << "fixture method " << name << "#" << index;
        return methods.at(static_cast<std::size_t>(index));
    }

    // The probe's `GetField`: the first `Fields` entry with the given name.
    // A null return means the selected mscorlib does not carry the
    // field (a REFERENCE assembly strips the private fields -- the
    // net48 corpus mscorlib's List`1 has no _items/_size), so the
    // callers gate on the null and skip rather than dereference.
    const TS::IField* GetField(const TS::ITypeDefinition* type,
                               const char* name) {
        if (type == nullptr)
            return nullptr;
        for (const TS::IField* f : type->Fields()) {
            if (f->Name() == name)
                return f;
        }
        return nullptr;
    }

    // The no-op-deleter alias over a module-owned type (the FakeMember.cpp
    // CreateDummyConstructor convention).
    TS::ITypePtr Alias(const TS::IType* type) {
        return TS::ITypePtr(const_cast<TS::IType*>(type), [](TS::IType*) {
            // no-op: the module's entity cache owns it
        });
    }

    // The List`1 definition's first type parameter as the same aliasing
    // handle (the probe's `tParam`).
    TS::ITypePtr ListTypeParameter(const TS::ITypeDefinition* list) {
        auto tps = list->TypeParameters();
        EXPECT_FALSE(tps.empty());
        return Alias(tps.front());
    }

    TS::ITypePtr KnownType(TS::KnownTypeCode code) {
        const TS::IType& t = comp.FindType(code);
        return TS::ITypePtr(const_cast<TS::IType*>(&t), [](TS::IType*) {
            // no-op: the compilation's type system owns the known type
        });
    }
};

// ---------------------------------------------------------------------------
// The render helpers -- each mirrors the gold probe's Program.cs shapes
// byte-for-byte (the TypesLine / TpsLine / ParamsLine / MethodLine /
// MemberLine / PropLine / EventLine / Fnv octet).
// ---------------------------------------------------------------------------

std::string TypesLine(const std::vector<TS::ITypePtr>& types) {
    std::string b = "[";
    bool first = true;
    for (const auto& t : types) {
        if (first)
            first = false;
        else
            b += ", ";
        b += t->ReflectionName();
    }
    b += "]";
    return b;
}

const char* TypeKindSpelling(TS::TypeKind k) {
    switch (k) {
        case TS::TypeKind::TypeParameter: return "TypeParameter";
        case TS::TypeKind::Class: return "Class";
        case TS::TypeKind::Struct: return "Struct";
        case TS::TypeKind::Interface: return "Interface";
        case TS::TypeKind::Array: return "Array";
        default: return "<other>";
    }
}

std::string TpsLine(const std::vector<const TS::ITypeParameter*>& tps) {
    std::string b = "[";
    bool first = true;
    for (const auto* tp : tps) {
        if (first)
            first = false;
        else
            b += ", ";
        b += tp->Name();
        b += ":";
        b += TypeKindSpelling(tp->Kind());
    }
    b += "]";
    return b;
}

std::string ParamsLine(const std::vector<const TS::IParameter*>& ps) {
    std::string b = "[";
    bool first = true;
    for (const auto* p : ps) {
        if (first)
            first = false;
        else
            b += ", ";
        b += "(";
        b += p->Name();
        b += ":";
        b += p->Type().ReflectionName();
        b += ")";
    }
    b += "]";
    return b;
}

const char* SymbolKindSpelling(TS::SymbolKind k) {
    switch (k) {
        case TS::SymbolKind::Method: return "Method";
        case TS::SymbolKind::Field: return "Field";
        case TS::SymbolKind::Property: return "Property";
        case TS::SymbolKind::Event: return "Event";
        case TS::SymbolKind::Constructor: return "Constructor";
        case TS::SymbolKind::Operator: return "Operator";
        case TS::SymbolKind::Accessor: return "Accessor";
        default: return "<other>";
    }
}

// The C# `DefaultTypeParameter`'s ReflectionName rule (the AbstractTypeParameter
// form: `"``" + index` for a method-owner parameter) -- a local stub deriving
// LookupTypeParameter, overriding only the reflection name. The shipped
// internal DefaultTypeParameter class is not yet ported; the STP wrap this
// slice's drives exercise reads only Name/Index, and the fake method's own
// TypeArguments render reads the ReflectionName.
class MethodTypeParameterStub final : public LS::LookupTypeParameter {
public:
    explicit MethodTypeParameterStub(std::string name)
        : LS::LookupTypeParameter(std::move(name)) {}
    std::string ReflectionName() const override {
        return "``" + std::to_string(Index());
    }
};

std::string DeclaringTypeName(const TS::IMember* m) {
    TS::ITypePtr declaringType = m->DeclaringType();
    return declaringType ? declaringType->ReflectionName()
                         : std::string("<null>");
}

// The probe's `MethodLine(IMethod m, IMethod def)` (both `IMethod` references
// compare directly -- the port's Specialize returns the `IMethod` view).
// `memberDefView` overrides the `MemberDefinition()` comparison view for the
// FAKE drives (the `FakeMember` base's view; null for the metadata drives where
// the `IMethod` view agrees).
std::string MethodLine(const TS::IMethod* m, const TS::IMethod* def,
                       const TS::IMember* memberDefView = nullptr) {
    const TS::IMember* defView = (memberDefView != nullptr)
        ? memberDefView
        : static_cast<const TS::IMember*>(def);
    std::string b;
    b += "refeq=";
    b += (m == def ? "True" : "False");
    b += " name=";
    b += m->Name();
    b += " kind=";
    b += SymbolKindSpelling(m->SymbolKind());
    b += " ret=";
    b += m->ReturnType().ReflectionName();
    b += " params=";
    b += ParamsLine(m->Parameters());
    b += " tps=";
    b += TpsLine(m->TypeParameters());
    b += " targs=";
    b += TypesLine(m->TypeArguments());
    b += " decl=";
    b += DeclaringTypeName(m);
    b += " memberdef-refeq=";
    b += (m->MemberDefinition() == defView ? "True" : "False");
    b += " subst=";
    b += m->Substitution()->ToString();
    b += " equals-def=";
    b += (m->Equals(def, nullptr) ? "True" : "False");
    return b;
}

// The probe's `MemberLine(IMember m, IMember def)` core (the field/property/
// event lines share the shape). `m` is the concrete-interface view's `IMember`;
// `refeqView` is the definition's MATCHING concrete-interface view (the
// same-instance arm's identity), `memberDefView` the view the definition's own
// `MemberDefinition()` returns (the metadata entities agree on both; the fakes'
// `FakeMember::MemberDefinition` returns the `FakeMember` subobject view).
std::string MemberLineOf(const TS::IMember* m, const TS::IMember* refeqView,
                          const TS::IMember* memberDefView) {
    std::string b;
    b += "refeq=";
    b += (m == refeqView ? "True" : "False");
    b += " name=";
    b += m->Name();
    b += " kind=";
    b += SymbolKindSpelling(m->SymbolKind());
    b += " ret=";
    b += m->ReturnType().ReflectionName();
    b += " decl=";
    b += DeclaringTypeName(m);
    b += " memberdef-refeq=";
    b += (m->MemberDefinition() == memberDefView ? "True" : "False");
    b += " subst=";
    b += m->Substitution()->ToString();
    b += " equals-def=";
    b += (m->Equals(refeqView, nullptr) ? "True" : "False");
    return b;
}

// The probe's `MemberLine` over a metadata field: `m` is the Specialize result
// (the `IField` view's `IMember`), `def` the field definition (one `IMember`
// subobject -- both views agree).
std::string FieldLine(const TS::IMember* m, const TS::IField* def) {
    return MemberLineOf(m, static_cast<const TS::IMember*>(def),
                        static_cast<const TS::IMember*>(def));
}

// The fake-member drives: the `FakeMember` base's `MemberDefinition()` returns
// the `FakeMember` subobject view, so `memberDefView` takes that view while
// `refeqView` stays the concrete-interface view.
std::string FakeFieldLine(const TS::IMember* m, const TI::FakeField& def) {
    return MemberLineOf(
        m,
        static_cast<const TS::IMember*>(static_cast<const TS::IField*>(&def)),
        static_cast<const TS::IMember*>(
            static_cast<const TI::FakeMember*>(&def)));
}
std::string FakePropLine(const TS::IProperty* m, const TI::FakeProperty& def) {
    std::string b = MemberLineOf(
        static_cast<const TS::IMember*>(m),
        static_cast<const TS::IMember*>(static_cast<const TS::IProperty*>(&def)),
        static_cast<const TS::IMember*>(
            static_cast<const TI::FakeMember*>(&def)));
    b += " canget=";
    b += (m->CanGet() ? "True" : "False");
    b += " canset=";
    b += (m->CanSet() ? "True" : "False");
    b += " isindexer=";
    b += (m->IsIndexer() ? "True" : "False");
    return b;
}
std::string FakeEventLine(const TS::IEvent* m, const TI::FakeEvent& def) {
    std::string b = MemberLineOf(
        static_cast<const TS::IMember*>(m),
        static_cast<const TS::IMember*>(static_cast<const TS::IEvent*>(&def)),
        static_cast<const TS::IMember*>(
            static_cast<const TI::FakeMember*>(&def)));
    b += " canadd=";
    b += (m->CanAdd() ? "True" : "False");
    b += " canremove=";
    b += (m->CanRemove() ? "True" : "False");
    b += " caninvoke=";
    b += (m->CanInvoke() ? "True" : "False");
    return b;
}

// The probe's FNV-1a-64.
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

class SpecializeTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!MscorlibAvailable())
            GTEST_SKIP() << "mscorlib fixture not available";
    }
};

// Section A: the MetadataMethod.Specialize Create-arm matrix (the probe's
// A1-A8/A10/A11 lines, byte-identical).
TEST_F(SpecializeTest, MethodCreateArms) {
    SpzFixture fx;
    const TS::ITypeDefinition* list = fx.Type(
        "System.Collections.Generic", "List", 1);
    const TS::ITypeDefinition* str = fx.Type("System", "String");
    const TS::ITypeDefinition* arr = fx.Type("System", "Array");
    const TS::ITypeDefinition* dict = fx.Type(
        "System.Collections.Generic", "Dictionary", 2);

    const TS::IMethod* addDef = fx.GetMethod(list, "Add", 0);
    const TS::IMethod* convDef = fx.GetMethod(list, "ConvertAll", 0);
    const TS::IMethod* equalsDef = fx.GetMethod(str, "Equals", 0, 0);
    const TS::IMethod* sortDef = fx.GetMethod(arr, "Sort", 0, 1);
    const TS::IMethod* dictAddDef = fx.GetMethod(dict, "Add", 0);

    // A1: identity -> the same instance.
    TS::TypeParameterSubstitution id(std::nullopt, std::nullopt);
    EXPECT_STREQ(MethodLine(addDef->Specialize(&id), addDef).c_str(),
        "refeq=True name=Add kind=Method ret=System.Void params=[(item:`0)] "
        "tps=[] targs=[] decl=System.Collections.Generic.List`1 "
        "memberdef-refeq=True subst=[] equals-def=True");
    // A2: non-generic method on a non-generic type -> the same instance.
    TS::TypeParameterSubstitution classI32(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32) },
        std::nullopt);
    EXPECT_STREQ(MethodLine(equalsDef->Specialize(&classI32), equalsDef).c_str(),
        "refeq=True name=Equals kind=Method ret=System.Boolean "
        "params=[(obj:System.Object)] tps=[] targs=[] decl=System.String "
        "memberdef-refeq=True subst=[] equals-def=True");
    // A3: non-generic method on a generic type, class args only -> fresh.
    const TS::IMethod* a3 = addDef->Specialize(&classI32);
    EXPECT_STREQ(MethodLine(a3, addDef).c_str(),
        "refeq=False name=Add kind=Method ret=System.Void "
        "params=[(item:System.Int32)] tps=[] targs=[] "
        "decl=System.Collections.Generic.List`1[[System.Int32]] "
        "memberdef-refeq=True subst=[`0 -> System.Int32] equals-def=False");
    // A4: method type args on a non-generic method are DROPPED.
    TS::TypeParameterSubstitution withMethod(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32) },
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::String) });
    EXPECT_STREQ(MethodLine(addDef->Specialize(&withMethod), addDef).c_str(),
        "refeq=False name=Add kind=Method ret=System.Void "
        "params=[(item:System.Int32)] tps=[] targs=[] "
        "decl=System.Collections.Generic.List`1[[System.Int32]] "
        "memberdef-refeq=True subst=[`0 -> System.Int32, []] equals-def=False");
    // A5: generic method, class args only -> the specialized type parameters.
    const TS::IMethod* a5 = convDef->Specialize(&classI32);
    EXPECT_STREQ(MethodLine(a5, convDef).c_str(),
        "refeq=False name=ConvertAll kind=Method "
        "ret=System.Collections.Generic.List`1[[``0]] "
        "params=[(converter:System.Converter`2[[System.Int32],[``0]])] "
        "tps=[TOutput:TypeParameter] targs=[``0] "
        "decl=System.Collections.Generic.List`1[[System.Int32]] "
        "memberdef-refeq=True subst=[`0 -> System.Int32, ``0 -> ``0] "
        "equals-def=False");
    // A6: generic method with class + method args.
    EXPECT_STREQ(MethodLine(convDef->Specialize(&withMethod), convDef).c_str(),
        "refeq=False name=ConvertAll kind=Method "
        "ret=System.Collections.Generic.List`1[[System.String]] "
        "params=[(converter:System.Converter`2[[System.Int32],[System.String]])] "
        "tps=[TOutput:TypeParameter] targs=[System.String] "
        "decl=System.Collections.Generic.List`1[[System.Int32]] "
        "memberdef-refeq=True subst=[`0 -> System.Int32, ``0 -> System.String] "
        "equals-def=False");
    // A7: static generic method on a non-generic type, method args only.
    TS::TypeParameterSubstitution methodOnly(
        std::nullopt,
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::String) });
    EXPECT_STREQ(MethodLine(sortDef->Specialize(&methodOnly), sortDef).c_str(),
        "refeq=False name=Sort kind=Method ret=System.Void "
        "params=[(array:System.String[])] tps=[T:TypeParameter] "
        "targs=[System.String] decl=System.Array "
        "memberdef-refeq=True subst=[``0 -> System.String] equals-def=False");
    // A8: the re-Specialize compose path.
    TS::TypeParameterSubstitution classObj(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Object) },
        std::nullopt);
    EXPECT_STREQ(MethodLine(a3->Specialize(&classObj), addDef).c_str(),
        "refeq=False name=Add kind=Method ret=System.Void "
        "params=[(item:System.Int32)] tps=[] targs=[] "
        "decl=System.Collections.Generic.List`1[[System.Int32]] "
        "memberdef-refeq=True subst=[`0 -> System.Int32] equals-def=False");
    // A10: the 2-arg class substitution.
    TS::TypeParameterSubstitution classDict(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32),
                                    fx.KnownType(TS::KnownTypeCode::String) },
        std::nullopt);
    EXPECT_STREQ(
        MethodLine(dictAddDef->Specialize(&classDict), dictAddDef).c_str(),
        "refeq=False name=Add kind=Method ret=System.Void "
        "params=[(key:System.Int32), (value:System.String)] tps=[] targs=[] "
        "decl=System.Collections.Generic.Dictionary`2[[System.Int32],"
        "[System.String]] "
        "memberdef-refeq=True subst=[`0 -> System.Int32, `1 -> System.String] "
        "equals-def=False");
    // A11: two fresh Specialize calls give distinct instances.
    const TS::IMethod* a3b = addDef->Specialize(&classI32);
    EXPECT_FALSE(a3 == a3b);
}

// Section A: the Equals shapes (the probe's A9a-A9d).
TEST_F(SpecializeTest, MethodSpecializedEqualsShapes) {
    SpzFixture fx;
    const TS::ITypeDefinition* list = fx.Type(
        "System.Collections.Generic", "List", 1);
    const TS::IMethod* addDef = fx.GetMethod(list, "Add", 0);
    const TS::IMethod* convDef = fx.GetMethod(list, "ConvertAll", 0);
    TS::TypeParameterSubstitution classI32(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32) },
        std::nullopt);

    const TS::IMethod* a3 = addDef->Specialize(&classI32);
    const TS::IMethod* a3b = addDef->Specialize(&classI32);
    const TS::IMethod* a5 = convDef->Specialize(&classI32);
    EXPECT_TRUE(a3->Equals(a3b, nullptr));
    EXPECT_FALSE(a3->Equals(addDef, nullptr));
    EXPECT_FALSE(a3->Equals(a5, nullptr));
    EXPECT_TRUE(a3->Equals(a3, nullptr));
}

// Section B: the MetadataField.Specialize matrix (the probe's B1-B4 lines).
TEST_F(SpecializeTest, FieldCreateArms) {
    SpzFixture fx;
    const TS::ITypeDefinition* list = fx.Type(
        "System.Collections.Generic", "List", 1);
    const TS::ITypeDefinition* str = fx.Type("System", "String");

    const TS::IField* itemsDef = fx.GetField(list, "_items");
    const TS::IField* sizeDef = fx.GetField(list, "_size");
    const TS::IField* emptyDef = fx.GetField(str, "Empty");
    if (itemsDef == nullptr || sizeDef == nullptr || emptyDef == nullptr)
    {
        // The selected mscorlib is a reference assembly: the private
        // fields the Specialize arms exercise are stripped (the net48
        // corpus mscorlib's List`1 carries no fields at all). The test
        // needs a mscorlib with the real implementation shapes.
        GTEST_SKIP() << "the selected mscorlib ("
                     << MscorlibPath()
                     << ") is a reference assembly without the "
                        "implementation fields";
    }

    TS::TypeParameterSubstitution id(std::nullopt, std::nullopt);
    EXPECT_STREQ(FieldLine(itemsDef->Specialize(&id), itemsDef).c_str(),
        "refeq=True name=_items kind=Field ret=`0[] "
        "decl=System.Collections.Generic.List`1 "
        "memberdef-refeq=True subst=[] equals-def=True");
    TS::TypeParameterSubstitution classI32(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32) },
        std::nullopt);
    EXPECT_STREQ(FieldLine(emptyDef->Specialize(&classI32), emptyDef).c_str(),
        "refeq=True name=Empty kind=Field ret=System.String "
        "decl=System.String memberdef-refeq=True subst=[] equals-def=True");
    EXPECT_STREQ(FieldLine(itemsDef->Specialize(&classI32), itemsDef).c_str(),
        "refeq=False name=_items kind=Field ret=System.Int32[] "
        "decl=System.Collections.Generic.List`1[[System.Int32]] "
        "memberdef-refeq=True subst=[`0 -> System.Int32] equals-def=False");
    EXPECT_STREQ(FieldLine(sizeDef->Specialize(&classI32), sizeDef).c_str(),
        "refeq=False name=_size kind=Field ret=System.Int32 "
        "decl=System.Collections.Generic.List`1[[System.Int32]] "
        "memberdef-refeq=True subst=[`0 -> System.Int32] equals-def=False");
}

// Section C: the FakeX general arms (the probe's C1-C7 lines). The
// LookupTypeParameter stub stands in for the C# DefaultTypeParameter the
// shipped internal class uses (the same Name/Index surface the
// SpecializedTypeParameter wrap reads).
TEST_F(SpecializeTest, FakeGeneralArms) {
    SpzFixture fx;
    const TS::ITypeDefinition* list = fx.Type(
        "System.Collections.Generic", "List", 1);
    const TS::ITypeDefinition* str = fx.Type("System", "String");

    TS::ITypePtr tParam = fx.ListTypeParameter(list);
    TS::TypeParameterSubstitution classI32(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32) },
        std::nullopt);

    // C1: FakeField on the List`1 definition -> fresh SpecializedField.
    {
        TI::FakeField ff(fx.comp);
        ff.SetName("ff");
        ff.SetDeclaringType(fx.Alias(list));
        ff.SetReturnType(tParam);
        EXPECT_STREQ(
            FakeFieldLine(ff.Specialize(&classI32), ff).c_str(),
            "refeq=False name=ff kind=Field ret=System.Int32 "
            "decl=System.Collections.Generic.List`1[[System.Int32]] "
            "memberdef-refeq=True subst=[`0 -> System.Int32] equals-def=False");
    }

    // C2: FakeMethod with own type parameters, class args only.
    // C3: FakeMethod with own type parameters + method args.
    {
        TI::FakeMethod fm(fx.comp, TS::SymbolKind::Method);
        fm.SetName("fm");
        fm.SetDeclaringType(fx.Alias(list));
        fm.SetReturnType(tParam);
        fm.SetTypeParameters({ std::shared_ptr<const TS::ITypeParameter>(
            new MethodTypeParameterStub("U")) });
        fm.SetParameters({ std::make_shared<TI::DefaultParameter>(
            tParam, "p") });
        const TS::IMember* fmMemberView =
            static_cast<const TS::IMember*>(
                static_cast<const TI::FakeMember*>(&fm));
        EXPECT_STREQ(MethodLine(fm.Specialize(&classI32),
            static_cast<const TS::IMethod*>(&fm), fmMemberView).c_str(),
            "refeq=False name=fm kind=Method ret=System.Int32 "
            "params=[(p:System.Int32)] tps=[U:TypeParameter] targs=[``0] "
            "decl=System.Collections.Generic.List`1[[System.Int32]] "
            "memberdef-refeq=True subst=[`0 -> System.Int32, ``0 -> ``0] "
            "equals-def=False");
        TS::TypeParameterSubstitution withMethod(
            std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32) },
            std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::String) });
        EXPECT_STREQ(MethodLine(fm.Specialize(&withMethod),
            static_cast<const TS::IMethod*>(&fm), fmMemberView).c_str(),
            "refeq=False name=fm kind=Method ret=System.Int32 "
            "params=[(p:System.Int32)] tps=[U:TypeParameter] "
            "targs=[System.String] "
            "decl=System.Collections.Generic.List`1[[System.Int32]] "
            "memberdef-refeq=True subst=[`0 -> System.Int32, ``0 -> System.String] "
            "equals-def=False");
        // C7: the fake method's own TypeArguments mirror TypeParameters.
        EXPECT_STREQ(TypesLine(fm.TypeArguments()).c_str(), "[``0]");
    }

    // C4: the ArrayType declaring-type arm (only a fake can reach it): the
    // method is non-generic and the ArrayType's tpc is 0, so without the arm
    // this would return the same instance.
    {
        TS::ArrayType arrayType(fx.Alias(str));
        TI::FakeMethod fmArr(fx.comp, TS::SymbolKind::Method);
        fmArr.SetName("fmArr");
        fmArr.SetDeclaringType(fx.Alias(&arrayType));
        fmArr.SetReturnType(fx.KnownType(TS::KnownTypeCode::String));
        fmArr.SetParameters({ std::make_shared<TI::DefaultParameter>(
            fx.KnownType(TS::KnownTypeCode::String), "p") });
        EXPECT_STREQ(MethodLine(fmArr.Specialize(&classI32),
            static_cast<const TS::IMethod*>(&fmArr),
            static_cast<const TS::IMember*>(
                static_cast<const TI::FakeMember*>(&fmArr))).c_str(),
            "refeq=False name=fmArr kind=Method ret=System.String "
            "params=[(p:System.String)] tps=[] targs=[] decl=System.String[] "
            "memberdef-refeq=True subst=[`0 -> System.Int32] equals-def=False");
    }

    // C5: FakeProperty on the List`1 definition -> fresh SpecializedProperty.
    {
        TI::FakeProperty fp(fx.comp);
        fp.SetName("fp");
        fp.SetDeclaringType(fx.Alias(list));
        fp.SetReturnType(tParam);
        fp.SetParameters({ std::make_shared<TI::DefaultParameter>(
            tParam, "i") });
        EXPECT_STREQ(
            FakePropLine(
                static_cast<const TS::IProperty*>(fp.Specialize(&classI32)),
                fp).c_str(),
            "refeq=False name=fp kind=Property ret=System.Int32 "
            "decl=System.Collections.Generic.List`1[[System.Int32]] "
            "memberdef-refeq=True subst=[`0 -> System.Int32] equals-def=False "
            "canget=False canset=False isindexer=False");
    }

    // C6: FakeEvent on the List`1 definition -> fresh SpecializedEvent.
    {
        TI::FakeEvent fe(fx.comp);
        fe.SetName("fe");
        fe.SetDeclaringType(fx.Alias(list));
        fe.SetReturnType(tParam);
        EXPECT_STREQ(
            FakeEventLine(
                static_cast<const TS::IEvent*>(fe.Specialize(&classI32)),
                fe).c_str(),
            "refeq=False name=fe kind=Event ret=System.Int32 "
            "decl=System.Collections.Generic.List`1[[System.Int32]] "
            "memberdef-refeq=True subst=[`0 -> System.Int32] equals-def=False "
            "canadd=False canremove=False caninvoke=False");
    }
}

// The null-DeclaringType NRE arm (the FakeMember_Test ffNoDecl shape, now
// reached through Create's declaring-type read).
TEST_F(SpecializeTest, NullDeclaringTypeThrowsTheNre) {
    SpzFixture fx;
    TS::TypeParameterSubstitution classI32(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32) },
        std::nullopt);
    TI::FakeField f(fx.comp);
    try {
        f.Specialize(&classI32);
        FAIL() << "the null-DeclaringType Specialize must throw";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(),
            "Object reference not set to an instance of an object.");
    }
    // The Identity arm fires BEFORE the declaring-type read: no throw.
    TS::TypeParameterSubstitution id(std::nullopt, std::nullopt);
    EXPECT_EQ(f.Specialize(&id),
        static_cast<const TS::IMember*>(
            static_cast<const TS::IField*>(&f)));
}

// Section D: the FNV-1a-64 digests over every method + field of List`1 /
// Dictionary`2 / String specialized with the full class substitution (the
// probe's D1-D3 lines).
TEST_F(SpecializeTest, WholeTypeSpecializeDigests) {
    SpzFixture fx;
    TS::TypeParameterSubstitution classI32(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32) },
        std::nullopt);

    const TS::ITypeDefinition* list = fx.Type(
        "System.Collections.Generic", "List", 1);
    const TS::ITypeDefinition* dict = fx.Type(
        "System.Collections.Generic", "Dictionary", 2);
    const TS::ITypeDefinition* str = fx.Type("System", "String");

    Fnv64 fnv;
    for (const TS::IMethod* m :
        list->GetMethods(nullptr, TS::GetMemberOptions::IgnoreInheritedMembers)) {
        fnv.Add(MethodLine(m->Specialize(&classI32), m));
    }
    for (const TS::IField* f : list->Fields()) {
        fnv.Add(FieldLine(f->Specialize(&classI32), f));
    }
    EXPECT_EQ(fnv.Digest(), 0x35D3CF322FF741F6ULL);

    TS::TypeParameterSubstitution classDict(
        std::vector<TS::ITypePtr>{ fx.KnownType(TS::KnownTypeCode::Int32),
                                    fx.KnownType(TS::KnownTypeCode::String) },
        std::nullopt);
    for (const TS::IMethod* m :
        dict->GetMethods(nullptr, TS::GetMemberOptions::IgnoreInheritedMembers)) {
        fnv.Add(MethodLine(m->Specialize(&classDict), m));
    }
    for (const TS::IField* f : dict->Fields()) {
        fnv.Add(FieldLine(f->Specialize(&classDict), f));
    }
    EXPECT_EQ(fnv.Digest(), 0xB6F21FAE9E0D5B84ULL);

    for (const TS::IMethod* m :
        str->GetMethods(nullptr, TS::GetMemberOptions::IgnoreInheritedMembers)) {
        fnv.Add(MethodLine(m->Specialize(&classI32), m));
    }
    EXPECT_EQ(fnv.Digest(), 0x18297946623ECA94ULL);
}

} // namespace
