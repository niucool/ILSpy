// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The --dump-table implementation. See the header for the porting
// decisions; the C# source is ICSharpCode.ILSpyCmd/MetadataTableDumper.cs
// (LoadRows/FormatValue/WriteConsoleTable/WriteJson).

#include "ILSpyCmd/MetadataTableDumper.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::Metadata;

namespace ILSpy::ILSpyCmd {

namespace {

// ---------------------------------------------------------------------------
// Value formatting (the C# FormatValue/FormatHex/FormatRid arms)
// ---------------------------------------------------------------------------

// The C# `static string FormatHex(int value)` -- "0x" + the 8-digit
// uppercase hex form.
std::string FormatHex(std::uint32_t value) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "0x%08X", value);
    return buf;
}

// The C# FormatValue EntityHandle arm -- "nil" for the nil token (0), the
// hex token otherwise.
std::string FormatEntity(std::uint32_t token) {
    return token == 0 ? "nil" : FormatHex(token);
}

// The .NET `Guid.ToString()` "D" form over the 16 raw #GUID bytes (the
// canonical little-endian binary form: the first three groups reversed,
// the last ten bytes in order), lowercased.
std::string GuidToString(const std::array<std::uint8_t, 16>& g) {
    std::uint32_t a = static_cast<std::uint32_t>(g[0])
        | (static_cast<std::uint32_t>(g[1]) << 8)
        | (static_cast<std::uint32_t>(g[2]) << 16)
        | (static_cast<std::uint32_t>(g[3]) << 24);
    std::uint32_t b = static_cast<std::uint32_t>(g[4])
        | (static_cast<std::uint32_t>(g[5]) << 8);
    std::uint32_t c = static_cast<std::uint32_t>(g[6])
        | (static_cast<std::uint32_t>(g[7]) << 8);
    char buf[40];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        a, b, c, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return buf;
}

// A table column read + the raw value's formatting (the C# FormatValue
// StringHandle/BlobHandle/GuidHandle arms).
std::string FormatStringColumn(const MetadataFile& file,
    CorTableIndex table, std::uint32_t row, std::uint32_t column) {
    // The nil StringHandle (offset 0) renders the empty string.
    return file.CorString(file.CorTableColumnValue(table, row, column));
}

std::string FormatBlobColumn(const MetadataFile& file,
    CorTableIndex table, std::uint32_t row, std::uint32_t column) {
    std::uint32_t offset = file.CorTableColumnValue(table, row, column);
    // The BlobHandle arm: "nil" for the nil handle, the heap offset in hex.
    return offset == 0 ? "nil" : FormatHex(offset);
}

std::string FormatGuidColumn(const MetadataFile& file,
    CorTableIndex table, std::uint32_t row, std::uint32_t column) {
    std::uint32_t index = file.CorTableColumnValue(table, row, column);
    if (index == 0) return "nil";
    auto guid = file.CorTryGuid(index);
    if (!guid)
        throw std::out_of_range("the #GUID index runs past the heap");
    return GuidToString(*guid);
}

// A simple-index column (a 1-based row number into one table): the target
// row's token, "nil" for the raw 0.
std::uint32_t FormatRidToken(std::uint32_t targetTableId, std::uint32_t rawRid) {
    return rawRid == 0 ? 0 : (targetTableId << 24) | (rawRid & 0x00FFFFFF);
}

// A coded-index column: the raw (rid << tagBits) | tag value to the target
// row's token, 0 (nil) for the raw 0. An out-of-range tag maps to the same
// target the C# switch arms produce for it (the TypeDefOrRef default arm
// lands on TypeSpec; the SRM coded-index decoders produce the nil default
// handle elsewhere), which no compiler-produced file ever carries.
std::uint32_t DecodeCoded(std::uint32_t raw, int tagBits,
    const std::uint32_t* tagTables, std::size_t tagCount) {
    if (raw == 0) return 0;
    std::uint32_t tag = raw & ((1u << tagBits) - 1);
    std::uint32_t rid = raw >> tagBits;
    if (tag >= tagCount) return 0;
    return (tagTables[tag] << 24) | (rid & 0x00FFFFFF);
}

std::uint32_t DecodeTypeDefOrRef(std::uint32_t raw) {
    // 2 tag bits: TypeDef (0x02), TypeRef (0x01); the C# GetInterfaceImplRows
    // `_ => TypeSpecHandle` default arm carries tags 2 AND 3 to TypeSpec.
    static const std::uint32_t kTables[] = { 0x02, 0x01, 0x1B, 0x1B };
    return DecodeCoded(raw, 2, kTables, 4);
}

std::uint32_t DecodeResolutionScope(std::uint32_t raw) {
    // Module (0x00), ModuleRef (0x1A), AssemblyRef (0x23), TypeRef (0x01).
    static const std::uint32_t kTables[] = { 0x00, 0x1A, 0x23, 0x01 };
    return DecodeCoded(raw, 2, kTables, 4);
}

std::uint32_t DecodeHasConstant(std::uint32_t raw) {
    // Field (0x04), Param (0x08), Property (0x17).
    static const std::uint32_t kTables[] = { 0x04, 0x08, 0x17 };
    return DecodeCoded(raw, 2, kTables, 3);
}

std::uint32_t DecodeHasCustomAttribute(std::uint32_t raw) {
    // 5 tag bits, the II.24.2.6 HasCustomAttribute order: MethodDef (0x06),
    // Field, TypeRef, TypeDef, Param, InterfaceImpl (0x09), MemberRef
    // (0x0A), Module, DeclSecurity/Permission (0x0E), Property (0x17),
    // Event (0x14), StandAloneSig (0x11), ModuleRef (0x1A), TypeSpec
    // (0x1B), Assembly (0x20), AssemblyRef (0x23), File (0x26),
    // ExportedType (0x27), ManifestResource (0x28), GenericParam (0x2A),
    // GenericParamConstraint (0x2C), MethodSpec (0x2B).
    static const std::uint32_t kTables[] = {
        0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17,
        0x14, 0x11, 0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C,
        0x2B,
    };
    return DecodeCoded(raw, 5, kTables, 22);
}

std::uint32_t DecodeHasFieldMarshal(std::uint32_t raw) {
    // Field (0x04), Param (0x08).
    static const std::uint32_t kTables[] = { 0x04, 0x08 };
    return DecodeCoded(raw, 1, kTables, 2);
}

std::uint32_t DecodeHasDeclSecurity(std::uint32_t raw) {
    // TypeDef (0x02), MethodDef (0x06), Assembly (0x20).
    static const std::uint32_t kTables[] = { 0x02, 0x06, 0x20 };
    return DecodeCoded(raw, 2, kTables, 3);
}

std::uint32_t DecodeMemberRefParent(std::uint32_t raw) {
    // 3 tag bits: TypeDef (0x02), TypeRef (0x01), ModuleRef (0x1A),
    // MethodDef (0x06), TypeSpec (0x1B).
    static const std::uint32_t kTables[] = { 0x02, 0x01, 0x1A, 0x06, 0x1B };
    return DecodeCoded(raw, 3, kTables, 5);
}

std::uint32_t DecodeHasSemantics(std::uint32_t raw) {
    // Event (0x14), Property (0x17).
    static const std::uint32_t kTables[] = { 0x14, 0x17 };
    return DecodeCoded(raw, 1, kTables, 2);
}

