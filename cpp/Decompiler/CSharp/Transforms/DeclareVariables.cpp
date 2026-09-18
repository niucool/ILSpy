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

#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {

// The C# `v.DefaultInitialization |= prev.DefaultInitialization` values are an
// enum-flag combination (None=0, NeedsDefaultValue=1, NeedsSkipInit=2).

// The C# `Debug.Assert(prev.Type.Equals(v.Type))` in ResolveCollisions: a same-name
// collision is only legal between equal-typed variables. The port asserts (debug
// builds) instead of the C# `Debug.Assert`.

} // namespace

// ---- InsertionPoint -------------------------------------------------------

DeclareVariables::InsertionPoint DeclareVariables::InsertionPoint::Up() const {
    return InsertionPoint{level - 1, nextNode->Parent()};
}

DeclareVariables::InsertionPoint DeclareVariables::InsertionPoint::UpTo(int targetLevel) const {
    InsertionPoint result = *this;
    while (result.level > targetLevel) {
        result.nextNode = result.nextNode->Parent();
        result.level -= 1;
    }
    return result;
}

// ---- VariableToDeclare ----------------------------------------------------

DeclareVariables::VariableToDeclare::VariableToDeclare(
    IL::ILVariable* variable, DeclareVariables::InsertionPoint insertionPoint,
    Syntax::IdentifierExpression* firstUse, int sourceOrder)
    : SourceOrder(sourceOrder),
      InsertionPoint(insertionPoint),
      FirstUse(firstUse),
      ilVariable_(variable) {
    if (variable->UsesInitialValue) {
        DefaultInitialization = variable->InitialValueIsInitialized
            ? VariableInitKind::NeedsDefaultValue
            : VariableInitKind::NeedsSkipInit;
    } else {
        DefaultInitialization = VariableInitKind::None;
    }
}

std::string DeclareVariables::VariableToDeclare::Name() const {
    return ilVariable_->Name;
}

// ---- Run / analysis entry points ------------------------------------------

void DeclareVariables::Run(Syntax::AstNode&, TransformContext&) {
    throw std::logic_error(
        "DeclareVariables::Run: the declaration-insertion phase "
        "(EnsureExpressionStatementsAreValid / InsertDeconstructionVariableDeclarations / "
        "InsertVariableDeclarations / UpdateAnnotations) is not ported yet -- it needs "
        "AssignVariableNames.GenerateVariableName and the ILVariable shared handle on "
        "VariableToDeclare; use Analyze() for the analysis half.");
}

void DeclareVariables::Analyze(Syntax::AstNode& rootNode) {
    variableDict.clear();
    variableOrder.clear();
    scopeTracking.clear();
    FindInsertionPoints(rootNode, 0);
    ResolveCollisions();
}

Syntax::AstNode* DeclareVariables::GetDeclarationPoint(IL::ILVariable& variable) {
    auto it = variableDict.find(&variable);
    if (it == variableDict.end())
        throw std::out_of_range("DeclareVariables::GetDeclarationPoint: unknown variable");
    VariableToDeclare* v = it->second.get();
    while (v->ReplacementDueToCollision != nullptr) {
        v = v->ReplacementDueToCollision;
    }
    return v->InsertionPoint.nextNode;
}

bool DeclareVariables::WasMerged(IL::ILVariable& variable) {
    auto it = variableDict.find(&variable);
    if (it == variableDict.end())
        throw std::out_of_range("DeclareVariables::WasMerged: unknown variable");
    VariableToDeclare* v = it->second.get();
    return v->InvolvedInCollision || v->RemovedDueToCollision();
}

void DeclareVariables::ClearAnalysisResults() {
    variableDict.clear();
    variableOrder.clear();
    scopeTracking.clear();
}

DeclareVariables::VariableToDeclare* DeclareVariables::ResolveVariableToDeclare(
    IL::ILVariable* variable) {
    if (variable == nullptr)
        return nullptr;
    auto it = variableDict.find(variable);
    if (it == variableDict.end())
        return nullptr;
    VariableToDeclare* v = it->second.get();
    while (v->ReplacementDueToCollision != nullptr) {
        v = v->ReplacementDueToCollision;
    }
    return v;
}

// ---- VariableNeedsDeclaration ---------------------------------------------

bool DeclareVariables::VariableNeedsDeclaration(IL::VariableKind kind) {
    switch (kind) {
        case IL::VariableKind::PinnedRegionLocal:
        case IL::VariableKind::Parameter:
        case IL::VariableKind::ExceptionLocal:
        case IL::VariableKind::ExceptionStackSlot:
        case IL::VariableKind::UsingLocal:
        case IL::VariableKind::ForeachLocal:
        case IL::VariableKind::PatternLocal:
            return false;
        default:
            return true;
    }
}

