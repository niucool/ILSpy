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

#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"
#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "Decompiler/Util/ResourcesFile.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "ILSpyX/PdbProvider/DebugInfoUtils.hpp"
#include "ILSpyCmd/ResourceExtensions.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

namespace ILSpy::ILSpyCmd {

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
// the full deferral/divergence notes).
int ExtractResource(const std::string& assemblyFileName,
    const std::string& resourceName, std::ostringstream& output,
    std::ostringstream& errorOutput) {
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
    // a byte[] value the real tool runs the BamlDecompiler (decompiling to
    // XAML); the port's BamlDecompiler is the deferred Phase-9 piece, so
    // the arm fails loudly instead of silently writing raw bytes.
    if (EndsWithBamlCaseInsensitive(resourceName)
        && value->kind == ResourceValue::Kind::ByteArray) {
        errorOutput << "BAML resource decompilation ('" << resourceName
                    << "') is not supported by the C++ port yet"
                    << " (the BamlDecompiler port is pending).\r\n";
        return 70;  // ProgramExitCodes.EX_SOFTWARE
    }

    if (value->kind == ResourceValue::Kind::ByteArray) {
        // The C# `stdout = Console.OpenStandardOutput(); stdout.Write(...)`
        // binary write -- the port's output block (the ostringstream holds
        // arbitrary bytes; main.cpp writes them in binary mode).
        output.write(
            reinterpret_cast<const char*>(value->bytes.data()),
            static_cast<std::streamsize>(value->bytes.size()));
        return 0;
    }

    // The C# `string text = value as string ?? value?.ToString() ??
    // string.Empty; output.Write(text)` -- no trailing newline.
    output << ResourceValueToText(*value);
    return 0;
}

}  // namespace ILSpy::ILSpyCmd
