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

#include "Decompiler/CSharp/Transforms/FlattenSwitchBlocks.hpp"

#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/SwitchSection.hpp"

#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {
namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;

// The C# `bool ContainsLocalDeclaration(AstNode node)` (the local function
// inside Run): a VariableDeclarationStatement /
// LocalFunctionDeclarationStatement / OutVarDeclarationExpression anywhere
// below the block (the block itself is transparent -- a nested block is NOT a
// reason to keep the wrapper) aborts the flatten.
bool ContainsLocalDeclaration(Syntax::AstNode* node) {
    if (dynamic_cast<const Syntax::VariableDeclarationStatement*>(node) != nullptr ||
        dynamic_cast<const Syntax::LocalFunctionDeclarationStatement*>(node) != nullptr ||
        dynamic_cast<const Syntax::OutVarDeclarationExpression*>(node) != nullptr) {
        return true;
    }
    if (dynamic_cast<const Syntax::BlockStatement*>(node) != nullptr) {
        return false;
    }
    for (Syntax::AstNode* child = node->FirstChild(); child != nullptr;
         child = child->NextSibling()) {
        if (ContainsLocalDeclaration(child)) return true;
    }
    return false;
}

} // namespace

void FlattenSwitchBlocks::Run(::ILSpy::Decompiler::CSharp::Syntax::AstNode& rootNode,
                              TransformContext& context) {
    (void)context;
    for (Syntax::AstNode* node : rootNode.Descendants()) {
        auto* switchSection = dynamic_cast<Syntax::SwitchSection*>(node);
        if (switchSection == nullptr) continue;
        if (switchSection->Statements().Count() != 1) continue;
        auto* blockStatement =
            dynamic_cast<Syntax::BlockStatement*>(switchSection->Statements().FirstOrNull());
        if (blockStatement == nullptr) continue;
        bool hasLocalDeclaration = false;
        for (Syntax::AstNode* child = blockStatement->FirstChild(); child != nullptr;
             child = child->NextSibling()) {
            if (ContainsLocalDeclaration(child)) {
                hasLocalDeclaration = true;
                break;
            }
        }
        if (hasLocalDeclaration) continue;
        context.StepOnce("Flatten switch section block", blockStatement);
        blockStatement->Remove();
        blockStatement->Statements().MoveTo(switchSection->Statements());
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms