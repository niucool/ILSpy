// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// TypeProvider.cs -- see the header's port conventions.

#include "Decompiler/TypeSystem/TypeProvider.hpp"

#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"  // ToPrimitiveTypeCode
#include "Decompiler/Metadata/SRMExtensions.hpp"  // GetFullTypeNameFrom*
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/ReflectionNameParseException.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // GetTypeDefinition(FullTypeName), IsKnownType
#include "Decompiler/TypeSystem/TypeUtils.hpp"  // GetEnumUnderlyingType

#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

namespace {

// The non-owning snapshot of a compilation-/module-owned object (the C#
// `return type;` over the GC-owned reference): convention (c) -- the no-op
// deleter borrows the pointee, the owner keeps it alive for the decoded
// types' lifetime.
ITypePtr SnapshotType(const IType* type) {
    return ITypePtr(const_cast<IType*>(type), [](IType*) { /* no-op */ });
}

} // namespace

TypeProvider::TypeProvider(const MetadataModule& module)
    : module_(&module), compilation_(&module.Compilation()) {}

TypeProvider::TypeProvider(const ICompilation& compilation)
    : module_(nullptr), compilation_(&compilation) {}

const ::ILSpy::Decompiler::TypeSystem::ICompilation&
TypeProvider::Compilation() const {
    return *compilation_;
}

// The C# `GetArrayType(IType elementType, ArrayShape shape) => new
// ArrayType(compilation, elementType, shape.Rank)`. The port's ArrayType drops
// the compilation parameter (the minimal-port composing types carry no
// compilation field; the C# uses it only for its KnownTypeCache interning).
ITypePtr TypeProvider::GetArrayType(ITypePtr elementType,
    const Metadata::ArrayShape& shape) {
    return std::make_shared<ArrayType>(std::move(elementType),
        static_cast<int>(shape.Rank));
}

ITypePtr TypeProvider::GetByReferenceType(ITypePtr elementType) {
    return std::make_shared<ByReferenceType>(std::move(elementType));
}

// The C# `GetFunctionPointerType(MethodSignature<IType> signature)`: pointers
// to member functions are not supported -- an instance header falls back to
// IntPtr; everything else goes through FromSignature.
ITypePtr TypeProvider::GetFunctionPointerType(
    const Metadata::ProviderMethodSignature<ITypePtr>& signature) {
    if (signature.Header.IsInstance()) {
        return SnapshotType(&compilation_->FindType(KnownTypeCode::IntPtr));
    }
    return FunctionPointerType::FromSignature(signature);
}

// The C# `GetGenericInstantiation`: the arity check -- a generic type with no
// type parameters, or an argument count that does not match the generic's
// arity, keeps the generic type as-is (the C# comment: the generic type can be
// from another assembly that does not have the typical ``1` suffix and is not
// loaded); otherwise the parameterization wraps.
ITypePtr TypeProvider::GetGenericInstantiation(ITypePtr genericType,
    std::vector<ITypePtr> typeArguments) {
    int tpc = genericType ? genericType->TypeParameterCount() : 0;
    if (tpc == 0 ||
        tpc != static_cast<int>(typeArguments.size())) {
        return genericType;
    }
    return std::make_shared<ParameterizedType>(std::move(genericType),
        std::move(typeArguments));
}

ITypePtr TypeProvider::GetGenericMethodParameter(
    const GenericContext& genericContext, int index) {
    return genericContext.GetMethodTypeParameter(index);
}

ITypePtr TypeProvider::GetGenericTypeParameter(
    const GenericContext& genericContext, int index) {
    return genericContext.GetClassTypeParameter(index);
}

ITypePtr TypeProvider::GetModifiedType(ITypePtr modifier,
    ITypePtr unmodifiedType, bool isRequired) {
    return std::make_shared<ModifiedType>(std::move(modifier),
        std::move(unmodifiedType), isRequired);
}

ITypePtr TypeProvider::GetPinnedType(ITypePtr elementType) {
    return std::make_shared<PinnedType>(std::move(elementType));
}

ITypePtr TypeProvider::GetPointerType(ITypePtr elementType) {
    return std::make_shared<PointerType>(std::move(elementType));
}

ITypePtr TypeProvider::GetPrimitiveType(
    Metadata::PrimitiveTypeCode typeCode) {
    return SnapshotType(&compilation_->FindType(
        Metadata::ToKnownTypeCode(typeCode)));
}

// The C# `GetSZArrayType(IType elementType) => new ArrayType(compilation,
// elementType)`.
ITypePtr TypeProvider::GetSZArrayType(ITypePtr elementType) {
    return std::make_shared<ArrayType>(std::move(elementType));
}

// The private `bool? IsReferenceType(reader, handle, rawTypeKind)` (the
// header's convention (d)): the decompiled SRM
// MetadataReaderExtensions.ResolveSignatureTypeKind semantics. The raw byte
// decides for a TypeDef handle and for a TypeRef handle under the None
// treatment; a TypeSpec handle and the 0x00 byte yield "unknown"; any other
// handle kind is the C# ArgumentOutOfRangeException (mapped to
// std::invalid_argument, the dead-defensive arm -- the two callers always
// pass TypeDef/TypeRef tokens).
std::optional<bool> TypeProvider::IsReferenceType(std::uint32_t entityToken,
    std::uint8_t rawTypeKind) const {
    if (rawTypeKind == 0x11) return std::optional<bool>(false);
    if (rawTypeKind == 0x12) return std::optional<bool>(true);
    if (rawTypeKind == 0x00) return std::nullopt;
    const std::uint8_t kind = static_cast<std::uint8_t>(entityToken >> 24);
    if (kind == 0x1b) return std::nullopt;  // TypeSpec: unknown
    throw std::invalid_argument("rawTypeKind");
}

