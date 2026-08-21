// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. AND NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `UnknownMemberResolveResult` (cpp/Decompiler/Semantics/UnknownMemberResolveResult.hpp,
// the D447 port of ICSharpCode.Decompiler/Semantics/UnknownMemberResolveResult.cs) -- the
// three `ResolveResult` subclasses representing an unresolvable member / method /
// identifier. The C# file declares:
//   * `UnknownMemberResolveResult : ResolveResult` -- an unknown member access on a
//     known target type (carries the target type, member name, type arguments); the
//     `IsError` override is `true`.
//   * `UnknownMethodResolveResult : UnknownMemberResolveResult` -- an unknown method
//     call (adds the parameter list); inherits the `IsError => true`.
//   * `UnknownIdentifierResolveResult : ResolveResult` -- an unknown identifier
//     reference (carries the identifier string and type-argument count); the `IsError`
//     override is `true`.
//
// The tests pin the ctor-stores-all-fields shape for each class, the `IsError => true`
// unconditional crux (direct + virtual dispatch through the base pointer layers -- the
// load-bearing override distinguishing an unresolvable name from the base `false`
// default), the `TargetType`-distinct-from-base-`Type` crux (the base forwards the
// `UnknownType` null object, while `TargetType` is the configured type), the inherited
// `IsError` for `UnknownMethodResolveResult` (it does NOT override `IsError`, it
// inherits the base `true`), the custom `ToString` formats (the
// `"[<class-name> <targetType>.<memberName>]"` / `"[<class-name> <identifier>]"` shapes
// the C# source overrides directly), the polymorphic `ClassName()` dispatch through a
// base pointer (the inherited `ToString` for `UnknownMethodResolveResult` reports the
// subclass name), the `ShallowClone` runtime-type preservation (not sliced) plus
// shared-ownership/value-copy clone plus distinct-instance, the inherited
// `ResolveResult` defaults the subclasses do NOT override, and the `is_base_of` /
// `has_virtual_destructor` / `is_polymorphic` / not-`final` (the C# classes are
// unsealed) class-shape static-asserts.

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A minimal concrete `IParameter` for testing the `UnknownMethodResolveResult`
// parameter snapshot (only `Name` is exercised in the assertions, the rest return
// simple defaults so the stub compiles). IDENTICAL in shape to the `TestParameter`
// in `InvocationResolveResult_Test.cpp` / `IParameterizedMember_Test.cpp`.
class TestParameter : public ILSpy::Decompiler::TypeSystem::IParameter {
public:
    TestParameter(ILSpy::Decompiler::TypeSystem::SymbolKind kind, std::string name,
                  ILSpy::Decompiler::TypeSystem::ITypePtr type)
        : kind_(kind), name_(std::move(name)), type_(std::move(type)) {}
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override { return {}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return ILSpy::Decompiler::TypeSystem::ReferenceKind::None; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override { return ILSpy::Decompiler::TypeSystem::LifetimeAnnotation{}; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
private:
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    std::string name_;
    ILSpy::Decompiler::TypeSystem::ITypePtr type_;
};

// Convenience: a `KnownType(Object)` (a reference type) for use as the target type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

} // namespace

// ===========================================================================
// UnknownMemberResolveResult : ResolveResult
// ===========================================================================

TEST(UnknownMemberResolveResultTest, ConstructorStoresAllFields)
{
    // The C# ctor forwards the `UnknownType` null object to the base and stores the
    // target type, member name, and type arguments. The base `Type()` is the
    // `UnknownType` null object (NOT the configured target type), while `TargetType`
    // is the configured type -- the load-bearing distinction the C# source carries
    // (the base `this.Type` is `SpecialType.UnknownType`).
    auto targetType = MakeObjectType();
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArgs = { MakeObjectType() };
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(
        targetType, "Foo", typeArgs);
    EXPECT_EQ(&umrr.TargetType(), targetType.get());
    EXPECT_EQ(umrr.MemberName(), "Foo");
    ASSERT_EQ(umrr.TypeArguments().size(), 1u);
    EXPECT_EQ(umrr.TypeArguments()[0].get(), typeArgs[0].get());
}

TEST(UnknownMemberResolveResultTest, TargetTypeIsDistinctFromBaseType)
{
    // The crux: the C# `base(SpecialType.UnknownType)` forwards the `UnknownType`
    // null object to the `ResolveResult` base, so the inherited `Type()` returns
    // `UnknownType` (Kind `Unknown`), while `TargetType` returns the configured
    // target type (Kind `Class` for `KnownType(Object)`). The two are DISTINCT --
    // a consumer that reads `Type()` (the resolution result type) gets the
    // unknown-type sentinel, while a consumer that reads `TargetType()` (the type
    // the member was looked up on) gets the real target.
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    EXPECT_EQ(umrr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Unknown);
    EXPECT_EQ(umrr.TargetType().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Class);
}

TEST(UnknownMemberResolveResultTest, MemberNameReturnsConfiguredName)
{
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Bar", {});
    EXPECT_EQ(umrr.MemberName(), "Bar");
}

TEST(UnknownMemberResolveResultTest, TypeArgumentsReturnsEmptySnapshotByDefault)
{
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    EXPECT_TRUE(umrr.TypeArguments().empty());
}

TEST(UnknownMemberResolveResultTest, TypeArgumentsReturnsConfiguredSnapshot)
{
    auto targetType = MakeObjectType();
    auto arg1 = MakeObjectType();
    auto arg2 = MakeObjectType();
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArgs = { arg1, arg2 };
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", typeArgs);
    auto& stored = umrr.TypeArguments();
    ASSERT_EQ(stored.size(), 2u);
    EXPECT_EQ(stored[0].get(), arg1.get());
    EXPECT_EQ(stored[1].get(), arg2.get());
}

TEST(UnknownMemberResolveResultTest, IsErrorIsAlwaysTrueDirect)
{
    // The C# `public override bool IsError => true` -- the load-bearing crux: an
    // unknown member access is always an error, unconditionally (the base default
    // is `false`).
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    EXPECT_TRUE(umrr.IsError());
}

TEST(UnknownMemberResolveResultTest, IsErrorIsAlwaysTrueThroughResolveResultBasePointer)
{
    // The `IsError` override dispatches through a `ResolveResult*` base pointer
    // (the virtual dispatch the resolver relies on).
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    ILSpy::Decompiler::Semantics::ResolveResult* base = &umrr;
    EXPECT_TRUE(base->IsError());
}

TEST(UnknownMemberResolveResultTest, InheritedDefaultsArePreserved)
{
    // UnknownMemberResolveResult does NOT override IsCompileTimeConstant /
    // ConstantValue / GetChildResults, so the inherited ResolveResult base defaults
    // hold (an unknown member access is not a compile-time constant; it has no
    // child results).
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    EXPECT_FALSE(umrr.IsCompileTimeConstant());
    EXPECT_FALSE(umrr.ConstantValue().has_value());
    EXPECT_TRUE(umrr.GetChildResults().empty());
}

TEST(UnknownMemberResolveResultTest, ToStringReportsCustomFormat)
{
    // The C# `ToString` is overridden directly: `"[<class-name> <targetType>.<memberName>]"`
    // using `targetType.ToString()` (the `AbstractType.ToString` -> `ReflectionName`
    // convention). For `KnownType(Object)` the `ReflectionName` is "System.Object"
    // (Namespace + '.' + Name), so the format is
    // "[UnknownMemberResolveResult System.Object.Foo]".
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    EXPECT_EQ(umrr.ToString(), "[UnknownMemberResolveResult System.Object.Foo]");
}

TEST(UnknownMemberResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The `ClassName()` override dispatches through a `ResolveResult*` base pointer
    // (the virtual dispatch the C# resolver relies on in `ToString` via
    // `GetType().Name`): the base pointer's `ToString` reports the subclass name.
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    ILSpy::Decompiler::Semantics::ResolveResult* base = &umrr;
    EXPECT_EQ(base->ToString(), "[UnknownMemberResolveResult System.Object.Foo]");
}

TEST(UnknownMemberResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the runtime
    // type, so a cloned `UnknownMemberResolveResult` stays an `UnknownMemberResolveResult`
    // (not sliced to the `ResolveResult` base). The C++ override reproduces this: the
    // clone is an `UnknownMemberResolveResult` (`dynamic_cast` succeeds).
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    auto clone = umrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult*>(clone.get()),
              nullptr);
}

