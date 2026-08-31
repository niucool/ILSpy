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
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupMethod;
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

// ---------------------------------------------------------------------------
// GetAllTypeDefinitions / GetTopLevelTypeDefinitions (TypeSystemExtensions.cs
// lines 462-478) -- the Modules.SelectMany over each module's type table.
// ---------------------------------------------------------------------------

// An unconfigured compilation yields the empty scan (no registered type tables).
TEST(TypeSystemExtensionsTest, GetAllTypeDefinitionsEmptyCompilationYieldsEmpty)
{
    LookupCompilation compilation;
    EXPECT_TRUE(TS::GetAllTypeDefinitions(compilation).empty());
    EXPECT_TRUE(TS::GetTopLevelTypeDefinitions(compilation).empty());
}

// The SelectMany concatenates the MAIN module's table first, then the extra
// (referenced) modules', in module-list order; the entries are the registered
// instances (pointer identity).
TEST(TypeSystemExtensionsTest, GetAllTypeDefinitionsConcatenatesModuleTypeTables)
{
    LookupCompilation compilation;
    auto mainType = MakeDefinition(compilation, "MainType", "N");
    compilation.AddTypeDefinition(mainType.get());
    auto extraModule = std::make_unique<TS::TestSupport::LookupModule>(compilation, "Extra");
    auto extraType1 = MakeDefinition(compilation, "ExtraType1", "N");
    auto extraType2 = MakeDefinition(compilation, "ExtraType2", "N");
    extraModule->AddTypeDefinition(extraType1.get());
    extraModule->AddTypeDefinition(extraType2.get());
    compilation.AddModule(extraModule.get());

    std::vector<const TS::ITypeDefinition*> all = TS::GetAllTypeDefinitions(compilation);
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0], mainType.get());
    EXPECT_EQ(all[1], extraType1.get());
    EXPECT_EQ(all[2], extraType2.get());
}

// The TopLevelTypeDefinitions SelectMany mirrors the concatenation over the
// module-level top-level tables.
TEST(TypeSystemExtensionsTest, GetTopLevelTypeDefinitionsConcatenatesModuleTables)
{
    LookupCompilation compilation;
    auto mainType = MakeDefinition(compilation, "MainType", "N");
    compilation.AddTopLevelTypeDefinition(mainType.get());
    auto extraModule = std::make_unique<TS::TestSupport::LookupModule>(compilation, "Extra");
    auto extraType = MakeDefinition(compilation, "ExtraType", "N");
    extraModule->AddTopLevelTypeDefinition(extraType.get());
    compilation.AddModule(extraModule.get());

    std::vector<const TS::ITypeDefinition*> topLevel = TS::GetTopLevelTypeDefinitions(compilation);
    ASSERT_EQ(topLevel.size(), 2u);
    EXPECT_EQ(topLevel[0], mainType.get());
    EXPECT_EQ(topLevel[1], extraType.get());
}

// The two scans read DIFFERENT module tables: `TypeDefinitions` includes the
// nested types while `TopLevelTypeDefinitions` carries only the non-nested ones
// (a module registering [Top, Nested] to TypeDefinitions but only [Top] to
// TopLevelTypeDefinitions yields both from GetAll and just Top from
// GetTopLevel -- the divergence crux).
TEST(TypeSystemExtensionsTest, GetTopLevelTypeDefinitionsExcludesNestedTypes)
{
    LookupCompilation compilation;
    auto top = MakeDefinition(compilation, "Top", "N");
    auto nested = MakeDefinition(compilation, "Nested", "N");
    compilation.AddTypeDefinition(top.get());
    compilation.AddTypeDefinition(nested.get());
    compilation.AddTopLevelTypeDefinition(top.get());

    std::vector<const TS::ITypeDefinition*> all = TS::GetAllTypeDefinitions(compilation);
    std::vector<const TS::ITypeDefinition*> topLevel = TS::GetTopLevelTypeDefinitions(compilation);
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0], top.get());
    EXPECT_EQ(all[1], nested.get());
    ASSERT_EQ(topLevel.size(), 1u);
    EXPECT_EQ(topLevel[0], top.get());
}

// ---------------------------------------------------------------------------
// GetElementTypeFromIEnumerable (TypeSystemExtensions.cs line 757) -- the
// foreach element-type extraction: the first IEnumerable<T> / IEnumerator<T>
// (with allowIEnumerator) in the base-type closure, the Object element for the
// non-generic interfaces, the UnknownType null object with isGeneric == null
// for neither.
// ---------------------------------------------------------------------------

