// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit to whom the Software is furnished to do so,
// subject to the following conditions:
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

// Tests for `TypeParameterSubstitution` (D407) -- the first concrete `TypeVisitor` (the
// D406 base), the class/method type-parameter substituter. The tests pin:
//  (a) `VisitTypeParameter` substitutes a class type parameter by `Index` from
//      `ClassTypeArguments` (owner `TypeDefinition`) and a method type parameter from
//      `MethodTypeArguments` (owner `Method`);
//  (b) an out-of-range index yields `UnknownType` (Kind == Unknown);
//  (c) a type parameter whose owner does not match the supplied list is left unchanged (the
//      base identity, via `shared_from_this` -- the stub is `make_shared`-managed);
//  (d) an empty list substitutes every index out of range -> `UnknownType`;
//  (e) `std::nullopt` (absent list) keeps that kind of type parameter unmodified;
//  (f) `Identity` is the no-op singleton (both lists absent);
//  (g) `Compose` is function composition; `Equals` (both overloads), `GetHashCode`,
//      `ToString`, and the `ClassTypeArguments` / `MethodTypeArguments` accessors;
//  (h) `AcceptVisitor` dispatches to `VisitTypeParameter` (via the stub's override that
//      mirrors the C# `AbstractTypeParameter.AcceptVisitor => visitor.VisitTypeParameter(this)`).
//
// The `TestSubstTypeParameter` stub derives from the real `ITypeParameter` (D383) and
// overrides `AcceptVisitor` to dispatch to `visitor.VisitTypeParameter(*this)` (the C#
// `AbstractTypeParameter.AcceptVisitor` bridge, ICSharpCode.Decompiler/TypeSystem/
// Implementation/AbstractTypeParameter.cs:279). It is constructed via `make_shared` so the
// base-fallback `VisitChildren -> shared_from_this` (the D406 identity) returns the managing
// shared_ptr rather than UB. `Owner()` returns nullptr (the dummy case) and `GetAttributes()`
// returns empty (no `IEntity` / `IAttribute` stand-in needed for the substitution logic).

#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::UnknownType;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;

// A minimal concrete `ITypeParameter` for the substitution tests: holds the owner kind,
// name, and index, returns trivial defaults for the rest. `AcceptVisitor` dispatches to
// `visitor.VisitTypeParameter(*this)` (the C# `AbstractTypeParameter.AcceptVisitor`
// bridge). Constructed via `make_shared` so the base-fallback `VisitChildren ->
// shared_from_this` returns the managing shared_ptr.
class TestSubstTypeParameter : public ITypeParameter {
public:
    TestSubstTypeParameter(::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType,
                           std::string name, int index)
        : ownerType_(ownerType), name_(std::move(name)), index_(index) {}

    // --- IType ---
    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    // The single `Name()` override is the final overrider for `IType::Name`, `ISymbol::Name`,
    // and `ITypeParameter::Name` (the D383 diamond disambiguation).
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    // The C# `AbstractTypeParameter.AcceptVisitor => visitor.VisitTypeParameter(this)` bridge.
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }

    // --- ITypeParameter ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override { return ownerType_; }
    const ILSpy::Decompiler::TypeSystem::IEntity* Owner() const override { return nullptr; }
    int Index() const override { return index_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    VarianceModifier Variance() const override { return VarianceModifier::Invariant; }
    ITypePtr EffectiveBaseClass() const override { return nullptr; }
    std::vector<ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    bool HasDefaultConstructorConstraint() const override { return false; }
    bool HasReferenceTypeConstraint() const override { return false; }
    bool HasValueTypeConstraint() const override { return false; }
    bool HasUnmanagedConstraint() const override { return false; }
    bool AllowsRefLikeType() const override { return false; }
    Nullability NullabilityConstraint() const override { return Nullability::Oblivious; }
    std::vector<TypeConstraint> TypeConstraints() const override { return {}; }

protected:
    bool StructuralEquals(const IType& other) const override {
        return this == &other; // identity equality for the test stub
    }

private:
    ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType_;
    std::string name_;
    int index_;
};

