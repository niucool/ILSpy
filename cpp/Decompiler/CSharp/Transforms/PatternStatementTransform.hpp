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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/PatternStatementTransform.cs -- the
// pattern-matching AST transform that recognizes the compiler's lowered statement shapes and
// rewrites them back to the high-level language forms (a `while`/`for` loop back to `for`,
// the array/`for` loop back to `foreach`, the getter/setter pair back to an automatic
// property, the `Finalize` method back to a destructor, the nested `try { try {} catch {} }
// finally {}` back to a single try-catch-finally, a cascading `if`/`else { if }` back to
// `else if`, and the `!`+`==`/associative conditional-logic forms back to their canonical
// spelling).
//
// The C# type is `sealed class PatternStatementTransform : ContextTrackingVisitor<AstNode>,
// IAstTransform`. The port derives from `ContextTrackingVisitor` (the `TResult = AstNode`
// instantiation) and overrides `Run`, `VisitChildren`, and the per-node visits the ported
// sub-transforms need.
//
// The port is landed in slices. The skeleton (`Run`, `VisitChildren`) and the two
// purely-structural sub-transforms that need no pattern tree landed first: the
// conditional-logic reassociation (`a && (b && c)` -> `(a && b) && c`) and the negated
// equality rewrite (`!(a == b)` -> `a != b`). This slice adds the pattern-based
// sub-transforms that need no `DeclareVariables` analysis and no resolver: the destructor
// (`Finalize` method / `try { ... } finally { base.Finalize(); }` body), the nested
// `try { try {} catch {} } finally {}` merge, and the cascading `if`/`else { if }`
// simplification. The remaining pattern-based sub-transforms (the `for`/`foreach` loops,
// the automatic property/event rewrites) and the `DeclareVariables` analysis they compose
// stay deferred -- each is named at the visit that would call it. The two remaining
// resolver-free overrides also land here: the pattern-based `fixed` statement (the C# 7.3
// `&target.GetPinnableReference()` -> `target` rewrite for value types) and the enhanced
// using declaration (the C# 8 `using var` flag), which need only a resolve-result type read
// and the settings flags, not `DeclareVariables`.
//
// The `DeclareVariables` analysis phase landed separately; with it in place, this slice adds
// the `for` rewrite (`TransformFor`): the `var = init; while (var <op> end) { ...; var = ...; }`
// to `for` conversion (with the by-ref-local and iterator-declared-inside-loop guards and the
// no-`continue` rule) and the move of a preceding declaration assignment into an existing
// `for` initializer. The `foreach`-over-array rewrite (`TransformForeachOnArray`) also lands:
// the compiler's `for (i = 0; i < array.Length; i++) { item = array[i]; ... }` index loop is
// reconstructed as `foreach (item in array)` (also for a `string` looped by index), including
// the `VariableCanBeUsedAsForeachLocal` gate. The multidimensional-array `foreach` rewrite
// (`TransformForeachOnMultiDimArray`, the nested `GetUpperBound`/`GetLowerBound` index loops)
// also lands. The inline-array `foreach` rewrite (`TransformForeachOnInlineArray`) also lands:
// the compiler's `for (i = 0; i < N; i++) { item =
// <PrivateImplementationDetails>.InlineArrayElementRef(ref buffer, i); ... }` over an
// `[InlineArray(N)]` buffer is reconstructed as `foreach (item in buffer)`. This slice adds the
// automatic-property rewrite (`VisitPropertyDeclaration` / `TransformAutomaticProperty`): the
// private compiler-generated backing field and the getter/setter pair are recognized and the
// accessor bodies cleared, turning the property back into an auto-property, with the backing
// field declaration removed and its remaining attributes moved onto the property with the
// `field` target. The automatic-EVENT rewrite (which needs the `PropertyAndEventBackingFieldLookup`
// metadata machinery) and the `VisitIdentifier` backing-field replacement remain deferred -- each
// is named at the visit that would call it.

#pragma once

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Transforms/ContextTrackingVisitor.hpp"
#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include <vector>

