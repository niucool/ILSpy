// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions.IsDelegateCompatible(IMethod m, IMethod d, bool
// isExtensionMethodInvocation)` helper (CSharpConversions.cs line 1457, C# spec draft-v11
// section 21.4 "delegate compatibility") -- the private 3-arg overload the public
// `IsDelegateCompatible(IMethod, IType)` (which resolves the delegate invoke method via
// `IType.GetDelegateInvokeMethod`, not yet ported) and the `MethodGroupConversion` helper
// both call. Lifted to a `Detail::` free function as a tested-but-not-yet-wired foundation
// (the D63/D66/D68/D70/D74 precedent): no `CSharpConversions` caller invokes it yet, so it
// is dead in the CLI call graph.
//
// The helper tests a method `m` against a delegate invoke method `d`:
//   * parameter count must match (skipping `m`'s first parameter when
//     `isExtensionMethodInvocation` -- the `this` the extension syntax supplies);
//   * each corresponding parameter's `ReferenceKind` must match;
//   * a ref/out/in parameter must have an identity conversion on the types (Roslyn relaxes
//     the spec's same-type requirement to identity);
//   * a by-value parameter must have an identity OR implicit reference conversion from
//     `d`'s parameter type to `m`'s;
//   * the `ReturnTypeIsRefReadOnly` flags must match;
//   * the return type must have an identity OR implicit reference conversion from `m`'s to `d`'s.
//
// CRUX STUB CONVENTIONS (carried from the D514-D525 tests):
//  * `RefDef` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    that overrides `IsReferenceType` to return `true` (definite, not `nullopt`) -- the
//    `IsImplicitReferenceConversion` reference guard needs a definite `true` on both sides.
//    A `RefDef` carrying `KnownTypeCode::Object` makes `IsKnownType(it, Object)` true on the
//    type's own `GetDefinition()?.KnownTypeCode` (the D517 `IsSubtypeOf` Object short-circuit),
//    so `IsImplicitReferenceConversion(Derived, Object)` is true without modelling a base-type
//    chain (no `FindType` registration needed).
//  * `Def(ktc)` is a `LookupTypeDefinition` with a configurable `KnownTypeCode` / `TypeKind`
//    (struct by default). `LookupTypeDefinition::StructuralEquals` is IDENTITY equality
//    (`this == &other`), so the identity-conversion crux cases reuse the SAME `ITypePtr`
//    instance for both sides (the D514 same-instance precedent). `KnownType` has value-based
//    `StructuralEquals` (code==code) but is NOT an `ITypeDefinition`, so it cannot stand where
//    a definition is needed; the primitive/value-type stubs use `Def(Int32)` so the types
//    resolve.
//  * `LookupMethod(name, compilation)` is the `IMethod` stub (extended with `SetReturnType` /
//    `SetParameters` / `SetReturnTypeIsRefReadOnly`); kept alive in a static vector so the raw
//    `const IMethod*` outlives the `IsDelegateCompatible` call.
//  * `TestParameter(type, refKind)` is the `IParameter` stub with a configurable type and
//    reference kind (the D524 `ref In` unwrap crux); the `SymbolKind()` / `ReferenceKind()`
//    accessors hide the namespace-scope enums of the same name for the rest of the class body
//    (the D402 cross-scope name-hiding crux), so the enum references are fully qualified.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::IsDelegateCompatible
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

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::IsDelegateCompatible;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
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
// plain `LookupTypeDefinition` defaults to `nullopt` (indeterminate, fails the guard). The D517
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