std::uint32_t DecodeMethodDefOrRef(std::uint32_t raw) {
    // MethodDef (0x06), MemberRef (0x0A).
    static const std::uint32_t kTables[] = { 0x06, 0x0A };
    return DecodeCoded(raw, 1, kTables, 2);
}

std::uint32_t DecodeMemberForwarded(std::uint32_t raw) {
    // Field (0x04), MethodDef (0x06).
    static const std::uint32_t kTables[] = { 0x04, 0x06 };
    return DecodeCoded(raw, 1, kTables, 2);
}

std::uint32_t DecodeImplementation(std::uint32_t raw) {
    // File (0x26), AssemblyRef (0x23), ExportedType (0x27).
    static const std::uint32_t kTables[] = { 0x26, 0x23, 0x27 };
    return DecodeCoded(raw, 2, kTables, 3);
}

std::uint32_t DecodeCustomAttributeType(std::uint32_t raw) {
    // 3 tag bits; tags 2 = MethodDef (0x06), 3 = MemberRef (0x0A) (0 and 1
    // are reserved); the other tags decode to the nil default.
    if (raw == 0) return 0;
    std::uint32_t tag = raw & 0x7;
    std::uint32_t rid = raw >> 3;
    switch (tag) {
        case 2: return (0x06u << 24) | (rid & 0x00FFFFFF);
        case 3: return (0x0Au << 24) | (rid & 0x00FFFFFF);
        default: return 0;
    }
}

std::uint32_t DecodeTypeOrMethodDef(std::uint32_t raw) {
    // TypeDef (0x02), MethodDef (0x06).
    static const std::uint32_t kTables[] = { 0x02, 0x06 };
    return DecodeCoded(raw, 1, kTables, 2);
}

// ---------------------------------------------------------------------------
// The .NET 10 Enum.ToString semantics (see the header's porting decisions)
// ---------------------------------------------------------------------------

struct EnumMember {
    std::int64_t Value;
    const char* Name;
};

// A plain (non-[Flags]) enum: the exact member name (the first of
// equal-valued members -- the GetName BinarySearch landing), else the
// decimal value.
std::string FormatPlainEnum(const EnumMember* members, std::size_t count,
    std::int64_t value) {
    for (std::size_t i = 0; i < count; ++i)
        if (members[i].Value == value) return members[i].Name;
    return std::to_string(value);
}

// A [Flags] enum, the decompiled .NET 10 FormatFlagNames algorithm.
std::string FormatFlagsEnum(const EnumMember* members, std::size_t count,
    std::int64_t value) {
    // The zero fast path: the first member's name when the smallest value is
    // the zero member, else the decimal.
    if (value == 0)
        return members[0].Value == 0 ? std::string(members[0].Name) : "0";
    // The exact-single-name scan: from the largest value downward, skip the
    // members above the value; the first member at or below it that EQUALS it
    // is the exact name (the last of equal-valued members).
    int idx = static_cast<int>(count) - 1;
    while (idx >= 0 && members[idx].Value > value) --idx;
    if (idx >= 0 && members[idx].Value == value)
        return members[idx].Name;
    // The union scan: from idx downward, record the members whose bits are
    // all present and clear them; a leftover bit discards the collected
    // names and renders the full value in decimal.
    std::uint64_t rest = static_cast<std::uint64_t>(value);
    std::vector<const char*> found;
    for (int i = idx; i >= 0; --i) {
        std::int64_t v = members[i].Value;
        // The zero member only participates at index 0, where the loop ends
        // anyway (the zero-name recording is discarded -- the leftover-bit
        // fallback fires either way).
        if (i == 0 && v == 0) break;
        std::uint64_t bits = static_cast<std::uint64_t>(v);
        if ((rest & bits) == bits) {
            found.push_back(members[i].Name);
            rest &= ~bits;
            if (rest == 0) break;
        }
    }
    if (rest != 0) return std::to_string(value);
    // WriteMultipleFoundFlagsNames: the collected descending-order names
    // join in reverse (ascending value) order, ", " separated.
    std::string result;
    for (auto it = found.rbegin(); it != found.rend(); ++it) {
        if (!result.empty()) result += ", ";
        result += *it;
    }
    return result;
}

// The member tables: Enum.GetValues order (ascending by value, ties in the
// probed .NET 10 Array.Sort order), the values the real renders pin.

// System.Reflection.TypeAttributes (the TypeDef Flags column and the
// ExportedType Flags column -- ExportedType.Attributes is the same enum).
constexpr EnumMember kTypeAttributes[] = {
    { 0x00000000, "NotPublic" },
    { 0x00000000, "AutoLayout" },
    { 0x00000000, "AnsiClass" },
    { 0x00000000, "Class" },
    { 0x00000001, "Public" },
    { 0x00000002, "NestedPublic" },
    { 0x00000003, "NestedPrivate" },
    { 0x00000004, "NestedFamily" },
    { 0x00000005, "NestedAssembly" },
    { 0x00000006, "NestedFamANDAssem" },
    { 0x00000007, "VisibilityMask" },
    { 0x00000007, "NestedFamORAssem" },
    { 0x00000008, "SequentialLayout" },
    { 0x00000010, "ExplicitLayout" },
    { 0x00000018, "LayoutMask" },
    { 0x00000020, "Interface" },
    { 0x00000020, "ClassSemanticsMask" },
    { 0x00000080, "Abstract" },
    { 0x00000100, "Sealed" },
    { 0x00000400, "SpecialName" },
    { 0x00000800, "RTSpecialName" },
    { 0x00001000, "Import" },
    { 0x00002000, "Serializable" },
    { 0x00004000, "WindowsRuntime" },
    { 0x00010000, "UnicodeClass" },
    { 0x00020000, "AutoClass" },
    { 0x00030000, "StringFormatMask" },
    { 0x00030000, "CustomFormatClass" },
    { 0x00040000, "HasSecurity" },
    { 0x00040800, "ReservedMask" },
    { 0x00100000, "BeforeFieldInit" },
    { 0x00C00000, "CustomFormatMask" },
};

// System.Reflection.FieldAttributes (the Field Flags column).
constexpr EnumMember kFieldAttributes[] = {
    { 0x0000, "PrivateScope" },
    { 0x0001, "Private" },
    { 0x0002, "FamANDAssem" },
    { 0x0003, "Assembly" },
    { 0x0004, "Family" },
    { 0x0005, "FamORAssem" },
    { 0x0006, "Public" },
    { 0x0007, "FieldAccessMask" },
    { 0x0010, "Static" },
    { 0x0020, "InitOnly" },
    { 0x0040, "Literal" },
    { 0x0080, "NotSerialized" },
    { 0x0100, "HasFieldRVA" },
    { 0x0200, "SpecialName" },
    { 0x0400, "RTSpecialName" },
    { 0x1000, "HasFieldMarshal" },
    { 0x2000, "PinvokeImpl" },
    { 0x8000, "HasDefault" },
    { 0x9520, "ReservedMask" },
};

