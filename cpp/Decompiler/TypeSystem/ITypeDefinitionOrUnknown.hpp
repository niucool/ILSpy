// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/TypeSystem/ITypeDefinitionOrUnknown.cs.
// ITypeDefinitionOrUnknown : IType is the base of ITypeDefinition (a resolved
// type definition) and the UnknownType placeholder (an unresolved type). It adds
// the FullTypeName accessor -- the full name of the type definition: a
// TopLevelTypeName plus zero or more nested-type segments, the reflection-name
// form that uniquely identifies a type definition within an assembly. The
// existing IType minimal port (IType.hpp) carries Name()/ReflectionName() but
// not FullTypeName(); this intermediate base is the first piece of the
// ITypeDefinition hierarchy toward IModule/IEntity/TypeSystemAstBuilder/CSharpAmbience.
// The existing IType concrete kinds (KnownType, SimpleType, ...) derive from
// IType only and do NOT implement FullTypeName -- they are not type definitions.

#pragma once

#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::TypeSystem {

// The base of ITypeDefinition and UnknownType: an IType that carries a full
// type name. ITypeDefinition (not yet ported) derives from this plus IType plus
// IEntity; the FullTypeName accessor is the sole member this intermediate adds
// over IType. Returned by const reference because the concrete implementation
// stores the name as a member (the SimpleType::GetTopLevelTypeName() const-
// reference precedent in IType.hpp); the C# property returns by value, but a
// FullTypeName holds a vector so a reference avoids the copy.
class ITypeDefinitionOrUnknown : public IType {
public:
    // The full name of this type definition: a top-level name plus zero or more
    // nested-type segments (e.g. "System.Collections.Generic.Dictionary`2+Enumerator").
    virtual const FullTypeName& FullTypeName() const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
