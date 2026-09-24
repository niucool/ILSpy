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

// The FakeMember test suite (gold-pinned against the real ICSharpCode.
// Decompiler 11.0 via the C:/temp-probe/MmEnumProbe gold probe, section D):
// the four fake classes' defaults, the settable surface, the
// CreateDummyConstructor factory (the Methods-enumeration consumer), and the
// Specialize short-circuits (the same-instance Identity + declaring-tpc-0
// arms gold-pinned refeq=True; the fresh-SpecializedX general arms are the
// port's documented deferral; the null-DeclaringType arm is the .NET NRE).

#include "Decompiler/Metadata/MetadataFile.hpp"
#include <cstdlib>
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedEvent.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedField.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedMethod.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedProperty.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <gtest/gtest.h>

#include <cstdio>
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

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
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
// The fixture (the probe's FakeDrives shape: the single-mscorlib
// compilation; the String and List`1 definitions as the tpc-0 / tpc-1
// declaring types).
// ---------------------------------------------------------------------------
struct FakeFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };

    TestCompilation comp;
    TS::MetadataModule msc{ comp, &mscorlibFile,
                             TS::TypeSystemOptions::Default };
    FixedModuleRef mscRef{ &msc };

    FakeFixture() { comp.Initialize(mscRef, {}); }

    const TS::ITypeDefinition* StringType() {
        return msc.GetTypeDefinition(TS::TopLevelTypeName("System", "String"));
    }

    const TS::ITypeDefinition* ListOfTType() {
        return msc.GetTypeDefinition(
            TS::TopLevelTypeName("System.Collections.Generic", "List", 1));
    }

    TS::ITypePtr TypePtr(const TS::ITypeDefinition* definition) {
        // The aliasing handle over the module-owned definition (the
        // FakeMember.cpp CreateDummyConstructor convention).
        return TS::ITypePtr(const_cast<TS::IType*>(static_cast<const TS::IType*>(definition)),
                            [](TS::IType*) {
                                // no-op: the module's entity cache owns it
                            });
    }

    TS::ITypePtr KnownType(TS::KnownTypeCode code) {
        const TS::IType& t = comp.FindType(code);
        return TS::ITypePtr(const_cast<TS::IType*>(&t), [](TS::IType*) {
            // no-op: the compilation's type system owns the known type
        });
    }

    // The probe's classSub: new TypeParameterSubstitution([Int32], null).
    TS::TypeParameterSubstitution ClassSub() {
        return TS::TypeParameterSubstitution(
            std::vector<TS::ITypePtr>{ KnownType(TS::KnownTypeCode::Int32) },
            std::nullopt);
    }
};

bool MscorlibAvailable() { return FileExists(MscorlibPath()); }

class FakeMemberTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!MscorlibAvailable())
            GTEST_SKIP() << "mscorlib fixture not available";
    }
};

