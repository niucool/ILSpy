// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the context-shaped TransformDisplayClassUsage::IsPotentialClosure
// (the leaf landed for the TransformCollectionAndObjectInitializers statement
// fold): the newobj's declaring type is a display class of the type the ROOT
// function's method declares.

#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/TransformDisplayClassUsage.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::TestSupport::LookupMethod;
using TS::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::IL::Call;
using ILSpy::Decompiler::IL::ILFunction;

namespace {

// A minimal compilation over the minimal corlib the type stubs resolve through.
struct DisplayClassFixture {
    TS::SimpleCompilation compilation{TS::Implementation::MinimalCorlib::Instance(), {}};
    std::shared_ptr<LookupTypeDefinition> decompiledType;
    std::shared_ptr<LookupTypeDefinition> displayStruct;

    DisplayClassFixture()
    {
        // The type the root function's method is declared in.
        decompiledType = std::make_shared<LookupTypeDefinition>(
            "Outer", "Ns", TS::FullTypeName("Ns.Outer"), TS::TypeKind::Class,
            TS::Accessibility::Public, compilation, nullptr);
        // A compiler-generated display struct nested inside it (the classic
        // `<>c__DisplayClass0` shape).
        displayStruct = std::make_shared<LookupTypeDefinition>(
            "<>c__DisplayClass0", "Ns", TS::FullTypeName("Ns.Outer+<>c__DisplayClass0"),
            TS::TypeKind::Struct, TS::Accessibility::Private, compilation, nullptr);
        displayStruct->SetDeclaringTypeDefinition(decompiledType.get());
        displayStruct->SetKnownAttributes({TS::KnownAttribute::CompilerGenerated});
    }

    std::shared_ptr<LookupMethod> MakeMethod(const TS::ITypeDefinition* declaringType) {
        auto method = std::make_shared<LookupMethod>("M", compilation);
        method->SetDeclaringTypeDefinition(declaringType);
        return method;
    }

    std::unique_ptr<ILFunction> MakeRootFunction(const TS::IMethod* method) {
        auto fn = std::make_unique<ILFunction>();
        fn->Method = const_cast<TS::IMethod*>(method);
        return fn;
    }

    std::unique_ptr<Call> MakeNewObj(const TS::IMethod* ctor) {
        auto call = std::make_unique<Call>(".ctor");
        call->IsNewObj = true;
        call->Method = std::const_pointer_cast<TS::IMethod>(
            std::shared_ptr<const TS::IMethod>(ctor, [](const TS::IMethod*) {}));
        return call;
    }
};

} // namespace

TEST(TransformDisplayClassUsageTest, IsPotentialClosureAcceptsNestedDisplayStruct)
{
    DisplayClassFixture fixture;
    auto ctor = fixture.MakeMethod(fixture.displayStruct.get());
    auto rootMethod = fixture.MakeMethod(fixture.decompiledType.get());
    auto fn = fixture.MakeRootFunction(rootMethod.get());
    auto newobj = fixture.MakeNewObj(ctor.get());
    // The root function's method declares Outer; the newobj constructs a
    // compiler-generated struct nested inside Outer -- the display-class shape.
    EXPECT_TRUE(IsPotentialClosure(fn.get(), newobj.get()));
}

TEST(TransformDisplayClassUsageTest, IsPotentialClosureRejectsNonCompilerGenerated)
{
    DisplayClassFixture fixture;
    // Same shape without the [CompilerGenerated] attribute.
    auto plain = std::make_shared<LookupTypeDefinition>(
        "PlainStruct", "Ns", TS::FullTypeName("Ns.Outer+PlainStruct"),
        TS::TypeKind::Struct, TS::Accessibility::Private, fixture.compilation, nullptr);
    plain->SetDeclaringTypeDefinition(fixture.decompiledType.get());
    auto ctor = fixture.MakeMethod(plain.get());
    auto rootMethod = fixture.MakeMethod(fixture.decompiledType.get());
    auto fn = fixture.MakeRootFunction(rootMethod.get());
    auto newobj = fixture.MakeNewObj(ctor.get());
    EXPECT_FALSE(IsPotentialClosure(fn.get(), newobj.get()));
}

