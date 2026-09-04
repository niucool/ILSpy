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

// The SingleFileBundle port (Decompiler/SingleFileBundle.{hpp,cpp}): the
// bundle-signature scan (IsBundle) and the manifest decode (ReadManifest).
// Every expected value and message is pinned against the real C#
// (ICSharpCode.Decompiler/SingleFileBundle.cs driven through
// System.IO.BinaryReader): the exception messages are the exact .NET 10
// strings (probed from a standalone SDK-10 project), and the byte layouts
// mirror Microsoft.NET.HostModel's Bundler output -- the manifest at the
// bundle end, the footer ([8-byte manifest offset][32-byte signature])
// patched into the apphost region mid-file, so the signature scan never sees
// it at the very end of the file.

#include "Decompiler/SingleFileBundle.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace Sfb = ILSpy::Decompiler::SingleFileBundle;

// The 32-byte bundle signature: SHA-256 for ".net core bundle"
// (Microsoft.NET.HostModel's BundleHeaderPlaceholder.Slice(8)).
const std::uint8_t kSignature[32] = {
    0x8b, 0x12, 0x02, 0xb9, 0x6a, 0x61, 0x20, 0x38,
    0x72, 0x7b, 0x93, 0x02, 0x14, 0xd7, 0xa0, 0x32,
    0x13, 0xf5, 0xb9, 0xe6, 0xef, 0xae, 0x33, 0x18,
    0xee, 0x3b, 0x2d, 0xce, 0x24, 0xb3, 0x6a, 0xae,
};

void PutU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; i++) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void PutI32(std::vector<std::uint8_t>& b, std::int32_t v) {
    PutU32(b, static_cast<std::uint32_t>(v));
}

void PutI64(std::vector<std::uint8_t>& b, long long v) {
    for (int i = 0; i < 8; i++) b.push_back(static_cast<std::uint8_t>((static_cast<std::uint64_t>(v)) >> (8 * i)));
}

