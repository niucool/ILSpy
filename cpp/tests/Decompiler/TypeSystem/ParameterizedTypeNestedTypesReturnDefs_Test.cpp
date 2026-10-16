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

// Tests for the `ParameterizedType.GetNestedTypes` `ReturnMemberDefinitions` arm (D492). The C#
// `ParameterizedType.GetNestedTypes(filter, options)` is:
//   if (options & ReturnMemberDefinitions) return genericType.GetNestedTypes(filter, options);
//   else return GetMembersHelper.GetNestedTypes(this, filter, options);
// This leaf ports the `ReturnMemberDefinitions` arm (delegate to the generic type, passing `options`
// through unchanged); the else (routing) arm is deferred (returns the inherited empty default).
// `GetNestedTypes` returns OWNING `ITypePtr` (shared_ptr) -- unlike the member families (`const T*`)
// -- so the routing arm (a later leaf) needs no owning cache: `GetMembersHelper.GetNestedTypes`
// returns `std::vector<ITypePtr>` directly.
//
// The tests pin:
//  (a) the `ReturnMemberDefinitions` arm delegates to `genericType.GetNestedTypes(filter,
//      options)`, passing `options` through unchanged;
//  (b) the arm applies the caller's filter (a null filter passes everything);
//  (c) the typeArguments overload delegates too (`genericType.GetNestedTypes(typeArguments,
//      filter, options)`);
//  (d) the non-`ReturnMemberDefinitions` call returns empty (the deferred routing arm).

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "LookupStubs.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

ITypePtr Object() { return std::make_shared<KnownType>(KnownTypeCode::Object); }
ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }

// A `TestTypeDefinition : LookupTypeDefinition` that holds a configurable nested-types vector and
// records the `GetMemberOptions` / filter it received (so the test can verify the `ParameterizedType`
// delegation passes `options` through unchanged). Overrides `GetNestedTypes` directly (standing in
// for the not-yet-ported `MetadataTypeDefinition.GetNestedTypes` member-enumeration override, which
// would build the nested types from `ITypeDefinition::NestedTypes()` + the filter + options).
class TestTypeDefinition : public LookupTypeDefinition {
public:
    TestTypeDefinition(std::string name, int typeParamCount, const ICompilation& compilation)
        : LookupTypeDefinition(name, "",
                               ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                                   ::ILSpy::Decompiler::TypeSystem::TopLevelTypeName(
                                       "", name, typeParamCount)),
                               TypeKind::Class, Accessibility::Public, compilation, nullptr) {}

    void SetNestedTypes(std::vector<ITypePtr> n) { nested_ = std::move(n); }

    ::ILSpy::Decompiler::TypeSystem::GetMemberOptions LastOptions() const { return lastOpts_; }
    int LastFilterCalls() const { return filterCalls_; }

    std::vector<ITypePtr> GetNestedTypes(
        std::function<bool(const ITypeDefinition*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastOpts_ = options;
        if (!filter) return nested_;
        std::vector<ITypePtr> out;
        for (const auto& t : nested_) {
            // The filter receives the nested type's definition; a nested KnownType has none
            // (GetDefinition -> null), so pass null -- a null filter passes, a real filter sees null.
            if (filter(t->GetDefinition())) out.push_back(t);
        }
        return out;
    }

    std::vector<ITypePtr> GetNestedTypes(
        const std::vector<ITypePtr>& /*typeArguments*/,
        std::function<bool(const ITypeDefinition*)> filter,
        ::ILSpy::Decompiler::TypeSystem::GetMemberOptions options) const override
    {
        lastOpts_ = options;
        if (!filter) return nested_;
        std::vector<ITypePtr> out;
        for (const auto& t : nested_) {
            if (filter(t->GetDefinition())) out.push_back(t);
        }
        return out;
    }

private:
    std::vector<ITypePtr> nested_;
    mutable ::ILSpy::Decompiler::TypeSystem::GetMemberOptions lastOpts_{};
    mutable int filterCalls_ = 0;
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

const auto kNone = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::None;
const auto kIgnoreInherited = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::IgnoreInheritedMembers;
const auto kReturnDefs = ::ILSpy::Decompiler::TypeSystem::GetMemberOptions::ReturnMemberDefinitions;
const auto kReturnDefsIgnoreInherited = kReturnDefs | kIgnoreInherited;

} // namespace

// ---------------------------------------------------------------------------
// GetNestedTypes: the ReturnMemberDefinitions arm delegates to
// genericType.GetNestedTypes, passing options through unchanged.
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesReturnDefsTest, GetNestedTypesDelegatesWithOptionsThrough) {
    auto gen = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    auto inner = std::make_shared<KnownType>(KnownTypeCode::Object);  // a stand-in nested type
    gen->SetNestedTypes({inner});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetNestedTypes(nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(gen->LastOptions(), kReturnDefsIgnoreInherited);
}

// ---------------------------------------------------------------------------
// GetNestedTypes: the arm applies the caller's filter (a filter that rejects all
// nested-type definitions yields empty).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesReturnDefsTest, GetNestedTypesAppliesFilter) {
    auto gen = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    auto inner = std::make_shared<KnownType>(KnownTypeCode::Object);
    gen->SetNestedTypes({inner});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetNestedTypes(
        [](const ITypeDefinition*) { return false; }, kReturnDefsIgnoreInherited);
    EXPECT_TRUE(result.empty());
}

// ---------------------------------------------------------------------------
// GetNestedTypes: the typeArguments overload delegates to
// genericType.GetNestedTypes(typeArguments, filter, options).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesReturnDefsTest, GetNestedTypesTypeArgsOverloadDelegates) {
    auto gen = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    auto inner = std::make_shared<KnownType>(KnownTypeCode::Object);
    gen->SetNestedTypes({inner});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    std::vector<ITypePtr> args{Int32()};
    auto result = pt->GetNestedTypes(args, nullptr, kReturnDefsIgnoreInherited);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(gen->LastOptions(), kReturnDefsIgnoreInherited);
}

// ---------------------------------------------------------------------------
// GetNestedTypes: the non-ReturnMemberDefinitions call returns empty (the deferred routing arm).
// ---------------------------------------------------------------------------
TEST(ParameterizedTypeNestedTypesReturnDefsTest, GetNestedTypesWithoutReturnDefsIsDeferredEmpty) {
    auto gen = std::make_shared<TestTypeDefinition>("Outer", 1, Compilation());
    auto inner = std::make_shared<KnownType>(KnownTypeCode::Object);
    gen->SetNestedTypes({inner});
    auto pt = std::make_shared<ParameterizedType>(gen, std::vector<ITypePtr>{Int32()});

    auto result = pt->GetNestedTypes(nullptr, kNone);
    EXPECT_TRUE(result.empty());
}
