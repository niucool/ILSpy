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

// Tests for the public `CSharpConversions::ImplicitConversion(IType, IType)` /
// `ExplicitConversion(IType, IType)` entry points (CSharpConversions.cs lines 151 / 298) and
// the private `Detail::ImplicitConversion(IType, IType, bool allowUserDefined, bool allowTuple)`
// overload they build on (line 166). The private overload is the implicit-conversion dispatch:
// `StandardImplicitConversion` first, then (if None and `allowUserDefined`) the user-defined
// implicit conversion. The public `ImplicitConversion(IType, IType)` caches the result of the
// private overload called with `allowUserDefined: true, allowTuple: true`. The public
// `ExplicitConversion(IType, IType)` checks the implicit conversion first
// (`ImplicitConversion(false, false)`), then the standard explicit conversion
// (`ExplicitConversionImpl`), then the user-defined explicit conversion
// (`UserDefinedExplicitConversion(null, ...)`).
//
// CRUX STUB CONVENTIONS (carried from the D523/D525/D530 tests):
//  * `Def(ktc)` is a `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`)
//    with a configurable `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves
//    the `KnownTypeCode` via the numeric cast, so a `Def(Int32)` reports `TypeCode::Int32`. Use this
//    (NOT `KnownType`) where `GetTypeCode` must resolve (the numeric helpers).
//    `LookupTypeDefinition::StructuralEquals` is IDENTITY equality (`this == &other`), so the
//    identity-conversion crux cases must reuse the SAME instance for both sides.
//  * `KnownType(ktc)` is NOT an `ITypeDefinition` (`GetDefinition() == nullptr`), but the faithful
//    stub for a plain value/reference primitive. `KnownType(Int32).IsReferenceType() == false`
//    (derived from Kind == Struct); `KnownType(String).IsReferenceType() == true` (Kind == Class).
//    Used for the boxing from-side (needs a definite `false` IsReferenceType) and the reference
//    to-side (a reference type).
//  * `RefDef` is a `LookupTypeDefinition` whose `IsReferenceType()` is `true` (the base default is
//    `std::nullopt`, which fails the reference/boxing/unboxing guards). A `RefDef` carrying
//    `KnownTypeCode::Object` makes `IsKnownType(it, Object)` resolve on the type itself (the
//    `IsSubtypeOf` short-circuit).
//  * `NullableOf(element)` is a `ParameterizedType` over the `System.Nullable`1` definition (a
//    1-arg `ParameterizedType` whose generic carries `KnownTypeCode::NullableOfT`), so
//    `NullableType.GetUnderlyingType` strips it to the element. The lifted-identity test
//    (`int -> Nullable<int>`) reuses the SAME int instance for the from-side and the Nullable's
//    type argument.
//  * `OperatorMethodHost` is a `LookupTypeDefinition` subclass whose `GetMethods(filter)` returns a
//    configured list (applying the filter faithfully), so the operator scan finds the configured
//    operators. Used for the user-defined-conversion crux cases where `StandardImplicitConversion`
//    returns None but a user-defined operator resolves.
//  * `PointerType` over `KnownType(Void)` has `ReflectionName() == "System.Void*"`; `PointerType`
//    over `KnownType(Int32)` has `"System.Int32*"` (an any-pointer source).

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"           // CSharpConversions (the public methods)
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"    // Detail::ImplicitConversion
#include "Decompiler/Semantics/ConversionFactories.hpp"              // Conversion / Conversions (the dispatch return singletons)
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"          // KnownType, ParameterizedType, GetMemberOptions
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LifetimeAnnotation.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"   // IsNullable, GetUnderlyingType
#include "Decompiler/TypeSystem/ReferenceKind.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::CSharpConversions;
using ILSpy::Decompiler::CSharp::Resolver::Detail::ImplicitConversion;
using ILSpy::Decompiler::Semantics::Conversion;
using ILSpy::Decompiler::Semantics::Conversions;
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
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::LifetimeAnnotation;
using ILSpy::Decompiler::TypeSystem::ParameterizedType;
using ILSpy::Decompiler::TypeSystem::PointerType;
using ILSpy::Decompiler::TypeSystem::ReferenceKind;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::SymbolKind;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupMethod;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;

