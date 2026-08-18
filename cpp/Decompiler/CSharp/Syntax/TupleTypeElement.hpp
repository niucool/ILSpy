// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to
// whom the Software is furnished to do so, subject to the following conditions:
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

// Port of the `TupleTypeElement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TupleAstType.cs (the generated `TupleTypeElement.g.cs` --
// the hand-written partial declares only the two slot properties). The next in-order Phase-5
// piece per the D288 plan ("the remaining GeneralScope ... FunctionPointerAstType,
// InvocationAstType, TupleAstType, SyntaxTree"): the dependency of `TupleAstType.Elements`
// (`AstNodeCollection<TupleTypeElement>`), so it lands before `TupleAstType`.
// `tuple_type_element ::= type identifier?` (C# grammar 8.3.1): an element of a tuple type, a
// type followed by an optional element name.
//
// A sealed `AstNode` (deriving DIRECTLY from the `AstNode` root, NOT `EntityDeclaration`/
// `Expression`/`Statement`/`AstType` -- a tuple element is a structural node owned by a
// `TupleAstType`'s `Elements` collection, not a member declaration or a type itself, so it
// carries no `SymbolKind`/`Modifiers`/`MatchAttributesAndModifiers` -- the `VariableInitializer`
// D266 / `CatchClause` D269 / `ParameterDeclaration` D278 direct-`AstNode` precedent; the
// `[DecompilerAstNode]` default `hasPatternPlaceholder` is false, so `final`). The
// `Constraint` D283 / `CaseLabel` D268 direct-`AstNode` precedent for a structural child.
//
// The two slots in source declaration order: a REQUIRED (non-nullable) `AstType` `Type`
// `[Slot("Type")]` single slot at flattened index 0 (the `CastExpression` D243 / `Attribute`
// D240 required-`AstType`-slot shape -- `MatchRequired`, NOT `MatchOptional` which the generator
// emits only for a nullable recursive child), and a NULLABLE `string?` `Name` string-name
// `[Slot("Identifier")]` over a backing `NameToken` `Identifier` slot at flattened index 1 (the
// `SimpleType` D237 nullable-string-name-`[Slot]` shape -- `Identifier::CreateIfNotEmpty`,
// `std::optional<std::string>` return, optional token, `MatchString`). The element name is
// optional (`string?`), so an absent name is a null token and the element is just a bare type.
//
// The generator emits the const-index `SetChildNode(ref field, value, index)` setters for both
// (no collection precedes either slot, so each flattened index is the constant slot position
// 0/1); `GetChildCount` is the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a
// flat two-case index switch.
//
// NO C++ name-shadowing crux (the `VariableInitializer` D266 / `MemberType.MemberName` D238 /
// `AttributeSection.AttributeTarget` D241 differently-named-property precedent): the `[Slot("Type")]`
// argument names the slot KIND "Type" but the PROPERTY is "Type"; no class named "Type" lives in
// the `Syntax` namespace (there is `AstType`, not `Type"), so the `Type()` accessor shadows
// nothing and the plain `AstType` resolves to the base class in every type position (the
// `Attribute` D240 no-class-named-Type precedent). The `[Slot("Identifier")]` argument names the
// slot KIND "Identifier" but the PROPERTY is "Name", so the string accessor is `Name()` (NOT
// `Identifier()`) and the backing token accessor is `NameToken()` (NOT `IdentifierToken()`);
// neither shadows the `Identifier` CLASS in this class scope (no member is named `Identifier`),
// so NO elaborated-type-specifier (`class Identifier`) is needed anywhere, and the
// `Identifier::CreateIfNotEmpty` factory call in the `Name` setter is unqualified.
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is TupleTypeElement o && this.Type.DoMatch(o.Type, match) &&
// MatchString(this.Name, o.Name)`. The `Type` term is a NON-NULLABLE recursive child, so the
// generator emits the direct `this.Type.DoMatch(o.Type, match)` term (NOT `MatchOptional`),
// which the port routes through `AstNode::MatchRequired` (the `[class.access.derived]`
// workaround, the `UnaryOperatorExpression` D231 / `CastExpression` D243 precedent). The `Name`
// term is a `String` `MatchString` (the `$any$` wildcard `Pattern::AnyString` in the pattern's
// `Name` matches any candidate name); the backing `NameToken` is a generated non-`partial`
// `[Slot]` (not seen by the source-property scan at generation time), so it never appears in
// `MembersToMatch` (no double-match). `Name()` returns `std::optional<std::string>` (nullopt when
// the token is absent); `Pattern::MatchString` takes `std::optional<std::string_view>`, so the
// view is built per side (nullopt passes through as the C# null).
//
// The generated ctors (the generator's `WriteConstructors`): the `Type` is non-nullable so it IS
// in the required prefix; the `Name` string-name `[Slot]` is a "required" ctor param regardless of
// optionality (the generator's line-168 rule). `RequiredConstructorPrefixLength` is 2
// (through the last non-optional param `Name` at index 1), `ConstructorPrefixLengths` is {2}
// (reqLen == cp.Count == 2, no shorter prefix), and there is no collection so no `params`
// overload. The generated ctors are the empty ctor + the `(AstType type, string name)`
// all-params ctor; the two-arg ctor is NOT `explicit` (a multi-arg ctor is not a converting
// ctor); the single-arg `(AstType)` is NOT generated (the required prefix IS the full set --
// `Name` is required, so no shorter prefix ctor). `TupleTypeElement` declares NO hand-written
// ctors, so the port carries only the generated ctors.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitTupleTypeElement(this)`
// (`TupleTypeElement` does not end in "AstType", so the generator's visit-method-name default
// yields `VisitTupleTypeElement`). The generated slot statics are `TypeSlot` (a
// `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required) and `NameTokenSlot` (a
// `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, optional -- the name may be
// absent). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`). NO new `Slots` constant in `Slots.hpp`: `Slots.Type` is
// already ported (by `Attribute` D240) and `Slots.Identifier` is already ported (by `SimpleType`
// D237). The new `Slots.Element` kind (a `CSharpSlotInfoT<TupleTypeElement>` collection kind for
// `TupleAstType.Elements`) is cycle-broken into THIS header after the `TupleTypeElement` class
// (the `Slots::Attribute` D241 / `Slots::Variable` D267 cycle-breaking precedent -- this header
// includes `Slots.hpp` for its per-node `TypeSlot`/`NameTokenSlot`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_TUPLETYPEELEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_TUPLETYPEELEMENT_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class TupleTypeElement : AstNode`. `final` (the C# `sealed`): no
// further derivation. A structural element owned by a `TupleAstType`'s `Elements` collection,
// combining the `CastExpression` D243 required-`AstType`-slot shape with the `SimpleType` D237
// nullable-string-name-`[Slot]` shape.
class TupleTypeElement final : public AstNode {
public:
    ~TupleTypeElement() override = default;

