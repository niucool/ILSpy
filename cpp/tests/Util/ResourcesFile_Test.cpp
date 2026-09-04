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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The ResourcesFile port (the .resources container reader): the header
// parse and the entry-name listing. The happy-path fixtures are the real
// System.Resources.ResourceWriter output (the 348-byte synthetic container
// in ResourcesTestFixtures.hpp, whose five entries were dumped through
// the real ICSharpCode.Decompiler ResourcesFile) and the real mscorlib
// 4.8 mscorlib.resources blob (353031 bytes, 3172 entries, read through the
// port's own MetadataFile embedded-resource read); the failure arms are
// hand-crafted blobs pinning every C# BadImageFormatException /
// EndOfStreamException message the constructor and GetResourceName render.

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Util/ResourcesFile.hpp"

#include "TestFixtures/ResourcesTestFixtures.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Util::ResourcesFile;
using ILSpy::Tests::TestResourcesBytes;

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// A little-endian int32 blob helper.
std::vector<std::uint8_t> Le32(std::uint32_t v) {
    return {static_cast<std::uint8_t>(v),
        static_cast<std::uint8_t>(v >> 8),
        static_cast<std::uint8_t>(v >> 16),
        static_cast<std::uint8_t>(v >> 24)};
}

void Append(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& in) {
    out.insert(out.end(), in.begin(), in.end());
}

// A BinaryWriter.Write(string) helper: the 7-bit-encoded length then the
// UTF-8 bytes (the reader type / resource set type / type table strings).
void AppendString(std::vector<std::uint8_t>& out, const std::string& s) {
    std::uint32_t len = static_cast<std::uint32_t>(s.size());
    while (len >= 0x80) {
        out.push_back(static_cast<std::uint8_t>((len & 0x7F) | 0x80));
        len >>= 7;
    }
    out.push_back(static_cast<std::uint8_t>(len));
    out.insert(out.end(), s.begin(), s.end());
}

// The ResourceManager V1 header prefix (magic + versions + the two type
// strings), the shape every hand-crafted blob starts with.
std::vector<std::uint8_t> ResourceManagerV1Header(const std::string& readerType,
    const std::string& resourceSetType) {
    std::vector<std::uint8_t> out;
    Append(out, Le32(0xBEEFCACE));
    Append(out, Le32(1));  // resMgrHeaderVersion 1: the strings path
    Append(out, Le32(0));  // numBytesToSkip (unused on the V1 path)
    AppendString(out, readerType);
    AppendString(out, resourceSetType);
    return out;
}

// A complete .resources blob: the V1 header, the resource-set version, the
// resource count, the (empty) type table, the aligned name-hash array, the
// name positions, the data-section offset, then the name and data section
// bytes. The data-section offset defaults to right after the name section
// (a valid file); the override pins the failure arms that need a bad
// offset. The C# `MagicNumber` is 0xBEEFCACE.
std::vector<std::uint8_t> MakeResources(std::uint32_t numResources,
    const std::vector<std::uint32_t>& namePositions,
    const std::vector<std::uint8_t>& nameAndDataSection,
    std::optional<std::uint32_t> dataSectionOffset = std::nullopt) {
    auto build = [&](std::uint32_t offset) {
        std::vector<std::uint8_t> out = ResourceManagerV1Header(
            "reader", "set");
        Append(out, Le32(2));  // the resource set version
        Append(out, Le32(numResources));
        Append(out, Le32(0));  // numTypes (an empty type table)
        // The 8-byte alignment of the name-hash array: the C# computes
        // the current position mod 8 and pads to the boundary.
        while (out.size() % 8 != 0) out.push_back(0x50);  // 'P'
        out.insert(out.end(), numResources * 4, 0);  // the name hashes
        for (std::uint32_t pos : namePositions) Append(out, Le32(pos));
        Append(out, Le32(offset));
        return out;
    };
    // The fixed-width fields make the header size stable across the two
    // passes, so the real offset is computable from a placeholder build.
    std::uint32_t offset = dataSectionOffset.value_or(
        static_cast<std::uint32_t>(build(0).size()
            + nameAndDataSection.size()));
    std::vector<std::uint8_t> out = build(offset);
    Append(out, nameAndDataSection);
    return out;
}

