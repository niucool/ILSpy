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
// Landed so far: the data carriers (ExpectedTargetDetails + ArgumentList, the
// C# nested structs that carry the translated call arguments through the
// Build arms) and the span-based string-concat family (the
// IsSpanBasedStringConcat(CallInstruction) overload, its
// IsStringToReadOnlySpanCharImplicitConversion prerequisite, and
// BuildStringConcat -- the `s1 + s2 + ...` fold the C# string-concat setting
// lowers the span-based `string.Concat(ReadOnlySpan<char>, ...)` overload to).
//
// Landed additionally: the argument-list machinery (BuildArgumentList,
// IsPrimitiveValueThatShouldBeNamedArgument, TransformParamsArgument,
// IsOptionalArgument) with the overload-resolution composition it validates
// through (IsUnambiguousCall + IsAppropriateCallTarget), and the
// overload-resolution driver itself (GetRequiredTransformationsForCall with the
// nested CallTransformation flags enum, CastArguments, EnforceExplicitIn /
// WrapInAsRefReadOnly, IsPossibleExtensionMethodCallOnNull,
// CanInferTypeArgumentsFromArguments, and the anonymous-type helpers
// PinTypesOfNullArguments / NewAnonymousTypeInstance over the NRExtensions
// predicate family). The EnforceExplicitIn statementBuilder
// EmitAsRefReadOnly flag write is deferred with the StatementBuilder slice; the
// CastArguments lambda-return-type arm is deferred with
// ModifyReturnTypeOfLambda/DecompiledLambdaResolveResult.
//
// The remaining arms (HandleDelegateConstruction, the tuple construction,
// the mainline Build(OpCode, ...) body, HandleConstructorCall/
// HandleAccessorCall, HandleRangeConstruction, HandleStringInterpolation,
// IsDelegateEqualityComparison, ...) are DEFERRED with the VisitNewObj/
// VisitCall slices they serve.

#pragma once

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/Util/BitSet.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Semantics {
class ResolveResult;
}

namespace ILSpy::Decompiler::IL {
class Call;
class ILInstruction;
}

// The real type-system namespace alias (the ExpressionBuilder TS:: convention --
// the CSharp/TypeSystem sub-namespace shadows the plain `TypeSystem::` lookup).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace ILSpy::Decompiler::CSharp {

class ExpressionBuilder;

// The C# `struct ExpectedTargetDetails` (CallBuilder.cs lines 34-38): the
// call-opcode + boxing-need pair the Build arms thread to the target/argument
// builders.
struct ExpectedTargetDetails {
    // The C# `public OpCode CallOpCode;` -- the IL call opcode (the port models
    // call/callvirt/newobj as one shared Call node, so a node-level
    // call-vs-callvirt distinction is deferred with the reader's decode; the
    // field carries the caller-provided opcode verbatim).
    IL::OpCode CallOpCode = IL::OpCode::Nop;
    // The C# `public bool NeedsBoxingConversion;` -- set when a boxing
    // conversion on the call target was unwrapped and must be re-applied.
    bool NeedsBoxingConversion = false;
};

// The C# `struct ArgumentList` (CallBuilder.cs lines 40-198): the translated
// call arguments plus the parameter bookkeeping the render arms consume.
struct ArgumentList {
    // The C# `public TranslatedExpression[] Arguments;`
    std::vector<TranslatedExpression> Arguments;
    // The C# `public IParameter[] ExpectedParameters;` -- the parameters in
    // ARGUMENT order (the expanded-params form reorders them).
    std::vector<const TS::IParameter*> ExpectedParameters;
    // The C# `public string[] ParameterNames;`
    std::vector<std::string> ParameterNames;
    // The C# `public string[]? ArgumentNames;` -- the assigned argument names
    // (null when no out-of-place argument demanded names).
    std::optional<std::vector<std::string>> ArgumentNames;
    // The C# `public int FirstOptionalArgumentIndex;` -- -2 none / -1 forbidden
    // / >= 0 the first removable optional argument.
    int FirstOptionalArgumentIndex = 0;
    // The C# `public BitSet IsPrimitiveValue;`
    Util::BitSet IsPrimitiveValue;
    // The C# `public IReadOnlyList<int>? ArgumentToParameterMap;`
    std::optional<std::vector<int>> ArgumentToParameterMap;

    // The C# `public bool AddNamesToPrimitiveValues;`
    bool AddNamesToPrimitiveValues = false;
    // The C# `public bool UseImplicitlyTypedOut;`
    bool UseImplicitlyTypedOut = false;
    // The C# `public bool IsExpandedForm;`
    bool IsExpandedForm = false;

