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

// The implementation of DotNetCorePathFinderExtensions.hpp (the C#
// DotNetCorePathFinderExtensions.cs).

#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The HasCustomAttribute parent of the assembly-definition row (the
// WriteAssemblyHeader convention: 0x20000000 | 1).
constexpr std::uint32_t kAssemblyDefinitionParent = 0x20000001;

constexpr char kTargetFrameworkAttributeName[] =
    "System.Runtime.Versioning.TargetFrameworkAttribute";

// ---------------------------------------------------------------------------
// The attribute-value blob read (the C#
// `metadata.GetBlobReader(attribute.Value)` + ReadUInt16 +
// ReadSerializedString). The SRM cursor semantics are the
// CustomAttributeDecoder.cpp reader's (copied next to its consumer here):
// the zero-advance invalid-compressed-integer form is what makes the
// SerString 0xFF null form work.

struct BlobCursor {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;
};

// The C# `BlobReader.ReadUInt16()` (throws at end-of-blob).
std::uint16_t ReadUInt16Blob(BlobCursor& r) {
    if (r.size - r.pos < 2)
        throw std::invalid_argument("Read out of bounds.");
    std::uint16_t v = static_cast<std::uint16_t>(r.data[r.pos])
        | (static_cast<std::uint16_t>(r.data[r.pos + 1]) << 8);
    r.pos += 2;
    return v;
}

// The C# `BlobReader.ReadCompressedIntegerOrInvalid()` (the zero-advance
// form; int.MaxValue = INVALID).
int ReadCompressedIntegerOrInvalid(BlobCursor& r) {
    if (r.pos >= r.size)
        return 0x7FFFFFFF;
    std::uint8_t b = r.data[r.pos];
    long long remaining = static_cast<long long>(r.size - r.pos);
    if ((b & 0x80) == 0) {
        r.pos += 1;
        return b;
    }
    if ((b & 0x40) == 0) {
        if (remaining >= 2) {
            r.pos += 2;
            return (static_cast<int>(b & 0x3F) << 8) | r.data[r.pos - 1];
        }
    } else if ((b & 0x20) == 0 && remaining >= 4) {
        r.pos += 4;
        return (static_cast<int>(b & 0x1F) << 24)
            | (static_cast<int>(r.data[r.pos - 3]) << 16)
            | (static_cast<int>(r.data[r.pos - 2]) << 8)
            | static_cast<int>(r.data[r.pos - 1]);
    }
    return 0x7FFFFFFF;
}

// The C# `BlobReader.ReadByte()`.
std::uint8_t ReadByteBlob(BlobCursor& r) {
    if (r.pos >= r.size)
        throw std::invalid_argument("Read out of bounds.");
    return r.data[r.pos++];
}

