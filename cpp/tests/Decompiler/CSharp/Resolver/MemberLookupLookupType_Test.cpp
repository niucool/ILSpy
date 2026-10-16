// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `MemberLookup.LookupType` (D501) -- the public type-name lookup. The C#
// `LookupType(IType declaringType, string name, IReadOnlyList<IType> typeArguments, bool
// parameterizeResultType = true)`: for each base type, fetch nested types (parameterized or
// definitions based on `parameterizeResultType`) + `AddNestedTypes`; remove `AllHidden` groups; if
// empty -> `UnknownMemberResolveResult(declaringType, name, typeArguments)`; else the most-derived
// group with nested types: `>1` nested type OR `>1` group -> `AmbiguousTypeResolveResult`, else
// `TypeResolveResult`. A `TypeKind.TypeParameter` declaring type skips the loop (no nested types).
//
// The tests pin:
//  (a) a type with no matching nested types -> `UnknownMemberResolveResult`;
//  (b) a `TypeKind.TypeParameter` declaring type -> `UnknownMemberResolveResult` (the loop is
//      skipped, no groups);
//  (c) `parameterizeResultType=true` vs `false` both call `GetNestedTypes` (the stub has no nested
//      types -> `UnknownMember`, but the call does not crash);
//  (d) the `UnknownMemberResolveResult` carries the `declaringType` (NOT the target's type, since
//      `LookupType` has no `ResolveResult` target).

#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/AmbiguousResolveResult.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::MemberLookup;
using ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult;
using ILSpy::Decompiler::Semantics::TypeResolveResult;
using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypeParameter;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeConstraint;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::VarianceModifier;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

std::shared_ptr<LookupTypeDefinition> MakeDef(std::string name) {
    return std::make_shared<LookupTypeDefinition>(std::move(name), "",
        FullTypeName(TopLevelTypeName("", name, 0)), TypeKind::Class,
        Accessibility::Public, Compilation(), nullptr);
}

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }

// A minimal `ITypeParameter` for the `TypeKind.TypeParameter` declaring-type test.
class TestTypeParameter : public ITypeParameter {
public:
    explicit TestTypeParameter(std::string name) : name_(std::move(name)) {}
    TypeKind Kind() const override { return TypeKind::TypeParameter; }
    std::string Name() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    int TypeParameterCount() const override { return 0; }
    ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
        return visitor.VisitTypeParameter(*this);
    }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeParameter;
    }
    ::ILSpy::Decompiler::TypeSystem::SymbolKind OwnerType() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }
    const ILSpy::Decompiler::TypeSystem::IEntity* Owner() const override { return nullptr; }
    int Index() const override { return 0; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override
    {
        return {};
    }
    VarianceModifier Variance() const override { return VarianceModifier::Invariant; }
    ITypePtr EffectiveBaseClass() const override { return nullptr; }
    std::vector<ITypePtr> EffectiveInterfaceSet() const override { return {}; }
    bool HasDefaultConstructorConstraint() const override { return false; }
    bool HasReferenceTypeConstraint() const override { return false; }
    bool HasValueTypeConstraint() const override { return false; }
    bool HasUnmanagedConstraint() const override { return false; }
    bool AllowsRefLikeType() const override { return false; }
    ::ILSpy::Decompiler::TypeSystem::Nullability NullabilityConstraint() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious;
    }
    std::vector<TypeConstraint> TypeConstraints() const override { return {}; }

protected:
    bool StructuralEquals(const IType& other) const override { return this == &other; }

private:
    std::string name_;
};

} // namespace

// ---------------------------------------------------------------------------
// LookupType: a type with no matching nested types -> UnknownMemberResolveResult.
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTypeTest, NoNestedTypesYieldsUnknownMember) {
    auto def = MakeDef("Foo");
    MemberLookup lookup(nullptr, nullptr, false);
    auto result = lookup.LookupType(*def, "NoSuchType", {}, true);
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// LookupType: a TypeKind.TypeParameter declaring type -> UnknownMemberResolveResult (the loop is
// skipped; no groups).
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTypeTest, TypeParameterDeclaringTypeYieldsUnknownMember) {
    auto tp = std::make_shared<TestTypeParameter>("T");
    MemberLookup lookup(nullptr, nullptr, false);
    auto result = lookup.LookupType(*tp, "Foo", {}, true);
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// LookupType: parameterizeResultType=false calls GetNestedTypes(ReturnMemberDefinitions) (the stub
// has no nested types -> UnknownMember, but the call does not crash).
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTypeTest, NoParameterizeResultTypeDoesNotCrash) {
    auto def = MakeDef("Foo");
    MemberLookup lookup(nullptr, nullptr, false);
    auto result = lookup.LookupType(*def, "Bar", {}, /*parameterizeResultType=*/false);
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}

// ---------------------------------------------------------------------------
// LookupType: the UnknownMemberResolveResult carries the declaringType (NOT a target's type, since
// LookupType has no ResolveResult target).
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTypeTest, UnknownMemberCarriesDeclaringType) {
    auto def = MakeDef("Foo");
    MemberLookup lookup(nullptr, nullptr, false);
    auto result = lookup.LookupType(*def, "Bar", {}, true);
    auto* umrr = dynamic_cast<UnknownMemberResolveResult*>(result.get());
    ASSERT_NE(umrr, nullptr);
    EXPECT_EQ(umrr->TargetType().GetDefinition(), def.get());  // the declaring type, not a target
}

// ---------------------------------------------------------------------------
// LookupType: a System.Object declaring type (no nested types) -> UnknownMember.
// ---------------------------------------------------------------------------
TEST(MemberLookupLookupTypeTest, ObjectDeclaringTypeYieldsUnknownMember) {
    MemberLookup lookup(nullptr, nullptr, false);
    auto result = lookup.LookupType(*Object(), "X", {}, true);
    EXPECT_NE(dynamic_cast<UnknownMemberResolveResult*>(result.get()), nullptr);
}
