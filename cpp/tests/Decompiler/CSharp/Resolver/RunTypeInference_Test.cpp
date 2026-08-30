// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
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

// Tests for `OverloadResolution`'s `RunTypeInference` engine step (OverloadResolution.cs
// lines 438-486, the "RunTypeInference" region) and its `ConstraintValidatingSubstitution`
// visitor (lines 503-521) -- the type-inference step of `CalculateCandidate`, ported as
// `Detail::RunTypeInference` / `Detail::ConstraintValidatingSubstitution` (the
// instance-state-threading lift: `compilation`/`conversions`/`arguments`/
// `explicitlyGivenTypeArguments` are parameters, the D536 convention).
//
// The load-bearing cruxes:
//  (a) the NON-GENERIC arm -- a candidate with no method type parameters adds
//      `WrongNumberOfTypeArguments` only when explicit type arguments were given, and
//      either way re-grabs the parameter types from the SPECIALIZED member
//      (`ResolveParameterTypes(candidate, true)` -- `candidate.Member.Parameters`, NOT the
//      definition's parameters the candidate ctor captured);
//  (b) the EXPLICIT type-arguments arm -- a matching count becomes `InferredTypes` as-is;
//      a mismatch adds `WrongNumberOfTypeArguments` and TRUNCATES the too-long list /
//      pads the too-short list with `UnknownType`;
//  (c) the INFERENCE arm -- the ported `Detail::InferTypeArguments` engine runs over the
//      `ArgumentToParameterMap`-projected parameter types (the flagship `M<T>(T x)` +
//      int-argument shape infers `T = int`), and an ambiguous inference adds
//      `TypeInferenceFailed`;
//  (d) the class type arguments -- the member's declaring type contributes the CLASS type
//      arguments when it is a `ParameterizedType` (the `C<int>` in `c.M<U>(...)`), which the
//      final substitution applies to class-owned type parameters in the formal parameter
//      types;
//  (e) the final substitution -- the merged class+inferred-method substitution is applied
//      to every formal parameter type (a type not mentioning any type parameter keeps its
//      identity -- no spurious rebuild), and the `ConstraintValidatingSubstitution` flags
//      `ConstructedTypeDoesNotSatisfyConstraint` when a constructed generic type violates
//      its type-parameter constraints (the `where T : struct` violation over `G<string>`),
//      staying silent when the constraint is satisfied (`G<int>`).
//
// The stubs mirror the ConsiderIfNewCandidateIsBest_Test / TypeInferenceInferTypeArguments_
// Test conventions (`TestMethod`/`TestParameter`, the `VisitableTypeParameter`
// `AcceptVisitor` bridge) plus the `ValidateConstraints_Test` `ValueTypeDef`/`RefDef`
// definite-reference-ness stubs (the `IsNonNullableValueType` struct-constraint check needs
// a DEFINITE `IsReferenceType == false`).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ConstraintValidatingSubstitution;
using ILSpy::Decompiler::CSharp::Resolver::Detail::RunTypeInference;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

// The shared compilation (a plain `LookupCompilation` -- none of the shapes tested here
// needs a `FindType` registration; the conversions resolve over the stubs' own
// structural equality).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeParameter` overriding `AcceptVisitor` to dispatch to
// `visitor.VisitTypeParameter` (the C# `AbstractTypeParameter.AcceptVisitor` bridge) --
// the plain `LookupTypeParameter` routes to `VisitOtherType`, so the final substitution's
// `VisitTypeParameter` would never fire for it (the D564 `VisitableTypeParameter`
// precedent).
class VisitableTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;
    ITypePtr AcceptVisitor(ILSpy::Decompiler::TypeSystem::TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
};

