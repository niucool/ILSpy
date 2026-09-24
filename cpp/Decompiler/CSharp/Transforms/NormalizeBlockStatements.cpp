// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/CSharp/Transforms/NormalizeBlockStatements.hpp"

#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/IndexerDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TranslationContext.hpp"

#include <memory>

namespace ILSpy::Decompiler::CSharp {
// The C# `CS` root surface (CopyAnnotationsFrom) lives here.
} // namespace ILSpy::Decompiler::CSharp

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;

// The C# `bool IsElseIf(Statement statement, Statement parent)`.
bool IsElseIf(Syntax::Statement* statement, Syntax::Statement* parent) {
    return dynamic_cast<const Syntax::IfElseStatement*>(parent) != nullptr &&
           statement->Slot() != nullptr &&
           statement->Slot()->Kind() == &Syntax::Slots::FalseStatement;
}

// The C# `static bool IsWithoutSideEffects`-style leaf set is shared with
// PrettifyAssignments; here the C# has its own private
// `IsAllowedAsEmbeddedStatement` switch.
bool IsAllowedAsEmbeddedStatement(Syntax::Statement* statement,
                                  Syntax::Statement* parent) {
    if (const auto* ies = dynamic_cast<const Syntax::IfElseStatement*>(statement)) {
        return dynamic_cast<const Syntax::IfElseStatement*>(parent) != nullptr &&
               statement->Slot() != nullptr &&
               statement->Slot()->Kind() == &Syntax::Slots::FalseStatement;
    }
    if (dynamic_cast<const Syntax::VariableDeclarationStatement*>(statement) !=
            nullptr ||
        dynamic_cast<const Syntax::WhileStatement*>(statement) != nullptr ||
        dynamic_cast<const Syntax::DoWhileStatement*>(statement) != nullptr ||
        dynamic_cast<const Syntax::SwitchStatement*>(statement) != nullptr ||
        dynamic_cast<const Syntax::ForeachStatement*>(statement) != nullptr ||
        dynamic_cast<const Syntax::ForStatement*>(statement) != nullptr ||
        dynamic_cast<const Syntax::LockStatement*>(statement) != nullptr ||
        dynamic_cast<const Syntax::FixedStatement*>(statement) != nullptr) {
        return false;
    }
    if (const auto* us = dynamic_cast<const Syntax::UsingStatement*>(statement)) {
        return dynamic_cast<const Syntax::UsingStatement*>(parent) != nullptr &&
               !us->IsEnhanced();
    }
    if (parent == nullptr) return true;
    return dynamic_cast<const Syntax::IfElseStatement*>(parent->Parent()) == nullptr;
}

} // namespace

// The visitor half (the C# class body): the context field + the per-node
// overrides. The C# `IAstTransform.Run` explicit implementation is the outer
// Run's drive.
class NormalizeBlockStatementsVisitor final : public Syntax::DepthFirstAstVisitor {
public:
    TransformContext* context = nullptr;
    bool hasNamespace = false;
    Syntax::NamespaceDeclaration* singleNamespaceDeclaration = nullptr;

    // The C# `public override void VisitSyntaxTree(SyntaxTree syntaxTree)`.
    void VisitSyntaxTree(Syntax::SyntaxTree* syntaxTree) override {
        if (syntaxTree == nullptr || context == nullptr) return;
        singleNamespaceDeclaration = nullptr;
        hasNamespace = false;
        DepthFirstAstVisitor::VisitSyntaxTree(syntaxTree);
        if (context->DecompileRun->Settings().FileScopedNamespaces() &&
            singleNamespaceDeclaration != nullptr) {
            context->StepOnce("Use file-scoped namespace",
                              singleNamespaceDeclaration);
            singleNamespaceDeclaration->IsFileScoped(true);
        }
    }

    // The C# `public override void VisitNamespaceDeclaration(...)`.
    void VisitNamespaceDeclaration(Syntax::NamespaceDeclaration* namespaceDeclaration)
        override {
        if (namespaceDeclaration == nullptr) return;
        singleNamespaceDeclaration = nullptr;
        if (!hasNamespace) {
            hasNamespace = true;
            singleNamespaceDeclaration = namespaceDeclaration;
        }
        DepthFirstAstVisitor::VisitNamespaceDeclaration(namespaceDeclaration);
    }

    // The C# `public override void VisitIfElseStatement(IfElseStatement ...)`.
    void VisitIfElseStatement(Syntax::IfElseStatement* ifElseStatement) override {
        if (ifElseStatement == nullptr) return;
        DepthFirstAstVisitor::VisitIfElseStatement(ifElseStatement);
        DoTransform(ifElseStatement->TrueStatement(), ifElseStatement);
        DoTransform(ifElseStatement->FalseStatement(), ifElseStatement);
    }

    void VisitWhileStatement(Syntax::WhileStatement* whileStatement) override {
        if (whileStatement == nullptr) return;
        DepthFirstAstVisitor::VisitWhileStatement(whileStatement);
        InsertBlock(whileStatement->EmbeddedStatement());
    }

    void VisitDoWhileStatement(Syntax::DoWhileStatement* doWhileStatement) override {
        if (doWhileStatement == nullptr) return;
        DepthFirstAstVisitor::VisitDoWhileStatement(doWhileStatement);
        InsertBlock(doWhileStatement->EmbeddedStatement());
    }

    void VisitForeachStatement(Syntax::ForeachStatement* foreachStatement) override {
        if (foreachStatement == nullptr) return;
        DepthFirstAstVisitor::VisitForeachStatement(foreachStatement);
        InsertBlock(foreachStatement->EmbeddedStatement());
    }

