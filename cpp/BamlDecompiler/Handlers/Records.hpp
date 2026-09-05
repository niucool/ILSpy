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
//  * the misc family: XmlnsPropertyHandler (the xmlns declaration record --
//    the NamespaceMap adds plus the xmlns attribute),
//    PropertyTypeReferenceHandler with its TargetTypeAnnotation payload
//    (the Style.TargetType property element),
//    PropertyWithExtensionHandler (the markup-extension attribute's four
//    initializer arms), and PropertyCustomHandler (the serializer-serialized
//    property attribute -- the KnownTypes serializer matrix over the record's
//    binary payload, with the NeedsFullName Style-ancestor walk the
//    DependencyPropertyConverter short form takes its name form through).
//  * the nine null-returning info/mapping/line handlers (PIMapping,
//    AssemblyInfo, TypeInfo, TypeSerializerInfo, AttributeInfo,
//    DeferableContentStart, ContentProperty, LineNumberAndPosition,
//    LinePosition).
//
// The one remaining Records leaf is LiteralContentHandler (gated on the
// XElement.Parse XML-parser slice the Xml stand-in does not carry).
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

#include <cstdint>
#include <memory>

// The Xml DOM alias (the BamlDecompiler tree is not nested in
// ILSpy::Decompiler, so every Xml reference needs the alias).
namespace Xml = ::ILSpy::Decompiler::Xml;

// The KnownTypes serializer-matrix ids PropertyCustomHandler dispatches
// over (the fixed int16_t underlying type allows the forward declaration;
// the generated KnownTypes.hpp defines the enum and its ToString spelling).
namespace ILSpy::BamlDecompiler::Baml {
enum class KnownTypes : std::int16_t;
} // namespace ILSpy::BamlDecompiler::Baml

// The XamlType/XamlProperty the handlers annotate and resolve (a forward
// declaration suffices -- the shared_ptr members only need the complete
// types at construction, which lives in the .cpp).
namespace ILSpy::BamlDecompiler::Xaml {
class XamlType;
class XamlProperty;
} // namespace ILSpy::BamlDecompiler::Xaml

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

// The C# `internal class XmlnsPropertyHandler : IHandler` -- the xmlns
// declaration record: the assembly-id loop adds one plain NamespaceMap per
// id plus, when the resolved assembly is the MAIN module, one clr-namespace
// map per XmlnsDefinitionAttribute row mapping the record's XML namespace;
// then the xmlns (or xmlns:<prefix>) attribute on the parent element.
class XmlnsPropertyHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class TargetTypeAnnotation` (declared inside
// PropertyTypeReferenceHandler.cs): the resolved target type the
// Style.TargetType property element carries, consumed by
// PropertyCustomHandler's NeedsFullName parent walk.
// The TargetTypeAnnotation payload holds the OWNING XamlType handle (the
// annotation is the GC root; the shared_ptr construction needs the
// complete type, which the .cpp includes).
class TargetTypeAnnotation {
public:
    explicit TargetTypeAnnotation(std::shared_ptr<Xaml::XamlType> type)
        : Type(std::move(type))
    {
    }

    // The C# `XamlType Type { get; }`.
    std::shared_ptr<Xaml::XamlType> Type;
};

// The C# `internal class PropertyTypeReferenceHandler : IHandler` -- the
// property element whose value is a type reference: a TypeExtension child
// element carrying the type name, plus the TargetTypeAnnotation the
// Style.TargetType shape attaches to the parent.
class PropertyTypeReferenceHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class PropertyWithExtensionHandler : IHandler` -- the
// markup-extension property attribute: the {x:Type}/{TemplateBinding}/
// {x:Static}/plain-string initializer arms selected by the extension type
// id and the flag bits, rendered through XamlExtension's ToString.
class PropertyWithExtensionHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;
};

// The C# `internal class PropertyCustomHandler : IHandler` -- the property
// attribute whose value the BAML stream carries in a serializer's binary
// form: the 0xfff-masked serializer type id selects the KnownTypes
// serializer matrix over the record's payload (the DependencyProperty
// converter's 2-byte property-id and type-id-plus-string forms, the enum
// and boolean renders, the XamlBrush solid/other color arms, the geometry
// path data, the Point3D/Vector3D/Point collections, and the Int32
// collection's consecutive/U1/U2/I4 forms), rendered into the property
// attribute the same PropertyHandler-style name form picks.
//
// C#-to-C++ porting decisions:
//  * The C# reads the payload through `new BinaryReader(new
//    MemoryStream(value))` -- the port reads it through the BamlBinaryReader
//    (the port's BinaryReader stand-in; the XamlPathDeserializer arm passes
//    the same reader, and the reader's EndOfStream message IS .NET's).
//  * The three-argument `AppendFormat("{0:R},{1:R},{2:R} ", ...)` calls of
//    the collection arms evaluate their ReadXamlDouble arguments
//    LEFT-TO-RIGHT by C# spec; MSVC may evaluate call arguments
//    right-to-left, so the port sequences each read through a named local
//    (the iteration-35 X/Y-swap trap).
//  * `valueType` (the 0x4000 flag bit) is computed by the C# Translate and
//    never read -- the port computes and discards it for the record-contract
//    documentation (the gold pins it has no observable effect).
//  * `Debug.Assert(value.Length == 1)` (BooleanConverter) is compiled out
//    of the shipped release assembly: a longer payload reads its first
//    byte and the rest is ignored.
//  * The .NET enum ToString of the nested private IntegerCollectionType
//    ("Unknown"/"Consecutive"/"U1"/"U2"/"I4", the decimal for values
//    without a member) and of the KnownTypes serializer id live in
//    IntegerCollectionTypeName/KnownTypeName; NotSupportedException maps
//    to std::runtime_error carrying the exact message (the custom-message
//    convention).
class PropertyCustomHandler : public IHandler {
public:
    Baml::BamlRecordType Type() const override;

    std::unique_ptr<BamlElement> Translate(XamlContext& ctx, Baml::BamlNode& node,
        BamlElement* parent) override;

private:
    // The C# private nested `enum IntegerCollectionType : byte`.
    enum class IntegerCollectionType : std::uint8_t {
        Unknown,
        Consecutive,
        U1,
        U2,
        I4,
    };

    // The C# `string Deserialize(XamlContext ctx, XElement elem, KnownTypes
    // ser, byte[] value)` -- the serializer matrix.
    std::string Deserialize(XamlContext& ctx, Xml::XElement& elem,
        Baml::KnownTypes ser, const std::vector<std::uint8_t>& value);

    // The C# `bool NeedsFullName(XamlProperty property, XamlContext ctx,
    // XElement elem)` -- the Style-ancestor walk: the nearest ancestor whose
    // XamlType annotation resolves to System.Windows.Style carries the
    // TargetTypeAnnotation whose type decides the property's name form
    // (attached to the target -> the full form; an instance property there
    // -> the short form; no Style ancestor or no annotation -> the full
    // form).
    bool NeedsFullName(const Xaml::XamlProperty& property, XamlContext& ctx,
        Xml::XElement& elem);
};

} // namespace ILSpy::BamlDecompiler::Handlers
