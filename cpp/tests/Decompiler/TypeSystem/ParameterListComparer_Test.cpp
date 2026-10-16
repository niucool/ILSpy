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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `ParameterListComparer` + `SignatureComparer` (the port of
// ICSharpCode.Decompiler/TypeSystem/ParameterListComparer.cs, which holds both
// comparers) -- the equality comparers behind `SignatureComparer.Ordinal`, the
// comparer the `MemberLookup` Lookup region (and `InheritanceHelper`) use to decide
// whether two members have the same signature. The load-bearing cruxes are: the
// normalizing visit of every parameter type (so `Method<T>(T)` equals `Method<S>(S)`
// but a CLASS type parameter `Goo(T)` never equals a METHOD type parameter),
// ref-int-vs-out-int equality unless `includeModifiers` is set, the
// object-vs-dynamic aliasing the normalization visitor folds, and the hash-codes
// consistent with equality.

#include "Decompiler/TypeSystem/ParameterListComparer.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupEvent;
using TS::TestSupport::LookupMethod;
using TS::TestSupport::LookupTypeDefinition;

namespace {

// A concrete `IParameter` stand-in carrying a name, a type, a reference kind, and a
// params flag. The attributes/lifetime/optional surfaces are the empty defaults (the
// comparer never reads them; `includeModifiers` reads only `ReferenceKind` and
// `IsParams`).
class StubParameter : public TS::IParameter {
public:
    StubParameter(std::string name, TS::ITypePtr type,
                  TS::ReferenceKind referenceKind = TS::ReferenceKind::None,
                  bool isParams = false)
        : name_(std::move(name)), type_(std::move(type)),
          referenceKind_(referenceKind), isParams_(isParams) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }

    // --- IVariable ---
    const TS::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool throwOnInvalidMetadata = false) const override
    {
        (void)throwOnInvalidMetadata;
        return {};
    }

    // --- IParameter ---
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    TS::ReferenceKind ReferenceKind() const override { return referenceKind_; }
    TS::LifetimeAnnotation Lifetime() const override { return {}; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const TS::IParameterizedMember* Owner() const override { return nullptr; }

private:
    std::string name_;
    TS::ITypePtr type_;
    TS::ReferenceKind referenceKind_;
    bool isParams_;
};

// A method with a settable parameter list and type-parameter list (LookupMethod
// hard-wires both to empty).
class StubMethod : public LookupMethod {
public:
    StubMethod(std::string name, const TS::ICompilation& compilation)
        : LookupMethod(std::move(name), compilation) {}

    void AddParameter(const TS::IParameter* p) { parameters_.push_back(p); }
    void AddTypeParameter(const TS::ITypeParameter* tp) { typeParameters_.push_back(tp); }

    std::vector<const TS::IParameter*> Parameters() const override { return parameters_; }
    std::vector<const TS::ITypeParameter*> TypeParameters() const override
    {
        return typeParameters_;
    }

private:
    std::vector<const TS::IParameter*> parameters_;
    std::vector<const TS::ITypeParameter*> typeParameters_;
};

// A configurable type parameter with the faithful AcceptVisitor dispatch (the port's
// IType default would bypass the normalization visitor's VisitTypeParameter); two
// instances compare by identity (the C# reference identity of real type parameters).
class StubTypeParameter : public TS::ITypeParameter {
public:
    StubTypeParameter(std::string name, TS::SymbolKind ownerType, int index)
        : name_(std::move(name)), ownerType_(ownerType), index_(index) {}

    TS::TypeKind Kind() const override { return TS::TypeKind::TypeParameter; }
    TS::ITypePtr AcceptVisitor(TS::TypeVisitor& visitor) override
    {
        return visitor.VisitTypeParameter(*this);
    }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::TypeParameter; }
    TS::SymbolKind OwnerType() const override { return ownerType_; }
    const TS::IEntity* Owner() const override { return nullptr; }
    int Index() const override { return index_; }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    TS::VarianceModifier Variance() const override { return TS::VarianceModifier::Invariant; }
    TS::ITypePtr EffectiveBaseClass() const override { return {}; }
    std::vector<TS::ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    bool HasDefaultConstructorConstraint() const override { return false; }
    bool HasReferenceTypeConstraint() const override { return false; }
    bool HasValueTypeConstraint() const override { return false; }
    bool HasUnmanagedConstraint() const override { return false; }
    bool AllowsRefLikeType() const override { return false; }
    TS::Nullability NullabilityConstraint() const override { return TS::Nullability::Oblivious; }
    std::vector<TS::TypeConstraint> TypeConstraints() const override { return {}; }

protected:
    bool StructuralEquals(const TS::IType& other) const override
    {
        return this == &other;
    }

private:
    std::string name_;
    TS::SymbolKind ownerType_;
    int index_;
};

