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

// Tests for the port-authored System.Xml.Linq XNode machinery (cpp/Decompiler/
// Xml/XNode.hpp): the sibling navigation, the insert-before/after-self engine,
// Remove/ReplaceWith, the lazy sibling sequences, CompareDocumentOrder, and
// DeepEquals. Every expectation is pinned against the REAL .NET 10
// System.Xml.Linq driven by the gold probe (C:/temp-probe/XDomProbe/
// Program.cs -- the section headers below name the probe sections).

#include "Decompiler/Xml/XCData.hpp"
#include "Decompiler/Xml/XComment.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XDocumentType.hpp"
#include "Decompiler/Xml/XNode.hpp"
#include "Decompiler/Xml/XProcessingInstruction.hpp"
#include "Decompiler/Xml/XText.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace Xml = ILSpy::Decompiler::Xml;

template <typename TException, typename TFunc>
std::string ThrowsMessage(TFunc f)
{
	try {
		f();
	} catch (const TException& ex) {
		return ex.what();
	}
	return "<no throw>";
}

// Renders a document's comments as "v1,v2,..." (the probe's join shape).
std::string CommentValues(Xml::XDocument& document)
{
	std::vector<std::string> values;
	for (const auto& node : document.Nodes())
		if (const auto* comment = dynamic_cast<const Xml::XComment*>(node.get()))
			values.push_back(comment->Value());
	std::string result;
	for (std::size_t i = 0; i < values.size(); ++i) {
		if (i != 0)
			result += ',';
		result += values[i];
	}
	return result;
}

std::shared_ptr<Xml::XComment> AddComment(Xml::XDocument& document, const std::string& value)
{
	auto comment = std::make_shared<Xml::XComment>(value);
	document.Add(comment);
	return comment;
}

} // namespace

// (probe: "linked list / siblings") The sibling links over a 3-node list.
TEST(XNodeTest, SiblingLinks)
{
	Xml::XDocument document;
	auto n1 = AddComment(document, "1");
	auto n2 = AddComment(document, "2");
	auto n3 = AddComment(document, "3");

	EXPECT_EQ(CommentValues(document), std::string("1,2,3"));
	EXPECT_EQ(n2->NextNode(), n3.get());
	EXPECT_EQ(n3->NextNode(), nullptr);
	EXPECT_EQ(n1->PreviousNode(), nullptr);
	EXPECT_EQ(n2->PreviousNode(), n1.get());
	EXPECT_EQ(document.FirstNode(), n1.get());
	EXPECT_EQ(document.LastNode(), n3.get());
}

// (probe: "AddBeforeSelf / AddAfterSelf / Inserter text batching") The
// node-content insertions around an anchor.
TEST(XNodeTest, AddBeforeSelfAndAddAfterSelf)
{
	Xml::XDocument document;
	auto m1 = AddComment(document, "m1");
	auto m2 = AddComment(document, "m2");
	auto m3 = AddComment(document, "m3");

	m2->AddBeforeSelf(std::make_shared<Xml::XComment>("before"));
	EXPECT_EQ(CommentValues(document), std::string("m1,before,m2,m3"));
	m2->AddAfterSelf(std::make_shared<Xml::XComment>("after"));
	EXPECT_EQ(CommentValues(document), std::string("m1,before,m2,after,m3"));

	// Unparented anchors throw.
	EXPECT_EQ(ThrowsMessage<std::runtime_error>([] { Xml::XComment("x").AddBeforeSelf(std::string("y")); }),
		std::string("The parent is missing."));
	EXPECT_EQ(ThrowsMessage<std::runtime_error>([] { Xml::XComment("x").AddAfterSelf(std::string("y")); }),
		std::string("The parent is missing."));
}

