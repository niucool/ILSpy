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

// Port of the BCL `System.Reflection.Metadata.CustomAttributeTypedArgument<TType>` readonly
// struct, instantiated for `IType` -- one positional (fixed) argument of a custom attribute.
// The `CustomAttributeDecoder.DecodeArgument` builds these from the attribute blob, and
// `IAttribute.FixedArguments` holds the `ImmutableArray<CustomAttributeTypedArgument<IType>>`
// of positional arguments; `TypeSystemAstBuilder.ConvertAttribute` reads `arg.Type` /
// `arg.Value` for each fixed argument to build the attribute's positional-argument
// expressions (TypeSystemAstBuilder.cs lines 791-797). The struct carries the argument's
// `Type` (the decoded `IType`, non-null) and its boxed `Value` (the `object?` -- a primitive,
// a string, a type, an enum, a boxed nested `CustomAttributeTypedArgument`, or an array).
//
// KEY PORT CONVENTIONS:
//  (a) The BCL struct is generic (`CustomAttributeTypedArgument<TType>`); the ILSpy type
//      system uses only the `IType` instantiation (the `CustomAttributeDecoder<TType>` in
//      the Metadata layer is generic, but that layer is not yet ported, and the TypeSystem
//      surface -- `IAttribute` and its consumers -- needs only `IType`). The faithful port
//      therefore absorbs the `<IType>` instantiation into the `TypeSystem` namespace as a
//      concrete (non-template) struct, the same convention the D384 `MethodSemanticsAttributes`
//      / D381 `IEntity.MetadataToken` ports followed for BCL helper types, and the concrete-
//      struct convention the `TypeConstraint` (D383) / `LifetimeAnnotation` (D382) /
//      `FullTypeName` / `TopLevelTypeName` TypeSystem value types established. A future
//      Metadata-layer port that needs the generic decoder can refactor or add a template then.
//  (b) The C# `TType Type` (non-null for a decoded argument) ports to `ITypePtr` (the D271
//      `std::shared_ptr<IType>` shared, cached handle -- the `TypeConstraint.Type` D383
//      precedent); a null `shared_ptr` is the faithful representation of an argument whose
//      type could not be decoded (the default-constructed value, the
//      `ImmutableArray.CreateBuilder` zero-initialization the decoder overwrites).
//  (c) The C# `object? Value` ports to `std::any` (the D374 `IVariable::GetConstantValue`
//      precedent -- `std::any` is the type-erased boxed value, empty for `null`). A
//      `std::any` holds any copy-constructible value, so it models the full BCL `object?`
//      range the decoder produces -- a primitive (`bool`/`int32_t`/...), a `std::string`,
//      an `ITypePtr`, an enum, a boxed nested `CustomAttributeTypedArgument` (the
//      `new CustomAttributeTypedArgument<TType>(outer.Type, new
//      CustomAttributeTypedArgument<TType>(info.Type, value))` boxed-value case, line 196),
//      or a `std::vector<CustomAttributeTypedArgument>` (the array case). Consumers retrieve
//      a value with `std::any_cast` (the C# `object` cast) and test with `has_value` (the C#
//      `null` test).
//  (d) The C# `readonly struct` (a value type with a ctor and readonly properties) ports to
//      a `struct` with private fields (the `_` suffix) and public accessor member functions
//      (the `TypeConstraint` D383 readonly-struct convention); the ctor stores the type and
//      the boxed value (the C# `CustomAttributeTypedArgument(TType type, object? value)`).
//      A defaulted default constructor is provided (the C# implicit parameterless struct ctor
//      that zeroes the fields) so a `std::vector<CustomAttributeTypedArgument>` can default-
//      construct its elements (the `ImmutableArray.CreateBuilder<T>(count)` zero-fill the
//      decoder relies on); the member initializers make the default a null type and an empty
//      value.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

#include <any>
#include <utility>

namespace ILSpy::Decompiler::TypeSystem {

// One positional (fixed) argument of a custom attribute: the decoded `Type` (non-null for a
// decoded argument) and the boxed `Value` (a primitive / string / type / enum / boxed nested
// argument / array, or empty for `null`). A value type (the C# `readonly struct`); default
// construction gives a null type and an empty value (the decoder's zero-fill sentinel).
struct CustomAttributeTypedArgument {
    // The C# `CustomAttributeTypedArgument(TType type, object? value)`. The type may be a
    // null `shared_ptr` for an argument whose type could not be decoded (the decoder does
    // not throw on a null type -- it records it); the value may be empty (the C# `null`).
    CustomAttributeTypedArgument(ITypePtr type, std::any value)
        : type_(std::move(type)), value_(std::move(value))
    {
    }

    // The C# implicit parameterless struct ctor (zeroes the fields). A null `ITypePtr` and
    // an empty `std::any` (the decoder's zero-fill sentinel, overwritten before use).
    CustomAttributeTypedArgument() = default;

    // The C# `TType Type { get; }` -- the decoded argument type. A nullable `ITypePtr` (the
    // shared, cached `IType` handle); null for an argument whose type could not be decoded.
    ITypePtr Type() const { return type_; }

    // The C# `object? Value { get; }` -- the boxed argument value. A `std::any` (the
    // type-erased boxed value); empty for `null`. Retrieve with `std::any_cast`, test with
    // `has_value`.
    std::any Value() const { return value_; }

private:
    ITypePtr type_;
    std::any value_;
};

} // namespace ILSpy::Decompiler::TypeSystem
