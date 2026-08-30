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

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"  // CSharpConversions (Get -- the anonymous-function arm threads the per-compilation controller to LambdaResolveResult::IsValid)
#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"  // LambdaResolveResult (the anonymous-function arm RTTI target) + LambdaConversion (the IsValid success result)
#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversions (the nullable-conversion singletons / EnumerationConversion factory)
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"  // ByReferenceResolveResult (the method-group args ref/out/in arm)
#include "Decompiler/Semantics/InterpolatedStringResolveResult.hpp"  // InterpolatedStringResolveResult (the interpolated-string arm RTTI check)
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (IsCompileTimeConstant / Type / ConstantValue -- the constant-expression conversion)
#include "Decompiler/Semantics/ThrowResolveResult.hpp"  // ThrowResolveResult (the throw-expression arm RTTI check)
#include "Decompiler/Semantics/TupleResolveResult.hpp"  // TupleResolveResult (the tuple-conversion ResolveResult overload RTTI target + Elements)
#include "Decompiler/TypeSystem/TupleType.hpp"  // GetTupleElementTypes (D539 -- the tuple-conversion element flattening)
#include "Decompiler/TypeSystem/TaskType.hpp"        // IsTask, IsCustomTask (the CSharpConversions.UnpackTask prerequisites)
#include "Decompiler/TypeSystem/ICompilation.hpp"   // ICompilation (FindType -- the array-to-System.Array arm)
#include "Decompiler/TypeSystem/IMethod.hpp"       // IMethod (IsStatic / IsOperator / Name / Parameters / ReturnType -- the operator scan)
#include "Decompiler/TypeSystem/IParameter.hpp"    // IParameter (Type / ReferenceKind -- the operator's parameter type)
#include "Decompiler/TypeSystem/IType.hpp"          // IType (Kind), ITypePtr, AcceptVisitor, Equals, ParameterizedType, ArrayType
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (KnownTypeCode -- the UnpackGenericArrayInterface definition arm)
#include "Decompiler/TypeSystem/ITypeParameter.hpp"  // ITypeParameter (Variance -- the IdentityOrVarianceConversion variance loop)
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"   // KnownTypeCode (the array-interface codes / Array)
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"  // NormalizeTypeVisitor::TypeErasure (IdentityConversion)
#include "Decompiler/TypeSystem/NullableType.hpp"   // IsNullable, GetUnderlyingType, Create, IsNonNullableValueType (the nullable helpers)
#include "Decompiler/TypeSystem/ReferenceKind.hpp"  // ReferenceKind (In -- the ref-In operator-parameter unwrap special case)
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"  // GetTypeCode, TypeCode
#include "Decompiler/TypeSystem/TypeKind.hpp"       // TypeKind
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetAllBaseTypes, IsKnownType (the IsSubtypeOf traversal)
#include "Decompiler/TypeSystem/VarianceModifier.hpp"  // VarianceModifier (the Covariant/Contravariant/Invariant switch)

#include <algorithm>  // std::any_of (the UserDefinedImplicit/Explicit operator-source/target reduction)
#include <any>       // std::any (the ConstantValue unbox -- the D374/D424 `object?` model)
#include <cassert>  // assert (the Debug.Assert rr.IsCompileTimeConstant)
#include <cstdint>   // std::int32_t / std::int64_t (the boxed int/long the constant-expression conversion reads)
#include <functional> // std::function (the opFilter predicate)
#include <memory>
#include <optional>  // std::optional<double> (the ConvertToDouble faithful-fallback return)

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::LambdaConversion;
using ILSpy::Decompiler::CSharp::Resolver::LambdaResolveResult;
using ILSpy::Decompiler::Semantics::ByReferenceResolveResult;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::Semantics::InterpolatedStringResolveResult;
using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::ThrowResolveResult;
using ILSpy::Decompiler::Semantics::TupleResolveResult;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::Create;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::GetAllBaseTypes;
using ILSpy::Decompiler::TypeSystem::GetDelegateInvokeMethod;
using ILSpy::Decompiler::TypeSystem::GetTupleElementTypes;
using ILSpy::Decompiler::TypeSystem::GetTypeCode;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsAnyPointer;
using ILSpy::Decompiler::TypeSystem::IsKnownType;
using ILSpy::Decompiler::TypeSystem::IsNonNullableValueType;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::NormalizeTypeVisitor;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::IsTask;
using ILSpy::Decompiler::TypeSystem::IsCustomTask;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeSystemOptions;
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

// The C# `Convert.ToDouble(rr.ConstantValue)` (the `ImplicitEnumerationConversion` zero-value check) --
// converts a boxed numeric constant to `double`. The guard in `ImplicitEnumerationConversion` ensures
// `rr.Type`'s `TypeCode` is in [SByte, Decimal] (the numeric primitives: sbyte/byte/short/ushort/int/
// uint/long/ulong/float/double/decimal), so the constant holds a boxed numeric value of one of those
// types. The pointer-form `std::any_cast` returns `nullptr` on a type mismatch (a divergent state the C#
// would `InvalidCastException` on); the guard returns `nullopt` as the safe faithful fallback (the caller
// treats `nullopt` as not-zero -> no conversion). The C# `decimal` has no standard C++ type; the helper
// does not handle it (a `decimal`-typed constant is not constructible in the port's test stubs, and the
// guard's `TypeCode` range check admits it only on the type side, not the value side -- a mismatched
// value returns `nullopt`, faithfully returning `None`).
static std::optional<double> ConvertToDouble(const std::any& cv) {
	if (const auto* p = std::any_cast<std::int8_t>(&cv))  return static_cast<double>(*p);
	if (const auto* p = std::any_cast<std::uint8_t>(&cv)) return static_cast<double>(*p);
	if (const auto* p = std::any_cast<std::int16_t>(&cv)) return static_cast<double>(*p);
	if (const auto* p = std::any_cast<std::uint16_t>(&cv)) return static_cast<double>(*p);
	if (const auto* p = std::any_cast<std::int32_t>(&cv)) return static_cast<double>(*p);
	if (const auto* p = std::any_cast<std::uint32_t>(&cv)) return static_cast<double>(*p);
	if (const auto* p = std::any_cast<std::int64_t>(&cv)) return static_cast<double>(*p);
	if (const auto* p = std::any_cast<std::uint64_t>(&cv)) return static_cast<double>(*p);
	if (const auto* p = std::any_cast<float>(&cv))       return static_cast<double>(*p);
	if (const auto* p = std::any_cast<double>(&cv))      return *p;
	return std::nullopt;
}

std::shared_ptr<Conversion>
ImplicitEnumerationConversion(const ResolveResult& rr, const IType& toType)
{
	// C# 9.0 spec section 10.2.4 + the enum part of section 10.2.6 (Nullable conversions). The C#
	// `Debug.Assert(rr.IsCompileTimeConstant)` -- the public `ImplicitConversion(ResolveResult, IType)`
	// dispatch only calls this when `rr.IsCompileTimeConstant` is true (CSharpConversions.cs line 104),
	// so the assert is a debug-only invariant check; the port asserts it faithfully (a release C# build
	// would proceed past a failed assert, but the guard below handles a non-constant's type correctly --
	// a non-numeric `TypeCode` fails the range check -> `None`).
	assert(rr.IsCompileTimeConstant());
	// `TypeCode constantType = ReflectionHelper.GetTypeCode(rr.Type);` -- the C# `rr.Type` ports to
	// `ResolveResult::Type()` (a `const` accessor returning `const IType&`); `GetTypeCode` takes
	// `const IType&` and dynamic_casts to `ITypeDefinition` (so the constant's type must be an
	// `ITypeDefinition` for the code to resolve -- a `KnownType` placeholder yields `TypeCode::Empty`).
	TypeCode constantType = GetTypeCode(rr.Type());
	// C# `if (constantType >= TypeCode.SByte && constantType <= TypeCode.Decimal &&
	// Convert.ToDouble(rr.ConstantValue) == 0)` -- the `TypeCode` relational comparisons have no
	// C++ enum-class counterpart (the D514 precedent), so the port casts via `static_cast<int>` for
	// the ordinal range check [SByte(5), Decimal(15)]. The `Convert.ToDouble(ConstantValue) == 0`
	// ports to the `ConvertToDouble` helper returning `std::optional<double>`; a `nullopt` (a type
	// mismatch) is treated as not-zero (the `&&` short-circuits to false -> `None`, the safe faithful
	// fallback). The `== 0` comparison: a zero-valued numeric converts to `0.0`, and `0.0 == 0` is
	// true (the C# `==` promotes the `int` literal `0` to `0.0`); `-0.0 == 0.0` is also true (IEEE 754).
	if (static_cast<int>(constantType) >= static_cast<int>(TypeCode::SByte)
	    && static_cast<int>(constantType) <= static_cast<int>(TypeCode::Decimal)) {
		auto d = ConvertToDouble(rr.ConstantValue());
		if (d.has_value() && *d == 0.0) {
			// C# `if (NullableType.GetUnderlyingType(toType).Kind == TypeKind.Enum)` -- the to-side is
			// stripped of its nullable wrapper first (`GetUnderlyingType` returns the type itself when
			// not nullable, the type argument when `Nullable<T>`), then `Kind` is checked for `Enum`.
			// The `GetUnderlyingType` const-overload returns `const IType&` (the underlying object is
			// owned by the `ParameterizedType` reachable through `toType`, outliving the call).
			if (GetUnderlyingType(toType).Kind() == TypeKind::Enum) {
				// C# `return Conversion.EnumerationConversion(true, NullableType.IsNullable(toType));`
				// -- the `EnumerationConversion` FACTORY (a fresh per-call `NumericOrEnumerationConversion`
				// instance, NOT a singleton): `isImplicit=true`, `isLifted=IsNullable(toType)` (the lifted
				// form for `0 -> E?`, the non-lifted form for `0 -> E`).
				return Conversions::EnumerationConversion(true, IsNullable(toType));
			}
		}
	}
	return Conversions::None();
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
	// C# `if (allowTupleConversion) { c = TupleConversion(fromType, toType, isExplicit: false);
	// if (c != Conversion.None) return c; }` -- the tuple arm (C# 9.0 spec section 10.2.13). The port's
	// `StandardImplicitConversion` has no `allowTupleConversion` parameter (the public entry always
	// passes `true`, the D523 collapse), so the arm always runs. The `c != Conversion.None` check ports
	// to pointer-identity against the `None` singleton; a non-tuple shape yields `None` and falls
	// through to the (deferred) inline-array / span arms.
	c = TupleConversion(compilation, fromType, toType, /*isExplicit*/ false);
	if (c.get() != Conversions::None().get())
		return c;
	// The inline-array / span arms are deferred (need IsInlineArrayType / the StandardImplicitConversion
	// span wiring). Until they land, those shapes yield None -- the faithful fallback for a shape the
	// ported arms do not yet handle.
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
	// C# `return TupleConversion(fromType, toType, isExplicit: true);` -- the LAST arm returns the
	// `TupleConversion` directly (NOT a `None`-guarded dispatch like the other arms). A non-tuple
	// shape yields `None` (the `TupleConversion` helper returns `None` when either side is not a
	// tuple or the element counts differ), faithfully matching the C# which also returns the
	// `TupleConversion` result as-is. The `TupleConversion` IType overload (D-something) threads the
	// compilation for the per-element `ExplicitConversion` calls.
	return TupleConversion(compilation, fromType, toType, /*isExplicit*/ true);
}

std::shared_ptr<Conversion>
ExplicitConversionNotUserDefined(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// CSharpConversions.cs line 331: `Conversion c = ImplicitConversion(fromType, toType,
	// allowUserDefined: false, allowTuple: false); if (c != Conversion.None) return c; return
	// ExplicitConversionImpl(fromType, toType);`. The private `ImplicitConversion(IType, IType,
	// bool allowUserDefined, bool allowTuple)` overload (line 166) the C# calls is:
	//   var c = StandardImplicitConversion(fromType, toType, allowTuple);
	//   if (c == Conversion.None && allowUserDefined) c = UserDefinedImplicitConversion(null, fromType, toType);
	//   return c;
	// With `allowUserDefined: false` the user-defined branch is unreachable, and the tuple arm is
	// deferred (yields None for tuple shapes until ported), so `allowTuple` is effectively `true`
	// for the ported arms. The faithful port is the standard implicit conversion (the already-ported
	// `Detail::StandardImplicitConversion` D523) then, if no implicit conversion exists, the standard
	// explicit conversion (the already-ported `Detail::ExplicitConversionImpl` D524). The implicit
	// check is FIRST: an implicit conversion is returned even though the name says "ExplicitConversion"
	// (the crux that distinguishes this helper from `ExplicitConversionImpl`).
	auto c = StandardImplicitConversion(compilation, fromType, toType);
	if (c.get() != Conversions::None().get())
		return c;
	return ExplicitConversionImpl(compilation, fromType, toType);
}

bool IsEncompassedBy(const ICompilation& compilation, IType& a, IType& b)
{
	// C# spec draft-v11 section 10.5.4 (the user-defined implicit conversions encompassment helper).
	// The C# `return StandardImplicitConversion(a, b).IsValid;` -- the `IsValid` fold of the
	// `StandardImplicitConversion` dispatch return. A `None` return (the `InvalidConversion` singleton,
	// `IsValid` false) means `a` is NOT encompassed by `b`; any other return (a `BuiltinConversion` /
	// `NumericOrEnumerationConversion` singleton, `IsValid` inherited true) means `a` IS encompassed.
	// The dispatch delegates to the already-ported `Detail::StandardImplicitConversion` (D523) which
	// threads the compilation through the reference/boxing/type-parameter/pointer arms.
	return StandardImplicitConversion(compilation, a, b)->IsValid();
}

