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

// Tests for the BAML binary-format layer (BamlRecords/BamlReader/BamlWriter,
// the first Phase-9 slice): the reader primitives, the signature/header
// checks, the record dispatch with every record's field read, the defer
// resolution and its malformed-block arms, and the writer round trip.
//
// Every expectation is gold-pinned against the REAL ICSharpCode.BamlDecompiler
// from the installed ilspycmd 11.0 tool, driven over the identical fixture
// bytes through the C:/temp-probe/BamlProbe reflection probes:
//  * the field dumps (out/*.dump.txt) pin every record's parsed fields,
//  * the write probes (BamlWriteProbe) pin the real writer's byte output,
//  * the failure fixtures (make_failures.py) pin every reader rejection
//    arm's exact exception type and message,
//  * the primitive probes (BamlHeaderProbe / BinReadProbe) pin the
//    System.IO.BinaryReader semantics the reader builds on (the short
//    ReadChars return, the ReadBytes short array, the 7-bit wrap and cap,
//    the argument-out-of-range message).

#include "BamlDecompiler/Baml/BamlBinaryReader.hpp"
#include "BamlDecompiler/Baml/BamlReader.hpp"
#include "BamlDecompiler/Baml/BamlRecords.hpp"
#include "BamlDecompiler/Baml/BamlWriter.hpp"

#include "BamlDecompiler/BamlTestSupport.hpp"
#include "TestFixtures/RealBaml.hpp"
#include "TestFixtures/SynthBaml.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace Baml = ILSpy::BamlDecompiler::Baml;
using Baml::BamlDocument;
using Baml::BamlRecord;
using Baml::BamlRecordType;

// The shared record-type name table (BamlTestSupport.hpp).
using ILSpy::BamlDecompiler::Baml::RecordTypeName;

// Reads a fixture's bytes through the reader (the byte-span convention;
// the shared helper's twin, kept for the suite's existing call sites).
BamlDocument ReadBytes(const std::string& bytes) {
    return ILSpy::Tests::Baml::ReadBaml(bytes);
}

// The failing reader call: the exception's message (every mapped
// exception type carries the exact C# message).
std::string ReadThrowsMessage(const std::string& bytes) {
    try {
        ReadBytes(bytes);
    } catch (const std::exception& ex) {
        return ex.what();
    }
    return "<no throw>";
}

std::string IsBamlHeaderThrowsMessage(const std::string& bytes) {
    try {
        Baml::IsBamlHeader(reinterpret_cast<const std::uint8_t*>(bytes.data()),
            bytes.size());
    } catch (const std::exception& ex) {
        return ex.what();
    }
    return "<no throw>";
}

template <typename T>
const T& As(const BamlRecord& record) {
    return static_cast<const T&>(record);
}

void ExpectRecord(const BamlDocument& doc, std::size_t index,
    BamlRecordType type, std::int64_t position) {
    ASSERT_LT(index, doc.Count());
    EXPECT_EQ(doc[index].Type(), type)
        << "record " << index << ": " << RecordTypeName(doc[index].Type());
    EXPECT_EQ(doc[index].Position, position)
        << "record " << index << " (" << RecordTypeName(type) << ")";
}

// --- The reader primitives ---------------------------------------------------

TEST(BamlReaderTest, PrimitivesFixedWidthReads) {
    const std::uint8_t bytes[] = {
        0x2A, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0xFF,
    };
    Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
    EXPECT_EQ(reader.ReadByte(), 0x2A);
    EXPECT_EQ(reader.ReadUInt16(), 0x0201);
    EXPECT_EQ(reader.ReadUInt32(), 0x06050403);
    EXPECT_TRUE(reader.ReadBoolean());
    EXPECT_EQ(reader.Position(), 8);
}

TEST(BamlReaderTest, PrimitivesInt32AndDouble) {
    // The ReadInt32/ReadDouble primitives the Xaml layer consumes
    // (XamlUtils::ReadXamlDouble's scaled int32 and tag-5 double forms).
    const std::uint8_t bytes[] = {
        0xB4, 0x00, 0xB2, 0x00,             // int32 0x00B200B4
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x40,  // double 2.5
        0x01, 0x00, 0x00, 0xFF,             // int32 0xFF000001
    };
    Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
    EXPECT_EQ(reader.ReadInt32(), 0x00B200B4);
    EXPECT_EQ(reader.ReadDouble(), 2.5);
    EXPECT_EQ(reader.ReadInt32(), static_cast<std::int32_t>(0xFF000001u));
    EXPECT_EQ(reader.Position(), 16);

    // The negative-zero and NaN bit patterns survive the round trip.
    const std::uint8_t zeros[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF8, 0x7F,
    };
    Baml::BamlBinaryReader reader2(zeros, sizeof(zeros));
    const double negZero = reader2.ReadDouble();
    EXPECT_EQ(negZero, 0.0);
    EXPECT_TRUE(std::signbit(negZero));
    EXPECT_TRUE(std::isnan(reader2.ReadDouble()));
}

