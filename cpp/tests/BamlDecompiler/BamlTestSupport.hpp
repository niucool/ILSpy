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

// Shared helpers for the Phase-9 BAML suites: the record-type name table
// (the C# BamlRecordType enum's ToString spelling, shared by the reader
// and node test renderers) and the byte-span ReadDocument call every
// fixture goes through.

#pragma once

#include "BamlDecompiler/Baml/BamlReader.hpp"

#include <cstdint>
#include <string>

namespace ILSpy::Tests::Baml {

// A readable record type name for the failure diffs (the C# enum member
// spelling).
inline const char* RecordTypeName(ILSpy::BamlDecompiler::Baml::BamlRecordType type) {
    using T = ILSpy::BamlDecompiler::Baml::BamlRecordType;
    switch (type) {
        case T::ClrEvent: return "ClrEvent";
        case T::Comment: return "Comment";
        case T::AssemblyInfo: return "AssemblyInfo";
        case T::AttributeInfo: return "AttributeInfo";
        case T::ConstructorParametersStart: return "ConstructorParametersStart";
        case T::ConstructorParametersEnd: return "ConstructorParametersEnd";
        case T::ConstructorParameterType: return "ConstructorParameterType";
        case T::ConnectionId: return "ConnectionId";
        case T::ContentProperty: return "ContentProperty";
        case T::DefAttribute: return "DefAttribute";
        case T::DefAttributeKeyString: return "DefAttributeKeyString";
        case T::DefAttributeKeyType: return "DefAttributeKeyType";
        case T::DeferableContentStart: return "DeferableContentStart";
        case T::DefTag: return "DefTag";
        case T::DocumentEnd: return "DocumentEnd";
        case T::DocumentStart: return "DocumentStart";
        case T::ElementEnd: return "ElementEnd";
        case T::ElementStart: return "ElementStart";
        case T::EndAttributes: return "EndAttributes";
        case T::KeyElementEnd: return "KeyElementEnd";
        case T::KeyElementStart: return "KeyElementStart";
        case T::LastRecordType: return "LastRecordType";
        case T::LineNumberAndPosition: return "LineNumberAndPosition";
        case T::LinePosition: return "LinePosition";
        case T::LiteralContent: return "LiteralContent";
        case T::NamedElementStart: return "NamedElementStart";
        case T::OptimizedStaticResource: return "OptimizedStaticResource";
        case T::PIMapping: return "PIMapping";
        case T::PresentationOptionsAttribute: return "PresentationOptionsAttribute";
        case T::ProcessingInstruction: return "ProcessingInstruction";
        case T::Property: return "Property";
        case T::PropertyArrayEnd: return "PropertyArrayEnd";
        case T::PropertyArrayStart: return "PropertyArrayStart";
        case T::PropertyComplexEnd: return "PropertyComplexEnd";
        case T::PropertyComplexStart: return "PropertyComplexStart";
        case T::PropertyCustom: return "PropertyCustom";
        case T::PropertyDictionaryEnd: return "PropertyDictionaryEnd";
        case T::PropertyDictionaryStart: return "PropertyDictionaryStart";
        case T::PropertyListEnd: return "PropertyListEnd";
        case T::PropertyListStart: return "PropertyListStart";
        case T::PropertyStringReference: return "PropertyStringReference";
        case T::PropertyTypeReference: return "PropertyTypeReference";
        case T::PropertyWithConverter: return "PropertyWithConverter";
        case T::PropertyWithExtension: return "PropertyWithExtension";
        case T::PropertyWithStaticResourceId: return "PropertyWithStaticResourceId";
        case T::RoutedEvent: return "RoutedEvent";
        case T::StaticResourceEnd: return "StaticResourceEnd";
        case T::StaticResourceId: return "StaticResourceId";
        case T::StaticResourceStart: return "StaticResourceStart";
        case T::StringInfo: return "StringInfo";
        case T::Text: return "Text";
        case T::TextWithConverter: return "TextWithConverter";
        case T::TextWithId: return "TextWithId";
        case T::TypeInfo: return "TypeInfo";
        case T::TypeSerializerInfo: return "TypeSerializerInfo";
        case T::XmlAttribute: return "XmlAttribute";
        case T::XmlnsProperty: return "XmlnsProperty";
    }
    return "<unnamed>";
}

// Reads a fixture's bytes through the reader (the byte-span convention).
inline ILSpy::BamlDecompiler::Baml::BamlDocument ReadBaml(const std::string& bytes) {
    return ILSpy::BamlDecompiler::Baml::ReadDocument(
        reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size());
}

} // namespace ILSpy::Tests::Baml
