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

// Tests for the project-export transform `RemoveCompilerGeneratedAssemblyAttributes`: the
// `assembly` arm's name-and-argument guards (Debuggable/TargetFramework unconditional,
// CompilationRelaxations(8), RuntimeCompatibility(WrapNonExceptionThrows = true),
// SecurityPermission(RequestMinimum, SkipVerification = true)), the `module` arm
// (UnverifiableCode/RefSafetyRules), the non-assembly/module target skip, the unannotated
// keep, and the empty-section cleanup. The type name is supplied through a
// `TypeResolveResult` annotation carrying a definition-less `IType` (the transformer reads
// the resolved type's full name through the `ReflectionName` fallback).

#include "Decompiler/CSharp/Transforms/RemoveCompilerGeneratedAssemblyAttributes.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the RemoveCLSCompliantAttribute suite shape).
struct TransformFixture {
    TS::SimpleCompilation compilation;
    DecompilerSettings settings;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope;
    DecompileRun run;
    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context;
    Syntax::TypeSystemAstBuilder astBuilder;

    TransformFixture()
        : compilation(Impl::MinimalCorlib::Instance(), {}),
          usingScope(MakeScope()),
          run(&settings, usingScope),
          context(std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
              compilation.MainModule(), usingScope))
    {
    }

    std::shared_ptr<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> MakeScope() {
        auto root = std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
            compilation.MainModule());
        return std::make_shared<::ILSpy::Decompiler::CSharp::TypeSystem::UsingScope>(
            root, compilation.RootNamespace(), std::vector<const TS::INamespace*>{});
    }

    Transforms::TransformContext MakeContext() {
        return Transforms::TransformContext(compilation, run, *context, astBuilder);
    }
};

// A minimal named `IType` for the annotation: it carries the full name as both `Name` and
// `ReflectionName` and has no definition, so the transformer's fallback supplies the value.
class NamedType : public TS::IType {
public:
    explicit NamedType(std::string fullName) : fullName_(std::move(fullName)) {}
    TS::TypeKind Kind() const override { return TS::TypeKind::Unknown; }
    std::string Name() const override { return fullName_; }
    std::string ReflectionName() const override { return fullName_; }
    int TypeParameterCount() const override { return 0; }
    bool StructuralEquals(const TS::IType& other) const override {
        const auto* o = dynamic_cast<const NamedType*>(&other);
        return o != nullptr && o->fullName_ == fullName_;
    }

private:
    std::string fullName_;
};

std::shared_ptr<Sem::TypeResolveResult> ResolveTo(std::string fullName) {
    return std::make_shared<Sem::TypeResolveResult>(
        std::make_shared<NamedType>(std::move(fullName)));
}

// A `[Type]` attribute whose `Type` carries the given resolve result (or none).
Syntax::Attribute* MakeAttribute(const std::string& typeName, bool annotated,
                                 std::string fullName) {
    auto* type = new Syntax::SimpleType(typeName);
    if (annotated)
        type->AddAnnotation(ResolveTo(std::move(fullName)));
    return new Syntax::Attribute(type);
}

// A `[Type(args...)]` attribute: the same annotated type plus the given argument nodes.
Syntax::Attribute* MakeAttributeWithArgs(const std::string& typeName, std::string fullName,
                                         std::vector<Syntax::Expression*> arguments) {
    auto* attribute = MakeAttribute(typeName, true, std::move(fullName));
    for (Syntax::Expression* argument : arguments)
        attribute->Arguments().Add(argument);
    return attribute;
}

Syntax::AttributeSection* MakeSection(const std::string& target,
                                      std::vector<Syntax::Attribute*> attributes) {
    auto* section = new Syntax::AttributeSection();
    if (!target.empty())
        section->AttributeTarget(target);
    for (Syntax::Attribute* attribute : attributes)
        section->Attributes().Add(attribute);
    return section;
}

} // namespace

