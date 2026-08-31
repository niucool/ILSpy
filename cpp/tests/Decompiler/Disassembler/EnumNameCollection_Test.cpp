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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `EnumNameCollection` (cpp/Decompiler/Disassembler/EnumNameCollection.hpp --
// the port of the ReflectionDisassembler.cs attribute-name machinery: the internal
// `EnumNameCollection<T>` struct, the internal static `WriteFlags<T>`/`WriteEnum<T>`
// helpers, and the thirteen attribute-name tables). The tests pin the collection
// mechanics (insertion order, the initializer-list ctor, the int64 key widening for
// both the int32-backed and the byte-backed enums), the `flags({0:x4}) ` placeholder
// formatting (zero-padding to four digits, no truncation, the full 64-bit two's
// complement of a negative remainder), every WriteFlags/WriteEnum dispatch arm
// (table-order rendering, the null-named entries that suppress output while still
// marking their bits tested, the unmatched-value fallback, the zero-value and
// matched-null no-output shapes, first-match-wins), the exact contents and insertion
// order of all thirteen tables, and the composed `.method`/`.field`/`.property`/
// `.event`/method-impl/`.class` header flag renders that replicate the C#
// DisassembleXHeaderInternal call-site mask splits.

#include "Decompiler/Disassembler/EnumNameCollection.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <utility>

namespace DA = ILSpy::Decompiler::Disassembler;
namespace OUT = ILSpy::Decompiler::Output;
namespace TS = ILSpy::Decompiler::TypeSystem;

using DA::EnumNameCollection;
using DA::EventAttributes;
using DA::FieldAttributes;
using DA::FormatFlagsPlaceholder;
using DA::MethodAttributes;
using DA::MethodImplAttributes;
using DA::PropertyAttributes;
using DA::TypeAttributes;
using DA::WriteEnum;
using DA::WriteFlags;
using OUT::ITextOutput;
using OUT::PlainTextOutput;
using TS::SignatureCallingConvention;

namespace {

// The shared table-contents assertion: every pair, in insertion order, with the
// exact int64 key and name (nullptr entries compared by pointer).
template <typename T>
void ExpectTableContents(const EnumNameCollection<T>& table,
	std::initializer_list<std::pair<std::int64_t, const char*>> expected) {
	ASSERT_EQ(table.size(), expected.size());
	auto expectedIt = expected.begin();
	for (const auto& pair : table) {
		EXPECT_EQ(pair.first, expectedIt->first);
		if (expectedIt->second == nullptr) {
			EXPECT_EQ(pair.second, nullptr);
		} else {
			ASSERT_NE(pair.second, nullptr);
			EXPECT_STREQ(pair.second, expectedIt->second);
		}
		++expectedIt;
	}
}

// ---------------------------------------------------------------------------
// EnumNameCollection mechanics
// ---------------------------------------------------------------------------

TEST(EnumNameCollectionTest, DefaultCtorIsEmpty) {
	EnumNameCollection<FieldAttributes> collection;
	EXPECT_EQ(collection.size(), 0u);
	EXPECT_EQ(collection.begin(), collection.end());
}

TEST(EnumNameCollectionTest, AddAppendsInInsertionOrder) {
	EnumNameCollection<FieldAttributes> collection;
	collection.Add(FieldAttributes::Static, "static");
	collection.Add(FieldAttributes::InitOnly, "initonly");
	ASSERT_EQ(collection.size(), 2u);
	auto it = collection.begin();
	EXPECT_EQ(it->first, static_cast<std::int64_t>(FieldAttributes::Static));
	EXPECT_STREQ(it->second, "static");
	++it;
	EXPECT_EQ(it->first, static_cast<std::int64_t>(FieldAttributes::InitOnly));
	EXPECT_STREQ(it->second, "initonly");
}

TEST(EnumNameCollectionTest, InitializerListCtorMatchesTheAddSequence) {
	const EnumNameCollection<EventAttributes> fromInitializer = {
		{ EventAttributes::SpecialName, "specialname" },
		{ EventAttributes::RTSpecialName, "rtspecialname" },
	};
	EnumNameCollection<EventAttributes> fromAdd;
	fromAdd.Add(EventAttributes::SpecialName, "specialname");
	fromAdd.Add(EventAttributes::RTSpecialName, "rtspecialname");
	ASSERT_EQ(fromInitializer.size(), fromAdd.size());
	auto initIt = fromInitializer.begin();
	auto addIt = fromAdd.begin();
	for (std::size_t i = 0; i < fromInitializer.size(); i++) {
		EXPECT_EQ(initIt->first, addIt->first);
		EXPECT_STREQ(initIt->second, addIt->second);
		++initIt;
		++addIt;
	}
}

TEST(EnumNameCollectionTest, ByteBackedEnumKeyZeroExtends) {
	// SignatureCallingConvention is `: byte` in the BCL, so Convert.ToInt64
	// zero-extends (FastCall == 4, not 0x...00000004 garbage or a sign extension).
	EnumNameCollection<SignatureCallingConvention> collection;
	collection.Add(SignatureCallingConvention::FastCall, "unmanaged fastcall");
	ASSERT_EQ(collection.size(), 1u);
	EXPECT_EQ(collection.begin()->first, 4);
}

TEST(EnumNameCollectionTest, Int32BackedEnumKeySignExtends) {
	// The System.Reflection stand-ins are int32-backed, so Convert.ToInt64 sign-
	// extends a value with bit 31 set -- the shape behind the negative-int64
	// flags fallback. (The expected value is spelled -2147483647LL - 1 because
	// MSVC gives the literal 2147483648 the type `unsigned long` -- the standard
	// says long long -- so the naive `-2147483648` is a POSITIVE unsigned wrap.)
	EnumNameCollection<TypeAttributes> collection;
	collection.Add(static_cast<TypeAttributes>(static_cast<std::int32_t>(0x80000000u)), "negative");
	ASSERT_EQ(collection.size(), 1u);
	EXPECT_EQ(collection.begin()->first, -2147483647LL - 1);
}

// ---------------------------------------------------------------------------
// WriteFlags
// ---------------------------------------------------------------------------

TEST(WriteFlagsTest, RendersNamesInTableOrder) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	WriteFlags(FieldAttributes::Static | FieldAttributes::InitOnly, DA::fieldAttributes, output);
	// The table lists Static, Literal, InitOnly -- the names render in TABLE
	// insertion order (Static before InitOnly), not bit-value order.
	EXPECT_EQ(concrete.ToString(), "static initonly ");
}

TEST(WriteFlagsTest, ComposesAllMatchingBitsInTableOrder) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const auto attrs = MethodAttributes::HideBySig | MethodAttributes::Abstract
		| MethodAttributes::Virtual | MethodAttributes::Static;
	WriteFlags(attrs, DA::methodAttributeFlags, output);
	// Bit-value order would be Static(0x10), Virtual(0x40), HideBySig(0x80),
	// Abstract(0x400); the table order renders HideBySig first and Static last.
	EXPECT_EQ(concrete.ToString(), "hidebysig abstract virtual static ");
}

