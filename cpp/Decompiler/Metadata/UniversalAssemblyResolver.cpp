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

#include "Decompiler/Metadata/DotNetCorePathFinder.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MethodBodyReader.hpp"
#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <cstdlib>
#include <filesystem>
#include <memory>
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

bool WinIsExtended(std::string_view path) {
    return path.size() >= 4 && path[0] == '\\'
        && (path[1] == '\\' || path[1] == '?') && path[2] == '?'
        && path[3] == '\\';
}

// The decompiled System.IO.PathInternal.IsDevice: an extended path is
// always a device; otherwise the `\\.\` (local device) and `\\?\`
// (extended) prefixes over separators.
bool WinIsDevice(std::string_view path) {
    if (!WinIsExtended(path)) {
        return path.size() >= 4 && WinIsDirectorySeparator(path[0])
            && WinIsDirectorySeparator(path[1])
            && (path[2] == '.' || path[2] == '?')
            && WinIsDirectorySeparator(path[3]);
    }
    return true;
}

bool WinIsDeviceUnc(std::string_view path) {
    return path.size() >= 8 && WinIsDevice(path)
        && WinIsDirectorySeparator(path[7]) && path[4] == 'U'
        && path[5] == 'N' && path[6] == 'C';
}

std::size_t WinGetRootLength(std::string_view path) {
#if !defined(_WIN32)
    // .NET on a POSIX host (the Unix PathInternal.GetRootLength): the only
    // root form is a single leading '/', and every other path is relative
    // (no UNC or device roots, no DOS drives). The Windows build below is
    // the decompiled Windows engine the gold pins were captured against,
    // where a leading-separator path parses as a UNC root -- which leaves a
    // POSIX '/dir/file' with no directory name and made the
    // DotNetCorePathFinder ctor throw the Path.Combine ArgumentNullException
    // on this host, so the POSIX build keeps its own root model.
    return (!path.empty() && path[0] == '/') ? 1 : 0;
#else
    std::size_t length = path.size();
    std::size_t i = 0;
    bool isDevice = WinIsDevice(path);
    bool isDeviceUnc = isDevice && WinIsDeviceUnc(path);
    if ((!isDevice || isDeviceUnc) && length > 0
        && WinIsDirectorySeparator(path[0])) {
        // UNC or device: \\server\\share or \\?\...
        if (isDeviceUnc) {
            i = 8;
        } else {
            i = 2;
        }
        // The decompiled scan: TWO separator events END the root (a
        // repeated separator is two events -- the port's old
        // skip-consecutive walk diverged on doubled-separator inputs).
        int segments = 2;
        for (; i < length; i++) {
            if (WinIsDirectorySeparator(path[i]) && --segments <= 0) {
                break;
            }
        }
    } else if (isDevice) {
        // The device scan: past the device name, then one separator.
        for (i = 4; i < length && !WinIsDirectorySeparator(path[i]); i++) {
        }
        if (i < length && i > 4 && WinIsDirectorySeparator(path[i])) {
            i++;
        }
    } else if (length >= 2 && path[1] == ':'
        && WinIsValidDriveChar(path[0])) {
        i = 2;
        if (length > 2 && WinIsDirectorySeparator(path[2])) i++;
    } else if (length >= 1 && WinIsDirectorySeparator(path[0])) {
        // A single leading separator is itself the root.
        i = 1;
    }
    return i;
#endif
}

// The decompiled System.IO.PathInternal.NormalizeDirectorySeparators: the
// fast-path scan (every separator a backslash with a non-separator after
// it), then the rebuild -- one leading backslash and every separator run
// collapsed to its last member, forward slashes replaced.
std::string NormalizeDirectorySeparators(const std::string& path) {
#if !defined(_WIN32)
    // The Unix build does not rewrite separators: the directory separator
    // is already '/' and a '\' is an ordinary filename character (the
    // Windows rebuild below would turn every '/' into a '\', which does
    // not name a file on this host).
    return path;
#else
    if (path.empty()) return path;
    bool needsNormalization = false;
    for (std::size_t i = 0; i < path.size(); i++) {
        char c = path[i];
        if (WinIsDirectorySeparator(c)
            && (c != '\\'
                || (i > 0 && i + 1 < path.size()
                    && WinIsDirectorySeparator(path[i + 1])))) {
            needsNormalization = true;
            break;
        }
    }
    if (!needsNormalization) return path;
    std::string result;
    std::size_t num = 0;
    if (WinIsDirectorySeparator(path[num])) {
        num++;
        result += '\\';
    }
    for (std::size_t j = num; j < path.size(); j++) {
        char c = path[j];
        if (WinIsDirectorySeparator(c)) {
            if (j + 1 < path.size() && WinIsDirectorySeparator(path[j + 1])) {
                continue;
            }
            c = '\\';
        }
        result += c;
    }
    return result;
#endif
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
#if !defined(_WIN32)
    // The Unix Path.DirectorySeparatorChar.
    return first + "/" + std::string(second);
#else
    return first + "\\" + std::string(second);
#endif
}

