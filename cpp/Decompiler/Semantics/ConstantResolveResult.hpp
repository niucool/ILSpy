// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/ConstantResolveResult.cs -- the resolved
// expression is a compile-time constant (mainly a literal). `ConstantResolveResult`
// is the eighth `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience`
// (the long-pole remaining blocker), after `ResolveResult` (D424),
// `TypeResolveResult` (D425), `ThisResolveResult` (D426), `TypeOfResolveResult`
// (D427), `ByReferenceResolveResult` (D428), `SizeOfResolveResult` (D430), and
// `TypeIsResolveResult` (D431). The C# source declares a ctor taking an `IType`
// (forwarded to the base) plus an `object constantValue` (the boxed compile-time
// value, which may be `null` for a `null` literal -- the C# ctor does NOT guard it
// with `ArgumentNullException`, unlike `Type`/`ReferencedType`), and overrides two
// `ResolveResult` virtuals: `IsCompileTimeConstant` (always `true`) and
// `ConstantValue` (returns the stored `constantValue`). It ALSO overrides
// `ToString` directly with a custom format `"[<class-name> <type> = <value>]"` --
// distinct from the sibling subclasses that inherit the base `ToString` and only
// override `ClassName()` -- because the constant value must appear in the debug
// rendering, so the format diverges from the base `"[<class-name> <type>]"`.
//
// The load-bearing crux of this port is the `std::any` stringification the custom
// `ToString` requires: the C# `string.Format(CultureInfo.InvariantCulture,
// "[{0} {1} = {2}]", GetType().Name, this.Type, constantValue)` calls
// `constantValue.ToString()` polymorphically (yielding the invariant-culture
// representation -- `"True"`/`"False"` for a `bool`, the digits for an integer, the
// text for a `string`, the empty string for `null`). The C++ `std::any` (the D374
// `object?` representation) has no polymorphic `ToString`, so the port supplies a
// private `StringifyConstantValue` helper that dispatches on `std::any::type()` to
// the boxed literal types the resolver produces (bool / int32 / int64 / uint32 /
// uint64 / string / char / double / float / null), reproducing the C#
// invariant-culture conventions for the common cases. The C++ port additionally
// overrides `ClassName()` (so a future subclass that inherits `ToString` without
// overriding it still reports its own name, the D424 convention) and
// `ShallowClone()` (to faithfully mirror the C# `MemberwiseClone` runtime-type
// preservation, avoiding the C++-only slicing a non-overriding base clone would
// perform -- the documented `ResolveResult` convention every subclass port
// carries).

#ifndef ILSPY_DECOMPILER_SEMANTICS_CONSTANTRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_CONSTANTRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <typeinfo>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ConstantResolveResult : ResolveResult` (NOT `sealed`, and
// no C# subclass derives from it) ports to a C++ subclass (NOT `final`) of
// `ResolveResult`. The C# surface is:
//   * `ConstantResolveResult(IType type, object constantValue) : base(type)` --
//     forwards `type` to the `ResolveResult` base (whose ctor guards it with
//     `ArgumentNullException` -> `assert`, the D424 convention) and stores
//     `constantValue` (a boxed compile-time value; the C# ctor does NOT guard it,
//     so a `null` literal stores a `null` `constantValue`).
//   * `override bool IsCompileTimeConstant => true` -- a constant is always a
//     compile-time constant.
//   * `override object? ConstantValue => constantValue` -- the stored boxed value.
//   * `override string ToString() => string.Format(CultureInfo.InvariantCulture,
//     "[{0} {1} = {2}]", GetType().Name, this.Type, constantValue)` -- the custom
//     debug format including the constant value.
//
// The C# does NOT override `ShallowClone`, but the C++ port overrides it (the
// documented `ResolveResult` convention) to preserve the runtime type via the
// default copy ctor (which copies the `std::any` value member, faithful to the
// C# `MemberwiseClone` value-copy of the boxed `object`).
class ConstantResolveResult : public ResolveResult {
public:
    // The C# `ConstantResolveResult(IType type, object constantValue) :
    // base(type)`: forwards `type` to the `ResolveResult` base (whose ctor guards
    // it with `ArgumentNullException` -> `assert`, the D424 convention) and stores
    // `constantValue`. The C# ctor does NOT guard `constantValue` with
    // `ArgumentNullException` (a `null` literal stores a `null` `constantValue`),
    // so the C++ port does NOT `assert` on an empty `std::any` -- an empty `any`
    // is the faithful C# `null`. The C# `object constantValue` ports to
    // `std::any` (the D374 `IVariable::GetConstantValue` / D424 `ConstantValue`
    // convention); the boxed literal the resolver stores is copied into the
    // `any` value member.
    ConstantResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type,
                           std::any constantValue)
        : ResolveResult(std::move(type)),
          constantValue_(std::move(constantValue)) {
    }

    // The C# `override bool IsCompileTimeConstant => true`: a constant is always
    // a compile-time constant (distinct from the base default `false` and from
    // the `SizeOfResolveResult` D430 `constantValue != null` conditional).
    bool IsCompileTimeConstant() const override { return true; }

    // The C# `override object? ConstantValue => constantValue`: returns the
    // stored boxed value. The C# `object?` ports to `std::any` (the D374/D424
    // convention); an empty `any` is the C# `null` (a `null` literal), a
    // non-empty `any` holds the boxed literal. Returning a COPY of the `any`
    // (not a reference) mirrors the C# `object?` value-return and keeps the
    // stored value immutable.
    std::any ConstantValue() const override { return constantValue_; }

    // The C# `override string ToString() => string.Format(InvariantCulture,
    // "[{0} {1} = {2}]", GetType().Name, this.Type, constantValue)`: the custom
    // debug format including the constant value. The C++ port reproduces it:
    // `"[<class-name> <type reflection name> = <stringified value>]"`. The
    // `ClassName()` helper (overridden below) supplies the polymorphic class
    // name (the C# `GetType().Name`); the base `Type().ReflectionName()` is the
    // C# `this.Type` (the `AbstractType.ToString` -> `ReflectionName`
    // convention); the `StringifyConstantValue` helper supplies the
    // invariant-culture representation of the boxed `constantValue` (the C#
    // `constantValue.ToString()` polymorphic dispatch).
    std::string ToString() const override {
        return "[" + ClassName() + " " + Type().ReflectionName() + " = "
            + StringifyConstantValue(constantValue_) + "]";
    }

    // The C# `ToString` uses `GetType().Name` (polymorphic class name); the
    // inherited base `ToString` is overridden directly here (the custom format
    // diverges from the base), but the `ClassName()` override is kept so a
    // future subclass that inherits this `ToString` reports its own name (the
    // D424 convention), and so the custom `ToString` reuses it without
    // duplicating the class-name string.
