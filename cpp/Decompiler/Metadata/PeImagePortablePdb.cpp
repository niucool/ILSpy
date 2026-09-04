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

// The PE-image side of the associated/embedded portable-PDB discovery -- the
// port of the PEReader members the C# `DebugInfoUtils.LoadSymbols` chain
// consumes (`PEReader.TryOpenAssociatedPortablePdb` and
// `PEReader.ReadEmbeddedPortablePdbDebugDirectoryData`, System.Reflection.PortableExecutable/PEReader.cs):
// the portable-CodeView entry's PDB file (the entry's path resolved against
// the PE image's own directory, matched by BlobContentId against the PDB's
// #Pdb ID) with the embedded MPDB blob (raw deflate, miniz) as the fallback.
//
// C#-to-C++ porting decisions:
//  * The `Stream` the C# provider returns is a stream of bytes the
//    FromPortablePdbStream path reads whole; the port's provider returns
//    those bytes directly (null for not-found, the C# FileNotFoundException
//    catch shape).
//  * The C# records the first BadImageFormatException/IOException in
//    `errorToReport` and rethrows it at the end of the discovery when
//    nothing opened (ExceptionDispatchInfo); the port records through
//    std::exception_ptr and rethrows the same way. The port maps
//    BadImageFormatException to std::out_of_range (the decoders' convention)
//    and an IOException-ish provider failure to whatever the provider threw
//    (std::runtime_error and derivatives) -- both record into the same
//    error slot and rethrow unchanged.
//  * The `MetadataReaderProvider` the C# returns collapses into the port's
//    PortablePdb reader value (the provider+reader pair collapsed at
//    construction; the parse IS the construction, so a garbage associated
//    file records the parse error the C# GetMetadataReader throw records).
//  * The MPDB blob's deflate stream is RAW deflate (RFC 1951, no zlib
//    wrapper -- the format System.IO.Compression.DeflateStream writes), so
//    the decode drives tinfl directly (the non-wrapping buffer mode) rather
//    than the zlib-wrapped mz_uncompress.
//  * A C# decode edge that escapes every catch (a BlobReader
//    ArgumentOutOfRangeException on an MPDB blob shorter than its 8-byte
//    header; a null DebugMetadataHeader NullReferenceException on a PDB
//    without a #Pdb stream) is mapped onto the port's recorded-error /
//    plain-miss outcomes instead -- the port never crashes on malformed
//    input (the graceful-degradation tenet).

#include "Decompiler/Metadata/MethodBodyReader.hpp"

#include <miniz/miniz_tinfl.h>

#include <array>
#include <cstring>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The C# `System.Reflection.Metadata.BlobContentId` -- the 20-byte (Guid,
// Stamp) pair that pairs a PDB with its PE image: the PE's CodeView entry
// carries the GUID and the entry's Stamp (the TimeDateStamp), the PDB's
// #Pdb stream carries the same 20 bytes as its ID. Kept file-local: the
// discovery is its only port consumer (the C# type is public for the
// -genpdb writer, which will hoist it when it lands).
struct BlobContentId {
    std::array<std::uint8_t, 16> Guid{};
    std::uint32_t Stamp = 0;

    BlobContentId() = default;
    BlobContentId(const std::array<std::uint8_t, 16>& guid, std::uint32_t stamp)
        : Guid(guid), Stamp(stamp) {}

    // The C# `BlobContentId(ImmutableArray<byte> id)` over a PDB's 20-byte
    // #Pdb ID: the Guid (16 raw bytes) then the Stamp (the uint32 after
    // them). The Guid compares bytewise (the canonical little-endian
    // storage form), so the whole comparison is bytewise.
    static BlobContentId FromId(const std::uint8_t* id) {
        BlobContentId result;
        std::memcpy(result.Guid.data(), id, 16);
        result.Stamp = ReadLe<4>(id + 16);
        return result;
    }

    bool operator==(const BlobContentId& other) const {
        return Guid == other.Guid && Stamp == other.Stamp;
    }
    bool operator!=(const BlobContentId& other) const { return !(*this == other); }
};

// The C# `Path.GetDirectoryName(peImagePath)` for the paths in play (a rooted
// or relative file path): everything before the last separator, excluding
// the separator itself; the empty string for a bare file name (both '/' and
// '\\' separate; a root form like "C:\\" has no file name after it and is
// not a shape the discovery produces).
std::string DirectoryNameOf(const std::string& path) {
    std::size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return {};
    return path.substr(0, pos);
}

// The C# `PathUtilities.GetFileName(path)`: the substring after the last
// separator ('\\', '/', or ':' -- a volume separator also terminates a file
// name), or the whole string when it has none.
std::string FileNameOf(const std::string& path) {
    std::size_t pos = path.find_last_of("\\/:");
    if (pos == std::string::npos) return path;
    return path.substr(pos + 1);
}

