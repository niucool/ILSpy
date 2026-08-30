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

// Port of ICSharpCode.Decompiler/CSharp/Resolver/MethodGroupResolveResult.cs -- a group of
// methods (a method reference used to create a delegate resolves to a
// `MethodGroupResolveResult`), together with the co-located per-declaring-type
// `MethodListWithDeclaringType` bucket. This is the eleventh
// `cpp/Decompiler/CSharp/Resolver/` leaf toward the `CSharpResolver` leaf deps (the
// long-pole remaining blocker of `TypeSystemAstBuilder` / `CSharpAmbience`), after the
// D467 twin Alias `ResolveResult` subclasses, the D468 twin enum leaves (`NameLookupMode`
// + `OverloadResolutionErrors`), the D469 `DynamicMemberResolveResult`, the D470
// `AwaitResolveResult`, the D471 `DynamicInvocationResolveResult` +
// `DynamicInvocationType`, the D472 `CSharpInvocationResolveResult`, and the D473
// `LambdaResolveResult` + `LambdaConversion`. The C# source declares two classes; this
// header ports BOTH:
//   * PORTED: `public class MethodListWithDeclaringType : List<IParameterizedMember>` --
//     the per-declaring-type method bucket. A `List<IParameterizedMember>` that also
//     carries the declaring `IType`. The C# surface:
//       - `readonly IType DeclaringType` -- the declaring type. (Note per the C# doc:
//         not all methods in the list necessarily have this as their declaring type, e.g.
//         a `Derived.M()` override groups under the `Base` declaring type.)
//       - `MethodListWithDeclaringType(IType declaringType)` and
//         `MethodListWithDeclaringType(IType declaringType, IEnumerable<IParameterizedMember>
//         methods)` ctors.
//     It ports to a `std::vector<const IParameterizedMember*>`-derived value type (the
//     type system owns the methods; the bucket observes them) with a `DeclaringType()`
//     accessor.
//   * PORTED: `public class MethodGroupResolveResult : ResolveResult` -- the method group.
//     The C# surface:
//       - `MethodGroupResolveResult(ResolveResult targetResult, string methodName,
//          IReadOnlyList<MethodListWithDeclaringType> methods, IReadOnlyList<IType>
//          typeArguments)` -- forwards `SpecialType.NoType` to the `ResolveResult` base
//         (a method group has NO type; the delegate type comes from the method-group
//         conversion) and stores the four fields (guarding `methods` non-null; a null
//         `typeArguments` defaults to empty).
//       - `ResolveResult TargetResult` -- the target object resolve result.
//       - `IType TargetType` -- the type of the reference to the target object
//         (`targetResult.Type` when present, `SpecialType.UnknownType` when the target is
//         null).
//       - `string MethodName` -- the name of the methods in the group.
//       - `IEnumerable<IMethod> Methods` -- the found methods flattened across
//         declaring-type buckets (base types first; NOT including extension methods).
//       - `IEnumerable<MethodListWithDeclaringType> MethodsGroupedByDeclaringType` -- the
//         buckets (base types first).
//       - `IReadOnlyList<IType> TypeArguments` -- the explicitly provided type arguments.
//       - `IMethod ChosenMethod` -- the method chosen for the group (only set for results
//         in ILSpy AST annotations); null by default.
//       - `MethodGroupResolveResult WithChosenMethod(IMethod method)` -- a `ShallowClone`
//         with `chosenMethod` set (the original untouched).
//       - `override string ToString()` =>
//         `"[" + GetType().Name + " with " + Methods.Count() + " method(s)]"` (a custom
//         format, NOT the inherited bracket form).
//       - `override IEnumerable<ResolveResult> GetChildResults()` => `{ targetResult }`
//         when present, empty otherwise.
//   * DEFERRED (method members, not classes): the extension-method machinery that needs the
//     unported `CSharpResolver` controller (`GetEligibleExtensionMethods` plus the internal
//     `extensionMethods`/`resolver` fields and the resolver-attached `GetExtensionMethods` fetch).
//     `GetExtensionMethods()` is ported in its resolver-less state (returns empty when no
//     resolver is attached; the `PerformOverloadResolution` extension-method arm is
//     correspondingly inert while `GetExtensionMethods()` yields empty -- it goes live when
//     the resolver lands). `PerformOverloadResolution` is PORTED (implemented in the new
//     `MethodGroupResolveResult.cpp`): it composes the now-ported `OverloadResolution` engine
//     (the ctor + the input properties + `AddMethodLists` + `AddCandidate` + the output
//     properties + `GetBestCandidateWithSubstitutedTypeArguments`).
//
// All other deps are already ported: `ResolveResult` (D424, the base), `SpecialType.NoType`
// / `SpecialType.UnknownType` (the D433 `IType.hpp` conveniences), `IType`/`ITypePtr`
// (D271), `IParameterizedMember` (D388, the bucket element type), and `IMethod` (D389, the
// `Methods`/`ChosenMethod` element type). `IMethod.hpp`/`IParameterizedMember.hpp` are
// INCLUDED (not forward-declared): the `Methods()` `static_cast` from
// `const IParameterizedMember*` to the derived `const IMethod*` requires both types
// complete (the `SelectMany(...).Cast<IMethod>()` cast up the `IParameterizedMember` ->
// `IMethod` single-inheritance chain).
//
// KEY PORT CONVENTIONS:
//  * The C# `MethodListWithDeclaringType : List<IParameterizedMember>` (a `List<T>`
//     subclass carrying a value-add member) has no direct C++ analog; per the mirror doc's
//     "C# `List<T>` -> `std::vector<T>`" the base collection ports to
//     `std::vector<const IParameterizedMember*>` and the subclass DERIVES from it, adding
//     the `declaringType_` member + `DeclaringType()` accessor. The element type is the
//     non-owning `const IParameterizedMember*` (the type system owns the methods; the C#
//     GC-shared `List<IParameterizedMember>` reference is a shared handle here). The two
//     C# ctors port to vector-forwarding ctors (the `methods` enumerable ports to an
//     `initializer_list<const IParameterizedMember*>` / a vector copy).
//  * The C# `MethodGroupResolveResult : ResolveResult` ports to a C++ subclass of
//     `Semantics::ResolveResult`. The ctor forwards `ILSpy::Decompiler::TypeSystem::NoType()`
//     (the D433 convenience -- `TypeKind::None`, NOT `UnknownType`: a method group has NO
//     type at all, the same `NoType`-vs-`UnknownType` distinction the D443/D473 ports
//     document) as the base type, and stores the four fields. The C#
//     `targetResult` (a `ResolveResult`, nullable) ports to a `shared_ptr<ResolveResult>`;
//     the C# `string methodName` to `std::string`; the C# `IReadOnlyList<...>` fields to
//     `std::vector` values; the C# `IReadOnlyList<IType> typeArguments` to
//     `std::vector<ITypePtr>` (the shared `IType` handles). The C# `IMethod chosenMethod`
//     (a mutable reference-type field, null by default) ports to a non-owning
//     `const IMethod*` (nullable, `nullptr` default).
//  * The C# `ResolveResult TargetResult` (a nullable reference) ports to
//     `Semantics::ResolveResult* TargetResult() const` (a non-owning raw pointer,
//     `target_.get()`; `nullptr` when a null shared_ptr was passed) -- the D471 convention.
//  * The C# `IType TargetType` (non-null; the null-target short-circuit to
//     `SpecialType.UnknownType`) ports to `const IType& TargetType() const` (a never-null
//     reference return): `target_ ? target_->Type() : *unknownType_`. The null-target
//     `UnknownType` is cached in an `ITypePtr` member (lazily/default-constructed) so the
//     reference return stays valid for the group's lifetime.
//  * The C# `IEnumerable<IMethod> Methods` (a lazy `SelectMany` over the buckets, casting
//     each `IParameterizedMember` up to its `IMethod` overload) ports to a by-value
//     `std::vector<const IMethod*> Methods() const` snapshot: it walks the buckets in
//     order (base types first) and `static_cast`s each stored `const IParameterizedMember*`
//     to `const IMethod*` (the C# `Cast<IMethod>()` -- the bucket holds methods, so the
//     `IMethod*`-of-method upcast is exact; the by-value snapshot is the faithful
//     IEnumerable-materialization).
//  * The C# `IEnumerable<MethodListWithDeclaringType> MethodsGroupedByDeclaringType` and
//     `IReadOnlyList<IType> TypeArguments` port to by-value `std::vector` snapshots (the
//     read-only enumerables; returning a copy keeps them immutable-by-convention -- the
//     caller cannot mutate the group's stored state).
//  * The C# `IMethod ChosenMethod` (null by default) ports to
//     `const IMethod* ChosenMethod() const` (`chosenMethod_`, `nullptr` default).
//  * The C# `WithChosenMethod(IMethod)` (a `ShallowClone` + set) ports to
//     `std::unique_ptr<MethodGroupResolveResult> WithChosenMethod(const IMethod*) const`:
//     it shallow-clones (the C# `ShallowClone` -- runtime-type-preserving) and sets the
//     clone's `chosenMethod_` (the original untouched).
//  * The C# `ToString` is a CUSTOM format (`"[MethodGroupResolveResult with N method(s)]"`,
//     using `GetType().Name` which for this unsealed class is always
//     "MethodGroupResolveResult"); it does NOT consult `ClassName()`. The C++ port overrides
//     `ToString` to the fixed format (and overrides `ClassName()` for RTTI consistency, per
//     the D471 convention -- the custom `ToString` does NOT use `ClassName()`).
//  * The C# `override IEnumerable<ResolveResult> GetChildResults()` =>
//     `{ targetResult }` / empty ports to the D424 `std::vector<const ResolveResult*>`
//     snapshot (non-owning), `{ target_.get() }` when present, empty otherwise.
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C# inherited
//     `MemberwiseClone` preserves the runtime type and shallow-copies the fields (the
//     `target_` shared_ptr is shared, the `methodLists_`/`typeArguments_` vectors are
//     copied element-wise, `methodName_` copied, `chosenMethod_`/`unknownType_` shared).
//     The C++ override does `make_unique<MethodGroupResolveResult>(*this)` (the default copy
//     ctor), avoiding the C++-only slicing a non-overriding base clone (`make_unique<
//     ResolveResult>(*this)`) would perform. `WithChosenMethod` uses the same clone.
//  * The class is NOT `final` (the C# is unsealed), pinned by `static_assert(!is_final)`.

