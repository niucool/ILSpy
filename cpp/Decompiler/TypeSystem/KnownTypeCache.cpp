// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Implementation of `KnownTypeCache` (see KnownTypeCache.hpp). The `FindType` body
// reads the cached slot via `LazyInit::VolatileRead` and, on a miss, builds the
// `IType` via `SearchType` and `LazyInit::GetOrSet`s it (first-writer-wins). The
// `SearchType` body walks `compilation.Modules`, returning the first
// `ITypeDefinition` a module's `GetTypeDefinition` finds (a non-owning alias), or
// the `UnknownType` fallback, or the `SpecialType.UnknownType` null object for
// `None`.

#include "Decompiler/TypeSystem/KnownTypeCache.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeReference.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/Util/LazyInit.hpp"

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// The C# `KnownTypeCache(ICompilation compilation)` -- the cache slots start empty
// (the `std::array` default-constructs empty `shared_ptr`s, the C# `null`).
KnownTypeCache::KnownTypeCache(const ICompilation& compilation)
    : compilation_(compilation) {}

// The C# `IType FindType(KnownTypeCode typeCode)`:
//   `IType type = LazyInit.VolatileRead(ref knownTypes[(int)typeCode]);`
//   `if (type != null) return type;`
//   `return LazyInit.GetOrSet(ref knownTypes[(int)typeCode], SearchType(typeCode));`
// The C++ port mirrors the three steps: read the cached slot (an empty
// `shared_ptr` is the C# `null`), return `*cached` on a hit, else `GetOrSet` the
// `SearchType` result and return `*cached`. `SearchType` is evaluated eagerly
// (before `GetOrSet`), matching the C# argument-evaluation order.
const IType& KnownTypeCache::FindType(KnownTypeCode typeCode) const
{
    const auto i = static_cast<std::size_t>(typeCode);
    std::shared_ptr<const IType> existing = Util::VolatileRead(&knownTypes_[i]);
    if (existing)
        return *existing;
    return *Util::GetOrSet(&knownTypes_[i], SearchType(typeCode));
}

// The C# `IType SearchType(KnownTypeCode typeCode)`:
//   `KnownTypeReference typeRef = KnownTypeReference.Get(typeCode);`
//   `if (typeRef == null) return SpecialType.UnknownType;`
//   `var typeName = new TopLevelTypeName(typeRef.Namespace, typeRef.Name, typeRef.TypeParameterCount);`
//   `foreach (IModule asm in compilation.Modules) {`
//   `    var typeDef = asm.GetTypeDefinition(typeName);`
//   `    if (typeDef != null) return typeDef;`
//   `}`
//   `return new UnknownType(typeName);`
// The C++ port mirrors the four branches:
//  (1) `None` (no `KnownTypeReference`) -> the `UnknownType()` convenience null
//      object (an OWNED `SpecialType(TypeKind::Unknown)`; the `UnknownType()`
//      FUNCTION, not the `class UnknownType` -- the two share the name via the C++
//      tag-vs-ordinary-namespace distinction, D417).
//  (2) a module has the type -> a NON-OWNING `shared_ptr<const IType>` aliasing the
//      module's `ITypeDefinition` (a no-op deleter; the module owns it for the
//      compilation's lifetime, so the `shared_ptr` does not delete it, convention
//      (d)). `static_cast<const IType*>(typeDef)` upcasts (`ITypeDefinition` IS-A
//      `IType` via `ITypeDefinitionOrUnknown`, D393); no `const_cast` because the
//      slot is `shared_ptr<const IType>`.
//  (3) no module has the type -> an OWNED `UnknownType(ns, name, tpc)` built via
//      `new class UnknownType(...)` (the elaborated-type-specifier dodges the MSVC
//      `make_shared<UnknownType>`-resolves-to-the-function quirk, D417). The C#
//      `new UnknownType(typeName)` uses the `FullTypeName` ctor which sets
//      `namespaceKnown = true` (the `TopLevelTypeName`'s `Name` is non-null); the
//      C++ `UnknownType` class ctor takes `(optional<string> ns, string name, int
//      tpc)`, so the namespace is passed as a PRESENT `optional` (the
//      `TopLevelTypeName`'s namespace, possibly empty) to mirror
//      `namespaceKnown = true`.
std::shared_ptr<const IType> KnownTypeCache::SearchType(KnownTypeCode typeCode) const
{
    const KnownTypeReference* typeRef = KnownTypeReference::Get(typeCode);
    if (typeRef == nullptr)
        return UnknownType(); // the SpecialType-based null object (owned)
    TopLevelTypeName typeName(std::string(typeRef->Namespace()),
                              std::string(typeRef->Name()),
                              typeRef->TypeParameterCount());
    for (const IModule* module : compilation_.Modules())
    {
        const ITypeDefinition* typeDef = module->GetTypeDefinition(typeName);
        if (typeDef != nullptr)
        {
            // Non-owning alias: the module owns the `ITypeDefinition` for the
            // compilation's lifetime; the no-op deleter ensures the `shared_ptr`
            // does not delete it when the cache slot is cleared. `static_cast`
            // upcasts `ITypeDefinition*` -> `IType*` (the `ITypeDefinition` IS-A
            // `IType` via `ITypeDefinitionOrUnknown`); no `const_cast` because the
            // slot is `shared_ptr<const IType>` (a `const IType*` binds directly).
            return std::shared_ptr<const IType>(
                static_cast<const IType*>(typeDef),
                [](const IType*){ /* no-op: the module owns the type definition */ });
        }
    }
    // The C# `new UnknownType(typeName)` -> `namespaceKnown = true` (the
    // `TopLevelTypeName` always has a known name); the namespace is passed as a
    // PRESENT `optional` (possibly an empty string) to mirror that.
    return std::shared_ptr<const IType>(
        new class UnknownType(
            std::optional<std::string>(std::string(typeRef->Namespace())),
            std::string(typeRef->Name()),
            typeRef->TypeParameterCount()));
}

} // namespace ILSpy::Decompiler::TypeSystem
