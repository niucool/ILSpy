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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions.SelectOperator` helper (CSharpConversions.cs line 993) plus the
// `OperatorInfo` data holder (CSharpConversions.cs lines 1131-1148) -- the user-defined-conversion
// operator selection. From the applicable operators, `SelectOperator` selects the ones whose
// `SourceType` equals `mostSpecificSource` AND whose `TargetType` equals `mostSpecificTarget`:
//
//   var selected = operators.Where(op => op.SourceType.Equals(mostSpecificSource)
//                                     && op.TargetType.Equals(mostSpecificTarget)).ToList();
//   if (selected.Count == 0) return Conversion.None;
//   if (selected.Count == 1) return Conversion.UserDefinedConversion(selected[0].Method, ...);
//   int nNonLifted = selected.Count(s => !s.IsLifted);
//   if (nNonLifted == 1) return Conversion.UserDefinedConversion(nonLifted.Method, ...);
//   return Conversion.UserDefinedConversion(selected[0].Method, isAmbiguous: true, ...);
//
// The before/after conversions the `UserDefinedConversion` carries are
// `ExplicitConversionNotUserDefined(source, mostSpecificSource)` and
// `ExplicitConversionNotUserDefined(mostSpecificTarget, target)` -- the already-ported D525 helper.
// `SelectOperator` threads the compilation through to it.
//
// CRUX STUB CONVENTIONS (carried from the D514-D525 tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `LookupTypeDefinition`
//    `StructuralEquals` is IDENTITY equality (`this == &other`), so the filter
//    `op.SourceType->Equals(mostSpecificSource)` is true ONLY when `op.SourceType` and
//    `mostSpecificSource` are the SAME instance (a shared_ptr to the same `LookupTypeDefinition`).
//    The test reuses the same `ITypePtr` for the operator's source/target and the most-specific
//    source/target so the filter matches.
//  * `LookupMethod(name, compilation)` is the `IMethod` stub. `SelectOperator` only reads
//    `op.Method` (the `const IMethod*` handle) -- it passes it to `Conversions::UserDefinedConversion`
//    which asserts non-null. The stubs are kept alive in a static vector (the `AddMembers_Test`
//    precedent) so the raw pointer the `OperatorInfo` holds outlives the `SelectOperator` call.
//  * The before/after conversion crux: `source = int (Def Int32)`, `mostSpecificSource = long
//    (Def Int64)` -> `ExplicitConversionNotUserDefined(int, long)` returns
//    `ImplicitNumericConversion` (the implicit check fires first for a widening); and
//    `mostSpecificTarget = long (Def Int64)`, `target = int (Def Int32)` ->
//    `ExplicitConversionNotUserDefined(long, int)` returns `ExplicitNumericConversion` (no implicit
//    narrowing, the explicit dispatch's numeric arm fires). These two assert the before/after
//    conversions are computed via the already-ported helper, not a fixed `None`.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::SelectOperator, Detail::OperatorInfo
#include "Decompiler/Semantics/ConversionFactories.hpp"              // Conversion / Conversions (the UserDefinedConversion factory / None singleton)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::OperatorInfo;
using ILSpy::Decompiler::CSharp::Resolver::Detail::SelectOperator;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves the `KnownTypeCode`
// via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`. `LookupTypeDefinition`
// `StructuralEquals` is IDENTITY equality, so the `SelectOperator` filter matches only on the
// SAME instance -- the test reuses the returned `ITypePtr` for both the operator's source/target
// and the most-specific source/target. The D514 `MakeDef` precedent.
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

// A `LookupMethod` kept alive in a static vector (the `AddMembers_Test` precedent) so the raw
// `const IMethod*` the `OperatorInfo` holds outlives the `SelectOperator` call. The name carries
// the operator kind (`op_Implicit` / `op_Explicit`); `SelectOperator` only reads the method pointer
// (it does not inspect the method's parameters/return type -- those live on the `OperatorInfo`).
const IMethod* MakeMethod(std::string name) {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

// Builds an `OperatorInfo` over the supplied method handle, source/target types, and lifted flag.
// The source/target `ITypePtr`s are shared with the caller so the `SelectOperator` filter (identity
// `Equals`) matches when the caller reuses the same `ITypePtr` for the most-specific source/target.
OperatorInfo MakeOp(const IMethod* method, ITypePtr sourceType, ITypePtr targetType, bool isLifted) {
    return OperatorInfo(method, std::move(sourceType), std::move(targetType), isLifted);
}

} // namespace

// ===========================================================================
// SelectOperator (CSharpConversions.cs line 993) + OperatorInfo (lines 1131-1148).
// ===========================================================================

// An empty operators list yields no match -> None.
TEST(CSharpConversionsSelectOperatorTest, ReturnsNoneForEmptyOperators) {
    ITypePtr source = Def(KnownTypeCode::Int32);
    ITypePtr target = Def(KnownTypeCode::Int64);
    std::vector<OperatorInfo> operators;
    auto c = SelectOperator(Compilation(), *source, *target, operators, true, *source, *target);
    EXPECT_EQ(c.get(), Conversions::None().get());
}