// The C# `BlobReader.ReadSerializedString()`: the SerString -- a compressed
// length then that many UTF-8 bytes (the ReadUTF8 bounds check throws for a
// length the remaining blob cannot satisfy); the 0xFF single-byte null form
// (an invalid compressed integer the Try rejects without consuming) reads
// NULL.
std::optional<std::string> ReadSerializedStringBlob(BlobCursor& r) {
    int length = ReadCompressedIntegerOrInvalid(r);
    if (length != 0x7FFFFFFF) {
        if (length < 0
            || r.size - r.pos < static_cast<std::size_t>(length)) {
            // The C# ReadUTF8's bounds check.
            throw std::invalid_argument("Read out of bounds.");
        }
        std::string out(reinterpret_cast<const char*>(r.data + r.pos),
            static_cast<std::size_t>(length));
        r.pos += static_cast<std::size_t>(length);
        return out;
    }
    if (ReadByteBlob(r) != 0xFF)
        throw std::invalid_argument("Invalid serialized string.");
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// The path-pattern fallback. The C# drives two .NET Regexes over the
// assembly path (PathPattern with six alternatives, RefPathPattern with
// three) with RegexOptions.IgnoreCase; the port hand-rolls the same
// matching (a per-alternative segment walker driven by .NET Regex.Match's
// leftmost-then-first-alternative scan). ASCII case folding stands in for
// the .NET OrdinalIgnoreCase comparer -- the needles are pure ASCII and the
// only divergence is a path carrying a case-folding non-ASCII unit (the
// U+212A-class exotics; the ToUpperInvariant convention).

// Case-insensitive ASCII literal match at `i`.
bool LitAt(const std::string& p, std::size_t i, std::string_view lit) {
    if (i + lit.size() > p.size()) return false;
    auto fold = [](char c) {
        return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    };
    for (std::size_t k = 0; k < lit.size(); k++) {
        if (fold(p[i + k]) != fold(lit[k])) return false;
    }
    return true;
}

// The [/\\] char class.
bool IsSep(char c) { return c == '/' || c == '\\'; }

// The [^/\\]+ segment length starting at `i`.
std::size_t SegLen(const std::string& p, std::size_t i) {
    std::size_t k = i;
    while (k < p.size() && !IsSep(p[k])) k++;
    return k - i;
}

struct PathMatch {
    std::string type;      // the (?<type>...) capture (empty when the
                           // alternative has no type group -- none do)
    std::string version;   // the (?<version>...) capture (empty for the
                           // GAC alternative, which captures no version)
};

// Alt 1: Reference Assemblies[/\]Microsoft[/\]Framework[/\]
// (?<type>.NETFramework)[/\]v(?<version>[^/\]+)[/]
// The type group's leading '.' is the regex ANY char (unescaped in the C#),
// so "XNETFramework" also matches -- and then fails the case-SENSITIVE
// `type == ".NETFramework"` comparison in the render step.
bool MatchAlt1(const std::string& p, std::size_t i, PathMatch& m) {
    if (!LitAt(p, i, "Reference Assemblies")) return false;
    std::size_t j = i + 20;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    if (!LitAt(p, j, "Microsoft")) return false;
    j += 9;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    if (!LitAt(p, j, "Framework")) return false;
    j += 9;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    if (j + 13 > p.size() || !LitAt(p, j + 1, "NETFramework")) return false;
    m.type = p.substr(j, 13);
    j += 13;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    // The literal 'v' (case-insensitive under IgnoreCase).
    if (j >= p.size()) return false;
    char v = p[j] | 0x20;
    if (v != 'v') return false;
    j++;
    std::size_t vlen = SegLen(p, j);
    if (vlen == 0) return false;
    m.version = p.substr(j, vlen);
    j += vlen;
    return j < p.size() && IsSep(p[j]);
}

// Alt 2: (?<type>Microsoft\.NET)[/\]assembly[/\]GAC_(MSIL|32|64)[/]
// (no version group).
bool MatchAlt2(const std::string& p, std::size_t i, PathMatch& m) {
    if (!LitAt(p, i, "Microsoft.NET")) return false;
    std::size_t j = i + 13;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    if (!LitAt(p, j, "assembly")) return false;
    j += 8;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    if (!LitAt(p, j, "GAC_")) return false;
    j += 4;
    if (LitAt(p, j, "MSIL")) j += 4;
    else if (LitAt(p, j, "32")) j += 2;
    else if (LitAt(p, j, "64")) j += 2;
    else return false;
    m.type = p.substr(i, 13);
    m.version.clear();
    return j < p.size() && IsSep(p[j]);
}

// Alt 3: (?<type>Microsoft\.NET)[/\]Framework(64)?[/\](?<version>[^/\]+)[/]
bool MatchAlt3(const std::string& p, std::size_t i, PathMatch& m) {
    if (!LitAt(p, i, "Microsoft.NET")) return false;
    std::size_t j = i + 13;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    if (!LitAt(p, j, "Framework")) return false;
    j += 9;
    if (LitAt(p, j, "64")) j += 2;  // the optional suffix
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    std::size_t vlen = SegLen(p, j);
    if (vlen == 0) return false;
    m.version = p.substr(j, vlen);
    j += vlen;
    m.type = p.substr(i, 13);
    return j < p.size() && IsSep(p[j]);
}

// Alt 4: NuGetFallbackFolder[/\](?<type>[^/\]+)\(?<version>[^/\]+)
// ([/\].*)?[/\]ref[/\] -- the type/version separator is a LITERAL backslash
// (not the [/\\] class), and a "ref" directory must follow: a separator
// right after the version, and a sep+"ref"+sep occurrence at or after it.
bool MatchAlt4(const std::string& p, std::size_t i, PathMatch& m) {
    if (!LitAt(p, i, "NuGetFallbackFolder")) return false;
    std::size_t j = i + 19;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    std::size_t tlen = SegLen(p, j);
    if (tlen == 0) return false;
    m.type = p.substr(j, tlen);
    j += tlen;
    if (j >= p.size() || p[j] != '\\') return false;
    j++;
    std::size_t vlen = SegLen(p, j);
    if (vlen == 0) return false;
    m.version = p.substr(j, vlen);
    j += vlen;
    if (j >= p.size() || !IsSep(p[j])) return false;
    for (std::size_t k = j; k + 4 < p.size(); k++) {
        if (IsSep(p[k]) && LitAt(p, k + 1, "ref") && IsSep(p[k + 4]))
            return true;
    }
    return false;
}

// Alt 5: shared[/\](?<type>[^/\]+)\(?<version>[^/\]+)([/\].*)?[/\] -- the
// literal-backslash separators again; the trailing ([/\].*)?[/\] reduces to
// "a separator follows the version" (the optional group's own separator
// already satisfies the final one).
bool MatchAlt5(const std::string& p, std::size_t i, PathMatch& m) {
    if (!LitAt(p, i, "shared")) return false;
    std::size_t j = i + 6;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    std::size_t tlen = SegLen(p, j);
    if (tlen == 0) return false;
    m.type = p.substr(j, tlen);
    j += tlen;
    if (j >= p.size() || p[j] != '\\') return false;
    j++;
    std::size_t vlen = SegLen(p, j);
    if (vlen == 0) return false;
    m.version = p.substr(j, vlen);
    j += vlen;
    return j < p.size() && IsSep(p[j]);
}

// Alt 6: packs[/\](?<type>[^/\]+)\(?<version>[^/\]+)\ref([/\].*)?[/\]
bool MatchAlt6(const std::string& p, std::size_t i, PathMatch& m) {
    if (!LitAt(p, i, "packs")) return false;
    std::size_t j = i + 5;
    if (j >= p.size() || !IsSep(p[j])) return false;
    j++;
    std::size_t tlen = SegLen(p, j);
    if (tlen == 0) return false;
    m.type = p.substr(j, tlen);
    j += tlen;
    if (j >= p.size() || p[j] != '\\') return false;
    j++;
    std::size_t vlen = SegLen(p, j);
    if (vlen == 0) return false;
    m.version = p.substr(j, vlen);
    j += vlen;
    if (j >= p.size() || p[j] != '\\') return false;
    j++;
    if (!LitAt(p, j, "ref")) return false;
    j += 3;
    return j < p.size() && IsSep(p[j]);
}

// The C# `Regex.Match(path, pattern, IgnoreCase | ExplicitCapture)`:
// leftmost start position first, alternatives in declaration order within a
// position. The RefPathPattern drops the GAC / Framework / shared
// alternatives, keeping alt 1 / 4 / 6.
std::optional<PathMatch> MatchPathPattern(const std::string& p, bool refPattern) {
    using AltFn = bool (*)(const std::string&, std::size_t, PathMatch&);
    static constexpr AltFn kPathAlts[] = {
        MatchAlt1, MatchAlt2, MatchAlt3, MatchAlt4, MatchAlt5, MatchAlt6};
    static constexpr AltFn kRefAlts[] = {MatchAlt1, MatchAlt4, MatchAlt6};
    const AltFn* alts = refPattern ? kRefAlts : kPathAlts;
    const std::size_t nAlts = refPattern ? 3 : 6;
    for (std::size_t i = 0; i < p.size(); i++) {
        for (std::size_t a = 0; a < nAlts; a++) {
            PathMatch m;
            if (alts[a](p, i, m)) return m;
        }
    }
    return std::nullopt;
}

// The C# `type.IndexOf("netcore"/"netstandard",
// StringComparison.OrdinalIgnoreCase) >= 0` (ASCII folding).
bool ContainsIgnoreCase(const std::string& haystack, std::string_view needle) {
    if (needle.size() > haystack.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); i++) {
        if (LitAt(haystack, i, needle)) return true;
    }
    return false;
}

