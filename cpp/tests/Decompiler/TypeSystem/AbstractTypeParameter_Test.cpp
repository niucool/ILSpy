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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `AbstractTypeParameter` (D487) -- the abstract base for `ITypeParameter`
// implementations (`Implementation/AbstractTypeParameter.{hpp,cpp}`). It holds the owner /
// index / name / variance and supplies the shared `IType` / `ISymbol` / `ITypeParameter` /
// `ICompilationProvider` surface. The test exercises it through a test-only concrete subclass
// `TestTypeParameter` (the base is abstract -- the C#-`abstract` members stay pure-virtual).
//
// The tests pin:
//  (a) the owner-based ctor: `Owner()` / `OwnerType()` / `Compilation()` derive from the owner;
//      `Index()` / `Name()` / `Variance()` are the passed values;
//  (b) the compilation-based ctor: `Owner()` is null, `OwnerType()` / `Compilation()` are the
//      passed values;
//  (c) the owner-based ctor throws on a null owner (the C# `ArgumentNullException`);
//  (d) the default-name computation (empty name -> the `!`/`!!` form);
//  (e) `Kind()` is `TypeParameter`, `SymbolKind()` is `TypeParameter`,
//      `TypeParameterCount()` is 0;
//  (f) `ReflectionName()` is the backtick form (``0 for method, `0 for class);
//  (g) `AcceptVisitor` dispatches to `VisitTypeParameter`;
//  (h) `ChangeNullability`: `Oblivious` -> `shared_from_this()` (identity); else -> a
//      `NullabilityAnnotatedTypeParameter` (Kind == the wrapped type parameter's Kind);
//  (i) `DirectBaseTypes` reads `TypeConstraints()` -> `TypeConstraint::Type()`;
//  (j) `EffectiveBaseClass` -> `UnknownType()` (deferred); `EffectiveInterfaceSet` -> empty;
//  (k) `StructuralEquals` -> reference equality; `GetHashCode` / `ToString` plain members;
//  (l) the class is abstract (a `static_assert` that it is not final / not concrete-constructible
//      is ill-formed for an abstract class, so the test-only subclass is the only way).

#include "Decompiler/TypeSystem/Implementation/AbstractTypeParameter.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"

#include "LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Implementation::AbstractTypeParameter;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IEntity;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedTypeParameter;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// The test-only CONCRETE `AbstractTypeParameter` subclass. Overrides the C#-`abstract`
// members (`GetAttributes` / `Has*Constraint` / `AllowsRefLikeType` / `NullabilityConstraint` /
// `TypeConstraints`) with configurable held values; the rest of the surface is inherited from
// the base. Constructed via `make_shared` so `shared_from_this()` / `ChangeNullability` work.
class TestTypeParameter : public AbstractTypeParameter {
public:
    TestTypeParameter(const IEntity* owner, int index, std::string name, VarianceModifier variance,
                     std::vector<TypeConstraint> typeConstraints = {},
                     bool hasValueTypeConstraint = false, bool hasReferenceTypeConstraint = false,
                     bool hasDefaultConstructorConstraint = false, bool hasUnmanagedConstraint = false,
                     bool allowsRefLikeType = false,
                     ::ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint = ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious,
                     std::vector<const IAttribute*> attributes = {})
        : AbstractTypeParameter(owner, index, std::move(name), variance),
          typeConstraints_(std::move(typeConstraints)),
          hasValueTypeConstraint_(hasValueTypeConstraint),
          hasReferenceTypeConstraint_(hasReferenceTypeConstraint),
          hasDefaultConstructorConstraint_(hasDefaultConstructorConstraint),
          hasUnmanagedConstraint_(hasUnmanagedConstraint),
          allowsRefLikeType_(allowsRefLikeType),
          nullabilityConstraint_(nullabilityConstraint),
          attributes_(std::move(attributes)) {}

    TestTypeParameter(const ICompilation& compilation, ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType, int index,
                     std::string name, VarianceModifier variance)
        : AbstractTypeParameter(compilation, ownerType, index, std::move(name), variance) {}

    // --- The C#-abstract overrides ---
    std::vector<const IAttribute*> GetAttributes() const override { return attributes_; }
    bool HasDefaultConstructorConstraint() const override { return hasDefaultConstructorConstraint_; }
    bool HasReferenceTypeConstraint() const override { return hasReferenceTypeConstraint_; }
    bool HasValueTypeConstraint() const override { return hasValueTypeConstraint_; }
    bool HasUnmanagedConstraint() const override { return hasUnmanagedConstraint_; }
    bool AllowsRefLikeType() const override { return allowsRefLikeType_; }
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override { return nullabilityConstraint_; }
    std::vector<TypeConstraint> TypeConstraints() const override { return typeConstraints_; }

private:
    std::vector<TypeConstraint> typeConstraints_;
    bool hasValueTypeConstraint_;
    bool hasReferenceTypeConstraint_;
    bool hasDefaultConstructorConstraint_;
    bool hasUnmanagedConstraint_;
    bool allowsRefLikeType_;
    ::ILSpy::Decompiler::TypeSystem::Nullability nullabilityConstraint_;
    std::vector<const IAttribute*> attributes_;
};

