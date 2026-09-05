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

// Out-of-line definitions for ApplyAttributeTypeVisitor (see the header).

#include "Decompiler/TypeSystem/ApplyAttributeTypeVisitor.hpp"

#include "Decompiler/DebugInfo/IDebugInfoProvider.hpp"
#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"  // MinimalAttributeTypeProvider
#include "Decompiler/Metadata/MetadataFile.hpp"          // GetCustomAttribute (the row read)
#include "Decompiler/Metadata/SRMExtensions.hpp"        // GetAttributeType / IsKnownType(handle)
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"  // IsTupleCompatible / CreateTupleType / TupleRestPosition
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // IsKnownType(IType, KnownTypeCode)

#include <algorithm>
#include <any>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

namespace {

// The `arg.Value is ImmutableArray<CustomAttributeTypedArgument<IType>>` shape:
// the decoder stores the decoded array elements as a
// `std::vector<CustomAttributeTypedArgument>` inside the `std::any` (an EMPTY
// vector for the count-0 array; a null array -- the C# -1 count -- leaves the
// any EMPTY, which is the C# `Value is null` non-match).
const std::vector<CustomAttributeTypedArgument>* FixedArgArray(const std::any& value)
{
    return std::any_cast<std::vector<CustomAttributeTypedArgument>>(&value);
}

}  // namespace

