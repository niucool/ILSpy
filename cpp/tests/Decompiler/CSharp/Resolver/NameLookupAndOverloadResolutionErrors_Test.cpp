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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the two CSharp/Resolver enum leaves (the next increments toward the
// CSharpResolver leaf deps, the long-pole remaining blocker of TypeSystemAstBuilder /
// CSharpAmbience): NameLookupMode (the lookup-mode enum the CSharpResolver /
// MemberLookup consume) and OverloadResolutionErrors (the [Flags] error mask the
// OverloadResolution accumulator produces). Both are self-contained value types
// (no type-system deps); the tests pin each member's value (matching the C# literals
// / declaration order), the [Flags] bitwise operators, and the flag-test idiom the
// resolver consumers use.

#include "Decompiler/CSharp/Resolver/NameLookupMode.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionErrors.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

namespace Res = ILSpy::Decompiler::CSharp::Resolver;
using Res::NameLookupMode;
using Res::OverloadResolutionErrors;

// ===========================================================================
// NameLookupMode -- the five lookup modes are the declaration-order 0..4 the
// MemberLookup / CSharpResolver consumers compare by identity.
// ===========================================================================

TEST(NameLookupModeTest, MembersMatchCSharpDeclarationOrderValues)
{
    // The C# enum has no explicit values, so the members are 0..4 by declaration
    // order (the C# default).
    EXPECT_EQ(static_cast<std::int32_t>(NameLookupMode::Expression), 0);
    EXPECT_EQ(static_cast<std::int32_t>(NameLookupMode::InvocationTarget), 1);
    EXPECT_EQ(static_cast<std::int32_t>(NameLookupMode::Type), 2);
    EXPECT_EQ(static_cast<std::int32_t>(NameLookupMode::TypeInUsingDeclaration), 3);
    EXPECT_EQ(static_cast<std::int32_t>(NameLookupMode::BaseTypeReference), 4);
}

TEST(NameLookupModeTest, MembersAreDistinct)
{
    // The five modes are distinct -- the MemberLookup result shape depends on the
    // exact mode, so a collision would silently merge two lookup kinds.
    EXPECT_NE(NameLookupMode::Expression, NameLookupMode::InvocationTarget);
    EXPECT_NE(NameLookupMode::Expression, NameLookupMode::Type);
    EXPECT_NE(NameLookupMode::Expression, NameLookupMode::TypeInUsingDeclaration);
    EXPECT_NE(NameLookupMode::Expression, NameLookupMode::BaseTypeReference);
    EXPECT_NE(NameLookupMode::InvocationTarget, NameLookupMode::Type);
    EXPECT_NE(NameLookupMode::InvocationTarget, NameLookupMode::TypeInUsingDeclaration);
    EXPECT_NE(NameLookupMode::InvocationTarget, NameLookupMode::BaseTypeReference);
    EXPECT_NE(NameLookupMode::Type, NameLookupMode::TypeInUsingDeclaration);
    EXPECT_NE(NameLookupMode::Type, NameLookupMode::BaseTypeReference);
    EXPECT_NE(NameLookupMode::TypeInUsingDeclaration, NameLookupMode::BaseTypeReference);
}

TEST(NameLookupModeTest, UnderlyingTypeIsInt32)
{
    // The C# enum has no underlying annotation, so it is `int` (System.Int32); the
    // port backs it with std::int32_t so the values fit as plain signed values.
    static_assert(std::is_same<std::underlying_type_t<NameLookupMode>, std::int32_t>::value,
                  "NameLookupMode is int32-backed, matching the C# int default");
}

// ===========================================================================
// OverloadResolutionErrors -- each individual flag matches its C# literal value.
// ===========================================================================

TEST(OverloadResolutionErrorsTest, IndividualFlagsMatchCSharpLiterals)
{
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::None), 0);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::TooManyPositionalArguments), 0x0001);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::NoParameterFoundForNamedArgument), 0x0002);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::TypeInferenceFailed), 0x0004);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::WrongNumberOfTypeArguments), 0x0008);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::ConstructedTypeDoesNotSatisfyConstraint), 0x0010);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::MissingArgumentForRequiredParameter), 0x0020);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::MultipleArgumentsForSingleParameter), 0x0040);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::ParameterPassingModeMismatch), 0x0080);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::ArgumentTypeMismatch), 0x0100);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::AmbiguousMatch), 0x0200);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::Inaccessible), 0x0400);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::MethodConstraintsNotSatisfied), 0x0800);
    EXPECT_EQ(static_cast<std::int32_t>(OverloadResolutionErrors::OutVarTypeMismatch), 0x1000);
}

