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

// Tests for the `AddXmlDocumentationTransform`: the settings/provider early-out,
// the line-wise documentation trivia insertion (leading blank-line skip, indentation
// stripping, internal blank retention, trailing blank drop), the entity/symbol
// guards, the parameterized-property accessor fallback, the resolve-result
// annotation carried by every inserted comment, and the `XmlException` reporting.

#include "Decompiler/CSharp/Transforms/AddXmlDocumentationTransform.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/CSharp/Syntax/Trivia.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Documentation/IDocumentationProvider.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/Xml/XmlConvert.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Transforms = ::ILSpy::Decompiler::CSharp::Transforms;
namespace Documentation = ::ILSpy::Decompiler::Documentation;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;
using ::ILSpy::Decompiler::DecompileRun;
using ::ILSpy::Decompiler::DecompilerSettings;

namespace {

// The TransformContext fixture (the FixNameCollisions suite shape): a real
// compilation over MinimalCorlib plus the resolve/ast-builder state a context
// composes.
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

// A minimal named `IType` used only to satisfy the `MemberResolveResult`'s
// non-null return type (the transform never reads it).
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

// The `MemberResolveResult` annotation over the given member (a non-null return
// type is required by the port's resolve-result base).
std::shared_ptr<Sem::MemberResolveResult> ResolveToMember(const TS::IMember* member) {
    return std::make_shared<Sem::MemberResolveResult>(
        nullptr, member, std::make_shared<StubType>("MemberType"));
}

// A documentation provider whose per-entity docs are set explicitly and that can be
// told to throw the `XmlException` the transform catches.
class StubDocumentationProvider : public Documentation::IDocumentationProvider {
public:
    std::unordered_map<const TS::IEntity*, std::string> docs;
    bool throwXmlException = false;

    std::optional<std::string> GetDocumentation(const TS::IEntity& entity) const override {
        if (throwXmlException)
            throw ::ILSpy::Decompiler::Xml::XmlException("bad xml");
        auto it = docs.find(&entity);
        if (it == docs.end())
            return std::nullopt;
        return it->second;
    }
};

// A named `MethodDeclaration` whose symbol is the given member.
Syntax::MethodDeclaration* MakeMethod(const std::string& name, const TS::IMember* symbol) {
    auto* method = new Syntax::MethodDeclaration();
    method->Name(name);
    method->AddAnnotation(ResolveToMember(symbol));
    return method;
}

// A method stub whose `AccessorOwner` the transform reads (the shared
// `LookupMethod` hardcodes null / not-an-accessor).
class AccessorMethodStub : public TestSupport::LookupMethod {
public:
    AccessorMethodStub(std::string name, const TS::ICompilation& compiler)
        : LookupMethod(std::move(name), compiler) {}

    void SetAccessorOwner(const TS::IProperty* owner) { owner_ = owner; }
    bool IsAccessor() const override { return owner_ != nullptr; }
    const TS::IMember* AccessorOwner() const override { return owner_; }

private:
    const TS::IProperty* owner_ = nullptr;
};

// A property stub whose accessors and parameters the transform reads (the shared
// `LookupProperty` hardcodes null accessors / empty parameters).
class AccessorPropertyStub : public TestSupport::LookupProperty {
public:
    AccessorPropertyStub(std::string name, TS::ITypePtr type, const TS::ICompilation& compiler)
        : LookupProperty(std::move(name), std::move(type), compiler) {}

    void SetAccessors(const TS::IMethod* getter, const TS::IMethod* setter) {
        getter_ = getter;
        setter_ = setter;
    }
    void SetParameters(std::vector<const TS::IParameter*> parameters) {
        parameters_ = std::move(parameters);
    }
    const TS::IMethod* Getter() const override { return getter_; }
    const TS::IMethod* Setter() const override { return setter_; }
    std::vector<const TS::IParameter*> Parameters() const override { return parameters_; }

private:
    const TS::IMethod* getter_ = nullptr;
    const TS::IMethod* setter_ = nullptr;
    std::vector<const TS::IParameter*> parameters_;
};

// The documentation-comment contents attached as leading trivia to a node, in
// attach order.
std::vector<std::string> CommentContents(const Syntax::AstNode& node) {
    std::vector<std::string> result;
    for (Syntax::Trivia* trivia : node.LeadingTrivia()) {
        if (auto* comment = dynamic_cast<Syntax::Comment*>(trivia))
            result.push_back(comment->Content());
    }
    return result;
}

// The first documentation-comment trivia attached to a node, or null.
Syntax::Comment* FirstComment(const Syntax::AstNode& node) {
    for (Syntax::Trivia* trivia : node.LeadingTrivia()) {
        if (auto* comment = dynamic_cast<Syntax::Comment*>(trivia))
            return comment;
    }
    return nullptr;
}

} // namespace

// The disabled `ShowXmlDocumentation` setting short-circuits the transform.
TEST(AddXmlDocumentationTransformTest, DisabledSettingSkipsDocumentation)
{
    TransformFixture fixture;
    fixture.settings.SetShowXmlDocumentation(false);
    StubDocumentationProvider provider;
    AccessorMethodStub method("M", fixture.compilation);
    provider.docs[&method] = "Docs for M";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("M", &method));
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    EXPECT_TRUE(CommentContents(*typeDecl->Members().At(0)).empty());
}

// No provider on the run short-circuits the transform.
TEST(AddXmlDocumentationTransformTest, NoProviderSkipsDocumentation)
{
    TransformFixture fixture;
    AccessorMethodStub method("M", fixture.compilation);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("M", &method));
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    EXPECT_TRUE(CommentContents(*typeDecl->Members().At(0)).empty());
}

// A single-line doc is inserted as one `" " + line` documentation comment.
TEST(AddXmlDocumentationTransformTest, AddsSingleLineDocumentationComment)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub method("M", fixture.compilation);
    provider.docs[&method] = "Docs for M";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = MakeMethod("M", &method);
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    ASSERT_EQ(CommentContents(*methodDecl).size(), 1u);
    EXPECT_EQ(CommentContents(*methodDecl)[0], " Docs for M");
    Syntax::Comment* comment = FirstComment(*methodDecl);
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->CommentType(), Syntax::CommentType::Documentation);
}

// Leading blank lines are skipped and the first content line's indentation is
// stripped; interior blank lines are restored as empty comments and trailing blank
// lines are dropped.
TEST(AddXmlDocumentationTransformTest, NormalizesIndentationAndBlankLines)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub method("M", fixture.compilation);
    provider.docs[&method] = "\n\n    Line one\n\n    Line two\n\n";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = MakeMethod("M", &method);
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    const std::vector<std::string> comments = CommentContents(*methodDecl);
    ASSERT_EQ(comments.size(), 3u);
    EXPECT_EQ(comments[0], " Line one");
    EXPECT_EQ(comments[1], "");
    EXPECT_EQ(comments[2], " Line two");
}

// A carriage-return line ending is treated as a line break (the `StringReader`
// semantics).
TEST(AddXmlDocumentationTransformTest, HandlesCarriageReturnLineEndings)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub method("M", fixture.compilation);
    provider.docs[&method] = "One\r\nTwo";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = MakeMethod("M", &method);
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    const std::vector<std::string> comments = CommentContents(*methodDecl);
    ASSERT_EQ(comments.size(), 2u);
    EXPECT_EQ(comments[0], " One");
    EXPECT_EQ(comments[1], " Two");
}

// An entity declaration without a symbol is skipped.
TEST(AddXmlDocumentationTransformTest, SkipsEntityWithoutSymbol)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = new Syntax::MethodDeclaration();
    methodDecl->Name("M");
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    EXPECT_TRUE(CommentContents(*methodDecl).empty());
}

// A parameterized property's first accessor (the getter) picks up the property's
// documentation when the accessor itself has none.
TEST(AddXmlDocumentationTransformTest, ParameterizedPropertyGetterGetsPropertyDocumentation)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub getter("get_Item", fixture.compilation);
    AccessorPropertyStub property("Item", std::make_shared<StubType>("ItemType"),
                                  fixture.compilation);
    property.SetAccessors(&getter, nullptr);
    property.SetParameters(
        {new Impl::DefaultParameter(std::make_shared<StubType>("KeyType"), "key")});
    getter.SetAccessorOwner(&property);
    provider.docs[&property] = "The item.";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = MakeMethod("get_Item", &getter);
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    ASSERT_EQ(CommentContents(*methodDecl).size(), 1u);
    EXPECT_EQ(CommentContents(*methodDecl)[0], " The item.");
}

