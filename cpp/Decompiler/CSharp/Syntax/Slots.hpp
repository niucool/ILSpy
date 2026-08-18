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
#include "Decompiler/CSharp/Syntax/ArraySpecifier.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/VariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"

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

// The `TargetExpression` kind -- a single `Expression` child (the target of a member access,
// invocation, indexer, or pointer reference: `MemberReferenceExpression.Target`,
// `InvocationExpression.Target`, `IndexerExpression.Target`,
// `PointerReferenceExpression.Target`). Distinct from the `Target` kind (an `AstType` target,
// unique to `MemberType`): a member access `expr.Member` has an `Expression`-typed target
// (`expr`), so its slot kind is `TargetExpression`, not `Target`. A
// `CSharpSlotInfoT<Expression>` (the element type is `Expression`); defined after the
// `Expression` constant, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to the `Expression`
// *variable* declared above (the D231 collision note).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> TargetExpression{"TargetExpression", false, nullptr, false};

// The `TypeArgument` kind -- a collection of `AstType` (the type arguments of a generic
// type reference, e.g. `SimpleType.TypeArguments`/`MemberType.TypeArguments`/
// `IdentifierExpression.TypeArguments`/`MemberReferenceExpression.TypeArguments`). A
// `CSharpSlotInfoT<AstType>` (the element type is `AstType`); the per-position
// `IsCollection`/`IsOptional` flags live on the per-node slot (the shared kind carries
// identity only, so the kind is constructed non-collection/non-optional).
inline const CSharpSlotInfoT<AstType> TypeArgument{"TypeArgument", false, nullptr, false};

// The `Type` kind -- a single `AstType` child (the type reference of a node that takes a type:
// `Attribute.Type`, `CastExpression.Type`, `AsExpression.Type`, `IsExpression.Type`,
// `TypeOfExpression.Type`, `TypeReferenceExpression.Type`, `DefaultValueExpression.Type`,
// `SizeOfExpression.Type`, `ComposedType.BaseType`, ...). Shared by many nodes (every
// `[Slot("Type")] AstType` declaration collapses to this one kind). A
// `CSharpSlotInfoT<AstType>` (the element type is `AstType`); no `Slots` variable is named
// `AstType`, so the unqualified `AstType` resolves to the class (no elaborated specifier
// needed). The name `Type` does not collide with any class in the `Syntax` namespace (there is
// `AstType`, not `Type`), unlike the `Expression`/`Identifier` constants.
inline const CSharpSlotInfoT<AstType> Type{"Type", false, nullptr, false};

// The `Argument` kind -- a collection of `Expression` (the argument list of a node that takes
// arguments: `Attribute.Arguments`, `InvocationExpression.Arguments`, `IndexerExpression.
// Arguments`, `ObjectCreateExpression.Arguments`, `ArrayCreateExpression.Arguments`). Shared
// by every `[Slot("Argument")] AstNodeCollection<Expression>` declaration. A
// `CSharpSlotInfoT<Expression>` (the element type is `Expression`); defined after the
// `Expression` constant, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to the `Expression`
// *variable* declared above (the D231 collision note).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> Argument{"Argument", false, nullptr, false};

// The `ArraySpecifier` kind -- a collection of `ArraySpecifier` (the rank specifiers of an
// array type: `ComposedType.ArraySpecifiers`, an `AstNodeCollection<ArraySpecifier>`). Unique
// to `ComposedType` among the ported nodes. A `CSharpSlotInfoT<ArraySpecifier>` (the element
// type is the concrete `ArraySpecifier` node, complete via the `ArraySpecifier.hpp` include
// above). `ArraySpecifier.hpp` does NOT include `Slots.hpp` (it is a leaf with no `[Slot]`
// children, so it has no per-node slot statics), so this kind lives HERE in `Slots.hpp` (no
// include cycle) -- unlike `Slots::Attribute`/`Slots::AttributeSection`, whose element-type
// node headers DO include `Slots.hpp` and so are cycle-broken into their own headers.
inline const CSharpSlotInfoT<ArraySpecifier> ArraySpecifier{"ArraySpecifier", false, nullptr, false};

// The `AdditionalArraySpecifier` kind -- a collection of `ArraySpecifier` (the ADDITIONAL
// rank specifiers of an `ArrayCreateExpression`: the trailing `[...]`s WITHOUT size info, e.g.
// the `[]` in `new int[5][]`). `ArrayCreateExpression.AdditionalArraySpecifiers` is an
// `AstNodeCollection<ArraySpecifier>` whose `[Slot("AdditionalArraySpecifier")]` argument names
// a slot kind DISTINCT from `ComposedType.ArraySpecifiers`'s `[Slot("ArraySpecifier")]` (the
// two collections have the same `ArraySpecifier` element type but different kind names, so the
// slot system can route by kind -- `GetCollectionByKind(&Slots::ArraySpecifier)` returns
// `ComposedType.ArraySpecifiers`, `GetCollectionByKind(&Slots::AdditionalArraySpecifier)`
// returns `ArrayCreateExpression.AdditionalArraySpecifiers`). A
// `CSharpSlotInfoT<ArraySpecifier>` (same element type as `Slots::ArraySpecifier`); the
// `ArraySpecifier.hpp` include above makes the element type complete (no cycle, same as
// `Slots::ArraySpecifier`). Defined AFTER the `Slots::ArraySpecifier` *variable*, so the
// element type is qualified (`::ILSpy::Decompiler::CSharp::Syntax::ArraySpecifier`) to avoid
// resolving to that variable (the `Expression`/`Condition`/`Argument` collision precedent: an
// unqualified name shared with a prior `Slots` variable resolves to the variable, not the
// class, since `Slots` is a namespace and the variable is in scope).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::ArraySpecifier> AdditionalArraySpecifier{"AdditionalArraySpecifier", false, nullptr, false};

