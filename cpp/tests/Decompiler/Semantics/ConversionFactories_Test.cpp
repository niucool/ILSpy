// Copyright (c) 2026 ILSpy contributors
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
// PURPOSE NONINFRINGEMENT IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the concrete `Conversion` subclasses and the `Conversions` factory accessors
// (cpp/Decompiler/Semantics/ConversionFactories.hpp, the D453 port of the concrete
// subclasses and singleton factory FIELDS from ICSharpCode.Decompiler/Semantics/Conversion.cs).
// This port covers the two subclasses backing the singleton factory fields that take no
// extra state beyond an `isImplicit` flag and a `type` byte -- `InvalidConversion` (backs
// `None`) and `BuiltinConversion` (backs the ~18 builtin-conversion singletons). The tests
// pin the `InvalidConversion` `IsValid`-false / `ToString`-"None" crux, the
// `BuiltinConversion` `isImplicit`/`type`-driven override surface and `ToString` switch, the
// `Conversions` factory singleton identity (reference-equality across calls, faithful to
// the C# `static readonly` field semantics), the factory-to-kind wiring (each factory
// returns the right conversion kind), and the class-shape `static_assert`s. The RED-neuter
// verifies the `None` factory crux (the only factory backed by `InvalidConversion`).

#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>

using ILSpy::Decompiler::Semantics::BuiltinConversion;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::InvalidConversion;
using ILSpy::Decompiler::Semantics::NumericOrEnumerationConversion;
using ILSpy::Decompiler::Semantics::TupleConv;

// ---------------------------------------------------------------------------
// InvalidConversion (backs `Conversions::None`)
// ---------------------------------------------------------------------------

TEST(InvalidConversionTest, IsValidIsFalse)
{
    // The C# `public override bool IsValid => false` -- the load-bearing crux the
    // `ConversionResolveResult.IsError => !IsValid` path reaches for the `None` case.
    InvalidConversion c;
    EXPECT_FALSE(c.IsValid());
}

TEST(InvalidConversionTest, ToStringReturnsNone)
{
    // The C# `public override string ToString() => "None"`.
    InvalidConversion c;
    EXPECT_EQ(c.ToString(), "None");
}

TEST(InvalidConversionTest, InheritsBaseReferenceEqualityAndIdentityHash)
{
    // The C# `InvalidConversion` does NOT override `Equals` / `GetHashCode`, so it inherits
    // the base reference-equality (`this == &other`) and identity hash. A conversion equals
    // itself; two distinct `InvalidConversion` instances are not equal (reference-equality).
    InvalidConversion a;
    InvalidConversion b;
    EXPECT_TRUE(a.Equals(a));
    EXPECT_FALSE(a.Equals(b));
    EXPECT_EQ(a.GetHashCode(), a.GetHashCode());
}

TEST(InvalidConversionTest, InheritsBooleanDiscriminatorDefaults)
{
    // The C# `InvalidConversion` overrides ONLY `IsValid` (and `ToString`); every other
    // conversion-kind discriminator inherits the base default (`false`). Pin a
    // representative spread -- the `None` conversion is none of the specific kinds.
    InvalidConversion c;
    EXPECT_FALSE(c.IsImplicit());
    EXPECT_FALSE(c.IsExplicit());
    EXPECT_FALSE(c.IsIdentityConversion());
    EXPECT_FALSE(c.IsBoxingConversion());
    EXPECT_FALSE(c.IsTryCast());
    EXPECT_FALSE(c.IsTupleConversion());
    EXPECT_EQ(c.Method(), nullptr);
    EXPECT_TRUE(c.ElementConversions().empty());
}

TEST(InvalidConversionTest, IsFinalAndDerivesFromConversion)
{
    static_assert(std::is_final_v<InvalidConversion>,
                  "InvalidConversion is final (mirrors the C# `sealed`).");
    static_assert(std::is_base_of_v<Conversion, InvalidConversion>,
                  "InvalidConversion derives from Conversion.");
    static_assert(std::has_virtual_destructor_v<InvalidConversion>,
                  "InvalidConversion has a virtual destructor (held via base pointers).");
}

// ---------------------------------------------------------------------------
// BuiltinConversion (backs the ~18 builtin-conversion singleton factories)
// ---------------------------------------------------------------------------

TEST(BuiltinConversionTest, CtorStoresIsImplicitAndType)
{
    // The C# `BuiltinConversion(bool isImplicit, byte type)` ctor stores both fields. The
    // `isImplicit` flag drives `IsImplicit` / `IsExplicit`; the `type` byte drives the
    // kind discriminators and the `ToString` switch.
    BuiltinConversion implicitIdentity(true, 0);
    BuiltinConversion explicitReference(false, 3);
    EXPECT_TRUE(implicitIdentity.IsImplicit());
    EXPECT_FALSE(implicitIdentity.IsExplicit());
    EXPECT_FALSE(explicitReference.IsImplicit());
    EXPECT_TRUE(explicitReference.IsExplicit());
}

TEST(BuiltinConversionTest, IsExplicitIsNegationOfIsImplicit)
{
    // The C# `public override bool IsExplicit => !isImplicit` -- `IsExplicit` is the
    // negation of `IsImplicit` (a conversion is implicit OR explicit, never both).
    BuiltinConversion implicit(true, 2);
    BuiltinConversion explicit_(false, 2);
    EXPECT_TRUE(implicit.IsImplicit());
    EXPECT_FALSE(implicit.IsExplicit());
    EXPECT_FALSE(explicit_.IsImplicit());
    EXPECT_TRUE(explicit_.IsExplicit());
}

