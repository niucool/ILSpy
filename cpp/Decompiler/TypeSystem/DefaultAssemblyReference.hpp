// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the `DefaultAssemblyReference` class from
// ICSharpCode.Decompiler/TypeSystem/Implementation/DefaultAssemblyReference.cs --
// the `sealed class DefaultAssemblyReference : IModuleReference, ISupportsInterning`
// that references an existing assembly by its short name. A `DefaultAssemblyReference`
// is constructed from a full assembly name (e.g. `"mscorlib, Version=4.0.0.0"`),
// extracts the short name (the prefix before the first `','`), and resolves by
// matching that short name -- case-insensitively -- against
// `context.CurrentModule` first, then each module in `context.Compilation.Modules`,
// returning the matched `IModule` or null when no module matches.
//
// It is the ONLY concrete `IModuleReference` (the D418 interface port), the
// module-reference counterpart of the concrete `ITypeReference` siblings
// (`KnownTypeReference` D411 / `ByReferenceTypeReference` D413 / `PointerTypeReference`
// D414 / `ArrayTypeReference` D415 / `ParameterizedTypeReference` D416 /
// `NestedTypeReference` D417). It is a leaf TypeSystem dependency toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker of
// `CSharpAmbience`); the `InterningProvider` (not yet ported) deduplicates
// structurally-equal references via the `ISupportsInterning` accessors.
//
// KEY PORT CONVENTIONS:
// (a) The C# `sealed class DefaultAssemblyReference : IModuleReference,
// ISupportsInterning` ports to a C++ `final` class multiply-inheriting
// `IModuleReference` (D418) and `ISupportsInterning` (D412). The two bases share no
// common base (neither derives from the other, and neither derives from a shared
// third), so there is NO diamond and NO `Name`/`SymbolKind` redeclaration -- the
// cleanest multiple-inheritance shape (the D413 `ByReferenceTypeReference` precedent).
// (b) The C# `readonly string shortName` field (set once in the ctor from the
// assembly-name prefix) ports to a `std::string` member. The ctor takes the full
// assembly name by value (`std::string`), finds the first `','`, and keeps the
// prefix (or the whole string when no comma). The C# ctor accepts a null
// `assemblyName` (the `IndexOf` is guarded by `assemblyName != null`, and a null
// yields `shortName = null`); the C++ `std::string` has no null state, so an empty
// string is the faithful counterpart (the `find` on an empty string yields `npos`,
// so `shortName_` stays empty).
// (c) The C# `IModule Resolve(ITypeResolveContext context)` checks
// `context.CurrentModule` first (a case-insensitive `string.Equals(shortName,
// current.AssemblyName, StringComparison.OrdinalIgnoreCase)`), then iterates
// `context.Compilation.Modules` (the same case-insensitive match), returning the
// matched `IModule` or null. The C# `IModule?` (nullable) ports to `const IModule*`
// (nullable pointer, the `IEntity::ParentModule` precedent); the case-insensitive
// match ports to `StringComparer::OrdinalIgnoreCase().Equals(shortName_,
// module->AssemblyName())` (the D398 `StringComparer::OrdinalIgnoreCase` ASCII
// case fold, the faithful C++ counterpart of `StringComparison.OrdinalIgnoreCase`
// for the ASCII identifier range assembly names live in). `Resolve` is `const`
// (reads the reference without mutating, the D408/D418 const-`Resolve` precedent).
// (d) The C# `int ISupportsInterning.GetHashCodeForInterning() => shortName.GetHashCode()`
// uses the STRING hash (NOT the identity hash -- the structural distinction from the
// D413-D417 concrete `ITypeReference` siblings whose `GetHashCodeForInterning` XORs
// the shared_ptr IDENTITY hash with a salt). The C++ port returns
// `static_cast<int>(std::hash<std::string>{}(shortName_))` -- the string hash
// truncated to `int` (the D398 `StringComparer::GetHashCode` `static_cast<int>`
// precedent), faithful to the C# `shortName.GetHashCode()` (the string's ordinal hash).
// (e) The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other) =>
// (other as DefaultAssemblyReference) is { } o && shortName == o.shortName` uses
// ORDINAL (case-SENSITIVE) string equality (`==`), NOT the case-insensitive match
// `Resolve` uses -- a C# source distinction: the interning equality is
// case-sensitive (two references with different-cased short names are NOT interned
// together), while the resolution match is case-insensitive. The C++ port downcasts
// via `dynamic_cast<const DefaultAssemblyReference*>(&other)` (the D412 `as`-returns-
// null precedent) and compares `shortName_ == o->shortName_` (`std::string::operator==`,
// byte-wise case-sensitive, the faithful C# `==`).
// (f) The C# `override string ToString() => shortName` returns the short name. Unlike
// the D413-D417 concrete `ITypeReference` siblings whose `ToString` is DEFERRED (they
// need a polymorphic element `ToString` the `ITypeReference` interface does not
// declare), `DefaultAssemblyReference.ToString` returns its own `shortName_` member
// with no polymorphic dependency, so it is ported as a non-virtual `std::string
// ToString() const` accessor (there is no polymorphic `ToString` on `IModuleReference`
// to override; the method is a concrete-class convenience the C# `Object.ToString`
// override provides, ported as a regular member here).
// (g) The C# `public static readonly IModuleReference CurrentAssembly = new
// CurrentModuleReference()` is a static field holding a `CurrentModuleReference`
// (the nested `sealed class CurrentModuleReference : IModuleReference` whose
// `Resolve` returns `context.CurrentModule` or throws `ArgumentException` when the
// current module is null -- the "reference to the current assembly" that resolves
// against whatever module the context is resolving within). The C++ port provides a
// `static const IModuleReference& CurrentAssembly()` accessor returning a
// function-local static `CurrentModuleReference` (the Meyers-singleton pattern, the
// D398 `StringComparer::Ordinal` precedent); the `CurrentModuleReference` is a
// private nested `final : IModuleReference` class (NOT `ISupportsInterning` -- the
// C# `CurrentModuleReference` does not implement it, so the current-assembly
// reference is NOT interned). Its `Resolve` throws `std::invalid_argument` when
// `context.CurrentModule()` is null (the C# `ArgumentException` counterpart, the
// faithful "cannot be resolved in the global context" error).

