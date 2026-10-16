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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The `AssemblyNameReference::Parse` / `FullName` implementation, pinned against
// the real .NET classes (the C:\temp-probe\AnrProbe parse/render matrix driven
// over the installed ilspycmd 11.0 tool's ICSharpCode.Decompiler.dll). The
// quirk inventory the matrix pins:
//   * the first ','-token (trimmed) is the Name -- it may be EMPTY (", Version=..."
//     parses with Name "");
//   * every later token must split on '=' into EXACTLY two parts, else
//     "Malformed name" (a trailing ',' yields an empty token that fails this);
//   * unknown keys and keys with inner whitespace ("Version = ...") are silently
//     ignored -- the key match is on the exact case-folded text;
//   * the publickeytoken value "null" keeps the token null; otherwise
//     `new byte[len / 2]` DROPS an odd trailing character and each two-character
//     slice parses as one hex byte (whitespace-tolerant, sign-rejecting);
//   * `FullName` renders ToString(fieldCount: 4) over the version, which THROWS
//     the "Argument must be between 0 and 2/3." ArgumentException when the
//     parsed version carries fewer than four components.

#include "Decompiler/Metadata/AssemblyNameReference.hpp"
#include "Decompiler/Disassembler/ReflectionAttributes.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/Util/Char.hpp"
#include "Decompiler/Util/Sha1ForNonSecretPurposes.hpp"
#include "Decompiler/Util/Utf.hpp"
#include "Decompiler/TypeSystem/Version.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The C# `token.Trim()`: the char.IsWhiteSpace units removed from both ends.
// The UTF-8 string decodes to UTF-16 (the .NET string form) so the whitespace
// set matches char.IsWhiteSpace over code units exactly.
std::string Trim(const std::string& s)
{
    std::u16string u16 = Util::Utf8ToUtf16(s);
    std::size_t begin = 0;
    std::size_t end = u16.size();
    while (begin < end && Util::IsWhiteSpace(u16[begin]))
        begin++;
    while (end > begin && Util::IsWhiteSpace(u16[end - 1]))
        end--;
    return Util::Utf16ToUtf8(u16.substr(begin, end - begin));
}

// The C# `parts[0].ToLowerInvariant()` key fold. The ASCII fold follows the
// `StringComparer::OrdinalIgnoreCase` convention; the one non-ASCII unit whose
// invariant lowercase is an ASCII letter (U+212A KELVIN SIGN -> 'k') would fold
// a crafted key like "public\u212Aeytoken" onto "publickeytoken" in the C# --
// unreachable with real assembly names (documented divergence).
std::string ToLowerInvariantAscii(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(c >= 'A' && c <= 'Z'
                          ? static_cast<char>(c - 'A' + 'a')
                          : static_cast<char>(c));
    }
    return out;
}

// `Byte.Parse(substring, NumberStyles.HexNumber)` over the two-character slice
// the C# feeds it: leading/trailing whitespace (the char.IsWhiteSpace units),
// then hex digits -- a sign is NOT allowed (probed: "+01"/"-0" are the
// FormatException quoting the RAW substring).
std::uint8_t ParseHexByte(const std::string& substring)
{
    std::u16string u16 = Util::Utf8ToUtf16(substring);
    std::size_t begin = 0;
    std::size_t end = u16.size();
    while (begin < end && Util::IsWhiteSpace(u16[begin]))
        begin++;
    while (end > begin && Util::IsWhiteSpace(u16[end - 1]))
        end--;
    if (begin == end)
        throw std::invalid_argument(
            "The input string '" + substring + "' was not in a correct format.");
    int value = 0;
    for (std::size_t i = begin; i < end; i++) {
        char16_t c = u16[i];
        int digit;
        if (c >= u'0' && c <= u'9')
            digit = c - u'0';
        else if (c >= u'a' && c <= u'f')
            digit = c - u'a' + 10;
        else if (c >= u'A' && c <= u'F')
            digit = c - u'A' + 10;
        else
            throw std::invalid_argument(
                "The input string '" + substring + "' was not in a correct format.");
        // The substring is always exactly two characters, so the accumulated
        // value cannot exceed a byte.
        value = value * 16 + digit;
    }
    return static_cast<std::uint8_t>(value);
}

