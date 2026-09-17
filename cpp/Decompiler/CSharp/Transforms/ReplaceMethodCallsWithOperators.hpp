// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.cs
// -- the AST transform that replaces operator-method calls with the operator
// expressions.
//
// This file carries the resolver-free half: the statics the ExpressionBuilder's
// VisitUserDefinedCompoundAssign arm consumes -- `HasCheckedEquivalent(IMethod)`
// (the checked-operator twin detection) and `RemoveRedundantToStringInConcat`
// (the string.Concat argument `ToString()` elimination with its
// ToStringIsKnownEffectFree support table) -- plus the operator-name mapping
// tables and `IsInstantiableTypeParameter` the instance machinery consumes. The
// instance VisitInvocationExpression machinery (the ProcessInvocationExpression
// rewrite, CheckArgumentsForStringConcat and the other method-call rewrites) is
// DEFERRED with the IAstTransform slice it serves.

#pragma once

#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
// The real type-system namespace alias (the ExpressionBuilder TS:: convention --
// the CSharp/TypeSystem sub-namespace shadows the plain `TypeSystem::` lookup).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class ReplaceMethodCallsWithOperators : DepthFirstAstVisitor,
// IAstTransform` -- the port carries the static half first (the
// VisitUserDefinedCompoundAssign prerequisite); the instance AST-transform
// machinery lands with that slice.
class ReplaceMethodCallsWithOperators {
public:
    virtual ~ReplaceMethodCallsWithOperators() = default;

    // The C# `internal static bool HasCheckedEquivalent(IMethod method)`
    // (ReplaceMethodCallsWithOperators.cs lines 264-271): whether the declaring
    // type also declares the `op_Checked...` twin of the operator method -- the
    // C# 11 checked-operator shape the VisitUserDefinedCompoundAssign arm
    // annotates the target with the UncheckedAnnotation for. The C# `name.
    // StartsWith("op_", StringComparison.Ordinal)` ports to the rfind(prefix, 0)
    // == 0 idiom (this toolchain's C++17 build has no starts_with), and the
    // `DeclaringType.GetMethods(predicate).Any()` walk ports to the IType::
    // GetMethods(filter) query. Implemented out-of-line in the .cpp.
    static bool HasCheckedEquivalent(const TS::IMethod& method);

    // The C# `internal static Expression RemoveRedundantToStringInConcat(
    // Expression expr, IMethod concatMethod, bool isLastArgument)` (lines
    // 353-404): when `expr` is a `target.ToString()` / `target?.ToString()`
    // call (the ToStringCallPattern), the string.Concat overloads that take
    // strings only, and the target's type survives the elimination rules
    // (not by-ref-like, effect-free ToString for non-final arguments, no
    // NullReferenceException risk for reference types without the null-
    // conditional, and no struct-mutation risk), the ToString() call is
    // removed and the target expression returned (the C# compiler would
    // generate an equivalent call if the code were recompiled). Anything else
    // returns `expr` unchanged. Implemented out-of-line in the .cpp.
    static Syntax::Expression* RemoveRedundantToStringInConcat(
        Syntax::Expression* expr, const TS::IMethod& concatMethod,
        bool isLastArgument);

    // The C# `static bool ToStringIsKnownEffectFree(IType type)` (lines
    // 406-427): whether ToString() on the type is known to be side-effect free
    // -- the nullable-unwrap followed by the 16-primitive + String known-type
    // switch. Implemented out-of-line in the .cpp.
    static bool ToStringIsKnownEffectFree(const TS::IType& type);

    // The C# `static readonly Pattern ToStringCallPattern` match result (the
    // port hand-writes the two structural shapes the C# declarative pattern
    // matches): the matched `call` invocation (the ToString() call), the matched
    // `target` expression (the receiver), and whether the `?.`-rewrap shape
    // matched (`nullConditional`).
    struct ToStringCallMatch {
        Syntax::InvocationExpression* call = nullptr;
        Syntax::Expression* target = nullptr;
        bool nullConditional = false;
    };

    // The port's ToStringCallPattern.Match stand-in: the two-pattern Choice
    // (lines 339-351) -- `target.ToString()` as an InvocationExpression over a
    // MemberReferenceExpression named "ToString", and the `target?.ToString()`
    // shape as the same invocation wrapped in the NullConditionalRewrap unary
    // over the NullConditional receiver. Returns the match shape; a failed
    // match leaves `call` null.
    static ToStringCallMatch MatchToStringCallPattern(Syntax::Expression* expr);

    // The C# `static BinaryOperatorType? GetBinaryOperatorTypeFromMetadataName(
    // string name, out bool isChecked, DecompilerSettings settings)` (lines
    // 433-487): the metadata method name of a binary operator (`op_Addition` &c.)
    // mapped to its `BinaryOperatorType`, or `std::nullopt` when the name is not
    // a binary operator. The four `op_Checked...` names are recognized only when
    // `settings.CheckedOperators` is on (the C# switch `when` guards) and set
    // `isChecked`; every other name leaves it false. The C# `out bool isChecked`
    // ports to a `bool&` out-parameter (the CallBuilder/CSharpResolver precedent).
    // Implemented out-of-line in the .cpp.
    static std::optional<Syntax::BinaryOperatorType>
    GetBinaryOperatorTypeFromMetadataName(const std::string& name,
                                          bool& isChecked,
                                          const DecompilerSettings& settings);

    // The C# `static UnaryOperatorType? GetUnaryOperatorTypeFromMetadataName(
    // string name, out bool isChecked, DecompilerSettings settings)` (lines
    // 489-517): the metadata method name of a unary operator (`op_LogicalNot`
    // &c.) mapped to its `UnaryOperatorType`, or `std::nullopt` when the name is
    // not a unary operator. The three `op_Checked...` names are recognized only
    // when `settings.CheckedOperators` is on and set `isChecked`; every other
    // name leaves it false. Implemented out-of-line in the .cpp.
    static std::optional<Syntax::UnaryOperatorType>
    GetUnaryOperatorTypeFromMetadataName(const std::string& name,
                                         bool& isChecked,
                                         const DecompilerSettings& settings);

    // The C# `bool IsInstantiableTypeParameter(IType type)` (lines 272-275):
    // whether the type is a type parameter with the `new()` constraint (a type
    // argument `Activator.CreateInstance<T>()` may become `new T()`). The C#
    // `type is ITypeParameter tp && tp.HasDefaultConstructorConstraint` ports to
    // a dynamic cast plus the interface predicate. Needs no instance state, so
    // the port carries it as a static member. Implemented out-of-line in the
    // .cpp.
    static bool IsInstantiableTypeParameter(const TS::IType& type);
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
