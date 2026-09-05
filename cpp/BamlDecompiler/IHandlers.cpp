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

// Implementation of the IHandler/IDeferHandler/HandlerMap port
// (ICSharpCode.BamlDecompiler/IHandlers.cs). The header carries the porting
// decisions; this file is the dispatch machinery.

#include "BamlDecompiler/Baml/BamlNode.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/Handlers/Blocks.hpp"
#include "BamlDecompiler/Handlers/Records.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/XmlnsDictionary.hpp"
#include "BamlDecompiler/XamlContext.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ILSpy::BamlDecompiler {

namespace {

// The C# `static readonly Dictionary<BamlRecordType, IHandler> handlers`
// plus its populate-once state. A function-local static: the registry is
// built at first use (the C# static ctor's timing) and the instances are
// owned here (the C# GC holding the singletons). Like the C# static field,
// the registry is NOT synchronized for concurrent mutation -- the single
// writer is the lazy population (the function-local static initialization
// itself is thread-safe); `InstallHandler`/`ClearHandlers` are test and
// embedding seams no translation run calls concurrently.
struct HandlerRegistry {
    std::unordered_map<Baml::BamlRecordType, std::unique_ptr<IHandler>> map;
    bool builtinLoaded = false;
};

HandlerRegistry& Registry()
{
    static HandlerRegistry registry;
    return registry;
}

} // namespace

// The C# static ctor's reflection walk: the explicit manifest of every
// ported handler class, one construction line each (the gold-registry's
// ascending record-type order -- the C# Assembly.GetTypes() order is an
// implementation detail the dictionary registry need not reproduce; the
// 37-row gold inventory the manifest must eventually reproduce is pinned in
// HandlerMap_Test.cpp, dumped from the shipped assembly's registry through
// the C:/temp-probe/HandlerMapProbe probe).
std::vector<std::unique_ptr<IHandler>> HandlerMap::CreateBuiltinHandlers()
{
    std::vector<std::unique_ptr<IHandler>> handlers;
    handlers.push_back(std::make_unique<Handlers::DocumentHandler>());
    handlers.push_back(std::make_unique<Handlers::ElementHandler>());
    handlers.push_back(std::make_unique<Handlers::PropertyHandler>());
    handlers.push_back(std::make_unique<Handlers::PropertyWithConverterHandler>());
    handlers.push_back(std::make_unique<Handlers::PropertyComplexHandler>());
    handlers.push_back(std::make_unique<Handlers::PropertyArrayHandler>());
    handlers.push_back(std::make_unique<Handlers::PropertyListHandler>());
    handlers.push_back(std::make_unique<Handlers::PropertyDictionaryHandler>());
    handlers.push_back(std::make_unique<Handlers::ConstructorParametersStartHandler>());
    handlers.push_back(std::make_unique<Handlers::ConstructorParameterTypeHandler>());
    handlers.push_back(std::make_unique<Handlers::TextHandler>());
    handlers.push_back(std::make_unique<Handlers::TextWithConverterHandler>());
    handlers.push_back(std::make_unique<Handlers::DefAttributeHandler>());
    handlers.push_back(std::make_unique<Handlers::PIMappingHandler>());
    handlers.push_back(std::make_unique<Handlers::AssemblyInfoHandler>());
    handlers.push_back(std::make_unique<Handlers::TypeInfoHandler>());
    handlers.push_back(std::make_unique<Handlers::TypeSerializerInfoHandler>());
    handlers.push_back(std::make_unique<Handlers::AttributeInfoHandler>());
    handlers.push_back(std::make_unique<Handlers::DeferableContentStartHandler>());
    handlers.push_back(std::make_unique<Handlers::DefAttributeStringHandler>());
    handlers.push_back(std::make_unique<Handlers::DefAttributeTypeHandler>());
    handlers.push_back(std::make_unique<Handlers::KeyElementStartHandler>());
    handlers.push_back(std::make_unique<Handlers::ConnectionIdHandler>());
    handlers.push_back(std::make_unique<Handlers::ContentPropertyHandler>());
    handlers.push_back(std::make_unique<Handlers::TextWithIdHandler>());
    handlers.push_back(std::make_unique<Handlers::PresentationOptionsAttributeHandler>());
    handlers.push_back(std::make_unique<Handlers::LineNumberAndPositionHandler>());
    handlers.push_back(std::make_unique<Handlers::LinePositionHandler>());
    return handlers;
}

