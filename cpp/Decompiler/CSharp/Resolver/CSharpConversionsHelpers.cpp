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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Out-of-line definitions of the `CSharpConversions` numeric-conversion helpers (see the header).
// The `implicitNumericConversionLookup` table is a file-local `constexpr` here (the C#
// `static readonly bool[,]` -- a single shared table, read-only after initialization).

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"

#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversions (the nullable-conversion singletons / EnumerationConversion factory)
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (IsCompileTimeConstant / Type / ConstantValue -- the constant-expression conversion)
#include "Decompiler/TypeSystem/ICompilation.hpp"   // ICompilation (FindType -- the array-to-System.Array arm)
#include "Decompiler/TypeSystem/IType.hpp"          // IType (Kind), ITypePtr, AcceptVisitor, Equals, ParameterizedType, ArrayType
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (KnownTypeCode -- the UnpackGenericArrayInterface definition arm)
#include "Decompiler/TypeSystem/ITypeParameter.hpp"  // ITypeParameter (Variance -- the IdentityOrVarianceConversion variance loop)
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"   // KnownTypeCode (the array-interface codes / Array)
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"  // NormalizeTypeVisitor::TypeErasure (IdentityConversion)
#include "Decompiler/TypeSystem/NullableType.hpp"   // IsNullable, GetUnderlyingType (the nullable helpers)
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"  // GetTypeCode, TypeCode
#include "Decompiler/TypeSystem/TypeKind.hpp"       // TypeKind
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetAllBaseTypes, IsKnownType (the IsSubtypeOf traversal)
#include "Decompiler/TypeSystem/VarianceModifier.hpp"  // VarianceModifier (the Covariant/Contravariant/Invariant switch)

#include <any>      // std::any (the ConstantValue unbox -- the D374/D424 `object?` model)
#include <cstdint>  // std::int32_t / std::int64_t (the boxed int/long the constant-expression conversion reads)
#include <memory>

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::GetAllBaseTypes;
using ILSpy::Decompiler::TypeSystem::GetTypeCode;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsAnyPointer;
using ILSpy::Decompiler::TypeSystem::IsKnownType;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::NormalizeTypeVisitor;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;

namespace {

// The C# `static readonly bool[,] implicitNumericConversionLookup` (CSharpConversions.cs line 377) --
// the implicit integral-to-integral conversion table (C# 9.0 spec section 10.2.3). Rows are the
// from-type (`Char`..`UInt64`, 9 rows), columns the to-type (`Int16`..`UInt64`, 6 cols). Indexed
// `[static_cast<int>(from) - Char][static_cast<int>(to) - Int16]`; the calling
// `ImplicitNumericConversion` guards the `from`/`to` ranges before indexing, so every access is
// in bounds.
constexpr bool implicitNumericConversionLookup[9][6] = {
    //       to:   short  ushort  int   uint   long   ulong
    // from:
    /* char   */ { false, true , true , true , true , true  },
    /* sbyte  */ { true , false, true , false, true , false },
    /* byte   */ { true , true , true , true , true , true  },
    /* short  */ { true , false, true , false, true , false },
    /* ushort */ { false, true , true , true , true , true  },
    /* int    */ { false, false, true , false, true , false },
    /* uint   */ { false, false, false, true , true , true  },
    /* long   */ { false, false, false, false, true , false },
    /* ulong  */ { false, false, false, false, false, true  },
};

} // namespace

bool IsNumericType(const IType& type)
{
	// The C# `switch (type.Kind) { case TypeKind.NInt: case TypeKind.NUInt: return true; }` -- the
	// native integers have no `KnownTypeCode`, so they are recognized via `Kind` before the
	// `GetTypeCode` range check.
	switch (type.Kind()) {
		case TypeKind::NInt:
		case TypeKind::NUInt:
			return true;
		default:
			break;
	}
	// The C# `TypeCode c = ReflectionHelper.GetTypeCode(type); return c >= TypeCode.Char && c <= TypeCode.Decimal;`
	// -- the `enum class` has no relational operators, so the ordinal comparison ports via
	// `static_cast<int>`.
	TypeCode c = GetTypeCode(type);
	return static_cast<int>(c) >= static_cast<int>(TypeCode::Char)
		&& static_cast<int>(c) <= static_cast<int>(TypeCode::Decimal);
}

bool AnyNumericConversion(const IType& fromType, const IType& toType)
{
	// The C# `return IsNumericType(fromType) && IsNumericType(toType);`
	return IsNumericType(fromType) && IsNumericType(toType);
}

bool ImplicitNumericConversion(const IType& fromType, const IType& toType)
{
	// C# 9.0 spec section 10.2.3.
	TypeCode from = GetTypeCode(fromType);
	if (from == TypeCode::Empty) {
		// When converting from a native-sized integer, treat it as 64-bits (the full range fits).
		switch (fromType.Kind()) {
			case TypeKind::NInt:
				from = TypeCode::Int64;
				break;
			case TypeKind::NUInt:
				from = TypeCode::UInt64;
				break;
			default:
				break;
		}
	}
	TypeCode to = GetTypeCode(toType);
	if (to == TypeCode::Empty) {
		// When converting to a native-sized integer, only 32-bits can be stored safely.
		switch (toType.Kind()) {
			case TypeKind::NInt:
				to = TypeCode::Int32;
				break;
			case TypeKind::NUInt:
				to = TypeCode::UInt32;
				break;
			default:
				break;
		}
	}
	const int fromI = static_cast<int>(from);
	const int toI = static_cast<int>(to);
	if (toI >= static_cast<int>(TypeCode::Single) && toI <= static_cast<int>(TypeCode::Decimal)) {
		// Conversions to float/double/decimal exist from all integral types, and there's a
		// conversion from float to double.
		return (fromI >= static_cast<int>(TypeCode::Char) && fromI <= static_cast<int>(TypeCode::UInt64))
			|| (from == TypeCode::Single && to == TypeCode::Double);
	}
	// Conversions to integral types: look at the table.
	return fromI >= static_cast<int>(TypeCode::Char) && fromI <= static_cast<int>(TypeCode::UInt64)
		&& toI >= static_cast<int>(TypeCode::Int16) && toI <= static_cast<int>(TypeCode::UInt64)
		&& implicitNumericConversionLookup[fromI - static_cast<int>(TypeCode::Char)]
		                                  [toI - static_cast<int>(TypeCode::Int16)];
}

