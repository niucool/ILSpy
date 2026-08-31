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

// Tests for the BCL System.Reflection attribute-flag enums
// (cpp/Decompiler/Disassembler/ReflectionAttributes.hpp -- the ECMA-335 II.23.1
// flag values the ReflectionDisassembler attribute-name tables and ILAmbience
// consume). The tests pin each member's exact value (verified against the local
// .NET's System.Reflection enums, member-for-member), the int32 backing (the BCL
// enums declare no underlying type, so they are System.Int32), the [Flags]
// bitwise operators, the exact WriteEnum/WriteFlags mask splits the C# tables
// perform (the FieldAccessMask / MemberAccessMask / CodeTypeMask+ManagedMask /
// VisibilityMask+LayoutMask+StringFormatMask+ClassSemanticsMask splits), and the
// (value & flag) == flag idiom the C# HasFlag / isCompilerControlled /
// pinvokeimpl checks use.

#include "Decompiler/Disassembler/ReflectionAttributes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

namespace DA = ILSpy::Decompiler::Disassembler;
using DA::EventAttributes;
using DA::FieldAttributes;
using DA::MethodAttributes;
using DA::MethodImplAttributes;
using DA::PropertyAttributes;
using DA::TypeAttributes;

// ---------------------------------------------------------------------------
// FieldAttributes (ECMA-335 II.23.1.5).
// ---------------------------------------------------------------------------

TEST(FieldAttributesTest, ValuesMatchBclLiterals)
{
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::PrivateScope), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::Private), 0x0001);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::FamANDAssem), 0x0002);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::Assembly), 0x0003);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::Family), 0x0004);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::FamORAssem), 0x0005);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::Public), 0x0006);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::FieldAccessMask), 0x0007);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::Static), 0x0010);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::InitOnly), 0x0020);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::Literal), 0x0040);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::NotSerialized), 0x0080);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::HasFieldRVA), 0x0100);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::SpecialName), 0x0200);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::RTSpecialName), 0x0400);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::HasFieldMarshal), 0x1000);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::PinvokeImpl), 0x2000);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::HasDefault), 0x8000);
	EXPECT_EQ(static_cast<std::int32_t>(FieldAttributes::ReservedMask), 0x9500);
}

TEST(FieldAttributesTest, UnderlyingTypeIsInt32)
{
	// The BCL enum declares no underlying type, so it is `System.Int32`.
	static_assert(std::is_same<std::underlying_type_t<FieldAttributes>, std::int32_t>::value,
		"FieldAttributes is int32-backed, matching the C# int default");
}

TEST(FieldAttributesTest, MembersAreDistinct)
{
	// No two named members share a value (the aliased PrivateScope/0x0000 pairs
	// with nothing; the named non-mask members are pairwise distinct). The
	// WriteFlags "flags({0:x4})" fallback fires for unnamed bits, so a
	// transcription collision would silently merge two table entries.
	FieldAttributes members[] = {
		FieldAttributes::Private, FieldAttributes::FamANDAssem, FieldAttributes::Assembly,
		FieldAttributes::Family, FieldAttributes::FamORAssem, FieldAttributes::Public,
		FieldAttributes::Static, FieldAttributes::InitOnly, FieldAttributes::Literal,
		FieldAttributes::NotSerialized, FieldAttributes::HasFieldRVA,
		FieldAttributes::SpecialName, FieldAttributes::RTSpecialName,
		FieldAttributes::HasFieldMarshal, FieldAttributes::PinvokeImpl, FieldAttributes::HasDefault,
	};
	for (auto a : members) {
		for (auto b : members) {
			if (a != b) {
				EXPECT_NE(a, b);
			}
		}
	}
}

