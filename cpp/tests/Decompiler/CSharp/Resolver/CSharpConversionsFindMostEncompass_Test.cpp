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
// PURPOSE NONINFRINGEMENT IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` most-encompassed / most-encompassing type helpers
// (CSharpConversions.cs lines 971-991): `Detail::FindMostEncompassedType` and
// `Detail::FindMostEncompassingType`. These are the User-Defined Conversions region helpers that
// reduce a set of candidate types to the single most-encompassed (the one every other candidate
// converts to, the "smallest") or most-encompassing (the one every other candidate converts from,
// the "biggest") type. They delegate to the already-ported `IsEncompassedBy` (D524) which threads
// the compilation through `StandardImplicitConversion` (D523).
//
//   IType FindMostEncompassedType(IEnumerable<IType> candidates)
//       => best=null; foreach (current in candidates)
//             if (best==null || IsEncompassedBy(current, best)) best=current;
//             else if (!IsEncompassedBy(best, current)) return null; // Ambiguous
//          return best;
//
//   IType FindMostEncompassingType(IEnumerable<IType> candidates)
//       => best=null; foreach (current in candidates)
//             if (best==null || IsEncompassedBy(best, current)) best=current;
//             else if (!IsEncompassedBy(current, best)) return null; // Ambiguous
//          return best;
//
// The C# `IEnumerable<IType>` ports to `const std::vector<ITypePtr>&` (the ILSpy collection
// convention, `std::vector<ITypePtr>` as used by `DirectBaseTypes` / `TypeArguments`). The C#
// nullable `IType` return ports to `ITypePtr` (a null `shared_ptr<IType>` means empty/ambiguous).
//
// CRUX STUB CONVENTIONS (carried from the D514-D524 tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves
//    the `KnownTypeCode` via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`. Use this
//    (NOT `KnownType`) where `GetTypeCode` must resolve (the numeric/constant-expression helpers).
//  * `KnownType(ktc)` is NOT an `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful
//    stub for a plain value/reference primitive. `KnownType(Int32).IsReferenceType() == false`
//    (derived from Kind == Struct); `KnownType(String).IsReferenceType() == true` (Kind == Class).
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default is
//    `std::nullopt`, which fails the reference/boxing guards). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsKnownType(it, Object)` resolve on the type itself.
//  * `NullableOf(element)` is a `ParameterizedType` over the `System.Nullable`1` definition, so
//    `NullableType.GetUnderlyingType` strips it to the element.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::FindMostEncompassedType / FindMostEncompassingType
#include "Decompiler/Semantics/ConversionFactories.hpp"              // Conversion / Conversions (the IsValid fold)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::FindMostEncompassedType;
using ILSpy::Decompiler::CSharp::Resolver::Detail::FindMostEncompassingType;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface` IS a
// reference type -- the faithful value the reference-conversion guard needs; the base
// `LookupTypeDefinition` default is `std::nullopt`, which fails the guard). Inherits the
// `LookupTypeDefinition` ctor; the only override is `IsReferenceType`.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves the `KnownTypeCode`
// via the numeric cast (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Def(Int32)`
// reports `TypeCode::Int32`. The D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive integral definition as an `ITypePtr` (for dereferencing to the `const IType&`).
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    return MakeDef(ktc, kind);
}

// A `RefDef` with a configurable `KnownTypeCode` and `TypeKind`. A `KnownTypeCode::Object` makes
// `IsKnownType(it, Object)` resolve on the type itself (the `IsSubtypeOf` short-circuit).
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind) {
    int n = static_cast<int>(ktc);
    std::string name = "RT" + std::to_string(n) + "_" + std::to_string(static_cast<int>(kind));
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// The `System.Object` definition -- a reference type (`Class`) carrying `KnownTypeCode::Object`,
// so `IsKnownType(it, Object)` is true (the `IsSubtypeOf` short-circuit fires).
std::shared_ptr<RefDef> ObjectDef() {
    static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class);
    return d;
}

// The `System.Nullable`1` generic definition (a struct, `KnownTypeCode::NullableOfT`).
std::shared_ptr<LookupTypeDefinition> NullableDef() {
    return std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
        FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::NullableOfT);
}

// `Nullable<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `System.Nullable`1` definition). `NullableType.GetUnderlyingType` strips it to the element.
ITypePtr NullableOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

} // namespace

