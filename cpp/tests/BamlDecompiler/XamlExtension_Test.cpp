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

// Tests for the XamlExtension port (ICSharpCode.BamlDecompiler/Xaml/
// XamlExtension.cs) and the two XamlUtils ToString extensions (the ToString
// halves of Xaml/XamlUtils.cs) -- every expectation is gold-pinned against the
// REAL internal classes from the installed ICSharpCode.BamlDecompiler.dll,
// driven through reflection by the gold probe
// (C:/temp-probe/XamlExtProbe/Program.cs) over the XamlContextProbe fixture
// shape (the XamlContextFixture: the six-module KnownThings compilation with
// PresentationCore carrying the XmlnsDefinitionAttribute rows, and the walk
// document with its PIMapping):
//  * ToString(ctx, elem, XamlType): the PI-mapped known type over a plain
//    element (no in-scope prefix, A1a), over an element with an in-scope
//    xmlns:pi prefix (A2a), and over an element whose DEFAULT namespace is the
//    type's namespace (no prefix, A3a); the clr-namespace fallback types
//    (the xmlns:seg attribute the render adds, A6a/A6b); the XmlnsDefinition
//    arm over the PresentationCore module (A7a/A7b); the empty-namespace
//    "global" prefix and the single-segment prefix (A8a/A8b).
//  * ToString(ctx, elem, XName): the xml-reserved-prefix fallback
//    (xml:space/xml:lang, A4a/A4b), the no-namespace name (A5a), the
//    in-scope prefix (A5b), the default-namespace collapse (A5c), the
//    not-in-scope namespace (A5d), and a prefix bound to a DIFFERENT
//    namespace (A5e).
//  * XamlExtension.ToString: the bare form (B1a), the Extension-suffix strip
//    (the exact-"Extension" name stripping to the empty name, B2b, and the
//    non-suffix name kept, B2c), the initializer shapes (B3a/B3b), the empty
//    initializer array rendering no space (B3c), the null initializer entry's
//    NRE (B3d), the nested extensions (B4a/B4b), the named arguments in
//    insertion order (B5a), the indexer's replace-in-place (B5b), the null
//    named value's NRE (B5c), the combined initializer+named forms (B6a/B6b),
//    the named nested extension (B7a), the XmlnsDefinition-arm type (B8b,
//    B10a/B10b), and the element mutation a fallback-resolved render leaves
//    behind (B9b).

#include "BamlDecompiler/Xaml/XamlExtension.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/Xaml/XamlUtils.hpp"
#include "BamlDecompiler/XamlContext.hpp"

#include "BamlTestSupport.hpp"
#include "Decompiler/Xml/XDocument.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace Xml = ILSpy::Decompiler::Xml;
namespace Xaml = ILSpy::BamlDecompiler::Xaml;
using ILSpy::Tests::Baml::XamlContextFixture;
using ILSpy::Tests::Baml::kPresentationCoreFullName;
using ILSpy::Tests::Baml::kProbePiNs;
using ILSpy::BamlDecompiler::Xaml::XamlExtension;
using ILSpy::BamlDecompiler::Xaml::XamlObject;
using ILSpy::BamlDecompiler::Xaml::XamlType;

// A parented holder element (ResolveNamespace's clr-namespace fallback calls
// AddBeforeSelf for the empty namespace, which throws for an unparented
// element -- the probe's Holder shape).
Xml::XElement* AddHolder(Xml::XDocument& doc, Xml::XElement& root, const char* name)
{
    auto holder = std::make_shared<Xml::XElement>(Xml::XName::Get(name));
    root.Add(holder);
    return holder.get();
}

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

// The extension-render fixture: the XamlContext over the walk document plus
// a parented holder element.
class XamlExtensionFixture {
public:
    XamlExtensionFixture()
    {
        ctx = fixture.MakeContext();
        doc = std::make_shared<Xml::XDocument>();
        root = std::make_shared<Xml::XElement>(Xml::XName::Get("root"));
        doc->Add(root);
        holder = AddHolder(*doc, *root, "e");
    }

