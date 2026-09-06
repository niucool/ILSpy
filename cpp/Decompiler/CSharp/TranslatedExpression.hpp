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

// Port of ICSharpCode.Decompiler/CSharp/TranslatedExpression.cs -- the wrapper structs
// that carry the decompiled C# expression plus its resolver-proven annotations:
//   * `ExpressionWithILInstruction` -- the helper struct that guarantees an expression
//     carries (at least) IL-instruction annotations; `.WithILInstruction(...)` /
//     `.WithoutILInstruction()` create instances.
//   * `ExpressionWithResolveResult` -- the helper struct that guarantees both the
//     IL-instruction and the ResolveResult annotation (the resolve result is read
//     directly off the struct instead of through the annotation list).
//   * `TranslatedExpression` -- the output of the C# ExpressionBuilder: an expression
//     that has BOTH a resolve result and IL-instruction annotations.
//
// The wrapper structs are the contract the back end builders (ExpressionBuilder /
// StatementBuilder / CallBuilder, unported) pass around so no case forgets to add the
// annotations. This file is the struct layer of that family; the C# file's remaining
// members (`ConvertTo` / `ConvertToBoolean` / `UnwrapImplicitBoolConversion` and the
// private helpers -- the machinery that inserts casts and bool conversions) are
// DEFERRED with the ExpressionBuilder skeleton they consume (`expressionBuilder.
// ConvertType` / `.compilation` / `.settings` / `.LogicNot` / `.ConvertConstantValue`
// and the resolver's ResolveCast), and land with that slice.
//
// The C# structs hold GC references; the port holds non-owning raw pointers with the
// documented owner (the builder owns the AST; the node's annotation channel owns the
// resolve results, which the wrappers alias). The C# `ExpressionWithResolveResult`
// implicit-conversion chain (Expression -> ExpressionWithResolveResult ->
// ExpressionWithILInstruction) ports to the explicit accessor set (no C++ implicit
// conversions between distinct wrapper types -- every C# implicit operator use site
// calls the port's accessor or a With*/Without* factory).

#pragma once

#include "Decompiler/CSharp/Syntax/AbstractAnnotatable.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp {

// Namespace-scope aliases shared by the wrapper structs below (the member
// declarations reference the Semantics namespace; the TypeSystemAstBuilder.hpp
// alias precedent).
namespace Sem = ::ILSpy::Decompiler::Semantics;

// The non-owning holder the annotation channel stores in place of the C# bare
// IL-instruction reference: the C# `AddAnnotation(ILInstruction)` stores the
// ILInstruction object itself (a GC reference -- identity semantics); the port's
// channel owns via `shared_ptr<AnnotationBase>` while the IL tree owns instructions
// uniquely (`std::unique_ptr` slots), so storing the instruction directly is
// impossible without transferring ownership. The holder aliases the instruction
// non-owning (the IL function outlives the translated AST within one function
// translation -- the lifetime the back end guarantees) and the With*/query helpers
// below are its only access forms, so the holder never leaks into builder call sites.
class ILInstructionAnnotation final : public Syntax::AnnotationBase {
public:
    explicit ILInstructionAnnotation(IL::ILInstruction* instruction)
        : Instruction(instruction) {}

    // The aliased instruction (non-owning; the IL tree owns it).
    IL::ILInstruction* Instruction;
};

// The C# `Expression.Annotations.OfType<ILInstruction>()` query: every IL-instruction
// annotation on the node, in insertion order (one per holder).
std::vector<IL::ILInstruction*> GetILInstructions(const Syntax::AstNode& node);

// The C# `struct ExpressionWithILInstruction` (an internal helper struct, the compile
// guard "did we remember the IL-instruction annotation"). The C# `readonly Expression
// Expression` field ports to a non-owning pointer with the expression's owner (the
// caller that constructed it) documented at the construction site.
class ExpressionWithILInstruction {
public:
    // The C# struct default (a null expression -- the zero struct); only the
    // factories construct real instances.
    ExpressionWithILInstruction() = default;

    explicit ExpressionWithILInstruction(Syntax::Expression* expression);

    // The C# `readonly Expression Expression` field.
    Syntax::Expression* Expression() const { return expression_; }