ITypePtr ApplyAttributeTypeVisitor::ApplyAttributesToType(
    ITypePtr inputType, ICompilation& compilation,
    const std::optional<std::vector<std::uint32_t>>& attributes,
    const ::ILSpy::Decompiler::Metadata::MetadataFile& metadata,
    TypeSystemOptions options,
    ::ILSpy::Decompiler::TypeSystem::Nullability nullableContext,
    bool typeChildrenOnly,
    const std::optional<std::vector<std::uint32_t>>& additionalAttributes)
{
    using ::ILSpy::Decompiler::Metadata::CustomAttributeDecoder;
    using ::ILSpy::Decompiler::Metadata::GetAttributeType;
    using ::ILSpy::Decompiler::Metadata::IsKnownType;
    using ::ILSpy::Decompiler::Metadata::MinimalAttributeTypeProvider;

    bool hasDynamicAttribute = false;
    std::optional<std::vector<bool>> dynamicAttributeData;
    // The C# initializes the native-integers flag from
    // NativeIntegersWithoutAttribute BEFORE the attribute loop (every IntPtr
    // becomes nint when the option is set, attribute or not).
    bool hasNativeIntegersAttribute =
        (options & TypeSystemOptions::NativeIntegersWithoutAttribute) != TypeSystemOptions::None;
    std::optional<std::vector<bool>> nativeIntegersAttributeData;
    std::optional<std::vector<std::string>> tupleElementNames;
    ::ILSpy::Decompiler::TypeSystem::Nullability nullability;
    std::optional<std::vector<::ILSpy::Decompiler::TypeSystem::Nullability>> nullableAttributeData;
    if ((options & TypeSystemOptions::NullabilityAnnotations) != TypeSystemOptions::None) {
        nullability = nullableContext;
    } else {
        nullability = Nullability::Oblivious;
    }

    // The C# local function `ProcessAttribute` (line 64): decodes one attribute
    // row into the recorded state. Each arm gates on its option, classifies the
    // row's constructor's declaring type against the KnownAttribute table, and
    // decodes the value blob through minimalCorlibTypeProvider (the
    // CustomAttributeDecoder); a matching-but-malformed blob's decode error
    // propagates (the C# propagates the BadImageFormatException /
    // EnumUnderlyingTypeResolveException).
    auto ProcessAttribute = [&](std::uint32_t attrHandle) {
        // The attribute TYPE (the C# `attr.GetAttributeType(metadata)`): the
        // constructor's declaring type; the row's own token drives the read.
        std::uint32_t attrType = GetAttributeType(metadata, attrHandle);
        if ((options & TypeSystemOptions::Dynamic) != TypeSystemOptions::None
            && IsKnownType(metadata, attrType, KnownAttribute::Dynamic))
        {
            hasDynamicAttribute = true;
            // The C# `attr.DecodeValue(minimalCorlibTypeProvider)` -- one
            // decoder per row over the minimal corlib provider.
            CustomAttributeDecoder decoder(metadata, MinimalAttributeTypeProvider(), false);
            const auto row = metadata.GetCustomAttribute(attrHandle);
            Metadata::CustomAttributeValue ctor = row
                ? decoder.DecodeValue(row->ConstructorToken,
                      row->ValueBlob ? row->ValueBlob->data() : nullptr,
                      row->ValueBlob ? row->ValueBlob->size() : 0)
                : Metadata::CustomAttributeValue{};
            if (ctor.FixedArguments.size() == 1) {
                const CustomAttributeTypedArgument& arg = ctor.FixedArguments[0];
                // `Value()` returns the std::any BY VALUE: materialize it before
                // taking its address (a pointer into the call temporary dangles at
                // the end of the condition's full expression).
                const std::any argValue = arg.Value();
                // C# `arg.Value is ImmutableArray<...> values && values.All(v => v.Value is bool)`
                // -> `dynamicAttributeData = values.SelectArray(v => (bool)v.Value)`.
                if (const auto* values = FixedArgArray(argValue)) {
                    bool all = true;
                    for (const auto& v : *values) {
                        if (std::any_cast<bool>(&v.Value()) == nullptr) {
                            all = false;
                            break;
                        }
                    }
                    if (all) {
                        std::vector<bool> data;
                        data.reserve(values->size());
                        for (const auto& v : *values) {
                            data.push_back(std::any_cast<bool>(v.Value()));
                        }
                        dynamicAttributeData = std::move(data);
                    }
                }
            }
        } else if ((options & TypeSystemOptions::NativeIntegers) != TypeSystemOptions::None
            && IsKnownType(metadata, attrType, KnownAttribute::NativeInteger))
        {
            hasNativeIntegersAttribute = true;
            CustomAttributeDecoder decoder(metadata, MinimalAttributeTypeProvider(), false);
            const auto row = metadata.GetCustomAttribute(attrHandle);
            Metadata::CustomAttributeValue ctor = row
                ? decoder.DecodeValue(row->ConstructorToken,
                      row->ValueBlob ? row->ValueBlob->data() : nullptr,
                      row->ValueBlob ? row->ValueBlob->size() : 0)
                : Metadata::CustomAttributeValue{};
            if (ctor.FixedArguments.size() == 1) {
                const CustomAttributeTypedArgument& arg = ctor.FixedArguments[0];
                // `Value()` returns the std::any BY VALUE: materialize it before
                // taking its address (a pointer into the call temporary dangles at
                // the end of the condition's full expression).
                const std::any argValue = arg.Value();
                if (const auto* values = FixedArgArray(argValue)) {
                    bool all = true;
                    for (const auto& v : *values) {
                        if (std::any_cast<bool>(&v.Value()) == nullptr) {
                            all = false;
                            break;
                        }
                    }
                    if (all) {
                        std::vector<bool> data;
                        data.reserve(values->size());
                        for (const auto& v : *values) {
                            data.push_back(std::any_cast<bool>(v.Value()));
                        }
                        nativeIntegersAttributeData = std::move(data);
                    }
                }
            }
        } else if ((options & TypeSystemOptions::Tuple) != TypeSystemOptions::None
            && IsKnownType(metadata, attrType, KnownAttribute::TupleElementNames))
        {
            CustomAttributeDecoder decoder(metadata, MinimalAttributeTypeProvider(), false);
            const auto row = metadata.GetCustomAttribute(attrHandle);
            Metadata::CustomAttributeValue ctor = row
                ? decoder.DecodeValue(row->ConstructorToken,
                      row->ValueBlob ? row->ValueBlob->data() : nullptr,
                      row->ValueBlob ? row->ValueBlob->size() : 0)
                : Metadata::CustomAttributeValue{};
            if (ctor.FixedArguments.size() == 1) {
                const CustomAttributeTypedArgument& arg = ctor.FixedArguments[0];
                // `Value()` returns the std::any BY VALUE: materialize it before
                // taking its address (a pointer into the call temporary dangles at
                // the end of the condition's full expression).
                const std::any argValue = arg.Value();
                // C# `values.All(v => v.Value is string || v.Value == null)` -- a
                // null entry is the C# null string (an empty `std::any`), which
                // the SelectArray keeps as null; the port maps it to the empty
                // string (convention (b)).
                if (const auto* values = FixedArgArray(argValue)) {
                    bool all = true;
                    for (const auto& v : *values) {
                        const std::any& val = v.Value();
                        if (val.has_value() && std::any_cast<std::string>(&val) == nullptr) {
                            all = false;
                            break;
                        }
                    }
                    if (all) {
                        std::vector<std::string> data;
                        data.reserve(values->size());
                        for (const auto& v : *values) {
                            const std::any& val = v.Value();
                            if (val.has_value()) {
                                data.push_back(std::any_cast<std::string>(val));
                            } else {
                                data.emplace_back();
                            }
                        }
                        tupleElementNames = std::move(data);
                    }
                }
            }
        } else if ((options & TypeSystemOptions::NullabilityAnnotations) != TypeSystemOptions::None
            && IsKnownType(metadata, attrType, KnownAttribute::Nullable))
        {
            CustomAttributeDecoder decoder(metadata, MinimalAttributeTypeProvider(), false);
            const auto row = metadata.GetCustomAttribute(attrHandle);
            Metadata::CustomAttributeValue ctor = row
                ? decoder.DecodeValue(row->ConstructorToken,
                      row->ValueBlob ? row->ValueBlob->data() : nullptr,
                      row->ValueBlob ? row->ValueBlob->size() : 0)
                : Metadata::CustomAttributeValue{};
            if (ctor.FixedArguments.size() == 1) {
                const CustomAttributeTypedArgument& arg = ctor.FixedArguments[0];
                // `Value()` returns the std::any BY VALUE: materialize it before
                // taking its address (a pointer into the call temporary dangles at
                // the end of the condition's full expression).
                const std::any argValue = arg.Value();
                // C# `values.All(v => v.Value is byte b && b <= 2)` -- the byte
                // form of the per-position array; else `arg.Value is byte b &&
                // b <= 2` -- the single-byte default for the whole type.
                if (const auto* values = FixedArgArray(argValue)) {
                    bool all = true;
                    for (const auto& v : *values) {
                        const auto* b = std::any_cast<std::uint8_t>(&v.Value());
                        if (b == nullptr || *b > 2) {
                            all = false;
                            break;
                        }
                    }
                    if (all) {
                        std::vector<Nullability> data;
                        data.reserve(values->size());
                        for (const auto& v : *values) {
                            data.push_back(static_cast<Nullability>(
                                std::any_cast<std::uint8_t>(v.Value())));
                        }
                        nullableAttributeData = std::move(data);
                    }
                } else if (const auto* b = std::any_cast<std::uint8_t>(&argValue)) {
                    if (*b <= 2) {
                        nullability = static_cast<Nullability>(*b);
                    }
                }
            }
        }
    };

    // The C# `const TypeSystemOptions relevantOptions = Dynamic | Tuple |
    // NullabilityAnnotations | NativeIntegers;` -- both loops run only when at
    // least one attribute-consuming option is set (the row decode is pointless
    // otherwise). Additional attributes run AFTER the normal ones and OVERRIDE
    // the recorded values (the C# comment).
    const TypeSystemOptions relevantOptions =
        TypeSystemOptions::Dynamic | TypeSystemOptions::Tuple
        | TypeSystemOptions::NullabilityAnnotations | TypeSystemOptions::NativeIntegers;
    if (attributes.has_value() && (options & relevantOptions) != TypeSystemOptions::None) {
        for (std::uint32_t attrHandle : *attributes) {
            ProcessAttribute(attrHandle);
        }
    }
    if (additionalAttributes.has_value() && (options & relevantOptions) != TypeSystemOptions::None) {
        // Note: additional attributes will override the values from the normal attributes.
        for (std::uint32_t attrHandle : *additionalAttributes) {
            ProcessAttribute(attrHandle);
        }
    }
    if (hasDynamicAttribute || hasNativeIntegersAttribute
        || nullability != Nullability::Oblivious || nullableAttributeData.has_value()
        || (options & (TypeSystemOptions::Tuple | TypeSystemOptions::KeepModifiers))
            != TypeSystemOptions::KeepModifiers)
    {
        // The raw `shared_ptr<T>(new T(...))` form reaches the private ctor
        // (convention (e) -- make_shared cannot).
        auto visitor = std::shared_ptr<ApplyAttributeTypeVisitor>(
            new ApplyAttributeTypeVisitor(
                compilation, hasDynamicAttribute, std::move(dynamicAttributeData),
                hasNativeIntegersAttribute, std::move(nativeIntegersAttributeData),
                options, std::move(tupleElementNames),
                nullability, std::move(nullableAttributeData)));
        if (typeChildrenOnly) {
            return inputType->VisitChildren(*visitor);
        } else {
            return inputType->AcceptVisitor(*visitor);
        }
    } else {
        return inputType;
    }
}