    // The C# `public int Length => Arguments.Length;`
    int Length() const { return static_cast<int>(Arguments.size()); }

private:
    // The C# `private int GetActualArgumentCount()`.
    int GetActualArgumentCount() const {
        if (FirstOptionalArgumentIndex < 0)
            return static_cast<int>(Arguments.size());
        return FirstOptionalArgumentIndex;
    }

public:
    // The C# `public string[]? GetArgumentNames(int skipCount = 0)`: the
    // argument names to render -- the field's array, with the
    // parameter-name fills for unnamed primitive arguments applied when
    // `AddNamesToPrimitiveValues` is set. The C# aliases the FIELD array when
    // one exists (the fills mutate the shared array; a second call observes
    // them), so the port mutates `ArgumentNames` in place when engaged and
    // builds a fresh local (never stored back) when disengaged -- the C#
    // `argumentNames = new string[...]` arm.
    std::optional<std::vector<std::string>> GetArgumentNames(int skipCount = 0);

    // The C# `public IList<ResolveResult> GetArgumentResolveResults(int
    // skipCount = 0)`: the resolve results of the first
    // `GetActualArgumentCount()` arguments from `skipCount` on, with the
    // implicitly-typed-out rule applied (an Out parameter over a
    // ByReferenceType argument type answers a fresh OutVarResolveResult over
    // the reference's element type -- a resolve result NOT attached to the
    // node, so the port's returned shared handles own the fresh instances and
    // alias the annotation channel for the plain ones).
    std::vector<std::shared_ptr<Sem::ResolveResult>> GetArgumentResolveResults(
        int skipCount = 0);

    // The C# `public IList<ResolveResult> GetArgumentResolveResultsDirect(int
    // skipCount = 0)`: the same slice without the out-var rule.
    std::vector<std::shared_ptr<Sem::ResolveResult>>
    GetArgumentResolveResultsDirect(int skipCount = 0);

    // The C# `public IEnumerable<Expression> GetArgumentExpressions(int
    // skipCount = 0)`: the argument expressions, wrapped in
    // NamedArgumentExpression where the composed name is non-null and the
    // `UseImplicitlyTypedOutAnnotation` applied to out-variable expressions
    // when the flag is set (the annotation mutates the node).
    std::vector<Syntax::Expression*> GetArgumentExpressions(int skipCount = 0);

    // The C# `public bool CanInferAnonymousTypePropertyNamesFromArguments()`:
    // whether every argument's expression is an identifier or member reference
    // whose inferred name equals the expected parameter's name.
    bool CanInferAnonymousTypePropertyNamesFromArguments() const;

    // The C# `[Conditional("DEBUG")] public void
    // CheckNoNamedOrOptionalArguments()`: the debug-only guard the accessor
    // arms call before their positional render. The port's assert() compiles
    // out with NDEBUG (the C# Conditional contract).
    void CheckNoNamedOrOptionalArguments() const;
};

// The C# `public class CallBuilder`. The port carries the string-concat family
// and the data carriers first; the instance Build family lands with the call
// arms.
class CallBuilder {
public:
    // The C# `[Flags] enum CallTransformation` (CallBuilder.cs lines 1138-1150,
    // nested PRIVATE in the class): the fix actions the
    // GetRequiredTransformationsForCall loop applied (or permits) on the way to
    // an unambiguous call -- the flags the Build arms re-apply when emitting the
    // call's shape. The C# is private; the port's no-visibility-level convention
    // keeps it public for tests (the [Flags] enum port convention, the
    // OverloadResolutionErrors precedent -- int32-backed with the bitwise
    // operators).
    enum class CallTransformation : std::int32_t {
        None = 0,
        RequireTarget = 1,
        RequireTypeArguments = 2,
        NoOptionalArgumentAllowed = 4,
        // Add calls to AsRefReadOnly for in parameters that did not have an
        // explicit DirectionExpression yet.
        EnforceExplicitIn = 8,
        NoNamedArgsForPrettiness = 0x10,
        All = 0x1f,
    };

    virtual ~CallBuilder() = default;

