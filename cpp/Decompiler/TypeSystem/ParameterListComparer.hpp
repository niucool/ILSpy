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

// Port of ICSharpCode.Decompiler/TypeSystem/ParameterListComparer.cs -- the C# file
// holds TWO comparers: `ParameterListComparer` (compares parameter lists by the types
// of all parameters, normalizing method type parameters / object-vs-dynamic /
// tuples / modifiers / nullability so signatures compare by shape) and
// `SignatureComparer` (compares member signatures: short name + type parameter count
// + parameter list). `SignatureComparer.Ordinal` is the comparer the MemberLookup
// Lookup region and `InheritanceHelper.GetBaseMembers` use to decide whether two
// members have the same signature.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IReadOnlyList<IParameter>` comparer inputs port to
//      `const std::vector<const IParameter*>&` (the port's
//      `IParameterizedMember::Parameters()` snapshot type, D271). The C# null-list
//      arms (`x == null || y == null` in Equals; callers never pass GetHashCode a
//      null) are unexpressible and elided -- a C++ reference cannot be null; the
//      per-element null handling is kept verbatim.
//  (b) The C# `static readonly ParameterListComparer Instance` /
//      `static readonly SignatureComparer Ordinal` singletons port to Meyers
//      singletons (the D398 precedent, const references).
//  (c) The C# `normalizationVisitor` static field ports to a shared
//      function-local `NormalizeTypeVisitor` configured exactly like the C#
//      initializer (only `ReplaceClassTypeParametersWithDummy` deviates from the
//      field initializers). The visitor's AcceptVisitor entry point is non-const
//      (it may return `shared_from_this()`), so the comparer `const_cast`s the
//      parameter's `const IType&` to feed it -- the visitor never mutates the
//      visited type; it rebuilds or returns the same instance (documented
//      divergence-by-necessity for the non-const visitor protocol).
//  (d) The C# `ArgumentNullException` on a null `nameComparer` is unexpressible
//      (references are non-null); the comparer must outlive the SignatureComparer
//      (the static `Ordinal` comparer does).
//  (e) `GetHashCode` mixes the .NET hash-combine arithmetic modulo 2^32 (the C#
//      `unchecked` overflow) via unsigned accumulation, and the C# identity hash of
//      a normalized type (`object.GetHashCode` on the visited `IType`) ports to
//      `std::hash<const IType*>{}(ptr.get())` (the TypeParameterSubstitution D407
//      identity-hash precedent). Hash CODES are thus only stable within a process,
//      consistent with .NET.

#pragma once

#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"

#include <cstddef>
#include <functional>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Compares parameter lists by comparing the (normalized) types of all parameters.
// 'ref int' and 'out int' are considered equal -- unless `includeModifiers` is set.
// 'object' and 'dynamic' are also equal. `Method{T}(T a)` and `Method{S}(S b)` are
// considered equal; class type parameters are NOT normalized away (see the C#
// remarks).
class ParameterListComparer final {
public:
    // The C# `public static readonly ParameterListComparer Instance` (the
    // no-modifiers comparer).
    static const ParameterListComparer& Instance()
    {
        static const ParameterListComparer instance;
        return instance;
    }

    // The C# `public static ParameterListComparer WithOptions(bool includeModifiers
    // = false)` -- a fresh comparer with the given option set.
    static ParameterListComparer WithOptions(bool includeModifiers = false)
    {
        ParameterListComparer comparer;
        comparer.includeModifiers_ = includeModifiers;
        return comparer;
    }

    // The C# `bool Equals(IReadOnlyList<IParameter> x, IReadOnlyList<IParameter> y)`.
    bool Equals(const std::vector<const IParameter*>& x,
                const std::vector<const IParameter*>& y) const
    {
        if (&x == &y) { // the C# `x == y` reference-equality shortcut
            return true;
        }
        if (x.size() != y.size()) {
            return false;
        }
        NormalizeTypeVisitor& visitor = NormalizationVisitor();
        for (std::size_t i = 0; i < x.size(); ++i) {
            const IParameter* a = x[i];
            const IParameter* b = y[i];
            if (a == nullptr && b == nullptr) {
                continue;
            }
            if (a == nullptr || b == nullptr) {
                return false;
            }
            if (includeModifiers_) {
                if (a->ReferenceKind() != b->ReferenceKind()) {
                    return false;
                }
                if (a->IsParams() != b->IsParams()) {
                    return false;
                }
            }
            // We want to consider the parameter lists "Method<T>(T a)" and
            // "Method<S>(S b)" as equal. However, the parameter types are not
            // considered equal, as T is a different type parameter than S. In
            // order to compare the method signatures, we will normalize all method
            // type parameters.
            ITypePtr aType = const_cast<IType&>(a->Type()).AcceptVisitor(visitor);
            ITypePtr bType = const_cast<IType&>(b->Type()).AcceptVisitor(visitor);
            if (!aType->Equals(*bType)) {
                return false;
            }
        }
        return true;
    }

