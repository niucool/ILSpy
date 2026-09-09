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
// through (IsUnambiguousCall + IsAppropriateCallTarget), the
// overload-resolution driver itself (GetRequiredTransformationsForCall with the
// nested CallTransformation flags enum, CastArguments, EnforceExplicitIn /
// WrapInAsRefReadOnly, IsPossibleExtensionMethodCallOnNull,
// CanInferTypeArgumentsFromArguments, and the anonymous-type helpers
// PinTypesOfNullArguments / NewAnonymousTypeInstance over the NRExtensions
// predicate family), and the call-build composition itself: the
// Build(CallInstruction) entry (the delegate-construction arm renders through
// HandleDelegateConstruction; the tuple arm DEFERs
// loudly, the span-based string-concat arm wired) and the mainline
// Build(OpCode, ...) body -- the EII sealed-class rewrite, the local-function
// target arm (with ExpressionBuilder.ResolveLocalFunction and ToMethodGroup),
// the TranslateTarget + boxing unwrap, the VarArgInstanceMethod arm, the
// delegate-invoke / delegate-equality / op_Implicit special cases, the
// HandleRangeConstruction arms (over the ported SyntheticRangeIndexAccessor),
// the InlineArray and GetValueOrDefault arms, and the final
// RequireTarget/RequireTypeArguments invocation render. The EnforceExplicitIn
// statementBuilder EmitAsRefReadOnly flag write is deferred with the
// StatementBuilder slice; the CastArguments lambda-return-type arm is deferred
// with ModifyReturnTypeOfLambda/DecompiledLambdaResolveResult.
//
// The remaining render arm is one loud deferral behind its real C# gate
// condition: TupleTransform.MatchTupleConstruction's tuple-expression render
// (the TupleExpression slice). The delegate-reference family
// (HandleDelegateConstruction + CanUseDelegateConstruction +
// BuildDelegateReference/DisambiguateDelegateReference +
// IsUnambiguousMethodReference + BuildMethodReference + Build(LdVirtDelegate))
// is ported (with the LdFtn/LdVirtFtn/LdVirtDelegate nodes carrying the
// resolved IMethod the entry reads), as are the accessor-call slice
// (IsUnambiguousAccess + HandleAccessorCall), the interpolation slice
// (HandleStringInterpolation + TryGetStringInterpolationTokens +
// TokenizeFormatString), and the constructor-call slice (HandleConstructorCall).

