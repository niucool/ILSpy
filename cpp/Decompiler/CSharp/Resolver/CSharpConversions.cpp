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

// Port of the `CSharpConversions` `Get` factory -- the per-compilation singleton cached on the
// compilation's `CacheManager`. See the header for the deferred conversion methods.

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"  // Detail::StandardImplicitConversion / Detail::ImplicitConversion / Detail::IsDelegateCompatible (the dispatch entry points)
#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversion / Conversions (the dispatch return singletons)
#include "Decompiler/Semantics/ResolveResult.hpp"  // ResolveResult (the ResolveResult-based public entries)
#include "Decompiler/Semantics/TupleResolveResult.hpp"  // TupleResolveResult (the ExplicitConversion(ResolveResult) tuple-arm RTTI)
#include "Decompiler/TypeSystem/IMethod.hpp"  // IMethod (the IsDelegateCompatible(IMethod, IType) entry)
#include "Decompiler/TypeSystem/TypeKind.hpp"  // TypeKind (the dynamic arm)
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetDelegateInvokeMethod (the IsDelegateCompatible(IMethod, IType) entry)
#include "Decompiler/Util/CacheManager.hpp"  // CacheManager (Get factory)

namespace ILSpy::Decompiler::CSharp::Resolver {

using ILSpy::Decompiler::Semantics::Conversions;

CSharpConversions& CSharpConversions::Get(
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
{
    // The C# keys on `typeof(CSharpConversions)` (a `System.Type` object). The port's `CacheManager`
    // keys on `const void*`; a function-local static's address is a stable per-process key (one per
    // instantiation of this translation unit, which is a single key for `CSharpConversions`).
    static char typeKey;
    const void* key = &typeKey;
    // The C# `compilation.CacheManager` returns a mutable `CacheManager` (the C# has no const); the
    // port's `ICompilation::CacheManager()` returns `const CacheManager&` (a const-correctness
    // over-restriction relative to the C#). The `GetOrAddShared` mutator stores the singleton in the
    // cache, but the mutation is logically idempotent (a repeat `Get` with the same key returns the
    // same value whether it stored or found), so the `const_cast` here is the established port
    // convention for logically-const lazy-cache accessors (the `TypeVisitor&`/`AcceptVisitor`
    // `const_cast` in `const` lazy accessors).
    auto& cache = const_cast<ILSpy::Decompiler::Util::CacheManager&>(compilation.CacheManager());
    // The cached value is an owning `shared_ptr<CSharpConversions>` (the cache owns the lifetime,
    // mirroring the C# `CacheManager` holding the reference). `GetOrAddShared` with a pre-built value
    // stores-and-returns it (dedup against a concurrent/racy `Get`).
    auto sp = std::make_shared<CSharpConversions>(compilation);
    std::any cached = cache.GetOrAddShared(key, std::any{std::shared_ptr<CSharpConversions>{sp}});
    // `cached` is the winning value (ours if we were first, the existing one otherwise). Unwrap and
    // return by reference (the cache keeps the `shared_ptr` alive for the compilation's lifetime).
    auto stored = std::any_cast<std::shared_ptr<CSharpConversions>>(cached);
    return *stored;
}

std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
CSharpConversions::StandardImplicitConversion(ILSpy::Decompiler::TypeSystem::IType& fromType,
                                                ILSpy::Decompiler::TypeSystem::IType& toType)
{
	// The C# `return StandardImplicitConversion(fromType, toType, allowTupleConversion: true);` --
	// the public method delegates to the private overload. The port collapses the overload into the
	// `Detail::` free function (the tuple arm is deferred, so `allowTupleConversion` is effectively
	// always true for the ported arms). The `*compilation_` threads the instance's compilation to the
	// reference/boxing/type-parameter/pointer helpers that need `FindType`/`IsSubtypeOf`.
	return Detail::StandardImplicitConversion(*compilation_, fromType, toType);
}

std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
CSharpConversions::ImplicitConversion(ILSpy::Decompiler::TypeSystem::IType& fromType,
                                      ILSpy::Decompiler::TypeSystem::IType& toType)
{
	// CSharpConversions.cs line 151. The public cached entry point: checks the
	// `implicitConversionCache` first; on a miss, delegates to the private
	// `ImplicitConversion(fromType, toType, allowUserDefined: true, allowTuple: true)` overload
	// (the `Detail::ImplicitConversion` free function) and caches the result. The C#
	// `TypePair pair = new TypePair(fromType, toType); if (implicitConversionCache.TryGetValue(pair,
	// out Conversion c)) return c;` ports to an `unordered_map::find` on a `TypePair` keyed by the
	// two `const IType*` (the D512 `TypePair` cache key). The cached value is an owning
	// `shared_ptr<Conversion>` (`Conversion` is polymorphic; a plain value would slice).
	TypePair pair(&fromType, &toType);
	auto it = implicitConversionCache_.find(pair);
	if (it != implicitConversionCache_.end())
		return it->second;
	auto c = Detail::ImplicitConversion(*compilation_, fromType, toType,
	                                      /*allowUserDefined*/ true, /*allowTuple*/ true);
		implicitConversionCache_[pair] = c;
	return c;
}

std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
CSharpConversions::ExplicitConversion(ILSpy::Decompiler::TypeSystem::IType& fromType,
                                      ILSpy::Decompiler::TypeSystem::IType& toType)
{
	// CSharpConversions.cs line 298. The public explicit entry point (NOT cached -- only
	// `ImplicitConversion` caches). Checks the implicit conversion first
	// (`ImplicitConversion(fromType, toType, allowUserDefined: false, allowTuple: false)` -- the
	// `Detail::ImplicitConversion` free function); if an implicit conversion exists, returns it
	// (an explicit conversion subsumes any implicit conversion). Then checks the standard explicit
	// conversion (`Detail::ExplicitConversionImpl`); if one exists, returns it. Otherwise falls back
	// to the user-defined explicit conversion (`Detail::UserDefinedExplicitConversion(null, ...)`).
	// The C# `c != Conversion.None` checks port to pointer-identity against the `None` singleton.
	auto c = Detail::ImplicitConversion(*compilation_, fromType, toType,
	                                      /*allowUserDefined*/ false, /*allowTuple*/ false);
	if (c.get() != Conversions::None().get())
		return c;
	c = Detail::ExplicitConversionImpl(*compilation_, fromType, toType);
	if (c.get() != Conversions::None().get())
		return c;
	return Detail::UserDefinedExplicitConversion(*compilation_, /*fromResult*/ nullptr,
	                                              fromType, toType);
}

std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
CSharpConversions::ImplicitConversion(const ILSpy::Decompiler::Semantics::ResolveResult& resolveResult,
                                      ILSpy::Decompiler::TypeSystem::IType& toType)
{
	// CSharpConversions.cs line 143. The public implicit entry point (the ResolveResult overload,
	// NOT cached -- the C# caches only the IType-based overload at line 151; the ResolveResult
	// context makes a cache key impractical). Delegates to the private
	// `ImplicitConversion(resolveResult, toType, allowUserDefined: true, allowTuple: true)`
	// overload -- the `Detail::ImplicitConversion` ResolveResult-based free function (D528). The
	// C# `if (resolveResult == null) throw new ArgumentNullException(...)` compiles out (the
	// `const ResolveResult&` reference cannot bind to null, the D374 convention).
	return Detail::ImplicitConversion(*compilation_, resolveResult, toType,
	                                  /*allowUserDefined*/ true, /*allowTuple*/ true);
}

std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
CSharpConversions::ExplicitConversion(const ILSpy::Decompiler::Semantics::ResolveResult& resolveResult,
                                      ILSpy::Decompiler::TypeSystem::IType& toType)
{
	// CSharpConversions.cs line 281. The public explicit entry point (the ResolveResult overload,
	// NOT cached). Checks (in order): the dynamic arm, the implicit check first, the (deferred)
	// tuple arm, the standard explicit dispatch, the user-defined explicit fallback. The C#
	// `ArgumentNullException` on null args compiles out (the references cannot bind to null, the
	// D374 convention). The C# `c != Conversion.None` checks port to pointer-identity against the
	// `None` singleton.

	// C# `if (resolveResult.Type.Kind == TypeKind.Dynamic) return Conversion.ExplicitDynamicConversion;`
	// -- the dynamic arm. `resolveResult.Type()` returns `const IType&`; `Kind()` is the `IType`
	// virtual (the D374 non-null-reference convention; `IType.hpp` pulls in `TypeKind.hpp`). An
	// explicit conversion from `dynamic` always succeeds (the C# `dynamic`-erasure semantics).
	if (resolveResult.Type().Kind() == ILSpy::Decompiler::TypeSystem::TypeKind::Dynamic)
		return Conversions::ExplicitDynamicConversion();

	// C# `Conversion c = ImplicitConversion(resolveResult, toType, allowUserDefined: false,
	// allowTuple: false);` -- the implicit check first (an implicit conversion subsumes the explicit
	// one). The `Detail::ImplicitConversion` ResolveResult-based free function (D528).
	auto c = Detail::ImplicitConversion(*compilation_, resolveResult, toType,
	                                      /*allowUserDefined*/ false, /*allowTuple*/ false);
	if (c.get() != Conversions::None().get())
		return c;

	// C# `if (resolveResult is TupleResolveResult tupleRR) { c = TupleConversion(tupleRR, toType,
	// isExplicit: true); if (c != Conversion.None) return c; }` -- the tuple-literal -> tuple-type arm
	// (C# 9.0 spec section 10.3.6). The dispatch owns the RTTI (the `dynamic_cast` to
	// `TupleResolveResult`, the D528 precedent); the `Detail::TupleConversion` helper owns the body.
	// A non-tuple `ResolveResult` (the `dynamic_cast` yields `nullptr`) falls through to the
	// `ExplicitConversionImpl` / user-defined arms.
	if (auto* tupleRR = dynamic_cast<const ILSpy::Decompiler::Semantics::TupleResolveResult*>(&resolveResult)) {
		c = Detail::TupleConversion(*compilation_, *tupleRR, toType, /*isExplicit*/ true);
		if (c.get() != Conversions::None().get())
			return c;
	}

	// C# `c = ExplicitConversionImpl(resolveResult.Type, toType);` -- the standard explicit
	// dispatch. `resolveResult.Type()` returns `const IType&` but `Detail::ExplicitConversionImpl`
	// takes `IType&` non-const (the non-const `AcceptVisitor`, D406), so the port `const_cast`s the
	// const reference -- the underlying type-system object is mutable (the accessor's `const` is
	// the contract, not a guarantee), the D515/D517/D528 `const_cast` precedent.
	c = Detail::ExplicitConversionImpl(*compilation_,
	                                   const_cast<ILSpy::Decompiler::TypeSystem::IType&>(resolveResult.Type()),
	                                   toType);
	if (c.get() != Conversions::None().get())
		return c;

	// C# `return UserDefinedExplicitConversion(resolveResult, resolveResult.Type, toType);` -- the
	// user-defined explicit fallback. `Detail::UserDefinedExplicitConversion` takes the
	// `ResolveResult*` (the nullable pointer, D530) -- `&resolveResult` is the non-null pointer
	// (the public entry always has a real `resolveResult`) -- and `IType&` non-const (the same
	// `const_cast` as the `ExplicitConversionImpl` call above).
	return Detail::UserDefinedExplicitConversion(*compilation_, &resolveResult,
	    const_cast<ILSpy::Decompiler::TypeSystem::IType&>(resolveResult.Type()), toType);
}

bool CSharpConversions::IsDelegateCompatible(const ILSpy::Decompiler::TypeSystem::IMethod& method,
                                             const ILSpy::Decompiler::TypeSystem::IType& delegateType)
{
	// CSharpConversions.cs line 1421. The public delegate-compatibility entry point (the
	// `IMethod` + `IType` overload). The C# `if (method == null) throw new ArgumentNullException(...)`
	// / `if (delegateType == null) throw new ArgumentNullException(...)` compile out (the `const
	// IMethod&` / `const IType&` references cannot bind to null, the D374 convention). Resolves the
	// delegate type's `Invoke` method via `GetDelegateInvokeMethod` (the TypeSystemExtensions free
	// function, D533 -- the C# `delegateType.GetDelegateInvokeMethod()` extension method). A
	// non-delegate kind, or a delegate with an empty/invoke-less method table, yields null -> return
	// `false` (a non-delegate type is never delegate-compatible with any method). Otherwise delegates
	// to the private 3-arg `IsDelegateCompatible(method, invoke, false)` overload (the
	// `Detail::IsDelegateCompatible` free function, D531) with `isExtensionMethodInvocation: false`
	// (the public entry is not an extension-method invocation). The `Detail::IsDelegateCompatible`
	// signature takes `const IMethod&` for both `m` and `d` (every `IMethod`/`IParameter` member it
	// reads is `const`), so the resolved `const IMethod* invoke` dereferences directly to `const IMethod&`
	// -- no `const_cast` needed here (unlike the reference/boxing helpers that `const_cast` the const
	// `IType&` accessors to the non-const `IType&` the helpers take). The `invoke` is guaranteed
	// non-null by the `invoke == nullptr` guard (a faithful port of the C# `if (invoke == null)
	// return false;`).
	const ILSpy::Decompiler::TypeSystem::IMethod* invoke =
		ILSpy::Decompiler::TypeSystem::GetDelegateInvokeMethod(delegateType);
	if (invoke == nullptr)
		return false;
	return Detail::IsDelegateCompatible(*compilation_, method, *invoke,
	                                     /*isExtensionMethodInvocation*/ false);
}

int CSharpConversions::BetterConversion(ILSpy::Decompiler::TypeSystem::IType& s,
                                        ILSpy::Decompiler::TypeSystem::IType& t1,
                                        ILSpy::Decompiler::TypeSystem::IType& t2)
{
	// CSharpConversions.cs line 1620. The public "better conversion from type" entry point (the
	// `IType` + `IType` + `IType` overload). The C# `bool ident1 = IdentityConversion(s, t1); bool
	// ident2 = IdentityConversion(s, t2); if (ident1 && !ident2) return 1; if (ident2 && !ident1)
	// return 2; return BetterConversionTarget(t1, t2);` -- an identity conversion from the source
	// `s` to a target beats a non-identity conversion; when neither (or both) is identity, the
	// verdict falls to `BetterConversionTarget`. The `IdentityConversion` calls are the C#
	// `this.IdentityConversion` (the public bool helper); the port delegates to the already-ported
	// `Detail::IdentityConversion` (D514, the `IType&` non-const signature for the non-const
	// `AcceptVisitor`, D406). The `BetterConversionTarget` call is the `Detail::BetterConversionTarget`
	// free function (D535), threading `*compilation_` to the `ImplicitConversion` calls in the
	// Span/core arms. The ResolveResult-based `BetterConversion(ResolveResult, IType, IType)`
	// overload is DEFERRED (needs `IsExactlyMatching` + the lambda/expression-tree arms -- not yet
	// ported); only the type-based overload lands here.
	bool ident1 = Detail::IdentityConversion(s, t1);
	bool ident2 = Detail::IdentityConversion(s, t2);
	if (ident1 && !ident2)
		return 1;
	if (ident2 && !ident1)
		return 2;
	return Detail::BetterConversionTarget(*compilation_, t1, t2);
}

bool CSharpConversions::IsConstraintConvertible(ILSpy::Decompiler::TypeSystem::IType& fromType,
                                               ILSpy::Decompiler::TypeSystem::IType& toType)
{
	// CSharpConversions.cs line 261. The public constraint-convertibility entry point. The C# `throw
	// new ArgumentNullException` on null `fromType`/`toType` compiles out (the `IType&` references
	// cannot bind to null, the D374 convention). Delegates to the `Detail::IsConstraintConvertible`
	// free function (the D517 reference-cluster convention), threading `*compilation_` to the
	// identity/reference/boxing/type-parameter helpers that need `FindType`/`IsSubtypeOf`.
	return Detail::IsConstraintConvertible(*compilation_, fromType, toType);
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