// The C# `version.TrimStart('v')` -- the LOWERCASE 'v' only (a capital V
// survives into the render).
std::string TrimStartLowerV(std::string s) {
    std::size_t k = 0;
    while (k < s.size() && s[k] == 'v') k++;
    return s.substr(k);
}

// The C# post-match render: the captured version (empty for the GAC
// alternative) falls back to the metadata version then "4.0", the 'v' is
// trimmed, and the type routes to the framework spelling. A matched type
// that is none of the three spellings falls out of the if-chain and the
// whole method returns "" -- faithfully reproduced here by the empty
// return (distinct from the no-match arm, which always renders
// .NETFramework).
std::optional<std::string> RenderPathMatch(const MetadataFile& metadata,
                                            const PathMatch& m) {
    std::string version = m.version;
    if (version.empty()) version = metadata.MetadataVersion();
    if (version.empty()) version = "4.0";
    version = TrimStartLowerV(std::move(version));
    if (m.type == "Microsoft.NET" || m.type == ".NETFramework") {
        return ".NETFramework,Version=v"
            + version.substr(0, std::min<std::size_t>(3, version.size()));
    }
    if (ContainsIgnoreCase(m.type, "netcore"))
        return ".NETCoreApp,Version=v" + version;
    if (ContainsIgnoreCase(m.type, "netstandard"))
        return ".NETStandard,Version=v" + version;
    return std::string();
}

