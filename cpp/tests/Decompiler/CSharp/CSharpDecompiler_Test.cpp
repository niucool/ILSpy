// Copyright (c) 2026 Jim Hester
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Tests for the CSharpDecompiler facade (the C# `public class
// CSharpDecompiler` port shell): the pipeline entry aliases the
// IL-namespace list, and DecompileFunctionToString runs the pipeline over
// a synthetic function and renders the method text.

#include "Decompiler/CSharp/CSharpDecompiler.hpp"

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "TestFixtures/ConnIdResFixtures.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/Util/CacheManager.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Metadata = ::ILSpy::Decompiler::Metadata;
using ::ILSpy::Decompiler::Metadata::MethodSignature;

// The connid_res compilation fixture (the MetadataModule_Test
// MainModuleCompilation pattern): a hand-rolled ICompilation whose main
// module is set after construction (the module ctor binds the compilation
// reference first).
class ConnIdCompilation : public TS::ICompilation {
public:
    ConnIdCompilation() = default;

    void SetMainModule(const TS::IModule* module) { mainModule_ = module; }

    // --- ICompilation ---
    const TS::IModule& MainModule() const override { return *mainModule_; }
    std::vector<const TS::IModule*> Modules() const override
    {
        return std::vector<const TS::IModule*>{ mainModule_ };
    }
    std::vector<const TS::IModule*> ReferencedModules() const override { return {}; }
    const TS::INamespace& RootNamespace() const override
    {
        return mainModule_->RootNamespace();
    }
    const TS::INamespace* GetNamespaceForExternAlias(const std::string&) const override
    {
        return nullptr;
    }
    const TS::IType& FindType(TS::KnownTypeCode) const override { return knownType_; }
    const TS::StringComparer& NameComparer() const override
    {
        return TS::StringComparer::Ordinal();
    }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override
    {
        return cacheManager_;
    }
    TS::TypeSystemOptions TypeSystemOptions() const override
    {
        return TS::TypeSystemOptions::Default;
    }

private:
    const TS::IModule* mainModule_ = nullptr;
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
    TS::KnownType knownType_{ TS::KnownTypeCode::Object };
};
namespace CSharp = ::ILSpy::Decompiler::CSharp;
using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

TEST(CSharpDecompilerTest, GetILTransformsAliasesTheILNamespaceFactory)
{
    auto transforms = CSharp::CSharpDecompiler::GetILTransforms();
    ASSERT_FALSE(transforms.empty());
    // The same list shape the IL-namespace factory produces (the head is
    // ControlFlowSimplification, per the C# GetILTransforms list).
    EXPECT_NE(dynamic_cast<IL::ControlFlowSimplification*>(
                  transforms[0].get()),
              nullptr);
}

// The per-body decompile half: a synthetic function whose body is
// `stloc v(ldc.i4 42)` runs through the pipeline and renders with the
// constant folded.
TEST(CSharpDecompilerTest, DecompileFunctionToStringRendersThePipelineOutput)
{
    auto fn = std::make_unique<IL::ILFunction>();
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    ILVariablePtr v = fn->RegisterVariable(IL::VariableKind::Local,
                                           intType, std::string("V_0"));
    auto body = std::make_unique<IL::BlockContainer>();
    fn->Body = std::move(body);
    // The Body's Parent wiring (the C# ILFunction ctor's child-slot
    // assignment; the port's hand-built trees wire it explicitly -- the
    // CachedDelegateInitialization fixture precedent).
    fn->Body->Parent = fn.get();
    fn->Body->ChildIndex = 0;
    auto block = std::make_unique<IL::Block>();
    block->Kind = IL::BlockKind::ControlFlow;
    IL::Block* blockPtr = block.get();
    fn->Body->AddBlock(std::move(block));
    {
        auto value = std::make_unique<IL::LdcI4>(42);
        auto store = std::make_unique<IL::StLoc>(v, std::move(value));
        blockPtr->Add(std::move(store));
    }
    blockPtr->SetFinal(std::make_unique<IL::LdLoc>(v));

    std::string text = CSharp::CSharpDecompiler::DecompileFunctionToString(
        *fn, "int", "M", "");
    EXPECT_FALSE(text.empty());
    EXPECT_NE(text.find("42"), std::string::npos)
        << "the rendered method text carries the constant";
}

// The signature-decl builder (the C# Decompile path's parameter-declaration
// half, extracted from the CLI's inline block): named parameters carry
// their name; unnamed parameters fall back to arg_<base + index> (base 1
// for an instance method -- `this` is the implicit arg_0; base 0 static).
TEST(CSharpDecompilerTest, MethodDeclStringFallsBackToArgNames)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    auto stringType =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::String);
    MethodSignature sig;
    sig.ReturnType = intType;
    sig.ParameterTypes = {intType, stringType};
    sig.IsInstance = true;
    // The first parameter carries a metadata name; the second does not.
    EXPECT_EQ(CSharp::CSharpDecompiler::MethodDeclString(
                  sig, std::vector<std::string>{"x"}),
              "int x, string arg_2");
}

