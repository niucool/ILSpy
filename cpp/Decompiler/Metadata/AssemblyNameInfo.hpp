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

// Port of System.Reflection.Metadata's `AssemblyNameInfo` (the .NET 8+ public class
// describing an assembly display name, decompiled from the .NET 10.0.8 runtime's
// System.Reflection.Metadata.dll) together with the System.Runtime internals it
// composes: `System.Reflection.AssemblyNameParser` (the display-name tokenizer over
// name/value attribute lists) and `System.Reflection.AssemblyNameFormatter`
// (`AppendDisplayName`, the canonical `FullName` render). The decompiled
// System.Private.CoreLib 10.0.8 sources are the port's reference.
//
// Consumers: `TypeName` (this directory) parses the assembly part of every
// assembly-qualified type name through `TryParse`, and the upcoming
// `ReflectionHelper.ParseReflectionName` chain resolves the parsed name against a
// compilation's modules through `ICompilation.FindModuleByAssemblyNameInfo`
// (comparing `FullName`, then `Name`). `ToAssemblyName` (the System.Reflection
// bridge) has no ported consumer and stays deferred.
//
// Every behavior pinned against the real .NET 10 classes through the
// C:\temp-probe\TypeNameProbe public-API probe: the parse matrix (quoting,
// escapes, attribute ordering/duplication, the version-component sentinel
// collisions, the ProcessorArchitecture/ContentType/Retargetable flag bits),
// the `FullName` render (fixed segment order, `Culture=neutral` for empty,
// `PublicKeyToken=null` for empty tokens, the always-escaping
// `AppendQuoted`), and each exception message.
//
// Port conventions (documented divergences from the C#):
//  * The C# `Version?`/`string?`/`ImmutableArray<byte>` null-vs-unset
//    distinctions are load-bearing in `FullName`: `Version == null` renders no
//    `Version=` segment, `CultureName == null` renders no `Culture=` segment
//    while `""` renders `Culture=neutral`, and a default (unset) PublicKeyOrToken
//    renders no segment while an empty one renders `PublicKeyToken=null`. The port
//    carries all three as `std::optional` (`nullopt` = the C# null/default).
//  * Exception mapping (the standing repo convention): ArgumentException and
//    ArgumentNullException map to `std::invalid_argument`, carrying the exact
//    .NET message text (the separate `paramName` argument is dropped -- C++
//    exceptions have no parameter-name channel).
//  * The public surface takes and returns UTF-8 `std::string`; parsing and
//    rendering run over UTF-16 code units internally so the .NET unit view
//    (the `char.IsWhiteSpace`-driven trims, the escaping) is reproduced
//    exactly. (A C# string can carry lone surrogate halves; the port's UTF-8
//    boundary cannot express them, which is the boundary divergence noted for
//    every string-taking ported API.)

#pragma once

#include "Decompiler/TypeSystem/Version.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// The C# `enum AssemblyNameFlags` (System.Reflection) -- the raw attribute bits of
// an assembly name. Only `None`/`PublicKey`/`Retargetable` are enum members; the
// ProcessorArchitecture bits (4-6, mask 0x0070) and ContentType bits (9-11, mask
// 0x0E00) are raw flag bits the enum deliberately leaves unnamed (the gold probe
// prints them as plain numbers: `ProcessorArchitecture=MSIL` -> 16,
// `ContentType=WindowsRuntime` -> 512).
enum class AssemblyNameFlags : std::int32_t {
    None = 0x0000,
    PublicKey = 0x0001,
    Retargetable = 0x0100,
};

// The C# `enum AssemblyContentType` (System.Reflection) -- the content-type bits
// extracted from `AssemblyNameFlags` bits 9-11.
enum class AssemblyContentType : std::int32_t {
    Default = 0,
    WindowsRuntime = 1,
};

// The C# `enum ProcessorArchitecture` (System.Reflection) -- the architecture bits
// extracted from `AssemblyNameFlags` bits 4-6. `TryParseProcessorArchitecture`
// accepts msil/x86/ia64/amd64/arm (case-insensitively) and rejects everything
// else (`None` included).
enum class ProcessorArchitecture : std::int32_t {
    None = 0,
    MSIL = 1,
    X86 = 2,
    IA64 = 3,
    Amd64 = 4,
    Arm = 5,
};

