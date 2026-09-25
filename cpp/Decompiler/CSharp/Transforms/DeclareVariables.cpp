// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The DeclareVariables analysis half (see DeclareVariables.hpp for the
// deferred mutation half): the insertion-point computation
// (FindInsertionPoints + the scope tracking) and the collision resolution.

#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"

#include <cassert>
#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace IL = ::ILSpy::Decompiler::IL;

namespace {

// The C# `node.Annotation<BlockContainer>()`: the first IL-instruction
// annotation that is a BlockContainer (the loop statements carry their
// container through WithILInstruction).
IL::BlockContainer* GetBlockContainerAnnotation(const Syntax::AstNode& node) {
    for (IL::ILInstruction* instruction : CS::GetILInstructions(node)) {
        if (auto* container = dynamic_cast<IL::BlockContainer*>(instruction))
            return container;
    }
    return nullptr;
}

// The C# `node.Annotation<ILFunction>()`: the first IL-instruction annotation
// that is an ILFunction (the lambda/local-function references carry theirs).
IL::ILFunction* GetILFunctionAnnotation(const Syntax::AstNode& node) {
    for (IL::ILInstruction* instruction : CS::GetILInstructions(node)) {
        if (auto* function = dynamic_cast<IL::ILFunction*>(instruction))
            return function;
    }
    return nullptr;
}

} // namespace

// ---- InsertionPoint ----------------------------------------------------------------------

DeclareVariables::InsertionPoint DeclareVariables::InsertionPoint::Up() const {
    // Insertion points live inside a method body, so walking up always finds
    // a parent (the C# null-forgiving `!` invariant).
    assert(nextNode != nullptr && nextNode->Parent() != nullptr);
    return InsertionPoint(level - 1, nextNode->Parent());
}

DeclareVariables::InsertionPoint DeclareVariables::InsertionPoint::UpTo(
    int targetLevel) const {
    InsertionPoint result = *this;
    while (result.level > targetLevel) {
        assert(result.nextNode != nullptr && result.nextNode->Parent() != nullptr);
        result.nextNode = result.nextNode->Parent();
        result.level -= 1;
    }
    return result;
}

// ---- VariableToDeclare -------------------------------------------------------------------

DeclareVariables::VariableToDeclare::VariableToDeclare(
    IL::ILVariable* variable, InsertionPoint insertionPoint,
    Syntax::IdentifierExpression* firstUse, int sourceOrder)
    : DefaultInitialization(VariableInitKind::None),
      insertionPoint(insertionPoint),
      firstUse(firstUse),
      sourceOrder(sourceOrder),
      ilVariable_(variable) {
    // The C# ctor's initialization-kind decision: a variable whose initial
    // value is read needs a default initialization (the initialized form when
    // the value was set up front, SkipInit otherwise).
    if (variable->UsesInitialValue) {
        if (variable->InitialValueIsInitialized) {
            DefaultInitialization = VariableInitKind::NeedsDefaultValue;
        } else {
            DefaultInitialization = VariableInitKind::NeedsSkipInit;
        }
    } else {
        DefaultInitialization = VariableInitKind::None;
    }
}

// ---- The analysis surface ----------------------------------------------------------------

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

void DeclareVariables::Analyze(Syntax::AstNode& rootNode) {
    ClearAnalysisResults();
    FindInsertionPoints(&rootNode, 0);
    ResolveCollisions();
}

void DeclareVariables::ClearAnalysisResults() {
    variables_.clear();
    variableDict_.clear();
    scopeTracking_.clear();
}

Syntax::AstNode* DeclareVariables::GetDeclarationPoint(IL::ILVariable* variable) {
    VariableToDeclare* v = variableDict_.at(variable);
    while (v->replacementDueToCollision != nullptr) {
        v = v->replacementDueToCollision;
    }
    return v->insertionPoint.nextNode;
}

bool DeclareVariables::WasMerged(IL::ILVariable* variable) {
    VariableToDeclare* v = variableDict_.at(variable);
    return v->involvedInCollision || v->RemovedDueToCollision();
}

// ---- FindInsertionPoints -----------------------------------------------------------------

