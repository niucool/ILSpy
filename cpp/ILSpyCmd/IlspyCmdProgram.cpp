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
// FOR ANY CLAIM, DAMAGES OR ANY OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// IlspyCmdProgram.cpp -- see IlspyCmdProgram.hpp for the port contract.

#include "ILSpyCmd/IlspyCmdProgram.hpp"

#include "BamlDecompiler/BamlDecompilerSettings.hpp"
#include "Decompiler/CSharp/ProjectDecompiler/WholeProjectDecompiler.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "Decompiler/SingleFileBundle.hpp"
#include "Decompiler/Util/ResourcesFile.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "ILSpyX/PdbProvider/DebugInfoUtils.hpp"
#include "ILSpyCmd/ResourceExtensions.hpp"
#include "Decompiler/Metadata/PEReaderParse.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <miniz/miniz_tinfl.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace ILSpy::ILSpyCmd {
namespace {

namespace fs = std::filesystem;
namespace Sfb = ILSpy::Decompiler::SingleFileBundle;
namespace ProjectDecompiler = ILSpy::Decompiler::CSharp::ProjectDecompiler;

// The C# `Path.IsPathRooted(string)` over an entry RelativePath (the Windows
// semantics): a leading directory separator or a drive-letter prefix roots
// the path ("C:foo" is rooted even though it is drive-relative).
bool IsPathRooted(const std::string& p) {
    if (p.empty())
        return false;
    if (p[0] == '\\' || p[0] == '/')
        return true;
    return p.size() >= 2 && p[1] == ':';
}

#ifdef _WIN32
// UTF-8 to UTF-16: the C# writes extracted files through System.IO's
// Unicode paths, so a non-ASCII entry RelativePath must survive the
// conversion (the std::string-to-fs::path conversion would go through the
// ANSI code page instead).
std::wstring Utf8ToWide(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
        static_cast<int>(s.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    if (len > 0) {
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
            wide.data(), len);
    }
    return wide;
}

// UTF-16 to UTF-8 (the inverse of Utf8ToWide): the CLI's paths carry the
// UTF-8 std::string convention (wmain converted the command line up
// front), so a resolved path must come back as UTF-8 -- fs::path::string()
// would go through the ANSI code page, mangling non-ASCII names or
// throwing "No mapping for the Unicode character exists in the target
// multi-byte code page" when the page cannot represent them.
std::string WideToUtf8(const std::wstring& s) {
    int len = WideCharToMultiByte(CP_UTF8, 0, s.data(),
        static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(len > 0 ? len : 0), '\0');
    if (len > 0) {
        WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
            utf8.data(), len, nullptr, nullptr);
    }
    return utf8;
}

fs::path ToFsPath(const std::string& utf8) {
    return fs::path(Utf8ToWide(utf8));
}
#else
fs::path ToFsPath(const std::string& utf8) {
    return fs::path(utf8);
}
#endif

// Raw-deflate inflate with the C# DeflateStream semantics the compressed
// bundle entries need: decompress up to the final block (trailing bytes
// after it are ignored -- DeflateStream stops reading input there), yield
// the PARTIAL production when the input runs out mid-stream (DeflateStream
// returns what it decoded; the caller's declared-size check fires on the
// count), and throw for a corrupt stream (the C# DeflateStream
// InvalidDataException escaping DumpPackageAssemblies to its global
// catch). The growing-buffer strategy: the C# pre-sizes its MemoryStream to
// the declared size and grows on demand; the port starts there and re-runs
// with a doubled buffer when the stream produces more (only corrupt streams
// ever do, so the re-run cost stays bounded by the actual production).
std::vector<std::uint8_t> InflateRawDeflate(const std::uint8_t* src,
    std::size_t srcSize, long long declaredSize) {
    // The C# `new MemoryStream((int)entry.Size)` pre-size: an int cast, so
    // the port mirrors the wrap before the negativity check.
    std::int32_t preSize = static_cast<std::int32_t>(
        static_cast<std::uint32_t>(static_cast<std::uint64_t>(declaredSize)));
    if (preSize < 0)
        throw std::out_of_range("Negative MemoryStream capacity.");
    std::size_t capacity = preSize > 0 ? static_cast<std::size_t>(preSize) : 1;
    for (;;) {
        std::vector<std::uint8_t> out(capacity);
        size_t inSize = srcSize;
        size_t outSize = capacity;
        tinfl_decompressor decomp;
        tinfl_init(&decomp);
        tinfl_status status = tinfl_decompress(
            &decomp, src, &inSize, out.data(), out.data(), &outSize,
            TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if (status == TINFL_STATUS_DONE) {
            out.resize(outSize);
            return out;
        }
        if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
            // The stream produces more than the buffer holds: grow and
            // re-run (the C# MemoryStream grows transparently).
            capacity *= 2;
            continue;
        }
        if (status == TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS) {
            // The input ran out mid-stream: the truncated input the probed
            // DeflateStream partially decodes -- yield what was produced so
            // the caller's size check fires.
            out.resize(outSize);
            return out;
        }
        // A genuinely corrupt deflate stream: the C# DeflateStream throws
        // (probed: InvalidDataException "The archive entry was compressed
        // using an unsupported compression method." for a bad stored-block
        // NLEN); the port's exact message for other corruption shapes may
        // differ (the exit code the caller renders is the same).
        throw std::out_of_range(
            "The archive entry was compressed using an unsupported compression method.");
    }
}

