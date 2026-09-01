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

// A GenericParam row (table 0x2A): its metadata token, the ECMA Number column
// (the parameter's authored zero-based position -- SRM's GenericParameter.Index,
// which the disassembler's WriteTypeParameter nil-name fallback reads), and the
// authored Name ("" when the row's Name column is nil -- SRM GetString(nil) is
// the empty string). The direct prerequisite MetadataGenericContext consumes.
struct GenericParameterInfo {
    std::uint32_t Token;    // 0x2A000000 | row (1-based)
    std::uint16_t Number;   // the Number column
    std::string Name;
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

// A custom attribute applied to an entity: the attribute type's namespace and
// name (e.g. "System", "SerializableAttribute"). Constructor/named-argument
// decoding is deferred to a later phase; the name is enough for the type
// system's attribute checks ([Serializable], [Obsolete], [Extension], ...).
struct CustomAttributeInfo {
    std::string Namespace;
    std::string Name;
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

    // Parameter names from the Param table for a MethodDef token (table 0x06).
    // Index 0 is the first declared parameter (after any implicit `this`); the
    // vector may be shorter than the parameter count or hold empty strings for
    // parameters that have no Param row.
    std::vector<std::string> GetParameterNames(std::uint32_t methodToken) const;

    // Decode a user-string token (table 0x70, the #US heap) to its text.
    // Returns an empty string if the heap is absent or the offset is bad.
    std::string GetUserString(std::uint32_t token) const;

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

    // A TypeRef row's (table 0x01) authored TypeName/TypeNamespace strings plus
    // the declaring TypeRef's token -- the per-row reads the SRMExtensions
    // GetFullTypeName(TypeReferenceHandle) reader composes (the SRM
    // TypeReference.Name/Namespace columns and the GetDeclaringType()
    // resolution-scope walk: a TypeRef-scoped row nests inside that TypeRef,
    // any other scope -- Module/ModuleRef/AssemblyRef -- is top-level).
    // Returns nullopt for an invalid file, an out-of-range row, a nil row, or
    // a non-TypeRef token; never throws.
    std::optional<TypeRefNameInfo> GetTypeRefNameInfo(std::uint32_t typeRefToken) const;

    // Custom attributes applied to an entity (TypeDef/MethodDef/Field/Property
    // token). Returns the attribute type namespace+name for each; never throws.
    std::vector<CustomAttributeInfo> GetCustomAttributes(std::uint32_t entityToken) const;

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
