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
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ErrorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/LambdaExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cassert>
#include <functional>
#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace IL = ::ILSpy::Decompiler::IL;

namespace {

// (The C# `node.Annotation<BlockContainer>()` / `Annotation<ILFunction>()`
// queries moved to the shared Annotations surface:
// CS::GetBlockContainerAnnotation / CS::GetILFunctionAnnotation.)

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
    const IL::ILVariablePtr& variable, InsertionPoint insertionPoint,
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

void DeclareVariables::Run(Syntax::AstNode& rootNode, TransformContext& context) {
    try {
        if (context_ != nullptr)
            throw std::logic_error("Reentrancy in DeclareVariables?");
        context_ = &context;
        ClearAnalysisResults();
        EnsureExpressionStatementsAreValid(&rootNode);
        FindInsertionPoints(&rootNode, 0);
        ResolveCollisions();
        // InsertDeconstructionVariableDeclarations is DEFERRED loudly (the
        // C# call site, line ~484): it needs
        // StatementBuilder.TranslateDeconstructionDesignation, which is not
        // ported. Deconstruction assignments keep their tuple-expression
        // left side until that lands.
        InsertVariableDeclarations();
        UpdateAnnotations(&rootNode);
    } catch (...) {
        context_ = nullptr;
        ClearAnalysisResults();
        throw;
    }
    context_ = nullptr;
    ClearAnalysisResults();
}

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
    IL::BlockContainer* scope = CS::GetBlockContainerAnnotation(*node);
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
            IL::ILFunction* function = CS::GetILFunctionAnnotation(*node);
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
                           CS::GetILFunctionAnnotation(*node);
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
        // A non-owning alias over the caller-owned variable (the IL function
        // tree owns it; the annotation constructions downstream copy the
        // alias without taking ownership -- the no-op-deleter convention).
        IL::ILVariablePtr variableHandle(variable, [](IL::ILVariable*) {});
        auto v = std::make_unique<VariableToDeclare>(
            variableHandle, newPoint, identExpr,
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

// ---- The mutation half -------------------------------------------------------------------

void DeclareVariables::EnsureExpressionStatementsAreValid(Syntax::AstNode* rootNode) {
    for (Syntax::AstNode* node : rootNode->DescendantsAndSelf()) {
        auto* stmt = dynamic_cast<Syntax::ExpressionStatement*>(node);
        if (stmt == nullptr)
            continue;
        auto* dir = dynamic_cast<Syntax::DirectionExpression*>(stmt->Expression());
        if (dir != nullptr && IsValidInStatementExpression(dir->Expression())) {
            context_->StepOnce("Unwrap direction expression statement", stmt);
            stmt->Expression(Syntax::Detach(dir->Expression()));
        } else if (!IsValidInStatementExpression(stmt->Expression())) {
            // fetch ILFunction: the root ILFunction annotation among the
            // statement's ancestors (the C# `Ancestors.SelectMany(a =>
            // a.Annotations.OfType<ILFunction>()).First(f => f.Parent ==
            // null)`; the First on an empty sequence throws).
            IL::ILFunction* function = nullptr;
            for (Syntax::AstNode* ancestor = stmt; ancestor != nullptr;
                 ancestor = ancestor->Parent()) {
                if (IL::ILFunction* candidate = CS::GetILFunctionAnnotation(*ancestor);
                    candidate != nullptr && candidate->Parent == nullptr) {
                    function = candidate;
                    break;
                }
            }
            if (function == nullptr)
                throw std::runtime_error(
                    "DeclareVariables: no root ILFunction annotation found for the "
                    "invalid expression statement");
            // if possible use C# 7.0 discard-assignment
            if (context_->DecompileRun->Settings().Discards() &&
                !ExpressionBuilder::HidesVariableWithName(*function, "_")) {
                context_->StepOnce("Assign invalid expression statement to discard", stmt);
                stmt->Expression(new Syntax::AssignmentExpression(
                    new Syntax::IdentifierExpression("_"),
                    // no ResolveResult
                    Syntax::Detach(stmt->Expression())));
            } else {
                // DEFERRED loudly (the C# temporary arm, line ~249): it needs
                // AssignVariableNames.GenerateVariableName over the function
                // and the UsingScope, which is not ported. The invalid
                // statement keeps its shape until that lands.
                context_->StepOnce(
                    "Assign invalid expression statement to temporary -- DEFERRED",
                    stmt);
            }
        }
    }
}

bool DeclareVariables::IsValidInStatementExpression(Syntax::Expression* expr) {
    if (dynamic_cast<Syntax::InvocationExpression*>(expr) != nullptr ||
        dynamic_cast<Syntax::ObjectCreateExpression*>(expr) != nullptr ||
        dynamic_cast<Syntax::AssignmentExpression*>(expr) != nullptr ||
        dynamic_cast<Syntax::ErrorExpression*>(expr) != nullptr)
        return true;
    if (auto* uoe = dynamic_cast<Syntax::UnaryOperatorExpression*>(expr)) {
        switch (uoe->Operator()) {
            case Syntax::UnaryOperatorType::PostIncrement:
            case Syntax::UnaryOperatorType::PostDecrement:
            case Syntax::UnaryOperatorType::Increment:
            case Syntax::UnaryOperatorType::Decrement:
            case Syntax::UnaryOperatorType::Await:
                return true;
            case Syntax::UnaryOperatorType::NullConditionalRewrap:
                return IsValidInStatementExpression(uoe->Expression());
            default:
                return false;
        }
    }
    return false;
}

bool DeclareVariables::CombineDeclarationAndInitializer(VariableToDeclare& v) {
    if (v.Type()->IsByRefLike())
        return true; // by-ref-like variables always must be initialized at their declaration.

    const Syntax::CSharpSlotInfo* slot = v.insertionPoint.nextNode->Slot();
    if (slot != nullptr && slot->Kind() == &Syntax::Slots::ForInitializer)
        return true; // for-statement initializers always should combine declaration and initialization.

    return !context_->DecompileRun->Settings().SeparateLocalVariableDeclarations();
}

void DeclareVariables::InsertVariableDeclarations() {
    // The C# replacements list: (oldNode, createNewNode, stepDescription)
    // -- performed at the end, so that no node is replaced while it is still
    // referenced by a VariableToDeclare.
    struct Replacement {
        Syntax::AstNode* oldNode;
        std::function<Syntax::AstNode*()> createNewNode;
        const char* stepDescription;
    };
    std::vector<Replacement> replacements;
    for (std::unique_ptr<VariableToDeclare>& vPtr : variables_) {
        VariableToDeclare& v = *vPtr;
        if (v.RemovedDueToCollision() || v.declaredInDeconstruction)
            continue;

        Syntax::AssignmentExpression* assignment = nullptr;
        Syntax::DirectionExpression* dirExpr = nullptr;
        if (CombineDeclarationAndInitializer(v) && IsMatchingAssignment(v, &assignment)) {
            // 'int v; v = expr;' can be combined to 'int v = expr;'
            Syntax::AstType* type =
                context_->DecompileRun->Settings().AnonymousTypes() &&
                        TS::ContainsAnonymousType(*v.ILVariable()->Type)
                    ? static_cast<Syntax::AstType*>(
                          new Syntax::SimpleType("var"))
                    : context_->TypeSystemAstBuilder->ConvertType(
                          *v.ILVariable()->Type);
            // (The C# IsRefReadOnly readonly-specifier fixup is DEFERRED: the
            // ILVariable flag is not ported.)
            if (v.ILVariable()->Kind == IL::VariableKind::PinnedLocal) {
                type->AddTrailingTrivia(
                    new Syntax::Comment("pinned", Syntax::CommentType::MultiLine));
            }
            replacements.push_back(Replacement{
                v.insertionPoint.nextNode,
                [&v, assignment, type]() -> Syntax::AstNode* {
                    auto* vds = new Syntax::VariableDeclarationStatement(
                        type, v.Name(), Syntax::Detach(assignment->Right()));
                    Syntax::VariableInitializer* init = vds->Variables().At(0);
                    init->AddAnnotation(
                        CS::GetSharedResolveResult(*assignment->Left()));
                    // Move the non-resolve-result annotations of the
                    // assignment's left side and of the whole assignment
                    // onto the initializer.
                    for (const auto& annotation :
                         assignment->Left()->SharedAnnotations()) {
                        if (dynamic_cast<const Sem::ResolveResult*>(
                                annotation.get()) == nullptr)
                            init->AddAnnotation(annotation);
                    }
                    for (const auto& annotation : assignment->SharedAnnotations()) {
                        if (dynamic_cast<const Sem::ResolveResult*>(
                                annotation.get()) == nullptr)
                            init->AddAnnotation(annotation);
                    }
                    return vds;
                },
                "Combine variable declaration with initializer"});
        } else if (CanBeDeclaredAsOutVariable(v, &dirExpr)) {
            // 'T v; SomeCall(out v);' can be combined to 'SomeCall(out T v);'
            Syntax::AstType* type = nullptr;
            bool isOutVar = false;
            if (context_->DecompileRun->Settings().AnonymousTypes() &&
                TS::ContainsAnonymousType(*v.ILVariable()->Type)) {
                type = new Syntax::SimpleType("var");
                isOutVar = true;
            } else {
                // (The C# UseImplicitlyTypedOutAnnotation `var` decision is
                // DEFERRED: the annotation surface is not ported, so only
                // the anonymous-type arm reaches the implicit form.)
                type = context_->TypeSystemAstBuilder->ConvertType(*v.ILVariable()->Type);
            }
            std::string name;
            // Variable is not used and discards are allowed, we can simplify
            // this to 'out T _'.
            if (context_->DecompileRun->Settings().Discards() &&
                v.ILVariable()->LoadCount == 0 && v.ILVariable()->StoreCount == 0 &&
                v.ILVariable()->AddressCount == 1) {
                name = "_";
            } else {
                name = v.Name();
            }
            auto* ovd = new Syntax::OutVarDeclarationExpression(type, name);
            ovd->Variable()->AddAnnotation(
                std::make_shared<CS::ILVariableResolveResult>(v.ILVariableHandle()));
            CS::CopyAnnotationsFrom(ovd, *dirExpr);
            if (isOutVar) {
                ovd->RemoveAnnotations<Sem::ResolveResult>();
                ovd->AddAnnotation(std::make_shared<Sem::OutVarResolveResult>(
                    v.ILVariable()->Type));
            }
            Replacement replacement{
                dirExpr, [ovd]() -> Syntax::AstNode* { return ovd; },
                "Declare out variable"};
            replacements.push_back(std::move(replacement));
        } else {
            // Insert a separate declaration statement.
            Syntax::AstType* type = context_->TypeSystemAstBuilder->ConvertType(*v.ILVariable()->Type);
            Syntax::Expression* initializer = nullptr;
            if (v.DefaultInitialization == VariableInitKind::NeedsDefaultValue) {
                initializer = new Syntax::DefaultValueExpression(type->Clone());
            }
            auto* vds =
                new Syntax::VariableDeclarationStatement(type, v.Name(), initializer);
            vds->Variables().At(0)->AddAnnotation(
                std::make_shared<CS::ILVariableResolveResult>(v.ILVariableHandle()));
            context_->StepOnce("Insert variable declaration", v.insertionPoint.nextNode);
            if (auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(
                    v.insertionPoint.nextNode->Parent());
                lambda != nullptr) {
                // An expression-bodied lambda body gets wrapped in a block
                // with a return statement, so the declaration has a block to
                // live in.
                auto* lambdaBody = dynamic_cast<Syntax::Expression*>(lambda->Body());
                assert(lambdaBody != nullptr);
                auto* blockStatement = new Syntax::BlockStatement();
                blockStatement->Statements().Add(
                    new Syntax::ReturnStatement(Syntax::Detach(lambdaBody)));
                lambda->Body(blockStatement);
            }
            if (dynamic_cast<Syntax::ReturnStatement*>(
                    v.insertionPoint.nextNode->Parent()) != nullptr) {
                v.insertionPoint = v.insertionPoint.Up();
            }
            assert(v.insertionPoint.nextNode->Slot() != nullptr &&
                   v.insertionPoint.nextNode->Slot()->Kind() ==
                       &Syntax::Slots::Statement);
            // The insertion point is a statement within a block, so it always
            // has a parent.
            Syntax::AstNode* insertionNode = v.insertionPoint.nextNode;
            Syntax::AstNode* insertionParent = insertionNode->Parent();
            if (insertionParent == nullptr)
                throw std::runtime_error("Variable insertion point has no parent.");
            // (The C# NeedsSkipInit forms -- the SkipInit call statements --
            // are DEFERRED: they need a live context.TypeSystem for
            // FindType(KnownTypeCode.Unsafe). The plain declaration is
            // inserted; the skip-init marker stays on the variable's
            // DefaultInitialization value until the compilation wiring
            // lands.)
            insertionParent->InsertChildBefore(insertionNode, vds,
                                                 &Syntax::Slots::Statement);
        }
    }
    // perform replacements at end, so that we don't replace a node while it
    // is still referenced by a VariableToDeclare
    for (Replacement& replacement : replacements) {
        context_->StepOnce(replacement.stepDescription, replacement.oldNode);
        Syntax::AstNode* newNode = replacement.createNewNode();
        replacement.oldNode->ReplaceWith(newNode);
    }
}

bool DeclareVariables::CanBeDeclaredAsOutVariable(
    VariableToDeclare& v, Syntax::DirectionExpression** dirExprOut) {
    *dirExprOut = dynamic_cast<Syntax::DirectionExpression*>(v.firstUse->Parent());
    Syntax::DirectionExpression* dirExpr = *dirExprOut;
    if (dirExpr == nullptr || dirExpr->FieldDirection() != Syntax::FieldDirection::Out)
        return false;
    if (!context_->DecompileRun->Settings().OutVariables())
        return false;
    if (v.DefaultInitialization != VariableInitKind::None)
        return false;
    for (Syntax::AstNode* node = v.firstUse; node != nullptr; node = node->Parent()) {
        const Syntax::CSharpSlotInfo* slot = node->Slot();
        if (slot != nullptr && slot->Kind() == &Syntax::Slots::EmbeddedStatement) {
            return false;
        }
        if (dynamic_cast<Syntax::IfElseStatement*>(node) != nullptr) {
            // variable declared in if condition appears in parent scope
            return node == v.insertionPoint.nextNode;
        }
        if (dynamic_cast<Syntax::ExpressionStatement*>(node) != nullptr)
            return node == v.insertionPoint.nextNode;
        if (dynamic_cast<Syntax::Statement*>(node) != nullptr) {
            // other statements (e.g. while) don't allow variables to be
            // promoted to parent scope
            return false;
        }
        if (auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(node))
            return lambda->Body() == v.insertionPoint.nextNode;
    }
    return false;
}

bool DeclareVariables::IsReferencedWithinDeclaringCall(
    Syntax::DirectionExpression* dirExpr, VariableToDeclare& v) {
    Syntax::AstNode* call = dirExpr->Parent();
    if (call == nullptr)
        return false;
    for (Syntax::AstNode* argument = call->FirstChild(); argument != nullptr;
         argument = argument->NextSibling()) {
        if (argument == dirExpr)
            continue;
        for (Syntax::AstNode* node : argument->DescendantsAndSelf()) {
            if (auto* identifier = dynamic_cast<Syntax::IdentifierExpression*>(node);
                identifier != nullptr &&
                ResolveVariableToDeclare(CS::GetILVariable(*identifier)) == &v)
                return true;
        }
    }
    return false;
}

DeclareVariables::VariableToDeclare* DeclareVariables::ResolveVariableToDeclare(
    IL::ILVariable* variable) {
    if (variable == nullptr)
        return nullptr;
    auto it = variableDict_.find(variable);
    if (it == variableDict_.end())
        return nullptr;
    VariableToDeclare* v = it->second;
    while (v->replacementDueToCollision != nullptr) {
        v = v->replacementDueToCollision;
    }
    return v;
}

void DeclareVariables::UpdateAnnotations(Syntax::AstNode* rootNode) {
    for (Syntax::AstNode* node : rootNode->Descendants()) {
        IL::ILVariable* ilVar = nullptr;
        if (auto* id = dynamic_cast<Syntax::IdentifierExpression*>(node)) {
            ilVar = CS::GetILVariable(*id);
        } else if (auto* vi = dynamic_cast<Syntax::VariableInitializer*>(node)) {
            ilVar = CS::GetILVariable(*vi);
        } else {
            continue;
        }
        if (ilVar == nullptr || !VariableNeedsDeclaration(ilVar->Kind))
            continue;
        auto it = variableDict_.find(ilVar);
        if (it == variableDict_.end())
            throw std::out_of_range(
                "DeclareVariables::UpdateAnnotations: variable not analyzed");
        VariableToDeclare* v = it->second;
        if (!v->RemovedDueToCollision())
            continue;
        while (v->replacementDueToCollision != nullptr) {
            v = v->replacementDueToCollision;
        }
        node->RemoveAnnotations<Sem::ResolveResult>();
        node->AddAnnotation(std::make_shared<CS::ILVariableResolveResult>(
            v->ILVariableHandle(), v->ILVariable()->Type));
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
