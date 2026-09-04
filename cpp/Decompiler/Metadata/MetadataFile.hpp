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

// A loaded CLI module: the C++ port's stand-in for ICSharpCode.Decompiler's
// MetadataFile / PEFile. Phase 1 wraps the vendored microsoft/winmd ECMA-335
// reader and adds method-body decoding on top (winmd is metadata-only); later
// phases add Portable PDB debug tables, WebCIL, single-file bundles, and the
// full handle ergonomics the type system and IL reader expect.
//
// The winmd dependency (and <windows.h> on Windows) is hidden behind a pimpl,
// so this public header stays clean and compiles cleanly everywhere.

#pragma once

#include "Decompiler/Metadata/MethodBody.hpp"
#include "Decompiler/Metadata/LocalTypeInfo.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// Minimal info for a MethodDef row, enough to drive a Phase 1 method-body test
// and to feed the IL reader later. The full handle ergonomics land with the
// rest of the Phase 1 metadata surface.
struct MethodDefInfo {
    std::string Name;
    std::uint32_t RVA;        // 0 for abstract/extern/pinvoke-only methods (no body)
    std::uint32_t Token;      // metadata token (table 0x06 << 24 | row index, 1-based)
};

// The constructor/static status of a MethodDef, the subset of the C# IMethod
// handle the ILAst transforms consult via ILFunction. Pre-resolved at reader
// time because the port's transforms carry no MetadataFile / IMethod handle
// (the Call::IsNewObj / IsOperator / LdFlda::IsCompilerGeneratedField
// precedent). IsConstructor is faithful to the C# MetadataMethod.SymbolKind ==
// Constructor (a .ctor/.cctor with the SpecialName|RTSpecialName flag); IsStatic
// is faithful to MethodAttributes.Static. Used by the IL reader to populate
// ILFunction::IsConstructor / IsStatic, the gate the NullCoalescingTransform
// hoisted-constructor-argument null-guard fold consults.
struct MethodDefKindInfo {
    bool IsConstructor = false;
    bool IsStatic = false;
};

// A decoded method signature: resolved return and parameter types, whether
// the method has an implicit `this` parameter, and the generic parameter count.
struct MethodSignature {
    ILSpy::Decompiler::TypeSystem::ITypePtr ReturnType;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> ParameterTypes;
    bool IsInstance = false;
    std::uint32_t GenericParameterCount = 0;
};

// A TypeDef row: name, namespace, metadata token (table 0x02), the resolved
// base type (nullptr for System.Object and interfaces with no base), and the
// raw TypeAttributes flags (II.23.1.15). Pseudo-attributes such as
// [Serializable] (tdSerializable = 0x4000) and [NonSerialized] live in the
// flags, not in CustomAttribute rows, so the flags are exposed alongside the
// custom-attribute list.
struct TypeDefInfo {
    std::string Name;
    std::string Namespace;
    std::uint32_t Token;
    std::uint32_t Flags;
    ILSpy::Decompiler::TypeSystem::TypeKind Kind;
    ILSpy::Decompiler::TypeSystem::ITypePtr BaseType;
};

struct MethodInfo {
    std::string Name;
    std::uint32_t Token;   // table 0x06
    std::uint32_t RVA;
};

struct FieldInfo {
    std::string Name;
    std::uint32_t Token;   // table 0x04
};

struct PropertyInfo {
    std::string Name;
    std::uint32_t Token;   // table 0x17
};

struct EventInfo {
    std::string Name;
    std::uint32_t Token;   // table 0x14
};

// A Param table row (table 0x08) as the disassembler's WriteParameters
// consumes it (ReflectionDisassembler.cs lines 1107-1160): the row's own
// token (the WriteParameterAttributes `.param` coded parent and the
// local-reference identity), the Sequence column (0 = the return-value
// row, 1.. the declared parameters -- the C# writeParameters skip), the raw
// Flags column (the System.Reflection ParameterAttributes bits: In 0x1,
// Out 0x2, Optional 0x10, HasDefault 0x1000, HasFieldMarshal 0x2000), the
// authored Name ("" for a nil Name column -- SRM GetString(nil) is the
// empty string), and the row's FieldMarshal NativeType blob (nullopt when
// the param has no marshalling descriptor -- the C# p.GetMarshallingDescriptor()
// IsNil test).
struct ParameterInfo {
    std::uint32_t Token = 0;           // 0x08000000 | row (1-based)
    std::uint16_t SequenceNumber = 0; // the Sequence column
    std::uint32_t Attributes = 0;     // the Flags column (ParameterAttributes)
    std::string Name;                 // "" when the Name column is nil
    std::optional<std::vector<std::uint8_t>> MarshallingDescriptor;
};

// A Constant table row (table 0x0B): the II.23.2 constant a Field (0x04),
// Param (0x08), or Property (0x17) row's DefaultValue resolves to. The C#
// reads it as `metadata.GetConstant(row.GetDefaultValue())`; the port
// fuses the HasConstant coded-index lookup into the read, keyed by the
// parent row's token. TypeCode is the LOW byte of the Type column (the
// SRM ConstantTableReader.GetType PeekByte at the column's offset 0; the
// column is physically 2 bytes wide -- see Ecma335/winmd's
// Constant.set_columns(2, ...)). The repo's pinned SRM (10.0.10) spells
// ConstantTypeCode.NullReference at the ELEMENT_TYPE_CLASS slot 0x12
// (ECMA's null-constant encoding), so a raw 0x12 constant renders as
// "nullref" and the old 0x18/0x1C spellings are invalid codes.
struct ConstantInfo {
    std::uint32_t Token = 0;         // 0x0B000000 | row (1-based)
    std::uint8_t TypeCode = 0;       // the Type column's low byte
    std::vector<std::uint8_t> Value; // the raw value-blob bytes
};

