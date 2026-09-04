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
// Property/Event/Type table rows (plus the GenericParam Flags and ImplMap
// MappingFlags columns -- II.23.1.7 and II.23.1.11, and the Assembly/
// AssemblyRef Flags columns with the HashAlgId -- II.23.1.2).
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
// GenericParameterAttributes (ECMA-335 II.23.1.7 "Flags for Generic Params").
// The GenericParam row's raw Flags column -- the System.Reflection enum the
// pinned SRM returns it as (GenericParamTableReader.GetFlags casts the raw
// uint16 straight over, no remapping; the modern BCL enum carries exactly
// the raw ECMA bits, probed over the installed .NET 10: Covariant 1,
// Contravariant 2, ReferenceTypeConstraint 4, NotNullableValueTypeConstraint
// 8, DefaultConstructorConstraint 0x10, plus the mask members and
// AllowByRefLike 0x20 -- the bit ILSpy's SRMHacks alias targets, the C# 14
// by-ref-like generics feature). WriteTypeParameters tests these bits to
// spell the "class "/"valuetype "/"byreflike "/".ctor "/'-'/'+' generic
// parameter prefixes.
// ---------------------------------------------------------------------------
enum class GenericParameterAttributes : std::int32_t {
	None = 0x0000,
	VarianceMask = 0x0003,
	Covariant = 0x0001,             // "+" before the parameter name
	Contravariant = 0x0002,          // "-" before the parameter name
	SpecialConstraintMask = 0x001c,
	ReferenceTypeConstraint = 0x0004,       // "class " prefix
	NotNullableValueTypeConstraint = 0x0008, // "valuetype " prefix
	DefaultConstructorConstraint = 0x0010,  // ".ctor " prefix
	AllowByRefLike = 0x0020,          // "byreflike " prefix (the ILSpy alias
											 // of the same bit -- SRMExtensions/SRMHacks)
};

// ---------------------------------------------------------------------------
// MethodSemanticsAttributes (ECMA-335 II.23.1 "Flags for Methods").
// The MethodSemantics row's MethodSemantics column (the System.Reflection
// enum over the raw bits). The SRM PropertyDefinition/EventDefinition
// GetAccessors() implementations switch on the EXACT value (an exact match,
// not a bit test -- a row carrying combined flags matches no arm and is
// ignored); the accessors reads (MetadataFile::GetPropertyAccessors /
// GetEventAccessors) mirror that switch.
// ---------------------------------------------------------------------------
enum class MethodSemanticsAttributes : std::int32_t {
	Setter = 0x0001,   // a property's .set
	Getter = 0x0002,   // a property's .get
	Other = 0x0004,     // a property's .other / an event's .other
	AddOn = 0x0008,    // an event's .addon
	RemoveOn = 0x0010, // an event's .removeon
	Fire = 0x0020,     // an event's .fire
};

// ---------------------------------------------------------------------------
// MethodImportAttributes (ECMA-335 II.23.1.11 "Flags for ImplMap
// [PInvokeImpl]"). The ImplMap row's MappingFlags column -- again the
// System.Reflection enum over the raw bits (MethodImportAttributes: ExactWord
// 1, CharSetNotSpec 0, CharSetAnsi 2, CharSetUnicode 4, CharSetAuto 6,
// BestFitMapping 0x30, SupportsLastError 0x40, CallConvMask 0x0700, probed
// over the installed .NET 10). The pinvokeimpl("...") arm of
// DisassembleMethodHeaderInternal tests these bits for the nomangle /
// charset / lasterr / calling-convention spellings.
// ---------------------------------------------------------------------------
enum class MethodImportAttributes : std::int32_t {
	None = 0x0000,
	ExactSpelling = 0x0001,   // " nomangle"
	CharSetAnsi = 0x0002,     // " ansi"
	CharSetUnicode = 0x0004,  // " unicode"
	CharSetAuto = 0x0006,     // " autochar"
	CharSetMask = 0x0006,
	BestFitMappingEnable = 0x0010,
	BestFitMappingDisable = 0x0020,
	BestFitMappingMask = 0x0030,
	SetLastError = 0x0040,     // " lasterr"
	CallingConventionWinApi = 0x0100,   // " winapi"
	CallingConventionCDecl = 0x0200,     // " cdecl"
	CallingConventionStdCall = 0x0300,  // " stdcall"
	CallingConventionThisCall = 0x0400, // " thiscall"
	CallingConventionFastCall = 0x0500, // " fastcall"
	CallingConventionMask = 0x0700,
	ThrowOnUnmappableCharEnable = 0x1000,
	ThrowOnUnmappableCharDisable = 0x2000,
	ThrowOnUnmappableCharMask = 0x3000,
};

