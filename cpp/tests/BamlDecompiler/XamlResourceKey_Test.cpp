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

// Tests for the XamlResourceKey port (ICSharpCode.BamlDecompiler/Xaml/
// XamlResourceKey.cs) -- every expectation is gold-pinned against the REAL
// internal class from the installed ICSharpCode.BamlDecompiler.dll, driven
// through reflection by the gold probe (C:/temp-probe/XamlExtProbe/Program.cs)
// over crafted BamlDocuments parsed by the REAL BamlNode.Parse:
//  * Create's ctor arms: the ElementEnd target annotating the PARENT and the
//    node (C1), the non-ElementStart target inside an ElementStart parent
//    (C2), the ElementStart target resolving through the children walk to
//    the value element (C3a), a non-ElementStart target under a
//    non-ElementStart parent finding its sibling (C3b), and the not-found
//    target annotating NOTHING while still returning the key (C3c);
//  * the annotation identity and liveness: the node annotations hold the
//    OWNING instance (C1b -- the C# GC roots the key through the
//    annotations), and every Create returns a fresh instance (C2's
//    sameAsC1=False);
//  * the exception arms: the non-defer-header cast (C4 -- the port's
//    fixed-message divergence, the type checked), the unresolved defer
//    target's NRE (C5), and the null parent NREs before arm 2's parent read
//    (C5b) and at arm 1's annotation write (C5c);
//  * FindKeyInSiblings: the node ITSELF annotated (C6a), the backward-only
//    scan skipping a key positioned AFTER the node, the no-annotation null
//    (C6c), and the not-in-children node (IndexOf -1, C6d);
//  * FindKeyInAncestors: the ancestor key (C7a), the out param reporting the
//    carrying node (C7b -- not observable through the probe's reflection
//    Invoke; the port's direct call pins it), the nothing-found null (C7c),
//    the node ITSELF annotated (C7d), and a non-key annotation skipped
//    (C7e).

#include "BamlDecompiler/Baml/BamlNode.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "BamlDecompiler/Xaml/XamlResourceKey.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace Baml = ILSpy::BamlDecompiler::Baml;
using ILSpy::BamlDecompiler::Xaml::XamlResourceKey;

// The crafted probe document:
//   DocumentStart
//     ElementStart(T1) block: [Text(hello)] ElementEnd
//     KeyElementStart block: [DefAttributeKeyString, Text] KeyElementEnd
//   DocumentEnd
// (the record pointers stay alive through the document; the deferred targets
// are set by hand before each Create -- the probe's SetDeferRecord).
struct CraftedTree {
    Baml::BamlDocument doc;
    std::unique_ptr<Baml::BamlBlockNode> tree;
    Baml::BamlBlockNode* root = nullptr;
    Baml::BamlBlockNode* elementBlock = nullptr;
    Baml::BamlBlockNode* keyBlock = nullptr;
    Baml::BamlNode* textInElement = nullptr;
    Baml::BamlNode* defAttr = nullptr;
    Baml::BamlNode* textInKey = nullptr;
    Baml::ElementStartRecord* elementStartRecord = nullptr;
    Baml::ElementEndRecord* elementEndRecord = nullptr;
    Baml::KeyElementStartRecord* keyHeader = nullptr;
    Baml::DefAttributeKeyStringRecord* defAttrRecord = nullptr;
    Baml::TextRecord* textInKeyRecord = nullptr;

    // The parse-succeeded guard the tests assert on first (a failed parse
    // leaves the node members null).
    bool Ok() const
    {
        return root != nullptr && elementBlock != nullptr && keyBlock != nullptr
            && textInElement != nullptr && defAttr != nullptr && textInKey != nullptr;
    }

