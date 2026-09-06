// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so.
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The MetadataMethod.cpp half of the MetadataMethod port (the header carries
// the conventions).

#include "Decompiler/TypeSystem/Implementation/MetadataMethod.hpp"

#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MethodSemanticsLookup.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/TypeSystem/ApplyAttributeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeDefinition.hpp"
#include "Decompiler/TypeSystem/Implementation/MetadataTypeParameter.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedMethod.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"

#include <cstdio>
#include <stdexcept>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

namespace {

// The raw ECMA-335 II.23.1.11 MethodAttributes bits the entity consults
// (the System.Reflection.MethodAttributes enum; the Disassembler layer's
// ReflectionAttributes.hpp copy is not included from the TypeSystem layer --
// the MetadataParameter raw-flags convention).
constexpr std::uint32_t kMemberAccessMask = 0x0007;
constexpr std::uint32_t kStatic = 0x0010;
constexpr std::uint32_t kFinal = 0x0020;
constexpr std::uint32_t kVirtual = 0x0040;
constexpr std::uint32_t kNewSlot = 0x0100;
constexpr std::uint32_t kAbstract = 0x0400;
constexpr std::uint32_t kSpecialName = 0x0800;
constexpr std::uint32_t kRTSpecialName = 0x1000;

// The C# `const MethodAttributes finalizerAttributes = (MethodAttributes.
// Virtual | MethodAttributes.Family | MethodAttributes.HideBySig)`.
constexpr std::uint32_t kFinalizerAttributes = 0x0040 | 0x0004 | 0x0080;

// The SRMExtensions HasBody extension's masks:
// `noBodyAttrs = Abstract | PinvokeImpl`, `noBodyImplAttrs = InternalCall |
// Native | Unmanaged | Runtime` (the probed BCL values: IL=0, Native=1,
// Runtime=3, Unmanaged=4, InternalCall=0x1000).
constexpr std::uint32_t kNoBodyAttrs = 0x0400 | 0x2000;
constexpr std::uint32_t kNoBodyImplAttrs = 0x1000 | 0x0001 | 0x0004 | 0x0003;

// The C# `mod.Modifier.Namespace` -- the port's `IType` carries no `Namespace`
// member (the documented minimal-IType divergence), so the read routes through
// the entity/parameterized/unknown dispatch -- the MetadataField.cpp
// NamespaceOf convention (copied next to its second consumer).
std::string NamespaceOf(const IType& type)
{
    if (const auto* entity = dynamic_cast<const IEntity*>(&type))
        return entity->Namespace();
    if (const auto* pt = dynamic_cast<const ParameterizedType*>(&type))
        return pt->GenericType() ? NamespaceOf(*pt->GenericType())
                                 : std::string();
    if (const auto* unknown =
            dynamic_cast<const class UnknownType*>(&type))
        return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
    return std::string();
}

} // namespace

// The ctor (the header convention (a)): the eager Attributes read, the
// MethodSemanticsLookup consultation, the type-parameter creation, and the
// symbolKind chain.
MetadataMethod::MetadataMethod(const MetadataModule& module,
    std::uint32_t methodToken)
    : module_(module),
      handle_(methodToken),
      attr_(module.MetadataFile()->GetMethodAttributes(methodToken)),
      symbolKind_(::ILSpy::Decompiler::TypeSystem::SymbolKind::Method),
      accessorOwner_(0),
      accessorKind_(::ILSpy::Decompiler::TypeSystem::
                        MethodSemanticsAttributes::None),
      isExtensionMethod_(false)
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    // The C# `var (accessorOwner, semanticsAttribute) =
    // module.MetadataFile.MethodSemanticsLookup.GetSemantics(handle);` --
    // the miss shape is (nil, 0) (the SemanticsInfo defaults).
    Metadata::MethodSemanticsLookup::SemanticsInfo semantics =
        metadata->GetMethodSemanticsLookup().GetSemantics(handle_);
    // The C# `this.typeParameters = MetadataTypeParameter.Create(module,
    // this, def.GetGenericParameters())` -- the method-scoped GenericParam
    // rows (the owner is the method itself; the C# ctor's `this` escape).
    typeParameters_ = MetadataTypeParameter::Create(
        module_, this, metadata->GetGenericParameters(handle_));
    std::uint32_t accessorTable = semantics.AssociationToken >> 24;
    if (semantics.Semantics
            != ::ILSpy::Decompiler::TypeSystem::
                MethodSemanticsAttributes::None
        && semantics.AssociationToken != 0
        && (accessorTable == 0x17 || accessorTable == 0x14))
    {
        // The C# accessor arm: a property (0x17) or event (0x14)
        // association.
        symbolKind_ = ::ILSpy::Decompiler::TypeSystem::SymbolKind::Accessor;
        accessorOwner_ = semantics.AssociationToken;
        accessorKind_ = semantics.Semantics;
    }
    else if ((attr_ & (kSpecialName | kRTSpecialName)) != 0
        && typeParameters_.empty())
    {
        // The C# ctor/operator arm over the special-name methods.
        std::string name = Name();
        if (name == ".cctor" || name == ".ctor")
        {
            symbolKind_ =
                ::ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor;
        }
        else if (name.rfind("op_", 0) == 0
            && ::ILSpy::Decompiler::CSharp::Syntax::OperatorDeclaration::
                GetOperatorType(name)
                .has_value())
        {
            symbolKind_ =
                ::ILSpy::Decompiler::TypeSystem::SymbolKind::Operator;
        }
    }
    else if ((attr_ & kFinalizerAttributes) == kFinalizerAttributes
        && typeParameters_.empty())
    {
        // The C# Finalize arm: a virtual family hidebysig parameterless
        // void method on a class kind.
        if (Name() == "Finalize" && Parameters().empty()
            && IsKnownType(ReturnType(), KnownTypeCode::Void))
        {
            const auto* decl = dynamic_cast<const MetadataTypeDefinition*>(
                DeclaringTypeDefinition());
            if (decl != nullptr && decl->Kind() == TypeKind::Class)
            {
                symbolKind_ =
                    ::ILSpy::Decompiler::TypeSystem::SymbolKind::Destructor;
            }
        }
    }
    else if ((attr_ & kStatic) != 0 && typeParameters_.empty())
    {
        // Operators that are explicit interface implementations are not
        // marked with SpecialName or RTSpecialName (the C# comment): a
        // static `IFace.op_XXX` method takes the last-segment re-test.
        std::string name = Name();
        std::size_t index = name.rfind('.');
        if (index != std::string::npos && index != 0
            && index < name.size())
        {
            std::string tail = name.substr(index + 1);
            if (tail.rfind("op_", 0) == 0
                && ::ILSpy::Decompiler::CSharp::Syntax::OperatorDeclaration::
                    GetOperatorType(tail)
                    .has_value())
            {
                symbolKind_ =
                    ::ILSpy::Decompiler::TypeSystem::SymbolKind::Operator;
            }
        }
    }
    // The C# `this.IsExtensionMethod = (attributes & Static) ==
    // MethodAttributes.Static && (module.TypeSystemOptions &
    // TypeSystemOptions.ExtensionMethods) == TypeSystemOptions.ExtensionMethods
    // && def.GetCustomAttributes().HasKnownAttribute(metadata,
    // KnownAttribute.Extension);`
    isExtensionMethod_ = (attr_ & kStatic) == kStatic
        && (module_.TypeSystemOptions()
            & TypeSystemOptions::ExtensionMethods)
            == TypeSystemOptions::ExtensionMethods
        && Metadata::HasKnownAttribute(
            *metadata, handle_, KnownAttribute::Extension);
}