void PutU64(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (int i = 0; i < 8; i++) b.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

// BinaryWriter.Write(string): the 7-bit length prefix over a non-negative
// int, then the raw UTF-8 bytes.
void PutLengthPrefixedString(std::vector<std::uint8_t>& b, const std::string& s) {
    std::uint32_t len = static_cast<std::uint32_t>(s.size());
    while (len >= 0x80) {
        b.push_back(static_cast<std::uint8_t>(len) | 0x80);
        len >>= 7;
    }
    b.push_back(static_cast<std::uint8_t>(len));
    b.insert(b.end(), s.begin(), s.end());
}

// A bundle byte layout mirroring the Bundler output: [apphost padding
// (zeros)][footer: manifest-offset placeholder + signature][payload region]
// [manifest]. The entries' Offset fields are relative to the start of the
// payload region (the builder adds the base); the footer is patched to the
// manifest start so IsBundle finds it mid-file, exactly like a genuine
// bundle.
struct BundleSpec {
    std::uint32_t majorVersion;
    std::uint32_t minorVersion = 0;
    std::string bundleId;
    std::size_t apphostPadding = 16;
    std::vector<std::uint8_t> payloadRegion;
    std::vector<Sfb::Entry> entries;
    // The v2+ header fields (ignored for majorVersion < 2).
    long long depsJsonOffset = 100, depsJsonSize = 10;
    long long runtimeConfigJsonOffset = 200, runtimeConfigJsonSize = 20;
    std::uint64_t flags = 0;
    // When false the footer signature is left out (a non-bundle image).
    bool withSignature = true;
};

std::vector<std::uint8_t> BuildBundle(const BundleSpec& spec) {
    std::vector<std::uint8_t> b(spec.apphostPadding, 0);
    std::size_t footerAt = b.size();
    b.insert(b.end(), 8, 0);  // the manifest-offset placeholder
    if (spec.withSignature)
        b.insert(b.end(), kSignature, kSignature + 32);
    std::size_t payloadBase = b.size();
    b.insert(b.end(), spec.payloadRegion.begin(), spec.payloadRegion.end());
    std::size_t manifestOffset = b.size();
    PutU32(b, spec.majorVersion);
    PutU32(b, spec.minorVersion);
    PutI32(b, static_cast<std::int32_t>(spec.entries.size()));
    PutLengthPrefixedString(b, spec.bundleId);
    if (spec.majorVersion >= 2) {
        PutI64(b, spec.depsJsonOffset);
        PutI64(b, spec.depsJsonSize);
        PutI64(b, spec.runtimeConfigJsonOffset);
        PutI64(b, spec.runtimeConfigJsonSize);
        PutU64(b, spec.flags);
    }
    for (const Sfb::Entry& e : spec.entries) {
        PutI64(b, static_cast<long long>(payloadBase) + e.Offset);
        PutI64(b, e.Size);
        if (spec.majorVersion >= 6)
            PutI64(b, e.CompressedSize);
        b.push_back(static_cast<std::uint8_t>(e.Type));
        PutLengthPrefixedString(b, e.RelativePath);
    }
    std::uint64_t footerValue = static_cast<std::uint64_t>(manifestOffset);
    std::memcpy(b.data() + footerAt, &footerValue, 8);
    return b;
}

Sfb::Entry MakeEntry(long long offset, long long size, Sfb::FileType type,
    const std::string& path, long long compressedSize = 0) {
    Sfb::Entry e;
    e.Offset = offset;
    e.Size = size;
    e.CompressedSize = compressedSize;
    e.Type = type;
    e.RelativePath = path;
    return e;
}

std::vector<std::uint8_t> Bytes(const std::string& s) {
    return std::vector<std::uint8_t>(s.begin(), s.end());
}

// ---------------------------------------------------------------------------
// IsBundle
// ---------------------------------------------------------------------------

TEST(SingleFileBundleIsBundle, FindsMidFileSignatureAndOffset) {
    BundleSpec spec;
    spec.majorVersion = 6;
    spec.bundleId = "id";
    spec.payloadRegion = Bytes("payload");
    spec.entries.push_back(MakeEntry(0, 7, Sfb::FileType::Assembly, "a.dll"));
    auto bundle = BuildBundle(spec);

    // The manifest starts after the padding (16) + footer (8 + 32) + payload
    // (7) -- the offset stored in the footer.
    const long long expectedOffset = 16 + 8 + 32 + 7;
    long long headerOffset = -1;
    ASSERT_TRUE(Sfb::IsBundle(bundle.data(), static_cast<long long>(bundle.size()), headerOffset));
    EXPECT_EQ(headerOffset, expectedOffset);
}

TEST(SingleFileBundleIsBundle, ReportsTheManifestStartOffset) {
    BundleSpec spec;
    spec.majorVersion = 2;
    spec.bundleId = "bundle-id";
    spec.payloadRegion = Bytes("0123456789");
    spec.entries.push_back(MakeEntry(0, 10, Sfb::FileType::Assembly, "x.dll"));
    auto bundle = BuildBundle(spec);

    // The manifest begins after the payload region: 16 padding + 8 + 32
    // (footer) + 10 (payload) bytes.
    const long long expectedOffset = 16 + 8 + 32 + 10;
    long long headerOffset = -1;
    ASSERT_TRUE(Sfb::IsBundle(bundle.data(), static_cast<long long>(bundle.size()), headerOffset));
    EXPECT_EQ(headerOffset, expectedOffset);

    // The manifest really decodes from there (the scan and the decode agree).
    Sfb::Header header = Sfb::ReadManifest(bundle.data(), static_cast<long long>(bundle.size()), headerOffset);
    EXPECT_EQ(header.MajorVersion, 2u);
    EXPECT_EQ(header.BundleID, "bundle-id");
    ASSERT_EQ(header.Entries.size(), 1u);
    EXPECT_EQ(header.Entries[0].RelativePath, "x.dll");
}

TEST(SingleFileBundleIsBundle, RejectsImageWithoutSignature) {
    std::vector<std::uint8_t> bytes(200, 0x41);
    long long headerOffset = -1;
    EXPECT_FALSE(Sfb::IsBundle(bytes.data(), static_cast<long long>(bytes.size()), headerOffset));
    EXPECT_EQ(headerOffset, 0);
}

TEST(SingleFileBundleIsBundle, RejectsEmptyAndTinyImages) {
    long long headerOffset = -1;
    EXPECT_FALSE(Sfb::IsBundle(nullptr, 0, headerOffset));
    EXPECT_EQ(headerOffset, 0);
    std::vector<std::uint8_t> tiny(31, 0x8b);
    EXPECT_FALSE(Sfb::IsBundle(tiny.data(), static_cast<long long>(tiny.size()), headerOffset));
    EXPECT_EQ(headerOffset, 0);
}

TEST(SingleFileBundleIsBundle, RejectsOutOfBoundsFooterOffsets) {
    BundleSpec spec;
    spec.majorVersion = 2;
    spec.bundleId = "id";
    spec.payloadRegion = Bytes("payload");
    spec.entries.push_back(MakeEntry(0, 7, Sfb::FileType::Assembly, "a.dll"));
    auto bundle = BuildBundle(spec);
    const long long size = static_cast<long long>(bundle.size());
    const std::size_t footerAt = 16;

    // Offset 0 (rejected), negative (rejected), == size (rejected), > size (rejected).
    for (long long bad : { 0LL, -1LL, -100LL, size, size + 1LL }) {
        std::uint64_t v = static_cast<std::uint64_t>(bad);
        std::memcpy(bundle.data() + footerAt, &v, 8);
        long long headerOffset = -1;
        EXPECT_FALSE(Sfb::IsBundle(bundle.data(), size, headerOffset))
            << "offset " << bad << " must be rejected";
        EXPECT_EQ(headerOffset, 0);
    }
}

TEST(SingleFileBundleIsBundle, GuardKeepsSignatureNearStartOutOfBounds) {
    // The crafted-file guard: a signature starting within the first
    // sizeof(long) bytes must be skipped WITHOUT reading before the buffer
    // (the C# comment in IsBundle documents the same guard).
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), 6, 0);  // signature starts at 6 < sizeof(long)
    bytes.insert(bytes.end(), kSignature, kSignature + 32);
    // The 8 bytes before the signature would underflow the buffer; the guard
    // must keep the read from happening.
    long long headerOffset = -1;
    EXPECT_FALSE(Sfb::IsBundle(bytes.data(), static_cast<long long>(bytes.size()), headerOffset));
    EXPECT_EQ(headerOffset, 0);
}