// The `Statement` kind -- a collection of `Statement` (the statement list of a block:
// `BlockStatement.Statements`, an `AstNodeCollection<Statement>`). The first ported
// collection whose element type is the `Statement` abstract base (the collection-only
// `BlockStatement` D256 shape). A `CSharpSlotInfoT<Statement>` (the element type is the
// `Statement` abstract base, complete via the `Statements/Statement.hpp` include above).
// `Statements/Statement.hpp` does NOT include `Slots.hpp` (the `Statement` abstract base has
// no per-node slot statics), so this kind lives HERE in `Slots.hpp` (no include cycle) -- the
// `Slots.ArraySpecifier` D242 precedent (an element-type node header that does not include
// `Slots.hpp` lives in `Slots.hpp`), applied to an abstract-base element type. The name
// `Statement` collides with the `Statement` CLASS in the parent `Syntax` namespace (the
// `Expression`/`Identifier` D231 collision pattern): the template argument in this definition
// resolves to the class (the constant being declared is not yet in scope at the point its
// type is parsed), and a LATER `Slots` entry wanting the `Statement` class as its element
// type must qualify it (`::ILSpy::Decompiler::CSharp::Syntax::Statement`) to avoid resolving
// to this constant.
inline const CSharpSlotInfoT<Statement> Statement{"Statement", false, nullptr, false};

// The `TrueStatement` kind -- a single `Statement` child (the `then` branch of an
// `IfElseStatement.TrueStatement`). Unique to `IfElseStatement` among the ported nodes (the
// `ConditionalExpression` arm uses the `True` `Expression` kind, not this). A
// `CSharpSlotInfoT<Statement>` (the element type is the `Statement` abstract base, complete via
// the `Statements/Statement.hpp` include above). `Statements/Statement.hpp` does NOT include
// `Slots.hpp` (the abstract base has no per-node slot statics), so this kind lives HERE in
// `Slots.hpp` (no include cycle) -- the `Slots.Statement` D256 precedent. Defined AFTER the
// `Slots::Statement` *variable*, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Statement`) to avoid resolving to that variable (the
// `Expression`/`Condition`/`AdditionalArraySpecifier` collision precedent: an unqualified name
// shared with a prior `Slots` variable resolves to the variable, not the class).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Statement> TrueStatement{"TrueStatement", false, nullptr, false};

// The `FalseStatement` kind -- a single NULLABLE `Statement` child (the `else` branch of an
// `IfElseStatement.FalseStatement`, absent for a bare `if` without `else`). Unique to
// `IfElseStatement` among the ported nodes (the `ConditionalExpression` arm uses the `False`
// `Expression` kind, not this). A `CSharpSlotInfoT<Statement>` (the element type is the
// `Statement` abstract base, complete via the `Statements/Statement.hpp` include above); the
// per-position `IsOptional` flag lives on the per-node slot (the shared kind carries identity
// only, constructed non-optional). `Statements/Statement.hpp` does NOT include `Slots.hpp`, so
// this kind lives HERE (no include cycle) -- the `Slots.Statement`/`Slots.TrueStatement`
// precedent. Defined AFTER the `Slots::Statement` variable, so the element type is qualified.
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Statement> FalseStatement{"FalseStatement", false, nullptr, false};

// The `EmbeddedStatement` kind -- a single `Statement` child (the loop body of a
// `WhileStatement`/`DoWhileStatement`/`ForStatement`/`ForeachStatement`/... -- the single
// statement a loop/using/lock/fixed/... wraps). Shared by every
// `[Slot("EmbeddedStatement")] Statement` declaration. A `CSharpSlotInfoT<Statement>` (the
// element type is the `Statement` abstract base, complete via the `Statements/Statement.hpp`
// include above). `Statements/Statement.hpp` does NOT include `Slots.hpp`, so this kind lives
// HERE (no include cycle) -- the `Slots.Statement` precedent. Defined AFTER the
// `Slots::Statement` variable, so the element type is qualified.
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Statement> EmbeddedStatement{"EmbeddedStatement", false, nullptr, false};

// The `ForInitializer` kind -- a collection of `Statement` (the initializer list of a
// `ForStatement`: the comma-separated init statements before the first `;` of a
// `for (init; test; iter) body`, e.g. `a = 2, b = 1` in `for (a = 2, b = 1; a > b; a--)`).
// Unique to `ForStatement` among the ported nodes (the `Initializers` collection). A
// `CSharpSlotInfoT<Statement>` (the element type is the `Statement` abstract base, complete via
// the `Statements/Statement.hpp` include above). `Statements/Statement.hpp` does NOT include
// `Slots.hpp` (the abstract base has no per-node slot statics), so this kind lives HERE in
// `Slots.hpp` (no include cycle) -- the `Slots.Statement`/`Slots.EmbeddedStatement` precedent.
// Defined AFTER the `Slots::Statement` variable, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Statement`) to avoid resolving to that variable (the
// `Expression`/`Condition`/`AdditionalArraySpecifier` collision precedent: an unqualified name
// shared with a prior `Slots` variable resolves to the variable, not the class).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Statement> ForInitializer{"ForInitializer", false, nullptr, false};