    // The generated empty ctor (the C# `public TupleTypeElement()`). `Type` defaults to null
    // (no type); the slot is REQUIRED, so a default-constructed node is only valid until the type
    // is set (or until `DoMatch`/`CheckInvariant` observe the missing type) -- the `CastExpression`
    // D243 / `UnaryOperatorExpression` D231 required-slot behavior. `NameToken` defaults to null
    // (no name); the slot is NULLABLE (the `string?`), so its absence is invariant-valid.
    TupleTypeElement() = default;

    // The generated all-params ctor (the C# `public TupleTypeElement(AstType type, string
    // name)`); both slots are required ctor params (`Type` non-nullable, `Name` a string
    // `[Slot]` required regardless of optionality), so `RequiredConstructorPrefixLength` is 2
    // and this two-arg form IS the full ctor (the required prefix IS the full set). The generated
    // body sets each slot through its setter (`this.Type = type; this.Name = name;`). The
    // two-arg form is NOT `explicit` (a multi-arg ctor is not a converting ctor). The `Name`
    // setter uses `Identifier::CreateIfNotEmpty`, so an empty name clears the token (an optional
    // name).
    TupleTypeElement(AstType* type, std::string name) : TupleTypeElement() {
        Type(type);
        Name(std::move(name));
    }