// The one-entry name and data section: the name "A" (7-bit length 2, the
// UTF-16LE bytes, data offset 0) then a Null value.
std::vector<std::uint8_t> MakeOneEntryTail() {
    return {0x02, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00};
}

}  // namespace

// ---- The ResourceWriter-produced container: the authoritative happy path

// The five entries the real C# ResourcesFile reports over the fixture: the
// names in row order (the name positions are hash-ordered, NOT sorted --
// the row order is the namePositions array order) and each entry's value
// offset relative to the data section (the out-parameter side channel).
TEST(ResourcesFileTest, ResourceWriterContainerParses)
{
    std::string bytes = TestResourcesBytes();
    ResourcesFile rf(reinterpret_cast<const std::uint8_t*>(bytes.data()),
        bytes.size());
    EXPECT_EQ(rf.ResourceCount(), 5);
    EXPECT_EQ(rf.Version(), 2);
    // The "System.Resources.ResourceReader, mscorlib, ..." reader type is
    // NOT the System.Resources.Extensions DeserializingResourceReader, so
    // usesSerializationFormat is false.
    EXPECT_FALSE(rf.UsesSerializationFormat());

    EXPECT_EQ(rf.GetResourceName(0), "Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87");
    EXPECT_EQ(rf.GetResourceName(1), "Alpha");
    EXPECT_EQ(rf.GetResourceName(2), "Gamma");
    EXPECT_EQ(rf.GetResourceName(3), "Delta");
    EXPECT_EQ(rf.GetResourceName(4), "Beta");

    // The data offsets (the values' positions relative to the data
    // section -- 313 in the fixture): Alpha 0, Beta 5, Delta 10, Gamma 12,
    // and the Unicode name's entry at 20.
    EXPECT_EQ(rf.GetResourceDataOffset(0), 20);
    EXPECT_EQ(rf.GetResourceDataOffset(1), 0);
    EXPECT_EQ(rf.GetResourceDataOffset(2), 12);
    EXPECT_EQ(rf.GetResourceDataOffset(3), 10);
    EXPECT_EQ(rf.GetResourceDataOffset(4), 5);

    // The two-argument form returns the same name and fills the offset.
    int dataOffset = -1;
    EXPECT_EQ(rf.GetResourceName(1, dataOffset), "Alpha");
    EXPECT_EQ(dataOffset, 0);
}

// An out-of-range entry index throws (the C# IndexOutOfRangeException --
// not one of the caught kinds).
TEST(ResourcesFileTest, ResourceIndexOutOfRangeThrows)
{
    std::string bytes = TestResourcesBytes();
    ResourcesFile rf(reinterpret_cast<const std::uint8_t*>(bytes.data()),
        bytes.size());
    EXPECT_THROW(rf.GetResourceName(5), std::out_of_range);
    EXPECT_THROW(rf.GetResourceName(-1), std::out_of_range);
}

// The real mscorlib 4.8 mscorlib.resources blob, read through the port's
// own embedded-resource read: 353031 bytes behind the length prefix, 3172
// entries (probed with the real C# pair: PEFile.Resources +
// ResourcesFile).
TEST(ResourcesFileTest, MscorlibResourcesContainerParses)
{
    MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    auto data = mscorlib.TryGetManifestResourceData(0x28000006u);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->size(), 353031u);
    ResourcesFile rf(data->data(), data->size());
    EXPECT_EQ(rf.ResourceCount(), 3172);
    EXPECT_EQ(rf.Version(), 2);
    EXPECT_FALSE(rf.UsesSerializationFormat());
    EXPECT_EQ(rf.GetResourceName(0), "Format_MissingIncompleteDate");
    EXPECT_EQ(rf.GetResourceName(1), "Interop.COM_TypeMismatch");
    EXPECT_EQ(rf.GetResourceName(2), "Cryptography_Padding_Win2KEnhOnly");
    EXPECT_EQ(rf.GetResourceName(3171),
        "Argument_BadPersistableModuleInTransientAssembly");
}

