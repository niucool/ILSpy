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

// Port of ICSharpCode.Decompiler/Semantics/Conversion.cs -- the abstract base describing
// a conversion between two types (C# spec 10.3 / ECMA-335 conversion kinds). It is the
// remaining `Semantics` dependency of `ConversionResolveResult` (the last unported
// `ResolveResult` subclass): `ConversionResolveResult` holds a `Conversion` reference and
// derives `IsError` from `!Conversion.IsValid`. The full 666-line `Conversion.cs` carries an
// abstract base plus six nested sealed subclasses (`InvalidConversion` /
// `NumericOrEnumerationConversion` / `BuiltinConversion` / `UserDefinedConv` /
// `MethodGroupConv` / `TupleConv`) and a set of `static readonly` factory fields
// (`None` / `IdentityConversion` / ...). This header ports ONLY the abstract base -- the
// virtual surface the `ConversionResolveResult` `IsError` crux reaches -- deferring the
// concrete subclasses and the static factories to a follow-up iteration (the
// `ConversionResolveResult` port will add the minimal concrete singletons the test needs,
// e.g. `None` for the invalid case). The next in-order increment after this base is
// `ConversionResolveResult`; the long-pole `CSharpAmbience` blocker remains
// `TypeSystemAstBuilder` (2782 C# lines plus `CSharpResolver` plus `Semantics`).
//
// KEY PORT CONVENTIONS:
//  * The C# `public abstract class Conversion : IEquatable<Conversion>` (an abstract
//    reference type -- not directly instantiable; the static factories build the nested
//    subclasses) ports to a C++ abstract base with a PURE-VIRTUAL destructor (mirroring
//    the C# `abstract` keyword) defined inline so the header stays header-only. A pure-
//    virtual destructor must be defined (subobject destructors call it), so the inline
//    definition follows the class. This is the first ported `Semantics` leaf that uses a
//    pure-virtual destructor to make the base abstract while keeping every virtual method
//    defaulted -- distinct from the D424 `ResolveResult` concrete-but-polymorphic base
//    (the C# `ResolveResult` is `public class`, NOT `abstract`).
//  * The ~30 C# `virtual bool Xxx` properties all carry a default (`false`, except
//    `IsValid` which defaults `true`). They port to `virtual bool Xxx() const { return
//    false; }` / `IsValid` returning `true` -- the D406 virtual-WITH-DEFAULT convention
//    (the flattened-`AbstractType` precedent) so a concrete subclass overrides only the
//    few it specializes. The `IsValid` default of `true` is the load-bearing one
//    `ConversionResolveResult.IsError` reaches (`!IsValid`).
//  * The C# `IMethod Method` (nullable reference, default `null`) ports to a nullable
//    raw-pointer return `const IMethod* Method() const { return nullptr; }` with
//    `IMethod` forward-declared (the IEntity::ParentModule nullable-pointer precedent; a
//    pointer return to an incomplete type needs only a forward declaration).
//  * The C# self-referential `Conversion ConversionBeforeUserDefinedOperator` /
//    `ConversionAfterUserDefinedOperator` (nullable, default `null`) port to nullable
//    `std::shared_ptr<Conversion>` returns (the C# GC-owned reference modelled as a
//    shared handle; a `shared_ptr` to an incomplete enclosing type is valid since the
//    inline pure-virtual destructor definition makes the type complete where the deleter
//    runs). The C# `ImmutableArray<Conversion> ElementConversions` (default empty) ports
//    to `std::vector<std::shared_ptr<Conversion>>` returned by value (a snapshot of shared
//    handles, the D438 `IList<ResolveResult>`-to-`shared_ptr`-vector precedent).
//  * The C# `IEquatable<Conversion>.Equals(Conversion other) => this == other` (reference
//    equality, since `Conversion` is a reference type) ports to `virtual bool
//    Equals(const Conversion& other) const { return this == &other; }` (pointer
//    equality, the faithful reference-equality mirror). The C# `override sealed bool
//    Equals(object)` (delegating to `Equals(obj as Conversion)`) and the C# `override int
//    GetHashCode() => base.GetHashCode()` (the identity hash `object.GetHashCode` /
//    `RuntimeHelpers.GetHashCode` yields) have C++ counterparts: the `object.Equals`
//    override has NO counterpart (C++ has no `object.Equals`), and `GetHashCode` ports to a
//    `virtual int GetHashCode() const` returning the identity hash
//    (`static_cast<int>(std::hash<const Conversion*>{}(this))` -- `std::hash<T*>` returns
//    `std::size_t`, cast to `int` to match the .NET `int` signature the subclass
//    value-based `GetHashCode` overrides XOR with). The subclasses override `Equals` /
//    `GetHashCode` with value-based implementations when ported.
//  * The C# `Conversion` base does NOT declare `ToString` (it inherits
//    `object.ToString`, and each nested subclass overrides it). The C++ port omits
//    `ToString` from the base too (faithful) -- the concrete subclasses each add their own
//    `ToString` when ported (the D432/D442 per-subclass-`ToString` convention).

