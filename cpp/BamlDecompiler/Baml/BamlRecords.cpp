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

// Port of ICSharpCode.BamlDecompiler/Baml/BamlRecords.cs (Ki, 2015, MIT):
// the record read/write bodies (see BamlRecords.hpp for the porting
// decisions).

#include "BamlDecompiler/Baml/BamlBinaryReader.hpp"
#include "BamlDecompiler/Baml/BamlBinaryWriter.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"

namespace ILSpy::BamlDecompiler::Baml {

// --- SizedBamlRecord -------------------------------------------------------

void SizedBamlRecord::Read(BamlBinaryReader& reader) {
    std::int64_t pos = reader.Position();
    int size = reader.ReadEncodedInt();
    ReadData(reader, size - static_cast<int>(reader.Position() - pos));
}

void SizedBamlRecord::Write(BamlBinaryWriter& writer) {
    std::int64_t pos = writer.Position();
    WriteData(writer);
    int size = static_cast<int>(writer.Position() - pos);
    // The payload size plus the byte length of encoding the total size
    // (which itself includes the length of that encoding -- the C# fixed
    // point SizeofEncodedInt(SizeofEncodedInt(size) + size)).
    size = SizeofEncodedInt(SizeofEncodedInt(size) + size) + size;
    writer.SetPosition(pos);
    writer.WriteEncodedInt(size);
    WriteData(writer);
}

int SizedBamlRecord::SizeofEncodedInt(int value) {
    if ((value & ~0x7F) == 0)
        return 1;
    if ((value & ~0x3FFF) == 0)
        return 2;
    if ((value & ~0x1FFFFF) == 0)
        return 3;
    if ((value & ~0x0FFFFFFF) == 0)
        return 4;
    return 5;
}

// --- BamlDeferReader --------------------------------------------------------

std::out_of_range BamlDeferReader::Malformed(const std::string& detail) {
    return std::out_of_range("Malformed BAML defer block: " + detail + ".");
}

int BamlDeferReader::SkipKeys(BamlDocument& doc, int index) {
    bool keys = true;
    do {
        if (index < 0 || index >= static_cast<int>(doc.Count()))
            throw Malformed("ran off the end of the record list while scanning keys");
        switch (doc[index].Type()) {
            case BamlRecordType::DefAttributeKeyString:
            case BamlRecordType::DefAttributeKeyType:
            case BamlRecordType::OptimizedStaticResource:
                keys = true;
                break;
            case BamlRecordType::StaticResourceStart:
                NavigateTree(doc, BamlRecordType::StaticResourceStart,
                    BamlRecordType::StaticResourceEnd, index, 0);
                keys = true;
                break;
            case BamlRecordType::KeyElementStart:
                NavigateTree(doc, BamlRecordType::KeyElementStart,
                    BamlRecordType::KeyElementEnd, index, 0);
                keys = true;
                break;
            default:
                keys = false;
                index--;
                break;
        }
        index++;
    } while (keys);
    if (index < 0 || index >= static_cast<int>(doc.Count()))
        throw Malformed("ran off the end of the record list before the defer target");
    return index;
}

void BamlDeferReader::NavigateTree(BamlDocument& doc, BamlRecordType start,
    BamlRecordType end, int& index, int depth) {
    if (depth >= MaxNestingDepth)
        throw Malformed("nested start records exceed the maximum supported depth");
    index++;
    for (;;) {
        if (index >= static_cast<int>(doc.Count()))
            throw Malformed("a start record has no matching end record");
        if (doc[index].Type() == start) {
            NavigateTree(doc, start, end, index, depth + 1);
        } else if (doc[index].Type() == end) {
            return;
        }
        index++;
    }
}

// --- The record read/write bodies -------------------------------------------

void XmlnsPropertyRecord::ReadData(BamlBinaryReader& reader, int) {
    Prefix = reader.ReadString();
    XmlNamespace = reader.ReadString();
    std::uint16_t count = reader.ReadUInt16();
    AssemblyIds.assign(count, 0);
    for (auto& id : AssemblyIds)
        id = reader.ReadUInt16();
}

void XmlnsPropertyRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteString(Prefix);
    writer.WriteString(XmlNamespace);
    writer.WriteUInt16(static_cast<std::uint16_t>(AssemblyIds.size()));
    for (auto id : AssemblyIds)
        writer.WriteUInt16(id);
}

