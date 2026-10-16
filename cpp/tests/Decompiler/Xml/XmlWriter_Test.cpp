// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The XmlWriter core tests: every case mirrors the identically-named case of
// the XmlWriteProbe gold probe (C:/temp-probe/XmlWriteProbe/src/Program.cs)
// and compares byte-for-byte with the generated gold fixture
// (TestFixtures/XmlWriteGold.hpp). The error arms pin the exception type
// mapping (the .NET InvalidOperationException -> std::runtime_error,
// ArgumentException -> std::invalid_argument, XmlException -> Xml::XmlException)
// and the exact messages.

#include "TestFixtures/XmlWriteGold.hpp"

#include <gtest/gtest.h>

#include <string>

#include "Decompiler/Xml/XmlConvert.hpp"
#include "Decompiler/Xml/XmlWriter.hpp"

namespace {

using ILSpy::Decompiler::Xml::ConformanceLevel;
using ILSpy::Decompiler::Xml::NamespaceHandling;
using ILSpy::Decompiler::Xml::NewLineHandling;
using ILSpy::Decompiler::Xml::XmlException;
using ILSpy::Decompiler::Xml::XmlWriter;
using ILSpy::Decompiler::Xml::XmlWriterSettings;
using ILSpy::Decompiler::Xml::XmlWriterSink;

// Reconstructs the probe's "EXCEPTION: <type>: <message>" line for the port's
// mapped exception types.
std::string PortExceptionLine(const std::exception& ex)
{
    std::string type = "InvalidOperationException";
    if (dynamic_cast<const XmlException*>(&ex) != nullptr)
        type = "XmlException";
    else if (dynamic_cast<const std::invalid_argument*>(&ex) != nullptr)
        type = "ArgumentException";
    std::string message = ex.what();
    // The probe escapes CRLF inside the message text.
    std::string escaped;
    for (char c : message) {
        if (c == '\r')
            escaped += "\\r";
        else if (c == '\n')
            escaped += "\\n";
        else
            escaped += c;
    }
    return "EXCEPTION: " + type + ": " + escaped;
}

// Runs a writer-driving lambda and renders the output the way the probe does:
// the sink text, or the exception line.
template <typename TDriver>
std::string RunCase(TDriver driver)
{
    try {
        std::string out;
        driver(&out);
        return out;
    } catch (const std::exception& ex) {
        return PortExceptionLine(ex);
    }
}

std::string Gold(const char* id)
{
    std::string_view text = ILSpy::Tests::XmlWriteGold::Find(id);
    return std::string(text);
}

void ExpectGold(const char* id, const std::string& actual)
{
    EXPECT_EQ(Gold(id), actual) << "case " << id;
}

} // namespace

namespace ILSpy::Tests {

// The default settings render (declaration + content, no indent).
TEST(XmlWriterTest, PlainDocument)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartDocument();
        w.WriteStartElement("root");
        w.WriteAttributeString("a", "v");
        w.WriteString("text");
        w.WriteEndElement();
        w.WriteEndDocument();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_plain", actual);
}

// The XNode.ToString shape (omitted declaration, indent) over the node kinds.
TEST(XmlWriterTest, OmitDeclarationIndented)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.Indent = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("root");
        w.WriteAttributeString("a", "1");
        w.WriteAttributeString("b", "2");
        w.WriteComment("comment");
        w.WriteProcessingInstruction("pi", "data");
        w.WriteCData("cdata");
        w.WriteStartElement("child");
        w.WriteString("text");
        w.WriteFullEndElement();
        w.WriteStartElement("empty");
        w.WriteEndElement();
        w.WriteWhitespace("  \t");
        w.WriteStartElement("c2");
        w.WriteStartElement("c3");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_omitdecl_indented", actual);
}

// The indentation shapes: mixed content, self-closing, attribute-only,
// text-only elements.
TEST(XmlWriterTest, IndentShapes)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.Indent = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("root");
        w.WriteStartElement("holder1");
        w.WriteString("t");
        w.WriteStartElement("child");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.WriteStartElement("empty");
        w.WriteEndElement();
        w.WriteStartElement("attrs");
        w.WriteAttributeString("x", "y");
        w.WriteFullEndElement();
        w.WriteStartElement("ws");
        w.WriteString(" ");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_indent_shapes", actual);
}