    CraftedTree()
    {
        auto documentStart = std::make_unique<Baml::DocumentStartRecord>();
        auto elementStart = std::make_unique<Baml::ElementStartRecord>();
        elementStart->TypeId = 0xFD63;
        auto text1 = std::make_unique<Baml::TextRecord>();
        text1->Value = "hello";
        auto elementEnd = std::make_unique<Baml::ElementEndRecord>();
        auto keyStart = std::make_unique<Baml::KeyElementStartRecord>();
        keyStart->TypeId = 0xFD63;
        auto defAttrRec = std::make_unique<Baml::DefAttributeKeyStringRecord>();
        defAttrRec->ValueId = 1;
        defAttrRec->Shared = true;
        defAttrRec->SharedSet = true;
        auto text2 = std::make_unique<Baml::TextRecord>();
        text2->Value = "v2";
        auto keyEnd = std::make_unique<Baml::KeyElementEndRecord>();
        auto documentEnd = std::make_unique<Baml::DocumentEndRecord>();

        elementStartRecord = elementStart.get();
        elementEndRecord = elementEnd.get();
        keyHeader = keyStart.get();
        defAttrRecord = defAttrRec.get();
        textInKeyRecord = text2.get();

        doc.Add(std::move(documentStart));
        doc.Add(std::move(elementStart));
        doc.Add(std::move(text1));
        doc.Add(std::move(elementEnd));
        doc.Add(std::move(keyStart));
        doc.Add(std::move(defAttrRec));
        doc.Add(std::move(text2));
        doc.Add(std::move(keyEnd));
        doc.Add(std::move(documentEnd));

        auto parsed = Baml::BamlNode::Parse(doc);
        if (parsed == nullptr || parsed->Children.size() != 2u)
            return; // a malformed parse leaves the node members null; the
                    // tests' ASSERT_NE(t.root, nullptr) reports it
        root = parsed.get();
        elementBlock = static_cast<Baml::BamlBlockNode*>(root->Children[0].get());
        keyBlock = static_cast<Baml::BamlBlockNode*>(root->Children[1].get());
        if (elementBlock->Children.size() == 1u)
            textInElement = elementBlock->Children[0].get();
        if (keyBlock->Children.size() == 2u) {
            defAttr = keyBlock->Children[0].get();
            textInKey = keyBlock->Children[1].get();
        }
        tree = std::move(parsed);
    }
};

// The annotation read-back: the C# `node.Annotation is XamlResourceKey`
// cast (the port stores the owning shared_ptr).
std::shared_ptr<XamlResourceKey> KeyOf(const Baml::BamlNode& node)
{
    if (const auto* key = std::any_cast<std::shared_ptr<XamlResourceKey>>(
            &node.Annotation))
        return *key;
    return nullptr;
}

TEST(XamlResourceKeyTest, ElementEndTargetAnnotatesParentAndNode)
{
    // Gold C1: the resolved defer target IS an ElementEnd record -- the key
    // annotates its parent and itself; KeyNode is the node and
    // StaticResources starts empty.
    CraftedTree t;
    ASSERT_TRUE(t.Ok());
    t.keyHeader->SetRecord(t.elementEndRecord);
    auto key = XamlResourceKey::Create(*t.keyBlock);
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->KeyNode, t.keyBlock);
    EXPECT_TRUE(key->StaticResources.empty());
    EXPECT_EQ(key->KeyElement, nullptr);
    EXPECT_EQ(KeyOf(*t.root), key);
    EXPECT_EQ(KeyOf(*t.keyBlock), key);
    EXPECT_EQ(KeyOf(*t.elementBlock), nullptr);
}

TEST(XamlResourceKeyTest, AnnotationOutlivesTheReturnedReference)
{
    // Gold C1b: the C# GC roots the key through the node annotations (the
    // probe re-read the annotation after dropping the local and collecting)
    // -- the port's annotation holds the OWNING shared_ptr.
    CraftedTree t;
    ASSERT_TRUE(t.Ok());
    t.keyHeader->SetRecord(t.elementEndRecord);
    std::weak_ptr<XamlResourceKey> weak = XamlResourceKey::Create(*t.keyBlock);
    auto fromAnnotation = KeyOf(*t.keyBlock);
    ASSERT_NE(fromAnnotation, nullptr);
    EXPECT_FALSE(weak.expired());
    EXPECT_EQ(fromAnnotation->KeyNode, t.keyBlock);
}

TEST(XamlResourceKeyTest, NonElementStartTargetInsideElementStartParent)
{
    // Gold C2: a record node whose target is neither ElementEnd nor
    // ElementStart, parented by an ElementStart block -- the parent and the
    // node are annotated; a FRESH key instance per Create.
    Baml::BamlDocument doc;
    auto documentStart = std::make_unique<Baml::DocumentStartRecord>();
    auto elementStart = std::make_unique<Baml::ElementStartRecord>();
    elementStart->TypeId = 0xFD63;
    auto defAttrRec = std::make_unique<Baml::DefAttributeKeyStringRecord>();
    auto elementEnd = std::make_unique<Baml::ElementEndRecord>();
    auto documentEnd = std::make_unique<Baml::DocumentEndRecord>();
    defAttrRec->ValueId = 0;
    Baml::DefAttributeKeyStringRecord* defAttrRecord = defAttrRec.get();
    doc.Add(std::move(documentStart));
    doc.Add(std::move(elementStart));
    doc.Add(std::move(defAttrRec));
    doc.Add(std::move(elementEnd));
    doc.Add(std::move(documentEnd));

    // The target: a Text record that is NOT part of the document (the
    // probe's fresh text2Rec).
    auto target = std::make_unique<Baml::TextRecord>();
    target->Value = "t";
    defAttrRecord->SetRecord(target.get());

    auto root = Baml::BamlNode::Parse(doc);
    ASSERT_EQ(root->Children.size(), 1u);
    auto* elementBlock = static_cast<Baml::BamlBlockNode*>(root->Children[0].get());
    ASSERT_EQ(elementBlock->Children.size(), 1u);
    Baml::BamlNode* defAttr = elementBlock->Children[0].get();

    auto key = XamlResourceKey::Create(*defAttr);
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->KeyNode, defAttr);
    EXPECT_EQ(KeyOf(*elementBlock), key);
    EXPECT_EQ(KeyOf(*defAttr), key);
    EXPECT_EQ(KeyOf(*root), nullptr);
}

