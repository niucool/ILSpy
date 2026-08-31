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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `AstType` abstract base in ICSharpCode.Decompiler/CSharp/Syntax/AstType.cs --
// the first slice of the `AstType` hierarchy (a type reference in the C# AST), the next
// in-order Phase-5 piece per the D235 plan ("the AstType hierarchy
// (SimpleType/MemberType/ComposedType/PrimitiveType/...) which unblocks the AstType-bearing
// nodes (CastExpression an Expression + an AstType, IdentifierExpression's TypeArguments
// AstNodeCollection<AstType>, AsExpression/IsExpression, TypeOfExpression,
// TypeReferenceExpression, DefaultValueExpression/SizeOfExpression whose single slot is
// [Slot("Type")] AstType)").
//
// `AstType` is the common base of every C# type-reference node (`SimpleType`, `MemberType`,
// `ComposedType`, `PrimitiveType`, `FunctionPointerAstType`, `InvocationAstType`,
// `TupleAstType`). It is an abstract, otherwise empty `partial class : AstNode` carrying the
// `[DecompilerAstNode(hasPatternPlaceholder: true)]` attribute; the generator emits a pattern
// placeholder (the `implicit operator AstType(Pattern)` + a sealed `PatternPlaceholder` nested
// class wrapping a `Pattern`) because `hasPatternPlaceholder` is true, and the hand-written
// part adds a typed `Clone` plus several convenience builders/queries (`IsVar`,
// `GetNameLookupMode`, `MakePointerType`/`MakeArrayType`/`MakeNullableType`/`MakeRefType`,
// `MemberType`, `Create`). The pattern placeholder lands when the concrete pattern nodes
// (`AnyNode`/`NamedNode`/...) and `VisitPatternPlaceholder` on `IAstVisitor` are ported (the
// D219 deferral), so it is deferred here -- the abstract base and the concrete type nodes do
// not depend on it.
//
// The typed `Clone` ports as a covariant pure-virtual override: `AstNode::Clone()` returns
// `AstNode*` (its base body throws, since C++ has no `MemberwiseClone`); `AstType`
// re-declares it `virtual AstType* Clone() const override = 0` so a call through an `AstType*`
// returns an `AstType*` (the typed return the C# `new AstType Clone()` gives), and a concrete
// type node MUST override it (the redeclaration makes it pure, so the base throwing body is
// unreachable through the type hierarchy). The convenience builders/queries were deferred with
// the D229-D235 stage (the resolver/output stage that consumes them was unported); with the
// CSharpResolver chain complete (the `TypeSystemAstBuilder` skeleton landed, its `ConvertType`
// path now consuming the builders), the `Make*` builders land here:
// `MakePointerType`/`MakeArrayType`/`MakeNullableType`/`MakeRefType` (AstType.cs lines 75-105)
// construct `ComposedType` wrappers, so their bodies are out-of-line in `AstType.cpp` (a
// `ComposedType.hpp` include from this header would be circular: `ComposedType` derives
// `AstType`). `IsVar`/`GetNameLookupMode` remain deferred (they reference not-yet-ported
// nodes -- `UsingDeclaration`/`UsingAliasDeclaration`/`TypeDeclaration`/`Constraint` -- and
// the `NameLookupMode`-consuming name-lookup stage), as does `MemberType` (the
// type-argument-collection convenience ctor, an `AddRange` consumer) and the pattern
// placeholder.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_ASTTYPE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_ASTTYPE_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract partial class AstType : AstNode`. Abstract: a concrete type node
// overrides at least `DoMatch`, `AcceptVisitor`, and `Clone`. Derives from `AstNode` (the
// annotation channel + the pattern-match interface + the slot-storage contract); `AstNode` is
// polymorphic, so `AstType` is too (the `dynamic_cast`-based is-a tests the slot system and
// the annotation channel use stay valid).
class AstType : public AstNode {
public:
    ~AstType() override = default;
    AstType() = default;
    AstType(const AstType&) = delete;
    AstType& operator=(const AstType&) = delete;

    // The C# `public new AstType Clone()` -- a typed clone returning an `AstType` instead
    // of an `AstNode`. The C# `new` hides the base virtual and downcasts `base.Clone()`;
    // this port re-declares the inherited `AstNode::Clone()` (which returns `AstNode*` and
    // has a throwing base body) as a covariant pure-virtual returning `AstType*`, so a call
    // through an `AstType*` is typed and a concrete type node must override it (the base
    // body becomes unreachable through the type hierarchy, matching the C# where every
    // concrete `AstType` overrides the virtual `Clone`). Covariant: `AstType*` derives from
    // `AstNode*`, so this is a valid override of `AstNode::Clone()`.
    AstType* Clone() const override = 0;

