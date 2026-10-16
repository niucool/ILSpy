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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `GetDelegateInvokeMethod` (the port of
// ICSharpCode.Decompiler/TypeSystem/TypeSystemExtensions.cs line 414) -- the
// extension that resolves a delegate type's `Invoke` method (the delegate's
// signature contract the conversion helpers compare a candidate method
// against). The load-bearing cruxes are: (a) a `TypeKind.Delegate` type with an
// `Invoke` method yields that method (pointer-identity); (b) `.FirstOrDefault()`
// returns the FIRST `Invoke` method when several are present (not
// `.SingleOrDefault()`); (c) a delegate with no `Invoke` method yields null (the
// filter yields an empty snapshot); (d) the `Kind == Delegate` guard
// short-circuits to null for EVERY non-delegate kind EVEN WHEN the method table
// holds an `Invoke` method; (e) the guard fires BEFORE `GetMethods` is called
// (a non-delegate kind does not touch the method table); (f) the helper
// faithfully forwards `GetMemberOptions.IgnoreInheritedMembers` (the delegate's
// `Invoke` is declared directly on the delegate, not inherited).
//
// The stub is a `LookupTypeDefinition` subclass whose `GetMethods(filter,
// options)` returns a configured list, applying the filter faithfully (the C#
// `GetMethodsImpl` runs the predicate over the method table), and recording the
// call count and the last options so the guard-ordering and faithful-option
// cruxes are pinnable. `LookupMethod` (from `LookupStubs.hpp`) supplies the
// `IMethod` stubs with a configurable `Name()`.

#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"

#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetDelegateInvokeMethod;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` whose `GetMethods(filter, options)` returns a
// configured list, applying the filter faithfully (the C# `GetMethodsImpl`
// runs the predicate over the method table). The `using
// LookupTypeDefinition::LookupTypeDefinition;` inherits the base ctor. The
// call count and the last options are recorded (mutable -- `GetMethods` is
// const) so the guard-ordering and faithful-option cruxes are pinnable.
class MethodHostType : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    int GetMethodsCallCount() const { return getMethodsCallCount_; }
    GetMemberOptions LastGetMethodsOptions() const { return lastOptions_; }

    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        ++getMethodsCallCount_;
        lastOptions_ = options;
        if (!filter)
            return methods_;
        std::vector<const IMethod*> r;
        for (const IMethod* m : methods_)
            if (filter(m))
                r.push_back(m);
        return r;
    }

private:
    std::vector<const IMethod*> methods_;
    mutable int getMethodsCallCount_ = 0;
    mutable GetMemberOptions lastOptions_ = GetMemberOptions::None;
};

