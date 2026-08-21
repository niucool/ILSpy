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

// Port of ICSharpCode.Decompiler/CSharp/Resolver/AliasTypeResolveResult.cs -- a
// `TypeResolveResult` (D425) subclass that records the extern-alias used to
// resolve the type. This is the first `cpp/Decompiler/CSharp/Resolver/` leaf
// toward the `CSharpResolver` leaf deps (the long-pole remaining blocker of
// `TypeSystemAstBuilder` / `CSharpAmbience`). The C# surface is:
//   * `public string Alias { get; private set; }` -- the alias name.
//   * `AliasTypeResolveResult(string alias, TypeResolveResult underlyingResult)
//      : base(underlyingResult.Type)` -- forwards the underlying result's type
//     to the `TypeResolveResult(IType)` base and stores the alias.
//
// The C# does NOT override `ToString` or `ShallowClone`, but the C++ port
// overrides both `ClassName()` and `ShallowClone()` to faithfully reproduce the
// C# `GetType().Name` (polymorphic class name in the inherited `ToString`) and
// `MemberwiseClone` (runtime-type-preserving shallow clone) -- the documented
// `ResolveResult` conventions (D424/D425) the first subclass in each new file
// carries, avoiding the C++-only slicing a non-overriding base clone would
// perform and the wrong class name a non-overriding `ClassName` would report.

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_ALIATYPERESOLVERESULT_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_ALIATYPERESOLVERESULT_HPP

#include "Decompiler/Semantics/TypeResolveResult.hpp"

#include <memory>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public class AliasTypeResolveResult : TypeResolveResult` (NOT `sealed`
// -- no C# subclass derives from it but it is unsealed) ports to a C++ subclass
// (NOT `final`) of `Semantics::TypeResolveResult`. The ctor forwards the
// underlying result's stored `IType` handle (`TypePtr()`, the public owning
// `shared_ptr<IType>` accessor on `ResolveResult` the D428
// `ByReferenceResolveResult` cross-instance-type-forward established) to the
// `TypeResolveResult(ITypePtr)` base ctor, mirroring the C#
// `: base(underlyingResult.Type)`. The `Alias` string is a value member copied
// by the default copy ctor (the D432/D443 string-field precedent).
class AliasTypeResolveResult : public ILSpy::Decompiler::Semantics::TypeResolveResult {
public:
    AliasTypeResolveResult(std::string alias, const ILSpy::Decompiler::Semantics::TypeResolveResult& underlyingResult)
        : TypeResolveResult(underlyingResult.TypePtr()), alias_(std::move(alias)) {}

    // The C# `public string Alias { get; private set; }` -- the alias name.
    const std::string& Alias() const { return alias_; }

    // The C# `ToString` (inherited from `ResolveResult` via `TypeResolveResult`,
    // which does NOT override it) uses `GetType().Name` which is polymorphic and
    // yields "AliasTypeResolveResult" for an `AliasTypeResolveResult` instance.
    // The C++ port reproduces this via the `ClassName()` override so the inherited
    // `ToString` reports the subclass name (not the "TypeResolveResult" the
    // `TypeResolveResult` `ClassName()` override would yield).
protected:
    std::string ClassName() const override { return "AliasTypeResolveResult"; }

public:
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the
    // runtime type, so a cloned `AliasTypeResolveResult` stays an
    // `AliasTypeResolveResult` (not sliced to the `TypeResolveResult` base). The
    // C++ port reproduces this by overriding `ShallowClone` to construct an
    // `AliasTypeResolveResult` copy (the default copy ctor shares the `type_`
    // `shared_ptr` and value-copies the `alias_` string, faithful to the C#
    // reference-copy of the `IType` field and value-copy of the string field).
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override {
        return std::make_unique<AliasTypeResolveResult>(*this);
    }

private:
    std::string alias_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_ALIATYPERESOLVERESULT_HPP
