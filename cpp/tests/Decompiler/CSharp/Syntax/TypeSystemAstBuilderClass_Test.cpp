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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the TypeSystemAstBuilder CLASS skeleton (cpp/Decompiler/CSharp/Syntax/
// TypeSystemAstBuilder.hpp, the port of TypeSystemAstBuilder.cs lines 43-265): the
// resolver-holding ctor with its null-guard, the resolver-less ctor, the
// InitProperties defaults (the twelve true-flipping properties + the remaining
// false defaults + the NameLookupMode::Expression default), and the full mutable
// property surface (set-and-read-back round trips). The CSharpResolver the ctor
// takes is the completed mega-class (every Resolve* region landed); the builder
// keeps it alive via the shared_ptr field.
//
// RED protocol: the InitProperties neuter (empty body) makes exactly the two
// true-defaults crux tests fail while every other test passes -- the false backing
// initializers, the Expression enum initializer, the setters, and the null-guard
// are all InitProperties-independent.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/NameLookupMode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace Syntax = ILSpy::Decompiler::CSharp::Syntax;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::CSharp::Resolver::NameLookupMode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;

// The shared compilation (a plain LookupCompilation -- no FindType registration is
// needed for the skeleton; the ctor only resolves the per-compilation conversions
// factory over it). The per-compilation CSharpConversions::Get factory stores its
// instance in the compilation's CacheManager, so the compilation must outlive every
// resolver constructed over it (it does -- a function-local static).
LookupCompilation& Compilation() {
    static LookupCompilation compilation;
    return compilation;
}

// A fresh resolver over the shared compilation (the make_shared discipline the
// resolver's identity-preserving With* early-outs require). The builder stores it as
// a shared_ptr<const CSharpResolver>; the shared_ptr converting constructor hands it
// over.
std::shared_ptr<const CSharpResolver> MakeResolver() {
    return std::make_shared<CSharpResolver>(Compilation());
}

} // namespace

// The C# class is unsealed; the port is not final.
static_assert(!std::is_final_v<Syntax::TypeSystemAstBuilder>);

TEST(TypeSystemAstBuilderClassTest, CtorWithResolverFlipsTheTwelveInitPropertiesTrue)
{
    Syntax::TypeSystemAstBuilder builder(MakeResolver());
    EXPECT_TRUE(builder.UseKeywordsForBuiltinTypes());
    EXPECT_TRUE(builder.UseNullableSpecifierForValueTypes());
    EXPECT_TRUE(builder.ShowAccessibility());
    EXPECT_TRUE(builder.UsePrivateProtectedAccessibility());
    EXPECT_TRUE(builder.ShowModifiers());
    EXPECT_TRUE(builder.ShowBaseTypes());
    EXPECT_TRUE(builder.ShowTypeParameters());
    EXPECT_TRUE(builder.ShowTypeParameterConstraints());
    EXPECT_TRUE(builder.ShowParameterNames());
    EXPECT_TRUE(builder.ShowConstantValues());
    EXPECT_TRUE(builder.UseAliases());
    EXPECT_TRUE(builder.UseSpecialConstants());
}

TEST(TypeSystemAstBuilderClassTest, ResolverLessCtorFlipsTheTwelveInitPropertiesTrue)
{
    // The resolver-less ctor runs the same InitProperties -- both ctors share the
    // identical defaults.
    Syntax::TypeSystemAstBuilder builder;
    EXPECT_TRUE(builder.UseKeywordsForBuiltinTypes());
    EXPECT_TRUE(builder.UseNullableSpecifierForValueTypes());
    EXPECT_TRUE(builder.ShowAccessibility());
    EXPECT_TRUE(builder.UsePrivateProtectedAccessibility());
    EXPECT_TRUE(builder.ShowModifiers());
    EXPECT_TRUE(builder.ShowBaseTypes());
    EXPECT_TRUE(builder.ShowTypeParameters());
    EXPECT_TRUE(builder.ShowTypeParameterConstraints());
    EXPECT_TRUE(builder.ShowParameterNames());
    EXPECT_TRUE(builder.ShowConstantValues());
    EXPECT_TRUE(builder.UseAliases());
    EXPECT_TRUE(builder.UseSpecialConstants());
}

