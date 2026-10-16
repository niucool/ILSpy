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

// Tests for `DummyTypeParameter` + `NullabilityAnnotatedTypeParameter` (the port of
// ICSharpCode.Decompiler/TypeSystem/Implementation/DummyTypeParameter.cs + the nested
// wrapper class from Implementation/NullabilityAnnotatedType.cs) -- the placeholder
// type parameters normalizers substitute in (e.g. comparing two generic method
// signatures as if `Method{T}(T)` and `Method{S}(S)` were the same shape). The
// load-bearing cruxes are: the PER-(owner-type, index) INSTANCE CACHE (the C#
// `Interlocked.CompareExchange`-grown static arrays -- two calls for the same index
// return the same instance, so structural comparison by identity works), the name
// forms (`!0` / "``0") distinct between class and method parameters, the all-empty
// constraint surface, the `visitor.VisitTypeParameter` dispatch, and the
// ChangeNullability wrap into `NullabilityAnnotatedTypeParameter`.

#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"

#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using TS::Implementation::DummyTypeParameter;

// Records which Visit* method the dispatch reached; the VisitTypeParameter override
// forwards to the base default (the stub parameter carries no children, so the
// result is the parameter itself via shared_from_this).
class RecordingVisitor : public TS::TypeVisitor {
public:
    std::string last;
    TS::ITypePtr VisitTypeParameter(TS::ITypeParameter& type) override
    {
        last = "VisitTypeParameter";
        return TS::TypeVisitor::VisitTypeParameter(type);
    }
};

} // namespace

TEST(DummyTypeParameterTest, MethodParameterNameForms)
{
    auto p0 = DummyTypeParameter::GetMethodTypeParameter(0);
    auto p3 = DummyTypeParameter::GetMethodTypeParameter(3);
    ASSERT_NE(p0, nullptr);
    EXPECT_EQ(p0->Name(), "!!0");          // the C# "!!" + index
    EXPECT_EQ(p0->ReflectionName(), "``0"); // the C# "``" + index
    EXPECT_EQ(p3->Name(), "!!3");
    EXPECT_EQ(p3->ReflectionName(), "``3");
}

TEST(DummyTypeParameterTest, ClassParameterNameForms)
{
    auto p0 = DummyTypeParameter::GetClassTypeParameter(0);
    auto p12 = DummyTypeParameter::GetClassTypeParameter(12);
    ASSERT_NE(p0, nullptr);
    EXPECT_EQ(p0->Name(), "!0");           // the C# "!" + index
    EXPECT_EQ(p0->ReflectionName(), "`0"); // the C# "`" + index
    EXPECT_EQ(p12->Name(), "!12");
    EXPECT_EQ(p12->ReflectionName(), "`12");
}

TEST(DummyTypeParameterTest, SymbolSurface)
{
    auto p1 = DummyTypeParameter::GetMethodTypeParameter(1);
    EXPECT_EQ(p1->Kind(), TS::TypeKind::TypeParameter);
    EXPECT_EQ(p1->SymbolKind(), TS::SymbolKind::TypeParameter);
    EXPECT_EQ(p1->TypeParameterCount(), 0);
    EXPECT_FALSE(p1->IsReferenceType().has_value()); // the C# `bool? IsReferenceType => null`
    // `ToString` is a `DummyTypeParameter` member (the C# object.ToString override; the
    // port's IType carries no virtual ToString), so read it through the concrete type.
    EXPECT_EQ(std::static_pointer_cast<DummyTypeParameter>(p1)->ToString(), "``1 (dummy)");
    auto c1 = DummyTypeParameter::GetClassTypeParameter(1);
    EXPECT_EQ(std::static_pointer_cast<DummyTypeParameter>(c1)->ToString(), "`1 (dummy)");
}

TEST(DummyTypeParameterTest, OwnerTypeAndIndex)
{
    auto m2 = DummyTypeParameter::GetMethodTypeParameter(2);
    auto c2 = DummyTypeParameter::GetClassTypeParameter(2);
    EXPECT_EQ(m2->OwnerType(), TS::SymbolKind::Method);
    EXPECT_EQ(c2->OwnerType(), TS::SymbolKind::TypeDefinition);
    EXPECT_EQ(m2->Index(), 2);
    EXPECT_EQ(c2->Index(), 2);
    EXPECT_EQ(m2->Owner(), nullptr); // the C# `IEntity Owner => null`
}