// System.Reflection.MethodAttributes (the MethodDef Flags column).
constexpr EnumMember kMethodAttributes[] = {
    { 0x0000, "PrivateScope" },
    { 0x0000, "ReuseSlot" },
    { 0x0001, "Private" },
    { 0x0002, "FamANDAssem" },
    { 0x0003, "Assembly" },
    { 0x0004, "Family" },
    { 0x0005, "FamORAssem" },
    { 0x0006, "Public" },
    { 0x0007, "MemberAccessMask" },
    { 0x0008, "UnmanagedExport" },
    { 0x0010, "Static" },
    { 0x0020, "Final" },
    { 0x0040, "Virtual" },
    { 0x0080, "HideBySig" },
    { 0x0100, "NewSlot" },
    { 0x0100, "VtableLayoutMask" },
    { 0x0200, "CheckAccessOnOverride" },
    { 0x0400, "Abstract" },
    { 0x0800, "SpecialName" },
    { 0x1000, "RTSpecialName" },
    { 0x2000, "PinvokeImpl" },
    { 0x4000, "HasSecurity" },
    { 0x8000, "RequireSecObject" },
    { 0xD000, "ReservedMask" },
};

// System.Reflection.MethodImplAttributes (the MethodDef ImplFlags column)
// -- NOT [Flags] on .NET 10, so ToString takes the plain path.
constexpr EnumMember kMethodImplAttributes[] = {
    { 0x0000, "IL" },
    { 0x0000, "Managed" },
    { 0x0001, "Native" },
    { 0x0002, "OPTIL" },
    { 0x0003, "CodeTypeMask" },
    { 0x0003, "Runtime" },
    { 0x0004, "ManagedMask" },
    { 0x0004, "Unmanaged" },
    { 0x0008, "NoInlining" },
    { 0x0010, "ForwardRef" },
    { 0x0020, "Synchronized" },
    { 0x0040, "NoOptimization" },
    { 0x0080, "PreserveSig" },
    { 0x0100, "AggressiveInlining" },
    { 0x0200, "AggressiveOptimization" },
    { 0x1000, "InternalCall" },
    { 0x2000, "Async" },
    { 0xFFFF, "MaxMethodImplVal" },
};

// System.Reflection.ParameterAttributes (the Param Flags column).
constexpr EnumMember kParameterAttributes[] = {
    { 0x0000, "None" },
    { 0x0001, "In" },
    { 0x0002, "Out" },
    { 0x0004, "Lcid" },
    { 0x0008, "Retval" },
    { 0x0010, "Optional" },
    { 0x1000, "HasDefault" },
    { 0x2000, "HasFieldMarshal" },
    { 0x4000, "Reserved3" },
    { 0x8000, "Reserved4" },
    { 0xF000, "ReservedMask" },
};

// System.Reflection.PropertyAttributes (the Property Flags column).
constexpr EnumMember kPropertyAttributes[] = {
    { 0x0000, "None" },
    { 0x0200, "SpecialName" },
    { 0x0400, "RTSpecialName" },
    { 0x1000, "HasDefault" },
    { 0x2000, "Reserved2" },
    { 0x4000, "Reserved3" },
    { 0x8000, "Reserved4" },
    { 0xF400, "ReservedMask" },
};

// System.Reflection.EventAttributes (the Event EventFlags column).
constexpr EnumMember kEventAttributes[] = {
    { 0x0000, "None" },
    { 0x0200, "SpecialName" },
    { 0x0400, "RTSpecialName" },
    { 0x0400, "ReservedMask" },
};

// System.Reflection.GenericParameterAttributes (the GenericParam Flags
// column).
constexpr EnumMember kGenericParameterAttributes[] = {
    { 0x00, "None" },
    { 0x01, "Covariant" },
    { 0x02, "Contravariant" },
    { 0x03, "VarianceMask" },
    { 0x04, "ReferenceTypeConstraint" },
    { 0x08, "NotNullableValueTypeConstraint" },
    { 0x10, "DefaultConstructorConstraint" },
    { 0x1C, "SpecialConstraintMask" },
    { 0x20, "AllowByRefLike" },
};

// System.Reflection.MethodSemanticsAttributes (the MethodSemantics
// Semantics column).
constexpr EnumMember kMethodSemanticsAttributes[] = {
    { 0x01, "Setter" },
    { 0x02, "Getter" },
    { 0x04, "Other" },
    { 0x08, "Adder" },
    { 0x10, "Remover" },
    { 0x20, "Raiser" },
};

// System.Reflection.MethodImportAttributes (the ImplMap MappingFlags
// column) -- short-backed, so the decimal fallback renders the signed
// value (the caller widens through int16_t).
constexpr EnumMember kMethodImportAttributes[] = {
    { 0x0000, "None" },
    { 0x0001, "ExactSpelling" },
    { 0x0002, "CharSetAnsi" },
    { 0x0004, "CharSetUnicode" },
    { 0x0006, "CharSetAuto" },
    { 0x0006, "CharSetMask" },
    { 0x0010, "BestFitMappingEnable" },
    { 0x0020, "BestFitMappingDisable" },
    { 0x0030, "BestFitMappingMask" },
    { 0x0040, "SetLastError" },
    { 0x0100, "CallingConventionWinApi" },
    { 0x0200, "CallingConventionCDecl" },
    { 0x0300, "CallingConventionStdCall" },
    { 0x0400, "CallingConventionThisCall" },
    { 0x0500, "CallingConventionFastCall" },
    { 0x0700, "CallingConventionMask" },
    { 0x1000, "ThrowOnUnmappableCharEnable" },
    { 0x2000, "ThrowOnUnmappableCharDisable" },
    { 0x3000, "ThrowOnUnmappableCharMask" },
};

// System.Reflection.AssemblyFlags (the Assembly/AssemblyRef Flags
// columns).
constexpr EnumMember kAssemblyFlags[] = {
    { 0x00000001, "PublicKey" },
    { 0x00000100, "Retargetable" },
    { 0x00000200, "WindowsRuntime" },
    { 0x00000E00, "ContentTypeMask" },
    { 0x00004000, "DisableJitCompileOptimizer" },
    { 0x00008000, "EnableJitCompileTracking" },
};

// System.Reflection.ManifestResourceAttributes (the ManifestResource
// Flags column).
constexpr EnumMember kManifestResourceAttributes[] = {
    { 0x1, "Public" },
    { 0x2, "Private" },
    { 0x7, "VisibilityMask" },
};

// System.Reflection.AssemblyHashAlgorithm (the Assembly HashAlgId column)
// -- plain, int32-backed.
constexpr EnumMember kAssemblyHashAlgorithm[] = {
    { 0x0000, "None" },
    { 0x8003, "MD5" },
    { 0x8004, "Sha1" },
    { 0x800C, "Sha256" },
    { 0x800D, "Sha384" },
    { 0x800E, "Sha512" },
};

// System.Reflection.Metadata.ConstantTypeCode (the Constant Type column's
// low byte) -- plain, byte-backed.
constexpr EnumMember kConstantTypeCode[] = {
    { 0x00, "Invalid" },
    { 0x02, "Boolean" },
    { 0x03, "Char" },
    { 0x04, "SByte" },
    { 0x05, "Byte" },
    { 0x06, "Int16" },
    { 0x07, "UInt16" },
    { 0x08, "Int32" },
    { 0x09, "UInt32" },
    { 0x0A, "Int64" },
    { 0x0B, "UInt64" },
    { 0x0C, "Single" },
    { 0x0D, "Double" },
    { 0x0E, "String" },
    { 0x12, "NullReference" },
};

