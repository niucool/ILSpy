// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
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

// Tests for the BamlNode decompose step (the second Phase-9 slice): the
// header/footer predicates, the header-to-footer match table, and the
// Parse stack machine that folds the flat record list into the block tree.
//
// Every expectation is gold-pinned against the REAL ICSharpCode.BamlDecompiler
// BamlNode.Parse from the installed ilspycmd 11.0 tool, driven over the
// identical fixture bytes through the C:/temp-probe/BamlProbe reflection
// probe (the out/*.dump.txt "TREE" sections, embedded here through
// TestFixtures/BamlParseGold.hpp). The crafted t_*.baml streams pin the
// shapes no real fixture exercises: the omitted end record (the
// still-open blocks keep a null Footer), the unexpected footer, the
// leaf-before-header NRE, the truncated document, the second top-level
// document, and the empty document.

#include "BamlDecompiler/Baml/BamlNode.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"

#include "BamlDecompiler/BamlTestSupport.hpp"
#include "TestFixtures/BamlParseGold.hpp"
#include "TestFixtures/RealBaml.hpp"
#include "TestFixtures/SynthBaml.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ILSpy::Tests;

namespace Baml = ILSpy::BamlDecompiler::Baml;
using Baml::BamlBlockNode;
using Baml::BamlDocument;
using Baml::BamlNode;
using Baml::BamlRecord;
using Baml::BamlRecordNode;
using Baml::BamlRecordType;

using ILSpy::Tests::Baml::ReadBaml;
using ILSpy::BamlDecompiler::Baml::RecordTypeName;

// One gold dump line: "B <headerType> @<pos> F=@<fpos>:<footerType>" (a
// block; "F=@null" while an end record stays omitted) or "R <type> @<pos>"
// (a leaf).
struct GoldLine {
    bool isBlock = false;
    std::int64_t position = -1;
    bool hasFooter = false;
    std::int64_t footerPosition = -1;
};

GoldLine ParseGoldLine(const std::string& line) {
    GoldLine result;
    const std::size_t indent = line.find_first_not_of(' ');
    const std::string trimmed = line.substr(indent);
    result.isBlock = trimmed.compare(0, 2, "B ") == 0;
    if (!result.isBlock && trimmed.compare(0, 2, "R ") != 0)
        ADD_FAILURE() << "unrecognized gold line: " << line;
    const std::size_t at = trimmed.find('@');
    const std::size_t space = trimmed.find(' ', at);
    result.position = std::stoll(trimmed.substr(at + 1, space - at - 1));
    const std::size_t f = trimmed.find("F=@");
    if (f != std::string::npos) {
        const std::string rest = trimmed.substr(f + 3);
        if (rest != "null") {
            result.hasFooter = true;
            result.footerPosition = std::stoll(rest);
        }
    }
    return result;
}

// Renders the block tree in the probe's DumpTree format (the gold render):
// "B <headerType> @<pos> F=@<pos>:<footerType>" per block ("F=@null" while
// an end record stays omitted), "R <type> @<pos>" per record leaf, children
// indented two spaces per depth.
void RenderNode(std::vector<std::string>& lines, const BamlNode& node, int depth) {
    const std::string indent(static_cast<std::size_t>(2 * depth), ' ');
    if (const auto* leaf = dynamic_cast<const BamlRecordNode*>(&node)) {
        lines.push_back(indent + "R " + RecordTypeName(leaf->Type()) + " @" +
            std::to_string(leaf->Record()->Position));
        return;
    }
    const auto* block = static_cast<const BamlBlockNode*>(&node);
    std::string footerText = "@null";
    if (block->Footer != nullptr) {
        footerText = "@" + std::to_string(block->Footer->Position) + ":" +
            RecordTypeName(block->Footer->Type());
    }
    lines.push_back(indent + "B " + RecordTypeName(block->Type()) + " @" +
        std::to_string(block->Header->Position) + " F=" + footerText);
    for (const auto& child : block->Children)
        RenderNode(lines, *child, depth + 1);
}

std::vector<std::string> RenderTree(const BamlNode& root) {
    std::vector<std::string> lines;
    RenderNode(lines, root, 0);
    return lines;
}

