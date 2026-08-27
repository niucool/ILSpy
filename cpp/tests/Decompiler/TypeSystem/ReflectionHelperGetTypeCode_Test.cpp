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

// Tests for `ReflectionHelper.GetTypeCode` (D513) -- the numeric-type-code lookup. The C#
// `TypeCode GetTypeCode(this IType type)`: `dynamic_cast` to `ITypeDefinition`; if `KnownTypeCode <=
// String && != Void`, return `(TypeCode)knownTypeCode` (numeric cast); else `Empty`. A non-definition
// type (e.g. `KnownType`) is not an `ITypeDefinition` and yields `Empty`. The `KnownTypeCode` values
// 0-17 align with `TypeCode` 0-17 (None<->Empty, rest identity).

#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetTypeCode;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a `KnownTypeCode`.
std::shared_ptr<LookupTypeDefinition> MakeDef(std::string name, KnownTypeCode ktc) {
    return std::make_shared<LookupTypeDefinition>(std::move(name), "",
        FullTypeName(TopLevelTypeName("", name, 0)), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr, ktc);
}

} // namespace

// ---------------------------------------------------------------------------
// A primitive definition (Int32) -> TypeCode::Int32 (the numeric cast identity).
// ---------------------------------------------------------------------------
TEST(ReflectionHelperGetTypeCodeTest, PrimitiveDefinitionYieldsTypeCode) {
    EXPECT_EQ(GetTypeCode(*MakeDef("Int32", KnownTypeCode::Int32)), TypeCode::Int32);
    EXPECT_EQ(GetTypeCode(*MakeDef("Object", KnownTypeCode::Object)), TypeCode::Object);
    EXPECT_EQ(GetTypeCode(*MakeDef("Char", KnownTypeCode::Char)), TypeCode::Char);
    EXPECT_EQ(GetTypeCode(*MakeDef("String", KnownTypeCode::String)), TypeCode::String);
}

// ---------------------------------------------------------------------------
// A non-primitive known definition (e.g. a collection interface, past String) -> Empty.
// ---------------------------------------------------------------------------
TEST(ReflectionHelperGetTypeCodeTest, NonPrimitiveDefinitionYieldsEmpty) {
    // IEnumerableOfT is well past String in KnownTypeCode.
    EXPECT_EQ(GetTypeCode(*MakeDef("IEnumerable`1", KnownTypeCode::IEnumerableOfT)), TypeCode::Empty);
    // Void is past String (and explicitly excluded by the != Void guard).
    EXPECT_EQ(GetTypeCode(*MakeDef("Void", KnownTypeCode::Void)), TypeCode::Empty);
}

// ---------------------------------------------------------------------------
// A `KnownType` (a non-definition type -- not an `ITypeDefinition`) -> Empty.
// ---------------------------------------------------------------------------
TEST(ReflectionHelperGetTypeCodeTest, NonDefinitionTypeYieldsEmpty) {
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_EQ(GetTypeCode(*int32), TypeCode::Empty);
    auto obj = std::make_shared<KnownType>(KnownTypeCode::Object);
    EXPECT_EQ(GetTypeCode(*obj), TypeCode::Empty);
}

// ---------------------------------------------------------------------------
// The `KnownTypeCode -> TypeCode` numeric-cast alignment for the full primitive range (0-17).
// ---------------------------------------------------------------------------
TEST(ReflectionHelperGetTypeCodeTest, FullPrimitiveRangeAlignment) {
    struct Pair { KnownTypeCode ktc; TypeCode tc; };
    const Pair pairs[] = {
        {KnownTypeCode::Object,   TypeCode::Object},
        {KnownTypeCode::DBNull,   TypeCode::DBNull},
        {KnownTypeCode::Boolean,  TypeCode::Boolean},
        {KnownTypeCode::Char,     TypeCode::Char},
        {KnownTypeCode::SByte,    TypeCode::SByte},
        {KnownTypeCode::Byte,     TypeCode::Byte},
        {KnownTypeCode::Int16,    TypeCode::Int16},
        {KnownTypeCode::UInt16,   TypeCode::UInt16},
        {KnownTypeCode::Int32,    TypeCode::Int32},
        {KnownTypeCode::UInt32,   TypeCode::UInt32},
        {KnownTypeCode::Int64,    TypeCode::Int64},
        {KnownTypeCode::UInt64,   TypeCode::UInt64},
        {KnownTypeCode::Single,   TypeCode::Single},
        {KnownTypeCode::Double,   TypeCode::Double},
        {KnownTypeCode::Decimal,  TypeCode::Decimal},
        {KnownTypeCode::DateTime, TypeCode::DateTime},
        {KnownTypeCode::String,   TypeCode::String},
    };
    int i = 0;
    for (const auto& p : pairs) {
        auto def = MakeDef("T" + std::to_string(i++), p.ktc);
        EXPECT_EQ(GetTypeCode(*def), p.tc) << "pair " << i;
    }
}