// A `VisitableTypeParameter` owned by a TYPE DEFINITION (not a method) -- the class-level
// type parameter the declaring type's class type arguments substitute (the substitution
// dispatches on `OwnerType`).
class ClassOwnedTypeParameter : public VisitableTypeParameter {
public:
    using VisitableTypeParameter::VisitableTypeParameter;
    TS::SymbolKind OwnerType() const override { return TS::SymbolKind::TypeDefinition; }
};

// A `LookupTypeDefinition` that reports itself as a reference type (a definite
// `IsReferenceType == true`, the D563 `RefDef` precedent).
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

// A `LookupTypeDefinition` that reports itself as a value type (a definite
// `IsReferenceType == false`, unlike the inherited `std::nullopt` default) -- the struct
// constraint's `IsNonNullableValueType` check needs the DEFINITE value-type answer.
class ValueTypeDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return false; }
};

// A primitive definition (`LookupTypeDefinition`, IS-A `ITypeDefinition`) with the given
// `KnownTypeCode` / `TypeKind` -- the D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Class) {
    int n = static_cast<int>(ktc);
    std::string name = "R" + std::to_string(n);
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

std::shared_ptr<ValueTypeDef> MakeValueDef(KnownTypeCode ktc) {
    int n = static_cast<int>(ktc);
    std::string name = "V" + std::to_string(n);
    return std::make_shared<ValueTypeDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A minimal by-value `IParameter` over a configured type (the D536/D550 `TestParameter`
// precedent, pruned to the fields the candidate ctor / `ResolveParameterTypes` read).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const IParameterizedMember* Owner() const override { return nullptr; }
    LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
};

// A `LookupMethod` with configurable `Parameters` (inherited), method `TypeParameters`
// (the candidate ctor reads them from the member DEFINITION -- this stub IS its own
// definition), `DeclaringType` (the class type-arguments source), and `MemberDefinition`
// (a separate definition for the specialized-member shape). The D536/D550 `TestMethod`
// precedent extended additively.
class TestMethod : public LookupMethod {
public:
    explicit TestMethod(std::string name = "M")
        : LookupMethod(std::move(name), Compilation()) {}
    void SetTypeParameters(std::vector<const ITypeParameter*> tps) { typeParameters_ = std::move(tps); }
    void SetDeclaringType(ITypePtr declaringType) { declaringType_ = std::move(declaringType); }
    void SetMemberDefinition(const LookupMethod* definition) { memberDefinition_ = definition; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override {
        return memberDefinition_ != nullptr ? memberDefinition_ : this;
    }
    ITypePtr DeclaringType() const override { return declaringType_; }

    // --- IMethod ---
    std::vector<const ITypeParameter*> TypeParameters() const override { return typeParameters_; }

private:
    std::vector<const ITypeParameter*> typeParameters_;
    ITypePtr declaringType_;
    const LookupMethod* memberDefinition_ = nullptr;
};

// A shared `VisitableTypeParameter` with the given index (the real `InferTypeArguments`
// contract is `typeParameters[i].Index == i`).
std::shared_ptr<VisitableTypeParameter> MakeTypeParam(std::string name, int index) {
    auto t = std::make_shared<VisitableTypeParameter>(std::move(name));
    t->SetIndex(index);
    return t;
}

// A plain `ResolveResult` over the given type (the base class is concrete; the inference's
// `PhaseOne` reads only `Type()` for a non-lambda/non-method-group argument).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

bool HasError(const OverloadResolutionCandidate& c, OverloadResolutionErrors error) {
    return (c.Errors() & error) != OverloadResolutionErrors::None;
}

// ---- The non-generic arm: re-grab the SPECIALIZED parameter types ----

// A non-generic candidate without explicit type arguments adds NO error and re-grabs the
// parameter types from the SPECIALIZED member (`candidate.Member.Parameters`), not from
// the definition's parameters the candidate ctor captured -- the specialized-vs-definition
// divergence pins the `ResolveParameterTypes(candidate, true)` call.
TEST(RunTypeInferenceTest, NonGenericGrabsSpecializedParameterTypes)
{
    auto defInt = MakeDef(KnownTypeCode::Int32);
    auto specInt = MakeDef(KnownTypeCode::Int64);
    TestParameter defParam(defInt, "def");
    TestParameter specParam(specInt, "spec");
    TestMethod definition("D");
    definition.SetParameters({&defParam});
    TestMethod member("M");
    member.SetParameters({&specParam});
    member.SetMemberDefinition(&definition);
    OverloadResolutionCandidate c(&member, false);
    CSharpConversions conversions(Compilation());
    RunTypeInference(c, Compilation(), conversions, {}, std::nullopt);
    EXPECT_EQ(c.ParameterTypes().size(), 1u);
    ASSERT_NE(c.ParameterTypes()[0], nullptr);
    // The SPECIALIZED member's parameter type (specInt), NOT the definition's (defInt).
    EXPECT_EQ(c.ParameterTypes()[0].get(), specInt.get());
    EXPECT_NE(c.ParameterTypes()[0].get(), defInt.get());
    EXPECT_EQ(c.ErrorCount(), 0);
}

// A non-generic candidate without explicit type arguments reports no errors (the no-error
// sentinel -- the error-free twin of the WrongNumberOfTypeArguments crux below).
TEST(RunTypeInferenceTest, NonGenericWithoutExplicitTypeArgumentsAddsNoError)
{
    auto defInt = MakeDef(KnownTypeCode::Int32);
    TestParameter defParam(defInt, "p");
    TestMethod member("M");
    member.SetParameters({&defParam});
    OverloadResolutionCandidate c(&member, false);
    CSharpConversions conversions(Compilation());
    RunTypeInference(c, Compilation(), conversions, {}, std::nullopt);
    EXPECT_EQ(c.Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(c.ErrorCount(), 0);
}

// A non-generic candidate WITH explicit type arguments adds WrongNumberOfTypeArguments
// (the method does not expect type arguments, but was given some) and still re-grabs the
// specialized parameter types.
TEST(RunTypeInferenceTest, NonGenericWithExplicitTypeArgumentsAddsWrongNumberError)
{
    auto specInt = MakeDef(KnownTypeCode::Int64);
    TestParameter specParam(specInt, "p");
    TestMethod member("M");
    member.SetParameters({&specParam});
    OverloadResolutionCandidate c(&member, false);
    CSharpConversions conversions(Compilation());
    RunTypeInference(c, Compilation(), conversions, {},
                     std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{MakeDef(KnownTypeCode::Int32)}});
    EXPECT_TRUE(HasError(c, OverloadResolutionErrors::WrongNumberOfTypeArguments));
    EXPECT_EQ(c.ErrorCount(), 1);
    ASSERT_NE(c.ParameterTypes()[0], nullptr);
    EXPECT_EQ(c.ParameterTypes()[0].get(), specInt.get());
}

// ---- The explicit type-arguments arm ----

// Explicit type arguments of the MATCHING count become the candidate's InferredTypes
// as-is, and the final substitution applies them to the formal parameter types.
TEST(RunTypeInferenceTest, ExplicitTypeArgumentsMatchingCountBecomeInferredTypes)
{
    auto t = MakeTypeParam("T", 0);
    auto given = MakeDef(KnownTypeCode::Int32);
    TestParameter param(t, "x");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param});
    OverloadResolutionCandidate c(&method, false);
    c.ParameterTypes() = {t};
    CSharpConversions conversions(Compilation());
    RunTypeInference(c, Compilation(), conversions, {},
                     std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{given}});
    ASSERT_EQ(c.InferredTypes().size(), 1u);
    EXPECT_EQ(c.InferredTypes()[0].get(), given.get());
    EXPECT_FALSE(HasError(c, OverloadResolutionErrors::WrongNumberOfTypeArguments));
    EXPECT_EQ(c.ErrorCount(), 0);
    // The final substitution applied the explicit type argument to the formal parameter
    // type (`T` -> the given type).
    ASSERT_NE(c.ParameterTypes()[0], nullptr);
    EXPECT_EQ(c.ParameterTypes()[0].get(), given.get());
}

