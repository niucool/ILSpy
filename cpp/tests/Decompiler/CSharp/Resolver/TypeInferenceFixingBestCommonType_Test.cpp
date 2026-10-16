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
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `TypeInference` Fixing / FindTypeInBounds / GetBestCommonType regions
// (the sixth slice of the TypeInference long pole): `Detail::GetFirstTypePreferNonInterfaces`
// (TypeInference.cs lines 1055-1058), `Detail::FindTypesInBounds` (lines 1059-1186, the
// C# spec draft-v11 12.6.3.13 candidate-types algorithm PLUS the IMPROVED refinement
// of lines 1101-1176 -- the lower bounds' base-type-definition intersection, the
// compilation-wide `GetAllTypeDefinitions` scan, the upper-bound `IsDerivedFrom` filter,
// the generic candidates' `InferTypeArgumentsFromBounds` construction, and the
// most-specific/least-specific redundancy reduction), `Detail::FindTypeInBounds`
// (lines 1033-1053), `Detail::Fix` (lines 967-994, section 12.6.3.13 "Fixing"), and
// `Detail::GetBestCommonType` (lines 1001-1026, section 12.6.3.17).
//
// The load-bearing cruxes:
//  (a) the exact-bound fast path of `Fix` -- the exact bound ALWAYS becomes FixedTo
//      (even when the fix fails), the multiple-different-exact-bounds abort, and the
//      lower/upper bounds must still convert to/from the fixed type;
//  (b) the non-exact path's single-candidate success -- the widening chain [int, long]
//      fixes to long, the unrelated pair yields the empty candidate list and the
//      UnknownType null object with success=false;
//  (c) the spec candidate-types algorithm -- a candidate must convert from every lower
//      bound and to every upper bound, then be the unique candidate all the others
//      convert to; the union of the bounds dedups under IType.Equals;
//  (d) the GetBestCommonType dummy-TP pipeline -- a single expression short-circuits to
//      its own type (gated on IsValidType), multiple expressions lower-bound the cached
//      DummyTypeParameter.GetMethodTypeParameter(0) through MakeOutputTypeInference, and
//      a lambda contributes NOTHING against the dummy (the dummy is not a delegate, so
//      the lambda falls through to the plain arm whose NoType fails the IsValidType gate);
//  (e) the `ImprovedReturnAllResults` arms -- the multi-candidate fix/lookup reduces to
//      the INTERSECTION of all candidates (an `IntersectionType`, success
//      `types.Count >= 1`) while the other algorithms pick the first non-interface
//      candidate and fail (`types.Count == 1`); for the 0- and 1-candidate shapes the
//      arms coincide with the fallback (`IntersectionType.Create` maps an empty list to
//      `SpecialType.UnknownType` and a singleton to the single type itself), pinned by
//      the single-candidate and empty-candidate `ImprovedReturnAllResults` tests.
//
// The stubs mirror the TypeInferenceMakeOutputTypeInference_Test conventions (`Def` for
// the GetTypeCode-resolving LookupTypeDefinition stubs, `RefDef` for the definite
// reference types the conversion guards need, `TestLambda` for the lambda fall-through).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"
#include "Decompiler/TypeSystem/IntersectionType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace Sem = ILSpy::Decompiler::Semantics;
namespace Res = ILSpy::Decompiler::CSharp::Resolver;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::FindTypeInBounds;
using ILSpy::Decompiler::CSharp::Resolver::Detail::FindTypesInBounds;
using ILSpy::Decompiler::CSharp::Resolver::Detail::Fix;
using ILSpy::Decompiler::CSharp::Resolver::Detail::GetBestCommonType;
using ILSpy::Decompiler::CSharp::Resolver::Detail::GetFirstTypePreferNonInterfaces;
using ILSpy::Decompiler::CSharp::Resolver::Detail::TP;
using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::Implementation::DummyTypeParameter;
using ILSpy::Decompiler::TypeSystem::IntersectionType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::UnknownType;

// The shared compilation (a plain `LookupCompilation` -- none of the shapes tested here
// is span-shaped or needs a FindType registration; the reference conversions resolve via
// the IsKnownType Object short-circuit over the stubs' own KnownTypeCode).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` / `TypeKind`. The D514 `MakeDef` precedent -- the
// GetTypeCode-resolving shape the numeric conversion arms need (a `KnownType`
// placeholder is NOT an `ITypeDefinition` and yields TypeCode::Empty).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }

// A `LookupTypeDefinition` overriding `IsReferenceType` to a DEFINITE true -- the
// reference-conversion guard requires `== true` on both sides (a plain `Def` inherits
// the indeterminate `nullopt` and fails the guard). The D517 `RefDef` precedent.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