    // The C# `public CallBuilder(ExpressionBuilder expressionBuilder,
    // IDecompilerTypeSystem typeSystem, DecompilerSettings settings)`: the
    // resolver is read off the builder (the C# `expressionBuilder.resolver`
    // internal-field read). The null-builder guard ports the C#'s implicit
    // NullReferenceException (the established invalid_argument convention). The
    // builder reference is MUTABLE in the C# (BuildStringConcat's Translate
    // calls mutate the builder's caches), so the port carries a non-const
    // pointer.
    CallBuilder(ExpressionBuilder* expressionBuilder,
                const TS::ICompilation& typeSystem,
                const DecompilerSettings* settings);

    // The C# `private ArgumentList BuildArgumentList(ExpectedTargetDetails
    // expectedTargetDetails, ResolveResult? target, IMethod method, int
    // firstParamIndex, IReadOnlyList<ILInstruction> callArguments,
    // IReadOnlyList<int>? argumentToParameterMap)` (CallBuilder.cs lines
    // 941-1043): translate every call argument against its expected parameter
    // type, bookkeeping the optional-argument index, the primitive-value bits,
    // the params expansion (TransformParamsArgument), the
    // argument-name/argumentToParameterMap mapping, and the direction
    // expressions. Made public for tests (the C# private member).
    ArgumentList BuildArgumentList(
        const ExpectedTargetDetails& expectedTargetDetails,
        const Sem::ResolveResult* target, const TS::IMethod& method,
        int firstParamIndex,
        const std::vector<IL::ILInstruction*>& callArguments,
        const std::optional<std::vector<int>>& argumentToParameterMap);

    // The C# `private bool IsPrimitiveValueThatShouldBeNamedArgument(
    // TranslatedExpression arg, IMethod method, IParameter p)` (lines
    // 1046-1051): a compile-time-constant argument whose parameter type is
    // Boolean over a non-Nullable`1 declaring type -- the value the
    // argument-name fills may expose. Made public for tests.
    static bool IsPrimitiveValueThatShouldBeNamedArgument(
        const TranslatedExpression& arg, const TS::IMethod& method,
        const TS::IParameter& p);

    // The C# `private bool TransformParamsArgument(...)` (lines 1053-1142):
    // the params-expansion arm -- the `new T[...]` / `Array.Empty<T>()` /
    // `ReadOnlySpan<T>..ctor(ref readonly T)` argument shapes expand into
    // per-element arguments + DefaultParameters, validated through
    // IsUnambiguousCall's expanded-form resolution (the caller's already
    // translated prefix is prepended). Made public for tests.
    bool TransformParamsArgument(
        const ExpectedTargetDetails& expectedTargetDetails,
        const Sem::ResolveResult* targetResolveResult,
        const TS::IMethod& method, const TS::IParameter& parameter,
        const TranslatedExpression& paramsArgument,
        std::vector<const TS::IParameter*>& expectedParameters,
        std::vector<TranslatedExpression>& arguments);

    // The C# `bool IsOptionalArgument(IParameter parameter, TranslatedExpression
    // arg)` (lines 1144-1150): whether an optional parameter's argument is the
    // optional parameter's own default value (removable from the call). The
    // Caller*Attribute parameters are never removable. Made public for tests.
    bool IsOptionalArgument(const TS::IParameter& parameter,
                            const TranslatedExpression& arg);

    // The C# `private CallTransformation GetRequiredTransformationsForCall(
    // ExpectedTargetDetails, IMethod, ref TranslatedExpression target, ref
    // ArgumentList, CallTransformation allowedTransforms, out IParameterizedMember?)`
    // (CallBuilder.cs lines 1152-1343): the overload-resolution driver -- the
    // requireTarget/requireTypeArguments initialization, then the fix loop that
    // drives IsUnambiguousCall and applies one transformation per failed
    // resolution until the call is unambiguous or the ladder gives up, and the
    // final transformation-flag aggregation. `target` / `argumentList` are the
    // C# `ref` parameters (mutated in place); `foundMethod` is the C# `out`
    // (the resolved member, or `method` itself when the ladder gives up). Made
    // public for tests.
    CallTransformation GetRequiredTransformationsForCall(
        const ExpectedTargetDetails& expectedTargetDetails,
        const TS::IMethod& method, TranslatedExpression& target,
        ArgumentList& argumentList, CallTransformation allowedTransforms,
        const TS::IParameterizedMember*& foundMethod);

    // The C# `private void CastArguments(IList<TranslatedExpression> arguments,
    // IList<IParameter> expectedParameters)` (lines 1446-1478): the explicit-cast
    // insertion the fix ladder's argumentsCasted arm applies -- every argument is
    // converted to its parameter type with allowImplicitConversion: false (the
    // dynamic-parameter Object substitution, the `in`-parameter element unwrap,
    // and the anonymous-type lambda-return-type arm all live here). Made public
    // for tests.
    void CastArguments(std::vector<TranslatedExpression>& arguments,
                       const std::vector<const TS::IParameter*>& expectedParameters);

