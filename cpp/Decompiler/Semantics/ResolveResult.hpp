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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/ResolveResult.cs -- the base class for all
// expression-resolution results. A `ResolveResult` carries the resolved `IType` plus a
// small virtual surface (`IsCompileTimeConstant` / `ConstantValue` / `IsError` /
// `GetChildResults` / `ShallowClone`) the resolver subclasses override; it is the root
// of the `Semantics` namespace -- the family `TypeSystemAstBuilder` reaches to
// annotate AST nodes with the resolution outcome -- and the first `Semantics` leaf
// toward `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker of
// `CSharpAmbience`). The next in-order `Semantics` leaves after this base are the
// concrete `ResolveResult` subclasses `TypeSystemAstBuilder` consumes directly
// (`TypeResolveResult` / `NamespaceResolveResult` / `MemberResolveResult` / ...).

#ifndef ILSPY_DECOMPILER_SEMANTICS_RESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_RESOLVERESULT_HPP

#include "Decompiler/TypeSystem/IType.hpp"

#include <any>
#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ResolveResult` (a concrete, instantiable reference type) ports
// to a C++ polymorphic base with a virtual destructor. The C# surface is:
//   * `readonly IType type` -- a non-null `IType` field (the ctor throws
//     `ArgumentNullException` on null).
//   * `IType Type` -- the non-null getter.
//   * `virtual bool IsCompileTimeConstant` (default `false`).
//   * `virtual object? ConstantValue` (default `null`).
//   * `virtual bool IsError` (default `false`).
//   * `override string ToString() => "[" + GetType().Name + " " + type + "]"`.
//   * `virtual IEnumerable<ResolveResult> GetChildResults()` (default empty).
//   * `virtual ResolveResult ShallowClone() => (ResolveResult)MemberwiseClone()`.
//
// KEY PORT CONVENTIONS:
//  * The C# `IType type` (a non-null reference-type field the ctor guards with
//    `ArgumentNullException`) ports to an `ITypePtr` (the D271 `shared_ptr<IType>`
//    reference-handle model) member guarded with `assert` (the D401
//    `Debug.Assert`-to-`assert` precedent, compiled out with NDEBUG). The
//    `shared_ptr` shares ownership of the resolved type with the caller (the C#
//    GC-owned reference).
//  * The C# `IType Type` (non-null reference) ports to `const IType& Type() const`
//    returning `*type_` (the D374 `IVariable::Type()` non-null-reference convention);
//    the held `shared_ptr` keeps the `IType` alive for the `ResolveResult`'s
//    lifetime.
//  * The C# `object? ConstantValue` ports to `std::any` (the D374
//    `IVariable::GetConstantValue` convention); an empty `any` is the C# `null`.
//    Subclasses for compile-time-constant expressions (`ConstantResolveResult`)
//    box the value.
//  * The C# `IEnumerable<ResolveResult> GetChildResults` (default
//    `Enumerable.Empty<ResolveResult>()`) ports to `std::vector<const ResolveResult*>`
//    (non-owning pointers, empty by default) -- the children are references to results
//    owned by the parent (the C# GC owns them; the C++ parent owns them via its own
//    `unique_ptr`/`shared_ptr` members), so the snapshot is a non-owning view.
//  * The C# `ToString` uses `GetType().Name` (the runtime class name, polymorphic);
//    the C++ port mirrors it via a `protected virtual ClassName()` (below) so a
//    subclass that does not override `ToString` still reports its own class name.
//    `type` is `type.ToString()` which for an `AbstractType` is `ReflectionName`; the
//    C++ port uses `type_->ReflectionName()`.
//  * The C# `ShallowClone` uses `MemberwiseClone` (shallow-copies the fields,
//    preserving the runtime type). The C++ base `ShallowClone` does
//    `std::make_unique<ResolveResult>(*this)` (the default copy ctor shares the
//    `type_` `shared_ptr`, faithful to the C# reference-copy). Subclasses MUST
//    override `ShallowClone` (a non-overriding C++ base clone would slice a subclass
//    to a `ResolveResult` base, diverging from the C# `MemberwiseClone` which
//    preserves the runtime type) -- the first subclass port (`TypeResolveResult`)
//    will add the override.
class ResolveResult {
public:
    explicit ResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr type) {
        assert(type && "ResolveResult: type must not be null");
        type_ = std::move(type);
    }

    virtual ~ResolveResult() = default;

    const ILSpy::Decompiler::TypeSystem::IType& Type() const { return *type_; }

    // The owning `shared_ptr<IType>` handle behind `Type()` -- exposed (as a
    // `const` reference) to subclasses so a subclass ctor can forward the stored
    // `IType` to ANOTHER `ResolveResult` base (e.g. `ByReferenceResolveResult`'s
    // public ctor wraps the element-result's type in a `ByReferenceType` and
    // forwards it to its own base). The reference-return keeps the `shared_ptr`
    // shared (a copy increments the refcount) without exposing a non-const
    // rebind. The first consumer is `ByReferenceResolveResult` (the first
    // `ResolveResult` subclass that holds another `ResolveResult` member and
    // builds its own type from that member's type).
    const ILSpy::Decompiler::TypeSystem::ITypePtr& TypePtr() const { return type_; }

    virtual bool IsCompileTimeConstant() const { return false; }

    virtual std::any ConstantValue() const { return {}; }

    virtual bool IsError() const { return false; }

    virtual std::vector<const ResolveResult*> GetChildResults() const { return {}; }

    virtual std::string ToString() const {
        return "[" + ClassName() + " " + type_->ReflectionName() + "]";
    }

    virtual std::unique_ptr<ResolveResult> ShallowClone() const {
        return std::make_unique<ResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields in `ToString`.
    // `protected virtual` so a subclass overrides it to report its own name without
    // re-implementing the whole `ToString` format.
    virtual std::string ClassName() const { return "ResolveResult"; }

private:
    ILSpy::Decompiler::TypeSystem::ITypePtr type_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_RESOLVERESULT_HPP