std::shared_ptr<RefDef> MakeRef(KnownTypeCode ktc, const std::string& name) {
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A `RefDef` bound to a CALLER-SUPPLIED compilation (the improved-refinement tests
// need a fresh per-test compilation -- the module type tables must not leak across
// tests, and `IsDerivedFrom`'s same-compilation check requires the candidates to be
// bound to the very compilation the local conversions carry). The optional arity wires
// the `FullTypeName` type-parameter count the generic-candidate arm reads through
// `IType::TypeParameterCount()`.
std::shared_ptr<RefDef> MakeLocalRef(LookupCompilation& compilation, const std::string& name,
                                    TypeKind kind = TypeKind::Class, int typeParameterCount = 0)
{
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, typeParameterCount)),
        kind, Accessibility::Public, compilation, nullptr);
}

// A one-entry `TP` state over the cached dummy METHOD type parameter 0 (the very dummy
// `GetBestCommonType` uses; for the direct `Fix` tests the tracked parameter is
// irrelevant -- `Fix` reads only the bounds).
std::vector<TP> MakeState() {
    std::vector<TP> state;
    state.emplace_back(*DummyTypeParameter::GetMethodTypeParameter(0));
    return state;
}

// A concrete `LambdaResolveResult` stub (the D534/D570 `TestLambda` precedent) -- the
// fall-through test only needs the RTTI and the NoType base; no delegate signature ever
// resolves against the dummy.
class TestLambda : public LambdaResolveResult {
public:
    ITypePtr returnType = std::make_shared<TS::KnownType>(KnownTypeCode::Int32);
    std::shared_ptr<ResolveResult> body =
        std::make_shared<TypeResolveResult>(
            std::make_shared<TS::KnownType>(KnownTypeCode::Void));

    bool HasParameterList() const override { return true; }
    bool IsAnonymousMethod() const override { return false; }
    bool IsImplicitlyTyped() const override { return true; }
    bool IsAsync() const override { return false; }
    ITypePtr GetInferredReturnType(const std::vector<ITypePtr>&) const override {
        return returnType;
    }
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> Parameters() const override {
        return {};
    }
    const IType& ReturnType() const override { return *returnType; }
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> IsValid(
        const std::vector<ITypePtr>&, const ITypePtr&,
        ILSpy::Decompiler::CSharp::Resolver::CSharpConversions&) const override
    {
        return std::make_shared<ILSpy::Decompiler::CSharp::Resolver::LambdaConversion>();
    }
    ResolveResult& Body() const override { return *body; }
    std::unique_ptr<ResolveResult> ShallowClone() const override
    {
        return std::make_unique<TestLambda>(*this);
    }
protected:
    std::string ClassName() const override { return "TestLambda"; }
};

// A `TypeResolveResult` expression over the supplied type (the plain-expression shape
// `MakeOutputTypeInference`'s plain arm consumes).
std::shared_ptr<ResolveResult> Expr(ITypePtr type) {
    return std::make_shared<TypeResolveResult>(std::move(type));
}

} // namespace

// ===========================================================================
// GetFirstTypePreferNonInterfaces (TypeInference.cs lines 1055-1058).
// ===========================================================================

// The FIRST non-interface candidate wins over an earlier interface candidate.
TEST(TypeInferenceFixingBestCommonTypeTest, PrefersNonInterfaceOverEarlierInterface)
{
    ITypePtr interfaceType = Def(KnownTypeCode::None, TypeKind::Interface);
    ITypePtr classType = Def(KnownTypeCode::None, TypeKind::Class);
    ITypePtr result = GetFirstTypePreferNonInterfaces({interfaceType, classType});
    EXPECT_EQ(result.get(), classType.get());
}

// An all-interface list takes the first interface (no non-interface candidate exists).
TEST(TypeInferenceFixingBestCommonTypeTest, ReturnsInterfaceWhenOnlyInterfaces)
{
    ITypePtr interfaceType = Def(KnownTypeCode::None, TypeKind::Interface);
    ITypePtr result = GetFirstTypePreferNonInterfaces({interfaceType});
    EXPECT_EQ(result.get(), interfaceType.get());
}

// An empty candidate list takes the `SpecialType.UnknownType` null object.
TEST(TypeInferenceFixingBestCommonTypeTest, EmptyListReturnsUnknownType)
{
    ITypePtr result = GetFirstTypePreferNonInterfaces({});
    EXPECT_EQ(result->Kind(), TypeKind::Unknown);
}

// ===========================================================================
// FindTypesInBounds (TypeInference.cs lines 1059-1186, the spec 12.6.3.13 candidate-
// types algorithm).
// ===========================================================================

// Both inputs empty -> the empty list (the first early-out).
TEST(TypeInferenceFixingBestCommonTypeTest, BothBoundsEmptyReturnsEmpty)
{
    CSharpConversions conversions(Compilation());
    EXPECT_TRUE(FindTypesInBounds(conversions, {}, {}, Res::TypeInferenceAlgorithm::CSharp4,
                                  /*nestingLevel*/ 0)
                    .empty());
}

// A single lower bound and no upper bound returns that lower bound as-is (the second
// early-out -- no conversion filtering runs).
TEST(TypeInferenceFixingBestCommonTypeTest, SingleLowerNoUpperReturnsLower)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, {int32}, {}, Res::TypeInferenceAlgorithm::CSharp4, /*nestingLevel*/ 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), int32.get());
}

// No lower bound and a single upper bound returns that upper bound as-is (the first
// early-out).
TEST(TypeInferenceFixingBestCommonTypeTest, NoLowerSingleUpperReturnsUpper)
{
    CSharpConversions conversions(Compilation());
    ITypePtr objectType = Def(KnownTypeCode::Object, TypeKind::Class);
    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, {}, {objectType}, Res::TypeInferenceAlgorithm::CSharp4, /*nestingLevel*/ 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), objectType.get());
}

// The nesting guard: a nesting level beyond the maxNestingLevel (5) yields the empty
// list -- the C# `CreateNestedInstance` recursion bound (relevant only for the Improved
// algorithm's generic-candidate recursion, but the guard fires for every algorithm).
TEST(TypeInferenceFixingBestCommonTypeTest, NestingGuardReturnsEmpty)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    EXPECT_TRUE(FindTypesInBounds(conversions, {int32, int64}, {},
                                  Res::TypeInferenceAlgorithm::CSharp4,
                                  /*nestingLevel*/ 6)
                    .empty());
}

// The widening chain [int, long]: long is the unique candidate every other candidate
// converts to (int->long is implicit; long->int is not) -- the spec 12.6.3.13 result.
TEST(TypeInferenceFixingBestCommonTypeTest, WideningLowerBoundsYieldWidest)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, {int32, int64}, {}, Res::TypeInferenceAlgorithm::CSharp4, /*nestingLevel*/ 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), int64.get());
}

// An unrelated pair (string, int): neither converts to the other, so no candidate
// survives the lower-bound filter -- the empty list.
TEST(TypeInferenceFixingBestCommonTypeTest, UnrelatedLowerBoundsYieldEmpty)
{
    CSharpConversions conversions(Compilation());
    ITypePtr stringType = MakeRef(KnownTypeCode::String, "String");
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    EXPECT_TRUE(FindTypesInBounds(conversions, {stringType, int32}, {},
                                  Res::TypeInferenceAlgorithm::CSharp4, /*nestingLevel*/ 0)
                    .empty());
}

// The union of the bounds dedups under IType.Equals: the same instance on both sides
// yields a single candidate.
TEST(TypeInferenceFixingBestCommonTypeTest, UnionDedupsEqualTypes)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, {int32}, {int32}, Res::TypeInferenceAlgorithm::CSharp4, /*nestingLevel*/ 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), int32.get());
}

// A single candidate returns before the deferred improved refinement -- the
// `candidateTypes.Count == 1` arm of the algorithm branch (the Improved algorithm value
// threads through).
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedSingleCandidateReturnsEarly)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, {int32, int64}, {}, Res::TypeInferenceAlgorithm::Improved,
        /*nestingLevel*/ 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), int64.get());
}

// No lower bounds and two upper bounds: the candidate must convert to BOTH upper bounds
// -- the derived type does (identity + the Object widening), the object type does not
// (object -> derived has no implicit conversion).
TEST(TypeInferenceFixingBestCommonTypeTest, TwoUpperBoundsNoLowerPickConvertibleToBoth)
{
    CSharpConversions conversions(Compilation());
    ITypePtr derived = MakeRef(KnownTypeCode::None, "Derived");
    ITypePtr object = MakeRef(KnownTypeCode::Object, "Object");
    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, {}, {derived, object}, Res::TypeInferenceAlgorithm::CSharp4,
        /*nestingLevel*/ 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), derived.get());
}

// ===========================================================================
// FindTypeInBounds (TypeInference.cs lines 1033-1053, the public entry).
// ===========================================================================

// The public entry reduces the found types to the single found type.
TEST(TypeInferenceFixingBestCommonTypeTest, FindTypeInBoundsReducesToSingleFoundType)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    ITypePtr result = FindTypeInBounds(conversions, {int32, int64}, {},
                                        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_EQ(result.get(), int64.get());
}

// Empty bounds reduce to the UnknownType null object (the empty found list -> the
// picker's fallback).
TEST(TypeInferenceFixingBestCommonTypeTest, FindTypeInBoundsEmptyYieldsUnknownType)
{
    CSharpConversions conversions(Compilation());
    ITypePtr result = FindTypeInBounds(conversions, {}, {},
                                       Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_EQ(result->Kind(), TypeKind::Unknown);
}

// An inconvertible bound set yields the UnknownType null object (the empty candidate
// list).
TEST(TypeInferenceFixingBestCommonTypeTest, FindTypeInBoundsNoCandidateYieldsUnknownType)
{
    CSharpConversions conversions(Compilation());
    ITypePtr stringType = MakeRef(KnownTypeCode::String, "String");
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr result = FindTypeInBounds(conversions, {stringType, int32}, {},
                                       Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_EQ(result->Kind(), TypeKind::Unknown);
}

// The ImprovedReturnAllResults arm: a MULTI-candidate result returns the INTERSECTION
// of all candidates. With the Improved refinement live, the multi-candidate list comes
// from the REFINED candidates: two derived classes sharing TWO UNRELATED interfaces
// (I1, I2) -- the chains' intersection [I1, I2] survives the most-specific reduction
// because neither interface is derived from the other. (The pre-refinement
// two-distinct-int-instance shape no longer reaches this arm: the refinement rebuilds
// that candidate list from the int chains.)
TEST(TypeInferenceFixingBestCommonTypeTest, FindTypeInBoundsImprovedReturnAllResultsYieldsIntersection)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto i1 = MakeLocalRef(compilation, "I1", TypeKind::Interface);
    auto i2 = MakeLocalRef(compilation, "I2", TypeKind::Interface);
    auto derived1 = MakeLocalRef(compilation, "Derived1");
    auto derived2 = MakeLocalRef(compilation, "Derived2");
    derived1->AddDirectBaseType(i1);
    derived1->AddDirectBaseType(i2);
    derived2->AddDirectBaseType(i1);
    derived2->AddDirectBaseType(i2);

    ITypePtr result = FindTypeInBounds(conversions, { derived1, derived2 }, {},
                                       Res::TypeInferenceAlgorithm::ImprovedReturnAllResults);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Kind(), TypeKind::Intersection);
    auto* intersection = dynamic_cast<IntersectionType*>(result.get());
    ASSERT_NE(intersection, nullptr);
    ASSERT_EQ(intersection->Types().size(), 2u);
    EXPECT_EQ(intersection->Types()[0].get(), i1.get());
    EXPECT_EQ(intersection->Types()[1].get(), i2.get());
}

// The CSharp4 twin over the same bounds: the picker reduces the two candidates to the
// FIRST non-interface type -- the else arm the intersection arm diverges from. CSharp4
// never refines, so the two-distinct-int-instance shape still yields the multi-candidate
// SPEC list here (the improved algorithms reduce that shape through the refinement).
TEST(TypeInferenceFixingBestCommonTypeTest, FindTypeInBoundsCSharp4MultiCandidateYieldsFirstNonInterface)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32a = Def(KnownTypeCode::Int32);
    ITypePtr int32b = Def(KnownTypeCode::Int32);
    ITypePtr result = FindTypeInBounds(conversions, {int32a, int32b}, {},
                                       Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_EQ(result.get(), int32a.get());
}

// A singleton candidate list: the Create singleton mapping returns the type itself (the
// single-lower-bound early-out feeds the one-entry list; the arm coincides with the
// picker for this shape).
TEST(TypeInferenceFixingBestCommonTypeTest, FindTypeInBoundsImprovedReturnAllResultsSingleCandidate)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr result = FindTypeInBounds(conversions, {int32}, {},
                                       Res::TypeInferenceAlgorithm::ImprovedReturnAllResults);
    EXPECT_EQ(result.get(), int32.get());
}

// An empty candidate list: the Create empty mapping returns the UnknownType null
// object (the arm coincides with the picker's UnknownType fallback for this shape).
TEST(TypeInferenceFixingBestCommonTypeTest, FindTypeInBoundsImprovedReturnAllResultsEmptyYieldsUnknownType)
{
    CSharpConversions conversions(Compilation());
    ITypePtr result = FindTypeInBounds(conversions, {}, {},
                                       Res::TypeInferenceAlgorithm::ImprovedReturnAllResults);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Kind(), TypeKind::Unknown);
}

// ===========================================================================
// Fix (TypeInference.cs lines 967-994, spec 12.6.3.13 "Fixing").
// ===========================================================================

// An exact bound with no other bounds always wins and the fix succeeds.
TEST(TypeInferenceFixingBestCommonTypeTest, ExactBoundWinsWithoutOtherBounds)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    state[0].AddExactBound(int32);
    EXPECT_TRUE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                    /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), int32.get());
}

// Two different exact bounds raise MultipleDifferentExactBounds and the fix FAILS, but
// the FIRST exact bound still becomes FixedTo (the C# assigns before the check).
TEST(TypeInferenceFixingBestCommonTypeTest, MultipleDifferentExactBoundsAbortTheFix)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    state[0].AddExactBound(int32);
    state[0].AddExactBound(int64);
    EXPECT_FALSE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                     /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), int32.get());
}

// The exact bound's lower bounds must still convert TO the fixed type: derived -> object
// is an implicit reference conversion, so the fix succeeds.
TEST(TypeInferenceFixingBestCommonTypeTest, ExactBoundWithConvertibleLowerBoundSucceeds)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr derived = MakeRef(KnownTypeCode::None, "Derived");
    ITypePtr object = MakeRef(KnownTypeCode::Object, "Object");
    state[0].AddExactBound(object);
    state[0].AddLowerBound(derived);
    EXPECT_TRUE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                    /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), object.get());
}

