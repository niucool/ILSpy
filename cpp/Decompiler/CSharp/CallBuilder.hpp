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

// Port of ICSharpCode.Decompiler/CSharp/CallBuilder.cs -- the call-expression
// builder the ExpressionBuilder VisitNewObj/VisitCall arms construct.
//
// This file is the FIRST SLICE: the `IsSpanBasedStringConcat(IMethod)` static
// the VisitUserDefinedCompoundAssign arm consults to detect the
// span-based `string.Concat(ReadOnlySpan<char>, ...)` compound-assign lowering
// (`s += "..."` on a const string). The Build/BuildStringConcat machinery and
// the `IsStringToReadOnlySpanCharImplicitConversion` helper are DEFERRED with
// the VisitCall/VisitNewObj slices they serve.

#pragma once

#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/Util/BitSet.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

// The real type-system namespace alias (the ExpressionBuilder TS:: convention --
// the CSharp/TypeSystem sub-namespace shadows the plain `TypeSystem::` lookup).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace ILSpy::Decompiler::TypeSystem {
class IParameter;
}

namespace ILSpy::Decompiler {
class DecompilerSettings;
namespace IL {
class ILInstruction;
class Call;
}
}

namespace ILSpy::Decompiler::CSharp {

class ExpressionBuilder;

// The C# `public class CallBuilder` -- the port carries the static half first
// (the VisitUserDefinedCompoundAssign prerequisite); the instance Build family
// lands with the call arms.
class CallBuilder {
public:
    // The C# `struct ExpectedTargetDetails` (CallBuilder.cs lines 42-46): the call
    // opcode and whether the boxing conversion on the target must be preserved.
    struct ExpectedTargetDetails {
        // The C# `default(OpCode)` zero value is `InvalidBranch` (the port's enum
        // first value); it is always assigned before the Build arms read it.
        IL::OpCode CallOpCode = IL::OpCode::InvalidBranch;
        bool NeedsBoxingConversion = false;
    };

    // The C# `struct ArgumentList` (CallBuilder.cs lines 48-200): the translated
    // call arguments plus the expected parameters, names, and optional/named
    // bookkeeping the Build arms fill in. The C# `string[]` name arrays port to
    // `std::vector<std::string>` with the empty string standing in for null (the
    // D222 IsNullOrEmpty convention); a null-name element means "no name".
    struct ArgumentList {
        std::vector<TranslatedExpression> Arguments;
        std::vector<const TS::IParameter*> ExpectedParameters;
        std::vector<std::string> ParameterNames;
        std::optional<std::vector<std::string>> ArgumentNames;
        int FirstOptionalArgumentIndex = -1;
        Util::BitSet IsPrimitiveValue;
        std::optional<std::vector<int>> ArgumentToParameterMap;
        bool AddNamesToPrimitiveValues = false;
        bool UseImplicitlyTypedOut = false;
        bool IsExpandedForm = false;

        // The C# `int Length => Arguments.Length`.
        int Length() const { return static_cast<int>(Arguments.size()); }

        // The C# `string[]? GetArgumentNames(int skipCount = 0)`.
        std::optional<std::vector<std::string>> GetArgumentNames(int skipCount = 0) const;
        // The C# `IList<ResolveResult> GetArgumentResolveResults(int skipCount = 0)`.
        std::vector<std::shared_ptr<Sem::ResolveResult>> GetArgumentResolveResults(
            int skipCount = 0) const;
        // The C# `IList<ResolveResult> GetArgumentResolveResultsDirect(int skipCount = 0)`.
        std::vector<std::shared_ptr<Sem::ResolveResult>> GetArgumentResolveResultsDirect(
            int skipCount = 0) const;
        // The C# `IEnumerable<Expression> GetArgumentExpressions(int skipCount = 0)`.
        std::vector<Syntax::Expression*> GetArgumentExpressions(int skipCount = 0) const;
        // The C# `bool CanInferAnonymousTypePropertyNamesFromArguments()`.
        bool CanInferAnonymousTypePropertyNamesFromArguments() const;
        // The C# `[Conditional("DEBUG")] void CheckNoNamedOrOptionalArguments()`.
        void CheckNoNamedOrOptionalArguments() const;

