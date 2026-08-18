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

// Port of the `VariableInitializer` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/VariableInitializer.cs (the generated
// `VariableInitializer.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D265 plan ("FixedStatement needing the VariableInitializer node ... then SwitchStatement,
// TryCatchStatement, ... VariableDeclarationStatement needing VariableInitializer"). The
// `variable_declarator ::= identifier ( '=' expression )?` (C# grammar 15.5.1): the element of a
// `VariableDeclaration`/`FixedStatement`'s `Variables` collection (a `name = initializer` pair),
// the common dependency of `FixedStatement.Variables` (`AstNodeCollection<VariableInitializer>`)
// and `VariableDeclarationStatement.Variables` -- the first ported node of the C# `TypeMembers`
// sub-namespace.
//
// The hand-written partial declares only the two slot properties (no ctors, no helpers, no const
// strings). The `[DecompilerAstNode(hasPatternPlaceholder: true)]` (the explicit `hasPatternPlaceholder:
// true` argument) means the node is NOT `sealed` (the generated `PatternPlaceholder` derives from
// it -- the `ArrayInitializerExpression` D250 non-sealed precedent; the pattern placeholder is
// deferred, but the class stays non-`final` to match the C# and to not block the placeholder
// landing). It derives DIRECTLY from `AstNode` (not `Expression`/`Statement`/`AstType`), the first
// ported `TypeMembers` node and the first non-sealed node deriving directly from the `AstNode`
// root.
//
// The two slots in source declaration order: a REQUIRED (non-nullable) `string Name` string-name
// `[Slot("Identifier")]` over a backing `NameToken` `Identifier` slot at flattened index 0 (the
// `LabelStatement` D259 / `SingleVariableDesignation` D264 non-nullable-string-name-`[Slot]` shape
// -- `Identifier::Create` not `CreateIfNotEmpty`, `std::string` return, required token), and a
// NULLABLE `Expression?` `Initializer` `[Slot("Expression")]` single slot at flattened index 1
// (the `ReturnStatement` D255 nullable-`Expression?`-single-slot shape -- `MatchOptional`). The
// generator emits the const-index `SetChildNode(ref field, value, index)` setters for both (no
// collection precedes either slot, so each flattened index is the constant slot position 0/1);
// `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat
// two-case index switch.
//
// NO C++ name-shadowing crux (unlike `SingleVariableDesignation` D264 / `SimpleType` D237 whose
// `Identifier()` accessor shadows the `Identifier` class): the `[Slot("Identifier")]` argument
// names the slot KIND "Identifier" but the PROPERTY is "Name", so the string accessor is `Name()`
// (NOT `Identifier()`) and the backing token accessor is `NameToken()` (NOT `IdentifierToken()`).
// Neither shadows the `Identifier` CLASS in this class scope (no member is named `Identifier`),
// so NO elaborated-type-specifier (`class Identifier`) is needed anywhere, and the
// `Identifier::Create` factory call in the `Name` setter is unqualified -- the `LabelStatement`
// D259 / `MemberType.MemberName` D238 / `AttributeSection` D241 differently-named-property
// precedent. The `Initializer()` accessor likewise does NOT collide with the `Expression` class
// (no member is named `Expression`), so no `class Expression` elaboration is needed either (the
// `MemberReferenceExpression.Target` D247 differently-named-property precedent).
//
// The NON-nullable `string Name` (vs `GotoStatement`'s nullable `string?`): the backing
// `NameToken` is a REQUIRED (non-nullable) slot (IsOptional=false), the `Name()` getter DEREFS the
// token (returning `std::string`, NOT `std::optional<std::string>` -- a null token is a
// half-constructed node that would `NullReferenceException` in C#), and the `Name` setter uses
// `Identifier::Create` (NOT `CreateIfNotEmpty` -- a non-nullable name creates a token even for an
// empty string, so an empty name yields a token with an empty `Name`, not a null token -- the
// `MemberType.MemberName` D238 / `LabelStatement.Label` D259 / `SingleVariableDesignation.Identifier`
// D264 precedent). `CheckInvariant` therefore passes only on a node whose `NameToken` is filled
// (the token is a required slot, so a default-constructed node violates the required-slot
// invariant -- the assert fires in debug), though the `Initializer` slot is nullable so its
// absence is invariant-valid. A nameless `VariableInitializer` is a half-constructed node (UB to
// deref or pass to `DoMatch`), so the tests exercise the empty-name case as a real "" match (two
// empty names match; empty does not match non-empty), not a nameless/nullopt case.
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is VariableInitializer o && MatchString(this.Name, o.Name) &&
// MatchOptional(this.Initializer, o.Initializer, match)`. The `Name` term is a `String`
// `MatchString` (the `$any$` wildcard in the pattern's `Name` matches any candidate name); the
// backing `NameToken` is a generated non-`partial` `[Slot]` (not seen by the source-property scan
// at generation time), so it never appears in `MembersToMatch` (no double-match). The `Initializer`
// term is a NULLABLE recursive child, so the generator emits `MatchOptional` (both absent, or both
// present and the pattern's `DoMatch` decides). A type-only mismatch (not a `VariableInitializer`)
// rejects early.
//
// The generated ctors (the generator's `WriteConstructors`): a string-name `[Slot]` is a
// "required" ctor param regardless of optionality (the generator's line-168 rule), and the
// `Initializer` is nullable so it is NOT in the required prefix -- `RequiredConstructorPrefixLength`
// is 1, `ConstructorPrefixLengths` is {1, 2} (the required prefix then the full count), and there
// is no collection so no `params` overload. The generated ctors are the empty ctor + the
// `(string name)` required-prefix ctor + the `(string name, Expression? initializer)` all-params
// ctor. The `(string name)` ctor body is `this.Name = name;` (it calls the string setter, which
// creates the token via `Identifier::Create`); the all-params ctor chains to the prefix via
// `: this(name)` then sets `this.Initializer = initializer;`. The `(std::string)` single-arg ctor
// is `explicit` (a single-argument ctor is a converting ctor by default -- the `SimpleType` D237 /
// `LabelStatement` D259 / `SingleVariableDesignation` D264 precedent); the two-arg all-params ctor
// is not `explicit` (a multi-arg ctor is not a converting ctor).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_VARIABLEINITIALIZER_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_VARIABLEINITIALIZER_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public partial class VariableInitializer : AstNode` (NOT `sealed` -- the
// `[DecompilerAstNode(hasPatternPlaceholder: true)]` emits a sealed `PatternPlaceholder` deriving
// from it; the pattern placeholder is deferred, but the class stays non-`final` to match the C#
// and to not block the placeholder landing -- the `ArrayInitializerExpression` D250 precedent).
// The first ported `TypeMembers` node, deriving directly from the `AstNode` root. The
// non-nullable-string-name-`[Slot]`-plus-a-nullable-`Expression?`-slot shape: a required
// `NameToken` (the backing `Identifier` of the non-nullable `string Name`) at flattened index 0
// plus a nullable `Initializer` `Expression?` at flattened index 1.
class VariableInitializer : public AstNode {
public:
    ~VariableInitializer() override = default;

