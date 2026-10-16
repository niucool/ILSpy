// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `OverloadResolution`'s `ResolveParameterTypes` helper (D508) -- the `Detail::` free
// function that reads the candidate's parameter types and handles the params-array/Span/array-interface
// expansion. The C# `ResolveParameterTypes(Candidate candidate, bool useSpecializedParameters)`:
//   for each parameter i:
//     type = useSpecializedParameters ? candidate.Member.Parameters[i].Type : candidate.Parameters[i].Type;
//     if IsExpandedForm && i == last:
//       if (Array, Rank==1) type = Element;
//       else if (Span/ReadOnlySpan) type = TypeArguments[0];
//       else if (array-interface) type = TypeArguments[0];
//       else return false;  // cannot unpack
//     candidate.ParameterTypes[i] = type;
//   return true;
// Returns false (abort the expanded-form candidate) if the last param's type is not unpackable.
//
// The tests pin:
//  (a) a non-expanded candidate -> ParameterTypes = the formal parameter types (verbatim); returns true;
//  (b) an expanded candidate with a single-dim array params -> the last ParameterType is the element;
//  (c) an expanded candidate with a multi-dim array params -> returns false (cannot unpack);
//  (d) an expanded candidate with a non-array/non-Span/non-interface last param -> returns false;
//  (e) useSpecializedParameters=true reads `Member.Parameters[i].Type` (the specialized member's params).

#include "Decompiler/CSharp/Resolver/OverloadResolutionCandidate.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MethodSemanticsAttributes.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::ResolveParameterTypes;
using ILSpy::Decompiler::CSharp::Resolver::OverloadResolutionCandidate;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::ArrayType;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IModule;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }

// A minimal `IParameter` stub with a configurable `Type` and `IsParams`.
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type, bool isParams = false, std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)), isParams_(isParams) {}

    // --- ISymbol ---
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter;
    }
    std::string Name() const override { return name_; }
    // --- IVariable ---
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    // --- IParameter ---
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override
    {
        return ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None;
    }
    bool IsParams() const override { return isParams_; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IParameterizedMember* Owner() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::LifetimeAnnotation Lifetime() const override
    {
        return {};
    }

private:
    std::string name_;
    ITypePtr type_;
    bool isParams_;
};

// A `LookupMethod`-like stub with a configurable `Parameters` vector (the formal parameters the
// candidate reads via `MemberDefinition()->Parameters()`). The `LookupMethod` stub returns empty
// Parameters; this subclass overrides `Parameters()` and `MemberDefinition()`.
class TestMethod : public LookupMethod {
public:
    explicit TestMethod(std::vector<const IParameter*> params, std::string name = "M")
        : LookupMethod(std::move(name), Compilation()), params_(std::move(params)) {}

    std::vector<const IParameter*> Parameters() const override { return params_; }
    // `MemberDefinition()` returns `this` (the candidate reads `MemberDefinition()->Parameters()`).
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }

private:
    std::vector<const IParameter*> params_;
};

} // namespace

// ---------------------------------------------------------------------------
// ResolveParameterTypes: a non-expanded candidate -> ParameterTypes = the formal parameter types;
// returns true.
// ---------------------------------------------------------------------------
TEST(ResolveParameterTypesTest, NonExpandedCopiesFormalTypes) {
    auto p1 = std::make_shared<TestParameter>(Int32());
    auto p2 = std::make_shared<TestParameter>(String());
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p1.get(), p2.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/false);
    bool ok = ResolveParameterTypes(c, /*useSpecializedParameters=*/false);
    EXPECT_TRUE(ok);
    ASSERT_EQ(c.ParameterTypes().size(), 2u);
    EXPECT_EQ(c.ParameterTypes()[0]->Name(), "Int32");
    EXPECT_EQ(c.ParameterTypes()[1]->Name(), "String");
}

// ---------------------------------------------------------------------------
// ResolveParameterTypes: an expanded candidate with a single-dim array params -> the last
// ParameterType is the array element; returns true.
// ---------------------------------------------------------------------------
TEST(ResolveParameterTypesTest, ExpandedSingleDimArrayUnpacksElement) {
    auto arrayType = std::make_shared<ArrayType>(Int32(), 1);  // single-dim int[]
    auto p = std::make_shared<TestParameter>(arrayType, /*isParams=*/true);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/true);
    bool ok = ResolveParameterTypes(c, false);
    EXPECT_TRUE(ok);
    ASSERT_EQ(c.ParameterTypes().size(), 1u);
    EXPECT_EQ(c.ParameterTypes()[0]->Name(), "Int32");  // the element type
}

// ---------------------------------------------------------------------------
// ResolveParameterTypes: an expanded candidate with a multi-dim array params -> returns false
// (cannot unpack a multi-dim params array).
// ---------------------------------------------------------------------------
TEST(ResolveParameterTypesTest, ExpandedMultiDimArrayCannotUnpack) {
    auto arrayType = std::make_shared<ArrayType>(Int32(), 2);  // int[,]
    auto p = std::make_shared<TestParameter>(arrayType, /*isParams=*/true);
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/true);
    bool ok = ResolveParameterTypes(c, false);
    EXPECT_FALSE(ok);  // Rank != 1 -> cannot unpack -> abort
}

// ---------------------------------------------------------------------------
// ResolveParameterTypes: an expanded candidate with a non-array/non-Span/non-interface last param ->
// returns false.
// ---------------------------------------------------------------------------
TEST(ResolveParameterTypesTest, ExpandedNonArrayNonSpanNonInterfaceCannotUnpack) {
    auto p = std::make_shared<TestParameter>(Object(), /*isParams=*/true);  // Object is not unpackable
    auto m = std::make_shared<TestMethod>(std::vector<const IParameter*>{p.get()});
    OverloadResolutionCandidate c(m.get(), /*isExpanded=*/true);
    bool ok = ResolveParameterTypes(c, false);
    EXPECT_FALSE(ok);  // Object -> cannot unpack -> abort
}
