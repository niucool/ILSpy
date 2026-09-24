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

#include "Decompiler/CSharp/CallBuilder.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/ExpressionBuilder.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/Log.hpp"
#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NullReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringText.hpp"
#include "Decompiler/CSharp/Syntax/Interpolation.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/Semantics/ArrayCreateResolveResult.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/InterpolatedStringResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/Conversion.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/OutVarResolveResult.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/InheritanceHelper.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/Decimal.hpp"

#include <any>
#include <cassert>
#include <charconv>
#include <set>
#include <stdexcept>

namespace ILSpy::Decompiler::CSharp {

// The C# `internal static bool IsSpanBasedStringConcat(IMethod method)`
// (CallBuilder.cs lines 300-318). The C# `method is not { Name: "Concat",
// IsStatic: true }` property-pattern guard ports to the two field checks; the
// C# `DeclaringType.IsKnownType(KnownTypeCode.String)` extension ports to the
// TypeSystemExtensions::IsKnownType free function over the resolved declaring
// type. The C# `p.Type.TypeArguments[0]` reads the generic instantiation's
// element type; the port's TypeArguments surface lives on ParameterizedType (the
// IType base does not carry it), so a non-ParameterizedType parameter type fails
// the span check -- an `IsKnownType(ReadOnlySpanOfT)` parameter type is always a
// closed `ReadOnlySpan<char>`/`ReadOnlySpan<T>` ParameterizedType in practice,
// and the C# would throw on a zero TypeArguments list where the port answers
// false (a C# `p.Type.TypeArguments[0]` ArgumentOutOfRangeException over an
// open generic parameter type; unreachable for a metadata-backed method).
bool CallBuilder::IsSpanBasedStringConcat(const TS::IMethod& method) {
    if (!(method.Name() == "Concat" && method.IsStatic())) {
        return false;
    }
    TS::ITypePtr declaringType = method.DeclaringType();
    if (!declaringType
        || !TS::IsKnownType(*declaringType,
                                    TS::KnownTypeCode::String)) {
        return false;
    }
    for (const TS::IParameter* p : method.Parameters()) {
        if (!p
            || !TS::IsKnownType(
                p->Type(), TS::KnownTypeCode::ReadOnlySpanOfT)) {
            return false;
        }
        auto* parameterized =
            dynamic_cast<const TS::ParameterizedType*>(&p->Type());
        if (!parameterized || parameterized->TypeArguments().empty()) {
            return false;
        }
        if (!TS::IsKnownType(*parameterized->TypeArguments()[0],
                                     TS::KnownTypeCode::Char)) {
            return false;
        }
    }
    return true;
}

// The C# `internal static bool IsStringToReadOnlySpanCharImplicitConversion(
// IMethod method)` (CallBuilder.cs lines 320-328). The `p.Type.TypeArguments[0]`
// element reads the ParameterizedType instantiation (the IsSpanBasedStringConcat
// note).
bool CallBuilder::IsStringToReadOnlySpanCharImplicitConversion(
    const TS::IMethod& method) {
    if (!method.IsOperator() || method.Name() != "op_Implicit")
        return false;
    if (method.Parameters().size() != 1)
        return false;
    const TS::IType& returnType = method.ReturnType();
    if (!TS::IsKnownType(returnType, TS::KnownTypeCode::ReadOnlySpanOfT))
        return false;
    auto* parameterized = dynamic_cast<const TS::ParameterizedType*>(&returnType);
    if (parameterized == nullptr || parameterized->TypeArguments().empty()
        || !TS::IsKnownType(*parameterized->TypeArguments()[0],
                           TS::KnownTypeCode::Char))
        return false;
    const TS::IParameter* parameter = method.Parameters()[0];
    return parameter != nullptr
        && TS::IsKnownType(parameter->Type(), TS::KnownTypeCode::String);
}

// The C# `private static bool IsInterpolatedStringCreation(IMethod method,
// ArgumentList argumentList)` (CallBuilder.cs lines 755-766).
bool CallBuilder::IsInterpolatedStringCreation(const TS::IMethod& method,
                                               const ArgumentList& argumentList) {
    TS::ITypePtr declaring = method.DeclaringType();
    bool isCreation =
        method.IsStatic()
        && ((declaring && TS::IsKnownType(*declaring, TS::KnownTypeCode::String)
             && method.Name() == "Format")
            || (declaring && method.Name() == "Create"
                && declaring->Name() == "FormattableStringFactory"
                && declaring->Namespace() == "System.Runtime.CompilerServices"));
    if (!isCreation)
        return false;
    if (argumentList.ArgumentNames.has_value())
        return false;
    const auto& parameters = method.Parameters();
    bool lastIsParams = !parameters.empty() && parameters.back() != nullptr
        && parameters.back()->IsParams();
    return argumentList.IsExpandedForm
        || !lastIsParams
        || (argumentList.Length() == 2
            && dynamic_cast<Syntax::ArrayCreateExpression*>(
                   argumentList.Arguments[1].Expression()) != nullptr);
}

// The C# `static bool IsNullConditional(Expression expr)` (CallBuilder.cs
// lines 1480-1483).
bool CallBuilder::IsNullConditional(const Syntax::Expression* expr) {
    auto* unary = dynamic_cast<const Syntax::UnaryOperatorExpression*>(expr);
    return unary != nullptr
        && unary->Operator() == Syntax::UnaryOperatorType::NullConditional;
}

// The C# `private ExpressionWithResolveResult BuildStringConcat(IMethod method,
// List<(ILInstruction Instruction, KnownTypeCode TypeCode)> operands)`
// (CallBuilder.cs lines 252-266). The first operand translates to its element
// type and seeds the chain; each further operand translates and folds in with
// a left-associative `+` over the single shared MemberResolveResult (the C#
// binds every BinaryOperatorExpression to the same `rr`). The caller (the
// Build wrapper) attaches the IL-instruction annotation.
ExpressionWithResolveResult CallBuilder::BuildStringConcat(
    const TS::IMethod& method,
    const std::vector<SpanConcatOperand>& operands) {
    assert(!operands.empty());
    TS::IType& firstType = const_cast<TS::IType&>(
        expressionBuilder_->compilation->FindType(operands[0].TypeCode));
    TranslatedExpression first =
        expressionBuilder_->Translate(operands[0].Instruction, &firstType)
            .ConvertTo(firstType, *expressionBuilder_);
    ExpressionWithResolveResult result(first.Expression(), first.ResolveResult());
    auto rr = std::make_shared<Sem::MemberResolveResult>(
        std::shared_ptr<Sem::ResolveResult>(), &method);

    for (std::size_t i = 1; i < operands.size(); ++i) {
        TS::IType& type = const_cast<TS::IType&>(
            expressionBuilder_->compilation->FindType(operands[i].TypeCode));
        TranslatedExpression expr =
            expressionBuilder_->Translate(operands[i].Instruction, &type)
                .ConvertTo(type, *expressionBuilder_);
        result = WithRR(*new Syntax::BinaryOperatorExpression(
                            result.Expression(), Syntax::BinaryOperatorType::Add,
                            expr.Expression()),
                        rr);
    }

    return result;
}

// The C# `static bool IsSpanBasedStringConcat(CallInstruction call,
// [NotNullWhen(true)] out List<(ILInstruction, KnownTypeCode)>? operands)`
// (CallBuilder.cs lines 268-298). Each argument is matched in turn; the first
// string-typed operand's `ChildIndex` (its argument position -- the port's
// Call::AddArg wires ChildIndex to the argument index, the same value the C#
// reads) is captured on the first string arm (`??=`) and the shape holds when
// at least two arguments matched and that index is 0 or 1.
bool CallBuilder::IsSpanBasedStringConcat(const IL::Call& call,
                                          std::vector<SpanConcatOperand>& operands) {
    operands.clear();

    if (call.Method == nullptr || !IsSpanBasedStringConcat(*call.Method)) {
        return false;
    }

    std::optional<int> firstStringArgumentIndex;

    for (const std::unique_ptr<IL::ILInstruction>& arg : call.Arguments) {
        if (auto* opImplicit = dynamic_cast<const IL::Call*>(arg.get());
            opImplicit != nullptr && opImplicit->Method != nullptr
            && IsStringToReadOnlySpanCharImplicitConversion(*opImplicit->Method)) {
            if (!firstStringArgumentIndex)
                firstStringArgumentIndex = arg->ChildIndex;
            if (opImplicit->Arguments.size() != 1 || !opImplicit->Arguments[0])
                return false;
            operands.push_back({opImplicit->Arguments[0].get(),
                                TS::KnownTypeCode::String});
        } else if (auto* newObj = dynamic_cast<const IL::Call*>(arg.get());
                   newObj != nullptr && newObj->IsNewObj
                   && newObj->Arguments.size() == 1
                   && newObj->Arguments[0] != nullptr) {
            auto* addressOf =
                dynamic_cast<const IL::AddressOf*>(newObj->Arguments[0].get());
            if (addressOf == nullptr || !newObj->Method
                || !IL::IsReadOnlySpanCharCtor(*newObj->Method)) {
                return false;
            }
            operands.push_back({addressOf->Value.get(), TS::KnownTypeCode::Char});
        } else {
            return false;
        }
    }

    return call.Arguments.size() >= 2 && firstStringArgumentIndex.has_value()
        && *firstStringArgumentIndex <= 1;
}

// The C# `private bool IsDelegateEqualityComparison(IMethod method,
// IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1511-1523).
bool CallBuilder::IsDelegateEqualityComparison(
    const TS::IMethod& method,
    const std::vector<TranslatedExpression>& arguments) {
    if (!method.IsOperator())
        return false;
    TS::ITypePtr declaring = method.DeclaringType();
    if (!declaring || !TS::IsKnownType(*declaring, TS::KnownTypeCode::Delegate))
        return false;
    if (method.Name() != "op_Equality" && method.Name() != "op_Inequality")
        return false;
    if (arguments.size() != 2)
        return false;
    if (arguments[0].Type().Kind() != TS::TypeKind::Delegate)
        return false;
    return arguments[1].Type().Equals(arguments[0].Type());
}

// The C# `private IEnumerable<(TokenKind, string?)> TokenizeFormatString(
// string value)` (CallBuilder.cs lines 841-941): the format-string tokenizer.
// The C# iterator's `Peek`/`Next` local functions become lambdas over the scan
// position; the yields materialize into the returned vector (the ToVector
// convention). `{{` opens a literal `{{` run and `}}` escapes a close brace;
// an unterminated hole or a stray `}` yields an Error token.
std::vector<std::pair<CallBuilder::TokenKind, std::optional<std::string>>>
CallBuilder::TokenizeFormatString(const std::string& value) {
    std::vector<std::pair<TokenKind, std::optional<std::string>>> tokens;
    int pos = -1;
    auto Peek = [&](int steps = 1) -> int {
        if (pos + steps < static_cast<int>(value.size()))
            return static_cast<unsigned char>(
                value[static_cast<std::size_t>(pos + steps)]);
        return -1;
    };
    auto Next = [&]() -> int {
        int val = Peek();
        pos++;
        return val;
    };
    int next;
    TokenKind kind = TokenKind::String;
    std::string sb;
    while ((next = Next()) > -1) {
        switch (static_cast<char>(next)) {
            case '{':
                if (Peek() == '{') {
                    kind = TokenKind::String;
                    sb += "{{";
                    Next();
                } else {
                    if (!sb.empty()) tokens.emplace_back(kind, sb);
                    kind = TokenKind::Argument;
                    sb.clear();
                }
                break;
            case '}':
                if (kind != TokenKind::String) {
                    tokens.emplace_back(kind, sb);
                    sb.clear();
                    kind = TokenKind::String;
                } else if (Peek() == '}') {
                    sb += "}}";
                    Next();
                } else {
                    tokens.emplace_back(TokenKind::Error, std::nullopt);
                }
                break;
            case ':':
                if (kind == TokenKind::Argument)
                    kind = TokenKind::ArgumentWithFormat;
                else if (kind == TokenKind::ArgumentWithAlignment)
                    kind = TokenKind::ArgumentWithAlignmentAndFormat;
                sb += ':';
                break;
            case ',':
                if (kind == TokenKind::Argument)
                    kind = TokenKind::ArgumentWithAlignment;
                sb += ',';
                break;
            default:
                sb += static_cast<char>(next);
                break;
        }
    }
    if (!sb.empty()) {
        if (kind == TokenKind::String)
            tokens.emplace_back(kind, sb);
        else
            tokens.emplace_back(TokenKind::Error, std::nullopt);
    }
    return tokens;
}

// The int.TryParse equivalent the token parsing uses (the C# TryParse never
// throws; std::from_chars reports failure without exceptions).
static bool TryParseInt(const std::string& text, int& out) {
    const char* first = text.data();
    const char* last = first + text.size();
    auto [ptr, ec] = std::from_chars(first, last, out);
    return ec == std::errc() && ptr == last;
}

// The C# `string.Split(char[], int)` port: split on any of `separators`,
// producing at most `maxParts` pieces (the last piece keeps the remainder).
static std::vector<std::string> SplitAny(const std::string& text,
                                         const std::string& separators,
                                         int maxParts) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (static_cast<int>(parts.size()) + 1 < maxParts) {
        std::size_t hit = text.find_first_of(separators, start);
        if (hit == std::string::npos) break;
        parts.push_back(text.substr(start, hit - start));
        start = hit + 1;
    }
    parts.push_back(text.substr(start));
    return parts;
}