    XamlContextFixture fixture;
    std::unique_ptr<ILSpy::BamlDecompiler::XamlContext> ctx;
    std::shared_ptr<Xml::XDocument> doc;
    std::shared_ptr<Xml::XElement> root;
    Xml::XElement* holder = nullptr;
};

// ===== XamlUtils.ToString(ctx, elem, XamlType) ==============================

TEST(XamlUtilsToStringTest, TypeOverloadPrefixMatrix)
{
    XamlExtensionFixture f;

    // Gold A1a: the PI-mapped known type (ToolBar) over a plain element -- the
    // PI namespace is not in scope, so no prefix.
    XamlType* toolBar = f.ctx->ResolveType(0xFD63);
    ASSERT_NE(toolBar, nullptr);
    EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder, *toolBar), "ToolBar");

    // Gold A2a: an in-scope xmlns:pi prefix renders.
    {
        XamlExtensionFixture f2;
        auto prefixed = std::make_shared<Xml::XElement>(Xml::XName::Get("p"),
            std::make_shared<Xml::XAttribute>(
                Xml::XNamespace::Xmlns() + "pi", std::string(kProbePiNs)));
        f2.root->Add(prefixed);
        EXPECT_EQ(Xaml::ToString(*f2.ctx, *prefixed, *f2.ctx->ResolveType(0xFD63)),
            "pi:ToolBar");
    }

    // Gold A3a: the element's DEFAULT namespace equals the type's namespace
    // -- no prefix (the first check collapses it).
    {
        XamlExtensionFixture f3;
        auto defaulted = std::make_shared<Xml::XElement>(Xml::XName::Get("d"),
            std::make_shared<Xml::XAttribute>("xmlns", std::string(kProbePiNs)));
        f3.root->Add(defaulted);
        EXPECT_EQ(Xaml::ToString(*f3.ctx, *defaulted, *f3.ctx->ResolveType(0xFD63)),
            "ToolBar");
    }
}

TEST(XamlUtilsToStringTest, TypeOverloadFallbackAndDefinitionArms)
{
    // Gold A6a: the assembly-less clr-namespace fallback -- the render adds
    // the xmlns:seg attribute (the last namespace segment lowercased) and
    // renders the prefixed name.
    {
        XamlExtensionFixture f;
        XamlType type(nullptr, "MyAssembly", "My.Nested.SEG", "TheType");
        EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder, type), "seg:TheType");
        EXPECT_EQ(f.holder->ToString(),
            "<e xmlns:seg=\"clr-namespace:My.Nested.SEG;assembly=MyAssembly\" />");
    }

    // Gold A6b: the same arm with an Extension-like type name.
    {
        XamlExtensionFixture f;
        XamlType type(nullptr, "MyAssembly", "My.Nested.SEG", "MyExt");
        EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder, type), "seg:MyExt");
    }

    // Gold A7a: the XmlnsDefinition arm over the fixture's PresentationCore
    // module (the presentation namespace, not in scope -- no prefix).
    {
        XamlExtensionFixture f;
        XamlType type(f.fixture.PresentationCore(), kPresentationCoreFullName,
            "System.Windows", "ContentElement");
        EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder, type), "ContentElement");
    }

    // Gold A7b: the same arm over an element with an in-scope pc prefix.
    {
        XamlExtensionFixture f;
        auto prefixed = std::make_shared<Xml::XElement>(Xml::XName::Get("p"),
            std::make_shared<Xml::XAttribute>(
                Xml::XNamespace::Xmlns() + "pc",
                std::string(ILSpy::BamlDecompiler::XamlContext::KnownNamespace_Presentation)));
        f.root->Add(prefixed);
        XamlType type(f.fixture.PresentationCore(), kPresentationCoreFullName,
            "System.Windows", "ContentElement");
        EXPECT_EQ(Xaml::ToString(*f.ctx, *prefixed, type), "pc:ContentElement");
    }

    // Gold A8a: the EMPTY namespace's prefix is "global".
    {
        XamlExtensionFixture f;
        XamlType type(nullptr, "MyAssembly", "", "TheType");
        EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder, type), "global:TheType");
    }

    // Gold A8b: a single-segment namespace is its own prefix.
    {
        XamlExtensionFixture f;
        XamlType type(nullptr, "MyAssembly", "JustOne", "TheType");
        EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder, type), "justone:TheType");
    }
}

