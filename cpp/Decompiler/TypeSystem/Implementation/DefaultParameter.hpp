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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/DefaultParameter.cs --
// the default `IParameter` implementation: a parameter built directly from a type,
// a name, and the optional parameter metadata (owner, attributes, reference kind,
// params/optional flags, default value). `CSharpOperators.InitParameterArrays`
// builds its whole `normalParameters` / `nullableParameters` tables out of the
// 2-argument shape (`new DefaultParameter(compilation.FindType(i), string.Empty)`),
// so this leaf is the direct prerequisite for the `CSharpOperators` port (the
// operator-method factory `CSharpResolver` resolves built-in operators through);
// the resolver-driven decompiler pipeline (CallBuilder / ExpressionBuilder) also
// constructs lambda/anonymous-method parameters with it.
//
// The class additionally carries the CANONICAL PARAMETER-SIGNATURE RENDERER the
// whole `IParameter` hierarchy shares: the static `ToString(IParameter)` that
// renders "[ref |out |in |ref readonly ][params ]name:TypeReflectionName[ = default]"
// (the C# `DefaultParameter.ToString(IParameter)` static, which `SpecializedParameter`
// and `DefaultParameter` itself delegate their own `ToString()` overrides to).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `public sealed class DefaultParameter : IParameter` ports to a
//      `final class DefaultParameter : public IParameter` (the interface-to-abstract-base
//      convention; `final` mirrors `sealed`, pinned by static_assert). `IParameter :
//      IVariable : ISymbol` is SINGLE inheritance (no diamond), so the single `Name()`
//      override is the final overrider for the whole chain (the D374
//      inherited-virtual-covers-the-`new` precedent).
//  (b) The C# has TWO constructors: the 2-argument `DefaultParameter(IType, string)`
//      (attributes empty, everything else defaulted) and the full
//      `DefaultParameter(IType, string, owner = null, attributes = null,
//      referenceKind = None, isParams = false, isOptional = false, defaultValue = null)`.
//      The 2-argument shape produces EXACTLY the state the full ctor's default
//      arguments produce (both ctors assign the same fields; the second merely
//      assigns the defaults explicitly), so the port collapses them into ONE ctor
//      with default arguments -- behavior-identical for every call shape (the
//      StandardImplicitConversion allowTuple-overload collapse precedent).
//  (c) The C# `ArgumentNullException` guards: `type == null` throws; the port throws
//      `std::invalid_argument` (the `IntersectionType.Create` ArgumentNullException
//      precedent). `name == null` cannot occur for a `std::string` (a C++ string is
//      never null), so that guard compiles out (the D374 references-non-null
//      convention) and is not testable.
//  (d) The C# `readonly IType type` ports to an owning `ITypePtr type_` (the caller
//      shares ownership; the `SpecializedParameter` newType_ precedent) --
//      `IVariable::Type()` returns `const IType&`, so `Type()` returns `*type_`
//      (the reference is stable for the parameter's lifetime).
//  (e) The C# `readonly IReadOnlyList<IAttribute> attributes` with the
//      `?? EmptyList<IAttribute>.Instance` null-normalization ports to a
//      `std::vector<const IAttribute*>` SNAPSHOT taken by value (a C++ vector is
//      never null -- an empty vector IS the EmptyList normalization; the
//      `IEntity::GetAttributes` snapshot convention).
//  (f) The C# `readonly object defaultValue` ports to `std::any defaultValue_`
//      (empty = the C# null; the `IVariable::GetConstantValue` std::any convention);
//      `GetConstantValue(bool)` returns it verbatim, ignoring `throwOnInvalidMetadata`
//      exactly like the C# (the default value is caller-provided, never invalid
//      metadata).
//  (g) The C# `readonly IParameterizedMember owner` (a NULLABLE reference -- the
//      `IParameter.Owner` "May return null" contract for lambda/anonymous-method
//      parameters) ports to a NON-OWNING `const IParameterizedMember*` (the
//      `SpecializedParameter` newOwner_ precedent: the owning member owns the
//      parameter, the back-reference is non-owning).
//  (h) The C# `bool HasConstantValueInSignature => IsOptional` is the load-bearing
//      coupling: this implementation reports the in-signature flag AS the optional
//      flag. The static `ToString(IParameter)` still tests the CONJUNCTION
//      `IsOptional && HasConstantValueInSignature`, because a WRAPPER `IParameter`
//      (e.g. `SpecializedParameter` over a metadata parameter) may report the two
//      flags independently -- pinned by the
//      ToStringOmitsConstantWhenOptionalButNotInSignature test over a local stub.
//  (i) The C# `LifetimeAnnotation Lifetime => default` returns the all-false
//      default-constructed annotation (`DefaultParameter.Lifetime => default`
//      returns the default value; the LifetimeAnnotation header note).
//  (j) The C# `bool IVariable.IsConst => false` (explicit interface
//      implementation, always false -- a parameter is never a C#-like const).
//  (k) The `ReferenceKind` / `SymbolKind` accessor return types are GLOBALLY
//      QUALIFIED: the inherited member-function names shadow the namespace-scope
//      enums of the same names inside the class body (the D372 name-hiding crux).
//      The static `ToString` hoists a local `using RK = ...` alias for the switch
//      labels (the same crux, resolved without spelling the full qualification on
//      every label).
//  (l) The C# switch's `_ => throw new NotSupportedException()` arm is unreachable
//      for the five well-formed `ReferenceKind` values (all handled); the port keeps
//      a `default:` throwing `std::runtime_error` that guards only a casted
//      out-of-range enum input (the safe-faithful-fallback convention).
//  (m) The `val.ToString()` on the BOXED default value has no single C++ equivalent
//      (there is no `object.ToString` virtual dispatch over `std::any`); the port
//      renders the primitive types a default value carries with the C#-faithful
//      spellings -- `bool` renders "True"/"False" (the C# invariant bool.ToString),
//      the integral types render decimal digits, `float`/`double` render the
//      SHORTEST ROUND-TRIP representation via `std::to_chars` (matching the
//      .NET Core 3.0+ shortest-round-trip double/float formatting), `std::string`
//      renders itself -- and falls back to the held type's name for anything else
//      (the C# `object.ToString` default returns the runtime type's name). The
//      `char`-boxed shape is not rendered character-wise (the minimal port boxes
//      no `char` defaults); the fallback covers it.
//  (n) The port's `ISymbol` / `IParameter` surface carries NO virtual `ToString`
//      (the DummyTypeParameter.ToString plain-member precedent), so `ToString()`
//      is a PLAIN member alongside the STATIC `ToString(const IParameter&)` -- an
//      overload set distinguished by arity (the C# instance override + static of
//      the same name). `SpecializedParameter`'s own deferred `ToString()` (its
//      header comment (f)) lands through the static renderer this leaf carries.

