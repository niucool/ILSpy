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
// `static readonly` factory fields plus four `static` factory methods. This header ports ALL
// SIX concrete subclasses plus a `Conversions` factory holder exposing every singleton
// FIELD and factory METHOD: `InvalidConversion` (backs `None`) and `BuiltinConversion`
// (backs the ~18 builtin-conversion singleton fields) are the two singleton subclasses
// with no extra state beyond a `bool isImplicit` and a `byte type` discriminator;
// `NumericOrEnumerationConversion` (backs the four numeric-conversion singleton fields and
// the `EnumerationConversion` factory method) and `TupleConv` (backs the `TupleConversion`
// factory method) are the two `IMethod`-free value-based subclasses; and `UserDefinedConv`
// (backs the `UserDefinedConversion` factory method) and `MethodGroupConv` (backs the
// `MethodGroupConversion` / `InvalidMethodGroupConversion` factory methods) are the two
// `IMethod`-bearing value-based subclasses. The two singleton subclasses (`InvalidConversion`
// / `BuiltinConversion`) inherit the base reference-equality `Equals` / identity
// `GetHashCode` (their factories return shared singletons, so reference-equality is
// faithful); the four value-based subclasses (`NumericOrEnumerationConversion` / `TupleConv`
// / `UserDefinedConv` / `MethodGroupConv`) override `Equals` / `GetHashCode` with value-based
// implementations (two distinct instances with equal state are equal) because their
// factory METHODS build a NEW instance per call. The `UserDefinedConv` / `MethodGroupConv`
// value-based `Equals` / `GetHashCode` fold the `IMethod` method handle by reference-equality
// and identity hash (the C# `method.Equals(o.method)` / `method.GetHashCode()` one-argument
// calls resolve to `object.Equals` / `object.GetHashCode`, i.e. reference-equality and the
// identity hash, since the one-argument call does not match the two-argument
// `IMember.Equals(IMember, TypeVisitor)`).
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
//    match for the C# singleton-field reference-equality. The four value-based subclasses
//    (`NumericOrEnumerationConversion` / `TupleConv` / `UserDefinedConv` / `MethodGroupConv`)
//    DO override `Equals` / `GetHashCode` with value-based implementations -- two distinct
//    instances with equal state are equal, distinct from the singleton reference-equality
//    above. The `IMethod`-bearing pair (`UserDefinedConv` / `MethodGroupConv`) folds the
//    method handle by reference-equality / identity-hash (the C# one-argument
//    `method.Equals` / `method.GetHashCode` resolve to `object.Equals` / `object.GetHashCode`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_CONVERSION_FACTORIES_HPP
#define ILSPY_DECOMPILER_SEMANTICS_CONVERSION_FACTORIES_HPP

#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"

#include <cassert>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

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

// The C# `sealed class NumericOrEnumerationConversion : Conversion` -- the numeric or
// enumeration conversion backing the four numeric-conversion singleton fields and the
// `EnumerationConversion` factory method. It carries three bool fields (`isImplicit` /
// `isLifted` / `isEnumeration`) and overrides `IsImplicit` / `IsExplicit` /
// `IsNumericConversion` / `IsEnumerationConversion` / `IsLifted` plus a value-based
// `Equals` / `GetHashCode` (two distinct instances with the same three bools are equal,
// distinct from the singleton reference-equality the `InvalidConversion` /
// `BuiltinConversion` factories inherit). This is the load-bearing crux distinguishing a
// value-based conversion (the `static` factory METHOD `EnumerationConversion` builds a
// NEW instance per call, so two calls with equal args are distinct instances that must
// compare equal by value) from a singleton-backed field.
class NumericOrEnumerationConversion final : public Conversion {
public:
    // The C# `NumericOrEnumerationConversion(bool isImplicit, bool isLifted, bool
    // isEnumeration = false)` -- the `isEnumeration` defaulted arg lets the four numeric
    // readonly fields omit it while `EnumerationConversion` passes `true`. The C++ port
    // mirrors the defaulted arg.
    NumericOrEnumerationConversion(bool isImplicit, bool isLifted, bool isEnumeration = false)
        : isImplicit_(isImplicit), isLifted_(isLifted), isEnumeration_(isEnumeration) {}

    bool IsImplicit() const override { return isImplicit_; }
    // The C# `public override bool IsExplicit => !isImplicit`.
    bool IsExplicit() const override { return !isImplicit_; }
    // The C# `public override bool IsNumericConversion => !isEnumeration`.
    bool IsNumericConversion() const override { return !isEnumeration_; }
    bool IsEnumerationConversion() const override { return isEnumeration_; }
    bool IsLifted() const override { return isLifted_; }