// The C# no-match arm: the MetadataVersion (or "4.0") trimmed of a leading
// lowercase 'v', truncated to 3 characters.
std::string RenderPathNoMatch(const MetadataFile& metadata) {
    std::string version = metadata.MetadataVersion();
    if (version.empty()) version = "4.0";
    version = TrimStartLowerV(std::move(version));
    return ".NETFramework,Version=v"
        + version.substr(0, std::min<std::size_t>(3, version.size()));
}

// The C# version over the AssemblyDefinitionInfo / AssemblyReferenceInfo
// raw columns (all four are always specified).
TypeSystem::Version ToVersion(std::uint16_t major, std::uint16_t minor,
                              std::uint16_t build, std::uint16_t revision) {
    return TypeSystem::Version(major, minor, build, revision);
}

}  // namespace

std::optional<std::string> GetDotNetCoreVersion(
    const TypeSystem::Version& version) {
    // The C# tuple switch: (Major, Minor, Build).
    if (version.Major == 4 && version.Minor == 1 && version.Build == 0)
        return std::string("1.1");
    if (version.Major == 4 && version.Minor == 2 && version.Build == 0)
        return std::string("2.0");
    if (version.Major == 4 && version.Minor == 2 && version.Build == 1)
        return std::string("3.0");
    if (version.Major == 4 && version.Minor == 2 && version.Build == 2)
        return std::string("3.1");
    if (version.Major >= 5)
        return version.ToString(2);
    return std::nullopt;
}

std::optional<std::string> DetectTargetFrameworkId(const MetadataFile& assembly) {
    return DetectTargetFrameworkId(assembly, assembly.FileName());
}

