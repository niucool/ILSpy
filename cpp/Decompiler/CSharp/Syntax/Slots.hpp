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

// Port of the `Slots` holder in ICSharpCode.Decompiler/CSharp/Syntax (the generated
// `Slots.g.cs`, emitted by DecompilerSyntaxTreeGenerator.cs). `Slots` is the shared set of
// canonical slot-kind constants: one typed `CSharpSlotInfo<T>` per distinct child position
// across all AST node types. Each constant is its own canonical kind (constructed with a
// null `Kind`); a node's per-node slot points back at it, and consumers compare
// `node.Slot.Kind == Slots.X` by object identity to identify a node's position
// polymorphically (replacing the old `node.Role == Roles.X` comparisons).
//
// The C# `public static class Slots` ports as a namespace (`ILSpy::...::Syntax::Slots`)
// holding `inline` variables: C++17 `inline` variables at namespace scope have external
// linkage and one address across translation units, so the pointer-identity comparison the
// slot system relies on is preserved. Each constant is `CSharpSlotInfoT<T>` (the C#
// `CSharpSlotInfo<T>`) whose ctor captures a `dynamic_cast<const T*>` is-a test, so the
// concrete child type must be complete where this header is included -- hence the
// `Expression.hpp` include (the type of the `Left`/`Right` positions ported so far).
//
// This header grows monotonically as node types land: each new slot kind (a `[Slot]` name
// not yet seen) adds one `inline` constant here. The kind carries identity and the precise
// child type only; the per-position `IsCollection`/`IsOptional` flags live on the per-node
// slots (the C# generator resolves the collection-vs-single ambiguity per position, so the
// shared constant's flags are not authoritative).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_SLOTS_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_SLOTS_HPP

#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax::Slots {

// The `Left` operand position (a single `Expression` child). Shared by the binary/assignment
// expressions (`BinaryOperatorExpression`, `AssignmentExpression`, ...) and any other node
// whose left operand is a single `Expression`.
inline const CSharpSlotInfoT<Expression> Left{"Left", false, nullptr, false};

// The `Right` operand position (a single `Expression` child). Shared by the binary/assignment
// expressions.
inline const CSharpSlotInfoT<Expression> Right{"Right", false, nullptr, false};

// The `Expression` operand position (a single `Expression` child). Used by the unary
// expressions (`UnaryOperatorExpression`, ...). The name collides with the `Expression`
// class in the parent `Syntax` namespace; the template argument in this definition resolves
// to the class (the constant being declared is not yet in scope at the point its type is
// parsed), and the unqualified `Expression` in any *later* `Slots` entry that wants the
// class as its element type must be qualified (`::ILSpy::Decompiler::CSharp::Syntax::Expression`)
// to avoid resolving to this constant.
inline const CSharpSlotInfoT<Expression> Expression{"Expression", false, nullptr, false};

// The `Condition` operand position (a single `Expression` child). Shared by
// `ConditionalExpression` and several statement nodes (`IfElseStatement`/`WhileStatement`/
// `DoWhileStatement`/`ForStatement`/`TryCatchStatement`/`QueryExpression`, all
// `Expression`-typed -- `TryCatchStatement`'s is nullable but the kind is the same). Defined
// AFTER `Expression`, so the element type is qualified to avoid resolving to the `Expression`
// *variable* declared above (the D231 collision note).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> Condition{"Condition", false, nullptr, false};

// The `True` operand position (a single `Expression` child). Unique to `ConditionalExpression`
// (the `TrueExpression` arm). The kind name is `True` (the `[Slot("True")]` argument); the
// per-node slot static is `TrueExpressionSlot` (named after the property). `True`/`False` are
// not C++ keywords (only lowercase `true`/`false` are), so the PascalCase identifiers are
// safe. Defined after `Expression`, so the element type is qualified.
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> True{"True", false, nullptr, false};

// The `False` operand position (a single `Expression` child). Unique to `ConditionalExpression`
// (the `FalseExpression` arm). The kind name is `False`; the per-node slot static is
// `FalseExpressionSlot`. Defined after `Expression`, so the element type is qualified.
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> False{"False", false, nullptr, false};

// The `Identifier` kind -- the backing `Identifier` token of a string-name `[Slot]` (e.g.
// `SimpleType.Identifier`, `IdentifierExpression.Identifier`, `MemberReferenceExpression`.
// `MemberName`). A string `[Slot]` is a convenience string accessor over a generated
// `Identifier` token child slot; the kind identifies that token position across all nodes
// that declare a string-name `[Slot]`. The name collides with the `Identifier` class in the
// parent `Syntax` namespace (the `Expression` precedent): the template argument in this
// definition resolves to the class (the constant being declared is not yet in scope), and a
// later `Slots` entry wanting the `Identifier` class as its element type must qualify it
// (`::ILSpy::Decompiler::CSharp::Syntax::Identifier`) to avoid resolving to this constant.
inline const CSharpSlotInfoT<Identifier> Identifier{"Identifier", false, nullptr, false};

// The `Target` kind -- a single `AstType` child (the target of a `MemberType`, e.g.
// `List<int>.Enumerator` -> the `MemberType` whose `Target` is the `SimpleType` `List<int>`
// and whose `MemberName` is `Enumerator`). Unique to `MemberType` among the ported nodes
// (the `Expression`-typed target positions of `MemberReferenceExpression`/
// `InvocationExpression`/`IndexerExpression`/`PointerReferenceExpression` use the
// `TargetExpression` kind, not `Target`). A `CSharpSlotInfoT<AstType>` (the element type is
// `AstType`); defined after `Identifier`/`TypeArgument`, but no `Slots` variable is named
// `AstType`, so the unqualified `AstType` resolves to the class (no elaborated specifier
// needed, unlike the `Expression`/`Identifier` constants whose names collide with their
// element-type classes).
inline const CSharpSlotInfoT<AstType> Target{"Target", false, nullptr, false};

// The `TypeArgument` kind -- a collection of `AstType` (the type arguments of a generic
// type reference, e.g. `SimpleType.TypeArguments`/`MemberType.TypeArguments`/
// `IdentifierExpression.TypeArguments`/`MemberReferenceExpression.TypeArguments`). A
// `CSharpSlotInfoT<AstType>` (the element type is `AstType`); the per-position
// `IsCollection`/`IsOptional` flags live on the per-node slot (the shared kind carries
// identity only, so the kind is constructed non-collection/non-optional).
inline const CSharpSlotInfoT<AstType> TypeArgument{"TypeArgument", false, nullptr, false};

} // namespace ILSpy::Decompiler::CSharp::Syntax::Slots

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_SLOTS_HPP