// The whole package image as one buffer (the C# memory-maps the file; the
// port's whole-file read is the established convention). A missing or
// unreadable file throws -- the C# MemoryMappedFile.CreateFromFile
// FileNotFoundException / IOException escaping to the global catch.
std::vector<std::uint8_t> ReadPackageImage(const std::string& path) {
    std::ifstream f(ToFsPath(path), std::ios::binary);
    if (!f)
        throw std::runtime_error("Unable to find the specified file.");
    std::vector<std::uint8_t> bytes;
    f.seekg(0, std::ios::end);
    auto sz = f.tellg();
    if (sz < 0)
        throw std::runtime_error("Unable to find the specified file.");
    bytes.resize(static_cast<std::size_t>(sz));
    f.seekg(0);
    if (!bytes.empty()) {
        f.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        if (f.gcount() != static_cast<std::streamsize>(bytes.size()))
            throw std::runtime_error("Unable to read the package file.");
    }
    return bytes;
}

// The C# UnmanagedMemoryStream over the mapped view does NOT validate an
// entry's extent against the file (a slightly out-of-bounds entry reads
// whatever the last mapped page holds -- undefined bytes; a far-out one
// access-violates). The port's plain buffer cannot replicate that, and
// writing undefined bytes is worse than refusing: an entry outside the
// image throws. A documented divergence (the C# is undefined here, the port
// is deterministic).
void CheckEntryExtent(const Sfb::Entry& entry, long long extentSize, long long imageSize) {
    if (entry.Offset < 0 || extentSize < 0 || entry.Offset + extentSize > imageSize) {
        throw std::out_of_range("Bundle entry '" + entry.RelativePath
            + "' lies outside the package image.");
    }
}

}  // namespace

// The C# `IDebugInfoProvider TryLoadPDB(PEFile module)` (IlspyCmdProgram.cs):
// the InputPDBFile dispatch -- the bare form's PDB discovery, the valued
// form's explicit PDB, no flag no debug info.
std::unique_ptr<Decompiler::DebugInfo::IDebugInfoProvider> TryLoadPDB(
    const Decompiler::Metadata::MetadataFile& module,
    const InputPDBFile& pdbFile)
{
    if (pdbFile.IsSet) {
        if (!pdbFile.Value)
            return ILSpyX::PdbProvider::LoadSymbols(module);
        return ILSpyX::PdbProvider::FromFile(module, *pdbFile.Value);
    }
    return nullptr;
}

// The C# `int ShowIL(string assemblyFileName, TextWriter output)`
// (IlspyCmdProgram.cs): the header line straight to the writer, then the
// ReflectionDisassembler over a PlainTextOutput wrapping it, DebugInfo from
// TryLoadPDB and ShowSequencePoints from the --il-sequence-points flag,
// rendering WriteModuleContents. Returns 0 (the C# return value).
int ShowIL(const std::string& assemblyFileName, std::ostringstream& output,
    bool showILSequencePoints, const InputPDBFile& pdbFile)
{
    // The C# `var module = new PEFile(assemblyFileName)` -- the port's
    // never-throwing MetadataFile (the unparseable-file divergence the
    // header documents: the bare header line, no contents, rc 0).
    Decompiler::Metadata::MetadataFile module(assemblyFileName);
    // The C# `output.WriteLine($"// IL code: {module.Name}")` -- written
    // before the PlainTextOutput wraps the writer (Environment.NewLine --
    // the port's kNewLine "\r\n" convention).
    output << "// IL code: " << module.Name() << "\r\n";
    // The C# object initializer's property sets: DebugInfo then
    // ShowSequencePoints. The provider must outlive the disassembler's
    // WriteModuleContents call (the caller-owned raw pointer).
    std::unique_ptr<Decompiler::DebugInfo::IDebugInfoProvider> debugInfo =
        TryLoadPDB(module, pdbFile);
    Decompiler::Output::PlainTextOutput textOutput(output);
    Decompiler::Disassembler::ReflectionDisassembler disassembler(textOutput);
    disassembler.DebugInfo(debugInfo.get());
    disassembler.ShowSequencePoints(showILSequencePoints);
    disassembler.WriteModuleContents(module);
    return 0;
}

namespace {

using ILSpy::Decompiler::TypeSystem::TypeKind;

// The C# `$"{type.Kind} {type.FullTypeName.ReflectionName}"` interpolation:
// the TypeKind enum member name (Enum.ToString). Every ported kind is a
// named member, so the .NET decimal fallback for an unnamed value is
// unreachable.
const char* TypeKindName(TypeKind kind) {
    switch (kind) {
        case TypeKind::Other: return "Other";
        case TypeKind::Class: return "Class";
        case TypeKind::Interface: return "Interface";
        case TypeKind::Struct: return "Struct";
        case TypeKind::Delegate: return "Delegate";
        case TypeKind::Enum: return "Enum";
        case TypeKind::Void: return "Void";
        case TypeKind::Unknown: return "Unknown";
        case TypeKind::Null: return "Null";
        case TypeKind::None: return "None";
        case TypeKind::Dynamic: return "Dynamic";
        case TypeKind::UnboundTypeArgument: return "UnboundTypeArgument";
        case TypeKind::TypeParameter: return "TypeParameter";
        case TypeKind::Array: return "Array";
        case TypeKind::Pointer: return "Pointer";
        case TypeKind::ByReference: return "ByReference";
        case TypeKind::Intersection: return "Intersection";
        case TypeKind::ArgList: return "ArgList";
        case TypeKind::Tuple: return "Tuple";
        case TypeKind::ModOpt: return "ModOpt";
        case TypeKind::ModReq: return "ModReq";
        case TypeKind::NInt: return "NInt";
        case TypeKind::NUInt: return "NUInt";
        case TypeKind::FunctionPointer: return "FunctionPointer";
    }
    return "";  // unreachable: every enum member is named above
}

}  // namespace

