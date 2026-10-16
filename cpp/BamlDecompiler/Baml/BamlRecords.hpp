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
// the BAML record type enum, the BamlRecord/SizedBamlRecord bases, the
// IBamlDeferRecord interface with its BamlDeferReader key-skip machinery,
// and every record class the reader dispatches to.
//
// C#-to-C++ porting decisions:
//  * Class order differs from the C# file: C++ needs base classes declared
//    before derived (the C# defines KeyElementStartRecord before its base
//    DefAttributeKeyTypeRecord and the Property*Start records before
//    PropertyComplexStartRecord). The classes themselves are 1:1.
//  * The C# `internal` fields (the defer records' pos/size) are public
//    here (the reader/writer state machine they carry is part of the
//    layer's own contract).
//  * The defer records' `Record` property (the resolved target) is a
//    non-owning pointer at a record of the same document (the C#
//    reference into the document's list).
//  * NamedElementStartRecord's Write skips a null RuntimeName in the C#
//    (the null-vs-empty distinction): the port's std::string has no null
//    state, so its Write always emits the name. Only a hand-constructed
//    record can be null in the C# (the reader always assigns it), so the
//    only divergence is an artificially constructed record, which no
//    ported caller creates.
//  * RoutedEventRecord's payload is asymmetric in the C# itself: ReadData
//    reads AttributeId then Value, WriteData writes Value then AttributeId
//    (a quirk faithfully reproduced -- a written RoutedEvent record does
//    not read back to the same values).

#pragma once

#include "BamlDecompiler/Baml/BamlDocument.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::BamlDecompiler::Baml {

class BamlBinaryReader;
class BamlBinaryWriter;

// The C# `BamlRecordType : byte` enum.
enum class BamlRecordType : std::uint8_t {
    ClrEvent = 0x13,
    Comment = 0x17,
    AssemblyInfo = 0x1c,
    AttributeInfo = 0x1f,
    ConstructorParametersStart = 0x2a,
    ConstructorParametersEnd = 0x2b,
    ConstructorParameterType = 0x2c,
    ConnectionId = 0x2d,
    ContentProperty = 0x2e,
    DefAttribute = 0x19,
    DefAttributeKeyString = 0x26,
    DefAttributeKeyType = 0x27,
    DeferableContentStart = 0x25,
    DefTag = 0x18,
    DocumentEnd = 0x02,
    DocumentStart = 0x01,
    ElementEnd = 0x04,
    ElementStart = 0x03,
    EndAttributes = 0x1a,
    KeyElementEnd = 0x29,
    KeyElementStart = 0x28,
    LastRecordType = 0x39,
    LineNumberAndPosition = 0x35,
    LinePosition = 0x36,
    LiteralContent = 0x0f,
    NamedElementStart = 0x2f,
    OptimizedStaticResource = 0x37,
    PIMapping = 0x1b,
    PresentationOptionsAttribute = 0x34,
    ProcessingInstruction = 0x16,
    Property = 0x05,
    PropertyArrayEnd = 0x0a,
    PropertyArrayStart = 0x09,
    PropertyComplexEnd = 0x08,
    PropertyComplexStart = 0x07,
    PropertyCustom = 0x06,
    PropertyDictionaryEnd = 0x0e,
    PropertyDictionaryStart = 0x0d,
    PropertyListEnd = 0x0c,
    PropertyListStart = 0x0b,
    PropertyStringReference = 0x21,
    PropertyTypeReference = 0x22,
    PropertyWithConverter = 0x24,
    PropertyWithExtension = 0x23,
    PropertyWithStaticResourceId = 0x38,
    RoutedEvent = 0x12,
    StaticResourceEnd = 0x31,
    StaticResourceId = 0x32,
    StaticResourceStart = 0x30,
    StringInfo = 0x20,
    Text = 0x10,
    TextWithConverter = 0x11,
    TextWithId = 0x33,
    TypeInfo = 0x1d,
    TypeSerializerInfo = 0x1e,
    XmlAttribute = 0x15,
    XmlnsProperty = 0x14,
};