#pragma once

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.hpp"
#include "Decompiler/CSharp/Syntax/Interpolation.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringText.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/TranslatedExpression.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
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
class LdVirtDelegate;
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
    // or the CallVirt override chain over the base members. `actualTarget` is a
    // POINTER (the C# can pass a null `foundMember` -- the no-candidate overload
    // resolution answers null and `expectedTarget.Equals(null, ...)` answers
    // false through reference equality); the CallVirt arm's own dereference is
    // the C# NullReferenceException arm (unreachable through the call arms that
    // never see a null with CallOpCode CallVirt). Made public for tests.
    bool IsAppropriateCallTarget(const ExpectedTargetDetails& expectedTargetDetails,
                                 const TS::IMember& expectedTarget,
                                 const TS::IMember* actualTarget) const;

    // The C# `bool IsUnambiguousAccess(ExpectedTargetDetails, ResolveResult? target,
    // IMethod method, IList<TranslatedExpression> arguments, string[]? argumentNames,
    // out IMember? foundMember)` (lines 1665-1698): the accessor overload-resolution
    // driver -- the null-target simple-name arm over ResolveSimpleName and the
    // member-lookup arms (the indexer arm over LookupIndexers + OverloadResolution,
    // the property/event arm over Lookup), each answering the resolved member.
    // `foundMember` is an out parameter (the C# `out` + NotNullWhen convention):
    // the caller must not read it when the call answers false. Made public for
    // tests.
    bool IsUnambiguousAccess(
        const ExpectedTargetDetails& expectedTargetDetails,
        const Sem::ResolveResult* target, const TS::IMethod& method,
        const std::vector<TranslatedExpression>& arguments,
        const std::optional<std::vector<std::string>>& argumentNames,
        const TS::IMember*& foundMember) const;

    // The C# `private ExpressionWithResolveResult HandleAccessorCall(
    // ExpectedTargetDetails, IMethod method, TranslatedExpression target,
    // List<TranslatedExpression> arguments, string[]? argumentNames)`
    // (lines 1712-1831): the accessor-call render -- the requireTarget/isSetter
    // pre-computation, the IsUnambiguousAccess fix loop with one transformation
    // per failed attempt (CastArguments -> requireTarget -> target cast -> the
    // accessor-owner fallback), and the setter/getter render matrix over the
    // Indexer/MemberReference/Identifier forms (the setter's event
    // +=/-= assignment operators included). `arguments` and `argumentNames` are
    // by-value copies (the C# caller passes `argumentList.Arguments.ToList()`).
    // Made public for tests.
    ExpressionWithResolveResult HandleAccessorCall(
        const ExpectedTargetDetails& expectedTargetDetails,
        const TS::IMethod& method, TranslatedExpression target,
        std::vector<TranslatedExpression> arguments,
        std::optional<std::vector<std::string>> argumentNames);

    // The C# `private ExpressionWithResolveResult HandleConstructorCall(
    // ExpectedTargetDetails, ResolveResult? target, IMethod method, ArgumentList
    // argumentList)` (CallBuilder.cs lines 1836-1900): the constructor-call
    // render -- the anonymous-type arm over AnonymousTypeCreateExpression (the
    // inferred or named-initializer shape) and the plain ObjectCreateExpression
    // render with the IsUnambiguousCall fix loop (one transformation per failed
    // attempt: AddNamesToPrimitiveValues -> FirstOptionalArgumentIndex ->
    // CastArguments) and the NativeIntegersWithoutAttribute n(u)int
    // returnTypeOverride. `argumentList` is the C# by-value parameter (the fix
    // loop mutates the copy). Made public for tests.
    ExpressionWithResolveResult HandleConstructorCall(
        const ExpectedTargetDetails& expectedTargetDetails,
        const Sem::ResolveResult* target, const TS::IMethod& method,
        ArgumentList argumentList);

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

    // -- The call-build composition (CallBuilder.cs lines 202-594) ---------------------

    // The C# `public TranslatedExpression Build(CallInstruction inst, IType?
    // typeHint = null)` (lines 202-241): the call entry -- the
    // delegate-construction arm (a newobj the IL match recognizes) renders
    // through HandleDelegateConstruction and the tuple-construction arm is
    // the remaining loud deferral (TupleTransform is unported), the
    // span-based string-concat arm renders its fold, and everything else
    // routes through the mainline Build with the IL-instruction and tail
    // markers applied.
    TranslatedExpression Build(const IL::Call& inst, const TS::IType* typeHint = nullptr);

    // The C# `public ExpressionWithResolveResult Build(OpCode callOpCode,
    // IMethod method, IReadOnlyList<ILInstruction> callArguments,
    // IReadOnlyList<int>? argumentToParameterMap = null, IType? constrainedTo =
    // null)` (lines 332-594): the mainline call render -- the EII sealed-class
    // rewrite, the local-function target arm, the TranslateTarget + boxing
    // unwrap, BuildArgumentList, the VarArgInstanceMethod arm, the delegate
    // invoke arm, the delegate-equality and op_Implicit special cases, the
    // InlineArray and GetValueOrDefault arms, the
    // GetRequiredTransformationsForCall fix ladder, and the final
    // RequireTarget/RequireTypeArguments invocation render.
    // HandleRangeConstruction is ported; the tuple-expression render is the
    // remaining loud deferral behind its real C# gate condition (the
    // accessor-call slice, the interpolation slice, the constructor-call
    // slice, and the delegate-reference family are ported now).
    ExpressionWithResolveResult Build(
        IL::OpCode callOpCode, const TS::IMethod& method,
        const std::vector<IL::ILInstruction*>& callArguments,
        const std::optional<std::vector<int>>& argumentToParameterMap = std::nullopt,
        const TS::IType* constrainedTo = nullptr);

    // The C# `static bool IsNullConditional(Expression expr)` (line 1480-1485):
    // a `?.` unary operator expression (the target shape the delegate-invoke
    // arm rejects).
    static bool IsNullConditional(const Syntax::Expression* expr);

    // The C# `private bool IsDelegateEqualityComparison(IMethod method,
    // IList<TranslatedExpression> arguments)` (lines 1523-1534): comparison on
    // a delegate type is a C# builtin operator that compiles down to a
    // Delegate.op_Equality call -- a special case that avoids inserting a
    // cast to System.Delegate. Made public for tests.
    static bool IsDelegateEqualityComparison(
        const TS::IMethod& method, const std::vector<TranslatedExpression>& arguments);

    // The C# `private Expression HandleDelegateEqualityComparison(IMethod
    // method, IList<TranslatedExpression> arguments)` (lines 1536-1543): the
    // plain `a == b` / `a != b` binary render. Made public for tests.
    static Syntax::Expression* HandleDelegateEqualityComparison(
        const TS::IMethod& method, const std::vector<TranslatedExpression>& arguments);

    // The C# `private ExpressionWithResolveResult HandleImplicitConversion(
    // IMethod method, TranslatedExpression argument)` (lines 1545-1565): the
    // op_Implicit user-defined conversion render -- the user-defined check
    // with the argument-type re-cast fallback, the `in`-DirectionExpression
    // unwrap, and the cast over the (possibly re-looked-up) conversion.
    // Made public for tests.
    ExpressionWithResolveResult HandleImplicitConversion(const TS::IMethod& method,
                                                        TranslatedExpression argument);

    // The C# `private static bool IsInterpolatedStringCreation(IMethod method,
    // ArgumentList argumentList)` (lines 755-765): the interpolation gate --
    // a static `string.Format` or a
    // `System.Runtime.CompilerServices.FormattableStringFactory.Create` over
    // a positional-only argument list that is either expanded-form, a
    // non-params overload, or a two-argument array literal. Made public for
    // tests.
    static bool IsInterpolatedStringCreation(const TS::IMethod& method,
                                             const ArgumentList& argumentList);

    // -- The string-interpolation slice (CallBuilder.cs lines 595-648 + 766-935) --

    // The C# `private enum TokenKind` (lines 874-881): the format-string token
    // kinds TokenizeFormatString classifies. Private in the C#; the port's
    // no-visibility-level convention keeps it public (the CallTransformation
    // precedent), int32-backed.
    enum class TokenKind : std::int32_t {
        Error,
        String,
        Argument,
        ArgumentWithFormat,
        ArgumentWithAlignment,
        ArgumentWithAlignmentAndFormat,
    };

    // The C# anonymous tuple `(TokenKind Kind, int Index, int Alignment, string?
    // Format)` the tokens list carries: the token's kind, the 0-based argument
    // slot index (-1 for a String token), the alignment (0 when absent), and
    // the format suffix or the literal text (nullopt for the C# null).
    struct FormatToken {
        TokenKind Kind = TokenKind::Error;
        int Index = 0;
        int Alignment = 0;
        std::optional<std::string> Format;
    };

    // The C# `private IEnumerable<(TokenKind, string?)> TokenizeFormatString(
    // string value)` (lines 883-935): the format-string tokenizer over the
    // `{`/`}`/`:`/`,` state machine -- `{{`/`}}` collapse to doubled literal
    // text, a `{` starts an argument run, a `}` ends it, `:` and `,` refine
    // the run's kind, an unterminated run is the Error token. The C# iterator
    // yields (kind, text) pairs; the text is nullopt for the C# null (the
    // Error token's shape). Made public for tests.
    static std::vector<std::pair<TokenKind, std::optional<std::string>>>
    TokenizeFormatString(const std::string& value);

    // The C# `private bool TryGetStringInterpolationTokens(ArgumentList
    // argumentList, out string? format, out List<(...)>? tokens)` (lines
    // 766-842): the interpolation-token gate over the argument list -- the
    // first argument is the format string (a String-typed compile-time
    // constant), no later argument carries a string literal (a nested literal
    // would make the render untrackable), no argument names, no
    // argument-to-parameter map, and the format's argument slots are
    // consecutive from 0 and exactly fill the remaining arguments. `format`
    // / `tokens` are the C# out parameters: both nullopt on false, both
    // engaged on true (the C# NotNullWhen contract). Made public for tests.
    bool TryGetStringInterpolationTokens(
        const ArgumentList& argumentList, std::optional<std::string>& format,
        std::optional<std::vector<FormatToken>>& tokens) const;

    // The C# `private ExpressionWithResolveResult HandleStringInterpolation(
    // IMethod method, ArgumentList argumentList)` (lines 595-648): the `$"..."
    // render -- the InterpolatedStringExpression over the token stream (each
    // argument token renders an Interpolation over its argument; a trailing
    // single-element array-literal argument is unwrapped into its element),
    // the `string.Format` arm answering the bare interpolation and the
    // `FormattableStringFactory.Create` arm the cast over the
    // ImplicitInterpolatedStringConversion. Returns the default (null
    // expression) when the tokens do not parse or the token list is empty.
    // Made public for tests.
    ExpressionWithResolveResult HandleStringInterpolation(
        const TS::IMethod& method, ArgumentList argumentList);


    // The C# `private bool HandleRangeConstruction(out ExpressionWithResolveResult
    // result, OpCode callOpCode, IMethod method, TranslatedExpression target,
    // ArgumentList argumentList)` (lines 2245-2300): the C# 8 range/index render
    // -- the four Range arms, the Index '^' arm, and the synthetic
    // range-indexer slicing arm. Made public for tests. The ArgumentList is
    // the C# by-value parameter (a copy -- GetArgumentExpressions' fills must
    // not leak into the caller's list).
    static bool HandleRangeConstruction(ExpressionWithResolveResult& result,
                                        IL::OpCode callOpCode, const TS::IMethod& method,
                                        const TranslatedExpression& target,
                                        ArgumentList argumentList);

    // The C# `static MethodGroupResolveResult ToMethodGroup(IMethod method,
    // ILFunction localFunction)` (lines 2199-2210): the local-function method
    // group -- a null target, the function's name, one declaring-type bucket
    // over the method, and the method's type arguments.
    static std::shared_ptr<Resolver::MethodGroupResolveResult> ToMethodGroup(
        const TS::IMethod& method, const IL::ILFunction& localFunction);

    // -- The delegate-reference family (CallBuilder.cs lines 1905-2212) ---------

    // The C# `TranslatedExpression HandleDelegateConstruction(CallInstruction
    // inst)` (lines 1905-1935): the delegate-construction entry -- the ldftn/
    // ldvirtftn arm reading the resolved method, the CanUseDelegateConstruction
    // gate, and the not-usable fallback routing through BuildArgumentList +
    // HandleConstructorCall (a plain `new` over the delegate ctor). Made public
    // for tests (the C# private member). The func node must carry a resolved
    // IMethod (the seed reader's string stand-in drives the C#'s
    // ArgumentException for an unknown opcode arm otherwise).
    TranslatedExpression HandleDelegateConstruction(const IL::Call& inst);

    // The C# `private bool CanUseDelegateConstruction(IMethod targetMethod,
    // ILInstruction thisArg, IMethod invokeMethod)` (lines 1937-1967): the
    // accessors-are-not-method-groups gate, the static arm's parameter-count
    // dance (the invoke-method-known/unknown splits with the extension-method
    // minus-one), and the instance arm's known-invoke gate. Made public for
    // tests. `invokeMethod` is the C# nullable reference: the null maps to
    // nullptr.
    static bool CanUseDelegateConstruction(
        const TS::IMethod& targetMethod, const IL::ILInstruction* thisArg,
        const TS::IMethod* invokeMethod);

    // The C# `private TranslatedExpression HandleDelegateConstruction(IType
    // delegateType, IMethod method, ExpectedTargetDetails expectedTargetDetails,
    // ILInstruction thisArg, ILInstruction inst)` (lines 2138-2152): the
    // delegate-construction render -- BuildDelegateReference over the target
    // method, the ObjectCreateExpression over the delegate type, and the
    // MethodGroupConversion resolve result. Made public for tests.
    TranslatedExpression HandleDelegateConstruction(
        const TS::IType& delegateType, const TS::IMethod& method,
        const ExpectedTargetDetails& expectedTargetDetails,
        IL::ILInstruction* thisArg, IL::ILInstruction* inst);

    // The C# `private ExpressionWithResolveResult BuildDelegateReference(IMethod
    // method, IMethod? invokeMethod, ExpectedTargetDetails expectedTargetDetails,
    // ILInstruction? thisArg)` (lines 2004-2026): the MemberReferenceExpression/
    // IdentifierExpression render over DisambiguateDelegateReference with the
    // type-argument inserts. Made public for tests.
    ExpressionWithResolveResult BuildDelegateReference(
        const TS::IMethod& method, const TS::IMethod* invokeMethod,
        const ExpectedTargetDetails& expectedTargetDetails,
        IL::ILInstruction* thisArg);

    // The C# `private bool IsUnambiguousMethodReference(ExpectedTargetDetails
    // expectedTargetDetails, IMethod method, ResolveResult? target,
    // IReadOnlyList<IType> typeArguments, bool isExtensionMethodReference,
    // out ResolveResult? result)` (lines 2154-2200): the disambiguation
    // oracle -- the extension arm over ResolveMemberAccess +
    // PerformOverloadResolution(allowExtensionMethods) and the general arm
    // over a fresh OverloadResolution fed by ResolveSimpleName/MemberLookup.
    // `target` null maps to nullptr; `result` is the C# out param (null on
    // false). Made public for tests.
    bool IsUnambiguousMethodReference(
        const ExpectedTargetDetails& expectedTargetDetails, const TS::IMethod& method,
        const Sem::ResolveResult* target,
        const std::vector<TS::ITypePtr>& typeArguments,
        bool isExtensionMethodReference,
        std::shared_ptr<Sem::ResolveResult>& result);

    // The C# `internal TranslatedExpression Build(LdVirtDelegate inst)` (lines
    // 1969-1971): the virtual delegate construction render. Made public for
    // tests. The node must carry a resolved IMethod (the seed's string
    // stand-in falls to the C# shape only through the resolved ctor).
    TranslatedExpression Build(const IL::LdVirtDelegate& inst);

    // The C# `internal ExpressionWithResolveResult BuildMethodReference(IMethod
    // method, bool isVirtual)` (lines 1973-1977): the `Callee` method-group
    // identifier render -- BuildDelegateReference with a null thisArg and the
    // resolve-result annotation replaced with a plain MemberResolveResult over
    // a null target. Made public for tests.
    ExpressionWithResolveResult BuildMethodReference(
        const TS::IMethod& method, bool isVirtual);

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
