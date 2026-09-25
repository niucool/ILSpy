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
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::IL::AccessPathElement;
using ILSpy::Decompiler::IL::Block;
using ILSpy::Decompiler::IL::BlockKind;
using ILSpy::Decompiler::IL::Call;
using ILSpy::Decompiler::IL::ILInstruction;
using ILSpy::Decompiler::IL::ILTransformContext;
using ILSpy::Decompiler::IL::ILTransformSettings;
using ILSpy::Decompiler::IL::ILVariablePtr;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::LdFlda;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::LdLoca;
using ILSpy::Decompiler::IL::LdObj;
using ILSpy::Decompiler::IL::OpCode;
using ILSpy::Decompiler::IL::StLoc;
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

    // The compilation's registered known type (a real definition).
    TS::ITypePtr Find(TS::KnownTypeCode code) {
        return const_cast<TS::IType&>(compilation.FindType(code)).shared_from_this();
    }

    // A plain (non-accessor) method with configurable static-ness.
    std::shared_ptr<Impl::FakeMethod> MakeMethod(const char* name, bool isStatic) {
        auto method = std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetIsStatic(isStatic);
        return method;
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

// ---- IsPartOfInitializer (the statement-scan state machine) ----

namespace {

// A fresh transform + context + block per scan, mirroring the C# Run's
// per-scan state reset (possibleIndexVariables/currentPath/isCollection/
// pathStack cleared, one empty set pushed) and its by-ref blockKind /
// initializerContainsInitOnlyItems slots.
struct Scan {
    TransformCollectionAndObjectInitializers transform;
    ILTransformContext ctx;
    Block block;
    StatementTransformContext context;
    BlockKind blockKind = BlockKind::CollectionInitializer;
    bool initOnly = false;

    Scan() : context(ctx, &block) { transform.ResetInitializerScanState(); }

    bool Drive(int pos, IL::ILVariable* target, const TS::IType* rootType) {
        return transform.IsPartOfInitializer(block.Instructions, pos, target,
                                             rootType, blockKind, initOnly, context);
    }
};

// A FakeMethod whose IsInitOnly is configurable (FakeMethod hardcodes false):
// the init-only setter accessor of the initializerContainsInitOnlyItems arm.
struct InitOnlyAccessorMethod : Impl::FakeMethod {
    bool isInitOnly_;
    InitOnlyAccessorMethod(const TS::ICompilation& compilation, bool isInitOnly)
        : FakeMethod(compilation, TS::SymbolKind::Method), isInitOnly_(isInitOnly) {}
    bool IsInitOnly() const override { return isInitOnly_; }
};

} // namespace

TEST(TransformCollectionAndObjectInitializersTest, IndexVariableArmRecordsSingleDefinitionLocalStores)
{
    Fixture f;
    Scan scan;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto idx = f.MakeVar("k", f.Known(TS::KnownTypeCode::Int32));
    idx->StoreCount = 1;  // IsSingleDefinition

    auto store = std::make_unique<StLoc>(idx, std::make_unique<LdcI4>(5));
    ILInstruction* storedValue = store->Value.get();
    scan.block.Add(std::move(store));

    EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
    ASSERT_EQ(scan.transform.possibleIndexVariables.count(idx.get()), 1u);
    const auto& info = scan.transform.possibleIndexVariables[idx.get()];
    EXPECT_EQ(info.Index, 0);         // the stloc's ChildIndex
    EXPECT_EQ(info.Value, storedValue);
    // No access path was consumed.
    EXPECT_TRUE(scan.transform.currentPath.empty());
    EXPECT_EQ(scan.blockKind, BlockKind::CollectionInitializer);
}

TEST(TransformCollectionAndObjectInitializersTest, IndexVariableArmRejectsWhenDictionaryInitializersIsOff)
{
    Fixture f;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto idx = f.MakeVar("k", f.Known(TS::KnownTypeCode::Int32));
    idx->StoreCount = 1;

    {
        // The IL-layer subset gate (the IL-layer pipeline callers' settings).
        Scan scan;
        scan.ctx.Settings.DictionaryInitializers = false;
        scan.block.Add(std::make_unique<StLoc>(idx, std::make_unique<LdcI4>(5)));
        EXPECT_FALSE(scan.Drive(0, target.get(), nullptr));
        EXPECT_TRUE(scan.transform.possibleIndexVariables.empty());
    }
    {
        // The threaded C#-layer settings take precedence over the subset (the
        // C# consults one settings object for both the gate and GetAccessPath).
        ILSpy::Decompiler::DecompilerSettings csharpSettings;
        csharpSettings.SetDictionaryInitializers(false);
        Scan scan;  // subset default true
        scan.ctx.CSharpSettings = &csharpSettings;
        scan.block.Add(std::make_unique<StLoc>(idx, std::make_unique<LdcI4>(5)));
        EXPECT_FALSE(scan.Drive(0, target.get(), nullptr));
        EXPECT_TRUE(scan.transform.possibleIndexVariables.empty());
    }
}

TEST(TransformCollectionAndObjectInitializersTest, IndexVariableArmRejectsASubtreeLoadingTheTarget)
{
    Fixture f;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto idx = f.MakeVar("k", f.Known(TS::KnownTypeCode::Int32));
    idx->StoreCount = 1;

    // The C# check walks the value's STRICT descendants (Descendants starts
    // at the children), so a load of the initializer variable inside the
    // stored expression's subtree rejects the store as an index.
    {
        Scan scan;
        // stloc k(add(ldloc v, ldc.i4 1))
        scan.block.Add(std::make_unique<StLoc>(
            idx, std::make_unique<IL::BinaryNumericInstruction>(
                     std::make_unique<LdLoc>(target), std::make_unique<LdcI4>(1),
                     IL::BinaryNumericOperator::Add)));
        EXPECT_FALSE(scan.Drive(0, target.get(), nullptr));
        EXPECT_TRUE(scan.transform.possibleIndexVariables.empty());
    }
    {
        // stloc k(call(ldloca v, ldc.i4 1)) -- the ldloca shape.
        Scan scan;
        auto call = std::make_unique<Call>("Ns.C::M");
        call->AddArg(std::make_unique<LdLoca>(target));
        call->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::make_unique<StLoc>(idx, std::move(call)));
        EXPECT_FALSE(scan.Drive(0, target.get(), nullptr));
        EXPECT_TRUE(scan.transform.possibleIndexVariables.empty());
    }

    // A BARE ldloc/ldloca value has no descendants, so the walk sees nothing:
    // the store is recorded as an index variable (the faithful C# shape).
    {
        Scan scan;
        scan.block.Add(std::make_unique<StLoc>(idx, std::make_unique<LdLoc>(target)));
        EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
        EXPECT_EQ(scan.transform.possibleIndexVariables.count(idx.get()), 1u);
    }

    // A non-single-definition store falls through to the access-path walk,
    // which rejects a StLoc (not a path instruction).
    Scan multi;
    auto multiDef = f.MakeVar("m", f.Known(TS::KnownTypeCode::Int32));
    multiDef->StoreCount = 2;
    multi.block.Add(std::make_unique<StLoc>(multiDef, std::make_unique<LdcI4>(5)));
    EXPECT_FALSE(multi.Drive(0, target.get(), nullptr));
    EXPECT_TRUE(multi.transform.possibleIndexVariables.empty());
}

