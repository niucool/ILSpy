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

// Internal: bridges the vendored microsoft/winmd signature decoders
// (TypeSig, MethodDefSig, ...) into the port's IType hierarchy. Not part of the
// public metadata surface; included only by MetadataFile.cpp. Includes winmd,
// so it pulls in <windows.h> on Windows -- keep it out of public headers.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include "Decompiler/Metadata/Ecma335/WinmdInclude.hpp"

#include <cstdint>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

// A decoded method signature: the return type, parameter types, whether the
// signature has an implicit `this` parameter, and the generic parameter count.
struct DecodedMethodSignature {
    TypeSystem::ITypePtr ReturnType;
    std::vector<TypeSystem::ITypePtr> ParameterTypes;
    bool IsInstance = false;
    std::uint32_t GenericParameterCount = 0;
};

// Decode a TypeSig into an IType. Handles the ECMA-335 ELEMENT_TYPE_* set:
// primitives -> KnownType, Class/ValueType -> resolved SimpleType (or KnownType
// if the name matches), GenericInst -> ParameterizedType, SZArray/Array ->
// ArrayType, ByRef (via Param/RetType) handled by the caller, Var/MVar ->
// TypeParameter, Ptr -> PointerType. Unknown/unsupported elements fall back to
// the UnknownType null object rather than throwing.
TypeSystem::ITypePtr DecodeType(const winmd::reader::database& db,
                                const winmd::reader::TypeSig& sig);

// Resolve a TypeDefOrRef coded index to an IType. TypeDef/TypeRef become a
// SimpleType (or KnownType if the name matches a known type); TypeSpec decodes
// its signature blob recursively.
TypeSystem::ITypePtr ResolveTypeDefOrRef(
    const winmd::reader::database& db,
    winmd::reader::coded_index<winmd::reader::TypeDefOrRef> cod);

// Decode a MethodDefSig into a DecodedMethodSignature.
DecodedMethodSignature DecodeMethodSignature(
    const winmd::reader::database& db,
    const winmd::reader::MethodDefSig& sig);

// Decode a FieldSig into its field type.
TypeSystem::ITypePtr DecodeFieldSignature(
    const winmd::reader::database& db,
    const winmd::reader::FieldSig& sig);

} // namespace ILSpy::Decompiler::Metadata
