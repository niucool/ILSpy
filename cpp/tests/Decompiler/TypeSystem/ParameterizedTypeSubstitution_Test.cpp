// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `ParameterizedType` substitution surface (D479) -- the
// `GetSubstitution()` / `GetSubstitution(methodTypeArguments)` /
// `GetTypeArgument(int index)` members of `ParameterizedType` (IType.hpp). The C#
// `ParameterizedType.GetSubstitution()` returns `new TypeParameterSubstitution(
// typeArguments, null)` and the two-arg overload returns `new
// TypeParameterSubstitution(typeArguments, methodTypeArguments)`; the port returns
// them BY VALUE (the C# heap allocation realized as a value, the D407 convention).
// `GetTypeArgument(index)` returns the `index`-th type argument (the C# is literally
// `typeArguments[index]`).
//
// This is the prerequisite surface the `GetMembersHelper` routing binds against (the
// next in-order `MemberLookup.LookupGroup` blocker): `GetMethodsImpl` calls
// `pt.GetSubstitution(methodTypeArguments)`, the constructors/accessors call
// `pt.GetSubstitution()`, and `GetNestedTypesImpl` reads `pt.GetTypeArgument(i)`.
// The tests pin:
//  (a) `GetTypeArgument` returns the Nth type argument and shares the underlying
//      `IType` instance with `TypeArguments()[index]`;
//  (b) `GetSubstitution()` carries the parameterized type's class type arguments and an
//      ABSENT method list (`std::nullopt` -- the C# `null`);
//  (c) `GetSubstitution(methodTypeArguments)` carries BOTH the class and method lists;
//  (d) the two overloads are distinct (absent vs present method list);
//  (e) the returned substitution, applied to a class type parameter by index, yields
//      the parameterized type's type argument at that index (the substitution
//      semantics -- the real correctness check, exercising the C# semantics through the
//      already-ported `TypeParameterSubstitution::VisitTypeParameter`);
//  (f) the two-arg overload, applied to a method type parameter by index, yields the
//      method type argument (and a class type parameter still yields the class arg);
//  (g) the returned substitution equals a `TypeParameterSubstitution` constructed
//      directly with the same lists (the faithful-construction equivalence).

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
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
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A minimal concrete `ITypeParameter` for the substitution-semantics tests: holds the
// owner kind and index, returns trivial defaults for the rest. `AcceptVisitor`
// dispatches to `visitor.VisitTypeParameter(*this)` (the C#
// `AbstractTypeParameter.AcceptVisitor` bridge). Constructed via `make_shared` so the
// base-fallback `VisitChildren -> shared_from_this` returns the managing shared_ptr.
// Mirrors `TestSubstTypeParameter` in `TypeParameterSubstitution_Test.cpp`.
class TestSubstTypeParameter : public ITypeParameter {
public:
    TestSubstTypeParameter(::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType,
                           std::string name, int index)
        : ownerType_(ownerType), name_(std::move(name)), index_(index) {}

    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }

    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }

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
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
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
// GetTypeArgument returns the Nth type argument (the C# `typeArguments[index]`).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetTypeArgumentReturnsNthTypeArgument)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    EXPECT_EQ(pt->GetTypeArgument(0)->Name(), "String");
    EXPECT_EQ(pt->GetTypeArgument(1)->Name(), "Int32");
}

// ---------------------------------------------------------------------------
// GetTypeArgument shares the underlying IType instance with TypeArguments()[index]
// (the C# returns the stored element reference; the port returns the shared_ptr, so
// the managed IType is the same object).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetTypeArgumentSharesInstanceWithTypeArguments)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    EXPECT_EQ(pt->GetTypeArgument(0).get(), pt->TypeArguments()[0].get());
    EXPECT_EQ(pt->GetTypeArgument(1).get(), pt->TypeArguments()[1].get());
}

// ---------------------------------------------------------------------------
// GetSubstitution() carries the parameterized type's class type arguments and an
// ABSENT method list (std::nullopt -- the C# null).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetSubstitutionNoArgsCarriesClassArgsAndAbsentMethodArgs)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    TypeParameterSubstitution subst = pt->GetSubstitution();
    ASSERT_TRUE(subst.ClassTypeArguments().has_value());
    ASSERT_EQ(subst.ClassTypeArguments()->size(), 2u);
    EXPECT_EQ((*subst.ClassTypeArguments())[0]->Name(), "String");
    EXPECT_EQ((*subst.ClassTypeArguments())[1]->Name(), "Int32");
    EXPECT_FALSE(subst.MethodTypeArguments().has_value());
}

// ---------------------------------------------------------------------------
// GetSubstitution()'s class-arg elements share the underlying IType instances with
// the parameterized type's type arguments (the C# shares the same list reference; the
// port shares the managed IType objects via shared_ptr).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetSubstitutionNoArgsClassArgsShareInstances)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    TypeParameterSubstitution subst = pt->GetSubstitution();
    ASSERT_TRUE(subst.ClassTypeArguments().has_value());
    EXPECT_EQ((*subst.ClassTypeArguments())[0].get(), pt->TypeArguments()[0].get());
    EXPECT_EQ((*subst.ClassTypeArguments())[1].get(), pt->TypeArguments()[1].get());
}