    private:
        // The C# `private int GetActualArgumentCount()`.
        int GetActualArgumentCount() const;
    };

    virtual ~CallBuilder() = default;

    // The C# `public CallBuilder(ExpressionBuilder expressionBuilder,
    // IDecompilerTypeSystem typeSystem, DecompilerSettings settings)` -- the port
    // keeps the expression builder it translates through and the settings bag it
    // consults (the type system is reached through the builder's compilation).
    CallBuilder(ExpressionBuilder& expressionBuilder,
                const DecompilerSettings& settings);

    // The C# `private ArgumentList BuildArgumentList(ExpectedTargetDetails
    // expectedTargetDetails, ResolveResult? target, IMethod method, int
    // firstParamIndex, IReadOnlyList<ILInstruction> callArguments,
    // IReadOnlyList<int>? argumentToParameterMap)` (CallBuilder.cs lines
    // 941-1052): translate every call argument to its expected parameter type,
    // flag the primitive-value arguments that should keep their names, track the
    // optional-argument index, expand a trailing `params` argument when the
    // setting allows, and fill the returned ArgumentList. The named-argument
    // (`argumentToParameterMap`) path and the params-expansion
    // (`TransformParamsArgument`) path are deferred loudly (both depend on the
    // unported overload-resolution machinery); the positional path is ported.
    // Declared private in the C#; the port keeps it public (the no-visibility
    // convention) so tests can call it directly.
    ArgumentList BuildArgumentList(const ExpectedTargetDetails& expectedTargetDetails,
                                   const Sem::ResolveResult* target,
                                   const TS::IMethod& method,
                                   int firstParamIndex,
                                   const std::vector<IL::ILInstruction*>& callArguments,
                                   const std::optional<std::vector<int>>& argumentToParameterMap);

    // The C# `internal static bool IsSpanBasedStringConcat(IMethod method)`
    // (CallBuilder.cs lines 300-318): whether the method is a static
    // `string.Concat` whose every parameter is `ReadOnlySpan<char>` -- the
    // span-based overload shape the C# compiler emits for `s += "literal"`.
    // Recognized by the method name, the static form, the
    // System.String declaring type (the C# `DeclaringType.IsKnownType
    // (KnownTypeCode.String)` extension), and the per-parameter
    // `ReadOnlySpan<char>` element check (`p.Type.TypeArguments[0]`).
    // Implemented out-of-line in the .cpp.
    static bool IsSpanBasedStringConcat(const TS::IMethod& method);

    // The C# `internal static bool IsStringToReadOnlySpanCharImplicitConversion(
    // IMethod method)` (CallBuilder.cs lines 320-328): whether the method is the
    // `op_Implicit` operator converting a `string` to a `ReadOnlySpan<char>` --
    // the span-based string.Concat operand shape. Implemented out-of-line.
    static bool IsStringToReadOnlySpanCharImplicitConversion(const TS::IMethod& method);

    // The C# `List<(ILInstruction Instruction, KnownTypeCode TypeCode)>` element
    // the operand-extraction overload fills (a plain pair struct; the C# tuple
    // element names carry over as the member names).
    struct SpanConcatOperand {
        IL::ILInstruction* Instruction = nullptr;
        TS::KnownTypeCode TypeCode = TS::KnownTypeCode::None;
    };

    // The C# `static bool IsSpanBasedStringConcat(CallInstruction call,
    // [NotNullWhen(true)] out List<(ILInstruction, KnownTypeCode)>? operands)`
    // (CallBuilder.cs lines 268-298): the operand-extraction overload of the
    // span-based string-concat shape. Every call argument must be either an
    // `op_Implicit(string -> ReadOnlySpan<char>)` call (operand = its single
    // argument, typed string -- the C# `opImplicit.Arguments.Single()`, with a
    // non-single-arity argument list failing the shape where the C# would
    // throw) or a `newobj ReadOnlySpan<char>(addressOf(charValue))` (operand =
    // the AddressOf's value, typed char). The shape holds when there are at
    // least two arguments and the first string-typed operand appears at index
    // 0 or 1 (`firstStringArgumentIndex <= 1`, false when no argument took the
    // string arm). The NewObj arm needs the `AddressOf` node and the
    // `ILInlining.IsReadOnlySpanCharCtor` helper. Implemented out-of-line.
    static bool IsSpanBasedStringConcat(const IL::Call& call,
                                        std::vector<SpanConcatOperand>& operands);