TEST(BamlReaderTest, PrimitivesInt32AndDoubleTruncatedThrow) {
    const std::uint8_t fiveBytes[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    {
        Baml::BamlBinaryReader reader(fiveBytes, 5);
        reader.ReadInt32();
        EXPECT_THROW(reader.ReadInt32(), std::out_of_range);
    }
    {
        Baml::BamlBinaryReader reader(fiveBytes, 5);
        EXPECT_THROW(reader.ReadDouble(), std::out_of_range);
    }
}

TEST(BamlReaderTest, PrimitivesTruncatedReadsThrow) {
    const std::uint8_t twoBytes[] = { 0x01, 0x02 };
    {
        Baml::BamlBinaryReader reader(twoBytes, 2);
        reader.ReadByte();
        reader.ReadByte();
        EXPECT_THROW(reader.ReadByte(), std::out_of_range);
    }
    {
        Baml::BamlBinaryReader reader(twoBytes, 2);
        reader.ReadByte();
        EXPECT_THROW(reader.ReadUInt16(), std::out_of_range);
    }
    {
        const std::uint8_t twoBytes[] = { 0x01, 0x41 };
        Baml::BamlBinaryReader reader(twoBytes, 2);
        EXPECT_EQ(reader.ReadString(), "A");
        EXPECT_THROW(reader.ReadString(), std::out_of_range);
    }
    EXPECT_STREQ(IsBamlHeaderThrowsMessage(std::string("\x01", 1)).c_str(),
        "Unable to read beyond the end of the stream.");
}

TEST(BamlReaderTest, PrimitivesEncodedInt) {
    // Single, two, three, and four-byte encodings.
    struct Case {
        std::size_t offset;
        std::size_t length;
        int expected;
    };
    const std::uint8_t encodings[] = {
        0x7F,
        0x80, 0x01,
        0x80, 0x80, 0x01,
        0x80, 0x80, 0x80, 0x01,
    };
    const Case cases[] = {
        { 0, 1, 0x7F },
        { 1, 2, 0x80 },
        { 3, 3, 0x4000 },
        { 6, 4, 0x200000 },
    };
    for (const Case& c : cases) {
        Baml::BamlBinaryReader reader(encodings + c.offset, c.length);
        EXPECT_EQ(reader.ReadEncodedInt(), c.expected) << "encoding at " << c.offset;
    }
}

TEST(BamlReaderTest, PrimitivesEncodedIntWrapAndCap) {
    // The 5-byte wrap prefix (FF FF FF FF 0F) decodes to -1 (the shift
    // count masks to 5 bits); the probed FormatException message fires on
    // the 6th continuation byte.
    {
        const std::uint8_t wrap[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x0F, 'a' };
        Baml::BamlBinaryReader reader(wrap, sizeof(wrap));
        EXPECT_THROW(reader.ReadString(), std::out_of_range);
        try {
            Baml::BamlBinaryReader reader2(wrap, sizeof(wrap));
            reader2.ReadString();
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(),
                "BinaryReader encountered an invalid string length of -1 characters.");
        }
    }
    {
        const std::uint8_t six[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
        Baml::BamlBinaryReader reader(six, sizeof(six));
        try {
            reader.ReadEncodedInt();
            FAIL();
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(),
                "Too many bytes in what should have been a 7-bit encoded integer.");
        }
    }
}

TEST(BamlReaderTest, PrimitivesReadString) {
    const std::uint8_t bytes[] = { 0x05, 'h', 'e', 'l', 'l', 'o', 0x00 };
    Baml::BamlBinaryReader reader(bytes, sizeof(bytes));
    EXPECT_EQ(reader.ReadString(), "hello");
    EXPECT_EQ(reader.ReadString(), "");
    EXPECT_EQ(reader.Position(), 7);

    // A truncated byte payload throws the EndOfStream message.
    const std::uint8_t truncated[] = { 0x05, 'a' };
    Baml::BamlBinaryReader reader2(truncated, sizeof(truncated));
    try {
        reader2.ReadString();
        FAIL();
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Unable to read beyond the end of the stream.");
    }

    // A multi-byte length prefix (200 = 0xC8 0x01).
    std::vector<std::uint8_t> longInput{ 0xC8, 0x01 };
    longInput.resize(2 + 200, 'x');
    Baml::BamlBinaryReader reader3(longInput.data(), longInput.size());
    EXPECT_EQ(reader3.ReadString().size(), 200u);
}

TEST(BamlReaderTest, PrimitivesReadBytes) {
    const std::uint8_t bytes[] = { 1, 2, 3 };
    {
        // A short stream returns the available bytes (no throw).
        Baml::BamlBinaryReader reader(bytes, 3);
        std::vector<std::uint8_t> shortRead = reader.ReadBytes(10);
        ASSERT_EQ(shortRead.size(), 3u);
        EXPECT_EQ(shortRead[0], 1);
        EXPECT_EQ(shortRead[2], 3);
        EXPECT_EQ(reader.Position(), 3);
    }
    {
        // Zero returns the empty vector.
        Baml::BamlBinaryReader reader(bytes, 3);
        EXPECT_TRUE(reader.ReadBytes(0).empty());
    }
    {
        // A negative count throws the probed ArgumentOutOfRangeException
        // message (the PropertyCustom lying-size arm).
        Baml::BamlBinaryReader reader(bytes, 3);
        try {
            reader.ReadBytes(-1);
            FAIL();
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(),
                "count ('-1') must be a non-negative value. (Parameter 'count')\n"
                "Actual value was -1.");
        }
    }
}

TEST(BamlReaderTest, PrimitivesReadChars) {
    // The Encoding.Unicode reader: complete UTF-16 units only; a short
    // stream returns the units it can form (the odd tail byte drops).
    {
        const std::uint8_t empty[] = { 0 };
        Baml::BamlBinaryReader reader(empty, 0);
        EXPECT_EQ(reader.ReadChars(6), "");
    }
    {
        const std::uint8_t five[] = { 'A', 0, 'B', 0, 'C' };
        Baml::BamlBinaryReader reader(five, sizeof(five));
        EXPECT_EQ(reader.ReadChars(3), "AB");
        EXPECT_EQ(reader.Position(), 4);
    }
    {
        const std::uint8_t seven[] = { 'A', 0, 'B', 0, 'C', 0, 'D' };
        Baml::BamlBinaryReader reader(seven, sizeof(seven));
        EXPECT_EQ(reader.ReadChars(3), "ABC");
        EXPECT_EQ(reader.Position(), 6);
    }
}

// --- IsBamlHeader ------------------------------------------------------------

