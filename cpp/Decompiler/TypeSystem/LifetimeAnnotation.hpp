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
// OTHERWISE, ARISING FROM, IN OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/IParameter.cs (the `LifetimeAnnotation`
// struct, the C# 11 scoped-reference annotation carried by an `IParameter`). A value
// type with two boolean fields: `RefScoped` (the `scoped ref` annotation, the C# 11
// `ScopedRefAttribute`) and `ValueScoped` (the C# 11 preview `ref scoped` annotation,
// no longer supported). The `ScopedRef` property is the public accessor for `RefScoped`:
// the C# marks `RefScoped` and `ValueScoped` `[Obsolete]` but keeps them as the backing
// storage; the `[Obsolete]` attribute is a C# compile-time warning with no runtime
// behavior, so the C++ port keeps both fields verbatim and exposes `ScopedRef` as a
// get/set pair delegating to `RefScoped` (a property and a field may not share a name in
// C++, but `ScopedRef` and `RefScoped` do not, so there is no name collision).
//
// `IParameter::Lifetime` returns this struct by value (the C# struct return); the
// consumer (`TypeSystemAstBuilder.ConvertParameter`) reads
// `parameter.Lifetime.ScopedRef` to set the `ParameterDeclaration.IsScopedRef` flag and
// emit the `scoped` keyword. `DefaultParameter.Lifetime => default` returns the
// all-false default-constructed value.

#pragma once

namespace ILSpy::Decompiler::TypeSystem {

// The C# 11 scoped-reference annotation on a parameter. A value type
// (default-constructed to all-false, the `DefaultParameter.Lifetime => default` case).
// Both fields are public (the C# `public bool RefScoped` / `public bool ValueScoped`);
// `ScopedRef` is the get/set accessor for `RefScoped` (the C# property).
struct LifetimeAnnotation {
    // The C# `[Obsolete] public bool RefScoped` -- the `scoped ref` annotation (the
    // backing field for the `ScopedRef` property). The `[Obsolete]` is compile-time only
    // (no C++ runtime counterpart); the field is kept verbatim for API fidelity.
    bool RefScoped = false;
    // The C# `[Obsolete] public bool ValueScoped` -- the C# 11 preview `ref scoped`
    // annotation, no longer supported. Kept verbatim for API fidelity.
    bool ValueScoped = false;

    // The C# `bool ScopedRef { get; set; }` -- the public accessor for `RefScoped`. The
    // C# property delegates get/set to the `RefScoped` field; the C++ port mirrors it as
    // a get/set member-function pair (the property's get/set semantics). The two overloads
    // are distinguished by their parameter list (zero args vs one bool).
    bool ScopedRef() const { return RefScoped; }
    void ScopedRef(bool value) { RefScoped = value; }
};

} // namespace ILSpy::Decompiler::TypeSystem