    // The C# `override string ToString()` -- `(isImplicit ? "implicit" : "explicit") +
    // (isLifted ? " lifted" : "") + (isEnumeration ? " enumeration" : " numeric") +
    // " conversion"`. Non-virtual (the base omits `ToString`); the `final` class needs no
    // further-override concern.
    std::string ToString() const {
        return std::string(isImplicit_ ? "implicit" : "explicit")
            + (isLifted_ ? " lifted" : "")
            + (isEnumeration_ ? " enumeration" : " numeric")
            + " conversion";
    }

    // The C# `override bool Equals(Conversion other)` -- `other as
    // NumericOrEnumerationConversion` then compare the three bools. Value-based: two
    // distinct instances with equal state are equal (the dynamic_cast-yields-null path
    // returns false for a different subtype, mirroring the C# `as`-returns-null).
    bool Equals(const Conversion& other) const override {
        const auto* o = dynamic_cast<const NumericOrEnumerationConversion*>(&other);
        return o != nullptr
            && isImplicit_ == o->isImplicit_
            && isLifted_ == o->isLifted_
            && isEnumeration_ == o->isEnumeration_;
    }

    // The C# `override int GetHashCode() => (isImplicit ? 1 : 0) + (isLifted ? 2 : 0) +
    // (isEnumeration ? 4 : 0)` -- a pure-int bit-OR-by-addition hash (the three bools map
    // to disjoint bits 1/2/4, so the hash distinguishes all eight combinations).
    int GetHashCode() const override {
        return (isImplicit_ ? 1 : 0) + (isLifted_ ? 2 : 0) + (isEnumeration_ ? 4 : 0);
    }

private:
    bool isImplicit_;
    bool isLifted_;
    bool isEnumeration_;
};

// The C# `sealed class TupleConv : Conversion` -- the tuple conversion backing the
// `TupleConversion` factory method. It carries an `ImmutableArray<Conversion>
// ElementConversions` (ported to `std::vector<std::shared_ptr<Conversion>>`) and a computed
// `IsImplicit` (`elementConversions.All(c => c.IsImplicit)` -- true for an empty array,
// vacuously). It overrides `IsImplicit` / `IsExplicit` / `ElementConversions` /
// `IsTupleConversion` plus a value-based `Equals` (`ElementConversions.SequenceEqual`,
// which calls each element's `Equals`) and `GetHashCode` (the `hash * 31 + conv.GetHashCode`
// fold). Two distinct `TupleConv`s with `SequenceEqual` element conversions are equal.
class TupleConv final : public Conversion {
public:
    // The C# `TupleConv(ImmutableArray<Conversion> elementConversions)` stores the array
    // and computes `IsImplicit = elementConversions.All(c => c.IsImplicit)`. The C# struct
    // `ImmutableArray<Conversion>` ports to `std::vector<std::shared_ptr<Conversion>>`
    // (shared ownership of each element; a snapshot, the D438 `IList<ResolveResult>`-to-
    // `shared_ptr`-vector precedent). The ctor takes the vector by value and moves it.
    explicit TupleConv(std::vector<std::shared_ptr<Conversion>> elementConversions)
        : elementConversions_(std::move(elementConversions)),
          isImplicit_(ComputeIsImplicit(elementConversions_)) {}

    bool IsImplicit() const override { return isImplicit_; }
    bool IsExplicit() const override { return !isImplicit_; }
    bool IsTupleConversion() const override { return true; }

    // The C# `public override ImmutableArray<Conversion> ElementConversions { get; }` --
    // returns a copy of the stored vector (a snapshot of shared handles).
    std::vector<std::shared_ptr<Conversion>> ElementConversions() const override {
        return elementConversions_;
    }

    // The C# `override string ToString() => (IsImplicit ? "implicit " : "explicit ") +
    // " tuple conversion"` -- note the source carries a leading space in " tuple
    // conversion" AND a trailing space in "implicit "/"explicit ", so the result has TWO
    // spaces between the kind and "tuple" (e.g. "implicit  tuple conversion"). Ported
    // verbatim (not "fixed"); the double-space is a C# source quirk.
    std::string ToString() const {
        return std::string(isImplicit_ ? "implicit " : "explicit ") + " tuple conversion";
    }