TEST(BamlReaderTest, IsBamlHeaderMatrix) {
    // The "MSBAML" signature as UTF-16LE (12 bytes; the explicit length keeps
    // the embedded NULs).
    const std::string msbamlUtf16("M\0S\0B\0A\0M\0L\0", 12);
    // The 12-length signature: true.
    std::string valid(std::string("\x0c\x00\x00\x00", 4) + msbamlUtf16);
    EXPECT_TRUE(Baml::IsBamlHeader(
        reinterpret_cast<const std::uint8_t*>(valid.data()), valid.size()));
    // The odd 13-length form passes the same halved check: true.
    std::string odd(std::string("\x0d\x00\x00\x00", 4) + msbamlUtf16 + "\x00");
    EXPECT_TRUE(Baml::IsBamlHeader(
        reinterpret_cast<const std::uint8_t*>(odd.data()), odd.size()));
    // A length other than 12/13 (10 -> 5 chars): false, no chars read.
    std::string wrongLength(std::string("\x0a\x00\x00\x00", 4)
        + std::string(16, '\0'));
    EXPECT_FALSE(Baml::IsBamlHeader(
        reinterpret_cast<const std::uint8_t*>(wrongLength.data()),
        wrongLength.size()));
    // Wrong chars: false.
    std::string wrongChars(std::string("\x0c\x00\x00\x00", 4)
        + std::string("M\0S\0B\0A\0M\0X\0", 12));
    EXPECT_FALSE(Baml::IsBamlHeader(
        reinterpret_cast<const std::uint8_t*>(wrongChars.data()), wrongChars.size()));
    // A stream shorter than the 4-byte length prefix throws (the C# lets
    // the BinaryReader throw escape IsBamlHeader).
    EXPECT_STREQ(IsBamlHeaderThrowsMessage(std::string("\x0c\x00\x00", 3)).c_str(),
        "Unable to read beyond the end of the stream.");
    // The fixtures themselves: true.
    const std::string synth = ILSpy::Tests::SynthBamlBytes();
    EXPECT_TRUE(Baml::IsBamlHeader(
        reinterpret_cast<const std::uint8_t*>(synth.data()), synth.size()));
}

// --- ReadDocument over the synthetic fixture --------------------------------