TEST(BuiltinConversionTest, TypeDiscriminatorsMatchTypeByte)
{
    // The C# `public override bool IsXxx => type == N` -- each kind discriminator is a
    // `type == N` equality. Pin one representative per `type` value 0..13 (the full range
    // the factories use). Exactly one kind is true per `BuiltinConversion`; the others are
    // false. Here we pin the identity (type 0), boxing (type 7), and try-cast (type 9)
    // discriminators across two distinct types to confirm the `type` byte drives the
    // override and the other kinds stay false.
    BuiltinConversion identity(true, 0);
    EXPECT_TRUE(identity.IsIdentityConversion());
    EXPECT_FALSE(identity.IsBoxingConversion());
    EXPECT_FALSE(identity.IsTryCast());

    BuiltinConversion boxing(true, 7);
    EXPECT_FALSE(boxing.IsIdentityConversion());
    EXPECT_TRUE(boxing.IsBoxingConversion());
    EXPECT_FALSE(boxing.IsUnboxingConversion());

    BuiltinConversion tryCast(false, 9);
    EXPECT_TRUE(tryCast.IsTryCast());
    EXPECT_FALSE(tryCast.IsBoxingConversion());
    EXPECT_FALSE(tryCast.IsIdentityConversion());
}

TEST(BuiltinConversionTest, AllTypeDiscriminatorsCovered)
{
    // Iterate the full `type` range 0..13 the factories use, asserting each yields exactly
    // its one kind discriminator true and every other false -- the single check proving no
    // kind was dropped / mis-wired.
    using KindPred = bool (BuiltinConversion::*)() const;
    const KindPred preds[] = {
        &BuiltinConversion::IsIdentityConversion,            // 0
        &BuiltinConversion::IsNullLiteralConversion,         // 1
        &BuiltinConversion::IsConstantExpressionConversion,  // 2
        &BuiltinConversion::IsReferenceConversion,           // 3
        &BuiltinConversion::IsDynamicConversion,            // 4
        &BuiltinConversion::IsNullableConversion,           // 5
        &BuiltinConversion::IsPointerConversion,            // 6
        &BuiltinConversion::IsBoxingConversion,             // 7
        &BuiltinConversion::IsUnboxingConversion,           // 8
        &BuiltinConversion::IsTryCast,                      // 9
        &BuiltinConversion::IsInterpolatedStringConversion, // 10
        &BuiltinConversion::IsThrowExpressionConversion,    // 11
        &BuiltinConversion::IsInlineArrayConversion,        // 12
        &BuiltinConversion::IsImplicitSpanConversion,      // 13
    };
    for (std::uint8_t t = 0; t < 14; ++t) {
        BuiltinConversion c(true, t);
        for (std::uint8_t k = 0; k < 14; ++k) {
            if (k == t) {
                EXPECT_TRUE((c.*(preds[k]))())
                    << "type " << static_cast<int>(t) << " kind " << static_cast<int>(k);
            } else {
                EXPECT_FALSE((c.*(preds[k]))())
                    << "type " << static_cast<int>(t) << " kind " << static_cast<int>(k);
            }
        }
    }
}

TEST(BuiltinConversionTest, ToStringFixedStringKinds)
{
    // The C# `ToString` returns a fixed string for `type` values 0/1/7/8/9/10/11/12/13 (no
    // implicit/explicit prefix). Pin the representative spread.
    EXPECT_EQ(BuiltinConversion(true, 0).ToString(), "identity conversion");
    EXPECT_EQ(BuiltinConversion(true, 1).ToString(), "null-literal conversion");
    EXPECT_EQ(BuiltinConversion(true, 7).ToString(), "boxing conversion");
    EXPECT_EQ(BuiltinConversion(false, 8).ToString(), "unboxing conversion");
    EXPECT_EQ(BuiltinConversion(false, 9).ToString(), "try cast");
    EXPECT_EQ(BuiltinConversion(true, 10).ToString(), "interpolated string");
    EXPECT_EQ(BuiltinConversion(true, 11).ToString(), "throw-expression conversion");
    EXPECT_EQ(BuiltinConversion(true, 12).ToString(), "inline array conversion");
    EXPECT_EQ(BuiltinConversion(true, 13).ToString(), "implicit span conversion");
}

TEST(BuiltinConversionTest, ToStringNamedKindsCarryImplicitExplicitPrefix)
{
    // The C# `ToString` for `type` values 2/3/4/5/6 sets a local `name` then returns
    // `(isImplicit ? "implicit " : "explicit ") + name + " conversion"` -- the named kinds
    // carry the implicit/explicit prefix. Pin both the implicit and explicit variants.
    EXPECT_EQ(BuiltinConversion(true, 2).ToString(), "implicit constant-expression conversion");
    EXPECT_EQ(BuiltinConversion(false, 2).ToString(), "explicit constant-expression conversion");
    EXPECT_EQ(BuiltinConversion(true, 3).ToString(), "implicit reference conversion");
    EXPECT_EQ(BuiltinConversion(false, 3).ToString(), "explicit reference conversion");
    EXPECT_EQ(BuiltinConversion(true, 4).ToString(), "implicit dynamic conversion");
    EXPECT_EQ(BuiltinConversion(false, 4).ToString(), "explicit dynamic conversion");
    EXPECT_EQ(BuiltinConversion(true, 5).ToString(), "implicit nullable conversion");
    EXPECT_EQ(BuiltinConversion(false, 5).ToString(), "explicit nullable conversion");
    EXPECT_EQ(BuiltinConversion(true, 6).ToString(), "implicit pointer conversion");
    EXPECT_EQ(BuiltinConversion(false, 6).ToString(), "explicit pointer conversion");
}

TEST(BuiltinConversionTest, InheritsBaseReferenceEqualityAndIdentityHash)
{
    // The C# `BuiltinConversion` does NOT override `Equals` / `GetHashCode`, so it inherits
    // the base reference-equality and identity hash. Two distinct `BuiltinConversion`
    // instances (even with the same `isImplicit` / `type`) are NOT equal (reference-
    // equality), faithfully matching the C# where the singleton FIELDS are reference-equal
    // but two separately-`new`-ed `BuiltinConversion`s are not.
    BuiltinConversion a(true, 0);
    BuiltinConversion b(true, 0);
    EXPECT_TRUE(a.Equals(a));
    EXPECT_FALSE(a.Equals(b));
    EXPECT_EQ(a.GetHashCode(), a.GetHashCode());
}

