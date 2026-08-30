// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `OverloadResolution` Output Wrappers region (OverloadResolution.cs lines
// 1087-1208):
//  * the PRIVATE core `GetArgumentsWithConversions(ResolveResult, IParameterizedMember)`
//    (line 1110) -- ported as `Detail::GetArgumentsWithConversions` (the state-threading
//    convention: the instance fields it reads -- `IsExtensionMethodInvocation` /
//    `CheckForOverflow` / `arguments` / `argumentNames` / `bestCandidate` / the
//    `ArgumentConversions` snapshot -- are parameters);
//  * the public `GetArgumentsWithConversions()` (line 1087) and
//    `GetArgumentsWithConversionsAndNames()` (line 1102) -- the no-best early return of the
//    live `arguments`, else the core with (null, null) / (null, the substituted best);
//  * `CreateResolveResult` (line 1188) -- the CSharpInvocationResolveResult composition.
//
// The load-bearing cruxes:
//  (a) the WRAP -- a mapped argument under a NON-identity conversion is wrapped in a
//      `ConversionResolveResult` carrying the best candidate's substituted parameter type,
//      the original argument, and the applied conversion singleton (pointer identity);
//  (b) the SKIP guards -- an IDENTITY conversion, an UNMAPPED argument (the -1 map entry),
//      and an UNKNOWN parameter type all leave the argument unwrapped;
//  (c) the DEFERRED constant arm -- a compile-time-constant argument under a valid
//      non-user-defined conversion is wrapped in the `ConversionResolveResult` (the C#
//      re-resolves it through `CSharpResolver.ResolveCast`, not yet ported; the wrap is the
//      documented faithful fallback that preserves the wrapper structure);
//  (d) the NAMED wrap -- when the caller passes a `bestCandidateForNamedArguments` and the
//      argument was given with an explicit name, the argument is wrapped in a
//      `NamedArgumentResolveResult` (the parameter + member ctor when mapped, the name-only
//      ctor when unmapped) -- composing AROUND the conversion wrap;
//  (e) the EXTENSION-METHOD RECEIVER SWAP -- the first argument is replaced by the
//      targetResolveResult when `IsExtensionMethodInvocation` (and only then, and only when
//      the target is non-null);
//  (f) the public delegation -- the pipeline (AddCandidate) folds a best candidate, then
//      the wrappers observe the folded state: `CreateResolveResult` carries the member, the
//      error mask, the expanded-form flag, the argument->parameter map, the initializer
//      statements and the return-type override; the extension-method target is a
//      `TypeResolveResult` over the member's declaring type (UnknownType when null).
//
// The stubs mirror the OutputProperties_Test / AddMethodLists_Test conventions: the
// `TestParameter`/`TestMethod` pair, and one SHARED type instance per logical type across
// the parameter/argument (the real compilation's type-cache model).

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/NamedArgumentResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::CSharp::Resolver::Detail::GetArgumentsWithConversions;
using ILSpy::Decompiler::Semantics::ConstantResolveResult;
using ILSpy::Decompiler::Semantics::ConversionResolveResult;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::NamedArgumentResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::UnknownType;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// The shared compilation (a plain `LookupCompilation`; the pipeline tests pass their own
// locally-constructed `CSharpConversions` to `OverloadResolution`, so no `Get` singleton is
// involved -- the D540 dangling-cache caveat never applies to these tests).
LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A primitive definition (`LookupTypeDefinition`, IS-A `ITypeDefinition`) with the given
// `KnownTypeCode` / `TypeKind` -- the D514 `MakeDef` precedent (distinct names so distinct
// instances never compare equal; `GetTypeCode` resolves through the KnownTypeCode).
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A minimal `IParameter` over a configured type (the OutputProperties_Test `TestParameter`
// precedent).
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
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    bool isParams_;
};