TEST(WriteFlagsTest, NullNamedEntriesSuppressOutputButMarkBitsTested) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// PinvokeImpl is in the table with a null name (handled separately by the
	// pinvokeimpl("...") arm): it renders nothing here AND its bit is `tested`,
	// so no flags(...) fallback appears for it either.
	WriteFlags(MethodAttributes::PinvokeImpl | MethodAttributes::Static, DA::methodAttributeFlags, output);
	EXPECT_EQ(concrete.ToString(), "static ");
}

TEST(WriteFlagsTest, HasSecurityIsInvisibleToo) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// The C# table comment: "?? also invisible in ILDasm" -- the HasSecurity
	// entry has a null name, so an otherwise-empty value renders nothing.
	WriteFlags(MethodAttributes::HasSecurity, DA::methodAttributeFlags, output);
	EXPECT_EQ(concrete.ToString(), "");
}

TEST(WriteFlagsTest, UnknownBitsRenderTheFlagsPlaceholder) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// Reserved2 (0x2000) has no propertyAttributes entry -- the remainder
	// renders the `flags({0:x4}) ` fallback (four digits, no padding needed).
	WriteFlags(PropertyAttributes::Reserved2, DA::propertyAttributes, output);
	EXPECT_EQ(concrete.ToString(), "flags(2000) ");
}

TEST(WriteFlagsTest, ZeroPadsTheHexToFourDigits) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// HasFieldRVA (0x0100) has no fieldAttributes entry (the at-<rva> arm
	// renders it separately): 0x100 is three hex digits, zero-padded to 0100.
	WriteFlags(FieldAttributes::HasFieldRVA, DA::fieldAttributes, output);
	EXPECT_EQ(concrete.ToString(), "flags(0100) ");
}

TEST(WriteFlagsTest, ZeroValueWritesNothing) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	WriteFlags(PropertyAttributes::None, DA::propertyAttributes, output);
	EXPECT_EQ(concrete.ToString(), "");
}

TEST(WriteFlagsTest, NegativeRemainderRendersTheFullTwoComplement) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// A degenerate TypeAttributes with bit 31 set (no real flag lives there):
	// Convert.ToInt64 sign-extends to a negative int64, every table entry is
	// below the sign bit, so the untested remainder is the full negative value
	// and the {0:x4} fallback renders all sixteen two's-complement digits.
	const auto attrs = static_cast<TypeAttributes>(static_cast<std::int32_t>(0x80000000u));
	WriteFlags(attrs, DA::typeAttributes, output);
	EXPECT_EQ(concrete.ToString(), "flags(ffffffff80000000) ");
}

TEST(WriteFlagsTest, NullNamedLocalEntryMarksBitsTestedSoNoFallback) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const EnumNameCollection<FieldAttributes> local = {
		{ FieldAttributes::Static, nullptr },
	};
	WriteFlags(FieldAttributes::Static, local, output);
	EXPECT_EQ(concrete.ToString(), "");
}

// ---------------------------------------------------------------------------
// WriteEnum
// ---------------------------------------------------------------------------

TEST(WriteEnumTest, RendersTheMatchingName) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	WriteEnum(MethodAttributes::Family, DA::methodVisibility, output);
	EXPECT_EQ(concrete.ToString(), "family ");
}

TEST(WriteEnumTest, FirstMatchingEntryWinsOverLaterDuplicate) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const EnumNameCollection<FieldAttributes> local = {
		{ FieldAttributes::Static, "first" },
		{ FieldAttributes::Static, "second" },
	};
	WriteEnum(FieldAttributes::Static, local, output);
	EXPECT_EQ(concrete.ToString(), "first ");
}

TEST(WriteEnumTest, ZeroValueWithoutAZeroEntryWritesNothing) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// PrivateScope (0) has no methodVisibility entry (the compiler-controlled
	// case is handled separately by the isCompilerControlled check), and the
	// unmatched-zero arm writes nothing at all.
	WriteEnum(MethodAttributes::PrivateScope, DA::methodVisibility, output);
	EXPECT_EQ(concrete.ToString(), "");
}

TEST(WriteEnumTest, ZeroValueMatchingAZeroEntryRenders) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// AutoLayout IS the zero entry of typeLayout, so the zero value matches it.
	WriteEnum(TypeAttributes::AutoLayout, DA::typeLayout, output);
	EXPECT_EQ(concrete.ToString(), "auto ");
}

TEST(WriteEnumTest, NullNamedMatchWritesNothingAndReturns) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// Default (0) is IN the callingConvention table with a null name: the match
	// renders nothing and returns -- it must not fall through to the fallback
	// (unreachable anyway for the zero value, but the return is the point).
	WriteEnum(SignatureCallingConvention::Default, DA::callingConvention, output);
	EXPECT_EQ(concrete.ToString(), "");
}