// Too FEW explicit type arguments: WrongNumberOfTypeArguments plus the list padded with
// UnknownType up to the type-parameter count.
TEST(RunTypeInferenceTest, ExplicitTypeArgumentsTooFewPadWithUnknownType)
{
    auto t0 = MakeTypeParam("T0", 0);
    auto t1 = MakeTypeParam("T1", 1);
    auto given = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    method.SetTypeParameters({t0.get(), t1.get()});
    OverloadResolutionCandidate c(&method, false);
    CSharpConversions conversions(Compilation());
    RunTypeInference(c, Compilation(), conversions, {},
                     std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{given}});
    EXPECT_TRUE(HasError(c, OverloadResolutionErrors::WrongNumberOfTypeArguments));
    ASSERT_EQ(c.InferredTypes().size(), 2u);
    EXPECT_EQ(c.InferredTypes()[0].get(), given.get());
    ASSERT_NE(c.InferredTypes()[1], nullptr);
    EXPECT_EQ(c.InferredTypes()[1]->Kind(), TypeKind::Unknown);
}

// Too MANY explicit type arguments: WrongNumberOfTypeArguments plus the list TRUNCATED
// to the type-parameter count (the surplus arguments are discarded).
TEST(RunTypeInferenceTest, ExplicitTypeArgumentsTooManyTruncate)
{
    auto t0 = MakeTypeParam("T0", 0);
    auto first = MakeDef(KnownTypeCode::Int32);
    auto second = MakeDef(KnownTypeCode::String, TypeKind::Class);
    TestMethod method("M");
    method.SetTypeParameters({t0.get()});
    OverloadResolutionCandidate c(&method, false);
    CSharpConversions conversions(Compilation());
    RunTypeInference(c, Compilation(), conversions, {},
                     std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{first, second}});
    EXPECT_TRUE(HasError(c, OverloadResolutionErrors::WrongNumberOfTypeArguments));
    ASSERT_EQ(c.InferredTypes().size(), 1u);
    EXPECT_EQ(c.InferredTypes()[0].get(), first.get());
}