ITypePtr ApplyAttributeTypeVisitor::ApplyAttributesToType(
    ITypePtr inputType, ICompilation& compilation, TypeSystemOptions options,
    const ::ILSpy::Decompiler::DebugInfo::PdbExtraTypeInfo& pdbExtraTypeInfo)
{
    // C# `if (pdbExtraTypeInfo.DynamicFlags is null && pdbExtraTypeInfo.TupleElementNames
    // is null) return inputType;`
    if (!pdbExtraTypeInfo.DynamicFlags.has_value()
        && !pdbExtraTypeInfo.TupleElementNames.has_value()) {
        return inputType;
    }
    // C# `inputType.AcceptVisitor(new ApplyAttributeTypeVisitor(compilation,
    // pdbExtraTypeInfo.DynamicFlags != null, pdbExtraTypeInfo.DynamicFlags, false,
    // null, options, pdbExtraTypeInfo.TupleElementNames, Nullability.Oblivious, null))`.
    auto visitor = std::shared_ptr<ApplyAttributeTypeVisitor>(
        new ApplyAttributeTypeVisitor(
            compilation,
            pdbExtraTypeInfo.DynamicFlags.has_value(), pdbExtraTypeInfo.DynamicFlags,
            false, std::nullopt,
            options, pdbExtraTypeInfo.TupleElementNames,
            Nullability::Oblivious, std::nullopt));
    return inputType->AcceptVisitor(*visitor);
}

