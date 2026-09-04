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

// The ResourcesFile entry-value decode (GetResourceValue /
// LoadObjectV1/V2 / GetBytesForSerializedObject): the value matrix over
// the three containers the value-decode manifest fixture embeds (the real
// System.Resources.ResourceWriter V2 container with one entry per supported
// value type, the hand-crafted RuntimeResourceSet V1 container with the
// type-table path and the three serialized-object region arms, and the
// hand-crafted System.Resources.Extensions DeserializingResourceReader
// container with the [kind][len]-wrapped serialized types), the failure
// arms over the standalone bad-entry container, the truncated-container
// EndOfStream wrap, and the real mscorlib.resources no-throw sweep. Every
// expected value was dumped from the real ICSharpCode.Decompiler
// ResourcesFile driven over the identical bytes (the SDK-10 probe,
// C:\temp-probe\ResGen\Program.cs).

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Util/ResourcesFile.hpp"

#include "TestFixtures/ResourcesTestFixtures.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::Util::ResourceValue;
using ILSpy::Decompiler::Util::ResourcesFile;
using ILSpy::Tests::BadV2ResourcesBytes;
using ILSpy::Tests::ValTestResourceData;
using ILSpy::Tests::WriteValTestDll;

std::string MscorlibPath() {
#if defined(_WIN32)
    return "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    return "/usr/lib/mono/4.5/mscorlib.dll";
#endif
}

// The valtest manifest's named .resources container bytes (the embedded
// resource's blob after the length prefix), failing the test when the row
// is missing (no silent empty container).
std::vector<std::uint8_t> ContainerBytes(
    const MetadataFile& module, const std::string& name) {
    auto data = ValTestResourceData(module, name);
    EXPECT_TRUE(data.has_value()) << name;
    if (!data)
        return {};
    return *data;
}

// The entry index with the given name (the container's row order), failing
// the test when absent.
int EntryIndex(ResourcesFile& file, const std::string& name) {
    for (int i = 0; i < file.ResourceCount(); i++) {
        if (file.GetResourceName(i) == name)
            return i;
    }
    ADD_FAILURE() << "no entry named '" << name << "'";
    return -1;
}

}  // namespace

// ---- The V2 value matrix (the ResourceWriter container)

