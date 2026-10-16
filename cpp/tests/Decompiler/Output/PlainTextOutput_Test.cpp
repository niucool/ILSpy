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

// Tests for `ITextOutput` + `PlainTextOutput` (cpp/Decompiler/Output/, the ports of
// ICSharpCode.Decompiler/Output/ITextOutput.cs and PlainTextOutput.cs -- the plain-text
// write sink of the Phase-6 IL-text stack). The tests pin the two ctor shapes (the
// owning buffer and the external stream), the pending-indent mechanism (indentation is
// written only at the start of a line, after a WriteLine; a mid-line Indent applies to
// the NEXT line), the custom IndentationString, the 1-based line/column Location with
// the pending-indent adjustment, the "\r\n" line terminator, the opcode-reference
// suffix strip (the shared-prefix collapse plus the no-dot-writes-nothing crux), the
// text-only reference overloads, the no-op folding markers, the WriteLine extension,
// and the polymorphic dispatch through the `ITextOutput&` interface view.
//
// NOTE on the interface view: the reference-overload default arguments live on the
// `ITextOutput` declarations (a C++ override may not restate them), so the tests drive
// every defaulted call site through the `ITextOutput&` view -- exactly how the
// disassembler consumers (ReflectionDisassembler, ILAmbience) use the sink.

#include "Decompiler/Disassembler/OpCodeInfo.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/ITextOutput.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

namespace Metadata = ILSpy::Decompiler::Metadata;
namespace TS = ILSpy::Decompiler::TypeSystem;
namespace OUT = ILSpy::Decompiler::Output;
using OUT::ITextOutput;
using OUT::PlainTextOutput;
using OUT::WriteLine;
using ILSpy::Decompiler::Disassembler::OpCodeInfo;
using ILOpCode = Metadata::ILOpCode;
using TS::KnownType;
using TS::KnownTypeCode;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupMethod;

namespace {

// The parameterless ctor owns its buffer: everything written is visible in ToString().
TEST(PlainTextOutputTest, DefaultCtorAccumulatesWritesInToString) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.Write("Hello");
	output.Write(',');
	output.Write(' ');
	output.Write("world");
	EXPECT_EQ(concrete.ToString(), "Hello, world");
}

TEST(PlainTextOutputTest, ExternalWriterReceivesTheWrites) {
	// The C# `PlainTextOutput(TextWriter writer)` shape: every construction site wraps
	// a StringWriter the caller also reads; the port takes the string stream directly.
	std::ostringstream sink;
	PlainTextOutput concrete(sink);
	ITextOutput& output = concrete;
	output.Write("IL_0000: ");
	output.Write("ldarg.0");
	EXPECT_EQ(sink.str(), "IL_0000: ldarg.0");
	EXPECT_EQ(concrete.ToString(), "IL_0000: ldarg.0");
}

TEST(PlainTextOutputTest, IndentationStringDefaultsToTabAndIsSettable) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	EXPECT_EQ(output.IndentationString(), "\t");
	output.IndentationString("  ");
	EXPECT_EQ(output.IndentationString(), "  ");
}

TEST(PlainTextOutputTest, IndentAppliesOnlyAfterAWriteLine) {
	// The pending-indent mechanism: indentation is written at the START of a line, so
	// an Indent() followed by a write without a preceding WriteLine adds nothing.
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.Indent();
	output.Write("x");
	EXPECT_EQ(concrete.ToString(), "x");

	output.WriteLine();
	output.Write("y");
	EXPECT_EQ(concrete.ToString(), "x\r\n\ty");
}

TEST(PlainTextOutputTest, UnindentReducesThePendingIndentation) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.Indent();
	output.Indent();
	output.Unindent();
	output.WriteLine();
	output.Write("x");
	EXPECT_EQ(concrete.ToString(), "\r\n\tx");
}

TEST(PlainTextOutputTest, CustomIndentationStringIsUsedForTheIndentRun) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.IndentationString("    ");
	output.Indent();
	output.WriteLine();
	output.Write("x");
	EXPECT_EQ(concrete.ToString(), "\r\n    x");
}

TEST(PlainTextOutputTest, MultipleIndentLevelsRepeatTheIndentationString) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.Indent();
	output.Indent();
	output.WriteLine();
	output.Write("x");
	EXPECT_EQ(concrete.ToString(), "\r\n\t\tx");
}

TEST(PlainTextOutputTest, WriteLineWritesCarriageReturnLineFeed) {
	// The port writes "\r\n" (the Windows Environment.NewLine the ILSpy test suite
	// assumes -- the TextWriterTokenWriter kNewLine convention).
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.WriteLine();
	EXPECT_EQ(concrete.ToString(), "\r\n");
}

TEST(PlainTextOutputTest, LocationTracksLineAndColumn) {
	PlainTextOutput output;
	output.Write('a');
	output.Write('b');
	EXPECT_EQ(output.Location().Line, 1);
	EXPECT_EQ(output.Location().Column, 3);
	output.WriteLine();
	EXPECT_EQ(output.Location().Line, 2);
	EXPECT_EQ(output.Location().Column, 1);
}

