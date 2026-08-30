// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the last two `StandardImplicitConversion` dispatch arms (CSharpConversions.cs
// lines 242-254, wired after the tuple arm D540): the C# 12 inline-array-to-span arm and the
// C# 14 first-class-span-types arm.
//
// The C#:
//   if ((toType.IsKnownType(KnownTypeCode.SpanOfT) || toType.IsKnownType(KnownTypeCode.ReadOnlySpanOfT))
//       && fromType.IsInlineArrayType())
//   {
//       var elementType = fromType.GetInlineArrayElementType();
//       var spanElementType = toType.TypeArguments[0];
//       if (IdentityConversion(elementType, spanElementType))
//           return Conversion.InlineArrayConversion;
//   }
//   if (IsImplicitSpanConversion(fromType, toType))
//       return Conversion.ImplicitSpanConversion;
//
// The tests pin:
//  (a) an inline-array struct (a struct carrying the [InlineArray] attribute with a single
//      instance field) converts to Span<T> / ReadOnlySpan<T> over an identity-equal element
//      type -- the returned conversion is the `InlineArrayConversion` singleton;
//  (b) a mismatched element type (int field vs Span<long>) yields None (the identity check
//      fails and no later arm fires for an inline-array-struct source);
//  (c) the `IsInlineArrayType` guard: a plain struct (no [InlineArray] attribute) to a span
//      yields None;
//  (d) the to-type guard: an inline-array struct to a NON-span generic type yields None;
//  (e) the span arm: a single-dimensional int[] to ReadOnlySpan<int> over the SAME element
//      instance yields the `ImplicitSpanConversion` singleton (gated on the compilation's
//      `FirstClassSpanTypes`);
//  (f) the span-arm flag gate: the same pair over a flag-less compilation yields None.
//
// Test-stub conventions (the D538 SpanConversion test + the InlineArrayHelpers test):
//  * `SpanCompilation` is a `LookupCompilation` subclass overriding `TypeSystemOptions()` to
//    return `FirstClassSpanTypes` (the base returns `None`, which fails the span-arm flag
//    gate; the inline-array arm is flag-independent, so one compilation serves all tests --
//    the D544 single-SpanCompilation precedent).
//  * `InlineArrayDef` is a `LookupTypeDefinition` subclass reporting a single [InlineArray]
//    attribute and a configurable field snapshot (the `GetFields` filter applied faithfully);
//    `TestField` is a minimal `IField` with a configurable type + static-ness.
//  * `LookupTypeDefinition::StructuralEquals` is identity (`this == &other`), so the
//    element-identity crux cases MUST reuse the SAME `ITypePtr` instance for the inline
//    array's field type and the span's type argument.
//  * The `Span`1` / `ReadOnlySpan`1` definitions are `LookupTypeDefinition`s carrying
//    `KnownTypeCode::SpanOfT` / `ReadOnlySpanOfT` (a `Span<T>` IS a `ParameterizedType` over
//    the definition, so `IsKnownType` resolves via `GetDefinition()->KnownTypeCode`).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::StandardImplicitConversion
#include "Decompiler/Semantics/ConversionFactories.hpp"              // Conversions (InlineArrayConversion / ImplicitSpanConversion / None)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace R = ILSpy::Decompiler::CSharp::Resolver;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using R::Detail::StandardImplicitConversion;
using ILSpy::Decompiler::Semantics::Conversions;
using TS::Accessibility;
using TS::ArrayType;
using TS::CustomAttributeNamedArgument;
using TS::CustomAttributeTypedArgument;
using TS::FullTypeName;
using TS::IAttribute;
using TS::ICompilation;
using TS::IField;
using TS::IModule;
using TS::IType;
using TS::ITypeDefinition;
using TS::ITypePtr;
using TS::KnownTypeCode;
using TS::ParameterizedType;
using TS::TopLevelTypeName;
using TS::TypeKind;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupTypeDefinition;

