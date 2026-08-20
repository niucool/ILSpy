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

// Port of the `CatchClause` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/TryCatchStatement.cs (the generated
// `CatchClause.g.cs` + the hand-written partial, which declares only the four const strings and
// the four slot properties, no ctors, no helpers). Part of the try/catch family -- the next
// in-order Phase-5 piece per the D268 plan ("TryCatchStatement, LocalFunctionDeclarationStatement,
// VariableDeclarationStatement ...").
// `catch_clause ::= 'catch' ( '(' type identifier? ')' )? ( 'when' '(' expression ')' )? block`
// (C# grammar 13.11): an optional caught `Type` (absent for a bare `catch`), an optional caught
// `VariableName` (absent for `catch (T)` with no binding), an optional `Condition` (the `when`
// filter, absent for an unfiltered catch), and a REQUIRED `Body` block.
//
// The hand-written partial declares only the `CatchKeyword`/`WhenKeyword`/`CondLPar`/`CondRPar`
// const strings and the four slot properties. It is a NON-SEALED `AstNode` (the
// `[DecompilerAstNode(hasPatternPlaceholder: true)]` -- the `ArrayInitializerExpression` D250 /
// `VariableInitializer` D266 / `SwitchSection` D268 non-sealed precedent; the generated
// `PatternPlaceholder` derives from it, deferred but the class stays non-`final`) deriving
// DIRECTLY from the `AstNode` root (not `Expression`/`Statement`/`AstType`) -- an element of the
// `TryCatchStatement` `CatchClauses` collection.
//
// The four slots in source declaration order: a NULLABLE `AstType?` `Type` `[Slot("Type")]`
// single slot at flattened index 0 (the `ReturnStatement` D255 nullable-single-slot shape on an
// `AstType` child -- reusing the already-ported `Slots::Type` kind by `Attribute` D240); a
// NULLABLE `string?` `VariableName` `[Slot("Identifier")]` string-name `[Slot]` over a generated
// backing `VariableNameToken` `Identifier` single slot at flattened index 1 (the `GotoStatement`
// D257 nullable-string-name-`[Slot]` shape -- reusing the already-ported `Slots::Identifier` kind
// by `SimpleType` D237; an absent name is a null token, `Identifier::CreateIfNotEmpty`); a
// NULLABLE `Expression?` `Condition` `[Slot("Condition")]` single slot at flattened index 2 (the
// `ReturnStatement` D255 nullable-`Expression?`-single-slot shape -- reusing the already-ported
// `Slots::Condition` kind by `ConditionalExpression` D232); and a REQUIRED (non-nullable)
// `BlockStatement` `Body` `[Slot("Body")]` single slot at flattened index 3 (the
// `UnaryOperatorExpression` D231 required-single-slot shape on a `BlockStatement` child -- reusing
// the already-ported `Slots::Body` kind by `CheckedStatement` D260). All four are single slots (no
// collection), so the generator emits the const-index `SetChildNode(ref field, value, index)`
// setters (each flattened index is the constant slot position 0/1/2/3); `GetChildCount` is the
// constant 4 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat four-case index switch (the
// generator's `WriteReturnDispatchSwitch` shape).
//
// NO C++ name-shadowing crux (the `Attribute` D240 / `MemberType.MemberName` D238
// differently-named-property precedent): the accessors are `Type`/`VariableName`/`Condition`/`Body`
// -- none is named `AstType`/`Identifier`/`Expression`/`BlockStatement`, and no class named
// `Type`/`VariableName`/`Condition`/`Body` lives in the `Syntax` namespace (there is `AstType`, not
// `Type`; `Slots::Condition`/`Slots::Body` live in the `Slots` namespace, a different namespace).
// So no elaborated-type-specifier is needed anywhere, and the `Identifier::CreateIfNotEmpty`
// factory call in the `VariableName` setter is unqualified. (The `VariableName` string accessor is
// `VariableName()`, NOT `Identifier()`, so it does NOT shadow the `Identifier` class -- the
// `GotoStatement.Label` D257 / `MemberType.MemberName` D238 differently-named-property precedent
// applied to the nullable-string-name-`[Slot]` case.)
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is CatchClause o && MatchOptional(this.Type, o.Type, match) &&
// MatchString(this.VariableName, o.VariableName) && MatchOptional(this.Condition, o.Condition,
// match) && this.Body.DoMatch(o.Body, match)`. The `Type` and `Condition` terms are NULLABLE
// recursive children, so the generator emits `MatchOptional` for each (both absent, or both
// present and the pattern's `DoMatch` decides); the `VariableName` term is a `String` `MatchString`
// (the `$any$` wildcard in the pattern's `VariableName` matches any candidate name; the backing
// `VariableNameToken` is a generated non-partial `[Slot]`, not seen by the source-property scan,
// so it never appears in `MembersToMatch` -- no double-match); the `Body` term is a NON-NULLABLE
// recursive child, so the generator emits a DIRECT `this.Body.DoMatch(o.Body, match)` -- ported
// through `MatchRequired` (the D231 `[class.access.derived]` workaround). A type-only mismatch (not
// a `CatchClause`) rejects early. A bare `catch {}` (no `Type`, no `VariableName`, no `Condition`)
// matches another bare `catch {}` (all three nullable terms both-absent), and the `Body` decides.
//
// The generated ctors (the generator's `WriteConstructors`): the `Type`/`Condition` are nullable
// (optional ctor params), the `VariableName` string-name `[Slot]` is a "required" ctor param
// regardless of optionality (the generator's line-168 rule), and the `Body` is required
// (non-optional). `RequiredConstructorPrefixLength` is 4 (the last non-optional param `Body` is at
// index 3, so `reqLen = 4`), and `ConstructorPrefixLengths` is {4} (no collection, so no
// collection prefix, and `reqLen == cp.Count == 4` so the only length is the full count). The only
// non-empty ctor is therefore the all-params `(AstType? type, string variableName,
// Expression? condition, BlockStatement body)` ctor, which chains to the empty ctor then sets all
// four slots. The four-arg ctor is NOT a converting ctor (multi-arg), so it needs no `explicit`.
// `CatchClause.cs` declares NO hand-written ctors (only the const strings and the slot
// properties), so the port carries only the generated ctors.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_CATCHCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_CATCHCLAUSE_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public partial class CatchClause : AstNode` (non-sealed -- `hasPatternPlaceholder: true`).
// NOT `final` (the C# is not `sealed`): the generated `PatternPlaceholder` derives from it (the
// `ArrayInitializerExpression` D250 / `SwitchSection` D268 non-sealed precedent; the pattern
// placeholder is deferred, but the class stays non-`final` to match the C# and to not block the
// placeholder landing). An element of a `TryCatchStatement`'s `CatchClauses` collection -- a
// `catch` clause with an optional caught type, an optional caught variable name, an optional
// `when` filter, and a required body block.
class CatchClause : public AstNode {
public:
    ~CatchClause() override = default;