// System.Reflection.DeclarativeSecurityAction (the DeclSecurity Action
// column) -- plain, short-backed.
constexpr EnumMember kDeclarativeSecurityAction[] = {
    { 0x0, "None" },
    { 0x2, "Demand" },
    { 0x3, "Assert" },
    { 0x4, "Deny" },
    { 0x5, "PermitOnly" },
    { 0x6, "LinkDemand" },
    { 0x7, "InheritanceDemand" },
    { 0x8, "RequestMinimum" },
    { 0x9, "RequestOptional" },
    { 0xA, "RequestRefuse" },
};

// ---------------------------------------------------------------------------
// Row building (the C# BuildRow shape)
// ---------------------------------------------------------------------------

// A row cell: the column name and the value -- the RID column stays
// numeric (the JSON writer's number arm), every other column holds its
// already-formatted string (the C# FormatValue runs inside BuildRow).
struct Cell {
    const char* Column = "";
    bool IsInt = false;
    std::int64_t IntValue = 0;
    std::string StrValue;
};
using Row = std::vector<Cell>;

Cell IntCell(const char* column, std::int64_t value) {
    Cell cell;
    cell.Column = column;
    cell.IsInt = true;
    cell.IntValue = value;
    return cell;
}

Cell TextCell(const char* column, std::string value) {
    Cell cell;
    cell.Column = column;
    cell.StrValue = std::move(value);
    return cell;
}

// The C# BuildRow: the RID + Token head plus the table columns.
Row BuildRow(std::uint32_t rid, std::uint32_t token, std::vector<Cell> cells) {
    Row row;
    row.reserve(cells.size() + 2);
    row.push_back(IntCell("RID", rid));
    row.push_back(TextCell("Token", FormatHex(token)));
    for (Cell& c : cells) row.push_back(std::move(c));
    return row;
}

// The .NET `Version.ToString()`: the four UInt16 version fields ("4.0.0.0")
// -- every assembly version carries all four (a -1 field would truncate the
// rendering, which no assembly manifest carries).
std::string FormatVersion(const MetadataFile::CorTableVersion& version) {
    return std::to_string(version.MajorVersion) + "."
        + std::to_string(version.MinorVersion) + "."
        + std::to_string(version.BuildNumber) + "."
        + std::to_string(version.RevisionNumber);
}

