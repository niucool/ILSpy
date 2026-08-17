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

// Port of the `PrimitiveExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.cs (the generated
// `PrimitiveExpression.g.cs` + the hand-written partial). The next in-order Phase-5 piece
// per the D227 plan ("PrimitiveExpression with the object-Value variant crux"): a leaf
// expression (no `[Slot]` children) carrying a literal value and its lexical format.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. `PrimitiveExpression` has `[DecompilerAstNode]`
// (no `hasPatternPlaceholder`), so no pattern placeholder is emitted. It declares its own
// `startLocation`/`endLocation` fields and overrides both `StartLocation`/`EndLocation`; the
// hand-written `DoMatch` compares only `Value` (not `Format`): a type check plus
// `this.Value == AnyValue || object.Equals(this.Value, o.Value)`. `Format` is carried as a
// scalar (a lexical hint the output visitor consults, not a structural member).
//
// KEY DESIGN (the `object Value` crux, flagged in D227): the C# `object Value` holds a boxed
// C# literal -- one of null, bool, char, string, decimal, float, double, or an integer
// (int/uint/long/ulong; the small integer types are cast to int/uint before boxing in
// `TypeSystemAstBuilder.ConvertConstantValue`). C++ has no boxed `object`, so the value is a
// `std::variant` (`PrimitiveValue`) whose alternatives are the faithful C++ equivalents:
// `std::monostate` is the C# null, `AnyValueTag` is the pattern wildcard, and the rest are
// the value alternatives. `operator==` on `std::variant` compares the index (the type) first
// then the held value, which reproduces `object.Equals(a, b)` semantics exactly: same type +
// value equality, different types never equal (even when numerically equal -- a boxed `int`
// 0 and a boxed `long` 0 are not equal), and two nulls equal.
//
// KEY DESIGN (the 128-bit `decimal` alternative): the C# `decimal` alternative is the
// faithful `System.Decimal` model `DecimalValue` already ported in the IL layer
// (`Decompiler/IL/Instructions/LdcDecimal.hpp`) -- a 96-bit unsigned mantissa (lo/mid/hi), a
// sign, and a scale, with `ToString` and the single-arg `FromInt32`/`FromUInt32`/.../
// `FromBits` factories. It is reused here (a `using` alias) rather than duplicated, since the
// 96-bit-mantissa + scale arithmetic (the divmod-by-1e9 digit formatting) is non-trivial and a
// second copy would be a drift hazard. The CSharp back end already depends on the IL layer
// (`ILAstToCSharp.cpp` consumes IL types); a Syntax header pulling in one IL value-type
// header is a minimal, contained cross-reference (the include weight is modest -- the IL
// instruction headers pull only `InstructionFlags`/`OpCode`/`StackType` + the standard
// `<cstdint>`/`<memory>`/`<string>`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_PRIMITIVEEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_PRIMITIVEEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

// The faithful `System.Decimal` value model (96-bit mantissa + sign + scale). Reused from the
// IL layer (see the file header note) so the C# `decimal` literal held in `Value` round-trips
// without a native decimal type.
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"

#include <cstdint>
#include <string>
#include <utility>
#include <variant>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The faithful `System.Decimal` value model -- the same struct the IL layer's `LdcDecimal`
// carries. Brought into the Syntax namespace by a `using` alias so the `decimal` alternative
// of `PrimitiveValue` names it without qualifying the IL namespace.
using DecimalValue = ILSpy::Decompiler::IL::DecimalValue;

// The C# `public enum LiteralFormat : byte` -- the lexical form of a C# literal (a hint the
// output visitor consults when rendering: print an integer as hex, a string as a utf8
// literal, ...). Mirrors `LiteralFormat` in PrimitiveExpression.cs.
enum class LiteralFormat : std::uint8_t {
    None,
    DecimalNumber,
    HexadecimalNumber,
    BinaryNumber,
    StringLiteral,
    VerbatimStringLiteral,
    CharLiteral,
    Utf8Literal,
};

// The AnyValue sentinel: the C# `public static readonly object AnyValue = new object();` -- a
// unique instance a pattern `PrimitiveExpression`'s `Value` is set to so its `DoMatch` matches
// any candidate. C++ has no single `object` root to hang a unique reference on, so it is a
// distinct empty-struct alternative in `PrimitiveValue` (a tag) -- `std::holds_alternative<Any
// ValueTag>` is the "Value is AnyValue" test. The `operator==`/`operator!=` are required for
// `std::variant::operator==` (every alternative must be equality-comparable); two `AnyValueTag`
// instances are always equal (the tag carries no state -- it is the singleton the C# `AnyValue`
// object is).
struct AnyValueTag {
    friend bool operator==(const AnyValueTag&, const AnyValueTag&) noexcept { return true; }
    friend bool operator!=(const AnyValueTag&, const AnyValueTag&) noexcept { return false; }
};

