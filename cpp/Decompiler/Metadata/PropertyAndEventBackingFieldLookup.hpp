// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Metadata/PropertyAndEventBackingFieldLookup.cs -- the
// compiler-emitted backing-field -> property/event association lookup. For a field it answers
// whether the field is the backing storage of an automatic property (`<Property>
// k__BackingField`, or the VB `_Property` that is compiler-generated) or of a field-like
// event (the same-named field, or the VB `PropertyEvent` fallback).
//
// The C# type is `class PropertyAndEventBackingFieldLookup` (internal). The ctor walks every
// TypeDef: it indexes the type's fields by name, then maps the `<Property>k__BackingField` /
// `_Property` field (the latter only when compiler-generated) to the property, and the
// event-named / `Event`-suffixed field to the event. Both the field-name map and the
// event-name set are per-type scratch state (cleared at the end of each type's iteration), so
// two types may declare same-named backing fields without colliding.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `MetadataReader metadata` ctor parameter ports to `const MetadataFile&`: the
//      class reads ONLY the established raw surface (`TypeDefs()`, `GetFields` /
//      `GetProperties` / `GetEvents`, the per-token names), the
//      MethodSemanticsLookup/NamespaceCache convention. The `Dictionary<FieldDefinitionHandle,
//      ...>` maps port to `std::unordered_map<std::uint32_t, std::uint32_t>` keyed by the raw
//      field TOKEN (the D381 EntityHandle-as-raw-token convention), with the property/event
//      token as the value.
//  (b) The C# `fieldHandle.IsCompilerGenerated(metadata)` (SRMExtensions.cs line 580) is the
//      field's own [CompilerGenerated] custom attribute -- `SRMExtensions::HasKnownAttribute`
//      over `KnownAttribute::CompilerGenerated`. The C# `GetCustomAttributes()...
//      HasKnownAttribute` propagates a BadImageFormatException for an attribute row with an
//      unexpected constructor kind; the port propagates GetAttributeType's throw the same way.
//  (c) The C# `DateTime`-style lazy property on `MetadataFile` (`PropertyAndEventBackingField-
//      Lookup`) is realized as `MetadataFile::GetPropertyAndEventBackingFieldLookup()`, a lazy
//      pimpl cache mirroring `GetMethodSemanticsLookup`. Tests construct the lookup directly
//      against a `MetadataFile` (the internal-ctor convention).

#pragma once

#include <cstdint>
#include <unordered_map>

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;

// `class PropertyAndEventBackingFieldLookup` (PropertyAndEventBackingFieldLookup.cs): the
// backing-field -> association lookup, built once per `MetadataFile`.
class PropertyAndEventBackingFieldLookup {
public:
    // The C# `public PropertyAndEventBackingFieldLookup(MetadataReader metadata)`: walks every
    // TypeDef and fills the two maps. Never throws for a valid file; a malformed custom
    // attribute on a `_Property` candidate propagates the attribute decode throw (convention
    // (b)).
    explicit PropertyAndEventBackingFieldLookup(const MetadataFile& file);

    // The C# `public bool IsPropertyBackingField(FieldDefinitionHandle field, out
    // PropertyDefinitionHandle handle)`: when the field is a property backing field, writes the
    // 0x17...... property token to `propertyToken` and answers true; otherwise answers false and
    // leaves `propertyToken` untouched (the C# `TryGetValue` leaves the out parameter default --
    // the port documents that the caller must not read it on a false answer).
    bool IsPropertyBackingField(std::uint32_t fieldToken,
                                std::uint32_t& propertyToken) const;

    // The C# `public bool IsEventBackingField(FieldDefinitionHandle field, out
    // EventDefinitionHandle handle)`: the event analogue, writing the 0x14...... event token.
    bool IsEventBackingField(std::uint32_t fieldToken, std::uint32_t& eventToken) const;

private:
    std::unordered_map<std::uint32_t, std::uint32_t> propertyLookup_;
    std::unordered_map<std::uint32_t, std::uint32_t> eventLookup_;
};

} // namespace ILSpy::Decompiler::Metadata
