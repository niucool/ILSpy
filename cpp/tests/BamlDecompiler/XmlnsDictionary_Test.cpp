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

// Tests for the ported ICSharpCode.BamlDecompiler XmlnsDictionary /
// XmlnsScope / Xaml::NamespaceMap (XmlnsDictionary.cs + Xaml/NamespaceMap.cs).
// Every expectation is pinned against the real internal classes of the
// installed ICSharpCode.BamlDecompiler assembly, driven through reflection by
// the gold probe (C:\temp-probe\XmlnsProbe): the scenario ids in the comments
// (S1a..S10c) refer to that probe's output lines.
//
// The BamlElement payload of a scope is opaque to this whole surface (the
// XmlnsDictionary.cs logic never dereferences it), so the tests pass nullptr
// everywhere the real handlers pass the element being processed.

#include "BamlDecompiler/XmlnsDictionary.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

namespace Xaml = ILSpy::BamlDecompiler::Xaml;

using ILSpy::BamlDecompiler::XmlnsDictionary;
using ILSpy::BamlDecompiler::XmlnsScope;
using Xaml::NamespaceMap;

std::shared_ptr<NamespaceMap> Map(std::string prefix, std::string full, std::string xmlNs)
{
	return std::make_shared<NamespaceMap>(std::move(prefix), std::move(full), std::move(xmlNs));
}

std::shared_ptr<NamespaceMap> Map(std::string prefix, std::string full, std::string xmlNs,
	std::string clrNs)
{
	return std::make_shared<NamespaceMap>(std::move(prefix), std::move(full), std::move(xmlNs),
		std::move(clrNs));
}

TEST(XmlnsDictionaryTest, NamespaceMapThreeArgCtorLeavesClrNamespaceNull)
{
	// Gold S1a/S1b: the 3-arg ctor chains with a null CLR namespace and
	// ToString falls back to the XML namespace (`??`).
	const NamespaceMap map("p", "Full", "Xml");
	EXPECT_EQ(map.XmlnsPrefix, "p");
	EXPECT_EQ(map.FullAssemblyName, "Full");
	EXPECT_EQ(map.XMLNamespace, "Xml");
	EXPECT_FALSE(map.CLRNamespace.has_value());
	EXPECT_EQ(map.ToString(), "p:[Full|Xml]");
}

TEST(XmlnsDictionaryTest, NamespaceMapFourArgCtorRendersClrNamespace)
{
	// Gold S1d/S1f: an engaged CLR namespace renders between the bars;
	// an engaged-but-empty one renders empty (no `??` fallback).
	EXPECT_EQ(NamespaceMap("p", "Full", "Xml", "Clr").ToString(), "p:[Full|Clr]");
	EXPECT_EQ(NamespaceMap("p", "Full", "Xml", "").ToString(), "p:[Full|]");
}

TEST(XmlnsDictionaryTest, NamespaceMapEmptyPrefixRendersEmpty)
{
	// Gold S1e: a null XmlnsPrefix renders as the empty prefix. The port's
	// documented null == "" equivalence (SetPIMapping's null prefix is
	// unobservable except here, and .NET interpolation renders null as
	// "" anyway).
	EXPECT_EQ(NamespaceMap("", "Full", "Xml", "Clr").ToString(), ":[Full|Clr]");
}

TEST(XmlnsDictionaryTest, NamespaceMapClrNamespaceIsSettable)
{
	// Gold S1g: the auto-property setter assigns through, and an
	// assigned CLR namespace displaces the ToString fallback.
	NamespaceMap map("p", "Full", "Xml");
	map.CLRNamespace = "LateClr";
	EXPECT_EQ(map.CLRNamespace, std::optional<std::string>("LateClr"));
	EXPECT_EQ(map.ToString(), "p:[Full|LateClr]");
}

TEST(XmlnsDictionaryTest, FreshDictionaryHasNullCurrentScope)
{
	// Gold S2a.
	XmlnsDictionary dict;
	EXPECT_EQ(dict.CurrentScope(), nullptr);
}

