// Copyright (c) 2026 Jun Cai
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/CSharp/Transforms/IntroduceUnsafeModifier.hpp"

// The include-order hazard (the ledger): Annotations.hpp pulls
// TranslatedExpression.hpp, which writes unqualified `TypeSystem::IType`
// inside namespace CSharp -- it must precede every header that opens the
// nested ILSpy::Decompiler::CSharp::TypeSystem.
#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PointerReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Resolver = ::ILSpy::Decompiler::CSharp::Resolver;

namespace ILSpy::Decompiler::CSharp::Transforms {

void IntroduceUnsafeModifier::Run(Syntax::AstNode& rootNode,
                                  TransformContext& context) {
    context_ = &context;
    try {
        rootNode.AcceptVisitorBool(*this);
    } catch (...) {
        context_ = nullptr;
        throw;
    }
    context_ = nullptr;
}

bool IntroduceUnsafeModifier::IsUnsafe(Syntax::AstNode* node) {
    IntroduceUnsafeModifier visitor;
    return node != nullptr && node->AcceptVisitorBool(visitor);
}

bool IntroduceUnsafeModifier::VisitChildren(Syntax::AstNode* node) {
    bool result = false;
    // The hand-over-hand guarantee: the Children enumeration captures each
    // successor before yielding, so the rewrites below (which replace the
    // current child) do not lose the place.
    for (Syntax::AstNode* child : node->Children())
        result |= child->AcceptVisitorBool(*this);
    if (result) {
        auto* entity = dynamic_cast<Syntax::EntityDeclaration*>(node);
        if (entity != nullptr
            && dynamic_cast<Syntax::Accessor*>(node) == nullptr) {
            if (context_ != nullptr)
                context_->StepOnce("Add unsafe modifier", node);
            // The C# `((EntityDeclaration)node).Modifiers |=
            // Modifiers.Unsafe`.
            entity->Modifiers(entity->Modifiers()
                              | Syntax::Modifiers::Unsafe);
            return false;
        }
    }
    return result;
}

bool IntroduceUnsafeModifier::VisitPointerReferenceExpression(
    Syntax::PointerReferenceExpression*) {
    return true;
}

bool IntroduceUnsafeModifier::VisitSizeOfExpression(
    Syntax::SizeOfExpression*) {
    return true;
}

bool IntroduceUnsafeModifier::VisitComposedType(
    Syntax::ComposedType* node) {
    if (node->PointerRank() > 0)
        return true;
    return VisitChildren(node);
}

bool IntroduceUnsafeModifier::VisitFunctionPointerType(
    Syntax::FunctionPointerAstType*) {
    return true;
}

bool IntroduceUnsafeModifier::VisitUnaryOperatorExpression(
    Syntax::UnaryOperatorExpression* node) {
    bool result = VisitChildren(node);
    if (node->Operator() == Syntax::UnaryOperatorType::Dereference) {
        // The C# pointer-addition indexer rewrite: `*(ptr + i)` becomes
        // `ptr[i]`.
        auto* bop = dynamic_cast<Syntax::BinaryOperatorExpression*>(
            node->Expression());
        if (bop != nullptr
            && bop->Operator() == Syntax::BinaryOperatorType::Add) {
            const auto* orr = dynamic_cast<const Semantics::OperatorResolveResult*>(
                bop->Annotation<Semantics::ResolveResult>());
            bool pointerOperand = false;
            if (orr != nullptr && !orr->Operands().empty()
                && orr->Operands()[0] != nullptr) {
                pointerOperand =
                    orr->Operands()[0]->Type().Kind()
                    == TS::TypeKind::Pointer;
            }
            if (pointerOperand && bop->Left() != nullptr
                && bop->Right() != nullptr) {
                if (context_ != nullptr)
                    context_->StepOnce(
                        "Replace pointer addition with indexer", node);
                auto* indexer = new Syntax::IndexerExpression();
                indexer->Target(Syntax::Detach(bop->Left()));
                indexer->Arguments().Add(Syntax::Detach(bop->Right()));
                CopyAnnotationsFrom(indexer, *node);
                CopyAnnotationsFrom(indexer, *bop);
                node->ReplaceWith(indexer);
            }
        }
        return true;
    }
    if (node->Operator() == Syntax::UnaryOperatorType::AddressOf)
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitMemberReferenceExpression(
    Syntax::MemberReferenceExpression* node) {
    bool result = VisitChildren(node);
    // The C# pointer member access rewrite: `(*p).M` becomes `p->M`.
    auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(
        node->Target());
    if (uoe != nullptr
        && uoe->Operator() == Syntax::UnaryOperatorType::Dereference) {
        if (context_ != nullptr)
            context_->StepOnce("Replace pointer member access", node);
        auto* pre = new Syntax::PointerReferenceExpression();
        if (uoe->Expression() != nullptr)
            pre->Target(Syntax::Detach(uoe->Expression()));
        pre->MemberName(node->MemberName());
        // The C# `memberReferenceExpression.TypeArguments.MoveTo(
        // pre.TypeArguments)`.
        for (int i = 0; i < node->TypeArguments().Count(); ++i)
            pre->TypeArguments().Add(node->TypeArguments().At(i));
        CopyAnnotationsFrom(pre, *uoe);
        pre->RemoveAnnotations<Semantics::ResolveResult>();
        CopyAnnotationsFrom(pre, *node);
        node->ReplaceWith(pre);
    }
    if (HasUnsafeResolveResult(*node))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitIdentifierExpression(
    Syntax::IdentifierExpression* node) {
    bool result = VisitChildren(node);
    if (HasUnsafeResolveResult(*node))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitStackAllocExpression(
    Syntax::StackAllocExpression* node) {
    bool result = VisitChildren(node);
    if (HasUnsafeResolveResult(*node))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitInvocationExpression(
    Syntax::InvocationExpression* node) {
    bool result = VisitChildren(node);
    if (HasUnsafeResolveResult(*node))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitObjectCreateExpression(
    Syntax::ObjectCreateExpression* node) {
    bool result = VisitChildren(node);
    if (HasUnsafeResolveResult(*node))
        return true;
    return result;
}

bool IntroduceUnsafeModifier::VisitFixedVariableInitializer(
    Syntax::FixedVariableInitializer* node) {
    VisitChildren(node);
    return true;
}

// The C# `private bool HasUnsafeResolveResult(AstNode node)`: the node's
// own resolve result (a null annotation stays safe -- the ERROR fallback the
// port's GetResolveResult would substitute must not count), the member's
// parameter types, and the method-group's chosen member (return type +
// parameters).
bool IntroduceUnsafeModifier::HasUnsafeResolveResult(
    const Syntax::AstNode& node) {
    const Semantics::ResolveResult* rr =
        node.Annotation<Semantics::ResolveResult>();
    if (rr == nullptr)
        return false;
    if (IsUnsafeType(&rr->Type()))
        return true;
    if (const auto* mrr =
            dynamic_cast<const Semantics::MemberResolveResult*>(rr)) {
        const auto* pm =
            dynamic_cast<const TS::IParameterizedMember*>(mrr->Member());
        if (pm != nullptr) {
            for (const TS::IParameter* p : pm->Parameters()) {
                if (p != nullptr && IsUnsafeType(&p->Type()))
                    return true;
            }
        }
        return false;
    }
    if (dynamic_cast<const Resolver::MethodGroupResolveResult*>(rr)
        != nullptr) {
        const TS::ISymbol* symbol = CSharp::GetSymbol(node);
        const auto* pm = dynamic_cast<const TS::IParameterizedMember*>(symbol);
        if (pm != nullptr) {
            if (IsUnsafeType(&pm->ReturnType()))
                return true;
            for (const TS::IParameter* p : pm->Parameters()) {
                if (p != nullptr && IsUnsafeType(&p->Type()))
                    return true;
            }
        }
    }
    return false;
}

// The C# `private bool IsUnsafeType(IType type)`: the pointer /
// function-pointer kinds; the array / by-reference kinds recurse through
// their element type (the port's leaf classes carry the element directly --
// no shared TypeWithElementType base).
bool IntroduceUnsafeModifier::IsUnsafeType(const TS::IType* type) {
    if (type == nullptr)
        return false;
    switch (type->Kind()) {
        case TS::TypeKind::Pointer:
        case TS::TypeKind::FunctionPointer:
            return true;
        case TS::TypeKind::ByReference:
            if (const auto* byRef =
                    dynamic_cast<const TS::ByReferenceType*>(type))
                return IsUnsafeType(byRef->Element().get());
            return false;
        case TS::TypeKind::Array:
            if (const auto* array =
                    dynamic_cast<const TS::ArrayType*>(type))
                return IsUnsafeType(array->Element().get());
            return false;
        default:
            return false;
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
