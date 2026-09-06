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

// The VarArgInstanceMethod test suite (gold-pinned against the real
// ICSharpCode.Decompiler 11.0 via the C:/temp-probe/MmEnumProbe gold probe,
// section C): the wrapper's construction over the real mscorlib vararg
// String.Concat (0x06000553 -- the sentinel drop, the RegularParameterCount,
// the vararg parameter append, the ToString render, the Equals contracts, the
// delegation surface), plus the Specialize fresh-wrapper identity over a
// stub base method (the port's MetadataMethod::Specialize is still the
// SpecializedMethod::Create deferral, so the gold's Specialize-identity
// facts are driven over a local stub whose Specialize returns itself --
// the same short-circuit the real engine's Create factory applies for the
// identity substitution).

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/VarArgInstanceMethod.hpp"

#include <gtest/gtest.h>

#include <cstdio>
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

bool FileExists(const char* path) {
    FILE* file = std::fopen(path, "rb");
    if (file == nullptr)
        return false;
    std::fclose(file);
    return true;
}

// A module reference resolving to an externally-owned module (the
// MetadataMethod_Test FixedModuleRef precedent).
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

// A local IMethod stub for the Specialize drives: a settable surface whose
// Specialize returns itself (the real engine's SpecializedMethod.Create
// Identity short-circuit -- what the gold's `two.Specialize(identity)` drive
// observes on the base method), the sentinel `__arglist` tail the wrapper
// ctor consumes, and a name/declaring-type/return-type triple the ToString
// render reads.
class StubVarArgMethod : public TS::IMethod {
public:
    StubVarArgMethod(std::string name, std::string declaringReflectionName,
                     std::string returnReflectionName, int regularParams)
        : name_(std::move(name)),
          declaringReflectionName_(std::move(declaringReflectionName)),
          returnReflectionName_(std::move(returnReflectionName)) {
        for (int i = 0; i < regularParams; i++) {
            params_.push_back(std::make_shared<TS::Implementation::DefaultParameter>(
                TS::UnknownType(), "arg" + std::to_string(i), this));
        }
        // The trailing `__arglist` sentinel (the SpecialType singleton
        // constructed over the TypeKind -- the port's `IType.hpp` shape).
        params_.push_back(std::make_shared<TS::Implementation::DefaultParameter>(
            std::make_shared<TS::SpecialType>(TS::TypeKind::ArgList),
            std::string(), this));
    }

    // --- ISymbol / INamedElement ---
    TS::SymbolKind SymbolKind() const override {
        return TS::SymbolKind::Method;
    }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return declaringReflectionName_ + "." + name_; }
    std::string ReflectionName() const override {
        return declaringReflectionName_ + "." + name_;
    }
    std::string Namespace() const override { return std::string(); }

    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("StubVarArgMethod::Compilation");
    }

    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override {
        std::vector<const TS::IParameter*> result;
        for (const auto& p : params_)
            result.push_back(p.get());
        return result;
    }

    // The stub's declaring/return types: named UnknownType instances (the
    // ReflectionName the wrapper's ToString render reads).
    TS::ITypePtr unknownDecl_
        = std::make_shared<class TS::UnknownType>(std::optional<std::string>("System"),
                                          std::string("String"), 0);
    TS::ITypePtr unknownRet_
        = std::make_shared<class TS::UnknownType>(std::optional<std::string>("System"),
                                          std::string("String"), 0);

    // --- IMember ---
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *unknownRet_; }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override {
        return &TS::TypeParameterSubstitution::Identity();
    }
    const TS::IMethod* Specialize(
        const TS::TypeParameterSubstitution* substitution) const override {
        (void)substitution;
        return this;  // the Identity short-circuit
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor* typeNormalization) const override {
        (void)typeNormalization;
        return obj == this;
    }

    // --- IMethod (the fixed surface the drives touch) ---
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return {};
    }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
    }
    TS::ITypePtr DeclaringType() const override {
        return unknownDecl_;
    }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetReturnTypeAttributes() const override {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    std::vector<const TS::ITypeParameter*> TypeParameters() const override {
        return {};
    }
    std::vector<TS::ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return true; }
    bool IsAccessor() const override { return false; }
    const TS::IMember* AccessorOwner() const override { return nullptr; }
    TS::MethodSemanticsAttributes AccessorKind() const override {
        return TS::MethodSemanticsAttributes::None;
    }
    const TS::IMethod* ReducedFrom() const override { return nullptr; }

    // The stub's declaring-type ReflectionName the VarArgInstanceMethod
    // ToString render consumes through DeclaringType() -- the stub throws on
    // the real DeclaringType() (unused surface), so the tests do not drive
    // the stub through ToString.
    std::string name_;
    std::string declaringReflectionName_;
    std::string returnReflectionName_;
    std::vector<std::shared_ptr<const TS::IParameter>> params_;
};

