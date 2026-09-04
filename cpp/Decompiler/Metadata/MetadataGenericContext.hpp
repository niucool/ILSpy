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

// A faithful port of ICSharpCode.Decompiler/Metadata/MetadataGenericContext.cs
// (96 lines): the generic-context resolution helper the Disassembler consumes
// (DisassemblerSignatureTypeProvider's !!N/!N type-parameter rendering,
// MethodBodyDisassembler's exception-handler catch types, and
// ReflectionDisassembler's member signatures). A context resolves the authored
// names (and GenericParam row handles) of a declaring type's type parameters
// (!N / VAR) and a method's own type parameters (!!N / MVAR).
//
// The C# struct's four ctors are (MethodDefinitionHandle, MetadataFile),
// (MethodDefinitionHandle, MetadataReader), (TypeDefinitionHandle, MetadataFile),
// and (TypeDefinitionHandle, MetadataReader); the reader-based twins store the
// identical state (the port has no public reader type -- MetadataFile is the
// only metadata entry point), so each handle family collapses to one factory.
// The method factory resolves the declaring TypeDef from the method row, the
// load-bearing ctor behavior: a method context answers BOTH the declaring
// type's !N and the method's own !!N queries, while a type context leaves the
// method nil (its !!N queries always take the nil-handle fallback).
//
// The C# handle returns (GenericParameterHandle) port to 32-bit metadata tokens
// (0x2A table); 0 is the C# nil handle (MetadataTokens.GenericParameterHandle(0)).
// An out-of-range or nil token, and any query against an invalid module, yield
// the C# nil-handle fallbacks (never throws; the C# ctor's GetRow would throw
// for an invalid handle, which the port's never-throw metadata convention
// replaces with the nil context).

#pragma once

#include <cstdint>
#include <string>

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;

struct MetadataGenericContext {
    // The C# method-context ctor family: `new MetadataGenericContext(method,
    // module)` resolves declaringType = metadata.GetMethodDefinition(method).
    // GetDeclaringType() -- the reason a method context also answers the
    // declaring type's !N queries. A nil or out-of-range methodToken yields
    // the nil context (all queries take the fallbacks).
    static MetadataGenericContext ForMethod(std::uint32_t methodToken, const MetadataFile& module);

    // The C# type-context ctor family: `new MetadataGenericContext(declaringType,
    // module)`; method stays nil, so the !!N queries always fall back.
    static MetadataGenericContext ForType(std::uint32_t typeToken, const MetadataFile& module);

    // The C# `default(MetadataGenericContext)` (ReflectionDisassembler's
    // WriteAttributes renders an attribute constructor at the default
    // context): a null module and nil type/method handles -- every query
    // takes the nil-handle fallbacks.
    static MetadataGenericContext Nil();

    // The authored name of the declaring type's Nth type parameter (!N), or
    // index.ToString() when the row is missing (nil declaring type, negative
    // or out-of-range index, or a null module). Returns the raw name string
    // ("" for a nil Name column) -- the identifier escaping and the nil-name
    // Index fallback live in the C# caller
    // (DisassemblerSignatureTypeProvider.WriteTypeParameter), not here.
    std::string GetGenericTypeParameterName(int index) const;

    // The authored name of the method's Nth type parameter (!!N), same shape.
    std::string GetGenericMethodTypeParameterName(int index) const;

    // The Nth type parameter's GenericParam token (table 0x2A), or 0 for the
    // C# nil handle (nil declaring type, negative or out-of-range index, or a
    // null module).
    std::uint32_t GetGenericTypeParameterHandleOrNull(int index) const;

    // The Nth method type parameter's GenericParam token, or 0.
    std::uint32_t GetGenericMethodTypeParameterHandleOrNull(int index) const;

private:
    const MetadataFile* module_ = nullptr;  // the C# MetadataReader? metadata
    std::uint32_t declaringTypeToken_ = 0;   // 0x02 token; 0 = the C# nil handle
    std::uint32_t methodToken_ = 0;          // 0x06 token; 0 = the C# nil handle
};

} // namespace ILSpy::Decompiler::Metadata
