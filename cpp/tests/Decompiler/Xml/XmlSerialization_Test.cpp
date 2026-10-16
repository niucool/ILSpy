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

// The XNode serialization tests: every case mirrors the identically-named
// n_* case of the XmlWriteProbe gold probe over the real System.Xml.Linq
// (C:/temp-probe/XmlWriteProbe/src/Program.cs) and compares byte-for-byte
// with the generated gold fixture (TestFixtures/XmlWriteGold.hpp): the
// XDocument/XElement/XAttribute ToString renders, the SaveOptions
// annotation walk, the leaf node renders, the ElementWriter namespace
// shapes, and the Save(fileName) file bytes.

#include "TestFixtures/XmlWriteGold.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "Decompiler/Xml/XmlConvert.hpp"
#include "Decompiler/Xml/XAttribute.hpp"
#include "Decompiler/Xml/XCData.hpp"
#include "Decompiler/Xml/XComment.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XDocumentType.hpp"
#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XProcessingInstruction.hpp"
#include "Decompiler/Xml/XText.hpp"

namespace {

using ILSpy::Decompiler::Xml::XAttribute;
using ILSpy::Decompiler::Xml::XCData;
using ILSpy::Decompiler::Xml::XComment;
using ILSpy::Decompiler::Xml::XDeclaration;
using ILSpy::Decompiler::Xml::XDocument;
using ILSpy::Decompiler::Xml::XDocumentType;
using ILSpy::Decompiler::Xml::XElement;
using ILSpy::Decompiler::Xml::XName;
using ILSpy::Decompiler::Xml::XNamespace;
using ILSpy::Decompiler::Xml::XNode;
using ILSpy::Decompiler::Xml::XProcessingInstruction;
using ILSpy::Decompiler::Xml::XText;
using ILSpy::Decompiler::Xml::XmlException;
using ILSpy::Decompiler::Xml::XmlWriter;
using ILSpy::Decompiler::Xml::XmlWriterSink;

std::string PortExceptionLine(const std::exception& ex)
{
    std::string type = "InvalidOperationException";
    if (dynamic_cast<const XmlException*>(&ex) != nullptr)
        type = "XmlException";
    else if (dynamic_cast<const std::invalid_argument*>(&ex) != nullptr)
        type = "ArgumentException";
    std::string message = ex.what();
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

std::string Gold(const char* id)
{
    std::string_view text = ILSpy::Tests::XmlWriteGold::Find(id);
    return std::string(text);
}

void ExpectGold(const char* id, const std::string& actual)
{
    EXPECT_EQ(Gold(id), actual) << "case " << id;
}

template <typename T>
std::string RunCase(T producer)
{
    try {
        return producer();
    } catch (const std::exception& ex) {
        return PortExceptionLine(ex);
    }
}

// The `new XElement(name, params)` helper over the port's XContent shape.
std::shared_ptr<XElement> MakeElem(XName name)
{
    return std::make_shared<XElement>(std::move(name));
}

std::shared_ptr<XElement> MakeElem(XName name, ILSpy::Decompiler::Xml::XContent content)
{
    return std::make_shared<XElement>(std::move(name), std::move(content));
}

std::shared_ptr<XAttribute> MakeAttr(XName name, std::string value)
{
    return std::make_shared<XAttribute>(std::move(name), std::move(value));
}

// The Save(TextWriter) shape: GetXmlWriterSettings + WriteTo over the Text
// sink (the C# builds the writer from the same settings).
std::string SaveToTextUtf8(const XNode& node, ILSpy::Decompiler::Xml::SaveOptions options)
{
    XmlWriter writer(XNode::GetXmlWriterSettings(options), XmlWriterSink::Text);
    node.WriteTo(writer);
    writer.Close();
    return writer.OutputUtf8();
}

} // namespace

namespace ILSpy::Tests {

// XDocument.ToString over the basic node kinds (indented, no declaration).
TEST(XmlSerializationTest, DocumentBasicToString)
{
    std::string actual = RunCase([] {
        XDocument doc(MakeElem(XName::Get("root"),
            {MakeAttr(XName::Get("a"), "1"),
                MakeElem(XName::Get("child"), {"text"}),
                std::make_shared<XComment>("c"),
                std::make_shared<XProcessingInstruction>("pi", "d"),
                std::make_shared<XCData>("cd")}));
        return doc.ToString();
    });
    ExpectGold("n_doc_basic", actual);
}

TEST(XmlSerializationTest, DocumentDisableFormatting)
{
    std::string actual = RunCase([] {
        XDocument doc(MakeElem(XName::Get("root"),
            {MakeElem(XName::Get("child"), {"text"})}));
        return doc.ToString(ILSpy::Decompiler::Xml::SaveOptions::DisableFormatting);
    });
    ExpectGold("n_doc_disableformatting", actual);
}

// The SaveOptions annotation on the root flattens the render.
TEST(XmlSerializationTest, SaveOptionsAnnotation)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> root = MakeElem(XName::Get("root"),
            {MakeElem(XName::Get("child"))});
        root->AddAnnotation(ILSpy::Decompiler::Xml::SaveOptions::DisableFormatting);
        return root->ToString();
    });
    ExpectGold("n_doc_annotation", actual);
}