// The first generic IEnumerable<T> in the base-type closure yields its type
// argument (pointer identity) and isGeneric = true. A ParameterizedType over
// the open-generic definition is itself in its own closure (the collector's
// self-entry; GetDefinition delegates to the generic).
TEST(TypeSystemExtensionsTest, GetElementTypeFromIEnumerableGenericYieldsTypeArgument)
{
    LookupCompilation compilation;
    auto int32 = MakeDefinition(compilation, "Int32", "System", TS::TypeKind::Struct,
                                nullptr, TS::KnownTypeCode::Int32);
    auto enumerableOfT = MakeDefinition(compilation, "IEnumerable", "System.Collections.Generic",
                                       TS::TypeKind::Interface, nullptr,
                                       TS::KnownTypeCode::IEnumerableOfT);
    auto collection = std::make_shared<TS::ParameterizedType>(
        enumerableOfT, std::vector<TS::ITypePtr>{ int32 });

    std::optional<bool> isGeneric;
    TS::ITypePtr element = TS::GetElementTypeFromIEnumerable(*collection, compilation,
                                                              /*allowIEnumerator*/ false,
                                                              isGeneric);
    EXPECT_EQ(element.get(), int32.get());
    ASSERT_TRUE(isGeneric.has_value());
    EXPECT_TRUE(*isGeneric);
}

// The IEnumerator<T> shape fires ONLY with allowIEnumerator = true.
TEST(TypeSystemExtensionsTest, GetElementTypeFromIEnumerableIEnumeratorOfTRequiresAllowIEnumerator)
{
    LookupCompilation compilation;
    auto int32 = MakeDefinition(compilation, "Int32", "System", TS::TypeKind::Struct,
                                nullptr, TS::KnownTypeCode::Int32);
    auto enumeratorOfT = MakeDefinition(compilation, "IEnumerator", "System.Collections.Generic",
                                        TS::TypeKind::Interface, nullptr,
                                        TS::KnownTypeCode::IEnumeratorOfT);
    auto collection = std::make_shared<TS::ParameterizedType>(
        enumeratorOfT, std::vector<TS::ITypePtr>{ int32 });

    std::optional<bool> isGeneric;
    TS::ITypePtr element = TS::GetElementTypeFromIEnumerable(*collection, compilation,
                                                              /*allowIEnumerator*/ true,
                                                              isGeneric);
    EXPECT_EQ(element.get(), int32.get());
    ASSERT_TRUE(isGeneric.has_value());
    EXPECT_TRUE(*isGeneric);

    // Without the flag the IEnumerator<T> base matches neither arm: neither
    // generic (gated) nor non-generic (the code is IEnumeratorOfT, not
    // IEnumerator), so the walk finds nothing.
    std::optional<bool> isGeneric2;
    TS::ITypePtr element2 = TS::GetElementTypeFromIEnumerable(*collection, compilation,
                                                               /*allowIEnumerator*/ false,
                                                               isGeneric2);
    EXPECT_EQ(element2->Kind(), TS::TypeKind::Unknown);
    EXPECT_FALSE(isGeneric2.has_value());
}

// The non-generic System.Collections.IEnumerable in the base-type closure
// yields the FindType(Object) registered instance and isGeneric = false.
TEST(TypeSystemExtensionsTest, GetElementTypeFromIEnumerableNonGenericYieldsObject)
{
    LookupCompilation compilation;
    auto objectDef = MakeDefinition(compilation, "Object", "System", TS::TypeKind::Class,
                                    nullptr, TS::KnownTypeCode::Object);
    compilation.RegisterKnownType(TS::KnownTypeCode::Object, objectDef.get());
    auto ienumerable = MakeDefinition(compilation, "IEnumerable", "System.Collections",
                                       TS::TypeKind::Interface, nullptr,
                                       TS::KnownTypeCode::IEnumerable);
    auto collection = MakeDefinition(compilation, "Collection", "N");
    collection->AddDirectBaseType(ienumerable);

    std::optional<bool> isGeneric;
    TS::ITypePtr element = TS::GetElementTypeFromIEnumerable(*collection, compilation,
                                                              /*allowIEnumerator*/ false,
                                                              isGeneric);
    EXPECT_EQ(element.get(), objectDef.get());
    ASSERT_TRUE(isGeneric.has_value());
    EXPECT_FALSE(*isGeneric);
}

