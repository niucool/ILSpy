// Copyright (c) 2026 ILSpy Contributors
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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Out-of-line definitions for the TaskType helpers (see the header).

#include "Decompiler/TypeSystem/TaskType.hpp"

#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp" // IsKnownType

#include <any>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

bool IsTask(const IType& type)
{
	// C# TaskType.IsTask: `def = type.GetDefinition(); if (def != null) { if
	// (def.KnownTypeCode == Task) return true; if (def.KnownTypeCode == TaskOfT) return
	// type is ParameterizedType; } return false;` -- the bare `Task`1` definition is NOT
	// a `ParameterizedType`, so only a parameterized `Task<T>` matches the `TaskOfT` arm.
	const ITypeDefinition* def = type.GetDefinition();
	if (def != nullptr) {
		if (def->KnownTypeCode() == KnownTypeCode::Task)
			return true;
		if (def->KnownTypeCode() == KnownTypeCode::TaskOfT)
			return dynamic_cast<const ParameterizedType*>(&type) != nullptr;
	}
	return false;
}

bool IsCustomTask(const IType& type, ITypePtr& builderType)
{
	// C# TaskType.IsCustomTask: `builderType = null; def = type.GetDefinition(); if (def
	// != null) { if (def.TypeParameterCount > 1) return false; attribute = def.GetAttribute
	// (AsyncMethodBuilder); if (attribute == null || attribute.FixedArguments.Length != 1)
	// return false; arg = attribute.FixedArguments[0]; if (!arg.Type.IsKnownType(Type))
	// return false; builderType = (IType)arg.Value; return true; } return false;`.
	builderType = ITypePtr(); // faithful to the C# `builderType = null` at the top.
	const ITypeDefinition* def = type.GetDefinition();
	if (def == nullptr)
		return false;
	if (def->TypeParameterCount() > 1)
		return false;
	const IAttribute* attribute = def->GetAttribute(KnownAttribute::AsyncMethodBuilder);
	if (attribute == nullptr)
		return false;
	auto fixedArgs = attribute->FixedArguments();
	if (fixedArgs.size() != 1)
		return false;
	const CustomAttributeTypedArgument& arg = fixedArgs[0];
	const ITypePtr& argType = arg.Type();
	// `arg.Type` is non-null for a decoded argument in the real decoder; the port's
	// `Type()` may be a null `ITypePtr` (the default-constructed zero-fill sentinel), so
	// guard before `IsKnownType` (a null deref would be UB). A null arg type is not
	// `System.Type`, faithfully yielding false (the C# would NRE on `arg.Type.IsKnownType`
	// only if the decoder produced a null type, which it does not).
	if (argType == nullptr || !IsKnownType(*argType, KnownTypeCode::Type))
		return false;
	// C# `(IType)arg.Value` -- the boxed `System.Type`-typed argument value is an `IType`.
	// The pointer-form `std::any_cast` returns null on a type mismatch (the safe faithful
	// fallback for a divergent state the C# would `InvalidCastException` on); the guard
	// then returns false with `builderType` already null above.
	const ITypePtr* builderPtr = std::any_cast<ITypePtr>(&arg.Value());
	if (builderPtr == nullptr || *builderPtr == nullptr)
		return false;
	builderType = *builderPtr;
	return true;
}

ITypePtr UnpackTask(const ICompilation& compilation, const IType& type)
{
	// C# TaskType.UnpackTask: `if (!IsTask(type)) return type; if
	// (type.TypeParameterCount == 0) return compilation.FindType(Void); else return
	// type.TypeArguments[0];`.
	if (!IsTask(type)) {
		// The non-task passthrough: an owning handle to the input type via `shared_from_this`.
		// `shared_from_this` on a `const IType&` yields `shared_ptr<const IType>`; the
		// underlying type-system object is mutable (the accessor's `const` is the contract),
		// so `const_pointer_cast` to `ITypePtr` is safe (the D515 NullableType.Create precedent).
		return std::const_pointer_cast<IType>(type.shared_from_this());
	}
	if (type.TypeParameterCount() == 0) {
		// The non-generic `Task` unpacks to `void`. `FindType` returns `const IType&` (the
		// compilation-owned `void`); obtain an owning handle via `shared_from_this` +
		// `const_pointer_cast` (the same const-accessor convention).
		const IType& voidType = compilation.FindType(KnownTypeCode::Void);
		return std::const_pointer_cast<IType>(voidType.shared_from_this());
	}
	// `Task<T>`: the type argument. `IsTask` returned true via the `TaskOfT` arm, which
	// requires `type` to be a `ParameterizedType`, so the cast succeeds; the guard is the
	// defensive null-check convention (the D516 precedent). `GetTypeArgument(0)` returns a
	// co-owning `ITypePtr` copy from the `ParameterizedType`'s `typeArgs_`.
	const ParameterizedType* pt = dynamic_cast<const ParameterizedType*>(&type);
	if (pt != nullptr)
		return pt->GetTypeArgument(0);
	// Defensive fallback (does not occur in practice): passthrough the input type.
	return std::const_pointer_cast<IType>(type.shared_from_this());
}

ITypePtr UnpackAnyTask(const ICompilation& compilation, const IType& type)
{
	// C# TaskType.UnpackAnyTask: `if (IsTask(type)) return TypeParameterCount == 0 ? Void
	// : TypeArguments[0]; if (IsCustomTask(type, out _)) return TypeParameterCount == 0 ?
	// Void : TypeArguments[0]; return type;` -- the IsTask arm first, then the IsCustomTask
	// arm (a custom task-like that is NOT `Task`/`Task<T>`), else the passthrough.
	bool isTaskLike = IsTask(type);
	if (!isTaskLike) {
		ITypePtr discardedBuilder; // the C# `out _` discards the builder type.
		isTaskLike = IsCustomTask(type, discardedBuilder);
	}
	if (isTaskLike) {
		if (type.TypeParameterCount() == 0) {
			const IType& voidType = compilation.FindType(KnownTypeCode::Void);
			return std::const_pointer_cast<IType>(voidType.shared_from_this());
		}
		// The generic task-like branch reads `TypeArguments[0]` (a `ParameterizedType`-specific
		// accessor in the port); a 1-type-param task-like is always parameterized in practice,
		// so the cast succeeds. The null guard falls back to the passthrough for a degenerate
		// non-parameterized 1-type-param custom task (the safe faithful fallback).
		const ParameterizedType* pt = dynamic_cast<const ParameterizedType*>(&type);
		if (pt != nullptr)
			return pt->GetTypeArgument(0);
	}
	// The non-task-like passthrough (or the degenerate fallback above): an owning handle to
	// the input type via `shared_from_this` + `const_pointer_cast` (the const-accessor
	// convention, the `UnpackTask` precedent).
	return std::const_pointer_cast<IType>(type.shared_from_this());
}

} // namespace ILSpy::Decompiler::TypeSystem
