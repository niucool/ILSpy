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

#include "Decompiler/CSharp/Transforms/NormalizeBlockStatements.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/IndexerDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/EmptyStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/SwitchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::Accessor;
using Syntax::AstNode;
using Syntax::BlockStatement;
using Syntax::CSharpSlotInfo;
using Syntax::DoWhileStatement;
using Syntax::EmptyStatement;
using Syntax::FixedStatement;
using Syntax::ForStatement;
using Syntax::ForeachStatement;
using Syntax::IfElseStatement;
using Syntax::IndexerDeclaration;
using Syntax::LockStatement;
using Syntax::Modifiers;
using Syntax::NamespaceDeclaration;
using Syntax::PropertyDeclaration;
using Syntax::ReturnStatement;
using Syntax::Statement;
using Syntax::SwitchStatement;
using Syntax::SyntaxTree;
using Syntax::UsingStatement;
using Syntax::VariableDeclarationStatement;
using Syntax::WhileStatement;

void NormalizeBlockStatements::Run(AstNode& rootNode, TransformContext& context) {
    context_ = &context;
    rootNode.AcceptVisitor(*this);
}

void NormalizeBlockStatements::VisitSyntaxTree(SyntaxTree* syntaxTree) {
    singleNamespaceDeclaration_ = nullptr;
    hasNamespace_ = false;
    Syntax::DepthFirstAstVisitor::VisitSyntaxTree(syntaxTree);
    if (context_->Settings().FileScopedNamespaces()
        && singleNamespaceDeclaration_ != nullptr) {
        singleNamespaceDeclaration_->IsFileScoped(true);
    }
}

void NormalizeBlockStatements::VisitNamespaceDeclaration(NamespaceDeclaration* node) {
    singleNamespaceDeclaration_ = nullptr;
    if (!hasNamespace_) {
        hasNamespace_ = true;
        singleNamespaceDeclaration_ = node;
    }
    Syntax::DepthFirstAstVisitor::VisitNamespaceDeclaration(node);
}

void NormalizeBlockStatements::VisitIfElseStatement(IfElseStatement* node) {
    Syntax::DepthFirstAstVisitor::VisitIfElseStatement(node);
    DoTransform(node->TrueStatement(), *node);
    DoTransform(node->FalseStatement(), *node);
}

void NormalizeBlockStatements::VisitWhileStatement(WhileStatement* node) {
    Syntax::DepthFirstAstVisitor::VisitWhileStatement(node);
    InsertBlock(*node->EmbeddedStatement());
}

void NormalizeBlockStatements::VisitDoWhileStatement(DoWhileStatement* node) {
    Syntax::DepthFirstAstVisitor::VisitDoWhileStatement(node);
    InsertBlock(*node->EmbeddedStatement());
}

void NormalizeBlockStatements::VisitForeachStatement(ForeachStatement* node) {
    Syntax::DepthFirstAstVisitor::VisitForeachStatement(node);
    InsertBlock(*node->EmbeddedStatement());
}

void NormalizeBlockStatements::VisitForStatement(ForStatement* node) {
    Syntax::DepthFirstAstVisitor::VisitForStatement(node);
    InsertBlock(*node->EmbeddedStatement());
}

void NormalizeBlockStatements::VisitFixedStatement(FixedStatement* node) {
    Syntax::DepthFirstAstVisitor::VisitFixedStatement(node);
    InsertBlock(*node->EmbeddedStatement());
}

void NormalizeBlockStatements::VisitLockStatement(LockStatement* node) {
    Syntax::DepthFirstAstVisitor::VisitLockStatement(node);
    InsertBlock(*node->EmbeddedStatement());
}

void NormalizeBlockStatements::VisitUsingStatement(UsingStatement* node) {
    Syntax::DepthFirstAstVisitor::VisitUsingStatement(node);
    DoTransform(node->EmbeddedStatement(), *node);
}

void NormalizeBlockStatements::VisitPropertyDeclaration(PropertyDeclaration* node) {
    if (context_->Settings().UseExpressionBodyForCalculatedGetterOnlyProperties()) {
        SimplifyPropertyDeclaration(*node);
    }
    Syntax::DepthFirstAstVisitor::VisitPropertyDeclaration(node);
}

void NormalizeBlockStatements::VisitIndexerDeclaration(IndexerDeclaration* node) {
    if (context_->Settings().UseExpressionBodyForCalculatedGetterOnlyProperties()) {
        SimplifyIndexerDeclaration(*node);
    }
    Syntax::DepthFirstAstVisitor::VisitIndexerDeclaration(node);
}