TEST(BuiltinConversionTest, IsFinalAndDerivesFromConversion)
{
    static_assert(std::is_final_v<BuiltinConversion>,
                  "BuiltinConversion is final (mirrors the C# `sealed`).");
    static_assert(std::is_base_of_v<Conversion, BuiltinConversion>,
                  "BuiltinConversion derives from Conversion.");
    static_assert(std::has_virtual_destructor_v<BuiltinConversion>,
                  "BuiltinConversion has a virtual destructor (held via base pointers).");
}

// ---------------------------------------------------------------------------
// Conversions factory accessors (the singleton factory FIELDS)
// ---------------------------------------------------------------------------

TEST(ConversionsTest, NoneIsInvalidConversionAndIsValidFalse)
{
    // `Conversions::None()` backs the C# `Conversion.None = new InvalidConversion()` -- the
    // not-a-valid-conversion singleton (`IsValid` false, `ToString` "None"). The load-bearing
    // crux the `ConversionResolveResult.IsError => !IsValid` path reaches for the `None`
    // case.
    auto none = Conversions::None();
    ASSERT_NE(none, nullptr);
    EXPECT_FALSE(none->IsValid());
    // The singleton IS an `InvalidConversion` (the factory builds the right subclass).
    EXPECT_NE(dynamic_cast<InvalidConversion*>(none.get()), nullptr);
}

TEST(ConversionsTest, NoneToStringIsNone)
{
    auto none = Conversions::None();
    // `InvalidConversion::ToString` is "None"; reach it via a downcast (the base omits
    // `ToString`, so the `Conversion*` has no `ToString` to call).
    auto* invalid = dynamic_cast<InvalidConversion*>(none.get());
    ASSERT_NE(invalid, nullptr);
    EXPECT_EQ(invalid->ToString(), "None");
}

TEST(ConversionsTest, NoneIsDistinctFromIdentityConversionSingleton)
{
    // `None` (an `InvalidConversion`) and `IdentityConversion` (a `BuiltinConversion`) are
    // DIFFERENT singletons -- `None().get() != IdentityConversion().get()`. This is the
    // load-bearing distinctness the RED-neuter targets (neutering `None()` to return the
    // `IdentityConversion` singleton makes `None().get() == IdentityConversion().get()`).
    auto none = Conversions::None();
    auto identity = Conversions::IdentityConversion();
    EXPECT_NE(none.get(), identity.get());
    EXPECT_NE(dynamic_cast<InvalidConversion*>(none.get()), nullptr);
    EXPECT_EQ(dynamic_cast<InvalidConversion*>(identity.get()), nullptr);
}

TEST(ConversionsTest, SingletonIdentityIsStableAcrossCalls)
{
    // The C# `static readonly` field is ONE instance shared by every consumer
    // (reference-equal across uses). The `std::shared_ptr<Conversion>` Meyers-singleton
    // accessor preserves this: two calls to the SAME factory return the same instance
    // (`get()` pointer-equal). Pin a representative spread across the `None` and several
    // `BuiltinConversion` factories.
    EXPECT_EQ(Conversions::None().get(), Conversions::None().get());
    EXPECT_EQ(Conversions::IdentityConversion().get(), Conversions::IdentityConversion().get());
    EXPECT_EQ(Conversions::BoxingConversion().get(), Conversions::BoxingConversion().get());
    EXPECT_EQ(Conversions::TryCast().get(), Conversions::TryCast().get());
    EXPECT_EQ(Conversions::ImplicitSpanConversion().get(),
              Conversions::ImplicitSpanConversion().get());
}

TEST(ConversionsTest, DistinctFactoriesReturnDistinctSingletons)
{
    // Each factory FIELDS is a distinct singleton instance (the C# `static readonly` fields
    // are distinct `new`-ed instances). Pin a representative spread: the implicit/explicit
    // pairs are distinct from each other and from the other singletons.
    EXPECT_NE(Conversions::IdentityConversion().get(), Conversions::NullLiteralConversion().get());
    EXPECT_NE(Conversions::IdentityConversion().get(), Conversions::BoxingConversion().get());
    EXPECT_NE(Conversions::ImplicitReferenceConversion().get(),
              Conversions::ExplicitReferenceConversion().get());
    EXPECT_NE(Conversions::ImplicitDynamicConversion().get(),
              Conversions::ExplicitDynamicConversion().get());
    EXPECT_NE(Conversions::BoxingConversion().get(), Conversions::UnboxingConversion().get());
}

TEST(ConversionsTest, IdentityConversionIsIdentityAndImplicit)
{
    // `Conversion.IdentityConversion = new BuiltinConversion(true, 0)` -- the identity
    // conversion (`IsIdentityConversion` true, `IsImplicit` true, valid).
    auto c = Conversions::IdentityConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsValid());
    EXPECT_TRUE(c->IsIdentityConversion());
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_FALSE(c->IsExplicit());
}

TEST(ConversionsTest, NullLiteralConversionIsNotNullLiteralAndImplicit)
{
    // `Conversion.NullLiteralConversion = new BuiltinConversion(true, 1)`.
    auto c = Conversions::NullLiteralConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsNullLiteralConversion());
    EXPECT_TRUE(c->IsImplicit());
}

TEST(ConversionsTest, ImplicitConstantExpressionConversionWiring)
{
    // `Conversion.ImplicitConstantExpressionConversion = new BuiltinConversion(true, 2)`.
    auto c = Conversions::ImplicitConstantExpressionConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsConstantExpressionConversion());
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_FALSE(c->IsExplicit());
}