// The annotation walk goes UP only: a child's annotation does not flatten
// the parent's render.
TEST(XmlSerializationTest, ChildAnnotationDoesNotFlattenParent)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> root = MakeElem(XName::Get("root"),
            {MakeElem(XName::Get("child"))});
        root->Element(XName::Get("child"))
            ->AddAnnotation(ILSpy::Decompiler::Xml::SaveOptions::DisableFormatting);
        return root->ToString();
    });
    ExpectGold("n_doc_child_annotation", actual);
}

TEST(XmlSerializationTest, ElementToString)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("root"),
            {MakeAttr(XName::Get("x"), "1"),
                MakeElem(XName::Get("child"), {"t"})});
        return e->ToString();
    });
    ExpectGold("n_element_tostring", actual);
}

// Elements in namespaces without declarations: the writer rebinds the
// default namespace and generates prefixes for the attributes.
TEST(XmlSerializationTest, ElementNamespacesNoDeclarations)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("{urn:a}root"),
            {MakeAttr(XName::Get("{urn:b}attr"), "v"),
                MakeElem(XName::Get("{urn:a}child"))});
        return e->ToString();
    });
    ExpectGold("n_element_ns_nodecl", actual);
}

// The xmlns declarations passed through the ElementWriter's resolver.
TEST(XmlSerializationTest, ElementNamespacesWithDeclarations)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("{urn:a}root"),
            {MakeAttr(XNamespace::Xmlns() + "p", "urn:p"),
                MakeAttr(XName::Get("{urn:p}attr"), "v"),
                MakeElem(XName::Get("{urn:a}child"))});
        return e->ToString();
    });
    ExpectGold("n_element_ns_decl", actual);
}

// The default namespace declaration + an element outside it.
TEST(XmlSerializationTest, ElementDefaultNamespace)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("{urn:a}root"),
            {MakeAttr(XName::Get("xmlns"), "urn:a"),
                MakeElem(XName::Get("{urn:a}child"), {"t"}),
                MakeElem(XName::Get("plain"))});
        return e->ToString();
    });
    ExpectGold("n_element_default_ns", actual);
}

// Two same-prefix xmlns declarations on one element: the XLinq duplicate
// attribute check fires at construction.
TEST(XmlSerializationTest, DuplicateXmlnsThrowsAtConstruction)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("root"),
            {MakeAttr(XNamespace::Xmlns() + "p", "urn:p"),
                MakeAttr(XNamespace::Xmlns() + "p", "urn:q")});
        return e->ToString();
    });
    ExpectGold("n_element_xmlns_dup", actual);
}

// The reserved xml:space/xml:lang attributes render through the special
// attribute path.
TEST(XmlSerializationTest, ReservedXmlAttributes)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("root"),
            {MakeAttr(XNamespace::Xml() + "space", "preserve"),
                MakeAttr(XNamespace::Xml() + "lang", "en")});
        return e->ToString();
    });
    ExpectGold("n_element_reserved_xml", actual);
}

