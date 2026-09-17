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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/CustomPatterns.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/Semantics/InvocationResolveResult.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {

// The C# `bool IsStringParameter(IParameter p)` local function (lines
// 396-402): a `params` array parameter's element type is unwrapped before the
// known-type check (`if (p.IsParams && ty.Kind == TypeKind.Array) ty = ((ArrayType)
// ty).ElementType`).
bool IsStringParameter(const TS::IParameter& p) {
    const TS::IType& type = p.Type();
    if (p.IsParams() && type.Kind() == TS::TypeKind::Array) {
        const auto* arrayType = dynamic_cast<const TS::ArrayType*>(&type);
        if (arrayType != nullptr && arrayType->Element() != nullptr)
            return TS::IsKnownType(*arrayType->Element(), TS::KnownTypeCode::String);
    }
    return TS::IsKnownType(type, TS::KnownTypeCode::String);
}

// The port's `typeHandleOnTypeOfPattern.IsMatch` stand-in (the C# `static readonly
// MemberReferenceExpression typeHandleOnTypeOfPattern`, lines 48-56): a
// MemberReferenceExpression named "TypeHandle" whose target is either `typeof(...)`
// (a TypeOfExpression) or `__reftype(...)` (the RefType UndocumentedExpression with
// exactly one argument).
bool IsTypeHandleOnTypeOf(Syntax::Expression* expr) {
    auto* memberRef = dynamic_cast<Syntax::MemberReferenceExpression*>(expr);
    if (memberRef == nullptr || memberRef->MemberName() != "TypeHandle")
        return false;
    Syntax::Expression* target = memberRef->Target();
    if (dynamic_cast<Syntax::TypeOfExpression*>(target) != nullptr)
        return true;
    auto* undocumented = dynamic_cast<Syntax::UndocumentedExpression*>(target);
    return undocumented != nullptr
        && undocumented->UndocumentedExpressionType()
               == Syntax::UndocumentedExpressionType::RefType
        && undocumented->Arguments().Count() == 1;
}

namespace PM = Syntax::PatternMatching;

// Owns the nodes of the `getMethodOrConstructorFromHandlePattern` rebuilt for one
// `VisitCastExpression` call. The C# pattern is `static readonly` (process
// lifetime); the port rebuilds an equivalent tree per call and keeps every node in
// this holder, so the non-owning child pointers the pattern nodes carry stay valid
// for the whole match (the PatternStatementTransform PatternTree precedent). AST
// nodes do not own their children in the port, so every node created here is kept.
class CustomPatternTree {
public:
    template <class T, class... Args>
    T* Make(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* result = node.get();
        nodes_.push_back(std::move(node));
        return result;
    }

    // The generated `implicit operator <TNode>(Pattern)`: wrap a pattern in a
    // placeholder so it can occupy an AST slot while still matching.
    template <class TNode>
    TNode* Wrap(std::shared_ptr<PM::Pattern> pattern) {
        return Make<Syntax::PatternPlaceholderNode<TNode>>(std::move(pattern));
    }

private:
    std::vector<std::unique_ptr<PM::INode>> nodes_;
};

// The C# `static readonly Expression getMethodOrConstructorFromHandlePattern`
// (lines 520-527): the methodof shape
// `(MethodInfo | ConstructorInfo)MethodBase.GetMethodFromHandle(
//      ldtoken(method).MethodHandle, typeof(declaringType).TypeHandle)`.
// Built into `tree` each call.
Syntax::CastExpression* BuildGetMethodOrConstructorFromHandlePattern(
    CustomPatternTree& tree) {
    // The cast's type: a `Choice` of the two method-reflection types, wrapped as an
    // `AstType` placeholder (the C# `new Choice { TypePattern, TypePattern }.ToType()`).
    auto choice = std::make_shared<PM::Choice>();
    choice->Add(tree.Make<TypePattern>("System.Reflection", "MethodInfo"));
    choice->Add(tree.Make<TypePattern>("System.Reflection", "ConstructorInfo"));

    // `TypeReferenceExpression(TypePattern(MethodBase)).GetMethodFromHandle`.
    auto* typeReference = tree.Make<Syntax::TypeReferenceExpression>(
        tree.Wrap<Syntax::AstType>(
            std::make_shared<TypePattern>("System.Reflection", "MethodBase")));
    auto* getMethodFromHandle = tree.Make<Syntax::MemberReferenceExpression>(
        typeReference, std::string("GetMethodFromHandle"));
    auto* invocation = tree.Make<Syntax::InvocationExpression>(getMethodFromHandle);

    // `ldtoken(method).MethodHandle`, capturing the token argument under "method"
    // and the whole member reference under "ldtokenNode".
    auto* methodHandle = tree.Make<Syntax::MemberReferenceExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<LdTokenPattern>("method")),
        std::string("MethodHandle"));
    invocation->Arguments().Add(tree.Wrap<Syntax::Expression>(
        std::make_shared<PM::NamedNode>("ldtokenNode", methodHandle)));

    // The optional second argument `typeof(declaringType).TypeHandle`.
    auto* declaringTypeOf = tree.Make<Syntax::TypeOfExpression>(
        tree.Wrap<Syntax::AstType>(std::make_shared<PM::AnyNode>("declaringType")));
    auto* declaringTypeHandle = tree.Make<Syntax::MemberReferenceExpression>(
        declaringTypeOf, std::string("TypeHandle"));
    invocation->Arguments().Add(tree.Wrap<Syntax::Expression>(
        std::make_shared<PM::OptionalNode>(declaringTypeHandle)));

    return tree.Make<Syntax::CastExpression>(
        tree.Wrap<Syntax::AstType>(std::move(choice)), invocation);
}

} // namespace

// The C# `internal static bool HasCheckedEquivalent(IMethod method)`
// (ReplaceMethodCallsWithOperators.cs lines 264-271).
bool ReplaceMethodCallsWithOperators::HasCheckedEquivalent(
    const TS::IMethod& method) {
    std::string name = method.Name();
    if (name.rfind("op_", 0) == 0)
        name = "op_Checked" + name.substr(3);
    // The C# `method.DeclaringType.GetMethods(m => m.IsOperator && m.Name == name).Any()`.
    TS::ITypePtr declaringType = method.DeclaringType();
    if (!declaringType)
        return false;
    auto methods = declaringType->GetMethods([&name](const TS::IMethod* m) {
        return m != nullptr && m->IsOperator() && m->Name() == name;
    });
    return !methods.empty();
}

// The port's ToStringCallPattern.Match stand-in (the C# `static readonly Pattern
// ToStringCallPattern` Choice, lines 339-351).
ReplaceMethodCallsWithOperators::ToStringCallMatch
ReplaceMethodCallsWithOperators::MatchToStringCallPattern(Syntax::Expression* expr) {
    ToStringCallMatch match;
    if (expr == nullptr)
        return match;
    // Pattern 1: `target.ToString()` -- InvocationExpression(MemberReference
    // Expression(AnyNode("target"), "ToString")).
    if (auto* invocation = dynamic_cast<Syntax::InvocationExpression*>(expr)) {
        auto* memberRef =
            dynamic_cast<Syntax::MemberReferenceExpression*>(invocation->Target());
        if (memberRef != nullptr && memberRef->MemberName() == "ToString") {
            match.call = invocation;
            match.target = memberRef->Target();
            match.nullConditional = false;
            return match;
        }
    }
    // Pattern 2: `target?.ToString()` -- UnaryOperatorExpression(
    // NullConditionalRewrap, InvocationExpression(MemberReferenceExpression(
    // UnaryOperatorExpression(NullConditional, AnyNode("target")), "ToString"))).
    if (auto* rewrap = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr)) {
        if (rewrap->Operator() == Syntax::UnaryOperatorType::NullConditionalRewrap) {
            auto* invocation =
                dynamic_cast<Syntax::InvocationExpression*>(rewrap->Expression());
            auto* memberRef =
                invocation != nullptr
                    ? dynamic_cast<Syntax::MemberReferenceExpression*>(
                          invocation->Target())
                    : nullptr;
            if (memberRef != nullptr && memberRef->MemberName() == "ToString") {
                if (auto* nullCheck =
                        dynamic_cast<Syntax::UnaryOperatorExpression*>(
                            memberRef->Target())) {
                    if (nullCheck->Operator()
                            == Syntax::UnaryOperatorType::NullConditional) {
                        match.call = invocation;
                        match.target = nullCheck->Expression();
                        match.nullConditional = true;
                        return match;
                    }
                }
            }
        }
    }
    return match;
}