// ===========================================================================
// FindMostEncompassedType (CSharpConversions.cs line 971, spec 10.5.4).
//
// The most-encompassed type -- the candidate that is encompassed by every other candidate (the
// "smallest" in the implicit-conversion partial order). The running `best` is updated to
// `current` when `current` is encompassed by `best`; if neither encompasses the other, the set
// is ambiguous -> null. Empty candidates -> null (best stays null).
// ===========================================================================

// ---------------------------------------------------------------------------
// Empty candidates -> null (best stays null, the C# `IType best = null; return best;`).
// ---------------------------------------------------------------------------

// An empty candidate set has no most-encompassed type; `best` stays null and the loop body never
// executes, so `FindMostEncompassedType({}, ...)` returns a null `ITypePtr`.
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassedTypeReturnsNullForEmptyCandidates) {
    std::vector<ITypePtr> candidates;
    ITypePtr result = FindMostEncompassedType(Compilation(), candidates);
    EXPECT_EQ(result.get(), nullptr);
}

// ---------------------------------------------------------------------------
// Single candidate -> that candidate (best becomes the sole candidate on the first iteration).
// ---------------------------------------------------------------------------

// A single candidate is the most-encompassed type by default: `best == null` on the first
// iteration, so `best` becomes the sole candidate. The result is pointer-identical to the input.
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassedTypeReturnsTheSoleCandidate) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    std::vector<ITypePtr> candidates{intType};
    ITypePtr result = FindMostEncompassedType(Compilation(), candidates);
    EXPECT_EQ(result.get(), intType.get());
}

// ---------------------------------------------------------------------------
// [long, int] -> int (int is most-encompassed; int is encompassed by long via numeric widening).
// ---------------------------------------------------------------------------

// `[long, int]`: best=null, current=long -> best=long; current=int, IsEncompassedBy(int, long) is
// true (int->long is an implicit numeric widening), so best=int. The result is `int` -- the
// most-encompassed type (int is encompassed by long; every int value is a valid long).
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassedTypeReturnsIntForLongInt) {
    ITypePtr longType = Def(KnownTypeCode::Int64);
    ITypePtr intType = Def(KnownTypeCode::Int32);
    std::vector<ITypePtr> candidates{longType, intType};
    ITypePtr result = FindMostEncompassedType(Compilation(), candidates);
    EXPECT_EQ(result.get(), intType.get());
}

// ---------------------------------------------------------------------------
// [int, long] -> int (same result regardless of order; long is NOT encompassed by int, but int IS
// encompassed by long, so best stays int).
// ---------------------------------------------------------------------------

// `[int, long]`: best=null, current=int -> best=int; current=long, IsEncompassedBy(long, int) is
// false (no implicit narrowing), but IsEncompassedBy(int, long) is true, so the `else if` guard
// `!IsEncompassedBy(best, current)` is `!true` = false -- not ambiguous, best stays int. The
// result is `int` regardless of the candidate order (the most-encompassed type is order-independent).
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassedTypeReturnsIntForIntLong) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr longType = Def(KnownTypeCode::Int64);
    std::vector<ITypePtr> candidates{intType, longType};
    ITypePtr result = FindMostEncompassedType(Compilation(), candidates);
    EXPECT_EQ(result.get(), intType.get());
}

// ---------------------------------------------------------------------------
// [short, int, long] -> short (short is most-encompassed; short->int->long widening chain).
// ---------------------------------------------------------------------------

// `[short, int, long]`: best=short; current=int, IsEncompassedBy(int, short) false, but
// IsEncompassedBy(short, int) true -> best stays short; current=long, IsEncompassedBy(long, short)
// false, but IsEncompassedBy(short, long) true -> best stays short. The result is `short` -- the
// most-encompassed type at the bottom of the numeric widening chain (short is encompassed by both
// int and long).
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassedTypeReturnsShortForShortIntLong) {
    ITypePtr shortType = Def(KnownTypeCode::Int16);
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr longType = Def(KnownTypeCode::Int64);
    std::vector<ITypePtr> candidates{shortType, intType, longType};
    ITypePtr result = FindMostEncompassedType(Compilation(), candidates);
    EXPECT_EQ(result.get(), shortType.get());
}

// ---------------------------------------------------------------------------
// [int, Nullable<int>] -> int (int is most-encompassed; int is encompassed by Nullable<int> via
// the lifted-identity nullable conversion, but Nullable<int> is NOT encompassed by int).
// ---------------------------------------------------------------------------

