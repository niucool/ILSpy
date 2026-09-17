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
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Syntax/SyntaxExtensions.cs -- the extension
// methods for the syntax tree. The C# static class ports to free functions in the
// matching `ILSpy::Decompiler::CSharp::Syntax` namespace (the TypeSystemExtensions /
// ReflectionHelper free-function convention); each method lands when its first
// consumer does. This iteration ports:
//   - IsComparisonOperator(OperatorType) (SyntaxExtensions.cs line 30) -- consumed by
//     the resolver's `CSharpOperators.IsComparisonOperator(IMethod)`
//     (CSharpOperators.cs line 1124), which recognizes a user-defined comparison
//     operator by its metadata name and keeps the lifted form's `bool` return type
//     un-lifted (CSharpOperators.cs line 1142).
//   - Detach<T>(T*) (SyntaxExtensions.cs line 75) -- consumed by the ambience's
//     parameter-list rendering (`CSharpAmbience.ConvertSymbol` strips a parameter's
//     default expression when `ShowParameterDefaultValues` is off).
//   - GetNextStatement(Statement*) (SyntaxExtensions.cs line 56) -- consumed by the
//     AddCheckedBlocks transform's block-range walk (the first statement whose
//     `NextSibling` is a `Statement`, used to iterate a `BlockStatement`'s statements
//     while insertion is planned).
//
// The remaining methods are DEFERRED until their consumers port: `IsBitwise`
// (BinaryOperatorType -- the unported CSharpResolver/OutputVisitor binary-operator
// tiebreaks), `IsArgList` / `AddNamedArgument` / `UnwrapInDirectionExpression`
// (the unported CSharpResolver/TypeSystemAstBuilder stages).

#pragma once

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"  // OperatorType (the enum)
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public static bool IsComparisonOperator(this OperatorType operatorType)`
// (SyntaxExtensions.cs line 30) -- whether the operator type is one of the six
// comparison operators (`==`, `!=`, `>`, `<`, `>=`, `<=`). The C# `switch` with the
// six `true` cases ports to a chain of `==` comparisons (the `IsChecked`
// chain-of-comparisons precedent).
inline bool IsComparisonOperator(OperatorType operatorType) {
    return operatorType == OperatorType::Equality
        || operatorType == OperatorType::Inequality
        || operatorType == OperatorType::GreaterThan
        || operatorType == OperatorType::LessThan
        || operatorType == OperatorType::GreaterThanOrEqual
        || operatorType == OperatorType::LessThanOrEqual;
}

// The C# `public static T Detach<T>(this T node) where T : AstNode` (SyntaxExtensions.cs
// line 75) -- remove the node from its parent and return it (`node.Remove(); return
// node;`). The C# generic-constrained-to-AstNode ports as a template over the node
// pointer type (the return is the same node, for chaining call sites); the template body
// resolves `Remove()` at the point of instantiation, so a forward-declared node type
// suffices here and the caller includes the full node header. First consumed by the
// ambience's parameter-list rendering (`CSharpAmbience.ConvertSymbol`: the
// `param.DefaultExpression?.Detach()` strip when `ShowParameterDefaultValues` is off).
template <class T>
T* Detach(T* node) {
    node->Remove();
    return node;
}

// The C# `public static Statement? GetNextStatement(this Statement statement)`
// (SyntaxExtensions.cs line 56) -- the next sibling that is a `Statement`, skipping any
// intervening non-`Statement` siblings (`while (next != null && !(next is Statement))`),
// or null at the end. A `BlockStatement`'s statements are all `Statement`s, so the loop
// is a single step there; the walk matters for a statement embedded in a node whose slot
// can hold a non-statement sibling. The nullable return ports as a nullable pointer.
inline Statement* GetNextStatement(Statement* statement) {
    AstNode* next = statement->NextSibling();
    while (next != nullptr && dynamic_cast<Statement*>(next) == nullptr)
        next = next->NextSibling();
    return static_cast<Statement*>(next);
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
