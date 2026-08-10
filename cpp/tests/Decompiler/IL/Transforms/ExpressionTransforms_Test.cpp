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
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for ExpressionTransforms (the second per-statement child of the
// StatementTransform, the next in-order Phase 4 target after the D80
// StatementTransform orchestration). The C# ExpressionTransforms is an
// ILVisitor IStatementTransform folding simple expression patterns; this
// iteration ports the self-contained VisitComp subset -- `logic.not(comp op)`
// -> `comp(op.Negate)`, `comp(x != 0)` -> `x`, and `comp.unsigned(left > 0)` /
// `<= 0` -> `comp(left != 0)` / `== 0`. The tests run the transform via the
// StatementTransform driver (the real path, exercising the if-final-only
// block-model compensation) on hand-built blocks; the mscorlib sweep pins the
// global contract (the invariant holds across the corpus).

#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/Transforms/ExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/StObjToStLoc.hpp"
#include "Decompiler/IL/Transforms/InlineReturnTransform.hpp"
#include "Decompiler/IL/Transforms/RemoveInfeasiblePathTransform.hpp"
#include "Decompiler/IL/Transforms/DetectCatchWhenConditionBlocks.hpp"
#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/Transforms/RemoveDeadVariableInit.hpp"
#include "Decompiler/IL/Transforms/SwitchOnNullableTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/Transforms/PatternMatchingTransform.hpp"
#include "Decompiler/IL/Transforms/LockTransform.hpp"
#include "Decompiler/IL/Transforms/UsingTransform.hpp"
#include "Decompiler/IL/Transforms/CachedDelegateInitialization.hpp"
#include "Decompiler/IL/Transforms/CachedReadOnlySpanInitialization.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/BitNot.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/ThreeValuedBoolInstructions.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/ControlFlow/LoopDetection.hpp"
#include "Decompiler/IL/ControlFlow/ConditionDetection.hpp"
#include "Decompiler/IL/ControlFlow/DetectPinnedRegions.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::Metadata::MetadataFile;

namespace {

ILVariablePtr MakeLocal(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, std::move(type), 0);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeParam(std::string name, ITypePtr type = nullptr) {
    auto v = std::make_shared<ILVariable>(VariableKind::Parameter, std::move(type), 1);
    v->Name = std::move(name);
    return v;
}

// A delegate declaring type: a SimpleType with Kind == Delegate (the shape the
// type-kind derivation produces for an in-module delegate constructor, and the
// shape TransformDelegateCtorLdVirtFtnToLdVirtDelegate requires -- the C# checks
// `Method.DeclaringType.Kind != TypeKind.Delegate`).
ITypePtr MakeDelegateType(std::string ns, std::string name) {
    return std::make_shared<SimpleType>(TopLevelTypeName(std::move(ns), std::move(name)),
                                        TypeKind::Delegate);
}

// Build a `newobj DelegateType(target, ldvirtftn method)` virtual delegate
// construction (the shape TransformDelegateCtorLdVirtFtnToLdVirtDelegate folds
// into an LdVirtDelegate). The C# `new DelegateType(target, ldvirtftn M(target))`.
std::unique_ptr<Call> MakeNewObjVirtDelegate(ITypePtr delegateType,
                                            std::unique_ptr<ILInstruction> target,
                                            std::string ldvirtftnMethod) {
    auto call = std::make_unique<Call>(
        delegateType ? delegateType->ReflectionName() + "::.ctor" : std::string("::.ctor"));
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = std::move(delegateType);
    call->AddArg(std::move(target));
    call->AddArg(std::make_unique<LdVirtFtn>(std::move(ldvirtftnMethod)));
    return call;
}

// A single-block function whose body block is empty (a Leave final); the test
// adds non-terminal statements via the returned block pointer.
std::unique_ptr<ILFunction> MakeFnWithBlock(std::vector<ILVariablePtr> vars = {}) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto block = std::make_unique<Block>();
    block->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(block));
    for (auto& v : vars) fn->Variables.push_back(v);
    return fn;
}

// A two-block function: P with an if-final (no non-terminal statements), Q with
// a Leave. Models the if-final-only block the driver compensation targets.
std::unique_ptr<ILFunction> MakeIfFinalOnlyFn(std::unique_ptr<ILInstruction> condition,
                                              std::vector<ILVariablePtr> vars = {}) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = std::make_unique<Block>();
    auto iff = std::make_unique<IfInstruction>(std::move(condition),
                                              std::make_unique<Branch>(nullptr));
    P->SetFinal(std::move(iff));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(P));
    fn->Body->AddBlock(std::move(Q));
    for (auto& v : vars) fn->Variables.push_back(v);
    return fn;
}

void RunExpressionTransforms(ILFunction& fn) {
    StatementTransform st;
    st.AddChild(std::make_unique<ExpressionTransforms>());
    ILTransformContext ctx;
    st.Run(fn, ctx);
}

void RunPrePipeline(ILFunction& fn, ILTransformContext& ctx) {
    ControlFlowSimplification().Run(fn, ctx);
    StObjToStLoc().Run(fn, ctx);
    ILInlining().Run(fn, ctx);
    InlineReturnTransform().Run(fn, ctx);
    RemoveInfeasiblePathTransform().Run(fn, ctx);
    DetectPinnedRegions().Run(fn, ctx);
    DetectCatchWhenConditionBlocks().Run(fn, ctx);
    LdLocaDupInitObjTransform().Run(fn, ctx);
    EarlyExpressionTransforms().Run(fn, ctx);
    RemoveDeadVariableInit().Run(fn, ctx);
    ControlFlowSimplification().Run(fn, ctx);
    SwitchDetection().Run(fn, ctx);
    SwitchOnNullableTransform().Run(fn, ctx);
    LoopDetection().Run(fn, ctx);
    PatternMatchingTransform().Run(fn, ctx);
    ConditionDetection().Run(fn, ctx);
    LockTransform().Run(fn, ctx);
    UsingTransform().Run(fn, ctx);
    CachedDelegateInitialization().Run(fn, ctx);
    CachedReadOnlySpanInitialization().Run(fn, ctx);
}

// Count StLoc-wrapping-IfInstruction occurrences (the conditional-operator form
// HandleConditionalOperator produces: `stloc V(if (...) V2 else V1))`). The
// ternary fold is monotone non-decreasing across ExpressionTransforms (each fold
// creates one; nothing in this subset removes them). Used by the sweep to confirm
// the transform makes corpus progress.
int CountConditionalOperators(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::StLoc) {
            auto* st = static_cast<StLoc*>(inst);
            if (st->Value && st->Value->Op == OpCode::IfInstruction) ++n;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count Box ILAst nodes in the tree. The VisitBox fold (`box ref-type(arg)` ->
// `arg`) is monotone non-increasing (each fold removes a Box; nothing in this
// subset creates one). Used by the sweep to confirm the transform does not
// regress and to measure corpus progress.
int CountBoxes(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Box) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Build a block whose FinalInstruction is an IfInstruction with the given
// condition and two Block arms (each a single StLoc to `v` of the given values,
// no FinalInstruction -- the expression-block shape after ConditionDetection's
// TryDropCommonExit). The arms' FinalInstruction is left null (an expression
// block); `withFinal=true` instead gives each arm a Branch final (a control-flow
// block, which HandleConditionalOperator must reject).
std::unique_ptr<Block> MakeTernaryBlock(ILVariablePtr v,
                                        std::unique_ptr<ILInstruction> cond,
                                        std::unique_ptr<ILInstruction> value1,
                                        std::unique_ptr<ILInstruction> value2,
                                        bool withFinal = false) {
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v, std::move(value1)));
    if (withFinal) trueBlock->SetFinal(std::make_unique<Branch>(nullptr));
    auto falseBlock = std::make_unique<Block>();
    falseBlock->Add(std::make_unique<StLoc>(v, std::move(value2)));
    if (withFinal) falseBlock->SetFinal(std::make_unique<Branch>(nullptr));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock),
                                               std::move(falseBlock));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    return P;
}

// Build a MatchInstruction `expr is T x` (CheckType + CheckNotNull, a type
// pattern with a designator) over `testedOperand`, storing into `v`. This is the
// shape PatternMatchValueTypes/PatternMatchRefTypes produce.
std::unique_ptr<MatchInstruction> MakeMatch(ILVariablePtr v,
                                             std::unique_ptr<ILInstruction> testedOperand) {
    auto m = std::make_unique<MatchInstruction>(v, std::move(testedOperand));
    m->CheckType = true;
    m->CheckNotNull = true;
    return m;
}

// Nullable<T> as a generic instantiation: ParameterizedType(KnownType(NullableOfT), {T}).
// Mirrors the NullableLiftingTransform_Test helper -- the 2-arg GetValueOrDefault
// fold's declaring type must resolve to KnownTypeCode::NullableOfT (the
// KnownTypeCodeOf helper unwraps the ParameterizedType to its generic definition).
ITypePtr MakeNullableOf(KnownTypeCode underlying) {
    std::vector<ITypePtr> args;
    args.push_back(std::make_shared<KnownType>(underlying));
    return std::make_shared<ParameterizedType>(
        std::make_shared<KnownType>(KnownTypeCode::NullableOfT), std::move(args));
}

// `call Nullable<bool>::GetValueOrDefault(ldloca v)` -- the 1-arg form on a
// Nullable<bool> variable. The condition the RunIfNullableLift `&`/`|` paths and
// the bool? equality fold consume.
std::unique_ptr<Call> MakeGVOCall(const ILVariablePtr& v) {
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    call->AddArg(std::make_unique<LdLoca>(v));
    return call;
}

// `call Nullable<bool>::get_HasValue(ldloca v)` -- the HasValue accessor on a
// Nullable<bool> variable.
std::unique_ptr<Call> MakeHasValueCall(const ILVariablePtr& v) {
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    call->AddArg(std::make_unique<LdLoca>(v));
    return call;
}

// Parameterized Nullable<T> accessor helpers (for Nullable<int> etc., used by the
// Run(BinaryNumericInstruction) equality-lift tests where the value comparison
// is on a non-bool underlying type).
std::unique_ptr<Call> MakeGVOCallOf(const ILVariablePtr& v, KnownTypeCode underlying) {
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(underlying);
    call->AddArg(std::make_unique<LdLoca>(v));
    return call;
}

std::unique_ptr<Call> MakeHasValueCallOf(const ILVariablePtr& v, KnownTypeCode underlying) {
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(underlying);
    call->AddArg(std::make_unique<LdLoca>(v));
    return call;
}

// logic.not(inner) in this port's `comp(Equality, inner, ldc.i4 0)` shape (the
// reader's brfalse, per the SwitchAnalysis/ConditionDetection convention). Used
// to build the `!v.HasValue` / `!v2.GetValueOrDefault()` arms of the two-nullable
// three-valued logic condition pattern.
std::unique_ptr<Comp> MakeLogicNot(std::unique_ptr<ILInstruction> inner) {
    return std::make_unique<Comp>(std::move(inner), std::make_unique<LdcI4>(0),
                                 ComparisonKind::Equality);
}

// `newobj Nullable<bool>(ldc.i4 value)` -- the Nullable<bool> constructor the
// `&`/`|` on bool? `(bool?)false` / `(bool?)true` arms use. MatchNullableCtor
// recognises it (IsNewObj + NullableOfT declaring type + 1 arg).
std::unique_ptr<Call> MakeNullableBoolCtor(int value) {
    auto call = std::make_unique<Call>("System.Nullable`1::.ctor");
    call->IsNewObj = true;
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    call->AddArg(std::make_unique<LdcI4>(value));
    return call;
}

// Count ThreeValuedBoolAnd/Or nodes in the tree. The RunIfNullableLift `&`/`|`
// on bool? fold is monotone non-decreasing for this count (each fold creates one;
// nothing in this subset removes one). Used by the sweep to confirm the
// transform does not regress.
int CountThreeValuedBool(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::ThreeValuedBoolAnd ||
            inst->Op == OpCode::ThreeValuedBoolOr) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count TryCatchHandlers whose catch Variable has been promoted to
// VariableKind::ExceptionLocal (the TransformCatchVariable fold's effect: the
// catch-local copy `stloc v(ldloc E)` is promoted so v becomes the catch
// variable). The fold is monotone non-decreasing for this count (each fold
// promotes one handler's variable; nothing in this subset demotes one). Used by
// the sweep to confirm the transform fires on the corpus.
int CountExceptionLocalHandlers(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::TryCatchHandler) {
            auto* h = static_cast<TryCatchHandler*>(inst);
            if (h->Variable && h->Variable->Kind == VariableKind::ExceptionLocal) ++n;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count LdcDecimal nodes (the TransformDecimalFieldToConstant fold's output).
int CountLdcDecimal(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::LdcDecimal) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count `ldobj(ldsflda System.Decimal::One/Zero/MinusOne)` static-field loads --
// the TransformDecimalFieldToConstant fold's input. Each fold removes one and
// produces one LdcDecimal, so the count is monotone non-increasing across
// ExpressionTransforms.
int CountDecimalConstantFieldLoads(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::LdObj) {
            auto* ldobj = static_cast<LdObj*>(inst);
            auto* addr = ldobj->Target ? dynamic_cast<LdsFlda*>(ldobj->Target.get()) : nullptr;
            if (addr) {
                const std::string& nm = addr->FieldName;
                if (nm == "System.Decimal::One" || nm == "System.Decimal::Zero" ||
                    nm == "System.Decimal::MinusOne") {
                    ++n;
                }
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count LdVirtDelegate ILAst nodes in the tree. The
// TransformDelegateCtorLdVirtFtnToLdVirtDelegate fold (`newobj Delegate(target,
// ldvirtftn M(target))` -> `ldvirtdelegate Delegate M(target)`) is monotone
// non-decreasing (each fold creates one LdVirtDelegate; nothing in this subset
// removes one). Used by the sweep to confirm the fold does not regress.
int CountLdVirtDelegate(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::LdVirtDelegate) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count `newobj Delegate(target, ldvirtftn M)` virtual delegate constructions --
// the TransformDelegateCtorLdVirtFtnToLdVirtDelegate fold's input. A newobj is
// modelled as a Call with IsNewObj; the fold's input is such a Call whose 2nd
// arg is an LdVirtFtn. Each fold removes one and produces one LdVirtDelegate, so
// the count is monotone non-increasing across ExpressionTransforms.
int CountNewObjVirtDelegate(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Call) {
            auto* call = static_cast<Call*>(inst);
            if (call->IsNewObj && call->Arguments.size() == 2 &&
                call->Arguments[1] && call->Arguments[1]->Op == OpCode::LdVirtFtn) {
                ++n;
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// A try/catch fixture for TransformCatchVariable: the catch handler's Variable
// is an ExceptionStackSlot `ex` (the E_<offset> slot the runtime pushes the
// caught exception into), and the catch entry block starts with the csc-emitted
// copy `stloc v(ldloc ex)` (the local v receives the caught exception, the body
// uses v). `catchType` is the catch variable's type; `vUses` is the number of
// `stloc result(ldloc v)` uses appended to the catch entry (so v is live). When
// `unboxAny` is set, the copy is `stloc v(unbox.any T(ldloc ex))` (the type-
// parameter catch shape) with the unbox.any's Type = catchType. When
// `escapedUse` is set, an extra `ldloc v` is planted in the TRY body (outside the
// catch handler) so a use of v escapes the catch (the fold must reject).
struct CatchFixture {
    std::unique_ptr<ILFunction> fn;
    TryCatchHandler* handler = nullptr;
    Block* catchEntry = nullptr;
    ILVariablePtr ex;    // ExceptionStackSlot
    ILVariablePtr v;      // Local (the copy target)
    ILVariablePtr result;
};

CatchFixture BuildCatch(ITypePtr catchType, int vUses = 1, bool unboxAny = false,
                        bool escapedUse = false,
                        VariableKind vKind = VariableKind::Local) {
    CatchFixture fx;
    fx.fn = std::make_unique<ILFunction>();
    fx.fn->Body = std::make_unique<BlockContainer>();
    fx.fn->Body->Parent = fx.fn.get();
    fx.fn->Body->ChildIndex = 0;

    fx.ex = std::make_shared<ILVariable>(VariableKind::ExceptionStackSlot, catchType, 100);
    fx.ex->Name = "E_100";
    fx.ex->HasGeneratedName = true;
    fx.v = std::make_shared<ILVariable>(vKind, catchType, 0);
    fx.v->Name = "V_0";
    fx.result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Int32));
    fx.fn->Variables.push_back(fx.ex);
    fx.fn->Variables.push_back(fx.v);
    fx.fn->Variables.push_back(fx.result);

    // try body: a single empty block (plus an escaped `ldloc v` use if requested).
    auto tryBody = std::make_unique<BlockContainer>();
    tryBody->AddBlock(std::make_unique<Block>());
    if (escapedUse)
        tryBody->Blocks[0]->Add(std::make_unique<StLoc>(fx.result, std::make_unique<LdLoc>(fx.v)));
    tryBody->Blocks[0]->SetFinal(std::make_unique<Leave>(tryBody.get()));

    // catch body: entry block with the copy + uses of v + leave.
    auto catchBody = std::make_unique<BlockContainer>();
    catchBody->AddBlock(std::make_unique<Block>());
    fx.catchEntry = catchBody->Blocks[0].get();
    std::unique_ptr<ILInstruction> slotLoad = std::make_unique<LdLoc>(fx.ex);
    if (unboxAny)
        slotLoad = std::make_unique<UnboxAny>(catchType, std::move(slotLoad));
    fx.catchEntry->Add(std::make_unique<StLoc>(fx.v, std::move(slotLoad)));
    for (int i = 0; i < vUses; ++i)
        fx.catchEntry->Add(std::make_unique<StLoc>(fx.result, std::make_unique<LdLoc>(fx.v)));
    fx.catchEntry->SetFinal(std::make_unique<Leave>(catchBody.get()));

    auto handler = std::make_unique<TryCatchHandler>(
        std::make_unique<LdcI4>(1),  // constant-true filter (plain catch)
        std::move(catchBody), fx.ex);
    fx.handler = handler.get();
    auto tryCatch = std::make_unique<TryCatch>(std::move(tryBody));
    tryCatch->AddHandler(std::move(handler));

    auto main = std::make_unique<Block>();
    main->Add(std::move(tryCatch));
    main->SetFinal(std::make_unique<Leave>(fx.fn->Body.get()));
    fx.fn->Body->AddBlock(std::move(main));

    ComputeVariableUsage(*fx.fn);
    RecomputeIncomingEdgeCounts(*fx.fn);
    return fx;
}

// Run only ExpressionTransforms via the StatementTransform driver (the real
// path, exercising the per-statement Visit dispatch that reaches
// VisitTryCatchHandler by recursing into the TryInstruction).
void RunExpressionTransformsOnly(ILFunction& fn) {
    StatementTransform st;
    st.AddChild(std::make_unique<ExpressionTransforms>());
    ILTransformContext ctx;
    st.Run(fn, ctx);
}

} // namespace

// logic.not(comp op) -> comp(op.Negate): comp(eq, comp(eq, a, b), 0) folds to
// comp(ne, a, b) (the `!(a == b)` -> `a != b` push).
TEST(ExpressionTransforms, LogicNotPushesNegationIntoComparison) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    auto inner = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b), ComparisonKind::Equality, false);
    auto outer = std::make_unique<Comp>(std::move(inner), std::make_unique<LdcI4>(0),
                                         ComparisonKind::Equality, false);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Inequality)
        << "!(a == b) must fold to a != b";
    ASSERT_EQ(c->Left->Op, OpCode::LdLoc);
    ASSERT_EQ(c->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Left.get())->Variable.get(), a.get());
    EXPECT_EQ(static_cast<LdLoc*>(c->Right.get())->Variable.get(), b.get());
}

// comp(x != 0) -> x when the comp is in an if condition: `if (comp(x != 0))`
// folds to `if (x)`. The comp is the if-final's condition (an if-final-only
// block), so this also exercises the driver's block-model compensation.
TEST(ExpressionTransforms, CompNotEqualsZeroDropsToOperandInIfCondition) {
    auto x = MakeParam("x");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Inequality, false);
    auto fn = MakeIfFinalOnlyFn(std::move(cond), {x});
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_EQ(iff->Condition->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(iff->Condition.get())->Variable.get(), x.get())
        << "if (x != 0) must fold to if (x)";
}

// comp(x != 0) -> x when the comp's left is itself a comp (the
// `comp(comp(...) != 0)` -> `comp(...)` case), even outside a condition slot.
TEST(ExpressionTransforms, CompNotEqualsZeroDropsToOperandWhenLeftIsComp) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    auto inner = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b), ComparisonKind::Equality, false);
    auto outer = std::make_unique<Comp>(std::move(inner), std::make_unique<LdcI4>(0),
                                         ComparisonKind::Inequality, false);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Equality)
        << "comp(comp(eq,a,b) != 0) must fold to comp(eq,a,b)";
}

// comp.unsigned(left > 0) cascades to `left`: `comp(gt.un, x, 0)` -> `comp(ne, x, 0)`
// -> (in condition slot) `x`.
TEST(ExpressionTransforms, CompUnsignedGreaterThanZeroBecomesOperand) {
    auto x = MakeParam("x");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::GreaterThan, /*unsigned=*/true);
    auto fn = MakeIfFinalOnlyFn(std::move(cond), {x});
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_EQ(iff->Condition->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(iff->Condition.get())->Variable.get(), x.get())
        << "if (x >u 0) must fold to if (x)";
}

// comp.unsigned(left <= 0) -> comp(left == 0) -> logic.not(left): `if (x <=u 0)`
// folds to `if (!x)` (rendered as comp(eq, x, 0), the reader's brfalse shape).
TEST(ExpressionTransforms, CompUnsignedLessThanOrEqualZeroBecomesLogicNot) {
    auto x = MakeParam("x");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::LessThanOrEqual, /*unsigned=*/true);
    auto fn = MakeIfFinalOnlyFn(std::move(cond), {x});
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff, nullptr);
    ASSERT_EQ(iff->Condition->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(iff->Condition.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Equality)
        << "if (x <=u 0) must fold to comp(eq, x, 0) == !x";
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 0);
}

