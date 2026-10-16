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

// Port of the BamlReader half of ICSharpCode.BamlDecompiler/Baml/BamlReader.cs
// (Ki, 2015, MIT): the signature/header parse and the record dispatch loop
// (see BamlReader.hpp for the porting decisions).

#include "BamlDecompiler/Baml/BamlBinaryReader.hpp"
#include "BamlDecompiler/Baml/BamlReader.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"

#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace ILSpy::BamlDecompiler::Baml {

namespace {

constexpr char kMsbamlSig[] = "MSBAML";
constexpr std::uint16_t kBamlVersion = 0x60;

// The C# `NotSupportedException()` parameterless throw (the default message).
[[noreturn]] void ThrowNotSupported() {
    throw std::invalid_argument("Specified method is not supported.");
}

// The C# private ReadSignature: the uint32 length (twice the char count),
// the chars as UTF-16, then the padding to the 4-byte multiple of the
// BYTE length. The only accepted length is "MSBAML"'s (12, or the odd 13,
// which passes the same halved check), so the length is validated before
// any chars are read.
std::string ReadSignature(BamlBinaryReader& reader) {
    std::uint32_t length = reader.ReadUInt32();
    if ((length >> 1) != static_cast<std::uint32_t>(sizeof(kMsbamlSig) - 1))
        throw std::out_of_range("Invalid BAML signature length.");
    std::string signature = reader.ReadChars(length >> 1);
    reader.ReadBytes(static_cast<std::int32_t>(((length + 3) & ~std::uint32_t{3}) - length));
    return signature;
}

// The C# ReadDocument switch: one record instance per record type byte,
// constructed fresh per record.
std::unique_ptr<BamlRecord> CreateRecord(BamlRecordType type) {
    switch (type) {
        case BamlRecordType::AssemblyInfo:
            return std::make_unique<AssemblyInfoRecord>();
        case BamlRecordType::AttributeInfo:
            return std::make_unique<AttributeInfoRecord>();
        case BamlRecordType::ConstructorParametersStart:
            return std::make_unique<ConstructorParametersStartRecord>();
        case BamlRecordType::ConstructorParametersEnd:
            return std::make_unique<ConstructorParametersEndRecord>();
        case BamlRecordType::ConstructorParameterType:
            return std::make_unique<ConstructorParameterTypeRecord>();
        case BamlRecordType::ConnectionId:
            return std::make_unique<ConnectionIdRecord>();
        case BamlRecordType::ContentProperty:
            return std::make_unique<ContentPropertyRecord>();
        case BamlRecordType::DefAttribute:
            return std::make_unique<DefAttributeRecord>();
        case BamlRecordType::DefAttributeKeyString:
            return std::make_unique<DefAttributeKeyStringRecord>();
        case BamlRecordType::DefAttributeKeyType:
            return std::make_unique<DefAttributeKeyTypeRecord>();
        case BamlRecordType::DeferableContentStart:
            return std::make_unique<DeferableContentStartRecord>();
        case BamlRecordType::DocumentEnd:
            return std::make_unique<DocumentEndRecord>();
        case BamlRecordType::DocumentStart:
            return std::make_unique<DocumentStartRecord>();
        case BamlRecordType::ElementEnd:
            return std::make_unique<ElementEndRecord>();
        case BamlRecordType::ElementStart:
            return std::make_unique<ElementStartRecord>();
        case BamlRecordType::KeyElementEnd:
            return std::make_unique<KeyElementEndRecord>();
        case BamlRecordType::KeyElementStart:
            return std::make_unique<KeyElementStartRecord>();
        case BamlRecordType::LineNumberAndPosition:
            return std::make_unique<LineNumberAndPositionRecord>();
        case BamlRecordType::LinePosition:
            return std::make_unique<LinePositionRecord>();
        case BamlRecordType::LiteralContent:
            return std::make_unique<LiteralContentRecord>();
        case BamlRecordType::NamedElementStart:
            return std::make_unique<NamedElementStartRecord>();
        case BamlRecordType::OptimizedStaticResource:
            return std::make_unique<OptimizedStaticResourceRecord>();
        case BamlRecordType::PIMapping:
            return std::make_unique<PIMappingRecord>();
        case BamlRecordType::PresentationOptionsAttribute:
            return std::make_unique<PresentationOptionsAttributeRecord>();
        case BamlRecordType::Property:
            return std::make_unique<PropertyRecord>();
        case BamlRecordType::PropertyArrayEnd:
            return std::make_unique<PropertyArrayEndRecord>();
        case BamlRecordType::PropertyArrayStart:
            return std::make_unique<PropertyArrayStartRecord>();
        case BamlRecordType::PropertyComplexEnd:
            return std::make_unique<PropertyComplexEndRecord>();
        case BamlRecordType::PropertyComplexStart:
            return std::make_unique<PropertyComplexStartRecord>();
        case BamlRecordType::PropertyCustom:
            return std::make_unique<PropertyCustomRecord>();
        case BamlRecordType::PropertyDictionaryEnd:
            return std::make_unique<PropertyDictionaryEndRecord>();
        case BamlRecordType::PropertyDictionaryStart:
            return std::make_unique<PropertyDictionaryStartRecord>();
        case BamlRecordType::PropertyListEnd:
            return std::make_unique<PropertyListEndRecord>();
        case BamlRecordType::PropertyListStart:
            return std::make_unique<PropertyListStartRecord>();
        case BamlRecordType::PropertyStringReference:
            return std::make_unique<PropertyStringReferenceRecord>();
        case BamlRecordType::PropertyTypeReference:
            return std::make_unique<PropertyTypeReferenceRecord>();
        case BamlRecordType::PropertyWithConverter:
            return std::make_unique<PropertyWithConverterRecord>();
        case BamlRecordType::PropertyWithExtension:
            return std::make_unique<PropertyWithExtensionRecord>();
        case BamlRecordType::PropertyWithStaticResourceId:
            return std::make_unique<PropertyWithStaticResourceIdRecord>();
        case BamlRecordType::RoutedEvent:
            return std::make_unique<RoutedEventRecord>();
        case BamlRecordType::StaticResourceEnd:
            return std::make_unique<StaticResourceEndRecord>();
        case BamlRecordType::StaticResourceId:
            return std::make_unique<StaticResourceIdRecord>();
        case BamlRecordType::StaticResourceStart:
            return std::make_unique<StaticResourceStartRecord>();
        case BamlRecordType::StringInfo:
            return std::make_unique<StringInfoRecord>();
        case BamlRecordType::Text:
            return std::make_unique<TextRecord>();
        case BamlRecordType::TextWithConverter:
            return std::make_unique<TextWithConverterRecord>();
        case BamlRecordType::TextWithId:
            return std::make_unique<TextWithIdRecord>();
        case BamlRecordType::TypeInfo:
            return std::make_unique<TypeInfoRecord>();
        case BamlRecordType::TypeSerializerInfo:
            return std::make_unique<TypeSerializerInfoRecord>();
        case BamlRecordType::XmlnsProperty:
            return std::make_unique<XmlnsPropertyRecord>();
        // The C# spells these cases out and falls through to the same
        // parameterless throw as the default arm.
        case BamlRecordType::XmlAttribute:
        case BamlRecordType::ProcessingInstruction:
        case BamlRecordType::LastRecordType:
        case BamlRecordType::EndAttributes:
        case BamlRecordType::DefTag:
        case BamlRecordType::ClrEvent:
        case BamlRecordType::Comment:
        default:
            ThrowNotSupported();
    }
}

} // namespace

