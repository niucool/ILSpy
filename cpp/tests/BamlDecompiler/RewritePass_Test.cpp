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

// Tests for the Rewrite-pass layer port (ICSharpCode.BamlDecompiler/
// Rewrite/{DocumentRewritePass,AttributeRewritePass,MarkupExtensionRewritePass,
// XClassRewritePass}.cs) -- every expectation is gold-pinned against the REAL
// internal passes from the installed ICSharpCode.BamlDecompiler.dll, driven
// through reflection by the gold probe (C:/temp-probe/RewritePassProbe,
// gold_raw.txt):
//  * DocumentRewritePass (section A): the single-child collapse, the
//    multi/zero-child non-rewrite, the attribute move (the namespace
//    declaration, the plain attribute the compiled-out Debug.Assert admits,
//    and the LAZY Attributes() iteration that moves only the FIRST attribute
//    before the stop condition ends the walk), the nested wrapper, the
//    append order on the surviving child, and the wrapper text left behind.
//  * AttributeRewritePass (section B): the settable-property inline (the
//    unresolved-namespace bare form), the read-only CanSet gate, the x:Key
//    position-0 insert, the HasElements/HasAttributes/no-annotation gates,
//    the lazy-stop fixpoint (two properties inlined over two rounds, a Plain
//    child skipped between them, and the duplicate-attribute THROW when two
//    children inline to the same name), the ProcessElement recursion into a
//    removed child's parent, the two-XText Value concatenation, and the
//    resolved-namespace short/full forms (the clr-namespace fallback the
//    scratch-element ResolveNamespace drives).
//  * MarkupExtensionRewritePass (section C): the parent-annotation gate, the
//    CanInlineExt base-chain walk (the MarkupExtension match, the non-
//    extension rejection, the annotation-less non-Ctor rejection), the Ctor
//    positional initializer, the named arguments (the attribute and the
//    property-child forms, the nested-extension value, the two-node
//    rejection), the x:Key arm, the two-children and non-xmlns-attribute
//    gates, the existing-attribute arm (the element still removed), and the
//    xmlns-declaration attribute that does NOT block the inline.
//  * XClassRewritePass (section D): the main-module rename to the direct
//    base type with the x:Class attribute, the ClassModifier arm, the
//    non-main-module and unresolved and unannotated gates, the two-children
//    rewrite, the XClassNames side effect, and the empty-base-types
//    InvalidOperationException (the port's faithful First() emulation).
//  * The full pass chain (section E): the four passes in the XamlDecompiler
//    order over the ProcessChildren e2e document, ending at the collapsed
//    <ToolBar> root.

#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Blocks.hpp"
#include "BamlDecompiler/Handlers/Records.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/Rewrite/AttributeRewritePass.hpp"
#include "BamlDecompiler/Rewrite/DocumentRewritePass.hpp"
#include "BamlDecompiler/Rewrite/MarkupExtensionRewritePass.hpp"
#include "BamlDecompiler/Rewrite/XClassRewritePass.hpp"
#include "BamlDecompiler/Xaml/XamlProperty.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/XamlContext.hpp"

#include "BamlTestSupport.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XText.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace Xml = ILSpy::Decompiler::Xml;
namespace Xaml = ILSpy::BamlDecompiler::Xaml;
namespace Rewrite = ILSpy::BamlDecompiler::Rewrite;
namespace TS = ILSpy::Decompiler::TypeSystem;
using ILSpy::BamlDecompiler::XamlContext;
using ILSpy::BamlDecompiler::Xaml::XamlProperty;
using ILSpy::BamlDecompiler::Xaml::XamlType;
using ILSpy::Tests::Baml::kMscorlibFullName;
using ILSpy::Tests::Baml::XamlContextFixture;

// The probe's pseudo namespace / xaml namespace spellings.
constexpr const char* kPseudoNs = "https://github.com/icsharpcode/ILSpy";
constexpr const char* kXamlNs = "http://schemas.microsoft.com/winfx/2006/xaml";

// The fixture the probe drives: the section-E context (real-shaped main
// module full name, the mscorlib-backed stub types) plus the rewrite-pass
// stub graph (ConfigureRewriteStubs).
class RewritePassTest : public ::testing::Test {
protected:
    XamlContextFixture fixture_;

    RewritePassTest()
    {
        // The rewrite-pass stub graph (the base types the XClass rename and
        // the CanInlineExt walk read) -- ConfigureRewriteStubs is idempotent
        // and additive to the fixture's existing stubs.
        (void)fixture_.ObjectType();
    }

    // A fresh section-E context (each drive's ResolveNamespace mutations stay
    // in its own context -- the probe's per-drive shape).
    std::unique_ptr<XamlContext> MakeContextE()
    {
        auto ctx = fixture_.MakeContextE();
        return ctx;
    }

    // The probe's NewXamlType: a directly-constructed XamlType over the
    // fixture's main module with a stub ResolvedType.
    std::shared_ptr<XamlType> NewXamlType(TS::TestSupport::LookupTypeDefinition& resolved)
    {
        auto type = std::make_shared<XamlType>(&fixture_.Compilation().MainModule(),
            kMscorlibFullName, resolved.Namespace(), resolved.Name());
        type->ResolvedType =
            TS::ITypePtr(&resolved, [](TS::IType*) {});
        return type;
    }

