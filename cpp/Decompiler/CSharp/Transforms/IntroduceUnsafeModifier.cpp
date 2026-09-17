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

// Implementation of the `IntroduceUnsafeModifier` AST transform
// (ICSharpCode.Decompiler/CSharp/Transforms/IntroduceUnsafeModifier.cs). See the
// header for the overview.

#include "Decompiler/CSharp/Transforms/IntroduceUnsafeModifier.hpp"

#include "Decompiler/CSharp/Annotations.hpp"

#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"

#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/FixedVariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/FunctionPointerAstType.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/SizeOfExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/StackAllocExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"

#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Resolver = ::ILSpy::Decompiler::CSharp::Resolver;

namespace ILSpy::Decompiler::CSharp::Transforms {

void IntroduceUnsafeModifier::Run(Syntax::AstNode& rootNode, TransformContext& context)
{
    context_ = &context;
    try {
        rootNode.AcceptVisitorBool(*this);
    } catch (...) {
        context_ = nullptr;
        throw;
    }
    context_ = nullptr;
}

bool IntroduceUnsafeModifier::IsUnsafe(Syntax::AstNode& node)
{
    IntroduceUnsafeModifier visitor;
    return node.AcceptVisitorBool(visitor);
}

bool IntroduceUnsafeModifier::VisitChildren(Syntax::AstNode* node)
{
    if (node == nullptr)
        return false;
    bool result = false;
    Syntax::AstNode* next;
    for (Syntax::AstNode* child = node->FirstChild(); child != nullptr; child = next) {
        // Store the next sibling before visiting, so the walk continues even if the
        // visitor removes or replaces the child (the C# loop's comment).
        next = child->NextSibling();
        result |= child->AcceptVisitorBool(*this);
    }
    if (result && dynamic_cast<Syntax::EntityDeclaration*>(node) != nullptr
        && dynamic_cast<Syntax::Accessor*>(node) == nullptr) {
        if (context_ != nullptr)
            context_->Step("Add unsafe modifier", node);
        auto* entity = static_cast<Syntax::EntityDeclaration*>(node);
        entity->Modifiers(entity->Modifiers() | Syntax::Modifiers::Unsafe);
        return false;
    }
    return result;
}

bool IntroduceUnsafeModifier::VisitPointerReferenceExpression(
    Syntax::PointerReferenceExpression* pointerReferenceExpression)
{
    DepthFirstAstVisitorBool::VisitPointerReferenceExpression(pointerReferenceExpression);
    return true;
}

bool IntroduceUnsafeModifier::VisitSizeOfExpression(Syntax::SizeOfExpression* sizeOfExpression)
{
    // C# sizeof(MyStruct) requires unsafe{} (not for sizeof(int), but that gets
    // constant-folded and thus decompiled to 4).
    DepthFirstAstVisitorBool::VisitSizeOfExpression(sizeOfExpression);
    return true;
}

bool IntroduceUnsafeModifier::VisitComposedType(Syntax::ComposedType* composedType)
{
    if (composedType->PointerRank() > 0)
        return true;
    return DepthFirstAstVisitorBool::VisitComposedType(composedType);
}

bool IntroduceUnsafeModifier::VisitFunctionPointerType(
    Syntax::FunctionPointerAstType* functionPointerType)
{
    return true;
}

bool IntroduceUnsafeModifier::VisitUnaryOperatorExpression(
    Syntax::UnaryOperatorExpression* unaryOperatorExpression)
{
    bool result = DepthFirstAstVisitorBool::VisitUnaryOperatorExpression(unaryOperatorExpression);
    if (unaryOperatorExpression->Operator() == Syntax::UnaryOperatorType::Dereference) {
        auto* bop = dynamic_cast<Syntax::BinaryOperatorExpression*>(
            unaryOperatorExpression->Expression());
        if (bop != nullptr && bop->Operator() == Syntax::BinaryOperatorType::Add) {
            const Sem::ResolveResult* rr = GetResolveResult(*bop);
            if (auto* orr = dynamic_cast<const Sem::OperatorResolveResult*>(rr)) {
                const auto& operands = orr->Operands();
                if (!operands.empty() && operands[0] != nullptr
                    && operands[0]->Type().Kind() == TS::TypeKind::Pointer) {
                    if (context_ != nullptr)
                        context_->Step("Replace pointer addition with indexer", unaryOperatorExpression);
                    // transform "*(ptr + int)" to "ptr[int]"
                    auto* indexer = new Syntax::IndexerExpression();
                    indexer->Target(Syntax::Detach(bop->Left()));
                    indexer->Arguments().Add(Syntax::Detach(bop->Right()));
                    CopyAnnotationsFrom(indexer, *unaryOperatorExpression);
                    CopyAnnotationsFrom(indexer, *bop);
                    unaryOperatorExpression->ReplaceWith(indexer);
                    if (context_ != nullptr)
                        context_->EndStep(indexer);
                }
            }
        }
        return true;
    } else if (unaryOperatorExpression->Operator() == Syntax::UnaryOperatorType::AddressOf) {
        return true;
    } else {
        return result;
    }
}

bool IntroduceUnsafeModifier::VisitMemberReferenceExpression(
    Syntax::MemberReferenceExpression* memberReferenceExpression)
{
    bool result = DepthFirstAstVisitorBool::VisitMemberReferenceExpression(memberReferenceExpression);
    auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(memberReferenceExpression->Target());
    if (uoe != nullptr && uoe->Operator() == Syntax::UnaryOperatorType::Dereference) {
        if (context_ != nullptr)
            context_->Step("Replace pointer member access", memberReferenceExpression);
        auto* pre = new Syntax::PointerReferenceExpression();
        pre->Target(Syntax::Detach(uoe->Expression()));
        pre->MemberName(memberReferenceExpression->MemberName());
        memberReferenceExpression->TypeArguments().MoveTo(pre->TypeArguments());
        CopyAnnotationsFrom(pre, *uoe);
        pre->RemoveAnnotations<Sem::ResolveResult>(); // only copy the ResolveResult from the MRE
        CopyAnnotationsFrom(pre, *memberReferenceExpression);
        memberReferenceExpression->ReplaceWith(pre);
        if (context_ != nullptr)
            context_->EndStep(pre);
    }
    if (HasUnsafeResolveResult(*memberReferenceExpression))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitIdentifierExpression(
    Syntax::IdentifierExpression* identifierExpression)
{
    bool result = DepthFirstAstVisitorBool::VisitIdentifierExpression(identifierExpression);
    if (HasUnsafeResolveResult(*identifierExpression))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitStackAllocExpression(
    Syntax::StackAllocExpression* stackAllocExpression)
{
    bool result = DepthFirstAstVisitorBool::VisitStackAllocExpression(stackAllocExpression);
    if (HasUnsafeResolveResult(*stackAllocExpression))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitInvocationExpression(
    Syntax::InvocationExpression* invocationExpression)
{
    bool result = DepthFirstAstVisitorBool::VisitInvocationExpression(invocationExpression);
    if (HasUnsafeResolveResult(*invocationExpression))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitObjectCreateExpression(
    Syntax::ObjectCreateExpression* objectCreateExpression)
{
    bool result = DepthFirstAstVisitorBool::VisitObjectCreateExpression(objectCreateExpression);
    if (HasUnsafeResolveResult(*objectCreateExpression))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitFixedVariableInitializer(
    Syntax::FixedVariableInitializer* fixedVariableInitializer)
{
    DepthFirstAstVisitorBool::VisitFixedVariableInitializer(fixedVariableInitializer);
    return true;
}

bool IntroduceUnsafeModifier::HasUnsafeResolveResult(const Syntax::AstNode& node)
{
    const Sem::ResolveResult* rr = GetResolveResult(node);
    if (rr == nullptr)
        return false;
    if (IsUnsafeType(rr->Type()))
        return true;
    if (auto* mrr = dynamic_cast<const Sem::MemberResolveResult*>(rr)) {
        auto* pm = dynamic_cast<const TS::IParameterizedMember*>(mrr->Member());
        if (pm != nullptr) {
            for (const TS::IParameter* p : pm->Parameters()) {
                if (p != nullptr && IsUnsafeType(p->Type()))
                    return true;
            }
        }
    } else if (dynamic_cast<const Resolver::MethodGroupResolveResult*>(rr) != nullptr) {
        const TS::ISymbol* chosenMethod = GetSymbol(node);
        auto* pm2 = dynamic_cast<const TS::IParameterizedMember*>(chosenMethod);
        if (pm2 != nullptr) {
            if (IsUnsafeType(pm2->ReturnType()))
                return true;
            for (const TS::IParameter* p : pm2->Parameters()) {
                if (p != nullptr && IsUnsafeType(p->Type()))
                    return true;
            }
        }
    }
    return false;
}

bool IntroduceUnsafeModifier::IsUnsafeType(const TS::IType& type)
{
    switch (type.Kind()) {
        case TS::TypeKind::Pointer:
        case TS::TypeKind::FunctionPointer:
            return true;
        case TS::TypeKind::ByReference:
        case TS::TypeKind::Array:
            // The C# casts to `TypeWithElementType`; the port carries the concrete
            // `ByReferenceType`/`ArrayType` leaves with no shared element-type base.
            if (auto* byReference = dynamic_cast<const TS::ByReferenceType*>(&type))
                return byReference->Element() != nullptr && IsUnsafeType(*byReference->Element());
            if (auto* array = dynamic_cast<const TS::ArrayType*>(&type))
                return array->Element() != nullptr && IsUnsafeType(*array->Element());
            return false;
        default:
            return false;
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