// The non-generic IEnumerator base fires the foundNonGenericIEnumerable fold
// only with allowIEnumerator = true.
TEST(TypeSystemExtensionsTest, GetElementTypeFromIEnumerableNonGenericIEnumeratorRequiresAllowIEnumerator)
{
    LookupCompilation compilation;
    auto objectDef = MakeDefinition(compilation, "Object", "System", TS::TypeKind::Class,
                                    nullptr, TS::KnownTypeCode::Object);
    compilation.RegisterKnownType(TS::KnownTypeCode::Object, objectDef.get());
    auto ienumerator = MakeDefinition(compilation, "IEnumerator", "System.Collections",
                                       TS::TypeKind::Interface, nullptr,
                                       TS::KnownTypeCode::IEnumerator);
    auto collection = MakeDefinition(compilation, "Collection", "N");
    collection->AddDirectBaseType(ienumerator);

    std::optional<bool> isGeneric;
    TS::ITypePtr element = TS::GetElementTypeFromIEnumerable(*collection, compilation,
                                                              /*allowIEnumerator*/ true,
                                                              isGeneric);
    EXPECT_EQ(element.get(), objectDef.get());
    ASSERT_TRUE(isGeneric.has_value());
    EXPECT_FALSE(*isGeneric);

    // Without the flag the non-generic IEnumerator base is invisible too.
    std::optional<bool> isGeneric2;
    TS::ITypePtr element2 = TS::GetElementTypeFromIEnumerable(*collection, compilation,
                                                               /*allowIEnumerator*/ false,
                                                               isGeneric2);
    EXPECT_EQ(element2->Kind(), TS::TypeKind::Unknown);
    EXPECT_FALSE(isGeneric2.has_value());
}

// A base type whose DEFINITION carries IEnumerableOfT but which is not itself
// a ParameterizedType (the bare open-generic definition) does not match: the C#
// `pt != null` guard continues the walk and neither arm fires.
TEST(TypeSystemExtensionsTest, GetElementTypeFromIEnumerableBareDefinitionDoesNotYieldGeneric)
{
    LookupCompilation compilation;
    auto enumerableOfT = MakeDefinition(compilation, "IEnumerable", "System.Collections.Generic",
                                       TS::TypeKind::Interface, nullptr,
                                       TS::KnownTypeCode::IEnumerableOfT);
    auto collection = MakeDefinition(compilation, "Collection", "N");
    collection->AddDirectBaseType(enumerableOfT);

    std::optional<bool> isGeneric;
    TS::ITypePtr element = TS::GetElementTypeFromIEnumerable(*collection, compilation,
                                                              /*allowIEnumerator*/ false,
                                                              isGeneric);
    EXPECT_EQ(element->Kind(), TS::TypeKind::Unknown);
    EXPECT_FALSE(isGeneric.has_value());
}

// A type with no IEnumerable / IEnumerator base of either arity yields the
// UnknownType null object with isGeneric = null.
TEST(TypeSystemExtensionsTest, GetElementTypeFromIEnumerableNoEnumerableBaseYieldsUnknown)
{
    LookupCompilation compilation;
    auto collection = MakeDefinition(compilation, "Collection", "N");

    std::optional<bool> isGeneric;
    TS::ITypePtr element = TS::GetElementTypeFromIEnumerable(*collection, compilation,
                                                              /*allowIEnumerator*/ false,
                                                              isGeneric);
    EXPECT_EQ(element->Kind(), TS::TypeKind::Unknown);
    EXPECT_FALSE(isGeneric.has_value());
}

// ---------------------------------------------------------------------------
// IsCompilerGeneratedOrIsInCompilerGeneratedClass / IsPotentialClosure /
// IsClosureParameter / IsDefaultValueAssignmentAllowed (the closure-parameter
// and default-value-assignment region: NRExtensions.cs lines 26-46,
// TransformDisplayClassUsage.cs IsPotentialClosure, LocalFunctionDecompiler.cs
// line 575 IsClosureParameter, TypeSystemExtensions.cs line 681).
// ---------------------------------------------------------------------------

namespace {

// A `LookupTypeDefinition` subclass with a configurable `[CompilerGenerated]`
// (the TaskType_Test CustomTaskDef pattern: the base stub hardcodes
// `HasAttribute` to false, so the closure tests override the attribute
// accessors).
class CompilerGeneratedDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetCompilerGenerated(bool value) { compilerGenerated_ = value; }

    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return compilerGenerated_ && attribute == TS::KnownAttribute::CompilerGenerated;
    }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }

private:
    bool compilerGenerated_ = false;
};

// A fully configurable `IParameter` stub (the CalculateCandidate_Test
// TestParameter pattern extended with the fields the default-value region
// reads: reference kind, constant-in-signature, owner, lifetime, constant
// value).
class DefaultTestParameter : public TS::IParameter {
public:
    explicit DefaultTestParameter(TS::ITypePtr type, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)) {}

    void SetOwner(const TS::IParameterizedMember* owner) { owner_ = owner; }
    void SetReferenceKind(TS::ReferenceKind rk) { referenceKind_ = rk; }
    void SetIsParams(bool v) { isParams_ = v; }
    void SetIsOptional(bool v) { isOptional_ = v; }
    void SetHasConstantValueInSignature(bool v) { hasConstantValueInSignature_ = v; }
    void SetLifetimeScopedRef(bool v) { lifetime_.ScopedRef(v); }
    void SetConstantValue(std::any v) { constantValue_ = std::move(v); }

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const TS::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return constantValue_; }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    TS::ReferenceKind ReferenceKind() const override { return referenceKind_; }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return isOptional_; }
    bool HasConstantValueInSignature() const override { return hasConstantValueInSignature_; }
    const TS::IParameterizedMember* Owner() const override { return owner_; }
    TS::LifetimeAnnotation Lifetime() const override { return lifetime_; }

private:
    std::string name_;
    TS::ITypePtr type_;
    const TS::IParameterizedMember* owner_ = nullptr;
    TS::ReferenceKind referenceKind_ = TS::ReferenceKind::None;
    bool isParams_ = false;
    bool isOptional_ = false;
    bool hasConstantValueInSignature_ = false;
    TS::LifetimeAnnotation lifetime_;
    std::any constantValue_;
};

// A helper building an owner method over a registered Outer type + Int32
// definition, for the subsequent-parameter walk tests (each parameter's
// Owner is wired to the same method).
struct OwnerFixture {
    LookupCompilation compilation;
    std::shared_ptr<LookupTypeDefinition> outer;
    std::shared_ptr<LookupTypeDefinition> int32;
    std::shared_ptr<LookupMethod> owner;

    OwnerFixture()
        : outer(MakeDefinition(compilation, "Outer", "N")),
          int32(MakeDefinition(compilation, "Int32", "System", TS::TypeKind::Struct,
                               nullptr, TS::KnownTypeCode::Int32)),
          owner(std::make_shared<LookupMethod>("M", compilation))
    {
        owner->SetDeclaringTypeDefinition(outer.get());
    }
};

} // namespace

// A null entity is not compiler-generated (the C# null-accepting extension).
TEST(TypeSystemExtensionsTest, IsCompilerGeneratedNullEntityReturnsFalse)
{
    EXPECT_FALSE(TS::IsCompilerGeneratedOrIsInCompilerGeneratedClass(nullptr));
}

// An entity without the attribute (and without a declaring chain carrying it)
// is not compiler-generated.
TEST(TypeSystemExtensionsTest, IsCompilerGeneratedPlainEntityReturnsFalse)
{
    LookupCompilation compilation;
    auto plain = MakeDefinition(compilation, "Plain", "N");
    EXPECT_FALSE(TS::IsCompilerGeneratedOrIsInCompilerGeneratedClass(plain.get()));
}