// A `LookupCompilation` whose `TypeSystemOptions()` carries `FirstClassSpanTypes` (the base
// returns `None`, which fails the span-arm flag gate). The member function
// `TypeSystemOptions()` shares its name with the `TypeSystemOptions` enum type (the C# idiom
// the D472 port hit); the member hides the enum in the class body, so the return type and the
// body are fully-qualified with `TS::` (the D538 SpanCompilation precedent).
class SpanCompilation : public LookupCompilation {
public:
    TS::TypeSystemOptions TypeSystemOptions() const override {
        return TS::TypeSystemOptions::FirstClassSpanTypes;
    }
};

SpanCompilation& Compilation() {
    static SpanCompilation c;
    return c;
}

// A plain `LookupCompilation` (no flags) for the span-arm flag-gate test.
LookupCompilation& NoFlagCompilation() {
    static LookupCompilation c;
    return c;
}

// A test `IAttribute` stub carrying one positional fixed argument (the [InlineArray]
// attribute's length; the helpers read only `FixedArguments` / `HasAttribute`).
class TestAttribute : public IAttribute {
public:
    explicit TestAttribute(std::vector<CustomAttributeTypedArgument> fixedArgs)
        : fixedArgs_(std::move(fixedArgs)) {}

    const IType& AttributeType() const override {
        static auto attrType = std::make_shared<TS::SimpleType>(
            TopLevelTypeName("System.Runtime.CompilerServices", "InlineArrayAttribute"));
        return *attrType;
    }
    const TS::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<CustomAttributeTypedArgument> FixedArguments() const override { return fixedArgs_; }
    std::vector<CustomAttributeNamedArgument> NamedArguments() const override { return {}; }

private:
    std::vector<CustomAttributeTypedArgument> fixedArgs_;
};

// A minimal `IField` stub with a configurable type and static-ness (the InlineArrayHelpers
// test's TestField).
class TestField : public IField {
public:
    TestField(std::string name, ITypePtr type, bool isStatic)
        : name_(std::move(name)), type_(std::move(type)), isStatic_(isStatic) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Field; }
    std::string Name() const override { return name_; }
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }
    const ICompilation& Compilation() const override { return Compilation(); }
    std::uint32_t MetadataToken() const override { return 0; }
    const ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const IModule* ParentModule() const override { return nullptr; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const IAttribute* GetAttribute(TS::KnownAttribute) const override { return nullptr; }
    TS::Accessibility Accessibility() const override { return Accessibility::Public; }
    bool IsStatic() const override { return isStatic_; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    const TS::IMember* MemberDefinition() const override { return this; }
    const IType& ReturnType() const override { return *type_; }
    std::vector<const TS::IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override { return &identitySubst_; }
    const TS::IMember* Specialize(const TS::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const TS::IMember* obj, const TS::TypeVisitor*) const override { return obj == this; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

private:
    std::string name_;
    ITypePtr type_;
    bool isStatic_;
    mutable TS::TypeParameterSubstitution identitySubst_{std::nullopt, std::nullopt};
};

// A `LookupTypeDefinition` subclass reporting a single [InlineArray] attribute and a
// configurable field snapshot (the `GetFields` filter applied faithfully).
class InlineArrayDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetFields(std::vector<const IField*> fields) { fields_ = std::move(fields); }

    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::InlineArray;
    }
    const IAttribute* GetAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::InlineArray ? &attr_ : nullptr;
    }
    std::vector<const IAttribute*> GetAttributes() const override {
        return std::vector<const IAttribute*>{&attr_};
    }
    std::vector<const IField*> GetFields(
        std::function<bool(const IField*)> filter = nullptr,
        TS::GetMemberOptions options = TS::GetMemberOptions::None) const override {
        (void)options;
        std::vector<const IField*> result;
        for (const IField* f : fields_) {
            if (filter == nullptr || filter(f))
                result.push_back(f);
        }
        return result;
    }