TEST(TransformCollectionAndObjectInitializersTest, SetterArmAcceptsAFieldStoreAndUpgradesBlockKind)
{
    Fixture f;
    Scan scan;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto field = f.MakeField("F", f.Known(TS::KnownTypeCode::Int32));

    // stobj T(ldflda F(ldloc v), ldc.i4 1)
    scan.block.Add(std::make_unique<StObj>(
        f.FieldAddress(std::make_unique<LdLoc>(target), field),
        std::make_unique<LdcI4>(1), f.Known(TS::KnownTypeCode::Int32)));

    EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
    EXPECT_EQ(scan.blockKind, BlockKind::ObjectInitializer);
    EXPECT_FALSE(scan.initOnly);
    // The last element never enters currentPath; it lands in the path stack's
    // top set (the root level here).
    EXPECT_TRUE(scan.transform.currentPath.empty());
    ASSERT_EQ(scan.transform.pathStack.size(), 1u);
    EXPECT_EQ(scan.transform.pathStack[0].count(
                  AccessPathElement(OpCode::StObj, AsFieldMember(*field))), 1u);
}

TEST(TransformCollectionAndObjectInitializersTest, TwoLeavesAccumulateAndTheDuplicateIsRejected)
{
    Fixture f;
    Scan scan;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto fieldF = f.MakeField("F", f.Known(TS::KnownTypeCode::Int32));
    auto fieldG = f.MakeField("G", f.Known(TS::KnownTypeCode::Int32));

    auto store = [&](const std::shared_ptr<Impl::FakeField>& field) {
        scan.block.Add(std::make_unique<StObj>(
            f.FieldAddress(std::make_unique<LdLoc>(target), field),
            std::make_unique<LdcI4>(1), f.Known(TS::KnownTypeCode::Int32)));
    };
    store(fieldF);
    store(fieldG);
    store(fieldF);  // the duplicate leaf

    EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
    EXPECT_TRUE(scan.Drive(1, target.get(), nullptr));
    ASSERT_EQ(scan.transform.pathStack[0].size(), 2u);
    // Writing the same member at the same level again ends the scan.
    EXPECT_FALSE(scan.Drive(2, target.get(), nullptr));
}