// The `assembly`-targeted DebuggableAttribute is removed unconditionally and, as the only
// attribute, takes its now-empty section with it.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesDebuggableAttribute)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("assembly",
        {MakeAttribute("DebuggableAttribute", true, "System.Diagnostics.DebuggableAttribute")}));

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// The `assembly`-targeted TargetFrameworkAttribute is removed unconditionally.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesTargetFrameworkAttribute)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("assembly",
        {MakeAttribute("TargetFrameworkAttribute", true, "System.Runtime.Versioning.TargetFrameworkAttribute")}));

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// CompilationRelaxationsAttribute(8) is removed (the compiler-emitted value).
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesCompilationRelaxationsWith8)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("assembly",
        {MakeAttributeWithArgs("CompilationRelaxationsAttribute",
            "System.Runtime.CompilerServices.CompilationRelaxationsAttribute",
            {new Syntax::PrimitiveExpression(std::int32_t{8})})}));

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// CompilationRelaxationsAttribute with any other literal is kept.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, KeepsCompilationRelaxationsWithOtherValue)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("assembly",
        {MakeAttributeWithArgs("CompilationRelaxationsAttribute",
            "System.Runtime.CompilerServices.CompilationRelaxationsAttribute",
            {new Syntax::PrimitiveExpression(std::int32_t{7})})});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// RuntimeCompatibility(WrapNonExceptionThrows = true) is removed.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesRuntimeCompatibilityWrapTrue)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("assembly",
        {MakeAttributeWithArgs("RuntimeCompatibilityAttribute",
            "System.Runtime.CompilerServices.RuntimeCompatibilityAttribute",
            {new Syntax::NamedExpression("WrapNonExceptionThrows",
                new Syntax::PrimitiveExpression(true))})}));

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// RuntimeCompatibility(WrapNonExceptionThrows = false) is kept.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, KeepsRuntimeCompatibilityWrapFalse)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("assembly",
        {MakeAttributeWithArgs("RuntimeCompatibilityAttribute",
            "System.Runtime.CompilerServices.RuntimeCompatibilityAttribute",
            {new Syntax::NamedExpression("WrapNonExceptionThrows",
                new Syntax::PrimitiveExpression(false))})});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// RuntimeCompatibility with a differently-named argument is kept (the C# name guard).
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, KeepsRuntimeCompatibilityWrongNamedArgument)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("assembly",
        {MakeAttributeWithArgs("RuntimeCompatibilityAttribute",
            "System.Runtime.CompilerServices.RuntimeCompatibilityAttribute",
            {new Syntax::NamedExpression("SomethingElse",
                new Syntax::PrimitiveExpression(true))})});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// SecurityPermission(SecurityAction.RequestMinimum, SkipVerification = true) is removed.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesSecurityPermissionRequestMinimum)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("assembly",
        {MakeAttributeWithArgs("SecurityPermissionAttribute",
            "System.Security.Permissions.SecurityPermissionAttribute",
            {new Syntax::MemberReferenceExpression(
                 new Syntax::IdentifierExpression("SecurityAction"), "RequestMinimum"),
             new Syntax::NamedExpression("SkipVerification",
                 new Syntax::PrimitiveExpression(true))})}));

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// SecurityPermission without the SkipVerification = true pair is kept.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, KeepsSecurityPermissionWithoutSkipVerification)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("assembly",
        {MakeAttributeWithArgs("SecurityPermissionAttribute",
            "System.Security.Permissions.SecurityPermissionAttribute",
            {new Syntax::MemberReferenceExpression(
                 new Syntax::IdentifierExpression("SecurityAction"), "RequestMinimum"),
             new Syntax::NamedExpression("SkipVerification",
                 new Syntax::PrimitiveExpression(false))})});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// The `module`-targeted UnverifiableCodeAttribute is removed.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesModuleUnverifiableCode)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("module",
        {MakeAttribute("UnverifiableCodeAttribute", true, "System.Security.UnverifiableCodeAttribute")}));

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// The `module`-targeted RefSafetyRulesAttribute is removed.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesModuleRefSafetyRules)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("module",
        {MakeAttribute("RefSafetyRulesAttribute", true, "System.Runtime.CompilerServices.RefSafetyRulesAttribute")}));

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// A `module`-targeted attribute of any other type is kept.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, KeepsModuleOtherAttribute)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("module",
        {MakeAttribute("SomeAttribute", true, "Some.Namespace.SomeAttribute")});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// A section with any target other than `assembly`/`module` is left completely untouched
// (the C# `continue` before the empty-section cleanup).
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, LeavesNonAssemblyModuleTargetUntouched)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("type",
        {MakeAttribute("DebuggableAttribute", true, "System.Diagnostics.DebuggableAttribute")});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// An unannotated assembly attribute never matches (the C# `trr == null` guard).
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, KeepsUnannotatedAssemblyAttribute)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("assembly",
        {MakeAttribute("DebuggableAttribute", false, "")});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// A mixed assembly section loses only the compiler-generated attribute and survives with
// the remaining one.
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesOnlyGeneratedFromMixedSection)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("assembly",
        {MakeAttribute("SomeAttribute", true, "Some.Namespace.SomeAttribute"),
         MakeAttribute("DebuggableAttribute", true, "System.Diagnostics.DebuggableAttribute")});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    ASSERT_EQ(section->Attributes().Count(), 1);
    EXPECT_EQ(section->Attributes().At(0)->Type()->ToString(), "SomeAttribute");
}

// A compiler-generated-shaped simple name in a different namespace is kept (the full-name
// match, not the simple name).
TEST(RemoveCompilerGeneratedAssemblyAttributesTest, KeepsSameSimpleNameInDifferentNamespace)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("assembly",
        {MakeAttribute("DebuggableAttribute", true, "Other.DebuggableAttribute")});
    tree.Members().Add(section);

    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}
