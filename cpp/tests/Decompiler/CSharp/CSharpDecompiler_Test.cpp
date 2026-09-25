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
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
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
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
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
        // The registry is process-global (the static placeholder): clear
        // the registration so the later corpus-driven tests do not see it.
        CSharp::CSharpDecompiler::ClearPartialTypes();
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
    // The registry lookup surface is the static FindPartialTypeInfo
    // probe.
    EXPECT_TRUE(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000001)
                    ->IsDeclaredMember(0x06000001));
    EXPECT_TRUE(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000001)
                    ->IsDeclaredMember(0x06000002));
    EXPECT_FALSE(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000001)
                     ->IsDeclaredMember(0x06000003));
    // The registry is process-global (the static placeholder for the C#
    // instance field): clear it so the later corpus-driven tests (the
    // whole-module render) do not see this test's registrations.
    CSharp::CSharpDecompiler::ClearPartialTypes();
    EXPECT_EQ(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000001),
              nullptr);
    EXPECT_EQ(CSharp::CSharpDecompiler::FindPartialTypeInfo(0x02000009),
              nullptr)
        << "an unregistered type has no partial info";
}

// The module/assembly-attributes entry (the C#
// `public string DecompileModuleAndAssemblyAttributesToString()` --
// CSharpDecompiler.cs line 838): the connid_res corpus renders
// `[assembly: ...]` attribute lines (the C# `[assembly: Attr(...)]`
// section shape; the attribute arguments render from the FixedArguments).
// The C# `public SyntaxTree DecompileModuleAndAssemblyAttributes()` (line
// 823): the AST path -- the attribute sections built through the
// TypeSystemAstBuilder's ConvertAttribute, the transform pipeline run over
// the tree. Each section carries its target and exactly one attribute.
TEST(CSharpDecompilerTest, DecompileModuleAndAssemblyAttributesBuildsTree)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    ConnIdCompilation compilation;
    TS::MetadataModule module{compilation, &file, TS::TypeSystemOptions::Default};
    compilation.SetMainModule(&module);

    std::unique_ptr<Syntax::SyntaxTree> tree(
        CSharp::CSharpDecompiler::DecompileModuleAndAssemblyAttributes(
            module));
    ASSERT_NE(tree, nullptr);
    int assemblySections = 0;
    int moduleSections = 0;
    for (int i = 0; i < tree->Members().Count(); i++) {
        auto* section =
            dynamic_cast<Syntax::AttributeSection*>(tree->Members().At(i));
        ASSERT_NE(section, nullptr)
            << "every member is an attribute section";
        ASSERT_EQ(section->Attributes().Count(), 1)
            << "each section holds exactly one attribute";
        if (section->AttributeTarget() == "assembly")
            assemblySections++;
        else if (section->AttributeTarget() == "module")
            moduleSections++;
        else
            FAIL() << "unexpected target " << section->AttributeTarget();
    }
    EXPECT_GT(assemblySections, 0)
        << "the corpus assembly carries assembly attributes";
    EXPECT_GT(moduleSections, 0)
        << "the corpus module carries the RefSafetyRules attribute";
}

// The C# `public string DecompileWholeModuleAsString()` (line 1220): the
// whole-module render -- the module/assembly attribute sections (the AST
// path), then every type in metadata order (the `<Module>` placeholder
// skipped, the C# DoDecompileTypes gate).
TEST(CSharpDecompilerTest, DecompileWholeModuleRendersAttributesAndTypes)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile file(path);
    ASSERT_TRUE(file.IsValid());
    std::string text =
        CSharp::CSharpDecompiler::DecompileWholeModuleToString(file);
    // The module/assembly attribute sections lead the output.
    EXPECT_NE(text.find("[assembly:"), std::string::npos);
    EXPECT_NE(text.find("[module:"), std::string::npos);
    // Every type renders (metadata order); the <Module> placeholder is
    // skipped.
    EXPECT_NE(text.find("public partial class Page1"), std::string::npos);
    EXPECT_EQ(text.find("<Module>"), std::string::npos);
    // The members ride the type bodies (the property surface lands with
    // this same entry's type loop).
    EXPECT_NE(text.find("InitializeComponent()"), std::string::npos);
    // The constructor renders with the type name (not the raw .ctor).
    EXPECT_EQ(text.find(".ctor"), std::string::npos);
}

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
    // set over a csc net10.0 library, in the C#-faithful short-name form
    // (the TypeSystemAstBuilder strips the trailing "Attribute" suffix).
    EXPECT_NE(text.find("CompilationRelaxations"), std::string::npos);
    EXPECT_NE(text.find("RuntimeCompatibility"), std::string::npos);
    EXPECT_NE(text.find("Debuggable"), std::string::npos);
    EXPECT_NE(text.find("AssemblyVersion"), std::string::npos);
    EXPECT_NE(text.find("RefSafetyRules"), std::string::npos);
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
// The decode-error shape (the C# TypeSystemAstBuilder.ConvertAttribute's
// `HasDecodeErrors` arm: `HasArgumentList = true` plus the
// `ErrorExpression("Could not decode attribute arguments.")`, which renders
// purely as its comment): an attribute whose blob fails to decode (the
// connid Debuggable row's enum argument resolves through a referenced core
// library the connid-only compilation does not carry) renders in the
// output-visitor form: `Debuggable (/*Could not decode attribute
// arguments.*/)` (the multi-line comment carries no inner padding, the
// NRefactory trivia-writer shape).
TEST(CSharpDecompilerTest,
     DecompileModuleAttributesRenderDecodeErrorsAsComments)
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
    EXPECT_NE(
        text.find("Debuggable (/*Could not decode attribute arguments.*/)"),
        std::string::npos)
        << "the decode-error attribute renders as the C# comment form: "
        << text;
}

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
// The property member surface (the C# DecompileType's DoDecompileMember
// property arm): a property renders as its `Type Name { get; set; }`
// declaration -- the type from the getter's return signature (the C# reads
// the property signature; the accessor's return type carries the same type
// by the compiler's contract), the accessor presence from the
// MethodSemantics lookup. The accessor METHODS no longer render separately
// (the C# renders them through the property), and the compiler-generated
// `<Name>k__BackingField` declarations disappear (the auto-property end
// state the AST-level transform produces).
TEST(CSharpDecompilerTest, DecompileTypeRendersProperties)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    for (const auto& t : module.TypeDefs()) {
        if (t.Name != "EventSetter") continue;
        std::string text;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            module, t.Token, text));
        // Event: get + set; Handler: get + set; Setters is getter-only.
        EXPECT_NE(text.find("Event { get; set; }"), std::string::npos)
            << "the property renders as its auto-property declaration: "
            << text;
        EXPECT_NE(text.find("Handler { get; set; }"), std::string::npos);
        EXPECT_EQ(text.find("get_Event("), std::string::npos)
            << "the accessor methods render through the property";
        EXPECT_EQ(text.find("set_Handler("), std::string::npos);
        EXPECT_EQ(text.find("k__BackingField;"), std::string::npos)
            << "the auto-property backing field declarations disappear";
        return;
    }
    FAIL() << "the connid corpus has no EventSetter type";
}