TEST(UnknownMemberResolveResultTest, ShallowCloneSharesTargetTypeAndTypeArguments)
{
    // The clone shares the `targetType` `shared_ptr` and each `typeArguments` element
    // (faithful to `MemberwiseClone`'s reference copy): the same `IType` objects back
    // both the original and the clone.
    auto targetType = MakeObjectType();
    auto arg = MakeObjectType();
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArgs = { arg };
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", typeArgs);
    auto clone = umrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* down = dynamic_cast<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult*>(clone.get());
    ASSERT_NE(down, nullptr);
    EXPECT_EQ(&down->TargetType(), &umrr.TargetType());
    ASSERT_EQ(down->TypeArguments().size(), 1u);
    EXPECT_EQ(down->TypeArguments()[0].get(), arg.get());
}

TEST(UnknownMemberResolveResultTest, ShallowCloneCopiesMemberName)
{
    // The clone value-copies the `memberName` `std::string` (faithful to
    // `MemberwiseClone`'s value copy of a `string` field): the clone reports the
    // same member name.
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    auto clone = umrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* down = dynamic_cast<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult*>(clone.get());
    ASSERT_NE(down, nullptr);
    EXPECT_EQ(down->MemberName(), "Foo");
    EXPECT_TRUE(clone->IsError());
}