// The entity's own [CompilerGenerated] satisfies the check.
TEST(TypeSystemExtensionsTest, IsCompilerGeneratedOwnAttributeReturnsTrue)
{
    LookupCompilation compilation;
    auto generated = std::make_shared<CompilerGeneratedDef>(
        "N.Gen", "N", TS::FullTypeName("N.Gen"), TS::TypeKind::Class,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    generated->SetCompilerGenerated(true);
    EXPECT_TRUE(TS::IsCompilerGeneratedOrIsInCompilerGeneratedClass(generated.get()));
}

// A [CompilerGenerated] on any type in the entity's nesting chain (here one
// level up) satisfies the check.
TEST(TypeSystemExtensionsTest, IsCompilerGeneratedDeclaringChainAttributeReturnsTrue)
{
    LookupCompilation compilation;
    auto generatedOuter = std::make_shared<CompilerGeneratedDef>(
        "N.Outer", "N", TS::FullTypeName("N.Outer"), TS::TypeKind::Class,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    generatedOuter->SetCompilerGenerated(true);
    LookupTypeDefinition inner("N.Inner", "N", TS::FullTypeName("N.Inner"),
                               TS::TypeKind::Class, TS::Accessibility::Public,
                               compilation, nullptr, TS::KnownTypeCode::None);
    inner.SetDeclaringTypeDefinition(generatedOuter.get());
    EXPECT_TRUE(TS::IsCompilerGeneratedOrIsInCompilerGeneratedClass(&inner));
}

// A null display class is not a potential closure.
TEST(TypeSystemExtensionsTest, IsPotentialClosureNullDisplayClassReturnsFalse)
{
    LookupCompilation compilation;
    auto outer = MakeDefinition(compilation, "Outer", "N");
    EXPECT_FALSE(TS::IsPotentialClosure(outer.get(), nullptr));
}

// A non-compiler-generated struct in the same tree is not a potential closure.
TEST(TypeSystemExtensionsTest, IsPotentialClosureNonGeneratedReturnsFalse)
{
    LookupCompilation compilation;
    auto outer = MakeDefinition(compilation, "Outer", "N");
    auto plainStruct = MakeDefinition(compilation, "S", "N", TS::TypeKind::Struct);
    plainStruct->SetDeclaringTypeDefinition(outer.get());
    EXPECT_FALSE(TS::IsPotentialClosure(outer.get(), plainStruct.get()));
}

// A non-Struct/non-Class kind (an interface) is rejected by the Kind switch.
TEST(TypeSystemExtensionsTest, IsPotentialClosureInterfaceKindReturnsFalse)
{
    LookupCompilation compilation;
    auto outer = MakeDefinition(compilation, "Outer", "N");
    auto generatedInterface = std::make_shared<CompilerGeneratedDef>(
        "N.IFace", "N", TS::FullTypeName("N.IFace"), TS::TypeKind::Interface,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    generatedInterface->SetCompilerGenerated(true);
    generatedInterface->SetDeclaringTypeDefinition(outer.get());
    EXPECT_FALSE(TS::IsPotentialClosure(outer.get(), generatedInterface.get()));
}

// A compiler-generated struct declared inside the decompiled type is a
// potential closure.
TEST(TypeSystemExtensionsTest, IsPotentialClosureGeneratedStructInSameTreeReturnsTrue)
{
    LookupCompilation compilation;
    auto outer = MakeDefinition(compilation, "Outer", "N");
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.Outer.Display", "N", TS::FullTypeName("N.Outer.Display"), TS::TypeKind::Struct,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    displayClass->SetDeclaringTypeDefinition(outer.get());
    EXPECT_TRUE(TS::IsPotentialClosure(outer.get(), displayClass.get()));
}

// A compiler-generated class whose only direct base is object is a potential
// closure; one implementing an interface is not (unless the flag allows it).
TEST(TypeSystemExtensionsTest, IsPotentialClosureClassBaseTypeGate)
{
    LookupCompilation compilation;
    auto object = MakeDefinition(compilation, "Object", "System", TS::TypeKind::Class,
                                 nullptr, TS::KnownTypeCode::Object);
    auto outer = MakeDefinition(compilation, "Outer", "N");
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.Outer.Display", "N", TS::FullTypeName("N.Outer.Display"), TS::TypeKind::Class,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    displayClass->SetDeclaringTypeDefinition(outer.get());

    displayClass->AddDirectBaseType(object);
    EXPECT_TRUE(TS::IsPotentialClosure(outer.get(), displayClass.get()));

    auto iface = MakeDefinition(compilation, "IFace", "N", TS::TypeKind::Interface);
    auto implementing = std::make_shared<CompilerGeneratedDef>(
        "N.Outer.Impl", "N", TS::FullTypeName("N.Outer.Impl"), TS::TypeKind::Class,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    implementing->SetCompilerGenerated(true);
    implementing->SetDeclaringTypeDefinition(outer.get());
    implementing->AddDirectBaseType(iface);
    EXPECT_FALSE(TS::IsPotentialClosure(outer.get(), implementing.get()));
    EXPECT_TRUE(TS::IsPotentialClosure(outer.get(), implementing.get(),
                                       /*allowTypeImplementingInterfaces*/ true));
}

// A compiler-generated struct in an unrelated nesting tree is not a potential
// closure.
TEST(TypeSystemExtensionsTest, IsPotentialClosureUnrelatedTreeReturnsFalse)
{
    LookupCompilation compilation;
    auto outer = MakeDefinition(compilation, "Outer", "N");
    auto otherOuter = MakeDefinition(compilation, "OtherOuter", "N");
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.OtherOuter.Display", "N", TS::FullTypeName("N.OtherOuter.Display"),
        TS::TypeKind::Struct, TS::Accessibility::Public, compilation, nullptr,
        TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    displayClass->SetDeclaringTypeDefinition(otherOuter.get());
    EXPECT_FALSE(TS::IsPotentialClosure(outer.get(), displayClass.get()));
}

// Both types sharing a common ancestor satisfies the tree check (the
// display class nested under a sibling of the decompiled type).
TEST(TypeSystemExtensionsTest, IsPotentialClosureCommonAncestorSatisfiesTreeCheck)
{
    LookupCompilation compilation;
    auto common = MakeDefinition(compilation, "Common", "N");
    auto outer = MakeDefinition(compilation, "Outer", "N");
    outer->SetDeclaringTypeDefinition(common.get());
    auto sibling = MakeDefinition(compilation, "Sibling", "N");
    sibling->SetDeclaringTypeDefinition(common.get());
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.Sibling.Display", "N", TS::FullTypeName("N.Sibling.Display"),
        TS::TypeKind::Struct, TS::Accessibility::Public, compilation, nullptr,
        TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    displayClass->SetDeclaringTypeDefinition(sibling.get());
    EXPECT_TRUE(TS::IsPotentialClosure(outer.get(), displayClass.get()));
}

// A null decompiled type walks zero ancestors and yields false (the C#
// `while (decompiledTypeDefinitionOrAncestor != null)` loop body never runs).
TEST(TypeSystemExtensionsTest, IsPotentialClosureNullDecompiledTypeReturnsFalse)
{
    LookupCompilation compilation;
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.Display", "N", TS::FullTypeName("N.Display"), TS::TypeKind::Struct,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    EXPECT_FALSE(TS::IsPotentialClosure(nullptr, displayClass.get()));
}

// A by-ref parameter whose element is not a closure shape is not a closure
// parameter; a definitionless element (a KnownType placeholder) is not
// either; a compiler-generated class display class with only-object bases
// is.
TEST(TypeSystemExtensionsTest, IsClosureParameterNonClosureShapesReturnFalse)
{
    LookupCompilation compilation;
    auto outer = MakeDefinition(compilation, "Outer", "N");
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.Outer.Display", "N", TS::FullTypeName("N.Outer.Display"), TS::TypeKind::Struct,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    displayClass->SetDeclaringTypeDefinition(outer.get());

    // A plain (non-by-ref) parameter.
    DefaultTestParameter plainParam(displayClass);
    EXPECT_FALSE(TS::IsClosureParameter(&plainParam, outer.get()));

    // A by-ref parameter over a definitionless element.
    DefaultTestParameter definitionless(
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32));
    EXPECT_FALSE(TS::IsClosureParameter(&definitionless, outer.get()));

    // A by-ref parameter over a non-compiler-generated struct.
    auto plainStruct = MakeDefinition(compilation, "S", "N", TS::TypeKind::Struct);
    plainStruct->SetDeclaringTypeDefinition(outer.get());
    DefaultTestParameter plainStructParam(
        std::make_shared<TS::ByReferenceType>(plainStruct));
    EXPECT_FALSE(TS::IsClosureParameter(&plainStructParam, outer.get()));

    // A by-ref parameter over a compiler-generated CLASS display class: the
    // `type.Kind == TypeKind.Struct` check rejects a Class kind even when the
    // display-class shape would pass `IsPotentialClosure` (the C# closure
    // parameters are the hoisted struct display classes).
    auto classDisplay = std::make_shared<CompilerGeneratedDef>(
        "N.Outer.ClassDisplay", "N", TS::FullTypeName("N.Outer.ClassDisplay"),
        TS::TypeKind::Class, TS::Accessibility::Public, compilation, nullptr,
        TS::KnownTypeCode::None);
    classDisplay->SetCompilerGenerated(true);
    classDisplay->SetDeclaringTypeDefinition(outer.get());
    DefaultTestParameter classDisplayParam(
        std::make_shared<TS::ByReferenceType>(classDisplay));
    EXPECT_FALSE(TS::IsClosureParameter(&classDisplayParam, outer.get()));
}

// A by-ref parameter over a compiler-generated struct declared inside the
// current type is a closure parameter.
TEST(TypeSystemExtensionsTest, IsClosureParameterClosureStructReturnsTrue)
{
    LookupCompilation compilation;
    auto outer = MakeDefinition(compilation, "Outer", "N");
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.Outer.Display", "N", TS::FullTypeName("N.Outer.Display"), TS::TypeKind::Struct,
        TS::Accessibility::Public, compilation, nullptr, TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    displayClass->SetDeclaringTypeDefinition(outer.get());
    DefaultTestParameter closureParam(
        std::make_shared<TS::ByReferenceType>(displayClass));
    EXPECT_TRUE(TS::IsClosureParameter(&closureParam, outer.get()));
}

// The individual check rejects a non-optional parameter.
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentNonOptionalReturnsFalse)
{
    LookupCompilation compilation;
    auto int32 = MakeDefinition(compilation, "Int32", "System", TS::TypeKind::Struct,
                                nullptr, TS::KnownTypeCode::Int32);
    DefaultTestParameter param(int32);
    param.SetIsOptional(false);
    param.SetHasConstantValueInSignature(true);
    EXPECT_FALSE(TS::IsDefaultValueAssignmentAllowed(param));
}

// The individual check rejects a parameter without the constant in its
// signature.
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentConstantNotInSignatureReturnsFalse)
{
    LookupCompilation compilation;
    auto int32 = MakeDefinition(compilation, "Int32", "System", TS::TypeKind::Struct,
                                nullptr, TS::KnownTypeCode::Int32);
    DefaultTestParameter param(int32);
    param.SetIsOptional(true);
    param.SetHasConstantValueInSignature(false);
    EXPECT_FALSE(TS::IsDefaultValueAssignmentAllowed(param));
}

// The individual check rejects `ref` and `out` parameters, but accepts `in`
// and `ref readonly` ones.
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentReferenceKindGate)
{
    LookupCompilation compilation;
    auto int32 = MakeDefinition(compilation, "Int32", "System", TS::TypeKind::Struct,
                                nullptr, TS::KnownTypeCode::Int32);
    DefaultTestParameter refParam(int32);
    refParam.SetIsOptional(true);
    refParam.SetHasConstantValueInSignature(true);
    refParam.SetReferenceKind(TS::ReferenceKind::Ref);
    EXPECT_FALSE(TS::IsDefaultValueAssignmentAllowed(refParam));

    DefaultTestParameter outParam(int32);
    outParam.SetIsOptional(true);
    outParam.SetHasConstantValueInSignature(true);
    outParam.SetReferenceKind(TS::ReferenceKind::Out);
    EXPECT_FALSE(TS::IsDefaultValueAssignmentAllowed(outParam));

    DefaultTestParameter inParam(int32);
    inParam.SetIsOptional(true);
    inParam.SetHasConstantValueInSignature(true);
    inParam.SetReferenceKind(TS::ReferenceKind::In);
    EXPECT_TRUE(TS::IsDefaultValueAssignmentAllowed(inParam));

    DefaultTestParameter refReadOnlyParam(int32);
    refReadOnlyParam.SetIsOptional(true);
    refReadOnlyParam.SetHasConstantValueInSignature(true);
    refReadOnlyParam.SetReferenceKind(TS::ReferenceKind::RefReadOnly);
    EXPECT_TRUE(TS::IsDefaultValueAssignmentAllowed(refReadOnlyParam));
}

