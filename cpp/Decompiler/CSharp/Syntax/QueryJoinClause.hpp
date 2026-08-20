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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `QueryJoinClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryJoinClause.g.cs` + the hand-written partial, which declares only the five const strings,
// the six slot properties, and the `IsGroupJoin` get-only computed property -- no ctors, no
// other helpers). The complex remaining query clause -- the last in-order Phase-5 piece of the
// query_expression family per the D311 plan. `join_clause ::=
//       'join' type? identifier 'in' expression 'on' expression 'equals' expression
//     | 'join' type? identifier 'in' expression 'on' expression 'equals' expression 'into' identifier`
// (C# grammar 12.23.1): a join clause introducing a range variable over an `InExpression`,
// matched against an `OnExpression`/`EqualsExpression` key pair, optionally with an explicit
// `Type` and optionally with an `into` group-join identifier.
//
// The six single [Slot] children in source declaration order: a NULLABLE `AstType?` `Type` at
// flattened index 0 (the `ReturnStatement` D255 nullable-single-slot shape on an `AstType`
// child -- reusing the already-ported `Slots::Type` kind by `Attribute` D240); a REQUIRED
// non-nullable `string` `JoinIdentifier` over a generated backing `JoinIdentifierToken`
// `Identifier` at flattened index 1 (the `MemberType.MemberName` D238 / `LabelStatement` D259 /
// `IdentifierExpression` D246 non-nullable-string-name-[Slot] shape -- a new `Slots::JoinIdentifier`
// kind; the string setter uses `Identifier::Create` NOT `CreateIfNotEmpty` since the name is
// non-nullable, an empty name yields a token with an empty `Name` not a null token); a REQUIRED
// `Expression` `InExpression` at flattened index 2 (new `Slots::InExpression` kind); a REQUIRED
// `Expression` `OnExpression` at flattened index 3 (new `Slots::OnExpression` kind); a REQUIRED
// `Expression` `EqualsExpression` at flattened index 4 (new `Slots::EqualsExpression` kind);
// and a NULLABLE `string?` `IntoIdentifier` over a generated backing `IntoIdentifierToken`
// `Identifier` at flattened index 5 (the `GotoStatement.Label` D257 / `CatchClause.VariableName`
// D269 nullable-string-name-[Slot] shape -- a new `Slots::IntoIdentifier` kind; the string setter
// uses `Identifier::CreateIfNotEmpty`, an empty/null name clears the token, faithful to the C#
// `string?` optionality). All six are single slots (no collection), so the generator emits the
// const-index `SetChildNode(ref field, value, index)` setters (each flattened index is the
// constant slot position 0..5); `GetChildCount` is the constant 6 and `GetChild`/`SetChild`/
// `GetChildSlotInfo` are a flat six-case index switch (the generator's
// `WriteReturnDispatchSwitch` shape).
//
// `IsGroupJoin` is a get-only `[ExcludeFromMatch]` bool computed from `IntoIdentifier`
// (`!string.IsNullOrEmpty(this.IntoIdentifier)`): it is NOT a `[Slot]`, NOT a ctor param, and
// (being `[ExcludeFromMatch]`) NOT in `MembersToMatch`/`DoMatch` -- the generator's exclude check
// drops it (the redundant-compare rationale in the C# source comment). Ports as a plain
// `bool IsGroupJoin() const` getter reading `IntoIdentifier()`.
//
// NO C++ name-shadowing crux (the `Attribute` D240 / `MemberType.MemberName` D238
// differently-named-property precedent): the accessors are `Type`/`JoinIdentifier`/
// `JoinIdentifierToken`/`InExpression`/`OnExpression`/`EqualsExpression`/`IntoIdentifier`/
// `IntoIdentifierToken` -- NONE is named `AstType`/`Identifier`/`Expression` (the `Type()`
// accessor does not collide since there is `AstType` not `Type`; the `JoinIdentifier()` /
// `IntoIdentifier()` string accessors are NOT named `Identifier()` so they do NOT shadow the
// `Identifier` class; the `InExpression()`/`OnExpression()`/`EqualsExpression()` accessors are
// NOT named `Expression()` so they do NOT shadow the `Expression` base type). So no
// elaborated-type-specifier is needed anywhere, and the `Identifier::Create`/
// `Identifier::CreateIfNotEmpty` factory calls in the string setters are unqualified. This is
// the cleanest multi-slot query-clause port: the only recurring work is the five new
// `Slots.hpp` kinds.
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is QueryJoinClause o && MatchOptional(this.Type, o.Type,
// match) && MatchString(this.JoinIdentifier, o.JoinIdentifier) &&
// this.InExpression.DoMatch(o.InExpression, match) && this.OnExpression.DoMatch(o.OnExpression,
// match) && this.EqualsExpression.DoMatch(o.EqualsExpression, match) &&
// MatchString(this.IntoIdentifier, o.IntoIdentifier)`. The `Type` term is a NULLABLE recursive
// child (`MatchOptional`); the `JoinIdentifier` term is a non-nullable `String` `MatchString`
// (the `$any$` wildcard in the pattern's `JoinIdentifier` matches any candidate name; the
// backing `JoinIdentifierToken` never appears in `MembersToMatch`); the
// `InExpression`/`OnExpression`/`EqualsExpression` terms are NON-NULLABLE recursive children, so
// the generator emits DIRECT `this.X.DoMatch(o.X, match)` calls -- ported through `MatchRequired`
// (the D231 `[class.access.derived]` workaround, since a derived node may not call the protected
// `DoMatch` through a base `Expression*`); the `IntoIdentifier` term is a nullable `String`
// `MatchString` (nullopt passes through as the C# null -- a group-less join matches another
// group-less join). `IsGroupJoin` is `[ExcludeFromMatch]`, so it never appears here. A
// type-only mismatch (not a `QueryJoinClause`) rejects early.
//
// The generated ctors (the generator's `WriteConstructors`): the `Type` is nullable (optional
// ctor param), the `JoinIdentifier` string-name `[Slot]` is a "required" ctor param regardless
// of optionality (the generator's line-168 rule), the `InExpression`/`OnExpression`/
// `EqualsExpression` are required (non-optional), and the `IntoIdentifier` nullable string-name
// `[Slot]` is also a "required" ctor param regardless of optionality (the line-168 rule applies
// to `string?` too -- the optionality governs only the setter empty-to-null behaviour). So every
// ctor param is "required" except `Type`, and `RequiredConstructorPrefixLength` walks the
// whole `ctorParams` list to the last non-optional param (`IntoIdentifier` at index 5) and
// equals `ctorParams.Count` (6) -- yielding ONLY the empty + this all-params ctor (no shorter
// prefix, no collection so no `params` overload). The `IntoIdentifier` ctor param is `std::string`
// (the nullable `string?` ports to `std::string` at the ctor param; the optionality is in the
// setter's `CreateIfNotEmpty`). The six-arg ctor is NOT a converting ctor (multi-arg), so it
// needs no `explicit`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYJOINCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYJOINCLAUSE_HPP

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

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class QueryJoinClause : QueryClause`. `final` (the C# `sealed`;
// `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false, so no
// `PatternPlaceholder` derives from it). The six-single-slot shape (a nullable `AstType` + a
// required non-nullable string `JoinIdentifier` + three required `Expression`s + a nullable
// string `IntoIdentifier`) applied to the `QueryClause` hierarchy.
class QueryJoinClause final : public QueryClause {
public:
    ~QueryJoinClause() override = default;

    // The C# `public const string JoinKeyword = "join"` / `InKeyword = "in"` / `OnKeyword = "on"`
    // / `EqualsKeyword = "equals"` / `IntoKeyword = "into"` -- the tokens the output visitor
    // emits for the `join`/`in`/`on`/`equals`/`into` keywords. Compile-time literals carried as
    // `static constexpr const char*` (static fields, not instance state, so the generator's
    // `MembersToMatch`, which iterates only instance `IPropertySymbol`s, excludes them from the
    // `DoMatch` -- the `CheckedExpression.CheckedKeyword` D234 / `QueryFromClause.FromKeyword`
    // D311 precedent).
    static constexpr const char* JoinKeyword = "join";
    static constexpr const char* InKeyword = "in";
    static constexpr const char* OnKeyword = "on";
    static constexpr const char* EqualsKeyword = "equals";
    static constexpr const char* IntoKeyword = "into";

    // The generated empty ctor (the C# `public QueryJoinClause()`). All six slots default to
    // null (no type, no name, no expressions, no group-join identifier). Null `JoinIdentifier`/
    // `InExpression`/`OnExpression`/`EqualsExpression` violate the required-slot invariants, so
    // a default-constructed node is only valid until those are set (or until `DoMatch`/
    // `CheckInvariant` observe the missing children); the nullable `Type`/`IntoIdentifierToken`
    // may stay null (a join with no explicit type and no `into` group-join).
    QueryJoinClause() = default;

    // The generated all-params ctor (the C# `public QueryJoinClause(AstType? type, string
    // joinIdentifier, Expression inExpression, Expression onExpression, Expression
    // equalsExpression, string intoIdentifier)`). `RequiredConstructorPrefixLength` is 6 (the
    // last "required" param `IntoIdentifier` is at index 5, so `reqLen = 6`), and
    // `ConstructorPrefixLengths` is {6} (no collection, `reqLen == cp.Count`), so this six-arg
    // form IS the only non-empty ctor (no shorter prefix ctor, no `params` overload). The body
    // chains to the empty ctor then sets all six slots in declaration order. The `type` param is
    // `AstType*` (nullable -- the C# `AstType?` ports to a nullable pointer); `joinIdentifier` is
    // `std::string` (the ctor param type precedes the `JoinIdentifier()` accessor, so the plain
    // type resolves); `inExpression`/`onExpression`/`equalsExpression` are the plain `Expression`
    // (each ctor param precedes its getter); `intoIdentifier` is `std::string` (the nullable
    // `string?` ctor param -- the optionality is in the setter's `CreateIfNotEmpty`, so an empty
    // string yields a null token). NOT `explicit` (a six-argument ctor is not a converting ctor).
    QueryJoinClause(AstType* type, std::string joinIdentifier, Expression* inExpression,
                    Expression* onExpression, Expression* equalsExpression, std::string intoIdentifier)
        : QueryJoinClause() {
        Type(type);
        JoinIdentifier(std::move(joinIdentifier));
        InExpression(inExpression);
        OnExpression(onExpression);
        EqualsExpression(equalsExpression);
        IntoIdentifier(std::move(intoIdentifier));
    }

    // ---- The `Type` slot -----------------------------------------------
    // The generated `[Slot("Type")] AstType? Type` -- a single, NULLABLE `AstType` child at
    // flattened index 0 (the optional explicit range-variable type, absent for an implicitly
    // typed `join x in ...`). The const-index `SetChildNode(ref field, value, 0)` setter (the
    // slot is the first and no collection precedes it). NO name shadowing (the `Type()` accessor
    // does not collide with any class in `Syntax` -- there is `AstType`, not `Type` -- the
    // `Attribute` D240 / `CastExpression` D243 precedent), so the element type is the plain
    // `AstType`. A nullable slot is null until set; `CheckInvariant` does not require it.
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `JoinIdentifierToken` slot (the backing token of the join name) ----------
    // The generated `[Slot("JoinIdentifier")] string JoinIdentifier`'s backing
    // `JoinIdentifierToken` child slot -- a REQUIRED (non-nullable) `Identifier` at flattened
    // index 1. The const-index `SetChildNode(ref field, value, 1)` setter (no collection precedes
    // it) re-parents and re-indexes in place. NO name shadowing (the `JoinIdentifierToken()`
    // accessor does NOT collide with the `Identifier` class -- no member is named `Identifier`).
    Identifier* JoinIdentifierToken() const { return joinIdentifierToken_; }
    void JoinIdentifierToken(Identifier* value) {
        SetChildNode(joinIdentifierToken_, value, 1);
    }

    // The `JoinIdentifier` string-name accessor (over the token). A NON-optional name (the C#
    // `string`, not `string?`): `get` returns `JoinIdentifierToken.Name` (derefs the token -- a
    // null token is a half-constructed node that would NRE in C#), `set` creates the token via
    // `Identifier::Create` (an empty name yields a token with an empty `Name`, NOT a null
    // token). `JoinIdentifier()` returns `std::string` (a copy of the token's name); the
    // `Identifier::Create` factory call is unqualified (the `JoinIdentifier()` accessor does NOT
    // shadow the `Identifier` class -- no member is named `Identifier` -- the
    // `MemberType.MemberName` D238 / `LabelStatement` D259 differently-named-property precedent).
    std::string JoinIdentifier() const { return joinIdentifierToken_->Name(); }
    void JoinIdentifier(std::string_view value) {
        JoinIdentifierToken(Identifier::Create(std::string(value)));
    }

    // ---- The `InExpression` slot ---------------------------------------
    // The generated `[Slot("InExpression")] Expression InExpression` -- a single, REQUIRED
    // (non-nullable) `Expression` child at flattened index 2 (the joined sequence). The
    // const-index `SetChildNode(ref field, value, 2)` setter. NO name shadowing (the
    // `InExpression()` accessor does not collide with the `Expression` base type -- no member is
    // named `Expression`), so the element type is the plain `Expression`.
    Expression* InExpression() const { return inExpression_; }
    void InExpression(Expression* value) {
        SetChildNode(inExpression_, value, 2);
    }

    // ---- The `OnExpression` slot ---------------------------------------
    // The generated `[Slot("OnExpression")] Expression OnExpression` -- a single, REQUIRED
    // (non-nullable) `Expression` child at flattened index 3 (the left key selector). The
    // const-index `SetChildNode(ref field, value, 3)` setter. NO name shadowing (the
    // `OnExpression()` accessor does not collide with the `Expression` base type).
    Expression* OnExpression() const { return onExpression_; }
    void OnExpression(Expression* value) {
        SetChildNode(onExpression_, value, 3);
    }

    // ---- The `EqualsExpression` slot ----------------------------------
    // The generated `[Slot("EqualsExpression")] Expression EqualsExpression` -- a single,
    // REQUIRED (non-nullable) `Expression` child at flattened index 4 (the right key selector).
    // The const-index `SetChildNode(ref field, value, 4)` setter. NO name shadowing (the
    // `EqualsExpression()` accessor does not collide with the `Expression` base type).
    Expression* EqualsExpression() const { return equalsExpression_; }
    void EqualsExpression(Expression* value) {
        SetChildNode(equalsExpression_, value, 4);
    }

    // ---- The `IntoIdentifierToken` slot (the backing token of the group-join name) ------
    // The generated `[Slot("IntoIdentifier")] string? IntoIdentifier`'s backing
    // `IntoIdentifierToken` child slot -- a single, OPTIONAL (nullable) `Identifier` at
    // flattened index 5 (absent for a non-group join without `into`). The const-index
    // `SetChildNode(ref field, value, 5)` setter. NO name shadowing (the `IntoIdentifierToken()`
    // accessor does NOT collide with the `Identifier` class).
    Identifier* IntoIdentifierToken() const { return intoIdentifierToken_; }
    void IntoIdentifierToken(Identifier* value) {
        SetChildNode(intoIdentifierToken_, value, 5);
    }

    // The `IntoIdentifier` string-name accessor (over the token). An OPTIONAL name (the C#
    // `string?`): `get` returns null when the token is absent; `set` creates the token via
    // `Identifier::CreateIfNotEmpty`, so an empty/null name clears the token (the C#
    // `IntoIdentifierToken = Identifier.CreateIfNotEmpty(value)`). `IntoIdentifier()` returns
    // `std::optional<std::string>` (nullopt when the token is absent -- the faithful `string?`);
    // the `Identifier::CreateIfNotEmpty` factory call is unqualified (the `IntoIdentifier()`
    // accessor does NOT shadow the `Identifier` class -- the `GotoStatement.Label` D257 /
    // `CatchClause.VariableName` D269 differently-named-property precedent).
    std::optional<std::string> IntoIdentifier() const {
        return intoIdentifierToken_ != nullptr
            ? std::optional<std::string>(intoIdentifierToken_->Name()) : std::nullopt;
    }
    void IntoIdentifier(std::string_view value) {
        IntoIdentifierToken(Identifier::CreateIfNotEmpty(value));
    }

    // ---- The `IsGroupJoin` computed property --------------------------
    // The C# `[ExcludeFromMatch] public bool IsGroupJoin { get { return
    // !string.IsNullOrEmpty(this.IntoIdentifier); } }` -- a get-only bool derived from
    // `IntoIdentifier`: true iff the `into` group-join identifier is present and non-empty. A
    // computed read-only property (NOT a `[Slot]`, NOT a ctor param), and `[ExcludeFromMatch]`
    // so it is NOT in `MembersToMatch`/`DoMatch` (the C# source comment: the `IntoIdentifier` term
    // already compares it, so a second compare would be redundant). The faithful `string?`
    // null/empty test on the `std::optional<std::string>`: "null" is `nullopt` (no token), "empty"
    // is the optional holding an empty string; `IsNullOrEmpty` is the disjunction, so
    // `IsGroupJoin` is the conjunction `has_value() && !empty()`.
    bool IsGroupJoin() const {
        return IntoIdentifier().has_value() && !IntoIdentifier()->empty();
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // `TypeSlot` (`CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, optional -- the type is
    // nullable); `JoinIdentifierTokenSlot` (`CSharpSlotInfoT<Identifier>` pointing at
    // `Slots.JoinIdentifier`, required -- the join name is non-nullable); `InExpressionSlot`/
    // `OnExpressionSlot`/`EqualsExpressionSlot` (`CSharpSlotInfoT<Expression>` pointing at
    // `Slots.InExpression`/`Slots.OnExpression`/`Slots.EqualsExpression`, required);
    // `IntoIdentifierTokenSlot` (`CSharpSlotInfoT<Identifier>` pointing at
    // `Slots.IntoIdentifier`, optional -- the group-join name is nullable). `Slots::Type` (by
    // `Attribute` D240) already exists; the other five kinds are new this iteration. No name
    // shadowing (no member is named `AstType`/`Identifier`/`Expression`), so the element types
    // are the plain classes.
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, true};
    static inline const CSharpSlotInfoT<Identifier> JoinIdentifierTokenSlot{"JoinIdentifierToken", false, &Slots::JoinIdentifier, false};
    static inline const CSharpSlotInfoT<Expression> InExpressionSlot{"InExpression", false, &Slots::InExpression, false};
    static inline const CSharpSlotInfoT<Expression> OnExpressionSlot{"OnExpression", false, &Slots::OnExpression, false};
    static inline const CSharpSlotInfoT<Expression> EqualsExpressionSlot{"EqualsExpression", false, &Slots::EqualsExpression, false};
    static inline const CSharpSlotInfoT<Identifier> IntoIdentifierTokenSlot{"IntoIdentifierToken", false, &Slots::IntoIdentifier, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryJoinClause`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryJoinClause(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitQueryJoinClause`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitQueryJoinClause(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Six single slots at flattened indices 0..5 (`Type`/`JoinIdentifierToken`/`InExpression`/
    // `OnExpression`/`EqualsExpression`/`IntoIdentifierToken`); no collection, so `GetChildCount`
    // is the constant 6 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with six cases).

    int GetChildCount() const override { return 6; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            case 1: return joinIdentifierToken_;
            case 2: return inExpression_;
            case 3: return onExpression_;
            case 4: return equalsExpression_;
            case 5: return intoIdentifierToken_;
            default: throw std::out_of_range("QueryJoinClause::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(joinIdentifierToken_, static_cast<Identifier*>(value), 1); break;
            case 2: SetChildNode(inExpression_, static_cast<Expression*>(value), 2); break;
            case 3: SetChildNode(onExpression_, static_cast<Expression*>(value), 3); break;
            case 4: SetChildNode(equalsExpression_, static_cast<Expression*>(value), 4); break;
            case 5: SetChildNode(intoIdentifierToken_, static_cast<Identifier*>(value), 5); break;
            default: throw std::out_of_range("QueryJoinClause::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            case 1: return &JoinIdentifierTokenSlot;
            case 2: return &InExpressionSlot;
            case 3: return &OnExpressionSlot;
            case 4: return &EqualsExpressionSlot;
            case 5: return &IntoIdentifierTokenSlot;
            default: throw std::out_of_range("QueryJoinClause::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The six `MembersToMatch` terms in source declaration order: `MatchOptional(Type)` +
    // `MatchString(JoinIdentifier)` + `MatchRequired(InExpression)` + `MatchRequired(OnExpression)`
    // + `MatchRequired(EqualsExpression)` + `MatchString(IntoIdentifier)`. The `Type` term is a
    // NULLABLE recursive child (`MatchOptional`); the `JoinIdentifier` term is a non-nullable
    // `String` `MatchString` (the `$any$` wildcard in the pattern's `JoinIdentifier` matches any
    // candidate name; the backing `JoinIdentifierToken` never appears in `MembersToMatch`); the
    // `InExpression`/`OnExpression`/`EqualsExpression` terms are NON-NULLABLE recursive children,
    // so the generator emits DIRECT `this.X.DoMatch(o.X, match)` calls -- ported through
    // `MatchRequired` (the D231 `[class.access.derived]` workaround); the `IntoIdentifier` term
    // is a nullable `String` `MatchString` (nullopt passes through as the C# null). `IsGroupJoin`
    // is `[ExcludeFromMatch]`, so it never appears here. A type-only mismatch rejects early.
    //
    // `JoinIdentifier()` returns `std::string` (the non-nullable token's name); the
    // `MatchString` arguments are built per side (`std::string_view` needs one explicit
    // construction from `std::string`, the D227 rule). The `JoinIdentifier()` calls are INLINED
    // in the `MatchString` arguments (not pre-computed in locals) so the C# `&&` short-circuits:
    // `o->JoinIdentifier()` derefs the candidate's token only when the `Type` term passed,
    // avoiding eager UB on a half-constructed candidate; the `std::string` temporaries live until
    // the end of the full `return` expression so the `std::string_view` views stay valid for the
    // `MatchString` call (the D227 / D238 inlined-MatchString-arguments precedent). The nullable
    // `IntoIdentifier` term uses locals (`thisInto`/`otherInto`) and the `optional<string_view>`
    // conversion (the `CatchClause.VariableName` D269 precedent); it is the last term so it
    // returns directly.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryJoinClause*>(other);
        if (o == nullptr)
            return false;
        if (!MatchOptional(type_, o->type_, match))
            return false;
        if (!PatternMatching::Pattern::MatchString(
                std::string_view(this->JoinIdentifier()),
                std::string_view(o->JoinIdentifier())))
            return false;
        if (!MatchRequired(inExpression_, o->inExpression_, match))
            return false;
        if (!MatchRequired(onExpression_, o->onExpression_, match))
            return false;
        if (!MatchRequired(equalsExpression_, o->equalsExpression_, match))
            return false;
        auto thisInto = IntoIdentifier();
        auto otherInto = o->IntoIdentifier();
        return PatternMatching::Pattern::MatchString(
            thisInto ? std::optional<std::string_view>(*thisInto) : std::nullopt,
            otherInto ? std::optional<std::string_view>(*otherInto) : std::nullopt);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the six
    // children deep-cloned through the setters when present (which re-parent and re-index via
    // `SetChildNode`). `Clone` tolerates missing nullable children (the `Type`/
    // `IntoIdentifierToken` may be absent); the required `JoinIdentifierToken`/`InExpression`/
    // `OnExpression`/`EqualsExpression` are also skipped if absent (the invariant is enforced by
    // `CheckInvariant`, not by `Clone`). No scalar to copy (the `JoinIdentifier`/`IntoIdentifier`
    // strings are derived from their tokens, so cloning the tokens carries them; `IsGroupJoin`
    // is computed, no state). No own location fields (`QueryJoinClause` does not derive
    // `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
    // `CatchClause` D269 / `QueryFromClause` D311 no-location-copy precedent). The covariant
    // return is `QueryJoinClause*` (through `QueryClause*`, the `QueryClause::Clone`
    // pure-virtual). No elaborated specifiers (no member is named `AstType`/`Identifier`/
    // `Expression`); the child `Clone()` calls return the typed pointers the setters accept
    // directly (`AstType::Clone` -> `AstType*`, `Identifier::Clone` -> `Identifier*`,
    // `Expression::Clone` -> `Expression*`).
    QueryJoinClause* Clone() const override {
        auto* node = new QueryJoinClause();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (joinIdentifierToken_ != nullptr)
            node->JoinIdentifierToken(joinIdentifierToken_->Clone());
        if (inExpression_ != nullptr)
            node->InExpression(inExpression_->Clone());
        if (onExpression_ != nullptr)
            node->OnExpression(onExpression_->Clone());
        if (equalsExpression_ != nullptr)
            node->EqualsExpression(equalsExpression_->Clone());
        if (intoIdentifierToken_ != nullptr)
            node->IntoIdentifierToken(intoIdentifierToken_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_`/`intoIdentifierToken_` are null until set (optional slots --
    // `CheckInvariant` does NOT assert they are filled); `joinIdentifierToken_`/`inExpression_`/
    // `onExpression_`/`equalsExpression_` are null until set (REQUIRED slots -- `CheckInvariant`
    // asserts they are filled). No name shadowing (no member is named `AstType`/`Identifier`/
    // `Expression`), so the field types are the plain classes.
    AstType* type_ = nullptr;
    Identifier* joinIdentifierToken_ = nullptr;
    Expression* inExpression_ = nullptr;
    Expression* onExpression_ = nullptr;
    Expression* equalsExpression_ = nullptr;
    Identifier* intoIdentifierToken_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYJOINCLAUSE_HPP