#ifndef ILSPY_DECOMPILER_CSHARP_RESOLVER_METHODGROUPRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_CSHARP_RESOLVER_METHODGROUPRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// Forward-declared (now ported): the overload-resolution engine `PerformOverloadResolution`
// builds and returns (the `unique_ptr` return type needs only a declaration; the .cpp
// includes the full header).
class OverloadResolution;

// Forward-declared (now ported): the conversion controller threaded through
// `PerformOverloadResolution` (a pointer parameter needs only a declaration).
class CSharpConversions;

// The C# `public class MethodListWithDeclaringType : List<IParameterizedMember>` -- a
// method list that belongs to a declaring type. Ports to a value type deriving from
// `std::vector<const IParameterizedMember*>` (the base collection, non-owning pointers --
// the type system owns the methods) with the added `DeclaringType` member. Per the C# doc
// remark, not all methods in a bucket necessarily have `DeclaringType` as their declaring
// type (an override groups under the base declaring type); `DeclaringType` is the type
// the bucket was formed for.
class MethodListWithDeclaringType
    : public std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*> {
public:
    // The C# `MethodListWithDeclaringType(IType declaringType)`.
    explicit MethodListWithDeclaringType(
        ILSpy::Decompiler::TypeSystem::ITypePtr declaringType)
        : declaringType_(std::move(declaringType)) {}

    // The C# `MethodListWithDeclaringType(IType declaringType,
    // IEnumerable<IParameterizedMember> methods)`.
    MethodListWithDeclaringType(
        ILSpy::Decompiler::TypeSystem::ITypePtr declaringType,
        std::initializer_list<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*> methods)
        : std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>(methods),
          declaringType_(std::move(declaringType)) {}

    // The C# `MethodListWithDeclaringType(IType declaringType,
    // IEnumerable<IParameterizedMember> methods)` (a vector copy).
    MethodListWithDeclaringType(
        ILSpy::Decompiler::TypeSystem::ITypePtr declaringType,
        const std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>& methods)
        : std::vector<const ILSpy::Decompiler::TypeSystem::IParameterizedMember*>(methods),
          declaringType_(std::move(declaringType)) {}

    // The C# `readonly IType DeclaringType` -- the declaring type. A never-null reference
    // return (the C# ctor guards nothing, but the `Methods`/`ToString` paths always
    // construct with a type; the `ITypePtr` held keeps it alive).
    const ILSpy::Decompiler::TypeSystem::IType& DeclaringType() const { return *declaringType_; }

private:
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
};

