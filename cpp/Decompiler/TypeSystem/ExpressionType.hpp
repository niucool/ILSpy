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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of the BCL `System.Linq.Expressions.ExpressionType` enum -- the node-kind
// classification the `System.Linq.Expressions` expression-tree API uses for every
// expression node. It is the type `OperatorResolveResult.OperatorType` returns
// (ICSharpCode.Decompiler/Semantics/OperatorResolveResult.cs), which records the
// operator kind of a resolved unary / binary / ternary operator invocation.
//
// The BCL enum is a plain `int`-backed ordinal enum (NOT `[Flags]`) with 85 members in
// declaration order, `Add = 0` through `IsFalse = 84`, with NO gaps (verified by
// decompiling the .NET 10 `System.Linq.Expressions.dll` with this repo's `ilspycmd`,
// which emits each `.field public static literal ... = int32(0x..)` line). Like the
// other BCL enums absorbed into the C++ port (the D384 `MethodSemanticsAttributes`
// / D403 `SignatureCallingConvention` / D381 `EntityHandle` precedent), it is placed
// in `ILSpy::Decompiler::TypeSystem` (the C++ port has no `System::Linq::Expressions`
// namespace mirror) and keeps the `int` backing and declaration-order values pinned.
//
// The consumers compare for equality against the named values only: the not-yet-ported
// `ExpressionBuilder` switches on `OperatorResolveResult.OperatorType` (e.g.
// `case ExpressionType.Add: ... case ExpressionType.Subtract: ...`) to map a resolved
// operator back to its C# AST node, and `TypeSystemAstBuilder` consults it the same
// way. It is an ordinal enum, so the C++ `enum class`'s built-in `==` / `!=` suffices
// and NO bitwise or relational operators are ported (the `Nullability` D380 /
// `SignatureCallingConvention` D403 convention). The values are pinned explicitly
// because the enum is stored by value in `OperatorResolveResult` and compared to the
// named literals, so reordering would silently remap every operator classification.

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

// The BCL `System.Linq.Expressions.ExpressionType` enum: 85 members, `Add = 0`
// through `IsFalse = 84`, `int`-backed, declaration order pinned to the .NET values.
enum class ExpressionType : std::int32_t {
    // --- Arithmetic / unary (the .NET 3.5 expression-tree operators, 0..1) ---
    Add = 0,                // a + b
    AddChecked = 1,          // a + b (overflow-checked)

    // --- Logical / bitwise binary (2..3) ---
    And = 2,                // a & b (bitwise AND)
    AndAlso = 3,            // a && b (short-circuit AND)

    // --- Array (4..5) ---
    ArrayLength = 4,        // array.Length
    ArrayIndex = 5,         // array[index]

    // --- Call / control-flow / constant (6..9) ---
    Call = 6,               // a method invocation
    Coalesce = 7,           // a ?? b
    Conditional = 8,        // cond ? a : b
    Constant = 9,          // a literal value

    // --- Conversion (10..11) ---
    Convert = 10,           // (T)a
    ConvertChecked = 11,    // (T)a (overflow-checked)

    // --- Arithmetic binary (12) ---
    Divide = 12,           // a / b

    // --- Comparison (13..16) ---
    Equal = 13,             // a == b
    ExclusiveOr = 14,       // a ^ b
    GreaterThan = 15,       // a > b
    GreaterThanOrEqual = 16,// a >= b

    // --- Higher-level nodes (17..18) ---
    Invoke = 17,            // invoke a lambda delegate
    Lambda = 18,            // a lambda expression

    // --- Shift / comparison (19..21) ---
    LeftShift = 19,         // a << b
    LessThan = 20,          // a < b
    LessThanOrEqual = 21,   // a <= b

    // --- Init (22..24) ---
    ListInit = 22,          // a ListInitExpression
    MemberAccess = 23,      // a member read/write (a.field / a.Prop)
    MemberInit = 24,        // a MemberInitExpression

    // --- Arithmetic binary / unary (25..30) ---
    Modulo = 25,            // a % b
    Multiply = 26,          // a * b
    MultiplyChecked = 27,   // a * b (overflow-checked)
    Negate = 28,             // -a
    UnaryPlus = 29,         // +a
    NegateChecked = 30,     // -a (overflow-checked)

    // --- New / array (31..33) ---
    New = 31,               // new T(...)
    NewArrayInit = 32,      // new[] { ... }
    NewArrayBounds = 33,    // new T[...]

    // --- Logical unary / binary (34..36) ---
    Not = 34,               // !a / ~a
    NotEqual = 35,          // a != b
    Or = 36,                // a | b (bitwise OR)

    // --- Short-circuit / parameter / power (37..39) ---
    OrElse = 37,            // a || b (short-circuit OR)
    Parameter = 38,         // a lambda parameter
    Power = 39,             // a ^ b (math power; the ** operator)

    // --- Shift / arithmetic binary (40..43) ---
    Quote = 40,             // a quoted expression tree
    RightShift = 41,        // a >> b
    Subtract = 42,          // a - b
    SubtractChecked = 43,   // a - b (overflow-checked)

    // --- Type tests (44..45) ---
    TypeAs = 44,            // a as T
    TypeIs = 45,            // a is T

    // --- .NET 4.0 additions: statements / dynamic (46..62) ---
    Assign = 46,            // a = b
    Block = 47,             // a statement block
    DebugInfo = 48,         // a sequence-point debug info
    Decrement = 49,         // a - 1 (the decrement helper)
    Dynamic = 50,           // a dynamic operation
    Default = 51,          // default(T)
    Extension = 52,         // an extension expression
    Goto = 53,              // a goto / label jump
    Increment = 54,         // a + 1 (the increment helper)
    Index = 55,             // an indexed property/array index
    Label = 56,             // a label target
    RuntimeVariables = 57,  // a RuntimeVariablesExpression
    Loop = 58,              // a loop expression
    Switch = 59,            // a switch expression
    Throw = 60,             // a throw expression
    Try = 61,               // a try/catch/finally
    Unbox = 62,             // an unbox conversion

    // --- Compound assignment (63..80) ---
    AddAssign = 63,                 // a += b
    AndAssign = 64,                 // a &= b
    DivideAssign = 65,              // a /= b
    ExclusiveOrAssign = 66,         // a ^= b
    LeftShiftAssign = 67,           // a <<= b
    ModuloAssign = 68,              // a %= b
    MultiplyAssign = 69,            // a *= b
    OrAssign = 70,                  // a |= b
    PowerAssign = 71,               // a **= b
    RightShiftAssign = 72,          // a >>= b
    SubtractAssign = 73,            // a -= b
    AddAssignChecked = 74,          // a += b (overflow-checked)
    MultiplyAssignChecked = 75,     // a *= b (overflow-checked)
    SubtractAssignChecked = 76,     // a -= b (overflow-checked)
    PreIncrementAssign = 77,        // ++a
    PreDecrementAssign = 78,        // --a
    PostIncrementAssign = 79,       // a++
    PostDecrementAssign = 80,       // a--

    // --- .NET 4.0 tail: type / complement / truth tests (81..84) ---
    TypeEqual = 81,         // `type ==` (an exact-TypeIs test)
    OnesComplement = 82,     // ~a
    IsTrue = 83,             // a (as a bool truth test)
    IsFalse = 84,           // !a (as a bool falsity test)
};

}  // namespace ILSpy::Decompiler::TypeSystem