// The `Iterator` kind -- a collection of `Statement` (the iterator/step list of a
// `ForStatement`: the comma-separated step statements after the second `;` of a
// `for (init; test; iter) body`, e.g. `a--` in `for (;; ; a--)`). Unique to `ForStatement`
// among the ported nodes (the `Iterators` collection); the kind name `Iterator` is DISTINCT from
// the per-node `Iterators` property name and from the `ForInitializer` kind (the two
// collections have the same `Statement` element type but different kind names, so the slot system
// can route by kind). A `CSharpSlotInfoT<Statement>` (the element type is the `Statement`
// abstract base, complete via the `Statements/Statement.hpp` include above).
// `Statements/Statement.hpp` does NOT include `Slots.hpp`, so this kind lives HERE (no include
// cycle) -- the `Slots.Statement`/`Slots.ForInitializer` precedent. Defined AFTER the
// `Slots::Statement` variable, so the element type is qualified.
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Statement> Iterator{"Iterator", false, nullptr, false};

// The `ResourceAcquisition` kind -- a single `AstNode` child (the resource acquisition of a
// `UsingStatement` -- the `( local_variable_declaration | expression )` inside `using (...)`, so
// the slot is typed the abstract `AstNode` base, NOT a specific derived class: it can hold either a
// `VariableDeclarationStatement` (the local-variable-declaration form) or an `Expression` (the
// expression form), both of which derive from `AstNode`). Unique to `UsingStatement` among the
// ported nodes. A `CSharpSlotInfoT<AstNode>` (the element type is the `AstNode` abstract base);
// `AstNode.hpp` does NOT include `Slots.hpp` (the `AstNode` abstract base has no per-node slot
// statics -- the `Slots.Statement`/`Slots.TrueStatement` precedent applied to the root base), and
// `AstNode` is complete where this header is included (it is the base of every type `Slots.hpp`
// already pulls in -- `Expression`/`AstType`/`Statement`/...), so this kind lives HERE in
// `Slots.hpp` (no include cycle). No `Slots` variable is named `AstNode`, and no class named
// `ResourceAcquisition` lives in the `Syntax` namespace, so no elaborated-type-specifier is
// needed (no name collision in either direction).
inline const CSharpSlotInfoT<AstNode> ResourceAcquisition{"ResourceAcquisition", false, nullptr, false};

// The `LambdaBody` kind -- a single `AstNode` child (the body of a `LambdaExpression` --
// the `=> block | expression` after the parameter list; typed the abstract `AstNode` base
// because the production takes EITHER a `BlockStatement` (the block form) OR an `Expression`
// (the expression form); both derive from `AstNode`, so the slot accepts either). Unique to
// `LambdaExpression` among the ported nodes. A `CSharpSlotInfoT<AstNode>` (the element type is
// the `AstNode` abstract root base); `AstNode.hpp` does NOT include `Slots.hpp` (the root base
// has no per-node slot statics -- the `Slots.ResourceAcquisition` D262 `AstNode`-typed-kind
// precedent), and `AstNode` is complete where this header is included, so this kind lives
// HERE in `Slots.hpp` (no include cycle). No `Slots` variable is named `AstNode`, and no class
// named `LambdaBody` lives in the `Syntax` namespace, so no elaborated-type-specifier is
// needed (no name collision in either direction). The shared constant is constructed
// non-collection/non-optional; the per-node `BodySlot` on `LambdaExpression` carries
// `IsOptional=false` (the `[Slot("LambdaBody")]` is a required single slot -- the C# `AstNode
// Body` is non-nullable, so the slot is required, unlike `UsingStatement`'s nullable-typed but
// required `ResourceAcquisition`).
inline const CSharpSlotInfoT<AstNode> LambdaBody{"LambdaBody", false, nullptr, false};

// The `VariableDesignation` kind -- a collection of `VariableDesignation` (the nested-designation
// list of a `ParenthesizedVariableDesignation`: `ParenthesizedVariableDesignation.VariableDesignations`,
// an `AstNodeCollection<VariableDesignation>`). Unique to `ParenthesizedVariableDesignation` among
// the ported nodes. A `CSharpSlotInfoT<VariableDesignation>` (the element type is the
// `VariableDesignation` abstract base, complete via the `VariableDesignation.hpp` include above).
// `VariableDesignation.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node slot
// statics -- the `Slots.Statement`/`Slots.ArraySpecifier` precedent applied to the designation
// base), so this kind lives HERE in `Slots.hpp` (no include cycle). The name `VariableDesignation`
// collides with the `VariableDesignation` CLASS in the parent `Syntax` namespace (the
// `Expression`/`Identifier`/`Statement` collision pattern): the template argument in this
// definition resolves to the class (the constant being declared is not yet in scope at the point
// its type is parsed), and a LATER `Slots` entry wanting the `VariableDesignation` class as its
// element type must qualify it (`::ILSpy::Decompiler::CSharp::Syntax::VariableDesignation`) to
// avoid resolving to this constant.
inline const CSharpSlotInfoT<VariableDesignation> VariableDesignation{"VariableDesignation", false, nullptr, false};

// The `EnumMemberInitializer` kind -- a single NULLABLE `Expression` child (the optional
// `= expression` initializer of an `EnumMemberDeclaration`, e.g. the `1` in `enum E { A = 1 }`).
// Unique to `EnumMemberDeclaration` among the ported nodes. A `CSharpSlotInfoT<Expression>`
// (the element type is the `Expression` abstract base, complete via the `Expression.hpp` include
// above). `Expression.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node slot
// statics -- the `Slots.Statement`/`Slots.ArraySpecifier` precedent applied to the expression
// base), so this kind lives HERE in `Slots.hpp` (no include cycle). Defined AFTER the
// `Slots::Expression` *variable*, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to that variable (the
// `Expression`/`Condition`/`TargetExpression`/`Argument` collision precedent: an unqualified name
// shared with a prior `Slots` variable resolves to the variable, not the class, since `Slots` is a
// namespace and the variable is in scope).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> EnumMemberInitializer{"EnumMemberInitializer", false, nullptr, false};