// The .NET `Path.GetDirectoryName(string)` (gold-pinned): null for the empty
// string and for root forms; everything before the LAST separator, with the
// returned text NORMALIZED to the platform separator.
std::optional<std::string> GetDirectoryName(const std::string& path) {
    // The C# `path == null || PathInternal.IsEffectivelyEmpty(...)` -- an
    // all-spaces (or empty) path has no directory information.
    bool allSpaces = true;
    for (char c : path) {
        if (c != ' ') {
            allSpaces = false;
            break;
        }
    }
    if (allSpaces) return std::nullopt;
    // The decompiled GetDirectoryNameOffset: the LAST separator run before
    // the root end, then the trailing-separator trim.
    std::size_t rootLength = WinGetRootLength(path);
    if (path.size() <= rootLength) return std::nullopt;
    std::size_t num = path.size();
    while (num > rootLength && !WinIsDirectorySeparator(path[--num])) {
    }
    while (num > rootLength && WinIsDirectorySeparator(path[num - 1])) {
        num--;
    }
    // The C# `PathInternal.NormalizeDirectorySeparators(path.Substring(
    // 0, directoryNameOffset))` -- the repeated-separator collapse the
    // UNC drives exercise.
    return NormalizeDirectorySeparators(path.substr(0, num));
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

// ---------------------------------------------------------------------------
// The instance surface (the C# UniversalAssemblyResolver.cs lines 78-226,
// 293-411, and 496-575).

namespace {

// The .NET `string.IsNullOrWhiteSpace(string)`.
bool IsNullOrWhiteSpace(const std::string& text) {
    std::u16string wide = Util::Utf8ToUtf16(text);
    for (char16_t c : wide) {
        if (!Util::IsWhiteSpace(c)) return false;
    }
    return true;
}

// The .NET `Environment.GetFolderPath(Environment.SpecialFolder.ProgramFiles)`
// / `...ProgramFilesX86` -- the process env vars (the known-folder values
// they carry on the x64 host the resolver machinery targets; a 64-bit
// process sees exactly these strings, gold-pinned).
std::optional<std::string> GetFolderPathProgramFiles() {
    return GetEnvironmentVariableUtf8(L"PROGRAMFILES", "PROGRAMFILES");
}

std::optional<std::string> GetFolderPathProgramFilesX86() {
    return GetEnvironmentVariableUtf8(L"PROGRAMFILES(X86)", "PROGRAMFILES(X86)");
}

// The .NET `Environment.SystemDirectory` -- `<windir>\system32` for a
// 64-bit process (the value GetSystemDirectory returns; composed from the
// Windows folder, gold-pinned equal on this host).
std::optional<std::string> GetSystemDirectoryPath() {
    std::optional<std::string> windir = GetWindowsFolderPath();
    if (!windir) return std::nullopt;
    return JoinPaths(*windir, "system32");
}

// The .NET `Environment.CurrentDirectory` -- the process-wide current
// directory (fs::current_path resolves through the same
// GetCurrentDirectoryW).
std::optional<std::string> GetCurrentDirectoryUtf8() {
    std::error_code ec;
    fs::path p = fs::current_path(ec);
    if (ec) return std::nullopt;
    return FromFsPath(p);
}

// The .NET `Environment.Is64BitOperatingSystem` -- the ProgramFiles(x86)
// env var exists exactly on a 64-bit Windows installation (a 32-bit process
// of a 64-bit OS still sees it, matching the C# predicate's WOW64-aware
// semantics).
bool Is64BitOperatingSystem() {
    return GetFolderPathProgramFilesX86().has_value();
}

// The C# `Path.GetDirectoryName(typeof(object).Module.FullyQualifiedName)`
// -- the shared-framework directory of the runtime executing the decompiler.
// The port pins the machine shape the gold tool exhibits: the NEWEST
// installed Microsoft.NETCore.App version folder under the dotnet root
// (FindDotNetExeDirectory's PATH scan). The two diverge only when the host
// process runs an OLDER runtime than the newest installed -- the standing
// single-framework machine shape makes them identical, and a miss (no
// dotnet on PATH) skips the arm instead of the C#'s always-present module
// directory.
std::optional<std::string> GetHostRuntimeDirectory() {
    std::optional<std::string> dotnetDir = DotNetCorePathFinder::FindDotNetExeDirectory();
    // The C#'s arm is `Path.GetDirectoryName(typeof(object).Module.
    // FullyQualifiedName)` -- the directory of the runtime EXECUTING the
    // decompiler, always present because the C# tool runs as a managed
    // process. This port is a native binary with no executing runtime;
    // the PATH scan is the primary substitute, and the DOTNET_ROOT
    // variable plus the host's well-known Linux install directories
    // (/usr/share/dotnet, /usr/lib/dotnet -- the hostfxr installation
    // search order below PATH) extend it so a bare environment (no
    // dotnet on PATH) still resolves the shared frameworks.
    if (!dotnetDir) {
        // The hostfxr installation search order below PATH: DOTNET_ROOT,
        // then the well-known Linux install directories, then the per-user
        // install default ($HOME/.dotnet -- the dotnet-install script's
        // default location, the last hostfxr probe before giving up).
        // The per-user probe used to be deliberately excluded because
        // resolving the runtime changed the facade fixtures' using sets
        // where the C# qualified the calls and added no using -- that
        // divergence is fixed (the FindRequiredImports recording filters
        // the emitted usings to what the render actually references), so
        // the port now resolves by default like the C# oracle does (it
        // always reads its host runtime's directory).
        const char* root = std::getenv("DOTNET_ROOT");
        std::vector<std::string> candidates;
        if (root != nullptr && *root != '\0')
            candidates.push_back(root);
        candidates.push_back("/usr/share/dotnet");
        candidates.push_back("/usr/lib/dotnet");
        const char* home = std::getenv("HOME");
        if (home != nullptr && *home != '\0')
            candidates.push_back(JoinPaths(home, ".dotnet"));
        for (const std::string& candidate : candidates) {
            if (candidate.empty()) continue;
            if (FileExists(JoinPaths(candidate, "dotnet"))) {
                dotnetDir = candidate;
                break;
            }
        }
    }
    if (!dotnetDir) return std::nullopt;
    std::string basePath =
        JoinPaths(JoinPaths(*dotnetDir, "shared"), "Microsoft.NETCore.App");
    if (!DirectoryExists(basePath)) return std::nullopt;
    std::optional<TypeSystem::Version> best;
    std::string bestName;
    std::error_code ec;
    fs::directory_iterator it(ToFsPath(basePath), ec);
    fs::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        if (!it->is_directory(dirEc) || dirEc) continue;
        std::string name = FromFsPath(it->path().filename());
        std::optional<TypeSystem::Version> version =
            DotNetCorePathFinder::ConvertToVersion(name);
        if (!version) continue;
        if (!best || best->CompareTo(*version) < 0) {
            best = *version;
            bestName = name;
        }
    }
    if (!best) return std::nullopt;
    return JoinPaths(basePath, bestName);
}

// The C# `reference.PublicKeyToken.ToHexString(8)` (the MetadataExtensions
// IEnumerable<byte> extension): the lowercase two-digit-per-byte hex render.
std::string ToHexTokenLower(const std::vector<std::uint8_t>& bytes) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(bytes.size() * 2);
    for (std::uint8_t byte : bytes) {
        result += kHexDigits[byte >> 4];
        result += kHexDigits[byte & 0xf];
    }
    return result;
}

