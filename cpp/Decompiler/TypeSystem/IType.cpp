// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/TypeSystem/IType.hpp"

#include <string>

namespace ILSpy::Decompiler::TypeSystem {

// ---- KnownType ----

TypeKind KnownType::Kind() const {
    const auto* r = LookupKnownType(code_);
    return r ? r->Kind : TypeKind::Unknown;
}
std::string KnownType::Name() const {
    const auto* r = LookupKnownType(code_);
    return r ? std::string(r->Name) : std::string();
}
std::string KnownType::ReflectionName() const {
    const auto* r = LookupKnownType(code_);
    if (!r) return "?";
    std::string s;
    if (!r->Namespace.empty()) { s += r->Namespace; s += '.'; }
    s += r->Name;
    return s;
}
int KnownType::TypeParameterCount() const {
    const auto* r = LookupKnownType(code_);
    return r ? r->TypeParameterCount : 0;
}

// ---- ParameterizedType ----

std::string ParameterizedType::Name() const {
    return genericType_ ? genericType_->Name() : std::string();
}
std::string ParameterizedType::ReflectionName() const {
    std::string s = genericType_ ? genericType_->ReflectionName() : std::string("?");
    s += "<";
    for (std::size_t i = 0; i < typeArgs_.size(); ++i) {
        if (i) s += ", ";
        s += typeArgs_[i] ? typeArgs_[i]->ReflectionName() : "?";
    }
    s += ">";
    return s;
}
bool ParameterizedType::StructuralEquals(const IType& other) const {
    const auto& o = static_cast<const ParameterizedType&>(other);
    if (typeArgs_.size() != o.typeArgs_.size()) return false;
    if (!genericType_ || !o.genericType_) return genericType_ == o.genericType_;
    if (!genericType_->Equals(*o.genericType_)) return false;
    for (std::size_t i = 0; i < typeArgs_.size(); ++i) {
        if (!typeArgs_[i]->Equals(*o.typeArgs_[i])) return false;
    }
    return true;
}

// ---- ArrayType ----

std::string ArrayType::Name() const {
    return element_ ? element_->Name() : std::string();
}
std::string ArrayType::ReflectionName() const {
    std::string s = element_ ? element_->ReflectionName() : std::string("?");
    if (isSzArray_) { s += "[]"; return s; }
    s += "[";
    for (int i = 1; i < rank_; ++i) s += ",";
    s += "]";
    return s;
}
bool ArrayType::StructuralEquals(const IType& other) const {
    const auto& o = static_cast<const ArrayType&>(other);
    return rank_ == o.rank_ && isSzArray_ == o.isSzArray_ &&
           element_->Equals(*o.element_);
}

// ---- ByReferenceType / PointerType ----

std::string ByReferenceType::ReflectionName() const {
    std::string s = element_ ? element_->ReflectionName() : std::string("?");
    s += "&";
    return s;
}
std::string PointerType::ReflectionName() const {
    std::string s = element_ ? element_->ReflectionName() : std::string("?");
    s += "*";
    return s;
}

// ---- TypeParameter ----

std::string TypeParameter::ReflectionName() const {
    // C# uses "T" for class params and "!!T"/"!T" tokens; for diagnostics we
    // emit the name if known, else a placeholder index form.
    if (!name_.empty()) return name_;
    return owner_ == OwnerKind::Method ? "!!" + std::to_string(index_) : "!" + std::to_string(index_);
}

// ---- SpecialType ----

std::string SpecialType::Name() const {
    switch (kind_) {
        case TypeKind::Unknown: return "?";
        case TypeKind::Null: return "null";
        case TypeKind::None: return "None";
        case TypeKind::Dynamic: return "dynamic";
        case TypeKind::UnboundTypeArgument: return "?";
        default: return "?";
    }
}

} // namespace ILSpy::Decompiler::TypeSystem
