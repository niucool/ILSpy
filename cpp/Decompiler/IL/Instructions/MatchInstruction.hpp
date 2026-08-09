// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// MatchInstruction: the ILAst node for a C# `is` pattern. Faithful to the
// generated MatchInstruction in ICSharpCode.Decompiler/IL/Instructions.cs and
// the hand-written MatchInstruction.cs. The node evaluates its TestedOperand,
// then matches the value against the pattern encoded in the flags: a type
// test (CheckType), a non-null test (CheckNotNull), or a plain `var` capture
// (neither). It stores the matched value into Variable (it is an
// IStoreInstruction in the C#) and evaluates to I4 -- 1 if the pattern matches,
// 0 otherwise. SubPatterns carries the recursive/positional sub-patterns of a
// recursive pattern (`expr is C { A: var x } z`).
//
// This is a tested-but-not-yet-wired foundation (like LongSet / LoopContext /
// the NullableLifting helpers): no pipeline transform constructs it yet. The
// next in-order transform, PatternMatchingTransform, builds MatchInstructions
// from isinst + null-test blocks; this node is the prerequisite it needs.
//
// Deferred vs the C# generated node: the deconstruct patterns
// (IsDeconstructCall / IsDeconstructTuple + the IMethod operand and the
// GetDeconstructResultType / NumPositionalPatterns / IsDeconstructMethod
// helpers) need an IMethod type-system object and TupleType, neither of which
// this port carries yet. The flags and helpers for the type/non-null/var
// patterns -- the ones PatternMatchValueTypes/PatternMatchRefTypes produce --
// are ported; IsVar therefore reduces to "!CheckType && !CheckNotNull &&
// SubPatterns.empty()". Add the deconstruct fields when positional patterns
// land. Per decision D1 the generated *output* is the source of truth.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <cassert>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

class MatchInstruction : public ILInstruction {
public:
    // The pattern variable: the value that matched is stored here. A
    // reference (not a tree child), faithful to the generated node. As an
    // IStoreInstruction this counts as a store in ComputeVariableUsage
    // (VariableUsage.cpp handles OpCode::MatchInstruction).
    ILVariablePtr Variable;
    // The expression being matched (slot 0, inlineable).
    std::unique_ptr<ILInstruction> TestedOperand;
    // The recursive/positional sub-patterns (slots 1..N). Empty for the
    // type/non-null/var patterns PatternMatchValueTypes/PatternMatchRefTypes
    // produce; populated by the recursive-pattern path (deferred).
    std::vector<std::unique_ptr<ILInstruction>> SubPatterns;

    // Pattern flags (the deconstruct pair is deferred -- see file header).
    bool CheckType = false;     // match.type[T](v = expr)  -- `expr is T v`
    bool CheckNotNull = false;  // match.notnull(v = expr)  -- `expr is {} v`

    MatchInstruction(ILVariablePtr variable, std::unique_ptr<ILInstruction> testedOperand)
        : ILInstruction(OpCode::MatchInstruction), Variable(std::move(variable)),
          TestedOperand(std::move(testedOperand)) {
        if (TestedOperand) { TestedOperand->Parent = this; TestedOperand->ChildIndex = 0; }
    }

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayWriteLocals | InstructionFlags::SideEffect |
               InstructionFlags::MayThrow | InstructionFlags::ControlFlow;
    }
    // The base Flags() is DirectFlags() | union(children Flags()), which is
    // exactly the C# ComputeFlags (MayWriteLocals | testedOperand.Flags |
    // SubPatterns flags | SideEffect | MayThrow | ControlFlow). No override.
    StackType ResultType() const override { return StackType::I4; }

    int ChildCount() const override {
        return (TestedOperand ? 1 : 0) + static_cast<int>(SubPatterns.size());
    }
    ILInstruction* GetChild(int i) const override {
        if (i == 0) return TestedOperand.get();
        int s = i - 1;
        return (s >= 0 && s < static_cast<int>(SubPatterns.size())) ? SubPatterns[s].get() : nullptr;
    }

    // Append a sub-pattern, wiring its parent/index (mirrors SwitchInstruction's
    // AddSection: SubPatterns are owned ILInstructions in the tree).
    void AddSubPattern(std::unique_ptr<ILInstruction> p) {
        if (!p) return;
        p->Parent = this;
        p->ChildIndex = static_cast<int>(SubPatterns.size()) + 1;
        SubPatterns.push_back(std::move(p));
    }

    // `expr is var x` -- a plain capture with no type/non-null test and no
    // sub-patterns. (The C# also requires !IsDeconstructCall && !IsDeconstructTuple;
    // those are deferred -- see file header.)
    bool IsVar() const { return !CheckType && !CheckNotNull && SubPatterns.empty(); }

    // Whether the pattern binds a designator variable. A pattern with N
    // sub-patterns only needs a designator if the variable is used beyond the
    // sub-patterns' own uses. Faithful to MatchInstruction.HasDesignator.
    bool HasDesignator() const {
        int use = (Variable ? Variable->LoadCount + Variable->AddressCount : 0);
        return use > static_cast<int>(SubPatterns.size());
    }

    void WriteTo(std::string& out) const override {
        out += "match";
        if (CheckNotNull) out += ".notnull";
        if (CheckType) {
            out += ".type[";
            out += (Variable && Variable->Type) ? Variable->Type->ReflectionName() : std::string("?");
            out += ']';
        }
        out += '(';
        out += Variable ? Variable->Name : std::string("?");
        out += " = ";
        if (TestedOperand) TestedOperand->WriteTo(out); else out += "(null)";
        out += ')';
        if (!SubPatterns.empty()) {
            out += " {\n";
            for (auto& p : SubPatterns) {
                out += "    ";
                if (p) p->WriteTo(out); else out += "(null)";
                out += '\n';
            }
            out += "  }";
        }
    }

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(int i, std::unique_ptr<ILInstruction> n) override {
        if (i == 0) { auto old = std::move(TestedOperand); TestedOperand = std::move(n); return old; }
        int s = i - 1;
        if (s < 0 || s >= static_cast<int>(SubPatterns.size())) return n;
        auto old = std::move(SubPatterns[s]);
        SubPatterns[s] = std::move(n);
        return old;
    }