// comp(x != 0) is left alone when it is NOT in a condition slot and its left is
// not a comp (the value is a real boolean expression, not a redundant test).
TEST(ExpressionTransforms, CompNotEqualsZeroStaysWhenNotInConditionSlot) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    // stloc v(comp(x != 0)) -- the comp is the stloc's value (not a condition
    // slot, left is a load not a comp).
    auto comp = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Inequality, false);
    auto fn = MakeFnWithBlock({v, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->Kind, ComparisonKind::Inequality)
        << "comp(x != 0) outside a condition slot must stay";
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 0);
}

// logic.not of a float comparison is NOT pushed (negating a float ordering is
// not a simple kind flip): comp(eq, comp(lt, a, b), 0) with float a,b stays.
TEST(ExpressionTransforms, LogicNotFloatComparisonNotPushed) {
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Single));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Single));
    auto v = MakeLocal("v");
    auto inner = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b), ComparisonKind::LessThan, false);
    auto outer = std::make_unique<Comp>(std::move(inner), std::make_unique<LdcI4>(0),
                                         ComparisonKind::Equality, false);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* outer2 = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(outer2->Kind, ComparisonKind::Equality)
        << "the logic.not wrapper must stay (float negation suppressed)";
    ASSERT_EQ(outer2->Left->Op, OpCode::Comp);
    auto* inner2 = static_cast<Comp*>(outer2->Left.get());
    EXPECT_EQ(inner2->Kind, ComparisonKind::LessThan)
        << "the inner float comparison must not be negated";
}

// The driver visits an if-final-only block's condition (the block-model
// compensation): a block with no non-terminal statements but an IfInstruction
// final still gets ExpressionTransforms run on the if's condition. A Leave-final
// only block (no if) is not visited.
TEST(ExpressionTransforms, IfFinalOnlyBlockConditionIsVisited) {
    // if-final-only block: condition comp(x != 0) folds to x.
    {
        auto x = MakeParam("x");
        auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(x),
                                           std::make_unique<LdcI4>(0),
                                           ComparisonKind::Inequality, false);
        auto fn = MakeIfFinalOnlyFn(std::move(cond), {x});
        fn->CheckInvariant(ILPhase::Normal);
        RunExpressionTransforms(*fn);
        fn->CheckInvariant(ILPhase::Normal);
        auto* iff = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
        ASSERT_NE(iff, nullptr);
        EXPECT_EQ(iff->Condition->Op, OpCode::LdLoc)
            << "if-final-only block condition must be visited by the driver";
    }
    // A Leave-final-only block (no non-terminals, no if) is skipped -- the driver
    // only runs the compensation for an IfInstruction final.
    {
        auto fn = std::make_unique<ILFunction>();
        fn->Body = std::make_unique<BlockContainer>();
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;
        auto blk = std::make_unique<Block>();
        blk->SetFinal(std::make_unique<Leave>(fn->Body.get()));
        fn->Body->AddBlock(std::move(blk));
        fn->CheckInvariant(ILPhase::Normal);
        RunExpressionTransforms(*fn);
        fn->CheckInvariant(ILPhase::Normal);
        EXPECT_EQ(fn->Body->Blocks[0]->Instructions.size(), 0u);
    }
}

// HandleConditionalOperator folds `if (cond) stloc A(V1) else stloc A(V2)` into
// `stloc A(if (!cond) V2 else V1))` (the conditional/ternary operator). Both arms
// are expression Blocks (no FinalInstruction) with a single StLoc to the same
// variable; the StLoc becomes a non-terminal and a Branch to the next block
// replaces the if-final (the block-model adaptation).
TEST(ExpressionTransforms, HandleConditionalOperatorFoldsTernary) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    // if (a != b) { stloc v(a) } else { stloc v(b) }
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(MakeTernaryBlock(v, std::move(cond),
                                        std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b)));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P = *fn->Body->Blocks[0];
    // The StLoc was appended to the block's non-terminal Instructions.
    ASSERT_EQ(P.Instructions.size(), 1u);
    ASSERT_EQ(P.Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(P.Instructions[0].get());
    EXPECT_EQ(st->Variable.get(), v.get()) << "the temp must survive the fold";
    // The StLoc's value is the conditional operator IfInstruction.
    ASSERT_EQ(st->Value->Op, OpCode::IfInstruction);
    auto* newIf = static_cast<IfInstruction*>(st->Value.get());
    // The condition was negated: comp(ne, a, b) -> comp(eq, a, b).
    ASSERT_EQ(newIf->Condition->Op, OpCode::Comp);
    EXPECT_EQ(static_cast<Comp*>(newIf->Condition.get())->Kind, ComparisonKind::Equality)
        << "the condition must be negated";
    // TrueInst = V2 (b), FalseInst = V1 (a) -- the C# swaps so `cond ? V1 : V2`.
    ASSERT_EQ(newIf->TrueInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(newIf->TrueInst.get())->Variable.get(), b.get());
    ASSERT_EQ(newIf->FalseInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(newIf->FalseInst.get())->Variable.get(), a.get());
    // The if-final is now a Branch to Q (the positional fall-through).
    ASSERT_EQ(P.FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(P.FinalInstruction.get())->TargetBlock,
              fn->Body->Blocks[1].get());
}

// HandleConditionalOperator does not fold when the two arms store to different
// variables (the conditional operator requires both arms to assign the same temp).
TEST(ExpressionTransforms, HandleConditionalOperatorRejectsDifferentVariables) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v1 = MakeLocal("v1");
    auto v2 = MakeLocal("v2");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    // True arm stores to v1, false arm to v2 -- different variables.
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v1, std::make_unique<LdLoc>(a)));
    auto falseBlock = std::make_unique<Block>();
    falseBlock->Add(std::make_unique<StLoc>(v2, std::make_unique<LdLoc>(b)));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock),
                                               std::move(falseBlock));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    for (auto vv : {v1, v2, a, b}) fn->Variables.push_back(vv);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // The if-final stays (no fold); the block has no non-terminal StLoc.
    auto& P2 = *fn->Body->Blocks[0];
    EXPECT_EQ(P2.Instructions.size(), 0u);
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::IfInstruction)
        << "the if must stay when the arms store different variables";
}

// HandleConditionalOperator does not fold when an arm is a control-flow Block
// (has a FinalInstruction) -- those were already handled by the block transform
// and the C# skips BlockKind.ControlFlow.
TEST(ExpressionTransforms, HandleConditionalOperatorRejectsArmWithFinal) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Body->AddBlock(MakeTernaryBlock(v, std::move(cond),
                                        std::make_unique<LdLoc>(a),
                                        std::make_unique<LdLoc>(b),
                                        /*withFinal=*/true));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P = *fn->Body->Blocks[0];
    EXPECT_EQ(P.Instructions.size(), 0u);
    ASSERT_EQ(P.FinalInstruction->Op, OpCode::IfInstruction)
        << "the if must stay when an arm has a FinalInstruction";
}

// HandleConditionalOperator does not fold when an arm Block has more than one
// instruction (the conditional operator requires each arm to be a single store).
TEST(ExpressionTransforms, HandleConditionalOperatorRejectsMultiInstructionArm) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    auto v = MakeLocal("v");
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    // True arm: two StLocs to v (not a single store).
    auto trueBlock = std::make_unique<Block>();
    trueBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(a)));
    trueBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(b)));
    auto falseBlock = std::make_unique<Block>();
    falseBlock->Add(std::make_unique<StLoc>(v, std::make_unique<LdLoc>(b)));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueBlock),
                                               std::move(falseBlock));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    for (auto vv : {v, a, b}) fn->Variables.push_back(vv);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P2 = *fn->Body->Blocks[0];
    EXPECT_EQ(P2.Instructions.size(), 0u);
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::IfInstruction)
        << "the if must stay when an arm has more than one instruction";
}

// The logic.and/or canonicalization swaps the arms and negates the condition
// when the true arm is ldc.i4 0 and the false arm is not: `if (cond) 0 else RHS`
// -> `if (!cond) RHS else 0`.
TEST(ExpressionTransforms, LogicAndOrCanonicalizationSwapsZeroTrueArm) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    // if (a) ldc.i4 0 else ldloc b -- the `a && b`-shaped normalization.
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdcI4>(0),
                                       ComparisonKind::Inequality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto iff = std::make_unique<IfInstruction>(std::move(cond),
                                               std::make_unique<LdcI4>(0),
                                               std::make_unique<LdLoc>(b));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff2 = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff2, nullptr);
    // The arms were swapped: TrueInst is now ldloc b, FalseInst is ldc.i4 0.
    ASSERT_EQ(iff2->TrueInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(iff2->TrueInst.get())->Variable.get(), b.get());
    ASSERT_EQ(iff2->FalseInst->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(iff2->FalseInst.get())->Value, 0);
    // The condition was negated: comp(ne, a, 0) -> comp(eq, a, 0) = !a.
    ASSERT_EQ(iff2->Condition->Op, OpCode::Comp);
    EXPECT_EQ(static_cast<Comp*>(iff2->Condition.get())->Kind, ComparisonKind::Equality)
        << "the condition must be negated by the canonicalization";
}

// The logic.and/or canonicalization does NOT swap when both arms are ldc.i4 1
// (the infinite-loop guard: swapping `1 else 1` would just re-trigger).
TEST(ExpressionTransforms, LogicAndOrBothOneNoSwap) {
    auto a = MakeParam("a");
    auto b = MakeParam("b");
    // if (a == b) ldc.i4 1 else ldc.i4 1 -- both arms 1, no swap. The condition
    // is comp(eq, a, b) (not comp(!=0), so the D81 comp(!=0)->x rewrite leaves it;
    // not a logic.not, so it stays a Comp) -- isolating the canonicalization.
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(a),
                                       std::make_unique<LdLoc>(b),
                                       ComparisonKind::Equality, false);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto iff = std::make_unique<IfInstruction>(std::move(cond),
                                               std::make_unique<LdcI4>(1),
                                               std::make_unique<LdcI4>(1));
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(a);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* iff2 = dynamic_cast<IfInstruction*>(fn->Body->Blocks[0]->FinalInstruction.get());
    ASSERT_NE(iff2, nullptr);
    // No swap: TrueInst stays ldc.i4 1, FalseInst stays ldc.i4 1.
    ASSERT_EQ(iff2->TrueInst->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(iff2->TrueInst.get())->Value, 1);
    ASSERT_EQ(iff2->FalseInst->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(iff2->FalseInst.get())->Value, 1);
    // The condition was not negated (no swap -> no negate): stays comp(eq, a, b).
    ASSERT_EQ(iff2->Condition->Op, OpCode::Comp);
    EXPECT_EQ(static_cast<Comp*>(iff2->Condition.get())->Kind, ComparisonKind::Equality)
        << "the condition must not be negated when the swap is suppressed";
}

// match(x) ? true : false -> match(x): a conditional whose condition is a
// pattern match and whose arms are ldc.i4 1 / ldc.i4 0 is redundant -- the
// MatchInstruction already evaluates to 1 (matched) / 0 (not matched). When the
// if is a sub-expression value (here `stloc boolVar(if (match) 1 else 0)`), the
// fold is a clean in-place ReplaceWith: the StLoc's value becomes the match.
TEST(ExpressionTransforms, MatchTrueFalseFoldsToMatchInValuePosition) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    auto boolVar = MakeLocal("boolVar");
    // stloc boolVar(if (match.type[T].notnull(v = ldloc x)) ldc.i4 1 else ldc.i4 0)
    auto match = MakeMatch(v, std::make_unique<LdLoc>(x));
    auto iff = std::make_unique<IfInstruction>(std::move(match),
                                                std::make_unique<LdcI4>(1),
                                                std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({boolVar, v, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(boolVar, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Variable.get(), boolVar.get());
    // The StLoc's value is now the MatchInstruction (the if was replaced by it).
    ASSERT_EQ(st->Value->Op, OpCode::MatchInstruction)
        << "if (match) 1 else 0 must fold to the match in the value slot";
    auto* m = static_cast<MatchInstruction*>(st->Value.get());
    EXPECT_TRUE(m->CheckType && m->CheckNotNull) << "the match pattern is preserved";
    ASSERT_EQ(m->TestedOperand->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(m->TestedOperand.get())->Variable.get(), x.get());
    EXPECT_EQ(m->Variable.get(), v.get());
}

// match(x) ? true : false as a block's FinalInstruction (a statement-if): the
// MatchInstruction (a value, not control flow) cannot be the final, so it
// becomes a non-terminal statement (its side effect -- storing into Variable --
// is preserved) and a Branch to the next block replaces the if-final (the
// HandleConditionalOperator block-model adaptation).
TEST(ExpressionTransforms, MatchTrueFalseFoldsToMatchAsBlockFinal) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    // P: if (match.type[T].notnull(v = ldloc x)) ldc.i4 1 else ldc.i4 0  (final)
    // Q: leave
    auto match = MakeMatch(v, std::make_unique<LdLoc>(x));
    auto iff = std::make_unique<IfInstruction>(std::move(match),
                                                std::make_unique<LdcI4>(1),
                                                std::make_unique<LdcI4>(0));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(x);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P2 = *fn->Body->Blocks[0];
    // The match became a non-terminal statement; the if-final is now a Branch to Q.
    ASSERT_EQ(P2.Instructions.size(), 1u);
    ASSERT_EQ(P2.Instructions[0]->Op, OpCode::MatchInstruction)
        << "the match must become a non-terminal statement";
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(P2.FinalInstruction.get())->TargetBlock,
              fn->Body->Blocks[1].get())
        << "the if-final must be replaced by a Branch to the next block";
}

// The fold does not fire when the condition is not a pattern match (a bare
// ldloc, not a MatchInstruction/Comp-pattern/Call): the if stays.
TEST(ExpressionTransforms, MatchTrueFalseRejectsNonPatternCondition) {
    auto x = MakeParam("x");
    auto boolVar = MakeLocal("boolVar");
    // stloc boolVar(if (ldloc x) ldc.i4 1 else ldc.i4 0) -- the condition is a
    // bare load, not a pattern match.
    auto iff = std::make_unique<IfInstruction>(std::make_unique<LdLoc>(x),
                                                std::make_unique<LdcI4>(1),
                                                std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({boolVar, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(boolVar, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    // The if stays (no fold): the StLoc's value is still the IfInstruction.
    ASSERT_EQ(st->Value->Op, OpCode::IfInstruction)
        << "a non-pattern condition must not fold";
}

// The fold does not fire when the true arm is not ldc.i4 1.
TEST(ExpressionTransforms, MatchTrueFalseRejectsNonOneTrueArm) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    auto boolVar = MakeLocal("boolVar");
    // stloc boolVar(if (match) ldc.i4 0 else ldc.i4 0) -- true arm is 0, not 1.
    auto match = MakeMatch(v, std::make_unique<LdLoc>(x));
    auto iff = std::make_unique<IfInstruction>(std::move(match),
                                                std::make_unique<LdcI4>(0),
                                                std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({boolVar, v, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(boolVar, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::IfInstruction)
        << "a non-1 true arm must not fold";
}

// The fold does not fire when the false arm is not ldc.i4 0.
TEST(ExpressionTransforms, MatchTrueFalseRejectsNonZeroFalseArm) {
    auto x = MakeParam("x");
    auto v = MakeLocal("v");
    auto boolVar = MakeLocal("boolVar");
    // stloc boolVar(if (match) ldc.i4 1 else ldc.i4 1) -- false arm is 1, not 0.
    auto match = MakeMatch(v, std::make_unique<LdLoc>(x));
    auto iff = std::make_unique<IfInstruction>(std::move(match),
                                                std::make_unique<LdcI4>(1),
                                                std::make_unique<LdcI4>(1));
    auto fn = MakeFnWithBlock({boolVar, v, x});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(boolVar, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::IfInstruction)
        << "a non-0 false arm must not fold";
}

// VisitBox drops `box ref-type(arg)` to `arg`: for a reference type, box is a
// no-op (the value is already on the heap). `stloc v(box string(ldloc s))`
// folds to `stloc v(ldloc s)`.
TEST(ExpressionTransforms, VisitBoxDropsBoxOfReferenceType) {
    auto s = MakeParam("s", std::make_shared<KnownType>(KnownTypeCode::String));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto box = std::make_unique<Box>(
        std::make_shared<KnownType>(KnownTypeCode::String),
        std::make_unique<LdLoc>(s));
    auto fn = MakeFnWithBlock({v, s});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    // The box was replaced by its argument: the StLoc's value is now ldloc s.
    ASSERT_EQ(st->Value->Op, OpCode::LdLoc)
        << "box string(ldloc s) must fold to ldloc s";
    EXPECT_EQ(static_cast<LdLoc*>(st->Value.get())->Variable.get(), s.get());
    EXPECT_EQ(CountBoxes(*fn), 0) << "no Box node must remain after the fold";
}

// VisitBox keeps `box <value-type>(arg)`: a value type's box is NOT a no-op
// (it allocates a boxed copy). The argument's stack type (I4 for int) does not
// match the box's result (O), so the fold does not fire even though a value type
// is never a reference type.
TEST(ExpressionTransforms, VisitBoxKeepsBoxOfValueType) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto box = std::make_unique<Box>(
        std::make_shared<KnownType>(KnownTypeCode::Int32),
        std::make_unique<LdLoc>(i));
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Box)
        << "box int(ldloc i) must stay (boxing a value type is not a no-op)";
    EXPECT_EQ(CountBoxes(*fn), 1) << "the Box node must survive";
}

// VisitBox keeps `box T(arg)` over a generic type parameter: IsReferenceType is
// nullopt for a TypeParameter (the kind alone cannot tell -- it depends on the
// `where T : class` constraint this minimal type system does not track), so the
// fold is conservative and the box stays. The C# folds it only when T has a
// class constraint.
TEST(ExpressionTransforms, VisitBoxKeepsBoxOfTypeParameter) {
    auto T = std::make_shared<ILSpy::Decompiler::TypeSystem::TypeParameter>(
        0, ILSpy::Decompiler::TypeSystem::TypeParameter::OwnerKind::Method, "T");
    auto t = MakeParam("t", T);
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    // box T(ldloc t) -- the type parameter boxes its argument.
    auto box = std::make_unique<Box>(T, std::make_unique<LdLoc>(t));
    auto fn = MakeFnWithBlock({v, t});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Box)
        << "box T(arg) over a type parameter must stay (IsReferenceType is nullopt)";
    EXPECT_EQ(CountBoxes(*fn), 1);
}

// VisitBox keeps a box whose type resolved to null (the reader hands back a
// null ITypePtr when it cannot resolve the token): the fold must not dereference
// a null type.
TEST(ExpressionTransforms, VisitBoxKeepsBoxWithNullType) {
    auto o = MakeParam("o", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto box = std::make_unique<Box>(nullptr, std::make_unique<LdLoc>(o));
    auto fn = MakeFnWithBlock({v, o});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Box)
        << "a box with a null type must stay (no dereference)";
    EXPECT_EQ(CountBoxes(*fn), 1);
}

// VisitBox drops `box object(ldloc o)`: object is a reference type, so boxing
// an already-object-typed value is a no-op. (The argument is stack-type O
// matching the box's result O.)
TEST(ExpressionTransforms, VisitBoxDropsBoxOfObjectReferenceType) {
    auto o = MakeParam("o", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto box = std::make_unique<Box>(
        std::make_shared<KnownType>(KnownTypeCode::Object),
        std::make_unique<LdLoc>(o));
    auto fn = MakeFnWithBlock({v, o});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(box)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::LdLoc)
        << "box object(ldloc o) must fold to ldloc o";
    EXPECT_EQ(static_cast<LdLoc*>(st->Value.get())->Variable.get(), o.get());
    EXPECT_EQ(CountBoxes(*fn), 0);
}

// Count Conv nodes whose ResultType is I (native int) sitting directly in a
// LdElema or NewArr Indices collection -- the array-index widening convs that
// CleanUpArrayIndices removes. The fold is monotone non-increasing (each fold
// drops one such conv; nothing in this subset creates one). Used by the sweep.
int CountArrayIndexConvI(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::LdElema) {
            auto* ld = static_cast<LdElema*>(inst);
            for (auto& idx : ld->Indices)
                if (idx && idx->Op == OpCode::Conv && idx->ResultType() == StackType::I) ++n;
        } else if (inst->Op == OpCode::NewArr) {
            auto* na = static_cast<NewArr*>(inst);
            for (auto& idx : na->Indices)
                if (idx && idx->Op == OpCode::Conv && idx->ResultType() == StackType::I) ++n;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// CleanUpArrayIndices drops `conv.i` (SignExtend, I4->I) widening of an array
// element-address index: ldelema(arr, conv.i(ldloc idx)) -> ldelema(arr, ldloc
// idx). The conv only widens the I4 index to native int and is redundant in C#.
TEST(ExpressionTransforms, CleanUpArrayIndicesDropsConvIFromLdElema) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    // conv.i(ldloc idx): SignExtend I4 -> I (the reader's Conv_i from I4).
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(idx), PrimitiveType::I, false, Sign::None));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountArrayIndexConvI(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountArrayIndexConvI(*fn), 0)
        << "conv.i widening of an array index must be dropped";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::LdElema);
    auto* ld = static_cast<LdElema*>(st->Value.get());
    ASSERT_EQ(ld->Indices.size(), 1u);
    EXPECT_EQ(ld->Indices[0]->Op, OpCode::LdLoc)
        << "the index must be the bare ldloc idx after the conv is dropped";
}