bool IsEncompassingOrEncompassedBy(const ICompilation& compilation, IType& a, IType& b)
{
	// C# spec draft-v11 section 10.5.5 (the user-defined explicit conversions encompassment helper).
	// The C# `return (StandardImplicitConversion(a, b).IsValid || StandardImplicitConversion(b, a).IsValid);`
	// -- the `IsValid` fold of the dispatch return in BOTH directions. A standard implicit conversion in
	// EITHER direction (`a` -> `b` OR `b` -> `a`) makes `a` encompass-or-be-encompassed-by `b`. The two
	// `StandardImplicitConversion` calls each thread the compilation through the dispatch arms; the
	// `||` short-circuits on the first `true` (a valid conversion in the forward direction suffices, so
	// the reverse-direction call is skipped when the forward direction already succeeds).
	return StandardImplicitConversion(compilation, a, b)->IsValid()
		|| StandardImplicitConversion(compilation, b, a)->IsValid();
}

ITypePtr FindMostEncompassedType(const ICompilation& compilation, const std::vector<ITypePtr>& candidates)
{
	// C# spec draft-v11 section 10.5.4 (the most-encompassed type). The C# `IType best = null;
	// foreach (var current in candidates) { if (best == null || IsEncompassedBy(current, best))
	// best = current; else if (!IsEncompassedBy(best, current)) return null; } return best;` --
	// the running `best` is the most-encompassed candidate so far. For each `current`: if `best` is
	// null (first iteration) or `current` is encompassed by `best` (`current` is "smaller"), `best`
	// becomes `current`; else if `best` is NOT encompassed by `current` (neither encompasses the
	// other), the set is ambiguous -> return null; otherwise (`best` is encompassed by `current`, so
	// `current` is "bigger") `best` stays. The result is the candidate every other candidate converts
	// to (the "smallest" in the implicit-conversion partial order). The `!best` check ports the C#
	// `best == null` (a default-constructed `shared_ptr` is null); `return ITypePtr()` ports `return null`.
	ITypePtr best;
	for (const ITypePtr& current : candidates)
	{
		if (!best || IsEncompassedBy(compilation, *current, *best))
			best = current;
		else if (!IsEncompassedBy(compilation, *best, *current))
			return ITypePtr();   // Ambiguous
	}
	return best;
}

ITypePtr FindMostEncompassingType(const ICompilation& compilation, const std::vector<ITypePtr>& candidates)
{
	// C# spec draft-v11 section 10.5.4 (the most-encompassing type). Mirrors `FindMostEncompassedType`
	// with the direction swapped: the running `best` is the most-encompassing candidate so far. For
	// each `current`: if `best` is null or `best` is encompassed by `current` (`current` is
	// "bigger"), `best` becomes `current`; else if `current` is NOT encompassed by `best` (neither
	// encompasses the other), the set is ambiguous -> return null; otherwise `best` stays. The result
	// is the candidate every other candidate converts from (the "biggest" in the implicit-conversion
	// partial order). The `!best` check ports the C# `best == null`; `return ITypePtr()` ports
	// `return null`.
	ITypePtr best;
	for (const ITypePtr& current : candidates)
	{
		if (!best || IsEncompassedBy(compilation, *best, *current))
			best = current;
		else if (!IsEncompassedBy(compilation, *current, *best))
			return ITypePtr();   // Ambiguous
	}
	return best;
}

std::shared_ptr<Conversion>
SelectOperator(const ICompilation& compilation, IType& mostSpecificSource, IType& mostSpecificTarget,
              const std::vector<OperatorInfo>& operators, bool isImplicit, IType& source, IType& target)
{
	// CSharpConversions.cs line 993. Filter the applicable operators to those whose `SourceType`
	// equals `mostSpecificSource` AND whose `TargetType` equals `mostSpecificTarget` (the most-specific
	// source/target the user-defined-conversion resolution computed). The C# `operators.Where(op =>
	// op.SourceType.Equals(mostSpecificSource) && op.TargetType.Equals(mostSpecificTarget)).ToList()`
	// ports to a loop collecting non-owning pointers into the filtered `selected` vector (the
	// `OperatorInfo` entries are owned by the `operators` argument and outlive this call).
	std::vector<const OperatorInfo*> selected;
	for (const OperatorInfo& op : operators)
	{
		if (op.SourceType->Equals(mostSpecificSource) && op.TargetType->Equals(mostSpecificTarget))
			selected.push_back(&op);
	}

	// The C# `if (selected.Count == 0) return Conversion.None;` -- no operator matches the most-specific
	// source/target pair.
	if (selected.empty())
		return Conversions::None();

	// The C# `if (selected.Count == 1) return Conversion.UserDefinedConversion(selected[0].Method,
	// isLifted: selected[0].IsLifted, isImplicit: isImplicit, conversionBeforeUserDefinedOperator:
	// ExplicitConversionNotUserDefined(source, mostSpecificSource), conversionAfterUserDefinedOperator:
	// ExplicitConversionNotUserDefined(mostSpecificTarget, target));` -- the unambiguous single match.
	// The before/after conversions come from the already-ported `Detail::ExplicitConversionNotUserDefined`
	// (D525): the conversion from the original `source` to the operator's `mostSpecificSource`, and
	// from the operator's `mostSpecificTarget` to the original `target`.
	if (selected.size() == 1)
	{
		const OperatorInfo* op = selected[0];
		return Conversions::UserDefinedConversion(op->Method, isImplicit,
			ExplicitConversionNotUserDefined(compilation, source, mostSpecificSource),
			ExplicitConversionNotUserDefined(compilation, mostSpecificTarget, target),
			op->IsLifted);
	}

	// More than one operator matches. The C# `int nNonLifted = selected.Count(s => !s.IsLifted);
	// if (nNonLifted == 1) { var op = selected.First(s => !s.IsLifted); return ...; }` -- if exactly one
	// of the matches is non-lifted, prefer it over the lifted forms.
	int nNonLifted = 0;
	const OperatorInfo* nonLifted = nullptr;
	for (const OperatorInfo* op : selected)
	{
		if (!op->IsLifted)
		{
			++nNonLifted;
			nonLifted = op;
		}
	}
	if (nNonLifted == 1)
	{
		return Conversions::UserDefinedConversion(nonLifted->Method, isImplicit,
			ExplicitConversionNotUserDefined(compilation, source, mostSpecificSource),
			ExplicitConversionNotUserDefined(compilation, mostSpecificTarget, target),
			nonLifted->IsLifted);
	}

	// The C# ambiguous fallback: `return Conversion.UserDefinedConversion(selected[0].Method,
	// isLifted: selected[0].IsLifted, isImplicit: isImplicit, isAmbiguous: true,
	// conversionBeforeUserDefinedOperator: ExplicitConversionNotUserDefined(source, mostSpecificSource),
	// conversionAfterUserDefinedOperator: ExplicitConversionNotUserDefined(mostSpecificTarget, target));`
	// -- zero non-lifted (all matches are lifted) OR more than one non-lifted. The `isAmbiguous: true`
	// flag makes the returned conversion `IsValid == false` (the `UserDefinedConv` ctor computes
	// `isValid = !isAmbiguous`). Uses `selected[0]` (the first match) as the representative method.
	const OperatorInfo* op = selected[0];
	return Conversions::UserDefinedConversion(op->Method, isImplicit,
		ExplicitConversionNotUserDefined(compilation, source, mostSpecificSource),
		ExplicitConversionNotUserDefined(compilation, mostSpecificTarget, target),
		op->IsLifted, /*isAmbiguous*/ true);
}

const IType& UnderlyingTypeForConversion(const IType& type)
{
	// CSharpConversions.cs line 1164. The C# `if (type.Kind == TypeKind.ByReference) type =
	// ((ByReferenceType)type).ElementType;` -- a `ref` parameter/local does not carry its own
	// conversion operators, so unwrap to the element type first. The `ByReferenceType::Element()`
	// accessor returns `const ITypePtr&` (a reference to the `element_` shared handle owned by the
	// `ByReferenceType`, reachable through `type`); `*element` yields the managed `IType&`. A
	// degenerate `ByReferenceType` with a null element would be UB to deref -- the guard falls
	// through to `GetUnderlyingType(type)` as the safe faithful fallback (the D516 null-guard
	// precedent; the C# would NRE, but a null element does not occur in practice).
	if (type.Kind() == TypeKind::ByReference) {
		const ByReferenceType& byRef = dynamic_cast<const ByReferenceType&>(type);
		const ITypePtr& element = byRef.Element();
		if (element) {
			// C# `return NullableType.GetUnderlyingType(type);` on the rebound `type` (the element):
			// the const `GetUnderlyingType` overload returns the `Nullable<T>` type argument (owned by
			// the element's `ParameterizedType`'s `typeArgs_`, reachable through the element), or the
			// element itself when not nullable. The returned reference outlives the call (the element
			// is owned by the `ByReferenceType`'s `element_`, reachable through the input `type`).
			return GetUnderlyingType(*element);
		}
	}
	// C# `return NullableType.GetUnderlyingType(type);` -- the const overload returns the underlying
	// `T` for `Nullable<T>`, otherwise the original `type` (modifiers preserved). The returned
	// reference is valid for the lifetime of `type` (the type argument is owned by the
	// `ParameterizedType`'s `typeArgs_` reachable through `type`, or the overload returns `type`
	// itself in the else branch).
	return GetUnderlyingType(type);
}

std::vector<OperatorInfo>
GetApplicableConversionOperators(const ICompilation& compilation, const ResolveResult* fromResult,
                                 IType& fromType, IType& toType, bool isExplicit)
{
	// CSharpConversions.cs line 1167. The C# `Predicate<IMethod> opFilter` -- the static
	// single-parameter conversion-operator filter. For explicit: `op_Explicit` OR `op_Implicit`
	// (an explicit conversion may use an implicit operator); for implicit: `op_Implicit` only.
	// The `m.Parameters.Count == 1` guard ports to `m->Parameters().size() == 1` (the `IList.Count`
	// -> `std::vector::size()`, the D510 argument-count precedent). The filter is applied by
	// `IType::GetMethods` (the faithful `GetMethodsImpl` runs it over the method table), so the
	// caller passes it through; the stub `GetMethods` applies it faithfully too.
	auto opFilter = [isExplicit](const IMethod* m) -> bool {
		bool nameOk = isExplicit
			? (m->Name() == "op_Explicit" || m->Name() == "op_Implicit")
			: (m->Name() == "op_Implicit");
		return m->IsStatic() && m->IsOperator() && nameOk && m->Parameters().size() == 1;
	};

	// C# `var operators = UnderlyingTypeForConversion(fromType).GetMethods(opFilter)
	// .Concat(UnderlyingTypeForConversion(toType).GetMethods(opFilter)).Distinct();` -- the
	// candidate operators from both types' method tables, concatenated and deduplicated. The C#
	// `.Distinct()` uses the default equality comparer for the reference type `IMethod` -- reference
	// equality -- which ports to a dedup by `const IMethod*` pointer identity (the two method-table
	// scans can return the SAME operator when fromType and toType share a base type declaring it).
	// `UnderlyingTypeForConversion` returns `const IType&` and `GetMethods` is a const method, so
	// the scans are const-only (no `AcceptVisitor`).
	std::vector<const IMethod*> operators;
	auto fromMethods = UnderlyingTypeForConversion(fromType).GetMethods(opFilter);
	for (const IMethod* m : fromMethods)
		operators.push_back(m);
	auto toMethods = UnderlyingTypeForConversion(toType).GetMethods(opFilter);
	for (const IMethod* m : toMethods)
	{
		bool dup = false;
		for (const IMethod* existing : operators)
		{
			if (existing == m) { dup = true; break; }
		}
		if (!dup)
			operators.push_back(m);
	}

	// C# `List<OperatorInfo> result = new List<OperatorInfo>(); foreach (IMethod op in operators)
	// { ... }` -- the per-operator applicability check. The operator's `sourceType` is its single
	// parameter's type; a `ref In` parameter unwraps to its element type when the from-side is not
	// itself by-ref (the operator takes the value by `in` ref but converts the underlying value).
	// The `targetType` is the return type. Both come from the const `IParameter::Type()` /
	// `IMember::ReturnType()` accessors, so they are held as `const IType*` and `const_cast` to
	// `IType&` for the non-const `IsEncompassedBy` / `IsEncompassingOrEncompassedBy` callers (the
	// underlying type-system objects are mutable -- the D515 const-overload-pair precedent).
	std::vector<OperatorInfo> result;
	for (const IMethod* op : operators)
	{
		const IType* sourceType = &op->Parameters()[0]->Type();
		// C# `if (sourceType.Kind == TypeKind.ByReference && op.Parameters[0].ReferenceKind ==
		// ReferenceKind.In && fromType.Kind != TypeKind.ByReference) sourceType =
		// ((ByReferenceType)sourceType).ElementType;` -- the `ref In` unwrap. A degenerate
		// `ByReferenceType` with a null element falls through (keeps the by-ref type) as the safe
		// faithful fallback (the D516 null-guard-before-deref precedent; the C# would NRE).
		if (sourceType->Kind() == TypeKind::ByReference
			&& op->Parameters()[0]->ReferenceKind() == ReferenceKind::In
			&& fromType.Kind() != TypeKind::ByReference)
		{
			const ByReferenceType& byRef = dynamic_cast<const ByReferenceType&>(*sourceType);
			const ITypePtr& element = byRef.Element();
			if (element)
				sourceType = element.get();
		}
		const IType* targetType = &op->ReturnType();

		// C# `bool isApplicable;` -- the applicability check. The `const_cast` feeds the const
		// accessor results to the non-const `IType&` encompassment helpers (the underlying objects
		// are mutable -- the accessor const is the contract, not the object's mutability).
		IType& srcRef = const_cast<IType&>(*sourceType);
		IType& tgtRef = const_cast<IType&>(*targetType);
		bool isApplicable;
		if (isExplicit)
		{
			// C# `isApplicable = (IsEncompassingOrEncompassedBy(fromType, sourceType) ||
			// ImplicitConstantExpressionConversion(fromResult, sourceType)) &&
			// IsEncompassingOrEncompassedBy(targetType, toType);`. The `fromResult` is a
			// nullable pointer (the C# nullable `ResolveResult` reference): a null `fromResult`
			// makes the constant-expression fallback false (the safe faithful port of the C# `||`
			// short-circuit -- the C# would NRE on `ImplicitConstantExpressionConversion(null, ...)`
			// if reached, but the `||` short-circuits whenever `IsEncompassingOrEncompassedBy` is
			// true, which is the only path the null-`fromResult` recursions reach in practice; the
			// guard avoids UB).
			isApplicable = (IsEncompassingOrEncompassedBy(compilation, fromType, srcRef)
				|| (fromResult != nullptr && ImplicitConstantExpressionConversion(*fromResult, *sourceType)))
				&& IsEncompassingOrEncompassedBy(compilation, tgtRef, toType);
		}
		else
		{
			// C# `isApplicable = (IsEncompassedBy(fromType, sourceType) ||
			// ImplicitConstantExpressionConversion(fromResult, sourceType)) &&
			// IsEncompassedBy(targetType, toType);`. The same null-`fromResult` guard as the
			// explicit arm above (the C# `||` short-circuit / NRE-avoidance precedent).
			isApplicable = (IsEncompassedBy(compilation, fromType, srcRef)
				|| (fromResult != nullptr && ImplicitConstantExpressionConversion(*fromResult, *sourceType)))
				&& IsEncompassedBy(compilation, tgtRef, toType);
		}
		// C# `if (isApplicable) result.Add(new OperatorInfo(op, sourceType, targetType, false));` --
		// the non-lifted form. The `OperatorInfo` holds owning `ITypePtr` handles, so the port
		// obtains them from the const `IType&` via `shared_from_this()` + `const_pointer_cast` (the
		// D529 `NullableType.Create` precedent -- the underlying objects are shared-managed by the
		// type system / the test stubs' `make_shared`).
		if (isApplicable)
		{
			result.emplace_back(op,
				std::const_pointer_cast<IType>(sourceType->shared_from_this()),
				std::const_pointer_cast<IType>(targetType->shared_from_this()),
				/*isLifted*/ false);
		}
		// C# `if (NullableType.IsNonNullableValueType(sourceType))` -- the lifted form. A
		// non-nullable value-type operator is lifted so its source (and, when the target is also
		// a non-nullable value type, its target) becomes `Nullable<T>`. The lifted target keeps
		// the original target type when it is not a non-nullable value type (e.g. a reference type
		// or a nullable). The lifted source/target are owning `ITypePtr` from `NullableType.Create`
		// (a freshly-allocated `ParameterizedType`) or -- for the non-lifted target -- from
		// `shared_from_this()` of the const accessor result.
		if (IsNonNullableValueType(*sourceType))
		{
			ITypePtr liftedSourceType = Create(compilation, *sourceType);
			ITypePtr liftedTargetType = IsNonNullableValueType(*targetType)
				? Create(compilation, *targetType)
				: std::const_pointer_cast<IType>(targetType->shared_from_this());
			IType& liftedSrcRef = *liftedSourceType;
			IType& liftedTgtRef = *liftedTargetType;
			if (isExplicit)
			{
				// C# `isApplicable = IsEncompassingOrEncompassedBy(fromType, liftedSourceType) &&
				// IsEncompassingOrEncompassedBy(liftedTargetType, toType);`
				isApplicable = IsEncompassingOrEncompassedBy(compilation, fromType, liftedSrcRef)
					&& IsEncompassingOrEncompassedBy(compilation, liftedTgtRef, toType);
			}
			else
			{
				// C# `isApplicable = IsEncompassedBy(fromType, liftedSourceType) &&
				// IsEncompassedBy(liftedTargetType, toType);`
				isApplicable = IsEncompassedBy(compilation, fromType, liftedSrcRef)
					&& IsEncompassedBy(compilation, liftedTgtRef, toType);
			}
			// C# `if (isApplicable) result.Add(new OperatorInfo(op, liftedSourceType,
			// liftedTargetType, true));` -- the lifted form. An operator can be applicable in BOTH
			// lifted and non-lifted forms (the explicit case); both are added.
			if (isApplicable)
			{
				result.emplace_back(op, std::move(liftedSourceType),
					std::move(liftedTargetType), /*isLifted*/ true);
			}
		}
	}
	return result;
}

