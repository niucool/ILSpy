// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the nullability-disambiguation tail of the TypeSystemAstBuilder
// "Convert Type Parameter" region (cpp/Decompiler/CSharp/Syntax/
// TypeSystemAstBuilder.{hpp,cpp}, the port of TypeSystemAstBuilder.cs lines
// 2683-2734: AddNullabilityDisambiguatingConstraints + the
// NullableTypeParameterCollector visitor + GetNullabilityDisambiguator).
//
// The load-bearing cruxes:
//  (a) the collector records a method's own type parameter ONLY where it is
//      visited with a Nullable annotation (NotNullable / Oblivious are ignored)
//      and ONLY when the annotated parameter wraps one of the ctor-captured
//      method parameters (a foreign type parameter that happens to share an
//      index with one of this method's own is ignored -- the specialized-
//      signature shape the C# comment describes);
//  (b) the recording fires from nested positions too (`List<T?>` -- the
//      ParameterizedType children walk reaches the annotated type argument);
//  (c) the AcceptVisitor dispatch is VIEW-INDEPENDENT for an annotated
//      parameter: both IType views (the ITypeParameter path ChangeNullability
//      returns the wrapper through, and the NullabilityAnnotatedType path)
//      route to VisitNullabilityAnnotatedType (the port's diamond would
//      otherwise dispatch the ITypeParameter path to the VisitOtherType
//      default and lose the annotation);
//  (d) the member appends `where T : class` / `where T : default` clauses
//      only for the RECORDED parameters that carry a disambiguator (a
//      value-type-constrained parameter carries none -- Nullable<T> rather
//      than a nullable annotation), reading the annotation from BOTH the
//      return type and every parameter type.
//
// GetNullabilityDisambiguator itself is NOT re-tested here: it was already
// ported as a namespace-scope free function (tested alongside its original
// consumer wiring); this region simply calls it.

#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"

#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"

#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

using ILSpy::Decompiler::CSharp::Syntax::Constraint;
using ILSpy::Decompiler::CSharp::Syntax::MethodDeclaration;
using ILSpy::Decompiler::CSharp::Syntax::PrimitiveType;
using ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedType;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedTypeParameter;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeParameter;

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
using ILSpy::Decompiler::CSharp::Syntax::NullableTypeParameterCollector;

std::shared_ptr<LookupTypeDefinition> MakeDef(const std::string& name,
                                               const std::string& ns,
                                               const ICompilation& compilation) {
    return std::make_shared<LookupTypeDefinition>(
        name, ns, FullTypeName(TopLevelTypeName(ns, name, 0)), TypeKind::Class,
        Accessibility::Public, compilation, nullptr);
}

// A class-constrained type parameter (IsReferenceType == true, the `where T :
// class` shape GetNullabilityDisambiguator maps to "class").
class RefTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;

    std::optional<bool> IsReferenceType() const override { return true; }
};

// A value-type-constrained type parameter (IsReferenceType == false -- the
// disambiguator is null: a value type uses Nullable<T> rather than a nullable
// annotation).
class ValTypeParameter : public LookupTypeParameter {
public:
    using LookupTypeParameter::LookupTypeParameter;

    std::optional<bool> IsReferenceType() const override { return false; }
};

// A fully configurable IParameter stub (the AddCandidate_Test TestParameter
// pattern): only the Type is configured here.
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)) {}

    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    TS::ReferenceKind ReferenceKind() const override { return TS::ReferenceKind::None; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const TS::IParameterizedMember* Owner() const override { return nullptr; }
    TS::LifetimeAnnotation Lifetime() const override { return {}; }

private:
    std::string name_;
    ITypePtr type_;
};

std::shared_ptr<NullabilityAnnotatedTypeParameter> MakeAnnotated(
    const std::shared_ptr<ITypeParameter>& tp, Nullability nullability) {
    return std::make_shared<NullabilityAnnotatedTypeParameter>(tp, nullability);
}

// The wrapper up-cast to IType through the ITypeParameter base -- the path
// ChangeNullability returns it through (the port's diamond makes the direct
// NATP -> IType conversion ambiguous).
ITypePtr AsITypeViaParameter(const std::shared_ptr<NullabilityAnnotatedTypeParameter>& natp) {
    return std::static_pointer_cast<IType>(std::static_pointer_cast<ITypeParameter>(natp));
}

