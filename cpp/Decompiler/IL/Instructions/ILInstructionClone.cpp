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

// Out-of-line definition of ILInstruction::Clone -- the deep-clone port of the
// C# generated `ILInstruction.Clone()` (one `sealed override` per instruction
// class in Instructions.cs). The C# Clone is a virtual per class; this port
// implements the dispatch as a single switch over `Op` in one translation unit
// so the clone logic is centralized and auditable (the port has ~60 concrete
// instruction classes vs the C#'s ~200; the not-yet-ported families -- dynamic,
// expression-tree, deconstruct, await, ... -- have no class here and fall
// through to the default assert). Each case constructs a fresh node copying the
// scalar fields, deep-clones the owned children (via their own Clone), copies
// the owned-collection elements ( cloning each), and propagates the IL byte
// range. References that are not owned children -- ILVariables (shared_ptr),
// Branch target blocks (non-owning Block*), and Leave target containers
// (non-owning BlockContainer*) -- are copied by reference, matching the C# where
// those are not owned subtrees; a clone inserted elsewhere keeps pointing at
// the original tree's variables/blocks/containers and the caller fixes them up.
// The clone is disconnected (Parent == nullptr, ChildIndex == -1) but
// internally a valid tree (its children's Parent back-pointers point at the
// clone, set by the constructors / Add helpers).

#include "Decompiler/IL/ILInstruction.hpp"

#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/BitNot.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/RefAnyType.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/SimpleInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/ThreeValuedBoolInstructions.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"

#include <cassert>
#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// Clone every element of a vector of owned instructions, preserving nulls
// (a null collection slot is rare but the collection model allows it).
std::vector<std::unique_ptr<ILInstruction>> CloneChildren(
    const std::vector<std::unique_ptr<ILInstruction>>& src) {
    std::vector<std::unique_ptr<ILInstruction>> out;
    out.reserve(src.size());
    for (auto& inst : src) out.push_back(inst ? inst->Clone() : nullptr);
    return out;
}

} // namespace

