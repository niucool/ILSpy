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

// Tests for the TransformCollectionAndObjectInitializers member-shape helpers
// (TypeContainsInitOnlyProperties, IsRecordCloneMethodCall,
// IsMethodCallOnVariable, IsValidObjectInitializerTarget), the settings flags
// that gate the deferred statement-transform body, and that body's deferral
// contract. The whole-transform fixture (the Run/IsPartOfInitializer state
// machine) lands with the body itself.

#include "Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::IL::AccessPathElement;
using ILSpy::Decompiler::IL::Block;
using ILSpy::Decompiler::IL::Call;
using ILSpy::Decompiler::IL::ILTransformContext;
using ILSpy::Decompiler::IL::ILTransformSettings;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::LdFlda;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::LdLoca;
using ILSpy::Decompiler::IL::LdObj;
using ILSpy::Decompiler::IL::OpCode;
using ILSpy::Decompiler::IL::StatementTransformContext;
using ILSpy::Decompiler::IL::StObj;
using ILSpy::Decompiler::IL::TransformCollectionAndObjectInitializers;
using ILSpy::Decompiler::IL::VariableKind;

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;

namespace {

struct Fixture {
    TS::SimpleCompilation compilation;

    Fixture() : compilation(Impl::MinimalCorlib::Instance(), {}) {}

    TS::ITypePtr Known(TS::KnownTypeCode code) {
        return std::make_shared<TS::KnownType>(code);
    }

    ILVariablePtr MakeVar(std::string name, TS::ITypePtr type) {
        auto v = std::make_shared<IL::ILVariable>(VariableKind::Local,
                                                  std::move(type), 0);
        v->Name = std::move(name);
        return v;
    }

    std::shared_ptr<Impl::FakeField> MakeField(const char* name, TS::ITypePtr type) {
        auto field = std::make_shared<Impl::FakeField>(compilation);
        field->SetName(name);
        field->SetDeclaringType(Known(TS::KnownTypeCode::Object));
        field->SetReturnType(std::move(type));
        return field;
    }

    // A property whose `IsIndexer` / `DeclaringType` are configurable; the
    // owning shared_ptr is kept by the caller (the element member is
    // non-owning).
    std::shared_ptr<Impl::FakeProperty> MakeProperty(const char* name,
                                                     bool isIndexer,
                                                     TS::ITypePtr declaringType) {
        auto property = std::make_shared<Impl::FakeProperty>(compilation);
        property->SetName(name);
        property->SetDeclaringType(std::move(declaringType));
        property->SetReturnType(Known(TS::KnownTypeCode::Int32));
        property->SetIsIndexer(isIndexer);
        return property;
    }

    // A setter method whose `IsInitOnly` is configurable (LookupMethod is the
    // stub with the settable flag; FakeMethod hardcodes false).
    std::shared_ptr<TestSupport::LookupMethod> MakeSetter(const char* name,
                                                          bool isInitOnly) {
        auto setter = std::make_shared<TestSupport::LookupMethod>(name, compilation);
        setter->SetIsInitOnly(isInitOnly);
        return setter;
    }

    std::shared_ptr<TestSupport::LookupTypeDefinition> MakeType(const char* name) {
        std::string ns("Ns");
        std::string n(name);
        return std::make_shared<TestSupport::LookupTypeDefinition>(
            n, ns, TS::FullTypeName(TS::TopLevelTypeName(ns, n)),
            TS::TypeKind::Class, TS::Accessibility::Public, compilation, nullptr);
    }