TEST(ConversionsTest, ReferenceConversionImplicitExplicitPair)
{
    // `Conversion.ImplicitReferenceConversion = new BuiltinConversion(true, 3)` and
    // `Conversion.ExplicitReferenceConversion = new BuiltinConversion(false, 3)` -- the
    // implicit/explicit reference-conversion pair share `type == 3` but differ in
    // `isImplicit`.
    auto implicit_ = Conversions::ImplicitReferenceConversion();
    auto explicit_ = Conversions::ExplicitReferenceConversion();
    ASSERT_NE(implicit_, nullptr);
    ASSERT_NE(explicit_, nullptr);
    EXPECT_TRUE(implicit_->IsReferenceConversion());
    EXPECT_TRUE(explicit_->IsReferenceConversion());
    EXPECT_TRUE(implicit_->IsImplicit());
    EXPECT_FALSE(implicit_->IsExplicit());
    EXPECT_FALSE(explicit_->IsImplicit());
    EXPECT_TRUE(explicit_->IsExplicit());
}

TEST(ConversionsTest, DynamicConversionImplicitExplicitPair)
{
    auto implicit_ = Conversions::ImplicitDynamicConversion();
    auto explicit_ = Conversions::ExplicitDynamicConversion();
    EXPECT_TRUE(implicit_->IsDynamicConversion());
    EXPECT_TRUE(implicit_->IsImplicit());
    EXPECT_TRUE(explicit_->IsDynamicConversion());
    EXPECT_TRUE(explicit_->IsExplicit());
}

TEST(ConversionsTest, NullableConversionImplicitExplicitPair)
{
    auto implicit_ = Conversions::ImplicitNullableConversion();
    auto explicit_ = Conversions::ExplicitNullableConversion();
    EXPECT_TRUE(implicit_->IsNullableConversion());
    EXPECT_TRUE(implicit_->IsImplicit());
    EXPECT_TRUE(explicit_->IsNullableConversion());
    EXPECT_TRUE(explicit_->IsExplicit());
}

TEST(ConversionsTest, PointerConversionImplicitExplicitPair)
{
    auto implicit_ = Conversions::ImplicitPointerConversion();
    auto explicit_ = Conversions::ExplicitPointerConversion();
    EXPECT_TRUE(implicit_->IsPointerConversion());
    EXPECT_TRUE(implicit_->IsImplicit());
    EXPECT_TRUE(explicit_->IsPointerConversion());
    EXPECT_TRUE(explicit_->IsExplicit());
}

TEST(ConversionsTest, BoxingAndUnboxingConversionWiring)
{
    // `Conversion.BoxingConversion = new BuiltinConversion(true, 7)` and
    // `Conversion.UnboxingConversion = new BuiltinConversion(false, 8)`.
    auto boxing = Conversions::BoxingConversion();
    auto unboxing = Conversions::UnboxingConversion();
    EXPECT_TRUE(boxing->IsBoxingConversion());
    EXPECT_TRUE(boxing->IsImplicit());
    EXPECT_FALSE(boxing->IsUnboxingConversion());
    EXPECT_TRUE(unboxing->IsUnboxingConversion());
    EXPECT_TRUE(unboxing->IsExplicit());
    EXPECT_FALSE(unboxing->IsBoxingConversion());
}

TEST(ConversionsTest, TryCastIsTryCastAndExplicit)
{
    // `Conversion.TryCast = new BuiltinConversion(false, 9)` -- the C# `as` cast.
    auto c = Conversions::TryCast();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsTryCast());
    EXPECT_TRUE(c->IsExplicit());
    EXPECT_FALSE(c->IsImplicit());
}

TEST(ConversionsTest, InterpolatedStringConversionWiring)
{
    // `Conversion.ImplicitInterpolatedStringConversion = new BuiltinConversion(true, 10)`.
    auto c = Conversions::ImplicitInterpolatedStringConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsInterpolatedStringConversion());
    EXPECT_TRUE(c->IsImplicit());
}

TEST(ConversionsTest, ThrowExpressionConversionWiring)
{
    // `Conversion.ThrowExpressionConversion = new BuiltinConversion(true, 11)`.
    auto c = Conversions::ThrowExpressionConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsThrowExpressionConversion());
    EXPECT_TRUE(c->IsImplicit());
}

TEST(ConversionsTest, InlineArrayConversionWiring)
{
    // `Conversion.InlineArrayConversion = new BuiltinConversion(true, 12)`.
    auto c = Conversions::InlineArrayConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsInlineArrayConversion());
    EXPECT_TRUE(c->IsImplicit());
}

TEST(ConversionsTest, ImplicitSpanConversionWiring)
{
    // `Conversion.ImplicitSpanConversion = new BuiltinConversion(true, 13)`.
    auto c = Conversions::ImplicitSpanConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsImplicitSpanConversion());
    EXPECT_TRUE(c->IsImplicit());
}

TEST(ConversionsTest, FactorySingletonsAreAllValidExceptNone)
{
    // Every singleton factory EXCEPT `None` is a valid conversion (the C# `BuiltinConversion`
    // keeps the base `IsValid` default of `true`; only `InvalidConversion` overrides it to
    // `false`). This is the invariant the `ConversionResolveResult.IsError => !IsValid` path
    // relies on: a builtin conversion is not an error, `None` is.
    EXPECT_FALSE(Conversions::None()->IsValid());
    EXPECT_TRUE(Conversions::IdentityConversion()->IsValid());
    EXPECT_TRUE(Conversions::NullLiteralConversion()->IsValid());
    EXPECT_TRUE(Conversions::ImplicitReferenceConversion()->IsValid());
    EXPECT_TRUE(Conversions::ExplicitReferenceConversion()->IsValid());
    EXPECT_TRUE(Conversions::BoxingConversion()->IsValid());
    EXPECT_TRUE(Conversions::UnboxingConversion()->IsValid());
    EXPECT_TRUE(Conversions::TryCast()->IsValid());
    EXPECT_TRUE(Conversions::ThrowExpressionConversion()->IsValid());
    EXPECT_TRUE(Conversions::ImplicitSpanConversion()->IsValid());
}

