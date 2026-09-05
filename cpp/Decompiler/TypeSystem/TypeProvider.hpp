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

// Port of ICSharpCode.Decompiler/TypeSystem/TypeProvider.cs -- the
// `sealed class TypeProvider : ICompilationProvider,
// ISignatureTypeProvider<IType, GenericContext>,
// ICustomAttributeTypeProvider<IType>` that lets metadata signature blobs decode
// onto the decompiler's type system. The MetadataModule owns one (`internal
// readonly TypeProvider TypeProvider = new TypeProvider(this)`), and every
// ResolveType / attribute-decode path drives it.
//
// KEY PORT CONVENTIONS:
//  (a) The contract: the port's `Metadata::ISignatureTypeProvider<TType,
//      TGenericContext>` (the SRM gap fill) with `TType = ITypePtr` and
//      `TGenericContext = GenericContext` (the VAR/MVAR scope, this slice's
//      other file). The `ICustomAttributeTypeProvider<IType>` half is NOT a
//      ported interface yet (the custom-attribute decoder slice); its four
//      members port as plain methods (GetSystemType /
//      GetTypeFromSerializedName / GetUnderlyingEnumType / IsSystemType) so the
//      future decoder slice calls them directly.
//  (b) The C# methods take the decoding `MetadataReader` as a parameter; the
//      port's provider contract has no reader parameter (the walker derives
//      every metadata read from the provider's own module -- the
//      DisassemblerSignatureTypeProvider convention). For the MODULE-BACKED
//      ctor that is exact: the module-backed provider always decodes its own
//      module's blobs. For the COMPILATION-ONLY ctor (the C# `TypeProvider
//      (ICompilation)`, used by MetadataExtensions over MinimalCorlib) the
//      reader-dependent arms (`GetTypeFromDefinition`'s / `GetTypeFromReference`'s
//      `handle.GetFullTypeName(reader)` fallback construction and
//      `GetTypeFromSpecification`'s blob read) throw a loud std::logic_error
//      naming the limitation -- a documented divergence (the C# reads through
//      the caller's reader, which the port's contract cannot express).
//  (c) The C# returns GC references (the compilation's FindType cache, the
//      module's entity cache, freshly-built composites); the port returns
//      `ITypePtr`. Freshly-built composites are `make_shared`-owned; every
//      pre-existing object (a FindType result, a module-owned
//      ITypeDefinition) is a NON-OWNING alias with a no-op deleter (the
//      ReflectionHelper::SnapshotDefinition convention -- the compilation /
//      module owns it for the decoded types' lifetime).
//  (d) `IsReferenceType(reader, handle, rawTypeKind)` -- the private helper
//      over the SRM `MetadataReader.ResolveSignatureTypeKind` (decompiled
//      from the pinned System.Reflection.Metadata 10, the
//      MetadataReaderExtensions.ResolveSignatureTypeKind extension): a raw
//      ELEMENT_TYPE_VALUETYPE/CLASS byte over a TypeDef handle decides
//      value-type-vs-class BY THE RAW BYTE (no IsValueType check -- the
//      metadata is trusted); over a TypeRef handle the
//      `TypeRefSignatureTreatment` projection is consulted, which is `None`
//      for every non-winmd reader (the port has no
//      ApplyWindowsRuntimeProjections machinery -- the ProjectedToClass/
//      ProjectedToValueType arms are the deferred winmd-projection slice,
//      so the port takes the raw byte, documented divergence); a TypeSpec
//      handle yields "unknown"; the raw byte 0x00 yields "unknown".
//  (e) The unknown fallbacks construct `UnknownType(fullTypeName,
//      isReferenceType)` -- the port's UnknownType carries the
//      `std::optional<bool> isReferenceType` ctor parameter for exactly this
//      call site (the D429 port note).
//  (f) `GetTypeFromSerializedName`'s C# `catch (ReflectionNameParseException)`
//      rethrow as `BadImageFormatException($"Invalid type name: \"{name}\":
//      {ex.Message}")` ports to std::invalid_argument carrying the same
//      formatted message (the BadImageFormatException mapping convention);
//      the C# null-name early return is N/A (std::string has no null state).

#pragma once

#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ICompilationProvider.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cstdint>
#include <string>
#include <vector>

// Forward declarations (the .cpp includes the full headers): the module (a
// nullable pointer member is complete with the pointee incomplete).
namespace ILSpy::Decompiler::TypeSystem { class MetadataModule; }