ApplyAttributeTypeVisitor::ApplyAttributeTypeVisitor(
    ICompilation& compilation,
    bool hasDynamicAttribute,
    std::optional<std::vector<bool>> dynamicAttributeData,
    bool hasNativeIntegersAttribute,
    std::optional<std::vector<bool>> nativeIntegersAttributeData,
    TypeSystemOptions options,
    std::optional<std::vector<std::string>> tupleElementNames,
    ::ILSpy::Decompiler::TypeSystem::Nullability defaultNullability,
    std::optional<std::vector<::ILSpy::Decompiler::TypeSystem::Nullability>> nullableAttributeData)
    : compilation_(compilation),
      hasDynamicAttribute_(hasDynamicAttribute),
      dynamicAttributeData_(std::move(dynamicAttributeData)),
      hasNativeIntegersAttribute_(hasNativeIntegersAttribute),
      nativeIntegersAttributeData_(std::move(nativeIntegersAttributeData)),
      options_(options),
      tupleElementNames_(std::move(tupleElementNames)),
      defaultNullability_(defaultNullability),
      nullableAttributeData_(std::move(nullableAttributeData))
{
    // The C# `compilation ?? throw new ArgumentNullException(nameof(compilation))`
    // is structurally unreachable through the C++ reference parameter.
}

// The C# `VisitModOpt` (line 205) / `VisitModReq` (line 214): a custom modifier
// occupies one slot of the dynamic-flag walk; with KeepModifiers the modifier
// itself survives (the base visit reconstructs around the visited element),
// without it the modifier is transparent (the element is visited directly).
ITypePtr ApplyAttributeTypeVisitor::VisitModOpt(ModifiedType& type)
{
    dynamicTypeIndex_++;
    if ((options_ & TypeSystemOptions::KeepModifiers) != TypeSystemOptions::None) {
        return TypeVisitor::VisitModOpt(type);
    } else {
        return type.Element()->AcceptVisitor(*this);
    }
}