#ifndef ILSPY_DECOMPILER_SEMANTICS_CONVERSION_HPP
#define ILSPY_DECOMPILER_SEMANTICS_CONVERSION_HPP

#include <cstddef>
#include <functional>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
class IMethod;
}

namespace ILSpy::Decompiler::Semantics {

// The C# `public abstract class Conversion : IEquatable<Conversion>` -- the abstract base
// describing a conversion between two types. The port is an abstract C++ base (pure-
// virtual destructor, mirroring C# `abstract`) carrying the full virtual surface with
// defaults so a concrete subclass overrides only the few conversions it specializes. The
// `IsValid` default of `true` is the load-bearing one `ConversionResolveResult.IsError`
// reaches (`!IsValid`); the `InvalidConversion` subclass (a deferred concrete subclass)
// overrides it to `false` to model the not-a-valid-conversion `None` singleton.
class Conversion {
public:
    // Pure-virtual destructor makes the base abstract (mirrors the C# `abstract class
    // Conversion` which cannot be directly instantiated -- only its nested subclasses are,
    // via the static factories). The destructor is defined inline (below) so the header
    // stays header-only and the subobject destructors that call it link cleanly.
    virtual ~Conversion() = 0;

    // The conversion is valid. Defaults to `true`; `InvalidConversion` (the `None`
    // singleton) overrides it to `false`. This is the crux `ConversionResolveResult.IsError`
    // reaches (`IsError => !Conversion.IsValid`).
    virtual bool IsValid() const { return true; }

    virtual bool IsImplicit() const { return false; }
    virtual bool IsExplicit() const { return false; }
    virtual bool IsTryCast() const { return false; }
    virtual bool IsThrowExpressionConversion() const { return false; }
    virtual bool IsIdentityConversion() const { return false; }
    virtual bool IsNullLiteralConversion() const { return false; }
    virtual bool IsConstantExpressionConversion() const { return false; }
    virtual bool IsNumericConversion() const { return false; }
    virtual bool IsLifted() const { return false; }
    virtual bool IsDynamicConversion() const { return false; }
    virtual bool IsReferenceConversion() const { return false; }
    virtual bool IsEnumerationConversion() const { return false; }
    virtual bool IsNullableConversion() const { return false; }
    virtual bool IsUserDefined() const { return false; }

    // The conversion applied to the input before the user-defined conversion operator is
    // invoked. Nullable; default `null` (C#) -> empty `shared_ptr` (C++).
    virtual std::shared_ptr<Conversion> ConversionBeforeUserDefinedOperator() const {
        return nullptr;
    }
    // The conversion applied to the result of the user-defined conversion operator.
    // Nullable; default `null` -> empty `shared_ptr`.
    virtual std::shared_ptr<Conversion> ConversionAfterUserDefinedOperator() const {
        return nullptr;
    }

    virtual bool IsBoxingConversion() const { return false; }
    virtual bool IsUnboxingConversion() const { return false; }
    virtual bool IsPointerConversion() const { return false; }
    virtual bool IsMethodGroupConversion() const { return false; }
    virtual bool IsVirtualMethodLookup() const { return false; }
    virtual bool DelegateCapturesFirstArgument() const { return false; }
    virtual bool IsAnonymousFunctionConversion() const { return false; }

    // The method associated with this conversion (the user-defined operator for
    // user-defined conversions, the chosen method for method-group conversions). Nullable;
    // default `null` -> `nullptr`.
    virtual const ILSpy::Decompiler::TypeSystem::IMethod* Method() const { return nullptr; }

    virtual bool IsTupleConversion() const { return false; }
    virtual bool IsInterpolatedStringConversion() const { return false; }
    virtual bool IsInlineArrayConversion() const { return false; }
    virtual bool IsImplicitSpanConversion() const { return false; }

    // For a tuple conversion, the individual tuple-element conversions. Default empty
    // (C# `default(ImmutableArray<Conversion>)`) -> empty vector.
    virtual std::vector<std::shared_ptr<Conversion>> ElementConversions() const {
        return {};
    }

    // The C# `IEquatable<Conversion>.Equals(Conversion other) => this == other` --
    // reference equality for the reference-type `Conversion`. The port compares pointer
    // identity (`this == &other`), the faithful mirror of the C# `this == other`
    // reference equality. Subclasses override this with value-based equality when ported.
    virtual bool Equals(const Conversion& other) const { return this == &other; }

    // The C# `override int GetHashCode() => base.GetHashCode()` -- the identity hash
    // (`object.GetHashCode` / `RuntimeHelpers.GetHashCode` yields). The port returns the
    // pointer-identity hash (`std::hash<const Conversion*>` on `this`), cast to `int` to
    // match the .NET `int` signature. Subclasses override this with value-based hashes
    // when ported.
    virtual int GetHashCode() const {
        return static_cast<int>(std::hash<const Conversion*>{}(this));
    }
};

// The pure-virtual destructor must be defined (subobject destructors call it). The inline
// definition keeps the header header-only and lets `std::shared_ptr<Conversion>` (which
// type-erases the deleter where `Conversion` is complete) link cleanly.
inline Conversion::~Conversion() {}

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_CONVERSION_HPP
