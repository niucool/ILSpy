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

// AttributeListBuilder.cs -- see the header comment.

#include "Decompiler/TypeSystem/Implementation/AttributeListBuilder.hpp"

#include "Decompiler/Metadata/CustomAttributeDecoder.hpp"
#include "Decompiler/Metadata/EnumUnderlyingTypeResolveException.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/Implementation/CustomAttribute.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultAttribute.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeSystemOptions.hpp"
#include "Decompiler/TypeSystem/TypeProvider.hpp"
#include "Decompiler/Util/Utf.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

namespace {

// The SRM blob-reader texts (the CustomAttributeDecoder.cpp constants -- the
// decompiled System.Reflection.Metadata 10 semantics).
constexpr const char* kReadOutOfBounds = "Read out of bounds.";
constexpr const char* kInvalidCompressedInteger = "Invalid compressed integer.";
constexpr const char* kInvalidSerializedString = "Invalid serialized string.";

// The C# `BlobReader` cursor over a marshalling-descriptor / security blob.
struct BlobCursor {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos;
};

// The C# `BlobReader.ReadByte()` -- the fixed-size read (Throw.OutOfBounds at
// the end of the blob).
std::uint8_t ReadByte(BlobCursor& r)
{
    if (r.pos >= r.size)
        throw std::invalid_argument(kReadOutOfBounds);
    return r.data[r.pos++];
}

// The decompiled SRM `MemoryBlock.PeekCompressedInteger`: a 1-byte form under
// 0x80, a 2-byte form under 0x80-0xBF, a 4-byte form under 0xC0-0xDF;
// everything else (the reserved 0xE0-0xFF prefixes, truncation, end-of-blob)
// is INVALID (int.MaxValue) with ZERO bytes consumed.
int ReadCompressedIntegerOrInvalid(BlobCursor& r)
{
    if (r.pos >= r.size)
        return 0x7FFFFFFF;
    std::uint8_t b = r.data[r.pos];
    std::size_t remaining = r.size - r.pos;
    if ((b & 0x80) == 0) {
        r.pos += 1;
        return b;
    }
    if ((b & 0x40) == 0) {
        if (remaining >= 2) {
            r.pos += 2;
            return (static_cast<int>(b & 0x3F) << 8)
                | r.data[r.pos - 1];
        }
    } else if ((b & 0x20) == 0 && remaining >= 4) {
        r.pos += 4;
        return (static_cast<int>(b & 0x1F) << 24)
            | (static_cast<int>(r.data[r.pos - 3]) << 16)
            | (static_cast<int>(r.data[r.pos - 2]) << 8)
            | static_cast<int>(r.data[r.pos - 1]);
    }
    return 0x7FFFFFFF;
}

// The C# `BlobReader.TryReadCompressedInteger(out int value)`:
// `value = ReadCompressedIntegerOrInvalid(); return value != int.MaxValue;`.
bool TryReadCompressedInteger(BlobCursor& r, int& value)
{
    value = ReadCompressedIntegerOrInvalid(r);
    return value != 0x7FFFFFFF;
}

// The C# `BlobReader.ReadCompressedInteger()` -- the throwing form.
int ReadCompressedInteger(BlobCursor& r)
{
    int value;
    if (!TryReadCompressedInteger(r, value))
        throw std::invalid_argument(kInvalidCompressedInteger);
    return value;
}

// The C# `BlobReader.ReadSerializedString()`: the SerString -- a compressed
// byte length then that many UTF-8 bytes; the 0xFF form is null (the port's
// nullopt). The port stores the payload bytes verbatim (the repo's UTF-8
// string convention).
std::optional<std::string> ReadSerializedString(BlobCursor& r)
{
    int length;
    if (TryReadCompressedInteger(r, length)) {
        // The C# `ReadUTF8(length)` -- a bounds-checked byte read.
        if (length < 0
            || static_cast<std::size_t>(length) > r.size - r.pos)
            throw std::invalid_argument(kReadOutOfBounds);
        std::string result(
            reinterpret_cast<const char*>(r.data + r.pos),
            static_cast<std::size_t>(length));
        r.pos += static_cast<std::size_t>(length);
        return result;
    }
    if (ReadByte(r) != 0xFF)
        throw std::invalid_argument(kInvalidSerializedString);
    return std::nullopt;
}

// The C# `BlobReader.RemainingBytes`.
std::size_t RemainingBytes(const BlobCursor& r)
{
    return r.size - r.pos;
}

// The `const IType&` (an `ICompilation::FindType(KnownTypeCode)` result -- a
// non-null reference) to `ITypePtr` bridge: the non-owning alias (the
// KnownTypeCache convention (d), the FindType result outlives the builder
// through the compilation).
ITypePtr AliasOf(const IType& type)
{
    return ITypePtr(std::shared_ptr<IType>(), const_cast<IType*>(&type));
}

// The C# `attributeType.Namespace` read (the minimal-IType divergence,
// convention (f)): the MetadataMethod.cpp `NamespaceOf` dispatch, copied next
// to its consumer.
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

// The C# `attributeType.DeclaringType != null` read (convention (f)): the
// entity types answer through `IEntity::DeclaringType` (the AbstractType
// default is null, which the non-entity dispatch reproduces).
bool HasDeclaringType(const IType& type)
{
    if (const auto* entity = dynamic_cast<const IEntity*>(&type))
        return entity->DeclaringType() != nullptr;
    return false;
}

// The C# `module.Compilation.FindType(new TopLevelTypeName("System.Runtime.
// InteropServices", name))` -- the modules-scan extension over the implicit
// TopLevelTypeName -> FullTypeName conversion (the explicit port ctor).
ITypePtr FindInteropType(const ICompilation& compilation,
                         const char* name)
{
    return FindType(compilation,
                    FullTypeName(TopLevelTypeName(
                        "System.Runtime.InteropServices", name, 0)));
}

} // namespace

AttributeListBuilder::AttributeListBuilder(const MetadataModule& module)
    : module_(module)
{
}

AttributeListBuilder::AttributeListBuilder(const MetadataModule& module,
                                           int capacity)
    : module_(module)
{
    attributes_.reserve(static_cast<std::size_t>(capacity < 0 ? 0
                                                               : capacity));
}

void AttributeListBuilder::Add(std::shared_ptr<IAttribute> attr)
{
    attributes_.push_back(std::move(attr));
}

void AttributeListBuilder::Add(KnownAttribute type)
{
    Add(module_.MakeAttribute(type));
}

void AttributeListBuilder::Add(KnownAttribute type, KnownTypeCode argType,
                               std::any argValue)
{
    // The C# `ImmutableArray.Create(new CustomAttributeTypedArgument<IType>(
    // module.Compilation.FindType(argType), argValue))`.
    Add(type,
        std::vector<CustomAttributeTypedArgument>{
            CustomAttributeTypedArgument(
                AliasOf(module_.Compilation().FindType(argType)),
                std::move(argValue))});
}

void AttributeListBuilder::Add(KnownAttribute type,
                               const TopLevelTypeName& argType,
                               std::any argValue)
{
    Add(type,
        std::vector<CustomAttributeTypedArgument>{
            CustomAttributeTypedArgument(
                FindType(module_.Compilation(), FullTypeName(argType)),
                std::move(argValue))});
}

void AttributeListBuilder::Add(
    KnownAttribute type,
    std::vector<CustomAttributeTypedArgument> fixedArguments)
{
    Add(std::make_shared<DefaultAttribute>(module_.GetAttributeType(type),
                                           std::move(fixedArguments),
                                           std::vector<
                                               CustomAttributeNamedArgument>()));
}

void AttributeListBuilder::AddMarshalInfo(
    const std::optional<std::vector<std::uint8_t>>& marshalInfo)
{
    // The C# `if (marshalInfo.IsNil) return;`.
    if (!marshalInfo)
        return;
    Add(ConvertMarshalInfo(marshalInfo->data(), marshalInfo->size()));
}

void AttributeListBuilder::Add(std::uint32_t entityToken,
                               SymbolKind target)
{
    const Metadata::MetadataFile* metadata = module_.MetadataFile();
    for (std::uint32_t attributeToken :
         metadata->GetCustomAttributeTokens(entityToken)) {
        std::optional<Metadata::CustomAttributeRowInfo> row =
            metadata->GetCustomAttribute(attributeToken);
        if (!row)
            throw std::invalid_argument(kReadOutOfBounds);
        // The C# "Attribute types shouldn't be open generic, so we don't need
        // a generic context" -- the struct-default `new GenericContext()`.
        const IMethod* ctor =
            module_.ResolveMethod(row->ConstructorToken, GenericContext());
        if (ctor == nullptr)
            throw std::runtime_error(
                "Object reference not set to an instance of an object.");
        const ITypePtr type = ctor->DeclaringType();
        if (!type)
            throw std::runtime_error(
                "Object reference not set to an instance of an object.");
        if (IgnoreAttribute(*type, target))
            continue;
        Add(std::make_shared<CustomAttribute>(module_, ctor,
                                              attributeToken));
    }
}

bool AttributeListBuilder::IgnoreAttribute(const IType& attributeType,
                                           SymbolKind target)
{
    // The C# `if (attributeType.DeclaringType != null ||
    // attributeType.TypeParameterCount != 0) return false;`.
    if (HasDeclaringType(attributeType)
        || attributeType.TypeParameterCount() != 0)
        return false;
    // The C# `return IgnoreAttribute(new TopLevelTypeName(
    // attributeType.Namespace, attributeType.Name), target);` -- the
    // 2-arg ctor leaves the type-parameter count 0.
    return IgnoreAttribute(
        TopLevelTypeName(NamespaceOf(attributeType),
                         attributeType.Name(), 0),
        target);
}

bool AttributeListBuilder::IgnoreAttribute(
    const TopLevelTypeName& attributeType, SymbolKind target)
{
    // The option gates (the module's TypeSystemOptions).
    const ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions options =
        module_.TypeSystemOptions();
    const std::string& ns = attributeType.Namespace();
    const std::string& name = attributeType.Name();
    if (ns == "System.Runtime.CompilerServices") {
        if (name == "DynamicAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        Dynamic)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
        if (name == "NativeIntegerAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        NativeIntegers)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
        if (name == "TupleElementNamesAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        Tuple)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
        if (name == "ExtensionAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        ExtensionMethods)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
        if (name == "DecimalConstantAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        DecimalConstants)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None
                   && (target == SymbolKind::Field
                       || target == SymbolKind::Parameter);
        if (name == "IsReadOnlyAttribute") {
            switch (target) {
                case SymbolKind::TypeDefinition:
                case SymbolKind::Parameter:
                    return (options
                            & ::ILSpy::Decompiler::TypeSystem::
                                TypeSystemOptions::
                                    ReadOnlyStructsAndParameters)
                        != ::ILSpy::Decompiler::TypeSystem::
                               TypeSystemOptions::None;
                case SymbolKind::Method:
                case SymbolKind::Accessor:
                    return (options
                            & ::ILSpy::Decompiler::TypeSystem::
                                TypeSystemOptions::ReadOnlyMethods)
                        != ::ILSpy::Decompiler::TypeSystem::
                               TypeSystemOptions::None;
                case SymbolKind::ReturnType:
                case SymbolKind::Property:
                case SymbolKind::Indexer:
                case SymbolKind::Field:
                    // "ref readonly" is currently always active.
                    return true;
                default:
                    return false;
            }
        }
        if (name == "IsByRefLikeAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        RefStructs)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None
                   && target == SymbolKind::TypeDefinition;
        if (name == "IsUnmanagedAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        UnmanagedConstraints)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None
                   && target == SymbolKind::TypeParameter;
        if (name == "NullableAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        NullabilityAnnotations)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None;
        if (name == "NullableContextAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        NullabilityAnnotations)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None
                   && (target == SymbolKind::TypeDefinition
                       || IsMethodLike(target));
        if (name == "ScopedRefAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        ScopedRef)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None
                   && target == SymbolKind::Parameter;
        if (name == "RequiresLocationAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        RefReadOnlyParameters)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None
                   && target == SymbolKind::Parameter;
        if (name == "ParamCollectionAttribute")
            return (options
                    & ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::
                        ParamsCollections)
                != ::ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None
                   && target == SymbolKind::Parameter;
        return false;
    }
    if (ns == "System") {
        return name == "ParamArrayAttribute"
            && target == SymbolKind::Parameter;
    }
    return false;
}

bool AttributeListBuilder::IsMethodLike(SymbolKind kind)
{
    switch (kind) {
        case SymbolKind::Method:
        case SymbolKind::Operator:
        case SymbolKind::Constructor:
        case SymbolKind::Destructor:
        case SymbolKind::Accessor:
            return true;
        default:
            return false;
    }
}

bool AttributeListBuilder::HasAttribute(
    const Metadata::MetadataFile& metadata, std::uint32_t entityToken,
    KnownAttribute attribute, SymbolKind symbolKind)
{
    for (std::uint32_t attributeToken :
         metadata.GetCustomAttributeTokens(entityToken)) {
        // The C# answers on the FIRST known-attribute row -- `return !
        // IgnoreAttribute(...)` -- false for an ignored row WITHOUT
        // scanning further.
        if (Metadata::IsKnownAttribute(metadata, attributeToken, attribute))
            return !IgnoreAttribute(GetTypeName(attribute), symbolKind);
    }
    return false;
}

std::shared_ptr<IAttribute> AttributeListBuilder::GetAttribute(
    const Metadata::MetadataFile& metadata, std::uint32_t entityToken,
    KnownAttribute attribute, SymbolKind symbolKind)
{
    // The C# CONTINUES scanning when a known row is target-ignored (the
    // `IgnoreAttribute` test inside the `if`, unlike HasAttribute's
    // first-row answer).
    for (std::uint32_t attributeToken :
         metadata.GetCustomAttributeTokens(entityToken)) {
        if (Metadata::IsKnownAttribute(metadata, attributeToken, attribute)
            && !IgnoreAttribute(GetTypeName(attribute), symbolKind)) {
            std::optional<Metadata::CustomAttributeRowInfo>
                row = metadata.GetCustomAttribute(attributeToken);
            if (!row)
                throw std::invalid_argument(kReadOutOfBounds);
            const IMethod* ctor = module_.ResolveMethod(
                row->ConstructorToken, GenericContext());
            if (ctor == nullptr)
                throw std::runtime_error(
                    "Object reference not set to an instance of an object.");
            return std::make_shared<CustomAttribute>(module_, ctor,
                                                      attributeToken);
        }
    }
    return nullptr;
}

void AttributeListBuilder::AddSecurityAttributes(std::uint32_t parentToken)
{
    for (const auto& secDecl :
         module_.MetadataFile()->GetDeclarativeSecurityAttributes(
             parentToken)) {
        // The C# `if (secDecl.IsNil) continue;` is vacuous through the
        // port's parent-keyed row read (only real rows enumerate).
        try {
            AddSecurityAttributes(secDecl);
        } catch (const Metadata::EnumUnderlyingTypeResolveException&) {
            // The C# `catch (EnumUnderlyingTypeResolveException)`: ignore
            // resolve errors.
        } catch (const std::invalid_argument&) {
            // The C# `catch (BadImageFormatException)`: ignore invalid
            // security declarations.
        }
    }
}

void AttributeListBuilder::AddSecurityAttributes(
    const Metadata::MetadataFile::DeclarativeSecurityInfo& secDecl)
{
    // The C# `module.Compilation.FindType(new TopLevelTypeName(
    // "System.Security.Permissions", "SecurityAction"))`.
    ITypePtr securityActionType = FindType(
        module_.Compilation(),
        FullTypeName(TopLevelTypeName("System.Security.Permissions",
                                      "SecurityAction", 0)));
    CustomAttributeTypedArgument securityAction(
        securityActionType, std::any(static_cast<int>(secDecl.Action)));
    BlobCursor r{secDecl.PermissionSet.data(),
                secDecl.PermissionSet.size(), 0};
    // The C# reads one byte to select the encoding; an EMPTY blob throws
    // here (the BadImageFormatException the per-row catch swallows).
    if (ReadByte(r) == '.') {
        // Binary attribute.
        int attributeCount = ReadCompressedInteger(r);
        for (int i = 0; i < attributeCount; i++) {
            Add(ReadBinarySecurityAttribute(
                secDecl.PermissionSet.data(), secDecl.PermissionSet.size(),
                r.pos, securityAction));
        }
    } else {
        // For backward compatibility with .NET 1.0: XML-encoded attribute.
        // The C# `reader.Reset()`.
        r.pos = 0;
        Add(ReadXmlSecurityAttribute(
            secDecl.PermissionSet.data(), secDecl.PermissionSet.size(),
            r.pos, securityAction));
    }
}

std::shared_ptr<IAttribute> AttributeListBuilder::ReadXmlSecurityAttribute(
    const std::uint8_t* base, std::size_t size, std::size_t& pos,
    const CustomAttributeTypedArgument& securityAction)
{
    // The C# `reader.ReadUTF16(reader.RemainingBytes)` -- the whole
    // remaining blob as UTF-16 (an odd tail byte drops, the established
    // decode convention).
    std::size_t remaining = size - pos;
    std::u16string units;
    units.reserve(remaining / 2);
    for (std::size_t i = 0; i + 2 <= remaining; i += 2) {
        units.push_back(static_cast<char16_t>(
            base[pos + i]
            | (static_cast<char16_t>(base[pos + i + 1]) << 8)));
    }
    std::string xml = ILSpy::Decompiler::Util::Utf16ToUtf8(units);
    pos = size;
    AttributeBuilder b(module_, KnownAttribute::PermissionSet);
    b.AddFixedArg(securityAction);
    b.AddNamedArg("XML", KnownTypeCode::String, std::any(std::move(xml)));
    return b.Build();
}

std::shared_ptr<IAttribute>
AttributeListBuilder::ReadBinarySecurityAttribute(
    const std::uint8_t* base, std::size_t size, std::size_t& pos,
    const CustomAttributeTypedArgument& securityAction)
{
    BlobCursor r{base, size, pos};
    std::optional<std::string> attributeTypeName = ReadSerializedString(r);
    // The C# `module.TypeProvider.GetTypeFromSerializedName(
    // attributeTypeName)` -- the provider's null arm (a null name) yields a
    // null IType, which the DefaultAttribute ctor below rejects (the
    // ArgumentNullException); the port maps that shape through the assert
    // (the D424 convention) -- the divergence note in the header.
    ITypePtr attributeType = module_.TypeProvider().GetTypeFromSerializedName(
        attributeTypeName.value_or(""));
    ReadCompressedInteger(r);  // ?? -- "The specification seems to be
                               // incorrect here, so I'm using the logic
                               // from Cecil instead."
    int numNamed = ReadCompressedInteger(r);
    Metadata::CustomAttributeDecoder decoder(
        *module_.MetadataFile(),
        const_cast<TypeProvider&>(module_.TypeProvider()));
    std::vector<CustomAttributeNamedArgument> namedArgs =
        decoder.DecodeNamedArguments(base, size, r.pos, numNamed);
    pos = r.pos;
    return std::make_shared<DefaultAttribute>(
        std::move(attributeType),
        std::vector<CustomAttributeTypedArgument>{securityAction},
        std::move(namedArgs));
}

std::shared_ptr<IAttribute> AttributeListBuilder::ConvertMarshalInfo(
    const std::uint8_t* data, std::size_t size)
{
    BlobCursor r{data, size};
    AttributeBuilder b(module_, KnownAttribute::MarshalAs);
    ITypePtr unmanagedTypeType =
        FindInteropType(module_.Compilation(), "UnmanagedType");

    int type = ReadByte(r);
    b.AddFixedArg(unmanagedTypeType, std::any(type));

    switch (type) {
        case 0x1e:  // FixedArray
        {
            int fixedSize;
            if (!TryReadCompressedInteger(r, fixedSize))
                fixedSize = 0;
            b.AddNamedArg("SizeConst", KnownTypeCode::Int32,
                          std::any(fixedSize));
            if (RemainingBytes(r) > 0) {
                type = ReadByte(r);
                if (type != 0x66)  // None
                    b.AddNamedArg("ArraySubType", unmanagedTypeType,
                                  std::any(type));
            }
            break;
        }
        case 0x1d:  // SafeArray
        {
            if (RemainingBytes(r) > 0) {
                int varType = ReadByte(r);
                if (varType != 0) {  // VarEnum.VT_EMPTY
                    ITypePtr varEnumType =
                        FindInteropType(module_.Compilation(), "VarEnum");
                    b.AddNamedArg("SafeArraySubType", varEnumType,
                                  std::any(varType));
                }
            }
            break;
        }
        case 0x2a:  // NATIVE_TYPE_ARRAY
        {
            int size2;
            int value;
            if (RemainingBytes(r) > 0) {
                type = ReadByte(r);
            } else {
                type = 0x66;  // Cecil uses NativeType.None as default.
            }
            if (type != 0x50) {  // Max
                b.AddNamedArg("ArraySubType", unmanagedTypeType,
                              std::any(type));
            }
            int sizeParameterIndex =
                TryReadCompressedInteger(r, value) ? value : -1;
            size2 = TryReadCompressedInteger(r, value) ? value : -1;
            int sizeParameterMultiplier =
                TryReadCompressedInteger(r, value) ? value : -1;
            if (size2 >= 0) {
                b.AddNamedArg("SizeConst", KnownTypeCode::Int32,
                              std::any(size2));
            }
            if (sizeParameterMultiplier != 0 && sizeParameterIndex >= 0) {
                b.AddNamedArg("SizeParamIndex", KnownTypeCode::Int16,
                              std::any(static_cast<std::int16_t>(
                                  sizeParameterIndex)));
            }
            break;
        }
        case 0x2c: {  // CustomMarshaler
            std::optional<std::string> guidValue = ReadSerializedString(r);
            std::optional<std::string> unmanagedType =
                ReadSerializedString(r);
            std::optional<std::string> managedType =
                ReadSerializedString(r);
            std::optional<std::string> cookie = ReadSerializedString(r);
            (void)guidValue;
            if (managedType) {
                b.AddNamedArg("MarshalType", KnownTypeCode::String,
                              std::any(*managedType));
            }
            if (cookie && !cookie->empty()) {
                b.AddNamedArg("MarshalCookie", KnownTypeCode::String,
                              std::any(*cookie));
            }
            break;
        }
        case 0x17:  // FixedSysString
            b.AddNamedArg("SizeConst", KnownTypeCode::Int32,
                          std::any(ReadCompressedInteger(r)));
            break;
        default:
            break;
    }

    return b.Build();
}

std::vector<std::shared_ptr<IAttribute>> AttributeListBuilder::Build()
{
    // The C# returns the built array (Empty<IAttribute>.Array when empty);
    // the port returns the owning vector.
    return std::move(attributes_);
}

// --- AttributeBuilder ---

AttributeBuilder::AttributeBuilder(const MetadataModule& module,
                                   KnownAttribute attributeType)
    : module_(module),
      attributeType_(module.GetAttributeType(attributeType))
{
}

AttributeBuilder::AttributeBuilder(const MetadataModule& module,
                                   ITypePtr attributeType)
    : module_(module), attributeType_(std::move(attributeType))
{
}

void AttributeBuilder::AddFixedArg(CustomAttributeTypedArgument arg)
{
    fixedArgs_.push_back(std::move(arg));
}

void AttributeBuilder::AddFixedArg(KnownTypeCode type, std::any value)
{
    AddFixedArg(AliasOf(module_.Compilation().FindType(type)),
                std::move(value));
}

void AttributeBuilder::AddFixedArg(const TopLevelTypeName& type,
                                   std::any value)
{
    AddFixedArg(FindType(module_.Compilation(), FullTypeName(type)),
                std::move(value));
}

void AttributeBuilder::AddFixedArg(ITypePtr type, std::any value)
{
    fixedArgs_.emplace_back(std::move(type), std::move(value));
}

void AttributeBuilder::AddNamedArg(const std::string& name,
                                   KnownTypeCode type, std::any value)
{
    AddNamedArg(name, AliasOf(module_.Compilation().FindType(type)),
                std::move(value));
}

void AttributeBuilder::AddNamedArg(const std::string& name,
                                   const TopLevelTypeName& type,
                                   std::any value)
{
    AddNamedArg(name, FindType(module_.Compilation(), FullTypeName(type)),
                std::move(value));
}

void AttributeBuilder::AddNamedArg(const std::string& name, ITypePtr type,
                                   std::any value)
{
    // The C# kind classification: a FIELD with the name on the attribute
    // type (the ReturnMemberDefinitions enumeration) -> Field, else
    // Property.
    CustomAttributeNamedArgumentKind kind;
    if (!attributeType_->GetFields(
            [&name](const IField* field) {
                return field->Name() == name;
            },
            GetMemberOptions::ReturnMemberDefinitions)
             .empty()) {
        kind = CustomAttributeNamedArgumentKind::Field;
    } else {
        kind = CustomAttributeNamedArgumentKind::Property;
    }
    namedArgs_.emplace_back(name, kind, std::move(type), std::move(value));
}

std::shared_ptr<IAttribute> AttributeBuilder::Build()
{
    return std::make_shared<DefaultAttribute>(attributeType_,
                                              std::move(fixedArgs_),
                                              std::move(namedArgs_));
}

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