// The shared `ICompilation` for the stubs.
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `IEntity`) for the owner-based ctor (a class owner).
std::shared_ptr<LookupTypeDefinition> ClassOwner() {
    return std::make_shared<LookupTypeDefinition>(
        "Foo", "", FullTypeName("Foo"), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr);
}

// A `LookupMethod` (IS-A `IEntity`) for the owner-based ctor (a method owner).
std::shared_ptr<LookupMethod> MethodOwner() {
    return std::make_shared<LookupMethod>("M", Compilation());
}

} // namespace

// ---------------------------------------------------------------------------
// The owner-based ctor: Owner / OwnerType / Compilation derive from the owner; Index / Name /
// Variance are the passed values.
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, OwnerBasedCtorDerivesFromOwner) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(
        owner.get(), /*index*/ 2, "T", VarianceModifier::Covariant);
    EXPECT_EQ(tp->Owner(), owner.get());
    EXPECT_EQ(tp->OwnerType(), SymbolKind::TypeDefinition);
    EXPECT_EQ(&tp->Compilation(), &Compilation());
    EXPECT_EQ(tp->Index(), 2);
    EXPECT_EQ(tp->Name(), "T");
    EXPECT_EQ(tp->Variance(), VarianceModifier::Covariant);
}

// ---------------------------------------------------------------------------
// The compilation-based ctor: Owner is null, OwnerType / Compilation are the passed values.
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, CompilationBasedCtorHasNullOwner) {
    auto tp = std::make_shared<TestTypeParameter>(
        Compilation(), SymbolKind::Method, /*index*/ 1, "U", VarianceModifier::Contravariant);
    EXPECT_EQ(tp->Owner(), nullptr);
    EXPECT_EQ(tp->OwnerType(), SymbolKind::Method);
    EXPECT_EQ(&tp->Compilation(), &Compilation());
    EXPECT_EQ(tp->Index(), 1);
    EXPECT_EQ(tp->Name(), "U");
    EXPECT_EQ(tp->Variance(), VarianceModifier::Contravariant);
}

// ---------------------------------------------------------------------------
// The owner-based ctor throws on a null owner (the C# ArgumentNullException).
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, OwnerBasedCtorThrowsOnNullOwner) {
    EXPECT_THROW(TestTypeParameter(nullptr, 0, "T", VarianceModifier::Invariant),
                 std::invalid_argument);
}

// ---------------------------------------------------------------------------
// The default-name computation: an empty name -> the `!`/`!!` form by owner kind.
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, DefaultNameForClassOwner) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(
        owner.get(), 3, /*name*/ "", VarianceModifier::Invariant);
    EXPECT_EQ(tp->Name(), "!3");
}

TEST(AbstractTypeParameterTest, DefaultNameForMethodOwner) {
    auto owner = MethodOwner();
    auto tp = std::make_shared<TestTypeParameter>(
        owner.get(), 3, /*name*/ "", VarianceModifier::Invariant);
    EXPECT_EQ(tp->Name(), "!!3");
}

// ---------------------------------------------------------------------------
// Kind / SymbolKind / TypeParameterCount.
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, KindSymbolKindTypeParameterCount) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(owner.get(), 0, "T", VarianceModifier::Invariant);
    EXPECT_EQ(tp->Kind(), TypeKind::TypeParameter);
    EXPECT_EQ(tp->SymbolKind(), SymbolKind::TypeParameter);
    EXPECT_EQ(tp->TypeParameterCount(), 0);
}

// ---------------------------------------------------------------------------
// ReflectionName is the backtick form (``0 for method, `0 for class).
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, ReflectionNameBacktickForm) {
    auto classOwner = ClassOwner();
    auto classTp = std::make_shared<TestTypeParameter>(
        classOwner.get(), 0, "T", VarianceModifier::Invariant);
    EXPECT_EQ(classTp->ReflectionName(), "`0");

    auto methodOwner = MethodOwner();
    auto methodTp = std::make_shared<TestTypeParameter>(
        methodOwner.get(), 0, "T", VarianceModifier::Invariant);
    EXPECT_EQ(methodTp->ReflectionName(), "``0");
}

// ---------------------------------------------------------------------------
// AcceptVisitor dispatches to VisitTypeParameter.
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, AcceptVisitorDispatchesToVisitTypeParameter) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(owner.get(), 0, "T", VarianceModifier::Invariant);
    TypeParameterSubstitution subst(std::nullopt, std::nullopt); // a TypeVisitor
    auto result = tp->AcceptVisitor(subst);
    // VisitTypeParameter with an Identity substitution (both lists absent) leaves the
    // parameter unchanged (the base identity). The result IS the type parameter.
    EXPECT_EQ(result.get(), tp.get());
}