// The V2 container's 26 entries: one per supported value type, with the
// values the real ResourcesFile decoded over the same bytes.
TEST(ResourcesFileValueTest, V2ValueMatrix)
{
    MetadataFile module(WriteValTestDll());
    ASSERT_TRUE(module.IsValid());
    std::vector<std::uint8_t> bytes = ContainerBytes(module, "v2.resources");
    ResourcesFile file(bytes.data(), bytes.size());
    ASSERT_EQ(file.ResourceCount(), 26);
    EXPECT_FALSE(file.UsesSerializationFormat());
    EXPECT_EQ(file.Version(), 2);

    // The string entries (ReadString: the 7-bit length + UTF-8 bytes).
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Str"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::String);
        EXPECT_EQ(v.str, "one");
    }
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "Unicode.Name.\xE4\xB8\xAD\xE6\x96\x87"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::String);
        EXPECT_EQ(v.str, "unicode value");
    }
    // The booleans (ReadBoolean: a nonzero byte).
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Bool"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Boolean);
        EXPECT_TRUE(v.boolean);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "BoolF"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Boolean);
        EXPECT_FALSE(v.boolean);
    }
    // The char (ReadUInt16: the UTF-16 code unit).
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Char"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Char);
        EXPECT_EQ(v.character, 0x4E2Du);  // U+4E2D
    }
    // The integral types.
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Byte"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Byte);
        EXPECT_EQ(v.integer, 171u);  // 0xAB
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "SByte"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::SByte);
        EXPECT_EQ(static_cast<std::int64_t>(v.integer), -12);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Short"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Int16);
        EXPECT_EQ(static_cast<std::int64_t>(v.integer), -1234);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "UShort"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::UInt16);
        EXPECT_EQ(v.integer, 1234u);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Int"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Int32);
        EXPECT_EQ(static_cast<std::int64_t>(v.integer), 42);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "UInt"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::UInt32);
        EXPECT_EQ(v.integer, 0xFEDCBA98u);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Long"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Int64);
        EXPECT_EQ(static_cast<std::int64_t>(v.integer), -1234567890123456789LL);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "ULong"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::UInt64);
        EXPECT_EQ(v.integer, 0xFEDCBA9876543210ull);
    }
    // The floats (ReadSingle: the LE bits).
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Float"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Single);
        EXPECT_EQ(v.single, 1.5f);
    }
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "FloatNegNaN"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Single);
        EXPECT_TRUE(std::isnan(v.single));
    }
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "FloatPosInf"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Single);
        EXPECT_EQ(v.single, std::numeric_limits<float>::infinity());
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "FloatSci"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Single);
        EXPECT_EQ(v.single, 1e16f);
    }
    // The doubles (ReadDouble).
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Double"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Double);
        EXPECT_EQ(v.doubleValue, 2.25);
    }
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "DoubleSci"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Double);
        EXPECT_EQ(v.doubleValue, 1e16);
    }
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "DoubleSmall"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Double);
        EXPECT_EQ(v.doubleValue, 0.0001);
    }
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "DoubleExp"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Double);
        EXPECT_EQ(v.doubleValue, 1e17);
    }
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "DoubleNeg"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Double);
        EXPECT_EQ(v.doubleValue, -0.5);
    }
    // The decimal (ReadDecimal: the four int32s in the [lo, mid, hi,
    // flags] order -- 12345.6789m's bits probed from the real decimal).
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "Decimal"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Decimal);
        EXPECT_EQ(v.decimalBits[0], 0x075BCD15u);  // lo: 123456789
        EXPECT_EQ(v.decimalBits[1], 0u);           // mid
        EXPECT_EQ(v.decimalBits[2], 0u);           // hi
        EXPECT_EQ(v.decimalBits[3], 0x00040000u);  // flags: scale 4
    }
    // The byte arrays (the int32 length + the bytes).
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Bytes"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::ByteArray);
        EXPECT_EQ(v.bytes, (std::vector<std::uint8_t>{1, 2, 3}));
    }
    {
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "EmptyBytes"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::ByteArray);
        EXPECT_TRUE(v.bytes.empty());
    }
    // The stream (the same shape as the byte array, the Stream kind).
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Stream"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Stream);
        EXPECT_EQ(v.bytes, (std::vector<std::uint8_t>{7, 8, 9}));
    }
}

// ---- The V1 value matrix (the hand-crafted type-table path)