// ===== XamlUtils.ToString(ctx, elem, XName) =================================

TEST(XamlUtilsToStringTest, NameOverloadScopeMatrix)
{
    XamlExtensionFixture f;

    // Gold A4a/A4b: the xml reserved-prefix fallback of
    // GetPrefixOfNamespace.
    EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder,
                   Xml::XName::Get("space", Xml::XNamespace::Xml().NamespaceName())),
        "xml:space");
    EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder,
                   Xml::XName::Get("lang", Xml::XNamespace::Xml().NamespaceName())),
        "xml:lang");

    // Gold A5a: a no-namespace name.
    EXPECT_EQ(Xaml::ToString(*f.ctx, *f.holder, Xml::XName::Get("foo")), "foo");

    // Gold A5b: the in-scope prefix.
    {
        auto prefixed = std::make_shared<Xml::XElement>(Xml::XName::Get("w"),
            std::make_shared<Xml::XAttribute>(
                Xml::XNamespace::Xmlns() + "w", std::string("urn:x")));
        f.root->Add(prefixed);
        EXPECT_EQ(Xaml::ToString(*f.ctx, *prefixed, Xml::XName::Get("foo", "urn:x")),
            "w:foo");
        // Gold A5e: a DIFFERENT namespace (no prefix in scope for it).
        EXPECT_EQ(Xaml::ToString(*f.ctx, *prefixed, Xml::XName::Get("foo", "urn:y")),
            "foo");
    }

    // Gold A5c: the element's default namespace collapses the prefix.
    {
        XamlExtensionFixture f2;
        auto defaulted = std::make_shared<Xml::XElement>(Xml::XName::Get("d"),
            std::make_shared<Xml::XAttribute>("xmlns", std::string("urn:x")));
        f2.root->Add(defaulted);
        EXPECT_EQ(Xaml::ToString(*f2.ctx, *defaulted, Xml::XName::Get("foo", "urn:x")),
            "foo");
    }

    // Gold A5d: a namespace nothing binds.
    {
        XamlExtensionFixture f3;
        EXPECT_EQ(Xaml::ToString(*f3.ctx, *f3.holder, Xml::XName::Get("foo", "urn:x")),
            "foo");
    }
}

// ===== XamlExtension ========================================================

TEST(XamlExtensionTest, BareRenderAndProperties)
{
    XamlExtensionFixture f;
    XamlType* toolBar = f.ctx->ResolveType(0xFD63);

    // Gold B1a: the bare form.
    XamlExtension ext(toolBar);
    EXPECT_EQ(ext.ExtensionType, toolBar);
    EXPECT_FALSE(ext.Initializer.has_value());
    EXPECT_TRUE(ext.NamedArguments.empty());
    EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar}");
}

TEST(XamlExtensionTest, ExtensionSuffixStrip)
{
    // Gold B2a: the fallback type's render strips nothing ("MyExt" has no
    // "Extension" suffix) but carries the xmlns prefix.
    {
        XamlExtensionFixture f;
        XamlType fallback(nullptr, "MyAssembly", "My.Nested.SEG", "MyExt");
        XamlExtension extFallback(&fallback);
        EXPECT_EQ(extFallback.ToString(*f.ctx, *f.holder), "{seg:MyExt}");
    }

    // Gold B2b: the exactly-"Extension" name strips to the empty name.
    {
        XamlExtensionFixture f;
        XamlType type(nullptr, "MyAssembly", "NS", "Extension");
        XamlExtension ext(&type);
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ns:}");
    }

    // Gold B2c: "Extensio" is not the suffix -- kept whole.
    {
        XamlExtensionFixture f;
        XamlType type(nullptr, "MyAssembly", "NS", "Extensio");
        XamlExtension ext(&type);
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ns:Extensio}");
    }
}