// (probe: "AddBeforeSelf / AddAfterSelf / Inserter text batching") The
// Inserter text handling over a document (text lands through ValidateString).
TEST(XNodeTest, InserterTextOnDocument)
{
	Xml::XDocument document;
	auto m1 = AddComment(document, "m1");
	auto m2 = AddComment(document, "m2");

	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] { m2->AddBeforeSelf(std::string("text-before")); }),
		std::string("Non-whitespace characters cannot be added to content."));
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] {
		m1->AddAfterSelf(
			{ std::string("txtA"), std::make_shared<Xml::XComment>("mid"), std::string("txtB") });
	}), std::string("Non-whitespace characters cannot be added to content."));
	EXPECT_EQ(CommentValues(document), std::string("m1,m2"));

	// A multi-node insert keeps its order.
	Xml::XDocument other;
	AddComment(other, "b1");
	auto b2 = AddComment(other, "b2");
	AddComment(other, "b3");
	b2->AddBeforeSelf({ std::make_shared<Xml::XComment>("i1"), std::make_shared<Xml::XComment>("i2") });
	EXPECT_EQ(CommentValues(other), std::string("b1,i1,i2,b2,b3"));
}

// (probe: "AddBeforeSelf / AddAfterSelf / Inserter text batching") Text
// content merges into an adjacent XText node (after-self) or inserts a new
// node before a comment predecessor (before-self).
TEST(XNodeTest, InserterTextMerging)
{
	Xml::XDocument document;
	AddComment(document, "q1");
	auto text = std::make_shared<Xml::XText>("  ");
	document.Add(text);
	AddComment(document, "q2");

	text->AddAfterSelf(std::string(" \t "));
	EXPECT_EQ(text->Value(), std::string("   \t "));

	text->AddBeforeSelf(std::string("  "));
	std::vector<std::string> rendered;
	for (const auto& node : document.Nodes()) {
		if (dynamic_cast<const Xml::XComment*>(node.get()) != nullptr)
			rendered.push_back("comment");
		else
			rendered.push_back("<" + std::static_pointer_cast<Xml::XText>(node)->Value() + ">");
	}
	EXPECT_EQ(rendered, (std::vector<std::string> { "comment", "<  >", "<   \t >", "comment" }));
}

// (probe: "Remove / ReplaceWith") Detaching and replacing nodes.
TEST(XNodeTest, RemoveAndReplaceWith)
{
	Xml::XDocument document;
	auto r1 = AddComment(document, "r1");
	auto r2 = AddComment(document, "r2");
	auto r3 = AddComment(document, "r3");

	r2->Remove();
	EXPECT_EQ(CommentValues(document), std::string("r1,r3"));
	EXPECT_EQ(r2->parent_, nullptr);
	r3->Remove();
	r1->Remove();
	EXPECT_EQ(document.FirstNode(), nullptr);
	// The removed node survives while a shared_ptr holds it (the GC
	// stand-in; the probe's discarded-remove pattern).
	EXPECT_EQ(r2->Value(), std::string("r2"));

	EXPECT_EQ(ThrowsMessage<std::runtime_error>([] { Xml::XComment("x").Remove(); }),
		std::string("The parent is missing."));

	Xml::XDocument other;
	auto s1 = AddComment(other, "s1");
	auto s2 = AddComment(other, "s2");
	AddComment(other, "s3");
	s2->ReplaceWith(std::make_shared<Xml::XComment>("repl"));
	EXPECT_EQ(CommentValues(other), std::string("s1,repl,s3"));
	EXPECT_EQ(s2->parent_, nullptr);
	s1->ReplaceWith(std::string("  "));
	std::size_t count = 0;
	for (const auto& node : other.Nodes())
		count++;
	EXPECT_EQ(count, static_cast<std::size_t>(3));

	// ReplaceWith detaches the node first, so the failing replacement leaves
	// the node removed (the same shape the real .NET shows).
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] {
		auto last = other.LastNode();
		last->ReplaceWith(std::string("x"));
	}), std::string("Non-whitespace characters cannot be added to content."));
	EXPECT_EQ(ThrowsMessage<std::runtime_error>([] { Xml::XComment("x").ReplaceWith(std::string("y")); }),
		std::string("The parent is missing."));
}