// The V1 container's eleven entries: the type-table index path with the
// assembly-name strip, the DateTime/TimeSpan/Decimal shapes, the type-index
// -1 null marker, and the three serialized-object region arms.
TEST(ResourcesFileValueTest, V1ValueMatrix)
{
    MetadataFile module(WriteValTestDll());
    ASSERT_TRUE(module.IsValid());
    std::vector<std::uint8_t> bytes = ContainerBytes(module, "v1.resources");
    ResourcesFile file(bytes.data(), bytes.size());
    ASSERT_EQ(file.ResourceCount(), 11);
    EXPECT_FALSE(file.UsesSerializationFormat());
    EXPECT_EQ(file.Version(), 1);

    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "V1Str"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::String);
        EXPECT_EQ(v.str, "hello v1");
    }
    {
        // "System.Int32, mscorlib" strips to the Int32 read.
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "V1Int"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Int32);
        EXPECT_EQ(static_cast<std::int64_t>(v.integer), -77);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "V1Long"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Int64);
        EXPECT_EQ(static_cast<std::int64_t>(v.integer), 0x1122334455667788LL);
    }
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "V1Double"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Double);
        EXPECT_EQ(v.doubleValue, 2.25);
    }
    {
        // The decimal bits in the same [lo, mid, hi, flags] order.
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "V1Decimal"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Decimal);
        EXPECT_EQ(v.decimalBits[0], 0x075BCD15u);
        EXPECT_EQ(v.decimalBits[3], 0x00040000u);
    }
    {
        // DateTime(2024-02-15 10:30:45).Ticks.
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "V1Date"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::DateTime);
        EXPECT_EQ(v.ticks, 638435898450000000LL);
    }
    {
        // TimeSpan(1, 2, 3, 4).Ticks.
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "V1TS"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::TimeSpan);
        EXPECT_EQ(v.ticks, 937840000000LL);
    }
    {
        // The type index -1 null marker.
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "V1Null"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Null);
    }
    {
        // A serialized user type in the middle of the data section: the
        // region from after the type index to the NEXT entry's data start
        // (the entry's own value bytes plus the next entry's type-index
        // byte), with the null TypeName (!usesSerializationFormat).
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "V1UserMid"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::SerializedObject);
        EXPECT_EQ(v.bytes, (std::vector<std::uint8_t>{0xAA, 0xBB, 0x07}));
        EXPECT_TRUE(v.typeName.empty());
    }
    {
        // A zero-length region: the next entry's data start equals the
        // position after the type index (the Array.BinarySearch exact
        // hit), so no bytes.
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "V1UserEmpty"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::SerializedObject);
        EXPECT_TRUE(v.bytes.empty());
        EXPECT_TRUE(v.typeName.empty());
    }
    {
        // The last entry: the region runs to the file length.
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "V1User"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::SerializedObject);
        EXPECT_EQ(v.bytes, (std::vector<std::uint8_t>{0xCC, 0xDD, 0xEE}));
        EXPECT_TRUE(v.typeName.empty());
    }
}

// ---- The serialization-format container (the DeserializingResourceReader)

// The System.Resources.Extensions container: the primitive entries decode
// directly (the wrapper applies only to the serialized user types), and
// the user types' GetBytes walk skips the [kind][len] wrapper.
TEST(ResourcesFileValueTest, SerFmtValueMatrix)
{
    MetadataFile module(WriteValTestDll());
    ASSERT_TRUE(module.IsValid());
    std::vector<std::uint8_t> bytes =
        ContainerBytes(module, "serfmt.resources");
    ResourcesFile file(bytes.data(), bytes.size());
    ASSERT_EQ(file.ResourceCount(), 4);
    EXPECT_TRUE(file.UsesSerializationFormat());
    EXPECT_EQ(file.Version(), 2);

    {
        // A primitive string entry: unwrapped (the real reader decoded
        // "ser string" through the String case directly).
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "SerStr"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::String);
        EXPECT_EQ(v.str, "ser string");
    }
    {
        // The [kind][len]-wrapped user type mid-section: the walk skips
        // the wrapper bytes and yields the payload, and the TypeName is
        // set (usesSerializationFormat).
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "SerUserMid"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::SerializedObject);
        EXPECT_EQ(v.bytes, (std::vector<std::uint8_t>{0x99, 0x88, 0x77}));
        EXPECT_EQ(v.typeName, "MyApp.MyType, MyAssembly");
    }
    {
        // A wrapped zero-length payload.
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "SerUserEmpty"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::SerializedObject);
        EXPECT_TRUE(v.bytes.empty());
        EXPECT_EQ(v.typeName, "MyApp.MyType, MyAssembly");
    }
    {
        // The last entry: the region runs to the file length.
        ResourceValue v = file.GetResourceValue(
            EntryIndex(file, "SerUserLast"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::SerializedObject);
        EXPECT_EQ(v.bytes, (std::vector<std::uint8_t>{0x55, 0x66}));
        EXPECT_EQ(v.typeName, "MyApp.MyLastType, MyAssembly");
    }
}

// ---- The failure arms

