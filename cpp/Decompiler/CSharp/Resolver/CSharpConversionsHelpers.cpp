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
#include "Decompiler/TypeSystem/IType.hpp"          // IType (Kind), ITypePtr, AcceptVisitor, Equals, ParameterizedType
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"  // ITypeDefinition (KnownTypeCode -- the UnpackGenericArrayInterface definition arm)
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"   // KnownTypeCode (the array-interface codes UnpackGenericArrayInterface switches on)
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"  // NormalizeTypeVisitor::TypeErasure (IdentityConversion)
#include "Decompiler/TypeSystem/NullableType.hpp"   // IsNullable, GetUnderlyingType (the nullable helpers)
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"  // GetTypeCode, TypeCode
#include "Decompiler/TypeSystem/TypeKind.hpp"       // TypeKind

#include <memory>

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
using ILSpy::Decompiler::TypeSystem::GetTypeCode;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IsNullable;
using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::NormalizeTypeVisitor;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TypeCode;
using ILSpy::Decompiler::TypeSystem::TypeKind;

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

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
