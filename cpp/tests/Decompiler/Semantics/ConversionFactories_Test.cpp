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
