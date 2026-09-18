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

#include "Decompiler/Metadata/PropertyAndEventBackingFieldLookup.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

PropertyAndEventBackingFieldLookup::PropertyAndEventBackingFieldLookup(
    const MetadataFile& file) {
    if (!file.IsValid())
        return;

    // The C# `Dictionary<string, List<FieldDefinitionHandle>> nameToFieldMap` is per-type
    // scratch state (cleared at the end of each type's iteration): the port rebuilds it inside
    // the type loop, along with the per-type `HashSet<string> eventNames`.
    for (const TypeDefInfo& type : file.TypeDefs()) {
        std::unordered_map<std::string, std::vector<std::uint32_t>> nameToFieldMap;
        for (const FieldInfo& field : file.GetFields(type.Token)) {
            nameToFieldMap[field.Name].push_back(field.Token);
        }

        for (const PropertyInfo& property : file.GetProperties(type.Token)) {
            const std::string backingFieldName = "<" + property.Name + ">k__BackingField";
            auto exact = nameToFieldMap.find(backingFieldName);
            if (exact != nameToFieldMap.end()) {
                for (std::uint32_t fieldToken : exact->second) {
                    propertyLookup_[fieldToken] = property.Token;
                }
            } else {
                auto vb = nameToFieldMap.find("_" + property.Name);
                if (vb != nameToFieldMap.end()) {
                    for (std::uint32_t fieldToken : vb->second) {
                        // The C# `if (fieldHandle.IsCompilerGenerated(metadata))`: the VB
                        // `_Property` field counts only when it carries the attribute
                        // [CompilerGenerated] (convention (b)).
                        if (HasKnownAttribute(
                                file, fieldToken,
                                TypeSystem::KnownAttribute::CompilerGenerated)) {
                            propertyLookup_[fieldToken] = property.Token;
                        }
                    }
                }
            }
        }

        // The C# collects every event name FIRST, so the `Something` / `SomethingEvent`
        // fallback below cannot steal the backing field of a genuinely-named `SomethingEvent`
        // custom event.
        std::unordered_set<std::string> eventNames;
        for (const EventInfo& event : file.GetEvents(type.Token)) {
            eventNames.insert(event.Name);
        }
        for (const EventInfo& event : file.GetEvents(type.Token)) {
            auto exact = nameToFieldMap.find(event.Name);
            if (exact != nameToFieldMap.end()) {
                for (std::uint32_t fieldToken : exact->second) {
                    eventLookup_[fieldToken] = event.Token;
                }
            } else {
                const std::string nameWithSuffix = event.Name + "Event";
                if (eventNames.find(nameWithSuffix) == eventNames.end()) {
                    auto suffixed = nameToFieldMap.find(nameWithSuffix);
                    if (suffixed != nameToFieldMap.end()) {
                        for (std::uint32_t fieldToken : suffixed->second) {
                            eventLookup_[fieldToken] = event.Token;
                        }
                    }
                }
            }
        }
    }
}

bool PropertyAndEventBackingFieldLookup::IsPropertyBackingField(
    std::uint32_t fieldToken, std::uint32_t& propertyToken) const {
    auto it = propertyLookup_.find(fieldToken);
    if (it == propertyLookup_.end())
        return false;
    propertyToken = it->second;
    return true;
}

bool PropertyAndEventBackingFieldLookup::IsEventBackingField(
    std::uint32_t fieldToken, std::uint32_t& eventToken) const {
    auto it = eventLookup_.find(fieldToken);
    if (it == eventLookup_.end())
        return false;
    eventToken = it->second;
    return true;
}

} // namespace ILSpy::Decompiler::Metadata
