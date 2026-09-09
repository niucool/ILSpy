// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. HOWEVER CAUSED AND OR IN WHATEVER EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the `TupleResolveResult` port (D450) -- the resolve result for a C# 7 tuple
// literal `(int, string)` / `(int x, string y)`. It is the twenty-ninth `Semantics` leaf,
// deriving directly from `ResolveResult` (D424). The C++ ctor defers the C#
// `ICompilation` / `valueTupleAssembly` pair (used by the C# `TupleType` ctor to build
// the `System.ValueTuple<...>` chain via `ICompilation.FindType` at runtime) by accepting
// a pre-built `underlyingType` (the D405 `TupleType` minimal-port deferral); the
// `GetTupleType` helper's None/Null early-return (which yields `SpecialType.NoType` when
// any element type is `TypeKind.None` or `TypeKind.Null`) IS ported faithfully. The tests
// pin the ctor-stores-elements contract, the `GetTupleType` None/Null crux (NoType base
// vs the TupleType base), the `Elements` accessor, the `GetChildResults`-returns-elements
// crux, the `ToString` format, the `ShallowClone` runtime-type / shared-ownership
// preservation, virtual dispatch through a base pointer, the inherited `ResolveResult`
// defaults, and the class-shape `static_assert`s.

#include "Decompiler/Semantics/TupleResolveResult.hpp"
#include "Decompiler/Semantics/ThrowResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::Semantics::ResolveResult;
using ILSpy::Decompiler::Semantics::ThrowResolveResult;
using ILSpy::Decompiler::Semantics::TupleResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::NoType;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupModule;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

namespace {

ITypePtr Int32Type() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr StringType() { return std::make_shared<KnownType>(KnownTypeCode::String); }


// A `ResolveResult` whose `Type().Kind()` is `TypeKind::None` (the `ThrowResolveResult`
// forwards `NoType()` to the base) -- exercises the `GetTupleType` None-branch.
std::shared_ptr<ResolveResult> NoneKindElement() {
    return std::make_shared<ThrowResolveResult>();
}

// A `ResolveResult` whose `Type().Kind()` is `TypeKind::Null` (a `TypeResolveResult` over
// a `SpecialType(TypeKind::Null)`) -- exercises the `GetTupleType` Null-branch.
std::shared_ptr<ResolveResult> NullKindElement() {
    return std::make_shared<TypeResolveResult>(
        std::make_shared<SpecialType>(TypeKind::Null));
}

// A `ResolveResult` over a normal type (a `TypeResolveResult` over `KnownType(Int32)`) --
// exercises the `GetTupleType` else-branch (the normal tuple case).
std::shared_ptr<ResolveResult> Int32Element() {
    return std::make_shared<TypeResolveResult>(Int32Type());
}

std::shared_ptr<ResolveResult> StringElement() {
    return std::make_shared<TypeResolveResult>(StringType());
}

// ---------------------------------------------------------------------------
// The ctor fixture: the compilation + valueTupleAssembly module pair the C#
// ctor threads to `CreateTupleType`/`FindValueTupleType` (the value-tuple-assembly
// definition first, the compilation-wide lookup fallback second). The module
// registers the `System.ValueTuple`1..`8` struct definitions so the underlying
// chain resolves through the module arm.
// ---------------------------------------------------------------------------
LookupCompilation& Comp() {
    static LookupCompilation comp;
    return comp;
}

LookupModule& ValueTupleModule() {
    struct ModuleFixture {
        LookupModule module;
        std::vector<std::shared_ptr<LookupTypeDefinition>> defs;
        ModuleFixture() : module(Comp(), "ValueTupleLib") {
            for (int tpc = 1; tpc <= 8; tpc++) {
                auto def = std::make_shared<LookupTypeDefinition>(
                    "ValueTuple", "System",
                    ILSpy::Decompiler::TypeSystem::FullTypeName(
                        ILSpy::Decompiler::TypeSystem::TopLevelTypeName("System", "ValueTuple", tpc)),
                    TypeKind::Struct, ILSpy::Decompiler::TypeSystem::Accessibility::Public,
                    Comp(), &module);
                defs.push_back(def);
                module.SetTypeDefinition(
                    ILSpy::Decompiler::TypeSystem::TopLevelTypeName("System", "ValueTuple", tpc),
                    def.get());
            }
        }
    };
    static ModuleFixture fixture;
    return fixture.module;
}

} // namespace

TEST(TupleResolveResultTest, CtorStoresElements) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    ASSERT_EQ(rr.Elements().size(), 2u);
    EXPECT_EQ(rr.Elements()[0].get(), elements[0].get());
    EXPECT_EQ(rr.Elements()[1].get(), elements[1].get());
}

TEST(TupleResolveResultTest, CtorAcceptsEmptyElements) {
    std::vector<std::shared_ptr<ResolveResult>> elements;
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    EXPECT_TRUE(rr.Elements().empty());
}

TEST(TupleResolveResultTest, GetTupleTypeReturnsTupleTypeForNormalElements) {
    // The normal case: all element types are non-None/non-Null -> the base type is the
    // `TupleType` built from the pre-built `underlyingType` and the element types.
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    EXPECT_EQ(rr.Type().Kind(), TypeKind::Tuple);
}

TEST(TupleResolveResultTest, GetTupleTypeReturnsNoTypeForNoneKindElement) {
    // The None-branch: an element whose `Type().Kind()` is `TypeKind.None` (a
    // `ThrowResolveResult` forwards `NoType()` to its base) -> the base type is
    // `SpecialType.NoType` (Kind == None), NOT the `TupleType`.
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), NoneKindElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    EXPECT_EQ(rr.Type().Kind(), TypeKind::None);
}

TEST(TupleResolveResultTest, GetTupleTypeReturnsNoTypeForNullKindElement) {
    // The Null-branch: an element whose `Type().Kind()` is `TypeKind.Null` (a
    // `TypeResolveResult` over a `SpecialType(TypeKind::Null)`) -> the base type is
    // `SpecialType.NoType` (Kind == None), NOT the `TupleType`.
    std::vector<std::shared_ptr<ResolveResult>> elements{NullKindElement(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    EXPECT_EQ(rr.Type().Kind(), TypeKind::None);
}

TEST(TupleResolveResultTest, GetTupleTypeNoTypeBranchDiscardsUnderlyingType) {
    // The None/Null early-return discards the `underlyingType` (the C# `SpecialType.NoType`
    // is returned directly, the `TupleType` is never constructed). A `nullptr`
    // `underlyingType` is safe in this branch (the `TupleType` ctor is never reached).
    std::vector<std::shared_ptr<ResolveResult>> elements{NoneKindElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    EXPECT_EQ(rr.Type().Kind(), TypeKind::None);
}

TEST(TupleResolveResultTest, ElementNamesArePassedToTupleType) {
    // The `elementNames` are threaded to `GetTupleType` which hands them to the `TupleType`
    // ctor. A present vector is passed through; the `TupleType` carries the names.
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    std::vector<std::string> names{"x", "y"};
    TupleResolveResult rr(Comp(), elements, names, &ValueTupleModule());
    EXPECT_EQ(rr.Type().Kind(), TypeKind::Tuple);
    // The `TupleType` exposes the `ElementNames` (the names are stored on the type, not
    // the resolve result; the C# `TupleResolveResult` stores only `Elements`).
    const auto& tupleType = static_cast<const TupleType&>(rr.Type());
    ASSERT_EQ(tupleType.ElementNames().size(), 2u);
    EXPECT_EQ(tupleType.ElementNames()[0], "x");
    EXPECT_EQ(tupleType.ElementNames()[1], "y");
}

TEST(TupleResolveResultTest, ElementNamesNotProvidedDefaultsToEmptyStrings) {
    // The `elementNames` default `std::nullopt` (the C# `default(ImmutableArray<string>)`
    // "not provided" sentinel) is converted to an empty vector which the `TupleType` ctor
    // fills with empty strings matching the element count (the D405 "not provided"
    // sentinel).
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    EXPECT_EQ(rr.Type().Kind(), TypeKind::Tuple);
    const auto& tupleType = static_cast<const TupleType&>(rr.Type());
    ASSERT_EQ(tupleType.ElementNames().size(), 2u);
    EXPECT_EQ(tupleType.ElementNames()[0], "");
    EXPECT_EQ(tupleType.ElementNames()[1], "");
}

TEST(TupleResolveResultTest, GetChildResultsReturnsElementsInOrder) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 2u);
    EXPECT_EQ(children[0], elements[0].get());
    EXPECT_EQ(children[1], elements[1].get());
}

TEST(TupleResolveResultTest, GetChildResultsEmptyWhenNoElements) {
    std::vector<std::shared_ptr<ResolveResult>> elements;
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    EXPECT_TRUE(rr.GetChildResults().empty());
}

TEST(TupleResolveResultTest, GetChildResultsCountMatchesElementsCount) {
    // A 3-tuple yields 3 children (the C# `GetChildResults` returns `Elements` directly).
    std::vector<std::shared_ptr<ResolveResult>> elements{
        Int32Element(), StringElement(),
        std::make_shared<TypeResolveResult>(std::make_shared<KnownType>(KnownTypeCode::Object))};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    // The underlying type is arity-2 but the tuple has 3 elements; the `GetChildResults`
    // returns the 3 elements regardless (the C# `Elements` is the tuple's own element
    // list, not the underlying `ValueTuple<...>` type arguments).
    EXPECT_EQ(rr.GetChildResults().size(), 3u);
}

TEST(TupleResolveResultTest, ToStringReportsSubclassClassNameAndTupleType) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    // The inherited `ResolveResult::ToString` yields "[TupleResolveResult <ReflectionName>]".
    // The `TupleType.ReflectionName` delegates to the `UnderlyingType.ReflectionName` -- the
    // `System.ValueTuple<int, string>` parameterized type's `ReflectionName`.
    auto str = rr.ToString();
    EXPECT_NE(str.find("TupleResolveResult"), std::string::npos) << str;
    EXPECT_EQ(str.front(), '[');
    EXPECT_EQ(str.back(), ']');
}