// ---- The header-parse failure arms (the C# BadImageFormatException
// messages, rendered as std::out_of_range)

TEST(ResourcesFileTest, BadMagicNumberThrows)
{
    std::vector<std::uint8_t> bytes = Le32(0xDEADBEEF);
    try {
        ResourcesFile rf(bytes.data(), bytes.size());
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Not a .resources file - invalid magic number");
    }
}

TEST(ResourcesFileTest, NegativeSkipOrHeaderVersionThrows)
{
    // numBytesToSkip < 0.
    {
        std::vector<std::uint8_t> bytes;
        Append(bytes, Le32(0xBEEFCACE));
        Append(bytes, Le32(1));
        Append(bytes, Le32(0x80000000));
        try {
            ResourcesFile rf(bytes.data(), bytes.size());
            FAIL() << "expected std::out_of_range";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Resources header corrupted.");
        }
    }
    // resMgrHeaderVersion < 0.
    {
        std::vector<std::uint8_t> bytes;
        Append(bytes, Le32(0xBEEFCACE));
        Append(bytes, Le32(0x80000000));
        Append(bytes, Le32(0));
        try {
            ResourcesFile rf(bytes.data(), bytes.size());
            FAIL() << "expected std::out_of_range";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Resources header corrupted.");
        }
    }
}

// The resMgrHeaderVersion > 1 path: the reader seeks numBytesToSkip bytes
// past the two version ints instead of reading the two strings (the V2
// ResourceManager header shape).
TEST(ResourcesFileTest, HeaderVersionSkipPathParses)
{
    std::vector<std::uint8_t> out;
    Append(out, Le32(0xBEEFCACE));
    Append(out, Le32(2));  // resMgrHeaderVersion 2: the skip path
    // numBytesToSkip: the two type strings (everything between the skip
    // count and the resource-set version).
    std::string readerType = "System.Resources.ResourceReader, mscorlib, "
        "Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089";
    std::string resourceSetType = "System.Resources.RuntimeResourceSet";
    std::vector<std::uint8_t> strings;
    AppendString(strings, readerType);
    AppendString(strings, resourceSetType);
    Append(out, Le32(static_cast<std::uint32_t>(strings.size())));
    Append(out, strings);
    Append(out, Le32(2));
    Append(out, Le32(1));  // one resource
    Append(out, Le32(0));  // no types
    while (out.size() % 8 != 0) out.push_back(0x50);
    out.insert(out.end(), 4, 0);  // one name hash
    Append(out, Le32(0));  // namePositions[0] = 0
    std::vector<std::uint8_t> tail = MakeOneEntryTail();
    // The data section sits right after the name entry (the fixed-width
    // fields make the offset computable ahead of the append).
    Append(out, Le32(static_cast<std::uint32_t>(out.size() + 4 + tail.size())));
    Append(out, tail);
    ResourcesFile rf(out.data(), out.size());
    EXPECT_EQ(rf.ResourceCount(), 1);
    EXPECT_EQ(rf.GetResourceName(0), "A");
    EXPECT_FALSE(rf.UsesSerializationFormat());
}

// A skip count that lands past the end of the blob: the seek itself
// succeeds (the C# MemoryStream allows it) and the next read throws.
TEST(ResourcesFileTest, SkipPastEndThrowsOnRead)
{
    std::vector<std::uint8_t> bytes;
    Append(bytes, Le32(0xBEEFCACE));
    Append(bytes, Le32(2));  // the skip path
    Append(bytes, Le32(1000000));
    try {
        ResourcesFile rf(bytes.data(), bytes.size());
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range&) {
        // The EndOfStreamException arm.
    }
}

TEST(ResourcesFileTest, UnsupportedResourceSetVersionThrows)
{
    std::vector<std::uint8_t> bytes;
    Append(bytes, Le32(0xBEEFCACE));
    Append(bytes, Le32(1));
    Append(bytes, Le32(0));
    AppendString(bytes, "reader");
    AppendString(bytes, "set");
    Append(bytes, Le32(3));  // neither 1 nor 2
    try {
        ResourcesFile rf(bytes.data(), bytes.size());
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Unsupported resource set version: 3");
    }
}

