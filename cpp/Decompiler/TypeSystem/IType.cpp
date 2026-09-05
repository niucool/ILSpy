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
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/TypeSystem/Implementation/GetMembersHelper.hpp"  // D490 routing arm

// FromSignature (the metadata walker's ProviderMethodSignature<ITypePtr>
// parameter, the PrimitiveTypeCode-based IsKnownType(KnownAttribute) helper,
// and the KnownAttribute marker table it reads).
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // IsKnownType

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

std::optional<bool> KnownType::IsReferenceType() const {
    // Faithful port of how a resolved known type's reference-ness derives from its
    // Kind (the MetadataTypeDefinition.cs `bool? IsReferenceType` switch): Struct /
    // Enum / Void are value types (false), every other kind is a reference type
    // (true); an unknown kind (a KnownTypeCode not in the table) is "not known"
    // (nullopt), faithful to the TypeKind.Unknown case where the reference-ness is
    // not derivable from the kind alone.
    TypeKind k = Kind();
    if (k == TypeKind::Unknown) return std::nullopt;
    switch (k) {
        case TypeKind::Struct:
        case TypeKind::Enum:
        case TypeKind::Void:
            return std::optional<bool>(false);
        default:
            return std::optional<bool>(true);
    }
}

// ---- ParameterizedType ----

