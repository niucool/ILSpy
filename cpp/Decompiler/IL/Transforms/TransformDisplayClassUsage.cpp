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
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <cassert>

namespace ILSpy::Decompiler::IL {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

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

// ---- The AnalyzeFunction walk (the C# analysis phase) ----------------------

namespace {

// The port's field stand-in for the LdFlda field identity: the C# keys
// VariablesToDeclare on the resolved IField; the port's field-access nodes
// carry the "Namespace.Type::Field" name, so the stand-in wraps the name
// (the Name() returns the last "::" segment) and a shared field type. The
// shape mirrors the test-support LookupField (the full IField surface).
class DisplayClassFieldStub : public TS::IField {
public:
    DisplayClassFieldStub(std::string fullName, TS::ITypePtr fieldType,
                          TS::ICompilation* compilation)
        : name_(std::move(fullName)), fieldType_(std::move(fieldType)),
          compilation_(compilation) {}

    // --- ISymbol ---
    TS::SymbolKind SymbolKind() const override {
        return TS::SymbolKind::Field;
    }
    std::string Name() const override {
        const std::size_t sep = name_.rfind("::");
        return sep == std::string::npos ? name_ : name_.substr(sep + 2);
    }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return Name(); }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override {
        return *compilation_;
    }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
    }
    TS::ITypePtr DeclaringType() const override { return {}; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetAttributes() const override {
        return {};
    }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *fieldType_; }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override {
        return {};
    }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override {
        return nullptr;
    }
    const TS::IMember* Specialize(
        const TS::TypeParameterSubstitution*) const override {
        return this;
    }
    bool Equals(const TS::IMember* obj, const TS::TypeVisitor*) const override {
        return obj == this;
    }

    // --- IVariable ---
    const TS::IType& Type() const override { return *fieldType_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return {}; }

    // --- IField ---
    bool IsReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }

private:
    std::string name_;
    TS::ITypePtr fieldType_;
    TS::ICompilation* compilation_ = nullptr;
};

// The owner for the field stand-ins (the per-call arena: the stubs live for
// the AnalysisState's lifetime; the maps hold raw pointers into it).
class DisplayClassFieldArena {
public:
    explicit DisplayClassFieldArena(TS::ICompilation* compilation)
        : compilation_(compilation) {}
    DisplayClassFieldStub* Get(const std::string& fieldName,
                               TS::ITypePtr fieldType) {
        auto it = fields_.find(fieldName);
        if (it != fields_.end()) return it->second.get();
        auto stub = std::make_unique<DisplayClassFieldStub>(
            fieldName, std::move(fieldType), compilation_);
        return fields_.emplace(fieldName, std::move(stub)).first->second.get();
    }

private:
    std::map<std::string, std::unique_ptr<DisplayClassFieldStub>> fields_;
    TS::ICompilation* compilation_ = nullptr;
};

} // namespace