std::optional<std::string> DetectTargetFrameworkId(
    const MetadataFile& metadata, const std::optional<std::string>& assemblyPath) {
    // The TargetFrameworkAttribute walk over the assembly-definition
    // custom-attribute rows. The C# BadImageFormatException catch skips a
    // malformed attribute and continues the walk; the port maps that
    // exception family to std::out_of_range / std::invalid_argument (the
    // winmd convention).
    for (std::uint32_t attributeToken :
         metadata.GetCustomAttributeTokens(kAssemblyDefinitionParent)) {
        try {
            std::optional<CustomAttributeRowInfo> row =
                metadata.GetCustomAttribute(attributeToken);
            if (!row)
                continue;
            // attribute.GetAttributeType(metadata).GetFullTypeName(
            //     metadata).ToString() != TargetFrameworkAttributeName
            std::uint32_t typeToken =
                GetDeclaringType(metadata, row->ConstructorToken);
            if (GetFullTypeName(metadata, typeToken).ReflectionName()
                != kTargetFrameworkAttributeName)
                continue;
            // A nil Value handle behaves like a truncated blob (the read
            // throws inside the C# try and the walk skips the attribute --
            // the dtfNilValue gold).
            BlobCursor reader{row->ValueBlob ? row->ValueBlob->data() : nullptr,
                              row->ValueBlob ? row->ValueBlob->size() : 0};
            // The prolog check: a blob whose first uint16 is not 0x0001
            // continues the walk to the next attribute.
            if (ReadUInt16Blob(reader) == 0x0001) {
                std::optional<std::string> name = ReadSerializedStringBlob(reader);
                if (name) {
                    // `?.Replace(" ", "")` -- every space removed.
                    std::string& s = *name;
                    s.erase(std::remove(s.begin(), s.end(), ' '), s.end());
                }
                return name;
            }
        } catch (const std::out_of_range&) {
            // The C# `catch (BadImageFormatException)`: ignore malformed
            // attributes.
        } catch (const std::invalid_argument&) {
        }
    }

    // The assembly-name arm (`metadata.IsAssembly`). The port's
    // GetAssemblyDefinition degrades a corrupt Assembly row to nullopt
    // where the C# IsAssembly is a table-presence check -- a corrupt row
    // takes the reference passes here instead of throwing (the
    // documented-divergence class, confined to corrupt metadata).
    if (std::optional<MetadataFile::AssemblyDefinitionInfo> assembly =
            metadata.GetAssemblyDefinition()) {
        TypeSystem::Version version = ToVersion(assembly->MajorVersion,
            assembly->MinorVersion, assembly->BuildNumber,
            assembly->RevisionNumber);
        if (assembly->Name == "mscorlib")
            return ".NETFramework,Version=v" + version.ToString(2);
        if (assembly->Name == "netstandard")
            return ".NETStandard,Version=v" + version.ToString(2);
        if (assembly->Name == "System.Runtime"
            || assembly->Name == "System.Private.CoreLib") {
            std::optional<std::string> core =
                GetDotNetCoreVersion(version);
            if (core)
                return ".NETCoreApp,Version=v" + *core;
            // A null mapping breaks out of the name switch and falls to the
            // reference passes.
        }
    }

    // The AssemblyReference pass (the C# switch is an ordinal case-SENSITIVE
    // compare; a nil PublicKeyOrToken skips the row).
    for (const MetadataFile::AssemblyReferenceInfo& r : metadata.GetAssemblyReferences()) {
        if (r.PublicKeyOrToken.empty())
            continue;
        TypeSystem::Version version = ToVersion(r.MajorVersion, r.MinorVersion,
            r.BuildNumber, r.RevisionNumber);
        if (r.Name == "mscorlib")
            return ".NETFramework,Version=v" + version.ToString(2);
        if (r.Name == "System.Runtime" || r.Name == "System.Private.CoreLib") {
            std::optional<std::string> core = GetDotNetCoreVersion(version);
            if (core)
                return ".NETCoreApp,Version=v" + *core;
        }
    }

    // The separate netstandard pass (.NET Core/Framework assemblies can
    // reference it).
    for (const MetadataFile::AssemblyReferenceInfo& r : metadata.GetAssemblyReferences()) {
        if (r.PublicKeyOrToken.empty() || r.Name != "netstandard")
            continue;
        TypeSystem::Version version = ToVersion(r.MajorVersion, r.MinorVersion,
            r.BuildNumber, r.RevisionNumber);
        return ".NETStandard,Version=v" + version.ToString(2);
    }

    // The path-pattern fallback (a null path skips it entirely).
    if (assemblyPath) {
        std::optional<PathMatch> match = MatchPathPattern(*assemblyPath, false);
        if (match)
            return RenderPathMatch(metadata, *match);
        return RenderPathNoMatch(metadata);
    }
    return std::string();
}

bool IsReferenceAssembly(const MetadataFile& assembly) {
    return IsReferenceAssembly(assembly, assembly.FileName());
}

bool IsReferenceAssembly(const MetadataFile& metadata,
                         const std::optional<std::string>& assemblyPath) {
    // The [ReferenceAssembly] marker over the assembly-definition rows (the
    // HasKnownAttribute composition).
    for (std::uint32_t attributeToken :
         metadata.GetCustomAttributeTokens(kAssemblyDefinitionParent)) {
        if (IsKnownAttribute(metadata, attributeToken,
                TypeSystem::KnownAttribute::ReferenceAssembly))
            return true;
    }
    // The C# `Regex.Match(assemblyPath, ...)` over a null input throws
    // ArgumentNullException.
    if (!assemblyPath)
        throw std::invalid_argument(
            "Value cannot be null. (Parameter 'input')");
    return MatchPathPattern(*assemblyPath, true).has_value();
}

std::string DetectRuntimePack(const MetadataFile& assembly) {
    // The SRM StringComparer.Equals(handle, value) default is
    // case-SENSITIVE (Equals(handle, value, ignoreCase: false)); the
    // nil-key skip precedes every name check.
    for (const MetadataFile::AssemblyReferenceInfo& r : assembly.GetAssemblyReferences()) {
        if (r.PublicKeyOrToken.empty())
            continue;
        if (r.Name == "WindowsBase")
            return "Microsoft.WindowsDesktop.App";
        if (r.Name == "PresentationFramework")
            return "Microsoft.WindowsDesktop.App";
        if (r.Name == "PresentationCore")
            return "Microsoft.WindowsDesktop.App";
    }
    return "Microsoft.NETCore.App";
}

}  // namespace ILSpy::Decompiler::Metadata