// The C# `PathUtilities.CombinePathWithRelativePath(root, relativePath)`:
// an empty root yields the relative path; a root ending in a separator (or
// a volume separator) concatenates directly; anything else inserts the
// platform's own separator.
std::string CombinePathWithRelativePath(const std::string& root,
                                        const std::string& relativePath) {
    if (root.empty()) return relativePath;
    char last = root[root.size() - 1];
    if (last == '\\' || last == '/' || last == ':') return root + relativePath;
#if defined(_WIN32)
    constexpr const char* kSeparator = "\\";
#else
    constexpr const char* kSeparator = "/";
#endif
    return root + kSeparator + relativePath;
}

// The C# `PortablePdbVersions.Format(version)`: "{major}.{minor}" of the
// raw 16-bit version fields' high/low bytes.
std::string FormatVersion(std::uint16_t version) {
    return std::to_string(version >> 8) + "." + std::to_string(version & 0xFF);
}

// The C# `PEReader.ValidateEmbeddedPortablePdbVersion` (internal static):
// the embedded-PDB entry's version fields must be exactly 1.0 (256/256);
// anything below 256 in the major field or anything but 256 in the minor
// field throws (the C# BadImageFormatException -> std::out_of_range).
void ValidateEmbeddedPortablePdbVersion(const PeImage::DebugDirectoryEntry& entry) {
    std::uint16_t majorVersion = entry.MajorVersion;
    if (majorVersion < 256)
        throw std::out_of_range(
            "Unsupported format version: " + FormatVersion(majorVersion));
    std::uint16_t minorVersion = entry.MinorVersion;
    if (minorVersion != 256)
        throw std::out_of_range(
            "Unsupported format version: " + FormatVersion(minorVersion));
}

// The C# `PEReader.DecodeEmbeddedPortablePdbDebugDirectoryData` (internal
// static): the MPDB blob -- the "MPDB" signature, the declared uncompressed
// size, and the raw-deflate stream -- inflated to the uncompressed PDB
// bytes. The C# checks the inflate produced exactly the declared size
// (CopyTo then num2 != num throws SizeMismatch) and that no more INFLATED
// data remains (ReadByte != -1 -- a read of the inflated stream, which
// ends at the deflate end-of-block marker; trailing bytes past the
// marker in the underlying block are accepted); a truncated or corrupt
// deflate stream throws in C# too (the DeflateStream error wrapped into
// a BadImageFormatException); the port maps every one of these to
// std::out_of_range (the discovery records and rethrows it when nothing
// opens, and DebugInfoUtils.LoadSymbols catches and degrades the same
// way). A negative or unallocatable declared size is the C# DataTooBig
// allocation arm ("Data too big to fit in memory.").
std::shared_ptr<const std::vector<std::uint8_t>>
DecodeEmbeddedPortablePdbDebugDirectoryData(const PeImage::SectionDataView& block) {
    constexpr std::uint32_t kEmbeddedSignature = 0x4244504Du;  // "MPDB"
    if (block.length < 8)
        throw std::out_of_range("Truncated embedded Portable PDB data");
    if (ReadLe<4>(block.base) != kEmbeddedSignature)
        throw std::out_of_range(
            "Unexpected Embedded Portable PDB data signature value.");
    std::int64_t declaredSize = static_cast<std::int32_t>(ReadLe<4>(block.base + 4));
    if (declaredSize < 0)
        throw std::out_of_range("Data too big to fit in memory.");

    const std::uint8_t* src = block.base + 8;
    std::size_t srcSize = block.length - 8;
    auto out = std::make_shared<std::vector<std::uint8_t>>();
    try {
        out->resize(static_cast<std::size_t>(declaredSize));
    } catch (const std::bad_alloc&) {
        // The C# NativeHeapMemoryBlock allocation failure, wrapped into the
        // DataTooBig BadImageFormatException.
        throw std::out_of_range("Data too big to fit in memory.");
    }
    if (srcSize == 0) {
        // No deflate stream at all: the C# DeflateStream over the empty
        // remainder reads to its end (ReadByte == -1), so the copy produced
        // nothing -- the declared size must be zero too.
        if (declaredSize != 0)
            throw std::out_of_range(
                "Declared size doesn't correspond to the actual size.");
        return out;
    }
    // Raw deflate into the exact-size output buffer (the C# CopyTo into
    // the NativeHeapMemoryBlock of the declared size). A null destination
    // for a zero declared size never writes (an empty stream), so a dummy
    // byte stands in for the empty vector's data().
    std::uint8_t dummy = 0;
    std::uint8_t* dest = !out->empty() ? out->data() : &dummy;
    size_t inSize = srcSize;
    size_t outSize = static_cast<size_t>(declaredSize);
    tinfl_decompressor decomp;
    tinfl_init(&decomp);
    tinfl_status status = tinfl_decompress(
        &decomp, src, &inSize, dest, dest, &outSize,
        TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (status == TINFL_STATUS_DONE) {
        // The whole stream decoded (the declared size is the output the C#
        // CopyTo wrote; trailing bytes past the deflate stream's end-of-
        // block are ACCEPTED -- the C# ReadByte != -1 probe reads the
        // inflated stream, which ends at the end-of-block marker, not the
        // underlying block). Only the produced count must line up.
        if (outSize != static_cast<size_t>(declaredSize))
            throw std::out_of_range(
                "Declared size doesn't correspond to the actual size.");
        return out;
    }
    if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
        // The output buffer filled before the stream ended: the C# CopyTo
        // stops at the declared size and the trailing ReadByte != -1.
        throw std::out_of_range(
            "Declared size doesn't correspond to the actual size.");
    }
    if (status == TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS) {
        // The input ran out mid-stream: the C# CopyTo gets the truncated
        // count (the DeflateStream treats the exhausted input as the end)
        // and num2 != num throws SizeMismatch -- the trunc.dll probe shape.
        throw std::out_of_range(
            "Declared size doesn't correspond to the actual size.");
    }
    // A corrupt deflate stream: the C# DeflateStream throws and the decode
    // wraps it into a BadImageFormatException carrying the inner message.
    throw std::out_of_range(
        "Invalid deflate stream in embedded Portable PDB data.");
}