    // The C# `override bool Equals(Conversion other) => other is TupleConv o &&
    // ElementConversions.SequenceEqual(o.ElementConversions)` -- `SequenceEqual` uses the
    // `IEquatable<Conversion>.Equals` for each element pair, so the port calls each
    // element's virtual `Equals` (value-based for the concrete subclasses, reference for
    // the base). A size-mismatch short-circuits before the element loop.
    bool Equals(const Conversion& other) const override {
        const auto* o = dynamic_cast<const TupleConv*>(&other);
        if (o == nullptr) return false;
        if (elementConversions_.size() != o->elementConversions_.size()) return false;
        for (std::size_t i = 0; i < elementConversions_.size(); ++i) {
            if (!elementConversions_[i]->Equals(*o->elementConversions_[i])) return false;
        }
        return true;
    }

    // The C# `override int GetHashCode()` -- `unchecked { int hash = 0; foreach (var conv
    // in ElementConversions) { hash *= 31; hash += conv.GetHashCode(); } return hash; }`.
    // The `unchecked` wraparound ports to `unsigned int` accumulation + `static_cast<int>`
    // return (well-defined modular wraparound, no signed-overflow UB; the D400
    // `FullTypeNameComparer.GetHashCode` precedent). An empty array yields hash 0.
    int GetHashCode() const override {
        unsigned int hash = 0;
        for (const auto& conv : elementConversions_) {
            hash = hash * 31u + static_cast<unsigned int>(conv->GetHashCode());
        }
        return static_cast<int>(hash);
    }

private:
    // The C# `elementConversions.All(c => c.IsImplicit)` -- true for an empty array
    // (vacuously, the `All` over an empty sequence is true), so an empty `TupleConv` is
    // implicit.
    static bool ComputeIsImplicit(const std::vector<std::shared_ptr<Conversion>>& convs) {
        for (const auto& c : convs) {
            if (!c->IsImplicit()) return false;
        }
        return true;
    }

    std::vector<std::shared_ptr<Conversion>> elementConversions_;
    bool isImplicit_;
};

// The C# `sealed class UserDefinedConv : Conversion` -- the user-defined (op_Implicit /
// op_Explicit) conversion backing the `UserDefinedConversion` factory method. It carries an
// `IMethod method` handle (non-owning; the method is owned by the type system), a `bool
// isLifted`, two `Conversion`s (`conversionBeforeUserDefinedOperator` /
// `conversionAfterUserDefinedOperator`, nullable, shared via `shared_ptr`), a `bool
// isImplicit`, and a computed `bool isValid` (= `!isAmbiguous`). It overrides `IsValid` /
// `IsImplicit` / `IsExplicit` / `IsLifted` / `IsUserDefined` /
// `ConversionBeforeUserDefinedOperator` / `ConversionAfterUserDefinedOperator` / `Method` plus
// a value-based `Equals` / `GetHashCode` (two distinct instances with equal state are equal
// -- the `UserDefinedConversion` factory METHOD builds a NEW instance per call, so two
// calls with equal args must compare equal by value). The `method` is folded into the
// value-based `Equals` / `GetHashCode` by reference-equality / identity-hash (the C#
// `method.Equals(o.method)` / `method.GetHashCode()` one-argument calls resolve to
// `object.Equals` / `object.GetHashCode`, NOT the two-argument `IMember.Equals(IMember,
// TypeVisitor)`).
class UserDefinedConv final : public Conversion {
public:
    // The C# `UserDefinedConv(bool isImplicit, IMethod method, Conversion
    // conversionBeforeUserDefinedOperator, Conversion conversionAfterUserDefinedOperator,
    // bool isLifted, bool isAmbiguous)` -- stores the fields and computes `isValid =
    // !isAmbiguous`. The C# `IMethod method` (a non-null reference the factory guards with
    // `ArgumentNullException`) ports to a non-owning `const IMethod*` raw pointer (the
    // method is owned by the type system; the D437 `MemberResolveResult` non-owning
    // `const IMember*` precedent). The two `Conversion` reference fields (nullable in C#)
    // port to `std::shared_ptr<Conversion>` (shared ownership; the D451 self-referential
    // `shared_ptr<Conversion>` convention).
    UserDefinedConv(bool isImplicit, const ILSpy::Decompiler::TypeSystem::IMethod* method,
                    std::shared_ptr<Conversion> conversionBeforeUserDefinedOperator,
                    std::shared_ptr<Conversion> conversionAfterUserDefinedOperator,
                    bool isLifted, bool isAmbiguous)
        : method_(method),
          isLifted_(isLifted),
          conversionBefore_(std::move(conversionBeforeUserDefinedOperator)),
          conversionAfter_(std::move(conversionAfterUserDefinedOperator)),
          isImplicit_(isImplicit),
          isValid_(!isAmbiguous) {}