// The C# `private bool TryGetStringInterpolationTokens(ArgumentList
// argumentList, out string? format, out List<...> tokens)` (CallBuilder.cs
// lines 770-838). The C# `arguments.Skip(1).All(a =>
// !a.Expression.DescendantsAndSelf.OfType<PrimitiveExpression>().Any(p =>
// p.Value is string))` rejects a string literal among the non-format
// arguments (it would be double-counted). The C# `(string)crr.ConstantValue!`
// cast ports to the materialize-then-any_cast form (the ConstantValue returns
// `std::any` by value).
bool CallBuilder::TryGetStringInterpolationTokens(
    const ArgumentList& argumentList, std::string& format,
    std::vector<InterpolationToken>& tokens) {
    tokens.clear();
    format.clear();
    const std::vector<TranslatedExpression>& arguments = argumentList.Arguments;
    if (arguments.empty() || argumentList.ArgumentNames.has_value()
        || argumentList.ArgumentToParameterMap.has_value())
        return false;
    const auto* crr = dynamic_cast<const Sem::ConstantResolveResult*>(
        arguments[0].ResolveResult());
    if (crr == nullptr
        || !TS::IsKnownType(crr->Type(), TS::KnownTypeCode::String))
        return false;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        for (const Syntax::AstNode* node :
             arguments[i].Expression()->DescendantsAndSelf()) {
            auto* primitive = dynamic_cast<const Syntax::PrimitiveExpression*>(node);
            if (primitive != nullptr
                && std::holds_alternative<std::string>(primitive->Value()))
                return false;
        }
    }
    std::any constant = crr->ConstantValue();
    const std::string* formatValue = std::any_cast<std::string>(&constant);
    if (formatValue == nullptr)
        return false;
    format = *formatValue;
    int i = 0;
    for (const auto& [kind, data] : TokenizeFormatString(format)) {
        int index = 0;
        switch (kind) {
            case TokenKind::Error:
                return false;
            case TokenKind::String:
                tokens.push_back({kind, -1, 0, data});
                break;
            case TokenKind::Argument:
                if (!data || !TryParseInt(*data, index) || index != i)
                    return false;
                i++;
                tokens.push_back({kind, index, 0, std::nullopt});
                break;
            case TokenKind::ArgumentWithFormat: {
                std::vector<std::string> parts = SplitAny(*data, ":", 2);
                if (parts.size() != 2 || parts[1].empty())
                    return false;
                if (!TryParseInt(parts[0], index) || index != i)
                    return false;
                i++;
                tokens.push_back({kind, index, 0, parts[1]});
                break;
            }
            case TokenKind::ArgumentWithAlignment: {
                std::vector<std::string> parts = SplitAny(*data, ",", 2);
                if (parts.size() != 2 || parts[1].empty())
                    return false;
                if (!TryParseInt(parts[0], index) || index != i)
                    return false;
                int alignment = 0;
                if (!TryParseInt(parts[1], alignment))
                    return false;
                i++;
                tokens.push_back({kind, index, alignment, std::nullopt});
                break;
            }
            case TokenKind::ArgumentWithAlignmentAndFormat: {
                std::vector<std::string> parts = SplitAny(*data, ",:", 3);
                if (parts.size() != 3 || parts[1].empty() || parts[2].empty())
                    return false;
                if (!TryParseInt(parts[0], index) || index != i)
                    return false;
                int alignment = 0;
                if (!TryParseInt(parts[1], alignment))
                    return false;
                i++;
                tokens.push_back({kind, index, alignment, parts[2]});
                break;
            }
            default:
                return false;
        }
    }
    return i == static_cast<int>(arguments.size()) - 1;
}

