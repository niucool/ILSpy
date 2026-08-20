// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IParameter.cs (the `IParameter` interface, a
// method/property parameter). `IParameter : IVariable` adds the parameter's attributes
// (`GetAttributes`), the reference kind (`ReferenceKind` -- None/Out/Ref/In/RefReadOnly,
// the ported `ReferenceKind` enum), the C# 11 scoped annotation (`Lifetime`, the ported
// `LifetimeAnnotation` struct), the `IsParams` / `IsOptional` / `HasConstantValueInSignature`
// flags, and the owning `IParameterizedMember` (nullable -- null for lambda/anonymous-method
// parameters). It is the next leaf toward `IParameterizedMember` (whose `Parameters` list
// holds `IParameter`s) and the member family (`IMethod` / `IProperty` are
// `IParameterizedMember`s).
//
// The cyclic member types `IAttribute` and `IParameterizedMember` are only forward-declared
// here (they are not yet ported): `GetAttributes` returns `std::vector<const IAttribute*>`
// (a non-owning snapshot -- the element type is a complete pointer type regardless of the
// pointee's completeness, so the vector instantiates with only `IAttribute` forward-declared,
// the `IEntity::GetAttributes` precedent), and `Owner` returns `const IParameterizedMember*`
// (a nullable pointer, the `IEntity::ParentModule` precedent). A null pointer is the C# null.
// `Lifetime` returns `LifetimeAnnotation` by value (the C# struct return), so
// `LifetimeAnnotation.hpp` must be complete (included).
//
// `IParameter` declares NO `new string Name` (unlike `IField`, which redeclares `Name` to
// disambiguate `IMember.Name` and `IVariable.Name`): the inherited `IVariable` / `ISymbol`
// `Name()` already covers the parameter's name, and there is no second base introducing a
// `Name`, so no redeclaration is needed (the D374 single-inheritance "inherited virtual
// covers the `new`" precedent -- here there is no `new` at all; the diamond only arises for
// `IField : IMember, IVariable`).

#pragma once

#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations of the cyclic member types (not yet ported). `IAttribute` is the
// attribute interface (needs `IType` / `IMethod` + the `CustomAttributeTypedArgument` /
// `CustomAttributeNamedArgument` value-argument structs); `IParameterizedMember` is the
// method/property base (needs `IMember`, which needs `TypeParameterSubstitution` /
// `TypeVisitor`). A pointer return needs only a forward declaration (the
// `IEntity::ParentModule` / `ICompilationProvider` precedent).
class IAttribute;
class IParameterizedMember;

// A parameter of a method or property. `IParameter : IVariable` adds the parameter's
// attributes, reference kind, C# 11 scoped annotation, the params / optional /
// has-constant-value-in-signature flags, and the owning parameterized member (nullable).
// The concrete `DefaultParameter` / `MetadataParameter` / `SpecializedParameter`
// implementations land with the rest of the Phase-5 type system.
class IParameter : public IVariable {
public:
    // The C# `IEnumerable<IAttribute> GetAttributes()` -- the attributes on this parameter
    // (a non-owning snapshot; the parameter owns its attributes, the caller holds raw
    // pointers). The `IEntity::GetAttributes` precedent applied to a parameter.
    virtual std::vector<const IAttribute*> GetAttributes() const = 0;

    // The C# `ReferenceKind ReferenceKind { get; }` -- the reference kind (None/Out/Ref/In/
    // RefReadOnly), the ported `ReferenceKind` enum (the C# comment notes the order should
    // match `CSharp.Syntax.FieldDirection`).
    virtual ReferenceKind ReferenceKind() const = 0;

    // The C# `LifetimeAnnotation Lifetime { get; }` -- the C# 11 scoped annotation (the
    // `scoped ref` annotation). Returned by value (the C# struct return); the consumer reads
    // `Lifetime().ScopedRef()` to decide whether to emit the `scoped` keyword.
    virtual LifetimeAnnotation Lifetime() const = 0;

    // The C# `bool IsParams { get; }` -- whether this parameter is a C# `params` parameter.
    virtual bool IsParams() const = 0;

    // The C# `bool IsOptional { get; }` -- whether this parameter is optional (the default
    // value is given by the inherited `IVariable::GetConstantValue`).
    virtual bool IsOptional() const = 0;

    // The C# `bool HasConstantValueInSignature { get; }` -- whether the parameter has a
    // constant value when presented in the method signature (only true when optional, and
    // true for most optional parameters; false e.g. for `DecimalConstantAttribute`-based
    // defaults when `DecompilerSettings.DecimalConstants` is false).
    virtual bool HasConstantValueInSignature() const = 0;

    // The C# `IParameterizedMember? Owner { get; }` -- the owning method/property, or
    // nullptr for lambda/anonymous-method parameters (the C# `May return null` doc comment).
    // A nullable pointer return (the `IEntity::ParentModule` precedent).
    virtual const IParameterizedMember* Owner() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
