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

// Port of the `IAssemblyReference` interface and the `AssemblyNameReference`
// class from ICSharpCode.Decompiler/Metadata/AssemblyReferences.cs (lines
// 98-217) -- the parse-and-render pair over assembly full names
// ("mscorlib, Version=4.0.0.0, Culture=neutral, PublicKeyToken=b77a5c561934e089").
// `BamlContext.ResolveAssembly` parses a BAML AssemblyInfoRecord's full name and
// compares it against the compilation's modules; `BamlDecompilerTypeSystem`
// parses its default BAML reference list; both consume `Parse`/`Name`/`FullName`
// only.
//
// The remaining members of the C# file are documented deferrals, unported until
// a ported consumer reaches them:
//   * `ResolutionException` and `IAssemblyReferenceClassifier` /
//     `AssemblyReferenceClassifier` (the UniversalAssemblyResolver / GAC lookup
//     machinery -- the resolver itself is a Phase-7 deferral),
//   * `TypeReferenceMetadata` and `ExportedTypeMetadata` (the classifier's
//     lazy per-reference row collections).
//
// The metadata-backed `AssemblyReference` row wrapper class (the third
// `IAssemblyReference` implementation, AssemblyReferences.cs lines 219-330)
// IS ported (below): its named prerequisites (`GetFullAssemblyName`,
// `Sha1ForNonSecretPurposes`) landed with the strong-name slice, and its
// consumer `MetadataModule.ResolveModule` (through
// `ICompilation.FindModuleByReference`) drives it over the raw Cor-table
// surface.
//
// `IAssemblyResolver` (the resolver interface `BamlDecompilerTypeSystem`'s ctor
// takes) ports its two synchronous members below; the two `Task`-returning
// members stay deferred -- the port is synchronous throughout, and no ported
// caller reaches them.
//
// Every behavior pinned against the real .NET classes: the parse matrix, the
// `FullName` render, and each exception message were dumped from the installed
// ilspycmd 11.0 tool's ICSharpCode.Decompiler.dll and the .NET 10 runtime (the
// C:\temp-probe\AnrProbe probe).

#pragma once

#include "Decompiler/TypeSystem/Version.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;

// The C# `IAssemblyReference` interface -- the name identity of a referenced
// assembly. The string properties are non-null; `Version` / `Culture` /
// `PublicKeyToken` are nullable (the C# `Version?` / `string?` / `byte[]?` --
// the nullable-array state is observable, an EMPTY `PublicKeyToken` renders
// "null" in a full name while still being a found value, so it ports to
// `std::optional<std::vector<std::uint8_t>>`).
class IAssemblyReference {
public:
    virtual ~IAssemblyReference() = default;

    // The C# `string Name { get; }` -- the assembly short name (non-null).
    virtual std::string Name() const = 0;

    // The C# `string FullName { get; }` -- the full assembly name (non-null).
    virtual std::string FullName() const = 0;

    // The C# `Version? Version { get; }` -- the assembly version, or null. The
    // getter is named after its return type (the `ITypeDefinition::KnownTypeCode`
    // D-name-hiding precedent); every later `Version` type mention inside an
    // implementor must be namespace-qualified (`TypeSystem::Version`) because
    // the member name hides the type name in class scope.
    virtual std::optional<TypeSystem::Version> Version() const = 0;

    // The C# `string? Culture { get; }` -- the culture, or null.
    virtual std::optional<std::string> Culture() const = 0;

    // The C# `byte[]? PublicKeyToken { get; }` -- the public key token bytes, or
    // null.
    virtual std::optional<std::vector<std::uint8_t>> PublicKeyToken() const = 0;

    // The C# `bool IsWindowsRuntime { get; }`.
    virtual bool IsWindowsRuntime() const = 0;

    // The C# `bool IsRetargetable { get; }`.
    virtual bool IsRetargetable() const = 0;
};

// The C# `IAssemblyResolver` interface -- resolves an assembly reference (or a
// module reference of a main module) to a loaded `MetadataFile`, or null when
// not found. Only the two synchronous members port; the `ResolveAsync` /
// `ResolveModuleAsync` pair stays deferred (no `Task` analogue in the port, and
// no ported caller reaches them).
class IAssemblyResolver {
public:
    virtual ~IAssemblyResolver() = default;

