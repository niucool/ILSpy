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

// The dynamic-call node family tests (the ThreeValuedBoolInstructions
// tested-but-not-yet-wired foundation precedent): the node invariants (the
// child slots, the ResultType table, the DirectFlags), the dump mnemonics
// (the binder-flag suffixes and the argument-info prefixes), the deep clone
// (the metadata and the operand trees), and the SetChild re-parenting.

#include "Decompiler/IL/Instructions/DynamicInstructions.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using namespace ILSpy::Decompiler::IL;
namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

std::unique_ptr<LdLoc> MakeLdLoc(const char* name) {
    auto v = std::make_shared<ILVariable>();
    v->Name = name;
    v->Kind = VariableKind::Local;
    v->Type = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    return std::make_unique<LdLoc>(std::move(v));
}

} // namespace

// The fixed-operand nodes carry their operands in typed slots, adopt them
// on construction, and re-parent through SetChild.
TEST(DynamicInstructions, BinaryOperatorNodeInvariantAndDump) {
    auto left = MakeLdLoc("a");
    auto right = MakeLdLoc("b");
    ILInstruction* leftPtr = left.get();
    DynamicBinaryOperatorInstruction inst(
        CSharpBinderFlags::CheckedContext, ExpressionType::Add,
        nullptr, CSharpArgumentInfo{.Name = "", .Flags = CSharpArgumentInfoFlags::UseCompileTimeType},
        std::move(left),
        CSharpArgumentInfo{.Name = "rhs", .Flags = CSharpArgumentInfoFlags::NamedArgument},
        std::move(right));
    EXPECT_EQ(inst.Op, OpCode::DynamicBinaryOperatorInstruction);
    EXPECT_EQ(inst.ChildCount(), 2);
    EXPECT_EQ(inst.GetChild(0), leftPtr);
    EXPECT_EQ(leftPtr->Parent, &inst);
    EXPECT_EQ(leftPtr->ChildIndex, 0);
    // The C# ComputeFlags: MayThrow | SideEffect (plus the children's).
    EXPECT_TRUE(HasFlag(inst.Flags(), InstructionFlags::MayThrow));
    EXPECT_TRUE(HasFlag(inst.Flags(), InstructionFlags::SideEffect));
    // The result of a dynamic binary operation is dynamic (O).
    EXPECT_EQ(inst.ResultType(), StackType::O);
    // The dump: the mnemonic, the binder-flag suffix, the operation name,
    // and the per-argument info prefixes.
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.binary.operator"), std::string::npos)
        << out;
    EXPECT_NE(out.find(".checked"), std::string::npos) << out;
    EXPECT_NE(out.find("Add"), std::string::npos) << out;
    EXPECT_NE(out.find("UseCompileTimeType"), std::string::npos) << out;
    EXPECT_NE(out.find("name: rhs"), std::string::npos) << out;
}

// The IsTrue / IsFalse operations result in bool (I4), everything else in
// dynamic (O).
TEST(DynamicInstructions, BinaryOperatorResultTypeTable) {
    auto operand = MakeLdLoc("a");
    DynamicBinaryOperatorInstruction isTrue(
        CSharpBinderFlags::None, ExpressionType::IsTrue, nullptr,
        CSharpArgumentInfo{}, std::move(operand), CSharpArgumentInfo{},
        nullptr);
    EXPECT_EQ(isTrue.ResultType(), StackType::I4);
    auto left2 = MakeLdLoc("a");
    DynamicBinaryOperatorInstruction add(
        CSharpBinderFlags::None, ExpressionType::Add, nullptr,
        CSharpArgumentInfo{}, std::move(left2), CSharpArgumentInfo{},
        nullptr);
    EXPECT_EQ(add.ResultType(), StackType::O);
}