// Generated prefixes: a null-prefix element with a new namespace REBINDS the
// default namespace; a null-prefix attribute gets p{n}.
TEST(XmlWriterTest, AutoPrefixGeneration)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.Indent = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement(nullptr, "e1", "urn:a");
        w.WriteAttributeString(nullptr, "attr", "urn:c", "v");
        w.WriteStartElement(nullptr, "e2", "urn:a");
        w.WriteFullEndElement();
        w.WriteStartElement(nullptr, "e3", "urn:b");
        w.WriteFullEndElement();
        w.WriteStartElement(nullptr, "e4", "urn:d");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_autoprefix", actual);
}

TEST(XmlWriterTest, ExplicitPrefixReuse)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("x", "e1", "urn:a");
        w.WriteStartElement("x", "e2", "urn:a");
        w.WriteStartElement("x", "e3", "urn:a");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.WriteStartElement(nullptr, "e4", "urn:d");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_explicit", actual);
}

// A same-scope prefix redefinition through WriteStartElement is a shadowing
// rebind (no throw); the same start tag through a different mechanism throws.
TEST(XmlWriterTest, ExplicitPrefixRedefinitionShadowing)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("x", "e1", "urn:a");
        w.WriteStartElement("x", "e2", "urn:b");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_explicit_redefine", actual);
}

// The xmlns declarations through WriteAttributeString (the special-attribute
// interception): a nsless element cannot rebind the default prefix in its
// own start tag.
TEST(XmlWriterTest, XmlnsAttributesOnNslessElementThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("root");
        w.WriteAttributeString("", "xmlns", "http://www.w3.org/2000/xmlns/", "urn:d");
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_xmlns_attrs", actual);
}

TEST(XmlWriterTest, DuplicateDefaultDeclarationThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("root");
        w.WriteAttributeString("", "xmlns", "http://www.w3.org/2000/xmlns/", "urn:d");
        w.WriteAttributeString("", "xmlns", "http://www.w3.org/2000/xmlns/", "urn:d");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_dup_default_decl", actual);
}

TEST(XmlWriterTest, OmitDuplicatesSuppressesRedundantDeclarations)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.NamespaceHandling = NamespaceHandling::OmitDuplicates;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("root");
        w.WriteAttributeString("", "xmlns", "http://www.w3.org/2000/xmlns/", "urn:d");
        w.WriteStartElement(nullptr, "e", "urn:d");
        w.WriteAttributeString("", "xmlns", "http://www.w3.org/2000/xmlns/", "urn:d");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_omitduplicates", actual);
}

TEST(XmlWriterTest, BindingToReservedNamespaceThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("root");
        w.WriteAttributeString("xmlns", "p", "http://www.w3.org/2000/xmlns/", "http://www.w3.org/2000/xmlns/");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_reserved_bind", actual);
}

TEST(XmlWriterTest, EmptyPrefixedDeclarationThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("root");
        w.WriteAttributeString("xmlns", "p", "http://www.w3.org/2000/xmlns/", "");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_empty_prefixed_decl", actual);
}

TEST(XmlWriterTest, PrefixWithEmptyNamespaceThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("p", "e", "");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_prefix_for_empty_ns", actual);
}

// The element-text escaping table.
TEST(XmlWriterTest, ElementTextEscaping)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("e");
        w.WriteString("a<b>&c\"d'e\tf\ng\r\nh\xE4\xB8\xAD\xE6\x96\x87");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_escape_text", actual);
}

// The attribute-value escaping table (quotes and the whitespace entities).
TEST(XmlWriterTest, AttributeValueEscaping)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("e");
        w.WriteAttributeString("a", "x<b>&y\"z'\tq\nw\re\r\nf\xE4\xB8\xAD");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_escape_attr", actual);
}

// The comment/PI/CDATA fixups (the "--" and "?>" splits, the "]]>" split).
TEST(XmlWriterTest, CommentPiCDataEscaping)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("e");
        w.WriteComment("a--b-");
        w.WriteProcessingInstruction("pi", "a?>b");
        w.WriteCData("x]]>y");
        w.WriteString("t");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_escape_comment_pi_cdata", actual);
}

TEST(XmlWriterTest, NewLineEntitize)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.NewLineHandling = NewLineHandling::Entitize;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("e");
        w.WriteAttributeString("a", "x\ny\rz\r\nw\tq");
        w.WriteString("a\nb\rc\r\nd\te");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_newline_entitize", actual);
}