    // The C# `private ExpressionWithResolveResult BuildStringConcat(IMethod
    // method, List<(ILInstruction, KnownTypeCode)> operands)` (CallBuilder.cs
    // lines 252-266): the `s + "literal"` render for the span-based
    // string.Concat shape -- translates each operand to its element type,
    // folds them left-associatively with `+` over a shared MemberResolveResult
    // on the Concat method. Public for the tests (the port's no-visibility
    // convention). Implemented out-of-line.
    ExpressionWithResolveResult BuildStringConcat(
        const TS::IMethod& method,
        const std::vector<SpanConcatOperand>& operands);

    // The C# `private enum TokenKind` (CallBuilder.cs lines 838-845) -- the
    // format-string token classes the interpolation tokenizer yields.
    enum class TokenKind {
        Error,
        String,
        Argument,
        ArgumentWithFormat,
        ArgumentWithAlignment,
        ArgumentWithAlignmentAndFormat,
    };

    // The C# `(TokenKind Kind, int Index, int Alignment, string? Format)`
    // token tuple (CallBuilder.cs lines 770-838 usage). `Format` carries the
    // literal text for a String token and the format suffix for the
    // format-bearing argument tokens (the C# `string?` -- an empty optional
    // is the C# null).
    struct InterpolationToken {
        TokenKind Kind = TokenKind::Error;
        int Index = 0;
        int Alignment = 0;
        std::optional<std::string> Format;
    };

    // The C# `private IEnumerable<(TokenKind, string?)> TokenizeFormatString(
    // string value)` (CallBuilder.cs lines 841-941): the format-string
    // tokenizer -- literal runs, the `{{`/`}}` escapes, and the argument
    // holes with their optional `,alignment` and `:format` suffixes. The C#
    // iterator-with-local-functions materializes eagerly into a vector (the
    // ToVector convention); an unterminated hole or a stray `}` yields an
    // Error token (the caller aborts). Implemented out-of-line.
    static std::vector<std::pair<TokenKind, std::optional<std::string>>>
    TokenizeFormatString(const std::string& value);

    // The C# `private bool TryGetStringInterpolationTokens(ArgumentList
    // argumentList, out string? format, out List<(TokenKind, int, int,
    // string?)> tokens)` (CallBuilder.cs lines 770-838): whether the call's
    // argument shape is a plain `string.Format("...", args)` renderable as
    // an interpolated string -- a constant string first argument (no named
    // arguments, no argument-to-parameter map), no other string literal
    // among the arguments, and format holes that reference the arguments
    // exactly once and in order. `format`/`tokens` are written only on the
    // true return. Implemented out-of-line.
    static bool TryGetStringInterpolationTokens(
        const ArgumentList& argumentList, std::string& format,
        std::vector<InterpolationToken>& tokens);

    // The C# `private ExpressionWithResolveResult HandleStringInterpolation(
    // IMethod method, ArgumentList argumentList)` (CallBuilder.cs lines
    // 595-666): the `string.Format("...", args)` render as an
    // InterpolatedStringExpression (the `FormattableStringFactory.Create`
    // shape casts the result to FormattableString). Returns the default
    // wrapper (a null expression) when the tokens fail -- the C# `return
    // default`. Public for the tests (the port's no-visibility convention).
    // Implemented out-of-line.
    ExpressionWithResolveResult HandleStringInterpolation(
        const TS::IMethod& method, const ArgumentList& argumentList);

    // The C# `static bool IsNullConditional(Expression expr)` (CallBuilder.cs
    // lines 1480-1483): whether the expression is the `?.` null-conditional
    // operator (so a delegate `Invoke` on it must not be re-rendered as a plain
    // invocation).
    static bool IsNullConditional(const Syntax::Expression* expr);

    // The C# `private static bool IsInterpolatedStringCreation(IMethod method,
    // ArgumentList argumentList)` (CallBuilder.cs lines 755-766): whether the
    // call is a `string.Format` / `FormattableStringFactory.Create` that the
    // string-interpolation transform can render as an interpolated string. Uses
    // the `IType::Namespace()` surface (the FormattableStringFactory declaring
    // type's namespace).
    static bool IsInterpolatedStringCreation(const TS::IMethod& method,
                                             const ArgumentList& argumentList);

