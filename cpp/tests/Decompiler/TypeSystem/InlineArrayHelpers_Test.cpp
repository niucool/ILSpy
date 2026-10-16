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

// Tests for the TypeSystemExtensions inline-array helpers (TypeSystemExtensions.cs lines
// 340-373): `IsInlineArrayType` / `GetInlineArrayLength` / `GetInlineArrayElementType` -- the
// prerequisites the `StandardImplicitConversion` inline-array-to-span arm consumes.
//
// The C#:
//   public static bool IsInlineArrayType(this IType type)
//     => type.Kind == Struct && type.GetDefinition()?.HasAttribute(InlineArray) == true;
//   public static int? GetInlineArrayLength(this IType type)
//     => ... td.GetAttribute(InlineArray)?.FixedArguments.FirstOrDefault().Value as int?;
//   public static IType GetInlineArrayElementType(this IType arrayType)
//     => arrayType?.GetFields(f => !f.IsStatic).SingleOrDefault()?.Type ?? SpecialType.UnknownType;
//
// The tests pin:
//  (a) `IsInlineArrayType`: true for a struct with the [InlineArray] attribute; false for a
//      class kind despite the attribute (the Kind guard); false for a struct without the
//      attribute; false for a definitionless struct-kind type (a `KnownType` placeholder);
//  (b) `GetInlineArrayLength`: the attribute's first fixed int argument; nullopt for a
//      non-struct kind, a missing attribute, an empty fixed-argument list, and a non-int
//      first argument;
//  (c) `GetInlineArrayElementType`: the single instance field's type (pointer-identity with
//      the field's `Type()`); the static fields are EXCLUDED by the filter (a static + an
//      instance field reduce to the instance one); `UnknownType` when there are no instance
//      fields (the `?? SpecialType.UnknownType` fallback); a `std::runtime_error` (the
//      C# `SingleOrDefault` InvalidOperationException analog) when there are multiple
//      instance fields.

#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using TS::Accessibility;
using TS::CustomAttributeTypedArgument;
using TS::FullTypeName;
using TS::GetInlineArrayElementType;
using TS::GetInlineArrayLength;
using TS::IAttribute;
using TS::ICompilation;
using TS::IField;
using TS::IType;
using TS::ITypePtr;
using TS::IsInlineArrayType;
using TS::KnownType;
using TS::KnownTypeCode;
using TS::TopLevelTypeName;
using TS::TypeKind;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A test `IAttribute` stub carrying configurable fixed arguments (the positional
// `[InlineArray(N)]` arguments). `GetInlineArrayLength` reads only `FixedArguments`.
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
    std::vector<TS::CustomAttributeNamedArgument> NamedArguments() const override { return {}; }

private:
    std::vector<CustomAttributeTypedArgument> fixedArgs_;
};

// A minimal `IField` stub with a configurable type and static-ness (the `!f.IsStatic` filter
// the element-type helper applies; the `GetMembersHelper_Test` TestField with a SetStatic).
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
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ITypePtr DeclaringType() const override { return {}; }
    const TS::IModule* ParentModule() const override { return nullptr; }
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

