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

// Tests for the `OverloadResolution` class SKELETON (D511) -- the constructor (validation + field init +
// input-property defaults) and the input properties. The C# ctor:
//   if (compilation == null) throw ArgumentNullException;       // compiled out (const ref can't be null)
//   if (arguments == null) throw ArgumentNullException;         // compiled out (owning vector)
//   if (argumentNames == null) argumentNames = new string[arguments.Length];
//   else if (argumentNames.Length != arguments.Length) throw ArgumentException;
//   this.compilation = compilation; this.arguments = arguments; this.argumentNames = argumentNames;
//   if (typeArguments != null && typeArguments.Length > 0) this.explicitlyGivenTypeArguments = typeArguments;
//   this.conversions = conversions ?? CSharpConversions.Get(compilation);  // fallback deferred
//   AllowExpandingParams = true; AllowOptionalParameters = true;
// (AllowImplicitIn defaults true via the auto-property initializer.)
//
// The tests pin:
//  (a) default ctor (null argumentNames/typeArguments) -> argumentNames normalized to all-empty (length
//      == arguments.size()); explicitlyGivenTypeArguments is nullopt; AllowExpandingParams/
//      AllowOptionalParameters/AllowImplicitIn true; CheckForOverflow/IsExtensionMethodInvocation false.
//  (b) explicit argumentNames (matching length) -> stored verbatim.
//  (c) mismatched argumentNames length -> throws `invalid_argument`.
//  (d) non-empty typeArguments -> explicitlyGivenTypeArguments set; empty typeArguments -> nullopt.
//  (e) the `Arguments` getter returns the ctor's arguments (identity).
//  (f) the input properties are mutable (set + get round-trip).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;

const ICompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

ITypePtr ObjectType() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

std::shared_ptr<ResolveResult> TypeArg() {
    return std::make_shared<TypeResolveResult>(ObjectType());
}

std::vector<std::shared_ptr<ResolveResult>> Args(std::size_t n) {
    std::vector<std::shared_ptr<ResolveResult>> v;
    v.reserve(n);
    for (std::size_t i = 0; i < n; i++) v.push_back(TypeArg());
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
// Default ctor (nullopt argumentNames/typeArguments) -> argumentNames normalized to all-empty;
// explicitlyGivenTypeArguments nullopt; defaults: AllowExpandingParams/AllowOptionalParameters/
// AllowImplicitIn true, CheckForOverflow/IsExtensionMethodInvocation false.
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCtorTest, Defaults) {
    OverloadResolution r(Compilation(), Args(3));
    EXPECT_EQ(r.ArgumentNames().size(), 3u);
    EXPECT_TRUE(r.ArgumentNames()[0].empty());
    EXPECT_TRUE(r.ArgumentNames()[1].empty());
    EXPECT_TRUE(r.ArgumentNames()[2].empty());
    EXPECT_FALSE(r.ExplicitlyGivenTypeArguments().has_value());
    EXPECT_TRUE(r.AllowExpandingParams());
    EXPECT_TRUE(r.AllowOptionalParameters());
    EXPECT_TRUE(r.AllowImplicitIn());
    EXPECT_FALSE(r.CheckForOverflow());
    EXPECT_FALSE(r.IsExtensionMethodInvocation());
    // The C# ctor fallback (`conversions ?? CSharpConversions.Get(compilation)`)
    // is now ported: a null conversions resolves to the per-compilation
    // CSharpConversions singleton.
    EXPECT_EQ(r.Conversions(), &CSharpConversions::Get(Compilation()));
}

// An EXPLICIT conversions instance is kept as-is (the C# `conversions ?? ...`
// only fires on null).
TEST(OverloadResolutionCtorTest, ExplicitConversionsAreKept) {
    auto conversions = std::make_shared<CSharpConversions>(Compilation());
    OverloadResolution r(Compilation(), Args(1), std::nullopt, std::nullopt,
        conversions.get());
    EXPECT_EQ(r.Conversions(), conversions.get());
}