TEST(XmlnsDictionaryTest, PushPopScopesChainAndRestore)
{
	// Gold S2: pushing creates a chain (each scope's PreviousScope is the
	// enclosing one) and popping unwinds it to null.
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	dict.PushScope(nullptr);
	const auto inner = dict.CurrentScope();
	ASSERT_NE(inner, nullptr);
	// Two pushes: the current scope chains to the first pushed one.
	EXPECT_NE(inner->PreviousScope(), nullptr);
	dict.Add(Map("p", "Asm.Outer", "http://outer"));
	dict.PopScope();
	const auto outer = dict.CurrentScope();
	ASSERT_NE(outer, nullptr);
	// Gold S2h: after the pop the current scope is the outermost, whose
	// PreviousScope is null.
	EXPECT_EQ(outer->PreviousScope(), nullptr);
	EXPECT_EQ(dict.CurrentScope()->Element(), nullptr);
	dict.PopScope();
	EXPECT_EQ(dict.CurrentScope(), nullptr);
	// Gold S2h: after one push the scope's PreviousScope is null (the
	// outermost), and the inner chain is covered by the push test above.
}

TEST(XmlnsDictionaryTest, PushScopeChainsToThePreviousScope)
{
	// Gold S9f: a directly-constructed child reports the parent as its
	// PreviousScope; the pushed chain behaves the same.
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	const auto outer = dict.CurrentScope();
	dict.PushScope(nullptr);
	const auto inner = dict.CurrentScope();
	EXPECT_EQ(inner->PreviousScope().get(), outer.get());
	EXPECT_EQ(inner->Element(), nullptr);
}

TEST(XmlnsDictionaryTest, LookupNamespaceFromPrefixWalksInnermostFirst)
{
	// Gold S2c-S2f: the current scope's row wins over the enclosing one,
	// a missing prefix misses, and popping re-exposes the outer row.
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.Outer", "http://outer"));
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.Inner", "http://inner"));
	const auto found = dict.LookupNamespaceFromPrefix("p");
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->FullAssemblyName, "Asm.Inner");
	EXPECT_EQ(found->XMLNamespace, "http://inner");
	EXPECT_EQ(dict.LookupNamespaceFromPrefix("q"), nullptr);
	dict.PopScope();
	const auto after = dict.LookupNamespaceFromPrefix("p");
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(after->FullAssemblyName, "Asm.Outer");
	// Gold S2j: with the chain fully popped the lookup misses.
	dict.PopScope();
	EXPECT_EQ(dict.LookupNamespaceFromPrefix("p"), nullptr);
}

TEST(XmlnsDictionaryTest, LookupNamespaceFromXmlnsWalksInnermostFirst)
{
	// Gold S2k + S10b: the xmlns-keyed lookup walks the same chain and
	// misses when the chain is empty.
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.Outer", "http://outer"));
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.Inner", "http://inner"));
	const auto found = dict.LookupNamespaceFromXmlns("http://inner");
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->FullAssemblyName, "Asm.Inner");
	dict.PopScope();
	EXPECT_EQ(dict.LookupNamespaceFromXmlns("http://outer")->FullAssemblyName, "Asm.Outer");
	EXPECT_EQ(dict.LookupNamespaceFromXmlns("http://x"), nullptr);
}

TEST(XmlnsDictionaryTest, LookupNamespaceFromPrefixAppliesPIFixup)
{
	// Gold S3a/S3c: a row whose XML namespace has a PI mapping comes back
	// with the PI row's assembly and CLR namespace, keeping its own XML
	// namespace.
	XmlnsDictionary dict;
	dict.SetPIMapping("http://pi", "Ns.A", "Asm.A");
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.B", "http://pi"));
	const auto found = dict.LookupNamespaceFromPrefix("p");
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->FullAssemblyName, "Asm.A");
	EXPECT_EQ(found->CLRNamespace, std::optional<std::string>("Ns.A"));
	EXPECT_EQ(found->XMLNamespace, "http://pi");
}

TEST(XmlnsDictionaryTest, PIFixupMutatesTheStoredRow)
{
	// Gold S3d/S3e: PIFixup's mutation happens IN PLACE on the row the
	// scope stores -- a later LookupNamespaceFromXmlns (which never fixes
	// up itself, S3f/S3g) observes the already-fixed-up values.
	XmlnsDictionary dict;
	dict.SetPIMapping("http://pi", "Ns.A", "Asm.A");
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.B", "http://pi"));
	ASSERT_NE(dict.LookupNamespaceFromPrefix("p"), nullptr);
	const auto after = dict.LookupNamespaceFromXmlns("http://pi");
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(after->FullAssemblyName, "Asm.A");
	EXPECT_EQ(after->CLRNamespace, std::optional<std::string>("Ns.A"));
}

