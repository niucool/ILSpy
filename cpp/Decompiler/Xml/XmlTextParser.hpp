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

// Port-authored stand-in for the XmlReader text-parser machinery behind
// XElement.Parse / XDocument.Parse (the .NET XLinq Load path: XmlReader.Create
// over a StringReader with IgnoreWhitespace = true, DtdProcessing = Parse and
// ConformanceLevel.Document -- the exact settings XNode.GetXmlReaderSettings
// builds for LoadOptions.None, the only form the BamlDecompiler's
// LiteralContentHandler uses). This closes the last documented gate of the
// Baml/ handler manifest (LiteralContentHandler, the 37th row): there is no
// ICSharpCode source file to mirror -- the BCL classes are consumed directly
// -- so this fills the same gap the vendored winmd reader fills for
// System.Reflection.Metadata.
//
// The parser reproduces the OBSERVABLE .NET 10 behavior, gold-pinned case for
// case against the real XElement.Parse/XDocument.Parse through the
// C:/temp-probe/XmlParseProbe probe (a ~215-case matrix: trees, renders, and
// the exact XmlException message/line/position arms) plus the decompiled
// System.Private.Xml XmlTextReaderImpl/XmlException semantics the positions
// derive from:
//
//  * The working buffer is UTF-16 (the .NET char[] view): the input UTF-8 is
//    decoded permissively (valid sequences to code points, surrogate-pair
//    encodings to lone surrogate units, malformed bytes to U+FFFD) and every
//    line ending is pre-normalized (\r\n and lone \r -> \n) exactly as the
//    .NET reader's in-place \r\n collapse does -- the XML 1.0 line-ending
//    normalization applies to text, attribute values, comments, CDATA and PI
//    data alike (a literal newline in an attribute value renders as a single
//    space through the attribute-value normalization; &#13;/&#10; char refs
//    stay literal).
//  * Positions: LinePos = charPos - lineStartPos with lineStartPos starting
//    at -1 (so the position of index i on a fresh line is i + 1); the
//    line/position suffix " Line {line}, position {pos}." is appended to
//    every reader error except Root-element-is-missing (line 0, no suffix).
//    EOF throws report len + 1 on a single-line input.
//  * Whitespace: with IgnoreWhitespace = true every text run whose expanded
//    value is entirely XML whitespace (0x20 0x9 0xA 0xD -- including char
//    refs like &#32;) is skipped, everywhere in content; a run with any
//    non-whitespace char is kept whole. Entity expansions and char refs
//    join the current text run (one XText); CDATA splits it (a separate
//    XCData).
//  * Names: a QName is one NCName run, at most one ':' separator; a second
//    ':' or any non-name char after the name throws the XmlConvert shapes
//    ("Name cannot begin with the '{c}' character, hexadecimal value
//    0x{X2}." for a bad first char, "The '{c}' character, hexadecimal value
//    0x{X2}, cannot be included in a name." for a bad continuation -- the
//    NUL-renders-as-'.' and surrogate-pair quirks included).
//  * Namespace declarations become XAttributes in the tree (xmlns -> the
//    no-namespace "xmlns" name, xmlns:b -> the
//    "http://www.w3.org/2000/xmlns/" namespace with the bound prefix as the
//    local name); the default xmlns='' undeclaration is allowed, an empty
//    prefixed binding throws "Invalid namespace declaration." at the value's
//    opening quote, the reserved xml prefix arms throw at the attribute
//    name start ("Prefix \"xml\" is reserved...") / the value start
//    ("Prefix '{0}' cannot be mapped to namespace name reserved for \"xml\"
//    or \"xmlns\"."). Prefixes resolve AFTER the whole start tag (an
//    attribute may use a prefix declared later in the same tag); the xml
//    prefix is implicitly bound; an undeclared prefix throws "'{prefix}' is
//    an undeclared prefix." at the element/attribute name start.
//  * The xml declaration is parsed at the reader's first position only
//    (<?xml case-sensitively, the char after "xml" must not be a name char):
//    version must come first and start with "1.0" ("Version number '{0}' is
//    invalid."), then encoding, then standalone ("yes"/"no", else "Syntax
//    for an XML declaration is invalid." at the value's opening quote); any
//    other/misordered/duplicate pseudo-attribute throws the same "Syntax for
//    an XML declaration is invalid." at that attribute name start. A PI
//    whose target case-insensitatively equals "xml" outside the declaration
//    position throws ("Unexpected XML declaration..." for the exact "xml"
//    spelling, "'{0}' is an invalid name for processing instructions."
//    otherwise) at the target start.
//  * The DOCTYPE is parsed before the root element only (a second DTD
//    throws "Cannot have multiple DTDs." at its '<'; one after the root
//    element throws "DTD must be defined before the document root
//    element."). The internal subset is captured verbatim and its ENTITY
//    declarations collected: general entities expand inline at use (char
//    refs and nested entity references included, first declaration wins
//    silently), SYSTEM (external) entities expand to empty, parameter (%)
//    entities are declared but not usable in content ("Reference to
//    undeclared entity '{0}'." -- the exact undeclared-entity error an
//    undefined name throws, at the entity name start). Markup ('<') inside
//    an entity value is a documented deferral (the .NET reader splices the
//    entity as a nested parse context with declaration-relative positions;
//    the port throws std::logic_error loudly until a real fixture carries
//    it). A DOCTYPE after the root element inside XDocument.Parse content
//    ("Unexpected DTD declaration.") and the root-level "<!X" fallback
//    ("Unexpected end of file while parsing DOCTYPE has occurred." at the
//    '!') are included.
//  * The XElement.Parse root rules: comments/PIs/DOCTYPE/xml-decl before the
//    root element are parsed but not kept; trailing comments/PIs are
//    skipped; any other trailing root-level content is invalid ("Data at
//    the root level is invalid." at the offending char, or at the '<' of a
//    tag with fewer than 4 characters left); a '<' at the root level with
//    fewer than 4 characters to EOF throws the same at the '<'. A second
//    root element throws "There are multiple root elements." at its name
//    start; a stray end tag "Unexpected end tag." at its name start; empty
//    input "Root element is missing." without line info. XDocument.Parse
//    keeps the declaration (XDeclaration: version, the encoding/standalone
//    values or the C# null mapped to the empty string), the XDocumentType
//    (name, PublicId/SystemId nullopt when absent, the verbatim internal
//    subset), and every root-level comment/PI.
//  * The element-content EOF arm: "Unexpected end of file has occurred. The
//    following elements are not closed: {list}." with the open elements
//    innermost-first joined ", " and a trailing "." (the empty list when the
//    EOF hits inside an incomplete start tag); the mismatched end tag
//    throws "The '{start}' start tag on line {L} position {P} does not match
//    the end tag of '{end}'." at the end tag's '>' with the start tag's
//    name-start line/position.
//
// C#-to-C++ porting decisions:
//  * XmlException (the reader's exception type) maps to the existing Xml::
//    XmlException (std::runtime_error subclass, see XmlConvert.hpp); the
//    message text is composed in full (the SR string plus the
//    " Line {line}, position {pos}." suffix) so tests assert byte-exact
//    strings.
//  * The UTF-8 boundary: names and values are decoded from the UTF-16
//    working buffer back to UTF-8 when the tree nodes are built (the port's
//    string convention); the invalid-char values render through the port's
//    XmlConvert helpers (InvalidCharDisplay/InvalidCharValue -- the
//    BuildCharExceptionArgs semantics including the NUL-as-'.' and the
//    lone-surrogate garbage-value quirk).
//  * XElement.Parse/XDocument.Parse are static members (the natural C#
//    surface); the free functions ParseElementText/ParseDocumentText are
//    the implementation entry points.
//  * LoadOptions beyond None (PreserveWhitespace/SetBaseUri/SetLineInfo) are
//    not ported (no BamlDecompiler consumer); XDocument.Parse maps the C#
//    nullable XDeclaration.Encoding/Standalone nulls to the empty string
//    (the null=="" equivalence -- every ported consumer reads them through
//    value comparisons).

#pragma once

#include <memory>
#include <string>

namespace ILSpy::Decompiler::Xml {

class XElement;
class XDocument;

// XElement.Parse(text) -- LoadOptions.None. Throws XmlException (the exact
// .NET message with line/position suffix), std::invalid_argument never (the
// C# ArgumentNullException arms are unreachable: std::string has no null, and
// the empty string throws XmlException "Root element is missing.").
std::shared_ptr<XElement> ParseElementText(const std::string& text);

// XDocument.Parse(text) -- LoadOptions.None.
std::shared_ptr<XDocument> ParseDocumentText(const std::string& text);

} // namespace ILSpy::Decompiler::Xml