// CSharpConversions.cs line 1030 (C# spec draft-v11 section 10.5.4 "user-defined implicit
// conversions"). See the header for the full contract. The body mirrors the C# verbatim with
// the established C++-port conventions: `operators.Any(p)` -> `std::any_of`; `operators.Select(p)`
// -> a hand-built `std::vector<ITypePtr>` (the ILSpy eager-collection convention); `mostSpecificSource
// == null` -> `!mostSpecificSource` (a null `shared_ptr`); `Conversion.None` -> `Conversions::None()`;
// `selected != Conversion.None` -> pointer-identity against the None singleton; `NullableType.IsNullable`
// / `GetUnderlyingType` -> the `NullableType::` free functions (D515); the recursive `UserDefinedImplicitConversion`
// call on the underlying target threads the same `fromResult` / `fromType` / `compilation`.
std::shared_ptr<Conversion>
UserDefinedImplicitConversion(const ICompilation& compilation, const ResolveResult* fromResult,
                             IType& fromType, IType& toType)
{
	// C# `if (fromType.Kind == TypeKind.Interface || toType.Kind == TypeKind.Interface) return
	// Conversion.None;` -- user-defined conversions are not supported with interfaces.
	if (fromType.Kind() == TypeKind::Interface || toType.Kind() == TypeKind::Interface)
		return Conversions::None();

	auto operators = GetApplicableConversionOperators(compilation, fromResult, fromType, toType,
	                                                   /*isExplicit*/ false);
	if (operators.empty())
		return Conversions::None();

	// C# `var mostSpecificSource = operators.Any(op => op.SourceType.Equals(fromType)) ? fromType :
	// FindMostEncompassedType(operators.Select(op => op.SourceType));`. The `fromType.shared_from_this()`
	// obtains an owning `ITypePtr` handle to `fromType` (the `enable_shared_from_this<IType>` bridge,
	// D406) so the `ITypePtr` type unifies with `FindMostEncompassedType`'s return.
	ITypePtr mostSpecificSource;
	if (std::any_of(operators.begin(), operators.end(),
	                [&](const OperatorInfo& op) { return op.SourceType->Equals(fromType); }))
		mostSpecificSource = fromType.shared_from_this();
	else
	{
		std::vector<ITypePtr> sourceTypes;
		sourceTypes.reserve(operators.size());
		for (const auto& op : operators)
			sourceTypes.push_back(op.SourceType);
		mostSpecificSource = FindMostEncompassedType(compilation, sourceTypes);
	}
	// C# `if (mostSpecificSource == null) return Conversion.UserDefinedConversion(operators[0].Method,
	// isImplicit: true, isLifted: operators[0].IsLifted, isAmbiguous: true, ...Conversion.None...);`.
	if (!mostSpecificSource)
		return Conversions::UserDefinedConversion(operators[0].Method, /*isImplicit*/ true,
			Conversions::None(), Conversions::None(), operators[0].IsLifted, /*isAmbiguous*/ true);

	// C# `var mostSpecificTarget = operators.Any(op => op.TargetType.Equals(toType)) ? toType :
	// FindMostEncompassingType(operators.Select(op => op.TargetType));`.
	ITypePtr mostSpecificTarget;
	if (std::any_of(operators.begin(), operators.end(),
	                [&](const OperatorInfo& op) { return op.TargetType->Equals(toType); }))
		mostSpecificTarget = toType.shared_from_this();
	else
	{
		std::vector<ITypePtr> targetTypes;
		targetTypes.reserve(operators.size());
		for (const auto& op : operators)
			targetTypes.push_back(op.TargetType);
		mostSpecificTarget = FindMostEncompassingType(compilation, targetTypes);
	}
	if (!mostSpecificTarget)
	{
		// C# `if (NullableType.IsNullable(toType)) return UserDefinedImplicitConversion(fromResult,
		// fromType, NullableType.GetUnderlyingType(toType)); else return Conversion.UserDefinedConversion(...);`.
		if (IsNullable(toType))
			return UserDefinedImplicitConversion(compilation, fromResult, fromType,
			                                       GetUnderlyingType(toType));
		return Conversions::UserDefinedConversion(operators[0].Method, /*isImplicit*/ true,
			Conversions::None(), Conversions::None(), operators[0].IsLifted, /*isAmbiguous*/ true);
	}

	// C# `var selected = SelectOperator(mostSpecificSource, mostSpecificTarget, operators, true,
	// fromType, toType);` -- both `mostSpecificSource` / `mostSpecificTarget` are non-null here (the
	// null checks above returned), so the derefs are safe.
	auto selected = SelectOperator(compilation, *mostSpecificSource, *mostSpecificTarget,
	                               operators, /*isImplicit*/ true, fromType, toType);
	// C# `if (selected != Conversion.None)` -- pointer-identity against the None singleton.
	if (selected.get() != Conversions::None().get())
	{
		if (selected->IsLifted() && IsNullable(toType))
		{
			// C# `// Prefer A -> B -> B? over A -> A? -> B?` -- recurse on the underlying target;
			// if THAT resolves, prefer it.
			auto other = UserDefinedImplicitConversion(compilation, fromResult, fromType,
			                                           GetUnderlyingType(toType));
			if (other.get() != Conversions::None().get())
				return other;
		}
		return selected;
	}
	else if (IsNullable(toType))
		return UserDefinedImplicitConversion(compilation, fromResult, fromType,
		                                       GetUnderlyingType(toType));
	else
		return Conversions::None();
}

// CSharpConversions.cs line 1079 (C# spec draft-v11 section 10.5.5 "user-defined explicit
// conversions"). See the header for the full contract. The body mirrors the C# verbatim with
// the same conventions as `UserDefinedImplicitConversion` (above) plus the two explicit-only
// divergences: (1) the most-specific-source else-arm filters by `IsEncompassedBy(fromType,
// op.SourceType) || ImplicitConstantExpressionConversion(...)` (the latter guarded by `fromResult
// != nullptr`) then `FindMostEncompassedType`, falling back to `FindMostEncompassingType` over
// all sources; (2) the most-specific-target middle arm filters by `IsEncompassedBy(op.TargetType,
// toType)` then `FindMostEncompassingType`, falling back to `FindMostEncompassedType` over all
// targets. The `A? -> A -> B` tail recursion passes `nullptr` for `fromResult`.
std::shared_ptr<Conversion>
UserDefinedExplicitConversion(const ICompilation& compilation, const ResolveResult* fromResult,
                             IType& fromType, IType& toType)
{
	// C# `if (fromType.Kind == TypeKind.Interface || toType.Kind == TypeKind.Interface) return
	// Conversion.None;`.
	if (fromType.Kind() == TypeKind::Interface || toType.Kind() == TypeKind::Interface)
		return Conversions::None();

	auto operators = GetApplicableConversionOperators(compilation, fromResult, fromType, toType,
	                                                   /*isExplicit*/ true);
	if (operators.empty())
		return Conversions::None();

	// C# most-specific-source reduction. The first arm (`operators.Any(op => op.SourceType.Equals
	// (fromType))`) takes `fromType` directly; the else-arm filters by `IsEncompassedBy(fromType,
	// op.SourceType) || ImplicitConstantExpressionConversion(fromResult, GetUnderlyingType(op.SourceType))`
	// (the latter guarded by `fromResult != nullptr` -- a null `fromResult` makes the
	// constant-expression fallback false, the safe faithful port of the C# `||` short-circuit /
	// NRE-avoidance), then `FindMostEncompassedType` over the filtered set; when the filtered set
	// is empty, falls back to `FindMostEncompassingType` over ALL sources.
	ITypePtr mostSpecificSource;
	if (std::any_of(operators.begin(), operators.end(),
	                [&](const OperatorInfo& op) { return op.SourceType->Equals(fromType); }))
	{
		mostSpecificSource = fromType.shared_from_this();
	}
	else
	{
		std::vector<ITypePtr> sourceTypesEncompassingFrom;
		for (const auto& op : operators)
		{
			if (IsEncompassedBy(compilation, fromType, *op.SourceType)
				|| (fromResult != nullptr
					&& ImplicitConstantExpressionConversion(*fromResult, GetUnderlyingType(*op.SourceType))))
				sourceTypesEncompassingFrom.push_back(op.SourceType);
		}
		if (!sourceTypesEncompassingFrom.empty())
			mostSpecificSource = FindMostEncompassedType(compilation, sourceTypesEncompassingFrom);
		else
		{
			std::vector<ITypePtr> allSourceTypes;
			allSourceTypes.reserve(operators.size());
			for (const auto& op : operators)
				allSourceTypes.push_back(op.SourceType);
			mostSpecificSource = FindMostEncompassingType(compilation, allSourceTypes);
		}
	}
	if (!mostSpecificSource)
		return Conversions::UserDefinedConversion(operators[0].Method, /*isImplicit*/ false,
			Conversions::None(), Conversions::None(), operators[0].IsLifted, /*isAmbiguous*/ true);

	// C# most-specific-target reduction. The first arm takes `toType` directly; the middle arm
	// filters by `IsEncompassedBy(op.TargetType, toType)` then `FindMostEncompassingType`; the
	// else-arm `FindMostEncompassedType` over all targets.
	ITypePtr mostSpecificTarget;
	if (std::any_of(operators.begin(), operators.end(),
	                [&](const OperatorInfo& op) { return op.TargetType->Equals(toType); }))
	{
		mostSpecificTarget = toType.shared_from_this();
	}
	else
	{
		std::vector<ITypePtr> targetTypesEncompassedByTo;
		for (const auto& op : operators)
		{
			if (IsEncompassedBy(compilation, *op.TargetType, toType))
				targetTypesEncompassedByTo.push_back(op.TargetType);
		}
		if (!targetTypesEncompassedByTo.empty())
			mostSpecificTarget = FindMostEncompassingType(compilation, targetTypesEncompassedByTo);
		else
		{
			std::vector<ITypePtr> allTargetTypes;
			allTargetTypes.reserve(operators.size());
			for (const auto& op : operators)
				allTargetTypes.push_back(op.TargetType);
			mostSpecificTarget = FindMostEncompassedType(compilation, allTargetTypes);
		}
	}
	if (!mostSpecificTarget)
	{
		if (IsNullable(toType))
			return UserDefinedExplicitConversion(compilation, fromResult, fromType,
			                                     GetUnderlyingType(toType));
		return Conversions::UserDefinedConversion(operators[0].Method, /*isImplicit*/ false,
			Conversions::None(), Conversions::None(), operators[0].IsLifted, /*isAmbiguous*/ true);
	}

	auto selected = SelectOperator(compilation, *mostSpecificSource, *mostSpecificTarget,
	                               operators, /*isImplicit*/ false, fromType, toType);
	if (selected.get() != Conversions::None().get())
	{
		if (selected->IsLifted() && IsNullable(toType))
		{
			// C# `// Prefer A -> B -> B? over A -> A? -> B?` -- recurse via the IMPLICIT resolution
			// on the underlying target; if THAT resolves, prefer it.
			auto other = UserDefinedImplicitConversion(compilation, fromResult, fromType,
			                                           GetUnderlyingType(toType));
			if (other.get() != Conversions::None().get())
				return other;
		}
		return selected;
	}
	else if (IsNullable(toType))
		return UserDefinedExplicitConversion(compilation, fromResult, fromType,
		                                     GetUnderlyingType(toType));
	else if (IsNullable(fromType))
		// C# `return UserDefinedExplicitConversion(null, NullableType.GetUnderlyingType(fromType),
		// toType);   // A? -> A -> B` -- the `A? -> A -> B` recursion unwraps the from-type and
		// passes a NULL `fromResult` (no `ResolveResult` context for the underlying value).
		return UserDefinedExplicitConversion(compilation, /*fromResult*/ nullptr,
		                                     GetUnderlyingType(fromType), toType);
	else
		return Conversions::None();
}

