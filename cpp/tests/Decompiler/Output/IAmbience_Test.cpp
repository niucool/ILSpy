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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `IAmbience` + `ConversionFlags` (cpp/Decompiler/Output/IAmbience.hpp, the D372
// port of ICSharpCode.Decompiler/Output/IAmbience.cs -- the `[Flags] ConversionFlags` enum
// and the `IAmbience` interface). `IAmbience` is the leaf `Output`-namespace dependency of
// `CSharpAmbience` (which `: IAmbience` and switches on its `ConversionFlags`); the
// `ConversionFlags` flags drive which parts of a symbol/type the ambience renders (parameter
// list, accessibility, return type, ...). The tests pin the enum values (matching the C#
// literals, the `StandardConversionFlags`/`All` composites, the `[Flags]` bitwise operators)
// and the interface contract (a concrete subclass overriding `ConversionFlags` get/set and
// the four `Convert*`/`WrapComment` methods, polymorphic dispatch through an `IAmbience*`).

#include "Decompiler/Output/IAmbience.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace TS = ILSpy::Decompiler::TypeSystem;
namespace OUT = ILSpy::Decompiler::Output;
using TS::ISymbol;
using TS::IType;
using TS::KnownType;
using TS::KnownTypeCode;
using TS::SymbolKind;
using OUT::ConversionFlags;
using OUT::IAmbience;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveValue;

namespace {

// A minimal concrete `ISymbol` for the `ConvertSymbol` dispatch test.
class TestSymbol : public ISymbol {
public:
    TestSymbol(TS::SymbolKind kind, std::string name) : kind_(kind), name_(std::move(name)) {}
    TS::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }
private:
    TS::SymbolKind kind_;
    std::string name_;
};

// A minimal concrete `IAmbience` for the interface-contract tests: records the
// `ConversionFlags` set and the method-dispatch calls (the shape `CSharpAmbience` will take,
// minus the real `TypeSystemAstBuilder` rendering the concrete `CSharpAmbience` defers to).
class TestAmbience : public IAmbience {
public:
    OUT::ConversionFlags flags_ = OUT::ConversionFlags::None;
    mutable std::string lastSymbolName;
    mutable std::string lastTypeName;
    mutable std::string lastConstantValue;
    mutable std::string lastComment;

    OUT::ConversionFlags ConversionFlags() const override { return flags_; }
    void ConversionFlags(::ILSpy::Decompiler::Output::ConversionFlags value) override {
        flags_ = value;
    }
    std::string ConvertSymbol(const ISymbol& symbol) override {
        lastSymbolName = symbol.Name();
        return symbol.Name();
    }
    std::string ConvertType(const IType& type) override {
        lastTypeName = type.Name();
        return type.Name();
    }
    std::string ConvertConstantValue(const PrimitiveValue& constantValue) override {
        // render the variant's index as a string (the dispatch is what is under test, not the
        // rendering; the real `CSharpAmbience` delegates to `PrintPrimitiveValue`).
        lastConstantValue = std::to_string(constantValue.index());
        return lastConstantValue;
    }
    std::string WrapComment(std::string_view comment) override {
        lastComment = std::string(comment);
        return "// " + std::string(comment);
    }
};

} // namespace

// ---------------------------------------------------------------------------
// ConversionFlags -- the individual flag values match the C# literals (the declaration-
// index / hex values, faithfully ported).
// ---------------------------------------------------------------------------
TEST(ConversionFlagsTest, IndividualFlagsMatchCSharpLiterals)
{
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::None), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowParameterList), 0x1u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowParameterNames), 0x2u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowAccessibility), 0x4u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowDefinitionKeyword), 0x8u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowDeclaringType), 0x10u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowModifiers), 0x20u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowReturnType), 0x40u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::UseFullyQualifiedTypeNames), 0x80u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowTypeParameterList), 0x100u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowBody), 0x200u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::UseFullyQualifiedEntityNames), 0x400u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::PlaceReturnTypeAfterParameterList), 0x800u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowTypeParameterVarianceModifier), 0x1000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowParameterModifiers), 0x2000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::ShowParameterDefaultValues), 0x4000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::UseNullableSpecifierForValueTypes), 0x8000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::SupportInitAccessors), 0x10000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::SupportRecordClasses), 0x20000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::SupportRecordStructs), 0x40000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::SupportUnsignedRightShift), 0x80000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::SupportOperatorChecked), 0x100000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::UsePrivateProtectedAccessibility), 0x200000u);
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::SupportExtensionDeclarations), 0x400000u);
}