// A `LookupMethod` with a CONFIGURABLE `DeclaringType` (the extension-method target crux;
// the plain stub's `DeclaringType()` returns null, the `?? SpecialType.UnknownType` shape).
class TestMethod : public LookupMethod {
public:
    explicit TestMethod(std::string name = "M")
        : LookupMethod(std::move(name), Compilation()) {}
    void SetDeclaringType(ITypePtr type) { declaringType_ = std::move(type); }
    ITypePtr DeclaringType() const override { return declaringType_; }
private:
    ITypePtr declaringType_;
};

// A plain `ResolveResult` over the given type (the base class is concrete).
std::shared_ptr<ResolveResult> Arg(ITypePtr type) {
    return std::make_shared<ResolveResult>(std::move(type));
}

// A hand-built best candidate over the method with the given parameter type, map, and
// per-argument conversions (the `Detail::GetArgumentsWithConversions` direct tests configure
// the candidate's mutable state rather than running the pipeline).
std::shared_ptr<OverloadResolutionCandidate> MakeCandidate(
    const LookupMethod* method,
    std::vector<ITypePtr> parameterTypes,
    std::vector<int> argumentToParameterMap,
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>> argumentConversions)
{
    auto candidate = std::make_shared<OverloadResolutionCandidate>(method, false);
    candidate->ParameterTypes() = std::move(parameterTypes);
    candidate->ArgumentToParameterMap() = std::move(argumentToParameterMap);
    candidate->ArgumentConversions() = std::move(argumentConversions);
    return candidate;
}

// ---- Detail::GetArgumentsWithConversions: the conversion wrap ----

// A mapped argument under a NON-identity conversion is wrapped in a `ConversionResolveResult`
// carrying the candidate's substituted parameter type, the original argument, and the applied
// conversion singleton (pointer identity), plus the threaded `CheckForOverflow` flag.
TEST(OutputWrappersTest, ConversionWrapsNonIdentityConvertedArgument)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto longType = MakeDef(KnownTypeCode::Int64);
    TestMethod method("M");
    auto original = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {longType}, {0}, {Conversions::ImplicitNumericConversion()});

    auto result = GetArgumentsWithConversions(
        /*targetResolveResult*/ nullptr, /*bestCandidateForNamedArguments*/ nullptr,
        /*isExtensionMethodInvocation*/ false, /*checkForOverflow*/ true,
        {original}, {""}, candidate, candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    const auto* wrapped = dynamic_cast<const ConversionResolveResult*>(result[0].get());
    ASSERT_NE(wrapped, nullptr);
    // The wrap carries the parameter type, the original argument, and the conversion.
    EXPECT_EQ(&wrapped->Type(), longType.get());
    EXPECT_EQ(wrapped->Input(), original.get());
    EXPECT_EQ(wrapped->ConversionProperty(), Conversions::ImplicitNumericConversion().get());
    EXPECT_TRUE(wrapped->CheckForOverflow());
    // The wrap is NOT the original argument (a fresh wrapper instance).
    EXPECT_NE(result[0].get(), original.get());
}

// An IDENTITY conversion leaves the argument unwrapped (pointer identity preserved) -- the
// `conversions[i] != Conversion.IdentityConversion` guard.
TEST(OutputWrappersTest, IdentityConversionDoesNotWrap)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    auto original = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {intType}, {0}, {Conversions::IdentityConversion()});

    auto result = GetArgumentsWithConversions(
        nullptr, nullptr, false, false, {original}, {""}, candidate,
        candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), original.get());
}

// An UNMAPPED argument (the -1 map entry) is left unwrapped even though its conversion entry
// is non-identity -- the `parameterIndex >= 0` guard.
TEST(OutputWrappersTest, UnmappedArgumentDoesNotWrap)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto longType = MakeDef(KnownTypeCode::Int64);
    TestMethod method("M");
    auto original = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {longType}, {-1}, {Conversions::ImplicitNumericConversion()});

    auto result = GetArgumentsWithConversions(
        nullptr, nullptr, false, false, {original}, {""}, candidate,
        candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), original.get());
}

// An UNKNOWN parameter type leaves the argument unwrapped -- the
// `parameterType.Kind != TypeKind.Unknown` guard.
TEST(OutputWrappersTest, UnknownParameterTypeDoesNotWrap)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    auto original = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {UnknownType()}, {0}, {Conversions::ImplicitNumericConversion()});

    auto result = GetArgumentsWithConversions(
        nullptr, nullptr, false, false, {original}, {""}, candidate,
        candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), original.get());
}