// The `DirectoryInfo.EnumerateDirectories()`/`GetDirectories()` entries of
// a directory (the fs::directory_iterator order -- the same FindFirstFile
// order .NET's own enumeration uses on NTFS).
std::vector<std::string> GetSubdirectoryNames(const std::string& path) {
    std::vector<std::string> names;
    std::error_code ec;
    fs::directory_iterator it(ToFsPath(path), ec);
    fs::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        if (!it->is_directory(dirEc) || dirEc) continue;
        names.push_back(FromFsPath(it->path().filename()));
    }
    return names;
}

}  // namespace

UniversalAssemblyResolver::UniversalAssemblyResolver(
    std::optional<std::string> mainAssemblyFileName, bool throwOnError,
    std::optional<std::string> targetFramework, std::optional<std::string> runtimePack,
    PEStreamOptions streamOptions, MetadataReaderOptions metadataOptions)
    : mainAssemblyFileName_(std::move(mainAssemblyFileName)),
      throwOnError_(throwOnError),
      streamOptions_(streamOptions),
      metadataOptions_(metadataOptions),
      targetFramework_(targetFramework ? std::move(*targetFramework) : std::string()),
      runtimePack_(
          runtimePack ? std::move(*runtimePack) : std::string("Microsoft.NETCore.App")) {
    ParsedTargetFramework parsed = ParseTargetFramework(targetFramework_);
    targetFrameworkIdentifier_ = parsed.Identifier;
    targetFrameworkVersion_ = parsed.ParsedVersion;
    if (mainAssemblyFileName_) {
        // The C# `baseDirectory = Path.GetDirectoryName(mainAssemblyFileName)`
        // -- null (a root path / no directory), empty, or whitespace-only all
        // map to Environment.CurrentDirectory.
        std::optional<std::string> dir = GetDirectoryName(*mainAssemblyFileName_);
        if (!dir || IsNullOrWhiteSpace(*dir)) {
            baseDirectory_ = GetCurrentDirectoryUtf8();
        } else {
            baseDirectory_ = *dir;
        }
        AddSearchDirectory(baseDirectory_);
    }
}