// (probe: "clone-on-parented") Adding a parented node clones it; the
// original stays in its tree.
TEST(XNodeTest, CloneOnParentedAdd)
{
	Xml::XDocument first;
	auto p1 = AddComment(first, "p1");

	Xml::XDocument second;
	second.Add(p1);
	EXPECT_EQ(CommentValues(first), std::string("p1"));
	EXPECT_NE(second.FirstNode(), p1.get());
	EXPECT_EQ(dynamic_cast<const Xml::XComment*>(second.FirstNode())->Value(), std::string("p1"));
	// The clone is a distinct node with the same value.
	EXPECT_EQ(first.FirstNode(), p1.get());
}

// (probe: "clone-on-parented" Exc self-add) Adding a document node is
// rejected by the document validation before any adoption.
TEST(XNodeTest, DocumentNodeRejected)
{
	Xml::XDocument document;
	EXPECT_EQ(ThrowsMessage<std::invalid_argument>([&] {
		std::shared_ptr<Xml::XNode> aliased(&document, [](Xml::XNode*) {});
		document.Add(aliased);
	}), std::string("A node of type Document cannot be added to content."));
}

// (probe: "NodesAfterSelf / NodesBeforeSelf") The lazy self-sibling
// sequences.
TEST(XNodeTest, NodesAfterSelfAndBeforeSelf)
{
	Xml::XDocument document;
	auto z1 = AddComment(document, "z1");
	auto z2 = AddComment(document, "z2");
	AddComment(document, "z3");
	auto z4 = AddComment(document, "z4");

	std::string after;
	for (const auto& node : z2->NodesAfterSelf())
		if (const auto* comment = dynamic_cast<const Xml::XComment*>(node.get()))
			after += (after.empty() ? "" : ",") + comment->Value();
	EXPECT_EQ(after, std::string("z3,z4"));

	std::string before;
	for (const auto& node : z4->NodesBeforeSelf())
		if (const auto* comment = dynamic_cast<const Xml::XComment*>(node.get()))
			before += (before.empty() ? "" : ",") + comment->Value();
	EXPECT_EQ(before, std::string("z1,z2,z3"));

	std::size_t count = 0;
	for (const auto& node : z4->NodesAfterSelf())
		count++;
	EXPECT_EQ(count, static_cast<std::size_t>(0));
	count = 0;
	for (const auto& node : z1->NodesBeforeSelf())
		count++;
	EXPECT_EQ(count, static_cast<std::size_t>(0));
	count = 0;
	for (const auto& node : Xml::XComment("x").NodesAfterSelf())
		count++;
	EXPECT_EQ(count, static_cast<std::size_t>(0));
}

// (probe: "CompareDocumentOrder") The document-order comparison.
TEST(XNodeTest, CompareDocumentOrder)
{
	Xml::XDocument document;
	auto z1 = AddComment(document, "z1");
	auto z2 = AddComment(document, "z2");
	auto z3 = AddComment(document, "z3");

	EXPECT_EQ(Xml::XNode::CompareDocumentOrder(z2.get(), z2.get()), 0);
	EXPECT_EQ(Xml::XNode::CompareDocumentOrder(z1.get(), z2.get()), -1);
	EXPECT_EQ(Xml::XNode::CompareDocumentOrder(z2.get(), z1.get()), 1);
	EXPECT_EQ(Xml::XNode::CompareDocumentOrder(nullptr, z1.get()), -1);
	EXPECT_EQ(Xml::XNode::CompareDocumentOrder(z1.get(), nullptr), 1);
	EXPECT_EQ(Xml::XNode::CompareDocumentOrder(nullptr, nullptr), 0);

	Xml::XDocument otherDocument;
	auto other = AddComment(otherDocument, "other");
	EXPECT_EQ(ThrowsMessage<std::runtime_error>([&] { Xml::XNode::CompareDocumentOrder(z1.get(), other.get()); }),
		std::string("A common ancestor is missing."));

	EXPECT_TRUE(z2->IsAfter(z1.get()));
	EXPECT_TRUE(z2->IsBefore(z3.get()));
	EXPECT_FALSE(z2->IsAfter(z2.get()));
	EXPECT_TRUE(z2->IsAfter(nullptr));
	EXPECT_FALSE(z2->IsBefore(nullptr));
}

