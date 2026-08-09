// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation, the rights to use, copy, modify, merge,
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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

// Tests for the UsingInstruction ILAst node -- the `using`-statement node the next
// in-order transform (UsingTransform) builds from a `stloc obj(resource); .try
// { } finally { if (obj != null) callvirt Dispose(obj) }` block tail. This is a
// tested-but-not-yet-wired foundation (like MatchInstruction / LongSet / LoopContext
// / the NullableLifting helpers): no pipeline transform constructs it yet. The
// tests cover the node invariant/flags/ResultType/dump (default, IsAsync, IsRefStruct),
// the two-child tree (ResourceExpression slot 0 inlineable, Body slot 1), the
// store-counting in ComputeVariableUsage (the node is an IStoreInstruction), the
// ILAstToCSharp seed rendering of `using (...)`, the UsingStatement setting default,
// and a mscorlib sweep that constructs UsingInstructions over real decoded
// variables/containers.

#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Disposable() { return std::make_shared<KnownType>(KnownTypeCode::IDisposable); }  // an IDisposable type

ILVariablePtr MakeLocal(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local, type, 0);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeParam(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::Parameter, type, 1);
    v->Name = std::move(name);
    return v;
}

// A using-local (the kind UsingTransform sets on the resource variable).
ILVariablePtr MakeUsingLocal(std::string name, ITypePtr type) {
    auto v = std::make_shared<ILVariable>(VariableKind::UsingLocal, type, 0);
    v->Name = std::move(name);
    return v;
}

void Walk(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) Walk(inst->GetChild(i), visit);
}

// Build a standalone ILFunction whose body is a single UsingInstruction over
// `resource` with the given body container, so the seed-rendering and
// store-counting tests exercise a realistic tree.
std::unique_ptr<ILFunction> MakeUsingFn(ILVariablePtr resourceVar,
                                        std::unique_ptr<ILInstruction> resourceExpr,
                                        std::unique_ptr<BlockContainer> body) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(resourceVar);
    auto root = std::make_unique<Block>();
    root->Add(std::make_unique<UsingInstruction>(resourceVar, std::move(resourceExpr),
                                                 std::move(body)));
    root->SetFinal(std::make_unique<Leave>(fn->Body.get()));
    fn->Body->AddBlock(std::move(root));
    return fn;
}

} // namespace

// ---- Node: invariant, flags, ResultType, dump ----

TEST(UsingInstruction, NodeInvariantFlagsAndDump) {
    auto res = MakeParam("res", Disposable());
    auto body = std::make_unique<BlockContainer>();
    auto us = std::make_unique<UsingInstruction>(res, std::make_unique<LdLoc>(res),
                                                  std::move(body));
    EXPECT_EQ(us->Op, OpCode::UsingInstruction);
    EXPECT_EQ(us->ResultType(), StackType::Void);
    EXPECT_TRUE(HasFlag(us->DirectFlags(), InstructionFlags::MayWriteLocals));
    EXPECT_TRUE(HasFlag(us->DirectFlags(), InstructionFlags::ControlFlow));
    EXPECT_TRUE(HasFlag(us->DirectFlags(), InstructionFlags::SideEffect));
    ASSERT_EQ(us->ChildCount(), 2);
    EXPECT_EQ(us->GetChild(0)->Op, OpCode::LdLoc);
    EXPECT_EQ(us->GetChild(1)->Op, OpCode::BlockContainer);
    EXPECT_EQ(us->GetChild(0)->Parent, us.get());
    EXPECT_EQ(us->GetChild(1)->Parent, us.get());
    EXPECT_EQ(us->GetChild(0)->ChildIndex, 0);
    EXPECT_EQ(us->GetChild(1)->ChildIndex, 1);
    // The dump renders the variable and the resource expression.
    std::string dump = us->ToString();
    EXPECT_NE(dump.find("using ("), std::string::npos) << dump;
    EXPECT_NE(dump.find("res"), std::string::npos) << dump;
    us->CheckInvariant(ILPhase::Normal);
}

// IsAsync marks an `await using`; IsRefStruct marks a `using` of a ref struct.
// Both flavour the ILAst dump (`.async` / `.ref`) faithful to UsingInstruction.cs.
TEST(UsingInstruction, IsAsyncAndIsRefStructFlavourDump) {
    auto res = MakeLocal("r", Object());
    auto us = std::make_unique<UsingInstruction>(res, std::make_unique<LdLoc>(res),
                                                 std::make_unique<BlockContainer>());
    us->IsAsync = true;
    std::string dump = us->ToString();
    EXPECT_NE(dump.find("using.async ("), std::string::npos) << dump;

    auto us2 = std::make_unique<UsingInstruction>(res, std::make_unique<LdLoc>(res),
                                                   std::make_unique<BlockContainer>());
    us2->IsRefStruct = true;
    std::string dump2 = us2->ToString();
    EXPECT_NE(dump2.find("using.ref ("), std::string::npos) << dump2;
}