// Forward declarations of the node types the ported sub-transforms take and return. Their
// definitions are pulled into the .cpp (the visitor overrides only need the pointer types).
namespace ILSpy::Decompiler::CSharp::Syntax {
class DestructorDeclaration;
class ExpressionStatement;
class FixedStatement;
class ForStatement;
class IdentifierExpression;
class IfElseStatement;
class MethodDeclaration;
class PropertyDeclaration;
class Statement;
class TryCatchStatement;
class UsingStatement;
class WhileStatement;
}

namespace ILSpy::Decompiler::IL {
class BlockContainer;
class ILVariable;
}

namespace ILSpy::Decompiler::TypeSystem {
class IProperty;
}

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public sealed class PatternStatementTransform`. `final` (the C# `sealed`).
class PatternStatementTransform final : public ContextTrackingVisitor, public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: rejects a
    // reentrant run, seeds the context, runs the `DeclareVariables` analysis, and walks the
    // tree (clearing the analysis afterwards).
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `public override AstNode VisitBinaryOperatorExpression(...)`: uses the
    // associativity of `&&`/`||` to avoid parentheses -- `a && (b && c)` becomes
    // `(a && b) && c` (the same for `||`). The rewrite makes the right operand the parent and
    // the visited expression its left operand, then continues the child walk from the new
    // parent.
    Syntax::AstNode* VisitBinaryOperatorExpression(
        Syntax::BinaryOperatorExpression* expr) override;

    // The C# `public override AstNode VisitUnaryOperatorExpression(...)`: replaces a negated
    // equality with the inequality spelling -- `!(a == b)` becomes `a != b`.
    Syntax::AstNode* VisitUnaryOperatorExpression(
        Syntax::UnaryOperatorExpression* expr) override;

    // The C# `public override AstNode VisitExpressionStatement(ExpressionStatement ...)`:
    // rewrites the compiler's multidimensional-array index loops back to `foreach`
    // (`TransformForeachOnMultiDimArray`), then `var = init; while (var <op> end) { ...;
    // var = ...; }` into a `for` loop, and moves a preceding declaration assignment into an
    // existing `for` initializer.
    Syntax::AstNode* VisitExpressionStatement(
        Syntax::ExpressionStatement* expressionStatement) override;

    // The C# `public override AstNode VisitForStatement(ForStatement ...)`: rewrites the
    // compiler's index loop back to `foreach` -- first the array/string form
    // (`TransformForeachOnArray`), then the inline-array form
    // (`TransformForeachOnInlineArray`).
    Syntax::AstNode* VisitForStatement(Syntax::ForStatement* forStatement) override;

    // The C# `public override AstNode VisitIfElseStatement(IfElseStatement ...)`: simplifies a
    // cascading `else { if (...) ... }` into `else if (...) ...`.
    Syntax::AstNode* VisitIfElseStatement(Syntax::IfElseStatement* ifElseStatement) override;

    // The C# `public override AstNode VisitTryCatchStatement(TryCatchStatement ...)`: merges a
    // nested `try { try {} catch {} } finally {}` into a single try-catch-finally.
    Syntax::AstNode* VisitTryCatchStatement(Syntax::TryCatchStatement* tryCatchStatement) override;

    // The C# `public override AstNode VisitPropertyDeclaration(PropertyDeclaration ...)`: with
    // `AutomaticProperties` on (and a setter present or `GetterOnlyAutomaticProperties` on),
    // rewrites the private compiler-generated backing-field getter/setter pair back into an
    // automatic property and hides the backing field declaration, then continues the child walk.
    Syntax::AstNode* VisitPropertyDeclaration(
        Syntax::PropertyDeclaration* propertyDeclaration) override;

    // The C# `public override AstNode VisitMethodDeclaration(MethodDeclaration ...)`: converts a
    // `Finalize` method into a destructor declaration.
    Syntax::AstNode* VisitMethodDeclaration(Syntax::MethodDeclaration* methodDeclaration) override;

    // The C# `public override AstNode VisitDestructorDeclaration(DestructorDeclaration ...)`:
    // simplifies a destructor's `try { ... } finally { base.Finalize(); }` body.
    Syntax::AstNode* VisitDestructorDeclaration(
        Syntax::DestructorDeclaration* destructorDeclaration) override;

    // The C# `public override AstNode VisitFixedStatement(FixedStatement fixedStatement)`: with
    // `PatternBasedFixedStatement` on, replaces a `fixed` variable's `&target
    // .GetPinnableReference()` initializer with the reference-typed `target` (the C# 7.3
    // pattern-based `fixed` form for value types), then continues the child walk.
    Syntax::AstNode* VisitFixedStatement(Syntax::FixedStatement* fixedStatement) override;

    // The C# `public override AstNode VisitUsingStatement(UsingStatement usingStatement)`: walks
    // the children first, then -- with `UseEnhancedUsing` on and when the statement is the last
    // statement of a `BlockStatement` and its resource acquisition is a variable declaration --
    // flags it as the C# 8 enhanced using declaration.
    Syntax::AstNode* VisitUsingStatement(Syntax::UsingStatement* usingStatement) override;

protected:
    // The C# `protected override AstNode VisitChildren(AstNode node)`: walks the children and
    // keeps visiting a node as long as the visit returns a different node (some sub-transforms
    // delete/replace nodes before and after the visited node, so the returned node is the
    // resumption point). Returns the walked node.
    Syntax::AstNode* VisitChildren(Syntax::AstNode* node) override;

private:
    // The C# `AstNode? SimplifyCascadingIfElseStatements(IfElseStatement node)`: rewrites the
    // matched shape in place. The C# always returns null (the instance is not replaced), so the
    // port keeps the nullable return for fidelity and the caller always continues the walk.
    Syntax::AstNode* SimplifyCascadingIfElseStatements(Syntax::IfElseStatement* node);

    // The C# `TryCatchStatement? TransformTryCatchFinally(TryCatchStatement tryFinally)`: merges
    // the matched nested try-catch into `tryFinally` in place. The C# always returns null.
    Syntax::TryCatchStatement* TransformTryCatchFinally(Syntax::TryCatchStatement* tryFinally);

    // The C# `DestructorDeclaration? TransformDestructor(MethodDeclaration methodDef)`: converts a
    // matched `Finalize` method into a destructor, or null when the shape does not match.
    Syntax::DestructorDeclaration* TransformDestructor(Syntax::MethodDeclaration* methodDef);

    // The C# `DestructorDeclaration? TransformDestructorBody(DestructorDeclaration dtorDef)`:
    // simplifies a matched destructor body in place, or null when the shape does not match.
    Syntax::DestructorDeclaration* TransformDestructorBody(Syntax::DestructorDeclaration* dtorDef);

    // The C# `for`-loop rewrite `ForStatement? TransformFor(ExpressionStatement node)`: the
    // `var = init; for (...)` declaration move and the `var = init; while (...) {...}` to
    // `for` rewrite, or null when the shape does not match.
    Syntax::ForStatement* TransformFor(Syntax::ExpressionStatement* node);

    // The C# `Statement? TransformForeachOnArray(ForStatement forStatement)`: rewrites the
    // compiler's `for (i = 0; i < array.Length; i++) { item = array[i]; ... }` shape back to
    // `foreach (var item in array) { ... }`, or null when the shape does not match.
    Syntax::Statement* TransformForeachOnArray(Syntax::ForStatement* forStatement);

    // The C# `Statement? TransformForeachOnMultiDimArray(ExpressionStatement ...)`: rewrites
    // the compiler's nested `GetUpperBound`/`GetLowerBound` index loops over a
    // multidimensional array back to `foreach (var item in array) { ... }`, or null when the
    // shape does not match.
    Syntax::Statement* TransformForeachOnMultiDimArray(
        Syntax::ExpressionStatement* expressionStatement);

    // The C# `Statement? TransformForeachOnInlineArray(ForStatement forStatement)`: rewrites
    // the compiler's `for (i = 0; i < N; i++) { item =
    // <PrivateImplementationDetails>.InlineArrayElementRef(ref buffer, i); ... }` loop over an
    // `[InlineArray(N)]` buffer back to `foreach (var item in buffer) { ... }`, or null when
    // the shape does not match.
    Syntax::Statement* TransformForeachOnInlineArray(Syntax::ForStatement* forStatement);

    // The C# `bool CanTransformToAutomaticProperty(IProperty property, bool
    // accessorsMustBeCompilerGenerated)`: whether the property can be rendered as an
    // auto-property -- readable, and (when the accessors must be compiler-generated) both the
    // getter and the setter are compiler-generated with a non-readonly setter.
    bool CanTransformToAutomaticProperty(
        const ILSpy::Decompiler::TypeSystem::IProperty& property,
        bool accessorsMustBeCompilerGenerated);

    // The C# `PropertyDeclaration? TransformAutomaticProperty(PropertyDeclaration ...)`: matches
    // the getter/setter (or getter-only) backing-field shape and, when the backing field is a
    // compiler-generated field of the property's own declaring type, clears the accessor bodies
    // and removes the backing field declaration (moving its attributes onto the property). The
    // C# always returns null (the property instance is not replaced), so the port keeps the
    // nullable return for fidelity.
    Syntax::AstNode* TransformAutomaticProperty(
        Syntax::PropertyDeclaration* propertyDeclaration);

    // The C# `bool MatchLowerBound(int indexNum, out ILVariable? index, ILVariable collection,
    // Statement statement)`: matches `$variable = $collection.GetLowerBound($indexNum)`.
    bool MatchLowerBound(int indexNum, IL::ILVariable*& index, IL::ILVariable* collection,
                         Syntax::Statement* statement);

    // The C# `bool MatchForeachOnMultiDimArray(ILVariable[] upperBounds, ILVariable collection,
    // Statement firstInitializerStatement, out IdentifierExpression? foreachVariable, out
    // IList<Statement>? statements, out ILVariable[] lowerBounds)`: walks the per-dimension
    // lower-bound / `for` nests and captures the body statements and the element assignment.
    bool MatchForeachOnMultiDimArray(const std::vector<IL::ILVariable*>& upperBounds,
                                     IL::ILVariable* collection,
                                     Syntax::Statement* firstInitializerStatement,
                                     Syntax::IdentifierExpression*& foreachVariable,
                                     std::vector<Syntax::Statement*>& statements,
                                     std::vector<IL::ILVariable*>& lowerBounds);

    // The C# `bool VariableCanBeUsedAsForeachLocal(ILVariable? itemVar, Statement loop)`:
    // whether the item variable can become the `foreach` loop variable (a local/stack-slot
    // with a single definition, not captured outside the loop, not merged by the declaration
    // analysis, and declared inside the loop).
    bool VariableCanBeUsedAsForeachLocal(IL::ILVariable* itemVar, Syntax::Statement* loop);

    // The C# `static bool AddressUsedForSingleCall(ILVariable v, BlockContainer? loop)`: the
    // special case accepting an item variable whose address is taken for a single method call.
    // DEFERRED: the port has no `IL.Call` node yet and no per-variable address-instruction list,
    // so the address-taken path cannot be reconstructed; the helper conservatively answers false.
    static bool AddressUsedForSingleCall(IL::ILVariable* v, IL::BlockContainer* loop);

    // The C# `bool DescendIntoStatement(AstNode node)`: the descendant-walk predicate that
    // stops at expressions and nested loops (so a `continue` in a nested loop does not block
    // the rewrite).
    static bool DescendIntoStatement(Syntax::AstNode* node);

    // The C# `bool ForStatementUsesVariable(ForStatement, ILVariable?)`: whether the `for`
    // condition or an iterator references the variable.
    static bool ForStatementUsesVariable(Syntax::ForStatement* statement,
                                         IL::ILVariable* variable);

    // The C# `bool IsVariableUsedAfter(Statement loop, ILVariable)`: whether any following
    // sibling statement references the variable (the by-ref-local guard).
    static bool IsVariableUsedAfter(Syntax::Statement* loop, IL::ILVariable& variable);

    // The C# `bool IteratorVariablesDeclaredInsideLoopBody(Statement iteratorStatement)`:
    // whether a variable used by the iterator would be declared in the loop body (which the
    // rewrite cannot split from the iterator part).
    bool IteratorVariablesDeclaredInsideLoopBody(Syntax::Statement* iteratorStatement);

    // The C# `readonly DeclareVariables declareVariables` -- the analysis the `for` rewrite
    // consumes (`Analyze` in `Run`, `GetDeclarationPoint` in the iterator guard).
    DeclareVariables declareVariables_;

    // The C# `[AllowNull] TransformContext context` -- the run state. Null outside a run; the
    // reentrancy check in `Run` reads it.
    TransformContext* context_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