// (probe: "DeepEquals") The value equality over nodes and documents.
TEST(XNodeTest, DeepEquals)
{
	auto makeDocument = [] {
		auto document = std::make_shared<Xml::XDocument>();
		document->Add(std::make_shared<Xml::XComment>("a"));
		document->Add(std::make_shared<Xml::XComment>("b"));
		return document;
	};
	auto dX = makeDocument();
	auto dY = makeDocument();
	EXPECT_TRUE(Xml::XNode::DeepEquals(dX.get(), dY.get()));
	dY->Add(std::make_shared<Xml::XComment>("c"));
	EXPECT_FALSE(Xml::XNode::DeepEquals(dX.get(), dY.get()));

	EXPECT_TRUE(Xml::XNode::DeepEquals(nullptr, nullptr));
	EXPECT_FALSE(Xml::XNode::DeepEquals(nullptr, dX.get()));
	EXPECT_TRUE(Xml::XNode::DeepEquals(std::make_shared<Xml::XText>("a").get(), std::make_shared<Xml::XText>("a").get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(std::make_shared<Xml::XText>("a").get(), std::make_shared<Xml::XCData>("a").get()));
	EXPECT_TRUE(Xml::XNode::DeepEquals(std::make_shared<Xml::XCData>("a").get(), std::make_shared<Xml::XCData>("a").get()));
	EXPECT_TRUE(Xml::XNode::DeepEquals(std::make_shared<Xml::XComment>("x").get(), std::make_shared<Xml::XComment>("x").get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(std::make_shared<Xml::XComment>("x").get(), std::make_shared<Xml::XComment>("y").get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(std::make_shared<Xml::XComment>("x").get(), std::make_shared<Xml::XText>("x").get()));
	EXPECT_TRUE(Xml::XNode::DeepEquals(std::make_shared<Xml::XProcessingInstruction>("t", "d").get(),
		std::make_shared<Xml::XProcessingInstruction>("t", "d").get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(std::make_shared<Xml::XProcessingInstruction>("t", "d").get(),
		std::make_shared<Xml::XProcessingInstruction>("t", "e").get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(std::make_shared<Xml::XProcessingInstruction>("t", "d").get(),
		std::make_shared<Xml::XProcessingInstruction>("u", "d").get()));

	EXPECT_TRUE(Xml::XNode::DeepEquals(
		std::make_shared<Xml::XDocumentType>("n", std::string("p"), std::string("s"), std::string("i")).get(),
		std::make_shared<Xml::XDocumentType>("n", std::string("p"), std::string("s"), std::string("i")).get()));
	EXPECT_FALSE(Xml::XNode::DeepEquals(
		std::make_shared<Xml::XDocumentType>("n", std::nullopt, std::nullopt, std::string("i")).get(),
		std::make_shared<Xml::XDocumentType>("n", std::nullopt, std::nullopt, std::nullopt).get()));
	// A null publicId does not equal an empty one (the optional carries the
	// C# null-vs-empty distinction).
	auto withNull = std::make_shared<Xml::XDocumentType>("n", std::nullopt, std::string("s"), std::nullopt);
	auto withEmpty = std::make_shared<Xml::XDocumentType>("n", std::string(""), std::string("s"), std::nullopt);
	EXPECT_FALSE(Xml::XNode::DeepEquals(withNull.get(), withEmpty.get()));
}