// `token.Split('=')` -- every '='-separated part, INCLUDING empty ones.
std::vector<std::string> SplitOnChar(const std::string& s, char separator)
{
    std::vector<std::string> parts;
    std::size_t pos = 0;
    for (;;) {
        std::size_t sep = s.find(separator, pos);
        parts.push_back(s.substr(pos, sep == std::string::npos ? std::string::npos : sep - pos));
        if (sep == std::string::npos)
            break;
        pos = sep + 1;
    }
    return parts;
}

} // namespace

AssemblyNameReference AssemblyNameReference::Parse(const std::string& fullName)
{
    if (fullName.empty())
        throw std::invalid_argument("Name can not be empty");

    AssemblyNameReference name;
    // `fullName.Split(',')` -- every ','-separated entry in order, including
    // empty ones; the loop below walks them without materializing the array.
    std::size_t pos = 0;
    int i = 0;
    for (;;) {
        std::size_t comma = fullName.find(',', pos);
        std::string token = fullName.substr(
            pos, comma == std::string::npos ? std::string::npos : comma - pos);
        token = Trim(token);

        if (i == 0) {
            name.name_ = token;
        } else {
            std::vector<std::string> parts = SplitOnChar(token, '=');
            if (parts.size() != 2)
                throw std::invalid_argument("Malformed name");
            std::string key = ToLowerInvariantAscii(parts[0]);
            if (key == "version") {
                name.version_ = TypeSystem::Version(parts[1]);
            } else if (key == "culture") {
                name.culture_ = parts[1] == "neutral" ? std::string() : parts[1];
            } else if (key == "publickeytoken") {
                const std::string& pk_token = parts[1];
                if (pk_token != "null") {
                    // `new byte[pk_token.Length / 2]` -- the integer division
                    // drops an odd trailing character.
                    std::vector<std::uint8_t> bytes(pk_token.size() / 2);
                    for (std::size_t j = 0; j < bytes.size(); j++)
                        bytes[j] = ParseHexByte(pk_token.substr(j * 2, 2));
                    name.publicKeyToken_ = std::move(bytes);
                }
            }
        }

        if (comma == std::string::npos)
            break;
        pos = comma + 1;
        i++;
    }
    return name;
}

std::string AssemblyNameReference::FullName() const
{
    if (fullName_.has_value())
        return *fullName_;

    const std::string sep = ", ";
    std::string builder;
    builder += name_;
    builder += sep;
    builder += "Version=";
    // `(Version ?? UniversalAssemblyResolver.ZeroVersion).ToString(fieldCount: 4)`
    // -- the four-component render, which throws the Version ArgumentException
    // when the parsed version specified fewer than four components.
    builder += version_.value_or(TypeSystem::Version(0, 0, 0, 0)).ToString(4);
    builder += sep;
    builder += "Culture=";
    builder += (culture_.has_value() && !culture_->empty()) ? *culture_ : "neutral";
    builder += sep;
    builder += "PublicKeyToken=";
    if (publicKeyToken_.has_value() && !publicKeyToken_->empty()) {
        // `pk_token[i].ToString("x2")` -- two lowercase hex digits per byte.
        static const char hexDigits[] = "0123456789abcdef";
        for (std::uint8_t b : *publicKeyToken_) {
            builder += hexDigits[b >> 4];
            builder += hexDigits[b & 0xF];
        }
    } else {
        builder += "null";
    }
    if (isRetargetable_) {
        builder += sep;
        builder += "Retargetable=Yes";
    }
    fullName_ = builder;
    return builder;
}

std::string AssemblyNameReference::ToString() const
{
    return FullName();
}

// --- AssemblyReference (the metadata-backed row wrapper) ---

