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

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformDisplayClassUsage.cs --
// scalar replacement of aggregates over compiler-generated display classes
// (closure frames): the closure fields become locals.
//
// Sliced: this file carries the scaffolding (the C# nested DisplayClass and
// VariableToDeclare classes), the AnalyzeFunction walk skeleton (the
// per-variable kind dispatch), DetectDisplayClass's gate (the
// ValidateDisplayClassDefinition shape with the port-available checks), and
// the AddVariable/VariableToDeclare machinery. Deferred with their anchors:
//  - the full SROA visitor (VisitStLoc/VisitStObj/VisitLdLoc/VisitLdLoca/
//    VisitAddressOf + the inline/propagate rewrites), which needs the ILAst
//    load/address instruction lists on ILVariable;
//  - ValidateConstructor (the raw-IL base-ctor-only check);
//  - the enumerator/state-machine exclusion gates (they take the metadata
//    handles the port's transform context does not carry).

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/ILVariable.hpp"

#include <map>
#include <memory>
#include <set>
#include <string>

namespace ILSpy {
namespace Decompiler {
namespace TypeSystem {
class IType;
class ITypeDefinition;
class IField;
} // namespace TypeSystem
} // namespace Decompiler
} // namespace ILSpy

namespace ILSpy::Decompiler::IL {

class ILVariable;
class BlockContainer;
struct ILInstruction;
class StLoc;
class LdLoc;
class LdLoca;

class TransformDisplayClassUsage final : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // ---- The C# nested DisplayClass / VariableToDeclare scaffolding -------

    // The C# `class VariableToDeclare` (TransformDisplayClassUsage.cs): one
    // display-class field's SROA plan. The C# `IField field` reference ports
    // to a non-owning pointer (the type-system-owned member; the
    // non-owning-shared_ptr convention). The C# `declaredVariable == null ||
    // declaredVariable.StateMachineField == field` Debug.Assert shape ports
    // to an unchecked invariant (the port's ILVariable has no
    // StateMachineField handle).
    class VariableToDeclare {
    public:
        // The C# `public string Name => field.Name`.
        std::string Name() const;

        bool CanPropagate = false;
        bool UsesInitialValue = false;
        // The C# `public HashSet<ILInstruction> Initializers`.
        std::set<ILInstruction*> Initializers;

        // The port carries the owning function explicitly (the C#
        // `container.Variable.Function` reads the back-pointer the port's
        // ILVariable does not have).
        VariableToDeclare(ILVariable* containerVariable,
                          ILFunction* ownerFunction,
                          const TypeSystem::ITypeDefinition* type,
                          const TypeSystem::IField* field,
                          ILVariable* declaredVariable = nullptr);

        // The C# `public void Propagate(ILVariable variable)` -- set the
        // propagated variable (null revokes the propagation).
        void Propagate(ILVariable* variable);
        // The C# `public ILVariable GetOrDeclare()` -- register the local on
        // the container's owning function with the field's type and name,
        // InitialValueIsInitialized + UsesInitialValue + CaptureScope. The
        // port returns the raw pointer (the registered variable stays owned
        // by the owning ILFunction's Variables list).
        ILVariable* GetOrDeclare();

        const TypeSystem::IField* Field() const { return field_; }
        ILVariable* DeclaredVariable() const { return declaredVariable_.get(); }

    private:
        ILVariable* containerVariable_;
        ILFunction* ownerFunction_ = nullptr;
        const TypeSystem::ITypeDefinition* type_;
        const TypeSystem::IField* field_;
        // The registered local (owned; the port's RegisterVariable returns an
        // owning shared_ptr, the C# GC equivalent).
        ILVariablePtr declaredVariable_;
    };

    // The C# `class DisplayClass` (TransformDisplayClassUsage.cs): one
    // detected display-class container. The C# `Dictionary<IField,
    // VariableToDeclare>` keys on the member definition (the port's stub
    // fields are IField*; the map keys on the pointer identity, the C#
    // reference-equality default).
    struct DisplayClass {
        ILVariable* Variable = nullptr;
        const TypeSystem::ITypeDefinition* Type = nullptr;
        // The C# `Dictionary<IField, VariableToDeclare>` keyed on the field's
        // member definition; the port keys on the field-access stand-in's
        // "Namespace.Type::Field" name (the identity the IL walk collects).
        std::map<std::string, std::shared_ptr<VariableToDeclare>>
            VariablesToDeclare;
        BlockContainer* CaptureScope = nullptr;
        ILInstruction* Initializer = nullptr;