    // ---- The `Type` slot (a single REQUIRED `AstType` child) ---------------------------
    // The generated `[Slot("Type")] public partial AstType Type` -- a single REQUIRED
    // (non-nullable) `AstType` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. NO name shadowing (no class named `Type` in the `Syntax` namespace;
    // the `Type()` accessor does not collide with the `AstType` base -- the `Attribute` D240
    // no-class-named-Type precedent), so the element type is the plain `AstType`.
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `NameToken` slot (the backing `Identifier` token of the name) -------------
    // The generated `[Slot("Identifier")] public partial Identifier NameToken` -- a single
    // NULLABLE `Identifier` slot at flattened index 1 (the backing token of the optional `Name`
    // string). The const-index `SetChildNode(ref field, value, 1)` setter (no collection precedes
    // it). NO name shadowing (the `NameToken()` accessor does NOT collide with the `Identifier`
    // class -- no member is named `Identifier`), so the element type is the plain `Identifier`.
    Identifier* NameToken() const { return nameToken_; }
    void NameToken(Identifier* value) {
        SetChildNode(nameToken_, value, 1);
    }

    // ---- The `Name` string-name accessor (over the token) -----------------------------
    // The generated `public partial string? Name` -- a convenience string over the `NameToken`
    // slot. An OPTIONAL name (the C# `string?`): `get` returns null when the token is absent;
    // `set` creates the token via `Identifier.CreateIfNotEmpty`, so an empty/null name clears the
    // token (an absent name is a null token, the bare `type` element form). `Name()` returns
    // `std::optional<std::string>` (nullopt when the token is absent -- the faithful `string?`).
    // NO name shadowing (the `Name()` accessor does NOT collide with the `Identifier` class),
    // so the `Identifier::CreateIfNotEmpty` factory call is unqualified.
    std::optional<std::string> Name() const {
        return nameToken_ != nullptr
            ? std::optional<std::string>(nameToken_->Name()) : std::nullopt;
    }
    void Name(std::string_view value) {
        NameToken(Identifier::CreateIfNotEmpty(value));
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ---------------
    // The `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the
    // type is non-nullable so `IsOptional=false`). NO name shadowing, so the element type is the
    // plain `AstType`. `Slots.Type` is already ported (by `Attribute` D240), so no new `Slots`
    // constant.
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};

