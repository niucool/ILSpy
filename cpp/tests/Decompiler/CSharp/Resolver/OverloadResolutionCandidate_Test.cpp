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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `OverloadResolution.Candidate` (D506) -- the per-candidate value type the C# overload
// resolution (`OverloadResolution.cs`, C# spec draft-v11 section 12.6.4) builds per candidate method. The C#
// `sealed class Candidate` (nested in `OverloadResolution`) holds the candidate's `Member`
// (readonly `IParameterizedMember`), `IsExpandedForm` (readonly bool), `ParameterTypes` (sized `IType[]`),
// `ArgumentToParameterMap` (`int[]`), `Errors`/`ErrorCount`/`HasUnmappedOptionalParameters`,
// `InferredTypes` (`IType[]`), `Parameters` (readonly `IReadOnlyList<IParameter>` from the member
// DEFINITION), `TypeParameters` (readonly `IReadOnlyList<ITypeParameter>`, for a generic method), and
// `ArgumentConversions` (`Conversion[]`). The computed properties `ParamsCollectionType` /
// `IsGenericMethod` / `ArgumentsPassedToParams`; the `AddError` method accumulates the error mask and
// increments `ErrorCount` if the error makes the candidate inapplicable. The static `IsApplicable`
// returns whether the error mask (minus `AmbiguousMatch` | `MethodConstraintsNotSatisfied`) is `None`.
//
// The tests pin:
//  (a) the ctor: `Member`/`IsExpandedForm`, `Parameters` from the member definition, `TypeParameters`
//      empty for a non-generic method, `ParameterTypes` sized to the parameter count (null entries);
//  (b) `IsGenericMethod`: false for a non-generic method (the LookupMethod stub has 0 type params);
//  (c) `ParamsCollectionType`: `UnknownType` for a non-params / non-expanded candidate;
//  (d) `ArgumentsPassedToParams`: 0 for a non-expanded candidate;
//  (e) `IsApplicable`: `None` -> true; `AmbiguousMatch` alone -> true (does not matter); a real error
//      (e.g. `ArgumentTypeMismatch`) -> false; `MethodConstraintsNotSatisfied` alone -> true;
//  (f) `AddError`: an inapplicable error increments `ErrorCount`; an "does not matter" error does not.

#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::IsApplicable;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionErrors;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

} // namespace

// ---------------------------------------------------------------------------
// Ctor: a non-generic method -> Member/IsExpandedForm set, Parameters from the definition, TypeParameters
// empty, ParameterTypes sized to the parameter count (0 for the LookupMethod stub).
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCandidateTest, CtorNonGenericMethod) {
    auto m = std::make_shared<LookupMethod>("M", Compilation());
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    EXPECT_EQ(c.Member(), m.get());
    EXPECT_FALSE(c.IsExpandedForm());
    EXPECT_TRUE(c.Parameters().empty());  // LookupMethod has 0 parameters
    EXPECT_TRUE(c.TypeParameters().empty());  // non-generic
    EXPECT_EQ(c.ParameterTypes().size(), 0u);  // sized to 0
}

// ---------------------------------------------------------------------------
// IsGenericMethod: false for a non-generic method (0 type parameters).
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCandidateTest, IsGenericMethodFalseForNonGeneric) {
    auto m = std::make_shared<LookupMethod>("M", Compilation());
    OverloadResolutionCandidate c(m.get(), false);
    EXPECT_FALSE(c.IsGenericMethod());
}

// ---------------------------------------------------------------------------
// ParamsCollectionType: UnknownType for a non-params / non-expanded candidate.
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCandidateTest, ParamsCollectionTypeUnknownForNonExpanded) {
    auto m = std::make_shared<LookupMethod>("M", Compilation());
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    EXPECT_EQ(c.ParamsCollectionType()->Kind(), TypeKind::Unknown);
}

// ---------------------------------------------------------------------------
// ArgumentsPassedToParams: 0 for a non-expanded candidate.
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCandidateTest, ArgumentsPassedToParamsZeroForNonExpanded) {
    auto m = std::make_shared<LookupMethod>("M", Compilation());
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    c.ArgumentToParameterMap() = {0, 1, 2};
    EXPECT_EQ(c.ArgumentsPassedToParams(), 0);
}

// ---------------------------------------------------------------------------
// IsApplicable: None -> true; AmbiguousMatch alone -> true (does not matter); a real error -> false;
// MethodConstraintsNotSatisfied alone -> true.
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCandidateTest, IsApplicableErrorMask) {
    EXPECT_TRUE(IsApplicable(OverloadResolutionErrors::None));
    EXPECT_TRUE(IsApplicable(OverloadResolutionErrors::AmbiguousMatch));
    EXPECT_TRUE(IsApplicable(OverloadResolutionErrors::MethodConstraintsNotSatisfied));
    // A real inapplicability error (e.g. ArgumentTypeMismatch) -> false.
    EXPECT_FALSE(IsApplicable(OverloadResolutionErrors::ArgumentTypeMismatch |
                              OverloadResolutionErrors::AmbiguousMatch));
}

// ---------------------------------------------------------------------------
// AddError: an inapplicable error increments ErrorCount; an "does not matter" error does not.
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCandidateTest, AddErrorIncrementsErrorCountForInapplicable) {
    auto m = std::make_shared<LookupMethod>("M", Compilation());
    OverloadResolutionCandidate c(m.get(), false);
    EXPECT_EQ(c.ErrorCount(), 0);
    c.AddError(OverloadResolutionErrors::ArgumentTypeMismatch);
    EXPECT_NE(c.ErrorCount(), 0);  // inapplicable -> incremented
    // An "does not matter" error does NOT increment ErrorCount.
    int before = c.ErrorCount();
    c.AddError(OverloadResolutionErrors::AmbiguousMatch);
    EXPECT_EQ(c.ErrorCount(), before);  // AmbiguousMatch does not matter -> not incremented
}