// The C# `private ExpressionWithResolveResult HandleStringInterpolation(IMethod
// method, ArgumentList argumentList)` (CallBuilder.cs lines 595-666). The C#
// local function `UnpackSingleElementArray` ports to a lambda over the
// `argumentList` capture. The C# `WithRR` attachments carry the shared handle;
// the InterpolatedStringResolveResult is built over the argument resolve
// results past the format string (skipCount 1).
ExpressionWithResolveResult CallBuilder::HandleStringInterpolation(
    const TS::IMethod& method, const ArgumentList& argumentList) {
    std::string format;
    std::vector<InterpolationToken> tokens;
    if (!TryGetStringInterpolationTokens(argumentList, format, tokens))
        return ExpressionWithResolveResult{};

    const std::vector<TranslatedExpression>& arguments = argumentList.Arguments;
    std::vector<Syntax::InterpolatedStringContent*> content;

    bool unpackSingleElementArray = !argumentList.IsExpandedForm
        && argumentList.Length() == 2
        && dynamic_cast<const Syntax::ArrayCreateExpression*>(
               argumentList.Arguments[1].Expression()) != nullptr
        && dynamic_cast<const Syntax::ArrayCreateExpression*>(
               argumentList.Arguments[1].Expression())
               ->Initializer()
        != nullptr
        && dynamic_cast<const Syntax::ArrayCreateExpression*>(
               argumentList.Arguments[1].Expression())
               ->Initializer()
               ->Elements()
               .Count()
        == 1;

    auto UnpackSingleElementArray = [&](TranslatedExpression& argument) {
        if (!unpackSingleElementArray)
            return;
        auto* arrayCreation =
            static_cast<Syntax::ArrayCreateExpression*>(argumentList.Arguments[1].Expression());
        const auto* arrayCreationRR =
            static_cast<const Sem::ArrayCreateResolveResult*>(argumentList.Arguments[1].ResolveResult());
        Syntax::Expression* element =
            Syntax::Detach(arrayCreation->Initializer()->Elements().At(0));
        argument = TranslatedExpression(
            element, (*arrayCreationRR->InitializerElements())[0].get());
    };

    if (tokens.empty()) {
        return ExpressionWithResolveResult{};
    }

    for (const InterpolationToken& token : tokens) {
        TranslatedExpression argument;
        switch (token.Kind) {
            case TokenKind::String:
                content.push_back(new Syntax::InterpolatedStringText(*token.Format));
                break;
            case TokenKind::Argument:
                argument = arguments[token.Index + 1];
                UnpackSingleElementArray(argument);
                content.push_back(new Syntax::Interpolation(argument.Expression()));
                break;
            case TokenKind::ArgumentWithFormat:
                argument = arguments[token.Index + 1];
                UnpackSingleElementArray(argument);
                content.push_back(new Syntax::Interpolation(
                    argument.Expression(), 0, token.Format));
                break;
            case TokenKind::ArgumentWithAlignment:
                argument = arguments[token.Index + 1];
                UnpackSingleElementArray(argument);
                content.push_back(new Syntax::Interpolation(
                    argument.Expression(), token.Alignment));
                break;
            case TokenKind::ArgumentWithAlignmentAndFormat:
                argument = arguments[token.Index + 1];
                UnpackSingleElementArray(argument);
                content.push_back(new Syntax::Interpolation(
                    argument.Expression(), token.Alignment, token.Format));
                break;
            default:
                break;
        }
    }
    TS::IType& formattableStringType = const_cast<TS::IType&>(
        expressionBuilder_->compilation->FindType(TS::KnownTypeCode::FormattableString));
    auto isrr = std::make_shared<Sem::InterpolatedStringResolveResult>(
        const_cast<TS::IType&>(
            expressionBuilder_->compilation->FindType(TS::KnownTypeCode::String))
            .shared_from_this(),
        format, argumentList.GetArgumentResolveResults(1));
    auto* expr = new Syntax::InterpolatedStringExpression();
    for (Syntax::InterpolatedStringContent* c : content) expr->Content().Add(c);
    if (method.Name() == "Format")
        return WithRR(*expr, std::move(isrr));
    return WithRR(
        *new Syntax::CastExpression(expressionBuilder_->ConvertType(formattableStringType),
                                    WithRR(*expr, isrr).Expression()),
        std::make_shared<Sem::ConversionResolveResult>(
            const_cast<TS::IType&>(formattableStringType).shared_from_this(), isrr,
            Sem::Conversions::ImplicitInterpolatedStringConversion()));
}

// The C# `private Expression HandleDelegateEqualityComparison(IMethod method,
// IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1524-1532).
Syntax::Expression* CallBuilder::HandleDelegateEqualityComparison(
    const TS::IMethod& method,
    const std::vector<TranslatedExpression>& arguments) {
    return new Syntax::BinaryOperatorExpression(
        arguments[0].Expression(),
        method.Name() == "op_Equality" ? Syntax::BinaryOperatorType::Equality
                                       : Syntax::BinaryOperatorType::InEquality,
        arguments[1].Expression());
}

// The C# `bool IsAppropriateCallTarget(ExpectedTargetDetails expectedTargetDetails,
// IMember expectedTarget, IMember actualTarget)` (CallBuilder.cs lines
// 1816-1835).
bool CallBuilder::IsAppropriateCallTarget(
    const ExpectedTargetDetails& expectedTargetDetails,
    const TS::IMember& expectedTarget, const TS::IMember& actualTarget) {
    if (expectedTarget.Equals(&actualTarget, &TS::NormalizeTypeVisitor::TypeErasure()))
        return true;

    if (expectedTargetDetails.CallOpCode == IL::OpCode::CallVirt
        && actualTarget.IsOverride()) {
        TS::ITypePtr declaring = actualTarget.DeclaringType();
        if (expectedTargetDetails.NeedsBoxingConversion && declaring
            && declaring->IsReferenceType() != true)
            return false;
        for (const TS::IMember* possibleTarget : TS::InheritanceHelper::GetBaseMembers(
                 actualTarget, /*includeImplementedInterfaces=*/false)) {
            if (expectedTarget.Equals(possibleTarget,
                                      &TS::NormalizeTypeVisitor::TypeErasure()))
                return true;
            if (!possibleTarget->IsOverride())
                break;
        }
    }
    return false;
}