// The bad-entry container: the invalid type code, the out-of-bounds type
// index, and the negative ByteArray length, each with the exact C#
// BadImageFormatException message the real reader threw over the same
// bytes (unwrapped -- a different C# exception type than LoadObject's
// EndOfStream catch).
TEST(ResourcesFileValueTest, BadEntryArms)
{
    std::vector<std::uint8_t> bytes = BadV2ResourcesBytes();
    ASSERT_EQ(bytes.size(), 351u);
    ResourcesFile file(bytes.data(), bytes.size());
    ASSERT_EQ(file.ResourceCount(), 3);

    {
        int i = EntryIndex(file, "BadTypeCode");
        try {
            file.GetResourceValue(i);
            FAIL() << "expected the invalid-typecode throw";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Invalid typeCode");
        }
    }
    {
        int i = EntryIndex(file, "BadUser");
        try {
            file.GetResourceValue(i);
            FAIL() << "expected the type-index throw";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Type index out of bounds");
        }
    }
    {
        int i = EntryIndex(file, "NegBytes");
        try {
            file.GetResourceValue(i);
            FAIL() << "expected the negative-length throw";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Resource with negative length");
        }
    }
}

// The truncated-container arm: the entries whose values run past the cut
// throw the EndOfStream wrap (the C# BadImageFormatException("Invalid
// resource file")), while the entries before the cut still decode -- the
// per-entry behavior the real reader showed over the same truncated bytes.
TEST(ResourcesFileValueTest, TruncatedContainerWrap)
{
    MetadataFile module(WriteValTestDll());
    ASSERT_TRUE(module.IsValid());
    std::vector<std::uint8_t> full = ContainerBytes(module, "v2.resources");
    ASSERT_GE(full.size(), 30u);
    std::vector<std::uint8_t> cut(full.begin(), full.end() - 30);
    ResourcesFile file(cut.data(), cut.size());
    ASSERT_EQ(file.ResourceCount(), 26);

    // An entry whose value sits past the cut: the wrap's message.
    {
        int i = EntryIndex(file, "UShort");
        try {
            file.GetResourceValue(i);
            FAIL() << "expected the truncated-value throw";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(), "Invalid resource file");
        }
    }
    // The entries whose data precedes the cut still decode.
    {
        ResourceValue v = file.GetResourceValue(EntryIndex(file, "Int"));
        EXPECT_EQ(v.kind, ResourceValue::Kind::Int32);
        EXPECT_EQ(static_cast<std::int64_t>(v.integer), 42);
    }
}

// ---- The real mscorlib.resources container

// The 3172-entry real container (all strings, the real-world shape): a
// spot value, and the full no-throw sweep with every entry decoding to a
// non-empty string.
TEST(ResourcesFileValueTest, MscorlibResourcesValues)
{
    MetadataFile mscorlib(MscorlibPath());
    ASSERT_TRUE(mscorlib.IsValid());
    std::optional<std::vector<std::uint8_t>> data;
    for (const auto& r : mscorlib.GetManifestResources()) {
        if (r.Name == "mscorlib.resources")
            data = mscorlib.TryGetManifestResourceData(r.Token);
    }
    ASSERT_TRUE(data.has_value());
    ResourcesFile file(data->data(), data->size());
    ASSERT_EQ(file.ResourceCount(), 3172);
    {
        // The probe-pinned first entry.
        std::string name = file.GetResourceName(0);
        EXPECT_EQ(name, "Format_MissingIncompleteDate");
        ResourceValue v = file.GetResourceValue(0);
        EXPECT_EQ(v.kind, ResourceValue::Kind::String);
        EXPECT_EQ(v.str,
            "There must be at least a partial date with a year present in "
            "the input.");
    }
    // The full sweep: every entry decodes to a String.
    int stringCount = 0;
    for (int i = 0; i < file.ResourceCount(); i++) {
        ResourceValue v = file.GetResourceValue(i);
        if (v.kind == ResourceValue::Kind::String)
            stringCount++;
    }
    EXPECT_EQ(stringCount, 3172);
}