// The C# `bool IsStringConcat(IParameterizedMember member)` (lines 344-351).
bool ReplaceMethodCallsWithOperators::IsStringConcat(
    const TS::IParameterizedMember& member) {
    if (member.Name() != "Concat")
        return false;
    TS::ITypePtr declaringType = member.DeclaringType();
    return declaringType != nullptr
        && TS::IsKnownType(*declaringType, TS::KnownTypeCode::String);
}

// The C# `bool CheckArgumentsForStringConcat(Expression[] arguments)` (lines
// 282-330).
bool ReplaceMethodCallsWithOperators::CheckArgumentsForStringConcat(
    const std::vector<Syntax::Expression*>& arguments) {
    if (arguments.size() < 2)
        return false;

    for (Syntax::Expression* arg : arguments) {
        if (dynamic_cast<Syntax::NamedArgumentExpression*>(arg) != nullptr)
            return false;
    }

    // The evaluation order when the object.ToString() calls happen is a mess; no
    // matter which compiler recompiles the output, every implicit ToString()
    // except for the last must be free of side effects. The C# `arguments
    // .SkipLast(1)` walks every argument but the last.
    for (size_t i = 0; i + 1 < arguments.size(); i++) {
        const Semantics::ResolveResult* rr =
            CSharp::GetResolveResult(*arguments[i]);
        if (!ToStringIsKnownEffectFree(rr->Type()))
            return false;
    }
    for (Syntax::Expression* arg : arguments) {
        const Semantics::ResolveResult* rr = CSharp::GetResolveResult(*arg);
        const auto* invocationRR =
            dynamic_cast<const Semantics::InvocationResolveResult*>(rr);
        if (invocationRR != nullptr && IsStringConcat(*invocationRR->Member())) {
            // Roslyn + mcs also flatten nested string.Concat() invocations within
            // an operator+ use, which causes it to use the incorrect evaluation
            // order despite the code using an explicit string.Concat() call. This
            // problem is avoided if the outer call remains string.Concat() as well.
            return false;
        }
        if (rr->Type().IsByRefLike()) {
            // ref structs cannot be converted to object for use with +
            return false;
        }
    }

    // One of the first two arguments must be string, otherwise the + operator
    // won't resolve to a string concatenation.
    const Semantics::ResolveResult* rr0 = CSharp::GetResolveResult(*arguments[0]);
    const Semantics::ResolveResult* rr1 = CSharp::GetResolveResult(*arguments[1]);
    return TS::IsKnownType(rr0->Type(), TS::KnownTypeCode::String)
        || TS::IsKnownType(rr1->Type(), TS::KnownTypeCode::String);
}

// The C# `static bool ToStringIsKnownEffectFree(IType type)` (lines 406-427).
bool ReplaceMethodCallsWithOperators::ToStringIsKnownEffectFree(
    const TS::IType& type) {
    // The C# `NullableType.GetUnderlyingType(type)`.
    const TS::IType& unwrapped = TS::GetUnderlyingType(type);
    const TS::ITypeDefinition* definition = unwrapped.GetDefinition();
    if (definition == nullptr)
        return false;
    using TS::KnownTypeCode;
    switch (definition->KnownTypeCode()) {
        case KnownTypeCode::Boolean:
        case KnownTypeCode::Char:
        case KnownTypeCode::SByte:
        case KnownTypeCode::Byte:
        case KnownTypeCode::Int16:
        case KnownTypeCode::UInt16:
        case KnownTypeCode::Int32:
        case KnownTypeCode::UInt32:
        case KnownTypeCode::Int64:
        case KnownTypeCode::UInt64:
        case KnownTypeCode::Single:
        case KnownTypeCode::Double:
        case KnownTypeCode::Decimal:
        case KnownTypeCode::IntPtr:
        case KnownTypeCode::UIntPtr:
        case KnownTypeCode::String:
            return true;
        default:
            return false;
    }
}

