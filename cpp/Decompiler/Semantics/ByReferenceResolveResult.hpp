// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/ByReferenceResolveResult.cs -- the
// resolved expression is a `ref x`, `in x`, or `out x` reference.  This is the
// fifth `Semantics` leaf toward `TypeSystemAstBuilder` / `CSharpAmbience` (the
// long-pole remaining blocker), after `ResolveResult` (D424), `TypeResolveResult`
// (D425), `ThisResolveResult` (D426), and `TypeOfResolveResult` (D427).  The C#
// source declares two ctors: a PUBLIC ctor taking a `ResolveResult elementResult`
// (delegates to the `internal` ctor with `elementResult.Type` and stores the
// `elementResult`) and an `internal` ctor taking an `IType elementType` (builds the
// base `ResolveResult` from a `new ByReferenceType(elementType)` and stores the
// `ReferenceKind`).  It exposes `ReferenceKind`, `ElementResult` (the stored
// `ResolveResult`, null on the `internal`-ctor path), and `ElementType` (the inner
// `IType` of the wrapping `ByReferenceType`); it overrides `GetChildResults`
// (returns the `ElementResult` when non-null) and `ToString` (a custom format with
// the lowercased `ReferenceKind`).  The C++ port additionally overrides
// `ShallowClone()` (the runtime-type-preserving clone, avoiding C++-only slicing) and
// the `protected ClassName()` (used by the inherited `ToString` shape but overridden
// directly here).  All dependencies are already ported: `ResolveResult` (D424),
// `ReferenceKind` (the `IParameter.cs` enum), and the minimal-port `ByReferenceType`
// (the `IType` concrete in `IType.hpp`); a new `protected ResolveResult::TypePtr()`
// accessor (added in this same change) lets the public ctor forward the
// `elementResult`'s type as an `ITypePtr` to the delegating `internal` ctor.

#ifndef ILSPY_DECOMPILER_SEMANTICS_BYREFERENCERESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_BYREFERENCERESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ByReferenceResolveResult : ResolveResult` (NOT `sealed`, and
// no C# subclass derives from it) ports to a C++ subclass (NOT `final`) of
// `ResolveResult`.  The C# surface is:
//   * `public ByReferenceResolveResult(ResolveResult elementResult, ReferenceKind
//     kind) : this(elementResult.Type, kind) { this.ElementResult = elementResult; }`
//     -- the public ctor delegates to the `internal` ctor with the element result's
//     `Type` and then stores the `elementResult` field.
//   * `internal ByReferenceResolveResult(IType elementType, ReferenceKind kind)
//     : base(new ByReferenceType(elementType)) { this.ReferenceKind = kind; }` -- the
//     `internal` (assembly-private) ctor builds the base `ResolveResult` from a
//     `new ByReferenceType(elementType)` and stores the `kind`.  The C++ port keeps
//     this ctor PUBLIC (C++ has no assembly boundary) but documents it as the
//     for-temporary-use ctor the C# `internal` gate restricts.
//   * `public ReferenceKind ReferenceKind { get; }` -- the stored `ReferenceKind`.
//   * `public readonly ResolveResult ElementResult` -- the stored `elementResult`
//     (null on the `internal`-ctor path).
//   * `public IType ElementType => ((ByReferenceType)this.Type).ElementType` -- the
//     inner `IType` of the wrapping `ByReferenceType`.
//   * `override IEnumerable<ResolveResult> GetChildResults()` -- returns the
//     `ElementResult` when non-null, else empty.
//   * `override string ToString()` -- `"[{0} {1} {2}]"` with `GetType().Name`, the
//     lowercased `ReferenceKind`, and the `ElementType`.
//
// The C# does NOT override `ShallowClone` (nor `IsError` / `IsCompileTimeConstant`
// / `ConstantValue`), but the C++ port overrides `ShallowClone()` to faithfully
// reproduce the C# `MemberwiseClone` runtime-type preservation (a non-overriding
// C++ base clone would slice the subclass to a `ResolveResult` base).  The C#
// `ToString` is OVERRIDDEN directly (not relying on the inherited `ClassName`-based
// `ToString`), so the C++ port overrides `ToString` directly too and provides the
// `protected ClassName()` for any future caller that reaches the base shape.
class ByReferenceResolveResult : public ResolveResult {
public:
    // The C# `public ByReferenceResolveResult(ResolveResult elementResult,
    // ReferenceKind kind) : this(elementResult.Type, kind)`: delegates to the
    // `internal` ctor (below) with the element result's `Type` (extracted as an
    // `ITypePtr` via the `protected ResolveResult::TypePtr()` accessor), then
    // stores the `elementResult`.  The `elementResult` is held as a
    // `shared_ptr<ResolveResult>` (the faithful C# GC-reference model -- the
    // caller and the `ByReferenceResolveResult` share the `ResolveResult`, and
    // `ShallowClone`'s default copy ctor shares it too, mirroring
    // `MemberwiseClone`'s reference-copy).  The delegating ctor evaluates
    // `elementResult->TypePtr()` before `elementResult` is moved into the member
    // (the move happens in the body), so the extraction is valid.
    ByReferenceResolveResult(std::shared_ptr<ResolveResult> elementResult,
                             ILSpy::Decompiler::TypeSystem::ReferenceKind kind)
        : ByReferenceResolveResult(elementResult->TypePtr(), kind) {
        elementResult_ = std::move(elementResult);
    }

