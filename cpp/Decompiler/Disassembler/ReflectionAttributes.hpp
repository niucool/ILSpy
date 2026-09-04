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
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The BCL `System.Reflection` attribute-flag enums, as port stand-ins -- the raw
// ECMA-335 II.23.1 metadata flag values carried by the Field/Method/MethodImpl/
// Property/Event/Type table rows.
//
// The C# ILSpy consumes these directly from the BCL (`using System.Reflection;`):
// ReflectionDisassembler.cs builds its attribute-name tables over them
// (methodAttributeFlags/methodVisibility, methodCodeType/methodImpl,
// fieldVisibility/fieldAttributes, propertyAttributes, eventAttributes,
// typeVisibility/typeLayout/typeStringFormat/typeAttributes) and drives
// WriteEnum/WriteFlags with the masked values; ILAmbience.cs calls those same
// tables with `fd.Attributes & FieldAttributes.FieldAccessMask`,
// `md.Attributes & ~MethodAttributes.MemberAccessMask`,
// `td.Attributes & ~masks`, and `td.Attributes.HasFlag(TypeAttributes.Interface)`;
// the MetadataFile per-row attribute-flag reads (GetFieldDefinition(token).Attributes
// etc.) expose them on the metadata layer. This header is the shared prerequisite of
// that chain (the EnumNameCollection/WriteEnum/WriteFlags helpers and the tables land
// next; they write through ITextOutput, so their tests can drive a PlainTextOutput).
//
// C#-to-C++ porting decisions:
//  * The C++ port has no System::Reflection namespace mirror (the
//    TypeSystem/SignatureCallingConvention.hpp precedent for BCL enums), so the
//    stand-ins live in ILSpy::Decompiler::Disassembler -- the directory/namespace
//    convention, in the tree of the first consumers (the ReflectionDisassembler
//    attribute-name tables).
//  * The BCL enums declare no underlying type, so they are `System.Int32`-backed;
//    the port backs each with `std::int32_t`. The values below are the exact
//    ECMA-335 II.23.1 flag values, verified member-for-member against the local
//    .NET's System.Reflection enums (the same values the C# compiles against).
//    The metadata columns are narrower (the Method/Field/Property/Event/Impl
//    Flags columns are 2 bytes; the Type Flags column is 4 bytes), but the BCL
//    enum reading a row widens it to `int`; the int32 backing is the faithful
//    shape of the C# values the WriteFlags `Convert.ToInt64` consumes.
//  * The `[Flags]` bitwise operators the C# compiler generates implicitly are
//    defined per enum as free functions (the `enum class` does NOT have them;
//    the OverloadResolutionErrors/TypeSystemOptions/ConversionFlags convention).
//  * The C# `Enum.HasFlag(flag)` ports to the established
//    `(value & flag) == flag` idiom (the TypeSystemOptions convention), so no
//    helper is provided here. Note the C# HasFlag quirk for completeness:
//    HasFlag returns true for the 0 flag on any value; every port consumer
//    passes a named non-zero flag, so the idiom and HasFlag agree on every
//    reachable input.
//  * Aliased members (the BCL's `PrivateScope`/`ReuseSlot`/`IL`/`Managed`/
//    `NotPublic`/`AutoLayout`/`AnsiClass`/`Class` zero-valued names, and the
//    mask members) are all ported with their exact values; the enum-to-name
//    tables key on `Convert.ToInt64` values, so aliases that share a value
//    (e.g. EventAttributes::ReservedMask == RTSpecialName == 0x0400) are
//    indistinguishable numerically -- faithful to the BCL, where the first
//    table entry whose Key matches wins (WriteEnum scans in insertion order).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::Disassembler {

