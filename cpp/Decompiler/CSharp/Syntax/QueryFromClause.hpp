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

// Port of the `QueryFromClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryFromClause.g.cs` + the hand-written partial, which declares only the `Type`,
// `Identifier`, and `Expression` slot properties plus the `FromKeyword`/`InKeyword` consts --
// no ctors, no helpers). The next in-order Phase-5 piece per the D310 plan ("the remaining
// string-name-[Slot] and multi-slot clauses"). `from_clause ::= 'from' type? identifier
// 'in' expression` (C# grammar 12.23.1): a `QueryClause` introducing a range variable over an
// `Expression`, optionally with an explicit `Type`.
//
// It is the `MemberType` D238 collection-plus-singles shape MINUS the collection: three single
// slots at flattened indices 0/1/2 -- a NULLABLE `AstType?` `Type` at index 0, a REQUIRED
// non-nullable string `Identifier` (over a backing `IdentifierToken`) at index 1, and a
// REQUIRED `Expression` at index 2. The `Identifier` is a NON-nullable `string` (the C#
// source declares `public partial string Identifier`, not `string?`), so the backing token is
// a REQUIRED (non-nullable) slot, the string getter DEREFS the token (returning `std::string`,
// not `std::optional<std::string>` -- a null token is a half-constructed node that would
// `NullReferenceException` in C#), and the string setter uses `Identifier::Create` (NOT
// `CreateIfNotEmpty` -- the `MemberType.MemberName` D238 / `LabelStatement` D259 /
// `IdentifierExpression` D246 precedent). The `Type` is nullable (`AstType?`), so it is set
// via the `MatchOptional` `DoMatch` term and `CheckInvariant` does not require it.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitQueryFromClause(this)`. The generated slot statics are `TypeSlot` (a
// `CSharpSlotInfo<AstType>` pointing at `Slots.Type`, optional), `IdentifierTokenSlot` (a
// `CSharpSlotInfo<Identifier>` pointing at `Slots.Identifier`, required), and `ExpressionSlot`
// (a `CSharpSlotInfo<Expression>` pointing at `Slots.Expression`, required). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): deep-clones the present children through the setters (which re-parent),
// and copies the annotation channel.
//
// C++ name-shadowing crux (the D237 `SimpleType` / D246 `IdentifierExpression` / D231
// `UnaryOperatorExpression` precedents combined): the string accessor `Identifier()` shadows
// the `Identifier` CLASS, and the `Expression()` accessor shadows the `Expression` base type,
// so the elaborated-type-specifiers `class Identifier` and `class Expression` are used in
// every type position AFTER the respective accessors (the backing fields, the slot statics'
// element types, the `SetChild` `static_cast`s, the token accessor's signature, and the
// ctor parameters), and the `Identifier::Create` factory call in the setter is fully-qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Identifier::...`). The `Type()` accessor does NOT
// shadow (no class named `Type` in `Syntax` -- there is `AstType`, not `Type` -- the
// `Attribute` D240 / `CastExpression` D243 differently-named-accessor precedent), so the
// `AstType` element type is the plain `class`-less `AstType` throughout.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYFROMCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYFROMCLAUSE_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/QueryClause.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class QueryFromClause : QueryClause`. `final` (the C#
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false,
// so no `PatternPlaceholder` derives from it). The three-single-slot shape (a nullable `AstType`
// + a required non-nullable string `Identifier` + a required `Expression`) applied to the
// `QueryClause` hierarchy.
class QueryFromClause final : public QueryClause {
public:
    ~QueryFromClause() override = default;

    // The C# `public const string FromKeyword = "from"` and `InKeyword = "in"` -- the tokens
    // the output visitor emits for the `from`/`in` keywords. Compile-time literals carried as
    // `static constexpr const char*` (static fields, not instance state, so they are not part
    // of `MembersToMatch`/`DoMatch`).
    static constexpr const char* FromKeyword = "from";
    static constexpr const char* InKeyword = "in";

    // The generated empty ctor (the C# `public QueryFromClause()`). All three slots default to
    // null (no type, no name, no expression). Null `Identifier`/`Expression` violate the
    // required-slot invariants, so a default-constructed node is only valid until those are set
    // (or until `DoMatch`/`CheckInvariant` observe the missing children); the nullable `Type`
    // may stay null.
    QueryFromClause() = default;

