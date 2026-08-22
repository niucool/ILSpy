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
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the base-type traversal region of `TypeSystemExtensions` (the port
// of ICSharpCode.Decompiler/TypeSystem/TypeSystemExtensions.cs
// GetAllBaseTypes / GetNonInterfaceBaseTypes / GetAllBaseTypeDefinitions /
// IsDerivedFrom overloads -- the surface the C# `MemberLookup` consumes). The
// load-bearing cruxes are: the post-order list (base types BEFORE derived
// types, diamond-shared bases once), the `Where(d != null).Distinct()` of the
// definitions view (a definition-less base type is skipped, a diamond-shared
// definition is deduped), the null/nullability contract (null `type` throws
// `std::invalid_argument` -- the C# ArgumentNullException; null `baseType` is
// false), the same-compilation guard (mismatched compilations throw
// `std::runtime_error` -- the C# InvalidOperationException), and the
// KnownTypeCode overload's FindType(KnownTypeCode) lookup. The stubs come from
// `LookupStubs.hpp` (definitions wired into hand-built DirectBaseTypes graphs;
// `GetDefinition()` returns `this` -- a definition is its own definition).

#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupTypeDefinition;

namespace {

std::shared_ptr<LookupTypeDefinition> MakeDefinition(
    const TS::ICompilation& compilation,
    std::string name,
    std::string ns,
    TS::TypeKind kind = TS::TypeKind::Class,
    const TS::IModule* module = nullptr,
    TS::KnownTypeCode knownTypeCode = TS::KnownTypeCode::None)
{
    return std::make_shared<LookupTypeDefinition>(
        ns + "." + name, ns, TS::FullTypeName(ns + "." + name), kind,
        TS::Accessibility::Public, compilation, module, knownTypeCode);
}

std::vector<std::string> TypeNames(const std::vector<const TS::IType*>& types)
{
    std::vector<std::string> names;
    for (const TS::IType* t : types)
        names.push_back(t->Name());
    return names;
}

std::vector<std::string> DefinitionNames(const std::vector<const TS::ITypeDefinition*>& defs)
{
    std::vector<std::string> names;
    for (const TS::ITypeDefinition* d : defs)
        names.push_back(d->Name());
    return names;
}

} // namespace

// ---------------------------------------------------------------------------
// GetAllBaseTypes -- post-order, bases before derived; null throws.
// ---------------------------------------------------------------------------

TEST(TypeSystemExtensionsTest, GetAllBaseTypesNullTypeThrowsInvalidArgument)
{
    EXPECT_THROW(TS::GetAllBaseTypes(static_cast<const TS::IType*>(nullptr)),
                 std::invalid_argument);
}

TEST(TypeSystemExtensionsTest, GetAllBaseTypesOrdersBasesBeforeDerived)
{
    LookupCompilation compilation;
    auto object = MakeDefinition(compilation, "Object", "System");
    auto comparable = MakeDefinition(compilation, "IComparable", "System", TS::TypeKind::Interface);
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    stream->AddDirectBaseType(object);
    stream->AddDirectBaseType(comparable);

    EXPECT_EQ(TypeNames(TS::GetAllBaseTypes(stream.get())),
              (std::vector<std::string>{ "Object", "IComparable", "Stream" }));
}

TEST(TypeSystemExtensionsTest, GetAllBaseTypesWithReferenceOverloadMatchesPointerOverload)
{
    LookupCompilation compilation;
    auto object = MakeDefinition(compilation, "Object", "System");
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    stream->AddDirectBaseType(object);

    EXPECT_EQ(TypeNames(TS::GetAllBaseTypes(*stream)),
              (std::vector<std::string>{ "Object", "Stream" }));
}

// ---------------------------------------------------------------------------
// GetNonInterfaceBaseTypes -- interfaces dropped for a class, kept for an
// interface input (the C# doc: "When `type` is an interface, this method will
// also return base interfaces").
// ---------------------------------------------------------------------------

TEST(TypeSystemExtensionsTest, GetNonInterfaceBaseTypesDropsInterfacesForClassInput)
{
    LookupCompilation compilation;
    auto object = MakeDefinition(compilation, "Object", "System");
    auto comparable = MakeDefinition(compilation, "IComparable", "System", TS::TypeKind::Interface);
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    stream->AddDirectBaseType(object);
    stream->AddDirectBaseType(comparable);

    EXPECT_EQ(TypeNames(TS::GetNonInterfaceBaseTypes(stream.get())),
              (std::vector<std::string>{ "Object", "Stream" }));
}

TEST(TypeSystemExtensionsTest, GetNonInterfaceBaseTypesKeepsBaseInterfacesForInterfaceInput)
{
    LookupCompilation compilation;
    auto comparable = MakeDefinition(compilation, "IComparable", "System", TS::TypeKind::Interface);
    auto stream = MakeDefinition(compilation, "IStream", "System.IO", TS::TypeKind::Interface);
    stream->AddDirectBaseType(comparable);

    EXPECT_EQ(TypeNames(TS::GetNonInterfaceBaseTypes(stream.get())),
              (std::vector<std::string>{ "IComparable", "IStream" }));
}

TEST(TypeSystemExtensionsTest, GetNonInterfaceBaseTypesNullTypeThrowsInvalidArgument)
{
    EXPECT_THROW(TS::GetNonInterfaceBaseTypes(static_cast<const TS::IType*>(nullptr)),
                 std::invalid_argument);
}

// ---------------------------------------------------------------------------
// GetAllBaseTypeDefinitions -- Select(GetDefinition).Where(!= null).Distinct().
// ---------------------------------------------------------------------------

TEST(TypeSystemExtensionsTest, GetAllBaseTypeDefinitionsNullTypeThrowsInvalidArgument)
{
    EXPECT_THROW(TS::GetAllBaseTypeDefinitions(static_cast<const TS::IType*>(nullptr)),
                 std::invalid_argument);
}

TEST(TypeSystemExtensionsTest, GetAllBaseTypeDefinitionsDedupsDiamondSharedBase)
{
    LookupCompilation compilation;
    auto object = MakeDefinition(compilation, "Object", "System");
    auto ileft = MakeDefinition(compilation, "ILeft", "N", TS::TypeKind::Interface);
    auto iright = MakeDefinition(compilation, "IRight", "N", TS::TypeKind::Interface);
    auto impl = MakeDefinition(compilation, "Impl", "N");
    ileft->AddDirectBaseType(object);
    iright->AddDirectBaseType(object);
    impl->AddDirectBaseType(ileft);
    impl->AddDirectBaseType(iright);

    EXPECT_EQ(DefinitionNames(TS::GetAllBaseTypeDefinitions(impl.get())),
              (std::vector<std::string>{ "Object", "ILeft", "IRight", "Impl" }));
}

TEST(TypeSystemExtensionsTest, GetAllBaseTypeDefinitionsSkipsDefinitionlessBases)
{
    LookupCompilation compilation;
    // A KnownType (GetDefinition() inherited nullptr) direct base of a
    // definition: the collector visits it, but it contributes no definition.
    auto knownBase = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Object);
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    stream->AddDirectBaseType(knownBase);

    auto defs = TS::GetAllBaseTypeDefinitions(stream.get());
    ASSERT_EQ(defs.size(), 1u);
    EXPECT_EQ(defs[0], stream.get());
}