// The unary operator node: a single operand slot.
TEST(DynamicInstructions, UnaryOperatorNodeInvariantAndDump) {
    auto operand = MakeLdLoc("d");
    ILInstruction* operandPtr = operand.get();
    DynamicUnaryOperatorInstruction inst(
        CSharpBinderFlags::None, ExpressionType::Negate, nullptr,
        CSharpArgumentInfo{.Name = "", .Flags = CSharpArgumentInfoFlags::Constant},
        std::move(operand));
    EXPECT_EQ(inst.ChildCount(), 1);
    EXPECT_EQ(inst.GetChild(0), operandPtr);
    EXPECT_EQ(operandPtr->Parent, &inst);
    EXPECT_EQ(inst.ResultType(), StackType::O);
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.unary.operator"), std::string::npos) << out;
    EXPECT_NE(out.find("Negate"), std::string::npos) << out;
    EXPECT_NE(out.find("Constant"), std::string::npos) << out;
}

// The convert node: the result is the target type's stack type; the checked
// and explicit predicates read the binder flags.
TEST(DynamicInstructions, ConvertNodeInvariantAndDump) {
    auto argument = MakeLdLoc("d");
    DynamicConvertInstruction inst(
        CSharpBinderFlags::CheckedContext | CSharpBinderFlags::ConvertExplicit,
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32), nullptr,
        std::move(argument));
    EXPECT_EQ(inst.ResultType(), StackType::I4);
    EXPECT_TRUE(inst.IsChecked());
    EXPECT_TRUE(inst.IsExplicit());
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.convert"), std::string::npos) << out;
    EXPECT_NE(out.find(".checked"), std::string::npos) << out;
    EXPECT_NE(out.find(".explicit"), std::string::npos) << out;
    EXPECT_NE(out.find("System.Int32"), std::string::npos) << out;
}

// The get-member node: the name renders between the mnemonic and the
// argument list.
TEST(DynamicInstructions, GetMemberNodeInvariantAndDump) {
    auto target = MakeLdLoc("d");
    DynamicGetMemberInstruction inst(
        CSharpBinderFlags::InvokeSimpleName, "Foo", nullptr,
        CSharpArgumentInfo{.Name = "", .Flags = CSharpArgumentInfoFlags::UseCompileTimeType},
        std::move(target));
    EXPECT_EQ(inst.ResultType(), StackType::O);
    EXPECT_EQ(inst.GetArgumentInfoOfChild(0).Flags,
              CSharpArgumentInfoFlags::UseCompileTimeType);
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.getmember"), std::string::npos) << out;
    EXPECT_NE(out.find("Foo"), std::string::npos) << out;
    EXPECT_NE(out.find(".invokesimple"), std::string::npos) << out;
}

// The set-member node: the two-operand slot table.
TEST(DynamicInstructions, SetMemberNodeInvariantAndDump) {
    auto target = MakeLdLoc("d");
    auto value = MakeLdLoc("v");
    ILInstruction* valuePtr = value.get();
    DynamicSetMemberInstruction inst(
        CSharpBinderFlags::None, "Foo", nullptr, CSharpArgumentInfo{},
        std::move(target), CSharpArgumentInfo{}, std::move(value));
    EXPECT_EQ(inst.ChildCount(), 2);
    EXPECT_EQ(inst.GetChild(1), valuePtr);
    EXPECT_EQ(valuePtr->ChildIndex, 1);
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.setmember"), std::string::npos) << out;
    EXPECT_NE(out.find("Foo"), std::string::npos) << out;
}

// The is-event node: the result is bool (I4).
TEST(DynamicInstructions, IsEventNodeInvariantAndDump) {
    auto argument = MakeLdLoc("d");
    DynamicIsEventInstruction inst(
        CSharpBinderFlags::None, "Foo", nullptr, std::move(argument));
    EXPECT_EQ(inst.ResultType(), StackType::I4);
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.isevent"), std::string::npos) << out;
    EXPECT_NE(out.find("Foo"), std::string::npos) << out;
}