// A setter-only parameterized property's accessor also picks up the property's
// documentation (the `Getter ?? Setter` fallback).
TEST(AddXmlDocumentationTransformTest, ParameterizedPropertySetterGetsPropertyDocumentation)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub setter("set_Item", fixture.compilation);
    AccessorPropertyStub property("Item", std::make_shared<StubType>("ItemType"),
                                  fixture.compilation);
    property.SetAccessors(nullptr, &setter);
    property.SetParameters(
        {new Impl::DefaultParameter(std::make_shared<StubType>("KeyType"), "key")});
    setter.SetAccessorOwner(&property);
    provider.docs[&property] = "The item.";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = MakeMethod("set_Item", &setter);
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    ASSERT_EQ(CommentContents(*methodDecl).size(), 1u);
    EXPECT_EQ(CommentContents(*methodDecl)[0], " The item.");
}

// An accessor that is neither the getter nor the setter does NOT pick up the
// property's documentation.
TEST(AddXmlDocumentationTransformTest, NonFirstAccessorDoesNotGetPropertyDocumentation)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub getter("get_Item", fixture.compilation);
    AccessorMethodStub other("other_Item", fixture.compilation);
    AccessorPropertyStub property("Item", std::make_shared<StubType>("ItemType"),
                                  fixture.compilation);
    property.SetAccessors(&getter, nullptr);
    property.SetParameters(
        {new Impl::DefaultParameter(std::make_shared<StubType>("KeyType"), "key")});
    other.SetAccessorOwner(&property);
    provider.docs[&property] = "The item.";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = MakeMethod("other_Item", &other);
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    EXPECT_TRUE(CommentContents(*methodDecl).empty());
}

// A non-parameterized property owner (an empty parameter list) does not trigger the
// accessor fallback.
TEST(AddXmlDocumentationTransformTest, NonParameterizedPropertyOwnerDoesNotFallback)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub getter("get_Item", fixture.compilation);
    AccessorPropertyStub property("Item", std::make_shared<StubType>("ItemType"),
                                  fixture.compilation);
    property.SetAccessors(&getter, nullptr);
    getter.SetAccessorOwner(&property);
    provider.docs[&property] = "The item.";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = MakeMethod("get_Item", &getter);
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    EXPECT_TRUE(CommentContents(*methodDecl).empty());
}

// The inserted comment carries the declaration's resolve result annotation.
TEST(AddXmlDocumentationTransformTest, CommentCarriesResolveResultAnnotation)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub method("M", fixture.compilation);
    provider.docs[&method] = "Docs for M";
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    auto* methodDecl = MakeMethod("M", &method);
    auto annotation = ResolveToMember(&method);
    methodDecl->RemoveAnnotations<Sem::ResolveResult>();
    methodDecl->AddAnnotation(annotation);
    typeDecl->Members().Add(methodDecl);
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    Syntax::Comment* comment = FirstComment(*methodDecl);
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->Annotation<Sem::ResolveResult>(), annotation.get());
}

// An `XmlException` raised while reading a doc string is reported as a leading
// documentation comment on the root.
TEST(AddXmlDocumentationTransformTest, XmlExceptionAddsLeadingDocumentationComment)
{
    TransformFixture fixture;
    StubDocumentationProvider provider;
    AccessorMethodStub method("M", fixture.compilation);
    provider.docs[&method] = "Docs for M";
    provider.throwXmlException = true;
    fixture.run.SetDocumentationProvider(&provider);
    Transforms::TransformContext context = fixture.MakeContext();

    auto* typeDecl = new Syntax::TypeDeclaration();
    typeDecl->Name("C");
    typeDecl->Members().Add(MakeMethod("M", &method));
    Syntax::SyntaxTree tree;
    tree.Members().Add(typeDecl);

    Transforms::AddXmlDocumentationTransform transform;
    transform.Run(tree, context);

    ASSERT_EQ(CommentContents(tree).size(), 1u);
    EXPECT_EQ(CommentContents(tree)[0],
              " Exception while reading XmlDoc: bad xml");
    Syntax::Comment* comment = FirstComment(tree);
    ASSERT_NE(comment, nullptr);
    EXPECT_EQ(comment->CommentType(), Syntax::CommentType::Documentation);
}