TEST(UnknownMemberResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult umrr(targetType, "Foo", {});
    auto clone = umrr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &umrr);
}

TEST(UnknownMemberResolveResultTest, ClassShapeStaticAsserts)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult>,
                  "UnknownMemberResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult>,
                  "UnknownMemberResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult>,
                  "UnknownMemberResolveResult is polymorphic (the IsError/ToString/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `UnknownMemberResolveResult` is NOT sealed (the C# class is unsealed,
    // and `UnknownMethodResolveResult` derives from it), so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult>,
                  "UnknownMemberResolveResult is not final (the C# class is unsealed).");
}

// ===========================================================================
// UnknownMethodResolveResult : UnknownMemberResolveResult
// ===========================================================================

namespace {
// A helper that builds an `UnknownMethodResolveResult` over a target type `Object`,
// method name `Add`, one type argument, and two parameters. Models the unknown
// method-call shape the C# `UnknownMethodResolveResult` represents.
struct UnknownMethodFixture {
    ILSpy::Decompiler::TypeSystem::ITypePtr targetType;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArgs;
    TestParameter param1;
    TestParameter param2;
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> parameters;
    std::unique_ptr<ILSpy::Decompiler::Semantics::UnknownMethodResolveResult> rr;

    UnknownMethodFixture()
        : targetType(MakeObjectType()),
          typeArgs({ MakeObjectType() }),
          param1(ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter, "x", MakeObjectType()),
          param2(ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter, "y", MakeObjectType()),
          parameters({ &param1, &param2 }),
          rr(std::make_unique<ILSpy::Decompiler::Semantics::UnknownMethodResolveResult>(
              targetType, "Add", typeArgs, parameters)) {}
};
} // namespace

TEST(UnknownMethodResolveResultTest, ConstructorForwardsToBase)
{
    // The C# ctor forwards the target type, method name, and type arguments to the
    // `UnknownMemberResolveResult` base ctor. The inherited `TargetType` /
    // `MemberName` / `TypeArguments` accessors return the forwarded values.
    UnknownMethodFixture f;
    ASSERT_NE(f.rr, nullptr);
    EXPECT_EQ(&f.rr->TargetType(), f.targetType.get());
    EXPECT_EQ(f.rr->MemberName(), "Add");
    ASSERT_EQ(f.rr->TypeArguments().size(), 1u);
    EXPECT_EQ(f.rr->TypeArguments()[0].get(), f.typeArgs[0].get());
}

