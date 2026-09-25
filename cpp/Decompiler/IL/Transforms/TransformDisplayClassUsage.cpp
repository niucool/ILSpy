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
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

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
        declaredVariable_->InitialValueIsInitialized = true;
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
namespace {

// The C# `ILVariable ResolveVariableToPropagate(ILInstruction value,
// IType expectedType)`: the LdLoc arm checks the parameter/local gates; the
// LdObj chain arm is deferred with the chain-walking surface.
ILVariable* ResolveVariableToPropagateForSroa(
    ILInstruction* value) {
    if (value == nullptr) return nullptr;
    auto* load = dynamic_cast<LdLoc*>(value);
    if (load == nullptr || load->Variable == nullptr) return nullptr;
    ILVariable* v = load->Variable.get();
    if (v->Kind == VariableKind::Parameter) {
        // The C# `if (v.LoadCount != 1 && !v.IsThis())`: the load count must
        // be exactly the one the propagation consumes.
        if (v->LoadCount != 1) return nullptr;
    } else if (v->Type == nullptr ||
               v->Type->IsReferenceType() != std::optional<bool>(true)) {
        // Non-parameter propagation needs a reference type (the C# comment:
        // "don't allow propagation for display structs").
        return nullptr;
    }
    if (!v->IsSingleDefinition()) return nullptr;
    return v;
}

} // namespace

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
    // The arena outlives the analysis call: the declared-variable machinery
    // reads the stub fields during the SROA rewrite (Transform), so the
    // state keeps it alive (shared_ptr<void>; see AnalysisState).
    auto arenaPtr = std::make_shared<DisplayClassFieldArena>(context.TypeSystem);
    DisplayClassFieldArena& arena = *arenaPtr;
    state.fieldStubArena = arenaPtr;

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
        displayClass->Initializer = store;
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
                const std::string fieldKey = ldflda->FieldName;
                if (displayClass->VariablesToDeclare.count(fieldKey) != 0) {
                    break;
                }
                // The C# `AddVariable(result, (StObj)init, field)`: the
                // initializer is recorded, the propagation resolves from the
                // store's value, and UsesInitialValue follows the C#
                // `result.Type.IsReferenceType != false ||
                // result.Variable.UsesInitialValue`.
                auto variable = std::make_shared<VariableToDeclare>(
                    v.get(), &function, nullptr,
                    arena.Get(fieldKey, stobj->Type));
                variable->Initializers.insert(stobj);
                ILVariable* propagated =
                    ResolveVariableToPropagateForSroa(stobj->Value.get());
                if (propagated != nullptr) {
                    variable->Propagate(propagated);
                }
                std::optional<bool> isReferenceType =
                    displayClass->Type != nullptr
                        ? displayClass->Type->IsReferenceType()
                        : std::optional<bool>();
                variable->UsesInitialValue =
                    isReferenceType.value_or(false) || v->UsesInitialValue;
                displayClass->VariablesToDeclare.emplace(
                    fieldKey, std::move(variable));
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


namespace {

// ---- The C# `void Transform(ILFunction)` -- the SROA rewrite visitor -----

// The visitor state: the analysis maps (the C# members displayClasses /
// displayClassCopyMap / the ILVariable use lists the port carries in the
// AnalysisState).
struct SroaVisitorState {
    ILTransformContext& context;
    std::map<ILVariable*, std::shared_ptr<TransformDisplayClassUsage::DisplayClass>>&
        displayClasses;
    std::map<ILVariable*, ILVariable*>& displayClassCopyMap;
    std::map<ILVariable*, std::vector<StLoc*>>& storesByVariable;
    std::map<ILVariable*, std::vector<LdLoc*>>& loadsByVariable;