// The variable-argument nodes: the Arguments collection re-parents, the
// child index space is dense, and the argument-info lookup maps through.
TEST(DynamicInstructions, InvokeNodeArgumentsAndDump) {
    std::vector<std::unique_ptr<ILInstruction>> args;
    auto a0 = MakeLdLoc("d");
    auto a1 = MakeLdLoc("x");
    ILInstruction* a0Ptr = a0.get();
    args.push_back(std::move(a0));
    args.push_back(std::move(a1));
    std::vector<CSharpArgumentInfo> infos{
        CSharpArgumentInfo{.Name = "", .Flags = CSharpArgumentInfoFlags::IsStaticType},
        CSharpArgumentInfo{.Name = "p", .Flags = CSharpArgumentInfoFlags::NamedArgument}};
    DynamicInvokeInstruction inst(CSharpBinderFlags::None, nullptr, infos,
                                  std::move(args));
    EXPECT_EQ(inst.ChildCount(), 2);
    EXPECT_EQ(inst.GetChild(0), a0Ptr);
    EXPECT_EQ(a0Ptr->Parent, &inst);
    EXPECT_EQ(a0Ptr->ChildIndex, 0);
    EXPECT_EQ(inst.GetArgumentInfoOfChild(1).Name, "p");
    // Out-of-range lookups return the default info (the C# throws; this
    // port's consumers never pass an out-of-range index).
    EXPECT_EQ(inst.GetArgumentInfoOfChild(99).Flags,
              CSharpArgumentInfoFlags::None);
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.invoke"), std::string::npos) << out;
    EXPECT_NE(out.find("IsStaticType"), std::string::npos) << out;
    EXPECT_NE(out.find("NamedArgument"), std::string::npos) << out;
}

// The invoke-member node renders the name and the type arguments.
TEST(DynamicInstructions, InvokeMemberNodeDump) {
    std::vector<std::unique_ptr<ILInstruction>> args;
    args.push_back(MakeLdLoc("d"));
    DynamicInvokeMemberInstruction inst(
        CSharpBinderFlags::None, "Foo",
        std::vector<TS::ITypePtr>{
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32)},
        nullptr, std::vector<CSharpArgumentInfo>{CSharpArgumentInfo{}},
        std::move(args));
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.invokemember"), std::string::npos) << out;
    EXPECT_NE(out.find("Foo<"), std::string::npos) << out;
    EXPECT_NE(out.find("System.Int32"), std::string::npos) << out;
}

// The invoke-constructor node renders the constructed type and the .ctor
// suffix; the result is the type's stack type.
TEST(DynamicInstructions, InvokeConstructorNodeDump) {
    std::vector<std::unique_ptr<ILInstruction>> args;
    args.push_back(MakeLdLoc("d"));
    DynamicInvokeConstructorInstruction inst(
        CSharpBinderFlags::None,
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String), nullptr,
        std::vector<CSharpArgumentInfo>{CSharpArgumentInfo{}},
        std::move(args));
    EXPECT_EQ(inst.ResultType(), StackType::O);
    std::string out;
    inst.WriteTo(out);
    EXPECT_NE(out.find("dynamic.invokeconstructor"), std::string::npos) << out;
    EXPECT_NE(out.find("System.String.ctor"), std::string::npos) << out;
}

// The get/set-index nodes render the canonical get_Item / set_Item names.
TEST(DynamicInstructions, IndexNodeDumps) {
    std::vector<std::unique_ptr<ILInstruction>> args;
    args.push_back(MakeLdLoc("d"));
    DynamicGetIndexInstruction get(CSharpBinderFlags::None, nullptr,
                                   std::vector<CSharpArgumentInfo>{CSharpArgumentInfo{}},
                                   std::move(args));
    std::string out;
    get.WriteTo(out);
    EXPECT_NE(out.find("dynamic.getindex"), std::string::npos) << out;
    EXPECT_NE(out.find("get_Item"), std::string::npos) << out;

    std::vector<std::unique_ptr<ILInstruction>> setArgs;
    setArgs.push_back(MakeLdLoc("d"));
    setArgs.push_back(MakeLdLoc("v"));
    DynamicSetIndexInstruction set(CSharpBinderFlags::None, nullptr,
                                   std::vector<CSharpArgumentInfo>(2),
                                   std::move(setArgs));
    out.clear();
    set.WriteTo(out);
    EXPECT_NE(out.find("dynamic.setindex"), std::string::npos) << out;
    EXPECT_NE(out.find("set_Item"), std::string::npos) << out;
}

