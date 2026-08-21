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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for the TypeVisitor dispatch infrastructure (D406) -- the faithful port
// of ICSharpCode.Decompiler/TypeSystem/TypeVisitor.cs plus the IType::AcceptVisitor
// / VisitChildren virtuals and the per-concrete-type overrides. This lands the
// visitor dispatch the four concrete IType VisitChildren types (ModifiedType D401 /
// NullabilityAnnotatedType D402 / FunctionPointerType D404 / TupleType D405) were
// ported toward; TypeParameterSubstitution (the first concrete TypeVisitor) lands
// next. The tests pin: (a) AcceptVisitor dispatches to the matching Visit* method
// (a recording visitor); (b) the Visit* defaults recurse via VisitChildren and the
// "has children" types reconstruct (via make_shared) iff a child changed, else
// return the same shared_ptr (the C# `return this` reference-identity, via
// enable_shared_from_this); (c) the nested recursion reaches inner children; (d)
// the "no children" types (KnownType / SimpleType / SpecialType / TypeParameter)
// and the C++-only types inherit the VisitOtherType / identity defaults.

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::ByReferenceType;
using ILSpy::Decompiler::TypeSystem::FunctionPointerType;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ModifiedType;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedType;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SignatureCallingConvention;
using ILSpy::Decompiler::TypeSystem::SimpleType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TupleType;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameter;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;

namespace {

// A TypeVisitor that records which Visit* method was dispatched to, while still
// recursing via VisitChildren (so the tree stays intact and the recording does
// not short-circuit the dispatch). VisitTypeDefinition / VisitTypeParameter are
// not recorded (no concrete ITypeDefinition / ITypeParameter dispatches to them
// in the minimal port; the test stubs inherit the VisitOtherType default).
class RecordingVisitor : public TypeVisitor {
public:
    std::vector<std::string> calls;