// The gold comparison: line count plus every line.
void ExpectGoldTree(const BamlNode& root, const char* const* gold,
    std::size_t goldCount, const char* what) {
    const std::vector<std::string> lines = RenderTree(root);
    ASSERT_EQ(lines.size(), goldCount) << what;
    for (std::size_t i = 0; i < goldCount; i++) {
        EXPECT_EQ(lines[i], gold[i]) << what << " line " << i;
    }
}

// The parse-or-throw helper: returns the exception's message.
std::string ParseThrowsMessage(const std::string& bytes) {
    BamlDocument doc = ReadBaml(bytes);
    try {
        BamlNode::Parse(doc);
    } catch (const std::exception& ex) {
        return ex.what();
    }
    return "<no throw>";
}

// Collects every record position the tree references (each block header,
// each non-null footer, each leaf), for the conservation invariant.
std::multiset<std::int64_t> CollectedPositions(const BamlNode& node) {
    std::multiset<std::int64_t> positions;
    if (const auto* leaf = dynamic_cast<const BamlRecordNode*>(&node)) {
        positions.insert(leaf->Record()->Position);
        return positions;
    }
    const auto* block = static_cast<const BamlBlockNode*>(&node);
    positions.insert(block->Header->Position);
    if (block->Footer != nullptr)
        positions.insert(block->Footer->Position);
    for (const auto& child : block->Children) {
        const auto childPositions = CollectedPositions(*child);
        positions.insert(childPositions.begin(), childPositions.end());
    }
    return positions;
}

// Walks the tree asserting the parent back-pointers, the Annotation
// default (the handler payload starts empty), and the Record()/Type()
// base contracts.
void CheckNodeInvariants(const BamlNode& node, const BamlBlockNode* parent) {
    EXPECT_EQ(node.Parent, parent);
    EXPECT_FALSE(node.Annotation.has_value());
    if (const auto* leaf = dynamic_cast<const BamlRecordNode*>(&node)) {
        EXPECT_EQ(leaf->Type(), leaf->Record()->Type());
        return;
    }
    const auto* block = static_cast<const BamlBlockNode*>(&node);
    EXPECT_EQ(block->Type(), block->Header->Type());
    EXPECT_EQ(block->Record(), block->Header);
    for (const auto& child : block->Children)
        CheckNodeInvariants(*child, block);
}

// Every fixture's records must appear exactly once in the tree (as a
// header, a footer, or a leaf) -- Parse neither drops nor duplicates a
// record.
void CheckConservation(const BamlDocument& doc, const BamlNode& root) {
    std::multiset<std::int64_t> expected;
    for (std::size_t i = 0; i < doc.Count(); i++)
        expected.insert(doc[i].Position);
    EXPECT_EQ(CollectedPositions(root), expected);
}

// The gold block/leaf classification of a fixture's records: header
// positions, footer positions, and the header->footer pairs.
struct GoldClassification {
    std::set<std::int64_t> headers;
    std::set<std::int64_t> footers;
    std::map<std::int64_t, std::int64_t> matched;
};

GoldClassification ClassifyGold(const char* const* gold, std::size_t count) {
    GoldClassification result;
    for (std::size_t i = 0; i < count; i++) {
        const GoldLine line = ParseGoldLine(gold[i]);
        if (!line.isBlock)
            continue;
        result.headers.insert(line.position);
        if (line.hasFooter) {
            result.footers.insert(line.footerPosition);
            result.matched[line.position] = line.footerPosition;
        }
    }
    return result;
}

// --- The header/footer predicates ---------------------------------------------