// The fallback is the per-compilation SINGLETON: two null-converted
// resolutions over the same compilation share the conversions instance (the
// CacheManager identity).
TEST(OverloadResolutionCtorTest, NullConversionsShareTheCompilationSingleton) {
    OverloadResolution r1(Compilation(), Args(1));
    OverloadResolution r2(Compilation(), Args(2));
    ASSERT_NE(r1.Conversions(), nullptr);
    EXPECT_EQ(r1.Conversions(), r2.Conversions());
}

// ---------------------------------------------------------------------------
// Explicit argumentNames (matching length) -> stored verbatim.
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCtorTest, ExplicitArgumentNames) {
    OverloadResolution r(Compilation(), Args(2),
                         std::optional<std::vector<std::string>>{std::vector<std::string>{"", "b"}});
    ASSERT_EQ(r.ArgumentNames().size(), 2u);
    EXPECT_TRUE(r.ArgumentNames()[0].empty());
    EXPECT_EQ(r.ArgumentNames()[1], "b");
}

// ---------------------------------------------------------------------------
// Mismatched argumentNames length -> throws `invalid_argument`.
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCtorTest, MismatchedArgumentNamesThrows) {
    EXPECT_THROW(
        OverloadResolution(Compilation(), Args(2),
                           std::optional<std::vector<std::string>>{std::vector<std::string>{"a"}}),
        std::invalid_argument);
}

// ---------------------------------------------------------------------------
// Non-empty typeArguments -> explicitlyGivenTypeArguments set; empty -> nullopt.
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCtorTest, TypeArguments) {
    // Non-empty -> set.
    OverloadResolution r1(Compilation(), Args(1),
                          std::nullopt,
                          std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{ObjectType()}});
    ASSERT_TRUE(r1.ExplicitlyGivenTypeArguments().has_value());
    ASSERT_EQ(r1.ExplicitlyGivenTypeArguments()->size(), 1u);
    EXPECT_EQ((*r1.ExplicitlyGivenTypeArguments())[0]->Name(), "Object");

    // Empty present vector -> nullopt (the C# `Length > 0` guard).
    OverloadResolution r2(Compilation(), Args(1),
                          std::nullopt,
                          std::optional<std::vector<ITypePtr>>{std::vector<ITypePtr>{}});
    EXPECT_FALSE(r2.ExplicitlyGivenTypeArguments().has_value());
}

// ---------------------------------------------------------------------------
// The `Arguments` getter returns the ctor's arguments (identity).
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCtorTest, ArgumentsIdentity) {
    auto args = Args(2);
    auto* args0 = args[0].get();
    auto* args1 = args[1].get();
    OverloadResolution r(Compilation(), std::vector<std::shared_ptr<ResolveResult>>{args[0], args[1]});
    ASSERT_EQ(r.Arguments().size(), 2u);
    EXPECT_EQ(r.Arguments()[0].get(), args0);
    EXPECT_EQ(r.Arguments()[1].get(), args1);
}

// ---------------------------------------------------------------------------
// The input properties are mutable (set + get round-trip).
// ---------------------------------------------------------------------------
TEST(OverloadResolutionCtorTest, InputPropertiesMutable) {
    OverloadResolution r(Compilation(), Args(1));
    r.IsExtensionMethodInvocation() = true;
    r.AllowExpandingParams() = false;
    r.AllowOptionalParameters() = false;
    r.AllowImplicitIn() = false;
    r.CheckForOverflow() = true;
    EXPECT_TRUE(r.IsExtensionMethodInvocation());
    EXPECT_FALSE(r.AllowExpandingParams());
    EXPECT_FALSE(r.AllowOptionalParameters());
    EXPECT_FALSE(r.AllowImplicitIn());
    EXPECT_TRUE(r.CheckForOverflow());
}