// A minimal `IParameter` with a configurable type and reference kind. The `SymbolKind()` /
// `ReferenceKind()` accessors hide the namespace-scope enums of the same name for the rest of
// the class body (the D402 cross-scope name-hiding crux), so the enum references are fully
// qualified with `::ILSpy::Decompiler::TypeSystem::`. The D524 `TestParameter` precedent.
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
// `const IMethod*` outlives the `IsDelegateCompatible` call. The `ReturnType` / `Parameters` /
// `ReturnTypeIsRefReadOnly` are configured via the additive setters on `LookupMethod`.
const IMethod* MakeMethod() {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>("M", Compilation());
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
// Parameter-count arms
// ---------------------------------------------------------------------------

// The simplest compatible case: one by-value `int` parameter on each side, `int` return on
// each side (the same `Def(Int32)` instance for identity), not an extension method.
TEST(CSharpConversionsDelegateCompatibleTest, CompatibleByIdentityParametersAndReturn)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto pm = MakeParam(intT);
    auto pd = MakeParam(intT);
    const IMethod* m = Configure(MakeMethod(), {pm}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_TRUE(IsDelegateCompatible(Compilation(), *m, *d, /*isExtensionMethodInvocation*/ false));
}

// A parameter-count mismatch (m has 2, d has 1) is not delegate-compatible.
TEST(CSharpConversionsDelegateCompatibleTest, ParameterCountMismatchReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto pm0 = MakeParam(intT);
    auto pm1 = MakeParam(intT);
    auto pd = MakeParam(intT);
    const IMethod* m = Configure(MakeMethod(), {pm0, pm1}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_FALSE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// ---------------------------------------------------------------------------
// ReferenceKind arms
// ---------------------------------------------------------------------------

// A `ref` vs `out` mismatch on corresponding parameters is not delegate-compatible (ret/out/in
// must match).
TEST(CSharpConversionsDelegateCompatibleTest, ReferenceKindMismatchReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto pm = MakeParam(intT, ReferenceKind::Ref);
    auto pd = MakeParam(intT, ReferenceKind::Out);
    const IMethod* m = Configure(MakeMethod(), {pm}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_FALSE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// Matching `ref` parameters with identity types are compatible (the ref/out/in arm requires only
// an identity conversion on the types).
TEST(CSharpConversionsDelegateCompatibleTest, RefParametersWithIdentityTypesAreCompatible)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto pm = MakeParam(intT, ReferenceKind::Ref);
    auto pd = MakeParam(intT, ReferenceKind::Ref);
    const IMethod* m = Configure(MakeMethod(), {pm}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_TRUE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// A `ref` parameter pair whose types are NOT identity-convertible (int vs long) is not
// delegate-compatible -- the ref/out/in arm requires identity (Roslyn relaxes same-type to
// identity, but int != long is neither).
TEST(CSharpConversionsDelegateCompatibleTest, RefParametersWithNonIdentityTypesReturnFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto longT = Def(KnownTypeCode::Int64);
    auto pm = MakeParam(longT, ReferenceKind::Ref);
    auto pd = MakeParam(intT, ReferenceKind::Ref);
    const IMethod* m = Configure(MakeMethod(), {pm}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_FALSE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// ---------------------------------------------------------------------------
// By-value parameter arms
// ---------------------------------------------------------------------------

// A by-value parameter pair whose types are NOT identity but ARE implicitly reference-convertible
// (d's param is a derived reference type, m's param is Object) is compatible -- the by-value arm
// accepts an identity OR implicit reference conversion from d's type to m's. `IsImplicitReference
// Conversion(Derived, Object)` is true via the `IsSubtypeOf` Object short-circuit (the D517
// precedent; `RefDef(Object)` carries `KnownTypeCode::Object`). The return types are the same
// `RefDef(Object)` instance (identity).
TEST(CSharpConversionsDelegateCompatibleTest, ByValueParamWithImplicitReferenceConversionIsCompatible)
{
    auto objectT = Ref(KnownTypeCode::Object);
    auto derivedT = Ref(KnownTypeCode::None);
    auto pm = MakeParam(objectT);
    auto pd = MakeParam(derivedT);
    const IMethod* m = Configure(MakeMethod(), {pm}, objectT);
    const IMethod* d = Configure(MakeMethod(), {pd}, objectT);
    EXPECT_TRUE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// A by-value parameter pair with no identity and no implicit reference conversion (int vs string;
// int is a value type, so the reference-conversion guard fails) is not delegate-compatible.
TEST(CSharpConversionsDelegateCompatibleTest, ByValueParamWithNoConversionReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto stringT = Ref(KnownTypeCode::String);
    auto pm = MakeParam(intT);
    auto pd = MakeParam(stringT);
    const IMethod* m = Configure(MakeMethod(), {pm}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_FALSE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// ---------------------------------------------------------------------------
// ReturnTypeIsRefReadOnly arm
// ---------------------------------------------------------------------------

// A `ReturnTypeIsRefReadOnly` flag mismatch (m ref-readonly return, d not) is not
// delegate-compatible even when the types and parameter lists match.
TEST(CSharpConversionsDelegateCompatibleTest, ReturnTypeIsRefReadOnlyMismatchReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto pm = MakeParam(intT);
    auto pd = MakeParam(intT);
    const IMethod* m = Configure(MakeMethod(), {pm}, intT, /*refReadOnlyReturn*/ true);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT, /*refReadOnlyReturn*/ false);
    EXPECT_FALSE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// ---------------------------------------------------------------------------
// Return-type arms
// ---------------------------------------------------------------------------

// A return type with an implicit reference conversion (m returns a derived reference type, d
// returns Object) is compatible -- the return-type arm accepts an identity OR implicit reference
// conversion from m's return to d's. `IsImplicitReferenceConversion(Derived, Object)` is true.
// The by-value parameters are the same `RefDef(Object)` instance (identity) so the parameter
// arm passes too.
TEST(CSharpConversionsDelegateCompatibleTest, ReturnTypeImplicitReferenceConversionIsCompatible)
{
    auto objectT = Ref(KnownTypeCode::Object);
    auto derivedT = Ref(KnownTypeCode::None);
    auto pm = MakeParam(objectT);
    auto pd = MakeParam(objectT);
    const IMethod* m = Configure(MakeMethod(), {pm}, derivedT);
    const IMethod* d = Configure(MakeMethod(), {pd}, objectT);
    EXPECT_TRUE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// A return type with no identity and no implicit reference conversion (m returns int, d returns
// string; int is a value type, so the reference-conversion guard fails) is not delegate-compatible.
// The parameters are the same `Def(Int32)` instance (identity) so the parameter arm passes.
TEST(CSharpConversionsDelegateCompatibleTest, ReturnTypeIncompatibleReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto stringT = Ref(KnownTypeCode::String);
    auto pm = MakeParam(intT);
    auto pd = MakeParam(intT);
    const IMethod* m = Configure(MakeMethod(), {pm}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, stringT);
    EXPECT_FALSE(IsDelegateCompatible(Compilation(), *m, *d, false));
}

// ---------------------------------------------------------------------------
// Extension-method arm
// ---------------------------------------------------------------------------

// An extension-method invocation skips m's first parameter (the `this` the extension syntax
// supplies): m has [this, int], d has [int], isExtensionMethodInvocation=true -> compatible (the
// remaining parameter is identity `int`, the return is identity).
TEST(CSharpConversionsDelegateCompatibleTest, ExtensionMethodSkipsFirstParameter)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto objectT = Ref(KnownTypeCode::Object);
    auto thisP = MakeParam(objectT);
    auto pm1 = MakeParam(intT);
    auto pd = MakeParam(intT);
    const IMethod* m = Configure(MakeMethod(), {thisP, pm1}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_TRUE(IsDelegateCompatible(Compilation(), *m, *d, /*isExtensionMethodInvocation*/ true));
}

// A parameter-count mismatch AFTER skipping m's first parameter (m has [this, int, int], d has
// [int], extension) is not delegate-compatible.
TEST(CSharpConversionsDelegateCompatibleTest, ExtensionMethodCountMismatchAfterSkipReturnsFalse)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto objectT = Ref(KnownTypeCode::Object);
    auto thisP = MakeParam(objectT);
    auto pm1 = MakeParam(intT);
    auto pm2 = MakeParam(intT);
    auto pd = MakeParam(intT);
    const IMethod* m = Configure(MakeMethod(), {thisP, pm1, pm2}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_FALSE(IsDelegateCompatible(Compilation(), *m, *d, true));
}

// A non-extension invocation does NOT skip m's first parameter: m has [this, int], d has [int],
// isExtensionMethodInvocation=false -> parameter-count mismatch (2 != 1), not compatible. This
// pins that the `isExtensionMethodInvocation` flag controls the first-parameter skip.
TEST(CSharpConversionsDelegateCompatibleTest, NonExtensionDoesNotSkipFirstParameter)
{
    auto intT = Def(KnownTypeCode::Int32);
    auto objectT = Ref(KnownTypeCode::Object);
    auto thisP = MakeParam(objectT);
    auto pm1 = MakeParam(intT);
    auto pd = MakeParam(intT);
    const IMethod* m = Configure(MakeMethod(), {thisP, pm1}, intT);
    const IMethod* d = Configure(MakeMethod(), {pd}, intT);
    EXPECT_FALSE(IsDelegateCompatible(Compilation(), *m, *d, /*isExtensionMethodInvocation*/ false));
}