// The C# LoadRows: every table's columns in ECMA-335 declaration order.
// The row indexes are 0-based here (the winmd get_value convention); the
// RID the row renders is +1.
std::vector<Row> LoadRows(const MetadataFile& file, CorTableIndex table) {
    std::vector<Row> rows;
    std::uint32_t count = file.CorTableRowCount(table);
    switch (table) {
        case CorTableIndex::Module:
            if (count > 0) {
                rows.push_back(BuildRow(1, (0x00u << 24) | 1u, {
                    TextCell("Generation", std::to_string(
                        file.CorTableColumnValue(table, 0, 0))),
                    TextCell("Name", FormatStringColumn(file, table, 0, 1)),
                    TextCell("Mvid", FormatGuidColumn(file, table, 0, 2)),
                    TextCell("GenerationId", FormatGuidColumn(file, table, 0, 3)),
                    TextCell("BaseGenerationId", FormatGuidColumn(file, table, 0, 4)),
                }));
            }
            break;
        case CorTableIndex::TypeRef:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x01u << 24) | (r + 1), {
                    TextCell("ResolutionScope", FormatEntity(
                        DecodeResolutionScope(
                            file.CorTableColumnValue(table, r, 0)))),
                    TextCell("Name", FormatStringColumn(file, table, r, 1)),
                    TextCell("Namespace", FormatStringColumn(file, table, r, 2)),
                }));
            break;
        case CorTableIndex::TypeDef:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x02u << 24) | (r + 1), {
                    TextCell("Attributes", FormatFlagsEnum(kTypeAttributes,
                        sizeof(kTypeAttributes) / sizeof(kTypeAttributes[0]),
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("Name", FormatStringColumn(file, table, r, 1)),
                    TextCell("Namespace", FormatStringColumn(file, table, r, 2)),
                    TextCell("BaseType", FormatEntity(
                        DecodeTypeDefOrRef(
                            file.CorTableColumnValue(table, r, 3)))),
                    // The FieldList/MethodList columns carry the running list
                    // position (the next type's first member row, or one past
                    // the member table's end), rendered as the plain row
                    // number -- the C# GetTypeDefListColumns reads (the C#
                    // FormatRid decimal string).
                    TextCell("FieldList", std::to_string(
                        file.CorTableColumnValue(table, r, 4))),
                    TextCell("MethodList", std::to_string(
                        file.CorTableColumnValue(table, r, 5))),
                }));
            break;
        case CorTableIndex::Field:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x04u << 24) | (r + 1), {
                    TextCell("Attributes", FormatFlagsEnum(kFieldAttributes,
                        sizeof(kFieldAttributes) / sizeof(kFieldAttributes[0]),
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("Name", FormatStringColumn(file, table, r, 1)),
                    TextCell("Signature", FormatBlobColumn(file, table, r, 2)),
                }));
            break;
        case CorTableIndex::MethodDef:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x06u << 24) | (r + 1), {
                    TextCell("RVA", FormatHex(
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("ImplAttributes", FormatPlainEnum(
                        kMethodImplAttributes,
                        sizeof(kMethodImplAttributes)
                            / sizeof(kMethodImplAttributes[0]),
                        file.CorTableColumnValue(table, r, 1))),
                    TextCell("Attributes", FormatFlagsEnum(kMethodAttributes,
                        sizeof(kMethodAttributes) / sizeof(kMethodAttributes[0]),
                        file.CorTableColumnValue(table, r, 2))),
                    TextCell("Name", FormatStringColumn(file, table, r, 3)),
                    TextCell("Signature", FormatBlobColumn(file, table, r, 4)),
                    TextCell("ParamList", std::to_string(
                        file.CorTableColumnValue(table, r, 5))),
                }));
            break;
        case CorTableIndex::Param:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x08u << 24) | (r + 1), {
                    TextCell("Attributes", FormatFlagsEnum(
                        kParameterAttributes,
                        sizeof(kParameterAttributes)
                            / sizeof(kParameterAttributes[0]),
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("SequenceNumber", std::to_string(
                        file.CorTableColumnValue(table, r, 1))),
                    TextCell("Name", FormatStringColumn(file, table, r, 2)),
                }));
            break;
        case CorTableIndex::InterfaceImpl:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x09u << 24) | (r + 1), {
                    TextCell("Class", FormatEntity(FormatRidToken(0x02,
                        file.CorTableColumnValue(table, r, 0)))),
                    TextCell("Interface", FormatEntity(
                        DecodeTypeDefOrRef(
                            file.CorTableColumnValue(table, r, 1)))),
                }));
            break;
        case CorTableIndex::MemberRef:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x0Au << 24) | (r + 1), {
                    TextCell("Parent", FormatEntity(
                        DecodeMemberRefParent(
                            file.CorTableColumnValue(table, r, 0)))),
                    TextCell("Name", FormatStringColumn(file, table, r, 1)),
                    TextCell("Signature", FormatBlobColumn(file, table, r, 2)),
                }));
            break;
        case CorTableIndex::Constant:
            for (std::uint32_t r = 0; r < count; ++r) {
                // The Type column is physically 2 bytes; SRM's TypeCode reads
                // the byte at the column's offset 0 (the low byte).
                std::uint32_t typeColumn =
                    file.CorTableColumnValue(table, r, 0);
                rows.push_back(BuildRow(r + 1, (0x0Bu << 24) | (r + 1), {
                    TextCell("TypeCode", FormatPlainEnum(kConstantTypeCode,
                        sizeof(kConstantTypeCode)
                            / sizeof(kConstantTypeCode[0]),
                        typeColumn & 0xFF)),
                    TextCell("Parent", FormatEntity(
                        DecodeHasConstant(
                            file.CorTableColumnValue(table, r, 1)))),
                    TextCell("Value", FormatBlobColumn(file, table, r, 2)),
                }));
            }
            break;
        case CorTableIndex::CustomAttribute:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x0Cu << 24) | (r + 1), {
                    TextCell("Parent", FormatEntity(
                        DecodeHasCustomAttribute(
                            file.CorTableColumnValue(table, r, 0)))),
                    TextCell("Constructor", FormatEntity(
                        DecodeCustomAttributeType(
                            file.CorTableColumnValue(table, r, 1)))),
                    TextCell("Value", FormatBlobColumn(file, table, r, 2)),
                }));
            break;
        case CorTableIndex::FieldMarshal:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x0Du << 24) | (r + 1), {
                    TextCell("Parent", FormatEntity(
                        DecodeHasFieldMarshal(
                            file.CorTableColumnValue(table, r, 0)))),
                    TextCell("NativeType", FormatBlobColumn(file, table, r, 1)),
                }));
            break;
        case CorTableIndex::DeclSecurity:
            for (std::uint32_t r = 0; r < count; ++r) {
                // The short-backed DeclarativeSecurityAction: an unnamed raw
                // value above 0x7FFF renders the signed decimal.
                std::uint32_t action = file.CorTableColumnValue(table, r, 0);
                rows.push_back(BuildRow(r + 1, (0x0Eu << 24) | (r + 1), {
                    TextCell("Action", FormatPlainEnum(
                        kDeclarativeSecurityAction,
                        sizeof(kDeclarativeSecurityAction)
                            / sizeof(kDeclarativeSecurityAction[0]),
                        static_cast<std::int16_t>(action))),
                    TextCell("Parent", FormatEntity(
                        DecodeHasDeclSecurity(
                            file.CorTableColumnValue(table, r, 1)))),
                    TextCell("PermissionSet",
                        FormatBlobColumn(file, table, r, 2)),
                }));
            }
            break;
        case CorTableIndex::ClassLayout:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x0Fu << 24) | (r + 1), {
                    TextCell("PackingSize", std::to_string(
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("ClassSize", std::to_string(
                        file.CorTableColumnValue(table, r, 1))),
                    TextCell("Parent", FormatEntity(FormatRidToken(0x02,
                        file.CorTableColumnValue(table, r, 2)))),
                }));
            break;
        case CorTableIndex::FieldLayout:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x10u << 24) | (r + 1), {
                    TextCell("Offset", std::to_string(
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("Field", FormatEntity(FormatRidToken(0x04,
                        file.CorTableColumnValue(table, r, 1)))),
                }));
            break;
        case CorTableIndex::StandAloneSig:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x11u << 24) | (r + 1), {
                    TextCell("Signature", FormatBlobColumn(file, table, r, 0)),
                }));
            break;
        case CorTableIndex::EventMap:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x12u << 24) | (r + 1), {
                    TextCell("Parent", FormatEntity(FormatRidToken(0x02,
                        file.CorTableColumnValue(table, r, 0)))),
                    // The EventList column is the running list position into
                    // the Event table, materialized as the row's token.
                    TextCell("EventList", FormatEntity(FormatRidToken(0x14,
                        file.CorTableColumnValue(table, r, 1)))),
                }));
            break;
        case CorTableIndex::Event:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x14u << 24) | (r + 1), {
                    TextCell("Attributes", FormatFlagsEnum(kEventAttributes,
                        sizeof(kEventAttributes) / sizeof(kEventAttributes[0]),
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("Name", FormatStringColumn(file, table, r, 1)),
                    TextCell("Type", FormatEntity(
                        DecodeTypeDefOrRef(
                            file.CorTableColumnValue(table, r, 2)))),
                }));
            break;
        case CorTableIndex::PropertyMap:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x15u << 24) | (r + 1), {
                    TextCell("Parent", FormatEntity(FormatRidToken(0x02,
                        file.CorTableColumnValue(table, r, 0)))),
                    TextCell("PropertyList", FormatEntity(
                        FormatRidToken(0x17,
                            file.CorTableColumnValue(table, r, 1)))),
                }));
            break;
        case CorTableIndex::Property:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x17u << 24) | (r + 1), {
                    TextCell("Attributes", FormatFlagsEnum(
                        kPropertyAttributes,
                        sizeof(kPropertyAttributes)
                            / sizeof(kPropertyAttributes[0]),
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("Name", FormatStringColumn(file, table, r, 1)),
                    TextCell("Signature", FormatBlobColumn(file, table, r, 2)),
                }));
            break;
        case CorTableIndex::MethodSemantics:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x18u << 24) | (r + 1), {
                    TextCell("Semantics", FormatFlagsEnum(
                        kMethodSemanticsAttributes,
                        sizeof(kMethodSemanticsAttributes)
                            / sizeof(kMethodSemanticsAttributes[0]),
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("Method", FormatEntity(FormatRidToken(0x06,
                        file.CorTableColumnValue(table, r, 1)))),
                    TextCell("Association", FormatEntity(
                        DecodeHasSemantics(
                            file.CorTableColumnValue(table, r, 2)))),
                }));
            break;
        case CorTableIndex::MethodImpl:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x19u << 24) | (r + 1), {
                    TextCell("Class", FormatEntity(FormatRidToken(0x02,
                        file.CorTableColumnValue(table, r, 0)))),
                    TextCell("MethodBody", FormatEntity(
                        DecodeMethodDefOrRef(
                            file.CorTableColumnValue(table, r, 1)))),
                    TextCell("MethodDeclaration", FormatEntity(
                        DecodeMethodDefOrRef(
                            file.CorTableColumnValue(table, r, 2)))),
                }));
            break;
        case CorTableIndex::ModuleRef:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x1Au << 24) | (r + 1), {
                    TextCell("Name", FormatStringColumn(file, table, r, 0)),
                }));
            break;
        case CorTableIndex::TypeSpec:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x1Bu << 24) | (r + 1), {
                    TextCell("Signature", FormatBlobColumn(file, table, r, 0)),
                }));
            break;
        case CorTableIndex::ImplMap:
            for (std::uint32_t r = 0; r < count; ++r) {
                // The short-backed MethodImportAttributes: an unnamed raw
                // value above 0x7FFF renders the signed decimal.
                std::uint32_t flags = file.CorTableColumnValue(table, r, 0);
                rows.push_back(BuildRow(r + 1, (0x1Cu << 24) | (r + 1), {
                    TextCell("MappingFlags", FormatFlagsEnum(
                        kMethodImportAttributes,
                        sizeof(kMethodImportAttributes)
                            / sizeof(kMethodImportAttributes[0]),
                        static_cast<std::int16_t>(flags))),
                    TextCell("MemberForwarded", FormatEntity(
                        DecodeMemberForwarded(
                            file.CorTableColumnValue(table, r, 1)))),
                    TextCell("ImportName",
                        FormatStringColumn(file, table, r, 2)),
                    TextCell("ImportScope", FormatEntity(FormatRidToken(0x1A,
                        file.CorTableColumnValue(table, r, 3)))),
                }));
            }
            break;
        case CorTableIndex::FieldRva:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x1Du << 24) | (r + 1), {
                    TextCell("RVA", FormatHex(
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("Field", FormatEntity(FormatRidToken(0x04,
                        file.CorTableColumnValue(table, r, 1)))),
                }));
            break;
        case CorTableIndex::Assembly:
            if (count > 0) {
                rows.push_back(BuildRow(1, (0x20u << 24) | 1u, {
                    TextCell("HashAlgorithm", FormatPlainEnum(
                        kAssemblyHashAlgorithm,
                        sizeof(kAssemblyHashAlgorithm)
                            / sizeof(kAssemblyHashAlgorithm[0]),
                        file.CorTableColumnValue(table, 0, 0))),
                    TextCell("Version", FormatVersion(
                        file.CorTableVersionValue(table, 0))),
                    TextCell("Flags", FormatFlagsEnum(kAssemblyFlags,
                        sizeof(kAssemblyFlags) / sizeof(kAssemblyFlags[0]),
                        file.CorTableColumnValue(table, 0, 2))),
                    TextCell("PublicKey", FormatBlobColumn(file, table, 0, 3)),
                    TextCell("Name", FormatStringColumn(file, table, 0, 4)),
                    TextCell("Culture", FormatStringColumn(file, table, 0, 5)),
                }));
            }
            break;
        case CorTableIndex::AssemblyRef:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x23u << 24) | (r + 1), {
                    TextCell("Version", FormatVersion(
                        file.CorTableVersionValue(table, r))),
                    TextCell("Flags", FormatFlagsEnum(kAssemblyFlags,
                        sizeof(kAssemblyFlags) / sizeof(kAssemblyFlags[0]),
                        file.CorTableColumnValue(table, r, 1))),
                    TextCell("PublicKeyOrToken",
                        FormatBlobColumn(file, table, r, 2)),
                    TextCell("Name", FormatStringColumn(file, table, r, 3)),
                    TextCell("Culture", FormatStringColumn(file, table, r, 4)),
                    TextCell("HashValue", FormatBlobColumn(file, table, r, 5)),
                }));
            break;
        case CorTableIndex::File:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x26u << 24) | (r + 1), {
                    // The C# `file.ContainsMetadata`: the Flags column == 0
                    // (the bit 0x00000001 marks a resource file that carries
                    // no metadata).
                    TextCell("ContainsMetadata",
                        file.CorTableColumnValue(table, r, 0) == 0
                            ? "true" : "false"),
                    TextCell("Name", FormatStringColumn(file, table, r, 1)),
                    TextCell("HashValue", FormatBlobColumn(file, table, r, 2)),
                }));
            break;
        case CorTableIndex::ExportedType:
            for (std::uint32_t r = 0; r < count; ++r) {
                std::uint32_t flags =
                    file.CorTableColumnValue(table, r, 0);
                std::uint32_t implementation =
                    DecodeImplementation(
                        file.CorTableColumnValue(table, r, 4));
                // The fused SRM IsForwarder: the ForwarderType flag
                // (TypeAttributes 0x00200000, which no TypeAttributes member
                // spells) AND an AssemblyReference implementation.
                bool isForwarder = (flags & 0x00200000) != 0
                    && (implementation >> 24) == 0x23;
                rows.push_back(BuildRow(r + 1, (0x27u << 24) | (r + 1), {
                    TextCell("Attributes", FormatFlagsEnum(kTypeAttributes,
                        sizeof(kTypeAttributes) / sizeof(kTypeAttributes[0]),
                        flags)),
                    TextCell("Name", FormatStringColumn(file, table, r, 2)),
                    TextCell("Namespace", FormatStringColumn(file, table, r, 3)),
                    TextCell("Implementation", FormatEntity(implementation)),
                    TextCell("IsForwarder", isForwarder ? "true" : "false"),
                }));
            }
            break;
        case CorTableIndex::ManifestResource:
            for (std::uint32_t r = 0; r < count; ++r) {
                // The Offset column is the SRM int: the raw value rendered
                // signed (the manifest resource offset inside the file).
                std::uint32_t offset = file.CorTableColumnValue(table, r, 0);
                rows.push_back(BuildRow(r + 1, (0x28u << 24) | (r + 1), {
                    TextCell("Offset", std::to_string(
                        static_cast<std::int32_t>(offset))),
                    TextCell("Attributes", FormatFlagsEnum(
                        kManifestResourceAttributes,
                        sizeof(kManifestResourceAttributes)
                            / sizeof(kManifestResourceAttributes[0]),
                        file.CorTableColumnValue(table, r, 1))),
                    TextCell("Name", FormatStringColumn(file, table, r, 2)),
                    TextCell("Implementation", FormatEntity(
                        DecodeImplementation(
                            file.CorTableColumnValue(table, r, 3)))),
                }));
            }
            break;
        case CorTableIndex::NestedClass:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x29u << 24) | (r + 1), {
                    TextCell("NestedClass", FormatEntity(FormatRidToken(0x02,
                        file.CorTableColumnValue(table, r, 0)))),
                    TextCell("EnclosingClass", FormatEntity(
                        FormatRidToken(0x02,
                            file.CorTableColumnValue(table, r, 1)))),
                }));
            break;
        case CorTableIndex::GenericParam:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x2Au << 24) | (r + 1), {
                    TextCell("Number", std::to_string(
                        file.CorTableColumnValue(table, r, 0))),
                    TextCell("Attributes", FormatFlagsEnum(
                        kGenericParameterAttributes,
                        sizeof(kGenericParameterAttributes)
                            / sizeof(kGenericParameterAttributes[0]),
                        file.CorTableColumnValue(table, r, 1))),
                    TextCell("Owner", FormatEntity(
                        DecodeTypeOrMethodDef(
                            file.CorTableColumnValue(table, r, 2)))),
                    TextCell("Name", FormatStringColumn(file, table, r, 3)),
                }));
            break;
        case CorTableIndex::MethodSpec:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x2Bu << 24) | (r + 1), {
                    TextCell("Method", FormatEntity(
                        DecodeMethodDefOrRef(
                            file.CorTableColumnValue(table, r, 0)))),
                    TextCell("Instantiation",
                        FormatBlobColumn(file, table, r, 1)),
                }));
            break;
        case CorTableIndex::GenericParamConstraint:
            for (std::uint32_t r = 0; r < count; ++r)
                rows.push_back(BuildRow(r + 1, (0x2Cu << 24) | (r + 1), {
                    TextCell("Owner", FormatEntity(FormatRidToken(0x2A,
                        file.CorTableColumnValue(table, r, 0)))),
                    TextCell("Constraint", FormatEntity(
                        DecodeTypeDefOrRef(
                            file.CorTableColumnValue(table, r, 1)))),
                }));
            break;
        case CorTableIndex::FieldPtr:
        case CorTableIndex::MethodPtr:
        case CorTableIndex::ParamPtr:
        case CorTableIndex::EventPtr:
        case CorTableIndex::PropertyPtr:
            // The *Ptr indirection tables carry no winmd model (a file with
            // rows there fails to open), so the count reads 0 -- the same
            // "0 rows" the C# prints for every compiler-produced assembly.
            break;
        default:
            // The C# default arm: a table listed as supported but without a
            // loader (unreachable -- the supported set above is exhaustive).
            throw std::logic_error("the table is listed as supported but has no loader");
    }
    return rows;
}