    // The generated all-params ctor (the C# `public QueryFromClause(AstType? type, string
    // identifier, Expression expression)`). `Type` is nullable (optional), `Identifier` is
    // required (a string `[Slot]` is a required ctor param regardless of optionality), and
    // `Expression` is required, so `RequiredConstructorPrefixLength` walks the whole
    // `ctorParams` list to the last non-optional param (`Expression` at index 2) and equals
    // `ctorParams.Count` (3) -- yielding ONLY the empty + this all-params ctor (no shorter
    // prefix, no collection so no `params` overload). The generated body sets the slots via the
    // property setters; the string setter creates the token via `Identifier::Create`. The
    // `type` param is `AstType*` (nullable -- the C# `AstType?` ports to a nullable pointer),
    // `identifier` is a `string` (the ctor param type precedes the `Identifier()` accessor, so
    // the plain type resolves to `std::string`), and `expression` is the plain `Expression`
    // (the ctor param precedes the `Expression()` getter).
    QueryFromClause(AstType* type, std::string identifier, Expression* expression)
        : QueryFromClause() {
        Type(type);
        Identifier(std::move(identifier));
        Expression(expression);
    }

    // ---- The `Type` slot -----------------------------------------------
    // The generated `[Slot("Type")] AstType? Type` -- a single, NULLABLE `AstType` child at
    // flattened index 0. The const-index `SetChildNode(ref field, value, 0)` setter (the slot
    // is the first and no collection precedes it). NO name shadowing (the `Type()` accessor does
    // not collide with any class in `Syntax` -- there is `AstType`, not `Type` -- the
    // `Attribute` D240 / `CastExpression` D243 precedent), so the element type is the plain
    // `AstType`. A nullable slot is null until set; `CheckInvariant` does not require it.
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `IdentifierToken` slot (the backing token of the string name) ------------
    // The generated `[Slot("Identifier")] string Identifier`'s backing `IdentifierToken` child
    // slot -- a REQUIRED (non-nullable) `Identifier` at flattened index 1. The const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it) re-parents and
    // re-indexes in place. `class Identifier` (the `Identifier()` accessor shadows the class).
    class Identifier* IdentifierToken() const { return identifierToken_; }
    void IdentifierToken(class Identifier* value) {
        SetChildNode(identifierToken_, value, 1);
    }

    // The `Identifier` string-name accessor (over the token). A NON-optional name (the C#
    // `string`, not `string?`): `get` returns `IdentifierToken.Name` (derefs the token -- a
    // null token is a half-constructed node that would NRE in C#), `set` creates the token via
    // `Identifier::Create` (an empty name yields a token with an empty `Name`, NOT a null
    // token). `Identifier()` returns `std::string` (a copy of the token's name); the
    // `Identifier::Create` factory call is fully-qualified (the `Identifier()` accessor
    // shadows the `Identifier` class in its own setter, the generator's `global::` case).
    std::string Identifier() const { return identifierToken_->Name(); }
    void Identifier(std::string_view value) {
        IdentifierToken(::ILSpy::Decompiler::CSharp::Syntax::Identifier::Create(std::string(value)));
    }