std::string ParameterizedType::Name() const {
    return genericType_ ? genericType_->Name() : std::string();
}
std::string ParameterizedType::ReflectionName() const {
    // The C# (ParameterizedType.cs lines 129-144):
    // `genericType.ReflectionName + "[" + each "[" + arg.ReflectionName +
    // "]" joined by "," + "]"` -- e.g. `List`1[[System.String]]`.
    std::string s = genericType_ ? genericType_->ReflectionName() : std::string("?");
    s += "[";
    for (std::size_t i = 0; i < typeArgs_.size(); ++i) {
        if (i > 0) s += ",";
        s += "[";
        s += typeArgs_[i] ? typeArgs_[i]->ReflectionName() : "?";
        s += "]";
    }
    s += "]";
    return s;
}
bool ParameterizedType::StructuralEquals(const IType& other) const {
    // The C# `other as ParameterizedType` + null check (ParameterizedType.cs line 303:
    // `ParameterizedType c = other as ParameterizedType; if (c == null || ...) return
    // false;`). The RTTI check is LOAD-BEARING here, not an optimization: this class's
    // `Kind()` DELEGATES to the generic's Kind, so the `IType::Equals` Kind guard does NOT
    // distinguish a ParameterizedType over a Struct/Class generic from a plain
    // Struct/Class-kind type -- an unchecked static_cast would reinterpret an unrelated
    // same-Kind type's storage (UB). The C# `as` yields null for that shape and Equals
    // returns false; the port mirrors exactly that.
    const auto* o = dynamic_cast<const ParameterizedType*>(&other);
    if (o == nullptr)
        return false;
    if (typeArgs_.size() != o->typeArgs_.size()) return false;
    if (!genericType_ || !o->genericType_) return genericType_ == o->genericType_;
    if (!genericType_->Equals(*o->genericType_)) return false;
    for (std::size_t i = 0; i < typeArgs_.size(); ++i) {
        if (!typeArgs_[i]->Equals(*o->typeArgs_[i])) return false;
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

// ---- PinnedType ----

// The C# `Name`/`ReflectionName` are the element's plus the `" pinned"`
// NameSuffix (the TypeWithElementType convention: `elementType.Name +
// NameSuffix` / `elementType.ReflectionName + NameSuffix`).
std::string PinnedType::Name() const {
    return (element_ ? element_->Name() : std::string()) + " pinned";
}
std::string PinnedType::ReflectionName() const {
    return (element_ ? element_->ReflectionName() : std::string("?")) + " pinned";
}

// Faithful port of PinnedType.cs VisitChildren: reconstruct with the visited
// element if it changed, else return this.
ITypePtr PinnedType::VisitChildren(TypeVisitor& visitor) {
    if (!element_) return shared_from_this();
    ITypePtr e = element_->AcceptVisitor(visitor);
    if (e.get() == element_.get()) return shared_from_this();
    return std::make_shared<PinnedType>(std::move(e));
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
        // The C# SpecialType.NInt / NUInt singleton names (added when the
        // normalize-to-nint/nuint arm landed them; the rest of the table lands
        // with the remaining special singletons).
        case TypeKind::NInt: return "nint";
        case TypeKind::NUInt: return "nuint";
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
    // Reconstruct preserving rank / isSzArray / nullability (the C# carries
    // dimensions + nullability through the public ctor; the minimal port
    // carries rank + the SZArray flag through the full-field ctor).
    return std::shared_ptr<ArrayType>(new ArrayType(std::move(e), rank_,
        isSzArray_, nullability_));
}

// ArrayType.cs ChangeNullability: the same annotation returns this; a
// different one reconstructs with the new annotation (every other field
// carried over).
ITypePtr ArrayType::ChangeNullability(
    ::ILSpy::Decompiler::TypeSystem::Nullability nullability) {
    if (nullability == nullability_) {
        return shared_from_this();
    }
    return std::shared_ptr<ArrayType>(new ArrayType(element_, rank_,
        isSzArray_, nullability));
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

// ---- FunctionPointerType::FromSignature (the C# FunctionPointerType.cs lines
// 33-95) ----

namespace {

// The `modReturn.Modifier.Namespace` read (the C# IType : INamedElement
// `Namespace`): the port's minimal IType has no Namespace virtual, so the
// namespace derives from the shapes reachable at this site -- a resolved
// definition (IEntity::Namespace), an unresolvable TypeRef's UnknownType (the
// full name's top-level namespace), or a parameterized type (its generic
// definition's namespace, recursively). Everything else renders empty. The
// read is gated by the `"CallConv"`-prefixed Name check, which over real
// metadata means the marker is a real CallConv* definition or the
// module-undefined marker's UnknownType -- both covered.
std::string NamespaceOfModifier(const IType& type) {
    if (const IEntity* entity = dynamic_cast<const IEntity*>(&type))
        return entity->Namespace();
    // The elaborated `class` specifier: a colliding free function
    // `UnknownType()` lives in the namespace (the self-named null-object
    // factory), hiding the class name in ordinary lookup.
    if (const class UnknownType* unknown =
            dynamic_cast<const class UnknownType*>(&type))
        return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
    if (const ParameterizedType* pt =
            dynamic_cast<const ParameterizedType*>(&type)) {
        return pt->GenericType() ? NamespaceOfModifier(*pt->GenericType())
                                 : std::string();
    }
    return std::string();
}

} // namespace

std::shared_ptr<FunctionPointerType> FunctionPointerType::FromSignature(
    const Metadata::ProviderMethodSignature<ITypePtr>& signature) {
    ITypePtr returnType = signature.ReturnType;
    bool returnIsRefReadOnly = false;
    // The C# reads the SRM `SignatureHeader.CallingConvention`; the port's
    // TypeSystem enum mirrors it member-for-member (Default=0 ... VarArgs=5,
    // Unmanaged=9 -- the corrected value matching the decompiled
    // System.Reflection.Metadata 10), so the static_cast is the identity.
    SignatureCallingConvention callingConvention =
        static_cast<SignatureCallingConvention>(signature.Header.CallingConvention);
    std::vector<ITypePtr> customCallConvs;
    while (const ModifiedType* modReturn =
               dynamic_cast<const ModifiedType*>(returnType.get())) {
        const ITypePtr& modifier = modReturn->Modifier();
        if (modifier && IsKnownType(*modifier, KnownAttribute::In)) {
            // The `[In]`-marked modreq on a byref return: `ref readonly`.
            returnType = modReturn->Element();
            returnIsRefReadOnly = true;
        } else if (modifier && modifier->Name().rfind("CallConv", 0) == 0 &&
                   NamespaceOfModifier(*modifier) ==
                       "System.Runtime.CompilerServices") {
            returnType = modReturn->Element();
            if (callingConvention == SignatureCallingConvention::Unmanaged) {
                const std::string& name = modifier->Name();
                if (name == "CallConvCdecl") {
                    callingConvention = SignatureCallingConvention::CDecl;
                } else if (name == "CallConvFastcall") {
                    callingConvention = SignatureCallingConvention::FastCall;
                } else if (name == "CallConvStdcall") {
                    callingConvention = SignatureCallingConvention::StdCall;
                } else if (name == "CallConvThiscall") {
                    callingConvention = SignatureCallingConvention::ThisCall;
                } else {
                    customCallConvs.push_back(modifier);
                }
            } else {
                customCallConvs.push_back(modifier);
            }
        } else {
            break;
        }
    }
    std::vector<ITypePtr> parameterTypes;
    parameterTypes.reserve(signature.ParameterTypes.size());
    std::vector<ReferenceKind> parameterReferenceKinds;
    parameterReferenceKinds.reserve(signature.ParameterTypes.size());
    for (const ITypePtr& p : signature.ParameterTypes) {
        ITypePtr paramType = p;
        ReferenceKind kind = ReferenceKind::None;
        if (const ModifiedType* mod =
                dynamic_cast<const ModifiedType*>(paramType.get())) {
            if (mod->Modifier() &&
                IsKnownType(*mod->Modifier(), KnownAttribute::In)) {
                kind = ReferenceKind::In;
                paramType = mod->Element();
            } else if (mod->Modifier() &&
                       IsKnownType(*mod->Modifier(), KnownAttribute::Out)) {
                kind = ReferenceKind::Out;
                paramType = mod->Element();
            } else if (mod->Modifier() &&
                       IsKnownType(*mod->Modifier(),
                                  KnownAttribute::RequiresLocation)) {
                kind = ReferenceKind::RefReadOnly;
                paramType = mod->Element();
            }
        }
        if (paramType->Kind() == TypeKind::ByReference) {
            if (kind == ReferenceKind::None)
                kind = ReferenceKind::Ref;
        } else {
            kind = ReferenceKind::None;
        }
        parameterTypes.push_back(std::move(paramType));
        parameterReferenceKinds.push_back(kind);
    }
    return std::make_shared<FunctionPointerType>(
        callingConvention, std::move(customCallConvs), std::move(returnType),
        returnIsRefReadOnly, std::move(parameterTypes),
        std::move(parameterReferenceKinds));
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

// ---- IType member-enumeration defaults ----

// The faithful `AbstractType.GetMembers` default: GetMethods.Concat(GetProperties)
// .Concat(GetFields).Concat(GetEvents), with the caller's `Delegate<Predicate>` filter
// applied to the composed set and the caller's `GetMemberOptions` forwarded to every
// family. Defined here (not inline in the header) because the composed up-casts
// (`const IMethod*` / `const IProperty*` / `const IField*` / `const IEvent*` ->
// `const IMember*`) need the member-family headers complete, and those headers include
// `IType.hpp` transitively -- a header-side body would cycle the includes. A derived
// type overriding only the family virtuals sees them aggregated here.
std::vector<const IMember*> IType::GetMembers(std::function<bool(const IMember*)> filter,
                                              GetMemberOptions options) const {
    std::vector<const IMember*> members;
    auto append = [&](auto family) {
        for (const auto* m : family)
            if (!filter || filter(static_cast<const IMember*>(m)))
                members.push_back(static_cast<const IMember*>(m));
    };
    append(GetMethods(nullptr, options));
    append(GetProperties(nullptr, options));
    append(GetFields(nullptr, options));
    append(GetEvents(nullptr, options));
    return members;
}

// ---- ChangeNullability overrides ----
// The faithful C# per-type overrides (the AbstractType default -- ignore the change
// and return `this` -- is inline on IType; ArrayType inherits it because the port's
// ArrayType carries no nullability field).

// NullabilityAnnotatedType.cs: same nullability -> this, else forward to the
// unwrapped base type.
ITypePtr NullabilityAnnotatedType::ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) {
    if (nullability == nullability_) {
        return shared_from_this();
    }
    return baseType_->ChangeNullability(nullability);
}

// SpecialType.cs: only Dynamic annotates; Oblivious and every other kind return
// this (the C# compares against `base.Nullability`, the AbstractType Oblivious
// default).
ITypePtr SpecialType::ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) {
    if (nullability == Nullability::Oblivious || kind_ != TypeKind::Dynamic) {
        return shared_from_this();
    }
    return std::make_shared<NullabilityAnnotatedType>(shared_from_this(), nullability);
}

// ParameterizedType.cs: forward to the generic type; rebuild only when it changed
// (pointer identity is the C# `newGenericType == genericType` reference check).
ITypePtr ParameterizedType::ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) {
    if (!genericType_) {
        return shared_from_this();
    }
    ITypePtr newGenericType = genericType_->ChangeNullability(nullability);
    if (newGenericType.get() == genericType_.get()) {
        return shared_from_this();
    }
    return std::make_shared<ParameterizedType>(std::move(newGenericType), typeArgs_);
}

// ModifiedType.cs: forward to the element type; rebuild only when it changed.
ITypePtr ModifiedType::ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) {
    ITypePtr newElementType = element_ ? element_->ChangeNullability(nullability) : nullptr;
    if (newElementType.get() == element_.get()) {
        return shared_from_this();
    }
    return std::make_shared<ModifiedType>(modifier_, std::move(newElementType), isRequired_);
}