// A GenericParam row (table 0x2A): its metadata token, the ECMA Number column
// (the parameter's authored zero-based position -- SRM's GenericParameter.Index,
// which the disassembler's WriteTypeParameter nil-name fallback reads), and the
// authored Name ("" when the row's Name column is nil -- SRM GetString(nil) is
// the empty string). The direct prerequisite MetadataGenericContext consumes.
struct GenericParameterInfo {
    std::uint32_t Token;    // 0x2A000000 | row (1-based)
    std::uint16_t Number;   // the Number column
    std::uint16_t Flags = 0;  // the raw GenericParam Attributes column (II.23.1.7)
    std::string Name;
};

// A GenericParamConstraint row (table 0x2C): the row's own token (the
// HasCustomAttribute parent the `.param constraint` attribute block reads)
// and the constraint Type (the TypeDefOrRef/TypeSpec coded index the
// ReflectionDisassembler's `.param constraint` header renders).
struct GenericParamConstraintInfo {
    std::uint32_t Token = 0;     // 0x2C000000 | row (1-based)
    std::uint32_t TypeToken = 0; // the Type column (0x02/0x01/0x1B); 0 = nil
};

// A TypeDef row's (table 0x02) name data: the authored Name/Namespace
// strings and the declaring TypeDef's token (0 for a top-level type -- the
// SRM TypeDefinition.GetDeclaringType() NestedClass-table walk). Consumed by
// the SRMExtensions GetFullTypeName readers.
struct TypeDefNameInfo {
    std::string Name;
    std::string Namespace;
    std::uint32_t DeclaringTypeToken = 0;   // 0 when top-level
};

// A TypeRef row's (table 0x01) name data: the authored TypeName/TypeNamespace
// strings and the declaring TypeRef's token (0 when the resolution scope is
// not a TypeRef -- the SRMExtensions GetDeclaringType(this in TypeReference)
// resolution-scope walk). Consumed by the SRMExtensions GetFullTypeName readers.
struct TypeRefNameInfo {
    std::string Name;
    std::string Namespace;
    std::uint32_t DeclaringTypeRefToken = 0; // 0 when top-level
};

// A TypeRef row's (table 0x01) resolution scope: the coded-index kind and,
// where applicable, the scope row's name string and raw token. Consumed by
// the IL InstructionOutputExtensions WriteTo's TypeReference `[assembly]`
// prefix walk (the outermost TypeRef's scope names the referenced module or
// assembly; a TypeRef scope means the row is nested and keeps walking).
struct TypeRefScopeInfo {
    enum class Kind : std::uint8_t {
        None = 0,        // nil scope (the .NET type-in-this-module case)
        Module,          // scope row = the Module table (own module)
        ModuleRef,       // scope row = a ModuleRef (a netmodule reference)
        AssemblyRef,     // scope row = an AssemblyRef
        TypeRef,         // scope row = a nested declaring TypeRef
    };
    Kind Scope = Kind::None;
    std::string Name;             // Module/ModuleRef/AssemblyRef scope name
    std::uint32_t ScopeToken = 0; // the scope row's raw token (0x00/0x1A/0x23/0x01)
};

// A custom attribute applied to an entity: the attribute type's namespace and
// name (e.g. "System", "SerializableAttribute"). Constructor/named-argument
// decoding is deferred to a later phase; the name is enough for the type
// system's attribute checks ([Serializable], [Obsolete], [Extension], ...).
struct CustomAttributeInfo {
    std::string Namespace;
    std::string Name;
};

// A raw CustomAttribute table row (0x0C) for the disassembler paths: the
// constructor (the CustomAttributeType coded index, always a MethodDef 0x06
// or MemberRef 0x0A row) and the value blob (the C# CustomAttribute.Value;
// nullopt is the nil blob handle). The SortByNameProcessor attribute sort
// key composes the constructor's declaring type; WriteAttributes renders
// the constructor and the blob.
struct CustomAttributeRowInfo {
    std::uint32_t Token = 0;             // 0x0C000000 | row (1-based)
    std::uint32_t ConstructorToken = 0;  // the CustomAttributeType target
    std::optional<std::vector<std::uint8_t>> ValueBlob;  // nullopt = nil
};

class MetadataFile {
public:
    explicit MetadataFile(std::string_view path);
    ~MetadataFile();

    MetadataFile(const MetadataFile&) = delete;
    MetadataFile& operator=(const MetadataFile&) = delete;
    MetadataFile(MetadataFile&&) noexcept;
    MetadataFile& operator=(MetadataFile&&) noexcept;

    // True if the file was recognised as a PE/CLI module and parsed.
    bool IsValid() const noexcept;

    // Row count of the TypeDef table.
    std::uint32_t TypeDefCount() const noexcept;

    // Row count of the TypeRef table (0x01) -- the scan bound for callers
    // that locate TypeRef rows by name. 0 for an invalid file; never throws.
    std::uint32_t TypeRefCount() const noexcept;

    // Up to `n` TypeDef type names (the short name, e.g. "Object"), in table
    // order. Returns fewer than `n` if the table is smaller.
    std::vector<std::string> TopTypeNames(std::size_t n) const;

    // All MethodDef rows in table order.
    std::vector<MethodDefInfo> MethodDefs() const;

    // All TypeDef rows in table order, with resolved base types.
    std::vector<TypeDefInfo> TypeDefs() const;

