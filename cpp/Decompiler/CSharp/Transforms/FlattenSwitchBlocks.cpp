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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "Decompiler/CSharp/Transforms/FlattenSwitchBlocks.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::BlockStatement;
using Syntax::LocalFunctionDeclarationStatement;
using Syntax::OutVarDeclarationExpression;
using Syntax::SwitchSection;
using Syntax::VariableDeclarationStatement;

namespace {

// The C# local function `bool ContainsLocalDeclaration(AstNode node)`: whether the
// statement (or a descendant) declares a local variable, local function, or out-var
// whose scope the flatten would change. A nested `BlockStatement` stops the walk
// (its own scope keeps the declaration valid without the outer braces), so only
// declarations that sit directly in the block being unwrapped suppress the flatten.
bool ContainsLocalDeclaration(AstNode& node) {
    if (dynamic_cast<VariableDeclarationStatement*>(&node) != nullptr
        || dynamic_cast<LocalFunctionDeclarationStatement*>(&node) != nullptr
        || dynamic_cast<OutVarDeclarationExpression*>(&node) != nullptr) {
        return true;
    }
    if (dynamic_cast<BlockStatement*>(&node) != nullptr)
        return false;
    for (AstNode* child : node.Children()) {
        if (ContainsLocalDeclaration(*child))
            return true;
    }
    return false;
}

} // namespace

void FlattenSwitchBlocks::Run(AstNode& rootNode, TransformContext& /*context*/) {
    // The C# `foreach (var switchSection in rootNode.Descendants.OfType<SwitchSection>())`:
    // the materialized pre-order walk (the port's `Descendants()` returns a vector, so
    // reparenting a block's statements during the loop cannot disturb the iteration --
    // stricter than the C# lazy LINQ sequence and behaviourally equivalent for the
    // flatten, which only ever removes a block and moves statements that are not
    // themselves `SwitchSection`s).
    for (AstNode* node : rootNode.Descendants()) {
        auto* switchSection = dynamic_cast<SwitchSection*>(node);
        if (switchSection == nullptr)
            continue;

        // The C# `if (switchSection.Statements.Count != 1) continue;`.
        auto& statements = switchSection->Statements();
        if (statements.Count() != 1)
            continue;

        // The C# `var blockStatement = switchSection.Statements.First() as BlockStatement;`
        // -- a non-block single statement is left alone.
        auto* blockStatement = dynamic_cast<BlockStatement*>(statements.At(0));
        if (blockStatement == nullptr)
            continue;

        // The C# `|| blockStatement.Statements.Any(ContainsLocalDeclaration)` guard.
        bool hasLocalDeclaration = false;
        for (int i = 0; i < blockStatement->Statements().Count(); i++) {
            if (ContainsLocalDeclaration(*blockStatement->Statements().At(i))) {
                hasLocalDeclaration = true;
                break;
            }
        }
        if (hasLocalDeclaration)
            continue;

        // The C# `context.Step("Flatten switch section block", blockStatement)` (a no-op
        // in the port), then `blockStatement.Remove()` followed by
        // `blockStatement.Statements.MoveTo(switchSection.Statements)` -- the block is
        // detached before its statements are re-parented into the section.
        blockStatement->Remove();
        blockStatement->Statements().MoveTo(statements);
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