TEST(TransformCollectionAndObjectInitializersTest, NestedPathPushesAndPopsTheState)
{
    Fixture f;
    Scan scan;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    // A nested `v.B.A = x` store: the path reads [B, A] (root-to-leaf).
    auto fieldB = f.MakeField("B", f.Known(TS::KnownTypeCode::Object));
    auto fieldA = f.MakeField("A", f.Known(TS::KnownTypeCode::Int32));
    auto fieldA2 = f.MakeField("A2", f.Known(TS::KnownTypeCode::Int32));
    auto fieldC = f.Known(TS::KnownTypeCode::Int32);
    auto fieldCReal = f.MakeField("C", f.Known(TS::KnownTypeCode::Int32));

    scan.block.Add(std::make_unique<StObj>(
        f.FieldAddress(f.FieldAddress(std::make_unique<LdLoc>(target), fieldB), fieldA),
        std::make_unique<LdcI4>(1), f.Known(TS::KnownTypeCode::Int32)));
    scan.block.Add(std::make_unique<StObj>(
        f.FieldAddress(f.FieldAddress(std::make_unique<LdLoc>(target), fieldB), fieldA2),
        std::make_unique<LdcI4>(2), f.Known(TS::KnownTypeCode::Int32)));
    scan.block.Add(std::make_unique<StObj>(
        f.FieldAddress(std::make_unique<LdLoc>(target), fieldCReal),
        std::make_unique<LdcI4>(3), f.Known(TS::KnownTypeCode::Int32)));

    // Drive 1: pushes B onto currentPath (and B into the root set) and opens
    // the nested level.
    EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
    ASSERT_EQ(scan.transform.currentPath.size(), 1u);
    EXPECT_EQ(scan.transform.currentPath[0].Member, AsFieldMember(*fieldB));
    ASSERT_EQ(scan.transform.pathStack.size(), 2u);
    ASSERT_EQ(scan.transform.pathStack[0].size(), 1u);

    // Drive 2: the same B prefix -- no push, the leaf joins the nested set.
    EXPECT_TRUE(scan.Drive(1, target.get(), nullptr));
    ASSERT_EQ(scan.transform.currentPath.size(), 1u);
    ASSERT_EQ(scan.transform.pathStack.size(), 2u);
    ASSERT_EQ(scan.transform.pathStack[1].size(), 2u);

    // Drive 3: a top-level store pops back to the root level (and the root
    // set accumulates the top-level leaf beside the pushed B).
    EXPECT_TRUE(scan.Drive(2, target.get(), nullptr));
    EXPECT_TRUE(scan.transform.currentPath.empty());
    ASSERT_EQ(scan.transform.pathStack.size(), 1u);
    ASSERT_EQ(scan.transform.pathStack[0].size(), 2u);
    (void)fieldC;
}