        DisplayClass(ILVariable* variable, const TypeSystem::ITypeDefinition* type)
            : Variable(variable), Type(type) {}
    };

    // The C# `VariableToDeclare AddVariable(DisplayClass container, StObj
    // init, IField field)` (the port drops the nullable StObj initializer --
    // the Initializers set is filled by the caller, which owns the store).
    static VariableToDeclare& AddVariable(
        DisplayClass& container, const TypeSystem::IField* field);

    // The C# `DisplayClass AnalyzeVariable(ILVariable v)` (the kind dispatch
    // over the port-available gates). The port takes the aggressive flag
    // from the context; the decompiled type definition (the C#
    // `new SimpleTypeResolveContext(context.Function.Method).CurrentTypeDefinition`)
    // is deferred with the ILFunction.Method surface and is passed in by the
    // Run walk (null when unknown, matching the C# null-the-same-way).
    static DisplayClass* AnalyzeVariable(
        ILVariable* v, ILTransformContext& context,
        const TypeSystem::ITypeDefinition* decompiledTypeDefinition,
        std::map<ILVariable*, std::shared_ptr<DisplayClass>>& displayClasses,
        std::map<ILVariable*, ILVariable*>& displayClassCopyMap);

    // The C# `bool ValidateDisplayClassDefinition(ITypeDefinition)`: the
    // module/metadata gates are dropped (the port's context carries no
    // PEFile) with a comment; the Kind + IsPotentialClosure gates remain.
    static bool ValidateDisplayClassDefinition(
        const TypeSystem::ITypeDefinition* definition,
        ILTransformContext& context,
        const TypeSystem::ITypeDefinition* decompiledTypeDefinition);

    // The C# `void AnalyzeFunction(ILFunction)` -- the analysis phase: the
    // per-variable walk (the port collects the store/load/address uses into
    // side maps -- the C# ILVariable maintains the lists incrementally),
    // DetectDisplayClass over the collected stores, the
    // ValidateDisplayClassUses cull, and the uninitialized-fields pass.
    // The probes/test surface runs the same shape and hands the maps out.
    struct AnalysisState {
        std::map<ILVariable*, std::shared_ptr<DisplayClass>> displayClasses;
        std::map<ILVariable*, ILVariable*> displayClassCopyMap;
        // The walk-collected uses (the C# ILVariable maintains the
        // StoreInstructions/LoadInstructions/AddressInstructions lists
        // incrementally; the port collects them during the analysis walk
        // and the SROA visitor consumes the same maps).
        std::map<ILVariable*, std::vector<StLoc*>> storesByVariable;
        std::map<ILVariable*, std::vector<LdLoc*>> loadsByVariable;
        std::map<ILVariable*, std::vector<LdLoca*>> addressesByVariable;
        // Keeps the display-class field stubs alive for as long as the
        // analysis results are consumed (the VariablesToDeclare maps hold
        // non-owning IField pointers into the stub arena; the arena type is
        // defined in the .cpp, so the shared_ptr is type-erased here).
        std::shared_ptr<void> fieldStubArena;
    };
    static void AnalyzeFunction(ILFunction& function,
                                ILTransformContext& context,
                                const TypeSystem::ITypeDefinition*
                                    decompiledTypeDefinition,
                                AnalysisState& state);

    // The C# `void Transform(ILFunction)` -- the SROA rewrite visitor (the
    // VisitStLoc/VisitStObj/VisitLdFlda rewrites over the analysis maps).
    // The port runs it as a manual pre-order walk (no ILVisitor base).
    static void Transform(ILFunction& function, ILTransformContext& context,
                          AnalysisState& state);

    // The probes/test surface: AnalyzeFunction with the state exposed (the
    // file-local-probe convention).
    static ILVariable* ResolveVariableToPropagateForTransform(
        ILInstruction* value, AnalysisState& state);

    static void AnalyzeFunctionForTests(
        ILFunction& function, ILTransformContext& context,
        std::map<ILVariable*, std::shared_ptr<DisplayClass>>& displayClasses) {
        AnalysisState state;
        AnalyzeFunction(function, context, nullptr, state);
        displayClasses = std::move(state.displayClasses);
    }
};

} // namespace ILSpy::Decompiler::IL