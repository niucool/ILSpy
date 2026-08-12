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

// ECMA-335 signature blob decoding (II.23.2) onto the IType model.
// Method/field/type-specification signatures come in as raw blob bytes (read
// off the winmd tables); malformed blobs report failure, never throw.

#pragma once

#include "Decompiler/Metadata/Ecma335/WinmdInclude.hpp"
#include "Decompiler/Metadata/LocalTypeInfo.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

struct DecodedMethodSignature {
    TypeSystem::ITypePtr ReturnType;
    std::vector<TypeSystem::ITypePtr> ParameterTypes;
    bool IsInstance = false;
    std::uint32_t GenericParameterCount = 0;
};

// Build a known type when the [ns, name, arity] triple is a framework type,
// else an unresolved SimpleType. Shared by the metadata row accessors.
TypeSystem::ITypePtr MakeTypeRef(std::string_view ns, std::string_view name, int arity);

// Build a type reference for a TypeDef row, deriving its TypeKind from the
// row's flags + base type (the same DeriveTypeKind call TypeDefs() makes). A
// known framework type keeps its KnownType; a non-known in-module type gets an
// accurate kind (Delegate/Struct/Enum/...) instead of the Class fallback.
// Used by ResolveMethodDeclaringType (so a delegate constructor's declaring
// type resolves to Kind == Delegate) and by the signature decoder's
// TypeDefOrRef element decoder (so a generic delegate's generic definition
// in a TypeSpec instantiation also carries Kind == Delegate).
TypeSystem::ITypePtr MakeTypeRefFromTypeDef(winmd::reader::TypeDef d);

// Build a type reference for a TypeRef row. A known framework type keeps its
// KnownType; a non-known TypeRef is treated as Unknown (the C# leaves an
// unresolvable cross-assembly TypeRef as UnknownType; MatchDelegateConstruction
// accepts Kind == Unknown, so a cross-assembly delegate still matches).
TypeSystem::ITypePtr MakeTypeRefFromTypeRef(winmd::reader::TypeRef r);

// The authored names of the generic type parameters in scope when decoding a
// method signature. ECMA-335 reference VAR (!N) / MVAR (!!N) indices; their C#
// names live on the owning TypeDef (VAR) / MethodDef (MVAR) GenericParam rows,
// keyed by GenericParam.Number (II.22.20). Populated from the method's owner
// metadata at decode time so a signature like `List<T>.Add(T item)` decodes its
// parameter to a TypeParameter named "T".
struct GenericParamNames {
    std::vector<std::string> classNames;   // VAR (!N) -> Nth entry
    std::vector<std::string> methodNames;  // MVAR (!!N) -> Nth entry
};

// Decode a method signature blob (MethodDef Signature, MemberRef Signature,
// or the definition behind a MethodSpec). ok is set false on any malformed
// content. `genericNames`, when non-null, is consulted to name VAR/MVAR type
// parameters (default: positional fallback named "!N"/"!!N").
DecodedMethodSignature DecodeMethodSignatureBlob(const winmd::reader::database& db,
                                                 const std::uint8_t* data, std::size_t size,
                                                 bool& ok,
                                                 const GenericParamNames* genericNames = nullptr);

// Decode a field signature blob (0x06 marker + type).
TypeSystem::ITypePtr DecodeFieldSignatureBlob(const winmd::reader::database& db,
                                              const std::uint8_t* data, std::size_t size);

// Decode a TypeSpec signature blob (the content type: array, instantiation,
// by-ref, ...).
TypeSystem::ITypePtr DecodeTypeSpecBlob(const winmd::reader::database& db,
                                        const std::uint8_t* data, std::size_t size);

// Decode a MethodSpec Instantiation blob (ECMA-335 II.23.2.15 MethodSpecSig:
// a 0x0A GENERICINST marker, then a compressed generic-argument count, then
// that many Type blobs) and return the generic-argument count. Returns -1 for
// a malformed blob or one whose first byte is not the 0x0A marker.
int DecodeMethodSpecTypeArgCount(const winmd::reader::database& db,
                                 const std::uint8_t* data, std::size_t size);

// Decode a LOCAL_SIG blob (0x07 marker + count + types) -- the local-variable
// signature referenced by a method body's fat header. Returns the local types
// (with their pinned flag) in index order; an empty/partial vector on a
// malformed blob.
std::vector<LocalTypeInfo> DecodeLocalSignatureBlob(
    const winmd::reader::database& db,
    const std::uint8_t* data, std::size_t size);

} // namespace ILSpy::Decompiler::Metadata