// The C# `internal static Expression RemoveRedundantToStringInConcat(
// Expression expr, IMethod concatMethod, bool isLastArgument)` (lines 353-404).
Syntax::Expression* ReplaceMethodCallsWithOperators::RemoveRedundantToStringInConcat(
    Syntax::Expression* expr, const TS::IMethod& concatMethod,
    bool isLastArgument) {
    ToStringCallMatch m = MatchToStringCallPattern(expr);
    if (m.call == nullptr)
        return expr;

    // The C# `if (!concatMethod.Parameters.All(IsStringParameter))`.
    for (const TS::IParameter* p : concatMethod.Parameters()) {
        if (p == nullptr || !IsStringParameter(*p))
            return expr;
    }

    // The C# `var toStringMethod = m.Get<Expression>("call").Single().GetSymbol()
    // as IMethod` -- the invocation's resolve-result symbol.
    const TS::ISymbol* symbol = CSharp::GetSymbol(*m.call);
    const TS::IMethod* toStringMethod =
        symbol != nullptr
            ? dynamic_cast<const TS::IMethod*>(symbol)
            : nullptr;

    // The C# `var type = target.GetResolveResult().Type`.
    const Semantics::ResolveResult* targetRR =
        m.target != nullptr ? CSharp::GetResolveResult(*m.target) : nullptr;
    if (targetRR == nullptr)
        return expr;
    const TS::IType& type = targetRR->Type();

    if (type.IsByRefLike()) {
        // ref structs cannot be converted to object for use with +
        return expr;
    }
    if (!(isLastArgument || ToStringIsKnownEffectFree(type))) {
        // ToString() order of evaluation matters, see CheckArgumentsForStringConcat().
        return expr;
    }
    if (type.IsReferenceType() != std::optional<bool>(false) && !m.nullConditional) {
        // ToString() might throw NullReferenceException, but the builtin operator+ doesn't.
        return expr;
    }
    if (!ToStringIsKnownEffectFree(type) && toStringMethod != nullptr
        && IL::MethodRequiresCopyForReadonlyLValue(toStringMethod)) {
        // ToString() on a struct may mutate the struct. For operator+ the C#
        // compiler creates a temporary copy before implicitly calling ToString(),
        // whereas an explicit ToString() call would mutate the original lvalue.
        // So we can't remove the compiler-generated ToString() call in cases
        // where this might make a difference.
        return expr;
    }

    // All checks succeeded, we can eliminate the ToString() call.
    // The C# compiler will generate an equivalent call if the code is recompiled.
    return m.target;
}

// The C# `static BinaryOperatorType? GetBinaryOperatorTypeFromMetadataName(string
// name, out bool isChecked, DecompilerSettings settings)` (lines 433-487).
std::optional<Syntax::BinaryOperatorType>
ReplaceMethodCallsWithOperators::GetBinaryOperatorTypeFromMetadataName(
    const std::string& name, bool& isChecked, const DecompilerSettings& settings) {
    isChecked = false;
    if (name == "op_Addition") return Syntax::BinaryOperatorType::Add;
    if (name == "op_Subtraction") return Syntax::BinaryOperatorType::Subtract;
    if (name == "op_Multiply") return Syntax::BinaryOperatorType::Multiply;
    if (name == "op_Division") return Syntax::BinaryOperatorType::Divide;
    if (name == "op_CheckedAddition" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Add;
    }
    if (name == "op_CheckedSubtraction" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Subtract;
    }
    if (name == "op_CheckedMultiply" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Multiply;
    }
    if (name == "op_CheckedDivision" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::BinaryOperatorType::Divide;
    }
    if (name == "op_Modulus") return Syntax::BinaryOperatorType::Modulus;
    if (name == "op_BitwiseAnd") return Syntax::BinaryOperatorType::BitwiseAnd;
    if (name == "op_BitwiseOr") return Syntax::BinaryOperatorType::BitwiseOr;
    if (name == "op_ExclusiveOr") return Syntax::BinaryOperatorType::ExclusiveOr;
    if (name == "op_LeftShift") return Syntax::BinaryOperatorType::ShiftLeft;
    if (name == "op_RightShift") return Syntax::BinaryOperatorType::ShiftRight;
    if (name == "op_UnsignedRightShift" && settings.UnsignedRightShift())
        return Syntax::BinaryOperatorType::UnsignedShiftRight;
    if (name == "op_Equality") return Syntax::BinaryOperatorType::Equality;
    if (name == "op_Inequality") return Syntax::BinaryOperatorType::InEquality;
    if (name == "op_LessThan") return Syntax::BinaryOperatorType::LessThan;
    if (name == "op_LessThanOrEqual") return Syntax::BinaryOperatorType::LessThanOrEqual;
    if (name == "op_GreaterThan") return Syntax::BinaryOperatorType::GreaterThan;
    if (name == "op_GreaterThanOrEqual")
        return Syntax::BinaryOperatorType::GreaterThanOrEqual;
    return std::nullopt;
}

