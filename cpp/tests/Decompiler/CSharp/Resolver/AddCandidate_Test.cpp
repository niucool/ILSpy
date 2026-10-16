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

// Tests for `OverloadResolution`'s `AddCandidate` engine entry (OverloadResolution.cs lines
// 231-263) -- the normal + expanded-form pair: the NORMAL-form candidate is allocated,
// given the `additionalErrors`, and calculated through the whole ported pipeline
// (`Detail::CalculateCandidate`, folding it into the best-candidate state); then, when
// `AllowExpandingParams` is set and the member's LAST parameter is `params`, the
// EXPANDED-form candidate is allocated and calculated the same way, and the expanded
// form's errors are returned when its `ErrorCount` is strictly lower than the normal
// form's. Ported as `Detail::AddCandidate` (the D574 instance-state-threading lift) plus
// the public `OverloadResolution::AddCandidate` methods delegating to it.
//
// The load-bearing cruxes:
//  (a) the EXPANDED-FORM PREFERENCE -- `M(params int[])` called with two int arguments:
//      the normal form has `TooManyPositionalArguments`, the expanded form is applicable,
//      so the returned errors are the EXPANDED form's (None) -- and the best-candidate
//      state shows the expanded candidate PROMOTED over the inapplicable normal form
//      (the prefer-applicable heuristic in `BetterFunctionMember`);
//  (b) the STRICT LESS-THAN -- the expanded form's errors win only when its `ErrorCount`
//      is strictly lower; `M(params int[])` called with a single `int[]` argument keeps the
//      NORMAL form's errors (the expanded form's `ArgumentTypeMismatch` is not returned);
//  (c) the EXPANDED-FORM ABORT -- a params parameter whose type is not unpackable (not an
//      array/Span/array-interface) aborts the expanded candidate (`CalculateCandidate`
//      returns false): it is not folded into the best state and its errors never win;
//  (d) the `additionalErrors` propagation -- the lookup errors (e.g. `Inaccessible`) are
//      added to BOTH forms, so the expanded form's error count includes them in the
//      comparison (an expanded form that would otherwise be applicable returns the
//      `Inaccessible` mask, not None);
//  (e) the guards -- `AllowExpandingParams == false`, an empty parameter list, or a last
//      parameter that is not `params` all skip the expanded form entirely (only the normal
//      form is calculated and folded);
//  (f) the public delegation -- the `OverloadResolution::AddCandidate` methods delegate to
//      the `Detail::` free function with the instance fields threaded, resolve the
//      conversions lazily (`CSharpConversions::Get`, the deferred C# ctor default), and
//      thread the `AllowExpandingParams` input property.
//
// The stubs mirror the CalculateCandidate_Test conventions (`TestMethod`/`TestParameter`,
// the `MakeDef` primitive definitions, the local `CSharpConversions` per call -- the
// D540 cache-dangling caveat applies to `CSharpConversions::Get` on the shared static
// compilation, so the Detail tests use local instances and the Get-fallback public test
// uses a non-generic non-params shape that never populates the cached
// `ImplicitConversion(IType, IType)` cache).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
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
using ILSpy::Decompiler::CSharp::Resolver::Detail::AddCandidate;
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

bool HasError(const OverloadResolutionCandidate& c, OverloadResolutionErrors error) {
    return (c.Errors() & error) != OverloadResolutionErrors::None;
}

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

// A minimal `IParameter` over a configured type with configurable `IsParams` (the
// CalculateCandidate_Test `TestParameter` precedent).
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

// A `LookupMethod` with configurable `Parameters` (the CalculateCandidate_Test `TestMethod`
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

