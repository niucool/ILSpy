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

// Port of the `Slots` holder in ICSharpCode.Decompiler/CSharp/Syntax (the generated
// `Slots.g.cs`, emitted by DecompilerSyntaxTreeGenerator.cs). `Slots` is the shared set of
// canonical slot-kind constants: one typed `CSharpSlotInfo<T>` per distinct child position
// across all AST node types. Each constant is its own canonical kind (constructed with a
// null `Kind`); a node's per-node slot points back at it, and consumers compare
// `node.Slot.Kind == Slots.X` by object identity to identify a node's position
// polymorphically (replacing the old `node.Role == Roles.X` comparisons).
//
// The C# `public static class Slots` ports as a namespace (`ILSpy::...::Syntax::Slots`)
// holding `inline` variables: C++17 `inline` variables at namespace scope have external
// linkage and one address across translation units, so the pointer-identity comparison the
// slot system relies on is preserved. Each constant is `CSharpSlotInfoT<T>` (the C#
// `CSharpSlotInfo<T>`) whose ctor captures a `dynamic_cast<const T*>` is-a test, so the
// concrete child type must be complete where this header is included -- hence the
// `Expression.hpp` include (the type of the `Left`/`Right` positions ported so far).
//
// This header grows monotonically as node types land: each new slot kind (a `[Slot]` name
// not yet seen) adds one `inline` constant here. The kind carries identity and the precise
// child type only; the per-position `IsCollection`/`IsOptional` flags live on the per-node
// slots (the C# generator resolves the collection-vs-single ambiguity per position, so the
// shared constant's flags are not authoritative).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_SLOTS_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_SLOTS_HPP

#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax::Slots {

// The `Left` operand position (a single `Expression` child). Shared by the binary/assignment
// expressions (`BinaryOperatorExpression`, `AssignmentExpression`, ...) and any other node
// whose left operand is a single `Expression`.
inline const CSharpSlotInfoT<Expression> Left{"Left", false, nullptr, false};

// The `Right` operand position (a single `Expression` child). Shared by the binary/assignment
// expressions.
inline const CSharpSlotInfoT<Expression> Right{"Right", false, nullptr, false};

// The `Expression` operand position (a single `Expression` child). Used by the unary
// expressions (`UnaryOperatorExpression`, ...). The name collides with the `Expression`
// class in the parent `Syntax` namespace; the template argument in this definition resolves
// to the class (the constant being declared is not yet in scope at the point its type is
// parsed), and the unqualified `Expression` in any *later* `Slots` entry that wants the
// class as its element type must be qualified (`::ILSpy::Decompiler::CSharp::Syntax::Expression`)
// to avoid resolving to this constant.
inline const CSharpSlotInfoT<Expression> Expression{"Expression", false, nullptr, false};

} // namespace ILSpy::Decompiler::CSharp::Syntax::Slots

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_SLOTS_HPP