// The whole 52-record field matrix, pinned against the real reader's dump
// (C:/temp-probe/BamlProbe/out/synth.dump.txt).
TEST(BamlReaderTest, SynthDocumentFields) {
    BamlDocument doc = ReadBytes(ILSpy::Tests::SynthBamlBytes());

    EXPECT_EQ(doc.Signature, "MSBAML");
    EXPECT_EQ(doc.ReaderVersion.Major, 0);
    EXPECT_EQ(doc.ReaderVersion.Minor, 0x60);
    EXPECT_EQ(doc.UpdaterVersion.Major, 0);
    EXPECT_EQ(doc.UpdaterVersion.Minor, 0x60);
    EXPECT_EQ(doc.WriterVersion.Major, 0);
    EXPECT_EQ(doc.WriterVersion.Minor, 0x60);
    ASSERT_EQ(doc.Count(), 52u);

    ExpectRecord(doc, 0, BamlRecordType::DocumentStart, 28);
    EXPECT_FALSE(As<Baml::DocumentStartRecord>(doc[0]).LoadAsync);
    EXPECT_EQ(As<Baml::DocumentStartRecord>(doc[0]).MaxAsyncRecords, 7u);
    EXPECT_FALSE(As<Baml::DocumentStartRecord>(doc[0]).DebugBaml);

    ExpectRecord(doc, 1, BamlRecordType::XmlnsProperty, 35);
    EXPECT_EQ(As<Baml::XmlnsPropertyRecord>(doc[1]).Prefix, "p");
    EXPECT_EQ(As<Baml::XmlnsPropertyRecord>(doc[1]).XmlNamespace, "urn:pw");
    const auto& assemblyIds = As<Baml::XmlnsPropertyRecord>(doc[1]).AssemblyIds;
    ASSERT_EQ(assemblyIds.size(), 2u);
    EXPECT_EQ(assemblyIds[0], 1);
    EXPECT_EQ(assemblyIds[1], 2);

    ExpectRecord(doc, 2, BamlRecordType::PIMapping, 52);
    EXPECT_EQ(As<Baml::PIMappingRecord>(doc[2]).AssemblyId, 0);
    EXPECT_EQ(As<Baml::PIMappingRecord>(doc[2]).ClrNamespace, "pw");
    EXPECT_EQ(As<Baml::PIMappingRecord>(doc[2]).XmlNamespace, "urn:pw");

    ExpectRecord(doc, 3, BamlRecordType::AssemblyInfo, 66);
    EXPECT_EQ(As<Baml::AssemblyInfoRecord>(doc[3]).AssemblyId, 0);
    EXPECT_EQ(As<Baml::AssemblyInfoRecord>(doc[3]).AssemblyFullName,
        "PresentationUI, Version=4.0.0.0");

    ExpectRecord(doc, 4, BamlRecordType::StringInfo, 102);
    EXPECT_EQ(As<Baml::StringInfoRecord>(doc[4]).StringId, 5);
    EXPECT_EQ(As<Baml::StringInfoRecord>(doc[4]).Value, "hello");

    ExpectRecord(doc, 5, BamlRecordType::TypeInfo, 112);
    EXPECT_EQ(As<Baml::TypeInfoRecord>(doc[5]).TypeId, 0);
    EXPECT_EQ(As<Baml::TypeInfoRecord>(doc[5]).AssemblyId, 0);
    EXPECT_EQ(As<Baml::TypeInfoRecord>(doc[5]).TypeFullName, "Sys.T");

    ExpectRecord(doc, 6, BamlRecordType::TypeSerializerInfo, 124);
    EXPECT_EQ(As<Baml::TypeSerializerInfoRecord>(doc[6]).TypeId, 1);
    EXPECT_EQ(As<Baml::TypeSerializerInfoRecord>(doc[6]).AssemblyId, 0);
    EXPECT_EQ(As<Baml::TypeSerializerInfoRecord>(doc[6]).TypeFullName, "Sys.Ser");
    EXPECT_EQ(As<Baml::TypeSerializerInfoRecord>(doc[6]).SerializerTypeId, 9);

    ExpectRecord(doc, 7, BamlRecordType::AttributeInfo, 140);
    EXPECT_EQ(As<Baml::AttributeInfoRecord>(doc[7]).AttributeId, 0);
    EXPECT_EQ(As<Baml::AttributeInfoRecord>(doc[7]).OwnerTypeId, 0);
    EXPECT_EQ(As<Baml::AttributeInfoRecord>(doc[7]).AttributeUsage, 0);
    EXPECT_EQ(As<Baml::AttributeInfoRecord>(doc[7]).Name, "Name");

    ExpectRecord(doc, 8, BamlRecordType::ConstructorParametersStart, 152);
    ExpectRecord(doc, 9, BamlRecordType::ConstructorParameterType, 153);
    EXPECT_EQ(As<Baml::ConstructorParameterTypeRecord>(doc[9]).TypeId, 7);
    ExpectRecord(doc, 10, BamlRecordType::ConstructorParametersEnd, 156);

    ExpectRecord(doc, 11, BamlRecordType::ElementStart, 157);
    EXPECT_EQ(As<Baml::ElementStartRecord>(doc[11]).TypeId, 0);
    EXPECT_EQ(As<Baml::ElementStartRecord>(doc[11]).Flags, 0);

    // NamedElementStart reads TypeId then the name (the flags byte is not
    // part of its payload; the field stays 0).
    ExpectRecord(doc, 12, BamlRecordType::NamedElementStart, 161);
    EXPECT_EQ(As<Baml::NamedElementStartRecord>(doc[12]).TypeId, 1);
    EXPECT_EQ(As<Baml::NamedElementStartRecord>(doc[12]).Flags, 0);
    EXPECT_EQ(As<Baml::NamedElementStartRecord>(doc[12]).RuntimeName, "e1");

    ExpectRecord(doc, 13, BamlRecordType::ElementEnd, 167);

    ExpectRecord(doc, 14, BamlRecordType::ContentProperty, 168);
    EXPECT_EQ(As<Baml::ContentPropertyRecord>(doc[14]).AttributeId, 3);

    ExpectRecord(doc, 15, BamlRecordType::Property, 171);
    EXPECT_EQ(As<Baml::PropertyRecord>(doc[15]).AttributeId, 0);
    EXPECT_EQ(As<Baml::PropertyRecord>(doc[15]).Value, "v");

    ExpectRecord(doc, 16, BamlRecordType::PropertyWithConverter, 177);
    EXPECT_EQ(As<Baml::PropertyWithConverterRecord>(doc[16]).AttributeId, 0);
    EXPECT_EQ(As<Baml::PropertyWithConverterRecord>(doc[16]).Value, "v");
    EXPECT_EQ(As<Baml::PropertyWithConverterRecord>(doc[16]).ConverterTypeId, 8);

    ExpectRecord(doc, 17, BamlRecordType::PropertyCustom, 185);
    EXPECT_EQ(As<Baml::PropertyCustomRecord>(doc[17]).AttributeId, 1);
    EXPECT_EQ(As<Baml::PropertyCustomRecord>(doc[17]).SerializerTypeId, 2);
    const std::uint8_t expectedData[] = { 0xde, 0xad, 0xbe, 0xef };
    EXPECT_EQ(As<Baml::PropertyCustomRecord>(doc[17]).Data,
        std::vector<std::uint8_t>(expectedData, expectedData + 4));

    ExpectRecord(doc, 18, BamlRecordType::PropertyWithExtension, 195);
    EXPECT_EQ(As<Baml::PropertyWithExtensionRecord>(doc[18]).AttributeId, 4);
    EXPECT_EQ(As<Baml::PropertyWithExtensionRecord>(doc[18]).Flags, 33);
    EXPECT_EQ(As<Baml::PropertyWithExtensionRecord>(doc[18]).ValueId, 9);

    ExpectRecord(doc, 19, BamlRecordType::Text, 202);
    EXPECT_EQ(As<Baml::TextRecord>(doc[19]).Value, "text");

    ExpectRecord(doc, 20, BamlRecordType::TextWithConverter, 209);
    EXPECT_EQ(As<Baml::TextWithConverterRecord>(doc[20]).Value, "tc");
    EXPECT_EQ(As<Baml::TextWithConverterRecord>(doc[20]).ConverterTypeId, 8);

    ExpectRecord(doc, 21, BamlRecordType::TextWithId, 216);
    EXPECT_EQ(As<Baml::TextWithIdRecord>(doc[21]).ValueId, 5);

    ExpectRecord(doc, 22, BamlRecordType::LiteralContent, 220);
    EXPECT_EQ(As<Baml::LiteralContentRecord>(doc[22]).Value, "lit");
    EXPECT_EQ(As<Baml::LiteralContentRecord>(doc[22]).Reserved0, 17u);
    EXPECT_EQ(As<Baml::LiteralContentRecord>(doc[22]).Reserved1, 34u);

    // The read order is AttributeId then Value.
    ExpectRecord(doc, 23, BamlRecordType::RoutedEvent, 234);
    EXPECT_EQ(As<Baml::RoutedEventRecord>(doc[23]).AttributeId, 2);
    EXPECT_EQ(As<Baml::RoutedEventRecord>(doc[23]).Value, "Click");

    ExpectRecord(doc, 24, BamlRecordType::PresentationOptionsAttribute, 244);
    EXPECT_EQ(As<Baml::PresentationOptionsAttributeRecord>(doc[24]).Value, "ign");
    EXPECT_EQ(As<Baml::PresentationOptionsAttributeRecord>(doc[24]).NameId, 64);

    ExpectRecord(doc, 25, BamlRecordType::LineNumberAndPosition, 252);
    EXPECT_EQ(As<Baml::LineNumberAndPositionRecord>(doc[25]).LineNumber, 100u);
    EXPECT_EQ(As<Baml::LineNumberAndPositionRecord>(doc[25]).LinePosition, 7u);

    ExpectRecord(doc, 26, BamlRecordType::LinePosition, 261);
    EXPECT_EQ(As<Baml::LinePositionRecord>(doc[26]).LinePosition, 8u);

    ExpectRecord(doc, 27, BamlRecordType::PropertyComplexStart, 266);
    EXPECT_EQ(As<Baml::PropertyComplexStartRecord>(doc[27]).AttributeId, 5);

    ExpectRecord(doc, 28, BamlRecordType::PropertyStringReference, 269);
    EXPECT_EQ(As<Baml::PropertyStringReferenceRecord>(doc[28]).AttributeId, 5);
    EXPECT_EQ(As<Baml::PropertyStringReferenceRecord>(doc[28]).StringId, 5);

    ExpectRecord(doc, 29, BamlRecordType::PropertyTypeReference, 274);
    EXPECT_EQ(As<Baml::PropertyTypeReferenceRecord>(doc[29]).AttributeId, 5);
    EXPECT_EQ(As<Baml::PropertyTypeReferenceRecord>(doc[29]).TypeId, 1);

    ExpectRecord(doc, 30, BamlRecordType::PropertyListStart, 279);
    EXPECT_EQ(As<Baml::PropertyListStartRecord>(doc[30]).AttributeId, 6);

    ExpectRecord(doc, 31, BamlRecordType::StaticResourceId, 282);
    EXPECT_EQ(As<Baml::StaticResourceIdRecord>(doc[31]).StaticResourceId, 0);

    ExpectRecord(doc, 32, BamlRecordType::PropertyWithStaticResourceId, 285);
    EXPECT_EQ(As<Baml::PropertyWithStaticResourceIdRecord>(doc[32]).AttributeId, 7);
    EXPECT_EQ(As<Baml::PropertyWithStaticResourceIdRecord>(doc[32]).StaticResourceId, 0);

    ExpectRecord(doc, 33, BamlRecordType::PropertyListEnd, 290);

    ExpectRecord(doc, 34, BamlRecordType::PropertyArrayStart, 291);
    EXPECT_EQ(As<Baml::PropertyArrayStartRecord>(doc[34]).AttributeId, 8);

    ExpectRecord(doc, 35, BamlRecordType::OptimizedStaticResource, 294);
    EXPECT_EQ(As<Baml::OptimizedStaticResourceRecord>(doc[35]).Flags, 3);
    EXPECT_TRUE(As<Baml::OptimizedStaticResourceRecord>(doc[35]).IsType());
    EXPECT_TRUE(As<Baml::OptimizedStaticResourceRecord>(doc[35]).IsStatic());
    EXPECT_EQ(As<Baml::OptimizedStaticResourceRecord>(doc[35]).ValueId, 85);

    ExpectRecord(doc, 36, BamlRecordType::PropertyArrayEnd, 298);

    ExpectRecord(doc, 37, BamlRecordType::PropertyDictionaryStart, 299);
    EXPECT_EQ(As<Baml::PropertyDictionaryStartRecord>(doc[37]).AttributeId, 9);

    ExpectRecord(doc, 38, BamlRecordType::PropertyDictionaryEnd, 302);
    ExpectRecord(doc, 39, BamlRecordType::PropertyComplexEnd, 303);

    ExpectRecord(doc, 40, BamlRecordType::DefAttribute, 304);
    EXPECT_EQ(As<Baml::DefAttributeRecord>(doc[40]).Value, "k");
    EXPECT_EQ(As<Baml::DefAttributeRecord>(doc[40]).NameId, 1);

    ExpectRecord(doc, 41, BamlRecordType::ElementEnd, 310);

    // The defer block: every defer record resolves at the terminator
    // record's position (48, the ElementStart @342), pinning the SkipKeys
    // walk (over the StaticResource and KeyElement key subtrees) and the
    // pos arithmetic.
    ExpectRecord(doc, 42, BamlRecordType::DeferableContentStart, 311);
    const Baml::DeferableContentStartRecord& deferable =
        As<Baml::DeferableContentStartRecord>(doc[42]);
    ASSERT_NE(deferable.Record(), nullptr);
    EXPECT_EQ(deferable.Record()->Position, 342);
    EXPECT_EQ(deferable.Record()->Type(), BamlRecordType::ElementStart);

    ExpectRecord(doc, 43, BamlRecordType::DefAttributeKeyString, 316);
    const Baml::DefAttributeKeyStringRecord& keyString =
        As<Baml::DefAttributeKeyStringRecord>(doc[43]);
    EXPECT_EQ(keyString.ValueId, 5);
    EXPECT_FALSE(keyString.Shared);
    EXPECT_TRUE(keyString.SharedSet);
    ASSERT_NE(keyString.Record(), nullptr);
    EXPECT_EQ(keyString.Record()->Position, 342);
    EXPECT_EQ(keyString.Record()->Type(), BamlRecordType::ElementStart);

    ExpectRecord(doc, 44, BamlRecordType::StaticResourceStart, 326);
    EXPECT_EQ(As<Baml::StaticResourceStartRecord>(doc[44]).TypeId, 1);
    EXPECT_EQ(As<Baml::StaticResourceStartRecord>(doc[44]).Flags, 0);

    ExpectRecord(doc, 45, BamlRecordType::StaticResourceEnd, 330);

    ExpectRecord(doc, 46, BamlRecordType::KeyElementStart, 331);
    const Baml::DefAttributeKeyTypeRecord& keyElement =
        As<Baml::DefAttributeKeyTypeRecord>(doc[46]);
    EXPECT_EQ(keyElement.TypeId, 1);
    EXPECT_EQ(keyElement.Flags, 0);
    EXPECT_FALSE(keyElement.Shared);
    EXPECT_FALSE(keyElement.SharedSet);
    ASSERT_NE(keyElement.Record(), nullptr);
    EXPECT_EQ(keyElement.Record()->Position, 342);
    EXPECT_EQ(keyElement.Record()->Type(), BamlRecordType::ElementStart);

    ExpectRecord(doc, 47, BamlRecordType::KeyElementEnd, 341);

    ExpectRecord(doc, 48, BamlRecordType::ElementStart, 342);
    EXPECT_EQ(As<Baml::ElementStartRecord>(doc[48]).TypeId, 2);
    EXPECT_EQ(As<Baml::ElementStartRecord>(doc[48]).Flags, 0);

    ExpectRecord(doc, 49, BamlRecordType::Text, 346);
    EXPECT_EQ(As<Baml::TextRecord>(doc[49]).Value, "t");

    ExpectRecord(doc, 50, BamlRecordType::ElementEnd, 350);
    ExpectRecord(doc, 51, BamlRecordType::DocumentEnd, 351);
}

