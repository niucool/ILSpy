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
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Conversion` (cpp/Decompiler/Semantics/Conversion.hpp, the D451 port of
// ICSharpCode.Decompiler/Semantics/Conversion.cs -- the abstract base only). The 666-line
// C# `Conversion.cs` carries the abstract base plus six nested sealed subclasses and a
// set of `static readonly` factory fields; this port covers ONLY the abstract base (the
// virtual surface the `ConversionResolveResult.IsError` crux reaches), deferring the
// concrete subclasses and the static factories. The tests pin the full default surface
// (`IsValid` -> `true`, the ~29 boolean conversion-kind discriminators -> `false`, the
// `Method` / `ConversionBefore/AfterUserDefinedOperator` nullable-pointer defaults, the
// `ElementConversions` empty default), the `IsValid` override-dispatch crux (the
// `ConversionResolveResult.IsError => !IsValid` path), the `Equals` reference-equality
// crux (`this == &other`), the `GetHashCode` identity-hash crux, the polymorphic dispatch
// through a `Conversion*` base pointer, and the abstract/polymorphic class shape.

#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace {

// A concrete `Conversion` that overrides NOTHING -- it exists only to make the abstract
// base instantiable so the inherited base defaults can be pinned. The compiler-generated
// destructor calls the inline-defined pure-virtual base destructor, so this empty class is
// concrete. It models the "a conversion with no specialization" shape the base defaults
// describe (a valid, non-implicit, non-explicit, ... conversion -- the closest the
// surface comes to a generic valid conversion).
class TestDefaultConversion : public ILSpy::Decompiler::Semantics::Conversion {};

// A concrete `Conversion` overriding `IsValid` to `true` -- pins the override-dispatch
// path the `ConversionResolveResult.IsError` crux reaches (a valid conversion ->
// `IsError` false).
class TestValidConversion : public ILSpy::Decompiler::Semantics::Conversion {
public:
    bool IsValid() const override { return true; }
};

// A concrete `Conversion` overriding `IsValid` to `false` -- mirrors the deferred
// `InvalidConversion` (`None` singleton) shape: a not-valid conversion ->
// `ConversionResolveResult.IsError` true.
class TestInvalidConversion : public ILSpy::Decompiler::Semantics::Conversion {
public:
    bool IsValid() const override { return false; }
};

} // namespace

TEST(ConversionTest, IsValidDefaultsToTrue)
{
    // The C# `public virtual bool IsValid => true` -- the load-bearing default
    // `ConversionResolveResult.IsError => !IsValid` reaches. A `TestDefaultConversion`
    // (overrides nothing) inherits the base default, so `IsValid()` is `true`.
    TestDefaultConversion c;
    EXPECT_TRUE(c.IsValid());
}

TEST(ConversionTest, BooleanDiscriminatorsDefaultToFalse)
{
    // The ~29 C# `virtual bool Xxx` conversion-kind discriminators all default `false`
    // (distinct from `IsValid` which defaults `true`). A `TestDefaultConversion`
    // (overrides nothing) inherits every default.
    TestDefaultConversion c;
    EXPECT_FALSE(c.IsImplicit());
    EXPECT_FALSE(c.IsExplicit());
    EXPECT_FALSE(c.IsTryCast());
    EXPECT_FALSE(c.IsThrowExpressionConversion());
    EXPECT_FALSE(c.IsIdentityConversion());
    EXPECT_FALSE(c.IsNullLiteralConversion());
    EXPECT_FALSE(c.IsConstantExpressionConversion());
    EXPECT_FALSE(c.IsNumericConversion());
    EXPECT_FALSE(c.IsLifted());
    EXPECT_FALSE(c.IsDynamicConversion());
    EXPECT_FALSE(c.IsReferenceConversion());
    EXPECT_FALSE(c.IsEnumerationConversion());
    EXPECT_FALSE(c.IsNullableConversion());
    EXPECT_FALSE(c.IsUserDefined());
    EXPECT_FALSE(c.IsBoxingConversion());
    EXPECT_FALSE(c.IsUnboxingConversion());
    EXPECT_FALSE(c.IsPointerConversion());
    EXPECT_FALSE(c.IsMethodGroupConversion());
    EXPECT_FALSE(c.IsVirtualMethodLookup());
    EXPECT_FALSE(c.DelegateCapturesFirstArgument());
    EXPECT_FALSE(c.IsAnonymousFunctionConversion());
    EXPECT_FALSE(c.IsTupleConversion());
    EXPECT_FALSE(c.IsInterpolatedStringConversion());
    EXPECT_FALSE(c.IsInlineArrayConversion());
    EXPECT_FALSE(c.IsImplicitSpanConversion());
}

TEST(ConversionTest, MethodDefaultsToNull)
{
    // The C# `virtual IMethod Method => null` -- the nullable reference default `null`
    // ports to a `nullptr` raw-pointer return.
    TestDefaultConversion c;
    EXPECT_EQ(c.Method(), nullptr);
}

TEST(ConversionTest, ConversionBeforeAndAfterUserDefinedOperatorDefaultToNull)
{
    // The C# `virtual Conversion ConversionBefore/AfterUserDefinedOperator => null` --
    // the self-referential nullable reference defaults `null` port to empty
    // `shared_ptr<Conversion>` returns.
    TestDefaultConversion c;
    EXPECT_EQ(c.ConversionBeforeUserDefinedOperator(), nullptr);
    EXPECT_EQ(c.ConversionAfterUserDefinedOperator(), nullptr);
}

TEST(ConversionTest, ElementConversionsDefaultsToEmpty)
{
    // The C# `virtual ImmutableArray<Conversion> ElementConversions => default` -- the
    // default (uninitialized) immutable array is empty, ported to an empty
    // `std::vector<std::shared_ptr<Conversion>>`.
    TestDefaultConversion c;
    EXPECT_TRUE(c.ElementConversions().empty());
}

TEST(ConversionTest, IsValidOverrideIsDispatched)
{
    // The `IsValid` override dispatches through the concrete subclass: a
    // `TestValidConversion` (override `true`) reports `IsValid` true, a
    // `TestInvalidConversion` (override `false`) reports `IsValid` false. This is the
    // crux `ConversionResolveResult.IsError => !IsValid` reaches -- the override wins
    // over the base default.
    TestValidConversion valid;
    TestInvalidConversion invalid;
    EXPECT_TRUE(valid.IsValid());
    EXPECT_FALSE(invalid.IsValid());
}

TEST(ConversionTest, IsValidDispatchesThroughBasePointer)
{
    // The `IsValid` virtual dispatches through a `Conversion*` base pointer (the virtual
    // dispatch the `ConversionResolveResult.IsError` crux relies on): the base pointer's
    // `IsValid` reports the subclass override, not the base default.
    TestValidConversion valid;
    TestInvalidConversion invalid;
    ILSpy::Decompiler::Semantics::Conversion* validBase = &valid;
    ILSpy::Decompiler::Semantics::Conversion* invalidBase = &invalid;
    EXPECT_TRUE(validBase->IsValid());
    EXPECT_FALSE(invalidBase->IsValid());
}

TEST(ConversionTest, EqualsIsReferenceEquality)
{
    // The C# `virtual bool Equals(Conversion other) => this == other` -- reference
    // equality for the reference-type `Conversion`. The port compares pointer identity
    // (`this == &other`): a conversion equals itself, and two distinct conversions are
    // not equal (even when they are the same subclass with the same `IsValid`).
    TestInvalidConversion a;
    TestInvalidConversion b;
    EXPECT_TRUE(a.Equals(a));
    EXPECT_FALSE(a.Equals(b));
    EXPECT_FALSE(b.Equals(a));
}

TEST(ConversionTest, EqualsDispatchesThroughBasePointer)
{
    // The `Equals` virtual dispatches through a `Conversion*` base pointer: the base
    // pointer's `Equals` reports the reference-equality comparison (`this == &other`),
    // not a base-specific behavior.
    TestValidConversion a;
    TestValidConversion b;
    ILSpy::Decompiler::Semantics::Conversion* base = &a;
    EXPECT_TRUE(base->Equals(a));
    EXPECT_FALSE(base->Equals(b));
}

TEST(ConversionTest, GetHashCodeIsIdentityHashAndStable)
{
    // The C# `override int GetHashCode() => base.GetHashCode()` -- the identity hash
    // (`object.GetHashCode` / `RuntimeHelpers.GetHashCode` yields). The port returns the
    // pointer-identity hash, which is stable across calls on the same instance and (for
    // distinct instances) generally distinct.
    TestDefaultConversion c;
    const int h1 = c.GetHashCode();
    const int h2 = c.GetHashCode();
    EXPECT_EQ(h1, h2);
}

TEST(ConversionTest, GetHashCodeDispatchesThroughBasePointer)
{
    // The `GetHashCode` virtual dispatches through a `Conversion*` base pointer: the
    // base pointer's `GetHashCode` reports the same identity hash as the direct call.
    TestDefaultConversion c;
    ILSpy::Decompiler::Semantics::Conversion* base = &c;
    EXPECT_EQ(base->GetHashCode(), c.GetHashCode());
}

TEST(ConversionTest, ConversionBeforeUserDefinedOperatorHoldsSharedHandle)
{
    // A subclass overriding `ConversionBeforeUserDefinedOperator` to return a non-null
    // `shared_ptr<Conversion>` exercises the self-referential shared-handle return: the
    // returned handle is a real `shared_ptr<Conversion>` (the deleter type-erases where
    // `Conversion` is complete). This pins that the self-referential `shared_ptr` return
    // type is usable from a subclass override.
    class TestUserDefinedConversion : public ILSpy::Decompiler::Semantics::Conversion {
    public:
        std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
        ConversionBeforeUserDefinedOperator() const override {
            return std::make_shared<TestDefaultConversion>();
        }
        bool IsUserDefined() const override { return true; }
    };
    TestUserDefinedConversion c;
    auto before = c.ConversionBeforeUserDefinedOperator();
    ASSERT_NE(before, nullptr);
    EXPECT_TRUE(before->IsValid());
    EXPECT_TRUE(c.IsUserDefined());
}

TEST(ConversionTest, ElementConversionsHoldsSharedHandles)
{
    // A subclass overriding `ElementConversions` to return a non-empty vector of
    // `shared_ptr<Conversion>` exercises the self-referential vector return: the returned
    // vector holds real shared handles.
    class TestTupleConversion : public ILSpy::Decompiler::Semantics::Conversion {
    public:
        std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>>
        ElementConversions() const override {
            return {std::make_shared<TestValidConversion>(),
                    std::make_shared<TestInvalidConversion>()};
        }
        bool IsTupleConversion() const override { return true; }
    };
    TestTupleConversion c;
    auto elems = c.ElementConversions();
    ASSERT_EQ(elems.size(), 2u);
    EXPECT_TRUE(elems[0]->IsValid());
    EXPECT_FALSE(elems[1]->IsValid());
    EXPECT_TRUE(c.IsTupleConversion());
}

TEST(ConversionTest, IsAbstractPolymorphicAndHasVirtualDestructor)
{
    // The C# `public abstract class Conversion` cannot be directly instantiated; the
    // pure-virtual destructor makes the C++ port abstract too (`std::is_abstract_v`).
    // The base is polymorphic (the virtual surface) and has a virtual destructor (held via
    // base pointers / `shared_ptr<Conversion>`).
    static_assert(std::is_abstract_v<ILSpy::Decompiler::Semantics::Conversion>,
                  "Conversion is abstract (mirrors the C# `abstract class`).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::Conversion>,
                  "Conversion is polymorphic (the virtual conversion-kind surface).");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::Conversion>,
                  "Conversion has a virtual destructor (held via base pointers).");
    // A directly-instantiated `Conversion` is NOT constructible (the base is abstract).
    static_assert(!std::is_default_constructible_v<ILSpy::Decompiler::Semantics::Conversion>,
                  "Conversion cannot be directly instantiated (the base is abstract).");
}

TEST(ConversionTest, ConcreteSubclassesAreAssignableToBase)
{
    // The concrete test subclasses derive from `Conversion` (IS-A) so a subclass instance
    // binds to a `Conversion*` base pointer / a `shared_ptr<Conversion>` via the derived-
    // to-base conversion.
    static_assert(
        std::is_base_of_v<ILSpy::Decompiler::Semantics::Conversion, TestDefaultConversion>,
        "TestDefaultConversion derives from Conversion.");
    static_assert(
        std::is_base_of_v<ILSpy::Decompiler::Semantics::Conversion, TestValidConversion>,
        "TestValidConversion derives from Conversion.");
    static_assert(
        std::is_base_of_v<ILSpy::Decompiler::Semantics::Conversion, TestInvalidConversion>,
        "TestInvalidConversion derives from Conversion.");
    // The subclasses are concrete (overriding the pure-virtual destructor implicitly via
    // the compiler-generated destructor that calls the inline base destructor).
    static_assert(std::is_default_constructible_v<TestDefaultConversion>,
                  "TestDefaultConversion is concrete (overrides nothing; the base "
                  "pure-virtual destructor is defined inline).");
    static_assert(std::is_default_constructible_v<TestValidConversion>,
                  "TestValidConversion is concrete.");
    static_assert(std::is_default_constructible_v<TestInvalidConversion>,
                  "TestInvalidConversion is concrete.");
}
