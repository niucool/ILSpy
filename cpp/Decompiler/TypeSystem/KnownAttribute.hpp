// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/KnownAttributes.cs -- the
// `KnownAttribute` enum and the self-contained members of the `KnownAttributes` static
// class. `KnownAttribute` names every attribute the decompiler recognizes by its metadata
// type name (CompilerGenerated, Extension, Obsolete, DllImport, ParamArray, ...); it is a
// leaf dependency of `IEntity`, whose `HasAttribute(KnownAttribute)` / `GetAttribute(
// KnownAttribute)` members classify an entity's attributes without resolving them to
// `IType`s. The enum itself has no external dependencies; the type-name table it indexes
// depends only on `TopLevelTypeName` (already ported), so the enum, the `Count` constant,
// the `typeNames` table, `GetTypeName`, and the pure `IsCustomAttribute` classifier land
// here as a self-contained leaf toward `IEntity` / `IModule` / `IAttribute` /
// `TypeSystemAstBuilder` / `CSharpAmbience`.
//
// The two `KnownAttributes` members that need the not-yet-ported type-system surfaces are
// deferred (documented at the bottom): `FindType` (needs `ICompilation`) and
// `IsKnownAttributeType` (needs `ITypeDefinition`).
//
// The C# enum has no explicit backing type, so it defaults to `int`; the port keeps `int`
// (the values 0..ExtensionMarker fit). The `KnownAttributes` static class (C# extension
// methods) ports to free functions in this namespace; the `typeNames` table is a
// function-local `static const std::array` (TopLevelTypeName is a non-literal type because
// it owns std::string, so it cannot be constexpr -- the function-local static is initialized
// once on first use, thread-safe under C++11+, and ODR-safe because the enclosing function is
// `inline`).

#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// The C# `public enum KnownAttribute`. The `enum class` (scoped, no implicit conversion
// to/from `int`, matching the C# enum's typed usage). The member order and values mirror
// the C# exactly (each member's numeric value is its declaration index; `None` = 0).
enum class KnownAttribute : int {
	None,
	CompilerGenerated,
	CompilerFeatureRequired,
	Extension,
	Dynamic,
	TupleElementNames,
	Nullable,
	NullableContext,
	NullablePublicOnly,
	Conditional,
	Obsolete,
	Embedded,
	IsReadOnly,
	SpecialName,
	DebuggerHidden,
	DebuggerStepThrough,
	DebuggerBrowsable,

	// Assembly attributes:
	AssemblyVersion,
	InternalsVisibleTo,
	TypeForwardedTo,
	ReferenceAssembly,

	// Type attributes:
	Serializable,
	Flags,
	ComImport,
	CoClass,
	StructLayout,
	DefaultMember,
	IsByRefLike,
	IteratorStateMachine,
	AsyncStateMachine,
	AsyncMethodBuilder,
	AsyncIteratorStateMachine,

	// Field attributes:
	FieldOffset,
	NonSerialized,
	DecimalConstant,
	FixedBuffer,

	// Method attributes:
	DllImport,
	PreserveSig,
	MethodImpl,

	// Property attributes:
	IndexerName,

	// Parameter attributes:
	ParamArray,
	ParamCollection,
	In,
	Out,
	Optional,
	DefaultParameterValue,
	CallerMemberName,
	CallerFilePath,
	CallerLineNumber,
	ScopedRef,
	RequiresLocation,

	// Type parameter attributes:
	IsUnmanaged,

	// Marshalling attributes:
	MarshalAs,

	// Security attributes:
	PermissionSet,

	// C# 9 attributes:
	NativeInteger,
	PreserveBaseOverrides,
	UnmanagedCallersOnly,

	// C# 11 attributes:
	Required,

	// C# 12 attributes:
	InlineArray,

	// C# 14 attributes:
	ExtensionMarker,
};

// The C# `internal const int Count = (int)KnownAttribute.ExtensionMarker + 1;` -- the size
// of the type-name table (and the number of `KnownAttribute` values, None..ExtensionMarker).
inline constexpr int KnownAttributeCount = static_cast<int>(KnownAttribute::ExtensionMarker) + 1;