TEST(TransformCollectionAndObjectInitializersTest, AdderArmSetsCollectionMode)
{
    Fixture f;
    Scan scan;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto field = f.MakeField("F", f.Known(TS::KnownTypeCode::Int32));
    auto add = f.MakeMethod("Add", /*isStatic=*/false);

    // call Add(ldloc v, ldc.i4 1)
    auto call = std::make_unique<Call>("Ns.C::Add");
    call->Method = add;
    call->AddArg(std::make_unique<LdLoc>(target));
    call->AddArg(std::make_unique<LdcI4>(1));
    scan.block.Add(std::move(call));

    EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
    EXPECT_TRUE(scan.transform.isCollection);
    EXPECT_EQ(scan.blockKind, BlockKind::CollectionInitializer);

    // A Setter after the collection mode is rejected.
    scan.block.Add(std::make_unique<StObj>(
        f.FieldAddress(std::make_unique<LdLoc>(target), field),
        std::make_unique<LdcI4>(1), f.Known(TS::KnownTypeCode::Int32)));
    EXPECT_FALSE(scan.Drive(1, target.get(), nullptr));
}

TEST(TransformCollectionAndObjectInitializersTest, AdderAfterSettersIsRejectedAndPopResetsCollectionMode)
{
    Fixture f;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto field = f.MakeField("F", f.Known(TS::KnownTypeCode::Int32));
    auto add = f.MakeMethod("Add", /*isStatic=*/false);

    {
        // The Adder arm requires an empty top: a preceding setter at the same
        // level has already filled the root set.
        Scan scan;
        scan.block.Add(std::make_unique<StObj>(
            f.FieldAddress(std::make_unique<LdLoc>(target), field),
            std::make_unique<LdcI4>(1), f.Known(TS::KnownTypeCode::Int32)));
        auto call = std::make_unique<Call>("Ns.C::Add");
        call->Method = add;
        call->AddArg(std::make_unique<LdLoc>(target));
        call->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::move(call));

        EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
        EXPECT_FALSE(scan.Drive(1, target.get(), nullptr));
    }

    {
        // Every pop resets the collection mode: a nested Add under B leaves
        // collection mode when the scan pops back to the root level, and a
        // top-level setter is accepted again.
        auto fieldB = f.MakeField("B", f.Known(TS::KnownTypeCode::Object));
        auto fieldA = f.MakeField("A", f.Known(TS::KnownTypeCode::Int32));
        Scan scan;
        // Add under B: call Add(ldobj A(ldloc v), 1)? -- use a nested receiver
        // through an ldflda chain the walk accepts: call Add over the B field.
        auto nestedAdd = std::make_unique<Call>("Ns.C::Add");
        nestedAdd->Method = add;
        nestedAdd->AddArg(std::make_unique<LdObj>(
            f.FieldAddress(std::make_unique<LdLoc>(target), fieldB),
            f.Known(TS::KnownTypeCode::Object)));
        nestedAdd->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::move(nestedAdd));
        // A top-level setter after the nested Add.
        scan.block.Add(std::make_unique<StObj>(
            f.FieldAddress(std::make_unique<LdLoc>(target), field),
            std::make_unique<LdcI4>(2), f.Known(TS::KnownTypeCode::Int32)));

        EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
        EXPECT_TRUE(scan.transform.isCollection);
        // The nested Add's path is [B, Add]: B was pushed, then the Adder set
        // collection mode at the nested level.
        ASSERT_EQ(scan.transform.currentPath.size(), 1u);
        // Popping back to the root resets isCollection and accepts the setter.
        EXPECT_TRUE(scan.Drive(1, target.get(), nullptr));
        EXPECT_FALSE(scan.transform.isCollection);
    }
}