// No operator's source/target matches the most-specific source/target -> None. The operator's
// source is `int` but the most-specific source is `long` (distinct instances -> identity `Equals`
// false), so the filter rejects it.
TEST(CSharpConversionsSelectOperatorTest, ReturnsNoneWhenNoOperatorMatches) {
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr opSource = Def(KnownTypeCode::Int32);   // distinct instance, != mostSpecificSource
    ITypePtr opTarget = mostSpecificTarget;          // matches
    const IMethod* method = MakeMethod("op_Implicit");
    std::vector<OperatorInfo> operators = {MakeOp(method, opSource, opTarget, false)};
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            true, *mostSpecificSource, *mostSpecificTarget);
    EXPECT_EQ(c.get(), Conversions::None().get());
}

// The filter rejects on a target-type mismatch too (source matches, target does not).
TEST(CSharpConversionsSelectOperatorTest, ReturnsNoneWhenTargetTypeDoesNotMatch) {
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int32);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr opSource = mostSpecificSource;          // matches
    ITypePtr opTarget = Def(KnownTypeCode::Int32);    // distinct, != mostSpecificTarget
    const IMethod* method = MakeMethod("op_Implicit");
    std::vector<OperatorInfo> operators = {MakeOp(method, opSource, opTarget, false)};
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            true, *mostSpecificSource, *mostSpecificTarget);
    EXPECT_EQ(c.get(), Conversions::None().get());
}

// A single matching operator returns a `UserDefinedConversion` over that operator's method with
// the `isImplicit` / `isLifted` flags propagated. `isImplicit: true` -> `IsImplicit` true,
// `IsExplicit` false; `IsValid` true (not ambiguous).
TEST(CSharpConversionsSelectOperatorTest, ReturnsUserDefinedForSingleMatch) {
    ITypePtr source = Def(KnownTypeCode::Int32);
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr target = Def(KnownTypeCode::Int32);
    const IMethod* method = MakeMethod("op_Implicit");
    std::vector<OperatorInfo> operators = {MakeOp(method, mostSpecificSource, mostSpecificTarget, false)};
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            /*isImplicit*/ true, *source, *target);
    EXPECT_TRUE(c->IsUserDefined());
    EXPECT_EQ(c->Method(), method);
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_FALSE(c->IsExplicit());
    EXPECT_FALSE(c->IsLifted());
    EXPECT_TRUE(c->IsValid());
}

// `isImplicit: false` (an explicit user-defined conversion) -> `IsExplicit` true, `IsImplicit`
// false.
TEST(CSharpConversionsSelectOperatorTest, PropagatesIsImplicitFalse) {
    ITypePtr source = Def(KnownTypeCode::Int32);
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr target = Def(KnownTypeCode::Int32);
    const IMethod* method = MakeMethod("op_Explicit");
    std::vector<OperatorInfo> operators = {MakeOp(method, mostSpecificSource, mostSpecificTarget, false)};
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            /*isImplicit*/ false, *source, *target);
    EXPECT_TRUE(c->IsExplicit());
    EXPECT_FALSE(c->IsImplicit());
    EXPECT_EQ(c->Method(), method);
}

// The `IsLifted` flag on the matching operator propagates to the returned conversion.
TEST(CSharpConversionsSelectOperatorTest, PropagatesIsLiftedFlag) {
    ITypePtr source = Def(KnownTypeCode::Int32);
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr target = Def(KnownTypeCode::Int32);
    const IMethod* method = MakeMethod("op_Implicit");
    std::vector<OperatorInfo> operators = {MakeOp(method, mostSpecificSource, mostSpecificTarget, /*isLifted*/ true)};
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            true, *source, *target);
    EXPECT_TRUE(c->IsLifted());
    EXPECT_EQ(c->Method(), method);
}

// The before/after conversions are computed via the already-ported `ExplicitConversionNotUserDefined`
// (D525): `before = ExplicitConversionNotUserDefined(source, mostSpecificSource)` and
// `after = ExplicitConversionNotUserDefined(mostSpecificTarget, target)`. With `source = int`,
// `mostSpecificSource = long` (an implicit widening -> `ImplicitNumericConversion`), and
// `mostSpecificTarget = long`, `target = int` (no implicit narrowing -> `ExplicitNumericConversion`),
// the crux asserts the two singletons by pointer-identity. This pins that `SelectOperator`
// delegates to the helper rather than carrying a fixed `None`.
TEST(CSharpConversionsSelectOperatorTest, BeforeAndAfterConversionsAreExplicitConversionNotUserDefinedResults) {
    ITypePtr source = Def(KnownTypeCode::Int32);             // int
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64); // long (operator's source)
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64); // long (operator's target)
    ITypePtr target = Def(KnownTypeCode::Int32);             // int
    const IMethod* method = MakeMethod("op_Implicit");
    std::vector<OperatorInfo> operators = {MakeOp(method, mostSpecificSource, mostSpecificTarget, false)};
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            true, *source, *target);
    EXPECT_EQ(c->ConversionBeforeUserDefinedOperator().get(),
              Conversions::ImplicitNumericConversion().get());
    EXPECT_EQ(c->ConversionAfterUserDefinedOperator().get(),
              Conversions::ExplicitNumericConversion().get());
}