// The `PrivateImplementationType` kind -- a single NULLABLE `AstType` child (the explicit-interface
// implementation type of a `PropertyDeclaration.PrivateImplementationType`, e.g. the `I` in
// `int I.P { get; set; }`; null when the property is not an explicit interface implementation).
// Unique to `PropertyDeclaration` among the ported nodes. A `CSharpSlotInfoT<AstType>` (the
// element type is the `AstType` abstract base, complete via the `AstType.hpp` include above).
// `AstType.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node slot statics --
// the `Slots.Statement`/`Slots.ArraySpecifier` precedent), so this kind lives HERE in `Slots.hpp`
// (no include cycle). No `Slots` variable is named `AstType`, and no class named
// `PrivateImplementationType` lives in the `Syntax` namespace, so no elaborated-type-specifier is
// needed (no name collision in either direction). The shared constant is constructed
// non-collection/non-optional; the per-node `PrivateImplementationTypeSlot` on `PropertyDeclaration`
// carries the `IsOptional=true` flag (the nullable slot -- the shared kind is constructed
// non-optional, the per-node slot carries the optionality, the `IfElseStatement.FalseStatement`
// D258 precedent).
inline const CSharpSlotInfoT<AstType> PrivateImplementationType{"PrivateImplementationType", false, nullptr, false};

// The `ExpressionBody` kind -- a single NULLABLE `Expression` child (the expression body of an
// expression-bodied `PropertyDeclaration`/`IndexerDeclaration`, e.g. the `=> expr` of
// `int P => 5`; absent for a classic `{ get; set; }` property). Shared by every
// `[Slot("ExpressionBody")] Expression?` declaration. A `CSharpSlotInfoT<Expression>` (the
// element type is the `Expression` abstract base, complete via the `Expression.hpp` include
// above). `Expression.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node slot
// statics -- the `Slots.Statement`/`Slots.ArraySpecifier` precedent), so this kind lives HERE in
// `Slots.hpp` (no include cycle). Defined AFTER the `Slots::Expression` *variable*, so the element
// type is qualified (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to that
// variable (the `Expression`/`Condition`/`TargetExpression`/`Argument`/`EnumMemberInitializer`
// collision precedent: an unqualified name shared with a prior `Slots` variable resolves to the
// variable, not the class, since `Slots` is a namespace and the variable is in scope). The kind
// name `ExpressionBody` is DISTINCT from the `Expression` variable, so the constant's own name
// does not collide. The shared constant is constructed non-collection/non-optional; the per-node
// `ExpressionBodySlot` on `PropertyDeclaration` carries the `IsOptional=true` flag (the nullable
// slot -- the shared kind is constructed non-optional, the per-node slot carries the optionality,
// the `IfElseStatement.FalseStatement` D258 precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> ExpressionBody{"ExpressionBody", false, nullptr, false};

// The `BaseType` kind -- a collection of `AstType` (the base-type constraint list of a
// `Constraint`: `Constraint.BaseTypes`, an `AstNodeCollection<AstType>` -- the `: Base1, Base2,
// ...` list of a `where T : ...` clause; `new()`/`struct`/`class` constraints are `PrimitiveType`s,
// all `AstType`-derived). Unique to `Constraint` among the ported nodes. A `CSharpSlotInfoT<AstType>`
// (the element type is the `AstType` abstract base, complete via the `AstType.hpp` include above).
// `AstType.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node slot statics -- the
// `Slots.Type`/`Slots.TypeArgument`/`Slots.PrivateImplementationType` precedent), so this kind
// lives HERE in `Slots.hpp` (no include cycle). No `Slots` variable is named `AstType`, and no
// class named `BaseType` lives in the `Syntax` namespace, so the unqualified `AstType` resolves to
// the class (no elaborated specifier needed). The shared constant is constructed
// non-collection/non-optional; the per-node `BaseTypesSlot` on `Constraint` carries the
// `IsCollection` flag (the collection `[Slot]` makes the per-node slot a collection, the
// `Slots.TypeArgument` precedent).
inline const CSharpSlotInfoT<AstType> BaseType{"BaseType", false, nullptr, false};

// The `Import` kind -- a single `AstType` (the imported type of a using directive:
// `UsingDeclaration.Import` and `UsingAliasDeclaration.Import`, both `[Slot("Import")] AstType`).
// A `CSharpSlotInfoT<AstType>` (the element type is the `AstType` abstract base, complete via the
// `AstType.hpp` include above). `AstType.hpp` does NOT include `Slots.hpp` (the abstract base has
// no per-node slot statics -- the `Slots.Type`/`Slots.Target`/`Slots.BaseType` precedent), so this
// kind lives HERE in `Slots.hpp` (no include cycle). No `Slots` variable is named `AstType`, and no
// class named `Import` lives in the `Syntax` namespace, so the unqualified `AstType` resolves to
// the class (no elaborated specifier needed). The shared constant is constructed
// non-collection/non-optional; the per-node `ImportSlot` on the owning node carries the
// `IsOptional` flag (the `[Slot("Import")]` is a required single slot, so `IsOptional=false`).
inline const CSharpSlotInfoT<AstType> Import{"Import", false, nullptr, false};