namespace {

// The AssemblyRef table's column indexes in winmd's merged-version layout
// (the same layout the iteration-63 GetFullAssemblyName reads): the merged
// 8-byte version column, Flags, PublicKeyOrToken, Name, Culture.
constexpr std::uint32_t kAssemblyRefFlagsColumn = 1;
constexpr std::uint32_t kAssemblyRefPublicKeyOrTokenColumn = 2;
constexpr std::uint32_t kAssemblyRefNameColumn = 3;
constexpr std::uint32_t kAssemblyRefCultureColumn = 4;

// The C# `$"AR:{Handle}"` fallback (convention (b)): the SRM handle struct's
// ToString is its TYPE NAME (no override), so the interpolation renders the
// fully-qualified handle type.
constexpr const char* kHandleToString =
    "System.Reflection.Metadata.AssemblyReferenceHandle";

} // namespace

AssemblyReference::AssemblyReference(const MetadataFile& file,
                                     std::uint32_t token)
    : file_(&file), token_(token)
{
}

std::string AssemblyReference::Name() const
{
    // The C# LazyInit.VolatileRead + the raw read + the catch fallback.
    if (name_.has_value())
        return *name_;
    try
    {
        std::uint32_t row = (token_ & 0x00FFFFFFu) - 1;
        name_ = file_->CorString(file_->CorTableColumnValue(
            CorTableIndex::AssemblyRef, row,
            kAssemblyRefNameColumn));
    }
    catch (const std::invalid_argument&)
    {
        // The C# `catch (BadImageFormatException) { name = $"AR:{Handle}"; }`
        // -- the raw-surface exception family is the C# analog (the
        // iteration-63 convention).
        name_ = std::string("AR:") + kHandleToString;
    }
    catch (const std::out_of_range&)
    {
        name_ = std::string("AR:") + kHandleToString;
    }
    return *name_;
}

std::string AssemblyReference::FullName() const
{
    if (fullName_.has_value())
        return *fullName_;
    try
    {
        // The C# `entry.GetFullAssemblyName(Metadata)` -- the AssemblyRef-row
        // extension (the iteration-63 port).
        fullName_ = GetFullAssemblyName(*file_, token_);
    }
    catch (const std::invalid_argument&)
    {
        // The C# `catch (BadImageFormatException) { fullName =
        // $"fullname(AR:{Handle})"; }`.
        fullName_ = std::string("fullname(AR:") + kHandleToString + ")";
    }
    catch (const std::out_of_range&)
    {
        fullName_ = std::string("fullname(AR:") + kHandleToString + ")";
    }
    return *fullName_;
}

std::optional<TypeSystem::Version> AssemblyReference::Version() const
{
    // The C# `Version? Version => entry.Version` -- the merged 8-byte
    // version column as the four components (convention (d); the raw
    // fixed-width read has no corrupt-heap arm). A nil row takes the same
    // underflow path as Name (the raw bounds throw), so the always-engaged
    // optional only surfaces for a real row.
    std::uint32_t row = (token_ & 0x00FFFFFFu) - 1;
    MetadataFile::CorTableVersion version = file_->CorTableVersionValue(
        CorTableIndex::AssemblyRef, row);
    return TypeSystem::Version(version.MajorVersion, version.MinorVersion,
                               version.BuildNumber, version.RevisionNumber);
}

std::optional<std::string> AssemblyReference::Culture() const
{
    // The C# `string Culture => Metadata.GetString(entry.Culture)` -- the raw
    // column string; the nil offset 0 reads as "" (convention (d)), never
    // null.
    std::uint32_t row = (token_ & 0x00FFFFFFu) - 1;
    return file_->CorString(file_->CorTableColumnValue(
        CorTableIndex::AssemblyRef, row,
        kAssemblyRefCultureColumn));
}

bool AssemblyReference::IsWindowsRuntime() const
{
    std::uint32_t row = (token_ & 0x00FFFFFFu) - 1;
    std::uint32_t flags = file_->CorTableColumnValue(
        CorTableIndex::AssemblyRef, row,
        kAssemblyRefFlagsColumn);
    return (flags & static_cast<std::uint32_t>(
                        Disassembler::AssemblyAttributes::WindowsRuntime)) != 0;
}

bool AssemblyReference::IsRetargetable() const
{
    std::uint32_t row = (token_ & 0x00FFFFFFu) - 1;
    std::uint32_t flags = file_->CorTableColumnValue(
        CorTableIndex::AssemblyRef, row,
        kAssemblyRefFlagsColumn);
    return (flags & static_cast<std::uint32_t>(
                        Disassembler::AssemblyAttributes::Retargetable)) != 0;
}