// The type-name table indexed by `KnownAttribute` (the C# `static readonly TopLevelTypeName[]
// typeNames`). Index 0 (`None`) is default-constructed (the C# `default` entry); every other
// entry is the metadata type name of the corresponding attribute. Exposed as a function
// returning a const reference to a function-local static so the not-yet-ported
// `IsKnownAttributeType` can iterate the same table `GetTypeName` indexes.
inline const std::array<TopLevelTypeName, KnownAttributeCount>& KnownAttributeTypeNames() {
	static const std::array<TopLevelTypeName, KnownAttributeCount> typeNames = {
		TopLevelTypeName{}, // None (index 0)
		TopLevelTypeName("System.Runtime.CompilerServices", "CompilerGeneratedAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "CompilerFeatureRequiredAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "ExtensionAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "DynamicAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "TupleElementNamesAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "NullableAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "NullableContextAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "NullablePublicOnlyAttribute"),
		TopLevelTypeName("System.Diagnostics", "ConditionalAttribute"),
		TopLevelTypeName("System", "ObsoleteAttribute"),
		TopLevelTypeName("Microsoft.CodeAnalysis", "EmbeddedAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "IsReadOnlyAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "SpecialNameAttribute"),
		TopLevelTypeName("System.Diagnostics", "DebuggerHiddenAttribute"),
		TopLevelTypeName("System.Diagnostics", "DebuggerStepThroughAttribute"),
		TopLevelTypeName("System.Diagnostics", "DebuggerBrowsableAttribute"),
		// Assembly attributes:
		TopLevelTypeName("System.Reflection", "AssemblyVersionAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "InternalsVisibleToAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "TypeForwardedToAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "ReferenceAssemblyAttribute"),
		// Type attributes:
		TopLevelTypeName("System", "SerializableAttribute"),
		TopLevelTypeName("System", "FlagsAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "ComImportAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "CoClassAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "StructLayoutAttribute"),
		TopLevelTypeName("System.Reflection", "DefaultMemberAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "IsByRefLikeAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "IteratorStateMachineAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "AsyncStateMachineAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "AsyncMethodBuilderAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "AsyncIteratorStateMachineAttribute"),
		// Field attributes:
		TopLevelTypeName("System.Runtime.InteropServices", "FieldOffsetAttribute"),
		TopLevelTypeName("System", "NonSerializedAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "DecimalConstantAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "FixedBufferAttribute"),
		// Method attributes:
		TopLevelTypeName("System.Runtime.InteropServices", "DllImportAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "PreserveSigAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "MethodImplAttribute"),
		// Property attributes:
		TopLevelTypeName("System.Runtime.CompilerServices", "IndexerNameAttribute"),
		// Parameter attributes:
		TopLevelTypeName("System", "ParamArrayAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "ParamCollectionAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "InAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "OutAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "OptionalAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "DefaultParameterValueAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "CallerMemberNameAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "CallerFilePathAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "CallerLineNumberAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "ScopedRefAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "RequiresLocationAttribute"),
		// Type parameter attributes:
		TopLevelTypeName("System.Runtime.CompilerServices", "IsUnmanagedAttribute"),
		// Marshalling attributes:
		TopLevelTypeName("System.Runtime.InteropServices", "MarshalAsAttribute"),
		// Security attributes:
		TopLevelTypeName("System.Security.Permissions", "PermissionSetAttribute"),
		// C# 9 attributes:
		TopLevelTypeName("System.Runtime.CompilerServices", "NativeIntegerAttribute"),
		TopLevelTypeName("System.Runtime.CompilerServices", "PreserveBaseOverridesAttribute"),
		TopLevelTypeName("System.Runtime.InteropServices", "UnmanagedCallersOnlyAttribute"),
		// C# 11 attributes:
		TopLevelTypeName("System.Runtime.CompilerServices", "RequiredMemberAttribute"),
		// C# 12 attributes:
		TopLevelTypeName("System.Runtime.CompilerServices", "InlineArrayAttribute"),
		// C# 14 attributes:
		TopLevelTypeName("System.Runtime.CompilerServices", "ExtensionMarkerAttribute"),
	};
	static_assert(typeNames.size() == KnownAttributeCount,
		"typeNames table size must match KnownAttributeCount");
	return typeNames;
}

// The C# `public static ref readonly TopLevelTypeName GetTypeName(this KnownAttribute attr)`
// -- the metadata type name for a known attribute. The C# `Debug.Assert(attr !=
// KnownAttribute.None)` ports to `assert(...)` (debug-only, the D371/D373 precedent). The
// `ref readonly` return ports to `const TopLevelTypeName&` (the const-reference-return
// convention, the ITypeDefinitionOrUnknown::FullTypeName D377 precedent).
inline const TopLevelTypeName& GetTypeName(KnownAttribute attr) {
	assert(attr != KnownAttribute::None);
	return KnownAttributeTypeNames()[static_cast<std::size_t>(attr)];
}

// The C# `public static bool IsCustomAttribute(this KnownAttribute knownAttribute)` -- true
// unless the attribute is one the C# compiler emits as a real metadata attribute (not a
// pseudo-attribute the runtime folds into other metadata tables). The C# `default` arm
// returns `true`, so `KnownAttribute::None` (and every value not in the false-list) classifies
// as a custom attribute.
inline bool IsCustomAttribute(KnownAttribute knownAttribute) {
	switch (knownAttribute) {
		case KnownAttribute::Serializable:
		case KnownAttribute::ComImport:
		case KnownAttribute::StructLayout:
		case KnownAttribute::DllImport:
		case KnownAttribute::PreserveSig:
		case KnownAttribute::MethodImpl:
		case KnownAttribute::FieldOffset:
		case KnownAttribute::NonSerialized:
		case KnownAttribute::MarshalAs:
		case KnownAttribute::PermissionSet:
		case KnownAttribute::Optional:
		case KnownAttribute::DefaultParameterValue:
		case KnownAttribute::In:
		case KnownAttribute::Out:
		case KnownAttribute::IndexerName:
		case KnownAttribute::SpecialName:
			return false;
		default:
			return true;
	}
}

// Deferred: `KnownAttributes.FindType(this ICompilation compilation, KnownAttribute attrType)`
// (needs `ICompilation` + `IType`, the not-yet-ported compilation surface) and
// `KnownAttributes.IsKnownAttributeType(this ITypeDefinition attributeType)` (needs
// `ITypeDefinition` + `KnownTypeCode`, walking the attribute type's base types and matching
// against the type-name table). Both land with the `ICompilation` / `ITypeDefinition`
// interfaces (the next TypeSystem pieces `IEntity` / `IModule` need).

} // namespace ILSpy::Decompiler::TypeSystem