    // Members of a TypeDef (by token): methods, fields, properties, and
    // events, in row order. Empty vectors for an invalid/out-of-range token.
    std::vector<MethodInfo> GetMethods(std::uint32_t typeToken) const;
    std::vector<FieldInfo> GetFields(std::uint32_t typeToken) const;
    std::vector<PropertyInfo> GetProperties(std::uint32_t typeToken) const;
    std::vector<EventInfo> GetEvents(std::uint32_t typeToken) const;

    // An InterfaceImpl row (table 0x09) of a TypeDef: the row's own token
    // and the Interface column (a TypeDef/TypeRef/TypeSpec coded index
    // resolved to a raw token; 0 when the column is nil). The C#
    // TypeDefinition.GetInterfaceImplementations() collection and the
    // SortByNameProcessor interface sort key consume the pair.
    struct InterfaceImplementationInfo {
        std::uint32_t Token = 0;           // 0x09000000 | row (1-based)
        std::uint32_t InterfaceToken = 0;  // 0x02/0x01/0x1B target token
    };
    // The InterfaceImpl rows a TypeDef (0x02) declares, in table order;
    // empty for an invalid file or an out-of-range/non-TypeDef token; never
    // throws.
    std::vector<InterfaceImplementationInfo> GetInterfaceImplementations(
        std::uint32_t typeDefToken) const;
    // One InterfaceImpl row by its own token; nullopt for a nil row, an
    // out-of-range row, or a non-InterfaceImpl token; never throws.
    std::optional<InterfaceImplementationInfo> GetInterfaceImplementation(
        std::uint32_t implToken) const;

    // A Property (0x17) row's Name by its own token ("" for an invalid or
    // non-Property token; the SortByNameProcessor property sort key). Never
    // throws.
    std::string GetPropertyName(std::uint32_t propertyToken) const;
    // An Event (0x14) row's Name by its own token (same contract as
    // GetPropertyName; the event sort key). Never throws.
    std::string GetEventName(std::uint32_t eventToken) const;

    // Parameter names from the Param table for a MethodDef token (table 0x06).
    // Index 0 is the first declared parameter (after any implicit `this`); the
    // vector may be shorter than the parameter count or hold empty strings for
    // parameters that have no Param row.
    std::vector<std::string> GetParameterNames(std::uint32_t methodToken) const;

    // The Param table rows (table 0x08) of a MethodDef (0x06) token, in table
    // order (the MethodDef.ParamList range; the sequence-0 return-value row
    // included -- the WriteParameters skip owns that rule). Empty for an
    // invalid file, an out-of-range row, a nil row, or a non-MethodDef token;
    // never throws.
    std::vector<ParameterInfo> GetParameters(std::uint32_t methodToken) const;

    // The Constant table row (table 0x0B) a Field (0x04), Param (0x08), or
    // Property (0x17) token's DefaultValue resolves to -- the C#
    // metadata.GetConstant(row.GetDefaultValue()) pair fused into one read
    // (the HasConstant coded-index scan of the Constant table's Parent
    // column; mscorlib keeps the table sorted by Parent but the scan does
    // not rely on it). nullopt for an invalid file, a nil/out-of-range row, a
    // token from any other table, or a parent without a constant row; never
    // throws.
    std::optional<ConstantInfo> GetConstant(std::uint32_t parentToken) const;

    // Decode a user-string token (table 0x70, the #US heap) to its text.
    // Returns an empty string if the heap is absent or the offset is bad.
    std::string GetUserString(std::uint32_t token) const;

    // The C# `MetadataReader.GetUserString(UserStringHandle)` null-vs-valid-
    // empty distinction the WriteInstruction String arm needs (a null handle
    // renders NO operand and the always-on token comment; a valid empty #US
    // row renders the empty "" operand). nullopt for an invalid file, no #US
    // heap, an out-of-range offset, or a truncated/short row; the string
    // (possibly empty) otherwise. Never throws.
    std::optional<std::string> TryGetUserString(std::uint32_t token) const;

    // The cor20 header's EntryPointTokenOrRelativeVirtualAddress -- the .NET
    // entrypoint's MethodDef token for .exes (0 for libraries and images
    // without a COM header, the C# `module.CorHeader?... ?? 0` shape). Never
    // throws.
    std::uint32_t GetEntryPointToken() const;

    // Decode the local-variable signature referenced by a method body's
    // LocalVarSigToken (a StandAloneSig token, table 0x11). Returns the local
    // types in index order; an empty vector if the token is 0/invalid/malformed.
    // `ownerMethodToken` (optional) names generic VAR/MVAR params after the
    // owning method's / method's type's GenericParam rows (D173).
    std::vector<TypeSystem::ITypePtr> GetLocalTypes(std::uint32_t localVarSigToken,
                                                    std::uint32_t ownerMethodToken = 0) const;
    // As GetLocalTypes, but also reports each local's pinned flag (the 0x45
    // ELEMENT_TYPE_PINNED marker). The IL reader uses this to mark
    // VariableKind::PinnedLocal, the input to DetectPinnedRegions.
    std::vector<LocalTypeInfo> GetLocalTypesWithPinned(std::uint32_t localVarSigToken,
                                                       std::uint32_t ownerMethodToken = 0) const;

    // Decode the field type of a Field row (by token). Returns nullptr if the
    // token is out of range or the signature is malformed; never throws.
    ILSpy::Decompiler::TypeSystem::ITypePtr GetFieldSignature(std::uint32_t fieldToken) const;