// The `Alias` kind -- the backing `Identifier` token of a `[Slot("Alias")] string` string-name
// slot (`UsingAliasDeclaration.Alias`, a non-nullable `string` over a backing `AliasToken`
// `Identifier`). A `CSharpSlotInfoT<Identifier>` (the element type is the `Identifier` token
// class, complete via the `Identifier.hpp` include above). `Identifier.hpp` does NOT include
// `Slots.hpp` (the `Identifier` token is a leaf with no per-node slot statics -- the
// `Slots.Identifier` precedent), so this kind lives HERE in `Slots.hpp` (no include cycle). The
// element type MUST be qualified (`::ILSpy::Decompiler::CSharp::Syntax::Identifier`) because the
// `Slots::Identifier` VARIABLE (a non-type, declared above at the same namespace scope) shadows the
// unqualified `Identifier` name -- a NEW `Slots` constant defined after a same-named prior `Slots`
// variable must qualify its element type, even when the prior variable's element type IS that
// class (the `Slots.AdditionalArraySpecifier` D252 precedent applied to the `Identifier` token
// type). The kind name `Alias` collides with no class in the `Syntax` namespace, so no
// elaborated-type-specifier is needed beyond the element-type qualification. The shared constant
// is constructed non-collection/non-optional; the per-node `AliasTokenSlot` on `UsingAliasDeclaration`
// carries the `IsOptional` flag (the `[Slot("Alias")]` is a required single slot, so
// `IsOptional=false` -- the `LabelStatement.LabelTokenSlot` D259 / `VariableInitializer.NameTokenSlot`
// D266 non-nullable-string-name-`[Slot]` precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Identifier> Alias{"Alias", false, nullptr, false};

// The `NamespaceName` kind -- a single `AstType` child (the dotted name of a `NamespaceDeclaration`,
// e.g. `Foo.Bar` -> a `MemberType` whose `Target` is `SimpleType("Foo")` and whose `MemberName` is
// `"Bar"`). Unique to `NamespaceDeclaration` among the ported nodes. A `CSharpSlotInfoT<AstType>`
// (the element type is the `AstType` abstract base, complete via the `AstType.hpp` include above).
// `AstType.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node slot statics -- the
// `Slots.Type`/`Slots.Target`/`Slots.BaseType`/`Slots.Import` precedent), so this kind lives HERE in
// `Slots.hpp` (no include cycle). No `Slots` variable is named `AstType`, and no class named
// `NamespaceName` lives in the `Syntax` namespace (there is `NamespaceDeclaration`, not
// `NamespaceName`), so the unqualified `AstType` resolves to the class and no elaborated specifier is
// needed. The shared constant is constructed non-collection/non-optional; the per-node
// `NamespaceNameSlot` on `NamespaceDeclaration` carries the `IsOptional` flag (the
// `[Slot("NamespaceName")]` is a required single slot, so `IsOptional=false`).
inline const CSharpSlotInfoT<AstType> NamespaceName{"NamespaceName", false, nullptr, false};

// The `Member` kind -- a collection of `AstNode` (the namespace-body members of a
// `NamespaceDeclaration.Members`, an `AstNodeCollection<AstNode>` -- the `{ namespace_member* }` of
// a block-scoped namespace or the top-level members of a file-scoped namespace). Unique to
// `NamespaceDeclaration` among the ported nodes. A `CSharpSlotInfoT<AstNode>` (the element type is
// the `AstNode` abstract root base, complete where this header is included -- it is the base of
// every type `Slots.hpp` already pulls in -- the `Slots.ResourceAcquisition` precedent applied to a
// COLLECTION). `AstNode.hpp` does NOT include `Slots.hpp` (the root base has no per-node slot
// statics -- the `Slots.ResourceAcquisition` D262 `AstNode`-typed-kind precedent), so this kind
// lives HERE in `Slots.hpp` (no include cycle) -- the FIRST `AstNode`-typed COLLECTION kind
// (`Slots.ResourceAcquisition` was a single slot). No `Slots` variable is named `AstNode`, and no
// class named `Member` lives in the `Syntax` namespace (there is `MemberType`/`MemberReferenceExpression`,
// not `Member` or `Members`), so the unqualified `AstNode` resolves to the class and no elaborated
// specifier is needed. The shared constant is constructed non-collection/non-optional; the
// per-node `MembersSlot` on `NamespaceDeclaration` carries the `IsCollection` flag (the
// `[Slot("Member")]` is a collection slot, the `Slots.TypeArgument`/`Slots.Statement` precedent).
inline const CSharpSlotInfoT<AstNode> Member{"Member", false, nullptr, false};

// The `Warning` kind -- a collection of `PrimitiveExpression` (the `PragmaWarningPreprocessorDirective.
// Warnings`, an `AstNodeCollection<PrimitiveExpression>` -- the `expression*` of a `#pragma warning`
// directive's disable/restore list, C# lexical grammar). Unique to `PragmaWarningPreprocessorDirective`
// among the ported nodes. A `CSharpSlotInfoT<PrimitiveExpression>` (the element type is the concrete
// `PrimitiveExpression` leaf, complete where this header is included via the include added for it --
// the `Slots.Argument`/`Slots.ArraySpecifier` precedent applied to a `PrimitiveExpression`-typed
// collection kind). `PrimitiveExpression.hpp` does NOT include `Slots.hpp` (a leaf with no per-node slot
// statics), so this kind lives HERE in `Slots.hpp` (no include cycle) -- the `Slots.ArraySpecifier`
// D242 leaf-element-type precedent. No `Slots` variable is named `PrimitiveExpression`, and no class
// named `Warning` lives in the `Syntax` namespace, so the unqualified `PrimitiveExpression` resolves
// to the class and no elaborated specifier is needed. The shared constant is constructed
// non-collection/non-optional; the per-node `WarningsSlot` on `PragmaWarningPreprocessorDirective`
// carries the `IsCollection` flag (the `[Slot("Warning")]` is a collection slot).
inline const CSharpSlotInfoT<PrimitiveExpression> Warning{"Warning", false, nullptr, false};