bool ExplicitEnumerationConversion(const IType& fromType, const IType& toType)
{
	// C# spec (draft-v11): section 10.3.3 explicit enumeration conversions. The C# `type.Kind`
	// ports to `IType::Kind()` (the `enum class` has no implicit `bool`); the `IsNumericType` calls
	// dispatch to the sibling helper (Detail:: scope, same TU).
	if (fromType.Kind() == TypeKind::Enum) {
		return toType.Kind() == TypeKind::Enum || IsNumericType(toType);
	} else if (IsNumericType(fromType)) {
		return toType.Kind() == TypeKind::Enum;
	}
	return false;
}

bool IdentityConversion(IType& fromType, IType& toType)
{
	// C# spec (draft-v11): section 10.2.2 identity conversion. Erase both types through the
	// `NormalizeTypeVisitor.TypeErasure` singleton (folds object<->dynamic, IntPtr/UIntPtr<->nint/nuint,
	// nullability, custom modifiers, tuple-vs-ValueTuple -- but NOT type parameters), then compare.
	// The C# `fromType.AcceptVisitor(...)` / `toType.Equals(...)` reference semantics port to
	// `IType::AcceptVisitor` (non-const, the D406 convention) and `IType::Equals` (structural,
	// `Kind() == other.Kind() && StructuralEquals`).
	NormalizeTypeVisitor& erasure = NormalizeTypeVisitor::TypeErasure();
	ITypePtr from = fromType.AcceptVisitor(erasure);
	ITypePtr to = toType.AcceptVisitor(erasure);
	return from->Equals(*to);
}

std::shared_ptr<Conversion> ImplicitNullableConversion(IType& fromType, IType& toType)
{
	// C# 9.0 spec section 10.2.6. Acts ONLY when `toType` is nullable (there is no implicit nullable
	// conversion TO a non-nullable type). Strip both types to their underlying types -- `s` may or
	// may not be nullable (the C# comment) -- then an identity conversion on the underlying types is
	// the lifted identity, an implicit numeric conversion is the lifted numeric. The non-const
	// `GetUnderlyingType` overload returns `IType&` so the underlying types can feed the non-const
	// `IdentityConversion(IType&, IType&)`.
	if (IsNullable(toType)) {
		IType& t = GetUnderlyingType(toType);
		IType& s = GetUnderlyingType(fromType);
		if (IdentityConversion(s, t))
			return Conversions::ImplicitNullableConversion();
		if (ImplicitNumericConversion(s, t))
			return Conversions::ImplicitLiftedNumericConversion();
	}
	return Conversions::None();
}

std::shared_ptr<Conversion> ExplicitNullableConversion(IType& fromType, IType& toType)
{
	// C# spec (draft-v11) section 10.3.4. Acts when EITHER operand is nullable (unlike the implicit
	// variant, which requires the to-side). Strip both, then identity / any-numeric / explicit-
	// enumeration on the underlying types pick the lifted variant. The enumeration return is the
	// `Conversions::EnumerationConversion(false, true)` FACTORY (a fresh per-call instance, NOT a
	// singleton) -- explicit + lifted + enumeration.
	if (IsNullable(toType) || IsNullable(fromType)) {
		IType& t = GetUnderlyingType(toType);
		IType& s = GetUnderlyingType(fromType);
		if (IdentityConversion(s, t))
			return Conversions::ExplicitNullableConversion();
		if (AnyNumericConversion(s, t))
			return Conversions::ExplicitLiftedNumericConversion();
		if (ExplicitEnumerationConversion(s, t))
			return Conversions::EnumerationConversion(false, true);
	}
	return Conversions::None();
}

bool NullLiteralConversion(const IType& fromType, const IType& toType)
{
	// C# 9.0 spec section 10.2.7. The null literal (`TypeKind.Null`) converts to any nullable type
	// or any reference type. The C# `toType.IsReferenceType == true` is the `bool? == true` check,
	// true ONLY when `IsReferenceType` holds `true` (not `std::nullopt`, not `false`), so an
	// indeterminate reference-ness does NOT accept the null literal.
	if (fromType.Kind() == TypeKind::Null) {
		return IsNullable(toType)
			|| (toType.IsReferenceType().has_value() && *toType.IsReferenceType() == true);
	}
	return false;
}

const IType* UnpackGenericArrayInterface(const IType& interfaceType)
{
	// C# `if (interfaceType is ParameterizedType pt) { switch (pt.GetDefinition()?.KnownTypeCode) {
	// case IListOfT: ...: return pt.GetTypeArgument(0); } } return null;`. The `is ParameterizedType`
	// ports to `dynamic_cast<const ParameterizedType*>(&interfaceType)`; the `?.` ports to a
	// `def != nullptr` guard before `def->KnownTypeCode()` (a degenerate `ParameterizedType` whose
	// generic has no definition yields `nullptr`, faithfully -- the C# `?.` skips the switch).
	// `GetTypeArgument(0)` returns `ITypePtr` by value (a `shared_ptr` copy); the managed `IType`
	// is owned by `pt`'s `typeArgs_`, so the returned raw pointer outlives the call (the caller
	// may dereference it without keeping the temporary `shared_ptr` alive).
	if (const ParameterizedType* pt = dynamic_cast<const ParameterizedType*>(&interfaceType)) {
		const ITypeDefinition* def = pt->GetDefinition();
		if (def != nullptr) {
			switch (def->KnownTypeCode()) {
				case KnownTypeCode::IListOfT:
				case KnownTypeCode::ICollectionOfT:
				case KnownTypeCode::IEnumerableOfT:
				case KnownTypeCode::IReadOnlyListOfT:
				case KnownTypeCode::IReadOnlyCollectionOfT:
					return pt->GetTypeArgument(0).get();
			}
		}
	}
	return nullptr;
}

bool IsImplicitReferenceConversion(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// C# `return ImplicitReferenceConversion(fromType, toType, 0);` -- the public entry delegates to
	// the recursive worker with a zero nesting depth.
	return ImplicitReferenceConversion(compilation, fromType, toType, 0);
}