    bool IsValid() const override { return isValid_; }
    bool IsImplicit() const override { return isImplicit_; }
    bool IsExplicit() const override { return !isImplicit_; }
    bool IsLifted() const override { return isLifted_; }
    bool IsUserDefined() const override { return true; }

    std::shared_ptr<Conversion> ConversionBeforeUserDefinedOperator() const override {
        return conversionBefore_;
    }
    std::shared_ptr<Conversion> ConversionAfterUserDefinedOperator() const override {
        return conversionAfter_;
    }

    const ILSpy::Decompiler::TypeSystem::IMethod* Method() const override { return method_; }

    // The C# `override bool Equals(Conversion other)` -- `other as UserDefinedConv` then
    // compare `isLifted` / `isImplicit` / `isValid` and `method.Equals(o.method)`. The
    // `method.Equals(o.method)` one-argument call resolves to `object.Equals` (reference-
    // equality), so the port compares the `IMethod*` pointers for identity. Value-based:
    // two distinct instances with equal state (incl. the same method pointer) are equal.
    bool Equals(const Conversion& other) const override {
        const auto* o = dynamic_cast<const UserDefinedConv*>(&other);
        return o != nullptr
            && isLifted_ == o->isLifted_
            && isImplicit_ == o->isImplicit_
            && isValid_ == o->isValid_
            && method_ == o->method_;
    }

    // The C# `override int GetHashCode() => unchecked(method.GetHashCode() + (isLifted ?
    // 31 : 27) + (isImplicit ? 71 : 61) + (isValid ? 107 : 109))`. The `method.GetHashCode()`
    // resolves to `object.GetHashCode` (the identity hash), ported to
    // `std::hash<const IMethod*>`. The `unchecked` wraparound ports to `unsigned int`
    // accumulation + `static_cast<int>` return (well-defined modular wraparound, no signed-
    // overflow UB; the D400 `FullTypeNameComparer.GetHashCode` precedent).
    int GetHashCode() const override {
        unsigned int hash = static_cast<unsigned int>(
            std::hash<const ILSpy::Decompiler::TypeSystem::IMethod*>{}(method_));
        hash += static_cast<unsigned int>(isLifted_ ? 31 : 27);
        hash += static_cast<unsigned int>(isImplicit_ ? 71 : 61);
        hash += static_cast<unsigned int>(isValid_ ? 107 : 109);
        return static_cast<int>(hash);
    }

    // The C# `override string ToString() => (isImplicit ? "implicit" : "explicit") +
    // (isLifted ? " lifted" : "") + (isValid ? "" : " ambiguous") + "user-defined conversion
    // (" + method + ")"`. The `+ method` calls `method.ToString()` which the minimal `IMethod`
    // port does NOT expose (no `ToString` virtual); the faithful counterpart uses
    // `method_->FullName()` (the closest meaningful fully-qualified representation the C#
    // `method.ToString()` includes for a real `IMethod` implementation) -- a documented
    // deviation (the D433 `NamespaceResolveResult` `ns.ToString()`-to-`FullName()` precedent
    // applied to a method).
    std::string ToString() const {
        return std::string(isImplicit_ ? "implicit" : "explicit")
            + (isLifted_ ? " lifted" : "")
            + (isValid_ ? "" : " ambiguous")
            + "user-defined conversion (" + method_->FullName() + ")";
    }

private:
    const ILSpy::Decompiler::TypeSystem::IMethod* method_;
    bool isLifted_;
    std::shared_ptr<Conversion> conversionBefore_;
    std::shared_ptr<Conversion> conversionAfter_;
    bool isImplicit_;
    bool isValid_;
};