inline GenericParameterAttributes operator&(GenericParameterAttributes a,
	GenericParameterAttributes b) {
	return static_cast<GenericParameterAttributes>(
		static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline MethodImportAttributes operator&(MethodImportAttributes a,
	MethodImportAttributes b) {
	return static_cast<MethodImportAttributes>(
		static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
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

// ---------------------------------------------------------------------------
// AssemblyAttributes (ECMA-335 II.23.1.2 "Flags for assemblies") -- the BCL
// `System.Reflection.Metadata.AssemblyFlags` the C# WriteAssemblyHeader/
// WriteAssemblyReferences test for the WindowsRuntime bit. The Assembly and
// AssemblyRef Flags columns are 4 bytes.
// ---------------------------------------------------------------------------
enum class AssemblyAttributes : std::int32_t {
	None = 0x0000,
	PublicKey = 0x0001,          // the reference holds the full (unhashed) public key
	Retargetable = 0x0100,
	WindowsRuntime = 0x0200,      // the content is a Windows Runtime component
	DisableJITcompileOptimizer = 0x4000,
	EnableJITcompileTracking = 0x8000,
};

inline AssemblyAttributes operator|(AssemblyAttributes a, AssemblyAttributes b) {
	return static_cast<AssemblyAttributes>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline AssemblyAttributes operator&(AssemblyAttributes a, AssemblyAttributes b) {
	return static_cast<AssemblyAttributes>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline AssemblyAttributes operator~(AssemblyAttributes a) {
	return static_cast<AssemblyAttributes>(~static_cast<std::int32_t>(a));
}

// ---------------------------------------------------------------------------
// AssemblyHashAlgorithm (the Assembly table's HashAlgId column) -- the BCL
// `System.Reflection.Metadata.AssemblyHashAlgorithm` the WriteAssemblyHeader
// hash-algorithm line spells (only SHA1 carries a comment).
// ---------------------------------------------------------------------------
enum class AssemblyHashAlgorithm : std::uint32_t {
	None = 0x0000,
	MD5 = 0x8003,
	Sha1 = 0x8004,
};

// ---------------------------------------------------------------------------
// Subsystem -- the BCL `System.Reflection.PortableExecutable.Subsystem` the
// WriteModuleHeader `.subsystem` line renders. The enum is ushort-backed in
// .NET, which is load-bearing: the `{0:x}` enum format pads to the underlying
// type's full width, so the hex form is always four digits ("0x0003"). The
// comment spelling is the plain enum ToString: the member name, or the
// decimal value for an unnamed one (never negative -- ushort).
// ---------------------------------------------------------------------------
enum class Subsystem : std::uint16_t {
	Unknown = 0x0,
	Native = 0x1,
	WindowsGui = 0x2,
	WindowsCui = 0x3,
	OS2Cui = 0x5,
	PosixCui = 0x7,
	NativeWindows = 0x8,
	WindowsCEGui = 0x9,
	EfiApplication = 0xA,
	EfiBootServiceDriver = 0xB,
	EfiRuntimeDriver = 0xC,
	EfiRom = 0xD,
	Xbox = 0xE,
	WindowsBootApplication = 0x10,
};

// ---------------------------------------------------------------------------
// CorFlags -- the BCL `System.Reflection.PortableExecutable.CorFlags` ([Flags],
// int32-backed) over the cor20 header's Flags field, the WriteModuleHeader
// `.corflags` line renders. The comment spelling is the flags ToString: the
// member names of an exact union joined with ", " in ascending value order;
// an unnamed leftover bit discards the names and renders the FULL value in
// decimal (the .NET Enum flags-format fallback), and 0 renders "0" (CorFlags
// carries no None member). The int32 backing pads the `{0:x}` hex form to
// eight digits.
// ---------------------------------------------------------------------------
enum class CorFlags : std::int32_t {
	ILOnly = 0x00000001,
	Requires32Bit = 0x00000002,
	ILLibrary = 0x00000004,
	StrongNameSigned = 0x00000008,
	NativeEntryPoint = 0x00000010,
	TrackDebugData = 0x00010000,
	Prefers32Bit = 0x00020000,
};

inline CorFlags operator|(CorFlags a, CorFlags b) {
	return static_cast<CorFlags>(static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline CorFlags operator&(CorFlags a, CorFlags b) {
	return static_cast<CorFlags>(static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline CorFlags operator~(CorFlags a) {
	return static_cast<CorFlags>(~static_cast<std::int32_t>(a));
}

// ---------------------------------------------------------------------------
// DebugDirectoryEntryType -- the BCL `System.Reflection.PortableExecutable.
// DebugDirectoryEntryType` over the IMAGE_DEBUG_DIRECTORY Type field (the PE
// debug data directory the PdbProvider's PDB discovery walks). The .NET 10
// enum carries exactly these members (no Copysign); a raw Type value outside
// the set casts to an unnamed value that compares unequal to every member --
// the DebugInfoUtils discovery skips such entries, matching the C#.
// ---------------------------------------------------------------------------
enum class DebugDirectoryEntryType : std::int32_t {
	Unknown = 0x0,
	Coff = 0x1,
	CodeView = 0x2,
	Reproducible = 0x10,
	EmbeddedPortablePdb = 0x11,
	PdbChecksum = 0x13,
};

} // namespace ILSpy::Decompiler::Disassembler
