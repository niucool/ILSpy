// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. HOWEVER CAUSED AND ON WHICHEVER THEORY OF LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH
// THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the minimal-port `TupleType` concrete `IType` -- the C# 7 tuple type
// `(int, string)` / `(int x, string y)` built atop an underlying `System.ValueTuple<...>`
// parameterized type. It is the fourth and final of the four "concrete IType VisitChildren
// types" (ModifiedType / NullabilityAnnotatedType / FunctionPointerType / TupleType) that the
// not-yet-ported TypeVisitor / TypeParameterSubstitution need; this port lands it as a
// self-contained leaf toward TypeVisitor / TypeSystemAstBuilder / CSharpAmbience. It follows
// the existing IType.hpp minimal-port convention (the D401/D402/D404 flatten-AbstractType
// precedent): the C# `TupleType : AbstractType, ICompilationProvider` is flattened to a direct
// `: IType` derivation, and the C# ctor's `CreateUnderlyingType` / `FindValueTupleType`
// recursion (which calls `ICompilation.FindType` at runtime to build the `System.ValueTuple<...>`
// chain) is DEFERRED by accepting an ALREADY-BUILT `UnderlyingType` as a ctor parameter -- the
// test builds the `ParameterizedType` itself and hands it in. `Name()` / `ReflectionName()`
// delegate to the `UnderlyingType` verbatim; the full IType surface (the `Compilation`
// property, the static `IsTupleCompatible` / `FromUnderlyingType` / `GetTupleElementTypes`
// helpers, `GetHashCode`, `ToString`, the member-access delegations, `AcceptVisitor` /
// `VisitChildren` -- the TypeVisitor dispatch) lands with the rest of Phase 2.

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;

namespace {

ITypePtr Int32Type() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr StringType() { return std::make_shared<KnownType>(KnownTypeCode::String); }
ITypePtr ObjectType() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

// The generic definition `System.ValueTuple`2` (the 8-ary `ValueTuple<T1,T2,...,TRest>` family,
// here arity 2). The minimal-port `SimpleType` carries the arity in the `TopLevelTypeName`.
ITypePtr ValueTuple2Definition() {
    return std::make_shared<SimpleType>(TopLevelTypeName("System", "ValueTuple", 2));
}

// The underlying type of a `(int, string)` tuple: `System.ValueTuple<int, string>`. The
// `ParameterizedType` wraps the `ValueTuple`2` definition with the element type arguments.
ITypePtr ValueTupleOfIntString() {
    return std::make_shared<ParameterizedType>(
        ValueTuple2Definition(), std::vector<ITypePtr>{Int32Type(), StringType()});
}

// A `(int, string)` tuple with no element names. The ctor's empty-`elementNames` sentinel
// fills the names with empty strings matching the element count (the C#
// `Enumerable.Repeat<string>(null, ...)`).
TupleType IntStringTuple() {
    return TupleType(ValueTupleOfIntString(),
                     std::vector<ITypePtr>{Int32Type(), StringType()},
                     std::vector<std::string>{});
}

}  // namespace

TEST(TupleTypeTest, KindIsTuple) {
    TupleType t = IntStringTuple();
    EXPECT_EQ(t.Kind(), TypeKind::Tuple);
}

TEST(TupleTypeTest, NameDelegatesToUnderlyingType) {
    // The C# `Name` delegates to `UnderlyingType.Name` -- the underlying `ValueTuple`2`
    // definition's short name, "ValueTuple".
    TupleType t = IntStringTuple();
    EXPECT_EQ(t.Name(), "ValueTuple");
}

TEST(TupleTypeTest, ReflectionNameDelegatesToUnderlyingType) {
    // The C# `ReflectionName` delegates to `UnderlyingType.ReflectionName` -- the full
    // `System.ValueTuple`2<System.Int32, System.String>` form, NOT a tuple-syntax form
    // like "(int, string)" (the tuple syntax the C# surfaces only in `ToString`).
    TupleType t = IntStringTuple();
    EXPECT_EQ(t.ReflectionName(), "System.ValueTuple`2<System.Int32, System.String>");
}

TEST(TupleTypeTest, TypeParameterCountIsZero) {
    // The C# `TupleType` overrides `TypeParameterCount` to `0`, distinct from the
    // underlying `ValueTuple<...>` which has arity 2 -- the tuple's own type-parameter
    // count is 0; the arity lives on the underlying parameterized type.
    TupleType t = IntStringTuple();
    EXPECT_EQ(t.TypeParameterCount(), 0);
}

TEST(TupleTypeTest, CardinalityReturnsElementCount) {
    // The C# `Cardinality => ElementTypes.Length`.
    TupleType t = IntStringTuple();
    EXPECT_EQ(t.Cardinality(), 2);
    // A single-element tuple has cardinality 1.
    ITypePtr one = std::make_shared<ParameterizedType>(
        std::make_shared<SimpleType>(TopLevelTypeName("System", "ValueTuple", 1)),
        std::vector<ITypePtr>{Int32Type()});
    TupleType single(one, std::vector<ITypePtr>{Int32Type()}, std::vector<std::string>{});
    EXPECT_EQ(single.Cardinality(), 1);
}

TEST(TupleTypeTest, ElementTypesReturnConfiguredValues) {
    ITypePtr e0 = Int32Type();
    ITypePtr e1 = StringType();
    TupleType t(ValueTupleOfIntString(),
                std::vector<ITypePtr>{e0, e1},
                std::vector<std::string>{});
    ASSERT_EQ(t.ElementTypes().size(), 2u);
    EXPECT_EQ(t.ElementTypes()[0].get(), e0.get());
    EXPECT_EQ(t.ElementTypes()[1].get(), e1.get());
}