    // The raw II.23.1 attribute-flags column of a metadata row, by token:
    // the value the C# MetadataReader surfaces as the System.Reflection
    // attribute enum (GetFieldDefinition(token).Attributes &c.). The caller
    // masks it with the ECMA-335 flag values -- the Disassembler attribute
    // enums carry exactly them (FieldAttributes II.23.1.5, MethodAttributes
    // II.23.1.10, TypeAttributes II.23.1.15, plus the unnumbered II.23.1
    // property/event flag families). The 2-byte member columns are widened
    // to uint32 (the BCL enum reading a row widens to int). Returns 0 for an
    // invalid file, an out-of-range row, or a token from the wrong table;
    // never throws. The direct ILAmbience prerequisite: the flag-driven
    // .field/.method/.property/.event/.class prefixes the ambience renders.
    std::uint32_t GetTypeDefAttributes(std::uint32_t typeToken) const;
    std::uint32_t GetFieldAttributes(std::uint32_t fieldToken) const;
    std::uint32_t GetMethodAttributes(std::uint32_t methodToken) const;
    std::uint32_t GetPropertyAttributes(std::uint32_t propertyToken) const;
    std::uint32_t GetEventAttributes(std::uint32_t eventToken) const;

    // The GenericParam rows (table 0x2A) owned by a TypeDef (0x02) or MethodDef
    // (0x06) token, in table order -- the port's analog of the SRM row methods
    // TypeDefinition.GetGenericParameters() / MethodDefinition.GetGenericParameters()
    // (SRM returns the same owner-contiguous, positionally-indexed collection;
    // ECMA-335 sorts the GenericParam table by Owner). Returns an empty vector
    // for an invalid file, an out-of-range row, a nil row, or a token from any
    // other table; never throws.
    std::vector<GenericParameterInfo> GetGenericParameters(std::uint32_t ownerToken) const;

    // The declaring TypeDef token (table 0x02) of a MethodDef (0x06) token --
    // the analog of the SRM MethodDefinition.GetDeclaringType() row read that
    // MetadataGenericContext's method-context construction resolves. Returns 0
    // for an invalid file, an out-of-range row, a nil row, or a non-MethodDef
    // token; never throws.
    std::uint32_t GetMethodDeclaringTypeToken(std::uint32_t methodToken) const;

    // A TypeDef row's (table 0x02) authored Name/Namespace strings plus the
    // declaring TypeDef's token -- the per-row reads the SRMExtensions
    // GetFullTypeName(TypeDefinitionHandle) reader composes (the SRM
    // TypeDefinition.Name/Namespace columns and the GetDeclaringType()
    // NestedClass-table walk). The namespace column of a nested type is
    // empty (ECMA-335 requires nested types to carry an empty Namespace).
    // Returns nullopt for an invalid file, an out-of-range row, a nil row, or
    // a non-TypeDef token; never throws.
    std::optional<TypeDefNameInfo> GetTypeDefNameInfo(std::uint32_t typeToken) const;

    // A TypeDef row's (table 0x02) Extends column -- the C# ILSpy
    // SRMExtensions `TypeDefinition.GetBaseTypeOrNil()`: the raw
    // TypeDefOrRef coded index as a TypeDef (0x02) / TypeRef (0x01) /
    // TypeSpec (0x1B) token; 0 for a nil column (interfaces with no base)
    // and for an invalid file, an out-of-range row, a nil row, or a
    // non-TypeDef token; never throws.
    std::uint32_t GetBaseTypeToken(std::uint32_t typeToken) const;

    // The nested TypeDef tokens (table 0x02) of a TypeDef -- the C#
    // `TypeDefinition.GetNestedTypes()`: the NestedClass-table (table
    // 0x29) rows whose EnclosingClass is this row, in table order (the
    // same rows the SRM collection walks).
    // Empty for an invalid file, an out-of-range row, a nil row, or a
    // non-TypeDef token; never throws.
    std::vector<std::uint32_t> GetNestedTypes(std::uint32_t typeToken) const;

    // A TypeRef row's (table 0x01) authored TypeName/TypeNamespace strings plus
    // the declaring TypeRef's token -- the per-row reads the SRMExtensions
    // GetFullTypeName(TypeReferenceHandle) reader composes (the SRM
    // TypeReference.Name/Namespace columns and the GetDeclaringType()
    // resolution-scope walk: a TypeRef-scoped row nests inside that TypeRef,
    // any other scope -- Module/ModuleRef/AssemblyRef -- is top-level).
    // Returns nullopt for an invalid file, an out-of-range row, a nil row, or
    // a non-TypeRef token; never throws.
    std::optional<TypeRefNameInfo> GetTypeRefNameInfo(std::uint32_t typeRefToken) const;

    // A TypeRef row's (table 0x01) resolution scope: the coded-index kind
    // (None/Module/ModuleRef/AssemblyRef/TypeRef), the scope row's name
    // string for the Module/ModuleRef/AssemblyRef kinds, and the scope row's
    // raw token -- the per-row read the IL InstructionOutputExtensions
    // WriteTo's TypeReference `[assembly]` prefix walk composes (the C#
    // tr.ResolutionScope then the outermost-TypeRef switch). Returns nullopt
    // for an invalid file, an out-of-range row, a nil row, or a non-TypeRef
    // token; never throws.
    std::optional<TypeRefScopeInfo> GetTypeRefScopeInfo(std::uint32_t typeRefToken) const;

    // A GenericParam row's (table 0x2A) data by raw token -- the single-row
    // read the DisassemblerSignatureTypeProvider's WriteTypeParameter composes
    // (the C# metadata.GetGenericParameter(paramRef) then Name/Index). Returns
    // nullopt for an invalid file, an out-of-range row, a nil row, or a
    // non-GenericParam token; never throws.
    std::optional<GenericParameterInfo> GetGenericParameterByToken(
        std::uint32_t genericParamToken) const;