TEST(FieldAttributesTest, FieldAccessMaskSplitMatchesWriteEnumWriteFlags)
{
	// The exact DisassembleField/ILAmbience split: the visibility sub-range feeds
	// WriteEnum, the remainder minus the hasXAttributes dropped flags feeds
	// WriteFlags (ReflectionDisassembler.cs lines 1363-1365 / ILAmbience.cs
	// lines 81-85).
	auto attrs = FieldAttributes::Public | FieldAttributes::Static | FieldAttributes::InitOnly |
		FieldAttributes::HasFieldRVA | FieldAttributes::HasFieldMarshal | FieldAttributes::HasDefault;
	// The WriteEnum half: the visibility in isolation.
	EXPECT_EQ(attrs & FieldAttributes::FieldAccessMask, FieldAttributes::Public);
	// The WriteFlags half: everything but the access mask and the dropped
	// hasX bits (HasDefault | HasFieldMarshal | HasFieldRVA).
	const auto hasXAttributes = FieldAttributes::HasDefault | FieldAttributes::HasFieldMarshal |
		FieldAttributes::HasFieldRVA;
	EXPECT_EQ(attrs & ~(FieldAttributes::FieldAccessMask | hasXAttributes),
		FieldAttributes::Static | FieldAttributes::InitOnly);
}

TEST(FieldAttributesTest, BitwiseOrAndAnd)
{
	auto combined = FieldAttributes::Public | FieldAttributes::Static;
	EXPECT_EQ(static_cast<std::int32_t>(combined), 0x0016);
	EXPECT_EQ(combined & FieldAttributes::Public, FieldAttributes::Public);
	EXPECT_EQ(combined & FieldAttributes::Static, FieldAttributes::Static);
	EXPECT_EQ(combined & FieldAttributes::Literal, FieldAttributes::PrivateScope);
	// Intersect over a shared bit.
	auto a = FieldAttributes::Public | FieldAttributes::Literal;
	auto b = FieldAttributes::Literal | FieldAttributes::InitOnly;
	EXPECT_EQ(a & b, FieldAttributes::Literal);
}

TEST(FieldAttributesTest, BitwiseXorAndNot)
{
	auto a = FieldAttributes::Public | FieldAttributes::Literal;
	auto toggled = a ^ FieldAttributes::Literal;
	EXPECT_EQ(toggled, FieldAttributes::Public);
	// ~None is all-one bits (the int32 sign-extended complement), so it carries
	// any flag through the AND.
	EXPECT_EQ((~FieldAttributes::PrivateScope) & FieldAttributes::Static, FieldAttributes::Static);
	// ~mask clears exactly the mask bits from a value.
	EXPECT_EQ((FieldAttributes::Public | FieldAttributes::Static) &
		~FieldAttributes::Static, FieldAttributes::Public);
}

TEST(FieldAttributesTest, FlagTestIdiomMatchesDisassemblerUsage)
{
	// The C# `(fd.Attributes & FieldAttributes.FieldAccessMask)` WriteEnum input
	// and the single-flag test idiom HasFlag compiles down to.
	auto attrs = FieldAttributes::Family | FieldAttributes::Static;
	EXPECT_TRUE((attrs & FieldAttributes::Static) == FieldAttributes::Static);
	EXPECT_FALSE((attrs & FieldAttributes::InitOnly) == FieldAttributes::InitOnly);
}

// ---------------------------------------------------------------------------
// MethodAttributes (ECMA-335 II.23.1.10).
// ---------------------------------------------------------------------------

TEST(MethodAttributesTest, ValuesMatchBclLiterals)
{
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::ReuseSlot), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::PrivateScope), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::Private), 0x0001);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::FamANDAssem), 0x0002);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::Assembly), 0x0003);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::Family), 0x0004);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::FamORAssem), 0x0005);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::Public), 0x0006);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::MemberAccessMask), 0x0007);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::UnmanagedExport), 0x0008);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::Static), 0x0010);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::Final), 0x0020);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::Virtual), 0x0040);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::HideBySig), 0x0080);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::NewSlot), 0x0100);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::VtableLayoutMask), 0x0100);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::CheckAccessOnOverride), 0x0200);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::Abstract), 0x0400);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::SpecialName), 0x0800);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::RTSpecialName), 0x1000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::PinvokeImpl), 0x2000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::HasSecurity), 0x4000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::RequireSecObject), 0x8000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodAttributes::ReservedMask), 0xD000);
}

