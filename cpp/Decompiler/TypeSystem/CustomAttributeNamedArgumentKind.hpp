// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the BCL `System.Reflection.Metadata.CustomAttributeNamedArgumentKind` enum, the
// one-byte tag that distinguishes a named attribute argument that sets a field from one
// that sets a property (ECMA-335 II.23.3 / II.23.1.16: the named-argument kind byte is a
// `SerializationTypeCode`, `Field` = 0x53 or `Property` = 0x54). The
// `CustomAttributeDecoder.DecodeNamedArguments` reads the kind byte and casts it to this
// enum, then rejects any value that is neither `Field` nor `Property`
// (`ICSharpCode.Decompiler/Metadata/CustomAttributeDecoder.cs` line 39). The consumer
// (`TypeSystemAstBuilder.ConvertAttribute`) builds a `NamedExpression` for each named
// argument; the `MemberForNamedArgument` lookup consults the kind to pick the field vs the
// property of the attribute type.
//
// KEY PORT CONVENTIONS:
//  (a) The BCL enum lives in `System.Reflection.Metadata`, but the C++ port has no
//      `System::Reflection::Metadata` namespace (no BCL-namespace fidelity for helper
//      types), and the value-argument structs that carry it (`CustomAttributeNamedArgument`)
//      live in `ILSpy::Decompiler::TypeSystem`; the faithful port therefore places the enum
//      in `ILSpy::Decompiler::TypeSystem` -- the same convention the D384
//      `MethodSemanticsAttributes` port followed (the BCL
//      `System.Reflection.MethodSemanticsAttributes` enum was absorbed into the
//      `TypeSystem` namespace where its consumer `IMethod.AccessorKind` lives, and the
//      D381 `IEntity.MetadataToken` port folded the `System.Reflection.Metadata.EntityHandle`
//      BCL value struct into a raw `std::uint32_t` in the `TypeSystem` namespace). BCL
//      helper types are absorbed into the `TypeSystem` namespace where their consumers
//      live, not re-homed under a BCL-namespace mirror.
//  (b) The C# `: byte` underlying type ports to `std::uint8_t` (the kind is a single
//      serialization byte); the member order and values mirror the ECMA-335 / BCL
//      literals exactly (each member's numeric value is its serialization code, so a
//      raw byte read from the metadata blob casts directly to the enum).
//  (c) NO `[Flags]` bitwise operators are needed (the enum is a closed two-value tag, never
//      combined or masked), so -- unlike `MethodSemanticsAttributes` (D384) /
//      `TypeSystemOptions` (D376) -- only the `enum class` itself is ported. The C++ enum
//      class's built-in `==`/`!=` suffices for the consumer's `kind == Field` /
//      `kind == Property` tests.

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

// The one-byte tag distinguishing a named attribute argument that sets a field from one
// that sets a property (ECMA-335 II.23.3). The values are the `SerializationTypeCode`
// literals the metadata reader casts a raw kind byte to.
enum class CustomAttributeNamedArgumentKind : std::uint8_t {
    // ECMA-335 II.23.1.16 `SERIALIZATION_TYPE_FIELD` (0x53): the named argument sets a
    // field of the attribute type.
    Field = 0x53,
    // ECMA-335 II.23.1.16 `SERIALIZATION_TYPE_PROPERTY` (0x54): the named argument sets a
    // property of the attribute type.
    Property = 0x54,
};

} // namespace ILSpy::Decompiler::TypeSystem