TEST(XamlExtensionTest, InitializerShapes)
{
    XamlExtensionFixture f;
    XamlType* toolBar = f.ctx->ResolveType(0xFD63);

    // Gold B3a: one initializer.
    {
        XamlExtension ext(toolBar);
        ext.Initializer = std::vector<XamlObject>{ XamlObject(std::string("a")) };
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar a}");
    }

    // Gold B3b: several initializers, comma-separated.
    {
        XamlExtension ext(toolBar);
        ext.Initializer = std::vector<XamlObject>{ XamlObject(std::string("a")),
            XamlObject(std::string("b")) };
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar a, b}");
    }

    // Gold B3c: an EMPTY initializer array renders no space (the null and
    // empty forms are indistinguishable in the render).
    {
        XamlExtension ext(toolBar);
        ext.Initializer = std::vector<XamlObject>{};
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar}");
    }

    // Gold B3d: a null initializer entry NREs in value.ToString().
    {
        XamlExtension ext(toolBar);
        ext.Initializer = std::vector<XamlObject>{ XamlObject(std::string("a")),
            XamlObject(std::monostate{}), XamlObject(std::string("c")) };
        EXPECT_EQ(ThrowsMessage<std::runtime_error>([&] {
            ext.ToString(*f.ctx, *f.holder);
        }),
            "Object reference not set to an instance of an object.");
    }
}

TEST(XamlExtensionTest, NestedExtensions)
{
    XamlExtensionFixture f;
    XamlType* toolBar = f.ctx->ResolveType(0xFD63);

    // Gold B4a: a nested extension initializer renders inline.
    {
        auto inner = std::make_shared<XamlExtension>(toolBar);
        inner->Initializer =
            std::vector<XamlObject>{ XamlObject(std::string("v")) };
        XamlExtension ext(toolBar);
        ext.Initializer = std::vector<XamlObject>{ XamlObject(inner),
            XamlObject(std::string("tail")) };
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar {ToolBar v}, tail}");
    }

    // Gold B4b: the nested fallback-resolved extension (its own xmlns
    // attribute is added to the shared element).
    {
        auto fallback = std::make_shared<XamlType>(nullptr, "MyAssembly",
            "My.Nested.SEG", "MyExt");
        auto inner = std::make_shared<XamlExtension>(fallback.get());
        inner->Initializer =
            std::vector<XamlObject>{ XamlObject(std::string("v2")) };
        XamlExtension ext(toolBar);
        ext.Initializer = std::vector<XamlObject>{ XamlObject(inner) };
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar {seg:MyExt v2}}");
    }
}

TEST(XamlExtensionTest, NamedArguments)
{
    XamlExtensionFixture f;
    XamlType* toolBar = f.ctx->ResolveType(0xFD63);

    // Gold B5a: the named arguments in insertion order.
    {
        XamlExtension ext(toolBar);
        ext.SetNamedArgument("k1", XamlObject(std::string("v1")));
        ext.SetNamedArgument("k2", XamlObject(std::string("v2")));
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar k1=v1, k2=v2}");
    }

    // Gold B5b: the indexer replaces an existing key's value in place (the
    // position is kept).
    {
        XamlExtension ext(toolBar);
        ext.SetNamedArgument("k1", XamlObject(std::string("v1")));
        ext.SetNamedArgument("k1", XamlObject(std::string("replaced")));
        ext.SetNamedArgument("k2", XamlObject(std::string("v2")));
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar k1=replaced, k2=v2}");
    }

    // Gold B5c: a null named value NREs in value.ToString().
    {
        XamlExtension ext(toolBar);
        ext.SetNamedArgument("k", XamlObject(std::monostate{}));
        EXPECT_EQ(ThrowsMessage<std::runtime_error>([&] {
            ext.ToString(*f.ctx, *f.holder);
        }),
            "Object reference not set to an instance of an object.");
    }
}