// The `DeclaringType` kind -- a single `AstType` child (the declaring type of a `DocumentationReference`
// `cref`, e.g. the `Foo` in `<see cref="Foo.Bar"/>`; null for a bare `member_name` or `type_name`
// `cref`). Unique to `DocumentationReference` among the ported nodes. A `CSharpSlotInfoT<AstType>`
// (the element type is the `AstType` abstract base, complete via the `AstType.hpp` include above).
// `AstType.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node slot statics -- the
// `Slots.Type`/`Slots.Target`/`Slots.BaseType`/`Slots.Import`/`Slots.NamespaceName` precedent), so this
// kind lives HERE in `Slots.hpp` (no include cycle). No `Slots` variable is named `AstType`, and no
// class named `DeclaringType` lives in the `Syntax` namespace, so the unqualified `AstType` resolves
// to the class and no elaborated specifier is needed. The shared constant is constructed
// non-collection/non-optional; the per-node `DeclaringTypeSlot` on `DocumentationReference` carries
// the `IsOptional` flag (the `[Slot("DeclaringType")]` is a nullable single slot, so
// `IsOptional=true`).
inline const CSharpSlotInfoT<AstType> DeclaringType{"DeclaringType", false, nullptr, false};

// The `ConversionOperatorReturnType` kind -- a single `AstType` child (the return type of a
// conversion operator `cref`, used only when `DocumentationReference.SymbolKind == Operator` and
// `OperatorType` is `Implicit` or `Explicit`). Unique to `DocumentationReference` among the ported
// nodes. A `CSharpSlotInfoT<AstType>` (the element type is the `AstType` abstract base, complete via
// the `AstType.hpp` include above). `AstType.hpp` does NOT include `Slots.hpp`, so this kind lives
// HERE in `Slots.hpp` (no include cycle). No `Slots` variable is named `AstType`, and no class named
// `ConversionOperatorReturnType` lives in the `Syntax` namespace, so the unqualified `AstType`
// resolves to the class and no elaborated specifier is needed. The shared constant is constructed
// non-collection/non-optional; the per-node `ConversionOperatorReturnTypeSlot` on
// `DocumentationReference` carries `IsOptional=false` (the `[Slot("ConversionOperatorReturnType")]`
// is a required single slot).
inline const CSharpSlotInfoT<AstType> ConversionOperatorReturnType{"ConversionOperatorReturnType", false, nullptr, false};

// The `Pattern` kind -- a single `Expression` child (the pattern of a `SwitchExpressionSection`:
// the `Expression` on the left of `=>` in a `switch` expression arm -- the C# AST models the
// pattern DSL as `Expression` nodes, so the `[Slot("Pattern")] Expression Pattern` is an
// `Expression`-typed slot whose KIND name is `Pattern`). Unique to `SwitchExpressionSection`
// among the ported nodes. A `CSharpSlotInfoT<Expression>` (the element type is the `Expression`
// abstract base, complete via the `Expression.hpp` include above). `Expression.hpp` does NOT
// include `Slots.hpp` (the abstract base has no per-node slot statics -- the
// `Slots.Expression`/`Slots.Condition`/`Slots.TargetExpression` precedent), so this kind lives HERE
// in `Slots.hpp` (no include cycle). Defined AFTER the `Slots::Expression` *variable*, so the
// element type is qualified (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving
// to that variable (the `Expression`/`Condition`/`TargetExpression`/`Argument`/
// `EnumMemberInitializer`/`ExpressionBody` collision precedent: an unqualified name shared with a
// prior `Slots` variable resolves to the variable, not the class). The kind name `Pattern` does
// NOT collide with any class in the `Syntax` namespace (the `PatternMatching::Pattern` is in the
// nested `PatternMatching` namespace, not found by unqualified lookup in `Syntax`, and no
// `Slots` variable is named `Pattern`), so no elaborated-type-specifier is needed for the kind
// name and the per-node `PatternSlot` on `SwitchExpressionSection` references `&Slots::Pattern`
// unambiguously. The shared constant is constructed non-collection/non-optional; the per-node
// `PatternSlot` carries `IsOptional=false` (the `[Slot("Pattern")]` is a required single slot).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> Pattern{"Pattern", false, nullptr, false};

// The `SwitchExpressionBody` kind -- a single `Expression` child (the body of a
// `SwitchExpressionSection`: the `Expression` on the right of `=>` in a `switch` expression arm).
// Unique to `SwitchExpressionSection` among the ported nodes. A `CSharpSlotInfoT<Expression>`
// (the element type is the `Expression` abstract base, complete via the `Expression.hpp` include
// above). `Expression.hpp` does NOT include `Slots.hpp`, so this kind lives HERE in `Slots.hpp`
// (no include cycle) -- the `Slots.Pattern`/`Slots.Expression` precedent. Defined AFTER the
// `Slots::Expression` *variable*, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to that variable (the
// `Expression`/`Condition`/`Pattern` collision precedent). The kind name `SwitchExpressionBody`
// collides with no class in the `Syntax` namespace (there is `SwitchExpression`/
// `SwitchExpressionSection`, not `SwitchExpressionBody`), and no `Slots` variable is named
// `SwitchExpressionBody`, so no elaborated-type-specifier is needed. The shared constant is
// constructed non-collection/non-optional; the per-node `BodySlot` on `SwitchExpressionSection`
// carries `IsOptional=false` (the `[Slot("SwitchExpressionBody")]` is a required single slot).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> SwitchExpressionBody{"SwitchExpressionBody", false, nullptr, false};

