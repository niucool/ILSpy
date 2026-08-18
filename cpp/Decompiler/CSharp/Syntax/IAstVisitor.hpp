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

// Port of the `IAstVisitor` interface in ICSharpCode.Decompiler/CSharp/Syntax (the
// generated `IAstVisitor.g.cs`, emitted by DecompilerSyntaxTreeGenerator.cs). This is the
// visitor the concrete AST nodes' `AcceptVisitor` dispatch to and the output visitor
// (`CSharpOutputVisitor`) implements -- the dispatch half of the visitor pattern, paired
// with `AstNode::AcceptVisitor` (the other half, declared abstract on `AstNode`).
//
// The C# generator emits three visitor interfaces -- `IAstVisitor` (returns void, no data),
// `IAstVisitor<out S>` (returns S), and `IAstVisitor<in T, out S>` (returns S, takes T
// data) -- each carrying one `Visit<NodeName>(ConcreteNode)` method per concrete node that
// needs a visitor. Only the void `IAstVisitor` is consumed by the engine (the
// `CSharpOutputVisitor : IAstVisitor` pretty-printer; the generic variants are declared on
// `AstNode` and the generated `DepthFirstAstVisitor<T>`/`<T,S>` bases but unused elsewhere
// -- verified by grep), and C++ has no virtual template methods (the C# `abstract T
// AcceptVisitor<T>(IAstVisitor<T>)` generic-method dispatch cannot be ported faithfully), so
// this port carries the void `IAstVisitor` only; the generic variants stay deferred.
//
// This header is the foundation the concrete nodes plug into. It is currently an abstract
// base with no per-node `Visit` methods: the concrete node hierarchy has not been ported
// yet (it lands next, per PORT_PLAN.md section 5.2 / decision D1: port the generated
// *output* by hand). As each concrete node lands it adds a pure-virtual
// `virtual void Visit<NodeName>(class <NodeName>*) = 0;` here and overrides
// `AstNode::AcceptVisitor` to call `visitor.Visit<NodeName>(this)`; the node's hand-written
// file forward-declares the concrete type so this header need not include it. The
// `DepthFirstAstVisitor` base (DepthFirstAstVisitor.hpp) supplies the default
// `VisitChildren` walk every per-node `Visit` override delegates to.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITOR_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITOR_HPP

