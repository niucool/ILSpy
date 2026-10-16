// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// Vendored from dotnet/runtime, src/libraries/Common/src/System/Sha1ForNonSecretPurposes.cs
// (commit 6e51f762bc4c98ea90ae6ca21c4e220b4b2e7a5c) -- see the header for the
// port notes. The algorithm below is the vendored code translated line for
// line; the 32-bit wraparound arithmetic is the unchecked form the C# carries
// explicitly (C++ unsigned arithmetic wraps by definition).

#include "Decompiler/Util/Sha1ForNonSecretPurposes.hpp"

#include <cstring>

namespace ILSpy::Decompiler::Util {

namespace {

constexpr std::uint32_t RotateLeft(std::uint32_t value, int count) noexcept
{
    return (value << count) | (value >> (32 - count));
}

std::uint16_t ReadUInt16BigEndian(const std::uint8_t* p) noexcept
{
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

void WriteUInt32BigEndian(std::uint8_t* p, std::uint32_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>(v >> 24);
    p[1] = static_cast<std::uint8_t>(v >> 16);
    p[2] = static_cast<std::uint8_t>(v >> 8);
    p[3] = static_cast<std::uint8_t>(v);
}

void WriteUInt64BigEndian(std::uint8_t* p, std::uint64_t v) noexcept
{
    p[0] = static_cast<std::uint8_t>(v >> 56);
    p[1] = static_cast<std::uint8_t>(v >> 48);
    p[2] = static_cast<std::uint8_t>(v >> 40);
    p[3] = static_cast<std::uint8_t>(v >> 32);
    p[4] = static_cast<std::uint8_t>(v >> 24);
    p[5] = static_cast<std::uint8_t>(v >> 16);
    p[6] = static_cast<std::uint8_t>(v >> 8);
    p[7] = static_cast<std::uint8_t>(v);
}

} // namespace

void Sha1ForNonSecretPurposes::HashData(Span<const std::uint8_t> source,
                                        Span<std::uint8_t> destination)
{
    std::uint32_t w[85];
    Start(w);

    const std::uint8_t* src = source.data();
    std::size_t remaining = source.size();
    const int originalLength = static_cast<int>(source.size());

    while (remaining >= 64)
    {
        for (int i = 0; i < 16; i++)
        {
            w[i] = ReadUInt16BigEndian(src);
            w[i] = (w[i] << 16) | ReadUInt16BigEndian(src + 2);
            src += 4;
        }
        Drain(w);
        remaining -= 64;
    }

    std::uint8_t tail[2 * 64] = {};
    std::memcpy(tail, src, remaining);
    int pos = static_cast<int>(remaining);
    tail[pos++] = 0x80;
    while ((pos & 63) != 56)
    {
        tail[pos++] = 0x00;
    }
    WriteUInt64BigEndian(tail + pos,
        static_cast<std::uint64_t>(originalLength) * 8);
    const int tailLength = pos + 8;

    int tailPos = 0;
    while (tailPos < tailLength)
    {
        for (int i = 0; i < 16; i++)
        {
            w[i] = ReadUInt16BigEndian(tail + tailPos);
            w[i] = (w[i] << 16) | ReadUInt16BigEndian(tail + tailPos + 2);
            tailPos += 4;
        }
        Drain(w);
    }

    std::uint8_t* out = destination.data();
    for (int i = 80; i < 85; i++)
    {
        WriteUInt32BigEndian(out, w[i]);
        out += 4;
    }
}

void Sha1ForNonSecretPurposes::Start()
{
    Start(w_.data());

    length_ = 0;
    pos_ = 0;
}

void Sha1ForNonSecretPurposes::Start(std::uint32_t* w)
{
    w[80] = 0x67452301;
    w[81] = 0xEFCDAB89;
    w[82] = 0x98BADCFE;
    w[83] = 0x10325476;
    w[84] = 0xC3D2E1F0;
}

void Sha1ForNonSecretPurposes::Append(std::uint8_t input)
{
    int idx = pos_ >> 2;
    w_[static_cast<std::size_t>(idx)] =
        (w_[static_cast<std::size_t>(idx)] << 8) | input;
    if (64 == ++pos_)
    {
        Drain();
    }
}

void Sha1ForNonSecretPurposes::Append(Span<const std::uint8_t> input)
{
    for (std::uint8_t b : input)
    {
        Append(b);
    }
}

void Sha1ForNonSecretPurposes::Finish(Span<std::uint8_t> output)
{
    const std::int64_t l = length_ + 8LL * pos_;
    Append(0x80);
    while (pos_ != 56)
    {
        Append(0x00);
    }

    Append(static_cast<std::uint8_t>(l >> 56));
    Append(static_cast<std::uint8_t>(l >> 48));
    Append(static_cast<std::uint8_t>(l >> 40));
    Append(static_cast<std::uint8_t>(l >> 32));
    Append(static_cast<std::uint8_t>(l >> 24));
    Append(static_cast<std::uint8_t>(l >> 16));
    Append(static_cast<std::uint8_t>(l >> 8));
    Append(static_cast<std::uint8_t>(l));

    std::uint8_t* out = output.data();
    for (int i = 80; i < 85; i++)
    {
        WriteUInt32BigEndian(out, w_[static_cast<std::size_t>(i)]);
        out += 4;
    }
}

void Sha1ForNonSecretPurposes::Drain()
{
    Drain(w_.data());
    length_ += 512; // 64 bytes == 512 bits
    pos_ = 0;
}

void Sha1ForNonSecretPurposes::Drain(std::uint32_t* w)
{
    for (int i = 16; i < 80; i++)
    {
        w[i] = RotateLeft(
            w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    std::uint32_t a = w[80];
    std::uint32_t b = w[81];
    std::uint32_t c = w[82];
    std::uint32_t d = w[83];
    std::uint32_t e = w[84];

    for (int i = 0; i < 20; i++)
    {
        const std::uint32_t k = 0x5A827999;
        std::uint32_t f = (b & c) | ((~b) & d);
        std::uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
        e = d; d = c; c = RotateLeft(b, 30); b = a; a = temp;
    }

    for (int i = 20; i < 40; i++)
    {
        const std::uint32_t k = 0x6ED9EBA1;
        std::uint32_t f = b ^ c ^ d;
        std::uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
        e = d; d = c; c = RotateLeft(b, 30); b = a; a = temp;
    }

    for (int i = 40; i < 60; i++)
    {
        const std::uint32_t k = 0x8F1BBCDC;
        std::uint32_t f = (b & c) | (b & d) | (c & d);
        std::uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
        e = d; d = c; c = RotateLeft(b, 30); b = a; a = temp;
    }

    for (int i = 60; i < 80; i++)
    {
        const std::uint32_t k = 0xCA62C1D6;
        std::uint32_t f = b ^ c ^ d;
        std::uint32_t temp = RotateLeft(a, 5) + f + e + k + w[i];
        e = d; d = c; c = RotateLeft(b, 30); b = a; a = temp;
    }

    w[80] += a;
    w[81] += b;
    w[82] += c;
    w[83] += d;
    w[84] += e;
}

} // namespace ILSpy::Decompiler::Util