TEST(TypeSystemAstBuilderClassTest, CtorWithNullResolverThrowsInvalidArgument)
{
    // The C# ArgumentNullException ports to std::invalid_argument (the D424
    // base-ctor convention). A null shared_ptr is the C# null reference.
    std::shared_ptr<const CSharpResolver> nullResolver;
    EXPECT_THROW((Syntax::TypeSystemAstBuilder(std::move(nullResolver))),
                 std::invalid_argument);
}

TEST(TypeSystemAstBuilderClassTest, NonInitPropertiesDefaultFalse)
{
    // Every property InitProperties does NOT touch keeps the C# bool field default
    // (false). Verified through both ctors.
    Syntax::TypeSystemAstBuilder withResolver(MakeResolver());
    Syntax::TypeSystemAstBuilder resolverLess;
    const Syntax::TypeSystemAstBuilder& a = withResolver;
    const Syntax::TypeSystemAstBuilder& b = resolverLess;
    EXPECT_FALSE(a.AddTypeReferenceAnnotations());
    EXPECT_FALSE(a.AddResolveResultAnnotations());
    EXPECT_FALSE(a.ShowTypeParametersForUnboundTypes());
    EXPECT_FALSE(a.ShowAttributes());
    EXPECT_FALSE(a.SortAttributes());
    EXPECT_FALSE(a.AlwaysUseShortTypeNames());
    EXPECT_FALSE(a.GenerateBody());
    EXPECT_FALSE(a.UseCustomEvents());
    EXPECT_FALSE(a.ConvertUnboundTypeArguments());
    EXPECT_FALSE(a.PrintIntegralValuesAsHex());
    EXPECT_FALSE(a.SupportInitAccessors());
    EXPECT_FALSE(a.SupportRecordClasses());
    EXPECT_FALSE(a.SupportRecordStructs());
    EXPECT_FALSE(a.SupportUnsignedRightShift());
    EXPECT_FALSE(a.SupportOperatorChecked());
    EXPECT_FALSE(a.AlwaysUseGlobal());
    EXPECT_FALSE(a.SupportExtensionDeclarations());
    EXPECT_FALSE(b.AddTypeReferenceAnnotations());
    EXPECT_FALSE(b.ShowAttributes());
    EXPECT_FALSE(b.GenerateBody());
    EXPECT_FALSE(b.SupportExtensionDeclarations());
}

TEST(TypeSystemAstBuilderClassTest, NameLookupModeDefaultsToExpression)
{
    // The C# `NameLookupMode` property default is `NameLookupMode.Expression`
    // (disambiguated for use in expression context) -- the enum default 0.
    Syntax::TypeSystemAstBuilder withResolver(MakeResolver());
    Syntax::TypeSystemAstBuilder resolverLess;
    EXPECT_EQ(withResolver.NameLookupMode(), NameLookupMode::Expression);
    EXPECT_EQ(resolverLess.NameLookupMode(), NameLookupMode::Expression);
}