// ---------------------------------------------------------------------------
// FieldAttributes (ECMA-335 II.23.1.5 "Flags for fields"). The member comments
// carry the ILDasm spellings the fieldVisibility/fieldAttributes tables map each
// flag to (the next slice).
// ---------------------------------------------------------------------------
enum class FieldAttributes : std::int32_t {
	// The FieldAccessMask sub-range (the WriteEnum half of the split; the
	// visibility table renders exactly one of these).
	PrivateScope = 0x0000,  // compiler-controlled; ildasm prints "privatescope"
							// as the out-of-mask fallback (the C# isCompilerControlled
							// check in DisassembleField)
	Private = 0x0001,        // "private"
	FamANDAssem = 0x0002,    // "famandassem"
	Assembly = 0x0003,       // "assembly"
	Family = 0x0004,         // "family"
	FamORAssem = 0x0005,     // "famorassem"
	Public = 0x0006,         // "public"
	FieldAccessMask = 0x0007,
	// The WriteFlags half of the split (everything outside FieldAccessMask).
	Static = 0x0010,        // "static"
	InitOnly = 0x0020,       // "initonly"
	Literal = 0x0040,       // "literal"
	NotSerialized = 0x0080, // "notserialized"
	HasFieldRVA = 0x0100,   // has no table entry (the C# writes "at <rva>" separately)
	SpecialName = 0x0200,   // "specialname"
	RTSpecialName = 0x0400, // "rtspecialname"
	// The runtime-reserved flags (dropped from the WriteFlags output via the
	// hasXAttributes mask: HasDefault | HasFieldMarshal | HasFieldRVA).
	HasFieldMarshal = 0x1000,
	PinvokeImpl = 0x2000,
	HasDefault = 0x8000,
	ReservedMask = 0x9500, // HasFieldRVA | RTSpecialName | HasFieldMarshal | HasDefault
};