UniversalAssemblyResolver::~UniversalAssemblyResolver() = default;

void UniversalAssemblyResolver::AddSearchDirectory(std::optional<std::string> directory) {
    directories_.push_back(directory);
    // The C# `if (dotNetCorePathFinder.IsValueCreated)`.
    if (dotNetCorePathFinder_) {
        dotNetCorePathFinder_->AddSearchDirectory(std::move(directory));
    }
}

void UniversalAssemblyResolver::RemoveSearchDirectory(std::optional<std::string> directory) {
    // The C# `List<string?>.Remove` -- the FIRST matching entry (null
    // compares equal to null).
    for (auto it = directories_.begin(); it != directories_.end(); ++it) {
        if (*it == directory) {
            directories_.erase(it);
            break;
        }
    }
    if (dotNetCorePathFinder_) {
        dotNetCorePathFinder_->RemoveSearchDirectory(std::move(directory));
    }
}

std::vector<std::optional<std::string>> UniversalAssemblyResolver::GetSearchDirectories()
    const {
    return directories_;
}

bool UniversalAssemblyResolver::IsSharedAssembly(const IAssemblyReference& reference,
    std::optional<std::string>& runtimePack) const {
    return Finder().TryResolveDotNetCoreShared(reference, runtimePack).has_value();
}

std::optional<std::string> UniversalAssemblyResolver::FindAssemblyFile(
    const IAssemblyReference& name) const {
    return FindAssemblyFileCore(name);
}

std::optional<std::string> UniversalAssemblyResolver::FindAssemblyFileCore(
    const IAssemblyReference& name) const {
    if (name.IsWindowsRuntime()) {
        return FindWindowsMetadataFile(name);
    }

    // The C# switch's `goto default` arms: every non-returning arm falls
    // through to ResolveInternal (the default case).
    std::optional<std::string> file;
    switch (targetFrameworkIdentifier_) {
        case TargetFrameworkIdentifier::NET:
        case TargetFrameworkIdentifier::NETCoreApp:
        case TargetFrameworkIdentifier::NETStandard:
            if (!IsZeroOrAllOnes(targetFrameworkVersion_)) {
                file = Finder().TryResolveDotNetCore(name);
                if (file) return file;
            }
            break;
        case TargetFrameworkIdentifier::Silverlight:
            if (!IsZeroOrAllOnes(targetFrameworkVersion_)) {
                file = ResolveSilverlight(name, targetFrameworkVersion_);
                if (file) return file;
            }
            break;
        default:
            break;
    }
    return ResolveInternal(name);
}

std::unique_ptr<DotNetCorePathFinder> UniversalAssemblyResolver::InitDotNetCorePathFinder()
    const {
    std::unique_ptr<DotNetCorePathFinder> finder;
    if (!mainAssemblyFileName_) {
        finder = std::make_unique<DotNetCorePathFinder>(
            targetFrameworkIdentifier_, targetFrameworkVersion_, runtimePack_);
    } else {
        finder = std::make_unique<DotNetCorePathFinder>(*mainAssemblyFileName_,
            targetFramework_, runtimePack_, targetFrameworkIdentifier_,
            targetFrameworkVersion_);
    }
    for (const auto& directory : directories_) {
        finder->AddSearchDirectory(directory);
    }
    return finder;
}

DotNetCorePathFinder& UniversalAssemblyResolver::Finder() const {
    if (!dotNetCorePathFinder_) {
        dotNetCorePathFinder_ = InitDotNetCorePathFinder();
    }
    return *dotNetCorePathFinder_;
}

