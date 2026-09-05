// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the Sha1ForNonSecretPurposes port (cpp/Decompiler/Util/
// Sha1ForNonSecretPurposes.{hpp,cpp} -- the vendored SHA-1 the
// MetadataExtensions CalculatePublicKeyToken hashing runs through).
//
// Every digest below is gold-pinned two ways: the canonical FIPS vectors
// ("abc" -> a9993e36..., the million-'a' -> 34aa973c...) are the published
// constants of the algorithm, and the whole matrix (including the
// block-boundary shapes at 55/56/57/63/64/65/127/128/129 bytes) was dumped
// from .NET's own System.Security.Cryptography.SHA1.HashData (the reference
// implementation) over the identical inputs by the FullAsmNameProbe gold
// probe (C:/temp-probe/FullAsmNameProbe).

#include "Decompiler/Util/Sha1ForNonSecretPurposes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

using ILSpy::Decompiler::Util::Sha1ForNonSecretPurposes;
using ILSpy::Decompiler::Util::Span;

namespace {

// The probe's input pattern: byte i = (i * 7 + 3) & 0xFF.
std::vector<std::uint8_t> Pattern(std::size_t n)
{
    std::vector<std::uint8_t> b(n);
    for (std::size_t i = 0; i < n; i++)
        b[i] = static_cast<std::uint8_t>(i * 7 + 3);
    return b;
}

std::string Hex(const std::uint8_t* bytes, std::size_t count)
{
    static const char digits[] = "0123456789abcdef";
    std::string result;
    for (std::size_t i = 0; i < count; i++)
    {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 0xF];
    }
    return result;
}

std::string Hex(const std::vector<std::uint8_t>& v) { return Hex(v.data(), v.size()); }

// The HashData digest of an input.
std::string HashOf(const std::vector<std::uint8_t>& input)
{
    std::uint8_t digest[20];
    Sha1ForNonSecretPurposes::HashData(
        Span<const std::uint8_t>(input.data(), input.size()),
        Span<std::uint8_t>(digest, sizeof(digest)));
    return Hex(digest, sizeof(digest));
}

struct VectorCase {
    const char* name;
    std::vector<std::uint8_t> input;
    const char* digest;
};

// The gold vector matrix (the probe's V lines, every value dumped from the
// real .NET SHA1.HashData; the abc and ma1M rows are the canonical FIPS
// constants, which the probe itself cross-checked).
std::vector<VectorCase> GoldVectors()
{
    std::vector<VectorCase> v;
    v.push_back({"empty", {}, "da39a3ee5e6b4b0d3255bfef95601890afd80709"});
    v.push_back({"abc", {'a', 'b', 'c'}, "a9993e364706816aba3e25717850c26c9cd0d89d"});
    // The block-boundary geometry: the 0x80 pad byte lands at index 55 (one
    // block), 56 (forces a second block for the length), and the exact-block
    // multiples plus-or-minus one.
    v.push_back({"p55", Pattern(55), "ddf57317ef34bfee3b6df83d359098930eb278bc"});
    v.push_back({"p56", Pattern(56), "a0d492bb0fc889d0eca3bc137066ab6f4f74f369"});
    v.push_back({"p57", Pattern(57), "11a02dcf95859677a62e75024067c22b165d890f"});
    v.push_back({"p63", Pattern(63), "c55856749bef509bdfe6bfebfc7bf4e793e82132"});
    v.push_back({"p64", Pattern(64), "bede92be29c3874e1b54ddc77988d606fc857a8e"});
    v.push_back({"p65", Pattern(65), "b05a80522b053d6dc7e0a517d0e70212c7dad11f"});
    v.push_back({"p127", Pattern(127), "34d5e582029e9b9b85b2febe31da3db7cdabaaea"});
    v.push_back({"p128", Pattern(128), "a09133e6730ffe899efb70204cb5646cd5dc24ee"});
    v.push_back({"p129", Pattern(129), "808aea332ce367541d37adae7f94e59c5c1a934e"});
    v.push_back({"p1000", Pattern(1000), "4231a8a50a10fa9758db8ec71fdef855b751048a"});
    // The canonical million-'a' vector (a real multi-thousand-block digest).
    std::vector<std::uint8_t> million(1000000, 'a');
    v.push_back({"ma1M", std::move(million), "34aa973cd4c4daa4f61eeb2bdbad27316534016f"});
    v.push_back({"hi8", {0x00, 0x7F, 0x80, 0xFF, 0xFE, 0x01, 0xAA, 0x55},
                 "194567bc8ffdb406c3d58bfa09dfb4ce15f5d99b"});
    return v;
}

} // namespace

