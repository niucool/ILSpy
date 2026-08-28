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

// Out-of-line definitions for the NullableType helpers (see the header).

#include "Decompiler/TypeSystem/NullableType.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp" // IsKnownType, SkipModifiers

#include <optional>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

bool IsNullable(const IType& type)
{
	// C# NullableType.IsNullable: `type.SkipModifiers() as ParameterizedType` then
	// `pt != null && pt.TypeParameterCount == 1 && pt.GenericType.IsKnownType(NullableOfT)`.
	const IType* unwrapped = SkipModifiers(type);
	const ParameterizedType* pt = unwrapped != nullptr
		? dynamic_cast<const ParameterizedType*>(unwrapped)
		: nullptr;
	if (pt == nullptr)
		return false;
	const ITypePtr& generic = pt->GenericType();
	return pt->TypeParameterCount() == 1
		&& generic != nullptr
		&& IsKnownType(*generic, KnownTypeCode::NullableOfT);
}

bool IsNonNullableValueType(const IType& type)
{
	// C# `type.IsReferenceType == false && !IsNullable(type)` -- the `bool? == false` is true
	// only when `IsReferenceType` holds `false` (a `std::nullopt` indeterminate is not `== false`).
	const std::optional<bool> isRef = type.IsReferenceType();
	return isRef.has_value() && *isRef == false && !IsNullable(type);
}

const IType& GetUnderlyingType(const IType& type)
{
	// C# NullableType.GetUnderlyingType: unwrap modifiers, and if the result is a 1-arg
	// `Nullable<T>`, return the type argument; else return the original `type` (modifiers
	// preserved -- the `else` returns `type`, NOT `SkipModifiers(type)`).
	if (const IType* unwrapped = SkipModifiers(type)) {
		if (const ParameterizedType* pt = dynamic_cast<const ParameterizedType*>(unwrapped)) {
			const ITypePtr& generic = pt->GenericType();
			if (pt->TypeParameterCount() == 1
				&& generic != nullptr
				&& IsKnownType(*generic, KnownTypeCode::NullableOfT))
			{
				// The type argument is owned by `pt`'s `typeArgs_` (the `ParameterizedType`
				// reachable through `type`), so the returned reference outlives the call.
				return *pt->GetTypeArgument(0);
			}
		}
	}
	return type;
}

IType& GetUnderlyingType(IType& type)
{
	// Non-const overload -- delegate to the const overload and cast away the added const. Safe
	// because `type` was non-const at the call site, so the object the const overload returns
	// (either the underlying element owned by the `ParameterizedType`'s shared handle, or the
	// caller's own non-const `type`) is not actually const-qualified. See the header comment.
	return const_cast<IType&>(GetUnderlyingType(static_cast<const IType&>(type)));
}

ITypePtr Create(const ICompilation& compilation, const IType& elementType)
{
	// C# NullableType.Create: resolve `System.Nullable`1` via the compilation, and if the
	// result has a definition (the normal case), build `new ParameterizedType(nullableTypeDef,
	// { elementType })`; otherwise return the `FindType` result itself (the defensive fallback
	// for a type system that cannot resolve `Nullable`1`).
	const IType& nullableType = compilation.FindType(KnownTypeCode::NullableOfT);
	const ITypeDefinition* nullableTypeDef = nullableType.GetDefinition();
	if (nullableTypeDef != nullptr) {
		// `nullableTypeDef` IS-an `IType` (via `ITypeDefinitionOrUnknown` -> `IType`); the
		// `ParameterizedType` ctor takes an owning `ITypePtr` for the generic type, so obtain a
		// co-owning handle via `shared_from_this` (the `enable_shared_from_this<IType>` bridge).
		// `FindType` / `GetDefinition` return `const` references (the accessor contract), so the
		// const `shared_from_this` overload yields `shared_ptr<const IType>`; the underlying
		// type-system object is mutable (the compilation-owned definition), so
		// `const_pointer_cast` to `ITypePtr` is safe (the D515 const-overload-pair precedent).
		ITypePtr genericType = std::const_pointer_cast<IType>(nullableTypeDef->shared_from_this());
		// Likewise the element is shared-managed by its declaring slot; the const `shared_from_this`
		// yields `shared_ptr<const IType>`, cast back to a co-owning `ITypePtr` (the underlying
		// `IType` is mutable).
		ITypePtr element = std::const_pointer_cast<IType>(elementType.shared_from_this());
		return std::make_shared<ParameterizedType>(std::move(genericType),
			std::vector<ITypePtr>{ std::move(element) });
	}
	// `GetDefinition() == null` (e.g. a `KnownType` placeholder that is not an `ITypeDefinition`):
	// return the `FindType` result itself, viewed as an owning handle (the object is
	// shared-managed by the compilation; `const_pointer_cast` for the same const-accessor reason).
	return std::const_pointer_cast<IType>(nullableType.shared_from_this());
}

} // namespace ILSpy::Decompiler::TypeSystem