// The C# `sealed class MethodGroupConv : Conversion` -- the method-group conversion
// backing the `MethodGroupConversion` / `InvalidMethodGroupConversion` factory methods. It
// carries an `IMethod method` handle (non-owning), a `bool isVirtualMethodLookup`, a `bool
// delegateCapturesFirstArgument`, and a `bool isValid` (set by the factory: `true` for
// `MethodGroupConversion`, `false` for `InvalidMethodGroupConversion`). It overrides
// `IsValid` / `IsImplicit` (always `true`) / `IsMethodGroupConversion` /
// `IsVirtualMethodLookup` / `DelegateCapturesFirstArgument` / `Method` plus a value-based
// `Equals` / `GetHashCode` folding ONLY the `method` handle by reference-equality /
// identity-hash (the C# `Equals` compares `method.Equals(o.method)` alone -- the other
// fields do NOT participate, so two method-group conversions over the SAME method but
// different lookup/capture flags are EQUAL). The C# does NOT override `ToString`, so the
// port adds none.
class MethodGroupConv final : public Conversion {
public:
    // The C# `MethodGroupConv(IMethod method, bool isVirtualMethodLookup, bool
    // delegateCapturesFirstArgument, bool isValid)` -- stores all four fields. The C#
    // `IMethod method` (a non-null reference the factory guards with `ArgumentNullException`)
    // ports to a non-owning `const IMethod*` raw pointer (the D437 precedent).
    MethodGroupConv(const ILSpy::Decompiler::TypeSystem::IMethod* method,
                    bool isVirtualMethodLookup, bool delegateCapturesFirstArgument,
                    bool isValid)
        : method_(method),
          isVirtualMethodLookup_(isVirtualMethodLookup),
          delegateCapturesFirstArgument_(delegateCapturesFirstArgument),
          isValid_(isValid) {}

    bool IsValid() const override { return isValid_; }
    bool IsImplicit() const override { return true; }
    bool IsMethodGroupConversion() const override { return true; }
    bool IsVirtualMethodLookup() const override { return isVirtualMethodLookup_; }
    bool DelegateCapturesFirstArgument() const override {
        return delegateCapturesFirstArgument_;
    }
    const ILSpy::Decompiler::TypeSystem::IMethod* Method() const override { return method_; }

    // The C# `override bool Equals(Conversion other) => other is MethodGroupConv o &&
    // method.Equals(o.method)` -- compares ONLY the `method` (the other fields do NOT
    // participate). The `method.Equals(o.method)` one-argument call resolves to
    // `object.Equals` (reference-equality), so the port compares the `IMethod*` pointers
    // for identity. Two method-group conversions over the SAME method (even with different
    // lookup/capture flags or different `isValid`) are EQUAL.
    bool Equals(const Conversion& other) const override {
        const auto* o = dynamic_cast<const MethodGroupConv*>(&other);
        return o != nullptr && method_ == o->method_;
    }

    // The C# `override int GetHashCode() => method.GetHashCode()` -- the identity hash of
    // the method (the one-argument `GetHashCode` resolves to `object.GetHashCode`).
    int GetHashCode() const override {
        return static_cast<int>(
            std::hash<const ILSpy::Decompiler::TypeSystem::IMethod*>{}(method_));
    }

private:
    const ILSpy::Decompiler::TypeSystem::IMethod* method_;
    bool isVirtualMethodLookup_;
    bool delegateCapturesFirstArgument_;
    bool isValid_;
};