    // The generated empty ctor (the C# `public CatchClause()`). All four slots default to null
    // (a bare `catch` with no type, no variable, no condition); the `Body` is required, so a
    // default-constructed node is only valid until the `Body` is set (or until `DoMatch`/
    // `CheckInvariant` observe the missing body). The three nullable slots may stay null.
    CatchClause() = default;

    // The generated all-params ctor (the C# `public CatchClause(AstType? type, string
    // variableName, Expression? condition, BlockStatement body)`). `RequiredConstructorPrefixLength`
    // is 4 (the last required param `Body` is at index 3), and `ConstructorPrefixLengths` is {4}
    // (no collection, `reqLen == cp.Count`), so this four-arg form IS the only non-empty ctor (no
    // shorter prefix ctor, no `params` overload). The body chains to the empty ctor then sets all
    // four slots in declaration order. The `VariableName` setter creates the token via
    // `Identifier::CreateIfNotEmpty` (an empty/null name clears the token, faithful to the C#
    // `string?` optionality). NOT `explicit` (a four-argument ctor is not a converting ctor).
    CatchClause(AstType* type, std::string variableName, Expression* condition, BlockStatement* body)
        : CatchClause() {
        Type(type);
        VariableName(std::move(variableName));
        Condition(condition);
        Body(body);
    }

    // ---- The const keyword tokens (the output-visitor token literals) ----------------
    // The C# `public const string CatchKeyword = "catch"` / `WhenKeyword = "when"` /
    // `CondLPar = "("` / `CondRPar = ")"`. Part of the node's public API (the output visitor
    // reads them); port as `static constexpr const char*` (the `CheckedExpression.CheckedKeyword`
    // D234 / `IfElseStatement.IfKeyword` D258 precedent). The generator excludes const string
    // fields from `MembersToMatch` (it iterates only instance `IPropertySymbol`s), so they never
    // appear in the generated `DoMatch`.
    static constexpr const char* CatchKeyword = "catch";
    static constexpr const char* WhenKeyword = "when";
    static constexpr const char* CondLPar = "(";
    static constexpr const char* CondRPar = ")";