// `[int, Nullable<int>]`: best=int; current=Nullable<int>, IsEncompassedBy(Nullable<int>, int) is
// false (no implicit conversion from Nullable<int> to int -- that is an explicit conversion),
// but IsEncompassedBy(int, Nullable<int>) is true (the lifted-identity nullable conversion), so
// the `else if` guard `!IsEncompassedBy(best, current)` is `!true` = false -- not ambiguous, best
// stays int. The result is `int` -- the most-encompassed type (int is encompassed by Nullable<int>).
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassedTypeReturnsIntForIntAndNullableInt) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intType);
    std::vector<ITypePtr> candidates{intType, nullableInt};
    ITypePtr result = FindMostEncompassedType(Compilation(), candidates);
    EXPECT_EQ(result.get(), intType.get());
}

// ---------------------------------------------------------------------------
// Ambiguous [string, int] -> null (neither encompasses the other; no partial-order relation).
// ---------------------------------------------------------------------------

// `[string, int]`: best=string; current=int, IsEncompassedBy(int, string) false (no implicit
// conversion), and IsEncompassedBy(string, int) false (no implicit conversion) -- so the `else if`
// guard `!IsEncompassedBy(best, current)` is `!false` = true -> AMBIGUOUS, return null. The two
// candidates are unrelated in the implicit-conversion partial order, so there is no single
// most-encompassed type.
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassedTypeReturnsNullForAmbiguousStringInt) {
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    ITypePtr intType = Def(KnownTypeCode::Int32);
    std::vector<ITypePtr> candidates{stringType, intType};
    ITypePtr result = FindMostEncompassedType(Compilation(), candidates);
    EXPECT_EQ(result.get(), nullptr);
}

// ---------------------------------------------------------------------------
// Ambiguous [int, long, string] -> null (the first two are related, but the third is unrelated).
// ---------------------------------------------------------------------------

// `[int, long, string]`: best=int; current=long, IsEncompassedBy(long, int) false, but
// IsEncompassedBy(int, long) true -> best stays int; current=string, IsEncompassedBy(string, int)
// false, and IsEncompassedBy(int, string) false -> AMBIGUOUS, return null. The first two candidates
// (int, long) are related by numeric widening, but the third (string) is unrelated to int, so the
// set is ambiguous. This pins that ambiguity is detected at the FIRST unrelated pair, not just the
// first two candidates.
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassedTypeReturnsNullWhenThirdCandidateIsUnrelated) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr longType = Def(KnownTypeCode::Int64);
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    std::vector<ITypePtr> candidates{intType, longType, stringType};
    ITypePtr result = FindMostEncompassedType(Compilation(), candidates);
    EXPECT_EQ(result.get(), nullptr);
}

// ===========================================================================
// FindMostEncompassingType (CSharpConversions.cs line 982, spec 10.5.4).
//
// The most-encompassing type -- the candidate that encompasses every other candidate (the
// "biggest" in the implicit-conversion partial order). The running `best` is updated to
// `current` when `best` is encompassed by `current`; if neither encompasses the other, the set
// is ambiguous -> null. Empty candidates -> null (best stays null).
// ===========================================================================

// ---------------------------------------------------------------------------
// Empty candidates -> null (best stays null).
// ---------------------------------------------------------------------------

// An empty candidate set has no most-encompassing type; `best` stays null and the loop body never
// executes, so `FindMostEncompassingType({}, ...)` returns a null `ITypePtr`.
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassingTypeReturnsNullForEmptyCandidates) {
    std::vector<ITypePtr> candidates;
    ITypePtr result = FindMostEncompassingType(Compilation(), candidates);
    EXPECT_EQ(result.get(), nullptr);
}

// ---------------------------------------------------------------------------
// Single candidate -> that candidate.
// ---------------------------------------------------------------------------

// A single candidate is the most-encompassing type by default: `best == null` on the first
// iteration, so `best` becomes the sole candidate. The result is pointer-identical to the input.
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassingTypeReturnsTheSoleCandidate) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    std::vector<ITypePtr> candidates{intType};
    ITypePtr result = FindMostEncompassingType(Compilation(), candidates);
    EXPECT_EQ(result.get(), intType.get());
}

// ---------------------------------------------------------------------------
// [int, long] -> long (long is most-encompassing; int is encompassed by long, so best becomes
// long).
// ---------------------------------------------------------------------------

