// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Syntax/TypeSystemAstBuilder.cs: the
// self-contained static helpers land as free functions in this namespace, and the
// TypeSystemAstBuilder class itself (the long-pole CSharpAmbience blocker, 2782 C#
// lines) is ported incrementally. The class holds a CSharpResolver field and threads
// the full IType / IMember / ITypeDefinition surface: the class skeleton (the resolver
// field, the two ctors, InitProperties, and the full configuration property surface),
// the "Convert Type" region (ConvertType / ConvertTypeHelper / TypeMatches /
// TypeDefMatches / AddTypeArguments / ConvertNamespace / IsValidNamespace, C# lines
// 266-768, implemented in TypeSystemAstBuilder.cpp), the "Convert Attribute" +
// "Convert Attribute Type" regions (ConvertAttribute / ConvertAttributes /
// ConvertAttributeType / ApplyShortAttributeNameIfPossible / IsAttributeType, C#
// lines 770-988), the "Convert Constant Value" SUPPORT helpers (IsSpecialConstant +
// ConvertFloatingPointLiteral + MakeConstant, C# lines 1168-1249 + 1496-1582), the
// mutually-recursive "Convert Constant Value" CORE (the three ConvertConstantValue
// overloads + ConvertEnumValue, C# lines 998-1078 + 1306-1480), the "Convert
// Parameter" region (ConvertParameter, C# lines 1786-1826, consuming the
// IsDefaultValueAssignmentAllowed prerequisite landed in TypeSystemExtensions),
// and the "Convert Type Parameter" + "Convert Variable" regions
// (ConvertTypeParameter / ConvertTypeParameterConstraint, C# lines 2601-2741,
// and ConvertVariable, C# lines 2743-2761), plus the nullability-disambiguation
// tail of the Convert Type Parameter region (AddNullabilityDisambiguatingConstraints
// + the NullableTypeParameterCollector visitor + GetNullabilityDisambiguator, C#
// lines 2683-2734, consumed only by the deferred ConvertEntity), the "Convert
// Modifiers" region (NeedsAccessibility + GetMemberModifiers, C# lines 2518-2596,
// consuming the ModifierFromAccessibility free function and the LocalFunctionMethod
// wrapper), and the "Convert Entity" accessor-support cluster (GenerateBodyBlock +
// ConvertAccessor + MergeReadOnlyModifiers + GetExplicitInterfaceType, C# lines
// 2188-2297 + 2771-2782, the shared prerequisites of the member renderers) are
// landed; the remaining `Convert*` instance methods (the ConvertProperty /
// ConvertIndexer / ConvertEvent / ConvertMethod / ConvertOperator /
// ConvertTypeDefinition renderers plus the ConvertSymbol / ConvertEntity /
// ConvertExtension entries) follow in later slices, consuming the members below
// as they grow.
//
// ModifierFromAccessibility (TypeSystemAstBuilder.cs line 2497) is the first such
// helper: a pure switch on Accessibility (the D373 leaf) that maps a symbol's
// visibility to the Syntax Modifiers bits (the D270 enum). It is the public static
// API the CSharpAmbience / GetMemberModifiers path calls; the usePrivateProtected
// bool arg gates whether ProtectedAndInternal maps to `private protected`
// (Private | Protected) or to the C# 7 fallback `protected` (Protected) -- the
// UsePrivateProtectedAccessibility property of TypeSystemAstBuilder.

#pragma once

#include "Accessor.hpp"
#include "EntityDeclaration.hpp"
#include "MemberType.hpp"
#include "Modifiers.hpp"
#include "SimpleType.hpp"

#include "Decompiler/CSharp/Resolver/NameLookupMode.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

// The CSharpResolver is forward-declared at global scope (a qualified
// namespace-definition written inside another namespace declares a fresh shadow
// chain on MSVC, shadowing the global ::ILSpy for every later qualified reference
// in the file): the TypeSystemAstBuilder class below holds the resolver as a
// shared_ptr over the incomplete type, which the skeleton's stores and moves
// support without the full definition.
namespace ILSpy::Decompiler::CSharp::Resolver { class CSharpResolver; }

// Forward declarations for the parameter types the TypeSystemAstBuilder class
// declares over incomplete types (the .cpp includes the full headers):
// `FullTypeName` (a const& parameter), the Semantics `NamespaceResolveResult`
// (the shared_ptr out-parameter), and the Syntax `Attribute` / `AttributeSection`
// nodes (the Convert Attribute region's return types). Written at GLOBAL scope --
// a qualified namespace-definition inside another namespace declares a fresh
// shadow chain on MSVC (the iteration-94 UsingScope trap).
namespace ILSpy::Decompiler::TypeSystem { class FullTypeName; }
namespace ILSpy::Decompiler::TypeSystem { class IParameter; }
namespace ILSpy::Decompiler::TypeSystem { class ITypeParameter; }
namespace ILSpy::Decompiler::TypeSystem { class IVariable; }
namespace ILSpy::Decompiler::Semantics { class NamespaceResolveResult; }
namespace ILSpy::Decompiler::CSharp::Syntax {
class Attribute;
class AttributeSection;
class BlockStatement;
class Constraint;
class MethodDeclaration;
class ParameterDeclaration;
class TypeParameterDeclaration;
class VariableDeclarationStatement;
} // namespace ILSpy::Decompiler::CSharp::Syntax
// The ResolveResult-based ConvertConstantValue overload passes the result by
// value (the C# parameter the body rebinds through the ConversionResolveResult
// unwrap), so the class declaration needs the shared_ptr element type complete
// enough for the template-id (a forward declaration suffices; the .cpp includes
// the full ResolveResult.hpp for the member access).
namespace ILSpy::Decompiler::Semantics { class ResolveResult; }

namespace ILSpy::Decompiler::CSharp::Syntax {

// Forward-declared for the member declarations below (the `Convert Constant
// Value` support methods return / produce `Expression*`; the .cpp includes the
// full expression headers). `Expression` is a Syntax-namespace type.
class Expression;

// Namespace-scope aliases shared by the free functions and the
// TypeSystemAstBuilder class below (the class body references both namespaces in
// its member declarations; a member-local alias would not reach them all).
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;

// TypeSystemAstBuilder.ModifierFromAccessibility (TypeSystemAstBuilder.cs line 2497).
// Maps an Accessibility value to the corresponding Syntax Modifiers bits. The
// ProtectedAndInternal case is gated by usePrivateProtected: when true it emits
// `private protected` (Modifiers::Private | Modifiers::Protected, the C# 7.2
// private-protected accessibility); when false it falls back to the pre-C#-7.2
// `protected` (Modifiers::Protected). The default case (Accessibility::None or any
// other value) returns Modifiers::None.
inline Modifiers ModifierFromAccessibility(::ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
                                          bool usePrivateProtected) noexcept {
    namespace TS = ::ILSpy::Decompiler::TypeSystem;
    switch (accessibility) {
        case TS::Accessibility::Private:
            return Modifiers::Private;
        case TS::Accessibility::Public:
            return Modifiers::Public;
        case TS::Accessibility::Protected:
            return Modifiers::Protected;
        case TS::Accessibility::Internal:
            return Modifiers::Internal;
        case TS::Accessibility::ProtectedOrInternal:
            return Modifiers::Protected | Modifiers::Internal;
        case TS::Accessibility::ProtectedAndInternal:
            return usePrivateProtected ? (Modifiers::Private | Modifiers::Protected)
                                      : Modifiers::Protected;
        default:
            return Modifiers::None;
    }
}

// ---------------------------------------------------------------------------
// Pure-math fraction helpers (TypeSystemAstBuilder.cs lines 1458-1490 and
// 1725-1773). These back ConvertFloatingPointLiteral, which renders a
// floating-point literal as a rational `num / den` BinaryOperatorExpression
// when the decimal form is long and an exact-enough fraction exists within the
// max-denominator bound. They are pure (no type-system state), so they port
// ahead of the instance method that drives them.
//
// The C# `long` arguments are System.Int64 (64-bit); C++ `long` is 32-bit on
// MSVC Windows, so the port uses std::int64_t to preserve the 64-bit range the
// continued-fraction accumulator relies on. The C# `(long Num, long Den)` tuple
// return ports to std::pair<std::int64_t, std::int64_t>.
// ---------------------------------------------------------------------------

// TypeSystemAstBuilder.IsValidFraction (TypeSystemAstBuilder.cs line 1458).
// A (num, den) pair is a valid printable fraction when the denominator is
// positive, the numerator is non-zero, the fraction is proper (|num| < den)
// unless it is whole (den == 1) or a unit (|num| == 1), and the denominator is
// 5-smooth (divisible by 2, 3, or 5) -- the denominators that yield a short
// terminating decimal expansion. The 5-smooth gate rejects coincidental
// fractions such as 1f/MathF.PI == 113f/355f.
inline bool IsValidFraction(std::int64_t num, std::int64_t den) noexcept {
    if (!(den > 0 && num != 0))
        return false;

    if (den == 1 || std::llabs(num) == 1)
        return true;
    return std::llabs(num) < den && (den % 2 == 0 || den % 3 == 0 || den % 5 == 0);
}

// TypeSystemAstBuilder.EqualDoubles / EqualFloats (TypeSystemAstBuilder.cs
// lines 1469 and 1477). The C# `in` parameters and [MethodImpl(NoInlining)]
// force the values through memory (no more than 64/32 bits of precision),
// defeating any JIT retention in an 80-bit x87 register. The C++ port passes
// by value: MSVC x64 uses SSE2 (exact 64/32-bit), so by-value carries no
// extended precision, and the NoInlining hint has no C++ counterpart.
inline bool EqualDoubles(double val1, double val2) noexcept {
    return val1 == val2;
}

inline bool EqualFloats(float val1, float val2) noexcept {
    return val1 == val2;
}

// TypeSystemAstBuilder.IsEqual (TypeSystemAstBuilder.cs line 1484). Compares a
// candidate fraction num/den against the boxed constant value, dispatching to
// EqualDoubles or EqualFloats by the isDouble flag. The C# `object` parameter
// (the coerced constant value, a boxed double or float) ports to const
// std::any&; the C# `(double)`/`(float)` cast (which throws InvalidCastException
// on a mismatched box) ports to std::any_cast (which throws std::bad_any_cast),
// the D374 object?-to-std::any convention.
inline bool IsEqual(std::int64_t num, std::int64_t den,
                    const std::any& constantValue, bool isDouble) {
    if (isDouble) {
        return EqualDoubles(std::any_cast<double>(constantValue),
                            num / static_cast<double>(den));
    } else {
        return EqualFloats(std::any_cast<float>(constantValue),
                           num / static_cast<float>(den));
    }
}

// TypeSystemAstBuilder.FractionApprox (TypeSystemAstBuilder.cs line 1725).
// Returns the best rational approximation (num, den) to `value` with
// den <= maxDenominator, via the continued-fraction / semi-convergent algorithm.
// Returns (0, 0) when |value| exceeds 0x7FFFFFFF (the magnitude guard) or when
// no non-trivial denominator is produced. The sign is stripped for the
// continued-fraction accumulation (which would overflow on a large negative)
// and re-applied from the original startValue at the return.
inline std::pair<std::int64_t, std::int64_t> FractionApprox(double value, int maxDenominator) {
    // The range check has to be on the magnitude: the sign is stripped below, so a
    // large negative value would otherwise reach the continued-fraction loop and
    // overflow the terms it accumulates.
    if (std::fabs(value) > 0x7FFFFFFF)
        return {0, 0};

    double startValue = value;
    if (value < 0)
        value = -value;

    std::int64_t ai;
    std::int64_t m[2][2] = {{1, 0}, {0, 1}};

    double v = value;

    while (m[1][0] * (ai = static_cast<std::int64_t>(v)) + m[1][1] <= maxDenominator) {
        std::int64_t t = m[0][0] * ai + m[0][1];
        m[0][1] = m[0][0];
        m[0][0] = t;
        t = m[1][0] * ai + m[1][1];
        m[1][1] = m[1][0];
        m[1][0] = t;
        if (v - static_cast<double>(ai) == 0)
            break;
        v = 1 / (v - static_cast<double>(ai));
        if (std::fabs(v) >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            // values greater than long.MaxValue cannot be stored in fraction without overflow.
            // Because the implicit conversion of long.MaxValue to double loses precision,
            // it's possible that a value v that is strictly greater than long.MaxValue will
            // nevertheless compare equal, so we use ">=" to compensate.
            break;
        }
    }

    if (m[1][0] == 0)
        return {0, 0};

    std::int64_t firstN = m[0][0];
    std::int64_t firstD = m[1][0];

    ai = (maxDenominator - m[1][1]) / m[1][0];
    std::int64_t secondN = m[0][0] * ai + m[0][1];
    std::int64_t secondD = m[1][0] * ai + m[1][1];

    double firstDelta = std::fabs(value - firstN / static_cast<double>(firstD));
    double secondDelta = std::fabs(value - secondN / static_cast<double>(secondD));

    if (firstDelta < secondDelta)
        return {startValue < 0 ? -firstN : firstN, firstD};
    return {startValue < 0 ? -secondN : secondN, secondD};
}

// ---------------------------------------------------------------------------
// GetNullabilityDisambiguator (TypeSystemAstBuilder.cs line 2707). Returns the
// constraint keyword that keeps `T?` meaning a nullable annotation on an
// override or explicit interface implementation, or nullopt where the type
// parameter neither needs nor permits one. The C# `string?` return ports to
// `std::optional<std::string>` (nullopt = the C# null string).
//
// The switch is on `tp.IsReferenceType`, the `bool?` (D429 `std::optional<bool>`)
// accessor `ITypeParameter` inherits from `IType`:
//   true      => "class"    (a reference type: the constraint keeps the nullable
//                            annotation; C# accepts only plain `class`, never
//                            `class?`, because the constraint's own nullability
//                            is inherited from the base member)
//   nullopt   => "default"  (constrained to neither a reference type nor a value
//                            type)
//   false     => nullopt     (a value type uses `Nullable<T>` rather than a
//                            nullable annotation)
// ---------------------------------------------------------------------------
inline std::optional<std::string> GetNullabilityDisambiguator(
    const ::ILSpy::Decompiler::TypeSystem::ITypeParameter& tp) {
    const auto refType = tp.IsReferenceType();
    if (refType.has_value()) {
        if (*refType)
            return std::optional<std::string>("class");
        return std::nullopt;
    }
    return std::optional<std::string>("default");
}

// ---------------------------------------------------------------------------
// NullableTypeParameterCollector (TypeSystemAstBuilder.cs lines 2717-2731, the
// `sealed class NullableTypeParameterCollector(IReadOnlyList<ITypeParameter>
// typeParameters) : TypeVisitor` nested in the builder). Collects the type
// parameters of one method that appear with a nullable annotation (`T?`)
// anywhere in a visited type, including nested positions such as `List<T?>` or
// `T?[]`. Type parameters of any other owner are ignored: a specialized
// signature can substitute a foreign type parameter that happens to share an
// index with one of this method's own.
//
// The C# `sealed` ports to `final`; the C# private nested class is lifted to
// namespace scope for direct TDD ahead of its only consumer
// (`AddNullabilityDisambiguatingConstraints` below -- the ILiftedOperator lift
// precedent). The C# `HashSet<ITypeParameter>` field (the default reference-
// equality comparer -- `ITypeParameter` declares no `Equals` override, so the
// set is an identity set) ports to an insertion-ordered
// `std::vector<const ITypeParameter*>` with pointer-identity dedup (the
// iteration-67 TP-bounds convention); the C# `IReadOnlyList<ITypeParameter>`
// ctor parameter ports to the by-value `std::vector<const ITypeParameter*>`
// (the `IMethod::TypeParameters()` return shape). The C# pattern match
// `type is NullabilityAnnotatedTypeParameter { Nullability: Nullability.Nullable }
// natp && typeParameters.Contains(natp.OriginalTypeParameter)` ports to the
// `dynamic_cast` + `Nullability()` check + pointer-scan conjunction, and the C#
// `base.VisitNullabilityAnnotatedType(type)` (continue into the children so the
// nested positions record too) ports to the `TypeVisitor` base default -- the
// `NullabilityAnnotatedTypeParameter::VisitChildren` override keeps that walk
// off the diamond's `bad_weak_ptr` arms.
// ---------------------------------------------------------------------------
class NullableTypeParameterCollector final : public TS::TypeVisitor {
public:
    explicit NullableTypeParameterCollector(
        std::vector<const TS::ITypeParameter*> typeParameters)
        : typeParameters_(std::move(typeParameters)) {}