// ---------------------------------------------------------------------------
// GetSubstitution(methodTypeArguments) carries BOTH the class and method lists.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetSubstitutionWithMethodArgsCarriesBoth)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    TypeParameterSubstitution subst = pt->GetSubstitution(List({Object(), String()}));
    ASSERT_TRUE(subst.ClassTypeArguments().has_value());
    ASSERT_EQ(subst.ClassTypeArguments()->size(), 2u);
    EXPECT_EQ((*subst.ClassTypeArguments())[0]->Name(), "String");
    ASSERT_TRUE(subst.MethodTypeArguments().has_value());
    ASSERT_EQ(subst.MethodTypeArguments()->size(), 2u);
    EXPECT_EQ((*subst.MethodTypeArguments())[0]->Name(), "Object");
    EXPECT_EQ((*subst.MethodTypeArguments())[1]->Name(), "String");
}

// ---------------------------------------------------------------------------
// The two overloads are distinct: the no-arg has an absent method list; the
// method-arg overload has a present method list.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetSubstitutionOverloadsDifferInMethodArgs)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    TypeParameterSubstitution noArgs = pt->GetSubstitution();
    TypeParameterSubstitution withMethod = pt->GetSubstitution(List({Int32()}));
    EXPECT_FALSE(noArgs.MethodTypeArguments().has_value());
    EXPECT_TRUE(withMethod.MethodTypeArguments().has_value());
    // They are not equal (the method list differs).
    EXPECT_FALSE(noArgs.Equals(&withMethod));
    EXPECT_FALSE(withMethod.Equals(&noArgs));
}

// ---------------------------------------------------------------------------
// GetSubstitution() applied to a class type parameter by index yields the
// parameterized type's type argument at that index (the substitution semantics).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetSubstitutionSubstitutesClassTypeParameter)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    TypeParameterSubstitution subst = pt->GetSubstitution();

    auto tp0 = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto tp1 = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "U", 1);
    auto r0 = tp0->AcceptVisitor(subst);
    auto r1 = tp1->AcceptVisitor(subst);
    ASSERT_NE(r0, nullptr);
    ASSERT_NE(r1, nullptr);
    EXPECT_EQ(r0->Name(), "String");
    EXPECT_EQ(r1->Name(), "Int32");
}

// ---------------------------------------------------------------------------
// GetSubstitution(methodTypeArguments) applied to a method type parameter by index
// yields the method type argument; a class type parameter still yields the class arg.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetSubstitutionWithMethodArgsSubstitutesMethodTypeParameter)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    TypeParameterSubstitution subst = pt->GetSubstitution(List({Object()}));

    // A method type parameter at index 0 -> the 0th method type argument (Object).
    auto mt = std::make_shared<TestSubstTypeParameter>(SymbolKind::Method, "M", 0);
    auto rm = mt->AcceptVisitor(subst);
    ASSERT_NE(rm, nullptr);
    EXPECT_EQ(rm->Name(), "Object");

    // A class type parameter at index 0 -> the 0th class type argument (String).
    auto ct = std::make_shared<TestSubstTypeParameter>(SymbolKind::TypeDefinition, "T", 0);
    auto rc = ct->AcceptVisitor(subst);
    ASSERT_NE(rc, nullptr);
    EXPECT_EQ(rc->Name(), "String");
}

// ---------------------------------------------------------------------------
// The returned substitution equals a TypeParameterSubstitution constructed directly
// with the same lists (the faithful-construction equivalence -- GetSubstitution() is
// `TypeParameterSubstitution(typeArguments, null)`).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetSubstitutionNoArgsEqualsDirectlyConstructed)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    TypeParameterSubstitution fromPt = pt->GetSubstitution();
    // The direct construction mirrors the C# `new TypeParameterSubstitution(typeArguments, null)`.
    TypeParameterSubstitution direct(
        List(std::vector<ITypePtr>{String(), Int32()}), std::nullopt);
    EXPECT_TRUE(fromPt.Equals(&direct));
    EXPECT_TRUE(direct.Equals(&fromPt));
}

// ---------------------------------------------------------------------------
// The method-arg overload equals a directly-constructed substitution with both lists.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeSubstitutionTest, GetSubstitutionWithMethodArgsEqualsDirectlyConstructed)
{
    auto pt = std::make_shared<ParameterizedType>(Object(),
                                                  std::vector<ITypePtr>{String(), Int32()});
    TypeParameterSubstitution fromPt = pt->GetSubstitution(List({Object()}));
    TypeParameterSubstitution direct(
        List(std::vector<ITypePtr>{String(), Int32()}), List({Object()}));
    EXPECT_TRUE(fromPt.Equals(&direct));
    EXPECT_TRUE(direct.Equals(&fromPt));
}