void PresentationOptionsAttributeRecord::ReadData(BamlBinaryReader& reader, int) {
    Value = reader.ReadString();
    NameId = reader.ReadUInt16();
}

void PresentationOptionsAttributeRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteString(Value);
    writer.WriteUInt16(NameId);
}

void PIMappingRecord::ReadData(BamlBinaryReader& reader, int) {
    XmlNamespace = reader.ReadString();
    ClrNamespace = reader.ReadString();
    AssemblyId = reader.ReadUInt16();
}

void PIMappingRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteString(XmlNamespace);
    writer.WriteString(ClrNamespace);
    writer.WriteUInt16(AssemblyId);
}

void AssemblyInfoRecord::ReadData(BamlBinaryReader& reader, int) {
    AssemblyId = reader.ReadUInt16();
    AssemblyFullName = reader.ReadString();
}

void AssemblyInfoRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteUInt16(AssemblyId);
    writer.WriteString(AssemblyFullName);
}

void PropertyRecord::ReadData(BamlBinaryReader& reader, int) {
    AttributeId = reader.ReadUInt16();
    Value = reader.ReadString();
}

void PropertyRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteUInt16(AttributeId);
    writer.WriteString(Value);
}

void PropertyWithConverterRecord::ReadData(BamlBinaryReader& reader, int size) {
    PropertyRecord::ReadData(reader, size);
    ConverterTypeId = reader.ReadUInt16();
}

void PropertyWithConverterRecord::WriteData(BamlBinaryWriter& writer) {
    PropertyRecord::WriteData(writer);
    writer.WriteUInt16(ConverterTypeId);
}

void PropertyCustomRecord::ReadData(BamlBinaryReader& reader, int size) {
    std::int64_t pos = reader.Position();
    AttributeId = reader.ReadUInt16();
    SerializerTypeId = reader.ReadUInt16();
    Data = reader.ReadBytes(size - static_cast<int>(reader.Position() - pos));
}

void PropertyCustomRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteUInt16(AttributeId);
    writer.WriteUInt16(SerializerTypeId);
    writer.WriteBytes(Data);
}

void DefAttributeRecord::ReadData(BamlBinaryReader& reader, int) {
    Value = reader.ReadString();
    NameId = reader.ReadUInt16();
}

void DefAttributeRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteString(Value);
    writer.WriteUInt16(NameId);
}

void TypeInfoRecord::ReadData(BamlBinaryReader& reader, int) {
    TypeId = reader.ReadUInt16();
    AssemblyId = reader.ReadUInt16();
    TypeFullName = reader.ReadString();
}

void TypeInfoRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteUInt16(TypeId);
    writer.WriteUInt16(AssemblyId);
    writer.WriteString(TypeFullName);
}

void TypeSerializerInfoRecord::ReadData(BamlBinaryReader& reader, int size) {
    TypeInfoRecord::ReadData(reader, size);
    SerializerTypeId = reader.ReadUInt16();
}

void TypeSerializerInfoRecord::WriteData(BamlBinaryWriter& writer) {
    TypeInfoRecord::WriteData(writer);
    writer.WriteUInt16(SerializerTypeId);
}

void AttributeInfoRecord::ReadData(BamlBinaryReader& reader, int) {
    AttributeId = reader.ReadUInt16();
    OwnerTypeId = reader.ReadUInt16();
    AttributeUsage = reader.ReadByte();
    Name = reader.ReadString();
}

void AttributeInfoRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteUInt16(AttributeId);
    writer.WriteUInt16(OwnerTypeId);
    writer.WriteByte(AttributeUsage);
    writer.WriteString(Name);
}

void StringInfoRecord::ReadData(BamlBinaryReader& reader, int) {
    StringId = reader.ReadUInt16();
    Value = reader.ReadString();
}

void StringInfoRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteUInt16(StringId);
    writer.WriteString(Value);
}

void TextRecord::ReadData(BamlBinaryReader& reader, int) {
    Value = reader.ReadString();
}

void TextRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteString(Value);
}

void TextWithConverterRecord::ReadData(BamlBinaryReader& reader, int size) {
    TextRecord::ReadData(reader, size);
    ConverterTypeId = reader.ReadUInt16();
}

void TextWithConverterRecord::WriteData(BamlBinaryWriter& writer) {
    TextRecord::WriteData(writer);
    writer.WriteUInt16(ConverterTypeId);
}