// The wrapper up-cast to IType through the NullabilityAnnotatedType base (the
// other view the diamond offers).
ITypePtr AsITypeViaAnnotation(const std::shared_ptr<NullabilityAnnotatedTypeParameter>& natp) {
    return std::static_pointer_cast<IType>(std::static_pointer_cast<NullabilityAnnotatedType>(natp));
}

struct NullabilityDisambigFixture {
    LookupCompilation compilation;
    TypeSystemAstBuilder builder;

    std::shared_ptr<LookupMethod> MakeMethod(const char* name = "M") const {
        return std::make_shared<LookupMethod>(name, compilation);
    }
};

} // namespace

// ---- NullableTypeParameterCollector (direct) --------------------------------

// A Nullable-annotated occurrence of one of the ctor-captured parameters is
// recorded (pointer identity).
TEST(NullableTypeParameterCollectorTest, RecordsNullableAnnotatedOwnParameter)
{
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    auto natp = MakeAnnotated(t0, Nullability::Nullable);
    NullableTypeParameterCollector collector({t0.get()});

    ITypePtr visited;
    EXPECT_NO_THROW(visited = natp->AcceptVisitor(collector));

    ASSERT_EQ(collector.NullableTypeParameters.size(), 1u);
    EXPECT_EQ(collector.NullableTypeParameters[0], t0.get());
}

// A NotNullable annotation is not a `T?` occurrence -- not recorded.
TEST(NullableTypeParameterCollectorTest, IgnoresNotNullableAnnotation)
{
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    auto natp = MakeAnnotated(t0, Nullability::NotNullable);
    NullableTypeParameterCollector collector({t0.get()});

    natp->AcceptVisitor(collector);

    EXPECT_TRUE(collector.NullableTypeParameters.empty());
}

// An Oblivious annotation is not a `T?` occurrence -- not recorded.
TEST(NullableTypeParameterCollectorTest, IgnoresObliviousAnnotation)
{
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    auto natp = MakeAnnotated(t0, Nullability::Oblivious);
    NullableTypeParameterCollector collector({t0.get()});

    natp->AcceptVisitor(collector);

    EXPECT_TRUE(collector.NullableTypeParameters.empty());
}

// A type parameter of any other owner is ignored: a specialized signature can
// substitute a foreign type parameter that happens to share an index with one
// of this method's own (the C# comment).
TEST(NullableTypeParameterCollectorTest, IgnoresForeignTypeParameter)
{
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    auto foreign = std::make_shared<LookupTypeParameter>("U");
    auto natp = MakeAnnotated(foreign, Nullability::Nullable);
    NullableTypeParameterCollector collector({t0.get()});

    natp->AcceptVisitor(collector);

    EXPECT_TRUE(collector.NullableTypeParameters.empty());
}

// A plain NullabilityAnnotatedType (not the type-parameter subclass) is not an
// annotated parameter -- the dynamic_cast fails and nothing records.
TEST(NullableTypeParameterCollectorTest, IgnoresPlainAnnotatedType)
{
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    auto plain = std::make_shared<NullabilityAnnotatedType>(
        std::static_pointer_cast<IType>(t0), Nullability::Nullable);
    NullableTypeParameterCollector collector({t0.get()});

    plain->AcceptVisitor(collector);

    EXPECT_TRUE(collector.NullableTypeParameters.empty());
}

// Repeated occurrences of the same annotated parameter record once (the C#
// HashSet idempotence under reference equality).
TEST(NullableTypeParameterCollectorTest, DeduplicatesRepeatedOccurrences)
{
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    auto natp = MakeAnnotated(t0, Nullability::Nullable);
    NullableTypeParameterCollector collector({t0.get()});

    natp->AcceptVisitor(collector);
    natp->AcceptVisitor(collector);

    ASSERT_EQ(collector.NullableTypeParameters.size(), 1u);
    EXPECT_EQ(collector.NullableTypeParameters[0], t0.get());
}

// The recording fires from a nested position (`List<T?>`): the
// ParameterizedType children walk reaches the annotated type argument and the
// wrapper records the wrapped method parameter.
TEST(NullableTypeParameterCollectorTest, RecordsNestedInParameterizedType)
{
    NullabilityDisambigFixture fx;
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    auto natp = MakeAnnotated(t0, Nullability::Nullable);
    auto listDef = MakeDef("List", "System.Collections.Generic", fx.compilation);
    auto list = std::make_shared<ParameterizedType>(
        listDef, std::vector<ITypePtr>{AsITypeViaParameter(natp)});
    NullableTypeParameterCollector collector({t0.get()});

    ITypePtr visited;
    EXPECT_NO_THROW(visited = list->AcceptVisitor(collector));

    ASSERT_EQ(collector.NullableTypeParameters.size(), 1u);
    EXPECT_EQ(collector.NullableTypeParameters[0], t0.get());
}

// The AcceptVisitor dispatch is view-independent for an annotated parameter:
// BOTH IType views (the ITypeParameter path ChangeNullability returns the
// wrapper through, and the NullabilityAnnotatedType path) route to
// VisitNullabilityAnnotatedType and record identically.
TEST(NullableTypeParameterCollectorTest, AcceptVisitorIsViewIndependent)
{
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    auto natp = MakeAnnotated(t0, Nullability::Nullable);

    NullableTypeParameterCollector viaParameter({t0.get()});
    AsITypeViaParameter(natp)->AcceptVisitor(viaParameter);
    ASSERT_EQ(viaParameter.NullableTypeParameters.size(), 1u);
    EXPECT_EQ(viaParameter.NullableTypeParameters[0], t0.get());

    NullableTypeParameterCollector viaAnnotation({t0.get()});
    AsITypeViaAnnotation(natp)->AcceptVisitor(viaAnnotation);
    ASSERT_EQ(viaAnnotation.NullableTypeParameters.size(), 1u);
    EXPECT_EQ(viaAnnotation.NullableTypeParameters[0], t0.get());
}

// ---- AddNullabilityDisambiguatingConstraints ----------------------------------

// A method without type parameters adds nothing (the early out).
TEST(AddNullabilityDisambiguatingConstraintsTest, NoTypeParametersAddsNothing)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    method->SetReturnType(AsITypeViaParameter(MakeAnnotated(t0, Nullability::Nullable)));

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    EXPECT_EQ(decl.Constraints().Count(), 0);
}

// A method whose signature never mentions a type parameter with a nullable
// annotation adds nothing (nothing was recorded).
TEST(AddNullabilityDisambiguatingConstraintsTest, UnannotatedSignatureAddsNothing)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    method->SetTypeParameters({t0.get()});
    method->SetReturnType(std::static_pointer_cast<IType>(t0));

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    EXPECT_EQ(decl.Constraints().Count(), 0);
}

// The class-constrained recorded parameter gets `where T : class`.
TEST(AddNullabilityDisambiguatingConstraintsTest, ClassConstrainedAddsClassConstraint)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<RefTypeParameter>("T");
    method->SetTypeParameters({t0.get()});
    method->SetReturnType(AsITypeViaParameter(MakeAnnotated(t0, Nullability::Nullable)));

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    ASSERT_EQ(decl.Constraints().Count(), 1);
    Constraint* c = decl.Constraints().FirstOrNull();
    ASSERT_NE(c, nullptr);
    ASSERT_NE(c->TypeParameter(), nullptr);
    EXPECT_EQ(c->TypeParameter()->Identifier(), std::optional<std::string>("T"));
    ASSERT_EQ(c->BaseTypes().Count(), 1);
    auto* keyword = dynamic_cast<PrimitiveType*>(c->BaseTypes().FirstOrNull());
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "class");
}

// An unconstrained recorded parameter gets `where T : default` (constrained to
// neither a reference type nor a value type).
TEST(AddNullabilityDisambiguatingConstraintsTest, UnconstrainedAddsDefaultConstraint)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<LookupTypeParameter>("T");
    method->SetTypeParameters({t0.get()});
    method->SetReturnType(AsITypeViaParameter(MakeAnnotated(t0, Nullability::Nullable)));

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    ASSERT_EQ(decl.Constraints().Count(), 1);
    Constraint* c = decl.Constraints().FirstOrNull();
    ASSERT_NE(c, nullptr);
    auto* keyword = dynamic_cast<PrimitiveType*>(c->BaseTypes().FirstOrNull());
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "default");
}

// A value-type-constrained parameter carries no disambiguator (it uses
// Nullable<T> rather than a nullable annotation) -- no clause even though the
// parameter was recorded.
TEST(AddNullabilityDisambiguatingConstraintsTest, ValueTypeConstrainedAddsNothing)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<ValTypeParameter>("T");
    method->SetTypeParameters({t0.get()});
    method->SetReturnType(AsITypeViaParameter(MakeAnnotated(t0, Nullability::Nullable)));

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    EXPECT_EQ(decl.Constraints().Count(), 0);
}