ITypePtr ApplyAttributeTypeVisitor::VisitModReq(ModifiedType& type)
{
    dynamicTypeIndex_++;
    if ((options_ & TypeSystemOptions::KeepModifiers) != TypeSystemOptions::None) {
        return TypeVisitor::VisitModReq(type);
    } else {
        return type.Element()->AcceptVisitor(*this);
    }
}

// The C# `VisitPointerType` (line 225): a pointer occupies one slot of the
// dynamic-flag walk; the base visits the element.
ITypePtr ApplyAttributeTypeVisitor::VisitPointerType(PointerType& type)
{
    dynamicTypeIndex_++;
    return TypeVisitor::VisitPointerType(type);
}

// The C# `Nullability GetNullability()` (line 236): the next recorded
// nullability at the walk position, else the context default (the single-byte
// [Nullable] form) -- the index advances on every call.
::ILSpy::Decompiler::TypeSystem::Nullability ApplyAttributeTypeVisitor::GetNullability()
{
    if (nullableAttributeData_
        && nullabilityTypeIndex_ < static_cast<int>(nullableAttributeData_->size())) {
        return (*nullableAttributeData_)[nullabilityTypeIndex_++];
    } else {
        return defaultNullability_;
    }
}

void ApplyAttributeTypeVisitor::ExpectDummyNullabilityForGenericValueType()
{
    // The C# `var n = GetNullability(); Debug.Assert(n == Nullability.Oblivious);`
    // -- the assert is compiled out of the shipped release assembly, but the
    // GetNullability() call still consumes the slot (load-bearing).
    (void)GetNullability();
}

// The C# `VisitArrayType` (line 248): the array's own nullability comes FIRST
// in the walk, then the element (the base visit).
ITypePtr ApplyAttributeTypeVisitor::VisitArrayType(ArrayType& type)
{
    ::ILSpy::Decompiler::TypeSystem::Nullability nullability = GetNullability();
    dynamicTypeIndex_++;
    return TypeVisitor::VisitArrayType(type)->ChangeNullability(nullability);
}

// The C# `VisitByReferenceType` (line 256): a byref occupies one dynamic slot;
// no nullability (a byref cannot be annotated).
ITypePtr ApplyAttributeTypeVisitor::VisitByReferenceType(ByReferenceType& type)
{
    dynamicTypeIndex_++;
    return TypeVisitor::VisitByReferenceType(type);
}

