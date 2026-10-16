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

// Port of ICSharpCode.Decompiler/Disassembler/OpCodeInfo.cs -- the `OpCodeInfo` value
// struct (an IL opcode's code + display name + the docs-link encoded name). This is the
// first leaf of the `Disassembler` tree (the Phase-6 IL-text-output stack:
// ReflectionDisassembler / MethodBodyDisassembler / ILAmbience's PlainTextOutput), and
// the direct prerequisite of `ITextOutput::WriteReference(OpCodeInfo, bool)` -- the IL
// disassembler emits opcode references so the rich-text outputs can hyperlink them into
// the Microsoft docs.
//
// C#-to-C++ porting decisions:
//  * `public struct OpCodeInfo` -> a header-only value class (no vtable; the C# struct is
//    a plain value type). The port does NOT model the C# struct's copy-assignment corner
//    cases (the C# `readonly` fields make every copy a field-wise copy, which the C++
//    implicitly-defined copy does).
//  * `public readonly ILOpCode Code; public readonly string Name;` -> private members +
//    `Code()` / `Name()` accessors. The C# ctor's `name ?? ""` normalization becomes the
//    port's `std::string` itself: a C++ string cannot be null, so every constructed
//    instance is non-null by construction (the C# null only ever arises from
//    `default(OpCodeInfo)`, where the C# `EncodedName` would NRE on the null `Name` in the
//    `foreach`; the port's empty-string analog yields `""` instead -- the documented
//    safe-fallback convention for the degenerate default instance).
//  * The C# implicit parameterless ctor (`default(OpCodeInfo)`) -> an explicit default
//    ctor with `Code = ILOpCode::Nop` (the 0 value) and the empty name.
//  * `Equals(OpCodeInfo)` -> a plain member; the C# `operator==`/`operator!=` and the
//    object-Equals override become free `operator==`/`operator!=` over `Equals` (the C++
//    object-Equals analog has no separate role once == is value-based).
//  * `GetHashCode` -> `std::size_t` combining the code and a `std::hash<std::string>` of
//    the name with the C#'s exact multipliers (982451629 / 982451653). The C# string
//    hash is randomized per process, so exact values cannot be pinned; the port's
//    `std::hash` is the deterministic stand-in (equal inputs still hash equal, the only
//    contract any consumer relies on).
//  * `EncodedName` -> cached on first read like the C# (`encodedName != null` -> a
//    `mutable bool` computed flag, because the C# caches inside a property getter on a
//    struct field). Returns `const std::string&` so the caching is observable (two
//    calls return the same buffer). The C#'s own special-case table is ported verbatim,
//    including the `readonly.` -> `Reaonly` typo the C# source carries (the docs link is
//    generated from it, so reproducing the typo keeps the URL byte-identical).
//  * `Link` -> computed from `EncodedName()` each call (the C# property recomputes the
//    concatenation too; only `EncodedName` itself is cached).

#pragma once

#include "Decompiler/Metadata/ILOpCodes.hpp"  // ILOpCode

#include <cctype>
#include <cstddef>
#include <functional>  // std::hash
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Disassembler {

// The C# `public struct OpCodeInfo : IEquatable<OpCodeInfo>` -- one IL opcode's code and
// display name, plus the `EncodedName` used to build the Microsoft docs URL.
class OpCodeInfo {
public:
	// The C# `default(OpCodeInfo)` shape (all fields zeroed): Code = the 0 opcode
	// (ILOpCode::Nop), Name = the C# null -> the port's empty string.
	OpCodeInfo() : code_(Metadata::ILOpCode::Nop), name_() {}

	// The C# `public OpCodeInfo(ILOpCode code, string name)` (the `name ?? ""`
	// normalization is the std::string's own non-nullness).
	OpCodeInfo(Metadata::ILOpCode code, std::string name)
		: code_(code), name_(std::move(name)) {}

	// The C# `public readonly ILOpCode Code`.
	Metadata::ILOpCode Code() const { return code_; }

	// The C# `public readonly string Name`.
	const std::string& Name() const { return name_; }

	// The C# `Equals(OpCodeInfo other)`: code and name equality.
	bool Equals(const OpCodeInfo& other) const {
		return code_ == other.code_ && name_ == other.name_;
	}

	// The C# `override int GetHashCode()`:
	// `unchecked(982451629 * Code.GetHashCode() + 982451653 * Name.GetHashCode())`.
	// An enum's C# hash is its numeric value; the C# string hash is randomized per
	// process, so the port's deterministic `std::hash<std::string>` stands in (equal
	// inputs hash equal, the only contract consumers rely on).
	std::size_t GetHashCode() const {
		return 982451629u * static_cast<std::size_t>(static_cast<std::uint16_t>(code_))
			+ 982451653u * std::hash<std::string>{}(name_);
	}

	// The C# `string EncodedName` -- the docs-link-safe spelling of the opcode name
	// (dots to underscores, each segment PascalCased), cached on first read. The six
	// opcode-prefix names (`constrained.`, `no.`, `readonly.`, `tail.`, `unaligned.`,
	// `volatile.`) have hand-mapped spellings instead; note `readonly.` maps to
	// `Reaonly` -- the C# source's own typo, ported verbatim so the generated docs
	// URL stays byte-identical to the C#'s.
	const std::string& EncodedName() const {
		if (!encodedNameComputed_) {
			encodedName_ = ComputeEncodedName();
			encodedNameComputed_ = true;
		}
		return encodedName_;
	}

	// The C# `string Link` -- the Microsoft docs URL for the opcode.
	std::string Link() const {
		std::string lowered = EncodedName();  // copy, then ToLowerInvariant
		for (char& ch : lowered) {
			ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
		}
		return "https://docs.microsoft.com/dotnet/api/system.reflection.emit.opcodes." + lowered;
	}

private:
	// The C# `EncodedName` getter body, minus the caching (the caller caches).
	std::string ComputeEncodedName() const {
		// The C# `switch (Name)` special cases, in declaration order.
		if (name_ == "constrained.") return "Constrained";
		if (name_ == "no.") return "No";
		if (name_ == "readonly.") return "Reaonly";
		if (name_ == "tail.") return "Tailcall";
		if (name_ == "unaligned.") return "Unaligned";
		if (name_ == "volatile.") return "Volatile";
		// The general conversion: each '.' becomes '_', and the character after each
		// dot (plus the first character) is uppercased.
		std::string text;
		bool toUpperCase = true;
		for (char ch : name_) {
			if (ch == '.') {
				text += '_';
				toUpperCase = true;
			} else if (toUpperCase) {
				text += static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
				toUpperCase = false;
			} else {
				text += ch;
			}
		}
		return text;
	}

	Metadata::ILOpCode code_;
	std::string name_;
	// The C# caches `encodedName` in a field on first property read; the port mirrors
	// it with a mutable member (the getter is const, like the C# property).
	mutable std::string encodedName_;
	mutable bool encodedNameComputed_ = false;
};

// The C# `operator==` / `operator!=` (value equality through `Equals`).
inline bool operator==(const OpCodeInfo& lhs, const OpCodeInfo& rhs) {
	return lhs.Equals(rhs);
}
inline bool operator!=(const OpCodeInfo& lhs, const OpCodeInfo& rhs) {
	return !lhs.Equals(rhs);
}

} // namespace ILSpy::Decompiler::Disassembler
