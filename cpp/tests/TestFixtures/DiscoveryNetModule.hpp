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

// The debug-discovery module machinery shared by the PDB-discovery test
// suites (the associated/embedded discovery and the DebugInfoUtils
// LoadSymbols walk): a patched tiny.netmodule carrying a debug directory
// with the given entries, plus the raw-deflate MPDB blob builders and the
// synthetic-PDB fixtures' #Pdb ID the entry builders need.

#pragma once

#include "TestFixtures/PatchedNetModule.hpp"
#include "TestFixtures/SyntheticPortablePdb.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ILSpy::Tests {

// A raw-deflate stored-block stream (RFC 1951, no zlib wrapper -- the
// format System.IO.Compression.DeflateStream writes): the 3-bit block
// header (BFINAL|BTYPE=00) padded to a byte, then LEN/NLEN per <=65535-byte
// chunk, then the raw bytes. A valid deflate stream the real SRM MPDB
// decode accepts; the tests use it because its bytes are hand-derivable.
inline std::string StoredDeflate(const std::uint8_t* data, std::size_t n) {
    std::string out;
    std::size_t off = 0;
    while (true) {
        std::size_t chunk = std::min(n - off, static_cast<std::size_t>(65535));
        bool last = off + chunk >= n;
        out.push_back(static_cast<char>(last ? 1 : 0));
        std::uint16_t len = static_cast<std::uint16_t>(chunk);
        std::uint16_t nlen = static_cast<std::uint16_t>(len ^ 0xFFFF);
        out.push_back(static_cast<char>(len & 0xFF));
        out.push_back(static_cast<char>((len >> 8) & 0xFF));
        out.push_back(static_cast<char>(nlen & 0xFF));
        out.push_back(static_cast<char>((nlen >> 8) & 0xFF));
        out.append(reinterpret_cast<const char*>(data + off), chunk);
        off += chunk;
        if (off >= n) break;
    }
    return out;
}

// The MPDB blob an EmbeddedPortablePdb entry carries: the "MPDB" signature,
// the declared uncompressed size, then the raw-deflate stream.
inline std::string MpdbBlob(std::int32_t declaredSize,
                            const std::string& deflateStream) {
    std::string b = "MPDB";
    b.push_back(static_cast<char>(declaredSize & 0xFF));
    b.push_back(static_cast<char>((declaredSize >> 8) & 0xFF));
    b.push_back(static_cast<char>((declaredSize >> 16) & 0xFF));
    b.push_back(static_cast<char>((declaredSize >> 24) & 0xFF));
    b += deflateStream;
    return b;
}

// One debug-directory entry the discovery-module builder lays out: the
// entry fields plus the entry's data blob.
struct DiscoveryEntry {
    std::uint32_t Stamp = 0;
    std::uint16_t MajorVersion = 0;
    std::uint16_t MinorVersion = 0;
    std::int32_t Type = 0;
    std::string Blob;
};

