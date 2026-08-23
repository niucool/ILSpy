// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
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

// Out-of-line members of `DummyTypeParameter` (see the header). The instance caches
// live here: one vector per owner kind, grown under a mutex (the C#
// `Interlocked.CompareExchange` publication dance simplified to a lock -- the
// contract is the per-(kind, index) unique instance, not the lock-free read).

#include "Decompiler/TypeSystem/Implementation/DummyTypeParameter.hpp"

#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <mutex>
#include <stdexcept>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

namespace {

// The two C# static arrays + their publication lock, as two static vectors behind
// one mutex. Pre-filled with the index-0 entry each (the C# array initializers).
struct DummyTypeParameterCaches {
    std::mutex Mutex;
    std::vector<std::shared_ptr<ITypeParameter>> Method;
    std::vector<std::shared_ptr<ITypeParameter>> Class;
};

DummyTypeParameterCaches& Caches()
{
    static DummyTypeParameterCaches caches;
    return caches;
}

} // namespace

std::shared_ptr<ITypeParameter> DummyTypeParameter::GetTypeParameter(
    std::vector<std::shared_ptr<ITypeParameter>>& cache,
    ::ILSpy::Decompiler::TypeSystem::SymbolKind ownerType, int index)
{
    if (index < 0) {
        // The C# grow-loop never terminates for a negative index and the final
        // `tps[index]` throws IndexOutOfRangeException; the port throws directly.
        throw std::out_of_range("DummyTypeParameter index must be non-negative");
    }
    const auto wanted = static_cast<std::size_t>(index);
    while (wanted >= cache.size()) {
        // A static member sees the private ctor; the allocation is handed to the
        // shared_ptr immediately (the one raw new in this class).
        cache.emplace_back(new DummyTypeParameter(ownerType, static_cast<int>(cache.size())));
    }
    return cache[wanted];
}

std::shared_ptr<ITypeParameter> DummyTypeParameter::GetMethodTypeParameter(int index)
{
    auto& caches = Caches();
    std::lock_guard<std::mutex> lock(caches.Mutex);
    return GetTypeParameter(caches.Method, ::ILSpy::Decompiler::TypeSystem::SymbolKind::Method, index);
}

std::shared_ptr<ITypeParameter> DummyTypeParameter::GetClassTypeParameter(int index)
{
    auto& caches = Caches();
    std::lock_guard<std::mutex> lock(caches.Mutex);
    return GetTypeParameter(caches.Class, ::ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition, index);
}

ITypePtr DummyTypeParameter::AcceptVisitor(TypeVisitor& visitor)
{
    // The C# `override AcceptVisitor` dispatches straight to the type-parameter
    // visit (a dummy carries no children to recurse into).
    return visitor.VisitTypeParameter(*this);
}

ITypePtr DummyTypeParameter::ChangeNullability(TypeSystem::Nullability nullability)
{
    if (nullability == TypeSystem::Nullability::Oblivious) {
        return shared_from_this();
    }
    // shared_from_this() yields the single `IType` subobject's handle; up-cast it to
    // `ITypeParameter` (a dummy is single-inheritance). The wrapper is returned through
    // `ITypeParameter` because the direct `NullabilityAnnotatedTypeParameter` -> `IType`
    // conversion is AMBIGUOUS (the wrapper carries TWO non-virtual `IType` subobjects).
    auto wrapped = std::make_shared<NullabilityAnnotatedTypeParameter>(
        std::static_pointer_cast<ITypeParameter>(shared_from_this()), nullability);
    return std::static_pointer_cast<ITypeParameter>(wrapped);
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