TEST(MethodAttributesTest, UnderlyingTypeIsInt32)
{
	static_assert(std::is_same<std::underlying_type_t<MethodAttributes>, std::int32_t>::value,
		"MethodAttributes is int32-backed, matching the C# int default");
}

TEST(MethodAttributesTest, MembersAreDistinct)
{
	MethodAttributes members[] = {
		MethodAttributes::Private, MethodAttributes::FamANDAssem, MethodAttributes::Assembly,
		MethodAttributes::Family, MethodAttributes::FamORAssem, MethodAttributes::Public,
		MethodAttributes::UnmanagedExport, MethodAttributes::Static, MethodAttributes::Final,
		MethodAttributes::Virtual, MethodAttributes::HideBySig, MethodAttributes::NewSlot,
		MethodAttributes::CheckAccessOnOverride, MethodAttributes::Abstract,
		MethodAttributes::SpecialName, MethodAttributes::RTSpecialName,
		MethodAttributes::PinvokeImpl, MethodAttributes::HasSecurity, MethodAttributes::RequireSecObject,
	};
	for (auto a : members) {
		for (auto b : members) {
			if (a != b) {
				EXPECT_NE(a, b);
			}
		}
	}
}

TEST(MethodAttributesTest, MemberAccessMaskSplitMatchesWriteEnumWriteFlags)
{
	// The exact DisassembleMethodHeaderInternal split
	// (ReflectionDisassembler.cs lines 183-184 / ILAmbience.cs lines 98-101).
	auto attrs = MethodAttributes::FamORAssem | MethodAttributes::Virtual |
		MethodAttributes::HideBySig | MethodAttributes::Abstract | MethodAttributes::NewSlot;
	EXPECT_EQ(attrs & MethodAttributes::MemberAccessMask, MethodAttributes::FamORAssem);
	EXPECT_EQ(attrs & ~MethodAttributes::MemberAccessMask,
		MethodAttributes::Virtual | MethodAttributes::HideBySig | MethodAttributes::Abstract |
		MethodAttributes::NewSlot);
	// The isCompilerControlled idiom (lines 185-187): a PrivateScope (0) access
	// mask means compiler-controlled.
	EXPECT_TRUE((MethodAttributes::Static & MethodAttributes::MemberAccessMask) ==
		MethodAttributes::PrivateScope);
	EXPECT_FALSE((attrs & MethodAttributes::MemberAccessMask) == MethodAttributes::PrivateScope);
}

TEST(MethodAttributesTest, BitwiseOrAndAnd)
{
	auto combined = MethodAttributes::Public | MethodAttributes::Virtual | MethodAttributes::Static;
	EXPECT_EQ(static_cast<std::int32_t>(combined), 0x0056);
	EXPECT_EQ(combined & MethodAttributes::Virtual, MethodAttributes::Virtual);
	EXPECT_EQ(combined & MethodAttributes::Abstract, MethodAttributes::PrivateScope);
	auto a = MethodAttributes::Final | MethodAttributes::Virtual;
	auto b = MethodAttributes::Virtual | MethodAttributes::Abstract;
	EXPECT_EQ(a & b, MethodAttributes::Virtual);
}

TEST(MethodAttributesTest, BitwiseXorAndNot)
{
	auto a = MethodAttributes::Public | MethodAttributes::Virtual;
	auto toggled = a ^ MethodAttributes::Virtual;
	EXPECT_EQ(toggled, MethodAttributes::Public);
	EXPECT_EQ((~MethodAttributes::PrivateScope) & MethodAttributes::Abstract,
		MethodAttributes::Abstract);
	EXPECT_EQ((MethodAttributes::Public | MethodAttributes::Static) &
		~MethodAttributes::Static, MethodAttributes::Public);
}

