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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The hand-patched tiny.netmodule PE machinery shared by the debug-directory
// test suites: the PE-header byte helpers plus WritePatchedNetModule, which
// appends debug bytes past the fixture's last section (the section's
// VirtualSize/SizeOfRawData extended to cover them) and points the debug data
// directory (optional-header data directory index 6) at the appended region.
// The failure arms no installed assembly carries are exercised over these
// patched copies; the 28-byte IMAGE_DEBUG_DIRECTORY row builder (DebugEntry)
// and the CV_INFO_PDB70 blob builder (RsdsBlob) ride along.

#pragma once

#include "TestFixtures/TinyNetModule.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

namespace ILSpy::Tests {

inline std::uint32_t Rd32(const std::string& b, std::size_t off) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(b[off]))
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 1])) << 8)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 2])) << 16)
         | (static_cast<std::uint32_t>(static_cast<unsigned char>(b[off + 3])) << 24);
}

inline std::uint16_t Rd16(const std::string& b, std::size_t off) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(b[off])
        | (static_cast<unsigned char>(b[off + 1]) << 8));
}

inline void Wr32(std::string& b, std::size_t off, std::uint32_t v) {
    b[off] = static_cast<char>(v & 0xFF);
    b[off + 1] = static_cast<char>((v >> 8) & 0xFF);
    b[off + 2] = static_cast<char>((v >> 16) & 0xFF);
    b[off + 3] = static_cast<char>((v >> 24) & 0xFF);
}

inline void Wr16(std::string& b, std::size_t off, std::uint16_t v) {
    b[off] = static_cast<char>(v & 0xFF);
    b[off + 1] = static_cast<char>((v >> 8) & 0xFF);
}

// A patched copy of the tiny.netmodule: `debugBytes` are appended past the
// last section's raw extent (the section's VirtualSize/SizeOfRawData
// extended to cover them), the debug data directory (optional-header data
// directory index 6) pointed at `dirRva`/`dirSize`, and the result written
// to a temp file. The appended region starts at the last section's raw end,
// which the caller needs to fill in the entry fields (DataPointer is the raw
// FILE offset; DataRelativeVirtualAddress the RVA).
struct PatchedNetModule {
    std::string Path;
    std::uint32_t AppendRva = 0;  // the RVA of the first appended byte
    std::uint32_t AppendPtr = 0;  // the file offset of the first appended byte
};

inline PatchedNetModule WritePatchedNetModule(const std::string& debugBytes,
                                               std::uint32_t dirRva,
                                               std::uint32_t dirSize) {
    std::string bytes = TinyNetModuleBytes();
    std::size_t lfanew = Rd32(bytes, 0x3C);
    std::size_t fileHeader = lfanew + 4;
    std::uint16_t numSections = Rd16(bytes, fileHeader + 2);
    std::uint16_t sizeOfOpt = Rd16(bytes, fileHeader + 16);
    std::size_t optHeader = fileHeader + 20;
    std::uint16_t magic = Rd16(bytes, optHeader);
    // PE32 (0x10B) and PE32+ (0x20B) place the data directories at different
    // optional-header offsets (96 fixed bytes vs 112).
    std::size_t dataDirs = optHeader + (magic == 0x20B ? 112 : 96);
    std::size_t debugDir = dataDirs + 6 * 8;
    std::size_t last =
        optHeader + sizeOfOpt + static_cast<std::size_t>(numSections - 1) * 40;
    std::uint32_t va = Rd32(bytes, last + 12);       // VirtualAddress
    std::uint32_t rawSize = Rd32(bytes, last + 16);  // SizeOfRawData
    std::uint32_t rawPtr = Rd32(bytes, last + 20);   // PointerToRawData

    // The appended region: at the last section's raw end, both as a file
    // offset and as an RVA (the section's VA + the raw offset into it).
    std::uint32_t rawEnd = rawPtr + rawSize;
    PatchedNetModule result;
    result.AppendPtr = rawEnd;
    result.AppendRva = va + rawSize;
    bytes.resize(rawEnd);
    bytes += debugBytes;
    // Extend the last section so the appended region is inside its virtual
    // and raw extents (the RVA the directory carries must resolve).
    Wr32(bytes, last + 8, rawSize + static_cast<std::uint32_t>(debugBytes.size()));
    Wr32(bytes, last + 16, rawSize + static_cast<std::uint32_t>(debugBytes.size()));
    // The debug data directory.
    Wr32(bytes, debugDir, dirRva);
    Wr32(bytes, debugDir + 4, dirSize);

    namespace fs = std::filesystem;
    fs::path path = fs::temp_directory_path() / "ilspy_debugdir_test.netmodule";
    std::FILE* out = std::fopen(path.string().c_str(), "wb");
    if (out == nullptr) return {};
    std::fwrite(bytes.data(), 1, bytes.size(), out);
    std::fclose(out);
    result.Path = path.string();
    return result;
}

// A 28-byte IMAGE_DEBUG_DIRECTORY row.
inline std::string DebugEntry(std::uint32_t characteristics, std::uint32_t stamp,
                              std::uint16_t major, std::uint16_t minor,
                              std::int32_t type, std::int32_t dataSize,
                              std::int32_t dataRva, std::int32_t dataPtr) {
    std::string b(28, '\0');
    Wr32(b, 0, characteristics);
    Wr32(b, 4, stamp);
    Wr16(b, 8, major);
    Wr16(b, 10, minor);
    Wr32(b, 12, static_cast<std::uint32_t>(type));
    Wr32(b, 16, static_cast<std::uint32_t>(dataSize));
    Wr32(b, 20, static_cast<std::uint32_t>(dataRva));
    Wr32(b, 24, static_cast<std::uint32_t>(dataPtr));
    return b;
}

// A CV_INFO_PDB70 blob: "RSDS" + the 16 GUID bytes + the age + the path. The
// path is NUL-terminated by default; the un-terminated shape pins the C#
// ReadUtf8NullTerminated end-of-blob behavior (the whole remaining block).
inline std::string RsdsBlob(const std::array<std::uint8_t, 16>& guid,
                            std::int32_t age, const std::string& path,
                            bool terminated = true) {
    std::string b;
    b += "RSDS";
    b.append(reinterpret_cast<const char*>(guid.data()), 16);
    b.push_back(static_cast<char>(age & 0xFF));
    b.push_back(static_cast<char>((age >> 8) & 0xFF));
    b.push_back(static_cast<char>((age >> 16) & 0xFF));
    b.push_back(static_cast<char>((age >> 24) & 0xFF));
    b += path;
    if (terminated) b.push_back('\0');
    return b;
}

}  // namespace ILSpy::Tests