// A `LookupTypeDefinition` subclass that overrides the attribute accessors (reporting a single
// configurable `[InlineArray]` attribute) and `GetFields` (a configurable field snapshot,
// applying the caller's filter faithfully) -- the surface the three inline-array helpers read.
class InlineArrayDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;

    void SetInlineArrayAttribute(const IAttribute* attr) { attr_ = attr; }
    void SetFields(std::vector<const IField*> fields) { fields_ = std::move(fields); }

    bool HasAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::InlineArray && attr_ != nullptr;
    }
    const IAttribute* GetAttribute(TS::KnownAttribute attribute) const override {
        return attribute == TS::KnownAttribute::InlineArray ? attr_ : nullptr;
    }
    std::vector<const IAttribute*> GetAttributes() const override {
        return attr_ != nullptr ? std::vector<const IAttribute*>{attr_}
                                : std::vector<const IAttribute*>{};
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
    const IAttribute* attr_ = nullptr;
    std::vector<const IField*> fields_;
};

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` and `TypeKind` (struct by default) -- the D514 `MakeDef`
// precedent, used for the field types and the definitionless-type sentinel.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "", FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// The `[InlineArray(N)]` attribute: one fixed argument whose boxed value is the int `N` (the
// type is a `Def(Int32)`, faithfully modelling the decoded `int` positional argument). Leaks
// the attribute (test scope -- it lives for the program's lifetime, the D541 precedent).
const IAttribute* MakeInlineArrayAttribute(int n, std::optional<std::any> value = std::nullopt) {
    std::any boxed = value.has_value() ? *value : std::any(n);
    auto arg = CustomAttributeTypedArgument(MakeDef(KnownTypeCode::Int32), std::move(boxed));
    return new TestAttribute(std::vector<CustomAttributeTypedArgument>{std::move(arg)});
}

// An `InlineArrayDef` (struct kind, the `[InlineArray(N)]` attribute) with no fields -- the
// `GetInlineArrayElementType` no-instance-field shape.
std::shared_ptr<InlineArrayDef> MakeInlineArrayStruct(int length = 10) {
    auto def = std::make_shared<InlineArrayDef>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    def->SetInlineArrayAttribute(MakeInlineArrayAttribute(length));
    return def;
}

} // namespace

// ---------------------------------------------------------------------------
// IsInlineArrayType (TypeSystemExtensions.cs line 340)
// ---------------------------------------------------------------------------

TEST(InlineArrayHelpersTest, IsInlineArrayTypeTrueForStructWithAttribute) {
    auto def = MakeInlineArrayStruct();
    EXPECT_TRUE(IsInlineArrayType(*def));
}

TEST(InlineArrayHelpersTest, IsInlineArrayTypeFalseForClassKindDespiteAttribute) {
    auto def = std::make_shared<InlineArrayDef>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    def->SetInlineArrayAttribute(MakeInlineArrayAttribute(10));
    EXPECT_FALSE(IsInlineArrayType(*def));
}

TEST(InlineArrayHelpersTest, IsInlineArrayTypeFalseForStructWithoutAttribute) {
    auto def = MakeDef(KnownTypeCode::Int32, TypeKind::Struct);
    EXPECT_FALSE(IsInlineArrayType(*def));
}

TEST(InlineArrayHelpersTest, IsInlineArrayTypeFalseForDefinitionlessType) {
    // A `KnownType(Int32)` has `Kind == Struct` but `GetDefinition() == nullptr` (it is NOT
    // an `ITypeDefinition`), so the second guard returns false.
    auto placeholder = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_FALSE(IsInlineArrayType(*placeholder));
}

// ---------------------------------------------------------------------------
// GetInlineArrayLength (TypeSystemExtensions.cs line 352)
// ---------------------------------------------------------------------------

TEST(InlineArrayHelpersTest, GetInlineArrayLengthReturnsAttributeArgument) {
    auto def = MakeInlineArrayStruct(42);
    std::optional<int> length = GetInlineArrayLength(*def);
    ASSERT_TRUE(length.has_value());
    EXPECT_EQ(*length, 42);
}

TEST(InlineArrayHelpersTest, GetInlineArrayLengthNulloptForNonStructKind) {
    auto def = std::make_shared<InlineArrayDef>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    def->SetInlineArrayAttribute(MakeInlineArrayAttribute(10));
    EXPECT_FALSE(GetInlineArrayLength(*def).has_value());
}

TEST(InlineArrayHelpersTest, GetInlineArrayLengthNulloptForMissingAttribute) {
    auto def = std::make_shared<InlineArrayDef>("MyBuffer", "Test",
        FullTypeName(TopLevelTypeName("Test", "MyBuffer", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    EXPECT_FALSE(GetInlineArrayLength(*def).has_value());
}

TEST(InlineArrayHelpersTest, GetInlineArrayLengthNulloptForEmptyFixedArguments) {
    auto def = MakeInlineArrayStruct();
    // `[InlineArray]` with NO positional arguments: `FirstOrDefault()` on empty yields null,
    // whose `.Value` the C# `?.` chain skips -- the whole `as int?` is null.
    def->SetInlineArrayAttribute(new TestAttribute(std::vector<CustomAttributeTypedArgument>{}));
    EXPECT_FALSE(GetInlineArrayLength(*def).has_value());
}

TEST(InlineArrayHelpersTest, GetInlineArrayLengthNulloptForNonIntArgument) {
    auto def = MakeInlineArrayStruct();
    // A first fixed argument whose boxed value is NOT an int (a string): the C# `as int?`
    // yields null; the port's pointer-form `any_cast` returns null on the mismatch.
    def->SetInlineArrayAttribute(
        MakeInlineArrayAttribute(10, std::any(std::string("not an int"))));
    EXPECT_FALSE(GetInlineArrayLength(*def).has_value());
}

// ---------------------------------------------------------------------------
// GetInlineArrayElementType (TypeSystemExtensions.cs line 361)
// ---------------------------------------------------------------------------

TEST(InlineArrayHelpersTest, GetInlineArrayElementTypeReturnsSingleInstanceFieldType) {
    auto intDef = MakeDef(KnownTypeCode::Int32);
    TestField element("_element", intDef, /*isStatic*/ false);
    auto def = MakeInlineArrayStruct();
    def->SetFields({&element});
    ITypePtr elementType = GetInlineArrayElementType(*def);
    ASSERT_NE(elementType, nullptr);
    // Pointer-identity with the field's type (the shared_from_this handle to the same object).
    EXPECT_EQ(elementType.get(), intDef.get());
    EXPECT_EQ(elementType->Kind(), TypeKind::Struct);
}

TEST(InlineArrayHelpersTest, GetInlineArrayElementTypeFilterExcludesStaticFields) {
    // One static + one instance field: the `!f.IsStatic` filter reduces the list to the
    // instance field, so `SingleOrDefault` sees exactly one element.
    auto intDef = MakeDef(KnownTypeCode::Int32);
    auto longDef = MakeDef(KnownTypeCode::Int64);
    TestField staticField("s_shared", longDef, /*isStatic*/ true);
    TestField element("_element", intDef, /*isStatic*/ false);
    auto def = MakeInlineArrayStruct();
    def->SetFields({&staticField, &element});
    ITypePtr elementType = GetInlineArrayElementType(*def);
    ASSERT_NE(elementType, nullptr);
    EXPECT_EQ(elementType.get(), intDef.get());
}

TEST(InlineArrayHelpersTest, GetInlineArrayElementTypeUnknownWhenNoInstanceFields) {
    // No fields at all: the `?.Type` chain yields null and the `??` coalesces to
    // `SpecialType.UnknownType`.
    auto def = MakeInlineArrayStruct();
    ITypePtr elementType = GetInlineArrayElementType(*def);
    ASSERT_NE(elementType, nullptr);
    EXPECT_EQ(elementType->Kind(), TypeKind::Unknown);
}

TEST(InlineArrayHelpersTest, GetInlineArrayElementTypeUnknownWhenAllFieldsAreStatic) {
    auto longDef = MakeDef(KnownTypeCode::Int64);
    TestField staticField("s_shared", longDef, /*isStatic*/ true);
    auto def = MakeInlineArrayStruct();
    def->SetFields({&staticField});
    ITypePtr elementType = GetInlineArrayElementType(*def);
    ASSERT_NE(elementType, nullptr);
    EXPECT_EQ(elementType->Kind(), TypeKind::Unknown);
}

TEST(InlineArrayHelpersTest, GetInlineArrayElementTypeThrowsForMultipleInstanceFields) {
    // `SingleOrDefault()` throws InvalidOperationException on more than one match; the port
    // throws the `std::runtime_error` analog (a real inline-array struct has exactly one
    // instance field, so the throw guards the same metadata invariant the C# does).
    auto intDef = MakeDef(KnownTypeCode::Int32);
    auto longDef = MakeDef(KnownTypeCode::Int64);
    TestField first("_element1", intDef, /*isStatic*/ false);
    TestField second("_element2", longDef, /*isStatic*/ false);
    auto def = MakeInlineArrayStruct();
    def->SetFields({&first, &second});
    EXPECT_THROW(GetInlineArrayElementType(*def), std::runtime_error);
}

TEST(InlineArrayHelpersTest, GetInlineArrayElementTypeWorksWithoutTheAttribute) {
    // The element-type helper does NOT check the [InlineArray] attribute (the C# reads only
    // the instance fields); any struct with a single instance field yields its type.
    auto intDef = MakeDef(KnownTypeCode::Int32);
    TestField element("_element", intDef, /*isStatic*/ false);
    auto fieldHost = std::make_shared<InlineArrayDef>("PlainStruct", "Test",
        FullTypeName(TopLevelTypeName("Test", "PlainStruct", 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::None);
    fieldHost->SetFields({&element});
    ITypePtr elementType = GetInlineArrayElementType(*fieldHost);
    ASSERT_NE(elementType, nullptr);
    EXPECT_EQ(elementType.get(), intDef.get());
}