TEST(MethodAttributesTest, FlagTestIdiomMatchesDisassemblerUsage)
{
	// The pinvokeimpl arm's exact check (line 189):
	// (md.Attributes & MethodAttributes.PinvokeImpl) == MethodAttributes.PinvokeImpl.
	auto pinvoke = MethodAttributes::Public | MethodAttributes::Static | MethodAttributes::PinvokeImpl;
	EXPECT_TRUE((pinvoke & MethodAttributes::PinvokeImpl) == MethodAttributes::PinvokeImpl);
	EXPECT_FALSE((pinvoke & MethodAttributes::Virtual) == MethodAttributes::Virtual);
}

// ---------------------------------------------------------------------------
// MethodImplAttributes (ECMA-335 II.23.1.12).
// ---------------------------------------------------------------------------

TEST(MethodImplAttributesTest, ValuesMatchBclLiterals)
{
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::IL), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::Managed), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::Native), 0x0001);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::OPTIL), 0x0002);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::Runtime), 0x0003);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::CodeTypeMask), 0x0003);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::Unmanaged), 0x0004);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::ManagedMask), 0x0004);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::NoInlining), 0x0008);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::ForwardRef), 0x0010);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::Synchronized), 0x0020);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::NoOptimization), 0x0040);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::PreserveSig), 0x0080);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::AggressiveInlining), 0x0100);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::SecurityMitigations), 0x0400);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::InternalCall), 0x1000);
	EXPECT_EQ(static_cast<std::int32_t>(MethodImplAttributes::MaxMethodImplVal), 0xFFFF);
}

TEST(MethodImplAttributesTest, UnderlyingTypeIsInt32)
{
	static_assert(std::is_same<std::underlying_type_t<MethodImplAttributes>, std::int32_t>::value,
		"MethodImplAttributes is int32-backed, matching the C# int default");
}

TEST(MethodImplAttributesTest, MembersAreDistinct)
{
	// The CodeType/Managed sub-ranges and the impl flags are pairwise distinct
	// (the aliased IL/Managed 0x0000 pair excepted).
	MethodImplAttributes members[] = {
		MethodImplAttributes::Native, MethodImplAttributes::OPTIL, MethodImplAttributes::Runtime,
		MethodImplAttributes::Unmanaged, MethodImplAttributes::NoInlining,
		MethodImplAttributes::ForwardRef, MethodImplAttributes::Synchronized,
		MethodImplAttributes::NoOptimization, MethodImplAttributes::PreserveSig,
		MethodImplAttributes::AggressiveInlining, MethodImplAttributes::SecurityMitigations,
		MethodImplAttributes::InternalCall,
	};
	for (auto a : members) {
		for (auto b : members) {
			if (a != b) {
				EXPECT_NE(a, b);
			}
		}
	}
}

TEST(MethodImplAttributesTest, CodeTypeMaskSplitMatchesWriteEnumWriteFlags)
{
	// The exact DisassembleMethodImpl split (ReflectionDisassembler.cs lines
	// 310-315): the CodeTypeMask half feeds the methodCodeType table, the rest
	// minus ManagedMask feeds the methodImpl table.
	auto impl = MethodImplAttributes::Runtime | MethodImplAttributes::Synchronized |
		MethodImplAttributes::InternalCall;
	EXPECT_EQ(impl & MethodImplAttributes::CodeTypeMask, MethodImplAttributes::Runtime);
	EXPECT_EQ(impl & ~(MethodImplAttributes::CodeTypeMask | MethodImplAttributes::ManagedMask),
		MethodImplAttributes::Synchronized | MethodImplAttributes::InternalCall);
	// The methodCodeType WriteEnum input picks the managed/unmanaged bit apart
	// from the code type.
	auto unmanaged = MethodImplAttributes::Native | MethodImplAttributes::Unmanaged;
	EXPECT_EQ(unmanaged & MethodImplAttributes::CodeTypeMask, MethodImplAttributes::Native);
	EXPECT_EQ(unmanaged & MethodImplAttributes::ManagedMask, MethodImplAttributes::Unmanaged);
}

