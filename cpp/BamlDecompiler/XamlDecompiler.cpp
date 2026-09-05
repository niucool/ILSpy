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

// The Decompose body of the XamlDecompiler port (the header carries the
// porting decisions): the static rewrite-pass array plus the byte-span
// Decompile composition.

#include "BamlDecompiler/XamlDecompiler.hpp"

#include "BamlDecompiler/Baml/BamlReader.hpp"
#include "BamlDecompiler/BamlElement.hpp"
#include "BamlDecompiler/IHandlers.hpp"
#include "BamlDecompiler/Rewrite/AttributeRewritePass.hpp"
#include "BamlDecompiler/Rewrite/DocumentRewritePass.hpp"
#include "BamlDecompiler/Rewrite/MarkupExtensionRewritePass.hpp"
#include "BamlDecompiler/Rewrite/XClassRewritePass.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/Xml/XDocument.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::BamlDecompiler {

namespace {

// The standard .NET NullReferenceException message (the XmlnsDictionary
// convention; the KnownThings.cpp sibling copy).
constexpr const char* kNreMessage = "Object reference not set to an instance of an object.";

// The C# `static readonly IRewritePass[] rewritePasses` (the one static
// field -- XClassRewritePass, MarkupExtensionRewritePass,
// AttributeRewritePass, ConnectionIdRewritePass, DocumentRewritePass, in
// order). The port's array holds the four ported passes in that order;
// ConnectionIdRewritePass's row is deferred with the Phase-3/4 ILAst
// machinery (for a document with no ConnectionId annotations -- every
// stream the port can currently decompile -- the C# pass is a no-op, so
// the observable chain is identical).
const std::vector<std::unique_ptr<Rewrite::IRewritePass>>& RewritePasses()
{
    static const std::vector<std::unique_ptr<Rewrite::IRewritePass>> passes = [] {
        std::vector<std::unique_ptr<Rewrite::IRewritePass>> v;
        v.push_back(std::make_unique<Rewrite::XClassRewritePass>());
        v.push_back(std::make_unique<Rewrite::MarkupExtensionRewritePass>());
        v.push_back(std::make_unique<Rewrite::AttributeRewritePass>());
        v.push_back(std::make_unique<Rewrite::DocumentRewritePass>());
        return v;
    }();
    return passes;
}

} // namespace

// The C# `public BamlDecompilationResult Decompile(Stream stream)`.
BamlDecompilationResult XamlDecompiler::Decompile(const std::uint8_t* data, std::size_t size)
{
    auto document = Baml::ReadDocument(data, size);
    auto ctx = XamlContext::Construct(typeSystem_, document, settings_);

    // The C# `HandlerMap.LookupHandler(ctx.RootNode.Type)`: an empty
    // document makes `BamlNode.Parse` return null and the `RootNode.Type`
    // deref is the NullReferenceException (the port's explicit throw --
    // the header porting decisions).
    Baml::BamlBlockNode* rootNode = ctx->RootNode();
    if (rootNode == nullptr)
        throw std::runtime_error(kNreMessage);
    IHandler* handler = HandlerMap::LookupHandler(rootNode->Type());

    auto elem = handler->Translate(*ctx, *rootNode, nullptr);
    // The C# `elem.Xaml.Element` deref (a root handler contributing no
    // element -- unreachable through the builtin handlers, whose root is
    // always a block record with a handler that returns an element).
    if (elem == nullptr)
        throw std::runtime_error(kNreMessage);

    auto xaml = std::make_shared<Xml::XDocument>();
    xaml->Add(elem->Xaml.Element);

    for (const auto& pass : RewritePasses())
        pass->Run(*ctx, *xaml);

    // The C# `ctx.Baml.AssemblyIdMap.Select(a => a.Value.AssemblyFullName)`
    // -- the map's values in id order (the index==id vector iteration).
    std::vector<std::string> assemblyReferences;
    for (const Baml::AssemblyInfoRecord* rec : ctx->Baml().AssemblyIdMap)
        assemblyReferences.push_back(rec->AssemblyFullName);

    // The C# `ctx.XClassNames.FirstOrDefault() is string s ?
    // (FullTypeName?)new FullTypeName(s) : null`.
    std::optional<ILSpy::Decompiler::TypeSystem::FullTypeName> typeName;
    if (!ctx->XClassNames().empty())
        typeName = ILSpy::Decompiler::TypeSystem::FullTypeName(ctx->XClassNames().front());

    return BamlDecompilationResult(std::move(xaml), std::move(typeName),
        std::move(assemblyReferences), ctx->GeneratedMembers());
}

} // namespace ILSpy::BamlDecompiler
