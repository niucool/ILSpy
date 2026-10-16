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

// The XML text-parser suite (XElement.Parse/XDocument.Parse over the
// port-authored XmlReader stand-in, cpp/Decompiler/Xml/XmlTextParser):
// the ENTIRE case matrix is GENERATED from the gold reference dumped from
// the real .NET 10 XElement.Parse/XDocument.Parse through the
// C:/temp-probe/XmlParseProbe probe (LoadOptions.None -- the XLinq
// settings IgnoreWhitespace=true / DtdProcessing.Parse /
// ConformanceLevel.Document). Every tree, render and exception message
// (the exact text including the " Line N, position P." suffix) is pinned
// byte-for-byte against the real engine. The generator
// (C:/temp-probe/XmlParseProbe/gen_tests.py) cross-checks the probe case
// list against the gold before emitting; regenerate the pair together.
// The one dump-level difference: the C# nullable
// XDeclaration.Encoding/Standalone and the XDocumentType ids map to the
// empty string in the port (the null=="" equivalence -- see the
// XmlTextParser.hpp header), so the expected dumps show '' where the
// probe printed <null>.

#include <gtest/gtest.h>

#include <cstdio>
#include <stdexcept>
#include <string>

#include "Decompiler/Xml/XCData.hpp"
#include "Decompiler/Xml/XComment.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XDocumentType.hpp"
#include "Decompiler/Xml/XElement.hpp"
#include "Decompiler/Xml/XProcessingInstruction.hpp"
#include "Decompiler/Xml/XText.hpp"
#include "Decompiler/Xml/XmlTextParser.hpp"