    // The C# `public readonly HashSet<ITypeParameter> NullableTypeParameters = [];`
    // (the recorded set; `Contains` over it is how
    // `AddNullabilityDisambiguatingConstraints` gates the per-parameter clause).
    std::vector<const TS::ITypeParameter*> NullableTypeParameters;

    TS::ITypePtr VisitNullabilityAnnotatedType(TS::NullabilityAnnotatedType& type) override {
        if (auto* natp = dynamic_cast<TS::NullabilityAnnotatedTypeParameter*>(&type)) {
            if (natp->Nullability() == TS::Nullability::Nullable &&
                ContainsTypeParameter(natp->OriginalTypeParameter().get())) {
                AddNullable(natp->OriginalTypeParameter().get());
            }
        }
        return TS::TypeVisitor::VisitNullabilityAnnotatedType(type);
    }

private:
    // The C# `typeParameters.Contains(...)` (LINQ over the ctor-captured list;
    // the default equality comparer is reference equality).
    bool ContainsTypeParameter(const TS::ITypeParameter* tp) const {
        return std::find(typeParameters_.begin(), typeParameters_.end(), tp) !=
               typeParameters_.end();
    }
    // The C# `NullableTypeParameters.Add(...)` (HashSet idempotence under
    // reference equality).
    void AddNullable(const TS::ITypeParameter* tp) {
        if (std::find(NullableTypeParameters.begin(), NullableTypeParameters.end(), tp) ==
            NullableTypeParameters.end()) {
            NullableTypeParameters.push_back(tp);
        }
    }

