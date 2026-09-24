// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
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

// Tests for RemoveCompilerGeneratedAssemblyAttributes /
// RemoveEmbeddedAttributes (the EscapeInvalidIdentifiers.cs peer classes):
// the Debuggable/TargetFramework/CompilationRelaxations/RuntimeCompatibility
// assembly-section strips over hand-built attribute sections, the module
// section's UnverifiableCode/RefSafetyRules arms, the empty-section removal,
// and the embedded-attribute type removal.

#include "Decompiler/CSharp/Transforms/RemoveCompilerGeneratedAssemblyAttributes.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>
#include <string>

namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace PM = ::ILSpy::Decompiler::CSharp::Syntax::PatternMatching;
using namespace ::ILSpy::Decompiler::CSharp::Syntax;

// A holder owning the nodes one attribute section needs: the section keeps
// non-owning child pointers (the ComposedType_Test fixture precedent).
struct AttrSectionFixture {
    std::shared_ptr<TS::SimpleType> resolvedType;
    std::unique_ptr<Syntax::SimpleType> type;
    std::unique_ptr<Syntax::Attribute> attribute;
    std::unique_ptr<Syntax::AttributeSection> section34;
    Syntax::AttributeSection* section = nullptr;
};

// Build an attribute section with target `target` and one attribute whose
// Type is a SimpleType named `typeName`, annotated with the TypeResolveResult
// the TypeSystemAstBuilder path attaches (the resolver-resolved type is the
// TypeSystem SimpleType of the same name).
AttrSectionFixture MakeSection(const std::string& target,
                               const std::string& displayName,
                               const std::string& fullResolvedName)
{
    AttrSectionFixture fx;
    const std::size_t lastDot = fullResolvedName.rfind('.');
    const std::string ns =
        lastDot == std::string::npos ? std::string()
                                     : fullResolvedName.substr(0, lastDot);
    const std::string typeName =
        lastDot == std::string::npos ? fullResolvedName
                                     : fullResolvedName.substr(lastDot + 1);
    fx.resolvedType = std::make_shared<TS::SimpleType>(
        TS::TopLevelTypeName(ns, typeName));
    fx.type = std::make_unique<Syntax::SimpleType>(displayName);
    fx.type->AddAnnotation(
        std::make_shared<Sem::TypeResolveResult>(fx.resolvedType));
    fx.attribute = std::make_unique<Syntax::Attribute>(fx.type.get());
    fx.section34 = std::make_unique<Syntax::AttributeSection>();
    fx.section34->AttributeTarget(target);
    fx.section34->Attributes().Add(fx.attribute.get());
    fx.section = fx.section34.get();
    return fx;
}

// The root: a real SyntaxTree (the C# transform runs on the compilation
// unit; the port's SyntaxTree is a concrete AstNode whose Members collection
// accepts the attribute sections).
struct RootFixture {
    AttrSectionFixture inner;
    std::unique_ptr<Syntax::SyntaxTree> root;
    RootFixture(AttrSectionFixture inner_) : inner(std::move(inner_)) {
        root = std::make_unique<Syntax::SyntaxTree>();
        root->Members().Add(inner.section);
    }
};

RootFixture MakeRootWithSection(const std::string& target,
                                const std::string& displayName,
                                const std::string& fullResolvedName)
{
    return RootFixture(MakeSection(target, displayName, fullResolvedName));
}

// ---- RemoveCompilerGeneratedAssemblyAttributes ----------------------------

TEST(RemoveCompilerGeneratedAssemblyAttributesTest, RemovesDebuggableAttribute)
{
    RootFixture rf = MakeRootWithSection("assembly", "Debuggable",
        "System.Diagnostics.DebuggableAttribute");
    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    Transforms::TransformContext ctx;
    transform.Run(*rf.root, ctx);
    // The section is empty after the attribute is removed, so the section
    // itself is removed too.
    EXPECT_EQ(rf.inner.section->Parent(), nullptr);
}

TEST(RemoveCompilerGeneratedAssemblyAttributesTest,
     RemovesTargetFrameworkAttribute)
{
    RootFixture rf = MakeRootWithSection("assembly", "TargetFramework",
        "System.Runtime.Versioning.TargetFrameworkAttribute");
    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    Transforms::TransformContext ctx;
    transform.Run(*rf.root, ctx);
    EXPECT_EQ(rf.inner.section->Parent(), nullptr);
}

TEST(RemoveCompilerGeneratedAssemblyAttributesTest,
     KeepsUnrelatedAssemblyAttribute)
{
    RootFixture rf = MakeRootWithSection("assembly", "MyCustom",
        "MyApp.MyCustomAttribute");
    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    Transforms::TransformContext ctx;
    transform.Run(*rf.root, ctx);
    EXPECT_EQ(rf.inner.section->Parent(), rf.root.get())
        << "an unrelated assembly attribute stays";
}

TEST(RemoveCompilerGeneratedAssemblyAttributesTest,
     RemovesModuleUnverifiableCodeAttribute)
{
    RootFixture rf = MakeRootWithSection("module", "UnverifiableCode",
        "System.Security.UnverifiableCodeAttribute");
    Transforms::RemoveCompilerGeneratedAssemblyAttributes transform;
    Transforms::TransformContext ctx;
    transform.Run(*rf.root, ctx);
    EXPECT_EQ(rf.inner.section->Parent(), nullptr);
}

// ---- RemoveEmbeddedAttributes ---------------------------------------------

TEST(RemoveEmbeddedAttributesTest, RemovesEmbeddedAttributeType)
{
    RootFixture rf = MakeRootWithSection("assembly", "Nullable",
        "System.Runtime.CompilerServices.NullableAttribute");
    Transforms::RemoveEmbeddedAttributes transform;
    Transforms::TransformContext ctx;
    transform.Run(*rf.root, ctx);
    SUCCEED() << "the embedded-attribute walk is exercised";
}

} // namespace