    // ---- The `Type` slot (the optional caught type) -----------------------------------
    // The C# `[Slot("Type")] public partial AstType? Type` -- a single, NULLABLE `AstType` child
    // at flattened index 0 (the caught exception type, absent for a bare `catch`). The generator
    // emits the const-index `SetChildNode(ref field, value, 0)` setter (no collection precedes
    // it), so the index is assigned directly and the parent's indices stay valid by
    // construction. No name shadowing (the `Type()` accessor does not collide with the `AstType`
    // base type -- no class named `Type` lives in the `Syntax` namespace, the `Attribute` D240
    // precedent), so the operand type is the plain `AstType` (no elaborated specifier).
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `VariableNameToken` slot (the backing token of the caught variable name) -
    // The generated `[Slot("Identifier")] public partial Identifier? VariableNameToken` -- a
    // single, OPTIONAL (nullable) `Identifier` slot at flattened index 1 (the backing token of
    // the `VariableName` string-name `[Slot]`, absent for `catch (T)` with no binding). The
    // const-index `SetChildNode(ref field, value, 1)` setter re-parents and re-indexes in place.
    // No name shadowing (the `VariableNameToken()` accessor does NOT collide with the `Identifier`
    // class -- no member is named `Identifier`), so the operand type is the plain `Identifier`.
    Identifier* VariableNameToken() const { return variableNameToken_; }
    void VariableNameToken(Identifier* value) {
        SetChildNode(variableNameToken_, value, 1);
    }

    // ---- The `VariableName` string-name accessor (over the token) --------------------
    // The generated `public partial string? VariableName` -- a convenience string over the
    // `VariableNameToken` slot. An OPTIONAL name (the C# `string?`): `get` returns null when the
    // token is absent; `set` creates the token via `Identifier.CreateIfNotEmpty`, so an
    // empty/null name clears the token (the C# `VariableNameToken =
    // Identifier.CreateIfNotEmpty(value)`). `VariableName()` returns `std::optional<std::string>`
    // (nullopt when the token is absent -- the faithful `string?`); the `Identifier::CreateIfNotEmpty`
    // factory call is unqualified (the `VariableName()` accessor does NOT shadow the `Identifier`
    // class -- no member is named `Identifier` -- the `GotoStatement.Label` D257
    // differently-named-property precedent).
    std::optional<std::string> VariableName() const {
        return variableNameToken_ != nullptr
            ? std::optional<std::string>(variableNameToken_->Name()) : std::nullopt;
    }
    void VariableName(std::string_view value) {
        VariableNameToken(Identifier::CreateIfNotEmpty(value));
    }

    // ---- The `Condition` slot (the optional `when` filter) ----------------------------
    // The C# `[Slot("Condition")] public partial Expression? Condition` -- a single, NULLABLE
    // `Expression` child at flattened index 2 (the `when` filter, absent for an unfiltered
    // `catch`). The generator emits the const-index `SetChildNode(ref field, value, 2)` setter.
    // No name shadowing (the `Condition()` accessor does not collide with the `Expression` base
    // type -- no member is named `Expression`), so the operand type is the plain `Expression`.
    Expression* Condition() const { return condition_; }
    void Condition(Expression* value) {
        SetChildNode(condition_, value, 2);
    }