std::shared_ptr<Conversion>
ImplicitConversion(const ICompilation& compilation, IType& fromType, IType& toType,
                  bool allowUserDefined, bool allowTuple)
{
	// C# spec draft-v11 section 10.2. The private `ImplicitConversion(IType, IType, bool
	// allowUserDefined, bool allowTuple)` overload (CSharpConversions.cs line 166): the standard
	// implicit conversion first (`StandardImplicitConversion(fromType, toType, allowTuple)`),
	// then -- only when no standard implicit conversion exists (`c == Conversion.None`) AND
	// `allowUserDefined` is true -- the user-defined implicit conversion
	// (`UserDefinedImplicitConversion(null, fromType, toType)`).
	//
	// The `allowTuple` parameter threads to `StandardImplicitConversion`'s tuple arm, which is
	// deferred (yields `None` for tuple shapes until the `TupleConversion` machinery lands), so it
	// is effectively ignored for the ported arms -- the faithful port does not thread it to the
	// already-ported `Detail::StandardImplicitConversion` (D523, which has no `allowTuple`
	// parameter; the tuple arm is deferred inside it). The C# `c == Conversion.None` check ports
	// to pointer-identity against the `None` singleton.
	(void)allowTuple;  // the tuple arm is deferred; effectively ignored for the ported arms
	auto c = StandardImplicitConversion(compilation, fromType, toType);
	if (c.get() == Conversions::None().get() && allowUserDefined)
		c = UserDefinedImplicitConversion(compilation, /*fromResult*/ nullptr, fromType, toType);
	return c;
}

// The C# `static IType UnpackExpressionTreeType(IType type)` (CSharpConversions.cs line 1348) --
// the `Expression<T>` wrapper stripper the anonymous-function conversion uses. A
// `ParameterizedType` over the `System.Linq.Expressions.Expression`1` generic definition (arity 1)
// unpacks to its single type argument; any other type passes through unchanged. The C#
// `pt.Name == "Expression"` reads the generic's `Name` (the `ParameterizedType::Name()` delegates
// to `genericType->Name()`); the C# `pt.Namespace == "System.Linq.Expressions"` reads the
// generic's `Namespace`, which the port's `IType` interface does not carry -- the faithful port
// reads it via `pt->GetDefinition()` (the `ParameterizedType::GetDefinition()` delegates to
// `genericType->GetDefinition()`, which is the definition itself for a real `Expression`1`
// generic), with a `nullptr` guard (a `ParameterizedType` over a non-definition generic -- a
// degenerate shape that does not occur for `Expression<T>` -- yields `nullptr`, so the namespace
// check fails and the type passes through, faithfully matching the C# where the generic's
// `Namespace` would be empty). Both the unpacked type argument (owned by the `ParameterizedType`'s
// `typeArgs_` reachable through the input `type`) and the passthrough (the input `type` itself,
// owned by the caller) outlive the call, so the return is a non-owning `const IType&` (the
// D516/D529 non-owning-reference-return precedent).
const IType& UnpackExpressionTreeType(const IType& type)
{
	auto* pt = dynamic_cast<const ParameterizedType*>(&type);
	if (pt != nullptr && pt->TypeParameterCount() == 1 && pt->Name() == "Expression") {
		const ITypeDefinition* def = pt->GetDefinition();
		if (def != nullptr && def->Namespace() == "System.Linq.Expressions") {
			return *pt->GetTypeArgument(0);
		}
	}
	return type;
}

// The C# `Conversion AnonymousFunctionConversion(ResolveResult resolveResult, IType toType)`
// (CSharpConversions.cs line 1280, C# 9.0 spec section 10.7 "anonymous function conversions") --
// the anonymous-function (lambda / anonymous-method) -> delegate-type conversion. The dispatch
// has already verified the resolve result is a `LambdaResolveResult` (the `dynamic_cast`); this
// helper owns the body. See the header doc for the C#-vs-port RTTI split and the
// `CSharpConversions&` threading.
std::shared_ptr<Conversion>
AnonymousFunctionConversion(CSharpConversions& conversions, const LambdaResolveResult& f,
                           const IType& toType)
{
	// C# `if (!f.IsAnonymousMethod) toType = UnpackExpressionTreeType(toType);` -- the expression-tree
	// unpack runs only for lambdas (C# 3.0+); an anonymous method (C# 2.0 `delegate { }`) cannot
	// convert to an expression tree, so the toType is left as-is.
	const IType& effectiveToType = f.IsAnonymousMethod() ? toType : UnpackExpressionTreeType(toType);
	// C# `IMethod d = toType.GetDelegateInvokeMethod(); if (d == null) return Conversion.None;` --
	// the delegate's `Invoke` method (a non-delegate toType, or a delegate with no `Invoke`, yields
	// `nullptr`). `GetDelegateInvokeMethod` is the TypeSystemExtensions free function (D533).
	const IMethod* d = GetDelegateInvokeMethod(effectiveToType);
	if (d == nullptr)
		return Conversions::None();
	// C# `IType[] dParamTypes = new IType[d.Parameters.Count]; for (...) dParamTypes[i] =
	// d.Parameters[i].Type;` -- the delegate's parameter types. `IsValid` takes
	// `const std::vector<ITypePtr>&`, so the port builds owning `ITypePtr` handles from the const
	// `IParameter::Type()` references via `shared_from_this()` + `const_pointer_cast` (the D529
	// precedent -- the type-system objects are shared-managed; the `const` is the accessor
	// contract, not a guarantee). `d->ReturnType()` likewise builds the `dReturnType` handle.
	auto dParams = d->Parameters();
	std::vector<ITypePtr> dParamTypes;
	dParamTypes.reserve(dParams.size());
	for (const IParameter* p : dParams)
		dParamTypes.push_back(std::const_pointer_cast<IType>(p->Type().shared_from_this()));
	ITypePtr dReturnType = std::const_pointer_cast<IType>(d->ReturnType().shared_from_this());
	// C# `if (f.HasParameterList) { ... } else { ... }` -- the parameter-list compatibility.
	if (f.HasParameterList()) {
		// C# `if (d.Parameters.Count != f.Parameters.Count) return Conversion.None;` -- the parameter-
		// count guard (D and F have the same number of parameters when F has a signature).
		auto fParams = f.Parameters();
		if (dParams.size() != fParams.size())
			return Conversions::None();
		if (f.IsImplicitlyTyped()) {
			// C# `if (f.IsImplicitlyTyped) { foreach (IParameter p in d.Parameters) if
			// (p.ReferenceKind != ReferenceKind.None) return Conversion.None; }` -- an implicitly-typed
			// lambda may not convert to a delegate with ref/out/in parameters.
			for (const IParameter* p : dParams)
				if (p->ReferenceKind() != ReferenceKind::None)
					return Conversions::None();
		} else {
			// C# `for (int i = 0; i < f.Parameters.Count; i++) { ... }` -- an explicitly-typed lambda:
			// each delegate parameter has the same `ReferenceKind` and an identity-convertible type
			// as the corresponding lambda parameter. `IdentityConversion` takes `IType&` non-const
			// (the non-const `AcceptVisitor`, D406), so the `pF->Type()` const reference is
			// `const_cast` to `IType&` (the D515/D517 precedent). `*dParamTypes[i]` is `IType&` directly
			// (the `ITypePtr` was built via `const_pointer_cast`, so dereferencing yields a non-const
			// view of the shared-managed type).
			for (size_t i = 0; i < fParams.size(); i++) {
				const IParameter* pD = dParams[i];
				const IParameter* pF = fParams[i];
				if (pD->ReferenceKind() != pF->ReferenceKind())
					return Conversions::None();
				if (!IdentityConversion(*dParamTypes[i], const_cast<IType&>(pF->Type())))
					return Conversions::None();
			}
		}
	} else {
		// C# `foreach (IParameter p in d.Parameters) if (p.ReferenceKind == ReferenceKind.Out)
		// return Conversion.None;` -- a parameter-list-less anonymous method accepts any parameter
		// list, as long as no delegate parameter is `out`.
		for (const IParameter* p : dParams)
			if (p->ReferenceKind() == ReferenceKind::Out)
				return Conversions::None();
	}
	// C# `return f.IsValid(dParamTypes, dReturnType, this);` -- the body-validity verdict. The
	// C# `this` ports to the `conversions` parameter (the D473 abstract-base `IsValid` signature).
	return f.IsValid(dParamTypes, dReturnType, conversions);
}

// The C# synthetic-arguments construction for the method-group conversion (CSharpConversions.cs
// line 1362, the local `args` construction inside `MethodGroupConversion`). See the header doc for
// the three branches (the ref/out/in `ByReferenceResolveResult`, the `dynamic`->`object` plain
// `ResolveResult`, the plain `ResolveResult(parameterType)`). The `args` are the synthetic
// arguments fed to `MethodGroupResolveResult.PerformOverloadResolution` (now ported); this
// helper is a tested-but-not-yet-wired foundation ahead of the `MethodGroupConversion` body.
std::vector<std::shared_ptr<ResolveResult>>
MethodGroupConversionArguments(const ICompilation& compilation, const IMethod& invoke)
{
	auto params = invoke.Parameters();
	std::vector<std::shared_ptr<ResolveResult>> args;
	args.reserve(params.size());
	for (const IParameter* param : params) {
		// C# `IType parameterType = param.Type;` -- the parameter's type. `IParameter::Type()`
		// returns `const IType&` (the IVariable accessor).
		const IType& parameterType = param->Type();
		// C# `if (param.ReferenceKind != ReferenceKind.None && parameterType.Kind == TypeKind.ByReference)`
		// -- the ref/out/in + ByReference-type arm. Both conditions must hold (the conjunction); a
		// ref parameter whose type is NOT a `ByReferenceType`, or a by-value parameter whose type
		// IS a `ByReferenceType`, falls through to the dynamic / plain arms. The `Kind == ByReference`
		// guard guarantees `parameterType` IS a `ByReferenceType` (the only `IType` subclass with that
		// kind), so the `static_cast` is safe (mirrors the C# `(ByReferenceType)parameterType` cast).
		if (param->ReferenceKind() != ReferenceKind::None
		    && parameterType.Kind() == TypeKind::ByReference) {
			// C# `parameterType = ((ByReferenceType)parameterType).ElementType;` -- unwrap the element.
			// `ByReferenceType::Element()` returns `const ITypePtr&` (the shared handle the `ByReferenceType`
			// owns), so a copy is the owning handle to the element directly (no `shared_from_this`).
			ITypePtr elementPtr = static_cast<const ByReferenceType&>(parameterType).Element();
			// C# `args[i] = new ByReferenceResolveResult(parameterType, param.ReferenceKind);` -- the
			// `internal ByReferenceResolveResult(IType, ReferenceKind)` ctor builds the base
			// `ResolveResult` from a fresh `ByReferenceType(elementType)`, faithfully.
			args.push_back(std::make_shared<ByReferenceResolveResult>(
				std::move(elementPtr), param->ReferenceKind()));
		} else if (param->Type().Kind() == TypeKind::Dynamic) {
			// C# `else if (param.Type.Kind == TypeKind.Dynamic) args[i] = new
			// ResolveResult(compilation.FindType(KnownTypeCode.Object));` -- the dynamic erasure: a
			// `dynamic`-typed delegate parameter erases to `object` for the method-group lookup.
			// `compilation.FindType(Object)` returns `const IType&`; the owning `ITypePtr` is obtained via
			// `shared_from_this()` + `const_pointer_cast` (the D529 precedent).
			const IType& objectType = compilation.FindType(KnownTypeCode::Object);
			args.push_back(std::make_shared<ResolveResult>(
				std::const_pointer_cast<IType>(objectType.shared_from_this())));
		} else {
			// C# `else args[i] = new ResolveResult(parameterType);` -- the plain arm. The owning
			// `ITypePtr` is obtained from the `const IType&` via `shared_from_this()` +
			// `const_pointer_cast` (the D529 precedent).
			args.push_back(std::make_shared<ResolveResult>(
				std::const_pointer_cast<IType>(parameterType.shared_from_this())));
		}
	}
	return args;
}