inline FieldAttributes operator|(FieldAttributes a, FieldAttributes b) {
	return static_cast<FieldAttributes>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline FieldAttributes operator&(FieldAttributes a, FieldAttributes b) {
	return static_cast<FieldAttributes>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline FieldAttributes operator^(FieldAttributes a, FieldAttributes b) {
	return static_cast<FieldAttributes>(static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b));
}
inline FieldAttributes operator~(FieldAttributes a) {
	return static_cast<FieldAttributes>(~static_cast<std::int32_t>(a));
}

// ---------------------------------------------------------------------------
// MethodAttributes (ECMA-335 II.23.1.10 "Flags for methods"). The member comments
// carry the ILDasm spellings the methodVisibility/methodAttributeFlags tables map
// each flag to (the null entries are handled separately or invisible in ILDasm).
// ---------------------------------------------------------------------------
enum class MethodAttributes : std::int32_t {
	// The MemberAccessMask sub-range (the WriteEnum half of the split).
	ReuseSlot = 0x0000,   // the VtableLayoutMask default (re-use a vtable slot)
	PrivateScope = 0x0000, // compiler-controlled; ildasm prints "privatescope" via the
						   // isCompilerControlled check in DisassembleMethodHeaderInternal
	Private = 0x0001,      // "private"
	FamANDAssem = 0x0002,  // "famandassem"
	Assembly = 0x0003,     // "assembly"
	Family = 0x0004,       // "family"
	FamORAssem = 0x0005,   // "famorassem"
	Public = 0x0006,       // "public"
	MemberAccessMask = 0x0007,
	// The WriteFlags half of the split (everything outside MemberAccessMask).
	UnmanagedExport = 0x0008, // "export"
	Static = 0x0010,           // "static"
	Final = 0x0020,            // "final"
	Virtual = 0x0040,          // "virtual"
	HideBySig = 0x0080,        // "hidebysig"
	NewSlot = 0x0100,          // "newslot"
	VtableLayoutMask = 0x0100,
	CheckAccessOnOverride = 0x0200, // "strict"
	Abstract = 0x0400,             // "abstract"
	SpecialName = 0x0800,          // "specialname"
	RTSpecialName = 0x1000,        // "rtspecialname"
	PinvokeImpl = 0x2000,          // null in the table -- handled separately (the
								   // pinvokeimpl("..." ...) arm)
	// The runtime-reserved tail (RTSpecialName is in ReservedMask, not in the table).
	HasSecurity = 0x4000,     // null in the table ("?? also invisible in ILDasm")
	RequireSecObject = 0x8000, // "reqsecobj"
	ReservedMask = 0xD000,     // RTSpecialName | HasSecurity | RequireSecObject
};

inline MethodAttributes operator|(MethodAttributes a, MethodAttributes b) {
	return static_cast<MethodAttributes>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline MethodAttributes operator&(MethodAttributes a, MethodAttributes b) {
	return static_cast<MethodAttributes>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline MethodAttributes operator^(MethodAttributes a, MethodAttributes b) {
	return static_cast<MethodAttributes>(static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b));
}
inline MethodAttributes operator~(MethodAttributes a) {
	return static_cast<MethodAttributes>(~static_cast<std::int32_t>(a));
}

// ---------------------------------------------------------------------------
// MethodImplAttributes (ECMA-335 II.23.1.12 "Flags for method implementations").
// The MethodImpl row's ImplAttributes column: the CodeTypeMask half feeds the
// methodCodeType table (WriteEnum), the rest (minus ManagedMask) feeds the
// methodImpl table (WriteFlags).
// ---------------------------------------------------------------------------
enum class MethodImplAttributes : std::int32_t {
	IL = 0x0000,        // "cil" (the CodeTypeMask default)
	Managed = 0x0000,    // the ManagedMask default
	Native = 0x0001,     // "native"
	OPTIL = 0x0002,      // "optil"
	Runtime = 0x0003,    // "runtime"
	CodeTypeMask = 0x0003,
	Unmanaged = 0x0004,
	ManagedMask = 0x0004,
	// The methodImpl table's entries (WriteFlags with
	// ~(CodeTypeMask | ManagedMask)).
	NoInlining = 0x0008,         // "noinlining"
	ForwardRef = 0x0010,         // "forwardref"
	Synchronized = 0x0020,       // "synchronized"
	NoOptimization = 0x0040,     // "nooptimization"
	PreserveSig = 0x0080,         // "preservesig"
	AggressiveInlining = 0x0100, // "aggressiveinlining"
	// The runtime-reserved tail.
	SecurityMitigations = 0x0400, // no table entry
	InternalCall = 0x1000,       // "internalcall"
	MaxMethodImplVal = 0xFFFF,   // the spec's all-bits-set sentinel
};

inline MethodImplAttributes operator|(MethodImplAttributes a, MethodImplAttributes b) {
	return static_cast<MethodImplAttributes>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline MethodImplAttributes operator&(MethodImplAttributes a, MethodImplAttributes b) {
	return static_cast<MethodImplAttributes>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline MethodImplAttributes operator^(MethodImplAttributes a, MethodImplAttributes b) {
	return static_cast<MethodImplAttributes>(static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b));
}
inline MethodImplAttributes operator~(MethodImplAttributes a) {
	return static_cast<MethodImplAttributes>(~static_cast<std::int32_t>(a));
}

// ---------------------------------------------------------------------------
// PropertyAttributes (ECMA-335 II.23.1 "Flags for properties"). The whole value
// feeds the propertyAttributes table (WriteFlags, no mask split).
// ---------------------------------------------------------------------------
enum class PropertyAttributes : std::int32_t {
	None = 0x0000,
	SpecialName = 0x0200,  // "specialname"
	RTSpecialName = 0x0400, // "rtspecialname"
	HasDefault = 0x1000,   // "hasdefault"
	// The runtime-reserved tail (no table entries).
	Reserved2 = 0x2000,
	Reserved3 = 0x4000,
	Reserved4 = 0x8000,
	ReservedMask = 0xF400, // RTSpecialName | HasDefault | Reserved2 | Reserved3 | Reserved4
};

inline PropertyAttributes operator|(PropertyAttributes a, PropertyAttributes b) {
	return static_cast<PropertyAttributes>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline PropertyAttributes operator&(PropertyAttributes a, PropertyAttributes b) {
	return static_cast<PropertyAttributes>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline PropertyAttributes operator^(PropertyAttributes a, PropertyAttributes b) {
	return static_cast<PropertyAttributes>(static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b));
}
inline PropertyAttributes operator~(PropertyAttributes a) {
	return static_cast<PropertyAttributes>(~static_cast<std::int32_t>(a));
}

// ---------------------------------------------------------------------------
// ParameterAttributes (the System.Reflection flags over the Param table's
// Flags column, ECMA-335 II.23.1 "Flags for params"). The C#
// ReflectionDisassembler.WriteParameters reads the In/Out/Optional bits for
// the "[in] "/"[out] "/"[opt] " prefixes (the (p.Attributes & X) == X
// single-bit tests -- the port masks the raw column value, so these members
// are documentation here; WriteParameterAttributes reads HasDefault the
// same way). The BCL declares Default as an alias of HasDefault and both
// spellings of the unused tail.
// ---------------------------------------------------------------------------
enum class ParameterAttributes : std::int32_t {
	None = 0x0000,
	In = 0x0001,         // "[in] "
	Out = 0x0002,        // "[out] "
	Optional = 0x0010,   // "[opt] "
	Default = 0x1000,    // the BCL alias
	HasDefault = 0x1000,
	HasFieldMarshal = 0x2000,
	Unused = 0xcfe0,
};

inline ParameterAttributes operator|(ParameterAttributes a, ParameterAttributes b) {
	return static_cast<ParameterAttributes>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline ParameterAttributes operator&(ParameterAttributes a, ParameterAttributes b) {
	return static_cast<ParameterAttributes>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline ParameterAttributes operator^(ParameterAttributes a, ParameterAttributes b) {
	return static_cast<ParameterAttributes>(static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b));
}
inline ParameterAttributes operator~(ParameterAttributes a) {
	return static_cast<ParameterAttributes>(~static_cast<std::int32_t>(a));
}

// ---------------------------------------------------------------------------
// EventAttributes (ECMA-335 II.23.1 "Flags for events"). The whole value feeds
// the eventAttributes table (WriteFlags, no mask split). The BCL declares
// ReservedMask ahead of RTSpecialName (both 0x0400 -- the only aliased pair).
// ---------------------------------------------------------------------------
enum class EventAttributes : std::int32_t {
	None = 0x0000,
	SpecialName = 0x0200,   // "specialname"
	ReservedMask = 0x0400,  // == RTSpecialName
	RTSpecialName = 0x0400, // "rtspecialname"
};

inline EventAttributes operator|(EventAttributes a, EventAttributes b) {
	return static_cast<EventAttributes>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline EventAttributes operator&(EventAttributes a, EventAttributes b) {
	return static_cast<EventAttributes>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline EventAttributes operator^(EventAttributes a, EventAttributes b) {
	return static_cast<EventAttributes>(static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b));
}
inline EventAttributes operator~(EventAttributes a) {
	return static_cast<EventAttributes>(~static_cast<std::int32_t>(a));
}

// ---------------------------------------------------------------------------
// TypeAttributes (ECMA-335 II.23.1 "Flags for types"). The TypeDef row's Flags
// column: three sub-ranges feed WriteEnum (VisibilityMask via typeVisibility,
// LayoutMask via typeLayout, StringFormatMask via typeStringFormat) and the
// remainder feeds the typeAttributes table (WriteFlags with ~masks).
// ---------------------------------------------------------------------------
enum class TypeAttributes : std::int32_t {
	// The VisibilityMask sub-range (the typeVisibility table).
	NotPublic = 0x0000,          // ildasm spells the zero value "private"
	AutoLayout = 0x0000,         // "auto" (the LayoutMask default)
	AnsiClass = 0x0000,          // "ansi" (the StringFormatMask default)
	Class = 0x0000,              // the ClassSemanticsMask default (not a class)
	Public = 0x0001,             // "public"
	NestedPublic = 0x0002,       // "nested public"
	NestedPrivate = 0x0003,     // "nested private"
	NestedFamily = 0x0004,      // "nested family"
	NestedAssembly = 0x0005,    // "nested assembly"
	NestedFamANDAssem = 0x0006, // "nested famandassem"
	NestedFamORAssem = 0x0007,  // "nested famorassem"
	VisibilityMask = 0x0007,
	// The LayoutMask sub-range (the typeLayout table).
	SequentialLayout = 0x0008, // "sequential"
	ExplicitLayout = 0x0010,    // "explicit"
	LayoutMask = 0x0018,
	// The ClassSemanticsMask bit (the HasFlag interface check).
	Interface = 0x0020,
	ClassSemanticsMask = 0x0020,
	// The typeAttributes table's entries (WriteFlags with ~masks).
	Abstract = 0x0080,       // "abstract"
	Sealed = 0x0100,          // "sealed"
	SpecialName = 0x0400,     // "specialname"
	RTSpecialName = 0x0800,   // no table entry
	Import = 0x1000,          // "import"
	Serializable = 0x2000,    // "serializable"
	WindowsRuntime = 0x4000,  // "windowsruntime"
	// The StringFormatMask sub-range (the typeStringFormat table).
	UnicodeClass = 0x10000,  // "unicode"
	AutoClass = 0x20000,     // "auto"
	StringFormatMask = 0x30000,
	CustomFormatClass = 0x30000,
	// The runtime-reserved tail.
	HasSecurity = 0x40000,   // null in the table
	ReservedMask = 0x40800,   // RTSpecialName | HasSecurity
	BeforeFieldInit = 0x100000, // "beforefieldinit"
	CustomFormatMask = 0xC00000,
};

inline TypeAttributes operator|(TypeAttributes a, TypeAttributes b) {
	return static_cast<TypeAttributes>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline TypeAttributes operator&(TypeAttributes a, TypeAttributes b) {
	return static_cast<TypeAttributes>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline TypeAttributes operator^(TypeAttributes a, TypeAttributes b) {
	return static_cast<TypeAttributes>(static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b));
}
inline TypeAttributes operator~(TypeAttributes a) {
	return static_cast<TypeAttributes>(~static_cast<std::int32_t>(a));
}

} // namespace ILSpy::Decompiler::Disassembler