// The C# `public override string ToString() =>
// $"{MetadataTokens.GetToken(handle):X8} {DeclaringType?.ReflectionName}.{Name}"`.
std::string MetadataMethod::ToString() const
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08X", handle_);
    std::string result(buf);
    result += " ";
    if (DeclaringType() != nullptr)
        result += DeclaringType()->ReflectionName();
    result += ".";
    result += Name();
    return result;
}

// The C# `public string Name` -- the lazy per-row read.
std::string MetadataMethod::Name() const
{
    if (name_)
        return *name_;
    name_ = module_.MetadataFile()->GetMethodName(handle_);
    return *name_;
}

// The C# `SymbolKind ISymbol.SymbolKind => symbolKind` (the GLOBALLY
// qualified return type, the D372 crux).
::ILSpy::Decompiler::TypeSystem::SymbolKind MetadataMethod::SymbolKind()
    const
{
    return symbolKind_;
}

// The C# `public Accessibility Accessibility => GetAccessibility(attributes)`.
::ILSpy::Decompiler::TypeSystem::Accessibility MetadataMethod::Accessibility()
    const
{
    return GetAccessibility(attr_);
}

// The C# `internal static Accessibility GetAccessibility(
// MethodAttributes attr)` -- the MemberAccessMask switch.
::ILSpy::Decompiler::TypeSystem::Accessibility MetadataMethod::GetAccessibility(
    std::uint32_t attr)
{
    switch (attr & kMemberAccessMask)
    {
        case 0x6:  // Public
            return ::ILSpy::Decompiler::TypeSystem::Accessibility::Public;
        case 0x3:  // Assembly
            return ::ILSpy::Decompiler::TypeSystem::Accessibility::Internal;
        case 0x1:  // Private
            return ::ILSpy::Decompiler::TypeSystem::Accessibility::Private;
        case 0x4:  // Family
            return ::ILSpy::Decompiler::TypeSystem::Accessibility::Protected;
        case 0x2:  // FamANDAssem
            return ::ILSpy::Decompiler::TypeSystem::Accessibility::
                ProtectedAndInternal;
        case 0x5:  // FamORAssem
            return ::ILSpy::Decompiler::TypeSystem::Accessibility::
                ProtectedOrInternal;
        default:
            return ::ILSpy::Decompiler::TypeSystem::Accessibility::None;
    }
}

