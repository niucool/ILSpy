// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE, NONINFRINGEMENT, OR AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the ReflectionDisassembler::WriteValue / WriteSimpleValue port
// (ReflectionDisassembler.cs lines 768-833) -- the security-declaration /
// custom-attribute-blob argument renderers: the "type(name(value))" wrapper
// over the pair-typed argument, the object(...) boxing wrapper, the
// elementType[Len](item item) array form, and the string / type /
// primitive WriteSimpleValue arms. The C# keeps both members private (the
// WriteDecodedCustomAttributeBlob and TryDecodeSecurityDeclaration paths
// drive them); the port keeps them public so the tests can drive them
// directly.

#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Disassembler/SecurityDeclarationDecoder.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <string>
#include <vector>

namespace {

namespace TD = ILSpy::Decompiler::Disassembler;
namespace TO = ILSpy::Decompiler::Output;

using TD::SecurityDeclarationType;
using TypedArgument = ILSpy::Decompiler::TypeSystem::
    CustomAttributeTypedArgumentT<SecurityDeclarationType>;

constexpr auto kNoCode = static_cast<ILSpy::Decompiler::Metadata::PrimitiveTypeCode>(0);

// The C# `(PrimitiveTypeCode Code, string Name)` pair helper.
SecurityDeclarationType Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode code,
    std::optional<std::string> name) {
    return SecurityDeclarationType{code, std::move(name)};
}

std::string Render(const SecurityDeclarationType& type, const std::any& value) {
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler disassembler(output);
    disassembler.WriteValue(output, type, value);
    return output.ToString();
}

std::string RenderSimple(const std::any& value, const std::string& typeName) {
    TO::PlainTextOutput output;
    TD::ReflectionDisassembler::WriteSimpleValue(output, value, typeName);
    return output.ToString();
}

TEST(WriteValueTest, PrimitiveWrappersRenderTheTypeAndValue) {
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Boolean,
                     std::nullopt),
                  std::any(true)),
        "bool(true)");
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Boolean,
                     std::nullopt),
                  std::any(false)),
        "bool(false)");
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32,
                     std::nullopt),
                  std::any(static_cast<std::int32_t>(42))),
        "int32(42)");
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Single,
                     std::nullopt),
                  std::any(1.5f)),
        "float32(1.5)");
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int64,
                     std::nullopt),
                  std::any(static_cast<std::int64_t>(-9223372036854775807LL - 1))),
        "int64(-9223372036854775808)");
}

TEST(WriteValueTest, EnumTypeRendersThroughThePrimitiveCode) {
    // An "enum "-prefixed name falls back to PrimitiveTypeCodeToString(Code)
    // for the wrapper spelling.
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32,
                     std::string("enum NS.MyEnum")),
                  std::any(static_cast<std::int32_t>(5))),
        "int32(5)");
}

TEST(WriteValueTest, NamedNonEnumTypeRendersTheNameWrapper) {
    EXPECT_EQ(Render(Pair(kNoCode, std::string("System.DateTime")),
                  std::any(static_cast<std::int64_t>(634000))),
        "System.DateTime(634000)");
}

TEST(WriteValueTest, StringValuesRenderSingleQuoted) {
    // The string arm: "'" + EscapeString(value).Replace("'", "\\'") + "'".
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::String,
                     std::nullopt),
                  std::any(std::string("it's"))),
        "string('it\\'s')");
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::String,
                     std::nullopt),
                  std::any(std::string("a\"b"))),
        "string('a\\\"b')");
}

TEST(WriteValueTest, TypeValuesStripTheEnumPrefix) {
    EXPECT_EQ(Render(Pair(kNoCode, std::string("type")),
                  std::any(Pair(kNoCode, std::string("enum NS.MyEnum")))),
        "type(NS.MyEnum)");
    EXPECT_EQ(Render(Pair(kNoCode, std::string("type")),
                  std::any(Pair(kNoCode, std::string("System.String")))),
        "type(System.String)");
}