// ---------------------------------------------------------------------------
// The fixture: the single-mscorlib compilation (the probe's VarArgDrives
// shape -- `new SimpleCompilation(new PEFile(MscorlibPath))`).
// ---------------------------------------------------------------------------
struct VarArgFixture {
    TM::MetadataFile mscorlibFile{ MscorlibPath() };

    TestCompilation comp;
    TS::MetadataModule msc{ comp, &mscorlibFile,
                             TS::TypeSystemOptions::Default };
    FixedModuleRef mscRef{ &msc };

    VarArgFixture() { comp.Initialize(mscRef, {}); }

    // The mscorlib vararg trio's String.Concat (0x06000553).
    const TS::IMethod* Concat() {
        return msc.GetDefinitionMethod(0x06000553u);
    }
    // Console.WriteLine (0x06000B7F) -- the other vararg base for the
    // not-equals arm.
    const TS::IMethod* WriteLine() {
        return msc.GetDefinitionMethod(0x06000B7Fu);
    }
};

// The non-owning alias over a module-owned method (the wrapper's ctor takes
// an owning shared_ptr; the module's per-row entity cache is the real owner,
// so a no-op-deleter alias is the fixture stand-in -- the FakeMember.cpp
// CreateDummyConstructor convention).
std::shared_ptr<TS::IMethod> AliasMethod(const TS::IMethod* m) {
    return std::shared_ptr<TS::IMethod>(const_cast<TS::IMethod*>(m),
                                        [](TS::IMethod*) {
                                            // no-op: the module owns it
                                        });
}

bool MscorlibAvailable() { return FileExists(MscorlibPath()); }

TS::ITypePtr KnownType(const VarArgFixture& fx, TS::KnownTypeCode code) {
    // The no-op-deleter alias over the compilation-owned known type (the
    // FakeMember.cpp CreateDummyConstructor convention).
    const TS::IType& t = fx.comp.FindType(code);
    return TS::ITypePtr(const_cast<TS::IType*>(&t), [](TS::IType*) {
        // no-op: the compilation's type system owns the known type
    });
}

// ---------------------------------------------------------------------------
// The tests (the probe's section C, byte-for-byte).
// ---------------------------------------------------------------------------

class VarArgInstanceMethodTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!MscorlibAvailable())
            GTEST_SKIP() << "mscorlib fixture not available";
    }
};

TEST_F(VarArgInstanceMethodTest, BaseConcatShape)
{
    VarArgFixture fx;
    const TS::IMethod* concat = fx.Concat();
    ASSERT_NE(concat, nullptr);
    std::vector<const TS::IParameter*> baseParams = concat->Parameters();
    ASSERT_EQ(baseParams.size(), 5u);
    // C: base params last kind ArgList
    EXPECT_EQ(baseParams.back()->Type().Kind(), TS::TypeKind::ArgList);
    // C: base regular 4
    EXPECT_EQ(static_cast<int>(baseParams.size() - 1), 4);
}

TEST_F(VarArgInstanceMethodTest, EmptyVarArgsDropTheSentinel)
{
    VarArgFixture fx;
    auto empty = std::make_shared<TS::VarArgInstanceMethod>(
        AliasMethod(fx.Concat()), std::vector<TS::ITypePtr>{});
    // C: empty rpc=4
    EXPECT_EQ(empty->RegularParameterCount(), 4);
    // C: empty tostring (the "..., " separator after the regular parameters,
    // the trailing ", ..." for the empty vararg list)
    EXPECT_EQ(empty->ToString(),
        "[MethodSystem.String.Concat(System.Object, System.Object, "
        "System.Object, System.Object, ...):System.String]");
    // The 4 regular parameters keep the base's names.
    std::vector<const TS::IParameter*> params = empty->Parameters();
    ASSERT_EQ(params.size(), 4u);
    EXPECT_EQ(params[0]->Name(), "arg0");
    EXPECT_EQ(params[3]->Name(), "arg3");
}

TEST_F(VarArgInstanceMethodTest, VarArgTypesAppend)
{
    VarArgFixture fx;
    auto two = std::make_shared<TS::VarArgInstanceMethod>(
        AliasMethod(fx.Concat()),
        std::vector<TS::ITypePtr>{ KnownType(fx, TS::KnownTypeCode::Int32),
                                   KnownType(fx, TS::KnownTypeCode::String),
                                   KnownType(fx, TS::KnownTypeCode::Object) });
    EXPECT_EQ(two->RegularParameterCount(), 4);
    std::vector<const TS::IParameter*> params = two->Parameters();
    ASSERT_EQ(params.size(), 7u);
    // The vararg parameters: the empty name + the given types.
    EXPECT_EQ(params[4]->Name(), "");
    EXPECT_EQ(params[4]->Type().ReflectionName(), "System.Int32");
    EXPECT_EQ(params[5]->Type().ReflectionName(), "System.String");
    EXPECT_EQ(params[6]->Type().ReflectionName(), "System.Object");
    // C: two tostring
    EXPECT_EQ(two->ToString(),
        "[MethodSystem.String.Concat(System.Object, System.Object, "
        "System.Object, System.Object, ..., System.Int32, System.String, "
        "System.Object):System.String]");
}

