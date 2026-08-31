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

// Tests for `OpCodeInfo` (cpp/Decompiler/Disassembler/OpCodeInfo.hpp, the port of
// ICSharpCode.Decompiler/Disassembler/OpCodeInfo.cs -- the IL opcode value struct).
// The tests pin the ctor storage, the value equality, the hash's equal-inputs-equal-hash
// contract, the `EncodedName` docs-link spelling (the six prefix special cases --
// including the C# source's own `readonly.` -> `Reaonly` typo, ported verbatim so the
// generated docs URL stays byte-identical -- plus the dot-to-underscore PascalCase
// conversion), the first-read caching, and the `Link` URL composition.

#include "Decompiler/Disassembler/OpCodeInfo.hpp"

#include <gtest/gtest.h>

#include <string>

namespace Metadata = ILSpy::Decompiler::Metadata;
using ILSpy::Decompiler::Disassembler::OpCodeInfo;

namespace {

TEST(OpCodeInfoTest, CtorStoresCodeAndName) {
	OpCodeInfo op(Metadata::ILOpCode::Ldfld, "ldfld");
	EXPECT_EQ(static_cast<std::uint16_t>(op.Code()), 0x7B);
	EXPECT_EQ(op.Name(), "ldfld");
}

TEST(OpCodeInfoTest, CtorAcceptsEmptyName) {
	// The C# `name ?? ""` normalization: the port's std::string is never null.
	OpCodeInfo op(Metadata::ILOpCode::Nop, "");
	EXPECT_EQ(op.Name(), "");
}

TEST(OpCodeInfoTest, DefaultCtorIsNopWithEmptyName) {
	// The C# `default(OpCodeInfo)` shape (all fields zeroed).
	OpCodeInfo op;
	EXPECT_EQ(op.Code(), Metadata::ILOpCode::Nop);
	EXPECT_EQ(op.Name(), "");
}

TEST(OpCodeInfoTest, EqualsComparesCodeAndName) {
	OpCodeInfo a(Metadata::ILOpCode::Ldc_i4_s, "ldc.i4.s");
	OpCodeInfo b(Metadata::ILOpCode::Ldc_i4_s, "ldc.i4.s");
	OpCodeInfo sameName(Metadata::ILOpCode::Ldc_i4, "ldc.i4.s");
	OpCodeInfo sameCode(Metadata::ILOpCode::Ldc_i4_s, "ldc.i4");
	EXPECT_TRUE(a.Equals(b));
	EXPECT_TRUE(a.Equals(a));
	EXPECT_FALSE(a.Equals(sameName));
	EXPECT_FALSE(a.Equals(sameCode));
}

TEST(OpCodeInfoTest, OperatorEqualsAndNotEquals) {
	OpCodeInfo a(Metadata::ILOpCode::Ldarg_0, "ldarg.0");
	OpCodeInfo b(Metadata::ILOpCode::Ldarg_0, "ldarg.0");
	OpCodeInfo c(Metadata::ILOpCode::Ldarg_1, "ldarg.1");
	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a != b);
	EXPECT_FALSE(a == c);
	EXPECT_TRUE(a != c);
}

TEST(OpCodeInfoTest, GetHashCodeEqualForEqualValuesAndDistinctForDistinctValues) {
	// The C# string hash is randomized per process, so only the hash contract is
	// pinnable: equal values hash equal, distinct values (usually) differ.
	OpCodeInfo a(Metadata::ILOpCode::Stloc_0, "stloc.0");
	OpCodeInfo b(Metadata::ILOpCode::Stloc_0, "stloc.0");
	OpCodeInfo c(Metadata::ILOpCode::Stloc_1, "stloc.1");
	EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
	EXPECT_NE(a.GetHashCode(), c.GetHashCode());
}

TEST(OpCodeInfoTest, EncodedNamePrefixSpecialCases) {
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Nop, "constrained.").EncodedName(), "Constrained");
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Nop, "no.").EncodedName(), "No");
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Nop, "tail.").EncodedName(), "Tailcall");
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Nop, "unaligned.").EncodedName(), "Unaligned");
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Nop, "volatile.").EncodedName(), "Volatile");
}

TEST(OpCodeInfoTest, EncodedNameReadonlyTypoIsPortedVerbatim) {
	// The C# source maps "readonly." to "Reaonly" (its own typo), and the docs link is
	// generated from it -- the typo is part of the link contract, ported verbatim.
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Nop, "readonly.").EncodedName(), "Reaonly");
}

TEST(OpCodeInfoTest, EncodedNameConvertsDotsAndUpperCasesSegments) {
	// The general conversion: '.' -> '_', the first char and each post-dot char
	// uppercased.
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Ldarg_0, "ldarg.0").EncodedName(), "Ldarg_0");
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Ldc_i4_s, "ldc.i4.s").EncodedName(), "Ldc_I4_S");
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Add, "add").EncodedName(), "Add");
	EXPECT_EQ(OpCodeInfo(Metadata::ILOpCode::Constrained, "constrained").EncodedName(), "Constrained");
}

TEST(OpCodeInfoTest, EncodedNameIsCachedOnFirstRead) {
	// The C# caches `encodedName` in a field on first property read; the port mirrors
	// it, observable as the identical buffer returned across two calls.
	OpCodeInfo op(Metadata::ILOpCode::Ldloc_0, "ldloc.0");
	const std::string& first = op.EncodedName();
	const std::string& second = op.EncodedName();
	EXPECT_EQ(&first, &second);
	EXPECT_EQ(first, "Ldloc_0");
}

TEST(OpCodeInfoTest, LinkLowercasesTheEncodedName) {
	OpCodeInfo op(Metadata::ILOpCode::Ldarg_0, "ldarg.0");
	EXPECT_EQ(op.Link(),
		"https://docs.microsoft.com/dotnet/api/system.reflection.emit.opcodes.ldarg_0");
}

TEST(OpCodeInfoTest, LinkUsesThePrefixSpecialCaseSpelling) {
	// "volatile." encodes to "Volatile", the link lowercases it back.
	OpCodeInfo op(Metadata::ILOpCode::Nop, "volatile.");
	EXPECT_EQ(op.Link(),
		"https://docs.microsoft.com/dotnet/api/system.reflection.emit.opcodes.volatile");
}

TEST(OpCodeInfoTest, LinkForReadonlyCarriesTheTypo) {
	// The typo flows into the URL (matching the C#-generated docs link exactly).
	OpCodeInfo op(Metadata::ILOpCode::Nop, "readonly.");
	EXPECT_EQ(op.Link(),
		"https://docs.microsoft.com/dotnet/api/system.reflection.emit.opcodes.reaonly");
}

} // namespace
