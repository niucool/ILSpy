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

// The out-of-line pieces of the dynamic-call nodes (the enum-name helpers,
// the binder-flag and argument-list renderers, the child-slot mechanics of
// the fixed-operand nodes). See DynamicInstructions.hpp for the family.

#include "Decompiler/IL/Instructions/DynamicInstructions.hpp"


namespace ILSpy::Decompiler::IL {

namespace {

// The C# ExpressionType member names, in declaration order (the dump's
// Operation.ToString()).
constexpr const char* kExpressionTypeNames[] = {
    "Add", "AddChecked", "And", "AndAlso", "ArrayLength", "ArrayIndex",
    "Call", "Coalesce", "Conditional", "Constant", "Convert", "ConvertChecked",
    "Divide", "Equal", "ExclusiveOr", "GreaterThan", "GreaterThanOrEqual",
    "Invoke", "Lambda", "LeftShift", "LessThan", "LessThanOrEqual", "ListInit",
    "MemberAccess", "MemberInit", "Modulo", "Multiply", "MultiplyChecked",
    "Negate", "UnaryPlus", "NegateChecked", "New", "NewArrayInit",
    "NewArrayBounds", "Not", "NotEqual", "Or", "OrElse", "Parameter", "Power",
    "Quote", "RightShift", "Subtract", "SubtractChecked", "TypeAs", "TypeIs",
    "Assign", "Block", "DebugInfo", "Decrement", "Dynamic", "Default",
    "Extension", "Goto", "Increment", "Index", "Label", "RuntimeVariables",
    "Loop", "Switch", "Throw", "Try", "Unbox", "AddAssign", "AndAssign",
    "DivideAssign", "ExclusiveOrAssign", "LeftShiftAssign", "ModuloAssign",
    "MultiplyAssign", "OrAssign", "PowerAssign", "RightShiftAssign",
    "SubtractAssign", "AddAssignChecked", "MultiplyAssignChecked",
    "SubtractAssignChecked", "PreIncrementAssign", "PreDecrementAssign",
    "PostIncrementAssign", "PostDecrementAssign", "TypeEqual",
    "OnesComplement", "IsTrue", "IsFalse",
};

} // namespace

std::string ExpressionTypeName(ExpressionType op) {
    int index = static_cast<int>(op);
    if (index < 0 || index >= static_cast<int>(
                              sizeof(kExpressionTypeNames) /
                              sizeof(kExpressionTypeNames[0])))
        return std::to_string(index);
    return kExpressionTypeNames[static_cast<std::size_t>(index)];
}

std::string CSharpArgumentInfoFlagsName(CSharpArgumentInfoFlags flags) {
    // The C# enum.ToString() on a flags value: the set member names joined
    // with ", ".
    static const struct {
        CSharpArgumentInfoFlags flag;
        const char* name;
    } kMembers[] = {
        {CSharpArgumentInfoFlags::UseCompileTimeType, "UseCompileTimeType"},
        {CSharpArgumentInfoFlags::Constant, "Constant"},
        {CSharpArgumentInfoFlags::NamedArgument, "NamedArgument"},
        {CSharpArgumentInfoFlags::IsRef, "IsRef"},
        {CSharpArgumentInfoFlags::IsOut, "IsOut"},
        {CSharpArgumentInfoFlags::IsStaticType, "IsStaticType"},
    };
    std::string result;
    for (const auto& m : kMembers) {
        if ((static_cast<std::uint32_t>(flags) &
             static_cast<std::uint32_t>(m.flag)) != 0) {
            if (!result.empty()) result += ", ";
            result += m.name;
        }
    }
    if (result.empty()) return "None";
    return result;
}

void DynamicInstruction::WriteBinderFlags(std::string& out) const {
    // The C# WriteBinderFlags (DynamicInstructions.cs lines 80-103): each set
    // flag appends its mnemonic suffix.
    if (HasBinderFlag(CSharpBinderFlags::BinaryOperationLogical))
        out += ".logic";
    if (HasBinderFlag(CSharpBinderFlags::CheckedContext))
        out += ".checked";
    if (HasBinderFlag(CSharpBinderFlags::ConvertArrayIndex))
        out += ".arrayindex";
    if (HasBinderFlag(CSharpBinderFlags::ConvertExplicit))
        out += ".explicit";
    if (HasBinderFlag(CSharpBinderFlags::InvokeSimpleName))
        out += ".invokesimple";
    if (HasBinderFlag(CSharpBinderFlags::InvokeSpecialName))
        out += ".invokespecial";
    if (HasBinderFlag(CSharpBinderFlags::ResultDiscarded))
        out += ".discard";
    if (HasBinderFlag(CSharpBinderFlags::ResultIndexed))
        out += ".resultindexed";
    if (HasBinderFlag(CSharpBinderFlags::ValueFromCompoundAssignment))
        out += ".compound";
}

void DynamicInstruction::WriteArgumentInfo(std::string& out,
                                          const CSharpArgumentInfo& info) {
    // The C# WriteArgumentList per-argument prefix:
    // "[flags: F, name: N] ".
    out += "[flags: ";
    out += CSharpArgumentInfoFlagsName(info.Flags);
    out += ", name: " + info.Name + "] ";
}

// ---------------------------------------------------------------------------
// DynamicBinaryOperatorInstruction
// ---------------------------------------------------------------------------

void DynamicBinaryOperatorInstruction::AdoptChildren() {
    if (Left) { Left->Parent = this; Left->ChildIndex = 0; }
    if (Right) { Right->Parent = this; Right->ChildIndex = 1; }
}

void DynamicBinaryOperatorInstruction::RenumberChildren() {
    AdoptChildren();
}

ILInstruction* DynamicBinaryOperatorInstruction::GetChild(int i) const {
    switch (i) {
        case 0: return Left.get();
        case 1: return Right.get();
        default: return nullptr;
    }
}

std::unique_ptr<ILInstruction> DynamicBinaryOperatorInstruction::SetChildRaw(
    int i, std::unique_ptr<ILInstruction> n) {
    std::unique_ptr<ILInstruction> old;
    switch (i) {
        case 0: old = std::move(Left); Left = std::move(n); break;
        case 1: old = std::move(Right); Right = std::move(n); break;
        default: return n;
    }
    AdoptChildren();
    return old;
}

void DynamicBinaryOperatorInstruction::WriteTo(std::string& out) const {
    out += "dynamic.binary.operator";
    WriteBinderFlags(out);
    out += ' ';
    out += ExpressionTypeName(Operation);
    out += "([flags: ";
    out += CSharpArgumentInfoFlagsName(LeftArgumentInfo.Flags);
    out += ", name: " + LeftArgumentInfo.Name + "] ";
    if (Left) Left->WriteTo(out);
    out += ", [flags: ";
    out += CSharpArgumentInfoFlagsName(RightArgumentInfo.Flags);
    out += ", name: " + RightArgumentInfo.Name + "] ";
    if (Right) Right->WriteTo(out);
    out += ')';
}

// ---------------------------------------------------------------------------
// DynamicUnaryOperatorInstruction
// ---------------------------------------------------------------------------

std::unique_ptr<ILInstruction> DynamicUnaryOperatorInstruction::SetChildRaw(
    int i, std::unique_ptr<ILInstruction> n) {
    if (i != 0) return n;
    auto old = std::move(Operand);
    Operand = std::move(n);
    if (Operand) { Operand->Parent = this; Operand->ChildIndex = 0; }
    return old;
}

void DynamicUnaryOperatorInstruction::WriteTo(std::string& out) const {
    out += "dynamic.unary.operator";
    WriteBinderFlags(out);
    out += ' ';
    out += ExpressionTypeName(Operation);
    // The C# WriteArgumentList over the (Operand, OperandArgumentInfo) pair
    // (DynamicInstructions.cs lines 498-506).
    out += '(';
    WriteArgumentInfo(out, OperandArgumentInfo);
    if (Operand) Operand->WriteTo(out);
    out += ')';
}

// ---------------------------------------------------------------------------
// DynamicConvertInstruction
// ---------------------------------------------------------------------------

std::unique_ptr<ILInstruction> DynamicConvertInstruction::SetChildRaw(
    int i, std::unique_ptr<ILInstruction> n) {
    if (i != 0) return n;
    auto old = std::move(Argument);
    Argument = std::move(n);
    if (Argument) { Argument->Parent = this; Argument->ChildIndex = 0; }
    return old;
}

void DynamicConvertInstruction::WriteTo(std::string& out) const {
    out += "dynamic.convert";
    WriteBinderFlags(out);
    out += ' ';
    if (Type != nullptr) out += Type->ReflectionName();
    out += '(';
    if (Argument) Argument->WriteTo(out);
    out += ')';
}

// ---------------------------------------------------------------------------
// DynamicGetMemberInstruction
// ---------------------------------------------------------------------------

std::unique_ptr<ILInstruction> DynamicGetMemberInstruction::SetChildRaw(
    int i, std::unique_ptr<ILInstruction> n) {
    if (i != 0) return n;
    auto old = std::move(Target);
    Target = std::move(n);
    if (Target) { Target->Parent = this; Target->ChildIndex = 0; }
    return old;
}

void DynamicGetMemberInstruction::WriteTo(std::string& out) const {
    out += "dynamic.getmember";
    WriteBinderFlags(out);
    out += ' ';
    out += Name;
    out += "([flags: ";
    out += CSharpArgumentInfoFlagsName(TargetArgumentInfo.Flags);
    out += ", name: " + TargetArgumentInfo.Name + "] ";
    if (Target) Target->WriteTo(out);
    out += ')';
}

// ---------------------------------------------------------------------------
// DynamicSetMemberInstruction
// ---------------------------------------------------------------------------

ILInstruction* DynamicSetMemberInstruction::GetChild(int i) const {
    switch (i) {
        case 0: return Target.get();
        case 1: return Value.get();
        default: return nullptr;
    }
}

void DynamicSetMemberInstruction::RenumberChildren() {
    if (Target) { Target->Parent = this; Target->ChildIndex = 0; }
    if (Value) { Value->Parent = this; Value->ChildIndex = 1; }
}

std::unique_ptr<ILInstruction> DynamicSetMemberInstruction::SetChildRaw(
    int i, std::unique_ptr<ILInstruction> n) {
    std::unique_ptr<ILInstruction> old;
    switch (i) {
        case 0: old = std::move(Target); Target = std::move(n); break;
        case 1: old = std::move(Value); Value = std::move(n); break;
        default: return n;
    }
    RenumberChildren();
    return old;
}

void DynamicSetMemberInstruction::WriteTo(std::string& out) const {
    out += "dynamic.setmember";
    WriteBinderFlags(out);
    out += ' ';
    out += Name;
    out += "([flags: ";
    out += CSharpArgumentInfoFlagsName(TargetArgumentInfo.Flags);
    out += ", name: " + TargetArgumentInfo.Name + "] ";
    if (Target) Target->WriteTo(out);
    out += ", [flags: ";
    out += CSharpArgumentInfoFlagsName(ValueArgumentInfo.Flags);
    out += ", name: " + ValueArgumentInfo.Name + "] ";
    if (Value) Value->WriteTo(out);
    out += ')';
}

// ---------------------------------------------------------------------------
// DynamicIsEventInstruction
// ---------------------------------------------------------------------------

std::unique_ptr<ILInstruction> DynamicIsEventInstruction::SetChildRaw(
    int i, std::unique_ptr<ILInstruction> n) {
    if (i != 0) return n;
    auto old = std::move(Argument);
    Argument = std::move(n);
    if (Argument) { Argument->Parent = this; Argument->ChildIndex = 0; }
    return old;
}

void DynamicIsEventInstruction::WriteTo(std::string& out) const {
    out += "dynamic.isevent";
    WriteBinderFlags(out);
    out += ' ';
    out += Name;
    out += '(';
    if (Argument) Argument->WriteTo(out);
    out += ')';
}

// ---------------------------------------------------------------------------
// DynamicArgumentsInstruction (the shared collection mechanics)
// ---------------------------------------------------------------------------

void DynamicArgumentsInstruction::WriteArguments(std::string& out) const {
    // The C# WriteArgumentList: '(' then the per-argument
    // "[flags: F, name: N] operand" (comma-separated), then ')'.
    out += '(';
    for (std::size_t i = 0; i < Arguments.size(); i++) {
        if (i > 0) out += ", ";
        CSharpArgumentInfo info;
        if (i < ArgumentInfo.size())
            info = ArgumentInfo[i];
        WriteArgumentInfo(out, info);
        if (Arguments[i]) Arguments[i]->WriteTo(out);
    }
    out += ')';
}

std::unique_ptr<ILInstruction> DynamicArgumentsInstruction::SetChildRaw(
    int i, std::unique_ptr<ILInstruction> n) {
    if (i < 0 || i >= static_cast<int>(Arguments.size())) return n;
    auto old = std::move(Arguments[static_cast<std::size_t>(i)]);
    Arguments[static_cast<std::size_t>(i)] = std::move(n);
    RenumberChildren();
    return old;
}

void DynamicGetIndexInstruction::WriteTo(std::string& out) const {
    out += "dynamic.getindex";
    WriteBinderFlags(out);
    out += ' ';
    out += "get_Item";
    WriteArguments(out);
}

void DynamicSetIndexInstruction::WriteTo(std::string& out) const {
    out += "dynamic.setindex";
    WriteBinderFlags(out);
    out += ' ';
    out += "set_Item";
    WriteArguments(out);
}

void DynamicInvokeInstruction::WriteTo(std::string& out) const {
    out += "dynamic.invoke";
    WriteBinderFlags(out);
    out += ' ';
    WriteArguments(out);
}

void DynamicInvokeMemberInstruction::WriteTo(std::string& out) const {
    out += "dynamic.invokemember";
    WriteBinderFlags(out);
    out += ' ';
    out += Name;
    if (!TypeArguments.empty()) {
        out += '<';
        for (std::size_t i = 0; i < TypeArguments.size(); i++) {
            if (i > 0) out += ", ";
            if (TypeArguments[i] != nullptr)
                out += TypeArguments[i]->ReflectionName();
        }
        out += '>';
    }
    WriteArguments(out);
}

void DynamicInvokeConstructorInstruction::WriteTo(std::string& out) const {
    out += "dynamic.invokeconstructor";
    WriteBinderFlags(out);
    out += ' ';
    if (ConstructedType != nullptr)
        out += ConstructedType->ReflectionName();
    out += ".ctor";
    WriteArguments(out);
}

} // namespace ILSpy::Decompiler::IL