// The type sequence: all 52 records in order (the full dispatch matrix).
TEST(BamlReaderTest, SynthDocumentTypeSequence) {
    BamlDocument doc = ReadBytes(ILSpy::Tests::SynthBamlBytes());
    static const BamlRecordType expected[] = {
        BamlRecordType::DocumentStart,
        BamlRecordType::XmlnsProperty,
        BamlRecordType::PIMapping,
        BamlRecordType::AssemblyInfo,
        BamlRecordType::StringInfo,
        BamlRecordType::TypeInfo,
        BamlRecordType::TypeSerializerInfo,
        BamlRecordType::AttributeInfo,
        BamlRecordType::ConstructorParametersStart,
        BamlRecordType::ConstructorParameterType,
        BamlRecordType::ConstructorParametersEnd,
        BamlRecordType::ElementStart,
        BamlRecordType::NamedElementStart,
        BamlRecordType::ElementEnd,
        BamlRecordType::ContentProperty,
        BamlRecordType::Property,
        BamlRecordType::PropertyWithConverter,
        BamlRecordType::PropertyCustom,
        BamlRecordType::PropertyWithExtension,
        BamlRecordType::Text,
        BamlRecordType::TextWithConverter,
        BamlRecordType::TextWithId,
        BamlRecordType::LiteralContent,
        BamlRecordType::RoutedEvent,
        BamlRecordType::PresentationOptionsAttribute,
        BamlRecordType::LineNumberAndPosition,
        BamlRecordType::LinePosition,
        BamlRecordType::PropertyComplexStart,
        BamlRecordType::PropertyStringReference,
        BamlRecordType::PropertyTypeReference,
        BamlRecordType::PropertyListStart,
        BamlRecordType::StaticResourceId,
        BamlRecordType::PropertyWithStaticResourceId,
        BamlRecordType::PropertyListEnd,
        BamlRecordType::PropertyArrayStart,
        BamlRecordType::OptimizedStaticResource,
        BamlRecordType::PropertyArrayEnd,
        BamlRecordType::PropertyDictionaryStart,
        BamlRecordType::PropertyDictionaryEnd,
        BamlRecordType::PropertyComplexEnd,
        BamlRecordType::DefAttribute,
        BamlRecordType::ElementEnd,
        BamlRecordType::DeferableContentStart,
        BamlRecordType::DefAttributeKeyString,
        BamlRecordType::StaticResourceStart,
        BamlRecordType::StaticResourceEnd,
        BamlRecordType::KeyElementStart,
        BamlRecordType::KeyElementEnd,
        BamlRecordType::ElementStart,
        BamlRecordType::Text,
        BamlRecordType::ElementEnd,
        BamlRecordType::DocumentEnd,
    };
    ASSERT_EQ(doc.Count(), sizeof(expected) / sizeof(expected[0]));
    for (std::size_t i = 0; i < doc.Count(); i++)
        EXPECT_EQ(doc[i].Type(), expected[i]) << "record " << i;
}

