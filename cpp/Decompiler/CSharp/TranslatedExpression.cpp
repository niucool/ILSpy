// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// The out-of-line half of TranslatedExpression.hpp (the wrapper-struct ctors and the
// UnwrapChild walk). See the header for the port conventions.

#include "Decompiler/CSharp/TranslatedExpression.hpp"

#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp {

std::vector<IL::ILInstruction*> GetILInstructions(const Syntax::AstNode& node) {
    std::vector<IL::ILInstruction*> result;
    for (Syntax::AnnotationBase* annotation : node.Annotations()) {
        if (auto* holder =
                dynamic_cast<ILInstructionAnnotation*>(annotation)) {
            result.push_back(holder->Instruction);
        }
    }
    return result;
}

ExpressionWithILInstruction::ExpressionWithILInstruction(Syntax::Expression* expression)
    : expression_(expression) {
    assert(expression_ != nullptr &&
           "ExpressionWithILInstruction: expression must not be null");
}

ExpressionWithResolveResult::ExpressionWithResolveResult(Syntax::Expression* expression)
    : expression_(expression),
      resolveResult_(expression->Annotation<Sem::ResolveResult>()) {
    assert(expression_ != nullptr &&
           "ExpressionWithResolveResult: expression must not be null");
    if (resolveResult_ == nullptr)
        resolveResult_ = &Sem::ErrorResolveResult::UnknownError();
}

ExpressionWithResolveResult::ExpressionWithResolveResult(
    Syntax::Expression* expression, const Sem::ResolveResult* resolveResult)
    : expression_(expression),
      resolveResult_(resolveResult) {
    assert(expression_ != nullptr &&
           "ExpressionWithResolveResult: expression must not be null");
    assert(resolveResult_ != nullptr &&
           "ExpressionWithResolveResult: resolveResult must not be null");
    assert(expression_->Annotation<Sem::ResolveResult>() == resolveResult_ &&
           "ExpressionWithResolveResult: the resolve result must be the expression's "
           "annotation");
}

TranslatedExpression::TranslatedExpression(Syntax::Expression* expression)
    : expression_(expression),
      resolveResult_(expression->Annotation<Sem::ResolveResult>()) {
    assert(expression_ != nullptr &&
           "TranslatedExpression: expression must not be null");
    if (resolveResult_ == nullptr)
        resolveResult_ = &Sem::ErrorResolveResult::UnknownError();
}

TranslatedExpression::TranslatedExpression(
    Syntax::Expression* expression, const Sem::ResolveResult* resolveResult)
    : expression_(expression),
      resolveResult_(resolveResult) {
    assert(expression_ != nullptr &&
           "TranslatedExpression: expression must not be null");
    assert(resolveResult_ != nullptr &&
           "TranslatedExpression: resolveResult must not be null");
    assert(expression_->Annotation<Sem::ResolveResult>() == resolveResult_ &&
           "TranslatedExpression: the resolve result must be the expression's "
           "annotation");
}

TranslatedExpression TranslatedExpression::UnwrapChild(
    Syntax::Expression* descendant) const {
    if (descendant == expression_)
        return *this;
    for (Syntax::AstNode* parent = descendant->Parent(); parent != nullptr;
         parent = parent->Parent()) {
        for (IL::ILInstruction* inst : GetILInstructions(*parent))
            descendant->AddAnnotation(std::make_shared<ILInstructionAnnotation>(inst));
        if (parent == expression_)
            return TranslatedExpression(Syntax::Detach(descendant));
    }
    throw std::invalid_argument(
        "descendant must be a descendant of the current node");
}

}  // namespace ILSpy::Decompiler::CSharp