// Runs `Detail::AddCandidate` with the C# `OverloadResolution` input-property defaults
// (`AllowExpandingParams`/`AllowOptionalParameters`/`AllowImplicitIn` true,
// `IsExtensionMethodInvocation` false) and the all-positional argument names (the ctor's
// `null` normalization), over the given best state. A LOCAL `CSharpConversions` per call
// (its cache dies with the call; the `CSharpConversions::Get` shared instance is only
// exercised by the public-method Get-fallback test).
OverloadResolutionErrors RunAdd(const IParameterizedMember& member,
                                const std::vector<std::shared_ptr<ResolveResult>>& arguments,
                                BestState& state,
                                OverloadResolutionErrors additionalErrors = OverloadResolutionErrors::None,
                                bool allowExpandingParams = true) {
    CSharpConversions conversions(Compilation());
    std::vector<std::string> names(arguments.size());
    return AddCandidate(member, additionalErrors, Compilation(), conversions, arguments, names,
                        std::nullopt, allowExpandingParams, /*allowOptionalParameters*/true,
                        /*allowImplicitIn*/true, /*isExtensionMethodInvocation*/false,
                        state.best, state.wasValidated, state.ambiguousWith);
}

// ---- The normal form alone (the non-params member shapes) ----

// `M(int)` called with an int argument: the applicable normal form's errors (None) are
// returned and the candidate is folded into the (empty) best state.
TEST(AddCandidateTest, NonParamsMemberReturnsNormalErrorsAndFoldsCandidate)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    EXPECT_EQ(RunAdd(method, arguments, state), OverloadResolutionErrors::None);
    // The normal-form candidate was created and folded into the empty best state.
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Member(), &method);
    EXPECT_FALSE(state.best->IsExpandedForm());
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// `M(int)` called with a string argument: the INAPPLICABLE normal form's errors
// (ArgumentTypeMismatch) are returned -- the inapplicable candidate is still folded into
// the best state (the folding runs regardless of applicability).
TEST(AddCandidateTest, NonParamsMemberInapplicableErrorsReturned)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto strType = MakeDef(KnownTypeCode::String, TypeKind::Class);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(strType)};
    EXPECT_EQ(RunAdd(method, arguments, state), OverloadResolutionErrors::ArgumentTypeMismatch);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->ErrorCount(), 1);
}

// `M(int)` called with an int argument plus `additionalErrors = Inaccessible` (the member
// lookup error): the additional error is reported in the returned mask.
TEST(AddCandidateTest, AdditionalErrorsReportedForNonParamsMember)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    EXPECT_EQ(RunAdd(method, arguments, state, OverloadResolutionErrors::Inaccessible),
              OverloadResolutionErrors::Inaccessible);
    ASSERT_NE(state.best, nullptr);
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::Inaccessible);
    EXPECT_EQ(state.best->ErrorCount(), 1);
}

// ---- The expanded-form pair (the params member shapes) ----

// `M(params int[])` called with two int arguments: the normal form `M(int[])` has
// TooManyPositionalArguments (the second argument has no parameter) while the expanded
// form unpacks the array so both arguments fit -- the expanded form's errors (None) are
// returned, and the best-candidate state shows the EXPANDED candidate promoted over the
// inapplicable normal form (the prefer-applicable heuristic).
TEST(AddCandidateTest, ExpandedFormErrorsReturnedWhenFewerErrors)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto intArray = std::make_shared<ArrayType>(intType);
    TestParameter param(intArray, "vals", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType), Arg(intType)};
    EXPECT_EQ(RunAdd(method, arguments, state), OverloadResolutionErrors::None);
    // The normal form was folded first (it became best), then the expanded form was
    // promoted over it (the expanded candidate is the best).
    ASSERT_NE(state.best, nullptr);
    EXPECT_TRUE(state.best->IsExpandedForm());
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::None);
    EXPECT_EQ(state.best->Member(), &method);
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// `M(params int[])` called with a single `int[]` argument: the NORMAL form is applicable
// (the identity conversion int[] -> int[]) while the expanded form is not (int[] -> int is
// no implicit conversion), so the normal form's errors (None) are returned -- the expanded
// form's ArgumentTypeMismatch is NOT returned (the strict less-than guard: 1 !< 0). The
// best candidate stays the applicable normal form.
TEST(AddCandidateTest, NormalFormErrorsReturnedWhenExpandedNotFewer)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto intArray = std::make_shared<ArrayType>(intType);
    TestParameter param(intArray, "vals", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intArray)};
    EXPECT_EQ(RunAdd(method, arguments, state), OverloadResolutionErrors::None);
    // The applicable normal form stayed best (the inapplicable expanded form did not
    // displace it).
    ASSERT_NE(state.best, nullptr);
    EXPECT_FALSE(state.best->IsExpandedForm());
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::None);
}

// `M(params C)` -- the params parameter's type is a plain class (not an array/Span/array-
// interface): the expanded candidate ABORTS (CalculateCandidate returns false -- the
// params collection cannot be unpacked), so it is not folded into the best state and its
// errors never win; the normal form's errors are returned.
TEST(AddCandidateTest, ExpandedFormAbortedReturnsNormalErrorsOnly)
{
    auto classType = MakeDef(KnownTypeCode::None, TypeKind::Class);
    TestParameter param(classType, "vals", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(classType), Arg(classType)};
    // The normal form has TooManyPositionalArguments (the second argument has no
    // parameter); the aborted expanded form contributes nothing.
    EXPECT_EQ(RunAdd(method, arguments, state), OverloadResolutionErrors::TooManyPositionalArguments);
    // Only the normal-form candidate was folded (the expanded form was removed without
    // being folded).
    ASSERT_NE(state.best, nullptr);
    EXPECT_FALSE(state.best->IsExpandedForm());
    EXPECT_TRUE(HasError(*state.best, OverloadResolutionErrors::TooManyPositionalArguments));
    EXPECT_EQ(state.ambiguousWith, nullptr);
}

// ---- The guards (the expanded form skipped) ----

// `M(params int[])` called with two int arguments but `AllowExpandingParams == false`:
// only the normal form is calculated, so ITS combined errors are returned -- the second
// argument has no parameter (TooManyPositionalArguments) and the first int argument does
// not implicitly convert to the params type int[] (ArgumentTypeMismatch). The input
// property threads into the entry.
TEST(AddCandidateTest, AllowExpandingParamsFalseSkipsExpandedForm)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto intArray = std::make_shared<ArrayType>(intType);
    TestParameter param(intArray, "vals", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType), Arg(intType)};
    EXPECT_EQ(RunAdd(method, arguments, state, OverloadResolutionErrors::None,
                     /*allowExpandingParams*/false),
              OverloadResolutionErrors::TooManyPositionalArguments
                  | OverloadResolutionErrors::ArgumentTypeMismatch);
    // Only the normal-form candidate was calculated and folded.
    ASSERT_NE(state.best, nullptr);
    EXPECT_FALSE(state.best->IsExpandedForm());
}

// A member with NO parameters: the `member.Parameters.Count > 0` guard skips the expanded
// form (no crash on the empty list); the normal form is calculated and folded.
TEST(AddCandidateTest, EmptyParameterListSkipsExpandedForm)
{
    TestMethod method("M");
    BestState state;
    EXPECT_EQ(RunAdd(method, {}, state), OverloadResolutionErrors::None);
    ASSERT_NE(state.best, nullptr);
    EXPECT_FALSE(state.best->IsExpandedForm());
    EXPECT_EQ(state.best->Member(), &method);
}

// `M(int, int)` -- the LAST parameter is not `params`: the expanded form is skipped; the
// applicable normal form's errors are returned and it becomes best.
TEST(AddCandidateTest, LastParamNotParamsSkipsExpandedForm)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param0(intType, "a");
    TestParameter param1(intType, "b");
    TestMethod method("M");
    method.SetParameters({&param0, &param1});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType), Arg(intType)};
    EXPECT_EQ(RunAdd(method, arguments, state), OverloadResolutionErrors::None);
    ASSERT_NE(state.best, nullptr);
    EXPECT_FALSE(state.best->IsExpandedForm());
}

// ---- The additionalErrors propagation to BOTH forms ----