// The C# `private ExpressionWithResolveResult HandleImplicitConversion(IMethod method,
// TranslatedExpression argument)` (CallBuilder.cs lines 1534-1556).
ExpressionWithResolveResult CallBuilder::HandleImplicitConversion(
    const TS::IMethod& method, TranslatedExpression argument) {
    Resolver::CSharpConversions& conversions =
        Resolver::CSharpConversions::Get(*expressionBuilder_->compilation);
    TS::IType& targetType = const_cast<TS::IType&>(method.ReturnType());
    std::shared_ptr<Sem::Conversion> conv = conversions.ImplicitConversion(
        const_cast<TS::IType&>(argument.Type()), targetType);
    const TS::IMethod* convMethod = conv ? conv->Method() : nullptr;
    if (!(conv && conv->IsUserDefined() && conv->IsValid() && convMethod != nullptr
          && convMethod->Equals(&method, &TS::NormalizeTypeVisitor::TypeErasure()))) {
        // The implicit conversion to the target type is not directly possible, so
        // first insert a cast to the operator's source (parameter) type.
        const TS::IParameter* parameter =
            method.Parameters().empty() ? nullptr : method.Parameters()[0];
        if (parameter != nullptr) {
            argument = argument.ConvertTo(const_cast<TS::IType&>(parameter->Type()),
                                          *expressionBuilder_);
        }
        conv = conversions.ImplicitConversion(const_cast<TS::IType&>(argument.Type()),
                                              targetType);
    }
    auto* direction = dynamic_cast<Syntax::DirectionExpression*>(argument.Expression());
    if (direction != nullptr && direction->FieldDirection() == Syntax::FieldDirection::In) {
        // `(TargetType)(in arg)` is invalid syntax; also, `f(in arg)` is invalid when
        // there is an implicit conversion involved.
        argument = argument.UnwrapChild(direction->Expression());
    }
    auto* cast = new Syntax::CastExpression(expressionBuilder_->ConvertType(targetType),
                                            argument.Expression());
    std::shared_ptr<Sem::ResolveResult> input =
        GetSharedResolveResult(*argument.Expression());
    if (input == nullptr)
        input = std::make_shared<Sem::ResolveResult>(
            const_cast<TS::IType&>(argument.Type()).shared_from_this());
    return WithRR(*cast, std::make_shared<Sem::ConversionResolveResult>(
                             targetType.shared_from_this(), std::move(input),
                             std::move(conv)));
}

// The C# `object.Equals(a, b)` over two boxed constant values (the
// IsOptionalArgument default comparison): both null (empty `std::any`) are
// equal, a null/non-null pair is not, a runtime-type mismatch is not, and
// same-typed primitives / strings / decimals compare by value. The
// CSharpOperators EqualsBoxedValues set plus the empty-any arm (that helper
// never sees the null shapes).
bool BoxedConstantEquals(const std::any& a, const std::any& b) {
    if (a.has_value() != b.has_value())
        return false;
    if (!a.has_value())
        return true;
    if (a.type() != b.type())
        return false;
    if (const bool* v = std::any_cast<bool>(&a))
        return *v == std::any_cast<bool>(b);
    if (const char16_t* v = std::any_cast<char16_t>(&a))
        return *v == std::any_cast<char16_t>(b);
    if (const std::int8_t* v = std::any_cast<std::int8_t>(&a))
        return *v == std::any_cast<std::int8_t>(b);
    if (const std::uint8_t* v = std::any_cast<std::uint8_t>(&a))
        return *v == std::any_cast<std::uint8_t>(b);
    if (const std::int16_t* v = std::any_cast<std::int16_t>(&a))
        return *v == std::any_cast<std::int16_t>(b);
    if (const std::uint16_t* v = std::any_cast<std::uint16_t>(&a))
        return *v == std::any_cast<std::uint16_t>(b);
    if (const std::int32_t* v = std::any_cast<std::int32_t>(&a))
        return *v == std::any_cast<std::int32_t>(b);
    if (const std::uint32_t* v = std::any_cast<std::uint32_t>(&a))
        return *v == std::any_cast<std::uint32_t>(b);
    if (const std::int64_t* v = std::any_cast<std::int64_t>(&a))
        return *v == std::any_cast<std::int64_t>(b);
    if (const std::uint64_t* v = std::any_cast<std::uint64_t>(&a))
        return *v == std::any_cast<std::uint64_t>(b);
    if (const float* v = std::any_cast<float>(&a))
        return *v == std::any_cast<float>(b);
    if (const double* v = std::any_cast<double>(&a))
        return *v == std::any_cast<double>(b);
    if (const Util::Decimal* v = std::any_cast<Util::Decimal>(&a))
        return Util::CompareDecimal(*v, std::any_cast<Util::Decimal>(b)) == 0;
    if (const std::string* v = std::any_cast<std::string>(&a))
        return *v == std::any_cast<std::string>(b);
    if (const TS::ITypePtr* v = std::any_cast<TS::ITypePtr>(&a))
        return *v == std::any_cast<TS::ITypePtr>(b);
    return false;
}

// ---------------------------------------------------------------------------
// CallBuilder instance (CallBuilder.cs constructor + BuildArgumentList)
// ---------------------------------------------------------------------------

CallBuilder::CallBuilder(ExpressionBuilder& expressionBuilder,
                         const DecompilerSettings& settings)
    : expressionBuilder_(&expressionBuilder), settings_(&settings) {}

// The C# `private ArgumentList BuildArgumentList(...)` (CallBuilder.cs lines
// 941-1052). The positional path is ported; the named-argument
// (`argumentToParameterMap`) path and the params-expansion
// (`TransformParamsArgument`) path throw the loud deferral (both need the
// unported overload-resolution machinery). The `target` parameter feeds only
// TransformParamsArgument, so it is unused on the ported path.
CallBuilder::ArgumentList CallBuilder::BuildArgumentList(
    const ExpectedTargetDetails& expectedTargetDetails, const Sem::ResolveResult* target,
    const TS::IMethod& method, int firstParamIndex,
    const std::vector<IL::ILInstruction*>& callArguments,
    const std::optional<std::vector<int>>& argumentToParameterMap)
{
    if (argumentToParameterMap.has_value())
    {
        throw std::logic_error(
            "CallBuilder::BuildArgumentList: the named-argument "
            "(argumentToParameterMap) path is not yet ported");
    }
    (void)target;
    const auto& parameters = method.Parameters();
    assert(static_cast<int>(callArguments.size())
           == firstParamIndex + static_cast<int>(parameters.size()));

    ArgumentList list;
    std::vector<TranslatedExpression> arguments;
    arguments.reserve(parameters.size());
    std::vector<const TS::IParameter*> expectedParameters;
    expectedParameters.reserve(parameters.size());
    bool isExpandedForm = false;
    Util::BitSet isPrimitiveValue(static_cast<int>(parameters.size()));

    // Optional arguments: -2 = none, -1 = forbidden, >= 0 = the first argument
    // that may be removed (the default value of the parameter).
    int firstOptionalArgumentIndex = settings_->OptionalArguments() ? -2 : -1;
    for (int i = firstParamIndex; i < static_cast<int>(callArguments.size()); ++i)
    {
        const TS::IParameter* parameter = parameters[i - firstParamIndex];
        TranslatedExpression arg =
            expressionBuilder_->Translate(callArguments[i], &parameter->Type());
        if (IsPrimitiveValueThatShouldBeNamedArgument(arg, method, *parameter))
            isPrimitiveValue.Set(static_cast<int>(arguments.size()));
        if (IsOptionalArgument(*parameter, arg))
        {
            if (firstOptionalArgumentIndex == -2)
                firstOptionalArgumentIndex = i - firstParamIndex;
        }
        else
        {
            if (firstOptionalArgumentIndex != -1)
                firstOptionalArgumentIndex = -2;
        }
        if (settings_->ExpandParamsArguments() && parameter->IsParams()
            && i + 1 == static_cast<int>(callArguments.size()))
        {
            if (TransformParamsArgument(expectedTargetDetails, target, method, *parameter,
                                        arg, expectedParameters, arguments))
            {
                firstOptionalArgumentIndex = -1;
                isExpandedForm = true;
                continue;
            }
        }

        const TS::IType* parameterType;
        if (parameter->Type().Kind() == TS::TypeKind::Dynamic)
            parameterType = &expressionBuilder_->compilation->FindType(
                TS::KnownTypeCode::Object);
        else
            parameterType = &parameter->Type();

        bool allowImplicitConversion = arg.Type().Kind() != TS::TypeKind::Dynamic;
        arg = arg.ConvertTo(const_cast<TS::IType&>(*parameterType), *expressionBuilder_,
                            /*checkForOverflow=*/false, allowImplicitConversion);

        if (parameter->ReferenceKind() != TS::ReferenceKind::None)
        {
            arg = ExpressionBuilder::ChangeDirectionExpressionTo(
                arg, parameter->ReferenceKind(),
                callArguments[i]->Op == IL::OpCode::AddressOf);
        }

        arguments.push_back(std::move(arg));
        expectedParameters.push_back(parameter);
    }

    list.Arguments = std::move(arguments);
    list.ExpectedParameters = std::move(expectedParameters);
    for (const TS::IParameter* p : list.ExpectedParameters)
        list.ParameterNames.push_back(p != nullptr ? p->Name() : std::string());
    list.ArgumentNames = std::nullopt;
    list.ArgumentToParameterMap = std::nullopt;
    list.IsExpandedForm = isExpandedForm;
    list.IsPrimitiveValue = std::move(isPrimitiveValue);
    list.FirstOptionalArgumentIndex = firstOptionalArgumentIndex;
    list.UseImplicitlyTypedOut = true;
    list.AddNamesToPrimitiveValues =
        settings_->NamedArguments() && settings_->NonTrailingNamedArguments();
    return list;
}