// The static shape: the fallback base is 0 (no implicit this).
TEST(CSharpDecompilerTest, MethodDeclStringStaticBaseIsZero)
{
    auto intType = std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
    MethodSignature sig;
    sig.ReturnType = intType;
    sig.ParameterTypes = {intType, intType};
    sig.IsInstance = false;
    EXPECT_EQ(CSharp::CSharpDecompiler::MethodDeclString(
                  sig, std::vector<std::string>{}),
              "int arg_0, int arg_1");
}

// The full per-method entry over the in-repo corpus: the connid_res.dll
// fixture (a real csc-compiled library) decompiles at least one method
// body with its method name in the rendered text.
TEST(CSharpDecompilerTest, DecompileMethodRendersACorpusMethod)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    bool rendered = false;
    for (const auto& t : module.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        for (const auto& m : module.GetMethods(t.Token)) {
            if (m.RVA == 0) continue;
            std::string text;
            if (CSharp::CSharpDecompiler::DecompileMethodToString(
                    module, m.Token, m.RVA, m.Name, text)) {
                EXPECT_NE(text.find(m.Name), std::string::npos)
                    << "the rendered method carries its name";
                rendered = true;
                break;
            }
        }
        if (rendered) break;
    }
    EXPECT_TRUE(rendered) << "the corpus yields at least one decodable body";
}

// The type-level entry: the connid_res corpus type renders with the type
// header and at least one member body.
TEST(CSharpDecompilerTest, DecompileTypeRendersTheTypeAndMembers)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    for (const auto& t : module.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        std::string text;
        if (CSharp::CSharpDecompiler::DecompileTypeToString(module, t.Token,
                                                            text)) {
            EXPECT_NE(text.find(t.Name), std::string::npos)
                << "the rendered type carries its name";
            SUCCEED();
            return;
        }
    }
    FAIL() << "no type in the corpus decompiled";
}

// The PartialTypeInfo registry (the C# `partialTypes` dictionary): a
// registered declared member is skipped in the type's member iteration,
// and merging a second info with the same declaring type unionizes the
// member sets (the C# AddDeclaredMembers shape).
TEST(CSharpDecompilerTest, PartialTypeInfoSkipsDeclaredMembers)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    // Find the first type with a decodable method body; capture its method
    // set and the first body's text.
    for (const auto& t : module.TypeDefs()) {
        if (t.Name == "<Module>") continue;
        auto methods = module.GetMethods(t.Token);
        std::uint32_t firstBody = 0;
        std::string firstMethod;
        for (const auto& m : methods) {
            if (m.RVA == 0) continue;
            std::string text;
            if (!CSharp::CSharpDecompiler::DecompileMethodToString(
                    module, m.Token, m.RVA, m.Name, text))
                continue;
            firstBody = m.Token;
            firstMethod = m.Name;
            break;
        }
        if (firstBody == 0) continue;
        // Register the method as partially declared: it disappears from the
        // type's rendered members.
        Metadata::PartialTypeInfo info(t.Token);
        info.AddDeclaredMember(firstBody);
        CSharp::CSharpDecompiler::AddPartialTypeDefinition(info);
        std::string text;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            module, t.Token, text));
        EXPECT_EQ(text.find(firstMethod), std::string::npos)
            << "the declared member is skipped in the type render";
        SUCCEED();
        return;
    }
    FAIL() << "no type with a decodable body in the corpus";
}

