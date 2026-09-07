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

// Comp: a comparison (ceq/cgt/clt and their .un forms). BinaryInstruction;
// result I4 for an ordinary comparison, O for a ThreeValuedLogic-lifted
// comparison (a SQL-style lifted comparison whose result is itself nullable).
// The ComparisonKind and ComparisonLiftingKind ports follow Comp.cs.

#pragma once

#include "Decompiler/IL/Instructions/BinaryInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

enum class ComparisonKind : std::uint8_t {
    Equality,
    Inequality,
    LessThan,
    LessThanOrEqual,
    GreaterThan,
    GreaterThanOrEqual,
};

// Port of Comp.cs ComparisonLiftingKind. A comparison may be a lifted
// comparison over Nullable<T> operands: None is an ordinary comparison;
// CSharp is the C#-style lifted comparison (operands whose ResultType is O
// are Nullable<T> of this InputType; both-null -> eq=1/ne=0/else=0; one-null
// -> eq=0/ne=1/else=0; else the underlying comparison runs; the ResultType
// stays I4); ThreeValuedLogic is the SQL-style lifted comparison used for
// operator! on bool? (any null input -> null result; the ResultType becomes O).
enum class ComparisonLiftingKind : std::uint8_t {
    None,
    CSharp,
    ThreeValuedLogic,
};

// Port of ComparisonKindExtensions.IsEqualityOrInequality (Comp.cs): whether the
// comparison is one of the two equality forms (the TranslateCeq routing gate).
inline bool IsEqualityOrInequality(ComparisonKind kind) {
    return kind == ComparisonKind::Equality || kind == ComparisonKind::Inequality;
}

// Negate a comparison kind (ECMA-335 II.3.2): == <=> !=, < => >=, <= => >.
// Port of ComparisonKind.Negate in Comp.cs.
inline ComparisonKind NegateComparison(ComparisonKind kind) {
    switch (kind) {
        case ComparisonKind::Equality: return ComparisonKind::Inequality;
        case ComparisonKind::Inequality: return ComparisonKind::Equality;
        case ComparisonKind::LessThan: return ComparisonKind::GreaterThanOrEqual;
        case ComparisonKind::LessThanOrEqual: return ComparisonKind::GreaterThan;
        case ComparisonKind::GreaterThan: return ComparisonKind::LessThanOrEqual;
        case ComparisonKind::GreaterThanOrEqual: return ComparisonKind::LessThan;
    }
    return ComparisonKind::Inequality;  // unreachable
}

class Comp : public BinaryInstruction {
public:
    ComparisonKind Kind = ComparisonKind::Equality;
    // The C# `public readonly Sign Sign` field ("If this is an integer comparison,
    // specifies the sign used to interpret the integers"): None for equality/float/
    // reference comparisons, Signed/Unsigned for the relational integer comparisons.
    // The reader's opcode-dispatch supplies the static value (the C# reader derives
    // it type-dependently in its Comparison helper -- the float/equality shapes are
    // None -- which the port's reader mirrors through the explicit-Sign ctor sites;
    // the doc comment on the reader macro records the residual type-dependent gap).
    // The member is named after the type (the self-named-accessor trap): every type
    // use inside the class body needs the TypeSystem:: qualification.
    TypeSystem::Sign Sign = TypeSystem::Sign::None;
    // The redundant Unsigned view the port's existing consumers read (the C# has no
    // such field): kept consistent in every ctor as `Sign == Sign::Unsigned`.
    bool Unsigned = false;
    // The lifting kind (None for an ordinary comparison). A non-None value
    // marks this Comp as a lifted comparison the nullable-lifting transform
    // produced; IsLifted() reports it and ResultType() flips to O for the
    // ThreeValuedLogic case, faithful to Comp.cs.
    ComparisonLiftingKind LiftingKind = ComparisonLiftingKind::None;
    // The stack type of the comparison inputs. For a non-lifted comparison
    // this is the operands' ResultType (the C# `InputType = left.ResultType`);
    // for a lifted comparison it is the underlying type inside the
    // Nullable<T> operands (the C# explicit-inputType constructor).
    StackType InputType = StackType::I4;