    // The generated empty ctor (the C# `public VariableInitializer()`). `NameToken` defaults to
    // null (no name); the token is a REQUIRED slot, so a default-constructed node is only valid
    // until the name is set (or until `DoMatch`/`CheckInvariant` observe the missing token) --
    // the `IdentifierExpression` D246 / `LabelStatement` D259 / `SingleVariableDesignation` D264
    // required-slot behavior. `Initializer` defaults to null (no initializer -- the bare `name;`
    // form); the `Initializer` slot is nullable, so its absence is invariant-valid.
    VariableInitializer() = default;

    // The generated required-prefix ctor (the C# `public VariableInitializer(string name)`); the
    // `Name` string-name `[Slot]` is a "required" ctor param regardless of optionality (the
    // generator's line-168 rule), so `RequiredConstructorPrefixLength` is 1 and this single-arg
    // form is the required-prefix ctor. The generated body is `this.Name = name;` -- it calls the
    // string setter, which creates the token via `Identifier::Create` (an empty name yields a
    // token with an empty `Name`, NOT a null token -- the non-nullable behaviour, faithful to the
    // C# `string`). `explicit` because a single-argument ctor is a converting ctor by default
    // (the `SimpleType` D237 / `LabelStatement` D259 / `SingleVariableDesignation` D264 precedent).
    // Braced-init in tests avoids the most-vexing-parse (`VariableInitializer v(std::string())`
    // would declare `v` as a function -- the D236/D257 precedent).
    explicit VariableInitializer(std::string name) : VariableInitializer() {
        Name(std::move(name));
    }

    // The generated all-params ctor (the C# `public VariableInitializer(string name, Expression?
    // initializer)`); the `Initializer` is nullable so it is NOT in the required prefix, and this
    // two-arg form is the full all-params ctor (no `params` overload since there is no collection).
    // The generated body chains to the prefix via `: this(name)` then sets
    // `this.Initializer = initializer;`. The two-arg form is NOT `explicit` (a multi-arg ctor is
    // not a converting ctor). The delegation to the explicit `(std::string)` prefix ctor is fine
    // (delegating ctor calls may invoke `explicit` ctors; `explicit` only blocks implicit
    // conversion sequences, not ctor delegation).
    VariableInitializer(std::string name, Expression* initializer)
        : VariableInitializer(std::move(name)) {
        Initializer(initializer);
    }