TEST(TupleTypeTest, ElementNamesDefaultToEmptyWhenNotProvided) {
    // The C# ctor fills `ElementNames` with `null` strings matching `ElementTypes.Length`
    // when the caller passes `default(ImmutableArray<string>)`. The C++ minimal port treats
    // an empty `elementNames` vector as the "not provided" sentinel and fills it with empty
    // strings -- so an unnamed 2-tuple carries two empty names, NOT a zero-length vector.
    TupleType t = IntStringTuple();
    ASSERT_EQ(t.ElementNames().size(), 2u);
    EXPECT_EQ(t.ElementNames()[0], "");
    EXPECT_EQ(t.ElementNames()[1], "");
}

TEST(TupleTypeTest, ElementNamesReturnConfiguredValues) {
    // A `(int x, string y)` named tuple -- the caller supplies the names.
    TupleType t(ValueTupleOfIntString(),
                std::vector<ITypePtr>{Int32Type(), StringType()},
                std::vector<std::string>{"x", "y"});
    ASSERT_EQ(t.ElementNames().size(), 2u);
    EXPECT_EQ(t.ElementNames()[0], "x");
    EXPECT_EQ(t.ElementNames()[1], "y");
}

TEST(TupleTypeTest, UnderlyingTypeReturnsConfiguredValue) {
    ITypePtr underlying = ValueTupleOfIntString();
    TupleType t(underlying,
                std::vector<ITypePtr>{Int32Type(), StringType()},
                std::vector<std::string>{});
    EXPECT_EQ(t.UnderlyingType().get(), underlying.get());
}

TEST(TupleTypeTest, EqualsComparesUnderlyingTypeAndElementNames) {
    TupleType a = IntStringTuple();
    TupleType same = IntStringTuple();
    EXPECT_TRUE(a.Equals(same));

    // Different element names => not equal (the names participate in equality).
    TupleType diffNames(ValueTupleOfIntString(),
                        std::vector<ITypePtr>{Int32Type(), StringType()},
                        std::vector<std::string>{"x", "y"});
    EXPECT_FALSE(a.Equals(diffNames));

    // Different underlying type (a `ValueTuple<int, object>` vs `ValueTuple<int, string>`)
    // => not equal.
    ITypePtr otherUnderlying = std::make_shared<ParameterizedType>(
        ValueTuple2Definition(),
        std::vector<ITypePtr>{Int32Type(), ObjectType()});
    TupleType diffUnderlying(otherUnderlying,
                             std::vector<ITypePtr>{Int32Type(), ObjectType()},
                             std::vector<std::string>{});
    EXPECT_FALSE(a.Equals(diffUnderlying));

    // Same underlying type but different element NAMES renders equal-underlying but
    // unequal-names: the names are the distinguishing field (the C# `Equals` checks both).
    TupleType sameUnderlyingDiffNames(ValueTupleOfIntString(),
                                      std::vector<ITypePtr>{Int32Type(), StringType()},
                                      std::vector<std::string>{"a", "b"});
    EXPECT_FALSE(a.Equals(sameUnderlyingDiffNames));

    // A type of a different kind is never equal (Kind differs, so IType::Equals
    // short-circuits before StructuralEquals -- TupleType has a unique Kind).
    EXPECT_FALSE(a.Equals(*Int32Type()));
    EXPECT_FALSE(a.Equals(*ObjectType()));
}

TEST(TupleTypeTest, EqualsIsReflexiveAndSymmetric) {
    TupleType a = IntStringTuple();
    TupleType b = IntStringTuple();
    EXPECT_TRUE(a.Equals(a));
    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(b.Equals(a));
}

TEST(TupleTypeTest, EqualsShortCircuitsOnDifferentKind) {
    // A tuple type is never equal to a type of a different kind (Kind differs, so
    // IType::Equals short-circuits before StructuralEquals).
    TupleType t = IntStringTuple();
    EXPECT_FALSE(t.Equals(*Int32Type()));
    EXPECT_FALSE(t.Equals(*ObjectType()));
}

TEST(TupleTypeTest, DispatchesPolymorphicallyThroughITypeReference) {
    TupleType t(ValueTupleOfIntString(),
                std::vector<ITypePtr>{Int32Type(), StringType()},
                std::vector<std::string>{"x", "y"});
    const IType& asBase = t;
    EXPECT_EQ(asBase.Kind(), TypeKind::Tuple);
    EXPECT_EQ(asBase.Name(), "ValueTuple");
    EXPECT_EQ(asBase.ReflectionName(), "System.ValueTuple`2<System.Int32, System.String>");
    EXPECT_EQ(asBase.TypeParameterCount(), 0);
    // The Cardinality / ElementTypes / ElementNames / UnderlyingType accessors are
    // TupleType-own (not IType virtuals in the minimal port -- the full IType surface
    // lands with the rest of Phase 2), so they are reached only through the concrete
    // type, not through the IType base.
    EXPECT_EQ(t.Cardinality(), 2);
    EXPECT_EQ(t.ElementNames()[0], "x");
    TupleType same(ValueTupleOfIntString(),
                   std::vector<ITypePtr>{Int32Type(), StringType()},
                   std::vector<std::string>{"x", "y"});
    EXPECT_TRUE(asBase.Equals(same));
}