bool ImplicitReferenceConversion(const ICompilation& compilation, IType& fromType, IType& toType,
                                  int subtypeCheckNestingDepth)
{
	// C# 9.0 spec section 10.2.8. The C# `fromType.IsReferenceType == true && toType.IsReferenceType != false`
	// guard: both lifted-bool comparisons reduce to "the operand holds `true`" (a `bool?` lifted `==`/`!=`
	// yields `null` when either operand is `null`, falsy in a boolean context), so an indeterminate
	// `IsReferenceType` (std::nullopt) fails the guard on EITHER side. The negation: if the guard fails,
	// return false (no reference conversion when the types are not both known reference types).
	auto fromRef = fromType.IsReferenceType();
	auto toRef = toType.IsReferenceType();
	if (!(fromRef.has_value() && *fromRef == true && toRef.has_value() && *toRef != false))
		return false;

	// The C# `ArrayType fromArray = fromType as ArrayType;` -- `as` ports to `dynamic_cast` (a non-const
	// `ArrayType*` since `fromType` is `IType&` non-const, the recursion's element feeds the non-const
	// `IdentityConversion`). `ArrayType::Element()` is the C# `ElementType`; `Rank()` is `Dimensions`.
	ArrayType* fromArray = dynamic_cast<ArrayType*>(&fromType);
	if (fromArray != nullptr) {
		ArrayType* toArray = dynamic_cast<ArrayType*>(&toType);
		if (toArray != nullptr) {
			// Array covariance (the broken kind): same dimension count + a recursive reference conversion
			// on the element types. The C# `fromArray.Dimensions == toArray.Dimensions` ports to `Rank()`.
			return fromArray->Rank() == toArray->Rank()
				&& ImplicitReferenceConversion(compilation, *fromArray->Element(), *toArray->Element(),
				                              subtypeCheckNestingDepth);
		}
		// Conversion from a single-dimensional array `S[]` to `IList<T>`/`IReadOnlyList<T>` + base
		// interfaces: an identity or recursive reference conversion from the element to the unpacked
		// type argument. `UnpackGenericArrayInterface` returns a non-owning `const IType*` (owned by the
		// `ParameterizedType`'s `typeArgs_`); the `const_cast` feeds it to the non-const `IdentityConversion` /
		// `ImplicitReferenceConversion` (the D515 `GetUnderlyingType` non-const-overload precedent -- the
		// underlying object is the mutable, type-system-owned `IType`).
		const IType* toTypeArgument = UnpackGenericArrayInterface(toType);
		if (fromArray->Rank() == 1 && toTypeArgument != nullptr) {
			IType& toArg = const_cast<IType&>(*toTypeArgument);
			return IdentityConversion(*fromArray->Element(), toArg)
				|| ImplicitReferenceConversion(compilation, *fromArray->Element(), toArg,
				                              subtypeCheckNestingDepth);
		}
		// Conversion from any array to `System.Array` and the interfaces it implements: recurse with
		// `compilation.FindType(KnownTypeCode.Array)` as the from-side. `FindType` returns `const IType&`
		// (the non-null convention); the `const_cast` feeds it to the non-const recursion (the underlying
		// known type is the mutable, compilation-owned `IType`).
		const IType& systemArray = compilation.FindType(KnownTypeCode::Array);
		IType& systemArrayMut = const_cast<IType&>(systemArray);
		return ImplicitReferenceConversion(compilation, systemArrayMut, toType, subtypeCheckNestingDepth);
	}

	// Now comes the hard part: traverse the inheritance chain and figure out generics + variance.
	return IsSubtypeOf(compilation, fromType, toType, subtypeCheckNestingDepth);
}

bool IsSubtypeOf(const ICompilation& compilation, IType& s, IType& t, int subtypeCheckNestingDepth)
{
	// Conversion to `dynamic` + `object` is always possible. The C# `t.Kind == TypeKind.Dynamic` ports
	// to `t.Kind() == TypeKind::Dynamic`; the C# `t.IsKnownType(KnownTypeCode.Object)` is the free
	// `IsKnownType(t, KnownTypeCode::Object)` (a null definition yields false).
	if (t.Kind() == TypeKind::Dynamic || IsKnownType(t, KnownTypeCode::Object))
		return true;
	if (subtypeCheckNestingDepth > 10) {
		// Subtyping in C# is undecidable (Kennedy & Pierce, "On Decidability of Nominal Subtyping with
		// Variance"); bound the variance-recursion depth to prevent infinite recursion. No real C# code
		// uses generics nested more than 10 levels deep, and most such nestings do not involve variance.
		return false;
	}
	// Let `GetAllBaseTypes` do the work: the free function returns `const IType*` (non-owning, the
	// type-system-owned base types); the `const_cast` feeds each to the non-const
	// `IdentityOrVarianceConversion` (the recursion's `IdentityConversion` takes non-const `IType&`).
	for (const IType* baseType : GetAllBaseTypes(&s)) {
		if (IdentityOrVarianceConversion(compilation, const_cast<IType&>(*baseType), t,
		                                  subtypeCheckNestingDepth + 1))
			return true;
	}
	return false;
}

bool IdentityOrVarianceConversion(const ICompilation& compilation, IType& s, IType& t,
                                  int subtypeCheckNestingDepth)
{
	// The C# `ITypeDefinition def = s.GetDefinition();` -- the non-owning `const ITypeDefinition*`
	// (a null definition for types without one: type parameters, arrays, `ParameterizedType` over a
	// `KnownType` which is not an `ITypeDefinition`).
	const ITypeDefinition* def = s.GetDefinition();
	if (def != nullptr) {
		// The C# `def.Equals(t.GetDefinition())` -- `IType::Equals` is structural (`Kind() == other.Kind()
		// && StructuralEquals`); `t.GetDefinition()` may be null, and the C# `def.Equals(null)` returns
		// false (the `obj is IType` guard in `IType.Equals(object)`), so the guard `tDef != nullptr &&
		// def->Equals(*tDef)` faithfully returns false when `t` has no matching definition.
		const ITypeDefinition* tDef = t.GetDefinition();
		if (!(tDef != nullptr && def->Equals(*tDef)))
			return false;
		// The C# `ParameterizedType ps = s as ParameterizedType;` / `pt = t as ParameterizedType;` --
		// non-const `ParameterizedType*` (the `GetTypeArgument` results feed the non-const recursion).
		ParameterizedType* ps = dynamic_cast<ParameterizedType*>(&s);
		ParameterizedType* pt = dynamic_cast<ParameterizedType*>(&t);
		if (ps != nullptr && pt != nullptr) {
			// C# spec (draft-v11) section 19.2.3.3 variance conversion. For each type parameter, the type
			// arguments must match by identity (continue) or by a variance-direction reference conversion.
			// The C# loop indexes `def.TypeParameters[i]` and `ps.GetTypeArgument(i)` by the SAME position `i`
			// (not `xi.Index`); the port mirrors that with an indexed loop over `def->TypeParameters()`.
			std::vector<const ITypeParameter*> typeParams = def->TypeParameters();
			for (std::size_t i = 0; i < typeParams.size(); i++) {
				const ITypeParameter* xi = typeParams[i];
				IType& si = *ps->GetTypeArgument(static_cast<int>(i));
				IType& ti = *pt->GetTypeArgument(static_cast<int>(i));
				if (IdentityConversion(si, ti))
					continue;
				switch (xi->Variance()) {
					case VarianceModifier::Covariant:
						if (!ImplicitReferenceConversion(compilation, si, ti, subtypeCheckNestingDepth))
							return false;
						break;
					case VarianceModifier::Contravariant:
						if (!ImplicitReferenceConversion(compilation, ti, si, subtypeCheckNestingDepth))
							return false;
						break;
					default: // `Invariant` -- no variance conversion is possible
						return false;
				}
			}
		}
		else if (ps != nullptr || pt != nullptr) {
			// Only one is parameterized (or the counts do not match) -> not a valid conversion.
			return false;
		}
		return true;
	}
	else {
		// Not type definitions: still check for equal types (e.g. `s` and `t` might be type parameters).
		// `IType::Equals` is structural (`Kind() == other.Kind() && StructuralEquals`).
		return s.Equals(t);
	}
}

