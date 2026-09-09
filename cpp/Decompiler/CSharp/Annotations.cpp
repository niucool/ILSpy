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

// The out-of-line half of Annotations.hpp (the extension functions with bodies the
// tests drive). See the header for the port conventions.

#include "Decompiler/CSharp/Annotations.hpp"

#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp {

// The non-owning holder's writer: the C# `AddAnnotation(ILInstruction)` port.
static void AddILInstructionAnnotation(Syntax::AstNode& node,
                                       IL::ILInstruction* instruction) {
    node.AddAnnotation(std::make_shared<ILInstructionAnnotation>(instruction));
}

ExpressionWithILInstruction WithILInstruction(Syntax::Expression& expression,
                                              IL::ILInstruction* instruction) {
    AddILInstructionAnnotation(expression, instruction);
    return ExpressionWithILInstruction(&expression);
}

ExpressionWithILInstruction WithILInstruction(
    Syntax::Expression& expression,
    const std::vector<IL::ILInstruction*>& instructions) {
    for (IL::ILInstruction* inst : instructions)
        AddILInstructionAnnotation(expression, inst);
    return ExpressionWithILInstruction(&expression);
}

ExpressionWithILInstruction WithoutILInstruction(Syntax::Expression& expression) {
    return ExpressionWithILInstruction(&expression);
}

TranslatedStatement WithILInstruction(Syntax::Statement& statement,
                                      IL::ILInstruction* instruction) {
    AddILInstructionAnnotation(statement, instruction);
    return TranslatedStatement(&statement);
}

TranslatedStatement WithILInstruction(
    Syntax::Statement& statement, const std::vector<IL::ILInstruction*>& instructions) {
    for (IL::ILInstruction* inst : instructions)
        AddILInstructionAnnotation(statement, inst);
    return TranslatedStatement(&statement);
}

TranslatedStatement WithoutILInstruction(Syntax::Statement& statement) {
    return TranslatedStatement(&statement);
}

TranslatedExpression WithILInstruction(const ExpressionWithResolveResult& expression,
                                      IL::ILInstruction* instruction) {
    // The C# annotates the WRAPPED expression (`expression.Expression
    // .AddAnnotation(instruction)`) and re-wraps it.
    AddILInstructionAnnotation(*expression.Expression(), instruction);
    return TranslatedExpression(expression.Expression(), expression.ResolveResult());
}

TranslatedExpression WithILInstruction(
    const ExpressionWithResolveResult& expression,
    const std::vector<IL::ILInstruction*>& instructions) {
    for (IL::ILInstruction* inst : instructions)
        AddILInstructionAnnotation(*expression.Expression(), inst);
    return TranslatedExpression(expression.Expression(), expression.ResolveResult());
}

TranslatedExpression WithoutILInstruction(const ExpressionWithResolveResult& expression) {
    return TranslatedExpression(expression.Expression(), expression.ResolveResult());
}

TranslatedExpression WithILInstruction(const TranslatedExpression& expression,
                                       IL::ILInstruction* instruction) {
    // The C# annotates the wrapped expression and returns the same wrapper (the
    // `AddAnnotation` mutates the node).
    AddILInstructionAnnotation(*expression.Expression(), instruction);
    return TranslatedExpression(expression.Expression(), expression.ResolveResult());
}

ExpressionWithResolveResult WithRR(Syntax::Expression& expression,
                                   std::shared_ptr<Sem::ResolveResult> resolveResult) {
    expression.AddAnnotation(resolveResult);
    return ExpressionWithResolveResult(&expression, resolveResult.get());
}

TranslatedExpression WithRR(const ExpressionWithILInstruction& expression,
                            std::shared_ptr<Sem::ResolveResult> resolveResult) {
    // The C# annotates the wrapped expression, then re-wraps it as a
    // TranslatedExpression.
    expression.Expression()->AddAnnotation(resolveResult);
    return TranslatedExpression(expression.Expression(), resolveResult.get());
}

const ILSpy::Decompiler::TypeSystem::ISymbol* GetSymbol(const Syntax::AstNode& node) {
    const Sem::ResolveResult* rr = node.Annotation<Sem::ResolveResult>();
    if (const auto* mgrr =
            dynamic_cast<const Resolver::MethodGroupResolveResult*>(rr)) {
        return mgrr->ChosenMethod();
    }
    return rr != nullptr ? ILSpy::Decompiler::TypeSystem::GetSymbol(*rr) : nullptr;
}

const Sem::ResolveResult* GetResolveResult(const Syntax::AstNode& node) {
    const Sem::ResolveResult* rr = node.Annotation<Sem::ResolveResult>();
    return rr != nullptr ? rr : &Sem::ErrorResolveResult::UnknownError();
}

IL::ILVariable* GetILVariable(
    const Syntax::IdentifierExpression& expression) {
    if (const auto* rr = expression.Annotation<ILVariableResolveResult>())
        return rr->Variable();
    return nullptr;
}

IL::ILVariable* GetILVariable(const Syntax::VariableInitializer& initializer) {
    if (const auto* rr = initializer.Annotation<ILVariableResolveResult>())
        return rr->Variable();
    return nullptr;
}

IL::ILVariable* GetILVariable(const Syntax::ForeachStatement& loop) {
    if (const auto* rr = loop.Annotation<ILVariableResolveResult>())
        return rr->Variable();
    return nullptr;
}

Syntax::VariableInitializer* WithILVariable(Syntax::VariableInitializer& initializer,
                                            const IL::ILVariablePtr& variable) {
    initializer.AddAnnotation(
        std::make_shared<ILVariableResolveResult>(variable, variable->Type));
    return &initializer;
}

Syntax::ForeachStatement* WithILVariable(Syntax::ForeachStatement& loop,
                                         const IL::ILVariablePtr& variable) {
    loop.AddAnnotation(
        std::make_shared<ILVariableResolveResult>(variable, variable->Type));
    return &loop;
}

// The C# `ide.AddAnnotation(localFunction)` over the holder channel.
void WithILFunction(Syntax::AstNode& node, IL::ILFunction* function) {
    node.AddAnnotation(std::make_shared<ILFunctionAnnotation>(function));
}

// The C# `node.Annotation<ILFunction>()` over the holder channel.
IL::ILFunction* GetILFunction(const Syntax::AstNode& node) {
    const auto* annotation = node.Annotation<ILFunctionAnnotation>();
    return annotation ? annotation->Function : nullptr;
}

// The C# `UseImplicitlyTypedOutAnnotation.Instance` shared handle (the port's
// annotation channel owns via shared_ptr; the aliasing-shared_ptr convention --
// the static Instance() object aliased with a no-op deleter).
std::shared_ptr<UseImplicitlyTypedOutAnnotation>
UseImplicitlyTypedOutAnnotation::SharedInstance() {
    return std::shared_ptr<UseImplicitlyTypedOutAnnotation>(
        const_cast<UseImplicitlyTypedOutAnnotation*>(&Instance()),
        [](UseImplicitlyTypedOutAnnotation*) {});
}

}  // namespace ILSpy::Decompiler::CSharp
