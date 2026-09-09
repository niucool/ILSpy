// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the StatementBuilder SKELETON (ICSharpCode.Decompiler/CSharp/
// StatementBuilder.cs -- the ILVisitor<TranslatedStatement> that translates ILAst
// blocks to C# statements): the ctor with its ExpressionBuilder construction (the
// C# builds the expression builder passing `this` -- the mutual reference), the
// translation entry family (Convert / ConvertAsBlock), the real C# `Default`
// fallback (an ExpressionStatement over the expression translation -- unlike the
// ExpressionBuilder's error-expression fallback, this one is the C#'s own
// behavior), and the per-instruction Visit arms the slices have landed (the leaf
// statement stores/nops and the isinst expression statement).
//
// The C# `ILVisitor<TranslatedStatement>` base is a double-dispatch: every
// ILInstruction overrides AcceptVisitor<T> to call the visitor's Visit<Instr>
// (this). The port's IL tree has no AcceptVisitor surface (the ExpressionBuilder
// convention): `Visit` is an OpCode switch dispatching to the per-instruction
// Visit methods, and `Default` is the C#'s own not-overridden fallback -- the
// ExpressionStatement over `exprBuilder.Translate(inst)`.
//
// The C# fields the ctor copies: `currentReturnContainer` (the body cast to
// BlockContainer -- the port's ILFunction::Body is statically a BlockContainer, so
// the C# InvalidCastException shape is unreachable), `currentIsIterator` (the C#
// `IsIterator` field -- the port's seed ILFunction copies it), and
// `currentResultType` (the C# `IsAsync ? AsyncReturnType! : ReturnType` -- the
// port's seed ILFunction carries both fields, null until the type-system plumbing
// populates them).
//
// Deferrals (each named at the member that needs it): the CancellationToken (the
// cooperative-cancel ThrowIfCancellationRequested in Convert is a no-op in the
// port, the DecompileRun convention), and the heavier Visit arms (the switch /
// branch / leave / try / lock / using / foreach / pinned-region / block-container
// arms) -- an instruction whose C# Visit method has not been ported yet degrades
// to the Default expression statement instead of crashing.

#pragma once

#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/TranslatedStatement.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <memory>

// The forward declarations at GLOBAL scope (the nested-namespace trap: a declaration
// written inside namespace CSharp would create CSharp::IL and shadow the real
// ILSpy::Decompiler::IL).
namespace ILSpy::Decompiler::IL {
class IsInst;
class StLoc;
class StObj;
class Nop;
class IfInstruction;
}  // namespace ILSpy::Decompiler::IL

namespace ILSpy::Decompiler::CSharp {

// The REAL type-system namespace alias: the CSharp/TypeSystem sub-namespace
// (CSharpTypeResolveContext/UsingScope) shadows the plain `TypeSystem::` lookup
// inside this namespace, so every type-system reference goes through the
// fully-qualified alias (the ExpressionBuilder TS:: convention).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The ExpressionBuilder skeleton. The C# `sealed class StatementBuilder :
// ILVisitor<TranslatedStatement>`; the port models the visitor as the OpCode-switch
// `Visit` (the ExpressionBuilder convention) with one `VisitXxx` member per ported
// instruction kind.
class StatementBuilder {
public:
    // The C# `public StatementBuilder(IDecompilerTypeSystem typeSystem,
    // ITypeResolveContext decompilationContext, ILFunction currentFunction,
    // DecompilerSettings settings, DecompileRun decompileRun, CancellationToken
    // cancellationToken)`: builds the ExpressionBuilder over `this`, casts the
    // function body to the currentReturnContainer, and copies the
    // iterator/async-result state. The C# `Debug.Assert(typeSystem != null &&
    // decompilationContext != null)` maps to the port's invalid_argument guard for
    // the pointer parameters (the D424 convention -- the reference parameters carry
    // their non-nullness in their type); the IDecompilerTypeSystem surface ports as
    // the ICompilation narrowing (the ExpressionBuilder convention); the
    // CancellationToken is the documented deferral.
    // The out-of-line destructor: the unique_ptr<ExpressionBuilder> member over a
    // forward-declared-in-some-TUs complete type would instantiate the default_delete
    // in every consuming TU (the XamlContext convention).
    ~StatementBuilder();
    // The defaulted move (the destructor suppresses the implicit one; the fixture
    // factory returns by value).
    StatementBuilder(StatementBuilder&&) = default;