    // The probe's NewXamlProperty + TryResolve (the property resolves through
    // the declaring type stub's property list).
    std::shared_ptr<XamlProperty> NewXamlProperty(const std::shared_ptr<XamlType>& declaring,
        const std::string& name)
    {
        auto property = std::make_shared<XamlProperty>(declaring.get(), name);
        property->TryResolve();
        return property;
    }

    // A document rooted at the given element (the probe's new XDocument(...)).
    std::shared_ptr<Xml::XDocument> DocOf(const std::shared_ptr<Xml::XElement>& root)
    {
        auto doc = std::make_shared<Xml::XDocument>();
        doc->Add(root);
        return doc;
    }
};

// ===== A: DocumentRewritePass ===============================================

TEST_F(RewritePassTest, DocumentPassCollapsesTheSingleChild)
{
    auto ctx = MakeContextE();
    Rewrite::DocumentRewritePass pass;

    // Gold A1: the single-child collapse.
    {
        auto child = std::make_shared<Xml::XElement>("Child");
        auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
            Xml::XContent(child)));
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(), "<Child />");
    }
    // Gold A2: two children -- untouched.
    {
        auto wrapper = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"));
        wrapper->Add({std::make_shared<Xml::XElement>("Child1"),
            std::make_shared<Xml::XElement>("Child2")});
        auto doc = DocOf(wrapper);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
            "  <Child1 xmlns=\"\" />\r\n"
            "  <Child2 xmlns=\"\" />\r\n"
            "</Document>");
    }
    // Gold A3: zero children -- untouched.
    {
        auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document")));
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Document xmlns=\"" + std::string(kPseudoNs) + "\" />");
    }
    // Gold A4: the namespace-declaration attribute moves to the child.
    {
        auto wrapper = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"));
        wrapper->Add(std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns() + "d",
            kPseudoNs));
        auto child = std::make_shared<Xml::XElement>("Child");
        child->Add(std::make_shared<Xml::XAttribute>("a", "1"));
        wrapper->Add(child);
        auto doc = DocOf(wrapper);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Child a=\"1\" xmlns:d=\"" + std::string(kPseudoNs) + "\" />");
    }
    // Gold A5: a PLAIN attribute also moves (the Debug.Assert is compiled
    // out of the release assembly).
    {
        auto wrapper = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"));
        wrapper->Add(std::make_shared<Xml::XAttribute>("x", "plain"));
        auto child = std::make_shared<Xml::XElement>("Child");
        child->Add(std::make_shared<Xml::XAttribute>("a", "1"));
        wrapper->Add(child);
        auto doc = DocOf(wrapper);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(), "<Child a=\"1\" x=\"plain\" />");
    }
    // Gold A6: the nested wrapper (the inner pseudo-named wrapper becomes
    // the root after the outer collapse).
    {
        auto innerChild = std::make_shared<Xml::XElement>("Child1");
        auto inner = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
            Xml::XContent(innerChild));
        auto outer = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
            Xml::XContent(inner));
        auto doc = DocOf(outer);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
            "  <Child1 xmlns=\"\" />\r\n"
            "</Document>");
    }
    // Gold A7: no wrapper at all -- untouched.
    {
        auto doc = DocOf(std::make_shared<Xml::XElement>("PlainRoot"));
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(), "<PlainRoot />");
    }
    // Gold A8: the moved attribute APPENDS to the child's own attributes.
    {
        auto wrapper = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"));
        wrapper->Add(std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns() + "d",
            kPseudoNs));
        auto child = std::make_shared<Xml::XElement>("Child");
        child->Add(std::make_shared<Xml::XAttribute>("b", "2"));
        child->Add(std::make_shared<Xml::XAttribute>("a", "1"));
        wrapper->Add(child);
        auto doc = DocOf(wrapper);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Child b=\"2\" a=\"1\" xmlns:d=\"" + std::string(kPseudoNs) + "\" />");
    }
    // Gold A9: the wrapper's text node does not survive ReplaceWith.
    {
        auto wrapper = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"));
        wrapper->Add(Xml::XContent{std::make_shared<Xml::XText>("sometext"),
            std::shared_ptr<Xml::XElement>(std::make_shared<Xml::XElement>("Child"))});
        auto doc = DocOf(wrapper);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(), "<Child />");
    }
    // Gold A10/A11: the LAZY Attributes() walk moves only the FIRST wrapper
    // attribute (removing the yielded attribute ends the sequence; the rest
    // are dropped with the wrapper).
    {
        auto wrapper = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"));
        wrapper->Add(std::make_shared<Xml::XAttribute>("x", "1"));
        wrapper->Add(std::make_shared<Xml::XAttribute>("y", "2"));
        auto child = std::make_shared<Xml::XElement>("Child");
        child->Add(std::make_shared<Xml::XAttribute>("a", "1"));
        wrapper->Add(child);
        auto doc = DocOf(wrapper);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(), "<Child a=\"1\" x=\"1\" />");
    }
    {
        auto wrapper = std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"));
        wrapper->Add(std::make_shared<Xml::XAttribute>("x", "1"));
        wrapper->Add(std::make_shared<Xml::XAttribute>("y", "2"));
        wrapper->Add(std::make_shared<Xml::XAttribute>("z", "3"));
        auto child = std::make_shared<Xml::XElement>("Child");
        child->Add(std::make_shared<Xml::XAttribute>("a", "1"));
        wrapper->Add(child);
        auto doc = DocOf(wrapper);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(), "<Child a=\"1\" x=\"1\" />");
    }
}