// ---------------------------------------------------------------------------
// ConversionFlags -- `StandardConversionFlags` is the bitwise OR of its constituents (the
// C# `ShowParameterNames | ShowAccessibility | ... | ShowBody` composite).
// ---------------------------------------------------------------------------
TEST(ConversionFlagsTest, StandardConversionFlagsIsBitwiseOrOfConstituents)
{
    const ConversionFlags expected =
        ConversionFlags::ShowParameterNames |
        ConversionFlags::ShowAccessibility |
        ConversionFlags::UsePrivateProtectedAccessibility |
        ConversionFlags::ShowParameterList |
        ConversionFlags::ShowParameterModifiers |
        ConversionFlags::ShowParameterDefaultValues |
        ConversionFlags::UseNullableSpecifierForValueTypes |
        ConversionFlags::ShowReturnType |
        ConversionFlags::ShowModifiers |
        ConversionFlags::ShowTypeParameterList |
        ConversionFlags::ShowTypeParameterVarianceModifier |
        ConversionFlags::ShowDefinitionKeyword |
        ConversionFlags::ShowBody;
    EXPECT_EQ(ConversionFlags::StandardConversionFlags, expected);
    // the C# computed value (0x20F36F), pinned independently of the `|` form.
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::StandardConversionFlags), 0x20F36Fu);
}

// ---------------------------------------------------------------------------
// ConversionFlags -- `All` is the C# `0x1fffff` literal verbatim (note: it does NOT include
// `UsePrivateProtectedAccessibility` (0x200000) / `SupportExtensionDeclarations` (0x400000),
// a quirk of the C# source).
// ---------------------------------------------------------------------------
TEST(ConversionFlagsTest, AllIsCSharpLiteralVerbatim)
{
    EXPECT_EQ(static_cast<std::uint32_t>(ConversionFlags::All), 0x1fffffu);
    EXPECT_EQ(ConversionFlags::All & ConversionFlags::SupportOperatorChecked,
              ConversionFlags::SupportOperatorChecked);
    EXPECT_EQ(ConversionFlags::All & ConversionFlags::SupportExtensionDeclarations,
              ConversionFlags::None);
    EXPECT_EQ(ConversionFlags::All & ConversionFlags::UsePrivateProtectedAccessibility,
              ConversionFlags::None);
}

// ---------------------------------------------------------------------------
// ConversionFlags -- the `[Flags]` bitwise operators (the C# `[Flags]` compiler generates
// them implicitly; the C++ `enum class` defines them as free functions).
// ---------------------------------------------------------------------------
TEST(ConversionFlagsTest, BitwiseOrCombinesFlags)
{
    auto combined = ConversionFlags::ShowParameterList | ConversionFlags::ShowReturnType;
    EXPECT_EQ(combined & ConversionFlags::ShowParameterList, ConversionFlags::ShowParameterList);
    EXPECT_EQ(combined & ConversionFlags::ShowReturnType, ConversionFlags::ShowReturnType);
    EXPECT_EQ(combined & ConversionFlags::ShowBody, ConversionFlags::None);
}

TEST(ConversionFlagsTest, BitwiseAndIntersectsFlags)
{
    auto a = ConversionFlags::ShowParameterList | ConversionFlags::ShowReturnType;
    auto b = ConversionFlags::ShowReturnType | ConversionFlags::ShowBody;
    EXPECT_EQ(a & b, ConversionFlags::ShowReturnType);
}

TEST(ConversionFlagsTest, BitwiseXorTogglesFlags)
{
    auto a = ConversionFlags::ShowParameterList | ConversionFlags::ShowReturnType;
    auto toggled = a ^ ConversionFlags::ShowReturnType;
    EXPECT_EQ(toggled, ConversionFlags::ShowParameterList);
}

TEST(ConversionFlagsTest, BitwiseNotInvertsFlags)
{
    EXPECT_EQ(~ConversionFlags::None,
              static_cast<ConversionFlags>(0xFFFFFFFFu));
}

