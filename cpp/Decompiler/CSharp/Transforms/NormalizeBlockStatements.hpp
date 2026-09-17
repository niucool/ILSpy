// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Transforms/NormalizeBlockStatements.cs -- the
// AST transform that normalizes statement braces and shared-declaration shapes before
// the output stage. It decides per statement whether an embedded statement is legal
// without braces (a single legal statement stays bare, an illegal one gets wrapped in a
// `BlockStatement`), unwraps a redundant single-statement block under an `if`/`else` or
// `using`, and optionally rewrites a calculated getter-only property/indexer to an
// expression body. It also marks the single namespace of a tree file-scoped when the
// `FileScopedNamespaces` setting is on.
//
// The C# class is `class NormalizeBlockStatements : DepthFirstAstVisitor, IAstTransform`
// (internal). The port keeps it public for direct testing, exactly like the other
// transform ports.
//
// Divergence: the C# `SimplifyPropertyDeclaration`/`SimplifyIndexerDeclaration` match the
// getter with the generated `CalculatedGetterOnlyPropertyPattern`/`...IndexerPattern`
// (`new PropertyDeclaration() { Attributes = { new Repeat(new AnyNode()) }, ... }`). The
// concrete pattern nodes (`AnyNode`/`Repeat`/`AnyNodeOrNull`) are not ported yet (only the
// `Pattern`/`Match` base and the backtracking engine are), so the port recognizes the same
// shape with direct structural checks (a present getter whose body is a single
// `return <expr>;` block, no accessor modifiers beyond `readonly`). The checks are stricter
// only in that they read the concrete structure instead of the pattern tree; the matched
// language is identical.

#pragma once

#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

class TransformContext;

// The C# `class NormalizeBlockStatements : DepthFirstAstVisitor, IAstTransform`. The
// transform keeps the traversal state (`context`, `hasNamespace`,
// `singleNamespaceDeclaration`) as instance fields, matching the C#.
class NormalizeBlockStatements : public Syntax::DepthFirstAstVisitor, public IAstTransform {
public:
    // The C# explicit `IAstTransform.Run` -- stores the context and drives the visitor.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `VisitSyntaxTree`: resets the namespace tracking, walks the tree, and marks
    // the single namespace file-scoped when the setting is enabled.
    void VisitSyntaxTree(Syntax::SyntaxTree* node) override;

    // The C# `VisitNamespaceDeclaration`: records the first namespace seen (the file-scoped
    // candidate) and recurses.
    void VisitNamespaceDeclaration(Syntax::NamespaceDeclaration* node) override;

    // The C# `VisitIfElseStatement`: normalizes the true and false arms through
    // `DoTransform` after recursing (a redundant arm block is unwrapped or a brace-less arm
    // is wrapped).
    void VisitIfElseStatement(Syntax::IfElseStatement* node) override;

    // The C# loop/using/fixed/lock visits: recurse, then force a block around the embedded
    // statement (`InsertBlock`), except `using` whose embedded statement goes through
    // `DoTransform` (it can drop a redundant block).
    void VisitWhileStatement(Syntax::WhileStatement* node) override;
    void VisitDoWhileStatement(Syntax::DoWhileStatement* node) override;
    void VisitForeachStatement(Syntax::ForeachStatement* node) override;
    void VisitForStatement(Syntax::ForStatement* node) override;
    void VisitFixedStatement(Syntax::FixedStatement* node) override;
    void VisitLockStatement(Syntax::LockStatement* node) override;
    void VisitUsingStatement(Syntax::UsingStatement* node) override;

    // The C# `VisitPropertyDeclaration`/`VisitIndexerDeclaration`: when
    // `UseExpressionBodyForCalculatedGetterOnlyProperties` is on, rewrite the calculated
    // getter-only form to an expression body, then recurse.
    void VisitPropertyDeclaration(Syntax::PropertyDeclaration* node) override;
    void VisitIndexerDeclaration(Syntax::IndexerDeclaration* node) override;

private:
    // The C# `void DoTransform(Statement? statement, Statement parent)`: with
    // `AlwaysUseBraces` on, wrap every non-else-if arm in a block; otherwise unwrap a
    // single-statement block that is legal as an embedded statement, or wrap a statement
    // that is not.
    void DoTransform(Syntax::Statement* statement, Syntax::Statement& parent);

    // The C# `bool IsElseIf(Statement, Statement parent)`: whether the statement is the
    // `else` arm of the given `if`.
    static bool IsElseIf(Syntax::Statement& statement, Syntax::Statement& parent);

    // The C# `void InsertBlock(Statement statement)`: wrap a non-block statement in a fresh
    // `BlockStatement`; a childless empty statement is dropped and its annotations are moved
    // to the block instead.
    void InsertBlock(Syntax::Statement& statement);

    // The C# `bool IsAllowedAsEmbeddedStatement(Statement, Statement parent)`: the
    // statement kinds that need braces, the `else`-if and `using` special cases, and the
    // default rule (a statement directly under an `if`'s arm cannot be brace-less).
    static bool IsAllowedAsEmbeddedStatement(Syntax::Statement& statement,
                                             Syntax::Statement& parent);

    // The C# `void SimplifyPropertyDeclaration`/`SimplifyIndexerDeclaration` (see the header
    // divergence note): rewrite a calculated getter-only property/indexer to an expression
    // body.
    void SimplifyPropertyDeclaration(Syntax::PropertyDeclaration& property);
    void SimplifyIndexerDeclaration(Syntax::IndexerDeclaration& indexer);

    TransformContext* context_ = nullptr;
    bool hasNamespace_ = false;
    Syntax::NamespaceDeclaration* singleNamespaceDeclaration_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