// A patched tiny.netmodule carrying the given debug-directory entries: the
// 28-byte entry array at the appended region's start, the entry data blobs
// right after it (each entry's DataRVA/DataPointer at the append point +
// 28*n + its blob's offset). The characteristics field is 0 (reserved); the
// portable-CodeView shape is MinorVersion 0x504D, the embedded shape Type
// 17 with version 1.0 (256/256). The module lands at the fixed temp path
// "ilspy_debugdir_test.netmodule" (WritePatchedNetModule), so the adjacent
// <module>.pdb file the DebugInfoUtils legacy arm looks for is
// "<temp>/ilspy_debugdir_test.pdb".
inline PatchedNetModule WriteDiscoveryModule(
    const std::vector<DiscoveryEntry>& entries) {
    std::size_t n = entries.size();
    // The append point: the last section's raw end (the file offset) and
    // its RVA (the section VA + the raw size) -- the same computation
    // WritePatchedNetModule performs internally.
    std::string base = TinyNetModuleBytes();
    std::size_t lfanew = Rd32(base, 0x3C);
    std::size_t fileHeader = lfanew + 4;
    std::uint16_t numSections = Rd16(base, fileHeader + 2);
    std::uint16_t sizeOfOpt = Rd16(base, fileHeader + 16);
    std::size_t optHeader = fileHeader + 20;
    std::uint16_t magic = Rd16(base, optHeader);
    std::size_t last =
        optHeader + sizeOfOpt + static_cast<std::size_t>(numSections - 1) * 40;
    std::uint32_t va = Rd32(base, last + 12);
    std::uint32_t rawSize = Rd32(base, last + 16);
    std::uint32_t rawPtr = Rd32(base, last + 20);
    std::uint32_t appendRva = va + rawSize;
    std::uint32_t appendPtr = rawPtr + rawSize;

    std::string entryBytes;
    std::string blobs;
    for (const auto& e : entries) {
        std::size_t blobOffset = blobs.size();
        entryBytes += DebugEntry(0, e.Stamp, e.MajorVersion, e.MinorVersion,
            e.Type, static_cast<std::int32_t>(e.Blob.size()),
            static_cast<std::int32_t>(appendRva + 28 * n + blobOffset),
            static_cast<std::int32_t>(appendPtr + 28 * n + blobOffset));
        blobs += e.Blob;
    }
    return WritePatchedNetModule(
        entryBytes + blobs, appendRva, static_cast<std::uint32_t>(28 * n));
}

// The synthetic PDB fixture's #Pdb ID: the 16 GUID bytes + the stamp (the
// uint32 after them) -- the ID the patched PE entries carry.
struct FixtureId {
    std::array<std::uint8_t, 16> Guid{};
    std::uint32_t Stamp = 0;
    FixtureId() {
        ILSpy::Decompiler::Metadata::PortablePdb pdb = LoadSyntheticPdb();
        const std::uint8_t* id = pdb.PdbId();
        std::memcpy(Guid.data(), id, 16);
        Stamp = static_cast<std::uint32_t>(id[16])
              | (static_cast<std::uint32_t>(id[17]) << 8)
              | (static_cast<std::uint32_t>(id[18]) << 16)
              | (static_cast<std::uint32_t>(id[19]) << 24);
    }
};

// A portable-CodeView entry whose CV_INFO_PDB70 blob carries the given ID
// (the GUID) and PDB path (the discovery resolves the path's FILE NAME
// against the PE image's own directory).
inline DiscoveryEntry PortableCodeViewEntry(const FixtureId& id,
                                            const std::string& cvPath) {
    DiscoveryEntry e;
    e.Stamp = id.Stamp;
    e.MajorVersion = 0x0100;
    e.MinorVersion = 0x504D;
    e.Type = static_cast<std::int32_t>(
        ILSpy::Decompiler::Disassembler::DebugDirectoryEntryType::CodeView);
    e.Blob = RsdsBlob(id.Guid, 1, cvPath);
    return e;
}

// An EmbeddedPortablePdb entry (version 1.0) carrying the MPDB blob.
inline DiscoveryEntry EmbeddedEntry(std::int32_t declaredSize,
                                     const std::string& deflateStream) {
    DiscoveryEntry e;
    e.Stamp = 2;
    e.MajorVersion = 0x0100;
    e.MinorVersion = 0x0100;
    e.Type = static_cast<std::int32_t>(
        ILSpy::Decompiler::Disassembler::DebugDirectoryEntryType::
            EmbeddedPortablePdb);
    e.Blob = MpdbBlob(declaredSize, deflateStream);
    return e;
}

// Writes the synthetic PDB fixture bytes at the given full path (the file
// an adjacent or associated discovery opens). False when the file could
// not be written.
inline bool WriteSyntheticPdbFile(const std::string& path) {
    auto bytes = SyntheticPdbBytes();
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    std::fwrite(bytes->data(), 1, bytes->size(), f);
    std::fclose(f);
    return true;
}

}  // namespace ILSpy::Tests