// D: fm (the FakeMethod defaults)
TEST_F(FakeMemberTest, FakeMethodDefaults)
{
    FakeFixture fx;
    TI::FakeMethod fm(fx.comp, TS::SymbolKind::Method);
    EXPECT_EQ(fm.SymbolKind(), TS::SymbolKind::Method);
    // The C# null Name default maps to "" (the null=="" equivalence).
    EXPECT_EQ(fm.Name(), "");
    // rt=? -- the UnknownType null object's ReflectionName.
    EXPECT_EQ(fm.ReturnType().ReflectionName(), "?");
    EXPECT_EQ(fm.Accessibility(), TS::Accessibility::Public);
    EXPECT_FALSE(fm.IsStatic());
    EXPECT_TRUE(fm.Parameters().empty());
    EXPECT_TRUE(fm.TypeParameters().empty());
    EXPECT_FALSE(fm.IsConstructor());
    EXPECT_FALSE(fm.IsOperator());
    EXPECT_FALSE(fm.IsAccessor());
    EXPECT_EQ(fm.AccessorOwner(), nullptr);
    EXPECT_FALSE(fm.HasBody());
    EXPECT_FALSE(fm.IsExtensionMethod());
    EXPECT_TRUE(fm.TypeArguments().empty());
    EXPECT_TRUE(fm.GetAttributes().empty());
    EXPECT_FALSE(fm.HasAttribute(TS::KnownAttribute::Obsolete));
    EXPECT_EQ(fm.GetAttribute(TS::KnownAttribute::Obsolete), nullptr);
    EXPECT_EQ(fm.FullName(), "");
    EXPECT_EQ(fm.ReflectionName(), "");
    EXPECT_EQ(fm.Namespace(), "");
    // D: fm subst=identity / memberdef-refeq=True / explicitiface=False /
    // eicount=0 / parentmodule=True dectype=True dectypedef=True /
    // token-nil=True abstract/sealed/virt/ovr/ovrb=False
    EXPECT_EQ(fm.Substitution(), &TS::TypeParameterSubstitution::Identity());
    EXPECT_EQ(fm.MemberDefinition(),
        static_cast<const TS::IMember*>(static_cast<const TI::FakeMember*>(&fm)));
    EXPECT_FALSE(fm.IsExplicitInterfaceImplementation());
    EXPECT_TRUE(fm.ExplicitlyImplementedInterfaceMembers().empty());
    EXPECT_EQ(fm.ParentModule(), nullptr);
    EXPECT_EQ(fm.DeclaringType(), nullptr);
    EXPECT_EQ(fm.DeclaringTypeDefinition(), nullptr);
    EXPECT_EQ(fm.MetadataToken(), 0u);
    EXPECT_FALSE(fm.IsAbstract());
    EXPECT_FALSE(fm.IsSealed());
    EXPECT_FALSE(fm.IsVirtual());
    EXPECT_FALSE(fm.IsOverride());
    EXPECT_FALSE(fm.IsOverridable());
    EXPECT_FALSE(fm.IsLocalFunction());
    EXPECT_EQ(fm.ReducedFrom(), nullptr);
    EXPECT_TRUE(fm.GetReturnTypeAttributes().empty());
    EXPECT_FALSE(fm.ReturnTypeIsRefReadOnly());
    EXPECT_FALSE(fm.ThisIsRefReadOnly());
    EXPECT_FALSE(fm.IsInitOnly());
    EXPECT_EQ(fm.AccessorKind(), TS::MethodSemanticsAttributes::None);
}

// D: fm set (the settable surface)
TEST_F(FakeMemberTest, FakeMemberSetters)
{
    FakeFixture fx;
    TI::FakeMethod fm(fx.comp, TS::SymbolKind::Method);
    fm.SetName("Test");
    fm.SetDeclaringType(fx.TypePtr(fx.StringType()));
    fm.SetReturnType(fx.KnownType(TS::KnownTypeCode::Int32));
    fm.SetIsStatic(true);
    // D: fm set name/full/refl/rt/static/dectype/dectypedef/parentmodule/ns
    EXPECT_EQ(fm.Name(), "Test");
    EXPECT_EQ(fm.FullName(), "System.String.Test");
    EXPECT_EQ(fm.ReflectionName(), "System.String.Test");
    EXPECT_EQ(fm.ReturnType().ReflectionName(), "System.Int32");
    EXPECT_TRUE(fm.IsStatic());
    EXPECT_EQ(fm.DeclaringType()->ReflectionName(), "System.String");
    EXPECT_EQ(fm.DeclaringTypeDefinition()->ReflectionName(), "System.String");
    EXPECT_EQ(fm.ParentModule(), static_cast<const TS::IModule*>(&fx.msc));
    EXPECT_EQ(fm.Namespace(), "System");

    // D: fm set specialize-identity-refeq=True
    const TS::IMethod* spec
        = fm.Specialize(&TS::TypeParameterSubstitution::Identity());
    EXPECT_EQ(spec, static_cast<const TS::IMethod*>(&fm));

    // The reference-equality Equals (the C# default object.Equals): a second
    // fake with the identical field values is NOT equal.
    TI::FakeMethod fm2(fx.comp, TS::SymbolKind::Method);
    fm2.SetName("Test");
    fm2.SetDeclaringType(fx.TypePtr(fx.StringType()));
    EXPECT_FALSE(fm.Equals(
        static_cast<const TS::IMember*>(static_cast<const TI::FakeMember*>(&fm2)),
        nullptr));
    EXPECT_TRUE(fm.Equals(
        static_cast<const TS::IMember*>(static_cast<const TI::FakeMember*>(&fm)),
        nullptr));
}