// The C# `int ListContent(string assemblyFileName, TextWriter output,
// ISet<TypeKind> kinds)` (IlspyCmdProgram.cs): the -l/--list render --
// every type definition in the TypeDef table's row order whose kind is
// selected, as `{Kind} {ReflectionName}` lines.
int ListContent(const std::string& assemblyFileName, std::ostringstream& output,
    const std::set<ILSpy::Decompiler::TypeSystem::TypeKind>& kinds)
{
    // The C# `var decompiler = GetDecompiler(assemblyFileName)` +
    // `decompiler.TypeSystem.MainModule.TypeDefinitions`: MetadataModule
    // iterates metadata.TypeDefinitions -- the TypeDef table in row order
    // (<Module> included, nested types at their physical rows) -- the port's
    // TypeDefs() walk. The C# type-system wrapper contributes nothing to
    // this render beyond the metadata walk (no resolver is consulted).
    ILSpy::Decompiler::Metadata::MetadataFile module(assemblyFileName);
    for (const auto& t : module.TypeDefs()) {
        if (kinds.find(t.Kind) == kinds.end())
            continue;
        // The C# `output.WriteLine($"{type.Kind} {type.FullTypeName.ReflectionName}")`:
        // the TypeKind enum name, a space, the GetFullTypeName declaring-chain
        // reflection name (the `n arity suffix, the '+' nesting separators).
        // TextWriter.WriteLine uses Environment.NewLine -- the port's "\r\n"
        // convention (PlainTextOutput hardcodes the Windows value).
        output << TypeKindName(t.Kind) << ' '
              << ILSpy::Decompiler::Metadata::GetFullTypeNameFromDefinition(module, t.Token)
                     .ReflectionName()
              << "\r\n";
    }
    return 0;
}

// The C# `int ListResources(string assemblyFileName, TextWriter output)`
// (IlspyCmdProgram.cs): the --list-resources render.
int ListResources(const std::string& assemblyFileName, std::ostringstream& output)
{
    // The C# `var module = new PEFile(assemblyFileName)` then one
    // WriteLine per EnumerateResourcePaths path. The port's MetadataFile
    // never throws -- an unparseable file has no resources and renders
    // nothing (main.cpp gates IsValid before dispatching).
    ILSpy::Decompiler::Metadata::MetadataFile module(assemblyFileName);
    for (const auto& path : EnumerateResourcePaths(module))
        output << path << "\r\n";
    return 0;
}

namespace {

using ILSpy::Decompiler::Util::ResourceValue;

// The C# `new decimal(bits).ToString()` for the invariant culture: the
// 96-bit magnitude [lo, mid, hi] rendered in decimal digits with the point
// at `scale` digits from the right (the flags word's low byte) and the sign
// flag's leading '-'.
std::string DecimalToString(const std::uint32_t (&bits)[4]) {
    // The magnitude's decimal digits: repeated division by 10^9 over the
    // three 32-bit words, least-significant 9-digit group first.
    std::uint32_t words[3] = {bits[0], bits[1], bits[2]};
    std::string digits;
    do {
        std::uint64_t remainder = 0;
        for (int i = 2; i >= 0; i--) {
            std::uint64_t cur = (remainder << 32) | words[i];
            words[i] = static_cast<std::uint32_t>(cur / 1000000000u);
            remainder = cur % 1000000000u;
        }
        bool more = (words[0] | words[1] | words[2]) != 0;
        char group[16];
        if (more)
            std::snprintf(group, sizeof(group), "%09u",
                static_cast<unsigned>(remainder));
        else
            std::snprintf(group, sizeof(group), "%u",
                static_cast<unsigned>(remainder));
        digits = group + digits;
    } while ((words[0] | words[1] | words[2]) != 0);
    std::uint32_t scale = (bits[3] >> 16) & 0xFF;
    bool negative = (bits[3] & 0x80000000u) != 0;
    std::string s;
    if (scale == 0) {
        s = digits;
    } else if (digits.size() <= scale) {
        s = "0.";
        s.append(scale - digits.size(), '0');
        s += digits;
    } else {
        s = digits.substr(0, digits.size() - scale);
        s.push_back('.');
        s.append(digits, digits.size() - scale, std::string::npos);
    }
    if (negative)
        s.insert(s.begin(), '-');
    return s;
}

// The days-since-0001-01-01 to (year, month, day) decomposition: the
// proleptic-Gregorian civil algorithm (the 400/100/4/1-year cycle
// decomposition of DateTime.GetDatePart).
void CivilFromDays(std::int64_t dayNumber, std::int64_t& year,
    unsigned& month, unsigned& day) {
    // Rebase to days since 1970-01-01, then the standard civil algorithm.
    std::int64_t z = dayNumber - 719162;
    z += 719468;
    std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    std::int64_t doe = z - era * 146097;
    std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    year = yoe + era * 400;
    std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    std::int64_t mp = (5 * doy + 2) / 153;
    day = static_cast<unsigned>(doy - (153 * mp + 2) / 5 + 1);
    month = static_cast<unsigned>(mp + (mp < 10 ? 3 : -9));
    if (month <= 2)
        year += 1;
}

// The C# `DateTime.ToString()` under the invariant culture (the real
// tool's culture -- its runtimeconfig sets System.Globalization.Invariant):
// the "MM/dd/yyyy HH:mm:ss" form (the sub-second ticks are dropped).
std::string DateTimeToString(std::int64_t ticks) {
    constexpr std::int64_t kTicksPerDay = 864000000000LL;
    std::int64_t dayNumber = ticks / kTicksPerDay;
    std::int64_t secondsOfDay = ticks % kTicksPerDay / 10000000LL;
    std::int64_t year;
    unsigned month, day;
    CivilFromDays(dayNumber, year, month, day);
    int hour = static_cast<int>(secondsOfDay / 3600);
    int minute = static_cast<int>(secondsOfDay / 60 % 60);
    int second = static_cast<int>(secondsOfDay % 60);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%02u/%02u/%04lld %02d:%02d:%02d",
        month, day, static_cast<long long>(year), hour, minute, second);
    return buf;
}