// ---- IsValidInStatementExpression -----------------------------------------

bool DeclareVariables::IsValidInStatementExpression(Syntax::Expression& expr) {
    if (dynamic_cast<Syntax::InvocationExpression*>(&expr) != nullptr
        || dynamic_cast<Syntax::ObjectCreateExpression*>(&expr) != nullptr
        || dynamic_cast<Syntax::AssignmentExpression*>(&expr) != nullptr
        || dynamic_cast<Syntax::ErrorExpression*>(&expr) != nullptr) {
        return true;
    }
    if (auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(&expr)) {
        switch (uoe->Operator()) {
            case Syntax::UnaryOperatorType::PostIncrement:
            case Syntax::UnaryOperatorType::PostDecrement:
            case Syntax::UnaryOperatorType::Increment:
            case Syntax::UnaryOperatorType::Decrement:
            case Syntax::UnaryOperatorType::Await:
                return true;
            case Syntax::UnaryOperatorType::NullConditionalRewrap:
                return IsValidInStatementExpression(*uoe->Expression());
            default:
                return false;
        }
    }
    return false;
}

// ---- FindInsertionPoints --------------------------------------------------

bool DeclareVariables::IsRelevantScope(IL::BlockContainer& scope) {
    IL::Block* entryPoint = scope.EntryPoint();
    if (entryPoint != nullptr && entryPoint->IncomingEdgeCount > 1)
        return true;
    return dynamic_cast<IL::ILFunction*>(scope.Parent) != nullptr;
}

void DeclareVariables::FindInsertionPoints(Syntax::AstNode& node, int nodeLevel) {
    IL::BlockContainer* scope = CSharp::GetBlockContainer(node);
    bool scopeAdded = false;
    if (scope != nullptr && IsRelevantScope(*scope)) {
        scopeTracking.push_back({InsertionPoint{nodeLevel, &node}, scope});
        scopeAdded = true;
    } else if (auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(&node);
               lambda != nullptr && dynamic_cast<Syntax::Expression*>(lambda->Body()) != nullptr) {
        // Expression-bodied lambdas don't have a BlockStatement linking to the
        // BlockContainer, so the body expression itself carries the insertion point.
        IL::ILFunction* function = CSharp::GetILFunction(node);
        scope = (function != nullptr && function->Body) ? function->Body.get() : nullptr;
        if (scope != nullptr) {
            scopeTracking.push_back({InsertionPoint{nodeLevel + 1, lambda->Body()}, scope});
            scopeAdded = true;
        }
    }

    auto findInsertionPointForVariable = [&](IL::ILVariable& variable) {
        InsertionPoint newPoint{};
        auto startIndex = static_cast<int>(scopeTracking.size()) - 1;
        IL::BlockContainer* captureScope = variable.CaptureScope;
        while (captureScope != nullptr && !IsRelevantScope(*captureScope)) {
            captureScope = IL::BlockContainer::FindClosestContainer(captureScope->Parent);
        }
        if (captureScope != nullptr && startIndex > 0
            && captureScope != scopeTracking[static_cast<std::size_t>(startIndex)].second) {
            while (startIndex > 0
                   && scopeTracking[static_cast<std::size_t>(startIndex)].second != captureScope) {
                startIndex--;
            }
            newPoint = scopeTracking[static_cast<std::size_t>(startIndex + 1)].first;
        } else {
            newPoint = InsertionPoint{nodeLevel, &node};
            if (variable.UsesInitialValue) {
                // Uninitialized variables are logically initialized at the beginning of
                // the function; because it's possible that the variable has a
                // loop-carried dependency, declare it outside of any loops.
                while (startIndex >= 0) {
                    IL::BlockContainer* tracked = scopeTracking[static_cast<std::size_t>(startIndex)].second;
                    IL::Block* entryPoint = tracked->EntryPoint();
                    if (entryPoint != nullptr && entryPoint->IncomingEdgeCount > 1) {
                        // declare variable outside of loop
                        newPoint = scopeTracking[static_cast<std::size_t>(startIndex)].first;
                    } else if (dynamic_cast<IL::ILFunction*>(tracked->Parent) != nullptr) {
                        // stop at beginning of function
                        break;
                    }
                    startIndex--;
                }
            }
        }
        auto it = variableDict.find(&variable);
        if (it != variableDict.end()) {
            it->second->InsertionPoint = FindCommonParent(it->second->InsertionPoint, newPoint);
        } else {
            auto v = std::make_unique<VariableToDeclare>(
                &variable, newPoint, static_cast<Syntax::IdentifierExpression*>(&node),
                static_cast<int>(variableDict.size()));
            variableOrder.push_back(v.get());
            variableDict.emplace(&variable, std::move(v));
        }
    };

    struct ScopeGuard {
        std::vector<std::pair<InsertionPoint, IL::BlockContainer*>>& tracking;
        bool active;
        ~ScopeGuard() {
            if (active)
                tracking.pop_back();
        }
    } guard{scopeTracking, scopeAdded};

    for (Syntax::AstNode* child = node.FirstChild(); child != nullptr; child = child->NextSibling()) {
        FindInsertionPoints(*child, nodeLevel + 1);
    }
    if (auto* identExpr = dynamic_cast<Syntax::IdentifierExpression*>(&node)) {
        IL::ILVariable* variable = CSharp::GetILVariable(*identExpr);
        if (variable != nullptr && VariableNeedsDeclaration(variable->Kind)) {
            findInsertionPointForVariable(*variable);
        } else if (IL::ILFunction* localFunction = CSharp::GetILFunction(node);
                   localFunction != nullptr
                   && localFunction->Kind == IL::ILFunctionKind::LocalFunction) {
            for (IL::ILVariable* captured : localFunction->CapturedVariables) {
                if (captured != nullptr && VariableNeedsDeclaration(captured->Kind))
                    findInsertionPointForVariable(*captured);
            }
        }
    }
}

// ---- FindCommonParent -----------------------------------------------------

DeclareVariables::InsertionPoint DeclareVariables::FindCommonParent(InsertionPoint oldPoint,
                                                                   InsertionPoint newPoint) {
    // First ensure we're looking at nodes on the same level:
    oldPoint = oldPoint.UpTo(newPoint.level);
    newPoint = newPoint.UpTo(oldPoint.level);
    // Then go up the tree until both points share the same parent:
    while (oldPoint.nextNode->Parent() != newPoint.nextNode->Parent()) {
        oldPoint = oldPoint.Up();
        newPoint = newPoint.Up();
    }
    // return oldPoint as that one comes first in the source code
    return oldPoint;
}

// ---- ResolveCollisions ----------------------------------------------------

void DeclareVariables::ResolveCollisions() {
    std::unordered_map<std::string, std::vector<VariableToDeclare*>> multiDict;
    for (VariableToDeclare* vptr : variableOrder) {
        VariableToDeclare& v = *vptr;
        // We can only insert variable declarations in blocks, but FindInsertionPoints()
        // didn't guarantee that it finds only blocks. Fix that up now.
        while (dynamic_cast<Syntax::BlockStatement*>(v.InsertionPoint.nextNode->Parent()) == nullptr
               && dynamic_cast<Syntax::LambdaExpression*>(v.InsertionPoint.nextNode->Parent()) == nullptr) {
            if (auto* f = dynamic_cast<Syntax::ForStatement*>(v.InsertionPoint.nextNode->Parent());
                f != nullptr && f->Initializers().Count() > 0
                && f->Initializers()[0] == v.InsertionPoint.nextNode) {
                // Special case: the initializer of a ForStatement can also declare a
                // variable (with scope local to the for loop).
                Syntax::AssignmentExpression* assignment = nullptr;
                if (IsMatchingAssignment(v, assignment))
                    break;
            }
            v.InsertionPoint = v.InsertionPoint.Up();
        }

        // Go through all potentially colliding variables:
        for (VariableToDeclare* prev : multiDict[v.Name()]) {
            if (prev->RemovedDueToCollision())
                continue;
            // Go up until both nodes are on the same level:
            InsertionPoint point1 = prev->InsertionPoint.UpTo(v.InsertionPoint.level);
            InsertionPoint point2 = v.InsertionPoint.UpTo(prev->InsertionPoint.level);
            if (point1.nextNode->Parent() == point2.nextNode->Parent()) {
                // We found a collision!
                v.InvolvedInCollision = true;
                prev->ReplacementDueToCollision = &v;
                if (prev->SourceOrder < v.SourceOrder) {
                    // Switch v's insertion point to prev's insertion point; since prev
                    // was first, it has the correct SourceOrder/FirstUse values for the
                    // new combined variable.
                    v.InsertionPoint = point1;
                    v.SourceOrder = prev->SourceOrder;
                    v.FirstUse = prev->FirstUse;
                } else {
                    // v is first in source order, so it keeps its old insertion point
                    // (and other properties), except that the insertion point is moved
                    // up to prev's level.
                    v.InsertionPoint = point2;
                }
                v.DefaultInitialization = v.DefaultInitialization | prev->DefaultInitialization;
            }
        }

        multiDict[v.Name()].push_back(&v);
    }
}