// CleanUpArrayIndices drops `conv.u` (ZeroExtend, I4->I) from a NewArr length.
TEST(ExpressionTransforms, CleanUpArrayIndicesDropsConvUFromNewArr) {
    auto len = MakeParam("len", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    // conv.u(ldloc len): ZeroExtend I4 -> I (the reader's Conv_u from I4).
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(len), PrimitiveType::U, false, Sign::None));
    auto newArr = std::make_unique<NewArr>(elemType, std::move(indices));
    auto fn = MakeFnWithBlock({len, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(newArr)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountArrayIndexConvI(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountArrayIndexConvI(*fn), 0)
        << "conv.u widening of the newarr length must be dropped";
}

// CleanUpArrayIndices drops a checked `conv.ovf.i` (Truncate + CheckForOverflow):
// an overflow-checked widening is safe to drop (it would throw only on values
// outside I4 range, which a C# int index cannot produce).
TEST(ExpressionTransforms, CleanUpArrayIndicesDropsConvOvfIFromLdElema) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    // conv.ovf.i(ldloc idx): SignExtend I4 -> I with overflow check (the reader's
    // Conv_ovf_i from I4; needsSign forces InputSign = Signed, Kind = SignExtend).
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(idx), PrimitiveType::I, true, Sign::Signed));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountArrayIndexConvI(*fn), 0)
        << "conv.ovf.i (checked SignExtend) widening of an array index is dropped";
}

// CleanUpArrayIndices keeps `conv.i` from an I8 input (Truncate without overflow
// check): that is a real truncation (the index is a long narrowed to native
// int), not a redundant widening, so the conv must survive.
TEST(ExpressionTransforms, CleanUpArrayIndicesKeepsConvIFromI8) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int64));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    // conv.i(ldloc idx) where idx is I8: Truncate I8 -> I, no overflow check.
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(idx), PrimitiveType::I, false, Sign::None));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountArrayIndexConvI(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountArrayIndexConvI(*fn), 1)
        << "conv.i from I8 (unchecked Truncate) is a real truncation and stays";
}

// CleanUpArrayIndices keeps `conv.i4` (Nop, I4->I4) in an index: its ResultType
// is I4 (not I), so the `ResultType == I` guard excludes it. (conv.i4 around an
// index would be a no-op cast, not a native-int widening.)
TEST(ExpressionTransforms, CleanUpArrayIndicesKeepsConvI4) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<Conv>(
        std::make_unique<LdLoc>(idx), PrimitiveType::I4, false, Sign::None));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // The conv.i4 stays (its ResultType is I4, not I, so CleanUpArrayIndices
    // skips it); CountArrayIndexConvI counts only ResultType==I convs, so it is 0
    // either way -- verify the conv node itself survived.
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* ld = static_cast<LdElema*>(st->Value.get());
    ASSERT_EQ(ld->Indices[0]->Op, OpCode::Conv)
        << "conv.i4 (Nop, ResultType I4) in an index must stay";
}

// CleanUpArrayIndices leaves a bare (non-conv) index untouched.
TEST(ExpressionTransforms, CleanUpArrayIndicesLeavesBareIndex) {
    auto arr = MakeParam("arr", std::make_shared<KnownType>(KnownTypeCode::String));
    auto idx = MakeParam("idx", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v");
    auto elemType = std::make_shared<KnownType>(KnownTypeCode::String);
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::make_unique<LdLoc>(idx));
    auto ldElema = std::make_unique<LdElema>(
        elemType, std::make_unique<LdLoc>(arr), std::move(indices));
    auto fn = MakeFnWithBlock({arr, idx, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(ldElema)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* ld = static_cast<LdElema*>(st->Value.get());
    ASSERT_EQ(ld->Indices.size(), 1u);
    EXPECT_EQ(ld->Indices[0]->Op, OpCode::LdLoc)
        << "a bare index must survive CleanUpArrayIndices unchanged";
}

// Count `conv.rN(conv.r.un(...))` patterns -- a float-target Conv (R4/R8/R)
// whose Argument is a Conv with Kind == IntToFloat and TargetType == R (the
// uncombined conv.r.un the VisitConv fold removes). The fold is monotone non-
// increasing (each fold removes one such nested pattern; nothing in this subset
// creates one). Used by the sweep to confirm the transform does not regress.
int CountConvRUnNested(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Conv) {
            auto* outer = static_cast<Conv*>(inst);
            if (IsFloatType(outer->TargetType) && outer->Argument &&
                outer->Argument->Op == OpCode::Conv) {
                auto* inner = static_cast<Conv*>(outer->Argument.get());
                if (inner->Kind == ConversionKind::IntToFloat &&
                    inner->TargetType == PrimitiveType::R) {
                    ++n;
                }
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// VisitConv combines `conv.r4(conv.r.un(x))` -> `conv.r4.un(x)`: IL conv.r.un
// does not say whether to convert to R4 or R8, so the C# compiler follows it with
// an explicit conv.r4; the two convs fold to a single `conv.r4.un` (int-to-
// float, unsigned input) carrying the inner conv's input sign but the outer's R4
// target. The integer argument (ldloc i, I4) is preserved as the new conv's
// argument.
TEST(ExpressionTransforms, VisitConvCombinesConvR4OverConvRUn) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Single));
    // conv.r.un(ldloc i): the reader's Conv_r_un -- Unsigned, TargetType R,
    // Kind IntToFloat (I4 -> F8).
    auto inner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(i), PrimitiveType::R, false, Sign::Unsigned);
    // conv.r4(conv.r.un(ldloc i)): the reader's Conv_r4 -- Signed (but the outer
    // conv.r4's InputType is F8, so needsSign is false and InputSign is None); the
    // fold checks the inner conv's Kind/TargetType, not the outer's sign.
    auto outer = std::make_unique<Conv>(
        std::move(inner), PrimitiveType::R4, false, Sign::Signed);
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0)
        << "conv.r4(conv.r.un(...)) must fold to a single conv.r4.un";
    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Conv)
        << "the result must be a single Conv (the combined conv.r4.un)";
    auto* combined = static_cast<Conv*>(st->Value.get());
    EXPECT_EQ(combined->TargetType, PrimitiveType::R4)
        << "the combined conv keeps the outer's R4 target";
    EXPECT_EQ(combined->Kind, ConversionKind::IntToFloat)
        << "the combined conv is an int-to-float conversion";
    EXPECT_EQ(combined->InputSign, Sign::Unsigned)
        << "the combined conv carries the inner conv.r.un's unsigned sign";
    ASSERT_EQ(combined->Argument->Op, OpCode::LdLoc)
        << "the integer argument survives the fold";
    EXPECT_EQ(static_cast<LdLoc*>(combined->Argument.get())->Variable.get(), i.get());
}

// VisitConv combines `conv.r8(conv.r.un(x))` -> `conv.r8.un(x)` (the R8 variant).
TEST(ExpressionTransforms, VisitConvCombinesConvR8OverConvRUn) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Double));
    auto inner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(i), PrimitiveType::R, false, Sign::Unsigned);
    auto outer = std::make_unique<Conv>(
        std::move(inner), PrimitiveType::R8, false, Sign::Signed);
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0);
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* combined = static_cast<Conv*>(st->Value.get());
    ASSERT_EQ(combined->TargetType, PrimitiveType::R8);
    EXPECT_EQ(combined->Kind, ConversionKind::IntToFloat);
    EXPECT_EQ(combined->InputSign, Sign::Unsigned);
}

// VisitConv keeps `conv.r4(ldloc d)` (a bare float argument, not a conv.r.un):
// the argument is not a Conv, so the combining fold does not fire. (conv.r4 from
// a double is a FloatPrecisionChange, not an int-to-float combine.)
TEST(ExpressionTransforms, VisitConvKeepsConvR4OverBareFloat) {
    auto d = MakeParam("d", std::make_shared<KnownType>(KnownTypeCode::Double));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Single));
    auto outer = std::make_unique<Conv>(
        std::make_unique<LdLoc>(d), PrimitiveType::R4, false, Sign::Signed);
    auto fn = MakeFnWithBlock({v, d});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0)
        << "a bare-float-argument conv.r4 must not fold";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Conv)
        << "the conv.r4 must survive (its argument is not a conv.r.un)";
    auto* conv = static_cast<Conv*>(st->Value.get());
    EXPECT_EQ(conv->TargetType, PrimitiveType::R4);
    ASSERT_EQ(conv->Argument->Op, OpCode::LdLoc)
        << "the bare float argument survives unchanged";
}

// VisitConv keeps `conv.r4(conv.r8(ldloc i))`: the inner conv.r8 has Kind
// IntToFloat but TargetType R8 (not R), so the `conv.TargetType == R` guard
// excludes it. (This is a precision change from an int via R8, not a
// conv.r.un combine.)
TEST(ExpressionTransforms, VisitConvKeepsConvR4OverConvR8) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Single));
    auto inner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(i), PrimitiveType::R8, false, Sign::Signed);
    auto outer = std::make_unique<Conv>(
        std::move(inner), PrimitiveType::R4, false, Sign::Signed);
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0)
        << "conv.r4(conv.r8(...)) must not fold (the inner is not conv.r.un)";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    // The two-conv nest must survive (the outer is still a Conv wrapping a Conv).
    ASSERT_EQ(st->Value->Op, OpCode::Conv);
    auto* outerConv = static_cast<Conv*>(st->Value.get());
    ASSERT_EQ(outerConv->Argument->Op, OpCode::Conv)
        << "the nested conv.r8 must survive (it is not a conv.r.un)";
}

// VisitConv keeps `conv.i4(conv.r.un(ldloc d))`: the outer's TargetType is I4
// (an integer type, not a float type), so the `IsFloatType(TargetType)` guard
// excludes it. (This is a float-to-int conversion, not a conv.r.un combine.)
TEST(ExpressionTransforms, VisitConvKeepsConvI4OverConvRUn) {
    auto i = MakeParam("i", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto inner = std::make_unique<Conv>(
        std::make_unique<LdLoc>(i), PrimitiveType::R, false, Sign::Unsigned);
    auto outer = std::make_unique<Conv>(
        std::move(inner), PrimitiveType::I4, false, Sign::None);
    auto fn = MakeFnWithBlock({v, i});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(outer)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountConvRUnNested(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountConvRUnNested(*fn), 0)
        << "conv.i4(conv.r.un(...)) must not fold (the outer target is not float)";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Conv);
    auto* outerConv = static_cast<Conv*>(st->Value.get());
    ASSERT_EQ(outerConv->Argument->Op, OpCode::Conv)
        << "the nested conv.r.un must survive (the outer is conv.i4, not float)";
}

// Count `shift(x, bitAnd(y, mask))` patterns -- a ShiftLeft/ShiftRight whose
// Right is a BitAnd whose own Right is the expected bit-width-minus-one mask
// (ldc.i4 31 for an I4 shift, ldc.i4 63 for an I8 shift). This is the redundant
// mask the VisitBinaryNumericInstruction shift-size fold removes. The fold is
// monotone non-increasing (each fold removes one such masked-shift; nothing in
// this subset creates one). Used by the sweep to confirm the transform does not
// regress.
int CountMaskedShifts(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::BinaryNumericInstruction) {
            auto* bni = static_cast<BinaryNumericInstruction*>(inst);
            if ((bni->Operator == BinaryNumericOperator::ShiftLeft ||
                 bni->Operator == BinaryNumericOperator::ShiftRight) &&
                bni->Right && bni->Right->Op == OpCode::BinaryNumericInstruction) {
                auto* bitAnd = static_cast<BinaryNumericInstruction*>(bni->Right.get());
                if (bitAnd->Operator == BinaryNumericOperator::BitAnd &&
                    bitAnd->Right && bitAnd->Right->Op == OpCode::LdcI4) {
                    int mask = static_cast<LdcI4*>(bitAnd->Right.get())->Value;
                    StackType rt = bni->ResultType();
                    if ((rt == StackType::I4 && mask == 31) ||
                        (rt == StackType::I8 && mask == 63)) {
                        ++n;
                    }
                }
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// VisitBinaryNumericInstruction drops the redundant `& 31` mask from a left
// shift: `a << (b & 31)` folds to `a << b` (the shift already masks the count to
// the low 5 bits for an int). The BitAnd wrapper is destroyed and the real shift
// amount (`b`) becomes the shift's right operand.
TEST(ExpressionTransforms, ShiftLeftDropsBitAnd31Mask) {
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Int32));
    // shl(ldloc a, bitAnd(ldloc b, ldc.i4 31)) -- the C#/Roslyn `a << b` shape.
    auto masked = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(b), std::make_unique<LdcI4>(31),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto shl = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(a), std::move(masked),
        BinaryNumericOperator::ShiftLeft, StackType::I4);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(shl)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountMaskedShifts(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountMaskedShifts(*fn), 0)
        << "shl(a, b & 31) must fold to shl(a, b)";
    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::BinaryNumericInstruction);
    auto* shift = static_cast<BinaryNumericInstruction*>(st->Value.get());
    EXPECT_EQ(shift->Operator, BinaryNumericOperator::ShiftLeft)
        << "the shift operator is preserved";
    // The right operand is now the bare shift amount (ldloc b), not the BitAnd.
    ASSERT_EQ(shift->Right->Op, OpCode::LdLoc)
        << "the & 31 mask must be dropped, leaving the bare shift amount";
    EXPECT_EQ(static_cast<LdLoc*>(shift->Right.get())->Variable.get(), b.get());
    ASSERT_EQ(shift->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(shift->Left.get())->Variable.get(), a.get());
}

// VisitBinaryNumericInstruction drops the `& 31` mask from a right shift too:
// `a >> (b & 31)` folds to `a >> b`.
TEST(ExpressionTransforms, ShiftRightDropsBitAnd31Mask) {
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto masked = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(b), std::make_unique<LdcI4>(31),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto shr = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(a), std::move(masked),
        BinaryNumericOperator::ShiftRight, StackType::I4);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(shr)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountMaskedShifts(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountMaskedShifts(*fn), 0);
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* shift = static_cast<BinaryNumericInstruction*>(st->Value.get());
    EXPECT_EQ(shift->Operator, BinaryNumericOperator::ShiftRight);
    ASSERT_EQ(shift->Right->Op, OpCode::LdLoc)
        << "the & 31 mask must be dropped from a right shift";
    EXPECT_EQ(static_cast<LdLoc*>(shift->Right.get())->Variable.get(), b.get());
}

// VisitBinaryNumericInstruction drops the `& 63` mask from a long (I8) shift:
// `a << (b & 63)` folds to `a << b` (a long shift masks the count to the low 6
// bits).
TEST(ExpressionTransforms, ShiftLeftI8DropsBitAnd63Mask) {
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Int64));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Int64));
    auto masked = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(b), std::make_unique<LdcI4>(63),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto shl = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(a), std::move(masked),
        BinaryNumericOperator::ShiftLeft, StackType::I8);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(shl)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountMaskedShifts(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountMaskedShifts(*fn), 0)
        << "shl(long a, b & 63) must fold (the I8 mask is 63)";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* shift = static_cast<BinaryNumericInstruction*>(st->Value.get());
    ASSERT_EQ(shift->Right->Op, OpCode::LdLoc)
        << "the & 63 mask must be dropped from an I8 shift";
}

// VisitBinaryNumericInstruction keeps a shift whose mask is the wrong width
// for its result type: `a << (b & 31)` where `a` is a long (I8) is NOT folded,
// because 31 is not the expected 63 for a long shift. (The C# would leave it too
// -- a long shift masked to 5 bits is a real semantic constraint, not the
// standard C# `& 63` elision.)
TEST(ExpressionTransforms, ShiftKeepsWrongWidthMask) {
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Int64));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Int64));
    // shl(long a, b & 31) -- the mask 31 does not match the I8 expected 63.
    auto masked = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(b), std::make_unique<LdcI4>(31),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto shl = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(a), std::move(masked),
        BinaryNumericOperator::ShiftLeft, StackType::I8);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(shl)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountMaskedShifts(*fn), 0)
        << "the 31-masked I8 shift is not the expected-mask pattern";

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // The masked shift survives: the right operand is still the BitAnd.
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* shift = static_cast<BinaryNumericInstruction*>(st->Value.get());
    ASSERT_EQ(shift->Right->Op, OpCode::BinaryNumericInstruction)
        << "a long shift with a 31 mask (not the expected 63) must stay";
    auto* bitAnd = static_cast<BinaryNumericInstruction*>(shift->Right.get());
    EXPECT_EQ(bitAnd->Operator, BinaryNumericOperator::BitAnd);
}

// VisitBinaryNumericInstruction keeps a shift whose right operand is not a
// BitAnd (a bare shift amount): `a << b` is already clean, so the fold does not
// fire.
TEST(ExpressionTransforms, ShiftKeepsBareShiftAmount) {
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto shl = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b),
        BinaryNumericOperator::ShiftLeft, StackType::I4);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(shl)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    auto* shift = static_cast<BinaryNumericInstruction*>(st->Value.get());
    ASSERT_EQ(shift->Right->Op, OpCode::LdLoc)
        << "a bare shift amount must survive unchanged";
    EXPECT_EQ(static_cast<LdLoc*>(shift->Right.get())->Variable.get(), b.get());
}

// VisitBinaryNumericInstruction leaves a top-level BitAnd (not a shift) alone:
// the BitAnd/Boolean nullable-lift case is deferred, so a plain `a & b` just
// visits its children and returns (the operands may fold, but the BitAnd stays).
TEST(ExpressionTransforms, PlainBoolBitAndStaysNoLift) {
    // A top-level `BitAnd(LdLoc a, LdLoc b)` of two plain bool locals/params: both
    // operands are Boolean-typed (IsBooleanValue -> true for a Boolean LdLoc), so
    // the BitAnd gate in VisitBinaryNumericInstruction fires and
    // RunBinaryNumericNullableLift is invoked. But no nullable-lift fold matches a
    // plain bool LdLoc pair (the `&`/`|` on bool? fold needs a NullableCtor or a
    // second-nullable-LdLoc false arm, which the fresh LdcI4(0) falseInst is
    // not), so LiftNullableCore returns nullptr and the BitAnd stays -- the C#
    // likewise does not lift a plain `a & b`.
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto bitAnd = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(bitAnd)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::BinaryNumericInstruction);
    // (The EXPECT above, not a trailing-decl ASSERT, separates the declaration so
    // gtest's ASSERT_* fail-goto does not jump over the following declaration.)
    auto* bni = static_cast<BinaryNumericInstruction*>(st->Value.get());
    EXPECT_EQ(bni->Operator, BinaryNumericOperator::BitAnd)
        << "a plain bool BitAnd must stay (no nullable shape to lift)";
}

// Count NullCoalescingInstruction nodes in the tree. The VisitCall
// `Nullable<T>.GetValueOrDefault(a, b) -> a ?? b` fold is monotone non-
// decreasing (each fold creates a NullCoalescingInstruction; nothing in this
// subset removes one). Used by the sweep to measure the fold's corpus progress.
int CountNullCoalescing(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::NullCoalescingInstruction) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count 2-arg `call GetValueOrDefault(nullableValue, fallback)` calls on
// System.Nullable<T> -- the pattern the VisitCall fold matches and removes. The
// fold is monotone non-increasing for this count (each fold removes one such
// call; nothing in this subset creates one). Used by the sweep to confirm the
// transform does not regress.
int CountGetValueOrDefaultTwoArg(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Call) {
            ILInstruction* nv = nullptr;
            ILInstruction* fb = nullptr;
            if (NullableLiftingTransform::MatchGetValueOrDefault(inst, nv, fb)) ++n;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count C#-lifted Comp nodes (Comp.IsLifted() -- LiftingKind != None). The
// RunCompNullableLift fold is monotone non-decreasing for this count (each fold
// marks one comp C#-lifted; nothing in this subset un-lifts one). Used by the
// sweep to confirm the transform does not regress.
int CountLiftedComps(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Comp && static_cast<Comp*>(inst)->IsLifted()) ++n;
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count 1-arg `call GetValueOrDefault(arg)` calls on System.Nullable<T> -- the
// pattern RunCompNullableLift matches on one side of an eq/ne comp and rewrites
// to `ldobj Nullable<T>(arg)` (marking the comp C#-lifted). The fold is monotone
// non-increasing for this count (each fold removes one such call from inside a
// comp; nothing in this subset creates one). Used by the sweep.
int CountGetValueOrDefaultOneArg(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Call) {
            ILInstruction* a = nullptr;
            if (NullableLiftingTransform::MatchGetValueOrDefault(inst, a)) ++n;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// Count 1-arg `call get_HasValue(ldloca v)` calls on System.Nullable<T> -- the
// arm RunIfNullableLift consumes in the bool? equality fold (each fold removes
// one such call from the true/false arm; nothing in this subset creates one).
// The fold is monotone non-increasing for this count. Used by the sweep.
int CountHasValueCall(ILFunction& fn) {
    int n = 0;
    std::function<void(ILInstruction*)> walk = [&](ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Call) {
            ILInstruction* a = nullptr;
            if (NullableLiftingTransform::MatchHasValueCall(inst, a)) ++n;
        }
        for (int i = 0; i < inst->ChildCount(); ++i) walk(inst->GetChild(i));
    };
    walk(fn.Body.get());
    return n;
}

// VisitCall folds `call Nullable<T>.GetValueOrDefault(nullableValue, fallback)`
// (a pure fallback) into a NullCoalescingInstruction (NullableWithValueFallback)
// whose ValueInst is `ldobj Nullable<T>(nullableValue)` and FallbackInst is the
// fallback; UnderlyingResultType is the fallback's ResultType. The call is a
// value (wrapped in a stloc), so ReplaceWith is a clean in-place swap.
TEST(ExpressionTransforms, VisitCallFoldsGetValueOrDefaultToNullCoalescing) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Int32));
    // call Nullable<int>::GetValueOrDefault(ldloca v, ldc.i4 0) -- the `v ?? 0`
    // lowering (an instance call: Arguments[0] is the receiver ldloca v).
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(call)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountGetValueOrDefaultTwoArg(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountGetValueOrDefaultTwoArg(*fn), 0)
        << "the 2-arg GetValueOrDefault call must be folded away";
    EXPECT_EQ(CountNullCoalescing(*fn), 1)
        << "the fold must produce exactly one NullCoalescingInstruction";
    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::NullCoalescingInstruction)
        << "the call must be replaced by a NullCoalescingInstruction in the value slot";
    auto* nc = static_cast<NullCoalescingInstruction*>(st->Value.get());
    EXPECT_EQ(nc->Kind, NullCoalescingKind::NullableWithValueFallback)
        << "the fold produces the NullableWithValueFallback kind";
    EXPECT_EQ(nc->UnderlyingResultType, StackType::I4)
        << "UnderlyingResultType is the fallback's ResultType (ldc.i4 -> I4)";
    // ValueInst is `ldobj Nullable<int>(ldloca v)`.
    ASSERT_EQ(nc->ValueInst->Op, OpCode::LdObj);
    auto* ldObj = static_cast<LdObj*>(nc->ValueInst.get());
    ASSERT_EQ(ldObj->Target->Op, OpCode::LdLoca)
        << "the ldobj loads the nullable from the receiver's address";
    EXPECT_EQ(static_cast<LdLoca*>(ldObj->Target.get())->Variable.get(), v.get());
    // FallbackInst is the original ldc.i4 0.
    ASSERT_EQ(nc->FallbackInst->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(nc->FallbackInst.get())->Value, 0);
}

