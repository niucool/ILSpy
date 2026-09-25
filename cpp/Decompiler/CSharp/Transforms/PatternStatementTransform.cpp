// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Transforms/PatternStatementTransform.cs --
// the pattern-based statement transforms over the C# AST. Ported arms: the
// cascading if-else simplification (`if (a) A else { if (b) B }` -> `else if`),
// the conditional-logic reassociation (`a && (b && c)` -> `(a && b) && c`),
// the negated-equality rewrite (`!(a == b)` -> `a != b`), and the for reshape
// (`v = init; while (v op end) { stmts; v = ...; }` -> `for (...) { stmts; }`,
// plus the declaration merge into an existing for's initializers).
//
// The C# `PatternStatementTransform : ContextTrackingVisitor<AstNode>, IAstTransform`
// ports to the IAstTransform + internal DepthFirstAstVisitor convention (the
// NormalizeBlockStatements precedent). The ContextTrackingVisitor<AstNode> base
// ports with it: the type/method context tracking (Initialize/Uninitialize and
// the per-declaration overrides feeding currentTypeDefinition/currentMethod) and
// the re-visit loop -- VisitChildren keeps visiting a child as long as the visit
// REPLACES it, because some transforms delete/replace nodes before and after the
// node being transformed. The C# relies on the visitor's AstNode return value to
// know where to keep iterating; the port's void visitor carries the same value in
// the visitor's `lastResult` slot (every Visit override records there the node the
// C# method returns, and the re-visit loop reads it after each AcceptVisitor).
//
// DEFERRED loudly at their C# slots, landing with their own slices (the smallest-
// first order of the port plan): foreach-on-array / inline-array / multi-dim,
// TransformAutomaticProperty, the destructor
// TransformDestructorFinalizerWithPattern, TransformTryCatchFinally, the C# 7.3
// backing-field rewrite -- ALL LANDED (the foreach arms in `b4e892ad2` /
// `df98f5c45`, the automatic property in `b31a10a2f` / `0073d82c7`, the
// destructor in `77a8695af`, try-catch-finally in `b40d74d57`, the fixed /
// using arms in `6f27d7110` / `e87a68ff8`). The `DeclareVariables
// declareVariables` member's analysis (Analyze/GetDeclarationPoint, feeding
// the for reshape's iterator-variable bail) is ported, as is the mutation half
// (its own earlier slices).
//
// The remaining known sub-deferrals, loud in place:
// AddressUsedForSingleCall (VariableCanBeUsedAsForeachLocal -- the per-variable
// address-use list), the anonymous-type `var` decision (NRExtensions
// ContainsAnonymousType), and the automatic-events family
// (IsEventBackingFieldDeclaration -- the PropertyAndEventBackingFieldLookup
// metadata surface).

#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cassert>
#include <stdexcept>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace PatternMatching = ::ILSpy::Decompiler::CSharp::Syntax::PatternMatching;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace {

// ---- Simplify cascading if-else-if statements ----------------------------------------

// The C# `static readonly IfElseStatement cascadingIfElsePattern` (line ~1013):
//
//     IfElseStatement {
//         Condition = AnyNode, TrueStatement = AnyNode,
//         FalseStatement = BlockStatement { Statements = {
//             NamedNode("nestedIfStatement", IfElseStatement {
//                 Condition = AnyNode, TrueStatement = AnyNode,
//                 FalseStatement = OptionalNode(AnyNode) }) } } }
//
// The C# embeds the pattern nodes into the statement slots through the generated
// `implicit operator Expression/Statement(Pattern)` conversions; the port uses the
// `Expression::ToExpression` / `Statement::ToStatement` wrappers (the
// PatternPlaceholder bridge). Built lazily as a process-lifetime singleton (the
// GetForeachPatterns convention); the placeholder nodes the wrappers allocate are
// the singleton's own (never freed, like the C# static's GC graph).
struct CascadingIfElsePatternHolder {
    // The nested if-else's pattern children.
    PatternMatching::AnyNode nestedCondition;
    PatternMatching::AnyNode nestedTrueStatement;
    PatternMatching::AnyNode nestedFalseStatement;
    PatternMatching::OptionalNode nestedFalse{nestedFalseStatement};
    // The nested `if` -- both required operands present, the optional else slot
    // holding the OptionalNode.
    Syntax::IfElseStatement nestedIf{
        Syntax::Expression::ToExpression(nestedCondition),
        Syntax::Statement::ToStatement(nestedTrueStatement),
        Syntax::Statement::ToStatement(nestedFalse)};
    PatternMatching::NamedNode nestedIfStatement{"nestedIfStatement", nestedIf};
    // The else block carrying exactly the nested if (a BlockStatement pattern whose
    // Statements collection holds the single NamedNode).
    Syntax::BlockStatement elseBlock;
    // The outer if-else's pattern children.
    PatternMatching::AnyNode outerCondition;
    PatternMatching::AnyNode outerTrueStatement;
    Syntax::IfElseStatement pattern{
        Syntax::Expression::ToExpression(outerCondition),
        Syntax::Statement::ToStatement(outerTrueStatement)};

    CascadingIfElsePatternHolder() {
        elseBlock.Statements().Add(Syntax::Statement::ToStatement(nestedIfStatement));
        pattern.FalseStatement(&elseBlock);
    }
};

Syntax::IfElseStatement& CascadingIfElsePattern() {
    static CascadingIfElsePatternHolder holder;
    return holder.pattern;
}

// ---- for ------------------------------------------------------------------------------

// The C# `static readonly AstNode variableAssignPattern` + `static readonly
// WhileStatement forPattern` (lines ~157-183), the same lazy process-lifetime
// singleton convention:
//
//     variableAssignPattern: ExpressionStatement(
//         AssignmentExpression(NamedNode("variable", IdentifierExpression(AnyString)),
//                              AnyNode("initializer")))
//
//     forPattern: WhileStatement {
//         Condition = BinaryOperatorExpression {
//             Left = NamedNode("ident", IdentifierExpression(AnyString)),
//             Operator = BinaryOperatorType.Any,
//             Right = AnyNode("endExpr") },
//         EmbeddedStatement = BlockStatement { Statements = {
//             Repeat(AnyNode("statement")),
//             NamedNode("iterator", ExpressionStatement(
//                 AssignmentExpression {
//                     Left = Backreference("ident"),
//                     Operator = AssignmentOperatorType.Any,
//                     Right = AnyNode() })) } } }
struct TransformForPatternsHolder {
    // -- `$variable = $initializer;`
    PatternMatching::AnyNode initializer{"initializer"};
    Syntax::IdentifierExpression variable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode variableNamed{"variable", variable};
    Syntax::AssignmentExpression variableAssign{
        Syntax::Expression::ToExpression(variableNamed),
        Syntax::AssignmentOperatorType::Assign,
        Syntax::Expression::ToExpression(initializer)};
    Syntax::ExpressionStatement variableAssignPattern{&variableAssign};