TEST(TypeSystemAstBuilderClassTest, EveryPropertyRoundTripsThroughItsSetter)
{
    // Every `{ get; set; }` property is mutable through the lvalue-reference
    // setter and reads back through the const getter.
    Syntax::TypeSystemAstBuilder builder;
    builder.AddTypeReferenceAnnotations() = true;
    builder.AddResolveResultAnnotations() = true;
    builder.ShowAccessibility() = false;
    builder.UsePrivateProtectedAccessibility() = false;
    builder.ShowModifiers() = false;
    builder.ShowBaseTypes() = false;
    builder.ShowTypeParameters() = false;
    builder.ShowTypeParametersForUnboundTypes() = true;
    builder.ShowTypeParameterConstraints() = false;
    builder.ShowParameterNames() = false;
    builder.ShowConstantValues() = false;
    builder.ShowAttributes() = true;
    builder.SortAttributes() = true;
    builder.AlwaysUseShortTypeNames() = true;
    builder.UseKeywordsForBuiltinTypes() = false;
    builder.UseNullableSpecifierForValueTypes() = false;
    builder.NameLookupMode() = NameLookupMode::BaseTypeReference;
    builder.GenerateBody() = true;
    builder.UseCustomEvents() = true;
    builder.ConvertUnboundTypeArguments() = true;
    builder.UseAliases() = false;
    builder.UseSpecialConstants() = false;
    builder.PrintIntegralValuesAsHex() = true;
    builder.SupportInitAccessors() = true;
    builder.SupportRecordClasses() = true;
    builder.SupportRecordStructs() = true;
    builder.SupportUnsignedRightShift() = true;
    builder.SupportOperatorChecked() = true;
    builder.AlwaysUseGlobal() = true;
    builder.SupportExtensionDeclarations() = true;

    const Syntax::TypeSystemAstBuilder& read = builder;
    EXPECT_TRUE(read.AddTypeReferenceAnnotations());
    EXPECT_TRUE(read.AddResolveResultAnnotations());
    EXPECT_FALSE(read.ShowAccessibility());
    EXPECT_FALSE(read.UsePrivateProtectedAccessibility());
    EXPECT_FALSE(read.ShowModifiers());
    EXPECT_FALSE(read.ShowBaseTypes());
    EXPECT_FALSE(read.ShowTypeParameters());
    EXPECT_TRUE(read.ShowTypeParametersForUnboundTypes());
    EXPECT_FALSE(read.ShowTypeParameterConstraints());
    EXPECT_FALSE(read.ShowParameterNames());
    EXPECT_FALSE(read.ShowConstantValues());
    EXPECT_TRUE(read.ShowAttributes());
    EXPECT_TRUE(read.SortAttributes());
    EXPECT_TRUE(read.AlwaysUseShortTypeNames());
    EXPECT_FALSE(read.UseKeywordsForBuiltinTypes());
    EXPECT_FALSE(read.UseNullableSpecifierForValueTypes());
    EXPECT_EQ(read.NameLookupMode(), NameLookupMode::BaseTypeReference);
    EXPECT_TRUE(read.GenerateBody());
    EXPECT_TRUE(read.UseCustomEvents());
    EXPECT_TRUE(read.ConvertUnboundTypeArguments());
    EXPECT_FALSE(read.UseAliases());
    EXPECT_FALSE(read.UseSpecialConstants());
    EXPECT_TRUE(read.PrintIntegralValuesAsHex());
    EXPECT_TRUE(read.SupportInitAccessors());
    EXPECT_TRUE(read.SupportRecordClasses());
    EXPECT_TRUE(read.SupportRecordStructs());
    EXPECT_TRUE(read.SupportUnsignedRightShift());
    EXPECT_TRUE(read.SupportOperatorChecked());
    EXPECT_TRUE(read.AlwaysUseGlobal());
    EXPECT_TRUE(read.SupportExtensionDeclarations());
}

TEST(TypeSystemAstBuilderClassTest, SettingOnePropertyLeavesTheOthersAtTheirDefaults)
{
    // The properties are independent: flipping one leaves every other property at
    // its InitProperties default.
    Syntax::TypeSystemAstBuilder builder;
    builder.ShowAttributes() = true;
    const Syntax::TypeSystemAstBuilder& read = builder;
    EXPECT_TRUE(read.ShowAttributes());
    EXPECT_TRUE(read.ShowAccessibility());
    EXPECT_TRUE(read.ShowModifiers());
    EXPECT_TRUE(read.ShowTypeParameters());
    EXPECT_TRUE(read.UseKeywordsForBuiltinTypes());
    EXPECT_FALSE(read.SortAttributes());
    EXPECT_FALSE(read.GenerateBody());
    EXPECT_EQ(read.NameLookupMode(), NameLookupMode::Expression);
}