std::optional<std::string> UniversalAssemblyResolver::FindWindowsMetadataFile(
    const IAssemblyReference& name) const {
    // The C# `Environment.OSVersion.Platform != PlatformID.Win32NT` early
    // return is unreachable on the Windows host the resolver machinery
    // targets (pinned).
    std::optional<std::string> programFilesX86 = GetFolderPathProgramFilesX86();
    if (!programFilesX86) {
        return FindWindowsMetadataInSystemDirectory(name);
    }
    std::string basePath =
        JoinPaths(JoinPaths(JoinPaths(*programFilesX86, "Windows Kits"), "10"), "References");
    if (!DirectoryExists(basePath)) {
        return FindWindowsMetadataInSystemDirectory(name);
    }
    // The C# `foreach (var versionFolder in di.EnumerateDirectories())
    // basePath = versionFolder.FullName;` -- the LAST enumerated directory
    // (the newest SDK folder under the shared FindFirstFile enumeration
    // order).
    std::optional<std::string> last;
    std::error_code ec;
    fs::directory_iterator it(ToFsPath(basePath), ec);
    fs::directory_iterator end;
    for (; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        if (!it->is_directory(dirEc) || dirEc) continue;
        last = FromFsPath(it->path());
    }
    if (!last) {
        return FindWindowsMetadataInSystemDirectory(name);
    }
    basePath = JoinPaths(*last, name.Name());
    if (!DirectoryExists(basePath)) {
        return FindWindowsMetadataInSystemDirectory(name);
    }
    basePath =
        JoinPaths(basePath, FindClosestVersionDirectory(basePath, name.Version()));
    if (!DirectoryExists(basePath)) {
        return FindWindowsMetadataInSystemDirectory(name);
    }
    std::string file = JoinPaths(basePath, name.Name() + ".winmd");
    if (!FileExists(file)) {
        return FindWindowsMetadataInSystemDirectory(name);
    }
    return file;
}

std::optional<std::string> UniversalAssemblyResolver::FindWindowsMetadataInSystemDirectory(
    const IAssemblyReference& name) const {
    std::optional<std::string> systemDirectory = GetSystemDirectoryPath();
    if (!systemDirectory) return std::nullopt;
    std::string file =
        JoinPaths(JoinPaths(*systemDirectory, "WinMetadata"), name.Name() + ".winmd");
    if (FileExists(file)) return file;
    return std::nullopt;
}

std::optional<std::string> UniversalAssemblyResolver::ResolveSilverlight(
    const IAssemblyReference& name, const std::optional<TypeSystem::Version>& version) const {
    for (const std::optional<std::string>& programFiles :
        {GetFolderPathProgramFiles(), GetFolderPathProgramFilesX86()}) {
        if (!programFiles) continue;
        std::string baseDirectory = JoinPaths(*programFiles, "Microsoft Silverlight");
        if (!DirectoryExists(baseDirectory)) continue;
        std::string versionDirectory =
            JoinPaths(baseDirectory, FindClosestVersionDirectory(baseDirectory, version));
        std::optional<std::string> file = SearchDirectory(name, versionDirectory);
        if (file) return file;
    }
    return std::nullopt;
}

std::string UniversalAssemblyResolver::FindClosestVersionDirectory(const std::string& basePath,
    const std::optional<TypeSystem::Version>& version) const {
    // The C# `GetDirectories().Select(ConvertToVersion).Where(v => v.Item1
    // != null).OrderByDescending(v => v.Item1)` -- the stable descending
    // sort (the subdirectory enumeration order is not observable through
    // the sort).
    struct FolderEntry {
        TypeSystem::Version Version;
        std::string Name;
    };
    std::vector<FolderEntry> folders;
    for (const std::string& name : GetSubdirectoryNames(basePath)) {
        std::optional<TypeSystem::Version> parsed =
            DotNetCorePathFinder::ConvertToVersion(name);
        if (parsed) folders.push_back({*parsed, name});
    }
    std::stable_sort(folders.begin(), folders.end(), [](const FolderEntry& a,
                              const FolderEntry& b) {
        return a.Version.CompareTo(b.Version) > 0;
    });
    // The C# `if (path == null || version == null || folder.Item1 >= version)
    // path = folder.Item2.Name;` walk.
    std::optional<std::string> path;
    for (const FolderEntry& folder : folders) {
        if (!path || !version || folder.Version.CompareTo(*version) >= 0) {
            path = folder.Name;
        }
    }
    if (path) return *path;
    return version ? version->ToString() : ".";
}