    // Ordinary comparison (the C# `Comp(kind, sign, left, right)`): InputType
    // is the left operand's ResultType, LiftingKind is None. The default
    // args keep every existing call site working. The bool overload maps
    // unsigned->Unsigned / false->Signed (the reader's static relational mapping);
    // the Sign overload carries the exact C# value (the None-sign equality sites).
    Comp(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
         ComparisonKind kind = ComparisonKind::Equality, bool unsigned_ = false)
        : Comp(std::move(left), std::move(right), kind,
               unsigned_ ? TypeSystem::Sign::Unsigned : TypeSystem::Sign::Signed) {}
    Comp(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
         ComparisonKind kind, TypeSystem::Sign sign)
        : BinaryInstruction(OpCode::Comp, std::move(left), std::move(right)),
          Kind(kind), Sign(sign), Unsigned(sign == TypeSystem::Sign::Unsigned) {
        if (Left) InputType = Left->ResultType();
    }
    // Lifted comparison (the C# `Comp(kind, lifting, inputType, sign, left,
    // right)`): LiftingKind and InputType are set explicitly, the
    // nullable-lifting machinery uses this to build a lifted Comp. The bool
    // overload maps unsigned->Unsigned / false->Signed; the Sign overload
    // carries the exact C# value (the lifted machinery propagates comp.Sign).
    Comp(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
         ComparisonKind kind, ComparisonLiftingKind lifting, StackType inputType,
         bool unsigned_ = false)
        : Comp(std::move(left), std::move(right), kind, lifting, inputType,
               unsigned_ ? TypeSystem::Sign::Unsigned : TypeSystem::Sign::Signed) {}
    Comp(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
         ComparisonKind kind, ComparisonLiftingKind lifting, StackType inputType,
         TypeSystem::Sign sign)
        : BinaryInstruction(OpCode::Comp, std::move(left), std::move(right)),
          Kind(kind), Sign(sign), Unsigned(sign == TypeSystem::Sign::Unsigned),
          LiftingKind(lifting), InputType(inputType) {}

    // Port of Comp.IsLifted: a non-None LiftingKind marks a lifted comparison.
    bool IsLifted() const { return LiftingKind != ComparisonLiftingKind::None; }
    // Port of Comp.UnderlyingResultType: the I4 result of the underlying
    // comparison (the lifted comparison's result type is ResultType()).
    StackType UnderlyingResultType() const { return StackType::I4; }
    // Port of Comp.ResultType: I4 for ordinary and C#-style lifted
    // comparisons; O for the SQL-style ThreeValuedLogic lift (a null result
    // is itself a nullable value).
    StackType ResultType() const override {
        return LiftingKind == ComparisonLiftingKind::ThreeValuedLogic
                   ? StackType::O : StackType::I4;
    }
    void WriteTo(std::string& out) const override {
        out += "comp(";
        switch (Kind) {
            case ComparisonKind::Equality: out += "eq"; break;
            case ComparisonKind::Inequality: out += "ne"; break;
            case ComparisonKind::LessThan: out += "lt"; break;
            case ComparisonKind::LessThanOrEqual: out += "le"; break;
            case ComparisonKind::GreaterThan: out += "gt"; break;
            case ComparisonKind::GreaterThanOrEqual: out += "ge"; break;
        }
        if (Unsigned) out += ".un";
        // Faithful to Comp.cs WriteToCore: a lifted comparison annotates the
        // lifting kind. The default (None) renders bare, so existing dumps are
        // unchanged; a C#-style lift adds `.lifted[C#]` and the SQL-style
        // ThreeValuedLogic lift adds `.lifted[3VL]`.
        switch (LiftingKind) {
            case ComparisonLiftingKind::CSharp: out += ".lifted[C#]"; break;
            case ComparisonLiftingKind::ThreeValuedLogic: out += ".lifted[3VL]"; break;
            case ComparisonLiftingKind::None: break;
        }
        out += ", ";
        if (Left) Left->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Right) Right->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