// ---------------------------------------------------------------------------
// The console table (the C# WriteConsoleTable)
// ---------------------------------------------------------------------------

std::string CellText(const Cell& cell) {
    // Convert.ToString(value, Invariant) -- the decimal form for the int.
    return cell.IsInt ? std::to_string(cell.IntValue) : cell.StrValue;
}

void WriteLine(std::ostream& output, const std::string& line) {
    output << line << "\r\n";
}

std::string TrimEnd(std::string s) {
    std::size_t end = s.find_last_not_of(" \t\r\n");
    return end == std::string::npos ? "" : s.substr(0, end + 1);
}

std::string PadRight(const std::string& s, std::size_t width) {
    // .NET PadRight counts chars; the values here are ASCII identifiers and
    // numbers, so the byte count agrees (a non-ASCII name pads by its byte
    // length -- the port's rendering tolerance).
    std::size_t length = s.size();
    return length >= width ? s
        : s + std::string(width - length, ' ');
}

void WriteConsoleTable(std::ostream& output, const std::vector<Row>& rows) {
    if (rows.empty()) {
        WriteLine(output, "0 rows");
        return;
    }
    // widths[i] = max(the column name length, every row's value length).
    std::size_t columnCount = rows[0].size();
    std::vector<std::size_t> widths(columnCount);
    for (std::size_t i = 0; i < columnCount; ++i)
        widths[i] = std::strlen(rows[0][i].Column);
    for (const Row& row : rows)
        for (std::size_t i = 0; i < row.size(); ++i)
            widths[i] = std::max(widths[i], CellText(row[i]).size());
    // The header line: the column names padded and trimmed.
    std::string header;
    for (std::size_t i = 0; i < columnCount; ++i) {
        if (i > 0) header += "  ";
        header += PadRight(rows[0][i].Column, widths[i]);
    }
    WriteLine(output, TrimEnd(header));
    // The dashes line: one run of the width per column, NOT trimmed.
    std::string dashes;
    for (std::size_t i = 0; i < columnCount; ++i) {
        if (i > 0) dashes += "  ";
        dashes += std::string(widths[i], '-');
    }
    WriteLine(output, dashes);
    for (const Row& row : rows) {
        std::string line;
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i > 0) line += "  ";
            line += PadRight(CellText(row[i]), widths[i]);
        }
        WriteLine(output, TrimEnd(line));
    }
}