    // A TypeSpec row's (table 0x1B) raw signature-blob bytes -- the single-row
    // read the provider-driven SignatureTypeProviderDecoder recurses through
    // for nested/inner TypeSpecs (the pimpl constraint keeps the winmd
    // database inside this file). Returns nullopt for an invalid file, an
    // out-of-range row, a nil row, or a non-TypeSpec token; never throws.
    std::optional<std::vector<std::uint8_t>> GetTypeSpecSignatureBlob(
        std::uint32_t typeSpecToken) const;

    // A MethodDef (column 4), Field (column 2), MemberRef (column 2), or
    // Property (column 2) row's raw signature-blob bytes -- the
    // provider-driven decode entries the IL InstructionOutputExtensions'
    // member arms and the property header drive (the C# fd.DecodeSignature /
    // md.DecodeSignature / mr.DecodeMethodSignature / mr.DecodeFieldSignature /
    // pd.DecodeSignature family; a property decodes as a METHOD signature
    // over its 0x08-header blob -- the SRM PropertyDefinition.DecodeSignature
    // calls DecodeMethodSignature). The blob is
    // returned raw (header byte included); the caller owns the kind checks.
    // Returns nullopt for an invalid file, an out-of-range row, a nil row, or
    // an unsupported token table; never throws.
    std::optional<std::vector<std::uint8_t>> GetSignatureBlob(
        std::uint32_t entityToken) const;

    // A Field row's (table 0x04) declaring TypeDef's token (0x02) -- the C#
    // FieldDefinition.GetDeclaringType() the member arms render as the
    // `<type>::<name>` prefix. 0 for an invalid file, an out-of-range row, a
    // nil row, or a non-Field token; never throws.
    std::uint32_t GetFieldDeclaringTypeToken(std::uint32_t fieldToken) const;

    // A GenericParam row's (table 0x2A) constraint types from the
    // GenericParamConstraint table (0x1C) -- the raw TypeDefOrRef tokens
    // (0x02/0x01/0x1B) the MethodDefinition arm's constraint block renders.
    // Empty for an invalid file, an unknown token, or a row without
    // constraints; never throws.
    std::vector<std::uint32_t> GetGenericParameterConstraintTokens(
        std::uint32_t genericParamToken) const;

    // The GenericParamConstraint rows (table 0x2C) of a GenericParam row:
    // each row's own token (the attribute parent) and its constraint Type
    // (TypeDefOrRef/TypeSpec) -- the C# `GenericParameter.GetConstraints()`
    // collection the ReflectionDisassembler's WriteGenericParametersAndAttributes
    // walks (the `.param constraint` blocks). Empty for an invalid file, an
    // unknown token, or a row without constraints; never throws.
    std::vector<GenericParamConstraintInfo> GetGenericParameterConstraints(
        std::uint32_t genericParamToken) const;

    // A MethodDef row's (table 0x06) authored Name column -- the identifier
    // the IL InstructionOutputExtensions' MethodDefinition arm escapes. ""
    // for an invalid file, an out-of-range row, a nil row, or a non-MethodDef
    // token; never throws.
    std::string GetMethodName(std::uint32_t methodToken) const;

    // A MethodDef row's (table 0x06) RelativeVirtualAddress column -- the C#
    // MethodDefinition.RelativeVirtualAddress the Disassemble header comments
    // print (0 for abstract/extern/pinvoke-only methods). 0 for an invalid
    // file, an out-of-range row, a nil row, or a non-MethodDef token; never
    // throws.
    std::uint32_t GetMethodRVA(std::uint32_t methodToken) const;

    // A Field row's (table 0x04) authored Name column -- the identifier the
    // FieldDefinition arm escapes. "" for an invalid file, an out-of-range
    // row, a nil row, or a non-Field token; never throws.
    std::string GetFieldName(std::uint32_t fieldToken) const;

    // A Field row's (table 0x04) RelativeVirtualAddress -- the C#
    // `FieldDefinition.GetRelativeVirtualAddress()` the field header's
    // `at <prefix>_<rva>` suffix and the HasFieldRVA initial-value read
    // consume (0 for fields without data; the FieldRVA table row whose Field
    // is this row -- the C# FindFieldRvaRowId scan). 0 for an invalid file, an
    // out-of-range row, a nil row, or a non-Field token; never throws.
    std::uint32_t GetFieldRVA(std::uint32_t fieldToken) const;

    // A Field row's (table 0x04) layout offset -- the C#
    // `FieldDefinition.GetOffset()`: the FieldLayout table row (table 0x10)
    // whose Field is this row, or -1 when the row has none (the C# -1 for
    // an absent row; the header renders the `[offset]` prefix only for
    // offsets > -1). -1 for an invalid file, an out-of-range row, a nil row,
    // or a non-Field token; never throws.
    std::int32_t GetFieldOffset(std::uint32_t fieldToken) const;

    // A Field row's (table 0x04) marshalling-descriptor blob -- the C#
    // `FieldDefinition.GetMarshallingDescriptor()`: the FieldMarshal table
    // row whose HasFieldMarshal coded index (Field tag 0, Param tag 1 -- 1
    // tag bit) points at this row. nullopt when the row has no descriptor
    // (the C# IsNil test); never throws.
    std::optional<std::vector<std::uint8_t>> GetFieldMarshallingDescriptor(
        std::uint32_t fieldToken) const;

    // A TypeDef row's (table 0x02) layout size -- the C#
    // `TypeDefinition.GetLayout().Size`: the ClassLayout table row (table
    // 0x11) whose Parent is this row, ClassSize column, widened from the
    // C# int cast; 0 when the row has no ClassLayout row. 0 for an invalid
    // file, an out-of-range row, a nil row, or a non-TypeDef token; never
    // throws.
    std::uint32_t GetTypeLayoutSize(std::uint32_t typeDefToken) const;

