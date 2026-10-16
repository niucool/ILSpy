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

// Port of the ReflectionDisassembler attribute-name machinery -- the internal
// `EnumNameCollection<T>` struct, the internal static `WriteFlags<T>`/`WriteEnum<T>`
// helpers (ReflectionDisassembler.cs lines 1970-2032), and the thirteen
// attribute-name tables (lines 102-144, 1270-1287, 1418-1422, 1492-1496,
// 1564-1597) those helpers render the ECMA-335 II.23.1 flag values through.
//
// These are `internal static` members of the C# ReflectionDisassembler class,
// but they read none of the instance state (the output sink is a parameter), so
// they land here ahead of the class skeleton as namespace-scope free templates
// and `inline const` tables (the tested-but-not-yet-wired convention; the D533
// GetDelegateInvokeMethod precedent for statics lifted out of a not-yet-ported
// class). Their two C# consumers are the not-yet-landed ReflectionDisassembler
// instance methods (DisassembleMethodHeaderInternal, DisassembleFieldHeader-
// Internal, the property/event/type header internals -- they write the IL
// `.method`/`.field`/`.property`/`.event`/`.class` flag prefixes) and
// ILAmbience.cs lines 81-149 (the IL view's ConvertSymbol drives the very same
// tables through `ReflectionDisassembler.WriteEnum`/`WriteFlags`) -- ILAmbience
// is the nearer consumer and the current target of this prerequisite chain.
//
// C#-to-C++ porting decisions:
//  * `internal struct EnumNameCollection<T> : IEnumerable<KeyValuePair<long,
//    string>>` ports as a class template holding a
//    `std::vector<std::pair<std::int64_t, const char*>>` in insertion order;
//    the C# collection-initializer `{ { flag, name }, ... }` (sugar calling
//    `Add` per element) ports to a `std::initializer_list` constructor; the C#
//    `foreach` iteration surface ports to `begin()`/`end()`.
//  * The C# `string` name may be null (the PinvokeImpl/HasSecurity/Default
//    entries suppress output but still mark the bit `tested`); the port models
//    the name as `const char*` where `nullptr` is the C# null.
//  * `Convert.ToInt64(flags)` -- T is one of the int32-backed
//    System.Reflection stand-ins (ReflectionAttributes.hpp) or the byte-backed
//    TypeSystem `SignatureCallingConvention` -- ports to
//    `static_cast<std::int64_t>(value)`: the scoped-enum conversion reads the
//    underlying value, so the int32 backing sign-extends and the byte backing
//    zero-extends, exactly `Convert.ToInt64` of the underlying type.
//  * The `"flags({0:x4}) "` fallbacks are the C# format-args `Write` extension
//    the port documents as deferred (ITextOutput.hpp); they format inline via
//    the `FormatFlagsPlaceholder` helper -- lowercase hex, zero-padded to at
//    least four digits, over the full 64-bit two's complement (a negative
//    int64 from a sign-extended int32 renders all sixteen digits, the
//    `string.Format("{0:x4}", long)` behavior).
//  * The tables keep their exact C# camelCase field names
//    (`methodAttributeFlags`, ...); `internal static readonly` fields port to
//    namespace-scope `inline const` variables. `typeLayout` and
//    `typeStringFormat` are private INSTANCE fields in the C# (no `static`
//    keyword; every ReflectionDisassembler instance builds an identical copy)
//    -- the port models them as the shared tables their contents are.
//  * The `callingConvention` table is over
//    `ILSpy::Decompiler::TypeSystem::SignatureCallingConvention` (the byte-
//    backed signature-header enum), the one table not over a
//    ReflectionAttributes.hpp stand-in.

#pragma once

#include "Decompiler/Disassembler/ReflectionAttributes.hpp"
#include "Decompiler/Output/ITextOutput.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Disassembler {

using TypeSystem::SignatureCallingConvention;

// ---------------------------------------------------------------------------
// EnumNameCollection<T> (ReflectionDisassembler.cs lines 2009-2032, the
// `internal struct` nested in the class and lifted to namespace scope -- the
// ILiftedOperator convention). An ordered (value, name) list: WriteFlags
// consumes it as a flag table (each entry whose bits intersect the value
// renders, in insertion order), WriteEnum as an enum table (the first entry
// whose key equals the value wins).
// ---------------------------------------------------------------------------
template <typename T>
class EnumNameCollection {
public:
	// The C# `public EnumNameCollection()` -- the parameterless shape.
	EnumNameCollection() = default;