    // The C# `internal ByReferenceResolveResult(IType elementType, ReferenceKind
    // kind) : base(new ByReferenceType(elementType))`: builds the base
    // `ResolveResult` from a `ByReferenceType` wrapping the `elementType` (the
    // `make_shared<ByReferenceType>` is the faithful `new ByReferenceType`), and
    // stores the `kind`.  The `elementResult_` member is left null (the
    // `internal`-ctor path has no `ElementResult`).  Public in C++ (no assembly
    // boundary); documented as the for-temporary-use ctor the C# `internal` gate
    // restricts.
    ByReferenceResolveResult(ILSpy::Decompiler::TypeSystem::ITypePtr elementType,
                             ILSpy::Decompiler::TypeSystem::ReferenceKind kind)
        : ResolveResult(std::make_shared<ILSpy::Decompiler::TypeSystem::ByReferenceType>(elementType)),
          referenceKind_(kind) {}

    // The C# `public ReferenceKind ReferenceKind { get; }` -- the stored
    // `ReferenceKind` (a value member, copied trivially by the default copy ctor).
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const noexcept {
        return referenceKind_;
    }

    // The C# `public readonly ResolveResult ElementResult` -- the stored
    // `elementResult` (null on the `internal`-ctor path).  Returns a non-owning
    // raw pointer (the `shared_ptr` keeps the `ResolveResult` alive for the
    // `ByReferenceResolveResult`'s lifetime); `nullptr` when the result was
    // constructed via the `internal` ctor.
    ResolveResult* ElementResult() const noexcept { return elementResult_.get(); }

    // The owning `shared_ptr` handle behind `ElementResult()` (the C# field is a GC
    // reference the annotation channel aliases; consumers re-owning the element need
    // the shared view).
    const std::shared_ptr<ResolveResult>& ElementResultShared() const noexcept {
        return elementResult_;
    }

    // The C# `public IType ElementType => ((ByReferenceType)this.Type).ElementType`
    // -- the inner `IType` of the wrapping `ByReferenceType`.  `Type()` is always a
    // `ByReferenceType` (both ctors build the base from one), so the
    // `static_cast` downcast is safe; `ByReferenceType::Element()` returns the
    // stored `ITypePtr`, dereferenced to the non-null `const IType&` (the D374
    // non-null-reference convention; the `ByReferenceType` ctor stores a non-null
    // element).
    const ILSpy::Decompiler::TypeSystem::IType& ElementType() const {
        return *static_cast<const ILSpy::Decompiler::TypeSystem::ByReferenceType&>(Type()).Element();
    }

    // The C# `override IEnumerable<ResolveResult> GetChildResults()`: returns the
    // `ElementResult` (a one-element snapshot) when non-null, else empty.  The
    // base default returns empty, so the `internal`-ctor path (no `ElementResult`)
    // inherits the empty behavior via this override returning `{}`.
    std::vector<const ResolveResult*> GetChildResults() const override {
        if (elementResult_)
            return { elementResult_.get() };
        return {};
    }

    // The C# `override string ToString() =>
    // string.Format(CultureInfo.InvariantCulture, "[{0} {1} {2}]", GetType().Name,
    // ReferenceKind.ToString().ToLowerInvariant(), ElementType)`.  The format is
    // `[ByReferenceResolveResult <lowercased ReferenceKind> <ElementType>]` where
    // `ElementType`'s `ToString` (the C# `AbstractType.ToString =>
    // ReflectionName`) ports to `ReflectionName()`.
    std::string ToString() const override {
        return "[" + ClassName() + " " + ReferenceKindToLower(referenceKind_) + " " + ElementType().ReflectionName() + "]";
    }

    // The C# `ShallowClone` (inherited from `ResolveResult`) uses `MemberwiseClone`
    // which preserves the runtime type and shallow-copies the fields (the
    // `referenceKind_` value and the `elementResult_` `shared_ptr` -- the latter
    // shares the `ElementResult` with the original, faithful to the C# reference
    // copy).  The C++ override reproduces this by constructing a
    // `ByReferenceResolveResult` copy (the default copy ctor copies the value
    // `referenceKind_` and shares the `elementResult_` `shared_ptr` and the base
    // `type_` `shared_ptr`), avoiding the C++-only slicing a non-overriding base
    // clone would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ByReferenceResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the
    // overridden `ToString` above (and available to any future caller of the base
    // `ToString` shape).
    std::string ClassName() const override { return "ByReferenceResolveResult"; }

private:
    // The C# `ReferenceKind.ToString().ToLowerInvariant()` -- the lowercased enum
    // name for the `ToString` format.  `ReferenceKind` is a `TypeSystem` enum
    // (sibling namespace), qualified through the parameter type.
    static std::string ReferenceKindToLower(ILSpy::Decompiler::TypeSystem::ReferenceKind kind) {
        switch (kind) {
            case ILSpy::Decompiler::TypeSystem::ReferenceKind::None:
                return "none";
            case ILSpy::Decompiler::TypeSystem::ReferenceKind::Out:
                return "out";
            case ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref:
                return "ref";
            case ILSpy::Decompiler::TypeSystem::ReferenceKind::In:
                return "in";
            case ILSpy::Decompiler::TypeSystem::ReferenceKind::RefReadOnly:
                return "readonlyref";
        }
        return "none";
    }

    ILSpy::Decompiler::TypeSystem::ReferenceKind referenceKind_;
    std::shared_ptr<ResolveResult> elementResult_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_BYREFERENCERESOLVERESULT_HPP