// ---------------------------------------------------------------------------
// The JSON document (the C# WriteJson over Utf8JsonWriter Indented)
// ---------------------------------------------------------------------------

// The JavaScriptEncoder.Default escaping (probed against .NET 10's
// Utf8JsonWriter over the same writer shape the dumper uses): the quote,
// backslash, and the HTML-sensitive <, >, &, ', plus, backtick, and DEL
// escape as their \u00XX forms (the quote and backslash included -- the
// quote is NOT the \" short form), the control characters as \u00XX
// except the five short forms (\b, \t, \n, \f, \r), and non-ASCII passes
// through unescaped.
std::string JsonEscape(const std::string& s) {
    std::string result;
    result.reserve(s.size() + 8);
    char buf[8];
    for (unsigned char c : s) {
        switch (c) {
            case '"': result += "\\u0022"; break;
            case '\\': result += "\\\\"; break;
            case '<': result += "\\u003C"; break;
            case '>': result += "\\u003E"; break;
            case '&': result += "\\u0026"; break;
            case '\'': result += "\\u0027"; break;
            case '+': result += "\\u002B"; break;
            case '`': result += "\\u0060"; break;
            case 0x7F: result += "\\u007F"; break;
            case '\b': result += "\\b"; break;
            case '\t': result += "\\t"; break;
            case '\n': result += "\\n"; break;
            case '\f': result += "\\f"; break;
            case '\r': result += "\\r"; break;
            default:
                if (c < 0x20) {
                    std::snprintf(buf, sizeof(buf), "\\u%04X", (int)c);
                    result += buf;
                } else {
                    result += (char)c;
                }
                break;
        }
    }
    return result;
}

std::string JsonString(const std::string& s) {
    return "\"" + JsonEscape(s) + "\"";
}

// The table's enum name ("Module", "TypeDef", ...) -- the C#
// `table.ToString()` (the supported ids all carry named members).
const char* TableName(CorTableIndex table) {
    switch (table) {
        case CorTableIndex::Module: return "Module";
        case CorTableIndex::TypeRef: return "TypeRef";
        case CorTableIndex::TypeDef: return "TypeDef";
        case CorTableIndex::FieldPtr: return "FieldPtr";
        case CorTableIndex::Field: return "Field";
        case CorTableIndex::MethodPtr: return "MethodPtr";
        case CorTableIndex::MethodDef: return "MethodDef";
        case CorTableIndex::ParamPtr: return "ParamPtr";
        case CorTableIndex::Param: return "Param";
        case CorTableIndex::InterfaceImpl: return "InterfaceImpl";
        case CorTableIndex::MemberRef: return "MemberRef";
        case CorTableIndex::Constant: return "Constant";
        case CorTableIndex::CustomAttribute: return "CustomAttribute";
        case CorTableIndex::FieldMarshal: return "FieldMarshal";
        case CorTableIndex::DeclSecurity: return "DeclSecurity";
        case CorTableIndex::ClassLayout: return "ClassLayout";
        case CorTableIndex::FieldLayout: return "FieldLayout";
        case CorTableIndex::StandAloneSig: return "StandAloneSig";
        case CorTableIndex::EventMap: return "EventMap";
        case CorTableIndex::EventPtr: return "EventPtr";
        case CorTableIndex::Event: return "Event";
        case CorTableIndex::PropertyMap: return "PropertyMap";
        case CorTableIndex::PropertyPtr: return "PropertyPtr";
        case CorTableIndex::Property: return "Property";
        case CorTableIndex::MethodSemantics: return "MethodSemantics";
        case CorTableIndex::MethodImpl: return "MethodImpl";
        case CorTableIndex::ModuleRef: return "ModuleRef";
        case CorTableIndex::TypeSpec: return "TypeSpec";
        case CorTableIndex::ImplMap: return "ImplMap";
        case CorTableIndex::FieldRva: return "FieldRva";
        case CorTableIndex::Assembly: return "Assembly";
        case CorTableIndex::AssemblyRef: return "AssemblyRef";
        case CorTableIndex::File: return "File";
        case CorTableIndex::ExportedType: return "ExportedType";
        case CorTableIndex::ManifestResource: return "ManifestResource";
        case CorTableIndex::NestedClass: return "NestedClass";
        case CorTableIndex::GenericParam: return "GenericParam";
        case CorTableIndex::MethodSpec: return "MethodSpec";
        case CorTableIndex::GenericParamConstraint: return "GenericParamConstraint";
        default: throw std::logic_error("the table carries no name");
    }
}