std::optional<std::string> UniversalAssemblyResolver::ResolveInternal(
    const IAssemblyReference& name) const {
    std::optional<std::string> assembly = SearchDirectory(name, directories_);
    if (assembly) return assembly;

    // The pinned NETCoreApp host arm: `<windir>\Microsoft.NET\\Framework64\\
    // v4.0.30319`. The C#'s `goto default` fallback when the Windows folder
    // is unavailable, and the Mono/NETFramework default arms (the decompiler
    // host's own runtime-module directory -- no native analogue), are
    // unreachable on the Windows host; an unavailable Windows folder leaves
    // the framework directories empty (the searches skip).
    std::vector<std::optional<std::string>> frameworkDirs;
    if (std::optional<std::string> windir = GetWindowsFolderPath()) {
        frameworkDirs.push_back(JoinPaths(
            JoinPaths(JoinPaths(*windir, "Microsoft.NET"), "Framework64"), "v4.0.30319"));
    }

    if (IsSpecialVersionOrRetargetable(name)) {
        assembly = SearchDirectory(name, frameworkDirs);
        if (assembly) return assembly;
    }

    if (name.Name() == "mscorlib") {
        assembly = GetCorlib(name);
        if (assembly) return assembly;
    }

    assembly = GetAssemblyInGac(name);
    if (assembly) return assembly;

    // "when decompiling assemblies that target frameworks prior to 4.0, we
    // can fall back to the 4.0 assemblies ... but when looking for
    // Microsoft.Build.Framework, Version=15.0.0.0 we should not use the
    // version 4.0 assembly here". The C# `name.Version <= new Version(4, 0,
    // 0, 0)` over the NULLABLE operator: a null version is LESS than every
    // version (gold-pinned), so a version-less reference takes the arm.
    const std::optional<TypeSystem::Version> version = name.Version();
    if (!version || version->CompareTo(TypeSystem::Version(4, 0, 0, 0)) <= 0) {
        assembly = SearchDirectory(name, frameworkDirs);
        if (assembly) return assembly;
    }

    // The NETCoreApp host arm: "Hosts without a .NET Framework installation
    // (e.g. Linux, macOS) have no GAC; the only system-wide assembly store
    // there is the shared-framework directory of the runtime executing the
    // decompiler. Search it as the last resort, regardless of the requested
    // version (the runtime itself rolls forward in the same way)."
    if (std::optional<std::string> runtimeDir = GetHostRuntimeDirectory()) {
        assembly = SearchDirectory(name, *runtimeDir);
        if (assembly) return assembly;
    }

    if (throwOnError_) {
        throw ResolutionException(&name, std::nullopt);
    }
    return std::nullopt;
}

std::optional<std::string> UniversalAssemblyResolver::SearchDirectory(
    const IAssemblyReference& name,
    const std::vector<std::optional<std::string>>& directories) const {
    for (const std::optional<std::string>& directory : directories) {
        if (!directory) continue;
        std::optional<std::string> file = SearchDirectory(name, *directory);
        if (file) return file;
    }
    return std::nullopt;
}

std::optional<std::string> UniversalAssemblyResolver::SearchDirectory(
    const IAssemblyReference& name, const std::string& directory) const {
    static const char* const kWinmdExtensions[] = { ".winmd", ".dll" };
    static const char* const kDllExtensions[] = { ".dll", ".exe" };
    const bool isWindowsRuntime = name.IsWindowsRuntime();
    const char* const* extensions = isWindowsRuntime ? kWinmdExtensions : kDllExtensions;
    for (std::size_t i = 0; i < 2; i++) {
        const char* extension = extensions[i];
        std::string file = JoinPaths(directory, name.Name() + extension);
        if (!FileExists(file)) continue;
        return file;
    }
    return std::nullopt;
}

std::optional<std::string> UniversalAssemblyResolver::GetCorlib(
    const IAssemblyReference& reference) const {
    // The C# `decompilerRuntime != DecompilerRuntime.NETCoreApp` arm (the
    // host corlib identity check returning typeof(object).Module.
    // FullyQualifiedName) is unreachable under the pinned host.
    const std::optional<std::vector<std::uint8_t>>& token = reference.PublicKeyToken();
    if (!token) return std::nullopt;
    std::optional<TypeSystem::Version> version = reference.Version();
    if (!version) return std::nullopt;
    // The C# Mono arm (GetMonoMscorlibBasePath) is unreachable under the
    // pinned host.
    std::optional<std::string> path =
        GetMscorlibBasePath(*version, ToHexTokenLower(*token));
    if (!path) return std::nullopt;
    std::string file = JoinPaths(*path, "mscorlib.dll");
    if (FileExists(file)) return file;
    return std::nullopt;
}

std::optional<std::string> UniversalAssemblyResolver::GetMscorlibBasePath(
    const TypeSystem::Version& version, const std::optional<std::string>& publicKeyToken)
    const {
    // The C# local `GetSubFolderForVersion()`: the Major / MajorRevision
    // table (MajorRevision is the REVISION's high 16 bits through the
    // arithmetic shift -- an unspecified revision (-1) reads -1, gold-
    // pinned).
    auto getSubFolderForVersion = [&]() -> std::optional<std::string> {
        switch (version.Major) {
            case 1:
                if ((version.Revision >> 16) == 3300) return "v1.0.3705";
                return "v1.1.4322";
            case 2:
                return "v2.0.50727";
            case 4:
                return "v4.0.30319";
            default:
                if (throwOnError_) {
                    throw std::runtime_error("Version not supported: " + version.ToString());
                }
                return std::nullopt;
        }
    };

    if (publicKeyToken && *publicKeyToken == "969db8053d3322ac") {
        // The .NET CompactFramework arm.
        std::optional<std::string> programFiles = Is64BitOperatingSystem()
            ? GetFolderPathProgramFilesX86()
            : GetFolderPathProgramFiles();
        if (programFiles) {
            std::string cfPath = "Microsoft.NET\\SDK\\CompactFramework\\v"
                + std::to_string(version.Major) + "." + std::to_string(version.Minor)
                + "\\WindowsCE\\";
            std::string cfBasePath = JoinPaths(*programFiles, cfPath);
            if (DirectoryExists(cfBasePath)) return cfBasePath;
        }
    } else {
        std::optional<std::string> folder = getSubFolderForVersion();
        if (folder) {
            std::optional<std::string> windir = GetWindowsFolderPath();
            if (windir) {
                std::string rootPath = JoinPaths(*windir, "Microsoft.NET");
                for (const char* frameworkPath : { "Framework", "Framework64" }) {
                    std::string basePath = JoinPaths(JoinPaths(rootPath, frameworkPath), *folder);
                    if (DirectoryExists(basePath)) return basePath;
                }
            }
        }
    }

    if (throwOnError_) {
        throw std::runtime_error("Version not supported: " + version.ToString());
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// The file-loading half: `Resolve` / `ResolveModule` /
// `CreatePEFileFromFileName`.
//
// The probe machinery below is file-local.
namespace {
//
// The C# failure classification over the file bytes: the C# `new FileStream`
// open failure is the IOException arm, the C# `new PEReader` eager header
// validation is the BadImageFormatException arm, and the C# `MetadataFile`
// ctor over a valid image with no CLI directory throws
// `MetadataFileNotSupportedException`, which escapes BOTH catches and
// propagates out of Resolve/ResolveModule regardless of throwOnError. The
// port's MetadataFile constructor never throws (it reports IsValid()), so
// the classification is a file-bytes probe over the port's own PE-header
// parser: a non-PE (or truncated-header) file is the PEReager arm, a valid
// PE with no cor20 directory is the escape arm, and a valid CLI image whose
// metadata does not parse is the GetMetadataReader arm (the port's
// IsValid() = false after the cor-directory probe).
struct PeHeaderProbe {
    bool validPe = false;         // the C# `new PEReader` eager validation
    bool hasCorDirectory = false; // the C# `reader.HasMetadata` test
};

// The probe's local ReadAllBytes copy (the MetadataFile.cpp local is not
// exported; the established per-consumer convention).
std::shared_ptr<const std::vector<std::uint8_t>> ReadAllBytesForProbe(
    std::string_view path) {
    std::ifstream f((std::string{path}), std::ios::binary);
    if (!f) return nullptr;
    auto bytes = std::make_shared<std::vector<std::uint8_t>>();
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz < 0) return nullptr;
    bytes->resize(static_cast<std::size_t>(sz));
    f.seekg(0);
    f.read(reinterpret_cast<char*>(bytes->data()), bytes->size());
    if (f.gcount() != static_cast<std::streamsize>(bytes->size())) return nullptr;
    return bytes;
}

PeHeaderProbe ProbePeHeaders(const std::string& path) {
    PeHeaderProbe probe;
    auto image = ReadAllBytesForProbe(path);
    if (!image) return probe;
    PeImage pe(image);
    // The PE signature check sits between the port's PeImage header parse
    // (MZ + e_lfanew bounds) and the C# PEReader's full validation matrix
    // (the documented divergence: the port's probe carries the MZ/PE-sig
    // subset, which is what every real resolver fixture separates).
    if (!pe.Valid()) return probe;
    if (pe.Size() < 4) return probe;
    const auto* bytes = pe.Data();
    std::uint32_t e_lfanew = 0;
    std::memcpy(&e_lfanew, bytes + 0x3C, sizeof(e_lfanew));
    if (e_lfanew + 4 > pe.Size()) return probe;
    if (bytes[e_lfanew] != 'P' || bytes[e_lfanew + 1] != 'E'
        || bytes[e_lfanew + 2] != 0 || bytes[e_lfanew + 3] != 0) {
        return probe;
    }
    probe.validPe = true;
    // The C# `PEReader.HasMetadata`: the CLI directory entry's presence
    // (winmd's own is_database reads the same directory; a zero entry is a
    // non-managed PE -- the MetadataFileNotSupportedException arm).
    probe.hasCorDirectory = pe.ComDirectoryRva() != 0;
    return probe;
}

}  // namespace

const MetadataFile* UniversalAssemblyResolver::Resolve(
    const IAssemblyReference& name) const {
    std::optional<std::string> file = FindAssemblyFile(name);
    // The C# factory captures `name` and `file`; the ctor-1
    // ResolutionException renders `resolvedPath ?? "<not found>"` from the
    // captured file.
    ResolutionExceptionFactory makeException =
        [&name, &file](const std::exception*) {
            return ResolutionException(&name, file);
        };
    return CreatePEFileFromFileName(file, makeException);
}

const MetadataFile* UniversalAssemblyResolver::ResolveModule(
    const MetadataFile& mainModule, const std::string& moduleName) const {
    // The C# `Path.GetDirectoryName(mainModule.FileName)` -- null for the
    // empty string and the root forms, so a main-module file name with no
    // directory information takes the early return.
    std::optional<std::string> baseDirectory = GetDirectoryName(mainModule.FileName());
    if (!baseDirectory) return nullptr;
    // The C# `Path.Combine(baseDirectory, moduleName)` (the rooted-second
    // and empty-component rules the local JoinPaths carries).
    std::string moduleFileName = JoinPaths(*baseDirectory, moduleName);
    // The ctor-2 factory: the exception carries the main-module path, the
    // module name, and the composed module path.
    ResolutionExceptionFactory makeException =
        [&mainModule, &moduleName, &moduleFileName](const std::exception*) {
            return ResolutionException(mainModule.FileName(), moduleName,
                                       moduleFileName);
        };
    return CreatePEFileFromFileName(moduleFileName, makeException);
}

// The C# private `MetadataFile? CreatePEFileFromFileName(string? fileName,
// Func<Exception?, Exception> makeException)` -- the load-and-fail machinery
// both Resolve arms share (the header documents the failure classes).
const MetadataFile* UniversalAssemblyResolver::CreatePEFileFromFileName(
    std::optional<std::string> fileName,
    const ResolutionExceptionFactory& makeException) const {
    // The C# null-fileName arm.
    if (!fileName) {
        if (throwOnError_) throw makeException(nullptr);
        return nullptr;
    }

    // The C# `new FileStream(fileName, FileMode.Open, FileAccess.Read)`
    // open failure -- FileNotFoundException/DirectoryNotFoundException (the
    // IOException catch arm). An UnauthorizedAccessException would escape
    // the C# catches; unreachable through the port's swallow-everything
    // MetadataFile constructor, so every unopenable file maps to the
    // IOException arm (documented divergence).
    {
        std::ifstream probe(*fileName, std::ios::binary);
        if (!probe.is_open()) {
            if (throwOnError_) throw makeException(nullptr);
            return nullptr;
        }
    }

    // The PE-header probe: the C# `new PEReader(stream, streamOptions)`
    // validates the headers eagerly (the BadImageFormatException catch
    // arm), then the C# MetadataFile ctor throws
    // MetadataFileNotSupportedException for a valid image with no CLI
    // directory -- the exception that escapes both catches.
    PeHeaderProbe pe = ProbePeHeaders(*fileName);
    if (!pe.validPe) {
        if (throwOnError_) throw makeException(nullptr);
        return nullptr;
    }
    if (!pe.hasCorDirectory) {
        throw MetadataFileNotSupportedException(
            "PE file does not contain any managed metadata.");
    }

    // The metadata parse: the C# GetMetadataReader BadImageFormatException
    // over a valid CLI image with corrupt tables -- the
    // BadImageFormatException catch arm again (the port's IsValid() report).
    auto file = std::make_unique<MetadataFile>(*fileName);
    if (!file->IsValid()) {
        if (throwOnError_) throw makeException(nullptr);
        return nullptr;
    }

    // The keep-alive registry: the C# GC roots every PEFile the resolver
    // returns; the port's non-owning pointer borrows the registry's entry.
    const MetadataFile* result = file.get();
    resolvedFiles_.push_back(std::move(file));
    return result;
}

}  // namespace ILSpy::Decompiler::Metadata