// ---------------------------------------------------------------------------
// IsDerivedFrom(ITypeDefinition) -- the reflexive/transitive closure, the null
// / unrelated cases, and the same-compilation guard.
// ---------------------------------------------------------------------------

TEST(TypeSystemExtensionsTest, IsDerivedFromSelfIsTrue)
{
    LookupCompilation compilation;
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    EXPECT_TRUE(TS::IsDerivedFrom(*stream, static_cast<const TS::ITypeDefinition*>(stream.get())));
}

TEST(TypeSystemExtensionsTest, IsDerivedFromTransitiveGrandparentIsTrue)
{
    LookupCompilation compilation;
    auto object = MakeDefinition(compilation, "Object", "System");
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    auto memoryStream = MakeDefinition(compilation, "MemoryStream", "System.IO");
    stream->AddDirectBaseType(object);
    memoryStream->AddDirectBaseType(stream);

    EXPECT_TRUE(TS::IsDerivedFrom(*memoryStream, static_cast<const TS::ITypeDefinition*>(object.get())));
}

TEST(TypeSystemExtensionsTest, IsDerivedFromUnrelatedIsFalse)
{
    LookupCompilation compilation;
    auto left = MakeDefinition(compilation, "Left", "N");
    auto right = MakeDefinition(compilation, "Right", "N");
    EXPECT_FALSE(TS::IsDerivedFrom(*left, static_cast<const TS::ITypeDefinition*>(right.get())));
}