// The C# `GetTypeFromDefinition`: the module's entity cache answers in-range
// rows; the UnknownType fallback (full name + reference-ness) is the
// compilation-only arm (convention (b): the module-backed provider always
// resolves its own module's rows).
ITypePtr TypeProvider::GetTypeFromDefinition(std::uint32_t typeDefToken,
    std::uint8_t rawTypeKind) {
    const ITypeDefinition* td =
        module_ != nullptr ? module_->GetDefinition(typeDefToken) : nullptr;
    if (td != nullptr)
        return SnapshotType(td);
    if (module_ == nullptr) {
        throw std::logic_error(
            "TypeProvider: the compilation-only provider cannot construct "
            "the UnknownType fallback (no module to read the type name from; "
            "the C# reads through the caller's MetadataReader)");
    }
    return std::make_shared<class UnknownType>(
        Metadata::GetFullTypeNameFromDefinition(*module_->MetadataFile(),
            typeDefToken),
        IsReferenceType(typeDefToken, rawTypeKind));
}

// The C# `GetTypeFromReference`: the TypeRef's declaring module (the
// resolution-scope walk) resolves the name; a null declaring module falls to
// the compilation-modules walk; a complete miss constructs the UnknownType
// fallback with the reference-ness from the raw byte. The full name is read
// from the provider's module (the C# reads it from the caller's reader) -- the
// whole method is reader-dependent, so the compilation-only provider throws
// (convention (b)).
ITypePtr TypeProvider::GetTypeFromReference(std::uint32_t typeRefToken,
    std::uint8_t rawTypeKind) {
    if (module_ == nullptr) {
        throw std::logic_error(
            "TypeProvider: the compilation-only provider cannot resolve a "
            "TypeReference (no module to read the type name from; the C# "
            "reads through the caller's MetadataReader)");
    }
    const FullTypeName fullTypeName =
        Metadata::GetFullTypeNameFromReference(*module_->MetadataFile(),
            typeRefToken);
    const IModule* resolvedModule =
        module_->GetDeclaringModule(typeRefToken);
    if (resolvedModule != nullptr) {
        const ITypeDefinition* type =
            GetTypeDefinition(*resolvedModule, fullTypeName);
        if (type != nullptr)
            return SnapshotType(type);
    } else {
        for (const IModule* module : compilation_->Modules()) {
            const ITypeDefinition* type =
                GetTypeDefinition(*module, fullTypeName);
            if (type != nullptr)
                return SnapshotType(type);
        }
    }
    return std::make_shared<class UnknownType>(fullTypeName,
        IsReferenceType(typeRefToken, rawTypeKind));
}

ITypePtr TypeProvider::GetTypeFromSpecification(std::uint32_t typeSpecToken,
    std::uint8_t rawTypeKind,
    const GenericContext& genericContext) {
    // The C# `reader.GetTypeSpecification(handle).DecodeSignature(this,
    // genericContext)` -- the TypeSpec row's signature blob decoded through
    // this provider (the rawTypeKind is consumed by the row's inner
    // CLASS/VALUETYPE marker, so it is dropped faithfully).
    (void)rawTypeKind;
    if (module_ == nullptr) {
        throw std::logic_error(
            "TypeProvider: the compilation-only provider cannot decode a "
            "TypeSpecification (no module to read the signature blob from; "
            "the C# reads through the caller's MetadataReader)");
    }
    auto blob = module_->MetadataFile()->GetTypeSpecSignatureBlob(typeSpecToken);
    if (!blob)
        throw std::out_of_range(
            "TypeProvider: invalid TypeSpecification token");
    Metadata::SignatureTypeProviderDecoder<TypeProvider> decoder(
        *this, *module_->MetadataFile());
    return decoder.DecodeType(blob->data(), blob->size(), genericContext);
}

// --- the ICustomAttributeTypeProvider surface ---

ITypePtr TypeProvider::GetSystemType() const {
    return SnapshotType(&compilation_->FindType(KnownTypeCode::Type));
}

ITypePtr TypeProvider::GetTypeFromSerializedName(const std::string& name) const {
    try {
        if (module_ != nullptr) {
            SimpleTypeResolveContext context(*module_);
            return ParseReflectionName(name, context);
        }
        SimpleTypeResolveContext context(*compilation_);
        return ParseReflectionName(name, context);
    } catch (const ReflectionNameParseException& ex) {
        // The C# `throw new BadImageFormatException($"Invalid type name:
        // \"{name}\": {ex.Message}", ex)`.
        throw std::invalid_argument(
            "Invalid type name: \"" + name + "\": " + ex.what());
    }
}

Metadata::PrimitiveTypeCode TypeProvider::GetUnderlyingEnumType(
    const IType& type) const {
    const IType* underlying = GetEnumUnderlyingType(&type);
    const ITypeDefinition* def =
        underlying != nullptr ? underlying->GetDefinition() : nullptr;
    if (def == nullptr)
        throw Metadata::EnumUnderlyingTypeResolveException();
    return Metadata::ToPrimitiveTypeCode(def->KnownTypeCode());
}

bool TypeProvider::IsSystemType(const IType& type) const {
    return IsKnownType(type, KnownTypeCode::Type);
}

} // namespace ILSpy::Decompiler::TypeSystem