TEST(WriteValueTest, BoxedValuesRenderTheObjectWrapper) {
    // The boxing arm: "object(" + the inner WriteValue + ")".
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Object,
                     std::nullopt),
                  std::any(TypedArgument{
                      Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::String,
                          std::nullopt),
                      std::any(std::string("abc"))})),
        "object(string('abc'))");
    // A nested boxing: object(object(...)).
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Object,
                     std::nullopt),
                  std::any(TypedArgument{
                      Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Object,
                          std::nullopt),
                      std::any(TypedArgument{
                          Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32,
                              std::nullopt),
                          std::any(static_cast<std::int32_t>(7))})})),
        "object(object(int32(7)))");
}

TEST(WriteValueTest, ArraysRenderTheElementTypeAndCount) {
    // The primitive-named array: the "[]" is stripped from the name.
    std::vector<TypedArgument> three{
        TypedArgument{
            Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32, std::nullopt),
            std::any(static_cast<std::int32_t>(1))},
        TypedArgument{
            Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32, std::nullopt),
            std::any(static_cast<std::int32_t>(2))},
        TypedArgument{
            Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32, std::nullopt),
            std::any(static_cast<std::int32_t>(3))}};
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32,
                     std::string("int32[]")),
                  std::any(std::move(three))),
        "int32[3](1 2 3)");

    // The empty array: no separator.
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32,
                     std::string("int32[]")),
                  std::any(std::vector<TypedArgument>{})),
        "int32[0]()");
}

TEST(WriteValueTest, EnumNamedArraysFallBackToThePrimitiveElement) {
    // An "enum "-prefixed array name does NOT strip the "[]": the element
    // type falls back to PrimitiveTypeCodeToString(Code).
    std::vector<TypedArgument> two{
        TypedArgument{
            Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32,
                std::string("enum X")),
            std::any(static_cast<std::int32_t>(1))},
        TypedArgument{
            Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32,
                std::string("enum X")),
            std::any(static_cast<std::int32_t>(2))}};
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Int32,
                     std::string("enum X[]")),
                  std::any(std::move(two))),
        "int32[2](1 2)");
}

TEST(WriteValueTest, NamedArraysRenderTheStrippedElementType) {
    // A named (non-primitive) array: the "[]" is stripped, and the items go
    // through WriteSimpleValue's DEFAULT arm (WriteOperand's double-quoted
    // string form for the string elements).
    std::vector<TypedArgument> two{
        TypedArgument{Pair(kNoCode, std::string("System.String")),
            std::any(std::string("a"))},
        TypedArgument{Pair(kNoCode, std::string("System.String")),
            std::any(std::string("b"))}};
    EXPECT_EQ(Render(Pair(kNoCode, std::string("System.String[]")),
                  std::any(std::move(two))),
        "System.String[2](\"a\" \"b\")");
}

TEST(WriteValueTest, ArraysWithBoxedItemsRenderTheObjectWrapper) {
    std::vector<TypedArgument> one{
        TypedArgument{Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Object,
                           std::nullopt),
            std::any(TypedArgument{
                Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::String,
                    std::nullopt),
                std::any(std::string("x"))})}};
    EXPECT_EQ(Render(Pair(ILSpy::Decompiler::Metadata::PrimitiveTypeCode::Object,
                     std::string("object[]")),
                  std::any(std::move(one))),
        // The C# array loop renders a boxed item through WriteValue over
        // the item's INNER (type, value) -- no extra object() wrapper.
        "object[1](string('x'))");
}

TEST(WriteSimpleValueTest, TheThreeArms) {
    // The string arm: the single-quoted escaped form.
    EXPECT_EQ(RenderSimple(std::any(std::string("v'w")), "string"),
        "'v\\'w'");
    // The type arm: the "enum " prefix stripped.
    EXPECT_EQ(RenderSimple(
                  std::any(Pair(kNoCode, std::string("enum NS.MyEnum"))), "type"),
        "NS.MyEnum");
    EXPECT_EQ(RenderSimple(
                  std::any(Pair(kNoCode, std::string("System.String"))), "type"),
        "System.String");
    // The default arm: DisassemblerHelpers.WriteOperand.
    EXPECT_EQ(RenderSimple(std::any(static_cast<std::int32_t>(9)), "int32"), "9");
    EXPECT_EQ(RenderSimple(std::any(std::string("s")), "System.String"),
        "\"s\"");
    EXPECT_EQ(RenderSimple(std::any(true), "bool"), "true");
}

} // namespace