namespace {

// The probe Esc(): the value-dump escaping (control and DEL bytes escaped,
// multi-byte UTF-8 passes raw).
std::string Esc(const std::string& s)
{
    std::string out;
    for (unsigned char b : s) {
        if (b == 0x0A) {
            out += "\\n";
        } else if (b == 0x0D) {
            out += "\\r";
        } else if (b == 0x09) {
            out += "\\t";
        } else if (b < 0x20 || b == 0x7F) {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\u%04X", static_cast<unsigned char>(b));
            out += buffer;
        } else {
            out += static_cast<char>(b);
        }
    }
    return out;
}

std::string DumpNode(ILSpy::Decompiler::Xml::XNode& node, int depth);

std::string DumpElement(ILSpy::Decompiler::Xml::XElement& e, int depth)
{
    using namespace ILSpy::Decompiler::Xml;
    std::string pad(depth * 2, ' ');
    std::string out = pad + "E ns='" + e.Name().Namespace().NamespaceName() + "' local='" + e.Name().LocalName() + "'\n";
    for (const auto& a : e.Attributes()) {
        out += pad + "  A ns='" + a->Name().Namespace().NamespaceName() + "' local='" + a->Name().LocalName()
            + "' = '" + Esc(a->Value()) + "'\n";
    }
    for (const auto& child : e.Nodes()) {
        out += DumpNode(*child, depth + 1);
    }
    return out;
}

std::string DumpNode(ILSpy::Decompiler::Xml::XNode& node, int depth)
{
    using namespace ILSpy::Decompiler::Xml;
    std::string pad(depth * 2, ' ');
    if (auto* e = dynamic_cast<XElement*>(&node)) {
        return DumpElement(*e, depth);
    }
    if (auto* cd = dynamic_cast<XCData*>(&node)) {
        return pad + "CD '" + Esc(cd->Value()) + "'\n";
    }
    if (auto* t = dynamic_cast<XText*>(&node)) {
        return pad + "T '" + Esc(t->Value()) + "'\n";
    }
    if (auto* c = dynamic_cast<XComment*>(&node)) {
        return pad + "C '" + Esc(c->Value()) + "'\n";
    }
    if (auto* pi = dynamic_cast<XProcessingInstruction*>(&node)) {
        return pad + "PI " + pi->Target() + " '" + Esc(pi->Data()) + "'\n";
    }
    if (auto* dt = dynamic_cast<XDocumentType*>(&node)) {
        std::string pub = dt->PublicId().has_value() ? "'" + *dt->PublicId() + "'" : "''";
        std::string sys = dt->SystemId().has_value() ? "'" + *dt->SystemId() + "'" : "''";
        std::string internalSubset = dt->InternalSubset().has_value() ? "'" + Esc(*dt->InternalSubset()) + "'" : "''";
        return pad + "DT " + dt->Name() + " pub=" + pub + " sys=" + sys + " internal=" + internalSubset + "\n";
    }
    return pad + "? unknown\n";
}

struct Case {
    const char* input;
    bool document;
    bool ok;
    const char* dump; // the expected tree dump + RENDER (null when !ok)
    const char* message; // the expected exception message (null when ok)
};

// The generated case table (%d cases: %d trees, %d exception arms).
const Case kCases[] = {
    // A01
    {"<a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // A02
    {"<a></a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // A03
    {"<a>b</a>", false, true, R"~(E ns='' local='a'
  T 'b'
RENDER: <a>b</a>
)~", nullptr},
    // A04
    {"<a><b/></a>", false, true, R"~(E ns='' local='a'
  E ns='' local='b'
RENDER: <a><b /></a>
)~", nullptr},
    // A05
    {"<a><b/><c/></a>", false, true, R"~(E ns='' local='a'
  E ns='' local='b'
  E ns='' local='c'
RENDER: <a><b /><c /></a>
)~", nullptr},
    // A06
    {"<a><b>x</b><c>y</c></a>", false, true, R"~(E ns='' local='a'
  E ns='' local='b'
    T 'x'
  E ns='' local='c'
    T 'y'
RENDER: <a><b>x</b><c>y</c></a>
)~", nullptr},
    // B01
    {"<a b='1'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '1'
RENDER: <a b="1" />
)~", nullptr},
    // B02
    {"<a b=\"1\"/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '1'
RENDER: <a b="1" />
)~", nullptr},
    // B03
    {"<a b='1' c='2'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '1'
  A ns='' local='c' = '2'
RENDER: <a b="1" c="2" />
)~", nullptr},
    // B04
    {"<a b='x\ny'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x y'
RENDER: <a b="x y" />
)~", nullptr},
    // B05
    {"<a b='x\ty'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x y'
RENDER: <a b="x y" />
)~", nullptr},
    // B06
    {"<a b='x\r\ny'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x y'
RENDER: <a b="x y" />
)~", nullptr},
    // B07
    {"<a b='x&#10;y'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x\ny'
RENDER: <a b="x&#xA;y" />
)~", nullptr},
    // B08
    {"<a b='&#9;'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '\t'
RENDER: <a b="&#x9;" />
)~", nullptr},
    // B09
    {"<a b='&#65;&#x42;'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'AB'
RENDER: <a b="AB" />
)~", nullptr},
    // B10
    {"<a b='&lt;&gt;&amp;&apos;&quot;'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '<>&'"'
RENDER: <a b="&lt;&gt;&amp;'&quot;" />
)~", nullptr},
    // B11
    {"<a b=''/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = ''
RENDER: <a b="" />
)~", nullptr},
    // B12
    {"<a b='1' b='2'/>", false, false, nullptr, "'b' is a duplicate attribute name. Line 1, position 10."},
    // B13
    {"<a b=1/>", false, false, nullptr, "'1' is an unexpected token. The expected token is '\"' or '''. Line 1, position 6."},
    // B14
    {"<a b='1/>", false, false, nullptr, "There is an unclosed literal string. Line 1, position 10."},
    // B15
    {"<a 1b='x'/>", false, false, nullptr, "Name cannot begin with the '1' character, hexadecimal value 0x31. Line 1, position 4."},
    // B16
    {"<a b:c='x' xmlns:b='urn:x'/>", false, true, R"~(E ns='' local='a'
  A ns='urn:x' local='c' = 'x'
  A ns='http://www.w3.org/2000/xmlns/' local='b' = 'urn:x'
RENDER: <a b:c="x" xmlns:b="urn:x" />
)~", nullptr},
    // B17
    {"<a b:c='x'/>", false, false, nullptr, "'b' is an undeclared prefix. Line 1, position 4."},
    // B18
    {"<a xmlns='urn:x'/>", false, true, R"~(E ns='urn:x' local='a'
  A ns='' local='xmlns' = 'urn:x'
RENDER: <a xmlns="urn:x" />
)~", nullptr},
    // B19
    {"<a xmlns:b='urn:x'/>", false, true, R"~(E ns='' local='a'
  A ns='http://www.w3.org/2000/xmlns/' local='b' = 'urn:x'
RENDER: <a xmlns:b="urn:x" />
)~", nullptr},
    // B20
    {"<a xmlns:xml='http://www.w3.org/XML/1998/namespace'/>", false, true, R"~(E ns='' local='a'
  A ns='http://www.w3.org/2000/xmlns/' local='xml' = 'http://www.w3.org/XML/1998/namespace'
RENDER: <a xmlns:xml="http://www.w3.org/XML/1998/namespace" />
)~", nullptr},
    // B21
    {"<a xmlns:xml='urn:wrong'/>", false, false, nullptr, "Prefix \"xml\" is reserved for use by XML and can be mapped only to namespace name \"http://www.w3.org/XML/1998/namespace\". Line 1, position 4."},
    // B22
    {"<a b='1'/>x", false, false, nullptr, "Data at the root level is invalid. Line 1, position 11."},
    // B23
    {"<a b='1'></b>", false, false, nullptr, "The 'a' start tag on line 1 position 2 does not match the end tag of 'b'. Line 1, position 12."},
    // B24
    {"<a b = '1' />", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '1'
RENDER: <a b="1" />
)~", nullptr},
    // C01
    {"<a> </a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // C02
    {"<a>\n  <b/>\n</a>", false, true, R"~(E ns='' local='a'
  E ns='' local='b'
RENDER: <a><b /></a>
)~", nullptr},
    // C03
    {"<a>x <b/> y</a>", false, true, R"~(E ns='' local='a'
  T 'x '
  E ns='' local='b'
  T ' y'
RENDER: <a>x <b /> y</a>
)~", nullptr},
    // C04
    {"<a>x<b/></a>", false, true, R"~(E ns='' local='a'
  T 'x'
  E ns='' local='b'
RENDER: <a>x<b /></a>
)~", nullptr},
    // C05
    {"<a>  x  </a>", false, true, R"~(E ns='' local='a'
  T '  x  '
RENDER: <a>  x  </a>
)~", nullptr},
    // C06
    {"<a>1\r\n2</a>", false, true, R"~(E ns='' local='a'
  T '1\n2'
RENDER: <a>1\r\n2</a>
)~", nullptr},
    // C07
    {"<a>1\r2</a>", false, true, R"~(E ns='' local='a'
  T '1\n2'
RENDER: <a>1\r\n2</a>
)~", nullptr},
    // C08
    {"<a>\t</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // C09
    {"<a>x <b/> </a>", false, true, R"~(E ns='' local='a'
  T 'x '
  E ns='' local='b'
RENDER: <a>x <b /></a>
)~", nullptr},
    // C10
    {"<a>\n</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // C11
    {"<a> </a><b/>", false, false, nullptr, "There are multiple root elements. Line 1, position 10."},
    // C12
    {"  <a/>  ", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // D01
    {"<a>&lt;&gt;&amp;&quot;&apos;</a>", false, true, R"~(E ns='' local='a'
  T '<>&"''
RENDER: <a>&lt;&gt;&amp;"'</a>
)~", nullptr},
    // D02
    {"<a>&#65;&#x42;</a>", false, true, R"~(E ns='' local='a'
  T 'AB'
RENDER: <a>AB</a>
)~", nullptr},
    // D03
    {"<a>&#x1F600;</a>", false, true, R"~(E ns='' local='a'
  T '😀'
RENDER: <a>😀</a>
)~", nullptr},
    // D04
    {"<a>&#0;</a>", false, false, nullptr, "'.', hexadecimal value 0x00, is an invalid character. Line 1, position 6."},
    // D05
    {"<a>&#x110000;</a>", false, false, nullptr, "'\355\260\200', hexadecimal value 0xDC00, is an invalid character. Line 1, position 7."},
    // D06
    {"<a>&foo;</a>", false, false, nullptr, "Reference to undeclared entity 'foo'. Line 1, position 5."},
    // D07
    {"<a>a & b</a>", false, false, nullptr, "An error occurred while parsing EntityName. Line 1, position 7."},
    // D08
    {"<a>&amp</a>", false, false, nullptr, "'<' is an unexpected token. The expected token is ';'. Line 1, position 8."},
    // D09
    {"<a>&#;</a>", false, false, nullptr, "Invalid syntax for a decimal numeric entity reference. Line 1, position 6."},
    // D10
    {"<a>&#x;</a>", false, false, nullptr, "Invalid syntax for a hexadecimal numeric entity reference. Line 1, position 7."},
    // E01
    {"<a><![CDATA[x]]></a>", false, true, R"~(E ns='' local='a'
  CD 'x'
RENDER: <a><![CDATA[x]]></a>
)~", nullptr},
    // E02
    {"<a><![CDATA[]]></a>", false, true, R"~(E ns='' local='a'
  CD ''
RENDER: <a><![CDATA[]]></a>
)~", nullptr},
    // E03
    {"<a>t<![CDATA[x]]>t2</a>", false, true, R"~(E ns='' local='a'
  T 't'
  CD 'x'
  T 't2'
RENDER: <a>t<![CDATA[x]]>t2</a>
)~", nullptr},
    // E04
    {"<a><![CDATA[1\r\n2]]></a>", false, true, R"~(E ns='' local='a'
  CD '1\n2'
RENDER: <a><![CDATA[1\r\n2]]></a>
)~", nullptr},
    // E05
    {"<a><![CDATA[x</a>", false, false, nullptr, "Unexpected end of file while parsing CDATA has occurred. Line 1, position 18."},
    // E06
    {"<a><![CDATA[]]><b/></a>", false, true, R"~(E ns='' local='a'
  CD ''
  E ns='' local='b'
RENDER: <a><![CDATA[]]><b /></a>
)~", nullptr},
    // E07
    {"<a><![CDATA[a]]]></a>", false, true, R"~(E ns='' local='a'
  CD 'a]'
RENDER: <a><![CDATA[a]]]></a>
)~", nullptr},
    // F01
    {"<a><!--c--><b/></a>", false, true, R"~(E ns='' local='a'
  C 'c'
  E ns='' local='b'
RENDER: <a><!--c--><b /></a>
)~", nullptr},
    // F02
    {"<a><!--c--></a>", false, true, R"~(E ns='' local='a'
  C 'c'
RENDER: <a><!--c--></a>
)~", nullptr},
    // F03
    {"<!--c--><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // F04
    {"<!---->", false, false, nullptr, "Root element is missing."},
    // F05
    {"<a><!--c\r\n2--></a>", false, true, R"~(E ns='' local='a'
  C 'c\n2'
RENDER: <a><!--c\r\n2--></a>
)~", nullptr},
    // F06
    {"<a><!--c</a>", false, false, nullptr, "Unexpected end of file while parsing Comment has occurred. Line 1, position 13."},
    // F07
    {"<a><!-- -- --></a>", false, false, nullptr, "An XML comment cannot contain '--', and '-' cannot be the last character. Line 1, position 9."},
    // F08
    {"<a><!---></a>", false, false, nullptr, "Unexpected end of file while parsing Comment has occurred. Line 1, position 14."},
    // G01
    {"<a><?pi data?><b/></a>", false, true, R"~(E ns='' local='a'
  PI pi 'data'
  E ns='' local='b'
RENDER: <a><?pi data?><b /></a>
)~", nullptr},
    // G02
    {"<?pi data?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // G03
    {"<?pi?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // G04
    {"<a><?pi data?>x</a>", false, true, R"~(E ns='' local='a'
  PI pi 'data'
  T 'x'
RENDER: <a><?pi data?>x</a>
)~", nullptr},
    // G05
    {"<a><?pi data<a/>", false, false, nullptr, "Unexpected end of file while parsing PI has occurred. Line 1, position 17."},
    // G06
    {"<?1p?><a/>", false, false, nullptr, "Name cannot begin with the '1' character, hexadecimal value 0x31. Line 1, position 3."},
    // G07
    {"<?xml?><a/>", false, false, nullptr, "Syntax for an XML declaration is invalid. Line 1, position 6."},
    // G08
    {"<a><?xml version='1.0'?></a>", false, false, nullptr, "Unexpected XML declaration. The XML declaration must be the first node in the document, and no whitespace characters are allowed to appear before it. Line 1, position 6."},
    // H01
    {"<?xml version='1.0'?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // H02
    {"<?xml version='1.0' encoding='utf-8'?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // H03
    {"<?xml version='1.0' standalone='yes'?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // H04
    {"<?xml version=\"1.0\"?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // H05
    {"<?xml version=1.0?><a/>", false, false, nullptr, "'1.0' is an unexpected token. The expected token is '\"' or '''. Line 1, position 15."},
    // H06
    {"<?xml version='1'?><a/>", false, false, nullptr, "Version number '1' is invalid. Line 1, position 16."},
    // H07
    {"<?xml ?><a/>", false, false, nullptr, "Syntax for an XML declaration is invalid. Line 1, position 7."},
    // H08
    {"<?xml version='1.0' ?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // H09
    {"<a/><?xml version='1.0'?>", false, false, nullptr, "Unexpected XML declaration. The XML declaration must be the first node in the document, and no whitespace characters are allowed to appear before it. Line 1, position 7."},
    // H10
    {"<?xml version='1.0'?>x<a/>", false, false, nullptr, "Data at the root level is invalid. Line 1, position 22."},
    // I01
    {"<!DOCTYPE a><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // I02
    {"<!DOCTYPE a [<!ENTITY e 'x'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T 'x'
RENDER: <a>x</a>
)~", nullptr},
    // I03
    {"<!DOCTYPE a SYSTEM 'x'><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // J01
    {"<a/><b/>", false, false, nullptr, "There are multiple root elements. Line 1, position 6."},
    // J02
    {"<a/>x", false, false, nullptr, "Data at the root level is invalid. Line 1, position 5."},
    // J03
    {"x<a/>", false, false, nullptr, "Data at the root level is invalid. Line 1, position 1."},
    // J04
    {"", false, false, nullptr, "Root element is missing."},
    // J05
    {"   ", false, false, nullptr, "Root element is missing."},
    // J06
    {"</a>", false, false, nullptr, "Unexpected end tag. Line 1, position 3."},
    // J07
    {"<a></a></a>", false, false, nullptr, "Unexpected end tag. Line 1, position 10."},
    // K01
    {"<a/>", true, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // K02
    {"<?xml version='1.0'?><a/>", true, true, R"~(DECL v='1.0' e='' s='' 
E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // K03
    {"<?xml version='1.0'?>\n<a/>", true, true, R"~(DECL v='1.0' e='' s='' 
E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // K04
    {"<a/>\n", true, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // K05
    {"<!--c--><a/><!--c2-->", true, true, R"~(C 'c'
E ns='' local='a'
C 'c2'
RENDER: <!--c--><a /><!--c2-->
)~", nullptr},
    // K06
    {"<a/><b/>", true, false, nullptr, "There are multiple root elements. Line 1, position 6."},
    // K07
    {"<!DOCTYPE a><a/>", true, true, R"~(DT a pub='' sys='' internal=''
E ns='' local='a'
RENDER: <!DOCTYPE a []><a />
)~", nullptr},
    // K08
    {"<!DOCTYPE a [<!ENTITY e 'x'>]><a>&e;</a>", true, true, R"~(DT a pub='' sys='' internal='<!ENTITY e 'x'>'
E ns='' local='a'
  T 'x'
RENDER: <!DOCTYPE a [<!ENTITY e 'x'>]><a>x</a>
)~", nullptr},
    // K09
    {"x<a/>", true, false, nullptr, "Data at the root level is invalid. Line 1, position 1."},
    // K10
    {"", true, false, nullptr, "Root element is missing."},
    // K11
    {"<a>b</a>", true, true, R"~(E ns='' local='a'
  T 'b'
RENDER: <a>b</a>
)~", nullptr},
    // K12
    {"<!DOCTYPE a PUBLIC 'pub' 'sys'><a/>", true, true, R"~(DT a pub='pub' sys='sys' internal=''
E ns='' local='a'
RENDER: <!DOCTYPE a PUBLIC "pub" "sys"[]><a />
)~", nullptr},
    // K13
    {"<a/><!DOCTYPE b>", true, false, nullptr, "DTD must be defined before the document root element. Line 1, position 5."},
    // L01
    {"<a><b></a>", false, false, nullptr, "The 'b' start tag on line 1 position 5 does not match the end tag of 'a'. Line 1, position 9."},
    // L02
    {"<a></b>", false, false, nullptr, "The 'a' start tag on line 1 position 2 does not match the end tag of 'b'. Line 1, position 6."},
    // L03
    {"<a>", false, false, nullptr, "Data at the root level is invalid. Line 1, position 1."},
    // L04
    {"<a", false, false, nullptr, "Data at the root level is invalid. Line 1, position 1."},
    // L05
    {"<a b='1'", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed:  Line 1, position 9."},
    // L06
    {"< a/>", false, false, nullptr, "Name cannot begin with the ' ' character, hexadecimal value 0x20. Line 1, position 2."},
    // L07
    {"<a/ >", false, false, nullptr, "'/' is an unexpected token. The expected token is '>'. Line 1, position 3."},
    // L08
    {"<a>x<y</a>", false, false, nullptr, "The '<' character, hexadecimal value 0x3C, cannot be included in a name. Line 1, position 7."},
    // L09
    {"<a.b/>", false, true, R"~(E ns='' local='a.b'
RENDER: <a.b />
)~", nullptr},
    // L10
    {"<a:b:c xmlns:a='urn:x' xmlns:b='urn:y'/>", false, false, nullptr, "The ':' character, hexadecimal value 0x3A, cannot be included in a name. Line 1, position 5."},
    // L11
    {"<:a/>", false, false, nullptr, "Name cannot begin with the ':' character, hexadecimal value 0x3A. Line 1, position 2."},
    // L12
    {"<a:b/>", false, false, nullptr, "'a' is an undeclared prefix. Line 1, position 2."},
    // L13
    {"<a ><b/></a >", false, true, R"~(E ns='' local='a'
  E ns='' local='b'
RENDER: <a><b /></a>
)~", nullptr},
    // L14
    {"<a>&1;</a>", false, false, nullptr, "An error occurred while parsing EntityName. Line 1, position 5."},
    // M01
    {"<a xmlns='urn:x'><b/></a>", false, true, R"~(E ns='urn:x' local='a'
  A ns='' local='xmlns' = 'urn:x'
  E ns='urn:x' local='b'
RENDER: <a xmlns="urn:x"><b /></a>
)~", nullptr},
    // M02
    {"<x:a xmlns:x='urn:x'><x:b/></x:a>", false, true, R"~(E ns='urn:x' local='a'
  A ns='http://www.w3.org/2000/xmlns/' local='x' = 'urn:x'
  E ns='urn:x' local='b'
RENDER: <x:a xmlns:x="urn:x"><x:b /></x:a>
)~", nullptr},
    // M03
    {"<x:a xmlns:x='urn:x' xmlns:x='urn:y'/>", false, false, nullptr, "'xmlns:x' is a duplicate attribute name. Line 1, position 22."},
    // M04
    {"<a xmlns='urn:x' xmlns='urn:y'/>", false, false, nullptr, "'xmlns' is a duplicate attribute name. Line 1, position 18."},
    // M05
    {"<x:a xmlns:x='urn:x'><y:b xmlns:y='urn:y'/></x:a>", false, true, R"~(E ns='urn:x' local='a'
  A ns='http://www.w3.org/2000/xmlns/' local='x' = 'urn:x'
  E ns='urn:y' local='b'
    A ns='http://www.w3.org/2000/xmlns/' local='y' = 'urn:y'
RENDER: <x:a xmlns:x="urn:x"><y:b xmlns:y="urn:y" /></x:a>
)~", nullptr},
    // M06
    {"<x:a><x:b/></x:a>", false, false, nullptr, "'x' is an undeclared prefix. Line 1, position 2."},
    // M07
    {"<x:a xmlns:x='urn:x'><b xmlns='urn:y'/></x:a>", false, true, R"~(E ns='urn:x' local='a'
  A ns='http://www.w3.org/2000/xmlns/' local='x' = 'urn:x'
  E ns='urn:y' local='b'
    A ns='' local='xmlns' = 'urn:y'
RENDER: <x:a xmlns:x="urn:x"><b xmlns="urn:y" /></x:a>
)~", nullptr},
    // M08
    {"<a xmlns:xml='http://www.w3.org/XML/1998/namespace'><b/></a>", false, true, R"~(E ns='' local='a'
  A ns='http://www.w3.org/2000/xmlns/' local='xml' = 'http://www.w3.org/XML/1998/namespace'
  E ns='' local='b'
RENDER: <a xmlns:xml="http://www.w3.org/XML/1998/namespace"><b /></a>
)~", nullptr},
    // N01
    {"<\303\251/>", false, true, R"~(E ns='' local='é'
RENDER: <é />
)~", nullptr},
    // N02
    {"<a\302\267b/>", false, true, R"~(E ns='' local='a·b'
RENDER: <a·b />
)~", nullptr},
    // N03
    {"<a b:c:d='x'/>", false, false, nullptr, "The ':' character, hexadecimal value 0x3A, cannot be included in a name. Line 1, position 7."},
    // N04
    {"<a:/>", false, false, nullptr, "Name cannot begin with the '/' character, hexadecimal value 0x2F. Line 1, position 4."},
    // N05
    {"<a b=''/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = ''
RENDER: <a b="" />
)~", nullptr},
    // O01
    {"<a><b>", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: b, a. Line 1, position 7."},
    // O02
    {"<a><b>x", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: b, a. Line 1, position 8."},
    // O03
    {"<a>x", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a. Line 1, position 5."},
    // O04
    {"<a b", false, false, nullptr, "Unexpected end of file while parsing Name has occurred. Line 1, position 5."},
    // O05
    {"<a b=", false, false, nullptr, "Unexpected end of file has occurred. Line 1, position 6."},
    // O06
    {"<a></a", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a. Line 1, position 7."},
    // O07
    {"<a></b", false, false, nullptr, "Unexpected end of file while parsing Name has occurred. Line 1, position 7."},
    // O08
    {"<a><b/></a", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a. Line 1, position 11."},
    // O09
    {"<a>x</a", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a. Line 1, position 8."},
    // O10
    {"<a><b\n<c\n</a>", false, false, nullptr, "Name cannot begin with the '<' character, hexadecimal value 0x3C. Line 2, position 1."},
    // O11
    {"<a>\n<b>\n</c>\n</a>", false, false, nullptr, "The 'b' start tag on line 2 position 2 does not match the end tag of 'c'. Line 3, position 3."},
    // O12
    {"<a>\r\n<b>\r\n</c>\r\n</a>", false, false, nullptr, "The 'b' start tag on line 2 position 2 does not match the end tag of 'c'. Line 3, position 3."},
    // O13
    {"<a>\t<b>\t</c>\t</a>", false, false, nullptr, "The 'b' start tag on line 1 position 6 does not match the end tag of 'c'. Line 1, position 11."},
    // O14
    {"<a>\n\n\n<b/>\n\n</c>", false, false, nullptr, "The 'a' start tag on line 1 position 2 does not match the end tag of 'c'. Line 6, position 3."},
    // O15
    {"<a/>x", false, false, nullptr, "Data at the root level is invalid. Line 1, position 5."},
    // O16
    {"<a/><", false, false, nullptr, "Data at the root level is invalid. Line 1, position 5."},
    // O17
    {"<a/>&", false, false, nullptr, "Data at the root level is invalid. Line 1, position 5."},
    // O18
    {"<a/>&lt;", false, false, nullptr, "Data at the root level is invalid. Line 1, position 5."},
    // O19
    {"<a>&lt;", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a. Line 1, position 8."},
    // O20
    {"<a><![CDATA[x", false, false, nullptr, "Unexpected end of file while parsing CDATA has occurred. Line 1, position 13."},
    // O21
    {"<a><!--c", false, false, nullptr, "Unexpected end of file while parsing Comment has occurred. Line 1, position 8."},
    // O22
    {"<a><?pi data", false, false, nullptr, "Unexpected end of file while parsing PI has occurred. Line 1, position 13."},
    // O23
    {"<a>x\n<b/></a>x\n", false, false, nullptr, "Data at the root level is invalid. Line 2, position 9."},
    // O24
    {"\n<a/>\n\n<b/>", false, false, nullptr, "There are multiple root elements. Line 4, position 2."},
    // P01
    {"<!DOCTYPE a [<!ENTITY e 'x'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T 'x'
RENDER: <a>x</a>
)~", nullptr},
    // P02
    {"<!DOCTYPE a [<!ENTITY e 'a&#65;b'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T 'aAb'
RENDER: <a>aAb</a>
)~", nullptr},
    // P03
    {"<!DOCTYPE a [<!ENTITY e1 '1'><!ENTITY e 'a&e1;b'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T 'a1b'
RENDER: <a>a1b</a>
)~", nullptr},
    // P04
    {"<!DOCTYPE a [<!ENTITY e 'x'><!ENTITY e 'y'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T 'x'
RENDER: <a>x</a>
)~", nullptr},
    // P05
    {"<!DOCTYPE a [<!ENTITY e '<'>]><a>&e;</a>", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a. Line 1, position 27."},
    // P06
    {"<!DOCTYPE a [<!ENTITY e '&amp;'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T '&'
RENDER: <a>&amp;</a>
)~", nullptr},
    // P07
    {"<!DOCTYPE a [<!ENTITY e 'x'>]><a b='&e;'></a>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x'
RENDER: <a b="x"></a>
)~", nullptr},
    // P08
    {"<!DOCTYPE a [<!ENTITY e SYSTEM 'x'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // P09
    {"<!DOCTYPE a [<!ENTITY % p 'x'>]><a>&p;</a>", false, false, nullptr, "Reference to undeclared entity 'p'. Line 1, position 37."},
    // P10
    {"<!DOCTYPE a [<!ENTITY e 'x'>]><a>&#38;#65;</a>", false, true, R"~(E ns='' local='a'
  T '&#65;'
RENDER: <a>&amp;#65;</a>
)~", nullptr},
    // P11
    {"<!DOCTYPE a [<!ENTITY e 'x'>]><a>y&f;z</a>", false, false, nullptr, "Reference to undeclared entity 'f'. Line 1, position 36."},
    // P12
    {"<!DOCTYPE a [<!ELEMENT a (#PCDATA)>]><a>x</a>", false, true, R"~(E ns='' local='a'
  T 'x'
RENDER: <a>x</a>
)~", nullptr},
    // P13
    {"<!DOCTYPE a [<!ATTLIST a b CDATA #IMPLIED>]><a b='x'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x'
RENDER: <a b="x" />
)~", nullptr},
    // P14
    {"<!DOCTYPE a [<!NOTATION n SYSTEM 'x'>]><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // P15
    {"<!DOCTYPE a [<!ENTITY e 'x'>]>", false, false, nullptr, "Root element is missing."},
    // P16
    {"<!DOCTYPE a [<!ENTITY]><a/>", false, false, nullptr, "']' is an unexpected token. Expecting whitespace. Line 1, position 22."},
    // P17
    {"<!DOCTYPE [<!ENTITY e 'x'>]><a>&e;</a>", false, false, nullptr, "Name cannot begin with the '[' character, hexadecimal value 0x5B. Line 1, position 11."},
    // P18
    {"<!DOCTYPE a [ ]><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // P19
    {"<!DOCTYPE a [<!ENTITY e 'x>]><a/>", false, false, nullptr, "There is an unclosed literal string. Line 1, position 34."},
    // P20
    {"<!DOCTYPE a [<!--c-->]><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // P21
    {"<!DOCTYPE a [<?pi?>]><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // P22
    {"<!DOCTYPE a SYSTEM 'x' [<!ENTITY e 'x'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T 'x'
RENDER: <a>x</a>
)~", nullptr},
    // P23
    {"<!DOCTYPE a [<!ENTITY e 'x'>] >&e;<a/>", false, false, nullptr, "Data at the root level is invalid. Line 1, position 32."},
    // P24
    {"<!DOCTYPE a><!DOCTYPE b><a/>", false, false, nullptr, "Cannot have multiple DTDs. Line 1, position 13."},
    // P25
    {"<!DOCTYPE a BAD>", false, false, nullptr, "Expecting external ID, '[' or '>'. Line 1, position 13."},
    // P26
    {"<a>&e2;</a>", false, false, nullptr, "Reference to undeclared entity 'e2'. Line 1, position 5."},
    // P27
    {"<!DOCTYPE a [<!ENTITY e 'x'>]><a>&e2;</a>", true, false, nullptr, "Reference to undeclared entity 'e2'. Line 1, position 35."},
    // Q01
    {"<a xmlns=''><b/></a>", false, true, R"~(E ns='' local='a'
  A ns='' local='xmlns' = ''
  E ns='' local='b'
RENDER: <a xmlns=""><b /></a>
)~", nullptr},
    // Q02
    {"<a xmlns:b=''><b:c/></a>", false, false, nullptr, "Invalid namespace declaration. Line 1, position 12."},
    // Q03
    {"<a xmlns:='urn:x'/>", false, false, nullptr, "Name cannot begin with the '=' character, hexadecimal value 0x3D. Line 1, position 10."},
    // Q04
    {"<a xmlns='urn:x'><b xmlns=''/></a>", false, true, R"~(E ns='urn:x' local='a'
  A ns='' local='xmlns' = 'urn:x'
  E ns='' local='b'
    A ns='' local='xmlns' = ''
RENDER: <a xmlns="urn:x"><b xmlns="" /></a>
)~", nullptr},
    // Q05
    {"<xml:a/>", false, true, R"~(E ns='http://www.w3.org/XML/1998/namespace' local='a'
RENDER: <xml:a />
)~", nullptr},
    // Q06
    {"<a xml:lang='en'/>", false, true, R"~(E ns='' local='a'
  A ns='http://www.w3.org/XML/1998/namespace' local='lang' = 'en'
RENDER: <a xml:lang="en" />
)~", nullptr},
    // Q07
    {"<a xml:space='preserve'/>", false, true, R"~(E ns='' local='a'
  A ns='http://www.w3.org/XML/1998/namespace' local='space' = 'preserve'
RENDER: <a xml:space="preserve" />
)~", nullptr},
    // Q08
    {"<a xmlns:XML='urn:x'/>", false, true, R"~(E ns='' local='a'
  A ns='http://www.w3.org/2000/xmlns/' local='XML' = 'urn:x'
RENDER: <a xmlns:XML="urn:x" />
)~", nullptr},
    // Q09
    {"<a xmlns:Xml='urn:x'/>", false, true, R"~(E ns='' local='a'
  A ns='http://www.w3.org/2000/xmlns/' local='Xml' = 'urn:x'
RENDER: <a xmlns:Xml="urn:x" />
)~", nullptr},
    // Q10
    {"<a><b xmlns='urn:y'/></a>", false, true, R"~(E ns='' local='a'
  E ns='urn:y' local='b'
    A ns='' local='xmlns' = 'urn:y'
RENDER: <a><b xmlns="urn:y" /></a>
)~", nullptr},
    // Q11
    {"<a xmlns='urn:x' b:c='1' xmlns:b='urn:x'/>", false, true, R"~(E ns='urn:x' local='a'
  A ns='' local='xmlns' = 'urn:x'
  A ns='urn:x' local='c' = '1'
  A ns='http://www.w3.org/2000/xmlns/' local='b' = 'urn:x'
RENDER: <b:a xmlns="urn:x" b:c="1" xmlns:b="urn:x" />
)~", nullptr},
    // Q12
    {"<a b:d='1' xmlns:b='urn:x' b:c='2'/>", false, true, R"~(E ns='' local='a'
  A ns='urn:x' local='d' = '1'
  A ns='http://www.w3.org/2000/xmlns/' local='b' = 'urn:x'
  A ns='urn:x' local='c' = '2'
RENDER: <a b:d="1" xmlns:b="urn:x" b:c="2" />
)~", nullptr},
    // R01
    {"<a>&#xFFFF;</a>", false, false, nullptr, "'\357\277\277', hexadecimal value 0xFFFF, is an invalid character. Line 1, position 7."},
    // R02
    {"<a>&#xFFFE;</a>", false, false, nullptr, "'\357\277\276', hexadecimal value 0xFFFE, is an invalid character. Line 1, position 7."},
    // R03
    {"<a>&#xD800;</a>", false, false, nullptr, "'\355\240\200', hexadecimal value 0xD800, is an invalid character. Line 1, position 7."},
    // R04
    {"<a>\001</a>", false, false, nullptr, "'\001', hexadecimal value 0x01, is an invalid character. Line 1, position 4."},
    // R05
    {"<a b='\001'/>", false, false, nullptr, "'\001', hexadecimal value 0x01, is an invalid character. Line 1, position 7."},
    // R06
    {"<a><!--\001--></a>", false, false, nullptr, "'\001', hexadecimal value 0x01, is an invalid character. Line 1, position 8."},
    // R07
    {"<a><?pi \001?></a>", false, false, nullptr, "'\001', hexadecimal value 0x01, is an invalid character. Line 1, position 9."},
    // R08
    {"<a>\355\240\200</a>", false, false, nullptr, "'<', hexadecimal value 0x3C, is an invalid character. Line 1, position 5."},
    // R09
    {"<a>]]&gt;</a>", false, true, R"~(E ns='' local='a'
  T ']]>'
RENDER: <a>]]&gt;</a>
)~", nullptr},
    // R10
    {"<a>]]></a>", false, false, nullptr, "']]>' is not allowed in character data. Line 1, position 4."},
    // R11
    {"<a>x&#65;y&lt;z</a>", false, true, R"~(E ns='' local='a'
  T 'xAy<z'
RENDER: <a>xAy&lt;z</a>
)~", nullptr},
    // R12
    {"<a b='x&#9;y\nz'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x\ty z'
RENDER: <a b="x&#x9;y z" />
)~", nullptr},
    // R13
    {"<a>&#xD;</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // R14
    {"<a>&#xA;</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // R15
    {"<a b='&#13;&#10;'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '\r\n'
RENDER: <a b="&#xD;&#xA;" />
)~", nullptr},
    // R16
    {"<a>&#x9;</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // R17
    {"<a>&#32;</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // R18
    {"<a>&#127;</a>", false, true, R"~(E ns='' local='a'
  T '\u007F'
RENDER: <a>\u007F</a>
)~", nullptr},
    // R19
    {"<a>&#x7F;</a>", false, true, R"~(E ns='' local='a'
  T '\u007F'
RENDER: <a>\u007F</a>
)~", nullptr},
    // R20
    {"<a>&#xFFFD;</a>", false, true, R"~(E ns='' local='a'
  T '�'
RENDER: <a>�</a>
)~", nullptr},
    // S01
    {"<a b='&f;'/>", false, false, nullptr, "Reference to undeclared entity 'f'. Line 1, position 8."},
    // S02
    {"<a b='x</a>", false, false, nullptr, "'<', hexadecimal value 0x3C, is an invalid attribute character. Line 1, position 8."},
    // S03
    {"<a b='1' c='2'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '1'
  A ns='' local='c' = '2'
RENDER: <a b="1" c="2" />
)~", nullptr},
    // S04
    {"<a b='1' /", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed:  Line 1, position 11."},
    // S05
    {"<a>\355\240\200", false, false, nullptr, "Unexpected end of file has occurred. Line 1, position 4."},
    // S06
    {"<1", false, false, nullptr, "Data at the root level is invalid. Line 1, position 1."},
    // S07
    {"<:", false, false, nullptr, "Data at the root level is invalid. Line 1, position 1."},
    // S08
    {"<a\n>", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a. Line 2, position 2."},
    // S09
    {"<a b='x'\n/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x'
RENDER: <a b="x" />
)~", nullptr},
    // S10
    {"<?xml version='1.0' encoding='8'?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // S11
    {"<?xml version='1.0' standalone='maybe'?><a/>", false, false, nullptr, "Syntax for an XML declaration is invalid. Line 1, position 32."},
    // S12
    {"<?xml version='1.0' version='1.0'?><a/>", false, false, nullptr, "Syntax for an XML declaration is invalid. Line 1, position 21."},
    // S13
    {"<?xml encoding='utf-8' version='1.0'?><a/>", false, false, nullptr, "Syntax for an XML declaration is invalid. Line 1, position 7."},
    // S14
    {"<?xml version='1.0' standalone='yes' encoding='utf-8'?><a/>", false, false, nullptr, "Syntax for an XML declaration is invalid. Line 1, position 38."},
    // S15
    {"<?xml version='1.0' encoding='utf-16'?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // S16
    {"<?xml version='1.0' foo='bar'?><a/>", false, false, nullptr, "Syntax for an XML declaration is invalid. Line 1, position 21."},
    // S17
    {"<?xml version='1.0'?><!--c--><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // S18
    {"<!DOCTYPE a [<!ENTITY e 'x'>]><!--c--><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T 'x'
RENDER: <a>x</a>
)~", nullptr},
    // S19
    {"<a>x<!--c--><![CDATA[y]]>z</a>", false, true, R"~(E ns='' local='a'
  T 'x'
  C 'c'
  CD 'y'
  T 'z'
RENDER: <a>x<!--c--><![CDATA[y]]>z</a>
)~", nullptr},
    // S20
    {"<a><![CDATA[x]]><![CDATA[y]]></a>", false, true, R"~(E ns='' local='a'
  CD 'x'
  CD 'y'
RENDER: <a><![CDATA[x]]><![CDATA[y]]></a>
)~", nullptr},
    // S21
    {"<a b='1'>x</a></a>", false, false, nullptr, "Unexpected end tag. Line 1, position 17."},
    // S22
    {"<a><b></b></a><b/>", false, false, nullptr, "There are multiple root elements. Line 1, position 16."},
    // S23
    {"<a xmlns:b='urn:x' b:c='1' xmlns:b='urn:x'/>", false, false, nullptr, "'xmlns:b' is a duplicate attribute name. Line 1, position 28."},
    // S24
    {"<a>&#xD800&#x3E;</a>", false, false, nullptr, "Invalid syntax for a hexadecimal numeric entity reference. Line 1, position 11."},
    // S25
    {"<a><![CDATA[\001]]></a>", false, false, nullptr, "'\001', hexadecimal value 0x01, is an invalid character. Line 1, position 13."},
    // S26
    {"<a1>", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a1. Line 1, position 5."},
    // S27
    {"<a b:c='1' b:d='2' xmlns:b='urn:x'/>", false, true, R"~(E ns='' local='a'
  A ns='urn:x' local='c' = '1'
  A ns='urn:x' local='d' = '2'
  A ns='http://www.w3.org/2000/xmlns/' local='b' = 'urn:x'
RENDER: <a b:c="1" b:d="2" xmlns:b="urn:x" />
)~", nullptr},
    // S28
    {"<a>&#x10FFFF;</a>", false, true, R"~(E ns='' local='a'
  T '􏿿'
RENDER: <a>􏿿</a>
)~", nullptr},
    // S29
    {"<a>&#1114112;</a>", false, false, nullptr, "'\355\260\200', hexadecimal value 0xDC00, is an invalid character. Line 1, position 6."},
    // S30
    {"<a>&#x0;</a>", false, false, nullptr, "'.', hexadecimal value 0x00, is an invalid character. Line 1, position 7."},
    // S31
    {"<?pi\r\ndata?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // S32
    {"<a><?pi\r\ndata?></a>", false, true, R"~(E ns='' local='a'
  PI pi 'data'
RENDER: <a><?pi data?></a>
)~", nullptr},
    // S33
    {"<a><!--\r\n--></a>", false, true, R"~(E ns='' local='a'
  C '\n'
RENDER: <a><!--\r\n--></a>
)~", nullptr},
    // S34
    {"<a1/>", false, true, R"~(E ns='' local='a1'
RENDER: <a1 />
)~", nullptr},
    // S35
    {"<a b='x'y='z'/>", false, false, nullptr, "'y' is an unexpected token. Expecting whitespace. Line 1, position 9."},
    // S36
    {"<a xmlns:b='http://www.w3.org/XML/1998/namespace'/>", false, false, nullptr, "Prefix 'b' cannot be mapped to namespace name reserved for \"xml\" or \"xmlns\". Line 1, position 13."},
    // S37
    {"<a1:b xmlns:a1='urn:x'/>", false, true, R"~(E ns='urn:x' local='b'
  A ns='http://www.w3.org/2000/xmlns/' local='a1' = 'urn:x'
RENDER: <a1:b xmlns:a1="urn:x" />
)~", nullptr},
    // S38
    {"<a b='x' b:c='y' xmlns:b='urn:x'/>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x'
  A ns='urn:x' local='c' = 'y'
  A ns='http://www.w3.org/2000/xmlns/' local='b' = 'urn:x'
RENDER: <a b="x" b:c="y" xmlns:b="urn:x" />
)~", nullptr},
    // S39
    {"<!DOCTYPE a [<!ENTITY e 'a'>]><a>&e;&e;</a>", false, true, R"~(E ns='' local='a'
  T 'aa'
RENDER: <a>aa</a>
)~", nullptr},
    // S40
    {"<a><!--a--><!--b--></a>", false, true, R"~(E ns='' local='a'
  C 'a'
  C 'b'
RENDER: <a><!--a--><!--b--></a>
)~", nullptr},
    // S41
    {"<!DOCTYPE a [<!ENTITY e '<b>'>]><a>&e;</a>", false, false, nullptr, "Incomplete entity contents. Line 1, position 29."},
    // S42
    {"<!DOCTYPE a [<!ENTITY e '</a>'>]><a>&e;</a>", false, false, nullptr, "Incomplete entity contents. Line 1, position 30."},
    // S43
    {"<!DOCTYPE a [<!ENTITY e '<'>]><a>&e;x</a>", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed: a. Line 1, position 27."},
    // S44
    {"<!DOCTYPE a [<!ENTITY e '<!--c-->'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  C 'c'
RENDER: <a><!--c--></a>
)~", nullptr},
    // S45
    {"<!DOCTYPE a [<!ENTITY e 'x'>>>]><a>&e;</a>", false, false, nullptr, "Expected DTD markup was not found. Line 1, position 29."},
    // S46
    {"<a1/>", false, true, R"~(E ns='' local='a1'
RENDER: <a1 />
)~", nullptr},
    // S47
    {"<a/><!--c--><?pi?>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // S48
    {"<![CDATA[x]]>", false, false, nullptr, "Data at the root level is invalid. Line 1, position 1."},
    // S49
    {"<a/><!foo>", false, false, nullptr, "Unexpected end of file while parsing DOCTYPE has occurred. Line 1, position 7."},
    // S50
    {"<?XML version='1.0'?><a/>", false, false, nullptr, "'XML' is an invalid name for processing instructions. Line 1, position 3."},
    // S51
    {"<?xml version='1.00'?><a/>", false, true, R"~(E ns='' local='a'
RENDER: <a />
)~", nullptr},
    // S52
    {"<a>&#38;#38;#65;</a>", false, true, R"~(E ns='' local='a'
  T '&#38;#65;'
RENDER: <a>&amp;#38;#65;</a>
)~", nullptr},
    // S53
    {"<!DOCTYPE a [<!ENTITY e '1&#65;2'>]><a>x&e;y</a>", false, true, R"~(E ns='' local='a'
  T 'x1A2y'
RENDER: <a>x1A2y</a>
)~", nullptr},
    // S54
    {"<a b='&#38;#65;'></a>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = '&#65;'
RENDER: <a b="&amp;#65;"></a>
)~", nullptr},
    // S55
    {"<a b='x&#10;y&#13;z'></a>", false, true, R"~(E ns='' local='a'
  A ns='' local='b' = 'x\ny\rz'
RENDER: <a b="x&#xA;y&#xD;z"></a>
)~", nullptr},
    // S56
    {"<a><!--a--b></a>", false, false, nullptr, "An XML comment cannot contain '--', and '-' cannot be the last character. Line 1, position 9."},
    // S57
    {"<a><!--a---></a>", false, false, nullptr, "An XML comment cannot contain '--', and '-' cannot be the last character. Line 1, position 9."},
    // S58
    {"<a><!---></a>", false, false, nullptr, "Unexpected end of file while parsing Comment has occurred. Line 1, position 14."},
    // S59
    {"<a><!--a--b--c--></a>", false, false, nullptr, "An XML comment cannot contain '--', and '-' cannot be the last character. Line 1, position 9."},
    // S60
    {"<a b='&'></a>", false, false, nullptr, "An error occurred while parsing EntityName. Line 1, position 8."},
    // S61
    {"<a b='& x'></a>", false, false, nullptr, "An error occurred while parsing EntityName. Line 1, position 8."},
    // S62
    {"<a b='&amp'></a>", false, false, nullptr, "''' is an unexpected token. The expected token is ';'. Line 1, position 11."},
    // S63
    {"<a>&#32;x</a>", false, true, R"~(E ns='' local='a'
  T ' x'
RENDER: <a> x</a>
)~", nullptr},
    // S64
    {"<!DOCTYPE a [<!ENTITY e ']]'>]><a>&e;</a>", false, true, R"~(E ns='' local='a'
  T ']]'
RENDER: <a>]]</a>
)~", nullptr},
    // S65
    {"<a><!---></a>x", false, false, nullptr, "Unexpected end of file while parsing Comment has occurred. Line 1, position 15."},
    // S66
    {"<a><!foo></a>", false, false, nullptr, "'foo' is an unexpected token. The expected token is '<!--' or '<[CDATA['. Line 1, position 6."},
    // S67
    {"<a><!DOCTYPE b></a>", false, false, nullptr, "Unexpected DTD declaration. Line 1, position 6."},
    // S68
    {"<a>\n<![CDATA[x]]>\n</a>", false, true, R"~(E ns='' local='a'
  CD 'x'
RENDER: <a><![CDATA[x]]></a>
)~", nullptr},
    // S69
    {"<a><![CDATA[a]] ]]></a>", false, true, R"~(E ns='' local='a'
  CD 'a]] '
RENDER: <a><![CDATA[a]] ]]></a>
)~", nullptr},
    // S70
    {"<a1:b1 xmlns:a1='urn:x'>text</a1:b1>", false, true, R"~(E ns='urn:x' local='b1'
  A ns='http://www.w3.org/2000/xmlns/' local='a1' = 'urn:x'
  T 'text'
RENDER: <a1:b1 xmlns:a1="urn:x">text</a1:b1>
)~", nullptr},
    // S71
    {"<a>&", false, false, nullptr, "Unexpected end of file has occurred. Line 1, position 4."},
    // S72
    {"<a>&f", false, false, nullptr, "An error occurred while parsing EntityName. Line 1, position 5."},
    // S73
    {"<a>&#", false, false, nullptr, "Unexpected end of file while parsing  has occurred. Line 1, position 4."},
    // S74
    {"<a>&#x", false, false, nullptr, "Unexpected end of file while parsing  has occurred. Line 1, position 4."},
    // S75
    {"<a>&#1", false, false, nullptr, "Unexpected end of file while parsing  has occurred. Line 1, position 4."},
    // S76
    {"<a><?pi?x></a>", false, false, nullptr, "The '?' character, hexadecimal value 0x3F, cannot be included in a name. Line 1, position 8."},
    // S77
    {"<a><?pi?>x</a>", false, true, R"~(E ns='' local='a'
  PI pi ''
  T 'x'
RENDER: <a><?pi?>x</a>
)~", nullptr},
    // S78
    {"<a> </a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // S79
    {"<a>&#9;&#10;&#13;</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // S80
    {"<a>x </a>", false, true, R"~(E ns='' local='a'
  T 'x '
RENDER: <a>x </a>
)~", nullptr},
    // S81
    {"<a/", false, false, nullptr, "Data at the root level is invalid. Line 1, position 1."},
    // S82
    {"<a b='1'/", false, false, nullptr, "Unexpected end of file has occurred. The following elements are not closed:  Line 1, position 10."},
    // S83
    {"<a><b/", false, false, nullptr, "Unexpected end of file while parsing > has occurred. Line 1, position 6."},
    // S84
    {"<a></a /", false, false, nullptr, "'/' is an unexpected token. The expected token is '>'. Line 1, position 8."},
    // S85
    {"<a></a/", false, false, nullptr, "'/' is an unexpected token. The expected token is '>'. Line 1, position 7."},
    // S86
    {"<!DOCTYPE a", false, false, nullptr, "Incomplete DTD content. Line 1, position 12."},
    // S87
    {"<!DOCTYPE a [", false, false, nullptr, "Incomplete DTD content. Line 1, position 14."},
    // S88
    {"<!DOCTYPE a [<!ENTITY e", false, false, nullptr, "Incomplete DTD content. Line 1, position 24."},
    // S89
    {"<?xml", false, false, nullptr, "Unexpected end of file while parsing Name has occurred. Line 1, position 6."},
    // S90
    {"<?pi", false, false, nullptr, "Unexpected end of file while parsing Name has occurred. Line 1, position 5."},
    // S91
    {"<a><?pi", false, false, nullptr, "Unexpected end of file while parsing Name has occurred. Line 1, position 8."},
    // S92
    {"<a><b></b></a>x", false, false, nullptr, "Data at the root level is invalid. Line 1, position 15."},
    // S93
    {"<a>\r</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
    // S94
    {"<a>\r\n</a>", false, true, R"~(E ns='' local='a'
RENDER: <a></a>
)~", nullptr},
};

const std::size_t kCaseCount = sizeof(kCases) / sizeof(kCases[0]);

std::string DumpTree(ILSpy::Decompiler::Xml::XNode& root)
{
    using namespace ILSpy::Decompiler::Xml;
    if (auto* e = dynamic_cast<XElement*>(&root))
        return DumpElement(*e, 0);
    return DumpNode(root, 0);
}

void RunCase(const Case& c)
{
    using namespace ILSpy::Decompiler::Xml;
    if (c.document) {
        if (c.ok) {
            auto doc = ParseDocumentText(c.input);
            std::string out;
            const XDeclaration* decl = doc->Declaration();
            if (decl != nullptr) {
                out += "DECL v='" + decl->Version + "' e='" + decl->Encoding + "' s='" + decl->Standalone + "' \n";
            }
            for (const auto& node : doc->Nodes()) {
                out += DumpNode(*node, 0);
            }
            out += "RENDER: " + Esc(doc->ToString(SaveOptions::DisableFormatting)) + "\n";
            EXPECT_STREQ(c.dump, out.c_str());
        } else {
            try {
                ParseDocumentText(c.input);
                FAIL() << "expected XmlException";
            } catch (const XmlException& ex) {
                EXPECT_STREQ(c.message, ex.what());
            }
        }
    } else {
        if (c.ok) {
            auto elem = ParseElementText(c.input);
            std::string out = DumpTree(*elem);
            out += "RENDER: " + Esc(elem->ToString(SaveOptions::DisableFormatting)) + "\n";
            EXPECT_STREQ(c.dump, out.c_str());
        } else {
            try {
                ParseElementText(c.input);
                FAIL() << "expected XmlException";
            } catch (const XmlException& ex) {
                EXPECT_STREQ(c.message, ex.what());
            }
        }
    }
}

} // namespace

TEST(XmlTextParserTest, ParsesTreesAndThrowsExactlyLikeDotNet)
{
    for (std::size_t i = 0; i < kCaseCount; ++i) {
        SCOPED_TRACE("case " + std::to_string(i));
        RunCase(kCases[i]);
    }
}
