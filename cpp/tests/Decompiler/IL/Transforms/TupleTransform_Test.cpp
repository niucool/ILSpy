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

// Tests for TupleTransform (the port of
// ICSharpCode.Decompiler/IL/Transforms/TupleTransform.cs): the tuple
// field-access match (ItemN parsing + the Rest-chain flatten), the
// tuple-construction match (the flat 2-tuple), and the abort cases (a
// non-tuple declaring type / a non-Item field name).

#include "Decompiler/IL/Transforms/TupleTransform.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/TypeSystem/Implementation/MinimalCorlib.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace IL = ::ILSpy::Decompiler::IL;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The fixture: the minimal-corlib compilation (the
// TransformCollectionAndObjectInitializers_Test precedent) + the
// System.ValueTuple`2 struct type the matches resolve against.
struct TupleFixture {
    TS::SimpleCompilation compilation{TS::Implementation::MinimalCorlib::Instance(), {}};
    std::shared_ptr<TS::TestSupport::LookupTypeDefinition> valueTuple2Def =
        std::make_shared<TS::TestSupport::LookupTypeDefinition>(
            "ValueTuple", "System",
            TS::FullTypeName("System.ValueTuple`2"),
            TS::TypeKind::Struct, TS::Accessibility::Public, compilation, nullptr);
    TS::ITypePtr valueTuple2 = valueTuple2Def;
    TS::ITypePtr int32Type =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Int32);
};

using ILVariablePtr = std::shared_ptr<IL::ILVariable>;

ILVariablePtr MakeLocal(std::string name, TS::ITypePtr type)
{
    auto v =
        std::make_shared<IL::ILVariable>(IL::VariableKind::Local, std::move(type));
    v->Name = std::move(name);
    return v;
}


// The `System.ValueTuple`2::.ctor(T1, T2)` stub: a value-type ctor whose
// DeclaringType is the tuple-compatible struct (the IsTupleCompatible gate).
class TupleCtorStub : public TS::IMethod {
public:
    explicit TupleCtorStub(TS::ITypePtr declaringType)
        : declaringType_(std::move(declaringType)) {}

    // --- ISymbol / INamedElement ---
    TS::SymbolKind SymbolKind() const override { return TS::SymbolKind::Method; }
    std::string Name() const override { return ".ctor"; }
    std::string FullName() const override { return "System.ValueTuple`2..ctor"; }
    std::string ReflectionName() const override { return ".ctor"; }
    std::string Namespace() const override { return "System"; }
    // --- ICompilationProvider ---
    const TS::ICompilation& Compilation() const override {
        throw std::logic_error("TupleCtorStub::Compilation");
    }
    // --- IParameterizedMember ---
    std::vector<const TS::IParameter*> Parameters() const override {
        return {};
    }
    // --- IMember ---
    const TS::IMember* MemberDefinition() const override { return this; }
    const TS::IType& ReturnType() const override { return *voidType_; }
    std::vector<const TS::IMember*>
    ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return false; }
    const TS::TypeParameterSubstitution* Substitution() const override {
        return &TS::TypeParameterSubstitution::Identity();
    }
    const TS::IMethod* Specialize(
        const TS::TypeParameterSubstitution* substitution) const override {
        (void)substitution;
        return this;
    }
    bool Equals(const TS::IMember* obj,
                const TS::TypeVisitor* typeNormalization) const override {
        (void)typeNormalization;
        return obj == this;
    }
    // --- IMethod ---
    std::vector<const TS::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(TS::KnownAttribute) const override { return false; }
    const TS::IAttribute* GetAttribute(TS::KnownAttribute) const override {
        return nullptr;
    }
    TS::Accessibility Accessibility() const override {
        return TS::Accessibility::Public;
    }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }
    std::uint32_t MetadataToken() const override { return 0; }
    const TS::ITypeDefinition* DeclaringTypeDefinition() const override {
        return nullptr;
    }
    TS::ITypePtr DeclaringType() const override { return declaringType_; }
    const TS::IModule* ParentModule() const override { return nullptr; }
    std::vector<const TS::IAttribute*> GetReturnTypeAttributes() const override {
        return {};
    }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool ThisIsRefReadOnly() const override { return false; }
    bool IsInitOnly() const override { return false; }
    std::vector<const TS::ITypeParameter*> TypeParameters() const override {
        return {};
    }
    std::vector<TS::ITypePtr> TypeArguments() const override { return {}; }
    bool IsExtensionMethod() const override { return false; }
    bool IsLocalFunction() const override { return false; }
    bool IsConstructor() const override { return false; }
    bool IsDestructor() const override { return false; }
    bool IsOperator() const override { return false; }
    bool HasBody() const override { return true; }
    bool IsAccessor() const override { return false; }
    const TS::IMember* AccessorOwner() const override { return nullptr; }
    TS::MethodSemanticsAttributes AccessorKind() const override {
        return TS::MethodSemanticsAttributes::None;
    }
    const TS::IMethod* ReducedFrom() const override { return nullptr; }

    TS::ITypePtr declaringType_;
    TS::ITypePtr voidType_ =
        std::make_shared<TS::KnownType>(TS::KnownTypeCode::Void);
};

} // namespace

