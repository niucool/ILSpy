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
#include "Decompiler/CSharp/Syntax/Expressions/ArrayCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedArgumentExpression.hpp"
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
#include "Decompiler/TypeSystem/InheritanceHelper.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/Decimal.hpp"

#include <any>
#include <cassert>
#include <charconv>
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

} // namespace ILSpy::Decompiler::CSharp