void NormalizeBlockStatements::DoTransform(Statement* statement, Statement& parent) {
    if (statement == nullptr)
        return;
    if (context_->Settings().AlwaysUseBraces()) {
        if (!IsElseIf(*statement, parent)) {
            InsertBlock(*statement);
        }
    } else {
        auto* block = dynamic_cast<BlockStatement*>(statement);
        if (block != nullptr && block->Statements().Count() == 1
            && IsAllowedAsEmbeddedStatement(*block->Statements().At(0), parent)) {
            // The C# `var innerStatement = b.Statements.First().Detach();
            // statement.ReplaceWith(innerStatement);` -- the inner statement is detached
            // from the block before the block is replaced by it.
            Statement* innerStatement = Syntax::Detach(block->Statements().At(0));
            statement->ReplaceWith(innerStatement);
        } else if (!IsAllowedAsEmbeddedStatement(*statement, parent)) {
            InsertBlock(*statement);
        }
    }
}

bool NormalizeBlockStatements::IsElseIf(Statement& statement, Statement& parent) {
    if (dynamic_cast<IfElseStatement*>(&parent) == nullptr)
        return false;
    const CSharpSlotInfo* slot = statement.Slot();
    return slot != nullptr && slot->Kind() == &Syntax::Slots::FalseStatement;
}

void NormalizeBlockStatements::InsertBlock(Statement& statement) {
    if (dynamic_cast<BlockStatement*>(&statement) != nullptr)
        return;
    auto* block = new BlockStatement();
    statement.ReplaceWith(block);
    if (dynamic_cast<EmptyStatement*>(&statement) != nullptr && !statement.HasChildren()) {
        // An empty embedded statement `;` carries no code, so the C# drops it and moves its
        // annotations onto the new (empty) block instead of adding the statement.
        CSharp::CopyAnnotationsFrom(block, statement);
    } else {
        block->Statements().Add(&statement);
    }
}

bool NormalizeBlockStatements::IsAllowedAsEmbeddedStatement(Statement& statement,
                                                            Statement& parent) {
    if (dynamic_cast<IfElseStatement*>(&statement) != nullptr) {
        return dynamic_cast<IfElseStatement*>(&parent) != nullptr
            && IsElseIf(statement, parent);
    }
    if (dynamic_cast<VariableDeclarationStatement*>(&statement) != nullptr
        || dynamic_cast<WhileStatement*>(&statement) != nullptr
        || dynamic_cast<DoWhileStatement*>(&statement) != nullptr
        || dynamic_cast<SwitchStatement*>(&statement) != nullptr
        || dynamic_cast<ForeachStatement*>(&statement) != nullptr
        || dynamic_cast<ForStatement*>(&statement) != nullptr
        || dynamic_cast<LockStatement*>(&statement) != nullptr
        || dynamic_cast<FixedStatement*>(&statement) != nullptr) {
        return false;
    }
    if (auto* usingStatement = dynamic_cast<UsingStatement*>(&statement)) {
        return dynamic_cast<UsingStatement*>(&parent) != nullptr
            && !usingStatement->IsEnhanced();
    }
    // The C# default arm: `return !(parent?.Parent is IfElseStatement)`.
    return dynamic_cast<IfElseStatement*>(parent.Parent()) == nullptr;
}

void NormalizeBlockStatements::SimplifyPropertyDeclaration(PropertyDeclaration& property) {
    Accessor* getter = property.Getter();
    if (getter == nullptr)
        return;
    BlockStatement* body = getter->Body();
    if (body == nullptr)
        return;
    if (body->Statements().Count() != 1)
        return;
    auto* returnStatement = dynamic_cast<ReturnStatement*>(body->Statements().At(0));
    if (returnStatement == nullptr || returnStatement->Expression() == nullptr)
        return;
    // The C# `movableModifiers = Modifiers.Readonly` check: only a `readonly` accessor
    // modifier may be hoisted onto the property declaration.
    if ((getter->Modifiers() & ~Modifiers::Readonly) != Modifiers::None)
        return;
    property.Modifiers(property.Modifiers() | getter->Modifiers());
    property.ExpressionBody(Syntax::Detach(returnStatement->Expression()));
    CSharp::CopyAnnotationsFrom(&property, *getter);
    getter->Remove();
}

void NormalizeBlockStatements::SimplifyIndexerDeclaration(IndexerDeclaration& indexer) {
    Accessor* getter = indexer.Getter();
    if (getter == nullptr)
        return;
    BlockStatement* body = getter->Body();
    if (body == nullptr)
        return;
    if (body->Statements().Count() != 1)
        return;
    auto* returnStatement = dynamic_cast<ReturnStatement*>(body->Statements().At(0));
    if (returnStatement == nullptr || returnStatement->Expression() == nullptr)
        return;
    if ((getter->Modifiers() & ~Modifiers::Readonly) != Modifiers::None)
        return;
    indexer.Modifiers(indexer.Modifiers() | getter->Modifiers());
    indexer.ExpressionBody(Syntax::Detach(returnStatement->Expression()));
    CSharp::CopyAnnotationsFrom(&indexer, *getter);
    getter->Remove();
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