// The ItemN parse: `ldflda Item1(ldloc t)` over System.ValueTuple`2 matches
// with position 1.
TEST(TupleTransformTest, Item1FieldAccessMatchesWithPosition)
{
    TupleFixture f;
    auto t = MakeLocal("t", f.valueTuple2);
    IL::LdFlda ldflda(std::make_unique<IL::LdLoc>(t),
                      "System.ValueTuple`2::Item1");

    TS::ITypePtr tupleType;
    IL::ILInstruction* target = nullptr;
    int position = 0;
    ASSERT_TRUE(IL::MatchTupleFieldAccess(ldflda, f.valueTuple2.get(), tupleType,
                                          target, position));
    EXPECT_EQ(position, 1);
    ASSERT_NE(target, nullptr);
    ASSERT_NE(tupleType, nullptr);
    EXPECT_EQ(tupleType->Name(), "ValueTuple");
}

// A non-Item field name does not match.
TEST(TupleTransformTest, NonItemFieldDoesNotMatch)
{
    TupleFixture f;
    auto t = MakeLocal("t", f.valueTuple2);
    IL::LdFlda ldflda(std::make_unique<IL::LdLoc>(t),
                      "System.ValueTuple`2::Tag");

    TS::ITypePtr tupleType;
    IL::ILInstruction* target = nullptr;
    int position = 0;
    EXPECT_FALSE(IL::MatchTupleFieldAccess(ldflda, f.valueTuple2.get(), tupleType,
                                           target, position));
    EXPECT_EQ(position, 0);
}

// A non-tuple declaring type does not match (the C# IsTupleCompatible gate).
TEST(TupleTransformTest, NonTupleDeclaringTypeDoesNotMatch)
{
    TupleFixture f;
    auto listType = std::make_shared<TS::TestSupport::LookupTypeDefinition>(
        "List", "System.Collections.Generic",
        TS::FullTypeName("System.Collections.Generic.List`1"),
        TS::TypeKind::Class, TS::Accessibility::Public, f.compilation, nullptr);
    auto t = MakeLocal("t", listType);
    IL::LdFlda ldflda(std::make_unique<IL::LdLoc>(t),
                      "System.ValueTuple`2::Item1");

    TS::ITypePtr tupleType;
    IL::ILInstruction* target = nullptr;
    int position = 0;
    EXPECT_FALSE(IL::MatchTupleFieldAccess(ldflda, listType.get(), tupleType,
                                           target, position));
}

// The flat construction: `newobj ValueTuple`2(a, b)` matches with the two
// argument nodes in order. The construction uses the DeconstructionTransform
// test's resolved-IMethod surface (the match reads IsNewObj +
// Method.DeclaringType + Arguments).
TEST(TupleTransformTest, FlatTupleConstructionMatches)
{
    TupleFixture f;
    auto ctor = std::make_shared<TupleCtorStub>(f.valueTuple2);
    IL::Call newobj(ctor, /*isNewObj=*/true);
    newobj.Arguments.push_back(std::make_unique<IL::LdcI4>(1));
    newobj.Arguments.push_back(std::make_unique<IL::LdcI4>(2));

    std::vector<IL::ILInstruction*> arguments;
    EXPECT_TRUE(IL::MatchTupleConstruction(newobj, arguments));
    ASSERT_EQ(arguments.size(), 2u);
    const auto* first = dynamic_cast<const IL::LdcI4*>(arguments[0]);
    const auto* second = dynamic_cast<const IL::LdcI4*>(arguments[1]);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->Value, 1);
    EXPECT_EQ(second->Value, 2);

    // A wrong arity aborts the match (the C# `Arguments.Count != elementCount`).
    IL::Call wrong(ctor, /*isNewObj=*/true);
    wrong.Arguments.push_back(std::make_unique<IL::LdcI4>(1));
    std::vector<IL::ILInstruction*> none;
    EXPECT_FALSE(IL::MatchTupleConstruction(wrong, none));
}