	// The C# collection-initializer shape
	// `new EnumNameCollection<T>() { { flag, name }, ... }`: one Add per
	// element, in source order.
	EnumNameCollection(std::initializer_list<std::pair<T, const char*>> entries) {
		for (const auto& entry : entries)
			Add(entry.first, entry.second);
	}

	// The C# `public void Add(T flag, string name)` -- the key is
	// Convert.ToInt64(flag) (the enum's underlying value widened to int64).
	void Add(T flag, const char* name) {
		names_.emplace_back(static_cast<std::int64_t>(flag), name);
	}

	// The C# IEnumerable<KeyValuePair<long, string>> foreach surface.
	auto begin() const { return names_.begin(); }
	auto end() const { return names_.end(); }
	std::size_t size() const { return names_.size(); }

private:
	std::vector<std::pair<std::int64_t, const char*>> names_;
};

// ---------------------------------------------------------------------------
// FormatFlagsPlaceholder -- the inline rendering of the C# format-args
// `output.Write("flags({0:x4}) ", val)` fallbacks (the ITextOutput format-args
// Write overloads are documentedly deferred; this is the "format inline"
// remedy ITextOutput.hpp notes for exactly these ReflectionDisassembler
// arms). .NET `{0:x4}` over an Int64: lowercase hex of the full 64-bit
// two's complement, zero-padded to at least four digits (longer values are
// not truncated).
// ---------------------------------------------------------------------------
inline std::string FormatFlagsPlaceholder(std::int64_t value) {
	char buf[24];
	const int n = std::snprintf(buf, sizeof buf, "%llx",
		static_cast<unsigned long long>(static_cast<std::uint64_t>(value)));
	std::string hex(buf, static_cast<std::size_t>(n));
	if (hex.size() < 4)
		hex.insert(0, 4 - hex.size(), '0');
	return "flags(" + hex + ") ";
}

// ---------------------------------------------------------------------------
// WriteFlags<T> (ReflectionDisassembler.cs line 1970, the internal static) --
// render every table entry whose bits intersect the value, in table insertion
// order (NOT bit-value order), each name followed by one space. Entries with a
// null name suppress output but still mark their bits `tested`; any remaining
// bits render the `flags({0:x4}) ` fallback.
// ---------------------------------------------------------------------------
template <typename T>
void WriteFlags(T flags, const EnumNameCollection<T>& flagNames, Output::ITextOutput& output) {
	const std::int64_t val = static_cast<std::int64_t>(flags);
	std::int64_t tested = 0;
	for (const auto& pair : flagNames) {
		tested |= pair.first;
		if ((val & pair.first) != 0 && pair.second != nullptr) {
			output.Write(pair.second);
			output.Write(' ');
		}
	}
	const std::int64_t untested = val & ~tested;
	if (untested != 0)
		output.Write(FormatFlagsPlaceholder(untested));
}

// ---------------------------------------------------------------------------
// WriteEnum<T> (ReflectionDisassembler.cs line 1987, the internal static) --
// render the name of the FIRST entry whose key equals the value (followed by
// one space); a matching entry with a null name renders nothing (and does NOT
// fall through to the fallback); an unmatched non-zero value renders the
// `flags({0:x4}) ` fallback; an unmatched zero renders nothing.
// ---------------------------------------------------------------------------
template <typename T>
void WriteEnum(T enumValue, const EnumNameCollection<T>& enumNames, Output::ITextOutput& output) {
	const std::int64_t val = static_cast<std::int64_t>(enumValue);
	for (const auto& pair : enumNames) {
		if (pair.first == val) {
			if (pair.second != nullptr) {
				output.Write(pair.second);
				output.Write(' ');
			}
			return;
		}
	}
	if (val != 0)
		output.Write(FormatFlagsPlaceholder(val));
}

// ---------------------------------------------------------------------------
// The attribute-name tables (in C# source order). Each entry's comment carries
// the ILDasm spelling it renders (or why it renders nothing).
// ---------------------------------------------------------------------------