TEST(XamlExtensionTest, CombinedInitializerAndNamed)
{
    XamlExtensionFixture f;
    XamlType* toolBar = f.ctx->ResolveType(0xFD63);

    // Gold B6a: the initializer items then the named ones.
    {
        XamlExtension ext(toolBar);
        ext.Initializer = std::vector<XamlObject>{ XamlObject(std::string("a")),
            XamlObject(std::string("b")) };
        ext.SetNamedArgument("k1", XamlObject(std::string("v1")));
        ext.SetNamedArgument("k2", XamlObject(std::string("v2")));
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar a, b, k1=v1, k2=v2}");
    }

    // Gold B6b: an EMPTY initializer array with named arguments -- the named
    // arm writes the leading space (comma never set).
    {
        XamlExtension ext(toolBar);
        ext.Initializer = std::vector<XamlObject>{};
        ext.SetNamedArgument("k1", XamlObject(std::string("v1")));
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar k1=v1}");
    }

    // Gold B7a: a named value can be a nested extension.
    {
        auto inner = std::make_shared<XamlExtension>(toolBar);
        inner->Initializer =
            std::vector<XamlObject>{ XamlObject(std::string("n")) };
        XamlExtension ext(toolBar);
        ext.SetNamedArgument("k", XamlObject(inner));
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ToolBar k={ToolBar n}}");
    }
}

TEST(XamlExtensionTest, DefinitionArmTypes)
{
    XamlExtensionFixture f;

    // Gold B8b: the XmlnsDefinition-arm type (ContentElement over the
    // PresentationCore module -- no in-scope prefix).
    {
        XamlType type(f.fixture.PresentationCore(), kPresentationCoreFullName,
            "System.Windows", "ContentElement");
        XamlExtension ext(&type);
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{ContentElement}");
    }

    // Gold B10a/B10b: another XmlnsDefinition-arm type with and without an
    // in-scope prefix.
    {
        XamlType type(f.fixture.PresentationCore(), kPresentationCoreFullName,
            "System.Windows", "Border");
        XamlExtension ext(&type);
        EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{Border}");
    }
    {
        auto prefixed = std::make_shared<Xml::XElement>(Xml::XName::Get("p"),
            std::make_shared<Xml::XAttribute>(
                Xml::XNamespace::Xmlns() + "pc",
                std::string(ILSpy::BamlDecompiler::XamlContext::KnownNamespace_Presentation)));
        f.root->Add(prefixed);
        XamlType type(f.fixture.PresentationCore(), kPresentationCoreFullName,
            "System.Windows", "Border");
        XamlExtension ext(&type);
        EXPECT_EQ(ext.ToString(*f.ctx, *prefixed), "{pc:Border}");
    }
}

TEST(XamlExtensionTest, FallbackRenderMutatesElement)
{
    XamlExtensionFixture f;

    // Gold B9a/B9b: a fallback-resolved render over a FRESH type adds the
    // xmlns:seg declaration to the context element and renders the prefixed
    // name.
    XamlType type(nullptr, "MyAssembly", "My.Nested.SEG", "MyExt");
    XamlExtension ext(&type);
    ext.Initializer = std::vector<XamlObject>{ XamlObject(std::string("x")) };
    EXPECT_EQ(ext.ToString(*f.ctx, *f.holder), "{seg:MyExt x}");
    EXPECT_EQ(f.holder->ToString(),
        "<e xmlns:seg=\"clr-namespace:My.Nested.SEG;assembly=MyAssembly\" />");
}

} // namespace