    // The C# `private void EnforceExplicitIn(TranslatedExpression[] arguments,
    // IParameter[] expectedParameters)` (lines 1345-1356): wraps every argument
    // over an `in` parameter whose expression is not already a
    // DirectionExpression in the AsRefReadOnly invocation. The C# also sets
    // `expressionBuilder.statementBuilder.EmitAsRefReadOnly = true` -- that
    // flag write is DEFERRED with the StatementBuilder slice (the port's
    // statementBuilder field is a forward-declared placeholder; the wrap
    // itself is the observable state the accessor-arm render consumes).
    void EnforceExplicitIn(std::vector<TranslatedExpression>& arguments,
                           const std::vector<const TS::IParameter*>& expectedParameters);

    // The C# `private TranslatedExpression WrapInAsRefReadOnly(TranslatedExpression
    // arg)` (lines 1357-1368): the `in ILSpyHelper_AsRefReadOnly(arg)` invocation
    // wrapped in an `in` DirectionExpression over a ByReferenceResolveResult of
    // the argument's type, with no IL-instruction annotations.
    static TranslatedExpression WrapInAsRefReadOnly(const TranslatedExpression& arg);

    // The C# `private bool IsPossibleExtensionMethodCallOnNull(IMethod method,
    // IList<TranslatedExpression> arguments)` (lines 1369-1373): an extension
    // method whose first argument is the null literal -- the shape whose type
    // arguments can never be inferred from the arguments (RequireTypeArguments
    // is forced without the inference probe).
    static bool IsPossibleExtensionMethodCallOnNull(
        const TS::IMethod& method,
        const std::vector<TranslatedExpression>& arguments);

    // The C# `static bool CanInferTypeArgumentsFromArguments(IMethod method,
    // ArgumentList argumentList, TypeInference typeInference)` (lines 1374-1403):
    // whether the type inference answers every type parameter of the
    // (unspecialized) method from the arguments -- always true for a non-generic
    // method; the parameter types come from the map when present (an unmapped
    // argument positions SpecialType.UnknownType) else positionally.
    static bool CanInferTypeArgumentsFromArguments(
        const TS::IMethod& method, const ArgumentList& argumentList,
        const ExpressionBuilder::TypeInferenceInstance& typeInference);

    // The C# `private bool PinTypesOfNullArguments(ArgumentList argumentList)`
    // (lines 1404-1430): rewrites null-literal arguments whose expected type is
    // an anonymous type with the `true ? null : new { ... }` conditional that
    // makes the anonymous type inferable. Over the port's type system no type
    // IS anonymous yet (the machinery is not ported), so the predicate is
    // observable but the replacement is unreachable. Made public for tests.
    bool PinTypesOfNullArguments(ArgumentList& argumentList);

    // The C# `private NewObj? NewAnonymousTypeInstance(IType type)` (lines
    // 1431-1445): the `newobj` instruction whose translation yields the
    // object-initializer syntax that names the anonymous type. The port's Call
    // node models newobj through its IsNewObj flag (the seed convention), so
    // the factory builds a Call with the flag set; null when a property type
    // involves an anonymous type other than by direct nesting. Made public for
    // tests.
    std::unique_ptr<IL::Call> NewAnonymousTypeInstance(const TS::IType& type);

    // The C# `OverloadResolutionErrors IsUnambiguousCall(...)` (lines
    // 1554-1650): the overload-resolution driver -- the NewObj arm over the
    // declaring type's constructors, the operator arm over the operand-type
    // candidates, and the target/simple-name arms over MemberLookup /
    // ResolveSimpleName, each answering the resolved member and the expanded
    // form. Made public for tests.
    Resolver::OverloadResolutionErrors IsUnambiguousCall(
        const ExpectedTargetDetails& expectedTargetDetails,
        const TS::IMethod& method, const Sem::ResolveResult* target,
        const std::vector<TS::ITypePtr>& typeArguments,
        std::vector<std::shared_ptr<Sem::ResolveResult>> arguments,
        std::optional<std::vector<std::string>> argumentNames,
        int firstOptionalArgumentIndex,
        const TS::IParameterizedMember*& foundMember,
        bool& bestCandidateIsExpandedForm) const;