// The per-element conversion the two `TupleConversion` overloads feed each element pair to. The C#
// `TupleConversion` calls `this.ImplicitConversion(fromEl, toEl)` (isExplicit false) or
// `this.ExplicitConversion(fromEl, toEl)` (isExplicit true) -- the public IType-based methods. The
// port uses the UNCACHED `Detail::` free-function equivalents rather than the cached
// `CSharpConversions::Get(compilation).ImplicitConversion(...)` public method: the cache's
// `TypePair` keys are non-owning `const IType*` (the `CSharpConversions.hpp` `TypePair` convention --
// the cached conversions outlive the key because the types are owned by the compilation/type-system
// in the real `CSharpResolver` path), so caching on test-local types (whose `shared_ptr<IType>` are
// destroyed when the test function returns) would dangle -- a later test whose `IType` lands in the
// same hash bucket would dereference the dangling pointer (an access violation). The uncached
// `Detail::ImplicitConversion` / `Detail::ExplicitConversionImpl` / `Detail::UserDefinedExplicitConversion`
// dispatch is functionally identical for the result (the D531 `BetterConversionTarget` precedent).
//
// The C# `this.ExplicitConversion(IType, IType)` (line 298) body is `ImplicitConversion(false, false)`
// first (the implicit check -- an implicit conversion subsumes the explicit one), then
// `ExplicitConversionImpl`, then `UserDefinedExplicitConversion(null, ...)` -- the port replicates
// this via the `Detail::` free functions. The `allowTuple` flag is `false` for the explicit
// per-element call (the C# `this.ExplicitConversion(IType, IType)` calls
// `ImplicitConversion(allowTuple: false)`, NOT the tuple-aware overload) and `true` for the
// implicit per-element call (the C# `this.ImplicitConversion(IType, IType)` calls the cached public
// method which delegates to `ImplicitConversion(allowUserDefined: true, allowTuple: true)`).
std::shared_ptr<Conversion>
ConvertElementForTuple(const ICompilation& compilation, IType& fromEl, IType& toEl, bool isExplicit)
{
	if (isExplicit) {
		// C# `Conversion c = ImplicitConversion(fromType, toType, allowUserDefined: false, allowTuple: false);`
		// -- the implicit check first (an implicit conversion subsumes the explicit one). The `None`
		// check ports to pointer-identity against the `None` singleton.
		auto c = ImplicitConversion(compilation, fromEl, toEl, /*allowUserDefined*/ false, /*allowTuple*/ false);
		if (c.get() != Conversions::None().get())
			return c;
		// C# `c = ExplicitConversionImpl(fromType, toType); if (c != Conversion.None) return c;`
		c = ExplicitConversionImpl(compilation, fromEl, toEl);
		if (c.get() != Conversions::None().get())
			return c;
		// C# `return UserDefinedExplicitConversion(null, fromType, toType);` -- the user-defined
		// explicit fallback. The `null` `fromResult` ports to `nullptr` (the public `ExplicitConversion(IType,
		// IType)` has no `ResolveResult` context).
		return UserDefinedExplicitConversion(compilation, /*fromResult*/ nullptr, fromEl, toEl);
	}
	// C# `return ImplicitConversion(fromEl, toEl);` -- the cached public `ImplicitConversion(IType,
	// IType)` delegates to `ImplicitConversion(allowUserDefined: true, allowTuple: true)`; the port
	// uses the uncached `Detail::` equivalent (the D531 precedent).
	return ImplicitConversion(compilation, fromEl, toEl, /*allowUserDefined*/ true, /*allowTuple*/ true);
}

// The C# `Conversion TupleConversion(TupleResolveResult fromRR, IType toType, bool isExplicit)`
// (CSharpConversions.cs line 1480, C# 9.0 spec sections 10.2.13 + 10.3.6) -- the tuple-literal
// (a `TupleResolveResult`) -> tuple-type conversion. See the header doc for the element flattening
// (source via `fromRR.Elements()`, target via `GetTupleElementTypes`) and the per-element
// `ImplicitConversion` / `ExplicitConversion` dispatch.
std::shared_ptr<Conversion>
TupleConversion(const ICompilation& compilation, const TupleResolveResult& fromRR,
               IType& toType, bool isExplicit)
{
	// C# `var fromElements = fromRR.Elements;` -- the tuple literal's per-element `ResolveResult`s.
	const auto& fromElements = fromRR.Elements();
	// C# `var toElements = TupleType.GetTupleElementTypes(toType);` -- the target's element types.
	// `IsDefault` (the C# `default(ImmutableArray<IType>)` sentinel, not-a-tuple) ports to
	// `!has_value()` (the D539 `std::optional<std::vector<ITypePtr>>` convention).
	auto toElements = GetTupleElementTypes(toType);
	if (!toElements.has_value() || fromElements.size() != toElements->size())
		return Conversions::None();
	std::vector<std::shared_ptr<Conversion>> elementConversions;
	elementConversions.reserve(fromElements.size());
	for (std::size_t i = 0; i < fromElements.size(); i++) {
		// `fromElements[i]->Type()` returns `const IType&` (the ResolveResult accessor), but the
		// per-element dispatch takes `IType&` non-const (the non-const `AcceptVisitor`, D406), so the
		// port `const_cast`s the const reference -- the underlying type-system object is mutable (the
		// accessor's `const` is the contract), the D515/D517/D528 `const_cast` precedent.
		// `(*toElements)[i]` is an `ITypePtr` whose deref yields `IType&` non-const directly.
		IType& fromEl = const_cast<IType&>(fromElements[i]->Type());
		IType& toEl = *(*toElements)[i];
		auto c = ConvertElementForTuple(compilation, fromEl, toEl, isExplicit);
		if (!c->IsValid())
			return Conversions::None();
		elementConversions.push_back(std::move(c));
	}
	return Conversions::TupleConversion(std::move(elementConversions));
}

// The C# `Conversion TupleConversion(IType fromType, IType toType, bool isExplicit)`
// (CSharpConversions.cs line 1506, C# 9.0 spec sections 10.2.13 + 10.3.6) -- the tuple-type ->
// tuple-type conversion (the IType overload). See the header doc for the element flattening (both
// sides via `GetTupleElementTypes`) and the per-element `ImplicitConversion` / `ExplicitConversion`
// dispatch.
std::shared_ptr<Conversion>
TupleConversion(const ICompilation& compilation, IType& fromType, IType& toType, bool isExplicit)
{
	// C# `var fromElements = TupleType.GetTupleElementTypes(fromType);` -- `IsDefaultOrEmpty` ports
	// to `!has_value() || value.empty()` (the D539 convention).
	auto fromElements = GetTupleElementTypes(fromType);
	if (!fromElements.has_value() || fromElements->empty())
		return Conversions::None();
	auto toElements = GetTupleElementTypes(toType);
	if (!toElements.has_value() || fromElements->size() != toElements->size())
		return Conversions::None();
	std::vector<std::shared_ptr<Conversion>> elementConversions;
	elementConversions.reserve(fromElements->size());
	for (std::size_t i = 0; i < fromElements->size(); i++) {
		// `(*fromElements)[i]` / `(*toElements)[i]` are `ITypePtr` whose deref yields `IType&`
		// non-const directly (no `const_cast` needed, unlike the `TupleResolveResult` overload).
		IType& fromEl = *(*fromElements)[i];
		IType& toEl = *(*toElements)[i];
		auto c = ConvertElementForTuple(compilation, fromEl, toEl, isExplicit);
		if (!c->IsValid())
			return Conversions::None();
		elementConversions.push_back(std::move(c));
	}
	return Conversions::TupleConversion(std::move(elementConversions));
}

std::shared_ptr<Conversion>
ImplicitConversion(const ICompilation& compilation, const ResolveResult& resolveResult,
                  IType& toType, bool allowUserDefined, bool allowTuple)
{
	// C# spec draft-v11 section 10.2 "implicit conversions". The private
	// `ImplicitConversion(ResolveResult, IType, bool allowUserDefined, bool allowTuple)` overload
	// (CSharpConversions.cs line 101) -- the ResolveResult-based dispatch core the public
	// `ImplicitConversion(ResolveResult, IType)` (line 143) and `ExplicitConversion(ResolveResult,
	// IType)` (line 281) entry points build on. The dispatch checks the already-ported helpers in
	// spec order; the still-deferred arms (`MethodGroupConversion` / `TupleConversion`) yield
	// `Conversions::None()` until their machinery lands, so a non-matching `ResolveResult` falls
	// through exactly as the C# does when those arms return `Conversion.None`.
	std::shared_ptr<Conversion> c;
	// C# `if (resolveResult.IsCompileTimeConstant) { c = ImplicitEnumerationConversion(...);
	// if (c.IsValid) return c; if (ImplicitConstantExpressionConversion(...)) return ...; }` -- the
	// compile-time-constant arms. `IsValid` is the `Conversion` virtual (false for the `None`
	// singleton, true for any other singleton/factory); the D527 enumeration helper returns a
	// factory (`EnumerationConversion`) when the conversion fires, else `None`.
	if (resolveResult.IsCompileTimeConstant()) {
		c = ImplicitEnumerationConversion(resolveResult, toType);
		if (c->IsValid())
			return c;
		if (ImplicitConstantExpressionConversion(resolveResult, toType))
			return Conversions::ImplicitConstantExpressionConversion();
	}
	// C# 9.0 spec section 10.2.5 -- the interpolated-string arm. The C# `resolveResult is
	// InterpolatedStringResolveResult` ports to a `dynamic_cast` against the `ResolveResult` base
	// (the C# `is` pattern); the `toType.IsKnownType(IFormattable) || toType.IsKnownType(FormattableString)`
	// check ports to the already-ported `IsKnownType` free function (D-something, `TypeSystemExtensions`).
	if (dynamic_cast<const InterpolatedStringResolveResult*>(&resolveResult) != nullptr) {
		if (IsKnownType(toType, KnownTypeCode::IFormattable)
		    || IsKnownType(toType, KnownTypeCode::FormattableString))
			return Conversions::ImplicitInterpolatedStringConversion();
	}
	// C# `if (resolveResult.Type.Kind == TypeKind.Dynamic) return Conversion.ImplicitDynamicConversion;`
	// -- the dynamic arm. `resolveResult.Type()` returns `const IType&`; `Kind()` is the `IType`
	// virtual. The arm fires for any `dynamic`-typed result (a `dynamic` converts implicitly to
	// any type, faithfully matching the C# `dynamic`-erasure semantics).
	if (resolveResult.Type().Kind() == TypeKind::Dynamic)
		return Conversions::ImplicitDynamicConversion();
	// C# `c = AnonymousFunctionConversion(resolveResult, toType); if (c != Conversion.None) return c;`
	// -- the anonymous-function (lambda / anonymous-method) -> delegate-type conversion (C# 9.0
	// spec section 10.7). The dispatch owns the RTTI (the `dynamic_cast` to `LambdaResolveResult`,
	// the D528 interpolated-string / throw-arm precedent); the helper owns the body. The C# `this`
	// (the `CSharpConversions` the public method was called on) ports to
	// `CSharpConversions::Get(compilation)` -- the per-compilation cached singleton (the real
	// `CSharpResolver` path obtains `CSharpConversions` via `Get`, so the dispatch's `Get` returns
	// the SAME instance; a test-constructed instance diverges, but `CSharpConversions` is
	// stateless beyond the compilation + the unused conversion cache, so the divergence is
	// functionally immaterial). `Get` is called only for an actual lambda (the `dynamic_cast`
	// guard), so the non-lambda dispatch paths (the existing constant / interpolated / dynamic /
	// throw / plain-`ResolveResult` arms) never reach it. `toType` is `IType&` non-const in the
	// dispatch signature but binds to the helper's `const IType&` parameter (implicit).
	if (auto* lambdaRR = dynamic_cast<const LambdaResolveResult*>(&resolveResult)) {
		c = AnonymousFunctionConversion(CSharpConversions::Get(compilation), *lambdaRR, toType);
		if (c.get() != Conversions::None().get())
			return c;
	}
	// C# `c = MethodGroupConversion(resolveResult, toType); if (c != Conversion.None) return c;`
	// -- DEFERRED: the method-group conversion needs `MethodGroupResolveResult.PerformOverloadResolution`
	// (now ported) plus `IsDelegateCompatible` (ported as the 3-arg helper and the public entry).
	// The body itself lands in a follow-up iteration; a non-method-group `ResolveResult` falls through.
	// Yields `None` until then.
	// c = MethodGroupConversion(resolveResult, toType);
	// if (c.get() != Conversions::None().get()) return c;
	// C# 9.0 spec section 10.2.16 default literal conversions -- `// TODO` in the C# source; skipped.
	if (resolveResult.IsCompileTimeConstant()) {
		// C# `c = StandardImplicitConversion(resolveResult.Type, toType, allowTuple);` -- the
		// compile-time-constant fallback. `resolveResult.Type()` returns `const IType&` but
		// `StandardImplicitConversion` takes `IType&` non-const (the non-const `AcceptVisitor`, D406),
		// so the port `const_cast`s the const reference -- the underlying type-system object is
		// mutable (the accessor's `const` is the contract, not a guarantee), the D515/D517 precedent.
		// The `allowTuple` parameter is effectively ignored (the tuple arm is deferred inside
		// `StandardImplicitConversion` D523, which has no `allowTuple` parameter).
		IType& fromType = const_cast<IType&>(resolveResult.Type());
		c = StandardImplicitConversion(compilation, fromType, toType);
		if (c.get() != Conversions::None().get())
			return c;
		if (allowUserDefined) {
			// C# `c = UserDefinedImplicitConversion(resolveResult, resolveResult.Type, toType);` --
			// the user-defined fallback. `UserDefinedImplicitConversion` takes `const ResolveResult*`
			// (the nullable pointer, D530) and `IType&` non-const; `&resolveResult` is the non-null
			// pointer (the public entry always has a real `resolveResult`), and the `const_cast` on
			// `resolveResult.Type()` mirrors the `StandardImplicitConversion` call above.
			c = UserDefinedImplicitConversion(compilation, &resolveResult, fromType, toType);
			if (c.get() != Conversions::None().get())
				return c;
		}
	} else {
		// C# `if (allowTuple && resolveResult is TupleResolveResult tupleRR) { c =
		// TupleConversion(tupleRR, toType, isExplicit: false); if (c != Conversion.None) return c; }`
		// -- the tuple-literal -> tuple-type arm (C# 9.0 spec section 10.2.13). The dispatch owns the
		// RTTI (the `dynamic_cast` to `TupleResolveResult`, the D528 interpolated-string / throw-arm
		// precedent); the `Detail::TupleConversion` helper owns the body. `allowTuple` gates the arm
		// (the public `ImplicitConversion(ResolveResult, IType)` calls with `true`; the public
		// `ExplicitConversion(ResolveResult, IType)` calls the implicit check with `false`). A non-tuple
		// `ResolveResult` (the `dynamic_cast` yields `nullptr`) falls through to the throw / IType arms.
		if (allowTuple) {
			if (auto* tupleRR = dynamic_cast<const TupleResolveResult*>(&resolveResult)) {
				c = TupleConversion(compilation, *tupleRR, toType, /*isExplicit*/ false);
				if (c.get() != Conversions::None().get())
					return c;
			}
		}
		// C# 9.0 spec section 10.2.17 -- the throw-expression arm. The C# `resolveResult is
		// ThrowResolveResult` ports to a `dynamic_cast` against the `ResolveResult` base; a throw
		// expression converts implicitly to any type.
		if (dynamic_cast<const ThrowResolveResult*>(&resolveResult) != nullptr)
			return Conversions::ThrowExpressionConversion();
		// C# `if (allowUserDefined && allowTuple) c = ImplicitConversion(resolveResult.Type, toType);
		// else c = ImplicitConversion(resolveResult.Type, toType, allowUserDefined, allowTuple);` --
		// the IType-based fallback. The public-entry path (`allowUserDefined && allowTuple` true)
		// calls the cached `ImplicitConversion(IType, IType)` in C#; the port collapses the cache
		// into the private `ImplicitConversion(IType, IType, bool, bool)` D531 overload (the cache is
		// an instance-level optimization on `CSharpConversions`, not on the `Detail::` free function).
		// `resolveResult.Type()` is `const_cast` to `IType&` for the non-const `AcceptVisitor`.
		c = ImplicitConversion(compilation, const_cast<IType&>(resolveResult.Type()), toType,
		                      allowUserDefined, allowTuple);
	}
	return c;
}