    explicit StatementBuilder(const TS::ICompilation& typeSystem,
                              const TS::ITypeResolveContext& decompilationContext,
                              IL::ILFunction* currentFunction,
                              const DecompilerSettings* settings,
                              const DecompileRun* decompileRun);

    // -- The translation entry family -------------------------------------------------

    // The C# `public Statement Convert(ILInstruction inst)`: the visitor dispatch
    // (the C# `inst.AcceptVisitor(this)` double dispatch through the port's OpCode
    // switch; the C# ThrowIfCancellationRequested is the documented no-op
    // deferral). The returned TranslatedStatement converts to its Statement
    // through the C# implicit operator.
    Syntax::Statement* Convert(IL::ILInstruction* inst);

    // The C# `public BlockStatement ConvertAsBlock(ILInstruction inst)`: re-attaches
    // the IL-instruction annotation and wraps the converted statement in a
    // BlockStatement unless it already is one (the C# `stmt as BlockStatement ??
    // new BlockStatement { stmt }`).
    Syntax::BlockStatement* ConvertAsBlock(IL::ILInstruction* inst);

    // The C# `internal readonly ExpressionBuilder exprBuilder` -- the expression
    // builder this statement builder owns (the C# GC roots it through the field;
    // the mutual reference back is the non-owning statementBuilder pointer).
    std::unique_ptr<ExpressionBuilder> exprBuilder;

    // -- The C# `internal` state fields ----------------------------------------------

    // The C# `readonly ILFunction currentFunction`.
    IL::ILFunction* currentFunction = nullptr;
    // The C# `internal BlockContainer currentReturnContainer` -- the body cast to
    // BlockContainer (the port's Body is statically a BlockContainer, so the C#
    // InvalidCastException shape is unreachable).
    IL::BlockContainer* currentReturnContainer = nullptr;
    // The C# `internal IType currentResultType` -- the async return type when the
    // function is async, the function's own return type otherwise (the C#
    // `currentFunction.IsAsync ? AsyncReturnType! : ReturnType`).
    const TS::IType* currentResultType = nullptr;
    // The C# `internal bool currentIsIterator` -- the YieldReturnDecompiler's flag.
    bool currentIsIterator = false;
    // The C# `internal bool EmitAsRefReadOnly` -- the CallBuilder's
    // EnforceExplicitIn writes it; the StatementBuilder's own consumption
    // (the as-readonly emission) is the StatementBuilder slice that owns it.
    bool EmitAsRefReadOnly = false;

private:
    // The C# `readonly IDecompilerTypeSystem typeSystem` / `DecompilerSettings
    // settings` / `internal readonly DecompileRun decompileRun` fields.
    const TS::ICompilation* typeSystem = nullptr;
    const DecompilerSettings* settings = nullptr;
    const DecompileRun* decompileRun = nullptr;

    // -- The visitor dispatch (the C# ILVisitor<TranslatedStatement> base) -----------

    // The port's stand-in for the C# AcceptVisitor double dispatch: the OpCode
    // switch over the landed Visit arms, falling back to Default (the C#'s own
    // fallback -- an ExpressionStatement over the expression translation).
    TranslatedStatement Visit(IL::ILInstruction* inst);

    // The C# `protected override TranslatedStatement Default(ILInstruction inst)`.
    TranslatedStatement Default(IL::ILInstruction* inst);

    // The C# `protected internal override TranslatedStatement VisitIsInst(IsInst
    // inst)`: the unused-result `is` test over the boxing-unwrapped argument.
    TranslatedStatement VisitIsInst(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitStLoc(StLoc
    // inst)` / VisitStObj sibling: the statement-level store renders the assignment
    // and strips the top-level ref on ref re-assignment.
    TranslatedStatement VisitStLoc(IL::ILInstruction* inst);
    TranslatedStatement VisitStObj(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement VisitNop(Nop inst)`:
    // the empty statement with the nop's comment as trailing trivia.
    TranslatedStatement VisitNop(IL::ILInstruction* inst);
    // The C# `protected internal override TranslatedStatement
    // VisitIfInstruction(IfInstruction inst)`: the if/else statement over the
    // translated condition (a false arm that is a Nop is the C#'s no-else shape).
    TranslatedStatement VisitIfInstruction(IL::ILInstruction* inst);
};

}  // namespace ILSpy::Decompiler::CSharp
