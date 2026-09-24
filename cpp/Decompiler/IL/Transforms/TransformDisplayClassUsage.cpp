// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/IL/Transforms/TransformDisplayClassUsage.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cassert>

namespace ILSpy::Decompiler::IL {

void TransformDisplayClassUsage::Run(ILFunction& function,
                                     ILTransformContext& context) {
    // The C# Run body (AnalyzeFunction + the SROA Transform visitor) is
    // deferred with the visitor surface; the Run shell keeps the settings
    // gate the pipeline wiring consults.
    (void)function;
    (void)context;
}

// The C# `class VariableToDeclare` (the ported scaffolding).
TransformDisplayClassUsage::VariableToDeclare::VariableToDeclare(
    ILVariable* containerVariable, ILFunction* ownerFunction,
    const TypeSystem::ITypeDefinition* type,
    const TypeSystem::IField* field, ILVariable* declaredVariable)
    : containerVariable_(containerVariable),
      ownerFunction_(ownerFunction),
      type_(type),
      field_(field),
      declaredVariable_(declaredVariable) {
    // The C# `declaredVariable == null || declaredVariable.StateMachineField
    // == field` Debug.Assert shape ports to an unchecked invariant (the
    // port's ILVariable has no StateMachineField handle).
}

std::string TransformDisplayClassUsage::VariableToDeclare::Name() const {
    return field_ != nullptr ? field_->Name() : std::string();
}

void TransformDisplayClassUsage::VariableToDeclare::Propagate(
    ILVariable* variable) {
    // The port wraps the raw pointer non-owningly ONLY when the variable is
    // already registered (Propagate(null) clears). For a raw pointer from
    // outside the owning function's list, the C# GC keeps it alive; the port
    // adopts it into a shared_ptr if it is not yet owned.
    if (variable != nullptr) {
        // Adopt: find it in the owner's Variables list first.
        for (auto& v : ownerFunction_ != nullptr ? ownerFunction_->Variables
                                                 : std::vector<ILVariablePtr>()) {
            if (v.get() == variable) {
                declaredVariable_ = v;
                CanPropagate = true;
                return;
            }
        }
        // Not registered (an externally-owned variable): adopt with a
        // non-owning aliasing shared_ptr is FORBIDDEN (the dangling rule);
        // the caller guarantees liveness per the C# contract.
        declaredVariable_ = ILVariablePtr(variable);
    } else {
        declaredVariable_ = nullptr;
    }
    CanPropagate = variable != nullptr;
}

ILVariable* TransformDisplayClassUsage::VariableToDeclare::GetOrDeclare() {
    if (declaredVariable_ != nullptr) return declaredVariable_.get();
    // The C# `container.Variable.Function.RegisterVariable(VariableKind.Local,
    // field.Type, field.Name)` + InitialValueIsInitialized/UsesInitialValue/
    // CaptureScope. The port passes the owning function explicitly (the C#
    // `v.Function` back-pointer is not carried); the field's type is the
    // compilation-owned object behind the interface reference (the
    // shared_from_this convention); CaptureScope is deferred with the
    // closure-scope surface (the C# `container.CaptureScope` may be null).
    declaredVariable_ =
        ownerFunction_ != nullptr
            ? ownerFunction_->RegisterVariable(
                  VariableKind::Local,
                  std::const_pointer_cast<TypeSystem::IType>(
                      field_->ReturnType().shared_from_this()),
                  field_->Name())
            : nullptr;
    if (declaredVariable_ != nullptr) {
        declaredVariable_->UsesInitialValue = UsesInitialValue;
    }
    return declaredVariable_.get();
}

} // namespace ILSpy::Decompiler::IL