void DeclareVariables::FindInsertionPoints(Syntax::AstNode* node, int nodeLevel) {
    // Track loops and function bodies as scopes, for comparison with
    // CaptureScope. A scope entry is added for a node carrying a relevant
    // BlockContainer annotation, or for an expression-bodied lambda (whose
    // BlockStatement-less body links to the container through the ILFunction
    // annotation).
    IL::BlockContainer* scope = GetBlockContainerAnnotation(*node);
    bool scopeAdded = false;
    if (scope != nullptr && IsRelevantScope(scope)) {
        scopeTracking_.push_back(
            ScopeTrackingEntry{InsertionPoint(nodeLevel, node), scope});
        scopeAdded = true;
    } else {
        auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(node);
        Syntax::Expression* lambdaBody =
            lambda != nullptr
                ? dynamic_cast<Syntax::Expression*>(lambda->Body())
                : nullptr;
        if (lambdaBody != nullptr) {
            // Expression-bodied lambdas don't have a BlockStatement linking
            // to the BlockContainer.
            IL::ILFunction* function = GetILFunctionAnnotation(*node);
            scope = function != nullptr
                        ? dynamic_cast<IL::BlockContainer*>(function->Body.get())
                        : nullptr;
            if (scope != nullptr) {
                scopeTracking_.push_back(
                    ScopeTrackingEntry{InsertionPoint(nodeLevel + 1, lambdaBody),
                                       scope});
                scopeAdded = true;
            }
        } else {
            scope = nullptr; // don't remove a scope if we didn't add one
        }
    }
    try {
        for (Syntax::AstNode* child = node->FirstChild(); child != nullptr;
             child = child->NextSibling()) {
            FindInsertionPoints(child, nodeLevel + 1);
        }
        if (auto* identExpr = dynamic_cast<Syntax::IdentifierExpression*>(node);
            identExpr != nullptr) {
            IL::ILVariable* variable = CS::GetILVariable(*identExpr);
            if (variable != nullptr &&
                VariableNeedsDeclaration(variable->Kind)) {
                FindInsertionPointForVariable(variable, identExpr, nodeLevel);
            } else if (IL::ILFunction* localFunction =
                           GetILFunctionAnnotation(*node);
                       localFunction != nullptr &&
                       localFunction->Kind == IL::ILFunctionKind::LocalFunction) {
                for (const IL::ILVariablePtr& v : localFunction->CapturedVariables) {
                    if (VariableNeedsDeclaration(v->Kind))
                        FindInsertionPointForVariable(v.get(), identExpr, nodeLevel);
                }
            }
        }
    } catch (...) {
        if (scopeAdded)
            scopeTracking_.pop_back();
        throw;
    }
    if (scopeAdded)
        scopeTracking_.pop_back();
}

void DeclareVariables::FindInsertionPointForVariable(
    IL::ILVariable* variable, Syntax::IdentifierExpression* identExpr,
    int nodeLevel) {
    InsertionPoint newPoint;
    int startIndex = static_cast<int>(scopeTracking_.size()) - 1;
    IL::BlockContainer* captureScope = variable->CaptureScope;
    while (captureScope != nullptr && !IsRelevantScope(captureScope)) {
        captureScope = IL::BlockContainer::FindClosestContainer(captureScope->Parent);
    }
    if (captureScope != nullptr && startIndex > 0 &&
        captureScope != scopeTracking_[static_cast<std::size_t>(startIndex)].scope) {
        while (startIndex > 0 &&
               scopeTracking_[static_cast<std::size_t>(startIndex)].scope !=
                   captureScope) {
            startIndex--;
        }
        newPoint =
            scopeTracking_[static_cast<std::size_t>(startIndex) + 1].insertionPoint;
    } else {
        newPoint = InsertionPoint(nodeLevel, identExpr);
        if (variable->UsesInitialValue) {
            // Uninitialized variables are logically initialized at the
            // beginning of the function. Because it's possible that the
            // variable has a loop-carried dependency, declare it outside of
            // any loops.
            while (startIndex >= 0) {
                const ScopeTrackingEntry& entry =
                    scopeTracking_[static_cast<std::size_t>(startIndex)];
                if (entry.scope->EntryPoint() != nullptr &&
                    entry.scope->EntryPoint()->IncomingEdgeCount > 1) {
                    // declare variable outside of loop
                    newPoint = entry.insertionPoint;
                } else if (dynamic_cast<IL::ILFunction*>(entry.scope->Parent) !=
                           nullptr) {
                    // stop at beginning of function
                    break;
                }
                startIndex--;
            }
        }
    }
    auto it = variableDict_.find(variable);
    if (it != variableDict_.end()) {
        it->second->insertionPoint =
            FindCommonParent(it->second->insertionPoint, newPoint);
    } else {
        auto v = std::make_unique<VariableToDeclare>(
            variable, newPoint, identExpr,
            static_cast<int>(variables_.size()));
        VariableToDeclare* raw = v.get();
        variables_.push_back(std::move(v));
        variableDict_.emplace(variable, raw);
    }
}

bool DeclareVariables::IsRelevantScope(IL::BlockContainer* scope) {
    return (scope->EntryPoint() != nullptr &&
            scope->EntryPoint()->IncomingEdgeCount > 1) ||
           dynamic_cast<IL::ILFunction*>(scope->Parent) != nullptr;
}

DeclareVariables::InsertionPoint DeclareVariables::FindCommonParent(
    InsertionPoint oldPoint, InsertionPoint newPoint) const {
    // First ensure we're looking at nodes on the same level:
    oldPoint = oldPoint.UpTo(newPoint.level);
    newPoint = newPoint.UpTo(oldPoint.level);
    assert(newPoint.level == oldPoint.level);
    // Then go up the tree until both points share the same parent:
    while (oldPoint.nextNode->Parent() != newPoint.nextNode->Parent()) {
        oldPoint = oldPoint.Up();
        newPoint = newPoint.Up();
    }
    // return oldPoint as that one comes first in the source code
    return oldPoint;
}