// A parameter that individually passes but has no owner returns true (the
// C# "Shouldn't happen, but we need to check for it" arm).
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentOwnerLessParameterReturnsTrue)
{
    LookupCompilation compilation;
    auto int32 = MakeDefinition(compilation, "Int32", "System", TS::TypeKind::Struct,
                                nullptr, TS::KnownTypeCode::Int32);
    DefaultTestParameter param(int32);
    param.SetIsOptional(true);
    param.SetHasConstantValueInSignature(true);
    param.SetConstantValue(std::any(std::int32_t(5)));
    EXPECT_TRUE(TS::IsDefaultValueAssignmentAllowed(param));
}

// The target being the LAST parameter passes the walk trivially (the backward
// walk reaches it before checking any other parameter).
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentLastParameterReturnsTrue)
{
    OwnerFixture fx;
    DefaultTestParameter required(fx.int32, "b");
    required.SetOwner(fx.owner.get());
    DefaultTestParameter target(fx.int32, "a");
    target.SetIsOptional(true);
    target.SetHasConstantValueInSignature(true);
    target.SetOwner(fx.owner.get());
    fx.owner->SetParameters({&required, &target});

    EXPECT_TRUE(TS::IsDefaultValueAssignmentAllowed(target));
}

// A subsequent (later-position) optional parameter does not block the target's
// default.
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentSubsequentOptionalAllowsDefault)
{
    OwnerFixture fx;
    DefaultTestParameter target(fx.int32, "a");
    target.SetIsOptional(true);
    target.SetHasConstantValueInSignature(true);
    target.SetOwner(fx.owner.get());
    DefaultTestParameter laterOptional(fx.int32, "b");
    laterOptional.SetIsOptional(true);
    laterOptional.SetHasConstantValueInSignature(true);
    laterOptional.SetOwner(fx.owner.get());
    fx.owner->SetParameters({&target, &laterOptional});

    EXPECT_TRUE(TS::IsDefaultValueAssignmentAllowed(target));
}