// The C# `bool IsDelegateCompatible(IMethod m, IMethod d, bool isExtensionMethodInvocation)`
// (CSharpConversions.cs line 1457, C# spec draft-v11 section 21.4 "delegate compatibility").
// Tests a method `m` against a delegate invoke method `d`: the parameter count must match
// (skipping `m`'s first parameter when `isExtensionMethodInvocation` -- the `this` the extension
// syntax supplies), each corresponding parameter's `ReferenceKind` must match, a ref/out/in
// parameter must have an identity conversion on the types (Roslyn relaxes the spec's same-type
// requirement to identity), a by-value parameter must have an identity OR implicit reference
// conversion from `d`'s parameter type to `m`'s, the `ReturnTypeIsRefReadOnly` flags must
// match, and the return type must have an identity OR implicit reference conversion from `m`'s
// to `d`'s. Returns `bool` (the C# `bool`, not a `Conversion`).
//
// The C# `throw new ArgumentNullException` for a null `m`/`d` is N/A: C++ references are non-null
// by contract. The parameter/return `Type()` accessors return `const IType&`, but
// `IdentityConversion` and `IsImplicitReferenceConversion` take `IType&` non-const (the
// non-const `AcceptVisitor`, D406), so the port `const_cast`s the const references -- the
// underlying type-system objects are mutable (the accessor's `const` is the contract, not a
// guarantee), the D515/D517 `const_cast` precedent. Delegates only to the already-ported
// `IdentityConversion` (D514) and `IsImplicitReferenceConversion` (D517), so it is pure given a
// compilation and lands as a `Detail::` free function.
bool IsDelegateCompatible(const ICompilation& compilation, const IMethod& m,
                           const IMethod& d, bool isExtensionMethodInvocation)
{
	int firstParameterInM = isExtensionMethodInvocation ? 1 : 0;
	auto mParams = m.Parameters();
	auto dParams = d.Parameters();
	if (static_cast<int>(mParams.size()) - firstParameterInM != static_cast<int>(dParams.size()))
		return false;
	for (int i = 0; i < static_cast<int>(dParams.size()); i++) {
		const IParameter* pm = mParams[firstParameterInM + i];
		const IParameter* pd = dParams[i];
		// ret/out/in must match
		if (pm->ReferenceKind() != pd->ReferenceKind())
			return false;
		if (pm->ReferenceKind() != ReferenceKind::None) {
			// ref/out/in parameters must have identity conversions on the types (Roslyn relaxes
			// the spec's same-type requirement to identity).
			if (!IdentityConversion(const_cast<IType&>(pd->Type()),
			                        const_cast<IType&>(pm->Type())))
				return false;
		} else {
			// non-ref/out parameters must have an identity or reference conversion from pd to pm.
			IType& pdType = const_cast<IType&>(pd->Type());
			IType& pmType = const_cast<IType&>(pm->Type());
			if (!IdentityConversion(pdType, pmType)
			    && !IsImplicitReferenceConversion(compilation, pdType, pmType))
				return false;
		}
	}
	if (m.ReturnTypeIsRefReadOnly() != d.ReturnTypeIsRefReadOnly())
		return false;
	// check return type compatibility: an identity or implicit reference conversion from m's
	// return type to d's.
	IType& mRet = const_cast<IType&>(m.ReturnType());
	IType& dRet = const_cast<IType&>(d.ReturnType());
	return IdentityConversion(mRet, dRet) || IsImplicitReferenceConversion(compilation, mRet, dRet);
}

// The C# `bool IsBetterIntegralType(TypeCode t1, TypeCode t2)` (CSharpConversions.cs line 1697,
// C# 9.0 spec section 12.6.4.7 "better conversion target" -- the integral-type tiebreak). The C#
// rule: signed integral types are better conversion targets than unsigned integral types when
// the signed type's range fully overlaps the unsigned type's. The `switch (t1)` ports to a C++
// `switch` on the scoped `TypeCode` enum; the `t2 == TypeCode.X` comparisons port to `==` on the
// scoped enum. Pure (reads only the two `TypeCode` values), so it lands as a `Detail::` free
// function taking `TypeCode` by value.
bool IsBetterIntegralType(TypeCode t1, TypeCode t2)
{
	// signed types are better than unsigned types
	switch (t1) {
		case TypeCode::SByte:
			return t2 == TypeCode::Byte || t2 == TypeCode::UInt16 || t2 == TypeCode::UInt32 || t2 == TypeCode::UInt64;
		case TypeCode::Int16:
			return t2 == TypeCode::UInt16 || t2 == TypeCode::UInt32 || t2 == TypeCode::UInt64;
		case TypeCode::Int32:
			return t2 == TypeCode::UInt32 || t2 == TypeCode::UInt64;
		case TypeCode::Int64:
			return t2 == TypeCode::UInt64;
		default:
			return false;
	}
}

// The C# `static IType UnpackTask(IType type)` (CSharpConversions.cs line 1654) -- the
// `Task<T>` unpacker the `BetterConversionTarget` recursion uses. Returns `type.TypeArguments[0]`
// when `type` is a generic task-like (`IsTask(type) || IsCustomTask(type, out _)`) with exactly
// one type parameter, else a null `ITypePtr`. See the header for the rationale and the
// `ParameterizedType`/`GetTypeArgument(0)` ownership convention.
ITypePtr UnpackTask(const IType& type)
{
	// C# `(TaskType.IsTask(type) || TaskType.IsCustomTask(type, out _)) && type.TypeParameterCount
	// == 1` -- the task-like + 1-type-param guard. `IsTask` / `IsCustomTask` take `const IType&`;
	// `TypeParameterCount()` is a const `IType` virtual. The `IsCustomTask` call discards the
	// builder type via a local `ITypePtr` (the C# `out _`).
	ITypePtr discardedBuilder; // the C# `out _` discards the builder type.
	bool isTaskLike = IsTask(type) || IsCustomTask(type, discardedBuilder);
	if (!(isTaskLike && type.TypeParameterCount() == 1))
		return ITypePtr(); // null -- non-task or non-generic task.
	// C# `? type.TypeArguments[0] : null` -- `TypeArguments[0]` is `ParameterizedType`-specific
	// (not on the `IType` surface), so the port `dynamic_cast`s + guards before `GetTypeArgument(0)`
	// (a 1-type-param task-like is always parameterized in practice; the guard is the defensive
	// null-check convention, the D516 precedent). `GetTypeArgument(0)` returns a co-owning
	// `ITypePtr` copy from the `ParameterizedType`'s `typeArgs_`.
	const ParameterizedType* pt = dynamic_cast<const ParameterizedType*>(&type);
	if (pt != nullptr)
		return pt->GetTypeArgument(0);
	return ITypePtr(); // null -- degenerate non-parameterized 1-type-param task-like (does not occur).
}

// The C# `int BetterConversionTarget(IType t1, IType t2)` (CSharpConversions.cs line 1660, C# 9.0
// spec section 12.6.4.7 "better conversion target"). Returns 0 (neither), 1 (t1), or 2 (t2). The
// `ReadOnlySpan`/`Span` tiebreak arms, the core implicit-convertibility check, the `UnpackTask`
// recursion, and the integral-type tiebreak are checked in order; the first verdict wins. See
// the header for the `UnpackTask` rationale and the `TypeArguments[0]` `dynamic_cast` guard.
int BetterConversionTarget(const ICompilation& compilation, IType& t1, IType& t2)
{
	// A small helper to extract the first type argument of a `ReadOnlySpan<T>` / `Span<T>` --
	// the C# `t.TypeArguments[0]` where `t` is a `ParameterizedType`. The port's `TypeArguments()`
	// is `ParameterizedType`-specific (not on the `IType` surface), so the `dynamic_cast` + guard
	// avoids UB on a degenerate stub. Returns null when `t` is not a parameterized type or carries
	// no type arguments; the caller guards before dereferencing.
	auto firstTypeArg = [](IType& t) -> IType* {
		auto* pt = dynamic_cast<ParameterizedType*>(&t);
		if (pt == nullptr || pt->TypeArguments().empty())
			return nullptr;
		return pt->TypeArguments()[0].get();
	};

	// C# `if (t1.IsKnownType(KnownTypeCode.ReadOnlySpanOfT)) { if (t2.IsKnownType(SpanOfT)) { if
	// (IdentityConversion(t1.TypeArguments[0], t2.TypeArguments[0])) return 1; } if (t2.IsKnownType(
	// ReadOnlySpanOfT)) { ... if (t1To2 && !t2To1) return 1; } }` -- the ReadOnlySpan-vs-Span identity
	// arm and the ReadOnlySpan-vs-ReadOnlySpan implicit-convertibility arm (t1 is better).
	if (IsKnownType(t1, KnownTypeCode::ReadOnlySpanOfT)) {
		if (IsKnownType(t2, KnownTypeCode::SpanOfT)) {
			IType* a1 = firstTypeArg(t1);
			IType* a2 = firstTypeArg(t2);
			if (a1 != nullptr && a2 != nullptr && IdentityConversion(*a1, *a2))
				return 1;
		}
		if (IsKnownType(t2, KnownTypeCode::ReadOnlySpanOfT)) {
			IType* a1 = firstTypeArg(t1);
			IType* a2 = firstTypeArg(t2);
			if (a1 != nullptr && a2 != nullptr) {
				bool t1To2 = ImplicitConversion(compilation, *a1, *a2, true, true)->IsValid();
				bool t2To1 = ImplicitConversion(compilation, *a2, *a1, true, true)->IsValid();
				if (t1To2 && !t2To1)
					return 1;
			}
		}
	}
	// C# `if (t2.IsKnownType(KnownTypeCode.ReadOnlySpanOfT)) { if (t1.IsKnownType(SpanOfT)) { if
	// (IdentityConversion(t2.TypeArguments[0], t1.TypeArguments[0])) return 2; } if (t1.IsKnownType(
	// ReadOnlySpanOfT)) { ... if (t2To1 && !t1To2) return 2; } }` -- the mirror arms (t2 is better).
	if (IsKnownType(t2, KnownTypeCode::ReadOnlySpanOfT)) {
		if (IsKnownType(t1, KnownTypeCode::SpanOfT)) {
			IType* a1 = firstTypeArg(t1);
			IType* a2 = firstTypeArg(t2);
			if (a1 != nullptr && a2 != nullptr && IdentityConversion(*a2, *a1))
				return 2;
		}
		if (IsKnownType(t1, KnownTypeCode::ReadOnlySpanOfT)) {
			IType* a1 = firstTypeArg(t1);
			IType* a2 = firstTypeArg(t2);
			if (a1 != nullptr && a2 != nullptr) {
				bool t1To2 = ImplicitConversion(compilation, *a1, *a2, true, true)->IsValid();
				bool t2To1 = ImplicitConversion(compilation, *a2, *a1, true, true)->IsValid();
				if (t2To1 && !t1To2)
					return 2;
			}
		}
	}
	// C# `{ bool t1To2 = ImplicitConversion(t1, t2).IsValid; bool t2To1 = ImplicitConversion(t2,
	// t1).IsValid; if (t1To2 && !t2To1) return 1; if (t2To1 && !t1To2) return 2; }` -- the core
	// implicit-convertibility check. The `ImplicitConversion(t1, t2)` is the C# cached public
	// overload; the port uses `Detail::ImplicitConversion(*compilation, ..., true, true)` (the
	// uncached IType-based dispatch, D531; the result is identical to the cached path).
	{
		bool t1To2 = ImplicitConversion(compilation, t1, t2, true, true)->IsValid();
		bool t2To1 = ImplicitConversion(compilation, t2, t1, true, true)->IsValid();
		if (t1To2 && !t2To1)
			return 1;
		if (t2To1 && !t1To2)
			return 2;
	}
	// C# `var s1 = UnpackTask(t1); var s2 = UnpackTask(t2); if (s1 != null && s2 != null) return
	// BetterConversionTarget(s1, s2);` -- the `Task<T>` recursion: when BOTH targets unpack to a
	// non-null inner type (both are `Task<T>`-shaped), recurse on the inner types. A non-`Task`
	// target (or a non-generic `Task`) unpacks to null, so the guard skips the recursion and the
	// dispatch falls to the integral-type tiebreak (the faithful fallback for the common case).
	ITypePtr s1 = UnpackTask(t1);
	ITypePtr s2 = UnpackTask(t2);
	if (s1 && s2)
		return BetterConversionTarget(compilation, *s1, *s2);
	// C# `TypeCode t1Code = ReflectionHelper.GetTypeCode(t1); TypeCode t2Code =
	// ReflectionHelper.GetTypeCode(t2); if (IsBetterIntegralType(t1Code, t2Code)) return 1; if
	// (IsBetterIntegralType(t2Code, t1Code)) return 2; return 0;` -- the integral-type tiebreak.
	TypeCode t1Code = GetTypeCode(t1);
	TypeCode t2Code = GetTypeCode(t2);
	if (IsBetterIntegralType(t1Code, t2Code))
		return 1;
	if (IsBetterIntegralType(t2Code, t1Code))
		return 2;
	return 0;
}