void TextWithIdRecord::ReadData(BamlBinaryReader& reader, int) {
    ValueId = reader.ReadUInt16();
}

void TextWithIdRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteUInt16(ValueId);
}

void LiteralContentRecord::ReadData(BamlBinaryReader& reader, int) {
    Value = reader.ReadString();
    Reserved0 = reader.ReadUInt32();
    Reserved1 = reader.ReadUInt32();
}

void LiteralContentRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteString(Value);
    writer.WriteUInt32(Reserved0);
    writer.WriteUInt32(Reserved1);
}

// The read order is AttributeId then Value; the write order is Value then
// AttributeId (the C# asymmetry, reproduced faithfully -- see the header).
void RoutedEventRecord::ReadData(BamlBinaryReader& reader, int) {
    AttributeId = reader.ReadUInt16();
    Value = reader.ReadString();
}

void RoutedEventRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteString(Value);
    writer.WriteUInt16(AttributeId);
}

void DocumentStartRecord::Read(BamlBinaryReader& reader) {
    LoadAsync = reader.ReadBoolean();
    MaxAsyncRecords = reader.ReadUInt32();
    DebugBaml = reader.ReadBoolean();
}

void DocumentStartRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteBoolean(LoadAsync);
    writer.WriteUInt32(MaxAsyncRecords);
    writer.WriteBoolean(DebugBaml);
}

void ElementStartRecord::Read(BamlBinaryReader& reader) {
    TypeId = reader.ReadUInt16();
    Flags = reader.ReadByte();
}

void ElementStartRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt16(TypeId);
    writer.WriteByte(Flags);
}

void ConnectionIdRecord::Read(BamlBinaryReader& reader) {
    ConnectionId = reader.ReadUInt32();
}

void ConnectionIdRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt32(ConnectionId);
}

void PropertyWithExtensionRecord::Read(BamlBinaryReader& reader) {
    AttributeId = reader.ReadUInt16();
    Flags = reader.ReadUInt16();
    ValueId = reader.ReadUInt16();
}

void PropertyWithExtensionRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt16(AttributeId);
    writer.WriteUInt16(Flags);
    writer.WriteUInt16(ValueId);
}

void PropertyComplexStartRecord::Read(BamlBinaryReader& reader) {
    AttributeId = reader.ReadUInt16();
}

void PropertyComplexStartRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt16(AttributeId);
}

void PropertyTypeReferenceRecord::Read(BamlBinaryReader& reader) {
    PropertyComplexStartRecord::Read(reader);
    TypeId = reader.ReadUInt16();
}

void PropertyTypeReferenceRecord::Write(BamlBinaryWriter& writer) {
    PropertyComplexStartRecord::Write(writer);
    writer.WriteUInt16(TypeId);
}

void PropertyStringReferenceRecord::Read(BamlBinaryReader& reader) {
    PropertyComplexStartRecord::Read(reader);
    StringId = reader.ReadUInt16();
}

void PropertyStringReferenceRecord::Write(BamlBinaryWriter& writer) {
    PropertyComplexStartRecord::Write(writer);
    writer.WriteUInt16(StringId);
}

void StaticResourceIdRecord::Read(BamlBinaryReader& reader) {
    StaticResourceId = reader.ReadUInt16();
}

void StaticResourceIdRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt16(StaticResourceId);
}

void PropertyWithStaticResourceIdRecord::Read(BamlBinaryReader& reader) {
    AttributeId = reader.ReadUInt16();
    StaticResourceIdRecord::Read(reader);
}

void PropertyWithStaticResourceIdRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt16(AttributeId);
    StaticResourceIdRecord::Write(writer);
}

void ContentPropertyRecord::Read(BamlBinaryReader& reader) {
    AttributeId = reader.ReadUInt16();
}

void ContentPropertyRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt16(AttributeId);
}

void ConstructorParameterTypeRecord::Read(BamlBinaryReader& reader) {
    TypeId = reader.ReadUInt16();
}

void ConstructorParameterTypeRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt16(TypeId);
}

void DeferableContentStartRecord::Read(BamlBinaryReader& reader) {
    size = reader.ReadUInt32();
    pos = reader.Position();
}

void DeferableContentStartRecord::Write(BamlBinaryWriter& writer) {
    pos = writer.Position();
    writer.WriteUInt32(0);
}