// ---- The inference arm (the ported TypeInference engine) ----

// The flagship inference shape: `M<T>(T x)` called with an int argument infers `T = int`
// and the final substitution rewrites the formal parameter type `T` to the inferred type.
TEST(RunTypeInferenceTest, InferenceInfersFromArgumentAndSubstitutesParameterTypes)
{
    auto t = MakeTypeParam("T", 0);
    auto int32 = MakeDef(KnownTypeCode::Int32);
    TestParameter param(t, "x");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param});
    OverloadResolutionCandidate c(&method, false);
    c.ParameterTypes() = {t};
    c.ArgumentToParameterMap() = {0};
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(int32)};
    RunTypeInference(c, Compilation(), conversions, arguments, std::nullopt);
    ASSERT_EQ(c.InferredTypes().size(), 1u);
    ASSERT_NE(c.InferredTypes()[0], nullptr);
    EXPECT_EQ(c.InferredTypes()[0].get(), int32.get());
    EXPECT_EQ(c.ErrorCount(), 0);
    ASSERT_NE(c.ParameterTypes()[0], nullptr);
    EXPECT_EQ(c.ParameterTypes()[0].get(), int32.get());
}

// An AMBIGUOUS inference (both int and string lower-bound the same `T`) fails the fix and
// adds TypeInferenceFailed.
TEST(RunTypeInferenceTest, AmbiguousInferenceAddsTypeInferenceFailed)
{
    auto t = MakeTypeParam("T", 0);
    auto int32 = MakeDef(KnownTypeCode::Int32);
    auto str = MakeDef(KnownTypeCode::String, TypeKind::Class);
    TestParameter param0(t, "a");
    TestParameter param1(t, "b");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param0, &param1});
    OverloadResolutionCandidate c(&method, false);
    c.ParameterTypes() = {t, t};
    c.ArgumentToParameterMap() = {0, 1};
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(int32), Arg(str)};
    RunTypeInference(c, Compilation(), conversions, arguments, std::nullopt);
    EXPECT_TRUE(HasError(c, OverloadResolutionErrors::TypeInferenceFailed));
    EXPECT_EQ(c.ErrorCount(), 1);
}

