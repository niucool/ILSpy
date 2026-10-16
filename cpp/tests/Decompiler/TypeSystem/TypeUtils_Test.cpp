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

// Tests for the type-system helpers the next in-order per-statement child of
// StatementTransform, TransformAssignment, needs (D126 foundation): the size /
// small-integer / sign queries on PrimitiveType and IType, plus the SwapSign
// helper. These mirror ICSharpCode.Decompiler/IL/ILTypeExtensions.cs
// (GetSize/IsSmallIntegerType/GetSign on PrimitiveType) and TypeUtils.cs
// (kNativeIntSize/GetSize/IsSmallIntegerType/IsCSharpSmallIntegerType on IType)
// and TransformAssignment.SwapSign, plus the GetEnumUnderlyingType leaf
// (TypeUtils.cs line 323) that resolves an enum to its underlying type -- the
// prerequisite the CSharpResolver Convert region and the enum-aware
// TypeUtils call sites consume. The compound-assignment validation
// (ValidateCompoundAssign / NumericCompoundAssign.IsBinaryCompatibleWithType)
// and the post-inc/dec sign-mismatch fixup consult these; the BNI Sign /
// input-type reconciliation is a separate deferred slice, so the helpers are
// ported here as a tested-but-not-yet-wired foundation ahead of the transform.

#include "Decompiler/IL/PrimitiveType.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>

using ILSpy::Decompiler::IL::GetSize;
using ILSpy::Decompiler::IL::GetSign;
using ILSpy::Decompiler::IL::IsSmallIntegerType;
using ILSpy::Decompiler::IL::PrimitiveType;
namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::Accessibility;
using TS::FullTypeName;
using TS::IType;
using TS::ITypeDefinition;
using TS::ITypePtr;
using TS::kNativeIntSize;
using TS::KnownType;
using TS::KnownTypeCode;
using TS::ModifiedType;
using TS::Sign;
using TS::SimpleType;
using TS::SwapSign;
using TS::TopLevelTypeName;
using TS::TypeKind;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupTypeDefinition;

