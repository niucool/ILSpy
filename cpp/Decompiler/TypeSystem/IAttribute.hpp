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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IAttribute.cs -- `IAttribute` is the interface
// for a resolved custom attribute (a `[Attribute(...)]` applied to an entity). It carries
// the attribute's `AttributeType` (the `[...]` type), the resolved `Constructor` (nullable --
// null when no matching constructor was found), the `HasDecodeErrors` flag, the positional
// `FixedArguments`, and the `NamedArguments`. The `CustomAttributeDecoder` builds these
// from the attribute blob; `TypeSystemAstBuilder.ConvertAttribute` reads every accessor to
// build the `Attribute` AST node (TypeSystemAstBuilder.cs lines 791-813). `IEntity` /
// `IParameter` / `ITypeParameter` / `IMethod` expose attributes via `GetAttributes` /
// `GetReturnTypeAttributes`, so `IAttribute` is a leaf consumed across the member family.
//
// It lands now that all its deps are ported: `IType` (D271, the `AttributeType` return),
// the `CustomAttributeTypedArgument` / `CustomAttributeNamedArgument` value-argument structs
// (D385, the `FixedArguments` / `NamedArguments` element types). The remaining dep `IMethod`
// (the `Constructor` slot) is NOT yet ported, but a pointer return needs only a forward
// declaration (the `IEntity::ParentModule` / `ICompilationProvider` precedent), so `IAttribute`
// compiles with `IMethod` incomplete. `IMethod` <-> `IAttribute` is a cyclic pair (`IMethod`
// exposes `GetReturnTypeAttributes` returning `IEnumerable<IAttribute>`), resolved here the
// same way the `IEntity` <-> `ITypeDefinition` cyclic triangle was resolved in D381: forward
// declarations plus pointer/reference returns.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IType AttributeType { get; }` is NON-NULL (the `DefaultAttribute` ctor throws
//      `ArgumentNullException` on a null type, and the `CustomAttribute` implementation falls
//      back to `SpecialType.UnknownType`). It ports to `const IType& AttributeType() const`
//      -- a non-null reference return, the `IVariable::Type()` D374 "non-null owned-type
//      reference" convention (the concrete implementation holds the `IType` via an `ITypePtr`
//      or a value member and returns a reference valid for the attribute's lifetime). This is
//      distinct from the nullable `ITypePtr` returns of `IEntity::DeclaringType` /
//      `ITypeParameter::EffectiveBaseClass` (which model a possibly-absent type); the
//      `AttributeType` contract is never-null.
//  (b) The C# `IMethod? Constructor { get; }` (nullable -- "may return null if no matching
//      constructor was found") ports to `const IMethod*` (a nullable raw pointer, the
//      `IEntity::ParentModule` precedent). `IMethod` is forward-declared; a null pointer is
//      the C# `null`.
//  (c) The C# `ImmutableArray<CustomAttributeTypedArgument<IType>> FixedArguments` (the
//      positional arguments) and `ImmutableArray<CustomAttributeNamedArgument<IType>>
//      NamedArguments` (the named arguments) port to `std::vector<CustomAttributeTypedArgument>`
//      and `std::vector<CustomAttributeNamedArgument>` returned BY VALUE (the D385 value-struct
//      convention -- the element types are concrete value structs, so the snapshot owns its
//      argument values directly). The `<IType>` BCL generic instantiation is absorbed into the
//      `TypeSystem` namespace (the D385 BCL-absorption convention).
//  (d) NO name-hiding qualification is needed: `IAttribute` does NOT derive from `ISymbol`
//      (it is a standalone interface), so there is no inherited `SymbolKind()` to hide the
//      `SymbolKind` enum, and none of `AttributeType` / `Constructor` / `HasDecodeErrors` /
//      `FixedArguments` / `NamedArguments` collides with a namespace-scope type in the
//      `TypeSystem` namespace -- the D375 `INamedElement` / D380 `Nullability` collision-free-
//      accessor convention applies.
//  (e) `IAttribute` declares NO `new string Name` (it has no `Name` accessor at all), so there
//      is no multiple-inheritance diamond to disambiguate -- unlike `IEntity` (D381) and
//      `ITypeParameter` (D383).

#pragma once

#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of the not-yet-ported `IMethod` (the method interface). `IAttribute`'s
// `Constructor` slot returns `IMethod?` and `IMethod`'s `GetReturnTypeAttributes` returns
// `IEnumerable<IAttribute>`, so the two are a cyclic pair. A pointer return needs only a
// forward declaration (the `IEntity::ParentModule` / `ICompilationProvider` precedent), so
// `IAttribute` compiles with `IMethod` incomplete; it lands as a separate later leaf.
class IMethod;

// A resolved custom attribute. A concrete attribute (`DefaultAttribute` / `CustomAttribute` /
// the `MetadataAttribute` variants -- the implementations land with the rest of the Phase-5
// type system) subclasses `IAttribute` and overrides every accessor. Equality is by identity
// (the C# interface has no equality contract); concrete attributes may add their own.
class IAttribute {
public:
    virtual ~IAttribute() = default;

    // The C# `IType AttributeType { get; }` -- the attribute's type (the `[...]` type).
    // NON-NULL (the implementations throw on a null type or fall back to `UnknownType`); a
    // non-null reference return (the `IVariable::Type()` D374 convention).
    virtual const IType& AttributeType() const = 0;

    // The C# `IMethod? Constructor { get; }` -- the constructor the attribute applied, or
    // null if no matching constructor was found. A nullable raw pointer (`IMethod` is
    // forward-declared; the `IEntity::ParentModule` precedent).
    virtual const IMethod* Constructor() const = 0;

    // The C# `bool HasDecodeErrors { get; }` -- whether there were errors decoding the
    // attribute (the decompiler emits such an attribute as a comment rather than an
    // `Attribute` node).
    virtual bool HasDecodeErrors() const = 0;

    // The C# `ImmutableArray<CustomAttributeTypedArgument<IType>> FixedArguments { get; }` --
    // the positional arguments. A by-value snapshot (the element type is a value struct, so
    // the snapshot owns its argument values directly; the D385 convention).
    virtual std::vector<CustomAttributeTypedArgument> FixedArguments() const = 0;

    // The C# `ImmutableArray<CustomAttributeNamedArgument<IType>> NamedArguments { get; }` --
    // the named arguments (field/property setters). A by-value snapshot (the D385 convention).
    virtual std::vector<CustomAttributeNamedArgument> NamedArguments() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