TEST(DummyTypeParameterTest, ConstraintSurfaceIsAllEmpty)
{
    auto p = DummyTypeParameter::GetClassTypeParameter(1);
    EXPECT_TRUE(p->GetAttributes().empty());
    EXPECT_EQ(p->Variance(), TS::VarianceModifier::Invariant);
    // The C# `EffectiveBaseClass => SpecialType.UnknownType`.
    ASSERT_NE(p->EffectiveBaseClass(), nullptr);
    EXPECT_EQ(p->EffectiveBaseClass()->Kind(), TS::TypeKind::Unknown);
    EXPECT_TRUE(p->EffectiveInterfaceSet().empty());
    EXPECT_FALSE(p->HasDefaultConstructorConstraint());
    EXPECT_FALSE(p->HasReferenceTypeConstraint());
    EXPECT_FALSE(p->HasValueTypeConstraint());
    EXPECT_FALSE(p->HasUnmanagedConstraint());
    EXPECT_FALSE(p->AllowsRefLikeType());
    EXPECT_EQ(p->NullabilityConstraint(), TS::Nullability::Oblivious);
    EXPECT_TRUE(p->TypeConstraints().empty());
}

TEST(DummyTypeParameterTest, CacheReturnsStablePerIndexInstances)
{
    auto a0 = DummyTypeParameter::GetMethodTypeParameter(0);
    auto b0 = DummyTypeParameter::GetMethodTypeParameter(0);
    EXPECT_EQ(a0.get(), b0.get()); // same cached instance (the C# static array slot)
    // Asking for a far index grows the cache without disturbing earlier slots.
    auto a7 = DummyTypeParameter::GetMethodTypeParameter(7);
    EXPECT_EQ(a7->Index(), 7);
    EXPECT_EQ(DummyTypeParameter::GetMethodTypeParameter(7).get(), a7.get());
    EXPECT_EQ(DummyTypeParameter::GetMethodTypeParameter(3)->Index(), 3);
    EXPECT_EQ(DummyTypeParameter::GetMethodTypeParameter(0).get(), a0.get());
}

TEST(DummyTypeParameterTest, MethodAndClassCachesAreDistinct)
{
    auto m0 = DummyTypeParameter::GetMethodTypeParameter(0);
    auto c0 = DummyTypeParameter::GetClassTypeParameter(0);
    EXPECT_NE(m0.get(), c0.get()) // separate static arrays in the C#
        << "method and class type parameters are cached separately";
}

TEST(DummyTypeParameterTest, NegativeIndexThrows)
{
    // The C# `while (index >= tps.Length)` never terminates the grow-loop for a
    // negative index and `tps[index]` then throws IndexOutOfRangeException; the
    // ports throws immediately.
    EXPECT_THROW((void)DummyTypeParameter::GetMethodTypeParameter(-1), std::out_of_range);
    EXPECT_THROW((void)DummyTypeParameter::GetClassTypeParameter(-1), std::out_of_range);
}

TEST(DummyTypeParameterTest, EqualityIsCachedIdentity)
{
    auto a0 = DummyTypeParameter::GetMethodTypeParameter(0);
    auto b0 = DummyTypeParameter::GetMethodTypeParameter(0);
    auto c0 = DummyTypeParameter::GetClassTypeParameter(0);
    auto m1 = DummyTypeParameter::GetMethodTypeParameter(1);
    // The C# DummyTypeParameter inherits the reference-identity Equals; the cache
    // makes per-(kind, index) instances unique, so identity is the whole equality.
    EXPECT_TRUE(a0->Equals(*b0));
    EXPECT_FALSE(a0->Equals(*c0)) << "same index, different owner kind -> different instances";
    EXPECT_FALSE(a0->Equals(*m1)) << "different index -> different instances";
}