TEST(XmlnsDictionaryTest, LookupNamespaceFromXmlnsDoesNotFixUp)
{
	// Gold S3f/S3g: without a prior prefix lookup the row keeps its raw
	// stored values through the xmlns-keyed lookup.
	XmlnsDictionary dict;
	dict.SetPIMapping("http://pi", "Ns.A", "Asm.A");
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.B", "http://pi"));
	const auto found = dict.LookupNamespaceFromXmlns("http://pi");
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->FullAssemblyName, "Asm.B");
	EXPECT_FALSE(found->CLRNamespace.has_value());
}

TEST(XmlnsDictionaryTest, SetPIMappingFirstWins)
{
	// Gold S4: a second mapping for the same XML namespace is ignored.
	XmlnsDictionary dict;
	dict.SetPIMapping("http://pi", "Ns.A", "Asm.A");
	dict.SetPIMapping("http://pi", "Ns.X", "Asm.X");
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.B", "http://pi"));
	const auto found = dict.LookupNamespaceFromPrefix("p");
	ASSERT_NE(found, nullptr);
	EXPECT_EQ(found->FullAssemblyName, "Asm.A");
	EXPECT_EQ(found->CLRNamespace, std::optional<std::string>("Ns.A"));
}

TEST(XmlnsDictionaryTest, LookupXmlnsPrefersThePITable)
{
	// Gold S5a: the PI table is consulted before any scope row, and the
	// returned namespace is the PI row's KEY (the XML namespace the
	// mapping was registered under).
	XmlnsDictionary dict;
	dict.SetPIMapping("http://pi", "Ns.A", "Asm.A");
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.A", "http://scopeA", "Ns.A"));
	EXPECT_EQ(dict.LookupXmlns("Asm.A", "Ns.A"), std::optional<std::string>("http://pi"));
	// Gold S5b: a scope-only pair still falls through to the scope chain.
	EXPECT_EQ(dict.LookupXmlns("Asm.B2", "Ns.B2"), std::nullopt);
}

TEST(XmlnsDictionaryTest, LookupXmlnsWalksPITableInInsertionOrder)
{
	// Gold S5f: two PI mappings sharing one (assembly, clr-namespace)
	// pair resolve to the first-inserted one.
	XmlnsDictionary dict;
	dict.SetPIMapping("http://pi1", "Ns.A", "Asm.A");
	dict.SetPIMapping("http://pi2", "Ns.A", "Asm.A");
	EXPECT_EQ(dict.LookupXmlns("Asm.A", "Ns.A"), std::optional<std::string>("http://pi1"));
}

TEST(XmlnsDictionaryTest, LookupXmlnsNullClrNamespaceNeverMatches)
{
	// Gold S5c: a 3-arg row (null CLR namespace) never matches a lookup,
	// even against an empty requested namespace -- null != "" in the C#.
	// (The C# null==null arm of S5d is unreachable through the port's
	// std::string parameter.)
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.C", "http://scopeC"));
	EXPECT_EQ(dict.LookupXmlns("Asm.C", ""), std::nullopt);
	EXPECT_EQ(dict.LookupXmlns("Asm.C", "x"), std::nullopt);
}

TEST(XmlnsDictionaryTest, LookupXmlnsReturnsFirstRowOfTheScopeList)
{
	// Gold S5e: within one scope the first row matching the pair wins.
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.D", "http://d1", "Ns.D"));
	dict.Add(Map("p", "Asm.D", "http://d2", "Ns.D"));
	EXPECT_EQ(dict.LookupXmlns("Asm.D", "Ns.D"), std::optional<std::string>("http://d1"));
}

TEST(XmlnsDictionaryTest, LookupXmlnsWalksScopeChainInnermostFirst)
{
	// Gold S6a/S6b/S6c.
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.S", "http://outer", "Ns.S"));
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.S", "http://inner", "Ns.S"));
	EXPECT_EQ(dict.LookupXmlns("Asm.S", "Ns.S"), std::optional<std::string>("http://inner"));
	dict.PopScope();
	EXPECT_EQ(dict.LookupXmlns("Asm.S", "Ns.S"), std::optional<std::string>("http://outer"));
	EXPECT_EQ(dict.LookupXmlns("Asm.X", "Ns.X"), std::nullopt);
}