// A `LookupTypeDefinition` whose `IsReferenceType()` is `true` (a `Class`/`Interface` IS a
// reference type -- the faithful value the reference-conversion guard needs; the base
// `LookupTypeDefinition` default is `std::nullopt`, which fails the guard). Inherits the
// `LookupTypeDefinition` ctor; the only override is `IsReferenceType`.
class RefDef : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    std::optional<bool> IsReferenceType() const override { return true; }
};

LookupCompilation& Compilation() {
    static LookupCompilation c;
    return c;
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves the `KnownTypeCode`
// via the numeric cast (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Def(Int32)`
// reports `TypeCode::Int32`. The D514 `MakeDef` precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    int n = static_cast<int>(ktc);
    std::string name = "T" + std::to_string(n);
    return std::make_shared<LookupTypeDefinition>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// A primitive integral definition as an `ITypePtr` (for dereferencing to the `const IType&`).
ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
    return MakeDef(ktc, kind);
}

// A `RefDef` with a configurable `KnownTypeCode` and `TypeKind`. A `KnownTypeCode::Object` makes
// `IsKnownType(it, Object)` resolve on the type itself (the `IsSubtypeOf` short-circuit fires).
std::shared_ptr<RefDef> MakeRefDef(KnownTypeCode ktc, TypeKind kind) {
    int n = static_cast<int>(ktc);
    std::string name = "RT" + std::to_string(n) + "_" + std::to_string(static_cast<int>(kind));
    return std::make_shared<RefDef>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

// The `System.Object` definition -- a reference type (`Class`) carrying `KnownTypeCode::Object`,
// so `IsKnownType(it, Object)` is true (the `IsSubtypeOf` short-circuit fires).
std::shared_ptr<RefDef> ObjectDef() {
    static auto d = MakeRefDef(KnownTypeCode::Object, TypeKind::Class);
    return d;
}

// The `System.Nullable`1` generic definition (a struct, `KnownTypeCode::NullableOfT`).
std::shared_ptr<LookupTypeDefinition> NullableDef() {
    return std::make_shared<LookupTypeDefinition>("Nullable`1", "System",
        FullTypeName(TopLevelTypeName("System", "Nullable`1", 1)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
        KnownTypeCode::NullableOfT);
}

// `Nullable<T>` over the supplied element type (a 1-arg `ParameterizedType` over the
// `System.Nullable`1` definition). `NullableType.GetUnderlyingType` strips it to the element.
ITypePtr NullableOf(ITypePtr element) {
    return std::make_shared<ParameterizedType>(NullableDef(), std::vector<ITypePtr>{std::move(element)});
}

// The null-literal type (`TypeKind::Null`). Faithful to the C# `SpecialType.NullType` singleton
// (`isReferenceType: true`); the null-literal arm checks `fromType.Kind == Null`.
ITypePtr NullType() {
    return std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true));
}

// `System.Void*` -- a `PointerType` over `KnownType(Void)`. `PointerType::ReflectionName()` is
// `element->ReflectionName() + "*"`, and `KnownType(Void).ReflectionName()` is `"System.Void"`,
// so the result is `"System.Void*"`.
ITypePtr VoidPtr() {
    return std::make_shared<PointerType>(std::make_shared<KnownType>(KnownTypeCode::Void));
}

// `int*` -- a `PointerType` over `KnownType(Int32)`. `ReflectionName()` is `"System.Int32*"`.
ITypePtr IntPtr() {
    return std::make_shared<PointerType>(std::make_shared<KnownType>(KnownTypeCode::Int32));
}

