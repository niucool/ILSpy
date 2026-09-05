// Licensed to the .NET Foundation under one or more agreements.
// The .NET Foundation licenses this file to you under the MIT license.

// Vendored from dotnet/runtime, src/libraries/Common/src/System/Sha1ForNonSecretPurposes.cs
// (commit 6e51f762bc4c98ea90ae6ca21c4e220b4b2e7a5c), as this repo's C# tree carries
// it (ICSharpCode.Decompiler/Util/Sha1ForNonSecretPurposes.cs), and ported to C++
// for the cpp tree. Like the C# copy, the port keeps the wrapping 32-bit
// arithmetic (SHA-1 depends on it) and the non-secret-purposes contract: this is
// the SHA-1 used for strong-name public-key tokens, kept as a self-contained
// implementation so reading assembly metadata stays independent of the host
// crypto policy.
//
// Port notes (the vendored file keeps its .NET Foundation header above; the
// naming and shape below mirror the C# struct member-for-member):
//   * The C# `namespace System` placement (the vendored file's own convention)
//     moves to `ILSpy::Decompiler::Util` -- the port never places code in the
//     global System namespace, and the C# placement itself is only a vendoring
//     artifact.
//   * The C# `ReadOnlySpan<byte>` / `Span<byte>` parameters port to
//     `Span<const std::uint8_t>` / `Span<std::uint8_t>` (the C++17 Span
//     polyfill in Decompiler/Util/Span.hpp).
//   * The C# lazily allocated `uint[] _w` workspace (created on the first
//     Start()) is a by-value `std::array<std::uint32_t, 85>` member; the C#
//     struct copies its heap array reference on copy, the C++ class copies the
//     array contents -- observable only through the same instance's continued
//     use, which the API contract already requires (Finish invalidates the
//     state; Start() reinitializes it).

#pragma once

#include "Decompiler/Util/Span.hpp"

#include <array>
#include <cstdint>

namespace ILSpy::Decompiler::Util {

/// Implements the SHA1 hashing algorithm. Note that
/// implementation is for hashing public information. Do not
/// use code to hash private data, as implementation does not
/// take any steps to avoid information disclosure.
class Sha1ForNonSecretPurposes {
public:
    /// Computes the SHA1 hash of the provided data.
    static void HashData(Span<const std::uint8_t> source,
                         Span<std::uint8_t> destination);

    /// Call Start() to initialize the hash object.
    void Start();

    /// Adds an input byte to the hash.
    void Append(std::uint8_t input);

    /// Adds input bytes to the hash.
    void Append(Span<const std::uint8_t> input);

    /// Retrieves the hash value.
    /// Note that after calling function, the hash object should
    /// be considered uninitialized. Subsequent calls to Append or
    /// Finish will produce useless results. Call Start() to
    /// reinitialize.
    void Finish(Span<std::uint8_t> output);

private:
    static void Start(std::uint32_t* w);
    static void Drain(std::uint32_t* w);
    void Drain();

    // Total message length in bits (completed 64-byte chunks).
    std::int64_t length_ = 0;
    // The workspace: the current chunk's words (0..79) then the five
    // state words (80..84).
    std::array<std::uint32_t, 85> w_{};
    // Length of current chunk in bytes.
    int pos_ = 0;
};

} // namespace ILSpy::Decompiler::Util