#pragma once

#include "Decompiler/TypeSystem/IParameter.hpp"

#include <any>
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The default `IParameter` implementation (see the header comment). All accessors
// are inline (simple field reads); the header is header-only (no .cpp -- no
// `TypeVisitor` / complete-type requirements beyond `IParameter`, which is included).
class DefaultParameter final : public IParameter {
public:
    // The C# `DefaultParameter(IType type, string name)` and
    // `DefaultParameter(IType type, string name, IParameterizedMember owner = null,
    // IReadOnlyList<IAttribute> attributes = null, ReferenceKind referenceKind =
    // ReferenceKind.None, bool isParams = false, bool isOptional = false,
    // object defaultValue = null)` -- collapsed into ONE ctor with default arguments
    // (the 2-argument shape and the full ctor's defaults produce the identical state,
    // convention (b)). `type` must be non-null (the C# ArgumentNullException ports to
    // std::invalid_argument, convention (c)); `attributes` is a snapshot by value
    // (convention (e)); `defaultValue` is the boxed default (convention (f));
    // `owner` is a nullable non-owning pointer (convention (g)).
    DefaultParameter(ITypePtr type, std::string name,
                     const IParameterizedMember* owner = nullptr,
                     std::vector<const IAttribute*> attributes = {},
                     ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind
                         = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                     bool isParams = false,
                     bool isOptional = false,
                     std::any defaultValue = {})
        : type_(std::move(type)),
          name_(std::move(name)),
          owner_(owner),
          attributes_(std::move(attributes)),
          referenceKind_(referenceKind),
          isParams_(isParams),
          isOptional_(isOptional),
          defaultValue_(std::move(defaultValue))
    {
        // The C# ctor throws `ArgumentNullException` on a null type (convention (c);
        // the init-then-throw-body TokenWriter / IntersectionType precedent).
        if (!type_) {
            throw std::invalid_argument("DefaultParameter: type must not be null");
        }
    }