TEST(UnknownMethodResolveResultTest, ParametersReturnsConfiguredSnapshot)
{
    // The C# `ReadOnlyCollection<IParameter> Parameters` (a snapshot of the ctor's
    // `IEnumerable<IParameter>`) ports to a non-owning pointer vector; the accessor
    // returns the configured parameters in order.
    UnknownMethodFixture f;
    auto& params = f.rr->Parameters();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0], &f.param1);
    EXPECT_EQ(params[1], &f.param2);
}

TEST(UnknownMethodResolveResultTest, ParametersIsEmptyByDefault)
{
    // An empty parameter list is a faithful `ReadOnlyCollection` snapshot (the C#
    // `parameters.ToArray()` yields an empty array).
    auto targetType = MakeObjectType();
    ILSpy::Decompiler::Semantics::UnknownMethodResolveResult umrr(
        targetType, "Foo", {}, {});
    EXPECT_TRUE(umrr.Parameters().empty());
}

TEST(UnknownMethodResolveResultTest, IsErrorIsInheritedTrueDirect)
{
    // `UnknownMethodResolveResult` does NOT override `IsError` -- it inherits the
    // base `UnknownMemberResolveResult::IsError => true`. An unknown method call is
    // always an error (the inherited unconditional override), pinned here at the
    // direct call.
    UnknownMethodFixture f;
    EXPECT_TRUE(f.rr->IsError());
}

TEST(UnknownMethodResolveResultTest, IsErrorIsInheritedTrueThroughUnknownMemberResolveResultBasePointer)
{
    // The inherited `IsError` dispatches through an `UnknownMemberResolveResult*`
    // base pointer (the one-level-up virtual dispatch).
    UnknownMethodFixture f;
    ILSpy::Decompiler::Semantics::UnknownMemberResolveResult* base = f.rr.get();
    EXPECT_TRUE(base->IsError());
}

TEST(UnknownMethodResolveResultTest, IsErrorIsInheritedTrueThroughResolveResultBasePointer)
{
    // The inherited `IsError` dispatches through the `ResolveResult*` root base
    // pointer too (the two-level-up virtual dispatch).
    UnknownMethodFixture f;
    ILSpy::Decompiler::Semantics::ResolveResult* base = f.rr.get();
    EXPECT_TRUE(base->IsError());
}

TEST(UnknownMethodResolveResultTest, InheritedDefaultsArePreserved)
{
    // `UnknownMethodResolveResult` does NOT override `IsCompileTimeConstant` /
    // `ConstantValue` / `GetChildResults`, so the inherited `ResolveResult` base
    // defaults hold.
    UnknownMethodFixture f;
    EXPECT_FALSE(f.rr->IsCompileTimeConstant());
    EXPECT_FALSE(f.rr->ConstantValue().has_value());
    EXPECT_TRUE(f.rr->GetChildResults().empty());
}

TEST(UnknownMethodResolveResultTest, ToStringReportsSubclassClassName)
{
    // The C# `ToString` is INHERITED from `UnknownMemberResolveResult` (the custom
    // `"[<class-name> <targetType>.<memberName>]"` format), but uses `GetType().Name`
    // which is polymorphic and yields "UnknownMethodResolveResult". The C++ port
    // reproduces this via the `ClassName()` override so the inherited `ToString`
    // reports the subclass name (not the base "UnknownMemberResolveResult").
    UnknownMethodFixture f;
    EXPECT_EQ(f.rr->ToString(), "[UnknownMethodResolveResult System.Object.Add]");
}

TEST(UnknownMethodResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The `ClassName()` override dispatches through a `ResolveResult*` base pointer
    // (the virtual dispatch the C# resolver relies on in `ToString` via
    // `GetType().Name`): the base pointer's `ToString` reports the subclass name.
    UnknownMethodFixture f;
    ILSpy::Decompiler::Semantics::ResolveResult* base = f.rr.get();
    EXPECT_EQ(base->ToString(), "[UnknownMethodResolveResult System.Object.Add]");
}

TEST(UnknownMethodResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the runtime
    // type, so a cloned `UnknownMethodResolveResult` stays an `UnknownMethodResolveResult`
    // (not sliced to the `UnknownMemberResolveResult` or `ResolveResult` base). The
    // C++ override reproduces this: the clone is an `UnknownMethodResolveResult`
    // (`dynamic_cast` succeeds).
    UnknownMethodFixture f;
    auto clone = f.rr->ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::UnknownMethodResolveResult*>(clone.get()),
              nullptr);
}

TEST(UnknownMethodResolveResultTest, ShallowCloneSharesParameters)
{
    // The clone copies the `parameters_` non-owning pointer vector (faithful to
    // `MemberwiseClone`'s value copy of the `ReadOnlyCollection` reference -- the
    // same `IParameter` objects back both the original and the clone).
    UnknownMethodFixture f;
    auto clone = f.rr->ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* down = dynamic_cast<ILSpy::Decompiler::Semantics::UnknownMethodResolveResult*>(clone.get());
    ASSERT_NE(down, nullptr);
    auto& params = down->Parameters();
    ASSERT_EQ(params.size(), 2u);
    EXPECT_EQ(params[0], &f.param1);
    EXPECT_EQ(params[1], &f.param2);
    EXPECT_TRUE(clone->IsError());
}

TEST(UnknownMethodResolveResultTest, ShallowCloneIsDistinctInstance)
{
    UnknownMethodFixture f;
    auto clone = f.rr->ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), f.rr.get());
}

TEST(UnknownMethodResolveResultTest, ClassShapeStaticAsserts)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::UnknownMemberResolveResult,
                                    ILSpy::Decompiler::Semantics::UnknownMethodResolveResult>,
                  "UnknownMethodResolveResult derives from UnknownMemberResolveResult.");
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::UnknownMethodResolveResult>,
                  "UnknownMethodResolveResult derives from ResolveResult (via UnknownMemberResolveResult).");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::UnknownMethodResolveResult>,
                  "UnknownMethodResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::UnknownMethodResolveResult>,
                  "UnknownMethodResolveResult is polymorphic (the ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `UnknownMethodResolveResult` is NOT sealed (the C# class is unsealed),
    // so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::UnknownMethodResolveResult>,
                  "UnknownMethodResolveResult is not final (the C# class is unsealed).");
}

// ===========================================================================
// UnknownIdentifierResolveResult : ResolveResult
// ===========================================================================

TEST(UnknownIdentifierResolveResultTest, ConstructorStoresIdentifierAndCount)
{
    // The C# ctor forwards the `UnknownType` null object to the base and stores the
    // identifier and type-argument count. The base `Type()` is the `UnknownType`
    // null object.
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 2);
    EXPECT_EQ(uirr.Identifier(), "Foo");
    EXPECT_EQ(uirr.TypeArgumentCount(), 2);
}

TEST(UnknownIdentifierResolveResultTest, ConstructorDefaultsTypeArgumentCountToZero)
{
    // The C# ctor's `int typeArgumentCount = 0` default ports to a C++ default arg
    // (the D426 defaulted-ctor-arg convention): the one-arg form `Foo` yields
    // `TypeArgumentCount == 0`.
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo");
    EXPECT_EQ(uirr.Identifier(), "Foo");
    EXPECT_EQ(uirr.TypeArgumentCount(), 0);
}

TEST(UnknownIdentifierResolveResultTest, IdentifierReturnsConfigured)
{
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Bar", 0);
    EXPECT_EQ(uirr.Identifier(), "Bar");
}

TEST(UnknownIdentifierResolveResultTest, TypeArgumentCountReturnsConfigured)
{
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Bar", 3);
    EXPECT_EQ(uirr.TypeArgumentCount(), 3);
}

TEST(UnknownIdentifierResolveResultTest, IsErrorIsAlwaysTrueDirect)
{
    // The C# `public override bool IsError => true` -- the load-bearing crux: an
    // unknown identifier is always an error, unconditionally (the base default
    // is `false`).
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 0);
    EXPECT_TRUE(uirr.IsError());
}