TEST(MethodImplAttributesTest, BitwiseOrAndAnd)
{
	auto combined = MethodImplAttributes::NoInlining | MethodImplAttributes::PreserveSig;
	EXPECT_EQ(static_cast<std::int32_t>(combined), 0x0088);
	EXPECT_EQ(combined & MethodImplAttributes::PreserveSig, MethodImplAttributes::PreserveSig);
	EXPECT_EQ(combined & MethodImplAttributes::Synchronized, MethodImplAttributes::IL);
	auto a = MethodImplAttributes::Native | MethodImplAttributes::Synchronized;
	auto b = MethodImplAttributes::Synchronized | MethodImplAttributes::PreserveSig;
	EXPECT_EQ(a & b, MethodImplAttributes::Synchronized);
}

TEST(MethodImplAttributesTest, BitwiseXorAndNot)
{
	auto a = MethodImplAttributes::NoInlining | MethodImplAttributes::ForwardRef;
	auto toggled = a ^ MethodImplAttributes::ForwardRef;
	EXPECT_EQ(toggled, MethodImplAttributes::NoInlining);
	EXPECT_EQ((~MethodImplAttributes::IL) & MethodImplAttributes::InternalCall,
		MethodImplAttributes::InternalCall);
	EXPECT_EQ((MethodImplAttributes::Synchronized | MethodImplAttributes::PreserveSig) &
		~MethodImplAttributes::PreserveSig, MethodImplAttributes::Synchronized);
}

TEST(MethodImplAttributesTest, FlagTestIdiomMatchesDisassemblerUsage)
{
	auto impl = MethodImplAttributes::Native | MethodImplAttributes::PreserveSig;
	EXPECT_TRUE((impl & MethodImplAttributes::PreserveSig) == MethodImplAttributes::PreserveSig);
	EXPECT_FALSE((impl & MethodImplAttributes::Synchronized) == MethodImplAttributes::Synchronized);
}

// ---------------------------------------------------------------------------
// PropertyAttributes (ECMA-335 II.23.1, flags for properties).
// ---------------------------------------------------------------------------

TEST(PropertyAttributesTest, ValuesMatchBclLiterals)
{
	EXPECT_EQ(static_cast<std::int32_t>(PropertyAttributes::None), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(PropertyAttributes::SpecialName), 0x0200);
	EXPECT_EQ(static_cast<std::int32_t>(PropertyAttributes::RTSpecialName), 0x0400);
	EXPECT_EQ(static_cast<std::int32_t>(PropertyAttributes::HasDefault), 0x1000);
	EXPECT_EQ(static_cast<std::int32_t>(PropertyAttributes::Reserved2), 0x2000);
	EXPECT_EQ(static_cast<std::int32_t>(PropertyAttributes::Reserved3), 0x4000);
	EXPECT_EQ(static_cast<std::int32_t>(PropertyAttributes::Reserved4), 0x8000);
	EXPECT_EQ(static_cast<std::int32_t>(PropertyAttributes::ReservedMask), 0xF400);
}

TEST(PropertyAttributesTest, UnderlyingTypeIsInt32)
{
	static_assert(std::is_same<std::underlying_type_t<PropertyAttributes>, std::int32_t>::value,
		"PropertyAttributes is int32-backed, matching the C# int default");
}

TEST(PropertyAttributesTest, MembersAreDistinct)
{
	PropertyAttributes members[] = {
		PropertyAttributes::SpecialName, PropertyAttributes::RTSpecialName,
		PropertyAttributes::HasDefault, PropertyAttributes::Reserved2,
		PropertyAttributes::Reserved3, PropertyAttributes::Reserved4,
	};
	for (auto a : members) {
		for (auto b : members) {
			if (a != b) {
				EXPECT_NE(a, b);
			}
		}
	}
}

