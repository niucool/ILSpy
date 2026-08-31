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

// Port of ICSharpCode.Decompiler/Output/PlainTextOutput.cs -- the `PlainTextOutput`
// class (the plain-text `ITextOutput` sink; the co-located `TextOutputWithRollback`
// internal class is documentedly deferred -- its only C# consumer is
// ReflectionDisassembler's lambda-display rollback). The plain-text sink is what the
// IL view's `ILAmbience` and every CLI/`ILLanguage` decompilation write through; the
// rich-text (hyperlink) outputs are the GUI's deferred counterparts.
//
// C#-to-C++ porting decisions:
//  * `public sealed class PlainTextOutput : ITextOutput, IDisposable` -> a `final`
//    class; the C# `Dispose` (releasing an owned StringWriter) becomes RAII -- the
//    owning ostringstream member destructs with the object (the C# `ownsWriter` flag).
//  * The C# ctor pair -- `PlainTextOutput()` (a fresh internal StringWriter, owned)
//    and `PlainTextOutput(TextWriter writer)` -- ports as the default ctor (owning
//    `std::ostringstream`) plus an explicit ctor over `std::ostringstream&`. Every C#
//    construction site wraps a `StringWriter` (ILAmbience, SaveCodeHelper,
//    ProjectExporter) or uses the parameterless shape, so the string-stream reference
//    is the faithful external-writer shape; the C# ArgumentNullException is n/a (C++
//    references are non-null).
//  * `TextWriter.WriteLine()` writes `Environment.NewLine` (platform-dependent); the
//    port writes `"\r\n"` (the Windows value the ILSpy test suite assumes -- the
//    `TextWriterTokenWriter::kNewLine` convention).
//  * `Write(string text)`'s `column += text.Length` counts UTF-16 chars; the port
//    counts bytes of the `std::string_view`. The IL-text output is ASCII, so the two
//    coincide for everything this sink renders (a documented divergence for non-ASCII).
//  * The `WriteReference` overloads all reduce to `Write(text)` in plain text (the C#
//    does the same); the opcode overload keeps the `omitSuffix` suffix strip. The
//    default arguments live on the `ITextOutput` interface declarations (a C++ override
//    may not restate them).
//  * The folding markers are no-ops (the C# implements them as empty methods).

#pragma once

#include "Decompiler/CSharp/Syntax/TextLocation.hpp"  // TextLocation
#include "Decompiler/Output/ITextOutput.hpp"           // ITextOutput (+ forward decls)

#include <sstream>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::Output {

// The C# `public sealed class PlainTextOutput : ITextOutput` -- a plain-text sink with
// indent tracking and 1-based line/column location accounting.
class PlainTextOutput final : public ITextOutput {
public:
	// The C# `public PlainTextOutput()` -- a fresh internal `StringWriter`, owned.
	// (RAII replaces the C# `Dispose`/`ownsWriter` pair.)
	PlainTextOutput();

	// The C# `public PlainTextOutput(TextWriter writer)` -- write into the caller's
	// writer (every C# call site wraps a `StringWriter`; the port takes the string
	// stream directly). The writer must outlive this object (C# reference semantics).
	explicit PlainTextOutput(std::ostringstream& writer);

	// The C# `public TextLocation Location` -- the current 1-based position; a pending
	// (not-yet-written) indentation is included so the location points at where the
	// next character will land.
	CSharp::Syntax::TextLocation Location() const;

	// The C# `override string ToString()` -- everything written so far.
	std::string ToString() const;

	// `ITextOutput` (the default arguments live on the interface declarations).
	std::string IndentationString() const override;
	void IndentationString(std::string value) override;
	void Indent() override;
	void Unindent() override;
	void Write(char ch) override;
	void Write(std::string_view text) override;
	void WriteLine() override;
	void WriteReference(const Disassembler::OpCodeInfo& opCode, bool omitSuffix) override;
	void WriteReference(const MetadataFile& metadata, std::uint32_t handle,
		std::string_view text, std::string_view protocol, bool isDefinition) override;
	void WriteReference(const IType& type, std::string_view text, bool isDefinition) override;
	void WriteReference(const IMember& member, std::string_view text, bool isDefinition) override;
	void WriteLocalReference(std::string_view text, const void* reference,
		bool isDefinition, bool isHoverOnly) override;
	void MarkFoldStart(std::string_view collapsedText, bool defaultCollapsed,
		bool isDefinition) override;
	void MarkDefinitionStart() override;
	void MarkFoldEnd() override;

private:
	// The C# `void WriteIndent()` -- flush the pending indentation before the next
	// character (and clear the pending flag).
	void WriteIndent();

	// The owned buffer (engaged only by the parameterless ctor); `writer_` points at
	// it or at the caller's stream (the C# `writer`/`ownsWriter` pair).
	std::ostringstream ownedBuffer_;
	std::ostringstream* writer_;
	std::string indentationString_ = "\t";
	int indent_ = 0;
	bool needsIndent_ = false;
	int line_ = 1;
	int column_ = 1;
};

} // namespace ILSpy::Decompiler::Output