TEST(UnknownIdentifierResolveResultTest, IsErrorIsAlwaysTrueThroughResolveResultBasePointer)
{
    // The `IsError` override dispatches through a `ResolveResult*` base pointer
    // (the virtual dispatch the resolver relies on).
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 0);
    ILSpy::Decompiler::Semantics::ResolveResult* base = &uirr;
    EXPECT_TRUE(base->IsError());
}

TEST(UnknownIdentifierResolveResultTest, BaseTypeIsUnknownType)
{
    // The C# `base(SpecialType.UnknownType)` forwards the `UnknownType` null object
    // to the `ResolveResult` base, so the inherited `Type()` returns `UnknownType`
    // (Kind `Unknown`).
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 0);
    EXPECT_EQ(uirr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Unknown);
}

TEST(UnknownIdentifierResolveResultTest, InheritedDefaultsArePreserved)
{
    // `UnknownIdentifierResolveResult` does NOT override `IsCompileTimeConstant` /
    // `ConstantValue` / `GetChildResults`, so the inherited `ResolveResult` base
    // defaults hold (an unknown identifier is not a compile-time constant; it has
    // no child results).
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 0);
    EXPECT_FALSE(uirr.IsCompileTimeConstant());
    EXPECT_FALSE(uirr.ConstantValue().has_value());
    EXPECT_TRUE(uirr.GetChildResults().empty());
}

TEST(UnknownIdentifierResolveResultTest, ToStringReportsCustomFormat)
{
    // The C# `ToString` is overridden directly: `"[<class-name> <identifier>]"` using
    // `GetType().Name` (polymorphic). The C++ port reproduces it via the `ClassName()`
    // override.
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 0);
    EXPECT_EQ(uirr.ToString(), "[UnknownIdentifierResolveResult Foo]");
}

TEST(UnknownIdentifierResolveResultTest, ClassNameDispatchesThroughBasePointer)
{
    // The `ClassName()` override dispatches through a `ResolveResult*` base pointer
    // (the virtual dispatch the C# resolver relies on in `ToString` via
    // `GetType().Name`): the base pointer's `ToString` reports the subclass name.
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 0);
    ILSpy::Decompiler::Semantics::ResolveResult* base = &uirr;
    EXPECT_EQ(base->ToString(), "[UnknownIdentifierResolveResult Foo]");
}

TEST(UnknownIdentifierResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the runtime
    // type, so a cloned `UnknownIdentifierResolveResult` stays an
    // `UnknownIdentifierResolveResult` (not sliced to the `ResolveResult` base). The
    // C++ override reproduces this: the clone is an `UnknownIdentifierResolveResult`
    // (`dynamic_cast` succeeds).
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 0);
    auto clone = uirr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult*>(clone.get()),
              nullptr);
}

TEST(UnknownIdentifierResolveResultTest, ShallowCloneCopiesIdentifierAndCount)
{
    // The clone value-copies the `identifier_` `std::string` and the
    // `typeArgumentCount_` `int` (faithful to `MemberwiseClone`'s value copy): the
    // clone reports the same identifier and count.
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 2);
    auto clone = uirr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* down = dynamic_cast<ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult*>(clone.get());
    ASSERT_NE(down, nullptr);
    EXPECT_EQ(down->Identifier(), "Foo");
    EXPECT_EQ(down->TypeArgumentCount(), 2);
    EXPECT_TRUE(clone->IsError());
}

TEST(UnknownIdentifierResolveResultTest, ShallowCloneIsDistinctInstance)
{
    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult uirr("Foo", 0);
    auto clone = uirr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &uirr);
}

TEST(UnknownIdentifierResolveResultTest, ClassShapeStaticAsserts)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult>,
                  "UnknownIdentifierResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult>,
                  "UnknownIdentifierResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult>,
                  "UnknownIdentifierResolveResult is polymorphic (the IsError/ToString/ClassName/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `UnknownIdentifierResolveResult` is NOT sealed (the C# class is unsealed),
    // so the C++ port is NOT final.
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult>,
                  "UnknownIdentifierResolveResult is not final (the C# class is unsealed).");
}
