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

// Port of the BCL `System.Reflection.Metadata.CustomAttributeNamedArgument<TType>` readonly
// struct, instantiated for `IType` -- one named argument of a custom attribute (a field or
// property setter). The `CustomAttributeDecoder.DecodeNamedArguments` builds these from the
// attribute blob, and `IAttribute.NamedArguments` holds the
// `ImmutableArray<CustomAttributeNamedArgument<IType>>` of named arguments;
// `TypeSystemAstBuilder.ConvertAttribute` reads `namedArg.Name` / `namedArg.Type` /
// `namedArg.Value` for each named argument to build the `NamedExpression` arguments
// (TypeSystemAstBuilder.cs lines 797-813). The struct carries the member `Name` (the field
// or property name), the `Kind` (field vs property, the `CustomAttributeNamedArgumentKind`
// tag), the argument `Type` (the decoded `IType`, non-null), and its boxed `Value`.
//
// KEY PORT CONVENTIONS:
//  (a) Like `CustomAttributeTypedArgument`, the BCL struct is generic
//      (`CustomAttributeNamedArgument<TType>`) with two instantiations in the repo (the
//      ILSpy type system's `IType` and the ReflectionDisassembler
//      SecurityDeclarationDecoder's `(PrimitiveTypeCode, string)` tuple), so the port
//      carries the template (`CustomAttributeNamedArgumentT<TType>`) plus the
//      `CustomAttributeNamedArgument` alias for the `ITypePtr` instantiation the
//      TypeSystem surface uses (the `...T`-suffix-plus-alias convention, the
//      `CustomAttributeTypedArgumentT` precedent).
//  (b) The C# `string? Name` (nullable -- `CustomAttributeDecoder.DecodeNamedArguments` reads
//      it from `valueReader.ReadSerializedString()`, which returns null for a malformed blob)
//      ports to `std::string` where the empty string represents the null case (the D357
//      `AttributeTarget` `IsNullOrEmpty`-to-`.empty()` convention -- the consumer
//      `TypeSystemAstBuilder` uses `namedArg.Name!` (null-forgiving), treating the name as
//      non-null in practice for well-formed attributes, so the null and empty cases collapse
//      to the empty string without loss for the consumer). The default-constructed value is
//      the empty string.
//  (c) The C# `CustomAttributeNamedArgumentKind Kind` ports to the ported
//      `CustomAttributeNamedArgumentKind` enum (a `Field`/`Property` tag); the
//      default-constructed value is `Field` (the lower serialization code, a safe default
//      the decoder overwrites before use).
//  (d) The C# `TType Type` (non-null) ports to the `TType` template parameter (the
//      `CustomAttributeTypedArgumentT.Type` convention); the C# `object? Value` ports to
//      `std::any` (the `CustomAttributeTypedArgumentT.Value` convention -- a type-erased
//      boxed value, empty for `null`).
//  (e) The C# `readonly struct` (a value type with a ctor and readonly properties) ports to
//      a `struct` with private fields and public accessor member functions (the `TypeConstraint`
//      / `CustomAttributeTypedArgument` readonly-struct convention); the ctor stores all four
//      members (the C# `CustomAttributeNamedArgument(string? name,
//      CustomAttributeNamedArgumentKind kind, TType type, object? value)`). A defaulted default
//      constructor is provided (the C# implicit parameterless struct ctor) so a
//      `std::vector<CustomAttributeNamedArgument>` can default-construct its elements (the
//      `ImmutableArray.CreateBuilder<T>(count)` zero-fill); the member initializers make the
//      default an empty name, `Field`, a null type, and an empty value.

#pragma once

#include "Decompiler/TypeSystem/CustomAttributeNamedArgumentKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <any>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// One named argument of a custom attribute: the member `Name` (field or property), the
// `Kind` (field vs property), the argument `Type` (non-null for a decoded argument), and
// the boxed `Value` (or empty for `null`). A value type (the C# `readonly struct`); default
// construction gives an empty name, `Field`, a null type, and an empty value.
template <typename TType>
struct CustomAttributeNamedArgumentT {
    // The C# `CustomAttributeNamedArgument(string? name, CustomAttributeNamedArgumentKind
    // kind, TType type, object? value)`. The name may be empty (the C# null/malformed case);
    // the type may be a null `shared_ptr`; the value may be empty (the C# `null`).
    CustomAttributeNamedArgumentT(std::string name, CustomAttributeNamedArgumentKind kind,
        TType type, std::any value)
        : name_(std::move(name)), kind_(kind), type_(std::move(type)), value_(std::move(value))
    {
    }

    // The C# implicit parameterless struct ctor (zeroes the fields). An empty name, `Field`,
    // a default-constructed `TType` (a null `ITypePtr` for the IType instantiation), and an
    // empty `std::any` (the decoder's zero-fill sentinel).
    CustomAttributeNamedArgumentT() = default;

    // The C# `string? Name { get; }` -- the field or property name. An empty string for the
    // C# null/malformed case (the consumer treats the name as non-null via `Name!`).
    std::string Name() const { return name_; }

    // The C# `CustomAttributeNamedArgumentKind Kind { get; }` -- the field-vs-property tag.
    CustomAttributeNamedArgumentKind Kind() const { return kind_; }

    // The C# `TType Type { get; }` -- the argument type. A nullable `ITypePtr` (the shared,
    // cached `IType` handle) for the IType instantiation; null for an argument whose type
    // could not be decoded.
    TType Type() const { return type_; }

    // The C# `object? Value { get; }` -- the boxed argument value. A `std::any` (the
    // type-erased boxed value); empty for `null`. Retrieve with `std::any_cast`, test with
    // `has_value`.
    std::any Value() const { return value_; }

private:
    std::string name_;
    CustomAttributeNamedArgumentKind kind_ = CustomAttributeNamedArgumentKind::Field;
    TType type_{};
    std::any value_;
};

// The `ITypePtr` instantiation the TypeSystem surface uses (convention (a)).
using CustomAttributeNamedArgument = CustomAttributeNamedArgumentT<ITypePtr>;

} // namespace ILSpy::Decompiler::TypeSystem
