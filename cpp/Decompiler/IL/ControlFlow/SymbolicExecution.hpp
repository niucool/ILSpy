// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/ControlFlow/SymbolicExecution.cs: the
// small symbolic evaluator the state-machine analyses (StateRangeAnalysis,
// used by both the YieldReturnDecompiler and the AsyncAwaitDecompiler) run
// over the 'state' expressions with -- ldc.i4 values are IntegerConstants,
// the state field (or a registered cached-state local) is State (+ Constant
// through a sub instruction), and comparisons against constants become
// StateInSet memberships.

#pragma once

#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/ControlFlow/SwitchAnalysis.hpp"  // MakeSetWhereComparisonIsTrue
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <stdexcept>
#include <vector>

namespace ILSpy::Decompiler::IL::ControlFlow {

// The C# `class SymbolicAnalysisFailedException`: thrown when the analyzed
// code does not match the expected compiler-generated shape -- the caller
// (the transform's Run) catches it and leaves the state machine as-is.
class SymbolicAnalysisFailedException : public std::runtime_error {
public:
    explicit SymbolicAnalysisFailedException(const std::string& message)
        : std::runtime_error(message) {}
};

// The C# `enum SymbolicValueType`.
enum class SymbolicValueType {
    Unknown,           // Unknown value
    IntegerConstant,   // int: Constant (result of ldc.i4)
    State,             // int: State + Constant
    This,              // This pointer (result of ldarg.0)
    StateInSet,        // bool: ValueSet.Contains(State)
};

// The C# `struct SymbolicValue`.
struct SymbolicValue {
    int Constant = 0;
    SymbolicValueType Type = SymbolicValueType::Unknown;
    Util::LongSet ValueSet;

    SymbolicValue() = default;
    SymbolicValue(SymbolicValueType type, int constant = 0)
        : Constant(constant), Type(type) {}
    SymbolicValue(SymbolicValueType type, Util::LongSet valueSet)
        : Type(type), ValueSet(std::move(valueSet)) {}

    // The C# `AsBool()`: convert a state integer to a bool --
    // `if (state + c)` is `if (state + c != 0)` is `if (state != -c)`.
    SymbolicValue AsBool() const {
        if (Type == SymbolicValueType::State) {
            return SymbolicValue(SymbolicValueType::StateInSet,
                                 Util::LongSet(
                                     -static_cast<long long>(Constant))
                                     .Invert());
        }
        return *this;
    }
};

// The C# `class SymbolicEvaluationContext`.
class SymbolicEvaluationContext {
public:
    SymbolicEvaluationContext(const TypeSystem::IField* stateField,
                              bool legacyVisualBasic = false)
        : stateField_(stateField), legacyVisualBasic_(legacyVisualBasic) {}

    void AddStateVariable(ILVariable* v) {
        for (ILVariable* existing : stateVariables_)
            if (existing == v) return;
        stateVariables_.push_back(v);
    }

    const std::vector<ILVariable*>& StateVariables() const {
        return stateVariables_;
    }

    // The C# `SymbolicValue Eval(ILInstruction inst)`.
    SymbolicValue Eval(const ILInstruction* inst) const {
        // The C# `inst is BinaryNumericInstruction bni && bni.Operator ==
        // Sub && (legacyVisualBasic || !bni.CheckForOverflow)` arm.
        if (const auto* bni =
                dynamic_cast<const BinaryNumericInstruction*>(inst)) {
            if (bni->Operator == BinaryNumericOperator::Sub &&
                (legacyVisualBasic_ || !bni->CheckForOverflow)) {
                SymbolicValue left = Eval(bni->Left.get());
                SymbolicValue right = Eval(bni->Right.get());
                if (left.Type != SymbolicValueType::State &&
                    left.Type != SymbolicValueType::IntegerConstant)
                    return Failed();
                if (right.Type != SymbolicValueType::IntegerConstant)
                    return Failed();
                return SymbolicValue(
                    left.Type,
                    static_cast<int>(static_cast<unsigned>(left.Constant)
                                     - static_cast<unsigned>(right.Constant)));
            }
            return Failed();
        }
        // The C# `inst.MatchLdFld(out target, out field)` arm.
        ILInstruction* target = nullptr;
        const TypeSystem::IField* field = nullptr;
        if (MatchLdFld(inst, target, field)) {
            if (Eval(target).Type != SymbolicValueType::This)
                return Failed();
            // The C# `field.MemberDefinition != stateField` identity check.
            const TypeSystem::IMember* definition =
                field != nullptr ? field->MemberDefinition() : nullptr;
            if (definition != stateField_)
                return Failed();
            return SymbolicValue(SymbolicValueType::State);
        }
        // The C# `inst.MatchLdLoc(out loadedVariable)` arm.
        if (const auto* ldloc = dynamic_cast<const LdLoc*>(inst)) {
            const ILVariable* loadedVariable = ldloc->Variable.get();
            if (loadedVariable == nullptr) return Failed();
            for (ILVariable* v : stateVariables_) {
                if (v == loadedVariable)
                    return SymbolicValue(SymbolicValueType::State);
            }
            if (loadedVariable->Kind == VariableKind::Parameter &&
                loadedVariable->Index < 0)
                return SymbolicValue(SymbolicValueType::This);
            return Failed();
        }
        // The C# `inst.MatchLdcI4(out value)` arm.
        if (const auto* ldc = dynamic_cast<const LdcI4*>(inst)) {
            return SymbolicValue(SymbolicValueType::IntegerConstant,
                                 ldc->Value);
        }
        // The C# `inst is Comp comp` arm.
        if (const auto* comp = dynamic_cast<const Comp*>(inst)) {
            SymbolicValue left = Eval(comp->Left.get());
            SymbolicValue right = Eval(comp->Right.get());
            if (left.Type == SymbolicValueType::State &&
                right.Type == SymbolicValueType::IntegerConstant) {
                // bool: (state + left.Constant == right.Constant)
                Util::LongSet trueSums = SwitchAnalysis::
                    MakeSetWhereComparisonIsTrue(
                        comp->Kind, right.Constant,
                        comp->Sign == TypeSystem::Sign::Unsigned);
                // symbolic value is true iff trueSums.Contains(state +
                // left.Constant)
                Util::LongSet trueStates = trueSums.AddOffset(
                    -static_cast<long long>(left.Constant));
                return SymbolicValue(SymbolicValueType::StateInSet,
                                     std::move(trueStates));
            }
            if (left.Type == SymbolicValueType::StateInSet &&
                right.Type == SymbolicValueType::IntegerConstant) {
                if (comp->Kind == ComparisonKind::Equality &&
                    right.Constant == 0) {
                    // comp((x in set) == 0) ==> x not in set
                    return SymbolicValue(SymbolicValueType::StateInSet,
                                         left.ValueSet.Invert());
                }
                if (comp->Kind == ComparisonKind::Inequality &&
                    right.Constant != 0) {
                    // comp((x in set) != 0) => x in set
                    return SymbolicValue(SymbolicValueType::StateInSet,
                                         left.ValueSet);
                }
                return Failed();
            }
            return Failed();
        }
        return Failed();
    }

private:
    static SymbolicValue Failed() {
        return SymbolicValue(SymbolicValueType::Unknown);
    }

    const TypeSystem::IField* stateField_;
    bool legacyVisualBasic_;
    std::vector<ILVariable*> stateVariables_;
};

} // namespace ILSpy::Decompiler::IL::ControlFlow
