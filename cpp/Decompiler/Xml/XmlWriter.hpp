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

// Port-authored stand-in for System.Xml.XmlWriter over the output kinds the
// XNode serialization path uses (the backing of XNode.ToString/Save/WriteTo,
// the eventual --decompile-baml output gate).
//
// The .NET pipeline for these paths is a fixed composition:
// XmlWriter.Create(TextWriter, settings) builds an XmlWellFormedWriter (the
// well-formedness state machine, the namespace stack and the prefix
// generation) over an XmlEncodedRawTextWriter or, when settings.Indent, an
// XmlEncodedRawTextWriterIndent (the character buffer, the entity escaping
// and the indentation). The port FLATTENS those three classes into one
// XmlWriter class: the composition is fixed for the sinks the port supports,
// and every marker/flush interaction between them (the _textPos/_contentPos/
// _attrEndPos buffer markers the indent logic reads) is internal to the
// composition, not observable at the public surface.
//
// Gold-pinned against the real .NET 10 System.Xml via the XmlWriteProbe probe
// (C:/temp-probe/XmlWriteProbe/src/Program.cs) over 62 writer/XNode cases;
// the C# semantics were taken from the decompiled System.Private.Xml sources
// (XmlWellFormedWriter.cs, XmlEncodedRawTextWriter.cs,
// XmlEncodedRawTextWriterIndent.cs, XmlCharType.cs).
//
// KEY DESIGN (the sink): the C# writes into a TextWriter (StringWriter for
// ToString) or a file stream. The port accumulates UTF-16 code units into an
// internal std::u16string sink (the StringWriter analog) -- exactly the char
// stream the C# buffers and flushes into its TextWriter -- and converts to
// UTF-8 at the end (XNode.ToString) or to BOM-prefixed UTF-8 bytes for a file
// (XDocument.Save). The Text sink renders the declaration with the
// TextWriter's own encoding name ("utf-16"); the File sink uses the settings'
// encoding (default "utf-8") and writes the UTF-8 preamble.
//
// KEY DESIGN (the 6144-unit buffer): the C# raw writer buffers 6144 chars
// and flushes to the TextWriter when the buffer fills; FlushBuffer resets the
// position markers (_textPos/_attrEndPos to 1-or-0, _contentPos/_cdataPos to
// 0, _bufPos to 1), and those reset values PARTICIPATE in the indentation
// decisions (a flush between two adjacent CDATA sections breaks the merge, a
// flush after text keeps _textPos == _bufPos so no indent is inserted). The
// port reproduces the buffer, the flush points and the marker resets exactly
// so outputs crossing the 6144-unit boundary stay byte-identical.
//
// DEFERRED (the full XmlWriter surface): the async methods, WriteBase64/
// WriteBinHex/WriteChars, WriteEntityRef/WriteCharEntity/WriteSurrogateCharEntity
// (only reachable through the raw writer's invalid-char fallback and the
// value.ToString() content paths, which no XNode call site produces),
// WriteRaw/WriteQualifiedName, WriteValue (the typed overloads), the
// XmlReader-style LookupPrefix public API, the HTML/Text output methods, the
// non-default encodings for the file sink (every consumer writes UTF-8), and
// the stream-sink CharEntityEncoderFallback machinery (the TextWriter sink
// does not set _trackTextContent, so the text-content marks are inert on this
// path). The _useNsHashtable/_attrHashtable lookup-table optimizations are
// ported as plain linear scans (behaviorally identical: both tables always
// answer with the most recent entry the scan would find).

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Xml {

// System.Xml.ConformanceLevel.
enum class ConformanceLevel : std::uint8_t {
    Auto,
    Document,
    Fragment,
};

// System.Xml.NewLineHandling.
enum class NewLineHandling : std::uint8_t {
    Replace,
    Entitize,
    None,
};

// System.Xml.XmlStandalone.
enum class XmlStandalone : std::uint8_t {
    Omit,
    Yes,
    No,
};

// System.Xml.NamespaceHandling.
enum class NamespaceHandling : std::uint32_t {
    Default = 0,
    OmitDuplicates = 1,
};

// The output kind: a TextWriter-shaped sink (the StringWriter analog, XNode's
// ToString path) or a file sink (XDocument.Save, the UTF-8 preamble).
enum class XmlWriterSink : std::uint8_t {
    Text,
    File,
};

// System.Xml.XmlWriterSettings (the members the serialization path reads).
// The defaults are the C# ctor's: Indent=false, IndentChars="  ",
// NewLineChars=Environment.NewLine ("\r\n" on Windows), NewLineHandling=
// Replace, OmitXmlDeclaration=false, ConformanceLevel=Document,
// CheckCharacters=true, NamespaceHandling=Default, WriteEndDocumentOnClose=
// true. Encoding carries the web name only (the declaration text); the null
// state is the C# default Encoding.UTF8, and the Text sink overrides it with
// the TextWriter's "utf-16".
class XmlWriterSettings {
public:
    bool Indent = false;
    std::string IndentChars = "  ";
    std::string NewLineChars = "\r\n";
    NewLineHandling NewLineHandling = NewLineHandling::Replace;
    bool OmitXmlDeclaration = false;
    ConformanceLevel ConformanceLevel = ConformanceLevel::Document;
    bool CheckCharacters = true;
    NamespaceHandling NamespaceHandling = NamespaceHandling::Default;
    bool WriteEndDocumentOnClose = true;
    std::string Encoding = "utf-8";
};

// The port's stand-in for System.Xml.XmlWriter (the flattened
// XmlWellFormedWriter/XmlEncodedRawTextWriterIndent composition; see the
// header comment). Null string arguments use const char* (nullptr is the C#
// null, "" the empty string -- the distinction is load-bearing in
// WriteStartElement/WriteStartAttribute).
class XmlWriter {
public:
    // XmlWriter.Create(TextWriter, settings) / XmlWriter.Create(fileName,
    // settings): the Text sink forces the "utf-16" declaration (the
    // StringWriter's own encoding); the File sink keeps the settings'
    // encoding and writes the UTF-8 preamble on FileBytes().
    XmlWriter(XmlWriterSettings settings, XmlWriterSink sink);

    XmlWriter(const XmlWriter&) = delete;
    XmlWriter& operator=(const XmlWriter&) = delete;

    // --- the write surface (well-formed writer level) ---
    void WriteStartDocument();
    void WriteStartDocument(bool standalone);
    void WriteEndDocument();

    // The pubid/sysid/subset null-vs-empty distinction is observable through
    // the rendered DOCTYPE, so they are const char*.
    void WriteDocType(const std::string& name, const char* pubid, const char* sysid, const char* subset);

    void WriteStartElement(const char* prefix, const std::string& localName, const char* ns);
    // The convenience forms (prefix/ns default to null, like the C#).
    void WriteStartElement(const std::string& localName)
    {
        WriteStartElement(nullptr, localName, nullptr);
    }
    void WriteEndElement();
    void WriteFullEndElement();

    void WriteStartAttribute(const char* prefix, const std::string& localName, const char* ns);
    void WriteEndAttribute();
    void WriteAttributeString(const char* prefix, const std::string& localName, const char* ns, const char* value);
    void WriteAttributeString(const std::string& localName, const char* ns, const char* value)
    {
        WriteStartAttribute(nullptr, localName, ns);
        if (value != nullptr)
            WriteString(std::string(value));
        WriteEndAttribute();
    }
    void WriteAttributeString(const std::string& localName, const char* value)
    {
        WriteAttributeString(localName, nullptr, value);
    }

    void WriteString(const std::string& text);
    void WriteString(std::nullptr_t);
    void WriteComment(const std::string& text);
    void WriteCData(const std::string& text);
    void WriteProcessingInstruction(const std::string& name, const std::string& text);
    void WriteWhitespace(const std::string& ws);

    void Flush();
    void Close();

    // --- the sink ---
    // The accumulated output as UTF-8 (the StringWriter content transcoded;
    // the TextWriter-level encoding is UTF-16 and the transcode is lossless
    // for the valid XML chars the writer can emit).
    std::string OutputUtf8() const;
    // The File sink bytes: the UTF-8 preamble (EF BB BF) + OutputUtf8().
    std::vector<std::uint8_t> FileBytes() const;

private:
    // The well-formed writer's state machine (State/Token + the two 240-entry
    // transition tables, indexed [token * 16 + state]).
    enum class State : std::uint8_t {
        Start = 0,
        TopLevel = 1,
        Document = 2,
        Element = 3,
        Content = 4,
        B64Content = 5,
        B64Attribute = 6,
        AfterRootEle = 7,
        Attribute = 8,
        SpecialAttr = 9,
        EndDocument = 10,
        RootLevelAttr = 11,
        RootLevelSpecAttr = 12,
        RootLevelB64Attr = 13,
        AfterRootLevelAttr = 14,
        Closed = 15,
        Error = 16,
        // The transition pseudo-states (AdvanceState expands them).
        StartContent = 101,
        StartContentEle = 102,
        StartContentB64 = 103,
        StartDoc = 104,
        StartDocEle = 106,
        EndAttrSEle = 107,
        EndAttrEEle = 108,
        EndAttrSCont = 109,
        EndAttrSAttr = 110,
        PostB64Cont = 111,
        PostB64Attr = 112,
        PostB64RootAttr = 113,
        StartFragEle = 114,
        StartFragCont = 115,
        StartFragB64 = 116,
        StartRootLevelAttr = 117,
    };
    enum class Token : std::uint8_t {
        StartDocument = 0,
        EndDocument = 1,
        PI = 2,
        Comment = 3,
        Dtd = 4,
        StartElement = 5,
        EndElement = 6,
        StartAttribute = 7,
        EndAttribute = 8,
        Text = 9,
        CData = 10,
        AtomicValue = 11,
        Base64 = 12,
        RawData = 13,
        Whitespace = 14,
    };

    static const State* StateTableDocument();
    static const State* StateTableAuto();
    static const char* StateName(State state);
    static const char* TokenName(Token token);

    void AdvanceState(Token token);
    void StartElementContent();
    void StartFragment();

    // The special attributes the well-formed writer intercepts (the xmlns
    // declarations and the reserved xml:space/xml:lang).
    enum class SpecialAttribute : std::uint8_t {
        No,
        DefaultXmlns,
        PrefixedXmlns,
        XmlSpace,
        XmlLang,
    };

    // The raw-level attribute/namespace-decl writers (the C# raw-writer
    // methods the well-formed writer drives directly, without a state
    // advance).
    void RawWriteStartAttribute(const std::string& prefix, const std::string& localName);
    void RawWriteNamespaceDeclaration(const std::string& prefix, const std::string& namespaceName);
    void RawWriteString(const std::string& text);
    void SetSpecialAttribute(SpecialAttribute special);
    static std::string TrimUtf16(const std::string& utf8);

    // The namespace stack (the well-formed writer's Namespace entries).
    enum class NamespaceKind : std::uint8_t {
        Written,
        NeedToWrite,
        Implied,
        Special,
    };
    struct Namespace {
        std::string prefix;
        std::string namespaceUri;
        NamespaceKind kind = NamespaceKind::Implied;
    };
    struct ElementScope {
        std::string prefix;
        std::string localName;
        std::string namespaceUri;
        std::size_t prevNSTop = 0;
    };
    struct AttrName {
        std::string prefix;
        std::string localName;
        std::string namespaceUri;
        bool IsDuplicate(const std::string& prefix, const std::string& localName, const std::string& namespaceUri) const;
    };

    std::ptrdiff_t LookupNamespaceIndex(const std::string& prefix) const;
    std::string LookupNamespace(const std::string& prefix) const;
    std::string LookupPrefix(const std::string& ns) const;
    std::string LookupLocalNamespace(const std::string& prefix) const;
    std::string GeneratePrefix();
    void PushNamespaceImplicit(const std::string& prefix, const std::string& ns);
    bool PushNamespaceExplicit(const std::string& prefix, const std::string& ns);
    void AddNamespace(std::string prefix, std::string ns, NamespaceKind kind);
    void StartElementContentNamespaces();
    void AddAttribute(const std::string& prefix, const std::string& localName, const std::string& namespaceName);
    void CheckNCName(const std::string& ncname);
    void WriteStartDocumentImpl(XmlStandalone standalone);
    void WriteStringRaw(const std::string& text);

    // The raw writer's buffer machinery (see the KEY DESIGN note).
    void FlushBuffer();
    void RawText(std::u16string_view s);
    void WriteAttributeTextBlock(std::u16string_view s);
    void WriteElementTextBlock(std::u16string_view s);
    void WriteCommentOrPi(std::u16string_view s, char16_t stopChar);
    void WriteCDataSection(std::u16string_view s);
    void WriteIndent();
    void EnsureBuffer(std::size_t extra);
    void InvalidXmlChar(char16_t ch, bool entitize);

    // The settings-derived state.
    XmlWriterSettings settings_;
    XmlWriterSink sinkKind_;
    std::string encodingWebName_;

    // The well-formed writer's state.
    ConformanceLevel conformanceLevel_;
    const State* stateTable_;
    State currentState_ = State::Start;
    std::vector<Namespace> nsStack_;
    std::size_t nsTop_ = 0;
    std::vector<ElementScope> elemScopeStack_;
    std::size_t elemTop_ = 0;
    std::vector<AttrName> attrStack_;
    std::size_t attrCount_ = 0;
    SpecialAttribute specAttr_ = SpecialAttribute::No;
    std::string curDeclPrefix_;
    std::string attrValueCache_;      // the AttributeValueCache string chunks
    bool checkCharacters_ = true;
    bool omitDuplNamespaces_ = false;
    bool writeEndDocumentOnClose_ = true;
    bool dtdWritten_ = false;
    bool xmlDeclFollows_ = false;

    // The raw writer's buffer (the 6144-unit buffer + the marker positions).
    static constexpr std::size_t kBufLen = 6144;
    std::vector<char16_t> bufChars_;
    std::size_t bufPos_ = 1;
    std::size_t textPos_ = 1;
    std::size_t contentPos_ = 0;
    std::size_t cdataPos_ = 0;
    std::size_t attrEndPos_ = 0;
    bool inAttributeValue_ = false;
    bool hadDoubleBracket_ = false;
    bool writeToNull_ = false;
    std::u16string sink_;

    // The indent writer's state.
    int indentLevel_ = 0;
    bool mixedContent_ = false;
    std::vector<bool> mixedContentStack_;
    ConformanceLevel rootConformanceLevel_ = ConformanceLevel::Auto;
};

} // namespace ILSpy::Decompiler::Xml
