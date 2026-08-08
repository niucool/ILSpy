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

// A decoded method signature: resolved return and parameter types, whether
// the method has an implicit `this` parameter, and the generic parameter count.
struct MethodSignature {
    ILSpy::Decompiler::TypeSystem::ITypePtr ReturnType;
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> ParameterTypes;
    bool IsInstance = false;
    std::uint32_t GenericParameterCount = 0;
};

// A TypeDef row: name, namespace, metadata token (table 0x02), and the resolved
// base type (nullptr for System.Object and interfaces with no base).
struct TypeDefInfo {
    std::string Name;
    std::string Namespace;
    std::uint32_t Token;
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

    // Up to `n` TypeDef type names (the short name, e.g. "Object"), in table
    // order. Returns fewer than `n` if the table is smaller.
    std::vector<std::string> TopTypeNames(std::size_t n) const;

    // All MethodDef rows in table order.
    std::vector<MethodDefInfo> MethodDefs() const;

    // All TypeDef rows in table order, with resolved base types.
    std::vector<TypeDefInfo> TypeDefs() const;

    // Members of a TypeDef (by token): methods, fields, and properties, in row
    // order. Empty vectors for an invalid/out-of-range token.
    std::vector<MethodInfo> GetMethods(std::uint32_t typeToken) const;
    std::vector<FieldInfo> GetFields(std::uint32_t typeToken) const;
    std::vector<PropertyInfo> GetProperties(std::uint32_t typeToken) const;

    // Decode the field type of a Field row (by token). Returns nullptr if the
    // token is out of range or the signature is malformed; never throws.
    ILSpy::Decompiler::TypeSystem::ITypePtr GetFieldSignature(std::uint32_t fieldToken) const;

    // Decode the method body at `rva` (from a MethodDefInfo::RVA). Returns an
    // invalid MethodBody for abstract/extern methods (RVA 0) or a malformed
    // header; never throws.
    MethodBody GetMethodBody(std::uint32_t rva) const;

    // Decode the signature of a MethodDef (by token, as in MethodDefInfo::Token).
    // Returns std::nullopt if the file is invalid or the token is out of range;
    // never throws.
    std::optional<MethodSignature> GetMethodSignature(std::uint32_t methodToken) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ILSpy::Decompiler::Metadata