// The `SubPattern` kind -- a collection of `Expression` (the nested pattern list of a
// `RecursivePatternExpression.SubPatterns`, an `AstNodeCollection<Expression>` -- the `pattern*`
// inside the `{...}`/`(...)` of a recursive pattern; the C# AST models the pattern DSL as
// `Expression` nodes). Unique to `RecursivePatternExpression` among the ported nodes. A
// `CSharpSlotInfoT<Expression>` (the element type is the `Expression` abstract base, complete
// via the `Expression.hpp` include above). `Expression.hpp` does NOT include `Slots.hpp` (the
// abstract base has no per-node slot statics -- the `Slots.Expression`/`Slots.Argument`/
// `Slots.Pattern` precedent), so this kind lives HERE in `Slots.hpp` (no include cycle). Defined
// AFTER the `Slots::Expression` *variable*, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to that variable (the
// `Expression`/`Condition`/`TargetExpression`/`Argument`/`Pattern` collision precedent: an
// unqualified name shared with a prior `Slots` variable resolves to the variable, not the
// class, since `Slots` is a namespace and the variable is in scope). The kind name `SubPattern`
// collides with no class in the `Syntax` namespace, so no elaborated-type-specifier is needed
// for the kind name. The shared constant is constructed non-collection/non-optional; the
// per-node `SubPatternsSlot` on `RecursivePatternExpression` carries the `IsCollection` flag
// (the `[Slot("SubPattern")]` is a collection slot, the `Slots.TypeArgument`/`Slots.Argument`
// precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> SubPattern{"SubPattern", false, nullptr, false};

// The `Content` kind -- a collection of `InterpolatedStringContent` (the content list of an
// `InterpolatedStringExpression.Content`, an `AstNodeCollection<InterpolatedStringContent>` --
// the literal-text runs `InterpolatedStringText` and the expression arms `Interpolation`
// inside a `$"..."` interpolated string). Unique to `InterpolatedStringExpression` among the
// ported nodes. A `CSharpSlotInfoT<InterpolatedStringContent>` (the element type is the
// `InterpolatedStringContent` abstract base, complete via the `InterpolatedStringContent.hpp`
// include above). `InterpolatedStringContent.hpp` does NOT include `Slots.hpp` (the abstract
// base has no per-node slot statics -- the `Slots.Statement`/`Slots.ArraySpecifier` precedent),
// so this kind lives HERE in `Slots.hpp` (no include cycle). The kind name `Content` collides
// with no class in the `Syntax` namespace, so no elaborated-type-specifier is needed for the
// kind name. The shared constant is constructed non-collection/non-optional; the per-node
// `ContentSlot` on `InterpolatedStringExpression` carries the `IsCollection` flag (the
// `[Slot("Content")]` is a collection slot, the `Slots.TypeArgument`/`Slots.Argument`/
// `Slots.SubPattern` precedent).
inline const CSharpSlotInfoT<InterpolatedStringContent> Content{"Content", false, nullptr, false};

// The `Clause` collection position (a `QueryClause`-typed collection). Unique to
// `QueryExpression.Clauses` among the ported nodes. A `CSharpSlotInfoT<QueryClause>` (the
// element type is the `QueryClause` abstract base, complete via the `QueryClause.hpp` include
// above). `QueryClause.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node
// slot statics -- the `Slots.Statement`/`Slots.ArraySpecifier`/`Slots.Content` precedent), so
// this kind lives HERE in `Slots.hpp` (no include cycle). The kind name `Clause` collides with
// no class in the `Syntax` namespace, so no elaborated-type-specifier is needed for the kind
// name. The shared constant is constructed non-collection/non-optional; the per-node
// `ClausesSlot` on `QueryExpression` carries the `IsCollection` flag (the `[Slot("Clause")]`
// is a collection slot, the `Slots.Content`/`Slots.Ordering` precedent).
inline const CSharpSlotInfoT<QueryClause> Clause{"Clause", false, nullptr, false};

// The `Projection` operand position (a single `Expression` child). Unique to
// `QueryGroupClause.Projection` among the ported nodes. A `CSharpSlotInfoT<Expression>` (the
// element type is the `Expression` abstract base, complete via the `Expression.hpp` include
// above). `Expression.hpp` does NOT include `Slots.hpp` (the abstract base has no per-node
// slot statics -- the `Slots.Condition`/`Slots.Content`/`Slots.Clause` precedent), so this kind
// lives HERE in `Slots.hpp` (no include cycle). The kind name `Projection` collides with no
// class in the `Syntax` namespace, so no elaborated-type-specifier is needed for the kind
// name. DEFINED AFTER the `Slots.Expression` variable above, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to that variable (the
// `Expression`/`Condition`/`AdditionalArraySpecifier` collision precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> Projection{"Projection", false, nullptr, false};