LookupCompilation& Compilation()
{
    static LookupCompilation compilation;
    return compilation;
}

// A type definition with the faithful AcceptVisitor dispatch (see the
// NormalizeTypeVisitor test stub) so the normalizer's object->dynamic arm fires.
class VisitableDefinition : public LookupTypeDefinition {
public:
    VisitableDefinition(std::string name, TS::KnownTypeCode code,
                        const TS::ICompilation& compilation)
        : LookupTypeDefinition(std::move(name), "System",
                               TS::FullTypeName(TS::TopLevelTypeName("System", "Object")),
                               TS::TypeKind::Class,
                               TS::Accessibility::Public, compilation,
                               &compilation.MainModule(), code) {}

    TS::ITypePtr AcceptVisitor(TS::TypeVisitor& visitor) override
    {
        return visitor.VisitTypeDefinition(*this);
    }
};

TS::ITypePtr Known(TS::KnownTypeCode code)
{
    return std::make_shared<TS::KnownType>(code);
}

} // namespace

// ---- ParameterListComparer ----

TEST(ParameterListComparerTest, EmptyListsAreEqual)
{
    std::vector<const TS::IParameter*> a, b;
    EXPECT_TRUE(TS::ParameterListComparer::Instance().Equals(a, b));
}

TEST(ParameterListComparerTest, DifferentLengthsAreNotEqual)
{
    StubParameter p("a", Known(TS::KnownTypeCode::Int32));
    std::vector<const TS::IParameter*> a{ &p };
    std::vector<const TS::IParameter*> b;
    EXPECT_FALSE(TS::ParameterListComparer::Instance().Equals(a, b));
}

TEST(ParameterListComparerTest, EqualParameterTypesCompareEqual)
{
    StubParameter x("x", Known(TS::KnownTypeCode::Int32));
    StubParameter y("y", Known(TS::KnownTypeCode::Int32));
    std::vector<const TS::IParameter*> a{ &x };
    std::vector<const TS::IParameter*> b{ &y };
    // Parameter NAMES never matter; the comparer looks at types only.
    EXPECT_TRUE(TS::ParameterListComparer::Instance().Equals(a, b));
}

TEST(ParameterListComparerTest, DifferentParameterTypesAreNotEqual)
{
    StubParameter x("x", Known(TS::KnownTypeCode::Int32));
    StubParameter y("y", Known(TS::KnownTypeCode::String));
    std::vector<const TS::IParameter*> a{ &x };
    std::vector<const TS::IParameter*> b{ &y };
    EXPECT_FALSE(TS::ParameterListComparer::Instance().Equals(a, b));
}

TEST(ParameterListComparerTest, NullEntriesMustMatchPairwise)
{
    StubParameter p("p", Known(TS::KnownTypeCode::Int32));
    std::vector<const TS::IParameter*> a{ nullptr, &p };
    std::vector<const TS::IParameter*> b{ nullptr, &p };
    std::vector<const TS::IParameter*> c{ &p, nullptr };
    EXPECT_TRUE(TS::ParameterListComparer::Instance().Equals(a, b))
        << "both null at the same position -- the C# `continue` arm";
    EXPECT_FALSE(TS::ParameterListComparer::Instance().Equals(a, c));
    EXPECT_FALSE(TS::ParameterListComparer::Instance().Equals(c, a));
}

