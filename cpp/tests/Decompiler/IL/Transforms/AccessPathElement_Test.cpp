// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
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

// Tests for AccessPathElement/GetAccessPath (the TransformCollectionAndObject
// Initializers.cs access-path machinery): the store/call walk (field chains,
// accessor calls with the empty-vs-null indices distinction, Add calls), the
// invalidating guards (newoj/unresolved-method calls, the readonly-field
// gate, values referencing the target, get-only inner properties, the
// resolver-driven Add applicability matrix), and the Equals/GetHashCode/
// ToString surface.

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdObjIfRef.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Transforms/AccessPathElement.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/Implementation/FakeMember.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using ILSpy::Decompiler::IL::AccessPathElement;
using ILSpy::Decompiler::IL::AccessPathKind;
using ILSpy::Decompiler::CSharp::Resolver::CSharpResolver;
using ILSpy::Decompiler::DecompilerSettings;
using ILSpy::Decompiler::IL::Call;
using ILSpy::Decompiler::IL::LdcI4;
using ILSpy::Decompiler::IL::LdLoc;
using ILSpy::Decompiler::IL::LdLoca;
using ILSpy::Decompiler::IL::LdObj;
using ILSpy::Decompiler::IL::LdObjIfRef;
using ILSpy::Decompiler::IL::LdStr;
using ILSpy::Decompiler::IL::LdFlda;
using ILSpy::Decompiler::IL::StObj;
using ILSpy::Decompiler::IL::VariableKind;

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Impl = ::ILSpy::Decompiler::TypeSystem::Implementation;
namespace TestSupport = ::ILSpy::Decompiler::TypeSystem::TestSupport;

namespace {

struct Fixture {
    TS::SimpleCompilation compilation;
    // Keeps every fake method alive for the lifetime of the fixture (the
    // property setter a FakeProperty holds is a raw pointer).
    std::vector<std::shared_ptr<Impl::FakeMethod>> methodSink;

    Fixture() : compilation(Impl::MinimalCorlib::Instance(), {}) {}

    TS::ITypePtr Known(TS::KnownTypeCode code) {
        return std::make_shared<TS::KnownType>(code);
    }

    TS::ITypePtr Find(TS::KnownTypeCode code) {
        return const_cast<TS::IType&>(compilation.FindType(code)).shared_from_this();
    }

    IL::ILVariablePtr MakeVar(std::string name) {
        auto v = std::make_shared<IL::ILVariable>(VariableKind::Local,
                                                  Known(TS::KnownTypeCode::Int32), 0);
        v->Name = std::move(name);
        return v;
    }

    std::shared_ptr<Impl::FakeField> MakeField(const char* name) {
        auto field = std::make_shared<Impl::FakeField>(compilation);
        field->SetName(name);
        field->SetDeclaringType(Known(TS::KnownTypeCode::Object));
        field->SetReturnType(Known(TS::KnownTypeCode::Int32));
        return field;
    }

    // An accessor FakeMethod wired to `property` through the IProperty-path
    // IMember view (the AsPropertyMember convention -- the view
    // `SetAccessorOwner` stores and the walk hands to the path elements).
    std::shared_ptr<Impl::FakeMethod> MakeAccessor(
        const char* name, TS::MethodSemanticsAttributes kind,
        const Impl::FakeProperty& property) {
        auto method = std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetDeclaringType(Known(TS::KnownTypeCode::Object));
        method->SetReturnType(Known(TS::KnownTypeCode::Int32));
        method->SetAccessorKind(kind);
        method->SetAccessorOwner(static_cast<const TS::IMember*>(
            static_cast<const TS::IProperty*>(&property)));
        return method;
    }

    // A plain (non-accessor) method.
    std::shared_ptr<Impl::FakeMethod> MakeMethod(const char* name, bool isStatic) {
        auto method = std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Method);
        method->SetName(name);
        method->SetDeclaringType(Known(TS::KnownTypeCode::Object));
        method->SetIsStatic(isStatic);
        method->SetReturnType(Known(TS::KnownTypeCode::Void));
        return method;
    }

