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

// Tests for `NormalizeTypeVisitor` (the port of
// ICSharpCode.Decompiler/TypeSystem/NormalizeTypeVisitor.cs) -- the type-rewriting
// visitor the signature comparers use to erase differences that should not matter
// (method type parameter identity, object-vs-dynamic, tuple-vs-ValueTuple, custom
// modifiers, nullability annotations). The load-bearing cruxes are: the three
// named singleton configurations (`TypeErasure` / `IgnoreNullabilityAndTuples` /
// `IgnoreNullability`) with their exact field layouts, the method-vs-class type
// parameter replacement split into `DummyTypeParameter`s, the OPPOSITE-direction
// object->dynamic normalization (so no compilation is needed to find the object
// type), and the nullability/modifier/annotation removal arms.

#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"

#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::Implementation::DummyTypeParameter;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupTypeDefinition;

namespace {

// A LookupTypeDefinition with the faithful C# ITypeDefinition AcceptVisitor dispatch
// (visitor.VisitTypeDefinition -- the stub base inherits the IType VisitOtherType
// default, which would bypass the visitor's VisitTypeDefinition override) and an
// adjustable Nullability (the C# AbstractType default Nullability is Oblivious; the
// visitor's Dynamic arm reads it when RemoveNullability is off).
class VisitableDefinition : public LookupTypeDefinition {
public:
    VisitableDefinition(std::string name, TS::KnownTypeCode code,
                        const TS::ICompilation& compilation)
        : LookupTypeDefinition(std::move(name), "System",
                               TS::FullTypeName(TS::TopLevelTypeName("System", name.empty() ? "?" : name)),
                               TS::TypeKind::Class,
                               TS::Accessibility::Public, compilation,
                               &compilation.MainModule(), code) {}

    void SetNullability(TS::Nullability n) { nullability_ = n; }
    TS::Nullability Nullability() const override { return nullability_; }
    TS::ITypePtr AcceptVisitor(TS::TypeVisitor& visitor) override
    {
        return visitor.VisitTypeDefinition(*this);
    }

private:
    TS::Nullability nullability_ = TS::Nullability::Oblivious;
};

// A configurable type parameter (the LookupTypeParameter stub fixes OwnerType at
// Method and Index at 0; class-parameter substitutes need the TypeDefinition owner).
class StubTypeParameter : public TS::ITypeParameter {
public:
    StubTypeParameter(std::string name, TS::SymbolKind ownerType, int index)
        : name_(std::move(name)), ownerType_(ownerType), index_(index) {}

    // --- IType ---
    TS::TypeKind Kind() const override { return TS::TypeKind::TypeParameter; }
    // The faithful C# type-parameter AcceptVisitor dispatch (the port's IType default
    // would fall back to VisitOtherType and bypass the visitor's VisitTypeParameter).
    TS::ITypePtr AcceptVisitor(TS::TypeVisitor& visitor) override
    {
        return visitor.VisitTypeParameter(*this);
    }
    // The single `Name()` override is the final overrider for the
    // IType/ISymbol/ITypeParameter diamond (the LookupStubs convention).
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::TypeParameter; }

    // --- ITypeParameter ---
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
        return this == &other; // identity equality for the test stub
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

TS::ITypePtr Visit(TS::IType& type, TS::NormalizeTypeVisitor& visitor)
{
    return type.AcceptVisitor(visitor);
}

} // namespace

TEST(NormalizeTypeVisitorTest, FieldDefaultsAreAllTrue)
{
    TS::NormalizeTypeVisitor visitor;
    // All eight option fields default-initialize to true (the C# field initializers).
    EXPECT_TRUE(visitor.RemoveModOpt);
    EXPECT_TRUE(visitor.RemoveModReq);
    EXPECT_TRUE(visitor.ReplaceClassTypeParametersWithDummy);
    EXPECT_TRUE(visitor.ReplaceMethodTypeParametersWithDummy);
    EXPECT_TRUE(visitor.DynamicAndObject);
    EXPECT_TRUE(visitor.IntPtrToNInt);
    EXPECT_TRUE(visitor.TupleToUnderlyingType);
    EXPECT_TRUE(visitor.RemoveNullability);
}

