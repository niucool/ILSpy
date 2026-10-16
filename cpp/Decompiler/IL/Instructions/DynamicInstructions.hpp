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

// The dynamic-call ILAst nodes: the C# `dynamic` operations the
// DynamicCallSiteTransform reconstructs from the CallSite cache pattern.
// Faithful to ICSharpCode.Decompiler/IL/Instructions/DynamicInstructions.cs
// (the enums, the CSharpArgumentInfo struct, the per-node partials) plus the
// sealed class declarations in ICSharpCode.Decompiler/IL/Instructions.cs
// (lines 5476-6420: the slot layouts and the clone shapes).
//
// The dump mnemonics are the C# `originalOpCodeNames` entries
// ("dynamic.getmember", "dynamic.invokemember", ...), and the binder flags
// render as the C# WriteBinderFlags suffixes (".checked", ".logic", ...).
// The argument lists render the C# WriteArgumentList shape (the per-argument
// "[flags: F, name: N] operand" prefix).
//
// This port's InstructionCollection<T> becomes a plain vector of unique_ptr
// with re-parenting accessors (the Call::Arguments convention), and the
// per-child CSharpArgumentInfo lookup (GetArgumentInfoOfChild) stays a pure
// virtual on the base like the C#. DynamicLogicOperatorInstruction (built by
// the logic-operator transforms, not by the callsite transform) is not ported
// with this family; DynamicCompoundAssign already lives with the compound
// assignment nodes.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

// Port of System.Linq.Expressions.ExpressionType (the full 85-member BCL
// enum, in declaration order): the operation kind a dynamic operator carries
// (the Binder.BinaryOperation / Binder.UnaryOperation second argument).
enum class ExpressionType : std::uint8_t {
    Add, AddChecked, And, AndAlso, ArrayLength, ArrayIndex, Call, Coalesce,
    Conditional, Constant, Convert, ConvertChecked, Divide, Equal,
    ExclusiveOr, GreaterThan, GreaterThanOrEqual, Invoke, Lambda, LeftShift,
    LessThan, LessThanOrEqual, ListInit, MemberAccess, MemberInit, Modulo,
    Multiply, MultiplyChecked, Negate, UnaryPlus, NegateChecked, New,
    NewArrayInit, NewArrayBounds, Not, NotEqual, Or, OrElse, Parameter, Power,
    Quote, RightShift, Subtract, SubtractChecked, TypeAs, TypeIs, Assign,
    Block, DebugInfo, Decrement, Dynamic, Default, Extension, Goto, Increment,
    Index, Label, RuntimeVariables, Loop, Switch, Throw, Try, Unbox,
    AddAssign, AndAssign, DivideAssign, ExclusiveOrAssign, LeftShiftAssign,
    ModuloAssign, MultiplyAssign, OrAssign, PowerAssign, RightShiftAssign,
    SubtractAssign, AddAssignChecked, MultiplyAssignChecked,
    SubtractAssignChecked, PreIncrementAssign, PreDecrementAssign,
    PostIncrementAssign, PostDecrementAssign, TypeEqual, OnesComplement,
    IsTrue, IsFalse,
};

// The C# ExpressionType member names (the dump renders Operation.ToString()).
std::string ExpressionTypeName(ExpressionType op);

// Port of CSharpArgumentInfoFlags (DynamicInstructions.cs lines 31-41).
enum class CSharpArgumentInfoFlags : std::uint32_t {
    None = 0,
    UseCompileTimeType = 1,
    Constant = 2,
    NamedArgument = 4,
    IsRef = 8,
    IsOut = 0x10,
    IsStaticType = 0x20,
};

// Port of CSharpBinderFlags (DynamicInstructions.cs lines 43-57).
enum class CSharpBinderFlags : std::uint32_t {
    None = 0,
    CheckedContext = 1,
    InvokeSimpleName = 2,
    InvokeSpecialName = 4,
    BinaryOperationLogical = 8,
    ConvertExplicit = 0x10,
    ConvertArrayIndex = 0x20,
    ResultIndexed = 0x40,
    ValueFromCompoundAssignment = 0x80,
    ResultDiscarded = 0x100,
};