bool IsSealedReferenceType(const IType& type)
{
	// C# `TypeKind kind = type.Kind; return kind == TypeKind.Class && type.GetDefinition().IsSealed
	// || kind == TypeKind.Delegate;`. The C# short-circuits the `GetDefinition().IsSealed` deref behind
	// the `kind == Class` test (a delegate returns true without ever calling `GetDefinition`). The port
	// adds a `def != nullptr` guard before `def->IsSealed()` for the class arm: a class-kind type whose
	// definition is unresolved (e.g. a `KnownType` placeholder) would NRE in the C#, but in C++ a null
	// deref is UB, so the guard returns false (not-sealed) -- the safe faithful fallback. The C# `||` is
	// the last evaluated, so the precedence mirrors the C#: `(Class && IsSealed) || Delegate`.
	TypeKind kind = type.Kind();
	const ITypeDefinition* def = type.GetDefinition();
	return (kind == TypeKind::Class && def != nullptr && def->IsSealed())
		|| kind == TypeKind::Delegate;
}

bool ExplicitReferenceConversion(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// C# spec (draft-v11) section 10.3.5 explicit reference conversions. The C# `toType.IsReferenceType != true`
	// / `fromType.IsReferenceType != true` guards are the `bool? != true` check, true UNLESS `IsReferenceType`
	// holds `true` (a `bool? != true` yields `null` [falsy] when the operand is `null`, and `false` when it is
	// `true`), so an indeterminate `IsReferenceType` (std::nullopt) fails the guard. The negation: the guard
	// returns false unless the operand holds `true`.
	auto toRef = toType.IsReferenceType();
	if (!(toRef.has_value() && *toRef == true))
		return false;
	auto fromRef = fromType.IsReferenceType();
	if (!(fromRef.has_value() && *fromRef == true)) {
		// Special case: converting from `F` to `T` is a reference conversion where `T : class, F` (because
		// `F` actually must be a reference type as well, even though C# doesn't treat it as one). The C#
		// `IsSubtypeOf(toType, fromType, 0)` -- note the SWAPPED order (`toType` is the subtype candidate
		// against `fromType`); the depth starts at 0 (the depth guard lives inside `IsSubtypeOf`).
		if (fromType.Kind() == TypeKind::TypeParameter)
			return IsSubtypeOf(compilation, toType, fromType, 0);
		return false;
	}

	if (toType.Kind() == TypeKind::Array) {
		// The C# `(ArrayType)toType` ports to `dynamic_cast` (non-const, the element feeds the non-const
		// recursion); the cast cannot fail (the `Kind == Array` guard), but a `dynamic_cast` is the faithful
		// shape and cheap.
		ArrayType* toArray = dynamic_cast<ArrayType*>(&toType);
		if (fromType.Kind() == TypeKind::Array) {
			// Array covariance: same dimensions + a recursive explicit reference conversion on the elements.
			ArrayType* fromArray = dynamic_cast<ArrayType*>(&fromType);
			if (fromArray->Rank() != toArray->Rank())
				return false;
			return ExplicitReferenceConversion(compilation, *fromArray->Element(), *toArray->Element());
		}
		// The C# `IType fromTypeArgument = UnpackGenericArrayInterface(fromType);` -- the non-owning `const IType*`
		// (owned by the `ParameterizedType`'s `typeArgs_`); the `const_cast` feeds it to the non-const
		// recursion (the D515 `GetUnderlyingType` non-const-overload precedent -- the underlying object is
		// the mutable, type-system-owned `IType`).
		const IType* fromTypeArgument = UnpackGenericArrayInterface(fromType);
		if (fromTypeArgument != nullptr && toArray->Rank() == 1) {
			IType& fromArg = const_cast<IType&>(*fromTypeArgument);
			return ExplicitReferenceConversion(compilation, fromArg, *toArray->Element())
				|| IdentityConversion(fromArg, *toArray->Element());
		}
		// Otherwise treat the array like a sealed class -- require an implicit conversion in the OPPOSITE
		// direction (the C# `IsImplicitReferenceConversion(toType, fromType)` -- swapped order).
		return IsImplicitReferenceConversion(compilation, toType, fromType);
	}
	else if (fromType.Kind() == TypeKind::Array) {
		ArrayType* fromArray = dynamic_cast<ArrayType*>(&fromType);
		const IType* toTypeArgument = UnpackGenericArrayInterface(toType);
		if (toTypeArgument != nullptr && fromArray->Rank() == 1) {
			IType& toArg = const_cast<IType&>(*toTypeArgument);
			return ExplicitReferenceConversion(compilation, *fromArray->Element(), toArg);
		}
		// Otherwise treat the array like a sealed class.
		return IsImplicitReferenceConversion(compilation, fromType, toType);
	}
	else if (fromType.Kind() == TypeKind::Delegate && toType.Kind() == TypeKind::Delegate) {
		// The C# `ITypeDefinition def = fromType.GetDefinition(); if (def == null || !def.Equals(toType.GetDefinition()))
		// return false;`. The `def->Equals(*tDef)` is the structural `IType::Equals`; `tDef` may be null, and the
		// C# `def.Equals(null)` returns false, so the guard `tDef != nullptr && def->Equals(*tDef)` is the
		// D516 null-guarded form.
		const ITypeDefinition* def = fromType.GetDefinition();
		const ITypeDefinition* tDef = toType.GetDefinition();
		if (def == nullptr || !(tDef != nullptr && def->Equals(*tDef)))
				return false;
		ParameterizedType* ps = dynamic_cast<ParameterizedType*>(&fromType);
		ParameterizedType* pt = dynamic_cast<ParameterizedType*>(&toType);
		if (ps == nullptr || pt == nullptr) {
			// Non-generic delegate -- return true for the identity conversion (both sides non-parameterized).
			return ps == nullptr && pt == nullptr;
		}
		// The C# loop indexes `def.TypeParameters[i]` and `ps.GetTypeArgument(i)` by the SAME position `i`
		// (the loop counter, NOT `xi.Index`); the port mirrors that with an indexed loop (the D517 precedent).
		std::vector<const ITypeParameter*> typeParams = def->TypeParameters();
		for (std::size_t i = 0; i < typeParams.size(); i++) {
			const ITypeParameter* xi = typeParams[i];
			IType& si = *ps->GetTypeArgument(static_cast<int>(i));
			IType& ti = *pt->GetTypeArgument(static_cast<int>(i));
			if (IdentityConversion(si, ti))
				continue;
			switch (xi->Variance()) {
				case VarianceModifier::Covariant:
					if (!ExplicitReferenceConversion(compilation, si, ti))
						return false;
					break;
				case VarianceModifier::Contravariant:
					// The C# `!(si.IsReferenceType == true && ti.IsReferenceType == true)` -- both `bool? == true`
					// checks (true ONLY when the operand holds `true`); an indeterminate on either side fails.
					if (!(si.IsReferenceType().has_value() && *si.IsReferenceType() == true
					      && ti.IsReferenceType().has_value() && *ti.IsReferenceType() == true))
						return false;
					break;
				default: // `Invariant` -- no variance conversion is possible
					return false;
			}
		}
		return true;
	}
	else if (IsSealedReferenceType(fromType)) {
		// If the source type is sealed, explicit conversions can't do anything more than implicit ones.
		return IsImplicitReferenceConversion(compilation, fromType, toType);
	}
	else if (IsSealedReferenceType(toType)) {
		// If the target type is sealed, there must be an implicit conversion in the OPPOSITE direction.
		return IsImplicitReferenceConversion(compilation, toType, fromType);
	}
	else {
		// Unsealed on both sides: an interface on either side is always explicitly convertible; otherwise
		// an implicit reference conversion in EITHER direction suffices.
		if (fromType.Kind() == TypeKind::Interface || toType.Kind() == TypeKind::Interface)
			return true;
		else
			return IsImplicitReferenceConversion(compilation, toType, fromType)
				|| IsImplicitReferenceConversion(compilation, fromType, toType);
	}
}