// The C# `private bool IsPrimitiveValueThatShouldBeNamedArgument(...)`
// (CallBuilder.cs lines 1054-1060).
bool CallBuilder::IsPrimitiveValueThatShouldBeNamedArgument(
    const TranslatedExpression& arg, const TS::IMethod& method,
    const TS::IParameter& p) const
{
    const Sem::ResolveResult* rr = arg.ResolveResult();
    if (rr == nullptr || !rr->IsCompileTimeConstant())
        return false;
    TS::ITypePtr declaringType = method.DeclaringType();
    if (declaringType
        && TS::IsKnownType(*declaringType, TS::KnownTypeCode::NullableOfT))
        return false;
    return TS::IsKnownType(p.Type(), TS::KnownTypeCode::Boolean);
}

// The C# `bool IsOptionalArgument(IParameter parameter, TranslatedExpression
// arg)` (CallBuilder.cs lines 1128-1141).
bool CallBuilder::IsOptionalArgument(const TS::IParameter& parameter,
                                     const TranslatedExpression& arg) const
{
    if (!parameter.IsOptional())
        return false;

    const Sem::ResolveResult* rr = arg.ResolveResult();
    bool nullLiteralConversion = false;
    if (auto* crr = dynamic_cast<const Sem::ConversionResolveResult*>(rr))
    {
        const Sem::Conversion* conversion = crr->ConversionProperty();
        nullLiteralConversion = conversion != nullptr
            && conversion->IsNullLiteralConversion();
    }
    if (!(rr != nullptr && rr->IsCompileTimeConstant()) && !nullLiteralConversion)
        return false;

    for (const TS::IAttribute* attribute : parameter.GetAttributes())
    {
        if (attribute == nullptr)
            continue;
        const TS::IType& attributeType = attribute->AttributeType();
        if (TS::IsKnownType(attributeType, TS::KnownAttribute::CallerMemberName)
            || TS::IsKnownType(attributeType, TS::KnownAttribute::CallerFilePath)
            || TS::IsKnownType(attributeType, TS::KnownAttribute::CallerLineNumber))
            return false;
    }
    return BoxedConstantEquals(parameter.GetConstantValue(), rr->ConstantValue());
}

// The C# `private bool TransformParamsArgument(...)` (CallBuilder.cs lines
// 1062-1126): deferred loudly. It needs the unported overload-resolution
// `IsUnambiguousCall` plus the CSharpInvocationResolveResult / array-create
// resolve-result arms; the positional non-params path never reaches it.
bool CallBuilder::TransformParamsArgument(
    const ExpectedTargetDetails& expectedTargetDetails,
    const Sem::ResolveResult* targetResolveResult, const TS::IMethod& method,
    const TS::IParameter& parameter, const TranslatedExpression& paramsArgument,
    std::vector<const TS::IParameter*>& expectedParameters,
    std::vector<TranslatedExpression>& arguments)
{
    (void)expectedTargetDetails;
    (void)targetResolveResult;
    (void)method;
    (void)parameter;
    (void)paramsArgument;
    (void)expectedParameters;
    (void)arguments;
    throw std::logic_error(
        "CallBuilder::TransformParamsArgument: the params-expansion path is not "
        "yet ported (needs the overload-resolution IsUnambiguousCall machinery)");
}

// ---------------------------------------------------------------------------
// CallBuilder::ArgumentList (CallBuilder.cs lines 48-200)
// ---------------------------------------------------------------------------

int CallBuilder::ArgumentList::GetActualArgumentCount() const
{
    if (FirstOptionalArgumentIndex < 0)
        return static_cast<int>(Arguments.size());
    return FirstOptionalArgumentIndex;
}

std::optional<std::vector<std::string>> CallBuilder::ArgumentList::GetArgumentNames(
    int skipCount) const
{
    std::optional<std::vector<std::string>> argumentNames = ArgumentNames;
    bool allParameterNamesNonEmpty = true;
    for (const std::string& name : ParameterNames)
    {
        if (name.empty())
        {
            allParameterNamesNonEmpty = false;
            break;
        }
    }
    if (AddNamesToPrimitiveValues && IsPrimitiveValue.Any() && !IsExpandedForm
        && allParameterNamesNonEmpty)
    {
        assert(skipCount == 0);
        if (!argumentNames.has_value())
            argumentNames = std::vector<std::string>(Arguments.size());
        for (std::size_t i = 0; i < Arguments.size(); ++i)
        {
            if (IsPrimitiveValue[static_cast<int>(i)] && (*argumentNames)[i].empty())
                (*argumentNames)[i] = ParameterNames[i];
        }
    }
    return argumentNames;
}

std::vector<std::shared_ptr<Sem::ResolveResult>>
CallBuilder::ArgumentList::GetArgumentResolveResults(int skipCount) const
{
    std::vector<std::shared_ptr<Sem::ResolveResult>> result;
    int count = GetActualArgumentCount();
    for (int i = skipCount; i < count; ++i)
    {
        const TranslatedExpression& expression = Arguments[i];
        const TS::IParameter* param = ExpectedParameters[i];
        if (UseImplicitlyTypedOut && param != nullptr
            && param->ReferenceKind() == TS::ReferenceKind::Out)
        {
            auto* byRef = dynamic_cast<TS::ByReferenceType*>(
                &const_cast<TS::IType&>(expression.Type()));
            if (byRef != nullptr)
            {
                result.push_back(std::make_shared<Sem::OutVarResolveResult>(byRef->Element()));
                continue;
            }
        }
        result.push_back(GetSharedResolveResult(*expression.Expression()));
    }
    return result;
}

std::vector<std::shared_ptr<Sem::ResolveResult>>
CallBuilder::ArgumentList::GetArgumentResolveResultsDirect(int skipCount) const
{
    std::vector<std::shared_ptr<Sem::ResolveResult>> result;
    int count = GetActualArgumentCount();
    for (int i = skipCount; i < count; ++i)
        result.push_back(GetSharedResolveResult(*Arguments[i].Expression()));
    return result;
}

std::vector<Syntax::Expression*> CallBuilder::ArgumentList::GetArgumentExpressions(
    int skipCount) const
{
    std::optional<std::vector<std::string>> argumentNames = GetArgumentNames(skipCount);
    int argumentCount = GetActualArgumentCount();
    auto addAnnotations = [&](Syntax::Expression* expression) -> Syntax::Expression* {
        if (!UseImplicitlyTypedOut)
            return expression;
        if (auto* brrr = dynamic_cast<const Sem::ByReferenceResolveResult*>(
                GetResolveResult(*expression));
            brrr != nullptr && brrr->ReferenceKind() == TS::ReferenceKind::Out)
        {
            expression->AddAnnotation(UseImplicitlyTypedOutAnnotationHandle());
        }
        return expression;
    };
    std::vector<Syntax::Expression*> result;
    if (!argumentNames.has_value())
    {
        for (int i = skipCount; i < argumentCount; ++i)
            result.push_back(addAnnotations(Arguments[i].Expression()));
    }
    else
    {
        assert(skipCount == 0);
        int nameCount = static_cast<int>(argumentNames->size());
        int end = argumentCount < nameCount ? argumentCount : nameCount;
        for (int i = 0; i < end; ++i)
        {
            Syntax::Expression* expression = addAnnotations(Arguments[i].Expression());
            const std::string& name = (*argumentNames)[i];
            if (name.empty())
                result.push_back(expression);
            else
                result.push_back(new Syntax::NamedArgumentExpression(name, expression));
        }
    }
    return result;
}