// The C# `static UnaryOperatorType? GetUnaryOperatorTypeFromMetadataName(string
// name, out bool isChecked, DecompilerSettings settings)` (lines 489-517).
std::optional<Syntax::UnaryOperatorType>
ReplaceMethodCallsWithOperators::GetUnaryOperatorTypeFromMetadataName(
    const std::string& name, bool& isChecked, const DecompilerSettings& settings) {
    isChecked = false;
    if (name == "op_LogicalNot") return Syntax::UnaryOperatorType::Not;
    if (name == "op_OnesComplement") return Syntax::UnaryOperatorType::BitNot;
    if (name == "op_UnaryNegation") return Syntax::UnaryOperatorType::Minus;
    if (name == "op_CheckedUnaryNegation" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Minus;
    }
    if (name == "op_UnaryPlus") return Syntax::UnaryOperatorType::Plus;
    if (name == "op_Increment") return Syntax::UnaryOperatorType::Increment;
    if (name == "op_Decrement") return Syntax::UnaryOperatorType::Decrement;
    if (name == "op_CheckedIncrement" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Increment;
    }
    if (name == "op_CheckedDecrement" && settings.CheckedOperators()) {
        isChecked = true;
        return Syntax::UnaryOperatorType::Decrement;
    }
    return std::nullopt;
}

// The C# `bool IsInstantiableTypeParameter(IType type)` (lines 272-275).
bool ReplaceMethodCallsWithOperators::IsInstantiableTypeParameter(
    const TS::IType& type) {
    const auto* typeParameter = dynamic_cast<const TS::ITypeParameter*>(&type);
    return typeParameter != nullptr
        && typeParameter->HasDefaultConstructorConstraint();
}

// The C# `void IAstTransform.Run(AstNode rootNode, TransformContext context)`
// (lines 531-543): store the context, walk the tree, and clear the context
// afterwards (the C# `try/finally`).
void ReplaceMethodCallsWithOperators::Run(Syntax::AstNode& rootNode,
                                          TransformContext& context) {
    context_ = &context;
    try {
        rootNode.AcceptVisitor(*this);
    } catch (...) {
        context_ = nullptr;
        throw;
    }
    context_ = nullptr;
}

// The C# `public override void VisitInvocationExpression(InvocationExpression
// invocationExpression)` (lines 60-64): walk the children first, then rewrite the
// invocation.
void ReplaceMethodCallsWithOperators::VisitInvocationExpression(
    Syntax::InvocationExpression* invocationExpression) {
    Syntax::DepthFirstAstVisitor::VisitInvocationExpression(invocationExpression);
    ProcessInvocationExpression(invocationExpression);
}

