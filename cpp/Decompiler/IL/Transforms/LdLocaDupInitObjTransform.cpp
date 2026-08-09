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

#include "Decompiler/IL/Transforms/LdLocaDupInitObjTransform.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

using TypeSystem::ITypePtr;
using TypeSystem::TypeKind;

// Collect every BlockContainer in the tree (the function body and any nested
// loop/try/switch containers the transforms introduced).
void CollectContainers(ILInstruction* inst, std::vector<BlockContainer*>& out) {
    if (!inst) return;
    if (auto* c = dynamic_cast<BlockContainer*>(inst)) out.push_back(c);
    for (int i = 0; i < inst->ChildCount(); ++i) CollectContainers(inst->GetChild(i), out);
}

// Approximation of TypeUtils.IsCompatibleTypeForMemoryAccess (TypeSystem/TypeUtils.cs).
// Two types are memory-compatible if they are equal, both reference types, both
// integer types of equal size (same StackType), or either is unknown. Skipped vs
// the C#: NormalizeTypeVisitor.TypeErasure (nullable/modifier stripping) -- this
// port does not yet carry those wrappers, so the raw types are compared directly.
bool IsCompatibleTypeForMemoryAccess(const ITypePtr& memoryType, const ITypePtr& accessType) {
    if (!memoryType || !accessType) return true;  // unknown -> assume compatible
    if (memoryType->ReflectionName() == accessType->ReflectionName()) return true;
    TypeKind k1 = memoryType->Kind();
    TypeKind k2 = accessType->Kind();
    if (k1 == TypeKind::Unknown || k2 == TypeKind::Unknown) return true;
    auto IsRef = [](TypeKind k) {
        return k == TypeKind::Class || k == TypeKind::Interface ||
               k == TypeKind::Delegate || k == TypeKind::Array ||
               k == TypeKind::Dynamic || k == TypeKind::Null;
    };
    if (IsRef(k1) && IsRef(k2)) return true;
    StackType ms = StackTypeOf(memoryType);
    StackType as = StackTypeOf(accessType);
    auto IsInteger = [](StackType s) {
        return s == StackType::I4 || s == StackType::I || s == StackType::I8;
    };
    if (ms == as && IsInteger(ms)) return true;
    return false;
}

// `stloc s(ldloca v)` followed by `stobj T(ldloc s, default(T))`:
// move the default-value store up to the local `v` so the address slot `s` can
// be inlined into its subsequent uses. Returns true on a rewrite.
bool TryTransform(Block& block, std::size_t i, ILTransformContext& context) {
    if (i + 1 >= block.Instructions.size()) return false;
    auto* inst1 = dynamic_cast<StLoc*>(block.Instructions[i].get());
    if (!inst1 || !inst1->Variable) return false;
    auto* ldloca = dynamic_cast<LdLoca*>(inst1->Value.get());
    if (!ldloca || !ldloca->Variable) return false;
    ILVariablePtr s = inst1->Variable;
    ILVariablePtr v = ldloca->Variable;

    auto* inst2 = dynamic_cast<StObj*>(block.Instructions[i + 1].get());
    if (!inst2) return false;

    // inst2.Target must be `ldloc s`.
    auto* target = dynamic_cast<LdLoc*>(inst2->Target.get());
    if (!target || target->Variable.get() != s.get()) return false;

    // The stored value must be `default(T)` and T must be memory-compatible with v.
    if (!inst2->Value || inst2->Value->Op != OpCode::DefaultValue) return false;
    if (!IsCompatibleTypeForMemoryAccess(v->Type, inst2->Type)) return false;

    context.StepOnce("LdLocaDupInitObjTransform");
    auto defaultValue = inst2->TakeChild(1);  // detach the DefaultValue from inst2
    auto newStLoc = std::make_unique<StLoc>(v, std::move(defaultValue));
    // Move inst1 (stloc s(ldloca v)) from position i to i+1; this destroys inst2
    // (the StObj), which we have already stripped its Value from. Then place the
    // new `stloc v(default(T))` at position i.
    block.Instructions[i + 1] = std::move(block.Instructions[i]);
    block.Instructions[i] = std::move(newStLoc);
    block.RenumberChildren();                 // fix ChildIndex for both slots
    block.Instructions[i]->Parent = &block;  // the new StLoc's Parent (was null)
    return true;
}

} // namespace

void LdLocaDupInitObjTransform::Run(ILFunction& function, ILTransformContext& context) {
    std::vector<BlockContainer*> containers;
    CollectContainers(function.Body.get(), containers);
    for (BlockContainer* container : containers) {
        if (!container) continue;
        for (auto& blockPtr : container->Blocks) {
            if (!blockPtr) continue;
            Block& block = *blockPtr;
            // Walk forward; a rewrite shifts the original store down one slot
            // but does not introduce a new matchable pair at i, so a plain
            // forward scan is enough (matches the C# per-instruction loop).
            for (std::size_t i = 0; i + 1 < block.Instructions.size(); ++i) {
                if (TryTransform(block, i, context)) {
                    // After the rewrite, Instructions[i] is the new stloc v
                    // and Instructions[i+1] is the moved stloc s; neither
                    // restarts the pattern, so continue the forward scan.
                }
            }
        }
    }
}

} // namespace ILSpy::Decompiler::IL