// The C# `VisitParameterizedType` (line 262): the tuple reconstruction over
// the 8-ary ValueTuple nesting, else the generic walk.
ITypePtr ApplyAttributeTypeVisitor::VisitParameterizedType(ParameterizedType& type)
{
    bool useTupleTypes = (options_ & TypeSystemOptions::Tuple) != TypeSystemOptions::None;
    if (useTupleTypes) {
        int tupleCardinality = 0;
        if (IsTupleCompatible(type, tupleCardinality)) {
            if (tupleCardinality > 1) {
                // The C# `type.GetDefinition()?.ParentModule` -- the assembly the
                // underlying ValueTuple chain resolves through.
                const ITypeDefinition* definition = type.GetDefinition();
                const IModule* valueTupleAssembly =
                    definition != nullptr ? definition->ParentModule() : nullptr;
                // The C# `ImmutableArray<string> elementNames = default;` then the
                // Array.Copy extraction when names remain at the walk position
                // (the entries past the available names stay null -- the port's
                // empty strings).
                std::optional<std::vector<std::string>> elementNames;
                if (tupleElementNames_
                    && tupleTypeIndex_ < static_cast<int>(tupleElementNames_->size())) {
                    std::vector<std::string> extractedValues(tupleCardinality);
                    int copyCount = std::min(tupleCardinality,
                        static_cast<int>(tupleElementNames_->size()) - tupleTypeIndex_);
                    for (int i = 0; i < copyCount; i++) {
                        extractedValues[i] = (*tupleElementNames_)[tupleTypeIndex_ + i];
                    }
                    elementNames = std::move(extractedValues);
                }
                tupleTypeIndex_ += tupleCardinality;
                ExpectDummyNullabilityForGenericValueType();
                std::vector<ITypePtr> elementTypes;
                elementTypes.reserve(tupleCardinality);
                // The C# do-while over the (reassigned) `type` local: each level
                // contributes up to 7 elements, and an 8-argument level recurses
                // into the `Rest` (8th) argument when it is another tuple.
                ParameterizedType* current = &type;
                do {
                    int normalArgCount = std::min(
                        static_cast<int>(current->TypeArguments().size()),
                        TupleRestPosition - 1);
                    for (int i = 0; i < normalArgCount; i++) {
                        dynamicTypeIndex_++;
                        elementTypes.push_back(current->TypeArguments()[i]->AcceptVisitor(*this));
                    }
                    if (static_cast<int>(current->TypeArguments().size()) == TupleRestPosition) {
                        // C# `type = type.TypeArguments.Last() as ParameterizedType;`
                        ParameterizedType* rest = dynamic_cast<ParameterizedType*>(
                            current->TypeArguments().back().get());
                        ExpectDummyNullabilityForGenericValueType();
                        dynamicTypeIndex_++;
                        if (rest != nullptr) {
                            int nestedCardinality = 0;
                            if (IsTupleCompatible(*rest, nestedCardinality)) {
                                tupleTypeIndex_ += nestedCardinality;
                                current = rest;
                            } else {
                                // C# `Debug.Fail("TRest should be another value tuple")`
                                // -- release-stripped; the walk ends.
                                current = nullptr;
                            }
                        } else {
                            current = nullptr;
                        }
                    } else {
                        current = nullptr;
                    }
                } while (current != nullptr);
                // The C# Debug.Assert(elementTypes.Count == tupleCardinality) is
                // release-stripped.
                return CreateTupleType(compilation_, std::move(elementTypes),
                    std::move(elementNames), valueTupleAssembly);
            } else {
                // C# doesn't have syntax for tuples of cardinality <= 1: consume
                // the names and fall through to the generic walk.
                tupleTypeIndex_ += tupleCardinality;
            }
        }
    }
    // Visit generic type and type arguments.
    // Like the base implementation, except that it increments dynamicTypeIndex.
    ITypePtr genericType = type.GenericType()->AcceptVisitor(*this);
    if (genericType->IsReferenceType() != std::optional<bool>(true)
        && !IsKnownType(*genericType, KnownTypeCode::NullableOfT)) {
        ExpectDummyNullabilityForGenericValueType();
    }
    bool changed = genericType.get() != type.GenericType().get();
    std::vector<ITypePtr> arguments(type.TypeArguments().size());
    for (std::size_t i = 0; i < type.TypeArguments().size(); i++) {
        dynamicTypeIndex_++;
        arguments[i] = type.TypeArguments()[i]->AcceptVisitor(*this);
        changed = changed || arguments[i].get() != type.TypeArguments()[i].get();
    }
    if (!changed) {
        return type.shared_from_this();
    }
    return std::make_shared<ParameterizedType>(std::move(genericType), std::move(arguments));
}