TEST(TypeSystemExtensionsTest, IsDerivedFromNullBaseTypeIsFalse)
{
    LookupCompilation compilation;
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    EXPECT_FALSE(TS::IsDerivedFrom(*stream, static_cast<const TS::ITypeDefinition*>(nullptr)));
}

TEST(TypeSystemExtensionsTest, IsDerivedFromDifferentCompilationsThrowsRuntimeError)
{
    LookupCompilation compilationA;
    LookupCompilation compilationB;
    auto left = MakeDefinition(compilationA, "Left", "N");
    auto right = MakeDefinition(compilationB, "Right", "N");
    // The C# `throw new InvalidOperationException("Both arguments to
    // IsDerivedFrom() must be from the same compilation.")`.
    EXPECT_THROW(TS::IsDerivedFrom(*left, static_cast<const TS::ITypeDefinition*>(right.get())),
                 std::runtime_error);
}

// ---------------------------------------------------------------------------
// IsDerivedFrom(KnownTypeCode) -- the FindType lookup through the compilation.
// ---------------------------------------------------------------------------

TEST(TypeSystemExtensionsTest, IsDerivedFromKnownTypeResolvesThroughFindType)
{
    LookupCompilation compilation;
    auto object = MakeDefinition(compilation, "Object", "System",
                                 TS::TypeKind::Class, nullptr, TS::KnownTypeCode::Object);
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    stream->AddDirectBaseType(object);
    compilation.RegisterKnownType(TS::KnownTypeCode::Object, object.get());

    EXPECT_TRUE(TS::IsDerivedFrom(*stream, TS::KnownTypeCode::Object));
}

TEST(TypeSystemExtensionsTest, IsDerivedFromKnownTypeNoneIsFalse)
{
    LookupCompilation compilation;
    auto stream = MakeDefinition(compilation, "Stream", "System.IO");
    EXPECT_FALSE(TS::IsDerivedFrom(*stream, TS::KnownTypeCode::None));
}

TEST(TypeSystemExtensionsTest, IsDerivedFromKnownTypeNotDerivedIsFalse)
{
    LookupCompilation compilation;
    auto object = MakeDefinition(compilation, "Object", "System",
                                 TS::TypeKind::Class, nullptr, TS::KnownTypeCode::Object);
    auto string = MakeDefinition(compilation, "String", "System",
                                 TS::TypeKind::Class, nullptr, TS::KnownTypeCode::String);
    auto exception = MakeDefinition(compilation, "Exception", "System",
                                    TS::TypeKind::Class, nullptr, TS::KnownTypeCode::Exception);
    string->AddDirectBaseType(object);
    exception->AddDirectBaseType(object);
    compilation.RegisterKnownType(TS::KnownTypeCode::Object, object.get());
    compilation.RegisterKnownType(TS::KnownTypeCode::String, string.get());
    compilation.RegisterKnownType(TS::KnownTypeCode::Exception, exception.get());

    EXPECT_FALSE(TS::IsDerivedFrom(*string, TS::KnownTypeCode::Exception));
    EXPECT_TRUE(TS::IsDerivedFrom(*exception, TS::KnownTypeCode::Object));
}