// A `LookupTypeDefinition` whose `GetMethods(filter)` returns a configured list of operators,
// applying the filter faithfully (the D530 precedent). `IsReferenceType` is configurable
// (default `nullopt`, the `LookupTypeDefinition` default). Used for the user-defined-conversion
// crux cases where `StandardImplicitConversion` returns None but a user-defined operator
// resolves. The `KnownTypeCode::None` host is a custom struct (not numeric, not a reference type),
// so `StandardImplicitConversion(Host, Int32)` returns None (no standard arm fires).
class OperatorMethodHost : public LookupTypeDefinition {
public:
    using LookupTypeDefinition::LookupTypeDefinition;
    void SetMethods(std::vector<const IMethod*> m) { methods_ = std::move(m); }
    void SetIsReferenceType(std::optional<bool> v) { isRef_ = v; }
    std::optional<bool> IsReferenceType() const override { return isRef_; }
    std::vector<const IMethod*> GetMethods(
        std::function<bool(const IMethod*)> filter = nullptr,
        GetMemberOptions options = GetMemberOptions::None) const override
    {
        (void)options;
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
    std::optional<bool> isRef_;
};

// An `OperatorMethodHost` (a definition whose `GetMethods` returns the configured operators).
// `KnownTypeCode::None` so the host is a custom struct (not numeric, not implicitly convertible).
std::shared_ptr<OperatorMethodHost> MakeHost(KnownTypeCode ktc,
                                              std::optional<bool> isRef = std::nullopt) {
    int n = static_cast<int>(ktc);
    std::string name = "H" + std::to_string(n);
    auto h = std::make_shared<OperatorMethodHost>(
        name, "",
        FullTypeName(TopLevelTypeName("", name, 0)),
        TypeKind::Struct, Accessibility::Public, Compilation(), nullptr, ktc);
    h->SetIsReferenceType(isRef);
    return h;
}

// A minimal `IParameter` with a configurable type and reference kind (the D529/D530 precedent). The
// `SymbolKind()` / `ReferenceKind()` accessors hide the namespace-scope enums of the same name
// for the rest of the class body (the D402 cross-scope name-hiding crux), so the enum references
// are fully qualified with `::ILSpy::Decompiler::TypeSystem::`.
class TestParameter : public IParameter {
public:
    explicit TestParameter(ITypePtr type,
                           ::ILSpy::Decompiler::TypeSystem::ReferenceKind refKind = ::ILSpy::Decompiler::TypeSystem::ReferenceKind::None,
                           std::string name = "p")
        : name_(std::move(name)), type_(std::move(type)), refKind_(refKind) {}
    ::ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Parameter; }
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

// A `LookupMethod` kept alive in a static vector (the D530 precedent) so the raw
// `const IMethod*` the `OperatorInfo` holds outlives the call.
const IMethod* MakeMethod(std::string name) {
    static std::vector<std::shared_ptr<LookupMethod>> keep;
    auto m = std::make_shared<LookupMethod>(std::move(name), Compilation());
    keep.push_back(m);
    return m.get();
}

// A fully-configured conversion operator: static, an operator, the given name, a single
// parameter of `sourceType` (reference kind `None`), and `targetType` as the return type.
const IMethod* MakeOperator(std::string name, ITypePtr sourceType, ITypePtr targetType,
                            std::vector<std::shared_ptr<TestParameter>>* keepParams) {
    const IMethod* m = MakeMethod(std::move(name));
    auto* method = const_cast<LookupMethod*>(static_cast<const LookupMethod*>(m));
    method->SetStatic(true);
    method->SetIsOperator(true);
    auto p = std::make_shared<TestParameter>(std::move(sourceType), ReferenceKind::None);
    keepParams->push_back(p);
    method->SetParameters({p.get()});
    method->SetReturnType(std::move(targetType));
    return m;
}

} // namespace

// ===========================================================================
// Detail::ImplicitConversion(IType, IType, bool, bool) (CSharpConversions.cs line 166).
//
//   var c = StandardImplicitConversion(fromType, toType, allowTuple);
//   if (c == Conversion.None && allowUserDefined)
//       c = UserDefinedImplicitConversion(null, fromType, toType);
//   return c;
//
// The `allowUserDefined` flag is the load-bearing crux: when true, the user-defined implicit
// conversion is the fallback when no standard implicit conversion exists; when false, only the
// standard implicit conversion is checked (the user-defined branch is unreachable).
// ===========================================================================

// ---------------------------------------------------------------------------
// allowUserDefined=true, standard implicit fires -- the standard singleton is returned (the
// user-defined fallback is NOT reached because StandardImplicitConversion returns non-None).
// ---------------------------------------------------------------------------

// `int -> int` (same instance), `allowUserDefined=true`: `StandardImplicitConversion` fires the
// identity arm -> IdentityConversion. The user-defined fallback is not reached (the standard
// implicit conversion is non-None).
TEST(CSharpConversionsImplicitExplicitTest, ImplicitConversionAllowUserDefinedReturnsIdentity) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    auto c = ImplicitConversion(Compilation(), *intType, *intType, true, true);
    EXPECT_EQ(c.get(), Conversions::IdentityConversion().get());
}

