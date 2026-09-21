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

// Port of ICSharpCode.Decompiler/CSharp/Annotations.cs -- the annotation-holder classes
// the back end builders attach to AST nodes and the `AnnotationExtensions` extension
// surface that binds them:
//   * The holders: `ILVariableResolveResult` (a variable-reference ResolveResult),
//     `ForeachAnnotation` (the GetEnumerator/MoveNext/get_Current calls of a foreach),
//     `ImplicitReturnAnnotation` (the implicitly executed return of a function block),
//     `MemberInitializerInOtherConstructorsAnnotation` (the discarded copies of a lifted
//     member initializer, kept for the SequencePointBuilder),
//     `ImplicitConversionAnnotation` (an omitted implicit user-defined conversion),
//     `QueryGroupClauseAnnotation` / `QueryJoinClauseAnnotation` (the lambdas of a query
//     clause), `UseImplicitlyTypedOutAnnotation` (an out variable that can be declared
//     implicitly typed), and the currently-unused `LdTokenAnnotation` marker.
//   * The extension surface: `WithILInstruction` / `WithoutILInstruction` /
//     `WithRR` (the binding factories returning the TranslatedExpression/
//     TranslatedStatement wrapper structs), `GetSymbol` / `GetResolveResult` /
//     `GetILVariable` (the queries), `WithILVariable` (the variable-attachment writers),
//     and `CopyAnnotationsFrom` / `CopyInstructionsFrom` (the copy helpers).
//
// C# extension methods port to free functions in the `ILSpy::Decompiler::CSharp`
// namespace (the TypeSystemExtensions / SyntaxExtensions precedent). The C#
// `IEnumerable<ILInstruction>` parameters port to `const std::vector<IL::ILInstruction*>&`
// and the C# GC-owned references to the documented non-owning / shared-pointer forms.

#pragma once

#include "Decompiler/CSharp/Syntax/AbstractAnnotatable.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/CSharp/TranslatedStatement.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"

#include <cassert>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::CSharp {

// ---------------------------------------------------------------------------
// The annotation-holder classes (in the C# file order).
// ---------------------------------------------------------------------------

// The C# `public class LdTokenAnnotation { }` -- "Currently unused; we'll probably use
// the LdToken ILInstruction as annotation instead when LdToken support gets
// reimplemented." Ported verbatim as the empty marker.
class LdTokenAnnotation final : public Syntax::AnnotationBase {};

// The C# `public class ILVariableResolveResult : ResolveResult` -- represents a
// reference to a local variable. The C# `public readonly ILVariable Variable` field
// ports to a shared handle (the C# GC keeps the variable alive through the result;
// the port's `ILVariablePtr` shared_ptr is that ownership stand-in -- the D271
// reference-handle convention).
class ILVariableResolveResult : public Sem::ResolveResult {
public:
    // The C# `public ILVariableResolveResult(ILVariable v) : base(v.Type)` -- the
    // variable's own type is the result type (a null `v` dereferences in the C# base
    // call; the port asserts).
    explicit ILVariableResolveResult(const IL::ILVariablePtr& variable)
        : ResolveResult(AssertVariable(variable)->Type),
          variable_(variable) {}

    // The C# `public ILVariableResolveResult(ILVariable v, IType type) : base(type)`
    // -- the explicit type form; the C# ctor throws `ArgumentNullException` for a
    // null variable (the invalid_argument convention).
    ILVariableResolveResult(const IL::ILVariablePtr& variable,
                            ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : ResolveResult(std::move(type)) {
        if (!variable)
            throw std::invalid_argument("ILVariableResolveResult: variable must not be null");
        variable_ = variable;
    }

    // The C# `public readonly ILVariable Variable` field (the C# GC reference is a
    // mutable handle; the port returns the shared_ptr's raw pointer).
    IL::ILVariable* Variable() const { return variable_.get(); }

protected:
    std::string ClassName() const override { return "ILVariableResolveResult"; }

    // The C# `ShallowClone` is inherited from `ResolveResult` (a `MemberwiseClone`
    // preserving the runtime type); the port mirrors it so a cloned
    // `ILVariableResolveResult` stays an `ILVariableResolveResult`.
    std::unique_ptr<Sem::ResolveResult> ShallowClone() const override {
        return std::make_unique<ILVariableResolveResult>(*this);
    }

private:
    IL::ILVariablePtr variable_;

    static const IL::ILVariablePtr& AssertVariable(const IL::ILVariablePtr& variable) {
        assert(variable != nullptr &&
               "ILVariableResolveResult: variable must not be null");
        return variable;
    }
};

// The C# `public class ForeachAnnotation` -- annotates a ForeachStatement with the
// instructions for the GetEnumerator, MoveNext and get_Current calls. The three
// C# `readonly ILInstruction` fields port to non-owning pointers (the IL function
// owns the instructions -- the ILInstructionAnnotation lifetime convention).
class ForeachAnnotation final : public Syntax::AnnotationBase {
public:
    ForeachAnnotation(IL::ILInstruction* getEnumeratorCall,
                      IL::ILInstruction* moveNextCall, IL::ILInstruction* getCurrentCall)
        : GetEnumeratorCall(getEnumeratorCall),
          MoveNextCall(moveNextCall),
          GetCurrentCall(getCurrentCall) {}