// ReflectionDisassembler.cs line 102 -- the non-visibility MethodAttributes
// half of the `.method` header split
// (`WriteFlags(md.Attributes & ~MethodAttributes.MemberAccessMask, ...)`).
inline const EnumNameCollection<MethodAttributes> methodAttributeFlags = {
	{ MethodAttributes::Final, "final" },
	{ MethodAttributes::HideBySig, "hidebysig" },
	{ MethodAttributes::SpecialName, "specialname" },
	{ MethodAttributes::PinvokeImpl, nullptr }, // handled separately (the pinvokeimpl("...") arm)
	{ MethodAttributes::UnmanagedExport, "export" },
	{ MethodAttributes::RTSpecialName, "rtspecialname" },
	{ MethodAttributes::RequireSecObject, "reqsecobj" },
	{ MethodAttributes::NewSlot, "newslot" },
	{ MethodAttributes::CheckAccessOnOverride, "strict" },
	{ MethodAttributes::Abstract, "abstract" },
	{ MethodAttributes::Virtual, "virtual" },
	{ MethodAttributes::Static, "static" },
	{ MethodAttributes::HasSecurity, nullptr }, // also invisible in ILDasm
};

// ReflectionDisassembler.cs line 118 -- the MemberAccessMask half of the
// `.method` header split (`WriteEnum(md.Attributes & MemberAccessMask, ...)`).
// PrivateScope (0) has no entry: the C# handles the compiler-controlled case
// separately (the isCompilerControlled check in DisassembleMethodHeaderInternal).
inline const EnumNameCollection<MethodAttributes> methodVisibility = {
	{ MethodAttributes::Private, "private" },
	{ MethodAttributes::FamANDAssem, "famandassem" },
	{ MethodAttributes::Assembly, "assembly" },
	{ MethodAttributes::Family, "family" },
	{ MethodAttributes::FamORAssem, "famorassem" },
	{ MethodAttributes::Public, "public" },
};

// ReflectionDisassembler.cs line 127 -- the signature-header calling
// convention (WriteEnum over Header.CallingConvention). Default (0) has a null
// name: the default managed convention renders nothing; Unmanaged (6) is NOT
// in the table and falls through to the flags fallback -- the unmanaged
// convention with custom `CallConv*` modifiers is rendered by the caller's
// custom-calling-convention arm instead.
inline const EnumNameCollection<SignatureCallingConvention> callingConvention = {
	{ SignatureCallingConvention::CDecl, "unmanaged cdecl" },
	{ SignatureCallingConvention::StdCall, "unmanaged stdcall" },
	{ SignatureCallingConvention::ThisCall, "unmanaged thiscall" },
	{ SignatureCallingConvention::FastCall, "unmanaged fastcall" },
	{ SignatureCallingConvention::VarArgs, "vararg" },
	{ SignatureCallingConvention::Default, nullptr },
};

// ReflectionDisassembler.cs line 136 -- the CodeTypeMask half of the
// MethodImplAttributes split
// (`WriteEnum(md.ImplAttributes & MethodImplAttributes.CodeTypeMask, ...)`).
inline const EnumNameCollection<MethodImplAttributes> methodCodeType = {
	{ MethodImplAttributes::IL, "cil" },
	{ MethodImplAttributes::Native, "native" },
	{ MethodImplAttributes::OPTIL, "optil" },
	{ MethodImplAttributes::Runtime, "runtime" },
};

// ReflectionDisassembler.cs line 143 -- the MethodImplAttributes remainder
// (`WriteFlags(md.ImplAttributes & ~(CodeTypeMask | ManagedMask), ...)`).
inline const EnumNameCollection<MethodImplAttributes> methodImpl = {
	{ MethodImplAttributes::Synchronized, "synchronized" },
	{ MethodImplAttributes::NoInlining, "noinlining" },
	{ MethodImplAttributes::NoOptimization, "nooptimization" },
	{ MethodImplAttributes::PreserveSig, "preservesig" },
	{ MethodImplAttributes::InternalCall, "internalcall" },
	{ MethodImplAttributes::ForwardRef, "forwardref" },
	{ MethodImplAttributes::AggressiveInlining, "aggressiveinlining" },
};

// ReflectionDisassembler.cs line 1270 -- the FieldAccessMask half of the
// `.field` header split (`WriteEnum(fd.Attributes & FieldAccessMask, ...)`).
// PrivateScope (0) has no entry (the isCompilerControlled check in
// DisassembleField handles it).
inline const EnumNameCollection<FieldAttributes> fieldVisibility = {
	{ FieldAttributes::Private, "private" },
	{ FieldAttributes::FamANDAssem, "famandassem" },
	{ FieldAttributes::Assembly, "assembly" },
	{ FieldAttributes::Family, "family" },
	{ FieldAttributes::FamORAssem, "famorassem" },
	{ FieldAttributes::Public, "public" },
};