// The C# `TimeSpan.ToString()`: the "c" constant format
// "[-][d.]hh:mm:ss[.fffffff]" (culture-independent; the full 7-digit
// fraction when nonzero -- no trailing-zero trim, no padding beyond 7).
std::string TimeSpanToString(std::int64_t ticks) {
    constexpr std::int64_t kTicksPerDay = 864000000000LL;
    constexpr std::int64_t kTicksPerHour = 36000000000LL;
    constexpr std::int64_t kTicksPerMinute = 600000000LL;
    constexpr std::int64_t kTicksPerSecond = 10000000LL;
    bool negative = ticks < 0;
    std::int64_t absTicks = negative ? -ticks : ticks;
    std::int64_t days = absTicks / kTicksPerDay;
    std::int64_t hours = absTicks % kTicksPerDay / kTicksPerHour;
    std::int64_t minutes = absTicks % kTicksPerHour / kTicksPerMinute;
    std::int64_t seconds = absTicks % kTicksPerMinute / kTicksPerSecond;
    std::int64_t fraction = absTicks % kTicksPerSecond;
    std::string s;
    if (negative)
        s.push_back('-');
    if (days != 0) {
        s += std::to_string(days);
        s.push_back('.');
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02lld:%02lld:%02lld",
        static_cast<long long>(hours), static_cast<long long>(minutes),
        static_cast<long long>(seconds));
    s += buf;
    if (fraction != 0) {
        char frac[16];
        std::snprintf(frac, sizeof(frac), ".%07lld",
            static_cast<long long>(fraction));
        s += frac;
    }
    return s;
}

// The C# `value?.ToString()` arm of ExtractResource: the invariant-culture
// render of every resource value kind. The float/double digits and
// notation are the .NET ToString() round-trip render (WriteOperand's
// FormatGeneral rule over the Ryu shortest digits -- the same digits and
// notation "R" produces, verified value-for-value against the real tool);
// the NaN and infinity spellings are the invariant culture's symbols.
std::string ResourceValueToText(const ResourceValue& value) {
    switch (value.kind) {
        case ResourceValue::Kind::Null:
            return std::string();
        case ResourceValue::Kind::String:
            return value.str;
        case ResourceValue::Kind::Boolean:
            return value.boolean ? "True" : "False";
        case ResourceValue::Kind::Char:
            return ILSpy::Decompiler::Util::Utf16ToUtf8(
                std::u16string(1, static_cast<char16_t>(value.character)));
        case ResourceValue::Kind::Byte:
        case ResourceValue::Kind::UInt16:
        case ResourceValue::Kind::UInt32:
        case ResourceValue::Kind::UInt64:
            return std::to_string(value.integer);
        case ResourceValue::Kind::SByte:
        case ResourceValue::Kind::Int16:
        case ResourceValue::Kind::Int32:
        case ResourceValue::Kind::Int64:
            // The signed kinds store the value's two's-complement bits in
            // the shared integer member.
            return std::to_string(
                static_cast<std::int64_t>(value.integer));
        case ResourceValue::Kind::Single:
        case ResourceValue::Kind::Double: {
            bool isDouble = value.kind == ResourceValue::Kind::Double;
            double d = isDouble ? value.doubleValue : value.single;
            if (std::isnan(d))
                return "NaN";
            if (std::isinf(d))
                return d > 0 ? "Infinity" : "-Infinity";
            // The finite render: WriteOperand drives the .NET
            // FormatGeneral round-trip rule over a PlainTextOutput.
            std::ostringstream text;
            ILSpy::Decompiler::Output::PlainTextOutput output(text);
            if (isDouble)
                ILSpy::Decompiler::Disassembler::WriteOperand(
                    output, value.doubleValue);
            else
                ILSpy::Decompiler::Disassembler::WriteOperand(
                    output, value.single);
            return text.str();
        }
        case ResourceValue::Kind::Decimal:
            return DecimalToString(value.decimalBits);
        case ResourceValue::Kind::DateTime:
            return DateTimeToString(value.ticks);
        case ResourceValue::Kind::TimeSpan:
            return TimeSpanToString(value.ticks);
        case ResourceValue::Kind::ByteArray:
        case ResourceValue::Kind::Stream:
        case ResourceValue::Kind::SerializedObject:
            // The byte kinds never reach the text arm (ExtractResource
            // writes them raw); a caller that renders one falls back to the
            // empty text (the C# byte[]/Stream ToString render is not
            // reachable through this arm either).
            return std::string();
    }
    return std::string();
}

