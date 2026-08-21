// Copyright (c) 2026 ILSpy contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
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

// Port of the concrete `Conversion` subclasses and the static factory accessors from
// ICSharpCode.Decompiler/Semantics/Conversion.cs -- the follow-up to the D451 abstract
// `Conversion` base port. The 666-line C# `Conversion.cs` carries the abstract base plus
// six nested sealed subclasses (`InvalidConversion` / `NumericOrEnumerationConversion` /
// `BuiltinConversion` / `UserDefinedConv` / `MethodGroupConv` / `TupleConv`) and a set of
// `static readonly` factory fields plus four `static` factory methods. This header ports
// the two subclasses backing the singleton factory FIELDS that take no extra state beyond
// a `bool isImplicit` and a `byte type` discriminator -- `InvalidConversion` (backs `None`)
// and `BuiltinConversion` (backs the ~18 builtin-conversion singleton fields:
// `IdentityConversion` / `NullLiteralConversion` / the reference / dynamic / nullable /
// pointer / boxing / unboxing / try-cast / interpolated-string / throw-expression /
// inline-array / span conversions) -- plus a `Conversions` factory holder exposing them
// as `std::shared_ptr<Conversion>` singletons. The remaining four subclasses
// (`NumericOrEnumerationConversion` / `UserDefinedConv` / `MethodGroupConv` / `TupleConv`)
// and their factory methods (the `static` methods that build a NEW instance per call, plus
// the four numeric-conversion readonly fields) are deferred to a follow-up iteration --
// they carry extra state (the method handle, the before/after conversions, the element
// conversions) and value-based `Equals` / `GetHashCode` overrides absent from the two
// subclasses ported here.
//
// KEY PORT CONVENTIONS:
//  * The C# nested `sealed class XxxConversion : Conversion` subclasses are PRIVATE to the
//    `Conversion` class (no access modifier = private for nested types); only the `static`
//    factories construct them. The C++ port lifts them to TOP-LEVEL `final` classes with
//    PUBLIC constructors (no assembly boundary in C++; the `Conversions` factory holder is
//    the public API, the subclasses are the implementation the factories build) -- the
//    D419 `DefaultAssemblyReference` nested-`CurrentModuleReference` precedent applied to
//    the `Conversion` subclass family.
//  * The C# `static readonly Conversion Foo = new XxxConversion(...)` singleton FIELDS (one
//    GC-owned instance shared by every consumer; reference-equal across uses) port to
//    `std::shared_ptr<Conversion> Conversions::Foo()` accessors returning a function-local
//    `static const auto singleton = std::make_shared<...>` (the Meyers-singleton
//    `shared_ptr`, the D398 `StringComparer::Ordinal` / D419 `CurrentAssembly` precedent).
//    Each call returns a COPY of the `shared_ptr` (sharing ownership of the one static
//    instance), so `Conversions::Foo().get() == Conversions::Foo().get()` is `true`
//    (reference-equality preserved, faithful to the C# singleton-field semantics). The
//    `shared_ptr<Conversion>` return lets a consumer store the singleton directly in a
//    `shared_ptr<Conversion>` member (e.g. `ConversionResolveResult`'s `conversion_`).
//  * The C# `Conversion` base does NOT declare `ToString` (it inherits `object.ToString`;
//    each nested subclass overrides it). The C++ base omits `ToString` too (the D451
//    convention), so each concrete subclass adds its own NON-VIRTUAL `std::string
//    ToString() const` (the D432/D442 per-subclass-`ToString` convention). Because the
//    subclasses are `final`, there is no further-override concern.
//  * Neither `InvalidConversion` nor `BuiltinConversion` overrides `Equals` /
//    `GetHashCode` -- they inherit the base reference-equality (`this == &other`) and
//    identity-hash. Since the factories return shared SINGLETONS, two calls to the same
//    factory return the same instance, so the inherited reference-equality is the faithful
//    match for the C# singleton-field reference-equality. (The value-based `Equals` /
//    `GetHashCode` overrides come with the deferred subclasses that carry distinguishing
//    state.)

#ifndef ILSPY_DECOMPILER_SEMANTICS_CONVERSION_FACTORIES_HPP
#define ILSPY_DECOMPILER_SEMANTICS_CONVERSION_FACTORIES_HPP

#include "Decompiler/Semantics/Conversion.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::Semantics {