// Build a `std::optional<std::vector<ITypePtr>>` holding the given args (a present list).
std::optional<std::vector<ITypePtr>> List(std::vector<ITypePtr> args) {
    return std::optional<std::vector<ITypePtr>>(std::move(args));
}

} // namespace

// ---------------------------------------------------------------------------
// VisitTypeParameter -- class type parameter substitution by index.
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, VisitTypeParameterSubstitutesClassTypeParameterByIndex)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    auto int32T = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TypeParameterSubstitution subst(List({stringT, int32T}), std::nullopt);

    auto tp0 = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto tp1 = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "U", 1);

    auto r0 = subst.VisitTypeParameter(*tp0);
    auto r1 = subst.VisitTypeParameter(*tp1);
    ASSERT_NE(r0, nullptr);
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r0->Kind(), TypeKind::Class);
    EXPECT_EQ(r0->Name(), "String");
    EXPECT_EQ(r1->Kind(), TypeKind::Struct);
    EXPECT_EQ(r1->Name(), "Int32");
}

TEST(TypeParameterSubstitutionTest, VisitTypeParameterSubstitutesMethodTypeParameterByIndex)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    auto int32T = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TypeParameterSubstitution subst(std::nullopt, List({stringT, int32T}));

    auto tp0 = std::make_shared<TestSubstTypeParameter>(SymbolKind::Method, "T", 0);
    auto tp1 = std::make_shared<TestSubstTypeParameter>(SymbolKind::Method, "U", 1);

    auto r0 = subst.VisitTypeParameter(*tp0);
    auto r1 = subst.VisitTypeParameter(*tp1);
    ASSERT_NE(r0, nullptr);
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r0->Name(), "String");
    EXPECT_EQ(r1->Name(), "Int32");
}

// ---------------------------------------------------------------------------
// Out-of-range index -> UnknownType (Kind == Unknown).
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, VisitTypeParameterOutOfRangeIndexReturnsUnknownType)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    TypeParameterSubstitution subst(List({stringT}), std::nullopt);

    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 5);
    auto r = subst.VisitTypeParameter(*tp);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->Kind(), TypeKind::Unknown);
}

// ---------------------------------------------------------------------------
// A class type parameter with only a method list provided is left unchanged (base identity).
// A method type parameter with only a class list provided is left unchanged.
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, VisitTypeParameterLeavesUnmatchedOwnerUnchanged)
{
    auto int32T = std::make_shared<KnownType>(KnownTypeCode::Int32);
    // Only a method list -- a class type parameter falls to the base identity.
    TypeParameterSubstitution methodOnly(std::nullopt, List({int32T}));
    auto classTp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto classResult = methodOnly.VisitTypeParameter(*classTp);
    EXPECT_EQ(classResult.get(), classTp.get()); // identity (same shared_ptr)

    // Only a class list -- a method type parameter falls to the base identity.
    TypeParameterSubstitution classOnly(List({int32T}), std::nullopt);
    auto methodTp = std::make_shared<TestSubstTypeParameter>(SymbolKind::Method, "T", 0);
    auto methodResult = classOnly.VisitTypeParameter(*methodTp);
    EXPECT_EQ(methodResult.get(), methodTp.get()); // identity (same shared_ptr)
}

// ---------------------------------------------------------------------------
// An empty list substitutes every index out of range -> UnknownType (distinct from nullopt).
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, EmptyListSubstitutesEveryIndexToUnknownType)
{
    TypeParameterSubstitution emptyClass(List({}), std::nullopt);
    auto classTp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto r = emptyClass.VisitTypeParameter(*classTp);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(r->Kind(), TypeKind::Unknown);

    // A method type parameter is left unchanged (the empty list is a CLASS list).
    auto methodTp = std::make_shared<TestSubstTypeParameter>(SymbolKind::Method, "T", 0);
    auto methodResult = emptyClass.VisitTypeParameter(*methodTp);
    EXPECT_EQ(methodResult.get(), methodTp.get()); // identity
}