public:
    // ---- Static pattern helpers (MatchInstruction.cs) ----

    // The constant-bearing instruction kinds a relational/constant pattern
    // matches against (MatchInstruction.IsConstant). LdcDecimal is deferred
    // (no node in this port); the rest are ported.
    static bool IsConstant(const ILInstruction* inst) {
        if (!inst) return false;
        switch (inst->Op) {
            case OpCode::LdcI4:
            case OpCode::LdcI8:
            case OpCode::LdcF4:
            case OpCode::LdcF8:
            case OpCode::LdNull:
                return true;
            default:
                return false;
        }
    }

    // Whether `inst` is a pattern-match-shaped instruction and, if so, the
    // expression it tests. Port of MatchInstruction.IsPatternMatch. A null
    // settings pointer is treated as all-features-on (the C# `?? true`),
    // matching a caller that has no settings to consult.
    //
    // Cases: a MatchInstruction tests its TestedOperand; a Comp is a constant
    // pattern (comp.Left tested against comp.Right) -- or, when wrapped as
    // logic.not (comp eq X 0), a negated pattern gated on PatternCombinators; a
    // `string.op_Equality(x, "lit")` call is a string constant pattern. The
    // decimal op_Equality case is deferred (no LdcDecimal node).
    static bool IsPatternMatch(const ILInstruction* inst, const ILInstruction*& testedOperand,
                               const ILTransformSettings* settings = nullptr) {
        testedOperand = nullptr;
        if (!inst) return false;
        bool patternCombinators = !settings || settings->PatternCombinators;
        bool relationalPatterns = !settings || settings->RelationalPatterns;
        switch (inst->Op) {
            case OpCode::MatchInstruction: {
                const auto* m = static_cast<const MatchInstruction*>(inst);
                testedOperand = m->TestedOperand.get();
                return true;
            }
            case OpCode::Comp: {
                const auto* comp = static_cast<const Comp*>(inst);
                // logic.not(X) is comp(Equality, X, ldc.i4 0) in this port
                // (the reader emits brfalse that way; see SwitchAnalysis).
                if (MatchLogicNot(comp)) {
                    if (IsPatternMatch(comp->Left.get(), testedOperand, settings))
                        return patternCombinators;
                    return false;
                }
                testedOperand = comp->Left.get();
                if (!relationalPatterns &&
                    comp->Kind != ComparisonKind::Equality &&
                    comp->Kind != ComparisonKind::Inequality)
                    return false;
                if (!patternCombinators && comp->Kind == ComparisonKind::Inequality)
                    return false;
                return IsConstant(comp->Right.get());
            }
            case OpCode::Call: {
                const auto* call = static_cast<const Call*>(inst);
                if (IsCallToOpEqualityOnString(call)) {
                    if (call->Arguments.size() >= 1) testedOperand = call->Arguments[0].get();
                    return call->Arguments.size() == 2 &&
                           call->Arguments[1] && call->Arguments[1]->Op == OpCode::LdStr;
                }
                return false;
            }
            default:
                return false;
        }
    }

private:
    // logic.not(X) == comp(Equality, X, ldc.i4(0)) (the reader's brfalse shape;
    // see SwitchAnalysis / ConditionDetection). Sign-independent, so the
    // Unsigned flag is irrelevant -- a `comp.eq.un X 0` is not a brfalse shape.
    static bool MatchLogicNot(const Comp* comp) {
        return comp && comp->Kind == ComparisonKind::Equality && !comp->Unsigned &&
               comp->Right && comp->Right->Op == OpCode::LdcI4 &&
               static_cast<const LdcI4*>(comp->Right.get())->Value == 0;
    }

    // `string.op_Equality(x, y)` -- a resolved declaring type of KnownTypeCode
    //::String and the method name ending in "::op_Equality". The C# checks
    // call.Method.IsOperator && Name == "op_Equality" && DeclaringType is
    // String; this port has no IsOperator flag, so the name suffix is the
    // approximation (op_Equality is always an operator).
    static bool IsCallToOpEqualityOnString(const Call* call) {
        if (!call) return false;
        if (call->Arguments.size() != 2) return false;
        static const std::string suffix = "::op_Equality";
        const std::string& name = call->MethodName;
        if (name.size() <= suffix.size()) return false;
        if (name.compare(name.size() - suffix.size(), suffix.size(), suffix) != 0) return false;
        if (!call->DeclaringType) return false;
        auto* k = dynamic_cast<const TypeSystem::KnownType*>(call->DeclaringType.get());
        return k && k->Code() == TypeSystem::KnownTypeCode::String;
    }
};

} // namespace ILSpy::Decompiler::IL