// When more than one operator matches and exactly one is non-lifted, the non-lifted operator is
// preferred over the lifted forms (the C# `if (nNonLifted == 1)` arm). The returned conversion's
// method is the non-lifted operator's method, and `IsLifted` is false.
TEST(CSharpConversionsSelectOperatorTest, PrefersNonLiftedWhenExactlyOneNonLiftedAmongMatches) {
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr source = Def(KnownTypeCode::Int32);
    ITypePtr target = Def(KnownTypeCode::Int32);
    const IMethod* nonLiftedMethod = MakeMethod("op_Implicit_nonLifted");
    const IMethod* liftedMethod = MakeMethod("op_Implicit_lifted");
    std::vector<OperatorInfo> operators = {
        MakeOp(nonLiftedMethod, mostSpecificSource, mostSpecificTarget, /*isLifted*/ false),
        MakeOp(liftedMethod, mostSpecificSource, mostSpecificTarget, /*isLifted*/ true),
    };
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            true, *source, *target);
    EXPECT_EQ(c->Method(), nonLiftedMethod);
    EXPECT_FALSE(c->IsLifted());
    EXPECT_TRUE(c->IsValid());   // not ambiguous
}

// When every matching operator is lifted (zero non-lifted), the result is ambiguous: the returned
// conversion carries `isAmbiguous: true` (`IsValid` false) and uses `selected[0]`'s method. The
// lifted flag is `selected[0].IsLifted` (true here).
TEST(CSharpConversionsSelectOperatorTest, AmbiguousWhenAllMatchesLifted) {
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr source = Def(KnownTypeCode::Int32);
    ITypePtr target = Def(KnownTypeCode::Int32);
    const IMethod* method0 = MakeMethod("op_Implicit_lifted0");
    const IMethod* method1 = MakeMethod("op_Implicit_lifted1");
    std::vector<OperatorInfo> operators = {
        MakeOp(method0, mostSpecificSource, mostSpecificTarget, /*isLifted*/ true),
        MakeOp(method1, mostSpecificSource, mostSpecificTarget, /*isLifted*/ true),
    };
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            true, *source, *target);
    EXPECT_FALSE(c->IsValid());   // ambiguous
    EXPECT_EQ(c->Method(), method0);   // selected[0]
    EXPECT_TRUE(c->IsLifted());        // selected[0].IsLifted
}

// When more than one matching operator is non-lifted (`nNonLifted > 1`), the result is ambiguous:
// `IsValid` false, uses `selected[0]`'s method.
TEST(CSharpConversionsSelectOperatorTest, AmbiguousWhenMultipleNonLifted) {
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr source = Def(KnownTypeCode::Int32);
    ITypePtr target = Def(KnownTypeCode::Int32);
    const IMethod* method0 = MakeMethod("op_Implicit_nonLifted0");
    const IMethod* method1 = MakeMethod("op_Implicit_nonLifted1");
    const IMethod* method2 = MakeMethod("op_Implicit_lifted");
    std::vector<OperatorInfo> operators = {
        MakeOp(method0, mostSpecificSource, mostSpecificTarget, /*isLifted*/ false),
        MakeOp(method1, mostSpecificSource, mostSpecificTarget, /*isLifted*/ false),
        MakeOp(method2, mostSpecificSource, mostSpecificTarget, /*isLifted*/ true),
    };
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            true, *source, *target);
    EXPECT_FALSE(c->IsValid());   // ambiguous (nNonLifted == 2)
    EXPECT_EQ(c->Method(), method0);   // selected[0]
}

// Operators that do NOT match the most-specific source/target are skipped: a non-matching operator
// ahead of a matching one does not affect the single-match return. The filter scans the whole list.
TEST(CSharpConversionsSelectOperatorTest, SkipsNonMatchingOperatorsAndReturnsTheMatch) {
    ITypePtr mostSpecificSource = Def(KnownTypeCode::Int64);
    ITypePtr mostSpecificTarget = Def(KnownTypeCode::Int64);
    ITypePtr source = Def(KnownTypeCode::Int32);
    ITypePtr target = Def(KnownTypeCode::Int32);
    ITypePtr otherSource = Def(KnownTypeCode::Int32);   // distinct, won't match mostSpecificSource
    const IMethod* nonMatching = MakeMethod("op_Implicit_other");
    const IMethod* matching = MakeMethod("op_Implicit_match");
    std::vector<OperatorInfo> operators = {
        MakeOp(nonMatching, otherSource, mostSpecificTarget, false),  // source mismatch
        MakeOp(matching, mostSpecificSource, mostSpecificTarget, false),  // matches
    };
    auto c = SelectOperator(Compilation(), *mostSpecificSource, *mostSpecificTarget, operators,
                            true, *source, *target);
    EXPECT_EQ(c->Method(), matching);
    EXPECT_TRUE(c->IsValid());
}
