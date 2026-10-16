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
#include "Decompiler/NRExtensions.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
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
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DefaultValueExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/OutVarDeclarationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
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

// The shared `ILVariable` handle for a VariableToDeclare: read off the first
// use's existing ILVariableResolveResult annotation (the identifier
// FindInsertionPoints analyzed -- `GetILVariable` succeeded on it, so the
// annotation with the owning shared handle is present).
IL::ILVariablePtr VariableHandleOf(
    const DeclareVariables::VariableToDeclare& v) {
    if (v.FirstUse == nullptr)
        return nullptr;
    if (const auto* rr =
            v.FirstUse->Annotation<ILVariableResolveResult>())
        return rr->VariableHandle();
    return nullptr;
}

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

// The C# `public override void Run(AstNode rootNode, TransformContext context)`:
// the ctor-guarded context field, the statement-expression validation, the
// analysis half (via ClearAnalysisResults + FindInsertionPoints +
// ResolveCollisions), the insertion sweep, the annotation re-targeting.
void DeclareVariables::Run(Syntax::AstNode& rootNode, TransformContext& context) {
    if (context_ != nullptr)
        throw std::logic_error("Reentrancy in DeclareVariables?");
    context_ = &context;
    try {
        ClearAnalysisResults();
        EnsureExpressionStatementsAreValid(&rootNode);
        FindInsertionPoints(rootNode, 0);
        ResolveCollisions();
        // InsertDeconstructionVariableDeclarations is DEFERRED loudly (the C#
        // call site): it needs StatementBuilder.TranslateDeconstructionDesignation,
        // which is not ported. Deconstruction assignments keep their
        // tuple-expression left side until that lands.
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


// The C# `void EnsureExpressionStatementsAreValid(AstNode rootNode)` (lines
// ~225-252): the DirectionExpression-statement unwrap and the
// invalid-statement-expression discard assignment.
void DeclareVariables::EnsureExpressionStatementsAreValid(Syntax::AstNode* rootNode) {
    for (Syntax::AstNode* node : rootNode->DescendantsAndSelf()) {
        auto* stmt = dynamic_cast<Syntax::ExpressionStatement*>(node);
        if (stmt == nullptr)
            continue;
        auto* dir = dynamic_cast<Syntax::DirectionExpression*>(stmt->Expression());
        if (dir != nullptr && IsValidInStatementExpression(*dir->Expression())) {
            context_->Step("Unwrap direction expression statement", stmt);
            stmt->Expression(Detach(dir->Expression()));
        } else if (!IsValidInStatementExpression(*stmt->Expression())) {
            // Fetch the root ILFunction annotation among the statement's
            // ancestors (the C# `Ancestors.SelectMany(a => a.Annotations
            // .OfType<ILFunction>()).First(f => f.Parent == null)`).
            IL::ILFunction* function = nullptr;
            for (Syntax::AstNode* ancestor = stmt; ancestor != nullptr;
                 ancestor = ancestor->Parent()) {
                if (IL::ILFunction* candidate = GetILFunctionAnnotation(*ancestor);
                    candidate != nullptr && candidate->Parent == nullptr) {
                    function = candidate;
                    break;
                }
            }
            if (function == nullptr)
                throw std::runtime_error(
                    "DeclareVariables: no root ILFunction annotation found for the "
                    "invalid expression statement");
            // If possible use a C# 7.0 discard-assignment.
            if (context_->Settings().Discards()
                && !ExpressionBuilder::HidesVariableWithName(*function, "_")) {
                context_->Step("Assign invalid expression statement to discard", stmt);
                stmt->Expression(new Syntax::AssignmentExpression(
                    new Syntax::IdentifierExpression("_"),
                    // no ResolveResult
                    Detach(stmt->Expression())));
            } else {
                // DEFERRED loudly (the C# temporary arm): it needs
                // AssignVariableNames.GenerateVariableName over the function
                // and the UsingScope, which is not ported. The invalid
                // statement keeps its shape until that lands.
                context_->Step(
                    "Assign invalid expression statement to temporary -- DEFERRED",
                    stmt);
            }
        }
    }
}

// The C# `void InsertVariableDeclarations()` (lines 502-716): the insertion
// sweep. The node replacements are performed at the end so that no node is
// replaced while it is still referenced by a VariableToDeclare.
void DeclareVariables::InsertVariableDeclarations() {
    struct Replacement {
        Syntax::AstNode* oldNode;
        std::function<Syntax::AstNode*()> createNewNode;
        const char* stepDescription;
    };
    std::vector<Replacement> replacements;
    for (VariableToDeclare* vPtr : variableOrder) {
        VariableToDeclare& v = *vPtr;
        if (v.RemovedDueToCollision() || v.DeclaredInDeconstruction)
            continue;

        Syntax::AssignmentExpression* assignment = nullptr;
        Syntax::DirectionExpression* dirExpr = nullptr;
        if (CombineDeclarationAndInitializer(v, *context_)
            && IsMatchingAssignment(v, assignment)) {
            // 'int v; v = expr;' can be combined to 'int v = expr;'
            Syntax::AstType* type =
                context_->Settings().AnonymousTypes()
                        && ::ILSpy::Decompiler::ContainsAnonymousType(*v.ILVariable()->Type)
                    ? static_cast<Syntax::AstType*>(new Syntax::SimpleType("var"))
                    : context_->TypeSystemAstBuilder().ConvertType(
                          *v.ILVariable()->Type);
            // (The C# IsRefReadOnly readonly-specifier fixup is DEFERRED: the
            // ILVariable flag is not ported.)
            if (v.ILVariable()->Kind == IL::VariableKind::PinnedLocal) {
                type->AddTrailingTrivia(
                    new Syntax::Comment("pinned", Syntax::CommentType::MultiLine));
            }
            replacements.push_back(Replacement{
                v.InsertionPoint.nextNode,
                [&v, assignment, type]() -> Syntax::AstNode* {
                    auto* vds = new Syntax::VariableDeclarationStatement(
                        type, v.Name(), Detach(assignment->Right()));
                    Syntax::VariableInitializer* init = vds->Variables().At(0);
                    init->AddAnnotation(GetSharedResolveResult(*assignment->Left()));
                    // Move the non-resolve-result annotations of the
                    // assignment's left side and of the whole assignment
                    // onto the initializer.
                    for (const auto& annotation :
                         assignment->Left()->SharedAnnotations()) {
                        if (dynamic_cast<const Semantics::ResolveResult*>(annotation.get())
                            == nullptr)
                            init->AddAnnotation(annotation);
                    }
                    for (const auto& annotation : assignment->SharedAnnotations()) {
                        if (dynamic_cast<const Semantics::ResolveResult*>(annotation.get())
                            == nullptr)
                            init->AddAnnotation(annotation);
                    }
                    return vds;
                },
                "Combine variable declaration with initializer"});
        } else if (CanBeDeclaredAsOutVariable(v, dirExpr, *context_)) {
            // 'T v; SomeCall(out v);' can be combined to 'SomeCall(out T v);'
            Syntax::AstType* type = nullptr;
            bool isOutVar = false;
            if (context_->Settings().AnonymousTypes()
                && ::ILSpy::Decompiler::ContainsAnonymousType(*v.ILVariable()->Type)) {
                type = new Syntax::SimpleType("var");
                isOutVar = true;
            } else {
                // (The C# UseImplicitlyTypedOutAnnotation `var` decision is
                // DEFERRED: the annotation surface is not ported, so only
                // the anonymous-type arm reaches the implicit form.)
                type = context_->TypeSystemAstBuilder().ConvertType(
                    *v.ILVariable()->Type);
            }
            std::string name;
            // Variable is not used and discards are allowed, so simplify to
            // 'out T _'.
            if (context_->Settings().Discards()
                && v.ILVariable()->LoadCount == 0 && v.ILVariable()->StoreCount == 0
                && v.ILVariable()->AddressCount == 1) {
                name = "_";
            } else {
                name = v.Name();
            }
            auto* ovd = new Syntax::OutVarDeclarationExpression(type, name);
            ovd->Variable()->AddAnnotation(
                std::make_shared<ILVariableResolveResult>(VariableHandleOf(v)));
            CopyAnnotationsFrom(ovd, *dirExpr);
            if (isOutVar) {
                ovd->RemoveAnnotations<Semantics::ResolveResult>();
                ovd->AddAnnotation(
                    std::make_shared<Semantics::OutVarResolveResult>(v.ILVariable()->Type));
            }
            replacements.push_back(
                Replacement{dirExpr,
                            [ovd]() -> Syntax::AstNode* { return ovd; },
                            "Declare out variable"});
        } else {
            // Insert a separate declaration statement.
            Syntax::AstType* type = context_->TypeSystemAstBuilder().ConvertType(
                *v.ILVariable()->Type);
            Syntax::Expression* initializer = nullptr;
            if (v.DefaultInitialization == VariableInitKind::NeedsDefaultValue) {
                initializer = new Syntax::DefaultValueExpression(type->Clone());
            }
            auto* vds =
                new Syntax::VariableDeclarationStatement(type, v.Name(), initializer);
            context_->Step("Insert variable declaration", v.InsertionPoint.nextNode);
            if (auto* lambda = dynamic_cast<Syntax::LambdaExpression*>(
                    v.InsertionPoint.nextNode->Parent());
                lambda != nullptr) {
                // An expression-bodied lambda body gets wrapped in a block
                // with a return statement, so the declaration has a block to
                // live in.
                auto* lambdaBody = dynamic_cast<Syntax::Expression*>(lambda->Body());
                assert(lambdaBody != nullptr);
                auto* blockStatement = new Syntax::BlockStatement();
                blockStatement->Statements().Add(
                    new Syntax::ReturnStatement(Detach(lambdaBody)));
                lambda->Body(blockStatement);
            }
            if (dynamic_cast<Syntax::ReturnStatement*>(
                    v.InsertionPoint.nextNode->Parent())
                != nullptr) {
                v.InsertionPoint = v.InsertionPoint.Up();
            }
            assert(v.InsertionPoint.nextNode->Slot() != nullptr
                   && v.InsertionPoint.nextNode->Slot()->Kind()
                       == &Syntax::Slots::Statement);
            // The insertion point is a statement within a block, so it
            // always has a parent.
            Syntax::AstNode* insertionNode = v.InsertionPoint.nextNode;
            Syntax::AstNode* insertionParent = insertionNode->Parent();
            if (insertionParent == nullptr)
                throw std::runtime_error(
                    "Variable insertion point has no parent.");
            // The C# NeedsSkipInit forms (lines 717-770): the
            // System.Runtime.CompilerServices.Unsafe.SkipInit call marks the
            // uninitialized local -- with the out-variables setting the
            // declaration folds into the call's argument (no separate
            // declaration renders); otherwise the plain declaration is
            // followed by the call.
            if (v.DefaultInitialization == VariableInitKind::NeedsSkipInit) {
                // The C# `context.TypeSystemAstBuilder.ConvertType(
                // context.TypeSystem.FindType(KnownTypeCode.Unsafe))`.
                Syntax::AstType* unsafeType =
                    context_->TypeSystemAstBuilder().ConvertType(
                        const_cast<::ILSpy::Decompiler::TypeSystem::IType&>(
                            context_->TypeSystem().FindType(
                                ::ILSpy::Decompiler::TypeSystem::KnownTypeCode::Unsafe)));
                Syntax::ExpressionStatement* skipInitStatement;
                Syntax::AstNode* insertedNode;
                if (context_->Settings().OutVariables()) {
                    auto* outVarDecl = new Syntax::OutVarDeclarationExpression(
                        type->Clone(), v.Name());
                    outVarDecl->Variable()->AddAnnotation(
                        std::make_shared<ILVariableResolveResult>(
                            VariableHandleOf(v)));
                    skipInitStatement = new Syntax::ExpressionStatement(
                        new Syntax::InvocationExpression(
                            new Syntax::MemberReferenceExpression(
                                new Syntax::TypeReferenceExpression(unsafeType),
                                "SkipInit")));
                    static_cast<Syntax::InvocationExpression*>(
                        skipInitStatement->Expression())
                        ->Arguments()
                        .Add(outVarDecl);
                    insertionParent->InsertChildBefore(
                        insertionNode, skipInitStatement,
                        &Syntax::Slots::Statement);
                    insertedNode = skipInitStatement;
                } else {
                    insertionParent->InsertChildBefore(
                        insertionNode, vds, &Syntax::Slots::Statement);
                    insertedNode = vds;
                    // The C# `new DirectionExpression(FieldDirection.Out,
                    // new IdentifierExpression(v.Name).WithRR(
                    // new ILVariableResolveResult(ilVariable)))`.
                    skipInitStatement = new Syntax::ExpressionStatement(
                        new Syntax::InvocationExpression(
                            new Syntax::MemberReferenceExpression(
                                new Syntax::TypeReferenceExpression(unsafeType),
                                "SkipInit")));
                    static_cast<Syntax::InvocationExpression*>(
                        skipInitStatement->Expression())
                        ->Arguments()
                        .Add(new Syntax::DirectionExpression(
                            Syntax::FieldDirection::Out,
                            [handle = VariableHandleOf(v),
                             name = v.Name()]() {
                                auto* identifier =
                                    new Syntax::IdentifierExpression(name);
                                identifier->AddAnnotation(
                                    std::make_shared<ILVariableResolveResult>(
                                        handle));
                                return identifier;
                            }()));
                    insertionParent->InsertChildBefore(
                        insertionNode, skipInitStatement,
                        &Syntax::Slots::Statement);
                }
                context_->Step("Insert variable declaration", insertedNode);
            } else {
                insertionParent->InsertChildBefore(
                    insertionNode, vds, &Syntax::Slots::Statement);
            }
        }
    }
    // Perform the replacements at the end so that we don't replace a node
    // while it is still referenced by a VariableToDeclare.
    for (Replacement& replacement : replacements) {
        context_->Step(replacement.stepDescription, replacement.oldNode);
        Syntax::AstNode* newNode = replacement.createNewNode();
        replacement.oldNode->ReplaceWith(newNode);
    }
}

// The C# `void UpdateAnnotations(AstNode rootNode)` (lines 775-796): a
// variable removed by a name collision keeps reading through its
// replacement -- the identifiers' resolve results are re-targeted onto the
// surviving variable.
void DeclareVariables::UpdateAnnotations(Syntax::AstNode* rootNode) {
    for (Syntax::AstNode* node : rootNode->Descendants()) {
        IL::ILVariable* ilVar = nullptr;
        if (auto* id = dynamic_cast<Syntax::IdentifierExpression*>(node)) {
            ilVar = GetILVariable(*id);
        } else if (auto* vi = dynamic_cast<Syntax::VariableInitializer*>(node)) {
            ilVar = GetILVariable(*vi);
        } else {
            continue;
        }
        if (ilVar == nullptr || !VariableNeedsDeclaration(ilVar->Kind))
            continue;
        auto it = variableDict.find(ilVar);
        if (it == variableDict.end())
            throw std::out_of_range(
                "DeclareVariables::UpdateAnnotations: variable not analyzed");
        VariableToDeclare* v = it->second.get();
        if (!v->RemovedDueToCollision())
            continue;
        while (v->ReplacementDueToCollision != nullptr) {
            v = v->ReplacementDueToCollision;
        }
        node->RemoveAnnotations<Semantics::ResolveResult>();
        node->AddAnnotation(std::make_shared<ILVariableResolveResult>(
            VariableHandleOf(*v), v->ILVariable()->Type));
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms

