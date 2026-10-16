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

// Tests for the AddressOf ILAst node -- the managed-reference wrapper the C#
// `AddressOf` node (ICSharpCode.Decompiler/IL/Instructions.cs) models. The
// tests pin the node invariant (the single Value child slot + the IType type
// operand), the Ref result type, the DirectFlags=None / flags-union behavior,
// the dump format, the deep-clone sharing, and the polymorphic dispatch. The
// node is a tested-but-not-yet-wired foundation (like MatchInstruction): the
// CallBuilder span-based string-concat shape and the
// VisitUserDefinedCompoundAssign span arm are the consumers that follow.

#include "Decompiler/IL/Instructions/AddressOf.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <type_traits>

namespace ILSpy::Tests {

namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;

namespace {

using namespace ::ILSpy::Decompiler;
namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

struct AddressOfFixture {
    TS::SimpleCompilation compilation;
    AddressOfFixture() : compilation(Impl::MinimalCorlib::Instance(), {}) {}

    TS::ITypePtr TypePtr(TS::KnownTypeCode code)
    {
        return std::const_pointer_cast<TS::IType>(
            compilation.FindType(code).shared_from_this());
    }
};

static_assert(std::is_base_of_v<IL::ILInstruction, IL::AddressOf>,
    "AddressOf is an ILInstruction");
static_assert(std::is_final_v<IL::AddressOf>,
    "the C# AddressOf is a sealed class");

} // namespace

TEST(AddressOf, CtorStoresValueAndTypeAndWiresTheChildSlot)
{
    AddressOfFixture fixture;
    auto value = std::make_unique<IL::LdStr>("hi");
    auto* valuePtr = value.get();
    auto stringType = fixture.TypePtr(TS::KnownTypeCode::String);
    IL::AddressOf node(std::move(value), stringType);

    EXPECT_EQ(node.Value.get(), valuePtr);
    EXPECT_EQ(node.Type.get(), stringType.get());
    EXPECT_EQ(node.Op, IL::OpCode::AddressOf);
    ASSERT_TRUE(node.Value != nullptr);
    EXPECT_EQ(node.Value->Parent, &node);
    EXPECT_EQ(node.Value->ChildIndex, 0);
}

TEST(AddressOf, ResultTypeIsRef)
{
    AddressOfFixture fixture;
    IL::AddressOf node(std::make_unique<IL::LdStr>("hi"),
                       fixture.TypePtr(TS::KnownTypeCode::String));
    EXPECT_EQ(node.ResultType(), IL::StackType::Ref);
}

TEST(AddressOf, DirectFlagsAreNone)
{
    AddressOfFixture fixture;
    IL::AddressOf node(std::make_unique<IL::LdStr>("hi"),
                       fixture.TypePtr(TS::KnownTypeCode::String));
    EXPECT_EQ(node.DirectFlags(), IL::InstructionFlags::None);
}

TEST(AddressOf, FlagsUnionTheValueFlags)
{
    AddressOfFixture fixture;
    // LdObj's DirectFlags are SideEffect | MayThrow; the base Flags() unions
    // them over the single Value child (the C# ComputeFlags returns
    // value.Flags -- the same union the port's default computes).
    auto ldObj = std::make_unique<IL::LdObj>(
        std::make_unique<IL::LdStr>("hi"), fixture.TypePtr(TS::KnownTypeCode::String));
    IL::AddressOf node(std::move(ldObj), fixture.TypePtr(TS::KnownTypeCode::String));
    EXPECT_EQ(node.Flags(), IL::InstructionFlags::SideEffect | IL::InstructionFlags::MayThrow);
}

TEST(AddressOf, ChildCountAndSlotShape)
{
    AddressOfFixture fixture;
    IL::AddressOf node(std::make_unique<IL::LdStr>("hi"),
                       fixture.TypePtr(TS::KnownTypeCode::String));
    EXPECT_EQ(node.ChildCount(), 1);
    IL::ILInstruction* asBase = &node;
    EXPECT_EQ(asBase->GetChild(0), node.Value.get());
    EXPECT_EQ(asBase->GetChild(1), nullptr);
}

TEST(AddressOf, TakeChildOrphansAndSetChildRewiresTheReplacement)
{
    AddressOfFixture fixture;
    IL::AddressOf node(std::make_unique<IL::LdStr>("old"),
                       fixture.TypePtr(TS::KnownTypeCode::String));
    std::unique_ptr<IL::ILInstruction> old = node.TakeChild(0);
    ASSERT_TRUE(old != nullptr);
    EXPECT_EQ(old->Parent, nullptr);
    EXPECT_EQ(old->ChildIndex, -1);
    EXPECT_EQ(node.ChildCount(), 0);

    auto replacement = std::make_unique<IL::LdStr>("new");
    auto* replacementPtr = replacement.get();
    node.SetChild(0, std::move(replacement));

    EXPECT_EQ(node.Value.get(), replacementPtr);
    EXPECT_EQ(node.Value->Parent, &node);
    EXPECT_EQ(node.Value->ChildIndex, 0);
}

TEST(AddressOf, WriteToRendersTheTypeAndTheValue)
{
    AddressOfFixture fixture;
    IL::AddressOf node(std::make_unique<IL::LdStr>("hi"),
                       fixture.TypePtr(TS::KnownTypeCode::String));
    std::string out;
    node.WriteTo(out);
    EXPECT_EQ(out, "addressof(System.String, ldstr \"hi\")");
}

TEST(AddressOf, CloneDeepClonesTheValueSharesTheTypeAndStaysDisconnected)
{
    AddressOfFixture fixture;
    IL::AddressOf node(std::make_unique<IL::LdStr>("hi"),
                       fixture.TypePtr(TS::KnownTypeCode::String));
    std::unique_ptr<IL::ILInstruction> cloned = node.Clone();
    auto* clone = dynamic_cast<IL::AddressOf*>(cloned.get());
    ASSERT_TRUE(clone != nullptr);

    EXPECT_NE(clone, &node);
    EXPECT_NE(clone->Value.get(), node.Value.get());
    EXPECT_EQ(clone->Parent, nullptr);
    EXPECT_EQ(clone->ChildIndex, -1);
    ASSERT_TRUE(clone->Value != nullptr);
    EXPECT_EQ(clone->Value->Parent, clone);
    EXPECT_EQ(clone->Type.get(), node.Type.get());
    std::string dump;
    clone->WriteTo(dump);
    EXPECT_EQ(dump, "addressof(System.String, ldstr \"hi\")");
}

TEST(AddressOf, PolymorphicDispatchThroughTheILInstructionBase)
{
    AddressOfFixture fixture;
    auto node = std::make_unique<IL::AddressOf>(
        std::make_unique<IL::LdStr>("hi"), fixture.TypePtr(TS::KnownTypeCode::String));
    IL::ILInstruction* asBase = node.get();

    EXPECT_EQ(asBase->Op, IL::OpCode::AddressOf);
    auto* roundTrip = dynamic_cast<IL::AddressOf*>(asBase);
    ASSERT_TRUE(roundTrip != nullptr);
    EXPECT_EQ(roundTrip, node.get());
}

} // namespace ILSpy::Tests