    // -----------------------------------------------------------------------
    // The `Make*` builders (the hand-written `AstType` partial, AstType.cs lines 75-105) --
    // the type-reference composition API the `TypeSystemAstBuilder.ConvertType` path
    // consumes to wrap a type reference in a pointer/array/nullable/ref `ComposedType`.
    // Each builder wraps `*this` in a fresh `ComposedType` (`BaseType = this`) and applies
    // the modifier; the `ComposedType` overrides (ComposedType.cs lines 99-123) then
    // EXTEND an existing composed node in place instead of re-wrapping (the
    // `T*` + `*` -> `T**` / `T[]` + `ref` cases). Virtual so that in-place extension
    // dispatches through an `AstType*` static type; `MakeNullableType` is NON-virtual
    // in the C# (no `ComposedType` override exists for it: a `?` always re-wraps).
    //
    // The C# `AstType` reference return (a GC-owned node) ports to a raw `new`-ed
    // pointer (the D223 non-owning leak model, the `Identifier::Create` factory
    // precedent): the caller attaches the returned node to the tree via a slot setter
    // (which re-parents but does not take ownership). Non-const: the `ComposedType`
    // overrides mutate `this` in place (`PointerRank++`, `HasRefSpecifier = true`, the
    // `ArraySpecifiers` insert), faithfully mirroring the C# instance methods.
    //
    // The bodies are defined out-of-line in `AstType.cpp` (which includes
    // `ComposedType.hpp`): this header is included by `ComposedType.hpp` (the derivation),
    // so it cannot include it back.
    // -----------------------------------------------------------------------

    // The C# `public virtual AstType MakePointerType()` (AstType.cs line 75): creates a
    // pointer type by nesting this type in a fresh `ComposedType` and dispatching to the
    // (virtual) `ComposedType.MakePointerType`, which extends the fresh wrapper's
    // `PointerRank` to 1. On a `ComposedType` receiver without array specifiers the
    // override extends the existing pointer run IN PLACE (`T*` + `*` -> the same node,
    // `PointerRank` 2); with array specifiers the override falls back to this base
    // (`T[]` + `*` -> a fresh wrapper around the array type).
    virtual AstType* MakePointerType();

    // The C# `public virtual AstType MakeArrayType(int rank = 1)` (AstType.cs line 85):
    // creates an array type by nesting this type in a fresh `ComposedType` and
    // dispatching to the (virtual) `ComposedType.MakeArrayType`, which inserts one rank
    // specifier of the given rank. On a `ComposedType` receiver the override inserts the
    // new specifier BEFORE the first existing one, so `T[].MakeArrayType(2)` renders
    // `T[,][]` (the new rank is the innermost). The default rank is 1 (the C# `int rank
    // = 1` default argument, declared here on the base only -- a default on the override
    // would re-define it).
    virtual AstType* MakeArrayType(int rank = 1);

    // The C# `public AstType MakeNullableType()` (AstType.cs line 93): creates a
    // nullable type by nesting this type in a fresh `ComposedType` with
    // `HasNullableSpecifier = true`. NON-virtual in the C# (no `ComposedType` override
    // exists): a `?` always re-wraps, never extends in place -- `T*` + `?` is a fresh
    // outer node around the pointer node.
    AstType* MakeNullableType();

    // The C# `public virtual AstType MakeRefType()` (AstType.cs line 101): creates a C# 7
    // ref type by nesting this type in a fresh `ComposedType` with `HasRefSpecifier =
    // true`. On a `ComposedType` receiver the override sets the flag IN PLACE and
    // returns the same node.
    virtual AstType* MakeRefType();

    // The C# `public static AstType Create(string dottedName)` (AstType.cs line 131): creates
    // a simple `AstType` from a dotted name -- a `SimpleType` head with each further part
    // wrapping the chain in a `MemberType` (`A.B.C` -> `MemberType(MemberType(SimpleType(A),
    // B), C)`). Does NOT support generics, arrays, etc. -- just simple dotted names (e.g.
    // namespace names). The C# `string.Split('.')` semantics: an empty string yields the
    // single empty part (`SimpleType("")`); a trailing dot yields a trailing empty part.
    // Like the `Make*` builders, the body is defined out-of-line in `AstType.cpp` (which
    // includes `SimpleType.hpp`/`MemberType.hpp`; this header cannot include them back --
    // both derive `AstType`), and the returned node follows the D223 non-owning leak model
    // (a raw `new`-ed pointer the caller attaches through a slot setter).
    static AstType* Create(const std::string& dottedName);
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_ASTTYPE_HPP