    // The C# `int GetHashCode(IReadOnlyList<IParameter> obj)` -- count-seeded,
    // then per-element 27*hash + the normalized type's identity hash.
    int GetHashCode(const std::vector<const IParameter*>& obj) const
    {
        unsigned int hashCode = static_cast<unsigned int>(obj.size());
        NormalizeTypeVisitor& visitor = NormalizationVisitor();
        for (const IParameter* p : obj) {
            if (p == nullptr) {
                // The C# `p.Type` would NRE on a null entry; the port skips the
                // entry's hash (a null entry is a malformed parameter list -- the
                // Equals path still treats it structurally).
                continue;
            }
            hashCode *= 27;
            ITypePtr type = const_cast<IType&>(p->Type()).AcceptVisitor(visitor);
            hashCode += static_cast<unsigned int>(std::hash<const IType*>{}(type.get()));
        }
        return static_cast<int>(hashCode);
    }

private:
    // The C# `static readonly NormalizeTypeVisitor normalizationVisitor = new
    // NormalizeTypeVisitor { ReplaceClassTypeParametersWithDummy = false, ... }` --
    // every other option keeps its `true` field-initializer.
    static NormalizeTypeVisitor& NormalizationVisitor()
    {
        static NormalizeTypeVisitor visitor = [] {
            NormalizeTypeVisitor v;
            v.ReplaceClassTypeParametersWithDummy = false;
            return v;
        }();
        return visitor;
    }

    // The C# `bool includeModifiers` field (set via WithOptions).
    bool includeModifiers_ = false;
};

// Compares member signatures: equal short name (under the configured comparer),
// equal type parameter count, and equal parameter types (via ParameterListComparer).
class SignatureComparer final {
public:
    // The C# `public SignatureComparer(StringComparer nameComparer)` -- the
    // comparer must outlive this object (the C# `if (nameComparer == null) throw`
    // arm is unexpressible for a reference).
    explicit SignatureComparer(const StringComparer& nameComparer)
        : nameComparer_(nameComparer) {}

    // The C# `public static readonly SignatureComparer Ordinal` -- ordinal
    // comparison for the member name.
    static const SignatureComparer& Ordinal()
    {
        static const SignatureComparer instance(StringComparer::Ordinal());
        return instance;
    }

    // The C# `bool Equals(IMember x, IMember y)`. Null members are admissible
    // (nullable raw pointers, the C# `x == null || y == null` arm).
    bool Equals(const IMember* x, const IMember* y) const
    {
        if (x == y) {
            return true;
        }
        if (x == nullptr || y == nullptr || x->SymbolKind() != y->SymbolKind() ||
            !nameComparer_.Equals(x->Name(), y->Name())) {
            return false;
        }
        const auto* px = dynamic_cast<const IParameterizedMember*>(x);
        const auto* py = dynamic_cast<const IParameterizedMember*>(y);
        if (px != nullptr && py != nullptr) {
            const auto* mx = dynamic_cast<const IMethod*>(x);
            const auto* my = dynamic_cast<const IMethod*>(y);
            if (mx != nullptr && my != nullptr &&
                mx->TypeParameters().size() != my->TypeParameters().size()) {
                return false;
            }
            return ParameterListComparer::Instance().Equals(px->Parameters(), py->Parameters());
        }
        return true;
    }

    // The C# `int GetHashCode(IMember obj)` -- symbol-kind * 33 + name hash, then
    // (parameterized members) 27*hash + parameter-list hash (+ type-parameter
    // count for methods). `obj` is non-null (the C# would NRE).
    int GetHashCode(const IMember* obj) const
    {
        unsigned int hash = static_cast<unsigned int>(obj->SymbolKind()) * 33u +
                            static_cast<unsigned int>(nameComparer_.GetHashCode(obj->Name()));
        if (const auto* pm = dynamic_cast<const IParameterizedMember*>(obj)) {
            hash *= 27u;
            hash += static_cast<unsigned int>(
                ParameterListComparer::Instance().GetHashCode(pm->Parameters()));
            if (const auto* m = dynamic_cast<const IMethod*>(obj)) {
                hash += static_cast<unsigned int>(m->TypeParameters().size());
            }
        }
        return static_cast<int>(hash);
    }

private:
    const StringComparer& nameComparer_;
};

} // namespace ILSpy::Decompiler::TypeSystem
