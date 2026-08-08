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

#include "Decompiler/IL/BlockBuilder.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/RefAnyType.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/Metadata/ILOpCodes.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

using namespace ILSpy::Decompiler::Metadata;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {
// The reader's scratch state: a stack of pending expression trees (the decode
// of stack-machine IL into trees) and the committed stack-slot variables for
// values that survived a flush.
struct ReaderState {
    std::vector<std::unique_ptr<ILInstruction>> expressionStack;
    // currentStack: the committed evaluation stack (values that survived a
    // flush or were seeded by the runtime, e.g. an exception object in a catch).
    // When the expression stack is empty, Pop reads from currentStack.
    std::vector<ILVariablePtr> currentStack;
    std::size_t stackBase = 0;  // index where this block's own slots begin
    std::vector<ILVariablePtr> parameters;
    std::vector<ILVariablePtr> locals;
    StackType returnStackType = StackType::Void;

    // Evaluation-stack merge state (method-wide): the input stack recorded for
    // each block-start offset, slot-merge representatives, and every stack-slot
    // variable created by flushes.
    std::map<std::uint32_t, std::vector<ILVariablePtr>> incomingStacks;
    std::map<ILVariable*, ILVariablePtr> slotRepresentative;
    std::vector<ILVariablePtr> stackVarsCreated;
    int nextStackSlot = 0;
    bool mergeFailed = false;

    bool Push(std::unique_ptr<ILInstruction> inst) {
        if (!inst) return false;
        expressionStack.push_back(std::move(inst));
        return true;
    }
    std::unique_ptr<ILInstruction> Pop() {
        if (!expressionStack.empty()) {
            auto inst = std::move(expressionStack.back());
            expressionStack.pop_back();
            return inst;
        }
        if (currentStack.size() > stackBase) {
            auto v = currentStack.back();
            currentStack.pop_back();
            return std::make_unique<LdLoc>(v);
        }
        return nullptr;  // stack underflow -> caller bails
    }
};

// Commit pending expression-stack entries into fresh stack-slot variables, in
// evaluation (push) order, so values can cross a control-flow boundary without
// moving side effects past it.
void FlushExpressionStack(ReaderState& s, Block* block) {
    for (auto& expr : s.expressionStack) {
        auto v = std::make_shared<ILVariable>();
        v->Name = "S_" + std::to_string(s.nextStackSlot++);
        v->Kind = VariableKind::StackSlot;
        block->Add(std::make_unique<StLoc>(v, std::move(expr)));
        s.currentStack.push_back(v);
        s.stackVarsCreated.push_back(v);
    }
    s.expressionStack.clear();
    // Flushing at a terminal runs after SetFinal; the final sits behind the
    // instructions list in the child layout, so its ChildIndex tracks the new
    // size.
    if (block->FinalInstruction)
        block->FinalInstruction->ChildIndex = static_cast<int>(block->Instructions.size());
}

// Follow the merge-representative chain for a stack-slot variable. Merges
// always point an incoming variable at an already-recorded representative, so
// chains stay short and deterministic (first-recorded wins).
ILVariablePtr FindSlotRep(const ReaderState& s, ILVariablePtr v) {
    ILVariablePtr found = v;
    std::size_t hops = 0;
    while (found) {
        auto it = s.slotRepresentative.find(found.get());
        if (it == s.slotRepresentative.end()) break;
        if (it->second.get() == found.get()) break;
        found = it->second;
        if (++hops > s.slotRepresentative.size()) break;  // defensive
    }
    return found;
}

// Record/merge the outgoing evaluation stack at a control-flow edge targeting
// the block starting at `target`. The first edge records the snapshot; later
// edges unify their slots with the recorded representatives slot-wise
// (compatible stack types only; the C# inserts a conversion StLoc for the
// I4/I and F4/F8 pairs, we treat them as mergeable without the conversion).
void MergeStackIntoTarget(ReaderState& s, std::uint32_t target,
                          const std::vector<ILVariablePtr>& stack) {
    auto it = s.incomingStacks.find(target);
    if (it == s.incomingStacks.end()) {
        s.incomingStacks[target] = stack;
        return;
    }
    auto& recorded = it->second;
    if (recorded.size() != stack.size()) {
        s.mergeFailed = true;  // invalid IL: mismatched stack heights at a join
        return;
    }
    for (std::size_t i = 0; i < stack.size(); ++i) {
        ILVariablePtr rec = FindSlotRep(s, recorded[i]);
        ILVariablePtr inc = stack[i];
        if (rec.get() == inc.get()) continue;
        StackType a = StackTypeOf(rec ? rec->Type : nullptr);
        StackType t = StackTypeOf(inc ? inc->Type : nullptr);
        bool mergeable = (a == t) || a == StackType::Unknown || t == StackType::Unknown ||
                         (a == StackType::I4 && t == StackType::I) ||
                         (a == StackType::I && t == StackType::I4) ||
                         (a == StackType::F4 && t == StackType::F8) ||
                         (a == StackType::F8 && t == StackType::F4);
        if (!mergeable) {
            s.mergeFailed = true;
            return;
        }
        s.slotRepresentative[inc.get()] = rec;
    }
}

// Replace merged stack-slot variables with their representatives across the
// tree (loads, stores, address-ofs).
void RemapMergedVariables(const ReaderState& s, ILInstruction* inst) {
    if (!inst) return;
    ILVariablePtr* slot = nullptr;
    if (inst->Op == OpCode::StLoc) slot = &static_cast<StLoc*>(inst)->Variable;
    else if (inst->Op == OpCode::LdLoc) slot = &static_cast<LdLoc*>(inst)->Variable;
    else if (inst->Op == OpCode::LdLoca) slot = &static_cast<LdLoca*>(inst)->Variable;
    if (slot && *slot) *slot = FindSlotRep(s, *slot);
    for (int i = 0; i < inst->ChildCount(); ++i) RemapMergedVariables(s, inst->GetChild(i));
}

bool ReadU8(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint8_t& out) {
    if (pos >= size) return false; out = b[pos]; return true;
}
bool ReadI8(const std::uint8_t* b, std::size_t size, std::size_t pos, std::int8_t& out) {
    if (pos + 1 > size) return false; out = static_cast<std::int8_t>(b[pos]); return true;
}
bool ReadU16(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint16_t& out) {
    if (pos + 2 > size) return false;
    out = static_cast<std::uint16_t>(b[pos]) | (static_cast<std::uint16_t>(b[pos + 1]) << 8);
    return true;
}
bool ReadI32(const std::uint8_t* b, std::size_t size, std::size_t pos, std::int32_t& out) {
    if (pos + 4 > size) return false;
    out = static_cast<std::int32_t>(static_cast<std::uint32_t>(b[pos]) |
        (static_cast<std::uint32_t>(b[pos + 1]) << 8) |
        (static_cast<std::uint32_t>(b[pos + 2]) << 16) |
        (static_cast<std::uint32_t>(b[pos + 3]) << 24));
    return true;
}
bool ReadU32(const std::uint8_t* b, std::size_t size, std::size_t pos, std::uint32_t& out) {
    if (pos + 4 > size) return false;
    out = static_cast<std::uint32_t>(b[pos]) | (static_cast<std::uint32_t>(b[pos + 1]) << 8) |
         (static_cast<std::uint32_t>(b[pos + 2]) << 16) | (static_cast<std::uint32_t>(b[pos + 3]) << 24);
    return true;
}

// Read a branch target (short or long) and return the absolute target offset.
// Returns false on a truncated operand.
bool ReadBranchTarget(const std::uint8_t* b, std::size_t size, std::size_t& pos,
                      bool isShort, std::size_t instStart, std::uint32_t& target) {
    if (isShort) {
        std::int8_t rel = 0;
        if (!ReadI8(b, size, pos, rel)) return false;
        pos += 1;
        target = static_cast<std::uint32_t>(static_cast<std::ptrdiff_t>(pos) + rel);
    } else {
        std::int32_t rel = 0;
        if (!ReadI32(b, size, pos, rel)) return false;
        pos += 4;
        target = static_cast<std::uint32_t>(static_cast<std::ptrdiff_t>(pos) + rel);
    }
    (void)instStart;
    return true;
}

void InitParameters(ReaderState& s, const MethodSignature& sig) {
    int first = sig.IsInstance ? 1 : 0;
    int n = static_cast<int>(sig.ParameterTypes.size()) + first;
    s.parameters.resize(n);
    int idx = 0;
    if (sig.IsInstance) {
        auto v = std::make_shared<ILVariable>();
        v->Name = "this";
        v->Kind = VariableKind::Parameter;
        v->Type = nullptr;
        v->Index = 0;
        s.parameters[0] = v;
        idx = 1;
    }
    for (const auto& t : sig.ParameterTypes) {
        auto v = std::make_shared<ILVariable>();
        v->Name = "arg_" + std::to_string(idx);
        v->Kind = VariableKind::Parameter;
        v->Type = t;
        v->Index = idx;
        s.parameters[idx] = v;
        ++idx;
    }
}

ILVariablePtr GetOrCreateLocal(ReaderState& s, int idx) {
    if (idx >= static_cast<int>(s.locals.size())) s.locals.resize(idx + 1);
    if (!s.locals[idx]) {
        auto v = std::make_shared<ILVariable>();
        v->Name = "V_" + std::to_string(idx);
        v->Kind = VariableKind::Local;
        v->Index = idx;
        s.locals[idx] = v;
    }
    return s.locals[idx];
}

StackType ReturnStackTypeOf(const ITypePtr& t) { return StackTypeOf(t); }

// Outcome of decoding one instruction.
enum class DecodeOutcome {
    Continue,     // pushed/added a statement; keep going
    Bail,         // unsupported opcode / stack underflow / malformed
    Terminal,     // block final set to a Leave/Throw (ret/throw); block ends
    BranchInstr,  // block final set to a Branch/IfInstruction; block ends
    SwitchInstr,  // block final set to a SwitchInstruction; block ends
};

// Decode one instruction, mutating `s` (push/pop) and `block` (add statements /
// set final). `container` is the function body container for `ret` -> Leave.
// `b/size/pos` is the IL stream; `start` is this instruction's offset. Returns
// the outcome. On Bail, pos may be partially advanced; the caller degrades.
DecodeOutcome DecodeOne(const MetadataFile& file, ReaderState& s, Block* block,
                        BlockContainer* container, ILOpCode op,
                        const std::uint8_t* b, std::size_t size, std::size_t& pos,
                        std::size_t start) {
    switch (op) {
        // ---- constants ----
        case ILOpCode::Ldnull:
            if (!s.Push(std::make_unique<LdNull>())) return DecodeOutcome::Bail;
            break;
        case ILOpCode::Ldc_i4_0: if (!s.Push(std::make_unique<LdcI4>(0))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_1: if (!s.Push(std::make_unique<LdcI4>(1))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_2: if (!s.Push(std::make_unique<LdcI4>(2))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_3: if (!s.Push(std::make_unique<LdcI4>(3))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_4: if (!s.Push(std::make_unique<LdcI4>(4))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_5: if (!s.Push(std::make_unique<LdcI4>(5))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_6: if (!s.Push(std::make_unique<LdcI4>(6))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_7: if (!s.Push(std::make_unique<LdcI4>(7))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_8: if (!s.Push(std::make_unique<LdcI4>(8))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_m1: if (!s.Push(std::make_unique<LdcI4>(-1))) return DecodeOutcome::Bail; break;
        case ILOpCode::Ldc_i4_s: {
            std::int8_t v = 0; if (!ReadI8(b, size, pos, v)) return DecodeOutcome::Bail; pos += 1;
            if (!s.Push(std::make_unique<LdcI4>(static_cast<std::int32_t>(v)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldc_i4: {
            std::int32_t v = 0; if (!ReadI32(b, size, pos, v)) return DecodeOutcome::Bail; pos += 4;
            if (!s.Push(std::make_unique<LdcI4>(v))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldc_i8: {
            if (pos + 8 > size) return DecodeOutcome::Bail;
            std::int64_t v = 0;
            for (int i = 0; i < 8; ++i) v |= static_cast<std::int64_t>(b[pos + i]) << (8 * i);
            pos += 8;
            if (!s.Push(std::make_unique<LdcI8>(v))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldc_r4: {
            if (pos + 4 > size) return DecodeOutcome::Bail;
            std::uint32_t u = static_cast<std::uint32_t>(b[pos])
                | (static_cast<std::uint32_t>(b[pos + 1]) << 8)
                | (static_cast<std::uint32_t>(b[pos + 2]) << 16)
                | (static_cast<std::uint32_t>(b[pos + 3]) << 24);
            pos += 4;
            float fv; std::memcpy(&fv, &u, sizeof(fv));
            if (!s.Push(std::make_unique<LdcF4>(fv))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldc_r8: {
            if (pos + 8 > size) return DecodeOutcome::Bail;
            std::uint64_t u = 0;
            for (int i = 0; i < 8; ++i) u |= static_cast<std::uint64_t>(b[pos + i]) << (8 * i);
            pos += 8;
            double dv; std::memcpy(&dv, &u, sizeof(dv));
            if (!s.Push(std::make_unique<LdcF8>(dv))) return DecodeOutcome::Bail;
            break;
        }

        // ---- arguments ----
        case ILOpCode::Ldarg_0: case ILOpCode::Ldarg_1:
        case ILOpCode::Ldarg_2: case ILOpCode::Ldarg_3: {
            int idx = static_cast<int>(op) - static_cast<int>(ILOpCode::Ldarg_0);
            if (idx >= static_cast<int>(s.parameters.size())) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdLoc>(s.parameters[idx]))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldarg_s: {
            std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return DecodeOutcome::Bail; pos += 1;
            if (idx >= s.parameters.size()) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdLoc>(s.parameters[idx]))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldarg: {
            std::uint16_t idx = 0; if (!ReadU16(b, size, pos, idx)) return DecodeOutcome::Bail; pos += 2;
            if (idx >= s.parameters.size()) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdLoc>(s.parameters[idx]))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldarga: {
            std::uint16_t idx = 0; if (!ReadU16(b, size, pos, idx)) return DecodeOutcome::Bail; pos += 2;
            if (idx >= s.parameters.size()) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdLoca>(s.parameters[idx]))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Starg: case ILOpCode::Starg_s: {
            std::uint16_t idx = 0;
            if (op == ILOpCode::Starg_s) {
                std::uint8_t i8 = 0; if (!ReadU8(b, size, pos, i8)) return DecodeOutcome::Bail; pos += 1; idx = i8;
            } else {
                if (!ReadU16(b, size, pos, idx)) return DecodeOutcome::Bail; pos += 2;
            }
            if (idx >= s.parameters.size()) return DecodeOutcome::Bail;
            auto value = s.Pop();
            if (!value) return DecodeOutcome::Bail;
            block->Add(std::make_unique<StLoc>(s.parameters[idx], std::move(value)));
            break;
        }

        // ---- locals ----
        case ILOpCode::Ldloc_0: case ILOpCode::Ldloc_1:
        case ILOpCode::Ldloc_2: case ILOpCode::Ldloc_3: {
            int idx = static_cast<int>(op) - static_cast<int>(ILOpCode::Ldloc_0);
            auto v = GetOrCreateLocal(s, idx);
            if (!s.Push(std::make_unique<LdLoc>(v))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Stloc_0: case ILOpCode::Stloc_1:
        case ILOpCode::Stloc_2: case ILOpCode::Stloc_3: {
            int idx = static_cast<int>(op) - static_cast<int>(ILOpCode::Stloc_0);
            auto v = GetOrCreateLocal(s, idx);
            auto value = s.Pop();
            if (!value) return DecodeOutcome::Bail;
            block->Add(std::make_unique<StLoc>(v, std::move(value)));
            break;
        }
        case ILOpCode::Ldloc_s: {
            std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return DecodeOutcome::Bail; pos += 1;
            auto v = GetOrCreateLocal(s, idx);
            if (!s.Push(std::make_unique<LdLoc>(v))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Stloc_s: {
            std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return DecodeOutcome::Bail; pos += 1;
            auto v = GetOrCreateLocal(s, idx);
            auto value = s.Pop();
            if (!value) return DecodeOutcome::Bail;
            block->Add(std::make_unique<StLoc>(v, std::move(value)));
            break;
        }
        case ILOpCode::Ldloca_s: {
            std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return DecodeOutcome::Bail; pos += 1;
            auto v = GetOrCreateLocal(s, idx);
            if (!s.Push(std::make_unique<LdLoca>(v))) return DecodeOutcome::Bail;
            break;
        }

        // ---- pop/dup ----
        case ILOpCode::Pop: {
            auto v = s.Pop();
            if (!v) return DecodeOutcome::Bail;
            break;  // discard
        }
        case ILOpCode::Dup: {
            if (s.expressionStack.empty()) {
                // Duplicating a committed stack-slot value is just another load
                // of the slot (the C# Peek does the same via currentStack).
                if (s.currentStack.empty()) return DecodeOutcome::Bail;
                if (!s.Push(std::make_unique<LdLoc>(s.currentStack.back()))) return DecodeOutcome::Bail;
                break;
            }
            auto top = s.Pop();
            if (!top) return DecodeOutcome::Bail;
            auto v = std::make_shared<ILVariable>();
            v->Name = "dup_" + std::to_string(start);
            v->Kind = VariableKind::StackSlot;
            s.stackVarsCreated.push_back(v);
            block->Add(std::make_unique<StLoc>(v, std::move(top)));
            if (!s.Push(std::make_unique<LdLoc>(v))) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdLoc>(v))) return DecodeOutcome::Bail;
            break;
        }

        // ---- call/callvirt/newobj ----
        case ILOpCode::Call: case ILOpCode::Callvirt: case ILOpCode::Newobj: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto callSig = file.GetMethodSignature(tok);
            if (!callSig) return DecodeOutcome::Bail;
            int argCount = static_cast<int>(callSig->ParameterTypes.size()) + (callSig->IsInstance ? 1 : 0);
            if (op == ILOpCode::Newobj) argCount = static_cast<int>(callSig->ParameterTypes.size());
            auto call = std::make_unique<Call>(file.ResolveTokenToString(tok));
            call->ReturnType = StackTypeOf(callSig->ReturnType);
            std::vector<std::unique_ptr<ILInstruction>> args;
            args.reserve(argCount);
            for (int i = 0; i < argCount; ++i) {
                auto a = s.Pop();
                if (!a) return DecodeOutcome::Bail;
                args.push_back(std::move(a));
            }
            for (auto it = args.rbegin(); it != args.rend(); ++it) call->AddArg(std::move(*it));
            if (op == ILOpCode::Newobj) {
                // newobj leaves the constructed object on the stack regardless
                // of the constructor's declared void return.
                call->ReturnType = StackType::O;
                if (!s.Push(std::move(call))) return DecodeOutcome::Bail;
            } else {
                bool returnsVoid = (callSig->ReturnType &&
                                    callSig->ReturnType->ReflectionName() == "System.Void");
                if (returnsVoid) block->Add(std::move(call));
                else if (!s.Push(std::move(call))) return DecodeOutcome::Bail;
            }
            break;
        }

        case ILOpCode::Ldstr: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            if (!s.Push(std::make_unique<LdStr>(file.ResolveTokenToString(tok)))) return DecodeOutcome::Bail;
            break;
        }

        // ---- ret / throw (terminal) ----
        case ILOpCode::Ret: {
            std::unique_ptr<ILInstruction> retVal;
            if (s.returnStackType != StackType::Void) {
                retVal = s.Pop();
                if (!retVal) return DecodeOutcome::Bail;
            }
            block->SetFinal(std::make_unique<Leave>(container, std::move(retVal)));
            return DecodeOutcome::Terminal;
        }
        case ILOpCode::Throw: {
            auto exc = s.Pop();
            if (!exc) return DecodeOutcome::Bail;
            block->SetFinal(std::make_unique<Throw>(std::move(exc)));
            return DecodeOutcome::Terminal;
        }
        case ILOpCode::Rethrow:
            block->SetFinal(std::make_unique<Rethrow>());
            return DecodeOutcome::Terminal;

        // ---- refanytype: TypedReference -> its type handle ----
        case ILOpCode::Refanytype: {
            auto v = s.Pop();
            if (!v) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<RefAnyType>(std::move(v)))) return DecodeOutcome::Bail;
            break;
        }

        case ILOpCode::Nop:
        case ILOpCode::Break:
            break;

        // ---- binary arithmetic / bitwise / shift ----
#define IL_BIN(opc, oper) \
    case ILOpCode::opc: { \
        auto r = s.Pop(); auto l = s.Pop(); \
        if (!l || !r) return DecodeOutcome::Bail; \
        if (!s.Push(std::make_unique<BinaryNumericInstruction>(std::move(l), std::move(r), \
            BinaryNumericOperator::oper, StackType::I4))) return DecodeOutcome::Bail; \
        break; \
    }
        IL_BIN(Add, Add) IL_BIN(Add_ovf, Add) IL_BIN(Add_ovf_un, Add)
        IL_BIN(Sub, Sub) IL_BIN(Sub_ovf, Sub) IL_BIN(Sub_ovf_un, Sub)
        IL_BIN(Mul, Mul) IL_BIN(Mul_ovf, Mul) IL_BIN(Mul_ovf_un, Mul)
        IL_BIN(Div, Div) IL_BIN(Div_un, Div)
        IL_BIN(Rem, Rem) IL_BIN(Rem_un, Rem)
        IL_BIN(And, BitAnd) IL_BIN(Or, BitOr) IL_BIN(Xor, BitXor)
        IL_BIN(Shl, ShiftLeft) IL_BIN(Shr, ShiftRight) IL_BIN(Shr_un, ShiftRight)
#undef IL_BIN

#define IL_CMP(opc, kind, uns) \
    case ILOpCode::opc: { \
        auto r = s.Pop(); auto l = s.Pop(); \
        if (!l || !r) return DecodeOutcome::Bail; \
        if (!s.Push(std::make_unique<Comp>(std::move(l), std::move(r), \
            ComparisonKind::kind, uns))) return DecodeOutcome::Bail; \
        break; \
    }
        IL_CMP(Ceq, Equality, false)
        IL_CMP(Cgt, GreaterThan, false)
        IL_CMP(Cgt_un, GreaterThan, true)
        IL_CMP(Clt, LessThan, false)
        IL_CMP(Clt_un, LessThan, true)
#undef IL_CMP

        // ---- neg/not (unary) -> emit via BinaryNumeric with zero, or a Conv ----
        case ILOpCode::Neg: {
            auto v = s.Pop(); if (!v) return DecodeOutcome::Bail;
            auto zero = std::make_unique<LdcI4>(0);
            if (!s.Push(std::make_unique<BinaryNumericInstruction>(std::move(zero), std::move(v),
                BinaryNumericOperator::Sub, StackType::I4))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Not: {
            auto v = s.Pop(); if (!v) return DecodeOutcome::Bail;
            // Model as xor with -1 (all bits set) for I4; approximate but keeps the tree.
            if (!s.Push(std::make_unique<BinaryNumericInstruction>(
                std::make_unique<LdcI4>(-1), std::move(v),
                BinaryNumericOperator::BitXor, StackType::I4))) return DecodeOutcome::Bail;
            break;
        }

#define IL_CONV(opc, target) \
    case ILOpCode::opc: { \
        auto v = s.Pop(); \
        if (!v) return DecodeOutcome::Bail; \
        if (!s.Push(std::make_unique<Conv>(std::move(v), StackType::target, false))) return DecodeOutcome::Bail; \
        break; \
    }
        IL_CONV(Conv_i1, I4) IL_CONV(Conv_i2, I4) IL_CONV(Conv_i4, I4)
        IL_CONV(Conv_u1, I4) IL_CONV(Conv_u2, I4) IL_CONV(Conv_u4, I4)
        IL_CONV(Conv_i8, I8) IL_CONV(Conv_u8, I8)
        IL_CONV(Conv_r4, F4) IL_CONV(Conv_r8, F8)
        IL_CONV(Conv_i, I) IL_CONV(Conv_u, I) IL_CONV(Conv_r_un, F8)
#undef IL_CONV

        // ---- cast/isinst/box/unbox ----
        case ILOpCode::Castclass: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto v = s.Pop(); if (!v) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<CastClass>(type, std::move(v)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Isinst: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto v = s.Pop(); if (!v) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<IsInst>(type, std::move(v)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Box: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto v = s.Pop(); if (!v) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<Box>(type, std::move(v)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Unbox: case ILOpCode::Unbox_any: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto v = s.Pop(); if (!v) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<UnboxAny>(type, std::move(v)))) return DecodeOutcome::Bail;
            break;
        }

        // ---- arrays ----
        case ILOpCode::Newarr: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto count = s.Pop(); if (!count) return DecodeOutcome::Bail;
            std::vector<std::unique_ptr<ILInstruction>> idx;
            idx.push_back(std::move(count));
            if (!s.Push(std::make_unique<NewArr>(type, std::move(idx)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldlen: {
            auto arr = s.Pop(); if (!arr) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdLen>(std::move(arr)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldelema: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto idx = s.Pop(); auto arr = s.Pop();
            if (!arr || !idx) return DecodeOutcome::Bail;
            std::vector<std::unique_ptr<ILInstruction>> indices;
            indices.push_back(std::move(idx));
            if (!s.Push(std::make_unique<LdElema>(type, std::move(arr), std::move(indices)))) return DecodeOutcome::Bail;
            break;
        }
#define IL_LDELEM(opc, kt) \
    case ILOpCode::opc: { \
        auto type = std::make_shared<KnownType>(KnownTypeCode::kt); \
        auto idx = s.Pop(); auto arr = s.Pop(); \
        if (!arr || !idx) return DecodeOutcome::Bail; \
        std::vector<std::unique_ptr<ILInstruction>> indices; \
        indices.push_back(std::move(idx)); \
        auto addr = std::make_unique<LdElema>(type, std::move(arr), std::move(indices)); \
        if (!s.Push(std::make_unique<LdObj>(std::move(addr), type))) return DecodeOutcome::Bail; \
        break; \
    }
        IL_LDELEM(Ldelem_i1, SByte) IL_LDELEM(Ldelem_u1, Byte)
        IL_LDELEM(Ldelem_i2, Int16) IL_LDELEM(Ldelem_u2, UInt16)
        IL_LDELEM(Ldelem_i4, Int32) IL_LDELEM(Ldelem_u4, UInt32)
        IL_LDELEM(Ldelem_i8, Int64)
        IL_LDELEM(Ldelem_r4, Single) IL_LDELEM(Ldelem_r8, Double)
        IL_LDELEM(Ldelem_i, IntPtr) IL_LDELEM(Ldelem_ref, Object)
#undef IL_LDELEM
        case ILOpCode::Ldelem: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto idx = s.Pop(); auto arr = s.Pop();
            if (!arr || !idx) return DecodeOutcome::Bail;
            std::vector<std::unique_ptr<ILInstruction>> indices;
            indices.push_back(std::move(idx));
            auto addr = std::make_unique<LdElema>(type, std::move(arr), std::move(indices));
            if (!s.Push(std::make_unique<LdObj>(std::move(addr), type))) return DecodeOutcome::Bail;
            break;
        }
#define IL_STELEM(opc, kt) \
    case ILOpCode::opc: { \
        auto type = std::make_shared<KnownType>(KnownTypeCode::kt); \
        auto val = s.Pop(); auto idx = s.Pop(); auto arr = s.Pop(); \
        if (!arr || !idx || !val) return DecodeOutcome::Bail; \
        std::vector<std::unique_ptr<ILInstruction>> indices; \
        indices.push_back(std::move(idx)); \
        auto addr = std::make_unique<LdElema>(type, std::move(arr), std::move(indices)); \
        block->Add(std::make_unique<StObj>(std::move(addr), std::move(val), type)); \
        break; \
    }
        IL_STELEM(Stelem_i, IntPtr) IL_STELEM(Stelem_i1, SByte)
        IL_STELEM(Stelem_i2, Int16) IL_STELEM(Stelem_i4, Int32)
        IL_STELEM(Stelem_i8, Int64) IL_STELEM(Stelem_r4, Single)
        IL_STELEM(Stelem_r8, Double) IL_STELEM(Stelem_ref, Object)
#undef IL_STELEM
        case ILOpCode::Stelem: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto val = s.Pop(); auto idx = s.Pop(); auto arr = s.Pop();
            if (!arr || !idx || !val) return DecodeOutcome::Bail;
            std::vector<std::unique_ptr<ILInstruction>> indices;
            indices.push_back(std::move(idx));
            auto addr = std::make_unique<LdElema>(type, std::move(arr), std::move(indices));
            block->Add(std::make_unique<StObj>(std::move(addr), std::move(val), type));
            break;
        }

        // ---- indirect loads/stores ----
#define IL_LDIND(opc, kt) \
    case ILOpCode::opc: { \
        auto type = std::make_shared<KnownType>(KnownTypeCode::kt); \
        auto ptr = s.Pop(); \
        if (!ptr) return DecodeOutcome::Bail; \
        if (!s.Push(std::make_unique<LdObj>(std::move(ptr), type))) return DecodeOutcome::Bail; \
        break; \
    }
        IL_LDIND(Ldind_i1, SByte) IL_LDIND(Ldind_u1, Byte)
        IL_LDIND(Ldind_i2, Int16) IL_LDIND(Ldind_u2, UInt16)
        IL_LDIND(Ldind_i4, Int32) IL_LDIND(Ldind_u4, UInt32)
        IL_LDIND(Ldind_i8, Int64) IL_LDIND(Ldind_i, IntPtr)
        IL_LDIND(Ldind_r4, Single) IL_LDIND(Ldind_r8, Double)
        IL_LDIND(Ldind_ref, Object)
#undef IL_LDIND
#define IL_STIND(opc, kt) \
    case ILOpCode::opc: { \
        auto type = std::make_shared<KnownType>(KnownTypeCode::kt); \
        auto val = s.Pop(); auto ptr = s.Pop(); \
        if (!ptr || !val) return DecodeOutcome::Bail; \
        block->Add(std::make_unique<StObj>(std::move(ptr), std::move(val), type)); \
        break; \
    }
        IL_STIND(Stind_i1, SByte) IL_STIND(Stind_i2, Int16)
        IL_STIND(Stind_i4, Int32) IL_STIND(Stind_i8, Int64)
        IL_STIND(Stind_r4, Single) IL_STIND(Stind_r8, Double)
        IL_STIND(Stind_ref, Object) IL_STIND(Stind_i, IntPtr)
#undef IL_STIND

        // ---- field access ----
        case ILOpCode::Ldfld:
        case ILOpCode::Ldflda:
        case ILOpCode::Stfld:
        case ILOpCode::Ldsfld:
        case ILOpCode::Ldsflda:
        case ILOpCode::Stsfld: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto fieldType = file.GetFieldSignature(tok);
            std::string fieldName = file.ResolveTokenToString(tok);
            if (op == ILOpCode::Ldfld || op == ILOpCode::Ldflda || op == ILOpCode::Stfld) {
                // stfld pops value (top) then target; the loads pop only target.
                std::unique_ptr<ILInstruction> storeValue;
                if (op == ILOpCode::Stfld) {
                    storeValue = s.Pop();
                    if (!storeValue) return DecodeOutcome::Bail;
                }
                auto target = s.Pop();
                if (!target) return DecodeOutcome::Bail;
                auto addr = std::make_unique<LdFlda>(std::move(target), fieldName);
                addr->DelayExceptions = (op != ILOpCode::Ldflda);
                if (op == ILOpCode::Ldflda) {
                    if (!s.Push(std::move(addr))) return DecodeOutcome::Bail;
                } else if (op == ILOpCode::Ldfld) {
                    if (!s.Push(std::make_unique<LdObj>(std::move(addr), fieldType))) return DecodeOutcome::Bail;
                } else {
                    block->Add(std::make_unique<StObj>(std::move(addr), std::move(storeValue), fieldType));
                }
            } else {
                auto addr = std::make_unique<LdsFlda>(fieldName);
                if (op == ILOpCode::Ldsflda) {
                    if (!s.Push(std::move(addr))) return DecodeOutcome::Bail;
                } else if (op == ILOpCode::Ldsfld) {
                    if (!s.Push(std::make_unique<LdObj>(std::move(addr), fieldType))) return DecodeOutcome::Bail;
                } else {
                    auto value = s.Pop();
                    if (!value) return DecodeOutcome::Bail;
                    block->Add(std::make_unique<StObj>(std::move(addr), std::move(value), fieldType));
                }
            }
            break;
        }

        // ---- unconditional branches (br/br.s) ----
        case ILOpCode::Br: case ILOpCode::Br_s: {
            bool isShort = (op == ILOpCode::Br_s);
            std::uint32_t target = 0;
            if (!ReadBranchTarget(b, size, pos, isShort, start, target)) return DecodeOutcome::Bail;
            FlushExpressionStack(s, block);
            MergeStackIntoTarget(s, target, s.currentStack);
            block->SetFinal(std::make_unique<Branch>(target));
            return DecodeOutcome::BranchInstr;
        }

        // ---- conditional branches (brtrue/brfalse + beq..blt + _s/_un) ----
        case ILOpCode::Brtrue: case ILOpCode::Brtrue_s:
        case ILOpCode::Brfalse: case ILOpCode::Brfalse_s: {
            bool isShort = (op == ILOpCode::Brtrue_s || op == ILOpCode::Brfalse_s);
            std::uint32_t target = 0;
            if (!ReadBranchTarget(b, size, pos, isShort, start, target)) return DecodeOutcome::Bail;
            auto cond = s.Pop();
            if (!cond) return DecodeOutcome::Bail;
            bool negate = (op == ILOpCode::Brfalse || op == ILOpCode::Brfalse_s);
            std::unique_ptr<ILInstruction> condition;
            if (!negate) {
                condition = std::move(cond);
            } else {
                // brfalse: branch if cond == 0/null. Emit Comp(Equality, cond, zero).
                auto zero = (cond->ResultType() == StackType::O)
                    ? std::unique_ptr<ILInstruction>(std::make_unique<LdNull>())
                    : std::unique_ptr<ILInstruction>(std::make_unique<LdcI4>(0));
                condition = std::make_unique<Comp>(std::move(cond), std::move(zero),
                                                   ComparisonKind::Equality, false);
            }
            FlushExpressionStack(s, block);
            MergeStackIntoTarget(s, target, s.currentStack);
            if (pos < size)
                MergeStackIntoTarget(s, static_cast<std::uint32_t>(pos), s.currentStack);
            block->SetFinal(std::make_unique<IfInstruction>(std::move(condition),
                                                           std::make_unique<Branch>(target)));
            return DecodeOutcome::BranchInstr;
        }
#define IL_CBR(opc, kind, uns, isShort) \
    case ILOpCode::opc: { \
        std::uint32_t target = 0; \
        if (!ReadBranchTarget(b, size, pos, isShort, start, target)) return DecodeOutcome::Bail; \
        auto r = s.Pop(); auto l = s.Pop(); \
        if (!l || !r) return DecodeOutcome::Bail; \
        auto comp = std::make_unique<Comp>(std::move(l), std::move(r), ComparisonKind::kind, uns); \
        FlushExpressionStack(s, block); \
        MergeStackIntoTarget(s, target, s.currentStack); \
        if (pos < size) \
            MergeStackIntoTarget(s, static_cast<std::uint32_t>(pos), s.currentStack); \
        block->SetFinal(std::make_unique<IfInstruction>(std::move(comp), \
            std::make_unique<Branch>(target))); \
        return DecodeOutcome::BranchInstr; \
    }
        IL_CBR(Beq, Equality, false, false) IL_CBR(Beq_s, Equality, false, true)
        IL_CBR(Bge, GreaterThanOrEqual, false, false) IL_CBR(Bge_s, GreaterThanOrEqual, false, true)
        IL_CBR(Bgt, GreaterThan, false, false) IL_CBR(Bgt_s, GreaterThan, false, true)
        IL_CBR(Ble, LessThanOrEqual, false, false) IL_CBR(Ble_s, LessThanOrEqual, false, true)
        IL_CBR(Blt, LessThan, false, false) IL_CBR(Blt_s, LessThan, false, true)
        IL_CBR(Bne_un, Inequality, true, false) IL_CBR(Bne_un_s, Inequality, true, true)
        IL_CBR(Bge_un, GreaterThanOrEqual, true, false) IL_CBR(Bge_un_s, GreaterThanOrEqual, true, true)
        IL_CBR(Bgt_un, GreaterThan, true, false) IL_CBR(Bgt_un_s, GreaterThan, true, true)
        IL_CBR(Ble_un, LessThanOrEqual, true, false) IL_CBR(Ble_un_s, LessThanOrEqual, true, true)
        IL_CBR(Blt_un, LessThan, true, false) IL_CBR(Blt_un_s, LessThan, true, true)
#undef IL_CBR

        case ILOpCode::Switch: {
            std::uint32_t n = 0;
            if (!ReadU32(b, size, pos, n)) return DecodeOutcome::Bail;
            pos += 4;
            std::size_t base = pos + static_cast<std::size_t>(n) * 4;
            // Read the n relative target offsets.
            std::vector<std::uint32_t> targets;
            targets.reserve(n);
            for (std::uint32_t i = 0; i < n; ++i) {
                std::int32_t rel = 0;
                if (!ReadI32(b, size, pos, rel)) return DecodeOutcome::Bail;
                pos += 4;
                targets.push_back(static_cast<std::uint32_t>(base + rel));
            }
            auto value = s.Pop();
            if (!value) return DecodeOutcome::Bail;
            FlushExpressionStack(s, block);
            for (auto t : targets) MergeStackIntoTarget(s, t, s.currentStack);
            if (pos < size)
                MergeStackIntoTarget(s, static_cast<std::uint32_t>(pos), s.currentStack);
            auto sw = std::make_unique<SwitchInstruction>(std::move(value));
            for (std::uint32_t i = 0; i < n; ++i) {
                auto sec = std::make_unique<SwitchSection>();
                sec->Labels.insert(static_cast<std::int64_t>(i));
                sec->SetBody(std::make_unique<Branch>(targets[i]));
                sw->AddSection(std::move(sec));
            }
            // Default section: fall through to the instruction after the switch.
            auto def = std::make_unique<SwitchSection>();
            def->SetBody(std::make_unique<Branch>(static_cast<std::uint32_t>(pos)));
            sw->AddSection(std::move(def));
            block->SetFinal(std::move(sw));
            return DecodeOutcome::SwitchInstr;
        }

        // ---- ldobj / stobj / initobj (typed memory ops with a type token) ----
        case ILOpCode::Ldobj: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto ptr = s.Pop(); if (!ptr) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdObj>(std::move(ptr), type))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Stobj: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto val = s.Pop(); auto ptr = s.Pop();
            if (!ptr || !val) return DecodeOutcome::Bail;
            block->Add(std::make_unique<StObj>(std::move(ptr), std::move(val), type));
            break;
        }
        case ILOpCode::Initobj: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto type = file.ResolveTypeToken(tok);
            auto ptr = s.Pop(); if (!ptr) return DecodeOutcome::Bail;
            // Model initobj as stobj(addr, default-value, type). The full ILAst
            // has an InitObj node; for the reader a StObj with a null/zero value
            // is a faithful approximation that keeps the tree valid.
            auto zero = (type && type->ReflectionName() == "System.IntPtr")
                ? std::unique_ptr<ILInstruction>(std::make_unique<LdcI4>(0))
                : std::unique_ptr<ILInstruction>(std::make_unique<LdNull>());
            block->Add(std::make_unique<StObj>(std::move(ptr), std::move(zero), type));
            break;
        }

        // ---- ldftn / ldvirtftn / sizeof / ldtoken ----
        case ILOpCode::Ldftn: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            if (!s.Push(std::make_unique<LdFtn>(file.ResolveTokenToString(tok)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldvirtftn: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto target = s.Pop(); if (!target) return DecodeOutcome::Bail;
            // ldvirtftn pops the object and pushes the function pointer. We emit
            // the LdVirtFtn but lose the object reference -- approximate; the full
            // ILAst carries the target as a child.
            if (!s.Push(std::make_unique<LdVirtFtn>(file.ResolveTokenToString(tok)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Sizeof: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            if (!s.Push(std::make_unique<SizeOf>(file.ResolveTokenToString(tok)))) return DecodeOutcome::Bail;
            break;
        }
        case ILOpCode::Ldtoken: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            if (!s.Push(std::make_unique<LdTypeToken>(file.ResolveTokenToString(tok)))) return DecodeOutcome::Bail;
            break;
        }

        // ---- IL prefixes (volatile./constrained./readonly./unaligned./tail./prefixref) ----
        // These prefix the next instruction; the full ILAst stores them on the
        // following instruction's prefix fields. For the reader we skip the prefix
        // (consuming its operand if any) and continue -- losing the prefix
        // semantics but keeping the method decodable.
        case ILOpCode::Volatile:
        case ILOpCode::Readonly:
        case ILOpCode::Tail:
            break;  // no operand; prefix semantics lost but method continues
        case ILOpCode::Constrained: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            break;  // type token operand consumed; prefix semantics lost
        }
        case ILOpCode::Unaligned: {
            std::uint8_t v = 0; if (!ReadU8(b, size, pos, v)) return DecodeOutcome::Bail; pos += 1;
            break;
        }

        // ---- ldarga.s (short form: 1-byte index) ----
        case ILOpCode::Ldarga_s: {
            std::uint8_t idx = 0; if (!ReadU8(b, size, pos, idx)) return DecodeOutcome::Bail; pos += 1;
            if (idx >= s.parameters.size()) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdLoca>(s.parameters[idx]))) return DecodeOutcome::Bail;
            break;
        }

        // ---- ckfinite: a unary op on the top of stack (in-place) ----
        case ILOpCode::Ckfinite: {
            // ckfinite operates on the top of stack in-place; pop and re-push.
            auto v = s.Pop(); if (!v) return DecodeOutcome::Bail;
            // Model as a no-op wrapper for now (the full ILAst has a Ckfinite node).
            if (!s.Push(std::move(v))) return DecodeOutcome::Bail;
            break;
        }

        // ---- arglist: push the argument list handle (vararg methods) ----
        case ILOpCode::Arglist: {
            if (!s.Push(std::make_unique<LdTypeToken>("arglist"))) return DecodeOutcome::Bail;
            break;
        }

        case ILOpCode::Leave: case ILOpCode::Leave_s: {
            // IL leave transfers control out of a protected region; with regions
            // nested into containers it decodes as a plain Branch to the target
            // (as in the C# ILReader's DecodeUnconditionalBranch(isLeave: true)).
            bool isShort = (op == ILOpCode::Leave_s);
            std::uint32_t target = 0;
            if (!ReadBranchTarget(b, size, pos, isShort, start, target)) return DecodeOutcome::Bail;
            if (target >= size) return DecodeOutcome::Bail;
            // leave empties the evaluation stack (ECMA-335 III.4.18): pending
            // values flush to preserve side effects, but the target receives an
            // empty input stack (the C# clears currentStack before marking).
            FlushExpressionStack(s, block);
            static const std::vector<ILVariablePtr> emptyStack;
            MergeStackIntoTarget(s, target, emptyStack);
            block->SetFinal(std::make_unique<Branch>(target));
            return DecodeOutcome::BranchInstr;
        }
        case ILOpCode::Endfinally: {
            // endfinally exits the finally container: Leave with a null target;
            // the BlockBuilder assigns the innermost enclosing container.
            block->SetFinal(std::make_unique<Leave>(nullptr));
            return DecodeOutcome::Terminal;
        }
        case ILOpCode::Endfilter: {
            // endfilter answers the catch filter: Leave(null, verdict).
            auto verdict = s.Pop();
            if (!verdict) return DecodeOutcome::Bail;
            block->SetFinal(std::make_unique<Leave>(nullptr, std::move(verdict)));
            return DecodeOutcome::Terminal;
        }

        // ---- overflow-checking conversions (same result types as the non-ovf forms) ----
#define IL_CONVOVF(opc, target) \
    case ILOpCode::opc: { \
        auto v = s.Pop(); \
        if (!v) return DecodeOutcome::Bail; \
        if (!s.Push(std::make_unique<Conv>(std::move(v), StackType::target, true))) return DecodeOutcome::Bail; \
        break; \
    }
        IL_CONVOVF(Conv_ovf_i1, I4) IL_CONVOVF(Conv_ovf_u1, I4)
        IL_CONVOVF(Conv_ovf_i2, I4) IL_CONVOVF(Conv_ovf_u2, I4)
        IL_CONVOVF(Conv_ovf_i4, I4) IL_CONVOVF(Conv_ovf_u4, I4)
        IL_CONVOVF(Conv_ovf_i8, I8) IL_CONVOVF(Conv_ovf_u8, I8)
        IL_CONVOVF(Conv_ovf_i, I) IL_CONVOVF(Conv_ovf_u, I)
        IL_CONVOVF(Conv_ovf_i1_un, I4) IL_CONVOVF(Conv_ovf_u1_un, I4)
        IL_CONVOVF(Conv_ovf_i2_un, I4) IL_CONVOVF(Conv_ovf_u2_un, I4)
        IL_CONVOVF(Conv_ovf_i4_un, I4) IL_CONVOVF(Conv_ovf_u4_un, I4)
        IL_CONVOVF(Conv_ovf_i8_un, I8) IL_CONVOVF(Conv_ovf_u8_un, I8)
        IL_CONVOVF(Conv_ovf_i_un, I) IL_CONVOVF(Conv_ovf_u_un, I)
#undef IL_CONVOVF

        // ---- localloc: stack-allocate (rare; model as a no-op pushing a null ptr) ----
        case ILOpCode::Localloc: {
            auto size = s.Pop(); if (!size) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdNull>())) return DecodeOutcome::Bail;  // placeholder
            break;
        }

        // ---- mkrefany: make a typed reference (rare) ----
        case ILOpCode::Mkrefany: {
            std::uint32_t tok = 0; if (!ReadU32(b, size, pos, tok)) return DecodeOutcome::Bail; pos += 4;
            auto ptr = s.Pop(); if (!ptr) return DecodeOutcome::Bail;
            if (!s.Push(std::make_unique<LdTypeToken>(file.ResolveTokenToString(tok)))) return DecodeOutcome::Bail;
            break;
        }

        default:
            return DecodeOutcome::Bail;
    }
    return DecodeOutcome::Continue;
}

// Scan the IL for branch targets (the offsets a branch/conditional-branch/switch
// jumps to) so the block splitter knows where to start new blocks. Also marks
// offset 0 implicitly (the entry). Returns false if the scan hits malformed IL.
bool FindBranchTargets(const std::uint8_t* b, std::size_t size, std::set<std::uint32_t>& targets) {
    targets.insert(0);
    std::size_t pos = 0;
    while (pos < size) {
        std::size_t start = pos;
        ILOpCode op = DecodeOpCode(b, size, pos);
        bool isShort;
        switch (op) {
            case ILOpCode::Br: case ILOpCode::Br_s:
            case ILOpCode::Brtrue: case ILOpCode::Brtrue_s:
            case ILOpCode::Brfalse: case ILOpCode::Brfalse_s:
            case ILOpCode::Beq: case ILOpCode::Beq_s:
            case ILOpCode::Bge: case ILOpCode::Bge_s:
            case ILOpCode::Bgt: case ILOpCode::Bgt_s:
            case ILOpCode::Ble: case ILOpCode::Ble_s:
            case ILOpCode::Blt: case ILOpCode::Blt_s:
            case ILOpCode::Bne_un: case ILOpCode::Bne_un_s:
            case ILOpCode::Bge_un: case ILOpCode::Bge_un_s:
            case ILOpCode::Bgt_un: case ILOpCode::Bgt_un_s:
            case ILOpCode::Ble_un: case ILOpCode::Ble_un_s:
            case ILOpCode::Blt_un: case ILOpCode::Blt_un_s:
            // IL leave transfers control out of the region: its target starts a
            // block just like a branch target does.
            case ILOpCode::Leave: case ILOpCode::Leave_s: {
                isShort = (op == ILOpCode::Br_s || op == ILOpCode::Brtrue_s ||
                           op == ILOpCode::Brfalse_s || op == ILOpCode::Beq_s ||
                           op == ILOpCode::Bge_s || op == ILOpCode::Bgt_s ||
                           op == ILOpCode::Ble_s || op == ILOpCode::Blt_s ||
                           op == ILOpCode::Bne_un_s || op == ILOpCode::Bge_un_s ||
                           op == ILOpCode::Bgt_un_s || op == ILOpCode::Ble_un_s ||
                           op == ILOpCode::Blt_un_s || op == ILOpCode::Leave_s);
                std::uint32_t target = 0;
                if (!ReadBranchTarget(b, size, pos, isShort, start, target)) return false;
                if (target < size) targets.insert(target);
                break;
            }
            case ILOpCode::Switch: {
                std::uint32_t n = 0;
                if (!ReadU32(b, size, pos, n)) return false;
                pos += 4;
                std::size_t base = pos + static_cast<std::size_t>(n) * 4;
                for (std::uint32_t i = 0; i < n; ++i) {
                    std::int32_t rel = 0;
                    if (!ReadI32(b, size, pos, rel)) return false;
                    pos += 4;
                    targets.insert(static_cast<std::uint32_t>(base + rel));
                }
                break;
            }
            default: {
                // Skip the operand using the opcode's operand size (Switch handled
                // above). For token/variable/constant operands this is exact.
                std::size_t opStart = start;
                (void)opStart;
                OperandType ot = GetOperandType(op);
                if (ot == OperandType::Switch) { /* handled */ }
                else {
                    std::uint32_t sz = OperandFixedSize(ot);
                    if (pos + sz > size) return false;
                    pos += sz;
                }
                break;
            }
        }
    }
    return true;
}

// Resolve all Branch.TargetOffset references in the tree to Block pointers,
// using the offset->Block map. Branches whose target offset has no block are
// left as offset branches (graceful).
void ResolveBranches(ILInstruction* inst, const std::map<std::uint32_t, Block*>& byOffset) {
    if (!inst) return;
    // Recurse into children first.
    for (int i = 0; i < inst->ChildCount(); ++i) {
        ResolveBranches(inst->GetChild(i), byOffset);
    }
    // A Branch carries the offset form until resolved.
    if (inst->Op == OpCode::Branch) {
        auto* br = static_cast<Branch*>(inst);
        if (br->HasOffset) {
            auto it = byOffset.find(br->TargetOffset);
            if (it != byOffset.end()) { br->TargetBlock = it->second; br->HasOffset = false; }
        }
    }
}
} // namespace

std::unique_ptr<ILFunction> ReadStraightLineIL(const MetadataFile& file,
                                               std::uint32_t methodToken,
                                               std::uint32_t rva) {
    if (rva == 0) return nullptr;
    auto body = file.GetMethodBody(rva);
    if (!body.IsValid()) return nullptr;
    if (!body.Handlers().empty()) return nullptr;

    auto sigOpt = file.GetMethodSignature(methodToken);
    if (!sigOpt) return nullptr;
    const auto& sig = *sigOpt;

    ReaderState s;
    InitParameters(s, sig);
    s.returnStackType = ReturnStackTypeOf(sig.ReturnType);

    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    auto* containerPtr = container.get();
    auto block = std::make_unique<Block>();

    const auto* b = body.IL().data();
    std::size_t size = body.IL().size();
    std::size_t pos = 0;

    while (pos < size) {
        std::size_t start = pos;
        ILOpCode op = DecodeOpCode(b, size, pos);
        DecodeOutcome out = DecodeOne(file, s, block.get(), containerPtr, op, b, size, pos, start);
        if (out == DecodeOutcome::Bail) return nullptr;
        if (out == DecodeOutcome::Terminal || out == DecodeOutcome::BranchInstr) {
            // Straight-line contract: a branch (not a ret/throw) is unsupported.
            if (out == DecodeOutcome::BranchInstr) return nullptr;
            break;  // Terminal: method ended at ret/throw.
        }
    }

    if (!block->FinalInstruction) return nullptr;
    if (!s.expressionStack.empty()) return nullptr;

    container->AddBlock(std::move(block));
    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    for (auto& v : s.parameters) if (v) fn->Variables.push_back(v);
    for (auto& v : s.locals) if (v) fn->Variables.push_back(v);
    fn->CheckInvariant(ILPhase::InILReader);
    return fn;
}

std::unique_ptr<ILFunction> ReadIL(const MetadataFile& file,
                                    std::uint32_t methodToken,
                                    std::uint32_t rva) {
    if (rva == 0) return nullptr;
    auto body = file.GetMethodBody(rva);
    if (!body.IsValid()) return nullptr;
    // EH methods: blocks are decoded flat (handler entries seeded with the
    // exception object), then the BlockBuilder nests region blocks into
    // TryCatch/TryFinally/TryFault containers.

    auto sigOpt = file.GetMethodSignature(methodToken);
    if (!sigOpt) return nullptr;
    const auto& sig = *sigOpt;

    ReaderState s;
    InitParameters(s, sig);
    s.returnStackType = ReturnStackTypeOf(sig.ReturnType);

    const auto* b = body.IL().data();
    std::size_t size = body.IL().size();
    if (size == 0) return nullptr;

    // Pre-scan branch targets so the splitter knows where blocks begin.
    std::set<std::uint32_t> branchTargets;
    if (!FindBranchTargets(b, size, branchTargets)) return nullptr;
    // A leave target outside the code is invalid IL (the C# emits an
    // InvalidBranch); the decode of such a leave bails below -- our graceful
    // degradation for the same case.

    // Build a map of handler-offset -> exception variable for seeding catch/filter
    // handler blocks with the exception object the runtime pushes.
    std::map<std::uint32_t, ILVariablePtr> handlerExceptionVar;
    for (const auto& eh : body.Handlers()) {
        branchTargets.insert(eh.HandlerOffset);
        branchTargets.insert(eh.TryOffset);
        if (eh.Kind == ExceptionHandlerKind::Catch || eh.Kind == ExceptionHandlerKind::Filter) {
            auto v = std::make_shared<ILVariable>();
            v->Name = "E_" + std::to_string(eh.HandlerOffset);
            v->Kind = VariableKind::ExceptionStackSlot;
            v->Type = nullptr;  // catch type resolved later; the token is in ClassTokenOrFilterOffset
            handlerExceptionVar[eh.HandlerOffset] = v;
        }
        if (eh.Kind == ExceptionHandlerKind::Filter) {
            // The filter block also starts with the exception object.
            handlerExceptionVar[eh.ClassTokenOrFilterOffset] = handlerExceptionVar[eh.HandlerOffset];
            branchTargets.insert(eh.ClassTokenOrFilterOffset);
        }
    }

    auto fn = std::make_unique<ILFunction>();
    auto container = std::make_unique<BlockContainer>();
    auto* containerPtr = container.get();

    // Blocks are decoded flat (tagged with their start offset); the BlockBuilder
    // nests them into EH-region containers at the end. byOffset maps each block's
    // start offset for branch resolution; the queue drives decode order.
    std::vector<std::pair<std::uint32_t, std::unique_ptr<Block>>> decodedBlocks;
    std::map<std::uint32_t, Block*> byOffset;
    std::vector<std::pair<std::uint32_t, Block*>> queue;
    auto makeBlock = [&](std::uint32_t off) -> Block* {
        // Idempotent: the same offset can be reached from a branch, a
        // fall-through boundary, and the EH seeding below.
        auto it = byOffset.find(off);
        if (it != byOffset.end()) return it->second;
        auto blk = std::make_unique<Block>();
        blk->StartILOffset = off;
        Block* raw = blk.get();
        decodedBlocks.push_back({off, std::move(blk)});
        byOffset[off] = raw;
        queue.push_back({off, raw});
        return raw;
    };
    makeBlock(0);
    // Every EH region boundary begins a block: seed the try, handler, and
    // filter entries even when ordinary flow never reaches them (the C# does
    // the same in EnsureExceptionHandlersHaveBlocks so every region container
    // has content for the BlockBuilder).
    for (const auto& eh : body.Handlers()) {
        if (eh.TryOffset < size) makeBlock(eh.TryOffset);
        if (eh.HandlerOffset < size) makeBlock(eh.HandlerOffset);
        if (eh.Kind == ExceptionHandlerKind::Filter && eh.ClassTokenOrFilterOffset < size)
            makeBlock(eh.ClassTokenOrFilterOffset);
    }
    // Pre-record the handler/filter entry input stacks: the runtime pushes the
    // exception object there (the C# StoreStackForOffset in
    // PrepareBranchTargetsAndStacksForExceptionHandlers).
    for (const auto& eh : body.Handlers()) {
        auto it = handlerExceptionVar.find(eh.HandlerOffset);
        if (it != handlerExceptionVar.end() && it->second)
            s.incomingStacks.emplace(eh.HandlerOffset, std::vector<ILVariablePtr>{it->second});
        if (eh.Kind == ExceptionHandlerKind::Filter) {
            auto fit = handlerExceptionVar.find(eh.ClassTokenOrFilterOffset);
            if (fit != handlerExceptionVar.end() && fit->second)
                s.incomingStacks.emplace(eh.ClassTokenOrFilterOffset,
                                         std::vector<ILVariablePtr>{fit->second});
        }
    }

    // Decode each block. The reader state is shared across blocks (parameters
    // and locals must be the same ILVariable instances method-wide); only the
    // evaluation-stack state resets at each block entry.
    for (std::size_t qi = 0; qi < queue.size(); ++qi) {
        auto [blockStart, block] = queue[qi];
        std::size_t pos = blockStart;
        s.expressionStack.clear();
        s.currentStack.clear();
        s.stackBase = 0;
        // Start from the block's recorded input stack (empty for the entry
        // block; the exception slot for handler/filter entries; whatever the
        // first predecessor merged in otherwise).
        auto instack = s.incomingStacks.find(static_cast<std::uint32_t>(blockStart));
        if (instack != s.incomingStacks.end()) s.currentStack = instack->second;

        while (pos < size) {
            // If this offset is a branch target AND we're not at the block start,
            // end the block with an explicit fall-through Branch to it (merging
            // the carried evaluation stack into the target's input stack).
            if (pos != blockStart && branchTargets.count(static_cast<std::uint32_t>(pos))) {
                FlushExpressionStack(s, block);
                MergeStackIntoTarget(s, static_cast<std::uint32_t>(pos), s.currentStack);
                block->SetFinal(std::make_unique<Branch>(static_cast<std::uint32_t>(pos)));
                makeBlock(static_cast<std::uint32_t>(pos));
                break;
            }
            std::size_t start = pos;
            ILOpCode op = DecodeOpCode(b, size, pos);
            DecodeOutcome out = DecodeOne(file, s, block, containerPtr, op, b, size, pos, start);
            if (out == DecodeOutcome::Bail) return nullptr;
            if (out == DecodeOutcome::Terminal) {
                // ret/throw/rethrow/endfinally/endfilter ended the block. Valid
                // IL has no pending values left at this point; invalid IL does
                // (the C# drops them) -- flush to preserve side effects anyway.
                if (!s.expressionStack.empty()) FlushExpressionStack(s, block);
                break;
            }
            if (out == DecodeOutcome::SwitchInstr) {
                // Create blocks for every switch target (case + default). The
                // switch final's sections carry Branch(offset) bodies.
                auto* sw = dynamic_cast<SwitchInstruction*>(block->FinalInstruction.get());
                if (sw) {
                    for (int si = 0; si < sw->ChildCount() - 1; ++si) {  // skip Value (slot 0)
                        if (auto* sec = dynamic_cast<SwitchSection*>(sw->GetChild(si + 1))) {
                            if (auto* br = dynamic_cast<Branch*>(sec->Body.get())) {
                                if (br->TargetOffset < size && !byOffset.count(br->TargetOffset))
                                    makeBlock(br->TargetOffset);
                            }
                        }
                    }
                }
                // The default section's target is the instruction after the switch:
                // a new block starts there too (if within the body).
                if (pos < size && !byOffset.count(static_cast<std::uint32_t>(pos)))
                    makeBlock(static_cast<std::uint32_t>(pos));
                break;
            }
            if (out == DecodeOutcome::BranchInstr) {
                // A branch may target a block we haven't created yet; create it
                // (forward reference). The branch's target offset is in the final.
                auto* fin = block->FinalInstruction.get();
                std::uint32_t tgt = 0;
                if (auto* br = dynamic_cast<Branch*>(fin)) tgt = br->TargetOffset;
                else if (auto* iff = dynamic_cast<IfInstruction*>(fin)) {
                    if (auto* tbr = dynamic_cast<Branch*>(iff->TrueInst.get())) tgt = tbr->TargetOffset;
                }
                if (tgt < size && !byOffset.count(tgt)) makeBlock(tgt);
                // A conditional branch also falls through to the next offset; if
                // that next offset is a branch target (or just the continuation),
                // a new block starts there.
                if (op == ILOpCode::Brtrue || op == ILOpCode::Brtrue_s ||
                    op == ILOpCode::Brfalse || op == ILOpCode::Brfalse_s ||
                    op == ILOpCode::Beq || op == ILOpCode::Beq_s ||
                    op == ILOpCode::Bge || op == ILOpCode::Bge_s ||
                    op == ILOpCode::Bgt || op == ILOpCode::Bgt_s ||
                    op == ILOpCode::Ble || op == ILOpCode::Ble_s ||
                    op == ILOpCode::Blt || op == ILOpCode::Blt_s ||
                    op == ILOpCode::Bne_un || op == ILOpCode::Bne_un_s ||
                    op == ILOpCode::Bge_un || op == ILOpCode::Bge_un_s ||
                    op == ILOpCode::Bgt_un || op == ILOpCode::Bgt_un_s ||
                    op == ILOpCode::Ble_un || op == ILOpCode::Ble_un_s ||
                    op == ILOpCode::Blt_un || op == ILOpCode::Blt_un_s) {
                    if (pos < size && !byOffset.count(static_cast<std::uint32_t>(pos)))
                        makeBlock(static_cast<std::uint32_t>(pos));
                }
                // DecodeOne flushed the pending values into stack slots and
                // merged the carried stack into the branch targets already.
                break;
            }
        }
        if (!block->FinalInstruction) {
            // Fell off the end without a terminal: not valid IL, or our walk
            // stopped early. Bail.
            return nullptr;
        }
    }

    // A merge conflict (mismatched stack heights/types at a join) means
    // invalid or unsupported IL; degrade whole-method rather than guess.
    if (s.mergeFailed) return nullptr;

    // Nest the flat blocks into exception-region containers (no-op ordering
    // pass for handler-less methods).
    BlockBuilder bb(body.Handlers(), handlerExceptionVar, static_cast<std::uint32_t>(size));
    bb.CreateBlocks(*containerPtr, decodedBlocks);

    // Resolve Branch.TargetOffset -> Block* across the (now nested) tree.
    ResolveBranches(containerPtr, byOffset);

    // Unify the merged stack slots and register the created variables.
    if (!s.slotRepresentative.empty()) RemapMergedVariables(s, containerPtr);

    fn->Body = std::move(container);
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    for (auto& v : s.parameters) if (v) fn->Variables.push_back(v);
    for (auto& v : s.locals) if (v) fn->Variables.push_back(v);
    // The catch/filter exception stack slots and the merge stack slots are
    // function variables too (merges alias slots, so dedupe by pointer after
    // representative resolution).
    {
        std::set<ILVariable*> seen;
        for (const auto& [offset, v] : handlerExceptionVar) {
            (void)offset;
            if (v && seen.insert(v.get()).second) fn->Variables.push_back(v);
        }
        for (auto& v : s.stackVarsCreated) {
            ILVariablePtr rep = FindSlotRep(s, v);
            if (rep && seen.insert(rep.get()).second) fn->Variables.push_back(rep);
        }
    }
    fn->CheckInvariant(ILPhase::InILReader);
    return fn;
}

} // namespace ILSpy::Decompiler::IL