TEST(XmlWriterTest, NewLineNone)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.NewLineHandling = NewLineHandling::None;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("e");
        w.WriteAttributeString("a", "x\ny\rz\r\nw\tq");
        w.WriteString("a\nb\rc\r\nd\te");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_newline_none", actual);
}

// Fragment conformance: top-level text and multiple elements.
TEST(XmlWriterTest, FragmentConformance)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.ConformanceLevel = ConformanceLevel::Fragment;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteString("t1");
        w.WriteString("t2");
        w.WriteStartElement("e");
        w.WriteFullEndElement();
        w.WriteString(" t3");
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_fragment", actual);
}

TEST(XmlWriterTest, FragmentStartDocumentThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.ConformanceLevel = ConformanceLevel::Fragment;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartDocument();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_fragment_decl_throws", actual);
}

// The state-machine error arms.
TEST(XmlWriterTest, EndAttributeWithoutStartThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("e");
        w.WriteEndAttribute();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_state_endattr_no_start", actual);
}

TEST(XmlWriterTest, TextAtTopLevelThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteString("t");
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_state_text_toplevel", actual);
}

TEST(XmlWriterTest, TwoRootElementsThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("a");
        w.WriteEndElement();
        w.WriteStartElement("b");
        w.WriteEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_state_two_roots", actual);
}

TEST(XmlWriterTest, EndElementWithoutStartThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_state_endelem_none", actual);
}

// The DOCTYPE renders (PUBLIC/SYSTEM/subset) and the indented position.
TEST(XmlWriterTest, DocTypePublic)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.Indent = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteDocType("root", "-//pub//en", "sys.dtd", "<!ELEMENT root EMPTY>");
        w.WriteStartElement("root");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_doctype", actual);
}

TEST(XmlWriterTest, DocTypeSystem)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteDocType("root", nullptr, "sys.dtd", nullptr);
        w.WriteStartElement("root");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_doctype_system", actual);
}

// The xml:space/xml:lang special attributes (validated + written as-is).
TEST(XmlWriterTest, SpecialXmlAttributes)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("e");
        w.WriteAttributeString("xml", "space", "http://www.w3.org/XML/1998/namespace", "preserve");
        w.WriteAttributeString("xml", "lang", "http://www.w3.org/XML/1998/namespace", "en-US");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_special_xmlattrs", actual);
}

TEST(XmlWriterTest, InvalidXmlSpaceValueThrows)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("e");
        w.WriteAttributeString("xml", "space", "http://www.w3.org/XML/1998/namespace", "bogus");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_special_xmlspace_invalid", actual);
}

// The 6144-unit buffer: flushes mid-text, the indent decisions across the
// boundary, and the adjacent-CDATA merge break.
TEST(XmlWriterTest, BigFlushBoundary)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.Indent = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("root");
        std::string x(7000, 'x');
        w.WriteString(x);
        w.WriteStartElement("child");
        w.WriteString("y");
        w.WriteFullEndElement();
        std::string a(6100, 'a');
        w.WriteCData(a);
        w.WriteCData("tail");
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_bigflush", actual);
}

TEST(XmlWriterTest, DeclarationStandaloneYes)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartDocument(true);
        w.WriteStartElement("root");
        w.WriteFullEndElement();
        w.WriteEndDocument();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_decl_standalone", actual);
}

// Top-level comments/PIs around the root (the indent placement).
TEST(XmlWriterTest, TopLevelCommentsAndPIs)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        settings.Indent = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteComment("before");
        w.WriteProcessingInstruction("pi", "before");
        w.WriteStartElement("root");
        w.WriteFullEndElement();
        w.WriteComment("after");
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_two_top_level_pi_comment", actual);
}

TEST(XmlWriterTest, ScopeExitFreesGeneratedPrefix)
{
    std::string actual = RunCase([](std::string* out) {
        XmlWriterSettings settings;
        settings.OmitXmlDeclaration = true;
        XmlWriter w(std::move(settings), XmlWriterSink::Text);
        w.WriteStartElement("r");
        w.WriteStartElement(nullptr, "e1", "urn:a");
        w.WriteFullEndElement();
        w.WriteStartElement(nullptr, "e2", "urn:d");
        w.WriteFullEndElement();
        w.WriteFullEndElement();
        w.Close();
        *out = w.OutputUtf8();
    });
    ExpectGold("w_ns_autoprefix_scope_exit", actual);
}

} // namespace ILSpy::Tests
