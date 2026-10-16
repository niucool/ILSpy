// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Semantics/ForEachResolveResult.cs -- the result of a
// `foreach` loop. It is the twenty-second `Semantics` leaf toward `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker), deriving directly from
// `ResolveResult` (D424) and adding six readonly fields describing the resolved foreach
// pattern: the `GetEnumeratorCall` (a `ResolveResult`), the `CollectionType` /
// `EnumeratorType` / `ElementType` (three `IType`s), the nullable `CurrentProperty` (an
// `IProperty`), and the nullable `MoveNextMethod` (an `IMethod`); the `voidType` is
// forwarded to the base. All deps are already ported: `ResolveResult` (D424), `IType`
// (D271, for the four `IType` fields), `IProperty` (D390, for the nullable
// `CurrentProperty`), `IMethod` (D389, for the nullable `MoveNextMethod`). The C# class
// declares NO virtual overrides: it inherits the `ResolveResult` defaults
// (`GetChildResults` empty, `IsError` false, `IsCompileTimeConstant` false,
// `ConstantValue` empty) and adds only the two C++-only convention overrides
// (`ClassName` / `ShallowClone`).

#ifndef ILSPY_DECOMPILER_SEMANTICS_FOREACHRESOLVERESULT_HPP
#define ILSPY_DECOMPILER_SEMANTICS_FOREACHRESOLVERESULT_HPP

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <memory>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {
// Forward declarations of the nullable member back-reference types. A pointer member (and
// a pointer-returning accessor) to an incomplete type needs only a forward declaration (the
// `IEntity::ParentModule` / `IProperty::Getter` nullable-pointer precedent), keeping the
// include graph minimal -- the real `IProperty.hpp` / `IMethod.hpp` are pulled only by a
// consumer that dereferences the pointers.
class IProperty;
class IMethod;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::Semantics {

// The C# `public class ForEachResolveResult : ResolveResult` (NOT `sealed`) ports to a
// C++ subclass (NOT `final`) of `ResolveResult`. The C# surface is:
//   * `readonly ResolveResult GetEnumeratorCall` -- the semantic tree for the call to
//     `GetEnumerator` (non-null; the ctor throws `ArgumentNullException` on null).
//   * `readonly IType CollectionType` -- the collection type (non-null; guarded).
//   * `readonly IType EnumeratorType` -- the enumerator type (non-null; guarded).
//   * `readonly IType ElementType` -- the element type (non-null; guarded); this is the
//     type that would be inferred for an implicitly-typed element variable.
//   * `readonly IProperty CurrentProperty` -- the `Current` property on the
//     `IEnumerator`, or null if the property is not found (NOT guarded).
//   * `readonly IMethod MoveNextMethod` -- the `MoveNext()` method on the `IEnumerator`,
//     or null if the method is not found (NOT guarded).
//   * Ctor `(ResolveResult getEnumeratorCall, IType collectionType, IType enumeratorType,
//     IType elementType, IProperty currentProperty, IMethod moveNextMethod, IType voidType)
//     : base(voidType)` -- forwards the void type to the base and stores the six fields
//     (four guarded with `ArgumentNullException`; `currentProperty` / `moveNextMethod` are
//     NOT guarded -- they may be null).
//
// KEY PORT CONVENTIONS:
//  * The C# `readonly ResolveResult GetEnumeratorCall` (a non-null reference-type field
//    the ctor guards with `ArgumentNullException`) ports to a `std::shared_ptr<ResolveResult>`
//    member guarded with `assert` (the D439 `ArrayAccessResolveResult::array_` precedent:
//    shared ownership so `ShallowClone`'s default copy ctor shares it faithfully mirroring
//    the C# `MemberwiseClone` reference-copy). `GetEnumeratorCall()` returns a non-owning
//    raw pointer (the D439 `Array()` precedent for a single held `ResolveResult` field).
//  * The C# `readonly IType CollectionType` / `EnumeratorType` / `ElementType` (three
//    non-null reference-type fields the ctor guards with `ArgumentNullException`) port to
//    three `ITypePtr` (`shared_ptr<IType>`) members guarded with `assert` (the D427
//    `TypeOfResolveResult::referencedType_` precedent for a held `IType` field: shared
//    ownership so `ShallowClone`'s default copy ctor shares each, faithfully mirroring the C#
//    reference-copy). The accessors return `const IType&` (the D427 `ReferencedType()`
//    non-null-reference convention), the held `shared_ptr` keeping the `IType` alive.
//  * The C# `readonly IProperty CurrentProperty` / `IMethod MoveNextMethod` (two
//    NULLABLE reference-type fields the ctor does NOT guard -- "Returns null if the
//    property / method is not found") port to nullable non-owning raw pointers
//    `const IProperty*` / `const IMethod*` (the `IEntity::ParentModule` /
//    `IProperty::Getter` nullable-pointer precedent: the type system owns the member, the
//    caller holds a raw pointer that may be null). The forward-declared pointees keep the
//    include graph minimal. `ShallowClone`'s default copy ctor copies the raw pointers
//    faithfully mirroring the C# reference-copy (the pointers stay valid because the type
//    system owns the members, outliving the clone).
//  * The C# `voidType` (forwarded to the `ResolveResult` base) is asserted non-null by the
//    inherited base ctor (the D424 convention); it has NO separate member.
//  * `ToString` is inherited (the D425/D426/D427 inherit-`ToString`-override-`ClassName`
//    convention): overriding `ClassName()` to "ForEachResolveResult" makes the inherited
//    base `ResolveResult::ToString` yield "[ForEachResolveResult <voidType>]".
//  * `GetChildResults` is inherited (the C# does NOT override it, so the base default --
//    empty -- applies): a `foreach` loop's `GetEnumeratorCall` is a public readonly FIELD,
//    not a child result (the D431 `TypeIsResolveResult::Input` precedent: a field, not a
//    child).
//  * `ShallowClone` is overridden (the D424 slicing-prevention convention): the C# inherited
//    `MemberwiseClone` preserves the runtime type, but a non-overriding C++ base clone
//    (`make_unique<ResolveResult>(*this)`) would SLICE a `ForEachResolveResult` to its
//    base, so the override does `make_unique<ForEachResolveResult>(*this)` (the default
//    copy ctor shares the `getEnumeratorCall_` shared_ptr and the three `ITypePtr`s and
//    copies the two raw pointers faithfully mirroring the C# reference-copy).
class ForEachResolveResult : public ResolveResult {
public:
    // The C# ctor `(ResolveResult getEnumeratorCall, IType collectionType, IType
    // enumeratorType, IType elementType, IProperty currentProperty, IMethod moveNextMethod,
    // IType voidType) : base(voidType)` -- forwards the void type to the `ResolveResult`
    // base (which asserts it non-null), then stores the four non-null fields (asserted in
    // the body, the D424 assert-then-move convention) and the two nullable member
    // back-references (no assert). The base ctor member-init runs first, then the body
    // asserts the four non-null fields before the moves into the members.
    ForEachResolveResult(std::shared_ptr<ResolveResult> getEnumeratorCall,
                         ILSpy::Decompiler::TypeSystem::ITypePtr collectionType,
                         ILSpy::Decompiler::TypeSystem::ITypePtr enumeratorType,
                         ILSpy::Decompiler::TypeSystem::ITypePtr elementType,
                         const ILSpy::Decompiler::TypeSystem::IProperty* currentProperty,
                         const ILSpy::Decompiler::TypeSystem::IMethod* moveNextMethod,
                         ILSpy::Decompiler::TypeSystem::ITypePtr voidType)
        : ResolveResult(std::move(voidType)) {
        assert(getEnumeratorCall && "ForEachResolveResult: getEnumeratorCall must not be null");
        assert(collectionType && "ForEachResolveResult: collectionType must not be null");
        assert(enumeratorType && "ForEachResolveResult: enumeratorType must not be null");
        assert(elementType && "ForEachResolveResult: elementType must not be null");
        getEnumeratorCall_ = std::move(getEnumeratorCall);
        collectionType_ = std::move(collectionType);
        enumeratorType_ = std::move(enumeratorType);
        elementType_ = std::move(elementType);
        currentProperty_ = currentProperty;
        moveNextMethod_ = moveNextMethod;
    }

    // The C# `readonly ResolveResult GetEnumeratorCall` -- the semantic tree for the call
    // to `GetEnumerator`. Returns a non-owning raw pointer (the `shared_ptr` keeps the
    // `ResolveResult` alive); never null (the ctor asserts it).
    ResolveResult* GetEnumeratorCall() const noexcept { return getEnumeratorCall_.get(); }

    // The C# `readonly IType CollectionType` -- the collection type. Returns a non-null
    // reference (the held `shared_ptr` keeps the `IType` alive).
    const ILSpy::Decompiler::TypeSystem::IType& CollectionType() const { return *collectionType_; }

    // The C# `readonly IType EnumeratorType` -- the enumerator type. Returns a non-null
    // reference (the held `shared_ptr` keeps the `IType` alive).
    const ILSpy::Decompiler::TypeSystem::IType& EnumeratorType() const { return *enumeratorType_; }

    // The C# `readonly IType ElementType` -- the element type (the type that would be
    // inferred for an implicitly-typed element variable). Returns a non-null reference
    // (the held `shared_ptr` keeps the `IType` alive).
    const ILSpy::Decompiler::TypeSystem::IType& ElementType() const { return *elementType_; }

    // The C# `readonly IProperty CurrentProperty` -- the `Current` property on the
    // `IEnumerator`, or null if the property is not found. Returns a nullable non-owning
    // raw pointer to the forward-declared `IProperty`.
    const ILSpy::Decompiler::TypeSystem::IProperty* CurrentProperty() const noexcept {
        return currentProperty_;
    }

    // The C# `readonly IMethod MoveNextMethod` -- the `MoveNext()` method on the
    // `IEnumerator`, or null if the method is not found. Returns a nullable non-owning
    // raw pointer to the forward-declared `IMethod`.
    const ILSpy::Decompiler::TypeSystem::IMethod* MoveNextMethod() const noexcept {
        return moveNextMethod_;
    }

    // The C# `ShallowClone` (inherited `MemberwiseClone`) preserves the runtime type and
    // shallow-copies the fields (the `getEnumeratorCall_` shared_ptr is shared, the three
    // `ITypePtr`s are shared, the two raw pointers are copied, and the base `type_`
    // shared_ptr is shared). The C++ override reproduces this via the default copy ctor,
    // avoiding the C++-only slicing a non-overriding base clone would perform.
    std::unique_ptr<ResolveResult> ShallowClone() const override {
        return std::make_unique<ForEachResolveResult>(*this);
    }

protected:
    // The runtime class name the C# `GetType().Name` yields; used by the inherited
    // `ResolveResult::ToString`.
    std::string ClassName() const override { return "ForEachResolveResult"; }

private:
    std::shared_ptr<ResolveResult> getEnumeratorCall_;
    ILSpy::Decompiler::TypeSystem::ITypePtr collectionType_;
    ILSpy::Decompiler::TypeSystem::ITypePtr enumeratorType_;
    ILSpy::Decompiler::TypeSystem::ITypePtr elementType_;
    const ILSpy::Decompiler::TypeSystem::IProperty* currentProperty_;
    const ILSpy::Decompiler::TypeSystem::IMethod* moveNextMethod_;
};

} // namespace ILSpy::Decompiler::Semantics

#endif // ILSPY_DECOMPILER_SEMANTICS_FOREACHRESOLVERESULT_HPP