TEST(TransformCollectionAndObjectInitializersTest, AccessorSetterMarksUsedIndexVariables)
{
    Fixture f;
    Scan scan;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto idx = f.MakeVar("k", f.Known(TS::KnownTypeCode::Int32));
    idx->StoreCount = 1;

    // The Item indexer property and its set_Item accessor (the accessor IS the
    // property's Setter, the real compiler shape).
    auto property = std::make_shared<Impl::FakeProperty>(f.compilation);
    property->SetName("Item");
    property->SetDeclaringType(f.Known(TS::KnownTypeCode::String));
    property->SetReturnType(f.Known(TS::KnownTypeCode::Int32));
    property->SetIsIndexer(true);
    auto accessor = std::make_shared<Impl::FakeMethod>(
        f.compilation, TS::SymbolKind::Method);
    accessor->SetName("set_Item");
    accessor->SetDeclaringType(f.Known(TS::KnownTypeCode::String));
    accessor->SetReturnType(f.Known(TS::KnownTypeCode::Void));
    accessor->SetAccessorKind(TS::MethodSemanticsAttributes::Setter);
    accessor->SetAccessorOwner(AsPropertyMember(*property));
    property->SetSetter(accessor.get());

    // [0] the index variable's store; [1] the indexed setter call.
    scan.block.Add(std::make_unique<StLoc>(idx, std::make_unique<LdcI4>(5)));
    auto call = std::make_unique<Call>("Ns.C::set_Item");
    call->Method = accessor;
    call->AddArg(std::make_unique<LdLoc>(target));
    call->AddArg(std::make_unique<LdLoc>(idx));
    call->AddArg(std::make_unique<LdcI4>(1));
    scan.block.Add(std::move(call));

    EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
    ASSERT_EQ(scan.transform.possibleIndexVariables.count(idx.get()), 1u);
    const auto& before = scan.transform.possibleIndexVariables[idx.get()];
    EXPECT_EQ(before.Index, 0);

    EXPECT_TRUE(scan.Drive(1, target.get(), nullptr));
    // MarkUsedIndices flipped the used index variable's Index to -1, keeping
    // the recorded Value.
    const auto& after = scan.transform.possibleIndexVariables[idx.get()];
    EXPECT_EQ(after.Index, -1);
    EXPECT_EQ(after.Value, before.Value);
    EXPECT_EQ(scan.blockKind, BlockKind::ObjectInitializer);
}

TEST(TransformCollectionAndObjectInitializersTest, InitOnlyAccessorSetsInitializerContainsInitOnlyItems)
{
    Fixture f;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));

    // The init-only set_Name accessor of the Name property.
    auto property = std::make_shared<Impl::FakeProperty>(f.compilation);
    property->SetName("Name");
    property->SetDeclaringType(f.Known(TS::KnownTypeCode::String));
    property->SetReturnType(f.Known(TS::KnownTypeCode::Int32));
    auto initAccessor = std::make_shared<InitOnlyAccessorMethod>(
        f.compilation, /*isInitOnly=*/true);
    initAccessor->SetName("set_Name");
    initAccessor->SetDeclaringType(f.Known(TS::KnownTypeCode::String));
    initAccessor->SetReturnType(f.Known(TS::KnownTypeCode::Void));
    initAccessor->SetAccessorKind(TS::MethodSemanticsAttributes::Setter);
    initAccessor->SetAccessorOwner(AsPropertyMember(*property));
    property->SetSetter(initAccessor.get());

    {
        Scan scan;
        auto call = std::make_unique<Call>("Ns.C::set_Name");
        call->Method = initAccessor;
        call->AddArg(std::make_unique<LdLoc>(target));
        call->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::move(call));
        EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
        EXPECT_TRUE(scan.initOnly);
    }

    // A plain (non-init-only) accessor of its OWN property leaves the flag
    // unset (the flag reads the PROPERTY's Setter, so wiring the plain accessor
    // under the init-only property would be an impossible real-world shape).
    {
        auto plainProperty = std::make_shared<Impl::FakeProperty>(f.compilation);
        plainProperty->SetName("Other");
        plainProperty->SetDeclaringType(f.Known(TS::KnownTypeCode::String));
        plainProperty->SetReturnType(f.Known(TS::KnownTypeCode::Int32));
        auto plainAccessor = std::make_shared<InitOnlyAccessorMethod>(
            f.compilation, /*isInitOnly=*/false);
        plainAccessor->SetName("set_Other");
        plainAccessor->SetDeclaringType(f.Known(TS::KnownTypeCode::String));
        plainAccessor->SetReturnType(f.Known(TS::KnownTypeCode::Void));
        plainAccessor->SetAccessorKind(TS::MethodSemanticsAttributes::Setter);
        plainAccessor->SetAccessorOwner(AsPropertyMember(*plainProperty));
        plainProperty->SetSetter(plainAccessor.get());
        Scan scan;
        auto call = std::make_unique<Call>("Ns.C::set_Other");
        call->Method = plainAccessor;
        call->AddArg(std::make_unique<LdLoc>(target));
        call->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::move(call));
        EXPECT_TRUE(scan.Drive(0, target.get(), nullptr));
        EXPECT_FALSE(scan.initOnly);
    }
}