// ===== B: AttributeRewritePass ===============================================

TEST_F(RewritePassTest, AttributePassInlinesTheSettableProperty)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::AttributeRewritePass pass;

    // Gold B1: the settable property inlined (the parent carries no XamlType
    // annotation -- IsAttachedTo true, but the Thread XamlType's namespace is
    // unresolved, so the full form renders the bare local name).
    auto parent = std::make_shared<Xml::XElement>("Root");
    auto child = std::make_shared<Xml::XElement>("Thread.Name");
    child->Value("n1");
    child->AddAnnotation(nameProp);
    parent->Add(child);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(), "<Root Thread.Name=\"n1\" />");
}

TEST_F(RewritePassTest, AttributePassSkipsTheReadonlyProperty)
{
    auto ctx = MakeContextE();
    // The section-E String.Length row: TryResolve resolves the LookupProperty
    // whose CanSet is false (the stub default).
    XamlProperty* lengthProp = ctx->ResolveProperty(1);
    ASSERT_NE(lengthProp, nullptr);
    Rewrite::AttributeRewritePass pass;

    // Gold B2: the CanSet gate -- not inlined.
    auto parent = std::make_shared<Xml::XElement>("Root");
    auto child = std::make_shared<Xml::XElement>("String.Length");
    child->Value("4");
    child->AddAnnotation(std::shared_ptr<XamlProperty>(lengthProp, [](XamlProperty*) {}));
    parent->Add(child);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Root>\r\n  <String.Length>4</String.Length>\r\n</Root>");
}

TEST_F(RewritePassTest, AttributePassInsertsTheKeyAtPositionZero)
{
    auto ctx = MakeContextE();
    Rewrite::AttributeRewritePass pass;
    Xml::XName key = ctx->GetKnownNamespace("Key", XamlContext::KnownNamespace_Xaml);

    // Gold B3: the x:Key arm.
    auto parent = std::make_shared<Xml::XElement>("Root");
    parent->Add(std::make_shared<Xml::XAttribute>("a", "1"));
    auto child = std::make_shared<Xml::XElement>(key);
    child->Value("k");
    parent->Add(child);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Root p1:Key=\"k\" a=\"1\" xmlns:p1=\"" + std::string(kXamlNs) + "\" />");
}

TEST_F(RewritePassTest, AttributePassSkipsElementedAndAttributedAndPlainChildren)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::AttributeRewritePass pass;

    // Gold B4: HasElements -- skipped.
    {
        auto parent = std::make_shared<Xml::XElement>("Root");
        auto child = std::make_shared<Xml::XElement>("Thread.Name");
        child->Add(std::make_shared<Xml::XElement>("Nested"));
        child->AddAnnotation(nameProp);
        parent->Add(child);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root>\r\n  <Thread.Name>\r\n    <Nested />\r\n  </Thread.Name>\r\n</Root>");
    }
    // Gold B5: HasAttributes -- skipped.
    {
        auto parent = std::make_shared<Xml::XElement>("Root");
        auto child = std::make_shared<Xml::XElement>("Thread.Name");
        child->Add(std::make_shared<Xml::XAttribute>("x", "1"));
        child->Value("v");
        child->AddAnnotation(nameProp);
        parent->Add(child);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root>\r\n  <Thread.Name x=\"1\">v</Thread.Name>\r\n</Root>");
    }
    // Gold B6: no annotation and not the key -- skipped.
    {
        auto parent = std::make_shared<Xml::XElement>("Root");
        auto child = std::make_shared<Xml::XElement>("Plain");
        child->Value("v");
        parent->Add(child);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(), "<Root>\r\n  <Plain>v</Plain>\r\n</Root>");
    }
}

TEST_F(RewritePassTest, AttributePassAnnotatedParentStillRendersTheBareForm)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::AttributeRewritePass pass;

    // Gold B7: the parent annotated with the STRING XamlType (a type whose
    // base chain never reaches Thread -- IsAttachedTo true), the Thread
    // XamlType's namespace still unresolved -- the bare form, appended after
    // the existing attribute.
    auto stringType = ctx->ResolveTypeOwning(1);
    ASSERT_NE(stringType, nullptr);
    auto parent = std::make_shared<Xml::XElement>("Root");
    parent->Add(std::make_shared<Xml::XAttribute>("first", "1"));
    parent->AddAnnotation(stringType);
    auto child = std::make_shared<Xml::XElement>("Thread.Name");
    child->Value("n1");
    child->AddAnnotation(nameProp);
    parent->Add(child);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(), "<Root first=\"1\" Thread.Name=\"n1\" />");
}