TEST(NormalizeTypeVisitorTest, NamedSingletonConfigurations)
{
    // The C# object-initializers on the three named static instances.
    auto& erasure = TS::NormalizeTypeVisitor::TypeErasure();
    EXPECT_FALSE(erasure.ReplaceClassTypeParametersWithDummy);
    EXPECT_FALSE(erasure.ReplaceMethodTypeParametersWithDummy);
    EXPECT_TRUE(erasure.DynamicAndObject);
    EXPECT_TRUE(erasure.IntPtrToNInt);
    EXPECT_TRUE(erasure.TupleToUnderlyingType);
    EXPECT_TRUE(erasure.RemoveModOpt);
    EXPECT_TRUE(erasure.RemoveModReq);
    EXPECT_TRUE(erasure.RemoveNullability);

    auto& noTuples = TS::NormalizeTypeVisitor::IgnoreNullabilityAndTuples();
    EXPECT_FALSE(noTuples.ReplaceClassTypeParametersWithDummy);
    EXPECT_FALSE(noTuples.ReplaceMethodTypeParametersWithDummy);
    EXPECT_FALSE(noTuples.DynamicAndObject);
    EXPECT_FALSE(noTuples.IntPtrToNInt);
    EXPECT_TRUE(noTuples.TupleToUnderlyingType);
    EXPECT_TRUE(noTuples.RemoveNullability);

    auto& noNullability = TS::NormalizeTypeVisitor::IgnoreNullability();
    EXPECT_FALSE(noNullability.DynamicAndObject);
    EXPECT_FALSE(noNullability.IntPtrToNInt);
    EXPECT_FALSE(noNullability.TupleToUnderlyingType);
    EXPECT_TRUE(noNullability.RemoveModOpt);
    EXPECT_TRUE(noNullability.RemoveModReq);
    EXPECT_TRUE(noNullability.RemoveNullability);
}

TEST(NormalizeTypeVisitorTest, NamedSingletonsAreStableInstances)
{
    // The C# `static readonly` fields are one instance each.
    EXPECT_EQ(&TS::NormalizeTypeVisitor::TypeErasure(), &TS::NormalizeTypeVisitor::TypeErasure());
    EXPECT_EQ(&TS::NormalizeTypeVisitor::IgnoreNullability(),
              &TS::NormalizeTypeVisitor::IgnoreNullability());
    EXPECT_NE(&TS::NormalizeTypeVisitor::TypeErasure(),
              &TS::NormalizeTypeVisitor::IgnoreNullability());
}

TEST(NormalizeTypeVisitorTest, MethodTypeParametersBecomeDummies)
{
    auto t = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::Method, 0);
    auto u = std::make_shared<StubTypeParameter>("U", TS::SymbolKind::Method, 1);
    TS::NormalizeTypeVisitor visitor; // ReplaceMethodTypeParametersWithDummy = true
    TS::ITypePtr rt = Visit(*t, visitor);
    TS::ITypePtr ru = Visit(*u, visitor);
    EXPECT_EQ(rt.get(), DummyTypeParameter::GetMethodTypeParameter(0).get());
    EXPECT_EQ(ru.get(), DummyTypeParameter::GetMethodTypeParameter(1).get());
    EXPECT_EQ(rt->Name(), "!!0");
}

TEST(NormalizeTypeVisitorTest, ClassTypeParametersBecomeDummiesWhenEnabled)
{
    auto t = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::TypeDefinition, 0);
    TS::NormalizeTypeVisitor replaceAll; // defaults: ReplaceClassTypeParametersWithDummy = true
    TS::ITypePtr rt = Visit(*t, replaceAll);
    EXPECT_EQ(rt.get(), DummyTypeParameter::GetClassTypeParameter(0).get());

    // Class versus method parameters at the same index normalize to DIFFERENT dummies
    // (a class type parameter never compares equal to a method type parameter).
    auto m = std::make_shared<StubTypeParameter>("S", TS::SymbolKind::Method, 0);
    TS::ITypePtr rm = Visit(*m, replaceAll);
    EXPECT_NE(rt.get(), rm.get());
}

TEST(NormalizeTypeVisitorTest, TypeParametersPassThroughWhenNotReplaced)
{
    auto t = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::Method, 2);
    TS::NormalizeTypeVisitor visitor;
    visitor.ReplaceMethodTypeParametersWithDummy = false;
    TS::ITypePtr result = Visit(*t, visitor);
    EXPECT_EQ(result.get(), t.get())
        << "with replacement off the base VisitTypeParameter default returns the parameter";
}

