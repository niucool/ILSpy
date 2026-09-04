// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/DefaultAttribute.cs -- the
// `IAttribute` implementation for already-resolved attributes: an attribute whose type
// and argument values are known without decoding a metadata blob (the constructor the
// `SyntheticWpfModule` XmlnsDefinitionAttribute reconstruction uses, and the shape the
// `CustomAttributeDecoder` materializes blob-decoded attributes into).
//
// KEY PORT CONVENTIONS:
//  (a) The C# `IType attributeType` / `IMethod constructor` non-null ctor guards
//      (`ArgumentNullException`) port to `assert` (the D424 assert-then-move convention;
//      the IAttribute stub-ctor assert precedent in IAttribute_Test.cpp).
//  (b) The C# `ImmutableArray<CustomAttributeTypedArgument<IType>> FixedArguments` /
//      `ImmutableArray<CustomAttributeNamedArgument<IType>> NamedArguments` (readonly
//      properties backed by ctor-stored fields) port to `std::vector` members returned
//      by value (the `IAttribute::FixedArguments` by-value-snapshot convention, D385).
//  (c) The C# `volatile IMethod constructor` lazy field (null until the first
//      `Constructor` read resolves it against `AttributeType.GetConstructors`) ports to
//      a `mutable const IMethod*` written under the const accessor (the CSharpConversions
//      const-lazy-cache const_cast/mutable precedent). Non-owning: the type system owns
//      the methods (the "caller holds raw pointers" convention).
//  (d) The C# ctor overloads: `(IType, fixedArguments, namedArguments)` and
//      `(IMethod, fixedArguments, namedArguments)`. The second derives the attribute type
//      from `constructor.DeclaringType ?? SpecialType.UnknownType` and validates
//      `fixedArguments.Length == constructor.Parameters.Count` with an
//      `ArgumentException` ("Positional argument count must match the constructor's
//      parameter count") -- mapped to `std::invalid_argument` carrying the exact message
//      (the D471 exception-mapping convention).
//  (e) The C# lazy `Constructor` getter: scans `AttributeType.GetConstructors(m =>
//      m.Parameters.Count == FixedArguments.Length)` for the first candidate whose
//      parameter types `SequenceEqual` the fixed-argument types (EqualityComparer<IType>.Default
//      -> `IType::Equals`, reference equality for AbstractType-derived types), caches it
//      (null when none matches -- the nullable-`Constructor` contract IAttribute_Test.cpp
//      pins), and returns it. A null fixed-argument type never matches (the C#
//      `p.Type.Equals(a.Type)` with a null argument is simply false, not an NRE).
//  (f) `bool IAttribute.HasDecodeErrors => false` -- the class never carries decode
//      errors (the values were already resolved).
//
// The class is NOT `final` (the C# is unsealed).

#pragma once

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"

#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

// Forward declaration of `IMethod` (D389, already ported): the `Constructor` nullable
// return and the second ctor's parameter only mention it as a pointer, and keeping the
// include graph minimal preserves include-graph isolation (the `IAttribute::Constructor`
// forward-declaration precedent).
class IMethod;

namespace Implementation {

class DefaultAttribute : public IAttribute {
public:
    // The C# `DefaultAttribute(IType attributeType, ImmutableArray<...> fixedArguments,
    // ImmutableArray<...> namedArguments)` -- the already-resolved shape. Asserts the
    // type non-null (the C# `ArgumentNullException`, convention (a)).
    DefaultAttribute(ITypePtr attributeType,
                     std::vector<CustomAttributeTypedArgument> fixedArguments,
                     std::vector<CustomAttributeNamedArgument> namedArguments);

    // The C# `DefaultAttribute(IMethod constructor, ImmutableArray<...> fixedArguments,
    // ImmutableArray<...> namedArguments)` -- derives the attribute type from the
    // constructor's declaring type (the `SpecialType.UnknownType` fallback when null)
    // and validates the positional-argument count against the constructor's parameter
    // count (`std::invalid_argument` with the exact C# message, convention (d)).
    DefaultAttribute(const IMethod* constructor,
                     std::vector<CustomAttributeTypedArgument> fixedArguments,
                     std::vector<CustomAttributeNamedArgument> namedArguments);

    // The C# `IType AttributeType { get; }` -- non-null (the ctor asserts it).
    const IType& AttributeType() const override;

    // The C# `IMethod? Constructor { get; }` -- the constructor the attribute applied,
    // resolved lazily against `AttributeType.GetConstructors` on the first read and
    // cached; null when no matching constructor was found (conventions (c)/(e)).
    const IMethod* Constructor() const override;

    // The C# `bool IAttribute.HasDecodeErrors => false`.
    bool HasDecodeErrors() const override { return false; }

    // The C# `ImmutableArray<CustomAttributeTypedArgument<IType>> FixedArguments` -- the
    // positional arguments, by value (convention (b)).
    std::vector<CustomAttributeTypedArgument> FixedArguments() const override
    {
        return fixedArguments_;
    }

    // The C# `ImmutableArray<CustomAttributeNamedArgument<IType>> NamedArguments` -- the
    // named arguments, by value (convention (b)).
    std::vector<CustomAttributeNamedArgument> NamedArguments() const override
    {
        return namedArguments_;
    }

private:
    // The resolved attribute type (non-null; the ctor asserts). A shared, cached handle
    // (the D271 `ITypePtr` convention).
    ITypePtr attributeType_;
    // The lazily-resolved constructor (null until the first `Constructor()` read; the
    // type system owns the method -- non-owning). `mutable` for the const lazy getter
    // (convention (c)).
    mutable const IMethod* constructor_ = nullptr;
    // The positional / named argument snapshots (convention (b)).
    std::vector<CustomAttributeTypedArgument> fixedArguments_;
    std::vector<CustomAttributeNamedArgument> namedArguments_;
};

} // namespace Implementation
} // namespace ILSpy::Decompiler::TypeSystem