// The C# `resourceName.EndsWith(".baml", StringComparison.OrdinalIgnoreCase)`
// over the fixed 5-char suffix.
bool EndsWithBamlCaseInsensitive(const std::string& name) {
    static constexpr const char* kSuffix = ".baml";
    if (name.size() < 5) return false;
    for (std::size_t i = 0; i < 5; i++) {
        char c = name[name.size() - 5 + i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (c != kSuffix[i]) return false;
    }
    return true;
}

}  // namespace

// The C# `int ExtractResource(string assemblyFileName, string resourceName,
// TextWriter output, string outputDirectory, CommandLineApplication app)`
// (IlspyCmdProgram.cs): the --resource extraction (the header contract has
// the full deferral/divergence notes). The C# reads the -r/--referencepath
// values off the static Options property; the port takes them as the
// referencePaths parameter (the caller main.cpp collects the option's
// occurrences). The two path helpers it composes are defined below, after
// the other -o path machinery (this file's definition order, not the C#
// class layout).
std::string FileNameOf(const std::string& path);
std::string CombinePaths(const std::string& dir, const std::string& name);
std::string FileNameWithoutExtensionOf(const std::string& path);

int ExtractResource(const std::string& assemblyFileName,
    const std::string& resourceName, std::ostringstream& output,
    std::ostringstream& errorOutput,
    const std::optional<std::string>& outputDirectory,
    const std::vector<std::string>& referencePaths) {
    ILSpy::Decompiler::Metadata::MetadataFile module(assemblyFileName);
    std::optional<ResourceValue> value = TryGetResource(module, resourceName);
    if (!value) {
        // The C# not-found arm: the two error lines plus the available-
        // resources listing (app.Error.WriteLine -- the "\r\n" TextWriter
        // convention), returning EX_DATAERR.
        errorOutput << "Resource '" << resourceName << "' not found.\r\n";
        errorOutput << "Available resources:\r\n";
        for (const auto& path : EnumerateResourcePaths(module))
            errorOutput << "  " << path << "\r\n";
        return 65;  // ProgramExitCodes.EX_DATAERR
    }

    // The C# `bool isBaml = resourceName.EndsWith(".baml", ...)` arm: with
    // a byte[] value the real tool runs the BamlDecompiler over a resolver
    // built for the assembly (throwOnError=false, DetectTargetFrameworkId)
    // with every -r reference path added, and the bamlSettings flag from
    // GetSettings -- which, without --ilspy-settingsfile (the option the
    // port does not register), is ALWAYS false (the C#
    // `ThrowOnAssemblyResolveErrors = false` initializer), so the resolver
    // degrades unresolvable references instead of throwing. With -o the
    // XAML saves under the '.xaml'-suffixed sanitized name; otherwise the
    // ToString render (no declaration, no trailing newline) goes to output.
    // A BamlReader rejection (e.g. "Invalid BAML signature length.")
    // propagates out -- the C# InvalidDataException reaches the global
    // catch (EX_SOFTWARE) with the stack trace the port omits.
    if (EndsWithBamlCaseInsensitive(resourceName)
        && value->kind == ResourceValue::Kind::ByteArray) {
        ILSpy::Decompiler::Metadata::UniversalAssemblyResolver resolver(
            assemblyFileName, false,
            // The C# CLI calls `module.Metadata.DetectTargetFrameworkId()` --
            // the MetadataReader overload with the DEFAULT null assemblyPath
            // (no path-pattern fallback), so an assembly with no detect
            // answer resolves to the empty string.
            ILSpy::Decompiler::Metadata::DetectTargetFrameworkId(
                module, std::nullopt));
        for (const auto& path : referencePaths)
            resolver.AddSearchDirectory(path);
        BamlDecompiler::BamlDecompilerSettings bamlSettings;
        // The C# GetSettings(module).ThrowOnAssemblyResolveErrors -- the
        // CLI's always-false default without --ilspy-settingsfile (the
        // DecompilerSettings initializer the C# OnExecute path builds).
        bamlSettings.SetThrowOnAssemblyResolveErrors(false);
        std::shared_ptr<Decompiler::Xml::XDocument> xaml =
            DecompileBaml(module, resolver, value->bytes.data(),
                value->bytes.size(), bamlSettings);
        if (outputDirectory.has_value()) {
            // The C# -o branch: `string xamlFile =
            // WholeProjectDecompiler.SanitizeFileName(
            // Path.GetFileNameWithoutExtension(resourceName) + ".xaml")`;
            // xaml.Save(Path.Combine(outputDirectory, xamlFile))` -- the
            // XML declaration + BOM render.
            std::string xamlFile = ProjectDecompiler::SanitizeFileName(
                FileNameWithoutExtensionOf(resourceName) + ".xaml");
            xaml->Save(CombinePaths(*outputDirectory, xamlFile));
            return 0;
        }
        // The C# `output.Write(xaml.ToString())` -- the XNode.ToString()
        // plain render.
        output << xaml->ToString();
        return 0;
    }

    if (value->kind == ResourceValue::Kind::ByteArray) {
        if (outputDirectory.has_value()) {
            // The C# -o branch: `string fileName =
            // WholeProjectDecompiler.SanitizeFileName(Path.GetFileName(
            // resourceName)); File.WriteAllBytes(Path.Combine(
            // outputDirectory, fileName), binary)` -- the bytes verbatim
            // (the WriteOutputFile write is byte-transparent).
            std::string fileName = ProjectDecompiler::SanitizeFileName(
                FileNameOf(resourceName));
            WriteOutputFile(CombinePaths(*outputDirectory, fileName),
                std::string(value->bytes.begin(), value->bytes.end()));
            return 0;
        }
        // The C# `stdout = Console.OpenStandardOutput(); stdout.Write(...)`
        // binary write -- the port's output block (the ostringstream holds
        // arbitrary bytes; main.cpp writes them in binary mode).
        output.write(
            reinterpret_cast<const char*>(value->bytes.data()),
            static_cast<std::streamsize>(value->bytes.size()));
        return 0;
    }

    // The C# `string text = value as string ?? value?.ToString() ??
    // string.Empty`: the invariant-culture text render.
    std::string text = ResourceValueToText(*value);
    if (outputDirectory.has_value()) {
        // The C# -o branch: `string fileName =
        // WholeProjectDecompiler.SanitizeFileName(Path.GetFileName(
        // resourceName)); File.WriteAllText(Path.Combine(
        // outputDirectory, fileName), text)` -- UTF-8 without a BOM, the
        // text verbatim (WriteOutputFile).
        std::string fileName = ProjectDecompiler::SanitizeFileName(
            FileNameOf(resourceName));
        WriteOutputFile(CombinePaths(*outputDirectory, fileName), text);
        return 0;
    }
    // The C# `output.Write(text)` -- no trailing newline.
    output << text;
    return 0;
}