// The C# `sealed class InvalidConversion : Conversion` -- the not-a-valid-conversion
// singleton backing `Conversion.None`. It overrides `IsValid` to `false` (the load-bearing
// crux `ConversionResolveResult.IsError => !IsValid` reaches for the `None` case) and
// `ToString` to `"None"`. It inherits the base reference-equality `Equals` / identity
// `GetHashCode` (the `None` singleton is reference-equal to itself).
class InvalidConversion final : public Conversion {
public:
    bool IsValid() const override { return false; }

    // The C# `override string ToString() => "None"`. Non-virtual (the base omits
    // `ToString`); the `final` class needs no further-override concern.
    std::string ToString() const { return "None"; }
};

// The C# `sealed class BuiltinConversion : Conversion` -- the builtin conversion backing
// the ~18 singleton factory fields that take no extra state beyond an `isImplicit` flag
// and a `type` discriminator byte. The `type` byte drives ~15 boolean conversion-kind
// overrides (each `type == N` for its kind) and the `ToString` switch; the `isImplicit`
// flag drives `IsImplicit` / `IsExplicit` and the `ToString` "implicit"/"explicit" prefix
// for the named-builtin kinds (constant-expression / reference / dynamic / nullable /
// pointer). It inherits the base reference-equality `Equals` / identity `GetHashCode` (the
// singleton factories return shared instances, so reference-equality is faithful).
class BuiltinConversion final : public Conversion {
public:
    // The C# `internal BuiltinConversion(bool isImplicit, byte type)` -- the C# ctor is
    // assembly-private (only the `Conversion` static fields call it); the C++ port makes it
    // public (no assembly boundary) so the `Conversions` factories can build it.
    BuiltinConversion(bool isImplicit, std::uint8_t type)
        : isImplicit_(isImplicit), type_(type) {}

    bool IsImplicit() const override { return isImplicit_; }
    bool IsExplicit() const override { return !isImplicit_; }

    bool IsIdentityConversion() const override { return type_ == 0; }
    bool IsNullLiteralConversion() const override { return type_ == 1; }
    bool IsConstantExpressionConversion() const override { return type_ == 2; }
    bool IsReferenceConversion() const override { return type_ == 3; }
    bool IsDynamicConversion() const override { return type_ == 4; }
    bool IsNullableConversion() const override { return type_ == 5; }
    bool IsPointerConversion() const override { return type_ == 6; }
    bool IsBoxingConversion() const override { return type_ == 7; }
    bool IsUnboxingConversion() const override { return type_ == 8; }
    bool IsTryCast() const override { return type_ == 9; }
    bool IsInterpolatedStringConversion() const override { return type_ == 10; }
    bool IsThrowExpressionConversion() const override { return type_ == 11; }
    bool IsInlineArrayConversion() const override { return type_ == 12; }
    bool IsImplicitSpanConversion() const override { return type_ == 13; }

    // The C# `override string ToString()` -- a `switch (type)` whose cases 0/1/7/8/9/10/11
    // /12/13 return a fixed string, and whose cases 2/3/4/5/6 set a local `name` then fall
    // through to the `return (isImplicit ? "implicit " : "explicit ") + name + " conversion"`.
    // The port mirrors the local-`name`-then-fallthrough structure faithfully.
    std::string ToString() const {
        std::string name;
        switch (type_) {
            case 0: return "identity conversion";
            case 1: return "null-literal conversion";
            case 2: name = "constant-expression"; break;
            case 3: name = "reference"; break;
            case 4: name = "dynamic"; break;
            case 5: name = "nullable"; break;
            case 6: name = "pointer"; break;
            case 7: return "boxing conversion";
            case 8: return "unboxing conversion";
            case 9: return "try cast";
            case 10: return "interpolated string";
            case 11: return "throw-expression conversion";
            case 12: return "inline array conversion";
            case 13: return "implicit span conversion";
            default: break;
        }
        return (isImplicit_ ? "implicit " : "explicit ") + name + " conversion";
    }

private:
    bool isImplicit_;
    std::uint8_t type_;
};