TEST(WriteEnumTest, NullNamedMatchSuppressesALaterDuplicate) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const EnumNameCollection<FieldAttributes> local = {
		{ FieldAttributes::Static, nullptr },
		{ FieldAttributes::Static, "should-not-render" },
	};
	WriteEnum(FieldAttributes::Static, local, output);
	EXPECT_EQ(concrete.ToString(), "");
}

TEST(WriteEnumTest, UnmatchedNonZeroRendersTheFlagsPlaceholder) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	// Unmanaged (6) is the one real SignatureCallingConvention value NOT in the
	// callingConvention table (the custom CallConv* modifiers render it) -- the
	// unmatched arm falls through to the zero-padded fallback.
	WriteEnum(SignatureCallingConvention::Unmanaged, DA::callingConvention, output);
	EXPECT_EQ(concrete.ToString(), "flags(0006) ");
}

TEST(WriteEnumTest, CDeclRendersUnmanagedCdecl) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	WriteEnum(SignatureCallingConvention::CDecl, DA::callingConvention, output);
	EXPECT_EQ(concrete.ToString(), "unmanaged cdecl ");
}

// ---------------------------------------------------------------------------
// FormatFlagsPlaceholder (the inline rendering of the `flags({0:x4}) ` writes)
// ---------------------------------------------------------------------------

TEST(FormatFlagsPlaceholderTest, PadsShortValuesToFourDigits) {
	EXPECT_EQ(FormatFlagsPlaceholder(6), "flags(0006) ");
}

TEST(FormatFlagsPlaceholderTest, DoesNotTruncateLongerValues) {
	EXPECT_EQ(FormatFlagsPlaceholder(0x12345), "flags(12345) ");
}

TEST(FormatFlagsPlaceholderTest, NegativeRendersTheFullTwoComplement) {
	// -2147483647LL - 1 == INT64_MIN's int32 tail (the sign-extended int32 with
	// bit 31 set); see the Int32BackedEnumKeySignExtends note on the MSVC
	// `unsigned long` literal quirk.
	EXPECT_EQ(FormatFlagsPlaceholder(-2147483647LL - 1),
		"flags(ffffffff80000000) ");
}

TEST(FormatFlagsPlaceholderTest, ZeroRendersFourZeros) {
	EXPECT_EQ(FormatFlagsPlaceholder(0), "flags(0000) ");
}

// ---------------------------------------------------------------------------
// The composed header flag renders -- the exact C#
// DisassembleXHeaderInternal call-site mask splits (ReflectionDisassembler.cs
// lines 183-184, 1363-1365, 1453, 1539, 310-315, 1733-1737).
// ---------------------------------------------------------------------------

TEST(HeaderFlagCompositionTest, MethodHeaderSplitsVisibilityFromFlags) {
	// `.method public hidebysig abstract virtual static ...`
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const auto attrs = MethodAttributes::Public | MethodAttributes::Static
		| MethodAttributes::Virtual | MethodAttributes::HideBySig | MethodAttributes::Abstract;
	WriteEnum(attrs & MethodAttributes::MemberAccessMask, DA::methodVisibility, output);
	WriteFlags(attrs & ~MethodAttributes::MemberAccessMask, DA::methodAttributeFlags, output);
	EXPECT_EQ(concrete.ToString(), "public hidebysig abstract virtual static ");
}

TEST(HeaderFlagCompositionTest, FieldHeaderMasksTheHasXAttributes) {
	// `.field public static initonly ...` -- HasDefault is masked OUT by the
	// hasXAttributes const (the .custom arm renders it), so it does NOT leak
	// into a flags(...) fallback.
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const auto attrs = FieldAttributes::Public | FieldAttributes::Static
		| FieldAttributes::InitOnly | FieldAttributes::HasDefault;
	const auto hasXAttributes = FieldAttributes::HasDefault | FieldAttributes::HasFieldMarshal
		| FieldAttributes::HasFieldRVA;
	WriteEnum(attrs & FieldAttributes::FieldAccessMask, DA::fieldVisibility, output);
	WriteFlags(attrs & ~(FieldAttributes::FieldAccessMask | hasXAttributes), DA::fieldAttributes, output);
	EXPECT_EQ(concrete.ToString(), "public static initonly ");
}