// A lower bound that does NOT convert to the exact bound fails the fix (int -> string
// has no implicit conversion); the exact bound still becomes FixedTo.
TEST(TypeInferenceFixingBestCommonTypeTest, ExactBoundWithInconvertibleLowerBoundFails)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr stringType = MakeRef(KnownTypeCode::String, "String");
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    state[0].AddExactBound(stringType);
    state[0].AddLowerBound(int32);
    EXPECT_FALSE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                     /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), stringType.get());
}

// The exact bound's upper bounds must still be convertible FROM the fixed type: int ->
// long is an implicit numeric widening, so the fix succeeds.
TEST(TypeInferenceFixingBestCommonTypeTest, ExactBoundWithConvertibleUpperBoundSucceeds)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    state[0].AddExactBound(int32);
    state[0].AddUpperBound(int64);
    EXPECT_TRUE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                    /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), int32.get());
}

// An upper bound the fixed type does not convert to fails the fix (object -> int has no
// implicit conversion); the exact bound still becomes FixedTo.
TEST(TypeInferenceFixingBestCommonTypeTest, ExactBoundWithInconvertibleUpperBoundFails)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr object = MakeRef(KnownTypeCode::Object, "Object");
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    state[0].AddExactBound(object);
    state[0].AddUpperBound(int32);
    EXPECT_FALSE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                     /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), object.get());
}

// The non-exact path: the widening chain [int, long] fixes to long (the unique
// candidate) and succeeds.
TEST(TypeInferenceFixingBestCommonTypeTest, LowerBoundChainFixesToWidest)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    state[0].AddLowerBound(int32);
    state[0].AddLowerBound(int64);
    EXPECT_TRUE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                    /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), int64.get());
}

// An inconvertible bound set fixes to the UnknownType null object and FAILS (no unique
// candidate).
TEST(TypeInferenceFixingBestCommonTypeTest, UnrelatedLowerBoundsFixToUnknownAndFail)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr stringType = MakeRef(KnownTypeCode::String, "String");
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    state[0].AddLowerBound(stringType);
    state[0].AddLowerBound(int32);
    EXPECT_FALSE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                     /*nestingLevel*/ 0));
    ASSERT_NE(state[0].FixedTo, nullptr);
    EXPECT_EQ(state[0].FixedTo->Kind(), TypeKind::Unknown);
}

// A boundless type parameter fixes to the UnknownType null object and fails (the empty
// candidate list).
TEST(TypeInferenceFixingBestCommonTypeTest, NoBoundsFixToUnknownAndFail)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    EXPECT_FALSE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                     /*nestingLevel*/ 0));
    ASSERT_NE(state[0].FixedTo, nullptr);
    EXPECT_EQ(state[0].FixedTo->Kind(), TypeKind::Unknown);
}

// The DEFERRED ImprovedReturnAllResults arm is exact for a single candidate
// (IntersectionType.Create of a singleton is the single type itself; success
// types.Count >= 1) -- pinned against the CSharp4 result.
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedReturnAllResultsSingleCandidateIsExact)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    state[0].AddLowerBound(int32);
    state[0].AddLowerBound(int64);
    EXPECT_TRUE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::ImprovedReturnAllResults,
                    /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), int64.get());
}

// The DEFERRED ImprovedReturnAllResults arm is exact for an empty candidate list
// (IntersectionType.Create of an empty list is UnknownType; success 0 >= 1 is false).
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedReturnAllResultsNoCandidatesIsExact)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr stringType = MakeRef(KnownTypeCode::String, "String");
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    state[0].AddLowerBound(stringType);
    state[0].AddLowerBound(int32);
    EXPECT_FALSE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::ImprovedReturnAllResults,
                     /*nestingLevel*/ 0));
    ASSERT_NE(state[0].FixedTo, nullptr);
    EXPECT_EQ(state[0].FixedTo->Kind(), TypeKind::Unknown);
}

