// Copyright (c) 2026 ILSpy Contributors
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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/ContextTrackingVisitor.cs -- the base
// class for AST visitors that need the current type/method context. Every
// `TypeDeclaration` / `MethodDeclaration` / `ConstructorDeclaration` / `DestructorDeclaration`
// / `OperatorDeclaration` / `Accessor` visit sets the corresponding context slot from the
// node's resolved symbol for the duration of the child walk, then restores the previous
// value (the C# `try/finally`; the port uses a small RAII guard, which also restores on
// exception, matching `finally`).
//
// The C# type is generic (`ContextTrackingVisitor<TResult> : DepthFirstAstVisitor<TResult>`),
// but the only instantiation is `TResult = AstNode` (PatternStatementTransform), so the port
// realizes exactly that instantiation over `DepthFirstAstVisitorAstNode`. `Initialize` /
// `Uninitialize` seed and clear the slots from a `TransformContext` (the
// `CurrentTypeDefinition` / `CurrentMember as IMethod` reads).
//
// The C# `currentMethod = methodDeclaration.GetSymbol() as IMethod` ports to the
// `dynamic_cast<const IMethod*>(GetSymbol(*node))` shape: the `as` downcasts of the resolved
// `ISymbol` are `dynamic_cast`s in the port, and a non-method symbol (there is none for these
// declaration kinds in practice) yields null exactly like the C# `as`.

#pragma once

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitorAstNode.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

// The real type-system namespace alias (the CSharp/TypeSystem sub-namespace the included
// TransformContext.hpp pulls into scope shadows the plain `TypeSystem::` lookup).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace ILSpy::Decompiler::CSharp::Transforms {

// The port's stand-in for the C# `finally` of the per-node `try/finally` context save/restore:
// an RAII guard that writes the saved value back to the slot on destruction (normal return or
// exception). Used only by this header.
template <class TSlot>
class RestoreOnExit {
public:
    RestoreOnExit(TSlot& slot, TSlot saved) : slot_(&slot), saved_(saved) {}
    ~RestoreOnExit() { *slot_ = saved_; }

    RestoreOnExit(const RestoreOnExit&) = delete;
    RestoreOnExit& operator=(const RestoreOnExit&) = delete;

private:
    TSlot* slot_;
    TSlot saved_;
};

// The C# `public abstract class ContextTrackingVisitor<TResult> : DepthFirstAstVisitor<TResult>`
// (instantiated `TResult = AstNode`). Abstract (a protected constructor): only derived
// visitors are constructed.
class ContextTrackingVisitor : public Syntax::DepthFirstAstVisitorAstNode {
protected:
    // The C# `protected ITypeDefinition? currentTypeDefinition`.
    const TS::ITypeDefinition* currentTypeDefinition = nullptr;
    // The C# `protected IMethod? currentMethod`.
    const TS::IMethod* currentMethod = nullptr;

    // The C# `protected void Initialize(TransformContext context)`: seed the slots from the
    // context's decompilation position.
    void Initialize(const TransformContext& context) {
        currentTypeDefinition = context.CurrentTypeDefinition();
        currentMethod = dynamic_cast<const TS::IMethod*>(context.CurrentMember());
    }

    // The C# `protected void Uninitialize()`: clear the slots.
    void Uninitialize() {
        currentTypeDefinition = nullptr;
        currentMethod = nullptr;
    }

public:
    ~ContextTrackingVisitor() override = default;

    // The C# `public override TResult VisitTypeDeclaration(TypeDeclaration typeDeclaration)`:
    // set `currentTypeDefinition` from the declaration's resolved symbol for the child walk,
    // then restore. The RAII guard restores on the normal return and on an exception (the C#
    // `finally`).
    Syntax::AstNode* VisitTypeDeclaration(Syntax::TypeDeclaration* typeDeclaration) override {
        const TS::ITypeDefinition* oldType = currentTypeDefinition;
        RestoreOnExit<const TS::ITypeDefinition*> guard(currentTypeDefinition, oldType);
        currentTypeDefinition = dynamic_cast<const TS::ITypeDefinition*>(GetSymbol(*typeDeclaration));
        return Syntax::DepthFirstAstVisitorAstNode::VisitTypeDeclaration(typeDeclaration);
    }

    // The C# `public override TResult VisitMethodDeclaration(MethodDeclaration methodDeclaration)`:
    // set `currentMethod` for the child walk, then restore.
    Syntax::AstNode* VisitMethodDeclaration(Syntax::MethodDeclaration* methodDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        RestoreOnExit<const TS::IMethod*> guard(currentMethod, oldMethod);
        currentMethod = dynamic_cast<const TS::IMethod*>(GetSymbol(*methodDeclaration));
        return Syntax::DepthFirstAstVisitorAstNode::VisitMethodDeclaration(methodDeclaration);
    }

    // The C# `public override TResult VisitConstructorDeclaration(ConstructorDeclaration ...)`:
    // set `currentMethod` for the child walk, then restore.
    Syntax::AstNode* VisitConstructorDeclaration(
        Syntax::ConstructorDeclaration* constructorDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        RestoreOnExit<const TS::IMethod*> guard(currentMethod, oldMethod);
        currentMethod = dynamic_cast<const TS::IMethod*>(GetSymbol(*constructorDeclaration));
        return Syntax::DepthFirstAstVisitorAstNode::VisitConstructorDeclaration(constructorDeclaration);
    }

    // The C# `public override TResult VisitDestructorDeclaration(DestructorDeclaration ...)`:
    // set `currentMethod` for the child walk, then restore.
    Syntax::AstNode* VisitDestructorDeclaration(
        Syntax::DestructorDeclaration* destructorDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        RestoreOnExit<const TS::IMethod*> guard(currentMethod, oldMethod);
        currentMethod = dynamic_cast<const TS::IMethod*>(GetSymbol(*destructorDeclaration));
        return Syntax::DepthFirstAstVisitorAstNode::VisitDestructorDeclaration(destructorDeclaration);
    }

    // The C# `public override TResult VisitOperatorDeclaration(OperatorDeclaration ...)`: set
    // `currentMethod` for the child walk, then restore.
    Syntax::AstNode* VisitOperatorDeclaration(
        Syntax::OperatorDeclaration* operatorDeclaration) override {
        const TS::IMethod* oldMethod = currentMethod;
        RestoreOnExit<const TS::IMethod*> guard(currentMethod, oldMethod);
        currentMethod = dynamic_cast<const TS::IMethod*>(GetSymbol(*operatorDeclaration));
        return Syntax::DepthFirstAstVisitorAstNode::VisitOperatorDeclaration(operatorDeclaration);
    }

    // The C# `public override TResult VisitAccessor(Accessor accessor)`: set `currentMethod`
    // from the accessor's resolved symbol for the child walk, then restore.
    Syntax::AstNode* VisitAccessor(Syntax::Accessor* accessor) override {
        const TS::IMethod* oldMethod = currentMethod;
        RestoreOnExit<const TS::IMethod*> guard(currentMethod, oldMethod);
        currentMethod = dynamic_cast<const TS::IMethod*>(GetSymbol(*accessor));
        return Syntax::DepthFirstAstVisitorAstNode::VisitAccessor(accessor);
    }
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