TEST(BamlNodeTest, PredicatesOverTheSynthFixture) {
    // The synth fixture carries all ten header record types and all nine
    // footer types; the gold tree (the real Parse's own classification)
    // labels each record: every block line's record is a header, every
    // F=@ record is a footer, and nothing else is either.
    BamlDocument doc = ReadBaml(SynthBamlBytes());
    ASSERT_EQ(doc.Count(), 52u);

    const GoldClassification gold = ClassifyGold(kSynthTreeGold, kSynthTreeGoldCount);
    // Eleven block instances over the ten header types (two ElementStart
    // blocks) and their eleven matched footers (nine footer types).
    ASSERT_EQ(gold.headers.size(), 11u);
    ASSERT_EQ(gold.footers.size(), 11u);

    for (std::size_t i = 0; i < doc.Count(); i++) {
        const bool isHeader = BamlNode::IsHeader(doc[i]);
        const bool isFooter = BamlNode::IsFooter(doc[i]);
        EXPECT_EQ(isHeader, gold.headers.count(doc[i].Position) == 1)
            << "record " << i << " (" << RecordTypeName(doc[i].Type()) << ")";
        EXPECT_EQ(isFooter, gold.footers.count(doc[i].Position) == 1)
            << "record " << i << " (" << RecordTypeName(doc[i].Type()) << ")";
        EXPECT_FALSE(isHeader && isFooter);
    }
}

TEST(BamlNodeTest, PredicatesOverEveryBoundaryRecordClass) {
    // The ten header classes classify header-only; the nine footer classes
    // footer-only; the leaf classes neither.
    Baml::DocumentStartRecord documentStart;
    Baml::ElementStartRecord elementStart;
    Baml::NamedElementStartRecord namedElementStart;
    Baml::StaticResourceStartRecord staticResourceStart;
    Baml::KeyElementStartRecord keyElementStart;
    Baml::ConstructorParametersStartRecord constructorParametersStart;
    Baml::PropertyComplexStartRecord propertyComplexStart;
    Baml::PropertyListStartRecord propertyListStart;
    Baml::PropertyDictionaryStartRecord propertyDictionaryStart;
    Baml::PropertyArrayStartRecord propertyArrayStart;
    const BamlRecord* const headerRecords[] = {&documentStart, &elementStart,
        &namedElementStart, &staticResourceStart, &keyElementStart,
        &constructorParametersStart, &propertyComplexStart, &propertyListStart,
        &propertyDictionaryStart, &propertyArrayStart};
    for (const BamlRecord* header : headerRecords) {
        SCOPED_TRACE(RecordTypeName(header->Type()));
        EXPECT_TRUE(BamlNode::IsHeader(*header));
        EXPECT_FALSE(BamlNode::IsFooter(*header));
    }

    Baml::DocumentEndRecord documentEnd;
    Baml::ElementEndRecord elementEnd;
    Baml::KeyElementEndRecord keyElementEnd;
    Baml::StaticResourceEndRecord staticResourceEnd;
    Baml::ConstructorParametersEndRecord constructorParametersEnd;
    Baml::PropertyComplexEndRecord propertyComplexEnd;
    Baml::PropertyListEndRecord propertyListEnd;
    Baml::PropertyDictionaryEndRecord propertyDictionaryEnd;
    Baml::PropertyArrayEndRecord propertyArrayEnd;
    const BamlRecord* const footerRecords[] = {&documentEnd, &elementEnd,
        &keyElementEnd, &staticResourceEnd, &constructorParametersEnd,
        &propertyComplexEnd, &propertyListEnd, &propertyDictionaryEnd,
        &propertyArrayEnd};
    for (const BamlRecord* footer : footerRecords) {
        SCOPED_TRACE(RecordTypeName(footer->Type()));
        EXPECT_FALSE(BamlNode::IsHeader(*footer));
        EXPECT_TRUE(BamlNode::IsFooter(*footer));
    }

    Baml::TextRecord text;
    Baml::ConnectionIdRecord connectionId;
    Baml::AssemblyInfoRecord assemblyInfo;
    Baml::DeferableContentStartRecord deferableContentStart;
    const BamlRecord* const leafRecords[] = {&text, &connectionId,
        &assemblyInfo, &deferableContentStart};
    for (const BamlRecord* leaf : leafRecords) {
        SCOPED_TRACE(RecordTypeName(leaf->Type()));
        EXPECT_FALSE(BamlNode::IsHeader(*leaf));
        EXPECT_FALSE(BamlNode::IsFooter(*leaf));
    }
}

// --- The match table -----------------------------------------------------------