// ---------------------------------------------------------------------------
// Identity is the no-op singleton (both lists absent -> every type parameter unchanged).
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, IdentityLeavesEveryTypeParameterUnchanged)
{
    auto classTp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto methodTp = std::make_shared<TestSubstTypeParameter>(SymbolKind::Method, "U", 1);

    // A locally-constructed identity (both lists nullopt) leaves every parameter unchanged.
    TypeParameterSubstitution identitySubst(std::nullopt, std::nullopt);
    EXPECT_EQ(identitySubst.VisitTypeParameter(*classTp).get(), classTp.get());
    EXPECT_EQ(identitySubst.VisitTypeParameter(*methodTp).get(), methodTp.get());

    // The `Identity()` singleton is stable (same reference across calls).
    EXPECT_EQ(&TypeParameterSubstitution::Identity(), &TypeParameterSubstitution::Identity());
}

// ---------------------------------------------------------------------------
// The ClassTypeArguments / MethodTypeArguments accessors return the configured optionals.
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, AccessorsReturnConfiguredOptionals)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    TypeParameterSubstitution subst(List({stringT}), std::nullopt);
    ASSERT_TRUE(subst.ClassTypeArguments().has_value());
    ASSERT_FALSE(subst.MethodTypeArguments().has_value());
    ASSERT_EQ(subst.ClassTypeArguments()->size(), 1u);
    EXPECT_EQ((*subst.ClassTypeArguments())[0]->Name(), "String");

    TypeParameterSubstitution identitySubst(std::nullopt, std::nullopt);
    EXPECT_FALSE(identitySubst.ClassTypeArguments().has_value());
    EXPECT_FALSE(identitySubst.MethodTypeArguments().has_value());
}

// ---------------------------------------------------------------------------
// Compose is function composition: t.AcceptVisitor(Compose(g, f)) == t.AcceptVisitor(f).AcceptVisitor(g).
// With concrete type arguments, applying g (a substitution) to f's concrete args leaves them
// unchanged (a substitution only affects type parameters), so Compose(g, f) == f.
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, ComposeAppliesGToFArguments)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    auto int32T = std::make_shared<KnownType>(KnownTypeCode::Int32);
    TypeParameterSubstitution f(List({stringT}), List({int32T}));
    TypeParameterSubstitution g(List({std::make_shared<KnownType>(KnownTypeCode::Object)}),
                                List({std::make_shared<KnownType>(KnownTypeCode::String)}));

    TypeParameterSubstitution composed = TypeParameterSubstitution::Compose(&g, &f);
    ASSERT_TRUE(composed.ClassTypeArguments().has_value());
    ASSERT_EQ(composed.ClassTypeArguments()->size(), 1u);
    // f's class arg [String] is a concrete KnownType; applying g (a substitution) leaves it.
    EXPECT_EQ((*composed.ClassTypeArguments())[0]->Name(), "String");
    ASSERT_TRUE(composed.MethodTypeArguments().has_value());
    ASSERT_EQ(composed.MethodTypeArguments()->size(), 1u);
    EXPECT_EQ((*composed.MethodTypeArguments())[0]->Name(), "Int32");
}

TEST(TypeParameterSubstitutionTest, ComposeWithNullReturnsTheOther)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    TypeParameterSubstitution f(List({stringT}), std::nullopt);

    // Compose(nullptr, f) == f.
    TypeParameterSubstitution composedGF = TypeParameterSubstitution::Compose(nullptr, &f);
    ASSERT_TRUE(composedGF.ClassTypeArguments().has_value());
    EXPECT_EQ((*composedGF.ClassTypeArguments())[0]->Name(), "String");

    // Compose(g, nullptr) == g.
    TypeParameterSubstitution g(List({std::make_shared<KnownType>(KnownTypeCode::Int32)}), std::nullopt);
    TypeParameterSubstitution composedGNull = TypeParameterSubstitution::Compose(&g, nullptr);
    ASSERT_TRUE(composedGNull.ClassTypeArguments().has_value());
    EXPECT_EQ((*composedGNull.ClassTypeArguments())[0]->Name(), "Int32");

    // Compose(nullptr, nullptr) == identity (both lists absent).
    TypeParameterSubstitution composedNullNull = TypeParameterSubstitution::Compose(nullptr, nullptr);
    EXPECT_FALSE(composedNullNull.ClassTypeArguments().has_value());
    EXPECT_FALSE(composedNullNull.MethodTypeArguments().has_value());

    // Compose(g, identity) == g (identity has both lists absent -> the f-is-identity short-circuit).
    TypeParameterSubstitution identitySubst(std::nullopt, std::nullopt);
    TypeParameterSubstitution composedGIdentity = TypeParameterSubstitution::Compose(&g, &identitySubst);
    ASSERT_TRUE(composedGIdentity.ClassTypeArguments().has_value());
    EXPECT_EQ((*composedGIdentity.ClassTypeArguments())[0]->Name(), "Int32");
}

// ---------------------------------------------------------------------------
// AcceptVisitor dispatches to VisitTypeParameter (via the stub's AcceptVisitor override).
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, AcceptVisitorDispatchesToVisitTypeParameter)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    TypeParameterSubstitution subst(List({stringT}), std::nullopt);

    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    // tp->AcceptVisitor dispatches to the stub's override, which calls VisitTypeParameter.
    auto result = tp->AcceptVisitor(subst);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Kind(), TypeKind::Class);
    EXPECT_EQ(result->Name(), "String");

    // An out-of-range index via AcceptVisitor -> UnknownType.
    auto tpOutOfRange = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 9);
    auto unknownResult = tpOutOfRange->AcceptVisitor(subst);
    ASSERT_NE(unknownResult, nullptr);
    EXPECT_EQ(unknownResult->Kind(), TypeKind::Unknown);

    // The identity leaves a type parameter unchanged via AcceptVisitor (base identity).
    TypeParameterSubstitution identitySubst(std::nullopt, std::nullopt);
    auto identityResult = tp->AcceptVisitor(identitySubst);
    EXPECT_EQ(identityResult.get(), tp.get());
}

// ---------------------------------------------------------------------------
// Equals (no normalization) compares the two lists element-wise by IType::Equals.
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, EqualsWithoutNormalizationComparesLists)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    auto int32T = std::make_shared<KnownType>(KnownTypeCode::Int32);

    TypeParameterSubstitution a(List({stringT}), std::nullopt);
    TypeParameterSubstitution b(List({std::make_shared<KnownType>(KnownTypeCode::String)}), std::nullopt);
    TypeParameterSubstitution c(List({int32T}), std::nullopt);
    TypeParameterSubstitution identityA(std::nullopt, std::nullopt);
    TypeParameterSubstitution identityB(std::nullopt, std::nullopt);

    // Same list (a fresh KnownType(String) equal by value) -> equal.
    EXPECT_TRUE(a.Equals(&b));
    // Different element (Int32 vs String) -> not equal.
    EXPECT_FALSE(a.Equals(&c));
    // Both identity (both nullopt) -> equal.
    EXPECT_TRUE(identityA.Equals(&identityB));
    // One has a list, the other absent -> not equal.
    EXPECT_FALSE(a.Equals(&identityA));
    // null other -> false.
    EXPECT_FALSE(a.Equals(nullptr));

    // A substitution with a method list differs from one without.
    TypeParameterSubstitution withMethod(std::nullopt, List({int32T}));
    EXPECT_FALSE(a.Equals(&withMethod));
    TypeParameterSubstitution sameMethod(std::nullopt, List({std::make_shared<KnownType>(KnownTypeCode::Int32)}));
    EXPECT_TRUE(withMethod.Equals(&sameMethod));
}

// ---------------------------------------------------------------------------
// Equals with a normalization (Identity) behaves like Equals without one for concrete args.
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, EqualsWithNormalizationComparesNormalizedLists)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    TypeParameterSubstitution a(List({stringT}), std::nullopt);
    TypeParameterSubstitution b(List({std::make_shared<KnownType>(KnownTypeCode::String)}), std::nullopt);

    // The identity normalization leaves a concrete KnownType unchanged, so the normalized
    // comparison matches the unnormalized one.
    TypeParameterSubstitution identityNormalizer(std::nullopt, std::nullopt);
    EXPECT_TRUE(a.Equals(&b, identityNormalizer));
    EXPECT_FALSE(a.Equals(nullptr, identityNormalizer));
}