// The annotation is read from the PARAMETER types too, not just the return
// type -- a parameter typed `T?` records T.
TEST(AddNullabilityDisambiguatingConstraintsTest, AnnotatedParameterTypePositionRecords)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<RefTypeParameter>("T");
    method->SetTypeParameters({t0.get()});
    // The return type does NOT mention T; only the parameter does.
    method->SetReturnType(std::static_pointer_cast<IType>(
        std::make_shared<LookupTypeParameter>("Unused")));
    auto param = std::make_shared<TestParameter>(
        AsITypeViaParameter(MakeAnnotated(t0, Nullability::Nullable)));
    method->SetParameters({param.get()});

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    ASSERT_EQ(decl.Constraints().Count(), 1);
    Constraint* c = decl.Constraints().FirstOrNull();
    ASSERT_NE(c, nullptr);
    auto* keyword = dynamic_cast<PrimitiveType*>(c->BaseTypes().FirstOrNull());
    ASSERT_NE(keyword, nullptr);
    EXPECT_EQ(keyword->Keyword(), "class");
}

// A nullable annotation over a FOREIGN type parameter records nothing -- no
// clause for the method's own (unannotated) parameter.
TEST(AddNullabilityDisambiguatingConstraintsTest, ForeignAnnotatedTypeAddsNothing)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<RefTypeParameter>("T");
    auto foreign = std::make_shared<RefTypeParameter>("U");
    method->SetTypeParameters({t0.get()});
    method->SetReturnType(AsITypeViaParameter(MakeAnnotated(foreign, Nullability::Nullable)));

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    EXPECT_EQ(decl.Constraints().Count(), 0);
}

// Both annotated parameters get their clauses, in TypeParameters order.
TEST(AddNullabilityDisambiguatingConstraintsTest, MultipleAnnotatedParametersAddInOrder)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<RefTypeParameter>("T");
    auto u0 = std::make_shared<LookupTypeParameter>("U");
    method->SetTypeParameters({t0.get(), u0.get()});
    method->SetReturnType(AsITypeViaParameter(MakeAnnotated(t0, Nullability::Nullable)));
    auto param = std::make_shared<TestParameter>(
        AsITypeViaParameter(MakeAnnotated(u0, Nullability::Nullable)));
    method->SetParameters({param.get()});

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    ASSERT_EQ(decl.Constraints().Count(), 2);
    Constraint* first = decl.Constraints()[0];
    ASSERT_NE(first, nullptr);
    ASSERT_NE(first->TypeParameter(), nullptr);
    EXPECT_EQ(first->TypeParameter()->Identifier(), std::optional<std::string>("T"));
    auto* firstKeyword = dynamic_cast<PrimitiveType*>(first->BaseTypes().FirstOrNull());
    ASSERT_NE(firstKeyword, nullptr);
    EXPECT_EQ(firstKeyword->Keyword(), "class");

    Constraint* second = decl.Constraints()[1];
    ASSERT_NE(second, nullptr);
    ASSERT_NE(second->TypeParameter(), nullptr);
    EXPECT_EQ(second->TypeParameter()->Identifier(), std::optional<std::string>("U"));
    auto* secondKeyword = dynamic_cast<PrimitiveType*>(second->BaseTypes().FirstOrNull());
    ASSERT_NE(secondKeyword, nullptr);
    EXPECT_EQ(secondKeyword->Keyword(), "default");
}

// The per-parameter gate: only the RECORDED parameter gets a clause -- a
// class-constrained but unannotated sibling is skipped.
TEST(AddNullabilityDisambiguatingConstraintsTest, PerParameterRecordGate)
{
    NullabilityDisambigFixture fx;
    auto method = fx.MakeMethod();
    auto t0 = std::make_shared<RefTypeParameter>("T");
    auto u0 = std::make_shared<RefTypeParameter>("U");
    method->SetTypeParameters({t0.get(), u0.get()});
    method->SetReturnType(AsITypeViaParameter(MakeAnnotated(t0, Nullability::Nullable)));

    MethodDeclaration decl;
    fx.builder.AddNullabilityDisambiguatingConstraints(decl, *method);

    ASSERT_EQ(decl.Constraints().Count(), 1);
    Constraint* c = decl.Constraints().FirstOrNull();
    ASSERT_NE(c, nullptr);
    ASSERT_NE(c->TypeParameter(), nullptr);
    EXPECT_EQ(c->TypeParameter()->Identifier(), std::optional<std::string>("T"));
}