bool IsBoxingConversion(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// C# 9.0 spec section 10.2.9. Strip the nullable wrapper from the from-side first (a `Nullable<T>`
	// boxes as its underlying `T`). The C# `fromType.IsReferenceType == false` is the `bool? == false`
	// check (true ONLY when `IsReferenceType` holds `false`, not `std::nullopt`); the `toType.IsReferenceType
	// == true` is the `bool? == true` check (true ONLY when `IsReferenceType` holds `true`); the
	// `!fromType.IsByRefLike` excludes by-ref-like types (ref structs cannot be boxed). With the guard
	// satisfied, the boxing conversion is a subtype relation: the value type is a subtype of the
	// reference type it implements (e.g. `int` -> `object`). The non-const `GetUnderlyingType` overload
	// returns `IType&` so the stripped type can feed the non-const `IsSubtypeOf`.
	IType& from = GetUnderlyingType(fromType);
	auto fromRef = from.IsReferenceType();
	auto toRef = toType.IsReferenceType();
	if (fromRef.has_value() && *fromRef == false && !from.IsByRefLike()
		&& toRef.has_value() && *toRef == true)
		return IsSubtypeOf(compilation, from, toType, 0);
	return false;
}

bool UnboxingConversion(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// C# spec (draft-v11) section 10.3.7. Strip the nullable wrapper from the TO-side first (unboxing
	// to a `Nullable<T>` unboxes the underlying `T`). The C# `fromType.IsReferenceType == true` is the
	// `bool? == true` check; the `toType.IsReferenceType == false` is the `bool? == false` check. With
	// the guard satisfied, the unboxing conversion is a subtype relation with the arguments SWAPPED
	// (`IsSubtypeOf(toType, fromType, 0)` -- the value type is a subtype of the boxed reference type).
	IType& to = GetUnderlyingType(toType);
	auto fromRef = fromType.IsReferenceType();
	auto toRef = to.IsReferenceType();
	if (fromRef.has_value() && *fromRef == true && toRef.has_value() && *toRef == false)
		return IsSubtypeOf(compilation, to, fromType, 0);
	return false;
}

bool ImplicitTypeParameterConversion(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// C# 9.0 spec section 10.2.12. Only a type parameter (`Kind == TypeParameter`) reaches the
	// `IsSubtypeOf` arm; a type parameter whose `IsReferenceType` has a definite value (`true` or
	// `false`) is already handled by `ImplicitReferenceConversion` / `IsBoxingConversion`, so only an
	// INDETERMINATE (`std::nullopt`) type parameter proceeds. The C# `fromType.IsReferenceType.HasValue`
	// is the `bool?.HasValue` check (`std::optional::has_value()`).
	if (fromType.Kind() != TypeKind::TypeParameter)
		return false;
	if (fromType.IsReferenceType().has_value())
		return false;
	return IsSubtypeOf(compilation, fromType, toType, 0);
}

bool IsBoxingConversionOrInvolvingTypeParameter(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// The C# `return IsBoxingConversion(fromType, toType) || ImplicitTypeParameterConversion(fromType, toType);`
	// -- a boxing conversion OR an implicit conversion involving a type parameter that might be a
	// boxing conversion when instantiated with a value type (the `IsBoxingConversion` arm handles a
	// concrete value type; the `ImplicitTypeParameterConversion` arm handles an unconstrained type
	// parameter that could be a value type).
	return IsBoxingConversion(compilation, fromType, toType)
		|| ImplicitTypeParameterConversion(compilation, fromType, toType);
}

