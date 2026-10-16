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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/AliasNamespaceResolveResult.cs --
// a `NamespaceResolveResult` (D433) subclass that records the extern-alias used
// to resolve the namespace. This is the structural twin of
// `AliasTypeResolveResult` (the same `Alias` + forwarding-ctor shape, forwarding
// to the `NamespaceResolveResult` base instead of `TypeResolveResult`) and the
// second `cpp/Decompiler/CSharp/Resolver/` leaf toward the `CSharpResolver` leaf
// deps. The C# surface is:
//   * `public string Alias { get; private set; }` -- the alias name.
//   * `AliasNamespaceResolveResult(string alias, NamespaceResolveResult
//      underlyingResult) : base(underlyingResult.Namespace)` -- forwards the
//     underlying result's namespace to the `NamespaceResolveResult(INamespace)`
//     base and stores the alias.
//
// The C# does NOT override `ToString` or `ShallowClone`, but the C++ port
// overrides both `ClassName()` and `ShallowClone()` to faithfully reproduce the
// C# `GetType().Name` (polymorphic class name in the inherited `ToString`,
// which `NamespaceResolveResult` DOES override with a custom format using
// `ClassName()`) and `MemberwiseClone` (runtime-type-preserving shallow clone)
// -- the documented `ResolveResult` conventions (D424/D433) the first subclass
// in each new file carries, avoiding the C++-only slicing a non-overriding base
// clone would perform and the wrong class name a non-overriding `ClassName`
// would report.

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_ALIASNAMESPACERESOLVERESULT_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_ALIASNAMESPACERESOLVERESULT_HPP

#include "Decompiler/Semantics/NamespaceResolveResult.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public class AliasNamespaceResolveResult : NamespaceResolveResult`
// (NOT `sealed` -- no C# subclass derives from it but it is unsealed) ports to a
// C++ subclass (NOT `final`) of `Semantics::NamespaceResolveResult`. The ctor
// forwards the underlying result's stored `INamespace*` raw pointer
// (`Namespace()`, the public non-owning accessor) to the
// `NamespaceResolveResult(const INamespace*)` base ctor, mirroring the C#
// `: base(underlyingResult.Namespace)`. The `INamespace` is non-owning (owned by
// the compilation's namespace tree, the D433 convention), so both the alias and
// the underlying result reference the same namespace. The `Alias` string is a
// value member copied by the default copy ctor (the D432/D443 string-field
// precedent).
class AliasNamespaceResolveResult : public ILSpy::Decompiler::Semantics::NamespaceResolveResult {
public:
    AliasNamespaceResolveResult(std::string alias, const ILSpy::Decompiler::Semantics::NamespaceResolveResult& underlyingResult)
        : NamespaceResolveResult(underlyingResult.Namespace()), alias_(std::move(alias)) {}

    // The C# `public string Alias { get; private set; }` -- the alias name.
    const std::string& Alias() const { return alias_; }

    // The C# `ToString` (inherited from `NamespaceResolveResult`, which DOES
    // override it with a custom `[{0} {1}]` format using `GetType().Name`) is
    // polymorphic on `ClassName()`, so the C++ port only overrides `ClassName()`
    // to yield "AliasNamespaceResolveResult" and the inherited `ToString`
    // reports the subclass name with the namespace's full name (the D433
    // custom-`ToString`-via-`ClassName` convention).
protected:
    std::string ClassName() const override { return "AliasNamespaceResolveResult"; }

public:
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `AliasNamespaceResolveResult` stays an
    // `AliasNamespaceResolveResult` (not sliced to the `NamespaceResolveResult`
    // base). The C++ port reproduces this by overriding `ShallowClone` to
    // construct an `AliasNamespaceResolveResult` copy (the default copy ctor
    // shares the `type_` `shared_ptr` and the non-owning `ns_` pointer, and
    // value-copies the `alias_` string, faithful to the C# reference-copy of
    // the `IType` field, the pointer-copy of the `INamespace` field, and the
    // value-copy of the string field).
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override {
        return std::make_unique<AliasNamespaceResolveResult>(*this);
    }

private:
    std::string alias_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_ALIASNAMESPACERESOLVERESULT_HPP