// The factory holder mirroring the C# `Conversion` `static readonly` fields (and, in a
// follow-up iteration, the `static` factory methods). The C# static members live ON the
// `Conversion` class; the C++ port cannot add static members to the `Conversion` class from
// a separate header without a circular include (the factories build the concrete
// subclasses, which derive from `Conversion`), so a dedicated `Conversions` struct holds
// them. Each singleton accessor returns a `std::shared_ptr<Conversion>` copy of a
// function-local `static const` singleton (the Meyers-singleton `shared_ptr`), preserving
// the C# singleton-field reference-equality (`Conversions::Foo().get() ==
// Conversions::Foo().get()` is `true`).
struct Conversions {
    // `public static readonly Conversion None = new InvalidConversion();` -- the
    // not-a-valid-conversion singleton (`IsValid` false).
    static std::shared_ptr<Conversion> None() {
        static const auto singleton = std::make_shared<InvalidConversion>();
        return singleton;
    }

    // `public static readonly Conversion IdentityConversion = new BuiltinConversion(true, 0);`
    static std::shared_ptr<Conversion> IdentityConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 0);
        return singleton;
    }

    // `public static readonly Conversion NullLiteralConversion = new BuiltinConversion(true, 1);`
    static std::shared_ptr<Conversion> NullLiteralConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 1);
        return singleton;
    }

    // `public static readonly Conversion ImplicitConstantExpressionConversion = new BuiltinConversion(true, 2);`
    static std::shared_ptr<Conversion> ImplicitConstantExpressionConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 2);
        return singleton;
    }

    // `public static readonly Conversion ImplicitReferenceConversion = new BuiltinConversion(true, 3);`
    static std::shared_ptr<Conversion> ImplicitReferenceConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 3);
        return singleton;
    }

    // `public static readonly Conversion ExplicitReferenceConversion = new BuiltinConversion(false, 3);`
    static std::shared_ptr<Conversion> ExplicitReferenceConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(false, 3);
        return singleton;
    }

    // `public static readonly Conversion ImplicitDynamicConversion = new BuiltinConversion(true, 4);`
    static std::shared_ptr<Conversion> ImplicitDynamicConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 4);
        return singleton;
    }

    // `public static readonly Conversion ExplicitDynamicConversion = new BuiltinConversion(false, 4);`
    static std::shared_ptr<Conversion> ExplicitDynamicConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(false, 4);
        return singleton;
    }

    // `public static readonly Conversion ImplicitNullableConversion = new BuiltinConversion(true, 5);`
    static std::shared_ptr<Conversion> ImplicitNullableConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 5);
        return singleton;
    }

    // `public static readonly Conversion ExplicitNullableConversion = new BuiltinConversion(false, 5);`
    static std::shared_ptr<Conversion> ExplicitNullableConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(false, 5);
        return singleton;
    }

    // `public static readonly Conversion ImplicitPointerConversion = new BuiltinConversion(true, 6);`
    static std::shared_ptr<Conversion> ImplicitPointerConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 6);
        return singleton;
    }

    // `public static readonly Conversion ExplicitPointerConversion = new BuiltinConversion(false, 6);`
    static std::shared_ptr<Conversion> ExplicitPointerConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(false, 6);
        return singleton;
    }

    // `public static readonly Conversion BoxingConversion = new BuiltinConversion(true, 7);`
    static std::shared_ptr<Conversion> BoxingConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 7);
        return singleton;
    }

    // `public static readonly Conversion UnboxingConversion = new BuiltinConversion(false, 8);`
    static std::shared_ptr<Conversion> UnboxingConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(false, 8);
        return singleton;
    }

    // `public static readonly Conversion TryCast = new BuiltinConversion(false, 9);` -- the C# `as` cast.
    static std::shared_ptr<Conversion> TryCast() {
        static const auto singleton = std::make_shared<BuiltinConversion>(false, 9);
        return singleton;
    }

    // `public static readonly Conversion ImplicitInterpolatedStringConversion = new BuiltinConversion(true, 10);`
    static std::shared_ptr<Conversion> ImplicitInterpolatedStringConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 10);
        return singleton;
    }

    // `public static readonly Conversion ThrowExpressionConversion = new BuiltinConversion(true, 11);`
    static std::shared_ptr<Conversion> ThrowExpressionConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 11);
        return singleton;
    }

    // `public static readonly Conversion InlineArrayConversion = new BuiltinConversion(true, 12);`
    static std::shared_ptr<Conversion> InlineArrayConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 12);
        return singleton;
    }

    // `public static readonly Conversion ImplicitSpanConversion = new BuiltinConversion(true, 13);`
    static std::shared_ptr<Conversion> ImplicitSpanConversion() {
        static const auto singleton = std::make_shared<BuiltinConversion>(true, 13);
        return singleton;
    }
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_CONVERSION_FACTORIES_HPP
