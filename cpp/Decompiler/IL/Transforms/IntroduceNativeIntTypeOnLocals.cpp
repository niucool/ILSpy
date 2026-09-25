// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/IL/Transforms/IntroduceNativeIntTypeOnLocals.hpp"

#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BitNot.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <map>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `static bool IsUsedAsNativeInt(LdLoc load)` (the port's manual walk
// over the load's parent; the C# switch over the parent node shapes).
bool IsUsedAsNativeInt(const LdLoc* load) {
    const ILInstruction* parent = load ? load->Parent : nullptr;
    if (parent == nullptr) return false;
    if (auto* binop = dynamic_cast<const BinaryNumericInstruction*>(parent))
        return binop->UnderlyingResultType() == StackType::I;
    if (auto* bitNot = dynamic_cast<const BitNot*>(parent))
        return bitNot->UnderlyingResultType == StackType::I;
    if (auto* call = dynamic_cast<const Call*>(parent)) {
        // The C# `call.GetParameter(load.ChildIndex)?.Type
        // .IsCSharpNativeIntegerType()`: the argument position minus the
        // receiver slot indexes the resolved parameter types.
        const int argIndex = load->ChildIndex - (call->IsInstanceCall ? 1 : 0);
        if (argIndex < 0 ||
            argIndex >= static_cast<int>(call->ParameterIType.size()))
            return false;
        return TypeSystem::IsCSharpNativeIntegerType(
            call->ParameterIType[static_cast<std::size_t>(argIndex)].get());
    }
    return false;
}

// The C# `static bool IsNativeIntStore(IStoreInstruction store,
// ICompilation compilation)` -- the StLoc value shapes. The C# default arm
// runs `stloc.Value.InferType(compilation)`; the port's InferType is the
// minimal per-node approximation (the NullPropagationTransform precedent),
// so only the resolved-return-type arm of a Call is carried here -- the
// remaining InferType node kinds are deferred with that surface.
bool IsNativeIntStore(const StLoc* stloc) {
    const ILInstruction* value = stloc ? stloc->Value.get() : nullptr;
    if (value == nullptr) return false;
    if (auto* binop = dynamic_cast<const BinaryNumericInstruction*>(value))
        return binop->UnderlyingResultType() == StackType::I;
    if (auto* conv = dynamic_cast<const Conv*>(value))
        return conv->ResultType() == StackType::I;
    if (auto* call = dynamic_cast<const Call*>(value)) {
        if (call->ReturnIType != nullptr)
            return TypeSystem::IsCSharpNativeIntegerType(
                call->ReturnIType.get());
    }
    return false;
}

// The per-variable use-site collector (the loads and stores of every
// variable, in tree order): the walk the missing LoadInstructions /
// StoreInstructions lists replace with.
void CollectUseSites(
    ILInstruction* node,
    std::map<ILVariable*, std::vector<LdLoc*>>& loads,
    std::map<ILVariable*, std::vector<ILInstruction*>>& stores) {
    if (auto* ldloc = dynamic_cast<LdLoc*>(node)) {
        if (ldloc->Variable != nullptr)
            loads[ldloc->Variable.get()].push_back(ldloc);
    } else if (auto* stloc = dynamic_cast<StLoc*>(node)) {
        if (stloc->Variable != nullptr)
            stores[stloc->Variable.get()].push_back(stloc);
    }
    for (int i = 0; i < node->ChildCount(); i++) {
        if (ILInstruction* child = node->GetChild(i))
            CollectUseSites(child, loads, stores);
    }
}

} // namespace