    // A TypeDef row's layout -- the C# `TypeDefinition.GetLayout()` (the
    // System.Reflection.Metadata TypeLayout): the ClassLayout table row
    // (table 0x11) whose Parent is this row, its PackingSize (column 0,
    // widened from uint16) and ClassSize (column 1) columns. The default
    // (both zero -- no ClassLayout row) is the C# `default(TypeLayout)`
    // whose `IsDefault` suppresses the .pack/.size lines; never throws.
    struct TypeLayoutInfo {
        std::int32_t PackingSize = 0;
        std::int32_t ClassSize = 0;
        bool IsDefault() const noexcept { return PackingSize == 0 && ClassSize == 0; }
    };
    TypeLayoutInfo GetTypeLayout(std::uint32_t typeDefToken) const;

    // The PE-section reads the field renderer drives (PeImage passthroughs
    // through the MethodBodyReader): the containing-section index (the C#
    // `MetadataFile.GetContainingSectionIndex(rva)` -- the section whose
    // [VirtualAddress, VirtualAddress + VirtualSize) range contains the
    // RVA; -1 when none or the file is not a PE image) and the section name
    // by index (the 8-byte name field trimmed at the first NUL; "" for an
    // out-of-range index). Never throw.
    int GetContainingSectionIndex(std::uint32_t rva) const;
    std::string GetSectionName(int sectionIndex) const;

    // A HasFieldRVA field's initial value -- the C# SRMExtensions
    // `GetInitialValue(field, pefile, typeSystem: null)` (the
    // ReflectionDisassembler.DisassembleField shape): the field's signature
    // decoded through the FieldValueSizeDecoder (SRMExtensions) for its
    // byte size, then that many bytes of the PE section data at the field's
    // RVA. Fields without the HasFieldRVA flag or with a zero RVA read empty;
    // a size that exceeds the section data, or a non-zero size with no
    // section data at the RVA, throw std::runtime_error carrying the exact
    // C# BadImageFormatException messages (the DisassembleField catch
    // renders them into the `// .data ...` comment). The ICompilation
    // typeSystem arm (the CSharpDecompiler/
    // TransformArrayInitializers callers) defers with the type system -- the
    // null arm is the only CLI shape.
    // A malformed field signature blob throws std::logic_error (the C#
    // BadImageFormatException out of DecodeFieldSignature).
    std::vector<std::uint8_t> GetFieldInitialValue(
        std::uint32_t fieldToken) const;

    // A MemberRef row (table 0x0A): the authored Name column and the
    // MemberRefParent coded index as a raw entity token (tag 0=TypeDef 0x02,
    // 1=TypeRef 0x01, 2=ModuleRef 0x1A, 3=MethodDef 0x06, 4=TypeSpec 0x1B;
    // 0 for a nil row). The signature blob goes through GetSignatureBlob.
    struct MemberRefInfo {
        std::string Name;
        std::uint32_t ParentToken = 0;
        std::uint32_t Token = 0;  // the 0x0A row's own token
    };
    std::optional<MemberRefInfo> GetMemberReference(std::uint32_t token) const;

    // A MethodSpec row (table 0x2B): the MethodDefOrRef coded index as a raw
    // MethodDef (0x06) or MemberRef (0x0A) token. The instantiation blob goes
    // through GetMethodSpecificationInstantiationBlob.
    struct MethodSpecInfo {
        std::uint32_t Token = 0;        // the 0x2B row's own token
        std::uint32_t MethodToken = 0;  // the MethodDefOrRef target; 0 = nil
    };
    std::optional<MethodSpecInfo> GetMethodSpecification(
        std::uint32_t methodSpecToken) const;

    // A MethodSpec row's (table 0x2B) raw instantiation blob -- the
    // SignatureTypeProviderDecoder::DecodeMethodSpecSignature entry consumes
    // it (a compressed count followed by that many type elements).
    std::optional<std::vector<std::uint8_t>> GetMethodSpecificationInstantiationBlob(
        std::uint32_t methodSpecToken) const;

    // A StandaloneSig row's (table 0x11) raw signature blob -- the
    // InstructionOutputExtensions' StandaloneSignature arm kind-switches on
    // the header byte the caller reads itself.
    std::optional<std::vector<std::uint8_t>> GetStandaloneSignatureBlob(
        std::uint32_t token) const;

    // A ModuleRef row's (table 0x1A) authored Name -- the WriteParent
    // ModuleReference arm's "[name]" spelling. nullopt for an invalid file,
    // an out-of-range row, or a non-ModuleRef token; never throws.
    std::optional<std::string> GetModuleReferenceName(std::uint32_t token) const;

    // A MethodDef row's (table 0x06) ImplAttributes column (II.23.1.12) --
    // the C# MethodDefinition.ImplAttributes the DisassembleMethodHeader's
    // cil/managed flag split consumes. 2 bytes widened to uint32 (the BCL
    // enum reading a row widens to int). 0 for an invalid file, an
    // out-of-range row, a nil row, or a non-MethodDef token; never throws.
    std::uint32_t GetMethodImplAttributes(std::uint32_t methodToken) const;

    // The ImplMap row (table 0x1C) whose MemberForwarded is the MethodDef --
    // the C# MethodDefinition.GetImport() (the SRM MethodImport struct:
    // Module, Name, Attributes). The ImportScope column is a plain ModuleRef
    // row index (no coding); the ImportName string is nullopt for a nil
    // column. nullopt when the method has no pinvoke import; never throws.
    struct MethodImportInfo {
        std::uint32_t ModuleRefToken = 0;  // the 0x1A row token; 0 = nil
        std::optional<std::string> Name;  // the ImportName column; nullopt = nil
        std::uint32_t Attributes = 0;     // the raw MappingFlags (MethodImportAttributes)
    };
    std::optional<MethodImportInfo> GetMethodImport(
        std::uint32_t methodToken) const;

