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

// AssignVariableNames tests (minimal subset). Locals and stack slots get a
// name inferred from their type (System.Int32 -> num, System.String -> text,
// ...), disambiguated against parameter names and earlier locals (num, num2,
// num3 -- the C# conflict suffix). Parameters keep their metadata names.

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using namespace ILSpy::Decompiler::IL;
using ILSpy::Decompiler::Metadata::MetadataFile;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

namespace {

ILVariablePtr MakeLocal(std::string name, KnownTypeCode code) {
    auto v = std::make_shared<ILVariable>(VariableKind::Local,
        std::make_shared<KnownType>(code), -1);
    v->Name = std::move(name);
    return v;
}

ILVariablePtr MakeParam(std::string name) {
    auto v = std::make_shared<ILVariable>(VariableKind::Parameter, nullptr, 0);
    v->Name = std::move(name);
    return v;
}

ILTransformContext& Ctx() {
    static ILTransformContext ctx;
    return ctx;
}

} // namespace

TEST(AssignVariableNames, InfersNameFromType) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(MakeLocal("V_0", KnownTypeCode::Int32));
    fn->Variables.push_back(MakeLocal("V_1", KnownTypeCode::String));
    fn->Variables.push_back(MakeLocal("V_2", KnownTypeCode::Boolean));
    // An array type is named "array", not the lowercased element type with
    // brackets (which would yield "byte[]").
    auto arrLocal = std::make_shared<ILVariable>(VariableKind::Local,
        std::make_shared<ArrayType>(std::make_shared<KnownType>(KnownTypeCode::Byte)), -1);
    arrLocal->Name = "V_3";
    fn->Variables.push_back(arrLocal);
    AssignVariableNames().Run(*fn, Ctx());
    EXPECT_EQ(fn->Variables[0]->Name, "num");
    EXPECT_EQ(fn->Variables[1]->Name, "text");
    EXPECT_EQ(fn->Variables[2]->Name, "flag");
    EXPECT_EQ(fn->Variables[3]->Name, "array");
}

TEST(AssignVariableNames, DisambiguatesCollisions) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(MakeLocal("V_0", KnownTypeCode::Int32));
    fn->Variables.push_back(MakeLocal("V_1", KnownTypeCode::Int32));
    fn->Variables.push_back(MakeLocal("V_2", KnownTypeCode::Int32));
    AssignVariableNames().Run(*fn, Ctx());
    EXPECT_EQ(fn->Variables[0]->Name, "num");
    EXPECT_EQ(fn->Variables[1]->Name, "num2");
    EXPECT_EQ(fn->Variables[2]->Name, "num3");
}

TEST(AssignVariableNames, AvoidsParameterNameCollisions) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(MakeParam("num"));  // a parameter named "num"
    fn->Variables.push_back(MakeLocal("V_0", KnownTypeCode::Int32));  // would be "num"
    AssignVariableNames().Run(*fn, Ctx());
    EXPECT_EQ(fn->Variables[0]->Name, "num") << "parameter keeps its name";
    EXPECT_EQ(fn->Variables[1]->Name, "num2") << "local avoids the parameter name";
}

TEST(AssignVariableNames, KeepsParameterNames) {
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    fn->Variables.push_back(MakeParam("value"));
    fn->Variables.push_back(MakeLocal("V_0", KnownTypeCode::Int32));
    AssignVariableNames().Run(*fn, Ctx());
    EXPECT_EQ(fn->Variables[0]->Name, "value") << "parameter untouched";
    EXPECT_EQ(fn->Variables[1]->Name, "num");
}

TEST(AssignVariableNames, MscorlibSweepRenamesSomeLocals) {
#if defined(_WIN32)
    const char* path = "C:\\Windows\\Microsoft.NET\\Framework64\\v4.0.30319\\mscorlib.dll";
#else
    const char* path = "/usr/lib/mono/4.5/mscorlib.dll";
#endif
    if (!std::filesystem::exists(path)) GTEST_SKIP() << "fixture not present";
    MetadataFile f(path);
    ASSERT_TRUE(f.IsValid());

    int renamed = 0;
    int processed = 0;
    for (const auto& m : f.MethodDefs()) {
        if (m.RVA == 0) continue;
        auto fn = ReadIL(f, m.Token, m.RVA);
        if (!fn) continue;
        AssignVariableNames().Run(*fn, Ctx());
        for (auto& v : fn->Variables) {
            if (v && v->Kind == VariableKind::Local && v->Name.rfind("V_", 0) != 0)
                ++renamed;
        }
        ++processed;
        if (processed >= 3000) break;
    }
    EXPECT_GT(processed, 2000);
    EXPECT_GT(renamed, 0) << "some local should pick up a type-based name";
}