TEST(HeaderFlagCompositionTest, PropertyHeaderRendersTheWholeValue) {
	// `.property specialname rtspecialname ...` -- no mask split.
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const auto attrs = PropertyAttributes::SpecialName | PropertyAttributes::RTSpecialName;
	WriteFlags(attrs, DA::propertyAttributes, output);
	EXPECT_EQ(concrete.ToString(), "specialname rtspecialname ");
}

TEST(HeaderFlagCompositionTest, EventHeaderRendersTheWholeValue) {
	// `.event specialname rtspecialname ...` -- no mask split.
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const auto attrs = EventAttributes::SpecialName | EventAttributes::RTSpecialName;
	WriteFlags(attrs, DA::eventAttributes, output);
	EXPECT_EQ(concrete.ToString(), "specialname rtspecialname ");
}

TEST(HeaderFlagCompositionTest, MethodImplSplitsCodeTypeFromImpl) {
	// IL|Managed (both zero) + PreserveSig + InternalCall:
	// `cil preservesig internalcall`.
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const auto impl = MethodImplAttributes::PreserveSig | MethodImplAttributes::InternalCall;
	WriteEnum(impl & MethodImplAttributes::CodeTypeMask, DA::methodCodeType, output);
	WriteFlags(impl & ~(MethodImplAttributes::CodeTypeMask | MethodImplAttributes::ManagedMask),
		DA::methodImpl, output);
	EXPECT_EQ(concrete.ToString(), "cil preservesig internalcall ");
}

TEST(HeaderFlagCompositionTest, TypeHeaderSplitsThreeEnumsFromFlags) {
	// `.class public sequential ansi sealed beforefieldinit ...` -- the three
	// WriteEnum sub-ranges (visibility, layout, string format) then the
	// WriteFlags remainder under the C# const masks (AnsiClass is the zero
	// string-format entry, so the ansi arm renders via the zero-value match).
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	const auto attrs = TypeAttributes::Public | TypeAttributes::SequentialLayout
		| TypeAttributes::AnsiClass | TypeAttributes::Sealed | TypeAttributes::BeforeFieldInit;
	const auto masks = TypeAttributes::ClassSemanticsMask | TypeAttributes::VisibilityMask
		| TypeAttributes::LayoutMask | TypeAttributes::StringFormatMask;
	WriteEnum(attrs & TypeAttributes::VisibilityMask, DA::typeVisibility, output);
	WriteEnum(attrs & TypeAttributes::LayoutMask, DA::typeLayout, output);
	WriteEnum(attrs & TypeAttributes::StringFormatMask, DA::typeStringFormat, output);
	WriteFlags(attrs & ~masks, DA::typeAttributes, output);
	EXPECT_EQ(concrete.ToString(), "public sequential ansi sealed beforefieldinit ");
}

// ---------------------------------------------------------------------------
// The thirteen attribute-name tables: exact contents, in insertion order
// ---------------------------------------------------------------------------

TEST(AttributeTableContentsTest, MethodAttributeFlagsContents) {
	ExpectTableContents(DA::methodAttributeFlags, {
		{ static_cast<std::int64_t>(MethodAttributes::Final), "final" },
		{ static_cast<std::int64_t>(MethodAttributes::HideBySig), "hidebysig" },
		{ static_cast<std::int64_t>(MethodAttributes::SpecialName), "specialname" },
		{ static_cast<std::int64_t>(MethodAttributes::PinvokeImpl), nullptr },
		{ static_cast<std::int64_t>(MethodAttributes::UnmanagedExport), "export" },
		{ static_cast<std::int64_t>(MethodAttributes::RTSpecialName), "rtspecialname" },
		{ static_cast<std::int64_t>(MethodAttributes::RequireSecObject), "reqsecobj" },
		{ static_cast<std::int64_t>(MethodAttributes::NewSlot), "newslot" },
		{ static_cast<std::int64_t>(MethodAttributes::CheckAccessOnOverride), "strict" },
		{ static_cast<std::int64_t>(MethodAttributes::Abstract), "abstract" },
		{ static_cast<std::int64_t>(MethodAttributes::Virtual), "virtual" },
		{ static_cast<std::int64_t>(MethodAttributes::Static), "static" },
		{ static_cast<std::int64_t>(MethodAttributes::HasSecurity), nullptr },
	});
}