// The C# `public string FullName => $"{DeclaringType?.FullName}.{Name}"`
// (the null declaring type renders as the empty string, convention (i)) --
// the port's `IType` carries no `FullName` member (the documented
// minimal-IType divergence), so the read routes through the declaring
// definition (the MetadataField.cpp precedent).
std::string MetadataMethod::FullName() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return (decl != nullptr ? decl->FullName() : std::string()) + "."
        + Name();
}

std::string MetadataMethod::ReflectionName() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return (decl != nullptr ? decl->ReflectionName() : std::string()) + "."
        + Name();
}

// The C# `public string Namespace =>
// DeclaringType?.Namespace ?? string.Empty`.
std::string MetadataMethod::Namespace() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return decl != nullptr ? decl->Namespace() : std::string();
}

// --- IMethod ---

// The C# `IReadOnlyList<ITypeParameter> TypeParameters => typeParameters` --
// the non-owning snapshot over the owning array.
std::vector<const ITypeParameter*> MetadataMethod::TypeParameters() const
{
    std::vector<const ITypeParameter*> result;
    result.reserve(typeParameters_.size());
    for (const auto& tp : typeParameters_)
        result.push_back(tp.get());
    return result;
}

// The C# `IReadOnlyList<IType> IMethod.TypeArguments => typeParameters` --
// the type parameters themselves (convention (k)); the shared handles alias
// the owning instances.
std::vector<ITypePtr> MetadataMethod::TypeArguments() const
{
    std::vector<ITypePtr> result;
    result.reserve(typeParameters_.size());
    for (const auto& tp : typeParameters_)
    {
        result.push_back(ITypePtr(
            const_cast<ITypeParameter*>(tp.get()), [](ITypeParameter*) {
                // no-op: the method owns its type parameters
            }));
    }
    return result;
}

bool MetadataMethod::IsConstructor() const
{
    return symbolKind_
        == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor;
}

bool MetadataMethod::IsDestructor() const
{
    return symbolKind_
        == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Destructor;
}

bool MetadataMethod::IsOperator() const
{
    return symbolKind_ == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Operator;
}

bool MetadataMethod::IsAccessor() const
{
    return symbolKind_
        == ::ILSpy::Decompiler::TypeSystem::SymbolKind::Accessor;
}

// The C# `public bool HasBody` -- the SRMExtensions.HasBody extension:
// neither the no-body MethodAttributes nor the no-body MethodImplAttributes
// is set, and the RVA is positive.
bool MetadataMethod::HasBody() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    return (attr_ & kNoBodyAttrs) == 0
        && (metadata->GetMethodImplAttributes(handle_) & kNoBodyImplAttrs)
            == 0
        && metadata->GetMethodRVA(handle_) > 0;
}