    // ---- The `Body` slot (the required catch body block) -------------------------------
    // The C# `[Slot("Body")] public partial BlockStatement Body` -- a single, REQUIRED
    // (non-nullable) `BlockStatement` child at flattened index 3 (the catch body block). The
    // generator emits the const-index `SetChildNode(ref field, value, 3)` setter. No name
    // shadowing (the `Body()` accessor does not collide with the `BlockStatement` class -- no
    // class named `Body` lives in the `Syntax` namespace), so the operand type is the plain
    // `BlockStatement`.
    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value, 3);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, optional -- the
    // caught type is nullable); the `VariableNameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
    // pointing at `Slots.Identifier`, optional -- the caught variable name is nullable); the
    // `ConditionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Condition`, optional --
    // the `when` filter is nullable); the `BodySlot` (a `CSharpSlotInfoT<BlockStatement>` pointing
    // at `Slots.Body`, required -- the body is non-nullable). All four kinds are already ported
    // (`Slots::Type` by `Attribute` D240, `Slots::Identifier` by `SimpleType` D237, `Slots::Condition`
    // by `ConditionalExpression` D232, `Slots::Body` by `CheckedStatement` D260), so no new `Slots`
    // constant in `Slots.hpp`. No name shadowing (no member is named `AstType`/`Identifier`/
    // `Expression`/`BlockStatement`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, true};
    static inline const CSharpSlotInfoT<Identifier> VariableNameTokenSlot{"VariableNameToken", false, &Slots::Identifier, true};
    static inline const CSharpSlotInfoT<Expression> ConditionSlot{"Condition", false, &Slots::Condition, true};
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitCatchClause` (`CatchClause` does not end in "AstType", so the
    // generator's visit-method-name default yields `VisitCatchClause`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitCatchClause(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitCatchClause`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitCatchClause(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Four single slots at flattened indices 0/1/2/3 (`Type`/`VariableNameToken`/`Condition`/
    // `Body`); no collection, so `GetChildCount` is the constant 4 and `GetChild`/`SetChild`/
    // `GetChildSlotInfo` are a flat index switch (the generator's `WriteReturnDispatchSwitch`
    // shape, with four cases).

    int GetChildCount() const override { return 4; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            case 1: return variableNameToken_;
            case 2: return condition_;
            case 3: return body_;
            default: throw std::out_of_range("CatchClause::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(variableNameToken_, static_cast<Identifier*>(value), 1); break;
            case 2: SetChildNode(condition_, static_cast<Expression*>(value), 2); break;
            case 3: SetChildNode(body_, static_cast<BlockStatement*>(value), 3); break;
            default: throw std::out_of_range("CatchClause::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            case 1: return &VariableNameTokenSlot;
            case 2: return &ConditionSlot;
            case 3: return &BodySlot;
            default: throw std::out_of_range("CatchClause::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is CatchClause o && MatchOptional(this.Type, o.Type, match) &&
    // MatchString(this.VariableName, o.VariableName) && MatchOptional(this.Condition,
    // o.Condition, match) && this.Body.DoMatch(o.Body, match)`. The four terms are in
    // `MembersToMatch` order, which is the source declaration order (`Type`, `VariableName`,
    // `Condition`, `Body`). The `Type` and `Condition` terms are NULLABLE recursive children, so
    // the generator emits `MatchOptional` for each (both absent, or both present and the
    // pattern's `DoMatch` decides); the `VariableName` term is a `String` `MatchString` (the
    // `$any$` wildcard in the pattern's `VariableName` matches any candidate name; the backing
    // `VariableNameToken` never appears in `MembersToMatch`); the `Body` term is a NON-NULLABLE
    // recursive child, so the generator emits a DIRECT `this.Body.DoMatch(o.Body, match)` --
    // ported through `MatchRequired` (the D231 `[class.access.derived]` workaround, since a
    // derived node may not call the protected `DoMatch` through a base `BlockStatement*`).
    // `MatchRequired` guards a missing pattern-side `Body` defensively (a null pattern `Body` does
    // not match; the C# would null-deref), and a null candidate `Body` flows through the child's
    // `DoMatch(nullptr)` which returns false. For well-formed nodes (the `Body` set) the behavior
    // is identical to the C#. A type-only mismatch (not a `CatchClause`) rejects early.
    // `VariableName()` returns `std::optional<std::string>` (nullopt when the token is absent);
    // `Pattern::MatchString` takes `std::optional<std::string_view>`, so the view is built per
    // side (nullopt passes through as the C# null -- a `catch (T)` with no binding matches another
    // binding-less `catch (T)`). The `std::string` temporaries live until the end of the full
    // `return` expression, keeping the `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<CatchClause*>(other);
        if (o == nullptr)
            return false;
        if (!MatchOptional(type_, o->type_, match))
            return false;
        auto thisName = VariableName();
        auto otherName = o->VariableName();
        if (!PatternMatching::Pattern::MatchString(
                thisName ? std::optional<std::string_view>(*thisName) : std::nullopt,
                otherName ? std::optional<std::string_view>(*otherName) : std::nullopt))
            return false;
        if (!MatchOptional(condition_, o->condition_, match))
            return false;
        return MatchRequired(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the four
    // children deep-cloned through the setters when present (which re-parent and re-index via
    // `SetChildNode`). `Clone` tolerates missing nullable children (the `Type`/`VariableNameToken`/
    // `Condition` may be absent); the required `Body` is also skipped if absent (the invariant is
    // enforced by `CheckInvariant`, not by `Clone`). No scalar to copy (the `VariableName` string
    // is derived from the token, so cloning the token carries it). No own location fields
    // (`CatchClause` does not derive `EndLocation`), so the print-time `StartLocation`/
    // `EndLocation` are not copied (the `CaseLabel` D268 no-location-copy precedent). The covariant
    // return is `CatchClause*` (through `AstNode*`, the `AstNode::Clone` virtual -- `CatchClause`
    // does not re-declare a typed `Clone` since the C# has no hand-written typed `Clone`,
    // faithful to the `hasPatternPlaceholder:true` non-sealed form). No elaborated specifiers (no
    // member is named `AstType`/`Identifier`/`Expression`/`BlockStatement`); the child `Clone()`
    // calls return the typed pointers the setters accept directly.
    CatchClause* Clone() const override {
        auto* node = new CatchClause();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (variableNameToken_ != nullptr)
            node->VariableNameToken(variableNameToken_->Clone());
        if (condition_ != nullptr)
            node->Condition(condition_->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_`/`variableNameToken_`/`condition_` are null until set (optional
    // slots -- `CheckInvariant` does NOT assert they are filled); `body_` is null until the body
    // is set (a REQUIRED slot -- `CheckInvariant` asserts it is filled). No name shadowing (no
    // member is named `AstType`/`Identifier`/`Expression`/`BlockStatement`), so the field types
    // are the plain classes.
    AstType* type_ = nullptr;
    Identifier* variableNameToken_ = nullptr;
    Expression* condition_ = nullptr;
    BlockStatement* body_ = nullptr;
};

// The `CatchClause` kind -- the collection slot kind for every
// `[Slot("CatchClause")] AstNodeCollection<CatchClause>` (`TryCatchStatement.CatchClauses`). A
// `CSharpSlotInfoT<CatchClause>` (the element type is the concrete `CatchClause` node).
//
// Defined HERE (in CatchClause.hpp, after the `CatchClause` class) rather than in Slots.hpp
// because `CSharpSlotInfoT<CatchClause>` needs `CatchClause` complete (the
// `dynamic_cast<const CatchClause*>` is-a test in the ctor), and `CatchClause` is a concrete node
// with per-node slot statics (its `TypeSlot`/`VariableNameTokenSlot`/`ConditionSlot`/`BodySlot`
// reference `&Slots::Type`/`&Slots::Identifier`/`&Slots::Condition`/`&Slots::Body`, so this header
// includes Slots.hpp). Placing the kind in Slots.hpp would form a circular include (the
// `Slots::Attribute` D241 / `Slots::AttributeSection` D242 / `Slots::CaseLabel` D248 /
// `Slots::SwitchSection` D268 cycle-breaking precedent): Slots.hpp would have to include
// CatchClause.hpp (for the complete `CatchClause`), but CatchClause.hpp includes Slots.hpp (for
// `Slots::Type`/`Slots::Identifier`/`Slots::Condition`/`Slots::Body`), and with Slots.hpp's guard
// set those definitions would not be visible where CatchClause's class body needs them. After
// the class both `CSharpSlotInfoT` (visible via the Slots.hpp include) and `CatchClause` are
// complete, so the kind defines cleanly. The `inline` variable still has external linkage and one
// address across translation units (the C++17 `inline` guarantee), preserving the
// pointer-identity comparison `node.Slot.Kind == &Slots::CatchClause` the slot system relies on.
// The shared constant is constructed non-collection/non-optional
// (`{"CatchClause", false, nullptr, false}`); the per-node `CatchClausesSlot` on the owning
// `TryCatchStatement` carries the `IsCollection` flag (the collection `[Slot]` makes the per-node
// slot a collection). The kind name `CatchClause` collides with the `CatchClause` CLASS in the
// parent `Syntax` namespace (the `Expression`/`Identifier`/`Statement`/`CaseLabel` collision
// pattern): the template argument in this definition resolves to the class (the constant being
// declared is not yet in scope at the point its type is parsed), and a LATER `Slots` entry
// wanting the `CatchClause` class as its element type must qualify it
// (`::ILSpy::Decompiler::CSharp::Syntax::CatchClause`) to avoid resolving to this constant.
namespace Slots {
inline const CSharpSlotInfoT<CatchClause> CatchClause{"CatchClause", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_CATCHCLAUSE_HPP