TEST_F(RewritePassTest, AttributePassThrowsOnTheDuplicateAttributeName)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::AttributeRewritePass pass;

    // Gold B8: two children inlining to the SAME attribute name -- the second
    // round's ReplaceAttributes throws InvalidOperationException "Duplicate
    // attribute." (the round-one attribute is already in the list), and the
    // second child survives the throw.
    auto parent = std::make_shared<Xml::XElement>("Root");
    parent->Add(std::make_shared<Xml::XAttribute>("first", "1"));
    auto c1 = std::make_shared<Xml::XElement>("Thread.Name");
    c1->Value("n1");
    c1->AddAnnotation(nameProp);
    auto c2 = std::make_shared<Xml::XElement>("Plain");
    c2->Value("skip");
    auto c3 = std::make_shared<Xml::XElement>("Thread.Name");
    c3->Value("n2");
    c3->AddAnnotation(nameProp);
    parent->Add(Xml::XContent{c1, c2, c3});
    auto doc = DocOf(parent);

    std::string message = "<no throw>";
    try {
        pass.Run(*ctx, *doc);
    } catch (const std::runtime_error& ex) {
        message = ex.what();
    }
    EXPECT_EQ(message, "Duplicate attribute.");
    EXPECT_EQ(doc->ToString(),
        "<Root first=\"1\" Thread.Name=\"n1\">\r\n"
        "  <Plain>skip</Plain>\r\n"
        "  <Thread.Name>n2</Thread.Name>\r\n"
        "</Root>");
}

TEST_F(RewritePassTest, AttributePassRecursesIntoNestedElements)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::AttributeRewritePass pass;

    // Gold B9: the ProcessElement recursion -- the leaf inlines into the Mid
    // element (its parent), not the Root.
    auto parent = std::make_shared<Xml::XElement>("Root");
    auto mid = std::make_shared<Xml::XElement>("Mid");
    auto leaf = std::make_shared<Xml::XElement>("Thread.Name");
    leaf->Value("n1");
    leaf->AddAnnotation(nameProp);
    mid->Add(leaf);
    parent->Add(mid);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(), "<Root>\r\n  <Mid Thread.Name=\"n1\" />\r\n</Root>");
}

TEST_F(RewritePassTest, AttributePassConcatenatesAdjacentTextNodes)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::AttributeRewritePass pass;

    // Gold B10: elem.Value concatenates the two XText nodes.
    auto parent = std::make_shared<Xml::XElement>("Root");
    auto child = std::make_shared<Xml::XElement>("Thread.Name");
    child->Add(Xml::XContent{std::make_shared<Xml::XText>("t1"),
        std::make_shared<Xml::XText>("t2")});
    child->AddAnnotation(nameProp);
    parent->Add(child);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(), "<Root Thread.Name=\"t1t2\" />");
}

TEST_F(RewritePassTest, AttributePassFixpointInlinesTwoDifferentProperties)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    auto priorityProp = NewXamlProperty(threadType, "Priority");
    Rewrite::AttributeRewritePass pass;

    // Gold B11: two DIFFERENT settable properties with a skipped child
    // between them -- the lazy-stop fixpoint inlines both over two rounds.
    auto parent = std::make_shared<Xml::XElement>("Root");
    parent->Add(std::make_shared<Xml::XAttribute>("first", "1"));
    auto c1 = std::make_shared<Xml::XElement>("Thread.Name");
    c1->Value("n1");
    c1->AddAnnotation(nameProp);
    auto c2 = std::make_shared<Xml::XElement>("Plain");
    c2->Value("skip");
    auto c3 = std::make_shared<Xml::XElement>("Thread.Priority");
    c3->Value("BelowNormal");
    c3->AddAnnotation(priorityProp);
    parent->Add(Xml::XContent{c1, c2, c3});
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Root first=\"1\" Thread.Name=\"n1\" Thread.Priority=\"BelowNormal\">\r\n"
        "  <Plain>skip</Plain>\r\n"
        "</Root>");
}

TEST_F(RewritePassTest, AttributePassResolvedNamespaceRendersTheShortAndFullForms)
{
    auto ctx = MakeContextE();
    Rewrite::AttributeRewritePass pass;

    // Gold B12: the Thread XamlType's namespace resolved against a scratch
    // element first, the parent annotated with the SAME XamlType --
    // IsAttachedTo false, the short local-only form.
    {
        auto threadType = NewXamlType(fixture_.ThreadType());
        auto scratch = std::make_shared<Xml::XElement>("Scratch");
        auto scratchRoot = std::make_shared<Xml::XElement>("ScratchRoot");
        scratchRoot->Add(scratch);
        threadType->ResolveNamespace(*scratch, *ctx);
        auto nameProp = NewXamlProperty(threadType, "Name");

        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto child = std::make_shared<Xml::XElement>("Thread.Name");
        child->Value("n1");
        child->AddAnnotation(nameProp);
        parent->Add(child);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(), "<Root Name=\"n1\" />");
    }
    // Gold B13: the same resolved XamlType WITHOUT the parent annotation --
    // IsAttachedTo true, the namespaced full form (the writer's generated
    // prefix for the clr-namespace).
    {
        auto threadType = NewXamlType(fixture_.ThreadType());
        auto scratch = std::make_shared<Xml::XElement>("Scratch");
        auto scratchRoot = std::make_shared<Xml::XElement>("ScratchRoot");
        scratchRoot->Add(scratch);
        threadType->ResolveNamespace(*scratch, *ctx);
        auto nameProp = NewXamlProperty(threadType, "Name");

        auto parent = std::make_shared<Xml::XElement>("Root");
        auto child = std::make_shared<Xml::XElement>("Thread.Name");
        child->Value("n1");
        child->AddAnnotation(nameProp);
        parent->Add(child);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root p1:Thread.Name=\"n1\" xmlns:p1=\"clr-namespace:System.Threading\" />");
    }
}