// The C# `public override void VisitCastExpression(CastExpression castExpression)`
// (lines 529-549): walk the children first, then rewrite the methodof declaration
// cast `(MethodInfo)MethodBase.GetMethodFromHandle(ldtoken(x).MethodHandle)` to
// `ldtoken(declaring.Method(parameters)).MethodHandle` when the declaring type is
// present and the token resolves to a method.
void ReplaceMethodCallsWithOperators::VisitCastExpression(
    Syntax::CastExpression* castExpression) {
    Syntax::DepthFirstAstVisitor::VisitCastExpression(castExpression);
    // Handle methodof.
    CustomPatternTree tree;
    Syntax::CastExpression* pattern = BuildGetMethodOrConstructorFromHandlePattern(tree);
    PM::Match match = PM::PatternExtensions::Match(*pattern, castExpression);
    if (!match.Success())
        return;
    std::vector<Syntax::AstNode*> methodNodes = match.Get<Syntax::AstNode>("method");
    if (methodNodes.empty() || methodNodes.front() == nullptr)
        return;
    const TS::ISymbol* symbol = CSharp::GetSymbol(*methodNodes.front());
    const TS::IMethod* method =
        symbol != nullptr ? dynamic_cast<const TS::IMethod*>(symbol) : nullptr;
    if (match.Has("declaringType") && method != nullptr) {
        std::vector<Syntax::AstType*> declaringTypes =
            match.Get<Syntax::AstType>("declaringType");
        Syntax::AstType* declaringType =
            declaringTypes.empty() ? nullptr : declaringTypes.front();
        if (declaringType != nullptr) {
            auto* newNode = new Syntax::MemberReferenceExpression(
                new Syntax::TypeReferenceExpression(Syntax::Detach(declaringType)),
                method->Name());
            auto* invocation = new Syntax::InvocationExpression(newNode);
            for (const TS::IParameter* parameter : method->Parameters()) {
                invocation->Arguments().Add(new Syntax::TypeReferenceExpression(
                    context_->TypeSystemAstBuilder().ConvertType(
                        const_cast<TS::IType&>(parameter->Type()))));
            }
            methodNodes.front()->ReplaceWith(invocation);
        }
    }
    std::vector<Syntax::AstNode*> ldtokenNodes = match.Get<Syntax::AstNode>("ldtokenNode");
    if (!ldtokenNodes.empty() && ldtokenNodes.front() != nullptr) {
        castExpression->ReplaceWith(
            CSharp::CopyAnnotationsFrom(ldtokenNodes.front(), *castExpression));
    }
}