// VisitCall does not fold when the declaring type is not Nullable<T> (the call
// resolves to a non-Nullable type): the call stays and its arguments are visited.
TEST(ExpressionTransforms, VisitCallRejectsNonNullableDeclaringType) {
    auto v = MakeLocal("v");
    auto result = MakeLocal("result");
    // call System.Int32::GetValueOrDefault(ldloca v, ldc.i4 0) -- Int32 is not Nullable.
    auto call = std::make_unique<Call>("System.Int32::GetValueOrDefault");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(call)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "a non-Nullable declaring type must not fold";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Call)
        << "the call must stay (the declaring type is not Nullable<T>)";
}

// VisitCall does not fold the 1-arg GetValueOrDefault form (the fold requires the
// 2-arg form with a fallback): the 1-arg call stays.
TEST(ExpressionTransforms, VisitCallRejectsOneArgGetValueOrDefault) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result");
    // call Nullable<int>::GetValueOrDefault(ldloca v) -- the 1-arg form.
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(call)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "the 1-arg form must not fold (the fold requires the 2-arg form)";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Call)
        << "the 1-arg GetValueOrDefault call must stay";
}

// VisitCall does not fold when the method name is not GetValueOrDefault (e.g.
// get_HasValue, also on Nullable<T>): the call stays.
TEST(ExpressionTransforms, VisitCallRejectsWrongMethodName) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    // call Nullable<int>::get_HasValue(ldloca v) -- the method is get_HasValue, not
    // GetValueOrDefault (and only 1 arg, so it would not match the 2-arg form anyway).
    auto call = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(call)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "a get_HasValue call must not fold";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Call)
        << "the get_HasValue call must stay (wrong method name)";
}

// VisitCall does not fold when the fallback is not pure (folding an impure
// fallback into the NullCoalescingInstruction's FallbackInst would drop its side
// effect when the value is non-null -- the C# guards on IsPure(fallback.Flags)).
// A Call with SideEffect is impure, so the fold does not fire.
TEST(ExpressionTransforms, VisitCallRejectsImpureFallback) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Int32));
    // The fallback is a call (SideEffect | MayThrow -> not pure). The call's
    // declaring type is Nullable<int> (so MatchGetValueOrDefault matches), but
    // IsPure(fallback.Flags) is false, so the fold does not fire.
    auto impureFallback = std::make_unique<Call>("System.SomeType::SideEffecting");
    impureFallback->AddArg(std::make_unique<LdcI4>(1));
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::move(impureFallback));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(call)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountGetValueOrDefaultTwoArg(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "an impure fallback must not fold (would drop the side effect)";
    EXPECT_EQ(CountGetValueOrDefaultTwoArg(*fn), 1)
        << "the 2-arg GetValueOrDefault call must stay when the fallback is impure";
}

// VisitCall does not fold when the call's declaring type could not be resolved
// (null DeclaringType -- treated like the C# null DeclaringTypeDefinition): the
// MatchGetValueOrDefault helper returns false, so the call stays.
TEST(ExpressionTransforms, VisitCallRejectsNullDeclaringType) {
    auto v = MakeLocal("v");
    auto result = MakeLocal("result");
    // call GetValueOrDefault(ldloca v, ldc.i4 0) with a null DeclaringType.
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->AddArg(std::make_unique<LdLoca>(v));
    call->AddArg(std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(call)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "a null declaring type must not fold";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Call)
        << "the call must stay when the declaring type is null";
}

// RunCompNullableLift (NullableLiftingTransform.Run(Comp)) lifts the
// VS2022.10/Roslyn 4.10 `a.GetValueOrDefault() == const` (no HasValue check)
// optimization back to a C#-lifted `comp.lifted[C#](a == const)`. A non-lifted
// equality whose Left is `call GetValueOrDefault(arg)` on Nullable<T> and whose
// Right is a non-zero ldc.i4 has Left replaced by `ldobj Nullable<T>(arg)` and
// is marked C#-lifted. The comp is a value (wrapped in a stloc, not in an if
// condition slot) so the head rewrites (logic.not / comp(!=0)=>x, which require
// Right == 0) do not fire and RunCompNullableLift is reached.
TEST(ExpressionTransforms, RunCompNullableLiftFoldsGetValueOrDefaultEqualToConst) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    // call Nullable<int>::GetValueOrDefault(ldloca v) -- the 1-arg accessor.
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    // comp(eq, call GetValueOrDefault(ldloca v), ldc.i4 5)
    auto comp = std::make_unique<Comp>(std::move(call), std::make_unique<LdcI4>(5),
                                      ComparisonKind::Equality);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLiftedComps(*fn), 0);
    ASSERT_EQ(CountGetValueOrDefaultOneArg(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "the comp must be marked C#-lifted";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 0)
        << "the GetValueOrDefault call must be folded away";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp) << "the stloc still wraps the comp";
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Equality);
    // Left is now `ldobj Nullable<int>(ldloca v)`.
    ASSERT_EQ(c->Left->Op, OpCode::LdObj);
    auto* ldObj = static_cast<LdObj*>(c->Left.get());
    ASSERT_EQ(ldObj->Target->Op, OpCode::LdLoca)
        << "the ldobj loads the nullable from the receiver's address";
    EXPECT_EQ(static_cast<LdLoca*>(ldObj->Target.get())->Variable.get(), v.get());
    // Right is still the ldc.i4 5.
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 5);
}

// The constant may be on the Left and the GetValueOrDefault call on the Right;
// RunCompNullableLift's second branch handles `comp(const == a.GetValueOrDefault())`
// and replaces the Right operand with `ldobj Nullable<T>(arg)`.
TEST(ExpressionTransforms, RunCompNullableLiftFoldsConstEqualToGetValueOrDefault) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    // comp(eq, ldc.i4 5, call GetValueOrDefault(ldloca v))
    auto comp = std::make_unique<Comp>(std::make_unique<LdcI4>(5), std::move(call),
                                      ComparisonKind::Equality);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1);
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 0);
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    // Left is still the ldc.i4 5.
    ASSERT_EQ(c->Left->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Left.get())->Value, 5);
    // Right is now `ldobj Nullable<int>(ldloca v)`.
    ASSERT_EQ(c->Right->Op, OpCode::LdObj);
    ASSERT_EQ(static_cast<LdObj*>(c->Right.get())->Target->Op, OpCode::LdLoca);
}

// RunCompNullableLift matches ldc.i8 too (a Nullable<long> comparison). MatchLdcI
// covers LdcI4 and LdcI8; the constant 42 is non-zero so the lift fires.
TEST(ExpressionTransforms, RunCompNullableLiftFoldsLdcI8Constant) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int64));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int64);
    call->AddArg(std::make_unique<LdLoca>(v));
    // comp(eq, call GetValueOrDefault(ldloca v), ldc.i8 42)
    auto comp = std::make_unique<Comp>(std::move(call), std::make_unique<LdcI8>(42),
                                      ComparisonKind::Equality);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "the ldc.i8 constant lifts too";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    auto* c = static_cast<Comp*>(st->Value.get());
    ASSERT_EQ(c->Left->Op, OpCode::LdObj);
    ASSERT_EQ(c->Right->Op, OpCode::LdcI8);
    EXPECT_EQ(static_cast<LdcI8*>(c->Right.get())->Value, 42);
}

// RunCompNullableLift rejects a zero constant: the C# only lifts a non-zero
// constant (`value != 0`), because `a.GetValueOrDefault() == 0` is NOT the same
// as a lifted `a == 0` (a null nullable has GetValueOrDefault == 0 but the lifted
// `a == 0` is false). The comp is not in a condition slot (it is a stloc value)
// and Left is a Call (not a Comp), so neither head rewrite fires and the comp
// stays non-lifted.
TEST(ExpressionTransforms, RunCompNullableLiftRejectsZeroConstant) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    // comp(ne, call GetValueOrDefault(ldloca v), ldc.i4 0) -- Inequality, not in a
    // condition slot, Left a Call (so the comp(!=0)=>x head rewrite does not fire).
    auto comp = std::make_unique<Comp>(std::move(call), std::make_unique<LdcI4>(0),
                                      ComparisonKind::Inequality);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0) << "a zero constant must not lift";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 1)
        << "the GetValueOrDefault call must stay";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_FALSE(c->IsLifted());
    ASSERT_EQ(c->Left->Op, OpCode::Call) << "the call must stay (no lift)";
}

// RunCompNullableLift only fires for equality/inequality (the C# `comp.Kind
// .IsEqualityOrInequality()` guard). A LessThan (or any relational) comparison is
// left alone.
TEST(ExpressionTransforms, RunCompNullableLiftRejectsRelationalKind) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    // comp(lt, call GetValueOrDefault(ldloca v), ldc.i4 5)
    auto comp = std::make_unique<Comp>(std::move(call), std::make_unique<LdcI4>(5),
                                      ComparisonKind::LessThan);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0) << "a relational kind must not lift";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    ASSERT_EQ(static_cast<Comp*>(st->Value.get())->Left->Op, OpCode::Call);
}

// RunCompNullableLift rejects a GetValueOrDefault whose declaring type is not
// Nullable<T> (MatchGetValueOrDefault returns false): the call stays and the
// comp is not lifted.
TEST(ExpressionTransforms, RunCompNullableLiftRejectsNonNullableDeclaringType) {
    auto v = MakeLocal("v");
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto call = std::make_unique<Call>("System.Int32::GetValueOrDefault");
    call->DeclaringType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    auto comp = std::make_unique<Comp>(std::move(call), std::make_unique<LdcI4>(5),
                                      ComparisonKind::Equality);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0);
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(static_cast<Comp*>(st->Value.get())->Left->Op, OpCode::Call);
}

// RunCompNullableLift rejects an already-lifted comp (the C# `!comp.IsLifted`
// guard): a C#-lifted comp is not re-lifted, and its operands are not rewritten.
TEST(ExpressionTransforms, RunCompNullableLiftRejectsAlreadyLiftedComp) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    // A C#-lifted comp (the 6-arg lifted constructor) with the call still as Left.
    auto comp = std::make_unique<Comp>(std::move(call), std::make_unique<LdcI4>(5),
                                      ComparisonKind::Equality,
                                      ComparisonLiftingKind::CSharp, StackType::I4);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLiftedComps(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "no new lift on an already-lifted comp";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 1)
        << "the call must stay (already-lifted comps are not re-lifted)";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    ASSERT_EQ(c->Left->Op, OpCode::Call) << "the operands must not be rewritten";
}

// RunCompNullableLift does not fire when the LiftNullables setting is off (the
// C# `context.Settings.LiftNullables` gate): the comp stays non-lifted with the
// call.
TEST(ExpressionTransforms, RunCompNullableLiftNoOpWhenLiftNullablesOff) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto call = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    call->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    call->AddArg(std::make_unique<LdLoca>(v));
    auto comp = std::make_unique<Comp>(std::move(call), std::make_unique<LdcI4>(5),
                                      ComparisonKind::Equality);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(comp)));
    fn->CheckInvariant(ILPhase::Normal);

    StatementTransform st;
    st.AddChild(std::make_unique<ExpressionTransforms>());
    ILTransformContext ctx;
    ctx.Settings.LiftNullables = false;
    st.Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0) << "LiftNullables=false gates the lift";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 1);
    auto* stLoc = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(static_cast<Comp*>(stLoc->Value.get())->Left->Op, OpCode::Call);
}

// RunIfNullableLift (NullableLiftingTransform.Run(IfInstruction) bool? equality
// fold): `v.GetValueOrDefault() ? v.HasValue : false` ==> `v == true` (a C#-lifted
// Comp `comp.lifted[C#](ldloc v, ldc.i4 1)`). The condition is the 1-arg
// GetValueOrDefault call on Nullable<bool>; the true arm is the HasValue call on
// the same variable; the false arm is ldc.i4 0. The if is a sub-expression value
// (wrapped in a stloc), so the fold is a clean ReplaceWith.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsGetValueOrDefaultHasValueFalse) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    // condition: call Nullable<bool>::GetValueOrDefault(ldloca v)
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    cond->AddArg(std::make_unique<LdLoca>(v));
    // true arm: call Nullable<bool>::get_HasValue(ldloca v)
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(hasVal),
                                               std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLiftedComps(*fn), 0);
    ASSERT_EQ(CountHasValueCall(*fn), 1);
    ASSERT_EQ(CountGetValueOrDefaultOneArg(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "the bool? fold must produce a lifted comp";
    EXPECT_EQ(CountHasValueCall(*fn), 0) << "the HasValue call must be folded away";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 0)
        << "the GetValueOrDefault call must be folded away";
    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp) << "the stloc now wraps the lifted comp";
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Equality) << "==> v == true";
    ASSERT_EQ(c->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Left.get())->Variable.get(), v.get());
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 1) << "==> v == true (1)";
}

// `v.GetValueOrDefault() ? false : v.HasValue` ==> `v == false`. The true arm is
// ldc.i4 0 and the false arm is the HasValue call; CanonicalizeLogicAndOr swaps
// the arms + negates the condition, then the logic.not unwrap in RunIfNullableLift
// re-swaps, so the `IsLdcI4(trueInst, 0) && MatchHasValueCall(falseInst, v)` branch
// fires and the fold produces `comp.lifted[C#](ldloc v, ldc.i4 0)`.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsFalseHasValue) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    cond->AddArg(std::make_unique<LdLoca>(v));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    // true arm = ldc.i4 0 (false), false arm = HasValue (the `v == false` shape).
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::make_unique<LdcI4>(0),
                                               std::move(hasVal));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1);
    EXPECT_EQ(CountHasValueCall(*fn), 0);
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Equality) << "==> v == false";
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 0) << "==> v == false (0)";
}

// `v.GetValueOrDefault() ? !v.HasValue : true` ==> `v != true`. The true arm is
// `logic.not(call get_HasValue(ldloca v))` (this port's comp(eq, HasValue, 0)
// shape) and the false arm is ldc.i4 1.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsNegatedHasValueTrue) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    cond->AddArg(std::make_unique<LdLoca>(v));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    // true arm = !v.HasValue (comp(eq, HasValue, 0)), false arm = ldc.i4 1.
    auto negHasVal = std::make_unique<Comp>(std::move(hasVal), std::make_unique<LdcI4>(0),
                                            ComparisonKind::Equality);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(negHasVal),
                                               std::make_unique<LdcI4>(1));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1);
    EXPECT_EQ(CountHasValueCall(*fn), 0);
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Inequality) << "==> v != true";
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 1) << "==> v != true (1)";
}

// `v.GetValueOrDefault() ? true : !v.HasValue` ==> `v != false`. The true arm is
// ldc.i4 1 and the false arm is `logic.not(call get_HasValue(ldloca v))`.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsTrueNegatedHasValue) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    cond->AddArg(std::make_unique<LdLoca>(v));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    // false arm = !v.HasValue (comp(eq, HasValue, 0)), true arm = ldc.i4 1.
    auto negHasVal = std::make_unique<Comp>(std::move(hasVal), std::make_unique<LdcI4>(0),
                                            ComparisonKind::Equality);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::make_unique<LdcI4>(1),
                                               std::move(negHasVal));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1);
    EXPECT_EQ(CountHasValueCall(*fn), 0);
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Inequality) << "==> v != false";
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 0) << "==> v != false (0)";
}

// RunIfNullableLift does not fire when the Nullable's underlying type is not
// Boolean (here Nullable<int>): the IsKnownType(underlying, Boolean) gate
// fails, so the if stays.
TEST(ExpressionTransforms, RunIfNullableLiftRejectsNonBooleanUnderlyingType) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    cond->AddArg(std::make_unique<LdLoca>(v));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(hasVal),
                                               std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0)
        << "a non-Boolean underlying type must not lift";
    // The HasValue call stays (the fold did not fire).
    EXPECT_EQ(CountHasValueCall(*fn), 1);
}

// RunIfNullableLift does not fire when the true arm is the HasValue call but the
// false arm is not ldc.i4 0 (here ldc.i4 2): none of the four branches match.
TEST(ExpressionTransforms, RunIfNullableLiftRejectsWrongConstantArm) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    cond->AddArg(std::make_unique<LdLoca>(v));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    // false arm = ldc.i4 2 (not 0 or 1) -- no branch matches.
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(hasVal),
                                               std::make_unique<LdcI4>(2));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0) << "a wrong constant arm must not lift";
}

// RunIfNullableLift does not fire when the condition is not a GetValueOrDefault
// call (here a bare ldloc v): MatchGetValueOrDefault fails, so the if stays.
TEST(ExpressionTransforms, RunIfNullableLiftRejectsNonGetValueOrDefaultCondition) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    // condition = ldloc v (not a GetValueOrDefault call).
    auto iff = std::make_unique<IfInstruction>(std::make_unique<LdLoc>(v),
                                               std::move(hasVal),
                                               std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0)
        << "a non-GetValueOrDefault condition must not lift";
}

// RunIfNullableLift does not fire when the HasValue call is on a different
// variable than the GetValueOrDefault call (v vs w): MatchHasValueCall(arm, v)
// checks the variable, so the fold bails.
TEST(ExpressionTransforms, RunIfNullableLiftRejectsHasValueOnDifferentVariable) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto w = MakeLocal("w", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    cond->AddArg(std::make_unique<LdLoca>(v));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(w));  // different variable w
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(hasVal),
                                               std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v, w});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0)
        << "a HasValue call on a different variable must not lift";
}

// RunIfNullableLift does not fire when the LiftNullables setting is off (the C#
// `context.Settings.LiftNullables` gate): the if stays.
TEST(ExpressionTransforms, RunIfNullableLiftNoOpWhenLiftNullablesOff) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    cond->AddArg(std::make_unique<LdLoca>(v));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(hasVal),
                                               std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    StatementTransform st;
    st.AddChild(std::make_unique<ExpressionTransforms>());
    ILTransformContext ctx;
    ctx.Settings.LiftNullables = false;
    st.Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0) << "LiftNullables=false gates the lift";
    EXPECT_EQ(CountHasValueCall(*fn), 1) << "the HasValue call stays";
}

// RunIfNullableLift as a block's FinalInstruction (a statement-if with value
// arms): the Comp (a value, not control flow) cannot be the final, so it becomes
// a non-terminal statement + a Branch to the next block replaces the if-final
// (the FoldMatchTrueFalse block-model adaptation).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsAsBlockFinal) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto cond = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    cond->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    cond->AddArg(std::make_unique<LdLoca>(v));
    auto hasVal = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hasVal->DeclaringType = MakeNullableOf(KnownTypeCode::Boolean);
    hasVal->AddArg(std::make_unique<LdLoca>(v));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(hasVal),
                                               std::make_unique<LdcI4>(0));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P2 = *fn->Body->Blocks[0];
    // The Comp became a non-terminal statement; the if-final is now a Branch to Q.
    ASSERT_EQ(P2.Instructions.size(), 1u);
    ASSERT_EQ(P2.Instructions[0]->Op, OpCode::Comp)
        << "the lifted comp must become a non-terminal statement";
    auto* c = static_cast<Comp*>(P2.Instructions[0].get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Equality) << "==> v == true";
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(P2.FinalInstruction.get())->TargetBlock,
              fn->Body->Blocks[1].get())
        << "the if-final must be replaced by a Branch to the next block";
}

// RunIfNullableLift AnalyzeCondition/LiftNormal early-out (NullableLiftingTransform.
// Run(IfInstruction) `Lift` method, the section before the bool? equality folds):
// `v.HasValue ? v : fallback => v ?? fallback`. The condition is a HasValue call on
// one Nullable<T> variable (AnalyzeCondition collects it); the true arm is `ldloc v`;
// the false arm is the fallback (a Nullable<T>). The fold produces a
// NullCoalescingInstruction(Nullable) whose ValueInst is the true arm (ldloc v) and
// FallbackInst is the false arm; UnderlyingResultType is the underlying type's
// StackType (I4 for Nullable<bool>). The if is a sub-expression value (wrapped in
// a stloc), so the fold is a clean ReplaceWith. The LiftNormal path runs before the
// bool? equality folds and the `&`/`|` on bool? section (the C# `Lift` order), so a
// `v.HasValue ? v : (bool?)false` folds to `v ?? (bool?)false` (the LiftNormal
// early-out), not to `v.HasValue & v` (the `&`/`|` fold).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsHasValueLdLocFallback) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto fallback = MakeLocal("fallback", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),                    // condition: v.HasValue
        std::make_unique<LdLoc>(v),            // true arm: ldloc v
        std::make_unique<LdLoc>(fallback));    // false arm: ldloc fallback
    auto fn = MakeFnWithBlock({result, v, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountNullCoalescing(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 1)
        << "the LiftNormal early-out must produce a NullCoalescing";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::NullCoalescingInstruction);
    auto* nc = static_cast<NullCoalescingInstruction*>(st->Value.get());
    EXPECT_EQ(nc->Kind, NullCoalescingKind::Nullable)
        << "the fold produces the Nullable kind";
    ASSERT_EQ(nc->ValueInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(nc->ValueInst.get())->Variable.get(), v.get())
        << "ValueInst is the true arm (ldloc v)";
    ASSERT_EQ(nc->FallbackInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(nc->FallbackInst.get())->Variable.get(),
              fallback.get())
        << "FallbackInst is the false arm (ldloc fallback)";
    EXPECT_EQ(nc->UnderlyingResultType, StackType::I4)
        << "UnderlyingResultType is the underlying Boolean's StackType (I4)";
}