namespace ILSpy::Decompiler::CSharp::Syntax {

// Forward declarations of the concrete AST nodes whose `Visit` methods are declared below.
// A pointer parameter needs only a forward declaration, so this header does not include the
// concrete node headers (the node's own header includes this one so its `AcceptVisitor`
// override can call `visitor.Visit<NodeName>(this)`); more are added as the hierarchy lands.
class Identifier;
class NullReferenceExpression;
class ThisReferenceExpression;
class BaseReferenceExpression;
class PrimitiveExpression;
class BinaryOperatorExpression;
class AssignmentExpression;
class UnaryOperatorExpression;
class ConditionalExpression;
class ParenthesizedExpression;
class CheckedExpression;
class UncheckedExpression;
class DirectionExpression;
class ThrowExpression;
// `PrimitiveType` is the first concrete `AstType` (a leaf, no `[Slot]` children);
// `SimpleType` is the first concrete `AstType` with a collection slot (`TypeArguments`);
// `MemberType` is the second collection-slot `AstType` (a `Target` `AstType` + a `MemberName`
// string-name `[Slot]` + a `TypeArguments` collection).
class PrimitiveType;
class SimpleType;
class MemberType;
// `ArraySpecifier` is the rank-specifier leaf of an array type (the `[...]`/`[,...]` of a
// `ComposedType`); the first in-order piece of the `ComposedType` dependency.
// `Attribute` is the first `GeneralScope`-sub-namespace concrete node (a sealed `AstNode` with a
// required `AstType` `Type` slot + an `Expression` `Arguments` collection + a `HasArgumentList`
// bool scalar -- the `MemberType` shape, needed by `AttributeSection`).
class ArraySpecifier;
class Attribute;
// `AttributeSection` is the second `GeneralScope`-sub-namespace concrete node (a sealed
// `AstNode` with an optional `Identifier` `AttributeTargetToken` slot + an `Attribute`
// `Attributes` collection -- the bracketed group of attributes, needed by `ComposedType`'s
// Attributes collection and by every Statement/TypeMember/GeneralScope/ParameterDeclaration node).
class AttributeSection;
// `ComposedType` is the third concrete `AstType` with a collection slot and the first ported
// node with TWO collection slots (an `Attributes` collection + a required `BaseType` `AstType`
// single slot + an `ArraySpecifiers` collection), carrying `HasRefSpecifier`/
// `HasReadOnlySpecifier`/`HasNullableSpecifier` bools and a `PointerRank` int -- the array/
// pointer/nullable/modifier wrapper over a `BaseType`.
class ComposedType;
// `CastExpression` is the first AstType-bearing `Expression` node (a sealed `Expression` with
// a required `AstType` `Type` slot + a required `Expression` `Expression` slot -- the cast
// `(type)expression`, reusing the already-ported `Slots::Type`/`Slots::Expression` kinds).
// `AsExpression`/`IsExpression` are the sibling pair of the CastExpression two-required-slot
// shape but with the slot order reversed (`Expression` at index 0, `Type` at index 1) plus an
// `as`/`is` keyword const string -- the `expression 'as'/'is' type` operators, reusing the
// already-ported `Slots::Type`/`Slots::Expression` kinds.
class CastExpression;
class AsExpression;
class IsExpression;
// The single-`[Slot("Type")]`-`AstType` `Expression` nodes -- the simplest slot-bearing
// AstType-bearing `Expression` shape (one required single `AstType` `Type` slot, reusing the
// already-ported `Slots::Type` kind): `TypeReferenceExpression` (an ILSpy wrapper letting an
// `AstType` appear in expression position, no const keyword) and the keyword trio
// `TypeOfExpression`/`DefaultValueExpression`/`SizeOfExpression` (a `typeof`/`default`/`sizeof`
// keyword const string each). All four share the exact same one-required-single-slot shape.
class TypeReferenceExpression;
class TypeOfExpression;
class DefaultValueExpression;
class SizeOfExpression;
// `IdentifierExpression` is the first AstType-bearing `Expression` with a COLLECTION slot
// (a sealed `Expression` with a required `Identifier` string-name `[Slot]` over a backing
// `IdentifierToken` + a `TypeArguments` `AstNodeCollection<AstType>` collection -- the
// `simple_name ::= identifier ('<' type (',' type)* '>')?` production, structurally identical
// to `SimpleType` but deriving from `Expression`; reusing the already-ported `Slots::Identifier`
// and `Slots::TypeArgument` kinds).
class IdentifierExpression;
// `MemberReferenceExpression` is the first `Expression` with BOTH a single `Expression` child
// slot and a collection slot (a sealed `Expression` with a required `Target` `Expression` slot
// + a required `MemberName` string-name `[Slot]` over a backing `MemberNameToken` + a
// `TypeArguments` `AstNodeCollection<AstType>` collection -- the
// `member_reference_expression ::= expression '.' identifier ('<' type (',' type)* '>')?`
// production, the `MemberType` shape with an `Expression` target and no scalar; reusing the
// already-ported `Slots::Identifier`/`Slots::TypeArgument` kinds and the new `Slots::
// TargetExpression` kind).
class MemberReferenceExpression;
// `PointerReferenceExpression` is the structural twin of `MemberReferenceExpression` (the
// same `MemberType` shape -- a `Target` `Expression` slot + a `MemberName` string-name `[Slot]` over
// a backing `MemberNameToken` + a `TypeArguments` `AstNodeCollection<AstType>` collection) but for
// the `pointer_member_access ::= expression '->' identifier ( '<' type ( ',' type )* '>' )?`
// production, plus the `ArrowToken` const string "->"; the two are disjoint concrete types so
// the pattern matcher's `other is PointerReferenceExpression` gate distinguishes them; reusing
// the already-ported `Slots::TargetExpression`/`Slots::Identifier`/`Slots::TypeArgument` kinds
// with no new `Slots` constant).
class PointerReferenceExpression;
// `InvocationExpression` is the second `Expression` with both a single `Expression` child slot
// and a collection slot (a sealed `Expression` with a required `Target` `Expression` slot + an
// `Arguments` `AstNodeCollection<Expression>` collection -- the
// `invocation_expression ::= expression '(' expression* ')'` production, the
// `MemberReferenceExpression` shape with no string-name `[Slot]` and no scalar; reusing the
// already-ported `Slots::TargetExpression`/`Slots::Argument` kinds).
class InvocationExpression;
// `IndexerExpression` is structurally identical to `InvocationExpression` (a sealed `Expression`
// with a required `Target` `Expression` slot + an `Arguments` `AstNodeCollection<Expression>`
// collection) but for the `element_access ::= expression '[' expression* ']'` production; reusing
// the already-ported `Slots::TargetExpression`/`Slots::Argument` kinds (the two are disjoint
// concrete types so the pattern matcher's `other is IndexerExpression` gate distinguishes them).
class IndexerExpression;
// `ArrayInitializerExpression` is the simplest collection-slot node (a non-sealed `Expression`
// whose sole child slot is the `Elements` `AstNodeCollection<Expression>` collection -- the
// `array_initializer ::= '{' expression* '}'` production), the dependency of
// `ObjectCreateExpression.Initializer` and `ArrayCreateExpression.Initializer`; the first ported
// node with a collection slot and NO single child slot, and the first non-sealed concrete node
// (its `[DecompilerAstNode(hasPatternPlaceholder: true)]` emits a `PatternPlaceholder` subclass --
// deferred). Reusing the already-ported `Slots::Expression` kind as the collection kind.
class ArrayInitializerExpression;
// `ObjectCreateExpression` is the first ported node to combine a collection with a nullable
// single child slot (a sealed `Expression` with a required `AstType` `Type` slot + an
// `Arguments` `AstNodeCollection<Expression>` collection + a nullable
// `ArrayInitializerExpression` `Initializer` slot -- the
// `object_create_expression ::= 'new' type '(' expression* ')' array_initializer?` production,
// reusing the already-ported `Slots::Type`/`Slots::Argument` kinds and the new `Slots::Initializer`
// kind; the `Arguments` collection is the first NON-incremental one-collection node since the
// `Initializer` single slot trails it).
class ObjectCreateExpression;
// `ArrayCreateExpression` is the first ported node with TWO collections FOLLOWED BY a single
// slot (a sealed `Expression` with a required `AstType` `Type` slot + an `Arguments`
// `AstNodeCollection<Expression>` collection + an `AdditionalArraySpecifiers`
// `AstNodeCollection<ArraySpecifier>` collection + a nullable `ArrayInitializerExpression`
// `Initializer` slot -- the `array_creation_expression ::= 'new' type '[' expression* ']'
// array_specifier* array_initializer?` production, reusing the already-ported `Slots::Type`/
// `Slots::Argument`/`Slots::Initializer` kinds and the new `Slots::AdditionalArraySpecifier`
// kind; both collections are non-incremental since the `Initializer` single slot trails them).
class ArrayCreateExpression;
// `ContinueStatement`/`BreakStatement`/`YieldBreakStatement` are the first concrete C# AST
// statement nodes -- the cleanest leaf statements (no `[Slot]` children, no members), the
// start of the Statement hierarchy (a leaf with no slots; `BreakStatement`/`YieldBreakStatement`
// carry a `BreakKeyword`/`YieldKeyword` const string the output visitor emits); plugging into
// the `IAstVisitor`/`AcceptVisitor` dispatch (the next in-order Phase-5 piece per the D253 plan).
class ContinueStatement;
class BreakStatement;
class YieldBreakStatement;
// `ReturnStatement`/`ThrowStatement`/`ExpressionStatement` are the first slot-bearing C# AST
// statement nodes -- the `UnaryOperatorExpression` D231 single-`Expression`-slot shape applied
// to a statement: `ReturnStatement`/`ThrowStatement` carry a single NULLABLE `Expression?`
// [Slot("Expression")] (the returned value / thrown exception, absent for a bare `return;`/
// rethrow `throw;`) plus a `ReturnKeyword`/`ThrowKeyword` const string; `ExpressionStatement`
// carries a single REQUIRED `Expression` [Slot("Expression")] (the statement-expression) and no
// const keyword. The nullable pair uses `MatchOptional` in its `DoMatch`, the required node uses
// the direct `MatchRequired` dispatch; all three reuse the already-ported `Slots::Expression`
// kind. The next in-order Phase-5 piece per the D254 plan ("the slot-bearing statements:
// ReturnStatement ... ThrowStatement ... ExpressionStatement ...").
class ReturnStatement;
class ThrowStatement;
class ExpressionStatement;
// `BlockStatement` is the first collection-bearing C# AST statement node -- the
// collection-only `ArrayInitializerExpression` D250 shape applied to the Statement hierarchy:
// a non-sealed `Statement` whose sole child slot is the `Statements` collection of `Statement`
// (the `block ::= '{' statement* '}'` production, the statement list inside the braces), with
// the new `Slots::Statement` kind. The next-in-order Phase-5 piece per the D255 plan ("the
// collection-bearing statements: BlockStatement the statement collection").
class BlockStatement;
// `GotoStatement`/`GotoCaseStatement`/`GotoDefaultStatement` are the goto family -- the next
// in-order Phase-5 piece per the D256 plan ("the EmptyStatement/GotoStatement/LabelStatement
// leaves"). `GotoStatement` is the `SimpleType` D237 string-name-`[Slot]` shape MINUS the
// `TypeArguments` collection: a single NULLABLE `string?` `Label` string-name `[Slot("Identifier")]`
// over a backing `LabelToken` `Identifier` slot (optional, the label may be absent), plus the
// `GotoKeyword` const string. `GotoCaseStatement` is the `ExpressionStatement` D255 shape (a
// single REQUIRED `Expression` `LabelExpression` slot) applied to the goto family, plus the
// `GotoKeyword`/`CaseKeyword` const strings. `GotoDefaultStatement` is the cleanest leaf of the
// goto family (no `[Slot]` children, no match members) plus the `GotoKeyword`/`DefaultKeyword`
// const strings. All three reuse the already-ported `Slots::Identifier`/`Slots::Expression`
// kinds with no new `Slots` constant.
class GotoStatement;
class GotoCaseStatement;
class GotoDefaultStatement;
// `IfElseStatement`/`WhileStatement`/`DoWhileStatement` are the Condition+embedded-statement
// loop/control statements -- the next in-order Phase-5 piece per the D257 plan ("the
// collection-bearing Condition+embedded-statement nodes: IfElseStatement/WhileStatement/
// DoWhileStatement"). They are all single-slot statements (no collection slots): `IfElseStatement`
// is a REQUIRED `Expression` `Condition` + a REQUIRED `Statement` `TrueStatement` + a NULLABLE
// `Statement?` `FalseStatement` (the `if/else`); `WhileStatement` is a REQUIRED `Expression`
// `Condition` + a REQUIRED `Statement` `EmbeddedStatement` (the `while`); `DoWhileStatement` is
// the `WhileStatement` shape with the slot order reversed (`EmbeddedStatement`-0/`Condition`-1,
// the source declaration order) plus a hand-written `(Expression, Statement)` convenience ctor
// with the param order reversed. `IfElseStatement` adds the new `Slots::TrueStatement`/
// `Slots::FalseStatement` kinds; `WhileStatement` adds the new `Slots::EmbeddedStatement` kind;
// `DoWhileStatement` reuses the already-ported `Slots::EmbeddedStatement`/`Slots::Condition`
// kinds.
class IfElseStatement;
class WhileStatement;
class DoWhileStatement;
// `YieldReturnStatement`/`EmptyStatement`/`LabelStatement` are the next simple statements -- the
// next in-order Phase-5 piece per the D258 plan ("the remaining concrete statements:
// YieldReturnStatement ... EmptyStatement/LabelStatement leaves"). `YieldReturnStatement` is the
// `ExpressionStatement` D255 shape (a single REQUIRED `Expression` slot) applied to the yield
// family plus the `YieldKeyword`/`ReturnKeyword` const strings (the `yield return expr;`
// production); `EmptyStatement` is the first ported statement leaf that carries its OWN
// `Location` field and overrides `StartLocation`/`EndLocation` (a one-column span, no `[Slot]`
// children, a type-only `DoMatch`); `LabelStatement` is the `GotoStatement` D257 string-name-
// `[Slot]` shape MINUS the nullable optionality and MINUS a const keyword (a single REQUIRED
// `string` `Label` over a backing `LabelToken`, the `identifier ':'` of a labeled statement).
// `YieldReturnStatement` reuses the already-ported `Slots::Expression` kind; `LabelStatement`
// reuses the already-ported `Slots::Identifier` kind; `EmptyStatement` adds no `Slots` kind
// (no slots); all three with no new `Slots` constant.
class YieldReturnStatement;
class EmptyStatement;
class LabelStatement;
// `CheckedStatement`/`UncheckedStatement`/`UnsafeStatement` are the embedded-`BlockStatement`
// keyword-statement trio -- the next in-order Phase-5 piece per the D259 plan ("the remaining
// concrete statements: ... CheckedStatement, UncheckedStatement, UnsafeStatement ..."). Each is a
// sealed `Statement` carrying a single REQUIRED `BlockStatement` `Body` child (the block under
// the keyword context) plus a const keyword string (the `CheckedStatement.CheckedKeyword` /
// `UncheckedStatement.UncheckedKeyword` / `UnsafeStatement.UnsafeKeyword`). They are the
// `ExpressionStatement` D255 single-required-slot shape with a `BlockStatement` child instead of
// an `Expression`, plus the const keyword (the `CheckedExpression` D234 const-string precedent
// applied to a statement). All three share the NEW `Slots::Body` kind (cycle-broken into
// `BlockStatement.hpp`, since `BlockStatement.hpp` includes `Slots.hpp`).
class CheckedStatement;
class UncheckedStatement;
class UnsafeStatement;
// `LockStatement` is the `WhileStatement` D258 two-required-single-slot shape (a REQUIRED
// `Expression` lock-object + a REQUIRED `Statement` `EmbeddedStatement` body) with the loop
// test slot renamed `Expression` -- the next in-order Phase-5 piece per the D260 plan. It
// reuses the already-ported `Slots::Expression` (by `UnaryOperatorExpression`) and
// `Slots::EmbeddedStatement` (by `WhileStatement`) kinds with no new `Slots` constant; the
// `Expression()` accessor shadows the `Expression` base type (the `ExpressionStatement` D255
// name-shadowing crux), so the port uses the elaborated `class Expression` specifier.
class LockStatement;
// `UsingStatement` is the `WhileStatement` D258 two-required-single-slot shape plus two bool
// scalars (`IsAsync`/`IsEnhanced`) and with the first slot typed the abstract `AstNode` base (a
// REQUIRED `ResourceAcquisition` `AstNode` child -- the `using (...)` production takes EITHER a
// local-variable-declaration OR an expression, both `AstNode`-derived; plus a REQUIRED
// `EmbeddedStatement` `Statement` body) -- the next in-order Phase-5 piece per the D261 plan.
// It adds the new `Slots::ResourceAcquisition` kind (a `CSharpSlotInfoT<AstNode>`, the first ported
// slot kind whose element type is the abstract `AstNode` base) and reuses the already-ported
// `Slots::EmbeddedStatement` (by `WhileStatement`). NO name-shadowing crux (no member is named
// `AstNode`/`Statement`; the `ResourceAcquisition()` accessor does not collide with any class),
// so no elaborated-type-specifier is needed anywhere. The `AwaitKeyword` const aliases
// `UnaryOperatorExpression::AwaitKeyword` (the canonical `await` literal).
class UsingStatement;
// `ForStatement` is the first ported node with a collection -> single -> collection -> single
// slot layout -- the next in-order Phase-5 piece per the D262 plan. A sealed `Statement` with an
// `Initializers` `AstNodeCollection<Statement>` collection (the init statements before the first
// `;`), a NULLABLE `Expression?` `Condition` (the loop test, absent for `for (;;)`), an
// `Iterators` `AstNodeCollection<Statement>` collection (the step statements after the second
// `;`), and a REQUIRED `Statement` `EmbeddedStatement` (the loop body); both collections are
// non-incremental (two collections), and both single slots follow collections so they use the
// index-less `SetChildNode` setter. It adds the new `Slots::ForInitializer`/`Slots::Iterator`
// kinds and reuses the already-ported `Slots::Condition` (by `ConditionalExpression`, the
// per-node slot carrying `IsOptional=true` for the nullable case)/`Slots::EmbeddedStatement` (by
// `WhileStatement`); NO name-shadowing crux (no member is named `Expression`/`Statement`).
class ForStatement;
// `VariableDesignation` is the abstract base of the C# 7 deconstruction designations -- the
// next in-order Phase-5 piece per the D263 plan (`ForeachStatement` needs the hierarchy). The
// abstract base itself gets NO `Visit` method (`NeedsVisitor = !IsAbstract && base.IsAbstract`
// is `false` since `VariableDesignation` is abstract), so only the two concrete subclasses land
// on `IAstVisitor`: `SingleVariableDesignation` (the `single_variable_designation ::= identifier`
// leaf, a sealed `VariableDesignation` with a single REQUIRED `string Identifier` string-name
// `[Slot("Identifier")]` over a backing `IdentifierToken` -- the `LabelStatement` D259 shape with a
// `VariableDesignation` base, plus the `SimpleType`/`IdentifierExpression` `Identifier` name-shadowing
// crux) and `ParenthesizedVariableDesignation` (the `tuple_designation ::= '(' designations? ')'`
// node, a sealed `VariableDesignation` whose sole child slot is the `VariableDesignations`
// `AstNodeCollection<VariableDesignation>` collection -- the `ArrayInitializerExpression` D250 /
// `BlockStatement` D256 collection-only shape with a `VariableDesignation` base, plus the new
// `Slots::VariableDesignation` kind). Neither ends in "AstType", so the generator's
// visit-method-name default yields `VisitSingleVariableDesignation`/`VisitParenthesizedVariableDesignation`.
class SingleVariableDesignation;
class ParenthesizedVariableDesignation;
// `ForeachStatement` is the `foreach_statement ::= 'await'? 'foreach' '(' type
// variable_designation 'in' expression ')' statement` node (C# grammar 13.9.5.1) -- the next
// in-order Phase-5 piece per the D263 plan (now unblocked by the `VariableDesignation` hierarchy
// just ported). A sealed `Statement` with FOUR single, REQUIRED (non-nullable) `[Slot]` children --
// a `VariableType` `AstType` (the element type), a `VariableDesignation` `VariableDesignation`
// (the loop variable or deconstruction), an `InExpression` `Expression` (the collection), and an
// `EmbeddedStatement` `Statement` (the loop body) -- plus an `IsAsync` bool scalar (the leading
// `await`). It reuses the already-ported `Slots::Type` (by `Attribute`),
// `Slots::VariableDesignation` (by `ParenthesizedVariableDesignation`), `Slots::Expression` (by
// `UnaryOperatorExpression`), and `Slots::EmbeddedStatement` (by `WhileStatement`) kinds with
// no new `Slots` constant; the `AwaitKeyword` const aliases `UnaryOperatorExpression::AwaitKeyword`
// (the `UsingStatement` D262 precedent). The `VariableDesignation()` slot accessor shadows the
// `VariableDesignation` class (the `Expression()`-of-type-`Expression` D231 crux applied to a
// `VariableDesignation`-typed slot accessor), so the port uses the elaborated
// `class VariableDesignation` specifier; the other three slot accessors do not shadow their
// element types.
class ForeachStatement;

// `VariableInitializer` is the `variable_declarator ::= identifier ( '=' expression )?` node
// (C# grammar 15.5.1) -- the element of a `VariableDeclaration`/`FixedStatement`'s `Variables`
// collection, the next in-order Phase-5 piece per the D265 plan (the dependency of
// `FixedStatement.Variables` and `VariableDeclarationStatement.Variables`). A non-sealed
// `AstNode` (the first ported `TypeMembers` node, the `[DecompilerAstNode(hasPatternPlaceholder:
// true)]` non-`sealed` form -- the `ArrayInitializerExpression` D250 precedent) with a REQUIRED
// `string Name` string-name `[Slot("Identifier")]` over a backing `NameToken` `Identifier` slot
// plus a NULLABLE `Expression?` `Initializer` `[Slot("Expression")]` single slot. NO name
// shadowing (the property is `Name`/`Initializer`, NOT `Identifier`/`Expression` -- the
// `LabelStatement` D259 / `MemberReferenceExpression.Target` D247 differently-named-property
// precedent), so no elaborated-type-specifier is needed; both slot kinds are already ported
// (`Slots::Identifier` by `SimpleType`, `Slots::Expression` by `UnaryOperatorExpression`).
class VariableInitializer;

// `FixedStatement` is the `fixed_statement ::= 'fixed' '(' type variable_initializer* ')' statement`
// node (C# grammar 24.7) -- the next in-order Phase-5 piece per the D266 plan (now unblocked -- it
// needs the `VariableInitializer` just ported plus a NEW `Slots::Variable` kind for its
// `Variables AstNodeCollection<VariableInitializer>` collection, plus the already-ported
// `Slots::Type`/`Slots::EmbeddedStatement` kinds). A sealed `Statement` with a single REQUIRED
// `AstType Type` slot at flattened index 0, a `Variables AstNodeCollection<VariableInitializer>`
// collection at index 1 (non-incremental since the `EmbeddedStatement` single slot follows), and a
// single REQUIRED `Statement EmbeddedStatement` at index 2 (the index-less `SetChildNode` setter,
// following a collection). NO name-shadowing crux (no member is named `AstType`/
// `VariableInitializer`/`Statement` -- the `ObjectCreateExpression` D251 differently-named-property
// precedent), so no elaborated-type-specifier is needed; the `Type`/`EmbeddedStatement` slot kinds
// are already ported (`Slots::Type` by `Attribute`, `Slots::EmbeddedStatement` by `WhileStatement`),
// and the new `Slots::Variable` kind is cycle-broken into `VariableInitializer.hpp`.
class FixedStatement;
// `CaseLabel`/`SwitchSection`/`SwitchStatement` are the switch family -- the next in-order
// Phase-5 piece per the D267 plan ("SwitchStatement, TryCatchStatement,
// LocalFunctionDeclarationStatement, VariableDeclarationStatement ..."). `CaseLabel` is a sealed
// `AstNode` (deriving DIRECTLY from the `AstNode` root, a leaf of a `SwitchSection`'s `CaseLabels`
// collection) with a single NULLABLE `Expression?` `Expression` `[Slot("Expression")]` slot (the
// case expression, null for `default:` -- the `ReturnStatement` D255 nullable-`Expression?`-slot
// shape applied to a direct-`AstNode`-derived node) plus `CaseKeyword`/`DefaultKeyword` const
// strings; the `Expression()` accessor shadows the `Expression` base type (the D231 crux). 
// `SwitchSection` is a non-sealed `AstNode` (the `[DecompilerAstNode(hasPatternPlaceholder: true)]`
// non-`sealed` form) with TWO collections -- a `CaseLabels AstNodeCollection<CaseLabel>` (the
// `case`/`default` labels) and a `Statements AstNodeCollection<Statement>` (the section's
// statements, `[Slot("EmbeddedStatement")]` reusing the `WhileStatement` D258 kind as a collection
// -- the kind-collapsing-by-name design); both non-incremental (two collections), the `ComposedType`
// D242 two-collection shape with NO single slot between. `SwitchStatement` is a sealed `Statement`
// structurally the `InvocationExpression` D248 shape (a single REQUIRED `Expression` child at index
// 0 + a `SwitchSections AstNodeCollection<SwitchSection>` collection at index 1, incremental) with
// the `SwitchKeyword` const string; the `Expression()` accessor shadows the `Expression` base type
// (the D231 crux). `CaseLabel` reuses the already-ported `Slots::Expression` kind; `SwitchSection`
// adds the NEW cycle-broken `Slots::CaseLabel` kind (cycle-broken into `CaseLabel.hpp`) and reuses
// `Slots::EmbeddedStatement`; `SwitchStatement` adds the NEW cycle-broken `Slots::SwitchSection`
// kind (cycle-broken into `SwitchSection.hpp`) and reuses `Slots::Expression`.
class CaseLabel;
class SwitchSection;
class SwitchStatement;

// The try/catch family (the `try_statement` production): `CatchClause` (a non-sealed `AstNode`
// with four single slots -- a NULLABLE `AstType?` `Type`, a NULLABLE `string?` `VariableName`
// string-name `[Slot]` over a backing `VariableNameToken` `Identifier`, a NULLABLE `Expression?`
// `Condition`, and a REQUIRED `BlockStatement` `Body` -- plus `CatchKeyword`/`WhenKeyword`/
// `CondLPar`/`CondRPar` const strings; `hasPatternPlaceholder: true` so non-`final`; reusing the
// already-ported `Slots::Type`/`Slots::Identifier`/`Slots::Condition`/`Slots::Body` kinds; plus the
// NEW cycle-broken `Slots::CatchClause` kind in `CatchClause.hpp`) and `TryCatchStatement` (a
// sealed `Statement` structurally the `ObjectCreateExpression` D251 shape -- a single REQUIRED
// `BlockStatement` `TryBlock` + a NON-INCREMENTAL `CatchClauses AstNodeCollection<CatchClause>`
// collection + a NULLABLE `BlockStatement?` `FinallyBlock` trailing single -- plus
// `TryKeyword`/`FinallyKeyword` const strings; the NEW cycle-broken `Slots::TryBlock`/
// `Slots::FinallyBlock` kinds in `BlockStatement.hpp`, reusing `Slots::CatchClause`).
class CatchClause;
class TryCatchStatement;

// `VariableDeclarationStatement` is the `local_variable_declaration ::= type
// variable_initializer+` node (C# grammar 13.6.2.1) -- the next in-order Phase-5 piece per the D269
// plan (now unblocked -- it needs the `VariableInitializer` already ported by D266 for its
// `Variables` collection, plus a `Modifiers` `[Flags]` enum scalar). A sealed `Statement` with a
// single REQUIRED `AstType Type` `[Slot]` at flattened index 0, a `Variables
// AstNodeCollection<VariableInitializer>` collection `[Slot("Variable")]` at slot 1 (incremental --
// the node's only collection and its last slot), and a `Modifiers` scalar (a plain settable
// `Modifiers`-typed property, NOT a `[Slot]` -- the first ported `[Flags]` enum scalar). NO
// name-shadowing crux (no member is named `AstType`/`VariableInitializer`/`Statement`/`Modifiers` --
// the `Modifiers()` accessor does not collide with the `Modifiers` `enum class`), so no
// elaborated-type-specifier is needed; `Slots::Type` is already ported (by `Attribute` D240) and
// `Slots::Variable` is cycle-broken into `VariableInitializer.hpp` (by `FixedStatement` D267), so
// `Slots.hpp` is unchanged; the `Modifiers` enum lives in its own `Modifiers.hpp` header.
class VariableDeclarationStatement;

// `EntityDeclaration` (the abstract base of the `TypeMember` hierarchy -- the common base of
// every type-member declaration node, with the abstract `SymbolKind` property, the virtual
// `Attributes`/`Name`/`NameToken`/`ReturnType`, the `Modifiers` scalar, and the
// `MatchAttributesAndModifiers` helper), `DestructorDeclaration` (the first concrete
// `TypeMember` -- the simplest `EntityDeclaration`: a sealed node with an `Attributes`
// `AttributeSection` collection + a required `NameToken` `Identifier` + a nullable `Body`
// `BlockStatement`, reusing the already-ported `Slots::AttributeSection`/`Identifier`/`Body` kinds
// with no new `Slots` constant), and `FieldDeclaration` (the first `EntityDeclaration` with two
// collections) are the next in-order Phase-5 pieces per the D271/D272 plan; `Accessor` (the
// simplest remaining concrete `EntityDeclaration` -- an `Attributes` collection + a nullable `Body`
// single, the `DestructorDeclaration` shape minus `NameToken`, plus an `AccessorKind` scalar and the
// `Name`/`NameToken` no-op overrides, reusing the already-ported `Slots::AttributeSection`/`Body`
// kinds) is the next per the D273 plan. The abstract `EntityDeclaration` base gets NO `Visit` method
// (`NeedsVisitor` is false for an abstract base); `DestructorDeclaration` adds
// `VisitDestructorDeclaration`, `FieldDeclaration` adds `VisitFieldDeclaration`, `Accessor` adds
// `VisitAccessor`, `EnumMemberDeclaration` adds `VisitEnumMemberDeclaration`, and
// `PropertyDeclaration` adds `VisitPropertyDeclaration`, `EventDeclaration` adds
// `VisitEventDeclaration`, and `CustomEventDeclaration` adds `VisitCustomEventDeclaration`.
// `ParameterDeclaration` (a direct-`AstNode` node, NOT an `EntityDeclaration` -- the next
// in-order piece per the D277 plan) adds `VisitParameterDeclaration`. `IndexerDeclaration` (the
// next in-order piece per the D278 plan, the first `EntityDeclaration` with a `Parameters`
// collection -- now unblocked by `ParameterDeclaration`) adds `VisitIndexerDeclaration`.
class EntityDeclaration;
class DestructorDeclaration;
class FieldDeclaration;
class Accessor;
class EnumMemberDeclaration;
class PropertyDeclaration;
class EventDeclaration;
class CustomEventDeclaration;
class ParameterDeclaration;
class IndexerDeclaration;
class OperatorDeclaration;
// `OperatorDeclaration` is the `operator_declaration ::= attribute_section* modifier+ type
// ( type '.' )? 'operator' 'checked'? operator_token '(' parameter* ')' ( block | ';' )` node
// (C# grammar 15.10.1) -- the next in-order Phase-5 piece per the D279 plan (now unblocked -- it
// needs the `ParameterDeclaration` already ported by D278 for its `Parameters` collection, plus
// the `OperatorType` enum co-located in `OperatorDeclaration.cs`). A sealed `EntityDeclaration` with
// an `Attributes` collection + a required `ReturnType` `AstType` + a nullable
// `PrivateImplementationType` `AstType?` + a `Parameters` `AstNodeCollection<ParameterDeclaration>`
// collection + a nullable `Body` `BlockStatement?`, plus an `OperatorType` scalar (a settable enum
// with NO `Any` member, so the `DoMatch` term is the plain `==`) and four const keyword tokens; the
// `Name`/`NameToken` overrides return the operator's method name / null and throw on set. NO new
// `Slots` constant (reuses the already-ported `Slots::AttributeSection`/`Type`/
// `PrivateImplementationType`/`Parameter`/`Body` kinds); the `OperatorType()` accessor shadows the
// `OperatorType` enum (the `DirectionExpression.FieldDirection` D235 name-shadowing crux).
// `ConstructorInitializer` is the `constructor_initializer ::= ':' ( 'base' | 'this' ) '(' expression* ')'`
// node (C# grammar 15.11.1) -- the next in-order Phase-5 piece per the D280 plan (the dependency of
// `ConstructorDeclaration.Initializer`, its sole child slot is the `Arguments` `AstNodeCollection<
// Expression>` collection reusing `Slots::Argument`). A sealed `AstNode` (deriving DIRECTLY from the
// `AstNode` root, NOT an `EntityDeclaration`) with a `ConstructorInitializerType` scalar (`Any`/
// `Base`/`This`, the `Any`-wildcard `DoMatch` term) and the `Arguments` collection; the
// `ConstructorInitializerType()` accessor shadows the `ConstructorInitializerType` enum (the D235/
// D280 name-shadowing crux).
// `ConstructorDeclaration` is the `constructor_declaration ::= attribute_section* modifier*
// identifier '(' parameter* ')' constructor_initializer? ( block | ';' )` node (C# grammar 15.11.1)
// -- the next in-order Phase-5 piece per the D280 plan (now unblocked by `ConstructorInitializer`
// just ported and `ParameterDeclaration` D278). A sealed `EntityDeclaration` with an `Attributes`
// collection + a required `NameToken` `Identifier` (the `DestructorDeclaration` D272
// `[ExcludeFromMatch]` shape -- the `Name` `MatchString` term is NOT in `DoMatch`) + a `Parameters`
// `AstNodeCollection<ParameterDeclaration>` collection + a nullable `ConstructorInitializer?`
// `Initializer` + a nullable `BlockStatement?` `Body`; the two-collection `OperatorDeclaration` D280
// shape with two trailing nullable singles. NO new `Slots` constant (reuses the already-ported
// `Slots::AttributeSection`/`Identifier`/`Parameter`/`ConstructorInitializer`/`Body` kinds).
class ConstructorInitializer;
class ConstructorDeclaration;
// `TypeParameterDeclaration` is the `type_parameter ::= attribute_section* ( 'in' | 'out' )?
// identifier` node (C# grammar 8.5 / 15.2.3) -- the next in-order Phase-5 piece per the D281 plan (the
// dependency of `MethodDeclaration.TypeParameters`, and of `TypeDeclaration`/`DelegateDeclaration`
// `TypeParameters` collections). A sealed direct-`AstNode` node (NOT an `EntityDeclaration`) with an
// `Attributes` `AttributeSection` collection + a `Variance` `VarianceModifier` enum scalar (no `Any`
// member, so the `DoMatch` term is the plain `==`) + a non-nullable `string Name` string-name `[Slot]`
// over a required `NameToken` `Identifier`; reuses the already-ported `Slots::AttributeSection`/
// `Slots::Identifier` kinds with no new `Slots` constant. The `VarianceModifier` enum ports to
// `cpp/Decompiler/TypeSystem/VarianceModifier.hpp` (the `SymbolKind` D271 / `ReferenceKind` D278
// precedent).
class TypeParameterDeclaration;
// `Constraint` is the `type_parameter_constraints_clause ::= 'where' type ':' type+` node (C#
// grammar 15.2.5) -- the `where T : ...` clause on a generic method or type. A sealed direct-
// `AstNode` node (NOT `EntityDeclaration` -- a constraint is a structural node owned by a
// declaration's `Constraints` collection) with a REQUIRED `SimpleType` `TypeParameter` single
// slot (the constrained type parameter) + a `BaseTypes` `AstNodeCollection<AstType>` collection (the
// base-type constraint list); reuses the cycle-broken `Slots::ConstraintTypeParameter` (in
// `SimpleType.hpp`) and the new `Slots::BaseType` (in `Slots.hpp`) with no further `Slots`
// constant. The next in-order Phase-5 piece per the D282 plan (the dependency of
// `MethodDeclaration.Constraints`).
class Constraint;
// `MethodDeclaration` is the `method_declaration ::= attribute_section* modifier* type ( type '.' )?
// identifier type_parameter* '(' parameter* ')' constraint* ( block | ';' )` node (C# grammar
// 15.6.1) -- the next in-order Phase-5 piece per the D283 plan (now unblocked by `TypeParameterDeclaration`
// D282 + `ParameterDeclaration` D278 + `Constraint` just ported). A sealed `EntityDeclaration` (the
// `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final`) with FOUR collections
// (`Attributes`/`TypeParameters`/`Parameters`/`Constraints`) plus four singles (`ReturnType`/
// `PrivateImplementationType`/`NameToken`/`Body`) -- the first ported `EntityDeclaration` with more
// than two collections. It adds the two NEW cycle-broken `Slots::TypeParameter` (in
// `TypeParameterDeclaration.hpp`) and `Slots::Constraint` (in `Constraint.hpp`) kinds; the other
// six kinds (`Slots::AttributeSection`/`Type`/`PrivateImplementationType`/`Identifier`/`Parameter`/
// `Body`) are all already ported. `NameToken` is NOT `[ExcludeFromMatch]` (unlike
// `ConstructorDeclaration` D281 / `DestructorDeclaration` D272), so the `Name` `MatchString` term
// IS in `DoMatch`; the inherited base `Name()` kind-walks (no `Name` override). The
// `IsExtensionMethod` computed property reads the first `Parameters` element's `HasThisModifier`
// via `GetChildByKind<ParameterDeclaration>(&Slots::Parameter)` (the D271 kind-based read).
class MethodDeclaration;
class ExtensionDeclaration;

// The `FixedVariableInitializer` (the `fixed_size_buffer_declarator ::= identifier '[' expression
// ']'` element of a `FixedFieldDeclaration.Variables` collection) -- a sealed direct-`AstNode` node
// with a required `NameToken` `Identifier` + a required `CountExpression` `Expression` (the
// `VariableInitializer` D266 shape with the `Expression` required, not nullable). It adds the new
// cycle-broken `Slots::FixedVariable` kind (in `FixedVariableInitializer.hpp`); the two kinds
// (`Slots::Identifier`/`Slots::Expression`) are already ported.
class FixedVariableInitializer;

// The `FixedFieldDeclaration` (the `fixed_size_buffer_declaration` node) -- a sealed
// `EntityDeclaration` with an `Attributes` collection + a required `ReturnType` + a `Variables`
// `FixedVariableInitializer` collection (the `FieldDeclaration` D273 two-collection shape with
// the `Variables` element type changed to `FixedVariableInitializer`) plus the `FixedKeyword`
// const. It reuses `Slots::AttributeSection`/`Slots::Type` and the new cycle-broken
// `Slots::FixedVariable`; the `Name`/`NameToken` are NOT overridden (the inherited base kind-walk
// finds no `NameToken` slot, so `Name()` returns empty).
class FixedFieldDeclaration;

// The C# `public interface IAstVisitor` -- the void-returning AST visitor interface. The
// concrete node's `AcceptVisitor(IAstVisitor&)` calls the matching `Visit<NodeName>(this)`
// on it; `CSharpOutputVisitor` (the pretty-printer) implements it. An abstract base (a
// protected constructor + a virtual destructor): it cannot be instantiated directly, only
// derived from, matching the C# interface. Per-node `Visit` pure-virtuals are added as the
// concrete node hierarchy lands.
class IAstVisitor {
protected:
    IAstVisitor() = default;

public:
    virtual ~IAstVisitor() = default;