// The C# `bool IsExactlyMatching(ResolveResult e, IType t)` (CSharpConversions.cs line 1615, C# 8.0
// spec section 12.6.4.6 "exactly matching expression"). See the header doc for the C# body and the
// purity rationale. The lambda arm builds the delegate invoke's parameter types as owning
// `ITypePtr` handles (the `GetInferredReturnType` signature) and the delegate return type as an
// owning `ITypePtr`, both via `shared_from_this()` + `const_pointer_cast` (the D529 / D534
// `AnonymousFunctionConversion` precedent -- the type-system objects are shared-managed; the
// `const` is the accessor contract, not a guarantee).
bool IsExactlyMatching(const ResolveResult& e, const IType& t)
{
	// C# `var s = e.Type; if (IdentityConversion(s, t)) return true;` -- the identity check on the
	// resolve result's type. `e.Type()` returns `const IType&`; `IdentityConversion` takes `IType&`
	// non-const (the non-const `AcceptVisitor`, D406), so the port `const_cast`s both sides (the
	// underlying type-system objects are mutable; the accessor's `const` is the contract, the
	// D515/D517 `const_cast` precedent). For a `LambdaResolveResult` the type is `NoType` (the
	// lambda has no type), so this is false for a lambda unless `t` is also `NoType`.
	const IType& s = e.Type();
	if (IdentityConversion(const_cast<IType&>(s), const_cast<IType&>(t)))
		return true;
	// C# `if (e is LambdaResolveResult lambda) { ... } else return false;` -- the lambda arm. A
	// non-lambda resolve result whose type is not identity-convertible to `t` does not exactly
	// match.
	const LambdaResolveResult* lambda = dynamic_cast<const LambdaResolveResult*>(&e);
	if (lambda == nullptr)
		return false;
	// C# `if (!lambda.IsAnonymousMethod) t = UnpackExpressionTreeType(t);` -- the expression-tree
	// unwrap runs only for lambdas (C# 3.0+); an anonymous method (C# 2.0 `delegate { }`) cannot
	// convert to an expression tree, so the toType is left as-is. A C++ reference cannot be
	// rebound, so the port tracks the effective toType via a `const IType*` pointer that is
	// rebound through the unwrap.
	const IType* effectiveT = &t;
	if (!lambda->IsAnonymousMethod())
		effectiveT = &UnpackExpressionTreeType(*effectiveT);
	// C# `IMethod m = t.GetDelegateInvokeMethod(); if (m == null) return false;` -- the delegate's
	// `Invoke` method (a non-delegate toType, or a delegate with no `Invoke`, yields `nullptr`).
	// `GetDelegateInvokeMethod` is the TypeSystemExtensions free function (D533).
	const IMethod* m = GetDelegateInvokeMethod(*effectiveT);
	if (m == nullptr)
		return false;
	// C# `IType[] parameterTypes = new IType[m.Parameters.Count]; for (...) parameterTypes[i] =
	// m.Parameters[i].Type;` -- the delegate invoke's parameter types, fed to `GetInferredReturnType`.
	// `GetInferredReturnType` takes `const std::vector<ITypePtr>&`, so the port builds owning
	// `ITypePtr` handles from the const `IParameter::Type()` references via `shared_from_this()` +
	// `const_pointer_cast` (the D529 / D534 `AnonymousFunctionConversion` precedent).
	auto params = m->Parameters();
	std::vector<ITypePtr> parameterTypes;
	parameterTypes.reserve(params.size());
	for (const IParameter* p : params)
		parameterTypes.push_back(std::const_pointer_cast<IType>(p->Type().shared_from_this()));
	// C# `var x = lambda.GetInferredReturnType(parameterTypes); var y = m.ReturnType;` -- the
	// inferred return type and the delegate invoke's return type. Both held as owning `ITypePtr`
	// (`x` directly from `GetInferredReturnType`; `y` built from the const `m->ReturnType()`
	// reference via `shared_from_this` + `const_pointer_cast`).
	ITypePtr x = lambda->GetInferredReturnType(parameterTypes);
	ITypePtr y = std::const_pointer_cast<IType>(m->ReturnType().shared_from_this());
	// C# `if (IdentityConversion(x, y)) return true;` -- `x` / `y` are non-null in practice (the
	// real `GetInferredReturnType` / `IMember::ReturnType` never return null); the guard avoids UB
	// on a degenerate stub returning null (the safe faithful fallback -- the C# would NRE on a
	// null, which never occurs in practice; skipping the check falls through to the async arm).
	if (x && y && IdentityConversion(*x, *y))
		return true;
	// C# `if (lambda.IsAsync) { x = UnpackTask(x); y = UnpackTask(y); }` -- for an async lambda,
	// unpack the `Task<T>` wrapper from both the inferred return and the delegate return.
	// `UnpackTask(null)` returns null (the C# `IsTask(null)` is false), so a null input stays null;
	// the port guards the deref to avoid UB (a null `x` / `y` stays null, faithfully matching the
	// C# `UnpackTask(null) == null`).
	if (lambda->IsAsync()) {
		if (x)
			x = UnpackTask(*x);
		if (y)
			y = UnpackTask(*y);
	}
	// C# `if (x != null && y != null) return IsExactlyMatching(new ResolveResult(x), y); return
	// false;` -- the recursion: re-wrap the (possibly unpacked) inferred return type in a fresh
	// `ResolveResult` and check whether it exactly matches the (possibly unpacked) delegate
	// return type. A null `x` or `y` (e.g. an async return that did not unpack to a `Task<T>`)
	// yields false. The fresh `ResolveResult` is a plain (non-lambda) resolve result, so the
	// recursion reduces to the `IdentityConversion(s, t)` check on the unpacked types.
	if (x && y) {
		auto rr = std::make_shared<ResolveResult>(x);
		return IsExactlyMatching(*rr, *y);
	}
	return false;
}

// The C# `public bool IsConstraintConvertible(IType fromType, IType toType)` (CSharpConversions.cs
// line 261, C# spec section 8.4.5 "satisfying constraints"). Delegates to the already-ported helpers
// in spec order: identity, implicit reference, the nullable-vs-boxing branch (nullable from-type ->
// the `object`-constraint special case; non-nullable from-type -> boxing), then implicit type-
// parameter. The C# `throw new ArgumentNullException` on null args compiles out (the `IType&`
// references cannot bind to null, the D374 convention). The C# `ImplicitReferenceConversion(
// fromType, toType, 0)` is the private recursive worker at depth 0 (not the public
// `IsImplicitReferenceConversion` which delegates to the worker at depth 0 -- both produce the
// same result); the port calls `Detail::ImplicitReferenceConversion(compilation, ..., 0)`
// faithfully. The `NullableType.IsNullable(fromType)` / `IsKnownType(toType, Object)` take
// `const IType&` but a non-const `IType&` binds to `const IType&` trivially. Returns `bool`.
bool IsConstraintConvertible(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                            ILSpy::Decompiler::TypeSystem::IType& fromType,
                            ILSpy::Decompiler::TypeSystem::IType& toType)
{
	// C# `if (IdentityConversion(fromType, toType)) return true;` -- the identity conversion (D514).
	if (IdentityConversion(fromType, toType))
		return true;
	// C# `if (ImplicitReferenceConversion(fromType, toType, 0)) return true;` -- the implicit reference
	// conversion (D517, the private recursive worker at depth 0).
	if (ImplicitReferenceConversion(compilation, fromType, toType, 0))
		return true;
	// C# `if (NullableType.IsNullable(fromType)) { if (toType.IsKnownType(KnownTypeCode.Object))
	// return true; } else { if (IsBoxingConversion(fromType, toType)) return true; }` -- the nullable
	// branch: a nullable from-type is convertible to `object` (the nullable-value-type-to-object
	// special case the `DefaultResolvedTypeParameter.DirectBaseTypes` `object` constraint inserts);
	// a non-nullable from-type is convertible by boxing (D519).
	if (IsNullable(fromType)) {
		if (IsKnownType(toType, KnownTypeCode::Object))
			return true;
	} else {
		if (IsBoxingConversion(compilation, fromType, toType))
			return true;
	}
	// C# `if (ImplicitTypeParameterConversion(fromType, toType)) return true; return false;` -- the
	// implicit type-parameter conversion (D519).
	if (ImplicitTypeParameterConversion(compilation, fromType, toType))
		return true;
	return false;
}

// The C# `bool IsImplicitSpanConversion(IType fromType, IType toType)` (CSharpConversions.cs line
// 1238, the C# 14.0 first-class-span-types proposal). See the header for the arm-by-arm rationale
// and the `TypeArguments[0]` `dynamic_cast`-plus-guard convention.
bool IsImplicitSpanConversion(const ICompilation& compilation, IType& fromType, IType& toType)
{
	// C# `if (!compilation.TypeSystemOptions.HasFlag(TypeSystemOptions.FirstClassSpanTypes)) return
	// false;` -- the first-class-span-types gate. `HasFlag` is true when all bits of the argument are
	// set; for a single flag this is `(options & flag) == flag`. The C++ `enum class` bitwise `&` is
	// lower precedence than `==`, so the `&` is parenthesized. Without the flag, no span conversion
	// exists (the faithful C# behavior).
	TypeSystemOptions options = compilation.TypeSystemOptions();
	if ((options & TypeSystemOptions::FirstClassSpanTypes) != TypeSystemOptions::FirstClassSpanTypes)
		return false;

	// A small helper to extract the first type argument of a `Span<T>` / `ReadOnlySpan<T>` -- the
	// C# `toType.TypeArguments[0]` where `toType` is a `ParameterizedType`. The port's
	// `TypeArguments()` is `ParameterizedType`-specific (not on the `IType` surface), so the
	// `dynamic_cast` + guard avoids UB on a degenerate stub (the BetterConversionTarget
	// `firstTypeArg` precedent). Returns null when `t` is not a parameterized type or carries no
	// type arguments; the caller guards before dereferencing.
	auto firstTypeArg = [](IType& t) -> IType* {
		auto* pt = dynamic_cast<ParameterizedType*>(&t);
		if (pt == nullptr || pt->TypeArguments().empty())
			return nullptr;
		return pt->TypeArguments()[0].get();
	};

	// C# `switch (fromType) { ... }` -- the pattern-match arms are checked in declaration order; the
	// first match that returns decides; a `break` falls through to the final `return false`.

	// C# `case ArrayType { Dimensions: 1, ElementType: var elementType }:` -- a single-dimensional
	// array. The C# `Dimensions: 1` pattern matches the rank (the number of dimensions); the port
	// checks `arr->Rank() == 1` (`ArrayType::Rank`). `ElementType` is `arr->Element()` whose
	// `shared_ptr` deref yields `IType&` non-const.
	if (auto* arr = dynamic_cast<ArrayType*>(&fromType); arr != nullptr && arr->Rank() == 1) {
		IType& elementType = *arr->Element();
		// C# `if (toType.IsKnownType(KnownTypeCode.SpanOfT)) return IdentityConversion(elementType,
		// toType.TypeArguments[0]);` -- the array-to-`Span<T>` arm (element identity).
		if (IsKnownType(toType, KnownTypeCode::SpanOfT)) {
			IType* toArg = firstTypeArg(toType);
			if (toArg != nullptr && IdentityConversion(elementType, *toArg))
				return true;
			return false;
		}
		// C# `if (toType.IsKnownType(KnownTypeCode.ReadOnlySpanOfT)) return IdentityConversion(
		// elementType, toType.TypeArguments[0]) || IsImplicitReferenceConversion(elementType,
		// toType.TypeArguments[0]);` -- the array-to-`ReadOnlySpan<T>` arm (element identity OR an
		// implicit reference conversion for covariance).
		if (IsKnownType(toType, KnownTypeCode::ReadOnlySpanOfT)) {
			IType* toArg = firstTypeArg(toType);
			if (toArg != nullptr) {
				if (IdentityConversion(elementType, *toArg))
					return true;
				if (IsImplicitReferenceConversion(compilation, elementType, *toArg))
					return true;
			}
			return false;
		}
		// C# `break;` -- neither `Span<T>` nor `ReadOnlySpan<T>`: falls through to the final `return
		// false` (the array-to-other-type direction is not a span conversion).
	}

	// C# `case ParameterizedType pt when pt.IsKnownType(KnownTypeCode.SpanOfT) || pt.IsKnownType(
	// KnownTypeCode.ReadOnlySpanOfT):` -- `Span<T>` / `ReadOnlySpan<T>` to `ReadOnlySpan<T>`. The C#
	// `pt` is already the `ParameterizedType`; the port `dynamic_cast`s once and reads `pt->
	// TypeArguments()[0]` directly (with the empty guard). The `when` filter restricts the arm to
	// span/readonly-span sources.
	if (auto* pt = dynamic_cast<ParameterizedType*>(&fromType); pt != nullptr && !pt->TypeArguments().empty()
		&& (IsKnownType(fromType, KnownTypeCode::SpanOfT) || IsKnownType(fromType, KnownTypeCode::ReadOnlySpanOfT))) {
		IType& fromArg = *pt->TypeArguments()[0];
		// C# `if (toType.IsKnownType(KnownTypeCode.ReadOnlySpanOfT)) return IdentityConversion(
		// pt.TypeArguments[0], toType.TypeArguments[0]) || IsImplicitReferenceConversion(
		// pt.TypeArguments[0], toType.TypeArguments[0]);`
		if (IsKnownType(toType, KnownTypeCode::ReadOnlySpanOfT)) {
			IType* toArg = firstTypeArg(toType);
			if (toArg != nullptr) {
				if (IdentityConversion(fromArg, *toArg))
					return true;
				if (IsImplicitReferenceConversion(compilation, fromArg, *toArg))
					return true;
			}
			return false;
		}
		// C# `break;` -- a span/readonly-span source to a non-`ReadOnlySpan<T>` target: falls through
		// to the final `return false`.
	}

	// C# `case var s when s.IsKnownType(KnownTypeCode.String): return toType.IsKnownType(
	// KnownTypeCode.ReadOnlySpanOfT) && toType.TypeArguments[0].IsKnownType(KnownTypeCode.Char);` --
	// the `string`-to-`ReadOnlySpan<char>` fixed-arm (the only string span conversion; the element
	// type must be exactly `char`). The `toType.TypeArguments[0]` access uses the `firstTypeArg`
	// helper; a missing argument yields false (a real `ReadOnlySpan<T>` always carries the
	// argument).
	if (IsKnownType(fromType, KnownTypeCode::String)) {
		if (!IsKnownType(toType, KnownTypeCode::ReadOnlySpanOfT))
			return false;
		IType* toArg = firstTypeArg(toType);
		return toArg != nullptr && IsKnownType(*toArg, KnownTypeCode::Char);
	}

	// C# `return false;` -- the default arm (no pattern matched) and every `break`-fallthrough.
	return false;
}