// The ImprovedReturnAllResults arm: a MULTI-candidate fix does not reduce to a single
// type -- the INTERSECTION of all candidates becomes the fixed type and any non-empty
// candidate list SUCCEEDS (`types.Count >= 1`). With the Improved refinement live the
// multi-candidate list comes from the REFINED candidates (the same two-derived-classes-
// sharing-two-unrelated-interfaces shape as the FindTypeInBounds intersection test --
// the chains' intersection [I1, I2] survives the most-specific reduction).
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedReturnAllResultsMultiCandidateFixesToIntersection)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    std::vector<TP> state = MakeState();
    auto i1 = MakeLocalRef(compilation, "I1", TypeKind::Interface);
    auto i2 = MakeLocalRef(compilation, "I2", TypeKind::Interface);
    auto derived1 = MakeLocalRef(compilation, "Derived1");
    auto derived2 = MakeLocalRef(compilation, "Derived2");
    derived1->AddDirectBaseType(i1);
    derived1->AddDirectBaseType(i2);
    derived2->AddDirectBaseType(i1);
    derived2->AddDirectBaseType(i2);
    state[0].AddLowerBound(derived1);
    state[0].AddLowerBound(derived2);
    EXPECT_TRUE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::ImprovedReturnAllResults,
                    /*nestingLevel*/ 0));
    ASSERT_NE(state[0].FixedTo, nullptr);
    EXPECT_EQ(state[0].FixedTo->Kind(), TypeKind::Intersection);
    auto* intersection = dynamic_cast<IntersectionType*>(state[0].FixedTo.get());
    ASSERT_NE(intersection, nullptr);
    ASSERT_EQ(intersection->Types().size(), 2u);
    EXPECT_EQ(intersection->Types()[0].get(), i1.get());
    EXPECT_EQ(intersection->Types()[1].get(), i2.get());
}

// The CSharp4 twin over the same bounds: the multi-candidate fix picks the FIRST
// non-interface candidate and FAILS (`types.Count == 1`) -- the else arm the
// intersection arm diverges from. CSharp4 never refines, so the two-distinct-int
// shape still yields the multi-candidate SPEC list here.
TEST(TypeInferenceFixingBestCommonTypeTest, CSharp4MultiCandidateFixesToFirstAndFails)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr int32a = Def(KnownTypeCode::Int32);
    ITypePtr int32b = Def(KnownTypeCode::Int32);
    state[0].AddLowerBound(int32a);
    state[0].AddLowerBound(int32b);
    EXPECT_FALSE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::CSharp4,
                     /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), int32a.get());
}

// An exact bound under ImprovedReturnAllResults takes the EXACT path (the intersection
// arm is the non-exact path only): the fixed type is the exact bound itself, not an
// intersection.
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedReturnAllResultsExactBoundBypassesTheIntersectionArm)
{
    CSharpConversions conversions(Compilation());
    std::vector<TP> state = MakeState();
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    state[0].AddExactBound(int32);
    EXPECT_TRUE(Fix(conversions, state[0], Res::TypeInferenceAlgorithm::ImprovedReturnAllResults,
                    /*nestingLevel*/ 0));
    EXPECT_EQ(state[0].FixedTo.get(), int32.get());
}

// ===========================================================================
// GetBestCommonType (TypeInference.cs lines 1001-1026, spec 12.6.3.17).
// ===========================================================================

// A single expression short-circuits to its own type (no inference runs at all).
TEST(TypeInferenceFixingBestCommonTypeTest, SingleExpressionReturnsItsOwnType)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = false;
    ITypePtr result = GetBestCommonType(Compilation(), conversions, {Expr(int32)}, success,
                                        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_TRUE(success);
    EXPECT_EQ(result.get(), int32.get());
}