// The C# `public class MethodGroupResolveResult : ResolveResult` (NOT `sealed`) ports to a
// C++ subclass (NOT `final`) of `Semantics::ResolveResult`. The result has NO type (the
// base is `SpecialType.NoType`); to retrieve the chosen overload / delegate type, look at
// the method-group conversion. The extension-method machinery remains DEFERRED (it needs
// the unported `CSharpResolver` controller); a resolver-less `GetExtensionMethods()` returns
// empty. `PerformOverloadResolution` is ported (see its declaration below).
class MethodGroupResolveResult : public ILSpy::Decompiler::Semantics::ResolveResult {
public:
    // The C# `MethodGroupResolveResult(ResolveResult targetResult, string methodName,
    // IReadOnlyList<MethodListWithDeclaringType> methods, IReadOnlyList<IType>
    // typeArguments) : base(SpecialType.NoType)` -- forwards `NoType()` (a method group has
    // NO type, distinct from `UnknownType()`) and stores the fields (`typeArguments`
    // defaults to empty for the C# `?? EmptyList<IType>.Instance`).
    MethodGroupResolveResult(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> targetResult,
        std::string methodName,
        std::vector<MethodListWithDeclaringType> methods,
        std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments = {})
        : ResolveResult(ILSpy::Decompiler::TypeSystem::NoType()),
          target_(std::move(targetResult)),
          methodName_(std::move(methodName)),
          methodLists_(std::move(methods)),
          typeArguments_(std::move(typeArguments)) {}