    // A property with a live setter (the sink keeps the setter method alive).
    std::shared_ptr<Impl::FakeProperty> MakeProperty(const char* name, bool withSetter) {
        auto property = std::make_shared<Impl::FakeProperty>(compilation);
        property->SetName(name);
        property->SetDeclaringType(Known(TS::KnownTypeCode::Object));
        property->SetReturnType(Known(TS::KnownTypeCode::Int32));
        property->SetAccessibility(TS::Accessibility::Public);
        if (withSetter) {
            auto setter = std::make_shared<Impl::FakeMethod>(compilation, TS::SymbolKind::Method);
            setter->SetName("set_Item");
            setter->SetDeclaringType(Known(TS::KnownTypeCode::Object));
            setter->SetReturnType(Known(TS::KnownTypeCode::Void));
            setter->SetAccessibility(TS::Accessibility::Public);
            methodSink.push_back(setter);
            property->SetSetter(setter.get());
        }
        return property;
    }

    std::unique_ptr<LdFlda> FieldAddress(std::unique_ptr<IL::ILInstruction> target,
                                         const std::shared_ptr<Impl::FakeField>& field,
                                         bool readOnly = false) {
        auto ldflda = std::make_unique<LdFlda>(std::move(target), std::string("F"));
        ldflda->Field = field;
        ldflda->FieldIsReadOnly = readOnly;
        return ldflda;
    }
};

const TS::IMember* AsMethodMember(const Impl::FakeMethod& method) {
    return static_cast<const TS::IMember*>(static_cast<const TS::IMethod*>(&method));
}

// The `IMember` view of a fake field matching what the walk stores
// (`LdFlda::Field.get()` is an `IField*`, so the implicit conversion takes the
// IField-path subobject -- the direct `FakeField* -> IMember*` cast is
// ambiguous through the diamond).
const TS::IMember* AsFieldMember(const Impl::FakeField& field) {
    return static_cast<const TS::IMember*>(static_cast<const TS::IField*>(&field));
}

} // namespace

// ---- The field-store walk ----

TEST(AccessPathElementTest, StObjOverLdFldaOfLdLocIsASetterPath) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto field = f.MakeField("F");

    // stobj T(ldflda F(ldloc v), ldc.i4 1)
    auto ldflda = f.FieldAddress(std::make_unique<LdLoc>(v), field);
    StObj store(std::move(ldflda), std::make_unique<LdcI4>(1),
                f.Known(TS::KnownTypeCode::Int32));

    auto info = AccessPathElement::GetAccessPath(&store, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
    ASSERT_EQ(info.Path.size(), 1u);
    EXPECT_EQ(info.Path[0].ElementOpCode, IL::OpCode::StObj);
    EXPECT_EQ(info.Path[0].Member, AsFieldMember(*field));
    EXPECT_FALSE(info.Path[0].Indices.has_value());
    ASSERT_TRUE(info.Values.has_value());
    ASSERT_EQ(info.Values->size(), 1u);
    EXPECT_EQ((*info.Values)[0], store.Value.get());
    EXPECT_EQ(info.Target, v.get());
}

TEST(AccessPathElementTest, NestedLdFldaChainBuildsRootToLeafPath) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto outer = f.MakeField("Outer");
    auto inner = f.MakeField("Inner");

    // stobj T(ldflda Outer(ldflda Inner(ldloc v)), ldc.i4 1)
    auto innerAddr = f.FieldAddress(std::make_unique<LdLoc>(v), inner);
    auto outerAddr = f.FieldAddress(std::move(innerAddr), outer);
    StObj store(std::move(outerAddr), std::make_unique<LdcI4>(1),
                f.Known(TS::KnownTypeCode::Int32));

    auto info = AccessPathElement::GetAccessPath(&store, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
    ASSERT_EQ(info.Path.size(), 2u);
    // Root-to-leaf order: the inner (root-most) field first.
    EXPECT_EQ(info.Path[0].Member, AsFieldMember(*inner));
    EXPECT_EQ(info.Path[1].Member, AsFieldMember(*outer));
    EXPECT_EQ(info.Target, v.get());
}

// ---- The ldobj arms ----