// `[int, long]`: best=null, current=int -> best=int; current=long, IsEncompassedBy(int, long) is
// true (best is encompassed by current), so best=long. The result is `long` -- the most-encompassing
// type (long encompasses int; every int value is a valid long).
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassingTypeReturnsLongForIntLong) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr longType = Def(KnownTypeCode::Int64);
    std::vector<ITypePtr> candidates{intType, longType};
    ITypePtr result = FindMostEncompassingType(Compilation(), candidates);
    EXPECT_EQ(result.get(), longType.get());
}

// ---------------------------------------------------------------------------
// [long, int] -> long (same result regardless of order; int is encompassed by long but long is
// NOT encompassed by int, so best stays long).
// ---------------------------------------------------------------------------

// `[long, int]`: best=null, current=long -> best=long; current=int, IsEncompassedBy(long, int) is
// false (best is NOT encompassed by current), but IsEncompassedBy(int, long) is true, so the `else
// if` guard `!IsEncompassedBy(current, best)` is `!true` = false -- not ambiguous, best stays long.
// The result is `long` regardless of the candidate order.
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassingTypeReturnsLongForLongInt) {
    ITypePtr longType = Def(KnownTypeCode::Int64);
    ITypePtr intType = Def(KnownTypeCode::Int32);
    std::vector<ITypePtr> candidates{longType, intType};
    ITypePtr result = FindMostEncompassingType(Compilation(), candidates);
    EXPECT_EQ(result.get(), longType.get());
}

// ---------------------------------------------------------------------------
// [short, int, long] -> long (long is most-encompassing; short->int->long widening chain).
// ---------------------------------------------------------------------------

// `[short, int, long]`: best=short; current=int, IsEncompassedBy(short, int) true -> best=int;
// current=long, IsEncompassedBy(int, long) true -> best=long. The result is `long` -- the
// most-encompassing type at the top of the numeric widening chain (long encompasses both short and
// int).
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassingTypeReturnsLongForShortIntLong) {
    ITypePtr shortType = Def(KnownTypeCode::Int16);
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr longType = Def(KnownTypeCode::Int64);
    std::vector<ITypePtr> candidates{shortType, intType, longType};
    ITypePtr result = FindMostEncompassingType(Compilation(), candidates);
    EXPECT_EQ(result.get(), longType.get());
}

// ---------------------------------------------------------------------------
// [int, Nullable<int>] -> Nullable<int> (Nullable<int> is most-encompassing; int is encompassed
// by Nullable<int> via the lifted-identity nullable conversion).
// ---------------------------------------------------------------------------

// `[int, Nullable<int>]`: best=int; current=Nullable<int>, IsEncompassedBy(int, Nullable<int>) is
// true (best is encompassed by current -- the lifted-identity nullable conversion), so
// best=Nullable<int>. The result is `Nullable<int>` -- the most-encompassing type (Nullable<int>
// encompasses int). This is the crux case distinguishing `FindMostEncompassingType` from
// `FindMostEncompassedType` on the same candidate set: the most-encompassed is `int`, the
// most-encompassing is `Nullable<int>` (the nullable wrapper is "bigger" than its underlying).
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassingTypeReturnsNullableIntForIntAndNullableInt) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    ITypePtr nullableInt = NullableOf(intType);
    std::vector<ITypePtr> candidates{intType, nullableInt};
    ITypePtr result = FindMostEncompassingType(Compilation(), candidates);
    EXPECT_EQ(result.get(), nullableInt.get());
}

// ---------------------------------------------------------------------------
// Ambiguous [string, int] -> null (neither encompasses the other).
// ---------------------------------------------------------------------------

// `[string, int]`: best=string; current=int, IsEncompassedBy(string, int) false (best is NOT
// encompassed by current), and IsEncompassedBy(int, string) false (current is NOT encompassed by
// best) -> AMBIGUOUS, return null. The two candidates are unrelated in the implicit-conversion
// partial order, so there is no single most-encompassing type.
TEST(CSharpConversionsFindMostEncompassTest, FindMostEncompassingTypeReturnsNullForAmbiguousStringInt) {
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    ITypePtr intType = Def(KnownTypeCode::Int32);
    std::vector<ITypePtr> candidates{stringType, intType};
    ITypePtr result = FindMostEncompassingType(Compilation(), candidates);
    EXPECT_EQ(result.get(), nullptr);
}