    void VisitForStatement(Syntax::ForStatement* forStatement) override {
        if (forStatement == nullptr) return;
        DepthFirstAstVisitor::VisitForStatement(forStatement);
        InsertBlock(forStatement->EmbeddedStatement());
    }

    void VisitFixedStatement(Syntax::FixedStatement* fixedStatement) override {
        if (fixedStatement == nullptr) return;
        DepthFirstAstVisitor::VisitFixedStatement(fixedStatement);
        InsertBlock(fixedStatement->EmbeddedStatement());
    }

    void VisitLockStatement(Syntax::LockStatement* lockStatement) override {
        if (lockStatement == nullptr) return;
        DepthFirstAstVisitor::VisitLockStatement(lockStatement);
        InsertBlock(lockStatement->EmbeddedStatement());
    }

    void VisitUsingStatement(Syntax::UsingStatement* usingStatement) override {
        if (usingStatement == nullptr) return;
        DepthFirstAstVisitor::VisitUsingStatement(usingStatement);
        DoTransform(usingStatement->EmbeddedStatement(), usingStatement);
    }

    // The C# `public override void VisitPropertyDeclaration(...)` + the
    // expression-body arm.
    void VisitPropertyDeclaration(Syntax::PropertyDeclaration* propertyDeclaration)
        override {
        if (propertyDeclaration == nullptr) return;
        if (context != nullptr &&
            context->DecompileRun->Settings()
                .UseExpressionBodyForCalculatedGetterOnlyProperties()) {
            SimplifyPropertyDeclaration(propertyDeclaration);
        }
        DepthFirstAstVisitor::VisitPropertyDeclaration(propertyDeclaration);
    }

    void VisitIndexerDeclaration(Syntax::IndexerDeclaration* indexerDeclaration)
        override {
        if (indexerDeclaration == nullptr) return;
        if (context != nullptr &&
            context->DecompileRun->Settings()
                .UseExpressionBodyForCalculatedGetterOnlyProperties()) {
            SimplifyIndexerDeclaration(indexerDeclaration);
        }
        DepthFirstAstVisitor::VisitIndexerDeclaration(indexerDeclaration);
    }

    // The C# `void DoTransform(Statement? statement, Statement parent)`.
    void DoTransform(Syntax::Statement* statement, Syntax::Statement* parent) {
        if (statement == nullptr || context == nullptr) return;
        if (context->DecompileRun->Settings().AlwaysUseBraces()) {
            if (!IsElseIf(statement, parent)) {
                InsertBlock(statement);
            }
        } else {
            auto* block = dynamic_cast<Syntax::BlockStatement*>(statement);
            if (block != nullptr && block->Statements().Count() == 1 &&
                IsAllowedAsEmbeddedStatement(block->Statements().FirstOrNull(),
                                             parent)) {
                context->StepOnce("Remove redundant block statement", statement);
                Syntax::Statement* innerStatement =
                    Syntax::Detach(block->Statements().FirstOrNull());
                statement->ReplaceWith(innerStatement);
            } else if (!IsAllowedAsEmbeddedStatement(statement, parent)) {
                InsertBlock(statement);
            }
        }
    }

    // The C# `void InsertBlock(Statement statement)`.
    void InsertBlock(Syntax::Statement* statement) {
        if (statement == nullptr || context == nullptr) return;
        if (dynamic_cast<Syntax::BlockStatement*>(statement) == nullptr) {
            auto* b = new Syntax::BlockStatement();
            context->StepOnce("Add block statement", statement);
            statement->ReplaceWith(b);
            if (dynamic_cast<Syntax::EmptyStatement*>(statement) != nullptr &&
                !statement->HasChildren()) {
                // The empty statement carries no children; the C# keeps the
                // annotations (an EmptyStatement's step annotation channel).
                CS::CopyAnnotationsFrom(b, *statement);
            } else {
                b->Statements().Add(statement);
            }
    }
    }

    // ---- The expression-body arms (the C# pattern-based simplifiers) --------
    //
    // DEFERRED loudly with the port's pattern-graph surface: the C# builds the
    // two static patterns (CalculatedGetterOnlyPropertyPattern /
    // CalculatedGetterOnlyIndexerPattern) out of real PropertyDeclaration /
    // Accessor nodes with INode-typed pattern children (AnyNode /
    // AnyNodeOrNull / Repeat) in the typed slots -- the C# pattern-side slots
    // accept INode because the generator's slot storage is INode-typed on the
    // pattern side; the port's typed slot setters require the concrete
    // Expression/AstType. The bridge (a pattern-side INode slot view) lands
    // with the PatternStatementTransform family. The two arms are gated on
    // UseExpressionBodyForCalculatedGetterOnlyProperties, which the seed
    // output does not exercise.
    void SimplifyPropertyDeclaration(Syntax::PropertyDeclaration* /*propertyDeclaration*/) {
        // DEFERRED (see the note above).
    }

    void SimplifyIndexerDeclaration(Syntax::IndexerDeclaration* /*indexerDeclaration*/) {
        // DEFERRED (see the note above).
    }
};

void NormalizeBlockStatements::Run(Syntax::AstNode& rootNode, TransformContext& context) {
    NormalizeBlockStatementsVisitor visitor;
    visitor.context = &context;
    rootNode.AcceptVisitor(visitor);
}

} // namespace ILSpy::Decompiler::CSharp::Transforms