TEST(AccessPathElementTest, LdObjOutermostBuildsPathWithInvalidKind) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto field = f.MakeField("F");

    // ldobj T2(ldflda roF(ldloc v)) -- readonly does NOT gate here (kind != Setter).
    auto ldflda = f.FieldAddress(std::make_unique<LdLoc>(v), field, /*readOnly=*/true);
    LdObj ldobj(std::move(ldflda), f.Known(TS::KnownTypeCode::Int32));

    auto info = AccessPathElement::GetAccessPath(&ldobj, nullptr);
    // No values were decided (the ldobj is not a store), so the kind stays
    // Invalid while the path and target are still collected.
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
    ASSERT_EQ(info.Path.size(), 1u);
    EXPECT_EQ(info.Path[0].Member, AsFieldMember(*field));
    EXPECT_EQ(info.Target, v.get());
    EXPECT_FALSE(info.Values.has_value());
}

TEST(AccessPathElementTest, ReadOnlyFieldUnderSetterPathIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto f2 = f.MakeField("F2");
    auto ro = f.MakeField("roF");

    // stobj T(ldflda F2(ldobj T2(ldflda roF(ldloc v))), ldc.i4 1)
    auto roAddr = f.FieldAddress(std::make_unique<LdLoc>(v), ro, /*readOnly=*/true);
    auto ldobj = std::make_unique<LdObj>(std::move(roAddr),
                                         f.Known(TS::KnownTypeCode::Int32));
    auto f2Addr = f.FieldAddress(std::move(ldobj), f2);
    StObj store(std::move(f2Addr), std::make_unique<LdcI4>(1),
                f.Known(TS::KnownTypeCode::Int32));

    auto info = AccessPathElement::GetAccessPath(&store, nullptr);
    // The stobj set kind = Setter, so the readonly-field ldobj gate fires.
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

TEST(AccessPathElementTest, MutableFieldUnderSetterPathPasses) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto f2 = f.MakeField("F2");
    auto inner = f.MakeField("Inner");

    auto innerAddr = f.FieldAddress(std::make_unique<LdLoc>(v), inner, /*readOnly=*/false);
    auto ldobj = std::make_unique<LdObj>(std::move(innerAddr),
                                         f.Known(TS::KnownTypeCode::Int32));
    auto f2Addr = f.FieldAddress(std::move(ldobj), f2);
    StObj store(std::move(f2Addr), std::make_unique<LdcI4>(1),
                f.Known(TS::KnownTypeCode::Int32));

    auto info = AccessPathElement::GetAccessPath(&store, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
    ASSERT_EQ(info.Path.size(), 2u);
    EXPECT_EQ(info.Path[0].Member, AsFieldMember(*inner));
    EXPECT_EQ(info.Path[1].Member, AsFieldMember(*f2));
    EXPECT_EQ(info.Target, v.get());
}

TEST(AccessPathElementTest, LdObjIfRefOverLdLocaSetsTarget) {
    Fixture f;
    auto v = f.MakeVar("target");

    // ldobj_ifref T(ldloca v)
    LdObjIfRef ldobjIfRef(std::make_unique<LdLoca>(v), f.Known(TS::KnownTypeCode::Int32));

    auto info = AccessPathElement::GetAccessPath(&ldobjIfRef, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
    EXPECT_TRUE(info.Path.empty());
    EXPECT_EQ(info.Target, v.get());
}

// ---- The call arms ----

TEST(AccessPathElementTest, PlainSetterCallBuildsPropertyElementWithEmptyIndices) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto property = f.MakeProperty("P", /*withSetter=*/true);
    auto setter = f.MakeAccessor("set_P", TS::MethodSemanticsAttributes::Setter, *property);

    // call set_P(ldloc v, ldc.i4 1)
    Call call("N::set_P");
    call.Method = setter;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
    ASSERT_EQ(info.Path.size(), 1u);
    EXPECT_EQ(info.Path[0].ElementOpCode, IL::OpCode::Call);
    // The element member is the AccessorOwner view the method carries.
    EXPECT_EQ(info.Path[0].Member, setter->AccessorOwner());
    // A plain property accessor call carries an EMPTY (never null) indices array.
    ASSERT_TRUE(info.Path[0].Indices.has_value());
    EXPECT_TRUE(info.Path[0].Indices->empty());
    ASSERT_TRUE(info.Values.has_value());
    ASSERT_EQ(info.Values->size(), 1u);
    EXPECT_EQ((*info.Values)[0], call.Arguments[1].get());
    EXPECT_EQ(info.Target, v.get());
}

TEST(AccessPathElementTest, IndexerSetterCallCarriesIndicesAndMarksIndexVariables) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto key = f.MakeVar("key");
    auto property = f.MakeProperty("Item", /*withSetter=*/true);
    property->SetIsIndexer(true);
    auto setter = f.MakeAccessor("set_Item", TS::MethodSemanticsAttributes::Setter, *property);

    // call set_Item(ldloc v, ldloc key, ldc.i4 1)
    Call call("N::set_Item");
    call.Method = setter;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdLoc>(key));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
    ASSERT_EQ(info.Path.size(), 1u);
    ASSERT_TRUE(info.Path[0].Indices.has_value());
    ASSERT_EQ(info.Path[0].Indices->size(), 1u);
    EXPECT_EQ((*info.Path[0].Indices)[0], call.Arguments[1].get());
    ASSERT_EQ(info.UsedIndexVariables.size(), 1u);
    EXPECT_EQ(info.UsedIndexVariables[0], key.get());
    ASSERT_TRUE(info.Values.has_value());
    EXPECT_EQ((*info.Values)[0], call.Arguments[2].get());
    EXPECT_EQ(info.Target, v.get());
}