TEST(DummyTypeParameterTest, AcceptVisitorDispatchesVisitTypeParameter)
{
    auto p = DummyTypeParameter::GetMethodTypeParameter(0);
    RecordingVisitor visitor;
    TS::ITypePtr result = p->AcceptVisitor(visitor);
    EXPECT_EQ(visitor.last, "VisitTypeParameter");
    EXPECT_EQ(result.get(), p.get())
        << "the base VisitTypeParameter default returns the parameter unchanged";
}

TEST(DummyTypeParameterTest, ChangeNullabilityObliviousIsIdentity)
{
    auto p = DummyTypeParameter::GetClassTypeParameter(0);
    TS::ITypePtr result = p->ChangeNullability(TS::Nullability::Oblivious);
    EXPECT_EQ(result.get(), p.get()); // the C# `if (nullability == Oblivious) return this`
}

TEST(DummyTypeParameterTest, ChangeNullabilityWrapsNullabilityAnnotatedTypeParameter)
{
    auto p = DummyTypeParameter::GetMethodTypeParameter(2);
    TS::ITypePtr result = p->ChangeNullability(TS::Nullability::Nullable);
    // The C# `new NullabilityAnnotatedTypeParameter(this, nullability)`.
    auto natp = std::dynamic_pointer_cast<TS::NullabilityAnnotatedTypeParameter>(result);
    ASSERT_NE(natp, nullptr) << "a non-Oblivious annotation wraps the dummy parameter";
    EXPECT_EQ(natp->Nullability(), TS::Nullability::Nullable);
    EXPECT_EQ(natp->OriginalTypeParameter().get(), p.get());
    EXPECT_EQ(natp->TypeWithoutAnnotation().get(), p.get());
    // The wrapper delegates the type-parameter surface to the underlying parameter.
    EXPECT_EQ(natp->Index(), 2);
    EXPECT_EQ(natp->OwnerType(), TS::SymbolKind::Method);
    EXPECT_EQ(natp->SymbolKind(), TS::SymbolKind::TypeParameter);
    EXPECT_EQ(natp->Kind(), TS::TypeKind::TypeParameter);
    EXPECT_EQ(natp->Name(), p->Name());
    EXPECT_EQ(natp->ReflectionName(), p->ReflectionName());
    EXPECT_EQ(natp->Variance(), TS::VarianceModifier::Invariant);
    EXPECT_EQ(natp->Owner(), nullptr);
    EXPECT_TRUE(natp->GetAttributes().empty());
    EXPECT_TRUE(natp->EffectiveInterfaceSet().empty());
    EXPECT_FALSE(natp->HasValueTypeConstraint());
}

TEST(NullabilityAnnotatedTypeParameterTest, StructuralEqualsComparesAnnotationAndBase)
{
    auto p = DummyTypeParameter::GetMethodTypeParameter(0);
    auto a = std::make_shared<TS::NullabilityAnnotatedTypeParameter>(p, TS::Nullability::Nullable);
    auto b = std::make_shared<TS::NullabilityAnnotatedTypeParameter>(p, TS::Nullability::Nullable);
    auto c = std::make_shared<TS::NullabilityAnnotatedTypeParameter>(p, TS::Nullability::NotNullable);
    // The C# NullabilityAnnotatedType.Equals: same annotation + equal base type.
    // The `Equals` overload is reached through the NullabilityAnnotatedType reference
    // (an annotated parameter carries two IType subobjects; the cast picks the
    // decorated-type one).
    EXPECT_TRUE(a->Equals(static_cast<const TS::NullabilityAnnotatedType&>(*b)));
    EXPECT_FALSE(a->Equals(static_cast<const TS::NullabilityAnnotatedType&>(*c)))
        << "different annotation -> not equal";
    EXPECT_FALSE(a->Equals(*p)) << "annotated parameter is not equal to the bare parameter";
}

// The class shape pins the C# `public sealed class DummyTypeParameter : AbstractType,
// ITypeParameter` port shape (final = sealed, single ITypeParameter inheritance).
static_assert(std::is_final<DummyTypeParameter>::value, "DummyTypeParameter is sealed/final");
static_assert(std::is_base_of<TS::ITypeParameter, DummyTypeParameter>::value,
              "DummyTypeParameter implements ITypeParameter");
static_assert(!std::is_default_constructible<DummyTypeParameter>::value,
              "the ctor is private -- instances come from the static caches");