// ---- IsMatchingAssignment -------------------------------------------------

bool DeclareVariables::IsMatchingAssignment(VariableToDeclare& v,
                                           Syntax::AssignmentExpression*& assignment) {
    assignment = dynamic_cast<Syntax::AssignmentExpression*>(v.InsertionPoint.nextNode);
    if (assignment == nullptr) {
        auto* stmt = dynamic_cast<Syntax::ExpressionStatement*>(v.InsertionPoint.nextNode);
        assignment = stmt != nullptr
            ? dynamic_cast<Syntax::AssignmentExpression*>(stmt->Expression())
            : nullptr;
        if (assignment == nullptr)
            return false;
    }
    if (assignment->Operator() != Syntax::AssignmentOperatorType::Assign)
        return false;
    auto* identExpr = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    if (identExpr == nullptr)
        return false;
    return identExpr->Identifier() == v.Name() && identExpr->TypeArguments().Count() == 0;
}

// ---- Mutation-phase helpers (the deferred Run pieces) ---------------------

bool DeclareVariables::CombineDeclarationAndInitializer(VariableToDeclare& v,
                                                        TransformContext& context) {
    // `if (v.Type.IsByRefLike) return true;` -- a by-ref-like variable (a `ref
    // struct` local) must be initialized at its declaration.
    if (v.ILVariable()->Type != nullptr && v.ILVariable()->Type->IsByRefLike())
        return true;
    // `if (v.InsertionPoint.nextNode.Slot?.Kind == Slots.ForInitializer) return true;`
    // -- a for-statement initializer is always combined with the declaration.
    Syntax::AstNode* nextNode = v.InsertionPoint.nextNode;
    if (nextNode != nullptr && nextNode->Slot() != nullptr
        && nextNode->Slot()->Kind() == &Syntax::Slots::ForInitializer) {
        return true;
    }
    return !context.Settings().SeparateLocalVariableDeclarations();
}

bool DeclareVariables::CanBeDeclaredAsOutVariable(VariableToDeclare& v,
                                                  Syntax::DirectionExpression*& dirExpr,
                                                  TransformContext& context) {
    dirExpr = v.FirstUse != nullptr
        ? dynamic_cast<Syntax::DirectionExpression*>(v.FirstUse->Parent())
        : nullptr;
    if (dirExpr == nullptr || dirExpr->FieldDirection() != Syntax::FieldDirection::Out)
        return false;
    if (!context.Settings().OutVariables())
        return false;
    if (v.DefaultInitialization != VariableInitKind::None)
        return false;
    // The C# switch: IfElseStatement / ExpressionStatement return whether the node is
    // the insertion point; other statements deny promotion (a `while` condition cannot
    // declare a variable in its parent scope); a lambda body may match the insertion
    // point. Any other ancestor keeps walking up.
    for (Syntax::AstNode* node = v.FirstUse; node != nullptr; node = node->Parent()) {
        if (node->Slot() != nullptr && node->Slot()->Kind() == &Syntax::Slots::EmbeddedStatement)
            return false;
        if (dynamic_cast<Syntax::IfElseStatement*>(node) != nullptr
            || dynamic_cast<Syntax::ExpressionStatement*>(node) != nullptr) {
            return node == v.InsertionPoint.nextNode;
        }
        if (dynamic_cast<Syntax::Statement*>(node) != nullptr)
            return false;
        if (auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(node))
            return lambda->Body() == v.InsertionPoint.nextNode;
    }
    return false;
}

bool DeclareVariables::IsReferencedWithinDeclaringCall(Syntax::DirectionExpression& dirExpr,
                                                       VariableToDeclare& v) {
    Syntax::AstNode* call = dirExpr.Parent();
    if (call == nullptr)
        return false;
    for (Syntax::AstNode* argument = call->FirstChild(); argument != nullptr;
         argument = argument->NextSibling()) {
        if (argument == &dirExpr)
            continue;
        for (Syntax::AstNode* node : argument->DescendantsAndSelf()) {
            auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(node);
            if (identifier != nullptr
                && ResolveVariableToDeclare(CSharp::GetILVariable(*identifier)) == &v) {
                return true;
            }
        }
    }
    return false;
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
