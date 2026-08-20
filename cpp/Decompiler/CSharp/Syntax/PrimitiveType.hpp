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

// Port of the `PrimitiveType` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/PrimitiveType.cs (the generated `PrimitiveType.g.cs`
// + the hand-written partial). The first concrete `AstType` -- the simplest one, a leaf with
// no `[Slot]` children -- and the next in-order Phase-5 piece per the D235 plan ("the AstType
// hierarchy (SimpleType/MemberType/ComposedType/PrimitiveType/...)"): `PrimitiveType` is the
// `predefined_type` of the C# grammar (the built-in type keywords `bool`/`int`/`string`/...),
// a leaf `AstType` carrying a single `Keyword` string and deriving `EndLocation` from it.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. `PrimitiveType` has `[DecompilerAstNode]` (no
// `hasPatternPlaceholder`), so no pattern placeholder is emitted. It declares NO `[Slot]`
// children: `Keyword` is a hand-written `string` property (NOT a `[Slot]` name -- there is no
// `[Slot]` attribute and no backing `Identifier` token), so the generator sees no slots and
// emits no slot properties, no name accessors, and no generated constructors (the
// `WriteConstructors` step returns early when `slots.Count == 0`); the three hand-written
// ctors stand. The generated `AcceptVisitor` calls `visitor.VisitPrimitiveType(this)`;
// `PrimitiveType` does not end in "AstType", so the generator's visit-method-name default
// (`targetSymbol.Name`) yields `VisitPrimitiveType` (the `EndsWith("AstType")` rewriting to
// "...Type" applies only to `FunctionPointerAstType`/`InvocationAstType`/`TupleAstType`).
// The generated `DoMatch` is `return other is PrimitiveType o &&
// MatchString(this.Keyword, o.Keyword)` -- a type check plus a `MatchString` on `Keyword`
// (a non-`[Slot]` public `string` property, so the generator adds it to `MembersToMatch` as a
// `String` term). `KnownTypeCode` carries `[ExcludeFromMatch]`, so the generator skips it.
//
// `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it
// (no `MemberwiseClone`): a fresh node with the same `Keyword` and `StartLocation`, plus the
// annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-
// clone pattern). No children to deep-copy (a leaf). `EndLocation` is always derived from
// `Keyword`, so it is not stored.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_PRIMITIVETYPE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_PRIMITIVETYPE_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class PrimitiveType : AstType`. `final` (the C# `sealed`):
// no further derivation. A leaf type node: no `[Slot]` children, so the inherited zero-child
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` defaults apply (the
// NullReferenceExpression/Identifier leaf precedent).
class PrimitiveType final : public AstType {
public:
    ~PrimitiveType() override = default;

    // The C# `public PrimitiveType()` -- the parameterless ctor. The `keyword` field
    // defaults to `string.Empty` (a `std::string` is empty by default, matching the C#).
    PrimitiveType() = default;

    // The C# `public PrimitiveType(string keyword)` -- sets `Keyword`. `explicit` (the
    // PrimitiveExpression single-arg-ctor precedent: avoids an implicit `std::string` ->
    // `PrimitiveType` conversion).
    explicit PrimitiveType(std::string keyword) : keyword_(std::move(keyword)) {}

    // The C# `public PrimitiveType(string keyword, TextLocation location)` -- sets
    // `Keyword` and records the start location (the C# calls `StorePrintStart(location)`).
    PrimitiveType(std::string keyword, TextLocation location) : keyword_(std::move(keyword)) {
        StorePrintStart(location);
    }

    // The C# `public string Keyword` -- the built-in type keyword (`bool`/`int`/`string`/
    // ...). The C# field defaults to `string.Empty` and the setter throws
    // `ArgumentNullException` on null; a `std::string` is never null (an empty keyword is
    // the "no keyword" the default-constructed node carries), so the null-throw has no C++
    // equivalent and the setter just assigns.
    const std::string& Keyword() const { return keyword_; }
    void Keyword(std::string value) { keyword_ = std::move(value); }

    // The C# `public override TextLocation EndLocation` -- spans `Keyword.Length` columns
    // from `StartLocation` (the keyword's lexical width). `StartLocation` is inherited from
    // the base (stored at print time; only the end is derived -- see the file header).
    TextLocation EndLocation() const override {
        return TextLocation(StartLocation().Line,
                            StartLocation().Column + static_cast<int>(keyword_.size()));
    }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch
    // entry: routes back to `VisitPrimitiveType`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitPrimitiveType(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitPrimitiveType`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitPrimitiveType(this);
    }

    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is PrimitiveType o && MatchString(this.Keyword, o.Keyword)`. A type match
    // plus a `MatchString` on `Keyword` -- the `$any$` wildcard (`Pattern::AnyString`) in the
    // pattern's `Keyword` matches any candidate keyword. `KnownTypeCode` is
    // `[ExcludeFromMatch]` (and is itself deferred -- it needs the unported `KnownTypeCode`
    // enum), so it is not compared; `match` is unused (no captures, no recursive child match).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<PrimitiveType*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(std::string_view(keyword_),
                                                     std::string_view(o->keyword_));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node with the same `Keyword` and
    // `StartLocation`, plus the annotation channel copied (`CloneAnnotationsFrom` +
    // `ReparentTrivia`, the D223 concrete-clone pattern). No children to deep-copy (a leaf);
    // `EndLocation` is derived from `Keyword`, so it is not stored.
    PrimitiveType* Clone() const override {
        auto* node = new PrimitiveType(keyword_);
        node->StorePrintStart(StartLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }

private:
    std::string keyword_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_PRIMITIVETYPE_HPP
