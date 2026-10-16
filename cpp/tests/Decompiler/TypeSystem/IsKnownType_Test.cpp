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

// Tests for `TypeSystemExtensions.IsKnownType`/`IsArrayInterfaceType` (D507). The C#
// `IsKnownType(this IType type, KnownTypeCode knownType)`:
//   var def = type.GetDefinition(); return def != null && def.KnownTypeCode == knownType;
// (For generic known types, true for any parameterization AND the definition itself.)
// `IsArrayInterfaceType(this IType type)`: `type.TypeParameterCount == 1` and the definition's
// `KnownTypeCode` is one of `IEnumerableOfT`/`ICollectionOfT`/`IListOfT`/`IReadOnlyCollectionOfT`/
// `IReadOnlyListOfT`.
//
// The tests pin:
//  (a) `IsKnownType`: a `KnownType(Object)` -> true for `KnownTypeCode::Object`;
//  (b) `IsKnownType`: a `KnownType(Int32)` -> false for `KnownTypeCode::Object`;
//  (c) `IsKnownType`: a non-definition type (a `SpecialType(Unknown)`, `GetDefinition` null) -> false;
//  (d) `IsArrayInterfaceType`: a `KnownType(IEnumerableOfT)`-parameterized type -> true (TPC==1, KTC match);
//  (e) `IsArrayInterfaceType`: a `KnownType(Object)` (TPC==0) -> false;
//  (f) `IsArrayInterfaceType`: a non-definition type -> false.

#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsArrayInterfaceType;
using ILSpy::Decompiler::TypeSystem::IsKnownType;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A `LookupTypeDefinition` with a configurable `KnownTypeCode` (for the array-interface test).
std::shared_ptr<LookupTypeDefinition> MakeDef(std::string name, int tpc, KnownTypeCode ktc) {
    return std::make_shared<LookupTypeDefinition>(std::move(name), "",
        FullTypeName(TopLevelTypeName("", name, tpc)), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr, ktc);
}

// A `KnownType` has no `ITypeDefinition` (`GetDefinition()` returns null), so for the `IsKnownType`
// match tests, use a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition()` returns `this`)
// with the desired `KnownTypeCode`.
std::shared_ptr<LookupTypeDefinition> ObjectDef() {
    return MakeDef("Object", 0, KnownTypeCode::Object);
}
std::shared_ptr<LookupTypeDefinition> Int32Def() {
    return MakeDef("Int32", 0, KnownTypeCode::Int32);
}

} // namespace

// ---------------------------------------------------------------------------
// IsKnownType: an Object def -> true for Object; an Int32 def -> false for Object.
// ---------------------------------------------------------------------------
TEST(IsKnownTypeTest, KnownTypeMatches) {
    EXPECT_TRUE(IsKnownType(*ObjectDef(), KnownTypeCode::Object));
    EXPECT_FALSE(IsKnownType(*Int32Def(), KnownTypeCode::Object));
    EXPECT_TRUE(IsKnownType(*Int32Def(), KnownTypeCode::Int32));
}

// ---------------------------------------------------------------------------
// IsKnownType: a non-definition type (a SpecialType(Unknown), GetDefinition null) -> false.
// ---------------------------------------------------------------------------
TEST(IsKnownTypeTest, NonDefinitionYieldsFalse) {
    ITypePtr unknown = ILSpy::Decompiler::TypeSystem::UnknownType();
    EXPECT_FALSE(IsKnownType(*unknown, KnownTypeCode::Object));
}

// ---------------------------------------------------------------------------
// IsArrayInterfaceType: a parameterized IEnumerable<T> -> true (TPC==1, KTC IEnumerableOfT).
// ---------------------------------------------------------------------------
TEST(IsArrayInterfaceTypeTest, ParameterizedIEnumerableYieldsTrue) {
    auto enumerableDef = MakeDef("Enumerable`1", 1, KnownTypeCode::IEnumerableOfT);
    auto pt = std::make_shared<ParameterizedType>(enumerableDef, std::vector<ITypePtr>{Int32()});
    EXPECT_TRUE(IsArrayInterfaceType(*pt));
}

// ---------------------------------------------------------------------------
// IsArrayInterfaceType: a non-collection interface (TPC==1 but KTC != the 5) -> false.
// ---------------------------------------------------------------------------
TEST(IsArrayInterfaceTypeTest, NonCollectionInterfaceYieldsFalse) {
    // A type with TPC==1 but KTC None (not a collection interface).
    auto otherDef = MakeDef("Other`1", 1, KnownTypeCode::None);
    auto pt = std::make_shared<ParameterizedType>(otherDef, std::vector<ITypePtr>{Int32()});
    EXPECT_FALSE(IsArrayInterfaceType(*pt));
}

// ---------------------------------------------------------------------------
// IsArrayInterfaceType: Object (TPC==0) -> false.
// ---------------------------------------------------------------------------
TEST(IsArrayInterfaceTypeTest, NonGenericYieldsFalse) {
    EXPECT_FALSE(IsArrayInterfaceType(*Object()));
}

// ---------------------------------------------------------------------------
// IsArrayInterfaceType: a non-definition type (TPC==1 via a SpecialType) -> GetDefinition null -> false.
// (SpecialType has TPC 0, so this is the TPC!=1 arm; the UnknownType() case.)
// ---------------------------------------------------------------------------
TEST(IsArrayInterfaceTypeTest, NonDefinitionYieldsFalse) {
    ITypePtr unknown = ILSpy::Decompiler::TypeSystem::UnknownType();
    EXPECT_FALSE(IsArrayInterfaceType(*unknown));
}
