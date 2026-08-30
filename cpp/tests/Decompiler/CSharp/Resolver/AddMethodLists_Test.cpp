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
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `OverloadResolution`'s `AddMethodLists` engine entry (OverloadResolution.cs lines
// 330-377) -- "the logic that causes applicable methods in derived types to hide all methods in
// base types": base types come FIRST in the list, the walk goes BACKWARDS (derived types
// first), every candidate of each list is added through `AddCandidate` (additionalErrors None),
// and once a list produced an APPLICABLE candidate, every earlier (more-base) list whose
// `DeclaringType` is a base type of the current list's declaring type (the `GetAllBaseTypes`
// closure) is marked hidden and skipped. Ported as `Detail::AddMethodLists` (the D575
// instance-state-threading lift) plus the public `OverloadResolution::AddMethodLists` method
// delegating to it, with the first output properties (`BestCandidate` /
// `BestCandidateAmbiguousWith` / `FoundApplicableCandidate` / `IsAmbiguous`) as the public
// observables.
//
// The load-bearing cruxes:
//  (a) the DERIVED-TYPE HIDING -- `[Base { M(int) }, Derived { M(int) }]` with `Derived : Base`
//      and an int argument: both methods are applicable with IDENTICAL signatures, so if BOTH
//      were folded the `BetterFunctionMember` tiebreaks would all return 0 (ambiguous); the
//      hiding keeps the Base list out, and the observable is `bestCandidateAmbiguousWith ==
//      nullptr` with the derived method as the sole best;
//  (b) the HIDING IS DERIVATION-GATED -- the same two lists with UNRELATED declaring types
//      process BOTH lists, and the identical signatures DO tie 0/0: the Base method ends up in
//      `bestCandidateAmbiguousWith` (the sentinel proving (a) is the hiding, not a tiebreak);
//  (c) the APPLICABLE-GATE -- a derived list whose candidates are all INAPPLICABLE (or empty)
//      does NOT hide the base list: the base methods are still added, and the applicable base
//      candidate is promoted over the inapplicable derived one;
//  (d) the TRANSITIVE closure -- a derived list hides both the direct-base list and the
//      transitive-base list (`Derived : Middle : Base`);
//  (e) the public delegation -- the `OverloadResolution::AddMethodLists` method threads the
//      instance fields, resolves the conversions lazily (`CSharpConversions::Get`), and the
//      output properties observe the folded state.
//
// The stubs mirror the AddCandidate_Test conventions (`TestMethod`/`TestParameter`, the
// `MakeDef` primitive definitions, one SHARED int instance across both methods' parameter
// types and the argument -- the real compilation's type-cache model, required for the
// applicability identity conversion AND the `parameterTypesEqual` gate in the ambiguous
// sentinel; the local `CSharpConversions` per call avoids the `CSharpConversions::Get`
// shared-instance cache-dangling caveat, which the public delegation tests avoid with the
// non-generic non-params shape).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::AddMethodLists;
using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// The shared compilation (a plain `LookupCompilation` -- none of the shapes tested here
// needs a `FindType` registration; the conversions resolve over the stubs' own structural
// equality).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A primitive definition (`LookupTypeDefinition`, IS-A `ITypeDefinition`) with the given
// `KnownTypeCode` / `TypeKind` -- the D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A named class definition for a method-list bucket's `DeclaringType`, with optional direct
// base types (the `LookupTypeDefinition::AddDirectBaseType` graph). The SAME shared instance
// must be used as the bucket's `DeclaringType` and in the derived type's base list -- the
// stub's `StructuralEquals` is IDENTITY equality, and the hiding check is
// `baseType.Equals(methodLists[j].DeclaringType())` (the D517 same-instance precedent).
std::shared_ptr<LookupTypeDefinition> MakeTypeDef(std::string name,
                                                   std::initializer_list<ITypePtr> bases = {}) {
    auto type = std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr, KnownTypeCode::None);
    for (const ITypePtr& base : bases)
        type->AddDirectBaseType(base);
    return type;
}

// A minimal `IParameter` over a configured type with configurable `IsParams` (the
// AddCandidate_Test `TestParameter` precedent).
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, std::string name = "p", bool isParams = false)
        : name_(std::move(name)), type_(std::move(type)), isParams_(isParams) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const IParameterizedMember* Owner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    bool isParams_;
};

// A `LookupMethod` with configurable `Parameters` (the AddCandidate_Test `TestMethod`
// precedent; the stub IS its own member definition).
class TestMethod : public LookupMethod {
public:
    explicit TestMethod(std::string name = "M")
        : LookupMethod(std::move(name), Compilation()) {}
};

// A plain `ResolveResult` over the given type (the base class is concrete).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// The best-candidate state threaded through the calls (the `OverloadResolution` instance
// fields `bestCandidate`/`bestCandidateWasValidated`/`bestCandidateAmbiguousWith`).
struct BestState {
    std::shared_ptr<OverloadResolutionCandidate> best;
    bool wasValidated = false;
    std::shared_ptr<OverloadResolutionCandidate> ambiguousWith;
};

// Runs `Detail::AddMethodLists` with the C# `OverloadResolution` input-property defaults
// (`AllowExpandingParams`/`AllowOptionalParameters`/`AllowImplicitIn` true,
// `IsExtensionMethodInvocation` false) and the all-positional argument names (the ctor's
// `null` normalization), over the given best state. A LOCAL `CSharpConversions` per call
// (its cache dies with the call; the `CSharpConversions::Get` shared instance is only
// exercised by the public-method delegation tests, which use the non-generic non-params
// shape that never populates its cached `ImplicitConversion(IType, IType)`).
void RunAddLists(const std::vector<MethodListWithDeclaringType>& methodLists,
                 const std::vector<std::shared_ptr<ResolveResult>>& arguments,
                 BestState& state) {
    CSharpConversions conversions(Compilation());
    std::vector<std::string> names(arguments.size());
    AddMethodLists(methodLists, Compilation(), conversions, arguments, names, std::nullopt,
                   /*allowExpandingParams*/true, /*allowOptionalParameters*/true,
                   /*allowImplicitIn*/true, /*isExtensionMethodInvocation*/false,
                   state.best, state.wasValidated, state.ambiguousWith);
}

// Builds a one-method bucket over the given declaring type.
MethodListWithDeclaringType Bucket(const ITypePtr& declaringType,
                                   std::initializer_list<const IParameterizedMember*> methods) {
    return MethodListWithDeclaringType(declaringType, methods);
}

// ---- The degenerate shapes ----

// An empty method-lists vector is a no-op: no candidate is added, the best state stays empty
// (the C# `ArgumentNullException` on a null list compiles out -- the reference is non-null).
TEST(AddMethodListsTest, EmptyMethodListsLeavesBestStateUnchanged)
{
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments;
    RunAddLists({}, arguments, state);
    EXPECT_EQ(state.best, nullptr);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// A single list is processed normally -- the `isHiddenByDerivedType` array is only allocated
// for more than one list (the C# keeps it null for `Count <= 1`).
TEST(AddMethodListsTest, SingleListFoldsItsCandidates)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    RunAddLists({Bucket(intType, {&method})}, arguments, state);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &method);
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// ---- The derived-type hiding (the crux) ----