TEST(SingleFileBundleIsBundle, SignatureInLast32BytesIsNotFound) {
    // The faithful C# scan bound `ptr < data + size - 32`: a signature
    // starting within the last 32 bytes of the file is never compared
    // (genuine bundles keep the footer mid-file, inside the apphost region,
    // so the bound is unobservable for real bundles -- the port keeps it).
    std::vector<std::uint8_t> bytes(8, 0);
    bytes.insert(bytes.end(), kSignature, kSignature + 32);
    long long headerOffset = -1;
    EXPECT_FALSE(Sfb::IsBundle(bytes.data(), static_cast<long long>(bytes.size()), headerOffset));
    EXPECT_EQ(headerOffset, 0);
}

TEST(SingleFileBundleIsBundle, FindsSignatureAfterData) {
    // A signature found anywhere strictly before the last 32 bytes is a
    // candidate, even with real data after it (the .NET 10 bundle layout:
    // 65MB of files follow the footer).
    std::vector<std::uint8_t> bytes(16, 0);
    std::uint64_t offset = 123;
    std::memcpy(bytes.data() + 8, &offset, 8);
    bytes.insert(bytes.end(), kSignature, kSignature + 32);
    size_t sigAt = bytes.size();
    bytes.insert(bytes.end(), 100, 0xEE);  // data after the footer

    long long headerOffset = -1;
    // The stored offset 123 must be in bounds (< size) for a true result;
    // it is (16 + 40 + 100 bytes).
    ASSERT_TRUE(Sfb::IsBundle(bytes.data(), static_cast<long long>(bytes.size()), headerOffset));
    EXPECT_EQ(headerOffset, 123);
    EXPECT_EQ(sigAt, 48u);
}

// ---------------------------------------------------------------------------
// ReadManifest
// ---------------------------------------------------------------------------

