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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OF OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `QueryLetClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryLetClause.g.cs` + the hand-written partial, which declares only the `Identifier`
// and `Expression` slot properties plus the `LetKeyword` const -- no ctors, no helpers).
// The next in-order Phase-5 piece per the D310 plan ("the remaining string-name-[Slot]
// and multi-slot clauses"). `let_clause ::= 'let' identifier '=' expression` (C# grammar
// 12.23.1): a `QueryClause` introducing a range variable bound to an `Expression`.
//
// It is the `VariableInitializer` D266 shape (a REQUIRED non-nullable string `Identifier`
// string-name `[Slot]` over a backing `IdentifierToken` plus a REQUIRED `Expression` slot)
// applied to the `QueryClause` hierarchy. The `Identifier` is a NON-nullable `string` (the
// C# source declares `public partial string Identifier`, not `string?`), so the backing token
// is a REQUIRED (non-nullable) slot, the string getter DEREFS the token (returning
// `std::string`, not `std::optional<std::string>` -- a null token is a half-constructed node
// that would `NullReferenceException` in C#), and the string setter uses `Identifier::Create`
// (NOT `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so
// an empty name yields a token with an empty `Name`, not a null token -- the
// `MemberType.MemberName` D238 / `LabelStatement` D259 / `IdentifierExpression` D246
// precedent). The `Expression` is a REQUIRED `Expression` slot.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitQueryLetClause(this)`. The generated slot statics are `IdentifierTokenSlot`
// (a `CSharpSlotInfo<Identifier>` pointing at `Slots.Identifier`, required -- the name is
// non-nullable so the token is a required slot) and `ExpressionSlot` (a
// `CSharpSlotInfo<Expression>` pointing at `Slots.Expression`, required). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): deep-clones the token and the `Expression` through the setters (which
// re-parent), and copies the annotation channel.
//
// C++ name-shadowing crux (the D237 `SimpleType` / D246 `IdentifierExpression` precedent):
// the string accessor `Identifier()` (a member function) shadows the `Identifier` CLASS in
// this class scope (C++ unqualified name lookup finds the member and stops, even though it is
// not a type -- the D224 `Annotation<T>()`-shadows-the-`Annotation`-type crux), so the token
// type is the elaborated-type-specifier `class Identifier` (`basic.lookup.elab` ignores
// non-type names) in every type position AFTER the accessor (the backing field, the slot
// static's element type, the `SetChild` `static_cast`, the token accessor's signature, and the
// ctor parameter), and the `Identifier::Create` factory call in the setter is fully-qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Identifier::...`) -- the C# generator's `global::`
// qualification for the same shadowing case. The `Expression()` accessor likewise shadows
// the `Expression` base type (the D231 `UnaryOperatorExpression` / D243 `CastExpression`
// crux), so every type position AFTER the `Expression()` getter uses the elaborated
// `class Expression` (the setter param, the slot static element type, the `SetChild`
// `static_cast`, the backing field); the ctor param and the getter return type PRECEDE the
// getter so they use the plain `Expression`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYLETCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYLETCLAUSE_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
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

// The C# `public sealed partial class QueryLetClause : QueryClause`. `final` (the C#
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false,
// so no `PatternPlaceholder` derives from it). The two-required-single-slot shape (a
// non-nullable string `Identifier` + a required `Expression`) applied to the `QueryClause`
// hierarchy.
class QueryLetClause final : public QueryClause {
public:
    ~QueryLetClause() override = default;

    // The C# `public const string LetKeyword = "let"` -- the token the output visitor emits
    // for the `let` keyword. Compile-time literal carried as `static constexpr const char*`
    // (a static field, not instance state, so it is not part of `MembersToMatch`/`DoMatch`).
    static constexpr const char* LetKeyword = "let";

    // The generated empty ctor (the C# `public QueryLetClause()`). Both slots default to null
    // (no name, no expression). Null slots violate the required-slot invariants, so a
    // default-constructed node is only valid until `Identifier`/`Expression` are set (or until
    // `DoMatch`/`CheckInvariant` observe the missing children).
    QueryLetClause() = default;

    // The generated all-params ctor (the C# `public QueryLetClause(string identifier,
    // Expression expression)`); both slots are REQUIRED, so `RequiredConstructorPrefixLength`
    // equals `ctorParams.Count` and this is the only parametrized ctor (no shorter prefix, no
    // collection so no `params` overload). The generated body sets the slots via the property
    // setters; the string setter creates the token via `Identifier::Create`. `identifier` is
    // a `string` (the ctor param type precedes the `Identifier()` accessor, so the plain type
    // resolves to the `std::string`), and `expression` is the plain `Expression` (the ctor
    // param precedes the `Expression()` getter).
    QueryLetClause(std::string identifier, Expression* expression) : QueryLetClause() {
        Identifier(std::move(identifier));
        Expression(expression);
    }