// A single INVALID expression (the error-type null object) fails the IsValidType gate;
// its own type still returns.
TEST(TypeInferenceFixingBestCommonTypeTest, SingleInvalidExpressionFails)
{
    CSharpConversions conversions(Compilation());
    ITypePtr unknown = UnknownType();
    bool success = true;
    ITypePtr result = GetBestCommonType(Compilation(), conversions, {Expr(unknown)}, success,
                                        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_FALSE(success);
    EXPECT_EQ(result.get(), unknown.get());
}

// The flagship: int and long lower-bound the dummy TP and the fix picks long (the
// widening target).
TEST(TypeInferenceFixingBestCommonTypeTest, IntAndLongResolveToLong)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    bool success = false;
    ITypePtr result = GetBestCommonType(
        Compilation(), conversions, {Expr(int32), Expr(int64)}, success,
        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_TRUE(success);
    EXPECT_EQ(result.get(), int64.get());
}

// A three-element widening chain also fixes to the widest.
TEST(TypeInferenceFixingBestCommonTypeTest, ThreeWideningExpressionsResolveToWidest)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int16 = Def(KnownTypeCode::Int16);
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    ITypePtr int64 = Def(KnownTypeCode::Int64);
    bool success = false;
    ITypePtr result = GetBestCommonType(
        Compilation(), conversions, {Expr(int16), Expr(int32), Expr(int64)}, success,
        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_TRUE(success);
    EXPECT_EQ(result.get(), int64.get());
}

// Unrelated expressions yield no candidate: the fix fails and the UnknownType null
// object returns.
TEST(TypeInferenceFixingBestCommonTypeTest, UnrelatedExpressionsFailWithUnknownType)
{
    CSharpConversions conversions(Compilation());
    ITypePtr stringType = MakeRef(KnownTypeCode::String, "String");
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = true;
    ITypePtr result = GetBestCommonType(
        Compilation(), conversions, {Expr(stringType), Expr(int32)}, success,
        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_FALSE(success);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Kind(), TypeKind::Unknown);
}

// Two expressions of the SAME type dedup through the bounds' IType.Equals and fix to
// the shared instance.
TEST(TypeInferenceFixingBestCommonTypeTest, IdenticalExpressionsFixToSharedInstance)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = false;
    ITypePtr result = GetBestCommonType(
        Compilation(), conversions, {Expr(int32), Expr(int32)}, success,
        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_TRUE(success);
    EXPECT_EQ(result.get(), int32.get());
}

// A LAMBDA against the dummy contributes NOTHING: the dummy is not a delegate (or an
// expression tree over one), so the lambda arm finds no signature and falls through to
// the plain arm whose NoType fails the IsValidType gate -- only the int expression
// lower-bounds the dummy.
TEST(TypeInferenceFixingBestCommonTypeTest, LambdaAgainstNonDelegateDummyContributesNothing)
{
    CSharpConversions conversions(Compilation());
    ITypePtr int32 = Def(KnownTypeCode::Int32);
    bool success = false;
    ITypePtr result = GetBestCommonType(
        Compilation(), conversions, {std::make_shared<TestLambda>(), Expr(int32)}, success,
        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_TRUE(success);
    EXPECT_EQ(result.get(), int32.get());
}

// An empty expression set builds the boundless dummy TP: the fix fails and the
// UnknownType null object returns.
TEST(TypeInferenceFixingBestCommonTypeTest, EmptyExpressionsFailWithUnknownType)
{
    CSharpConversions conversions(Compilation());
    bool success = true;
    ITypePtr result = GetBestCommonType(Compilation(), conversions, {}, success,
                                        Res::TypeInferenceAlgorithm::CSharp4);
    EXPECT_FALSE(success);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Kind(), TypeKind::Unknown);
}

// ===========================================================================
// FindTypesInBounds -- the IMPROVED refinement (TypeInference.cs lines 1101-1176).
// The Improved algorithms run a second pass when the spec candidate-types algorithm
// yields no single candidate: the lower bounds' base-type-definition intersection (or
// the compilation-wide type scan when there are no lower bounds), the upper-bound
// IsDerivedFrom filter, the generic candidates' InferTypeArgumentsFromBounds
// construction, and the most-specific/least-specific redundancy reduction. Each test
// builds a FRESH local LookupCompilation (the scan tables must not leak across tests,
// and the candidates must be bound to the very compilation the local conversions
// carry -- the IsDerivedFrom same-compilation check).
// ===========================================================================

// The lower bounds' base-type-definition intersection finds the common base the spec
// algorithm misses: two derived classes share Base, but neither converts to the
// other, so the spec candidates are empty and the intersection supplies Base.
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedFindsCommonBaseViaLowerBoundsIntersection)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto base = MakeLocalRef(compilation, "Base");
    auto derived1 = MakeLocalRef(compilation, "Derived1");
    auto derived2 = MakeLocalRef(compilation, "Derived2");
    derived1->AddDirectBaseType(base);
    derived2->AddDirectBaseType(base);

    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, { derived1, derived2 }, {}, Res::TypeInferenceAlgorithm::Improved, 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), base.get());
}

// The most-specific reduction (upper bounds absent): a candidate made redundant by a
// more specific candidate is REMOVED -- the intersection [object, Base] reduces to
// [Base] because Base is derived from object (object was added first but does not
// survive Base's redundancy sweep).
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedMostSpecificRemovesRedundantBaseCandidate)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto object = MakeLocalRef(compilation, "Object");
    auto base = MakeLocalRef(compilation, "Base");
    auto derived1 = MakeLocalRef(compilation, "Derived1");
    auto derived2 = MakeLocalRef(compilation, "Derived2");
    base->AddDirectBaseType(object);
    derived1->AddDirectBaseType(base);
    derived2->AddDirectBaseType(base);

    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, { derived1, derived2 }, {}, Res::TypeInferenceAlgorithm::Improved, 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), base.get());
}

// The default CSharp4 algorithm does NOT run the refinement: the same shape returns
// the (empty) spec candidate list (the refinement is Improved-only -- the mode
// divergence pin).
TEST(TypeInferenceFixingBestCommonTypeTest, CSharp4DoesNotRunTheImprovedRefinement)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto base = MakeLocalRef(compilation, "Base");
    auto derived1 = MakeLocalRef(compilation, "Derived1");
    auto derived2 = MakeLocalRef(compilation, "Derived2");
    derived1->AddDirectBaseType(base);
    derived2->AddDirectBaseType(base);

    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, { derived1, derived2 }, {}, Res::TypeInferenceAlgorithm::CSharp4, 0);
    EXPECT_TRUE(result.empty());
}