bool CallBuilder::ArgumentList::CanInferAnonymousTypePropertyNamesFromArguments() const
{
    for (std::size_t i = 0; i < Arguments.size(); ++i)
    {
        std::string inferredName;
        if (auto* ident =
                dynamic_cast<Syntax::IdentifierExpression*>(Arguments[i].Expression()))
            inferredName = ident->Identifier();
        else if (auto* member = dynamic_cast<Syntax::MemberReferenceExpression*>(
                     Arguments[i].Expression()))
            inferredName = member->MemberName();
        if (ExpectedParameters[i] == nullptr
            || inferredName != ExpectedParameters[i]->Name())
            return false;
    }
    return true;
}

void CallBuilder::ArgumentList::CheckNoNamedOrOptionalArguments() const
{
    assert(!ArgumentToParameterMap.has_value() && !ArgumentNames.has_value()
           && FirstOptionalArgumentIndex < 0);
}


// ---------------------------------------------------------------------------
// The overload-resolution front end (CallBuilder.cs lines 1138-1556)
// ---------------------------------------------------------------------------

// The C# `private bool IsPossibleExtensionMethodCallOnNull(IMethod method,
// IList<TranslatedExpression> arguments)` (CallBuilder.cs lines 1369-1372).
bool CallBuilder::IsPossibleExtensionMethodCallOnNull(
    const TS::IMethod& method, const std::vector<TranslatedExpression>& arguments) {
    return method.IsExtensionMethod() && !arguments.empty()
        && dynamic_cast<const Syntax::NullReferenceExpression*>(
               arguments[0].Expression())
        != nullptr;
}

// The C# `static bool CanInferTypeArgumentsFromArguments(IMethod method,
// ArgumentList argumentList, TypeInference typeInference)` (CallBuilder.cs
// lines 1383-1401). The C# `method.MemberDefinition` cast to IMethod ports as
// a dynamic_cast of the IMember handle; the port's TypeInference lift is the
// free InferTypeArguments function over (compilation, conversions,
// typeParameters, arguments, parameterTypes) -- the conversions surface comes
// from the builder's resolver (the same CSharpConversions instance the
// resolver owns). The C# anonymous-type pinning retry is deferred (see the
// header note); the argument resolve results ride the shared annotation
// handles (the C# GC references).
bool CallBuilder::CanInferTypeArgumentsFromArguments(
    const TS::IMethod& method, const ArgumentList& argumentList,
    const ExpressionBuilder& expressionBuilder) {
    if (method.TypeParameters().empty())
        return true;
    // always use unspecialized member, otherwise type inference fails
    const auto* definition =
        dynamic_cast<const TS::IMethod*>(method.MemberDefinition());
    if (definition == nullptr)
        return false;
    std::vector<TS::ITypePtr> paramTypesInArgumentOrder;
    if (!argumentList.ArgumentToParameterMap.has_value()) {
        for (const TS::IParameter* p : definition->Parameters())
            paramTypesInArgumentOrder.push_back(
                const_cast<TS::IType&>(p->Type()).shared_from_this());
    } else {
        for (int index : *argumentList.ArgumentToParameterMap) {
            paramTypesInArgumentOrder.push_back(
                index >= 0
                    ? const_cast<TS::IType&>(
                          definition->Parameters()[static_cast<std::size_t>(index)]->Type())
                          .shared_from_this()
                    : TS::UnknownType());
        }
    }
    std::vector<std::shared_ptr<Sem::ResolveResult>> argumentResolveResults;
    argumentResolveResults.reserve(argumentList.Arguments.size());
    for (const TranslatedExpression& argument : argumentList.Arguments)
        argumentResolveResults.push_back(
            GetSharedResolveResult(*argument.Expression()));
    std::vector<const TS::ITypeParameter*> typeParameters =
        definition->TypeParameters();
    bool success = false;
    Resolver::Detail::InferTypeArguments(
        *expressionBuilder.compilation, expressionBuilder.resolver->Conversions(),
        typeParameters, argumentResolveResults, paramTypesInArgumentOrder, success,
        std::nullopt, expressionBuilder.typeInference.algorithm);
    return success;
}

// The C# `private TranslatedExpression WrapInAsRefReadOnly(TranslatedExpression
// arg)` (CallBuilder.cs lines 1357-1367).
TranslatedExpression CallBuilder::WrapInAsRefReadOnly(TranslatedExpression arg) {
    auto* invocation = new Syntax::InvocationExpression();
    invocation->Target(new Syntax::IdentifierExpression("ILSpyHelper_AsRefReadOnly"));
    invocation->Arguments().Add(arg.Expression());
    auto* direction =
        new Syntax::DirectionExpression(Syntax::FieldDirection::In, invocation);
    return WithoutILInstruction(WithRR(
        *direction,
        std::make_shared<Sem::ByReferenceResolveResult>(
            const_cast<TS::IType&>(arg.Type()).shared_from_this(),
            TS::ReferenceKind::In)));
}

// The C# `private void EnforceExplicitIn(TranslatedExpression[] arguments,
// IParameter[] expectedParameters)` (CallBuilder.cs lines 1343-1355). The C#
// `expressionBuilder.statementBuilder.EmitAsRefReadOnly = true` bookkeeping is
// deferred with the StatementBuilder port (the flag tells the statement stage
// to emit the helper declaration; the call-builder side of the shape is
// complete here).
void CallBuilder::EnforceExplicitIn(
    std::vector<TranslatedExpression>& arguments,
    const std::vector<const TS::IParameter*>& expectedParameters) {
    for (std::size_t i = 0; i < arguments.size(); i++) {
        if (expectedParameters[i]->ReferenceKind() != TS::ReferenceKind::In)
            continue;
        if (dynamic_cast<const Syntax::DirectionExpression*>(
                arguments[i].Expression())
            != nullptr)
            continue;

        arguments[i] = WrapInAsRefReadOnly(arguments[i]);
        // The C# `expressionBuilder.statementBuilder.EmitAsRefReadOnly = true`
        // -- deferred with the StatementBuilder port.
    }
}

// The C# `private void CastArguments(IList<TranslatedExpression> arguments,
// IList<IParameter> expectedParameters)` (CallBuilder.cs lines 1446-1483).
// The C# anonymous-type arm (the lambda return-type rewrite through
// ModifyReturnTypeOfLambda) is deferred with the anonymous-type surface; the
// plain cast arm is the path every non-anonymous call takes.
void CallBuilder::CastArguments(
    std::vector<TranslatedExpression>& arguments,
    const std::vector<const TS::IParameter*>& expectedParameters) {
    for (std::size_t i = 0; i < arguments.size(); i++) {
        const TS::IParameter* parameter = expectedParameters[i];
        const TS::IType* parameterType;
        if (parameter->Type().Kind() == TS::TypeKind::Dynamic) {
            parameterType = &const_cast<TS::IType&>(
                expressionBuilder_->compilation->FindType(TS::KnownTypeCode::Object));
        } else {
            parameterType = &parameter->Type();
        }

        auto* parameterByRef =
            dynamic_cast<const TS::ByReferenceType*>(parameterType);
        if (parameter->ReferenceKind() == TS::ReferenceKind::In
            && parameterByRef != nullptr
            && dynamic_cast<const TS::ByReferenceType*>(&arguments[i].Type())
                == nullptr) {
            parameterType = parameterByRef->Element().get();
        }

        arguments[i] = arguments[i].ConvertTo(
            *const_cast<TS::IType*>(parameterType), *expressionBuilder_,
            /*checkForOverflow=*/false, /*allowImplicitConversion=*/false);
    }
}

