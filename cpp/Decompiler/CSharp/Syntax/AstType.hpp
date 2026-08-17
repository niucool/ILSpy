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
// unreachable through the type hierarchy). The convenience builders/queries are deferred:
// `IsVar`/`GetNameLookupMode` reference not-yet-ported nodes (`SimpleType`,
// `UsingDeclaration`/`UsingAliasDeclaration`/`TypeDeclaration`/`Constraint`) and the
// `NameLookupMode` enum; `MakePointerType`/`MakeArrayType`/`MakeNullableType`/`MakeRefType`/
// `MemberType`/`Create` construct `ComposedType`/`MemberType`/`SimpleType` (not yet ported).
// They are consumed only by the resolver/output stage (`TypeSystemAstBuilder` and friends, the
// unported section 5.3 long pole), so they land with that stage -- the D229-D235
// behavior-consumed-by-the-unported-stage deferral.

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
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_ASTTYPE_HPP