// A compile-time-constant argument under a valid non-user-defined conversion is wrapped in
// the `ConversionResolveResult` -- the documented DEFERRED-constant-arm fallback: the C#
// re-resolves the constant through `CSharpResolver.ResolveCast` (not yet ported); the wrap
// preserves the wrapper structure (target type + the applied conversion), only the constant
// is not re-folded.
TEST(OutputWrappersTest, ConstantArgumentWrapsWhileResolveCastDeferred)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto longType = MakeDef(KnownTypeCode::Int64);
    TestMethod method("M");
    auto constant = std::make_shared<ConstantResolveResult>(intType, std::any{std::int32_t{5}});
    auto candidate = MakeCandidate(
        &method, {longType}, {0}, {Conversions::ImplicitNumericConversion()});

    auto result = GetArgumentsWithConversions(
        nullptr, nullptr, false, false, {constant}, {""}, candidate,
        candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    const auto* wrapped = dynamic_cast<const ConversionResolveResult*>(result[0].get());
    ASSERT_NE(wrapped, nullptr);
    EXPECT_EQ(&wrapped->Type(), longType.get());
    EXPECT_EQ(wrapped->Input(), constant.get());
}

// ---- Detail::GetArgumentsWithConversions: the named wrap ----

// A MAPPED named argument is wrapped in a `NamedArgumentResolveResult` carrying the
// parameter (from the candidate-for-named-arguments' method table) and the member itself.
TEST(OutputWrappersTest, NamedArgumentWrapsWithParameterAndMember)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    TestParameter param(intType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {intType}, {0}, {Conversions::IdentityConversion()});

    auto result = GetArgumentsWithConversions(
        nullptr, &method, false, false, {original}, {"x"}, candidate,
        candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    const auto* named = dynamic_cast<const NamedArgumentResolveResult*>(result[0].get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->ParameterName(), "x");
    EXPECT_EQ(named->Parameter(), &param);
    EXPECT_EQ(named->Member(), &method);
    // The named wrap composes AROUND the conversion wrap: an identity conversion left the
    // argument unwrapped, so the named argument carries the original directly.
    EXPECT_EQ(named->Argument(), original.get());
}

// An UNMAPPED named argument uses the NAME-ONLY ctor (no parameter, no member) -- the
// `parameterIndex < 0` arm.
TEST(OutputWrappersTest, NamedArgumentUnmappedUsesNameOnlyCtor)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    TestParameter param(intType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {intType}, {-1}, {Conversions::IdentityConversion()});

    auto result = GetArgumentsWithConversions(
        nullptr, &method, false, false, {original}, {"x"}, candidate,
        candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    const auto* named = dynamic_cast<const NamedArgumentResolveResult*>(result[0].get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->ParameterName(), "x");
    EXPECT_EQ(named->Parameter(), nullptr);
    EXPECT_EQ(named->Member(), nullptr);
    EXPECT_EQ(named->Argument(), original.get());
}

// The named wrap is skipped without a candidate-for-named-arguments (a positional call's
// names never reach the wrap) and for a positional argument (the empty name -- the C# `null`
// entry -- never reaches the wrap even when the candidate is given).
TEST(OutputWrappersTest, NamedWrapSkippedForPositionalAndNullCandidate)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    TestParameter param(intType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {intType}, {0}, {Conversions::IdentityConversion()});

    // A null candidate-for-named-arguments: the name is present but there is no member to
    // resolve the parameter from, so the argument comes back unwrapped.
    auto nullCandidate = GetArgumentsWithConversions(
        nullptr, nullptr, false, false, {original}, {"x"}, candidate,
        candidate->ArgumentConversions());
    ASSERT_EQ(nullCandidate.size(), 1u);
    EXPECT_EQ(nullCandidate[0].get(), original.get());

    // A positional argument (the empty name): not named-wrapped.
    auto positional = GetArgumentsWithConversions(
        nullptr, &method, false, false, {original}, {""}, candidate,
        candidate->ArgumentConversions());
    ASSERT_EQ(positional.size(), 1u);
    EXPECT_EQ(positional[0].get(), original.get());
}