// ===== C: MarkupExtensionRewritePass ========================================

TEST_F(RewritePassTest, MarkupExtensionPassInlinesTheExtension)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::MarkupExtensionRewritePass pass;

    // Gold C1: the property element wrapping a MarkupExtension element --
    // inlined as the {markup:Null} attribute (the XamlExtension.ToString
    // resolves the NullExtension type's namespace against the parent).
    auto nullExtType = NewXamlType(fixture_.NullExtensionType());
    auto parent = std::make_shared<Xml::XElement>("Root");
    parent->AddAnnotation(threadType);
    auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
    propElem->AddAnnotation(nameProp);
    auto value = std::make_shared<Xml::XElement>("NullExtension");
    value->AddAnnotation(nullExtType);
    propElem->Add(value);
    parent->Add(propElem);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Root xmlns:markup=\"clr-namespace:System.Windows.Markup\" Name=\"{markup:Null}\" />");
}

TEST_F(RewritePassTest, MarkupExtensionPassSkipsNonExtensionsAndGateViolations)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::MarkupExtensionRewritePass pass;

    // Gold C2: CanInlineExt false -- the Thread element's base chain never
    // reaches MarkupExtension.
    {
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto value = std::make_shared<Xml::XElement>("Thread");
        value->AddAnnotation(threadType);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root>\r\n  <Thread.Name>\r\n    <Thread />\r\n  </Thread.Name>\r\n</Root>");
    }
    // Gold C14: the property annotation WITHOUT the parent's XamlType
    // annotation -- the `type == null` half of the gate (not inlined).
    {
        auto nullExtType = NewXamlType(fixture_.NullExtensionType());
        auto parent = std::make_shared<Xml::XElement>("Root");
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto value = std::make_shared<Xml::XElement>("NullExtension");
        value->AddAnnotation(nullExtType);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root>\r\n  <Thread.Name>\r\n    <NullExtension />\r\n  </Thread.Name>\r\n</Root>");
    }
    // Gold C9: the value element with neither a XamlType annotation nor the
    // Ctor name -- CanInlineExt false.
    {
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto value = std::make_shared<Xml::XElement>("Plain");
        value->Value("v");
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root>\r\n  <Thread.Name>\r\n    <Plain>v</Plain>\r\n  </Thread.Name>\r\n</Root>");
    }
}

TEST_F(RewritePassTest, MarkupExtensionPassRendersTheCtorAndNamedArguments)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::MarkupExtensionRewritePass pass;
    Xml::XName ctor = ctx->GetPseudoName("Ctor");

    // Gold C3: the Ctor arm (the pseudo-named Ctor element's text child --
    // the positional initializer).
    {
        auto arrayExtType = NewXamlType(fixture_.ArrayExtensionType());
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto value = std::make_shared<Xml::XElement>("ArrayExtension");
        value->AddAnnotation(arrayExtType);
        auto ctorElem = std::make_shared<Xml::XElement>(ctor);
        ctorElem->Add(std::make_shared<Xml::XText>("System.String"));
        value->Add(ctorElem);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root xmlns:markup=\"clr-namespace:System.Windows.Markup\" "
            "Name=\"{markup:Array System.String}\" />");
    }
    // Gold C4: the named arguments (the attribute and the property-child
    // forms).
    {
        auto arrayExtType = NewXamlType(fixture_.ArrayExtensionType());
        auto itemsProp = NewXamlProperty(arrayExtType, "Items");
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto value = std::make_shared<Xml::XElement>("ArrayExtension");
        value->AddAnnotation(arrayExtType);
        value->Add(std::make_shared<Xml::XAttribute>("Type", "System.Int32"));
        auto itemsElem = std::make_shared<Xml::XElement>("ArrayExtension.Items");
        itemsElem->Value("item");
        itemsElem->AddAnnotation(itemsProp);
        value->Add(itemsElem);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root xmlns:markup=\"clr-namespace:System.Windows.Markup\" "
            "Name=\"{markup:Array Type=System.Int32, Items=item}\" />");
    }
}

TEST_F(RewritePassTest, MarkupExtensionPassInsertsTheKeyAtPositionZero)
{
    auto ctx = MakeContextE();
    Rewrite::MarkupExtensionRewritePass pass;
    Xml::XName key = ctx->GetKnownNamespace("Key", XamlContext::KnownNamespace_Xaml);

    // Gold C5: the x:Key arm (no property annotation required).
    auto nullExtType = NewXamlType(fixture_.NullExtensionType());
    auto parent = std::make_shared<Xml::XElement>("Root");
    parent->Add(std::make_shared<Xml::XAttribute>("a", "1"));
    auto propElem = std::make_shared<Xml::XElement>(key);
    auto value = std::make_shared<Xml::XElement>("NullExtension");
    value->AddAnnotation(nullExtType);
    propElem->Add(value);
    parent->Add(propElem);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Root p1:Key=\"{markup:Null}\" a=\"1\" "
        "xmlns:markup=\"clr-namespace:System.Windows.Markup\" "
        "xmlns:p1=\"" + std::string(kXamlNs) + "\" />");
}