// The C# `void ProcessInvocationExpression(InvocationExpression
// invocationExpression)` (lines 66-259): the method-level rewrites -- the
// `String.Concat(a, b)` -> `a + b` reduction, the three special methods, the
// binary/unary operator methods, the explicit conversion operator, and the
// `op_True` condition removal.
void ReplaceMethodCallsWithOperators::ProcessInvocationExpression(
    Syntax::InvocationExpression* invocationExpression) {
    const TS::ISymbol* symbol = CSharp::GetSymbol(*invocationExpression);
    const TS::IMethod* method =
        symbol != nullptr ? dynamic_cast<const TS::IMethod*>(symbol) : nullptr;
    if (method == nullptr)
        return;
    std::vector<Syntax::Expression*> arguments;
    for (int i = 0; i < invocationExpression->Arguments().Count(); i++)
        arguments.push_back(invocationExpression->Arguments()[i]);

    // Reduce "String.Concat(a, b)" to "a + b".
    if (IsStringConcat(*method) && context_->Settings().StringConcat()) {
        // The C# list pattern `arguments is [ArrayCreateExpression { Initializer: { }
        // aceInitializer }] && method.Parameters is [{ Type: ArrayType }]`: a single
        // params-array argument expands into its initializer elements.
        if (arguments.size() == 1) {
            auto* arrayCreate =
                dynamic_cast<Syntax::ArrayCreateExpression*>(arguments[0]);
            if (arrayCreate != nullptr && arrayCreate->Initializer() != nullptr
                && method->Parameters().size() == 1
                && method->Parameters()[0] != nullptr
                && method->Parameters()[0]->Type().Kind() == TS::TypeKind::Array) {
                arguments.clear();
                Syntax::AstNodeCollectionT<Syntax::Expression>& elements =
                    arrayCreate->Initializer()->Elements();
                for (int i = 0; i < elements.Count(); i++)
                    arguments.push_back(elements[i]);
            }
        }

        if (!CheckArgumentsForStringConcat(arguments)) {
            return;
        }

        // The C# `invocationExpression.Ancestors.OfType<LambdaExpression>().Any(
        // lambda => lambda.Annotation<IL.ILFunction>()?.Kind ==
        // IL.ILFunctionKind.ExpressionTree)`.
        bool isInExpressionTree = false;
        for (Syntax::AstNode* ancestor : invocationExpression->Ancestors()) {
            auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(ancestor);
            if (lambda == nullptr)
                continue;
            IL::ILFunction* function = CSharp::GetILFunction(*lambda);
            if (function != nullptr
                && function->Kind == IL::ILFunctionKind::ExpressionTree) {
                isInExpressionTree = true;
                break;
            }
        }

        context_->Step("Replace String.Concat with +", invocationExpression);
        Syntax::Expression* arg0 = Syntax::Detach(arguments[0]);
        Syntax::Expression* arg1 = Syntax::Detach(arguments[1]);
        if (!isInExpressionTree) {
            arg1 = Syntax::Detach(RemoveRedundantToStringInConcat(
                arg1, *method, arguments.size() == 2));
            const Semantics::ResolveResult* arg1RR =
                CSharp::GetResolveResult(*arg1);
            if (TS::IsKnownType(arg1RR->Type(), TS::KnownTypeCode::String)) {
                arg0 = Syntax::Detach(
                    RemoveRedundantToStringInConcat(arg0, *method, false));
            }
        }
        auto* expr = new Syntax::BinaryOperatorExpression(
            arg0, Syntax::BinaryOperatorType::Add, arg1);
        for (size_t i = 2; i < arguments.size(); i++) {
            Syntax::Expression* arg = Syntax::Detach(arguments[i]);
            if (!isInExpressionTree) {
                arg = Syntax::Detach(RemoveRedundantToStringInConcat(
                    arg, *method, i == arguments.size() - 1));
            }
            expr = new Syntax::BinaryOperatorExpression(
                expr, Syntax::BinaryOperatorType::Add, arg);
        }
        CSharp::CopyAnnotationsFrom(expr, *invocationExpression);
        invocationExpression->ReplaceWith(expr);
        context_->EndStep(expr);
        return;
    }

    const std::string fullName = method->FullName();
    if (fullName == "System.Type.GetTypeFromHandle") {
        if (arguments.size() == 1) {
            if (IsTypeHandleOnTypeOf(arguments[0])) {
                Syntax::Expression* target =
                    static_cast<Syntax::MemberReferenceExpression*>(arguments[0])
                        ->Target();
                CSharp::CopyInstructionsFrom(target, *invocationExpression);
                invocationExpression->ReplaceWith(target);
                return;
            }
        }
    } else if (fullName == "System.Activator.CreateInstance") {
        std::vector<TS::ITypePtr> typeArguments = method->TypeArguments();
        if (context_->Settings().UseObjectCreationOfGenericTypeParameter()
            && arguments.empty() && typeArguments.size() == 1
            && IsInstantiableTypeParameter(*typeArguments[0])) {
            auto* objectCreate = new Syntax::ObjectCreateExpression(
                context_->TypeSystemAstBuilder().ConvertType(*typeArguments[0]));
            invocationExpression->ReplaceWith(objectCreate);
        }
    } else if (fullName
               == "System.Runtime.CompilerServices.RuntimeHelpers.GetSubArray") {
        if (arguments.size() == 2 && context_->Settings().Ranges()) {
            auto* slicing =
                new Syntax::IndexerExpression(Syntax::Detach(arguments[0]));
            slicing->Arguments().Add(Syntax::Detach(arguments[1]));
            CSharp::CopyAnnotationsFrom(slicing, *invocationExpression);
            invocationExpression->ReplaceWith(slicing);
            return;
        }
    }

    // The binary operator methods (`op_Addition` &c.).
    bool isChecked = false;
    std::optional<Syntax::BinaryOperatorType> bop =
        GetBinaryOperatorTypeFromMetadataName(method->Name(), isChecked,
                                              context_->Settings());
    if (bop.has_value() && arguments.size() == 2) {
        invocationExpression->Arguments().Clear();
        if (isChecked)
            invocationExpression->AddAnnotation(CheckedAnnotationHandle());
        else if (HasCheckedEquivalent(*method))
            invocationExpression->AddAnnotation(UncheckedAnnotationHandle());
        auto* binaryOperator = new Syntax::BinaryOperatorExpression(
            Syntax::UnwrapInDirectionExpression(arguments[0]), *bop,
            Syntax::UnwrapInDirectionExpression(arguments[1]));
        CSharp::CopyAnnotationsFrom(binaryOperator, *invocationExpression);
        invocationExpression->ReplaceWith(binaryOperator);
        return;
    }

    // The unary operator methods (`op_LogicalNot` &c.).
    std::optional<Syntax::UnaryOperatorType> uop =
        GetUnaryOperatorTypeFromMetadataName(method->Name(), isChecked,
                                             context_->Settings());
    if (uop.has_value() && arguments.size() == 1) {
        if (isChecked)
            invocationExpression->AddAnnotation(CheckedAnnotationHandle());
        else if (HasCheckedEquivalent(*method))
            invocationExpression->AddAnnotation(UncheckedAnnotationHandle());
        if (*uop == Syntax::UnaryOperatorType::Increment
            || *uop == Syntax::UnaryOperatorType::Decrement) {
            // `op_Increment(a)` is not equivalent to `++a`, because it does not
            // assign the incremented value to `a`; only the decimal shape (a
            // legacy csc optimization) is reversed to `a + 1m` / `a - 1m`.
            TS::ITypePtr declaringType = method->DeclaringType();
            if (declaringType != nullptr
                && TS::IsKnownType(*declaringType, TS::KnownTypeCode::Decimal)) {
                auto* arithmetic = new Syntax::BinaryOperatorExpression(
                    Syntax::Detach(
                        Syntax::UnwrapInDirectionExpression(arguments[0])),
                    *uop == Syntax::UnaryOperatorType::Increment
                        ? Syntax::BinaryOperatorType::Add
                        : Syntax::BinaryOperatorType::Subtract,
                    new Syntax::PrimitiveExpression(
                        Syntax::DecimalValue::FromInt32(1)));
                CSharp::CopyAnnotationsFrom(arithmetic, *invocationExpression);
                invocationExpression->ReplaceWith(arithmetic);
            }
        } else {
            arguments[0]->Remove();
            auto* unaryOperator = new Syntax::UnaryOperatorExpression(
                Syntax::UnwrapInDirectionExpression(arguments[0]), *uop);
            CSharp::CopyAnnotationsFrom(unaryOperator, *invocationExpression);
            invocationExpression->ReplaceWith(unaryOperator);
        }
        return;
    }

    // The explicit conversion operator methods (`op_Explicit` / `op_CheckedExplicit`).
    if ((method->Name() == "op_Explicit" || method->Name() == "op_CheckedExplicit")
        && arguments.size() == 1) {
        arguments[0]->Remove();
        if (method->Name() == "op_CheckedExplicit")
            invocationExpression->AddAnnotation(CheckedAnnotationHandle());
        else if (HasCheckedEquivalent(*method))
            invocationExpression->AddAnnotation(UncheckedAnnotationHandle());
        auto* cast = new Syntax::CastExpression(
            context_->TypeSystemAstBuilder().ConvertType(
                const_cast<TS::IType&>(method->ReturnType())),
            Syntax::UnwrapInDirectionExpression(arguments[0]));
        CSharp::CopyAnnotationsFrom(cast, *invocationExpression);
        invocationExpression->ReplaceWith(cast);
        return;
    }

    // `op_True(x)` in a condition slot is just `x` (the C# compiler inserts the
    // call for the implicit bool conversion).
    if (method->Name() == "op_True" && arguments.size() == 1
        && invocationExpression->Slot() != nullptr
        && invocationExpression->Slot()->Kind() == &Syntax::Slots::Condition) {
        Syntax::Expression* condition =
            Syntax::UnwrapInDirectionExpression(arguments[0]);
        invocationExpression->ReplaceWith(condition);
        return;
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