// A `MethodHostType` with the supplied `TypeKind` (Delegate by default).
std::shared_ptr<MethodHostType> MakeHost(std::string name, TypeKind kind = TypeKind::Delegate) {
    return std::make_shared<MethodHostType>(
        std::move(name), "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr);
}

// A `LookupMethod` kept alive in a static vector (the `AddMembers_Test`
// precedent) so the raw `const IMethod*` the host holds outlives the call.
const IMethod* MakeMethod(std::string name) {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

} // namespace

// ---------------------------------------------------------------------------
// A delegate type with an `Invoke` method yields that method (pointer-identity).
// ---------------------------------------------------------------------------
TEST(GetDelegateInvokeMethodTest, ReturnsInvokeMethodForDelegateType) {
    auto host = MakeHost("D");
    const IMethod* invoke = MakeMethod("Invoke");
    host->SetMethods({invoke, MakeMethod("BeginInvoke"), MakeMethod("EndInvoke")});
    EXPECT_EQ(GetDelegateInvokeMethod(*host), invoke);
}

// ---------------------------------------------------------------------------
// `.FirstOrDefault()` returns the FIRST `Invoke` method when several are
// present (NOT `.SingleOrDefault()` -- multiple matches do not throw, the first
// wins). This pins the `FirstOrDefault` semantics against a `Single` divergence.
// ---------------------------------------------------------------------------
TEST(GetDelegateInvokeMethodTest, ReturnsFirstInvokeWhenMultipleInvokeMethods) {
    auto host = MakeHost("D");
    const IMethod* first = MakeMethod("Invoke");
    const IMethod* second = MakeMethod("Invoke");
    host->SetMethods({first, second});
    EXPECT_EQ(GetDelegateInvokeMethod(*host), first);
    EXPECT_NE(GetDelegateInvokeMethod(*host), second);
}

// ---------------------------------------------------------------------------
// A delegate whose method table has no `Invoke` method yields null (the filter
// yields an empty snapshot, `.FirstOrDefault()` returns null).
// ---------------------------------------------------------------------------
TEST(GetDelegateInvokeMethodTest, ReturnsNullWhenDelegateHasNoInvokeMethod) {
    auto host = MakeHost("D");
    host->SetMethods({MakeMethod("BeginInvoke"), MakeMethod("EndInvoke")});
    EXPECT_EQ(GetDelegateInvokeMethod(*host), nullptr);
}

// ---------------------------------------------------------------------------
// A delegate whose method table is empty yields null.
// ---------------------------------------------------------------------------
TEST(GetDelegateInvokeMethodTest, ReturnsNullWhenDelegateMethodListIsEmpty) {
    auto host = MakeHost("D");
    EXPECT_EQ(GetDelegateInvokeMethod(*host), nullptr);
}

// ---------------------------------------------------------------------------
// The `Kind == Delegate` guard short-circuits to null for a NON-delegate kind
// EVEN WHEN the method table holds an `Invoke` method. A `Class` kind with an
// `Invoke` method still yields null (the guard fires before the method scan).
// ---------------------------------------------------------------------------
TEST(GetDelegateInvokeMethodTest, ReturnsNullForNonDelegateKindDespiteInvokeMethod) {
    auto host = MakeHost("C", TypeKind::Class);
    const IMethod* invoke = MakeMethod("Invoke");
    host->SetMethods({invoke});
    EXPECT_EQ(GetDelegateInvokeMethod(*host), nullptr);
}

// ---------------------------------------------------------------------------
// Every non-delegate kind short-circuits to null; a `Struct` kind with an
// `Invoke` method also yields null (a second non-Delegate kind pins the guard
// is not a one-off for `Class`).
// ---------------------------------------------------------------------------
TEST(GetDelegateInvokeMethodTest, ReturnsNullForStructKindWithInvokeMethod) {
    auto host = MakeHost("S", TypeKind::Struct);
    const IMethod* invoke = MakeMethod("Invoke");
    host->SetMethods({invoke});
    EXPECT_EQ(GetDelegateInvokeMethod(*host), nullptr);
}

// ---------------------------------------------------------------------------
// The `Kind == Delegate` guard fires BEFORE `GetMethods` is called: a
// non-delegate kind does not touch the method table (call count stays 0). This
// pins the guard ordering against a reordering that would scan methods first.
// ---------------------------------------------------------------------------
TEST(GetDelegateInvokeMethodTest, NonDelegateKindShortCircuitsBeforeGetMethods) {
    auto host = MakeHost("C", TypeKind::Class);
    host->SetMethods({MakeMethod("Invoke")});
    EXPECT_EQ(GetDelegateInvokeMethod(*host), nullptr);
    EXPECT_EQ(host->GetMethodsCallCount(), 0);
}

// ---------------------------------------------------------------------------
// A delegate kind forwards `GetMemberOptions::IgnoreInheritedMembers` to
// `GetMethods` (the delegate's `Invoke` is declared directly on the delegate,
// not inherited from `System.Delegate`/`MulticastDelegate`; the faithful port
// forwards the option, not `None`).
// ---------------------------------------------------------------------------
TEST(GetDelegateInvokeMethodTest, DelegateKindPassesIgnoreInheritedMembersOption) {
    auto host = MakeHost("D");
    host->SetMethods({MakeMethod("Invoke")});
    (void)GetDelegateInvokeMethod(*host);
    EXPECT_EQ(host->LastGetMethodsOptions(), GetMemberOptions::IgnoreInheritedMembers);
}
