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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/DeclareVariables.cs (the analysis
// phase): computes where each undeclared ILAst variable would be declared by finding
// the smallest scope that contains all of its uses (the common parent of the insertion
// points), then resolves illegal name "collisions" (two variables of the same name
// whose declarations would land in the same or a nested block) by merging them into a
// single declaration. The result is exposed through `Analyze` /
// `GetDeclarationPoint` / `WasMerged` / `ClearAnalysisResults`, the surface
// `PatternStatementTransform` consumes (`declareVariables.Analyze(rootNode)` and
// `declareVariables.GetDeclarationPoint(v)`).
//
// The mutation phase (`Run`'s `EnsureExpressionStatementsAreValid`,
// `InsertDeconstructionVariableDeclarations`, `InsertVariableDeclarations`,
// `CanBeDeclaredAsOutVariable`, `UpdateAnnotations`, `CombineDeclarationAndInitializer`,
// `IsReferencedWithinDeclaringCall`) stays DEFERRED, each named at its would-be call
// site. `Run` therefore throws a loud std::logic_error rather than silently skipping the
// insertion; the transform is not wired into any seed path yet.

#pragma once

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {
class ILVariable;
class BlockContainer;
}

namespace ILSpy::Decompiler::CSharp::Syntax {
class AssignmentExpression;
class Expression;
class IdentifierExpression;
}

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class DeclareVariables : IAstTransform`. The nested
// `InsertionPoint` / `VariableToDeclare` are `public` in the port (the C# declares them
// private, but the port widens them for direct TDD -- the TypeSystemAstBuilder /
// TransformFieldAndConstructorInitializers precedent).
class DeclareVariables : public IAstTransform {
public:
    // The C# `enum VariableInitKind`.
    enum class VariableInitKind {
        None = 0,
        NeedsDefaultValue = 1,
        NeedsSkipInit = 2,
    };

    // The C# `struct InsertionPoint` -- a position immediately before `nextNode`
    // (either an `ExpressionStatement` in a `BlockStatement`, or an initializer in a
    // for-loop). `level` is `nextNode`'s nesting depth within the AST, used to speed
    // up `FindCommonParent`.
    struct InsertionPoint {
        int level = 0;
        Syntax::AstNode* nextNode = nullptr;

        // The C# `internal InsertionPoint Up()` -- go up one level.
        InsertionPoint Up() const;

        // The C# `internal InsertionPoint UpTo(int targetLevel)`.
        InsertionPoint UpTo(int targetLevel) const;
    };

    // The C# `class VariableToDeclare`.
    class VariableToDeclare {
    public:
        VariableToDeclare(IL::ILVariable* variable, InsertionPoint insertionPoint,
                          Syntax::IdentifierExpression* firstUse, int sourceOrder);

        // The C# `public readonly ILVariable ILVariable` field.
        IL::ILVariable* ILVariable() const { return ilVariable_; }

        // The C# `public string Name => ILVariable.Name` (the variable already
        // carries the name assigned by the IL pipeline).
        std::string Name() const;

        // The C# `public VariableInitKind DefaultInitialization`.
        VariableInitKind DefaultInitialization = VariableInitKind::None;

        // The C# `public int SourceOrder` -- the order the variable was first seen
        // (the lower value comes first in the source).
        int SourceOrder = 0;

        // The C# `public InsertionPoint InsertionPoint`. The member shares the
        // nested struct's name; the type is written with the enclosing-class
        // qualification so the member name does not hide the type at its own
        // declaration (the `Expression()`-shadows-`Expression` D237 precedent).
        DeclareVariables::InsertionPoint InsertionPoint;

        // The C# `public IdentifierExpression FirstUse`.
        Syntax::IdentifierExpression* FirstUse = nullptr;

        // The C# `public VariableToDeclare? ReplacementDueToCollision` plus the
        // `RemovedDueToCollision` / `InvolvedInCollision` / `DeclaredInDeconstruction`
        // flags.
        VariableToDeclare* ReplacementDueToCollision = nullptr;
        bool InvolvedInCollision = false;
        bool DeclaredInDeconstruction = false;

        bool RemovedDueToCollision() const { return ReplacementDueToCollision != nullptr; }

    private:
        IL::ILVariable* ilVariable_;
    };

    // The C# `void Run(AstNode rootNode, TransformContext context)` -- the mutation
    // phase, deferred (see the header comment). Throws std::logic_error.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `public void Analyze(AstNode rootNode)`: analyze the input AST (containing
    // undeclared variables) for where those variables would be declared. Analysis does
    // not modify the AST.
    void Analyze(Syntax::AstNode& rootNode);

    // The C# `public AstNode GetDeclarationPoint(ILVariable variable)` -- the position
    // where the declaration for the variable will be inserted (following collision
    // merges). Throws std::out_of_range for an unknown variable (the C#
    // KeyNotFoundException).
    Syntax::AstNode* GetDeclarationPoint(IL::ILVariable& variable);

    // The C# `public bool WasMerged(ILVariable variable)`.
    bool WasMerged(IL::ILVariable& variable);

    // The C# `public void ClearAnalysisResults()`.
    void ClearAnalysisResults();

    // The C# `internal static bool VariableNeedsDeclaration(VariableKind kind)`:
    // whether the variable's declaration is emitted by this transform (false for
    // parameters / exception / using / foreach / pattern locals, which the higher-level
    // constructs declare themselves).
    static bool VariableNeedsDeclaration(IL::VariableKind kind);

    // The C# `private static bool IsValidInStatementExpression(Expression expr)`:
    // whether the expression is legal as a statement-expression (its value may be
    // discarded). Widened to public for direct TDD.
    static bool IsValidInStatementExpression(Syntax::Expression& expr);

    // The C# private `VariableToDeclare? ResolveVariableToDeclare(ILVariable?)` (used
    // by the deferred mutation phase); exposed so the analysis tests can inspect the
    // post-collision variable.
    VariableToDeclare* ResolveVariableToDeclare(IL::ILVariable* variable);

private:
    // The C# `readonly Dictionary<ILVariable, VariableToDeclare> variableDict`. The
    // `unique_ptr` keeps the `VariableToDeclare*` addresses the collision links store
    // stable as the map grows.
    std::unordered_map<IL::ILVariable*, std::unique_ptr<VariableToDeclare>> variableDict;

    // The C# `Dictionary.Values` iteration order (the .NET Dictionary preserves
    // insertion order; `std::unordered_map` does not). `ResolveCollisions` walks the
    // variables in this order so the collision links match the C# exactly.
    std::vector<VariableToDeclare*> variableOrder;

    // The C# `List<(InsertionPoint, BlockContainer)> scopeTracking` -- the active loop
    // / function-body scopes along the current root-to-node path, rebuilt by each
    // `FindInsertionPoints` descent and empty once it returns.
    std::vector<std::pair<InsertionPoint, IL::BlockContainer*>> scopeTracking;

    // The C# `TransformContext context` (only non-null within `Run`; the analysis
    // entry points leave it null).
    bool isRunning_ = false;

    // The C# `void FindInsertionPoints(AstNode node, int nodeLevel)` and its
    // `IsRelevantScope` / `FindCommonParent` helpers.
    void FindInsertionPoints(Syntax::AstNode& node, int nodeLevel);
    static bool IsRelevantScope(IL::BlockContainer& scope);
    InsertionPoint FindCommonParent(InsertionPoint oldPoint, InsertionPoint newPoint);

    // The C# `void ResolveCollisions()`.
    void ResolveCollisions();

    // The C# `bool IsMatchingAssignment(VariableToDeclare v, out AssignmentExpression?)`.
    bool IsMatchingAssignment(VariableToDeclare& v, Syntax::AssignmentExpression*& assignment);
};

// The C# `v.DefaultInitialization |= prev.DefaultInitialization` flag combination.
inline DeclareVariables::VariableInitKind operator|(DeclareVariables::VariableInitKind a,
                                                    DeclareVariables::VariableInitKind b) {
    return static_cast<DeclareVariables::VariableInitKind>(static_cast<int>(a) | static_cast<int>(b));
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