// The C# `OverloadResolutionErrors IsUnambiguousCall(...)` (CallBuilder.cs
// lines 1554-1663): the four candidate-source arms over the ported
// OverloadResolution engine. The C# HashSet-based operator-candidate union
// deduplicates by reference identity preserving first-seen order (the port's
// pointer-identity seen-set mirrors the C# HashSet<T> enumeration shape
// closely enough for candidate ordering: the C# HashSet<T> enumerates in
// insertion order for reference-type elements).
Resolver::OverloadResolutionErrors CallBuilder::IsUnambiguousCall(
    const ExpectedTargetDetails& expectedTargetDetails, const TS::IMethod& method,
    const Sem::ResolveResult* target, const std::vector<TS::ITypePtr>& typeArguments,
    const std::vector<std::shared_ptr<Sem::ResolveResult>>& arguments,
    const std::optional<std::vector<std::string>>& argumentNames,
    int firstOptionalArgumentIndex, const TS::IParameterizedMember*& foundMember,
    bool& bestCandidateIsExpandedForm) {
    foundMember = nullptr;
    bestCandidateIsExpandedForm = false;
    const TS::ITypeDefinition* currentTypeDefinition =
        expressionBuilder_->resolver->CurrentTypeDefinition();

    Resolver::Log::WriteLine(
        "IsUnambiguousCall: Performing overload resolution for {} ({})",
        method.Name(),
        expectedTargetDetails.CallOpCode == IL::OpCode::NewObj ? "newobj" : "call");

    Resolver::MemberLookup lookup(
        currentTypeDefinition,
        currentTypeDefinition != nullptr ? currentTypeDefinition->ParentModule()
                                         : nullptr);

    std::optional<std::vector<std::string>> names = argumentNames;
    if (!(firstOptionalArgumentIndex < 0 || !names.has_value()))
        names->resize(static_cast<std::size_t>(firstOptionalArgumentIndex));

    Resolver::OverloadResolution overloadResolution(
        expressionBuilder_->resolver->Compilation(), arguments, names, typeArguments,
        &expressionBuilder_->resolver->Conversions());
    if (expectedTargetDetails.CallOpCode == IL::OpCode::NewObj) {
        bool allowProtectedAccess =
            currentTypeDefinition == method.DeclaringTypeDefinition();
        for (const TS::IMethod* ctor : method.DeclaringType()->GetConstructors()) {
            if (lookup.IsAccessible(*ctor, allowProtectedAccess))
                overloadResolution.AddCandidate(*ctor);
        }
    } else if (method.IsOperator()) {
        // The C# HashSet<IParameterizedMember> union deduplicates by reference
        // identity preserving first-seen order; the port mirrors that with a
        // pointer-identity seen-set over the resolver's candidate vectors (the
        // candidates are shared IMethod handles).
        std::vector<std::shared_ptr<const TS::IMethod>> operatorCandidates;
        std::set<const TS::IMethod*> seenCandidates;
        auto Collect = [&](const TS::IType& operandType) {
            for (auto& candidate :
                 expressionBuilder_->resolver->GetUserDefinedOperatorCandidates(
                     operandType, method.Name().c_str())) {
                if (seenCandidates.insert(candidate.get()).second)
                    operatorCandidates.push_back(std::move(candidate));
            }
        };
        if (arguments.size() == 1) {
            const TS::IType& argType = TS::GetUnderlyingType(arguments[0]->Type());
            Collect(argType);
            if (method.Name() == "op_Explicit") {
                // For casts, also consider candidates from the target type we
                // are casting to.
                const TS::IType& targetType =
                    TS::GetUnderlyingType(method.ReturnType());
                Collect(targetType);
            }
        } else if (arguments.size() == 2) {
            const TS::IType& lhsType = TS::GetUnderlyingType(arguments[0]->Type());
            const TS::IType& rhsType = TS::GetUnderlyingType(arguments[1]->Type());
            Collect(lhsType);
            Collect(rhsType);
        }
        for (const auto& candidate : operatorCandidates)
            overloadResolution.AddCandidate(*candidate);
    } else if (target == nullptr) {
        auto result = std::dynamic_pointer_cast<const Resolver::MethodGroupResolveResult>(
            expressionBuilder_->resolver->ResolveSimpleName(
                method.Name(), typeArguments, /*isInvocationTarget=*/true));
        if (result == nullptr)
            return Resolver::OverloadResolutionErrors::AmbiguousMatch;
        overloadResolution.AddMethodLists(result->MethodsGroupedByDeclaringType());
    } else {
        auto result = std::dynamic_pointer_cast<const Resolver::MethodGroupResolveResult>(
            lookup.Lookup(*target, method.Name(), typeArguments, /*isInvocation=*/true));
        if (result == nullptr)
            return Resolver::OverloadResolutionErrors::AmbiguousMatch;
        overloadResolution.AddMethodLists(result->MethodsGroupedByDeclaringType());
    }
    bestCandidateIsExpandedForm = overloadResolution.BestCandidateIsExpandedForm();
    if (overloadResolution.BestCandidateErrors()
        != Resolver::OverloadResolutionErrors::None)
        return overloadResolution.BestCandidateErrors();
    if (overloadResolution.IsAmbiguous())
        return Resolver::OverloadResolutionErrors::AmbiguousMatch;
    foundMember = overloadResolution.GetBestCandidateWithSubstitutedTypeArguments();
    if (!IsAppropriateCallTarget(expectedTargetDetails, method, *foundMember))
        return Resolver::OverloadResolutionErrors::AmbiguousMatch;
    std::optional<std::vector<int>> map = overloadResolution.GetArgumentToParameterMap();
    for (std::size_t i = 0; i < arguments.size(); i++) {
        int parameterIndex = (*map)[i];
        auto* outVar = dynamic_cast<const Sem::OutVarResolveResult*>(arguments[i].get());
        if (outVar != nullptr && parameterIndex >= 0) {
            const TS::IParameter* parameter = foundMember->Parameters()[parameterIndex];
            const TS::IType& paramType = TS::UnwrapByRef(parameter->Type());
            if (!paramType.Equals(*outVar->OriginalVariableType()))
                return Resolver::OverloadResolutionErrors::OutVarTypeMismatch;
        }
    }

    return Resolver::OverloadResolutionErrors::None;
}

// The C# `private CallTransformation GetRequiredTransformationsForCall(...)` 
// (CallBuilder.cs lines 1152-1341). The C# `goto case` chains inside the
// overload-resolution switch flatten into fall-through if-arms evaluating the
// same conditions: the WrongNumberOfTypeArguments arm with
// `requireTypeArguments` already true and the
// MissingArgumentForRequiredParameter arm with no optional argument both fall
// into the default arm, exactly as the C# gotos do. The C# anonymous-type
// pinning arm (the `settings.AnonymousTypes &&
// method.TypeArguments.Any(ContainsAnonymousType)` retry) is deferred with
// the anonymous-type surface -- the port takes the else branch (the explicit
// type-arguments shortcut) unconditionally, which is the C# behavior for
// every argument shape whose type arguments do not involve anonymous types.
CallBuilder::CallTransformation CallBuilder::GetRequiredTransformationsForCall(
    const ExpectedTargetDetails& expectedTargetDetails, const TS::IMethod& method,
    TranslatedExpression& target, ArgumentList& argumentList,
    CallTransformation allowedTransforms, const TS::IParameterizedMember*& foundMethod) {
    CallBuilder::CallTransformation transform = CallTransformation::None;

    // initialize requireTarget flag
    bool requireTarget;
    const Sem::ResolveResult* targetResolveResult;
    if ((allowedTransforms & CallTransformation::RequireTarget)
        != CallTransformation::None) {
        if (settings_->AlwaysQualifyMemberReferences()
            || expressionBuilder_->HidesVariableWithName(method.Name())) {
            requireTarget = true;
        } else {
            if (method.IsLocalFunction())
                requireTarget = false;
            else if (method.IsStatic())
                requireTarget =
                    !expressionBuilder_->IsCurrentOrContainingType(
                        method.DeclaringTypeDefinition())
                    || method.Name() == ".cctor";
            else if (method.Name() == ".ctor")
                // always use target for base/this-ctor-call, the constructor
                // initializer pattern depends on this
                requireTarget = true;
            else if (dynamic_cast<const Syntax::BaseReferenceExpression*>(
                         target.Expression())
                     != nullptr)
                requireTarget = (expectedTargetDetails.CallOpCode != IL::OpCode::CallVirt
                                 && method.IsVirtual());
            else
                requireTarget =
                    dynamic_cast<const Syntax::ThisReferenceExpression*>(
                        target.Expression())
                    == nullptr;
        }
        targetResolveResult = requireTarget ? target.ResolveResult() : nullptr;
    } else {
        // HACK: this is a special case for collection initializer calls, they
        // do not allow a target to be emitted, but we still need it for
        // overload resolution.
        requireTarget = true;
        targetResolveResult = target.ResolveResult();
    }

    // initialize requireTypeArguments flag
    bool requireTypeArguments;
    std::vector<TS::ITypePtr> typeArguments;
    bool appliedRequireTypeArgumentsShortcut = false;
    if (method.TypeParameters().size() > 0
        && (allowedTransforms & CallTransformation::RequireTypeArguments)
        != CallTransformation::None
        && !IsPossibleExtensionMethodCallOnNull(method, argumentList.Arguments)) {
        // The ambiguity resolution below only adds type arguments as last
        // resort measure; the CanInferTypeArgumentsFromArguments shortcut
        // detects the cases that always require them (such as
        // Enumerable.OfType<TResult>(IEnumerable input)) and lends overload
        // resolution a hand. The C# anonymous-type pinning retry inside the
        // failure arm is deferred with the anonymous-type surface.
        if (!CanInferTypeArgumentsFromArguments(method, argumentList,
                                                *expressionBuilder_)) {
            requireTypeArguments = true;
            typeArguments = method.TypeArguments();
            appliedRequireTypeArgumentsShortcut = true;
        } else {
            requireTypeArguments = false;
            typeArguments.clear();
        }
    } else {
        requireTypeArguments = false;
        typeArguments.clear();
    }

    bool targetCasted = false;
    bool argumentsCasted = false;
    bool originalRequireTarget = requireTarget;
    bool skipTargetCast = method.Accessibility() <= TS::Accessibility::Protected
        && expressionBuilder_->IsBaseTypeOfCurrentType(
            method.DeclaringTypeDefinition());
    Resolver::OverloadResolutionErrors errors;
    bool bestCandidateIsExpandedForm = false;
    while ((errors = IsUnambiguousCall(
                expectedTargetDetails, method, targetResolveResult, typeArguments,
                argumentList.GetArgumentResolveResults(),
                argumentList.GetArgumentNames(),
                argumentList.FirstOptionalArgumentIndex, foundMethod,
                bestCandidateIsExpandedForm))
               != Resolver::OverloadResolutionErrors::None
           || bestCandidateIsExpandedForm != argumentList.IsExpandedForm) {
        if (errors == Resolver::OverloadResolutionErrors::OutVarTypeMismatch) {
            assert(argumentList.UseImplicitlyTypedOut);
            argumentList.UseImplicitlyTypedOut = false;
            continue;
        }
        if (errors == Resolver::OverloadResolutionErrors::TypeInferenceFailed
            && (allowedTransforms & CallTransformation::RequireTypeArguments)
            != CallTransformation::None) {
            // goto case OverloadResolutionErrors.WrongNumberOfTypeArguments
            errors = Resolver::OverloadResolutionErrors::WrongNumberOfTypeArguments;
        }
        if (errors == Resolver::OverloadResolutionErrors::WrongNumberOfTypeArguments) {
            assert((allowedTransforms & CallTransformation::RequireTypeArguments)
                   != CallTransformation::None);
            if (requireTypeArguments) {
                // goto default
            } else {
                requireTypeArguments = true;
                typeArguments = method.TypeArguments();
                continue;
            }
        }
        if (errors == Resolver::OverloadResolutionErrors::MissingArgumentForRequiredParameter) {
            if (argumentList.FirstOptionalArgumentIndex == -1) {
                // goto default
            } else {
                argumentList.FirstOptionalArgumentIndex = -1;
                continue;
            }
        }
        // default:
        // TODO : implement some more intelligent algorithm that decides which
        // of these fixes (cast args, add target, cast target, add type args)
        // is best in this case. Additionally we should not cast all arguments
        // at once, but step-by-step try to add only a minimal number of casts.
        if (argumentList.AddNamesToPrimitiveValues) {
            argumentList.AddNamesToPrimitiveValues = false;
        } else if (argumentList.FirstOptionalArgumentIndex >= 0) {
            argumentList.FirstOptionalArgumentIndex = -1;
        } else if (!argumentsCasted) {
            // If we added type arguments beforehand, but that didn't make the
            // code any better, undo that decision and add casts first.
            if (appliedRequireTypeArgumentsShortcut) {
                requireTypeArguments = false;
                typeArguments.clear();
                appliedRequireTypeArgumentsShortcut = false;
            }
            argumentsCasted = true;
            argumentList.UseImplicitlyTypedOut = false;
            CastArguments(argumentList.Arguments, argumentList.ExpectedParameters);
        } else if ((allowedTransforms & CallTransformation::RequireTarget)
                   != CallTransformation::None
            && !requireTarget) {
            requireTarget = true;
            targetResolveResult = target.ResolveResult();
        } else if ((allowedTransforms & CallTransformation::RequireTarget)
                   != CallTransformation::None
            && !targetCasted) {
            if (skipTargetCast && requireTarget != originalRequireTarget) {
                requireTarget = originalRequireTarget;
                if (!originalRequireTarget)
                    targetResolveResult = nullptr;
                allowedTransforms = allowedTransforms
                    & ~CallTransformation::RequireTarget;
            } else {
                targetCasted = true;
                target = target.ConvertTo(
                    const_cast<TS::IType&>(*method.DeclaringType()),
                    *expressionBuilder_);
                targetResolveResult = target.ResolveResult();
            }
        } else if ((allowedTransforms & CallTransformation::RequireTypeArguments)
                   != CallTransformation::None
            && !requireTypeArguments) {
            requireTypeArguments = true;
            typeArguments = method.TypeArguments();
        } else if ((allowedTransforms & CallTransformation::EnforceExplicitIn)
                   != CallTransformation::None) {
            EnforceExplicitIn(argumentList.Arguments,
                              argumentList.ExpectedParameters);
            allowedTransforms = allowedTransforms
                & ~CallTransformation::EnforceExplicitIn;
        } else {
            break;
        }
        continue;
    }
    // We've given up (or the loop resolved): foundMethod is assigned by the
    // IsUnambiguousCall out param each iteration; the C# "We've given up"
    // path re-assigns `foundMethod = method` before its break.
    if (errors != Resolver::OverloadResolutionErrors::None
        || bestCandidateIsExpandedForm != argumentList.IsExpandedForm)
        foundMethod = &method;
    if ((allowedTransforms & CallTransformation::RequireTarget)
        != CallTransformation::None
        && requireTarget)
        transform |= CallTransformation::RequireTarget;
    if ((allowedTransforms & CallTransformation::RequireTypeArguments)
        != CallTransformation::None
        && requireTypeArguments)
        transform |= CallTransformation::RequireTypeArguments;
    if (argumentList.FirstOptionalArgumentIndex < 0)
        transform |= CallTransformation::NoOptionalArgumentAllowed;
    if (!argumentList.AddNamesToPrimitiveValues)
        transform |= CallTransformation::NoNamedArgsForPrettiness;
    return transform;
}


} // namespace ILSpy::Decompiler::CSharp