TEST(PropertyAttributesTest, WholeValueFeedsWriteFlags)
{
	// No mask split: the whole value feeds the propertyAttributes table
	// (ReflectionDisassembler.cs line 1453 / ILAmbience.cs line 115).
	auto attrs = PropertyAttributes::SpecialName | PropertyAttributes::RTSpecialName;
	EXPECT_EQ(static_cast<std::int32_t>(attrs), 0x0600);
	// The ReservedMask sub-bits are the runtime-reserved flags the table drops.
	auto reserved = PropertyAttributes::HasDefault | PropertyAttributes::Reserved2 |
		PropertyAttributes::Reserved3 | PropertyAttributes::Reserved4;
	EXPECT_EQ(reserved & PropertyAttributes::ReservedMask, reserved);
	EXPECT_EQ(attrs & PropertyAttributes::ReservedMask, PropertyAttributes::RTSpecialName);
}

TEST(PropertyAttributesTest, BitwiseOrAndAnd)
{
	auto combined = PropertyAttributes::SpecialName | PropertyAttributes::HasDefault;
	EXPECT_EQ(static_cast<std::int32_t>(combined), 0x1200);
	EXPECT_EQ(combined & PropertyAttributes::HasDefault, PropertyAttributes::HasDefault);
	EXPECT_EQ(combined & PropertyAttributes::RTSpecialName, PropertyAttributes::None);
	auto a = PropertyAttributes::SpecialName | PropertyAttributes::HasDefault;
	auto b = PropertyAttributes::HasDefault | PropertyAttributes::Reserved2;
	EXPECT_EQ(a & b, PropertyAttributes::HasDefault);
}

TEST(PropertyAttributesTest, BitwiseXorAndNot)
{
	auto a = PropertyAttributes::SpecialName | PropertyAttributes::HasDefault;
	auto toggled = a ^ PropertyAttributes::HasDefault;
	EXPECT_EQ(toggled, PropertyAttributes::SpecialName);
	EXPECT_EQ((~PropertyAttributes::None) & PropertyAttributes::Reserved4,
		PropertyAttributes::Reserved4);
	EXPECT_EQ((PropertyAttributes::SpecialName | PropertyAttributes::Reserved2) &
		~PropertyAttributes::Reserved2, PropertyAttributes::SpecialName);
}

TEST(PropertyAttributesTest, FlagTestIdiomMatchesDisassemblerUsage)
{
	auto attrs = PropertyAttributes::SpecialName | PropertyAttributes::HasDefault;
	EXPECT_TRUE((attrs & PropertyAttributes::SpecialName) == PropertyAttributes::SpecialName);
	EXPECT_FALSE((attrs & PropertyAttributes::Reserved4) == PropertyAttributes::Reserved4);
}

// ---------------------------------------------------------------------------
// EventAttributes (ECMA-335 II.23.1, flags for events).
// ---------------------------------------------------------------------------

TEST(EventAttributesTest, ValuesMatchBclLiterals)
{
	EXPECT_EQ(static_cast<std::int32_t>(EventAttributes::None), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(EventAttributes::SpecialName), 0x0200);
	EXPECT_EQ(static_cast<std::int32_t>(EventAttributes::ReservedMask), 0x0400);
	EXPECT_EQ(static_cast<std::int32_t>(EventAttributes::RTSpecialName), 0x0400);
}

TEST(EventAttributesTest, UnderlyingTypeIsInt32)
{
	static_assert(std::is_same<std::underlying_type_t<EventAttributes>, std::int32_t>::value,
		"EventAttributes is int32-backed, matching the C# int default");
}

TEST(EventAttributesTest, ReservedMaskAliasesRTSpecialName)
{
	// The BCL's only aliased pair in this family: ReservedMask == RTSpecialName
	// (both 0x0400). The enum-to-name table keys on the numeric value, so the
	// alias is invisible to WriteFlags (the table's SpecialName/RTSpecialName
	// entries key 0x0200/0x0400 and match either name).
	EXPECT_EQ(EventAttributes::ReservedMask, EventAttributes::RTSpecialName);
}