// The C# `PEReader.TryOpenPortablePdbFile` (private static): opens `path`
// through the provider and matches the PDB's #Pdb ID against `id`. A file
// the provider does not serve is a miss (false, no error); a file that
// parses but does not match is a miss too; a file that fails to parse
// records the error (the C# GetMetadataReader() BadImageFormatException)
// and misses. A provider that throws an IO-ish error (std::runtime_error)
// records it the way the C# records the provider's IOException.
bool TryOpenPortablePdbFile(const std::string& path, const BlobContentId& id,
    const PdbStreamProvider& pdbFileStreamProvider, PortablePdb& provider,
    std::exception_ptr& errorToReport) {
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    try {
        bytes = pdbFileStreamProvider(path);
        if (!bytes) return false;
        PortablePdb pdb(bytes);
        if (!pdb.IsValid()) {
            // The C# GetMetadataReader() parse of a garbage file throws
            // BadImageFormatException; the catch records it (the discovery
            // rethrows it when nothing opens).
            if (!errorToReport)
                errorToReport = std::make_exception_ptr(std::out_of_range(
                    "Invalid portable PDB: the file does not contain valid metadata."));
            return false;
        }
        const std::uint8_t* pdbId = pdb.PdbId();
        if (pdbId == nullptr) {
            // A PDB without a #Pdb stream: the C# DebugMetadataHeader null
            // would crash the discovery (an uncaught
            // NullReferenceException); the port treats it as a plain miss.
            return false;
        }
        if (BlobContentId::FromId(pdbId) != id) return false;
        provider = std::move(pdb);
        return true;
    } catch (const std::out_of_range&) {
        // The BadImageFormatException filter arm.
        if (!errorToReport) errorToReport = std::current_exception();
        return false;
    } catch (const std::runtime_error&) {
        // The IOException filter arm.
        if (!errorToReport) errorToReport = std::current_exception();
        return false;
    }
}

// The C# `PEReader.TryOpenCodeViewPortablePdb` (private): reads the
// entry's CV_INFO_PDB70 data (a decode failure records the error and
// misses -- the C# BadImageFormatException/IOException catch), forms the
// entry's BlobContentId (the CV GUID + the ENTRY's Stamp -- the
// TimeDateStamp, not the CV age), and opens the entry's PDB path resolved
// against the PE image's own directory (the CV path's FILE NAME in that
// directory -- the C# PathUtilities.GetFileName/CombinePathWithRelativePath
// pair).
bool TryOpenCodeViewPortablePdb(const PeImage& image,
    const PeImage::DebugDirectoryEntry& codeViewEntry,
    const std::string& peImageDirectory, const PdbStreamProvider& provider,
    PortablePdb& outProvider, std::string& outPdbPath,
    std::exception_ptr& errorToReport) {
    PeImage::CodeViewDebugDirectoryData cv;
    try {
        cv = image.ReadCodeViewDebugDirectoryData(codeViewEntry);
    } catch (const std::out_of_range&) {
        // The BadImageFormatException arm (a truncated or non-RSDS blob).
        if (!errorToReport) errorToReport = std::current_exception();
        return false;
    }
    BlobContentId id(cv.Guid, codeViewEntry.Stamp);
    std::string path = CombinePathWithRelativePath(peImageDirectory, FileNameOf(cv.Path));
    if (TryOpenPortablePdbFile(path, id, provider, outProvider, errorToReport)) {
        outPdbPath = path;
        return true;
    }
    return false;
}

