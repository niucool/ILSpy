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

// The implementation of UniversalAssemblyResolver.hpp (the first sub-slice of
// ICSharpCode.Decompiler/Metadata/UniversalAssemblyResolver.cs: the enums and
// the ParseTargetFramework classifier).

#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"

#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace ILSpy::Decompiler::Metadata {

namespace {

// The .NET `string.Trim()` (both ends) over the char.IsWhiteSpace set. The
// C# calls Trim() on the comma-separated tokens; a token consisting only of
// whitespace becomes the empty string.
std::string TrimString(std::string_view text) {
    std::u16string wide = Util::Utf8ToUtf16(text);
    std::size_t begin = 0;
    std::size_t end = wide.size();
    while (begin < end && Util::IsWhiteSpace(wide[begin])) begin++;
    while (end > begin && Util::IsWhiteSpace(wide[end - 1])) end--;
    if (begin == 0 && end == wide.size()) return std::string(text);
    return Util::Utf16ToUtf8(std::u16string_view(wide).substr(begin, end - begin));
}

// The .NET `string.ToUpperInvariant()` restricted to the units that can
// reach an ASCII letter: the ASCII block plus U+017F (long s), the only BMP
// unit whose invariant uppercase is an ASCII letter (probed unit-by-unit
// over the BMP -- the iteration-31 sanitizer finding). Every other unit
// uppercases to a non-ASCII unit and can never equal the pure-ASCII
// ".NETCOREAPP"-style keys, so leaving it untouched is exact for the
// comparison.
std::string ToUpperInvariantAscii(std::string_view text) {
    std::u16string wide = Util::Utf8ToUtf16(text);
    for (char16_t& c : wide) {
        if (c >= u'a' && c <= u'z') {
            c = static_cast<char16_t>(c - u'a' + u'A');
        } else if (c == u'\u017F') {
            c = u'S';
        }
    }
    return Util::Utf16ToUtf8(wide);
}

// The .NET `Version.TryParse(string, out Version)`: the string ctor's parse
// (the same NumberStyles.Integer per-component shape) with the ctor's
// exception family caught into false.
bool TryParseVersion(const std::string& text, TypeSystem::Version& result) {
    try {
        result = TypeSystem::Version(text);
        return true;
    } catch (const std::invalid_argument&) {
        return false;
    } catch (const std::out_of_range&) {
        return false;
    }
}

// The .NET `string.Split(',')` / `Split('=')`: every occurrence of the
// separator splits, and EMPTY entries are kept ("a,,b" -> ["a", "", "b"]).
std::vector<std::string> SplitOn(std::string_view text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t pos = text.find(separator, start);
        if (pos == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            return parts;
        }
        parts.emplace_back(text.substr(start, pos - start));
        start = pos + 1;
    }
}

}  // namespace