void IntroduceNativeIntTypeOnLocals::Run(ILFunction& function,
                                         ILTransformContext& context) {
    if (!context.Settings.NativeIntegers) return;
    // The use-site lists the C# reads live (ILVariable.LoadInstructions and
    // friends) are this port's ComputeVariableUsage snapshots; recompute so
    // the enumeration below sees the current tree.
    ComputeVariableUsage(function);
    // The C# `foreach (var nestedFunction in function.Descendants
    // .OfType<ILFunction>())` -- self first, then the nested functions.
    std::vector<ILFunction*> functions;
    functions.push_back(&function);
    struct Frame {
        ILInstruction* node;
        int nextChild;
    };
    std::vector<Frame> stack;
    stack.push_back({function.Body.get(), 0});
    while (!stack.empty()) {
        Frame& frame = stack.back();
        const int count = frame.node->ChildCount();
        if (frame.nextChild >= count) {
            stack.pop_back();
            continue;
        }
        ILInstruction* child = frame.node->GetChild(frame.nextChild);
        frame.nextChild++;
        if (child == nullptr) continue;
        if (auto* nested = dynamic_cast<ILFunction*>(child))
            functions.push_back(nested);
        stack.push_back({child, 0});
    }
    for (ILFunction* nestedFunction : functions) {
        // The use-site lists (the C# reads the variable's
        // LoadInstructions/StoreInstructions; the port does not maintain the
        // per-variable lists, D11/D62/D68, so the loads and stores are
        // gathered by a walk -- the CachedDelegateInitialization precedent).
        std::map<ILVariable*, std::vector<LdLoc*>> loads;
        std::map<ILVariable*, std::vector<ILInstruction*>> stores;
        CollectUseSites(nestedFunction->Body.get(), loads, stores);
        // The retype pass (the C# first loop): the load/store shape decides.
        std::map<std::int32_t, TypeSystem::ITypePtr> variableTypeMapping;
        for (auto& variable : nestedFunction->Variables) {
            if (!variable) continue;
            if (variable->Kind != VariableKind::Local &&
                variable->Kind != VariableKind::StackSlot &&
                variable->Kind != VariableKind::PatternLocal &&
                variable->Kind != VariableKind::ForeachLocal &&
                variable->Kind != VariableKind::UsingLocal) {
                continue;
            }
            if (variable->Type == nullptr ||
                (!TypeSystem::IsKnownType(*variable->Type,
                                          TypeSystem::KnownTypeCode::IntPtr) &&
                 !TypeSystem::IsKnownType(*variable->Type,
                                          TypeSystem::KnownTypeCode::UIntPtr))) {
                continue;
            }
            bool isUsedAsNativeInt = false;
            auto loadIt = loads.find(variable.get());
            if (loadIt != loads.end()) {
                for (const LdLoc* load : loadIt->second) {
                    if (IsUsedAsNativeInt(load)) {
                        isUsedAsNativeInt = true;
                        break;
                    }
                }
            }
            bool isAssignedNativeInt = false;
            {
                auto storeIt = stores.find(variable.get());
                if (storeIt != stores.end()) {
                    for (const ILInstruction* store : storeIt->second) {
                        if (auto* stloc =
                                dynamic_cast<const StLoc*>(store)) {
                            if (IsNativeIntStore(stloc)) {
                                isAssignedNativeInt = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (isUsedAsNativeInt || isAssignedNativeInt) {
                variable->Type = TypeSystem::GetSign(variable->Type.get()) ==
                                         TypeSystem::Sign::Unsigned
                                     ? TypeSystem::NUInt()
                                     : TypeSystem::NInt();
                if (variable->Kind == VariableKind::Local &&
                    variable->Index >= 0)
                    variableTypeMapping[variable->Index] = variable->Type;
            }
        }
        // The same-index pass (the C# second loop): the other locals sharing
        // the retyped local's index follow.
        for (auto& variable : nestedFunction->Variables) {
            if (!variable) continue;
            if (variable->Kind != VariableKind::Local || variable->Index < 0)
                continue;
            auto it = variableTypeMapping.find(variable->Index);
            if (it != variableTypeMapping.end()) variable->Type = it->second;
        }
    }
}

} // namespace ILSpy::Decompiler::IL