// A subsequent required by-value parameter blocks the target's default (C#
// requires every parameter after an optional one to have a default too).
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentSubsequentRequiredBlocksDefault)
{
    OwnerFixture fx;
    DefaultTestParameter target(fx.int32, "a");
    target.SetIsOptional(true);
    target.SetHasConstantValueInSignature(true);
    target.SetOwner(fx.owner.get());
    DefaultTestParameter laterRequired(fx.int32, "b");
    laterRequired.SetOwner(fx.owner.get());
    fx.owner->SetParameters({&target, &laterRequired});

    EXPECT_FALSE(TS::IsDefaultValueAssignmentAllowed(target));
}

// A subsequent params array does not block the target's default.
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentSubsequentParamsAllowsDefault)
{
    OwnerFixture fx;
    DefaultTestParameter target(fx.int32, "a");
    target.SetIsOptional(true);
    target.SetHasConstantValueInSignature(true);
    target.SetOwner(fx.owner.get());
    DefaultTestParameter laterParams(fx.int32, "b");
    laterParams.SetIsParams(true);
    laterParams.SetOwner(fx.owner.get());
    fx.owner->SetParameters({&target, &laterParams});

    EXPECT_TRUE(TS::IsDefaultValueAssignmentAllowed(target));
}

// A subsequent closure parameter (a by-ref over a compiler-generated struct
// in the owner's declaring-type tree) is skipped and does not block the
// target's default.
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentSubsequentClosureParameterIsSkipped)
{
    OwnerFixture fx;
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.Outer.Display", "N", TS::FullTypeName("N.Outer.Display"), TS::TypeKind::Struct,
        TS::Accessibility::Public, fx.compilation, nullptr, TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    displayClass->SetDeclaringTypeDefinition(fx.outer.get());

    DefaultTestParameter target(fx.int32, "a");
    target.SetIsOptional(true);
    target.SetHasConstantValueInSignature(true);
    target.SetOwner(fx.owner.get());
    DefaultTestParameter closureParam(
        std::make_shared<TS::ByReferenceType>(displayClass), "closure");
    closureParam.SetReferenceKind(TS::ReferenceKind::Ref);
    closureParam.SetOwner(fx.owner.get());
    fx.owner->SetParameters({&target, &closureParam});

    EXPECT_TRUE(TS::IsDefaultValueAssignmentAllowed(target));
}

