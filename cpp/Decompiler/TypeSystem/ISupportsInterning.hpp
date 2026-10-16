// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
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
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, IN CONNECTION WITH
// THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/ISupportsInterning.cs -- the
// `ISupportsInterning` interface, the marker a TypeSystem object implements to
// opt into the `InterningProvider`'s reference-deduplication. The C#
// `interface ISupportsInterning { int GetHashCodeForInterning(); bool
// EqualsForInterning(ISupportsInterning other); }` ports to a C++ abstract base
// with a virtual destructor and two pure-virtuals, the established
// C#-interface-to-C++-abstract-base convention (the `ISymbol` (D372) /
// `INamedElement` (D375) / `ICompilationProvider` (D379) precedents in this port).
//
// It is a leaf TypeSystem dependency toward the concrete `ITypeReference`
// siblings of `KnownTypeReference` (D411): `ArrayTypeReference` / `ByReferenceTypeReference`
// / `PointerTypeReference` / `ParameterizedTypeReference` / `NestedTypeReference` each
// `: ITypeReference, ISupportsInterning`, so the faithful port of any of them requires
// this interface to derive from. It is also implemented by `DefaultAssemblyReference`
// (the assembly-reference counterpart). The `InterningProvider` (in
// `IInterningProvider.cs`, an abstract class with a `Dummy` no-op singleton) calls
// `GetHashCodeForInterning` to bucket candidate duplicates and `EqualsForInterning` to
// confirm a pair is structurally equal before returning the shared instance; the
// `InterningProvider` itself is NOT yet ported (its surface pulls a generic
// `InternList<T>` / `InternValue` / `Intern(string)` set), and this leaf lands only the
// interface the concrete references derive from, consistent with the established
// "port the interface ahead of its consumer" pattern.
//
// KEY PORT CONVENTIONS: (a) `GetHashCodeForInterning` returns `int` to match the .NET
// `int GetHashCode()` signature the `InterningProvider`'s bucketing dictionary uses
// (the `StringComparer::GetHashCode` (D398) / `TopLevelTypeNameComparer::GetHashCode`
// (D400) `int`-return precedent); the concrete references XOR their element's hash with
// a per-type salt constant, so the `int` width is load-bearing. (b) The C#
// `EqualsForInterning(ISupportsInterning other)` takes a non-null reference-type
// parameter (no `?`), so it ports to a `const ISupportsInterning&` parameter (the
// `IType::Equals(const IType&)` (D271) / `IModule::InternalsVisibleTo(const IModule&)`
// (D396) non-null-reference-parameter precedent). The concrete references implement
// the C# `other as ConcreteType` downcast with `dynamic_cast<const ConcreteType*>(&other)`,
// which yields `nullptr` on a type mismatch (the C# `as`-returns-null case), so the
// `const ISupportsInterning&` parameter binds a real object while the downcast handles
// the type-mismatch -- a faithful mirror of the C# `as`-then-null-check shape. (c) Both
// methods are `const` (they read the object's interning-relevant fields without
// mutating; the `InterningProvider` calls them on already-constructed, immutable
// candidate objects), matching the C# instance methods. NO name-hiding qualification is
// needed (neither `GetHashCodeForInterning` nor `EqualsForInterning` collides with a
// namespace-scope type in the `TypeSystem` namespace -- the D375 `INamedElement` /
// D379 `ICompilationProvider` collision-free-accessor convention). NO stand-in
// reconciliation is needed (`ISupportsInterning` is a NEW type; no existing test file
// defined an `ISupportsInterning` stand-in).

#pragma once

namespace ILSpy::Decompiler::TypeSystem {

// The opt-in marker a TypeSystem object implements so the `InterningProvider` can
// deduplicate structurally-equal instances (reducing memory by sharing a single object
// where possible). A concrete supports-interning object subclasses `ISupportsInterning`
// and overrides the two accessors: `GetHashCodeForInterning` returns a hash the provider
// uses to bucket candidate duplicates, and `EqualsForInterning` confirms a bucketed
// pair is structurally equal (returning `false` for a different `ISupportsInterning`
// subtype via a failed `dynamic_cast`).
class ISupportsInterning {
public:
    virtual ~ISupportsInterning() = default;

    // The C# `int GetHashCodeForInterning()` -- a hash the `InterningProvider` uses to
    // bucket candidate duplicates. Returns `int` to match the .NET `int GetHashCode()`
    // signature the provider's bucketing dictionary uses (the `StringComparer::GetHashCode`
    // (D398) `int`-return precedent); the concrete references XOR their element's hash
    // with a per-type salt constant.
    virtual int GetHashCodeForInterning() const = 0;

    // The C# `bool EqualsForInterning(ISupportsInterning other)` -- confirms a bucketed
    // pair is structurally equal before the provider returns the shared instance. The
    // parameter is a non-null reference (the C# `ISupportsInterning other` has no `?`),
    // so a `const ISupportsInterning&`; a concrete reference downcasts via
    // `dynamic_cast<const ConcreteType*>(&other)` which yields `nullptr` on a type
    // mismatch (the C# `as`-returns-null case), returning `false` for a different subtype.
    virtual bool EqualsForInterning(const ISupportsInterning& other) const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
