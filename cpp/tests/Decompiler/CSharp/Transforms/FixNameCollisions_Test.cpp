// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

// Tests for the `FixNameCollisions` transform: the private single-variable field
// rename when its name collides with a single-named member of the same type, the
// `m_`/numbered fallback naming, the accessibility and field-shape guards, the
// explicit-interface member keying, and the reference-retarget walk for both
// IdentifierExpression and MemberReferenceExpression.
//
// Symbols are supplied through the port's `MemberResolveResult` annotation channel:
// the declaration's annotation and the reference's annotation carry the SAME
// `IField*`, which is the identity the transform's rename dictionary keys on.

#include "Decompiler/CSharp/Transforms/FixNameCollisions.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the RenameVisualBasicAnonymousTypes suite shape): a real
// compilation over MinimalCorlib plus the resolve/ast-builder state a context composes.
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

// A minimal named `IType` used only to satisfy the `MemberResolveResult`'s non-null
// return type (the transform never reads a field's type).
class StubType : public TS::IType {
public:
    explicit StubType(std::string name) : name_(std::move(name)) {}
    TS::TypeKind Kind() const override { return TS::TypeKind::Unknown; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    bool StructuralEquals(const TS::IType& other) const override { return &other == this; }

private:
    std::string name_;
};

// A field stub whose `Accessibility` the transform reads: subclasses the shared
// `LookupField` and overrides the accessor (the shared stub hardcodes Public).
class FieldStub : public TestSupport::LookupField {
public:
    FieldStub(std::string name, TS::Accessibility accessibility,
              const TS::ICompilation& compilation)
        : LookupField(std::move(name), TS::ITypePtr{}, compilation),
          accessibility_(accessibility) {}

    TS::Accessibility Accessibility() const override { return accessibility_; }

private:
    TS::Accessibility accessibility_;
};

// The `MemberResolveResult` annotation over the given field (a non-null return type is
// required by the port's resolve-result base).
std::shared_ptr<Sem::MemberResolveResult> ResolveToMember(const TS::IMember* member) {
    return std::make_shared<Sem::MemberResolveResult>(
        nullptr, member, std::make_shared<StubType>("FieldType"));
}

// A `FieldDeclaration` with one `VariableInitializer` of the given name, annotated with
// the field symbol.
Syntax::FieldDeclaration* MakeField(const std::string& name, const TS::IMember* field) {
    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* variable = new Syntax::VariableInitializer(name);
    fieldDecl->Variables().Add(variable);
    fieldDecl->AddAnnotation(ResolveToMember(field));
    return fieldDecl;
}

// A named `MethodDeclaration` (a single-named member that adds its name to the type's
// member-name set).
Syntax::MethodDeclaration* MakeMethod(const std::string& name) {
    auto* method = new Syntax::MethodDeclaration();
    method->Name(name);
    return method;
}

// A property whose `PrivateImplementationType` is the given type (an explicit interface
// implementation).
Syntax::PropertyDeclaration* MakeExplicitProperty(const std::string& interfaceName,
                                                  const std::string& name) {
    auto* property = new Syntax::PropertyDeclaration();
    property->Name(name);
    property->PrivateImplementationType(new Syntax::SimpleType(interfaceName));
    return property;
}

} // namespace

// A private field whose name collides with a single-named member is renamed to `m_` +
// name.
TEST(FixNameCollisionsTest, RenamesPrivateFieldCollidingWithMember)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Foo"));
    auto* fieldDecl = MakeField("Foo", &field);
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    ASSERT_EQ(fieldDecl->Variables().Count(), 1);
    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "m_Foo");
}

// A reference (IdentifierExpression) to a renamed field is retargeted to the new name.
TEST(FixNameCollisionsTest, RenamesIdentifierExpressionReference)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Foo"));
    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* reference = new Syntax::IdentifierExpression("Foo");
    reference->AddAnnotation(ResolveToMember(&field));
    fieldDecl->Variables().Add(new Syntax::VariableInitializer("Foo", reference));
    fieldDecl->AddAnnotation(ResolveToMember(&field));
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(reference->Identifier(), "m_Foo");
}

// A reference (MemberReferenceExpression) to a renamed field is retargeted to the new name.
TEST(FixNameCollisionsTest, RenamesMemberReferenceExpressionReference)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Foo"));
    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* reference = new Syntax::MemberReferenceExpression(
        new Syntax::IdentifierExpression("c"), "Foo");
    reference->AddAnnotation(ResolveToMember(&field));
    fieldDecl->Variables().Add(new Syntax::VariableInitializer("Foo", reference));
    fieldDecl->AddAnnotation(ResolveToMember(&field));
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(reference->MemberName(), "m_Foo");
}

