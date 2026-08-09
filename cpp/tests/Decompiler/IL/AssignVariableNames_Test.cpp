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
// ...), disambiguated against parameter names and earlier locals (num, num_1,
// num_2). Parameters keep their metadata names.

#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/AssignVariableNames.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

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
    EXPECT_EQ(fn->Variables[1]->Name, "num_1");
    EXPECT_EQ(fn->Variables[2]->Name, "num_2");
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
    EXPECT_EQ(fn->Variables[1]->Name, "num_1") << "local avoids the parameter name";
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