    IL::ILInstruction* GetEnumeratorCall;
    IL::ILInstruction* MoveNextCall;
    IL::ILInstruction* GetCurrentCall;
};

// The C# `public class ImplicitReturnAnnotation` -- annotates the top-level block
// statement of a function with the implicitly executed return/yield break.
class ImplicitReturnAnnotation final : public Syntax::AnnotationBase {
public:
    explicit ImplicitReturnAnnotation(IL::Leave* leave) : Leave(leave) {}

    IL::Leave* Leave;
};

// The C# `public class MemberInitializerInOtherConstructorsAnnotation` -- annotates a
// field/auto-property/event initializer that the decompiler lifted to the declaration
// site with the copies of the same initializer found in the other constructors. The
// C# annotation object keeps the discarded copies alive (GC); the port's holder owns
// them (`unique_ptr` -- the copies are built by the lifting builder and moved in), so
// the SequencePointBuilder consumer can read them later.
class MemberInitializerInOtherConstructorsAnnotation final
    : public Syntax::AnnotationBase {
public:
    explicit MemberInitializerInOtherConstructorsAnnotation(
        std::vector<std::unique_ptr<Syntax::Expression>> initializers)
        : initializers_(std::move(initializers)) {}

    // The C# `public readonly IReadOnlyList<Expression> Initializers` field -- the
    // owned copies, exposed as non-owning pointers (the read form every consumer
    // uses).
    std::vector<Syntax::Expression*> Initializers() const {
        std::vector<Syntax::Expression*> view;
        view.reserve(initializers_.size());
        for (const auto& e : initializers_)
            view.push_back(e.get());
        return view;
    }

private:
    std::vector<std::unique_ptr<Syntax::Expression>> initializers_;
};

// The C# `public class ImplicitConversionAnnotation` -- annotates an expression when
// an implicit user-defined conversion was omitted (the CallBuilder re-introduces it
// when the conversion turns out to be necessary for overload resolution).
class ImplicitConversionAnnotation final : public Syntax::AnnotationBase {
public:
    explicit ImplicitConversionAnnotation(
        std::shared_ptr<Sem::ConversionResolveResult> conversionResolveResult)
        : ConversionResolveResult(std::move(conversionResolveResult)) {}

    // The C# `public readonly ConversionResolveResult ConversionResolveResult` field
    // (the owning shared handle -- the C# GC ownership stand-in).
    std::shared_ptr<Sem::ConversionResolveResult> ConversionResolveResult;

    // The C# `public IType TargetType => ConversionResolveResult.Type` property.
    const ILSpy::Decompiler::TypeSystem::IType& TargetType() const {
        return ConversionResolveResult->Type();
    }
};

// The C# `public class QueryGroupClauseAnnotation` -- annotates a QueryGroupClause
// with the ILFunctions of each (implicit lambda) expression.
class QueryGroupClauseAnnotation final : public Syntax::AnnotationBase {
public:
    QueryGroupClauseAnnotation(const IL::ILFunction* key, const IL::ILFunction* projection)
        : KeyLambda(key), ProjectionLambda(projection) {}

