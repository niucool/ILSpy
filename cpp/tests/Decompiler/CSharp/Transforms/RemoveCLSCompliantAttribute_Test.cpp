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

// Tests for the project-export transform `RemoveCLSCompliantAttribute`: the
// assembly-target skip, the `System.CLSCompliantAttribute` removal, the
// empty-section cleanup, and the unannotated / different-type keeps. The type
// name is supplied through a `TypeResolveResult` annotation carrying an
// `UnknownType` (the transformer reads the resolved type's full name).

#include "Decompiler/CSharp/Transforms/RemoveCLSCompliantAttribute.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
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

#include <memory>
#include <optional>
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

// The TransformContext fixture (the EscapeInvalidIdentifiers suite shape): a real
// compilation over MinimalCorlib plus the resolve/ast-builder state a context composes.
// RemoveCLSCompliantAttribute ignores the context, but the Run contract requires one.
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

// A minimal named `IType` for the annotation: it carries the full name as both
// `Name` and `ReflectionName` and has no definition, so the transformer's
// fallback (`ReflectionName`) supplies the matched value.
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

// A `TypeResolveResult` over a named type (the transformer reads the resolved type's
// full name).
std::shared_ptr<Sem::TypeResolveResult> ResolveTo(std::string fullName) {
    return std::make_shared<Sem::TypeResolveResult>(
        std::make_shared<NamedType>(std::move(fullName)));
}

// A `[Type]` attribute whose `Type` carries the given resolve result (or none).
// `fullName` is the resolved full name (only used when `annotated`).
Syntax::Attribute* MakeAttribute(const std::string& typeName, bool annotated,
                                 std::string fullName = "System.CLSCompliantAttribute") {
    auto* type = new Syntax::SimpleType(typeName);
    if (annotated)
        type->AddAnnotation(ResolveTo(std::move(fullName)));
    return new Syntax::Attribute(type);
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

// A non-assembly section holding only a `[CLSCompliant]` attribute loses the
// attribute and, with it, the now-empty section.
TEST(RemoveCLSCompliantAttributeTest, RemovesAttributeAndEmptySection)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("",
        {MakeAttribute("CLSCompliantAttribute", true)}));

    Transforms::RemoveCLSCompliantAttribute transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// The `assembly`-targeted section is skipped verbatim (the assembly identity is
// written by the project file, not the generated AssemblyInfo.cs).
TEST(RemoveCLSCompliantAttributeTest, KeepsAssemblyTargetedSection)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("assembly",
        {MakeAttribute("CLSCompliantAttribute", true)}));

    Transforms::RemoveCLSCompliantAttribute transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    auto* section = dynamic_cast<Syntax::AttributeSection*>(tree.Members().At(0));
    ASSERT_NE(section, nullptr);
    EXPECT_EQ(section->AttributeTarget(), "assembly");
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// A non-assembly section keeps attributes of other types; only the CLSCompliant
// one is removed and the section survives with the remaining attribute.
TEST(RemoveCLSCompliantAttributeTest, RemovesOnlyClsCompliantFromMixedSection)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("",
        {MakeAttribute("ObsoleteAttribute", true, "System.ObsoleteAttribute"),
         MakeAttribute("CLSCompliantAttribute", true)});
    tree.Members().Add(section);

    Transforms::RemoveCLSCompliantAttribute transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    ASSERT_EQ(section->Attributes().Count(), 1);
    EXPECT_EQ(section->Attributes().At(0)->Type()->ToString(), "ObsoleteAttribute");
}

// An unannotated type node never matches (the C# `trr == null` guard), so the
// section is preserved.
TEST(RemoveCLSCompliantAttributeTest, KeepsUnannotatedAttribute)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("",
        {MakeAttribute("CLSCompliantAttribute", false)});
    tree.Members().Add(section);

    Transforms::RemoveCLSCompliantAttribute transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}

// A `module`-targeted section is a non-assembly target, so its CLSCompliant
// attribute is removed as well.
TEST(RemoveCLSCompliantAttributeTest, RemovesFromModuleTargetedSection)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    tree.Members().Add(MakeSection("module",
        {MakeAttribute("CLSCompliantAttribute", true)}));

    Transforms::RemoveCLSCompliantAttribute transform;
    transform.Run(tree, context);

    EXPECT_EQ(tree.Members().Count(), 0);
}

// A type whose full name is not exactly `System.CLSCompliantAttribute` (here a
// different namespace) is kept even when annotated.
TEST(RemoveCLSCompliantAttributeTest, KeepsSameSimpleNameInDifferentNamespace)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    Syntax::SyntaxTree tree;
    auto* section = MakeSection("",
        {MakeAttribute("CLSCompliantAttribute", true, "Other.CLSCompliantAttribute")});
    tree.Members().Add(section);

    Transforms::RemoveCLSCompliantAttribute transform;
    transform.Run(tree, context);

    ASSERT_EQ(tree.Members().Count(), 1);
    EXPECT_EQ(section->Attributes().Count(), 1);
}