// The LiftNormal early-out as a block's FinalInstruction (a statement-if with value
// arms): the NullCoalescingInstruction becomes a non-terminal statement and a
// Branch to the next block replaces the if-final (the block-model adaptation
// shared with the bool? equality fold and the `&`/`|` on bool? fold).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsHasValueLdLocFallbackAsBlockFinal) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto fallback = MakeLocal("fallback", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),
        std::make_unique<LdLoc>(v),
        std::make_unique<LdLoc>(fallback));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(fallback);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P2 = *fn->Body->Blocks[0];
    ASSERT_EQ(P2.Instructions.size(), 1u);
    ASSERT_EQ(P2.Instructions[0]->Op, OpCode::NullCoalescingInstruction)
        << "the NullCoalescing must become a non-terminal statement";
    auto* nc = static_cast<NullCoalescingInstruction*>(P2.Instructions[0].get());
    EXPECT_EQ(nc->Kind, NullCoalescingKind::Nullable);
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(P2.FinalInstruction.get())->TargetBlock,
              fn->Body->Blocks[1].get())
        << "the if-final must be replaced by a Branch to the next block";
}

// The LiftNormal early-out must not fire when AnalyzeCondition collects more than
// one nullable var (a 2-nullable BitAnd condition `(v1.HasValue & v2.HasValue)`):
// the `v.HasValue ? v : fallback => v ?? fallback` early-out requires
// nullableVars.Count == 1, and the DoLift path (which handles the multi-nullable
// case) is deferred, so the if stays as-is (matching the C# which returns null).
TEST(ExpressionTransforms, RunIfNullableLiftRejectsTwoNullableBitAndCondition) {
    auto v1 = MakeLocal("v1", MakeNullableOf(KnownTypeCode::Boolean));
    auto v2 = MakeLocal("v2", MakeNullableOf(KnownTypeCode::Boolean));
    auto fallback = MakeLocal("fallback", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    // condition: v1.HasValue & v2.HasValue (a BitAnd(I4) of two HasValue calls)
    auto bitand = std::make_unique<BinaryNumericInstruction>(
        MakeHasValueCall(v1), MakeHasValueCall(v2),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto iff = std::make_unique<IfInstruction>(
        std::move(bitand),
        std::make_unique<LdLoc>(v1),            // true arm: ldloc v1
        std::make_unique<LdLoc>(fallback));    // false arm: ldloc fallback
    auto fn = MakeFnWithBlock({result, v1, v2, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "a 2-nullable BitAnd must not fold (DoLift is deferred)";
    EXPECT_EQ(CountThreeValuedBool(*fn), 0)
        << "a 2-nullable BitAnd must not fold as `&`/`|` either";
}

// The LiftNormal early-out must not fire when the true arm is not `ldloc` of the
// nullable var (here ldloc of a different variable): the MatchLdLoc check fails
// (vLd != nullableVars[0]). AnalyzeCondition succeeded, so the if stays as-is
// (the DoLift / LiftCSharpUserComparison paths are deferred; matching the C#
// which returns null).
TEST(ExpressionTransforms, RunIfNullableLiftRejectsTrueArmNotLdLocOfNullableVar) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto other = MakeLocal("other", MakeNullableOf(KnownTypeCode::Boolean));
    auto fallback = MakeLocal("fallback", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),
        std::make_unique<LdLoc>(other),     // not ldloc v
        std::make_unique<LdLoc>(fallback));
    auto fn = MakeFnWithBlock({result, v, other, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "a true arm that is not ldloc of the nullable var must not fold";
}

// The LiftNormal early-out must not fire when the true arm is a NullableCtor (the
// `!MatchNullableCtor` branch is not taken; the DoLift path that handles the
// NullableCtor true arm is deferred), so the if stays as-is.
TEST(ExpressionTransforms, RunIfNullableLiftRejectsTrueArmNullableCtor) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto fallback = MakeLocal("fallback", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),
        MakeNullableBoolCtor(0),          // true arm: newobj Nullable<bool>(0)
        std::make_unique<LdLoc>(fallback));
    auto fn = MakeFnWithBlock({result, v, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "a NullableCtor true arm must not fold (the DoLift path is deferred)";
    EXPECT_EQ(CountThreeValuedBool(*fn), 0)
        << "a NullableCtor true arm must not fold as `&`/`|` either";
}

// RunIfNullableLift AnalyzeCondition/LiftNormal conv.nop.lifted case
// (NullableLiftingTransform.Run(IfInstruction) `Lift` method, the section after the
// `v.HasValue ? v : fallback` early-out): `v.HasValue ? v.GetValueOrDefault() :
// fallback => v ?? fallback`. The condition is a HasValue call on one Nullable<T>
// variable (AnalyzeCondition collects it); the true arm is a GetValueOrDefault call
// on that var; the false arm is the fallback. The fold produces a fresh `ldloc v`
// (or `conv.nop.lifted(ldloc v)` when the underlying type differs from the GVO's
// return type) wrapped in a NullCoalescingInstruction(NullableWithValueFallback).
// For Nullable<bool> the underlying Boolean != the I4-stacked Int32 the GVO
// returns, so a conv.nop.lifted (a no-op I4->I4 lifted conv) is inserted.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsHasValueGetValueOrDefaultWithConv) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto fallback = MakeLocal("fallback", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto gvo = MakeGVOCall(v);  // call GetValueOrDefault(ldloca v) on Nullable<bool>
    gvo->ReturnType = StackType::I4;  // GVO on Nullable<bool> returns bool (I4)
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),                 // condition: v.HasValue
        std::move(gvo),                      // true arm: call GetValueOrDefault(ldloca v)
        std::make_unique<LdLoc>(fallback));  // false arm: ldloc fallback (a bool)
    auto fn = MakeFnWithBlock({result, v, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountNullCoalescing(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 1)
        << "the conv.nop.lifted case must produce a NullCoalescing";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::NullCoalescingInstruction);
    auto* nc = static_cast<NullCoalescingInstruction*>(st->Value.get());
    EXPECT_EQ(nc->Kind, NullCoalescingKind::NullableWithValueFallback)
        << "the non-NullableCtor wrap is NullableWithValueFallback";
    EXPECT_EQ(nc->UnderlyingResultType, StackType::I4)
        << "UnderlyingResultType is the GVO call's ResultType (I4)";
    // ValueInst is conv.nop.lifted(ldloc v) -- a lifted Conv wrapping an LdLoc.
    ASSERT_EQ(nc->ValueInst->Op, OpCode::Conv)
        << "the Nullable<bool> fold inserts a conv.nop.lifted (Boolean != Int32)";
    auto* conv = static_cast<Conv*>(nc->ValueInst.get());
    EXPECT_TRUE(conv->IsLifted) << "the conv is a lifted (conv.nop.lifted) conv";
    EXPECT_EQ(conv->ResultType(), StackType::O)
        << "the lifted conv produces a boxed Nullable<bool> (ResultType O)";
    EXPECT_EQ(conv->UnderlyingResultType(), StackType::I4);
    ASSERT_EQ(conv->Argument->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(conv->Argument.get())->Variable.get(), v.get())
        << "the conv wraps a fresh ldloc v";
    ASSERT_EQ(nc->FallbackInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(nc->FallbackInst.get())->Variable.get(), fallback.get())
        << "FallbackInst is the false arm (ldloc fallback)";
}

// For Nullable<int> the underlying Int32 == the I4-stacked Int32 the GVO returns,
// so NO conv is inserted -- the fold produces `ldloc v ?? fallback` directly (a
// NullCoalescingInstruction(NullableWithValueFallback) whose ValueInst is the
// bare ldloc v, no Conv wrapper).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsHasValueGetValueOrDefaultNoConv) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto fallback = MakeLocal("fallback", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Int32));
    // call GetValueOrDefault(ldloca v) on Nullable<int> -- the declaring type must
    // be Nullable<int> (MakeGVOCall hardcodes Nullable<bool>, so build it inline).
    auto gvo = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    gvo->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    gvo->AddArg(std::make_unique<LdLoca>(v));
    gvo->ReturnType = StackType::I4;  // GVO on Nullable<int> returns int (I4)
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),                 // condition: v.HasValue (Nullable<int>)
        std::move(gvo),                      // true arm: call GetValueOrDefault(ldloca v)
        std::make_unique<LdLoc>(fallback));  // false arm: ldloc fallback (an int)
    auto fn = MakeFnWithBlock({result, v, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountNullCoalescing(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 1)
        << "the conv.nop.lifted case must produce a NullCoalescing (no-conv variant)";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::NullCoalescingInstruction);
    auto* nc = static_cast<NullCoalescingInstruction*>(st->Value.get());
    EXPECT_EQ(nc->Kind, NullCoalescingKind::NullableWithValueFallback);
    EXPECT_EQ(nc->UnderlyingResultType, StackType::I4);
    // ValueInst is the bare ldloc v (no Conv) -- Int32 == Int32, no conv inserted.
    ASSERT_EQ(nc->ValueInst->Op, OpCode::LdLoc)
        << "the Nullable<int> fold does NOT insert a conv (Int32 == Int32)";
    EXPECT_EQ(static_cast<LdLoc*>(nc->ValueInst.get())->Variable.get(), v.get())
        << "ValueInst is the fresh ldloc v";
    ASSERT_EQ(nc->FallbackInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(nc->FallbackInst.get())->Variable.get(), fallback.get());
}

// The conv.nop.lifted fold as a block's FinalInstruction (a statement-if with
// value arms): the NullCoalescingInstruction becomes a non-terminal statement and
// a Branch to the next block replaces the if-final (the block-model adaptation
// shared with the early-out and the `&`/`|` on bool? fold).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsHasValueGetValueOrDefaultAsBlockFinal) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto fallback = MakeLocal("fallback", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto gvo = MakeGVOCall(v);
    gvo->ReturnType = StackType::I4;
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),
        std::move(gvo),
        std::make_unique<LdLoc>(fallback));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(fallback);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P2 = *fn->Body->Blocks[0];
    ASSERT_EQ(P2.Instructions.size(), 1u);
    ASSERT_EQ(P2.Instructions[0]->Op, OpCode::NullCoalescingInstruction)
        << "the NullCoalescing must become a non-terminal statement";
    auto* nc = static_cast<NullCoalescingInstruction*>(P2.Instructions[0].get());
    EXPECT_EQ(nc->Kind, NullCoalescingKind::NullableWithValueFallback);
    EXPECT_EQ(nc->ValueInst->Op, OpCode::Conv)
        << "the Nullable<bool> block-final fold inserts a conv.nop.lifted";
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(P2.FinalInstruction.get())->TargetBlock,
              fn->Body->Blocks[1].get())
        << "the if-final must be replaced by a Branch to the next block";
}

// The conv.nop.lifted fold must not fire when the true arm is a GetValueOrDefault
// call on a DIFFERENT variable (not the single nullable var AnalyzeCondition
// collected) -- the match-against-v check fails.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsGetValueOrDefaultRejectsDifferentVariable) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto w = MakeLocal("w", MakeNullableOf(KnownTypeCode::Boolean));
    auto fallback = MakeLocal("fallback", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto gvo = MakeGVOCall(w);  // GVO on w, not v
    gvo->ReturnType = StackType::I4;
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),                 // condition: v.HasValue (collects v)
        std::move(gvo),                      // true arm: GVO on w (not v)
        std::make_unique<LdLoc>(fallback));
    auto fn = MakeFnWithBlock({result, v, w, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "a GVO on a different variable must not fold (match-against-v fails)";
}

// RunIfNullableLift AnalyzeCondition/LiftNormal DoLift path (the LiftNormal
// else-branch after the conv.nop.lifted case): `v.HasValue ? conv.i4(GVO(v)) :
// fallback` => `conv.lifted(ldloc v) ?? fallback`. The true arm is a Conv
// wrapping a GVO (not a bare GVO, so the conv.nop.lifted case does not fire);
// DoLift(Conv) recursively lifts the inner GVO to `ldloc v` and builds a lifted
// Conv. The wrap is NullableWithValueFallback (the non-NullableCtor case), so
// the result is a NullCoalescingInstruction(NullableWithValueFallback) whose
// ValueInst is the lifted Conv. The if is a sub-expression value (a stloc), so
// the fold is a clean ReplaceWith (no block-model adaptation).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsHasValueConvGetValueOrDefault) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto fallback = MakeLocal("fallback", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto gvo = MakeGVOCall(v);  // call GetValueOrDefault(ldloca v) on Nullable<int>
    gvo->ReturnType = StackType::I4;  // GVO on Nullable<int> returns int (I4)
    // true arm: conv.i4(GVO(v)) -- a Conv wrapping the GVO (not a bare GVO).
    auto trueArm = std::make_unique<Conv>(
        std::move(gvo), PrimitiveType::I4, false, Sign::Signed);
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),                 // condition: v.HasValue (collects v)
        std::move(trueArm),                  // true arm: conv.i4(GVO(v))
        std::make_unique<LdLoc>(fallback));  // false arm: ldloc fallback (an int)
    auto fn = MakeFnWithBlock({result, v, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountNullCoalescing(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 1)
        << "the DoLift path must produce a NullCoalescing";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::NullCoalescingInstruction);
    auto* nc = static_cast<NullCoalescingInstruction*>(st->Value.get());
    EXPECT_EQ(nc->Kind, NullCoalescingKind::NullableWithValueFallback)
        << "the non-NullableCtor wrap is NullableWithValueFallback";
    // ValueInst is conv.lifted(ldloc v) -- a lifted Conv wrapping an LdLoc.
    ASSERT_EQ(nc->ValueInst->Op, OpCode::Conv)
        << "the DoLift(Conv) fold produces a lifted Conv";
    auto* conv = static_cast<Conv*>(nc->ValueInst.get());
    EXPECT_TRUE(conv->IsLifted) << "the conv is a lifted conv";
    EXPECT_EQ(conv->ResultType(), StackType::O)
        << "the lifted conv produces a boxed Nullable<int> (ResultType O)";
    ASSERT_EQ(conv->Argument->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(conv->Argument.get())->Variable.get(), v.get())
        << "the lifted conv wraps a fresh ldloc v";
    ASSERT_EQ(nc->FallbackInst->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(nc->FallbackInst.get())->Variable.get(), fallback.get())
        << "FallbackInst is the false arm (ldloc fallback)";
}

// RunIfNullableLift DoLift path with a BinaryNumericInstruction true arm:
// `v.HasValue ? binary.add(GVO(v), ldc.i4 5) : fallback` =>
// `binary.add.lifted(ldloc v, ldc.i4 5) ?? fallback`. The pure non-nullable
// constant (ldc.i4 5) is embedded (NewNullable returns it unchanged for the
// UnknownType expected type), so the lifted BNI's right operand is the bare
// constant.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsHasValueBinaryNumericGetValueOrDefault) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto fallback = MakeLocal("fallback", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto gvo = MakeGVOCall(v);
    gvo->ReturnType = StackType::I4;
    auto trueArm = std::make_unique<BinaryNumericInstruction>(
        std::move(gvo), std::make_unique<LdcI4>(5),
        BinaryNumericOperator::Add, StackType::I4);
    auto iff = std::make_unique<IfInstruction>(
        MakeHasValueCall(v),
        std::move(trueArm),
        std::make_unique<LdLoc>(fallback));
    auto fn = MakeFnWithBlock({result, v, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountNullCoalescing(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 1)
        << "the DoLift(BinaryNumeric) path must produce a NullCoalescing";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::NullCoalescingInstruction);
    auto* nc = static_cast<NullCoalescingInstruction*>(st->Value.get());
    ASSERT_EQ(nc->ValueInst->Op, OpCode::BinaryNumericInstruction);
    auto* bni = static_cast<BinaryNumericInstruction*>(nc->ValueInst.get());
    EXPECT_TRUE(bni->IsLifted) << "the binary is a lifted binary";
    EXPECT_EQ(bni->ResultType(), StackType::O);
    ASSERT_EQ(bni->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(bni->Left.get())->Variable.get(), v.get());
    ASSERT_EQ(bni->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(bni->Right.get())->Value, 5);
}

// RunIfNullableLift DoLift path does not fire when AnalyzeCondition collects more
// than one nullable var but the true arm's DoLift only involves one of them
// (bits.All fails -- a nullableVar did not contribute). The if stays as-is.
TEST(ExpressionTransforms, RunIfNullableLiftDoLiftRejectsWhenAVarDoesNotContribute) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto w = MakeLocal("w", MakeNullableOf(KnownTypeCode::Int32));
    auto fallback = MakeLocal("fallback", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto gvo = MakeGVOCall(v);  // GVO on v, not w
    gvo->ReturnType = StackType::I4;
    auto trueArm = std::make_unique<Conv>(
        std::move(gvo), PrimitiveType::I4, false, Sign::Signed);
    // condition: v.HasValue && w.HasValue (collects both v and w)
    auto cond = std::make_unique<BinaryNumericInstruction>(
        MakeHasValueCall(v), MakeHasValueCall(w),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto iff = std::make_unique<IfInstruction>(
        std::move(cond), std::move(trueArm),
        std::make_unique<LdLoc>(fallback));
    auto fn = MakeFnWithBlock({result, v, w, fallback});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountNullCoalescing(*fn), 0)
        << "w did not contribute to the lift; the if stays as-is";
}

// RunIfNullableLift `&`/`|` on bool? fold (NullableLiftingTransform.Run(IfInstruction)
// `Lift` method, the section after the bool? equality folds): `condition ? v :
// (bool?)false` ==> `3vl.bool.and(condition, v)` (the D95 node). The condition is a
// plain bool (ldloc b); the true arm is ldloc v (Nullable<bool>); the false arm
// is `newobj Nullable<bool>(ldc.i4 0)`. The if is a sub-expression value (wrapped
// in a stloc), so the fold is a clean ReplaceWith.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsAndBoolBoolNullable) {
    auto b = MakeLocal("b", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(b),               // condition: a plain bool
        std::make_unique<LdLoc>(v),               // true arm: ldloc v (Nullable<bool>)
        MakeNullableBoolCtor(0));                 // false arm: (bool?)false
    auto fn = MakeFnWithBlock({result, v, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountThreeValuedBool(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountThreeValuedBool(*fn), 1) << "the & fold must produce a 3vl.bool.and";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::ThreeValuedBoolAnd);
    auto* andNode = static_cast<ThreeValuedBoolAnd*>(st->Value.get());
    ASSERT_EQ(andNode->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(andNode->Left.get())->Variable.get(), b.get())
        << "Left is the condition (ldloc b)";
    ASSERT_EQ(andNode->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(andNode->Right.get())->Variable.get(), v.get())
        << "Right is the true arm (ldloc v)";
}

// `condition ? (bool?)true : v` ==> `3vl.bool.or(condition, v)`. The true arm is
// `newobj Nullable<bool>(ldc.i4 1)` and the false arm is ldloc v. The `|` fold
// fires via the `else if (MatchLdLoc(falseInst, v))` branch (the true arm is not
// an LdLoc).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsOrBoolBoolNullable) {
    auto b = MakeLocal("b", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(b),               // condition: a plain bool
        MakeNullableBoolCtor(1),                 // true arm: (bool?)true
        std::make_unique<LdLoc>(v));              // false arm: ldloc v
    auto fn = MakeFnWithBlock({result, v, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountThreeValuedBool(*fn), 1) << "the | fold must produce a 3vl.bool.or";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::ThreeValuedBoolOr);
    auto* orNode = static_cast<ThreeValuedBoolOr*>(st->Value.get());
    ASSERT_EQ(orNode->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(orNode->Left.get())->Variable.get(), b.get())
        << "Left is the condition (ldloc b)";
    ASSERT_EQ(orNode->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(orNode->Right.get())->Variable.get(), v.get())
        << "Right is the false arm (ldloc v)";
}

// The two-nullable `|` on bool? fold: `(n1.GVO || (!n2.GVO && !n1.HV)) ? n1 :
// n2` ==> `3vl.bool.or(n1, n2)`. The condition is a logic.or (if (n1.GVO) 1 else
// logic.and) whose logic.and is `if (!n2.GVO) !n1.HV else 0`; the arms are ldloc
// n1 / ldloc n2 (v==n1 && v2==n2).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsOrTwoNullables) {
    auto n1 = MakeLocal("n1", MakeNullableOf(KnownTypeCode::Boolean));
    auto n2 = MakeLocal("n2", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto logicAnd = std::make_unique<IfInstruction>(
        MakeLogicNot(MakeGVOCall(n2)),                 // !n2.GetValueOrDefault()
        MakeLogicNot(MakeHasValueCall(n1)),           // !n1.HasValue
        std::make_unique<LdcI4>(0));
    auto logicOr = std::make_unique<IfInstruction>(
        MakeGVOCall(n1),                              // n1.GetValueOrDefault()
        std::make_unique<LdcI4>(1),
        std::move(logicAnd));
    auto iff = std::make_unique<IfInstruction>(
        std::move(logicOr),
        std::make_unique<LdLoc>(n1),
        std::make_unique<LdLoc>(n2));
    auto fn = MakeFnWithBlock({result, n1, n2});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountThreeValuedBool(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountThreeValuedBool(*fn), 1) << "the two-nullable | fold must fire";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::ThreeValuedBoolOr);
    auto* orNode = static_cast<ThreeValuedBoolOr*>(st->Value.get());
    ASSERT_EQ(orNode->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(orNode->Left.get())->Variable.get(), n1.get())
        << "Left is the true arm (ldloc n1)";
    ASSERT_EQ(orNode->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(orNode->Right.get())->Variable.get(), n2.get())
        << "Right is the false arm (ldloc n2)";
}