    std::vector<const TS::ITypeParameter*> typeParameters_;
};

// ---------------------------------------------------------------------------
// IsObjectOrValueType (TypeSystemAstBuilder.cs line 2736). Returns true when
// `type` resolves to the `System.Object` or `System.ValueType` type definition --
// the two base types whose nullable-annotation disambiguation the builder must
// handle specially (a `T?` where `T : object`/`T : ValueType` keeps the nullable
// annotation rather than collapsing to a `Nullable<T>`). The C# body is
// `d = type.GetDefinition(); return d != null && (d.KnownTypeCode == Object ||
// d.KnownTypeCode == ValueType)` -- it reads `IType.GetDefinition()` (the D459
// virtual-with-default `nullptr` accessor) then `ITypeDefinition.KnownTypeCode()`
// (the D393 accessor returning the D271 enum by value).
//
// A type whose `GetDefinition()` returns null (arrays, pointers, type parameters,
// the C++-only minimal `KnownType`/`SimpleType`/`SpecialType`, ... -- the D459
// `nullptr`-default inheritors) yields false; a resolved type definition yields
// true only when its `KnownTypeCode` is `Object` or `ValueType`.
// ---------------------------------------------------------------------------
inline bool IsObjectOrValueType(const ::ILSpy::Decompiler::TypeSystem::IType& type) {
    const auto* d = type.GetDefinition();
    return d != nullptr &&
           (d->KnownTypeCode() == ::ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object ||
            d->KnownTypeCode() == ::ILSpy::Decompiler::TypeSystem::KnownTypeCode::ValueType);
}

// ---------------------------------------------------------------------------
// MergeReadOnlyModifiers (TypeSystemAstBuilder.cs line 2277). A local static
// helper inside ConvertProperty/ConvertIndexer/ConvertCustomEvent that lifts the
// `readonly` modifier from the accessor(s) to the declaration when ALL accessors
// carrying it agree -- the C# 7.2 `readonly` on a property/event is stored on the
// declaration, but the resolver may have placed it on the individual accessors.
//
// The C# `EntityDeclaration decl` (non-null) ports to `EntityDeclaration& decl`
// (a non-null reference); the C# `Accessor? accessor1`/`accessor2` (nullable) port
// to `Accessor*` (a nullable raw pointer, nullptr = the C# null). The C#
// `accessor2!.HasModifier(...)` null-forgiving deref ports to an unguarded
// `accessor2->HasModifier(...)` (the D354 `!`-to-unguarded-deref convention): the
// dereference is safe because the `&&` short-circuits on the preceding
// `accessor1->HasModifier(Readonly)` and the first `if`-branch already returned
// when `accessor1` has `readonly` and `accessor2` is null, so the `else if` only
// reaches the `accessor2->` dereference when `accessor2` is non-null.
//
// The `Modifiers &= ~Readonly` / `|= Readonly` compound assignments port to
// read-modify-write through the `Modifiers()` getter/setter pair (a property with
// no `&=`/`|=` C++ counterpart on an `enum class`): `accessor1->Modifiers(
// accessor1->Modifiers() & ~Modifiers::Readonly)` clears the bit, `decl.Modifiers(
// decl.Modifiers() | Modifiers::Readonly)` sets it. The `[Flags]` bitwise
// operators (`|`, `&`, `~`) on `Modifiers` are the D270 free functions.
// ---------------------------------------------------------------------------
inline void MergeReadOnlyModifiers(EntityDeclaration& decl,
                                  Accessor* accessor1,
                                  Accessor* accessor2) {
    if (accessor1 == nullptr)
        return;
    if (accessor1->HasModifier(Modifiers::Readonly) && accessor2 == nullptr) {
        accessor1->Modifiers(accessor1->Modifiers() & ~Modifiers::Readonly);
        decl.Modifiers(decl.Modifiers() | Modifiers::Readonly);
    } else if (accessor1->HasModifier(Modifiers::Readonly) &&
               accessor2->HasModifier(Modifiers::Readonly)) {
        accessor1->Modifiers(accessor1->Modifiers() & ~Modifiers::Readonly);
        accessor2->Modifiers(accessor2->Modifiers() & ~Modifiers::Readonly);
        decl.Modifiers(decl.Modifiers() | Modifiers::Readonly);
    }
}

// ---------------------------------------------------------------------------
// CompareType / CompareAttribute (TypeSystemAstBuilder.cs lines 886 and 835),
// the local static attribute-sorting pair inside `ConvertAttributes`. The
// `SortAttributes` path orders an entity's attributes by attribute type, then by
// the decoded arguments, so the emitted `[Attr(...)]` list is stable across
// re-decompilations. `CompareType` orders two types by their full name;
// `CompareAttribute` orders two attributes by type, decode-errors, then the
// positional (Fixed) and named argument lists.
//
// The C# `IType.FullName` (the qualified `Namespace.Name`) is NOT exposed by the
// minimal IType port (deferred to Phase 2 with the rest of the `AbstractType`
// surface); `ReflectionName()` is the closest equivalent (the D433/D455
// FullName-to-ReflectionName convention), so `CompareType` compares
// `ReflectionName` -- a documented deviation that holds for the attribute types
// the decoder produces (their `ReflectionName` is the namespace-qualified name
// the C# `FullName` would yield for a top-level type).
//
// The C# `argA.Value is IComparable compA && argB.Value is IComparable compB
// ? compA.CompareTo(compB) : 0` has no direct C++ counterpart: `std::any` is
// type-erased with no polymorphic `CompareTo`, so a `CompareAny` helper
// dispatches on `std::any::type()` (a typeid ladder) to the comparable BCL
// primitives (`bool`, `char`, the integer types, `float`, `double`, `string`)
// and the boxed `ITypePtr` (a `System.Type`, compared by `ReflectionName` as the
// C# `Type.CompareTo` full-name ordering), comparing same-type pairs. An empty
// `std::any` (the C# `null`, which is not `IComparable`) yields 0; mismatched or
// unhandled types (arrays, the boxed nested `CustomAttributeTypedArgument`)
// yield 0 too -- the C# `else -> 0` branch for a non-`IComparable`, and a
// documented deviation for the mismatched-type case (the C# `CompareTo` would
// throw `ArgumentException`, but the well-formed same-AttributeType case never
// reaches it, so the port returns 0 rather than throw so a malformed pair does
// not crash the sort).
// ---------------------------------------------------------------------------

// `CompareType` (TypeSystemAstBuilder.cs line 886). Orders two types by their
// (reflection) name. The C# `a.FullName.CompareTo(b.FullName)` ports to
// `a.ReflectionName().compare(b.ReflectionName())` (the FullName-to-ReflectionName
// deviation).
inline int CompareType(const ::ILSpy::Decompiler::TypeSystem::IType& a,
                       const ::ILSpy::Decompiler::TypeSystem::IType& b) {
    return a.ReflectionName().compare(b.ReflectionName());
}

namespace Detail {
// A typed comparison for a comparable primitive: the C# `IComparable<T>.CompareTo`
// ordering (negative / zero / positive) via the built-in `<` / `>`.
template <typename T>
int ComparePrimitive(const std::any& a, const std::any& b) {
    const T av = std::any_cast<T>(a), bv = std::any_cast<T>(b);
    return av < bv ? -1 : (av > bv ? 1 : 0);
}
} // namespace Detail

// `CompareAny`: the C# `argA.Value is IComparable compA && argB.Value is
// IComparable compB ? compA.CompareTo(compB) : 0` port. Returns the C# `CompareTo`
// ordering for two same-type comparable boxed values, and 0 otherwise (an empty
// `std::any` = the C# `null`, mismatched types, or a non-comparable type).
inline int CompareAny(const std::any& a, const std::any& b) {
    if (!a.has_value() || !b.has_value())
        return 0;
    const auto& ta = a.type();
    const auto& tb = b.type();
    if (ta != tb)
        return 0;
    if (ta == typeid(bool)) {
        // `Boolean.CompareTo`: `true` is greater than `false`.
        const bool av = std::any_cast<bool>(a), bv = std::any_cast<bool>(b);
        return av == bv ? 0 : (av ? 1 : -1);
    }
    if (ta == typeid(char16_t))
        return Detail::ComparePrimitive<char16_t>(a, b);
    if (ta == typeid(std::int8_t))
        return Detail::ComparePrimitive<std::int8_t>(a, b);
    if (ta == typeid(std::uint8_t))
        return Detail::ComparePrimitive<std::uint8_t>(a, b);
    if (ta == typeid(std::int16_t))
        return Detail::ComparePrimitive<std::int16_t>(a, b);
    if (ta == typeid(std::uint16_t))
        return Detail::ComparePrimitive<std::uint16_t>(a, b);
    if (ta == typeid(std::int32_t))
        return Detail::ComparePrimitive<std::int32_t>(a, b);
    if (ta == typeid(std::uint32_t))
        return Detail::ComparePrimitive<std::uint32_t>(a, b);
    if (ta == typeid(std::int64_t))
        return Detail::ComparePrimitive<std::int64_t>(a, b);
    if (ta == typeid(std::uint64_t))
        return Detail::ComparePrimitive<std::uint64_t>(a, b);
    if (ta == typeid(float))
        return Detail::ComparePrimitive<float>(a, b);
    if (ta == typeid(double))
        return Detail::ComparePrimitive<double>(a, b);
    if (ta == typeid(std::string))
        return std::any_cast<std::string>(a).compare(std::any_cast<std::string>(b));
    if (ta == typeid(::ILSpy::Decompiler::TypeSystem::ITypePtr)) {
        // A boxed `System.Type` is `IComparable` by full name; the C++ box is an
        // `ITypePtr`, compared by `ReflectionName` (the FullName-to-ReflectionName
        // convention). A null `ITypePtr` (an undecoded type) yields 0.
        const auto av = std::any_cast<::ILSpy::Decompiler::TypeSystem::ITypePtr>(a);
        const auto bv = std::any_cast<::ILSpy::Decompiler::TypeSystem::ITypePtr>(b);
        if (!av || !bv)
            return 0;
        return av->ReflectionName().compare(bv->ReflectionName());
    }
    return 0;
}

// `CompareAttribute` (TypeSystemAstBuilder.cs line 835). Orders two attributes by
// their type, then by the decode-errors flag (an errored attribute sorts
// first), then by the positional (Fixed) and named argument lists. The
// argument lists compare element-wise: each fixed-argument position by
// argument type then by the boxed value (via `CompareAny`); each named argument
// by member name, type, then value. The C# `argA.Type`/`argB.Type` (non-null for
// a decoded argument) port to an unguarded deref of the `ITypePtr` (the D354
// `!`-to-unguarded-deref convention) -- the decoder overwrites the zero-fill
// sentinel before use, so the deref is safe by contract.
inline int CompareAttribute(const ::ILSpy::Decompiler::TypeSystem::IAttribute& a,
                            const ::ILSpy::Decompiler::TypeSystem::IAttribute& b) {
    namespace TS = ::ILSpy::Decompiler::TypeSystem;
    int result = CompareType(a.AttributeType(), b.AttributeType());
    if (result != 0)
        return result;
    if (a.HasDecodeErrors() && b.HasDecodeErrors())
        return 0;
    if (a.HasDecodeErrors())
        return -1;
    if (b.HasDecodeErrors())
        return 1;
    const auto fixedA = a.FixedArguments();
    const auto fixedB = b.FixedArguments();
    result = static_cast<int>(fixedA.size()) - static_cast<int>(fixedB.size());
    if (result != 0)
        return result;
    for (std::size_t i = 0; i < fixedA.size(); ++i) {
        const auto& argA = fixedA[i];
        const auto& argB = fixedB[i];
        result = CompareType(*argA.Type(), *argB.Type());
        if (result != 0)
            return result;
        result = CompareAny(argA.Value(), argB.Value());
        if (result != 0)
            return result;
    }
    const auto namedA = a.NamedArguments();
    const auto namedB = b.NamedArguments();
    result = static_cast<int>(namedA.size()) - static_cast<int>(namedB.size());
    if (result != 0)
        return result;
    for (std::size_t i = 0; i < namedA.size(); ++i) {
        const auto& argA = namedA[i];
        const auto& argB = namedB[i];
        result = argA.Name().compare(argB.Name());
        if (result != 0)
            return result;
        result = CompareType(*argA.Type(), *argB.Type());
        if (result != 0)
            return result;
        result = CompareAny(argA.Value(), argB.Value());
        if (result != 0)
            return result;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// MakeSimpleType / MakeMemberType (TypeSystemAstBuilder.cs lines 747 and 762),
// the local static name-to-AstType factories used throughout the ConvertType
// path (the top-level `MakeSimpleType(top.Name)` / the dotted `MakeMemberType(
// MakeSimpleType(top.Namespace), top.Name)` / the nested-type walk
// `MakeMemberType(type, fullTypeName.GetNestedTypeName(i))`, plus the
// constraint `MakeSimpleType(tp.Name)` and the type-parameter argument walk).
// The C# `static SimpleType MakeSimpleType(string name)` returns a `new
// SimpleType("@_")` when `name == "_"` (the C# 7 discard identifier `_` is a
// reserved token, so a type named `_` is emitted as the verbatim `@_` to keep it
// a valid identifier), else `new SimpleType(name)`; `MakeMemberType` is the
// `MemberType` twin taking the `AstType target` plus the member name.
//
// The C# reference-type return (`SimpleType`/`MemberType` are AST nodes,
// GC-owned) ports to a raw `new`-ed pointer (the D223 non-owning leak model, the
// `Identifier::Create` factory precedent): the caller attaches the returned
// node to the tree via `AddChild`/a slot setter (which re-parent but do not take
// ownership, faithful to the C# GC ownership). The C# `AstType target`
// (non-null reference) ports to `AstType*` (a non-null raw pointer, the callee
// assumes it is never null).
// ---------------------------------------------------------------------------

// `MakeSimpleType` (TypeSystemAstBuilder.cs line 747). Maps a type name to a
// `SimpleType`, substituting the verbatim `@_` for the reserved `_` discard.
inline SimpleType* MakeSimpleType(std::string name) {
    if (name == "_")
        return new SimpleType("@_");
    return new SimpleType(std::move(name));
}

// `MakeMemberType` (TypeSystemAstBuilder.cs line 762). Maps a `target.name`
// pair to a `MemberType`, substituting the verbatim `@_` for the reserved `_`
// discard in the member name.
inline MemberType* MakeMemberType(AstType* target, std::string name) {
    if (name == "_")
        return new MemberType(target, "@_");
    return new MemberType(target, std::move(name));
}

// ---------------------------------------------------------------------------
// CalculateHammingWeight (TypeSystemAstBuilder.cs line 1427). A local function
// inside `PrepareConstant` (itself a local function inside `ConvertEnumValue`)
// that computes the Hamming weight (population count) of a 64-bit value -- the
// number of set bits. The `[Flags]` enum decomposition prefers single-bit
// members directly (a value equal to a member whose weight is 1 is rendered as
// the member name alone), so each enum member's constant value is reduced to its
// weight to gate that path.
//
// The C# `ulong` is System.UInt64 (64-bit unsigned); the port uses std::uint64_t
// (NOT the 32-bit MSVC `unsigned long`). The bit-manipulation algorithm (the
// Wikipedia Hamming_weight bit-twiddling form, a SWAR popcount) is ported
// verbatim; the C# `unchecked` (wraparound arithmetic on the final cast) ports to
// the well-defined unsigned modular arithmetic of std::uint64_t and the
// truncating `static_cast<int>` of the high byte (identical two's-complement bit
// pattern to the C# `unchecked (int) ...`).
// ---------------------------------------------------------------------------
inline int CalculateHammingWeight(std::uint64_t value) noexcept {
    const std::uint64_t m1  = 0x5555555555555555; //binary: 0101...
    const std::uint64_t m2  = 0x3333333333333333; //binary: 00110011..
    const std::uint64_t m4  = 0x0f0f0f0f0f0f0f0f; //binary:  4 zeros,  4 ones ...
    const std::uint64_t h01 = 0x0101010101010101; //the sum of 256 to the power of 0,1,2,3...
    std::uint64_t x = value - ((value >> 1) & m1); //put count of each 2 bits into those 2 bits
    x = (x & m2) + ((x >> 2) & m2);               //put count of each 4 bits into those 4 bits
    x = (x + (x >> 4)) & m4;                      //put count of each 8 bits into those 8 bits
    return static_cast<int>((x * h01) >> 56);    //returns left 8 bits of x + (x<<8) + (x<<16) + ...
}

// ---------------------------------------------------------------------------
// TryGetSpecialConstant (TypeSystemAstBuilder.cs line 1252, the
// `specialConstants` static readonly Dictionary<object, (KnownTypeCode, string)>).
// The lookup half of the `IsSpecialConstant` instance method: a boxed BCL
// primitive value (the decoded constant value) maps to the (KnownTypeCode,
// member-name) pair of the BCL static field that renders it as a named reference
// rather than a literal (e.g. `byte.MaxValue` -> `(Byte, "MaxValue")`,
// `double.NaN` -> `(Double, "NaN")`). The full `IsSpecialConstant` method
// (which resolves the field via `compilation.FindType(info.Type).GetFields(...)`
// and constructs the AST reference) depends on the unported CSharpResolver /
// ITypeDefinition.GetFields / Expression-construction and is deferred; the pure
// lookup lands here ahead of it.
//
// The C# `Dictionary<object, ...>.TryGetValue` uses the boxed value type's
// value-based `Equals`/`GetHashCode` (a `byte.MaxValue` key matches any boxed
// `byte` 255, NOT a boxed `int` 255 -- the runtime type distinguishes them), so
// the faithful C++ port dispatches on `std::any::type()` (the typeid, the D462
// CompareAny precedent) to the matching BCL primitive arm and compares the
// unboxed value with `==`. A type not in the table (including the deferred
// `decimal` -- the C++ port has no decimal value type, the D432/D462
// no-decimal-arm convention) yields `std::nullopt`.
//
// The C# `float.NaN`/`double.NaN` are matched via `std::isnan` (NOT `==`, which
// is false for NaN); the infinities via `std::isinf` then the sign. The C#
// `float.MinValue`/`float.MaxValue`/`float.Epsilon` map to
// `-std::numeric_limits<float>::max()` / `max()` / `denorm_min()` respectively
// (C# `float.MinValue` is the most-negative finite, NOT `FLT_MIN` which is the
// smallest positive normal; C# `float.Epsilon` is the smallest positive
// subnormal = `denorm_min()`, NOT `FLT_EPSILON` which is the 1-to-next gap).
// Same for `double`.
// ---------------------------------------------------------------------------
inline std::optional<std::pair<::ILSpy::Decompiler::TypeSystem::KnownTypeCode, std::string>>
TryGetSpecialConstant(const std::any& constant) {
    namespace TS = ::ILSpy::Decompiler::TypeSystem;
    using KC = TS::KnownTypeCode;
    if (!constant.has_value())
        return std::nullopt;
    const auto& t = constant.type();
    if (t == typeid(std::uint8_t)) {
        const auto v = std::any_cast<std::uint8_t>(constant);
        if (v == std::numeric_limits<std::uint8_t>::max())
            return std::make_pair(KC::Byte, std::string("MaxValue"));
    } else if (t == typeid(std::int8_t)) {
        const auto v = std::any_cast<std::int8_t>(constant);
        if (v == std::numeric_limits<std::int8_t>::min())
            return std::make_pair(KC::SByte, std::string("MinValue"));
        if (v == std::numeric_limits<std::int8_t>::max())
            return std::make_pair(KC::SByte, std::string("MaxValue"));
    } else if (t == typeid(std::int16_t)) {
        const auto v = std::any_cast<std::int16_t>(constant);
        if (v == std::numeric_limits<std::int16_t>::min())
            return std::make_pair(KC::Int16, std::string("MinValue"));
        if (v == std::numeric_limits<std::int16_t>::max())
            return std::make_pair(KC::Int16, std::string("MaxValue"));
    } else if (t == typeid(std::uint16_t)) {
        const auto v = std::any_cast<std::uint16_t>(constant);
        if (v == std::numeric_limits<std::uint16_t>::max())
            return std::make_pair(KC::UInt16, std::string("MaxValue"));
    } else if (t == typeid(std::int32_t)) {
        const auto v = std::any_cast<std::int32_t>(constant);
        if (v == std::numeric_limits<std::int32_t>::min())
            return std::make_pair(KC::Int32, std::string("MinValue"));
        if (v == std::numeric_limits<std::int32_t>::max())
            return std::make_pair(KC::Int32, std::string("MaxValue"));
    } else if (t == typeid(std::uint32_t)) {
        const auto v = std::any_cast<std::uint32_t>(constant);
        if (v == std::numeric_limits<std::uint32_t>::max())
            return std::make_pair(KC::UInt32, std::string("MaxValue"));
    } else if (t == typeid(std::int64_t)) {
        const auto v = std::any_cast<std::int64_t>(constant);
        if (v == std::numeric_limits<std::int64_t>::min())
            return std::make_pair(KC::Int64, std::string("MinValue"));
        if (v == std::numeric_limits<std::int64_t>::max())
            return std::make_pair(KC::Int64, std::string("MaxValue"));
    } else if (t == typeid(std::uint64_t)) {
        const auto v = std::any_cast<std::uint64_t>(constant);
        if (v == std::numeric_limits<std::uint64_t>::max())
            return std::make_pair(KC::UInt64, std::string("MaxValue"));
    } else if (t == typeid(float)) {
        const auto v = std::any_cast<float>(constant);
        if (std::isnan(v))
            return std::make_pair(KC::Single, std::string("NaN"));
        if (std::isinf(v))
            return std::make_pair(KC::Single, v < 0 ? std::string("NegativeInfinity") : std::string("PositiveInfinity"));
        if (v == -std::numeric_limits<float>::max())
            return std::make_pair(KC::Single, std::string("MinValue"));
        if (v == std::numeric_limits<float>::max())
            return std::make_pair(KC::Single, std::string("MaxValue"));
        if (v == std::numeric_limits<float>::denorm_min())
            return std::make_pair(KC::Single, std::string("Epsilon"));
    } else if (t == typeid(double)) {
        const auto v = std::any_cast<double>(constant);
        if (std::isnan(v))
            return std::make_pair(KC::Double, std::string("NaN"));
        if (std::isinf(v))
            return std::make_pair(KC::Double, v < 0 ? std::string("NegativeInfinity") : std::string("PositiveInfinity"));
        if (v == -std::numeric_limits<double>::max())
            return std::make_pair(KC::Double, std::string("MinValue"));
        if (v == std::numeric_limits<double>::max())
            return std::make_pair(KC::Double, std::string("MaxValue"));
        if (v == std::numeric_limits<double>::denorm_min())
            return std::make_pair(KC::Double, std::string("Epsilon"));
    }
    // The C# `decimal.MinValue`/`decimal.MaxValue` entries are deferred: the C++
    // port has no decimal value type (the D432/D462 no-decimal-arm convention), so
    // a boxed decimal (or any other unmatched type/value) yields nullopt.
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// IsFlagsEnum (TypeSystemAstBuilder.cs line 1292). The `[Flags]` enum predicate
// that gates the entire flag-decomposition path in `ConvertEnumValue`: a
// `[Flags]` enum's combined values are rendered as a bitwise-OR of their
// single-bit members (e.g. `FileAccess.Read | FileAccess.Write`), while a
// non-flags enum's values are rendered as a single member reference or a plain
// numeric cast. The classification is by the presence of the `System.FlagsAttribute`
// attribute on the type definition (the D378 `KnownAttribute::Flags` kind).
//
// The C# source declares this as an INSTANCE method (`bool IsFlagsEnum(ITypeDefinition
// type)`), but it does NOT reference `this` or any instance field -- it is a pure
// delegation to `type.HasAttribute(KnownAttribute.Flags)`. The faithful free-function
// port therefore advances it ahead of the full instance method: the behavior is
// identical (no instance state is read), and it lands now that `ITypeDefinition`
// (D393), the inherited `IEntity.HasAttribute(KnownAttribute)` (D381), and
// `KnownAttribute::Flags` (D378) are all ported. This is the FIRST instance-method-
// shaped helper on TypeSystemAstBuilder ported as a free function (the D456-D465
// helpers were all `static` in the C# source); it is the smallest self-contained
// piece of the `ConvertEnumValue` surface not yet blocked by the unported
// `CSharpResolver` / `IField.GetConstantValue` / `Expression`-construction deps.
// ---------------------------------------------------------------------------
inline bool IsFlagsEnum(const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition& type) {
    return type.HasAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute::Flags);
}

// ---------------------------------------------------------------------------
// The TypeSystemAstBuilder CLASS skeleton (TypeSystemAstBuilder.cs lines 43-265):
// the `readonly CSharpResolver? resolver` field, the two public ctors, the private
// InitProperties, and the full `{ get; set; }` configuration property surface. This
// is the first slice of the class itself: every `Convert*` instance method reads
// these properties, so the configuration surface lands ahead of them, now that the
// CSharpResolver dependency chain is complete (the CSharpResolver class port
// finished with its CanTransformToExtensionMethodCall region).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `readonly CSharpResolver? resolver` field (a nullable reference the
//      resolver-less ctor leaves null) ports to an owning
//      `std::shared_ptr<const CSharpResolver>` (the C# GC-reference convention: the
//      builder must keep the resolver alive for its own lifetime). Every
//      CSharpResolver member the `Convert*` methods consume -- Compilation,
//      CurrentUsingScope, LookupSimpleNameOrTypeName, IsVariableReferenceWithSameType,
//      ResolveSimpleName -- is a const member, so the const-qualified handle supports
//      the full future consumer surface. CSharpResolver is only forward-declared (a
//      shared_ptr member over an incomplete type supports the skeleton's stores and
//      moves); the `Convert*` slices will include the full resolver header when they
//      first deref it.
//  (b) The C# `throw new ArgumentNullException(nameof(resolver))` ports to
//      `throw std::invalid_argument(...)` (the D424 base-ctor convention). The check
//      runs in the ctor body after the member-init-list move -- observationally
//      identical to the C# check-before-assign: a throw during construction never
//      completes the object in either language, and the moved-from handle is
//      destroyed as the exception unwinds.
//  (c) Each C# `{ get; set; }` auto-property ports to the const-getter +
//      mutable-lvalue-reference pair (the OverloadResolution input-property
//      convention): `bool X() const` reads, `bool& X()` assigns (the two overloads
//      share one doc comment). The `NameLookupMode` property's member function shares
//      the enum type's name (the D472 property-name-shares-enum-type collision: a
//      member function and an enclosing-namespace enum type cannot share a name in
//      the class body, since the function name hides the type), so the accessors and
//      the backing member use the NLM alias declared at the top of the class (which
//      the member function name does not hide).
//  (d) The C# class is unsealed (no C# subclass exists; CSharpAmbience /
//      CSharpDecompiler / ExpressionBuilder / IntroduceUsingDeclarations /
//      CSharpLanguage construct it), so the port is not final (pinned by a
//      static_assert in the test).
// ---------------------------------------------------------------------------
class TypeSystemAstBuilder {
public:
    // A type alias for the `NameLookupMode` enum -- the member function
    // `NameLookupMode()` (below) shadows the enum type name in the class scope
    // (the D472 property-name-shares-enum-type collision). This alias provides an
    // unshadowed reference for the property accessor declarations and the backing
    // member. The alias IS the fully-qualified enum type.
    using NLM = ::ILSpy::Decompiler::CSharp::Resolver::NameLookupMode;

    // The C# `public TypeSystemAstBuilder(CSharpResolver resolver)` (line 54) --
    // "a resolver initialized for the position where the type will be inserted".
    explicit TypeSystemAstBuilder(
        std::shared_ptr<const ::ILSpy::Decompiler::CSharp::Resolver::CSharpResolver> resolver)
        : resolver_(std::move(resolver))
    {
        if (!resolver_)
            throw std::invalid_argument("resolver");
        InitProperties();
    }

    // The C# `public TypeSystemAstBuilder()` (line 65) -- the resolver-less
    // construction: the name-disambiguation paths (ConvertType(FullTypeName) /
    // ConvertNamespace, which consult the resolver's using scopes and name
    // lookups) are skipped when the resolver is null; the plain `Convert*` paths
    // work without one.
    TypeSystemAstBuilder() { InitProperties(); }

    // -- The configuration property surface (C# lines 92-263) --

    // The C# `public bool AddTypeReferenceAnnotations { get; set; }` (line 92):
    // whether the ast builder should add annotations to type references (false).
    bool AddTypeReferenceAnnotations() const { return addTypeReferenceAnnotations_; }
    bool& AddTypeReferenceAnnotations() { return addTypeReferenceAnnotations_; }

    // The C# `public bool AddResolveResultAnnotations { get; set; }` (line 98):
    // whether the ast builder should add ResolveResult annotations to AST nodes
    // (false).
    bool AddResolveResultAnnotations() const { return addResolveResultAnnotations_; }
    bool& AddResolveResultAnnotations() { return addResolveResultAnnotations_; }

    // The C# `public bool ShowAccessibility { get; set; }` (line 104): whether
    // accessibility modifiers are shown (true).
    bool ShowAccessibility() const { return showAccessibility_; }
    bool& ShowAccessibility() { return showAccessibility_; }

    // The C# `public bool UsePrivateProtectedAccessibility { get; set; }` (line
    // 110): whether "private protected" accessibility modifiers are shown (true).
    bool UsePrivateProtectedAccessibility() const { return usePrivateProtectedAccessibility_; }
    bool& UsePrivateProtectedAccessibility() { return usePrivateProtectedAccessibility_; }

    // The C# `public bool ShowModifiers { get; set; }` (line 116): whether
    // non-accessibility modifiers are shown (true).
    bool ShowModifiers() const { return showModifiers_; }
    bool& ShowModifiers() { return showModifiers_; }

    // The C# `public bool ShowBaseTypes { get; set; }` (line 122): whether base
    // type references are shown (true).
    bool ShowBaseTypes() const { return showBaseTypes_; }
    bool& ShowBaseTypes() { return showBaseTypes_; }

    // The C# `public bool ShowTypeParameters { get; set; }` (line 128): whether
    // type parameter declarations are shown (true).
    bool ShowTypeParameters() const { return showTypeParameters_; }
    bool& ShowTypeParameters() { return showTypeParameters_; }

    // The C# `public bool ShowTypeParametersForUnboundTypes { get; set; }` (line
    // 134): whether type parameter names are shown for unbound types (false).
    bool ShowTypeParametersForUnboundTypes() const { return showTypeParametersForUnboundTypes_; }
    bool& ShowTypeParametersForUnboundTypes() { return showTypeParametersForUnboundTypes_; }

    // The C# `public bool ShowTypeParameterConstraints { get; set; }` (line 141):
    // whether constraints on type parameter declarations are shown; has no effect
    // if ShowTypeParameters is false (true).
    bool ShowTypeParameterConstraints() const { return showTypeParameterConstraints_; }
    bool& ShowTypeParameterConstraints() { return showTypeParameterConstraints_; }

    // The C# `public bool ShowParameterNames { get; set; }` (line 147): whether
    // the names of parameters are shown (true).
    bool ShowParameterNames() const { return showParameterNames_; }
    bool& ShowParameterNames() { return showParameterNames_; }

    // The C# `public bool ShowConstantValues { get; set; }` (line 153): whether
    // to show default values of optional parameters, and the values of constant
    // fields (true).
    bool ShowConstantValues() const { return showConstantValues_; }
    bool& ShowConstantValues() { return showConstantValues_; }

    // The C# `public bool ShowAttributes { get; set; }` (line 159): whether to
    // show attributes (false).
    bool ShowAttributes() const { return showAttributes_; }
    bool& ShowAttributes() { return showAttributes_; }

    // The C# `public bool SortAttributes { get; set; }` (line 165): whether to
    // sort attributes; if false, attributes are shown in metadata order (false).
    bool SortAttributes() const { return sortAttributes_; }
    bool& SortAttributes() { return sortAttributes_; }

    // The C# `public bool AlwaysUseShortTypeNames { get; set; }` (line 171):
    // whether to use fully-qualified type names or short type names (false).
    bool AlwaysUseShortTypeNames() const { return alwaysUseShortTypeNames_; }
    bool& AlwaysUseShortTypeNames() { return alwaysUseShortTypeNames_; }

    // The C# `public bool UseKeywordsForBuiltinTypes { get; set; }` (line 177):
    // whether to use keywords for builtin types (true).
    bool UseKeywordsForBuiltinTypes() const { return useKeywordsForBuiltinTypes_; }
    bool& UseKeywordsForBuiltinTypes() { return useKeywordsForBuiltinTypes_; }

    // The C# `public bool UseNullableSpecifierForValueTypes { get; set; }` (line
    // 183): whether to use `T?` or `Nullable<T>` for nullable value types (true).
    bool UseNullableSpecifierForValueTypes() const { return useNullableSpecifierForValueTypes_; }
    bool& UseNullableSpecifierForValueTypes() { return useNullableSpecifierForValueTypes_; }

    // The C# `public NameLookupMode NameLookupMode { get; set; }` (line 191): the
    // name lookup mode for converting a type name -- the default
    // `NameLookupMode.Expression` disambiguates the name for use in expression
    // context.
    NLM NameLookupMode() const { return nameLookupMode_; }
    NLM& NameLookupMode() { return nameLookupMode_; }

    // The C# `public bool GenerateBody { get; set; }` (line 197): whether to
    // generate a body that throws a System.NotImplementedException (false).
    bool GenerateBody() const { return generateBody_; }
    bool& GenerateBody() { return generateBody_; }

    // The C# `public bool UseCustomEvents { get; set; }` (line 203): whether to
    // generate custom events (false).
    bool UseCustomEvents() const { return useCustomEvents_; }
    bool& UseCustomEvents() { return useCustomEvents_; }

    // The C# `public bool ConvertUnboundTypeArguments { get; set; }` (line 209):
    // whether unbound type argument names are inserted in the ast or not
    // (false).
    bool ConvertUnboundTypeArguments() const { return convertUnboundTypeArguments_; }
    bool& ConvertUnboundTypeArguments() { return convertUnboundTypeArguments_; }

    // The C# `public bool UseAliases { get; set; }` (line 215): whether aliases
    // should be used inside the type name or not (true).
    bool UseAliases() const { return useAliases_; }
    bool& UseAliases() { return useAliases_; }

    // The C# `public bool UseSpecialConstants { get; set; }` (line 221): whether
    // constants like `int.MaxValue` are converted to a MemberReferenceExpression
    // or a PrimitiveExpression (true).
    bool UseSpecialConstants() const { return useSpecialConstants_; }
    bool& UseSpecialConstants() { return useSpecialConstants_; }

    // The C# `public bool PrintIntegralValuesAsHex { get; set; }` (line 227):
    // whether integral constants should be printed in hexadecimal format
    // (false).
    bool PrintIntegralValuesAsHex() const { return printIntegralValuesAsHex_; }
    bool& PrintIntegralValuesAsHex() { return printIntegralValuesAsHex_; }

    // The C# `public bool SupportInitAccessors { get; set; }` (line 233): whether
    // C# 9 "init;" accessors are supported; if disabled, emits "set /*init*/;"
    // instead (false).
    bool SupportInitAccessors() const { return supportInitAccessors_; }
    bool& SupportInitAccessors() { return supportInitAccessors_; }

    // The C# `public bool SupportRecordClasses { get; set; }` (line 238): whether
    // C# 9 "record" class types are supported (false).
    bool SupportRecordClasses() const { return supportRecordClasses_; }
    bool& SupportRecordClasses() { return supportRecordClasses_; }

    // The C# `public bool SupportRecordStructs { get; set; }` (line 243): whether
    // C# 10 "record" struct types are supported (false).
    bool SupportRecordStructs() const { return supportRecordStructs_; }
    bool& SupportRecordStructs() { return supportRecordStructs_; }

    // The C# `public bool SupportUnsignedRightShift { get; set; }` (line 248):
    // whether C# 11 "operator >>>" is supported (false).
    bool SupportUnsignedRightShift() const { return supportUnsignedRightShift_; }
    bool& SupportUnsignedRightShift() { return supportUnsignedRightShift_; }

    // The C# `public bool SupportOperatorChecked { get; set; }` (line 253):
    // whether C# 11 "operator checked" is supported (false).
    bool SupportOperatorChecked() const { return supportOperatorChecked_; }
    bool& SupportOperatorChecked() { return supportOperatorChecked_; }

    // The C# `public bool AlwaysUseGlobal { get; set; }` (line 258): whether all
    // fully qualified type names should be prefixed with "global::" (false).
    bool AlwaysUseGlobal() const { return alwaysUseGlobal_; }
    bool& AlwaysUseGlobal() { return alwaysUseGlobal_; }

    // The C# `public bool SupportExtensionDeclarations { get; set; }` (line 263):
    // whether C# 14 "extension" declarations are supported (false).
    bool SupportExtensionDeclarations() const { return supportExtensionDeclarations_; }
    bool& SupportExtensionDeclarations() { return supportExtensionDeclarations_; }

    // -- The Convert Type region (C# lines 266-768) --
    //
    // The C# private members (`ConvertTypeHelper` / `TypeMatches` / `TypeDefMatches` /
    // `AddTypeArguments` / `IsValidNamespace` / `AddTypeAnnotation` / the `Make*`
    // helpers) are widened to public for direct TDD ahead of the `ConvertAttribute` /
    // `ConvertConstantValue` / `ConvertParameter` / `ConvertEntity` consumer slices
    // (the CSharpResolver TryConvert-widening convention). Every method is `const`:
    // the region reads only the configuration properties and the (const) resolver.
    //
    // The C# `AstType` return ports to a raw `AstType*` (the D223 non-owning leak
    // model, the `Make*` builder precedent): the caller attaches the returned node to
    // the tree via a slot setter (which re-parents but does not take ownership).
    //
    // The `IType` parameters are NON-CONST references: the nullability-wrap arm calls
    // the non-const `IType::ChangeNullability` (which may `shared_from_this()`), and
    // `AddTypeAnnotation` recovers the owning `ITypePtr` handle via the non-const
    // `shared_from_this()` (the D529 convention -- every type fed to the region must
    // be shared-managed).

    // The C# `public AstType ConvertType(IType type)` (line 268) -- the public
    // type-to-syntax entry: `ConvertTypeHelper` then `AddTypeAnnotation`. The C#
    // null-check / ArgumentNullException is structurally unreachable through the
    // reference parameter (the D374 convention).
    AstType* ConvertType(TS::IType& type) const;

    // The C# `public AstType ConvertType(FullTypeName fullTypeName)` (line 283) -- the
    // unresolved-name entry: with a resolver, the first module whose type table
    // resolves the full name wins (`GetTypeDefinition(IModule, FullTypeName)`, the
    // TypeSystemExtensions extension) and the found definition converts through the
    // `IType` overload; without one (or when no module has the type), the name renders
    // structurally -- the top-level name as a `SimpleType` (or a `MemberType` under
    // its namespace), then one `MemberType` level per nesting level.
    AstType* ConvertType(const TS::FullTypeName& fullTypeName) const;

    // The C# `private AstType ConvertTypeHelper(IType type)` (line 313) -- the
    // type-shape dispatch: the `TypeWithElementType` shapes (pointer / array /
    // by-reference / the not-supported-in-C# modifier fallback that unwraps to the
    // element), the `NullabilityAnnotatedType` unwrap (+ `?` when the annotation is
    // `Nullable`), the `TupleType` element list, the `FunctionPointerType` signature
    // (calling conventions, custom `CallConv*` modifiers, parameters, return type,
    // and the treated-as arm), and the else-branch (the unbound-generic definition /
    // `UnknownType` shapes, the `ParameterizedType` (with the `Nullable<T>` -> `T?`
    // short-circuit), and the by-kind default (`dynamic`/`nint`/`nuint` as a
    // `PrimitiveType`, anything else as a `SimpleType`), each + the trailing `?` when
    // the type's nullability is `Nullable`).
    AstType* ConvertTypeHelper(TS::IType& type) const;

    // The C# `private AstType ConvertTypeHelper(IType genericType, IReadOnlyList<IType>
    // typeArguments)` (line 434) -- the named-type renderer over a generic type and
    // its type arguments: the builtin keyword short-circuit
    // (`KnownTypeReference.GetCSharpNameByTypeCode`), the using-alias lookup, the
    // short-name lookup through the resolver (`LookupSimpleNameOrTypeName` /
    // `IsVariableReferenceWithSameType` + `TypeMatches`), the
    // `AlwaysUseShortTypeNames` / definition-less top-level short-name arms, and the
    // qualified `MemberType` composition (the nested-type recursion over the
    // declaring type, or the namespace target via `ConvertNamespace` with the
    // `global::` double-colon form for the global namespace).
    AstType* ConvertTypeHelper(TS::IType& genericType,
                               const std::vector<TS::ITypePtr>& typeArguments) const;

    // The C# `private bool TypeMatches(IType type, ITypeDefinition typeDef,
    // IReadOnlyList<IType> typeArguments)` (line 596) -- whether `type` is the same
    // as `typeDef` parameterized with the given type arguments (the alias and
    // short-name lookups' verification). A non-parameterized `typeDef` (0 type
    // parameters) delegates to `TypeDefMatches`; otherwise the definition must match
    // and either every type argument is the `UnboundTypeArgument` placeholder (an
    // unbound generic) or the `ParameterizedType`'s arguments equal the given ones
    // element-wise.
    bool TypeMatches(const TS::IType& type, const TS::ITypeDefinition& typeDef,
                     const std::vector<TS::ITypePtr>& typeArguments) const;

    // The C# `private bool TypeDefMatches(ITypeDefinition typeDef, IType? type)`
    // (line 617) -- name / namespace / type-parameter-count equality with the
    // nesting-chain recursion (both nested or both top-level).
    bool TypeDefMatches(const TS::ITypeDefinition& typeDef, const TS::IType* type) const;

    // The C# `private void AddTypeArguments(AstType result, IReadOnlyList<ITypeParameter>
    // typeParameters, IReadOnlyList<IType> typeArguments, int startIndex, int endIndex)`
    // (line 636) -- appends `[start, end)` type arguments to a `SimpleType`/`MemberType`,
    // rendering an `UnboundTypeArgument` slot as the corresponding type PARAMETER's
    // name when `ConvertUnboundTypeArguments` is set.
    void AddTypeArguments(AstType& result,
                          const std::vector<const TS::ITypeParameter*>& typeParameters,
                          const std::vector<TS::ITypePtr>& typeArguments,
                          int startIndex, int endIndex) const;

    // The C# `public AstType ConvertNamespace(string namespaceName, out
    // NamespaceResolveResult? nrr)` (line 663) -- the namespace-reference renderer
    // (delegating to the private overload without the global prefix). The C# `out`
    // parameter ports to a `shared_ptr&` out-param reset to null at the top (the
    // `IsVariableReferenceWithSameType` convention).
    AstType* ConvertNamespace(
        const std::string& namespaceName,
        std::shared_ptr<Sem::NamespaceResolveResult>& nrr) const;

    // The C# `private AstType ConvertNamespace(string namespaceName, out
    // NamespaceResolveResult? nrr, bool requiresGlobalPrefix)` (line 668) -- the
    // recursive renderer: the using-alias lookup, then the last-dot split (a valid
    // single-part name renders as a `SimpleType` (with the `global::` prefix form
    // when required) or a `MemberType` over the invalid-name `global::` form; a
    // multi-part name recurses over the parent namespace).
    AstType* ConvertNamespace(const std::string& namespaceName,
                              std::shared_ptr<Sem::NamespaceResolveResult>& nrr,
                              bool requiresGlobalPrefix) const;

    // The C# `private bool IsValidNamespace(string firstNamespacePart, out
    // NamespaceResolveResult? nrr)` (line 733) -- whether the single namespace part
    // resolves (through the resolver's `ResolveSimpleName`) to that exact namespace;
    // without a resolver every namespace is assumed valid.
    bool IsValidNamespace(const std::string& firstNamespacePart,
                          std::shared_ptr<Sem::NamespaceResolveResult>& nrr) const;

    // -- The "Convert Constant Value" SUPPORT region (C# lines 1168-1249 +
    // 1496-1582) --
    //
    // The support helpers the mutually-recursive `ConvertConstantValue` /
    // `ConvertEnumValue` core (the CORE region below) consumes: the 3-arg
    // `ConvertConstantValue` dispatch calls `IsSpecialConstant` (before the
    // floating-point arm) and `ConvertFloatingPointLiteral` (for double/single
    // constants), and both the floating-point fraction arm and the deferred
    // Math.PI/E extraction call `MakeConstant`. The C# private members are
    // widened to public for direct TDD (the TryConvert convention); every
    // method is `const` (they read only the configuration properties, the
    // resolver, and ConvertType).
    //
    // The C# `out Expression? expression` ports to `Expression*&` (the rebindable
    // caller variable, reset to nullptr at the top, the IsVariableReferenceWithSameType
    // convention); the C# `object constant` ports to `const std::any&` (the D374
    // boxed-value convention); the returned/produced `Expression` ports to a raw
    // `new`-ed pointer (the D223 non-owning leak model, the ConvertType precedent).

    // The C# `bool IsSpecialConstant(IType expectedType, object constant,
    // [NotNullWhen(true)] out Expression? expression)` (line 1168) -- whether the
    // boxed constant is one of the BCL's named static fields (int.MaxValue,
    // double.NaN, ...; the `specialConstants` table, ported as the
    // `TryGetSpecialConstant` free function) and should render as a member
    // reference (`TypeReferenceExpression.MemberName`) instead of a literal. When
    // the field cannot be resolved and the constant is one of the three
    // non-encodable floating-point values (+Infty / -Infty / NaN), an equivalent
    // arithmetic expression (`-1.0 / 0.0` &c.) is produced instead.
    bool IsSpecialConstant(TS::IType& expectedType, const std::any& constant,
                           Expression*& expression) const;

    // The C# `Expression ConvertFloatingPointLiteral(IType type, object
    // constantValue)` (line 1499) -- renders a double/single constant: a whole
    // value or a short-decimal form as a plain `PrimitiveExpression`, a
    // long-decimal form as the exact fraction `num / den` (the
    // `FractionApprox` / `IsValidFraction` / `IsEqual` free functions) when one
    // exists within the max-denominator bound, else the plain
    // `PrimitiveExpression` fallback. The leading `CSharpPrimitiveCast.Cast`
    // coercion handles compilers that embed `0` (and possibly other values) as
    // `int` into constant value signatures even when the expected type is
    // float/double. The `UseSpecialConstants` Math.PI / MathE extraction
    // (`TryExtractExpression`, C# lines 1559-1723) is DEFERRED (see the
    // implementation) -- the C# itself falls through to the plain
    // `PrimitiveExpression` when the extraction yields null, so the deferral is
    // observationally identical for every value except a PI/E rational multiple
    // with a long decimal form.
    Expression* ConvertFloatingPointLiteral(TS::IType& type, const std::any& constantValue) const;

    // The C# `Expression MakeConstant(IType type, long c)` (line 1578) -- the
    // fraction-arm literal builder: the integral numerator/denominator boxed as
    // `long` and cast through the type's TypeCode with overflow CHECKING (an
    // out-of-range fraction term throws, the C# OverflowException).
    Expression* MakeConstant(TS::IType& type, std::int64_t c) const;

    // -- The "Convert Constant Value" CORE region (C# lines 998-1078 + 1306-1480)
    // --
    //
    // The mutually-recursive pair the support region above was landed ahead of:
    // the 3-arg `ConvertConstantValue` dispatch calls `IsSpecialConstant` (before
    // the floating-point arm) and `ConvertFloatingPointLiteral` (for double/single
    // constants) and routes enum-typed constants into `ConvertEnumValue`, whose
    // numeric fallback calls `ConvertConstantValue` back. The C# private
    // `ConvertEnumValue` is widened to public for direct TDD (the TryConvert
    // convention); every method is `const` (they read only the configuration
    // properties, the resolver, and the already-landed ConvertType/IsSpecialConstant/
    // ConvertFloatingPointLiteral surface).
    //
    // Port conventions: the C# `object?` constant value ports to `const std::any&`
    // (the D374 boxed-value convention; an empty `std::any` is the C# null); the
    // `ImmutableArray<CustomAttributeTypedArgument<IType>>` shape (a params-array
    // fixed argument of a custom attribute) ports to a `std::any` holding a
    // `std::vector<CustomAttributeTypedArgument>` (the CustomAttributeTypedArgument.hpp
    // array-case convention); every `Expression` ports to a raw `new`-ed pointer (the
    // D223 non-owning leak model, the ConvertType precedent); and every `IType`
    // parameter is non-const `TS::IType&` (the ConvertType/IsSpecialConstant
    // convention -- the callers const_cast the `NullableType.GetUnderlyingType`
    // const-reference results).

    // The C# `public Expression ConvertConstantValue(ResolveResult rr)` (line 998)
    // -- creates an Expression for the given resolve result: unpacks a
    // `ConversionResolveResult` (the boxing flag drives the small/native-integer
    // cast wrap), a `TypeOfResolveResult` (a `TypeOfExpression`), an
    // `ArrayCreateResolveResult` (an `ArrayCreateExpression` with the size
    // arguments and/or the initializer elements converted recursively), a
    // compile-time constant (delegating to the 2-arg overload, wrapping a boxed
    // small/native-integer literal in a cast), else the `ErrorExpression`. The C#
    // parameter is by value (a reference copy the body REBINDS through the
    // conversion unwrap), so the port takes the owning handle by value and rebinds
    // it locally. The C# `ArgumentNullException` on a null rr ports to
    // `std::invalid_argument` (the D424 convention).
    Expression* ConvertConstantValue(std::shared_ptr<Sem::ResolveResult> rr) const;

    // The C# `public Expression ConvertConstantValue(IType type, object? constantValue)`
    // (line 1073) -- the 2-arg convenience overload delegating to the 3-arg core with
    // `expectedType == type` (see the 3-arg doc).
    Expression* ConvertConstantValue(TS::IType& type, const std::any& constantValue) const;

    // The C# `public Expression ConvertConstantValue(IType expectedType, IType type,
    // object? constantValue)` (line 1081) -- the core constant renderer: a null
    // constant renders as a `NullReferenceExpression` (a reference/nullable/pointer
    // target) or a `DefaultValueExpression` (a value-type target); an `IType`-boxed
    // constant renders as a `TypeOfExpression`; a params-array
    // `CustomAttributeTypedArgument` vector renders as an `ArrayCreateExpression` with
    // the element types threaded; an enum-typed constant routes into
    // `ConvertEnumValue`; a double/single constant into `ConvertFloatingPointLiteral`;
    // a small/native-integer constant re-boxes through int32/uint32 (C# has no
    // small/native integer literals) with the literal type resolved through the
    // resolver-else-definition compilation; else the `PrimitiveExpression` (hex when
    // `PrintIntegralValuesAsHex`), wrapped in a `CastExpression` when the literal type
    // mismatches the expected type or the underlying kind is Unknown. The returned
    // expression is always implicitly convertible to `type` (the C# doc contract),
    // though not necessarily OF that type.
    Expression* ConvertConstantValue(TS::IType& expectedType, TS::IType& type,
                                     const std::any& constantValue) const;

    // The C# `internal Expression ConvertEnumValue(IType type, long val,
    // IField? declaringEnumMember = null)` (line 1306) -- converts a numeric enum
    // value into its enum member representation, if possible: an exact match renders
    // as the member reference (`E.Member`, or the unqualified `Member` inside an enum
    // member initializer -- the `declaringEnumMember` shape); a `[Flags]` enum's
    // combined values render as a bitwise-OR of their single-bit components (with
    // the complement `~X` form when it is smaller), the declared-later members
    // skipped (the metadata row-number ordering); else `(EnumType)value` (the plain
    // numeric value inside an enum member initializer). The metadata row number is
    // the low 24 bits of the raw `IEntity.MetadataToken` (the C#
    // `MetadataTokens.GetRowNumber`, the D381 raw-token convention).
    Expression* ConvertEnumValue(TS::IType& type, std::int64_t val,
                                 const TS::IField* declaringEnumMember = nullptr) const;

    // -- The "Convert Attribute" + "Convert Attribute Type" regions (C# lines
    // 770-988) --
    //
    // The attribute renderer, the next `Convert*` regions in C# source order and
    // the prerequisite the future `ConvertParameter` / `ConvertEntity` slices
    // consume (`decl.Attributes.AddRange(ConvertAttributes(...))`):
    // `ConvertAttributes` wraps every `ConvertAttribute` result in an
    // `AttributeSection` (with the optional target), `ConvertAttribute` renders
    // the `[...]` node (the attribute type through `ConvertAttributeType`, the
    // fixed and named arguments through the already-landed 3-arg
    // `ConvertConstantValue`), `ConvertAttributeType` strips the trailing
    // "Attribute" suffix when the short name is safe, and the two
    // `IsAttributeType` predicates test the `KnownTypeCode.Attribute`
    // derivation. The C# private members (`ApplyShortAttributeNameIfPossible` /
    // `IsAttributeType`) are widened to public for direct TDD (the TryConvert
    // convention); every method is `const` (they read only the configuration
    // properties, the resolver, and the already-landed Convert* surface).

    // The C# `public Attribute ConvertAttribute(IAttribute attribute)` (line 771)
    // -- creates the attribute node: the type through `ConvertAttributeType`
    // (with the trailing "Attribute" suffix stripped off the rendered
    // `SimpleType`/`MemberType` name), a `MemberResolveResult` annotation over the
    // attribute's constructor when `AddResolveResultAnnotations` is set, the
    // positional `FixedArguments` converted through the 3-arg `ConvertConstantValue`
    // (each argument's expected type threaded from the constructor's parameter
    // when the position has one, else the argument's own type), the
    // `NamedArguments` as `NamedExpression`s (with the
    // `MemberForNamedArgument`-resolved member annotated), and -- when the
    // decoder failed -- the `HasArgumentList` flag plus the trailing
    // "Could not decode attribute arguments." `ErrorExpression`. The C# `IType
    // AttributeType` (non-null) ports to `const TS::IAttribute&` (the reference
    // convention); the returned node ports to a raw `new`-ed pointer (the D223
    // non-owning leak model, the ConvertType precedent).
    Attribute* ConvertAttribute(const TS::IAttribute& attribute) const;

    // The C# `internal IEnumerable<AttributeSection> ConvertAttributes(
    // IEnumerable<IAttribute> attributes, string? target = null)` (line 824) --
    // renders each attribute into its own `AttributeSection` (with the target
    // written into the section's `AttributeTarget`), sorting the list through
    // `CompareAttribute` when `SortAttributes` is set (the C# lazy `IEnumerable`
    // ports to an eager vector, the GetAllBaseTypes convention; the C# `OrderBy`
    // is a stable sort, `std::stable_sort`). The C# nullable `string? target`
    // ports to `std::optional<std::string>` (the GetExtensionMethods convention);
    // the attribute list ports to non-owning pointers (the type system owns the
    // attributes, the caller holds raw handles).
    std::vector<AttributeSection*> ConvertAttributes(
        const std::vector<const TS::IAttribute*>& attributes,
        const std::optional<std::string>& target = std::nullopt) const;

    // The C# `public AstType ConvertAttributeType(IType type)` (line 894) -- the
    // attribute-type renderer: `ConvertTypeHelper` plus the short-name handling
    // (the trailing "Attribute" suffix removed when the name is longer than the
    // suffix). Under `AlwaysUseShortTypeNames` the short name replaces the
    // rendered identifier unconditionally (a null short name CLEARS the
    // `SimpleType` identifier, the C# `Identifier.CreateIfNotEmpty(null)` shape);
    // with a resolver, `ApplyShortAttributeNameIfPossible` decides per the
    // resolved environment. The C# `ArgumentNullException` on a null type is
    // structurally unreachable through the reference parameter (the D374
    // convention); the parameter is non-const `TS::IType&` because
    // `AddTypeAnnotation` recovers the owning handle through the non-const
    // `shared_from_this()` (the D529 convention).
    AstType* ConvertAttributeType(TS::IType& type) const;

    // The C# `private void ApplyShortAttributeNameIfPossible(IType type, AstType
    // astType, string? shortName)` (line 926) -- the resolver-driven short-name
    // decision: for a `SimpleType`, the short name is used when the short name is
    // unknown or resolves to a non-attribute type; a `@` verbatim prefix is added
    // when `name + "Attribute"` resolves to an attribute type (disabling the
    // implicit "Attribute" suffix). For a `MemberType`, the same decision over
    // the declaring type's nested types (a nested type reference) or over the
    // annotated namespace target (a namespace-qualified reference). The `string?`
    // ports to `std::optional<std::string>` (the GetExtensionMethods convention);
    // the C# `resolver!` deref is a caller contract (the method is reached only
    // through `ConvertAttributeType`'s `resolver != null` arm).
    void ApplyShortAttributeNameIfPossible(TS::IType& type, AstType& astType,
                                           const std::optional<std::string>& shortName) const;

    // The C# `private bool IsAttributeType(IType? type)` (line 979) -- whether the
    // type derives (through non-interface base types) from `System.Attribute`.
    // The C# nullable parameter ports to a nullable pointer.
    bool IsAttributeType(const TS::IType* type) const;

    // The C# `private bool IsAttributeType(ResolveResult rr)` (line 984) -- a
    // `TypeResolveResult` over an attribute type (the resolver lookup's result
    // shape).
    bool IsAttributeType(const Sem::ResolveResult& rr) const;

    // The C# `public ParameterDeclaration ConvertParameter(IParameter parameter)`
    // (line 1786) -- the parameter renderer: the reference kind into
    // `ParameterModifier`, the `IsParams` / `IsScopedRef` (the
    // `Lifetime().ScopedRef()` C# 11 annotation) flags, the `ShowAttributes`-gated
    // attribute sections, the declared type through `ConvertType` (a
    // by-reference parameter type unwrapped to its element first -- the C#
    // comment "avoid 'out ref'"), the `ShowParameterNames`-gated name, and the
    // `IsDefaultValueAssignmentAllowed` + `ShowConstantValues`-gated default
    // expression through the 2-arg `ConvertConstantValue` (the C# catch of the
    // metadata decoder's `BadImageFormatException` ports to a `std::exception`
    // catch with the `what()` message -- see the .cpp). The C#
    // `ArgumentNullException` on a null parameter is N/A (a reference cannot be
    // null, the D374 convention); the parameter is `const TS::IParameter&`
    // because every member read is const (the ConvertAttribute convention); the
    // returned node is a raw `new`-ed pointer (the D223 non-owning leak model,
    // the ConvertType precedent).
    ParameterDeclaration* ConvertParameter(const TS::IParameter& parameter) const;

    // The C# `internal TypeParameterDeclaration ConvertTypeParameter(ITypeParameter tp)`
    // (line 2602) -- the type-parameter declaration renderer: the variance into the
    // `Variance` scalar, the name, and the `ShowAttributes`-gated attribute sections
    // over `tp.GetAttributes()`. Widened to public for direct TDD ahead of the
    // `ConvertSymbol` / `ConvertExtension` / `ConvertEntity` consumer slices (the
    // `ConvertEnumValue` widening convention). The parameter is
    // `const TS::ITypeParameter&` because every member read is const (the
    // ConvertParameter convention); the returned node is a raw `new`-ed pointer
    // (the D223 non-owning leak model).
    TypeParameterDeclaration* ConvertTypeParameter(const TS::ITypeParameter& tp) const;

    // The C# `internal Constraint? ConvertTypeParameterConstraint(ITypeParameter tp)`
    // (line 2612) -- the `where T : ...` clause renderer: the no-constraint early
    // out (every flag false, no `notnull` nullability, and every direct base type
    // an object/valuetype -- yields nullptr, the C# null), then the `class` /
    // `class?` / `struct` / `unmanaged` / `notnull` keyword arms, the `TypeConstraints`
    // loop (a non-object/valuetype base type -- or one carrying attributes --
    // renders through `ConvertType`, the attributes wrapping the rendered type in a
    // `ComposedType`), the `new()` arm (skipped when a value-type constraint already
    // implies it), and the C# 11 `allows ref struct` arm. The object/valuetype
    // filter is the already-ported namespace-scope `IsObjectOrValueType` free
    // function (the gnhf-112 D460 landing at the top of this header -- the C#
    // private static re-homed as a free function ahead of this region). Widened
    // to public for direct TDD (the ConvertTypeParameter convention); the nullable
    // C# return ports to a nullable raw pointer (nullptr is the C# null).
    Constraint* ConvertTypeParameterConstraint(const TS::ITypeParameter& tp) const;

    // The C# `void AddNullabilityDisambiguatingConstraints(MethodDeclaration decl,
    // IMethod method)` (line 2686) -- the C# 8 nullability disambiguation for
    // overrides and explicit interface implementations: a `T?` in the re-emitted
    // signature means a nullable annotation (not `Nullable<T>`) only where the
    // type parameter carries a `class` / `default` constraint, so the method's
    // return type and every parameter type are visited for nullable-annotated
    // occurrences of the method's own type parameters (the
    // `NullableTypeParameterCollector` above), and each such parameter with a
    // disambiguator (the `GetNullabilityDisambiguator` free function at the top
    // of this header) gets a `where T : class` / `where T : default` clause
    // appended to `decl.Constraints`. The clause is built here rather than
    // through `ConvertTypeParameterConstraint` (which also prints `allows ref
    // struct` from the byreflike flag -- restating it is CS0460). Widened to
    // public for direct TDD ahead of the `ConvertEntity` consumer slice (the
    // `ConvertTypeParameter` convention); `decl` is non-const because the
    // constraints collection is mutated, and `method` is `const` because every
    // member read is const (the ConvertParameter convention). AcceptVisitor is
    // non-const (the D406 contract), so the const `ReturnType()`/`Type()`
    // accessors' results are const_cast for the visits (the D515/D517
    // convention).
    void AddNullabilityDisambiguatingConstraints(MethodDeclaration& decl,
                                                  const TS::IMethod& method) const;

    // The C# `public VariableDeclarationStatement ConvertVariable(IVariable v)`
    // (line 2744) -- the local-variable/const-field renderer: the `IsConst` flag
    // into `Modifiers.Const`, the type through `ConvertType`, and the
    // const-gated initializer through the 2-arg `ConvertConstantValue` over
    // `GetConstantValue(throwOnInvalidMetadata: true)` (the C# catch of the
    // metadata decoder's `BadImageFormatException` ports to a `std::exception`
    // catch rendering an `ErrorExpression` over the message -- the ConvertParameter
    // catch-arm convention). The parameter is `const TS::IVariable&` because every
    // member read is const; the returned node is a raw `new`-ed pointer (the D223
    // non-owning leak model).
    VariableDeclarationStatement* ConvertVariable(const TS::IVariable& v) const;

    // The C# `bool NeedsAccessibility(IMember member)` (line 2518) -- whether the
    // member's accessibility modifier should be rendered: explicit interface
    // implementations never carry one, static constructors don't, destructors
    // don't, interface-declared members only when not public, and local functions
    // don't (the CSharpDecompiler re-renders local functions with their own
    // accessibility). Reads no instance state (the C# private instance method
    // lifted unchanged, the `IsNullableTypeOrNonValueType` resolver precedent);
    // widened to public for direct TDD ahead of the `ConvertSymbol` /
    // `ConvertEntity` consumer slices (the `ConvertTypeParameter` convention).
    // The C# `declaringType?.Kind == TypeKind.Interface` null-conditional ports to
    // a null check on the `ITypePtr` (a null declaring type reads as
    // not-an-interface); the C# `member is not IMethod method ||
    // !method.IsLocalFunction` ports to a dynamic_cast + negated
    // `IsLocalFunction()`.
    bool NeedsAccessibility(const TS::IMember& member) const;

    // The C# `Modifiers GetMemberModifiers(IMember member)` (line 2538) -- the
    // member's modifier bits: the accessibility bits under
    // `ShowAccessibility && NeedsAccessibility` (the already-ported
    // `ModifierFromAccessibility` free function, gated by
    // `UsePrivateProtectedAccessibility`), then under `ShowModifiers` either the
    // local-function branch (the concrete `LocalFunctionMethod` RTTI match --
    // only `IsStaticLocalFunction` decides, the wrapper's unconditionally-true
    // `IsStatic` is deliberately NOT read) or the general branch (Static, the
    // Readonly bit for `ThisIsRefReadOnly` methods whose declaring-type
    // definition is NOT readonly, and the interface-vs-class spread of
    // Abstract / Virtual / Override / Sealed). The C#
    // `method.DeclaringTypeDefinition?.IsReadOnly == false` is the lifted-bool
    // `==` (true only for a definite false -- a null definition or a readonly
    // definition yields no bit). The second `declaringType.Kind ==
    // TypeKind.Interface` read derefs unconditionally in the C# (a real member
    // always has a declaring type); the port guards the degenerate
    // null-declaring-type stub shape, reading it as not-an-interface (the D516
    // safe-fallback convention). Widened to public for direct TDD (the
    // `NeedsAccessibility` convention).
    Modifiers GetMemberModifiers(const TS::IMember& member) const;

    // -- The "Convert Entity" accessor-support cluster (C# lines 2188-2297 +
    // 2771-2782) --
    //
    // The shared prerequisites of the "Convert Entity" region's member
    // renderers (the ConvertProperty / ConvertIndexer / ConvertEvent /
    // ConvertMethod / ConvertOperator / ConvertTypeDefinition slices that
    // follow): the body generator, the accessor renderer, the readonly-bit
    // hoist, and the explicit-interface-type helper.

    // The C# `BlockStatement? GenerateBodyBlock()` (line 2188) -- the
    // `throw new NotImplementedException();` body every `GenerateBody`-gated
    // renderer attaches (a single `ThrowStatement` over an
    // `ObjectCreateExpression` of `System.NotImplementedException`, the type
    // rendered through the `FullTypeName` overload with no resolver needed);
    // null when `GenerateBody` is false. Widened to public for direct TDD
    // ahead of the consumer slices (the `ConvertTypeParameter` convention).
    BlockStatement* GenerateBodyBlock() const;

    // The C# `Accessor? ConvertAccessor(IMethod? accessor,
    // MethodSemanticsAttributes kind, Accessibility ownerAccessibility, bool
    // addParameterAttribute)` (line 2206) -- the property / indexer / event
    // accessor renderer: the attribute sections (the accessor's own, its
    // return-type `[return: ...]`, and -- under `addParameterAttribute` -- the
    // last parameter's `[param: ...]`), the accessibility modifier only when
    // it differs from the owner's, the `readonly` modifier for
    // ref-readonly-this accessors on non-readonly declaring types, the
    // `MethodSemanticsAttributes` -> `AccessorKind` mapping (with the init
    // accessor upgrade under `SupportInitAccessors`), the trailing `/* init */`
    // comment for an init-only accessor that did NOT become an `init`
    // accessor, the `MemberResolveResult` annotation, and the generated body.
    // Null when `accessor` is null (the C# nullable parameter). The parameters
    // are a nullable non-owning `const TS::IMethod*` (the type system owns the
    // accessor method, the caller holds the raw handle) and pass-through
    // values; widened to public for direct TDD (the `ConvertTypeParameter`
    // convention).
    Accessor* ConvertAccessor(const TS::IMethod* accessor,
                               TS::MethodSemanticsAttributes kind,
                               TS::Accessibility ownerAccessibility,
                               bool addParameterAttribute) const;

    // The C# `static void MergeReadOnlyModifiers(EntityDeclaration decl,
    // Accessor? accessor1, Accessor? accessor2)` (line 2286) -- hoists the
    // `readonly` bit onto the declaration when it is carried by accessor1
    // alone (accessor2 is null) or by BOTH accessors (a single-sided bit stays
    // on the accessor: only an unambiguous both-sided `readonly` is a property
    // of the declared member). A null accessor1 returns immediately (nothing
    // to hoist); a null accessor2 with a non-readonly accessor1 also does
    // nothing. Static in the C# too (reads no instance state); widened to
    // public for direct TDD (the `NeedsAccessibility` convention).
    static void MergeReadOnlyModifiers(EntityDeclaration& decl,
                                        Accessor* accessor1,
                                        Accessor* accessor2);

    // The C# `AstType? GetExplicitInterfaceType(IMember member)` (line 2771) --
    // the `PrivateImplementationType` helper: for an explicit interface
    // implementation, the FIRST explicitly-implemented interface member's
    // declaring type rendered through `ConvertType`; null for an implicit
    // implementation or an implementation list with no entries (the C#
    // `FirstOrDefault` on the empty list). The returned node is a raw
    // `new`-ed pointer (the D223 non-owning model); widened to public for
    // direct TDD ahead of the consumer slices (the `ConvertTypeParameter`
    // convention).
    AstType* GetExplicitInterfaceType(const TS::IMember& member) const;

private:
    // The C# `private void AddTypeAnnotation(AstType astType, IType type)` (line 278)
    // -- attaches a `TypeResolveResult` annotation when `AddResolveResultAnnotations`
    // is set. The port recovers the owning `ITypePtr` handle via the non-const
    // `shared_from_this()` (the D529 convention).
    void AddTypeAnnotation(AstType& astType, TS::IType& type) const;

    // The C# `private static SimpleType MakeSimpleType(string name)` (line 746) --
    // the `_` identifier (a C# 9 discard) renders as the escaped `@_`.
    static SimpleType* MakeSimpleType(std::string_view name);

    // The C# `private SimpleType MakeGlobal()` (line 753) -- the `global` keyword
    // node, annotated with the compilation's root namespace under
    // `AddResolveResultAnnotations`.
    SimpleType* MakeGlobal() const;

    // The C# `private static MemberType MakeMemberType(AstType target, string name)`
    // (line 760) -- the nested-name helper with the same `_` -> `@_` escaping.
    static MemberType* MakeMemberType(AstType* target, std::string_view name);

    // The C# `void InitProperties()` (line 79) -- the non-false defaults every ctor
    // shares. Everything not assigned here keeps its `= false` backing initializer
    // (the C# bool field default), and `nameLookupMode_` keeps its `Expression`
    // initializer (the C# enum default 0).
    void InitProperties() {
        UseKeywordsForBuiltinTypes() = true;
        UseNullableSpecifierForValueTypes() = true;
        ShowAccessibility() = true;
        UsePrivateProtectedAccessibility() = true;
        ShowModifiers() = true;
        ShowBaseTypes() = true;
        ShowTypeParameters() = true;
        ShowTypeParameterConstraints() = true;
        ShowParameterNames() = true;
        ShowConstantValues() = true;
        UseAliases() = true;
        UseSpecialConstants() = true;
    }

    // The C# `readonly CSharpResolver? resolver` -- an owning handle (the C# GC
    // reference; the builder keeps the resolver alive for its lifetime). Null for
    // the resolver-less ctor. The CSharpResolver is an incomplete type here
    // (forward-declared above the namespace); a shared_ptr member supports stores
    // and moves over the incomplete type, and the `Convert*` slices will include the
    // full header when they first deref it.
    std::shared_ptr<const ::ILSpy::Decompiler::CSharp::Resolver::CSharpResolver> resolver_;

    // The C# auto-property backing fields (declaration order mirrors the C#
    // property order; every bool defaults to false, the C# bool field default --
    // InitProperties flips the twelve non-false defaults).
    bool addTypeReferenceAnnotations_ = false;
    bool addResolveResultAnnotations_ = false;
    bool showAccessibility_ = false;
    bool usePrivateProtectedAccessibility_ = false;
    bool showModifiers_ = false;
    bool showBaseTypes_ = false;
    bool showTypeParameters_ = false;
    bool showTypeParametersForUnboundTypes_ = false;
    bool showTypeParameterConstraints_ = false;
    bool showParameterNames_ = false;
    bool showConstantValues_ = false;
    bool showAttributes_ = false;
    bool sortAttributes_ = false;
    bool alwaysUseShortTypeNames_ = false;
    bool useKeywordsForBuiltinTypes_ = false;
    bool useNullableSpecifierForValueTypes_ = false;
    NLM nameLookupMode_ = NLM::Expression;
    bool generateBody_ = false;
    bool useCustomEvents_ = false;
    bool convertUnboundTypeArguments_ = false;
    bool useAliases_ = false;
    bool useSpecialConstants_ = false;
    bool printIntegralValuesAsHex_ = false;
    bool supportInitAccessors_ = false;
    bool supportRecordClasses_ = false;
    bool supportRecordStructs_ = false;
    bool supportUnsignedRightShift_ = false;
    bool supportOperatorChecked_ = false;
    bool alwaysUseGlobal_ = false;
    bool supportExtensionDeclarations_ = false;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax
