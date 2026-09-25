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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/DeclareVariables.cs -- the
// analysis half: where each variable's declaration would be inserted. The
// insertion point for a variable is the common parent of all its uses (the
// smallest scope containing them), computed by FindInsertionPoints over the
// resolve-result annotations and then adjusted by ResolveCollisions so that
// same-named variables whose scopes cannot legally coexist merge into one
// declaration. The consumers: PatternStatementTransform.Run calls Analyze up
// front (the C# does the same), and TransformFor's iterator bail reads
// GetDeclarationPoint.
//
// The mutation half is ported in Run: EnsureExpressionStatementsAreValid
// (the direction-expression unwrap and the discard assignment), and
// InsertVariableDeclarations (the combine-declaration-and-initializer arm,
// the out-var arm, and the separate-declaration arm) with UpdateAnnotations
// for the collision-replaced variables. DEFERRED loudly within it:
// InsertDeconstructionVariableDeclarations (needs
// StatementBuilder.TranslateDeconstructionDesignation), the temporary arm of
// EnsureExpressionStatementsAreValid (needs
// AssignVariableNames.GenerateVariableName), the NeedsSkipInit forms (need a
// live context.TypeSystem for FindType(KnownTypeCode.Unsafe)), the
// anonymous-type `var` decision (needs the NRExtensions
// ContainsAnonymousType/IsAnonymousType walk), and the IsRefReadOnly
// readonly-specifier fixup (the ILVariable flag is not ported).
//
// The C# `Dictionary<ILVariable, VariableToDeclare>` enumeration order (the
// insertion order the collision resolution and the insertion loop rely on)
// ports as an insertion-ordered vector plus a reference-identity index.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"
#include "Decompiler/IL/ILVariable.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Sibling-namespace forward declarations at global scope (the fwd-decl
// placement rule: a nested `namespace TypeSystem { ... }` inside the CSharp
// namespace would shadow the sibling for every later qualified reference in
// this file).
namespace ILSpy::Decompiler::TypeSystem {
class IType;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp::Syntax {
class AstNode;
class AssignmentExpression;
class DirectionExpression;
class Expression;
class IdentifierExpression;
} // namespace ILSpy::Decompiler::CSharp::Syntax

namespace ILSpy::Decompiler::IL {
class BlockContainer;
} // namespace ILSpy::Decompiler::IL

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class DeclareVariables : IAstTransform`.
class DeclareVariables final : public IAstTransform {
public:
    // The C# `struct InsertionPoint` (the DebuggerDisplay'd position
    // immediately before nextNode; nextNode is either an ExpressionStatement
    // in a BlockStatement, or an initializer in a for-loop).
    struct InsertionPoint {
        // The nesting level of `nextNode` within the AST; used to speed up
        // FindCommonParent().
        int level = 0;
        Syntax::AstNode* nextNode = nullptr;

        InsertionPoint() = default;
        InsertionPoint(int level, Syntax::AstNode* nextNode)
            : level(level), nextNode(nextNode) {}

        // The C# `internal InsertionPoint Up()`: go up one level (insertion
        // points live inside a method body, so walking up always finds a
        // parent -- the port keeps that invariant as the assert).
        InsertionPoint Up() const;

        // The C# `internal InsertionPoint UpTo(int targetLevel)`.
        InsertionPoint UpTo(int targetLevel) const;
    };

    // The C# `enum VariableInitKind`.
    enum class VariableInitKind {
        None,
        NeedsDefaultValue,
        NeedsSkipInit
    };

    // The C# `class VariableToDeclare`.
    class VariableToDeclare {
    public:
        VariableToDeclare(const IL::ILVariablePtr& variable,
                          InsertionPoint insertionPoint,
                          Syntax::IdentifierExpression* firstUse, int sourceOrder);

        // The C# `public readonly ILVariable ILVariable` (a GC reference; the
        // port holds a NON-OWNING shared-handle alias -- the no-op-deleter
        // convention -- because the variable is owned by the IL function
        // tree, and the annotation constructions copy the alias without
        // taking ownership).
        IL::ILVariable* ILVariable() const { return ilVariable_.get(); }
        const IL::ILVariablePtr& ILVariableHandle() const { return ilVariable_; }
        // The C# `IType Type => ILVariable.Type`.
        const ::ILSpy::Decompiler::TypeSystem::IType* Type() const {
            return ilVariable_->Type.get();
        }
        // The C# `string Name => ILVariable.Name!` (variables reaching this
        // transform have already been assigned a name).
        const std::string& Name() const { return ilVariable_->Name; }

        // Whether the variable needs to be default-initialized.
        VariableInitKind DefaultInitialization = VariableInitKind::None;

        // The insertion point, i.e. the node before which the variable
        // declaration should be inserted.
        InsertionPoint insertionPoint;

        // The first use of the variable.
        Syntax::IdentifierExpression* firstUse = nullptr;

        // The value used to compare VariableToDeclare instances to determine
        // which variable was used first in the source code.
        int sourceOrder = 0;

        VariableToDeclare* replacementDueToCollision = nullptr;
        bool involvedInCollision = false;
        // The C# `bool RemovedDueToCollision => ReplacementDueToCollision != null`.
        bool RemovedDueToCollision() const {
            return replacementDueToCollision != nullptr;
        }
        bool declaredInDeconstruction = false;

    private:
        IL::ILVariablePtr ilVariable_;
    };

    // The C# `internal static bool VariableNeedsDeclaration(VariableKind kind)`:
    // the kinds that already carry their own declaration (or are handled by
    // the construct that introduced them) are not declared by this transform.
    static bool VariableNeedsDeclaration(IL::VariableKind kind);

    // The C# `void Run(AstNode rootNode, TransformContext context)` -- the
    // IAstTransform entry: the statement fixup, the analysis, and the
    // declaration insertion.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `public void Analyze(AstNode rootNode)`: analyze the input AST
    // (containing undeclared variables) for where those variables would be
    // declared by this transform. Analysis does not modify the AST.
    void Analyze(Syntax::AstNode& rootNode);

    // The C# `public void ClearAnalysisResults()`.
    void ClearAnalysisResults();

    // The C# `public AstNode GetDeclarationPoint(ILVariable variable)`: the
    // node before which the declaration for the variable will be inserted
    // (following merges performed by ResolveCollisions). Throws
    // std::out_of_range for a variable the analysis has not seen (the C#
    // KeyNotFoundException).
    Syntax::AstNode* GetDeclarationPoint(IL::ILVariable* variable);

    // The C# `public bool WasMerged(ILVariable variable)`: whether the
    // variable was merged with other variables.
    bool WasMerged(IL::ILVariable* variable);

private:
    // The C# `[AllowNull] TransformContext context` (the reentrancy guard's
    // state; null between runs).
    TransformContext* context_ = nullptr;

    // The C# `readonly Dictionary<ILVariable, VariableToDeclare>
    // variableDict`: the insertion-ordered list (the C# Dictionary's
    // enumeration order) with a reference-identity index over the stable
    // element addresses (the collision replacements point across entries).
    std::vector<std::unique_ptr<VariableToDeclare>> variables_;
    std::unordered_map<IL::ILVariable*, VariableToDeclare*> variableDict_;

