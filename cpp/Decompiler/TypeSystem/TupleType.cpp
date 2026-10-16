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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. CAUSED BY ON THE WHICHEVER THEORY OF LIABILITY, WHETHER IN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Out-of-line definitions for the TupleType static helpers (see the header).

#include "Decompiler/TypeSystem/TupleType.hpp"

#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"  // GetTypeDefinition (FindValueTupleType)
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // WithoutNullability (the .WithoutNullability() tail) + FindType (the compilation fallback)

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

namespace {

// The C# `const int RestPosition = 8` / `RestIndex = RestPosition - 1` -- the 8-ary
// `ValueTuple<T1,T2,T3,T4,T5,T6,T7,TRest>` nests the remaining elements in the 8th type
// argument `TRest` (itself a `ValueTuple<...>`). A 1..7-element tuple's underlying
// `ValueTuple<...>` carries the elements directly in its 1..7 type arguments; an 8+-element
// tuple's outer `ValueTuple` carries the first 7 in `TypeArguments[0..6]` and the rest in
// `TypeArguments[7]` (the nested `ValueTuple<...>`).
constexpr int kRestPosition = 8;
constexpr int kRestIndex = kRestPosition - 1;

// The C# `GetTupleElementTypes` local function `bool Collect(IType type)` -- the recursive
// worker that appends the tuple's element types to `output` and returns `true` when `type` is
// a tuple, or returns `false` (leaving `output` unreturned) when it is not. The recursion
// threads the 8-ary `ValueTuple<...>` nesting: a `ValueTuple<T1..T7,TRest>` (arity 8) contributes
// its first 7 type arguments then recurses on the 8th (the `Rest`, itself a `ValueTuple<...>`).
bool CollectTupleElements(const IType& type, std::vector<ITypePtr>& output)
{
	switch (type.Kind()) {
		case TypeKind::Tuple: {
			// C# `output.AddRange(((TupleType)type).ElementTypes)`. The port's `TupleType`
			// class (IType.hpp) carries the element types as a `std::vector<ITypePtr>`.
			const auto* tuple = dynamic_cast<const TupleType*>(&type);
			if (tuple == nullptr) {
				// A `TypeKind::Tuple` type that is not the port's `TupleType` class does not
				// occur in the minimal type system; treat it as not-a-tuple (the safe fallback).
				return false;
			}
			const auto& elementTypes = tuple->ElementTypes();
			output.insert(output.end(), elementTypes.begin(), elementTypes.end());
			return true;
		}
		case TypeKind::Class:
		case TypeKind::Struct: {
			// C# `if (type.Namespace == "System" && type.Name == "ValueTuple")`. The port's
			// `IType` interface carries `Name()` (a `ParameterizedType::Name()` delegates to
			// the generic definition's `Name()`, so an instantiated `ValueTuple<...>` reads
			// "ValueTuple") but NOT `Namespace()` -- `Namespace()` lives on `ITypeDefinition`
			// (via `INamedElement`), so the namespace check reads `type.GetDefinition()`
			// (a `ParameterizedType::GetDefinition()` delegates to the generic definition's
			// `GetDefinition()`, which is the definition itself for a real `ValueTuple`1`..
			// `ValueTuple`8` generic), with a `nullptr` guard (a `ParameterizedType` over a
			// non-definition generic -- a degenerate shape -- yields `nullptr`, so the
			// namespace check fails and the type is not-a-tuple, faithfully matching the C#
			// where the generic's `Namespace` would be empty). The `UnpackExpressionTreeType`
			// (D534) `pt->GetDefinition()->Namespace()` precedent.
			if (type.Name() != "ValueTuple") {
				return false;
			}
			const ITypeDefinition* def = type.GetDefinition();
			if (def == nullptr || def->Namespace() != "System") {
				return false;
			}
			// C# `int tpc = type.TypeParameterCount` -- for a `ParameterizedType` this is the
			// number of type arguments (the instantiation's arity, e.g. 2 for
			// `ValueTuple<int, string>`); the port's `ParameterizedType::TypeParameterCount()`
			// returns `typeArgs_.size()` (NOT the generic definition's arity).
			int tpc = type.TypeParameterCount();
			// C# `type.TypeArguments` -- the port's `IType` interface does NOT carry
			// `TypeArguments()` (it is `ParameterizedType`-specific, NOT an `IType` virtual),
			// so the faithful port reads it via a `dynamic_cast` with a `nullptr` guard. A
			// `Class`/`Struct` `ValueTuple` that is NOT a `ParameterizedType` (the bare
			// open-generic definition) is a degenerate shape: in the C# its `TypeArguments`
			// returns the type parameters, and the `tpc == RestPosition` recursion on the
			// `TRest` type parameter (a `TypeKind::TypeParameter`, not a tuple) returns
			// `false`, so the C# returns `default` (not-a-tuple) for the bare definition too;
			// the `nullptr` guard returns `false` for the same degenerate shape, faithfully
			// matching the C# result (the internal `output` state differs but is discarded
			// when `Collect` returns `false`). The realistic in-scope operands (an
			// instantiated `ValueTuple<...>` parameterized type) ARE `ParameterizedType`s.
			const auto* pt = dynamic_cast<const ParameterizedType*>(&type);
			if (pt == nullptr) {
				return false;
			}
			const auto& typeArgs = pt->TypeArguments();
			if (tpc > 0 && tpc < kRestPosition) {
				// C# `output.AddRange(type.TypeArguments)` -- a 1..7-element tuple's
				// underlying `ValueTuple<...>` carries the elements directly.
				output.insert(output.end(), typeArgs.begin(), typeArgs.end());
				return true;
			} else if (tpc == kRestPosition) {
				// C# `output.AddRange(type.TypeArguments.Take(RestPosition - 1))` then
				// `return Collect(type.TypeArguments[RestIndex])` -- an 8+-element tuple's
				// outer `ValueTuple<T1..T7,TRest>` carries the first 7 elements in
				// `TypeArguments[0..6]` and the remaining elements in the 8th argument (the
				// nested `ValueTuple<...>` `Rest`), which the recursion flattens.
				if (kRestIndex >= static_cast<int>(typeArgs.size())) {
					// A degenerate 8-arity `ValueTuple` with fewer than 8 type arguments does
					// not occur in practice; bail rather than read out of bounds (the C# would
					// throw on the `TypeArguments[RestIndex]` index; the port returns false).
					return false;
				}
				output.insert(output.end(), typeArgs.begin(), typeArgs.begin() + kRestIndex);
				return CollectTupleElements(*typeArgs[kRestIndex], output);
			}
			// `tpc == 0` (no type arguments) or `tpc > 8` (more than 8 type arguments on a
			// single `ValueTuple` -- not a real `ValueTuple` shape) fall through to
			// not-a-tuple, matching the C# `break` -> `return false`.
			return false;
		}
		default:
			// C# `default` -> `return false` for any other kind (TypeParameter, Interface,
			// Array, Pointer, ...).
			return false;
	}
}

}  // namespace