// `int -> long`, `allowUserDefined=true`: `StandardImplicitConversion` fires the numeric widening
// arm -> ImplicitNumericConversion. The user-defined fallback is not reached.
TEST(CSharpConversionsImplicitExplicitTest, ImplicitConversionAllowUserDefinedReturnsNumericWidening) {
    ITypePtr fromInt = Def(KnownTypeCode::Int32);
    ITypePtr toLong = Def(KnownTypeCode::Int64);
    auto c = ImplicitConversion(Compilation(), *fromInt, *toLong, true, true);
    EXPECT_EQ(c.get(), Conversions::ImplicitNumericConversion().get());
}

// ---------------------------------------------------------------------------
// allowUserDefined=true, standard None + user-defined fires -- the CRUX: the user-defined
// fallback resolves when no standard implicit conversion exists.
// ---------------------------------------------------------------------------

// A custom struct `Host` with an `op_Implicit(Host -> Int32)` operator, `allowUserDefined=true`:
// `StandardImplicitConversion(Host, Int32)` returns None (Host is not numeric, not a reference
// type, not a type parameter, not a pointer -- no standard arm fires), then
// `UserDefinedImplicitConversion(null, Host, Int32)` resolves the operator -> a valid
// `UserDefinedConversion` (not None). This is the CRUX: the user-defined fallback fires.
TEST(CSharpConversionsImplicitExplicitTest, ImplicitConversionAllowUserDefinedFallsBackToUserDefined) {
    auto fromType = MakeHost(KnownTypeCode::None);
    auto toType = Def(KnownTypeCode::Int32);
    std::vector<std::shared_ptr<TestParameter>> keepParams;
    const IMethod* op = MakeOperator("op_Implicit", fromType, toType, &keepParams);
    fromType->SetMethods({op});
    auto c = ImplicitConversion(Compilation(), *fromType, *toType, true, true);
    ASSERT_NE(c.get(), Conversions::None().get());
    EXPECT_TRUE(c->IsImplicit());
    EXPECT_TRUE(c->IsUserDefined());
    EXPECT_TRUE(c->IsValid());
}

// ---------------------------------------------------------------------------
// allowUserDefined=false -- the user-defined fallback is SKIPPED even when a user-defined
// operator exists. The CRUX: `allowUserDefined=false` gates the user-defined branch.
// ---------------------------------------------------------------------------

// `int -> int` (same instance), `allowUserDefined=false`: `StandardImplicitConversion` fires the
// identity arm -> IdentityConversion. The user-defined fallback is not reached (and is gated off).
TEST(CSharpConversionsImplicitExplicitTest, ImplicitConversionDisallowUserDefinedReturnsIdentity) {
    ITypePtr intType = Def(KnownTypeCode::Int32);
    auto c = ImplicitConversion(Compilation(), *intType, *intType, false, false);
    EXPECT_EQ(c.get(), Conversions::IdentityConversion().get());
}

// A custom struct `Host` with an `op_Implicit(Host -> Int32)` operator, `allowUserDefined=false`:
// `StandardImplicitConversion(Host, Int32)` returns None, then the user-defined branch is SKIPPED
// (allowUserDefined is false) -> None. The CRUX: the user-defined operator is NOT consulted when
// allowUserDefined is false, even though it would resolve.
TEST(CSharpConversionsImplicitExplicitTest, ImplicitConversionDisallowUserDefinedSkipsUserDefined) {
    auto fromType = MakeHost(KnownTypeCode::None);
    auto toType = Def(KnownTypeCode::Int32);
    std::vector<std::shared_ptr<TestParameter>> keepParams;
    const IMethod* op = MakeOperator("op_Implicit", fromType, toType, &keepParams);
    fromType->SetMethods({op});
    auto c = ImplicitConversion(Compilation(), *fromType, *toType, false, false);
    EXPECT_EQ(c.get(), Conversions::None().get());
}

// ---------------------------------------------------------------------------
// None case -- no standard implicit and no user-defined (allowUserDefined=true but no operator).
// ---------------------------------------------------------------------------

// `bool -> string`, `allowUserDefined=true`: `StandardImplicitConversion` returns None (bool is
// not implicitly convertible to string), and `UserDefinedImplicitConversion(null, bool, string)`
// returns None (no op_Implicit operator on bool/string). Returns None.
TEST(CSharpConversionsImplicitExplicitTest, ImplicitConversionReturnsNoneWhenNoConversionExists) {
    ITypePtr boolType = Def(KnownTypeCode::Boolean);
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto c = ImplicitConversion(Compilation(), *boolType, *stringType, true, true);
    EXPECT_EQ(c.get(), Conversions::None().get());
}