private:
    // The [InlineArray(10)] attribute: one fixed argument whose boxed value is the int 10.
    TestAttribute attr_{std::vector<CustomAttributeTypedArgument>{
        CustomAttributeTypedArgument(nullptr, std::any(10))}};
    std::vector<const IField*> fields_;
};

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` and `TypeKind` (struct by default) -- the D514 `MakeDef`
// precedent, used for the element types.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// The `System.Span`1` generic definition (a struct, `KnownTypeCode::SpanOfT`).
std::shared_ptr<LookupTypeDefinition> SpanDef() {
    static auto d = std::make_shared<LookupTypeDefinition>("Span`1", "System",
        FullTypeName(TopLevelTypeName("System", "Span`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::SpanOfT);
    return d;
}

// The `System.ReadOnlySpan`1` generic definition (a struct, `KnownTypeCode::ReadOnlySpanOfT`).
std::shared_ptr<LookupTypeDefinition> ReadOnlySpanDef() {
    static auto d = std::make_shared<LookupTypeDefinition>("ReadOnlySpan`1", "System",
        FullTypeName(TopLevelTypeName("System", "ReadOnlySpan`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::ReadOnlySpanOfT);
    return d;
}

// `Span<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `System.Span`1` definition).
ITypePtr SpanOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(SpanDef(), std::vector<ITypePtr>{std::move(element)});
}

// `ReadOnlySpan<T>` over the supplied element type.
ITypePtr ReadOnlySpanOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(ReadOnlySpanDef(), std::vector<ITypePtr>{std::move(element)});
}

} // namespace

// ===========================================================================
// The inline-array-to-span arm (CSharpConversions.cs lines 242-248)
// ===========================================================================

TEST(StandardImplicitInlineArraySpanTest, InlineArrayToSpanIsInlineArrayConversion) {
    // The SAME int instance is the inline array's field type and the Span's type argument
    // (LookupTypeDefinition::StructuralEquals is identity equality) -- the
    // IdentityConversion(elementType, spanElementType) crux.
    auto intDef = MakeDef(KnownTypeCode::Int32);
    TestField field("_element", intDef, /*isStatic*/ false);
    auto def = std::make_shared<InlineArrayDef>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    def->SetFields({&field});
    ITypePtr span = SpanOf(intDef);
    auto result = StandardImplicitConversion(Compilation(), *def, *span);
    EXPECT_EQ(result.get(), Conversions::InlineArrayConversion().get());
    EXPECT_TRUE(result->IsInlineArrayConversion());
    EXPECT_TRUE(result->IsImplicit());
    EXPECT_TRUE(result->IsValid());
}

TEST(StandardImplicitInlineArraySpanTest, InlineArrayToReadOnlySpanIsInlineArrayConversion) {
    auto intDef = MakeDef(KnownTypeCode::Int32);
    TestField field("_element", intDef, /*isStatic*/ false);
    auto def = std::make_shared<InlineArrayDef>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    def->SetFields({&field});
    ITypePtr span = ReadOnlySpanOf(intDef);
    auto result = StandardImplicitConversion(Compilation(), *def, *span);
    EXPECT_EQ(result.get(), Conversions::InlineArrayConversion().get());
    EXPECT_TRUE(result->IsInlineArrayConversion());
}

TEST(StandardImplicitInlineArraySpanTest, InlineArrayToSpanMismatchedElementYieldsNone) {
    // The field is typed int but the span is over long: the identity check fails, and no
    // later arm fires for an inline-array-struct source (the span arm's own shapes are
    // array/Span/string, not an inline-array struct).
    auto intDef = MakeDef(KnownTypeCode::Int32);
    auto longDef = MakeDef(KnownTypeCode::Int64);
    TestField field("_element", intDef, /*isStatic*/ false);
    auto def = std::make_shared<InlineArrayDef>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    def->SetFields({&field});
    ITypePtr span = SpanOf(longDef);
    auto result = StandardImplicitConversion(Compilation(), *def, *span);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

TEST(StandardImplicitInlineArraySpanTest, PlainStructToSpanYieldsNone) {
    // The `IsInlineArrayType` guard: a plain struct (no [InlineArray] attribute) does not
    // fire the arm even though its field type identity-matches the span element.
    auto intDef = MakeDef(KnownTypeCode::Int32);
    TestField field("_element", intDef, /*isStatic*/ false);
    auto def = std::make_shared<LookupTypeDefinition>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    ITypePtr span = SpanOf(intDef);
    auto result = StandardImplicitConversion(Compilation(), *def, *span);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

TEST(StandardImplicitInlineArraySpanTest, InlineArrayToNonSpanTypeYieldsNone) {
    // The to-type guard: the arm fires only for SpanOfT / ReadOnlySpanOfT targets; a
    // generic type over a NON-span definition (a List`1-shaped parameterized type) skips
    // the arm entirely.
    auto intDef = MakeDef(KnownTypeCode::Int32);
    TestField field("_element", intDef, /*isStatic*/ false);
    auto def = std::make_shared<InlineArrayDef>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    def->SetFields({&field});
    auto listDef = std::make_shared<LookupTypeDefinition>("List`1", "System.Collections.Generic",
        FullTypeName(TopLevelTypeName("System.Collections.Generic", "List`1", 1)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    ITypePtr listOfInt = std::make_shared<ParameterizedType>(listDef, std::vector<ITypePtr>{intDef});
    auto result = StandardImplicitConversion(Compilation(), *def, *listOfInt);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

// ===========================================================================
// The first-class-span arm (CSharpConversions.cs lines 249-254, IsImplicitSpanConversion D538)
// ===========================================================================

TEST(StandardImplicitInlineArraySpanTest, ArrayToReadOnlySpanIsImplicitSpanConversion) {
    // A single-dimensional int[] to ReadOnlySpan<int> over the SAME element instance: the
    // span arm fires (the array arm of IsImplicitSpanConversion, gated on
    // FirstClassSpanTypes which SpanCompilation carries).
    auto intDef = MakeDef(KnownTypeCode::Int32);
    ITypePtr array = std::make_shared<ArrayType>(intDef);
    ITypePtr span = ReadOnlySpanOf(intDef);
    auto result = StandardImplicitConversion(Compilation(), *array, *span);
    EXPECT_EQ(result.get(), Conversions::ImplicitSpanConversion().get());
    EXPECT_TRUE(result->IsImplicitSpanConversion());
    EXPECT_TRUE(result->IsImplicit());
    EXPECT_TRUE(result->IsValid());
}

TEST(StandardImplicitInlineArraySpanTest, ArrayToReadOnlySpanWithoutFlagYieldsNone) {
    // The flag gate: the same pair over a plain LookupCompilation (TypeSystemOptions::None)
    // fails IsImplicitSpanConversion's flag check, so no arm fires.
    auto intDef = MakeDef(KnownTypeCode::Int32);
    ITypePtr array = std::make_shared<ArrayType>(intDef);
    ITypePtr span = ReadOnlySpanOf(intDef);
    auto result = StandardImplicitConversion(NoFlagCompilation(), *array, *span);
    EXPECT_EQ(result.get(), Conversions::None().get());
}

TEST(StandardImplicitInlineArraySpanTest, ArrayToSpanIsImplicitSpanConversion) {
    // The array arm of IsImplicitSpanConversion also covers int[] -> Span<int> by element
    // identity.
    auto intDef = MakeDef(KnownTypeCode::Int32);
    ITypePtr array = std::make_shared<ArrayType>(intDef);
    ITypePtr span = SpanOf(intDef);
    auto result = StandardImplicitConversion(Compilation(), *array, *span);
    EXPECT_EQ(result.get(), Conversions::ImplicitSpanConversion().get());
}