TEST(EventAttributesTest, WholeValueFeedsWriteFlags)
{
	// No mask split: the whole value feeds the eventAttributes table
	// (ReflectionDisassembler.cs line 1539 / ILAmbience.cs line 129).
	auto attrs = EventAttributes::SpecialName | EventAttributes::RTSpecialName;
	EXPECT_EQ(static_cast<std::int32_t>(attrs), 0x0600);
}

TEST(EventAttributesTest, BitwiseOrAndAnd)
{
	auto combined = EventAttributes::SpecialName | EventAttributes::RTSpecialName;
	EXPECT_EQ(static_cast<std::int32_t>(combined), 0x0600);
	EXPECT_EQ(combined & EventAttributes::SpecialName, EventAttributes::SpecialName);
	EXPECT_EQ(combined & EventAttributes::None, EventAttributes::None);
}

TEST(EventAttributesTest, BitwiseXorAndNot)
{
	auto a = EventAttributes::SpecialName | EventAttributes::RTSpecialName;
	auto toggled = a ^ EventAttributes::SpecialName;
	EXPECT_EQ(toggled, EventAttributes::RTSpecialName);
	EXPECT_EQ((~EventAttributes::None) & EventAttributes::SpecialName,
		EventAttributes::SpecialName);
	EXPECT_EQ((EventAttributes::SpecialName | EventAttributes::RTSpecialName) &
		~EventAttributes::RTSpecialName, EventAttributes::SpecialName);
}

TEST(EventAttributesTest, FlagTestIdiomMatchesDisassemblerUsage)
{
	auto attrs = EventAttributes::SpecialName | EventAttributes::RTSpecialName;
	EXPECT_TRUE((attrs & EventAttributes::SpecialName) == EventAttributes::SpecialName);
	EXPECT_FALSE((EventAttributes::None & EventAttributes::SpecialName) ==
		EventAttributes::SpecialName);
}

// ---------------------------------------------------------------------------
// TypeAttributes (ECMA-335 II.23.1, flags for types).
// ---------------------------------------------------------------------------

TEST(TypeAttributesTest, ValuesMatchBclLiterals)
{
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::NotPublic), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::AutoLayout), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::AnsiClass), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::Class), 0x0000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::Public), 0x0001);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::NestedPublic), 0x0002);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::NestedPrivate), 0x0003);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::NestedFamily), 0x0004);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::NestedAssembly), 0x0005);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::NestedFamANDAssem), 0x0006);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::NestedFamORAssem), 0x0007);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::VisibilityMask), 0x0007);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::SequentialLayout), 0x0008);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::ExplicitLayout), 0x0010);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::LayoutMask), 0x0018);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::Interface), 0x0020);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::ClassSemanticsMask), 0x0020);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::Abstract), 0x0080);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::Sealed), 0x0100);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::SpecialName), 0x0400);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::RTSpecialName), 0x0800);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::Import), 0x1000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::Serializable), 0x2000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::WindowsRuntime), 0x4000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::UnicodeClass), 0x10000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::AutoClass), 0x20000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::StringFormatMask), 0x30000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::CustomFormatClass), 0x30000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::HasSecurity), 0x40000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::ReservedMask), 0x40800);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::BeforeFieldInit), 0x100000);
	EXPECT_EQ(static_cast<std::int32_t>(TypeAttributes::CustomFormatMask), 0xC00000);
}

TEST(TypeAttributesTest, UnderlyingTypeIsInt32)
{
	static_assert(std::is_same<std::underlying_type_t<TypeAttributes>, std::int32_t>::value,
		"TypeAttributes is int32-backed, matching the C# int default");
}