    // The C# `public readonly ILFunction KeyLambda` field.
    const IL::ILFunction* KeyLambda;
    // The C# `public readonly ILFunction ProjectionLambda` field.
    const IL::ILFunction* ProjectionLambda;
};

// The C# `public class QueryJoinClauseAnnotation` -- annotates a QueryJoinClause with
// the ILFunctions of each (implicit lambda) expression.
class QueryJoinClauseAnnotation final : public Syntax::AnnotationBase {
public:
    QueryJoinClauseAnnotation(const IL::ILFunction* on, const IL::ILFunction* equals)
        : OnLambda(on), EqualsLambda(equals) {}

    // The C# `public readonly ILFunction OnLambda` field.
    const IL::ILFunction* OnLambda;
    // The C# `public readonly ILFunction EqualsLambda` field.
    const IL::ILFunction* EqualsLambda;
};

// The C# `public class UseImplicitlyTypedOutAnnotation` -- annotates an out
// DirectionExpression if the out variable can be declared implicitly typed.
class UseImplicitlyTypedOutAnnotation final : public Syntax::AnnotationBase {
public:
    // The C# `public static readonly UseImplicitlyTypedOutAnnotation Instance` (the
    // Meyers-singleton form preserving the C# reference identity).
    static const UseImplicitlyTypedOutAnnotation& Instance() {
        static const UseImplicitlyTypedOutAnnotation instance;
        return instance;
    }
};

// The non-owning shared handle to the `UseImplicitlyTypedOutAnnotation` singleton
// (the annotation channel stores `shared_ptr`; the C# singleton is a GC reference).
inline std::shared_ptr<UseImplicitlyTypedOutAnnotation> UseImplicitlyTypedOutAnnotationHandle() {
    return std::shared_ptr<UseImplicitlyTypedOutAnnotation>(
        const_cast<UseImplicitlyTypedOutAnnotation*>(&UseImplicitlyTypedOutAnnotation::Instance()),
        [](UseImplicitlyTypedOutAnnotation*) {});
}

// ---------------------------------------------------------------------------
// The AnnotationExtensions extension surface (free functions in this namespace).
// ---------------------------------------------------------------------------

// The C# `internal static ExpressionWithILInstruction WithILInstruction(
// this Expression expression, ILInstruction instruction)`.
ExpressionWithILInstruction WithILInstruction(Syntax::Expression& expression,
                                              IL::ILInstruction* instruction);

// The C# list overload (`IEnumerable<ILInstruction> instructions`).
ExpressionWithILInstruction WithILInstruction(
    Syntax::Expression& expression,
    const std::vector<IL::ILInstruction*>& instructions);

// The C# `internal static ExpressionWithILInstruction WithoutILInstruction(
// this Expression expression)` -- a wrap-only factory (no annotation change).
ExpressionWithILInstruction WithoutILInstruction(Syntax::Expression& expression);

// The C# `internal static TranslatedStatement WithILInstruction(
// this Statement statement, ILInstruction instruction)`.
TranslatedStatement WithILInstruction(Syntax::Statement& statement,
                                      IL::ILInstruction* instruction);

// The C# list overload.
TranslatedStatement WithILInstruction(
    Syntax::Statement& statement, const std::vector<IL::ILInstruction*>& instructions);

// The C# `internal static TranslatedStatement WithoutILInstruction(
// this Statement statement)` -- a wrap-only factory (no annotation change).
TranslatedStatement WithoutILInstruction(Syntax::Statement& statement);

// The C# `internal static TranslatedExpression WithILInstruction(
// this ExpressionWithResolveResult expression, ILInstruction instruction)`.
TranslatedExpression WithILInstruction(const ExpressionWithResolveResult& expression,
                                       IL::ILInstruction* instruction);

// The C# list overload.
TranslatedExpression WithILInstruction(
    const ExpressionWithResolveResult& expression,
    const std::vector<IL::ILInstruction*>& instructions);

// The C# `internal static TranslatedExpression WithoutILInstruction(
// this ExpressionWithResolveResult expression)` -- re-wraps the expression's own
// resolve result without touching the annotations.
TranslatedExpression WithoutILInstruction(const ExpressionWithResolveResult& expression);

// The C# `internal static TranslatedExpression WithILInstruction(
// this TranslatedExpression expression, ILInstruction instruction)` (Annotations.cs
// line 109): adds the annotation to the expression and returns the same wrapper.
TranslatedExpression WithILInstruction(const TranslatedExpression& expression,
                                       IL::ILInstruction* instruction);

// The C# `internal static ExpressionWithResolveResult WithRR(
// this Expression expression, ResolveResult resolveResult)`: adds the resolve result
// as an annotation on the expression and returns the bound wrapper.
ExpressionWithResolveResult WithRR(Syntax::Expression& expression,
                                   std::shared_ptr<Sem::ResolveResult> resolveResult);

// The C# `internal static TranslatedExpression WithRR(
// this ExpressionWithILInstruction expression, ResolveResult resolveResult)`.
TranslatedExpression WithRR(const ExpressionWithILInstruction& expression,
                            std::shared_ptr<Sem::ResolveResult> resolveResult);

// The C# `public static ISymbol? GetSymbol(this AstNode node)` -- the symbol
// associated with the node's resolve-result annotation (a method group resolves to
// its chosen method; everything else through the resolve result's own symbol).
const ILSpy::Decompiler::TypeSystem::ISymbol* GetSymbol(const Syntax::AstNode& node);

// The C# `public static ResolveResult GetResolveResult(this AstNode node)` -- the
// node's resolve-result annotation, or `ErrorResolveResult::UnknownError()` when
// none is associated.
const Sem::ResolveResult* GetResolveResult(const Syntax::AstNode& node);

// The owning shared handle behind the node's resolve-result annotation (the port's
// resolve results are not `enable_shared_from_this`; the annotation channel owns the
// shared handle the C# GC reference aliases). Null when no resolve-result annotation
// is present.
std::shared_ptr<Sem::ResolveResult> GetSharedResolveResult(const Syntax::AstNode& node);

// The C# `public static ILVariable? GetILVariable(this IdentifierExpression expr)` /
// `(this VariableInitializer vi)` / `(this ForeachStatement loop)` -- the ILVariable
// carried by the node's `ILVariableResolveResult` annotation, or null.
IL::ILVariable* GetILVariable(const Syntax::IdentifierExpression& expression);
IL::ILVariable* GetILVariable(const Syntax::VariableInitializer& initializer);
IL::ILVariable* GetILVariable(const Syntax::ForeachStatement& loop);

// The C# `public static VariableInitializer WithILVariable(
// this VariableInitializer vi, ILVariable v)` -- attaches the variable as the
// initializer's `ILVariableResolveResult` annotation.
Syntax::VariableInitializer* WithILVariable(Syntax::VariableInitializer& initializer,
                                            const IL::ILVariablePtr& variable);

// The C# `public static ForeachStatement WithILVariable(
// this ForeachStatement loop, ILVariable v)`.
Syntax::ForeachStatement* WithILVariable(Syntax::ForeachStatement& loop,
                                         const IL::ILVariablePtr& variable);

// The C# `public static T CopyAnnotationsFrom<T>(this T node, AstNode other)
// where T : AstNode` -- copies all annotations from `other` to `node` (the same
// objects, the C# reference-sharing), skipping the trivia holder (each trivia's
// Parent points at its single owning node -- the trivia is deep-copied onto the
// target by `CopyTriviaFrom` instead). The C# generic `where T : AstNode` ports to
// a template over the node pointer type.
template <class T>
T* CopyAnnotationsFrom(T* node, const Syntax::AstNode& other) {
    for (const auto& annotation : other.SharedAnnotations()) {
        // The trivia holder must not be shared between nodes: each trivia's Parent
        // points at its single owning node. Trivia is deep-copied onto the target
        // instead.
        if (dynamic_cast<const Syntax::NodeTrivia*>(annotation.get()) != nullptr)
            continue;
        node->AddAnnotation(annotation);
    }
    node->CopyTriviaFrom(other);
    return node;
}

// The C# `public static T CopyInstructionsFrom<T>(this T node, AstNode other)
// where T : AstNode` -- copies all IL-instruction annotations from `other` to
// `node` (the same instructions, through the non-owning holder channel).
template <class T>
T* CopyInstructionsFrom(T* node, const Syntax::AstNode& other) {
    for (IL::ILInstruction* inst : GetILInstructions(other))
        node->AddAnnotation(std::make_shared<ILInstructionAnnotation>(inst));
    return node;
}

}  // namespace ILSpy::Decompiler::CSharp