// A converted AND named argument is double-wrapped: the `NamedArgumentResolveResult`'s
// argument is the `ConversionResolveResult` (the conversion wrap runs first, the named wrap
// composes around it) -- the C# reassignment order pinned.
TEST(OutputWrappersTest, ConversionAndNamedWrapCompose)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto longType = MakeDef(KnownTypeCode::Int64);
    TestMethod method("M");
    TestParameter param(longType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {longType}, {0}, {Conversions::ImplicitNumericConversion()});

    auto result = GetArgumentsWithConversions(
        nullptr, &method, false, false, {original}, {"x"}, candidate,
        candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    const auto* named = dynamic_cast<const NamedArgumentResolveResult*>(result[0].get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->ParameterName(), "x");
    const auto* wrapped = dynamic_cast<const ConversionResolveResult*>(named->Argument());
    ASSERT_NE(wrapped, nullptr);
    EXPECT_EQ(&wrapped->Type(), longType.get());
    EXPECT_EQ(wrapped->Input(), original.get());
}

// ---- Detail::GetArgumentsWithConversions: the extension-method receiver swap ----

// The FIRST argument is replaced by the targetResolveResult when
// `IsExtensionMethodInvocation` (and the target is non-null) -- the C# extension-method
// receiver swap; the remaining arguments keep their identity (identity conversions).
TEST(OutputWrappersTest, ExtensionMethodInvocationSwapsFirstArgumentOnly)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    auto receiver = Arg(intType);
    auto first = Arg(intType);
    auto second = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {intType, intType}, {0, 1},
        {Conversions::IdentityConversion(), Conversions::IdentityConversion()});

    auto result = GetArgumentsWithConversions(
        receiver, nullptr, /*isExtensionMethodInvocation*/ true, false,
        {first, second}, {"", ""}, candidate, candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].get(), receiver.get());
    EXPECT_EQ(result[1].get(), second.get());
}

// Without a targetResolveResult the first argument is NOT swapped (a static extension-method
// call carries no receiver in the wrapper output) -- the `targetResolveResult != null` guard.
TEST(OutputWrappersTest, ExtensionMethodInvocationWithoutTargetKeepsArgument)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    auto first = Arg(intType);
    auto candidate = MakeCandidate(
        &method, {intType}, {0}, {Conversions::IdentityConversion()});

    auto result = GetArgumentsWithConversions(
        nullptr, nullptr, /*isExtensionMethodInvocation*/ true, false,
        {first}, {""}, candidate, candidate->ArgumentConversions());

    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].get(), first.get());
}

// ---- The public delegation ----

// With NO best candidate the public wrappers return the arguments as-is (the shared handles
// preserve the argument pointer identity, the C# live `arguments` array).
TEST(OutputWrappersTest, PublicWrappersReturnArgumentsWithoutBest)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto first = Arg(intType);
    auto second = Arg(intType);

    OverloadResolution resolution(Compilation(), {first, second});
    auto withConversions = resolution.GetArgumentsWithConversions();
    ASSERT_EQ(withConversions.size(), 2u);
    EXPECT_EQ(withConversions[0].get(), first.get());
    EXPECT_EQ(withConversions[1].get(), second.get());

    auto withNames = resolution.GetArgumentsWithConversionsAndNames();
    ASSERT_EQ(withNames.size(), 2u);
    EXPECT_EQ(withNames[0].get(), first.get());
    EXPECT_EQ(withNames[1].get(), second.get());
}

