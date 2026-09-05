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

// Port of ICSharpCode.BamlDecompiler/Handlers/Records (Ki, 2015, MIT): the
// leaf-record handlers -- one IHandler per leaf BAML record type, the
// no-children half of the registry (the Handlers/Blocks file holds the
// block-header half). This header declares the classes whose registry rows
// the CreateBuiltinHandlers manifest has landed so far:
//  * TextHandler (Text) / TextWithIdHandler (TextWithId) /
//    TextWithConverterHandler (TextWithConverter, the TextHandler subclass
//    that only re-binds the record type),
//  * ConnectionIdHandler (ConnectionId -- the BamlConnectionId annotation),
//  * DefAttributeHandler (DefAttribute -- the x:-namespaced attribute) and
//    PresentationOptionsAttributeHandler (PresentationOptionsAttribute -- the
//    presentation-options-namespaced attribute),
//  * the nine null-returning handlers (AssemblyInfo, AttributeInfo,
//    ContentProperty, DeferableContentStart, LineNumberAndPosition,
//    LinePosition, PIMapping, TypeInfo, TypeSerializerInfo).
//
// C#-to-C++ porting decisions:
//  * `Translate`'s C# `BamlElement parent` parameter is nullable (the
//    XamlDecompiler.Decompile root call passes null) -- the port's
//    IHandler::Translate takes `BamlElement*`; every deref of a null parent
//    throws the .NET NullReferenceException message (the XmlnsDictionary
//    convention).
//  * The C# `(<Record>)((BamlRecordNode)node).Record` double cast ports to
//    dynamic_casts carrying the .NET InvalidCastException message; only a
//    hand-built lying tree (a node whose type does not match its handler)
//    reaches either arm, and the source type renders through the fixed
//    '<node>'/'<record>' placeholder (the XamlResourceKey convention).
//  * The C# `Debug.Assert`/`Debug.WriteLine` bodies (DeferableContentStart's
//    footer check, ContentProperty's TODO) are compiled out of the release
//    assembly the tool ships -- the port targets the release behavior.
//  * The annotations hold OWNING shared_ptrs (the GC-rooting convention:
//    the payload must survive the XamlContext).

#pragma once

#include "BamlDecompiler/IHandlers.hpp"

namespace ILSpy::BamlDecompiler::Handlers {

// The C# `internal class TextHandler : IHandler` -- the record's value is a
// plain text node of the parent element.
class TextHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class TextWithIdHandler : IHandler` -- the record's value
// resolves through the StringInfo id table.
class TextWithIdHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class TextWithConverterHandler : TextHandler, IHandler`:
// the C# re-binds `IHandler.Type` through an explicit interface
// implementation (the inherited `TextHandler.Type` still answers Text through
// a base reference) and inherits Translate unchanged. The port re-binds
// through a virtual override -- the only observable difference is a call
// through a `TextHandler*` pointing at a TextWithConverterHandler, which no
// ported consumer makes.
class TextWithConverterHandler : public TextHandler {
public:
    Baml::BamlRecordType Type() const override;
};

// The C# `internal class ConnectionIdHandler : IHandler`.
class ConnectionIdHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class DefAttributeHandler : IHandler` -- the x: attribute
// (Name/Uid/Class/...: the string-id-resolved attribute name in the XAML
// namespace).
class DefAttributeHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class PresentationOptionsAttributeHandler : IHandler` --
// the presentation-options attribute (the freeze/... namespace).
class PresentationOptionsAttributeHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class AssemblyInfoHandler : IHandler` -- the record walk
// (BamlContext.ConstructContext) consumes the assembly table; the handler
// contributes nothing.
class AssemblyInfoHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class AttributeInfoHandler : IHandler` -- the attribute
// table is consumed by the record walk.
class AttributeInfoHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class ContentPropertyHandler : IHandler` -- the C#
// carries a `// TODO: What to do here?` and returns null.
class ContentPropertyHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class DeferableContentStartHandler : IHandler` -- the
// deferred-content marker; the release body is the null return (the
// Debug.Assert footer check is compiled out).
class DeferableContentStartHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class LineNumberAndPositionHandler : IHandler`.
class LineNumberAndPositionHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class LinePositionHandler : IHandler`.
class LinePositionHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class PIMappingHandler : IHandler` -- the record walk's
// BuildPIMappings consumes the mapping; the handler contributes nothing.
class PIMappingHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class TypeInfoHandler : IHandler` -- the type table is
// consumed by the record walk.
class TypeInfoHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class TypeSerializerInfoHandler : IHandler`.
class TypeSerializerInfoHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

} // namespace ILSpy::BamlDecompiler::Handlers
