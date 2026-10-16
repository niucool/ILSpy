// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit to whom the Software is furnished to do so,
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

// Out-of-line `TypeParameterSubstitution` definitions. `VisitTypeParameter` reads
// `type.Index()` / `type.OwnerType()` (the `ITypeParameter` interface D383
// accessors), so it lives here (the header forward-declares `ITypeParameter` only);
// the `Compose` / `Equals` / `GetHashCode` / `ToString` / helper definitions are
// out-of-line here too, keeping the header lean. The `Identity` singleton is a
// function-local static (the Meyers-singleton pattern).

#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <string>

namespace ILSpy::Decompiler::TypeSystem {

const TypeParameterSubstitution& TypeParameterSubstitution::Identity() {
    static const TypeParameterSubstitution instance(std::nullopt, std::nullopt);
    return instance;
}

ITypePtr TypeParameterSubstitution::VisitTypeParameter(ITypeParameter& type) {
    int index = type.Index();
    if (classTypeArguments_.has_value() && type.OwnerType() == SymbolKind::TypeDefinition) {
        if (index >= 0 && index < static_cast<int>(classTypeArguments_->size()))
            return (*classTypeArguments_)[static_cast<std::size_t>(index)];
        else
            return UnknownType();
    } else if (methodTypeArguments_.has_value() && type.OwnerType() == SymbolKind::Method) {
        if (index >= 0 && index < static_cast<int>(methodTypeArguments_->size()))
            return (*methodTypeArguments_)[static_cast<std::size_t>(index)];
        else
            return UnknownType();
    } else {
        return TypeVisitor::VisitTypeParameter(type);
    }
}

TypeParameterSubstitution TypeParameterSubstitution::Compose(TypeParameterSubstitution* g,
                                                             TypeParameterSubstitution* f) {
    if (g == nullptr) {
        if (f == nullptr)
            return TypeParameterSubstitution(std::nullopt, std::nullopt);
        return *f;
    }
    if (f == nullptr || (!f->classTypeArguments_.has_value() && !f->methodTypeArguments_.has_value()))
        return *g;
    std::optional<std::vector<ITypePtr>> classTypeArguments =
        f->classTypeArguments_.has_value()
            ? std::optional<std::vector<ITypePtr>>(GetComposedTypeArguments(*f->classTypeArguments_, *g))
            : g->classTypeArguments_;
    std::optional<std::vector<ITypePtr>> methodTypeArguments =
        f->methodTypeArguments_.has_value()
            ? std::optional<std::vector<ITypePtr>>(GetComposedTypeArguments(*f->methodTypeArguments_, *g))
            : g->methodTypeArguments_;
    return TypeParameterSubstitution(std::move(classTypeArguments), std::move(methodTypeArguments));
}

std::vector<ITypePtr> TypeParameterSubstitution::GetComposedTypeArguments(
    const std::vector<ITypePtr>& input, TypeParameterSubstitution& substitution) {
    std::vector<ITypePtr> result;
    result.reserve(input.size());
    for (const auto& element : input) {
        // The C# `input[i].AcceptVisitor(substitution)` assumes a non-null element; guard
        // a null shared_ptr defensively (the minimal-port convention) rather than crash.
        result.push_back(element ? element->AcceptVisitor(substitution) : nullptr);
    }
    return result;
}

bool TypeParameterSubstitution::Equals(const TypeParameterSubstitution* other,
                                      TypeVisitor& normalization) const {
    if (other == nullptr) return false;
    return TypeListEquals(classTypeArguments_, other->classTypeArguments_, normalization)
        && TypeListEquals(methodTypeArguments_, other->methodTypeArguments_, normalization);
}

bool TypeParameterSubstitution::Equals(const TypeParameterSubstitution* other) const {
    if (other == nullptr) return false;
    return TypeListEquals(classTypeArguments_, other->classTypeArguments_)
        && TypeListEquals(methodTypeArguments_, other->methodTypeArguments_);
}

bool TypeParameterSubstitution::TypeListEquals(
    const std::optional<std::vector<ITypePtr>>& a,
    const std::optional<std::vector<ITypePtr>>& b) {
    if (!a.has_value() && !b.has_value()) return true;
    if (!a.has_value() || !b.has_value()) return false;
    if (a->size() != b->size()) return false;
    for (std::size_t i = 0; i < a->size(); ++i) {
        const ITypePtr& ai = (*a)[i];
        const ITypePtr& bi = (*b)[i];
        // The C# `a[i].Equals(b[i])` assumes non-null; guard nulls by pointer-identity.
        if (!ai || !bi) {
            if (ai.get() != bi.get()) return false;
        } else if (!ai->Equals(*bi)) {
            return false;
        }
    }
    return true;
}

bool TypeParameterSubstitution::TypeListEquals(
    const std::optional<std::vector<ITypePtr>>& a,
    const std::optional<std::vector<ITypePtr>>& b,
    TypeVisitor& normalization) {
    if (!a.has_value() && !b.has_value()) return true;
    if (!a.has_value() || !b.has_value()) return false;
    if (a->size() != b->size()) return false;
    for (std::size_t i = 0; i < a->size(); ++i) {
        const ITypePtr& ai = (*a)[i];
        const ITypePtr& bi = (*b)[i];
        if (!ai || !bi) {
            if (ai.get() != bi.get()) return false;
        } else {
            ITypePtr an = ai->AcceptVisitor(normalization);
            ITypePtr bn = bi->AcceptVisitor(normalization);
            if (!an->Equals(*bn)) return false;
        }
    }
    return true;
}

int TypeParameterSubstitution::TypeListHashCode(
    const std::optional<std::vector<ITypePtr>>& obj) {
    if (!obj.has_value()) return 0;
    int hashCode = 1;
    for (const auto& element : *obj) {
        hashCode *= 27;
        // The C# `element.GetHashCode()` is the `object.GetHashCode` identity hash
        // (`AbstractType.GetHashCode` returns `RuntimeHelpers.GetHashCode(this)`). The
        // C++ minimal `IType` port has no `GetHashCode`; hash the shared object's address
        // (the identity hash) and cast to int (the .NET int width), guarding a null element.
        hashCode += static_cast<int>(std::hash<IType*>{}(element.get()));
    }
    return hashCode;
}

int TypeParameterSubstitution::GetHashCode() const {
    // The C# `unchecked { return 1124131 * TypeListHashCode(classTypeArguments) +
    // 1821779 * TypeListHashCode(methodTypeArguments); }` -- port with unsigned
    // accumulation + int cast (the well-defined modular wraparound, no signed-overflow UB;
    // the D400 unchecked-wraparound convention).
    unsigned int h = 1124131u * static_cast<unsigned int>(TypeListHashCode(classTypeArguments_))
                   + 1821779u * static_cast<unsigned int>(TypeListHashCode(methodTypeArguments_));
    return static_cast<int>(h);
}

std::string TypeParameterSubstitution::ToString() const {
    std::string b;
    b.push_back('[');
    bool first = true;
    auto appendEntry = [&](char prefix1, const char* prefix2, int i, const ITypePtr& element) {
        if (first) first = false;
        else b += ", ";
        if (prefix2 != nullptr) {
            b += prefix2; // "``" for the method-arg double-backtick
        } else {
            b.push_back(prefix1); // '`' for the class-arg single-backtick
        }
        b += std::to_string(i);
        b += " -> ";
        b += element ? element->ReflectionName() : std::string("null");
    };
    if (classTypeArguments_.has_value()) {
        for (int i = 0; i < static_cast<int>(classTypeArguments_->size()); ++i)
            appendEntry('`', nullptr, i, (*classTypeArguments_)[static_cast<std::size_t>(i)]);
        if (classTypeArguments_->empty()) {
            if (first) first = false;
            else b += ", ";
            b += "[]";
        }
    }
    if (methodTypeArguments_.has_value()) {
        for (int i = 0; i < static_cast<int>(methodTypeArguments_->size()); ++i)
            appendEntry('\0', "``", i, (*methodTypeArguments_)[static_cast<std::size_t>(i)]);
        if (methodTypeArguments_->empty()) {
            if (first) first = false;
            else b += ", ";
            b += "[]";
        }
    }
    b.push_back(']');
    return b;
}

} // namespace ILSpy::Decompiler::TypeSystem