// The `Key` operand position (a single `Expression` child). Unique to
// `QueryGroupClause.Key` among the ported nodes. A `CSharpSlotInfoT<Expression>` (the element
// type is the `Expression` abstract base, complete via the `Expression.hpp` include above).
// `Expression.hpp` does NOT include `Slots.hpp`, so this kind lives HERE in `Slots.hpp` (no
// include cycle). The kind name `Key` collides with no class in the `Syntax` namespace, so no
// elaborated-type-specifier is needed for the kind name. DEFINED AFTER the `Slots.Expression`
// variable above, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to that variable (the
// `Expression`/`Condition`/`Projection` collision precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> Key{"Key", false, nullptr, false};

// The `JoinIdentifier` token position (a single `Identifier` child -- the backing token of the
// NON-nullable `string` `JoinIdentifier` string-name `[Slot]` on `QueryJoinClause`). Unique to
// `QueryJoinClause.JoinIdentifierToken` among the ported nodes. A `CSharpSlotInfoT<Identifier>`
// (the element type is the `Identifier` token, complete via the `Identifier.hpp` include above).
// `Identifier.hpp` does NOT include `Slots.hpp` (the leaf token has no per-node slot statics --
// the `Slots.Identifier` precedent), so this kind lives HERE in `Slots.hpp` (no include cycle).
// The kind name `JoinIdentifier` collides with no class in the `Syntax` namespace, so no
// elaborated-type-specifier is needed for the kind name. DEFINED AFTER the `Slots.Identifier`
// variable above, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Identifier`) to avoid resolving to that variable (the
// `Identifier`/`Alias` collision precedent). The shared constant is constructed
// non-collection/non-optional; the per-node `JoinIdentifierTokenSlot` on `QueryJoinClause`
// carries the `IsOptional=false` flag (the join name is non-nullable -- the `LabelStatement`/
// `MemberType.MemberName` non-nullable-string-name-[Slot] precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Identifier> JoinIdentifier{"JoinIdentifier", false, nullptr, false};

// The `InExpression` operand position (a single `Expression` child). Unique to
// `QueryJoinClause.InExpression` among the ported nodes. A `CSharpSlotInfoT<Expression>` (the
// element type is the `Expression` abstract base, complete via the `Expression.hpp` include
// above). `Expression.hpp` does NOT include `Slots.hpp`, so this kind lives HERE in `Slots.hpp`
// (no include cycle). The kind name `InExpression` collides with no class in the `Syntax`
// namespace (there is `Expression`, not `InExpression`), so no elaborated-type-specifier is
// needed for the kind name. DEFINED AFTER the `Slots.Expression` variable above, so the element
// type is qualified (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to
// that variable (the `Expression`/`Condition`/`Projection` collision precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> InExpression{"InExpression", false, nullptr, false};

// The `OnExpression` operand position (a single `Expression` child). Unique to
// `QueryJoinClause.OnExpression` among the ported nodes. A `CSharpSlotInfoT<Expression>` (the
// element type is the `Expression` abstract base). `Expression.hpp` does NOT include `Slots.hpp`,
// so this kind lives HERE in `Slots.hpp` (no include cycle). The kind name `OnExpression`
// collides with no class in the `Syntax` namespace, so no elaborated-type-specifier is needed
// for the kind name. DEFINED AFTER the `Slots.Expression` variable above, so the element type
// is qualified (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to that
// variable (the `Expression`/`InExpression` collision precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> OnExpression{"OnExpression", false, nullptr, false};

// The `EqualsExpression` operand position (a single `Expression` child). Unique to
// `QueryJoinClause.EqualsExpression` among the ported nodes. A `CSharpSlotInfoT<Expression>`
// (the element type is the `Expression` abstract base). `Expression.hpp` does NOT include
// `Slots.hpp`, so this kind lives HERE in `Slots.hpp` (no include cycle). The kind name
// `EqualsExpression` collides with no class in the `Syntax` namespace, so no
// elaborated-type-specifier is needed for the kind name. DEFINED AFTER the `Slots.Expression`
// variable above, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Expression`) to avoid resolving to that variable (the
// `Expression`/`InExpression`/`OnExpression` collision precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Expression> EqualsExpression{"EqualsExpression", false, nullptr, false};

// The `IntoIdentifier` token position (a single `Identifier` child -- the backing token of the
// NULLABLE `string?` `IntoIdentifier` string-name `[Slot]` on `QueryJoinClause`). Unique to
// `QueryJoinClause.IntoIdentifierToken` among the ported nodes. A `CSharpSlotInfoT<Identifier>`
// (the element type is the `Identifier` token, complete via the `Identifier.hpp` include above).
// `Identifier.hpp` does NOT include `Slots.hpp`, so this kind lives HERE in `Slots.hpp` (no
// include cycle). The kind name `IntoIdentifier` collides with no class in the `Syntax` namespace,
// so no elaborated-type-specifier is needed for the kind name. DEFINED AFTER the
// `Slots.Identifier`/`Slots.JoinIdentifier` variables above, so the element type is qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Identifier`) to avoid resolving to those variables
// (the `Identifier`/`Alias`/`JoinIdentifier` collision precedent). The shared constant is
// constructed non-collection/non-optional; the per-node `IntoIdentifierTokenSlot` on
// `QueryJoinClause` carries the `IsOptional=true` flag (the group-join name is nullable -- the
// `GotoStatement.Label`/`CatchClause.VariableName` nullable-string-name-[Slot] precedent).
inline const CSharpSlotInfoT<::ILSpy::Decompiler::CSharp::Syntax::Identifier> IntoIdentifier{"IntoIdentifier", false, nullptr, false};

} // namespace ILSpy::Decompiler::CSharp::Syntax::Slots

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_SLOTS_HPP