// --- ReadDocument over the real fixtures -------------------------------------

TEST(BamlReaderTest, FindToolbarDocumentFields) {
    const std::string bytes = ILSpy::Tests::FindToolbarBamlBytes();
    BamlDocument doc = ReadBytes(bytes);
    ASSERT_EQ(doc.Count(), 997u);
    EXPECT_EQ(doc.Signature, "MSBAML");
    EXPECT_EQ(doc.ReaderVersion.Minor, 0x60);

    ExpectRecord(doc, 0, BamlRecordType::DocumentStart, 28);
    EXPECT_EQ(As<Baml::DocumentStartRecord>(doc[0]).MaxAsyncRecords, 0xFFFFFFFFu);

    ExpectRecord(doc, 1, BamlRecordType::AssemblyInfo, 35);
    EXPECT_EQ(As<Baml::AssemblyInfoRecord>(doc[1]).AssemblyId, 0);
    EXPECT_EQ(As<Baml::AssemblyInfoRecord>(doc[1]).AssemblyFullName,
        "PresentationUI, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=31bf3856ad364e35");

    ExpectRecord(doc, 2, BamlRecordType::PIMapping, 121);
    EXPECT_EQ(As<Baml::PIMappingRecord>(doc[2]).ClrNamespace,
        "System.Windows.Documents");
    EXPECT_EQ(As<Baml::PIMappingRecord>(doc[2]).XmlNamespace,
        "clr-namespace:System.Windows.Documents");

    ExpectRecord(doc, 4, BamlRecordType::TypeInfo, 257);
    EXPECT_EQ(As<Baml::TypeInfoRecord>(doc[4]).AssemblyId, 4096);
    EXPECT_EQ(As<Baml::TypeInfoRecord>(doc[4]).TypeFullName,
        "MS.Internal.Documents.FindToolBar");

    ExpectRecord(doc, 5, BamlRecordType::ElementStart, 297);
    EXPECT_EQ(As<Baml::ElementStartRecord>(doc[5]).TypeId, 0);

    // A real PropertyCustom record (a serializer-driven value).
    ExpectRecord(doc, 21, BamlRecordType::PropertyCustom, 990);
    EXPECT_EQ(As<Baml::PropertyCustomRecord>(doc[21]).AttributeId, 2);
    EXPECT_EQ(As<Baml::PropertyCustomRecord>(doc[21]).SerializerTypeId, 46);
    ASSERT_EQ(As<Baml::PropertyCustomRecord>(doc[21]).Data.size(), 1u);
    EXPECT_EQ(As<Baml::PropertyCustomRecord>(doc[21]).Data[0], 1);

    // The real defer block: DeferableContentStart @1773 resolves past the
    // whole record stream at @7152, and the key strings walk to their own
    // targets.
    ExpectRecord(doc, 67, BamlRecordType::DeferableContentStart, 1773);
    const auto& deferable = As<Baml::DeferableContentStartRecord>(doc[67]);
    ASSERT_NE(deferable.Record(), nullptr);
    EXPECT_EQ(deferable.Record()->Position, 7152);
    EXPECT_EQ(deferable.Record()->Type(), BamlRecordType::ElementEnd);

    ExpectRecord(doc, 68, BamlRecordType::DefAttributeKeyString, 1778);
    const auto& key0 = As<Baml::DefAttributeKeyStringRecord>(doc[68]);
    EXPECT_EQ(key0.ValueId, 0);
    EXPECT_FALSE(key0.Shared);
    EXPECT_FALSE(key0.SharedSet);
    ASSERT_NE(key0.Record(), nullptr);
    EXPECT_EQ(key0.Record()->Position, 1960);
    EXPECT_EQ(key0.Record()->Type(), BamlRecordType::ElementStart);

    ExpectRecord(doc, 69, BamlRecordType::DefAttributeKeyString, 1788);
    EXPECT_EQ(As<Baml::DefAttributeKeyStringRecord>(doc[69]).ValueId, 1);
    EXPECT_EQ(As<Baml::DefAttributeKeyStringRecord>(doc[69]).Record()->Position, 1994);
}