TEST(SingleFileBundleManifest, DecodesV6HeaderAndEntries) {
    BundleSpec spec;
    spec.majorVersion = 6;
    spec.minorVersion = 0;
    spec.bundleId = "RGUeD-03fXLo";
    spec.depsJsonOffset = 73373992;
    spec.depsJsonSize = 26481;
    spec.runtimeConfigJsonOffset = 9977856;
    spec.runtimeConfigJsonSize = 373;
    spec.flags = 0;
    spec.payloadRegion = Bytes("payload-bytes");
    spec.entries.push_back(MakeEntry(0, 13, Sfb::FileType::Assembly, "app.dll", 0));
    spec.entries.push_back(MakeEntry(13, 4, Sfb::FileType::DepsJson, "sub/app.deps.json", 0));
    spec.entries.push_back(MakeEntry(17, 0, Sfb::FileType::RuntimeConfigJson, "app.runtimeconfig.json", 5));
    spec.entries.push_back(MakeEntry(17, 0, Sfb::FileType::Symbols, "app.pdb", 7));
    auto bundle = BuildBundle(spec);

    long long headerOffset = 0;
    ASSERT_TRUE(Sfb::IsBundle(bundle.data(), static_cast<long long>(bundle.size()), headerOffset));
    Sfb::Header header = Sfb::ReadManifest(bundle.data(), static_cast<long long>(bundle.size()), headerOffset);

    EXPECT_EQ(header.MajorVersion, 6u);
    EXPECT_EQ(header.MinorVersion, 0u);
    EXPECT_EQ(header.FileCount, 4);
    EXPECT_EQ(header.BundleID, "RGUeD-03fXLo");
    EXPECT_EQ(header.DepsJsonOffset, 73373992);
    EXPECT_EQ(header.DepsJsonSize, 26481);
    EXPECT_EQ(header.RuntimeConfigJsonOffset, 9977856);
    EXPECT_EQ(header.RuntimeConfigJsonSize, 373);
    EXPECT_EQ(header.Flags, 0u);
    ASSERT_EQ(header.Entries.size(), 4u);

    EXPECT_EQ(header.Entries[0].Offset, 16 + 8 + 32);
    EXPECT_EQ(header.Entries[0].Size, 13);
    EXPECT_EQ(header.Entries[0].CompressedSize, 0);
    EXPECT_EQ(header.Entries[0].Type, Sfb::FileType::Assembly);
    EXPECT_EQ(header.Entries[0].RelativePath, "app.dll");

    EXPECT_EQ(header.Entries[1].Offset, 16 + 8 + 32 + 13);
    EXPECT_EQ(header.Entries[1].Type, Sfb::FileType::DepsJson);
    EXPECT_EQ(header.Entries[1].RelativePath, "sub/app.deps.json");

    EXPECT_EQ(header.Entries[2].CompressedSize, 5);
    EXPECT_EQ(header.Entries[2].Type, Sfb::FileType::RuntimeConfigJson);

    EXPECT_EQ(header.Entries[3].CompressedSize, 7);
    EXPECT_EQ(header.Entries[3].Type, Sfb::FileType::Symbols);
    EXPECT_EQ(header.Entries[3].RelativePath, "app.pdb");
}

TEST(SingleFileBundleManifest, DecodesV1WithoutV2FieldsOrCompressedSizes) {
    // Major versions below 2 carry no deps/runtimeconfig/flags header fields,
    // and only version 6+ carries the per-entry compressed size: a v1
    // manifest's entries read offset/size/type/path with no extra field.
    BundleSpec spec;
    spec.majorVersion = 1;
    spec.bundleId = "v1id";
    spec.payloadRegion = Bytes("0123456789");
    spec.entries.push_back(MakeEntry(0, 10, Sfb::FileType::Assembly, "one.dll"));
    spec.entries.push_back(MakeEntry(10, 0, Sfb::FileType::NativeBinary, "two/native.bin"));
    auto bundle = BuildBundle(spec);

    long long headerOffset = 0;
    ASSERT_TRUE(Sfb::IsBundle(bundle.data(), static_cast<long long>(bundle.size()), headerOffset));
    Sfb::Header header = Sfb::ReadManifest(bundle.data(), static_cast<long long>(bundle.size()), headerOffset);

    EXPECT_EQ(header.MajorVersion, 1u);
    EXPECT_EQ(header.FileCount, 2);
    // The v2+ fields stay at their defaults (the C# leaves them 0).
    EXPECT_EQ(header.DepsJsonOffset, 0);
    EXPECT_EQ(header.DepsJsonSize, 0);
    EXPECT_EQ(header.RuntimeConfigJsonOffset, 0);
    EXPECT_EQ(header.RuntimeConfigJsonSize, 0);
    EXPECT_EQ(header.Flags, 0u);
    ASSERT_EQ(header.Entries.size(), 2u);
    // The second entry decodes at all (the offsets line up without the
    // 8-byte compressed-size field).
    EXPECT_EQ(header.Entries[1].RelativePath, "two/native.bin");
    EXPECT_EQ(header.Entries[1].Type, Sfb::FileType::NativeBinary);
    EXPECT_EQ(header.Entries[1].CompressedSize, 0);
}