// The C# `public IMember AccessorOwner` -- the loud DEFERRAL gated on the
// property/event entity caches (convention (e)).
const IMember* MetadataMethod::AccessorOwner() const
{
    if (accessorOwner_ == 0)
        return nullptr;
    throw std::logic_error(
        "MetadataMethod::AccessorOwner: the MetadataProperty/"
        "MetadataEvent entity caches are not yet ported");
}

::ILSpy::Decompiler::TypeSystem::MethodSemanticsAttributes
MetadataMethod::AccessorKind() const
{
    return accessorKind_;
}

bool MetadataMethod::IsExtensionMethod() const
{
    return isExtensionMethod_;
}

// DEFERRED (convention (c)): the AttributeListBuilder machinery.
std::vector<const IAttribute*>
MetadataMethod::GetReturnTypeAttributes() const
{
    throw std::logic_error(
        "MetadataMethod::GetReturnTypeAttributes: the AttributeListBuilder "
        "is not yet ported");
}

// The C# `public bool ReturnTypeIsRefReadOnly`: the ThreeState-cached
// [IsReadOnly] classification over the method's seq-0 Param row (the
// return-value row).
bool MetadataMethod::ReturnTypeIsRefReadOnly() const
{
    if (returnTypeIsRefReadonly_ != 0)
        return returnTypeIsRefReadonly_ == 2;
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    bool hasReadOnlyAttr = false;
    std::vector<Metadata::ParameterInfo> params =
        metadata->GetParameters(handle_);
    if (!params.empty())
    {
        const Metadata::ParameterInfo& retParam = params.front();
        if (retParam.SequenceNumber == 0)
        {
            hasReadOnlyAttr = Metadata::HasKnownAttribute(*metadata,
                retParam.Token, KnownAttribute::IsReadOnly);
        }
    }
    returnTypeIsRefReadonly_ = hasReadOnlyAttr ? 2 : 1;
    return hasReadOnlyAttr;
}

// The C# `public bool IsInitOnly`: forces the signature decode (the
// modreq(IsExternalInit) test computed with the return type).
bool MetadataMethod::IsInitOnly() const
{
    if (!signatureDecoded_)
        DecodeSignature();
    return isInitOnly_;
}

// The C# `public bool ThisIsRefReadOnly`: the declaring type's IsReadOnly
// flag OR (under the ReadOnlyMethods option) the method's own [IsReadOnly]
// row.
bool MetadataMethod::ThisIsRefReadOnly() const
{
    if (thisIsRefReadonly_ != 0)
        return thisIsRefReadonly_ == 2;
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    bool hasReadOnlyAttr = false;
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    if (decl != nullptr)
        hasReadOnlyAttr = decl->IsReadOnly();
    if ((module_.TypeSystemOptions() & TypeSystemOptions::ReadOnlyMethods)
        == TypeSystemOptions::ReadOnlyMethods)
    {
        hasReadOnlyAttr = hasReadOnlyAttr
            || Metadata::HasKnownAttribute(*metadata, handle_,
                KnownAttribute::IsReadOnly);
    }
    thisIsRefReadonly_ = hasReadOnlyAttr ? 2 : 1;
    return hasReadOnlyAttr;
}

// --- IParameterizedMember ---

// The C# `public IReadOnlyList<IParameter> Parameters` -- the snapshot over
// the signature-decoded owning array.
std::vector<const IParameter*> MetadataMethod::Parameters() const
{
    if (!signatureDecoded_)
        DecodeSignature();
    std::vector<const IParameter*> result;
    result.reserve(parameters_.size());
    for (const auto& p : parameters_)
        result.push_back(p.get());
    return result;
}

// --- IMember ---

// The C# `public IType ReturnType` -- never null (the catch arm caches
// SpecialType.UnknownType).
const IType& MetadataMethod::ReturnType() const
{
    if (!signatureDecoded_)
        DecodeSignature();
    return *returnType_;
}

// The C# `IMember IMember.MemberDefinition => this`.
const IMember* MetadataMethod::MemberDefinition() const
{
    return this;
}