// The C# `public sealed class AssemblyNameInfo` -- one parsed assembly display
// name ("mscorlib, Version=4.0.0.0, Culture=neutral,
// PublicKeyToken=b77a5c561934e089"). Instances are shared by value in C#; the
// port hands out `std::shared_ptr` so a `TypeName` tree can share one parsed
// `AssemblyNameInfo` across all its nodes exactly like the C# GC does.
class AssemblyNameInfo final {
public:
    // The C# public ctor: `AssemblyNameInfo(string name, Version? version = null,
    // string? cultureName = null, AssemblyNameFlags flags = AssemblyNameFlags.None,
    // ImmutableArray<byte> publicKeyOrToken = default)`. A null `name` throws
    // ArgumentNullException ("Value cannot be null. (Parameter 'name')"); the
    // port takes the name by value and throws `std::invalid_argument` for the
    // empty-string stand-in for null (no ported caller reaches that arm).
    AssemblyNameInfo(std::string name,
                     std::optional<TypeSystem::Version> version = std::nullopt,
                     std::optional<std::string> cultureName = std::nullopt,
                     AssemblyNameFlags flags = AssemblyNameFlags::None,
                     std::optional<std::vector<std::uint8_t>> publicKeyOrToken = std::nullopt);

    // The C# `string Name` -- the simple name, unescaped (the escape processing
    // happened in the tokenizer; the property value carries the literal text, so
    // a parsed `"My,Name"` or `My\,Name` yields "My,Name").
    const std::string& Name() const { return name_; }

    // The C# `Version? Version` -- null (`nullopt`) when no `Version=` attribute
    // was given. The ushort sentinel trick inside `TryParseVersion` makes a
    // component equal to 65535 read as "absent": `Version=1.2.65535` parses to
    // 1.2 and `Version=1.2.65535.65535` renders "1.2" (gold-pinned).
    const std::optional<TypeSystem::Version>& Version() const { return version_; }

    // The C# `string? CultureName` -- null (`nullopt`) when no `Culture=` attribute
    // was given; `Culture=neutral` (any case) yields the empty string, which
    // `FullName` renders back as `Culture=neutral`.
    const std::optional<std::string>& CultureName() const { return cultureName_; }

    // The C# `AssemblyNameFlags Flags` -- the raw bits (the PublicKey bit for a
    // `PublicKey=` attribute, the Retargetable bit, the raw architecture/content-type
    // bits; see the enum comment).
    AssemblyNameFlags Flags() const { return flags_; }

    // The C# `ImmutableArray<byte> PublicKeyOrToken` -- `nullopt` (the C#
    // default ImmutableArray) when no key attribute was given; an empty vector
    // for the explicit `null`/`""` spellings; the decoded bytes otherwise. The
    // PublicKey flag bit selects which segment `FullName` renders.
    const std::optional<std::vector<std::uint8_t>>& PublicKeyOrToken() const
    {
        return publicKeyOrToken_;
    }

    // The C# `string FullName` -- the canonical display name, lazily built by
    // `AssemblyNameFormatter.AppendDisplayName`: the (escaped) simple name, then
    // the specified segments in a FIXED order (`Version=`, `Culture=`,
    // `PublicKeyToken=`/`PublicKey=`, `Retargetable=Yes`, `ContentType=WindowsRuntime`)
    // regardless of the input order. Cached after the first read.
    const std::string& FullName() const;

    // The C# `static AssemblyNameInfo Parse(ReadOnlySpan<char> assemblyName)` --
    // ArgumentException ("The given assembly name was invalid. (Parameter
    // 'assemblyName')") on any parse failure, mapped to
    // `std::invalid_argument` carrying the same text.
    static std::shared_ptr<AssemblyNameInfo> Parse(std::string_view assemblyName);

    // The C# `static bool TryParse(ReadOnlySpan<char> assemblyName,
    // out AssemblyNameInfo? result)` -- the silent parse (no exception on
    // failure; `result` untouched on false). The empty input fails (the C#
    // `!assemblyName.IsEmpty` guard; the empty-throwing ctor is unreachable
    // from this path).
    static bool TryParse(std::string_view assemblyName,
                         std::shared_ptr<AssemblyNameInfo>& result);

    // `internal void AppendFullName(ref ValueStringBuilder)` -- the
    // `TypeName.AssemblyQualifiedName` render composes the assembly part through
    // this member (public in the port because C++ has no internal-assembly
    // channel; no other ported consumer reaches it). Appends the same text as
    // `FullName` into the UTF-16 builder (the ValueStringBuilder stand-in).
    void AppendFullName(std::u16string& builder) const;

private:
    std::string name_;
    std::optional<TypeSystem::Version> version_;
    std::optional<std::string> cultureName_;
    AssemblyNameFlags flags_;
    std::optional<std::vector<std::uint8_t>> publicKeyOrToken_;

    // The lazy `_fullName` cache (the C# field is a plain lazy string).
    mutable std::string fullName_;
    mutable bool fullNameComputed_ = false;
};

} // namespace ILSpy::Decompiler::Metadata