// The two-nullable `&` on bool? fold (the swapped shape): the same condition but
// the arms are ldloc n2 / ldloc n1 (v==n2 && v2==n1) ==> `3vl.bool.and(n1, n2)`
// (ThreeValuedBoolAnd(falseInst=ldloc n1, trueInst=ldloc n2)).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsAndTwoNullables) {
    auto n1 = MakeLocal("n1", MakeNullableOf(KnownTypeCode::Boolean));
    auto n2 = MakeLocal("n2", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto logicAnd = std::make_unique<IfInstruction>(
        MakeLogicNot(MakeGVOCall(n2)),
        MakeLogicNot(MakeHasValueCall(n1)),
        std::make_unique<LdcI4>(0));
    auto logicOr = std::make_unique<IfInstruction>(
        MakeGVOCall(n1), std::make_unique<LdcI4>(1), std::move(logicAnd));
    // The swapped shape: true arm = n2, false arm = n1.
    auto iff = std::make_unique<IfInstruction>(
        std::move(logicOr),
        std::make_unique<LdLoc>(n2),
        std::make_unique<LdLoc>(n1));
    auto fn = MakeFnWithBlock({result, n1, n2});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountThreeValuedBool(*fn), 1) << "the two-nullable & fold must fire";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::ThreeValuedBoolAnd);
    auto* andNode = static_cast<ThreeValuedBoolAnd*>(st->Value.get());
    ASSERT_EQ(andNode->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(andNode->Left.get())->Variable.get(), n1.get())
        << "Left is the false arm (ldloc n1)";
    ASSERT_EQ(andNode->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(andNode->Right.get())->Variable.get(), n2.get())
        << "Right is the true arm (ldloc n2)";
}

// The `&` fold must not fire when the Nullable ctor's underlying type is not
// Boolean (here Nullable<int>): the IsKnownType(utype, Boolean) gate fails.
TEST(ExpressionTransforms, RunIfNullableLiftAndBoolBoolNullableRejectsNonBooleanUnderlying) {
    auto b = MakeLocal("b", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto ctor = std::make_unique<Call>("System.Nullable`1::.ctor");
    ctor->IsNewObj = true;
    ctor->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);  // Nullable<int>
    ctor->AddArg(std::make_unique<LdcI4>(0));
    auto iff = std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(b), std::make_unique<LdLoc>(v), std::move(ctor));
    auto fn = MakeFnWithBlock({result, v, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountThreeValuedBool(*fn), 0)
        << "a non-Boolean underlying type must not fold";
}

// The `&` fold must not fire when the Nullable ctor's argument is 1 (not the 0
// the `(bool?)false` arm requires). The `|` fold is not reached (the true arm is
// an LdLoc, so the `if (MatchLdLoc(trueInst, v))` branch is taken).
TEST(ExpressionTransforms, RunIfNullableLiftAndBoolBoolNullableRejectsNonZeroCtorArg) {
    auto b = MakeLocal("b", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(b), std::make_unique<LdLoc>(v), MakeNullableBoolCtor(1));
    auto fn = MakeFnWithBlock({result, v, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountThreeValuedBool(*fn), 0)
        << "a non-zero ctor arg must not fold as `&`";
}

// The two-nullable fold must not fire when the condition is not the
// three-valued logic.or pattern (here a plain bool): MatchThreeValuedLogic-
// ConditionPattern's MatchLogicOr fails.
TEST(ExpressionTransforms, RunIfNullableLiftTwoNullablesRejectsNonMatchingCondition) {
    auto b = MakeLocal("b", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto n1 = MakeLocal("n1", MakeNullableOf(KnownTypeCode::Boolean));
    auto n2 = MakeLocal("n2", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(b),               // not the logic.or pattern
        std::make_unique<LdLoc>(n1),
        std::make_unique<LdLoc>(n2));
    auto fn = MakeFnWithBlock({result, n1, n2, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountThreeValuedBool(*fn), 0)
        << "a non-matching condition must not fold";
}

// The two-nullable fold must not fire when the arms' variables do not match the
// condition's nullable1/nullable2 (here n3/n4): neither (v==n1 && v2==n2) nor
// (v==n2 && v2==n1) holds.
TEST(ExpressionTransforms, RunIfNullableLiftTwoNullablesRejectsMismatchedVars) {
    auto n1 = MakeLocal("n1", MakeNullableOf(KnownTypeCode::Boolean));
    auto n2 = MakeLocal("n2", MakeNullableOf(KnownTypeCode::Boolean));
    auto n3 = MakeLocal("n3", MakeNullableOf(KnownTypeCode::Boolean));
    auto n4 = MakeLocal("n4", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", MakeNullableOf(KnownTypeCode::Boolean));
    auto logicAnd = std::make_unique<IfInstruction>(
        MakeLogicNot(MakeGVOCall(n2)),
        MakeLogicNot(MakeHasValueCall(n1)),
        std::make_unique<LdcI4>(0));
    auto logicOr = std::make_unique<IfInstruction>(
        MakeGVOCall(n1), std::make_unique<LdcI4>(1), std::move(logicAnd));
    auto iff = std::make_unique<IfInstruction>(
        std::move(logicOr),
        std::make_unique<LdLoc>(n3),               // neither n1 nor n2
        std::make_unique<LdLoc>(n4));               // neither n1 nor n2
    auto fn = MakeFnWithBlock({result, n1, n2, n3, n4});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountThreeValuedBool(*fn), 0)
        << "mismatched arm variables must not fold";
}

// The `&` fold as a block's FinalInstruction (a statement-if with value arms):
// the ThreeValuedBoolAnd becomes a non-terminal statement and a Branch to the
// next block replaces the if-final (the block-model adaptation shared with the
// bool? equality fold).
TEST(ExpressionTransforms, RunIfNullableLiftAndBoolBoolNullableFoldsAsBlockFinal) {
    auto b = MakeLocal("b", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto iff = std::make_unique<IfInstruction>(
        std::make_unique<LdLoc>(b), std::make_unique<LdLoc>(v), MakeNullableBoolCtor(0));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto P = std::make_unique<Block>();
    P->SetFinal(std::move(iff));
    fn->Body->AddBlock(std::move(P));
    auto Q = std::make_unique<Block>();
    Q->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(Q));
    fn->Variables.push_back(v);
    fn->Variables.push_back(b);
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& P2 = *fn->Body->Blocks[0];
    ASSERT_EQ(P2.Instructions.size(), 1u);
    ASSERT_EQ(P2.Instructions[0]->Op, OpCode::ThreeValuedBoolAnd)
        << "the 3vl.bool.and must become a non-terminal statement";
    auto* andNode = static_cast<ThreeValuedBoolAnd*>(P2.Instructions[0].get());
    ASSERT_EQ(andNode->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(andNode->Left.get())->Variable.get(), b.get());
    ASSERT_EQ(andNode->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(andNode->Right.get())->Variable.get(), v.get());
    ASSERT_EQ(P2.FinalInstruction->Op, OpCode::Branch);
    EXPECT_EQ(static_cast<Branch*>(P2.FinalInstruction.get())->TargetBlock,
              fn->Body->Blocks[1].get())
        << "the if-final must be replaced by a Branch to the next block";
}


// On the real mscorlib corpus, running the full pre-pipeline through the
// StatementTransform{ILInlining, ExpressionTransforms} (the GetILTransforms()
// position) preserves the ILAst invariant and the HandleConditionalOperator
// ternary fold makes corpus progress (the StLoc-wrapping-IfInstruction count
// rises as if/else pairs over the same temp fold to conditional operators).
TEST(ExpressionTransforms, MscorlibSweepPreservesInvariant) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int totalFolds = 0;
    int totalArrayIndexConvDrops = 0;
    int totalNullCoalescingFolds = 0;
    int totalLiftedCompFolds = 0;
    int totalCatchVarPromotions = 0;
    int totalDecimalFieldFolds = 0;
    int totalLdVirtDelegateFolds = 0;
    ILTransformContext ctx;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        RunPrePipeline(*fn, ctx);
        int before = CountConditionalOperators(*fn);
        int boxesBefore = CountBoxes(*fn);
        int arrayIdxConvBefore = CountArrayIndexConvI(*fn);
        int convRUnBefore = CountConvRUnNested(*fn);
        int maskedShiftsBefore = CountMaskedShifts(*fn);
        int getValOrDefaultBefore = CountGetValueOrDefaultTwoArg(*fn);
        int nullCoalescingBefore = CountNullCoalescing(*fn);
        int liftedCompsBefore = CountLiftedComps(*fn);
        int gvo1Before = CountGetValueOrDefaultOneArg(*fn);
        int hasValueBefore = CountHasValueCall(*fn);
        int threeValuedBoolBefore = CountThreeValuedBool(*fn);
        int exceptionLocalBefore = CountExceptionLocalHandlers(*fn);
        int ldcDecimalBefore = CountLdcDecimal(*fn);
        int decimalFieldLoadsBefore = CountDecimalConstantFieldLoads(*fn);
        int ldVirtDelegateBefore = CountLdVirtDelegate(*fn);
        int newObjVirtDelegateBefore = CountNewObjVirtDelegate(*fn);
        {
            StatementTransform st;
            st.AddChild(std::make_unique<ILInlining>());
            st.AddChild(std::make_unique<ExpressionTransforms>());
            st.Run(*fn, ctx);
        }
        fn->CheckInvariant(ILPhase::Normal);
        int after = CountConditionalOperators(*fn);
        int boxesAfter = CountBoxes(*fn);
        int arrayIdxConvAfter = CountArrayIndexConvI(*fn);
        int convRUnAfter = CountConvRUnNested(*fn);
        int maskedShiftsAfter = CountMaskedShifts(*fn);
        int getValOrDefaultAfter = CountGetValueOrDefaultTwoArg(*fn);
        int nullCoalescingAfter = CountNullCoalescing(*fn);
        int liftedCompsAfter = CountLiftedComps(*fn);
        int gvo1After = CountGetValueOrDefaultOneArg(*fn);
        int hasValueAfter = CountHasValueCall(*fn);
        int threeValuedBoolAfter = CountThreeValuedBool(*fn);
        int exceptionLocalAfter = CountExceptionLocalHandlers(*fn);
        int ldcDecimalAfter = CountLdcDecimal(*fn);
        int decimalFieldLoadsAfter = CountDecimalConstantFieldLoads(*fn);
        int ldVirtDelegateAfter = CountLdVirtDelegate(*fn);
        int newObjVirtDelegateAfter = CountNewObjVirtDelegate(*fn);
        // The ternary fold is monotone non-decreasing (each fold creates a
        // StLoc-if; nothing in this subset removes one). The VisitComp rewrites
        // (comp(!=0)->x, logic.not push, unsigned normalization) do not create
        // StLoc-if, so the count must not drop.
        EXPECT_GE(after, before);
        // The VisitBox fold (`box ref-type(arg)` -> `arg`) is monotone
        // non-increasing (each fold removes a Box; nothing in this subset
        // creates one). The fold fires on the legacy-csc corpus (the sweep counts
        // ~40 box-of-reference-type folds across 8000 methods -- a real-corpus
        // ILAst-cleaning transform, not faithfulness-only), but the count is not
        // asserted (the pre-pipeline's unordered-container iteration makes the
        // exact total vary slightly); the per-method monotone-non-increasing
        // invariant is the deterministic correctness gate.
        EXPECT_LE(boxesAfter, boxesBefore);
        // The CleanUpArrayIndices fold (`conv.i` widening of an array index ->
        // the bare index) is monotone non-increasing (each fold removes a
        // ResultType==I conv from a LdElema/NewArr Indices; nothing in this
        // subset creates one). The fold fires on the legacy-csc corpus when the
        // compiler emits `conv.i` to widen an I4 array index to native int before
        // ldelema/newarr; the per-method monotone-non-increasing invariant is the
        // deterministic correctness gate (the absolute count is not asserted).
        EXPECT_LE(arrayIdxConvAfter, arrayIdxConvBefore);
        // The VisitConv conv.r.un combining fold (`conv.r4(conv.r.un(x))` /
        // `conv.r8(conv.r.un(x))` -> a single `conv.r4.un` / `conv.r8.un`) is
        // monotone non-increasing (each fold removes one nested conv.r.un; nothing
        // in this subset creates one). The fold fires on the legacy-csc corpus when
        // the compiler emits `conv.r.un` followed by an explicit `conv.r4`/`conv.r8`
        // (a Roslyn-era codegen pattern); the per-method monotone-non-increasing
        // invariant is the deterministic correctness gate (the absolute count is
        // not asserted -- the .NET Framework 4 legacy-csc corpus may emit
        // conv.r.un rarely, so the fold may fire 0 times on it).
        EXPECT_LE(convRUnAfter, convRUnBefore);
        // The VisitBinaryNumericInstruction shift-size fold (`a << (b & 31)` /
        // `a >> (b & 63)` -> `a << b` / `a >> b`) is monotone non-increasing (each
        // fold removes one masked-shift; nothing in this subset creates one). The
        // fold fires on the legacy-csc corpus when the compiler emits an explicit
        // `& 31`/`& 63` mask on a shift count (a Roslyn-era codegen pattern); the
        // per-method monotone-non-increasing invariant is the deterministic
        // correctness gate (the absolute count is not asserted -- the .NET
        // Framework 4 legacy-csc corpus may emit the mask rarely).
        EXPECT_LE(maskedShiftsAfter, maskedShiftsBefore);
        // The VisitCall `Nullable<T>.GetValueOrDefault(a, b) -> a ?? b` fold is
        // monotone non-increasing for the 2-arg GetValueOrDefault-call count
        // (each fold removes one such call; nothing in this subset creates one)
        // and monotone non-decreasing for the NullCoalescingInstruction count
        // (each fold creates one; nothing in this subset removes one). The fold
        // fires when the compiler emits the 2-arg GetValueOrDefault for a
        // `Nullable<T> ?? T` expression; whether the .NET Framework 4 legacy-csc
        // corpus contains any is corpus-dependent, so the per-method monotone
        // invariant is the deterministic correctness gate (the absolute count is
        // not asserted -- the fold may fire 0 times on this corpus).
        EXPECT_LE(getValOrDefaultAfter, getValOrDefaultBefore);
        EXPECT_GE(nullCoalescingAfter, nullCoalescingBefore);
        // The RunCompNullableLift fold (`a.GetValueOrDefault() == const` (const
        // != 0) -> `comp.lifted[C#](a == const)`) is monotone non-decreasing for
        // the C#-lifted Comp count (each fold marks one comp lifted; nothing in
        // this subset un-lifts one) and monotone non-increasing for the 1-arg
        // GetValueOrDefault-call count (each fold removes one such call from a
        // comp; nothing in this subset creates one). The fold is the
        // VS2022.10/Roslyn 4.10 optimization, which the .NET Framework 4
        // legacy-csc corpus does not emit, so the fold may fire 0 times on it;
        // the per-method monotone invariant is the deterministic correctness gate.
        EXPECT_GE(liftedCompsAfter, liftedCompsBefore);
        EXPECT_LE(gvo1After, gvo1Before);
        // The RunIfNullableLift bool? equality fold (`v.GetValueOrDefault() ?
        // v.HasValue : false` -> `comp.lifted[C#](v == true)`, and the three
        // sibling folds) is monotone non-decreasing for the C#-lifted Comp count
        // (already asserted above -- both RunCompNullableLift and RunIfNullableLift
        // produce lifted Comps) and monotone non-increasing for the 1-arg
        // get_HasValue call count (each bool? fold removes one HasValue call from
        // an arm; nothing in this subset creates one). The bool? fold is a Roslyn-
        // era codegen pattern for `bool?` comparisons, which the .NET Framework 4
        // legacy-csc corpus may emit rarely; the per-method monotone invariant is
        // the deterministic correctness gate (the absolute count is not asserted).
        EXPECT_LE(hasValueAfter, hasValueBefore);
        // The RunIfNullableLift `&`/`|` on bool? fold (the section of Lift after the
        // bool? equality folds) is monotone non-decreasing for the ThreeValuedBool
        // node count (each fold creates one ThreeValuedBoolAnd/Or; nothing in this
        // subset removes one). The `&`/`|` on bool? codegen is a Roslyn-era pattern
        // which the .NET Framework 4 legacy-csc corpus may emit rarely; the per-
        // method monotone-non-decreasing invariant is the deterministic correctness
        // gate (the absolute count is not asserted).
        EXPECT_GE(threeValuedBoolAfter, threeValuedBoolBefore);
        // TransformCatchVariable promotes a catch-local copy `stloc v(ldloc E)` to
        // the catch variable (Kind=ExceptionLocal), so the ExceptionLocal-handler
        // count is monotone non-decreasing (each fold promotes one handler's
        // variable; nothing in this subset demotes one). csc emits the copy for
        // every catch whose variable is used and not already inlined, so the fold
        // fires on the legacy-csc corpus (a real-corpus high-frequency transform,
        // unlike many earlier faithfulness-only ones).
        EXPECT_GE(exceptionLocalAfter, exceptionLocalBefore);
        // TransformDecimalFieldToConstant folds a `ldobj(ldsflda
        // System.Decimal::One/Zero/MinusOne)` static-field load into the
        // corresponding LdcDecimal constant. The LdcDecimal count is monotone
        // non-decreasing (each fold creates one; nothing in this subset removes
        // one) and the Decimal-constant-field-load count is monotone
        // non-increasing (each fold removes one; nothing in this subset creates
        // one). The fold fires on the legacy-csc corpus when a method references
        // the Decimal.One/Zero/MinusOne named-constant fields; the per-method
        // monotone invariant is the deterministic correctness gate.
        EXPECT_GE(ldcDecimalAfter, ldcDecimalBefore);
        EXPECT_LE(decimalFieldLoadsAfter, decimalFieldLoadsBefore);
        // TransformDelegateCtorLdVirtFtnToLdVirtDelegate folds a virtual delegate
        // construction `newobj DelegateType(target, ldvirtftn M(target))` into an
        // LdVirtDelegate. The LdVirtDelegate count is monotone non-decreasing
        // (each fold creates one; nothing in this subset removes one) and the
        // newobj-virtual-delegate count is monotone non-increasing (each fold
        // removes one; nothing in this subset creates one). The fold fires on the
        // legacy-csc corpus when a method constructs a delegate from a virtual
        // method (e.g. an event handler `new EventHandler(this.OnClick)`); the
        // per-method monotone invariant is the deterministic correctness gate
        // (the absolute count is not asserted -- it is corpus-dependent).
        EXPECT_GE(ldVirtDelegateAfter, ldVirtDelegateBefore);
        EXPECT_LE(newObjVirtDelegateAfter, newObjVirtDelegateBefore);
        totalFolds += (after - before);
        totalArrayIndexConvDrops += (arrayIdxConvBefore - arrayIdxConvAfter);
        totalNullCoalescingFolds += (nullCoalescingAfter - nullCoalescingBefore);
        totalLiftedCompFolds += (liftedCompsAfter - liftedCompsBefore);
        totalCatchVarPromotions += (exceptionLocalAfter - exceptionLocalBefore);
        totalDecimalFieldFolds += (ldcDecimalAfter - ldcDecimalBefore);
        totalLdVirtDelegateFolds += (ldVirtDelegateAfter - ldVirtDelegateBefore);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    // The ternary fold fires on the legacy-csc corpus (csc emits `V = a ? b : c`
    // as if/else stloc to the same temp, which ConditionDetection shapes into the
    // two-expression-Block-arms form HandleConditionalOperator matches).
    EXPECT_GT(totalFolds, 0)
        << "HandleConditionalOperator must fold some ternary on mscorlib";
    // The CleanUpArrayIndices fold fires on the legacy-csc corpus when a method
    // indexes an array with a conv.i-widened index; if it fires at all, the drop
    // count must be positive (a regression that made the fold stop firing would
    // surface as zero). The exact count is not asserted (corpus-dependent on how
    // many methods widen an index before ldelema).
    (void)totalArrayIndexConvDrops;
    // The VisitCall `Nullable<T>.GetValueOrDefault(a, b) -> a ?? b` fold fires
    // when the compiler emits the 2-arg form for a `Nullable<T> ?? T`
    // expression; whether the legacy-csc corpus contains any is corpus-dependent
    // (System.Nullable<T> IS in mscorlib, so `a ?? b` over a nullable may occur).
    // The count is reported (not asserted) -- a non-zero total is informative.
    (void)totalNullCoalescingFolds;
    // The RunCompNullableLift fold is the VS2022.10/Roslyn 4.10 optimization,
    // which the .NET Framework 4 legacy-csc corpus does not emit; the count is
    // reported (not asserted) -- a non-zero total is informative (it fires on
    // Roslyn-compiled / modern .NET).
    (void)totalLiftedCompFolds;
    // TransformCatchVariable fires on the legacy-csc corpus: csc emits
    // `catch T; stloc V_0(ldloc E)` for every catch whose variable is used (and
    // not already inlined by ILInlining), so the fold promotes thousands of
    // catch-local copies across the corpus. A zero total would mean the
    // transform stopped firing -- a regression.
    EXPECT_GT(totalCatchVarPromotions, 0)
        << "TransformCatchVariable must promote catch copies on mscorlib";
    // TransformDecimalFieldToConstant fires on the legacy-csc corpus: mscorlib
    // references the System.Decimal.One/Zero/MinusOne named-constant fields
    // (e.g. Decimal.op_Multiply by One, Decimal.Equals(Zero)), so a non-zero
    // total confirms the fold makes real-corpus progress (a real-corpus
    // readability improvement, unlike most recent nullable-family
    // faithfulness-only pieces). A zero total would mean the fold stopped firing.
    EXPECT_GT(totalDecimalFieldFolds, 0)
        << "TransformDecimalFieldToConstant must fold Decimal field loads on mscorlib";
    // TransformDelegateCtorLdVirtFtnToLdVirtDelegate fires on the legacy-csc corpus
    // when a method constructs a delegate from a virtual method (e.g. an event
    // handler `new EventHandler(this.OnClick)` where OnClick is virtual). The
    // count is reported (not asserted) -- whether the legacy-csc corpus contains
    // any virtual delegate constructions in the 8000-method sweep is
    // corpus-dependent (a non-zero total is informative; a zero total means the
    // fold is faithfulness-only on this corpus, firing on Roslyn-compiled /
    // modern .NET). The per-method monotone invariant (asserted above) is the
    // deterministic correctness gate regardless.
    (void)totalLdVirtDelegateFolds;
}

// RunIfNullableLift MatchCompOrDecimal equality case (LiftCSharpEqualityComparison
// hasValueComp two-nullable case): `comp(eq, GVO(v1), GVO(v2)) ?
// comp(eq, HV(v1), HV(v2)) : false` ==> the C#-lifted
// `comp.lifted[C#](eq, ldloc v1, ldloc v2)`. Both HasValue comparisons compare
// equal, both GVOs lift to ldloc, both pure. A Roslyn-era codegen pattern.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsCLiftedEqualityComparison) {
    auto v1 = MakeLocal("v1", MakeNullableOf(KnownTypeCode::Int32));
    auto v2 = MakeLocal("v2", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    // condition: comp(eq, GVO(v1), GVO(v2))
    auto gvo1 = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    gvo1->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    gvo1->ReturnType = StackType::I4;
    gvo1->AddArg(std::make_unique<LdLoca>(v1));
    auto gvo2 = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    gvo2->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    gvo2->ReturnType = StackType::I4;
    gvo2->AddArg(std::make_unique<LdLoca>(v2));
    auto cond = std::make_unique<Comp>(std::move(gvo1), std::move(gvo2),
                                       ComparisonKind::Equality, false);
    // true arm: comp(eq, HV(v1), HV(v2))
    auto hv1 = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hv1->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    hv1->AddArg(std::make_unique<LdLoca>(v1));
    auto hv2 = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hv2->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    hv2->AddArg(std::make_unique<LdLoca>(v2));
    auto trueArm = std::make_unique<Comp>(std::move(hv1), std::move(hv2),
                                            ComparisonKind::Equality, false);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(trueArm),
                                                std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v1, v2});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLiftedComps(*fn), 0);
    ASSERT_EQ(CountHasValueCall(*fn), 2);
    ASSERT_EQ(CountGetValueOrDefaultOneArg(*fn), 2);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "the equality fold must produce a lifted comp";
    EXPECT_EQ(CountHasValueCall(*fn), 0) << "both HasValue calls must be folded away";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 0) << "both GVO calls must be folded away";
    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Equality);
    ASSERT_EQ(c->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Left.get())->Variable.get(), v1.get());
    ASSERT_EQ(c->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Right.get())->Variable.get(), v2.get());
}