// The C# `IEnumerable<IMember> ExplicitlyImplementedInterfaceMembers` --
// the declaring type's `GetOverrides(handle)` walk (the MethodImpl rows
// whose MethodBody is this method, each MethodDeclaration resolved through
// `module.ResolveMethod` over the declaring type's type parameters).
std::vector<const IMember*>
MetadataMethod::ExplicitlyImplementedInterfaceMembers() const
{
    const auto* typeDef = dynamic_cast<
        const Implementation::MetadataTypeDefinition*>(
            DeclaringTypeDefinition());
    if (typeDef == nullptr)
        return {};
    std::vector<const IMember*> result;
    for (const IMethod* method : typeDef->GetOverrides(handle_))
        result.push_back(method);
    return result;
}

// The C# `bool IsExplicitInterfaceImplementation` -- a dotted-name method
// (the explicit-interface-implementation name form) whose declaring type
// carries a MethodImpl row for it.
bool MetadataMethod::IsExplicitInterfaceImplementation() const
{
    // The C# `if (Name.IndexOf('.') < 0) return false;`.
    if (Name().find('.') == std::string::npos)
        return false;
    const auto* typeDef = dynamic_cast<
        const Implementation::MetadataTypeDefinition*>(
            DeclaringTypeDefinition());
    if (typeDef == nullptr)
        return false;
    return typeDef->HasOverrides(handle_);
}

// The C# `public bool IsVirtual`:
//  * a static method is virtual only when exactly the Virtual bit is set
//    (a static `abstract virtual` slots through the same mask);
//  * an instance method is virtual when (Abstract|Virtual|NewSlot|Final)
//    == Virtual|NewSlot (the C# `newslot virtual` pair -- a `virtual` method
//    compiled with `reuseSlot` semantics is NOT IsVirtual, the
//    IsOverridable property covers it).
bool MetadataMethod::IsVirtual() const
{
    if (IsStatic())
    {
        return (attr_ & (kAbstract | kVirtual)) == kVirtual;
    }
    else
    {
        constexpr std::uint32_t mask =
            kAbstract | kVirtual | kNewSlot | kFinal;
        return (attr_ & mask) == (kVirtual | kNewSlot);
    }
}

// The C# `public bool IsOverride => (attributes & (NewSlot | Virtual |
// Static)) == Virtual`.
bool MetadataMethod::IsOverride() const
{
    return (attr_ & (kNewSlot | kVirtual | kStatic)) == kVirtual;
}

// The C# `public bool IsOverridable => (attributes & (Abstract | Virtual))
// != 0 && (attributes & Final) == 0`.
bool MetadataMethod::IsOverridable() const
{
    return (attr_ & (kAbstract | kVirtual)) != 0 && (attr_ & kFinal) == 0;
}

// The C# `TypeParameterSubstitution IMember.Substitution =>
// TypeParameterSubstitution.Identity`.
const TypeParameterSubstitution* MetadataMethod::Substitution() const
{
    return &TypeParameterSubstitution::Identity();
}

// The C# `public IMethod Specialize(TypeParameterSubstitution substitution) =>
// SpecializedMethod.Create(this, substitution)` (MetadataMethod.cs lines 645-653, both the
// IMethod and the IMember explicit-interface form). The alias over `this` carries the
// no-op deleter (this module's `methodDefs_` cache owns the instance); the keep-alive
// registry owns every fresh result, so the module outliving the results is the lifetime
// contract the whole type system carries (the `ResolveForwardedType` no-op-deleter-alias
// convention).
const IMethod* MetadataMethod::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    // A null pointer is the Identity (the `IMember::Specialize` nullable-parameter
    // convention; the C# takes the substitution by value).
    TypeParameterSubstitution sub = (substitution != nullptr)
        ? *substitution
        : TypeParameterSubstitution(std::nullopt, std::nullopt);
    std::shared_ptr<IMethod> alias(
        static_cast<IMethod*>(const_cast<MetadataMethod*>(this)),
        [](IMethod*) {
            // no-op: the module's methodDefs_ cache owns this instance
        });
    std::shared_ptr<IMethod> result =
        SpecializedMethod::Create(std::move(alias), std::move(sub));
    const IMethod* raw = result.get();
    if (raw != static_cast<const IMethod*>(this)) {
        // A fresh `SpecializedMethod` (the Identity / declaring-tpc-0 arms return
        // `this` itself, needing no registry slot).
        specializedMethods_.push_back(std::move(result));
    }
    return raw;
}