TEST_F(VarArgInstanceMethodTest, EqualsContracts)
{
    VarArgFixture fx;
    auto emptyA = std::make_shared<TS::VarArgInstanceMethod>(
        AliasMethod(fx.Concat()), std::vector<TS::ITypePtr>{});
    auto emptyB = std::make_shared<TS::VarArgInstanceMethod>(
        AliasMethod(fx.Concat()), std::vector<TS::ITypePtr>{});
    auto otherBase = std::make_shared<TS::VarArgInstanceMethod>(
        AliasMethod(fx.WriteLine()), std::vector<TS::ITypePtr>{});
    // C: empty equals-samebase=True / equals-otherbase=False
    EXPECT_TRUE(emptyA->Equals(emptyB.get(), nullptr));
    EXPECT_FALSE(emptyA->Equals(otherBase.get(), nullptr));
    EXPECT_FALSE(emptyA->Equals(nullptr, nullptr));
}

TEST_F(VarArgInstanceMethodTest, DelegationSurface)
{
    VarArgFixture fx;
    auto two = std::make_shared<TS::VarArgInstanceMethod>(
        AliasMethod(fx.Concat()),
        std::vector<TS::ITypePtr>{ KnownType(fx, TS::KnownTypeCode::Int32) });
    // C: two kind / name / token
    EXPECT_EQ(two->SymbolKind(), TS::SymbolKind::Method);
    EXPECT_EQ(two->Name(), "Concat");
    EXPECT_EQ(two->MetadataToken(), 0x06000553u);
    // C: two declaring / reduced
    EXPECT_EQ(two->DeclaringType()->ReflectionName(), "System.String");
    EXPECT_EQ(two->ReducedFrom(), nullptr);
    // C: two acc / fullname
    EXPECT_EQ(two->Accessibility(), TS::Accessibility::Public);
    EXPECT_EQ(two->FullName(), "System.String.Concat");
    // The HasBody/ReturnType delegations.
    EXPECT_TRUE(two->HasBody());
    EXPECT_EQ(two->ReturnType().ReflectionName(), "System.String");
}

TEST_F(VarArgInstanceMethodTest, SpecializeBuildsAFreshEqualsEqualRewrap)
{
    // The gold's `C: specialize-identity` drive (re-eq=False, equals=True,
    // tostring-eq=True) -- driven over the stub base (the port's
    // MetadataMethod::Specialize is the SpecializedMethod::Create deferral;
    // the stub's own-Specialize is the same Identity short-circuit the real
    // engine applies for the base method).
    auto stub = std::make_shared<StubVarArgMethod>(
        "Concat", "System.String", "System.String", 4);
    auto two = std::make_shared<TS::VarArgInstanceMethod>(
        stub, std::vector<TS::ITypePtr>{ TS::UnknownType(),
                                         TS::UnknownType() });
    const TS::IMethod* spec
        = two->Specialize(&TS::TypeParameterSubstitution::Identity());
    ASSERT_NE(spec, nullptr);
    // C: specialize-identity refeq=False
    EXPECT_NE(spec, static_cast<const TS::IMethod*>(two.get()));
    // C: specialize-identity equals=True
    EXPECT_TRUE(two->Equals(spec, nullptr));
    // C: specialize-identity tostring-eq=True
    const auto* specWrapper
        = dynamic_cast<const TS::VarArgInstanceMethod*>(spec);
    ASSERT_NE(specWrapper, nullptr);
    EXPECT_EQ(specWrapper->ToString(), two->ToString());
    // The fresh wrapper keeps the vararg types (the substituted-tail arm of
    // the Specialize composition).
    EXPECT_EQ(specWrapper->Parameters().size(), 6u);
    EXPECT_EQ(specWrapper->RegularParameterCount(), 4);
    // The wrapper survives its Specialize results (the rewrap registry).
    const TS::IMethod* spec2
        = two->Specialize(&TS::TypeParameterSubstitution::Identity());
    EXPECT_NE(spec, spec2);
}

TEST_F(VarArgInstanceMethodTest, NullBaseMethodThrows)
{
    EXPECT_THROW(
        TS::VarArgInstanceMethod(nullptr, std::vector<TS::ITypePtr>{}),
        std::invalid_argument);
}

} // namespace