TEST(AssignVariableNames, GenericTypeTakesBaseNameNotTypeArg) {
    // A List<T>/Dictionary<...> local must be named from its base generic type
    // ("list"), not from a mangled last-segment substring of the type arguments
    // ("string>", "exceptionDispatchInfo>"). The reflection name is
    // `System.Collections.Generic.List`1<System.String>`; cutting the `<...>`
    // type-argument list before taking the last segment yields the base name.
    using namespace ILSpy::Decompiler::TypeSystem;
    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto listOfString = std::make_shared<ParameterizedType>(
        std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic.List`1")),
        std::vector<ITypePtr>{ std::make_shared<KnownType>(KnownTypeCode::String) });
    auto v = std::make_shared<ILVariable>(VariableKind::Local, listOfString, -1);
    v->Name = "V_0";
    fn->Variables.push_back(v);
    AssignVariableNames().Run(*fn, Ctx());
    EXPECT_EQ(fn->Variables[0]->Name, "list");
}

TEST(AssignVariableNames, RejectsProposalThatShadowsInheritedMemberName) {
    // The C# currentLowerCaseTypeOrMemberNames filter (AssignVariableNames.cs
    // VariableScope ctor): a naming proposal matching a lower-case MEMBER name
    // of the declaring type is rejected -- the local must not shadow the member
    // (an `allTypeDefs` local over the base field would force `base.` on every
    // later access). The store proposal for `stloc V = ldfld this->allTypeDefs`
    // suggests the field name; the filter rejects it and the type fallback
    // names the array local "array".
    using namespace ILSpy::Decompiler::TypeSystem;
    using namespace ILSpy::Decompiler::TypeSystem::TestSupport;
    LookupCompilation compilation;
    auto baseType = std::make_shared<LookupTypeDefinition>(
        std::string("Base"), std::string("Test"),
        FullTypeName(TopLevelTypeName(std::string("Test"), std::string("Base"))),
        TypeKind::Class, Accessibility::Public, compilation, nullptr);
    auto arrayType = std::make_shared<ArrayType>(
        std::make_shared<KnownType>(KnownTypeCode::Byte));
    auto field = std::make_shared<LookupField>(
        std::string("allTypeDefs"), arrayType, compilation);
    baseType->SetFields({ field.get() });
    auto derivedType = std::make_shared<LookupTypeDefinition>(
        std::string("Derived"), std::string("Test"),
        FullTypeName(TopLevelTypeName(std::string("Test"), std::string("Derived"))),
        TypeKind::Class, Accessibility::Public, compilation, nullptr);
    derivedType->AddDirectBaseType(baseType);

    auto fn = std::make_unique<ILFunction>();
    fn->Body = std::make_unique<BlockContainer>();
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto block = std::make_unique<Block>();
    Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));

    // The `this` parameter (the reader's negative-index convention).
    auto thisVar = std::make_shared<ILVariable>(VariableKind::Parameter,
                                                baseType, -1);
    thisVar->Name = "this";
    fn->Variables.push_back(thisVar);
    // The local stored from the field load.
    auto local = std::make_shared<ILVariable>(VariableKind::Local,
                                             arrayType, -1);
    local->Name = "V_0";
    fn->Variables.push_back(local);

    // stloc V_0(ldobj(ldflda(ldloc this, "Test.Base::allTypeDefs"))).
    auto ldflda = std::make_unique<LdFlda>(
        std::make_unique<LdLoc>(thisVar), std::string("Test.Base::allTypeDefs"));
    auto load = std::make_unique<LdObj>(std::move(ldflda), arrayType);
    auto store = std::make_unique<StLoc>(local, std::move(load));
    local->StoreInstructions.push_back(store.get());
    blockPtr->Add(std::move(store));

    ILTransformContext ctx;
    ctx.CurrentTypeDefinition = derivedType.get();
    AssignVariableNames().Run(*fn, ctx);
    // The proposal "allTypeDefs" is a member name of the declaring type's
    // base chain: rejected. The type fallback names the array "array".
    EXPECT_EQ(local->Name, "array")
        << "a proposal matching a member name must not shadow the member";
}