// The merge: two infos for the same declaring type unionize (the C#
// AddDeclaredMembers path in AddPartialTypeDefinition).
TEST(CSharpDecompilerTest, PartialTypeInfosMergeForTheSameType)
{
    Metadata::PartialTypeInfo a(0x02000001);
    a.AddDeclaredMember(0x06000001);
    Metadata::PartialTypeInfo b(0x02000001);
    b.AddDeclaredMember(0x06000002);
    CSharp::CSharpDecompiler::AddPartialTypeDefinition(a);
    CSharp::CSharpDecompiler::AddPartialTypeDefinition(b);
    // The merged registry holds both members: the accessors answer
    // through the registry's copy (the C# `partialTypes.TryGetValue` shape;
    // the lookup surface is the static FindPartialTypeInfo probe).
    EXPECT_TRUE(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000001)
                    ->IsDeclaredMember(0x06000001));
    EXPECT_TRUE(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000001)
                    ->IsDeclaredMember(0x06000002));
    EXPECT_FALSE(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000001)
                     ->IsDeclaredMember(0x06000003));
    EXPECT_EQ(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000009),
              nullptr)
        << "an unregistered type has no partial info";
}

// The module/assembly-attributes entry (the C#
// `public string DecompileModuleAndAssemblyAttributesToString()` --
// CSharpDecompiler.cs line 838): the connid_res corpus renders
// `[assembly: ...]` attribute lines (the C# `[assembly: Attr(...)]`
// section shape; the attribute arguments render from the FixedArguments).
TEST(CSharpDecompilerTest, DecompileModuleAndAssemblyAttributesRender)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    ConnIdCompilation compilation;
    TS::MetadataModule module{compilation, &file, TS::TypeSystemOptions::Default};
    compilation.SetMainModule(&module);
    std::string text =
        CSharp::CSharpDecompiler::DecompileModuleAndAssemblyAttributesToString(
            module);
    EXPECT_FALSE(text.empty())
        << "the corpus assembly carries assembly attributes";
    EXPECT_NE(text.find("[assembly:"), std::string::npos)
        << "the attribute section carries the assembly target";
    EXPECT_NE(text.find("[module:"), std::string::npos)
        << "the module section carries the module target";
    // The corpus rows (the mscorlib-backed decode): the compiler-emitted
    // set over a csc net10.0 library.
    EXPECT_NE(text.find("CompilationRelaxationsAttribute"), std::string::npos);
    EXPECT_NE(text.find("RuntimeCompatibilityAttribute"), std::string::npos);
    EXPECT_NE(text.find("DebuggableAttribute"), std::string::npos);
    EXPECT_NE(text.find("AssemblyVersionAttribute"), std::string::npos);
    EXPECT_NE(text.find("RefSafetyRulesAttribute"), std::string::npos);
    if (std::getenv("TET_TRACE")) {
        std::fprintf(stderr, "TET-ATTR: %s\n", text.c_str());
    }
}

// The type-level field/property surfaces (the C# DecompileType member
// iteration covers fields and properties; the port renders a field's
// `Type name;` declaration from GetFields + GetFieldSignature, and the
// property accessors ride the method iteration). The connid_res corpus
// (MyApp.Page1, the XAML code-behind) carries at least one decodable
// member set.
TEST(CSharpDecompilerTest, DecompileTypeRendersFieldsAndProperties)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    // MyApp.Page1 carries the _contentLoaded field (the XAML code-behind's
    // IComponentConnector pattern).
    for (const auto& t : module.TypeDefs()) {
        if (t.Name != "Page1") continue;
        auto fields = module.GetFields(t.Token);
        ASSERT_FALSE(fields.empty()) << "Page1 carries its code-behind fields";
        std::string text;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            module, t.Token, text));
        // The field surfaces in the rendered members.
        EXPECT_NE(text.find("_contentLoaded"), std::string::npos)
            << "the code-behind field renders in the type body";
        return;
    }
    FAIL() << "the connid corpus has no Page1 type";
}

// The `partial` type-header modifier (the C# DecompileType renders the
// type declaration with the `partial` modifier when the type carries
// partial-type info -- CSharpDecompiler.cs DoDecompileType, the
// `hasPartialTypeDeclaration` gate): the facade's type render emits the
// `public partial class Name` header over the members.
TEST(CSharpDecompilerTest, DecompileTypeEmitsPartialHeader)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    for (const auto& t : module.TypeDefs()) {
        if (t.Name != "Page1") continue;
        std::string text;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            module, t.Token, text));
        // The XAML code-behind is generated as a partial class (the
        // InitializeComponent half).
        EXPECT_NE(text.find("public partial class Page1"), std::string::npos)
            << "the partial type header renders";
        if (std::getenv("TET_TRACE")) {
            std::fprintf(stderr, "TET-TYPE: %s\n", text.c_str());
        }
        return;
    }
    FAIL() << "the connid corpus has no Page1 type";
}

} // namespace