    // -- `while ($ident $op $endExpr) { $statement...; $ident = ...; }`
    Syntax::IdentifierExpression ident{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode identNamed{"ident", ident};
    PatternMatching::AnyNode endExpr{"endExpr"};
    Syntax::BinaryOperatorExpression loopCondition{
        Syntax::Expression::ToExpression(identNamed),
        Syntax::BinaryOperatorType::Any,
        Syntax::Expression::ToExpression(endExpr)};
    PatternMatching::AnyNode statement{"statement"};
    PatternMatching::Repeat statementRepeat{statement};
    PatternMatching::Backreference identBackreference{"ident"};
    PatternMatching::AnyNode iteratorRight;
    Syntax::AssignmentExpression iteratorAssign{
        Syntax::Expression::ToExpression(identBackreference),
        Syntax::AssignmentOperatorType::Any,
        Syntax::Expression::ToExpression(iteratorRight)};
    Syntax::ExpressionStatement iteratorStatement{&iteratorAssign};
    PatternMatching::NamedNode iteratorNamed{"iterator", iteratorStatement};
    Syntax::BlockStatement loopBody;
    Syntax::WhileStatement forPattern{&loopCondition, &loopBody};

    TransformForPatternsHolder() {
        loopBody.Statements().Add(Syntax::Statement::ToStatement(statementRepeat));
        loopBody.Statements().Add(Syntax::Statement::ToStatement(iteratorNamed));
    }
};

TransformForPatternsHolder& GetTransformForPatterns() {
    static TransformForPatternsHolder holder;
    return holder;
}

// ---- foreach -----------------------------------------------------------------------------

// The C# `static readonly ForStatement forOnArrayPattern` (line ~287):
//
//     for ($indexVariable = 0; $indexVariable < $arrayVariable.Length;
//          $indexVariable = $indexVariable + 1)
//     {
//         $itemVariable = $arrayVariable[$indexVariable];
//         $statements...
//     }
//
// the same lazy process-lifetime singleton convention.
struct ForeachOnArrayPatternHolder {
    // -- `indexVariable = 0;`
    Syntax::IdentifierExpression indexVariable{
        std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode indexVariableNamed{"indexVariable", indexVariable};
    Syntax::PrimitiveExpression zero{Syntax::PrimitiveValue(0)};
    Syntax::AssignmentExpression indexInit{
        Syntax::Expression::ToExpression(indexVariableNamed),
        Syntax::AssignmentOperatorType::Assign, &zero};
    Syntax::ExpressionStatement indexInitStatement{&indexInit};

    // -- `indexVariable < arrayVariable.Length`
    PatternMatching::IdentifierExpressionBackreference indexRef{"indexVariable"};
    Syntax::IdentifierExpression arrayVariable{
        std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode arrayVariableNamed{"arrayVariable", arrayVariable};
    Syntax::MemberReferenceExpression arrayLength{
        Syntax::Expression::ToExpression(arrayVariableNamed), "Length"};
    Syntax::BinaryOperatorExpression condition{
        Syntax::Expression::ToExpression(indexRef),
        Syntax::BinaryOperatorType::LessThan, &arrayLength};

    // -- `indexVariable = indexVariable + 1;`
    Syntax::PrimitiveExpression one{Syntax::PrimitiveValue(1)};
    Syntax::BinaryOperatorExpression indexIncrement{
        Syntax::Expression::ToExpression(indexRef),
        Syntax::BinaryOperatorType::Add, &one};
    Syntax::AssignmentExpression iteratorAssign{
        Syntax::Expression::ToExpression(indexRef),
        Syntax::AssignmentOperatorType::Assign, &indexIncrement};
    Syntax::ExpressionStatement iteratorStatement{&iteratorAssign};

    // -- `itemVariable = arrayVariable[indexVariable]; $statements...`
    PatternMatching::IdentifierExpressionBackreference arrayRef{"arrayVariable"};
    Syntax::IdentifierExpression itemVariable{
        std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode itemVariableNamed{"itemVariable", itemVariable};
    Syntax::IndexerExpression elementAccess{
        Syntax::Expression::ToExpression(arrayRef)};
    Syntax::AssignmentExpression itemAssign{
        Syntax::Expression::ToExpression(itemVariableNamed),
        Syntax::AssignmentOperatorType::Assign, &elementAccess};
    Syntax::ExpressionStatement itemAssignStatement{&itemAssign};
    PatternMatching::AnyNode statements{"statements"};
    PatternMatching::Repeat statementsRepeat{statements};
    Syntax::BlockStatement body;
    Syntax::ForStatement pattern;

    ForeachOnArrayPatternHolder() {
        elementAccess.Arguments().Add(
            Syntax::Expression::ToExpression(indexRef));
        body.Statements().Add(&itemAssignStatement);
        body.Statements().Add(Syntax::Statement::ToStatement(statementsRepeat));
        pattern.Initializers().Add(&indexInitStatement);
        pattern.Condition(&condition);
        pattern.Iterators().Add(&iteratorStatement);
        pattern.EmbeddedStatement(&body);
    }
};

ForeachOnArrayPatternHolder& GetForeachOnArrayPattern() {
    static ForeachOnArrayPatternHolder holder;
    return holder;
}

// The C# multi-dimensional-foreach patterns (lines ~516-570):
//
//     variableAssignUpperBoundPattern:  `$variable = $collection.GetUpperBound($index);`
//     variableAssignLowerBoundPattern:  `$variable = $collection.GetLowerBound($index);`
//     foreachVariableOnMultArrayAssignPattern:
//         `$variable = $collection[$index, $index, ...];`
//     forOnArrayMultiDimPattern:
//         `for (; $indexVariable <= $upperBoundVariable;
//               $indexVariable = $indexVariable + 1) { $lowerBoundAssign; $statements... }`
struct ForeachMultiDimPatternsHolder {
    // -- `$variable = $collection.GetUpperBound($index);`
    Syntax::IdentifierExpression upperVariable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode upperVariableNamed{"variable", upperVariable};
    Syntax::IdentifierExpression upperCollection{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode upperCollectionNamed{"collection", upperCollection};
    Syntax::MemberReferenceExpression upperBoundMember{
        Syntax::Expression::ToExpression(upperCollectionNamed), "GetUpperBound"};
    Syntax::PrimitiveExpression upperAnyIndex{Syntax::AnyValueTag{}};
    PatternMatching::NamedNode upperIndexNamed{"index", upperAnyIndex};
    Syntax::InvocationExpression upperBoundCall{&upperBoundMember};
    Syntax::AssignmentExpression upperBoundAssign{
        Syntax::Expression::ToExpression(upperVariableNamed),
        Syntax::AssignmentOperatorType::Assign, &upperBoundCall};
    Syntax::ExpressionStatement variableAssignUpperBoundPattern{&upperBoundAssign};

    // -- `$variable = $collection.GetLowerBound($index);`
    Syntax::IdentifierExpression lowerVariable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode lowerVariableNamed{"variable", lowerVariable};
    Syntax::IdentifierExpression lowerCollection{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode lowerCollectionNamed{"collection", lowerCollection};
    Syntax::MemberReferenceExpression lowerBoundMember{
        Syntax::Expression::ToExpression(lowerCollectionNamed), "GetLowerBound"};
    Syntax::PrimitiveExpression lowerAnyIndex{Syntax::AnyValueTag{}};
    PatternMatching::NamedNode lowerIndexNamed{"index", lowerAnyIndex};
    Syntax::InvocationExpression lowerBoundCall{&lowerBoundMember};
    Syntax::AssignmentExpression lowerBoundAssign{
        Syntax::Expression::ToExpression(lowerVariableNamed),
        Syntax::AssignmentOperatorType::Assign, &lowerBoundCall};
    Syntax::ExpressionStatement variableAssignLowerBoundPattern{&lowerBoundAssign};

    // -- `$variable = $collection[$index, $index, ...];`
    Syntax::IdentifierExpression itemVariable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode itemVariableNamed{"variable", itemVariable};
    Syntax::IdentifierExpression itemCollection{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode itemCollectionNamed{"collection", itemCollection};
    Syntax::IndexerExpression itemElementAccess{
        Syntax::Expression::ToExpression(itemCollectionNamed)};
    Syntax::AssignmentExpression itemAssign{
        Syntax::Expression::ToExpression(itemVariableNamed),
        Syntax::AssignmentOperatorType::Assign, &itemElementAccess};
    Syntax::ExpressionStatement foreachVariableOnMultArrayAssignPattern{&itemAssign};
    Syntax::IdentifierExpression itemIndex{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode itemIndexNamed{"index", itemIndex};
    PatternMatching::Repeat itemIndexes{itemIndexNamed};

    // -- the nested for loop (empty initializers -- the lower-bound
    // assignment precedes the loop).
    Syntax::IdentifierExpression indexVariable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode indexVariableNamed{"indexVariable", indexVariable};
    Syntax::IdentifierExpression upperBoundVariable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode upperBoundVariableNamed{"upperBoundVariable", upperBoundVariable};
    Syntax::BinaryOperatorExpression multiDimCondition{
        Syntax::Expression::ToExpression(indexVariableNamed),
        Syntax::BinaryOperatorType::LessThanOrEqual,
        Syntax::Expression::ToExpression(upperBoundVariableNamed)};
    PatternMatching::IdentifierExpressionBackreference multiDimIndexRef{"indexVariable"};
    Syntax::PrimitiveExpression one{Syntax::PrimitiveValue(1)};
    Syntax::BinaryOperatorExpression multiDimIncrement{
        Syntax::Expression::ToExpression(multiDimIndexRef),
        Syntax::BinaryOperatorType::Add, &one};
    Syntax::AssignmentExpression multiDimIteratorAssign{
        Syntax::Expression::ToExpression(multiDimIndexRef),
        Syntax::AssignmentOperatorType::Assign, &multiDimIncrement};
    Syntax::ExpressionStatement multiDimIterator{&multiDimIteratorAssign};
    PatternMatching::AnyNode anyLowerBoundAssign{"lowerBoundAssign"};
    PatternMatching::AnyNode anyStatements{"statements"};
    PatternMatching::Repeat statementsRepeat{anyStatements};
    Syntax::BlockStatement multiDimBody;
    Syntax::ForStatement forOnArrayMultiDimPattern;

    ForeachMultiDimPatternsHolder() {
        upperBoundCall.Arguments().Add(
            Syntax::Expression::ToExpression(upperIndexNamed));
        lowerBoundCall.Arguments().Add(
            Syntax::Expression::ToExpression(lowerIndexNamed));
        itemElementAccess.Arguments().Add(
            Syntax::Expression::ToExpression(itemIndexes));
        multiDimBody.Statements().Add(
            Syntax::Statement::ToStatement(anyLowerBoundAssign));
        multiDimBody.Statements().Add(
            Syntax::Statement::ToStatement(statementsRepeat));
        forOnArrayMultiDimPattern.Condition(&multiDimCondition);
        forOnArrayMultiDimPattern.Iterators().Add(&multiDimIterator);
        forOnArrayMultiDimPattern.EmbeddedStatement(&multiDimBody);
    }
};

ForeachMultiDimPatternsHolder& GetForeachMultiDimPatterns() {
    static ForeachMultiDimPatternsHolder holder;
    return holder;
}

// The C# `forOnInlineArrayPattern` (line ~410):
//   `for ($indexVariable = 0; $indexVariable < $length;
//        $indexVariable = $indexVariable + 1)
//      { $itemVariable = $elementAccess; $statements... }`
// (the `$length` is any literal; the element access is any node -- the arm
// checks it is the InlineArrayElementRef helper by its resolved symbol).
struct ForeachOnInlineArrayPatternHolder {
    Syntax::IdentifierExpression indexVariable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode indexVariableNamed{"indexVariable", indexVariable};
    PatternMatching::IdentifierExpressionBackreference indexRef{"indexVariable"};
    Syntax::PrimitiveExpression zero{Syntax::PrimitiveValue(0)};
    Syntax::AssignmentExpression indexInit{
        Syntax::Expression::ToExpression(indexVariableNamed),
        Syntax::AssignmentOperatorType::Assign, &zero};
    Syntax::ExpressionStatement indexInitStatement{&indexInit};
    Syntax::PrimitiveExpression anyLength{Syntax::AnyValueTag{}};
    PatternMatching::NamedNode lengthNamed{"length", anyLength};
    Syntax::BinaryOperatorExpression condition{
        Syntax::Expression::ToExpression(indexRef),
        Syntax::BinaryOperatorType::LessThan,
        Syntax::Expression::ToExpression(lengthNamed)};
    Syntax::PrimitiveExpression one{Syntax::PrimitiveValue(1)};
    Syntax::BinaryOperatorExpression increment{
        Syntax::Expression::ToExpression(indexRef),
        Syntax::BinaryOperatorType::Add, &one};
    Syntax::AssignmentExpression iteratorAssign{
        Syntax::Expression::ToExpression(indexRef),
        Syntax::AssignmentOperatorType::Assign, &increment};
    Syntax::ExpressionStatement iterator{&iteratorAssign};
    Syntax::IdentifierExpression itemVariable{std::string(PatternMatching::Pattern::AnyString)};
    PatternMatching::NamedNode itemVariableNamed{"itemVariable", itemVariable};
    PatternMatching::AnyNode elementAccess{"elementAccess"};
    Syntax::AssignmentExpression itemAssign{
        Syntax::Expression::ToExpression(itemVariableNamed),
        Syntax::AssignmentOperatorType::Assign,
        Syntax::Expression::ToExpression(elementAccess)};
    Syntax::ExpressionStatement itemAssignStatement{&itemAssign};
    PatternMatching::AnyNode anyStatement{"statements"};
    PatternMatching::Repeat statementsRepeat{anyStatement};
    Syntax::BlockStatement body;
    Syntax::ForStatement pattern;

    ForeachOnInlineArrayPatternHolder() {
        pattern.Initializers().Add(&indexInitStatement);
        pattern.Condition(&condition);
        pattern.Iterators().Add(&iterator);
        body.Statements().Add(&itemAssignStatement);
        body.Statements().Add(Syntax::Statement::ToStatement(statementsRepeat));
        pattern.EmbeddedStatement(&body);
    }
};

ForeachOnInlineArrayPatternHolder& GetForeachOnInlineArrayPattern() {
    static ForeachOnInlineArrayPatternHolder holder;
    return holder;
}

// The C# destructor patterns (lines ~933-948):
//   destructorBodyPattern: `{ try { $body } finally { base.Finalize(); } }`
//   destructorPattern: `$modifiers void Finalize() { destructorBodyPattern }`
struct DestructorPatternsHolder {
    PatternMatching::AnyNode body{"body"};
    Syntax::BaseReferenceExpression baseReference;
    Syntax::MemberReferenceExpression finalizeMember{&baseReference, "Finalize"};
    Syntax::InvocationExpression finalizeCall{&finalizeMember};
    // The C# `new BlockStatement { new InvocationExpression(...) }` -- the
    // expression-statement convenience wrap.
    Syntax::ExpressionStatement finalizeStatement{&finalizeCall};
    Syntax::BlockStatement finallyBlock;
    Syntax::TryCatchStatement tryCatch;
    Syntax::BlockStatement destructorBodyPattern;
    Syntax::PrimitiveType voidType;
    PatternMatching::AnyNode anyAttribute;
    PatternMatching::Repeat anyAttributes{anyAttribute};
    Syntax::MethodDeclaration destructorPattern;

    DestructorPatternsHolder() {
        voidType.Keyword("void");
        destructorPattern.Modifiers(Syntax::Modifiers::Any);
        finallyBlock.Statements().Add(&finalizeStatement);
        tryCatch.TryBlock(Syntax::BlockStatement::ToBlockStatement(body));
        tryCatch.FinallyBlock(&finallyBlock);
        destructorBodyPattern.Statements().Add(&tryCatch);
        destructorPattern.Attributes().Add(
            Syntax::AttributeSection::ToAttributeSection(anyAttributes));
        destructorPattern.ReturnType(&voidType);
        destructorPattern.Name("Finalize");
        destructorPattern.Body(&destructorBodyPattern);
    }
};

DestructorPatternsHolder& GetDestructorPatterns() {
    static DestructorPatternsHolder holder;
    return holder;
}

// The C# `tryCatchFinallyPattern` (line ~985): the nested
// `try { try { ... } catch { ... } } finally { ... }` shape.
struct TryCatchFinallyPatternsHolder {
    PatternMatching::AnyNode innerTry{"innerTry"};
    PatternMatching::AnyNode anyCatch;
    PatternMatching::Repeat anyCatches{anyCatch};
    Syntax::TryCatchStatement innerTryCatch;
    Syntax::BlockStatement outerTryBlock;
    PatternMatching::AnyNode finallyBlock;
    Syntax::TryCatchStatement tryCatchFinallyPattern;

    TryCatchFinallyPatternsHolder() {
        innerTryCatch.TryBlock(Syntax::BlockStatement::ToBlockStatement(innerTry));
        innerTryCatch.CatchClauses().Add(
            Syntax::CatchClause::ToCatchClause(anyCatches));
        outerTryBlock.Statements().Add(&innerTryCatch);
        tryCatchFinallyPattern.TryBlock(&outerTryBlock);
        tryCatchFinallyPattern.FinallyBlock(
            Syntax::BlockStatement::ToBlockStatement(finallyBlock));
    }
};

TryCatchFinallyPatternsHolder& GetTryCatchFinallyPatterns() {
    static TryCatchFinallyPatternsHolder holder;
    return holder;
}

// The C# `addressOfPinnableReference` pattern (line ~1091):
// `&$target.GetPinnableReference()` (the C# 7.3 pattern-based fixed form).
// Reference types are handled by DetectPinnedRegions.IsCustomRefPinPattern.
struct PatternBasedFixedPatternsHolder {
    PatternMatching::AnyNode target{"target"};
    Syntax::MemberReferenceExpression pinnableMember{
        Syntax::Expression::ToExpression(target), "GetPinnableReference"};
    Syntax::InvocationExpression pinnableCall{&pinnableMember};
    Syntax::UnaryOperatorExpression addressOfPinnableReference{
        &pinnableCall, Syntax::UnaryOperatorType::AddressOf};
};

PatternBasedFixedPatternsHolder& GetPatternBasedFixedPatterns() {
    static PatternBasedFixedPatternsHolder holder;
    return holder;
}

// The C# `NameCouldBeBackingFieldOfAutomaticProperty` (line ~878): the C#-style
// `<Name>k__BackingField` or the VB-style `_Name` (the C# regex
// `^(<(?<name>.+)>k__BackingField|_(?<name>.+))$`, hand-matched).
bool NameCouldBeBackingFieldOfAutomaticProperty(const std::string& name,
                                                 std::string* propertyName) {
    static const std::string kBackingFieldSuffix = ">k__BackingField";
    if (name.size() > kBackingFieldSuffix.size() + 1 && !name.empty() &&
        name[0] == '<' &&
        name.compare(name.size() - kBackingFieldSuffix.size(),
                     kBackingFieldSuffix.size(), kBackingFieldSuffix) == 0) {
        *propertyName = name.substr(
            1, name.size() - kBackingFieldSuffix.size() - 1);
        return true;
    }
    if (name.size() >= 2 && name[0] == '_') {
        *propertyName = name.substr(1);
        return true;
    }
    return false;
}

// The C# automatic-property patterns (lines ~694-738):
//   automaticPropertyPattern:
//     `$RET $Name { get { return $fieldReference; }
//                 set { $fieldReference = value; } }`
//   automaticReadonlyPropertyPattern: the getter-only form.
// (Each pattern owns its own accessor/body chain -- a node cannot be
// parented in two patterns.)
struct AutomaticPropertyPatternsHolder {
    // -- the full pattern: getter + setter
    PatternMatching::AnyNode fieldReference{"fieldReference"};
    Syntax::ReturnStatement fieldReturn;
    Syntax::BlockStatement getterBody;
    PatternMatching::AnyNode getterAnyAttribute;
    PatternMatching::Repeat getterAttributes{getterAnyAttribute};
    Syntax::Accessor getter;
    PatternMatching::IdentifierExpressionBackreference fieldReferenceBack{
        "fieldReference"};
    Syntax::IdentifierExpression valueIdentifier{"value"};
    Syntax::AssignmentExpression fieldAssign{
        Syntax::Expression::ToExpression(fieldReferenceBack),
        Syntax::AssignmentOperatorType::Assign, &valueIdentifier};
    Syntax::ExpressionStatement fieldAssignStatement{&fieldAssign};
    Syntax::BlockStatement setterBody;
    PatternMatching::AnyNode setterAnyAttribute;
    PatternMatching::Repeat setterAttributes{setterAnyAttribute};
    Syntax::Accessor setter;
    PatternMatching::AnyNode anyReturnType;
    PatternMatching::AnyNode anyPrivateImpl;
    PatternMatching::OptionalNode optionalPrivateImpl{anyPrivateImpl};
    PatternMatching::AnyNode propertyAnyAttribute;
    PatternMatching::Repeat propertyAttributes{propertyAnyAttribute};
    Syntax::PropertyDeclaration automaticPropertyPattern;

    // -- the readonly pattern's own getter chain
    PatternMatching::AnyNode readonlyFieldReference{"fieldReference"};
    Syntax::ReturnStatement readonlyFieldReturn;
    Syntax::BlockStatement readonlyGetterBody;
    PatternMatching::AnyNode roGetterAnyAttribute;
    PatternMatching::Repeat roGetterAttributes{roGetterAnyAttribute};
    Syntax::Accessor readonlyGetter;
    PatternMatching::AnyNode roAnyReturnType;
    PatternMatching::AnyNode roAnyPrivateImpl;
    PatternMatching::OptionalNode roOptionalPrivateImpl{roAnyPrivateImpl};
    PatternMatching::AnyNode roPropertyAnyAttribute;
    PatternMatching::Repeat roPropertyAttributes{roPropertyAnyAttribute};
    Syntax::PropertyDeclaration automaticReadonlyPropertyPattern;

    AutomaticPropertyPatternsHolder() {
        // the full pattern
        fieldReturn.Expression(Syntax::Expression::ToExpression(fieldReference));
        getterBody.Statements().Add(&fieldReturn);
        getter.Modifiers(Syntax::Modifiers::Any);
        getter.Body(&getterBody);
        getter.Attributes().Add(
            Syntax::AttributeSection::ToAttributeSection(getterAttributes));
        setterBody.Statements().Add(&fieldAssignStatement);
        setter.Modifiers(Syntax::Modifiers::Any);
        setter.Body(&setterBody);
        setter.Attributes().Add(
            Syntax::AttributeSection::ToAttributeSection(setterAttributes));
        automaticPropertyPattern.Attributes().Add(
            Syntax::AttributeSection::ToAttributeSection(propertyAttributes));
        automaticPropertyPattern.Modifiers(Syntax::Modifiers::Any);
        automaticPropertyPattern.ReturnType(Syntax::AstType::ToType(anyReturnType));
        automaticPropertyPattern.PrivateImplementationType(
            Syntax::AstType::ToType(optionalPrivateImpl));
        automaticPropertyPattern.Name(
            std::string(PatternMatching::Pattern::AnyString));
        automaticPropertyPattern.Getter(&getter);
        automaticPropertyPattern.Setter(&setter);

        // the readonly pattern
        readonlyFieldReturn.Expression(
            Syntax::Expression::ToExpression(readonlyFieldReference));
        readonlyGetterBody.Statements().Add(&readonlyFieldReturn);
        readonlyGetter.Modifiers(Syntax::Modifiers::Any);
        readonlyGetter.Body(&readonlyGetterBody);
        readonlyGetter.Attributes().Add(
            Syntax::AttributeSection::ToAttributeSection(roGetterAttributes));
        automaticReadonlyPropertyPattern.Attributes().Add(
            Syntax::AttributeSection::ToAttributeSection(roPropertyAttributes));
        automaticReadonlyPropertyPattern.Modifiers(Syntax::Modifiers::Any);
        automaticReadonlyPropertyPattern.ReturnType(
            Syntax::AstType::ToType(roAnyReturnType));
        automaticReadonlyPropertyPattern.PrivateImplementationType(
            Syntax::AstType::ToType(roOptionalPrivateImpl));
        automaticReadonlyPropertyPattern.Name(
            std::string(PatternMatching::Pattern::AnyString));
        automaticReadonlyPropertyPattern.Getter(&readonlyGetter);
    }
};

AutomaticPropertyPatternsHolder& GetAutomaticPropertyPatterns() {
    static AutomaticPropertyPatternsHolder holder;
    return holder;
}

// The C# `bool DescendIntoStatement(AstNode node)` -- the continue-scan gate:
// do not descend into expressions (their identifier references are not
// statements) or into NESTED loops (a continue there targets the nested loop,
// not this one). Stateless, so a file-local free function (the port's probe
// convention).
bool DescendIntoStatement(Syntax::AstNode* node) {
    if (dynamic_cast<Syntax::Expression*>(node) != nullptr ||
        dynamic_cast<Syntax::ExpressionStatement*>(node) != nullptr)
        return false;
    if (dynamic_cast<Syntax::WhileStatement*>(node) != nullptr ||
        dynamic_cast<Syntax::ForeachStatement*>(node) != nullptr ||
        dynamic_cast<Syntax::DoWhileStatement*>(node) != nullptr ||
        dynamic_cast<Syntax::ForStatement*>(node) != nullptr)
        return false;
    return true;
}

// The C# `bool ForStatementUsesVariable(ForStatement statement, ILVariable?
// variable)` -- the for statement reads the variable in its condition or one of
// its iterators.
bool ForStatementUsesVariable(Syntax::ForStatement* statement,
                              IL::ILVariable* variable) {
    if (statement->Condition() != nullptr) {
        for (Syntax::AstNode* node : statement->Condition()->DescendantsAndSelf()) {
            if (auto* identifierExpression =
                    dynamic_cast<Syntax::IdentifierExpression*>(node);
                identifierExpression != nullptr &&
                CS::GetILVariable(*identifierExpression) == variable)
                return true;
        }
    }
    for (int i = 0; i < statement->Iterators().Count(); i++) {
        for (Syntax::AstNode* node : statement->Iterators().At(i)->DescendantsAndSelf()) {
            if (auto* identifierExpression =
                    dynamic_cast<Syntax::IdentifierExpression*>(node);
                identifierExpression != nullptr &&
                CS::GetILVariable(*identifierExpression) == variable)
                return true;
        }
    }
    return false;
}

// The C# `bool IsVariableUsedAfter(Statement loop, ILVariable variable)` -- any
// use of the variable in the statements following the loop.
bool IsVariableUsedAfter(Syntax::Statement* loop, IL::ILVariable* variable) {
    for (Syntax::AstNode* sibling = loop->NextSibling(); sibling != nullptr;
         sibling = sibling->NextSibling()) {
        for (Syntax::AstNode* node : sibling->DescendantsAndSelf()) {
            if (auto* identifierExpression =
                    dynamic_cast<Syntax::IdentifierExpression*>(node);
                identifierExpression != nullptr &&
                CS::GetILVariable(*identifierExpression) == variable)
                return true;
        }
    }
    return false;
}

} // namespace

// The visitor half (the C# class body): the ContextTrackingVisitor<AstNode> state
// (the context field, the current type/method tracking) plus the per-node
// overrides. The C# `IAstTransform.Run` explicit implementation is the outer Run's
// drive below.
class PatternStatementTransformVisitor final : public Syntax::DepthFirstAstVisitor {
public:
    // The C# `[AllowNull] TransformContext context`.
    TransformContext* context = nullptr;
    // The transform's DeclareVariables analysis (the C# reads the same
    // instance's member through the transform object).
    DeclareVariables* declareVariables = nullptr;

    // The C# `protected ITypeDefinition? currentTypeDefinition` / `IMethod?
    // currentMethod` (the ContextTrackingVisitor tracking slots; the deferred
    // arms -- the destructor name, the automatic-property accessor owner -- read
    // them, carried now so the shell is complete).
    const TS::ITypeDefinition* currentTypeDefinition = nullptr;
    const TS::IMethod* currentMethod = nullptr;

    // The C# visitor's AstNode RETURN VALUE: the node the matching C# Visit method
    // returns -- the visited node after an in-place rewrite, or its replacement.
    // The re-visit loop in VisitChildren reads it after each child's
    // AcceptVisitor; the per-node defaults and every override below keep it
    // current (the base VisitChildren sets it to the visited node, the C#
    // `return node`).
    Syntax::AstNode* lastResult = nullptr;

    // The C# `protected void Initialize(TransformContext context)` /
    // `Uninitialize` (the ContextTrackingVisitor members; the Uninitialize half is
    // the visitor's destruction here -- the visitor is scoped to one Run).
    void Initialize(TransformContext& context) {
        currentTypeDefinition = context.CurrentTypeDefinition;
        currentMethod = dynamic_cast<const TS::IMethod*>(context.CurrentMember);
    }

    // ---- The ContextTrackingVisitor<AstNode>.VisitChildren re-visit loop ------
    //
    // Go through the children, and keep visiting a node as long as it changes.
    // Because some transforms delete/replace nodes before and after the node being
    // transformed, the transform's return value (lastResult) says where iteration
    // needs to continue. The C# `Debug.Assert(child != null && child.Parent ==
    // node)` ports as the assert (the return contract: a Visit records the node
    // now occupying the visited child's slot).
    void VisitChildren(Syntax::AstNode* node) override {
        for (Syntax::AstNode* child = node->FirstChild(); child != nullptr;
             child = child->NextSibling()) {
            Syntax::AstNode* oldChild;
            do {
                oldChild = child;
                child->AcceptVisitor(*this);
                child = lastResult;
                assert(child != nullptr && child->Parent() == node);
            } while (child != oldChild);
        }
        lastResult = node;
    }

    // ---- The ContextTrackingVisitor type/method tracking ----------------------
    // The C# `public override TResult VisitTypeDeclaration(...)` family: track the
    // enclosing type/method through the walk (restored on the way out; the C#
    // try/finally reduces to the sequential restore -- the engine's normal path
    // carries no exception past a Visit).

    void VisitTypeDeclaration(Syntax::TypeDeclaration* typeDeclaration) override {
        const TS::ITypeDefinition* oldType = currentTypeDefinition;
        currentTypeDefinition =
            dynamic_cast<const TS::ITypeDefinition*>(CS::GetSymbol(*typeDeclaration));
        Syntax::DepthFirstAstVisitor::VisitTypeDeclaration(typeDeclaration);
        currentTypeDefinition = oldType;
    }

    void VisitMethodDeclaration(Syntax::MethodDeclaration* methodDeclaration) override {
        // The destructor reshape routes before the tracking/base visit (the
        // C# derived override chains onto the ContextTrackingVisitor base).
        if (Syntax::AstNode* result = TransformDestructor(methodDeclaration)) {
            lastResult = result;
            return;
        }
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod =
            dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*methodDeclaration));
        Syntax::DepthFirstAstVisitor::VisitMethodDeclaration(methodDeclaration);
        currentMethod = oldMethod;
    }

    void VisitConstructorDeclaration(
        Syntax::ConstructorDeclaration* constructorDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod =
            dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*constructorDeclaration));
        Syntax::DepthFirstAstVisitor::VisitConstructorDeclaration(constructorDeclaration);
        currentMethod = oldMethod;
    }

    void VisitDestructorDeclaration(
        Syntax::DestructorDeclaration* destructorDeclaration) override {
        // The lowered-body unwrap routes before the tracking/base visit.
        if (Syntax::AstNode* result =
                TransformDestructorBody(destructorDeclaration)) {
            lastResult = result;
            return;
        }
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod =
            dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*destructorDeclaration));
        Syntax::DepthFirstAstVisitor::VisitDestructorDeclaration(destructorDeclaration);
        currentMethod = oldMethod;
    }