std::unique_ptr<ILInstruction> ILInstruction::Clone() const {
    std::unique_ptr<ILInstruction> c;
    switch (Op) {
        // ---- leaf nodes (no children) ----
        case OpCode::Nop: c = std::make_unique<Nop>(); break;
        case OpCode::LdNull: c = std::make_unique<LdNull>(); break;
        case OpCode::Rethrow: c = std::make_unique<Rethrow>(); break;
        case OpCode::LdcI4: {
            const auto& s = static_cast<const LdcI4&>(*this);
            c = std::make_unique<LdcI4>(s.Value);
            break;
        }
        case OpCode::LdcI8: {
            const auto& s = static_cast<const LdcI8&>(*this);
            c = std::make_unique<LdcI8>(s.Value);
            break;
        }
        case OpCode::LdcF4: {
            const auto& s = static_cast<const LdcF4&>(*this);
            c = std::make_unique<LdcF4>(s.Value);
            break;
        }
        case OpCode::LdcF8: {
            const auto& s = static_cast<const LdcF8&>(*this);
            c = std::make_unique<LdcF8>(s.Value);
            break;
        }
        case OpCode::LdcDecimal: {
            const auto& s = static_cast<const LdcDecimal&>(*this);
            c = std::make_unique<LdcDecimal>(s.Value);
            break;
        }
        case OpCode::LdStr: {
            const auto& s = static_cast<const LdStr&>(*this);
            c = std::make_unique<LdStr>(s.Value);
            break;
        }
        case OpCode::DefaultValue: {
            const auto& s = static_cast<const DefaultValue&>(*this);
            c = std::make_unique<DefaultValue>(s.Type);
            break;
        }
        case OpCode::LdsFlda: {
            const auto& s = static_cast<const LdsFlda&>(*this);
            auto clone = std::make_unique<LdsFlda>(s.FieldName);
            clone->FieldToken = s.FieldToken;
            clone->IsCompilerGeneratedField = s.IsCompilerGeneratedField;
            c = std::move(clone);
            break;
        }
        case OpCode::LdFtn: {
            const auto& s = static_cast<const LdFtn&>(*this);
            c = std::make_unique<LdFtn>(s.MethodName);
            break;
        }
        case OpCode::LdVirtFtn: {
            const auto& s = static_cast<const LdVirtFtn&>(*this);
            c = std::make_unique<LdVirtFtn>(s.MethodName);
            break;
        }
        case OpCode::SizeOf: {
            const auto& s = static_cast<const SizeOf&>(*this);
            c = std::make_unique<SizeOf>(s.TypeName);
            break;
        }
        case OpCode::LdTypeToken: {
            const auto& s = static_cast<const LdTypeToken&>(*this);
            c = std::make_unique<LdTypeToken>(s.TokenName);
            break;
        }
        case OpCode::LdLoc: {
            const auto& s = static_cast<const LdLoc&>(*this);
            c = std::make_unique<LdLoc>(s.Variable);
            break;
        }
        case OpCode::LdLoca: {
            const auto& s = static_cast<const LdLoca&>(*this);
            c = std::make_unique<LdLoca>(s.Variable);
            break;
        }
        case OpCode::Branch: {
            const auto& s = static_cast<const Branch&>(*this);
            auto clone = std::make_unique<Branch>(s.TargetOffset);
            clone->TargetBlock = s.TargetBlock;  // non-owning reference
            clone->HasOffset = s.HasOffset;
            c = std::move(clone);
            break;
        }

        // ---- one-value-slot nodes ----
        case OpCode::StLoc: {
            const auto& s = static_cast<const StLoc&>(*this);
            c = std::make_unique<StLoc>(s.Variable, s.Value ? s.Value->Clone() : nullptr);
            break;
        }
        case OpCode::Leave: {
            const auto& s = static_cast<const Leave&>(*this);
            c = std::make_unique<Leave>(s.TargetContainer, s.Value ? s.Value->Clone() : nullptr);
            break;
        }

        // ---- unary nodes (Argument slot) ----
        case OpCode::BitNot: {
            const auto& s = static_cast<const BitNot&>(*this);
            c = std::make_unique<BitNot>(s.Argument ? s.Argument->Clone() : nullptr,
                                         s.IsLifted, s.UnderlyingResultType);
            break;
        }
        case OpCode::Box: {
            const auto& s = static_cast<const Box&>(*this);
            c = std::make_unique<Box>(s.Type, s.Argument ? s.Argument->Clone() : nullptr);
            break;
        }
        case OpCode::CastClass: {
            const auto& s = static_cast<const CastClass&>(*this);
            c = std::make_unique<CastClass>(s.Type, s.Argument ? s.Argument->Clone() : nullptr);
            break;
        }
        case OpCode::IsInst: {
            const auto& s = static_cast<const IsInst&>(*this);
            c = std::make_unique<IsInst>(s.Type, s.Argument ? s.Argument->Clone() : nullptr);
            break;
        }
        case OpCode::UnboxAny: {
            const auto& s = static_cast<const UnboxAny&>(*this);
            c = std::make_unique<UnboxAny>(s.Type, s.Argument ? s.Argument->Clone() : nullptr);
            break;
        }
        case OpCode::Conv: {
            const auto& s = static_cast<const Conv&>(*this);
            auto clone = std::make_unique<Conv>(s.Argument ? s.Argument->Clone() : nullptr,
                s.InputType, s.InputSign, s.TargetType, s.CheckForOverflow, s.IsLifted);
            clone->Kind = s.Kind;  // ctor re-derives; overwrite for exact fidelity
            c = std::move(clone);
            break;
        }
        case OpCode::LdLen: {
            const auto& s = static_cast<const LdLen&>(*this);
            c = std::make_unique<LdLen>(s.resultType, s.Argument ? s.Argument->Clone() : nullptr);
            break;
        }
        case OpCode::Throw: {
            const auto& s = static_cast<const Throw&>(*this);
            c = std::make_unique<Throw>(s.Argument ? s.Argument->Clone() : nullptr);
            break;
        }
        case OpCode::RefAnyType: {
            const auto& s = static_cast<const RefAnyType&>(*this);
            c = std::make_unique<RefAnyType>(s.Argument ? s.Argument->Clone() : nullptr);
            break;
        }
        case OpCode::NullableRewrap: {
            const auto& s = static_cast<const NullableRewrap&>(*this);
            c = std::make_unique<NullableRewrap>(s.Argument ? s.Argument->Clone() : nullptr);
            break;
        }
        case OpCode::NullableUnwrap: {
            const auto& s = static_cast<const NullableUnwrap&>(*this);
            c = std::make_unique<NullableUnwrap>(s.ResultTypeField,
                s.Argument ? s.Argument->Clone() : nullptr, s.RefInput);
            break;
        }
        case OpCode::LdVirtDelegate: {
            const auto& s = static_cast<const LdVirtDelegate&>(*this);
            c = std::make_unique<LdVirtDelegate>(s.Argument ? s.Argument->Clone() : nullptr,
                s.Type, s.MethodName);
            break;
        }

        // ---- binary nodes (Left/Right slots) ----
        case OpCode::Comp: {
            const auto& s = static_cast<const Comp&>(*this);
            c = std::make_unique<Comp>(s.Left ? s.Left->Clone() : nullptr,
                s.Right ? s.Right->Clone() : nullptr, s.Kind, s.LiftingKind, s.InputType, s.Unsigned);
            break;
        }
        case OpCode::BinaryNumericInstruction: {
            const auto& s = static_cast<const BinaryNumericInstruction&>(*this);
            auto clone = std::make_unique<BinaryNumericInstruction>(
                s.Left ? s.Left->Clone() : nullptr,
                s.Right ? s.Right->Clone() : nullptr,
                s.Operator, s.LeftInputType, s.RightInputType,
                s.CheckForOverflow, s.Sign, s.IsLifted);
            clone->ResultStackType = s.ResultStackType;  // ctor computes; overwrite for fidelity
            clone->Signed = s.Signed;
            c = std::move(clone);
            break;
        }
        case OpCode::ThreeValuedBoolAnd: {
            const auto& s = static_cast<const ThreeValuedBoolAnd&>(*this);
            c = std::make_unique<ThreeValuedBoolAnd>(s.Left ? s.Left->Clone() : nullptr,
                s.Right ? s.Right->Clone() : nullptr);
            break;
        }
        case OpCode::ThreeValuedBoolOr: {
            const auto& s = static_cast<const ThreeValuedBoolOr&>(*this);
            c = std::make_unique<ThreeValuedBoolOr>(s.Left ? s.Left->Clone() : nullptr,
                s.Right ? s.Right->Clone() : nullptr);
            break;
        }
        case OpCode::UserDefinedLogicOperator: {
            const auto& s = static_cast<const UserDefinedLogicOperator&>(*this);
            c = std::make_unique<UserDefinedLogicOperator>(s.MethodName, s.MethodDeclaringType,
                s.Left ? s.Left->Clone() : nullptr, s.Right ? s.Right->Clone() : nullptr);
            break;
        }

        // ---- compound-assignment nodes (Target/Value slots) ----
        case OpCode::NumericCompoundAssign: {
            const auto& s = static_cast<const NumericCompoundAssign&>(*this);
            auto clone = std::make_unique<NumericCompoundAssign>(s.Operator, s.CheckForOverflow,
                s.Sign, s.LeftInputType, s.RightInputType, s.UnderlyingResultTypeField, s.IsLifted,
                s.Type, s.EvalMode, s.Target ? s.Target->Clone() : nullptr, s.TargetKind,
                s.Value ? s.Value->Clone() : nullptr);
            c = std::move(clone);
            break;
        }
        case OpCode::UserDefinedCompoundAssign: {
            const auto& s = static_cast<const UserDefinedCompoundAssign&>(*this);
            c = std::make_unique<UserDefinedCompoundAssign>(s.MethodName, s.MethodDeclaringType,
                s.MethodReturnType, s.EvalMode, s.Target ? s.Target->Clone() : nullptr, s.TargetKind,
                s.Value ? s.Value->Clone() : nullptr);
            break;
        }

        // ---- memory nodes ----
        case OpCode::LdFlda: {
            const auto& s = static_cast<const LdFlda&>(*this);
            auto clone = std::make_unique<LdFlda>(s.Target ? s.Target->Clone() : nullptr, s.FieldName);
            clone->FieldToken = s.FieldToken;
            clone->IsCompilerGeneratedField = s.IsCompilerGeneratedField;
            clone->DelayExceptions = s.DelayExceptions;
            c = std::move(clone);
            break;
        }
        case OpCode::LdObj: {
            const auto& s = static_cast<const LdObj&>(*this);
            c = std::make_unique<LdObj>(s.Target ? s.Target->Clone() : nullptr, s.Type);
            break;
        }
        case OpCode::StObj: {
            const auto& s = static_cast<const StObj&>(*this);
            c = std::make_unique<StObj>(s.Target ? s.Target->Clone() : nullptr,
                s.Value ? s.Value->Clone() : nullptr, s.Type);
            break;
        }

        // ---- array nodes ----
        case OpCode::NewArr: {
            const auto& s = static_cast<const NewArr&>(*this);
            c = std::make_unique<NewArr>(s.Type, CloneChildren(s.Indices));
            break;
        }
        case OpCode::LdElema: {
            const auto& s = static_cast<const LdElema&>(*this);
            c = std::make_unique<LdElema>(s.Type, s.Array ? s.Array->Clone() : nullptr,
                CloneChildren(s.Indices));
            break;
        }

        // ---- control-flow / container nodes ----
        case OpCode::IfInstruction: {
            const auto& s = static_cast<const IfInstruction&>(*this);
            c = std::make_unique<IfInstruction>(s.Condition ? s.Condition->Clone() : nullptr,
                s.TrueInst ? s.TrueInst->Clone() : nullptr,
                s.FalseInst ? s.FalseInst->Clone() : nullptr);
            break;
        }
        case OpCode::NullCoalescingInstruction: {
            const auto& s = static_cast<const NullCoalescingInstruction&>(*this);
            auto clone = std::make_unique<NullCoalescingInstruction>(s.Kind,
                s.ValueInst ? s.ValueInst->Clone() : nullptr,
                s.FallbackInst ? s.FallbackInst->Clone() : nullptr);
            clone->UnderlyingResultType = s.UnderlyingResultType;
            c = std::move(clone);
            break;
        }
        case OpCode::Call: {
            const auto& s = static_cast<const Call&>(*this);
            auto clone = std::make_unique<Call>(s.MethodName);
            clone->ReturnType = s.ReturnType;
            clone->ReturnIType = s.ReturnIType;
            clone->ParameterIType = s.ParameterIType;
            clone->DeclaringType = s.DeclaringType;
            clone->IsInstanceCall = s.IsInstanceCall;
            clone->IsNewObj = s.IsNewObj;
            clone->IsOperator = s.IsOperator;
            clone->TypeArgumentsCount = s.TypeArgumentsCount;
            clone->IsLifted = s.IsLifted;
            for (auto& a : s.Arguments) clone->AddArg(a ? a->Clone() : nullptr);
            c = std::move(clone);
            break;
        }
        case OpCode::MatchInstruction: {
            const auto& s = static_cast<const MatchInstruction&>(*this);
            auto clone = std::make_unique<MatchInstruction>(s.Variable,
                s.TestedOperand ? s.TestedOperand->Clone() : nullptr);
            clone->CheckType = s.CheckType;
            clone->CheckNotNull = s.CheckNotNull;
            for (auto& p : s.SubPatterns) clone->AddSubPattern(p ? p->Clone() : nullptr);
            c = std::move(clone);
            break;
        }
        case OpCode::PinnedRegion: {
            const auto& s = static_cast<const PinnedRegion&>(*this);
            c = std::make_unique<PinnedRegion>(s.Variable,
                s.Init ? s.Init->Clone() : nullptr, s.Body ? s.Body->Clone() : nullptr);
            break;
        }
        case OpCode::LockInstruction: {
            const auto& s = static_cast<const LockInstruction&>(*this);
            c = std::make_unique<LockInstruction>(s.OnExpression ? s.OnExpression->Clone() : nullptr,
                s.Body ? s.Body->Clone() : nullptr);
            break;
        }
        case OpCode::UsingInstruction: {
            const auto& s = static_cast<const UsingInstruction&>(*this);
            auto clone = std::make_unique<UsingInstruction>(s.Variable,
                s.ResourceExpression ? s.ResourceExpression->Clone() : nullptr,
                s.Body ? s.Body->Clone() : nullptr);
            clone->IsAsync = s.IsAsync;
            clone->IsRefStruct = s.IsRefStruct;
            c = std::move(clone);
            break;
        }
        case OpCode::SwitchSection: {
            const auto& s = static_cast<const SwitchSection&>(*this);
            auto clone = std::make_unique<SwitchSection>(s.Labels);
            clone->HasNullLabel = s.HasNullLabel;
            clone->SetBody(s.Body ? s.Body->Clone() : nullptr);
            c = std::move(clone);
            break;
        }
        case OpCode::SwitchInstruction: {
            const auto& s = static_cast<const SwitchInstruction&>(*this);
            auto clone = std::make_unique<SwitchInstruction>(s.Value ? s.Value->Clone() : nullptr);
            clone->IsLifted = s.IsLifted;
            clone->Type = s.Type;
            for (auto& sec : s.Sections) {
                clone->AddSection(std::unique_ptr<SwitchSection>(
                    static_cast<SwitchSection*>(sec ? sec->Clone().release() : nullptr)));
            }
            c = std::move(clone);
            break;
        }
        case OpCode::TryCatchHandler: {
            const auto& s = static_cast<const TryCatchHandler&>(*this);
            c = std::make_unique<TryCatchHandler>(s.Filter ? s.Filter->Clone() : nullptr,
                s.Body ? s.Body->Clone() : nullptr, s.Variable);
            break;
        }
        case OpCode::TryCatch: {
            const auto& s = static_cast<const TryCatch&>(*this);
            auto clone = std::make_unique<TryCatch>(s.TryBlock ? s.TryBlock->Clone() : nullptr);
            for (auto& h : s.Handlers) {
                if (h) clone->AddHandler(std::unique_ptr<TryCatchHandler>(
                    static_cast<TryCatchHandler*>(h->Clone().release())));
            }
            c = std::move(clone);
            break;
        }
        case OpCode::TryFinally: {
            const auto& s = static_cast<const TryFinally&>(*this);
            c = std::make_unique<TryFinally>(s.TryBlock ? s.TryBlock->Clone() : nullptr,
                s.FinallyBlock ? s.FinallyBlock->Clone() : nullptr);
            break;
        }
        case OpCode::TryFault: {
            const auto& s = static_cast<const TryFault&>(*this);
            c = std::make_unique<TryFault>(s.TryBlock ? s.TryBlock->Clone() : nullptr,
                s.FaultBlock ? s.FaultBlock->Clone() : nullptr);
            break;
        }
        case OpCode::Block: {
            const auto& s = static_cast<const Block&>(*this);
            auto clone = std::make_unique<Block>();
            clone->Kind = s.Kind;
            clone->StartILOffset = s.StartILOffset;  // Block's own uint32 IL offset
            // IncomingEdgeCount stays 0: a disconnected clone has no incoming
            // edges until RecomputeIncomingEdgeCounts runs in its new tree.
            for (auto& inst : s.Instructions) clone->Add(inst ? inst->Clone() : nullptr);
            clone->SetFinal(s.FinalInstruction ? s.FinalInstruction->Clone() : nullptr);
            c = std::move(clone);
            break;
        }
        case OpCode::BlockContainer: {
            const auto& s = static_cast<const BlockContainer&>(*this);
            auto clone = std::make_unique<BlockContainer>();
            clone->Kind = s.Kind;
            for (auto& b : s.Blocks) {
                clone->AddBlock(std::unique_ptr<Block>(
                    static_cast<Block*>(b ? b->Clone().release() : nullptr)));
            }
            c = std::move(clone);
            break;
        }
        case OpCode::ILFunction: {
            const auto& s = static_cast<const ILFunction&>(*this);
            auto clone = std::make_unique<ILFunction>();
            clone->IsConstructor = s.IsConstructor;
            clone->IsStatic = s.IsStatic;
            clone->Variables = s.Variables;  // shared ILVariablePtr copies
            clone->Body.reset(static_cast<BlockContainer*>(
                s.Body ? s.Body->Clone().release() : nullptr));
            if (clone->Body) { clone->Body->Parent = clone.get(); clone->Body->ChildIndex = 0; }
            c = std::move(clone);
            break;
        }

        default:
            assert(!"ILInstruction::Clone: unhandled OpCode");
            c = std::make_unique<Nop>();  // release-degradation fallback (debug asserts)
            break;
    }
    // Propagate the IL byte range (StartILOffset/EndILOffset) for every node
    // except Block, which carries its own StartILOffset (copied above) and
    // does not use the base ILRange. ILFunction has no ILRange of its own.
    if (Op != OpCode::Block && Op != OpCode::ILFunction) {
        c->SetILRange(*this);
    }
    return c;
}

} // namespace ILSpy::Decompiler::IL