std::optional<std::vector<ITypePtr>> GetTupleElementTypes(const IType& tupleType)
{
	// C# `List<IType> output = null; if (Collect(tupleType)) return output.ToImmutableArray();
	// else return default(ImmutableArray<IType>);`.
	std::vector<ITypePtr> output;
	if (CollectTupleElements(tupleType, output)) {
		return output;
	}
	return std::nullopt;
}

// The C# `public static IType TupleUnderlyingTypeOrSelf(this IType type)` (TupleType.cs line
// 381, the `TupleTypeExtensions` static class).
ITypePtr TupleUnderlyingTypeOrSelf(IType& type)
{
	// C# `var t = (type as TupleType)?.UnderlyingType ?? type;` -- a tuple delegates to its
	// underlying `ValueTuple<...>`; a degenerate `TupleType` with a null underlying falls back
	// to the tuple itself (the `??` catches both the non-tuple and the null-underlying
	// shapes; a non-null input never yields a null return).
	IType* t = &type;
	if (const TupleType* tuple = dynamic_cast<const TupleType*>(&type)) {
		if (const ITypePtr& underlying = tuple->UnderlyingType())
			t = underlying.get();
	}
	// C# `return t.WithoutNullability();`
	return WithoutNullability(*t);
}

// The C# `public static bool IsTupleCompatible(IType type, out int tupleCardinality)`
// (TupleType.cs line 111).
bool IsTupleCompatible(const IType& type, int& tupleCardinality)
{
	switch (type.Kind()) {
		case TypeKind::Tuple: {
			// C# `tupleCardinality = ((TupleType)type).ElementTypes.Length; return true;`
			const auto* tuple = dynamic_cast<const TupleType*>(&type);
			if (tuple == nullptr) {
				// A `TypeKind::Tuple` type that is not the port's `TupleType` class does not
				// occur in the minimal type system; treat it as not-a-tuple (the safe fallback).
				break;
			}
			tupleCardinality = static_cast<int>(tuple->ElementTypes().size());
			return true;
		}
		case TypeKind::Struct: {
			// The C# requires `TypeKind.Struct` -- a CLASS named ValueTuple must not become
			// tuple syntax (the C# comment). The namespace/name check reads the definition's
			// `Namespace()` (the `CollectTupleElements` convention above: the port's IType
			// carries no Namespace virtual, and a `ParameterizedType::GetDefinition()`
			// resolves the generic definition).
			if (type.Name() != "ValueTuple") {
				break;
			}
			const ITypeDefinition* def = type.GetDefinition();
			if (def == nullptr || def->Namespace() != "System") {
				break;
			}
			int tpc = type.TypeParameterCount();
			if (tpc > 0 && tpc < kRestPosition) {
				// C# `tupleCardinality = tpc; return true;`
				tupleCardinality = tpc;
				return true;
			} else if (tpc == kRestPosition) {
				// C# `type is ParameterizedType pt` then the `TRest` recursion -- the 8th type
				// argument must itself be tuple-compatible, adding `RestPosition - 1` to the
				// nested cardinality. A `Struct` `ValueTuple`8` that is not a
				// `ParameterizedType` (the bare open-generic definition) falls to `break`
				// (the port's `nullptr` guard; the C# `is` pattern fails the same way).
				const auto* pt = dynamic_cast<const ParameterizedType*>(&type);
				if (pt != nullptr && kRestIndex < static_cast<int>(pt->TypeArguments().size())) {
					int nested = 0;
					if (IsTupleCompatible(*pt->TypeArguments()[kRestIndex], nested)) {
						nested += kRestPosition - 1;
						tupleCardinality = nested;
						return true;
					}
				}
			}
			break;
		}
		default:
			break;
	}
	// C# `tupleCardinality = 0; return false;` (the out parameter assigned in every path).
	tupleCardinality = 0;
	return false;
}