// The C# [Flags] enums compose with | and & (the port's operator overloads).
inline CSharpArgumentInfoFlags operator|(CSharpArgumentInfoFlags a,
                                        CSharpArgumentInfoFlags b) {
    return static_cast<CSharpArgumentInfoFlags>(
        static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline CSharpArgumentInfoFlags operator&(CSharpArgumentInfoFlags a,
                                        CSharpArgumentInfoFlags b) {
    return static_cast<CSharpArgumentInfoFlags>(
        static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline CSharpBinderFlags operator|(CSharpBinderFlags a, CSharpBinderFlags b) {
    return static_cast<CSharpBinderFlags>(static_cast<std::uint32_t>(a) |
                                          static_cast<std::uint32_t>(b));
}
inline CSharpBinderFlags operator&(CSharpBinderFlags a, CSharpBinderFlags b) {
    return static_cast<CSharpBinderFlags>(static_cast<std::uint32_t>(a) &
                                          static_cast<std::uint32_t>(b));
}

// Port of the CSharpArgumentInfo struct (DynamicInstructions.cs lines 59-68):
// the per-argument binder metadata the CSharpArgumentInfo.Create calls carry
// (the flags, the named-argument name, and the compile-time type the binder
// uses for the argument).
struct CSharpArgumentInfo {
    std::string Name;
    CSharpArgumentInfoFlags Flags = CSharpArgumentInfoFlags::None;
    TypeSystem::ITypePtr CompileTimeType;

    bool HasFlag(CSharpArgumentInfoFlags flag) const {
        return (static_cast<std::uint32_t>(Flags) &
                static_cast<std::uint32_t>(flag)) != 0;
    }
};

// The CSharpArgumentInfoFlags member names (the dump's "[flags: ...]"
// prefix). Multiple set bits render comma-separated, like the C#
// enum.ToString().
std::string CSharpArgumentInfoFlagsName(CSharpArgumentInfoFlags flags);

// Port of the abstract DynamicInstruction (Instructions.cs lines 642-660 +
// DynamicInstructions.cs lines 70-127): the shared binder flags and calling
// context. The C# ComputeFlags/DirectFlags (MayThrow | SideEffect) override
// the ILInstruction default for every node in this family.
class DynamicInstruction : public ILInstruction {
public:
    CSharpBinderFlags BinderFlags = CSharpBinderFlags::None;
    // The C# `public IType? CallingContext` -- the type the call site was
    // compiled in (the Binder.* context-type argument); null when the
    // references were missing.
    TypeSystem::ITypePtr CallingContext;

    DynamicInstruction(OpCode opCode, CSharpBinderFlags binderFlags,
                       TypeSystem::ITypePtr context)
        : ILInstruction(opCode), BinderFlags(binderFlags),
          CallingContext(std::move(context)) {}

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayThrow | InstructionFlags::SideEffect;
    }

    // The C# `public abstract CSharpArgumentInfo GetArgumentInfoOfChild(
    // int index)`: the binder metadata for the operand slot.
    virtual CSharpArgumentInfo GetArgumentInfoOfChild(int index) const = 0;

protected:
    // The C# WriteBinderFlags (DynamicInstructions.cs lines 80-103): the
    // mnemonic flag suffixes (".logic", ".checked", ...).
    void WriteBinderFlags(std::string& out) const;
    bool HasBinderFlag(CSharpBinderFlags flag) const {
        return (static_cast<std::uint32_t>(BinderFlags) &
                static_cast<std::uint32_t>(flag)) != 0;
    }
    // The C# WriteArgumentList: "(...)" with the per-argument
    // "[flags: F, name: N] operand" prefix.
    static void WriteArgumentInfo(std::string& out,
                                  const CSharpArgumentInfo& info);
};

// ---------------------------------------------------------------------------
// The fixed-operand nodes.
// ---------------------------------------------------------------------------

// Port of DynamicBinaryOperatorInstruction (Instructions.cs lines 5567+):
// `dynamic.binary.operator Operation(left, right)`; the result is dynamic (O)
// -- except the IsTrue/IsFalse operations, whose result is bool (I4).
class DynamicBinaryOperatorInstruction : public DynamicInstruction {
public:
    CSharpArgumentInfo LeftArgumentInfo;
    CSharpArgumentInfo RightArgumentInfo;
    ExpressionType Operation = ExpressionType::Add;
    std::unique_ptr<ILInstruction> Left;
    std::unique_ptr<ILInstruction> Right;

    DynamicBinaryOperatorInstruction(
        CSharpBinderFlags binderFlags, ExpressionType operation,
        TypeSystem::ITypePtr context, CSharpArgumentInfo leftArgumentInfo,
        std::unique_ptr<ILInstruction> left,
        CSharpArgumentInfo rightArgumentInfo,
        std::unique_ptr<ILInstruction> right)
        : DynamicInstruction(OpCode::DynamicBinaryOperatorInstruction,
                             binderFlags, std::move(context)),
          LeftArgumentInfo(std::move(leftArgumentInfo)),
          RightArgumentInfo(std::move(rightArgumentInfo)),
          Operation(operation), Left(std::move(left)),
          Right(std::move(right)) {
        AdoptChildren();
    }

    StackType ResultType() const override {
        return Operation == ExpressionType::IsTrue ||
                       Operation == ExpressionType::IsFalse
                   ? StackType::I4
                   : StackType::O;
    }
    CSharpArgumentInfo GetArgumentInfoOfChild(int index) const override {
        return index == 0 ? LeftArgumentInfo : RightArgumentInfo;
    }
    int ChildCount() const override {
        return (Left ? 1 : 0) + (Right ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override;
    void WriteTo(std::string& out) const override;
    void AdoptChildren();
    void RenumberChildren();

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(
        int i, std::unique_ptr<ILInstruction> n) override;
};

// Port of DynamicUnaryOperatorInstruction (Instructions.cs lines 5667+):
// `dynamic.unary.operator Operation(operand)`; the result is dynamic (O) --
// except IsTrue/IsFalse (I4).
class DynamicUnaryOperatorInstruction : public DynamicInstruction {
public:
    CSharpArgumentInfo OperandArgumentInfo;
    ExpressionType Operation = ExpressionType::Negate;
    std::unique_ptr<ILInstruction> Operand;

    DynamicUnaryOperatorInstruction(
        CSharpBinderFlags binderFlags, ExpressionType operation,
        TypeSystem::ITypePtr context, CSharpArgumentInfo operandArgumentInfo,
        std::unique_ptr<ILInstruction> operand)
        : DynamicInstruction(OpCode::DynamicUnaryOperatorInstruction,
                             binderFlags, std::move(context)),
          OperandArgumentInfo(std::move(operandArgumentInfo)),
          Operation(operation), Operand(std::move(operand)) {
        if (Operand) { Operand->Parent = this; Operand->ChildIndex = 0; }
    }

    StackType ResultType() const override {
        return Operation == ExpressionType::IsTrue ||
                       Operation == ExpressionType::IsFalse
                   ? StackType::I4
                   : StackType::O;
    }
    CSharpArgumentInfo GetArgumentInfoOfChild(int index) const override {
        (void)index;
        return OperandArgumentInfo;
    }
    int ChildCount() const override { return Operand ? 1 : 0; }
    ILInstruction* GetChild(int i) const override {
        return i == 0 ? Operand.get() : nullptr;
    }
    void WriteTo(std::string& out) const override;

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(
        int i, std::unique_ptr<ILInstruction> n) override;
};

// Port of DynamicConvertInstruction (Instructions.cs lines 5750+):
// `dynamic.convert Type(argument)`; the result is the target type's stack
// type. IsChecked/IsExplicit read the binder flags.
class DynamicConvertInstruction : public DynamicInstruction {
public:
    TypeSystem::ITypePtr Type;
    std::unique_ptr<ILInstruction> Argument;

    DynamicConvertInstruction(CSharpBinderFlags binderFlags,
                             TypeSystem::ITypePtr type,
                             TypeSystem::ITypePtr context,
                             std::unique_ptr<ILInstruction> argument)
        : DynamicInstruction(OpCode::DynamicConvertInstruction, binderFlags,
                             std::move(context)),
          Type(std::move(type)), Argument(std::move(argument)) {
        if (Argument) { Argument->Parent = this; Argument->ChildIndex = 0; }
    }

    StackType ResultType() const override {
        return Type != nullptr ? StackTypeOf(Type) : StackType::Unknown;
    }
    // The C# `public bool IsChecked => (BinderFlags &
    // CSharpBinderFlags.CheckedContext) != 0`.
    bool IsChecked() const {
        return HasBinderFlag(CSharpBinderFlags::CheckedContext);
    }
    // The C# `public bool IsExplicit => (BinderFlags &
    // CSharpBinderFlags.ConvertExplicit) != 0`.
    bool IsExplicit() const {
        return HasBinderFlag(CSharpBinderFlags::ConvertExplicit);
    }
    CSharpArgumentInfo GetArgumentInfoOfChild(int index) const override {
        (void)index;
        return CSharpArgumentInfo();
    }
    int ChildCount() const override { return Argument ? 1 : 0; }
    ILInstruction* GetChild(int i) const override {
        return i == 0 ? Argument.get() : nullptr;
    }
    void WriteTo(std::string& out) const override;

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(
        int i, std::unique_ptr<ILInstruction> n) override;
};

// Port of DynamicGetMemberInstruction (Instructions.cs lines 5844+):
// `dynamic.getmember Name((target))`; the result is dynamic (O).
class DynamicGetMemberInstruction : public DynamicInstruction {
public:
    std::string Name;
    CSharpArgumentInfo TargetArgumentInfo;
    std::unique_ptr<ILInstruction> Target;

    DynamicGetMemberInstruction(CSharpBinderFlags binderFlags,
                               std::string name,
                               TypeSystem::ITypePtr context,
                               CSharpArgumentInfo targetArgumentInfo,
                               std::unique_ptr<ILInstruction> target)
        : DynamicInstruction(OpCode::DynamicGetMemberInstruction, binderFlags,
                             std::move(context)),
          Name(std::move(name)),
          TargetArgumentInfo(std::move(targetArgumentInfo)),
          Target(std::move(target)) {
        if (Target) { Target->Parent = this; Target->ChildIndex = 0; }
    }

    StackType ResultType() const override { return StackType::O; }
    CSharpArgumentInfo GetArgumentInfoOfChild(int index) const override {
        (void)index;
        return TargetArgumentInfo;
    }
    int ChildCount() const override { return Target ? 1 : 0; }
    ILInstruction* GetChild(int i) const override {
        return i == 0 ? Target.get() : nullptr;
    }
    void WriteTo(std::string& out) const override;

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(
        int i, std::unique_ptr<ILInstruction> n) override;
};

// Port of DynamicSetMemberInstruction (Instructions.cs lines 5932+):
// `dynamic.setmember Name((target), (value))`; the result is dynamic (O).
class DynamicSetMemberInstruction : public DynamicInstruction {
public:
    std::string Name;
    CSharpArgumentInfo TargetArgumentInfo;
    std::unique_ptr<ILInstruction> Target;
    CSharpArgumentInfo ValueArgumentInfo;
    std::unique_ptr<ILInstruction> Value;

    DynamicSetMemberInstruction(
        CSharpBinderFlags binderFlags, std::string name,
        TypeSystem::ITypePtr context, CSharpArgumentInfo targetArgumentInfo,
        std::unique_ptr<ILInstruction> target,
        CSharpArgumentInfo valueArgumentInfo,
        std::unique_ptr<ILInstruction> value)
        : DynamicInstruction(OpCode::DynamicSetMemberInstruction, binderFlags,
                             std::move(context)),
          Name(std::move(name)),
          TargetArgumentInfo(std::move(targetArgumentInfo)),
          Target(std::move(target)),
          ValueArgumentInfo(std::move(valueArgumentInfo)),
          Value(std::move(value)) {
        if (Target) { Target->Parent = this; Target->ChildIndex = 0; }
        if (Value) { Value->Parent = this; Value->ChildIndex = 1; }
    }

    StackType ResultType() const override { return StackType::O; }
    CSharpArgumentInfo GetArgumentInfoOfChild(int index) const override {
        return index == 0 ? TargetArgumentInfo : ValueArgumentInfo;
    }
    int ChildCount() const override {
        return (Target ? 1 : 0) + (Value ? 1 : 0);
    }
    ILInstruction* GetChild(int i) const override;
    void WriteTo(std::string& out) const override;
    void RenumberChildren();

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(
        int i, std::unique_ptr<ILInstruction> n) override;
};

// Port of DynamicIsEventInstruction (Instructions.cs lines 6392+):
// `dynamic.isevent Name((argument))`; the result is bool (I4).
class DynamicIsEventInstruction : public DynamicInstruction {
public:
    std::string Name;
    std::unique_ptr<ILInstruction> Argument;

    DynamicIsEventInstruction(CSharpBinderFlags binderFlags, std::string name,
                              TypeSystem::ITypePtr context,
                              std::unique_ptr<ILInstruction> argument)
        : DynamicInstruction(OpCode::DynamicIsEventInstruction, binderFlags,
                             std::move(context)),
          Name(std::move(name)), Argument(std::move(argument)) {
        if (Argument) { Argument->Parent = this; Argument->ChildIndex = 0; }
    }

    StackType ResultType() const override { return StackType::I4; }
    CSharpArgumentInfo GetArgumentInfoOfChild(int index) const override {
        (void)index;
        return CSharpArgumentInfo();
    }
    int ChildCount() const override { return Argument ? 1 : 0; }
    ILInstruction* GetChild(int i) const override {
        return i == 0 ? Argument.get() : nullptr;
    }
    void WriteTo(std::string& out) const override;

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(
        int i, std::unique_ptr<ILInstruction> n) override;
};

// ---------------------------------------------------------------------------
// The variable-argument nodes (the C# InstructionCollection Arguments slots;
// this port models the collection as a re-parenting vector, the
// Call::Arguments convention).
// ---------------------------------------------------------------------------

// The shared variable-argument mechanics: the children are the Arguments
// vector; the per-child CSharpArgumentInfo lookup is the subclass's
// ArgumentInfo list.
class DynamicArgumentsInstruction : public DynamicInstruction {
public:
    std::vector<CSharpArgumentInfo> ArgumentInfo;
    std::vector<std::unique_ptr<ILInstruction>> Arguments;

    DynamicArgumentsInstruction(OpCode opCode, CSharpBinderFlags binderFlags,
                               TypeSystem::ITypePtr context,
                               std::vector<CSharpArgumentInfo> argumentInfo,
                               std::vector<std::unique_ptr<ILInstruction>>
                                   arguments)
        : DynamicInstruction(opCode, binderFlags, std::move(context)),
          ArgumentInfo(std::move(argumentInfo)),
          Arguments(std::move(arguments)) {
        RenumberChildren();
    }

    CSharpArgumentInfo GetArgumentInfoOfChild(int index) const override {
        if (index < 0 || index >= static_cast<int>(ArgumentInfo.size()))
            return CSharpArgumentInfo();
        return ArgumentInfo[static_cast<std::size_t>(index)];
    }
    int ChildCount() const override {
        return static_cast<int>(Arguments.size());
    }
    ILInstruction* GetChild(int i) const override {
        return (i >= 0 && i < static_cast<int>(Arguments.size()))
                   ? Arguments[static_cast<std::size_t>(i)].get()
                   : nullptr;
    }
    void AddArgument(std::unique_ptr<ILInstruction> a) {
        if (a) {
            a->Parent = this;
            a->ChildIndex = static_cast<int>(Arguments.size());
        }
        Arguments.push_back(std::move(a));
    }
    void RenumberChildren() {
        for (std::size_t i = 0; i < Arguments.size(); i++) {
            if (Arguments[i]) {
                Arguments[i]->Parent = this;
                Arguments[i]->ChildIndex = static_cast<int>(i);
            }
        }
    }
    // The C# WriteArgumentList over Arguments.Zip(ArgumentInfo).
    void WriteArguments(std::string& out) const;

protected:
    std::unique_ptr<ILInstruction> SetChildRaw(
        int i, std::unique_ptr<ILInstruction> n) override;
};

// Port of DynamicGetIndexInstruction (Instructions.cs lines 6037+):
// `dynamic.getindex (args)`; the result is dynamic (O).
class DynamicGetIndexInstruction : public DynamicArgumentsInstruction {
public:
    DynamicGetIndexInstruction(
        CSharpBinderFlags binderFlags, TypeSystem::ITypePtr context,
        std::vector<CSharpArgumentInfo> argumentInfo,
        std::vector<std::unique_ptr<ILInstruction>> arguments)
        : DynamicArgumentsInstruction(OpCode::DynamicGetIndexInstruction,
                                      binderFlags, std::move(context),
                                      std::move(argumentInfo),
                                      std::move(arguments)) {}
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override;
};

// Port of DynamicSetIndexInstruction (Instructions.cs lines 6108+):
// `dynamic.setindex (args)`; the result is dynamic (O).
class DynamicSetIndexInstruction : public DynamicArgumentsInstruction {
public:
    DynamicSetIndexInstruction(
        CSharpBinderFlags binderFlags, TypeSystem::ITypePtr context,
        std::vector<CSharpArgumentInfo> argumentInfo,
        std::vector<std::unique_ptr<ILInstruction>> arguments)
        : DynamicArgumentsInstruction(OpCode::DynamicSetIndexInstruction,
                                      binderFlags, std::move(context),
                                      std::move(argumentInfo),
                                      std::move(arguments)) {}
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override;
};

// Port of DynamicInvokeInstruction (Instructions.cs lines 6321+):
// `dynamic.invoke (args)`; the result is dynamic (O).
class DynamicInvokeInstruction : public DynamicArgumentsInstruction {
public:
    DynamicInvokeInstruction(
        CSharpBinderFlags binderFlags, TypeSystem::ITypePtr context,
        std::vector<CSharpArgumentInfo> argumentInfo,
        std::vector<std::unique_ptr<ILInstruction>> arguments)
        : DynamicArgumentsInstruction(OpCode::DynamicInvokeInstruction,
                                      binderFlags, std::move(context),
                                      std::move(argumentInfo),
                                      std::move(arguments)) {}
    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override;
};

// Port of DynamicInvokeMemberInstruction (Instructions.cs lines 6179+):
// `dynamic.invokemember Name<TypeArgs>(args)`; the result is dynamic (O).
class DynamicInvokeMemberInstruction : public DynamicArgumentsInstruction {
public:
    std::string Name;
    std::vector<TypeSystem::ITypePtr> TypeArguments;

    DynamicInvokeMemberInstruction(
        CSharpBinderFlags binderFlags, std::string name,
        std::vector<TypeSystem::ITypePtr> typeArguments,
        TypeSystem::ITypePtr context,
        std::vector<CSharpArgumentInfo> argumentInfo,
        std::vector<std::unique_ptr<ILInstruction>> arguments)
        : DynamicArgumentsInstruction(OpCode::DynamicInvokeMemberInstruction,
                                      binderFlags, std::move(context),
                                      std::move(argumentInfo),
                                      std::move(arguments)),
          Name(std::move(name)), TypeArguments(std::move(typeArguments)) {}

    StackType ResultType() const override { return StackType::O; }
    void WriteTo(std::string& out) const override;
};

// Port of DynamicInvokeConstructorInstruction (Instructions.cs lines
// 6250+): `dynamic.invokeconstructor Type.ctor(args)`; the result is the
// constructed type's stack type.
class DynamicInvokeConstructorInstruction : public DynamicArgumentsInstruction {
public:
    TypeSystem::ITypePtr ConstructedType;

    DynamicInvokeConstructorInstruction(
        CSharpBinderFlags binderFlags, TypeSystem::ITypePtr type,
        TypeSystem::ITypePtr context,
        std::vector<CSharpArgumentInfo> argumentInfo,
        std::vector<std::unique_ptr<ILInstruction>> arguments)
        : DynamicArgumentsInstruction(
              OpCode::DynamicInvokeConstructorInstruction, binderFlags,
              std::move(context), std::move(argumentInfo),
              std::move(arguments)),
          ConstructedType(std::move(type)) {}

    StackType ResultType() const override {
        return ConstructedType != nullptr
                   ? StackTypeOf(ConstructedType)
                   : StackType::Unknown;
    }
    void WriteTo(std::string& out) const override;
};

} // namespace ILSpy::Decompiler::IL