TEST(BamlReaderTest, InstallationErrorDocumentFields) {
    const std::string bytes = ILSpy::Tests::InstallationErrorBamlBytes();
    BamlDocument doc = ReadBytes(bytes);
    ASSERT_EQ(doc.Count(), 443u);

    ExpectRecord(doc, 1, BamlRecordType::AssemblyInfo, 35);
    EXPECT_EQ(As<Baml::AssemblyInfoRecord>(doc[1]).AssemblyFullName, "PresentationUI");

    ExpectRecord(doc, 2, BamlRecordType::TypeInfo, 54);
    EXPECT_EQ(As<Baml::TypeInfoRecord>(doc[2]).AssemblyId, 4096);
    EXPECT_EQ(As<Baml::TypeInfoRecord>(doc[2]).TypeFullName,
        "Microsoft.Internal.DeploymentUI.InstallationErrorPage");

    ExpectRecord(doc, 3, BamlRecordType::ElementStart, 114);

    ExpectRecord(doc, 4, BamlRecordType::AssemblyInfo, 118);
    EXPECT_EQ(As<Baml::AssemblyInfoRecord>(doc[4]).AssemblyFullName,
        "WindowsBase, Version=4.0.0.0, Culture=neutral, "
        "PublicKeyToken=31bf3856ad364e35");

    // The real defer block with a DefAttributeKeyType key.
    ExpectRecord(doc, 321, BamlRecordType::DeferableContentStart, 3457);
    ASSERT_NE(As<Baml::DeferableContentStartRecord>(doc[321]).Record(), nullptr);
    EXPECT_EQ(As<Baml::DeferableContentStartRecord>(doc[321]).Record()->Position, 3661);
    EXPECT_EQ(As<Baml::DeferableContentStartRecord>(doc[321]).Record()->Type(),
        BamlRecordType::ElementEnd);

    ExpectRecord(doc, 322, BamlRecordType::DefAttributeKeyType, 3462);
}

// --- The reader rejection arms -----------------------------------------------

TEST(BamlReaderTest, FailureArms) {
    const std::string synth = ILSpy::Tests::SynthBamlBytes();
    ASSERT_EQ(synth.size(), 352u);

    // A raw length other than 12/13 (10): the invalid-signature-length arm.
    std::string badLength = synth;
    badLength[0] = 10; badLength[1] = badLength[2] = badLength[3] = 0;
    EXPECT_STREQ(ReadThrowsMessage(badLength).c_str(),
        "Invalid BAML signature length.");

    // The 12-byte prefix-only form (u32(10) + zeros): the same arm.
    EXPECT_STREQ(ReadThrowsMessage(std::string("\x0a\x00\x00\x00", 4)
        + std::string(8, '\0')).c_str(), "Invalid BAML signature length.");

    // A signature length prefix of 12 over an empty stream: ReadChars
    // returns "" (no throw), so the mismatch throws the parameterless
    // NotSupportedException.
    EXPECT_STREQ(ReadThrowsMessage(std::string("\x0c\x00\x00\x00", 4)).c_str(),
        "Specified method is not supported.");

    // Wrong signature chars: the same throw after a valid length.
    std::string wrongSig = synth;
    wrongSig[15] = 'X';
    EXPECT_STREQ(ReadThrowsMessage(wrongSig).c_str(),
        "Specified method is not supported.");

    // A wrong version byte (ReaderVersion.Minor 0x61): the same throw.
    std::string badVersion = synth;
    badVersion[18] = 0x61;
    EXPECT_STREQ(ReadThrowsMessage(badVersion).c_str(),
        "Specified method is not supported.");

    // An unknown record type byte (0x99 after a DocumentStart).
    std::string unknown = synth.substr(0, 28)
        + std::string("\x01\x00\x07\x00\x00\x00\x00", 7) + "\x99";
    EXPECT_STREQ(ReadThrowsMessage(unknown).c_str(),
        "Specified method is not supported.");

    // A record cut mid-payload (@173, mid Property): EndOfStream.
    EXPECT_STREQ(ReadThrowsMessage(synth.substr(0, 173)).c_str(),
        "Unable to read beyond the end of the stream.");

    // A defer record pointing one byte into the target record: the
    // offset-not-a-boundary arm (the DefAttributeKeyString @316 whose pos
    // field sits at 320, patched to 1).
    std::string deferBad = synth;
    deferBad[320] = 1; deferBad[321] = deferBad[322] = deferBad[323] = 0;
    EXPECT_STREQ(ReadThrowsMessage(deferBad).c_str(),
        "BAML defer record points at an offset that is not a record boundary.");

    // A PropertyCustom record whose size prefix lies below its own header
    // fields: the negative ReadBytes count arm (the probed
    // ArgumentOutOfRangeException message).
    std::string lyingSize = synth.substr(0, 28)
        + std::string("\x06\x04\x00\x01\x00\x02", 6);
    EXPECT_STREQ(ReadThrowsMessage(lyingSize).c_str(),
        "count ('-1') must be a non-negative value. (Parameter 'count')\n"
        "Actual value was -1.");
}

// The odd 13-length signature: accepted (the halved check), the records
// start after the 3-byte padding (probed: the real reader parses the doc
// with its first record at 31).
TEST(BamlReaderTest, OddLengthSignatureDoc) {
    const std::string msbamlUtf16("M\0S\0B\0A\0M\0L\0", 12);
    std::string doc(std::string("\x0d\x00\x00\x00", 4) + msbamlUtf16
        + std::string("\x00\x00\x00", 3)
        // The three versions (Major 0, Minor 0x60 each).
        + std::string("\x00\x00\x60\x00\x00\x00\x60\x00\x00\x00\x60\x00", 12)
        // A single DocumentEnd record.
        + "\x02");
    BamlDocument parsed = ReadBytes(doc);
    ASSERT_EQ(parsed.Count(), 1u);
    EXPECT_EQ(parsed[0].Type(), BamlRecordType::DocumentEnd);
    EXPECT_EQ(parsed[0].Position, 31);
}