TEST(AccessPathElementTest, GetterCallTakesAllArgumentsAsIndices) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto key = f.MakeVar("key");
    auto property = f.MakeProperty("Item", /*withSetter=*/false);
    property->SetIsIndexer(true);
    auto getter = f.MakeAccessor("get_Item", TS::MethodSemanticsAttributes::Getter, *property);

    // call get_Item(ldloc v, ldloc key)
    Call call("N::get_Item");
    call.Method = getter;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdLoc>(key));

    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    // The C# quirk: a getter as the outermost accessor still decides
    // kind = Setter with values = { Arguments.Last() } -- the LAST INDEX, not
    // a value (the getter shape never reaches this walk through the real
    // initializer pipeline).
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
    ASSERT_TRUE(info.Path[0].Indices.has_value());
    ASSERT_EQ(info.Path[0].Indices->size(), 1u);
    EXPECT_EQ((*info.Path[0].Indices)[0], call.Arguments[1].get());
    ASSERT_TRUE(info.Values.has_value());
    ASSERT_EQ(info.Values->size(), 1u);
    EXPECT_EQ((*info.Values)[0], call.Arguments[1].get());
    EXPECT_EQ(info.Target, v.get());
}

TEST(AccessPathElementTest, AddCallIsAnAdderPath) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto add = f.MakeMethod("Add", /*isStatic=*/false);

    // call Add(ldloc v, ldc.i4 1)
    Call call("N::Add");
    call.Method = add;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Adder);
    ASSERT_EQ(info.Path.size(), 1u);
    EXPECT_EQ(info.Path[0].ElementOpCode, IL::OpCode::Call);
    EXPECT_EQ(info.Path[0].Member, AsMethodMember(*add));
    EXPECT_FALSE(info.Path[0].Indices.has_value());
    ASSERT_TRUE(info.Values.has_value());
    ASSERT_EQ(info.Values->size(), 1u);
    EXPECT_EQ((*info.Values)[0], call.Arguments[1].get());
    EXPECT_EQ(info.Target, v.get());
}