// ---------------------------------------------------------------------------
// ConversionFlags -- the `(flags & flag) == flag` flag-test idiom `CSharpAmbience` uses.
// ---------------------------------------------------------------------------
TEST(ConversionFlagsTest, FlagTestIdiomMatchesCSharpAmbienceUsage)
{
    auto flags = ConversionFlags::StandardConversionFlags;
    // the C# `(ConversionFlags & ConversionFlags.ShowReturnType) == ConversionFlags.ShowReturnType`
    // (ShowReturnType is a constituent of StandardConversionFlags, so the test is true).
    EXPECT_TRUE((flags & ConversionFlags::ShowReturnType) == ConversionFlags::ShowReturnType);
    // ShowBody IS a constituent of StandardConversionFlags, so the flag-test idiom is true.
    EXPECT_TRUE((flags & ConversionFlags::ShowBody) == ConversionFlags::ShowBody);
    EXPECT_TRUE((flags & ConversionFlags::ShowDefinitionKeyword) ==
                ConversionFlags::ShowDefinitionKeyword);
    // a flag NOT in StandardConversionFlags (UseFullyQualifiedTypeNames), so the idiom is false.
    EXPECT_FALSE((flags & ConversionFlags::UseFullyQualifiedTypeNames) ==
                 ConversionFlags::UseFullyQualifiedTypeNames);
}

// ---------------------------------------------------------------------------
// IAmbience -- the ConversionFlags property get/set round-trips through the interface.
// ---------------------------------------------------------------------------
TEST(IAmbienceTest, ConversionFlagsPropertyGetSetRoundTrips)
{
    TestAmbience ambience;
    IAmbience& base = ambience;
    EXPECT_EQ(base.ConversionFlags(), ConversionFlags::None);
    base.ConversionFlags(ConversionFlags::ShowParameterList | ConversionFlags::ShowReturnType);
    EXPECT_EQ(base.ConversionFlags(),
              ConversionFlags::ShowParameterList | ConversionFlags::ShowReturnType);
    EXPECT_EQ(ambience.flags_, ConversionFlags::ShowParameterList | ConversionFlags::ShowReturnType);
}

// ---------------------------------------------------------------------------
// IAmbience -- `ConvertSymbol` dispatches to the override through an `IAmbience*` and
// receives the `ISymbol`.
// ---------------------------------------------------------------------------
TEST(IAmbienceTest, ConvertSymbolDispatchesToOverride)
{
    TestAmbience ambience;
    IAmbience& base = ambience;
    TestSymbol symbol(SymbolKind::Method, "ToString");
    std::string result = base.ConvertSymbol(symbol);
    EXPECT_EQ(result, "ToString");
    EXPECT_EQ(ambience.lastSymbolName, "ToString");
}

// ---------------------------------------------------------------------------
// IAmbience -- `ConvertType` dispatches to the override through an `IAmbience*` and
// receives the `IType`.
// ---------------------------------------------------------------------------
TEST(IAmbienceTest, ConvertTypeDispatchesToOverride)
{
    TestAmbience ambience;
    IAmbience& base = ambience;
    auto type = std::make_shared<KnownType>(KnownTypeCode::Int32);
    std::string result = base.ConvertType(*type);
    EXPECT_EQ(result, type->Name());
    EXPECT_EQ(ambience.lastTypeName, type->Name());
}

// ---------------------------------------------------------------------------
// IAmbience -- `ConvertConstantValue` dispatches to the override through an `IAmbience*`
// and receives the `PrimitiveValue` (the C++ faithful equivalent of the C# `object` boxed
// literal).
// ---------------------------------------------------------------------------
TEST(IAmbienceTest, ConvertConstantValueDispatchesToOverride)
{
    TestAmbience ambience;
    IAmbience& base = ambience;
    PrimitiveValue value(static_cast<std::int32_t>(42));
    std::string result = base.ConvertConstantValue(value);
    EXPECT_FALSE(result.empty());
    EXPECT_EQ(ambience.lastConstantValue, result);
}

// ---------------------------------------------------------------------------
// IAmbience -- `WrapComment` dispatches to the override through an `IAmbience*` and
// receives the `std::string_view` comment.
// ---------------------------------------------------------------------------
TEST(IAmbienceTest, WrapCommentDispatchesToOverride)
{
    TestAmbience ambience;
    IAmbience& base = ambience;
    std::string result = base.WrapComment("hello");
    EXPECT_EQ(result, "// hello");
    EXPECT_EQ(ambience.lastComment, "hello");
}

// ---------------------------------------------------------------------------
// IAmbience -- has a virtual destructor (a concrete subclass can be deleted through an
// `IAmbience*`), the established abstract-base contract.
// ---------------------------------------------------------------------------
TEST(IAmbienceTest, HasVirtualDestructor)
{
    static_assert(std::has_virtual_destructor_v<IAmbience>,
        "IAmbience must have a virtual destructor for abstract-base deletion");
    std::unique_ptr<IAmbience> owned = std::make_unique<TestAmbience>();
    owned.reset();
    SUCCEED();
}