namespace {

// The C# `private static IType FindValueTupleType(ICompilation compilation,
// IModule valueTupleAssembly, int tpc)` (TupleType.cs line 95): the
// value-tuple-assembly's own `System.ValueTuple<tpc>` definition first, the
// compilation-wide `FindType` fallback second. The module-owned definition
// aliases with a no-op deleter (the TypeProvider convention).
ITypePtr FindValueTupleType(const ICompilation& compilation,
                             const IModule* valueTupleAssembly, int tpc)
{
	TopLevelTypeName typeName("System", "ValueTuple", tpc);
	if (valueTupleAssembly != nullptr) {
		const ITypeDefinition* typeDef = valueTupleAssembly->GetTypeDefinition(typeName);
		if (typeDef != nullptr) {
			return std::shared_ptr<IType>(
				const_cast<IType*>(static_cast<const IType*>(typeDef)),
				[](IType*) { /* no-op: the module owns the definition */ });
		}
	}
	return FindType(compilation, FullTypeName(std::move(typeName)));
}

}  // namespace

// The C# `public TupleType(ICompilation compilation, ImmutableArray<IType>
// elementTypes, ImmutableArray<string> elementNames = default,
// IModule valueTupleAssembly = null)` (TupleType.cs line 52) over its
// `CreateUnderlyingType` (line 73).
std::shared_ptr<TupleType> CreateTupleType(
	const ICompilation& compilation,
	std::vector<ITypePtr> elementTypes,
	std::optional<std::vector<std::string>> elementNames,
	const IModule* valueTupleAssembly)
{
	// C# `int remainder = (elementTypes.Length - 1) % (RestPosition - 1) + 1;` -- the
	// count of elements the innermost `ValueTuple` carries (1..7; the C++/C#
	// truncated-division `%` agree on the negative dividend of the degenerate
	// empty-elementTypes shape, which no caller reaches -- the visitor gates on
	// cardinality > 1).
	const int count = static_cast<int>(elementTypes.size());
	int remainder = (count - 1) % (kRestPosition - 1) + 1;
	int pos = count - remainder;
	std::vector<ITypePtr> tailArgs(elementTypes.begin() + pos, elementTypes.end());
	ITypePtr type = std::make_shared<ParameterizedType>(
		FindValueTupleType(compilation, valueTupleAssembly, remainder), std::move(tailArgs));
	while (pos > 0) {
		pos -= (kRestPosition - 1);
		// C# `elementTypes.Slice(pos, RestPosition - 1).Concat(new[] { type })`.
		std::vector<ITypePtr> args(elementTypes.begin() + pos,
			elementTypes.begin() + pos + (kRestPosition - 1));
		args.push_back(type);
		type = std::make_shared<ParameterizedType>(
			FindValueTupleType(compilation, valueTupleAssembly, kRestPosition),
			std::move(args));
	}
	// The element names: `nullopt` is the C# `default` (the null-filled array --
	// the port's TupleType ctor fills empty strings); a provided vector maps the
	// C# null entries to empty strings (the established null-name convention).
	std::vector<std::string> names;
	if (elementNames) {
		names = std::move(*elementNames);
	}
	return std::make_shared<TupleType>(std::move(type), std::move(elementTypes),
		std::move(names));
}

}  // namespace ILSpy::Decompiler::TypeSystem
