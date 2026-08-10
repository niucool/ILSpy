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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Out-of-line ILFunction members that need the full instruction tree: the lazy
// ChainedConstructorCallILOffset (walks descendants for the first chained .ctor
// Call) and RegisterVariable. Keeping these out of the widely-included header
// avoids pulling Call.hpp / TypeUtils.hpp into every translation unit.

#include "Decompiler/IL/Instructions/ILFunction.hpp"

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <optional>
#include <string_view>

namespace ILSpy::Decompiler::IL {

namespace {

// The short part of a resolved method name -- the substring after the last
// '::' (the C# IMethod.Name). Returns the whole string when no '::' is present.
// Mirrors the IL reader's ShortMethodName so the chained-call check recognises
// a constructor call (short name ".ctor") without exposing the reader helper.
std::string_view ShortMethodName(std::string_view fullName) {
    auto p = fullName.rfind("::");
    if (p == std::string_view::npos) return fullName;
    return fullName.substr(p + 2);
}

// True when `inst` is the chained-constructor call the C#
// ILFunction.ChainedConstructorCallILOffset searches for: a Call (not newobj)
// whose method is a constructor on a reference-type declaring type and whose
// parent is a Block. The C# additionally requires call.Method.IsConstructor;
// this port approximates that by the resolved method name ending in ".ctor"
// (every constructor call resolves to "Type::.ctor"), matching the
// Call::IsOperator name-only precedent. The reference-type gate uses the shared
// IsReferenceType helper (true only for Class/Interface/Delegate/Array/...).
bool IsChainedConstructorCall(const ILInstruction* inst) {
    if (!inst || inst->Op != OpCode::Call) return false;
    const auto* call = static_cast<const Call*>(inst);
    if (call->IsNewObj) return false;  // the C# `!(call is NewObj)`
    if (ShortMethodName(call->MethodName) != ".ctor") return false;
    auto ref = TypeSystem::IsReferenceType(call->DeclaringType.get());
    if (!ref.has_value() || !*ref) return false;  // the C# `IsReferenceType == true`
    // The C# requires `call.Parent is Block`; a chained : base/: this call is a
    // statement (a void call the reader added to its block), so its parent is
    // the enclosing Block.
    return call->Parent != nullptr && call->Parent->Op == OpCode::Block;
}

// Pre-order DFS over `inst`'s descendants (excluding inst itself), returning
// the first chained-constructor call's StartILOffset, or -1 if none. Matches
// the C# `this.Descendants.FirstOrDefault(...)?.StartILOffset ?? -1` (Descendants
// yields a pre-order walk).
std::int32_t FindChainedCtorOffset(const ILInstruction* inst) {
    if (!inst) return -1;
    for (int i = 0; i < inst->ChildCount(); ++i) {
        auto* child = inst->GetChild(i);
        if (!child) continue;
        if (IsChainedConstructorCall(child)) return child->StartILOffset;
        std::int32_t deeper = FindChainedCtorOffset(child);
        if (deeper >= 0) return deeper;
    }
    return -1;
}

// Reassign every load/store/address of `v2` in `inst`'s subtree to `v1`,
// incrementing v1's usage counts for the reassigned uses. Mirrors the C#
// ILFunction.RecombineVariables iterating the per-variable LoadInstructions /
// StoreInstructions / AddressInstructions lists and setting each
// instruction's Variable (the setter maintains the lists and the counts).
// This port has no such lists, so a tree walk finds the uses and a manual
// count increment replaces the setter's bookkeeping. A variable-bearing
// instruction whose Variable is v2 has its Variable reparented to the v1
// shared_ptr (the count on v1 grows by exactly one per reassigned use); v2's
// counts are zeroed by the caller after the walk. The store-bearing opcodes
// (StLoc, MatchInstruction, UsingInstruction, TryCatchHandler) all count as
// stores, matching CountUsage in VariableUsage.cpp.
void ReassignUses(ILInstruction* inst, const ILVariablePtr& v1, const ILVariable* v2) {
    if (!inst) return;
    auto reassign = [&](ILVariablePtr& member, int& v1Count) {
        if (member.get() == v2) {
            member = v1;
            ++v1Count;
        }
    };
    switch (inst->Op) {
        case OpCode::LdLoc:
            reassign(static_cast<LdLoc*>(inst)->Variable, v1->LoadCount);
            break;
        case OpCode::LdLoca:
            reassign(static_cast<LdLoca*>(inst)->Variable, v1->AddressCount);
            break;
        case OpCode::StLoc:
            reassign(static_cast<StLoc*>(inst)->Variable, v1->StoreCount);
            break;
        case OpCode::MatchInstruction:
            reassign(static_cast<MatchInstruction*>(inst)->Variable, v1->StoreCount);
            break;
        case OpCode::UsingInstruction:
            reassign(static_cast<UsingInstruction*>(inst)->Variable, v1->StoreCount);
            break;
        case OpCode::TryCatchHandler:
            reassign(static_cast<TryCatchHandler*>(inst)->Variable, v1->StoreCount);
            break;
        default:
            break;
    }
    for (int i = 0; i < inst->ChildCount(); ++i)
        ReassignUses(inst->GetChild(i), v1, v2);
}

} // namespace

std::int32_t ILFunction::ChainedConstructorCallILOffset() const {
    if (chainedCtorOffsetComputed_) return chainedCtorOffset_;
    // The C# bails when Method is null/not-an-instance-constructor; this port's
    // IsConstructor/IsStatic are the pre-resolved equivalent (default false
    // models a null Method, so the gate fails and the offset is -1).
    if (!IsConstructor || IsStatic) {
        chainedCtorOffset_ = -1;
    } else {
        chainedCtorOffset_ = Body ? FindChainedCtorOffset(Body.get()) : -1;
    }
    chainedCtorOffsetComputed_ = true;
    return chainedCtorOffset_;
}

ILVariablePtr ILFunction::RegisterVariable(VariableKind kind, TypeSystem::ITypePtr type,
                                           const std::string& name) {
    auto v = std::make_shared<ILVariable>(kind, std::move(type));
    if (name.empty()) {
        v->Name = "I_" + std::to_string(helperVariableCount_++);
        v->HasGeneratedName = true;
    } else {
        v->Name = name;
    }
    Variables.push_back(v);
    return v;
}

void ILFunction::RecombineVariables(ILVariablePtr variable1, ILVariablePtr variable2) {
    if (!variable1 || !variable2 || variable1.get() == variable2.get()) return;
    ReassignUses(Body.get(), variable1, variable2.get());
    // v2 is now unreferenced in the tree; zero its counts (the C# setter would
    // have drained its LoadInstructions/StoreInstructions/AddressInstructions
    // lists) and drop it from the function's Variables list.
    variable2->LoadCount = 0;
    variable2->StoreCount = 0;
    variable2->AddressCount = 0;
    for (auto it = Variables.begin(); it != Variables.end(); ++it) {
        if (it->get() == variable2.get()) {
            Variables.erase(it);
            break;
        }
    }
}

} // namespace ILSpy::Decompiler::IL