TEST(AttributeTableContentsTest, MethodVisibilityContents) {
	ExpectTableContents(DA::methodVisibility, {
		{ static_cast<std::int64_t>(MethodAttributes::Private), "private" },
		{ static_cast<std::int64_t>(MethodAttributes::FamANDAssem), "famandassem" },
		{ static_cast<std::int64_t>(MethodAttributes::Assembly), "assembly" },
		{ static_cast<std::int64_t>(MethodAttributes::Family), "family" },
		{ static_cast<std::int64_t>(MethodAttributes::FamORAssem), "famorassem" },
		{ static_cast<std::int64_t>(MethodAttributes::Public), "public" },
	});
}

TEST(AttributeTableContentsTest, CallingConventionContents) {
	ExpectTableContents(DA::callingConvention, {
		{ static_cast<std::int64_t>(SignatureCallingConvention::CDecl), "unmanaged cdecl" },
		{ static_cast<std::int64_t>(SignatureCallingConvention::StdCall), "unmanaged stdcall" },
		{ static_cast<std::int64_t>(SignatureCallingConvention::ThisCall), "unmanaged thiscall" },
		{ static_cast<std::int64_t>(SignatureCallingConvention::FastCall), "unmanaged fastcall" },
		{ static_cast<std::int64_t>(SignatureCallingConvention::VarArgs), "vararg" },
		{ static_cast<std::int64_t>(SignatureCallingConvention::Default), nullptr },
	});
}

TEST(AttributeTableContentsTest, MethodCodeTypeContents) {
	ExpectTableContents(DA::methodCodeType, {
		{ static_cast<std::int64_t>(MethodImplAttributes::IL), "cil" },
		{ static_cast<std::int64_t>(MethodImplAttributes::Native), "native" },
		{ static_cast<std::int64_t>(MethodImplAttributes::OPTIL), "optil" },
		{ static_cast<std::int64_t>(MethodImplAttributes::Runtime), "runtime" },
	});
}

TEST(AttributeTableContentsTest, MethodImplContents) {
	ExpectTableContents(DA::methodImpl, {
		{ static_cast<std::int64_t>(MethodImplAttributes::Synchronized), "synchronized" },
		{ static_cast<std::int64_t>(MethodImplAttributes::NoInlining), "noinlining" },
		{ static_cast<std::int64_t>(MethodImplAttributes::NoOptimization), "nooptimization" },
		{ static_cast<std::int64_t>(MethodImplAttributes::PreserveSig), "preservesig" },
		{ static_cast<std::int64_t>(MethodImplAttributes::InternalCall), "internalcall" },
		{ static_cast<std::int64_t>(MethodImplAttributes::ForwardRef), "forwardref" },
		{ static_cast<std::int64_t>(MethodImplAttributes::AggressiveInlining), "aggressiveinlining" },
	});
}

TEST(AttributeTableContentsTest, FieldVisibilityContents) {
	ExpectTableContents(DA::fieldVisibility, {
		{ static_cast<std::int64_t>(FieldAttributes::Private), "private" },
		{ static_cast<std::int64_t>(FieldAttributes::FamANDAssem), "famandassem" },
		{ static_cast<std::int64_t>(FieldAttributes::Assembly), "assembly" },
		{ static_cast<std::int64_t>(FieldAttributes::Family), "family" },
		{ static_cast<std::int64_t>(FieldAttributes::FamORAssem), "famorassem" },
		{ static_cast<std::int64_t>(FieldAttributes::Public), "public" },
	});
}

