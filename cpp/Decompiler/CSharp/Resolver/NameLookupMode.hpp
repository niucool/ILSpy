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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/NameLookupMode.cs -- the
// `NameLookupMode` enum controlling how a name is looked up in the C# resolver.
// This is the third `cpp/Decompiler/CSharp/Resolver/` leaf toward the
// `CSharpResolver` leaf deps (the long-pole remaining blocker of
// `TypeSystemAstBuilder` / `CSharpAmbience`), after the D467 twin Alias
// `ResolveResult` subclasses. The C# source is a plain (non-`[Flags]`) `enum`
// with no underlying-type annotation (so `int`-backed, the C# default) and five
// members in declaration order:
//   * `Expression` (0) -- normal name lookup in expressions.
//   * `InvocationTarget` (1) -- the expression is the target of an invocation;
//     such a lookup returns only methods and delegate-typed fields.
//   * `Type` (2) -- normal name lookup in type references.
//   * `TypeInUsingDeclaration` (3) -- the type reference inside a `using`
//     declaration.
//   * `BaseTypeReference` (4) -- name lookup for base type references.
//
// The C# `CSharpResolver.ResolveSimpleTypeReference` / `ApplyShortAttributeNameIfPossible`
// pass `NameLookupMode.Type`, and the `MemberLookup` helper (the next
// `CSharp/Resolver` leaf toward `TypeDefinitionNameableInBaseList`) consumes the
// mode to shape the member-lookup result. The enum is a self-contained value type
// (no external dependencies), so it lands whole here ahead of those consumers.
// The C++ `enum class` (scoped, no implicit conversion to/from `int`, matching the
// C# enum's typed usage) is backed by `std::int32_t` (the C# `int` default); the
// member order and values mirror the C# exactly (each member's numeric value is
// its declaration index, 0..4).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public enum NameLookupMode` (an `int`-backed enum, no `[Flags]`). The
// `enum class` (scoped, no implicit conversion to/from `int`, matching the C#
// enum's typed usage) is backed by `std::int32_t` (the C# `int` default). The
// member order and values mirror the C# exactly: each member's numeric value is
// its declaration index (`Expression = 0` through `BaseTypeReference = 4`), the
// C# declaration-order assignment the `MemberLookup` / `CSharpResolver` consumers
// rely on (the mode is compared by identity, never by arithmetic).
enum class NameLookupMode : std::int32_t {
	Expression,
	InvocationTarget,
	Type,
	TypeInUsingDeclaration,
	BaseTypeReference,
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