    // The C# `private bool IsDelegateEqualityComparison(IMethod method,
    // IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1511-1523):
    // whether the call is a `Delegate.op_Equality`/`op_Inequality` comparison
    // that should render as the C# builtin `==`/`!=` operator on two delegate
    // operands.
    static bool IsDelegateEqualityComparison(
        const TS::IMethod& method,
        const std::vector<TranslatedExpression>& arguments);

    // The C# `private Expression HandleDelegateEqualityComparison(IMethod method,
    // IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1524-1532):
    // the `left == right` / `left != right` render.
    static Syntax::Expression* HandleDelegateEqualityComparison(
        const TS::IMethod& method,
        const std::vector<TranslatedExpression>& arguments);

    // The C# `bool IsAppropriateCallTarget(ExpectedTargetDetails expectedTargetDetails,
    // IMember expectedTarget, IMember actualTarget)` (CallBuilder.cs lines
    // 1816-1835): whether the overload-resolution result `actualTarget` may stand
    // in for the IL's `expectedTarget`. True on a type-erasure match; else a
    // `CallVirt` to an override whose base-member chain (via `GetBaseMembers`)
    // contains the expected target, unless the expected call needed a boxing
    // conversion on a non-reference declaring type.
    static bool IsAppropriateCallTarget(const ExpectedTargetDetails& expectedTargetDetails,
                                        const TS::IMember& expectedTarget,
                                        const TS::IMember& actualTarget);

    // The C# `private ExpressionWithResolveResult HandleImplicitConversion(IMethod method,
    // TranslatedExpression argument)` (CallBuilder.cs lines 1534-1556): the user-defined
    // `op_Implicit` render -- re-check the implicit conversion, cast the argument to the
    // operator's source type when the cached conversion is not the operator itself, unwrap
    // an `in` direction, and emit the cast to the target type with a
    // ConversionResolveResult. Public for the tests (the port's no-visibility convention).
    ExpressionWithResolveResult HandleImplicitConversion(const TS::IMethod& method,
                                                         TranslatedExpression argument);

private:
    // The C# `private bool IsPrimitiveValueThatShouldBeNamedArgument(
    // TranslatedExpression arg, IMethod method, IParameter p)` (CallBuilder.cs
    // lines 1054-1060): a compile-time constant boolean argument of a method
    // that is not `Nullable<T>` -- such an argument keeps its parameter name.
    bool IsPrimitiveValueThatShouldBeNamedArgument(const TranslatedExpression& arg,
                                                   const TS::IMethod& method,
                                                   const TS::IParameter& p) const;

    // The C# `bool IsOptionalArgument(IParameter parameter, TranslatedExpression
    // arg)` (CallBuilder.cs lines 1128-1141): whether `arg` is the exact default
    // value of the optional `parameter`, so it may be omitted from the call.
    bool IsOptionalArgument(const TS::IParameter& parameter,
                            const TranslatedExpression& arg) const;

    // The C# `private bool TransformParamsArgument(ExpectedTargetDetails
    // expectedTargetDetails, ResolveResult? targetResolveResult, IMethod method,
    // IParameter parameter, TranslatedExpression paramsArgument, ref
    // List<IParameter> expectedParameters, ref List<TranslatedExpression>
    // arguments)` (CallBuilder.cs lines 1062-1126): inline a trailing array
    // argument into the expanded `params` argument list when the call is
    // unambiguous. Deferred loudly -- it needs the unported overload-resolution
    // (`IsUnambiguousCall`) plus the array/invocation resolve-result arms.
    bool TransformParamsArgument(const ExpectedTargetDetails& expectedTargetDetails,
                                 const Sem::ResolveResult* targetResolveResult,
                                 const TS::IMethod& method,
                                 const TS::IParameter& parameter,
                                 const TranslatedExpression& paramsArgument,
                                 std::vector<const TS::IParameter*>& expectedParameters,
                                 std::vector<TranslatedExpression>& arguments);

    ExpressionBuilder* expressionBuilder_ = nullptr;
    const DecompilerSettings* settings_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp
