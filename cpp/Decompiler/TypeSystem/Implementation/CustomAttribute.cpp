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
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// CustomAttribute.cs -- see the header comment.

#include "Decompiler/TypeSystem/Implementation/CustomAttribute.hpp"

#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

CustomAttribute::CustomAttribute(const MetadataModule& module,
                                 const IMethod* attrCtor,
                                 std::uint32_t handle)
    : module_(module), handle_(handle), constructor_(attrCtor)
{
}

const IType& CustomAttribute::AttributeType() const
{
    // The C# `public IType AttributeType => Constructor.DeclaringType;` --
    // the resolved constructor's declaring type. Every resolved constructor
    // (a MetadataMethod or a CreateFakeMethod result) carries a non-null
    // declaring type (the fake's is an UnknownType), so the non-null
    // reference contract holds; the assert is the release-form stand-in for
    // the C# Debug.Asserts.
    const ITypePtr declaringType = constructor_->DeclaringType();
    if (!declaringType)
        throw std::runtime_error(
            "Object reference not set to an instance of an object.");
    return *declaringType;
}

bool CustomAttribute::HasDecodeErrors() const
{
    DecodeValue();
    return hasDecodeErrors_;
}

std::vector<CustomAttributeTypedArgument>
CustomAttribute::FixedArguments() const
{
    DecodeValue();
    return fixedArguments_;
}

std::vector<CustomAttributeNamedArgument>
CustomAttribute::NamedArguments() const
{
    DecodeValue();
    return namedArguments_;
}

void CustomAttribute::DecodeValue() const
{
    std::lock_guard<std::mutex> lock(syncRoot_);
    if (valueDecoded_)
        return;
    try {
        // The C# `var attr = metadata.GetCustomAttribute(handle); value =
        // attr.DecodeValue(module.TypeProvider);` -- the row read (the
        // constructor token + the value blob) feeding the port's decoder over
        // the module-owned provider. The provider surface is non-const (the
        // CustomAttributeDecoder contract) reached through the const module
        // accessor's pointee (the const TypeProvider& mutable-pointee
        // convention, the ResolveType precedent).
        const Metadata::MetadataFile* metadata = module_.MetadataFile();
        std::optional<Metadata::CustomAttributeRowInfo> row =
            metadata->GetCustomAttribute(handle_);
        if (!row)
            throw std::invalid_argument("Read out of bounds.");
        Metadata::CustomAttributeDecoder decoder(
            *metadata,
            const_cast<TypeProvider&>(module_.TypeProvider()));
        Metadata::CustomAttributeValue value = decoder.DecodeValue(
            row->ConstructorToken,
            row->ValueBlob ? row->ValueBlob->data() : nullptr,
            row->ValueBlob ? row->ValueBlob->size() : 0);
        fixedArguments_ = std::move(value.FixedArguments);
        namedArguments_ = std::move(value.NamedArguments);
        valueDecoded_ = true;
    } catch (const Metadata::EnumUnderlyingTypeResolveException&) {
        // The C# catch (EnumUnderlyingTypeResolveException): reset to the
        // empty value with the error flag, never trying again.
        fixedArguments_.clear();
        namedArguments_.clear();
        hasDecodeErrors_ = true;
        valueDecoded_ = true;
    } catch (const std::invalid_argument&) {
        // The C# catch (BadImageFormatException) -- the decoder's
        // convention-(d) mapping.
        fixedArguments_.clear();
        namedArguments_.clear();
        hasDecodeErrors_ = true;
        valueDecoded_ = true;
    } catch (const std::out_of_range&) {
        // A documented belt-and-braces arm (convention (c)): no real decode
        // path reaches it (the decoder maps its BadImageFormat arms to
        // std::invalid_argument), but a crafted manifest's row read can throw
        // the winmd raw-surface family, and the C# catches the equivalent
        // BadImageFormatException there.
        fixedArguments_.clear();
        namedArguments_.clear();
        hasDecodeErrors_ = true;
        valueDecoded_ = true;
    }
}

const IMember* CustomAttribute::MemberForNamedArgument(
    const IType& attributeType,
    const CustomAttributeNamedArgument& namedArgument)
{
    switch (namedArgument.Kind()) {
        case CustomAttributeNamedArgumentKind::Field: {
            // The C# `attributeType.GetFields(f => f.Name ==
            // namedArgument.Name).LastOrDefault()` -- the LAST name match of
            // the member enumeration.
            std::vector<const IField*> fields = attributeType.GetFields(
                [&namedArgument](const IField* field) {
                    return field->Name() == namedArgument.Name();
                });
            return fields.empty() ? nullptr : fields.back();
        }
        case CustomAttributeNamedArgumentKind::Property: {
            std::vector<const IProperty*> properties =
                attributeType.GetProperties(
                    [&namedArgument](const IProperty* property) {
                        return property->Name() == namedArgument.Name();
                    });
            return properties.empty() ? nullptr : properties.back();
        }
        default:
            return nullptr;
    }
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
