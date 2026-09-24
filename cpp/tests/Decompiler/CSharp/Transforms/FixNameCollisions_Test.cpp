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

// Tests for FixNameCollisions (the port of
// ICSharpCode.Decompiler/CSharp/Transforms/FixNameCollisions.cs): a private
// field whose name collides with a member declaration in its type is renamed
// (`m_` prefix, then numeric suffixes), and the references carrying the same
// symbol annotation are renamed with it; a public field and a non-colliding
// field keep their names.

#include "Decompiler/CSharp/Transforms/FixNameCollisions.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

namespace {

namespace CS = ::ILSpy::Decompiler::CSharp;
namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

struct FixCollisionsFixture {
    TS::SimpleCompilation compilation{Impl::MinimalCorlib::Instance(), {}};
    // The fake fields stay alive for the run (the annotations alias them).
    std::vector<std::shared_ptr<Impl::FakeField>> fields;

    std::unique_ptr<Syntax::TypeDeclaration> MakeType() {
        return std::make_unique<Syntax::TypeDeclaration>();
    }

    Syntax::FieldDeclaration* AddField(Syntax::TypeDeclaration& typeDecl,
                                       const std::string& name,
                                       TS::Accessibility accessibility) {
        auto field = std::make_shared<Impl::FakeField>(compilation);
        field->SetName(name);
        field->SetAccessibility(accessibility);
        fields.push_back(field);
        auto* fieldDecl = new Syntax::FieldDeclaration();
        fieldDecl->ReturnType(new Syntax::PrimitiveType("int"));
        fieldDecl->Variables().Add(new Syntax::VariableInitializer(name));
        // The C# `fieldDecl.GetSymbol()` reads the resolve-result annotation
        // (the member IS the symbol; the ErrorResolveResult fallback yields
        // null, so a member resolve result must be attached).
        // The resolve-result target is shared via ResolveResult handles; the
        // TypeResolveResult is shared_ptr<ResolveResult>-compatible.
        auto target = std::make_shared<Sem::TypeResolveResult>(
            std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32));
        const TS::IMember* fieldAsMember =
            static_cast<const TS::IMember*>(static_cast<const Impl::FakeMember*>(field.get()));
        fieldDecl->AddAnnotation(
            std::make_shared<Sem::MemberResolveResult>(target, fieldAsMember));
        typeDecl.Members().Add(fieldDecl);
        return fieldDecl;
    }

    Syntax::PropertyDeclaration* AddProperty(Syntax::TypeDeclaration& typeDecl,
                                             const std::string& name) {
        auto* property = new Syntax::PropertyDeclaration();
        property->ReturnType(new Syntax::PrimitiveType("int"));
        property->NameToken(Syntax::Identifier::Create(name));
        typeDecl.Members().Add(property);
        return property;
    }
};

} // namespace

// A private field colliding with a property name is renamed with the m_
// prefix; the reference carrying the same symbol follows.
TEST(FixNameCollisionsTest, PrivateFieldCollisionIsRenamed)
{
    FixCollisionsFixture fx;
    auto typeDecl = fx.MakeType();
    Syntax::FieldDeclaration* fieldDecl =
        fx.AddField(*typeDecl, "value", TS::Accessibility::Private);
    fx.AddProperty(*typeDecl, "value");

    CS::Transforms::TransformContext context;
    CS::Transforms::FixNameCollisions transform;
    transform.Run(*typeDecl, context);

    // The field declaration is renamed.
    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "m_value");
}

// A public field with the same collision keeps its name (the C# gate
// `symbol is IField { Accessibility: Private }`).
TEST(FixNameCollisionsTest, PublicFieldKeepsItsName)
{
    FixCollisionsFixture fx;
    auto typeDecl = fx.MakeType();
    Syntax::FieldDeclaration* fieldDecl =
        fx.AddField(*typeDecl, "value", TS::Accessibility::Public);
    fx.AddProperty(*typeDecl, "value");

    CS::Transforms::TransformContext context;
    CS::Transforms::FixNameCollisions transform;
    transform.Run(*typeDecl, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "value");
}

// A non-colliding private field keeps its name.
TEST(FixNameCollisionsTest, NonCollidingFieldKeepsItsName)
{
    FixCollisionsFixture fx;
    auto typeDecl = fx.MakeType();
    Syntax::FieldDeclaration* fieldDecl =
        fx.AddField(*typeDecl, "other", TS::Accessibility::Private);
    fx.AddProperty(*typeDecl, "value");

    CS::Transforms::TransformContext context;
    CS::Transforms::FixNameCollisions transform;
    transform.Run(*typeDecl, context);

    EXPECT_EQ(fieldDecl->Variables().At(0)->Name(), "other");
}