TEST(ParameterListComparerTest, MethodTypeParametersAreNormalized)
{
    // The documented crux: `Method<T>(T a)` and `Method<S>(S b)` have EQUAL parameter
    // lists -- method type parameters normalize to `DummyTypeParameter`s by index.
    auto t = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::Method, 0);
    auto s = std::make_shared<StubTypeParameter>("S", TS::SymbolKind::Method, 0);
    StubParameter x("x", t), y("y", s);
    std::vector<const TS::IParameter*> a{ &x };
    std::vector<const TS::IParameter*> b{ &y };
    EXPECT_TRUE(TS::ParameterListComparer::Instance().Equals(a, b));

    // A different method type parameter INDEX is a different signature.
    auto u = std::make_shared<StubTypeParameter>("U", TS::SymbolKind::Method, 1);
    StubParameter z("z", u);
    std::vector<const TS::IParameter*> c{ &z };
    EXPECT_FALSE(TS::ParameterListComparer::Instance().Equals(a, c));
}

TEST(ParameterListComparerTest, ClassTypeParametersAreNotNormalized)
{
    // The documented counterpoint: when the type parameters belong to CLASSES,
    // `Method(T)` and `Method(S)` are NOT equal (each class's T is a distinct identity).
    auto t1 = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::TypeDefinition, 0);
    auto t2 = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::TypeDefinition, 0);
    StubParameter x("x", t1), y("y", t2);
    std::vector<const TS::IParameter*> a{ &x };
    std::vector<const TS::IParameter*> b{ &y };
    EXPECT_FALSE(TS::ParameterListComparer::Instance().Equals(a, b));

    // The SAME class type parameter instance is of course equal to itself.
    StubParameter z("z", t1);
    std::vector<const TS::IParameter*> c{ &z };
    EXPECT_TRUE(TS::ParameterListComparer::Instance().Equals(a, c));
}

TEST(ParameterListComparerTest, ObjectAndDynamicAreEqual)
{
    // The documented crux: 'object' and 'dynamic' are considered equal (the
    // normalization maps an ITypeDefinition with KnownTypeCode Object to dynamic).
    auto object = std::make_shared<VisitableDefinition>(
        "Object", TS::KnownTypeCode::Object, Compilation());
    auto dynamic = std::make_shared<TS::SpecialType>(TS::TypeKind::Dynamic,
                                                     /*isReferenceType=*/true);
    StubParameter po("x", object);
    StubParameter pd("y", dynamic);
    std::vector<const TS::IParameter*> a{ &po };
    std::vector<const TS::IParameter*> b{ &pd };
    EXPECT_TRUE(TS::ParameterListComparer::Instance().Equals(a, b));
}

TEST(ParameterListComparerTest, RefAndOutAreEqualUnlessModifiersIncluded)
{
    // The documented crux: 'ref int' and 'out int' are considered equal unless
    // includeModifiers is set.
    StubParameter byRef("x", Known(TS::KnownTypeCode::Int32), TS::ReferenceKind::Ref);
    StubParameter byOut("y", Known(TS::KnownTypeCode::Int32), TS::ReferenceKind::Out);
    std::vector<const TS::IParameter*> a{ &byRef };
    std::vector<const TS::IParameter*> b{ &byOut };
    EXPECT_TRUE(TS::ParameterListComparer::Instance().Equals(a, b));
    auto withModifiers = TS::ParameterListComparer::WithOptions(/*includeModifiers=*/true);
    EXPECT_FALSE(withModifiers.Equals(a, b));
}

TEST(ParameterListComparerTest, ParamsFlagIgnoredUnlessModifiersIncluded)
{
    StubParameter plain("x", Known(TS::KnownTypeCode::Int32), TS::ReferenceKind::None, false);
    StubParameter variadic("y", Known(TS::KnownTypeCode::Int32), TS::ReferenceKind::None, true);
    std::vector<const TS::IParameter*> a{ &plain };
    std::vector<const TS::IParameter*> b{ &variadic };
    EXPECT_TRUE(TS::ParameterListComparer::Instance().Equals(a, b));
    auto withModifiers = TS::ParameterListComparer::WithOptions(/*includeModifiers=*/true);
    EXPECT_FALSE(withModifiers.Equals(a, b));
}

