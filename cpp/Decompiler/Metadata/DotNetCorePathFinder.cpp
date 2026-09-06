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

// The implementation of DotNetCorePathFinder.hpp (the
// ICSharpCode.Decompiler/Metadata/DotNetCorePathFinder.cs port).

#include "Decompiler/Metadata/DotNetCorePathFinder.hpp"

#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if !defined(_WIN32)
#include <climits>
#endif

namespace fs = std::filesystem;

namespace ILSpy::Decompiler::Metadata {

namespace {

// ---------------------------------------------------------------------------
// The OS-boundary conversion (the CLI's ToNativePath convention): the C#
// composes Unicode paths end to end (System.IO), so a non-ASCII path must
// survive -- on Windows the fs::path narrow ctor would transcode through the
// ANSI code page instead.

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

// The .NET `Environment.GetEnvironmentVariable(name)` (the UTF-8 form; a
// missing variable is null).
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

// The .NET `Environment.GetFolderPath(Environment.SpecialFolder.UserProfile)`
// on Windows resolves to the USERPROFILE variable (the SHGetKnownFolderPath
// folder; the same value in every practical environment).
std::string GetUserProfileFolderPath() {
    auto value = GetEnvironmentVariableUtf8(L"USERPROFILE", "USERPROFILE");
    return value.value_or("");
}

// ---------------------------------------------------------------------------
// The .NET System.IO.Path helpers (the decompiled PathInternal machinery the
// iteration-31 sanitizer built; copied next to this consumer). Every arm is
// gold-pinned by the DncpfProbe P section.

bool WinIsDirectorySeparator(char c) {
    return c == '\\' || c == '/';
}

bool WinIsValidDriveChar(char value) {
    // The C# `(uint)((value | 0x20) - 97) <= 25u`: the ASCII letters, both
    // cases.
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

// The decompiled System.IO.PathInternal.GetRootLength (the Windows build):
// the length of the root prefix (a DOS drive root 'X:\'/'X:', a UNC root
// '\\server\share\', or a device root '\\?\...').
std::size_t WinGetRootLength(std::string_view path) {
    std::size_t length = path.size();
    std::size_t i = 0;
    bool isDevice = WinIsDevice(path);
    bool isDeviceUnc = isDevice && WinIsDeviceUnc(path);
    if ((!isDevice || isDeviceUnc) && length > 0
        && WinIsDirectorySeparator(path[0])) {
        // UNC or device: \\server\share or \\?\...
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
}

// The decompiled System.IO.PathInternal.NormalizeDirectorySeparators: the
// fast-path scan (every separator a backslash with a non-separator after
// it), then the rebuild -- one leading backslash and every separator run
// collapsed to its last member, forward slashes replaced.
std::string NormalizeDirectorySeparators(const std::string& path) {
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
}

// The .NET `Path.IsPathRooted(string)`: a leading separator or a
// drive-letter prefix ('C:' IS rooted -- a drive-relative path counts,
// gold-pinned).
bool IsPathRooted(std::string_view path) {
    if (path.empty()) return false;
    if (WinIsDirectorySeparator(path[0])) return true;
    return path.size() >= 2 && WinIsValidDriveChar(path[0]) && path[1] == ':';
}

// The .NET ArgumentNullException the Path.Combine null arms throw (the
// exact two-line message the gold dumps print as one line).
[[noreturn]] void ThrowPathNull(const char* parameter) {
    throw std::invalid_argument(std::string("Value cannot be null. (Parameter '")
        + parameter + "')");
}

// The .NET `Path.Combine` pairwise join (the nulls already excluded): an
// empty second yields the first, an empty first the second; a ROOTED second
// (a leading separator or a drive prefix, drive-relative included) returns
// as-is; a first ending in a separator concatenates directly; anything else
// inserts the platform separator.
std::string JoinPaths(const std::string& first, std::string_view second) {
    if (second.empty()) return first;
    if (first.empty()) return std::string(second);
    if (IsPathRooted(second)) return std::string(second);
    char last = first[first.size() - 1];
    if (last == '\\' || last == '/') return first + std::string(second);
    return first + "\\" + std::string(second);
}

// The .NET `Path.Combine(path1, path2)` -- the two-argument form (the
// ctor's deps.json probe and every directory probe): a null argument throws
// with its position name ('path1'/'path2').
std::string CombinePaths(const std::optional<std::string>& path1, std::string_view path2) {
    if (!path1) ThrowPathNull("path1");
    return JoinPaths(*path1, path2);
}

// The .NET `Path.Combine(path1, path2, path3)` (the shared/pack
// compositions): the FIRST null argument names itself.
std::string CombinePaths(const std::optional<std::string>& path1,
    const std::optional<std::string>& path2,
    const std::optional<std::string>& path3) {
    if (!path1) ThrowPathNull("path1");
    if (!path2) ThrowPathNull("path2");
    if (!path3) ThrowPathNull("path3");
    return JoinPaths(JoinPaths(*path1, *path2), *path3);
}

// The .NET `Path.Combine(path1, path2, path3, path4)` (the ctor's package
// base path composition): the FIRST null argument names itself (an empty
// runtime component yields a null itemPath, and the message reads 'path4',
// gold-pinned).
std::string CombinePaths(const std::optional<std::string>& path1,
    const std::optional<std::string>& path2,
    const std::optional<std::string>& path3,
    const std::optional<std::string>& path4) {
    if (!path1) ThrowPathNull("path1");
    if (!path2) ThrowPathNull("path2");
    if (!path3) ThrowPathNull("path3");
    if (!path4) ThrowPathNull("path4");
    return JoinPaths(JoinPaths(JoinPaths(*path1, *path2), *path3), *path4);
}

// The .NET `Path.GetDirectoryName(string)` (gold-pinned): null for the empty
// string and for root forms ('C:', 'C:\', '\\server\share', '/'); otherwise
// everything before the LAST separator (a trailing separator cuts to the
// path minus that separator: 'C:\x\y\' -> 'C:\x\y'), with the separators in
// the returned text NORMALIZED to the platform separator ('a/b/c/d.dll' ->
// 'a\b\c') and no separator at all yielding the root ('a' -> '', 'C:x' ->
// 'C:').
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

// The .NET `Path.GetFileNameWithoutExtension(string)` (gold-pinned): the
// root-aware file-name slice (the max(rootLength, lastSeparator+1) rule --
// 'C:' and '/' have no file name) minus everything from its LAST '.'.
std::string GetFileNameWithoutExtension(const std::string& path) {
    std::size_t root = WinGetRootLength(path);
    std::size_t num = path.find_last_of("\\/");
    std::size_t sep = num == std::string::npos ? 0 : num + 1;
    std::size_t start = sep > root ? sep : root;
    std::string name = path.substr(start);
    std::size_t dot = name.find_last_of('.');
    if (dot == std::string::npos) return name;
    return name.substr(0, dot);
}

// ---------------------------------------------------------------------------
// The filesystem probes.

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

// The .NET `DirectoryInfo.GetDirectories()`: the subdirectory names. A
// missing directory throws DirectoryNotFoundException ("Could not find a
// part of the path '<path>'.") -- the message the GetClosestVersionFolder
// and GetReferenceAssemblyPath arms surface.
std::vector<std::string> GetDirectories(const std::string& basePath) {
    fs::path dir = ToFsPath(basePath);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        throw std::runtime_error("Could not find a part of the path '" + basePath + "'.");
    }
    std::vector<std::string> names;
    fs::directory_iterator it(dir, ec);
    if (ec) return names;
    fs::directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (it->is_directory()) {
            names.push_back(FromFsPath(it->path().filename()));
        }
    }
    return names;
}

// The .NET `DirectoryInfo.EnumerateFiles("*.dll",
// SearchOption.AllDirectories).Any()` gate: a recursive walk answering
// whether any FILE matches the pattern. .NET 10's default MatchType is
// Simple (the DOS 8.3 three-char-extension quirk does NOT fire --
// gold-pinned: 'y.dlly' and 'y.dllx' do NOT match), so the match is the
// case-insensitive extension equality against the text after the last '.'
// (a trailing dot yields the empty extension).
bool FileNameMatchesDllPattern(const std::string& name) {
    std::size_t dot = name.find_last_of('.');
    std::string extension = dot == std::string::npos ? "" : name.substr(dot + 1);
    if (extension.size() != 3) return false;
    return (extension[0] | 0x20) == 'd' && (extension[1] | 0x20) == 'l'
        && (extension[2] | 0x20) == 'l';
}

bool ContainsDllRecursive(const fs::path& dir) {
    std::error_code ec;
    fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec);
    if (ec) return false;
    fs::recursive_directory_iterator end;
    for (; it != end; it.increment(ec)) {
        if (ec) {
            ec.clear();
            continue;
        }
        if (it->is_regular_file()) {
            if (FileNameMatchesDllPattern(FromFsPath(it->path().filename()))) {
                return true;
            }
        }
    }
    return false;
}

// The .NET `string.ToLowerInvariant()` restricted to the ASCII block (the
// full invariant casing table is out of scope; a non-ASCII unit in a
// package base path keeps its case -- the only observable surface is the
// returned path text, and NuGet layout names are ASCII).
std::string ToLowerInvariantAscii(std::string_view text) {
    std::string result(text);
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

// The .NET `string.IsNullOrWhiteSpace` over the char.IsWhiteSpace set.
bool IsNullOrWhiteSpace(std::string_view text) {
    if (text.empty()) return true;
    std::u16string wide = Util::Utf8ToUtf16(text);
    for (char16_t c : wide) {
        if (!Util::IsWhiteSpace(c)) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// The .deps.json read (the LightJson surface over nlohmann's SAX parser --
// see the header's divergence notes).

// The LightJson JsonValue subset LoadPackageInfos consumes: the document
// tree with insertion-ordered object properties.
struct JsonNode {
    enum class Kind {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object,
    };

    Kind kind = Kind::Null;
    bool boolean = false;
    // The original number token (the dead AsString arm's text; the number
    // value itself is never consumed).
    std::string numberToken;
    std::string str;
    std::vector<JsonNode> items;
    std::vector<std::pair<std::string, JsonNode>> properties;
};

// The LightJson JsonParseException texts the reader arms throw.
constexpr const char* kJsonIncompleteMessage =
    "The string ended before a value could be parsed.";
constexpr const char* kJsonInvalidCharMessage =
    "The parser encountered an invalid or unexpected character.";
constexpr const char* kJsonDuplicateKeysMessage =
    "The parser encountered a JsonObject with duplicate keys.";

// The nlohmann SAX handler building the JsonNode tree. nlohmann does the
// tokenization (escapes, numbers, UTF-8 validation, the comment skipping
// via ignore_comments); the handler adds LightJson's duplicate-object-key
// rejection and the exception-message mapping.
class LightJsonSax {
public:
    using number_integer_t = std::int64_t;
    using number_unsigned_t = std::uint64_t;
    using number_float_t = double;
    using string_t = std::string;
    using binary_t = nlohmann::json::binary_t;

    bool null() {
        Attach(JsonNode{});
        return true;
    }

    bool boolean(bool value) {
        JsonNode node;
        node.kind = JsonNode::Kind::Boolean;
        node.boolean = value;
        Attach(std::move(node));
        return true;
    }

    bool number_integer(number_integer_t) {
        JsonNode node;
        node.kind = JsonNode::Kind::Number;
        Attach(std::move(node));
        return true;
    }

    bool number_unsigned(number_unsigned_t) {
        JsonNode node;
        node.kind = JsonNode::Kind::Number;
        Attach(std::move(node));
        return true;
    }

    bool number_float(number_float_t, const string_t& token) {
        JsonNode node;
        node.kind = JsonNode::Kind::Number;
        node.numberToken = token;
        Attach(std::move(node));
        return true;
    }

    bool string(string_t& value) {
        JsonNode node;
        node.kind = JsonNode::Kind::String;
        node.str = std::move(value);
        Attach(std::move(node));
        return true;
    }

    bool binary(binary_t&) {
        // JSON text never produces binary values.
        return true;
    }

    bool start_object(std::size_t) {
        stack_.push_back(Frame{JsonNode::Kind::Object});
        return true;
    }

    bool key(string_t& value) {
        // LightJson's ReadObject rejects a duplicate key.
        for (const auto& property : stack_.back().node.properties) {
            if (property.first == value) {
                throw std::runtime_error(kJsonDuplicateKeysMessage);
            }
        }
        stack_.back().pendingKey = std::move(value);
        return true;
    }

    bool end_object() {
        Attach(Pop());
        return true;
    }

    bool start_array(std::size_t) {
        stack_.push_back(Frame{JsonNode::Kind::Array});
        return true;
    }

    bool end_array() {
        Attach(Pop());
        return true;
    }

    bool parse_error(std::size_t position, const std::string&,
        const nlohmann::detail::exception& ex) {
        // The LightJson TextScanner's two arms: Read/Peek at EOF throws
        // the IncompleteMessage (the empty input, the truncated
        // structure); a lexer failure at a present character is the
        // InvalidOrUnexpectedCharacter. nlohmann reports both through
        // the id-101 parse_error family (the empty input and the
        // mid-parse "unexpected end of input" spell the EOF arm), so the
        // message text is the discriminator.
        (void)position;
        const char* message = ex.what();
        if (std::strstr(message, "end of input") != nullptr
            || std::strstr(message, "empty input") != nullptr) {
            throw std::runtime_error(kJsonIncompleteMessage);
        }
        throw std::runtime_error(kJsonInvalidCharMessage);
    }

    JsonNode TakeRoot() { return std::move(root_); }

private:
    // One in-progress value per nesting level, each with its own pending
    // property key (a nested object's key events must not clobber the
    // enclosing level's pending key).
    struct Frame {
        JsonNode node;
        std::string pendingKey;

        explicit Frame(JsonNode::Kind kind) {
            node.kind = kind;
        }
    };

    void Attach(JsonNode value) {
        if (stack_.empty()) {
            root_ = std::move(value);
            return;
        }
        JsonNode& top = stack_.back().node;
        if (top.kind == JsonNode::Kind::Object) {
            top.properties.emplace_back(std::move(stack_.back().pendingKey),
                std::move(value));
        } else {
            top.items.push_back(std::move(value));
        }
    }

    JsonNode Pop() {
        JsonNode node = std::move(stack_.back().node);
        stack_.pop_back();
        return node;
    }

    JsonNode root_;
    std::vector<Frame> stack_;
};

// The LightJson `JsonReader.Parse(string)` (one value; trailing content
// ignored) over the file text (`File.ReadAllText`: the UTF-8 BOM stripped,
// the bytes passed as UTF-8 -- non-UTF-8 bytes diverge, see the header).
JsonNode ParseJsonFile(const std::string& path) {
    std::ifstream file(ToFsPath(path), std::ios::binary);
    if (!file) {
        // File.ReadAllText over a vanished/unreadable file: the
        // FileNotFoundException family (the ctor checked File.Exists first,
        // so this arm is a TOCTOU-only shape).
        throw std::runtime_error("Could not find a part of the path '" + path + "'.");
    }
    std::string text((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF
        && static_cast<unsigned char>(text[1]) == 0xBB
        && static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    LightJsonSax sax;
    // strict=false: LightJson's Parse reads one value and ignores trailing
    // content. ignore_comments=true: LightJson's TextScanner skips // and
    // /* */ comments.
    nlohmann::json::sax_parse(text, &sax, nlohmann::json::input_format_t::json,
        /*strict=*/false, /*ignore_comments=*/true);
    return sax.TakeRoot();
}

// The LightJson `JsonObject` indexer: the property with the key, or null
// (the missing-key JsonValue.Null placeholder).
const JsonNode* FindProperty(const JsonNode& object, std::string_view key) {
    for (const auto& property : object.properties) {
        if (property.first == key) return &property.second;
    }
    return nullptr;
}

// The LightJson `JsonValue.AsString` (the null-tolerant conversion the dead
// Type/Path fields consume): a string stays; booleans render
// "true"/"false"; numbers carry their raw token; objects, arrays, and the
// null placeholder yield null.
std::optional<std::string> AsString(const JsonNode* value) {
    if (value == nullptr) return std::nullopt;
    switch (value->kind) {
        case JsonNode::Kind::Null:
        case JsonNode::Kind::Array:
        case JsonNode::Kind::Object:
            return std::nullopt;
        case JsonNode::Kind::Boolean:
            return value->boolean ? "true" : "false";
        case JsonNode::Kind::Number:
            return value->numberToken;
        case JsonNode::Kind::String:
            return value->str;
    }
    return std::nullopt;
}

}  // namespace

// ---------------------------------------------------------------------------
// The class.

DotNetCorePathFinder::DotNetCorePathFinder(TargetFrameworkIdentifier targetFramework,
    TypeSystem::Version targetFrameworkVersion,
    std::optional<std::string> preferredRuntimePack)
    : targetFrameworkVersion_(std::move(targetFrameworkVersion)),
      dotnetBasePath_(FindDotNetExeDirectory()),
      preferredRuntimePack_(std::move(preferredRuntimePack)) {
    // ".NET Standard 2.1 is implemented by .NET Core 3.0 or higher"
    if (targetFramework == TargetFrameworkIdentifier::NETStandard
        && targetFrameworkVersion_.Major == 2 && targetFrameworkVersion_.Minor == 1) {
        targetFrameworkVersion_ = TypeSystem::Version(3, 0, 0);
    }
}

DotNetCorePathFinder::DotNetCorePathFinder(const std::string& parentAssemblyFileName,
    const std::string& targetFrameworkIdString,
    std::optional<std::string> preferredRuntimePack,
    TargetFrameworkIdentifier targetFramework,
    TypeSystem::Version targetFrameworkVersion,
    ReferenceLoadInfo* loadInfo)
    : DotNetCorePathFinder(targetFramework, std::move(targetFrameworkVersion),
        std::move(preferredRuntimePack)) {
    std::string assemblyName = GetFileNameWithoutExtension(parentAssemblyFileName);
    std::optional<std::string> basePath = GetDirectoryName(parentAssemblyFileName);

    searchPaths_.push_back(basePath);

    // The C# `Path.Combine(basePath, $"{assemblyName}.deps.json")`: a null
    // basePath (a root-form parent path) throws here.
    std::string depsJsonFileName = CombinePaths(basePath, assemblyName + ".deps.json");

    if (FileExists(depsJsonFileName)) {
        packages_ = LoadPackageInfos(depsJsonFileName, targetFrameworkIdString);

        // The C# static LookupPaths (the NUGET_PACKAGES variable and the
        // user-profile NuGet cache), read per construction (the documented
        // per-process/per-construction divergence).
        std::vector<std::optional<std::string>> lookupPaths;
        lookupPaths.push_back(
            GetEnvironmentVariableUtf8(L"NUGET_PACKAGES", "NUGET_PACKAGES"));
        lookupPaths.push_back(
            CombinePaths(GetUserProfileFolderPath(), ".nuget", "packages"));

        for (const std::optional<std::string>& path : lookupPaths) {
            if (IsNullOrWhiteSpace(path.value_or(""))) continue;
            for (const DotNetCorePackageInfo& package : packages_) {
                for (const std::string& item : package.RuntimeComponents) {
                    std::optional<std::string> itemPath = GetDirectoryName(item);
                    std::string fullPath = ToLowerInvariantAscii(
                        CombinePaths(*path, package.Name, package.Version, itemPath));
                    if (DirectoryExists(fullPath)) {
                        packageBasePaths_.push_back(fullPath);
                    }
                }
            }
        }
    } else {
        if (loadInfo != nullptr) {
            loadInfo->AddMessage(assemblyName, MessageKind::Warning,
                assemblyName + ".deps.json could not be found!");
        }
    }
}

void DotNetCorePathFinder::AddSearchDirectory(std::optional<std::string> path) {
    searchPaths_.push_back(std::move(path));
}

void DotNetCorePathFinder::RemoveSearchDirectory(std::optional<std::string> path) {
    for (auto it = searchPaths_.begin(); it != searchPaths_.end(); ++it) {
        if (*it == path) {
            searchPaths_.erase(it);
            return;
        }
    }
}

std::optional<std::string> DotNetCorePathFinder::TryResolveDotNetCore(
    const IAssemblyReference& name) {
    for (const std::optional<std::string>& basePath : searchPaths_) {
        // The C# `Path.Combine(basePath, name.Name + ".dll")`: a null search
        // path throws ArgumentNullException ('path1').
        std::string file = CombinePaths(basePath, name.Name() + ".dll");
        if (FileExists(file)) return file;
        file = CombinePaths(basePath, name.Name() + ".exe");
        if (FileExists(file)) return file;
    }
    for (const std::string& basePath : packageBasePaths_) {
        std::string file = CombinePaths(basePath, name.Name() + ".dll");
        if (FileExists(file)) return file;
        file = CombinePaths(basePath, name.Name() + ".exe");
        if (FileExists(file)) return file;
    }
    std::optional<std::string> pack;
    return TryResolveDotNetCoreShared(name, pack);
}

std::optional<std::string> DotNetCorePathFinder::TryResolveDotNetCoreShared(
    const IAssemblyReference& name, std::optional<std::string>& runtimePack) {
    if (!dotnetBasePath_) {
        runtimePack = std::nullopt;
        return std::nullopt;
    }

    // The C# RuntimePacks table, optionally headed by the preferred pack.
    std::vector<std::string> runtimePacks = {
        "Microsoft.NETCore.App",
        "Microsoft.WindowsDesktop.App",
        "Microsoft.AspNetCore.App",
        "Microsoft.AspNetCore.All",
    };
    if (preferredRuntimePack_) {
        std::vector<std::string> headed = {*preferredRuntimePack_};
        headed.insert(headed.end(), runtimePacks.begin(), runtimePacks.end());
        runtimePacks = std::move(headed);
    }

    for (const std::string& pack : runtimePacks) {
        runtimePack = pack;
        std::string basePath = CombinePaths(*dotnetBasePath_, "shared", pack);
        if (!DirectoryExists(basePath)) continue;
        std::string closestVersion = GetClosestVersionFolder(basePath, targetFrameworkVersion_);
        std::string file = CombinePaths(CombinePaths(basePath, closestVersion),
            name.Name() + ".dll");
        if (FileExists(file)) return file;
        file = CombinePaths(CombinePaths(basePath, closestVersion), name.Name() + ".exe");
        if (FileExists(file)) return file;
    }
    runtimePack = std::nullopt;
    return std::nullopt;
}

std::string DotNetCorePathFinder::GetReferenceAssemblyPath(
    const std::string& targetFramework) {
    ParsedTargetFramework parsed = ParseTargetFramework(targetFramework);
    std::string identifier;
    std::string identifierExt;
    switch (parsed.Identifier) {
        case TargetFrameworkIdentifier::NETCoreApp:
            identifier = "Microsoft.NETCore.App";
            identifierExt = "netcoreapp" + std::to_string(parsed.ParsedVersion.Major)
                + "." + std::to_string(parsed.ParsedVersion.Minor);
            break;
        case TargetFrameworkIdentifier::NETStandard:
            identifier = "NETStandard.Library";
            identifierExt = "netstandard" + std::to_string(parsed.ParsedVersion.Major)
                + "." + std::to_string(parsed.ParsedVersion.Minor);
            break;
        case TargetFrameworkIdentifier::NET:
            identifier = "Microsoft.NETCore.App";
            identifierExt = "net" + std::to_string(parsed.ParsedVersion.Major)
                + "." + std::to_string(parsed.ParsedVersion.Minor);
            break;
        default:
            // The C# `throw new NotSupportedException()`.
            throw std::logic_error("Specified method is not supported.");
    }
    std::string basePath = CombinePaths(dotnetBasePath_, "packs", identifier + ".Ref");
    std::string versionFolder = GetClosestVersionFolder(basePath, parsed.ParsedVersion);
    return CombinePaths(CombinePaths(basePath, versionFolder, "ref"), identifierExt);
}

std::optional<std::string> DotNetCorePathFinder::FindDotNetExeDirectory() {
#if defined(_WIN32)
    constexpr const char* dotnetExeName = "dotnet.exe";
    constexpr char pathSeparator = ';';
#else
    constexpr const char* dotnetExeName = "dotnet";
    constexpr char pathSeparator = ':';
#endif
    std::optional<std::string> pathEnv =
        GetEnvironmentVariableUtf8(L"PATH", "PATH");
    if (!pathEnv) {
        // The C# NullReferenceException over
        // `GetEnvironmentVariable("PATH").Split` is unreachable (PATH is
        // always set in practice); the null return is the documented
        // divergence.
        return std::nullopt;
    }
    std::string_view rest = *pathEnv;
    while (true) {
        std::size_t separator = rest.find(pathSeparator);
        std::string item(separator == std::string_view::npos
            ? std::string(rest)
            : std::string(rest.substr(0, separator)));
        std::string fileName = CombinePaths(item, dotnetExeName);
        if (FileExists(fileName)) {
#if !defined(_WIN32)
            // The C# reparse-point resolution (the Unix arm): realpath(3)
            // over a symlinked dotnet, continuing the scan when the target
            // vanishes.
            std::error_code ec;
            if (fs::is_symlink(ToFsPath(fileName), ec)) {
                char* resolved = realpath(fileName.c_str(), nullptr);
                if (resolved != nullptr) {
                    std::string real = resolved;
                    free(resolved);
                    if (FileExists(real)) {
                        return GetDirectoryName(real);
                    }
                }
            } else {
                return GetDirectoryName(fileName);
            }
#else
            return GetDirectoryName(fileName);
#endif
        }
        if (separator == std::string_view::npos) break;
        rest = rest.substr(separator + 1);
    }
    return std::nullopt;
}

std::optional<TypeSystem::Version> DotNetCorePathFinder::ConvertToVersion(
    const std::string& directoryName) {
    // The C# `RemoveTrailingVersionInfo`: everything before the FIRST '-'
    // (a dash at position 0 stays).
    std::string shortName = directoryName;
    std::size_t dashIndex = shortName.find('-');
    if (dashIndex != std::string::npos && dashIndex > 0) {
        shortName = shortName.substr(0, dashIndex);
    }
    try {
        return TypeSystem::Version(shortName);
    } catch (const std::invalid_argument&) {
        // The C# catch (Exception) -> Trace.TraceWarning -> the null tuple
        // (the default trace listeners observe nothing).
        return std::nullopt;
    } catch (const std::out_of_range&) {
        return std::nullopt;
    }
}

std::string DotNetCorePathFinder::GetClosestVersionFolder(const std::string& basePath,
    const TypeSystem::Version& version) {
    struct FolderVersion {
        TypeSystem::Version version;
        std::string name;
    };
    std::vector<FolderVersion> foundVersions;
    for (const std::string& name : GetDirectories(basePath)) {
        std::optional<TypeSystem::Version> parsed = ConvertToVersion(name);
        if (parsed) {
            foundVersions.push_back({*parsed, name});
        }
    }
    // The C# `OrderBy(v => v.version)` -- a stable ascending sort.
    std::stable_sort(foundVersions.begin(), foundVersions.end(),
        [](const FolderVersion& a, const FolderVersion& b) {
            return a.version < b.version;
        });
    for (const FolderVersion& folder : foundVersions) {
        // The C# `folder.version >= version` and the recursive *.dll gate.
        if (!(folder.version < version)
            && ContainsDllRecursive(ToFsPath(JoinPaths(JoinPaths(basePath, folder.name), "")))) {
            return folder.name;
        }
    }
    return version.ToString();
}

std::vector<DotNetCorePathFinder::DotNetCorePackageInfo>
DotNetCorePathFinder::LoadPackageInfos(const std::string& depsJsonFileName,
    const std::string& targetFramework) {
    std::vector<DotNetCorePackageInfo> packages;
    JsonNode dependencies = ParseJsonFile(depsJsonFileName);

    // `dependencies["targets"]` throws over a non-object root; a MISSING
    // "targets" (the JsonValue.Null placeholder) throws at the following
    // index (the LightJson JsonValue indexer contract).
    if (dependencies.kind != JsonNode::Kind::Object) {
        throw std::runtime_error("This value does not represent a JsonObject.");
    }
    const JsonNode* targets = FindProperty(dependencies, "targets");
    if (targets == nullptr || targets->kind != JsonNode::Kind::Object) {
        throw std::runtime_error("This value does not represent a JsonObject.");
    }
    // `dependencies["targets"][targetFramework].AsJsonObject` --
    // null-tolerant (a missing key or a non-object value yields the empty
    // package set).
    const JsonNode* targetValue = FindProperty(*targets, targetFramework);
    const JsonNode* runtimeInfos =
        (targetValue != nullptr && targetValue->kind == JsonNode::Kind::Object)
        ? targetValue : nullptr;
    // `dependencies["libraries"].AsJsonObject` -- null-tolerant.
    const JsonNode* librariesValue = FindProperty(dependencies, "libraries");
    const JsonNode* libraries =
        (librariesValue != nullptr && librariesValue->kind == JsonNode::Kind::Object)
        ? librariesValue : nullptr;
    if (runtimeInfos == nullptr || libraries == nullptr) {
        return packages;
    }

    for (const auto& [key, value] : libraries->properties) {
        // `library.Value["type"]` -- the JsonValue indexer over a
        // non-object library value throws.
        if (value.kind != JsonNode::Kind::Object) {
            throw std::runtime_error("This value does not represent a JsonObject.");
        }
        std::optional<std::string> type = AsString(FindProperty(value, "type"));
        std::optional<std::string> path = AsString(FindProperty(value, "path"));

        // `runtimeInfos[library.Key].AsJsonObject?["runtime"].AsJsonObject`
        // -- every miss in the chain yields the empty component set.
        std::vector<std::string> components;
        const JsonNode* libraryEntry = FindProperty(*runtimeInfos, key);
        if (libraryEntry != nullptr && libraryEntry->kind == JsonNode::Kind::Object) {
            const JsonNode* runtimeNode = FindProperty(*libraryEntry, "runtime");
            if (runtimeNode != nullptr && runtimeNode->kind == JsonNode::Kind::Object) {
                for (const auto& [componentKey, componentValue] : runtimeNode->properties) {
                    (void)componentValue;
                    components.push_back(componentKey);
                }
            }
        }

        DotNetCorePackageInfo package;
        // The C# `fullName.Split('/')`: parts[0] and parts[1] (the SECOND
        // slash onward is dropped from the version, faithful to Split).
        std::size_t slash = key.find('/');
        package.Name = key.substr(0, slash);
        if (slash == std::string::npos) {
            package.Version = "<UNKNOWN>";
        } else {
            std::size_t slash2 = key.find('/', slash + 1);
            package.Version = key.substr(slash + 1,
                slash2 == std::string::npos ? std::string::npos : slash2 - slash - 1);
        }
        package.Type = std::move(type);
        package.Path = std::move(path);
        package.RuntimeComponents = std::move(components);
        packages.push_back(std::move(package));
    }
    return packages;
}

}  // namespace ILSpy::Decompiler::Metadata