bool IsIntegerType(const IType& type)
{
	// C# `switch (type.Kind) { case TypeKind.NInt: case TypeKind.NUInt: return true; }` -- the native
	// integers have no `KnownTypeCode` (synthetic `TypeKind.NInt`/`NUInt`, not `ITypeDefinition`s),
	// so they are recognized via `Kind` before the `GetTypeCode` range check (the `IsNumericType`
	// precedent). The `enum class` has no implicit `bool`, so the `switch` ports verbatim.
	switch (type.Kind()) {
		case TypeKind::NInt:
		case TypeKind::NUInt:
			return true;
		default:
			break;
	}
	// C# `TypeCode c = ReflectionHelper.GetTypeCode(type); return c >= TypeCode.SByte && c <= TypeCode.UInt64;`
	// -- the `enum class` has no relational operators, so the ordinal comparison ports via
	// `static_cast<int>` (the `IsNumericType` range-check precedent).
	TypeCode c = GetTypeCode(type);
	return static_cast<int>(c) >= static_cast<int>(TypeCode::SByte)
		&& static_cast<int>(c) <= static_cast<int>(TypeCode::UInt64);
}

bool ImplicitPointerConversion(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// C# spec (draft-v11) section 24.5. The C# `fromType.Kind.IsAnyPointer()` is the free
	// `IsAnyPointer(TypeKind)` helper (the TypeSystemExtensions prerequisite); `toType is PointerType`
	// ports to `dynamic_cast<PointerType*>(&toType)`; `toType.ReflectionName == "System.Void*"`
	// ports to `toType.ReflectionName() == "System.Void*"` (a `PointerType` over `System.Void` --
	// `PointerType::ReflectionName` is `element->ReflectionName() + "*"`, and `KnownType(Void)`
	// renders `"System.Void"`). Any pointer kind (Pointer or FunctionPointer) on the from-side converts.
	if (IsAnyPointer(fromType.Kind()) && dynamic_cast<PointerType*>(&toType) != nullptr
		&& toType.ReflectionName() == "System.Void*")
		return true;
	// The C# `fromType.Kind == TypeKind.Null && toType.Kind.IsAnyPointer()` -- the null literal converts
	// to any pointer kind. The `IsAnyPointer(toType.Kind())` helper covers both `Pointer` and `FunctionPointer`.
	if (fromType.Kind() == TypeKind::Null && IsAnyPointer(toType.Kind()))
		return true;
	// The C# `fromType is FunctionPointerType fromFnPtr && toType is FunctionPointerType toFnPtr &&
	// fromFnPtr.CallingConvention == toFnPtr.CallingConvention && fromFnPtr.ParameterTypes.Length ==
	// toFnPtr.ParameterTypes.Length`. The `is FunctionPointerType` ports to `dynamic_cast`; the
	// `CallingConvention` and `ParameterTypes().size()` are the faithful accessors.
	FunctionPointerType* fromFnPtr = dynamic_cast<FunctionPointerType*>(&fromType);
	FunctionPointerType* toFnPtr = dynamic_cast<FunctionPointerType*>(&toType);
	if (fromFnPtr != nullptr && toFnPtr != nullptr
		&& fromFnPtr->CallingConvention() == toFnPtr->CallingConvention()
		&& fromFnPtr->ParameterTypes().size() == toFnPtr->ParameterTypes().size())
	{
		// Variance applies to function pointer types. The C# `const int nestingDepth = 0;` -- the depth
		// starts at 0 (the depth guard lives inside `IsSubtypeOf`). The return type must be convertible
		// by identity or implicit reference conversion. `ReturnType()` returns `const ITypePtr&`; `*ptr`
		// is `IType&` (the `shared_ptr<IType>` `operator*` returns a non-const reference to the managed
		// `IType`), so the extracted types feed the non-const `IdentityConversion` / `ImplicitReferenceConversion`.
		const int nestingDepth = 0;
		IType& fromReturn = *fromFnPtr->ReturnType();
		IType& toReturn = *toFnPtr->ReturnType();
		if (!(IdentityConversion(fromReturn, toReturn)
			|| ImplicitReferenceConversion(compilation, fromReturn, toReturn, nestingDepth)))
			return false;
		// The C# `foreach (var (fromPT, toPT) in fromFnPtr.ParameterTypes.Zip(toFnPtr.ParameterTypes))`
		// -- the `Zip` pairs elements by position; the length-equality guard above ensures both vectors
		// have the same length. NOTE the SWAPPED order in the body: `IdentityConversion(toPT, fromPT)` and
		// `ImplicitReferenceConversion(toPT, fromPT)` -- function-pointer parameter variance is
		// CONTRAVARIANT (the target's parameter must accept the source's argument, so the conversion
		// direction is reversed for parameters).
		const std::vector<ITypePtr>& fromParams = fromFnPtr->ParameterTypes();
		const std::vector<ITypePtr>& toParams = toFnPtr->ParameterTypes();
		for (std::size_t i = 0; i < fromParams.size(); i++) {
			IType& fromPT = *fromParams[i];
			IType& toPT = *toParams[i];
			if (!(IdentityConversion(toPT, fromPT)
				|| ImplicitReferenceConversion(compilation, toPT, fromPT, nestingDepth)))
				return false;
		}
		return true;
	}
	return false;
}

bool ExplicitPointerConversion(const IType& fromType, const IType& toType)
{
	// C# spec (draft-v11) section 24.5. The C# `fromType.Kind.IsAnyPointer()` is the free
	// `IsAnyPointer(TypeKind)` helper. A pointer (any kind) converts to any other pointer or to any
	// integer type; conversely any integer type converts to a pointer. Pure (reads only `Kind` +
	// `IsIntegerType`), so the parameters are `const IType&`.
	if (IsAnyPointer(fromType.Kind()))
		return IsAnyPointer(toType.Kind()) || IsIntegerType(toType);
	return IsAnyPointer(toType.Kind()) && IsIntegerType(fromType);
}