    // ---- The `NameToken` slot (the backing `Identifier` token of the name) ---------------
    // The generated `[Slot("Identifier")] public partial Identifier NameToken` -- a single
    // REQUIRED (non-nullable) `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). NO name shadowing
    // (the `NameToken` accessor does NOT collide with the `Identifier` class -- no member is
    // named `Identifier`), so the element type is the plain `Identifier` and the
    // `Identifier::Create` factory is unqualified (the `LabelStatement` D259 /
    // `MemberType.MemberName` D238 differently-named-property precedent).
    Identifier* NameToken() const { return nameToken_; }
    void NameToken(Identifier* value) {
        SetChildNode(nameToken_, value, 0);
    }

    // ---- The `Name` string-name accessor (over the token) --------------------------------
    // The generated `public partial string Name` -- a convenience string over the `NameToken`
    // slot. A NON-optional name (the C# `string`, not `string?`): `get` returns
    // `NameToken.Name` (deref the token -- a null token is a half-constructed node that would
    // `NullReferenceException` in C#); `set` creates the token via `Identifier.Create` (NOT
    // `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
    // empty name yields a token with an empty `Name`, not a null token -- the
    // `MemberType.MemberName` D238 / `LabelStatement.Label` D259 precedent). `Name()` returns
    // `std::string` (a copy of the token's name); the `Identifier::Create` factory call is
    // unqualified (the `Name()` accessor does NOT shadow the `Identifier` class -- no member is
    // named `Identifier`).
    std::string Name() const { return nameToken_->Name(); }
    void Name(std::string_view value) {
        NameToken(Identifier::Create(std::string(value)));
    }