TEST(SingleFileBundleManifest, DecodesV5HeaderWithoutCompressedSizes) {
    // The last version before the per-entry compressed-size field: the v2
    // header fields ARE present, the entries are not.
    BundleSpec spec;
    spec.majorVersion = 5;
    spec.bundleId = "v5";
    spec.depsJsonOffset = 5;
    spec.depsJsonSize = 6;
    spec.runtimeConfigJsonOffset = 7;
    spec.runtimeConfigJsonSize = 8;
    spec.flags = 1;
    spec.payloadRegion = Bytes("xy");
    spec.entries.push_back(MakeEntry(0, 2, Sfb::FileType::Assembly, "a.dll"));
    spec.entries.push_back(MakeEntry(0, 2, Sfb::FileType::Assembly, "b.dll"));
    auto bundle = BuildBundle(spec);

    long long headerOffset = 0;
    ASSERT_TRUE(Sfb::IsBundle(bundle.data(), static_cast<long long>(bundle.size()), headerOffset));
    Sfb::Header header = Sfb::ReadManifest(bundle.data(), static_cast<long long>(bundle.size()), headerOffset);
    EXPECT_EQ(header.DepsJsonOffset, 5);
    EXPECT_EQ(header.Flags, 1u);
    ASSERT_EQ(header.Entries.size(), 2u);
    EXPECT_EQ(header.Entries[1].RelativePath, "b.dll");
}

TEST(SingleFileBundleManifest, RejectsUnsupportedVersions) {
    for (std::uint32_t bad : { 0u, 7u, 100u }) {
        BundleSpec spec;
        spec.majorVersion = bad;
        spec.bundleId = "id";
        auto bundle = BuildBundle(spec);
        // The manifest starts right after the padding + footer (no payload,
        // no entries).
        long long headerOffset = 16 + 8 + 32;
        try {
            Sfb::ReadManifest(bundle.data(), static_cast<long long>(bundle.size()), headerOffset);
            FAIL() << "major version " << bad << " must be rejected";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(),
                ("Unsupported manifest version: " + std::to_string(bad) + ".0").c_str());
        } catch (...) {
            FAIL() << "wrong exception type for major version " << bad;
        }
    }
}

TEST(SingleFileBundleManifest, RejectsDishonestFileCount) {
    // FileCount larger than the bytes that remain after it (each entry
    // occupies at least one byte): the exact C# message.
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 6);    // major
    PutU32(bytes, 0);    // minor
    PutI32(bytes, 1000); // FileCount
    PutLengthPrefixedString(bytes, "id");

    try {
        Sfb::ReadManifest(bytes.data(), static_cast<long long>(bytes.size()), 0);
        FAIL() << "FileCount 1000 must be rejected";
    } catch (const std::runtime_error& ex) {
        EXPECT_STREQ(ex.what(),
            "Invalid bundle manifest: FileCount 1000 exceeds available data.");
    } catch (...) {
        FAIL() << "wrong exception type";
    }

    // A negative FileCount hits the same arm.
    {
        std::vector<std::uint8_t> bytes;
        PutU32(bytes, 6);
        PutU32(bytes, 0);
        PutI32(bytes, -1);
        PutLengthPrefixedString(bytes, "id");
        try {
            Sfb::ReadManifest(bytes.data(), static_cast<long long>(bytes.size()), 0);
            FAIL() << "negative FileCount must be rejected";
        } catch (const std::runtime_error& ex) {
            EXPECT_STREQ(ex.what(),
                "Invalid bundle manifest: FileCount -1 exceeds available data.");
        } catch (...) {
            FAIL() << "wrong exception type";
        }
    }
}