bool IsBamlHeader(const std::uint8_t* data, std::size_t size) {
    BamlBinaryReader reader(data, size);
    std::uint32_t raw = reader.ReadUInt32();
    std::uint32_t length = raw >> 1;
    if (length != sizeof(kMsbamlSig) - 1)
        return false;
    return reader.ReadChars(length) == kMsbamlSig;
}

BamlDocument ReadDocument(const std::uint8_t* data, std::size_t size) {
    BamlDocument ret;
    BamlBinaryReader reader(data, size);
    ret.Signature = ReadSignature(reader);
    if (ret.Signature != kMsbamlSig)
        ThrowNotSupported();
    ret.ReaderVersion = BamlVersion{ reader.ReadUInt16(), reader.ReadUInt16() };
    ret.UpdaterVersion = BamlVersion{ reader.ReadUInt16(), reader.ReadUInt16() };
    ret.WriterVersion = BamlVersion{ reader.ReadUInt16(), reader.ReadUInt16() };
    if (ret.ReaderVersion.Major != 0 || ret.ReaderVersion.Minor != kBamlVersion
        || ret.UpdaterVersion.Major != 0 || ret.UpdaterVersion.Minor != kBamlVersion
        || ret.WriterVersion.Major != 0 || ret.WriterVersion.Minor != kBamlVersion)
        ThrowNotSupported();

    std::unordered_map<std::int64_t, BamlRecord*> recs;
    while (reader.Position() < static_cast<std::int64_t>(size)) {
        std::int64_t pos = reader.Position();
        auto type = static_cast<BamlRecordType>(reader.ReadByte());
        std::unique_ptr<BamlRecord> rec = CreateRecord(type);
        rec->Position = pos;
        rec->Read(reader);
        BamlRecord* raw = rec.get();
        ret.Add(std::move(rec));
        recs.emplace(pos, raw);
    }
    for (std::size_t i = 0; i < ret.Count(); i++) {
        if (auto* defer = dynamic_cast<IBamlDeferRecord*>(&ret[i])) {
            defer->ReadDefer(ret, static_cast<int>(i),
                [&recs](std::int64_t offset) -> BamlRecord* {
                    auto it = recs.find(offset);
                    if (it == recs.end())
                        throw std::out_of_range(
                            "BAML defer record points at an offset that is not a record boundary.");
                    return it->second;
                });
        }
    }

    return ret;
}

} // namespace ILSpy::BamlDecompiler::Baml
