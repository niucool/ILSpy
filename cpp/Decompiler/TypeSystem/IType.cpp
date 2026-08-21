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
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <utility>
#include <vector>
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

// ---- ModifiedType ----

std::string ModifiedType::ReflectionName() const {
    std::string s = element_ ? element_->ReflectionName() : std::string("?");
    s += (isRequired_ ? " modreq" : " modopt");
    if (modifier_) {
        s += "(";
        s += modifier_->ReflectionName();
        s += ")";
    }
    return s;
}
bool ModifiedType::StructuralEquals(const IType& other) const {
    const auto& o = static_cast<const ModifiedType&>(other);
    return isRequired_ == o.isRequired_
        && modifier_->Equals(*o.modifier_)
        && element_->Equals(*o.element_);
}

// ---- TypeVisitor dispatch (D406) ----
// Faithful port of IType.cs AcceptVisitor / VisitChildren and the per-concrete-
// type overrides. AcceptVisitor hands the type to the TypeVisitor's matching
// Visit* method; the Visit* defaults (in TypeVisitor.hpp / .cpp) recurse via
// VisitChildren. The C++-only minimal types (KnownType / SimpleType /
// SpecialType / TypeParameter) and the not-yet-concrete interfaces
// (ITypeDefinition / ITypeParameter) inherit IType's AcceptVisitor default
// (VisitOtherType) and VisitChildren default (shared_from_this). The 8 "has
// children" types below override both: AcceptVisitor dispatches to the
// dedicated Visit* method, VisitChildren recurses into the children and
// reconstructs (via make_shared) iff a child changed, else returns
// shared_from_this() -- the C# `return this` reference-identity. The "child
// changed" test is pointer-identity (`r.get() != stored.get()`), mirroring the
// C# `r != typeArguments[i]` reference equality.

ITypePtr IType::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitOtherType(*this);
}

ITypePtr ParameterizedType::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitParameterizedType(*this);
}

ITypePtr ParameterizedType::VisitChildren(TypeVisitor& visitor) {
    ITypePtr g = genericType_ ? genericType_->AcceptVisitor(visitor) : nullptr;
    std::vector<ITypePtr> types;
    types.reserve(typeArgs_.size());
    bool changed = (g.get() != genericType_.get());
    for (std::size_t i = 0; i < typeArgs_.size(); ++i) {
        ITypePtr r = typeArgs_[i]->AcceptVisitor(visitor);
        if (r.get() != typeArgs_[i].get()) changed = true;
        types.push_back(std::move(r));
    }
    if (!changed) return shared_from_this();
    return std::make_shared<ParameterizedType>(std::move(g), std::move(types));
}

ITypePtr ArrayType::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitArrayType(*this);
}

ITypePtr ArrayType::VisitChildren(TypeVisitor& visitor) {
    if (!element_) return shared_from_this();
    ITypePtr e = element_->AcceptVisitor(visitor);
    if (e.get() == element_.get()) return shared_from_this();
    // Reconstruct preserving rank / isSzArray (the C# carries dimensions +
    // nullability; the minimal port carries rank + the SZArray flag).
    if (isSzArray_) return std::make_shared<ArrayType>(std::move(e));
    return std::make_shared<ArrayType>(std::move(e), rank_);
}

ITypePtr ByReferenceType::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitByReferenceType(*this);
}

ITypePtr ByReferenceType::VisitChildren(TypeVisitor& visitor) {
    if (!element_) return shared_from_this();
    ITypePtr e = element_->AcceptVisitor(visitor);
    if (e.get() == element_.get()) return shared_from_this();
    return std::make_shared<ByReferenceType>(std::move(e));
}

ITypePtr PointerType::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitPointerType(*this);
}

ITypePtr PointerType::VisitChildren(TypeVisitor& visitor) {
    if (!element_) return shared_from_this();
    ITypePtr e = element_->AcceptVisitor(visitor);
    if (e.get() == element_.get()) return shared_from_this();
    return std::make_shared<PointerType>(std::move(e));
}

ITypePtr ModifiedType::AcceptVisitor(TypeVisitor& visitor) {
    return isRequired_ ? visitor.VisitModReq(*this) : visitor.VisitModOpt(*this);
}

ITypePtr ModifiedType::VisitChildren(TypeVisitor& visitor) {
    ITypePtr newElement = element_ ? element_->AcceptVisitor(visitor) : nullptr;
    ITypePtr newModifier = modifier_ ? modifier_->AcceptVisitor(visitor) : nullptr;
    if (newModifier.get() == modifier_.get() && newElement.get() == element_.get())
        return shared_from_this();
    return std::make_shared<ModifiedType>(std::move(newModifier), std::move(newElement), isRequired_);
}

ITypePtr NullabilityAnnotatedType::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitNullabilityAnnotatedType(*this);
}

ITypePtr NullabilityAnnotatedType::VisitChildren(TypeVisitor& visitor) {
    if (!baseType_) return shared_from_this();
    ITypePtr newBase = baseType_->AcceptVisitor(visitor);
    if (newBase.get() == baseType_.get()) return shared_from_this();
    // Minimal-port reconstruction: rewrap the visited base with the same nullability.
    // The C# ChangeNullability / IsReferenceType / TypeParameter edge cases (T!
    // substituted with U? -> U?, etc.) land with the Phase 2 nullability-lifting
    // stage that consumes TypeVisitor; this leaf reconstructs the wrapper directly.
    return std::make_shared<NullabilityAnnotatedType>(std::move(newBase), nullability_);
}

ITypePtr FunctionPointerType::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitFunctionPointerType(*this);
}

ITypePtr FunctionPointerType::VisitChildren(TypeVisitor& visitor) {
    ITypePtr r = returnType_ ? returnType_->AcceptVisitor(visitor) : nullptr;
    std::vector<ITypePtr> pt;
    pt.reserve(parameterTypes_.size());
    bool changed = (r.get() != returnType_.get());
    for (std::size_t i = 0; i < parameterTypes_.size(); ++i) {
        ITypePtr p = parameterTypes_[i]->AcceptVisitor(visitor);
        if (p.get() != parameterTypes_[i].get()) changed = true;
        pt.push_back(std::move(p));
    }
    if (!changed) return shared_from_this();
    // Custom calling conventions are carried over unvisited (matching the C# source,
    // which visits only ReturnType + ParameterTypes); parameter reference kinds are
    // carried over verbatim.
    return std::make_shared<FunctionPointerType>(callingConvention_, customCallingConventions_,
                                                 std::move(r), returnIsRefReadOnly_,
                                                 std::move(pt), parameterReferenceKinds_);
}

ITypePtr TupleType::AcceptVisitor(TypeVisitor& visitor) {
    return visitor.VisitTupleType(*this);
}

ITypePtr TupleType::VisitChildren(TypeVisitor& visitor) {
    bool changed = false;
    std::vector<ITypePtr> newElementTypes;
    newElementTypes.reserve(elementTypes_.size());
    for (std::size_t i = 0; i < elementTypes_.size(); ++i) {
        ITypePtr newType = elementTypes_[i]->AcceptVisitor(visitor);
        if (newType.get() != elementTypes_[i].get()) changed = true;
        newElementTypes.push_back(std::move(newType));
    }
    if (!changed) return shared_from_this();
    // Minimal-port reconstruction: carry over the underlying ValueTuple<...> type
    // and the element names; the C# Compilation / GetDefinition().ParentModule
    // reconstruction inputs are deferred to the Phase 2 type-resolution stage.
    return std::make_shared<TupleType>(underlyingType_, std::move(newElementTypes), elementNames_);
}

} // namespace ILSpy::Decompiler::TypeSystem