    // ---- The `Initializer` slot (the nullable Expression) --------------------------------
    // The generated `[Slot("Expression")] public partial Expression? Initializer` -- a single
    // NULLABLE `Expression` slot at flattened index 1. The const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it). NO name shadowing
    // (the `Initializer` accessor does NOT collide with the `Expression` class -- no member is
    // named `Expression` -- the `MemberReferenceExpression.Target` D247
    // differently-named-property precedent), so the element type is the plain `Expression`.
    Expression* Initializer() const { return initializer_; }
    void Initializer(Expression* value) {
        SetChildNode(initializer_, value, 1);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ----------------
    // The `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // required -- the name is non-nullable so the token is a required slot, `IsOptional=false`).
    // NO name shadowing (`Identifier` resolves to the class -- no member is named `Identifier`),
    // so the element type is the plain `Identifier`. `Slots::Identifier` is already ported (by
    // `SimpleType` D237), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};

    // The `InitializerSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
    // nullable -- the C# `Expression?` so `IsOptional=true`). NO name shadowing (no member is
    // named `Expression`), so the element type is the plain `Expression`. `Slots::Expression` is
    // already ported (by `UnaryOperatorExpression` D231), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<Expression> InitializerSlot{"Initializer", false, &Slots::Expression, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitVariableInitializer` (`VariableInitializer` does not end in "AstType",
    // so the generator's visit-method-name default yields `VisitVariableInitializer`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitVariableInitializer(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0 (`NameToken`) and 1 (`Initializer`); no collection,
    // so `GetChildCount` is the constant 2 (each slot counts even when its child is absent) and
    // `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the generator's
    // `WriteReturnDispatchSwitch` shape with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return nameToken_;
            case 1: return initializer_;
            default: throw std::out_of_range("VariableInitializer::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(nameToken_, static_cast<Identifier*>(value), 0); break;
            case 1: SetChildNode(initializer_, static_cast<Expression*>(value), 1); break;
            default: throw std::out_of_range("VariableInitializer::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &NameTokenSlot;
            case 1: return &InitializerSlot;
            default: throw std::out_of_range("VariableInitializer::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is VariableInitializer o && MatchString(this.Name, o.Name) &&
    // MatchOptional(this.Initializer, o.Initializer, match)`. The `Name` term is a `String`
    // `MatchString` (the `$any$` wildcard in the pattern's `Name` matches any candidate name); the
    // backing `NameToken` is a generated non-`partial` `[Slot]` (not seen by the source-property
    // scan at generation time), so it never appears in `MembersToMatch` (no double-match). The
    // `Initializer` term is a NULLABLE recursive child, so the generator emits `MatchOptional`
    // (both absent, or both present and the pattern's `DoMatch` decides). A type-only mismatch
    // (not a `VariableInitializer`) rejects early. `Name()` returns `std::string` (the token's
    // name); `Pattern::MatchString` takes `std::optional<std::string_view>`, so the view is built
    // per side. `Name` is non-nullable, so the `std::optional<std::string_view>` is always engaged
    // (a real name, never `nullopt` -- the `MemberType.MemberName` D238 precedent). The `Name()`
    // calls are INLINED in the `MatchString` arguments (not pre-computed in locals) so the C# `&&`
    // short-circuit is preserved: `o->Name()` derefs the candidate's token only after the type
    // check passed. The `std::string` temporaries live until the end of the full `return`
    // expression, keeping the `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<VariableInitializer*>(other);
        if (o == nullptr)
            return false;
        if (!PatternMatching::Pattern::MatchString(
                std::optional<std::string_view>(std::string_view(Name())),
                std::optional<std::string_view>(std::string_view(o->Name()))))
            return false;
        return MatchOptional(initializer_, o->initializer_, std::move(match));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `NameToken`
    // deep-cloned through the setter when present (which re-parents; the cloned token carries its
    // own `Name`), and the `Initializer` deep-cloned through the setter when present (which
    // re-parents and re-indexes via `SetChildNode`). No scalar to copy (the `Name` string is
    // derived from the token, so cloning the token carries it). The `VariableInitializer` itself
    // has no own location fields (`StartLocation`/`EndLocation` are the print-time base fields
    // set by the unported output visitor), so they are not copied (the `LabelStatement` D259 /
    // `ArrayInitializerExpression` D250 no-location-copy precedent). NO elaborated specifiers
    // (no member is named `Identifier` or `Expression`); the `Identifier::Clone` returns
    // `Identifier*` which `NameToken(Identifier*)` accepts directly, and `Expression::Clone`
    // returns `Expression*` which `Initializer(Expression*)` accepts directly.
    VariableInitializer* Clone() const override {
        auto* node = new VariableInitializer();
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        if (initializer_ != nullptr)
            node->Initializer(static_cast<Expression*>(initializer_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `nameToken_` is null until the name is set; the slot is REQUIRED, so
    // `CheckInvariant` asserts it is filled (unlike `GotoStatement`'s optional token).
    // `initializer_` is null until the initializer is set; the slot is NULLABLE, so its absence is
    // invariant-valid. NO name shadowing (no member is named `Identifier` or `Expression`), so
    // the field types are the plain `Identifier`/`Expression`.
    Identifier* nameToken_ = nullptr;
    Expression* initializer_ = nullptr;
};

// The `Variable` kind -- the collection slot kind for every
// `[Slot("Variable")] AstNodeCollection<VariableInitializer>` (`FixedStatement.Variables`,
// `VariableDeclarationStatement.Variables`). A `CSharpSlotInfoT<VariableInitializer>` (the element
// type is the concrete `VariableInitializer` node).
//
// Defined HERE (in VariableInitializer.hpp, after the `VariableInitializer` class) rather than in
// Slots.hpp because `CSharpSlotInfoT<VariableInitializer>` needs `VariableInitializer` complete
// (the `dynamic_cast<const VariableInitializer*>` is-a test in the ctor), and `VariableInitializer`
// is a concrete node with per-node slot statics (its `NameTokenSlot`/`InitializerSlot` reference
// `&Slots::Identifier`/`&Slots::Expression`, so this header includes Slots.hpp). Placing the kind
// in Slots.hpp would form a circular include: Slots.hpp would have to include
// VariableInitializer.hpp (for the complete `VariableInitializer`), but VariableInitializer.hpp
// includes Slots.hpp (for `Slots::Identifier`/`Slots::Expression`), and with Slots.hpp's guard set
// those definitions would not be visible where VariableInitializer's class body needs them. After
// the class both `CSharpSlotInfoT` (visible via the Slots.hpp include) and `VariableInitializer` are
// complete, so the kind defines cleanly. The `inline` variable still has external linkage and one
// address across translation units (the C++17 `inline` guarantee), preserving the
// pointer-identity comparison `node.Slot.Kind == &Slots::Variable` the slot system relies on. This
// is the `Slots::Attribute`/`Slots::AttributeSection`/`Slots::Initializer` cycle-breaking precedent
// (D241/D242/D251) applied to a collection kind. The shared constant is constructed
// non-collection/non-optional (`{"Variable", false, nullptr, false}`); the per-node
// `VariablesSlot` on the owning node carries the `IsCollection` flag (the collection `[Slot]` makes
// the per-node slot a collection). The kind name `Variable` collides with no class in the `Syntax`
// namespace (there is `VariableInitializer`/`VariableDesignation`, not `Variable`), so no
// elaborated-type-specifier is needed.
namespace Slots {
inline const CSharpSlotInfoT<VariableInitializer> Variable{"Variable", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_VARIABLEINITIALIZER_HPP