// The closure skip requires the display class to be in the owner's
// declaring-type tree: a same-shaped closure parameter under an unrelated
// outer type does not get skipped and blocks the default.
TEST(TypeSystemExtensionsTest, DefaultValueAssignmentSubsequentUnrelatedClosureBlocksDefault)
{
    OwnerFixture fx;
    auto otherOuter = MakeDefinition(fx.compilation, "OtherOuter", "N");
    auto displayClass = std::make_shared<CompilerGeneratedDef>(
        "N.OtherOuter.Display", "N", TS::FullTypeName("N.OtherOuter.Display"),
        TS::TypeKind::Struct, TS::Accessibility::Public, fx.compilation, nullptr,
        TS::KnownTypeCode::None);
    displayClass->SetCompilerGenerated(true);
    displayClass->SetDeclaringTypeDefinition(otherOuter.get());

    DefaultTestParameter target(fx.int32, "a");
    target.SetIsOptional(true);
    target.SetHasConstantValueInSignature(true);
    target.SetOwner(fx.owner.get());
    DefaultTestParameter closureParam(
        std::make_shared<TS::ByReferenceType>(displayClass), "closure");
    closureParam.SetReferenceKind(TS::ReferenceKind::Ref);
    closureParam.SetOwner(fx.owner.get());
    fx.owner->SetParameters({&target, &closureParam});

    EXPECT_FALSE(TS::IsDefaultValueAssignmentAllowed(target));
}
