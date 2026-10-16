// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/NRExtensions.cs -- the VB/C# compiler-generated
// type and anonymous-type predicate family (the root-namespace extension class).
// The C# class sits in the `ICSharpCode.Decompiler` root namespace and its
// members are consumed as extension methods over IType/ITypeDefinition; the port
// carries them as free functions in the `ILSpy::Decompiler` root namespace (the
// DecompilerSettings placement convention for root-namespace C# files), so the
// C#-namespace call sites resolve the unqualified names through the enclosing
// namespace chain.
//
// Landed: IsCompilerGenerated, HasGeneratedName (the IMember and IType
// overloads), HasOnlyReadOnlyProperties (the C# private helper), and the
// anonymous-type predicate family IsAnonymousTypeDeclaredAsNamedType /
// IsAnonymousType / ContainsAnonymousType -- the predicates CallBuilder's
// anonymous-type arms (PinTypesOfNullArguments / NewAnonymousTypeInstance /
// CastArguments / the RequireTypeArguments block) consult.
//
// DEFERRED with their consumers: GetDocumentation (needs the XmlDocLoader
// machinery) and GetMetadataAttributes (needs the parent module's
// MetadataFile GetTypeDefinition read -- lands with a metadata-reading
// consumer).
#pragma once

#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include <string>

namespace ILSpy::Decompiler {

// The C# `public static bool IsCompilerGenerated(this IEntity entity)`
// (NRExtensions.cs line 28): the entity carries the
// [CompilerGenerated] attribute (a null entity answers false -- the C#
// `if (entity != null)` guard).
bool IsCompilerGenerated(const TypeSystem::IEntity* entity);

// The C# `public static bool HasGeneratedName(this IMember member)`
// (line 46): the member name starts with '<' (the C# ordinal StartsWith).
bool HasGeneratedName(const TypeSystem::IMember& member);

// The C# `public static bool HasGeneratedName(this IType type)`
// (line 51): the type's name passes SRMExtensions.IsGeneratedName (a '<'
// prefix or a '$' separator).
bool HasGeneratedName(const TypeSystem::IType& type);

// The C# `static bool HasOnlyReadOnlyProperties(ITypeDefinition type)`
// (line 62): every property of the type refuses to be set (a C# anonymous
// type is immutable and compares all of its members, so only an anonymous
// type with no settable property can be written as one). The C# is private;
// the port's no-visibility-level convention keeps it public with this
// documented note.
bool HasOnlyReadOnlyProperties(const TypeSystem::ITypeDefinition& type);

// The C# `public static bool IsAnonymousTypeDeclaredAsNamedType(
// this ITypeDefinition type)` (line 76): a VB anonymous type that keeps its
// own declaration because it cannot be written as a C# anonymous type (a
// VB anonymous type with at least one settable, non-'Key' property).
bool IsAnonymousTypeDeclaredAsNamedType(const TypeSystem::ITypeDefinition& type);

// The C# `public static bool IsAnonymousType(this IType type)`
// (line 86): the compiler-generated anonymous type shape -- an empty
// namespace, a generated name (`<`-prefixed or `$`-containing) containing
// "AnonType" or "AnonymousType", backed by a definition that is
// [CompilerGenerated] and has only read-only properties. A null type
// answers false (the C# `if (type == null)` guard).
bool IsAnonymousType(const TypeSystem::IType* type);

// The C# `public static bool ContainsAnonymousType(this IType type)`
// (line 99): whether the type or any of its type arguments / nested
// element types is an anonymous type -- the ContainsAnonTypeVisitor walk
// over the type tree. The visitor takes a NON-CONST IType& (the port's
// visitor contract; the walk never mutates anything), so the parameter is
// a const reference that the implementation casts away.
bool ContainsAnonymousType(const TypeSystem::IType& type);

} // namespace ILSpy::Decompiler