TEST_F(RewritePassTest, MarkupExtensionPassSkipsGateViolations)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::MarkupExtensionRewritePass pass;

    // Gold C6: two children inside the property element.
    {
        auto nullExtType = NewXamlType(fixture_.NullExtensionType());
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto v1 = std::make_shared<Xml::XElement>("NullExtension");
        v1->AddAnnotation(nullExtType);
        propElem->Add(Xml::XContent{v1, std::make_shared<Xml::XElement>("Second")});
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root>\r\n  <Thread.Name>\r\n    <NullExtension />\r\n    <Second />\r\n"
            "  </Thread.Name>\r\n</Root>");
    }
    // Gold C7: a non-xmlns attribute on the property element.
    {
        auto nullExtType = NewXamlType(fixture_.NullExtensionType());
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        propElem->Add(std::make_shared<Xml::XAttribute>("x", "1"));
        auto value = std::make_shared<Xml::XElement>("NullExtension");
        value->AddAnnotation(nullExtType);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root>\r\n  <Thread.Name x=\"1\">\r\n    <NullExtension />\r\n"
            "  </Thread.Name>\r\n</Root>");
    }
    // Gold C13: the named-argument child with TWO nodes -- InlineExtension
    // returns null.
    {
        auto arrayExtType = NewXamlType(fixture_.ArrayExtensionType());
        auto itemsProp = NewXamlProperty(arrayExtType, "Items");
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto value = std::make_shared<Xml::XElement>("ArrayExtension");
        value->AddAnnotation(arrayExtType);
        auto itemsElem = std::make_shared<Xml::XElement>("ArrayExtension.Items");
        itemsElem->Add(Xml::XContent{std::make_shared<Xml::XText>("a"),
            std::make_shared<Xml::XText>("b")});
        itemsElem->AddAnnotation(itemsProp);
        value->Add(itemsElem);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root>\r\n  <Thread.Name>\r\n    <ArrayExtension>\r\n"
            "      <ArrayExtension.Items>ab</ArrayExtension.Items>\r\n"
            "    </ArrayExtension>\r\n  </Thread.Name>\r\n</Root>");
    }
}

TEST_F(RewritePassTest, MarkupExtensionPassRemovesTheElementDespiteTheExistingAttr)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::MarkupExtensionRewritePass pass;

    // Gold C8: the parent already carries a Thread.Name attribute (a
    // DIFFERENT local name than the inlined Name) -- the element is removed,
    // both attributes remain.
    auto nullExtType = NewXamlType(fixture_.NullExtensionType());
    auto parent = std::make_shared<Xml::XElement>("Root");
    parent->Add(std::make_shared<Xml::XAttribute>("Thread.Name", "existing"));
    parent->AddAnnotation(threadType);
    auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
    propElem->AddAnnotation(nameProp);
    auto value = std::make_shared<Xml::XElement>("NullExtension");
    value->AddAnnotation(nullExtType);
    propElem->Add(value);
    parent->Add(propElem);
    auto doc = DocOf(parent);
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Root Thread.Name=\"existing\" xmlns:markup=\"clr-namespace:System.Windows.Markup\" "
        "Name=\"{markup:Null}\" />");
}

TEST_F(RewritePassTest, MarkupExtensionPassRendersNestedExtensions)
{
    auto ctx = MakeContextE();
    auto threadType = NewXamlType(fixture_.ThreadType());
    auto nameProp = NewXamlProperty(threadType, "Name");
    Rewrite::MarkupExtensionRewritePass pass;
    Xml::XName ctor = ctx->GetPseudoName("Ctor");

    // Gold C10: a nested extension inside the Ctor element.
    {
        auto arrayExtType = NewXamlType(fixture_.ArrayExtensionType());
        auto nullExtType = NewXamlType(fixture_.NullExtensionType());
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto value = std::make_shared<Xml::XElement>("ArrayExtension");
        value->AddAnnotation(arrayExtType);
        auto ctorElem = std::make_shared<Xml::XElement>(ctor);
        auto nested = std::make_shared<Xml::XElement>("NullExtension");
        nested->AddAnnotation(nullExtType);
        ctorElem->Add(nested);
        value->Add(ctorElem);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root xmlns:markup=\"clr-namespace:System.Windows.Markup\" "
            "Name=\"{markup:Array {markup:Null}}\" />");
    }
    // Gold C12: a property-child named argument whose value is a nested
    // extension element.
    {
        auto arrayExtType = NewXamlType(fixture_.ArrayExtensionType());
        auto nullExtType = NewXamlType(fixture_.NullExtensionType());
        auto itemsProp = NewXamlProperty(arrayExtType, "Items");
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        auto value = std::make_shared<Xml::XElement>("ArrayExtension");
        value->AddAnnotation(arrayExtType);
        auto itemsElem = std::make_shared<Xml::XElement>("ArrayExtension.Items");
        auto nested = std::make_shared<Xml::XElement>("NullExtension");
        nested->AddAnnotation(nullExtType);
        itemsElem->Add(nested);
        itemsElem->AddAnnotation(itemsProp);
        value->Add(itemsElem);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root xmlns:markup=\"clr-namespace:System.Windows.Markup\" "
            "Name=\"{markup:Array Items={markup:Null}}\" />");
    }
    // Gold C11: an xmlns-namespace attribute on the property element does NOT
    // block the inline (only non-xmlns attributes do).
    {
        auto nullExtType = NewXamlType(fixture_.NullExtensionType());
        auto parent = std::make_shared<Xml::XElement>("Root");
        parent->AddAnnotation(threadType);
        auto propElem = std::make_shared<Xml::XElement>("Thread.Name");
        propElem->AddAnnotation(nameProp);
        propElem->Add(std::make_shared<Xml::XAttribute>(Xml::XNamespace::Xmlns() + "sys",
            "urn:sys"));
        auto value = std::make_shared<Xml::XElement>("NullExtension");
        value->AddAnnotation(nullExtType);
        propElem->Add(value);
        parent->Add(propElem);
        auto doc = DocOf(parent);
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Root xmlns:markup=\"clr-namespace:System.Windows.Markup\" "
            "Name=\"{markup:Null}\" />");
    }
}