    // --- IParameter ---

    // The C# `IEnumerable<IAttribute> GetAttributes() => attributes` -- the attribute
    // snapshot (empty for the default; convention (e)).
    std::vector<const IAttribute*> GetAttributes() const override {
        return attributes_;
    }

    // The C# `ReferenceKind ReferenceKind => referenceKind`. The return type is
    // globally qualified: the inherited `IParameter::ReferenceKind` member name
    // shadows the namespace-scope `ReferenceKind` enum in MSVC's complete-class
    // lookup (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override {
        return referenceKind_;
    }

    // The C# `LifetimeAnnotation Lifetime => default` -- the all-false
    // default-constructed annotation (convention (i)).
    LifetimeAnnotation Lifetime() const override {
        return LifetimeAnnotation{};
    }

    // The C# `bool IsParams => isParams`.
    bool IsParams() const override {
        return isParams_;
    }

    // The C# `bool IsOptional => isOptional`.
    bool IsOptional() const override {
        return isOptional_;
    }

    // The C# `bool HasConstantValueInSignature => IsOptional` -- the load-bearing
    // coupling (convention (h)): this implementation reports the in-signature flag
    // AS the optional flag.
    bool HasConstantValueInSignature() const override {
        return isOptional_;
    }

    // The C# `IParameterizedMember? Owner => owner` -- nullable (nullptr for
    // lambda/anonymous-method parameters, the `IParameter.Owner` "May return null"
    // contract; convention (g)).
    const IParameterizedMember* Owner() const override {
        return owner_;
    }

    // --- IVariable ---

    // The C# `IType Type => type` -- a reference to the held type (convention (d)).
    const IType& Type() const override {
        return *type_;
    }

    // The C# `bool IVariable.IsConst => false` (convention (j)).
    bool IsConst() const override {
        return false;
    }

    // The C# `object GetConstantValue(bool throwOnInvalidMetadata) => defaultValue`
    // -- the boxed default value verbatim (convention (f)); the flag is ignored
    // exactly like the C# (the default value is caller-provided, never invalid
    // metadata).
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/) const override {
        return defaultValue_;
    }

    // --- ISymbol ---

    // The C# `string Name => name`.
    std::string Name() const override {
        return name_;
    }

    // The C# `SymbolKind ISymbol.SymbolKind => SymbolKind.Parameter`. The return
    // type is globally qualified: the inherited `ISymbol::SymbolKind` member name
    // shadows the namespace-scope `SymbolKind` enum in MSVC's complete-class lookup
    // (the D372 crux).
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }

    // --- ToString ---

    // The C# `public override string ToString() => ToString(this)` (the instance
    // override delegating to the static renderer). A PLAIN member -- the port's
    // `ISymbol` surface carries no virtual `ToString` (convention (n)) -- overloaded
    // with the static below by arity.
    std::string ToString() const {
        return ToString(*this);
    }

    // The C# `public static string ToString(IParameter parameter)` -- the CANONICAL
    // PARAMETER-SIGNATURE RENDERER the whole `IParameter` hierarchy shares (the
    // `SpecializedParameter.ToString` delegates here; `DefaultParameter.ToString()`
    // above delegates here): "[ref |out |in |ref readonly ][params ]name:
    // TypeReflectionName[ = default]".
    static std::string ToString(const IParameter& parameter) {
        // The local alias keeps the ReferenceKind switch labels readable: the
        // inherited member name shadows the namespace-scope enum inside this class
        // body (convention (k)).
        using RK = ::ILSpy::Decompiler::TypeSystem::ReferenceKind;
        std::string result;
        switch (parameter.ReferenceKind()) {
            case RK::None:
                break;
            case RK::Ref:
                result += "ref ";
                break;
            case RK::Out:
                result += "out ";
                break;
            case RK::In:
                result += "in ";
                break;
            case RK::RefReadOnly:
                result += "ref readonly ";
                break;
            // The C# switch throws NotSupportedException for an out-of-range
            // value; every well-formed `ReferenceKind` is handled above, so this
            // arm guards only a casted out-of-range input (convention (l)).
            default:
                throw std::runtime_error("NotSupportedException: ReferenceKind");
        }
        if (parameter.IsParams())
            result += "params ";
        result += parameter.Name();
        result += ':';
        result += parameter.Type().ReflectionName();
        // The C# conjunction: the constant renders only when the parameter is
        // optional AND its constant value is presented in the signature -- a
        // wrapper `IParameter` may report the two flags independently (convention
        // (h)).
        if (parameter.IsOptional() && parameter.HasConstantValueInSignature()) {
            result += " = ";
            std::any val = parameter.GetConstantValue(false);
            if (val.has_value())
                result += BoxedValueToString(val);
            else
                result += "null";
        }
        return result;
    }

private:
    // The port's stand-in for the C# `object.ToString()` virtual dispatch on the
    // boxed default value (convention (m)): the primitive spellings a default value
    // carries, with the type-name fallback.
    static std::string BoxedValueToString(const std::any& value) {
        if (const bool* v = std::any_cast<bool>(&value))
            return *v ? "True" : "False";
        if (const std::int8_t* v = std::any_cast<std::int8_t>(&value))
            return std::to_string(static_cast<int>(*v));
        if (const std::uint8_t* v = std::any_cast<std::uint8_t>(&value))
            return std::to_string(static_cast<unsigned int>(*v));
        if (const std::int16_t* v = std::any_cast<std::int16_t>(&value))
            return std::to_string(static_cast<int>(*v));
        if (const std::uint16_t* v = std::any_cast<std::uint16_t>(&value))
            return std::to_string(static_cast<unsigned int>(*v));
        if (const std::int32_t* v = std::any_cast<std::int32_t>(&value))
            return std::to_string(*v);
        if (const std::uint32_t* v = std::any_cast<std::uint32_t>(&value))
            return std::to_string(*v);
        if (const std::int64_t* v = std::any_cast<std::int64_t>(&value))
            return std::to_string(*v);
        if (const std::uint64_t* v = std::any_cast<std::uint64_t>(&value))
            return std::to_string(*v);
        if (const float* v = std::any_cast<float>(&value))
            return RenderShortestFloatingPoint(*v);
        if (const double* v = std::any_cast<double>(&value))
            return RenderShortestFloatingPoint(*v);
        if (const std::string* v = std::any_cast<std::string>(&value))
            return *v;
        // The C# `object.ToString` default returns the runtime type's name.
        return value.type().name();
    }

    // The shortest round-trip rendering of a floating-point default value via
    // `std::to_chars` (the .NET Core 3.0+ float/double ToString behavior,
    // convention (m)); a 64-character buffer never overflows for a shortest
    // rendering of any float/double.
    template <typename T>
    static std::string RenderShortestFloatingPoint(T value) {
        char buffer[64];
        const std::to_chars_result result =
            std::to_chars(buffer, buffer + sizeof(buffer), value);
        if (result.ec != std::errc{})
            return std::string();
        return std::string(buffer, result.ptr);
    }

    // Member order matches the C# field block (type, name, attributes, reference
    // kind, flags, default value, owner), reordered so the ctor init list and the
    // declarations agree (no -Wreorder).
    ITypePtr type_;
    std::string name_;
    const IParameterizedMember* owner_;
    std::vector<const IAttribute*> attributes_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind_;
    bool isParams_;
    bool isOptional_;
    std::any defaultValue_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