// ---- The class type arguments (the parameterized declaring type) ----

// The member's declaring type contributes the CLASS type arguments when it is a
// `ParameterizedType`: `c.M<U>(TClass x, U y)` on `C<int>` substitutes the class-owned
// `TClass` with the declaring type's `int` (and `U` with the inferred method type
// argument).
TEST(RunTypeInferenceTest, ClassTypeArgumentsFromParameterizedDeclaringType)
{
    auto cDef = MakeDef(KnownTypeCode::None, TypeKind::Class);
    auto classArg = MakeDef(KnownTypeCode::Int32);
    auto declaringType = std::make_shared<ParameterizedType>(cDef, std::vector<ITypePtr>{classArg});
    auto tClass = std::make_shared<ClassOwnedTypeParameter>("TClass");
    tClass->SetIndex(0);
    auto u = MakeTypeParam("U", 0);
    auto int32 = MakeDef(KnownTypeCode::Int64);
    TestParameter param0(tClass, "a");
    TestParameter param1(u, "b");
    TestMethod method("M");
    method.SetTypeParameters({u.get()});
    method.SetParameters({&param0, &param1});
    method.SetDeclaringType(declaringType);
    OverloadResolutionCandidate c(&method, false);
    c.ParameterTypes() = {tClass, u};
    c.ArgumentToParameterMap() = {0, 1};
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(classArg), Arg(int32)};
    RunTypeInference(c, Compilation(), conversions, arguments, std::nullopt);
    EXPECT_EQ(c.ErrorCount(), 0);
    ASSERT_NE(c.ParameterTypes()[0], nullptr);
    // The class-owned `TClass` was substituted with the DECLARING TYPE's type argument.
    EXPECT_EQ(c.ParameterTypes()[0].get(), classArg.get());
    ASSERT_NE(c.ParameterTypes()[1], nullptr);
    // The method-owned `U` was substituted with the INFERRED type argument.
    EXPECT_EQ(c.ParameterTypes()[1].get(), int32.get());
    ASSERT_EQ(c.InferredTypes().size(), 1u);
    ASSERT_NE(c.InferredTypes()[0], nullptr);
    EXPECT_EQ(c.InferredTypes()[0].get(), int32.get());
}

// ---- The final substitution + the ConstraintValidatingSubstitution ----

// A formal parameter type not mentioning any type parameter keeps its IDENTITY through
// the substitution (no spurious rebuild), and a successful inference reports no errors.
TEST(RunTypeInferenceTest, UnrelatedParameterTypeStaysSameInstance)
{
    auto t = MakeTypeParam("T", 0);
    auto int32 = MakeDef(KnownTypeCode::Int32);
    auto int64 = MakeDef(KnownTypeCode::Int64);
    TestParameter param0(int32, "a");
    TestParameter param1(t, "b");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param0, &param1});
    OverloadResolutionCandidate c(&method, false);
    c.ParameterTypes() = {int32, t};
    c.ArgumentToParameterMap() = {0, 1};
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(int32), Arg(int64)};
    RunTypeInference(c, Compilation(), conversions, arguments, std::nullopt);
    EXPECT_EQ(c.Errors(), OverloadResolutionErrors::None);
    ASSERT_NE(c.ParameterTypes()[0], nullptr);
    EXPECT_EQ(c.ParameterTypes()[0].get(), int32.get());
}

