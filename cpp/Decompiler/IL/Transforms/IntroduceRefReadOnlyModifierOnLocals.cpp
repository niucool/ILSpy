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

// The ref-readonly local inference (see the .hpp).

#include "Decompiler/IL/Transforms/IntroduceRefReadOnlyModifierOnLocals.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::IL {

void IntroduceRefReadOnlyModifierOnLocals::Run(
    ILFunction& function, ILTransformContext& context) {
    (void)context;
    for (auto& variable : function.Variables) {
        if (variable == nullptr)
            continue;
        if (variable->Type == nullptr ||
            variable->Type->Kind() != TypeSystem::TypeKind::ByReference ||
            variable->Kind == VariableKind::Parameter)
            continue;
        // ref readonly
        if (IsUsedAsRefReadonly(variable.get())) {
            variable->IsRefReadOnly = true;
            continue;
        }
    }
}

bool IntroduceRefReadOnlyModifierOnLocals::IsUsedAsRefReadonly(
    const ILVariable* variable) {
    // Infer ref readonly type from usage:
    // An ILVariable should be marked as readonly,
    // if it's a "by-ref-like" type and the initialized value is known to be
    // readonly.
    for (ILInstruction* storeInst : variable->StoreInstructions) {
        auto* store = dynamic_cast<StLoc*>(storeInst);
        if (store == nullptr || store->Value == nullptr)
            continue;
        // Check if C# requires that the local is ref-readonly in order to
        // allow the store:
        if (IsReadonlyReference(store->Value.get()))
            return true;
        // Check whether the local needs to be ref-readonly to avoid
        // changing the semantics of a readonly.ldelema:
        ILInstruction* val = store->Value.get();
        while (auto* ldflda = dynamic_cast<LdFlda*>(val)) {
            val = ldflda->Target.get();
        }
        if (auto* ldelema = dynamic_cast<LdElema*>(val)) {
            if (ldelema->IsReadOnly)
                return true;
        }
    }
    return false;
}

} // namespace ILSpy::Decompiler::IL