TEST(ParameterListComparerTest, GetHashCodeIsConsistentWithEquality)
{
    // The hash mixes the NORMALIZED TYPE'S IDENTITY hash (the C# identity
    // `object.GetHashCode` arm) -- a distinct-but-structurally-equal instance hashes
    // differently, exactly as in C#; the type system interning is what makes equal
    // types share instances. Both parameters therefore share one Int32 instance here.
    auto int32 = Known(TS::KnownTypeCode::Int32);
    StubParameter x("x", int32);
    StubParameter y("y", int32);
    StubParameter s("s", Known(TS::KnownTypeCode::String));
    std::vector<const TS::IParameter*> a{ &x };
    std::vector<const TS::IParameter*> b{ &y };
    std::vector<const TS::IParameter*> ab{ &x, &s };
    EXPECT_EQ(TS::ParameterListComparer::Instance().GetHashCode(a),
              TS::ParameterListComparer::Instance().GetHashCode(b))
        << "equal lists hash equal";
    EXPECT_NE(TS::ParameterListComparer::Instance().GetHashCode(a),
              TS::ParameterListComparer::Instance().GetHashCode(ab))
        << "the element count seeds the hash";
}

TEST(ParameterListComparerTest, InstanceIsASingleton)
{
    // The C# `static readonly ParameterListComparer Instance` is one instance.
    EXPECT_EQ(&TS::ParameterListComparer::Instance(), &TS::ParameterListComparer::Instance());
}

// ---- SignatureComparer ----

TEST(SignatureComparerTest, IdentityIsEqual)
{
    StubMethod m("M", Compilation());
    EXPECT_TRUE(TS::SignatureComparer::Ordinal().Equals(&m, &m)) << "the x == y arm";
}

TEST(SignatureComparerTest, NullIsNotEqualToNonNull)
{
    StubMethod m("M", Compilation());
    EXPECT_FALSE(TS::SignatureComparer::Ordinal().Equals(nullptr, &m));
    EXPECT_FALSE(TS::SignatureComparer::Ordinal().Equals(&m, nullptr));
}

TEST(SignatureComparerTest, SymbolKindMustMatch)
{
    StubMethod m("Foo", Compilation());
    LookupEvent e("Foo", Known(TS::KnownTypeCode::Int32), Compilation());
    EXPECT_FALSE(TS::SignatureComparer::Ordinal().Equals(&m, &e));
    EXPECT_FALSE(TS::SignatureComparer::Ordinal().Equals(&e, &m));
}

TEST(SignatureComparerTest, NameComparisonUsesTheConfiguredComparer)
{
    StubMethod a("Perform", Compilation());
    StubMethod b("perform", Compilation());
    // Ordinal: case-sensitive.
    EXPECT_FALSE(TS::SignatureComparer::Ordinal().Equals(&a, &b));
    // A comparer built on OrdinalIgnoreCase folds case.
    TS::SignatureComparer ignoreCase(TS::StringComparer::OrdinalIgnoreCase());
    EXPECT_TRUE(ignoreCase.Equals(&a, &b));
    // Exactly-equal names compare equal under Ordinal.
    StubMethod c("Perform", Compilation());
    EXPECT_TRUE(TS::SignatureComparer::Ordinal().Equals(&a, &c));
}

TEST(SignatureComparerTest, ParameterTypesAreCompared)
{
    StubMethod a("M", Compilation());
    StubMethod b("M", Compilation());
    StubParameter pa("x", Known(TS::KnownTypeCode::Int32));
    StubParameter pb("y", Known(TS::KnownTypeCode::Int32));
    StubParameter pc("z", Known(TS::KnownTypeCode::String));
    a.AddParameter(&pa);
    b.AddParameter(&pb);
    EXPECT_TRUE(TS::SignatureComparer::Ordinal().Equals(&a, &b));
    StubMethod c("M", Compilation());
    c.AddParameter(&pc);
    EXPECT_FALSE(TS::SignatureComparer::Ordinal().Equals(&a, &c));
}