    void VisitOperatorDeclaration(
        Syntax::OperatorDeclaration* operatorDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod =
            dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*operatorDeclaration));
        Syntax::DepthFirstAstVisitor::VisitOperatorDeclaration(operatorDeclaration);
        currentMethod = oldMethod;
    }

    void VisitAccessor(Syntax::Accessor* accessor) override {
        const TS::IMethod* oldMethod = currentMethod;
        currentMethod = dynamic_cast<const TS::IMethod*>(CS::GetSymbol(*accessor));
        Syntax::DepthFirstAstVisitor::VisitAccessor(accessor);
        currentMethod = oldMethod;
    }

    // ---- The ported arms --------------------------------------------------------

    // The C# `public override AstNode VisitExpressionStatement(ExpressionStatement
    // ...)` (line ~82): the multi-dimensional foreach arm routes first in the
    // C#, then the for reshape.
    void VisitExpressionStatement(
        Syntax::ExpressionStatement* expressionStatement) override {
        if (Syntax::AstNode* result =
                TransformForeachOnMultiDimArray(expressionStatement)) {
            lastResult = result;
            return;
        }
        if (Syntax::AstNode* result = TransformFor(expressionStatement)) {
            lastResult = result;
            return;
        }
        Syntax::DepthFirstAstVisitor::VisitExpressionStatement(expressionStatement);
    }

    // The C# `public ForStatement? TransformFor(ExpressionStatement node)` (line
    // ~186): `v = init; while (v op end) { stmts; v = ...; }` becomes
    // `for (v = init; v op end; v = ...) { stmts; }`, and `v = init;` immediately
    // before an existing for statement that uses `v` moves into that for's
    // initializers.
    Syntax::ForStatement* TransformFor(Syntax::ExpressionStatement* node) {
        if (!context->DecompileRun->Settings().ForStatement())
            return nullptr;
        PatternMatching::Match m1 =
            Syntax::MatchNode(GetTransformForPatterns().variableAssignPattern, node);
        if (!m1.Success())
            return nullptr;
        std::vector<Syntax::IdentifierExpression*> variables =
            m1.Get<Syntax::IdentifierExpression>("variable");
        // The C# `.Single()` (exactly one capture by construction).
        assert(variables.size() == 1);
        IL::ILVariable* variable = CS::GetILVariable(*variables.front());
        Syntax::AstNode* next = node->NextSibling();
        if (next == nullptr)
            return nullptr;
        if (auto* nextForStatement = dynamic_cast<Syntax::ForStatement*>(next);
            nextForStatement != nullptr &&
            ForStatementUsesVariable(nextForStatement, variable)) {
            context->StepOnce("Move declaration into for initializer", node);
            node->Remove();
            next->InsertChildAfter(nullptr, node, &Syntax::Slots::ForInitializer);
            return nextForStatement;
        }
        PatternMatching::Match m3 =
            Syntax::MatchNode(GetTransformForPatterns().forPattern, next);
        if (!m3.Success())
            return nullptr;
        // ensure the variable in the for pattern is the same as in the declaration
        std::vector<Syntax::IdentifierExpression*> idents =
            m3.Get<Syntax::IdentifierExpression>("ident");
        assert(idents.size() == 1);
        if (variable != CS::GetILVariable(*idents.front()))
            return nullptr;
        auto* loop = dynamic_cast<Syntax::WhileStatement*>(next);
        // The forPattern is a WhileStatement pattern, so a successful match
        // guarantees the cast.
        assert(loop != nullptr);
        // Cannot convert to for loop, if the iteration variable is a ref local
        // used after the loop: its declaration is hoisted in front, leaving a
        // headless `for (; cond; v = ref ...)` whose only initialization is the
        // for-initializer ref-assignment -- which can't be split from a ref local
        // (CS8174). Keeping it a while-loop matches the source and keeps the
        // initializer on the decl.
        if (variable != nullptr && variable->Type->IsByRefLike() &&
            IsVariableUsedAfter(loop, variable))
            return nullptr;
        std::vector<Syntax::Statement*> iteratorCaptures =
            m3.Get<Syntax::Statement>("iterator");
        assert(iteratorCaptures.size() == 1);
        Syntax::Statement* iteratorStatement = iteratorCaptures.front();
        // Cannot convert to for loop, if any variable that is used in the
        // "iterator" part of the pattern, will be declared in the body of
        // the while-loop.
        if (IteratorVariablesDeclaredInsideLoopBody(iteratorStatement))
            return nullptr;
        // Cannot convert to for loop, because that would change the semantics of
        // the program: continue in while jumps to the condition block, whereas
        // continue in for jumps to the increment block.
        for (Syntax::AstNode* descendant :
             loop->DescendantNodes(DescendIntoStatement)) {
            auto* statement = dynamic_cast<Syntax::Statement*>(descendant);
            if (statement != nullptr &&
                dynamic_cast<Syntax::ContinueStatement*>(statement) != nullptr)
                return nullptr;
        }
        context->StepOnce("Transform while loop to for", loop);
        node->Remove();
        auto* newBody = new Syntax::BlockStatement();
        for (Syntax::Statement* stmt : m3.Get<Syntax::Statement>("statement"))
            newBody->Statements().Add(Syntax::Detach(stmt));
        auto* forStatement = new Syntax::ForStatement();
        CS::CopyAnnotationsFrom(forStatement, *loop);
        forStatement->Initializers().Add(node);
        forStatement->Condition(Syntax::Detach(loop->Condition()));
        forStatement->Iterators().Add(Syntax::Detach(iteratorStatement));
        forStatement->EmbeddedStatement(newBody);
        loop->ReplaceWith(forStatement);
        // The C# `context.EndStep(forStatement)` (the step-group close) folds
        // onto the single step hook.
        return forStatement;
    }

    // The C# `bool IteratorVariablesDeclaredInsideLoopBody(Statement
    // iteratorStatement)` (line ~266): a variable whose declaration point sits
    // in the same block as the iterator (the loop body) would be declared in
    // the body scope, which the for's iterator slot cannot see.
    bool IteratorVariablesDeclaredInsideLoopBody(Syntax::Statement* iteratorStatement) {
        for (Syntax::AstNode* node : iteratorStatement->DescendantsAndSelf()) {
            auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(node);
            if (identifier == nullptr)
                continue;
            IL::ILVariable* v = CS::GetILVariable(*identifier);
            if (v == nullptr || !DeclareVariables::VariableNeedsDeclaration(v->Kind))
                continue;
            if (declareVariables->GetDeclarationPoint(v)->Parent() ==
                iteratorStatement->Parent())
                return true;
        }
        return false;
    }

    // The C# `public override AstNode VisitForStatement(ForStatement
    // forStatement)` (line ~93): the foreach-on-array arm routes first; the
    // inline-array arm is DEFERRED (its GetSymbol/IMethod
    // DeclaringType.FullName checks need the resolved call surface).
    void VisitForStatement(Syntax::ForStatement* forStatement) override {
        if (Syntax::AstNode* result = TransformForeachOnArray(forStatement)) {
            lastResult = result;
            return;
        }
        if (Syntax::AstNode* result =
                TransformForeachOnInlineArray(forStatement)) {
            lastResult = result;
            return;
        }
        Syntax::DepthFirstAstVisitor::VisitForStatement(forStatement);
    }

    // The C# `static bool AddressUsedForSingleCall(IL.ILVariable v,
    // IL.BlockContainer? loop)` (line ~346): the variable's single address
    // use feeds an instance-method call as the this pointer (the first
    // argument), and the call is not within a NESTED loop container (the
    // parent walk reaches the loop's container before any other). Any
    // mutation by the call then cannot be observed.
    bool AddressUsedForSingleCall(IL::ILVariable* v, IL::BlockContainer* loop) {
        if (v->StoreCount == 1 && v->AddressCount == 1 && v->LoadCount == 0 &&
            v->Type != nullptr &&
            v->Type->IsReferenceType() == std::optional<bool>(false)) {
            if (v->AddressInstructions.empty())
                return false;
            IL::LdLoca* addressOf = v->AddressInstructions[0];
            auto* call = dynamic_cast<IL::Call*>(addressOf->Parent);
            if (call != nullptr && addressOf->ChildIndex == 0 &&
                call->Method != nullptr && !call->Method->IsStatic()) {
                // used as this pointer for a method call
                // this is OK iff the call is not within a nested loop
                for (IL::ILInstruction* node = call->Parent; node != nullptr;
                     node = node->Parent) {
                    if (node == loop)
                        return true;
                    else if (dynamic_cast<IL::BlockContainer*>(node) != nullptr)
                        break;
                }
            }
        }
        return false;
    }

    // The C# `bool VariableCanBeUsedAsForeachLocal(IL.ILVariable itemVar,
    // Statement loop)` (line ~322): the checks deciding whether the loop's
    // item variable can become the foreach designation.
    bool VariableCanBeUsedAsForeachLocal(IL::ILVariable* itemVar,
                                         Syntax::Statement* loop) {
        if (itemVar == nullptr ||
            !(itemVar->Kind == IL::VariableKind::Local ||
              itemVar->Kind == IL::VariableKind::StackSlot)) {
            // only locals/temporaries can be converted into foreach loop variable
            return false;
        }

        IL::BlockContainer* blockContainer = CS::GetBlockContainerAnnotation(*loop);

        if (!itemVar->IsSingleDefinition()) {
            // foreach variable cannot be assigned to.
            // As a special case, we accept taking the address for a method
            // call, but only if the call is the only use, so that any
            // mutation by the call cannot be observed.
            if (!AddressUsedForSingleCall(itemVar, blockContainer)) {
                return false;
            }
        }

        if (itemVar->CaptureScope != nullptr &&
            itemVar->CaptureScope != blockContainer) {
            // captured variables cannot be declared in the loop unless the
            // loop is their capture scope
            return false;
        }

        Syntax::AstNode* declPoint = declareVariables->GetDeclarationPoint(itemVar);
        // The C# `declPoint.Ancestors.Contains(loop)`.
        bool insideLoop = false;
        for (Syntax::AstNode* ancestor : declPoint->Ancestors()) {
            if (ancestor == loop) {
                insideLoop = true;
                break;
            }
        }
        return insideLoop && !declareVariables->WasMerged(itemVar);
    }

    // The C# `Statement? TransformForeachOnArray(ForStatement forStatement)`
    // (line ~355): the index loop over an array (or string) becomes a
    // foreach.
    Syntax::Statement* TransformForeachOnArray(Syntax::ForStatement* forStatement) {
        if (!context->DecompileRun->Settings().ForEachStatement())
            return nullptr;
        PatternMatching::Match m =
            Syntax::MatchNode(GetForeachOnArrayPattern().pattern, forStatement);
        if (!m.Success())
            return nullptr;
        std::vector<Syntax::IdentifierExpression*> itemCaptures =
            m.Get<Syntax::IdentifierExpression>("itemVariable");
        assert(itemCaptures.size() == 1);
        std::vector<Syntax::IdentifierExpression*> indexCaptures =
            m.Get<Syntax::IdentifierExpression>("indexVariable");
        assert(indexCaptures.size() == 1);
        std::vector<Syntax::IdentifierExpression*> arrayCaptures =
            m.Get<Syntax::IdentifierExpression>("arrayVariable");
        assert(arrayCaptures.size() == 1);
        IL::ILVariable* itemVariable = CS::GetILVariable(*itemCaptures.front());
        IL::ILVariable* indexVariable = CS::GetILVariable(*indexCaptures.front());
        IL::ILVariable* arrayVariable = CS::GetILVariable(*arrayCaptures.front());
        if (itemVariable == nullptr || indexVariable == nullptr ||
            arrayVariable == nullptr)
            return nullptr;
        if (arrayVariable->Type->Kind() != TS::TypeKind::Array &&
            !TS::IsKnownType(*arrayVariable->Type, TS::KnownTypeCode::String))
            return nullptr;
        if (!VariableCanBeUsedAsForeachLocal(itemVariable, forStatement))
            return nullptr;
        if (indexVariable->StoreCount != 2 || indexVariable->LoadCount != 3 ||
            indexVariable->AddressCount != 0)
            return nullptr;
        context->StepOnce("Introduce foreach over array", forStatement);
        auto* body = new Syntax::BlockStatement();
        for (Syntax::Statement* statement : m.Get<Syntax::Statement>("statements"))
            body->Statements().Add(Syntax::Detach(statement));
        auto* foreachStmt = new Syntax::ForeachStatement();
        foreachStmt->VariableType(
            context->DecompileRun->Settings().AnonymousTypes() &&
                    TS::ContainsAnonymousType(*itemVariable->Type)
                ? static_cast<Syntax::AstType*>(new Syntax::SimpleType("var"))
                : context->TypeSystemAstBuilder->ConvertType(
                      *itemVariable->Type));
        auto* designation = new Syntax::SingleVariableDesignation();
        designation->Identifier(itemVariable->Name);
        foreachStmt->VariableDesignation(designation);
        foreachStmt->InExpression(Syntax::Detach(arrayCaptures.front()));
        foreachStmt->EmbeddedStatement(body);
        CS::CopyAnnotationsFrom(foreachStmt, *forStatement);
        itemVariable->Kind = IL::VariableKind::ForeachLocal;
        // Add the variable annotation for highlighting (TokenTextWriter
        // expects it directly on the ForeachStatement).
        // A non-owning alias over the caller-owned variable (the IL function
        // tree owns it -- the no-op-deleter convention).
        IL::ILVariablePtr itemVariableHandle(itemVariable, [](IL::ILVariable*) {});
        foreachStmt->VariableDesignation()->AddAnnotation(
            std::make_shared<CS::ILVariableResolveResult>(itemVariableHandle,
                                                           itemVariable->Type));
        // TODO : add ForeachAnnotation
        forStatement->ReplaceWith(foreachStmt);
        // The C# `context.EndStep(foreachStmt)` (the step-group close) folds
        // onto the single step hook.
        return foreachStmt;
    }

    // The C# `bool MatchLowerBound(int indexNum, out IL.ILVariable? index,
    // IL.ILVariable collection, Statement statement)` (line ~575): matches
    // `$index = $collection.GetLowerBound($indexNum);` and checks the index
    // literal.
    bool MatchLowerBound(int indexNum, IL::ILVariable** index,
                         IL::ILVariable* collection, Syntax::Statement* statement) {
        *index = nullptr;
        PatternMatching::Match m = Syntax::MatchNode(
            GetForeachMultiDimPatterns().variableAssignLowerBoundPattern, statement);
        if (!m.Success())
            return false;
        std::vector<Syntax::PrimitiveExpression*> indexCaptures =
            m.Get<Syntax::PrimitiveExpression>("index");
        // The C# `.Single()` (exactly one capture by construction).
        assert(indexCaptures.size() == 1);
        // The C# `int.TryParse(value.ToString(), out int i)`: the compiler
        // emits the dimension as an int literal; anything else fails the
        // parse.
        const std::int32_t* i =
            std::get_if<std::int32_t>(&indexCaptures.front()->Value());
        if (i == nullptr || indexNum != *i)
            return false;
        std::vector<Syntax::IdentifierExpression*> variableCaptures =
            m.Get<Syntax::IdentifierExpression>("variable");
        assert(variableCaptures.size() == 1);
        *index = CS::GetILVariable(*variableCaptures.front());
        std::vector<Syntax::IdentifierExpression*> collectionCaptures =
            m.Get<Syntax::IdentifierExpression>("collection");
        assert(collectionCaptures.size() == 1);
        return CS::GetILVariable(*collectionCaptures.front()) == collection;
    }

    // The C# `bool MatchForeachOnMultiDimArray(IL.ILVariable[] upperBounds,
    // IL.ILVariable collection, Statement firstInitializerStatement,
    // out IdentifierExpression foreachVariable, out List<Statement> statements,
    // out IL.ILVariable[] lowerBounds)` (line ~593): walks the nested
    // per-dimension lower-bound assignments and for loops, ending at the
    // element assignment. The returned `statements` are captured by the
    // innermost for pattern match.
    bool MatchForeachOnMultiDimArray(
        std::vector<IL::ILVariable*>& upperBounds, IL::ILVariable* collection,
        Syntax::Statement* firstInitializerStatement,
        Syntax::IdentifierExpression** foreachVariable,
        std::vector<Syntax::Statement*>* statements,
        std::vector<IL::ILVariable*>& lowerBounds) {
        int i = 0;
        *foreachVariable = nullptr;
        PatternMatching::Match m;
        Syntax::Statement* stmt = firstInitializerStatement;
        IL::ILVariable* indexVariable = nullptr;
        while (i < (int)upperBounds.size() &&
               MatchLowerBound(i, &indexVariable, collection, stmt)) {
            m = Syntax::MatchNode(GetForeachMultiDimPatterns().forOnArrayMultiDimPattern,
                                  Syntax::GetNextStatement(stmt));
            if (!m.Success())
                return false;
            std::vector<Syntax::IdentifierExpression*> upperBoundCaptures =
                m.Get<Syntax::IdentifierExpression>("upperBoundVariable");
            // The C# `.Single()` (exactly one capture by construction).
            assert(upperBoundCaptures.size() == 1);
            IL::ILVariable* upperBound =
                CS::GetILVariable(*upperBoundCaptures.front());
            if (upperBounds[i] != upperBound)
                return false;
            std::vector<Syntax::Statement*> lowerBoundAssignCaptures =
                m.Get<Syntax::Statement>("lowerBoundAssign");
            assert(lowerBoundAssignCaptures.size() == 1);
            stmt = lowerBoundAssignCaptures.front();
            lowerBounds[i] = indexVariable;
            i++;
        }
        if (collection->Type->Kind() != TS::TypeKind::Array)
            return false;
        PatternMatching::Match m2 = Syntax::MatchNode(
            GetForeachMultiDimPatterns().foreachVariableOnMultArrayAssignPattern,
            stmt);
        if (!m2.Success())
            return false;
        std::vector<Syntax::IdentifierExpression*> collectionCaptures =
            m2.Get<Syntax::IdentifierExpression>("collection");
        assert(collectionCaptures.size() == 1);
        if (CS::GetILVariable(*collectionCaptures.front()) != collection)
            return false;
        std::vector<Syntax::IdentifierExpression*> variableCaptures =
            m2.Get<Syntax::IdentifierExpression>("variable");
        assert(variableCaptures.size() == 1);
        *foreachVariable = variableCaptures.front();
        *statements = m.Get<Syntax::Statement>("statements");
        return true;
    }

    // The C# `Statement? TransformForeachOnMultiDimArray(ExpressionStatement
    // expressionStatement)` (line ~626): the
    // `var ub_i = arr.GetUpperBound(i);` chain followed by the nested
    // lower-bound/for-loop structure becomes a single foreach over the
    // multi-dimensional array.
    Syntax::Statement* TransformForeachOnMultiDimArray(
        Syntax::ExpressionStatement* expressionStatement) {
        if (!context->DecompileRun->Settings().ForEachStatement())
            return nullptr;
        PatternMatching::Match m;
        Syntax::Statement* stmt = expressionStatement;
        IL::ILVariable* collection = nullptr;
        std::vector<IL::ILVariable*> upperBounds;
        bool haveUpperBounds = false;
        std::vector<Syntax::Statement*> statementsToDelete;
        int i = 0;
        do {
            m = Syntax::MatchNode(
                GetForeachMultiDimPatterns().variableAssignUpperBoundPattern, stmt);
            if (!m.Success())
                break;
            if (!haveUpperBounds) {
                std::vector<Syntax::IdentifierExpression*> collectionCaptures =
                    m.Get<Syntax::IdentifierExpression>("collection");
                // The C# `.Single()` (exactly one capture by construction).
                assert(collectionCaptures.size() == 1);
                collection = CS::GetILVariable(*collectionCaptures.front());
                auto* arrayType =
                    dynamic_cast<TS::ArrayType*>(collection->Type.get());
                if (arrayType == nullptr)
                    break;
                upperBounds.assign(arrayType->Rank(), nullptr);
                haveUpperBounds = true;
            } else {
                statementsToDelete.push_back(stmt);
            }
            std::vector<Syntax::IdentifierExpression*> nextCollectionCaptures =
                m.Get<Syntax::IdentifierExpression>("collection");
            assert(nextCollectionCaptures.size() == 1);
            IL::ILVariable* nextCollection =
                CS::GetILVariable(*nextCollectionCaptures.front());
            if (nextCollection != collection)
                break;
            // The C# `int.TryParse(...)`: the dimension index must be the
            // literal `i` (the compiler emits int literals).
            std::vector<Syntax::PrimitiveExpression*> indexCaptures =
                m.Get<Syntax::PrimitiveExpression>("index");
            assert(indexCaptures.size() == 1);
            const std::int32_t* indexValue =
                std::get_if<std::int32_t>(&indexCaptures.front()->Value());
            if (indexValue == nullptr || *indexValue != i)
                break;
            std::vector<Syntax::IdentifierExpression*> variableCaptures =
                m.Get<Syntax::IdentifierExpression>("variable");
            assert(variableCaptures.size() == 1);
            upperBounds[i] = CS::GetILVariable(*variableCaptures.front());
            stmt = Syntax::GetNextStatement(stmt);
            i++;
        } while (stmt != nullptr && haveUpperBounds && i < (int)upperBounds.size());
        // The C# `upperBounds?.LastOrDefault() == null` (an unfilled
        // dimension) -- `m` is the last SUCCESSFUL upper-bound match only
        // when the loop ended by the dimension count; a pattern break
        // leaves the last dimension unfilled in that case, and this check
        // rejects both.
        if (!haveUpperBounds || upperBounds.empty() ||
            upperBounds.back() == nullptr)
            return nullptr;
        if (collection == nullptr || stmt == nullptr)
            return nullptr;
        Syntax::IdentifierExpression* foreachVariable = nullptr;
        std::vector<Syntax::Statement*> statements;
        std::vector<IL::ILVariable*> lowerBounds(upperBounds.size(), nullptr);
        if (!MatchForeachOnMultiDimArray(upperBounds, collection, stmt,
                                         &foreachVariable, &statements,
                                         lowerBounds))
            return nullptr;
        // `stmt` is the first lower-bound assignment; its next statement is
        // the outer for loop (both are absorbed by the foreach).
        statementsToDelete.push_back(stmt);
        // The matched multi-dimensional foreach pattern guarantees a
        // statement after `stmt` (the C# null-forgiving `!`; the deletion
        // loop below guards null defensively).
        statementsToDelete.push_back(Syntax::GetNextStatement(stmt));
        IL::ILVariable* itemVariable = CS::GetILVariable(*foreachVariable);
        if (itemVariable == nullptr || !itemVariable->IsSingleDefinition())
            return nullptr;
        if (itemVariable->Kind != IL::VariableKind::Local &&
            itemVariable->Kind != IL::VariableKind::StackSlot)
            return nullptr;
        for (IL::ILVariable* upperBound : upperBounds) {
            if (!upperBound->IsSingleDefinition() || upperBound->LoadCount != 1)
                return nullptr;
        }
        for (IL::ILVariable* lowerBound : lowerBounds) {
            if (lowerBound->StoreCount != 2 || lowerBound->LoadCount != 3 ||
                lowerBound->AddressCount != 0)
                return nullptr;
        }
        context->StepOnce("Introduce foreach over multidimensional array",
                          expressionStatement);
        // Move the loop body statements out first: the inner statements are
        // deleted with the outer for loop, so they must be detached before
        // the outer loop itself is removed.
        auto* body = new Syntax::BlockStatement();
        for (Syntax::Statement* statement : statements)
            body->Statements().Add(Syntax::Detach(statement));
        auto* foreachStmt = new Syntax::ForeachStatement();
        foreachStmt->VariableType(
            context->DecompileRun->Settings().AnonymousTypes() &&
                    TS::ContainsAnonymousType(*itemVariable->Type)
                ? static_cast<Syntax::AstType*>(new Syntax::SimpleType("var"))
                : context->TypeSystemAstBuilder->ConvertType(
                      *itemVariable->Type));
        auto* designation = new Syntax::SingleVariableDesignation();
        designation->Identifier(itemVariable->Name);
        foreachStmt->VariableDesignation(designation);
        // The C# reads the collection identifier from `m`, the LAST
        // upper-bound match.
        std::vector<Syntax::IdentifierExpression*> inExpressionCaptures =
            m.Get<Syntax::IdentifierExpression>("collection");
        assert(inExpressionCaptures.size() == 1);
        foreachStmt->InExpression(Syntax::Detach(inExpressionCaptures.front()));
        foreachStmt->EmbeddedStatement(body);
        itemVariable->Kind = IL::VariableKind::ForeachLocal;
        // A non-owning alias over the caller-owned variable (the IL
        // function tree owns it -- the no-op-deleter convention).
        IL::ILVariablePtr itemVariableHandle(itemVariable, [](IL::ILVariable*) {});
        foreachStmt->VariableDesignation()->AddAnnotation(
            std::make_shared<CS::ILVariableResolveResult>(itemVariableHandle,
                                                           itemVariable->Type));
        for (Syntax::Statement* statement : statementsToDelete) {
            if (statement != nullptr)
                Syntax::Detach(statement);
        }
        expressionStatement->ReplaceWith(foreachStmt);
        return foreachStmt;
    }

    // The C# `Statement? TransformForeachOnInlineArray(ForStatement
    // forStatement)` (line ~427): reconstructs a foreach over an inline array
    // from the lowered for loop. The rewrite is only sound because the loop
    // bound equals the inline array length, which proves the index is always
    // in range: InlineArrayElementRef is the compiler's unchecked element
    // accessor, whereas the C# inline-array indexer is bounds-checked.
    Syntax::Statement* TransformForeachOnInlineArray(
        Syntax::ForStatement* forStatement) {
        if (!context->DecompileRun->Settings().ForEachStatement() ||
            !context->DecompileRun->Settings().InlineArrays())
            return nullptr;
        PatternMatching::Match m =
            Syntax::MatchNode(GetForeachOnInlineArrayPattern().pattern,
                              forStatement);
        if (!m.Success())
            return nullptr;
        std::vector<Syntax::IdentifierExpression*> itemCaptures =
            m.Get<Syntax::IdentifierExpression>("itemVariable");
        // The C# `.Single()` (exactly one capture by construction).
        assert(itemCaptures.size() == 1);
        std::vector<Syntax::IdentifierExpression*> indexCaptures =
            m.Get<Syntax::IdentifierExpression>("indexVariable");
        assert(indexCaptures.size() == 1);
        IL::ILVariable* itemVariable = CS::GetILVariable(*itemCaptures.front());
        IL::ILVariable* indexVariable = CS::GetILVariable(*indexCaptures.front());
        if (itemVariable == nullptr || indexVariable == nullptr)
            return nullptr;

        // The loop body must start with
        // `item = InlineArrayElementRef(ref buffer, index)`.
        std::vector<Syntax::AstNode*> elementAccessCaptures =
            m.Get<Syntax::AstNode>("elementAccess");
        assert(elementAccessCaptures.size() == 1);
        auto* elementAccess =
            dynamic_cast<Syntax::InvocationExpression*>(elementAccessCaptures.front());
        if (elementAccess == nullptr)
            return nullptr;
        const TS::ISymbol* symbol = CS::GetSymbol(*elementAccess);
        auto* helper = dynamic_cast<const TS::IMethod*>(symbol);
        if (helper == nullptr)
            return nullptr;
        // The C# checks `DeclaringType.FullName ==
        // "<PrivateImplementationDetails>"` -- the port's IType surface has no
        // FullName, and the type is top-level with an empty namespace, so
        // Name() is the equivalent read (the InlineArrayTransform
        // MatchInlineArrayHelper precedent).
        TS::ITypePtr declaringType = helper->DeclaringType();
        if (declaringType == nullptr ||
            declaringType->Name() != "<PrivateImplementationDetails>")
            return nullptr;
        if (helper->Name() != "InlineArrayElementRef" &&
            helper->Name() != "InlineArrayElementRefReadOnly")
            return nullptr;
        if (elementAccess->Arguments().Count() != 2)
            return nullptr;
        // arg0: `ref buffer`, arg1: the loop index.
        auto* bufferDirection =
            dynamic_cast<Syntax::DirectionExpression*>(
                elementAccess->Arguments().At(0));
        if (bufferDirection == nullptr)
            return nullptr;
        auto* bufferIdentifier =
            dynamic_cast<Syntax::IdentifierExpression*>(
                bufferDirection->Expression());
        if (bufferIdentifier == nullptr)
            return nullptr;
        IL::ILVariable* bufferVariable = CS::GetILVariable(*bufferIdentifier);
        if (bufferVariable == nullptr)
            return nullptr;
        auto* indexIdentifier = dynamic_cast<Syntax::IdentifierExpression*>(
            elementAccess->Arguments().At(1));
        if (indexIdentifier == nullptr ||
            CS::GetILVariable(*indexIdentifier) != indexVariable)
            return nullptr;

        // Soundness: the loop counts 0..length-1 over exactly the inline
        // array's length, so the index is provably in range. Any other bound
        // (or a non-inline-array buffer) is rejected.
        std::optional<int> arrayLength =
            TS::GetInlineArrayLength(*bufferVariable->Type);
        if (!arrayLength.has_value())
            return nullptr;
        std::vector<Syntax::PrimitiveExpression*> lengthCaptures =
            m.Get<Syntax::PrimitiveExpression>("length");
        assert(lengthCaptures.size() == 1);
        const std::int32_t* loopBound =
            std::get_if<std::int32_t>(&lengthCaptures.front()->Value());
        if (loopBound == nullptr || *loopBound != *arrayLength)
            return nullptr;

        if (!VariableCanBeUsedAsForeachLocal(itemVariable, forStatement))
            return nullptr;
        // The index is a pure counter: stored at init + increment, loaded at
        // the condition, the increment, and the element access; never
        // captured by address.
        if (indexVariable->StoreCount != 2 || indexVariable->LoadCount != 3 ||
            indexVariable->AddressCount != 0)
            return nullptr;

        context->StepOnce("Introduce foreach over inline array", forStatement);
        // Take the buffer reference for the `in` expression before dropping
        // the element access.
        Syntax::Expression* inExpression = Syntax::Detach(bufferIdentifier);
        // Reuse the loop body (preserving its annotations) after removing
        // its leading `item = InlineArrayElementRef(ref buffer, i)`
        // statement.
        auto* body =
            dynamic_cast<Syntax::BlockStatement*>(
                forStatement->EmbeddedStatement());
        // The pattern guarantees a BlockStatement body.
        assert(body != nullptr);
        body->Statements().At(0)->Remove();
        auto* foreachStmt = new Syntax::ForeachStatement();
        foreachStmt->VariableType(
            context->DecompileRun->Settings().AnonymousTypes() &&
                    TS::ContainsAnonymousType(*itemVariable->Type)
                ? static_cast<Syntax::AstType*>(new Syntax::SimpleType("var"))
                : context->TypeSystemAstBuilder->ConvertType(
                      *itemVariable->Type));
        auto* designation = new Syntax::SingleVariableDesignation();
        designation->Identifier(itemVariable->Name);
        foreachStmt->VariableDesignation(designation);
        foreachStmt->InExpression(inExpression);
        foreachStmt->EmbeddedStatement(Syntax::Detach(body));
        CS::CopyAnnotationsFrom(foreachStmt, *forStatement);
        itemVariable->Kind = IL::VariableKind::ForeachLocal;
        // A non-owning alias over the caller-owned variable (the IL function
        // tree owns it -- the no-op-deleter convention).
        IL::ILVariablePtr itemVariableHandle(itemVariable, [](IL::ILVariable*) {});
        foreachStmt->VariableDesignation()->AddAnnotation(
            std::make_shared<CS::ILVariableResolveResult>(itemVariableHandle,
                                                           itemVariable->Type));
        forStatement->ReplaceWith(foreachStmt);
        // The C# `context.EndStep(foreachStmt)` (the step-group close) folds
        // onto the single step hook.
        return foreachStmt;
    }

    // The C# `DestructorDeclaration? TransformDestructor(MethodDeclaration
    // methodDef)` (line ~949): the compiler-generated `protected override
    // void Finalize() { try { body } finally { base.Finalize(); } }` becomes
    // the destructor `~T() { body }`.
    Syntax::DestructorDeclaration* TransformDestructor(
        Syntax::MethodDeclaration* methodDef) {
        PatternMatching::Match m =
            Syntax::MatchNode(GetDestructorPatterns().destructorPattern, methodDef);
        if (!m.Success())
            return nullptr;
        context->StepOnce("Convert Finalize method to destructor", methodDef);
        auto* dd = new Syntax::DestructorDeclaration();
        methodDef->Attributes().MoveTo(dd->Attributes());
        CS::CopyAnnotationsFrom(dd, *methodDef);
        dd->Modifiers(methodDef->Modifiers() &
                      ~(Syntax::Modifiers::Protected | Syntax::Modifiers::Override));
        std::vector<Syntax::BlockStatement*> bodyCaptures =
            m.Get<Syntax::BlockStatement>("body");
        // The C# `.Single()` (exactly one capture by construction).
        assert(bodyCaptures.size() == 1);
        dd->Body(Syntax::Detach(bodyCaptures.front()));
        // A destructor only appears inside a type declaration, so the
        // context tracker has an enclosing type at this point (the C#
        // null-forgiving `!`).
        assert(currentTypeDefinition != nullptr);
        dd->Name(currentTypeDefinition->Name());
        methodDef->ReplaceWith(dd);
        // The C# `context.EndStep(dd)` (the step-group close) folds onto the
        // single step hook.
        return dd;
    }

    // The C# `DestructorDeclaration? TransformDestructorBody(DestructorDeclaration
    // dtorDef)` (line ~970): a destructor whose body is still the lowered
    // try-finally shape is unwrapped to just the body.
    Syntax::DestructorDeclaration* TransformDestructorBody(
        Syntax::DestructorDeclaration* dtorDef) {
        PatternMatching::Match m = Syntax::MatchNode(
            GetDestructorPatterns().destructorBodyPattern, dtorDef->Body());
        if (!m.Success())
            return nullptr;
        context->StepOnce("Simplify destructor body", dtorDef);
        std::vector<Syntax::BlockStatement*> bodyCaptures =
            m.Get<Syntax::BlockStatement>("body");
        assert(bodyCaptures.size() == 1);
        dtorDef->Body(Syntax::Detach(bodyCaptures.front()));
        return dtorDef;
    }

    // The C# `bool CanTransformToAutomaticProperty(IProperty property, bool
    // accessorsMustBeCompilerGenerated)` (line ~742).
    bool CanTransformToAutomaticProperty(const TS::IProperty* property,
                                          bool accessorsMustBeCompilerGenerated) {
        if (!property->CanGet())
            return false;
        // The C# `property.Getter.IsCompilerGenerated()` (NRExtensions -- the
        // direct HasAttribute check).
        if (accessorsMustBeCompilerGenerated &&
            !property->Getter()->HasAttribute(TS::KnownAttribute::CompilerGenerated))
            return false;
        const TS::IMethod* setter = property->Setter();
        if (setter != nullptr) {
            if (accessorsMustBeCompilerGenerated &&
                !setter->HasAttribute(TS::KnownAttribute::CompilerGenerated))
                return false;
            if (TS::HasReadonlyModifier(*setter))
                return false;
        }
        return true;
    }

    // The C# `static void RemoveCompilerGeneratedAttribute(
    // AstNodeCollection<AttributeSection> attributeSections, params string[]
    // attributesToRemove)` (line ~817): strips the named attribute types.
    void RemoveCompilerGeneratedAttribute(
        Syntax::AstNodeCollectionT<Syntax::AttributeSection>& attributeSections) {
        for (int i = 0; i < attributeSections.Count(); i++) {
            Syntax::AttributeSection* section = attributeSections.At(i);
            for (int j = 0; j < section->Attributes().Count(); j++) {
                Syntax::Attribute* attr = section->Attributes().At(j);
                const TS::ISymbol* symbol = CS::GetSymbol(*attr->Type());
                auto* type = dynamic_cast<const TS::IType*>(symbol);
                // The C# `tr.FullName` -- the port's IType has no FullName;
                // compose the top-level form (the inline-array PID precedent).
                if (type != nullptr) {
                    std::string fullName = type->Namespace().empty()
                                               ? type->Name()
                                               : type->Namespace() + "." + type->Name();
                    if (fullName ==
                            "System.Runtime.CompilerServices."
                            "CompilerGeneratedAttribute" ||
                        fullName == "System.Diagnostics.DebuggerBrowsableAttribute") {
                        attr->Remove();
                        j--;
                    }
                }
            }
            if (section->Attributes().Count() == 0)
                section->Remove();
        }
    }

    // The C# `PropertyDeclaration? TransformAutomaticProperty(PropertyDeclaration
    // propertyDeclaration)` (line ~757): `int Count { get { return field; } set {
    // field = value; } }` over its compiler-generated backing field becomes the
    // auto-property `int Count { get; set; }`, absorbing the backing field
    // declaration. The property instance is not changed in identity, so the
    // visitor continues as usual -- return null (the port falls through to
    // the base visit).
    void TransformAutomaticProperty(Syntax::PropertyDeclaration* propertyDeclaration) {
        if (!context->DecompileRun->Settings().AutomaticProperties())
            return;
        const TS::ISymbol* symbol = CS::GetSymbol(*propertyDeclaration);
        auto* property = dynamic_cast<const TS::IProperty*>(symbol);
        if (property == nullptr)
            return;
        // The C# `accessorsMustBeCompilerGenerated` -- false when the declaring
        // type carries a VB-style compiler-generated `_Name` field.
        bool accessorsMustBeCompilerGenerated = true;
        const TS::ITypeDefinition* declaringType = property->DeclaringTypeDefinition();
        if (declaringType != nullptr) {
            for (const TS::IField* f : declaringType->GetFields()) {
                if (f->Name() == "_" + property->Name() &&
                    f->HasAttribute(TS::KnownAttribute::CompilerGenerated)) {
                    accessorsMustBeCompilerGenerated = false;
                    break;
                }
            }
        }
        if (!CanTransformToAutomaticProperty(property, accessorsMustBeCompilerGenerated))
            return;
        const TS::IField* field = nullptr;
        PatternMatching::Match m = Syntax::MatchNode(
            GetAutomaticPropertyPatterns().automaticPropertyPattern,
            propertyDeclaration);
        if (m.Success()) {
            std::vector<Syntax::AstNode*> fieldReferenceCaptures =
                m.Get<Syntax::AstNode>("fieldReference");
            // The C# `.Single()` (exactly one capture by construction).
            assert(fieldReferenceCaptures.size() == 1);
            field = dynamic_cast<const TS::IField*>(
                CS::GetSymbol(*fieldReferenceCaptures.front()));
        } else {
            PatternMatching::Match m2 = Syntax::MatchNode(
                GetAutomaticPropertyPatterns().automaticReadonlyPropertyPattern,
                propertyDeclaration);
            if (m2.Success()) {
                std::vector<Syntax::AstNode*> fieldReferenceCaptures =
                    m2.Get<Syntax::AstNode>("fieldReference");
                assert(fieldReferenceCaptures.size() == 1);
                field = dynamic_cast<const TS::IField*>(
                    CS::GetSymbol(*fieldReferenceCaptures.front()));
            }
        }
        std::string propertyName;
        if (field == nullptr ||
            !NameCouldBeBackingFieldOfAutomaticProperty(field->Name(), &propertyName))
            return;
        if ((propertyDeclaration->Setter() != nullptr &&
             propertyDeclaration->Setter()->HasModifier(Syntax::Modifiers::Readonly)) ||
            (propertyDeclaration->HasModifier(Syntax::Modifiers::Readonly) &&
             propertyDeclaration->Setter() != nullptr))
            return;
        if (field->HasAttribute(TS::KnownAttribute::CompilerGenerated) &&
            field->DeclaringTypeDefinition() == property->DeclaringTypeDefinition()) {
            context->StepOnce("Convert property to auto-property", propertyDeclaration);
            // Clearing the accessor body turns it into an auto-property
            // accessor.
            Syntax::Accessor* getter = propertyDeclaration->Getter();
            Syntax::Accessor* setter = propertyDeclaration->Setter();
            if (getter != nullptr) {
                RemoveCompilerGeneratedAttribute(getter->Attributes());
                getter->Body(nullptr);
            }
            if (setter != nullptr) {
                RemoveCompilerGeneratedAttribute(setter->Attributes());
                setter->Body(nullptr);
            }
            propertyDeclaration->Modifiers(propertyDeclaration->Modifiers() &
                                           ~Syntax::Modifiers::Readonly);
            if (getter != nullptr)
                getter->Modifiers(getter->Modifiers() &
                                  ~Syntax::Modifiers::Readonly);

            Syntax::AstNode* parent = propertyDeclaration->Parent();
            Syntax::FieldDeclaration* fieldDecl = nullptr;
            if (parent != nullptr) {
                for (Syntax::AstNode* child : parent->Children()) {
                    auto* fd = dynamic_cast<Syntax::FieldDeclaration*>(child);
                    if (fd == nullptr)
                        continue;
                    if (dynamic_cast<const TS::IField*>(CS::GetSymbol(*fd)) == field) {
                        fieldDecl = fd;
                        break;
                    }
                }
            }
            if (fieldDecl != nullptr) {
                fieldDecl->Remove();
                // Add C# 7.3 attributes on backing field:
                CS::CSharpDecompiler::RemoveAttribute(
                    *fieldDecl, TS::KnownAttribute::CompilerGenerated);
                CS::CSharpDecompiler::RemoveAttribute(
                    *fieldDecl, TS::KnownAttribute::DebuggerBrowsable);
                for (int i = 0; i < fieldDecl->Attributes().Count(); i++) {
                    Syntax::AttributeSection* section = fieldDecl->Attributes().At(i);
                    section->AttributeTarget("field");
                    propertyDeclaration->Attributes().Add(Syntax::Detach(section));
                    i--;
                }
            }
        }
        // Since the property instance is not changed, we can continue in the
        // visitor as usual, so return null.
    }

    // The C# `internal static bool IsBackingFieldOfAutomaticProperty(IField
    // field, out IProperty? property)` (line ~853): whether the field is the
    // compiler-generated backing field of a same-named property on its
    // declaring type.
    static bool IsBackingFieldOfAutomaticProperty(const TS::IField* field,
                                                   const TS::IProperty** property) {
        *property = nullptr;
        std::string propertyName;
        if (!NameCouldBeBackingFieldOfAutomaticProperty(field->Name(), &propertyName))
            return false;
        // The C# `field.IsCompilerGenerated()` (the direct HasAttribute).
        if (!field->HasAttribute(TS::KnownAttribute::CompilerGenerated))
            return false;
        const TS::ITypeDefinition* declaringType = field->DeclaringTypeDefinition();
        if (declaringType != nullptr) {
            for (const TS::IProperty* p : declaringType->GetProperties(
                     /*filter=*/nullptr,
                     TS::GetMemberOptions::IgnoreInheritedMembers)) {
                if (p->Name() == propertyName) {
                    *property = p;
                    break;
                }
            }
        }
        return *property != nullptr;
    }

    // The C# `Identifier? ReplaceBackingFieldUsage(Identifier identifier)`
    // (line ~894): a backing-field identifier becomes the property name,
    // with the parent's resolve result re-pointed at the property.
    Syntax::Identifier* ReplaceBackingFieldUsage(Syntax::Identifier* identifier) {
        std::string propertyName;
        if (!NameCouldBeBackingFieldOfAutomaticProperty(identifier->Name(),
                                                        &propertyName))
            return nullptr;
        Syntax::AstNode* parent = identifier->Parent();
        if (parent == nullptr)
            return nullptr;
        const auto* mrr = parent->Annotation<Sem::MemberResolveResult>();
        const TS::IField* field =
            mrr != nullptr
                ? dynamic_cast<const TS::IField*>(mrr->Member())
                : nullptr;
        const TS::IProperty* property = nullptr;
        if (field != nullptr &&
            IsBackingFieldOfAutomaticProperty(field, &property) &&
            CanTransformToAutomaticProperty(
                property,
                !(field->HasAttribute(TS::KnownAttribute::CompilerGenerated) &&
                  field->Name() == "_" + property->Name())) &&
            (currentMethod == nullptr ||
             currentMethod->AccessorOwner() !=
                 static_cast<const TS::IMember*>(property))) {
            if (!property->CanSet() &&
                !context->DecompileRun->Settings().GetterOnlyAutomaticProperties())
                return nullptr;
            context->StepOnce("Replace backing field use with property", identifier);
            parent->RemoveAnnotations<Sem::MemberResolveResult>();
            // The C# re-uses mrr.TargetResult; the port re-attaches the
            // owning shared handle.
            parent->AddAnnotation(std::make_shared<Sem::MemberResolveResult>(
                mrr->SharedTargetResult(), property));
            return Syntax::Identifier::Create(property->Name());
        }
        return nullptr;
    }

    // The C# `TryCatchStatement? TransformTryCatchFinally(TryCatchStatement
    // tryFinally)` (line ~996): simplify nested 'try { try {} catch {} } finally
    // {}'. Runs after the using/lock transformations in the C# pipeline.
    // The tryFinally instance is not changed in identity, so the visitor
    // continues as usual -- return null (the port falls through to the base
    // visit).
    void TransformTryCatchFinally(Syntax::TryCatchStatement* tryFinally) {
        if (!Syntax::MatchNode(GetTryCatchFinallyPatterns().tryCatchFinallyPattern,
                               tryFinally)
                 .Success())
            return;
        context->StepOnce("Merge nested try-catch-finally", tryFinally);
        // The C# `.Single()` on the try block's statements (the pattern
        // guarantees exactly one).
        Syntax::BlockStatement* tryBlock = tryFinally->TryBlock();
        assert(tryBlock->Statements().Count() == 1);
        auto* tryCatch =
            dynamic_cast<Syntax::TryCatchStatement*>(tryBlock->Statements().At(0));
        assert(tryCatch != nullptr);
        tryFinally->TryBlock(Syntax::Detach(tryCatch->TryBlock()));
        tryCatch->CatchClauses().MoveTo(tryFinally->CatchClauses());
    }

    // The C# `public override AstNode VisitFixedStatement(FixedStatement
    // fixedStatement)` (line ~1102): `fixed (var p = &target.GetPinnableReference())
    // { }` over a value-type target becomes `fixed (var p = target) { }`.
    void VisitFixedStatement(Syntax::FixedStatement* fixedStatement) override {
        if (context->DecompileRun->Settings().PatternBasedFixedStatement()) {
            for (int i = 0; i < fixedStatement->Variables().Count(); i++) {
                Syntax::VariableInitializer* v = fixedStatement->Variables().At(i);
                PatternMatching::Match m = Syntax::MatchNode(
                    GetPatternBasedFixedPatterns().addressOfPinnableReference,
                    v->Initializer());
                if (m.Success()) {
                    std::vector<Syntax::Expression*> targetCaptures =
                        m.Get<Syntax::Expression>("target");
                    // The C# `.Single()` (exactly one capture by construction).
                    assert(targetCaptures.size() == 1);
                    Syntax::Expression* target = targetCaptures.front();
                    // The C# `target.GetResolveResult().Type.IsReferenceType ==
                    // false` -- the port's IsReferenceType is an optional<bool>
                    // (an unknown/null is not the C# false).
                    if (CS::GetResolveResult(*target)->Type().IsReferenceType() ==
                        std::optional<bool>(false)) {
                        context->StepOnce("Use pattern-based fixed statement",
                                          fixedStatement);
                        v->Initializer(Syntax::Detach(target));
                    }
                }
            }
        }
        Syntax::DepthFirstAstVisitor::VisitFixedStatement(fixedStatement);
    }

    // The C# `public override AstNode VisitTryCatchStatement(TryCatchStatement
    // tryCatchStatement)` (line ~149): the merge mutates in place and returns
    // null, so the visit continues into the children as usual.
    void VisitTryCatchStatement(
        Syntax::TryCatchStatement* tryCatchStatement) override {
        TransformTryCatchFinally(tryCatchStatement);
        Syntax::DepthFirstAstVisitor::VisitTryCatchStatement(tryCatchStatement);
    }

    // The C# `public override AstNode VisitUsingStatement(UsingStatement
    // usingStatement)` (line ~1120): a `using (var x = e) { }` statement that is
    // the last statement of its block becomes the C# 8.0 using-declaration form
    // (the resource acquisition stays a variable declaration; the embedded
    // statement disappears from the printed form).
    void VisitUsingStatement(Syntax::UsingStatement* usingStatement) override {
        Syntax::DepthFirstAstVisitor::VisitUsingStatement(usingStatement);
        if (!context->DecompileRun->Settings().UseEnhancedUsing()) {
            lastResult = usingStatement;
            return;
        }
        if (Syntax::GetNextStatement(usingStatement) != nullptr ||
            dynamic_cast<Syntax::BlockStatement*>(usingStatement->Parent()) ==
                nullptr) {
            lastResult = usingStatement;
            return;
        }
        if (dynamic_cast<Syntax::VariableDeclarationStatement*>(
                usingStatement->ResourceAcquisition()) == nullptr) {
            lastResult = usingStatement;
            return;
        }
        context->StepOnce("Use enhanced using statement", usingStatement);
        usingStatement->IsEnhanced(true);
        lastResult = usingStatement;
    }

    // The C# `public override AstNode VisitPropertyDeclaration(PropertyDeclaration
    // propertyDeclaration)` (line ~134): the auto-property reshape mutates in
    // place and returns null, so the visit continues into the children.
    void VisitPropertyDeclaration(
        Syntax::PropertyDeclaration* propertyDeclaration) override {
        TransformAutomaticProperty(propertyDeclaration);
        Syntax::DepthFirstAstVisitor::VisitPropertyDeclaration(propertyDeclaration);
    }

    // The C# `public override AstNode VisitIdentifier(Identifier identifier)`
    // (line ~838): the backing-field rewrite replaces the identifier token
    // in place.
    void VisitIdentifier(Syntax::Identifier* identifier) override {
        if (context->DecompileRun->Settings().AutomaticProperties()) {
            if (Syntax::Identifier* newIdentifier =
                    ReplaceBackingFieldUsage(identifier)) {
                identifier->ReplaceWith(newIdentifier);
                lastResult = newIdentifier;
                return;
            }
        }
        Syntax::DepthFirstAstVisitor::VisitIdentifier(identifier);
    }

    // The C# `public override AstNode VisitIfElseStatement(IfElseStatement ...)`.
    // (The C# also routes VisitExpressionStatement/VisitForStatement through the
    // foreach/for arms and VisitPropertyDeclaration/VisitEventDeclaration/
    // VisitMethodDeclaration/VisitTryCatchStatement/VisitFixedStatement/
    // VisitUsingStatement through their arms -- all deferred, loud in the file
    // header.)
    void VisitIfElseStatement(Syntax::IfElseStatement* ifElseStatement) override {
        Syntax::AstNode* simplifiedIfElse =
            SimplifyCascadingIfElseStatements(ifElseStatement);
        if (simplifiedIfElse != nullptr) {
            lastResult = simplifiedIfElse;
            return;
        }
        Syntax::DepthFirstAstVisitor::VisitIfElseStatement(ifElseStatement);
    }

    // The C# `AstNode? SimplifyCascadingIfElseStatements(IfElseStatement node)`
    // (line ~1028): the else block wrapping a lone if-else collapses to a bare
    // `else if`. The node itself is not changed in identity, so the visitor
    // continues as usual -- return null.
    Syntax::AstNode* SimplifyCascadingIfElseStatements(Syntax::IfElseStatement* node) {
        PatternMatching::Match m = Syntax::MatchNode(CascadingIfElsePattern(), node);
        if (m.Success()) {
            context->StepOnce("Simplify cascading if-else", node);
            std::vector<Syntax::IfElseStatement*> nested =
                m.Get<Syntax::IfElseStatement>("nestedIfStatement");
            // The C# `.Single()` (exactly one capture by construction).
            assert(nested.size() == 1);
            node->FalseStatement(Syntax::Detach(nested.front()));
        }

        return nullptr;
    }

    // The C# `public override AstNode VisitBinaryOperatorExpression(...)`
    // (line ~1046): use the associativity of the conditional logic operators to
    // avoid parentheses.
    void VisitBinaryOperatorExpression(
        Syntax::BinaryOperatorExpression* expr) override {
        switch (expr->Operator()) {
            case Syntax::BinaryOperatorType::ConditionalAnd:
            case Syntax::BinaryOperatorType::ConditionalOr:
                // a && (b && c) ==> (a && b) && c
                if (auto* bAndC =
                        dynamic_cast<Syntax::BinaryOperatorExpression*>(expr->Right());
                    bAndC != nullptr && bAndC->Operator() == expr->Operator()) {
                    context->StepOnce("Reassociate conditional logic", expr);
                    // make bAndC the parent and expr the child.
                    // A conditional-and/or operator always has both operands present.
                    Syntax::Expression* b = Syntax::Detach(bAndC->Left());
                    Syntax::Expression* c = Syntax::Detach(bAndC->Right());
                    expr->ReplaceWith(Syntax::Detach(bAndC));
                    bAndC->Left(expr);
                    bAndC->Right(c);
                    expr->Right(b);
                    // The C# `context.EndStep(bAndC)` (the step-group close) folds
                    // onto the single step hook.
                    Syntax::DepthFirstAstVisitor::VisitBinaryOperatorExpression(bAndC);
                    return;
                }
                break;
            default:
                break;
        }
        Syntax::DepthFirstAstVisitor::VisitBinaryOperatorExpression(expr);
    }

    // The C# `public override AstNode VisitUnaryOperatorExpression(...)` (line
    // ~1073): `!(a == b)` becomes `a != b` (the replacement then goes through the
    // binary-operator arm, the C# `VisitBinaryOperatorExpression(binary)` virtual
    // call).
    void VisitUnaryOperatorExpression(Syntax::UnaryOperatorExpression* expr) override {
        if (expr->Operator() == Syntax::UnaryOperatorType::Not) {
            auto* binary =
                dynamic_cast<Syntax::BinaryOperatorExpression*>(expr->Expression());
            if (binary != nullptr &&
                binary->Operator() == Syntax::BinaryOperatorType::Equality) {
                context->StepOnce("Replace negated equality with inequality", expr);
                binary->Operator(Syntax::BinaryOperatorType::InEquality);
                expr->ReplaceWith(Syntax::Detach(binary));
                // The C# `context.EndStep(binary)` folds onto the single step hook.
                VisitBinaryOperatorExpression(binary);
                return;
            }
        }
        Syntax::DepthFirstAstVisitor::VisitUnaryOperatorExpression(expr);
    }
};

void PatternStatementTransform::Run(Syntax::AstNode& rootNode, TransformContext& context) {
    // The C# reentrancy guard (`InvalidOperationException` -> the port's
    // logic_error convention).
    if (context_ != nullptr)
        throw std::logic_error("Reentrancy in PatternStatementTransform.Run?");
    context_ = &context;
    try {
        PatternStatementTransformVisitor visitor;
        visitor.context = &context;
        visitor.declareVariables = &declareVariables_;
        visitor.Initialize(context);
        declareVariables_.Analyze(rootNode);
        rootNode.AcceptVisitor(visitor);
        // The C# finally also Uninitializes; the visitor's destruction covers it.
    } catch (...) {
        context_ = nullptr;
        declareVariables_.ClearAnalysisResults();
        throw;
    }
    context_ = nullptr;
    declareVariables_.ClearAnalysisResults();
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