// --- The defer-key walk -------------------------------------------------------

TEST(BamlReaderTest, DeferReaderSkipKeysWalks) {
    BamlDocument doc = ReadBytes(ILSpy::Tests::SynthBamlBytes());
    // The synth defer block: the key records at 43 walk past the
    // StaticResource and KeyElement subtrees to the terminator at 48.
    EXPECT_EQ(Baml::BamlDeferReader::SkipKeys(doc, 43), 48);
    // A start index already past the keys lands on the terminator itself
    // (the default arm steps back one, then forward one).
    EXPECT_EQ(Baml::BamlDeferReader::SkipKeys(doc, 48), 48);

    // The real defer block: the key records at 68 (the first
    // DefAttributeKeyString) run through the whole OptimizedStaticResource
    // and DefAttributeKeyType key list to the first non-key record, the
    // ElementStart @1960 (the key string's resolved target).
    BamlDocument findToolbar = ReadBytes(ILSpy::Tests::FindToolbarBamlBytes());
    EXPECT_EQ(Baml::BamlDeferReader::SkipKeys(findToolbar, 68), 97);
}

TEST(BamlReaderTest, DeferReaderMalformedArms) {
    // Keys that run off the end of the record list.
    {
        BamlDocument doc;
        doc.Add(std::make_unique<Baml::DefAttributeKeyStringRecord>());
        doc.Add(std::make_unique<Baml::DefAttributeKeyStringRecord>());
        try {
            Baml::BamlDeferReader::SkipKeys(doc, 0);
            FAIL();
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(),
                "Malformed BAML defer block: ran off the end of the record list while scanning keys.");
        }
    }
    // A start record with no matching end record.
    {
        BamlDocument doc;
        doc.Add(std::make_unique<Baml::StaticResourceStartRecord>());
        doc.Add(std::make_unique<Baml::ElementEndRecord>());
        try {
            Baml::BamlDeferReader::SkipKeys(doc, 0);
            FAIL();
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(),
                "Malformed BAML defer block: a start record has no matching end record.");
        }
    }
    // The nesting cap: a chain of nested start records deeper than the
    // maximum supported depth (the crafted-resource hardening).
    {
        BamlDocument doc;
        for (int i = 0; i < 1200; i++)
            doc.Add(std::make_unique<Baml::StaticResourceStartRecord>());
        try {
            Baml::BamlDeferReader::SkipKeys(doc, 0);
            FAIL();
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(),
                "Malformed BAML defer block: nested start records exceed the maximum supported depth.");
        }
    }
}

// --- The writer ---------------------------------------------------------------

// The port's WriteDocument over the synth doc is byte-identical to the
// REAL writer's output (the write gold), pinning every record's WriteData
// including the RoutedEvent asymmetry (the write order is Value then
// AttributeId; the read order is the reverse) and the defer patches.
TEST(BamlReaderTest, SynthWriteDocumentMatchesRealWriter) {
    BamlDocument doc = ReadBytes(ILSpy::Tests::SynthBamlBytes());
    std::vector<std::uint8_t> out;
    Baml::WriteDocument(doc, out);

    const std::string gold = ILSpy::Tests::SynthBamlWriteGoldBytes();
    ASSERT_EQ(out.size(), gold.size());
    EXPECT_EQ(0, std::memcmp(out.data(), gold.data(), gold.size()));

    // The RoutedEvent divergence: the written payload differs from the
    // fixture exactly at the 8 payload bytes (236..243).
    const std::string synth = ILSpy::Tests::SynthBamlBytes();
    ASSERT_EQ(synth.size(), out.size());
    for (std::size_t i = 0; i < out.size(); i++) {
        if (i >= 236 && i < 244)
            EXPECT_NE(out[i], static_cast<std::uint8_t>(synth[i])) << "byte " << i;
        else
            EXPECT_EQ(out[i], static_cast<std::uint8_t>(synth[i])) << "byte " << i;
    }
}

// The real fixtures round-trip byte-identically (the real writer's output
// over both is the input itself, probed).
TEST(BamlReaderTest, RealFixtureWriteRoundTrips) {
    for (const std::string bytes : {
        ILSpy::Tests::FindToolbarBamlBytes(),
        ILSpy::Tests::InstallationErrorBamlBytes(),
    }) {
        BamlDocument doc = ReadBytes(bytes);
        std::vector<std::uint8_t> out;
        Baml::WriteDocument(doc, out);
        ASSERT_EQ(out.size(), bytes.size());
        EXPECT_EQ(0, std::memcmp(out.data(), bytes.data(), bytes.size()));

        // The written bytes re-read to the same document.
        BamlDocument reparsed = ReadBytes(
            std::string(reinterpret_cast<const char*>(out.data()), out.size()));
        ASSERT_EQ(reparsed.Count(), doc.Count());
        for (std::size_t i = 0; i < doc.Count(); i++) {
            EXPECT_EQ(reparsed[i].Type(), doc[i].Type()) << "record " << i;
            EXPECT_EQ(reparsed[i].Position, doc[i].Position) << "record " << i;
        }
    }
}

// The written defer patches land exactly where the placeholders were and
// carry the original distances: the DeferableContentStart size field
// (@312..315) keeps the distance to its target record, and the key
// records' pos fields (@320..323, @335..338) keep the distance from the
// defer terminator (the ElementStart @342) to the target.
TEST(BamlReaderTest, WriteDeferPatches) {
    BamlDocument doc = ReadBytes(ILSpy::Tests::SynthBamlBytes());
    std::vector<std::uint8_t> out;
    Baml::WriteDocument(doc, out);
    const std::string synth = ILSpy::Tests::SynthBamlBytes();
    ASSERT_EQ(out.size(), synth.size());
    for (std::size_t i = 312; i < 316; i++)
        EXPECT_EQ(out[i], static_cast<std::uint8_t>(synth[i]))
            << "DeferableContentStart size field byte " << i;
    for (std::size_t i = 320; i < 324; i++)
        EXPECT_EQ(out[i], 0) << "DefAttributeKeyString pos field byte " << i;
    for (std::size_t i = 335; i < 339; i++)
        EXPECT_EQ(out[i], 0) << "KeyElementStart pos field byte " << i;
}

} // namespace