    // Per-node `Visit<NodeName>(ConcreteNode*)` pure-virtual methods. Each concrete node's
    // `AcceptVisitor` override calls the matching `Visit<NodeName>(this)`. The concrete leaf
    // nodes land here (the rest of the generated hierarchy follows): the `Identifier` token
    // (the first non-`Expression` concrete node), then the leaf expressions (the three
    // reference expressions and the literal-carrying `PrimitiveExpression`).
    virtual void VisitIdentifier(Identifier*) = 0;
    virtual void VisitNullReferenceExpression(NullReferenceExpression*) = 0;
    virtual void VisitThisReferenceExpression(ThisReferenceExpression*) = 0;
    virtual void VisitBaseReferenceExpression(BaseReferenceExpression*) = 0;
    virtual void VisitPrimitiveExpression(PrimitiveExpression*) = 0;
    virtual void VisitBinaryOperatorExpression(BinaryOperatorExpression*) = 0;
    virtual void VisitAssignmentExpression(AssignmentExpression*) = 0;
    virtual void VisitUnaryOperatorExpression(UnaryOperatorExpression*) = 0;
    virtual void VisitConditionalExpression(ConditionalExpression*) = 0;
    virtual void VisitParenthesizedExpression(ParenthesizedExpression*) = 0;
    virtual void VisitCheckedExpression(CheckedExpression*) = 0;
    virtual void VisitUncheckedExpression(UncheckedExpression*) = 0;
    virtual void VisitDirectionExpression(DirectionExpression*) = 0;
    virtual void VisitThrowExpression(ThrowExpression*) = 0;
    virtual void VisitPrimitiveType(PrimitiveType*) = 0;
    virtual void VisitSimpleType(SimpleType*) = 0;
    virtual void VisitMemberType(MemberType*) = 0;
    virtual void VisitArraySpecifier(ArraySpecifier*) = 0;
    virtual void VisitAttribute(Attribute*) = 0;
    virtual void VisitAttributeSection(AttributeSection*) = 0;
    virtual void VisitComposedType(ComposedType*) = 0;
    virtual void VisitCastExpression(CastExpression*) = 0;
    virtual void VisitAsExpression(AsExpression*) = 0;
    virtual void VisitIsExpression(IsExpression*) = 0;
    virtual void VisitTypeReferenceExpression(TypeReferenceExpression*) = 0;
    virtual void VisitTypeOfExpression(TypeOfExpression*) = 0;
    virtual void VisitDefaultValueExpression(DefaultValueExpression*) = 0;
    virtual void VisitSizeOfExpression(SizeOfExpression*) = 0;
    virtual void VisitIdentifierExpression(IdentifierExpression*) = 0;
    virtual void VisitMemberReferenceExpression(MemberReferenceExpression*) = 0;
    virtual void VisitPointerReferenceExpression(PointerReferenceExpression*) = 0;
    virtual void VisitInvocationExpression(InvocationExpression*) = 0;
    virtual void VisitIndexerExpression(IndexerExpression*) = 0;
    virtual void VisitArrayInitializerExpression(ArrayInitializerExpression*) = 0;
    virtual void VisitObjectCreateExpression(ObjectCreateExpression*) = 0;
    virtual void VisitArrayCreateExpression(ArrayCreateExpression*) = 0;
    virtual void VisitContinueStatement(ContinueStatement*) = 0;
    virtual void VisitBreakStatement(BreakStatement*) = 0;
    virtual void VisitYieldBreakStatement(YieldBreakStatement*) = 0;
    virtual void VisitReturnStatement(ReturnStatement*) = 0;
    virtual void VisitThrowStatement(ThrowStatement*) = 0;
    virtual void VisitExpressionStatement(ExpressionStatement*) = 0;
    virtual void VisitBlockStatement(BlockStatement*) = 0;
    virtual void VisitGotoStatement(GotoStatement*) = 0;
    virtual void VisitGotoCaseStatement(GotoCaseStatement*) = 0;
    virtual void VisitGotoDefaultStatement(GotoDefaultStatement*) = 0;
    virtual void VisitIfElseStatement(IfElseStatement*) = 0;
    virtual void VisitWhileStatement(WhileStatement*) = 0;
    virtual void VisitDoWhileStatement(DoWhileStatement*) = 0;
    virtual void VisitYieldReturnStatement(YieldReturnStatement*) = 0;
    virtual void VisitEmptyStatement(EmptyStatement*) = 0;
    virtual void VisitLabelStatement(LabelStatement*) = 0;
    virtual void VisitCheckedStatement(CheckedStatement*) = 0;
    virtual void VisitUncheckedStatement(UncheckedStatement*) = 0;
    virtual void VisitUnsafeStatement(UnsafeStatement*) = 0;
    virtual void VisitLockStatement(LockStatement*) = 0;
    virtual void VisitUsingStatement(UsingStatement*) = 0;
    virtual void VisitForStatement(ForStatement*) = 0;
    virtual void VisitSingleVariableDesignation(SingleVariableDesignation*) = 0;
    virtual void VisitParenthesizedVariableDesignation(ParenthesizedVariableDesignation*) = 0;
    virtual void VisitForeachStatement(ForeachStatement*) = 0;
    virtual void VisitVariableInitializer(VariableInitializer*) = 0;
    virtual void VisitFixedStatement(FixedStatement*) = 0;
    virtual void VisitCaseLabel(CaseLabel*) = 0;
    virtual void VisitSwitchSection(SwitchSection*) = 0;
    virtual void VisitSwitchStatement(SwitchStatement*) = 0;
    virtual void VisitCatchClause(CatchClause*) = 0;
    virtual void VisitTryCatchStatement(TryCatchStatement*) = 0;
    virtual void VisitVariableDeclarationStatement(VariableDeclarationStatement*) = 0;
    virtual void VisitDestructorDeclaration(DestructorDeclaration*) = 0;
    virtual void VisitFieldDeclaration(FieldDeclaration*) = 0;
    virtual void VisitAccessor(Accessor*) = 0;
    virtual void VisitEnumMemberDeclaration(EnumMemberDeclaration*) = 0;
    virtual void VisitPropertyDeclaration(PropertyDeclaration*) = 0;
    virtual void VisitEventDeclaration(EventDeclaration*) = 0;
    virtual void VisitCustomEventDeclaration(CustomEventDeclaration*) = 0;
    virtual void VisitParameterDeclaration(ParameterDeclaration*) = 0;
    virtual void VisitIndexerDeclaration(IndexerDeclaration*) = 0;
    virtual void VisitOperatorDeclaration(OperatorDeclaration*) = 0;
    virtual void VisitConstructorInitializer(ConstructorInitializer*) = 0;
    virtual void VisitConstructorDeclaration(ConstructorDeclaration*) = 0;
    virtual void VisitTypeParameterDeclaration(TypeParameterDeclaration*) = 0;
    virtual void VisitConstraint(Constraint*) = 0;
    virtual void VisitMethodDeclaration(MethodDeclaration*) = 0;
    virtual void VisitExtensionDeclaration(ExtensionDeclaration*) = 0;
    virtual void VisitFixedVariableInitializer(FixedVariableInitializer*) = 0;
    virtual void VisitFixedFieldDeclaration(FixedFieldDeclaration*) = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITOR_HPP
