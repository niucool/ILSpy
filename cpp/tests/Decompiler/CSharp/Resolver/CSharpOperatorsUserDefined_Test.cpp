// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the CSharpOperators user-defined operator region (CSharpOperators.cs lines
// 1104-1168): the public `LiftUserDefinedOperator(IMethod)` static (the lifted `Nullable<T>`
// form of a USER-DEFINED operator method), the internal `IsComparisonOperator(IMethod)`
// static (the metadata-name recognition driving the return-type shape), and the sealed
// `LiftedUserDefinedOperator : SpecializedMethod, ILiftedOperator` class. The region's
// load-bearing cruxes: (1) a COMPARISON operator's lifted form KEEPS the plain `bool`
// return type (only the parameters lift), while a non-comparison operator lifts the
// return type to `Nullable<T>` too; (2) the `SetParameters`/`SetReturnType` protected
// setters are what carry the `Nullable<T>` shape -- without them the inherited lazy
// computation would produce the plain substituted types (int, not Nullable<int>);
// (3) the lift itself does NOT gate on `IsOperator` -- only `IsComparisonOperator` (the
// comparison-name check) and the value-type shape checks run.
//
// The registered-compilation stub is the CSharpOperatorsBitwise_Test.cpp precedent: every
// `KnownTypeCode` the parameter tables and `NullableType.Create` resolve must be
// registered with a shared-managed `LookupTypeDefinition` (the ctors and the
// `shared_from_this` recoveries need shared management, the D529/D578 conventions).

#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"
#include "Decompiler/CSharp/Resolver/ILiftedOperator.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpOperators;
using ILSpy::Decompiler::CSharp::Resolver::ILiftedOperator;
using ILSpy::Decompiler::CSharp::Resolver::LiftedUserDefinedOperator;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::Create;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsKnownType;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::IsNonNullableValueType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A fresh `LookupCompilation` with every `KnownTypeCode` the region resolves registered
// as a shared-managed `LookupTypeDefinition` (the CSharpOperatorsBitwise_Test precedent --
// `NullableType.Create` resolves `NullableOfT` through `FindType`).
struct RegisteredCompilation {
    std::unique_ptr<LookupCompilation> compilation;
    std::vector<std::shared_ptr<LookupTypeDefinition>> types;
    std::shared_ptr<LookupTypeDefinition> nullableOfT;
};

RegisteredCompilation MakeRegisteredCompilation() {
    RegisteredCompilation rc;
    rc.compilation = std::make_unique<LookupCompilation>();
    for (int raw = static_cast<int>(KnownTypeCode::Object);
         raw <= static_cast<int>(KnownTypeCode::String); ++raw) {
        KnownTypeCode code = static_cast<KnownTypeCode>(raw);
        std::string name = "T" + std::to_string(raw);
        auto def = std::make_shared<LookupTypeDefinition>(
            name, "", FullTypeName(TopLevelTypeName("", name, 0)),
            TypeKind::Struct, Accessibility::Public, *rc.compilation, nullptr, code);
        rc.compilation->RegisterKnownType(code, def.get());
        rc.types.push_back(std::move(def));
    }
    rc.nullableOfT = std::make_shared<LookupTypeDefinition>(
        "Nullable", "System", FullTypeName(TopLevelTypeName("System", "Nullable", 1)),
        TypeKind::Struct, Accessibility::Public, *rc.compilation, nullptr,
        KnownTypeCode::NullableOfT);
    rc.compilation->RegisterKnownType(KnownTypeCode::NullableOfT, rc.nullableOfT.get());
    return rc;
}

// The shared registered compilation (the registrations are kept alive by the static struct
// for the program's lifetime -- the LookupCompilation stores only raw pointers).
RegisteredCompilation& TheCompilation() {
    static RegisteredCompilation rc = MakeRegisteredCompilation();
    return rc;
}

LookupCompilation& Compilation() {
    return *TheCompilation().compilation;
}

// A `LookupTypeDefinition` that reports itself as a reference type (a definite
// `IsReferenceType == true`, unlike the inherited `std::nullopt` default) -- the
// by-value-parameter rejection crux needs the DEFINITE reference-type answer.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

// A `LookupTypeDefinition` that reports itself as a value type (a definite
// `IsReferenceType == false`) -- `IsNonNullableValueType` needs the DEFINITE value-type
// answer (the RunTypeInference_Test ValueTypeDef precedent).
class ValueTypeDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return false; }
};