TEST(TypeAttributesTest, MembersAreDistinct)
{
	TypeAttributes members[] = {
		TypeAttributes::Public, TypeAttributes::NestedPublic, TypeAttributes::NestedPrivate,
		TypeAttributes::NestedFamily, TypeAttributes::NestedAssembly,
		TypeAttributes::NestedFamANDAssem, TypeAttributes::NestedFamORAssem,
		TypeAttributes::SequentialLayout, TypeAttributes::ExplicitLayout,
		TypeAttributes::Interface, TypeAttributes::Abstract, TypeAttributes::Sealed,
		TypeAttributes::SpecialName, TypeAttributes::RTSpecialName, TypeAttributes::Import,
		TypeAttributes::Serializable, TypeAttributes::WindowsRuntime,
		TypeAttributes::UnicodeClass, TypeAttributes::AutoClass, TypeAttributes::HasSecurity,
		TypeAttributes::BeforeFieldInit,
	};
	for (auto a : members) {
		for (auto b : members) {
			if (a != b) {
				EXPECT_NE(a, b);
			}
		}
	}
}

TEST(TypeAttributesTest, FourMaskSplitMatchesWriteEnumWriteFlags)
{
	// The exact DisassembleType split (ReflectionDisassembler.cs lines 1733-1737
	// / ILAmbience.cs lines 146-149): three sub-ranges feed WriteEnum, the
	// remainder (minus ClassSemanticsMask) feeds the typeAttributes table.
	auto attrs = TypeAttributes::NestedPublic | TypeAttributes::SequentialLayout |
		TypeAttributes::Interface | TypeAttributes::Abstract | TypeAttributes::UnicodeClass |
		TypeAttributes::BeforeFieldInit;
	EXPECT_EQ(attrs & TypeAttributes::VisibilityMask, TypeAttributes::NestedPublic);
	EXPECT_EQ(attrs & TypeAttributes::LayoutMask, TypeAttributes::SequentialLayout);
	EXPECT_EQ(attrs & TypeAttributes::StringFormatMask, TypeAttributes::UnicodeClass);
	const auto masks = TypeAttributes::ClassSemanticsMask | TypeAttributes::VisibilityMask |
		TypeAttributes::LayoutMask | TypeAttributes::StringFormatMask;
	EXPECT_EQ(attrs & ~masks,
		TypeAttributes::Abstract | TypeAttributes::BeforeFieldInit);
}

TEST(TypeAttributesTest, HasFlagInterfaceIdiom)
{
	// ILAmbience.cs line 142: td.Attributes.HasFlag(TypeAttributes.Interface) --
	// the (value & flag) == flag idiom the C# Enum.HasFlag compiles down to.
	auto iface = TypeAttributes::Interface | TypeAttributes::Abstract;
	EXPECT_TRUE((iface & TypeAttributes::Interface) == TypeAttributes::Interface);
	auto klass = TypeAttributes::Public | TypeAttributes::Abstract;
	EXPECT_FALSE((klass & TypeAttributes::Interface) == TypeAttributes::Interface);
}

TEST(TypeAttributesTest, BitwiseOrAndAnd)
{
	auto combined = TypeAttributes::Public | TypeAttributes::Abstract | TypeAttributes::Sealed;
	EXPECT_EQ(static_cast<std::int32_t>(combined), 0x0181);
	EXPECT_EQ(combined & TypeAttributes::Sealed, TypeAttributes::Sealed);
	EXPECT_EQ(combined & TypeAttributes::Interface, TypeAttributes::NotPublic);
	auto a = TypeAttributes::Abstract | TypeAttributes::Sealed;
	auto b = TypeAttributes::Sealed | TypeAttributes::Import;
	EXPECT_EQ(a & b, TypeAttributes::Sealed);
}

TEST(TypeAttributesTest, BitwiseXorAndNot)
{
	auto a = TypeAttributes::Abstract | TypeAttributes::Sealed;
	auto toggled = a ^ TypeAttributes::Sealed;
	EXPECT_EQ(toggled, TypeAttributes::Abstract);
	EXPECT_EQ((~TypeAttributes::NotPublic) & TypeAttributes::BeforeFieldInit,
		TypeAttributes::BeforeFieldInit);
	EXPECT_EQ((TypeAttributes::Abstract | TypeAttributes::Sealed) &
		~TypeAttributes::Sealed, TypeAttributes::Abstract);
}