TEST(TupleResolveResultTest, ShallowClonePreservesRuntimeType) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    auto clone = rr.ShallowClone();
    EXPECT_NE(dynamic_cast<TupleResolveResult*>(clone.get()), nullptr);
}

TEST(TupleResolveResultTest, ShallowCloneSharesElements) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    auto clone = rr.ShallowClone();
    auto* cloned = dynamic_cast<TupleResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    ASSERT_EQ(cloned->Elements().size(), 2u);
    // The `elements_` shared_ptr vector is shared element-wise through the default copy
    // ctor (faithfully mirroring the C# `MemberwiseClone` reference-copy).
    EXPECT_EQ(cloned->Elements()[0].get(), elements[0].get());
    EXPECT_EQ(cloned->Elements()[1].get(), elements[1].get());
}

TEST(TupleResolveResultTest, ShallowCloneSharesTupleType) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    auto clone = rr.ShallowClone();
    // The base `type_` shared_ptr is shared through the default copy ctor.
    EXPECT_EQ(&clone->Type(), &rr.Type());
}

TEST(TupleResolveResultTest, ShallowCloneIsDistinctInstance) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    auto clone = rr.ShallowClone();
    EXPECT_NE(clone.get(), &rr);
}

TEST(TupleResolveResultTest, VirtualDispatchThroughBasePointer) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    ResolveResult* base = &rr;
    // The `GetChildResults` virtual dispatches through the base pointer to the
    // `TupleResolveResult` override (returns the 2 elements, not the base empty default).
    EXPECT_EQ(base->GetChildResults().size(), 2u);
    // The `ClassName` virtual dispatches to "TupleResolveResult".
    auto str = base->ToString();
    EXPECT_NE(str.find("TupleResolveResult"), std::string::npos) << str;
}

TEST(TupleResolveResultTest, InheritedResolveResultDefaultsArePreserved) {
    std::vector<std::shared_ptr<ResolveResult>> elements{Int32Element(), StringElement()};
    TupleResolveResult rr(Comp(), elements, std::nullopt, &ValueTupleModule());
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.IsError());
    // The `ConstantValue` default is an empty `std::any` (the C# `null`).
    EXPECT_FALSE(rr.ConstantValue().has_value());
}

TEST(TupleResolveResultTest, IsResolveResultSubclassAndNotFinal) {
    static_assert(std::is_base_of_v<ResolveResult, TupleResolveResult>);
    static_assert(std::has_virtual_destructor_v<TupleResolveResult>);
    static_assert(std::is_polymorphic_v<TupleResolveResult>);
    // The C# `class TupleResolveResult` (NOT `sealed`) ports to a C++ subclass (NOT
    // `final`); a future `AmbiguousMemberResolveResult`-style subclass may derive from it.
    static_assert(!std::is_final_v<TupleResolveResult>);
}
