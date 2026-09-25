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

// Implementation of PropertyAndEventBackingFieldLookup (see the header).

#include "Decompiler/Metadata/PropertyAndEventBackingFieldLookup.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Metadata {

// The C# ctor's walk, over the port's per-type enumeration surface (the
// C# reads the same rows through the MetadataReader directly).
PropertyAndEventBackingFieldLookup::PropertyAndEventBackingFieldLookup(
    const MetadataFile& file) {
    for (const auto& t : file.TypeDefs()) {
        // The C# `MultiDictionary<string, FieldDefinitionHandle>
        // nameToFieldMap` (built and cleared per type) and the `HashSet<string>
        // eventNames`.
        std::map<std::string, std::vector<std::uint32_t>> nameToFieldMap;
        std::vector<std::string> eventNames;
        for (const auto& f : file.GetFields(t.Token))
            nameToFieldMap[f.Name].push_back(f.Token);

        for (const auto& p : file.GetProperties(t.Token)) {
            // default C# property backing field name is
            // "<PropertyName>k__BackingField"
            auto it = nameToFieldMap.find("<" + p.Name + ">k__BackingField");
            if (it != nameToFieldMap.end()) {
                for (std::uint32_t fieldToken : it->second)
                    propertyLookup_[fieldToken] = p.Token;
            } else {
                it = nameToFieldMap.find("_" + p.Name);
                if (it != nameToFieldMap.end()) {
                    for (std::uint32_t fieldToken : it->second) {
                        if (file.IsFieldCompilerGeneratedOrInCompilerGeneratedClass(
                                fieldToken)) {
                            propertyLookup_[fieldToken] = p.Token;
                        }
                    }
                }
            }
        }

        // first get all names of events defined, so that we can make sure we
        // don't accidentally associate the wrong backing field with the
        // event, in case there is an event called "Something" without a
        // backing field (i.e., custom event) as well as an auto/field event
        // called "SomethingEvent" declared in the same type.
        for (const auto& e : file.GetEvents(t.Token))
            eventNames.push_back(e.Name);

        for (const auto& e : file.GetEvents(t.Token)) {
            auto it = nameToFieldMap.find(e.Name);
            if (it != nameToFieldMap.end()) {
                for (std::uint32_t fieldToken : it->second)
                    eventLookup_[fieldToken] = e.Token;
            } else {
                std::string nameWithSuffix = e.Name + "Event";
                if (std::find(eventNames.begin(), eventNames.end(),
                              nameWithSuffix) == eventNames.end()) {
                    it = nameToFieldMap.find(nameWithSuffix);
                    if (it != nameToFieldMap.end()) {
                        for (std::uint32_t fieldToken : it->second)
                            eventLookup_[fieldToken] = e.Token;
                    }
                }
            }
        }
    }
}

bool PropertyAndEventBackingFieldLookup::IsPropertyBackingField(
    std::uint32_t fieldToken, std::uint32_t* propertyToken) const {
    auto it = propertyLookup_.find(fieldToken);
    if (it == propertyLookup_.end())
        return false;
    *propertyToken = it->second;
    return true;
}

bool PropertyAndEventBackingFieldLookup::IsEventBackingField(
    std::uint32_t fieldToken, std::uint32_t* eventToken) const {
    auto it = eventLookup_.find(fieldToken);
    if (it == eventLookup_.end())
        return false;
    *eventToken = it->second;
    return true;
}

} // namespace ILSpy::Decompiler::Metadata