TEST(ConversionsTest, FactoryReturnsAreUsableAsSharedConversion)
{
    // The factory returns `std::shared_ptr<Conversion>` (the base type), so a consumer can
    // store the singleton directly in a `shared_ptr<Conversion>` member (e.g.
    // `ConversionResolveResult`'s `conversion_`) and dispatch through the base pointer.
    std::shared_ptr<Conversion> c = Conversions::BoxingConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsBoxingConversion());
    // Polymorphic dispatch through the base pointer reaches the `BuiltinConversion` override.
    EXPECT_TRUE(c->IsValid());
}

// ---------------------------------------------------------------------------
// NumericOrEnumerationConversion (backs the four numeric-conversion singleton fields
// and the `EnumerationConversion` factory method)
// ---------------------------------------------------------------------------

TEST(NumericOrEnumerationConversionTest, CtorStoresThreeBools)
{
    // The C# `NumericOrEnumerationConversion(bool isImplicit, bool isLifted, bool
    // isEnumeration = false)` ctor stores all three bools. The `isImplicit` flag drives
    // `IsImplicit` / `IsExplicit`; the `isLifted` flag drives `IsLifted`; the `isEnumeration`
    // flag drives `IsNumericConversion` / `IsEnumerationConversion`.
    NumericOrEnumerationConversion implicitLiftedEnum(true, true, true);
    EXPECT_TRUE(implicitLiftedEnum.IsImplicit());
    EXPECT_TRUE(implicitLiftedEnum.IsLifted());
    EXPECT_TRUE(implicitLiftedEnum.IsEnumerationConversion());
    EXPECT_FALSE(implicitLiftedEnum.IsNumericConversion());
}

TEST(NumericOrEnumerationConversionTest, IsExplicitIsNegationOfIsImplicit)
{
    // The C# `public override bool IsExplicit => !isImplicit`.
    NumericOrEnumerationConversion implicit_(true, false);
    NumericOrEnumerationConversion explicit_(false, false);
    EXPECT_TRUE(implicit_.IsImplicit());
    EXPECT_FALSE(implicit_.IsExplicit());
    EXPECT_FALSE(explicit_.IsImplicit());
    EXPECT_TRUE(explicit_.IsExplicit());
}

TEST(NumericOrEnumerationConversionTest, IsNumericConversionIsNegationOfIsEnumeration)
{
    // The C# `public override bool IsNumericConversion => !isEnumeration` and
    // `public override bool IsEnumerationConversion => isEnumeration` -- a conversion is
    // numeric OR enumeration, never both.
    NumericOrEnumerationConversion numeric(true, false, false);
    NumericOrEnumerationConversion enumeration(true, false, true);
    EXPECT_TRUE(numeric.IsNumericConversion());
    EXPECT_FALSE(numeric.IsEnumerationConversion());
    EXPECT_FALSE(enumeration.IsNumericConversion());
    EXPECT_TRUE(enumeration.IsEnumerationConversion());
}

TEST(NumericOrEnumerationConversionTest, DefaultedIsEnumerationArgIsFalse)
{
    // The C# `bool isEnumeration = false` defaulted arg lets the four numeric readonly
    // fields omit it. A two-arg construction defaults `isEnumeration` to false (a numeric
    // conversion, NOT an enumeration conversion).
    NumericOrEnumerationConversion twoArg(true, true);
    EXPECT_FALSE(twoArg.IsEnumerationConversion());
    EXPECT_TRUE(twoArg.IsNumericConversion());
}

TEST(NumericOrEnumerationConversionTest, ToStringFormats)
{
    // The C# `ToString` -- `(isImplicit ? "implicit" : "explicit") + (isLifted ? " lifted"
    // : "") + (isEnumeration ? " enumeration" : " numeric") + " conversion"`. Pin a
    // representative spread: the bare implicit numeric, the explicit lifted numeric, and
    // the implicit enumeration.
    EXPECT_EQ(NumericOrEnumerationConversion(true, false, false).ToString(),
              "implicit numeric conversion");
    EXPECT_EQ(NumericOrEnumerationConversion(false, true, false).ToString(),
              "explicit lifted numeric conversion");
    EXPECT_EQ(NumericOrEnumerationConversion(true, false, true).ToString(),
              "implicit enumeration conversion");
    EXPECT_EQ(NumericOrEnumerationConversion(false, true, true).ToString(),
              "explicit lifted enumeration conversion");
}

TEST(NumericOrEnumerationConversionTest, ValueBasedEqualsTwoDistinctInstancesWithSameBoolsAreEqual)
{
    // The load-bearing crux distinguishing a value-based conversion from the singleton
    // reference-equality the `InvalidConversion` / `BuiltinConversion` factories inherit:
    // two DISTINCT instances with the same three bools are EQUAL by value (the
    // `dynamic_cast` + bool comparison), NOT reference-equal. The `static` factory METHOD
    // `EnumerationConversion` builds a NEW instance per call, so two calls with equal args
    // are distinct instances that must compare equal by value -- this `Equals` is what
    // makes that work.
    NumericOrEnumerationConversion a(true, true, false);
    NumericOrEnumerationConversion b(true, true, false);
    EXPECT_NE(&a, &b);       // distinct instances
    EXPECT_TRUE(a.Equals(b));  // but value-equal
    EXPECT_TRUE(b.Equals(a));  // symmetric
}

TEST(NumericOrEnumerationConversionTest, EqualsReturnsFalseForDifferentSubtype)
{
    // The C# `other as NumericOrEnumerationConversion` yields null for a different subtype,
    // so `Equals` returns false. A `BuiltinConversion` is NOT a
    // `NumericOrEnumerationConversion`.
    NumericOrEnumerationConversion a(true, false, false);
    BuiltinConversion b(true, 0);
    EXPECT_FALSE(a.Equals(b));
}

