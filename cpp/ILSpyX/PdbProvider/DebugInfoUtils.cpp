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

// Port of ICSharpCode.ILSpyX/PdbProvider/DebugInfoUtils.cs -- see the
// header for the porting decisions. Every LoadSymbols/FromFile outcome the
// reachable arms produce was pinned against the real C# DebugInfoUtils
// (the DiuProbe SDK-10 project over the identical fixtures) by the test
// suite (tests/ILSpyX/PdbProvider/DebugInfoUtils_Test.cpp).

#include "ILSpyX/PdbProvider/DebugInfoUtils.hpp"

#include "Decompiler/Metadata/PortablePdb.hpp"
#include "ILSpyX/PdbProvider/PortableDebugInfoProvider.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::ILSpyX::PdbProvider {

namespace {

namespace DI = ILSpy::Decompiler::DebugInfo;
namespace Md = ILSpy::Decompiler::Metadata;

using ILSpy::Decompiler::Disassembler::DebugDirectoryEntryType;

// The C# `const string LegacyPDBPrefix = "Microsoft C/C++ MSF 7.00"`: the
// native Windows PDB (MSF) file header every legacy PDB file starts with.
constexpr char kLegacyPdbPrefix[] = "Microsoft C/C++ MSF 7.00";

// The C# `Path.GetDirectoryName(path)` for the paths in play (a rooted or
// relative file path): everything before the last separator, excluding the
// separator itself; the empty string for a bare file name.
std::string DirectoryNameOf(const std::string& path) {
    std::size_t pos = path.find_last_of("\\/");
    if (pos == std::string::npos) return {};
    return path.substr(0, pos);
}

// The C# `Path.GetFileNameWithoutExtension(path)`: the file-name component
// (after the last separator) minus everything from its last '.' (the whole
// component when it starts with one -- the C# treats a leading dot as the
// extension separator). The empty string for a path ending in a separator.
std::string FileNameWithoutExtensionOf(const std::string& path) {
    std::size_t sep = path.find_last_of("\\/");
    std::string name = sep == std::string::npos ? path : path.substr(sep + 1);
    std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// The C# `Path.Combine(dir, name)`: an empty dir yields the name; a dir
// ending in a separator concatenates directly; anything else inserts the
// platform's own separator.
std::string CombinePaths(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    if (last == '\\' || last == '/' || last == ':') return dir + name;
#if defined(_WIN32)
    constexpr const char* kSeparator = "\\";
#else
    constexpr const char* kSeparator = "/";
#endif
    return dir + kSeparator + name;
}

// The C# `static Stream? OpenStream(string fileName)`: the whole file in
// memory, null when it does not exist. A file that exists but cannot be
// opened throws (the C# File.OpenRead IOException, which no catch on this
// chain absorbs -- the port's std::runtime_error carries it).
std::shared_ptr<const std::vector<std::uint8_t>> OpenStream(
    const std::string& fileName) {
    std::error_code ec;
    if (!std::filesystem::exists(fileName, ec)) {
        return nullptr;
    }
    std::ifstream f(fileName, std::ios::binary);
    if (!f.is_open()) {
        throw std::runtime_error("DebugInfoUtils: cannot open " + fileName);
    }
    f.seekg(0, std::ios::end);
    auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    auto bytes = std::make_shared<std::vector<std::uint8_t>>();
    bytes->resize(static_cast<std::size_t>(size));
    if (bytes->size() != 0) {
        f.read(reinterpret_cast<char*>(bytes->data()),
            static_cast<std::streamsize>(bytes->size()));
    }
    return bytes;
}

// True when the bytes start with the native MSF header (the C#
// stream.Read == prefix length && ASCII compare; a shorter file never
// matches -- it falls through to the portable parse).
bool HasLegacyPdbPrefix(const std::vector<std::uint8_t>& bytes) {
    constexpr std::size_t kPrefixLength = sizeof(kLegacyPdbPrefix) - 1;
    return bytes.size() >= kPrefixLength
        && std::memcmp(bytes.data(), kLegacyPdbPrefix, kPrefixLength) == 0;
}

// The C# `static bool TryOpenPortablePdb(PEFile module, out
// MetadataReaderProvider? provider, out string? pdbFileName)`: the PE
// debug-directory walk. The first portable-CodeView entry routes through
// the associated/embedded discovery (returning its result verbatim -- a
// false is a clean miss, a true carries the open PDB, and its parse
// failures throw the BadImageFormatException LoadSymbols catches); a
// legacy CodeView entry probes the adjacent <module>.pdb file -- a native
// MSF file returns false (the MonoCecil caller's business), a portable one
// opens unconditionally (NO ID match here, unlike the associated
// discovery), a missing file continues the walk.
bool TryOpenPortablePdb(const Md::MetadataFile& module,
                        Md::PortablePdb& provider, std::string& pdbFileName) {
    provider = Md::PortablePdb(nullptr);
    pdbFileName.clear();
    for (const Md::MetadataFile::DebugDirectoryEntryInfo& entry :
        module.GetDebugDirectoryEntries()) {
        if (entry.IsPortableCodeView()) {
            return module.TryOpenAssociatedPortablePdb(
                module.FileName(), OpenStream, provider, pdbFileName);
        }
        if (entry.Type == DebugDirectoryEntryType::CodeView) {
            pdbFileName = CombinePaths(DirectoryNameOf(module.FileName()),
                FileNameWithoutExtensionOf(module.FileName()) + ".pdb");
            std::shared_ptr<const std::vector<std::uint8_t>> bytes
                = OpenStream(pdbFileName);
            if (bytes != nullptr) {
                if (HasLegacyPdbPrefix(*bytes)) {
                    return false;
                }
                // The C# MetadataReaderProvider.FromPortablePdbStream: the
                // parse is deferred, so a file that does not parse still
                // returns true here (the provider's first read fails and
                // its error state reports it).
                provider = Md::PortablePdb(bytes);
                return true;
            }
        }
    }
    return false;
}

}  // namespace

std::unique_ptr<DI::IDebugInfoProvider> LoadSymbols(
    const Md::MetadataFile& module) {
    try {
        Md::PortablePdb provider(nullptr);
        std::string pdbFileName;
        if (TryOpenPortablePdb(module, provider, pdbFileName)) {
            // The C# null pdbFileName (an embedded PDB) ports as the empty
            // out string -- the disengaged optional here.
            std::optional<std::string> pdbPath;
            if (!pdbFileName.empty()) {
                pdbPath = pdbFileName;
            }
            return std::make_unique<PortableDebugInfoProvider>(
                module.FileName(), std::move(provider), std::move(pdbPath));
        }
        // The C# falls back to the MonoCecilDebugInfoProvider over an
        // adjacent <module>.pdb (the legacy Windows-PDB reader). DEFERRED:
        // the Cecil bridge is scoped out (PORT_PLAN.md 5.7) -- no provider.
        return nullptr;
    } catch (const std::out_of_range&) {
        // The C# catch (BadImageFormatException || COMException): ignore
        // PDB load errors.
        return nullptr;
    }
}

std::unique_ptr<DI::IDebugInfoProvider> FromFile(const Md::MetadataFile& module,
    const std::string& pdbFileName) {
    if (pdbFileName.empty()) {
        return nullptr;
    }
    std::shared_ptr<const std::vector<std::uint8_t>> bytes
        = OpenStream(pdbFileName);
    if (bytes == nullptr) {
        return nullptr;
    }
    if (HasLegacyPdbPrefix(*bytes)) {
        // The C# constructs the MonoCecilDebugInfoProvider (the legacy
        // Windows-PDB reader). DEFERRED: the Cecil bridge is scoped out
        // (PORT_PLAN.md 5.7) -- no provider.
        return nullptr;
    }
    // The C# MetadataReaderProvider.FromPortablePdbStream: the deferred
    // parse, so a file that does not parse still yields the provider (its
    // error state reports the failure).
    return std::make_unique<PortableDebugInfoProvider>(module.FileName(),
        Md::PortablePdb(bytes), pdbFileName);
}

}  // namespace ILSpy::ILSpyX::PdbProvider
