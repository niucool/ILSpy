// Tests for the ILVariable use-site instruction lists (the C#
// LoadInstructions/StoreInstructions/AddressInstructions, filled by the
// ComputeVariableUsage walk -- the port's recompute convention for the
// C# Connected/Disconnected-maintained lists) and the
// IntroduceNativeIntTypeOnLocals transform they enable.

#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Transforms/IntroduceNativeIntTypeOnLocals.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/IntroduceNativeIntTypeOnLocals.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <utility>

namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace {

using namespace ILSpy::Decompiler::IL;

// Build `stloc v(binop.or(nint, ldloc v, ldc.i4 1)); ldloca v` -- a local
// with one load, one store, one address use.
struct UsageFixture {
    std::unique_ptr<ILFunction> fn;
    std::shared_ptr<ILVariable> v;
    Block* entry = nullptr;

    UsageFixture(TS::ITypePtr varType)
    {
        fn = std::make_unique<ILFunction>();
        auto container = std::make_unique<BlockContainer>();
        auto entryBlock = std::make_unique<Block>();
        entry = entryBlock.get();
        container->AddBlock(std::move(entryBlock));
        fn->Body = std::move(container);
        fn->Body->Parent = fn.get();
        fn->Body->ChildIndex = 0;
        v = std::make_shared<ILVariable>(VariableKind::Local, std::move(varType), 0);
        v->Name = "v";
        fn->Variables.push_back(v);
    }
};

TEST(VariableUsageLists, WalkFillsLoadStoreAddressLists) {
    auto intPtr = std::make_shared<TS::KnownType>(TS::KnownTypeCode::IntPtr);
    UsageFixture fx(intPtr);

    // The store: stloc v(binary.or(nint, ldloc v, ldc.i4 1)).
    auto binop = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(fx.v), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::BitOr, StackType::I);
    auto stloc = std::make_unique<StLoc>(fx.v, std::move(binop));
    StLoc* stlocPtr = stloc.get();
    LdLoc* loadPtr = static_cast<LdLoc*>(
        stloc->Value->GetChild(0));
    auto ldloca = std::make_unique<LdLoca>(fx.v);
    LdLoca* ldlocaPtr = ldloca.get();
    fx.entry->Add(std::move(stloc));
    fx.entry->Add(std::move(ldloca));
    fx.entry->SetFinal(std::make_unique<Branch>());

    ComputeVariableUsage(*fx.fn);

    ASSERT_EQ(fx.v->LoadCount, 1);
    ASSERT_EQ(fx.v->LoadInstructions.size(), 1u);
    EXPECT_EQ(fx.v->LoadInstructions[0], loadPtr);
    ASSERT_EQ(fx.v->StoreInstructions.size(), 1u);
    EXPECT_EQ(fx.v->StoreInstructions[0], stlocPtr);
    ASSERT_EQ(fx.v->AddressInstructions.size(), 1u);
    EXPECT_EQ(fx.v->AddressInstructions[0], ldlocaPtr);
}

// The lists follow a mutation: after the store is detached and re-added,
// a recompute refreshes them.
TEST(VariableUsageLists, RecomputeRefreshesAfterMutation) {
    UsageFixture fx(nullptr);
    auto ld = std::make_unique<LdLoc>(fx.v);
    LdLoc* ldPtr = ld.get();
    auto st = std::make_unique<StLoc>(fx.v, std::move(ld));
    fx.entry->Add(std::move(st));
    fx.entry->SetFinal(std::make_unique<Branch>());
    ComputeVariableUsage(*fx.fn);
    ASSERT_EQ(fx.v->LoadCount, 1);

    // Detach the store (the load leaves the tree with it) and recompute.
    std::unique_ptr<ILInstruction> removed = fx.entry->TakeChild(0);
    ComputeVariableUsage(*fx.fn);
    EXPECT_EQ(fx.v->LoadCount, 0);
    EXPECT_TRUE(fx.v->LoadInstructions.empty());

    // Re-attach and recompute: the load is back.
    fx.entry->Add(std::move(removed));
    ComputeVariableUsage(*fx.fn);
    EXPECT_EQ(fx.v->LoadCount, 1);
    EXPECT_EQ(fx.v->LoadInstructions[0], ldPtr);
    (void)ldPtr;
}

// The native-integer introduction: a Local typed System.IntPtr whose store
// value is a BinaryNumericInstruction over I retypes to nint (and the same
// index's other locals follow).
TEST(IntroduceNativeIntTypeOnLocals, RetypesNativeIntShapedLocals) {
    auto intPtr = std::make_shared<TS::KnownType>(TS::KnownTypeCode::IntPtr);
    UsageFixture fx(intPtr);
    auto binop = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(fx.v), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Add, StackType::I);
    auto stloc = std::make_unique<StLoc>(fx.v, std::move(binop));
    fx.entry->Add(std::move(stloc));
    fx.entry->SetFinal(std::make_unique<Branch>());

    ILTransformContext ctx;
    ctx.Settings.NativeIntegers = true;
    IntroduceNativeIntTypeOnLocals().Run(*fx.fn, ctx);

    ASSERT_NE(fx.v->Type, nullptr);
    EXPECT_EQ(fx.v->Type->Kind(), TS::TypeKind::NInt);
}

// A plain IntPtr local without a native-integer use keeps its type.
TEST(IntroduceNativeIntTypeOnLocals, KeepsPlainIntPtrLocals) {
    auto intPtr = std::make_shared<TS::KnownType>(TS::KnownTypeCode::IntPtr);
    UsageFixture fx(intPtr);
    // The store's value is a plain ldc.i4 (a stack-typed I4 load, not an
    // I-typed binop): not a native-int assignment shape.
    fx.entry->Add(std::make_unique<StLoc>(fx.v,
                                          std::make_unique<LdcI4>(42)));
    fx.entry->SetFinal(std::make_unique<Branch>());
    ILTransformContext ctx;
    IntroduceNativeIntTypeOnLocals().Run(*fx.fn, ctx);
    ASSERT_NE(fx.v->Type, nullptr);
    EXPECT_EQ(fx.v->Type->Kind(), TS::TypeKind::Struct)
        << "the IntPtr KnownType kind (a struct) is unchanged";
}

// The NativeIntegers setting off leaves everything alone.
TEST(IntroduceNativeIntTypeOnLocals, HonorsTheNativeIntegersGate) {
    auto intPtr = std::make_shared<TS::KnownType>(TS::KnownTypeCode::IntPtr);
    UsageFixture fx(intPtr);
    auto binop = std::make_unique<BinaryNumericInstruction>(
        std::make_unique<LdLoc>(fx.v), std::make_unique<LdcI4>(1),
        BinaryNumericOperator::Add, StackType::I);
    fx.entry->Add(std::make_unique<StLoc>(fx.v, std::move(binop)));
    fx.entry->SetFinal(std::make_unique<Branch>());
    ILTransformContext ctx;
    ctx.Settings.NativeIntegers = false;
    IntroduceNativeIntTypeOnLocals().Run(*fx.fn, ctx);
    ASSERT_NE(fx.v->Type, nullptr);
    EXPECT_EQ(fx.v->Type->Kind(), TS::TypeKind::Struct);
}

} // namespace

