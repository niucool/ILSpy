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

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"  // Detail::StandardImplicitConversion (the dispatch entry point)
#include "Decompiler/Semantics/ConversionFactories.hpp"  // Conversion / Conversions (the dispatch return singletons)
#include "Decompiler/Util/CacheManager.hpp"  // CacheManager (Get factory)

namespace ILSpy::Decompiler::CSharp::Resolver {

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

} // namespace ILSpy::Decompiler::CSharp::Resolver