// `[Base { M(int) }, Derived { M(int) }]` with `Derived : Base` and an int argument: the
// derived list is processed first and produces an APPLICABLE candidate, so the Base list
// (whose declaring type is a base type of Derived) is marked hidden and SKIPPED -- only the
// derived method is folded. The observable: with identical signatures both-folded would tie
// 0/0 in `BetterFunctionMember` (ambiguous); the hidden list keeps `ambiguousWith == nullptr`.
TEST(AddMethodListsTest, ApplicableDerivedHidesBaseList)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto base = MakeTypeDef("Base");
    auto derived = MakeTypeDef("Derived", {base});
    TestParameter param(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&param});
    TestMethod derivedMethod("M");
    derivedMethod.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    RunAddLists({Bucket(base, {&baseMethod}), Bucket(derived, {&derivedMethod})}, arguments, state);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &derivedMethod);
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// The sentinel proving the hiding is derivation-gated: the same two lists with UNRELATED
// declaring types (Derived does NOT derive from Base) process BOTH lists, and the identical
// signatures tie 0/0 in `BetterFunctionMember` -- the Base method ends up in
// `bestCandidateAmbiguousWith` (overwritten on every ambiguous fold, the last one wins).
TEST(AddMethodListsTest, UnrelatedDeclaringTypesBothProcessed)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto base = MakeTypeDef("Base");
    auto derived = MakeTypeDef("Derived");  // NO derivation -- both lists are processed.
    TestParameter param(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&param});
    TestMethod derivedMethod("M");
    derivedMethod.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    RunAddLists({Bucket(base, {&baseMethod}), Bucket(derived, {&derivedMethod})}, arguments, state);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &derivedMethod);
    ASSERT_NE(state.ambiguousWith, nullptr);
    EXPECT_EQ(state.ambiguousWith->Member(), &baseMethod);
}

// ---- The applicable-gate ----

// A derived list whose only candidate is INAPPLICABLE (`M(string)` with an int argument)
// does NOT hide the base list: the base method is added too, and the applicable base
// candidate is promoted over the inapplicable derived one (the prefer-applicable
// heuristic in `BetterFunctionMember`).
TEST(AddMethodListsTest, InapplicableDerivedListDoesNotHideBase)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto strType = MakeDef(KnownTypeCode::String, TypeKind::Class);
    auto base = MakeTypeDef("Base");
    auto derived = MakeTypeDef("Derived", {base});
    TestParameter intParam(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&intParam});
    TestParameter strParam(strType, "y");
    TestMethod derivedMethod("M");
    derivedMethod.SetParameters({&strParam});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    RunAddLists({Bucket(base, {&baseMethod}), Bucket(derived, {&derivedMethod})}, arguments, state);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &baseMethod);
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// An EMPTY derived list produces no candidate (foundApplicable stays false) and does not
// hide the base list either.
TEST(AddMethodListsTest, EmptyDerivedListDoesNotHideBase)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto base = MakeTypeDef("Base");
    auto derived = MakeTypeDef("Derived", {base});
    TestParameter param(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    RunAddLists({Bucket(base, {&baseMethod}), Bucket(derived, {})}, arguments, state);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &baseMethod);
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::None);
}

// A derived list with MIXED applicability (`M(string)` inapplicable + `M2(int) applicable`)
// has an applicable candidate, so the base list IS hidden -- the base method is never folded
// (with the identical `M(int)` signature, folding it would have made the resolution
// ambiguous).
TEST(AddMethodListsTest, MixedApplicabilityInDerivedListHidesBase)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto strType = MakeDef(KnownTypeCode::String, TypeKind::Class);
    auto base = MakeTypeDef("Base");
    auto derived = MakeTypeDef("Derived", {base});
    TestParameter intParam(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&intParam});
    TestParameter strParam(strType, "y");
    TestMethod derivedInapplicable("M");
    derivedInapplicable.SetParameters({&strParam});
    TestMethod derivedApplicable("M2");
    derivedApplicable.SetParameters({&intParam});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    RunAddLists({Bucket(base, {&baseMethod}),
                 Bucket(derived, {&derivedInapplicable, &derivedApplicable})},
                arguments, state);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &derivedApplicable);
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// ---- The transitive closure ----