TEST(AccessPathElementTest, AddCallWithOnlyReceiverIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto add = f.MakeMethod("Add", /*isStatic=*/false);

    Call call("N::Add");
    call.Method = add;
    call.AddArg(std::make_unique<LdLoc>(v));

    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

TEST(AccessPathElementTest, NewObjCallIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto add = f.MakeMethod("Add", /*isStatic=*/false);

    Call call("N::Add");
    call.Method = add;
    call.IsNewObj = true;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

TEST(AccessPathElementTest, UnresolvedMethodCallIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");

    Call call("N::Add");  // Method left unset (the port's seed-path shape)
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

TEST(AccessPathElementTest, NestedPropertySetterPathWithSettableInner) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto inner = f.MakeProperty("B", /*withSetter=*/true);
    auto getB = f.MakeAccessor("get_B", TS::MethodSemanticsAttributes::Getter, *inner);
    auto outer = f.MakeProperty("A", /*withSetter=*/true);
    auto setA = f.MakeAccessor("set_A", TS::MethodSemanticsAttributes::Setter, *outer);

    // call set_A(call get_B(ldloc v), ldc.i4 1)
    Call call("N::set_A");
    call.Method = setA;
    auto innerCall = std::make_unique<Call>("N::get_B");
    innerCall->Method = getB;
    innerCall->AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::move(innerCall));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
    ASSERT_EQ(info.Path.size(), 2u);
    // Root-to-leaf: B first, then A.
    EXPECT_EQ(info.Path[0].Member, getB->AccessorOwner());
    EXPECT_EQ(info.Path[1].Member, setA->AccessorOwner());
    EXPECT_EQ(info.Target, v.get());
}

TEST(AccessPathElementTest, GetOnlyInnerPropertyUnderSetterIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto inner = f.MakeProperty("B", /*withSetter=*/false);
    auto getB = f.MakeAccessor("get_B", TS::MethodSemanticsAttributes::Getter, *inner);
    auto outer = f.MakeProperty("A", /*withSetter=*/true);
    auto setA = f.MakeAccessor("set_A", TS::MethodSemanticsAttributes::Setter, *outer);

    Call call("N::set_A");
    call.Method = setA;
    auto innerCall = std::make_unique<Call>("N::get_B");
    innerCall->Method = getB;
    innerCall->AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::move(innerCall));
    call.AddArg(std::make_unique<LdcI4>(1));

    // The outer setter set kind = Setter, so the get-only inner property fails
    // CanBeUsedInInitializer.
    auto info = AccessPathElement::GetAccessPath(&call, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

// ---- The values-referencing-target guard ----

TEST(AccessPathElementTest, ValueContainingTargetLoadInvalidates) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto field = f.MakeField("F");

    // stobj T(ldflda F(ldloc v), call M(ldloc v)) -- the value's strict
    // descendants load the target variable.
    auto ldflda = f.FieldAddress(std::make_unique<LdLoc>(v), field);
    auto valueCall = std::make_unique<Call>("N::M");
    valueCall->AddArg(std::make_unique<LdLoc>(v));
    StObj store(std::move(ldflda), std::move(valueCall), f.Known(TS::KnownTypeCode::Int32));

    auto info = AccessPathElement::GetAccessPath(&store, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

TEST(AccessPathElementTest, ValueBeingTargetLoadItselfStaysSetter) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto field = f.MakeField("F");

    // stobj T(ldflda F(ldloc v), ldloc v) -- the C# Descendants walk EXCLUDES
    // the value itself, so a value that IS an ldloc of the target does not
    // invalidate (the pinned quirk).
    auto ldflda = f.FieldAddress(std::make_unique<LdLoc>(v), field);
    StObj store(std::move(ldflda), std::make_unique<LdLoc>(v),
                f.Known(TS::KnownTypeCode::Int32));

    auto info = AccessPathElement::GetAccessPath(&store, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
    EXPECT_EQ(info.Target, v.get());
}

// ---- The resolver-driven Add applicability matrix ----

TEST(AccessPathElementTest, StaticAddWithResolverIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto add = f.MakeMethod("Add", /*isStatic=*/true);
    CSharpResolver resolver(f.compilation);

    Call call("N::Add");
    call.Method = add;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(
        &call, f.Find(TS::KnownTypeCode::IEnumerable).get(), nullptr, &resolver);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

TEST(AccessPathElementTest, AddOnNonEnumerableRootTypeIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto add = f.MakeMethod("Add", /*isStatic=*/false);
    CSharpResolver resolver(f.compilation);

    Call call("N::Add");
    call.Method = add;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    // The LdLoc receiver yields no type, so the root type (Int32) decides.
    auto info = AccessPathElement::GetAccessPath(
        &call, f.Find(TS::KnownTypeCode::Int32).get(), nullptr, &resolver);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

TEST(AccessPathElementTest, AddOnEnumerableRootTypeIsAdder) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto add = f.MakeMethod("Add", /*isStatic=*/false);
    CSharpResolver resolver(f.compilation);

    Call call("N::Add");
    call.Method = add;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(
        &call, f.Find(TS::KnownTypeCode::IEnumerable).get(), nullptr, &resolver);
    EXPECT_EQ(info.Kind, AccessPathKind::Adder);
    ASSERT_EQ(info.Path.size(), 1u);
    EXPECT_EQ(info.Path[0].Member, AsMethodMember(*add));
    EXPECT_EQ(info.Target, v.get());
}

TEST(AccessPathElementTest, AccessorCallSkipsAddApplicabilityChecks) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto property = f.MakeProperty("P", /*withSetter=*/true);
    auto setter = f.MakeAccessor("set_P", TS::MethodSemanticsAttributes::Setter, *property);
    CSharpResolver resolver(f.compilation);

    // An accessor-owned method is applicable regardless of the name or the
    // receiver's IEnumerable-ness (IsMethodApplicable's early return).
    Call call("N::set_P");
    call.Method = setter;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(
        &call, f.Find(TS::KnownTypeCode::Int32).get(), nullptr, &resolver);
    EXPECT_EQ(info.Kind, AccessPathKind::Setter);
}

TEST(AccessPathElementTest, DictionaryInitializersOffWithIndicesIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto key = f.MakeVar("key");
    auto property = f.MakeProperty("Item", /*withSetter=*/true);
    property->SetIsIndexer(true);
    auto setter = f.MakeAccessor("set_Item", TS::MethodSemanticsAttributes::Setter, *property);
    DecompilerSettings settings;

    Call call("N::set_Item");
    call.Method = setter;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdLoc>(key));
    call.AddArg(std::make_unique<LdcI4>(1));

    settings.SetDictionaryInitializers(false);
    auto info = AccessPathElement::GetAccessPath(&call, nullptr, &settings, nullptr);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);

    // With the default (on) setting the same walk is a setter path.
    DecompilerSettings defaults;
    auto info2 = AccessPathElement::GetAccessPath(&call, nullptr, &defaults, nullptr);
    EXPECT_EQ(info2.Kind, AccessPathKind::Setter);
}

TEST(AccessPathElementTest, ExtensionAddWithSettingOffIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto add = f.MakeMethod("Add", /*isStatic=*/true);
    add->SetIsExtensionMethod(true);
    CSharpResolver resolver(f.compilation);
    DecompilerSettings settings;
    settings.SetExtensionMethodsInCollectionInitializers(false);

    Call call("N::Add");
    call.Method = add;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    // The settings gate fires before the CanTransformToExtensionMethodCall
    // resolver check, so the walk is Invalid without any member-lookup
    // machinery running.
    auto info = AccessPathElement::GetAccessPath(
        &call, f.Find(TS::KnownTypeCode::IEnumerable).get(), &settings, &resolver);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

TEST(AccessPathElementTest, GenericAddWithUninferableTypeArgumentIsInvalid) {
    Fixture f;
    auto v = f.MakeVar("target");
    auto add = f.MakeMethod("Add", /*isStatic=*/false);

    // A generic Add whose parameter list gives no evidence for its type
    // parameter: type inference fails, so the method is not applicable.
    auto typeParameter = std::make_shared<TestSupport::LookupTypeParameter>("T");
    typeParameter->SetIndex(0);
    typeParameter->SetOwnerType(TS::SymbolKind::Method);
    add->SetTypeParameters({typeParameter});
    std::vector<std::shared_ptr<const TS::IParameter>> parameters;
    parameters.push_back(std::make_shared<Impl::DefaultParameter>(
        f.Known(TS::KnownTypeCode::Int32), "p"));
    add->SetParameters(parameters);
    CSharpResolver resolver(f.compilation);

    Call call("N::Add");
    call.Method = add;
    call.AddArg(std::make_unique<LdLoc>(v));
    call.AddArg(std::make_unique<LdcI4>(1));

    auto info = AccessPathElement::GetAccessPath(
        &call, f.Find(TS::KnownTypeCode::IEnumerable).get(), nullptr, &resolver);
    EXPECT_EQ(info.Kind, AccessPathKind::Invalid);
}

// ---- The Equals / GetHashCode / ToString surface ----

TEST(AccessPathElementTest, EqualsComparesMembersByReference) {
    Fixture f;
    auto field = f.MakeField("F");
    auto other = f.MakeField("F");

    AccessPathElement a(IL::OpCode::StObj, AsFieldMember(*field));
    AccessPathElement b(IL::OpCode::StObj, AsFieldMember(*field));
    AccessPathElement c(IL::OpCode::StObj, AsFieldMember(*other));

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_FALSE(a == c);
    // The opcode is not part of the C# equality.
    AccessPathElement d(IL::OpCode::LdFlda, AsFieldMember(*field));
    EXPECT_TRUE(a == d);
}

TEST(AccessPathElementTest, EqualsNullVsEmptyIndicesDiffer) {
    Fixture f;
    auto field = f.MakeField("F");
    const TS::IMember* member = AsFieldMember(*field);

    AccessPathElement nullIndices(IL::OpCode::LdFlda, member);
    AccessPathElement emptyIndices(IL::OpCode::Call, member, std::vector<IL::ILInstruction*>{});

    // The C# reference-compares the arrays first: null vs empty is UNEQUAL
    // (and the SequenceEqual arm requires both non-null).
    EXPECT_FALSE(nullIndices == emptyIndices);
    EXPECT_TRUE(nullIndices == nullIndices);
    EXPECT_TRUE(emptyIndices == emptyIndices);
}

TEST(AccessPathElementTest, EqualsIndicesReferenceAndStructuralForms) {
    Fixture f;
    auto field = f.MakeField("F");
    const TS::IMember* member = AsFieldMember(*field);

    LdcI4 first(1);
    LdcI4 second(1);
    LdcI4 third(2);
    LdStr str("x");

    AccessPathElement a(IL::OpCode::Call, member, std::vector<IL::ILInstruction*>{&first});
    AccessPathElement sameRef(IL::OpCode::Call, member, std::vector<IL::ILInstruction*>{&first});
    AccessPathElement sameValue(IL::OpCode::Call, member, std::vector<IL::ILInstruction*>{&second});
    AccessPathElement differentValue(IL::OpCode::Call, member,
                                     std::vector<IL::ILInstruction*>{&third});
    AccessPathElement differentLength(IL::OpCode::Call, member,
                                      std::vector<IL::ILInstruction*>{&first, &str});

    EXPECT_TRUE(a == sameRef);
    // Distinct-but-pure structurally-equal constants match (the
    // ILInstructionMatchComparer's structural arm).
    EXPECT_TRUE(a == sameValue);
    EXPECT_FALSE(a == differentValue);
    EXPECT_FALSE(a == differentLength);
}

TEST(AccessPathElementTest, EqualsImpureIndicesDiffer) {
    Fixture f;
    auto field = f.MakeField("F");
    const TS::IMember* member = AsFieldMember(*field);

    // Two distinct impure calls never match (the comparer's IsPure gate).
    Call first("N::M");
    first.AddArg(std::make_unique<LdcI4>(1));
    Call second("N::M");
    second.AddArg(std::make_unique<LdcI4>(1));

    AccessPathElement a(IL::OpCode::Call, member, std::vector<IL::ILInstruction*>{&first});
    AccessPathElement b(IL::OpCode::Call, member, std::vector<IL::ILInstruction*>{&second});

    EXPECT_FALSE(a == b);
    EXPECT_EQ(a.Equals(b), a == b);
}

TEST(AccessPathElementTest, GetHashCodeFollowsMemberIdentity) {
    Fixture f;
    auto field = f.MakeField("F");
    auto other = f.MakeField("F");

    AccessPathElement a(IL::OpCode::StObj, AsFieldMember(*field));
    AccessPathElement b(IL::OpCode::LdFlda, AsFieldMember(*field));
    AccessPathElement c(IL::OpCode::StObj, AsFieldMember(*other));

    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
    EXPECT_NE(a.GetHashCode(), c.GetHashCode());
}

TEST(AccessPathElementTest, ToStringRendersMemberAndArrayQuirk) {
    Fixture f;
    auto field = f.MakeField("F");
    // FakeField's FullName is the declaring type's full name + the name
    // (the FakeMember fixture shape over Known(Object), whose wrapper
    // full name is the bare metadata name).
    AccessPathElement a(IL::OpCode::StObj, AsFieldMember(*field));
    EXPECT_EQ(a.ToString(), std::string("[Object.F, ]"));

    AccessPathElement b(IL::OpCode::Call, AsFieldMember(*field),
                        std::vector<IL::ILInstruction*>{});
    // The C# interpolates the ARRAY (rendering its type name), not the elements.
    EXPECT_EQ(b.ToString(), std::string("[Object.F, ILInstruction[]]"));
}