// RunIfNullableLift MatchCompOrDecimal relational case (LiftCSharpComparison):
// `comp(lt, GVO(v), ldc.i4 5) ? HV(v) : false` ==> the C#-lifted
// `comp.lifted[C#](lt, ldloc v, ldc.i4 5)` (DoLiftBinary lifts the GVO to ldloc v,
// embeds the pure ldc.i4 5, the single nullableVar v contributes).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsCLiftedRelationalComparison) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto gvo = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    gvo->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    gvo->ReturnType = StackType::I4;
    gvo->AddArg(std::make_unique<LdLoca>(v));
    auto cond = std::make_unique<Comp>(std::move(gvo), std::make_unique<LdcI4>(5),
                                        ComparisonKind::LessThan, false);
    auto hv = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hv->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    hv->AddArg(std::make_unique<LdLoca>(v));
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(hv),
                                                std::make_unique<LdcI4>(0));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLiftedComps(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "the relational fold must produce a lifted comp";
    EXPECT_EQ(CountHasValueCall(*fn), 0) << "the HasValue call must be folded away";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 0) << "the GVO call must be folded away";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::LessThan);
    ASSERT_EQ(c->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Left.get())->Variable.get(), v.get());
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 5);
}

// RunIfNullableLift MatchCompOrDecimal relational negated-condition case:
// `comp(lt, GVO(v), ldc.i4 5) ? !HV(v) : true` ==> `logic.not(comp.lifted[C#](lt,
// ldloc v, ldc.i4 5))` (the `!(v1 != null && ...) : true` shape wraps the lifted
// relational comp in a Comp.LogicNot). The outer comp is a non-lifted Equality
// (the logic.not shape), wrapping the inner C#-lifted LessThan.
TEST(ExpressionTransforms, RunIfNullableLiftFoldsCLiftedRelationalNegated) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto gvo = std::make_unique<Call>("System.Nullable`1::GetValueOrDefault");
    gvo->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    gvo->ReturnType = StackType::I4;
    gvo->AddArg(std::make_unique<LdLoca>(v));
    auto cond = std::make_unique<Comp>(std::move(gvo), std::make_unique<LdcI4>(5),
                                        ComparisonKind::LessThan, false);
    auto hv = std::make_unique<Call>("System.Nullable`1::get_HasValue");
    hv->DeclaringType = MakeNullableOf(KnownTypeCode::Int32);
    hv->AddArg(std::make_unique<LdLoca>(v));
    // true arm = !v.HasValue (comp(eq, HasValue, 0)), false arm = ldc.i4 1.
    auto negHasVal = std::make_unique<Comp>(std::move(hv), std::make_unique<LdcI4>(0),
                                             ComparisonKind::Equality, false);
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(negHasVal),
                                                std::make_unique<LdcI4>(1));
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLiftedComps(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "the relational fold must produce a lifted comp";
    EXPECT_EQ(CountHasValueCall(*fn), 0) << "the HasValue call must be folded away";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp)
        << "the outer logic.not comp wraps the lifted relational";
    auto* outer = static_cast<Comp*>(st->Value.get());
    EXPECT_FALSE(outer->IsLifted()) << "the logic.not wrapper is non-lifted (Equality)";
    EXPECT_EQ(outer->Kind, ComparisonKind::Equality);
    ASSERT_EQ(outer->Left->Op, OpCode::Comp) << "the inner lifted relational comp";
    auto* inner = static_cast<Comp*>(outer->Left.get());
    EXPECT_EQ(inner->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(inner->Kind, ComparisonKind::LessThan);
    ASSERT_EQ(inner->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(inner->Left.get())->Variable.get(), v.get());
    ASSERT_EQ(inner->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(inner->Right.get())->Value, 5);
}

// RunIfNullableLift MatchCompOrDecimal IsGenericNewPattern case (the D108-
// deferred Activator.CreateInstance<T>() fold):
//   (default(T) == null) ? Activator.CreateInstance<T>() : default(T)
//     ==> Activator.CreateInstance<T>()
// The condition compares `default(T)` (a DefaultValue whose Type is a type
// parameter) against ldnull; the false arm is another `default(T)` of the SAME
// type; the true arm is a call to `System.Activator.CreateInstance` with exactly
// one generic type argument. The fold returns the Activator call (the if's
// TrueInst), so the if is replaced by it. A Roslyn-era codegen pattern (fires 0
// on the .NET Framework 4 legacy-csc corpus, ported for faithfulness).
TEST(ExpressionTransforms, RunIfNullableLiftFoldsIsGenericNewPattern) {
    auto T = std::make_shared<ILSpy::Decompiler::TypeSystem::TypeParameter>(
        0, ILSpy::Decompiler::TypeSystem::TypeParameter::OwnerKind::Method, "T");
    auto result = MakeLocal("result", T);
    // condition: comp(eq, default(T), ldnull)
    auto cond = std::make_unique<Comp>(std::make_unique<DefaultValue>(T),
                                        std::make_unique<LdNull>(),
                                        ComparisonKind::Equality, false);
    // true arm: call System.Activator::CreateInstance (generic, 1 type arg)
    auto activator = std::make_unique<Call>("System.Activator::CreateInstance");
    activator->TypeArgumentsCount = 1;
    activator->ReturnType = StackType::O;
    // false arm: default(T) (same T)
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(activator),
                                                std::make_unique<DefaultValue>(T));
    auto fn = MakeFnWithBlock({result});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // The if is replaced by the Activator call (the true arm); the null check
    // and the default(T) fallback are dropped.
    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Call) << "the if must fold to the Activator call";
    auto* call = static_cast<Call*>(st->Value.get());
    EXPECT_EQ(call->MethodName, "System.Activator::CreateInstance");
    EXPECT_EQ(call->TypeArgumentsCount, 1);
}

// IsGenericNewPattern rejects a non-type-parameter underlying type: the false
// arm is `default(int)`, not `default(T)`, so the Activator.CreateInstance<int>()
// shape is not the generic-new pattern (it's a concrete instantiation, not the
// `default(T) == null` null-check Roslyn emits for `new T()`).
TEST(ExpressionTransforms, RunIfNullableLiftIsGenericNewPatternRejectsNonTypeParameter) {
    auto result = MakeLocal("result");
    auto Int = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto cond = std::make_unique<Comp>(std::make_unique<DefaultValue>(Int),
                                        std::make_unique<LdNull>(),
                                        ComparisonKind::Equality, false);
    auto activator = std::make_unique<Call>("System.Activator::CreateInstance");
    activator->TypeArgumentsCount = 1;
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(activator),
                                                std::make_unique<DefaultValue>(Int));
    auto fn = MakeFnWithBlock({result});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    // No fold: the if survives.
    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::IfInstruction) << "non-type-parameter default must not fold";
}

// IsGenericNewPattern rejects mismatched default types: the comp's left is
// `default(T)` but the false arm is `default(U)` (a different type parameter),
// so the two defaults are not the same type.
TEST(ExpressionTransforms, RunIfNullableLiftIsGenericNewPatternRejectsMismatchedDefaults) {
    auto T = std::make_shared<ILSpy::Decompiler::TypeSystem::TypeParameter>(
        0, ILSpy::Decompiler::TypeSystem::TypeParameter::OwnerKind::Method, "T");
    auto U = std::make_shared<ILSpy::Decompiler::TypeSystem::TypeParameter>(
        1, ILSpy::Decompiler::TypeSystem::TypeParameter::OwnerKind::Method, "U");
    auto result = MakeLocal("result", T);
    auto cond = std::make_unique<Comp>(std::make_unique<DefaultValue>(T),
                                        std::make_unique<LdNull>(),
                                        ComparisonKind::Equality, false);
    auto activator = std::make_unique<Call>("System.Activator::CreateInstance");
    activator->TypeArgumentsCount = 1;
    // false arm is default(U), not default(T)
    auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(activator),
                                                std::make_unique<DefaultValue>(U));
    auto fn = MakeFnWithBlock({result});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::IfInstruction) << "mismatched default types must not fold";
}

// IsGenericNewPattern rejects a non-Activator method name (and a non-generic
// Activator call): the true arm is a call to a different method, or the
// Activator call is not a generic instantiation (TypeArgumentsCount == 0).
TEST(ExpressionTransforms, RunIfNullableLiftIsGenericNewPatternRejectsNonActivatorAndNonGeneric) {
    auto T = std::make_shared<ILSpy::Decompiler::TypeSystem::TypeParameter>(
        0, ILSpy::Decompiler::TypeSystem::TypeParameter::OwnerKind::Method, "T");
    auto result = MakeLocal("result", T);
    auto Int = std::make_shared<KnownType>(KnownTypeCode::Int32);

    // Case 1: wrong method name.
    {
        auto cond = std::make_unique<Comp>(std::make_unique<DefaultValue>(T),
                                            std::make_unique<LdNull>(),
                                            ComparisonKind::Equality, false);
        auto other = std::make_unique<Call>("System.Environment::Exit");
        other->TypeArgumentsCount = 1;
        auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(other),
                                                    std::make_unique<DefaultValue>(T));
        auto fn = MakeFnWithBlock({result});
        fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
        fn->CheckInvariant(ILPhase::Normal);
        RunExpressionTransforms(*fn);
        fn->CheckInvariant(ILPhase::Normal);
        auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
        EXPECT_EQ(st->Value->Op, OpCode::IfInstruction)
            << "a non-Activator method name must not fold";
    }
    // Case 2: non-generic Activator (TypeArgumentsCount == 0).
    {
        auto cond = std::make_unique<Comp>(std::make_unique<DefaultValue>(Int),
                                            std::make_unique<LdNull>(),
                                            ComparisonKind::Equality, false);
        auto activator = std::make_unique<Call>("System.Activator::CreateInstance");
        activator->TypeArgumentsCount = 0;  // not a generic instantiation
        auto iff = std::make_unique<IfInstruction>(std::move(cond), std::move(activator),
                                                    std::make_unique<DefaultValue>(Int));
        auto fn = MakeFnWithBlock({result});
        fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(iff)));
        fn->CheckInvariant(ILPhase::Normal);
        RunExpressionTransforms(*fn);
        fn->CheckInvariant(ILPhase::Normal);
        auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
        EXPECT_EQ(st->Value->Op, OpCode::IfInstruction)
            << "a non-generic Activator call must not fold";
    }
}

// RunBinaryNumericNullableLift equality case (the VS2017.8 / Roslyn 2.9
// `&&`-as-`&` optimization on bool operands, analysed as-if short-circuit):
//   BitAnd( comp(eq, GVO(a), GVO(b)), comp(eq, HV(a), HV(b)) )
//     ==> comp.lifted[C#](eq, ldloc a, ldloc b)
// bni.Left is the value comparison (a Comp), bni.Right is the HasValue-bits
// comparison (a Comp), and the fresh LdcI4(0) is the false arm. MatchCompOrDecimal
// matches the value Comp, LiftCSharpEqualityComparison's hasValueComp case lifts
// both GVO sides to ldloc and both HasValue operands match, producing the C#-
// lifted equality. Both operands are Comps so IsBooleanValue gates the BitAnd in.
// A Roslyn-era codegen pattern (fires 0 times on the .NET Framework 4 legacy-csc
// corpus).
TEST(ExpressionTransforms, RunBinaryNumericLiftFoldsCLiftedEqualityComparison) {
    auto a = MakeLocal("a", MakeNullableOf(KnownTypeCode::Int32));
    auto b = MakeLocal("b", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    // bni.Left: comp(eq, GVO(a), GVO(b))  (the value comparison, a Comp)
    auto valueComp = std::make_unique<Comp>(
        MakeGVOCallOf(a, KnownTypeCode::Int32),
        MakeGVOCallOf(b, KnownTypeCode::Int32),
        ComparisonKind::Equality);
    // bni.Right: comp(eq, HV(a), HV(b))  (the HasValue-bits comparison, a Comp)
    auto hasValueComp = std::make_unique<Comp>(
        MakeHasValueCallOf(a, KnownTypeCode::Int32),
        MakeHasValueCallOf(b, KnownTypeCode::Int32),
        ComparisonKind::Equality);
    auto bitAnd = std::make_unique<BinaryNumericInstruction>(
        std::move(valueComp), std::move(hasValueComp),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto fn = MakeFnWithBlock({result, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(bitAnd)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLiftedComps(*fn), 0);
    ASSERT_EQ(CountHasValueCall(*fn), 2);
    ASSERT_EQ(CountGetValueOrDefaultOneArg(*fn), 2);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "the BNI equality lift must produce a lifted comp";
    EXPECT_EQ(CountHasValueCall(*fn), 0) << "both HasValue calls must be folded away";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 0)
        << "both GetValueOrDefault calls must be folded away";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp) << "the stloc now wraps the lifted comp";
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Equality);
    ASSERT_EQ(c->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Left.get())->Variable.get(), a.get());
    ASSERT_EQ(c->Right->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Right.get())->Variable.get(), b.get());
}

// RunBinaryNumericNullableLift bool? equality case: the `&&`-as-`&` form of
//   v.GetValueOrDefault() && v.HasValue   (for Nullable<bool>, i.e. `v == true`)
// lowered to BitAnd(GVO(v), HV(v)). bni.Left is the GVO call (the condition),
// bni.Right is the HasValue call (the true arm), falseInst is the fresh LdcI4(0).
// The bool? equality fold matches (MatchGetValueOrDefault(condition) on a
// Nullable<bool> + MatchHasValueCall(trueInst, v) + IsLdcI4(falseInst, 0)) and
// produces comp.lifted[C#](eq, ldloc v, ldc.i4 1) (`v == true`). A Roslyn-era
// codegen pattern.
TEST(ExpressionTransforms, RunBinaryNumericLiftFoldsBoolEqualityGetValueOrDefaultHasValue) {
    auto v = MakeLocal("v", MakeNullableOf(KnownTypeCode::Boolean));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto bitAnd = std::make_unique<BinaryNumericInstruction>(
        MakeGVOCall(v), MakeHasValueCall(v),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto fn = MakeFnWithBlock({result, v});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(bitAnd)));
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLiftedComps(*fn), 0);
    ASSERT_EQ(CountHasValueCall(*fn), 1);
    ASSERT_EQ(CountGetValueOrDefaultOneArg(*fn), 1);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 1) << "the BNI bool? fold must produce a lifted comp";
    EXPECT_EQ(CountHasValueCall(*fn), 0) << "the HasValue call must be folded away";
    EXPECT_EQ(CountGetValueOrDefaultOneArg(*fn), 0)
        << "the GetValueOrDefault call must be folded away";
    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::Comp);
    auto* c = static_cast<Comp*>(st->Value.get());
    EXPECT_EQ(c->LiftingKind, ComparisonLiftingKind::CSharp);
    EXPECT_EQ(c->Kind, ComparisonKind::Equality) << "==> v == true";
    ASSERT_EQ(c->Left->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(c->Left.get())->Variable.get(), v.get());
    ASSERT_EQ(c->Right->Op, OpCode::LdcI4);
    EXPECT_EQ(static_cast<LdcI4*>(c->Right.get())->Value, 1) << "==> v == true (1)";
}

// RunBinaryNumericNullableLift does not fire when the operands are not Boolean-
// typed: an `int & int` BitAnd (IsBooleanValue -> false for an Int32 LdLoc) does
// not pass the BitAnd gate, so RunBinaryNumericNullableLift is not invoked and the
// BitAnd stays. This is the common bitwise-and case (the gate's purpose is to
// avoid the wasted lift attempt on it).
TEST(ExpressionTransforms, RunBinaryNumericLiftRejectsNonBooleanOperands) {
    auto a = MakeParam("a", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto b = MakeParam("b", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto v = MakeLocal("v", std::make_shared<KnownType>(KnownTypeCode::Int32));
    auto bitAnd = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(a), std::make_unique<LdLoc>(b),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto fn = MakeFnWithBlock({v, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(v, std::move(bitAnd)));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto* st = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::BinaryNumericInstruction);
    auto* bni = static_cast<BinaryNumericInstruction*>(st->Value.get());
    EXPECT_EQ(bni->Operator, BinaryNumericOperator::BitAnd)
        << "an int BitAnd must stay (not Boolean-typed, gate fails)";
}

// RunBinaryNumericNullableLift does not fire when LiftNullables is off: the
// equality BNI shape is recognised by the BitAnd gate (both operands are Comps)
// and RunBinaryNumericNullableLift is invoked, but LiftNullableCore returns nullptr
// (the LiftNullables gate inside it), so the BitAnd stays.
TEST(ExpressionTransforms, RunBinaryNumericLiftNoOpWhenLiftNullablesOff) {
    auto a = MakeLocal("a", MakeNullableOf(KnownTypeCode::Int32));
    auto b = MakeLocal("b", MakeNullableOf(KnownTypeCode::Int32));
    auto result = MakeLocal("result", std::make_shared<KnownType>(KnownTypeCode::Boolean));
    auto valueComp = std::make_unique<Comp>(
        MakeGVOCallOf(a, KnownTypeCode::Int32),
        MakeGVOCallOf(b, KnownTypeCode::Int32),
        ComparisonKind::Equality);
    auto hasValueComp = std::make_unique<Comp>(
        MakeHasValueCallOf(a, KnownTypeCode::Int32),
        MakeHasValueCallOf(b, KnownTypeCode::Int32),
        ComparisonKind::Equality);
    auto bitAnd = std::make_unique<BinaryNumericInstruction>(
        std::move(valueComp), std::move(hasValueComp),
        BinaryNumericOperator::BitAnd, StackType::I4);
    auto fn = MakeFnWithBlock({result, a, b});
    fn->Body->Blocks[0]->Add(std::make_unique<StLoc>(result, std::move(bitAnd)));
    fn->CheckInvariant(ILPhase::Normal);

    StatementTransform st;
    st.AddChild(std::make_unique<ExpressionTransforms>());
    ILTransformContext ctx;
    ctx.Settings.LiftNullables = false;
    st.Run(*fn, ctx);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLiftedComps(*fn), 0) << "LiftNullables off must block the BNI lift";
    auto* stloc = static_cast<StLoc*>(fn->Body->Blocks[0]->Instructions[0].get());
    ASSERT_EQ(stloc->Value->Op, OpCode::BinaryNumericInstruction)
        << "the BitAnd must stay when LiftNullables is off";
}

// TransformCatchVariable: the catch entry's `stloc v(ldloc ex)` copy (csc emits
// this for every catch whose variable is used) is promoted -- v becomes the
// handler's catch variable (Kind=ExceptionLocal, name/type/generated-name copied
// from the E_<offset> slot) and the copy stloc is dropped. The common case: a
// concrete catch type, no unbox.any.
TEST(ExpressionTransforms, TransformCatchVariablePromotesCopyLocalToCatchVariable) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto fx = BuildCatch(catchType, /*vUses=*/2);
    ASSERT_TRUE(fx.handler && fx.handler->Variable);
    ASSERT_EQ(fx.handler->Variable->Kind, VariableKind::ExceptionStackSlot)
        << "the catch variable starts as the E_<offset> exception slot";
    ASSERT_EQ(fx.handler->Variable->LoadCount, 1) << "the slot is loaded once by the copy";
    ASSERT_TRUE(fx.handler->Variable->IsSingleDefinition());
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fx.handler->Variable.get(), fx.v.get())
        << "the local v is now the catch variable";
    EXPECT_EQ(fx.v->Kind, VariableKind::ExceptionLocal);
    EXPECT_EQ(fx.v->Name, "E_100") << "the slot's name is copied to the promoted local";
    EXPECT_TRUE(fx.v->HasGeneratedName) << "the slot's HasGeneratedName is copied";
    // The copy `stloc v(ldloc ex)` is removed; the remaining instructions are the
    // uses of v (`stloc result(ldloc v)`), so no StLoc to v remains in the entry.
    bool hasStLocToV = false;
    for (auto& inst : fx.catchEntry->Instructions) {
        auto* s = dynamic_cast<StLoc*>(inst.get());
        if (s && s->Variable.get() == fx.v.get()) hasStLocToV = true;
    }
    EXPECT_FALSE(hasStLocToV) << "the copy `stloc v(ldloc ex)` is removed";
}