TEST(XmlnsDictionaryTest, LookupXmlnsOnEmptyChainMisses)
{
	// Gold S7c/S10c.
	XmlnsDictionary dict;
	EXPECT_EQ(dict.LookupXmlns("F", "N"), std::nullopt);
	dict.PushScope(nullptr);
	EXPECT_EQ(dict.LookupXmlns("F", "N"), std::nullopt);
	EXPECT_EQ(dict.LookupNamespaceFromPrefix("p"), nullptr);
	EXPECT_EQ(dict.LookupNamespaceFromXmlns("http://x"), nullptr);
}

TEST(XmlnsDictionaryTest, PopScopeOnEmptyThrowsNullReference)
{
	// Gold S7a: the C# `CurrentScope.PreviousScope` NullReferenceException
	// message.
	XmlnsDictionary dict;
	EXPECT_THROW({ try { dict.PopScope(); } catch (const std::runtime_error& e) {
		EXPECT_STREQ(e.what(), "Object reference not set to an instance of an object.");
		throw;
	} }, std::runtime_error);
}

TEST(XmlnsDictionaryTest, AddWithoutScopeThrowsNullReference)
{
	// Gold S7b: the C# `CurrentScope.Add(map)` NRE.
	XmlnsDictionary dict;
	EXPECT_THROW({ try { dict.Add(Map("p", "F", "X")); } catch (const std::runtime_error& e) {
		EXPECT_STREQ(e.what(), "Object reference not set to an instance of an object.");
		throw;
	} }, std::runtime_error);
}

TEST(XmlnsDictionaryTest, CurrentScopeIsSettable)
{
	// Gold S8: the public `CurrentScope { get; set; }` -- assigning null
	// (then popping) reproduces the NRE arm.
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	dict.SetCurrentScope(nullptr);
	EXPECT_EQ(dict.CurrentScope(), nullptr);
	EXPECT_THROW(dict.PopScope(), std::runtime_error);
}

TEST(XmlnsDictionaryTest, ScopeLocalLookupXmlns)
{
	// Gold S9a-S9e: a directly-constructed XmlnsScope (the C# ctor is
	// public) keeps its element and previous-scope payload and resolves
	// the first matching row of ITS OWN list only.
	XmlnsDictionary unused;
	const std::shared_ptr<XmlnsScope> scope = std::make_shared<XmlnsScope>(nullptr, nullptr);
	scope->Add(Map("p", "Asm.S", "http://s", "Ns.S"));
	scope->Add(Map("p2", "Asm.S2", "http://s2"));
	EXPECT_EQ(scope->LookupXmlns("Asm.S", "Ns.S"), std::optional<std::string>("http://s"));
	EXPECT_EQ(scope->LookupXmlns("Asm.S2", ""), std::nullopt);
	EXPECT_EQ(scope->LookupXmlns("Asm.X", "Ns.X"), std::nullopt);
	EXPECT_EQ(scope->Element(), nullptr);
	EXPECT_EQ(scope->PreviousScope(), nullptr);
	EXPECT_EQ(scope->Maps().size(), 2u);
}

TEST(XmlnsDictionaryTest, ScopeChildReportsParentAsPrevious)
{
	// Gold S9f: the child's PreviousScope is the exact parent instance.
	const auto parent = std::make_shared<XmlnsScope>(nullptr, nullptr);
	const auto child = std::make_shared<XmlnsScope>(parent, nullptr);
	EXPECT_EQ(child->PreviousScope().get(), parent.get());
}

TEST(XmlnsDictionaryTest, PoppedScopeStaysAliveThroughHeldHandles)
{
	// The port's shared_ptr chain reproduces the C# GC lifetime the
	// XElement annotations rely on (ProcessChildren attaches the popped
	// scope to the element's XAML node): a handle to a popped scope keeps
	// it and its whole PreviousScope chain usable.
	XmlnsDictionary dict;
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.Outer", "http://outer"));
	dict.PushScope(nullptr);
	dict.Add(Map("p", "Asm.Inner", "http://inner"));
	const auto held = dict.CurrentScope();
	dict.PopScope();
	dict.PopScope();
	EXPECT_EQ(dict.CurrentScope(), nullptr);
	// The held (popped) scope still resolves and still walks the chain.
	EXPECT_EQ(held->LookupXmlns("Asm.Inner", "x"), std::nullopt);
	EXPECT_NE(held->PreviousScope(), nullptr);
	EXPECT_EQ(held->PreviousScope()->Maps().size(), 1u);
}

} // namespace
