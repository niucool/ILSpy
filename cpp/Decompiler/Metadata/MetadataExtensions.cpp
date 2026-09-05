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

#include "Decompiler/Metadata/MetadataExtensions.hpp"

#include "Decompiler/Disassembler/DisassemblerHelpers.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/TypeSystem/Version.hpp"
#include "Decompiler/Util/Sha1ForNonSecretPurposes.hpp"

#include <algorithm>
#include <functional>

namespace ILSpy::Decompiler::Metadata {

using Disassembler::Escape;
using TypeSystem::FullTypeName;
using TypeSystem::KnownTypeCode;

namespace {

// The C# `ToHexString(this IEnumerable<byte> bytes, int estimatedLength)`
// (MetadataExtensions.cs): each byte rendered "{0:x2}" (lowercase hex). The
// estimatedLength is only a StringBuilder capacity hint; the port renders
// straight into the result string.
std::string ToHexLower(const std::uint8_t* bytes, std::size_t count)
{
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(count * 2);
    for (std::size_t i = 0; i < count; i++)
    {
        result += digits[bytes[i] >> 4];
        result += digits[bytes[i] & 0xF];
    }
    return result;
}

// The Assembly table (0x20) columns in winmd's layout (schema.h reader::Assembly):
// 0 = HashAlgId, 1 = the merged Version column, 2 = Flags, 3 = PublicKey,
// 4 = Name, 5 = Culture. The AssemblyRef table (0x23): 0 = the merged
// Version column, 1 = Flags, 2 = PublicKeyOrToken, 3 = Name, 4 = Culture,
// 5 = HashValue.
constexpr std::uint32_t kAssemblyPublicKeyColumn = 3;
constexpr std::uint32_t kAssemblyNameColumn = 4;
constexpr std::uint32_t kAssemblyCultureColumn = 5;
constexpr std::uint32_t kAssemblyRefFlagsColumn = 1;
constexpr std::uint32_t kAssemblyRefPublicKeyOrTokenColumn = 2;
constexpr std::uint32_t kAssemblyRefNameColumn = 3;
constexpr std::uint32_t kAssemblyRefCultureColumn = 4;

// System.Reflection.AssemblyFlags (the raw II.23.1.2 AssemblyAttributes bits):
// PublicKey selects a full public key in the PublicKeyOrToken column,
// Retargetable renders the "Retargetable=true" name segment.
constexpr std::uint32_t kAssemblyFlagsPublicKey = 0x0001;
constexpr std::uint32_t kAssemblyFlagsRetargetable = 0x0100;

// The C# `Version` render of the merged 8-byte version column -- the four
// ushort components in declaration order (SRM constructs a four-component
// System.Version from the raw columns, whose ToString renders all four).
std::string VersionToString(const MetadataFile::CorTableVersion& version)
{
    TypeSystem::Version v(version.MajorVersion, version.MinorVersion,
                          version.BuildNumber, version.RevisionNumber);
    return v.ToString();
}

// The C# `Culture=<...>` segment: the nil Culture column renders "neutral",
// a present column renders its string (an empty-string column renders
// empty).
std::string CultureSegment(const MetadataFile& file,
                            std::uint32_t cultureColumnValue)
{
    return cultureColumnValue == 0
        ? std::string("neutral")
        : file.CorString(cultureColumnValue);
}

} // namespace

// The C# CalculatePublicKeyToken (MetadataExtensions.cs):
//   1. hash the public key with SHA-1 (the strong-name format mandates SHA-1;
//      the managed implementation works under restrictive crypto policies);
//   2. take the last 8 bytes of the 20-byte digest;
//   3. reverse them (per Cecil; other sources do not mention this) and render
//      lowercase hex.
std::string CalculatePublicKeyToken(const MetadataFile& file,
                                    std::uint32_t blobOffset)
{
    std::vector<std::uint8_t> publicKey = file.CorBlob(blobOffset);
    std::uint8_t digest[20];
    Util::Sha1ForNonSecretPurposes::HashData(
        Util::Span<const std::uint8_t>(publicKey.data(), publicKey.size()),
        Util::Span<std::uint8_t>(digest, sizeof(digest)));
    // TakeLast(8).Reverse().ToHexString(8): the digest's final 8 bytes,
    // reversed, as lowercase hex.
    std::uint8_t token[8];
    for (int i = 0; i < 8; i++)
        token[i] = digest[20 - 8 + i];
    std::reverse(token, token + 8);
    return ToHexLower(token, 8);
}

// The C# GetPublicKeyToken(this MetadataReader) (MetadataExtensions.cs).
std::string GetPublicKeyToken(const MetadataFile& file)
{
    if (file.CorTableRowCount(CorTableIndex::Assembly) == 0)
        return std::string();
    // The raw-surface row parameter is 0-based (the displayed rid is row+1);
    // the Assembly table's single row is index 0.
    std::uint32_t publicKeyOffset = file.CorTableColumnValue(
        CorTableIndex::Assembly, 0, kAssemblyPublicKeyColumn);
    if (publicKeyOffset == 0)
        return "null";
    // AssemblyFlags.PublicKey does not apply to assembly definitions -- the
    // column is always the full public key.
    return CalculatePublicKeyToken(file, publicKeyOffset);
}

// The C# GetFullAssemblyName(this MetadataReader) (MetadataExtensions.cs).
std::string GetFullAssemblyName(const MetadataFile& file)
{
    if (file.CorTableRowCount(CorTableIndex::Assembly) == 0)
        return std::string();
    std::string name = file.CorString(file.CorTableColumnValue(
        CorTableIndex::Assembly, 0, kAssemblyNameColumn));
    std::string version = VersionToString(file.CorTableVersionValue(
        CorTableIndex::Assembly, 0));
    std::string culture = CultureSegment(file, file.CorTableColumnValue(
        CorTableIndex::Assembly, 0, kAssemblyCultureColumn));
    return name + ", Version=" + version + ", Culture=" + culture +
           ", PublicKeyToken=" + GetPublicKeyToken(file);
}

// The C# GetFullAssemblyName(this AssemblyReference, MetadataReader)
// (MetadataExtensions.cs).
std::string GetFullAssemblyName(const MetadataFile& file,
                                std::uint32_t assemblyReferenceToken)
{
    // The token's low 24 bits are the 1-based row number; the raw-surface row
    // parameter is 0-based. A nil row (0) is the C# GetAssemblyReference
    // BadImageFormatException arm.
    std::uint32_t row = assemblyReferenceToken & 0x00FFFFFF;
    if (row == 0)
        throw std::invalid_argument("Invalid row index");
    row -= 1;
    std::string name = file.CorString(file.CorTableColumnValue(
        CorTableIndex::AssemblyRef, row, kAssemblyRefNameColumn));
    std::string version = VersionToString(file.CorTableVersionValue(
        CorTableIndex::AssemblyRef, row));
    std::string culture = CultureSegment(file, file.CorTableColumnValue(
        CorTableIndex::AssemblyRef, row, kAssemblyRefCultureColumn));
    std::uint32_t flags = file.CorTableColumnValue(
        CorTableIndex::AssemblyRef, row, kAssemblyRefFlagsColumn);
    std::uint32_t publicKeyOrTokenOffset = file.CorTableColumnValue(
        CorTableIndex::AssemblyRef, row, kAssemblyRefPublicKeyOrTokenColumn);

    std::string result = name + ", Version=" + version + ", Culture=" + culture;
    if (publicKeyOrTokenOffset == 0)
    {
        result += ", PublicKeyToken=null";
    }
    else if ((flags & kAssemblyFlagsPublicKey) != 0)
    {
        result += ", PublicKeyToken=" +
                  CalculatePublicKeyToken(file, publicKeyOrTokenOffset);
    }
    else
    {
        std::vector<std::uint8_t> token = file.CorBlob(publicKeyOrTokenOffset);
        result += ", PublicKeyToken=" + ToHexLower(token.data(), token.size());
    }
    if ((flags & kAssemblyFlagsRetargetable) != 0)
        result += ", Retargetable=true";
    return result;
}

namespace {

// The shared Try body: the C# catch narrows to BadImageFormatException; the
// port's raw-surface analogs are std::invalid_argument (the winmd
// seek/row/terminator throws) and std::out_of_range (the port's own reader
// throws).
std::optional<std::string> TryCatch(const std::function<std::string()>& body)
{
    try
    {
        return body();
    }
    catch (const std::invalid_argument&)
    {
        return std::nullopt;
    }
    catch (const std::out_of_range&)
    {
        return std::nullopt;
    }
}

} // namespace

std::optional<std::string> TryGetFullAssemblyName(const MetadataFile& file)
{
    return TryCatch([&] { return GetFullAssemblyName(file); });
}

std::optional<std::string> TryGetFullAssemblyName(
    const MetadataFile& file, std::uint32_t assemblyReferenceToken)
{
    return TryCatch([&] { return GetFullAssemblyName(file, assemblyReferenceToken); });
}


KnownTypeCode ToKnownTypeCode(PrimitiveTypeCode typeCode)
{
    switch (typeCode) {
        case PrimitiveTypeCode::Boolean: return KnownTypeCode::Boolean;
        case PrimitiveTypeCode::Byte: return KnownTypeCode::Byte;
        case PrimitiveTypeCode::SByte: return KnownTypeCode::SByte;
        case PrimitiveTypeCode::Char: return KnownTypeCode::Char;
        case PrimitiveTypeCode::Int16: return KnownTypeCode::Int16;
        case PrimitiveTypeCode::UInt16: return KnownTypeCode::UInt16;
        case PrimitiveTypeCode::Int32: return KnownTypeCode::Int32;
        case PrimitiveTypeCode::UInt32: return KnownTypeCode::UInt32;
        case PrimitiveTypeCode::Int64: return KnownTypeCode::Int64;
        case PrimitiveTypeCode::UInt64: return KnownTypeCode::UInt64;
        case PrimitiveTypeCode::Single: return KnownTypeCode::Single;
        case PrimitiveTypeCode::Double: return KnownTypeCode::Double;
        case PrimitiveTypeCode::IntPtr: return KnownTypeCode::IntPtr;
        case PrimitiveTypeCode::UIntPtr: return KnownTypeCode::UIntPtr;
        case PrimitiveTypeCode::Object: return KnownTypeCode::Object;
        case PrimitiveTypeCode::String: return KnownTypeCode::String;
        case PrimitiveTypeCode::TypedReference: return KnownTypeCode::TypedReference;
        case PrimitiveTypeCode::Void: return KnownTypeCode::Void;
        default: return KnownTypeCode::None;
    }
}

PrimitiveTypeCode ToPrimitiveTypeCode(KnownTypeCode typeCode)
{
    // The C# switch (MetadataExtensions.cs line 234): the primitive codes map
    // back onto their element types; the default arm returns 0 (the C#
    // `default(PrimitiveTypeCode)` -- the `Unknown`-spelled value 0).
    switch (typeCode) {
        case KnownTypeCode::Object: return PrimitiveTypeCode::Object;
        case KnownTypeCode::Boolean: return PrimitiveTypeCode::Boolean;
        case KnownTypeCode::Char: return PrimitiveTypeCode::Char;
        case KnownTypeCode::SByte: return PrimitiveTypeCode::SByte;
        case KnownTypeCode::Byte: return PrimitiveTypeCode::Byte;
        case KnownTypeCode::Int16: return PrimitiveTypeCode::Int16;
        case KnownTypeCode::UInt16: return PrimitiveTypeCode::UInt16;
        case KnownTypeCode::Int32: return PrimitiveTypeCode::Int32;
        case KnownTypeCode::UInt32: return PrimitiveTypeCode::UInt32;
        case KnownTypeCode::Int64: return PrimitiveTypeCode::Int64;
        case KnownTypeCode::UInt64: return PrimitiveTypeCode::UInt64;
        case KnownTypeCode::Single: return PrimitiveTypeCode::Single;
        case KnownTypeCode::Double: return PrimitiveTypeCode::Double;
        case KnownTypeCode::String: return PrimitiveTypeCode::String;
        case KnownTypeCode::Void: return PrimitiveTypeCode::Void;
        case KnownTypeCode::TypedReference: return PrimitiveTypeCode::TypedReference;
        case KnownTypeCode::IntPtr: return PrimitiveTypeCode::IntPtr;
        case KnownTypeCode::UIntPtr: return PrimitiveTypeCode::UIntPtr;
        default: return static_cast<PrimitiveTypeCode>(0);
    }
}

std::string ToILNameString(const FullTypeName& typeName, bool omitGenerics)
{
    std::string name;
    if (typeName.IsNested())
    {
        name = typeName.Name();
        if (!omitGenerics)
        {
            int localTypeParameterCount =
                typeName.GetNestedTypeAdditionalTypeParameterCount(typeName.NestingLevel() - 1);
            if (localTypeParameterCount > 0)
                name += "`" + std::to_string(localTypeParameterCount);
        }
        name = Escape(name);
        return ToILNameString(typeName.GetDeclaringType(), omitGenerics) + "/" + name;
    }
    if (!typeName.GetTopLevelTypeName().Namespace().empty())
    {
        name = typeName.GetTopLevelTypeName().Namespace() + "." + typeName.Name();
        if (!omitGenerics && typeName.TypeParameterCount() > 0)
            name += "`" + std::to_string(typeName.TypeParameterCount());
    }
    else
    {
        name = typeName.Name();
        if (!omitGenerics && typeName.TypeParameterCount() > 0)
            name += "`" + std::to_string(typeName.TypeParameterCount());
    }
    return Escape(name);
}

// --- the minimalCorlibTypeProvider (MetadataExtensions.cs lines 215-228) ---

namespace {

// The C# `internal static readonly TypeProvider minimalCorlibTypeProvider =
// new TypeProvider(new SimpleCompilation(MinimalCorlib.Instance))`: one
// process-lifetime provider over a compilation whose only module is a fresh
// MinimalCorlib. The holder constructs the compilation FIRST (the provider
// holds a non-owning pointer to it); the minimal-corlib module the compilation
// resolves stays alive in the `MinimalCorlib::Instance()` reference's registry
// (the MinimalCorlib header convention (b)), and the KnownTypeCache slots that
// back `FindType` live in the compilation -- so the holder's static lifetime
// keeps every decoded type alive for the process, matching the C# static
// field's GC rooting.
struct MinimalCorlibProviderHolder {
    TypeSystem::SimpleCompilation compilation;
    TypeSystem::TypeProvider provider;

    MinimalCorlibProviderHolder()
        : compilation(TypeSystem::Implementation::MinimalCorlib::Instance(), {}),
          provider(compilation) {}
};

} // namespace

TypeSystem::TypeProvider& MinimalAttributeTypeProvider()
{
    // The C# static-readonly field initializer: thread-safe first-use
    // initialization (the magic-static; the C# relies on the class's static
    // constructor).
    static MinimalCorlibProviderHolder holder;
    return holder.provider;
}

TypeSystem::TypeProvider& MinimalSignatureTypeProvider()
{
    // The C# `get => minimalCorlibTypeProvider` -- the same static field.
    return MinimalAttributeTypeProvider();
}

} // namespace ILSpy::Decompiler::Metadata
