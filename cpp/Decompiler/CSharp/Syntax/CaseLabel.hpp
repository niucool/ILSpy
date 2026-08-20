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

// Port of the `CaseLabel` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/SwitchStatement.cs (the generated
// `CaseLabel.g.cs` + the hand-written partial, which declares only the two const strings and
// the one slot property, no ctors, no helpers). Part of the switch family -- the next in-order
// Phase-5 piece per the D267 plan ("SwitchStatement, TryCatchStatement,
// LocalFunctionDeclarationStatement, VariableDeclarationStatement ... then the remaining
// TypeMember/GeneralScope hierarchies"). `switch_label ::= 'case' expression ':' | 'default' ':'`
// (C# grammar 13.8.3): a `case` label carries the case-expression; the `default` label carries a
// null expression (the C# doc comment notes "The expression can be null - if the expression is
// null, it's the default switch section"), so the slot is NULLABLE.
//
// The hand-written partial declares only the `CaseKeyword`/`DefaultKeyword` const strings and
// the `Expression` slot property. It is a sealed `AstNode` deriving DIRECTLY from the `AstNode`
// root (not `Expression`/`Statement`/`AstType`) -- a leaf of the switch section's `CaseLabels`
// collection. The single slot in source declaration order: a NULLABLE `Expression?` `Expression`
// `[Slot("Expression")]` single slot at flattened index 0 (the `ReturnStatement` D255
// nullable-`Expression?`-single-slot shape applied to a direct-`AstNode`-derived node). The
// generator emits the const-index `SetChildNode(ref field, value, 0)` setter (the single slot is
// the first and only slot, no collection precedes it); `GetChildCount` is the constant 1 and
// `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat single-case index switch.
//
// C++ name-shadowing crux (the `ReturnStatement` D255 / `UnaryOperatorExpression` D231
// precedent): the C# property is `Expression` of type `Expression?` (a property named the same as
// its type -- legal in C#, which keeps property and type names in separate spaces). The faithful
// port names the accessor `Expression()`, but a member function named `Expression` SHADOWS the
// `Expression` class in this class scope (C++ unqualified name lookup finds the member and stops,
// even though it is not a type -- the D224 `Annotation<T>()` crux). Every type usage AFTER the
// `Expression()` getter is declared therefore uses the elaborated-type-specifier
// `class Expression` (basic.lookup.elab: an elaborated specifier ignores non-type names and finds
// the hidden class), so the setter parameter, the slot static, the `static_cast`s, and the backing
// field spell the operand type as `class Expression`. The ctor parameter and the getter return
// type precede the getter's declaration, so they use the plain `Expression` (no member function
// is in scope there yet).
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is CaseLabel o && MatchOptional(this.Expression, o.Expression,
// match)`. The `Expression` term is a NULLABLE recursive child, so the generator emits
// `MatchOptional` (both absent, or both present and the pattern's `DoMatch` decides); there is no
// scalar term (the `CaseKeyword`/`DefaultKeyword` const strings are static fields, excluded from
// `MembersToMatch`). A type-only mismatch (not a `CaseLabel`) rejects early. A `default:` label
// (a null `Expression`) matches another `default:` label (both absent), and an `OptionalNode` or
// any pattern matches an absent candidate.
//
// The generated ctors (the generator's `WriteConstructors`): the single `Expression?` slot is
// nullable, so `RequiredConstructorPrefixLength` is 0 and the single-arg form IS the full
// all-params ctor (no shorter prefix ctor and no `params` overload since there is no
// collection). The generated ctors are the empty ctor + the `(Expression?)` all-params ctor.
// The `(Expression*)` single-arg ctor is `explicit` (a single-argument ctor is a converting
// ctor by default -- the `ReturnStatement` D255 / `TypeReferenceExpression` D248 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_CASELABEL_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_CASELABEL_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class CaseLabel : AstNode`. `final` (the C# `sealed`): no
// further derivation. A leaf of a `SwitchSection`'s `CaseLabels` collection -- a `case expr:` or
// `default:` label. The `ReturnStatement` D255 nullable-`Expression?`-single-slot shape applied to
// a direct-`AstNode`-derived node (the `VariableInitializer` D266 direct-`AstNode`-root base, but
// `sealed` here since `CaseLabel` carries no `hasPatternPlaceholder`).
class CaseLabel final : public AstNode {
public:
    ~CaseLabel() override = default;

    // The generated empty ctor (the C# `public CaseLabel()`). `Expression` defaults to null (the
    // `default:` label form). A null operand is valid here (the slot is nullable), so a
    // default-constructed node is invariant-valid (the `ReturnStatement` D255 nullable-slot
    // behavior).
    CaseLabel() = default;

    // The generated all-params ctor (the C# `public CaseLabel(Expression? expression)`); the
    // `Expression` slot is nullable so the required-prefix length is 0 and this single-arg form
    // IS the full all-params ctor (no shorter prefix ctor and no `params` overload since there is
    // no collection). `explicit` because a single-argument ctor is a converting ctor by default
    // (the `ReturnStatement` D255 precedent). The parameter type precedes the `Expression()`
    // accessor declaration, so the plain `Expression` (the base type) is unshadowed here; the
    // body is in complete-class context, so the `Expression(expression)` call resolves to the
    // setter declared below.
    explicit CaseLabel(Expression* expression)
        : CaseLabel() {
        Expression(expression);
    }

    // ---- The const keyword tokens (the output-visitor token literals) ----------------
    // The C# `public const string CaseKeyword = "case"` / `DefaultKeyword = "default"`. Part of
    // the node's public API (the output visitor reads them); port as `static constexpr const
    // char*` (the `CheckedExpression.CheckedKeyword` D234 precedent). The generator excludes
    // const string fields from `MembersToMatch` (it iterates only instance `IPropertySymbol`s),
    // so they never appear in the generated `DoMatch`.
    static constexpr const char* CaseKeyword = "case";
    static constexpr const char* DefaultKeyword = "default";

    // ---- The `Expression` slot (the nullable case expression) -------------------------
    // The C# `[Slot("Expression")] public partial Expression? Expression` -- a single, NULLABLE
    // `Expression` child at flattened index 0 (the case expression, null for `default:`). The
    // generator emits the const-index `SetChildNode(ref field, value, 0)` setter (the single slot
    // is the first and only slot, no collection precedes it), so the index is assigned directly
    // and the parent's indices stay valid by construction.
    Expression* Expression() const { return expression_; }
    // The setter parameter type uses the elaborated specifier `class Expression`: the
    // `Expression()` getter declared just above shadows the `Expression` base type in this
    // class scope, so the plain name would resolve to the member function (not a type).
    void Expression(class Expression* value) {
        SetChildNode(expression_, value, 0);
    }

    // ---- The per-node slot static (pointing at the shared `Slots` kind) ----------------
    // The `ExpressionSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`,
    // nullable -- the C# `Expression?` so `IsOptional=true`; the kind carries identity only,
    // constructed non-optional). `Slots::Expression` is already ported (by
    // `UnaryOperatorExpression` D231), so no new `Slots` constant. The element type uses the
    // elaborated `class Expression` (the `Expression()` accessor shadows the base type in this
    // scope).
    static inline const CSharpSlotInfoT<class Expression> ExpressionSlot{"Expression", false, &Slots::Expression, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitCaseLabel` (`CaseLabel` does not end in "AstType", so the generator's
    // visit-method-name default yields `VisitCaseLabel`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitCaseLabel(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitCaseLabel`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitCaseLabel(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Expression`); no collection, so `GetChildCount` is
    // the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with a single case).

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return expression_;
            default: throw std::out_of_range("CaseLabel::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(expression_, static_cast<class Expression*>(value), 0); break;
            default: throw std::out_of_range("CaseLabel::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ExpressionSlot;
            default: throw std::out_of_range("CaseLabel::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is CaseLabel o && MatchOptional(this.Expression, o.Expression, match)`.
    // `Expression` is a NULLABLE recursive child, so the generator emits `MatchOptional` (both
    // absent, or both present and the pattern's `DoMatch` decides); there is no scalar term (the
    // `CaseKeyword`/`DefaultKeyword` const strings are static fields, excluded from
    // `MembersToMatch`). A type-only mismatch (not a `CaseLabel`) rejects early. Two `default:`
    // labels (both null `Expression`) match; a `case expr:` vs a `default:` (one present, one
    // absent) reject.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<CaseLabel*>(other);
        if (o == nullptr)
            return false;
        return MatchOptional(expression_, o->expression_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `Expression` child deep-cloned through the setter (when present, which re-parents and
    // re-indexes via `SetChildNode`; a `default:` label with a null `Expression` clones to a
    // null-`Expression` clone). No own location fields (`CaseLabel` does not derive
    // `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
    // `ReturnStatement` D255 no-location-copy precedent). The `static_cast` uses the elaborated
    // `class Expression` (the `Expression()` accessor shadows the base type in this scope).
    CaseLabel* Clone() const override {
        auto* node = new CaseLabel();
        node->CloneAnnotationsFrom(*this);
        if (expression_ != nullptr)
            node->Expression(static_cast<class Expression*>(expression_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field uses the elaborated `class Expression` (the `Expression()` accessor
    // declared above shadows the `Expression` base type in this class scope). A nullable slot is
    // null until the value is set (the `default:` label form).
    class Expression* expression_ = nullptr;
};

// The `CaseLabel` kind -- the collection slot kind for every
// `[Slot("CaseLabel")] AstNodeCollection<CaseLabel>` (`SwitchSection.CaseLabels`). A
// `CSharpSlotInfoT<CaseLabel>` (the element type is the concrete `CaseLabel` node).
//
// Defined HERE (in CaseLabel.hpp, after the `CaseLabel` class) rather than in Slots.hpp because
// `CSharpSlotInfoT<CaseLabel>` needs `CaseLabel` complete (the `dynamic_cast<const CaseLabel*>`
// is-a test in the ctor), and `CaseLabel` is a concrete node with per-node slot statics (its
// `ExpressionSlot` references `&Slots::Expression`, so this header includes Slots.hpp). Placing the
// kind in Slots.hpp would form a circular include: Slots.hpp would have to include CaseLabel.hpp
// (for the complete `CaseLabel`), but CaseLabel.hpp includes Slots.hpp (for `Slots::Expression`),
// and with Slots.hpp's guard set those definitions would not be visible where CaseLabel's class
// body needs them. After the class both `CSharpSlotInfoT` (visible via the Slots.hpp include) and
// `CaseLabel` are complete, so the kind defines cleanly. The `inline` variable still has external
// linkage and one address across translation units (the C++17 `inline` guarantee), preserving the
// pointer-identity comparison `node.Slot.Kind == &Slots::CaseLabel` the slot system relies on.
// This is the `Slots::Attribute`/`Slots::AttributeSection`/`Slots::Initializer`/`Slots::Variable`
// cycle-breaking precedent (D241/D242/D251/D267) applied to a collection kind. The shared constant
// is constructed non-collection/non-optional (`{"CaseLabel", false, nullptr, false}`); the
// per-node `CaseLabelsSlot` on the owning `SwitchSection` carries the `IsCollection` flag (the
// collection `[Slot]` makes the per-node slot a collection). The kind name `CaseLabel` collides
// with the `CaseLabel` CLASS in the parent `Syntax` namespace (the `Expression`/`Identifier`/
// `Statement` collision pattern): the template argument in this definition resolves to the class
// (the constant being declared is not yet in scope at the point its type is parsed), and a LATER
// `Slots` entry wanting the `CaseLabel` class as its element type must qualify it
// (`::ILSpy::Decompiler::CSharp::Syntax::CaseLabel`) to avoid resolving to this constant.
namespace Slots {
inline const CSharpSlotInfoT<CaseLabel> CaseLabel{"CaseLabel", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_CASELABEL_HPP