// A primitive value-type definition with the given `KnownTypeCode` (shared-managed: the
// `AcceptVisitor`/`shared_from_this` calls in the lift need it, the D578 convention).
std::shared_ptr<ValueTypeDef> MakeValueDef(KnownTypeCode ktc) {
    int n = static_cast<int>(ktc);
    std::string name = "V" + std::to_string(n);
    return std::make_shared<ValueTypeDef>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A reference-type definition (the by-value-parameter / reference-return rejection cruxes).
std::shared_ptr<RefDef> MakeRefDef() {
    return std::make_shared<RefDef>(
        "R", "", FullTypeName(TopLevelTypeName("", "R", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
}

// A `Nullable<T>` over a value-type element (the nullable return/parameter rejection
// cruxes -- built through the registered `NullableOfT`, the Create convention).
ITypePtr MakeNullableOf(const IType& elementType) {
    return Create(Compilation(), elementType);
}

// A configurable `IParameter` (the CSharpConversionsDelegateCompatible_Test TestParameter
// precedent -- each test file carries its own stub).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type)
        : type_(std::move(type)) {}
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return "p"; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    { return {}; }
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    { return ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override
    { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
    ITypePtr type_;
};

// A configured operator method: a `LookupMethod` with the metadata name, the `IsOperator`
// flag, the return type, and the parameter types. The method, its parameters, and their
// types are kept alive for the program's lifetime (static keep vectors -- the lifted
// operator holds the method handle, and the method's `Parameters()` snapshot is
// non-owning, so the parameters must outlive every lift).
std::shared_ptr<LookupMethod> MakeOperatorMethod(
    const std::string& name,
    ITypePtr returnType,
    std::vector<ITypePtr> paramTypes,
    bool isOperator = true)
{
    static std::vector<std::shared_ptr<LookupMethod>> methods;
    static std::vector<std::vector<std::shared_ptr<TestParameter>>> params;
    static std::vector<ITypePtr> types;
    types.insert(types.end(), paramTypes.begin(), paramTypes.end());
    types.push_back(returnType);
    auto m = std::make_shared<LookupMethod>(name, Compilation());
    m->SetIsOperator(isOperator);
    m->SetReturnType(std::move(returnType));
    std::vector<std::shared_ptr<TestParameter>> owned;
    std::vector<const IParameter*> raw;
    for (auto& t : paramTypes) {
        owned.push_back(std::make_shared<TestParameter>(t));
        raw.push_back(owned.back().get());
    }
    m->SetParameters(std::move(raw));
    params.push_back(std::move(owned));
    methods.push_back(m);
    return m;
}

// The shared int/bool value-type singletons (the type-cache model -- the element
// instances are reused across the methods so the pointer-identity assertions hold).
std::shared_ptr<ValueTypeDef> IntDef() {
    static auto t = MakeValueDef(KnownTypeCode::Int32);
    return t;
}

std::shared_ptr<ValueTypeDef> BoolDef() {
    static auto t = MakeValueDef(KnownTypeCode::Boolean);
    return t;
}

} // namespace

// ---------------------------------------------------------------------------
// IsComparisonOperator(IMethod) -- the metadata-name recognition
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsUserDefinedTest, RecognizesALessThanOperatorByMetadataName) {
    auto m = MakeOperatorMethod("op_LessThan", BoolDef(), {IntDef(), IntDef()});
    EXPECT_TRUE(CSharpOperators::IsComparisonOperator(*m));
}

TEST(CSharpOperatorsUserDefinedTest, RecognizesTheEqualityOperator) {
    auto m = MakeOperatorMethod("op_Equality", BoolDef(), {IntDef(), IntDef()});
    EXPECT_TRUE(CSharpOperators::IsComparisonOperator(*m));
}

TEST(CSharpOperatorsUserDefinedTest, RecognizesGreaterThanOrEqual) {
    auto m = MakeOperatorMethod("op_GreaterThanOrEqual", BoolDef(), {IntDef(), IntDef()});
    EXPECT_TRUE(CSharpOperators::IsComparisonOperator(*m));
}

TEST(CSharpOperatorsUserDefinedTest, RequiresTheIsOperatorFlag) {
    auto m = MakeOperatorMethod("op_LessThan", BoolDef(), {IntDef(), IntDef()},
                                /*isOperator=*/false);
    EXPECT_FALSE(CSharpOperators::IsComparisonOperator(*m));
}

TEST(CSharpOperatorsUserDefinedTest, RequiresExactlyTwoParameters) {
    auto oneParam = MakeOperatorMethod("op_LessThan", BoolDef(), {IntDef()});
    EXPECT_FALSE(CSharpOperators::IsComparisonOperator(*oneParam));
    auto threeParams = MakeOperatorMethod(
        "op_LessThan", BoolDef(), {IntDef(), IntDef(), IntDef()});
    EXPECT_FALSE(CSharpOperators::IsComparisonOperator(*threeParams));
}

TEST(CSharpOperatorsUserDefinedTest, NonComparisonOperatorNameIsRejected) {
    // `op_Addition` resolves through `GetOperatorType` but is not one of the six
    // comparison kinds.
    auto m = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    EXPECT_FALSE(CSharpOperators::IsComparisonOperator(*m));
}

TEST(CSharpOperatorsUserDefinedTest, NonOperatorMethodNameIsRejected) {
    // "M" does not resolve through `GetOperatorType` at all (`?? false`).
    auto m = MakeOperatorMethod("M", BoolDef(), {IntDef(), IntDef()});
    EXPECT_FALSE(CSharpOperators::IsComparisonOperator(*m));
}

// ---------------------------------------------------------------------------
// LiftUserDefinedOperator(IMethod) -- the lift guards and shapes
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsUserDefinedTest, LiftsAValueOperatorIntoNullableParameters) {
    auto m = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto lifted = CSharpOperators::LiftUserDefinedOperator(m);
    ASSERT_NE(lifted, nullptr);
    // The parameters lift to Nullable<int>: each parameter's type is nullable, with the
    // underlying element pointer-identical to the original int singleton. This pins the
    // `SetParameters` wiring -- the inherited lazy computation would produce plain int.
    auto parameters = lifted->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    for (const IParameter* p : parameters) {
        EXPECT_TRUE(IsNullable(p->Type()));
        EXPECT_EQ(&GetUnderlyingType(p->Type()), static_cast<const IType*>(IntDef().get()));
    }
    // The non-comparison return type lifts to Nullable<int> too (the SetReturnType
    // wiring -- the lazy computation would produce plain int).
    EXPECT_TRUE(IsNullable(lifted->ReturnType()));
    EXPECT_EQ(&GetUnderlyingType(lifted->ReturnType()),
              static_cast<const IType*>(IntDef().get()));
}

TEST(CSharpOperatorsUserDefinedTest, ComparisonOperatorLiftKeepsPlainBoolReturn) {
    // The region's flagship crux: a comparison operator's lifted form KEEPS the plain
    // `bool` return type (the C# comment at line 1141: "Comparison operators keep the
    // 'bool' return type even when lifted") -- only the parameters lift.
    auto m = MakeOperatorMethod("op_LessThan", BoolDef(), {IntDef(), IntDef()});
    auto lifted = CSharpOperators::LiftUserDefinedOperator(m);
    ASSERT_NE(lifted, nullptr);
    EXPECT_EQ(&lifted->ReturnType(), static_cast<const IType*>(BoolDef().get()));
    EXPECT_FALSE(IsNullable(lifted->ReturnType()));
    // ... while the parameters still lift.
    auto parameters = lifted->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    for (const IParameter* p : parameters) {
        EXPECT_TRUE(IsNullable(p->Type()));
    }
}

TEST(CSharpOperatorsUserDefinedTest, ComparisonOperatorWithNonBoolReturnIsRejected) {
    // A comparison operator that does not return `bool` cannot be lifted.
    auto m = MakeOperatorMethod("op_LessThan", IntDef(), {IntDef(), IntDef()});
    EXPECT_EQ(CSharpOperators::LiftUserDefinedOperator(m), nullptr);
}

TEST(CSharpOperatorsUserDefinedTest, NonComparisonOperatorWithReferenceReturnIsRejected) {
    auto m = MakeOperatorMethod("op_Addition", MakeRefDef(), {IntDef(), IntDef()});
    EXPECT_EQ(CSharpOperators::LiftUserDefinedOperator(m), nullptr);
}

TEST(CSharpOperatorsUserDefinedTest, NonComparisonOperatorWithNullableReturnIsRejected) {
    // A Nullable<int> return is not a NON-nullable value type.
    auto m = MakeOperatorMethod("op_Addition", MakeNullableOf(*IntDef()),
                                {IntDef(), IntDef()});
    EXPECT_EQ(CSharpOperators::LiftUserDefinedOperator(m), nullptr);
}

TEST(CSharpOperatorsUserDefinedTest, ReferenceTypedParameterIsRejected) {
    auto m = MakeOperatorMethod("op_Addition", IntDef(), {MakeRefDef(), IntDef()});
    EXPECT_EQ(CSharpOperators::LiftUserDefinedOperator(m), nullptr);
}

TEST(CSharpOperatorsUserDefinedTest, NullableParameterIsRejected) {
    auto m = MakeOperatorMethod("op_Addition", IntDef(),
                                {MakeNullableOf(*IntDef()), IntDef()});
    EXPECT_EQ(CSharpOperators::LiftUserDefinedOperator(m), nullptr);
}

TEST(CSharpOperatorsUserDefinedTest, NullHandleIsRejected) {
    // The degenerate null handle (the C# would NRE on `m.ReturnType`) gets the documented
    // safe fallback: the same "cannot lift" null.
    std::shared_ptr<IMethod> null;
    EXPECT_EQ(CSharpOperators::LiftUserDefinedOperator(null), nullptr);
}

TEST(CSharpOperatorsUserDefinedTest, NotAnOperatorMethodStillLifts) {
    // The lift itself does NOT gate on `IsOperator` -- only the comparison-name check
    // (inside `IsComparisonOperator`) and the value-type shape checks run. A plain
    // method with a value-type shape lifts all the same (the C# callers are responsible
    // for passing operator methods).
    auto m = MakeOperatorMethod("M", IntDef(), {IntDef(), IntDef()},
                                /*isOperator=*/false);
    auto lifted = CSharpOperators::LiftUserDefinedOperator(m);
    ASSERT_NE(lifted, nullptr);
    EXPECT_TRUE(IsNullable(lifted->ReturnType()));
}

// ---------------------------------------------------------------------------
// LiftedUserDefinedOperator -- the SpecializedMethod + ILiftedOperator surface
// ---------------------------------------------------------------------------

TEST(CSharpOperatorsUserDefinedTest, IsAnIMethodAndAnILiftedOperator) {
    static_assert(std::is_base_of_v<IMethod, LiftedUserDefinedOperator>);
    static_assert(std::is_base_of_v<ILiftedOperator, LiftedUserDefinedOperator>);
    static_assert(std::is_final_v<LiftedUserDefinedOperator>);
    auto m = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto lifted = CSharpOperators::LiftUserDefinedOperator(m);
    ASSERT_NE(lifted, nullptr);
    // The cross-cast from the IMethod base to the standalone ILiftedOperator base (the
    // BetterFunctionMember `dynamic_cast<IMember> -> ILiftedOperator` shape).
    const ILiftedOperator* marker = dynamic_cast<const ILiftedOperator*>(lifted.get());
    ASSERT_NE(marker, nullptr);
}

TEST(CSharpOperatorsUserDefinedTest, NonLiftedSurfaceExposesTheOriginals) {
    auto m = MakeOperatorMethod("op_LessThan", BoolDef(), {IntDef(), IntDef()});
    auto lifted = std::make_shared<LiftedUserDefinedOperator>(m);
    const ILiftedOperator* marker = dynamic_cast<const ILiftedOperator*>(lifted.get());
    ASSERT_NE(marker, nullptr);
    // `NonLiftedParameters` / `NonLiftedReturnType` expose the ORIGINAL (un-lifted)
    // signature -- pointer-identical to the method's own parameters / return type.
    auto nonLiftedParameters = marker->NonLiftedParameters();
    auto originalParameters = m->Parameters();
    ASSERT_EQ(nonLiftedParameters.size(), originalParameters.size());
    for (std::size_t i = 0; i < nonLiftedParameters.size(); i++) {
        EXPECT_EQ(nonLiftedParameters[i], originalParameters[i]);
    }
    EXPECT_EQ(&marker->NonLiftedReturnType(), static_cast<const IType*>(BoolDef().get()));
}

TEST(CSharpOperatorsUserDefinedTest, MemberDefinitionIsTheOriginalMethod) {
    // The base-ctor aliasing handle: the specialized method's `MemberDefinition` is the
    // non-lifted method itself (an unspecialized method IS its own definition).
    auto m = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto lifted = std::make_shared<LiftedUserDefinedOperator>(m);
    EXPECT_EQ(lifted->MemberDefinition(), static_cast<const ILSpy::Decompiler::TypeSystem::IMember*>(m.get()));
}

TEST(CSharpOperatorsUserDefinedTest, DirectConstructionLiftsParametersAndReturn) {
    // The ctor exercised directly (independent of the static's guards): the parameters
    // lift to Nullable<int> and the non-comparison return lifts to Nullable<int>.
    auto m = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto lifted = std::make_shared<LiftedUserDefinedOperator>(m);
    auto parameters = lifted->Parameters();
    ASSERT_EQ(parameters.size(), 2u);
    for (const IParameter* p : parameters) {
        EXPECT_TRUE(IsNullable(p->Type()));
        EXPECT_EQ(&GetUnderlyingType(p->Type()), static_cast<const IType*>(IntDef().get()));
    }
    EXPECT_TRUE(IsNullable(lifted->ReturnType()));
    EXPECT_EQ(&GetUnderlyingType(lifted->ReturnType()),
              static_cast<const IType*>(IntDef().get()));
}

TEST(CSharpOperatorsUserDefinedTest, DirectConstructionKeepsTheComparisonReturn) {
    // The ctor's comparison branch, exercised directly: the return type stays the plain
    // `bool` while the parameters lift.
    auto m = MakeOperatorMethod("op_LessThan", BoolDef(), {IntDef(), IntDef()});
    auto lifted = std::make_shared<LiftedUserDefinedOperator>(m);
    EXPECT_EQ(&lifted->ReturnType(), static_cast<const IType*>(BoolDef().get()));
    for (const IParameter* p : lifted->Parameters()) {
        EXPECT_TRUE(IsNullable(p->Type()));
    }
}

TEST(CSharpOperatorsUserDefinedTest, EqualsComparesTheNonLiftedOperators) {
    auto m1 = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto m2 = MakeOperatorMethod("op_Subtraction", IntDef(), {IntDef(), IntDef()});
    auto a = std::make_shared<LiftedUserDefinedOperator>(m1);
    auto aAgain = std::make_shared<LiftedUserDefinedOperator>(m1);
    auto b = std::make_shared<LiftedUserDefinedOperator>(m2);
    // Two lifts of the SAME non-lifted operator are equal (the stub's identity Equals).
    EXPECT_TRUE(a->Equals(aAgain.get()));
    // Lifts of different operators are not.
    EXPECT_FALSE(a->Equals(b.get()));
    // The null comparison is false.
    EXPECT_FALSE(a->Equals(nullptr));
}

TEST(CSharpOperatorsUserDefinedTest, GetHashCodeIsStableForTheSameMethod) {
    // The C# Equals/GetHashCode contract: two equal lifts (the same non-lifted operator)
    // carry the same hash (`nonLiftedOperator.GetHashCode() ^ 0x7191254`).
    auto m = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto a = std::make_shared<LiftedUserDefinedOperator>(m);
    auto aAgain = std::make_shared<LiftedUserDefinedOperator>(m);
    EXPECT_EQ(a->GetHashCode(), aAgain->GetHashCode());
}

TEST(CSharpOperatorsUserDefinedTest, LiftedParametersAreSpecializedOverTheOriginals) {
    // The parameters are the substituted `SpecializedParameter` wrappers (the
    // `CreateParameters` output): each lifts the ORIGINAL parameter's type into
    // `Nullable<T>` while delegating the rest of the parameter surface (the name) to the
    // original parameter.
    auto m = MakeOperatorMethod("op_Addition", IntDef(), {IntDef(), IntDef()});
    auto lifted = CSharpOperators::LiftUserDefinedOperator(m);
    ASSERT_NE(lifted, nullptr);
    auto originalParameters = m->Parameters();
    auto parameters = lifted->Parameters();
    ASSERT_EQ(parameters.size(), originalParameters.size());
    for (std::size_t i = 0; i < parameters.size(); i++) {
        EXPECT_EQ(parameters[i]->Name(), originalParameters[i]->Name());
        EXPECT_NE(&parameters[i]->Type(), &originalParameters[i]->Type());
    }
}