// ReflectionDisassembler.cs line 1279 -- the FieldAttributes remainder
// (`WriteFlags(fd.Attributes & ~(FieldAccessMask | hasXAttributes), ...)`;
// the HasDefault/HasFieldMarshal/HasFieldRVA bits are masked OUT -- they are
// rendered by the .custom/at-<rva> arms, not as flags).
inline const EnumNameCollection<FieldAttributes> fieldAttributes = {
	{ FieldAttributes::Static, "static" },
	{ FieldAttributes::Literal, "literal" },
	{ FieldAttributes::InitOnly, "initonly" },
	{ FieldAttributes::SpecialName, "specialname" },
	{ FieldAttributes::RTSpecialName, "rtspecialname" },
	{ FieldAttributes::NotSerialized, "notserialized" },
};

// ReflectionDisassembler.cs line 1418 -- the whole PropertyAttributes value
// (WriteFlags, no mask split).
inline const EnumNameCollection<PropertyAttributes> propertyAttributes = {
	{ PropertyAttributes::SpecialName, "specialname" },
	{ PropertyAttributes::RTSpecialName, "rtspecialname" },
	{ PropertyAttributes::HasDefault, "hasdefault" },
};

// ReflectionDisassembler.cs line 1492 -- the whole EventAttributes value
// (WriteFlags, no mask split).
inline const EnumNameCollection<EventAttributes> eventAttributes = {
	{ EventAttributes::SpecialName, "specialname" },
	{ EventAttributes::RTSpecialName, "rtspecialname" },
};

// ReflectionDisassembler.cs line 1564 -- the VisibilityMask half of the
// `.class` header split (`WriteEnum(td.Attributes & VisibilityMask, ...)`).
// NotPublic (0) IS in the table ("private" -- the ILDasm spelling of a
// non-public top-level type).
inline const EnumNameCollection<TypeAttributes> typeVisibility = {
	{ TypeAttributes::Public, "public" },
	{ TypeAttributes::NotPublic, "private" },
	{ TypeAttributes::NestedPublic, "nested public" },
	{ TypeAttributes::NestedPrivate, "nested private" },
	{ TypeAttributes::NestedAssembly, "nested assembly" },
	{ TypeAttributes::NestedFamily, "nested family" },
	{ TypeAttributes::NestedFamANDAssem, "nested famandassem" },
	{ TypeAttributes::NestedFamORAssem, "nested famorassem" },
};

// ReflectionDisassembler.cs line 1575 -- the LayoutMask half of the `.class`
// header split. A private INSTANCE field in the C# (no `static` keyword; each
// instance carries an identical copy) -- modeled as the shared table the
// contents are. AutoLayout (0) is the zero entry, so a well-formed row always
// renders one of the three.
inline const EnumNameCollection<TypeAttributes> typeLayout = {
	{ TypeAttributes::AutoLayout, "auto" },
	{ TypeAttributes::SequentialLayout, "sequential" },
	{ TypeAttributes::ExplicitLayout, "explicit" },
};

// ReflectionDisassembler.cs line 1581 -- the StringFormatMask half of the
// `.class` header split (also a private INSTANCE field in the C#). AnsiClass
// (0) is the zero entry.
inline const EnumNameCollection<TypeAttributes> typeStringFormat = {
	{ TypeAttributes::AutoClass, "auto" },
	{ TypeAttributes::AnsiClass, "ansi" },
	{ TypeAttributes::UnicodeClass, "unicode" },
};

// ReflectionDisassembler.cs line 1587 -- the TypeAttributes remainder
// (`WriteFlags(td.Attributes & ~masks, ...)` with
// `masks = ClassSemanticsMask | VisibilityMask | LayoutMask | StringFormatMask`;
// the Interface bit itself renders through the separate "interface " prefix,
// RTSpecialName through the .custom arm).
inline const EnumNameCollection<TypeAttributes> typeAttributes = {
	{ TypeAttributes::Abstract, "abstract" },
	{ TypeAttributes::Sealed, "sealed" },
	{ TypeAttributes::SpecialName, "specialname" },
	{ TypeAttributes::Import, "import" },
	{ TypeAttributes::Serializable, "serializable" },
	{ TypeAttributes::WindowsRuntime, "windowsruntime" },
	{ TypeAttributes::BeforeFieldInit, "beforefieldinit" },
	{ TypeAttributes::HasSecurity, nullptr },
};

} // namespace ILSpy::Decompiler::Disassembler