TEST(XamlResourceKeyTest, ElementStartTargetResolvesThroughChildrenWalk)
{
    // Gold C3a: the target IS the ElementStart record of a sibling block --
    // the children walk finds the block node (its Record is its header) and
    // annotates it.
    CraftedTree t;
    ASSERT_TRUE(t.Ok());
    t.keyHeader->SetRecord(t.elementStartRecord);
    auto key = XamlResourceKey::Create(*t.keyBlock);
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(KeyOf(*t.elementBlock), key);
    EXPECT_EQ(KeyOf(*t.keyBlock), key);
    EXPECT_EQ(KeyOf(*t.root), nullptr);
}

TEST(XamlResourceKeyTest, SiblingTargetUnderNonElementStartParent)
{
    // Gold C3b: a non-ElementStart target under the KeyElementStart block
    // (not ElementStart) -- the arm-2 condition fails and the children walk
    // finds the Text sibling.
    CraftedTree t;
    ASSERT_TRUE(t.Ok());
    t.defAttrRecord->SetRecord(t.textInKeyRecord);
    auto key = XamlResourceKey::Create(*t.defAttr);
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(KeyOf(*t.textInKey), key);
    EXPECT_EQ(KeyOf(*t.defAttr), key);
    EXPECT_EQ(KeyOf(*t.keyBlock), nullptr);
}

TEST(XamlResourceKeyTest, NotFoundTargetAnnotatesNothing)
{
    // Gold C3c: the target is not among the parent's children -- the key is
    // returned (non-null) but NOTHING is annotated (the Debug.WriteLine is
    // compiled out of the release assembly).
    CraftedTree t;
    ASSERT_TRUE(t.Ok());
    t.defAttrRecord->SetRecord(t.textInElement->Record());
    auto key = XamlResourceKey::Create(*t.defAttr);
    ASSERT_NE(key, nullptr);
    EXPECT_EQ(key->KeyNode, t.defAttr);
    EXPECT_EQ(KeyOf(*t.defAttr), nullptr);
    EXPECT_EQ(KeyOf(*t.keyBlock), nullptr);
    EXPECT_EQ(KeyOf(*t.textInKey), nullptr);
}

TEST(XamlResourceKeyTest, NonDeferHeaderThrows)
{
    // Gold C4: Create over a block whose header implements no defer
    // interface -- the C# InvalidCastException (the port's fixed-message
    // divergence: the dynamic C# type name is not rendered).
    CraftedTree t;
    ASSERT_TRUE(t.Ok());
    std::string message;
    try {
        XamlResourceKey::Create(*t.elementBlock);
    } catch (const std::runtime_error& ex) {
        message = ex.what();
    }
    EXPECT_NE(message.find("IBamlDeferRecord"), std::string::npos);
}

TEST(XamlResourceKeyTest, NullParentAndNullTargetThrow)
{
    CraftedTree t;
    ASSERT_TRUE(t.Ok());

    // Gold C5: a fresh header with no resolved defer target -- the NRE at
    // keyRecord.Record.Type (the target is null).
    {
        auto freshBlock = std::make_unique<Baml::BamlBlockNode>();
        auto freshHeader = std::make_unique<Baml::KeyElementStartRecord>();
        freshBlock->Header = freshHeader.get();
        // Parent stays null.
        EXPECT_THROW(XamlResourceKey::Create(*freshBlock), std::runtime_error);

        // Gold C5b: the target set, the parent null -- the NRE at arm 2's
        // node.Parent read.
        freshHeader->SetRecord(t.textInKeyRecord);
        EXPECT_THROW(XamlResourceKey::Create(*freshBlock), std::runtime_error);

        // Gold C5c: the target is an ElementEnd record, the parent null --
        // the NRE at arm 1's annotation write.
        freshHeader->SetRecord(t.elementEndRecord);
        EXPECT_THROW(XamlResourceKey::Create(*freshBlock), std::runtime_error);
    }

    // The exact .NET message for all three arms.
    {
        auto freshBlock = std::make_unique<Baml::BamlBlockNode>();
        auto freshHeader = std::make_unique<Baml::KeyElementStartRecord>();
        freshBlock->Header = freshHeader.get();
        std::string message;
        try {
            XamlResourceKey::Create(*freshBlock);
        } catch (const std::runtime_error& ex) {
            message = ex.what();
        }
        EXPECT_EQ(message, "Object reference not set to an instance of an object.");
    }
}