// ---------------------------------------------------------------------------
// GetHashCode is consistent (same lists -> same hash) and differs for distinct lists.
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, GetHashCodeIsConsistent)
{
    // The C# `element.GetHashCode()` is `AbstractType.GetHashCode => base.GetHashCode()` -- the
    // identity hash (RuntimeHelpers.GetHashCode). The port mirrors that with pointer-identity
    // hashing (`std::hash<IType*>`), so two substitutions holding the SAME shared instance hash
    // equal; two holding value-equal-but-distinct instances hash differently (faithful to the
    // C# identity-hash default, the production concrete types' value-based overrides are not
    // ported yet). The test therefore uses the same `stringT` instance for both `a` and `b`.
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    TypeParameterSubstitution a(List({stringT}), std::nullopt);
    TypeParameterSubstitution b(List({stringT}), std::nullopt);
    TypeParameterSubstitution identitySubst(std::nullopt, std::nullopt);

    // Same lists (the same shared instance) -> equal hash (identity hash matches).
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
    // The hash is stable across re-evaluation.
    EXPECT_EQ(a.GetHashCode(), a.GetHashCode());
    // The identity has a stable hash (both lists absent -> 1124131*0 + 1821779*0 = 0).
    EXPECT_EQ(identitySubst.GetHashCode(), 0);
    // A non-empty list has a non-zero hash (27 * identity-hash + ... per element, starting at 1).
    EXPECT_NE(a.GetHashCode(), identitySubst.GetHashCode());
}

// ---------------------------------------------------------------------------
// ToString renders the ECMA-335 backtick form.
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, ToStringRendersBacktickForm)
{
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    auto int32T = std::make_shared<KnownType>(KnownTypeCode::Int32);

    TypeParameterSubstitution classOnly(List({stringT, int32T}), std::nullopt);
    EXPECT_EQ(classOnly.ToString(), "[`0 -> System.String, `1 -> System.Int32]");

    TypeParameterSubstitution methodOnly(std::nullopt, List({stringT}));
    EXPECT_EQ(methodOnly.ToString(), "[``0 -> System.String]");

    TypeParameterSubstitution both(List({stringT}), List({int32T}));
    EXPECT_EQ(both.ToString(), "[`0 -> System.String, ``0 -> System.Int32]");

    TypeParameterSubstitution emptyClass(List({}), std::nullopt);
    // A present-but-empty class list renders the outer brackets plus the `[]` empty-list marker.
    EXPECT_EQ(emptyClass.ToString(), "[[]]");

    TypeParameterSubstitution identitySubst(std::nullopt, std::nullopt);
    // Both lists absent -> just the outer brackets.
    EXPECT_EQ(identitySubst.ToString(), "[]");

    // An empty method list after a non-empty class list renders the class entries then [].
    TypeParameterSubstitution classThenEmptyMethod(List({stringT}), List({}));
    EXPECT_EQ(classThenEmptyMethod.ToString(), "[`0 -> System.String, []]");
}

// ---------------------------------------------------------------------------
// The class shape: TypeParameterSubstitution IS-A TypeVisitor (derives from it).
// ---------------------------------------------------------------------------
TEST(TypeParameterSubstitutionTest, IsATypeVisitor)
{
    static_assert(std::is_base_of_v<TypeVisitor, TypeParameterSubstitution>);
    static_assert(std::has_virtual_destructor_v<TypeVisitor>);
    // A TypeParameterSubstitution dispatches as a TypeVisitor (AcceptVisitor takes a TypeVisitor&).
    auto stringT = std::make_shared<KnownType>(KnownTypeCode::String);
    TypeParameterSubstitution subst(List({stringT}), std::nullopt);
    TypeVisitor& asVisitor = subst;
    auto tp = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto result = tp->AcceptVisitor(asVisitor);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Name(), "String");
}