TEST(PlainTextOutputTest, LocationIncludesThePendingIndentation) {
	// The C# `new TextLocation(line, column + (needsIndent ? indent : 0))`: after a
	// WriteLine (before the next character), the location points at where the next
	// character will land -- including the not-yet-written indentation.
	PlainTextOutput output;
	output.Indent();
	output.WriteLine();
	EXPECT_EQ(output.Location().Line, 2);
	EXPECT_EQ(output.Location().Column, 2);
	// Once a character is written, the indentation is flushed and no longer pending.
	output.Write("xy");
	EXPECT_EQ(output.Location().Line, 2);
	EXPECT_EQ(output.Location().Column, 4);
}

TEST(PlainTextOutputTest, WriteReferenceOpCodeWritesTheFullName) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.WriteReference(OpCodeInfo(ILOpCode::Ldc_i4_s, "ldc.i4.s"));
	EXPECT_EQ(concrete.ToString(), "ldc.i4.s");
}

TEST(PlainTextOutputTest, WriteReferenceOpCodeOmitSuffixStopsAtTheLastDot) {
	// omitSuffix keeps the name up to and including its LAST dot, so the operand-sized
	// opcode forms collapse to the shared prefix.
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.WriteReference(OpCodeInfo(ILOpCode::Ldc_i4_s, "ldc.i4.s"), true);
	EXPECT_EQ(concrete.ToString(), "ldc.i4.");
}

TEST(PlainTextOutputTest, WriteReferenceOpCodeOmitSuffixOnDotlessNameWritesNothing) {
	// The C# `if (lastDot > 0)` guard: with no dot, the omitSuffix mode writes
	// NOTHING (not the full name -- that is only the omitSuffix == false branch).
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.WriteReference(OpCodeInfo(ILOpCode::Add, "add"), true);
	EXPECT_EQ(concrete.ToString(), "");
}

TEST(PlainTextOutputTest, WriteReferenceOpCodeOmitSuffixOnTrailingDotNameIsUnchanged) {
	// A name whose last dot is its final character keeps the whole name (the prefix
	// opcode spellings like "constrained." collapse to themselves).
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.WriteReference(OpCodeInfo(ILOpCode::Nop, "constrained."), true);
	EXPECT_EQ(concrete.ToString(), "constrained.");
}

TEST(PlainTextOutputTest, WriteReferenceMetadataWritesOnlyTheText) {
	// The metadata/handle/protocol/isDefinition hyperlink hints are ignored by the
	// plain-text sink; only the text is written. (A MetadataFile with a bad path is
	// the cheapest instance -- the ctor degrades to invalid without throwing.)
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	Metadata::MetadataFile metadata("no-such-file.dll");
	output.WriteReference(metadata, 0x06000001, "System.Object::ToString");
	EXPECT_EQ(concrete.ToString(), "System.Object::ToString");
}

TEST(PlainTextOutputTest, WriteReferenceTypeAndMemberWriteOnlyTheText) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	auto type = std::make_shared<KnownType>(KnownTypeCode::Object);
	output.WriteReference(*type, "object");
	LookupCompilation compilation;
	auto method = std::make_shared<LookupMethod>("ToString", compilation);
	output.WriteReference(*method, "ToString");
	EXPECT_EQ(concrete.ToString(), "objectToString");
}

TEST(PlainTextOutputTest, WriteLocalReferenceWritesOnlyTheText) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	int local = 0;
	output.WriteLocalReference("x", &local);
	EXPECT_EQ(concrete.ToString(), "x");
}

TEST(PlainTextOutputTest, FoldMarkersAreNoOps) {
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.Write("a");
	output.MarkFoldStart();
	output.MarkDefinitionStart();
	output.MarkFoldEnd();
	output.Write("b");
	EXPECT_EQ(concrete.ToString(), "ab");
}

TEST(PlainTextOutputTest, WriteLineExtensionWritesTextThenLineBreak) {
	// The TextOutputExtensions port: Write(text) + WriteLine().
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	WriteLine(output, "IL_0001: ret");
	EXPECT_EQ(concrete.ToString(), "IL_0001: ret\r\n");
}

TEST(PlainTextOutputTest, DispatchesThroughTheITextOutputInterfaceView) {
	// The same interface view the disassembler consumers hold: virtual dispatch for
	// every member, with the reference-overload defaults resolved from the interface.
	PlainTextOutput concrete;
	ITextOutput& output = concrete;
	output.Indent();
	output.WriteLine();
	output.Write("x");
	output.WriteReference(OpCodeInfo(ILOpCode::Ldarg_0, "ldarg.0"));
	output.WriteLine();
	EXPECT_EQ(concrete.ToString(), "\r\n\txldarg.0\r\n");
}

TEST(PlainTextOutputTest, TwoOutputsDoNotShareState) {
	PlainTextOutput a;
	PlainTextOutput b;
	a.Write("a");
	b.Write("b");
	EXPECT_EQ(a.ToString(), "a");
	EXPECT_EQ(b.ToString(), "b");
}

} // namespace
