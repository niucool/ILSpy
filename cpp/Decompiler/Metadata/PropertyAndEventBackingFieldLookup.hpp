// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the C# `class PropertyAndEventBackingFieldLookup` (Metadata/
// PropertyAndEventBackingFieldLookup.cs): the compiler-naming-convention map
// from backing-field rows to their property or event rows.
//
//   * Properties: the C# compiler emits `<Name>k__BackingField`; the VB
//     compiler emits `_Name` (accepted only when compiler-generated).
//   * Events: the C# compiler emits a field with the SAME NAME as the event
//     (or `NameEvent`, only when no event of that name exists -- guarding
//     against a custom event named "Something" next to a field-like event
//     "SomethingEvent" declared in the same type).
//
// The lookups are per-type (the name maps are built and cleared per type
// definition) and dictionary-shaped (a second mapping for the same field row
// overwrites the first, the C# indexer assignment). Consumed by the automatic-
// events family (PatternStatementTransform.IsEventBackingFieldDeclaration)
// through the lazy MetadataFile accessor.

#ifndef ILSPY_DECOMPILER_METADATA_PROPERTYANDEVENTBACKINGFIELDLOOKUP_HPP
#define ILSPY_DECOMPILER_METADATA_PROPERTYANDEVENTBACKINGFIELDLOOKUP_HPP

#include <cstdint>
#include <map>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;

class PropertyAndEventBackingFieldLookup {
public:
    // The C# `PropertyAndEventBackingFieldLookup(MetadataReader metadata)`:
    // the whole-table walk (every type definition's fields, properties and
    // events).
    explicit PropertyAndEventBackingFieldLookup(const MetadataFile& file);

    // The C# `bool IsPropertyBackingField(FieldDefinitionHandle field, out
    // PropertyDefinitionHandle handle)`.
    bool IsPropertyBackingField(std::uint32_t fieldToken,
                                std::uint32_t* propertyToken) const;

    // The C# `bool IsEventBackingField(FieldDefinitionHandle field, out
    // EventDefinitionHandle handle)`.
    bool IsEventBackingField(std::uint32_t fieldToken,
                             std::uint32_t* eventToken) const;

private:
    // The field row's 0x04...... token -> the property row's 0x17...... token.
    std::map<std::uint32_t, std::uint32_t> propertyLookup_;
    // The field row's 0x04...... token -> the event row's 0x14...... token.
    std::map<std::uint32_t, std::uint32_t> eventLookup_;
};

} // namespace ILSpy::Decompiler::Metadata

#endif // ILSPY_DECOMPILER_METADATA_PROPERTYANDEVENTBACKINGFIELDLOOKUP_HPP
