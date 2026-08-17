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
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_IASTVISITOR_HPP