    // ---- The `Expression` slot -----------------------------------------
    // The generated `[Slot("Expression")] Expression Expression` -- a single, REQUIRED
    // (non-nullable) `Expression` child at flattened index 2. The const-index
    // `SetChildNode(ref field, value, 2)` setter (no collection precedes it). `class
    // Expression` (the `Expression()` accessor shadows the `Expression` base type).
    class Expression* Expression() const { return expression_; }
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 2);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // `TypeSlot` (`CSharpSlotInfo<AstType>` pointing at `Slots.Type`, optional); `class
    // Identifier` (the `Identifier()` accessor shadows the class); `class Expression` (the
    // `Expression()` accessor shadows the base type). `Slots::Type` (by `Attribute` D240),
    // `Slots::Identifier` (by `SimpleType` D237), and `Slots::Expression` (by
    // `UnaryOperatorExpression` D231) all already exist, so no new `Slots.hpp` constant.
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, true};
    static inline const CSharpSlotInfoT<class Identifier> IdentifierTokenSlot{"IdentifierToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryFromClause`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryFromClause(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitQueryFromClause`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitQueryFromClause(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Three single slots at flattened indices 0/1/2; no collection, so `GetChildCount` is the
    // constant 3 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with three cases).

    int GetChildCount() const override { return 3; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            case 1: return identifierToken_;
            case 2: return expression_;
            default: throw std::out_of_range("QueryFromClause::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(identifierToken_, static_cast<class Identifier*>(value), 1); break;
            case 2: SetChildNode(expression_, static_cast<class Expression*>(value), 2); break;
            default: throw std::out_of_range("QueryFromClause::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            case 1: return &IdentifierTokenSlot;
            case 2: return &ExpressionSlot;
            default: throw std::out_of_range("QueryFromClause::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QueryFromClause o && this.Type?.DoMatch(...) == true
    //      && MatchString(this.Identifier, o.Identifier)
    //      && this.Expression.DoMatch(o.Expression, match)` (source declaration order: `Type`
    // before `Identifier` before `Expression`). The `Type` term is a NULLABLE recursive child
    // (`MatchOptional`); the `Identifier` term is a `String` `MatchString` (the `$any$`
    // wildcard in the pattern's `Identifier` matches any candidate name); the `Expression` term
    // is a NON-NULLABLE recursive child (the direct dispatch). The `FromKeyword`/`InKeyword`
    // const strings are static fields, not instance properties, so they are not part of
    // `MembersToMatch` and do not appear here. A type-only mismatch (not a `QueryFromClause`)
    // rejects early.
    //
    // The C# direct dispatch (`this.Expression.DoMatch`) assumes the required child is present;
    // the port routes it through `AstNode::MatchRequired` (the same-class static helper) because
    // C++ `[class.access.derived]` forbids a derived node from calling the protected `DoMatch`
    // through a base `Expression*`. The nullable `Type` term uses `MatchOptional` (returns true
    // when both are absent, delegates when both present, rejects when only one present).
    //
    // `Identifier()` returns `std::string` (the token's name); `Pattern::MatchString` takes
    // `std::optional<std::string_view>`, so the view is built per argument (`std::string_view`
    // needs one explicit construction from `std::string`, the D227 rule). The `Identifier()`
    // calls are INLINED in the `MatchString` arguments (not pre-computed in locals) so the C#
    // `&&` short-circuits: `o->Identifier()` derefs the candidate's token only when the `Type`
    // term passed, avoiding eager UB on a half-constructed candidate; the `std::string`
    // temporaries live until the end of the full `return` expression so the
    // `std::string_view` views stay valid for the `MatchString` call (the D227 / D238
    // inlined-MatchString-arguments precedent).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryFromClause*>(other);
        if (o == nullptr)
            return false;
        if (!MatchOptional(type_, o->type_, match))
            return false;
        if (!PatternMatching::Pattern::MatchString(
                std::string_view(this->Identifier()),
                std::string_view(o->Identifier())))
            return false;
        return MatchRequired(expression_, o->expression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Type`
    // deep-cloned through the setter when present, the `IdentifierToken` deep-cloned through
    // the setter when present, and the `Expression` deep-cloned through the setter when present.
    // No own location fields, so they are not copied (the `MemberType` D238 /
    // `CastExpression` D243 no-location-copy precedent). The covariant return is
    // `QueryFromClause*` (through `QueryClause*`, the `QueryClause::Clone` pure-virtual). Each
    // child is skipped if absent (`Clone` tolerates a missing child even though
    // `Identifier`/`Expression` are required -- the invariant is enforced by `CheckInvariant`,
    // not by `Clone`). `class Identifier`/`class Expression` (the accessors shadow the classes);
    // the token `Clone()` returns `Identifier*` and the expression `Clone()` returns
    // `Expression*`, which the typed setters accept directly. The `AstType` `Clone()` returns
    // `AstType*` which the `Type(AstType*)` setter accepts directly.
    QueryFromClause* Clone() const override {
        auto* node = new QueryFromClause();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (identifierToken_ != nullptr)
            node->IdentifierToken(identifierToken_->Clone());
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `AstType` is unshadowed (no class named `Type`); `class Identifier`
    // (the `Identifier()` accessor shadows the class); `class Expression` (the `Expression()`
    // accessor shadows the base type). The `Type` slot is nullable (null until set); the
    // `IdentifierToken`/`Expression` slots are required (non-null only by invariant, so the
    // pointers are null until the slots are set).
    AstType* type_ = nullptr;
    class Identifier* identifierToken_ = nullptr;
    class Expression* expression_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYFROMCLAUSE_HPP