namespace {

ITypePtr KT(KnownTypeCode code) {
    return std::make_shared<KnownType>(code);
}

// The compilation every `LookupTypeDefinition` below binds to (the definition
// ctor requires an `ICompilation&`; no known types are registered because
// `GetEnumUnderlyingType` never consults `FindType`).
LookupCompilation& TestCompilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` of the requested kind (`GetDefinition() == this`,
// the `EnumUnderlyingType` configurable via `SetEnumUnderlyingType`).
std::shared_ptr<LookupTypeDefinition> MakeDef(TypeKind kind) {
    return std::make_shared<LookupTypeDefinition>(
        "E", "Test", FullTypeName(TopLevelTypeName("Test", "E")), kind,
        Accessibility::Public, TestCompilation(), nullptr);
}

// A typical custom modifier: `System.Runtime.CompilerServices.IsConst`.
ITypePtr IsConstModifier() {
    return std::make_shared<SimpleType>(
        TopLevelTypeName("System.Runtime.CompilerServices", "IsConst"));
}

// `modopt(<modifier>)<element>` -- a custom-modifier-decorated type.
ITypePtr ModOptOf(ITypePtr modifier, ITypePtr element) {
    return std::make_shared<ModifiedType>(std::move(modifier), std::move(element),
                                          /*isRequired=*/false);
}

// A `LookupTypeDefinition` whose `GetDefinition()` is null -- the degenerate
// Enum-kind shape where the C# `type.GetDefinition().EnumUnderlyingType`
// deref would NRE.
class DefinitionlessEnum : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    const ITypeDefinition* GetDefinition() const override { return nullptr; }
};

} // namespace

// ---------------------------------------------------------------------------
// GetSize(PrimitiveType) -- faithful port of ILTypeExtensions.GetSize.
// ---------------------------------------------------------------------------
TEST(PrimitiveTypeGetSizeTest, ReturnsByteSizesForEachPrimitive)
{
    EXPECT_EQ(GetSize(PrimitiveType::I1), 1);
    EXPECT_EQ(GetSize(PrimitiveType::U1), 1);
    EXPECT_EQ(GetSize(PrimitiveType::I2), 2);
    EXPECT_EQ(GetSize(PrimitiveType::U2), 2);
    EXPECT_EQ(GetSize(PrimitiveType::I4), 4);
    EXPECT_EQ(GetSize(PrimitiveType::U4), 4);
    EXPECT_EQ(GetSize(PrimitiveType::R4), 4);
    EXPECT_EQ(GetSize(PrimitiveType::I8), 8);
    EXPECT_EQ(GetSize(PrimitiveType::U8), 8);
    EXPECT_EQ(GetSize(PrimitiveType::R8), 8);
    EXPECT_EQ(GetSize(PrimitiveType::R), 8);
    EXPECT_EQ(GetSize(PrimitiveType::I), kNativeIntSize);
    EXPECT_EQ(GetSize(PrimitiveType::U), kNativeIntSize);
    EXPECT_EQ(GetSize(PrimitiveType::Ref), kNativeIntSize);
    EXPECT_EQ(GetSize(PrimitiveType::None), 0);
    EXPECT_EQ(GetSize(PrimitiveType::Unknown), 0);
}

// ---------------------------------------------------------------------------
// IsSmallIntegerType(PrimitiveType) -- GetSize < 4: I1/U1/I2/U2 are small.
// ---------------------------------------------------------------------------
TEST(PrimitiveTypeIsSmallIntegerTest, SmallPrimitivesAreLessThanFourBytes)
{
    EXPECT_TRUE(IsSmallIntegerType(PrimitiveType::I1));
    EXPECT_TRUE(IsSmallIntegerType(PrimitiveType::U1));
    EXPECT_TRUE(IsSmallIntegerType(PrimitiveType::I2));
    EXPECT_TRUE(IsSmallIntegerType(PrimitiveType::U2));
    EXPECT_FALSE(IsSmallIntegerType(PrimitiveType::I4));
    EXPECT_FALSE(IsSmallIntegerType(PrimitiveType::U4));
    EXPECT_FALSE(IsSmallIntegerType(PrimitiveType::I8));
    EXPECT_FALSE(IsSmallIntegerType(PrimitiveType::U8));
    EXPECT_FALSE(IsSmallIntegerType(PrimitiveType::R4));
    EXPECT_FALSE(IsSmallIntegerType(PrimitiveType::I));
    EXPECT_FALSE(IsSmallIntegerType(PrimitiveType::U));
    EXPECT_FALSE(IsSmallIntegerType(PrimitiveType::Ref));
}

// ---------------------------------------------------------------------------
// GetSign(PrimitiveType) -- faithful port of ILTypeExtensions.GetSign.
// ---------------------------------------------------------------------------
TEST(PrimitiveTypeGetSignTest, ReturnsSignedForSignedPrimitives)
{
    EXPECT_EQ(GetSign(PrimitiveType::I1), Sign::Signed);
    EXPECT_EQ(GetSign(PrimitiveType::I2), Sign::Signed);
    EXPECT_EQ(GetSign(PrimitiveType::I4), Sign::Signed);
    EXPECT_EQ(GetSign(PrimitiveType::I8), Sign::Signed);
    EXPECT_EQ(GetSign(PrimitiveType::R4), Sign::Signed);
    EXPECT_EQ(GetSign(PrimitiveType::R8), Sign::Signed);
    EXPECT_EQ(GetSign(PrimitiveType::R), Sign::Signed);
    EXPECT_EQ(GetSign(PrimitiveType::I), Sign::Signed);
    EXPECT_EQ(GetSign(PrimitiveType::U1), Sign::Unsigned);
    EXPECT_EQ(GetSign(PrimitiveType::U2), Sign::Unsigned);
    EXPECT_EQ(GetSign(PrimitiveType::U4), Sign::Unsigned);
    EXPECT_EQ(GetSign(PrimitiveType::U8), Sign::Unsigned);
    EXPECT_EQ(GetSign(PrimitiveType::U), Sign::Unsigned);
    EXPECT_EQ(GetSign(PrimitiveType::Ref), Sign::None);
    EXPECT_EQ(GetSign(PrimitiveType::None), Sign::None);
    EXPECT_EQ(GetSign(PrimitiveType::Unknown), Sign::None);
}

// ---------------------------------------------------------------------------
// GetSize(IType*) -- faithful port of TypeUtils.GetSize: the primitive
// KnownTypes report their byte sizes; pointer-sized kinds report
// kNativeIntSize; unknown/non-primitive types report 0.
// ---------------------------------------------------------------------------
TEST(ITypeGetSizeTest, ReturnsByteSizesForKnownPrimitiveTypes)
{
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Boolean).get()), 1);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::SByte).get()), 1);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Byte).get()), 1);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Char).get()), 2);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Int16).get()), 2);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::UInt16).get()), 2);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Int32).get()), 4);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::UInt32).get()), 4);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Single).get()), 4);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Int64).get()), 8);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::UInt64).get()), 8);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Double).get()), 8);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::IntPtr).get()), kNativeIntSize);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::UIntPtr).get()), kNativeIntSize);
    // A reference type (Class kind) reports the native-int size (the C# uses
    // NativeIntSize for Class/Pointer/ByRef/NInt/NUInt).
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Object).get()), kNativeIntSize);
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::String).get()), kNativeIntSize);
    // Void / unknown / null report 0.
    EXPECT_EQ(TS::GetSize(KT(KnownTypeCode::Void).get()), 0);
    EXPECT_EQ(TS::GetSize(nullptr), 0);
}

// ---------------------------------------------------------------------------
// IsSmallIntegerType(IType*) -- GetSize > 0 && GetSize < 4: the small integer
// KnownTypes (Boolean/SByte/Byte/Char/Int16/UInt16) are small; I4/I8/etc are
// not; Void/unknown (size 0) are not (the > 0 guard).
// ---------------------------------------------------------------------------
TEST(ITypeIsSmallIntegerTest, SmallIntegerKnownTypesAreSmall)
{
    EXPECT_TRUE(TS::IsSmallIntegerType(KT(KnownTypeCode::Boolean).get()));
    EXPECT_TRUE(TS::IsSmallIntegerType(KT(KnownTypeCode::SByte).get()));
    EXPECT_TRUE(TS::IsSmallIntegerType(KT(KnownTypeCode::Byte).get()));
    EXPECT_TRUE(TS::IsSmallIntegerType(KT(KnownTypeCode::Char).get()));
    EXPECT_TRUE(TS::IsSmallIntegerType(KT(KnownTypeCode::Int16).get()));
    EXPECT_TRUE(TS::IsSmallIntegerType(KT(KnownTypeCode::UInt16).get()));
    EXPECT_FALSE(TS::IsSmallIntegerType(KT(KnownTypeCode::Int32).get()));
    EXPECT_FALSE(TS::IsSmallIntegerType(KT(KnownTypeCode::UInt32).get()));
    EXPECT_FALSE(TS::IsSmallIntegerType(KT(KnownTypeCode::Int64).get()));
    EXPECT_FALSE(TS::IsSmallIntegerType(KT(KnownTypeCode::Single).get()));
    EXPECT_FALSE(TS::IsSmallIntegerType(KT(KnownTypeCode::Object).get()));
    EXPECT_FALSE(TS::IsSmallIntegerType(KT(KnownTypeCode::Void).get()));
    EXPECT_FALSE(TS::IsSmallIntegerType(nullptr));
}

// ---------------------------------------------------------------------------
// TS::IsCSharpSmallIntegerType(IType*) -- only byte/sbyte/short/ushort (not bool,
// char, or enum). C# numeric-promotes a small integer to int, so the compound-
// assignment validation requires a signed binary only for these.
// ---------------------------------------------------------------------------
TEST(ITypeIsCSharpSmallIntegerTest, OnlyByteSbyteShortUshortAreCSharpSmallIntegers)
{
    EXPECT_TRUE(TS::IsCSharpSmallIntegerType(KT(KnownTypeCode::Byte).get()));
    EXPECT_TRUE(TS::IsCSharpSmallIntegerType(KT(KnownTypeCode::SByte).get()));
    EXPECT_TRUE(TS::IsCSharpSmallIntegerType(KT(KnownTypeCode::Int16).get()));
    EXPECT_TRUE(TS::IsCSharpSmallIntegerType(KT(KnownTypeCode::UInt16).get()));
    // bool / char are ILAst-small but NOT C#-small.
    EXPECT_FALSE(TS::IsCSharpSmallIntegerType(KT(KnownTypeCode::Boolean).get()));
    EXPECT_FALSE(TS::IsCSharpSmallIntegerType(KT(KnownTypeCode::Char).get()));
    // Int32/etc are not small.
    EXPECT_FALSE(TS::IsCSharpSmallIntegerType(KT(KnownTypeCode::Int32).get()));
    EXPECT_FALSE(TS::IsCSharpSmallIntegerType(KT(KnownTypeCode::Object).get()));
    EXPECT_FALSE(TS::IsCSharpSmallIntegerType(nullptr));
}

// ---------------------------------------------------------------------------
// SwapSign(IType*) -- the opposite-sign KnownType for a primitive integer.
// ---------------------------------------------------------------------------
TEST(SwapSignTest, ReturnsOppositeSignKnownTypeForPrimitives)
{
    auto check = [](KnownTypeCode in, KnownTypeCode out) {
        auto got = SwapSign(KT(in).get());
        ASSERT_NE(got, nullptr);
        auto* k = dynamic_cast<KnownType*>(got.get());
        ASSERT_NE(k, nullptr);
        EXPECT_EQ(k->Code(), out);
    };
    check(KnownTypeCode::SByte, KnownTypeCode::Byte);
    check(KnownTypeCode::Byte, KnownTypeCode::SByte);
    check(KnownTypeCode::Int16, KnownTypeCode::UInt16);
    check(KnownTypeCode::UInt16, KnownTypeCode::Int16);
    check(KnownTypeCode::Char, KnownTypeCode::Int16); // Char is U2 -> Int16
    check(KnownTypeCode::Int32, KnownTypeCode::UInt32);
    check(KnownTypeCode::UInt32, KnownTypeCode::Int32);
    check(KnownTypeCode::Int64, KnownTypeCode::UInt64);
    check(KnownTypeCode::UInt64, KnownTypeCode::Int64);
    check(KnownTypeCode::IntPtr, KnownTypeCode::UIntPtr);
    check(KnownTypeCode::UIntPtr, KnownTypeCode::IntPtr);
}

TEST(SwapSignTest, ReturnsNullForNonIntegerTypes)
{
    // The C# throws ArgumentException for a type with no opposite sign; this
    // port returns nullptr (the callers only consult SwapSign after a sign-
    // mismatch guard, so a nullptr propagates as a no-fold).
    EXPECT_EQ(SwapSign(KT(KnownTypeCode::Single).get()), nullptr);
    EXPECT_EQ(SwapSign(KT(KnownTypeCode::Double).get()), nullptr);
    EXPECT_EQ(SwapSign(KT(KnownTypeCode::Boolean).get()), nullptr);
    EXPECT_EQ(SwapSign(KT(KnownTypeCode::Object).get()), nullptr);
    EXPECT_EQ(SwapSign(nullptr), nullptr);
}

// ---------------------------------------------------------------------------
// GetEnumUnderlyingType(IType*) -- faithful port of TypeUtils.GetEnumUnderlyingType:
// an enum resolves to its definition's underlying type; every other type passes
// through the SkipModifiers-unwrapped input unchanged.
// ---------------------------------------------------------------------------
TEST(GetEnumUnderlyingTypeTest, ReturnsNullForNullInput)
{
    // The C# extension method would NRE on a null receiver; the port's
    // nullable-IType convention yields null.
    EXPECT_EQ(TS::GetEnumUnderlyingType(nullptr), nullptr);
}

TEST(GetEnumUnderlyingTypeTest, ReturnsInputForNonEnumTypes)
{
    auto i32 = KT(KnownTypeCode::Int32);
    auto str = KT(KnownTypeCode::String);
    auto cls = MakeDef(TypeKind::Class);
    auto st = MakeDef(TypeKind::Struct);
    EXPECT_EQ(TS::GetEnumUnderlyingType(i32.get()), i32.get());
    EXPECT_EQ(TS::GetEnumUnderlyingType(str.get()), str.get());
    EXPECT_EQ(TS::GetEnumUnderlyingType(cls.get()), cls.get());
    EXPECT_EQ(TS::GetEnumUnderlyingType(st.get()), st.get());
}

TEST(GetEnumUnderlyingTypeTest, ReturnsUnderlyingTypeForEnum)
{
    auto underlying = KT(KnownTypeCode::Int32);
    auto def = MakeDef(TypeKind::Enum);
    def->SetEnumUnderlyingType(underlying);
    EXPECT_EQ(TS::GetEnumUnderlyingType(def.get()), underlying.get());
    // Not the enum itself: the underlying type replaces the enum.
    EXPECT_NE(TS::GetEnumUnderlyingType(def.get()), def.get());
}

TEST(GetEnumUnderlyingTypeTest, EnumWithoutConfiguredUnderlyingYieldsNull)
{
    // A definition whose EnumUnderlyingType is null yields null -- the faithful
    // C# result for a definition that reports no underlying type (a real enum
    // always carries one; this pins the port's null contract for the stub shape).
    auto def = MakeDef(TypeKind::Enum);
    EXPECT_EQ(TS::GetEnumUnderlyingType(def.get()), nullptr);
}

TEST(GetEnumUnderlyingTypeTest, UnwrapsModifiersBeforeTheEnumCheck)
{
    // modopt(IsConst) over an enum: the C# rebinds through SkipModifiers()
    // BEFORE the Kind check, so the decorated enum still resolves to its
    // underlying type.
    auto underlying = KT(KnownTypeCode::UInt32);
    auto def = MakeDef(TypeKind::Enum);
    def->SetEnumUnderlyingType(underlying);
    auto modopt = ModOptOf(IsConstModifier(), def);
    EXPECT_EQ(TS::GetEnumUnderlyingType(modopt.get()), underlying.get());
}

TEST(GetEnumUnderlyingTypeTest, PassthroughUnwrapsModifiersForNonEnum)
{
    // The C# `type = type.SkipModifiers()` rebind means a decorated NON-enum
    // passes through as the UNWRAPPED element, not the ModifiedType.
    auto i32 = KT(KnownTypeCode::Int32);
    auto modopt = ModOptOf(IsConstModifier(), i32);
    const IType* result = TS::GetEnumUnderlyingType(modopt.get());
    EXPECT_EQ(result, i32.get());
    EXPECT_NE(result, modopt.get());
}

TEST(GetEnumUnderlyingTypeTest, UnwrapsNestedModifiersOverEnum)
{
    // modopt over modopt over an enum: the SkipModifiers loop unwraps the
    // whole decorator chain before the Kind check.
    auto underlying = KT(KnownTypeCode::Int64);
    auto def = MakeDef(TypeKind::Enum);
    def->SetEnumUnderlyingType(underlying);
    auto inner = ModOptOf(IsConstModifier(), def);
    auto outer = ModOptOf(IsConstModifier(), inner);
    EXPECT_EQ(TS::GetEnumUnderlyingType(outer.get()), underlying.get());
}

TEST(GetEnumUnderlyingTypeTest, ReturnsNullForDegenerateNullElementModifier)
{
    // A ModifiedType with a null element: SkipModifiers yields null, where the
    // C# `type.Kind` deref would NRE; the port's safe fallback is null.
    auto modopt = ModOptOf(IsConstModifier(), nullptr);
    EXPECT_EQ(TS::GetEnumUnderlyingType(modopt.get()), nullptr);
}

TEST(GetEnumUnderlyingTypeTest, EnumWithoutDefinitionYieldsNull)
{
    // An Enum kind whose GetDefinition() is null: the C#
    // `type.GetDefinition().EnumUnderlyingType` deref would NRE; the port's
    // safe fallback is null.
    DefinitionlessEnum def("E", "Test", FullTypeName(TopLevelTypeName("Test", "E")),
                            TypeKind::Enum, Accessibility::Public, TestCompilation(),
                            nullptr);
    EXPECT_EQ(TS::GetEnumUnderlyingType(&def), nullptr);
}