protected:
    std::string ClassName() const override { return "ConstantResolveResult"; }

public:
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `ConstantResolveResult` stays a
    // `ConstantResolveResult` (not sliced to the `ResolveResult` base). The C++
    // override reproduces this by constructing a `ConstantResolveResult` copy
    // (the default copy ctor copies the base `type_` `shared_ptr` and the own
    // `constantValue_` `std::any` value member, faithful to the C# reference-copy
    // of the `IType` and value-copy of the boxed `object`).
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ConstantResolveResult>(*this);
    }

private:
    // The C# `constantValue.ToString()` polymorphic dispatch (under
    // `string.Format`'s `InvariantCulture`) ports to a `typeid`-dispatched
    // helper because `std::any` has no polymorphic `ToString`. Each arm reproduces
    // the C# invariant-culture representation for the boxed literal type the
    // resolver produces:
    //   * an empty `any` (the C# `null`) renders as the empty string (C#
    //     `string.Format` prints `""` for a `null` argument).
    //   * a `bool` renders as `"True"` / `"False"` (the C# `Boolean.ToString`
    //     invariant representation, capitalised).
    //   * an integer (`int32` / `int64` / `uint32` / `uint64`) renders as its
    //     decimal digits (the C# integer `ToString` invariant representation).
    //   * a `std::string` renders as itself (the C# `String.ToString` identity).
    //   * a `char16_t` (the C# `char`, a UTF-16 code unit) renders as its UTF-8
    //     encoding (the C# `Char.ToString` yields the character).
    //   * a `double` / `float` renders via `std::to_string` -- a DOCUMENTED
    //     deviation from the C# invariant-culture "G" general format (which
    //     trims trailing zeros, e.g. `3.0` -> `"3"`, while `std::to_string`
    //     yields `"3.000000"`); the constant-value `ToString` is a debug aid
    //     (not load-bearing for the decompiler output), and the integer / bool
    //     / string / null arms (the testable, unambiguous cases) are exact.
    //   * an unhandled type renders as `"<unknown>"` (clearly distinguishable
    //     from the faithful `null` -> `""` so a missing arm is visible in debug
    //     output rather than silently producing the wrong value).
    static std::string StringifyConstantValue(const std::any& value) {
        if (!value.has_value()) return "";
        const std::type_info& t = value.type();
        if (t == typeid(bool))
            return std::any_cast<bool>(value) ? "True" : "False";
        if (t == typeid(std::int32_t))
            return std::to_string(std::any_cast<std::int32_t>(value));
        if (t == typeid(std::int64_t))
            return std::to_string(std::any_cast<std::int64_t>(value));
        if (t == typeid(std::uint32_t))
            return std::to_string(std::any_cast<std::uint32_t>(value));
        if (t == typeid(std::uint64_t))
            return std::to_string(std::any_cast<std::uint64_t>(value));
        if (t == typeid(std::string))
            return std::any_cast<std::string>(value);
        if (t == typeid(char16_t)) {
            char16_t c = std::any_cast<char16_t>(value);
            std::string s;
            if (c <= 0x7F) {
                s += static_cast<char>(c);
            } else if (c <= 0x7FF) {
                s += static_cast<char>(0xC0 | (c >> 6));
                s += static_cast<char>(0x80 | (c & 0x3F));
            } else {
                s += static_cast<char>(0xE0 | (c >> 12));
                s += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
                s += static_cast<char>(0x80 | (c & 0x3F));
            }
            return s;
        }
        if (t == typeid(double))
            return std::to_string(std::any_cast<double>(value));
        if (t == typeid(float))
            return std::to_string(std::any_cast<float>(value));
        return "<unknown>";
    }

    std::any constantValue_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_CONSTANTRESOLVERESULT_HPP
