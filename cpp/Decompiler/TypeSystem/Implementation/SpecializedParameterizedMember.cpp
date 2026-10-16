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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Out-of-line members of `SpecializedParameterizedMember` (the header comment in
// `SpecializedParameterizedMember.hpp` documents the port conventions + deferrals).

#include "Decompiler/TypeSystem/Implementation/SpecializedParameterizedMember.hpp"

#include "Decompiler/TypeSystem/Implementation/SpecializedParameter.hpp"
#include "Decompiler/Util/LazyInit.hpp"

#include <utility>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `IReadOnlyList<IParameter> Parameters` -- lazily
// `CreateParameters(t => t.AcceptVisitor(this.Substitution))`, cached via `LazyInit`.
// The port caches the OWNING vector (`shared_ptr<vector<shared_ptr<IParameter>>>`), then
// returns a non-owning snapshot (`const IParameter*` per cached `SpecializedParameter`).
std::vector<const IParameter*> SpecializedParameterizedMember::Parameters() const {
    auto cached = Util::VolatileRead(&parameters_);
    if (!cached) {
        // The C# `t => t.AcceptVisitor(this.Substitution)`. The port's `Substitution()`
        // returns a `const TypeParameterSubstitution*`; `*Substitution()` is a `const&`,
        // but `IType::AcceptVisitor` takes a non-const `TypeVisitor&` (the D406 convention).
        // `const_cast` to a non-const ref is safe: the underlying `substitution_` is
        // `mutable` (non-const in a `const` method), and a substitution does not mutate
        // in an `AcceptVisitor` read (the D482 `const_cast` precedent). The `const_cast` on
        // the input `IType&` is the same D482 friction (`AcceptVisitor` is non-const).
        cached = Util::GetOrSet(&parameters_, CreateParameters(
            [this](const IType& t) -> ITypePtr {
                return const_cast<IType&>(t).AcceptVisitor(
                    const_cast<TypeParameterSubstitution&>(
                        *SpecializedMember::Substitution()));
            }));
    }
    // Build a non-owning snapshot of the cached owning vector.
    std::vector<const IParameter*> snapshot;
    if (cached) {
        snapshot.reserve(cached->size());
        for (const auto& sp : *cached) {
            snapshot.push_back(sp.get());
        }
    }
    return snapshot;
}

// The C# `protected IParameter[] CreateParameters(Func<IType, IType> substitution)` --
// builds the owning substituted parameter list. `paramDefs` is the base member's
// `Parameters()` snapshot (non-owning `const IParameter*`); each is wrapped in a
// `SpecializedParameter` (non-owning base pointer, owning new type, `this` as the
// non-owning owner).
std::shared_ptr<std::vector<std::shared_ptr<IParameter>>>
SpecializedParameterizedMember::CreateParameters(
    std::function<ITypePtr(const IType&)> substitution) const {
    auto owned = std::make_shared<std::vector<std::shared_ptr<IParameter>>>();
    // The C# `((IParameterizedMember)this.baseMember).Parameters`. The port downcasts
    // the `IMember` base member to `IParameterizedMember` via `dynamic_cast` (a null
    // result -- a base member that is not parameterized -- treated as no parameters).
    const auto* parameterizedBase =
        dynamic_cast<const IParameterizedMember*>(baseMember_.get());
    if (!parameterizedBase) {
        return owned; // empty (the base member is not parameterized)
    }
    auto paramDefs = parameterizedBase->Parameters(); // non-owning snapshot
    if (paramDefs.empty()) {
        return owned; // empty (the C# `Empty<IParameter>.Array`)
    }
    owned->reserve(paramDefs.size());
    // `this` as the `Owner` -- upcast to `const IParameterizedMember*` (unambiguous: one
    // `IParameterizedMember` subobject). Non-owning (the `SpecializedParameterizedMember`
    // owns the cache that owns the `SpecializedParameter`; the back-reference is non-owning).
    const IParameterizedMember* owner = this;
    for (const IParameter* p : paramDefs) {
        if (!p) {
            owned->push_back(nullptr);
            continue;
        }
        ITypePtr newType = substitution(p->Type());
        owned->push_back(std::make_shared<SpecializedParameter>(p, std::move(newType), owner));
    }
    return owned;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