TEST(NumericOrEnumerationConversionTest, EqualsReturnsFalseForDifferentBoolCombination)
{
    // The C# `Equals` compares all three bools; a difference in any one makes the
    // conversions unequal.
    NumericOrEnumerationConversion a(true, false, false);
    NumericOrEnumerationConversion b(false, false, false);  // isImplicit differs
    NumericOrEnumerationConversion c(true, true, false);    // isLifted differs
    NumericOrEnumerationConversion d(true, false, true);    // isEnumeration differs
    EXPECT_FALSE(a.Equals(b));
    EXPECT_FALSE(a.Equals(c));
    EXPECT_FALSE(a.Equals(d));
}

TEST(NumericOrEnumerationConversionTest, GetHashCodeMatchesBitFormula)
{
    // The C# `GetHashCode => (isImplicit ? 1 : 0) + (isLifted ? 2 : 0) + (isEnumeration ?
    // 4 : 0)` -- the three bools map to disjoint bits 1/2/4. Pin the representative spread.
    EXPECT_EQ(NumericOrEnumerationConversion(false, false, false).GetHashCode(), 0);
    EXPECT_EQ(NumericOrEnumerationConversion(true, false, false).GetHashCode(), 1);
    EXPECT_EQ(NumericOrEnumerationConversion(false, true, false).GetHashCode(), 2);
    EXPECT_EQ(NumericOrEnumerationConversion(true, true, false).GetHashCode(), 3);
    EXPECT_EQ(NumericOrEnumerationConversion(false, false, true).GetHashCode(), 4);
    EXPECT_EQ(NumericOrEnumerationConversion(true, false, true).GetHashCode(), 5);
    EXPECT_EQ(NumericOrEnumerationConversion(false, true, true).GetHashCode(), 6);
    EXPECT_EQ(NumericOrEnumerationConversion(true, true, true).GetHashCode(), 7);
}