// The `where Tstruct : struct` constraint SATISFIED: `M<T>(G<T> a, T b)` with an int
// argument infers `T = int`, constructs `G<int>`, and the constraint validation stays
// silent (no ConstructedTypeDoesNotSatisfyConstraint).
TEST(RunTypeInferenceTest, SatisfiedConstraintAddsNoConstructedTypeError)
{
    auto structT = std::make_shared<LookupTypeParameter>("Tstruct");
    structT->SetHasValueTypeConstraint(true);
    auto gDef = MakeDef(KnownTypeCode::None, TypeKind::Class);
    gDef->SetTypeParameters({structT.get()});
    auto t = MakeTypeParam("T", 0);
    auto intVT = MakeValueDef(KnownTypeCode::Int32);
    auto gOfT = std::make_shared<ParameterizedType>(gDef, std::vector<ITypePtr>{t});
    TestParameter param0(gOfT, "a");
    TestParameter param1(t, "b");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param0, &param1});
    OverloadResolutionCandidate c(&method, false);
    c.ParameterTypes() = {gOfT, t};
    c.ArgumentToParameterMap() = {0, 1};
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(MakeDef(KnownTypeCode::String, TypeKind::Class)), Arg(intVT)};
    RunTypeInference(c, Compilation(), conversions, arguments, std::nullopt);
    EXPECT_FALSE(HasError(c, OverloadResolutionErrors::ConstructedTypeDoesNotSatisfyConstraint));
    EXPECT_EQ(c.ErrorCount(), 0);
    // The `G<T>` parameter type was rebuilt as `G<int>`.
    ASSERT_NE(c.ParameterTypes()[0], nullptr);
    const auto* constructed = dynamic_cast<const ParameterizedType*>(c.ParameterTypes()[0].get());
    ASSERT_NE(constructed, nullptr);
    ASSERT_NE(constructed->GetTypeArgument(0), nullptr);
    EXPECT_EQ(constructed->GetTypeArgument(0).get(), intVT.get());
    // The plain `T` parameter type was substituted with the inferred type.
    ASSERT_NE(c.ParameterTypes()[1], nullptr);
    EXPECT_EQ(c.ParameterTypes()[1].get(), intVT.get());
}

// The `where Tstruct : struct` constraint VIOLATED: the same shape with a string
// argument constructs `G<string>`, the constraint validation fails, and
// ConstructedTypeDoesNotSatisfyConstraint is added.
TEST(RunTypeInferenceTest, ViolatedConstraintAddsConstructedTypeError)
{
    auto structT = std::make_shared<LookupTypeParameter>("Tstruct");
    structT->SetHasValueTypeConstraint(true);
    auto gDef = MakeDef(KnownTypeCode::None, TypeKind::Class);
    gDef->SetTypeParameters({structT.get()});
    auto t = MakeTypeParam("T", 0);
    auto strRef = MakeRefDef(KnownTypeCode::String);
    auto gOfT = std::make_shared<ParameterizedType>(gDef, std::vector<ITypePtr>{t});
    TestParameter param0(gOfT, "a");
    TestParameter param1(t, "b");
    TestMethod method("M");
    method.SetTypeParameters({t.get()});
    method.SetParameters({&param0, &param1});
    OverloadResolutionCandidate c(&method, false);
    c.ParameterTypes() = {gOfT, t};
    c.ArgumentToParameterMap() = {0, 1};
    CSharpConversions conversions(Compilation());
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(MakeDef(KnownTypeCode::Int32)), Arg(strRef)};
    RunTypeInference(c, Compilation(), conversions, arguments, std::nullopt);
    EXPECT_TRUE(HasError(c, OverloadResolutionErrors::ConstructedTypeDoesNotSatisfyConstraint));
    EXPECT_EQ(c.ErrorCount(), 1);
}

} // namespace