TEST(SignatureComparerTest, MethodTypeParameterCountMustMatch)
{
    StubMethod generic1("M", Compilation());
    StubMethod generic2("M", Compilation());
    auto t = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::Method, 0);
    auto u = std::make_shared<StubTypeParameter>("U", TS::SymbolKind::Method, 1);
    generic1.AddTypeParameter(t.get());
    generic2.AddTypeParameter(t.get());
    generic2.AddTypeParameter(u.get());
    EXPECT_FALSE(TS::SignatureComparer::Ordinal().Equals(&generic1, &generic2));
    EXPECT_TRUE(TS::SignatureComparer::Ordinal().Equals(&generic1, &generic1));
}

TEST(SignatureComparerTest, MethodTypeParameterNamesDoNotMatter)
{
    // M<T>(T) equals M<S>(S): name + non-parameter count + the normalized parameter
    // list match, regardless of the parameter/type-parameter names.
    auto t = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::Method, 0);
    auto s = std::make_shared<StubTypeParameter>("S", TS::SymbolKind::Method, 0);
    StubParameter pa("x", t), pb("y", s);
    StubMethod a("M", Compilation());
    a.AddTypeParameter(t.get());
    a.AddParameter(&pa);
    StubMethod b("M", Compilation());
    b.AddTypeParameter(s.get());
    b.AddParameter(&pb);
    EXPECT_TRUE(TS::SignatureComparer::Ordinal().Equals(&a, &b));
}

TEST(SignatureComparerTest, NonParameterizedMembersCompareByKindAndName)
{
    // Events are not IParameterizedMember: only the symbol kind and name are compared
    // (the handler type never enters the comparison).
    LookupEvent a("Changed", Known(TS::KnownTypeCode::Int32), Compilation());
    LookupEvent b("Changed", Known(TS::KnownTypeCode::String), Compilation());
    LookupEvent c("Raised", Known(TS::KnownTypeCode::Int32), Compilation());
    EXPECT_TRUE(TS::SignatureComparer::Ordinal().Equals(&a, &b));
    EXPECT_FALSE(TS::SignatureComparer::Ordinal().Equals(&a, &c));
}

TEST(SignatureComparerTest, GetHashCodeIsConsistentWithEquality)
{
    // Same shared-instance convention as the parameter-list hash test above (the
    // parameter-list hash mixes the normalized type's identity hash).
    auto int32 = Known(TS::KnownTypeCode::Int32);
    StubParameter pa("x", int32);
    StubParameter pb("y", int32);
    StubMethod a("M", Compilation());
    a.AddParameter(&pa);
    StubMethod b("M", Compilation());
    b.AddParameter(&pb);
    EXPECT_TRUE(TS::SignatureComparer::Ordinal().Equals(&a, &b));
    EXPECT_EQ(TS::SignatureComparer::Ordinal().GetHashCode(&a),
              TS::SignatureComparer::Ordinal().GetHashCode(&b));
    StubMethod c("N", Compilation());
    c.AddParameter(&pa);
    EXPECT_NE(TS::SignatureComparer::Ordinal().GetHashCode(&a),
              TS::SignatureComparer::Ordinal().GetHashCode(&c))
        << "the name hash participates in the member hash";
}

TEST(SignatureComparerTest, OrdinalIsASingleton)
{
    // The C# `static readonly SignatureComparer Ordinal` is one instance.
    EXPECT_EQ(&TS::SignatureComparer::Ordinal(), &TS::SignatureComparer::Ordinal());
}

// The class shapes pin the C# `public sealed class ParameterListComparer /
// SignatureComparer : IEqualityComparer<...>`.
static_assert(std::is_final<TS::ParameterListComparer>::value,
              "ParameterListComparer is sealed/final");
static_assert(std::is_final<TS::SignatureComparer>::value,
              "SignatureComparer is sealed/final");