// `[Base, Middle : Base, Derived : Middle]` all declaring the same `M(int)`: the derived
// list's applicable candidate hides BOTH more-base lists (the `GetAllBaseTypes` closure
// of `Derived` contains `Middle` and `Base`) -- only the derived method is folded; the
// resolution is not ambiguous.
TEST(AddMethodListsTest, TransitiveBaseTypesAreHidden)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto base = MakeTypeDef("Base");
    auto middle = MakeTypeDef("Middle", {base});
    auto derived = MakeTypeDef("Derived", {middle});
    TestParameter param(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&param});
    TestMethod middleMethod("M");
    middleMethod.SetParameters({&param});
    TestMethod derivedMethod("M");
    derivedMethod.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    RunAddLists({Bucket(base, {&baseMethod}), Bucket(middle, {&middleMethod}),
                 Bucket(derived, {&derivedMethod})},
                arguments, state);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &derivedMethod);
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// The three-list sentinel: with NO derivations all three lists are processed, and the three
// identical signatures keep tying -- `bestCandidateAmbiguousWith` is OVERWRITTEN on each
// ambiguous fold, so the LAST ambiguous method (the Base list's, processed last) is the one
// reported.
TEST(AddMethodListsTest, ThreeUnrelatedListsAllProcessed)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto base = MakeTypeDef("Base");
    auto middle = MakeTypeDef("Middle");
    auto derived = MakeTypeDef("Derived");
    TestParameter param(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&param});
    TestMethod middleMethod("M");
    middleMethod.SetParameters({&param});
    TestMethod derivedMethod("M");
    derivedMethod.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    RunAddLists({Bucket(base, {&baseMethod}), Bucket(middle, {&middleMethod}),
                 Bucket(derived, {&derivedMethod})},
                arguments, state);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &derivedMethod);
    ASSERT_NE(state.ambiguousWith, nullptr);
    EXPECT_EQ(state.ambiguousWith->Member(), &baseMethod);
}

// ---- The public-method delegation ----

// The public `OverloadResolution::AddMethodLists` threads the instance fields and the lazy
// `CSharpConversions::Get` fallback (the conversions ctor parameter left null); the hiding
// crux is observable through the output properties: the derived method is the best candidate,
// no ambiguity, an applicable candidate was found.
TEST(AddMethodListsTest, PublicMethodHidesBaseList)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto base = MakeTypeDef("Base");
    auto derived = MakeTypeDef("Derived", {base});
    TestParameter param(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&param});
    TestMethod derivedMethod("M");
    derivedMethod.SetParameters({&param});
    OverloadResolution resolution(Compilation(), {Arg(intType)});
    resolution.AddMethodLists({Bucket(base, {&baseMethod}), Bucket(derived, {&derivedMethod})});
    EXPECT_EQ(resolution.BestCandidate(), &derivedMethod);
    EXPECT_EQ(resolution.BestCandidateAmbiguousWith(), nullptr);
    EXPECT_FALSE(resolution.IsAmbiguous());
    EXPECT_TRUE(resolution.FoundApplicableCandidate());
}

// The public-method sentinel for the unrelated shape: both lists processed, the identical
// signatures tie, `IsAmbiguous()` is true and `BestCandidateAmbiguousWith()` reports the
// Base method.
TEST(AddMethodListsTest, PublicMethodUnrelatedListsAreAmbiguous)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto base = MakeTypeDef("Base");
    auto derived = MakeTypeDef("Derived");
    TestParameter param(intType, "x");
    TestMethod baseMethod("M");
    baseMethod.SetParameters({&param});
    TestMethod derivedMethod("M");
    derivedMethod.SetParameters({&param});
    OverloadResolution resolution(Compilation(), {Arg(intType)});
    resolution.AddMethodLists({Bucket(base, {&baseMethod}), Bucket(derived, {&derivedMethod})});
    EXPECT_EQ(resolution.BestCandidate(), &derivedMethod);
    EXPECT_EQ(resolution.BestCandidateAmbiguousWith(), &baseMethod);
    EXPECT_TRUE(resolution.IsAmbiguous());
    EXPECT_TRUE(resolution.FoundApplicableCandidate());
}

// The public output properties on the empty state: no candidate at all.
TEST(AddMethodListsTest, PublicMethodEmptyListsReportNoCandidate)
{
    OverloadResolution resolution(Compilation(), {});
    resolution.AddMethodLists({});
    EXPECT_EQ(resolution.BestCandidate(), nullptr);
    EXPECT_EQ(resolution.BestCandidateAmbiguousWith(), nullptr);
    EXPECT_FALSE(resolution.IsAmbiguous());
    EXPECT_FALSE(resolution.FoundApplicableCandidate());
}

} // namespace