std::optional<std::vector<std::uint8_t>>
AssemblyReference::GetPublicKeyToken() const
{
    // The C# nil check comes FIRST (before the cache check): a nil column
    // returns null WITHOUT caching, re-reading the column on every call
    // (convention (c): no throw, no cache).
    std::uint32_t row = (token_ & 0x00FFFFFFu) - 1;
    std::uint32_t keyOffset = file_->CorTableColumnValue(
        CorTableIndex::AssemblyRef, row,
        kAssemblyRefPublicKeyOrTokenColumn);
    if (keyOffset == 0)
    {
        // The C# `if (entry.PublicKeyOrToken.IsNil) return null;`.
        return std::nullopt;
    }
    // The C# lazy cache: `if (publicKeyToken == null) { ... }` -- the FIELD
    // null-ness, not the value (the port's optional engaged state).
    if (publicKeyToken_.has_value())
        return *publicKeyToken_;
    std::vector<std::uint8_t> bytes = file_->CorBlob(keyOffset);
    std::uint32_t flags = file_->CorTableColumnValue(
        CorTableIndex::AssemblyRef, row,
        kAssemblyRefFlagsColumn);
    if ((flags & static_cast<std::uint32_t>(
                    Disassembler::AssemblyAttributes::PublicKey)) != 0)
    {
        // The C# PublicKey-flag arm: `Sha1ForNonSecretPurposes.HashData(bytes,
        // hash)` over the 20-byte digest, then `hash.Skip(12).ToArray()` --
        // the digest's LAST 8 bytes.
        std::array<std::uint8_t, 20> digest{};
        Util::Sha1ForNonSecretPurposes::HashData(
            Util::Span<const std::uint8_t>(bytes.data(), bytes.size()),
            Util::Span<std::uint8_t>(digest.data(), digest.size()));
        bytes.assign(digest.begin() + 12, digest.end());
    }
    publicKeyToken_ = std::move(bytes);
    return publicKeyToken_;
}

// ---------------------------------------------------------------------------
// ResolutionException and AssemblyReferenceClassifier (the
// UniversalAssemblyResolver prerequisites).

// The interpolation of the reference parameter in the ctor-1 message: the
// `AssemblyNameReference.ToString()` override IS its `FullName`, so the
// render routes through the interface's `FullName()` (the divergence note on
// the class).
ResolutionException::ResolutionException(const IAssemblyReference* reference,
    const std::optional<std::string>& resolvedPath)
    : std::runtime_error("Failed to resolve assembly: '"
          + (reference != nullptr ? reference->FullName() : std::string())
          + "'\r\nResolve result: "
          + (resolvedPath ? *resolvedPath : std::string("<not found>"))),
      reference_(reference),
      resolvedFullPath_(resolvedPath) {
    // The C# null guard (`this.Reference = reference ?? throw new
    // ArgumentNullException(nameof(reference))`) runs AFTER the base ctor --
    // the message is never observable for a null reference, so the guard
    // here throws the same observable exception.
    if (reference == nullptr) {
        throw std::invalid_argument("Value cannot be null. (Parameter 'reference')");
    }
}

ResolutionException::ResolutionException(const std::string& mainModule,
    const std::string& moduleName, const std::optional<std::string>& resolvedPath)
    : std::runtime_error("Failed to resolve module: '" + moduleName + " of " + mainModule
          + "'\r\nResolve result: "
          + (resolvedPath ? *resolvedPath : std::string("<not found>"))),
      moduleName_(moduleName),
      mainModuleFullPath_(mainModule),
      resolvedFullPath_(resolvedPath) {
    // The C# `mainModule ?? throw ...` / `moduleName ?? throw ...` guards:
    // structurally unreachable through `std::string` parameters (the class
    // divergence note).
}

bool AssemblyReferenceClassifier::IsGacAssembly(
    const IAssemblyReference& reference) const {
    return UniversalAssemblyResolver::GetAssemblyInGac(reference).has_value();
}

} // namespace ILSpy::Decompiler::Metadata