ParsedTargetFramework ParseTargetFramework(const std::string& targetFramework) {
    if (targetFramework.empty())
        return {TargetFrameworkIdentifier::NETFramework, ZeroVersion()};

    std::vector<std::string> tokens = SplitOn(targetFramework, ',');
    TargetFrameworkIdentifier identifier;

    std::string head = ToUpperInvariantAscii(TrimString(tokens[0]));
    if (head == ".NETCOREAPP") {
        identifier = TargetFrameworkIdentifier::NETCoreApp;
    } else if (head == ".NETSTANDARD") {
        identifier = TargetFrameworkIdentifier::NETStandard;
    } else if (head == "SILVERLIGHT") {
        identifier = TargetFrameworkIdentifier::Silverlight;
    } else {
        identifier = TargetFrameworkIdentifier::NETFramework;
    }

    // The C# nullable local: null until a "Version" pair parses.
    std::optional<TypeSystem::Version> version;

    for (std::size_t i = 1; i < tokens.size(); i++) {
        std::vector<std::string> pair = SplitOn(TrimString(tokens[i]), '=');
        if (pair.size() != 2) continue;

        if (ToUpperInvariantAscii(TrimString(pair[0])) == "VERSION") {
            // The C# `pair[1].TrimStart('v', 'V', ' ', '\t')`: a char-set
            // prefix strip (not a whitespace trim).
            std::string value = TrimString(pair[1]);
            std::size_t start = 0;
            while (start < value.size()) {
                char c = value[start];
                if (c == 'v' || c == 'V' || c == ' ' || c == '\t') {
                    start++;
                } else {
                    break;
                }
            }
            std::string versionString = value.substr(start);

            TypeSystem::Version parsed;
            if (TryParseVersion(versionString, parsed)) {
                // The C# re-wrap: (Major, Minor, Build < 0 ? 0 : Build) --
                // the REVISION is dropped.
                version = TypeSystem::Version(parsed.Major, parsed.Minor,
                    parsed.Build < 0 ? 0 : parsed.Build);
            } else {
                version = std::nullopt;
            }
            // ".NET 5 or greater still use .NETCOREAPP as
            // TargetFrameworkAttribute value..."
            if (version && version->Major >= 5
                && identifier == TargetFrameworkIdentifier::NETCoreApp) {
                identifier = TargetFrameworkIdentifier::NET;
            }
        }
    }

    return {identifier, version.value_or(ZeroVersion())};
}

const TypeSystem::Version& ZeroVersion() {
    // The C# `internal static Version ZeroVersion = new Version(0, 0, 0, 0)`
    // -- a process-lifetime singleton.
    static const TypeSystem::Version zero(0, 0, 0, 0);
    return zero;
}

// ---------------------------------------------------------------------------
// The class-body slice: the `#region .NET / mono GAC handling` machinery.

