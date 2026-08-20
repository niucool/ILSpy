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

// Port of the `TypeParameterDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/TypeParameterDeclaration.cs (the generated
// `TypeParameterDeclaration.g.cs` + the hand-written partial). The next in-order Phase-5 piece per
// the D281 plan ("MethodDeclaration needing TypeParameterDeclaration, ExtensionDeclaration, ...").
// The `type_parameter ::= attribute_section* ( 'in' | 'out' )? identifier` (C# grammar 8.5 /
// 15.2.3): a generic type or method's type parameter (the element of a `TypeParameters`
// `AstNodeCollection<TypeParameterDeclaration>` on a `MethodDeclaration`/`TypeDeclaration`/
// `DelegateDeclaration` -- the kind lands when the first owning declaration ports).
//
// The hand-written partial declares the two const-string variance keyword tokens
// (`OutVarianceKeyword`/`InVarianceKeyword`), the `Variance` scalar property (a `VarianceModifier`
// enum), the two `[Slot]` properties (`Attributes`/`Name`), and the `(string name)` convenience ctor.
// The `[DecompilerAstNode]` (the bare attribute, so `hasPatternPlaceholder` defaults to false) on a
// `sealed` class means the node is `sealed` and gets NO pattern placeholder -- the
// `NullReferenceExpression` D226 sealed-leaf precedent applied to a node with slots. It derives
// DIRECTLY from the `AstNode` root (not `EntityDeclaration`/`Expression`/`Statement`/`AstType`): a
// type parameter is a structural node owned by a declaration's `TypeParameters` collection, not a
// member declaration (it carries no `SymbolKind`/`Modifiers`/`MatchAttributesAndModifiers` -- the
// `EntityDeclaration` machinery does NOT apply) -- the `VariableInitializer` D266 / `CatchClause`
// D269 / `ParameterDeclaration` D278 direct-`AstNode` precedent.
//
// The two slots in source declaration order: an `Attributes` `AstNodeCollection<AttributeSection>`
// collection at slot 0 (reusing the cycle-broken `Slots::AttributeSection`, NON-incremental -- the
// collection is the node's only collection but NOT its last slot, the `NameToken` single trails it,
// so `supportsIncremental` is false and `Add`/`Insert`/`Remove`/single-slot-set invalidate the
// parent's indices for a lazy `EnsureChildIndices` rebuild, the `DestructorDeclaration` D272 /
// `EnumMemberDeclaration` D275 / `ParameterDeclaration` D278 non-incremental precedent); and a
// `[Slot("Identifier")] public partial string Name` string-name `[Slot]` over a backing `NameToken`
// `Identifier` single REQUIRED slot at slot 1 (a NON-nullable name since the property type is
// `string` under `#nullable enable`, NOT `string?` -- the `LabelStatement.Label` D259 /
// `MemberType.MemberName` D238 / `IdentifierExpression.Identifier` D246 non-nullable-string-name-
// `[Slot]` precedent; index-less `SetChildNode` setter following the collection; `Identifier::Create`
// NOT `CreateIfNotEmpty`, so an empty name yields a token with an empty `Name`, not a null token;
// reusing `Slots::Identifier`). The flattened layout is `Attributes [0, attrCount)`, `NameToken` at
// `attrCount`; `GetChildCount` is `attrCount + 1`.
//
// The `Variance` scalar property is NOT a `[Slot]` (no child slot): a `VarianceModifier` enum (a
// settable enum, so it IS a ctor param -- the generator adds settable enum-typed scalars to
// `CtorParams` in declaration order, the `VariableDeclarationStatement.Modifiers` D270 /
// `ParameterDeclaration.ParameterModifier` D278 settable-enum-ctor-param precedent). It IS in
// `MembersToMatch` (the generator adds every non-`[Slot]` settable instance property), and
// `VarianceModifier` declares NO `Any` member, so the `DoMatch` term is the PLAIN
// `this.Variance == o.Variance` (the `DirectionExpression.FieldDirection` D235 / `ReferenceKind` D278
// no-`Any`-enum precedent applied to a `TypeSystem` enum). NO name shadowing (the `Variance()`
// accessor does NOT share the `VarianceModifier` enum's name -- unlike
// `DirectionExpression.FieldDirection`-of-type-`FieldDirection` -- so no elaborated enum specifier
// is needed; the plain `VarianceModifier` resolves in both the getter and setter signatures, and
// the qualified `VarianceModifier::Invariant` is unambiguous -- the `ParameterDeclaration.
// ParameterModifier` D278 no-shadowing precedent applied to a `Variance`-of-type-`VarianceModifier`
// accessor whose name differs from the enum name).
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is TypeParameterDeclaration o && this.Attributes.DoMatch(
// o.Attributes, match) && this.Variance == o.Variance && MatchString(this.Name, o.Name)`. The
// `Attributes` term is the collection recursive match (the generator emits a collection-typed
// recursive term directly, NOT `MatchOptional`); the `Variance` term is the fall-through
// plain-equality (a no-`Any` enum); the `Name` term is a `String` `MatchString` (the `$any$`
// wildcard in the pattern's `Name` matches any candidate name). The backing `NameToken` is a
// generated non-`partial` `[Slot]` (not seen by the source-property scan at generation time), so it
// never appears in `MembersToMatch` (no double-match). A type-only mismatch (not a
// `TypeParameterDeclaration`) rejects early.
//
// The generated ctors (the generator's `WriteConstructors`): `CtorParams` in declaration order is
// `[Attributes (collection, optional), Variance (enum, required), Name (string, required)]` -- the
// `Name` string-name `[Slot]` is a "required" ctor param regardless of optionality (the generator's
// line-168 rule), and `Variance` is a settable enum so it is a required ctor param (the
// `VariableDeclarationStatement.Modifiers` D270 / `ParameterDeclaration.ParameterModifier` D278
// precedent). `RequiredConstructorPrefixLength` is 3 (through the last non-optional param `Name` at
// index 2 -- the generator walks the whole `ctorParams` list and takes the last non-optional index
// + 1, the `CatchClause` D269 / `PropertyDeclaration` D276 precedent); `ConstructorPrefixLengths`
// is {3} (`reqLen` and `cp.Count` both 3, no collection param at-or-after `reqLen` -- the
// `Attributes` collection is at index 0, before `reqLen`); the `(len=3)` ctor takes an `Attributes`
// `IEnumerable<AttributeSection>` collection param, so its body calls `this.Attributes.AddRange(...)`
// (the `AddRange` convenience is the D222 deferral), so it is DEFERRED -- the only portable
// generated ctor is the empty ctor. The hand-written `(string name)` convenience ctor (which sets
// `Name = name`, calling the `Name` string setter, which creates the token via `Identifier::Create`
// -- portable, the `LabelStatement(string)` D259 / `VariableInitializer(string)` D266 hand-written-
// ctor precedent) ports alongside the empty ctor. A `TypeParameterDeclaration` is built via the
// `(string)` ctor or the empty ctor + `Attributes().Add(...)` + `Variance(...)` + `Name(...)`.
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`Identifier`/`VarianceModifier`
// -- the `Attributes()`/`NameToken()`/`Name()`/`Variance()` accessors do not collide with any
// class/enum in the `Syntax` or `TypeSystem` namespaces; `[Slot("Identifier")]` names the slot KIND
// but the PROPERTY is `Name`, so the accessors do not collide with the `Identifier` class -- the
// `MemberType.MemberName` D238 / `ParameterDeclaration` D278 differently-named-property precedent;
// `Variance()` does not collide because no enum/class named `Variance` exists -- there is
// `VarianceModifier`, not `Variance` -- the `ParameterDeclaration.ParameterModifier` D278
// accessor-name-differs-from-enum-name precedent). So no elaborated-type-specifier is needed
// anywhere, and the `Identifier::Create` factory call is unqualified. This is the cleanest
// collection-plus-a-non-nullable-string-name-`[Slot]`-plus-a-no-`Any`-enum-scalar port so far.
//
// NO new `Slots` constant (both kinds are already ported: `Slots::AttributeSection` cycle-broken in
// `AttributeSection.hpp` D241, `Slots::Identifier` in `Slots.hpp` D237). The `VarianceModifier` enum
// ports to `cpp/Decompiler/TypeSystem/VarianceModifier.hpp` (header-only, the `SymbolKind` D271 /
// `ReferenceKind` D278 precedent) -- the second ported `TypeSystem` enum consumed by a
// `CSharp.Syntax` scalar. Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is
// hand-translated from the generated output. The generated `AcceptVisitor` calls
// `visitor.VisitTypeParameterDeclaration(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitTypeParameterDeclaration`). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the `Variance` scalar copied directly (private to this class,
// so accessible from the derived `Clone` -- the `ComposedType` D242 scalar-copy precedent), the
// annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
// pattern), the `NameToken` deep-cloned through the setter when present (which re-parent; the
// covariant `Clone()` returns `Identifier*` the setter accepts directly), and every `Attributes`
// element deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()`
// returns `AttributeSection*`). No own location fields (does not derive `EndLocation`), so the
// print-time `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
// `VariableInitializer` D266 / `ParameterDeclaration` D278 no-location-copy precedent). The
// covariant return is `TypeParameterDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_TYPEPARAMETERDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_TYPEPARAMETERDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/TypeSystem/VarianceModifier.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class TypeParameterDeclaration : AstNode` (`sealed` -- the bare
// `[DecompilerAstNode]` has `hasPatternPlaceholder` default false, so NO `PatternPlaceholder`
// subclass is emitted; the port uses `final` to match the `sealed`). Derives directly from the
// `AstNode` root (a type parameter is a structural node owned by a declaration's `TypeParameters`
// collection, not a member declaration). The collection-plus-one-trailing-single shape with a
// non-nullable string-name `[Slot]` as the trailing single: an `Attributes` collection (slot 0) +
// a required `NameToken` `Identifier` (slot 1, the backing token of the non-nullable `string Name`),
// plus a `Variance` enum scalar.
class TypeParameterDeclaration final : public AstNode {
public:
    ~TypeParameterDeclaration() override = default;

    // The generated empty ctor (the C# `public TypeParameterDeclaration()`). The `Attributes`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = false` (the
    // collection is NOT the node's last slot -- `NameToken` trails it -- so an element's flattened
    // `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation). `NameToken`
    // defaults to null (a default-constructed node has no name; the slot is REQUIRED, so the empty
    // node is NOT invariant-valid -- the `LabelStatement` D259 required-token precedent). The
    // `Variance` scalar defaults to `VarianceModifier::Invariant` (the C# default, the zero value).
    TypeParameterDeclaration() : attributes_(this, &AttributesSlot, 0, false) {}

    // The hand-written `(string name)` convenience ctor (the C# `public TypeParameterDeclaration(
    // string name) { Name = name; }`) -- sets `Name` to `name`, calling the `Name` string setter,
    // which creates the token via `Identifier::Create` (an empty name yields a token with an empty
    // `Name`, NOT a null token -- the non-nullable behaviour, faithful to the C# `string`).
    // `explicit` because a single-argument ctor is a converting ctor by default (the
    // `LabelStatement(string)` D259 / `VariableInitializer(string)` D266 precedent). Braced-init in
    // tests avoids the most-vexing-parse (`TypeParameterDeclaration s(std::string())` would declare
    // `s` as a function -- the D236/D257/D259 precedent).
    explicit TypeParameterDeclaration(std::string name) : TypeParameterDeclaration() {
        Name(std::move(name));
    }

    // ---- The const keyword tokens (the output-visitor token literals) -------------------
    // The C# `public const string OutVarianceKeyword = "out"` / `InVarianceKeyword = "in"`. Part of
    // the node's public API (the output visitor reads them); port as `static constexpr const char*`
    // (the `CheckedExpression.CheckedKeyword` D234 / `ComposedType.RefKeyword` D242 /
    // `ParameterDeclaration.ThisModifier` D278 precedent). The generator excludes `const string`
    // fields from `MembersToMatch` (it iterates only instance `IPropertySymbol`s), so they never
    // appear in the generated `DoMatch`.
    static constexpr const char* OutVarianceKeyword = "out";
    static constexpr const char* InVarianceKeyword = "in";

    // ---- The `Variance` scalar property (NOT a `[Slot]`, not a child slot) ----------------
    // The C# `public VarianceModifier Variance { get; set; }` -- the `in`/`out`/none variance
    // modifier (a `VarianceModifier` enum scalar). A settable enum, so it IS a ctor param (the
    // `VariableDeclarationStatement.Modifiers` D270 / `ParameterDeclaration.ParameterModifier` D278
    // settable-enum-ctor-param precedent). `VarianceModifier` declares NO `Any` member, so the
    // generator's `hasAny` path does NOT fire -- the `DoMatch` term is the PLAIN
    // `this.Variance == o.Variance` (the `DirectionExpression.FieldDirection` D235 / `ReferenceKind`
    // D278 no-`Any`-enum precedent applied to a `TypeSystem` enum). NO name shadowing (the
    // `Variance()` accessor does NOT share the `VarianceModifier` enum's name -- unlike
    // `DirectionExpression.FieldDirection`-of-type-`FieldDirection` -- so no elaborated enum
    // specifier is needed; the plain `VarianceModifier` resolves in both the getter and setter
    // signatures, and the qualified `VarianceModifier::Invariant` is unambiguous -- the
    // `ParameterDeclaration.ParameterModifier` D278 accessor-name-differs-from-enum-name precedent).
    ILSpy::Decompiler::TypeSystem::VarianceModifier Variance() const { return variance_; }
    void Variance(ILSpy::Decompiler::TypeSystem::VarianceModifier value) { variance_ = value; }

    // ---- The `Attributes` collection slot -----------------------------------------------
    // The generated `[Slot("AttributeSection")] public partial AstNodeCollection<AttributeSection>
    // Attributes` -- the attribute sections on the type parameter (a `CSharpSlotInfoT<AttributeSection>`
    // slot at slot 0, non-incremental). The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly. A
    // `const` convenience overload returns `const&` for a `const TypeParameterDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `NameToken` slot (the backing `Identifier` token of the name) --------------
    // The generated `[Slot("Identifier")] public partial Identifier NameToken` (the generator emits
    // the backing token as a public non-partial `[Slot]` named `{property.Name}Token`) -- a single
    // REQUIRED (non-nullable) `Identifier` slot at slot 1. The slot FOLLOWS the `Attributes`
    // collection, so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the
    // dynamic flattened index after a collection). NO name shadowing (the `NameToken` accessor does
    // NOT collide with the `Identifier` class -- no member is named `Identifier` -- the
    // `VariableInitializer.NameToken` D266 / `ParameterDeclaration.NameToken` D278 precedent), so the
    // element type is the plain `Identifier` and the `Identifier::Create` factory is unqualified.
    Identifier* NameToken() const { return nameToken_; }
    void NameToken(Identifier* value) {
        SetChildNode(nameToken_, value);
    }

    // ---- The `Name` string-name accessor (over the token) -------------------------------
    // The generated `public partial string Name` -- a convenience string over the `NameToken` slot.
    // A NON-optional name (the C# `string`, not `string?`, under `#nullable enable`): `get` returns
    // `NameToken.Name` (deref the token -- a null token is a half-constructed node that would
    // `NullReferenceException` in C#); `set` creates the token via `Identifier.Create` (NOT
    // `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
    // empty name yields a token with an empty `Name`, not a null token -- the `MemberType.MemberName`
    // D238 / `IdentifierExpression.Identifier` D246 / `LabelStatement.Label` D259 precedent).
    // `Name()` returns `std::string` (a copy of the token's name); the `Identifier::Create` factory
    // call is unqualified (the `Name()` accessor does NOT shadow the `Identifier` class -- no member
    // is named `Identifier` -- the `MemberType.MemberName` D238 / `ParameterDeclaration.Name` D278
    // differently-named-property precedent).
    std::string Name() const { return nameToken_->Name(); }
    void Name(std::string_view value) {
        NameToken(Identifier::Create(std::string(value)));
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ----------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's only collection); `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
    // pointing at `Slots.Identifier`, required -- the non-nullable name makes the token a required
    // slot). NO name shadowing (no member is named `AttributeSection`/`Identifier`), so the element
    // types are the plain classes. NO new `Slots` constant (both kinds are already ported).
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitTypeParameterDeclaration` (`TypeParameterDeclaration` does not end in
    // "AstType", so the generator's visit-method-name default yields `VisitTypeParameterDeclaration`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitTypeParameterDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitTypeParameterDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitTypeParameterDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Two slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), and a `NameToken` single slot at slot 1 (index `attrCount`).
    // `GetChildCount` is `attrCount + 1` (the collection's current length plus the one single); the
    // single slot counts even when the token is absent (a half-constructed node reports
    // `GetChildCount` 1). `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each
    // one's width from a running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a collection step, then one single step).
    // `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind.
    // This is the `EnumMemberDeclaration` D275 / `ParameterDeclaration` D278 collection -> single
    // dispatch shape (the collection FIRST, then one single).
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
            return nameToken_;
        throw std::out_of_range("TypeParameterDeclaration::GetChild");
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
            SetChildNode(nameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        throw std::out_of_range("TypeParameterDeclaration::SetChild");
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
            return &NameTokenSlot;
        throw std::out_of_range("TypeParameterDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is TypeParameterDeclaration o && this.Attributes.DoMatch(o.Attributes, match) &&
    // this.Variance == o.Variance && MatchString(this.Name, o.Name)`. The terms are in
    // `MembersToMatch` order (source declaration order -- the generator adds every non-`[Slot]`
    // settable instance property plus the `[Slot]` children). The `Attributes` term is the
    // collection recursive match; the `Variance` term is the fall-through plain-equality (a no-`Any`
    // enum); the `Name` term is a `String` `MatchString` (the `$any$` wildcard in the pattern's
    // `Name` matches any candidate name). The backing `NameToken` is a generated non-`partial`
    // `[Slot]` (not seen by the source-property scan), so it never appears in `MembersToMatch` (no
    // double-match). A type-only mismatch (not a `TypeParameterDeclaration`) rejects early.
    // `Name()` returns `std::string` (the token's name); `Pattern::MatchString` takes
    // `std::optional<std::string_view>`, so the view is built per side. `Name` is non-nullable, so
    // the `std::optional<std::string_view>` is always engaged (a real name, never `nullopt` -- the
    // `MemberType.MemberName` D238 / `LabelStatement.Label` D259 precedent). The `Name()` calls are
    // INLINED in the `MatchString` arguments (not pre-computed in locals) so the C# `&&`
    // short-circuit is preserved: `o->Name()` derefs the candidate's token only after the type
    // check and the `Attributes`/`Variance` terms already passed. The `std::string` temporaries live
    // until the end of the full `return` expression, keeping the `std::string_view` views valid for
    // the `MatchString` call. The private scalar fields are compared directly (same-class access).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<TypeParameterDeclaration*>(other);
        if (o == nullptr)
            return false;
        return attributes_.DoMatch(o->attributes_, match)
            && variance_ == o->variance_
            && PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides it
    // (no `MemberwiseClone`): a fresh node, the `Variance` scalar copied directly (private to this
    // class, so accessible from the derived `Clone` -- the `ComposedType` D242 scalar-copy
    // precedent), the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the
    // D223 concrete-clone pattern), the `NameToken` deep-cloned through the setter when present
    // (which re-parent; the cloned token carries its own `Name`; `Identifier::Clone()` returns
    // `Identifier*` which the `NameToken(Identifier*)` setter accepts directly), and every
    // `Attributes` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `AttributeSection::Clone()` returns `AttributeSection*`). No own location fields (does not
    // derive `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied (the
    // `DestructorDeclaration` D272 / `VariableInitializer` D266 / `ParameterDeclaration` D278
    // no-location-copy precedent). The covariant return is `TypeParameterDeclaration*` (through
    // `AstNode*`, the `AstNode::Clone` virtual).
    TypeParameterDeclaration* Clone() const override {
        auto* node = new TypeParameterDeclaration();
        node->variance_ = variance_;
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_` is the always-present collection member (empty until the
    // first `Add`, non-incremental); `nameToken_` is null until the name is set (the slot is
    // REQUIRED, so `CheckInvariant` asserts it is filled -- the `LabelStatement` D259 required-token
    // precedent); the `Variance` scalar defaults to `VarianceModifier::Invariant` (the C# default).
    // NO name shadowing (no member is named `AttributeSection`/`Identifier`/`VarianceModifier`), so
    // the field types are the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    Identifier* nameToken_ = nullptr;
    ILSpy::Decompiler::TypeSystem::VarianceModifier variance_ =
        ILSpy::Decompiler::TypeSystem::VarianceModifier::Invariant;
};

// The `TypeParameter` kind -- a collection of `TypeParameterDeclaration` (the generic type
// parameters of a generic method or type: `MethodDeclaration.TypeParameters`/`TypeDeclaration.
// TypeParameters`/`DelegateDeclaration.TypeParameters`, an `AstNodeCollection<
// TypeParameterDeclaration>`). Unique to `MethodDeclaration` among the ported nodes (the first
// owning declaration); `TypeDeclaration`/`DelegateDeclaration` will reuse it when they port.
//
// Defined HERE (in `TypeParameterDeclaration.hpp`, after the `TypeParameterDeclaration` class)
// rather than in `Slots.hpp` because `CSharpSlotInfoT<TypeParameterDeclaration>` needs
// `TypeParameterDeclaration` complete (the `dynamic_cast<const TypeParameterDeclaration*>` is-a
// test in the ctor), and `TypeParameterDeclaration` is a concrete node with per-node slot statics
// (its `AttributesSlot`/`NameTokenSlot` reference `&Slots::AttributeSection`/`&Slots::Identifier`,
// so this header includes `Slots.hpp`). Placing the kind in `Slots.hpp` would form a circular
// include: `Slots.hpp` would have to include `TypeParameterDeclaration.hpp` (for the complete
// `TypeParameterDeclaration`), but `TypeParameterDeclaration.hpp` includes `Slots.hpp` (for
// `Slots::AttributeSection`/`Slots::Identifier`), and with `Slots.hpp`'s guard set those definitions
// would not be visible where `TypeParameterDeclaration`'s class body needs them. After the class
// both `CSharpSlotInfoT` (visible via the `Slots.hpp` include) and `TypeParameterDeclaration` are
// complete, so the kind defines cleanly. The `inline` variable still has external linkage and one
// address across translation units (the C++17 `inline` guarantee), preserving the
// pointer-identity comparison `node.Slot.Kind == &Slots::TypeParameter` the slot system relies on.
// This is the `Slots::Attribute` D241 / `Slots::AttributeSection` D242 / `Slots::Parameter` D279 /
// `Slots::Variable` D267 / `Slots::ConstructorInitializer` D281 cycle-breaking precedent applied to
// a `TypeParameterDeclaration`-typed collection kind. The shared constant is constructed
// non-collection/non-optional (`{"TypeParameter", false, nullptr, false}`); the per-node
// `TypeParametersSlot` on the owning node carries the `IsCollection` flag (the collection `[Slot]`
// makes the per-node slot a collection). The kind name `TypeParameter` collides with no class in
// the `Syntax` namespace (there is `TypeParameterDeclaration`, not `TypeParameter`), so no
// elaborated-type-specifier is needed -- the same no-collision discriminator as `Slots::Variable`
// (there is `VariableInitializer`/`VariableDesignation`, not `Variable`).
namespace Slots {
inline const CSharpSlotInfoT<TypeParameterDeclaration> TypeParameter{"TypeParameter", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_TYPEPARAMETERDECLARATION_HPP
