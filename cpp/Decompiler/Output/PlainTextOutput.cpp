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

// Implementation of ICSharpCode.Decompiler/Output/PlainTextOutput.cs (see the header
// for the porting decisions). The folding markers are no-ops and every reference
// overload reduces to `Write(text)` -- faithfully mirroring the C# class, which only
// distinguishes itself from raw writing by the indent/location bookkeeping.

#include "Decompiler/Output/PlainTextOutput.hpp"

#include "Decompiler/Disassembler/OpCodeInfo.hpp"  // OpCodeInfo (complete for Name())

namespace ILSpy::Decompiler::Output {

// The C# `TextWriter.NewLine` default on the build platform (`Environment.NewLine`);
// the port writes the Windows `"\r\n"` the ILSpy test suite assumes (the
// `TextWriterTokenWriter::kNewLine` convention).
static constexpr std::string_view kNewLine = "\r\n";

PlainTextOutput::PlainTextOutput()
	: ownedBuffer_(), writer_(&ownedBuffer_) {}

PlainTextOutput::PlainTextOutput(std::ostringstream& writer)
	: ownedBuffer_(), writer_(&writer) {}

// The C# `public TextLocation Location =>
// new TextLocation(line, column + (needsIndent ? indent : 0));`
CSharp::Syntax::TextLocation PlainTextOutput::Location() const {
	return CSharp::Syntax::TextLocation(line_, column_ + (needsIndent_ ? indent_ : 0));
}

// The C# `public override string ToString() => writer.ToString();`
std::string PlainTextOutput::ToString() const {
	return writer_->str();
}

std::string PlainTextOutput::IndentationString() const {
	return indentationString_;
}

void PlainTextOutput::IndentationString(std::string value) {
	indentationString_ = std::move(value);
}

void PlainTextOutput::Indent() {
	++indent_;
}

void PlainTextOutput::Unindent() {
	--indent_;
}

// The C# `void WriteIndent()` -- flush the pending indentation (the tab run) before the
// first character of a line, then clear the pending flag.
void PlainTextOutput::WriteIndent() {
	if (needsIndent_) {
		needsIndent_ = false;
		for (int i = 0; i < indent_; i++) {
			*writer_ << indentationString_;
		}
		column_ += indent_;
	}
}

// The C# `public void Write(char ch)`.
void PlainTextOutput::Write(char ch) {
	WriteIndent();
	*writer_ << ch;
	column_++;
}

// The C# `public void Write(string text)` (the column counts UTF-16 chars in C#, bytes
// here -- the IL-text output is ASCII).
void PlainTextOutput::Write(std::string_view text) {
	WriteIndent();
	*writer_ << text;
	column_ += static_cast<int>(text.size());
}

// The C# `public void WriteLine()` -- the line terminator, then the pending-indent flag
// for the next line, and the 1-based line/column reset.
void PlainTextOutput::WriteLine() {
	*writer_ << kNewLine;
	needsIndent_ = true;
	line_++;
	column_ = 1;
}

// The C# `public void WriteReference(Disassembler.OpCodeInfo opCode, bool omitSuffix)`:
// with `omitSuffix`, keep the name up to and including its LAST dot (so the sized
// opcode forms collapse to the shared prefix); when there is no dot at position > 0,
// write NOTHING in this mode (the C# `if (lastDot > 0)` guard -- not the full name).
void PlainTextOutput::WriteReference(const Disassembler::OpCodeInfo& opCode, bool omitSuffix) {
	if (omitSuffix) {
		std::size_t lastDot = opCode.Name().rfind('.');
		if (lastDot != std::string::npos && lastDot > 0) {
			Write(std::string_view(opCode.Name()).substr(0, lastDot + 1));
		}
	} else {
		Write(opCode.Name());
	}
}

// The C# plain-text reference overloads: the hyperlink hints are ignored, only the
// text is written.
void PlainTextOutput::WriteReference(const MetadataFile& metadata, std::uint32_t handle,
	std::string_view text, std::string_view protocol, bool isDefinition) {
	(void)metadata; (void)handle; (void)protocol; (void)isDefinition;
	Write(text);
}

void PlainTextOutput::WriteReference(const IType& type, std::string_view text, bool isDefinition) {
	(void)type; (void)isDefinition;
	Write(text);
}

void PlainTextOutput::WriteReference(const IMember& member, std::string_view text, bool isDefinition) {
	(void)member; (void)isDefinition;
	Write(text);
}

void PlainTextOutput::WriteLocalReference(std::string_view text, const void* reference,
	bool isDefinition, bool isHoverOnly) {
	(void)reference; (void)isDefinition; (void)isHoverOnly;
	Write(text);
}

// The C# folding-marker trio: empty methods in PlainTextOutput.
void PlainTextOutput::MarkFoldStart(std::string_view collapsedText, bool defaultCollapsed,
	bool isDefinition) {
	(void)collapsedText; (void)defaultCollapsed; (void)isDefinition;
}

void PlainTextOutput::MarkDefinitionStart() {}

void PlainTextOutput::MarkFoldEnd() {}

} // namespace ILSpy::Decompiler::Output