// The full pipeline (AddCandidate) folds a best candidate, then
// `GetArgumentsWithConversions()` wraps a widening argument in the `ConversionResolveResult`
// over the best candidate's substituted parameter type.
TEST(OutputWrappersTest, PublicGetArgumentsWithConversionsWrapsConvertedArgument)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto longType = MakeDef(KnownTypeCode::Int64);
    TestMethod method("M");
    TestParameter param(longType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);

    CSharpConversions conversions(Compilation());
    OverloadResolution resolution(Compilation(), {original}, std::nullopt, std::nullopt,
                                  &conversions);
    resolution.AddCandidate(method);
    EXPECT_EQ(resolution.BestCandidate(), &method);

    auto result = resolution.GetArgumentsWithConversions();
    ASSERT_EQ(result.size(), 1u);
    const auto* wrapped = dynamic_cast<const ConversionResolveResult*>(result[0].get());
    ASSERT_NE(wrapped, nullptr);
    EXPECT_EQ(&wrapped->Type(), longType.get());
    EXPECT_EQ(wrapped->Input(), original.get());
    EXPECT_EQ(wrapped->ConversionProperty(), Conversions::ImplicitNumericConversion().get());
}

// `GetArgumentsWithConversionsAndNames()` wraps a named argument in the
// `NamedArgumentResolveResult` carrying the parameter AND the best candidate's member (the
// non-generic `GetBestCandidateWithSubstitutedTypeArguments` returns the member as-is).
TEST(OutputWrappersTest, PublicGetArgumentsWithConversionsAndNamesWrapsNamedArgument)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    TestParameter param(intType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);

    CSharpConversions conversions(Compilation());
    OverloadResolution resolution(
        Compilation(), {original},
        std::optional<std::vector<std::string>>{std::vector<std::string>{"x"}},
        std::nullopt, &conversions);
    resolution.AddCandidate(method);

    auto result = resolution.GetArgumentsWithConversionsAndNames();
    ASSERT_EQ(result.size(), 1u);
    const auto* named = dynamic_cast<const NamedArgumentResolveResult*>(result[0].get());
    ASSERT_NE(named, nullptr);
    EXPECT_EQ(named->ParameterName(), "x");
    EXPECT_EQ(named->Parameter(), &param);
    EXPECT_EQ(named->Member(), &method);
    // The identity conversion left the argument unwrapped, so the named wrap carries it
    // directly.
    EXPECT_EQ(named->Argument(), original.get());
}

// `CreateResolveResult` throws when no candidate was added (the C#
// `InvalidOperationException`).
TEST(OutputWrappersTest, PublicCreateResolveResultThrowsWithoutBest)
{
    OverloadResolution resolution(Compilation(), {});
    EXPECT_THROW(resolution.CreateResolveResult(), std::runtime_error);
}

// A static invocation: `CreateResolveResult(nullptr)` yields the
// `CSharpInvocationResolveResult` over the best member with a null target, the `None` error
// mask, and the argument->parameter map snapshot.
TEST(OutputWrappersTest, PublicCreateResolveResultStaticInvocation)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    method.SetReturnType(intType);
    TestParameter param(intType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);

    CSharpConversions conversions(Compilation());
    OverloadResolution resolution(Compilation(), {original}, std::nullopt, std::nullopt,
                                  &conversions);
    resolution.AddCandidate(method);

    auto result = resolution.CreateResolveResult();
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->Member(), &method);
    EXPECT_EQ(result->TargetResult(), nullptr);
    EXPECT_EQ(result->OverloadResolutionErrors(), OverloadResolutionErrors::None);
    EXPECT_FALSE(result->IsError());
    EXPECT_FALSE(result->IsExtensionMethodInvocation());
    EXPECT_FALSE(result->IsDelegateInvocation());
    EXPECT_FALSE(result->IsExpandedForm());
    ASSERT_EQ(result->Arguments().size(), 1u);
    EXPECT_EQ(result->Arguments()[0].get(), original.get());
    auto map = result->GetArgumentToParameterMap();
    ASSERT_TRUE(map.has_value());
    ASSERT_EQ(map->size(), 1u);
    EXPECT_EQ((*map)[0], 0);
}