TEST(AttributeTableContentsTest, FieldAttributesContents) {
	ExpectTableContents(DA::fieldAttributes, {
		{ static_cast<std::int64_t>(FieldAttributes::Static), "static" },
		{ static_cast<std::int64_t>(FieldAttributes::Literal), "literal" },
		{ static_cast<std::int64_t>(FieldAttributes::InitOnly), "initonly" },
		{ static_cast<std::int64_t>(FieldAttributes::SpecialName), "specialname" },
		{ static_cast<std::int64_t>(FieldAttributes::RTSpecialName), "rtspecialname" },
		{ static_cast<std::int64_t>(FieldAttributes::NotSerialized), "notserialized" },
	});
}

TEST(AttributeTableContentsTest, PropertyAttributesContents) {
	ExpectTableContents(DA::propertyAttributes, {
		{ static_cast<std::int64_t>(PropertyAttributes::SpecialName), "specialname" },
		{ static_cast<std::int64_t>(PropertyAttributes::RTSpecialName), "rtspecialname" },
		{ static_cast<std::int64_t>(PropertyAttributes::HasDefault), "hasdefault" },
	});
}

TEST(AttributeTableContentsTest, EventAttributesContents) {
	ExpectTableContents(DA::eventAttributes, {
		{ static_cast<std::int64_t>(EventAttributes::SpecialName), "specialname" },
		{ static_cast<std::int64_t>(EventAttributes::RTSpecialName), "rtspecialname" },
	});
}

TEST(AttributeTableContentsTest, TypeVisibilityContents) {
	ExpectTableContents(DA::typeVisibility, {
		{ static_cast<std::int64_t>(TypeAttributes::Public), "public" },
		{ static_cast<std::int64_t>(TypeAttributes::NotPublic), "private" },
		{ static_cast<std::int64_t>(TypeAttributes::NestedPublic), "nested public" },
		{ static_cast<std::int64_t>(TypeAttributes::NestedPrivate), "nested private" },
		{ static_cast<std::int64_t>(TypeAttributes::NestedAssembly), "nested assembly" },
		{ static_cast<std::int64_t>(TypeAttributes::NestedFamily), "nested family" },
		{ static_cast<std::int64_t>(TypeAttributes::NestedFamANDAssem), "nested famandassem" },
		{ static_cast<std::int64_t>(TypeAttributes::NestedFamORAssem), "nested famorassem" },
	});
}

TEST(AttributeTableContentsTest, TypeLayoutContents) {
	ExpectTableContents(DA::typeLayout, {
		{ static_cast<std::int64_t>(TypeAttributes::AutoLayout), "auto" },
		{ static_cast<std::int64_t>(TypeAttributes::SequentialLayout), "sequential" },
		{ static_cast<std::int64_t>(TypeAttributes::ExplicitLayout), "explicit" },
	});
}

TEST(AttributeTableContentsTest, TypeStringFormatContents) {
	ExpectTableContents(DA::typeStringFormat, {
		{ static_cast<std::int64_t>(TypeAttributes::AutoClass), "auto" },
		{ static_cast<std::int64_t>(TypeAttributes::AnsiClass), "ansi" },
		{ static_cast<std::int64_t>(TypeAttributes::UnicodeClass), "unicode" },
	});
}

TEST(AttributeTableContentsTest, TypeAttributesContents) {
	ExpectTableContents(DA::typeAttributes, {
		{ static_cast<std::int64_t>(TypeAttributes::Abstract), "abstract" },
		{ static_cast<std::int64_t>(TypeAttributes::Sealed), "sealed" },
		{ static_cast<std::int64_t>(TypeAttributes::SpecialName), "specialname" },
		{ static_cast<std::int64_t>(TypeAttributes::Import), "import" },
		{ static_cast<std::int64_t>(TypeAttributes::Serializable), "serializable" },
		{ static_cast<std::int64_t>(TypeAttributes::WindowsRuntime), "windowsruntime" },
		{ static_cast<std::int64_t>(TypeAttributes::BeforeFieldInit), "beforefieldinit" },
		{ static_cast<std::int64_t>(TypeAttributes::HasSecurity), nullptr },
	});
}

}  // namespace