TEST(OverloadResolutionErrorsTest, UnderlyingTypeIsInt32)
{
    // The C# [Flags] enum has no underlying annotation, so it is `int` (System.Int32);
    // the port backs it with std::int32_t so the largest member (OutVarTypeMismatch =
    // 0x1000) fits as a plain signed value.
    static_assert(std::is_same<std::underlying_type_t<OverloadResolutionErrors>, std::int32_t>::value,
                  "OverloadResolutionErrors is int32-backed, matching the C# int default");
}

TEST(OverloadResolutionErrorsTest, IndividualFlagsAreDisjointSingleBits)
{
    // The 12 individual flags are disjoint single bits, so the OverloadResolution
    // accumulator can OR them into a combined mask without clobbering (a collision
    // would silently drop an error).
    auto checkDisjoint = [](OverloadResolutionErrors a, OverloadResolutionErrors b) {
        // (a & b) == None iff the two flags share no bit.
        return (a & b) == OverloadResolutionErrors::None;
    };
    OverloadResolutionErrors flags[] = {
        OverloadResolutionErrors::TooManyPositionalArguments,
        OverloadResolutionErrors::NoParameterFoundForNamedArgument,
        OverloadResolutionErrors::TypeInferenceFailed,
        OverloadResolutionErrors::WrongNumberOfTypeArguments,
        OverloadResolutionErrors::ConstructedTypeDoesNotSatisfyConstraint,
        OverloadResolutionErrors::MissingArgumentForRequiredParameter,
        OverloadResolutionErrors::MultipleArgumentsForSingleParameter,
        OverloadResolutionErrors::ParameterPassingModeMismatch,
        OverloadResolutionErrors::ArgumentTypeMismatch,
        OverloadResolutionErrors::AmbiguousMatch,
        OverloadResolutionErrors::Inaccessible,
        OverloadResolutionErrors::MethodConstraintsNotSatisfied,
        OverloadResolutionErrors::OutVarTypeMismatch,
    };
    for (auto a : flags) {
        for (auto b : flags) {
            if (a != b) {
                EXPECT_TRUE(checkDisjoint(a, b))
                << "flags " << static_cast<std::int32_t>(a) << " and "
                << static_cast<std::int32_t>(b) << " share a bit";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// OverloadResolutionErrors -- the [Flags] bitwise operators (the C# [Flags]
// compiler generates them implicitly; the C++ enum class defines them as free
// functions).
// ---------------------------------------------------------------------------

TEST(OverloadResolutionErrorsTest, BitwiseOrCombinesFlags)
{
    auto combined = OverloadResolutionErrors::ArgumentTypeMismatch |
                    OverloadResolutionErrors::TypeInferenceFailed;
    EXPECT_EQ(combined & OverloadResolutionErrors::ArgumentTypeMismatch,
              OverloadResolutionErrors::ArgumentTypeMismatch);
    EXPECT_EQ(combined & OverloadResolutionErrors::TypeInferenceFailed,
              OverloadResolutionErrors::TypeInferenceFailed);
    EXPECT_EQ(combined & OverloadResolutionErrors::AmbiguousMatch,
              OverloadResolutionErrors::None);
}

TEST(OverloadResolutionErrorsTest, BitwiseAndIntersectsFlags)
{
    auto a = OverloadResolutionErrors::ArgumentTypeMismatch |
             OverloadResolutionErrors::TypeInferenceFailed;
    auto b = OverloadResolutionErrors::TypeInferenceFailed |
             OverloadResolutionErrors::AmbiguousMatch;
    EXPECT_EQ(a & b, OverloadResolutionErrors::TypeInferenceFailed);
}

TEST(OverloadResolutionErrorsTest, BitwiseXorTogglesFlags)
{
    auto a = OverloadResolutionErrors::ArgumentTypeMismatch |
             OverloadResolutionErrors::TypeInferenceFailed;
    auto toggled = a ^ OverloadResolutionErrors::TypeInferenceFailed;
    EXPECT_EQ(toggled, OverloadResolutionErrors::ArgumentTypeMismatch);
}

TEST(OverloadResolutionErrorsTest, BitwiseNotInvertsFlags)
{
    EXPECT_EQ(~OverloadResolutionErrors::None,
              static_cast<OverloadResolutionErrors>(~static_cast<std::int32_t>(0)));
    // None is all-zero, so ~None is all-one bits; OR-ing it with any flag still
    // carries that flag (the bit is set in ~None).
    EXPECT_EQ((~OverloadResolutionErrors::None) & OverloadResolutionErrors::AmbiguousMatch,
              OverloadResolutionErrors::AmbiguousMatch);
}

// ---------------------------------------------------------------------------
// OverloadResolutionErrors -- the (errors & flag) == flag flag-test idiom the
// OverloadResolution accumulator and the MethodGroupResolveResult /
// CSharpInvocationResolveResult surfaces use.
// ---------------------------------------------------------------------------

TEST(OverloadResolutionErrorsTest, FlagTestIdiomMatchesResolverUsage)
{
    auto errors = OverloadResolutionErrors::ArgumentTypeMismatch |
                  OverloadResolutionErrors::TypeInferenceFailed |
                  OverloadResolutionErrors::AmbiguousMatch;
    // The three accumulated errors test true.
    EXPECT_TRUE((errors & OverloadResolutionErrors::ArgumentTypeMismatch) ==
                OverloadResolutionErrors::ArgumentTypeMismatch);
    EXPECT_TRUE((errors & OverloadResolutionErrors::TypeInferenceFailed) ==
                OverloadResolutionErrors::TypeInferenceFailed);
    EXPECT_TRUE((errors & OverloadResolutionErrors::AmbiguousMatch) ==
                OverloadResolutionErrors::AmbiguousMatch);
    // A flag NOT in the mask tests false.
    EXPECT_FALSE((errors & OverloadResolutionErrors::Inaccessible) ==
                 OverloadResolutionErrors::Inaccessible);
    EXPECT_FALSE((errors & OverloadResolutionErrors::OutVarTypeMismatch) ==
                 OverloadResolutionErrors::OutVarTypeMismatch);
    // None is the empty mask: no flag tests true against it.
    EXPECT_FALSE((OverloadResolutionErrors::None &
                  OverloadResolutionErrors::ArgumentTypeMismatch) ==
                 OverloadResolutionErrors::ArgumentTypeMismatch);
}

TEST(OverloadResolutionErrorsTest, NoneIsTheZeroBaseline)
{
    EXPECT_EQ(OverloadResolutionErrors::None,
              static_cast<OverloadResolutionErrors>(0));
    // OR-ing None into a mask is a no-op (the identity element of the accumulator).
    auto mask = OverloadResolutionErrors::ArgumentTypeMismatch |
               OverloadResolutionErrors::AmbiguousMatch;
    EXPECT_EQ(mask | OverloadResolutionErrors::None, mask);
    EXPECT_EQ(OverloadResolutionErrors::None | OverloadResolutionErrors::None,
              OverloadResolutionErrors::None);
}