// The C# `bool IMember.Equals(IMember obj, TypeVisitor typeNormalization)
// => Equals(obj)`.
bool MetadataMethod::Equals(const IMember* obj,
    const TypeVisitor* /*typeNormalization*/) const
{
    return Equals(dynamic_cast<const MetadataMethod*>(obj));
}

// The C# `public override bool Equals(object obj)`: the handle + module-file
// identity.
bool MetadataMethod::Equals(const MetadataMethod* other) const
{
    if (other == nullptr)
        return false;
    return handle_ == other->handle_
        && module_.MetadataFile() == other->module_.MetadataFile();
}

// --- IEntity ---

// The C# `public ITypeDefinition DeclaringTypeDefinition` -- the lazy
// `module.GetDefinition(def.GetDeclaringType())`.
const ITypeDefinition* MetadataMethod::DeclaringTypeDefinition() const
{
    if (declaringTypeLoaded_)
        return declaringType_;
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    declaringType_ = module_.GetDefinition(
        metadata->GetMethodDeclaringTypeToken(handle_));
    declaringTypeLoaded_ = true;
    return declaringType_;
}

// The C# `public IType DeclaringType => DeclaringTypeDefinition` -- the
// non-owning alias over the module-owned definition.
ITypePtr MetadataMethod::DeclaringType() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    if (decl == nullptr)
        return nullptr;
    return ITypePtr(const_cast<ITypeDefinition*>(decl),
        [](ITypeDefinition*) {
            // no-op: the module's entity cache owns the definition
        });
}

const IModule* MetadataMethod::ParentModule() const
{
    return &module_;
}

const ICompilation& MetadataMethod::Compilation() const
{
    return module_.Compilation();
}

// DEFERRED (convention (c)): the AttributeListBuilder machinery.
std::vector<const IAttribute*> MetadataMethod::GetAttributes() const
{
    throw std::logic_error(
        "MetadataMethod::GetAttributes: the AttributeListBuilder is not yet "
        "ported");
}

bool MetadataMethod::HasAttribute(KnownAttribute /*attribute*/) const
{
    throw std::logic_error(
        "MetadataMethod::HasAttribute: the AttributeListBuilder is not yet "
        "ported");
}

const IAttribute* MetadataMethod::GetAttribute(
    KnownAttribute /*attribute*/) const
{
    throw std::logic_error(
        "MetadataMethod::GetAttribute: the AttributeListBuilder is not yet "
        "ported");
}

// The C# `public bool IsStatic => (attributes & Static) != 0`.
bool MetadataMethod::IsStatic() const
{
    return (attr_ & kStatic) != 0;
}

// The C# `public bool IsAbstract => (attributes & Abstract) != 0`.
bool MetadataMethod::IsAbstract() const
{
    return (attr_ & kAbstract) != 0;
}

// The C# `public bool IsSealed => (attributes & (Abstract | Final |
// NewSlot | Static)) == Final`.
bool MetadataMethod::IsSealed() const
{
    return (attr_ & (kAbstract | kFinal | kNewSlot | kStatic)) == kFinal;
}

// The C# `internal Nullability NullableContext` -- the method's own
// [NullableContext] row ?? the declaring type's context (LANDED, the
// NRT-context sub-slice). The null-declaring-type fallback keeps the
// port's stub-shape guard (the C# NREs there; unreachable through real
// metadata -- the C# reads `DeclaringTypeDefinition.NullableContext` with
// no null fallback).
::ILSpy::Decompiler::TypeSystem::Nullability MetadataMethod::NullableContext()
    const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return Metadata::GetNullableContext(*module_.MetadataFile(), handle_)
        .value_or(decl != nullptr
                      ? decl->NullableContext()
                      : ::ILSpy::Decompiler::TypeSystem::Nullability::
                            Oblivious);
}

// The C# `public override int GetHashCode() => 0x5a00d671 ^
// module.MetadataFile.GetHashCode() ^ handle.GetHashCode();` -- the
// MetadataFile identity is the pointer (the MetadataField convention (j)).
int MetadataMethod::GetHashCode() const
{
    return static_cast<int>(0x5a00d671u
        ^ static_cast<std::uint32_t>(
              reinterpret_cast<std::uintptr_t>(module_.MetadataFile()))
        ^ handle_);
}

// The C# `private void DecodeSignature()` (the instance member): the
// method-signature decode over the module's TypeProvider + the GenericContext
// of the declaring type's and this method's type parameters, the shared
// static walk, the `modreq(IsExternalInit)` IsInitOnly test, and the
// BadImageFormatException catch arm.
void MetadataMethod::DecodeSignature() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    ITypePtr returnType;
    std::vector<std::shared_ptr<const IParameter>> parameters;
    ITypePtr mod;
    try
    {
        // The C# `var genericContext = new GenericContext(
        // DeclaringType.TypeParameters, this.TypeParameters);` -- the null
        // declaring type feeds the empty class list (the C# null list).
        std::vector<const ITypeParameter*> classTypeParameters;
        if (DeclaringTypeDefinition() != nullptr)
            classTypeParameters =
                DeclaringTypeDefinition()->TypeParameters();
        GenericContext genericContext(std::move(classTypeParameters),
            TypeParameters());
        auto blob = metadata->GetSignatureBlob(handle_);
        Metadata::SignatureTypeProviderDecoder<TypeProvider> decoder(
            const_cast<TypeProvider&>(module_.TypeProvider()), *metadata);
        auto signature = decoder.DecodeMethodSignature(
            blob ? blob->data() : nullptr,
            blob ? blob->size() : 0, genericContext);
        DecodedSignature decoded = DecodeSignature(
            module_, this, signature,
            &metadata->GetParameters(handle_),
            NullableContext(), module_.OptionsForEntity(this));
        returnType = std::move(decoded.ReturnType);
        parameters = std::move(decoded.Parameters);
        mod = std::move(decoded.ReturnTypeModifier);
    }
    catch (const std::invalid_argument&)
    {
        // The C# `catch (BadImageFormatException)` -- the family mapping
        // (convention (h)); every other type propagates.
        returnType = ::ILSpy::Decompiler::TypeSystem::UnknownType();
        parameters.clear();
        mod = nullptr;
    }
    catch (const std::out_of_range&)
    {
        returnType = ::ILSpy::Decompiler::TypeSystem::UnknownType();
        parameters.clear();
        mod = nullptr;
    }
    // The C# `this.isInitOnly = mod is { Modifier: { Name: "IsExternalInit",
    // Namespace: "System.Runtime.CompilerServices" } };`
    {
        auto* modified = dynamic_cast<const ModifiedType*>(mod.get());
        isInitOnly_ = modified != nullptr
            && modified->Modifier() != nullptr
            && modified->Modifier()->Name() == "IsExternalInit"
            && NamespaceOf(*modified->Modifier())
                == "System.Runtime.CompilerServices";
    }
    returnType_ = std::move(returnType);
    parameters_ = std::move(parameters);
    signatureDecoded_ = true;
}

// The shared static `DecodeSignature` (MetadataMethod.cs lines 238-313): the
// Param-row walk.
MetadataMethod::DecodedSignature MetadataMethod::DecodeSignature(
    const MetadataModule& module, const IParameterizedMember* owner,
    const Metadata::ProviderMethodSignature<ITypePtr>& signature,
    const std::vector<Metadata::ParameterInfo>* parameterHandles,
    ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext,
    ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions typeSystemOptions,
    const std::optional<std::vector<std::uint32_t>>&
        additionalReturnTypeAttributes)
{
    const Metadata::MetadataFile* metadata = module.MetadataFile();
    std::size_t i = 0;
    std::vector<std::shared_ptr<const IParameter>> parameters(
        signature.RequiredParameterCount
        + (signature.Header.CallingConvention
                == Metadata::SignatureCallingConvention::VarArgs
            ? 1
            : 0));
    ITypePtr parameterType;
    std::optional<std::vector<std::uint32_t>> returnTypeAttributes;
    if (parameterHandles != nullptr)
    {
        for (const auto& par : *parameterHandles)
        {
            if (par.SequenceNumber == 0)
            {
                // "parameter" holds return type attributes.
                // Note: for properties, the attributes normally stored on a
                // method's return type are instead typically stored as normal
                // attributes on the property. So MetadataProperty provides a
                // non-null value for additionalReturnTypeAttributes, which
                // then will be preferred over the attributes on the
                // accessor's parameters. However if an attribute only exists
                // on the accessor's parameters, we still want to process it
                // here.
                returnTypeAttributes =
                    metadata->GetCustomAttributeTokens(par.Token);
            }
            else if (i < par.SequenceNumber
                && par.SequenceNumber <= signature.RequiredParameterCount)
            {
                // "Successive rows of the Param table that are owned by the
                // same method shall be ordered by increasing Sequence value -
                // although gaps in the sequence are allowed" (ECMA): fill
                // gaps in the sequence with non-metadata parameters.
                while (i < par.SequenceNumber - 1)
                {
                    parameterType =
                        ApplyAttributeTypeVisitor::ApplyAttributesToType(
                            signature.ParameterTypes[i],
                            const_cast<ICompilation&>(module.Compilation()),
                            std::nullopt, *metadata, typeSystemOptions,
                            nullableContext);
                    parameters[i] = std::make_shared<DefaultParameter>(
                        std::move(parameterType), std::string(), owner,
                        std::vector<const IAttribute*>(),
                        signature.ParameterTypes[i]->Kind()
                                == TypeKind::ByReference
                            ? ReferenceKind::Ref
                            : ReferenceKind::None);
                    i++;
                }
                parameterType =
                    ApplyAttributeTypeVisitor::ApplyAttributesToType(
                        signature.ParameterTypes[i],
                        const_cast<ICompilation&>(module.Compilation()),
                        metadata->GetCustomAttributeTokens(par.Token),
                        *metadata, typeSystemOptions, nullableContext);
                parameters[i] = std::make_shared<MetadataParameter>(
                    module, owner, std::move(parameterType), par.Token);
                i++;
            }
        }
    }
    while (i < signature.RequiredParameterCount)
    {
        parameterType = ApplyAttributeTypeVisitor::ApplyAttributesToType(
            signature.ParameterTypes[i],
            const_cast<ICompilation&>(module.Compilation()), std::nullopt,
            *metadata, typeSystemOptions, nullableContext);
        parameters[i] = std::make_shared<DefaultParameter>(
            std::move(parameterType), std::string(), owner,
            std::vector<const IAttribute*>(),
            signature.ParameterTypes[i]->Kind() == TypeKind::ByReference
                ? ReferenceKind::Ref
                : ReferenceKind::None);
        i++;
    }
    if (signature.Header.CallingConvention
        == Metadata::SignatureCallingConvention::VarArgs)
    {
        // The vararg sentinel: the trailing __arglist parameter.
        parameters[i] = std::make_shared<DefaultParameter>(
            std::make_shared<SpecialType>(TypeKind::ArgList),
            std::string(), owner);
        i++;
    }
    // The C# `Debug.Assert(i == parameters.Length)` -- compiled out of the
    // release assembly; a corrupt signature's RequiredParameterCount beyond
    // the declared count writes past the vector and the port's operator[] is
    // the bounds-checked .at() shape (an out_of_range the instance catch arm
    // swallows into UnknownType, the documented divergence).
    ITypePtr returnType =
        ApplyAttributeTypeVisitor::ApplyAttributesToType(
            signature.ReturnType,
            const_cast<ICompilation&>(module.Compilation()),
            returnTypeAttributes, *metadata, typeSystemOptions,
            nullableContext, false, additionalReturnTypeAttributes);
    DecodedSignature result;
    result.ReturnType = std::move(returnType);
    result.Parameters = std::move(parameters);
    // The C# `return (returnType, parameters, signature.ReturnType as
    // ModifiedType)` -- the RAW (pre-ApplyAttributes) return type.
    result.ReturnTypeModifier =
        dynamic_cast<const ModifiedType*>(signature.ReturnType.get())
        ? signature.ReturnType
        : nullptr;
    return result;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