std::shared_ptr<Conversion> ExplicitTypeParameterConversion(const ICompilation& compilation,
                                                               IType& fromType, IType& toType)
{
	// C# spec (draft-v11) section 10.3.6. Explicit conversions involving a type parameter.
	// When the to-side is a type parameter: an explicit conversion from an interface OR from a type the
	// type parameter is a subtype of is considered an unboxing conversion (the C# comment: "explicit
	// type parameter conversions that aren't also reference conversions are considered to be unboxing
	// conversions"). The `IsSubtypeOf(toType, fromType, 0)` call -- note the SWAPPED order (the type
	// parameter `toType` is the subtype candidate against `fromType`) -- short-circuits to true when
	// `fromType` carries `KnownTypeCode::Object` (the `IsKnownType(t, Object)` check in `IsSubtypeOf`).
	// When the to-side is NOT a type parameter: a conversion from a type parameter to an interface is a
	// boxing conversion. Otherwise `None`.
	if (toType.Kind() == TypeKind::TypeParameter) {
		if (fromType.Kind() == TypeKind::Interface || IsSubtypeOf(compilation, toType, fromType, 0))
			return Conversions::UnboxingConversion();
	}
	else {
		if (fromType.Kind() == TypeKind::TypeParameter && toType.Kind() == TypeKind::Interface)
			return Conversions::BoxingConversion();
	}
	return Conversions::None();
}

bool ImplicitConstantExpressionConversion(const ResolveResult& rr, const IType& toType)
{
	// C# 9.0 spec section 10.2.11 (plus part of section 10.2.6 for the nullable-strip). The C#
	// `if (rr == null || !rr.IsCompileTimeConstant) return false;` -- the C++ reference cannot be
	// null (the C# null guard compiles out), so only the `IsCompileTimeConstant` check remains.
	if (!rr.IsCompileTimeConstant())
		return false;
	// `TypeCode fromTypeCode = ReflectionHelper.GetTypeCode(rr.Type);` -- the C# `rr.Type` ports to
	// `ResolveResult::Type()` (a `const` accessor returning `const IType&`); `GetTypeCode` takes
	// `const IType&` and dynamic_casts to `ITypeDefinition` (so the constant's type must be an
	// `ITypeDefinition` for the code to resolve -- a `KnownType` placeholder yields `TypeCode::Empty`).
	TypeCode fromTypeCode = GetTypeCode(rr.Type());
	// `toType = NullableType.GetUnderlyingType(toType);` -- the C# rebinds the local reference; the
	// C++ port binds a `const IType&` local to the const `GetUnderlyingType` return (the original
	// `toType` when not nullable, the type argument when `Nullable<T>` -- both outlive the call,
	// owned by the `ParameterizedType` reachable through `toType` or by `toType` itself).
	const IType& toTypeStripped = GetUnderlyingType(toType);
	TypeCode toTypeCode = GetTypeCode(toTypeStripped);
	if (toTypeStripped.Kind() == TypeKind::NUInt) {
		// C# `if (toType.Kind == TypeKind.NUInt) { toTypeCode = TypeCode.UInt32; }` -- a `nuint`
		// to-side is treated as `UInt32` (only 32 bits store safely on a 32-bit platform).
		toTypeCode = TypeCode::UInt32;
	}
	if (fromTypeCode == TypeCode::Int64) {
		// C# `long val = (long)rr.ConstantValue; return val >= 0 && toTypeCode == TypeCode.UInt64;` --
		// unboxes the boxed `long`. The guard above ensures `rr` is a compile-time constant; a
		// `long`-typed constant holds a boxed `long`. The pointer-form `std::any_cast` returns
		// `nullptr` on a type mismatch (a divergent state the C# would `InvalidCastException` on);
		// the guard returns `false` (no constant-expression conversion) as the safe faithful fallback.
		std::any cv = rr.ConstantValue();
		const auto* pVal = std::any_cast<std::int64_t>(&cv);
		if (pVal == nullptr)
			return false;
		return *pVal >= 0 && toTypeCode == TypeCode::UInt64;
	}
	else if (fromTypeCode == TypeCode::Int32) {
		// C# `object cv = rr.ConstantValue; if (cv == null) return false; int val = (int)cv;` -- the
		// C# null check ports to `!cv.has_value()` (an empty `any` is the C# `null`); the `(int)cv`
		// unbox ports to the pointer-form `std::any_cast<std::int32_t>` (returns `nullptr` on a type
		// mismatch, the safe faithful fallback rather than throwing `bad_any_cast`).
		std::any cv = rr.ConstantValue();
		if (!cv.has_value())
			return false;
		const auto* pVal = std::any_cast<std::int32_t>(&cv);
		if (pVal == nullptr)
			return false;
		std::int32_t val = *pVal;
		switch (toTypeCode) {
			// C# `case TypeCode.SByte: return val >= SByte.MinValue && val <= SByte.MaxValue;` --
			// `SByte.MinValue`/`SByte.MaxValue` are the BCL constants -128/127 (the `sbyte` range).
			case TypeCode::SByte:
				return val >= -128 && val <= 127;
			// C# `case TypeCode.Byte: return val >= Byte.MinValue && val <= Byte.MaxValue;` --
			// `Byte.MinValue`/`Byte.MaxValue` are 0/255 (the `byte` range).
			case TypeCode::Byte:
				return val >= 0 && val <= 255;
			// C# `case TypeCode.Int16: return val >= Int16.MinValue && val <= Int16.MaxValue;` --
			// `Int16.MinValue`/`Int16.MaxValue` are -32768/32767 (the `short` range).
			case TypeCode::Int16:
				return val >= -32768 && val <= 32767;
			// C# `case TypeCode.UInt16: return val >= UInt16.MinValue && val <= UInt16.MaxValue;` --
			// `UInt16.MinValue`/`UInt16.MaxValue` are 0/65535 (the `ushort` range).
			case TypeCode::UInt16:
				return val >= 0 && val <= 65535;
			// C# `case TypeCode.UInt32: case TypeCode.UInt64: return val >= 0;` -- the unsigned
			// 32/64-bit targets accept any non-negative `int` (the value fits).
			case TypeCode::UInt32:
			case TypeCode::UInt64:
				return val >= 0;
			default:
				break;
		}
	}
	return false;
}