    std::unique_ptr<LdFlda> FieldAddress(std::unique_ptr<IL::ILInstruction> target,
                                         const std::shared_ptr<Impl::FakeField>& field) {
        auto ldflda = std::make_unique<LdFlda>(std::move(target), std::string("F"));
        ldflda->Field = field;
        return ldflda;
    }
};

// The `IMember` views matching what the port's machinery stores (the
// interface-subobject convention; the direct FakeX* -> IMember* cast is
// ambiguous through the diamond).
const TS::IMember* AsFieldMember(const Impl::FakeField& field) {
    return static_cast<const TS::IMember*>(static_cast<const TS::IField*>(&field));
}

const TS::IMember* AsPropertyMember(const Impl::FakeProperty& property) {
    return static_cast<const TS::IMember*>(static_cast<const TS::IProperty*>(&property));
}

} // namespace

// ---- The settings flags gating the deferred body ----

TEST(TransformCollectionAndObjectInitializersTest, SettingsDefaultToTheCSharpDefaults)
{
    ILTransformSettings settings;
    // DecompilerSettings.ObjectOrCollectionInitializers (C# 3.0, default true),
    // UseObjectCreationOfGenericTypeParameter (C# 2.0, default true) and
    // WithExpressions (C# 9.0, default true).
    EXPECT_TRUE(settings.ObjectOrCollectionInitializers);
    EXPECT_TRUE(settings.UseObjectCreationOfGenericTypeParameter);
    EXPECT_TRUE(settings.WithExpressions);
}

// ---- TypeContainsInitOnlyProperties ----

TEST(TransformCollectionAndObjectInitializersTest, InitOnlySetterMakesTheTypeContainInitOnlyProperties)
{
    Fixture f;
    auto type = f.MakeType("Person");
    auto property = std::make_shared<Impl::FakeProperty>(f.compilation);
    property->SetName("Name");
    property->SetDeclaringType(f.Known(TS::KnownTypeCode::String));
    // The property's Setter is a raw pointer, so the setter must outlive it.
    auto initSetter = f.MakeSetter("set_Name", /*isInitOnly=*/true);
    property->SetSetter(initSetter.get());
    type->SetProperties({static_cast<const TS::IProperty*>(property.get())});

    EXPECT_TRUE(TransformCollectionAndObjectInitializers::
                TypeContainsInitOnlyProperties(type.get()));
}

TEST(TransformCollectionAndObjectInitializersTest, PlainOrMissingSettersDoNotMakeTheTypeContainInitOnlyProperties)
{
    Fixture f;
    auto type = f.MakeType("Person");

    // No properties at all.
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::
                 TypeContainsInitOnlyProperties(type.get()));

    // A property with a regular (non-init) setter.
    auto plain = std::make_shared<Impl::FakeProperty>(f.compilation);
    plain->SetName("Age");
    auto plainSetter = f.MakeSetter("set_Age", /*isInitOnly=*/false);
    plain->SetSetter(plainSetter.get());
    // A property with no setter at all (the C# `Setter?.IsInitOnly ?? false`).
    auto getterOnly = std::make_shared<Impl::FakeProperty>(f.compilation);
    getterOnly->SetName("Id");
    type->SetProperties({static_cast<const TS::IProperty*>(plain.get()),
                         static_cast<const TS::IProperty*>(getterOnly.get())});

    EXPECT_FALSE(TransformCollectionAndObjectInitializers::
                 TypeContainsInitOnlyProperties(type.get()));
    // The C# null type definition (`typeDefinition?.Properties ?? []`).
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::
                 TypeContainsInitOnlyProperties(nullptr));
}

// ---- IsRecordCloneMethodCall ----

