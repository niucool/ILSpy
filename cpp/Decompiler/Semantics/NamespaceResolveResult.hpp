// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/NamespaceResolveResult.cs -- the
// `ResolveResult` (D424) for an expression that resolved to a namespace (e.g. a
// `using`-alias target or a namespace name used in a member-access expression). It
// is the ninth `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience`
// (the long-pole remaining blocker). The C# source declares:
//   * `NamespaceResolveResult(INamespace ns) : base(SpecialType.NoType)` -- the
//     ctor forwards `SpecialType.NoType` (the `TypeKind::None` null object, no
//     type at all) to the `ResolveResult` base and stores the namespace.
//   * `INamespace Namespace { get; }` -- the resolved namespace.
//   * `string NamespaceName { get; }` -- `ns.FullName` (the dotted full name).
//   * `override string ToString() => string.Format(..., "[{0} {1}]", GetType().Name, ns)`.
//
// The `SpecialType.NoType` dependency (the `TypeKind::None` special-type null
// object) was the prerequisite the D427/D432 next-in-order analysis flagged: the
// C++ minimal `IType` port had only the `UnknownType()` convenience
// (`SpecialType(TypeKind::Unknown)`), NOT a `NoType()` (`SpecialType(TypeKind::None)`).
// This port adds the `NoType()` convenience to `IType.hpp` (the D429
// `IsReferenceType` prerequisite-leaf precedent applied to the `NoType` accessor)
// and ports `NamespaceResolveResult` on top of it, unblocking
// `ThrowResolveResult` (the other `SpecialType.NoType`-dependent subclass).
//
// KEY PORT CONVENTIONS:
//  * The C# `INamespace ns` (a reference-type parameter the ctor does NOT guard
//    with `ArgumentNullException` and the accessors dereference with NO `?.`)
//    ports to a raw `const INamespace*` non-owning pointer (the namespace is owned
//    by the compilation's namespace tree, not by the `ResolveResult`). The
//    `D354` no-`?.` convention: the C# dereferences `ns` with no null-conditional,
//    so the C++ port dereferences `ns_` directly with no null guard (the tests
//    pass a non-null namespace; a null would be UB matching the C# NPE).
//  * The C# `ToString` uses `ns` (formatted via `ns.ToString()`) -- but the C++
//    `INamespace` interface does NOT expose a `ToString()` virtual (the D422
//    `MergedNamespace` non-virtual-`ToString` convention: `INamespace` / its bases
//    declare no `ToString` virtual, so the C# `override string ToString()` has no
//    C++ virtual counterpart). The faithful C++ port uses `ns_->FullName()` (the
//    closest meaningful representation the C# `ns.ToString()` includes for a real
//    namespace) -- a documented deviation from the C# (which would embed the
//    concrete `INamespace` impl's `ToString()` output, e.g.
//    `"[MergedNamespace ...]"` for a `MergedNamespace`).
//  * The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
//    runtime type; the C++ override constructs a `NamespaceResolveResult` copy
//    (the default copy ctor copies the raw `ns_` pointer, faithful to the C#
//    reference-copy -- both the original and the clone point to the same
//    compilation-owned `INamespace`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_NAMESPACERESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_NAMESPACERESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class NamespaceResolveResult : ResolveResult` (NOT `sealed` --
// no C# subclass derives from it but it is unsealed) ports to a C++ subclass
// (NOT `final`) of `ResolveResult`.
class NamespaceResolveResult : public ResolveResult {
public:
    // The C# `NamespaceResolveResult(INamespace ns) : base(SpecialType.NoType)`:
    // forwards `SpecialType.NoType` (the `TypeKind::None` null object) to the
    // `ResolveResult` base ctor and stores the namespace as a non-owning raw
    // pointer. The C# does NOT guard `ns` with `ArgumentNullException` and
    // dereferences it with no `?.`, so the C++ port holds it as a raw pointer
    // with no null guard (the `D354` no-`?.` convention).
    explicit NamespaceResolveResult(const ILSpy::Decompiler::TypeSystem::INamespace* ns)
        : ResolveResult(ILSpy::Decompiler::TypeSystem::NoType()), ns_(ns) {}

    // The C# `INamespace Namespace { get; }` -- the resolved namespace (non-owning
    // raw pointer, the namespace is owned by the compilation's namespace tree).
    const ILSpy::Decompiler::TypeSystem::INamespace* Namespace() const { return ns_; }

    // The C# `string NamespaceName { get; } => ns.FullName` -- the dotted full
    // name (e.g. "System.Collections"). Dereferences `ns_` directly (the C# has
    // no `?.`; the tests pass a non-null namespace).
    std::string NamespaceName() const { return ns_->FullName(); }

    // The C# `override string ToString() => string.Format(..., "[{0} {1}]",
    // GetType().Name, ns)`. The C# `{1}` formats `ns` via `ns.ToString()`, but
    // the C++ `INamespace` interface does NOT expose a `ToString()` virtual (the
    // D422 non-virtual-`ToString` convention), so the faithful C++ port uses
    // `ns_->FullName()` (the closest meaningful representation the C#
    // `ns.ToString()` includes for a real namespace) -- a documented deviation.
    std::string ToString() const override {
        return "[" + ClassName() + " " + ns_->FullName() + "]";
    }

protected:
    // The C# `ToString` uses `GetType().Name` which is polymorphic and yields
    // "NamespaceResolveResult". The C++ port reproduces this via the
    // `ClassName()` override so the custom `ToString` above reports the subclass
    // name.
    std::string ClassName() const override { return "NamespaceResolveResult"; }

public:
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `NamespaceResolveResult` stays a
    // `NamespaceResolveResult` (not sliced to the `ResolveResult` base). The C++
    // override reproduces this: the default copy ctor copies the raw `ns_`
    // pointer (both the original and the clone point to the same
    // compilation-owned `INamespace`), faithful to the C# reference-copy.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<NamespaceResolveResult>(*this);
    }

private:
    const ILSpy::Decompiler::TypeSystem::INamespace* ns_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_NAMESPACERESOLVERESULT_HPP