    // The MethodImpl rows (table 0x19) whose MethodBody is the MethodDef --
    // the C# handle.GetMethodImplementations(metadata) extension (the
    // declaring type's Class-column range filtered by MethodBody == handle,
    // the ILSpy SRMExtensions implementation). The MethodDeclaration column
    // is a MethodDefOrRef coded index (tag 0=MethodDef 0x06, 1=MemberRef
    // 0x0A); the .override lines render it. Empty for an invalid file, an
    // out-of-range row, a nil row, or a non-MethodDef token; never throws.
    struct MethodImplementationInfo {
        std::uint32_t Token = 0;  // the 0x19 row's own token
        std::uint32_t MethodDeclarationToken = 0;  // the MethodDefOrRef target
    };
    std::vector<MethodImplementationInfo> GetMethodImplementations(
        std::uint32_t methodToken) const;

    // The DeclSecurity rows (table 0x0E) whose Parent is the token -- the C#
    // TypeDefinition/MethodDefinition.GetDeclarativeSecurityAttributes()
    // collection. The Parent column is a HasDeclSecurity coded index (tag
    // 0=TypeDef 0x02, 1=MethodDef 0x06, 2=Assembly 0x20); the Action column
    // is the raw II.23.1.9 DeclarativeSecurityAction the
    // WriteSecurityDeclarations spellings switch on, and PermissionSet is
    // the raw blob (empty for a nil handle -- the C# GetBlobReader over a
    // nil blob renders the empty dump). Empty for an invalid file, an
    // unknown token, or a row without security declarations; never throws.
    struct DeclarativeSecurityInfo {
        std::uint32_t Token = 0;       // the 0x0E row's own token
        std::uint16_t Action = 0;      // the raw DeclarativeSecurityAction
        std::vector<std::uint8_t> PermissionSet;  // the raw blob; empty = nil
    };
    std::vector<DeclarativeSecurityInfo> GetDeclarativeSecurityAttributes(
        std::uint32_t parentToken) const;

    // A property's accessor set -- the C# PropertyDefinition.GetAccessors()
    // (the SRM PropertyAccessors struct): the MethodSemantics rows whose
    // Association is the property. The MethodSemantics column is an EXACT
    // value match in the SRM switch (Getter 0x2 / Setter 0x1 / Other 0x4 --
    // not a bit test; any other value, including combined flags, is
    // ignored), and the LAST matching row wins the getter/setter slots (the
    // C# switch assignment). The Others keep the row order. Nil handles are
    // 0; the accessors of an invalid token are all-nil; never throws.
    struct PropertyAccessorsInfo {
        std::uint32_t GetterToken = 0;   // 0x06000000 | row; 0 = nil
        std::uint32_t SetterToken = 0;   // 0x06000000 | row; 0 = nil
        std::vector<std::uint32_t> OtherTokens;  // 0x06 tokens, row order
    };
    PropertyAccessorsInfo GetPropertyAccessors(
        std::uint32_t propertyToken) const;

    // An event's accessor set -- the C# EventDefinition.GetAccessors() (the
    // SRM EventAccessors struct): the MethodSemantics rows whose Association
    // is the event, with the same exact-value switch (AddOn 0x8 / RemoveOn
    // 0x10 / Fire 0x20 / Other 0x4) and last-row-wins rule. Never throws.
    struct EventAccessorsInfo {
        std::uint32_t AdderToken = 0;    // AddOn; 0x06000000 | row; 0 = nil
        std::uint32_t RemoverToken = 0;  // RemoveOn; 0 = nil
        std::uint32_t RaiserToken = 0;   // Fire; 0 = nil
        std::vector<std::uint32_t> OtherTokens;  // 0x06 tokens, row order
    };
    EventAccessorsInfo GetEventAccessors(std::uint32_t eventToken) const;

    // An Event row's EventType column (the TypeDefOrRef coded index, 2-bit
    // tag: 0=TypeDef 0x02, 1=TypeRef 0x01, 2=TypeSpec 0x1B) -- the C#
    // `eventDefinition.Type` the event header renders through the signature
    // provider. 0 for an invalid file, an out-of-range row, a nil row, or a
    // non-Event token; never throws.
    std::uint32_t GetEventTypeToken(std::uint32_t eventToken) const;

    // Whole-table enumerations for the disassembler paths and the tests:
    // every MemberRef row, every MethodSpec row, and every StandaloneSig row's
    // 0x11 token. Never throws.
    std::vector<MemberRefInfo> MemberRefs() const;
    std::vector<MethodSpecInfo> MethodSpecs() const;
    std::vector<std::uint32_t> StandaloneSignatureTokens() const;

    // Custom attributes applied to an entity (TypeDef/MethodDef/Field/Property
    // token). Returns the attribute type namespace+name for each; never throws.
    std::vector<CustomAttributeInfo> GetCustomAttributes(std::uint32_t entityToken) const;

    // The CustomAttribute row tokens (table 0x0C, in table order) applied to
    // an entity -- the C# `entity.GetCustomAttributes()` collection over any
    // HasCustomAttribute parent (TypeDef, MethodDef, Field, Param,
    // InterfaceImpl, MemberRef, Module, Property, Event, StandAloneSig,
    // ModuleRef, TypeSpec, Assembly, AssemblyRef, File, ExportedType,
    // ManifestResource, GenericParam, GenericParamConstraint, MethodSpec).
    // Empty for an invalid file or a token no row points at; never throws.
    std::vector<std::uint32_t> GetCustomAttributeTokens(
        std::uint32_t entityToken) const;