// The C# `PEReader.TryOpenEmbeddedPortablePdb` (private): reads and
// inflates the embedded MPDB blob; any BadImageFormatException/IOException
// (the port's std::out_of_range/std::runtime_error) records the error and
// misses, and so does a blob that inflates but does not parse as a PDB
// (the C# GetMetadataReader validation right after the read).
bool TryOpenEmbeddedPortablePdb(const PeImage& image,
    const PeImage::DebugDirectoryEntry& embeddedPdbEntry, PortablePdb& outProvider,
    std::exception_ptr& errorToReport) {
    try {
        PortablePdb pdb = image.ReadEmbeddedPortablePdbDebugDirectoryData(embeddedPdbEntry);
        if (!pdb.IsValid()) {
            if (!errorToReport)
                errorToReport = std::make_exception_ptr(std::out_of_range(
                    "Invalid portable PDB: the file does not contain valid metadata."));
            return false;
        }
        outProvider = std::move(pdb);
        return true;
    } catch (const std::out_of_range&) {
        if (!errorToReport) errorToReport = std::current_exception();
        return false;
    } catch (const std::runtime_error&) {
        if (!errorToReport) errorToReport = std::current_exception();
        return false;
    }
}

}  // namespace

// See MethodBodyReader.hpp for the full contract (the header declares).
bool PeImage::TryOpenAssociatedPortablePdb(const std::string& peImagePath,
    const PdbStreamProvider& pdbFileStreamProvider,
    PortablePdb& pdbReaderProvider, std::string& pdbPath) const {
    pdbReaderProvider = PortablePdb(nullptr);
    pdbPath.clear();
    std::string directoryName = DirectoryNameOf(peImagePath);
    std::exception_ptr errorToReport;
    // The C# ReadDebugDirectory() call sits outside every try: a malformed
    // debug directory throws straight out (the caller's catch degrades).
    std::vector<DebugDirectoryEntry> entries = ReadDebugDirectory();
    // FirstOrDefault(IsPortableCodeView): the CodeView entry a portable PDB
    // was emitted next to (MinorVersion 20557, "MP" -- the SRM
    // IsPortableCodeView predicate, no Type check).
    const DebugDirectoryEntry* codeViewEntry = nullptr;
    for (const auto& e : entries) {
        if (e.MinorVersion == 0x504D) {
            codeViewEntry = &e;
            break;
        }
    }
    if (codeViewEntry != nullptr && codeViewEntry->DataSize != 0
        && TryOpenCodeViewPortablePdb(*this, *codeViewEntry, directoryName,
               pdbFileStreamProvider, pdbReaderProvider, pdbPath, errorToReport))
        return true;
    // FirstOrDefault(Type == EmbeddedPortablePdb): the embedded MPDB blob.
    const DebugDirectoryEntry* embeddedPdbEntry = nullptr;
    for (const auto& e : entries) {
        if (e.Type == static_cast<std::int32_t>(
                Disassembler::DebugDirectoryEntryType::EmbeddedPortablePdb)) {
            embeddedPdbEntry = &e;
            break;
        }
    }
    if (embeddedPdbEntry != nullptr && embeddedPdbEntry->DataSize != 0) {
        PortablePdb provider(nullptr);
        if (TryOpenEmbeddedPortablePdb(*this, *embeddedPdbEntry, provider, errorToReport)) {
            pdbReaderProvider = std::move(provider);
            // pdbPath stays empty (the C# null): the PDB is embedded in the
            // image itself.
            return true;
        }
    }
    if (errorToReport) std::rethrow_exception(errorToReport);
    return false;
}

// See MethodBodyReader.hpp for the full contract (the header declares).
PortablePdb PeImage::ReadEmbeddedPortablePdbDebugDirectoryData(
    const DebugDirectoryEntry& entry) const {
    if (entry.Type != static_cast<std::int32_t>(
            Disassembler::DebugDirectoryEntryType::EmbeddedPortablePdb))
        throw std::invalid_argument("entry is not an EmbeddedPortablePdb entry");
    ValidateEmbeddedPortablePdbVersion(entry);
    SectionDataView data = GetDebugDirectoryEntryData(entry);
    // The C# wraps the inflated NativeHeapMemoryBlock in a
    // MetadataReaderProvider; the port's reader parses the bytes itself.
    return PortablePdb(DecodeEmbeddedPortablePdbDebugDirectoryData(data));
}

}  // namespace ILSpy::Decompiler::Metadata