    // The C# `IEnumerable<ILInstruction> ILInstructions` property -- the IL-instruction
    // annotations on the expression, in insertion order.
    std::vector<IL::ILInstruction*> ILInstructions() const {
        return GetILInstructions(*expression_);
    }

private:
    Syntax::Expression* expression_ = nullptr;
};

// The C# `struct ExpressionWithResolveResult`: the expression plus its resolve result
// (read directly, not through the annotation list). The C# parameterless ctor reads
// the annotation off the expression, falling back to `ErrorResolveResult.UnknownError`.
class ExpressionWithResolveResult {
public:
    ExpressionWithResolveResult() = default;

    // The C# `internal ExpressionWithResolveResult(Expression expression)`:
    // `ResolveResult = expression.Annotation<ResolveResult>() ??
    // ErrorResolveResult.UnknownError`.
    explicit ExpressionWithResolveResult(Syntax::Expression* expression);

    // The C# `internal ExpressionWithResolveResult(Expression expression,
    // ResolveResult resolveResult)` ctor: asserts the resolve result IS the
    // expression's annotation (the C# `Debug.Assert(expression.
    // Annotation<ResolveResult>() == resolveResult)` -- the D401 assert convention).
    // Takes the aliasing raw pointer (the C# GC reference); the node's annotation
    // channel owns the result (WithRR attached it).
    ExpressionWithResolveResult(Syntax::Expression* expression,
                                const Sem::ResolveResult* resolveResult);

    // The C# `readonly Expression Expression` field.
    Syntax::Expression* Expression() const { return expression_; }

    // The C# `readonly ResolveResult ResolveResult` field (never null: the
    // parameterless ctor form falls back to `ErrorResolveResult::UnknownError()`).
    const Sem::ResolveResult* ResolveResult() const { return resolveResult_; }

    // The C# `IType Type => ResolveResult.Type` property.
    const ILSpy::Decompiler::TypeSystem::IType& Type() const {
        return resolveResult_->Type();
    }

private:
    Syntax::Expression* expression_ = nullptr;
    const Sem::ResolveResult* resolveResult_ = nullptr;
};

// The C# `struct TranslatedExpression` -- "Output of C# ExpressionBuilder -- a
// decompiled C# expression that has both a resolve result and ILInstruction
// annotation". The resolve result is also always available as an annotation on the
// expression; the separate type exists so no builder case forgets to add it.
class TranslatedExpression {
public:
    TranslatedExpression() = default;

    // The C# `internal TranslatedExpression(Expression expression)` ctor: the
    // resolve result is read off the node (`?? ErrorResolveResult.UnknownError`).
    explicit TranslatedExpression(Syntax::Expression* expression);

    // The C# `internal TranslatedExpression(Expression expression,
    // ResolveResult resolveResult)` ctor: asserts the resolve result IS the
    // expression's annotation (the C# `Debug.Assert`), stores the raw alias.
    TranslatedExpression(Syntax::Expression* expression,
                         const Sem::ResolveResult* resolveResult);

    // The C# `readonly Expression Expression` field.
    Syntax::Expression* Expression() const { return expression_; }

    // The C# `readonly ResolveResult ResolveResult` field.
    const Sem::ResolveResult* ResolveResult() const { return resolveResult_; }

    // The C# `IEnumerable<ILInstruction> ILInstructions` property.
    std::vector<IL::ILInstruction*> ILInstructions() const {
        return GetILInstructions(*expression_);
    }

    // The C# `IType Type => ResolveResult.Type` property.
    const ILSpy::Decompiler::TypeSystem::IType& Type() const {
        return resolveResult_->Type();
    }

    // The C# `TranslatedExpression UnwrapChild(Expression descendant)`: returns a new
    // TranslatedExpression that represents the specified descendant expression. All
    // IL-instruction annotations from the current expression (and the ancestors
    // between it and the descendant) are copied onto the descendant; the descendant
    // is detached from the AST. The C# ArgumentException for a non-descendant ports
    // to `std::invalid_argument` (the established argument-exception convention).
    TranslatedExpression UnwrapChild(Syntax::Expression* descendant) const;

private:
    Syntax::Expression* expression_ = nullptr;
    const Sem::ResolveResult* resolveResult_ = nullptr;
};

}  // namespace ILSpy::Decompiler::CSharp