#pragma once

#include "Decompiler/TypeSystem/IModuleReference.hpp"
#include "Decompiler/TypeSystem/ISupportsInterning.hpp"

#include <functional>
#include <string>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IModule` (the D396 port, the `Resolve` return type) and
// `ITypeResolveContext` (the D409 paired context interface, the `Resolve` parameter
// type). Both are already forward-declared in `IModuleReference.hpp` (D418) but are
// re-declared here so this header is self-contained for readers; a nullable pointer
// return and a reference parameter each need only a forward declaration (the
// `ITypeReference.hpp` (D408) both-forward-declared precedent), so this header
// compiles with both incomplete.
class IModule;
class ITypeResolveContext;

// A reference to an existing assembly by its short name. Constructed from a full
// assembly name, it extracts the short name (the prefix before the first `','`)
// and resolves by matching that short name -- case-insensitively -- against the
// current module first, then the compilation's modules, returning the matched
// `IModule` or null when no module matches. The C# `sealed class` counterpart; it
// is `final` here, deriving from both `IModuleReference` (the resolve contract) and
// `ISupportsInterning` (the interning-deduplication contract).
class DefaultAssemblyReference final : public IModuleReference,
                                       public ISupportsInterning {
public:
    // The C# `DefaultAssemblyReference(string assemblyName)` -- extracts the short
    // name (the prefix before the first `','`) from the full assembly name. When no
    // comma is present the whole name is the short name. The C# ctor accepts a null
    // `assemblyName` (yielding a null `shortName`); the C++ `std::string` has no null
    // state, so an empty string is the faithful counterpart.
    explicit DefaultAssemblyReference(std::string assemblyName);

    // The C# `IModule Resolve(ITypeResolveContext context)` -- resolves this
    // reference by matching `shortName_` case-insensitively against
    // `context.CurrentModule` first, then each module in
    // `context.Compilation.Modules`, returning the matched `IModule` or null when no
    // module matches. `const` (reads the reference without mutating); the nullable C#
    // `IModule?` return ports to a nullable `const IModule*`.
    const IModule* Resolve(const ITypeResolveContext& context) const override;

    // The C# `override string ToString() => shortName` -- the short name. A
    // non-virtual concrete-class accessor (there is no polymorphic `ToString` on
    // `IModuleReference` to override); returns the `shortName_` member directly.
    std::string ToString() const
    {
        return shortName_;
    }

    // The C# `int ISupportsInterning.GetHashCodeForInterning() =>
    // shortName.GetHashCode()` -- the STRING hash of the short name (NOT the identity
    // hash the D413-D417 concrete `ITypeReference` siblings use -- the structural
    // distinction: `DefaultAssemblyReference` interns by VALUE, two references with
    // the same short name are interned together regardless of identity). `std::hash
    // <std::string>` hashes the string's bytes (the C# `string.GetHashCode` ordinal
    // hash); `static_cast<int>` truncates the `size_t` to the .NET `int` return.
    int GetHashCodeForInterning() const override
    {
        return static_cast<int>(std::hash<std::string>{}(shortName_));
    }

    // The C# `bool ISupportsInterning.EqualsForInterning(ISupportsInterning other)
    // => (other as DefaultAssemblyReference) is { } o && shortName == o.shortName` --
    // downcast via `dynamic_cast` (returns null on a type mismatch) and ORDINAL
    // (case-SENSITIVE) string equality on the short name. The case-sensitivity is the
    // C# source distinction from `Resolve`'s case-insensitive match: the interning
    // equality is exact (two references with different-cased short names are NOT
    // interned together), while the resolution match folds case.
    bool EqualsForInterning(const ISupportsInterning& other) const override
    {
        const auto* o = dynamic_cast<const DefaultAssemblyReference*>(&other);
        return o != nullptr && shortName_ == o->shortName_;
    }

    // The C# `public static readonly IModuleReference CurrentAssembly = new
    // CurrentModuleReference()` -- a static reference to the current assembly that
    // resolves against whatever module the context is resolving within. Returns a
    // `const IModuleReference&` to a function-local static `CurrentModuleReference`
    // (the Meyers-singleton pattern, the D398 `StringComparer::Ordinal` precedent).
    static const IModuleReference& CurrentAssembly();

    // The short name (the prefix before the first `','` of the full assembly name).
    // Exposed for testing and for consumers that need the short name without
    // resolving.
    const std::string& ShortName() const noexcept
    {
        return shortName_;
    }

private:
    std::string shortName_;

    // The C# `sealed class CurrentModuleReference : IModuleReference` nested class --
    // a reference to the current assembly that resolves by returning
    // `context.CurrentModule` (or throwing `ArgumentException` when the current
    // module is null). It does NOT implement `ISupportsInterning` (the C# source does
    // not), so the current-assembly reference is NOT interned. It is a private nested
    // `final : IModuleReference` class; the `CurrentAssembly()` static accessor
    // (a member of the enclosing `DefaultAssemblyReference`) constructs it.
    class CurrentModuleReference final : public IModuleReference {
    public:
        // The C# `IModule Resolve(ITypeResolveContext context)` -- returns
        // `context.CurrentModule`, or throws `ArgumentException` when the current
        // module is null (the "cannot be resolved in the compilation's global type
        // resolve context" error). `const` (reads the context without mutating).
        const IModule* Resolve(const ITypeResolveContext& context) const override;
    };
};

} // namespace ILSpy::Decompiler::TypeSystem