// The C# `VisitFunctionPointerType` (line 331): the return type first (two
// dynamic slots when ref-readonly -- the [In] modreq), then the parameters
// (In/Out/RefReadOnly count the modreq/modopt as a second slot).
ITypePtr ApplyAttributeTypeVisitor::VisitFunctionPointerType(FunctionPointerType& type)
{
    dynamicTypeIndex_++;
    if (type.ReturnIsRefReadOnly()) {
        dynamicTypeIndex_++;
    }
    ITypePtr returnType = type.ReturnType()->AcceptVisitor(*this);
    bool changed = returnType.get() != type.ReturnType().get();
    std::vector<ITypePtr> parameters(type.ParameterTypes().size());
    for (std::size_t i = 0; i < parameters.size(); i++) {
        // The C# switch over ParameterReferenceKinds: in/out also count the
        // modreq, RefReadOnly counts the modopt.
        switch (type.ParameterReferenceKinds()[i]) {
            case ReferenceKind::None:
                dynamicTypeIndex_ += 1;
                break;
            case ReferenceKind::Ref:
                dynamicTypeIndex_ += 1;
                break;
            case ReferenceKind::Out:
                dynamicTypeIndex_ += 2;  // in/out also count the modreq
                break;
            case ReferenceKind::In:
                dynamicTypeIndex_ += 2;
                break;
            case ReferenceKind::RefReadOnly:
                dynamicTypeIndex_ += 2;  // counts the modopt
                break;
            default:
                // The C# `_ => throw new NotSupportedException()`.
                throw std::runtime_error("Specified method is not supported.");
        }
        parameters[i] = type.ParameterTypes()[i]->AcceptVisitor(*this);
        changed = changed || parameters[i].get() != type.ParameterTypes()[i].get();
    }
    if (!changed) {
        return type.shared_from_this();
    }
    return type.WithSignature(std::move(returnType), std::move(parameters));
}

// The C# `VisitTypeDefinition` (line 366): Object -> dynamic and
// IntPtr/UIntPtr -> nint/nuint at the walk position; then the reference-type
// nullability annotation.
ITypePtr ApplyAttributeTypeVisitor::VisitTypeDefinition(ITypeDefinition& type)
{
    // The C# `IType newType = type;`.
    ITypePtr newType = type.shared_from_this();
    KnownTypeCode ktc = type.KnownTypeCode();
    if (ktc == KnownTypeCode::Object && hasDynamicAttribute_) {
        // A missing/out-of-range flag slot or a `true` flag selects dynamic.
        if (!dynamicAttributeData_
            || dynamicTypeIndex_ >= static_cast<int>(dynamicAttributeData_->size())) {
            newType = Dynamic();
        } else if ((*dynamicAttributeData_)[dynamicTypeIndex_]) {
            newType = Dynamic();
        }
    } else if ((ktc == KnownTypeCode::IntPtr || ktc == KnownTypeCode::UIntPtr)
        && hasNativeIntegersAttribute_) {
        // Native integers use the same indexing logic as 'dynamic'.
        if (!nativeIntegersAttributeData_
            || nativeIntTypeIndex_ >= static_cast<int>(nativeIntegersAttributeData_->size())) {
            newType = (ktc == KnownTypeCode::IntPtr) ? NInt() : NUInt();
        } else if ((*nativeIntegersAttributeData_)[nativeIntTypeIndex_]) {
            newType = (ktc == KnownTypeCode::IntPtr) ? NInt() : NUInt();
        }
        nativeIntTypeIndex_++;
    }
    if (type.IsReferenceType() == std::optional<bool>(true)) {
        ::ILSpy::Decompiler::TypeSystem::Nullability nullability = GetNullability();
        return newType->ChangeNullability(nullability);
    } else {
        return newType;
    }
}

// The C# `VisitOtherType` (line 394): the base visits the children (no-op for
// the C++-only minimal types), then an UNKNOWN REFERENCE type takes the
// nullability annotation (the UnboundTypeArgument/UnknownType-with-known-name
// shapes).
ITypePtr ApplyAttributeTypeVisitor::VisitOtherType(IType& type)
{
    ITypePtr visited = TypeVisitor::VisitOtherType(type);
    if (visited->Kind() == TypeKind::Unknown
        && visited->IsReferenceType() == std::optional<bool>(true)) {
        ::ILSpy::Decompiler::TypeSystem::Nullability nullability = GetNullability();
        visited = visited->ChangeNullability(nullability);
    }
    return visited;
}

// The C# `VisitTypeParameter` (line 406): every type parameter takes the
// nullability annotation at its walk position.
ITypePtr ApplyAttributeTypeVisitor::VisitTypeParameter(ITypeParameter& type)
{
    ::ILSpy::Decompiler::TypeSystem::Nullability nullability = GetNullability();
    return type.ChangeNullability(nullability);
}

}  // namespace ILSpy::Decompiler::TypeSystem