// The deep clone: the metadata, the binder flags, and the operand trees.
TEST(DynamicInstructions, DeepCloneCarriesMetadataAndOperands) {
    auto target = MakeLdLoc("d");
    DynamicGetMemberInstruction inst(
        CSharpBinderFlags::InvokeSimpleName, "Foo",
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String),
        CSharpArgumentInfo{.Name = "", .Flags = CSharpArgumentInfoFlags::Constant},
        std::move(target));
    inst.StartILOffset = 0x10;
    inst.EndILOffset = 0x20;
    auto clonePtr = inst.Clone();
    auto* clone = dynamic_cast<DynamicGetMemberInstruction*>(clonePtr.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->Name, "Foo");
    EXPECT_EQ(clone->BinderFlags, CSharpBinderFlags::InvokeSimpleName);
    EXPECT_EQ(clone->CallingContext->Name(), std::string("String"));
    EXPECT_EQ(clone->TargetArgumentInfo.Flags,
              CSharpArgumentInfoFlags::Constant);
    ASSERT_NE(clone->Target, nullptr);
    EXPECT_NE(clone->Target.get(), inst.Target.get()) << "a deep copy";
    EXPECT_EQ(clone->Target->Op, OpCode::LdLoc);
    EXPECT_EQ(clone->StartILOffset, 0x10u);
    EXPECT_EQ(clone->EndILOffset, 0x20u);
}

// The clone of a variable-argument node re-parents the collected operands.
TEST(DynamicInstructions, DeepCloneInvokeArguments) {
    std::vector<std::unique_ptr<ILInstruction>> args;
    args.push_back(MakeLdLoc("d"));
    args.push_back(MakeLdLoc("x"));
    DynamicInvokeInstruction inst(
        CSharpBinderFlags::None, nullptr,
        std::vector<CSharpArgumentInfo>{CSharpArgumentInfo{},
                                        CSharpArgumentInfo{.Name = "p"}},
        std::move(args));
    auto clonePtr = inst.Clone();
    auto* clone = dynamic_cast<DynamicInvokeInstruction*>(clonePtr.get());
    ASSERT_NE(clone, nullptr);
    ASSERT_EQ(clone->ChildCount(), 2);
    EXPECT_EQ(clone->GetArgumentInfoOfChild(1).Name, "p");
    EXPECT_EQ(clone->GetChild(0)->Parent, clone);
    EXPECT_EQ(clone->GetChild(1)->Parent, clone);
    EXPECT_EQ(clone->GetChild(1)->ChildIndex, 1);
}

// SetChild replaces a slot and re-parents the replacement.
TEST(DynamicInstructions, SetChildReparents) {
    auto target = MakeLdLoc("d");
    DynamicGetMemberInstruction inst(
        CSharpBinderFlags::None, "Foo", nullptr, CSharpArgumentInfo{},
        std::move(target));
    // Take the old child out (orphaned), then set the replacement: the
    // public SetChild destroys the previous occupant, so the take-then-set
    // pair is the observable-swap form.
    auto old = inst.TakeChild(0);
    ASSERT_NE(old, nullptr);
    EXPECT_EQ(old->Op, OpCode::LdLoc);
    EXPECT_EQ(old->Parent, nullptr);
    auto replacement = MakeLdLoc("e");
    ILInstruction* replacementPtr = replacement.get();
    inst.SetChild(0, std::move(replacement));
    EXPECT_EQ(inst.GetChild(0), replacementPtr);
    EXPECT_EQ(replacementPtr->Parent, &inst);
    EXPECT_EQ(replacementPtr->ChildIndex, 0);
}
