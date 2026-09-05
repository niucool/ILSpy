// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files ("the Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Implementation of the Handlers/Blocks port (the block-header handlers).

#include "BamlDecompiler/Baml/BamlNode.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Blocks.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/Xaml/XamlResourceKey.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <any>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::BamlDecompiler::Handlers {

namespace {

// The .NET NullReferenceException message (the port's mapping convention).
constexpr const char* kNullReferenceMessage =
    "Object reference not set to an instance of an object.";

// The C# `(BamlBlockNode)node` cast: only a leaf node can fail it (the
// block-opening record types always parse as blocks), so the source type in
// the .NET InvalidCastException message is exact.
Baml::BamlBlockNode& BlockNodeOf(Baml::BamlNode& node)
{
    auto* blockNode = dynamic_cast<Baml::BamlBlockNode*>(&node);
    if (blockNode == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.BamlRecordNode' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.BamlBlockNode'.");
    return *blockNode;
}

} // namespace

// ===== DocumentHandler ======================================================

Baml::BamlRecordType DocumentHandler::Type() const
{
    return Baml::BamlRecordType::DocumentStart;
}

std::unique_ptr<BamlElement> DocumentHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    // The C# body never touches the parent (the XamlDecompiler.Decompile
    // root call passes null).
    (void)parent;
    // The C# `new BamlElement(node)` + the pseudo-named root element.
    auto doc = std::make_unique<BamlElement>(&node);
    doc->Xaml = std::make_shared<Xml::XElement>(ctx.GetPseudoName("Document"));
    // The C# `HandlerMap.ProcessChildren(ctx, (BamlBlockNode)node, doc)`.
    HandlerMap::ProcessChildren(ctx, BlockNodeOf(node), *doc);
    return doc;
}

// ===== ElementHandler =======================================================

Baml::BamlRecordType ElementHandler::Type() const
{
    return Baml::BamlRecordType::ElementStart;
}

std::unique_ptr<BamlElement> ElementHandler::Translate(XamlContext& ctx,
    Baml::BamlNode& node, BamlElement* parent)
{
    // The C# `var record = (ElementStartRecord)((BamlBlockNode)node).Header`
    // -- a headerless block keeps the C#'s null-cast shape and NREs at the
    // record read below; a non-ElementStart header is the InvalidCastException
    // with the fixed '<record>' placeholder (unreachable through a parsed
    // tree: an ElementStart-typed block always carries the matching header).
    Baml::BamlRecord* header = BlockNodeOf(node).Header;
    if (header == nullptr)
        throw std::runtime_error(kNullReferenceMessage);
    auto* record = dynamic_cast<Baml::ElementStartRecord*>(header);
    if (record == nullptr)
        throw std::runtime_error(
            "Unable to cast object of type 'ICSharpCode.BamlDecompiler.Baml.<record>' "
            "to type 'ICSharpCode.BamlDecompiler.Baml.ElementStartRecord'.");
    Baml::BamlBlockNode& blockNode = static_cast<Baml::BamlBlockNode&>(node);

    auto doc = std::make_unique<BamlElement>(&node);
    // The C# `ctx.ResolveType(record.TypeId)` -- the owning form: the element
    // annotation below is the type's only root once the context is gone
    // (the XamlContext cache hands the shared handle out).
    std::shared_ptr<Xaml::XamlType> elemType = ctx.ResolveTypeOwning(record->TypeId);
    doc->Xaml = std::make_shared<Xml::XElement>(elemType->ToXName(ctx));

    doc->Xaml.Element->AddAnnotation(elemType);
    // The C# `parent.Xaml.Element.Add(...)`: the null-parent deref NREs.
    if (parent == nullptr || !parent->Xaml.Element)
        throw std::runtime_error(kNullReferenceMessage);
    parent->Xaml.Element->Add(doc->Xaml.Element);

    HandlerMap::ProcessChildren(ctx, blockNode, *doc);
    // The C# `if (node.Annotation is XamlResourceKey key && key.KeyNode.Record
    // != node.Record)` -- the key handlers (DefAttributeStringHandler,
    // DefAttributeTypeHandler, KeyElementStartHandler) annotate the key node
    // with the OWNING shared_ptr (the XamlResourceKey.Create convention).
    if (auto* keyAny = std::any_cast<std::shared_ptr<Xaml::XamlResourceKey>>(
            &node.Annotation)) {
        std::shared_ptr<Xaml::XamlResourceKey> key = *keyAny;
        if (key->KeyNode != nullptr && key->KeyNode->Record() != node.Record()) {
            // The C# `(IDeferHandler)HandlerMap.LookupHandler(...)`: a null
            // handler NREs at the TranslateDefer call (the C# null cast
            // succeeds); a non-defer handler is the InvalidCastException with
            // the fixed '<handler>' placeholder. Both arms are unreachable
            // through a real XamlResourceKey (all three key-record handlers
            // implement IDeferHandler).
            IHandler* handler = HandlerMap::LookupHandler(
                key->KeyNode->Record()->Type());
            if (handler == nullptr)
                throw std::runtime_error(kNullReferenceMessage);
            auto* deferHandler = dynamic_cast<IDeferHandler*>(handler);
            if (deferHandler == nullptr)
                throw std::runtime_error(
                    "Unable to cast object of type '<handler>' to type "
                    "'ICSharpCode.BamlDecompiler.IDeferHandler'.");
            std::unique_ptr<BamlElement> keyElem =
                deferHandler->TranslateDefer(ctx, *key->KeyNode, doc.get());
            // The C# `doc.Children.Add(keyElem); keyElem.Parent = doc;` -- the
            // Add does not assign the back-pointer (the BamlElement
            // convention); a null TranslateDefer result NREs at the Parent
            // write.
            BamlElement* rawKeyElem = keyElem.get();
            doc->AddChild(std::move(keyElem));
            if (rawKeyElem == nullptr)
                throw std::runtime_error(kNullReferenceMessage);
            rawKeyElem->Parent = doc.get();
        }
    }

    // The C# pair that attaches the xmlns: ResolveNamespace mutates the type
    // (picking the namespace and appending the xmlns declaration), then the
    // element re-renders its name.
    elemType->ResolveNamespace(*doc->Xaml.Element, ctx);
    doc->Xaml.Element->Name(elemType->ToXName(ctx));
    return doc;
}

} // namespace ILSpy::BamlDecompiler::Handlers
