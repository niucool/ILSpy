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
//
// The remaining methods are DEFERRED until their consumers port: `IsBitwise`
// (BinaryOperatorType -- the unported CSharpResolver/OutputVisitor binary-operator
// tiebreaks), `GetNextStatement` (Statement -- the unported statement-flow stages),
// `IsArgList` / `AddNamedArgument` / `Detach` / `UnwrapInDirectionExpression`
// (the unported CSharpResolver/TypeSystemAstBuilder stages).

#pragma once

#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"  // OperatorType (the enum)

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

} // namespace ILSpy::Decompiler::CSharp::Syntax