TEST(NumericOrEnumerationConversionTest, GetHashCodeIsConsistentWithEquals)
{
    // Equal conversions have equal hashes (the hash-consistent-with-equality invariant the
    // `ConversionResolveResult` / interning consumers rely on).
    NumericOrEnumerationConversion a(true, true, false);
    NumericOrEnumerationConversion b(true, true, false);
    EXPECT_TRUE(a.Equals(b));
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(NumericOrEnumerationConversionTest, IsFinalAndDerivesFromConversion)
{
    static_assert(std::is_final_v<NumericOrEnumerationConversion>,
                  "NumericOrEnumerationConversion is final (mirrors the C# `sealed`).");
    static_assert(std::is_base_of_v<Conversion, NumericOrEnumerationConversion>,
                  "NumericOrEnumerationConversion derives from Conversion.");
    static_assert(std::has_virtual_destructor_v<NumericOrEnumerationConversion>,
                  "NumericOrEnumerationConversion has a virtual destructor.");
}

// ---------------------------------------------------------------------------
// TupleConv (backs the `TupleConversion` factory method)
// ---------------------------------------------------------------------------

namespace {
// A pair of singletons usable as `TupleConv` element conversions: the identity conversion
// (a `BuiltinConversion`, `IsImplicit` true) and the explicit reference conversion
// (`IsImplicit` false). Reused across the `TupleConv` tests.
std::shared_ptr<Conversion> ImplicitElement() { return Conversions::IdentityConversion(); }
std::shared_ptr<Conversion> ExplicitElement() { return Conversions::ExplicitReferenceConversion(); }
} // namespace

TEST(TupleConvTest, CtorStoresElementConversions)
{
    // The C# `TupleConv(ImmutableArray<Conversion> elementConversions)` stores the array.
    // The port stores a `std::vector<std::shared_ptr<Conversion>>` snapshot.
    std::vector<std::shared_ptr<Conversion>> elems = {ImplicitElement(), ExplicitElement()};
    TupleConv t(elems);
    ASSERT_EQ(t.ElementConversions().size(), 2u);
    EXPECT_EQ(t.ElementConversions()[0].get(), elems[0].get());
    EXPECT_EQ(t.ElementConversions()[1].get(), elems[1].get());
}

TEST(TupleConvTest, IsImplicitTrueWhenAllElementsImplicit)
{
    // The C# `IsImplicit = elementConversions.All(c => c.IsImplicit)` -- true when every
    // element is implicit.
    std::vector<std::shared_ptr<Conversion>> elems = {ImplicitElement(), ImplicitElement()};
    TupleConv t(elems);
    EXPECT_TRUE(t.IsImplicit());
    EXPECT_FALSE(t.IsExplicit());
}

TEST(TupleConvTest, IsImplicitTrueForEmptyArrayVacuously)
{
    // The C# `All` over an empty sequence is true (vacuously), so an empty `TupleConv` is
    // implicit.
    std::vector<std::shared_ptr<Conversion>> elems;
    TupleConv t(elems);
    EXPECT_TRUE(t.IsImplicit());
    EXPECT_FALSE(t.IsExplicit());
}

TEST(TupleConvTest, IsImplicitFalseWhenAnyElementExplicit)
{
    // The C# `All` returns false as soon as any element is NOT implicit.
    std::vector<std::shared_ptr<Conversion>> elems = {ImplicitElement(), ExplicitElement()};
    TupleConv t(elems);
    EXPECT_FALSE(t.IsImplicit());
    EXPECT_TRUE(t.IsExplicit());
}

TEST(TupleConvTest, IsTupleConversionTrue)
{
    // The C# `public override bool IsTupleConversion => true`.
    std::vector<std::shared_ptr<Conversion>> elems;
    TupleConv t(elems);
    EXPECT_TRUE(t.IsTupleConversion());
}

TEST(TupleConvTest, ElementConversionsReturnsSnapshot)
{
    // The `ElementConversions()` accessor returns a copy of the stored vector (a snapshot
    // of shared handles); mutating the returned copy does not affect the `TupleConv`.
    std::vector<std::shared_ptr<Conversion>> elems = {ImplicitElement()};
    TupleConv t(elems);
    auto snap = t.ElementConversions();
    ASSERT_EQ(snap.size(), 1u);
    snap.clear();
    EXPECT_EQ(t.ElementConversions().size(), 1u);  // the stored vector is unaffected
}

TEST(TupleConvTest, ToStringImplicitDoubleSpaceQuirk)
{
    // The C# `ToString => (IsImplicit ? "implicit " : "explicit ") + " tuple conversion"` --
    // the source carries a trailing space in "implicit " AND a leading space in " tuple
    // conversion", so the result has TWO spaces between the kind and "tuple" (a C# source
    // quirk ported verbatim).
    std::vector<std::shared_ptr<Conversion>> elems;  // empty -> implicit
    TupleConv t(elems);
    EXPECT_EQ(t.ToString(), "implicit  tuple conversion");
}

TEST(TupleConvTest, ToStringExplicitDoubleSpaceQuirk)
{
    std::vector<std::shared_ptr<Conversion>> elems = {ExplicitElement()};  // explicit
    TupleConv t(elems);
    EXPECT_EQ(t.ToString(), "explicit  tuple conversion");
}

TEST(TupleConvTest, ValueBasedEqualsTwoDistinctInstancesWithSequenceEqualElements)
{
    // The load-bearing crux: two DISTINCT `TupleConv`s with `SequenceEqual` element
    // conversions are EQUAL by value (the element-wise `Equals` loop), NOT reference-equal.
    std::vector<std::shared_ptr<Conversion>> aElems = {ImplicitElement(), ImplicitElement()};
    std::vector<std::shared_ptr<Conversion>> bElems = {ImplicitElement(), ImplicitElement()};
    TupleConv a(aElems);
    TupleConv b(bElems);
    EXPECT_NE(&a, &b);       // distinct instances
    EXPECT_TRUE(a.Equals(b));  // but value-equal (SequenceEqual elements)
    EXPECT_TRUE(b.Equals(a));  // symmetric
}

TEST(TupleConvTest, EqualsReturnsFalseForDifferentSubtype)
{
    // The C# `other is TupleConv o` -- a non-`TupleConv` conversion is not equal.
    std::vector<std::shared_ptr<Conversion>> elems;
    TupleConv a(elems);
    BuiltinConversion b(true, 0);
    EXPECT_FALSE(a.Equals(b));
}

TEST(TupleConvTest, EqualsReturnsFalseForSizeMismatch)
{
    // The C# `SequenceEqual` returns false when the counts differ; the port short-circuits
    // on a size mismatch before the element loop.
    std::vector<std::shared_ptr<Conversion>> aElems = {ImplicitElement()};
    std::vector<std::shared_ptr<Conversion>> bElems = {ImplicitElement(), ImplicitElement()};
    TupleConv a(aElems);
    TupleConv b(bElems);
    EXPECT_FALSE(a.Equals(b));
}

TEST(TupleConvTest, EqualsReturnsFalseForElementMismatch)
{
    // The C# `SequenceEqual` compares element-wise; a single differing element (here an
    // implicit vs an explicit element) makes the tuples unequal.
    std::vector<std::shared_ptr<Conversion>> aElems = {ImplicitElement(), ImplicitElement()};
    std::vector<std::shared_ptr<Conversion>> bElems = {ImplicitElement(), ExplicitElement()};
    TupleConv a(aElems);
    TupleConv b(bElems);
    EXPECT_FALSE(a.Equals(b));
}

TEST(TupleConvTest, GetHashCodeEmptyIsZero)
{
    // The C# `GetHashCode` starts `hash = 0` and the empty-array loop does not run, so an
    // empty `TupleConv` yields hash 0.
    std::vector<std::shared_ptr<Conversion>> elems;
    TupleConv t(elems);
    EXPECT_EQ(t.GetHashCode(), 0);
}

TEST(TupleConvTest, GetHashCodeIsConsistentWithEquals)
{
    // Equal tuples have equal hashes (the fold of each element's `GetHashCode`).
    std::vector<std::shared_ptr<Conversion>> aElems = {ImplicitElement(), ImplicitElement()};
    std::vector<std::shared_ptr<Conversion>> bElems = {ImplicitElement(), ImplicitElement()};
    TupleConv a(aElems);
    TupleConv b(bElems);
    EXPECT_TRUE(a.Equals(b));
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(TupleConvTest, IsFinalAndDerivesFromConversion)
{
    static_assert(std::is_final_v<TupleConv>, "TupleConv is final (mirrors the C# `sealed`).");
    static_assert(std::is_base_of_v<Conversion, TupleConv>,
                  "TupleConv derives from Conversion.");
    static_assert(std::has_virtual_destructor_v<TupleConv>,
                  "TupleConv has a virtual destructor.");
}

// ---------------------------------------------------------------------------
// Conversions factory accessors (the four numeric-conversion singleton FIELDS and the
// `EnumerationConversion` / `TupleConversion` factory METHODS)
// ---------------------------------------------------------------------------

TEST(ConversionsTest, ImplicitNumericConversionWiring)
{
    // `Conversion.ImplicitNumericConversion = new NumericOrEnumerationConversion(true,
    // false)` -- the implicit non-lifted numeric-conversion singleton.
    auto c = Conversions::ImplicitNumericConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsValid());
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_FALSE(c->IsExplicit());
    EXPECT_TRUE(c->IsNumericConversion());
    EXPECT_FALSE(c->IsEnumerationConversion());
    EXPECT_FALSE(c->IsLifted());
    EXPECT_NE(dynamic_cast<NumericOrEnumerationConversion*>(c.get()), nullptr);
}

TEST(ConversionsTest, ExplicitNumericConversionWiring)
{
    // `Conversion.ExplicitNumericConversion = new NumericOrEnumerationConversion(false,
    // false)`.
    auto c = Conversions::ExplicitNumericConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsExplicit());
    EXPECT_TRUE(c->IsNumericConversion());
    EXPECT_FALSE(c->IsLifted());
    EXPECT_FALSE(c->IsEnumerationConversion());
}

TEST(ConversionsTest, ImplicitLiftedNumericConversionWiring)
{
    // `Conversion.ImplicitLiftedNumericConversion = new NumericOrEnumerationConversion(true,
    // true)`.
    auto c = Conversions::ImplicitLiftedNumericConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_TRUE(c->IsLifted());
    EXPECT_TRUE(c->IsNumericConversion());
}