    ITypePtr VisitParameterizedType(ParameterizedType& t) override {
        calls.push_back("VisitParameterizedType");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitArrayType(ArrayType& t) override {
        calls.push_back("VisitArrayType");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitPointerType(PointerType& t) override {
        calls.push_back("VisitPointerType");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitByReferenceType(ByReferenceType& t) override {
        calls.push_back("VisitByReferenceType");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitTupleType(TupleType& t) override {
        calls.push_back("VisitTupleType");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitOtherType(IType& t) override {
        calls.push_back("VisitOtherType");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitModReq(ModifiedType& t) override {
        calls.push_back("VisitModReq");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitModOpt(ModifiedType& t) override {
        calls.push_back("VisitModOpt");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitNullabilityAnnotatedType(NullabilityAnnotatedType& t) override {
        calls.push_back("VisitNullabilityAnnotatedType");
        return t.VisitChildren(*this);
    }
    ITypePtr VisitFunctionPointerType(FunctionPointerType& t) override {
        calls.push_back("VisitFunctionPointerType");
        return t.VisitChildren(*this);
    }
};

// A TypeVisitor that substitutes KnownType(Int32) -> KnownType(String) at the
// leaves (via VisitOtherType) and otherwise recurses via VisitChildren. This
// exercises the "has children" types' VisitChildren reconstruction: the generic
// type (a SimpleType or non-Int32 KnownType) recurses to identity, the Int32
// child is replaced, and the parent reconstructs with the new child.
class Int32ToStringVisitor : public TypeVisitor {
public:
    ITypePtr VisitOtherType(IType& t) override {
        if (auto* kt = dynamic_cast<KnownType*>(&t)) {
            if (kt->Code() == KnownTypeCode::Int32)
                return std::make_shared<KnownType>(KnownTypeCode::String);
        }
        return t.VisitChildren(*this);
    }
};

// A no-op TypeVisitor (inherits all defaults): every Visit* recurses via
// VisitChildren and returns the same shared_ptr when nothing changes.
class NoOpVisitor : public TypeVisitor {};

} // namespace

// ---- AcceptVisitor dispatch (the matching Visit* method is called) ----

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitParameterizedType) {
    auto gen = std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic", "List`1", 1), TypeKind::Class);
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{std::make_shared<KnownType>(KnownTypeCode::Int32)});
    RecordingVisitor v;
    pt->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitParameterizedType");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitArrayType) {
    auto arr = std::make_shared<ArrayType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    RecordingVisitor v;
    arr->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitArrayType");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitByReferenceType) {
    auto br = std::make_shared<ByReferenceType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    RecordingVisitor v;
    br->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitByReferenceType");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitPointerType) {
    auto ptr = std::make_shared<PointerType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    RecordingVisitor v;
    ptr->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitPointerType");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitOtherTypeForKnownType) {
    auto kt = std::make_shared<KnownType>(KnownTypeCode::Int32);
    RecordingVisitor v;
    kt->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitOtherType");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitModReqForRequiredModifiedType) {
    auto modReq = std::make_shared<ModifiedType>(
        std::make_shared<KnownType>(KnownTypeCode::Object),
        std::make_shared<KnownType>(KnownTypeCode::Int32),
        /*isRequired=*/true);
    RecordingVisitor v;
    modReq->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitModReq");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitModOptForOptionalModifiedType) {
    auto modOpt = std::make_shared<ModifiedType>(
        std::make_shared<KnownType>(KnownTypeCode::Object),
        std::make_shared<KnownType>(KnownTypeCode::Int32),
        /*isRequired=*/false);
    RecordingVisitor v;
    modOpt->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitModOpt");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitNullabilityAnnotatedType) {
    auto nat = std::make_shared<NullabilityAnnotatedType>(
        std::make_shared<KnownType>(KnownTypeCode::String), Nullability::Nullable);
    RecordingVisitor v;
    nat->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitNullabilityAnnotatedType");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitFunctionPointerType) {
    auto fpt = std::make_shared<FunctionPointerType>(
        SignatureCallingConvention::Default, std::vector<ITypePtr>{},
        std::make_shared<KnownType>(KnownTypeCode::Void), /*returnIsRefReadOnly=*/false,
        std::vector<ITypePtr>{std::make_shared<KnownType>(KnownTypeCode::Int32)},
        std::vector<ReferenceKind>{ReferenceKind::None});
    RecordingVisitor v;
    fpt->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitFunctionPointerType");
}

TEST(TypeVisitorTest, AcceptVisitorDispatchesToVisitTupleType) {
    auto underlying = std::make_shared<SimpleType>(TopLevelTypeName("System", "ValueTuple`2", 2), TypeKind::Struct);
    auto tup = std::make_shared<TupleType>(underlying,
        std::vector<ITypePtr>{std::make_shared<KnownType>(KnownTypeCode::Int32),
                              std::make_shared<KnownType>(KnownTypeCode::String)},
        std::vector<std::string>{"x", "y"});
    RecordingVisitor v;
    tup->AcceptVisitor(v);
    ASSERT_FALSE(v.calls.empty());
    EXPECT_EQ(v.calls.front(), "VisitTupleType");
}

// ---- VisitChildren: identity when no child changes (the C# `return this`) ----

TEST(TypeVisitorTest, VisitChildrenReturnsSameSharedPtrWhenNoChangeParameterized) {
    auto gen = std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic", "List`1", 1), TypeKind::Class);
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{std::make_shared<KnownType>(KnownTypeCode::Int32)});
    NoOpVisitor v;
    ITypePtr result = pt->AcceptVisitor(v);
    EXPECT_EQ(result.get(), pt.get());
}

TEST(TypeVisitorTest, VisitChildrenReturnsSameSharedPtrWhenNoChangeArray) {
    auto arr = std::make_shared<ArrayType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    NoOpVisitor v;
    ITypePtr result = arr->AcceptVisitor(v);
    EXPECT_EQ(result.get(), arr.get());
}

TEST(TypeVisitorTest, VisitChildrenReturnsSameSharedPtrWhenNoChangeByReference) {
    auto br = std::make_shared<ByReferenceType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    NoOpVisitor v;
    ITypePtr result = br->AcceptVisitor(v);
    EXPECT_EQ(result.get(), br.get());
}

TEST(TypeVisitorTest, VisitChildrenReturnsSameSharedPtrWhenNoChangePointer) {
    auto ptr = std::make_shared<PointerType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
    NoOpVisitor v;
    ITypePtr result = ptr->AcceptVisitor(v);
    EXPECT_EQ(result.get(), ptr.get());
}

// ---- VisitChildren: reconstruction when a child changes ----

TEST(TypeVisitorTest, VisitChildrenReconstructsParameterizedTypeWithSubstitutedChild) {
    auto gen = std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic", "List`1", 1), TypeKind::Class);
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{int32});
    Int32ToStringVisitor v;
    ITypePtr result = pt->AcceptVisitor(v);
    // A new object (reconstructed), distinct from the original.
    EXPECT_NE(result.get(), pt.get());
    // Still a ParameterizedType wrapping the same generic type, but with String.
    ASSERT_EQ(result->Kind(), TypeKind::Class);
    auto* rpt = static_cast<ParameterizedType*>(result.get());
    ASSERT_EQ(rpt->TypeArguments().size(), 1u);
    EXPECT_EQ(rpt->TypeArguments()[0]->Kind(), TypeKind::Class);  // String is a class
    EXPECT_NE(rpt->TypeArguments()[0].get(), int32.get());
}

TEST(TypeVisitorTest, VisitChildrenReconstructsArrayTypeWithSubstitutedElement) {
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto arr = std::make_shared<ArrayType>(int32);
    Int32ToStringVisitor v;
    ITypePtr result = arr->AcceptVisitor(v);
    EXPECT_NE(result.get(), arr.get());
    ASSERT_EQ(result->Kind(), TypeKind::Array);
    auto* rarr = static_cast<ArrayType*>(result.get());
    EXPECT_EQ(rarr->Element()->Kind(), TypeKind::Class);  // String
    EXPECT_NE(rarr->Element().get(), int32.get());
}

TEST(TypeVisitorTest, VisitChildrenReconstructsByReferenceTypeWithSubstitutedElement) {
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto br = std::make_shared<ByReferenceType>(int32);
    Int32ToStringVisitor v;
    ITypePtr result = br->AcceptVisitor(v);
    EXPECT_NE(result.get(), br.get());
    ASSERT_EQ(result->Kind(), TypeKind::ByReference);
    auto* rbr = static_cast<ByReferenceType*>(result.get());
    EXPECT_EQ(rbr->Element()->Kind(), TypeKind::Class);
}

TEST(TypeVisitorTest, VisitChildrenReconstructsPointerTypeWithSubstitutedElement) {
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto ptr = std::make_shared<PointerType>(int32);
    Int32ToStringVisitor v;
    ITypePtr result = ptr->AcceptVisitor(v);
    EXPECT_NE(result.get(), ptr.get());
    ASSERT_EQ(result->Kind(), TypeKind::Pointer);
    auto* rptr = static_cast<PointerType*>(result.get());
    EXPECT_EQ(rptr->Element()->Kind(), TypeKind::Class);
}

// ---- VisitChildren: nested recursion reaches inner children ----

TEST(TypeVisitorTest, VisitChildrenRecursesIntoNestedParameterizedType) {
    auto listGen = std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic", "List`1", 1), TypeKind::Class);
    auto dictGen = std::make_shared<SimpleType>(TopLevelTypeName("System.Collections.Generic", "Dictionary`2", 2), TypeKind::Class);
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto str = std::make_shared<KnownType>(KnownTypeCode::String);
    auto inner = std::make_shared<ParameterizedType>(dictGen, std::vector<ITypePtr>{int32, str});
    auto outer = std::make_shared<ParameterizedType>(listGen, std::vector<ITypePtr>{inner});
    Int32ToStringVisitor v;
    ITypePtr result = outer->AcceptVisitor(v);
    ASSERT_NE(result.get(), outer.get());
    auto* router = static_cast<ParameterizedType*>(result.get());
    ASSERT_EQ(router->TypeArguments().size(), 1u);
    auto* rinner = static_cast<ParameterizedType*>(router->TypeArguments()[0].get());
    ASSERT_EQ(rinner->TypeArguments().size(), 2u);
    // The inner Int32 was substituted to String; the inner String was left as-is.
    EXPECT_EQ(rinner->TypeArguments()[0]->Kind(), TypeKind::Class);  // Int32 -> String
    EXPECT_EQ(rinner->TypeArguments()[1]->Kind(), TypeKind::Class);  // String unchanged
}

// ---- VisitChildren: reconstruction for the remaining "has children" types ----

TEST(TypeVisitorTest, VisitChildrenReconstructsTupleTypeWithSubstitutedElement) {
    auto underlying = std::make_shared<SimpleType>(TopLevelTypeName("System", "ValueTuple`2", 2), TypeKind::Struct);
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto str = std::make_shared<KnownType>(KnownTypeCode::String);
    auto tup = std::make_shared<TupleType>(underlying,
        std::vector<ITypePtr>{int32, str}, std::vector<std::string>{"x", "y"});
    Int32ToStringVisitor v;
    ITypePtr result = tup->AcceptVisitor(v);
    EXPECT_NE(result.get(), tup.get());
    ASSERT_EQ(result->Kind(), TypeKind::Tuple);
    auto* rtup = static_cast<TupleType*>(result.get());
    ASSERT_EQ(rtup->ElementTypes().size(), 2u);
    EXPECT_EQ(rtup->ElementTypes()[0]->Kind(), TypeKind::Class);  // Int32 -> String
    EXPECT_EQ(rtup->ElementTypes()[1]->Kind(), TypeKind::Class);  // String unchanged
    // Element names are carried over.
    ASSERT_EQ(rtup->ElementNames().size(), 2u);
    EXPECT_EQ(rtup->ElementNames()[0], "x");
    EXPECT_EQ(rtup->ElementNames()[1], "y");
}

TEST(TypeVisitorTest, VisitChildrenReconstructsModifiedTypeWithSubstitutedElement) {
    auto modifier = std::make_shared<KnownType>(KnownTypeCode::Object);
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto modReq = std::make_shared<ModifiedType>(modifier, int32, /*isRequired=*/true);
    Int32ToStringVisitor v;
    ITypePtr result = modReq->AcceptVisitor(v);
    EXPECT_NE(result.get(), modReq.get());
    ASSERT_EQ(result->Kind(), TypeKind::ModReq);
    auto* rmod = static_cast<ModifiedType*>(result.get());
    EXPECT_EQ(rmod->Element()->Kind(), TypeKind::Class);  // Int32 -> String
    // The modifier (Object, not Int32) is recursed to identity.
    EXPECT_EQ(rmod->Modifier().get(), modifier.get());
}

TEST(TypeVisitorTest, VisitChildrenReconstructsNullabilityAnnotatedTypeWithSubstitutedBase) {
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto nat = std::make_shared<NullabilityAnnotatedType>(int32, Nullability::Nullable);
    Int32ToStringVisitor v;
    ITypePtr result = nat->AcceptVisitor(v);
    EXPECT_NE(result.get(), nat.get());
    ASSERT_EQ(result->Kind(), TypeKind::Class);  // delegates to base (now String)
    auto* rnat = static_cast<NullabilityAnnotatedType*>(result.get());
    EXPECT_EQ(rnat->Nullability(), Nullability::Nullable);
    EXPECT_EQ(rnat->TypeWithoutAnnotation()->Kind(), TypeKind::Class);
}

TEST(TypeVisitorTest, VisitChildrenReconstructsFunctionPointerTypeWithSubstitutedReturnType) {
    auto int32 = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto str = std::make_shared<KnownType>(KnownTypeCode::String);
    auto fpt = std::make_shared<FunctionPointerType>(
        SignatureCallingConvention::Default, std::vector<ITypePtr>{},
        int32, /*returnIsRefReadOnly=*/false,
        std::vector<ITypePtr>{str},
        std::vector<ReferenceKind>{ReferenceKind::None});
    Int32ToStringVisitor v;
    ITypePtr result = fpt->AcceptVisitor(v);
    EXPECT_NE(result.get(), fpt.get());
    ASSERT_EQ(result->Kind(), TypeKind::FunctionPointer);
    auto* rfpt = static_cast<FunctionPointerType*>(result.get());
    // The return type (Int32) was substituted to String.
    EXPECT_EQ(rfpt->ReturnType()->Kind(), TypeKind::Class);
    // The parameter (String, not Int32) is recursed to identity.
    EXPECT_EQ(rfpt->ParameterTypes()[0].get(), str.get());
}

// ---- VisitChildren: the "no children" types return identity ----

TEST(TypeVisitorTest, NoChildrenTypesReturnIdentity) {
    NoOpVisitor v;
    auto kt = std::make_shared<KnownType>(KnownTypeCode::Int32);
    EXPECT_EQ(kt->AcceptVisitor(v).get(), kt.get());
    auto st = std::make_shared<SimpleType>(TopLevelTypeName("System", "Object", 0), TypeKind::Class);
    EXPECT_EQ(st->AcceptVisitor(v).get(), st.get());
    auto sp = std::make_shared<SpecialType>(TypeKind::Unknown);
    EXPECT_EQ(sp->AcceptVisitor(v).get(), sp.get());
    auto tp = std::make_shared<TypeParameter>(0, TypeParameter::OwnerKind::Class, "T");
    EXPECT_EQ(tp->AcceptVisitor(v).get(), tp.get());
}