TEST(TransformCollectionAndObjectInitializersTest, InvalidKindAndTargetMismatchAreRejected)
{
    Fixture f;
    Scan scan;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto other = f.MakeVar("w", f.Known(TS::KnownTypeCode::String));
    auto field = f.MakeField("F", f.Known(TS::KnownTypeCode::Int32));

    // A bare load: the walk terminates at the target with an Invalid kind.
    scan.block.Add(std::make_unique<LdLoc>(target));
    // A store rooted at another variable.
    scan.block.Add(std::make_unique<StObj>(
        f.FieldAddress(std::make_unique<LdLoc>(other), field),
        std::make_unique<LdcI4>(1), f.Known(TS::KnownTypeCode::Int32)));

    EXPECT_FALSE(scan.Drive(0, target.get(), nullptr));
    EXPECT_FALSE(scan.Drive(1, target.get(), nullptr));
    EXPECT_TRUE(scan.transform.possibleIndexVariables.empty());
}

TEST(TransformCollectionAndObjectInitializersTest, ResolverThreadedAddApplicabilityCheck)
{
    Fixture f;
    auto target = f.MakeVar("v", f.Known(TS::KnownTypeCode::String));
    auto ienumerable = f.Find(TS::KnownTypeCode::IEnumerable);
    ILSpy::Decompiler::CSharp::Resolver::CSharpResolver resolver(f.compilation);

    // An instance method NOT named Add: the threaded resolver's applicability
    // check rejects it (the C# `goto default` -> Invalid kind).
    auto foo = f.MakeMethod("Foo", /*isStatic=*/false);
    {
        Scan scan;
        scan.ctx.Resolver = &resolver;
        auto call = std::make_unique<Call>("Ns.C::Foo");
        call->Method = foo;
        call->AddArg(std::make_unique<LdLoc>(target));
        call->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::move(call));
        EXPECT_FALSE(scan.Drive(0, target.get(), ienumerable.get()));
    }

    // A static Add is rejected too (IsMethodApplicable's static-method check).
    auto staticAdd = f.MakeMethod("Add", /*isStatic=*/true);
    {
        Scan scan;
        scan.ctx.Resolver = &resolver;
        auto call = std::make_unique<Call>("Ns.C::Add");
        call->Method = staticAdd;
        call->AddArg(std::make_unique<LdLoc>(target));
        call->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::move(call));
        EXPECT_FALSE(scan.Drive(0, target.get(), ienumerable.get()));
    }

    // A real instance Add over an IEnumerable receiver type passes.
    auto add = f.MakeMethod("Add", /*isStatic=*/false);
    {
        Scan scan;
        scan.ctx.Resolver = &resolver;
        auto call = std::make_unique<Call>("Ns.C::Add");
        call->Method = add;
        call->AddArg(std::make_unique<LdLoc>(target));
        call->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::move(call));
        EXPECT_TRUE(scan.Drive(0, target.get(), ienumerable.get()));
        EXPECT_TRUE(scan.transform.isCollection);
    }

    // Without a threaded resolver (the port's IL-layer pipeline callers) the
    // applicability check is skipped: even Foo is folded as an Adder -- the
    // documented divergence pinned here.
    {
        Scan scan;
        auto call = std::make_unique<Call>("Ns.C::Foo");
        call->Method = foo;
        call->AddArg(std::make_unique<LdLoc>(target));
        call->AddArg(std::make_unique<LdcI4>(1));
        scan.block.Add(std::move(call));
        EXPECT_TRUE(scan.Drive(0, target.get(), ienumerable.get()));
        EXPECT_TRUE(scan.transform.isCollection);
    }
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