// When `m_` + name is already in use, the next free name+num candidate is picked.
TEST(FixNameCollisionsTest, PicksNumberedNameWhenPrefixedNameTaken)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Foo"));
    typeDecl->Members().Add(MakeMethod("m_Foo"));
    auto* fieldDecl = MakeField("Foo", &field);
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "Foo2");
}

// Skip past an in-use `Foo2` as well, to `Foo3`.
TEST(FixNameCollisionsTest, PicksNextNumberedNamePastTakenNumbers)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Foo"));
    typeDecl->Members().Add(MakeMethod("m_Foo"));
    typeDecl->Members().Add(MakeMethod("Foo2"));
    auto* fieldDecl = MakeField("Foo", &field);
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "Foo3");
}

// A private field whose name does not collide is left alone.
TEST(FixNameCollisionsTest, KeepsFieldWithoutCollision)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Bar"));
    auto* fieldDecl = MakeField("Foo", &field);
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "Foo");
}

// A non-private field that collides is left alone (only private fields are renamed).
TEST(FixNameCollisionsTest, KeepsNonPrivateCollidingField)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Public, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Foo"));
    auto* fieldDecl = MakeField("Foo", &field);
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "Foo");
}

// A field declaration with more than one variable is skipped.
TEST(FixNameCollisionsTest, SkipsMultiVariableField)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Foo"));
    auto* fieldDecl = new Syntax::FieldDeclaration();
    fieldDecl->Variables().Add(new Syntax::VariableInitializer("Foo"));
    fieldDecl->Variables().Add(new Syntax::VariableInitializer("Bar"));
    fieldDecl->AddAnnotation(ResolveToMember(&field));
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "Foo");
    EXPECT_EQ(fieldDecl->Variables().At(1)->Name(), "Bar");
}

// A reference to a field that was NOT renamed keeps its name.
TEST(FixNameCollisionsTest, KeepsReferenceToUnrenamedField)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Public, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("Foo"));
    auto* fieldDecl = new Syntax::FieldDeclaration();
    auto* reference = new Syntax::IdentifierExpression("Foo");
    reference->AddAnnotation(ResolveToMember(&field));
    fieldDecl->Variables().Add(new Syntax::VariableInitializer("Foo", reference));
    fieldDecl->AddAnnotation(ResolveToMember(&field));
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(reference->Identifier(), "Foo");
}

// A reference with no symbol annotation is left alone.
TEST(FixNameCollisionsTest, KeepsReferenceWithoutSymbol)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* method = new Syntax::MethodDeclaration();
    method->Name("M");
    method->Body(new Syntax::BlockStatement());
    auto* reference = new Syntax::IdentifierExpression("Foo");
    method->Body()->Statements().Add(new Syntax::ExpressionStatement(reference));
    typeDecl->Members().Add(method);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(reference->Identifier(), "Foo");
}

// An explicit-interface member contributes its `I.Name` key, not its bare name, so a
// private field with the member's short name does NOT collide and is left alone.
TEST(FixNameCollisionsTest, ExplicitInterfaceMemberDoesNotCollideWithBareName)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub field("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeExplicitProperty("I", "Foo"));
    auto* fieldDecl = MakeField("Foo", &field);
    typeDecl->Members().Add(fieldDecl);

    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "Foo");
}

// The walk descends into nested types: a private field in a nested type colliding with a
// member of that nested type is renamed.
TEST(FixNameCollisionsTest, RenamesFieldInNestedType)
{
    TransformFixture fixture;
    Transforms::TransformContext context = fixture.MakeContext();
    FieldStub nestedField("Foo", TS::Accessibility::Private, fixture.compilation);

    auto* nested = new Syntax::TypeDeclaration();
    nested->Name("Nested");
    nested->Members().Add(MakeMethod("Foo"));
    auto* fieldDecl = MakeField("Foo", &nestedField);
    nested->Members().Add(fieldDecl);

    auto* outer = new Syntax::TypeDeclaration();
    outer->Name("C");
    outer->Members().Add(nested);

    Syntax::SyntaxTree tree;
    tree.Members().Add(outer);

    Transforms::FixNameCollisions transform;
    transform.Run(tree, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "m_Foo");
}