    // ---- The `IdentifierToken` slot (the backing token of the string name) ------------
    // The generated `[Slot("Identifier")] string Identifier`'s backing `IdentifierToken`
    // child slot -- a REQUIRED (non-nullable) `Identifier` at flattened index 0. The
    // const-index `SetChildNode(ref field, value, 0)` setter (the slot is the first and no
    // collection precedes it) re-parents and re-indexes in place. `class Identifier` (the
    // `Identifier()` accessor shadows the class).
    class Identifier* IdentifierToken() const { return identifierToken_; }
    void IdentifierToken(class Identifier* value) {
        SetChildNode(identifierToken_, value, 0);
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
    // (non-nullable) `Expression` child at flattened index 1. The const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it). `class
    // Expression` (the `Expression()` accessor shadows the `Expression` base type).
    class Expression* Expression() const { return expression_; }
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 1);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // `class Identifier` (the `Identifier()` accessor shadows the class); `class Expression`
    // (the `Expression()` accessor shadows the base type). Both slots are required
    // (`IsOptional` is false). `Slots::Identifier` (by `SimpleType` D237) and `Slots::Expression`
    // (by `UnaryOperatorExpression` D231) already exist, so no new `Slots.hpp` constant.
    static inline const CSharpSlotInfoT<class Identifier> IdentifierTokenSlot{"IdentifierToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitQueryLetClause`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitQueryLetClause(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0/1; no collection, so `GetChildCount` is the
    // constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return identifierToken_;
            case 1: return expression_;
            default: throw std::out_of_range("QueryLetClause::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(identifierToken_, static_cast<class Identifier*>(value), 0); break;
            case 1: SetChildNode(expression_, static_cast<class Expression*>(value), 1); break;
            default: throw std::out_of_range("QueryLetClause::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &IdentifierTokenSlot;
            case 1: return &ExpressionSlot;
            default: throw std::out_of_range("QueryLetClause::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is QueryLetClause o && MatchString(this.Identifier, o.Identifier) &&
    // this.Expression.DoMatch(o.Expression, match)` (source declaration order: `Identifier`
    // before `Expression`). The `Identifier` term is a `String` `MatchString` (the `$any$`
    // wildcard in the pattern's `Identifier` matches any candidate name); the `Expression`
    // term is a NON-NULLABLE recursive child, so the generator emits the direct
    // `this.Expression.DoMatch(o.Expression, match)` term (NOT `MatchOptional`). The
    // `LetKeyword` const string is a static field, not an instance property, so it is not
    // part of `MembersToMatch` and does not appear here. A type-only mismatch (not a
    // `QueryLetClause`) rejects early.
    //
    // The C# direct dispatch (`this.Expression.DoMatch`) assumes the required child is
    // present; the port routes it through `AstNode::MatchRequired` (the same-class static
    // helper) because C++ `[class.access.derived]` forbids a derived node from calling the
    // protected `DoMatch` through a base `Expression*`. `MatchRequired` guards a missing
    // operand defensively (a null pattern child does not match; the C# would null-deref), and a
    // null candidate child flows through the operand's `DoMatch(nullptr)` which returns false.
    //
    // `Identifier()` returns `std::string` (the token's name); `Pattern::MatchString` takes
    // `std::optional<std::string_view>`, so the view is built per argument (`std::string_view`
    // needs one explicit construction from `std::string`, the D227 rule). The `Identifier()`
    // calls are INLINED in the `MatchString` arguments (not pre-computed in locals) so the C#
    // `&&` short-circuits: `o->Identifier()` derefs the candidate's token only when the type
    // check passed, avoiding eager UB on a half-constructed candidate; the `std::string`
    // temporaries live until the end of the full `return` expression so the
    // `std::string_view` views stay valid for the `MatchString` call (the D227 / D238
    // inlined-MatchString-arguments precedent).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<QueryLetClause*>(other);
        if (o == nullptr)
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
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `IdentifierToken` deep-cloned through the setter when present (which re-parents and
    // re-indexes via `SetChildNode`), and the `Expression` deep-cloned through the setter when
    // present. No own location fields (`StartLocation`/`EndLocation` are the print-time base
    // fields set by the unported output visitor), so they are not copied (the
    // `VariableInitializer` D266 / `UnaryOperatorExpression` D231 no-location-copy precedent).
    // The covariant return is `QueryLetClause*` (through `QueryClause*`, the `QueryClause::Clone`
    // pure-virtual). Each child is skipped if absent (`Clone` tolerates a missing child even
    // though the slot is required -- the invariant is enforced by `CheckInvariant`, not by
    // `Clone`). `class Identifier`/`class Expression` (the accessors shadow the classes); the
    // token `Clone()` returns `Identifier*` and the expression `Clone()` returns `Expression*`,
    // which the typed setters accept directly.
    QueryLetClause* Clone() const override {
        auto* node = new QueryLetClause();
        node->CloneAnnotationsFrom(*this);
        if (identifierToken_ != nullptr)
            node->IdentifierToken(identifierToken_->Clone());
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `class Identifier` (the `Identifier()` accessor shadows the class);
    // `class Expression` (the `Expression()` accessor shadows the base type). Required slots are
    // non-null only by invariant, so the pointers are null until the slots are set.
    class Identifier* identifierToken_ = nullptr;
    class Expression* expression_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYLETCLAUSE_HPP