TEST(TransformDisplayClassUsageTest, IsPotentialClosureRejectsForeignNestingTree)
{
    DisplayClassFixture fixture;
    // A compiler-generated display struct nested in a DIFFERENT type: neither
    // the decompiled type nor any of its ancestors is an ancestor of it.
    auto other = std::make_shared<LookupTypeDefinition>(
        "Other", "Ns", TS::FullTypeName("Ns.Other"), TS::TypeKind::Class,
        TS::Accessibility::Public, fixture.compilation, nullptr);
    auto foreign = std::make_shared<LookupTypeDefinition>(
        "<>c__DisplayClass9", "Ns", TS::FullTypeName("Ns.Other+<>c__DisplayClass9"),
        TS::TypeKind::Struct, TS::Accessibility::Private, fixture.compilation, nullptr);
    foreign->SetDeclaringTypeDefinition(other.get());
    foreign->SetKnownAttributes({TS::KnownAttribute::CompilerGenerated});
    auto ctor = fixture.MakeMethod(foreign.get());
    auto rootMethod = fixture.MakeMethod(fixture.decompiledType.get());
    auto fn = fixture.MakeRootFunction(rootMethod.get());
    auto newobj = fixture.MakeNewObj(ctor.get());
    EXPECT_FALSE(IsPotentialClosure(fn.get(), newobj.get()));
}

TEST(TransformDisplayClassUsageTest, IsPotentialClosureAcceptsSharedAncestor)
{
    DisplayClassFixture fixture;
    // The decompiled method is declared in a SIBLING type: Outer and Inner
    // share the Root ancestor, and the display class is nested in Inner.
    auto root = std::make_shared<LookupTypeDefinition>(
        "Root", "Ns", TS::FullTypeName("Ns.Root"), TS::TypeKind::Class,
        TS::Accessibility::Public, fixture.compilation, nullptr);
    fixture.decompiledType->SetDeclaringTypeDefinition(root.get());
    auto inner = std::make_shared<LookupTypeDefinition>(
        "Inner", "Ns", TS::FullTypeName("Ns.Root+Inner"), TS::TypeKind::Class,
        TS::Accessibility::Public, fixture.compilation, nullptr);
    inner->SetDeclaringTypeDefinition(root.get());
    fixture.displayStruct->SetDeclaringTypeDefinition(inner.get());
    auto ctor = fixture.MakeMethod(fixture.displayStruct.get());
    auto rootMethod = fixture.MakeMethod(fixture.decompiledType.get());
    auto fn = fixture.MakeRootFunction(rootMethod.get());
    auto newobj = fixture.MakeNewObj(ctor.get());
    EXPECT_TRUE(IsPotentialClosure(fn.get(), newobj.get()));
}

TEST(TransformDisplayClassUsageTest, IsPotentialClosureRejectsDisplayClassWithInterfaces)
{
    DisplayClassFixture fixture;
    // A compiler-generated CLASS display class with a non-object direct base
    // type is not a closure (display classes extend nothing but object).
    auto displayClass = std::make_shared<LookupTypeDefinition>(
        "<>c", "Ns", TS::FullTypeName("Ns.Outer+<>c"), TS::TypeKind::Class,
        TS::Accessibility::Private, fixture.compilation, nullptr);
    displayClass->SetDeclaringTypeDefinition(fixture.decompiledType.get());
    displayClass->SetKnownAttributes({TS::KnownAttribute::CompilerGenerated});
    displayClass->AddDirectBaseType(
        std::const_pointer_cast<TS::IType>(
            fixture.compilation.FindType(TS::KnownTypeCode::Task).shared_from_this()));
    auto ctor = fixture.MakeMethod(displayClass.get());
    auto rootMethod = fixture.MakeMethod(fixture.decompiledType.get());
    auto fn = fixture.MakeRootFunction(rootMethod.get());
    auto newobj = fixture.MakeNewObj(ctor.get());
    EXPECT_FALSE(IsPotentialClosure(fn.get(), newobj.get()));
}

TEST(TransformDisplayClassUsageTest, IsPotentialClosureNullMethodAndFunctionShapes)
{
    DisplayClassFixture fixture;
    auto ctor = fixture.MakeMethod(fixture.displayStruct.get());
    auto newobj = fixture.MakeNewObj(ctor.get());
    auto rootMethod = fixture.MakeMethod(fixture.decompiledType.get());

    // A root function without a resolved method: the C# `Method!` dereference
    // would NRE; the port maps it to a null current type (false).
    auto fnNoMethod = fixture.MakeRootFunction(nullptr);
    EXPECT_FALSE(IsPotentialClosure(fnNoMethod.get(), newobj.get()));

    // A newobj without a resolved method: a null display class is false.
    auto fn = fixture.MakeRootFunction(rootMethod.get());
    auto unresolved = std::make_unique<Call>(".ctor");
    unresolved->IsNewObj = true;
    EXPECT_FALSE(IsPotentialClosure(fn.get(), unresolved.get()));

    // A null root function is false.
    EXPECT_FALSE(IsPotentialClosure(nullptr, newobj.get()));

    // A null newobj is false.
    EXPECT_FALSE(IsPotentialClosure(fn.get(), nullptr));
}