namespace ILSpy::Decompiler::TypeSystem {

// The decompiler-type-system signature provider (see the header comment).
class TypeProvider final
    : public ICompilationProvider,
      public Metadata::ISignatureTypeProvider<ITypePtr, GenericContext> {
public:
    // The contract's result / context types (the walker reads them off the
    // provider).
    using TType = ITypePtr;
    using TGenericContext = GenericContext;

    // The C# `public TypeProvider(MetadataModule module)`.
    explicit TypeProvider(const MetadataModule& module);

    // The C# `public TypeProvider(ICompilation compilation)` -- the
    // compilation-only provider (the MetadataExtensions-over-MinimalCorlib
    // shape); its reader-dependent arms are the convention-(b) divergence.
    explicit TypeProvider(const ICompilation& compilation);

    // --- ICompilationProvider (the C# `ICompilation Compilation`) ---
    // The return type GLOBALLY QUALIFIED: the accessor name hides the
    // `ICompilation` class for the rest of the class body (the D372 crux).
    const ::ILSpy::Decompiler::TypeSystem::ICompilation& Compilation()
        const override;

    // --- ISignatureTypeProvider<IType, GenericContext> ---
    ITypePtr GetArrayType(ITypePtr elementType,
        const Metadata::ArrayShape& shape) override;
    ITypePtr GetByReferenceType(ITypePtr elementType) override;
    ITypePtr GetFunctionPointerType(
        const Metadata::ProviderMethodSignature<ITypePtr>& signature) override;
    ITypePtr GetGenericInstantiation(ITypePtr genericType,
        std::vector<ITypePtr> typeArguments) override;
    ITypePtr GetGenericMethodParameter(
        const GenericContext& genericContext, int index) override;
    ITypePtr GetGenericTypeParameter(
        const GenericContext& genericContext, int index) override;
    ITypePtr GetModifiedType(ITypePtr modifier,
        ITypePtr unmodifiedType, bool isRequired) override;
    ITypePtr GetPinnedType(ITypePtr elementType) override;
    ITypePtr GetPointerType(ITypePtr elementType) override;
    ITypePtr GetPrimitiveType(Metadata::PrimitiveTypeCode typeCode) override;
    ITypePtr GetSZArrayType(ITypePtr elementType) override;
    ITypePtr GetTypeFromDefinition(std::uint32_t typeDefToken,
        std::uint8_t rawTypeKind) override;
    ITypePtr GetTypeFromReference(std::uint32_t typeRefToken,
        std::uint8_t rawTypeKind) override;
    ITypePtr GetTypeFromSpecification(std::uint32_t typeSpecToken,
        std::uint8_t rawTypeKind,
        const GenericContext& genericContext) override;

    // --- the ICustomAttributeTypeProvider<IType> surface (plain methods
    // until the custom-attribute decoder slice ports the interface) ---
    // The C# `IType GetSystemType() => compilation.FindType(KnownTypeCode.Type)`.
    ITypePtr GetSystemType() const;
    // The C# `IType GetTypeFromSerializedName(string name)` -- the
    // reflection-name parser over a SimpleTypeResolveContext (the module
    // when present, else the compilation). The convention-(f) exception
    // mapping.
    ITypePtr GetTypeFromSerializedName(const std::string& name) const;
    // The C# `PrimitiveTypeCode GetUnderlyingEnumType(IType type)` -- the
    // enum's underlying primitive code for an attribute blob's boxed enum
    // value; throws EnumUnderlyingTypeResolveException when the underlying
    // type has no resolvable definition.
    Metadata::PrimitiveTypeCode GetUnderlyingEnumType(const IType& type) const;
    // The C# `bool IsSystemType(IType type) => type.IsKnownType(KnownTypeCode.Type)`.
    bool IsSystemType(const IType& type) const;

private:
    // The C# private `bool? IsReferenceType(reader, handle, rawTypeKind)`
    // (convention (d)); the token is the raw 0x01/0x02 handle.
    std::optional<bool> IsReferenceType(std::uint32_t entityToken,
        std::uint8_t rawTypeKind) const;

    // The C# fields (`readonly MetadataModule module; readonly ICompilation
    // compilation;`). The compilation member is GLOBALLY QUALIFIED (the
    // Compilation() accessor hides the class name -- the D372 crux).
    const MetadataModule* module_ = nullptr;
    const ::ILSpy::Decompiler::TypeSystem::ICompilation* compilation_ = nullptr;
};

} // namespace ILSpy::Decompiler::TypeSystem