TEST(BamlNodeTest, MatchTableOverTheSynthFixture) {
    // The gold tree gives every header's real-matched footer; assert the
    // port's IsMatch agrees for every (header, footer) pair the fixture
    // carries -- and disagrees for every cross pair.
    BamlDocument doc = ReadBaml(SynthBamlBytes());

    const GoldClassification gold = ClassifyGold(kSynthTreeGold, kSynthTreeGoldCount);
    ASSERT_EQ(gold.matched.size(), 11u);

    std::vector<const BamlRecord*> headers, footers;
    for (std::size_t i = 0; i < doc.Count(); i++) {
        if (BamlNode::IsHeader(doc[i]))
            headers.push_back(&doc[i]);
        else if (BamlNode::IsFooter(doc[i]))
            footers.push_back(&doc[i]);
    }
    ASSERT_EQ(headers.size(), 11u);
    ASSERT_EQ(footers.size(), 11u);

    // IsMatch pairs by TYPE (any ElementStart matches any ElementEnd --
    // the Parse stack decides WHICH footer belongs to which header), so
    // the expected value is the header's gold-matched footer TYPE.
    std::map<std::int64_t, BamlRecordType> typeAtPosition;
    for (std::size_t i = 0; i < doc.Count(); i++)
        typeAtPosition[doc[i].Position] = doc[i].Type();

    for (const auto* header : headers) {
        const BamlRecordType matchedType =
            typeAtPosition.at(gold.matched.at(header->Position));
        for (const auto* footer : footers) {
            const bool expected = footer->Type() == matchedType;
            EXPECT_EQ(BamlNode::IsMatch(*header, *footer), expected)
                << RecordTypeName(header->Type()) << " vs "
                << RecordTypeName(footer->Type());
        }
    }
}

TEST(BamlNodeTest, MatchTableExplicitPairs) {
    Baml::DocumentStartRecord documentStart;
    Baml::DocumentEndRecord documentEnd;
    Baml::ElementStartRecord elementStart;
    Baml::NamedElementStartRecord namedElementStart;
    Baml::ElementEndRecord elementEnd;
    Baml::KeyElementStartRecord keyElementStart;
    Baml::KeyElementEndRecord keyElementEnd;
    Baml::PropertyListStartRecord propertyListStart;
    Baml::PropertyListEndRecord propertyListEnd;
    Baml::TextRecord text;

    EXPECT_TRUE(BamlNode::IsMatch(documentStart, documentEnd));
    // Both ElementStart and NamedElementStart match ElementEnd (the C#
    // pairing table's shared arm).
    EXPECT_TRUE(BamlNode::IsMatch(elementStart, elementEnd));
    EXPECT_TRUE(BamlNode::IsMatch(namedElementStart, elementEnd));
    EXPECT_TRUE(BamlNode::IsMatch(keyElementStart, keyElementEnd));
    EXPECT_TRUE(BamlNode::IsMatch(propertyListStart, propertyListEnd));

    EXPECT_FALSE(BamlNode::IsMatch(documentStart, elementEnd));
    EXPECT_FALSE(BamlNode::IsMatch(elementStart, documentEnd));
    EXPECT_FALSE(BamlNode::IsMatch(namedElementStart, keyElementEnd));
    EXPECT_FALSE(BamlNode::IsMatch(keyElementStart, elementEnd));
    // A leaf record never matches anything (the default arm).
    EXPECT_FALSE(BamlNode::IsMatch(text, documentEnd));
    EXPECT_FALSE(BamlNode::IsMatch(documentStart, text));
}

// --- The Parse tree over the fixtures ------------------------------------------

TEST(BamlNodeTest, ParseTreeOverTheSynthFixture) {
    BamlDocument doc = ReadBaml(SynthBamlBytes());
    auto root = BamlNode::Parse(doc);
    ASSERT_NE(root, nullptr);
    CheckNodeInvariants(*root, nullptr);
    CheckConservation(doc, *root);
    ExpectGoldTree(*root, kSynthTreeGold, kSynthTreeGoldCount, "synth");
}

TEST(BamlNodeTest, ParseTreeOverFindToolbar) {
    BamlDocument doc = ReadBaml(FindToolbarBamlBytes());
    auto root = BamlNode::Parse(doc);
    ASSERT_NE(root, nullptr);
    CheckNodeInvariants(*root, nullptr);
    CheckConservation(doc, *root);
    ExpectGoldTree(*root, kFindToolbarTreeGold, kFindToolbarTreeGoldCount,
        "findtoolbar");
}

TEST(BamlNodeTest, ParseTreeOverInstallationError) {
    BamlDocument doc = ReadBaml(InstallationErrorBamlBytes());
    auto root = BamlNode::Parse(doc);
    ASSERT_NE(root, nullptr);
    CheckNodeInvariants(*root, nullptr);
    CheckConservation(doc, *root);
    ExpectGoldTree(*root, kInstallationErrorTreeGold,
        kInstallationErrorTreeGoldCount, "installationerror");
}

// --- The crafted Parse shapes ---------------------------------------------------

TEST(BamlNodeTest, OmittedEndRecordKeepsANullFooter) {
    // [DocumentStart, PropertyListStart, ElementStart, ElementEnd,
    //  DocumentEnd]: the DocumentEnd pops past the still-open
    // PropertyListStart block, so that block keeps a null Footer (the C#
    // "End record can be omited (sometimes)" path).
    BamlDocument doc = ReadBaml(tOmitBamlBytes());
    auto root = BamlNode::Parse(doc);
    ASSERT_NE(root, nullptr);
    CheckNodeInvariants(*root, nullptr);
    CheckConservation(doc, *root);
    ExpectGoldTree(*root, kOmitTreeGold, kOmitTreeGoldCount, "t_omit");
}

TEST(BamlNodeTest, TwoOmittedEndRecords) {
    // [DocumentStart, PropertyArrayStart, PropertyComplexStart,
    //  ElementStart, ElementEnd, DocumentEnd]: both the Array and the
    // Complex blocks keep null footers.
    BamlDocument doc = ReadBaml(tOmit2BamlBytes());
    auto root = BamlNode::Parse(doc);
    ASSERT_NE(root, nullptr);
    CheckNodeInvariants(*root, nullptr);
    CheckConservation(doc, *root);

    ASSERT_EQ(root->Children.size(), 1u);
    const auto* arrayBlock =
        dynamic_cast<const BamlBlockNode*>(root->Children[0].get());
    ASSERT_NE(arrayBlock, nullptr);
    EXPECT_EQ(arrayBlock->Type(), BamlRecordType::PropertyArrayStart);
    EXPECT_EQ(arrayBlock->Footer, nullptr);
    ASSERT_EQ(arrayBlock->Children.size(), 1u);
    const auto* complexBlock =
        dynamic_cast<const BamlBlockNode*>(arrayBlock->Children[0].get());
    ASSERT_NE(complexBlock, nullptr);
    EXPECT_EQ(complexBlock->Type(), BamlRecordType::PropertyComplexStart);
    EXPECT_EQ(complexBlock->Footer, nullptr);
}

TEST(BamlNodeTest, UnexpectedFooterThrows) {
    // [DocumentEnd]: the C# Exception("Unexpected footer.").
    EXPECT_EQ(ParseThrowsMessage(tUnexpectedFooterBamlBytes()),
        "Unexpected footer.");
    BamlDocument doc = ReadBaml(tUnexpectedFooterBamlBytes());
    EXPECT_THROW(BamlNode::Parse(doc), std::runtime_error);
}

TEST(BamlNodeTest, LeafBeforeAnyHeaderThrows) {
    // [ConnectionId]: the C# current.Children.Add NullReferenceException.
    EXPECT_EQ(ParseThrowsMessage(tLeafFirstBamlBytes()),
        "Object reference not set to an instance of an object.");
    BamlDocument doc = ReadBaml(tLeafFirstBamlBytes());
    EXPECT_THROW(BamlNode::Parse(doc), std::runtime_error);
}