    // The C# `List<(InsertionPoint InsertionPoint, BlockContainer Scope)>
    // scopeTracking`.
    struct ScopeTrackingEntry {
        InsertionPoint insertionPoint;
        IL::BlockContainer* scope;
    };
    std::vector<ScopeTrackingEntry> scopeTracking_;

    // The C# `void FindInsertionPoints(AstNode node, int nodeLevel)`.
    void FindInsertionPoints(Syntax::AstNode* node, int nodeLevel);

    // The C# `void FindInsertionPointForVariable(ILVariable variable)` -- the
    // local function inside FindInsertionPoints (it captures the first-use
    // identifier expression and the node level; the port passes them).
    void FindInsertionPointForVariable(IL::ILVariable* variable,
                                       Syntax::IdentifierExpression* identExpr,
                                       int nodeLevel);

    // The C# `private static bool IsRelevantScope(BlockContainer scope)`.
    static bool IsRelevantScope(IL::BlockContainer* scope);

    // The C# `InsertionPoint FindCommonParent(InsertionPoint, InsertionPoint)`.
    InsertionPoint FindCommonParent(InsertionPoint oldPoint,
                                    InsertionPoint newPoint) const;

    // The C# `void ResolveCollisions()`.
    void ResolveCollisions();

    // The C# `void EnsureExpressionStatementsAreValid(AstNode rootNode)`.
    void EnsureExpressionStatementsAreValid(Syntax::AstNode* rootNode);

    // The C# `private static bool IsValidInStatementExpression(Expression
    // expr)`.
    static bool IsValidInStatementExpression(Syntax::Expression* expr);

    // The C# `bool CombineDeclarationAndInitializer(VariableToDeclare v,
    // TransformContext context)`.
    bool CombineDeclarationAndInitializer(VariableToDeclare& v);

    // The C# `void InsertVariableDeclarations(TransformContext context)`.
    void InsertVariableDeclarations();

    // The C# `bool CanBeDeclaredAsOutVariable(VariableToDeclare v, out
    // DirectionExpression? dirExpr)`.
    bool CanBeDeclaredAsOutVariable(VariableToDeclare& v,
                                    Syntax::DirectionExpression** dirExpr);

    // The C# `bool IsReferencedWithinDeclaringCall(DirectionExpression
    // dirExpr, VariableToDeclare v)`.
    bool IsReferencedWithinDeclaringCall(Syntax::DirectionExpression* dirExpr,
                                         VariableToDeclare& v);

    // The C# `VariableToDeclare? ResolveVariableToDeclare(ILVariable?
    // variable)`.
    VariableToDeclare* ResolveVariableToDeclare(IL::ILVariable* variable);

    // The C# `void UpdateAnnotations(AstNode rootNode)`.
    void UpdateAnnotations(Syntax::AstNode* rootNode);

    // The C# `bool IsMatchingAssignment(VariableToDeclare v, out
    // AssignmentExpression? assignment)`: whether the insertion point's node
    // is `v = ...` (the declaration that can combine with an initializer).
    bool IsMatchingAssignment(VariableToDeclare& v,
                              Syntax::AssignmentExpression** assignment = nullptr) const;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