TEST(NormalizeTypeVisitorTest, ObjectBecomesDynamic)
{
    auto object = std::make_shared<VisitableDefinition>(
        "Object", TS::KnownTypeCode::Object, Compilation());
    TS::NormalizeTypeVisitor visitor; // DynamicAndObject = true, RemoveNullability = true
    TS::ITypePtr result = Visit(*object, visitor);
    EXPECT_EQ(result->Kind(), TS::TypeKind::Dynamic)
        << "object normalizes to SpecialType.Dynamic (the opposite direction of"
           " dynamic->object, so no compilation is needed)";
    EXPECT_EQ(result->Name(), "dynamic");

    TS::NormalizeTypeVisitor keepObject;
    keepObject.DynamicAndObject = false;
    TS::ITypePtr kept = Visit(*object, keepObject);
    EXPECT_EQ(kept.get(), object.get());
}

TEST(NormalizeTypeVisitorTest, ObjectBecomesAnnotatedDynamicWhenKeepingNullability)
{
    auto object = std::make_shared<VisitableDefinition>(
        "Object", TS::KnownTypeCode::Object, Compilation());
    object->SetNullability(TS::Nullability::Nullable);
    TS::NormalizeTypeVisitor visitor;
    visitor.RemoveNullability = false;
    TS::ITypePtr result = Visit(*object, visitor);
    // The C# `SpecialType.Dynamic.ChangeNullability(type.Nullability)` arm.
    auto annotated = std::dynamic_pointer_cast<TS::NullabilityAnnotatedType>(result);
    ASSERT_NE(annotated, nullptr) << "nullable object normalizes to dynamic?";
    EXPECT_EQ(annotated->Nullability(), TS::Nullability::Nullable);
    EXPECT_EQ(annotated->TypeWithoutAnnotation()->Kind(), TS::TypeKind::Dynamic);
}

TEST(NormalizeTypeVisitorTest, IntPtrAndUIntPtrBecomeNIntAndNUInt)
{
    auto intptr = std::make_shared<VisitableDefinition>(
        "IntPtr", TS::KnownTypeCode::IntPtr, Compilation());
    auto uintptr = std::make_shared<VisitableDefinition>(
        "UIntPtr", TS::KnownTypeCode::UIntPtr, Compilation());
    TS::NormalizeTypeVisitor visitor; // IntPtrToNInt = true
    EXPECT_EQ(Visit(*intptr, visitor)->Kind(), TS::TypeKind::NInt);
    EXPECT_EQ(Visit(*uintptr, visitor)->Kind(), TS::TypeKind::NUInt);

    TS::NormalizeTypeVisitor keep;
    keep.IntPtrToNInt = false;
    EXPECT_EQ(Visit(*intptr, keep).get(), intptr.get());
}

TEST(NormalizeTypeVisitorTest, NonSpecialDefinitionsPassThrough)
{
    auto stringDef = std::make_shared<VisitableDefinition>(
        "String", TS::KnownTypeCode::String, Compilation());
    TS::NormalizeTypeVisitor visitor;
    EXPECT_EQ(Visit(*stringDef, visitor).get(), stringDef.get());
}

TEST(NormalizeTypeVisitorTest, TupleBecomesUnderlyingTypeWhenEnabled)
{
    auto underlying = std::make_shared<TS::ParameterizedType>(
        std::make_shared<TS::SimpleType>(TS::TopLevelTypeName("System", "ValueTuple`1")),
        std::vector<TS::ITypePtr>{ std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32) });
    auto tuple = std::make_shared<TS::TupleType>(
        underlying, std::vector<TS::ITypePtr>{ underlying->TypeArguments() });

    TS::NormalizeTypeVisitor visitor; // TupleToUnderlyingType = true
    TS::ITypePtr result = Visit(*tuple, visitor);
    EXPECT_EQ(result.get(), underlying.get());

    TS::NormalizeTypeVisitor keep;
    keep.TupleToUnderlyingType = false;
    TS::ITypePtr kept = Visit(*tuple, keep);
    EXPECT_NE(kept.get(), underlying.get())
        << "the base VisitTupleType recurses into the element types instead";
}

TEST(NormalizeTypeVisitorTest, NullabilityAnnotationIsRemovedWhenEnabled)
{
    auto base = std::make_shared<VisitableDefinition>(
        "String", TS::KnownTypeCode::String, Compilation());
    auto annotated = std::make_shared<TS::NullabilityAnnotatedType>(base, TS::Nullability::Nullable);
    TS::NormalizeTypeVisitor visitor; // RemoveNullability = true
    TS::ITypePtr result = Visit(*annotated, visitor);
    EXPECT_EQ(result.get(), base.get());

    TS::NormalizeTypeVisitor keep;
    keep.RemoveNullability = false;
    TS::ITypePtr kept = Visit(*annotated, keep);
    auto keptAnnotated = std::dynamic_pointer_cast<TS::NullabilityAnnotatedType>(kept);
    ASSERT_NE(keptAnnotated, nullptr) << "the annotation is kept when RemoveNullability is off";
    EXPECT_EQ(keptAnnotated->Nullability(), TS::Nullability::Nullable);
}

TEST(NormalizeTypeVisitorTest, CustomModifiersAreRemovedWhenEnabled)
{
    auto element = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto modifier = std::make_shared<TS::KnownType>(TS::KnownTypeCode::IntPtr);
    auto modopt = std::make_shared<TS::ModifiedType>(modifier, element, /*isRequired=*/false);
    auto modreq = std::make_shared<TS::ModifiedType>(modifier, element, /*isRequired=*/true);

    TS::NormalizeTypeVisitor visitor; // RemoveModOpt / RemoveModReq = true
    EXPECT_EQ(Visit(*modopt, visitor).get(), element.get());
    EXPECT_EQ(Visit(*modreq, visitor).get(), element.get());

    TS::NormalizeTypeVisitor keep;
    keep.RemoveModOpt = false;
    keep.RemoveModReq = false;
    EXPECT_EQ(Visit(*modopt, keep).get(), modopt.get());
    EXPECT_EQ(Visit(*modreq, keep).get(), modreq.get());
}

TEST(NormalizeTypeVisitorTest, EquivalentTypesComparesAfterNormalization)
{
    TS::NormalizeTypeVisitor visitor;

    // Method type parameters with different names compare equal (the documented
    // `Method<T>(T a)` vs `Method<S>(S b)` crux of ParameterListComparer).
    auto t = std::make_shared<StubTypeParameter>("T", TS::SymbolKind::Method, 0);
    auto s = std::make_shared<StubTypeParameter>("S", TS::SymbolKind::Method, 0);
    EXPECT_TRUE(visitor.EquivalentTypes(*t, *s));

    // Different indices stay different.
    auto t1 = std::make_shared<StubTypeParameter>("T1", TS::SymbolKind::Method, 1);
    EXPECT_FALSE(visitor.EquivalentTypes(*t, *t1));

    // A nullability annotation never matters.
    auto base = std::make_shared<VisitableDefinition>(
        "String", TS::KnownTypeCode::String, Compilation());
    auto annotated = std::make_shared<TS::NullabilityAnnotatedType>(base, TS::Nullability::Nullable);
    EXPECT_TRUE(visitor.EquivalentTypes(*annotated, *base));

    // object is equivalent to dynamic.
    auto object = std::make_shared<VisitableDefinition>(
        "Object", TS::KnownTypeCode::Object, Compilation());
    auto dynamic = std::make_shared<TS::SpecialType>(TS::TypeKind::Dynamic, /*isReferenceType=*/true);
    EXPECT_TRUE(visitor.EquivalentTypes(*object, *dynamic));
    EXPECT_TRUE(visitor.EquivalentTypes(*dynamic, *object));

    // int is not equivalent to string.
    auto int32 = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto str = std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    EXPECT_FALSE(visitor.EquivalentTypes(*int32, *str));
}

// The class shape pins the C# `sealed class NormalizeTypeVisitor : TypeVisitor`.
static_assert(std::is_final<TS::NormalizeTypeVisitor>::value,
              "NormalizeTypeVisitor is sealed/final");
static_assert(std::is_base_of<TS::TypeVisitor, TS::NormalizeTypeVisitor>::value,
              "NormalizeTypeVisitor derives from TypeVisitor");