// ---- ResolveCollisions -------------------------------------------------------------------

void DeclareVariables::ResolveCollisions() {
    // The C# `MultiDictionary<string, VariableToDeclare>` (a name-keyed
    // multimap preserving insertion order).
    std::unordered_map<std::string, std::vector<VariableToDeclare*>> multiDict;
    for (std::unique_ptr<VariableToDeclare>& v : variables_) {
        // We can only insert variable declarations in blocks, but
        // FindInsertionPoints() didn't guarantee that it finds only blocks.
        // Fix that up now.
        while (!(dynamic_cast<Syntax::BlockStatement*>(
                     v->insertionPoint.nextNode->Parent()) != nullptr ||
                 dynamic_cast<Syntax::LambdaExpression*>(
                     v->insertionPoint.nextNode->Parent()) != nullptr)) {
            auto* forStatement = dynamic_cast<Syntax::ForStatement*>(
                v->insertionPoint.nextNode->Parent());
            Syntax::Statement* firstInitializer =
                forStatement != nullptr && forStatement->Initializers().Count() > 0
                    ? forStatement->Initializers().At(0)
                    : nullptr;
            if (forStatement != nullptr &&
                v->insertionPoint.nextNode == firstInitializer &&
                IsMatchingAssignment(*v)) {
                // Special case: the initializer of a ForStatement can also
                // declare a variable (with scope local to the for loop).
                break;
            }
            v->insertionPoint = v->insertionPoint.Up();
        }
        // Note: 'out var', pattern matching etc. is not considered a valid
        // insertion point here, because the scope of the resulting variable
        // is not restricted to the parent node of the insertion point, but
        // extends to the whole BlockStatement. We moved up the insertion
        // point to the whole BlockStatement so that we can resolve
        // collisions, later we might decide to declare the variable more
        // locally (as 'out var') instead if still possible.

        // Go through all potentially colliding variables:
        for (VariableToDeclare* prev : multiDict[v->Name()]) {
            if (prev->RemovedDueToCollision())
                continue;
            // Go up until both nodes are on the same level:
            InsertionPoint point1 =
                prev->insertionPoint.UpTo(v->insertionPoint.level);
            InsertionPoint point2 =
                v->insertionPoint.UpTo(prev->insertionPoint.level);
            assert(point1.level == point2.level);
            if (point1.nextNode->Parent() == point2.nextNode->Parent()) {
                assert(prev->Type()->Equals(*v->Type()));
                // We found a collision!
                v->involvedInCollision = true;
                prev->replacementDueToCollision = v.get();
                // Continue checking other entries in multiDict against the
                // new position of `v`.
                if (prev->sourceOrder < v->sourceOrder) {
                    // Switch v's insertion point to prev's insertion point:
                    v->insertionPoint = point1;
                    // Since prev was first, it has the correct
                    // SourceOrder/FirstUse values for the new combined
                    // variable:
                    v->sourceOrder = prev->sourceOrder;
                    v->firstUse = prev->firstUse;
                } else {
                    // v is first in source order, so it keeps its old
                    // insertion point (and other properties), except that
                    // the insertion point is moved up to prev's level.
                    v->insertionPoint = point2;
                }
                v->DefaultInitialization = static_cast<VariableInitKind>(
                    static_cast<int>(v->DefaultInitialization) |
                    static_cast<int>(prev->DefaultInitialization));
                // We don't need to re-check the dict entries that we already
                // checked earlier, because the new v.InsertionPoint only
                // collides with another point x if either the old
                // v.InsertionPoint or the old prev.InsertionPoint already
                // collided with x.
            }
        }

        multiDict[v->Name()].push_back(v.get());
    }
}

bool DeclareVariables::IsMatchingAssignment(
    VariableToDeclare& v, Syntax::AssignmentExpression** assignmentOut) const {
    Syntax::AssignmentExpression* assignment = dynamic_cast<Syntax::AssignmentExpression*>(
        v.insertionPoint.nextNode);
    if (assignment == nullptr) {
        auto* statement = dynamic_cast<Syntax::ExpressionStatement*>(
            v.insertionPoint.nextNode);
        assignment =
            statement != nullptr
                ? dynamic_cast<Syntax::AssignmentExpression*>(statement->Expression())
                : nullptr;
        if (assignment == nullptr) {
            if (assignmentOut != nullptr)
                *assignmentOut = nullptr;
            return false;
        }
    }
    if (assignmentOut != nullptr)
        *assignmentOut = assignment;
    auto* identExpr = dynamic_cast<Syntax::IdentifierExpression*>(assignment->Left());
    return assignment->Operator() == Syntax::AssignmentOperatorType::Assign &&
           identExpr != nullptr && identExpr->Identifier() == v.Name() &&
           identExpr->TypeArguments().Count() == 0;
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