// numResources < 0, numTypes < 0, a negative name position, a negative
// data-section offset, and a data section before the name section all
// render the corrupted-header message.
TEST(ResourcesFileTest, CorruptedHeaderFieldsThrow)
{
    // numResources < 0.
    {
        std::vector<std::uint8_t> bytes;
        Append(bytes, Le32(0xBEEFCACE));
        Append(bytes, Le32(1));
        Append(bytes, Le32(0));
        AppendString(bytes, "reader");
        AppendString(bytes, "set");
        Append(bytes, Le32(2));
        Append(bytes, Le32(0x80000000));
        try {
            ResourcesFile rf(bytes.data(), bytes.size());
            FAIL() << "expected std::out_of_range";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Resources header corrupted.");
        }
    }
    // numTypes < 0.
    {
        std::vector<std::uint8_t> bytes;
        Append(bytes, Le32(0xBEEFCACE));
        Append(bytes, Le32(1));
        Append(bytes, Le32(0));
        AppendString(bytes, "reader");
        AppendString(bytes, "set");
        Append(bytes, Le32(2));
        Append(bytes, Le32(0));
        Append(bytes, Le32(0x80000000));
        try {
            ResourcesFile rf(bytes.data(), bytes.size());
            FAIL() << "expected std::out_of_range";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Resources header corrupted.");
        }
    }
    // A negative name position.
    {
        std::vector<std::uint8_t> bytes = MakeResources(
            1, {0x80000000}, MakeOneEntryTail());
        try {
            ResourcesFile rf(bytes.data(), bytes.size());
            FAIL() << "expected std::out_of_range";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Resources header corrupted.");
        }
    }
    // A negative data-section offset.
    {
        std::vector<std::uint8_t> bytes = MakeResources(
            1, {0}, MakeOneEntryTail(), 0x80000000u);
        try {
            ResourcesFile rf(bytes.data(), bytes.size());
            FAIL() << "expected std::out_of_range";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Resources header corrupted.");
        }
    }
    // The data section before the name section.
    {
        std::vector<std::uint8_t> bytes = MakeResources(
            1, {0}, MakeOneEntryTail(), 0u);
        try {
            ResourcesFile rf(bytes.data(), bytes.size());
            FAIL() << "expected std::out_of_range";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Resources header corrupted.");
        }
    }
}

// A truncated header (the blob ends mid-header): every read past the end
// throws the EndOfStreamException arm.
TEST(ResourcesFileTest, TruncatedHeaderThrows)
{
    std::string full = TestResourcesBytes();
    for (std::size_t cut = 1; cut < 40; cut++) {
        // The cut lands before the resource-set version completes, so the
        // parse throws somewhere in the header.
        std::string bytes = full.substr(0, cut);
        EXPECT_THROW(
            ResourcesFile rf(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                bytes.size()),
            std::out_of_range)
            << "cut at " << cut;
    }
}

// A valid header whose name-hash skip would need the checked-multiply
// guard: numResources large enough that 4 * numResources overflows int32.
// The numTypes field and the (empty) type table must be present -- the C#
// reads them before the checked skip.
TEST(ResourcesFileTest, NameHashSkipOverflowThrows)
{
    std::vector<std::uint8_t> bytes;
    Append(bytes, Le32(0xBEEFCACE));
    Append(bytes, Le32(1));
    Append(bytes, Le32(0));
    AppendString(bytes, "reader");
    AppendString(bytes, "set");
    Append(bytes, Le32(2));
    Append(bytes, Le32(0x40000000));  // 2^30: 4 * n overflows int32
    Append(bytes, Le32(0));           // numTypes: an empty type table
    // The 8-byte alignment of the name-hash array must be satisfied -- the
    // C# pads before the checked skip.
    while (bytes.size() % 8 != 0) bytes.push_back(0x50);
    try {
        ResourcesFile rf(bytes.data(), bytes.size());
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Resources header corrupted.");
    }
}