std::optional<std::string> ResolveOutputDirectory(const std::string& outputDirectory) {
    // The C# `string.IsNullOrWhiteSpace(outputDirectory)` arm: no value
    // means the actions write to standard out.
    bool whitespace = true;
    for (char c : outputDirectory) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\v' && c != '\f') {
            whitespace = false;
            break;
        }
    }
    if (whitespace)
        return std::nullopt;
    // The .NET Path.GetFullPath shape: resolve against the current
    // directory and normalize the '.'/'..' components. The port's
    // fs::absolute + lexically_normal pair (no '~' expansion -- GetFullPath
    // has none on Windows either).
    fs::path resolved = fs::absolute(ToFsPath(outputDirectory)).lexically_normal();
    // The UTF-8 std::string convention: on Windows the resolved path is
    // wide and .string() would transcode through the ANSI code page
    // (mangling non-ASCII names, or throwing when the page cannot
    // represent them); the explicit UTF-8 conversion keeps the -o value
    // usable by the output-file composition below (whose ToFsPath write
    // converts back).
#if defined(_WIN32)
    return WideToUtf8(resolved.native());
#else
    return resolved.string();
#endif
}

std::filesystem::path ToNativePath(const std::string& utf8) {
    // The OS-boundary translation for the CLI's UTF-8 paths: the C#
    // composes Unicode paths end to end (System.IO), so a non-ASCII -o
    // directory or output name must survive -- on Windows the fs::path
    // narrow ctor would transcode through the ANSI code page instead.
    return ToFsPath(utf8);
}