// ===== D: XClassRewritePass =================================================

TEST_F(RewritePassTest, XClassPassRenamesTheMainModuleTypeAndAddsClass)
{
    auto ctx = MakeContextE();
    Rewrite::XClassRewritePass pass;

    // Gold D1: the main-module String type -- renamed to the base type's
    // namespaced name, the x:Class attribute carrying the ORIGINAL type's
    // full name, the XClassNames side effect.
    auto stringType = ctx->ResolveTypeOwning(1);
    ASSERT_NE(stringType, nullptr);
    auto root = std::make_shared<Xml::XElement>("String");
    root->AddAnnotation(stringType);
    auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
        Xml::XContent(root)));
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
        "  <system:Object p2:Class=\"System.String\" xmlns:system=\"clr-namespace:System\" "
        "xmlns:p2=\"" + std::string(kXamlNs) + "\" />\r\n"
        "</Document>");
    ASSERT_EQ(ctx->XClassNames().size(), 1u);
    EXPECT_EQ(ctx->XClassNames()[0], "System.String");
}

TEST_F(RewritePassTest, XClassPassSkipsNonMainAndUnresolvedAndUnannotated)
{
    auto ctx = MakeContextE();
    Rewrite::XClassRewritePass pass;

    // Gold D2: the non-main-module type -- unchanged (the port drives the
    // stronger ResolvedType-set shape over the synthetic PresentationFramework
    // module; the render matches the probe's).
    {
        auto buttonStub = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
            "System.Windows.Controls.Button", "System.Windows.Controls",
            TS::FullTypeName(TS::TopLevelTypeName("System.Windows.Controls", "Button")),
            TS::TypeKind::Class, TS::Accessibility::Public, fixture_.Compilation(),
            fixture_.PresentationFramework());
        auto buttonType = std::make_shared<XamlType>(fixture_.PresentationFramework(),
            "PresentationFramework, Version=4.0.0.0, Culture=neutral, "
            "PublicKeyToken=31bf3856ad364e35",
            "System.Windows.Controls", "Button");
        buttonType->ResolvedType = TS::ITypePtr(buttonStub.get(), [](TS::IType*) {});
        auto root = std::make_shared<Xml::XElement>("Button");
        root->AddAnnotation(buttonType);
        auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
            Xml::XContent(root)));
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
            "  <Button xmlns=\"\" />\r\n"
            "</Document>");
        EXPECT_TRUE(ctx->XClassNames().empty());
    }
    // Gold D4: no annotation -- unchanged.
    {
        auto root = std::make_shared<Xml::XElement>("Plain");
        auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
            Xml::XContent(root)));
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
            "  <Plain xmlns=\"\" />\r\n"
            "</Document>");
        EXPECT_TRUE(ctx->XClassNames().empty());
    }
    // Gold D5: ResolvedType null -- unchanged.
    {
        auto unresolved = std::make_shared<XamlType>(&fixture_.Compilation().MainModule(),
            kMscorlibFullName, "System", "String");
        auto root = std::make_shared<Xml::XElement>("String");
        root->AddAnnotation(unresolved);
        auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
            Xml::XContent(root)));
        pass.Run(*ctx, *doc);
        EXPECT_EQ(doc->ToString(),
            "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
            "  <String xmlns=\"\" />\r\n"
            "</Document>");
        EXPECT_TRUE(ctx->XClassNames().empty());
    }
}

TEST_F(RewritePassTest, XClassPassAddsTheInternalClassModifier)
{
    auto ctx = MakeContextE();
    Rewrite::XClassRewritePass pass;

    // Gold D3: the internal main-module type -- the x:ClassModifier arm.
    auto canonType = NewXamlType(fixture_.CanonType());
    auto root = std::make_shared<Xml::XElement>("__Canon");
    root->AddAnnotation(canonType);
    auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
        Xml::XContent(root)));
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
        "  <system:Object p2:Class=\"System.__Canon\" p2:ClassModifier=\"internal\" "
        "xmlns:system=\"clr-namespace:System\" xmlns:p2=\"" + std::string(kXamlNs)
        + "\" />\r\n"
        "</Document>");
    ASSERT_EQ(ctx->XClassNames().size(), 1u);
    EXPECT_EQ(ctx->XClassNames()[0], "System.__Canon");
}