std::shared_ptr<Conversion> StandardImplicitConversion(const ICompilation& compilation,
                                                         IType& fromType, IType& toType)
{
	// C# 9.0 spec section 10.4.2. The standard implicit conversion dispatch entry point: checks the
	// already-ported conversion helpers in spec order, returning the first matching Conversion
	// singleton. The dispatch ORDER matters and is spec-mandated: identity before numeric (int->int
	// same instance is identity, not numeric), numeric before nullable (the lifted-numeric arm is
	// inside the nullable helper), nullable before null-literal (Nullable<T> accepts the null literal
	// but the identity/nullable arms fire first for a non-null source), reference before boxing (a
	// reference-to-reference is not a boxing), boxing before type-parameter (the type-parameter arm
	// yields a boxing conversion when it isn't also a reference conversion), pointer last among the
	// ported arms. The tuple/inline-array/span arms are deferred (need TupleResolveResult /
	// IsInlineArrayType / Span machinery) and yield None for those shapes until ported.
	if (IdentityConversion(fromType, toType))
		return Conversions::IdentityConversion();
	if (ImplicitNumericConversion(fromType, toType))
		return Conversions::ImplicitNumericConversion();
	// The C# `Conversion c = ImplicitNullableConversion(fromType, toType); if (c != Conversion.None)
	// return c;` -- the `c != Conversion.None` check ports to pointer-identity against the `None`
	// singleton (`c.get() != Conversions::None().get()`). A nullable conversion returning a singleton
	// (ImplicitNullableConversion / ImplicitLiftedNumericConversion) is not None; the `None` return
	// falls through to the next arm.
	std::shared_ptr<Conversion> c = ImplicitNullableConversion(fromType, toType);
	if (c.get() != Conversions::None().get())
		return c;
	if (NullLiteralConversion(fromType, toType))
		return Conversions::NullLiteralConversion();
	// The C# `ImplicitReferenceConversion(fromType, toType, 0)` -- the depth starts at 0 (the depth
	// guard lives inside `IsSubtypeOf`). The Detail helper takes the threaded compilation + the depth.
	if (ImplicitReferenceConversion(compilation, fromType, toType, 0))
		return Conversions::ImplicitReferenceConversion();
	if (IsBoxingConversion(compilation, fromType, toType))
		return Conversions::BoxingConversion();
	if (ImplicitTypeParameterConversion(compilation, fromType, toType))
	{
		// Implicit type parameter conversions that aren't also reference conversions are considered
		// to be boxing conversions (the C# comment at line 224). The type-parameter arm fires only
		// when the reference/boxing guards have already failed (an unconstrained type parameter
		// whose IsReferenceType is indeterminate), so the dispatch returns a boxing conversion.
		return Conversions::BoxingConversion();
	}
	if (ImplicitPointerConversion(compilation, fromType, toType))
		return Conversions::ImplicitPointerConversion();
	// The tuple / inline-array / span arms are deferred (need TupleResolveResult / IsInlineArrayType /
	// Span machinery). Until they land, those shapes yield None -- the faithful fallback for a shape
	// the ported arms do not yet handle.
	return Conversions::None();
}

std::shared_ptr<Conversion> ExplicitConversionImpl(const ICompilation& compilation,
                                                       IType& fromType, IType& toType)
{
	// C# spec draft-v11 section 10.4.3. The standard explicit conversion dispatch entry point:
	// called by the public `ExplicitConversion` methods AFTER the implicit conversions have been
	// checked, so any remaining conversion must be explicit. Checks the already-ported helpers in
	// spec order, returning the first matching Conversion singleton. The dispatch ORDER matters and
	// is spec-mandated: numeric before enumeration (both could match an enum<->numeric pair, but
	// AnyNumericConversion requires both sides numeric -- an enum is NOT numeric, so only the
	// enumeration arm fires for an enum<->numeric pair; the numeric arm fires for a numeric<->numeric
	// pair), enumeration before nullable (an enum<->Nullable<enum> pair could match both, but
	// ExplicitEnumerationConversion fires first for the non-lifted case), nullable before reference
	// (the lifted-nullable arm is inside ExplicitNullableConversion), reference before unboxing (a
	// reference-to-reference is not an unboxing), unboxing before type-parameter (an unboxing is a
	// type-parameter conversion but the unboxing arm is the more specific case), type-parameter
	// before pointer, pointer last among the ported arms. The tuple arm is deferred (needs
	// TupleResolveResult machinery) and yields None until ported.
	if (AnyNumericConversion(fromType, toType))
		return Conversions::ExplicitNumericConversion();
	if (ExplicitEnumerationConversion(fromType, toType))
		// The C# `Conversion.EnumerationConversion(false, false)` -- explicit (isImplicit=false),
		// not lifted (isLifted=false). A FACTORY method (a fresh per-call instance), NOT a singleton --
		// the `Conversions::EnumerationConversion(false, false)` builds a new
		// `NumericOrEnumerationConversion(false, false, true)` each call. The test asserts flags,
		// not pointer-identity.
		return Conversions::EnumerationConversion(false, false);
	// The C# `Conversion c = ExplicitNullableConversion(fromType, toType); if (c != Conversion.None)`
	// return c;` -- the `c != Conversion.None` check ports to pointer-identity against the `None`
	// singleton. ExplicitNullableConversion may return ExplicitNullableConversion /
	// ExplicitLiftedNumericConversion / EnumerationConversion(false, true) (all non-None) or None.
	std::shared_ptr<Conversion> c = ExplicitNullableConversion(fromType, toType);
	if (c.get() != Conversions::None().get())
		return c;
	if (ExplicitReferenceConversion(compilation, fromType, toType))
		return Conversions::ExplicitReferenceConversion();
	if (UnboxingConversion(compilation, fromType, toType))
		return Conversions::UnboxingConversion();
	// The C# `c = ExplicitTypeParameterConversion(fromType, toType); if (c != Conversion.None)` return c;`
	// -- the same pointer-identity-against-None check as the nullable arm. ExplicitTypeParameterConversion
	// may return UnboxingConversion / BoxingConversion (both non-None singletons) or None.
	c = ExplicitTypeParameterConversion(compilation, fromType, toType);
	if (c.get() != Conversions::None().get())
		return c;
	if (ExplicitPointerConversion(fromType, toType))
		return Conversions::ExplicitPointerConversion();
	// The tuple arm (`TupleConversion(fromType, toType, isExplicit: true)`) is deferred (needs
	// TupleResolveResult machinery). Until it lands, non-tuple shapes yield None -- the faithful
	// fallback for a shape the ported arms do not yet handle.
	return Conversions::None();
}

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
