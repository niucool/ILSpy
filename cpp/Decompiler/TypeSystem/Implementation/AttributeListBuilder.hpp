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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/AttributeListBuilder.cs
// -- the `readonly struct AttributeListBuilder` plus the sibling `struct
// AttributeBuilder` (same C# file): the one factory every metadata entity's
// `GetAttributes()` builds its `IAttribute` list through. It composes
// synthetic builtin attributes ([Serializable], [StructLayout],
// [DllImport], [FieldOffset], [Optional], ... -- constructed over the
// compilation's FindType through the `MetadataModule.MakeAttribute` cache),
// the metadata custom-attribute rows (`CustomAttribute` per surviving row,
// after the `IgnoreAttribute` option/target filter), the marshalling
// descriptors ([MarshalAs] over the II.23.4 blob), and the security
// declarations (the binary and XML-encoded [PermissionSet] forms), plus the
// `HasAttribute` / `GetAttribute` known-attribute scans the entities'
// `HasAttribute(KnownAttribute)` / `GetAttribute(KnownAttribute)` members
// route through.
//
// KEY PORT CONVENTIONS:
//  (a) OWNERSHIP: the C# builder holds a `List<IAttribute>` of GC references;
//    the port holds `std::vector<std::shared_ptr<IAttribute>>` and `Build()`
//    RETURNS THE OWNING VECTOR (the C# `IAttribute[]` -- an array of
//    references the caller owns). The consuming entities cache the vector
//    (their `GetAttributes()` projects it as raw pointers), and the
//    `MakeAttribute` entries are the module's cached instances (the
//    shared_ptr copies keep the module cache and the built list aliasing the
//    SAME attribute object, faithful to the C# `LazyInit` identity).
//  (b) The C# `Add(CustomAttributeHandleCollection attributes, SymbolKind
//    target)` ports to `Add(std::uint32_t entityToken, SymbolKind target)`
//    (the GetCustomAttributeTokens composition -- the HasSemantics
//    convention): the row walk, the per-row `module.ResolveMethod(constructor,
//    GenericContext())` (the struct-default context -- "Attribute types
//    shouldn't be open generic"), and the `IgnoreAttribute` filter.
//  (c) The C# `AddSecurityAttributes(DeclarativeSecurityAttributeHandleCollection)`
//    ports to `AddSecurityAttributes(std::uint32_t parentToken)` (the
//    GetDeclarativeSecurityAttributes composition); the per-row overload
//    takes the port's `Metadata::DeclarativeSecurityInfo` row (the raw
//    Action + PermissionSet blob).
//  (d) `HasAttribute` / `GetAttribute` take the metadata file + the PARENT
//    token (the C# takes the reader + the row collection); the subtle C#
//    difference is preserved verbatim: `HasAttribute` answers on the FIRST
//    known-attribute row (returning `!IgnoreAttribute(...)` -- false for an
//    ignored row, WITHOUT scanning further), while `GetAttribute` CONTINUES
//    scanning when the first known row is target-ignored.
//  (e) The `Debug.Assert`s (the null module, `attribute.IsCustomAttribute()`)
//    are compiled out of the shipped release assembly (the release-form
//    convention); the port takes the members at face value.
//  (f) The `IgnoreAttribute` IType overload needs the C# `IType.Namespace` /
//    `IType.Name` / `IType.DeclaringType` members the port's minimal `IType`
//    omits -- the reads route through the entity/parameterized/unknown
//    dispatch (the MetadataMethod.cpp `NamespaceOf` convention, copied next
//    to its consumer here; `DeclaringType` through the `IEntity` cast, the
//    AbstractType null default for the non-entity types).

#pragma once

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>



namespace ILSpy::Decompiler::TypeSystem {

class MetadataModule;

namespace Implementation {

class AttributeListBuilder {
public:
    // The C# `AttributeListBuilder(MetadataModule module)` (the capacity
    // overload is the same shape with a reserved list).
    explicit AttributeListBuilder(const MetadataModule& module);
    AttributeListBuilder(const MetadataModule& module, int capacity);

    // The C# `public void Add(IAttribute attr)`.
    void Add(std::shared_ptr<IAttribute> attr);

    // The C# `public void Add(KnownAttribute type)` -- "use the assemblies'
    // cache for simple attributes" (the `MetadataModule.MakeAttribute`
    // parameterless-instance cache).
    void Add(KnownAttribute type);

    // The C# `public void Add(KnownAttribute type, KnownTypeCode argType,
    // object argValue)` / `(KnownAttribute type, TopLevelTypeName argType,
    // object argValue)` -- a builtin attribute with a single positional
    // argument of known type (the FindType of the arg type).
    void Add(KnownAttribute type, KnownTypeCode argType,
             std::any argValue);
    void Add(KnownAttribute type, const TopLevelTypeName& argType,
             std::any argValue);

    // The C# `public void Add(KnownAttribute type, ImmutableArray<
    // CustomAttributeTypedArgument<IType>> fixedArguments)` -- a builtin
    // attribute with explicit positional arguments.
    void Add(KnownAttribute type,
             std::vector<CustomAttributeTypedArgument> fixedArguments);

    // The C# `internal void AddMarshalInfo(BlobHandle marshalInfo)` -- the
    // [MarshalAs] attribute reconstructed from the II.23.4 marshalling
    // descriptor blob (nullopt = the nil handle; the C# IsNil early return).
    void AddMarshalInfo(
        const std::optional<std::vector<std::uint8_t>>& marshalInfo);

    // The C# `public void Add(CustomAttributeHandleCollection attributes,
    // SymbolKind target)` (convention (b)).
    void Add(std::uint32_t entityToken, SymbolKind target);

    // The C# `public void AddSecurityAttributes(
    // DeclarativeSecurityAttributeHandleCollection securityDeclarations)`
    // (convention (c)) and the C# per-row overload.
    void AddSecurityAttributes(std::uint32_t parentToken);
    void AddSecurityAttributes(
        const Metadata::MetadataFile::DeclarativeSecurityInfo&
            secDecl);

    // The C# `internal bool HasAttribute(MetadataReader metadata,
    // CustomAttributeHandleCollection customAttributes, KnownAttribute
    // attribute, SymbolKind symbolKind)` (convention (d)).
    bool HasAttribute(const Metadata::MetadataFile& metadata,
                      std::uint32_t entityToken,
                      KnownAttribute attribute, SymbolKind symbolKind);

    // The C# `internal IAttribute GetAttribute(...)` (convention (d)) -- the
    // found attribute as an OWNING shared_ptr (null -> nullptr; the caller
    // keeps it alive, the C# GC root equivalent).
    std::shared_ptr<IAttribute> GetAttribute(
        const Metadata::MetadataFile& metadata,
        std::uint32_t entityToken, KnownAttribute attribute,
        SymbolKind symbolKind);

    // The C# `internal bool IgnoreAttribute(TopLevelTypeName attributeType,
    // SymbolKind target)` -- the option/target gate table (public here for
    // the entities' direct calls, the internal-access convention).
    bool IgnoreAttribute(const TopLevelTypeName& attributeType,
                         SymbolKind target);

    // The C# `public IAttribute[] Build()` -- the owning vector (convention
    // (a)); the C# `Empty<IAttribute>.Array` for the empty list (the port's
    // empty vector).
    std::vector<std::shared_ptr<IAttribute>> Build();

private:
    // The C# `bool IgnoreAttribute(IType attributeType, SymbolKind target)`
    // (convention (f)).
    bool IgnoreAttribute(const IType& attributeType, SymbolKind target);

    // The C# `static bool IsMethodLike(SymbolKind kind)`.
    static bool IsMethodLike(SymbolKind kind);

    // The C# `IAttribute ConvertMarshalInfo(SRM.BlobReader marshalInfo)` --
    // the [MarshalAs] reconstruction over the blob cursor.
    std::shared_ptr<IAttribute> ConvertMarshalInfo(
        const std::uint8_t* data, std::size_t size);

    // The C# `IAttribute ReadXmlSecurityAttribute(ref BlobReader,
    // CustomAttributeTypedArgument securityAction)` / `ReadBinarySecurityAttribute(...)`
    // -- the cursor is the (base, size, pos) convention.
    std::shared_ptr<IAttribute> ReadXmlSecurityAttribute(
        const std::uint8_t* base, std::size_t size, std::size_t& pos,
        const CustomAttributeTypedArgument& securityAction);
    std::shared_ptr<IAttribute> ReadBinarySecurityAttribute(
        const std::uint8_t* base, std::size_t size, std::size_t& pos,
        const CustomAttributeTypedArgument& securityAction);

    const MetadataModule& module_;
    std::vector<std::shared_ptr<IAttribute>> attributes_;
};

// The C# `struct AttributeBuilder` -- the factory for a builtin attribute
// with explicit fixed/named arguments (the [StructLayout] / [DllImport] /
// [MethodImpl] / [MarshalAs] / [PermissionSet] constructions inside the
// entities' GetAttributes bodies).
class AttributeBuilder {
public:
    // The C# `AttributeBuilder(MetadataModule module, KnownAttribute
    // attributeType)` (the module's cached known-attribute type) / the IType
    // overload.
    AttributeBuilder(const MetadataModule& module,
                     KnownAttribute attributeType);
    AttributeBuilder(const MetadataModule& module, ITypePtr attributeType);

    // The C# `AddFixedArg` forms (the already-typed argument; the known-type
    // code; the top-level type name; the raw (type, value) pair).
    void AddFixedArg(CustomAttributeTypedArgument arg);
    void AddFixedArg(KnownTypeCode type, std::any value);
    void AddFixedArg(const TopLevelTypeName& type, std::any value);
    void AddFixedArg(ITypePtr type, std::any value);

    // The C# `AddNamedArg` forms -- the Field-vs-Property kind classification
    // over the attribute type's member lists (a field with the name ->
    // CustomAttributeNamedArgumentKind::Field, else Property).
    void AddNamedArg(const std::string& name, KnownTypeCode type,
                     std::any value);
    void AddNamedArg(const std::string& name, const TopLevelTypeName& type,
                     std::any value);
    void AddNamedArg(const std::string& name, ITypePtr type,
                     std::any value);

    // The C# `public IAttribute Build()`.
    std::shared_ptr<IAttribute> Build();

private:
    const MetadataModule& module_;
    ITypePtr attributeType_;
    std::vector<CustomAttributeTypedArgument> fixedArgs_;
    std::vector<CustomAttributeNamedArgument> namedArgs_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation

} // namespace ILSpy::Decompiler::TypeSystem