    // The C# `ResolveResult TargetResult` -- the target object resolve result. A non-owning
    // raw pointer; `nullptr` when a null shared_ptr was passed.
    ILSpy::Decompiler::Semantics::ResolveResult* TargetResult() const noexcept
    {
        return target_.get();
    }

    // The C# `IType TargetType` => `targetResult != null ? targetResult.Type :
    // SpecialType.UnknownType`. A never-null reference return; the null-target `UnknownType`
    // is cached in `unknownType_` so the reference stays valid.
    const ILSpy::Decompiler::TypeSystem::IType& TargetType() const
    {
        if (target_)
            return target_->Type();
        if (!unknownType_)
            unknownType_ = ILSpy::Decompiler::TypeSystem::UnknownType();
        return *unknownType_;
    }

    // The C# `string MethodName` -- the name of the methods in this group.
    const std::string& MethodName() const noexcept { return methodName_; }

    // The C# `IEnumerable<IMethod> Methods` -- the found methods (NOT extension methods),
    // flattened across declaring-type buckets (base types first). A by-value snapshot of
    // non-owning `const IMethod*` (the faithful `SelectMany(...).Cast<IMethod>()`
    // materialization; each stored `IParameterizedMember*` is a method, so the upcast is
    // exact).
    std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> Methods() const
    {
        std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*> result;
        for (const auto& list : methodLists_)
            for (const auto* member : list)
                result.push_back(static_cast<const ILSpy::Decompiler::TypeSystem::IMethod*>(member));
        return result;
    }

    // The C# `IEnumerable<MethodListWithDeclaringType> MethodsGroupedByDeclaringType` --
    // the buckets (base types first). A by-value snapshot (read-only; the caller cannot
    // mutate the group's stored buckets).
    std::vector<MethodListWithDeclaringType> MethodsGroupedByDeclaringType() const
    {
        return methodLists_;
    }

    // The C# `IReadOnlyList<IType> TypeArguments` -- the explicitly provided type
    // arguments. A by-value snapshot of shared `IType` handles.
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> TypeArguments() const
    {
        return typeArguments_;
    }

    // The C# `IMethod ChosenMethod` -- the method chosen for the group (only set for
    // results in ILSpy AST annotations); null by default.
    const ILSpy::Decompiler::TypeSystem::IMethod* ChosenMethod() const noexcept
    {
        return chosenMethod_;
    }

    // The C# `MethodGroupResolveResult WithChosenMethod(IMethod method)` -- a
    // `ShallowClone` with `chosenMethod` set (the original untouched). Returns a fresh
    // owning `unique_ptr`.
    std::unique_ptr<MethodGroupResolveResult> WithChosenMethod(
        const ILSpy::Decompiler::TypeSystem::IMethod* method) const
    {
        auto result = std::make_unique<MethodGroupResolveResult>(*this);
        result->chosenMethod_ = method;
        return result;
    }

