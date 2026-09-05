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
//  * PropertyHandler (Property -- the property attribute, with the attached
//    / x:Name / plain arm selection) and PropertyWithConverterHandler
//    (PropertyWithConverter, the PropertyHandler subclass that only re-binds
//    the record type),
//  * ConstructorParameterTypeHandler (ConstructorParameterType -- the
//    {x:Type} TypeExtension element),
//  * DefAttributeStringHandler (DefAttributeKeyString) and
//    DefAttributeTypeHandler (DefAttributeKeyType) -- the x:Key RECORD
//    handlers: Translate only creates the XamlResourceKey annotation pair
//    (the key node and its value element), and the IDeferHandler arm
//    renders the x:Key element (the resolved string value / the
//    TypeExtension child) that the ElementHandler defer branch drives.
//  * the static-resource family (the StaticResourceStart block, the
//    OptimizedStaticResource leaf, and the StaticResourceId /
//    PropertyWithStaticResourceId consumers): the resource handlers'
//    Translate registers the node under the nearest annotated SIBLING
//    (the key node -- the real defer-block wiring the registrations
//    precede the consumer-carrying value blocks in document order), and
//    the IDeferHandler arms render the StaticResourceStart element / the
//    {StaticResource} extension element the consumers re-drive; the
//    consumers walk the ancestors for the key whose StaticResources list
//    holds the referenced node.
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
//  * The C# `StaticResourceIdHandler.Translate` adds the rendered element
//    to `parent.Children` AND returns it -- `ProcessChildren` then re-adds
//    the same reference into the SAME list (no C# reader ever iterates
//    BamlElement.Children; the double-add is write-only bookkeeping). The
//    port's owning children vector cannot hold one element twice, so the
//    handler returns the element and leaves the list add to its caller
//    (`ProcessChildren`'s add is the single owner; the direct-call
//    parent-list population is the documented divergence).
//  * The annotations hold OWNING shared_ptrs (the GC-rooting convention:
//    the payload must survive the XamlContext).

#pragma once

#include "BamlDecompiler/IHandlers.hpp"

namespace ILSpy::BamlDecompiler::Handlers {

// The C# `internal class PropertyHandler : IHandler` -- the plain property
// attribute on the parent element (the attached form, the x:Name form, or
// the plain declaring-type-qualified form; the arm selection runs at Add
// time, after DeclaringType.ResolveNamespace mutated the parent).
class PropertyHandler : public IHandler {
public:
    // The C# `public virtual BamlRecordType Type` (the PropertyWithConverter
    // subclass re-binds it).
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class PropertyWithConverterHandler : PropertyHandler,
// IHandler` -- the explicit-interface re-bind of Type (the
// TextWithConverterHandler pattern: the port re-binds through a virtual
// override; the only observable difference is a call through a
// `PropertyHandler*`, which no ported consumer makes).
class PropertyWithConverterHandler : public PropertyHandler {
public:
    Baml::BamlRecordType Type() const override;
};

// The C# `internal class ConstructorParameterTypeHandler : IHandler` --
// the {x:Type ...} constructor argument (a TypeExtension element whose Ctor
// child carries the type name).
class ConstructorParameterTypeHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

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

// The C# `internal class DefAttributeStringHandler : IHandler,
// IDeferHandler` (the DefAttributeKeyStringHandler.cs file): the x:Key
// string record -- the C# class name diverges from its FILE name. Translate
// only creates the key annotation pair; TranslateDefer renders the x:Key
// element carrying the resolved string value.
class DefAttributeStringHandler : public IHandler, public IDeferHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;

    std::unique_ptr<BamlElement> TranslateDefer(XamlContext& ctx,
        Baml::BamlNode& node, BamlElement* parent) override;
};

// The C# `internal class DefAttributeTypeHandler : IHandler, IDeferHandler`
// (the DefAttributeKeyTypeHandler.cs file): the x:Key type record -- the
// deferred arm renders the x:Key element wrapping the {x:Type}
// TypeExtension child (the resolved type's name).
class DefAttributeTypeHandler : public IHandler, public IDeferHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;

    std::unique_ptr<BamlElement> TranslateDefer(XamlContext& ctx,
        Baml::BamlNode& node, BamlElement* parent) override;
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

// The C# `internal class StaticResourceStartHandler : IHandler,
// IDeferHandler`: the deferred static-resource VALUE block. Translate
// registers the block node under the nearest annotated sibling (the key
// node the x:Key handler annotated) and contributes no element;
// TranslateDefer renders the block's typed element and walks its children
// (the consumer's re-render -- no ResolveNamespace/rename pair and no
// defer branch of its own, unlike ElementHandler).
class StaticResourceStartHandler : public IHandler, public IDeferHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;

    std::unique_ptr<BamlElement> TranslateDefer(XamlContext& ctx,
        Baml::BamlNode& node, BamlElement* parent) override;
};

// The C# `internal class StaticResourceIdHandler : IHandler`: the
// deferred static-resource REFERENCE -- walks the ancestors for the key
// whose StaticResources list holds the referenced node (skipping keys
// whose list is too short), re-drives the resource handler's TranslateDefer
// into the parent, and returns the rendered element (see the file note for
// the children-list ownership divergence).
class StaticResourceIdHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class OptimizedStaticResourceHandler : IHandler,
// IDeferHandler`: the optimized (short-form) static-resource reference.
// Translate registers the leaf node under the nearest annotated sibling;
// TranslateDefer renders the {StaticResource} extension element whose
// Ctor child carries the key -- the {x:Type} TypeExtension element, the
// {x:Static} StaticExtension element (the resolved property's name, or
// the KnownThings resource row for the high wire ids with the 232/464/467
// magic-range arithmetic), or the resolved string.
class OptimizedStaticResourceHandler : public IHandler, public IDeferHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;

    std::unique_ptr<BamlElement> TranslateDefer(XamlContext& ctx,
        Baml::BamlNode& node, BamlElement* parent) override;
};

// The C# `internal class PropertyWithStaticResourceIdHandler : IHandler`:
// the deferred static-resource reference in its property-element form --
// renders the property element, re-drives the resource handler's
// TranslateDefer into it, and finishes with the ResolveNamespace + rename
// pair over the property element.
class PropertyWithStaticResourceIdHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

} // namespace ILSpy::BamlDecompiler::Handlers
