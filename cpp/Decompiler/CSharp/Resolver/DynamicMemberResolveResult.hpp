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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/DynamicMemberResolveResult.cs -- the
// result of an access to a member of a `dynamic` object. This is the fifth
// `cpp/Decompiler/CSharp/Resolver/` leaf toward the `CSharpResolver` leaf deps (the
// long-pole remaining blocker of `TypeSystemAstBuilder` / `CSharpAmbience`), after the
// D467 twin Alias `ResolveResult` subclasses and the D468 twin enum leaves
// (`NameLookupMode` + `OverloadResolutionErrors`). All deps are already ported:
// `ResolveResult` (D424, the base), `SpecialType` / `TypeKind::Dynamic` (the
// `SpecialType.Dynamic` singleton the C# `: base(SpecialType.Dynamic)` forwards), and
// `IMember` (D387, for the nullable `Symbol`). The C# surface is:
//   * `DynamicMemberResolveResult(ResolveResult target, string member,
//      IMember symbol = null) : base(SpecialType.Dynamic)` -- forwards the dynamic
//     special type to the `ResolveResult` base and stores the target / member name /
//     optional symbol.
//   * `readonly ResolveResult Target` -- the dynamic object being accessed (a held
//     child; `GetChildResults` returns it).
//   * `readonly string Member` -- the accessed member's name.
//   * `readonly IMember Symbol` -- a synthesized member (a `dynamic` field on the
//     `dynamic` type) representing the accessed member so the member reference carries
//     a navigable symbol; may be `null`.
//   * `override string ToString()` -- `"[Dynamic member '{Member}']"` (a custom format,
//     NOT the inherited `ResolveResult::ToString` bracket form).
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- `new[] { Target }`.
//
// KEY PORT CONVENTIONS:
//  * The C# `: base(SpecialType.Dynamic)` forwards the C# `SpecialType.Dynamic`
//    singleton (`new SpecialType(TypeKind.Dynamic, "dynamic", isReferenceType: true)`)
//    to the `ResolveResult(IType)` base. The C++ port constructs
//    `std::make_shared<SpecialType>(TypeKind::Dynamic, true)` inline -- the `true` is
//    the `isReferenceType: true` (the C# `dynamic` is a reference type); the name
//    "dynamic" comes from `SpecialType::Name()` which switches on `Kind()` (returns
//    "dynamic" for `TypeKind::Dynamic`, the IType.cpp line-144 branch). The `ResolveResult`
//    base ctor `assert`-guards the type is non-null; `make_shared` returns a non-null
//    `shared_ptr`, so the assert holds. There is no `DynamicType()` convenience
//    function (unlike the `UnknownType()` / `NoType()` singletons) so the dynamic type
//    is constructed inline here (the first `ResolveResult` subclass to forward
//    `SpecialType.Dynamic`).
//  * The C# `ResolveResult Target` (a reference-type field the ctor does NOT guard with
//    `ArgumentNullException`; `GetChildResults` returns it unconditionally so it is
//    non-null in practice) ports to a `std::shared_ptr<ResolveResult>` (the D428
//    `ByReferenceResolveResult` `elementResult_` / D437 `MemberResolveResult`
//    `targetResult_` precedent: shared ownership so `ShallowClone`'s default copy ctor
//    shares it, faithfully mirroring the C# `MemberwiseClone` reference-copy). No null
//    guard (the C# does not guard `target`).
//  * The C# `string Member` ports to a `std::string` value member (the D432/D443
//    string-field precedent), copied by the default copy ctor.
//  * The C# `IMember Symbol` (a NULLABLE reference-type field the ctor defaults to
//    `null`) ports to a non-owning `const IMember*` raw pointer defaulting to `nullptr`
//    (the D437 `MemberResolveResult` non-owning `const IMember*` `member_` precedent;
//    the D445 `ForEachResolveResult` nullable-`IMember*` precedent). The member is owned
//    by the type system (the C# GC guarantee); the `DynamicMemberResolveResult` is a
//    temporary resolution outcome that does not outlive it. `Symbol()` is never
//    dereferenced by `DynamicMemberResolveResult` itself (it is stored and returned
//    verbatim, a navigability hint for the caller). `IMember` is forward-declared (not
//    included) in the header -- a pointer member and a pointer-returning accessor to an
//    incomplete type need only a forward declaration (the D445 IProperty/IMethod
//    forward-declaration precedent).
//  * The C# `override string ToString()` uses a custom format
//    `string.Format(CultureInfo.InvariantCulture, "[Dynamic member '{0}']", Member)` --
//    NOT `GetType().Name`. The C++ port overrides `ToString()` directly
//    (`return "[Dynamic member '" + member_ + "']";`) rather than overriding only
//    `ClassName()` (the D424/D425 inherit-`ToString` convention does NOT apply since the
//    C# overrides `ToString` with a custom format; the `MemberResolveResult` custom-
//    `ToString` precedent). `ClassName()` is still overridden to "DynamicMemberResolveResult"
//    for RTTI consistency (the polymorphic-class-name convention), though the custom
//    `ToString` does not consult it.
//  * The C# `override IEnumerable<ResolveResult> GetChildResults()` returns
//    `new[] { Target }` -- a one-element snapshot. The C++ port returns
//    `std::vector<const ResolveResult*>` (non-owning pointers, the D424 convention) with
//    the single element `target_.get()` (the `shared_ptr` keeps the `ResolveResult`
//    alive).
//  * The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
//    shallow-copies the fields (the `target_` `shared_ptr` is shared, the `member_`
//    string is value-copied, the `symbol_` raw pointer is copied, the base `type_`
//    `shared_ptr` is shared). The C++ override reproduces this via the default copy
//    ctor, avoiding the C++-only slicing a non-overriding base clone would perform (the
//    D424 slicing-prevention convention).

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_DYNAMICMEMBERRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_DYNAMICMEMBERRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem { class IMember; }

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public class DynamicMemberResolveResult : ResolveResult` (NOT `sealed` --
// no C# subclass derives from it but it is unsealed) ports to a C++ subclass (NOT
// `final`) of `Semantics::ResolveResult`. The ctor forwards
// `std::make_shared<SpecialType>(TypeKind::Dynamic, true)` (the C#
// `SpecialType.Dynamic` singleton) to the `ResolveResult` base and stores the target
// (a `shared_ptr<ResolveResult>` child), the member name (a `string`), and the optional
// symbol (a non-owning nullable `const IMember*`).
class DynamicMemberResolveResult : public ILSpy::Decompiler::Semantics::ResolveResult {
public:
    DynamicMemberResolveResult(std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target,
                               std::string member,
                               const ILSpy::Decompiler::TypeSystem::IMember* symbol = nullptr)
        : ResolveResult(std::make_shared<ILSpy::Decompiler::TypeSystem::SpecialType>(
              ILSpy::Decompiler::TypeSystem::TypeKind::Dynamic, true)),
          target_(std::move(target)),
          member_(std::move(member)),
          symbol_(symbol) {}

    // The C# `ResolveResult Target` -- the dynamic object being accessed. Returns a
    // non-owning raw pointer (`target_.get()`); `nullptr` only when an empty
    // `shared_ptr` was passed (the C# does not guard `target`, and
    // `GetChildResults` returns it unconditionally).
    ILSpy::Decompiler::Semantics::ResolveResult* Target() const noexcept { return target_.get(); }

    // The C# `string Member` -- the accessed member's name. Returns a const reference
    // to the stored string (the faithful exposure of a readonly field).
    const std::string& Member() const noexcept { return member_; }

    // The C# `IMember Symbol` -- a synthesized member representing the accessed member
    // (a navigability hint); may be `null`. Returns a non-owning raw pointer; `nullptr`
    // when no symbol was provided (the default). Never dereferenced by
    // `DynamicMemberResolveResult` itself.
    const ILSpy::Decompiler::TypeSystem::IMember* Symbol() const noexcept { return symbol_; }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` -- returns
    // `new[] { Target }`, a one-element snapshot. The snapshot is a non-owning pointer
    // (the `shared_ptr` keeps the `ResolveResult` alive).
    std::vector<const ILSpy::Decompiler::Semantics::ResolveResult*> GetChildResults() const override
    {
        return { target_.get() };
    }

    // The C# `override string ToString()` =>
    // `string.Format(CultureInfo.InvariantCulture, "[Dynamic member '{0}']", Member)`.
    // A custom format (NOT the inherited `ResolveResult::ToString` bracket form): the
    // member name is quoted inside `"[Dynamic member '...']"`.
    std::string ToString() const override {
        return "[Dynamic member '" + member_ + "']";
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type
    // and shallow-copies the fields (the `target_` `shared_ptr` is shared, the
    // `member_` string is value-copied, the `symbol_` raw pointer is copied, the base
    // `type_` `shared_ptr` is shared). The C++ override reproduces this via the default
    // copy ctor, avoiding the C++-only slicing a non-overriding base clone would
    // perform.
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<DynamicMemberResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields. Overridden for RTTI
    // consistency (the polymorphic-class-name convention); the custom `ToString` above
    // does NOT consult it (the C# `ToString` uses a fixed format, not `GetType().Name`).
    std::string ClassName() const override { return "DynamicMemberResolveResult"; }

private:
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target_;
    std::string member_;
    const ILSpy::Decompiler::TypeSystem::IMember* symbol_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_DYNAMICMEMBERRESOLVERESULT_HPP