    // The C# `override string ToString()` => `"[MethodGroupResolveResult with N
    // method(s)]"` (a custom format, NOT the inherited bracket form; `GetType().Name` is
    // "MethodGroupResolveResult" for this unsealed class). `Methods().size()` is the
    // flattened method count across buckets (the C# `Methods.Count()`).
    std::string ToString() const override
    {
        return "[" + ClassName() + " with " + std::to_string(Methods().size()) + " method(s)]";
    }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()` => `{ targetResult }`
    // when present, empty otherwise.
    std::vector<const ILSpy::Decompiler::Semantics::ResolveResult*> GetChildResults() const override
    {
        if (target_)
            return { target_.get() };
        return {};
    }

    // The C# `IEnumerable<IEnumerable<IMethod>> GetExtensionMethods()` in its resolver-less
    // state. The C# fetches candidate extension methods from the attached `resolver` on
    // first call (caching them in `extensionMethods`); with no resolver it yields
    // `Enumerable.Empty<IEnumerable<IMethod>>()`. The resolver fields and the on-demand
    // `CSharpResolver.GetExtensionMethods` fetch are DEFERRED (they need the unported
    // `CSharpResolver` controller); with no resolver attached the result is empty.
    std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>> GetExtensionMethods() const
    {
        return {};
    }

    // The C# `public OverloadResolution PerformOverloadResolution(ICompilation compilation,
    // ResolveResult[] arguments, string[] argumentNames = null, bool allowExtensionMethods =
    // true, bool allowExpandingParams = true, bool allowOptionalParameters = true, bool
    // allowImplicitIn = true, bool checkForOverflow = false, CSharpConversions conversions =
    // null)` (lines 248-307) -- performs overload resolution on this method group: builds an
    // `OverloadResolution` over the given arguments (with this group's explicitly provided
    // `TypeArguments` as the given type arguments), sets the four input properties, adds the
    // group's own method lists, and -- when `allowExtensionMethods` and no applicable
    // candidate was found -- retries with the extension methods (the receiver prepended as
    // the first argument, `IsExtensionMethodInvocation` set). The C# returns the live
    // `OverloadResolution` reference; the port returns an owning `unique_ptr` (a factory --
    // the single ownership transfers to the caller).
    //
    // The extension-method arm is structurally complete but currently INERT: with no resolver
    // attached, `GetExtensionMethods()` yields empty, so the `extensionMethods.Any()` guard
    // skips the whole block (the documented resolver-less state; the arm goes live when the
    // resolver-attached `GetExtensionMethods` lands).
    //
    // The C# `argumentNames` null default ports to `std::nullopt`; the `conversions` null
    // default to a nullable pointer (the `OverloadResolution` ctor's lazy
    // `CSharpConversions::Get` fallback resolves it at the first engine call).
    std::unique_ptr<OverloadResolution> PerformOverloadResolution(
        const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
        const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
        const std::optional<std::vector<std::string>>& argumentNames = std::nullopt,
        bool allowExtensionMethods = true,
        bool allowExpandingParams = true,
        bool allowOptionalParameters = true,
        bool allowImplicitIn = true,
        bool checkForOverflow = false,
        const CSharpConversions* conversions = nullptr) const;

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields. The C++ override reproduces this via the default copy
    // ctor, avoiding the C++-only slicing a non-overriding base clone
    // (`make_unique<ResolveResult>(*this)`) would perform.
    std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ShallowClone() const override
    {
        return std::make_unique<MethodGroupResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields. Overridden for RTTI
    // consistency; the custom `ToString` above uses a fixed format and does NOT consult it
    // (the C# `ToString` uses `GetType().Name`, which is always "MethodGroupResolveResult"
    // here -- this is the unsealed leaf, so it matches).
    std::string ClassName() const override { return "MethodGroupResolveResult"; }

private:
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target_;
    std::string methodName_;
    std::vector<MethodListWithDeclaringType> methodLists_;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments_;
    const ILSpy::Decompiler::TypeSystem::IMethod* chosenMethod_ = nullptr;
    // The lazily-built null-target `SpecialType.UnknownType` (mutable so the const
    // `TargetType()` can cache it). Holds the `SpecialType UnknownType` alive for the
    // reference return.
    mutable ILSpy::Decompiler::TypeSystem::ITypePtr unknownType_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver

#endif // ILSPY_DECOMPILER_CSHARP_RESOLVER_METHODGROUPRESOLVERESULT_HPP