void HandlerMap::InstallHandler(std::unique_ptr<IHandler> handler)
{
    // The C# `handlers.Add(handler.Type, handler)`: the key is read off the
    // instance first, and a duplicate throws the .NET duplicate-key
    // ArgumentException (the invariant the reflection walk relies on: no
    // two handler classes claim the same record type).
    Baml::BamlRecordType type = handler->Type();
    auto& map = Registry().map;
    if (map.find(type) != map.end())
        throw std::invalid_argument(
            "An item with the same key has already been added. Key: "
            + Baml::RecordTypeName(type));
    map.emplace(type, std::move(handler));
}

void HandlerMap::ClearHandlers()
{
    auto& registry = Registry();
    registry.map.clear();
    registry.builtinLoaded = false;
}

IHandler* HandlerMap::LookupHandler(Baml::BamlRecordType type)
{
    // The lazy population (the C# static ctor runs on the first
    // LookupHandler/ProcessChildren touch): every manifest row routes through
    // InstallHandler EXCEPT the record types an explicit install has
    // already claimed (the test/embedding-seam precedence -- see the
    // header's porting decisions; manifest-internal duplicates still throw
    // through InstallHandler, the C# reflection-walk invariant).
    HandlerRegistry& registry = Registry();
    if (!registry.builtinLoaded) {
        registry.builtinLoaded = true;
        for (auto& handler : CreateBuiltinHandlers()) {
            if (registry.map.find(handler->Type()) != registry.map.end())
                continue;
            InstallHandler(std::move(handler));
        }
    }
    auto& map = registry.map;
    auto it = map.find(type);
    // The C# `handlers.ContainsKey(type) ? handlers[type] : null` -- the
    // release form (the DEBUG-only NotSupportedException arm is compiled
    // out of the shipped assembly).
    return it == map.end() ? nullptr : it->second.get();
}

void HandlerMap::ProcessChildren(XamlContext& ctx, Baml::BamlBlockNode& node,
    BamlElement& nodeElem)
{
    ctx.XmlNs().PushScope(&nodeElem);
    if (nodeElem.Xaml.Element)
        // The scope annotation: the shared_ptr copy roots the scope
        // through the element (the C# GC reference), and
        // LookupNamespaceFromPrefix-style readers fish it back out with
        // Annotation<std::shared_ptr<XmlnsScope>>().
        nodeElem.Xaml.Element->AddAnnotation(ctx.XmlNs().CurrentScope());
    for (const auto& child : node.Children) {
        IHandler* handler = LookupHandler(child->Type());
        if (handler == nullptr) {
            // The C# `Debug.WriteLine("BAML Handler {0} not implemented.",
            // child.Type)` -- a release no-op; the child is skipped.
            continue;
        }
        std::unique_ptr<BamlElement> elem = handler->Translate(ctx, *child, &nodeElem);
        if (elem != nullptr) {
            // The C# `nodeElem.Children.Add(elem); elem.Parent = nodeElem;`
            // -- the Add does not assign the back-pointer, so the port
            // appends through AddChild and sets Parent by hand.
            BamlElement* rawElem = elem.get();
            nodeElem.AddChild(std::move(elem));
            rawElem->Parent = &nodeElem;
        }
        // The C# `ctx.CancellationToken.ThrowIfCancellationRequested()` --
        // the token deferral (the XamlContext convention).
    }
    ctx.XmlNs().PopScope();
}

} // namespace ILSpy::BamlDecompiler