// The C# `object Value` of a `PrimitiveExpression` -- a boxed C# literal. The alternatives are
// the faithful C++ equivalents of the boxed types the engine stores (see the file header).
// `std::monostate` is the C# null; `AnyValueTag` is the pattern wildcard; `char16_t` is the
// C# `char` (a UTF-16 code unit). `operator==` compares the index (the type) first then the
// held value, reproducing `object.Equals(a, b)` (same type + value equality, different types
// never equal, two nulls equal). Exposed at namespace scope so the engine call sites and the
// tests can name and construct it.
using PrimitiveValue = std::variant<
    std::monostate,   // the C# null (a `null` boxed object)
    AnyValueTag,      // the AnyValue pattern wildcard
    bool,
    char16_t,         // the C# `char` (a UTF-16 code unit)
    std::string,
    DecimalValue,
    float,
    double,
    std::int32_t,
    std::uint32_t,
    std::int64_t,
    std::uint64_t
>;

// The C# `public sealed partial class PrimitiveExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. A leaf expression: no `[Slot]` children, so the inherited
// zero-child `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` defaults apply (the
// NullReferenceExpression precedent).
class PrimitiveExpression final : public Expression {
public:
    ~PrimitiveExpression() override = default;

    // The C# `public PrimitiveExpression(object value)` and
    // `public PrimitiveExpression(object value, LiteralFormat format)`. The `object value`
    // ports as a `PrimitiveValue` (the variant); the variant's converting constructor lets a
    // `bool`/`int`/`double`/.../`DecimalValue` bind to the matching alternative, mirroring the
    // C# boxing. The single-arg ctor is `explicit` (the C# `new PrimitiveExpression(value)` is
    // always explicit construction).
    explicit PrimitiveExpression(PrimitiveValue value) : value_(std::move(value)) {}
    PrimitiveExpression(PrimitiveValue value, LiteralFormat format)
        : value_(std::move(value)), format_(format) {}

    // The C# `public object Value { get; set; }` -- the literal value (a boxed C# literal).
    const PrimitiveValue& Value() const { return value_; }
    void Value(PrimitiveValue value) { value_ = std::move(value); }

    // The C# `public LiteralFormat Format { get; set; }` -- the lexical form (a hint the
    // output visitor consults; not a structural member, not compared by `DoMatch`).
    LiteralFormat Format() const { return format_; }
    void Format(LiteralFormat value) { format_ = value; }

    // The C# `public static readonly object AnyValue = new object();` -- the pattern wildcard.
    // A pattern `PrimitiveExpression` whose `Value` is `AnyValue()` matches any candidate.
    static PrimitiveValue AnyValue() { return PrimitiveValue(AnyValueTag{}); }

    // The C# `internal void SetLocation(TextLocation startLocation, TextLocation endLocation)`
    // -- the construction-time span setter (the output visitor records the literal's span after
    // rendering it). The C# declares its own `startLocation`/`endLocation` fields and overrides
    // both `StartLocation`/`EndLocation`; this port reuses the inherited `startLocation_`/
    // `endLocation_` (set via `StorePrintStart`/`StorePrintEnd` -- the Identifier precedent) so
    // the base `StartLocation()`/`EndLocation()` return them without an override (the C#
    // overrides are purely because it has its own fields, the semantics are identical).
    void SetLocation(TextLocation startLocation, TextLocation endLocation) {
        StorePrintStart(startLocation);
        StorePrintEnd(endLocation);
    }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitPrimitiveExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitPrimitiveExpression(this);
    }

    // The C# `protected internal override bool DoMatch(AstNode? other, Match match)` (hand-
    // written, not generated): `o = other as PrimitiveExpression; return o != null &&
    // (this.Value == AnyValue || object.Equals(this.Value, o.Value))`. A type match plus a value
    // match -- `AnyValue` (the wildcard) matches any candidate; otherwise the values must be
    // the same type and equal (`object.Equals` semantics, reproduced by `std::variant::operator
    // ==`). `Format` is not compared (a lexical hint, not structural); `match` is unused.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<PrimitiveExpression*>(other);
        if (o == nullptr)
            return false;
        if (std::holds_alternative<AnyValueTag>(value_))
            return true;
        return value_ == o->value_;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node with the same `Value`, `Format`, and
    // `StartLocation`/`EndLocation`, plus the annotation channel copied (`CloneAnnotationsFrom`
    // + `ReparentTrivia`, the D223 concrete-clone pattern). No children to deep-copy (a leaf).
    PrimitiveExpression* Clone() const override {
        auto* node = new PrimitiveExpression(value_, format_);
        node->StorePrintStart(StartLocation());
        node->StorePrintEnd(EndLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }

private:
    PrimitiveValue value_;
    LiteralFormat format_ = LiteralFormat::None;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_PRIMITIVEEXPRESSION_HPP