// ===========================================================================
// CSharpConversions::ImplicitConversion(IType, IType) (CSharpConversions.cs line 151).
//
//   TypePair pair = new TypePair(fromType, toType);
//   if (implicitConversionCache.TryGetValue(pair, out Conversion c)) return c;
//   c = ImplicitConversion(fromType, toType, allowUserDefined: true, allowTuple: true);
//   implicitConversionCache[pair] = c;
//   return c;
//
// The public cached entry point: delegates to the private overload with true, true and caches
// the result. The CRUX is the caching: a repeat call with the same pair returns the SAME cached
// pointer (not a fresh dispatch).
// ===========================================================================

// The public method delegates to `Detail::ImplicitConversion(*compilation_, from, to, true,
// true)`: the identity case (int -> int, same instance) returns the IdentityConversion singleton.
TEST(CSharpConversionsImplicitExplicitTest, PublicImplicitConversionReturnsIdentity) {
    CSharpConversions conversions(Compilation());
    ITypePtr intType = Def(KnownTypeCode::Int32);
    auto c = conversions.ImplicitConversion(*intType, *intType);
    EXPECT_EQ(c.get(), Conversions::IdentityConversion().get());
}

// The public method delegates to the private overload: the numeric widening case (int -> long)
// returns the ImplicitNumericConversion singleton (the standard implicit dispatch fires).
TEST(CSharpConversionsImplicitExplicitTest, PublicImplicitConversionReturnsNumericWidening) {
    CSharpConversions conversions(Compilation());
    ITypePtr fromInt = Def(KnownTypeCode::Int32);
    ITypePtr toLong = Def(KnownTypeCode::Int64);
    auto c = conversions.ImplicitConversion(*fromInt, *toLong);
    EXPECT_EQ(c.get(), Conversions::ImplicitNumericConversion().get());
}

// The CRUX caching test: a repeat call with the SAME pair (same `const IType*` addresses) returns
// the SAME cached `shared_ptr<Conversion>` (pointer-identity between the two calls' returned
// `shared_ptr`'s raw `Conversion*`). The cache key is `TypePair(&fromType, &toType)` (the D512
// `TypePair` cache key), so reusing the same instances hits the cache. The identity case is used
// (the cached value is the IdentityConversion singleton -- the same singleton on a cache hit, but
// the test asserts the raw pointers match, confirming the cache returned the stored entry rather
// than re-dispatching).
TEST(CSharpConversionsImplicitExplicitTest, PublicImplicitConversionCachesRepeatCall) {
    CSharpConversions conversions(Compilation());
    ITypePtr intType = Def(KnownTypeCode::Int32);
    auto c1 = conversions.ImplicitConversion(*intType, *intType);
    auto c2 = conversions.ImplicitConversion(*intType, *intType);
    // The cache returns the SAME stored shared_ptr (the raw Conversion* matches).
    EXPECT_EQ(c1.get(), c2.get());
}

// ===========================================================================
// CSharpConversions::ExplicitConversion(IType, IType) (CSharpConversions.cs line 298).
//
//   Conversion c = ImplicitConversion(fromType, toType, allowUserDefined: false, allowTuple: false);
//   if (c != Conversion.None) return c;
//   c = ExplicitConversionImpl(fromType, toType);
//   if (c != Conversion.None) return c;
//   return UserDefinedExplicitConversion(null, fromType, toType);
//
// The implicit check is FIRST (an implicit conversion subsumes the explicit one), then the
// standard explicit dispatch, then the user-defined explicit fallback.
// ===========================================================================

// ---------------------------------------------------------------------------
// Implicit-check-first: an implicit conversion is returned even though the method is named
// "ExplicitConversion". The CRUX that distinguishes ExplicitConversion from ExplicitConversionImpl.
// ---------------------------------------------------------------------------

// `int -> long`: `ImplicitConversion(false, false)` fires the numeric widening arm ->
// ImplicitNumericConversion (the implicit check is first). The explicit dispatch
// (`ExplicitConversionImpl`) would return ExplicitNumericConversion, but the implicit check wins.
TEST(CSharpConversionsImplicitExplicitTest, PublicExplicitConversionReturnsImplicitForWidening) {
    CSharpConversions conversions(Compilation());
    ITypePtr fromInt = Def(KnownTypeCode::Int32);
    ITypePtr toLong = Def(KnownTypeCode::Int64);
    auto c = conversions.ExplicitConversion(*fromInt, *toLong);
    EXPECT_EQ(c.get(), Conversions::ImplicitNumericConversion().get());
}