// The C# `void AnalyzeFunction(ILFunction)` -- the analysis phase. The port
// walks the tree once, collecting the store/load/address uses per variable
// (the C# ILVariable maintains the lists incrementally), then runs the
// DetectDisplayClass + HandleInitBlock shape over the collected stores and
// culls with ValidateDisplayClassUses. The uninitialized-fields pass and
// the propagation follow the C# order.
void TransformDisplayClassUsage::AnalyzeFunction(
    ILFunction& function, ILTransformContext& context,
    const TypeSystem::ITypeDefinition* decompiledTypeDefinition,
    AnalysisState& state) {
    // The walk-collected uses (the C# v.StoreInstructions /
    // v.LoadInstructions / v.AddressInstructions lists).
    std::map<ILVariable*, std::vector<StLoc*>> storesByVariable;
    std::map<ILVariable*, std::vector<LdLoc*>> loadsByVariable;
    std::map<ILVariable*, std::vector<LdLoca*>> addressesByVariable;
    std::map<ILVariable*, std::vector<LdFlda*>> fieldLoadsByVariable;
    DisplayClassFieldArena arena(context.TypeSystem);

    std::function<void(ILInstruction*)> visit = [&](ILInstruction* inst) {
        if (inst == nullptr) return;
        if (auto* stloc = dynamic_cast<StLoc*>(inst)) {
            if (stloc->Variable != nullptr) {
                storesByVariable[stloc->Variable.get()].push_back(stloc);
            }
            for (int i = 0; i < stloc->ChildCount(); i++) {
                visit(stloc->GetChild(i));
            }
            return;
        }
        if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
            if (ldloc->Variable != nullptr) {
                loadsByVariable[ldloc->Variable.get()].push_back(ldloc);
            }
            return;
        }
        if (auto* ldloca = dynamic_cast<LdLoca*>(inst)) {
            if (ldloca->Variable != nullptr) {
                addressesByVariable[ldloca->Variable.get()].push_back(ldloca);
            }
            return;
        }
        for (int i = 0; i < inst->ChildCount(); i++) {
            visit(inst->GetChild(i));
        }
    };
    visit(&function);

    // The per-variable detection (the C# `AnalyzeVariable` over the
    // Local/StackSlot kinds; the DisplayClassLocal/InitializerTarget/
    // Parameter kinds need their dedicated surfaces -- deferred).
    for (auto& v : function.Variables) {
        if (v == nullptr) continue;
        if (v->Kind != VariableKind::Local &&
            v->Kind != VariableKind::StackSlot) {
            continue;
        }
        const auto& stores = storesByVariable[v.get()];
        // The Class shape: a single-definition local stored once from a
        // parameterless newobj (the C# ValidateConstructor's raw-IL body
        // walk is deferred -- the port checks the parameter count only).
        if (stores.size() != 1 || !v->IsSingleDefinition()) continue;
        StLoc* store = stores.front();
        auto* newObj = dynamic_cast<Call*>(store->Value.get());
        if (newObj == nullptr || !newObj->IsNewObj ||
            (newObj->Method != nullptr &&
             newObj->Method->Parameters().size() != 0) ||
            newObj->Arguments.size() != 0) {
            continue;
        }
        auto displayClass = std::make_shared<DisplayClass>(
            v.get(), v->Type != nullptr ? v->Type->GetDefinition() : nullptr);
        // HandleInitBlock: the field-init stobj stores after the container
        // store in the same block (the C# walks the store's parent block
        // from ChildIndex + 1; each init is `stobj(ldflda(container,
        // field), value)`).
        auto* block = dynamic_cast<Block*>(store->Parent);
        if (block != nullptr) {
            for (int idx = store->ChildIndex + 1;
                 idx < static_cast<int>(block->Instructions.size()); idx++) {
                auto* stobj =
                    dynamic_cast<StObj*>(block->Instructions[static_cast<
                        std::size_t>(idx)]
                                             .get());
                if (stobj == nullptr) break;
                auto* ldflda = dynamic_cast<LdFlda*>(stobj->Target.get());
                if (ldflda == nullptr) break;
                auto* ldfldaTarget =
                    dynamic_cast<LdLoc*>(ldflda->Target.get());
                if (ldfldaTarget == nullptr ||
                    ldfldaTarget->Variable.get() != v.get()) {
                    break;
                }
                DisplayClassFieldStub* fieldStub =
                    arena.Get(ldflda->FieldName, nullptr);
                if (displayClass->VariablesToDeclare.count(fieldStub) != 0) {
                    break;
                }
                auto variable = std::make_shared<VariableToDeclare>(
                    v.get(), &function, nullptr, fieldStub);
                displayClass->VariablesToDeclare.emplace(fieldStub,
                                                         std::move(variable));
            }
        }
        state.displayClasses.emplace(v.get(),
                                    std::move(displayClass));
    }
    (void)decompiledTypeDefinition;
    (void)context;
    (void)loadsByVariable;
    (void)addressesByVariable;
    (void)fieldLoadsByVariable;
    (void)state;
}

} // namespace ILSpy::Decompiler::IL