    // The C# `MetadataFile? Resolve(IAssemblyReference reference)`.
    virtual const MetadataFile* Resolve(const IAssemblyReference& reference) const = 0;

    // The C# `MetadataFile? ResolveModule(MetadataFile mainModule, string moduleName)`.
    virtual const MetadataFile* ResolveModule(
        const MetadataFile& mainModule, const std::string& moduleName) const = 0;
};

// The C# `AssemblyNameReference : IAssemblyReference` -- a parsed assembly full
// name. `Parse` splits on ',' (the first token, trimmed, is the name; every
// later token must be a `key=value` pair with the key case-insensitively one
// of version/culture/publickeytoken -- anything else is either ignored or the
// "Malformed name" ArgumentException), and `FullName` re-renders the canonical
// form with `Version=` / `Culture=` / `PublicKeyToken=` (plus
// `Retargetable=Yes` -- dead for parsed instances: `Parse` never sets the flag).
class AssemblyNameReference final : public IAssemblyReference {
public:
    // The C# `static AssemblyNameReference Parse(string fullName)`. The null
    // argument of the C# has no `std::string` counterpart -- the C#'s
    // `ArgumentNullException` is unreachable and an empty string takes the
    // `ArgumentException` ("Name can not be empty") arm (the
    // `DefaultAssemblyReference` empty-string convention). Throws
    // `std::invalid_argument` for the C# `ArgumentException` /
    // `FormatException` arms, all with the exact probed messages.
    static AssemblyNameReference Parse(const std::string& fullName);

    // The C# `override string ToString()` -- the `FullName` form.
    std::string ToString() const;

    std::string Name() const override { return name_; }
    std::string FullName() const override;

    std::optional<TypeSystem::Version> Version() const override { return version_; }
    std::optional<std::string> Culture() const override { return culture_; }
    std::optional<std::vector<std::uint8_t>> PublicKeyToken() const override {
        return publicKeyToken_;
    }
    bool IsWindowsRuntime() const override { return isWindowsRuntime_; }
    bool IsRetargetable() const override { return isRetargetable_; }

private:
    // The C# `string Name { get; private set; } = string.Empty;` -- the
    // initializer ports to the in-class default.
    std::string name_;
    std::optional<TypeSystem::Version> version_;
    std::optional<std::string> culture_;
    std::optional<std::vector<std::uint8_t>> publicKeyToken_;
    // The C# `bool IsWindowsRuntime/IsRetargetable { get; private set; }` pair
    // is never assigned by `Parse` (the C# switch has no case for either), so a
    // parsed reference always reports false; the metadata-backed
    // `AssemblyReference` class is the one that reads them from flags.
    bool isWindowsRuntime_ = false;
    bool isRetargetable_ = false;
    // The C# lazy `fullName` cache (mutable so the const `FullName` getter can
    // fill it).
    mutable std::optional<std::string> fullName_;
};

// The C# `public class AssemblyReference : IAssemblyReference`
// (AssemblyReferences.cs lines 219-330) -- the metadata-backed
// `IAssemblyReference` over ONE AssemblyRef row: the lazy Name/FullName pair
// with the corrupt-row catch fallbacks, the raw version/culture columns, and
// the public-key-token derivation (the plain bytes, or the SHA-1 last-8-bytes
// when the row's Flags carry `PublicKey`). `MetadataModule.ResolveModule`
// constructs one per resolution and `ICompilation.FindModuleByReference`
// consumes only `Name`/`FullName`, but the whole `IAssemblyReference` surface
// ports (the classifier's `TypeReferences`/`ExportedTypes` lazy collections
// stay deferred with the classifier).
//
// KEY PORT CONVENTIONS:
//  (a) The C# ctor `(MetadataReader metadata, AssemblyReferenceHandle
//      handle)` with the null-metadata `ArgumentNullException` ports to the
//      (file, token) pair over the raw Cor-table surface: a C++ reference
//      cannot be null (the guard is structurally satisfied), and the reads go
//      through the THROWING raw reads (`CorTableColumnValue`/`CorString`/
//      `CorBlob`) so a corrupt row reproduces the C# `BadImageFormatException`
//      arms the catch fallbacks below take (the iteration-63 raw-surface
//      convention -- the degrading never-throw reads would silently mask the
//      fallbacks).
//  (b) The C# catch fallback strings interpolate the HANDLE struct's
//      `ToString()`, which is the SRM type name (no handle override):
//      `$"AR:{Handle}"` renders "AR:System.Reflection.Metadata."
//      "AssemblyReferenceHandle" -- the port carries the rendered literal (the
//      gold-pinned corrupt-row behavior; the C#-only interpolation source has
//      no C++ counterpart).
//  (c) `GetPublicKeyToken` has NO try/catch in the C# -- a corrupt
//      PublicKeyOrToken blob index throws out of it (unlike Name/FullName,
//      whose catch produces the fallback strings); the port propagates the raw
//      surface's `std::invalid_argument`/`std::out_of_range` unchanged
//      (the documented exception-family divergence: the C# surfaces
//      `BadImageFormatException` from the SRM memory-block read).
//  (d) `Version`/`Culture` read the fixed-width/raw columns: the C#
//      `Version?` is the non-nullable SRM `Version` struct (always present,
//      the raw column values) and `Culture` is `GetString(entry.Culture)` --
//      "" for the nil column, never null. The port keeps both always-engaged
//      optionals (the interface's nullable shape is preserved for the OTHER
//      implementations).
class AssemblyReference final : public IAssemblyReference {
public:
    // The C# ctor (convention (a)). The token's low 24 bits are the 1-based
    // AssemblyRef row number; row 0 (the nil handle) reads a default row in the
    // C# and the C# name read throws -- the port's underflowed row index hits
    // the raw bounds check and takes the same catch arm.
    AssemblyReference(const MetadataFile& file, std::uint32_t token);

    // The C# `string Name` -- the row's Name column through the throwing raw
    // read, the `AR:{Handle}` fallback on the corrupt-row catch (convention
    // (b)), the LazyInit caching of whichever value the first read produced.
    std::string Name() const override;
    // The C# `string FullName` -- `entry.GetFullAssemblyName(Metadata)` (the
    // iteration-63 AssemblyRef-row extension), the `fullname(AR:{Handle})`
    // fallback, the same lazy caching.
    std::string FullName() const override;
    // The C# `Version? Version => entry.Version` -- the merged 8-byte version
    // column as the four components (convention (d)).
    std::optional<TypeSystem::Version> Version() const override;
    // The C# `string Culture => Metadata.GetString(entry.Culture)` -- the raw
    // column string, "" for the nil offset (convention (d)).
    std::optional<std::string> Culture() const override;
    // The C# explicit `byte[]? IAssemblyReference.PublicKeyToken =>
    // GetPublicKeyToken()`.
    std::optional<std::vector<std::uint8_t>> PublicKeyToken() const override
    {
        return GetPublicKeyToken();
    }
    // The C# `bool IsWindowsRuntime => (entry.Flags &
    // AssemblyFlags.WindowsRuntime) != 0` and `IsRetargetable`.
    bool IsWindowsRuntime() const override;
    bool IsRetargetable() const override;

    // The C# `public byte[]? GetPublicKeyToken()`: the nil PublicKeyOrToken
    // column -> null; the PublicKey-flag arm -> the SHA-1 digest's LAST 8
    // bytes (the C# `hash.Skip(12)` over the 20-byte digest); otherwise the
    // blob bytes verbatim. Lazy-cached; THROWS for a corrupt blob index
    // (convention (c)).
    std::optional<std::vector<std::uint8_t>> GetPublicKeyToken() const;

private:
    const MetadataFile* file_;
    std::uint32_t token_;
    // The C# lazy `string? name` / `string? fullName` / `byte[]?
    // publicKeyToken` caches (mutable so the const getters can fill them).
    mutable std::optional<std::string> name_;
    mutable std::optional<std::string> fullName_;
    mutable std::optional<std::vector<std::uint8_t>> publicKeyToken_;
};

} // namespace ILSpy::Decompiler::Metadata