TEST(XamlResourceKeyTest, FindKeyInSiblingsBackwardScan)
{
    CraftedTree t;
    ASSERT_TRUE(t.Ok());

    // Gold C6a: the node ITSELF carries the key annotation.
    t.keyHeader->SetRecord(t.elementEndRecord);
    auto key = XamlResourceKey::Create(*t.keyBlock);
    EXPECT_EQ(XamlResourceKey::FindKeyInSiblings(*t.keyBlock), key);

    // The backward-only scan: the key sits AFTER the element block, so
    // finding FROM the element block misses it (the scan runs from the
    // node's own position DOWN to index 0).
    EXPECT_EQ(XamlResourceKey::FindKeyInSiblings(*t.elementBlock), nullptr);

    // Gold C6c: no annotations at all -- null.
    t.keyBlock->Annotation = std::any();
    t.root->Annotation = std::any();
    EXPECT_EQ(XamlResourceKey::FindKeyInSiblings(*t.keyBlock), nullptr);

    // Gold C6d: a node not among its parent's children (IndexOf -1) -- the
    // loop never runs and the result is null.
    auto orphan = std::make_unique<Baml::BamlRecordNode>(t.textInKeyRecord);
    orphan->Parent = t.root;
    EXPECT_EQ(XamlResourceKey::FindKeyInSiblings(*orphan), nullptr);

    // A null parent NREs (the C# node.Parent.Children read).
    orphan->Parent = nullptr;
    EXPECT_THROW(XamlResourceKey::FindKeyInSiblings(*orphan), std::runtime_error);
}

TEST(XamlResourceKeyTest, FindKeyInAncestorsWalk)
{
    CraftedTree t;
    ASSERT_TRUE(t.Ok());
    t.keyHeader->SetRecord(t.elementEndRecord);
    auto key = XamlResourceKey::Create(*t.keyBlock);
    t.root->Annotation = std::any(); // keep only the keyBlock annotation

    // Gold C7a: walking up from the text inside the key block finds the key
    // at the key block.
    EXPECT_EQ(XamlResourceKey::FindKeyInAncestors(*t.textInKey), key);

    // Gold C7b (the port's direct call): the out param reports the carrying
    // node.
    Baml::BamlNode* found = nullptr;
    EXPECT_EQ(XamlResourceKey::FindKeyInAncestors(*t.textInKey, found), key);
    EXPECT_EQ(found, t.keyBlock);

    // Gold C7c: no key anywhere -- null and found null.
    t.keyBlock->Annotation = std::any();
    EXPECT_EQ(XamlResourceKey::FindKeyInAncestors(*t.textInKey), nullptr);
    found = reinterpret_cast<Baml::BamlNode*>(1);
    EXPECT_EQ(XamlResourceKey::FindKeyInAncestors(*t.textInKey, found), nullptr);
    EXPECT_EQ(found, nullptr);

    // Gold C7d: the node ITSELF annotated -- found at the start node.
    t.textInKey->Annotation = key;
    EXPECT_EQ(XamlResourceKey::FindKeyInAncestors(*t.textInKey), key);
    EXPECT_EQ(XamlResourceKey::FindKeyInAncestors(*t.textInKey, found), key);
    EXPECT_EQ(found, t.textInKey);

    // Gold C7e: a non-key annotation above is skipped.
    t.textInKey->Annotation = std::any();
    t.root->Annotation = std::string("not-a-key");
    EXPECT_EQ(XamlResourceKey::FindKeyInAncestors(*t.textInKey), nullptr);
    t.root->Annotation = std::any();
}

TEST(XamlResourceKeyTest, StaticResourcesCollect)
{
    // The `IList<BamlNode> StaticResources` surface the
    // StaticResourceStart/OptimizedStaticResource handlers append to
    // (non-owning -- the block tree owns the nodes).
    CraftedTree t;
    ASSERT_TRUE(t.Ok());
    t.keyHeader->SetRecord(t.elementEndRecord);
    auto key = XamlResourceKey::Create(*t.keyBlock);
    key->StaticResources.push_back(t.textInKey);
    key->StaticResources.push_back(t.defAttr);
    ASSERT_EQ(key->StaticResources.size(), 2u);
    EXPECT_EQ(key->StaticResources[0], t.textInKey);
    EXPECT_EQ(key->StaticResources[1], t.defAttr);
}

} // namespace