// UnknownType.cs (the Implementation/UnknownType.cs override): Oblivious (and
// known value types) return this; otherwise wrap in NullabilityAnnotatedType.
ITypePtr UnknownType::ChangeNullability(::ILSpy::Decompiler::TypeSystem::Nullability nullability) {
    if (nullability == Nullability::Oblivious || isReferenceType_ == false) {
        return shared_from_this();
    }
    return std::make_shared<NullabilityAnnotatedType>(shared_from_this(), nullability);
}

// ---- ParameterizedType substitution surface (D479) ----
// Faithful port of the three ParameterizedType.cs members the GetMembersHelper routing
// binds against. `GetTypeArgument` is the C# `typeArguments[index]` (no bounds check; the
// caller's contract). `GetSubstitution()` is `new TypeParameterSubstitution(typeArguments,
// null)`; the two-arg overload is `new TypeParameterSubstitution(typeArguments,
// methodTypeArguments)`. Both return BY VALUE (the C# heap allocation realized as a value,
// the D407 convention): a fresh `std::optional<std::vector<ITypePtr>>` holds copies of
// this type's `typeArgs_` (the managed IType objects are shared via the shared_ptrs), and
// the method list is `std::nullopt` (the no-arg overload) or the moved-in argument.

ITypePtr ParameterizedType::GetTypeArgument(int index) const {
    return typeArgs_[index];
}