// The HashData digests over the full gold matrix.
TEST(Sha1ForNonSecretPurposesTest, HashDataMatchesTheGoldVectors)
{
    for (const VectorCase& c : GoldVectors())
    {
        EXPECT_EQ(HashOf(c.input), c.digest) << "vector " << c.name;
    }
}

// The incremental instance API (Start / Append / Finish) produces the same
// digest as HashData over the identical inputs -- the algorithm-mandated
// equivalence (the C# instance path is the same Start/Drain machinery the
// static HashData drives).
TEST(Sha1ForNonSecretPurposesTest, IncrementalApiMatchesHashData)
{
    for (const VectorCase& c : GoldVectors())
    {
        // Feed the input in mixed shapes: a 33-byte span chunk (not a
        // multiple of the 64-byte block), then a 47-byte chunk, then the
        // remainder byte-by-byte -- so Append(span), the block wrap, and
        // Append(byte) all take part in every case.
        Sha1ForNonSecretPurposes h;
        h.Start();
        std::size_t pos = 0;
        while (pos < c.input.size())
        {
            std::size_t take = std::min<std::size_t>(33, c.input.size() - pos);
            h.Append(Span<const std::uint8_t>(c.input.data() + pos, take));
            pos += take;
            if (pos < c.input.size())
            {
                take = std::min<std::size_t>(47, c.input.size() - pos);
                h.Append(Span<const std::uint8_t>(c.input.data() + pos, take));
                pos += take;
            }
            while (pos < c.input.size() && (pos % 2 == 0 || pos == c.input.size() - 1))
            {
                h.Append(c.input[pos]);
                pos++;
            }
        }
        std::uint8_t digest[20];
        h.Finish(Span<std::uint8_t>(digest, sizeof(digest)));
        EXPECT_EQ(Hex(digest, sizeof(digest)), c.digest) << "vector " << c.name;
        EXPECT_EQ(HashOf(c.input), c.digest) << "vector " << c.name;
    }
}

// A purely byte-at-a-time feed (the Append(byte) path with no span chunks).
TEST(Sha1ForNonSecretPurposesTest, SingleByteAppendMatchesHashData)
{
    for (const VectorCase& c : GoldVectors())
    {
        Sha1ForNonSecretPurposes h;
        h.Start();
        for (std::uint8_t b : c.input)
            h.Append(b);
        std::uint8_t digest[20];
        h.Finish(Span<std::uint8_t>(digest, sizeof(digest)));
        EXPECT_EQ(Hex(digest, sizeof(digest)), c.digest) << "vector " << c.name;
    }
}

// Start() reinitializes the state: a second run over the same input after a
// Finish produces the same digest (the C# doc contract -- "Call Start() to
// reinitialize").
TEST(Sha1ForNonSecretPurposesTest, StartReinitializesAfterFinish)
{
    auto input = Pattern(500);
    Sha1ForNonSecretPurposes h;
    h.Start();
    h.Append(Span<const std::uint8_t>(input.data(), input.size()));
    std::uint8_t first[20];
    h.Finish(Span<std::uint8_t>(first, sizeof(first)));

    h.Start();
    h.Append(Span<const std::uint8_t>(input.data(), input.size()));
    std::uint8_t second[20];
    h.Finish(Span<std::uint8_t>(second, sizeof(second)));

    EXPECT_EQ(Hex(first, sizeof(first)), Hex(second, sizeof(second)));
    EXPECT_EQ(Hex(first, sizeof(first)), "bca4aa6c5708ac105943c9ee56e5d7316982c8b4");
    // The digest equals HashData over the identical input.
    EXPECT_EQ(Hex(first, sizeof(first)), HashOf(input));
}

// HashData writes exactly the 20 digest bytes into the destination span.
TEST(Sha1ForNonSecretPurposesTest, HashDataWritesTwentyBytes)
{
    std::uint8_t digest[20] = {};
    std::vector<std::uint8_t> input(200, 0xAB);
    Sha1ForNonSecretPurposes::HashData(
        Span<const std::uint8_t>(input.data(), input.size()),
        Span<std::uint8_t>(digest, sizeof(digest)));
    EXPECT_EQ(Hex(digest, sizeof(digest)), HashOf(input));
    // The pre-fill is fully overwritten (no byte left at its sentinel 0).
    bool allZero = true;
    for (std::uint8_t b : digest)
        if (b != 0) allZero = false;
    EXPECT_FALSE(allZero);
}