// ---------------------------------------------------------------------------
// ChangeNullability: Oblivious -> shared_from_this() (identity); else -> a
// NullabilityAnnotatedTypeParameter.
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, ChangeNullabilityObliviousReturnsSelf) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(owner.get(), 0, "T", VarianceModifier::Invariant);
    auto result = tp->ChangeNullability(Nullability::Oblivious);
    EXPECT_EQ(result.get(), tp.get()); // identity
}

TEST(AbstractTypeParameterTest, ChangeNullabilityNonObliviousWraps) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(owner.get(), 0, "T", VarianceModifier::Invariant);
    auto result = tp->ChangeNullability(Nullability::NotNullable);
    ASSERT_NE(result, nullptr);
    // The result is a NullabilityAnnotatedTypeParameter wrapping `tp`.
    auto nat = std::dynamic_pointer_cast<NullabilityAnnotatedTypeParameter>(result);
    ASSERT_NE(nat, nullptr);
    EXPECT_EQ(nat->Nullability(), Nullability::NotNullable);
    // The wrapped base type is the type parameter (Kind == TypeParameter).
    EXPECT_EQ(nat->Kind(), TypeKind::TypeParameter);
}

// ---------------------------------------------------------------------------
// DirectBaseTypes reads TypeConstraints() -> TypeConstraint::Type().
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, DirectBaseTypesReadsTypeConstraints) {
    auto owner = ClassOwner();
    std::vector<TypeConstraint> constraints;
    constraints.emplace_back(Int32(), std::vector<const IAttribute*>{});
    constraints.emplace_back(Object(), std::vector<const IAttribute*>{});
    auto tp = std::make_shared<TestTypeParameter>(
        owner.get(), 0, "T", VarianceModifier::Invariant, std::move(constraints));
    auto bases = tp->DirectBaseTypes();
    ASSERT_EQ(bases.size(), 2u);
    EXPECT_EQ(bases[0]->Name(), "Int32");
    EXPECT_EQ(bases[1]->Name(), "Object");
}

// ---------------------------------------------------------------------------
// EffectiveBaseClass -> UnknownType (deferred); EffectiveInterfaceSet -> empty (deferred).
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, EffectiveBaseClassAndInterfaceSetDeferred) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(owner.get(), 0, "T", VarianceModifier::Invariant);
    EXPECT_EQ(tp->EffectiveBaseClass()->Kind(), TypeKind::Unknown);
    EXPECT_TRUE(tp->EffectiveInterfaceSet().empty());
}

// ---------------------------------------------------------------------------
// StructuralEquals is reference equality (the C# Equals(IType) => this == other).
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, StructuralEqualsIsReferenceEquality) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(owner.get(), 0, "T", VarianceModifier::Invariant);
    auto tp2 = std::make_shared<TestTypeParameter>(owner.get(), 0, "T", VarianceModifier::Invariant);
    // Same object -> equal (reference identity).
    EXPECT_TRUE(tp->Equals(*tp));
    // Different objects (even with the same fields) -> not equal (reference identity).
    EXPECT_FALSE(tp->Equals(*tp2));
}

// ---------------------------------------------------------------------------
// GetHashCode / ToString are plain members (identity hash / ReflectionName).
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, GetHashCodeAndToStringPlainMembers) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(owner.get(), 5, "T", VarianceModifier::Invariant);
    EXPECT_EQ(tp->GetHashCode(), tp->GetHashCode()); // stable
    EXPECT_EQ(tp->ToString(), tp->ReflectionName());
}

// ---------------------------------------------------------------------------
// The abstract overrides delegate to the configured values.
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, AbstractOverridesDelegate) {
    auto owner = ClassOwner();
    auto tp = std::make_shared<TestTypeParameter>(
        owner.get(), 0, "T", VarianceModifier::Invariant,
        /*typeConstraints*/ std::vector<TypeConstraint>{}, /*hasValueTypeConstraint*/ true,
        /*hasReferenceTypeConstraint*/ true, /*hasDefaultConstructorConstraint*/ true,
        /*hasUnmanagedConstraint*/ true, /*allowsRefLikeType*/ true,
        /*nullabilityConstraint*/ Nullability::NotNullable);
    EXPECT_TRUE(tp->HasValueTypeConstraint());
    EXPECT_TRUE(tp->HasReferenceTypeConstraint());
    EXPECT_TRUE(tp->HasDefaultConstructorConstraint());
    EXPECT_TRUE(tp->HasUnmanagedConstraint());
    EXPECT_TRUE(tp->AllowsRefLikeType());
    EXPECT_EQ(tp->NullabilityConstraint(), Nullability::NotNullable);
}

// ---------------------------------------------------------------------------
// The class shape: AbstractTypeParameter IS-A ITypeParameter / IType / ICompilationProvider.
// ---------------------------------------------------------------------------
TEST(AbstractTypeParameterTest, ClassShape) {
    static_assert(std::is_base_of_v<ITypeParameter, AbstractTypeParameter>);
    static_assert(std::is_base_of_v<IType, AbstractTypeParameter>);
    static_assert(std::is_abstract_v<AbstractTypeParameter>);
}