TypeParameterSubstitution ParameterizedType::GetSubstitution() const {
    return TypeParameterSubstitution(
        std::optional<std::vector<ITypePtr>>(typeArgs_), std::nullopt);
}

TypeParameterSubstitution ParameterizedType::GetSubstitution(
    std::optional<std::vector<ITypePtr>> methodTypeArguments) const {
    return TypeParameterSubstitution(
        std::optional<std::vector<ITypePtr>>(typeArgs_), std::move(methodTypeArguments));
}

// ---- ParameterizedType member-enumeration routing arm (D490) ----
// The `else` branch of each `ParameterizedType.cs` member-enumeration override:
// `GetMembersHelper.GetXxx(this, ...)`. `GetMembersHelper` builds the `Specialized*` (owning
// `shared_ptr<const T>`); the `ParameterizedType` caches them in `mutable` members (lazy, built
// once with `nullptr` filter + `IgnoreInheritedMembers` -- the declared-specialized set
// `MemberLookup.LookupGroup` uses) and returns NON-OWNING `const T*` snapshots (the D477 "type
// system owns" convention), applying the caller's filter at return time. The
// `ReturnMemberDefinitions` arm (D489) delegates to `genericType_` unchanged. A `nullptr`
// `genericType_` (the defensive guard) yields empty. See the IType.hpp `ParameterizedType` comment.

namespace {
inline bool ptReturningDefs(GetMemberOptions options) {
    return (static_cast<std::int32_t>(options) &
            static_cast<std::int32_t>(GetMemberOptions::ReturnMemberDefinitions)) != 0;
}
} // namespace

std::vector<const IMethod*> ParameterizedType::GetMethods(
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const {
    if (ptReturningDefs(options))
        return genericType_ ? genericType_->GetMethods(filter, options) : std::vector<const IMethod*>{};
    if (methodsCache_.empty() && genericType_) {
        methodsCache_ = Implementation::GetMembersHelper::GetMethods(
            this, nullptr, GetMemberOptions::IgnoreInheritedMembers);
    }
    std::vector<const IMethod*> result;
    for (const auto& m : methodsCache_) {
        if (!filter || filter(m.get())) result.push_back(m.get());
    }
    return result;
}

std::vector<const IMethod*> ParameterizedType::GetConstructors(
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const {
    if (ptReturningDefs(options))
        return genericType_ ? genericType_->GetConstructors(filter, options) : std::vector<const IMethod*>{};
    if (constructorsCache_.empty() && genericType_) {
        constructorsCache_ = Implementation::GetMembersHelper::GetConstructors(
            this, nullptr, GetMemberOptions::IgnoreInheritedMembers);
    }
    std::vector<const IMethod*> result;
    for (const auto& m : constructorsCache_) {
        if (!filter || filter(m.get())) result.push_back(m.get());
    }
    return result;
}

std::vector<const IMethod*> ParameterizedType::GetAccessors(
    std::function<bool(const IMethod*)> filter,
    GetMemberOptions options) const {
    if (ptReturningDefs(options))
        return genericType_ ? genericType_->GetAccessors(filter, options) : std::vector<const IMethod*>{};
    if (accessorsCache_.empty() && genericType_) {
        accessorsCache_ = Implementation::GetMembersHelper::GetAccessors(
            this, nullptr, GetMemberOptions::IgnoreInheritedMembers);
    }
    std::vector<const IMethod*> result;
    for (const auto& m : accessorsCache_) {
        if (!filter || filter(m.get())) result.push_back(m.get());
    }
    return result;
}

std::vector<const IProperty*> ParameterizedType::GetProperties(
    std::function<bool(const IProperty*)> filter,
    GetMemberOptions options) const {
    if (ptReturningDefs(options))
        return genericType_ ? genericType_->GetProperties(filter, options) : std::vector<const IProperty*>{};
    if (propertiesCache_.empty() && genericType_) {
        propertiesCache_ = Implementation::GetMembersHelper::GetProperties(
            this, nullptr, GetMemberOptions::IgnoreInheritedMembers);
    }
    std::vector<const IProperty*> result;
    for (const auto& m : propertiesCache_) {
        if (!filter || filter(m.get())) result.push_back(m.get());
    }
    return result;
}

std::vector<const IField*> ParameterizedType::GetFields(
    std::function<bool(const IField*)> filter,
    GetMemberOptions options) const {
    if (ptReturningDefs(options))
        return genericType_ ? genericType_->GetFields(filter, options) : std::vector<const IField*>{};
    if (fieldsCache_.empty() && genericType_) {
        fieldsCache_ = Implementation::GetMembersHelper::GetFields(
            this, nullptr, GetMemberOptions::IgnoreInheritedMembers);
    }
    std::vector<const IField*> result;
    for (const auto& m : fieldsCache_) {
        if (!filter || filter(m.get())) result.push_back(m.get());
    }
    return result;
}

std::vector<const IEvent*> ParameterizedType::GetEvents(
    std::function<bool(const IEvent*)> filter,
    GetMemberOptions options) const {
    if (ptReturningDefs(options))
        return genericType_ ? genericType_->GetEvents(filter, options) : std::vector<const IEvent*>{};
    if (eventsCache_.empty() && genericType_) {
        eventsCache_ = Implementation::GetMembersHelper::GetEvents(
            this, nullptr, GetMemberOptions::IgnoreInheritedMembers);
    }
    std::vector<const IEvent*> result;
    for (const auto& m : eventsCache_) {
        if (!filter || filter(m.get())) result.push_back(m.get());
    }
    return result;
}

// ---- ParameterizedType.GetNestedTypes routing arm (D494) ----
// The `else` branch of the `ParameterizedType.cs` `GetNestedTypes` overrides:
// `GetMembersHelper.GetNestedTypes(this, ...)`. Unlike the member families (which return non-owning
// `const T*` and so cache the owning `Specialized*`), `GetNestedTypes` returns OWNING `ITypePtr`
// (shared_ptr) -- this helper returns `std::vector<ITypePtr>` directly, which is exactly what the
// override returns. NO owning cache needed. The `ReturnMemberDefinitions` arm (the `if`) delegates to
// `genericType_` unchanged. See the IType.hpp `ParameterizedType` comment.

std::vector<ITypePtr> ParameterizedType::GetNestedTypes(
    std::function<bool(const ITypeDefinition*)> filter,
    GetMemberOptions options) const {
    if (ptReturningDefs(options))
        return genericType_ ? genericType_->GetNestedTypes(filter, options) : std::vector<ITypePtr>{};
    return Implementation::GetMembersHelper::GetNestedTypes(this, filter, options);
}

std::vector<ITypePtr> ParameterizedType::GetNestedTypes(
    const std::vector<ITypePtr>& typeArguments,
    std::function<bool(const ITypeDefinition*)> filter,
    GetMemberOptions options) const {
    if (ptReturningDefs(options))
        return genericType_ ? genericType_->GetNestedTypes(typeArguments, filter, options)
                           : std::vector<ITypePtr>{};
    return Implementation::GetMembersHelper::GetNestedTypes(this, &typeArguments, filter, options);
}

} // namespace ILSpy::Decompiler::TypeSystem