// An extension-method invocation: the result's TARGET is a `TypeResolveResult` over the
// member's DECLARING TYPE (the `member.DeclaringType ?? SpecialType.UnknownType` coalesce --
// the non-null direction), NOT the passed receiver; the passed receiver appears as the FIRST
// ARGUMENT instead (the receiver swap) -- the target-vs-first-argument asymmetry the C#
// extension-method shape produces.
TEST(OutputWrappersTest, PublicCreateResolveResultExtensionMethodTargetIsDeclaringType)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto declaringType = MakeDef(KnownTypeCode::None, TypeKind::Class);
    TestMethod method("M");
    method.SetDeclaringType(declaringType);
    method.SetReturnType(intType);
    TestParameter param(intType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);
    auto receiver = Arg(declaringType);

    CSharpConversions conversions(Compilation());
    OverloadResolution resolution(Compilation(), {original}, std::nullopt, std::nullopt,
                                  &conversions);
    resolution.IsExtensionMethodInvocation() = true;
    resolution.AddCandidate(method);

    auto result = resolution.CreateResolveResult(receiver);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->IsExtensionMethodInvocation());
    const auto* target = dynamic_cast<const TypeResolveResult*>(result->TargetResult());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(&target->Type(), declaringType.get());
    // The receiver swap put the passed target as the first argument (replacing the original
    // first argument the applicability check ran against).
    ASSERT_EQ(result->Arguments().size(), 1u);
    EXPECT_EQ(result->Arguments()[0].get(), receiver.get());
}

// An extension-method invocation over a member with NO declaring type (the plain stub): the
// target is the `TypeResolveResult` over `SpecialType.UnknownType` -- the `??` fallback.
TEST(OutputWrappersTest, PublicCreateResolveResultExtensionMethodUnknownTypeFallback)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    TestMethod method("M");
    method.SetReturnType(intType);
    TestParameter param(intType, "x");
    method.SetParameters({&param});
    auto original = Arg(intType);

    CSharpConversions conversions(Compilation());
    OverloadResolution resolution(Compilation(), {original}, std::nullopt, std::nullopt,
                                  &conversions);
    resolution.IsExtensionMethodInvocation() = true;
    resolution.AddCandidate(method);

    auto result = resolution.CreateResolveResult();
    ASSERT_NE(result, nullptr);
    const auto* target = dynamic_cast<const TypeResolveResult*>(result->TargetResult());
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->Type().Kind(), TypeKind::Unknown);
}

// The params-expanded form: `CreateResolveResult` carries the expanded-form flag, the
// initializer statements, and the return-type override (the override replaces the member's
// return type as the result type).
TEST(OutputWrappersTest, PublicCreateResolveResultCarriesExpandedFormInitializerAndOverride)
{
    auto intType = MakeDef(KnownTypeCode::Int32);
    auto intArray = std::make_shared<ArrayType>(intType);
    auto stringType = MakeDef(KnownTypeCode::String, TypeKind::Class);
    TestMethod method("M");
    TestParameter param(intArray, "p", /*isParams*/true);
    method.SetParameters({&param});
    auto first = Arg(intType);
    auto second = Arg(intType);
    auto initStatement = Arg(stringType);
    auto returnTypeOverride = MakeDef(KnownTypeCode::None, TypeKind::Class);

    CSharpConversions conversions(Compilation());
    OverloadResolution resolution(Compilation(), {first, second}, std::nullopt, std::nullopt,
                                  &conversions);
    resolution.AddCandidate(method);
    EXPECT_TRUE(resolution.BestCandidateIsExpandedForm());

    auto result = resolution.CreateResolveResult(
        nullptr, {initStatement}, returnTypeOverride);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->IsExpandedForm());
    // The expanded form mapped both arguments to the single params parameter.
    auto map = result->GetArgumentToParameterMap();
    ASSERT_TRUE(map.has_value());
    ASSERT_EQ(map->size(), 2u);
    EXPECT_EQ((*map)[0], 0);
    EXPECT_EQ((*map)[1], 0);
    // The initializer statements pass through, and the return-type override replaces the
    // member's return type.
    ASSERT_EQ(result->InitializerStatements().size(), 1u);
    EXPECT_EQ(result->InitializerStatements()[0].get(), initStatement.get());
    EXPECT_EQ(&result->Type(), returnTypeOverride.get());
}

} // namespace