// The C# `public int BetterConversion(IType s, IType t1, IType t2)` (CSharpConversions.cs line
// 1620, C# 4.0 spec section 7.5.3.4 "better conversion from type"). See the header for the rationale.
// The body is lifted verbatim from the C# public method: an identity conversion from the source
// `s` to a target beats a non-identity conversion; when neither (or both) is identity, the verdict
// falls to `BetterConversionTarget`. The public `CSharpConversions::BetterConversion(IType, IType,
// IType)` method delegates to this free function, and the ResolveResult-based `BetterConversion`
// overload below calls it for its recursion (the C# calls the public IType overload; the port
// uses the uncached Detail dispatch, the `BetterConversionTarget` precedent).
int BetterConversion(const ICompilation& compilation, IType& s, IType& t1, IType& t2)
{
	// C# `bool ident1 = IdentityConversion(s, t1); bool ident2 = IdentityConversion(s, t2);` -- the
	// identity checks (D514). Both `s` and the targets are non-const `IType&` (the non-const
	// `AcceptVisitor`, D406).
	bool ident1 = IdentityConversion(s, t1);
	bool ident2 = IdentityConversion(s, t2);
	// C# `if (ident1 && !ident2) return 1; if (ident2 && !ident1) return 2;` -- an identity conversion
	// to a target beats a non-identity conversion.
	if (ident1 && !ident2)
		return 1;
	if (ident2 && !ident1)
		return 2;
	// C# `return BetterConversionTarget(t1, t2);` -- neither (or both) is identity: the verdict
	// falls to the better-conversion-target worker (D535), threading the compilation to its
	// `ImplicitConversion` calls.
	return BetterConversionTarget(compilation, t1, t2);
}

// The C# `public int BetterConversion(ResolveResult resolveResult, IType t1, IType t2)`
// (CSharpConversions.cs line 1540, C# 8.0 spec section 12.6.4.5 "better conversion from
// expression"). See the header for the arm-by-arm rationale and the purity/ownership conventions.
int BetterConversion(const ICompilation& compilation, const ResolveResult& resolveResult,
                     IType& t1, IType& t2)
{
	// C# `bool t1Exact = IsExactlyMatching(resolveResult, t1); bool t2Exact = IsExactlyMatching(
	// resolveResult, t2);` -- the exactly-matching checks (D543). `IsExactlyMatching` takes
	// `const ResolveResult&` + `const IType&`, so the non-const `t1` / `t2` bind directly.
	bool t1Exact = IsExactlyMatching(resolveResult, t1);
	bool t2Exact = IsExactlyMatching(resolveResult, t2);
	// C# `if (t1Exact && !t2Exact) return 1; if (t2Exact && !t1Exact) return 2;` -- an exactly-matching
	// target beats a non-exactly-matching one.
	if (t1Exact && !t2Exact)
		return 1;
	if (t2Exact && !t1Exact)
		return 2;
	// C# `if (!t1Exact && !t2Exact) { bool c1ImplicitSpanConversion = IsImplicitSpanConversion(
	// resolveResult.Type, t1); bool c2ImplicitSpanConversion = IsImplicitSpanConversion(
	// resolveResult.Type, t2); if (c1ImplicitSpanConversion && !c2ImplicitSpanConversion) return 1;
	// if (c2ImplicitSpanConversion && !c1ImplicitSpanConversion) return 2; }` -- the implicit-span
	// tiebreak, fired only when NEITHER target exactly matches. `IsImplicitSpanConversion` (D538)
	// takes `IType&` non-const (the non-const `AcceptVisitor`), but `resolveResult.Type()` returns
	// `const IType&`, so the port `const_cast`s it (the underlying type-system object is mutable,
	// the accessor's `const` is the contract, the D515/D517/D528 `const_cast` precedent).
	if (!t1Exact && !t2Exact) {
		bool c1ImplicitSpanConversion = IsImplicitSpanConversion(compilation,
			const_cast<IType&>(resolveResult.Type()), t1);
		bool c2ImplicitSpanConversion = IsImplicitSpanConversion(compilation,
			const_cast<IType&>(resolveResult.Type()), t2);
		if (c1ImplicitSpanConversion && !c2ImplicitSpanConversion)
			return 1;
		if (c2ImplicitSpanConversion && !c1ImplicitSpanConversion)
			return 2;
	}
	// C# `if (t1Exact == t2Exact) { int r = BetterConversionTarget(t1, t2); if (r != 0) return r; }`
	// -- when both (or neither) exactly match, the verdict falls to the better-conversion-target
	// worker (D535); a zero verdict falls through to the lambda/non-lambda arms below.
	if (t1Exact == t2Exact) {
		int r = BetterConversionTarget(compilation, t1, t2);
		if (r != 0)
			return r;
	}
	// C# `LambdaResolveResult lambda = resolveResult as LambdaResolveResult; if (lambda != null)
	// { ... } else { return BetterConversion(resolveResult.Type, t1, t2); }` -- the RTTI dispatch.
	const LambdaResolveResult* lambda = dynamic_cast<const LambdaResolveResult*>(&resolveResult);
	if (lambda != nullptr) {
		// C# `if (!lambda.IsAnonymousMethod) { t1 = UnpackExpressionTreeType(t1); t2 =
		// UnpackExpressionTreeType(t2); }` -- the expression-tree unwrap runs only for lambdas (C#
		// 3.0+); an anonymous method (C# 2.0 `delegate { }`) cannot convert to an expression tree.
		// A C++ reference cannot be rebound, so the port tracks the effective t1/t2 via `const IType*`
		// pointers rebound through the unwrap. `GetDelegateInvokeMethod` (D533) takes `const IType&`,
		// so the rebound pointers feed it directly.
		const IType* effectiveT1 = &t1;
		const IType* effectiveT2 = &t2;
		if (!lambda->IsAnonymousMethod()) {
			effectiveT1 = &UnpackExpressionTreeType(*effectiveT1);
			effectiveT2 = &UnpackExpressionTreeType(*effectiveT2);
		}
		// C# `IMethod m1 = t1.GetDelegateInvokeMethod(); IMethod m2 = t2.GetDelegateInvokeMethod();
		// if (m1 == null || m2 == null) return 0;` -- resolve the delegates' `Invoke` methods. A
		// non-delegate target (or a delegate with no `Invoke`) yields `nullptr` -> the lambda arm
		// returns 0 (no better target).
		const IMethod* m1 = GetDelegateInvokeMethod(*effectiveT1);
		const IMethod* m2 = GetDelegateInvokeMethod(*effectiveT2);
		if (m1 == nullptr || m2 == nullptr)
			return 0;
		// C# `if (m1.Parameters.Count != m2.Parameters.Count) return 0;` -- the parameter-count
		// match (the two delegates must take the same number of arguments).
		if (m1->Parameters().size() != m2->Parameters().size())
			return 0;
		// C# `IType[] parameterTypes = new IType[m1.Parameters.Count]; for (...) { parameterTypes[i] =
		// m1.Parameters[i].Type; if (!parameterTypes[i].Equals(m2.Parameters[i].Type)) return 0; }`
		// -- the per-parameter type match. `GetInferredReturnType` takes `const vector<ITypePtr>&`,
		// so the port builds owning `ITypePtr` handles from the const `IParameter::Type()` references
		// via `shared_from_this()` + `const_pointer_cast` (the D529 / D534 `AnonymousFunctionConversion`
		// precedent). The `.Equals` is `IType::Equals(const IType&)` (structural equality; a
		// `LookupTypeDefinition` is identity-equal, so the crux tests reuse the same instance).
		std::vector<ITypePtr> parameterTypes;
		parameterTypes.reserve(m1->Parameters().size());
		for (size_t i = 0; i < m1->Parameters().size(); i++) {
			parameterTypes.push_back(std::const_pointer_cast<IType>(
				m1->Parameters()[i]->Type().shared_from_this()));
			if (!parameterTypes[i]->Equals(m2->Parameters()[i]->Type()))
				return 0;
		}
		// C# `if (lambda.HasParameterList && parameterTypes.Length != lambda.Parameters.Count)
		// return 0;` -- a lambda with an explicit parameter list must list the same number of
		// parameters as the delegate's `Invoke`.
		if (lambda->HasParameterList() && parameterTypes.size() != lambda->Parameters().size())
			return 0;
		// C# `IType ret1 = m1.ReturnType; IType ret2 = m2.ReturnType;` -- the delegates' return
		// types. Held as owning `ITypePtr` (the locals are reassigned to `UnpackTask` results below;
		// the const `IMember::ReturnType()` references are built into handles via `shared_from_this`
		// + `const_pointer_cast`, the D529 precedent).
		ITypePtr ret1 = std::const_pointer_cast<IType>(m1->ReturnType().shared_from_this());
		ITypePtr ret2 = std::const_pointer_cast<IType>(m2->ReturnType().shared_from_this());
		// C# `if (ret1.Kind == TypeKind.Void && ret2.Kind != TypeKind.Void) return 2; if (ret1.Kind !=
		// TypeKind.Void && ret2.Kind == TypeKind.Void) return 1;` -- a void-returning delegate is
		// worse than a non-void-returning one (the non-void target is better, since a void lambda
		// cannot satisfy a non-void delegate and vice versa).
		if (ret1->Kind() == TypeKind::Void && ret2->Kind() != TypeKind::Void)
			return 2;
		if (ret1->Kind() != TypeKind::Void && ret2->Kind() == TypeKind::Void)
			return 1;
		// C# `IType inferredRet = lambda.GetInferredReturnType(parameterTypes); int r =
		// BetterConversion(inferredRet, ret1, ret2);` -- the better conversion of the lambda's
		// inferred return type to the two delegate return types. `GetInferredReturnType` returns
		// `ITypePtr` directly; the `*inferredRet` / `*ret1` / `*ret2` derefs yield `IType&` non-const
		// (the `shared_ptr<IType>` deref), feeding the IType `BetterConversion` overload without a
		// `const_cast`. The recursion is the IType overload (the C# `inferredRet` is an `IType`, not
		// a `ResolveResult`), so it terminates (no recursion back to this ResolveResult overload).
		ITypePtr inferredRet = lambda->GetInferredReturnType(parameterTypes);
		int r = BetterConversion(compilation, *inferredRet, *ret1, *ret2);
		// C# `if (r == 0 && lambda.IsAsync) { ret1 = UnpackTask(ret1); ret2 = UnpackTask(ret2);
		// inferredRet = UnpackTask(inferredRet); if (ret1 != null && ret2 != null && inferredRet !=
		// null) r = BetterConversion(inferredRet, ret1, ret2); }` -- for an async lambda, unpack the
		// `Task<T>` wrapper from all three and recompute. `UnpackTask` (D542) takes `const IType&` and
		// returns `ITypePtr`; the `*ret1` / `*ret2` / `*inferredRet` derefs feed it. A non-`Task<T>`
		// return unpacks to null, so the null guard skips the recompute (faithfully matching the C#
		// `if (ret1 != null && ...)`).
		if (r == 0 && lambda->IsAsync()) {
			ret1 = UnpackTask(*ret1);
			ret2 = UnpackTask(*ret2);
			inferredRet = UnpackTask(*inferredRet);
			if (ret1 && ret2 && inferredRet)
				r = BetterConversion(compilation, *inferredRet, *ret1, *ret2);
		}
		return r;
	} else {
		// C# `return BetterConversion(resolveResult.Type, t1, t2);` -- a non-lambda expression:
		// the better conversion of the expression's TYPE to the two targets (the IType overload).
		// `resolveResult.Type()` returns `const IType&`, `const_cast` to the non-const `IType&` the
		// IType overload takes (the D515/D517/D528 `const_cast` precedent).
		return BetterConversion(compilation, const_cast<IType&>(resolveResult.Type()), t1, t2);
	}
}

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
