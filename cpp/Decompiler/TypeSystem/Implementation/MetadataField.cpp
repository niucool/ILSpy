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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// MetadataField.cs -- see the header's port notes.

#include "Decompiler/TypeSystem/Implementation/MetadataField.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/ApplyAttributeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/Implementation/AttributeListBuilder.hpp"
#include "Decompiler/TypeSystem/Implementation/DecimalConstantHelper.hpp"
#include "Decompiler/TypeSystem/Implementation/SpecializedField.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

namespace {

// The non-owning snapshot of a module-owned type (the C# `return this;` over
// the GC-owned reference): the module owns its entities, so the returned
// `ITypePtr` aliases with a no-op deleter -- the MetadataTypeDefinition.cpp
// SnapshotType precedent (copied next to its second consumer).
ITypePtr SnapshotType(const IType* type)
{
    return ITypePtr(const_cast<IType*>(type), [](IType*) {
        // no-op: the module's entity cache owns the type
    });
}

// The SRM `BlobReader.ReadConstant(ConstantTypeCode)` over the constant's
// value blob (the .NET 10 System.Reflection.Metadata the repo pins) -- the
// ReflectionDisassembler.cpp ReadConstantValue precedent, copied next to its
// second consumer with the ONE divergence the MetadataField catch arms
// require: the 0x12 (NullReference) nonzero-payload arm throws the
// BadImageFormatException FAMILY (`std::invalid_argument` carrying the
// SR.InvalidConstantValue text "Invalid constant value."), not the
// disassembler's deliberately-distinct `std::runtime_error` (that copy's
// WriteConstant catch never sees it; THIS copy's GetConstantValue outer catch
// must).
std::any ReadConstantValue(std::uint8_t typeCode, const std::uint8_t* base,
    std::size_t size)
{
    auto need = [&](std::size_t n) {
        if (n > size) {
            throw std::out_of_range("constant blob read past end");
        }
    };
    switch (typeCode) {
        case 0x02: {  // Boolean
            need(1);
            return std::any{static_cast<bool>(base[0] != 0)};
        }
        case 0x03: {  // Char
            need(2);
            return std::any{static_cast<char16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x04: {  // SByte
            need(1);
            return std::any{static_cast<std::int8_t>(base[0])};
        }
        case 0x05: {  // Byte
            need(1);
            return std::any{static_cast<std::uint8_t>(base[0])};
        }
        case 0x06: {  // Int16
            need(2);
            return std::any{static_cast<std::int16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x07: {  // UInt16
            need(2);
            return std::any{static_cast<std::uint16_t>(
                static_cast<std::uint16_t>(base[0])
                | (static_cast<std::uint16_t>(base[1]) << 8))};
        }
        case 0x08: {  // Int32
            need(4);
            std::uint32_t v = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            return std::any{static_cast<std::int32_t>(v)};
        }
        case 0x09: {  // UInt32
            need(4);
            std::uint32_t v = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            return std::any{v};
        }
        case 0x0A: {  // Int64
            need(8);
            std::uint64_t v = 0;
            for (int i = 7; i >= 0; --i)
                v = (v << 8) | static_cast<std::uint64_t>(base[i]);
            return std::any{static_cast<std::int64_t>(v)};
        }
        case 0x0B: {  // UInt64
            need(8);
            std::uint64_t v = 0;
            for (int i = 7; i >= 0; --i)
                v = (v << 8) | static_cast<std::uint64_t>(base[i]);
            return std::any{v};
        }
        case 0x0C: {  // Single
            need(4);
            std::uint32_t bits = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            float f;
            std::memcpy(&f, &bits, sizeof(f));
            return std::any{f};
        }
        case 0x0D: {  // Double
            need(8);
            std::uint64_t bits = 0;
            for (int i = 7; i >= 0; --i)
                bits = (bits << 8) | static_cast<std::uint64_t>(base[i]);
            double d;
            std::memcpy(&d, &bits, sizeof(d));
            return std::any{d};
        }
        case 0x0E: {  // String: the remaining blob as UTF-16.
            std::size_t chars = size / 2;
            std::u16string utf16(chars, u'\0');
            for (std::size_t i = 0; i < chars; ++i) {
                utf16[i] = static_cast<char16_t>(
                    static_cast<std::uint16_t>(base[2 * i])
                    | (static_cast<std::uint16_t>(base[2 * i + 1]) << 8));
            }
            return std::any{Util::Utf16ToUtf8(utf16)};
        }
        case 0x12: {  // NullReference (the ELEMENT_TYPE_CLASS slot)
            need(4);
            std::uint32_t v = static_cast<std::uint32_t>(base[0])
                | (static_cast<std::uint32_t>(base[1]) << 8)
                | (static_cast<std::uint32_t>(base[2]) << 16)
                | (static_cast<std::uint32_t>(base[3]) << 24);
            if (v != 0) {
                // The C# `throw new BadImageFormatException(SR.InvalidConstantValue)`
                // (the decompiled SRM ReadConstant) -- caught by the outer catch.
                throw std::invalid_argument("Invalid constant value.");
            }
            return std::any{};  // the C# null
        }
        default:
            // The C# `throw new ArgumentOutOfRangeException("typeCode")` --
            // wrapped by GetConstantValue's inner catch.
            throw std::out_of_range("invalid constant type code");
    }
}

// The C# `mod.Modifier.Namespace` -- the port's `IType` carries no
// `Namespace` member (the documented minimal-IType divergence), so the read
// routes through the entity/parameterized/unknown dispatch -- the ILAmbience /
// XamlContext NamespaceOf convention (copied next to its second consumer).
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

// The ctor (the header convention (a)/(b)): the eager Attributes read and the
// decimal-constant state seed.
MetadataField::MetadataField(const MetadataModule& module,
    std::uint32_t fieldToken)
    : module_(module), handle_(fieldToken)
{
    attr_ = module.MetadataFile()->GetFieldAttributes(handle_);
    // The C# `if ((attributes & (FieldAttributes.Static | FieldAttributes
    // .InitOnly)) != (FieldAttributes.Static | FieldAttributes.InitOnly))
    // decimalConstantState = ThreeState.False;` -- a decimal constant is
    // `static readonly`, so any other flag combination can never be one.
    constexpr std::uint32_t kStaticInitOnly = 0x0010u | 0x0020u;
    if ((attr_ & kStaticInitOnly) != kStaticInitOnly)
        decimalConstantState_ = 1;  // ThreeState.False
    else
        decimalConstantState_ = 0;  // ThreeState.Unknown
}

std::uint32_t MetadataField::MetadataToken() const
{
    return handle_;
}

// The C# `public override string ToString()`.
std::string MetadataField::ToString() const
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%08X", handle_);
    // The C# interpolation renders the null DeclaringType as the empty
    // string (convention (h)).
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return std::string(buffer) + " "
        + (decl != nullptr ? decl->ReflectionName() : std::string()) + "."
        + Name();
}

// --- ISymbol / INamedElement ---

::ILSpy::Decompiler::TypeSystem::SymbolKind
MetadataField::SymbolKind() const
{
    return ::ILSpy::Decompiler::TypeSystem::SymbolKind::Field;
}

std::string MetadataField::Name() const
{
    if (name_)
        return *name_;
    name_ = module_.MetadataFile()->GetFieldName(handle_);
    return *name_;
}

::ILSpy::Decompiler::TypeSystem::Accessibility
MetadataField::Accessibility() const
{
    // The C# `switch (attributes & FieldAttributes.FieldAccessMask)` (the
    // ECMA II.23.1.5 visibility bits; the default arm is Private, covering
    // PrivateScope and any out-of-mask value).
    switch (attr_ & 0x0007u) {
        case 0x0006u:  // Public
            return Accessibility::Public;
        case 0x0002u:  // FamANDAssem
            return Accessibility::ProtectedAndInternal;
        case 0x0003u:  // Assembly
            return Accessibility::Internal;
        case 0x0004u:  // Family
            return Accessibility::Protected;
        case 0x0005u:  // FamORAssem
            return Accessibility::ProtectedOrInternal;
        default:
            return Accessibility::Private;
    }
}

std::string MetadataField::FullName() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return (decl != nullptr ? decl->FullName() : std::string()) + "." + Name();
}

std::string MetadataField::ReflectionName() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return (decl != nullptr ? decl->ReflectionName() : std::string()) + "."
        + Name();
}

std::string MetadataField::Namespace() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    return decl != nullptr ? decl->Namespace() : std::string();
}

// --- IField ---

bool MetadataField::IsReadOnly() const
{
    return (attr_ & 0x0020u) != 0;  // InitOnly
}

bool MetadataField::IsStatic() const
{
    return (attr_ & 0x0010u) != 0;  // Static
}

bool MetadataField::ReturnTypeIsRefReadOnly() const
{
    // The C# `def.GetCustomAttributes().HasKnownAttribute(module.metadata,
    // KnownAttribute.IsReadOnly)`.
    return Metadata::HasKnownAttribute(*module_.MetadataFile(), handle_,
        ::ILSpy::Decompiler::TypeSystem::KnownAttribute::IsReadOnly);
}

bool MetadataField::IsVolatile() const
{
    // The C# `if (LazyInit.VolatileRead(ref this.type) == null)
    // DecodeTypeAndVolatileFlag(); return this.isVolatile;`.
    if (type_ == nullptr)
        DecodeTypeAndVolatileFlag();
    return isVolatile_;
}

// --- IVariable ---

const IType& MetadataField::Type() const
{
    if (type_ == nullptr)
        return DecodeTypeAndVolatileFlag();
    return *type_;
}

bool MetadataField::IsConst() const
{
    // The C# `(attributes & FieldAttributes.Literal) != 0 || (IsDecimalConstant
    // && DecimalConstantHelper.AllowsDecimalConstants(module))`.
    return (attr_ & 0x0040u) != 0  // Literal
        || (IsDecimalConstant() && AllowsDecimalConstants(module_));
}

std::any MetadataField::GetConstantValue(bool throwOnInvalidMetadata) const
{
    // The C# `object val = LazyInit.VolatileRead(ref this.constantValue);
    // if (val != null) return val;` -- a null value never caches, so an empty
    // `std::any` means both "not loaded" and "loaded null" (the recompute
    // per read is the faithful LazyInit.GetOrSet-over-null behavior).
    if (constantValue_.has_value())
        return constantValue_;
    std::any val;
    try
    {
        const Metadata::MetadataFile* metadata = module_.MetadataFile();
        if (IsDecimalConstant() && AllowsDecimalConstants(module_))
        {
            // The C# decimal arm -- INSIDE the try (the decode errors are
            // BadImageFormatExceptions the outer catch swallows; the
            // decimal-ctor ArgumentOutOfRange deliberately is NOT one and
            // escapes -- the DecimalConstantHelper convention (c)).
            val = Implementation::GetDecimalConstantValue(module_, handle_);
        }
        else
        {
            auto constant = metadata->GetConstant(handle_);
            // The C# `if (constantHandle.IsNil) return null;` -- the null
            // return bypasses the LazyInit caching entirely.
            if (!constant)
                return {};
            try
            {
                val = ReadConstantValue(constant->TypeCode,
                    constant->Value.data(), constant->Value.size());
            }
            catch (const std::out_of_range&)
            {
                // The C# `catch (ArgumentOutOfRangeException) { throw new
                // BadImageFormatException($"Constant with invalid typecode:
                // {constant.TypeCode}"); }` -- the decimal-ctor
                // ArgumentOutOfRange is a DISTINCT derived type thrown
                // outside this inner try, so the base-class catch here never
                // sees it.
                throw std::invalid_argument(
                    "Constant with invalid typecode: "
                    + std::to_string(static_cast<int>(constant->TypeCode)));
            }
        }
        // The C# `LazyInit.GetOrSet(ref this.constantValue, val)` -- only a
        // non-null value sticks.
        if (val.has_value())
            constantValue_ = val;
        return val;
    }
    catch (const ArgumentOutOfRangeException&)
    {
        // The C# ArgumentOutOfRangeException is NOT a BadImageFormatException:
        // it escapes GetConstantValue even with throwOnInvalidMetadata == false
        // (gold-pinned over the crafted MfSynth scale-29 field).
        throw;
    }
    catch (const std::invalid_argument&)
    {
        if (!throwOnInvalidMetadata)
            return {};
        throw;
    }
    catch (const std::out_of_range&)
    {
        if (!throwOnInvalidMetadata)
            return {};
        throw;
    }
}

// --- IEntity ---

const ITypeDefinition* MetadataField::DeclaringTypeDefinition() const
{
    if (declaringTypeLoaded_)
        return declaringType_;
    declaringTypeLoaded_ = true;
    // The C# `module.GetDefinition(def.GetDeclaringType())` -- the nil row
    // reads as null.
    declaringType_ = module_.GetDefinition(
        module_.MetadataFile()->GetFieldDeclaringTypeToken(handle_));
    return declaringType_;
}

ITypePtr MetadataField::DeclaringType() const
{
    const ITypeDefinition* decl = DeclaringTypeDefinition();
    if (decl == nullptr)
        return ITypePtr();
    return SnapshotType(decl);
}

const IModule* MetadataField::ParentModule() const
{
    return &module_;
}

const ICompilation& MetadataField::Compilation() const
{
    return module_.Compilation();
}

// The attribute members (the AttributeListBuilder slice): the C# bodies
// over the builder (the header convention (c) note is retired).
std::vector<const IAttribute*> MetadataField::GetAttributes() const
{
    // The C# rebuilds the list per call; the port caches it (the divergence
    // documented at the cache members).
    if (!attributeListLoaded_)
    {
        Implementation::AttributeListBuilder b(module_);
        const Metadata::MetadataFile* metadata = module_.MetadataFile();

        // The raw II.23.1.5 FieldAttributes bits (the System.Reflection
        // FieldAttributes enum; the raw-flags convention).
        constexpr std::uint32_t kNotSerialized = 0x0080;
        constexpr std::uint32_t kSpecialName = 0x0200;
        constexpr std::uint32_t kRTSpecialName = 0x0400;

        // FieldOffsetAttribute
        int offset = metadata->GetFieldOffset(handle_);
        if (offset != -1)
        {
            b.Add(KnownAttribute::FieldOffset, KnownTypeCode::Int32,
                  std::any(offset));
        }

        // NonSerializedAttribute
        if ((attr_ & kNotSerialized) != 0)
            b.Add(KnownAttribute::NonSerialized);

        // SpecialName
        if ((attr_ & (kSpecialName | kRTSpecialName)) == kSpecialName)
        {
            b.Add(KnownAttribute::SpecialName);
        }

        b.AddMarshalInfo(metadata->GetFieldMarshallingDescriptor(handle_));
        b.Add(handle_, SymbolKind::Field);

        attributeList_ = b.Build();
        attributeListLoaded_ = true;
    }
    std::vector<const IAttribute*> result;
    result.reserve(attributeList_.size());
    for (const auto& attr : attributeList_)
        result.push_back(attr.get());
    return result;
}

bool MetadataField::HasAttribute(KnownAttribute attribute) const
{
    if (!IsCustomAttribute(attribute))
    {
        for (const IAttribute* attr : GetAttributes())
        {
            if (IsKnownType(attr->AttributeType(), attribute))
                return true;
        }
        return false;
    }
    Implementation::AttributeListBuilder b(module_);
    return b.HasAttribute(*module_.MetadataFile(), handle_, attribute,
                          SymbolKind::Field);
}

const IAttribute* MetadataField::GetAttribute(KnownAttribute attribute) const
{
    if (!IsCustomAttribute(attribute))
    {
        for (const IAttribute* attr : GetAttributes())
        {
            if (IsKnownType(attr->AttributeType(), attribute))
                return attr;
        }
        return nullptr;
    }
    Implementation::AttributeListBuilder b(module_);
    std::shared_ptr<IAttribute> found = b.GetAttribute(
        *module_.MetadataFile(), handle_, attribute, SymbolKind::Field);
    if (!found)
        return nullptr;
    // A fresh instance per call (the C# GC root; the keep-alive registry).
    foundAttributes_.push_back(std::move(found));
    return foundAttributes_.back().get();
}

bool MetadataField::IsAbstract() const
{
    return false;
}

bool MetadataField::IsSealed() const
{
    return false;
}

// --- IMember ---

const IMember* MetadataField::MemberDefinition() const
{
    return this;
}

const IType& MetadataField::ReturnType() const
{
    // The C# `IType IMember.ReturnType => Type` -- the same cached object.
    return Type();
}

std::vector<const IMember*>
MetadataField::ExplicitlyImplementedInterfaceMembers() const
{
    // The C# `EmptyList<IMember>.Instance` -- fields cannot implement
    // interfaces.
    return {};
}

bool MetadataField::IsExplicitInterfaceImplementation() const
{
    return false;
}

bool MetadataField::IsVirtual() const
{
    return false;
}

bool MetadataField::IsOverride() const
{
    return false;
}

bool MetadataField::IsOverridable() const
{
    return false;
}

const TypeParameterSubstitution* MetadataField::Substitution() const
{
    // The C# `TypeParameterSubstitution.Identity` (fields are never
    // specialized).
    return &TypeParameterSubstitution::Identity();
}

// The C# `public IMember Specialize(TypeParameterSubstitution substitution) =>
// SpecializedField.Create(this, substitution)` (MetadataField.cs lines 317-319). The alias
// over `this` carries the no-op deleter (this module's `fieldDefs_` cache owns the
// instance); the keep-alive registry owns every fresh result.
const IMember* MetadataField::Specialize(
    const TypeParameterSubstitution* substitution) const
{
    // A null pointer is the Identity (the `IMember::Specialize` convention).
    TypeParameterSubstitution sub = (substitution != nullptr)
        ? *substitution
        : TypeParameterSubstitution(std::nullopt, std::nullopt);
    std::shared_ptr<IField> alias(
        static_cast<IField*>(const_cast<MetadataField*>(this)),
        [](IField*) {
            // no-op: the module's fieldDefs_ cache owns this instance
        });
    std::shared_ptr<IField> result =
        SpecializedField::Create(std::move(alias), std::move(sub));
    const IField* raw = result.get();
    if (raw != static_cast<const IField*>(this)) {
        // A fresh `SpecializedField` (the Identity / declaring-tpc-0 arms return
        // `this` itself, needing no registry slot).
        specializedFields_.push_back(std::move(result));
    }
    // The returned `IMember*` view is the SpecializedField's own `IField`
    // subobject (sub B); the same-instance arms return the FakeMember-free
    // `MetadataField` view (its direct `IField` base). Both views dispatch to
    // the same final overriders, and fresh-vs-same-instance pointer
    // comparisons against either view of `this` stay correct.
    return static_cast<const IMember*>(raw);
}

bool MetadataField::Equals(const IMember* obj,
    const TypeVisitor* typeNormalization) const
{
    (void)typeNormalization;
    // The C# `bool IMember.Equals(IMember obj, TypeVisitor typeNormalization)
    // => Equals(obj)` -- the handle + module-file identity.
    auto* other = dynamic_cast<const MetadataField*>(obj);
    if (other == nullptr)
        return false;
    return handle_ == other->handle_
        && module_.MetadataFile() == other->module_.MetadataFile();
}

// The C# `public override int GetHashCode() => 0x11dda32b ^
// module.MetadataFile.GetHashCode() ^ handle.GetHashCode();` -- the
// MetadataFile identity is the pointer (the MetadataTypeDefinition::GetHashCode
// convention (i)).
int MetadataField::GetHashCode() const
{
    return static_cast<int>(0x11dda32bu
        ^ static_cast<std::uint32_t>(
              reinterpret_cast<std::uintptr_t>(module_.MetadataFile()))
        ^ handle_);
}

// --- the private members ---

// The C# `private bool IsDecimalConstant` (the ThreeState byte caching the
// HasKnownAttribute classification).
bool MetadataField::IsDecimalConstant() const
{
    if (decimalConstantState_ == 0)  // ThreeState.Unknown
    {
        decimalConstantState_ = Implementation::IsDecimalConstant(
            module_, handle_) ? 2 : 1;
    }
    return decimalConstantState_ == 2;
}

// The C# `private IType DecodeTypeAndVolatileFlag()` (the header conventions
// (b)/(e)/(f)/(g)).
const IType& MetadataField::DecodeTypeAndVolatileFlag() const
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    ITypePtr ty;
    try
    {
        auto blob = metadata->GetSignatureBlob(handle_);
        // The C# `fieldDef.DecodeSignature(module.TypeProvider, new
        // GenericContext(DeclaringType?.TypeParameters))` -- the walker's
        // DecodeType over the BARE type (past the 0x06 FIELD header byte;
        // the TypeProvider_Test field-decode precedent). The null declaring
        // type feeds the empty context (the C# null list).
        std::vector<const ITypeParameter*> typeParameters;
        if (DeclaringType() != nullptr)
            typeParameters = DeclaringType()->TypeParameters();
        GenericContext context(std::move(typeParameters));
        Metadata::SignatureTypeProviderDecoder<TypeProvider> decoder(
            const_cast<TypeProvider&>(module_.TypeProvider()), *metadata);
        ty = decoder.DecodeType(
            blob ? blob->data() + 1 : nullptr,
            blob ? blob->size() - 1 : 0, context);
        // The C# `if (ty is ModifiedType mod && mod.Modifier.Name ==
        // "IsVolatile" && mod.Modifier.Namespace ==
        // "System.Runtime.CompilerServices") Volatile.Write(ref
        // this.isVolatile, true);`.
        auto* mod = dynamic_cast<const ModifiedType*>(ty.get());
        if (mod != nullptr && mod->Modifier() != nullptr
            && mod->Modifier()->Name() == "IsVolatile"
            && NamespaceOf(*mod->Modifier())
                == "System.Runtime.CompilerServices")
        {
            isVolatile_ = true;
        }
        // The C# `ty = ApplyAttributeTypeVisitor.ApplyAttributesToType(ty,
        // Compilation, fieldDef.GetCustomAttributes(), metadata,
        // module.OptionsForEntity(this), DeclaringTypeDefinition?
        // .NullableContext ?? Nullability.Oblivious)` -- the field's OWN
        // attribute rows; the OptionsForEntity / NullableContext passes are
        // LANDED (the NRT-context sub-slice)
        // (convention (e)); the OptionsForEntity(this) + the declaring
        // type's NullableContext() are LANDED (the NRT-context sub-slice).
        // The const_cast carries the C#'s mutable compilation reference
        // through the port's const-module convention (the ResolveType
        // precedent).
        const ITypeDefinition* decl = DeclaringTypeDefinition();
        ty = ApplyAttributeTypeVisitor::ApplyAttributesToType(
            std::move(ty), const_cast<ICompilation&>(Compilation()),
            std::optional<std::vector<std::uint32_t>>(
                metadata->GetCustomAttributeTokens(handle_)),
            *metadata, module_.OptionsForEntity(this),
            decl != nullptr
                ? decl->NullableContext()
                : ::ILSpy::Decompiler::TypeSystem::Nullability::Oblivious);
    }
    catch (const std::invalid_argument&)
    {
        // The C# `catch (BadImageFormatException)` -- the family mapping
        // (convention (g)); every other type propagates.
        ty = UnknownType();
    }
    catch (const std::out_of_range&)
    {
        ty = UnknownType();
    }
    // The C# `LazyInit.GetOrSet(ref this.type, ty)`.
    type_ = std::move(ty);
    return *type_;
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