    // The `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // optional -- the name is nullable so `IsOptional=true`). NO name shadowing, so the element
    // type is the plain `Identifier`. `Slots.Identifier` is already ported (by `SimpleType`
    // D237), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitTupleTypeElement` (`TupleTypeElement` does not end in "AstType", so
    // the generator's visit-method-name default yields `VisitTupleTypeElement`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitTupleTypeElement(this);
    }

    // ---- Slot storage (the generated overrides) ----------------------------------------
    // Two single slots at flattened indices 0 (`Type`) and 1 (`NameToken`); no collection, so
    // `GetChildCount` is the constant 2 (each slot counts even when its child is absent) and
    // `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the generator's
    // `WriteReturnDispatchSwitch` shape with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            case 1: return nameToken_;
            default: throw std::out_of_range("TupleTypeElement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            case 1: SetChildNode(nameToken_, static_cast<Identifier*>(value), 1); break;
            default: throw std::out_of_range("TupleTypeElement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            case 1: return &NameTokenSlot;
            default: throw std::out_of_range("TupleTypeElement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is TupleTypeElement o && this.Type.DoMatch(o.Type, match) &&
    // MatchString(this.Name, o.Name)`. The `Type` term is a NON-NULLABLE recursive child, so the
    // generator emits the direct dispatch (NOT `MatchOptional`), routed through
    // `AstNode::MatchRequired` (the `[class.access.derived]` workaround). `MatchRequired` guards
    // a missing operand defensively (a null pattern child does not match; the C# would
    // null-deref), and a null candidate child flows through the operand's `DoMatch(nullptr)`
    // which returns false. The `Name` term is a `String` `MatchString` (the `$any$` wildcard in
    // the pattern's `Name` matches any candidate name). A type-only mismatch (not a
    // `TupleTypeElement`) rejects early. `Name()` returns `std::optional<std::string>` (nullopt
    // when the token is absent); `Pattern::MatchString` takes `std::optional<std::string_view>`,
    // so the view is built per side (nullopt passes through as the C# null -- the `SimpleType`
    // D237 precedent). The `Name()` calls are INLINED in the `MatchString` arguments so the C#
    // `&&` short-circuit is preserved: `o->Name()` builds the view only after the type terms
    // passed; the `std::optional<std::string>` temporaries live until the end of the full
    // `return` expression, keeping the `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<TupleTypeElement*>(other);
        if (o == nullptr)
            return false;
        if (!MatchRequired(type_, o->type_, match))
            return false;
        auto thisName = Name();
        auto otherName = o->Name();
        return PatternMatching::Pattern::MatchString(
            thisName ? std::optional<std::string_view>(*thisName) : std::nullopt,
            otherName ? std::optional<std::string_view>(*otherName) : std::nullopt);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Type`
    // deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`,
    // the covariant override, which `Type(AstType*)` accepts directly), and the `NameToken`
    // deep-cloned through the setter when present (which re-parents; `Identifier::Clone()`
    // returns `Identifier*`). No scalar to copy (the `Name` string is derived from the token, so
    // cloning the token carries it). The `TupleTypeElement` itself has no own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor), so they are not copied (the `VariableInitializer` D266 / `CastExpression` D243
    // no-location-copy precedent). NO elaborated specifiers (no member is named `AstType` or
    // `Identifier`). The covariant return is `TupleTypeElement*` (through `AstNode*`, the
    // `AstNode::Clone` virtual -- `TupleTypeElement` derives directly from `AstNode`).
    TupleTypeElement* Clone() const override {
        auto* node = new TupleTypeElement();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_` is null until the type is set; the slot is REQUIRED, so
    // `CheckInvariant` asserts it is filled. `nameToken_` is null until the name is set; the
    // slot is NULLABLE (the `string?`), so its absence is invariant-valid. NO name shadowing (no
    // member is named `AstType` or `Identifier`), so the field types are the plain `AstType`/
    // `Identifier`.
    AstType* type_ = nullptr;
    Identifier* nameToken_ = nullptr;
};

// The `Element` kind -- the collection slot kind for every
// `[Slot("Element")] AstNodeCollection<TupleTypeElement>` (`TupleAstType.Elements`). A
// `CSharpSlotInfoT<TupleTypeElement>` (the element type is the concrete `TupleTypeElement`
// node).
//
// Defined HERE (in TupleTypeElement.hpp, after the `TupleTypeElement` class) rather than in
// Slots.hpp because `CSharpSlotInfoT<TupleTypeElement>` needs `TupleTypeElement` complete (the
// `dynamic_cast<const TupleTypeElement*>` is-a test in the ctor), and `TupleTypeElement` is a
// concrete node with per-node slot statics (its `TypeSlot`/`NameTokenSlot` reference
// `&Slots::Type`/`&Slots::Identifier`, so this header includes Slots.hpp). Placing the kind in
// Slots.hpp would form a circular include: Slots.hpp would have to include TupleTypeElement.hpp
// (for the complete `TupleTypeElement`), but TupleTypeElement.hpp includes Slots.hpp (for
// `Slots::Type`/`Slots::Identifier`), and with Slots.hpp's guard set those definitions would not
// be visible where `TupleTypeElement`'s class body needs them. After the class both
// `CSharpSlotInfoT` (visible via the Slots.hpp include) and `TupleTypeElement` are complete, so
// the kind defines cleanly. The `inline` variable still has external linkage and one address
// across translation units (the C++17 `inline` guarantee), preserving the pointer-identity
// comparison `node.Slot.Kind == &Slots::Element` the slot system relies on. This is the
// `Slots::Attribute`/`Slots::AttributeSection`/`Slots::Variable` cycle-breaking precedent
// (D241/D242/D267) applied to a collection kind. The shared constant is constructed
// non-collection/non-optional (`{"Element", false, nullptr, false}`); the per-node
// `ElementsSlot` on `TupleAstType` carries the `IsCollection` flag (the collection `[Slot]` makes
// the per-node slot a collection). The kind name `Element` collides with no class in the `Syntax`
// namespace, so no elaborated-type-specifier is needed.
namespace Slots {
inline const CSharpSlotInfoT<TupleTypeElement> Element{"Element", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_TUPLETYPEELEMENT_HPP
