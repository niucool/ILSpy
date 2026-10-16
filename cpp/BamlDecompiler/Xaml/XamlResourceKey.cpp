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

// Port of ICSharpCode.BamlDecompiler/Xaml/XamlResourceKey.cs (see
// XamlResourceKey.hpp for the porting decisions).

#include "BamlDecompiler/Xaml/XamlResourceKey.hpp"

#include <any>
#include <stdexcept>

namespace ILSpy::BamlDecompiler::Xaml {

namespace {

// The .NET NullReferenceException message (the port's mapping convention).
constexpr const char* kNullReferenceMessage =
    "Object reference not set to an instance of an object.";

} // namespace

std::shared_ptr<XamlResourceKey> XamlResourceKey::Create(Baml::BamlNode& node)
{
    std::shared_ptr<XamlResourceKey> key(new XamlResourceKey(node));
    // The C# ctor's `X.Annotation = this` assignments, holding the owning
    // shared_ptr (the GC roots the key through the annotations -- a
    // ctor-local alias would dangle once Create's return dies).
    for (Baml::BamlNode* target : key->annotateTargets_)
        target->Annotation = key;
    return key;
}

XamlResourceKey::XamlResourceKey(Baml::BamlNode& node)
{
    KeyNode = &node;

    // The C# `keyRecord = (IBamlDeferRecord)node.Record`: a null record
    // (a hand-built headerless block) keeps the C#'s null-cast shape and
    // NREs at the Record deref below; a non-defer record is the C#
    // InvalidCastException (the fixed-message divergence -- see the header).
    Baml::BamlRecord* target = nullptr;
    if (Baml::BamlRecord* record = node.Record()) {
        Baml::IBamlDeferRecord* keyRecord =
            dynamic_cast<Baml::IBamlDeferRecord*>(record);
        if (keyRecord == nullptr)
            throw std::runtime_error(
                "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<record>' "
                "to type 'ICSharpCode.BamlDecompiler.Baml.IBamlDeferRecord'.");
        // The C# `keyRecord.Record.Type` -- the resolved defer target, null
        // before the document's defer resolution.
        target = keyRecord->Record();
    }
    if (target == nullptr)
        throw std::runtime_error(kNullReferenceMessage);

    // The C# `keyRecord.Record.Type == BamlRecordType.ElementEnd` arm: the
    // key annotates its parent and itself (the Debug.Assert on the parent's
    // footer is compiled out of the release assembly).
    if (target->Type() == Baml::BamlRecordType::ElementEnd) {
        if (node.Parent == nullptr)
            throw std::runtime_error(kNullReferenceMessage);
        annotateTargets_.push_back(node.Parent);
        annotateTargets_.push_back(&node);
        return;
    }

    // The C# `keyRecord.Record.Type != BamlRecordType.ElementStart &&
    // node.Parent.Type == BamlRecordType.ElementStart` arm (the
    // short-circuit keeps the ElementStart target from reading the parent
    // here -- that shape's null parent NREs at the children walk below).
    if (target->Type() != Baml::BamlRecordType::ElementStart) {
        if (node.Parent == nullptr)
            throw std::runtime_error(kNullReferenceMessage);
        if (node.Parent->Type() == Baml::BamlRecordType::ElementStart) {
            annotateTargets_.push_back(node.Parent);
            annotateTargets_.push_back(&node);
            return;
        }
        // The Debug.WriteLine ("must be attached to ElementStart") is
        // compiled out of the release assembly: fall through to the walk.
    }

    // The C# children walk: the child whose record IS the resolved defer
    // target is the value element the key annotates (reference equality --
    // a block node's Record is its header). The not-found
    // Debug.WriteLine is compiled out: nothing is annotated.
    if (node.Parent == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    for (const auto& child : node.Parent->Children) {
        if (child->Record() != target)
            continue;

        annotateTargets_.push_back(child.get());
        annotateTargets_.push_back(&node);
        return;
    }
}

std::shared_ptr<XamlResourceKey> XamlResourceKey::FindKeyInSiblings(Baml::BamlNode& node)
{
    // The C# `node.Parent.Children` read NREs for a null parent.
    if (node.Parent == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    const auto& children = node.Parent->Children;

    // The C# `children.IndexOf(node)` -- reference equality, -1 when the
    // node is not among its parent's children (the loop then never runs).
    std::ptrdiff_t index = -1;
    for (std::size_t i = 0; i < children.size(); i++) {
        if (children[i].get() == &node) {
            index = static_cast<std::ptrdiff_t>(i);
            break;
        }
    }

    // The C# scans BACKWARD from the node's own position.
    for (std::ptrdiff_t i = index; i >= 0; i--) {
        if (const auto* key = std::any_cast<std::shared_ptr<XamlResourceKey>>(
                &children[static_cast<std::size_t>(i)]->Annotation))
            return *key;
    }
    return nullptr;
}

std::shared_ptr<XamlResourceKey> XamlResourceKey::FindKeyInAncestors(Baml::BamlNode& node)
{
    Baml::BamlNode* found = nullptr;
    return FindKeyInAncestors(node, found);
}

std::shared_ptr<XamlResourceKey> XamlResourceKey::FindKeyInAncestors(Baml::BamlNode& node,
    Baml::BamlNode*& found)
{
    Baml::BamlNode* n = &node;
    do {
        if (const auto* key = std::any_cast<std::shared_ptr<XamlResourceKey>>(
                &n->Annotation)) {
            found = n;
            return *key;
        }
        n = n->Parent;
    } while (n != nullptr);
    found = nullptr;
    return nullptr;
}

} // namespace ILSpy::BamlDecompiler::Xaml
