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

// Tests for the public `CSharpConversions::IsDelegateCompatible(IMethod method, IType
// delegateType)` entry point (CSharpConversions.cs line 1421, C# spec draft-v11 section 21.4
// "delegate compatibility") -- the public `IMethod` + `IType` overload. It resolves the delegate
// type's `Invoke` method via `GetDelegateInvokeMethod` (the TypeSystemExtensions free function,
// D533), returns `false` when the delegate type carries no `Invoke` method, and otherwise
// delegates to the private 3-arg `IsDelegateCompatible(method, invoke, false)` overload (the
// `Detail::IsDelegateCompatible` free function, D531). The 3-arg helper's behavior is pinned by
// the `CSharpConversionsDelegateCompatible_Test` suite; this suite pins the public entry's
// resolution-and-delegation: that the delegate type's `Invoke` is resolved, that a non-delegate
// kind short-circuits to `false`, and that the resolved invoke is threaded to the 3-arg helper.
//
// CRUX STUB CONVENTIONS (carried from the D531 DelegateCompatible test + the D533
// GetDelegateInvokeMethod test):
//  * `MethodHostType` is a `LookupTypeDefinition` subclass whose `GetMethods(filter, options)`
//    returns a configured list (applying the filter faithfully). The delegate type is a
//    `MethodHostType` with `TypeKind::Delegate` whose method table holds the `Invoke` method
//    (named `"Invoke"` so the `GetDelegateInvokeMethod` filter matches). A non-delegate kind
//    (e.g. `Class`) with an `Invoke` method in the table pins the `Kind == Delegate` guard
//    short-circuit -> null -> `false`.
//  * `Def(ktc)` / `Ref(ktc)` are the value-type / reference-type `LookupTypeDefinition` stubs
//    (the D531 precedent); `LookupTypeDefinition::StructuralEquals` is IDENTITY equality, so the
//    identity-conversion crux cases reuse the SAME instance for both sides. `Ref(Object)`
//    carries `KnownTypeCode::Object` so `IsImplicitReferenceConversion(Derived, Object)` holds
//    via the `IsSubtypeOf` Object short-circuit (no `FindType` registration needed).
//  * `LookupMethod(name, compilation)` is the `IMethod` stub (extended with `SetReturnType` /
//    `SetParameters` / `SetReturnTypeIsRefReadOnly`); kept alive in a static vector so the raw
//    `const IMethod*` outlives the call. Both the candidate `m` and the delegate's `invoke` are
//    `LookupMethod`s configured via `Configure`.
//  * `TestParameter(type, refKind)` is the `IParameter` stub (the D524/D531 precedent); the
//    `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of the same
//    name for the rest of the class body (the D402 cross-scope name-hiding crux), so the enum
//    references are fully qualified.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"           // CSharpConversions (the public method)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::IsDelegateCompatible (the 3-arg helper the public method delegates to)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::GetMemberOptions;
using ILSpy::Decompiler::TypeSystem::IAttribute;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IMethod;
using ILSpy::Decompiler::TypeSystem::IParameter;
using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` that overrides `IsReferenceType` to return `true` (definite) -- the
// `IsImplicitReferenceConversion` reference guard needs a definite `true` on both sides, and a
// plain `LookupTypeDefinition` defaults to `nullopt` (indeterminate, fails the guard). The D531
// `RefDef` precedent.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

// A `RefDef` with a configurable `KnownTypeCode` / `TypeKind`. A `KnownTypeCode::Object` makes
// `IsKnownType(it, Object)` true on the type's own definition (the `IsSubtypeOf` Object
// short-circuit), so `IsImplicitReferenceConversion(Derived, Object)` holds without a base chain.
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Class) {
    int n = static_cast<int>(ktc);
    std::string name = "R" + std::to_string(n);
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a
// configurable `KnownTypeCode` / `TypeKind` (struct by default). The D514 `MakeDef` precedent;
// `StructuralEquals` is IDENTITY equality, so the identity-conversion crux cases reuse the SAME
// instance for both sides.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) { return MakeDef(ktc, kind); }
ITypePtr Ref(KnownTypeCode ktc, TypeKind kind = TypeKind::Class) { return MakeRefDef(ktc, kind); }

// A `LookupTypeDefinition` subclass whose `GetMethods(filter, options)` returns a configured list,
// applying the filter faithfully (the D533 `MethodHostType` precedent). The delegate type is a
// `MethodHostType` with `TypeKind::Delegate`; the call count / last options are recorded so the
// guard-ordering and faithful-option cruxes are pinnable.
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

// A `MethodHostType` with the supplied `TypeKind` (Delegate by default). The D533 `MakeHost`
// precedent.
std::shared_ptr<MethodHostType> MakeHost(std::string name, TypeKind kind = TypeKind::Delegate) {
    return std::make_shared<MethodHostType>(
        std::move(name), "",
        FullTypeName(TopLevelTypeName("", std::move(name), 0)),
        kind, Accessibility::Public, Compilation(), nullptr);
}

// A minimal `IParameter` with a configurable type and reference kind. The `SymbolKind()` /
// `ReferenceKind()` accessors hide the namespace-scope enums of the same name for the rest of
// the class body (the D402 cross-scope name-hiding crux), so the enum references are fully
// qualified with `::ILSpy::Decompiler::TypeSystem::`. The D524/D531 `TestParameter` precedent.
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type,
                           ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                           std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)), refKind_(refKind) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override
    { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
    std::string Name() const override { return name_; }
    const IType& Type() const override { return *type_; }
    bool IsConst() const override { return false; }
    std::any GetConstantValue(bool) const override { return std::any{}; }
    std::vector<const IAttribute*> GetAttributes() const override { return {}; }
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind ReferenceKind() const override { return refKind_; }
    bool IsParams() const override { return false; }
    bool IsOptional() const override { return false; }
    bool HasConstantValueInSignature() const override { return false; }
    const IParameterizedMember* Owner() const override { return nullptr; }
    LifetimeAnnotation Lifetime() const override { return {}; }
private:
    std::string name_;
    ITypePtr type_;
    ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind_;
};

// A `LookupMethod` kept alive in a static vector (the `AddMembers_Test` precedent) so the raw
// `const IMethod*` outlives the `IsDelegateCompatible` call. The `name` is configurable so the
// delegate's `Invoke` method is named `"Invoke"` (the `GetDelegateInvokeMethod` filter matches).
const IMethod* MakeMethod(std::string name = "M") {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

// A `TestParameter` kept alive in a static vector so the raw `const IParameter*` the method holds
// outlives the `IsDelegateCompatible` call (the parameter's `Type()` dereferences the owned
// `ITypePtr`, so the parameter must stay alive while the helper reads it).
std::shared_ptr<TestParameter> MakeParam(ITypePtr type, ReferenceKind rk = ReferenceKind::None) {
    static std::vector<std::shared_ptr<TestParameter>> keep;
    auto p = std::make_shared<TestParameter>(std::move(type), rk);
    keep.push_back(p);
    return p;
}

// Configure a method: set its parameters (from the kept `TestParameter` shared_ptrs) and its
// return type + `ReturnTypeIsRefReadOnly` flag. Returns the same raw `const IMethod*` for chaining.
const IMethod* Configure(const IMethod* method, const std::vector<std::shared_ptr<TestParameter>>& params,
                         ITypePtr returnType, bool refReadOnlyReturn = false) {
    auto* m = const_cast<LookupMethod*>(static_cast<const LookupMethod*>(method));
    std::vector<const IParameter*> paramPtrs;
    for (const auto& p : params)
        paramPtrs.push_back(p.get());
    m->SetParameters(std::move(paramPtrs));
    m->SetReturnType(std::move(returnType));
    m->SetReturnTypeIsRefReadOnly(refReadOnlyReturn);
    return method;
}

} // namespace

// ---------------------------------------------------------------------------
// Crux: the public entry resolves the delegate's `Invoke` and delegates to the 3-arg helper.
// ---------------------------------------------------------------------------

// The simplest compatible case: a delegate type whose `Invoke` has one by-value `int` parameter
// and an `int` return, and a candidate method `m` with the same shape (identity on both the
// parameter and the return -- the same `Def(Int32)` instance). The public entry resolves the
// delegate's `Invoke` via `GetDelegateInvokeMethod`, then delegates to the 3-arg helper, which
// returns true. This pins the resolve-and-delegate path end to end.
TEST(CSharpConversionsDelegateCompatibleITypeTest, CompatibleMethodAndDelegateReturnsTrue)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT);
    auto mP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    const IMethod* m = Configure(MakeMethod("M"), {mP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    CSharpConversions conversions(Compilation());
    EXPECT_TRUE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// A by-value parameter pair whose types are NOT identity but ARE implicitly reference-convertible
// (the delegate's `Invoke` param is a derived reference type, the candidate `m`'s param is
// `Object`) is compatible -- the 3-arg by-value arm accepts an identity OR implicit reference
// conversion from `d`'s type to `m`'s. `IsImplicitReferenceConversion(Derived, Object)` is true via
// the `IsSubtypeOf` Object short-circuit (the D517 precedent; `Ref(Object)` carries
// `KnownTypeCode::Object`). The return types are the same `Ref(Object)` instance (identity). This
// pins the implicit-reference crux is reachable through the public entry, not just the 3-arg
// helper directly.
TEST(CSharpConversionsDelegateCompatibleITypeTest, ByValueParamWithImplicitReferenceConversionIsCompatible)
{
    auto objectT = Ref(KnownTypeCode::Object);
    auto derivedT = Ref(KnownTypeCode::None);
    auto invokeP = MakeParam(derivedT);
    auto mP = MakeParam(objectT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, objectT);
    const IMethod* m = Configure(MakeMethod("M"), {mP}, objectT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    CSharpConversions conversions(Compilation());
    EXPECT_TRUE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// Matching `ref` parameters with identity types are compatible (the ref/out/in arm requires only
// an identity conversion on the types). This pins the ref arm is reachable through the public
// entry; the same `Def(Int32)` instance on both sides gives identity.
TEST(CSharpConversionsDelegateCompatibleITypeTest, RefParametersWithIdentityTypesAreCompatible)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT, ReferenceKind::Ref);
    auto mP = MakeParam(intT, ReferenceKind::Ref);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    const IMethod* m = Configure(MakeMethod("M"), {mP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    CSharpConversions conversions(Compilation());
    EXPECT_TRUE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// ---------------------------------------------------------------------------
// Sentinel: the resolved invoke is threaded to the 3-arg helper, which detects incompatibility.
// ---------------------------------------------------------------------------

// A parameter-count mismatch (the candidate `m` has 2 params, the delegate's `Invoke` has 1) is
// not delegate-compatible -- the 3-arg helper's first guard fires.
TEST(CSharpConversionsDelegateCompatibleITypeTest, ParameterCountMismatchReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT);
    auto mP0 = MakeParam(intT);
    auto mP1 = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    const IMethod* m = Configure(MakeMethod("M"), {mP0, mP1}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    CSharpConversions conversions(Compilation());
    EXPECT_FALSE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// A `ref` vs `out` mismatch on corresponding parameters is not delegate-compatible (ret/out/in
// must match) -- the 3-arg helper's `ReferenceKind` guard fires.
TEST(CSharpConversionsDelegateCompatibleITypeTest, ReferenceKindMismatchReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT, ReferenceKind::Ref);
    auto mP = MakeParam(intT, ReferenceKind::Out);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    const IMethod* m = Configure(MakeMethod("M"), {mP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    CSharpConversions conversions(Compilation());
    EXPECT_FALSE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// A return type with no identity and no implicit reference conversion (the delegate's `Invoke`
// returns `int`, the candidate `m` returns `string`; `int` is a value type, so the
// reference-conversion guard fails) is not delegate-compatible. The parameters are the same
// `Def(Int32)` instance (identity) so the parameter arm passes; the return-type arm fails.
TEST(CSharpConversionsDelegateCompatibleITypeTest, ReturnTypeIncompatibleReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto stringT = Ref(KnownTypeCode::String);
    auto invokeP = MakeParam(intT);
    auto mP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    const IMethod* m = Configure(MakeMethod("M"), {mP}, stringT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    CSharpConversions conversions(Compilation());
    EXPECT_FALSE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// A `ref` parameter pair whose types are NOT identity-convertible (the delegate's `Invoke` has
// `ref int`, the candidate `m` has `ref long`) is not delegate-compatible -- the ref/out/in arm
// requires identity (Roslyn relaxes same-type to identity, but `int != long` is neither). This
// pins the ref-arm identity requirement is reachable through the public entry.
TEST(CSharpConversionsDelegateCompatibleITypeTest, RefParametersWithNonIdentityTypesReturnFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto longT = Def(KnownTypeCode::Int64);
    auto invokeP = MakeParam(intT, ReferenceKind::Ref);
    auto mP = MakeParam(longT, ReferenceKind::Ref);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    const IMethod* m = Configure(MakeMethod("M"), {mP}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({invoke});
    CSharpConversions conversions(Compilation());
    EXPECT_FALSE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// ---------------------------------------------------------------------------
// Sentinel: the `GetDelegateInvokeMethod` resolution short-circuits to false.
// ---------------------------------------------------------------------------

// A NON-delegate kind (a `Class`) passed as the delegate type returns `false` EVEN WHEN its
// method table holds an `Invoke` method -- `GetDelegateInvokeMethod` short-circuits on the
// `Kind == Delegate` guard (returns null), so the public entry returns `false` before the 3-arg
// helper runs. This pins the resolution short-circuit is reachable through the public entry.
TEST(CSharpConversionsDelegateCompatibleITypeTest, NonDelegateTypeReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto invokeP = MakeParam(intT);
    const IMethod* invoke = Configure(MakeMethod("Invoke"), {invokeP}, intT);
    const IMethod* m = Configure(MakeMethod("M"), {MakeParam(intT)}, intT);
    auto delegateType = MakeHost("C", TypeKind::Class);
    delegateType->SetMethods({invoke});
    CSharpConversions conversions(Compilation());
    EXPECT_FALSE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// A delegate kind whose method table has NO `Invoke` method (only `BeginInvoke` / `EndInvoke`)
// returns `false` -- the `GetDelegateInvokeMethod` filter yields an empty snapshot, so the public
// entry gets a null invoke and returns `false`.
TEST(CSharpConversionsDelegateCompatibleITypeTest, DelegateWithNoInvokeMethodReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    const IMethod* beginInvoke = MakeMethod("BeginInvoke");
    const IMethod* endInvoke = MakeMethod("EndInvoke");
    const IMethod* m = Configure(MakeMethod("M"), {MakeParam(intT)}, intT);
    auto delegateType = MakeHost("D");
    delegateType->SetMethods({beginInvoke, endInvoke});
    CSharpConversions conversions(Compilation());
    EXPECT_FALSE(conversions.IsDelegateCompatible(*m, *delegateType));
}

// A delegate kind whose method table is empty returns `false` -- `GetDelegateInvokeMethod`
// returns null, so the public entry returns `false`. This pins the empty-snapshot path.
TEST(CSharpConversionsDelegateCompatibleITypeTest, EmptyDelegateReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    const IMethod* m = Configure(MakeMethod("M"), {MakeParam(intT)}, intT);
    auto delegateType = MakeHost("D");
    // No SetMethods -- the method table is empty.
    CSharpConversions conversions(Compilation());
    EXPECT_FALSE(conversions.IsDelegateCompatible(*m, *delegateType));
}