TEST_F(RewritePassTest, XClassPassRenamesBothChildren)
{
    auto ctx = MakeContextE();
    Rewrite::XClassRewritePass pass;

    // Gold D6: two children under the Document wrapper -- the String renamed
    // to Object, the Type renamed to MemberInfo (the System.Reflection
    // clr-namespace), both carrying their x:Class attributes.
    auto stringType = ctx->ResolveTypeOwning(1);
    ASSERT_NE(stringType, nullptr);
    auto typeType = ctx->ResolveTypeOwning(2);
    ASSERT_NE(typeType, nullptr);
    auto r1 = std::make_shared<Xml::XElement>("String");
    r1->AddAnnotation(stringType);
    auto r2 = std::make_shared<Xml::XElement>("Type");
    r2->AddAnnotation(typeType);
    auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
        Xml::XContent{r1, r2}));
    pass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(),
        "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
        "  <system:Object p2:Class=\"System.String\" xmlns:system=\"clr-namespace:System\" "
        "xmlns:p2=\"" + std::string(kXamlNs) + "\" />\r\n"
        "  <reflection:MemberInfo p2:Class=\"System.Type\" "
        "xmlns:reflection=\"clr-namespace:System.Reflection\" "
        "xmlns:p2=\"" + std::string(kXamlNs) + "\" />\r\n"
        "</Document>");
    ASSERT_EQ(ctx->XClassNames().size(), 2u);
    EXPECT_EQ(ctx->XClassNames()[0], "System.String");
    EXPECT_EQ(ctx->XClassNames()[1], "System.Type");
}

TEST_F(RewritePassTest, XClassPassThrowsOnEmptyBaseTypes)
{
    auto ctx = MakeContextE();
    Rewrite::XClassRewritePass pass;

    // The empty-DirectBaseTypes arm: the C# DirectBaseTypes.First() throws
    // InvalidOperationException -- the port maps it to std::runtime_error
    // with the probed .NET message (no trailing period). Real framework types
    // always report at least Object, so only a stub reaches the arm.
    auto emptyStub = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "System.Empty", "System",
        TS::FullTypeName(TS::TopLevelTypeName("System", "Empty")),
        TS::TypeKind::Class, TS::Accessibility::Public, fixture_.Compilation(),
        &fixture_.Compilation().MainModule());
    auto emptyType = NewXamlType(*emptyStub);
    auto root = std::make_shared<Xml::XElement>("Empty");
    root->AddAnnotation(emptyType);
    auto doc = DocOf(std::make_shared<Xml::XElement>(ctx->GetPseudoName("Document"),
        Xml::XContent(root)));

    std::string message = "<no throw>";
    try {
        pass.Run(*ctx, *doc);
    } catch (const std::runtime_error& ex) {
        message = ex.what();
    }
    EXPECT_EQ(message, "Sequence contains no elements");
}

// ===== E: the full pass chain ================================================

TEST_F(RewritePassTest, TheFullPassChainCollapsesTheWrapper)
{
    // Gold E: the ProcessChildren e2e document driven through the four
    // passes in the XamlDecompiler order (ConnectionIdRewritePass deferred) --
    // the XClass/MarkupExtension/Attribute passes leave the ToolBar document
    // untouched (the synthetic-module type is not main-module, the text node
    // is not an element), and DocumentRewritePass collapses the wrapper.
    auto ctx = fixture_.MakeContext();
    ILSpy::BamlDecompiler::IHandler* handler =
        ILSpy::BamlDecompiler::HandlerMap::LookupHandler(
            ILSpy::BamlDecompiler::Baml::BamlRecordType::DocumentStart);
    ASSERT_NE(handler, nullptr);
    std::unique_ptr<ILSpy::BamlDecompiler::BamlElement> elem =
        handler->Translate(*ctx, *ctx->RootNode(), nullptr);
    ASSERT_NE(elem, nullptr);

    auto doc = std::make_shared<Xml::XDocument>();
    doc->Add(elem->Xaml.Element);
    const std::string wrapperRender =
        "<Document xmlns=\"" + std::string(kPseudoNs) + "\">\r\n"
        "  <ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>\r\n"
        "</Document>";
    EXPECT_EQ(doc->ToString(), wrapperRender);

    Rewrite::XClassRewritePass xClassPass;
    xClassPass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(), wrapperRender);
    Rewrite::MarkupExtensionRewritePass markupPass;
    markupPass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(), wrapperRender);
    Rewrite::AttributeRewritePass attributePass;
    attributePass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(), wrapperRender);
    Rewrite::DocumentRewritePass documentPass;
    documentPass.Run(*ctx, *doc);
    EXPECT_EQ(doc->ToString(), "<ToolBar xmlns=\"http://probe.pi/ns\">hello</ToolBar>");
    EXPECT_TRUE(ctx->XClassNames().empty());
}

} // namespace