namespace {

// ---------------------------------------------------------------------------
// The OS-boundary and System.IO.Path helpers -- copied next to this consumer
// from DotNetCorePathFinder.cpp (the established per-consumer convention:
// the .NET System.IO composition this resolver performs needs the exact
// `Path.Combine`/`GetDirectoryName` semantics over Unicode paths).

#if defined(_WIN32)

std::wstring Utf8ToWide(std::string_view utf8) {
    std::u16string wide = Util::Utf8ToUtf16(utf8);
    return std::wstring(wide.begin(), wide.end());
}

std::string WideToUtf8(const std::wstring& wide) {
    std::u16string narrow(wide.begin(), wide.end());
    return Util::Utf16ToUtf8(narrow);
}

fs::path ToFsPath(std::string_view utf8) {
    return fs::path(Utf8ToWide(utf8));
}

std::string FromFsPath(const fs::path& path) {
    return WideToUtf8(path.native());
}

#else

fs::path ToFsPath(std::string_view utf8) {
    return fs::path(std::string(utf8));
}

std::string FromFsPath(const fs::path& path) {
    return path.native();
}

#endif

// The decompiled System.IO.PathInternal separators and roots (the
// `GetDirectoryName` machinery).
bool WinIsDirectorySeparator(char c) {
    return c == '\\' || c == '/';
}

bool WinIsValidDriveChar(char value) {
    return static_cast<unsigned>((value | 0x20) - 'a') <= 25u;
}

bool WinIsDevice(std::string_view path) {
    return path.size() >= 4 && path[0] == '\\'
        && (path[1] == '\\' || path[1] == '?') && path[2] == '?'
        && path[3] == '\\';
}

bool WinIsDeviceUnc(std::string_view path) {
    return path.size() >= 8 && WinIsDevice(path)
        && WinIsDirectorySeparator(path[7]) && path[4] == 'U'
        && path[5] == 'N' && path[6] == 'C';
}

std::size_t WinGetRootLength(std::string_view path) {
    std::size_t length = path.size();
    std::size_t i = 0;
    if (length >= 1 && (path[0] == '\\' || path[0] == '/')
        && length >= 2 && path[1] == path[0]) {
        // UNC or device: \\server\\share or \\?\...
        if (WinIsDevice(path)) {
            i = 4;
            if (length > 6 && path[4] == '.' && path[5] == '.'
                && WinIsDirectorySeparator(path[6])) {
                return 6;
            }
            if (WinIsDeviceUnc(path)) {
                i = 8;
            } else {
                return length;
            }
        } else {
            i = 2;
        }
        // Scan past the server and the share
        int segments = 2;
        while (i < length && segments > 0) {
            if (WinIsDirectorySeparator(path[i])) {
                segments--;
                while (i < length && WinIsDirectorySeparator(path[i])) i++;
            } else {
                i++;
            }
        }
        return i;
    }
    if (length >= 2 && WinIsValidDriveChar(path[0]) && path[1] == ':') {
        i = 2;
        if (length > 2 && WinIsDirectorySeparator(path[2])) i++;
    }
    return i;
}

// The .NET `Path.IsPathRooted(string)` (the `JoinPaths` rooted-second rule).
bool IsPathRooted(std::string_view path) {
    if (path.empty()) return false;
    if (WinIsDirectorySeparator(path[0])) return true;
    return path.size() >= 2 && WinIsValidDriveChar(path[0]) && path[1] == ':';
}

// The .NET `Path.Combine` pairwise join (the null arms are unreachable at
// every resolver call site -- the C# passes non-null roots and the composed
// name segments; the empty-second/empty-first/rooted-second/separator-tail
// rules are the ones the GAC compositions exercise).
std::string JoinPaths(const std::string& first, std::string_view second) {
    if (second.empty()) return first;
    if (first.empty()) return std::string(second);
    if (IsPathRooted(second)) return std::string(second);
    char last = first[first.size() - 1];
    if (last == '\\' || last == '/') return first + std::string(second);
    return first + "\\" + std::string(second);
}

// The .NET `Path.GetDirectoryName(string)` (gold-pinned): null for the empty
// string and for root forms; everything before the LAST separator, with the
// returned text NORMALIZED to the platform separator.
std::optional<std::string> GetDirectoryName(const std::string& path) {
    if (path.empty()) return std::nullopt;
    std::size_t root = WinGetRootLength(path);
    if (path.size() <= root) return std::nullopt;
    std::string result;
    std::size_t lastSep = path.find_last_of("\\/");
    if (lastSep == std::string::npos) {
        result = path.substr(0, root);
    } else {
        result = path.substr(0, lastSep);
    }
    for (char& c : result) {
        if (c == '/') c = '\\';
    }
    return result;
}

// The .NET `File.Exists` (a directory is NOT a file; errors are false).
bool FileExists(const std::string& path) {
    std::error_code ec;
    return fs::is_regular_file(ToFsPath(path), ec);
}

// The .NET `Directory.Exists`.
bool DirectoryExists(const std::string& path) {
    std::error_code ec;
    return fs::is_directory(ToFsPath(path), ec);
}

// The .NET `Environment.GetEnvironmentVariable(name)` (the UTF-8 form).
std::optional<std::string> GetEnvironmentVariableUtf8(const wchar_t* wideName,
    const char* narrowName) {
#if defined(_WIN32)
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, wideName) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result = WideToUtf8(value);
    free(value);
    return result;
#else
    (void)wideName;
    const char* value = std::getenv(narrowName);
    if (value == nullptr) return std::nullopt;
    return std::string(value);
#endif
}

// The .NET `Environment.GetFolderPath(Environment.SpecialFolder.Windows)`:
// the GetWindowsDirectory value (the gold dumps' `C:\WINDOWS`) -- the same
// value the windir variable carries in every practical Windows environment
// (the SystemRoot the variable and the API both resolve through). The
// non-Windows arm models the C# null shape as nullopt (the .NET
// non-Windows empty-string form, which would compose the RELATIVE
// `assembly`/`Microsoft.NET\\assembly` roots, stays unreproduced: the GAC
// machinery is Windows-only).
std::optional<std::string> GetWindowsFolderPath() {
#if defined(_WIN32)
    return GetEnvironmentVariableUtf8(L"WINDIR", "WINDIR");
#else
    return std::nullopt;
#endif
}