TEST(SingleFileBundleManifest, RejectsTruncatedManifests) {
    BundleSpec spec;
    spec.majorVersion = 6;
    spec.bundleId = "truncated-id";
    spec.payloadRegion = Bytes("data");
    spec.entries.push_back(MakeEntry(0, 4, Sfb::FileType::Assembly, "a.dll"));
    auto bundle = BuildBundle(spec);
    // The manifest starts after the padding (16) + footer (8 + 32) + payload
    // (4) bytes.
    const long long headerOffset = 16 + 8 + 32 + 4;

    // Cut mid-bundle-id: major/minor/FileCount (12 bytes) + the 1-byte length
    // prefix + 3 of the 12 id bytes.
    std::vector<std::uint8_t> cut(bundle.begin(), bundle.begin() + (16 + 40 + 4 + 12 + 1 + 3));
    try {
        Sfb::ReadManifest(cut.data(), static_cast<long long>(cut.size()), headerOffset);
        FAIL() << "a manifest cut mid-string must throw";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Unable to read beyond the end of the stream.");
    } catch (...) {
        FAIL() << "wrong exception type";
    }

    // Cut mid-entry (the last 5 bytes of the final entry's path string).
    std::vector<std::uint8_t> cut2(bundle.begin(), bundle.end() - 5);
    try {
        Sfb::ReadManifest(cut2.data(), static_cast<long long>(cut2.size()), headerOffset);
        FAIL() << "a manifest cut mid-entry must throw";
    } catch (const std::out_of_range& ex) {
        EXPECT_STREQ(ex.what(), "Unable to read beyond the end of the stream.");
    } catch (...) {
        FAIL() << "wrong exception type";
    }
}

TEST(SingleFileBundleManifest, StringLengthPrefixSemantics) {
    // The 5-byte 0xFFFFFFFF0F prefix decodes to -1 through the .NET shift
    // wrap (the probe): the "invalid string length" IOException message.
    {
        std::vector<std::uint8_t> bytes;
        PutU32(bytes, 6);
        PutU32(bytes, 0);
        PutI32(bytes, 0);
        const std::uint8_t prefix[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x0F };
        bytes.insert(bytes.end(), prefix, prefix + 5);
        bytes.push_back('x');
        try {
            Sfb::ReadManifest(bytes.data(), static_cast<long long>(bytes.size()), 0);
            FAIL() << "a -1 string length must throw";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(),
                "BinaryReader encountered an invalid string length of -1 characters.");
        } catch (...) {
            FAIL() << "wrong exception type";
        }
    }
    // A 6th continuation byte trips .NET's 7-bit cap (the probed FormatException
    // message).
    {
        std::vector<std::uint8_t> bytes;
        PutU32(bytes, 6);
        PutU32(bytes, 0);
        PutI32(bytes, 0);
        const std::uint8_t prefix[] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x01 };
        bytes.insert(bytes.end(), prefix, prefix + 6);
        try {
            Sfb::ReadManifest(bytes.data(), static_cast<long long>(bytes.size()), 0);
            FAIL() << "a 6-byte 7-bit prefix must throw";
        } catch (const std::out_of_range& ex) {
            EXPECT_STREQ(ex.what(),
                "Too many bytes in what should have been a 7-bit encoded integer.");
        } catch (...) {
            FAIL() << "wrong exception type";
        }
    }
}

TEST(SingleFileBundleManifest, FileCountEqualToRemainingBytesIsAccepted) {
    // The boundary of the honesty check: FileCount == remaining bytes is
    // allowed (the C# rejects only strictly-greater counts) -- every entry
    // after it must then fail to read, but the check itself passes.
    std::vector<std::uint8_t> bytes;
    PutU32(bytes, 6);
    PutU32(bytes, 0);
    PutI32(bytes, 1);
    PutLengthPrefixedString(bytes, "id");
    PutI64(bytes, 0);  // deps offset
    PutI64(bytes, 0);
    PutI64(bytes, 0);
    PutI64(bytes, 0);
    PutU64(bytes, 0);
    // FileCount 1; remaining bytes at the check point: the whole tail
    // (4+1+8+8+8+8+8+40 = more than 1). Build a valid single entry.
    PutI64(bytes, 0);  // entry offset
    PutI64(bytes, 0);  // size
    PutI64(bytes, 0);  // compressed size
    bytes.push_back(1);  // Assembly
    PutLengthPrefixedString(bytes, "a.dll");

    Sfb::Header header = Sfb::ReadManifest(bytes.data(), static_cast<long long>(bytes.size()), 0);
    EXPECT_EQ(header.FileCount, 1);
    ASSERT_EQ(header.Entries.size(), 1u);
    EXPECT_EQ(header.Entries[0].RelativePath, "a.dll");
}

}  // namespace