TEST(XmlSerializationTest, AttributeToStringPlain)
{
    std::string actual = RunCase([] {
        XAttribute a(XName::Get("name"), "value");
        return a.ToString();
    });
    ExpectGold("n_attr_tostring_plain", actual);
}

TEST(XmlSerializationTest, AttributeToStringNamespace)
{
    std::string actual = RunCase([] {
        XAttribute a(XName::Get("{urn:a}name"), "value");
        return a.ToString();
    });
    ExpectGold("n_attr_tostring_ns", actual);
}

// A parented attribute resolves its prefix through the element's scope.
TEST(XmlSerializationTest, AttributeToStringParented)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("{urn:a}root"),
            {MakeAttr(XNamespace::Xmlns() + "p", "urn:p"),
                MakeAttr(XName::Get("{urn:p}name"), "value")});
        return e->LastAttribute()->ToString();
    });
    ExpectGold("n_attr_tostring_parented", actual);
}

TEST(XmlSerializationTest, AttributeToStringXmlPrefix)
{
    std::string actual = RunCase([] {
        XAttribute a(XNamespace::Xml() + "space", "preserve");
        return a.ToString();
    });
    ExpectGold("n_attr_tostring_xml_prefix", actual);
}

TEST(XmlSerializationTest, AttributeToStringEscaping)
{
    std::string actual = RunCase([] {
        XAttribute a(XName::Get("a"), "x\ny\tz\"q");
        return a.ToString();
    });
    ExpectGold("n_attr_tostring_escape", actual);
}

// The leaf node renders (the fragment conformance for a text node).
TEST(XmlSerializationTest, TextToString)
{
    std::string actual = RunCase([] {
        XText t("hello & goodbye");
        return t.ToString();
    });
    ExpectGold("n_text_tostring", actual);
}

// A document-level text node serializes as whitespace.
TEST(XmlSerializationTest, DocumentLevelTextIsWhitespace)
{
    std::string actual = RunCase([] {
        XDocument doc({std::make_shared<XText>("  "), MakeElem(XName::Get("root"))});
        return doc.ToString();
    });
    ExpectGold("n_text_doclevel_whitespace", actual);
}

TEST(XmlSerializationTest, CommentToString)
{
    std::string actual = RunCase([] {
        XComment c("a -- b");
        return c.ToString();
    });
    ExpectGold("n_comment_tostring", actual);
}

TEST(XmlSerializationTest, ProcessingInstructionToString)
{
    std::string actual = RunCase([] {
        XProcessingInstruction pi("pi", "data");
        return pi.ToString();
    });
    ExpectGold("n_pi_tostring", actual);
}

TEST(XmlSerializationTest, CDataToString)
{
    std::string actual = RunCase([] {
        XCData c("x]]>y");
        return c.ToString();
    });
    ExpectGold("n_cdata_tostring", actual);
}

TEST(XmlSerializationTest, DocumentTypeToString)
{
    std::string actual = RunCase([] {
        XDocument doc({std::make_shared<XDocumentType>("root", "-//pub//en", "sys.dtd", "<!ELEMENT root EMPTY>"),
            MakeElem(XName::Get("root"))});
        return doc.ToString();
    });
    ExpectGold("n_doctype_node_tostring", actual);
}

TEST(XmlSerializationTest, EmptyDocumentToString)
{
    std::string actual = RunCase([] {
        XDocument doc;
        return doc.ToString();
    });
    ExpectGold("n_doc_empty_tostring", actual);
}

// Save(TextWriter): the declaration with the TextWriter's utf-16 encoding.
TEST(XmlSerializationTest, DocumentSaveToTextWriter)
{
    std::string actual = RunCase([] {
        XDocument doc(MakeElem(XName::Get("root"), {"t"}));
        return SaveToTextUtf8(doc, ILSpy::Decompiler::Xml::SaveOptions::None);
    });
    ExpectGold("n_doc_save_textwriter", actual);
}

TEST(XmlSerializationTest, DocumentWithStandaloneDeclarationSaveToTextWriter)
{
    std::string actual = RunCase([] {
        XDocument doc(MakeElem(XName::Get("root")));
        doc.Declaration(XDeclaration("1.0", "utf-8", "yes"));
        return SaveToTextUtf8(doc, ILSpy::Decompiler::Xml::SaveOptions::None);
    });
    ExpectGold("n_doc_standalone_save_textwriter", actual);
}

TEST(XmlSerializationTest, ElementSaveToTextWriter)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("root"),
            {MakeElem(XName::Get("child"), {"t"})});
        return SaveToTextUtf8(*e, ILSpy::Decompiler::Xml::SaveOptions::None);
    });
    ExpectGold("n_element_save_textwriter", actual);
}

// The deep namespace/mixed-content shape.
TEST(XmlSerializationTest, XDocumentDeep)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> root = MakeElem(XName::Get("{urn:deep}root"),
            {MakeAttr(XNamespace::Xmlns() + "d", "urn:deep"),
                MakeAttr(XName::Get("{urn:other}a"), "1"),
                MakeElem(XName::Get("{urn:deep}child"),
                    {MakeElem(XName::Get("{urn:deep}leaf"), {"text"}),
                        std::make_shared<XText>("mixed"),
                        MakeElem(XName::Get("bare"))}),
                MakeElem(XName::Get("second"), {MakeAttr(XName::Get("b"), "2")})});
        XDocument doc({root});
        return doc.ToString();
    });
    ExpectGold("n_xdocument_deep", actual);
}

// An element in NO namespace carrying a default xmlns declaration: the
// writer's implicit empty-ns binding conflicts in the same start tag.
TEST(XmlSerializationTest, NamespacelessElementWithDefaultDeclThrows)
{
    std::string actual = RunCase([] {
        std::shared_ptr<XElement> e = MakeElem(XName::Get("root"),
            {MakeAttr(XName::Get("xmlns"), "urn:a")});
        return e->ToString();
    });
    ExpectGold("n_element_nsless_default_decl", actual);
}

// XDocument.Save(fileName): the UTF-8 preamble + the declaration + the
// indented content, byte-for-byte with the real tool's file.
TEST(XmlSerializationTest, DocumentSaveFileBytes)
{
    XDocument doc(MakeElem(XName::Get("root"),
        {MakeAttr(XName::Get("a"), "1"),
            MakeElem(XName::Get("child"), {"t"})}));
    std::string path = "xmlwrite_test_n_doc_save_file.tmp";
    doc.Save(path);
    std::vector<char> bytes;
    {
        std::ifstream file(path, std::ios::binary);
        bytes.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }
    std::remove(path.c_str());
    std::size_t count = 0;
    const std::uint8_t* gold = ILSpy::Tests::XmlWriteGold::FindBytes("n_doc_save_file", count);
    ASSERT_NE(gold, nullptr);
    ASSERT_EQ(count, bytes.size());
    for (std::size_t i = 0; i < count; ++i)
        EXPECT_EQ(gold[i], static_cast<std::uint8_t>(bytes[i])) << "byte " << i;
}

TEST(XmlSerializationTest, DocumentSaveFileStandaloneBytes)
{
    XDocument doc(XDeclaration("1.0", "utf-8", "yes"), {MakeElem(XName::Get("root"))});
    std::string path = "xmlwrite_test_n_doc_save_file_standalone.tmp";
    doc.Save(path);
    std::vector<char> bytes;
    {
        std::ifstream file(path, std::ios::binary);
        bytes.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    }
    std::remove(path.c_str());
    std::size_t count = 0;
    const std::uint8_t* gold = ILSpy::Tests::XmlWriteGold::FindBytes("n_doc_save_file_standalone", count);
    ASSERT_NE(gold, nullptr);
    ASSERT_EQ(count, bytes.size());
    for (std::size_t i = 0; i < count; ++i)
        EXPECT_EQ(gold[i], static_cast<std::uint8_t>(bytes[i])) << "byte " << i;
}

} // namespace ILSpy::Tests