TEST(TransformCollectionAndObjectInitializersTest, IsRecordCloneMethodCallMatrix)
{
    Fixture f;
    auto record = f.MakeType("Person");
    record->SetIsRecord(true);
    auto clone = std::make_shared<TestSupport::LookupMethod>("<Clone>$", f.compilation);
    clone->SetDeclaringTypeDefinition(record.get());

    auto v = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));

    // The positive: a record-declared `<Clone>$` with the single receiver.
    Call call("Ns.Person::<Clone>$");
    call.Method = clone;
    call.AddArg(std::make_unique<LdLoc>(v));
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::IsRecordCloneMethodCall(&call));

    // A second argument disqualifies the shape.
    call.AddArg(std::make_unique<LdLoc>(v));
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsRecordCloneMethodCall(&call));

    // A non-record declaring type.
    record->SetIsRecord(false);
    call.Arguments.pop_back();
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsRecordCloneMethodCall(&call));
    record->SetIsRecord(true);

    // A different method name.
    auto other = std::make_shared<TestSupport::LookupMethod>("Clone", f.compilation);
    other->SetDeclaringTypeDefinition(record.get());
    call.Method = other;
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsRecordCloneMethodCall(&call));

    // No resolved method, and no declaring type definition.
    call.Method = clone;
    clone->SetDeclaringTypeDefinition(nullptr);
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsRecordCloneMethodCall(&call));
    call.Method = nullptr;
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsRecordCloneMethodCall(&call));
}

// ---- IsMethodCallOnVariable ----

TEST(TransformCollectionAndObjectInitializersTest, IsMethodCallOnVariableAcceptsRefTypedLoadAndByRefLoad)
{
    Fixture f;
    auto refVar = f.MakeVar("list", f.Known(TS::KnownTypeCode::String));
    auto valVar = f.MakeVar("n", f.Known(TS::KnownTypeCode::Int32));

    // A reference-typed variable is used through a plain ldloc.
    LdLoc refLoad(refVar);
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &refLoad, refVar.get()));

    // A value-typed variable is used through an ldloca.
    LdLoca valAddress(valVar);
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &valAddress, valVar.get()));

    // The cross shapes do NOT match: a value-typed ldloc fails the
    // MatchLdLocRef reference-type gate, and a reference-typed ldloca fails
    // its `IsReferenceType != true` gate.
    LdLoc valLoad(valVar);
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &valLoad, valVar.get()));
    LdLoca refAddress(refVar);
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &refAddress, refVar.get()));

    // Another variable's load is not a use of this one.
    auto other = f.MakeVar("other", f.Known(TS::KnownTypeCode::String));
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &refLoad, other.get()));
}

TEST(TransformCollectionAndObjectInitializersTest, IsMethodCallOnVariableRecursesIntoCallReceivers)
{
    Fixture f;
    auto valVar = f.MakeVar("n", f.Known(TS::KnownTypeCode::Int32));

    // An instance call whose receiver (Arguments[0]) is a use of the variable.
    auto method = std::make_shared<Impl::FakeMethod>(
        f.compilation, TS::SymbolKind::Method);
    method->SetName("GetHashCode");
    method->SetDeclaringType(f.Known(TS::KnownTypeCode::Int32));
    method->SetIsStatic(false);

    Call call("System.Int32::GetHashCode");
    call.Method = method;
    call.AddArg(std::make_unique<LdLoca>(valVar));
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &call, valVar.get()));

    // A static call does not recurse into its arguments (the argument is a
    // plain value, not a receiver).
    method->SetIsStatic(true);
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &call, valVar.get()));

    // A call without a resolved method falls back to the reader's
    // IsInstanceCall flag.
    call.Method = nullptr;
    call.IsInstanceCall = true;
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &call, valVar.get()));
    call.IsInstanceCall = false;
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &call, valVar.get()));
}