// ---- The GetResourceName failure arms

// A name-position that seeks past the end: the read of the name length
// throws.
TEST(ResourcesFileTest, NamePositionPastEndThrows)
{
    std::vector<std::uint8_t> bytes = MakeResources(
        1, {1000000}, MakeOneEntryTail());
    ResourcesFile rf(bytes.data(), bytes.size());
    EXPECT_THROW(rf.GetResourceName(0), std::out_of_range);
}

// The 7-bit-encoded negative name length (a 0xFFFFFFFF... prefix decodes
// to -1).
TEST(ResourcesFileTest, NegativeNameLengthThrows)
{
    std::vector<std::uint8_t> tail = {0xFF, 0xFF, 0xFF, 0xFF, 0x0F,
        0x00, 0x00, 0x00, 0x00};
    std::vector<std::uint8_t> bytes = MakeResources(1, {0}, tail);
    ResourcesFile rf(bytes.data(), bytes.size());
    try {
        rf.GetResourceName(0);
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Resource name has negative length");
    }
}

// A name whose declared byte length runs past the end of the blob: the
// blocking read loop consumes the available bytes and the zero-return
// read throws (a partial read is NOT an error -- the C# loops until a
// read returns 0).
TEST(ResourcesFileTest, TruncatedNameThrows)
{
    std::vector<std::uint8_t> tail = {0x04, 0x41, 0x00};  // len 4, 2 bytes
    std::vector<std::uint8_t> bytes = MakeResources(1, {0}, tail);
    ResourcesFile rf(bytes.data(), bytes.size());
    try {
        rf.GetResourceName(0);
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "End of stream within a resource name");
    }
}

// A negative data offset after the name bytes.
TEST(ResourcesFileTest, NegativeDataOffsetThrows)
{
    std::vector<std::uint8_t> tail = {0x02, 0x41, 0x00, 0x00, 0x00, 0x00,
        0x80};  // "A", then data offset 0x80000000
    std::vector<std::uint8_t> bytes = MakeResources(1, {0}, tail);
    ResourcesFile rf(bytes.data(), bytes.size());
    try {
        rf.GetResourceName(0);
        FAIL() << "expected std::out_of_range";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Negative data offset");
    }
}

// An odd-length name: the trailing lone byte decodes as U+FFFD (the
// Encoding.Unicode end-of-input behavior, probed against real .NET:
// {0x41, 0x00, 0x42} decodes as 'A' + U+FFFD).
TEST(ResourcesFileTest, OddLengthNameDecodesReplacementChar)
{
    std::vector<std::uint8_t> tail = {0x03, 0x41, 0x00, 0x42, 0x00, 0x00,
        0x00, 0x00};  // len 3: "A" + the lone 0x42
    std::vector<std::uint8_t> bytes = MakeResources(1, {0}, tail);
    ResourcesFile rf(bytes.data(), bytes.size());
    EXPECT_EQ(rf.GetResourceName(0), "A\xEF\xBF\xBD");
}

// A non-empty type table parses: the strings are read before the
// alignment (the table is carried for the deferred value decode's
// FindType).
TEST(ResourcesFileTest, TypeTableParses)
{
    std::vector<std::uint8_t> out;
    Append(out, Le32(0xBEEFCACE));
    Append(out, Le32(1));
    Append(out, Le32(0));
    AppendString(out, "reader");
    AppendString(out, "set");
    Append(out, Le32(2));
    Append(out, Le32(1));  // one resource
    Append(out, Le32(2));  // two types
    AppendString(out, "System.String");
    AppendString(out, "System.Int32");
    while (out.size() % 8 != 0) out.push_back(0x50);
    out.insert(out.end(), 4, 0);
    Append(out, Le32(0));
    std::vector<std::uint8_t> tail = MakeOneEntryTail();
    Append(out, Le32(static_cast<std::uint32_t>(out.size() + 4 + tail.size())));
    Append(out, tail);
    ResourcesFile rf(out.data(), out.size());
    EXPECT_EQ(rf.ResourceCount(), 1);
    EXPECT_EQ(rf.GetResourceName(0), "A");
}
