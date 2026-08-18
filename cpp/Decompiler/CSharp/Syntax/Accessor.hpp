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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of the `Accessor` concrete node (and its `AccessorKind` enum) in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/Accessor.cs (the generated `Accessor.g.cs` +
// the hand-written partial, which declares the `AccessorKind` enum, the `SymbolKind` override, the
// `Kind` scalar, the `Name`/`NameToken` no-op overrides, and the two slot properties -- no ctors,
// no helpers). The next in-order Phase-5 piece per the D273 plan ("the remaining TypeMember
// hierarchy (MethodDeclaration ...; ConstructorDeclaration; OperatorDeclaration;
// PropertyDeclaration; IndexerDeclaration; EventDeclaration; EnumMemberDeclaration; Accessor; ...)")
// -- the simplest remaining concrete `EntityDeclaration` and the dependency that unblocks
// `PropertyDeclaration` (its `Getter`/`Setter` are `Accessor?` single slots), `IndexerDeclaration`
// (likewise), and `EventDeclaration`/`CustomEventDeclaration` (the `AddAccessor`/`RemoveAccessor`).
//
// `Accessor` is the `get`/`set`/`init`/`add`/`remove` accessor of a property/indexer/event -- a
// sealed `EntityDeclaration` (the `[DecompilerAstNode]` with the default `hasPatternPlaceholder:
// false`, so `final` -- no pattern placeholder). Two `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the accessor (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's only collection but
//     NOT its last slot (the `Body` single trails it), so `supportsIncremental` is FALSE
//     (`collectionCount == 1 && slotIndex == 0 == slots.Count - 1` is false -- `slots.Count` is 2):
//     every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the parent's indices for a lazy
//     `EnsureChildIndices` rebuild (the `DestructorDeclaration` D272 / `ObjectCreateExpression`
//     D251 non-incremental precedent).
//   * `[Slot("Body")] public partial BlockStatement? Body` -- the accessor's body (a single
//     NULLABLE `BlockStatement?` slot at slot 1; absent for `get;`/`set;` -- an accessor with no
//     body, just the `;`). The slot FOLLOWS the `Attributes` collection, so the property setter uses
//     the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a
//     collection). Reusing the cycle-broken `Slots::Body` kind.
//
// The `AccessorKind` enum (`Any`, `Getter`, `Setter`, `Init`, `Adder`, `Remover`) distinguishes the
// accessor's role; `Any` is the zero value and the pattern-match wildcard (the generator detects
// the `Any` member by name, so the `DoMatch` term is the plain `==` value equality, NOT a bitmask
// test -- the `BinaryOperatorExpression.Operator` D229 / `VariableDeclarationStatement.Modifiers`
// D270 `Any`-wildcard precedent). The `Kind` scalar is a plain settable `AccessorKind`-typed
// property (no `[Slot]`), so the generator adds it to `MembersToMatch` (as a `MatchAny` term) and to
// the ctor params (a settable enum-typed scalar is a required ctor param -- the `DirectionExpression
// .FieldDirection` D235 / `VariableDeclarationStatement.Modifiers` D270 precedent).
//
// `Accessor` carries no name: it is printed as its keyword (`get`/`set`/`init`/`add`/`remove`), never
// an identifier, so the inherited `Name`/`NameToken` contract members are overridden to no-ops
// (`Name` returns the empty string, `NameToken` returns null, the setters are empty bodies -- the
// shared decompiler code sets a name on every method-like entity, which is irrelevant here and must
// not throw, per the C# source comment). This is the `FieldDeclaration` D273 `Name`/`NameToken`
// shape MINUS the throw (the `Accessor` setters are empty no-ops, not `std::logic_error` throws --
// faithful to the C# `set { }`).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]` on `Accessor`, so the
// `Name` term IS added), `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every
// `EntityDeclaration`-derived node, then the per-property scan adds `Kind` (a settable enum with
// an `Any` member -> the `Any`-wildcard term) and `Body` (a non-override, non-`[ExcludeFromMatch]`
// `AstNode`-derived `BlockStatement?` property -> a `MatchOptional` recursive term). So
// `MembersToMatch` is `[Name, MatchAttributesAndModifiers, ReturnType, Kind, Body]`, and the
// generated `DoMatch` is `return other is Accessor o && MatchString(this.Name, o.Name) &&
// this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType, match)
// && (this.Kind == AccessorKind.Any || this.Kind == o.Kind) && MatchOptional(this.Body, o.Body,
// match)`. The `Name` term is a `MatchString` over the always-empty `Name` (both return `""`, so
// `MatchString("", "")` is vacuously true); the `ReturnType` term is `MatchOptional` (nullable
// recursive) -- an `Accessor` has no `ReturnType` slot, so the inherited base `ReturnType()` kind-walk
// returns null and `MatchOptional(null, null)` is always true (the term is structurally present but
// vacuous for an accessor); the `Kind` term is the `Any`-wildcard; the `Body` term is `MatchOptional`
// over the nullable `Body` slot.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Kind (enum, required), Attributes
// (collection, optional), Body (single, optional/nullable)]`, `RequiredConstructorPrefixLength` is
// 1 (through the last non-optional param `Kind`), `ConstructorPrefixLengths` is {1, 2, 3}. The
// (len=1) ctor `(AccessorKind)` just sets `this.Kind = kind` (a plain scalar assignment, no
// `AddRange`), so it PORTS. The (len=2) ctor `(AccessorKind, IEnumerable<AttributeSection>)`, its
// `params` overload, and the (len=3) ctor
// `(AccessorKind, IEnumerable<AttributeSection>, BlockStatement?)` all call
// `this.Attributes.AddRange(...)` for the `Attributes` collection (the `AddRange` convenience is the
// D222 deferral), so they are DEFERRED. The empty ctor + the `(AccessorKind)` required-prefix ctor
// cover the generated construction API; an `Accessor` is otherwise built via `Body(...)` +
// `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitAccessor(this)` (the class
// name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitAccessor`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection) and
// `BodySlot` (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`, nullable). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public `Modifiers()`
// getter/setter (the base's private `modifiers_` is not accessible from the derived `Clone` -- the
// C# `MemberwiseClone` copies the private backing; the port uses the public surface), the `Kind`
// scalar copied, the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
// concrete-clone pattern), every `Attributes` element deep-cloned through `Add`, and the `Body`
// deep-cloned through the setter.
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`BlockStatement`/`AccessorKind`/
// `SymbolKind` -- the `Attributes()`/`Body()`/`Kind()`/`Name()`/`NameToken()`/`SymbolKind()`
// accessors do not collide with any class in the `Syntax` namespace, and the `Kind()` accessor does
// NOT collide with the `AccessorKind` enum -- different names), so no elaborated-type-specifier is
// needed anywhere; the plain element types resolve to the classes. The `Kind` scalar backing field
// and the `DoMatch` `Any`-wildcard term use the plain `AccessorKind` (no shadowing, unlike the
// `VariableDeclarationStatement` D270 `Modifiers`-shadows-`Modifiers`-enum crux where the accessor
// shares the enum's name).
//
// NO new `Slots` constant: `Slots::AttributeSection` is cycle-broken in `AttributeSection.hpp`
// (D241), and `Slots::Body` is cycle-broken in `BlockStatement.hpp` (D260), so `Slots.hpp` is
// unchanged.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_ACCESSOR_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_ACCESSOR_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum AccessorKind` -- the role of an `Accessor` (`get`/`set`/`init`/`add`/
// `remove`). `Any` is the zero value and the pattern-match wildcard (the generator detects the
// `Any` member by name, so the `DoMatch` `Kind` term is the plain `==` value equality with the
// `Any`-wildcard short-circuit -- the `BinaryOperatorExpression.Operator` D229 / `FieldDirection`
// D235 `Any`-wildcard precedent; `AccessorKind` is NOT `[Flags]`, so no bitwise operators). The
// values are in C# declaration order.
enum class AccessorKind {
    Any = 0,
    Getter,
    Setter,
    Init,
    Adder,
    Remover
};

// The C# `public sealed partial class Accessor : EntityDeclaration`. `final` (the C# `sealed`): no
// further derivation. The simplest remaining concrete `EntityDeclaration`: an `Attributes`
// `AttributeSection` collection + a nullable `Body` `BlockStatement` single slot (the
// `DestructorDeclaration` D272 shape MINUS the `NameToken`), plus an `AccessorKind` scalar and the
// `Name`/`NameToken` no-op overrides.
class Accessor final : public EntityDeclaration {
public:
    ~Accessor() override = default;

    // The generated empty ctor (the C# `public Accessor()`). The `Attributes` collection is a
    // member (the D222 always-present-stack-member design), initialized here with `baseIndex = 0`
    // (the collection is the first slot) and `supportsIncremental = false` (the collection is NOT
    // the node's last slot -- `Body` trails it -- so an element's flattened `ChildIndex` is
    // dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation). `Body` defaults to null
    // (nullable -- its absence is invariant-valid). `Kind` defaults to `Any` (the enum's zero
    // value, the C# default -- a wildcard accessor; a real accessor sets it to `Getter`/`Setter`/
    // `Init`/`Adder`/`Remover`).
    Accessor() : attributes_(this, &AttributesSlot, 0, false) {}

    // The generated required-prefix ctor (the C# `public Accessor(AccessorKind kind)`) -- the one
    // required ctor param before the optional `Attributes` collection and `Body`. Sets `Kind`
    // directly (a plain scalar assignment, the C# `this.Kind = kind`). Delegates to the empty ctor
    // so the collection member is initialized. `explicit` (a single-argument ctor is a converting
    // ctor by default -- the `UnaryOperatorExpression` D231 / `TypeReferenceExpression` D245
    // single-arg-ctor precedent). NO name-shadowing crux on `Kind`/`AccessorKind` (the `Kind()`
    // accessor does NOT share the `AccessorKind` enum's name, unlike the
    // `VariableDeclarationStatement` D270 `Modifiers`-shadows-`Modifiers` crux), so the direct
    // field assignment is unambiguous.
    explicit Accessor(AccessorKind kind) : Accessor() {
        kind_ = kind;
    }

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Method; } }` -- the
    // kind of member this declaration is (an accessor is method-like). Overrides the base abstract
    // `SymbolKind` (the `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Method` avoids
    // a `using` (the enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }

    // ---- The `Kind` scalar (a settable enum, NOT a `[Slot]`) -------------------------------
    // The C# `public AccessorKind Kind { get; set; }` -- a plain settable `AccessorKind`-typed
    // property (no `[Slot]` attribute). The generator adds it to `MembersToMatch` as a `MatchAny`
    // term (the enum declares an `Any` member) and to the ctor params (a settable enum-typed scalar
    // is a required ctor param). NO name-shadowing (`Kind()` does NOT collide with the
    // `AccessorKind` enum -- different names, the `DirectionExpression` D235 differently-named-
    // accessor precedent), so the plain `AccessorKind` (the enum) resolves in both the getter and
    // the setter signatures.
    AccessorKind Kind() const { return kind_; }
    void Kind(AccessorKind value) { kind_ = value; }

    // ---- The `Name`/`NameToken` no-op overrides (the accessor carries no name) --------------
    // The C# `public override string Name { get { return string.Empty; } set { } }` -- an accessor is
    // printed as its keyword, never an identifier, so the inherited `Name` is hidden and returns the
    // empty string; the setter is an empty no-op (NOT a throw -- the shared decompiler code sets a
    // name on every method-like entity, which is irrelevant here and must not throw, per the C#
    // source comment). Overrides the base `EntityDeclaration::Name` virtual.
    std::string Name() const override { return std::string(); }
    void Name(std::string_view) override { /* no-op, faithful to the C# `set { }` */ }

    // The C# `public override Identifier NameToken { get { return null!; } set { } }` -- there is no
    // name token for an accessor, so the inherited `NameToken` is hidden and returns null; the
    // setter is an empty no-op. Overrides the base `EntityDeclaration::NameToken` virtual.
    Identifier* NameToken() const override { return nullptr; }
    void NameToken(Identifier*) override { /* no-op, faithful to the C# `set { }` */ }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the accessor (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental). The C# lazily allocates
    // the wrapper; the D222 port makes the collection an always-present stack member, so the
    // accessor returns the member directly. Overrides the base `EntityDeclaration::Attributes` (the
    // base body returns a detached empty via `GetChildren`; this override returns the real
    // `attributes_` member). A `const` convenience overload returns `const&` for a `const Accessor*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `Body` slot (a NULLABLE single `BlockStatement`, NOT a base virtual) -----------
    // The generated `[Slot("Body")] public partial BlockStatement? Body` -- a single NULLABLE
    // `BlockStatement?` slot at slot 1 (the accessor's body; absent for `get;`/`set;` -- an accessor
    // with no body, just the `;`). The slot FOLLOWS the `Attributes` collection, so the property
    // setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after
    // a collection). NOT an override (the `EntityDeclaration` base declares no `Body` virtual), so a
    // plain non-virtual accessor. `CheckInvariant` passes without a `Body` (the slot is nullable).
    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's only collection); `BodySlot` (a `CSharpSlotInfoT<BlockStatement>`
    // pointing at `Slots.Body`, nullable). NO name shadowing (no member is named
    // `AttributeSection`/`BlockStatement`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitAccessor`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitAccessor(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Two slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`) and a `Body` single slot at slot 1 (index `attrCount`). `GetChildCount` is
    // `attrCount + 1` (the collection's current length plus the one single slot -- the single slot
    // contributes 1 to the flattened count even when the `Body` is absent); `GetChild`/`SetChild`/
    // `GetChildSlotInfo` walk the slots subtracting each one's width from a running index (the
    // generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a
    // collection step, then a single step). `GetCollectionByKind` returns the `Attributes`
    // collection for the `AttributeSection` kind. This is the `DestructorDeclaration` D272
    // collection -> single -> single dispatch shape MINUS the middle `NameToken` single (the
    // `ObjectCreateExpression` D251 collection -> single dispatch shape with the collection FIRST).

    int GetChildCount() const override { return attributes_.Count() + 1; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        if (i == 0)
            return body_;
        throw std::out_of_range("Accessor::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n) {
                attributes_.SetAt(i, static_cast<AttributeSection*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(body_, static_cast<BlockStatement*>(value), index);
            return;
        }
        throw std::out_of_range("Accessor::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return &AttributesSlot;
            i -= n;
        }
        if (i == 0)
            return &BodySlot;
        throw std::out_of_range("Accessor::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is Accessor o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && (this.Kind == AccessorKind.Any || this.Kind == o.Kind) &&
    // MatchOptional(this.Body, o.Body, match)`. The terms are in `MembersToMatch` order (the
    // generator adds `Name`, `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every
    // `EntityDeclaration`-derived node -- `NameToken` is NOT `[ExcludeFromMatch]` on `Accessor`, so
    // the `Name` `String` term IS added; then the per-property scan adds `Kind` (the `Any`-wildcard)
    // and `Body` (a `MatchOptional` recursive term)). The `Name` term is a `MatchString` over the
    // always-empty `Name` (both return `""`, so `MatchString("", "")` is vacuously true); the
    // `ReturnType` term is `MatchOptional` (nullable recursive) -- an `Accessor` has no `ReturnType`
    // slot, so the inherited base `ReturnType()` kind-walk returns null and `MatchOptional(null,
    // null)` is always true (the term is structurally present but vacuous for an accessor); the
    // `Kind` term is the `Any`-wildcard (the enum declares an `Any` member; the generator detects it
    // by name, so the term is the plain `==` value equality, NOT a bitmask test -- `Any` matches any
    // candidate, a real kind matches only the exact same kind); the `Body` term is `MatchOptional`
    // over the nullable `Body` slot. A type-only mismatch (not an `Accessor`) rejects early. The
    // `Name()` calls are inlined in the `MatchString` arguments (the `MemberType` D238 precedent) so
    // the C# `&&` short-circuit is preserved; the `std::string` temporaries live until the end of
    // the full `return` expression, so the `std::string_view` views are valid for the `MatchString`
    // call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<Accessor*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && (kind_ == AccessorKind::Any || kind_ == o->kind_)
            && MatchOptional(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the derived
    // `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the public
    // surface), the `Kind` scalar copied, the annotation channel copied (`CloneAnnotationsFrom` +
    // `ReparentTrivia`, the D223 concrete-clone pattern), every `Attributes` element deep-cloned
    // through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()` returns
    // `AttributeSection*`, which `Add(AttributeSection*)` accepts directly), and the `Body`
    // deep-cloned through the setter (which re-parents; `BlockStatement::Clone()` returns
    // `BlockStatement*`, which `Body(BlockStatement*)` accepts directly). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `Accessor` does not derive `EndLocation`), so they are not copied (the
    // `DestructorDeclaration` D272 / `FieldDeclaration` D273 no-location-copy precedent). The
    // covariant return is `Accessor*` (through `AstNode*`, the `AstNode::Clone` virtual --
    // `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written partial;
    // the covariant `Accessor*` is a valid override of `AstNode::Clone`).
    Accessor* Clone() const override {
        auto* node = new Accessor();
        node->Modifiers(Modifiers());
        node->kind_ = kind_;
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_` is the always-present collection member (empty until the
    // first `Add`, non-incremental); `body_` is null until the body is set (a NULLABLE slot -- its
    // absence is invariant-valid); `kind_` is the scalar (defaults to `Any`, the enum's zero value
    // -- the C# default, a wildcard accessor). NO name shadowing (no member is named
    // `AttributeSection`/`BlockStatement`/`AccessorKind`), so the field types are the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    BlockStatement* body_ = nullptr;
    AccessorKind kind_ = AccessorKind::Any;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_ACCESSOR_HPP