TEST(ConversionsTest, ExplicitLiftedNumericConversionWiring)
{
    // `Conversion.ExplicitLiftedNumericConversion = new NumericOrEnumerationConversion(false,
    // true)`.
    auto c = Conversions::ExplicitLiftedNumericConversion();
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsExplicit());
    EXPECT_TRUE(c->IsLifted());
    EXPECT_TRUE(c->IsNumericConversion());
}

TEST(ConversionsTest, NumericSingletonIdentityIsStableAcrossCalls)
{
    // The four numeric-conversion FIELDS are singletons (one instance shared by every
    // consumer, reference-equal across uses) -- the C# `static readonly` field semantics.
    EXPECT_EQ(Conversions::ImplicitNumericConversion().get(),
              Conversions::ImplicitNumericConversion().get());
    EXPECT_EQ(Conversions::ExplicitNumericConversion().get(),
              Conversions::ExplicitNumericConversion().get());
    EXPECT_EQ(Conversions::ImplicitLiftedNumericConversion().get(),
              Conversions::ImplicitLiftedNumericConversion().get());
    EXPECT_EQ(Conversions::ExplicitLiftedNumericConversion().get(),
              Conversions::ExplicitLiftedNumericConversion().get());
}

TEST(ConversionsTest, NumericSingletonsAreDistinct)
{
    // Each numeric-conversion FIELD is a distinct singleton instance.
    EXPECT_NE(Conversions::ImplicitNumericConversion().get(),
              Conversions::ExplicitNumericConversion().get());
    EXPECT_NE(Conversions::ImplicitNumericConversion().get(),
              Conversions::ImplicitLiftedNumericConversion().get());
    EXPECT_NE(Conversions::ImplicitLiftedNumericConversion().get(),
              Conversions::ExplicitLiftedNumericConversion().get());
}

TEST(ConversionsTest, NumericSingletonsAreAllValid)
{
    // The `NumericOrEnumerationConversion` keeps the base `IsValid` default of `true` (it
    // does NOT override `IsValid`), so all four numeric-conversion singletons are valid.
    EXPECT_TRUE(Conversions::ImplicitNumericConversion()->IsValid());
    EXPECT_TRUE(Conversions::ExplicitNumericConversion()->IsValid());
    EXPECT_TRUE(Conversions::ImplicitLiftedNumericConversion()->IsValid());
    EXPECT_TRUE(Conversions::ExplicitLiftedNumericConversion()->IsValid());
}

TEST(ConversionsTest, EnumerationConversionIsFactoryMethodNotSingleton)
{
    // The load-bearing distinction between a singleton FIELD and a factory METHOD: two
    // calls to `EnumerationConversion` with equal args return DISTINCT instances (NOT
    // reference-equal, unlike the singleton fields), but they compare EQUAL by value (the
    // `NumericOrEnumerationConversion` value-based `Equals`). The factory METHOD builds a
    // NEW instance per call; the value-based `Equals` is what makes two such instances
    // compare equal.
    auto a = Conversions::EnumerationConversion(true, false);
    auto b = Conversions::EnumerationConversion(true, false);
    EXPECT_NE(a.get(), b.get());   // distinct instances (factory method, not singleton)
    EXPECT_TRUE(a->Equals(*b));    // but value-equal
    EXPECT_NE(dynamic_cast<NumericOrEnumerationConversion*>(a.get()), nullptr);
}

TEST(ConversionsTest, EnumerationConversionWiring)
{
    // `Conversion.EnumerationConversion(isImplicit, isLifted) => new
    // NumericOrEnumerationConversion(isImplicit, isLifted, true)` -- an enumeration
    // conversion (`IsEnumerationConversion` true, `IsNumericConversion` false).
    auto c = Conversions::EnumerationConversion(true, false);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsEnumerationConversion());
    EXPECT_FALSE(c->IsNumericConversion());
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_FALSE(c->IsLifted());

    auto lifted = Conversions::EnumerationConversion(false, true);
    EXPECT_TRUE(lifted->IsEnumerationConversion());
    EXPECT_TRUE(lifted->IsLifted());
    EXPECT_TRUE(lifted->IsExplicit());
}

TEST(ConversionsTest, TupleConversionIsFactoryMethodNotSingleton)
{
    // `TupleConversion` is a factory METHOD (a NEW instance per call), NOT a singleton: two
    // calls with equal conversions return DISTINCT instances that compare EQUAL by value.
    std::vector<std::shared_ptr<Conversion>> aElems = {ImplicitElement()};
    std::vector<std::shared_ptr<Conversion>> bElems = {ImplicitElement()};
    auto a = Conversions::TupleConversion(aElems);
    auto b = Conversions::TupleConversion(bElems);
    EXPECT_NE(a.get(), b.get());   // distinct instances
    EXPECT_TRUE(a->Equals(*b));     // but value-equal
    EXPECT_NE(dynamic_cast<TupleConv*>(a.get()), nullptr);
}

TEST(ConversionsTest, TupleConversionWiring)
{
    // `Conversion.TupleConversion(conversions) => new TupleConv(conversions)` -- a tuple
    // conversion (`IsTupleConversion` true, `IsImplicit` derived from the elements).
    std::vector<std::shared_ptr<Conversion>> elems = {ImplicitElement(), ImplicitElement()};
    auto c = Conversions::TupleConversion(elems);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsTupleConversion());
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_EQ(c->ElementConversions().size(), 2u);
}

TEST(ConversionsTest, TupleConversionEmptyIsImplicit)
{
    // An empty `TupleConversion` is implicit (the `All` over an empty sequence is true).
    std::vector<std::shared_ptr<Conversion>> elems;
    auto c = Conversions::TupleConversion(elems);
    ASSERT_NE(c, nullptr);
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_TRUE(c->ElementConversions().empty());
}