// The C# `BamlRecordType.ToString()`: the enum member name, or the decimal
// byte value for a value with no member (the .NET enum ToString fallback).
// The HandlerMap's duplicate-key message renders its key through this
// spelling (the Dictionary.Add `Key: {key}` suffix).
std::string RecordTypeName(BamlRecordType type);

// The C# `BamlRecord` base.
class BamlRecord {
public:
    virtual ~BamlRecord() = default;

    virtual BamlRecordType Type() const = 0;

    // The C# `public long Position { get; internal set; }`: the absolute
    // stream offset of the record's type byte (assigned by ReadDocument
    // and WriteDocument).
    std::int64_t Position = 0;

    virtual void Read(BamlBinaryReader& reader) = 0;
    virtual void Write(BamlBinaryWriter& writer) = 0;
};

// The C# `IBamlDeferRecord`: a record whose payload carries a file offset
// that only resolves after the whole document is read (the defer blocks'
// key records precede their targets). Implementations store the resolved
// target as a non-owning pointer at a record of the same document.
class IBamlDeferRecord {
public:
    virtual ~IBamlDeferRecord() = default;

    // The C# `Record` property: the resolved defer target.
    virtual BamlRecord* Record() const = 0;
    virtual void SetRecord(BamlRecord* record) = 0;

    virtual void ReadDefer(BamlDocument& doc, int index,
        const std::function<BamlRecord*(std::int64_t)>& resolve) = 0;
    virtual void WriteDefer(BamlDocument& doc, int index, BamlBinaryWriter& writer) = 0;
};

// The C# `SizedBamlRecord`: a record whose payload is prefixed by the
// 7-bit-encoded total size of the record including that prefix itself
// (the type byte is not counted).
class SizedBamlRecord : public BamlRecord {
public:
    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;

protected:
    virtual void ReadData(BamlBinaryReader& reader, int size) = 0;
    virtual void WriteData(BamlBinaryWriter& writer) = 0;

    // The C# private SizeofEncodedInt: the byte length of a value's
    // 7-bit encoding.
    static int SizeofEncodedInt(int value);
};

// The C# `BamlDeferReader` static class: walks past the key records that
// precede a defer block's target. The record offsets are part of the BAML
// payload and therefore attacker-controlled, so every list access is
// bounded and the nesting walk is depth-capped: a crafted resource fails
// with a catchable exception instead of reading past the end of the record
// list or recursing until the process dies with an uncatchable
// StackOverflowException.
struct BamlDeferReader {
    // Legitimate BAML defer blocks nest only a handful of levels; this cap
    // sits far below the stack limit, so it never rejects real input but
    // stops a crafted chain of nested start records before it overflows.
    static constexpr int MaxNestingDepth = 1000;

    // Advances past the leading key records of a defer block and returns
    // the index of the record that terminates it.
    static int SkipKeys(BamlDocument& doc, int index);

private:
    static void NavigateTree(BamlDocument& doc, BamlRecordType start,
        BamlRecordType end, int& index, int depth);
    static std::out_of_range Malformed(const std::string& detail);
};

// ---------------------------------------------------------------------------
// The record classes (C# BamlRecords.cs order, adjusted for base-first).
// ---------------------------------------------------------------------------

class XmlnsPropertyRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::XmlnsProperty; }

    std::string Prefix;
    std::string XmlNamespace;
    std::vector<std::uint16_t> AssemblyIds;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class PresentationOptionsAttributeRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PresentationOptionsAttribute; }

    std::string Value;
    std::uint16_t NameId = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class PIMappingRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PIMapping; }

    std::string XmlNamespace;
    std::string ClrNamespace;
    std::uint16_t AssemblyId = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class AssemblyInfoRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::AssemblyInfo; }

    std::uint16_t AssemblyId = 0;
    std::string AssemblyFullName;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class PropertyRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::Property; }

    std::uint16_t AttributeId = 0;
    std::string Value;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class PropertyWithConverterRecord : public PropertyRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyWithConverter; }

    std::uint16_t ConverterTypeId = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class PropertyCustomRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyCustom; }

    std::uint16_t AttributeId = 0;
    std::uint16_t SerializerTypeId = 0;
    std::vector<std::uint8_t> Data;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class DefAttributeRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::DefAttribute; }

    std::string Value;
    std::uint16_t NameId = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class TypeInfoRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::TypeInfo; }

    std::uint16_t TypeId = 0;
    std::uint16_t AssemblyId = 0;
    std::string TypeFullName;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class TypeSerializerInfoRecord : public TypeInfoRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::TypeSerializerInfo; }

    std::uint16_t SerializerTypeId = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class AttributeInfoRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::AttributeInfo; }

    std::uint16_t AttributeId = 0;
    std::uint16_t OwnerTypeId = 0;
    std::uint8_t AttributeUsage = 0;
    std::string Name;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class StringInfoRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::StringInfo; }

    std::uint16_t StringId = 0;
    std::string Value;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class TextRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::Text; }

    std::string Value;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class TextWithConverterRecord : public TextRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::TextWithConverter; }

    std::uint16_t ConverterTypeId = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class TextWithIdRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::TextWithId; }

    std::uint16_t ValueId = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class LiteralContentRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::LiteralContent; }

    std::string Value;
    std::uint32_t Reserved0 = 0;
    std::uint32_t Reserved1 = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class RoutedEventRecord : public SizedBamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::RoutedEvent; }

    std::string Value;
    std::uint16_t AttributeId = 0;
    // Never read (a reserved column the reader discards).
    std::uint32_t Reserved1 = 0;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;
};

class DocumentStartRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::DocumentStart; }

    bool LoadAsync = false;
    std::uint32_t MaxAsyncRecords = 0;
    bool DebugBaml = false;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class DocumentEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::DocumentEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class ElementStartRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::ElementStart; }

    std::uint16_t TypeId = 0;
    std::uint8_t Flags = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class ElementEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::ElementEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

// The C# DefAttributeKeyTypeRecord: an ElementStart payload extended by the
// defer-block key fields (the offset placeholder, Shared, SharedSet).
class DefAttributeKeyTypeRecord : public ElementStartRecord, public IBamlDeferRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::DefAttributeKeyType; }

    bool Shared = false;
    bool SharedSet = false;
    // The C# `internal uint pos`: the absolute offset of this record's own
    // 4-byte offset placeholder (the write pass records it, the defer patch
    // seeks to it); 0xffffffff before the first write.
    std::uint32_t pos = 0xffffffff;

    BamlRecord* Record() const override { return record_; }
    void SetRecord(BamlRecord* record) override { record_ = record; }

    void ReadDefer(BamlDocument& doc, int index,
        const std::function<BamlRecord*(std::int64_t)>& resolve) override;
    void WriteDefer(BamlDocument& doc, int index, BamlBinaryWriter& writer) override;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;

private:
    BamlRecord* record_ = nullptr;
};

class KeyElementStartRecord : public DefAttributeKeyTypeRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::KeyElementStart; }
};

class KeyElementEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::KeyElementEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class StaticResourceStartRecord : public ElementStartRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::StaticResourceStart; }
};

class StaticResourceEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::StaticResourceEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class ConnectionIdRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::ConnectionId; }

    std::uint32_t ConnectionId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class PropertyWithExtensionRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyWithExtension; }

    std::uint16_t AttributeId = 0;
    std::uint16_t Flags = 0;
    std::uint16_t ValueId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class PropertyComplexStartRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyComplexStart; }

    std::uint16_t AttributeId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class PropertyTypeReferenceRecord : public PropertyComplexStartRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyTypeReference; }

    std::uint16_t TypeId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class PropertyStringReferenceRecord : public PropertyComplexStartRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyStringReference; }

    std::uint16_t StringId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class PropertyListStartRecord : public PropertyComplexStartRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyListStart; }
};

class PropertyListEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyListEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class PropertyDictionaryStartRecord : public PropertyComplexStartRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyDictionaryStart; }
};

class PropertyDictionaryEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyDictionaryEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class PropertyArrayStartRecord : public PropertyComplexStartRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyArrayStart; }
};

class PropertyArrayEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyArrayEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class PropertyComplexEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyComplexEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class StaticResourceIdRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::StaticResourceId; }

    std::uint16_t StaticResourceId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class PropertyWithStaticResourceIdRecord : public StaticResourceIdRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::PropertyWithStaticResourceId; }

    std::uint16_t AttributeId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class ContentPropertyRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::ContentProperty; }

    std::uint16_t AttributeId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class ConstructorParametersStartRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::ConstructorParametersStart; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class ConstructorParametersEndRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::ConstructorParametersEnd; }

    void Read(BamlBinaryReader&) override {}
    void Write(BamlBinaryWriter&) override {}
};

class ConstructorParameterTypeRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::ConstructorParameterType; }

    std::uint16_t TypeId = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

// The C# DeferableContentStartRecord: the marker that opens a defer block;
// its size field carries the distance to the deferred content's start.
class DeferableContentStartRecord : public BamlRecord, public IBamlDeferRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::DeferableContentStart; }

    // The C# `internal long pos`: after Read, the absolute offset just past
    // the size field (the record's end); after Write, the absolute offset
    // of the size field itself. ReadDefer resolves `pos + size`; WriteDefer
    // seeks to `pos` and writes `Record.Position - (pos + 4)` -- the two
    // rounds are inverse (read: pos = size field + 4).
    std::int64_t pos = 0;
    // The C# `internal uint size`: the distance from just past the size
    // field to the deferred content's first record.
    std::uint32_t size = 0xffffffff;

    BamlRecord* Record() const override { return record_; }
    void SetRecord(BamlRecord* record) override { record_ = record; }

    void ReadDefer(BamlDocument& doc, int index,
        const std::function<BamlRecord*(std::int64_t)>& resolve) override;
    void WriteDefer(BamlDocument& doc, int index, BamlBinaryWriter& writer) override;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;

private:
    BamlRecord* record_ = nullptr;
};

// The C# DefAttributeKeyStringRecord: a defer-block key carrying the value
// as a string-table id plus the offset placeholder.
class DefAttributeKeyStringRecord : public SizedBamlRecord, public IBamlDeferRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::DefAttributeKeyString; }

    std::uint16_t ValueId = 0;
    bool Shared = false;
    bool SharedSet = false;
    // The C# `internal uint pos`: the absolute offset of this record's own
    // 4-byte offset placeholder; 0xffffffff before the first write.
    std::uint32_t pos = 0xffffffff;

    BamlRecord* Record() const override { return record_; }
    void SetRecord(BamlRecord* record) override { record_ = record; }

    void ReadDefer(BamlDocument& doc, int index,
        const std::function<BamlRecord*(std::int64_t)>& resolve) override;
    void WriteDefer(BamlDocument& doc, int index, BamlBinaryWriter& writer) override;

protected:
    void ReadData(BamlBinaryReader& reader, int size) override;
    void WriteData(BamlBinaryWriter& writer) override;

private:
    BamlRecord* record_ = nullptr;
};

class OptimizedStaticResourceRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::OptimizedStaticResource; }

    std::uint8_t Flags = 0;
    std::uint16_t ValueId = 0;

    bool IsType() const { return (Flags & 1) != 0; }
    bool IsStatic() const { return (Flags & 2) != 0; }

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class LineNumberAndPositionRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::LineNumberAndPosition; }

    std::uint32_t LineNumber = 0;
    std::uint32_t LinePosition = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

class LinePositionRecord : public BamlRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::LinePosition; }

    std::uint32_t LinePosition = 0;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

// The C# NamedElementStartRecord: an ElementStart whose payload carries the
// element's runtime name instead of the flags byte (Read skips Flags; Write
// emits TypeId then the name).
class NamedElementStartRecord : public ElementStartRecord {
public:
    BamlRecordType Type() const override { return BamlRecordType::NamedElementStart; }

    std::string RuntimeName;

    void Read(BamlBinaryReader& reader) override;
    void Write(BamlBinaryWriter& writer) override;
};

} // namespace ILSpy::BamlDecompiler::Baml