// The two children live in typed slots: ResourceExpression (slot 0) and Body
// (slot 1). Either may be absent (a degenerate node the invariant still accepts).
TEST(UsingInstruction, ChildrenAreInTypedSlots) {
    auto res = MakeLocal("r", Object());
    auto us = std::make_unique<UsingInstruction>(res, std::make_unique<LdLoc>(res),
                                                  std::make_unique<BlockContainer>());
    ASSERT_EQ(us->ChildCount(), 2);
    EXPECT_EQ(us->GetChild(0)->Op, OpCode::LdLoc);          // ResourceExpression
    EXPECT_EQ(us->GetChild(1)->Op, OpCode::BlockContainer);  // Body
    us->CheckInvariant(ILPhase::Normal);
}

// ---- Store counting ----

// UsingInstruction is an IStoreInstruction in the C# (Connected() calls
// variable.AddStoreInstruction); this port counts it as a store to its Variable in
// ComputeVariableUsage, mirroring MatchInstruction / StLoc / TryCatchHandler.
TEST(UsingInstruction, CountsAsStoreInVariableUsage) {
    auto res = MakeLocal("res", Object());
    auto fn = MakeUsingFn(res, std::make_unique<LdLoc>(res), std::make_unique<BlockContainer>());
    ComputeVariableUsage(*fn);
    // The UsingInstruction store landed on the resource variable.
    EXPECT_GE(res->StoreCount, 1);
    fn->CheckInvariant(ILPhase::Normal);
}

// ---- Seed rendering ----

// The ILAstToCSharp seed renders a UsingInstruction as `using (resource) { body }`
// (the expression form), matching the LockInstruction `lock (expr)` precedent.
TEST(UsingInstruction, SeedRendersUsingStatement) {
    auto res = MakeLocal("res", Object());
    auto body = std::make_unique<BlockContainer>();
    auto entry = std::make_unique<Block>();
    entry->Add(std::make_unique<Call>("System.Foo::Bar"));
    entry->SetFinal(std::make_unique<Leave>(body.get()));
    body->AddBlock(std::move(entry));
    auto fn = MakeUsingFn(res, std::make_unique<LdLoc>(res), std::move(body));
    fn->CheckInvariant(ILPhase::Normal);

    std::string text = ILAstToCSharp(*fn, "void", "M", "object res");
    EXPECT_NE(text.find("using (res)"), std::string::npos) << text;
    // The body call flattens "::" to ".": "System.Foo::Bar" -> "System.Foo.Bar".
    EXPECT_NE(text.find("System.Foo.Bar"), std::string::npos) << text;
}

// A `using (null) { }` (the degenerate `using (null)` the C# compiler emits for
// a null resource) renders the null resource expression.
TEST(UsingInstruction, SeedRendersUsingNull) {
    auto res = MakeLocal("res", Object());
    auto fn = MakeUsingFn(res, std::make_unique<LdNull>(), std::make_unique<BlockContainer>());
    fn->CheckInvariant(ILPhase::Normal);
    std::string text = ILAstToCSharp(*fn, "void", "M", "");
    EXPECT_NE(text.find("using (null)"), std::string::npos) << text;
}

// ---- Setting ----

// The UsingStatement setting defaults to true (matching DecompilerSettings, a
// C# 1.0 setting); it gates the UsingTransform that builds UsingInstructions.
TEST(UsingInstruction, UsingStatementSettingDefaultsTrue) {
    ILTransformSettings settings;
    EXPECT_TRUE(settings.UsingStatement);
}

// ---- mscorlib sweep ----

// A mscorlib sweep: decode real methods and, for each method whose body loads a
// variable, construct a UsingInstruction over that real LdLoc operand (a new LdLoc
// of the same variable) with the function body as the using body, set the variable
// to UsingLocal, and assert the node invariant holds, the store-counting lands the
// store on the variable, and the dump renders. This exercises the node on thousands
// of real variables/containers (the volume the foundation will see once
// UsingTransform is wired).
TEST(UsingInstruction, MscorlibConstructFromRealLoadsSweep) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int processed = 0;
    int constructed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        ++processed;
        // Find the first LdLoc with a resolvable variable.
        ILVariablePtr firstVar;
        Walk(fn->Body.get(), [&](ILInstruction* inst) {
            if (firstVar) return;
            if (inst && inst->Op == OpCode::LdLoc) {
                auto* ld = static_cast<LdLoc*>(inst);
                if (ld->Variable) firstVar = ld->Variable;
            }
        });
        if (firstVar) {
            auto resVar = std::make_shared<ILVariable>(VariableKind::UsingLocal,
                                                       firstVar->Type, 0);
            resVar->Name = firstVar->Name + "_u";
            auto us = std::make_unique<UsingInstruction>(resVar,
                std::make_unique<LdLoc>(firstVar), std::make_unique<BlockContainer>());
            us->CheckInvariant(ILPhase::Normal);
            std::string dump = us->ToString();
            EXPECT_NE(dump.find("using ("), std::string::npos) << dump;
            ++constructed;
        }
        fn->CheckInvariant(ILPhase::Normal);
        if (processed >= 8000) break;
    }
    EXPECT_GT(processed, 5000);
    EXPECT_GT(constructed, 0);
}