// The factory holder mirroring the C# `Conversion` `static readonly` fields and the
// `static` factory methods. The C# static members live ON the
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

    // `public static readonly Conversion ImplicitNumericConversion = new
    // NumericOrEnumerationConversion(true, false);` -- the implicit numeric-conversion
    // singleton (`IsImplicit` true, `IsLifted` false, `IsNumericConversion` true).
    static std::shared_ptr<Conversion> ImplicitNumericConversion() {
        static const auto singleton = std::make_shared<NumericOrEnumerationConversion>(true, false);
        return singleton;
    }

    // `public static readonly Conversion ExplicitNumericConversion = new
    // NumericOrEnumerationConversion(false, false);`.
    static std::shared_ptr<Conversion> ExplicitNumericConversion() {
        static const auto singleton = std::make_shared<NumericOrEnumerationConversion>(false, false);
        return singleton;
    }

    // `public static readonly Conversion ImplicitLiftedNumericConversion = new
    // NumericOrEnumerationConversion(true, true);`.
    static std::shared_ptr<Conversion> ImplicitLiftedNumericConversion() {
        static const auto singleton = std::make_shared<NumericOrEnumerationConversion>(true, true);
        return singleton;
    }

    // `public static readonly Conversion ExplicitLiftedNumericConversion = new
    // NumericOrEnumerationConversion(false, true);`.
    static std::shared_ptr<Conversion> ExplicitLiftedNumericConversion() {
        static const auto singleton = std::make_shared<NumericOrEnumerationConversion>(false, true);
        return singleton;
    }

    // `public static Conversion EnumerationConversion(bool isImplicit, bool isLifted) =>
    // new NumericOrEnumerationConversion(isImplicit, isLifted, true)` -- a FACTORY METHOD (a
    // NEW instance per call, NOT a singleton): two calls with equal args return DISTINCT
    // instances that compare EQUAL by value (the `NumericOrEnumerationConversion` value-
    // based `Equals`). This is the structural distinction from the singleton FIELDS above:
    // the FIELDS return the same instance (reference-equal), the METHODS return fresh
    // instances (value-equal).
    static std::shared_ptr<Conversion> EnumerationConversion(bool isImplicit, bool isLifted) {
        return std::make_shared<NumericOrEnumerationConversion>(isImplicit, isLifted, true);
    }

    // `public static Conversion TupleConversion(ImmutableArray<Conversion> conversions)
    // => new TupleConv(conversions)` -- a FACTORY METHOD (a NEW instance per call). Two
    // calls with `SequenceEqual` conversions return DISTINCT instances that compare EQUAL
    // by value (the `TupleConv` value-based `Equals`).
    static std::shared_ptr<Conversion> TupleConversion(
            std::vector<std::shared_ptr<Conversion>> conversions) {
        return std::make_shared<TupleConv>(std::move(conversions));
    }

    // `public static Conversion UserDefinedConversion(IMethod operatorMethod, bool
    // isImplicit, Conversion conversionBeforeUserDefinedOperator, Conversion
    // conversionAfterUserDefinedOperator, bool isLifted = false, bool isAmbiguous = false)
    // => new UserDefinedConv(...)` -- a FACTORY METHOD (a NEW instance per call). The C#
    // `operatorMethod == null` `ArgumentNullException` guard ports to an `assert` (the D424
    // convention). Two calls with equal args return DISTINCT instances that compare EQUAL
    // by value (the `UserDefinedConv` value-based `Equals`).
    static std::shared_ptr<Conversion> UserDefinedConversion(
            const ILSpy::Decompiler::TypeSystem::IMethod* operatorMethod, bool isImplicit,
            std::shared_ptr<Conversion> conversionBeforeUserDefinedOperator,
            std::shared_ptr<Conversion> conversionAfterUserDefinedOperator,
            bool isLifted = false, bool isAmbiguous = false) {
        assert(operatorMethod != nullptr);
        return std::make_shared<UserDefinedConv>(isImplicit, operatorMethod,
            std::move(conversionBeforeUserDefinedOperator),
            std::move(conversionAfterUserDefinedOperator), isLifted, isAmbiguous);
    }

    // `public static Conversion MethodGroupConversion(IMethod chosenMethod, bool
    // isVirtualMethodLookup, bool delegateCapturesFirstArgument) => new MethodGroupConv(
    // ..., isValid: true)` -- the VALID method-group conversion factory method. The C#
    // `chosenMethod == null` `ArgumentNullException` guard ports to an `assert`.
    static std::shared_ptr<Conversion> MethodGroupConversion(
            const ILSpy::Decompiler::TypeSystem::IMethod* chosenMethod,
            bool isVirtualMethodLookup, bool delegateCapturesFirstArgument) {
        assert(chosenMethod != nullptr);
        return std::make_shared<MethodGroupConv>(chosenMethod, isVirtualMethodLookup,
            delegateCapturesFirstArgument, /*isValid*/ true);
    }

    // `public static Conversion InvalidMethodGroupConversion(IMethod chosenMethod, bool
    // isVirtualMethodLookup, bool delegateCapturesFirstArgument) => new MethodGroupConv(
    // ..., isValid: false)` -- the INVALID method-group conversion factory method (the
    // `isValid: false` twin of `MethodGroupConversion`). The C# `chosenMethod == null`
    // `ArgumentNullException` guard ports to an `assert`.
    static std::shared_ptr<Conversion> InvalidMethodGroupConversion(
            const ILSpy::Decompiler::TypeSystem::IMethod* chosenMethod,
            bool isVirtualMethodLookup, bool delegateCapturesFirstArgument) {
        assert(chosenMethod != nullptr);
        return std::make_shared<MethodGroupConv>(chosenMethod, isVirtualMethodLookup,
            delegateCapturesFirstArgument, /*isValid*/ false);
    }
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_CONVERSION_FACTORIES_HPP
