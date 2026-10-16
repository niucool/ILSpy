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

// Port of ICSharpCode.Decompiler/TypeSystem/IVariable.cs (the `IVariable` interface,
// the name/type pair for a local variable or parameter). `IVariable : ISymbol` adds
// the variable's `IType` (`Type`), whether it is a C#-like `const` (`IsConst`), and
// the boxed constant value or parameter default value (`GetConstantValue`). It is
// the base of `IParameter` and the local-variable hierarchy, and a leaf TypeSystem
// dependency toward `TypeSystemAstBuilder` (whose `ConvertConstantValue`/field
// initializer paths call `GetConstantValue`). The C# `interface IVariable : ISymbol`
// ports to a C++ abstract base deriving from `ISymbol` (the established
// interface-to-abstract-base convention, the `ISymbol`/`IType` precedents). The C#
// `new string Name` re-states the `ISymbol.Name` contract (the C# `new` hides rather
// than overrides, but the C++ inherited virtual `Name()` already covers it, so no
// re-declaration is needed); the `Type` accessor returns `const IType&` (a non-null
// reference -- the C# `IType` has no `?`, a variable always has a type); and
// `GetConstantValue` returns `std::any` (the C++ equivalent of `object?`: a
// type-erased boxed value, empty for null -- keeping this TypeSystem-layer header
// free of a cross-layer include of the CSharp/Syntax `PrimitiveValue`).

#pragma once

#include "Decompiler/TypeSystem/ISymbol.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <any>

namespace ILSpy::Decompiler::TypeSystem {

// A variable (name/type pair). `IVariable : ISymbol` adds the variable's `IType`,
// whether it is a C#-like `const`, and the boxed constant value (or default value for
// a parameter) or null. The base of `IParameter` and the local-variable hierarchy;
// the concrete `DefaultVariable`/`DefaultParameter`/... implementations land with the
// rest of the Phase-5 type system.
class IVariable : public ISymbol {
public:
    // The C# `new string Name { get; }` -- re-states the `ISymbol.Name` contract.
    // The C# `new` hides the base member rather than overriding it, but in C++ the
    // inherited virtual `ISymbol::Name()` already covers the same contract, so no
    // re-declaration is needed here (a concrete variable overrides `Name()` once).
    // (inherited: virtual std::string Name() const from ISymbol)

    // The C# `IType Type { get; }` -- the type of the variable (non-null; a variable
    // always has a type). Returns a `const IType&` reference to the owned type.
    virtual const IType& Type() const = 0;

    // The C# `bool IsConst { get; }` -- whether this variable is a C#-like const.
    virtual bool IsConst() const = 0;

    // The C# `object? GetConstantValue(bool throwOnInvalidMetadata = false)` -- the
    // boxed constant value (for a const field) or default value (for a parameter),
    // or null when there is none. Ports to `std::any` (the C++ equivalent of
    // `object?`: a type-erased boxed value, empty for null). `throwOnInvalidMetadata`
    // controls whether to throw on invalid metadata (the C# default is false).
    virtual std::any GetConstantValue(bool throwOnInvalidMetadata = false) const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