// D: dummy / dummy2 (CreateDummyConstructor)
TEST_F(FakeMemberTest, CreateDummyConstructor)
{
    FakeFixture fx;
    std::shared_ptr<TS::IMethod> dummy = TI::FakeMethod::CreateDummyConstructor(
        fx.comp, fx.TypePtr(fx.StringType()),
        TS::Accessibility::Public);
    ASSERT_NE(dummy, nullptr);
    // D: dummy ctor/params/rt/acc/dectype/memberdef/token-nil/body/owner
    EXPECT_EQ(dummy->SymbolKind(), TS::SymbolKind::Constructor);
    EXPECT_TRUE(dummy->IsConstructor());
    EXPECT_EQ(dummy->Name(), ".ctor");
    EXPECT_TRUE(dummy->Parameters().empty());
    EXPECT_EQ(dummy->ReturnType().ReflectionName(), "System.Void");
    EXPECT_EQ(dummy->Accessibility(), TS::Accessibility::Public);
    EXPECT_FALSE(dummy->IsStatic());
    EXPECT_EQ(dummy->DeclaringType()->ReflectionName(), "System.String");
    // The MemberDefinition self-reference: the sub-A IMember address (the
    // gold's ReferenceEquals(memberDefinition, dummy) -- the two-IMember-
    // subobject diamond makes the bare dummy.get() comparison ambiguous, so
    // the sub-A path is the faithful port-side form).
    EXPECT_EQ(dummy->MemberDefinition(),
        static_cast<const TS::IMember*>(static_cast<const TI::FakeMember*>(
            dynamic_cast<const TI::FakeMethod*>(dummy.get()))));
    EXPECT_EQ(dummy->MetadataToken(), 0u);
    EXPECT_FALSE(dummy->HasBody());
    EXPECT_EQ(dummy->AccessorOwner(), nullptr);
    EXPECT_FALSE(dummy->IsAccessor());
    // The 3-arg default-Accessibility form.
    std::shared_ptr<TS::IMethod> dummy2 = TI::FakeMethod::CreateDummyConstructor(
        fx.comp, fx.TypePtr(fx.StringType()));
    EXPECT_EQ(dummy2->Accessibility(), TS::Accessibility::Public);
    // D: dummy2 equals-other=False (the reference equality).
    EXPECT_FALSE(dummy->Equals(dummy2.get(), nullptr));
}

// D: fc (the ctor-kind FakeMethod)
TEST_F(FakeMemberTest, ConstructorKindFakeMethod)
{
    FakeFixture fx;
    TI::FakeMethod fc(fx.comp, TS::SymbolKind::Constructor);
    EXPECT_EQ(fc.SymbolKind(), TS::SymbolKind::Constructor);
    EXPECT_TRUE(fc.IsConstructor());
    EXPECT_FALSE(fc.IsOperator());
    EXPECT_FALSE(fc.IsDestructor());
}

// D: ff (the FakeField surface)
TEST_F(FakeMemberTest, FakeFieldDefaultsAndSetters)
{
    FakeFixture fx;
    TI::FakeField ff(fx.comp);
    EXPECT_EQ(ff.SymbolKind(), TS::SymbolKind::Field);
    EXPECT_EQ(ff.Name(), "");
    // rt=? (the UnknownType default)
    EXPECT_EQ(ff.Type().ReflectionName(), "?");
    EXPECT_FALSE(ff.IsReadOnly());
    EXPECT_FALSE(ff.IsVolatile());
    EXPECT_FALSE(ff.IsConst());
    EXPECT_FALSE(ff.GetConstantValue(false).has_value());
    EXPECT_EQ(ff.Accessibility(), TS::Accessibility::Public);

    ff.SetName("F");
    ff.SetDeclaringType(fx.TypePtr(fx.StringType()));
    // D: ff full/refl
    EXPECT_EQ(ff.FullName(), "System.String.F");
    EXPECT_EQ(ff.ReflectionName(), "System.String.F");
    // The C# `IType IVariable.Type => this.ReturnType`.
    EXPECT_EQ(ff.ReturnType().ReflectionName(), "?");
}