    // One raw CustomAttribute row by its own token (the C#
    // metadata.GetCustomAttribute(handle)): the constructor token and the
    // value blob. Nullopt for a nil row, an out-of-range row, or a
    // non-CustomAttribute token; never throws.
    std::optional<CustomAttributeRowInfo> GetCustomAttribute(
        std::uint32_t attributeToken) const;

    // Resolve a metadata token to a display string for the IL disassembler and
    // the IL reader's operand resolution. TypeDef/TypeRef -> "Namespace.Type";
    // Field/MethodDef -> "Namespace.Type::Member"; MemberRef (TypeRef/TypeDef
    // parent) -> "Namespace.Type::Member"; MethodSpec unwraps to its underlying
    // MethodDefOrRef (so a generic-instantiation call renders its resolved
    // method name, not the raw token). TypeSpec/StandAloneSig/UserString and
    // out-of-range tokens fall back to the raw hex token. Never throws.
    // `ownerMethodToken` (optional) is the method the operand appears in: a
    // MemberRef-parent TypeSpec's VAR/MVAR bind in that method's scope (D174).
    std::string ResolveTokenToString(std::uint32_t token,
                                     std::uint32_t ownerMethodToken = 0) const;

    // Resolve a TypeDef/TypeRef (or TypeSpec) token to an IType. Returns nullptr
    // for an out-of-range/unsupported token; never throws. Used by the IL reader
    // for castclass/isinst/box/newarr/ldelem type operands.
    // `ownerMethodToken` (optional) is the method the operand appears in: a
    // TypeSpec's VAR/MVAR scope to that method's class/method generic params
    // (e.g. `newarr T` inside a generic method encodes `!!0[`T`]` -- D173).
    ILSpy::Decompiler::TypeSystem::ITypePtr ResolveTypeToken(std::uint32_t token,
                                                             std::uint32_t ownerMethodToken = 0) const;

    // Resolve the declaring type of a method token (MethodDef parent TypeDef,
    // MemberRef parent TypeRef/TypeDef/TypeSpec, MethodSpec unwrapped to its
    // MethodDefOrRef) to an IType. Returns nullptr for an out-of-range or
    // unsupported token; never throws. Used by the IL reader to populate
    // Call::DeclaringType so the nullable-lifting helpers can recognise
    // Nullable<T>.get_HasValue / GetValueOrDefault by KnownTypeCode.
    // `ownerMethodToken` (optional) is the method the call appears in: a
    // MemberRef-parent TypeSpec's VAR/MVAR scope to that method's generic
    // params (e.g. `new List<T>()` renders its TypeSpec arg named -- D174).
    ILSpy::Decompiler::TypeSystem::ITypePtr ResolveMethodDeclaringType(std::uint32_t methodToken,
                                                                       std::uint32_t ownerMethodToken = 0) const;

    // Whether a field token (FieldDef or a field MemberRef) is compiler-generated
    // or declared in a compiler-generated class. Mirrors the C#
    // NRExtensions.IsCompilerGeneratedOrIsInCompilerGeneratedClass: the field's
    // own [CompilerGenerated] custom attribute, or (recursively up the nesting
    // chain) its declaring type's. Used by the IL reader to populate
    // LdFlda/LdsFlda::IsCompilerGeneratedField, the gate the cached-delegate /
    // cached-ReadOnlySpan transforms consult. A cross-assembly TypeRef parent
    // cannot be resolved without the full type system, so only an in-module
    // TypeDef parent is checked (matching the C# which needs the type system for
    // a TypeRef). Returns false for an out-of-range or unsupported token; never
    // throws.
    bool IsFieldCompilerGeneratedOrInCompilerGeneratedClass(std::uint32_t fieldToken) const;

    // The number of generic type arguments a method-spec instantiation supplies.
    // For a MethodSpec token (table 0x2B) reads the Instantiation blob
    // (ECMA-335 II.23.2.15 MethodSpecSig: a 0x0A GENERICINST marker, then a
    // compressed generic-argument count, then that many Type blobs) and returns
    // the count. Returns 0 for a non-MethodSpec token, an out-of-range row, a
    // missing/malformed blob, or a blob whose first byte is not the 0x0A marker;
    // never throws. Used by the IL reader to populate Call::TypeArgumentsCount so
    // a transform can distinguish a generic-instantiation call
    // (e.g. `Activator.CreateInstance<T>()`) from a non-generic overload.
    int GetMethodSpecTypeArgumentCount(std::uint32_t methodToken) const;

    // Decode the method body at `rva` (from a MethodDefInfo::RVA). Returns an
    // invalid MethodBody for abstract/extern methods (RVA 0) or a malformed
    // header; never throws.
    MethodBody GetMethodBody(std::uint32_t rva) const;

    // Decode the signature of a MethodDef (by token, as in MethodDefInfo::Token).
    // Returns std::nullopt if the file is invalid or the token is out of range;
    // never throws.
    std::optional<MethodSignature> GetMethodSignature(std::uint32_t methodToken) const;

    // The constructor/static status of a MethodDef (table 0x06): IsConstructor
    // is true for a .ctor/.cctor carrying the SpecialName|RTSpecialName flag
    // (the C# MetadataMethod.SymbolKind == Constructor, which gates on those
    // flags plus the name); IsStatic is the MethodAttributes Static flag. Returns
    // a default (false/false) MethodDefKindInfo for an out-of-range or unsupported
    // token; never throws. Used by the IL reader to populate
    // ILFunction::IsConstructor / IsStatic, the gate the NullCoalescingTransform
    // hoisted-constructor-argument null-guard fold consults.
    MethodDefKindInfo GetMethodDefKindInfo(std::uint32_t methodToken) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ILSpy::Decompiler::Metadata