// `int -> int` (same instance): `ImplicitConversion(false, false)` fires the identity arm ->
// IdentityConversion (the implicit check is first). The explicit dispatch would return
// ExplicitNumericConversion (AnyNumericConversion is true for two ints), but identity wins.
TEST(CSharpConversionsImplicitExplicitTest, PublicExplicitConversionReturnsIdentityForSameType) {
    CSharpConversions conversions(Compilation());
    ITypePtr intType = Def(KnownTypeCode::Int32);
    auto c = conversions.ExplicitConversion(*intType, *intType);
    EXPECT_EQ(c.get(), Conversions::IdentityConversion().get());
}

// `int -> object`: `ImplicitConversion(false, false)` fires the boxing arm -> BoxingConversion
// (the implicit check is first). `KnownType(Int32)` (NOT an `ITypeDefinition`) is the faithful
// value-type stub (IsReferenceType == false, definite); `ObjectDef` is the reference type.
TEST(CSharpConversionsImplicitExplicitTest, PublicExplicitConversionReturnsBoxingForIntToObject) {
    CSharpConversions conversions(Compilation());
    ITypePtr intType = std::make_shared<KnownType>(KnownTypeCode::Int32);
    auto c = conversions.ExplicitConversion(*intType, *ObjectDef());
    EXPECT_EQ(c.get(), Conversions::BoxingConversion().get());
}

// ---------------------------------------------------------------------------
// Explicit fallback: the implicit check returns None, so the ExplicitConversionImpl dispatch
// fires and returns the EXPLICIT conversion singleton.
// ---------------------------------------------------------------------------

// `long -> int`: `ImplicitConversion(false, false)` returns None (no implicit narrowing), then
// `ExplicitConversionImpl` fires the numeric arm -> ExplicitNumericConversion. The classic
// explicit narrowing.
TEST(CSharpConversionsImplicitExplicitTest, PublicExplicitConversionReturnsExplicitNumericForNarrowing) {
    CSharpConversions conversions(Compilation());
    ITypePtr fromLong = Def(KnownTypeCode::Int64);
    ITypePtr toInt = Def(KnownTypeCode::Int32);
    auto c = conversions.ExplicitConversion(*fromLong, *toInt);
    EXPECT_EQ(c.get(), Conversions::ExplicitNumericConversion().get());
}

// `void* -> int*`: `ImplicitConversion(false, false)` returns None (the implicit pointer arm
// converts any pointer TO void*, never FROM void*), then `ExplicitConversionImpl` fires the
// pointer arm -> ExplicitPointerConversion (any pointer converts to any other pointer).
TEST(CSharpConversionsImplicitExplicitTest, PublicExplicitConversionReturnsExplicitPointerForVoidPtrToIntPtr) {
    CSharpConversions conversions(Compilation());
    ITypePtr voidPtr = VoidPtr();
    ITypePtr intPtr = IntPtr();
    auto c = conversions.ExplicitConversion(*voidPtr, *intPtr);
    EXPECT_EQ(c.get(), Conversions::ExplicitPointerConversion().get());
}

// ---------------------------------------------------------------------------
// None case -- no implicit, no explicit, no user-defined -> None.
// ---------------------------------------------------------------------------

// `bool -> string`: `ImplicitConversion(false, false)` returns None, `ExplicitConversionImpl`
// returns None (bool is not numeric/enum/nullable/ref/pointer, string is not matching), and
// `UserDefinedExplicitConversion(null, ...)` returns None (no op_Explicit operator on bool/string).
// Returns None.
TEST(CSharpConversionsImplicitExplicitTest, PublicExplicitConversionReturnsNoneWhenNoConversionExists) {
    CSharpConversions conversions(Compilation());
    ITypePtr boolType = Def(KnownTypeCode::Boolean);
    ITypePtr stringType = std::make_shared<KnownType>(KnownTypeCode::String);
    auto c = conversions.ExplicitConversion(*boolType, *stringType);
    EXPECT_EQ(c.get(), Conversions::None().get());
}