    // The C# `bool IsAppropriateCallTarget(...)` (lines 1816-1831): whether the
    // resolved member may replace the expected one -- the type-erased equality,
    // or the CallVirt override chain over the base members. Made public for
    // tests.
    bool IsAppropriateCallTarget(const ExpectedTargetDetails& expectedTargetDetails,
                                 const TS::IMember& expectedTarget,
                                 const TS::IMember& actualTarget) const;

    // The C# `internal static bool IsSpanBasedStringConcat(IMethod method)`
    // (CallBuilder.cs lines 300-318): whether the method is a static
    // `string.Concat` whose every parameter is `ReadOnlySpan<char>` -- the
    // span-based overload shape the C# compiler emits for `s += "literal"`.
    // Implemented out-of-line in the .cpp.
    static bool IsSpanBasedStringConcat(const TS::IMethod& method);

    // The C# `static bool IsSpanBasedStringConcat(CallInstruction call, out
    // List<(ILInstruction, KnownTypeCode)>? operands)` (CallBuilder.cs lines
    // 275-298): the argument walk over the span-based call -- each argument is
    // either the `string -> ReadOnlySpan<char>` op_Implicit conversion call
    // (its single argument is a String operand) or the
    // `newobj ReadOnlySpan<char>(&c)` constructor over an AddressOf (the
    // referenced value is a Char operand); any other shape rejects the whole
    // call. The `call.Arguments.Count >= 2 && firstStringArgumentIndex <= 1`
    // tail requires at least one STRING argument in the first two slots (the
    // C# `int?` comparison is lifted: no string argument at all answers
    // false). Returns the operands (empty on the method-shaped-but-no-operand
    // degenerate) or nullopt.
    static bool IsSpanBasedStringConcat(
        const IL::Call& call,
        std::optional<std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>>&
            operands);

    // The C# `internal static bool
    // IsStringToReadOnlySpanCharImplicitConversion(IMethod method)` (lines
    // 322-330): the `string -> ReadOnlySpan<char>` op_Implicit operator.
    static bool IsStringToReadOnlySpanCharImplicitConversion(
        const TS::IMethod* method);

    // The C# `private ExpressionWithResolveResult BuildStringConcat(IMethod
    // method, List<(ILInstruction, KnownTypeCode)> operands)` (lines 227-252):
    // the `s1 + s2 + ...` fold -- every operand translated to its type code's
    // type and folded left-associatively with the SAME MemberResolveResult(null,
    // method) annotation on every node.
    ExpressionWithResolveResult BuildStringConcat(
        const TS::IMethod& method,
        const std::vector<std::pair<IL::ILInstruction*, TS::KnownTypeCode>>& operands);

private:
    ExpressionBuilder* expressionBuilder_ = nullptr;
    std::shared_ptr<const Resolver::CSharpResolver> resolver_;
    const DecompilerSettings* settings_ = nullptr;
    const TS::ICompilation* typeSystem_ = nullptr;

    // The expanded-params DefaultParameter keep-alive registry (the C# GC roots
    // the freshly allocated parameters through the ArgumentList's
    // ExpectedParameters array; the port's non-owning pointers need the owning
    // registry -- the uncached-entity-cache convention).
    std::vector<std::shared_ptr<const TS::IParameter>> ownedParameters_;
};

// The CallTransformation [Flags] operator surface (the OverloadResolutionErrors
// port convention -- the C# compiler generates the bitwise operators for every
// [Flags] enum; namespace-scope non-members are what enum-class operands find
// through ADL).
inline CallBuilder::CallTransformation operator|(
    CallBuilder::CallTransformation a, CallBuilder::CallTransformation b) {
    return static_cast<CallBuilder::CallTransformation>(
        static_cast<std::int32_t>(a) | static_cast<std::int32_t>(b));
}
inline CallBuilder::CallTransformation operator&(
    CallBuilder::CallTransformation a, CallBuilder::CallTransformation b) {
    return static_cast<CallBuilder::CallTransformation>(
        static_cast<std::int32_t>(a) & static_cast<std::int32_t>(b));
}
inline CallBuilder::CallTransformation operator^(
    CallBuilder::CallTransformation a, CallBuilder::CallTransformation b) {
    return static_cast<CallBuilder::CallTransformation>(
        static_cast<std::int32_t>(a) ^ static_cast<std::int32_t>(b));
}
inline CallBuilder::CallTransformation operator~(CallBuilder::CallTransformation a) {
    return static_cast<CallBuilder::CallTransformation>(
        ~static_cast<std::int32_t>(a));
}

} // namespace ILSpy::Decompiler::CSharp