// D: fp (the FakeProperty surface)
TEST_F(FakeMemberTest, FakePropertyDefaultsAndSetters)
{
    FakeFixture fx;
    TI::FakeProperty fp(fx.comp);
    EXPECT_EQ(fp.SymbolKind(), TS::SymbolKind::Property);
    EXPECT_FALSE(fp.CanGet());
    EXPECT_FALSE(fp.CanSet());
    EXPECT_FALSE(fp.IsIndexer());
    EXPECT_FALSE(fp.ReturnTypeIsRefReadOnly());
    // The C# NULL Parameters default maps to the empty vector (the
    // null=="" equivalence; the C# null is observable only through the
    // deferred ToString -- documented unreachable in the port).
    EXPECT_TRUE(fp.Parameters().empty());

    fp.SetName("P");
    fp.SetDeclaringType(fx.TypePtr(fx.StringType()));
    fp.SetReturnType(fx.KnownType(TS::KnownTypeCode::Int32));
    // D: fp kind-after=Indexer (the IsIndexer switch)
    fp.SetIsIndexer(true);
    EXPECT_EQ(fp.SymbolKind(), TS::SymbolKind::Indexer);
    // The getter/setter wiring drives CanGet/CanSet.
    std::shared_ptr<TS::IMethod> dummy = TI::FakeMethod::CreateDummyConstructor(
        fx.comp, fx.TypePtr(fx.StringType()));
    fp.SetGetter(dummy.get());
    fp.SetSetter(dummy.get());
    EXPECT_TRUE(fp.CanGet());
    EXPECT_TRUE(fp.CanSet());
    EXPECT_EQ(fp.Getter(), dummy.get());
    EXPECT_EQ(fp.Setter(), dummy.get());
}

// D: fe (the FakeEvent surface)
TEST_F(FakeMemberTest, FakeEventDefaultsAndSetters)
{
    FakeFixture fx;
    TI::FakeEvent fe(fx.comp);
    EXPECT_EQ(fe.SymbolKind(), TS::SymbolKind::Event);
    EXPECT_FALSE(fe.CanAdd());
    EXPECT_FALSE(fe.CanRemove());
    EXPECT_FALSE(fe.CanInvoke());

    fe.SetName("E");
    fe.SetDeclaringType(fx.TypePtr(fx.StringType()));
    fe.SetReturnType(fx.KnownType(TS::KnownTypeCode::String));
    EXPECT_EQ(fe.FullName(), "System.String.E");
    EXPECT_EQ(fe.ReturnType().ReflectionName(), "System.String");

    std::shared_ptr<TS::IMethod> dummy = TI::FakeMethod::CreateDummyConstructor(
        fx.comp, fx.TypePtr(fx.StringType()));
    fe.SetAddAccessor(dummy.get());
    fe.SetRemoveAccessor(dummy.get());
    fe.SetInvokeAccessor(dummy.get());
    EXPECT_TRUE(fe.CanAdd());
    EXPECT_TRUE(fe.CanRemove());
    EXPECT_TRUE(fe.CanInvoke());
    EXPECT_EQ(fe.AddAccessor(), dummy.get());
    EXPECT_EQ(fe.RemoveAccessor(), dummy.get());
    EXPECT_EQ(fe.InvokeAccessor(), dummy.get());
}

// The Specialize same-instance arms (the probe's Specialize drives): the
// Identity / declaring-tpc-0 arms gold-pinned refeq=True (the view through
// each fake's own concrete interface -- the Specialize_Test C-section
// convention); the null-DeclaringType arm is the .NET NRE (mapped to
// std::runtime_error).
TEST_F(FakeMemberTest, SpecializeShortCircuits)
{
    FakeFixture fx;
    TS::TypeParameterSubstitution classSub = fx.ClassSub();

    // ffTpc0: the declaring type has no type parameters -> the same instance.
    {
        TI::FakeField f(fx.comp);
        f.SetDeclaringType(fx.TypePtr(fx.StringType()));
        EXPECT_EQ(f.Specialize(&classSub),
                  static_cast<const TS::IMember*>(
                      static_cast<const TS::IField*>(&f)));
    }
    // fmTpc0: TypeParameters empty + the declaring tpc-0 -> the same instance.
    {
        TI::FakeMethod m(fx.comp, TS::SymbolKind::Method);
        m.SetDeclaringType(fx.TypePtr(fx.StringType()));
        EXPECT_EQ(m.Specialize(&classSub),
                  static_cast<const TS::IMethod*>(&m));
    }
    // fpTpc0 / feTpc0: the Field-form short-circuit.
    {
        TI::FakeProperty p(fx.comp);
        p.SetDeclaringType(fx.TypePtr(fx.StringType()));
        EXPECT_EQ(p.Specialize(&classSub),
                  static_cast<const TS::IMember*>(
                      static_cast<const TS::IProperty*>(&p)));
    }
    {
        TI::FakeEvent e(fx.comp);
        e.SetDeclaringType(fx.TypePtr(fx.StringType()));
        EXPECT_EQ(e.Specialize(&classSub),
                  static_cast<const TS::IMember*>(
                      static_cast<const TS::IEvent*>(&e)));
    }
    // ffNoDecl: the null DeclaringType NRE (the .NET message).
    {
        TI::FakeField f(fx.comp);
        try {
            f.Specialize(&classSub);
            FAIL() << "the null-DeclaringType Specialize must throw";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(),
                "Object reference not set to an instance of an object.");
        }
    }
}

// The Specialize general arms (the fresh-SpecializedX construction): the
// tpc-1 declaring types build a fresh specialized instance of each kind
// (gold refeq=False; the Specialize_Test C-section pins the full renders).
TEST_F(FakeMemberTest, SpecializeGeneralArmsBuildFreshSpecialized)
{
    FakeFixture fx;
    TS::TypeParameterSubstitution classSub = fx.ClassSub();

    {
        TI::FakeField f(fx.comp);
        f.SetDeclaringType(fx.TypePtr(fx.ListOfTType()));
        const TS::IMember* spec = f.Specialize(&classSub);
        EXPECT_NE(spec, static_cast<const TS::IMember*>(
                                static_cast<const TS::IField*>(&f)));
        EXPECT_NE(dynamic_cast<const TI::SpecializedField*>(spec), nullptr);
    }
    {
        TI::FakeMethod m(fx.comp, TS::SymbolKind::Method);
        m.SetDeclaringType(fx.TypePtr(fx.ListOfTType()));
        const TS::IMethod* spec = m.Specialize(&classSub);
        EXPECT_NE(spec, static_cast<const TS::IMethod*>(&m));
        EXPECT_NE(dynamic_cast<const TI::SpecializedMethod*>(spec), nullptr);
    }
    {
        TI::FakeProperty p(fx.comp);
        p.SetDeclaringType(fx.TypePtr(fx.ListOfTType()));
        const TS::IMember* spec = p.Specialize(&classSub);
        EXPECT_NE(spec, static_cast<const TS::IMember*>(
                                static_cast<const TS::IProperty*>(&p)));
        EXPECT_NE(dynamic_cast<const TI::SpecializedProperty*>(spec), nullptr);
    }
    {
        TI::FakeEvent e(fx.comp);
        e.SetDeclaringType(fx.TypePtr(fx.ListOfTType()));
        const TS::IMember* spec = e.Specialize(&classSub);
        EXPECT_NE(spec, static_cast<const TS::IMember*>(
                                static_cast<const TS::IEvent*>(&e)));
        EXPECT_NE(dynamic_cast<const TI::SpecializedEvent*>(spec), nullptr);
    }
}

} // namespace
