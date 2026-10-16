// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
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

// Port of ICSharpCode.Decompiler/TypeSystem/ApplyAttributeTypeVisitor.cs -- the
// sealed TypeVisitor that introduces 'dynamic' and native-integer and tuple
// types (and C# 8 nullability annotations) based on attribute values.
//
// The two public ApplyAttributesToType entries drive the whole slice:
//  - the metadata entry (the C# 8-parameter overload): decodes the
//    [Dynamic]/[NativeInteger]/[TupleElementNames]/[Nullable] attribute rows
//    of the entity's CustomAttributeHandleCollection through
//    minimalCorlibTypeProvider's CustomAttributeDecoder, then walks the input
//    type through the visitor substituting the recorded shapes. The future
//    MetadataModule::ResolveType / MetadataField / MetadataMethod slices are
//    the consumers (the C# call sites: MetadataModule.cs lines 398/409/416,
//    MetadataField.cs line 236, MetadataMethod.cs lines 262-290).
//  - the PDB entry (the C# 4-parameter overload): the same visitor seeded
//    from a PdbExtraTypeInfo's pre-decoded DynamicFlags/TupleElementNames (the
//    ILReader's debug-info path, ILReader.cs line 329).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `SRM.CustomAttributeHandleCollection?` ports to
//      `const std::optional<std::vector<std::uint32_t>>&` -- the collection's
//      row TOKENS in table order (the MetadataFile::GetCustomAttributeTokens
//      composition; a nil/disengaged optional is the C# null collection). The
//      attribute row itself (the ctor token + value blob) is re-read from the
//      MetadataFile per handle inside ProcessAttribute.
//  (b) The C# nullable `bool[]`/`string[]`/`Nullability[]` fields port to
//      `std::optional<std::vector<...>>` (nullopt = the C# null array); the
//      C# null string entries of TupleElementNames port to empty strings (the
//      established null-name mapping -- the PdbExtraTypeInfo convention).
//  (c) The C# `Debug.Assert` in ExpectDummyNullabilityForGenericValueType and
//      the tuple-element count is compiled out of the release assembly the
//      shipped engine runs; the port keeps the index-advancing
//      GetNullability() call (its side effect is load-bearing) and drops the
//      assert, mirroring the shipped behavior.
//  (d) The C# local function `ProcessAttribute` ports as a lambda inside the
//      metadata entry (capturing the locals by reference).
//  (e) The class ctor is private (the C# `private ApplyAttributeTypeVisitor`);
//      the static entries construct through the raw
//      `std::shared_ptr<T>(new T(...))` form (the make_shared private-ctor
//      access rule -- the access is checked at the new-expression's point of
//      use, which is inside the class's own static member).

#pragma once

#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata { class MetadataFile; }
namespace ILSpy::Decompiler::DebugInfo { struct PdbExtraTypeInfo; }

namespace ILSpy::Decompiler::TypeSystem {

class ICompilation;

// Introduces 'dynamic' and tuple types based on attribute values (the C#
// `sealed class ApplyAttributeTypeVisitor : TypeVisitor`). All overrides are
// out-of-line in the .cpp (they name ITypeDefinition / ITypeParameter /
// TupleType / TypeSystemExtensions members kept to forward declarations here).
class ApplyAttributeTypeVisitor final : public TypeVisitor {
public:
    // The C# `public static IType ApplyAttributesToType(IType inputType,
    // ICompilation compilation, SRM.CustomAttributeHandleCollection? attributes,
    // SRM.MetadataReader metadata, TypeSystemOptions options, Nullability
    // nullableContext, bool typeChildrenOnly = false,
    // SRM.CustomAttributeHandleCollection? additionalAttributes = null)`
    // (ApplyAttributeTypeVisitor.cs line 47). The `compilation ?? throw`
    // null-check is structurally unreachable through the C++ reference
    // parameter (the D374 convention).
    static ITypePtr ApplyAttributesToType(
        ITypePtr inputType, ICompilation& compilation,
        const std::optional<std::vector<std::uint32_t>>& attributes,
        const ::ILSpy::Decompiler::Metadata::MetadataFile& metadata,
        TypeSystemOptions options,
        ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext,
        bool typeChildrenOnly = false,
        const std::optional<std::vector<std::uint32_t>>& additionalAttributes = std::nullopt);

    // The C# `public static IType ApplyAttributesToType(IType inputType,
    // ICompilation compilation, TypeSystemOptions options, PdbExtraTypeInfo
    // pdbExtraTypeInfo)` (line 115) -- the PDB-seeded entry (no metadata
    // reads; the flags/names are pre-decoded by the debug-info provider).
    static ITypePtr ApplyAttributesToType(
        ITypePtr inputType, ICompilation& compilation,
        TypeSystemOptions options,
        const ::ILSpy::Decompiler::DebugInfo::PdbExtraTypeInfo& pdbExtraTypeInfo);

    // The C# overrides (see the .cpp for the per-arm notes):
    ITypePtr VisitModOpt(ModifiedType& type) override;
    ITypePtr VisitModReq(ModifiedType& type) override;
    ITypePtr VisitPointerType(PointerType& type) override;
    ITypePtr VisitArrayType(ArrayType& type) override;
    ITypePtr VisitByReferenceType(ByReferenceType& type) override;
    ITypePtr VisitParameterizedType(ParameterizedType& type) override;
    ITypePtr VisitFunctionPointerType(FunctionPointerType& type) override;
    ITypePtr VisitTypeDefinition(ITypeDefinition& type) override;
    ITypePtr VisitOtherType(IType& type) override;
    ITypePtr VisitTypeParameter(ITypeParameter& type) override;

private:
    // The C# `private ApplyAttributeTypeVisitor(...)` (line 143): the recorded
    // attribute state + the input-independent visitor configuration. The
    // parameter types are the C# shapes under convention (b).
    ApplyAttributeTypeVisitor(ICompilation& compilation,
        bool hasDynamicAttribute,
        std::optional<std::vector<bool>> dynamicAttributeData,
        bool hasNativeIntegersAttribute,
        std::optional<std::vector<bool>> nativeIntegersAttributeData,
        TypeSystemOptions options,
        std::optional<std::vector<std::string>> tupleElementNames,
        ::ILSpy::Decompiler::TypeSystem::Nullability defaultNullability,
        std::optional<std::vector<::ILSpy::Decompiler::TypeSystem::Nullability>> nullableAttributeData);

    // The C# `Nullability GetNullability()` (line 236): the next recorded
    // nullability byte at the walk position, else the context default. The
    // index ADVANCES on every call (load-bearing even where the caller only
    // asserts the result, convention (c)).
    ::ILSpy::Decompiler::TypeSystem::Nullability GetNullability();

    // The C# `void ExpectDummyNullabilityForGenericValueType()` (line 243):
    // consumes one nullability slot (the dummy position a generic VALUE type
    // occupies in the encoding); the Debug.Assert is release-stripped
    // (convention (c)) so the whole body is the GetNullability() call.
    void ExpectDummyNullabilityForGenericValueType();

    ICompilation& compilation_;
    bool hasDynamicAttribute_;
    std::optional<std::vector<bool>> dynamicAttributeData_;
    bool hasNativeIntegersAttribute_;
    std::optional<std::vector<bool>> nativeIntegersAttributeData_;
    TypeSystemOptions options_;
    std::optional<std::vector<std::string>> tupleElementNames_;
    ::ILSpy::Decompiler::TypeSystem::Nullability defaultNullability_;
    std::optional<std::vector<::ILSpy::Decompiler::TypeSystem::Nullability>> nullableAttributeData_;
    // The walk-position counters (the C# mutable locals, advanced by the Visit*
    // overrides as the input type tree is walked).
    int dynamicTypeIndex_ = 0;
    int tupleTypeIndex_ = 0;
    int nullabilityTypeIndex_ = 0;
    int nativeIntTypeIndex_ = 0;
};

}  // namespace ILSpy::Decompiler::TypeSystem