// TransformCatchVariable must reject when the copy target is not a Local /
// StackSlot (e.g. a Parameter) -- only a local/stack-slot copy is the csc pattern.
TEST(ExpressionTransforms, TransformCatchVariableRejectsNonLocalCopyTarget) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto fx = BuildCatch(catchType, /*vUses=*/1, /*unboxAny=*/false, /*escapedUse=*/false,
                         /*vKind=*/VariableKind::Parameter);
    ASSERT_EQ(fx.v->Kind, VariableKind::Parameter);
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fx.handler->Variable.get(), fx.ex.get())
        << "the slot stays the catch variable when the copy target is a Parameter";
    EXPECT_EQ(fx.v->Kind, VariableKind::Parameter) << "the local is not promoted";
    ASSERT_FALSE(fx.catchEntry->Instructions.empty());
    EXPECT_EQ(fx.catchEntry->Instructions[0]->Op, OpCode::StLoc)
        << "the copy `stloc v(ldloc ex)` stays";
}

// TransformCatchVariable must reject when the copy's value is not `ldloc ex`
// (the slot). Here the copy stores a different variable, so the slot is not the
// loaded one and the pattern does not match.
TEST(ExpressionTransforms, TransformCatchVariableRejectsNonLdLocSlotValue) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto fx = BuildCatch(catchType, /*vUses=*/1);
    // Rewrite the copy's value to a different variable's load.
    auto other = MakeLocal("other", catchType);
    fx.fn->Variables.push_back(other);
    static_cast<StLoc*>(fx.catchEntry->Instructions[0].get())->SetChild(
        0, std::make_unique<LdLoc>(other));
    ComputeVariableUsage(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fx.handler->Variable.get(), fx.ex.get())
        << "the slot stays the catch variable when the copy does not load it";
    ASSERT_FALSE(fx.catchEntry->Instructions.empty());
    EXPECT_EQ(fx.catchEntry->Instructions[0]->Op, OpCode::StLoc)
        << "the copy stays";
}

// TransformCatchVariable must reject when a use of the promoted local escapes
// the catch handler (a `ldloc v` in the try body). The C# `!inst.IsDescendantOf(
// handler)` guard.
TEST(ExpressionTransforms, TransformCatchVariableRejectsEscapedUse) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto fx = BuildCatch(catchType, /*vUses=*/1, /*unboxAny=*/false, /*escapedUse=*/true);
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fx.handler->Variable.get(), fx.ex.get())
        << "the slot stays the catch variable when a use of v escapes";
}

// TransformCatchVariable must reject when the slot is not a single definition
// loaded exactly once (here the slot is loaded twice, by a second `ldloc ex` in
// the catch body).
TEST(ExpressionTransforms, TransformCatchVariableRejectsMultiLoadSlot) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto fx = BuildCatch(catchType, /*vUses=*/2);
    // Add a second load of the slot inside the catch body.
    fx.catchEntry->Add(std::make_unique<StLoc>(fx.result, std::make_unique<LdLoc>(fx.ex)));
    ComputeVariableUsage(*fx.fn);
    ASSERT_EQ(fx.handler->Variable->LoadCount, 2);
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fx.handler->Variable.get(), fx.ex.get())
        << "the slot stays the catch variable when it is loaded more than once";
}

// TransformCatchVariable unwraps an unbox.any wrapping the slot load when the
// unbox.any's Type equals the catch variable's type (the type-parameter catch
// shape `stloc v(unbox.any T(ldloc ex))` => `catch v : T`). The fold promotes v
// and drops the copy.
TEST(ExpressionTransforms, TransformCatchVariableUnwrapsUnboxAnyForTypeParameterCatch) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto fx = BuildCatch(catchType, /*vUses=*/1, /*unboxAny=*/true);
    ASSERT_EQ(fx.handler->Variable->Type.get(), catchType.get())
        << "the slot's type matches the unbox.any's type";
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fx.handler->Variable.get(), fx.v.get())
        << "the local v is promoted even with an unbox.any wrapping the slot load";
    EXPECT_EQ(fx.v->Kind, VariableKind::ExceptionLocal);
    bool hasStLocToV = false;
    for (auto& inst : fx.catchEntry->Instructions) {
        auto* s = dynamic_cast<StLoc*>(inst.get());
        if (s && s->Variable.get() == fx.v.get()) hasStLocToV = true;
    }
    EXPECT_FALSE(hasStLocToV) << "the copy `stloc v(unbox.any T(ldloc ex))` is removed";
}

// TransformCatchVariable must reject the unbox.any when its Type does not
// equal the catch variable's type (a mismatched unbox.any is not the type-
// parameter catch pattern).
TEST(ExpressionTransforms, TransformCatchVariableRejectsMismatchedUnboxAnyType) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto otherType = std::make_shared<KnownType>(KnownTypeCode::Object);
    auto fx = BuildCatch(catchType, /*vUses=*/1, /*unboxAny=*/true);
    // Rewrite the unbox.any's type to a mismatched type.
    auto* st = static_cast<StLoc*>(fx.catchEntry->Instructions[0].get());
    static_cast<UnboxAny*>(st->Value.get())->Type = otherType;
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fx.handler->Variable.get(), fx.ex.get())
        << "the slot stays the catch variable when the unbox.any type mismatches";
}

// TransformCatchVariable must reject when the catch entry's first instruction
// is not a StLoc (the copy was already inlined or the catch has a different
// shape). The deferred "remove inlined UnboxAny" branch is not exercised; the
// fold bails conservative.
TEST(ExpressionTransforms, TransformCatchVariableRejectsNonStLocFirstInstruction) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto fx = BuildCatch(catchType, /*vUses=*/1);
    // Replace the copy with a non-StLoc first instruction (a LdLoc of the slot).
    fx.catchEntry->Instructions[0] = std::make_unique<LdLoc>(fx.ex);
    fx.catchEntry->Instructions[0]->Parent = fx.catchEntry;
    fx.catchEntry->Instructions[0]->ChildIndex = 0;
    fx.catchEntry->RenumberChildren();
    ComputeVariableUsage(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(fx.handler->Variable.get(), fx.ex.get())
        << "the slot stays the catch variable when the first instruction is not a StLoc";
}

// TransformCatchWhen: a catch-when filter whose entry block is a single Leave
// carrying the condition is inlined -- the filter BlockContainer is replaced by
// the leave's Value (the condition). The filter entry's leave is this port's
// FinalInstruction (not a non-terminal as in the C#), so the check is: empty
// non-terminal Instructions + a Leave final with a Value. The `when (ex != null)`
// condition loads the catch slot ex, so ex.LoadCount becomes 2 and the catch
// body's copy is NOT promoted (the test focuses on the filter inlining).
TEST(ExpressionTransforms, TransformCatchWhenInlinesSingleLeaveFilter) {
    auto catchType = std::make_shared<KnownType>(KnownTypeCode::Exception);
    auto fx = BuildCatch(catchType, /*vUses=*/1);
    // Replace the constant-true LdcI4(1) filter with a single-block catch-when
    // filter: a BlockContainer whose one block has empty Instructions and a
    // Leave(filter, condition) final.
    auto filter = std::make_unique<BlockContainer>();
    filter->AddBlock(std::make_unique<Block>());
    auto cond = std::make_unique<Comp>(std::make_unique<LdLoc>(fx.ex),
                                       std::make_unique<LdNull>(),
                                       ComparisonKind::Inequality, false);
    filter->Blocks[0]->SetFinal(std::make_unique<Leave>(filter.get(), std::move(cond)));
    fx.handler->Filter = std::move(filter);
    fx.handler->Filter->Parent = fx.handler;
    fx.handler->Filter->ChildIndex = 0;
    ComputeVariableUsage(*fx.fn);
    RecomputeIncomingEdgeCounts(*fx.fn);
    ASSERT_EQ(fx.handler->Variable->LoadCount, 2)
        << "ex is loaded by the filter condition + the catch body copy";
    fx.fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransformsOnly(*fx.fn);
    fx.fn->CheckInvariant(ILPhase::Normal);

    ASSERT_NE(fx.handler->Filter, nullptr);
    EXPECT_EQ(fx.handler->Filter->Op, OpCode::Comp)
        << "the catch-when filter is inlined to its condition (a Comp)";
    EXPECT_EQ(fx.handler->Variable.get(), fx.ex.get())
        << "the catch body copy is not promoted (ex is loaded by the filter too)";
}

// ---- TransformDecimalFieldToConstant (VisitLdObj subset) ----

namespace {

// A one-block function assigning a `ldobj(ldsflda <fieldName>)` static-field
// load (of field type `fieldType`) to a local `d`, the shape
// TransformDecimalFieldToConstant matches.
std::unique_ptr<ILFunction> MakeFnWithDecimalFieldLoad(ITypePtr fieldType,
                                                      std::string fieldName) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto d = MakeLocal("d", fieldType);
    fn->Variables.push_back(d);
    auto root = std::make_unique<Block>();
    auto addr = std::make_unique<LdsFlda>(std::move(fieldName));
    root->Add(std::make_unique<StLoc>(d,
        std::make_unique<LdObj>(std::move(addr), fieldType)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    return fn;
}

} // namespace

// VisitLdObj folds `ldobj(ldsflda System.Decimal::One)` into the LdcDecimal
// constant `1m`. The static-field load is replaced by the constant, so the
// StLoc's value becomes an LdcDecimal.
TEST(ExpressionTransforms, VisitLdObjFoldsDecimalOneFieldToLdcDecimal) {
    auto decimalType = std::make_shared<KnownType>(KnownTypeCode::Decimal);
    auto fn = MakeFnWithDecimalFieldLoad(decimalType, "System.Decimal::One");
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::LdcDecimal)
        << "ldsfld Decimal.One must fold to LdcDecimal";
    EXPECT_EQ(static_cast<LdcDecimal*>(st->Value.get())->Value, DecimalValue::One());
    EXPECT_EQ(CountLdcDecimal(*fn), 1);
    EXPECT_EQ(CountDecimalConstantFieldLoads(*fn), 0);
    // The seed renders the folded constant as `d = 1m`.
    std::string text = ILAstToCSharp(*fn, "void", "M", "decimal d");
    EXPECT_NE(text.find("d = 1m"), std::string::npos) << text;
}

// VisitLdObj folds Decimal.Zero -> `0m` and Decimal.MinusOne -> `-1m`.
TEST(ExpressionTransforms, VisitLdObjFoldsDecimalZeroAndMinusOneFields) {
    auto decimalType = std::make_shared<KnownType>(KnownTypeCode::Decimal);
    for (auto [fieldName, expected] : std::vector<std::pair<std::string, DecimalValue>>{
            {"System.Decimal::Zero", DecimalValue::Zero()},
            {"System.Decimal::MinusOne", DecimalValue::MinusOne()}}) {
        auto fn = MakeFnWithDecimalFieldLoad(decimalType, fieldName);
        fn->CheckInvariant(ILPhase::Normal);
        RunExpressionTransforms(*fn);
        fn->CheckInvariant(ILPhase::Normal);
        auto& blk = fn->Body->Blocks[0];
        ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
        auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
        ASSERT_EQ(st->Value->Op, OpCode::LdcDecimal)
            << fieldName << " must fold to LdcDecimal";
        EXPECT_EQ(static_cast<LdcDecimal*>(st->Value.get())->Value, expected);
    }
}

// VisitLdObj keeps a non-Decimal static field load (e.g. `ldsfld
// System.String::Empty`) -- the field's declaring type is not System.Decimal, so
// TransformDecimalFieldToConstant does not fire.
TEST(ExpressionTransforms, VisitLdObjRejectsNonDecimalField) {
    auto stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto fn = MakeFnWithDecimalFieldLoad(stringType, "System.String::Empty");
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::LdObj)
        << "a non-Decimal static field load must stay (not folded to LdcDecimal)";
    EXPECT_EQ(CountLdcDecimal(*fn), 0);
}

// VisitLdObj keeps a Decimal static field that is NOT one of the three
// named constants (e.g. a hypothetical `System.Decimal::SomeField`) -- only
// One/Zero/MinusOne fold.
TEST(ExpressionTransforms, VisitLdObjRejectsNonConstantDecimalField) {
    auto decimalType = std::make_shared<KnownType>(KnownTypeCode::Decimal);
    auto fn = MakeFnWithDecimalFieldLoad(decimalType, "System.Decimal::SomeField");
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::LdObj)
        << "a non-constant Decimal static field must stay (only One/Zero/MinusOne fold)";
    EXPECT_EQ(CountLdcDecimal(*fn), 0);
}

// VisitLdObj keeps an instance field load `ldobj(ldflda target, F)` -- the
// target is an LdFlda (not an LdsFlda), so TransformDecimalFieldToConstant does
// not fire (the fold is for static fields only).
TEST(ExpressionTransforms, VisitLdObjRejectsInstanceFieldLoad) {
    auto decimalType = std::make_shared<KnownType>(KnownTypeCode::Decimal);
    auto target = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto d = MakeLocal("d", decimalType);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(target);
    fn->Variables.push_back(d);
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(d,
        std::make_unique<LdObj>(
            std::make_unique<LdFlda>(std::make_unique<LdLoc>(target), "System.Decimal::One"),
            decimalType)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::LdObj)
        << "an instance field load must stay (the fold is for static fields only)";
    EXPECT_EQ(CountLdcDecimal(*fn), 0);
}


// A function whose body stores a `newobj DelegateType(target, ldvirtftn M)` into
// a local `d` (the shape TransformDelegateCtorLdVirtFtnToLdVirtDelegate folds).
std::unique_ptr<ILFunction> MakeFnWithNewObjVirtDelegate(ITypePtr delegateType,
                                                          std::unique_ptr<ILInstruction> target,
                                                          std::string ldvirtftnMethod) {
    auto d = MakeLocal("d", delegateType);
    auto t = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(d);
    fn->Variables.push_back(t);
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(d,
        MakeNewObjVirtDelegate(delegateType, std::move(target), std::move(ldvirtftnMethod))));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    return fn;
}

// TransformDelegateCtorLdVirtFtnToLdVirtDelegate folds a virtual delegate
// construction `newobj DelegateType(target, ldvirtftn M(target))` into an
// LdVirtDelegate (`ldvirtdelegate DelegateType M(target)`), unifying the target
// and the virtual method. The newobj Call is replaced in place (a value-position
// swap -- a newobj is a value, never a block final).
TEST(ExpressionTransforms, TransformDelegateCtorLdVirtFtnToLdVirtDelegateFolds) {
    auto delegateType = MakeDelegateType("System", "Action");
    auto target = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeFnWithNewObjVirtDelegate(delegateType,
                                           std::make_unique<LdLoc>(target),
                                           "System.Foo::Bar");
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountNewObjVirtDelegate(*fn), 1);
    ASSERT_EQ(CountLdVirtDelegate(*fn), 0);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    auto& blk = fn->Body->Blocks[0];
    ASSERT_EQ(blk->Instructions.size(), 1u);
    ASSERT_EQ(blk->Instructions[0]->Op, OpCode::StLoc);
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    ASSERT_EQ(st->Value->Op, OpCode::LdVirtDelegate)
        << "newobj Delegate(target, ldvirtftn M) must fold to LdVirtDelegate";
    auto* d = static_cast<LdVirtDelegate*>(st->Value.get());
    ASSERT_NE(d->Type, nullptr);
    EXPECT_EQ(d->Type->Kind(), TypeKind::Delegate);
    EXPECT_EQ(d->MethodName, "System.Foo::Bar");
    ASSERT_EQ(d->Argument->Op, OpCode::LdLoc);
    EXPECT_EQ(static_cast<LdLoc*>(d->Argument.get())->Variable.get(), target.get())
        << "the LdVirtDelegate carries the newobj's target";
    EXPECT_EQ(CountNewObjVirtDelegate(*fn), 0);
    EXPECT_EQ(CountLdVirtDelegate(*fn), 1);
}

// The fold requires a Delegate declaring type (the C# checks
// `Method.DeclaringType.Kind != TypeKind.Delegate`); a Class declaring type does
// not fold.
TEST(ExpressionTransforms, TransformDelegateCtorRejectsNonDelegateDeclaringType) {
    auto classType = std::make_shared<SimpleType>(TopLevelTypeName("System", "Foo"),
                                                  TypeKind::Class);
    auto target = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeFnWithNewObjVirtDelegate(classType,
                                           std::make_unique<LdLoc>(target),
                                           "System.Foo::Bar");
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLdVirtDelegate(*fn), 0)
        << "a non-Delegate declaring type must not fold";
    EXPECT_EQ(CountNewObjVirtDelegate(*fn), 1);
}

// The fold is for virtual delegates only: a `newobj Delegate(target, ldftn M)`
// (a static/instance method delegate, not a virtual one) does not fold -- the
// 2nd arg is an LdFtn, not an LdVirtFtn.
TEST(ExpressionTransforms, TransformDelegateCtorRejectsLdFtnSecondArg) {
    auto delegateType = MakeDelegateType("System", "Action");
    auto target = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto d = MakeLocal("d", delegateType);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(d);
    fn->Variables.push_back(target);
    auto root = std::make_unique<Block>();
    auto call = std::make_unique<Call>(
        delegateType->ReflectionName() + "..ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = delegateType;
    call->AddArg(std::make_unique<LdLoc>(target));
    call->AddArg(std::make_unique<LdFtn>("System.Foo::Bar"));  // ldftn, not ldvirtftn
    root->Add(std::make_unique<StLoc>(d, std::move(call)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLdVirtDelegate(*fn), 0)
        << "a ldftn (non-virtual) delegate construction must not fold";
    auto& blk = fn->Body->Blocks[0];
    auto* st = static_cast<StLoc*>(blk->Instructions[0].get());
    EXPECT_EQ(st->Value->Op, OpCode::Call) << "the newobj Call must stay";
}

// The fold requires a pure target (the C# `IsPure(Arguments[0].Flags)`); an
// impure target (a Call with side effects) does not fold.
TEST(ExpressionTransforms, TransformDelegateCtorRejectsImpureTarget) {
    auto delegateType = MakeDelegateType("System", "Action");
    auto target = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto d = MakeLocal("d", delegateType);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(d);
    fn->Variables.push_back(target);
    // An impure target: a Call (a method call has side effects, so IsPure is
    // false). The 1st arg is the call, the 2nd is the ldvirtftn.
    auto impureTarget = std::make_unique<Call>("System.Foo::MakeTarget");
    impureTarget->ReturnType = StackType::O;
    auto call = std::make_unique<Call>(
        delegateType->ReflectionName() + "..ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = delegateType;
    call->AddArg(std::move(impureTarget));
    call->AddArg(std::make_unique<LdVirtFtn>("System.Foo::Bar"));
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(d, std::move(call)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLdVirtDelegate(*fn), 0)
        << "an impure target must not fold (the target must be pure)";
}

// The fold requires exactly 2 arguments (target + ldvirtftn); a 3-arg newobj
// delegate does not fold.
TEST(ExpressionTransforms, TransformDelegateCtorRejectsWrongArgCount) {
    auto delegateType = MakeDelegateType("System", "Action");
    auto target = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto d = MakeLocal("d", delegateType);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(d);
    fn->Variables.push_back(target);
    auto call = std::make_unique<Call>(
        delegateType->ReflectionName() + "..ctor");
    call->IsNewObj = true;
    call->ReturnType = StackType::O;
    call->DeclaringType = delegateType;
    call->AddArg(std::make_unique<LdLoc>(target));
    call->AddArg(std::make_unique<LdVirtFtn>("System.Foo::Bar"));
    call->AddArg(std::make_unique<LdLoc>(target));  // a 3rd arg -> not the 2-arg shape
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(d, std::move(call)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLdVirtDelegate(*fn), 0)
        << "a 3-arg newobj delegate must not fold (the shape is 2-arg)";
}

// The fold requires a newobj (Call with IsNewObj); a plain call/callvirt with an
// ldvirtftn 2nd arg does not fold.
TEST(ExpressionTransforms, TransformDelegateCtorRejectsNonNewObj) {
    auto delegateType = MakeDelegateType("System", "Action");
    auto target = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto d = MakeLocal("d", delegateType);
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(d);
    fn->Variables.push_back(target);
    auto call = std::make_unique<Call>(
        delegateType->ReflectionName() + "::MakeDelegate");
    call->IsNewObj = false;  // a plain call, not a newobj
    call->ReturnType = StackType::O;
    call->DeclaringType = delegateType;
    call->AddArg(std::make_unique<LdLoc>(target));
    call->AddArg(std::make_unique<LdVirtFtn>("System.Foo::Bar"));
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<StLoc>(d, std::move(call)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);

    EXPECT_EQ(CountLdVirtDelegate(*fn), 0)
        << "a non-newobj call with an ldvirtftn arg must not fold";
}

// The seed renders an LdVirtDelegate as `new DelegateType(target.Method)` --
// the real back end's VisitLdVirtDelegate (CallBuilder.Build ->
// HandleDelegateConstruction) folds the target and the virtual method into a
// `target.Method` method group inside `new DelegateType(...)`.
TEST(ExpressionTransforms, SeedRendersLdVirtDelegateAsNewDelegateTargetMethod) {
    auto delegateType = MakeDelegateType("System", "Action");
    auto target = MakeLocal("target", std::make_shared<KnownType>(KnownTypeCode::Object));
    auto fn = MakeFnWithNewObjVirtDelegate(delegateType,
                                           std::make_unique<LdLoc>(target),
                                           "System.Foo::Bar");
    fn->CheckInvariant(ILPhase::Normal);

    RunExpressionTransforms(*fn);
    fn->CheckInvariant(ILPhase::Normal);
    ASSERT_EQ(CountLdVirtDelegate(*fn), 1);

    std::string text = ILAstToCSharp(*fn, "void", "M", "Action d");
    EXPECT_NE(text.find("new System.Action(target.Bar)"), std::string::npos)
        << "the LdVirtDelegate must render as `new DelegateType(target.Method)`"
        << text;
}