// The C# `Path.GetFileName(path)` (the -o extraction branches' file-name
// source). The .NET 10 Windows shape (decompiled from System.Private.CoreLib:
// Path.GetFileName slices at max(rootLength, lastSeparator + 1)) needs the
// full PathInternal.GetRootLength -- the DOS drive roots 'X:'/'X:\\',
// the UNC roots '\\\\server\\share\\', and the device roots '\\\\?\\' --
// so a separator inside a root yields the remainder past the root (an
// incomplete UNC like '\\\\C:foo' is ALL root: the root scan runs past the
// end and the name is empty). The plain cases: 'C:foo' -> 'foo', 'a:b'
// -> 'b', '1:foo' and ':foo' stay whole (only a-z/A-Z are drive chars),
// 'a\\b:c' -> 'b:c' (an embedded ':' never splits), and a path ending in
// a separator yields the empty name. (POSIX has no volume separator and
// only '/'-roots, so the non-Windows port arm splits at the last
// separator -- the port's standing path convention.)
#if defined(_WIN32)
namespace {

// The decompiled System.IO.PathInternal members (the Windows build):
// IsDirectorySeparator, IsValidDriveChar, IsDevice, IsDeviceUNC and
// GetRootLength, verbatim in shape.
bool WinIsDirectorySeparator(char c) {
    return c == '\\' || c == '/';
}

bool WinIsValidDriveChar(char value) {
    // The C# `(uint)((value | 0x20) - 97) <= 25u`: the ASCII letters, both
    // cases.
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

int WinGetRootLength(std::string_view path) {
    std::size_t length = path.size();
    std::size_t i = 0;
    bool isDevice = WinIsDevice(path);
    bool isDeviceUnc = isDevice && WinIsDeviceUnc(path);
    if ((!isDevice || isDeviceUnc) && length > 0
        && WinIsDirectorySeparator(path[0]))
    {
        if (isDeviceUnc || (length > 1 && WinIsDirectorySeparator(path[1])))
        {
            // The UNC root: skip the leading pair, then consume the server
            // and share up to (and including) the second separator.
            i = isDeviceUnc ? 8 : 2;
            int num = 2;
            for (; i < length; i++)
            {
                if (WinIsDirectorySeparator(path[i]) && --num <= 0)
                    break;
            }
        }
        else
        {
            // A single leading separator: a rooted relative path.
            i = 1;
        }
    }
    else if (isDevice)
    {
        // A device path: skip the '\\\\?\\' prefix, then consume the drive
        // (or volume) up to its separator, inclusive.
        for (i = 4; i < length && !WinIsDirectorySeparator(path[i]); i++)
        {
        }
        if (i < length && i > 4 && WinIsDirectorySeparator(path[i]))
            i++;
    }
    else if (length >= 2 && path[1] == ':' && WinIsValidDriveChar(path[0]))
    {
        // A DOS drive root, with or without its trailing separator.
        i = 2;
        if (length > 2 && WinIsDirectorySeparator(path[2]))
            i++;
    }
    return static_cast<int>(i);
}

}  // namespace
#endif

std::string FileNameOf(const std::string& path) {
#if defined(_WIN32)
    int root = WinGetRootLength(path);
    std::size_t num = path.find_last_of("\\/");
    int sep = num == std::string::npos ? -1 : static_cast<int>(num);
    return path.substr(static_cast<std::size_t>(sep < root ? root : sep + 1));
#else
    std::size_t sep = path.find_last_of("\\/");
    return sep == std::string::npos ? path : path.substr(sep + 1);
#endif
}

// The C# `Path.GetFileNameWithoutExtension(path)` (the -o writer
// branches' output-name source): the file-name component (after the last
// separator, both separators) minus everything from its LAST '.' -- a
// trailing dot drops the dot ("foo." -> "foo"), a leading dot yields the
// empty name (".dll" -> ""), a component with no dot keeps its whole
// self. The same helper exists file-locally in DebugInfoUtils.cpp (the
// adjacent-PDB name); the -o composition is a second, independent
// consumer.
std::string FileNameWithoutExtensionOf(const std::string& path) {
    std::size_t sep = path.find_last_of("\\/");
    std::string name = sep == std::string::npos ? path : path.substr(sep + 1);
    std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

// The C# `Path.Combine(dir, name)` (the probed .NET shape): an empty name
// yields the directory itself, an empty dir the name; a dir ending in a
// directory separator concatenates directly (a bare drive like "C:" does
// NOT terminate -- .NET inserts the separator); anything else inserts
// the platform's own separator.
std::string CombinePaths(const std::string& dir, const std::string& name) {
    if (name.empty()) return dir;
    if (dir.empty()) return name;
    char last = dir[dir.size() - 1];
    if (last == '\\' || last == '/') return dir + name;
#if defined(_WIN32)
    constexpr const char* kSeparator = "\\";
#else
    constexpr const char* kSeparator = "/";
#endif
    return dir + kSeparator + name;
}

std::string OutputFilePath(const std::string& outputDirectory,
    const std::string& assemblyFileName, const std::string& extension) {
    // The C# `Path.Combine(outputDirectory, outputName) + extension` with
    // `outputName = Path.GetFileNameWithoutExtension(fileName)`: the
    // input's base name joined under the -o directory, the extension
    // string-concatenated onto the combined result (an empty outputName
    // makes the combine return the directory itself).
    return CombinePaths(outputDirectory,
        FileNameWithoutExtensionOf(assemblyFileName)) + extension;
}

std::string DecompiledOutputFilePath(const std::string& outputDirectory,
    const std::string& assemblyFileName, const std::string& typeName) {
    // The C# `-o` writer branch for the decompile path (lines 406-411):
    // the base is the assembly's name without -t, or the TYPE NAME
    // VERBATIM with -t (a type name's dots are part of the name -- only
    // the assembly path goes through GetFileNameWithoutExtension).
    return CombinePaths(outputDirectory,
               typeName.empty()
                   ? FileNameWithoutExtensionOf(assemblyFileName)
                   : typeName)
        + ".decompiled.cs";
}

void WriteOutputFile(const std::string& path, const std::string& contents) {
    // The C# `File.CreateText(path)`: create/truncate and a UTF-8-no-BOM
    // StreamWriter (the rendered text already carries its CRLFs, so the
    // bytes go out verbatim); the finally `output.Close()` flushes -- the
    // port writes the finished buffer at once instead. The write goes
    // through the UTF-16 path so a non-ASCII output directory resolves
    // correctly on Windows (the ToFsPath convention).
    std::ofstream fileStream(ToFsPath(path),
        std::ios::binary | std::ios::trunc);
    if (!fileStream) {
        // The C# File.CreateText IOException escaping to the global catch
        // (EX_SOFTWARE): the port renders the message only.
        throw std::runtime_error("Cannot create the output file '" + path + "'.");
    }
    if (!contents.empty()) {
        fileStream.write(contents.data(),
            static_cast<std::streamsize>(contents.size()));
    }
}

int DumpPackage(const std::string& packageFileName,
    const std::optional<std::string>& outputDirectory,
    std::ostringstream& errorOutput) {
    std::vector<std::uint8_t> image = ReadPackageImage(packageFileName);
    const long long imageSize = static_cast<long long>(image.size());

    long long bundleHeaderOffset = 0;
    if (!Sfb::IsBundle(image.data(), imageSize, bundleHeaderOffset)) {
        // The exact C# line, including the "assembiles" misspelling of the
        // real source (IlspyCmdProgram.cs DumpPackageAssemblies).
        errorOutput << "Cannot dump assembiles for " << packageFileName
                    << ", because it is not a single file bundle.\r\n";
        return 65;  // ProgramExitCodes.EX_DATAERR
    }

    Sfb::Header manifest = Sfb::ReadManifest(image.data(), imageSize, bundleHeaderOffset);
    for (const Sfb::Entry& entry : manifest.Entries) {
        // The C# traversal guard: a "../" component after the
        // backslash-to-slash normalization, or a rooted RelativePath.
        std::string normalized = entry.RelativePath;
        for (char& c : normalized) {
            if (c == '\\')
                c = '/';
        }
        if (normalized.find("../") != std::string::npos || IsPathRooted(entry.RelativePath)) {
            errorOutput << "Skipping single-file entry '" << entry.RelativePath
                        << "' because it might refer to a location outside of"
                        " the bundle output directory.\r\n";
            continue;
        }

        std::vector<std::uint8_t> contents;
        if (entry.CompressedSize == 0) {
            CheckEntryExtent(entry, entry.Size, imageSize);
            contents.assign(image.begin() + entry.Offset,
                image.begin() + entry.Offset + entry.Size);
        } else {
            CheckEntryExtent(entry, entry.CompressedSize, imageSize);
            contents = InflateRawDeflate(image.data() + entry.Offset,
                static_cast<std::size_t>(entry.CompressedSize), entry.Size);
            if (static_cast<long long>(contents.size()) != entry.Size) {
                errorOutput << "Corrupted single-file entry '" << entry.RelativePath
                            << "'. Declared decompressed size '" << entry.Size
                            << "' is not the same as actual decompressed size '"
                            << contents.size() << "'.\r\n";
                return 65;  // ProgramExitCodes.EX_DATAERR
            }
        }

        // The C# `Path.Combine(outputDirectory, entry.RelativePath)` -- with
        // no -o value this is the ArgumentNullException escaping to the
        // global catch (rendered with EX_SOFTWARE there).
        if (!outputDirectory.has_value())
            throw std::invalid_argument("Value cannot be null. (Parameter 'path1')");
        fs::path target = ToFsPath(*outputDirectory) / ToFsPath(entry.RelativePath);

        // The C# `Directory.CreateDirectory(Path.GetDirectoryName(target))`
        // -- every intermediate directory up to the entry's own.
        std::error_code ec;
        fs::create_directories(target.parent_path(), ec);
        if (ec) {
            throw std::runtime_error("Cannot create the output directory for '"
                + entry.RelativePath + "': " + ec.message());
        }
        std::ofstream fileStream(target, std::ios::binary | std::ios::trunc);
        if (!fileStream) {
            // The C# File.Create failure (an IOException for a locked path,
            // a trailing ".." pair resolving onto a directory, ...) escapes
            // to the global catch with EX_SOFTWARE; the port renders the
            // message with the same code.
            throw std::runtime_error("Cannot create the output file '"
                + entry.RelativePath + "'.");
        }
        if (!contents.empty()) {
            fileStream.write(reinterpret_cast<const char*>(contents.data()),
                static_cast<std::streamsize>(contents.size()));
        }
    }

    return 0;
}

CliOpenFailure ClassifyCliOpenFailure(const std::string& path) {
    // The C# RunAsync's pre-command argument validation: a missing path
    // prints the usage hint to stdout and the validation error to stderr,
    // exit code 1.
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {"File '" + path + "' does not exist!",
            "Specify --help for a list of available options and commands.",
            1};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        // An unreadable existing file: no C# shape is reachable here (the
        // managed engine would throw its own IO exception); keep the
        // caller's pre-existing diagnostic and exit code.
        return {"ilspycmd: could not open '" + path
                + "' as a CLI assembly",
            {}, 1};
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
    try {
        ILSpy::Decompiler::Metadata::MetadataBlock block =
            ILSpy::Decompiler::Metadata::ParsePEReaderHeaders(bytes);
        if (block.size == 0) {
            // The C# MetadataFile constructor's throw for a valid image
            // without a CLI directory -- the exception that escapes the
            // PEReader catch arm.
            return {"ICSharpCode.Decompiler.Metadata."
                    "MetadataFileNotSupportedException: PE file does not "
                    "contain any managed metadata.",
                {}, 70};
        }
        // A valid CLI image whose metadata tables the port could not parse:
        // the PEReaderParse.hpp documented representative (the port cannot
        // classify the corruption through the never-throwing constructor).
        return {"System.OverflowException: Arithmetic operation resulted in "
                "an overflow.",
            {}, 70};
    } catch (const std::invalid_argument& ex) {
        // The C# PEReader eager parse: BadImageFormatException with the SRM
        // message for every malformed-image arm.
        return {std::string("System.BadImageFormatException: ") + ex.what(),
            {}, 70};
    }
}

}  // namespace ILSpy::ILSpyCmd