// The .NET `DirectoryInfo.EnumerateFiles("*.dll",
// SearchOption.AllDirectories)` file-name gate: .NET 10's MatchType is
// Simple (the case-insensitive extension equality -- 'y.dlly'/'y.dllx' do
// NOT match).
bool FileNameMatchesDllPattern(const std::string& name) {
    std::size_t dot = name.find_last_of('.');
    std::string extension = dot == std::string::npos ? "" : name.substr(dot + 1);
    if (extension.size() != 3) return false;
    return (extension[0] | 0x20) == 'd' && (extension[1] | 0x20) == 'l'
        && (extension[2] | 0x20) == 'l';
}

// The recursive `*.dll` walk (the `EnumerateFiles` gate above, in
// directory-entry order; the yielded order is not observable -- `EnumerateGac`
// sorts at the test boundary and the C# consumers materialize).
std::vector<std::string> EnumerateDllFilesRecursive(const std::string& rootPath) {
    std::vector<std::string> files;
    std::error_code ec;
    fs::recursive_directory_iterator it(ToFsPath(rootPath),
        fs::directory_options::skip_permission_denied, ec);
    if (ec) return files;
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        std::error_code statEc;
        if (!it->is_regular_file(statEc) || statEc) continue;
        if (FileNameMatchesDllPattern(FromFsPath(it->path().filename()))) {
            files.push_back(FromFsPath(it->path()));
        }
    }
    return files;
}

// The `string.Split(new[] { "\\" }, StringSplitOptions.RemoveEmptyEntries)`
// of `EnumerateGac`: every backslash splits, EMPTY entries dropped.
std::vector<std::string> SplitBackslashRemoveEmpty(std::string_view text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        std::size_t pos = text.find('\\', start);
        if (pos == std::string_view::npos) {
            if (start < text.size()) {
                parts.emplace_back(text.substr(start));
            }
            return parts;
        }
        if (pos > start) {
            parts.emplace_back(text.substr(start, pos - start));
        }
        start = pos + 1;
    }
}

// The `Regex.Match` groups of `EnumerateGac`'s version-folder-name pattern
// (the public `GacFolderNameMatch` shape).

// The length of the non-underscore run from `start`.
std::size_t NonUnderscoreRun(std::string_view text, std::size_t start) {
    std::size_t end = start;
    while (end < text.size() && text[end] != '_') end++;
    return end - start;
}

// One `Regex.Match` attempt at `start` over the pattern
// `(?<version>[^_]+)_(?<culture>[^_]+)?_(?<publicKey>[^_]+)` (the prefix
// already consumed by the caller when present). Every group's length is
// FORCED by the next literal '_' -- a shorter take always leaves a
// non-underscore where the literal must match, so the greedy takes are the
// only viable ones and the backtracking reduces to the two culture paths
// (the gold X matrix pins every shape).
bool TryMatchGacFolderNameAt(
    std::string_view text, std::size_t start, GacFolderNameMatch& match) {
    std::size_t versionLength = NonUnderscoreRun(text, start);
    if (versionLength == 0) return false;
    std::size_t vEnd = start + versionLength;
    if (vEnd >= text.size()) return false;  // the '_' must follow
    // The greedy culture path: [^_]+ then '_'
    if (vEnd + 1 < text.size() && text[vEnd + 1] != '_') {
        std::size_t cultureLength = NonUnderscoreRun(text, vEnd + 1);
        std::size_t cEnd = vEnd + 1 + cultureLength;
        if (cEnd < text.size()) {  // text[cEnd] == '_'
            std::size_t keyLength = NonUnderscoreRun(text, cEnd + 1);
            if (keyLength > 0) {
                match.Version = std::string(text.substr(start, versionLength));
                match.Culture = std::string(text.substr(vEnd + 1, cultureLength));
                match.PublicKey = std::string(text.substr(cEnd + 1, keyLength));
                return true;
            }
        }
    }
    // The ABSENT culture path: the second literal '_' right after
    // version's '_' (the `4.0.0.0__token` neutral-folder shape).
    if (vEnd + 1 < text.size() && text[vEnd + 1] == '_') {
        std::size_t keyLength = NonUnderscoreRun(text, vEnd + 2);
        if (keyLength > 0) {
            match.Version = std::string(text.substr(start, versionLength));
            match.Culture.clear();
            match.PublicKey = std::string(text.substr(vEnd + 2, keyLength));
            return true;
        }
    }
    return false;
}

}  // namespace