// A GENERIC candidate constructs its closed form through InferTypeArgumentsFromBounds:
// the lower bounds G<Derived1>/G<Derived2> put G itself in the intersection (a
// parameterized type's own definition); G's covariant T accumulates [Derived1,
// Derived2] as lower bounds, the NESTED improved refinement fixes T to the shared
// Base, and the candidate list becomes [G<Base>] (GenericType and the inferred
// argument asserted by pointer identity).
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedGenericCandidateInfersTypeArguments)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto base = MakeLocalRef(compilation, "Base");
    auto derived1 = MakeLocalRef(compilation, "Derived1");
    auto derived2 = MakeLocalRef(compilation, "Derived2");
    derived1->AddDirectBaseType(base);
    derived2->AddDirectBaseType(base);
    auto t0 = std::make_shared<TS::TestSupport::LookupTypeParameter>(
        "T", TS::VarianceModifier::Covariant);
    t0->SetIndex(0);
    auto g = MakeLocalRef(compilation, "G", TypeKind::Class, /*typeParameterCount*/ 1);
    g->SetTypeParameters({ t0.get() });
    auto gOfDerived1 = std::make_shared<TS::ParameterizedType>(
        g, std::vector<ITypePtr>{ derived1 });
    auto gOfDerived2 = std::make_shared<TS::ParameterizedType>(
        g, std::vector<ITypePtr>{ derived2 });

    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, { gOfDerived1, gOfDerived2 }, {}, Res::TypeInferenceAlgorithm::Improved,
        0);
    ASSERT_EQ(result.size(), 1u);
    auto* parameterized = dynamic_cast<TS::ParameterizedType*>(result[0].get());
    ASSERT_NE(parameterized, nullptr);
    EXPECT_EQ(parameterized->GenericType().get(), g.get());
    ASSERT_EQ(parameterized->TypeArguments().size(), 1u);
    EXPECT_EQ(parameterized->TypeArguments()[0].get(), base.get());
}

// No lower bounds: the candidates come from the COMPILATION-WIDE scan (the modules'
// type tables), filtered by the upper bounds' IsDerivedFrom -- a class implementing
// both interfaces survives, an unrelated registered type is filtered out.
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedCompilationWideScanWithoutLowerBounds)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto i1 = MakeLocalRef(compilation, "I1", TypeKind::Interface);
    auto i2 = MakeLocalRef(compilation, "I2", TypeKind::Interface);
    auto c1 = MakeLocalRef(compilation, "C1");
    c1->AddDirectBaseType(i1);
    c1->AddDirectBaseType(i2);
    auto unrelated = MakeLocalRef(compilation, "Unrelated");
    compilation.AddTypeDefinition(c1.get());
    compilation.AddTypeDefinition(unrelated.get());

    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, {}, { i1, i2 }, Res::TypeInferenceAlgorithm::Improved, 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), c1.get());
}

// The LEAST-specific reduction (upper bounds present): a candidate made redundant by
// a LESS specific candidate is removed -- registering [Sub, Middle] (Sub : Middle :
// I1, I2) ends with [Middle] because Middle replaces the more specific Sub in the
// redundancy sweep.
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedLeastSpecificRemovesRedundantDerivedCandidate)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto i1 = MakeLocalRef(compilation, "I1", TypeKind::Interface);
    auto i2 = MakeLocalRef(compilation, "I2", TypeKind::Interface);
    auto middle = MakeLocalRef(compilation, "Middle");
    middle->AddDirectBaseType(i1);
    middle->AddDirectBaseType(i2);
    auto sub = MakeLocalRef(compilation, "Sub");
    sub->AddDirectBaseType(middle);
    compilation.AddTypeDefinition(sub.get());
    compilation.AddTypeDefinition(middle.get());

    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, {}, { i1, i2 }, Res::TypeInferenceAlgorithm::Improved, 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), middle.get());
}

// Two lower bounds sharing NO base type: the intersection is empty and even the
// improved refinement finds no candidate (the no-crash sentinel).
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedEmptyIntersectionYieldsEmpty)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto a = MakeLocalRef(compilation, "A");
    auto b = MakeLocalRef(compilation, "B");

    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, { a, b }, {}, Res::TypeInferenceAlgorithm::Improved, 0);
    EXPECT_TRUE(result.empty());
}

// The refinement fires for ImprovedReturnAllResults too (the early return admits BOTH
// improved algorithms); the candidate list is the same [Base] (the IntersectionType
// wrap of the multi-candidate result is FindTypeInBounds's concern, not this
// function's).
TEST(TypeInferenceFixingBestCommonTypeTest, ImprovedReturnAllResultsRunsTheRefinement)
{
    LookupCompilation compilation;
    CSharpConversions conversions(compilation);
    auto base = MakeLocalRef(compilation, "Base");
    auto derived1 = MakeLocalRef(compilation, "Derived1");
    auto derived2 = MakeLocalRef(compilation, "Derived2");
    derived1->AddDirectBaseType(base);
    derived2->AddDirectBaseType(base);

    std::vector<ITypePtr> result = FindTypesInBounds(
        conversions, { derived1, derived2 }, {},
        Res::TypeInferenceAlgorithm::ImprovedReturnAllResults, 0);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), base.get());
}