void WriteJson(std::ostream& output, const std::string& assemblyFileName,
    CorTableIndex table, const std::vector<Row>& rows) {
    // The Utf8JsonWriter(Indented) layout on .NET 10: two-space indentation,
    // CRLF line breaks, ": " after every name, an empty rows array on one
    // line; the WriteNumber arm for the int RID cell, the WriteString arm
    // for every formatted string.
    std::string json;
    json += "{\r\n";
    json += "  \"assembly\": " + JsonString(assemblyFileName) + ",\r\n";
    json += "  \"table\": " + JsonString(TableName(table)) + ",\r\n";
    json += "  \"rowCount\": " + std::to_string(rows.size()) + ",\r\n";
    if (rows.empty()) {
        json += "  \"rows\": []\r\n";
    } else {
        json += "  \"rows\": [\r\n";
        for (std::size_t r = 0; r < rows.size(); ++r) {
            json += "    {\r\n";
            for (std::size_t c = 0; c < rows[r].size(); ++c) {
                const Cell& cell = rows[r][c];
                std::string value = cell.IsInt
                    ? std::to_string(cell.IntValue)
                    : JsonString(cell.StrValue);
                json += std::string("      \"") + JsonEscape(cell.Column)
                    + "\": " + value;
                json += c + 1 < rows[r].size() ? ",\r\n" : "\r\n";
            }
            json += "    }";
            json += r + 1 < rows.size() ? ",\r\n" : "\r\n";
        }
        json += "  ]\r\n";
    }
    json += "}\r\n";
    // The C# output.WriteLine(string) -- the document bytes plus one final
    // newline.
    output << json;
}

} // namespace

// ---------------------------------------------------------------------------
// The public surface
// ---------------------------------------------------------------------------

const std::vector<CorTableIndex>& SupportedTables() {
    // The C# supportedTables declaration order.
    static const std::vector<CorTableIndex> kTables = {
        CorTableIndex::Module,
        CorTableIndex::TypeRef,
        CorTableIndex::TypeDef,
        CorTableIndex::FieldPtr,
        CorTableIndex::Field,
        CorTableIndex::MethodPtr,
        CorTableIndex::MethodDef,
        CorTableIndex::ParamPtr,
        CorTableIndex::Param,
        CorTableIndex::InterfaceImpl,
        CorTableIndex::MemberRef,
        CorTableIndex::Constant,
        CorTableIndex::CustomAttribute,
        CorTableIndex::FieldMarshal,
        CorTableIndex::DeclSecurity,
        CorTableIndex::ClassLayout,
        CorTableIndex::FieldLayout,
        CorTableIndex::StandAloneSig,
        CorTableIndex::EventMap,
        CorTableIndex::EventPtr,
        CorTableIndex::Event,
        CorTableIndex::PropertyMap,
        CorTableIndex::PropertyPtr,
        CorTableIndex::Property,
        CorTableIndex::MethodSemantics,
        CorTableIndex::MethodImpl,
        CorTableIndex::ModuleRef,
        CorTableIndex::TypeSpec,
        CorTableIndex::ImplMap,
        CorTableIndex::FieldRva,
        CorTableIndex::Assembly,
        CorTableIndex::AssemblyRef,
        CorTableIndex::File,
        CorTableIndex::ExportedType,
        CorTableIndex::ManifestResource,
        CorTableIndex::NestedClass,
        CorTableIndex::GenericParam,
        CorTableIndex::MethodSpec,
        CorTableIndex::GenericParamConstraint,
    };
    return kTables;
}

std::string SupportedTableNames() {
    std::string result;
    for (CorTableIndex t : SupportedTables()) {
        if (!result.empty()) result += ", ";
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%02X",
            static_cast<unsigned>(static_cast<std::uint32_t>(t)));
        result += std::string(TableName(t)) + " (" + buf + ")";
    }
    return result;
}

bool TryParseTableName(const std::string& name, CorTableIndex& table) {
    // The numeric forms first: a 0x/0X-prefixed hex number, else a plain
    // decimal (NumberStyles.None -- no signs or whitespace). The HexNumber
    // style also accepts a second 0x prefix inside the digits, which the
    // strtoul base-16 parse reproduces.
    auto parseNumber = [](const std::string& text, int base) -> int {
        if (text.empty()) return -1;
        // .NET TryParse allows leading/trailing whitespace.
        std::size_t begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) return -1;
        std::size_t end = text.find_last_not_of(" \t\r\n");
        std::string s = text.substr(begin, end - begin + 1);
        if (base == 10) {
            for (char c : s)
                if (c < '0' || c > '9') return -1;
        }
        char* parseEnd = nullptr;
        unsigned long value = std::strtoul(s.c_str(), &parseEnd, base);
        if (parseEnd == nullptr || *parseEnd != '\0' || value > 0x7FFFFFFF)
            return -1;
        return (int)value;
    };
    int number = -1;
    if (name.size() >= 2 && (name[0] == '0')
        && (name[1] == 'x' || name[1] == 'X')) {
        number = parseNumber(name.substr(2), 16);
    } else {
        number = parseNumber(name, 10);
    }
    if (number >= 0) {
        table = static_cast<CorTableIndex>(number);
        for (CorTableIndex t : SupportedTables())
            if (t == table) return true;
        return false;
    }
    // The name form: the ECMA-335 table name, case-insensitive (the C#
    // Enum.TryParse trims leading/trailing whitespace before matching).
    std::size_t nameBegin = name.find_first_not_of(" \t\r\n");
    std::string trimmed = nameBegin == std::string::npos
        ? std::string() : name.substr(nameBegin,
            name.find_last_not_of(" \t\r\n") - nameBegin + 1);
    for (CorTableIndex t : SupportedTables()) {
        const char* candidate = TableName(t);
        if (trimmed.size() == std::strlen(candidate)) {
            bool equal = true;
            for (std::size_t i = 0; i < trimmed.size(); ++i) {
                char a = trimmed[i];
                char b = candidate[i];
                if (a >= 'A' && a <= 'Z') a = (char)(a | 32);
                if (b >= 'A' && b <= 'Z') b = (char)(b | 32);
                if (a != b) { equal = false; break; }
            }
            if (equal) {
                table = t;
                return true;
            }
        }
    }
    return false;
}

int DumpTable(const std::string& assemblyFileName, std::ostream& output,
    CorTableIndex table, bool asJson) {
    MetadataFile file(assemblyFileName);
    std::vector<Row> rows = LoadRows(file, table);
    if (asJson)
        WriteJson(output, assemblyFileName, table, rows);
    else
        WriteConsoleTable(output, rows);
    return 0;
}

} // namespace ILSpy::ILSpyCmd