// `M(params int[])` called with two int arguments plus `additionalErrors = Inaccessible`:
// the normal form has {Inaccessible, TooManyPositionalArguments} (2 errors) and the
// expanded form has {Inaccessible} (1 error) -- the additional error was added to BOTH
// forms, so the expanded form returns the Inaccessible mask (not None: without the
// additional error the expanded form would be error-free).
TEST(AddCandidateTest, AdditionalErrorsPropagateToBothForms)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto intArray = std::make_shared<ArrayType>(intType);
    TestParameter param(intArray, "vals", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    BestState state;
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType), Arg(intType)};
    EXPECT_EQ(RunAdd(method, arguments, state, OverloadResolutionErrors::Inaccessible),
              OverloadResolutionErrors::Inaccessible);
    // The expanded form (1 error) was promoted over the normal form (2 errors) via the
    // less-errors heuristic; its mask carries ONLY the additional error.
    ASSERT_NE(state.best, nullptr);
    EXPECT_TRUE(state.best->IsExpandedForm());
    EXPECT_EQ(state.best->Errors(), OverloadResolutionErrors::Inaccessible);
    EXPECT_EQ(state.best->ErrorCount(), 1);
}

// ---- The public `OverloadResolution::AddCandidate` delegation ----

// The public methods delegate to the `Detail::` free function with the instance fields
// threaded: `M(params int[])` with two int arguments returns the expanded form's errors
// (None); with `AllowExpandingParams` set to false the same call returns the normal form's
// combined errors (the input property threads through the delegation).
TEST(AddCandidateTest, PublicMethodDelegatesAndThreadsProperties)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto intArray = std::make_shared<ArrayType>(intType);
    TestParameter param(intArray, "vals", /*isParams*/true);
    TestMethod method("M");
    method.SetParameters({&param});
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType), Arg(intType)};

    // An explicit conversions instance (the ctor parameter) -- the local instance's cache
    // dies with the test (the Get-fallback is exercised by its own test below).
    CSharpConversions conversions(Compilation());
    OverloadResolution r(Compilation(), arguments, std::nullopt, std::nullopt, &conversions);
    EXPECT_EQ(r.AddCandidate(method), OverloadResolutionErrors::None);

    // The input property threads through the delegation.
    OverloadResolution r2(Compilation(), arguments, std::nullopt, std::nullopt, &conversions);
    r2.AllowExpandingParams() = false;
    EXPECT_EQ(r2.AddCandidate(method),
              OverloadResolutionErrors::TooManyPositionalArguments
                  | OverloadResolutionErrors::ArgumentTypeMismatch);
}

// An `OverloadResolution` constructed WITHOUT an explicit conversions instance resolves the
// conversions lazily at the first engine call via `CSharpConversions::Get(compilation)` --
// the deferred C# ctor default `conversions ?? CSharpConversions.Get(compilation)`. The
// shape is deliberately non-generic non-params so the shared cached instance's
// `implicitConversionCache` is never populated (the D540 cache-dangling caveat).
TEST(AddCandidateTest, PublicMethodGetFallbackResolvesConversions)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    OverloadResolution r(Compilation(), arguments);  // conversions == nullptr -> Get fallback
    EXPECT_EQ(r.AddCandidate(method), OverloadResolutionErrors::None);
}

// The 2-arg public overload (the additional-errors entry): `M(int)` with an int argument
// plus `Inaccessible` returns the Inaccessible mask.
TEST(AddCandidateTest, PublicMethodAdditionalErrorsOverload)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestParameter param(intType, "x");
    TestMethod method("M");
    method.SetParameters({&param});
    std::vector<std::shared_ptr<ResolveResult>> arguments = {Arg(intType)};
    CSharpConversions conversions(Compilation());
    OverloadResolution r(Compilation(), arguments, std::nullopt, std::nullopt, &conversions);
    EXPECT_EQ(r.AddCandidate(method, OverloadResolutionErrors::Inaccessible),
              OverloadResolutionErrors::Inaccessible);
}

} // namespace