// The full `Regex.Match` over `(v4.0_)?(?<version>[^_]+)_(?<culture>[^_]+)?_
// (?<publicKey>[^_]+)`: the unanchored first-position scan, the GREEDY
// optional prefix tried first and the prefix-less body retried at the same
// start when it fails (the `v4.0_a_b` shape: the prefixed body cannot match,
// so the prefix becomes part of the VERSION group -- gold-pinned).
bool TryMatchGacFolderName(std::string_view text, GacFolderNameMatch& match) {
    constexpr std::string_view kPrefix = "v4.0_";
    for (std::size_t start = 0; start < text.size(); start++) {
        if (text.size() - start >= kPrefix.size()
            && text.substr(start, kPrefix.size()) == kPrefix) {
            if (TryMatchGacFolderNameAt(text, start + kPrefix.size(), match)) {
                return true;
            }
        }
        if (TryMatchGacFolderNameAt(text, start, match)) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> UniversalAssemblyResolver::GetGacPaths() {
    // The C# Mono arm (`decompilerRuntime == Mono -> GetDefaultMonoGacPaths()`)
    // is unreachable under the pinned NETCoreApp host (the header note).
    std::vector<std::string> paths;
    std::optional<std::string> windir = GetWindowsFolderPath();
    if (!windir) return paths;  // the C# null arm (dead on Windows)
    paths.push_back(JoinPaths(*windir, "assembly"));
    paths.push_back(JoinPaths(JoinPaths(*windir, "Microsoft.NET"), "assembly"));
    return paths;
}

const std::vector<std::string>& UniversalAssemblyResolver::GacPaths() {
    // The C# `static readonly List<string> gac_paths = GetGacPaths()` -- a
    // process-lifetime singleton (the field initializer's class-load timing
    // is not observable).
    static const std::vector<std::string> paths = GetGacPaths();
    return paths;
}

std::optional<std::string> UniversalAssemblyResolver::GetAssemblyInGac(
    const IAssemblyReference& reference) {
    const std::optional<std::vector<std::uint8_t>>& token = reference.PublicKeyToken();
    if (!token || token->empty()) return std::nullopt;
    // The C# Mono arm (`GetAssemblyInMonoGac`) is unreachable under the
    // pinned NETCoreApp host (the header note).
    return GetAssemblyInNetGac(reference);
}

std::optional<std::string> UniversalAssemblyResolver::GetAssemblyInNetGac(
    const IAssemblyReference& reference) {
    static const char* kGacs[] = { "GAC_MSIL", "GAC_32", "GAC_64", "GAC" };
    static const char* kPrefixes[] = { "", "v4.0_" };
    const std::vector<std::string>& paths = GacPaths();
    for (std::size_t i = 0; i < paths.size(); i++) {
        // The C# indexes `prefixes[i]` by the gac-root index: the legacy root
        // (C:\WINDOWS\assembly) composes the plain `4.0.0.0__<hex>` folder
        // names, the v4 root (C:\WINDOWS\Microsoft.NET\assembly) the
        // `v4.0_`-prefixed ones. The pinned-host `GetGacPaths` returns
        // exactly the two roots the prefixes table covers.
        const char* prefix = kPrefixes[i];
        for (const char* gac : kGacs) {
            std::string gacPath = JoinPaths(paths[i], gac);
            std::string file = GetAssemblyFile(reference, prefix, gacPath);
            if (FileExists(file)) return file;
        }
    }
    return std::nullopt;
}

std::string UniversalAssemblyResolver::GetAssemblyFile(const IAssemblyReference& reference,
    const std::string& prefix, const std::string& gac) {
    // The C# `new StringBuilder().Append(prefix).Append(reference.Version)`:
    // a NULL reference version renders EMPTY (the `Append(object)` null arm
    // -- gold-pinned: `foo` with no version composes `v4.0___<hex>`);
    // otherwise the unspecified-component `ToString()` render.
    std::optional<TypeSystem::Version> version = reference.Version();
    std::string folder = prefix + (version ? version->ToString() : std::string());
    folder += "__";
    // The C# `reference.PublicKeyToken[i].ToString("x2")` -- the lowercase
    // two-digit hex per byte. The callers guarantee a non-null token (the
    // `GetAssemblyInGac` guard); the public-key loop is a no-op for a
    // somehow-null token.
    constexpr char kHexDigits[] = "0123456789abcdef";
    if (const std::optional<std::vector<std::uint8_t>>& token
        = reference.PublicKeyToken()) {
        for (std::uint8_t byte : *token) {
            folder += kHexDigits[byte >> 4];
            folder += kHexDigits[byte & 0xf];
        }
    }
    // The .NET `Path.Combine(gac, name, folder, name + ".dll")` -- the
    // four-part pairwise join (the null arms unreachable: non-null roots
    // and composed segments).
    std::string name = reference.Name();
    return JoinPaths(JoinPaths(JoinPaths(gac, name), folder), name + ".dll");
}

bool UniversalAssemblyResolver::IsZeroOrAllOnes(
    const std::optional<TypeSystem::Version>& version) {
    if (!version) return true;
    // The C# component tests over the raw components: an UNSPECIFIED
    // component is -1, so `new Version(0, 0, 0)` is NOT all-zeros (its
    // Revision) and `new Version(0, 0)` is not either (gold-pinned).
    return (version->Major == 0 && version->Minor == 0 && version->Build == 0
               && version->Revision == 0)
        || (version->Major == 65535 && version->Minor == 65535
               && version->Build == 65535 && version->Revision == 65535);
}

bool UniversalAssemblyResolver::IsSpecialVersionOrRetargetable(
    const IAssemblyReference& reference) {
    return IsZeroOrAllOnes(reference.Version()) || reference.IsRetargetable();
}

std::vector<AssemblyNameReference> UniversalAssemblyResolver::EnumerateGac() {
    std::vector<AssemblyNameReference> entries;
    static const char* kGacs[] = { "GAC_MSIL", "GAC_32", "GAC_64", "GAC" };
    for (const std::string& path : GetGacPaths()) {
        for (const char* gac : kGacs) {
            std::string rootPath = JoinPaths(path, gac);
            if (!DirectoryExists(rootPath)) continue;
            for (const std::string& item : EnumerateDllFilesRecursive(rootPath)) {
                // The C# `Path.GetDirectoryName(item.FullName)` then the
                // `assemblyParentPath?.Length > rootPath.Length` gate.
                std::optional<std::string> assemblyParentPath = GetDirectoryName(item);
                if (!assemblyParentPath
                    || !(assemblyParentPath->size() > rootPath.size())) {
                    continue;
                }
                std::string relative = assemblyParentPath->substr(rootPath.size() + 1);
                std::vector<std::string> name = SplitBackslashRemoveEmpty(relative);
                // The C# `name?.Length != 2` skip.
                if (name.size() != 2) continue;
                GacFolderNameMatch match;
                if (!TryMatchGacFolderName(name[1], match)) continue;
                // The C# `IsNullOrEmpty(culture) ? "neutral"` map.
                std::string culture = match.Culture.empty() ? "neutral" : match.Culture;
                entries.push_back(AssemblyNameReference::Parse(
                    name[0] + ", Version=" + match.Version + ", Culture=" + culture
                    + ", PublicKeyToken=" + match.PublicKey));
            }
        }
    }
    return entries;
}

}  // namespace ILSpy::Decompiler::Metadata