TEST(BamlNodeTest, TruncatedDocumentReturnsTheRoot) {
    // [DocumentStart, ElementStart]: the C# returns `current` -- the
    // INNERMOST open block (the gold dump's root line is
    // "B ElementStart @35 F=@null"). The port's single-owner tree returns
    // the DocumentStart root instead (the documented divergence), which
    // owns the innermost block as its child with the Parent chain intact.
    BamlDocument doc = ReadBaml(tTruncatedBamlBytes());
    auto root = BamlNode::Parse(doc);
    ASSERT_NE(root, nullptr);
    CheckNodeInvariants(*root, nullptr);
    CheckConservation(doc, *root);

    EXPECT_EQ(root->Type(), BamlRecordType::DocumentStart);
    EXPECT_EQ(root->Footer, nullptr);
    ASSERT_EQ(root->Children.size(), 1u);
    const auto* element =
        dynamic_cast<const BamlBlockNode*>(root->Children[0].get());
    ASSERT_NE(element, nullptr);
    EXPECT_EQ(element->Type(), BamlRecordType::ElementStart);
    EXPECT_EQ(element->Footer, nullptr);
    EXPECT_EQ(element->Parent, root.get());
}

TEST(BamlNodeTest, SecondTopLevelDocumentNestsUnderTheRoot) {
    // [DocumentStart, DocumentEnd, DocumentStart, DocumentEnd]: after the
    // first DocumentEnd pops back to the root, the second DocumentStart
    // re-parents onto the root's children (the gold tree).
    BamlDocument doc = ReadBaml(tTwoDocsBamlBytes());
    auto root = BamlNode::Parse(doc);
    ASSERT_NE(root, nullptr);
    CheckNodeInvariants(*root, nullptr);
    CheckConservation(doc, *root);
    ExpectGoldTree(*root, kTwoDocsTreeGold, kTwoDocsTreeGoldCount, "t_two_docs");
}

TEST(BamlNodeTest, EmptyDocumentReturnsNull) {
    // The header-only stream: zero records. The C# Debug.Assert is
    // compiled out of the release assembly, so Parse returns null.
    BamlDocument doc = ReadBaml(tEmptyBamlBytes());
    EXPECT_EQ(doc.Count(), 0u);
    EXPECT_EQ(BamlNode::Parse(doc), nullptr);
}

TEST(BamlNodeTest, UnmatchedFooterThrowsInsteadOfSpinning) {
    // [DocumentStart, PropertyListEnd]: the end record matches no open
    // block once the parent stack empties. The C# while-loop spins forever
    // (the infinite-loop quirk); the port throws instead (the
    // crafted-input hardening -- see BamlNode.hpp).
    BamlDocument doc = ReadBaml(tUnmatchedFooterBamlBytes());
    EXPECT_THROW(BamlNode::Parse(doc), std::out_of_range);
}

// --- The node base contracts ------------------------------------------------------

TEST(BamlNodeTest, HandConstructedNodes) {
    // A headerless block node: Type() maps the C# Header.Type NRE, and
    // Record() mirrors the C# null Header.
    BamlBlockNode headerless;
    EXPECT_EQ(headerless.Record(), nullptr);
    EXPECT_EQ(headerless.Parent, nullptr);
    EXPECT_EQ(headerless.Footer, nullptr);
    EXPECT_TRUE(headerless.Children.empty());
    EXPECT_THROW(headerless.Type(), std::runtime_error);

    // A record node round-trips Type()/Record() and starts unannotated.
    Baml::ConnectionIdRecord connectionId;
    BamlRecordNode node(&connectionId);
    EXPECT_EQ(node.Type(), BamlRecordType::ConnectionId);
    EXPECT_EQ(node.Record(), &connectionId);
    EXPECT_EQ(node.Parent, nullptr);
    EXPECT_FALSE(node.Annotation.has_value());
}

TEST(BamlNodeTest, AnnotationCarriesTheHandlerPayload) {
    // The C# `object Annotation`: the handlers attach their XamlElement
    // payload; std::any models the slot.
    BamlDocument doc = ReadBaml(SynthBamlBytes());
    auto root = BamlNode::Parse(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_FALSE(root->Annotation.has_value());
    root->Annotation = std::string("payload");
    ASSERT_TRUE(root->Annotation.has_value());
    EXPECT_EQ(std::any_cast<std::string>(root->Annotation), "payload");
}

} // namespace