// The getter-only property form (the Style.Setters shape).
TEST(CSharpDecompilerTest, DecompileTypeRendersGetterOnlyProperties)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    for (const auto& t : module.TypeDefs()) {
        if (t.Name != "Style") continue;
        std::string text;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            module, t.Token, text));
        EXPECT_NE(text.find("Setters { get; }"), std::string::npos)
            << "the getter-only property carries only the get accessor: "
            << text;
        EXPECT_EQ(text.find("set_Setters("), std::string::npos);
        return;
    }
    FAIL() << "the connid corpus has no Style type";
}

// The constructor member surface (the C# DoDecompileMember's constructor
// arm): a `.ctor` method renders with the TYPE name and no return type
// (the C# `public EventSetter()` header; the flat renderer carries no
// modifiers).
TEST(CSharpDecompilerTest, DecompileTypeRendersConstructors)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ASSERT_FALSE(path.empty());
    ::ILSpy::Decompiler::Metadata::MetadataFile module(path);
    ASSERT_TRUE(module.IsValid());
    for (const auto& t : module.TypeDefs()) {
        if (t.Name != "EventSetter") continue;
        std::string text;
        ASSERT_TRUE(CSharp::CSharpDecompiler::DecompileTypeToString(
            module, t.Token, text));
        EXPECT_NE(text.find("EventSetter()"), std::string::npos)
            << "the constructor renders with the type name: " << text;
        EXPECT_EQ(text.find(".ctor"), std::string::npos)
            << "the raw metadata constructor name does not leak: " << text;
        return;
    }
    FAIL() << "the connid corpus has no EventSetter type";
}

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

// The fixed-argument decode (the C# TypeSystemAstBuilder.ConvertAttribute
// shape -- each positional argument renders as its constant literal, a
// string in double quotes, a bool as true/false, the numerics as their
// text): the connid corpus's assembly attributes carry decodable fixed
// arguments (the CompilationRelaxations(8) int and the AssemblyVersion
// string).
TEST(CSharpDecompilerTest, DecompileModuleAttributesRenderFixedArguments)
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
    // CompilationRelaxations (8): the compiler-emitted relaxation value
    // renders as the numeric literal (the short-name + the Mono policy's
    // space-before-call-parentheses, the output-visitor form).
    EXPECT_NE(text.find("CompilationRelaxations (8)"),
              std::string::npos)
        << "the int fixed argument renders: " << text;
    if (std::getenv("TET_TRACE")) {
        std::fprintf(stderr, "TET-ATTR: %s\n", text.c_str());
    }
}


TEST(CSharpDecompilerTest, ProbeDumpMembers)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ::ILSpy::Decompiler::Metadata::MetadataFile f(path);
    for (const auto& t : f.TypeDefs()) {
        std::fprintf(stderr, "TYPE %s (0x%08x)\n", t.Name.c_str(), t.Token);
        for (const auto& pr : f.GetProperties(t.Token))
            std::fprintf(stderr, "  PROP %s (0x%08x)\n", pr.Name.c_str(), pr.Token);
        for (const auto& e : f.GetEvents(t.Token))
            std::fprintf(stderr, "  EVENT %s (0x%08x)\n", e.Name.c_str(), e.Token);
        for (const auto& m : f.GetMethods(t.Token))
            std::fprintf(stderr, "  METHOD %s (0x%08x, rva=%u)\n", m.Name.c_str(), m.Token, m.RVA);
    }
}

TEST(CSharpDecompilerTest, ProbeDumpRender)
{
    std::string path = ILSpy::Tests::WriteConnIdResDll();
    ::ILSpy::Decompiler::Metadata::MetadataFile f(path);
    for (const auto& t : f.TypeDefs()) {
        if (t.Name != "EventSetter") continue;
        std::string text;
        CSharp::CSharpDecompiler::DecompileTypeToString(f, t.Token, text);
        std::fprintf(stderr, "RENDER:\n%s\n", text.c_str());
    }
}
} // namespace