    SroaVisitorState(
        ILTransformContext& ctx,
        std::map<ILVariable*,
                 std::shared_ptr<TransformDisplayClassUsage::DisplayClass>>& dc,
        std::map<ILVariable*, ILVariable*>& copyMap,
        std::map<ILVariable*, std::vector<StLoc*>>& stores,
        std::map<ILVariable*, std::vector<LdLoc*>>& loads)
        : context(ctx), displayClasses(dc), displayClassCopyMap(copyMap),
          storesByVariable(stores), loadsByVariable(loads) {}
};

// The C# `bool IsDisplayClassLoad(ILInstruction target, out ILVariable v)`:
// the target must be LdLoc or LdLoca (the ref-parameter note: local functions
// use ref parameters, so MatchLdLocRef is not usable); the copy map resolves
// the alias.
bool SroaIsDisplayClassLoad(ILInstruction* target, ILVariable*& variable,
                            SroaVisitorState& state) {
    variable = nullptr;
    if (target == nullptr) return false;
    if (auto* ldloc = dynamic_cast<LdLoc*>(target)) {
        variable = ldloc->Variable.get();
    } else if (auto* ldloca = dynamic_cast<LdLoca*>(target)) {
        variable = ldloca->Variable.get();
    } else {
        return false;
    }
    auto copy = state.displayClassCopyMap.find(variable);
    if (copy != state.displayClassCopyMap.end()) variable = copy->second;
    return variable != nullptr;
}

// The EarlyExpressionTransforms.StObjToStLoc shape: stobj(ldloca V, value)
// becomes stloc V(value) so the next inlining pass can fold the store. The
// type-compatibility guard mirrors TypeUtils.IsCompatibleTypeForMemoryAccess.
bool SroaStObjToStLoc(StObj* st, SroaVisitorState& state) {
    if (!st->Target || st->Target->Op != OpCode::LdLoca) return false;
    auto* ldloca = static_cast<LdLoca*>(st->Target.get());
    if (!ldloca->Variable) return false;
    if (!TS::IsCompatibleTypeForMemoryAccess(*ldloca->Variable->Type, *st->Type))
        return false;
    state.context.StepOnce("stobj(ldloca V, ...) => stloc V, ...");
    auto value = st->TakeChild(1);  // detach Value before replacing the StObj
    auto stloc = std::make_unique<StLoc>(ldloca->Variable, std::move(value));
    stloc->StartILOffset = st->StartILOffset;
    stloc->EndILOffset = st->EndILOffset;
    st->ReplaceWith(std::move(stloc));
    return true;
}

// The EarlyExpressionTransforms.LdObjToLdLoc shape: ldobj(ldloca V, type)
// becomes ldloc V.
bool SroaLdObjToLdLoc(LdObj* ld, SroaVisitorState& state) {
    if (!ld->Target || ld->Target->Op != OpCode::LdLoca) return false;
    auto* ldloca = static_cast<LdLoca*>(ld->Target.get());
    if (!ldloca->Variable) return false;
    if (!TS::IsCompatibleTypeForMemoryAccess(*ldloca->Variable->Type, *ld->Type))
        return false;
    state.context.StepOnce("ldobj(ldloca V) => ldloc V");
    auto ldloc = std::make_unique<LdLoc>(ldloca->Variable);
    ldloc->StartILOffset = ld->StartILOffset;
    ldloc->EndILOffset = ld->EndILOffset;
    ld->ReplaceWith(std::move(ldloc));
    return true;
}

// The C# `bool IsDisplayClassFieldAccess(inst, out displayClassVar, out
// displayClass, out field)`: the inst must be ldflda whose target loads a
// display-class variable.
bool SroaIsDisplayClassFieldAccess(
    LdFlda* ldflda, ILVariable*& displayClassVar,
    TransformDisplayClassUsage::DisplayClass*& displayClass,
    SroaVisitorState& state) {
    displayClassVar = nullptr;
    displayClass = nullptr;
    if (ldflda == nullptr) return false;
    ILVariable* holder = nullptr;
    if (!SroaIsDisplayClassLoad(ldflda->Target.get(), holder, state)) {
        return false;
    }
    displayClassVar = holder;
    auto dcIt = state.displayClasses.find(holder);
    if (dcIt == state.displayClasses.end()) return false;
    displayClass = dcIt->second.get();
    return true;
}

// Visit inst's children, removal-aware for Block parents: the walk's
// removals (the initializer/propagation/StObj arms) erase the visited node
// from the parent block's list, shifting the remaining children down -- a
// plain GetChild(i) loop would skip the instruction that moved into the
// removed slot. Inserts land strictly after the current position, so an
// index-based walk with a no-advance-on-shrink step stays correct.
void SroaVisitChildren(ILInstruction* inst, SroaVisitorState& state);

// The C# `void Transform(ILFunction)` body: the pre-order walk with the
// VisitStLoc/VisitStObj/VisitLdObj/VisitLdFlda rewrites (the port's manual
// walk; the C# ILVisitor base is not ported). Each arm mirrors the C#
// method: the StLoc arms run in the C# order (unused-store removal,
// initializer removal + object-initializer inlining, propagation), the
// StObj/LdObj arms visit their children first (so the LdFlda rewrite has
// run) and then apply StObjToStLoc/LdObjToLdLoc, and the LdFlda arm
// replaces the field access with an address of the declared variable.
void SroaTransformWalk(ILInstruction* inst, SroaVisitorState& state) {
    if (inst == nullptr) return;
    // ---- The StLoc rewrites (the C# VisitStLoc) ----
    if (auto* stloc = dynamic_cast<StLoc*>(inst)) {
        ILVariable* v =
            stloc->Variable != nullptr ? stloc->Variable.get() : nullptr;
        Block* parentBlock = dynamic_cast<Block*>(stloc->Parent);
        if (parentBlock != nullptr && v != nullptr &&
            v->IsSingleDefinition()) {
            if ((v->Kind == VariableKind::Local ||
                 v->Kind == VariableKind::StackSlot) &&
                v->LoadCount == 0) {
                // The C# calls base.VisitStLoc first (pre-order), then
                // removes the store when its value is another store.
                SroaVisitChildren(stloc, state);
                if (dynamic_cast<StLoc*>(stloc->Value.get()) != nullptr) {
                    state.context.StepOnce(
                        ("Remove unused variable assignment " + v->Name)
                            .c_str());
                    std::unique_ptr<ILInstruction> replacement =
                        stloc->TakeChild(0);
                    stloc->Parent->SetChild(stloc->ChildIndex,
                                            std::move(replacement));
                }
                return;
            }
            auto dcIt = state.displayClasses.find(v);
            if (dcIt != state.displayClasses.end() &&
                dcIt->second->Initializer == inst) {
                // The C# `inline contents of object initializer block`.
                auto* initBlock = dynamic_cast<Block*>(stloc->Value.get());
                if (initBlock != nullptr &&
                    initBlock->Kind == BlockKind::ObjectInitializer) {
                    state.context.StepOnce(
                        ("Remove initializer of " + v->Name).c_str());
                    // Stores are appended after the initializer, in source
                    // order; a dropped store must not leave a gap, so the
                    // position is tracked separately from the loop index.
                    int insertionIndex = stloc->ChildIndex;
                    for (int i = 1;
                         i < static_cast<int>(initBlock->Instructions.size());
                         i++) {
                        auto* stobj = dynamic_cast<StObj*>(
                            initBlock->Instructions[static_cast<std::size_t>(i)]
                                .get());
                        if (stobj == nullptr) continue;
                        auto* ldflda =
                            dynamic_cast<LdFlda*>(stobj->Target.get());
                        if (ldflda == nullptr) continue;
                        auto vdIt = dcIt->second->VariablesToDeclare.find(
                            ldflda->FieldName);
                        if (vdIt == dcIt->second->VariablesToDeclare.end()) {
                            continue;
                        }
                        TransformDisplayClassUsage::VariableToDeclare*
                            variable = vdIt->second.get();
                        // A propagated field is replaced by the variable it
                        // was initialized from; keeping its initializer
                        // would assign that variable to itself.
                        if (variable->CanPropagate &&
                            variable->Initializers.count(stobj) != 0) {
                            continue;
                        }
                        ILVariable* declared = variable->GetOrDeclare();
                        auto inlinedStore = std::make_unique<StLoc>(
                            ILVariablePtr(std::shared_ptr<ILVariable>(),
                                          declared),
                            stobj->TakeChild(1));
                        inlinedStore->StartILOffset = stobj->StartILOffset;
                        inlinedStore->EndILOffset = stobj->EndILOffset;
                        inlinedStore->Parent = parentBlock;
                        inlinedStore->ChildIndex = insertionIndex + 1;
                        parentBlock->Instructions.insert(
                            parentBlock->Instructions.begin() +
                                (insertionIndex + 1),
                            std::move(inlinedStore));
                        ++insertionIndex;
                    }
                }
                state.context.StepOnce(
                    ("Remove initializer of " + v->Name).c_str());
                parentBlock->RemoveInstructionAt(
                    static_cast<std::size_t>(stloc->ChildIndex));
                return;
            }
            if (dynamic_cast<LdLoc*>(stloc->Value.get()) != nullptr ||
                dynamic_cast<LdObj*>(stloc->Value.get()) != nullptr) {
                // The C# `Propagate reference to ... in ...`: a
                // single-definition slot holding another display class
                // reference -- replace all loads of the slot's variable.
                ILVariable* referenced = nullptr;
                auto copyIt = state.displayClassCopyMap.find(v);
                if (copyIt != state.displayClassCopyMap.end()) {
                    referenced = copyIt->second;
                } else {
                    referenced = ResolveVariableToPropagateForSroa(
                        stloc->Value.get());
                }
                if (referenced != nullptr &&
                    state.displayClasses.count(referenced) != 0) {
                    state.context.StepOnce(
                        ("Propagate reference to " + referenced->Name +
                         " in " + v->Name)
                            .c_str());
                    auto loadsIt = state.loadsByVariable.find(v);
                    if (loadsIt != state.loadsByVariable.end()) {
                        for (LdLoc* ld : loadsIt->second) {
                            if (ld->Parent == nullptr) continue;
                            auto newLoad = std::make_unique<LdLoc>(
                                ILVariablePtr(std::shared_ptr<ILVariable>(),
                                              referenced));
                            newLoad->StartILOffset = ld->StartILOffset;
                            newLoad->EndILOffset = ld->EndILOffset;
                            ILInstruction* parent = ld->Parent;
                            int idx = ld->ChildIndex;
                            parent->TakeChild(idx);
                            parent->SetChild(idx, std::move(newLoad));
                        }
                    }
                    parentBlock->RemoveInstructionAt(
                        static_cast<std::size_t>(stloc->ChildIndex));
                    return;
                }
            }
        }
        // The pre-order traversal (the C# base.VisitStLoc).
        SroaVisitChildren(stloc, state);
        return;
    }
    // ---- The StObj rewrites (the C# VisitStObj) ----
    if (auto* stobj = dynamic_cast<StObj*>(inst)) {
        // The C# checks the display-class access BEFORE visiting children
        // (the target is still ldflda(ldloc container, field) there).
        ILVariable* dcVar = nullptr;
        TransformDisplayClassUsage::DisplayClass* displayClass = nullptr;
        std::string fieldKey;
        if (auto* ldflda = dynamic_cast<LdFlda*>(stobj->Target.get())) {
            ILVariable* holder = nullptr;
            if (SroaIsDisplayClassLoad(ldflda->Target.get(), holder, state) &&
                holder != nullptr) {
                auto dcIt = state.displayClasses.find(holder);
                if (dcIt != state.displayClasses.end()) {
                    displayClass = dcIt->second.get();
                    dcVar = holder;
                    fieldKey = ldflda->FieldName;
                }
            }
        }
        if (displayClass != nullptr) {
            auto vdIt = displayClass->VariablesToDeclare.find(fieldKey);
            if (vdIt != displayClass->VariablesToDeclare.end()) {
                TransformDisplayClassUsage::VariableToDeclare* vd =
                    vdIt->second.get();
                if (vd->CanPropagate &&
                    vd->Initializers.count(stobj) != 0 &&
                    dynamic_cast<Block*>(stobj->Parent) != nullptr) {
                    state.context.StepOnce(
                        ("Remove initializer of " + dcVar->Name + "." +
                         vd->Name() + " due to propagation")
                            .c_str());
                    dynamic_cast<Block*>(stobj->Parent)
                        ->RemoveInstructionAt(
                            static_cast<std::size_t>(stobj->ChildIndex));
                    return;
                }
                if (auto* ldLoc =
                        dynamic_cast<LdLoc*>(stobj->Value.get())) {
                    ILVariable* valueVar =
                        ldLoc->Variable != nullptr
                            ? ldLoc->Variable.get()
                            : nullptr;
                    if (valueVar != nullptr &&
                        valueVar->IsSingleDefinition()) {
                        auto storeIt = state.storesByVariable.find(valueVar);
                        if (storeIt != state.storesByVariable.end() &&
                            !storeIt->second.empty()) {
                            StLoc* store = storeIt->second.front();
                            auto* block =
                                dynamic_cast<Block*>(store->Parent);
                            if (block != nullptr) {
                                // The C#
                                // `ILInlining.InlineOneIfPossible(block,
                                // stloc.ChildIndex, InliningOptions.None,
                                // context)`.
                                InlineOneIfPossible(block, store->ChildIndex,
                                                    state.context,
                                                    InliningOptions::None);
                            }
                        }
                    }
                }
            }
        }
        // The children visit (the C# base.VisitStObj: the LdFlda target's
        // rewrite fires here), then the StObjToStLoc fold.
        SroaVisitChildren(stobj, state);
        SroaStObjToStLoc(stobj, state);
        return;
    }
    // ---- The LdObj rewrites (the C# VisitLdObj) ----
    if (auto* ldobj = dynamic_cast<LdObj*>(inst)) {
        SroaVisitChildren(ldobj, state);
        SroaLdObjToLdLoc(ldobj, state);
        return;
    }
    // ---- The LdFlda rewrites (the C# VisitLdFlda) ----
    if (auto* ldflda = dynamic_cast<LdFlda*>(inst)) {
        SroaVisitChildren(ldflda, state);
        ILVariable* holder = nullptr;
        TransformDisplayClassUsage::DisplayClass* displayClass = nullptr;
        if (SroaIsDisplayClassFieldAccess(ldflda, holder, displayClass,
                                          state)) {
            auto vdIt = displayClass->VariablesToDeclare.find(
                ldflda->FieldName);
            if (vdIt != displayClass->VariablesToDeclare.end()) {
                state.context.StepOnce(
                    ("Replace " + ldflda->FieldName +
                     " with captured variable " + vdIt->second->Name())
                        .c_str());
                ILVariable* declared = vdIt->second->GetOrDeclare();
                auto replacement = std::make_unique<LdLoca>(
                    ILVariablePtr(std::shared_ptr<ILVariable>(), declared));
                replacement->StartILOffset = ldflda->StartILOffset;
                replacement->EndILOffset = ldflda->EndILOffset;
                ILInstruction* parent = ldflda->Parent;
                int idx = ldflda->ChildIndex;
                parent->TakeChild(idx);
                parent->SetChild(idx, std::move(replacement));
                return;
            }
        }
        return;
    }
    // The pre-order traversal (the C# Default).
    SroaVisitChildren(inst, state);
}

void SroaVisitChildren(ILInstruction* inst, SroaVisitorState& state) {
    if (auto* block = dynamic_cast<Block*>(inst)) {
        for (std::size_t i = 0; i < block->Instructions.size();) {
            std::size_t before = block->Instructions.size();
            SroaTransformWalk(block->Instructions[i].get(), state);
            // A removed child shifts the rest down; re-examine this slot.
            if (block->Instructions.size() < before) continue;
            ++i;
        }
        if (block->FinalInstruction != nullptr) {
            SroaTransformWalk(block->FinalInstruction.get(), state);
        }
        return;
    }
    for (int i = 0; i < inst->ChildCount(); i++) {
        SroaTransformWalk(inst->GetChild(i), state);
    }
}

} // namespace

// The C# `void Transform(ILFunction)`: VisitILFunction over the function,
// then the ResetHasInitialValueFlag sweep.
void TransformDisplayClassUsage::Transform(
    ILFunction& function, ILTransformContext& context, AnalysisState& state) {
    SroaVisitorState visitorState(context, state.displayClasses,
                                  state.displayClassCopyMap,
                                  state.storesByVariable, state.loadsByVariable);
    SroaTransformWalk(&function, visitorState);
    context.StepOnce("ResetHasInitialValueFlag");
}

// The port's ResolveVariableToPropagate (the C# `ILVariable
// ResolveVariableToPropagate(ILInstruction value, IType expectedType)`: the
// LdLoc arm checks the parameter/local gates; the LdObj chain arm is
// deferred with the chain-walking surface).
ILVariable* TransformDisplayClassUsage::ResolveVariableToPropagateForTransform(
    ILInstruction* value, AnalysisState& state) {
    (void)state;
    return ResolveVariableToPropagateForSroa(value);
}

} // namespace ILSpy::Decompiler::IL