void DeferableContentStartRecord::ReadDefer(BamlDocument&, int,
    const std::function<BamlRecord*(std::int64_t)>& resolve) {
    SetRecord(resolve(pos + size));
}

void DeferableContentStartRecord::WriteDefer(BamlDocument&, int,
    BamlBinaryWriter& writer) {
    writer.SetPosition(pos);
    writer.WriteUInt32(
        static_cast<std::uint32_t>(Record()->Position - (pos + 4)));
}

void DefAttributeKeyTypeRecord::Read(BamlBinaryReader& reader) {
    ElementStartRecord::Read(reader);
    pos = reader.ReadUInt32();
    Shared = reader.ReadBoolean();
    SharedSet = reader.ReadBoolean();
}

void DefAttributeKeyTypeRecord::Write(BamlBinaryWriter& writer) {
    ElementStartRecord::Write(writer);
    pos = static_cast<std::uint32_t>(writer.Position());
    writer.WriteUInt32(0);
    writer.WriteBoolean(Shared);
    writer.WriteBoolean(SharedSet);
}

void DefAttributeKeyTypeRecord::ReadDefer(BamlDocument& doc, int index,
    const std::function<BamlRecord*(std::int64_t)>& resolve) {
    index = BamlDeferReader::SkipKeys(doc, index);
    SetRecord(resolve(doc[index].Position + pos));
}

void DefAttributeKeyTypeRecord::WriteDefer(BamlDocument& doc, int index,
    BamlBinaryWriter& writer) {
    index = BamlDeferReader::SkipKeys(doc, index);
    writer.SetPosition(pos);
    writer.WriteUInt32(
        static_cast<std::uint32_t>(Record()->Position - doc[index].Position));
}

void DefAttributeKeyStringRecord::ReadData(BamlBinaryReader& reader, int) {
    ValueId = reader.ReadUInt16();
    pos = reader.ReadUInt32();
    Shared = reader.ReadBoolean();
    SharedSet = reader.ReadBoolean();
}

void DefAttributeKeyStringRecord::WriteData(BamlBinaryWriter& writer) {
    writer.WriteUInt16(ValueId);
    pos = static_cast<std::uint32_t>(writer.Position());
    writer.WriteUInt32(0);
    writer.WriteBoolean(Shared);
    writer.WriteBoolean(SharedSet);
}

void DefAttributeKeyStringRecord::ReadDefer(BamlDocument& doc, int index,
    const std::function<BamlRecord*(std::int64_t)>& resolve) {
    index = BamlDeferReader::SkipKeys(doc, index);
    SetRecord(resolve(doc[index].Position + pos));
}

void DefAttributeKeyStringRecord::WriteDefer(BamlDocument& doc, int index,
    BamlBinaryWriter& writer) {
    index = BamlDeferReader::SkipKeys(doc, index);
    writer.SetPosition(pos);
    writer.WriteUInt32(
        static_cast<std::uint32_t>(Record()->Position - doc[index].Position));
}

void OptimizedStaticResourceRecord::Read(BamlBinaryReader& reader) {
    Flags = reader.ReadByte();
    ValueId = reader.ReadUInt16();
}

void OptimizedStaticResourceRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteByte(Flags);
    writer.WriteUInt16(ValueId);
}

void LineNumberAndPositionRecord::Read(BamlBinaryReader& reader) {
    LineNumber = reader.ReadUInt32();
    LinePosition = reader.ReadUInt32();
}

void LineNumberAndPositionRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt32(LineNumber);
    writer.WriteUInt32(LinePosition);
}

void LinePositionRecord::Read(BamlBinaryReader& reader) {
    LinePosition = reader.ReadUInt32();
}

void LinePositionRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt32(LinePosition);
}

void NamedElementStartRecord::Read(BamlBinaryReader& reader) {
    TypeId = reader.ReadUInt16();
    RuntimeName = reader.ReadString();
}

// The C# writes the name only when it is non-null; the reader always assigns
// it, so the null arm is unreachable through the port's std::string (see
// BamlRecords.hpp).
void NamedElementStartRecord::Write(BamlBinaryWriter& writer) {
    writer.WriteUInt16(TypeId);
    writer.WriteString(RuntimeName);
}

} // namespace ILSpy::BamlDecompiler::Baml