TEST(TransformCollectionAndObjectInitializersTest, IsMethodCallOnVariableRecursesIntoFieldTargets)
{
    Fixture f;
    auto valVar = f.MakeVar("n", f.Known(TS::KnownTypeCode::Int32));
    auto field = f.MakeField("F", f.Known(TS::KnownTypeCode::Int32));

    // ldfld F(ldloca n) -- a load through a field of the variable.
    LdObj ldobj(f.FieldAddress(std::make_unique<LdLoca>(valVar), field),
                f.Known(TS::KnownTypeCode::Int32));
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &ldobj, valVar.get()));

    // stfld F(ldloca n, value) -- a store through a field of the variable.
    StObj store(f.FieldAddress(std::make_unique<LdLoca>(valVar), field),
                std::make_unique<LdLoca>(valVar),
                f.Known(TS::KnownTypeCode::Int32));
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &store, valVar.get()));

    // ldflda F(ldloca n) -- a bare field address.
    auto ldflda = f.FieldAddress(std::make_unique<LdLoca>(valVar), field);
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        ldflda.get(), valVar.get()));

    // Anything else is not a use.
    LdObj other(f.FieldAddress(std::make_unique<LdLoca>(f.MakeVar("m", f.Known(TS::KnownTypeCode::Int32))), field),
                f.Known(TS::KnownTypeCode::Int32));
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
        &other, valVar.get()));
}

// ---- IsValidObjectInitializerTarget ----

TEST(TransformCollectionAndObjectInitializersTest, EmptyOrNonIndexerPathsAreValidTargets)
{
    Fixture f;
    // The empty path (a setter directly on the initializer target).
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::
                IsValidObjectInitializerTarget({}));

    // A field member as the last container element.
    auto field = f.MakeField("F", f.Known(TS::KnownTypeCode::String));
    AccessPathElement fieldElem(OpCode::LdFlda, AsFieldMember(*field));
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::
                IsValidObjectInitializerTarget({fieldElem}));

    // A non-indexer property as the last container element.
    auto property = f.MakeProperty("P", /*isIndexer=*/false,
                                   f.Known(TS::KnownTypeCode::Object));
    AccessPathElement propElem(OpCode::Call, AsPropertyMember(*property));
    EXPECT_TRUE(TransformCollectionAndObjectInitializers::
                IsValidObjectInitializerTarget({propElem}));
}

TEST(TransformCollectionAndObjectInitializersTest, IndexerTargetRequiresMatchingPreviousReturnType)
{
    Fixture f;
    // previous: a field typed String; element: a String-declared indexer.
    auto field = f.MakeField("Items", f.Known(TS::KnownTypeCode::String));
    auto indexer = f.MakeProperty("Item", /*isIndexer=*/true,
                                  f.Known(TS::KnownTypeCode::String));
    AccessPathElement fieldElem(OpCode::LdFlda, AsFieldMember(*field));
    AccessPathElement indexerElem(OpCode::Call, AsPropertyMember(*indexer));

    EXPECT_TRUE(TransformCollectionAndObjectInitializers::
                IsValidObjectInitializerTarget({fieldElem, indexerElem}));

    // A previous return type that is NOT the indexer's declaring type fails
    // (the nested initializer's Add calls would not resolve on it).
    auto otherField = f.MakeField("Items", f.Known(TS::KnownTypeCode::Int32));
    AccessPathElement otherElem(OpCode::LdFlda, AsFieldMember(*otherField));
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::
                 IsValidObjectInitializerTarget({otherElem, indexerElem}));

    // A first-and-only indexer element has no previous element to establish
    // the indexer's declaring type.
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::
                 IsValidObjectInitializerTarget({indexerElem}));

    // A member without a declaring type cannot be proven to match.
    auto noDeclaring = f.MakeProperty("Item", /*isIndexer=*/true, nullptr);
    AccessPathElement noDeclaringElem(OpCode::Call, AsPropertyMember(*noDeclaring));
    EXPECT_FALSE(TransformCollectionAndObjectInitializers::
                 IsValidObjectInitializerTarget({fieldElem, noDeclaringElem}));
}

// ---- The Run deferral contract ----

TEST(TransformCollectionAndObjectInitializersTest, RunThrowsUntilTheBodyIsPorted)
{
    ILTransformContext ctx;
    TransformCollectionAndObjectInitializers transform;
    Block block;
    StatementTransformContext context(ctx, &block);
    EXPECT_THROW(transform.Run(block, 0, context), std::logic_error);
}
