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

// Port of the `IndexerDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/IndexerDeclaration.cs (the generated
// `IndexerDeclaration.g.cs` + the hand-written partial, which declares the `ThisKeyword` const
// string, the `SymbolKind` override, the seven slot properties, and the `[EditorBrowsable(Never)]`
// `Name`/`NameToken` overrides -- no ctors, no helpers). The next in-order Phase-5 piece per the
// D278 plan ("IndexerDeclaration (Type + PrivateImplementationType + Parameters + Getter/Setter +
// ExpressionBody -- needs ParameterDeclaration, now ported, plus a new Slots::Parameter collection
// kind)").
//
// `indexer_declaration ::= attribute_section* modifier* type ( type '.' )? 'this' '[' parameter*
// ']' '{' accessor* '}' | attribute_section* modifier* type ( type '.' )? 'this' '[' parameter*
// ']' '=>' expression ';'` (C# grammar 15.9.1): a sealed `EntityDeclaration` (the
// `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final` -- no pattern
// placeholder). Seven `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the indexer (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's FIRST of TWO
//     collections, so `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex ==
//     slots.Count - 1` is false -- `collectionCount` is 2): every `Add`/`Insert`/`Remove`/
//     single-slot-set INVALIDATES the parent's indices for a lazy `EnsureChildIndices` rebuild
//     (the `ComposedType` D242 two-collection / `ArrayCreateExpression` D252 precedent).
//   * `[Slot("Type")] public override partial AstType ReturnType` -- the indexer's declared type
//     (a single REQUIRED `AstType` slot at slot 1, non-nullable; reusing `Slots::Type`). The slot
//     FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode(ref field, value)` (the dynamic flattened index after a collection; the
//     `ComposedType.BaseType` D242 / `PropertyDeclaration` D276 precedent). Overrides the base
//     `EntityDeclaration::ReturnType` (the base body kind-walks for the `Type` kind; this
//     override returns the backing field directly, the generated `get => field!`).
//   * `[Slot("PrivateImplementationType")] public partial AstType? PrivateImplementationType` --
//     the explicit-interface-implementation type (a single NULLABLE `AstType?` slot at slot 2, e.g.
//     the `I` in `int I.this[...] { get; set; }`; null when the indexer is not an explicit
//     interface implementation). The slot FOLLOWS the `Attributes` collection, so the setter uses
//     the INDEX-LESS `SetChildNode`. Reusing the `Slots::PrivateImplementationType` kind (added by
//     `PropertyDeclaration` D276). NOT an override, so a plain non-virtual accessor.
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> Parameters` --
//     the indexer's parameter list (a COLLECTION at slot 3, the `int this[int x]` `x`; a NEW
//     `Slots::Parameter` kind, cycle-broken into `ParameterDeclaration.hpp` this iteration --
//     `ParameterDeclaration.hpp` includes `Slots.hpp` for its per-node slot statics, the
//     `Slots::Variable` D267 / `Slots::AttributeSection` D241 cycle-breaking precedent applied to a
//     `ParameterDeclaration`-typed collection kind). The collection is the node's SECOND of TWO
//     collections, so `supportsIncremental` is FALSE (the `ComposedType` D242 two-collection
//     precedent); `baseIndex` is 3 (the slot index; the generator passes the slot index, not the
//     dynamic flattened index -- unused on the non-incremental fast path). NOT a base virtual, so a
//     plain non-virtual accessor.
//   * `[Slot("Getter")] public partial Accessor? Getter` -- the `get` accessor (a single NULLABLE
//     `Accessor?` slot at slot 4; absent for a set-only or expression-bodied indexer). The slot
//     FOLLOWS the `Parameters` collection, so the setter uses the INDEX-LESS `SetChildNode`. NOT
//     an override, so a plain non-virtual accessor. Reusing the `Slots::Getter` kind (cycle-broken
//     into `Accessor.hpp` by `PropertyDeclaration` D276).
//   * `[Slot("Setter")] public partial Accessor? Setter` -- the `set`/`init` accessor (a single
//     NULLABLE `Accessor?` slot at slot 5; absent for a get-only or expression-bodied indexer).
//     The slot FOLLOWS the `Parameters` collection, so the setter uses the INDEX-LESS
//     `SetChildNode`. NOT an override, so a plain non-virtual accessor. Reusing the `Slots::Setter`
//     kind (cycle-broken into `Accessor.hpp` by `PropertyDeclaration` D276).
//   * `[Slot("ExpressionBody")] public partial Expression? ExpressionBody` -- the expression body
//     of an expression-bodied indexer (a single NULLABLE `Expression?` slot at slot 6, e.g. the
//     `x => x` in `int this[int x] => x`; absent for a classic `{ get; set; }` indexer). The slot
//     FOLLOWS the `Parameters` collection, so the setter uses the INDEX-LESS `SetChildNode`. NOT an
//     override, so a plain non-virtual accessor. Reusing the `Slots::ExpressionBody` kind (added to
//     `Slots.hpp` by `PropertyDeclaration` D276).
//
// The `Name`/`NameToken` virtuals are overridden: `Name` returns the literal `"Item"` (the
// conventional indexer name -- the decompiler prints an indexer as `this[...]`, but the `Name`
// convenience is the fixed `"Item"` so a resolver lookup by name works) and throws on set;
// `NameToken` returns null and throws on set (the `[EditorBrowsable(Never)]` hides them -- an
// indexer has no name token, the `this` keyword carries the identity). The `Name` term IS added to
// `DoMatch` (the `NameToken` is `[EditorBrowsable]`, NOT `[ExcludeFromMatch]` -- the
// `FieldDeclaration` D273 precedent: the `Name` `MatchString` term is present and vacuously matches
// `"Item"` vs `"Item"`). The `ThisKeyword` const string `"this"` ports as a `static constexpr const
// char*` (the `CheckedExpression.CheckedKeyword` D234 / `EventDeclaration.EventKeyword` D277
// precedent -- a static field, NOT instance state, so NOT in `MembersToMatch`/`DoMatch`).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]` on `IndexerDeclaration`,
// so the `Name` term IS added, the `FieldDeclaration` D273 / `PropertyDeclaration` D276
// precedent), `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every
// `EntityDeclaration`-derived node, then the per-property scan adds the four non-override non-
// `[ExcludeFromMatch]` `[Slot]` children (`PrivateImplementationType`, `Parameters`, `Getter`,
// `Setter`, `ExpressionBody`, in source declaration order). So `MembersToMatch` is `[Name,
// MatchAttributesAndModifiers, ReturnType, PrivateImplementationType, Parameters, Getter, Setter,
// ExpressionBody]`, and the generated `DoMatch` is `return other is IndexerDeclaration o &&
// MatchString(this.Name, o.Name) && this.MatchAttributesAndModifiers(o, match) &&
// MatchOptional(this.ReturnType, o.ReturnType, match) && MatchOptional(this.PrivateImplementationType,
// o.PrivateImplementationType, match) && this.Parameters.DoMatch(o.Parameters, match) &&
// MatchOptional(this.Getter, o.Getter, match) && MatchOptional(this.Setter, o.Setter, match) &&
// MatchOptional(this.ExpressionBody, o.ExpressionBody, match)`. The `Name` term is a `MatchString`
// over the always-`"Item"` `Name` (both return `"Item"`, so it vacuously matches -- the
// `FieldDeclaration` D273 always-empty-`Name` precedent applied to a fixed-const `Name`); the
// `ReturnType` term is `MatchOptional` (nullable recursive -- the generator treats the
// `EntityDeclaration` `ReturnType` uniformly as `MatchOptional`); the `PrivateImplementationType`/
// `Getter`/`Setter`/`ExpressionBody` terms are each `MatchOptional` over their nullable slots; the
// `Parameters` term is the collection recursive `DoMatch` (the generator emits a collection-typed
// recursive term directly, NOT `MatchOptional` -- the `FieldDeclaration.Variables` D273 /
// `SwitchSection.Statements` D268 precedent). The `MatchAttributesAndModifiers` helper (on
// `EntityDeclaration`, protected) matches the `Modifiers` scalar (the `Any`-wildcard) AND the
// `Attributes` collection together.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// ReturnType (required), PrivateImplementationType (single, optional), Parameters (collection,
// optional), Getter (single, optional), Setter (single, optional), ExpressionBody (single,
// optional)]` (the `Modifiers` scalar lives on the `EntityDeclaration` base and is NOT a declared
// member of `IndexerDeclaration`, so `GetMembers()` does not add it -- the `DestructorDeclaration`
// D272 / `PropertyDeclaration` D276 precedent). `RequiredConstructorPrefixLength` is 2 (through the
// last non-optional param `ReturnType` at index 1 -- the generator walks the whole `ctorParams`
// list and takes the last non-optional index + 1, the `CatchClause` D254 / `PropertyDeclaration`
// D276 precedent). `ConstructorPrefixLengths` is {2, 7}; the (len=2) ctor
// `(IEnumerable<AttributeSection>, AstType)` and the (len=7) ctor both call
// `this.Attributes.AddRange(...)` for the `Attributes` collection (and the (len=7) ctor also calls
// `this.Parameters.AddRange(...)` for the `Parameters` collection -- both `AddRange` conveniences
// are the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. An
// `IndexerDeclaration` is built via the empty ctor + `ReturnType(...)` + `PrivateImplementationType(...)`
// + `Parameters().Add(...)` + `Getter(...)` + `Setter(...)` + `ExpressionBody(...)` +
// `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitIndexerDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitIndexerDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required),
// `PrivateImplementationTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at
// `Slots.PrivateImplementationType`, nullable), `ParametersSlot` (a
// `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection), `GetterSlot`
// (a `CSharpSlotInfoT<Accessor>` pointing at `Slots.Getter`, nullable), `SetterSlot` (a
// `CSharpSlotInfoT<Accessor>` pointing at `Slots.Setter`, nullable), `ExpressionBodySlot` (a
// `CSharpSlotInfoT<Expression>` pointing at `Slots.ExpressionBody`, nullable). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// a fresh node, the `Modifiers` scalar copied via the public `Modifiers()` getter/setter (the
// base's private `modifiers_` is not accessible from the derived `Clone` -- the C#
// `MemberwiseClone` copies the private backing; the port uses the public surface, the
// `DestructorDeclaration` D272 precedent), the annotation channel copied (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern), the `ReturnType` deep-cloned through the
// setter (which re-parents; `AstType::Clone()` returns `AstType*`, which `ReturnType(AstType*)`
// accepts directly), every `Attributes` element deep-cloned through `Add` (which re-parents and
// re-indexes; `AttributeSection::Clone()` returns `AttributeSection*`), every `Parameters` element
// deep-cloned through `Add` (which re-parents and re-indexes; `ParameterDeclaration::Clone()`
// returns `ParameterDeclaration*`), and the `PrivateImplementationType`/`Getter`/`Setter`/
// `ExpressionBody` deep-cloned through their setters (which re-parent; `AstType::Clone()`/
// `Accessor::Clone()`/`Expression::Clone()` return the covariant concrete types the setters accept
// directly). No own location fields (does not derive `EndLocation`), so the print-time
// `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
// `PropertyDeclaration` D276 no-location-copy precedent). The covariant return is
// `IndexerDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual -- `EntityDeclaration`
// re-declares no typed `Clone`, faithful to its empty hand-written partial).
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`AstType`/`ParameterDeclaration`/
// `Accessor`/`Expression`/`SymbolKind` -- the `Attributes()`/`ReturnType()`/
// `PrivateImplementationType()`/`Parameters()`/`Getter()`/`Setter()`/`ExpressionBody()`/
// `SymbolKind()` accessors do not collide with any class in the `Syntax` namespace), so no
// elaborated-type-specifier is needed anywhere; the plain element types resolve to the classes. The
// `[Slot("Parameter")]` argument names the slot KIND but the PROPERTY is `Parameters`, so the
// `Parameters()` accessor does NOT collide with the `ParameterDeclaration` class (no member is
// named `ParameterDeclaration` -- the `MemberType.MemberName` D238 / `PropertyDeclaration` D276
// differently-named-property precedent). This is the `CustomEventDeclaration` D277
// collection -> single -> single -> collection -> single -> single -> single dispatch shape (the
// `Attributes` collection plus the `Parameters` collection, with five singles interleaved) -- the
// `ComposedType` D242 two-collection shape with a `Parameters` collection in place of
// `ComposedType`'s second `ArraySpecifiers` collection, plus three trailing nullable singles.
//
// ONE new `Slots` constant this iteration: `Slots::Parameter` (a `ParameterDeclaration`-typed
// collection kind, cycle-broken into `ParameterDeclaration.hpp` -- `ParameterDeclaration.hpp`
// includes `Slots.hpp` for its per-node `AttributesSlot`/`TypeSlot`/`NameTokenSlot`/
// `DefaultExpressionSlot`, the `Slots::Variable` D267 cycle-breaking precedent applied to a
// `ParameterDeclaration`-typed collection kind). `Slots::AttributeSection` (cycle-broken in
// `AttributeSection.hpp` D241), `Slots::Type` (D240), `Slots::PrivateImplementationType` (D276),
// `Slots::Getter` and `Slots::Setter` (cycle-broken in `Accessor.hpp` D276), and
// `Slots::ExpressionBody` (D276) are already ported.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_INDEXERDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_INDEXERDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class IndexerDeclaration : EntityDeclaration`. `final` (the C#
// `sealed`): no further derivation. The ninth concrete `TypeMember`, an indexer (an
// `this[...]`-indexed property), deriving from `EntityDeclaration` (the `TypeMember` base), not
// from `Statement`/`Expression`/`AstType`. Structurally the `CustomEventDeclaration` D277
// `Attributes` collection + trailing-singles shape with a `Parameters` collection inserted before
// the `Getter`/`Setter`/`ExpressionBody` nullable singles -- the `ComposedType` D242 two-collection
// shape with a `ParameterDeclaration`-typed second collection.
class IndexerDeclaration final : public EntityDeclaration {
public:
    ~IndexerDeclaration() override = default;

    // The generated empty ctor (the C# `public IndexerDeclaration()`). The `Attributes` collection is
    // a member (the D222 always-present-stack-member design), initialized here with `baseIndex = 0`
    // (the collection is the first slot) and `supportsIncremental = false` (the node has TWO
    // collections, so neither is incremental -- an element's flattened `ChildIndex` is dynamic,
    // rebuilt lazily by `EnsureChildIndices` after a mutation). The `Parameters` collection is a
    // member initialized with `baseIndex = 3` (its slot index) and `supportsIncremental = false`
    // (the second of two collections). The five singles default to null; `ReturnType` is a REQUIRED
    // slot (a default-constructed node violates its required-slot invariant, the
    // `UnaryOperatorExpression` D231 precedent; `CheckInvariant` rejects an empty node), the other
    // four are nullable so their absence is invariant-valid.
    IndexerDeclaration() : attributes_(this, &AttributesSlot, 0, false),
                          parameters_(this, &ParametersSlot, 3, false) {}

    // The C# `public const string ThisKeyword = "this"` -- the `this` keyword token literal, part
    // of the node's public API (the output visitor reads it). Ports as a `static constexpr const
    // char*` (the `CheckedExpression.CheckedKeyword` D234 / `EventDeclaration.EventKeyword` D277
    // precedent). The generator excludes `const string` fields from `MembersToMatch` (it iterates
    // only instance `IPropertySymbol`s), so it never appears in the generated `DoMatch`.
    static constexpr const char* ThisKeyword = "this";

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Indexer; } }` -- the
    // kind of member this declaration is (an indexer). Overrides the base abstract `SymbolKind` (the
    // `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Indexer` avoids a `using` (the
    // enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Indexer;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the indexer (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental -- the node's first of two
    // collections). The C# lazily allocates the wrapper; the D222 port makes the collection an
    // always-present stack member, so the accessor returns the member directly. Overrides the base
    // `EntityDeclaration::Attributes` (the base body returns a detached empty via `GetChildren`;
    // this override returns the real `attributes_` member). A `const` convenience overload returns
    // `const&` for a `const IndexerDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `ReturnType` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Type")] public override partial AstType ReturnType` -- a single
    // REQUIRED `AstType` slot at slot 1 (the indexer's declared type; non-nullable, so required --
    // `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the property setter
    // uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a
    // collection). Overrides the base `EntityDeclaration::ReturnType` (the base body kind-walks for
    // the `Type` kind; this override returns the backing field directly, the generated
    // `get => field!`). The getter returns the raw pointer (null for a half-constructed node; the
    // C# `!` null-forgiving).
    AstType* ReturnType() const override { return returnType_; }
    void ReturnType(AstType* value) override {
        SetChildNode(returnType_, value);
    }

    // ---- The `PrivateImplementationType` slot (a NULLABLE single `AstType`, NOT a base virtual) -
    // The generated `[Slot("PrivateImplementationType")] public partial AstType?
    // PrivateImplementationType` -- a single NULLABLE `AstType?` slot at slot 2 (the
    // explicit-interface-implementation type, e.g. the `I` in `int I.this[int x] { get; set; }`;
    // null when the indexer is not an explicit interface implementation). The slot FOLLOWS the
    // `Attributes` collection, so the property setter uses the INDEX-LESS `SetChildNode(ref field,
    // value)`. NOT an override (the `EntityDeclaration` base declares no `PrivateImplementationType`
    // virtual), so a plain non-virtual accessor. `CheckInvariant` passes without a
    // `PrivateImplementationType` (the slot is nullable). Reuses the `Slots::PrivateImplementationType`
    // kind (added by `PropertyDeclaration` D276).
    AstType* PrivateImplementationType() const { return privateImplementationType_; }
    void PrivateImplementationType(AstType* value) {
        SetChildNode(privateImplementationType_, value);
    }

    // ---- The `Name`/`NameToken` overrides (hidden from users; an indexer has no name token) --
    // The C# `public override string Name { get { return "Item"; } set { throw new
    // NotSupportedException(); } }` -- the conventional indexer name (the decompiler prints an
    // indexer as `this[...]`, but the `Name` convenience is the fixed `"Item"` so a resolver lookup
    // by name works). The setter throws (`NotSupportedException` ports as `std::logic_error`).
    // Overrides the base `EntityDeclaration::Name` virtual. The `Name` `MatchString` term in
    // `DoMatch` vacuously matches `"Item"` vs `"Item"` (the `FieldDeclaration` D273 always-empty
    // `Name` precedent applied to a fixed-const `Name`).
    std::string Name() const override { return std::string("Item"); }
    void Name(std::string_view) override {
        throw std::logic_error("IndexerDeclaration.Name is not supported");
    }
    // The C# `[EditorBrowsable(EditorBrowsableState.Never)] public override Identifier NameToken
    // { get { return null!; } set { throw new NotSupportedException(); } }` -- an indexer has no
    // name token (the `this` keyword carries the identity), so the inherited `NameToken` is hidden
    // and returns null. The setter throws. Overrides the base `EntityDeclaration::NameToken`
    // virtual (the `FieldDeclaration` D273 / `EventDeclaration` D277 throw-on-set precedent).
    Identifier* NameToken() const override { return nullptr; }
    void NameToken(Identifier*) override {
        throw std::logic_error("IndexerDeclaration.NameToken is not supported");
    }

    // ---- The `Parameters` collection slot (NOT a base virtual) -----------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the indexer's parameter list (a `CSharpSlotInfoT<ParameterDeclaration>` slot
    // at slot 3, the node's SECOND of TWO collections, non-incremental). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly. NOT a base virtual, so a plain non-virtual accessor. A NEW
    // `Slots::Parameter` kind (cycle-broken into `ParameterDeclaration.hpp` this iteration). A
    // `const` convenience overload returns `const&` for a `const IndexerDeclaration*`.
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The `Getter` slot (a NULLABLE single `Accessor`, NOT a base virtual) ---------------
    // The generated `[Slot("Getter")] public partial Accessor? Getter` -- a single NULLABLE
    // `Accessor?` slot at slot 4 (the `get` accessor; absent for a set-only or expression-bodied
    // indexer). The slot FOLLOWS the `Parameters` collection, so the property setter uses the
    // INDEX-LESS `SetChildNode(ref field, value)`. NOT an override, so a plain non-virtual accessor.
    // `CheckInvariant` passes without a `Getter` (the slot is nullable). Reuses the `Slots::Getter`
    // kind (cycle-broken into `Accessor.hpp` by `PropertyDeclaration` D276).
    Accessor* Getter() const { return getter_; }
    void Getter(Accessor* value) {
        SetChildNode(getter_, value);
    }

    // ---- The `Setter` slot (a NULLABLE single `Accessor`, NOT a base virtual) ---------------
    // The generated `[Slot("Setter")] public partial Accessor? Setter` -- a single NULLABLE
    // `Accessor?` slot at slot 5 (the `set`/`init` accessor; absent for a get-only or
    // expression-bodied indexer). The slot FOLLOWS the `Parameters` collection, so the property
    // setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT an override, so a plain
    // non-virtual accessor. `CheckInvariant` passes without a `Setter` (the slot is nullable).
    // Reuses the `Slots::Setter` kind (cycle-broken into `Accessor.hpp` by `PropertyDeclaration`
    // D276).
    Accessor* Setter() const { return setter_; }
    void Setter(Accessor* value) {
        SetChildNode(setter_, value);
    }

    // ---- The `ExpressionBody` slot (a NULLABLE single `Expression`, NOT a base virtual) -----
    // The generated `[Slot("ExpressionBody")] public partial Expression? ExpressionBody` -- a
    // single NULLABLE `Expression?` slot at slot 6 (the expression body of an expression-bodied
    // indexer, e.g. the `x` in `int this[int x] => x`; absent for a classic `{ get; set; }`
    // indexer). The slot FOLLOWS the `Parameters` collection, so the property setter uses the
    // INDEX-LESS `SetChildNode(ref field, value)`. NOT an override, so a plain non-virtual accessor.
    // `CheckInvariant` passes without an `ExpressionBody` (the slot is nullable). Reuses the
    // `Slots::ExpressionBody` kind (added to `Slots.hpp` by `PropertyDeclaration` D276).
    Expression* ExpressionBody() const { return expressionBody_; }
    void ExpressionBody(Expression* value) {
        SetChildNode(expressionBody_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's first collection); `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>`
    // pointing at `Slots.Type`, required); `PrivateImplementationTypeSlot` (a
    // `CSharpSlotInfoT<AstType>` pointing at `Slots.PrivateImplementationType`, nullable);
    // `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
    // collection -- the node's second collection); `GetterSlot` (a `CSharpSlotInfoT<Accessor>`
    // pointing at `Slots.Getter`, nullable); `SetterSlot` (a `CSharpSlotInfoT<Accessor>` pointing at
    // `Slots.Setter`, nullable); `ExpressionBodySlot` (a `CSharpSlotInfoT<Expression>` pointing at
    // `Slots.ExpressionBody`, nullable). NO name shadowing (no member is named
    // `AttributeSection`/`AstType`/`ParameterDeclaration`/`Accessor`/`Expression`), so the element
    // types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> ReturnTypeSlot{"ReturnType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<AstType> PrivateImplementationTypeSlot{"PrivateImplementationType", false, &Slots::PrivateImplementationType, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<Accessor> GetterSlot{"Getter", false, &Slots::Getter, true};
    static inline const CSharpSlotInfoT<Accessor> SetterSlot{"Setter", false, &Slots::Setter, true};
    static inline const CSharpSlotInfoT<Expression> ExpressionBodySlot{"ExpressionBody", false, &Slots::ExpressionBody, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitIndexerDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitIndexerDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Seven slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `ReturnType` single at slot 1 (`attrCount`), a `PrivateImplementationType`
    // single at slot 2 (`attrCount + 1`), a `Parameters` collection at slot 3 (the contiguous range
    // `[attrCount + 2, attrCount + 2 + paramCount)`), a `Getter` single at slot 4
    // (`attrCount + 2 + paramCount`), a `Setter` single at slot 5 (`attrCount + 3 + paramCount`), and
    // an `ExpressionBody` single at slot 6 (`attrCount + 4 + paramCount`). `GetChildCount` is
    // `attrCount + paramCount + 5` (the two collections' current lengths plus the five singles --
    // each single slot contributes 1 to the flattened count regardless of whether it is filled);
    // `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a
    // running index (the generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections`
    // shape -- a collection step, two single steps, a collection step, then three single steps).
    // `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind and
    // the `Parameters` collection for the `Parameter` kind. This is the `ComposedType` D242
    // two-collection shape with a `Parameters` collection in place of `ComposedType`'s second
    // `ArraySpecifiers` collection, plus three trailing nullable singles.
    int GetChildCount() const override { return attributes_.Count() + parameters_.Count() + 5; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        if (i == 0)
            return returnType_;
        i--;
        if (i == 0)
            return privateImplementationType_;
        i--;
        {
            int n = parameters_.Count();
            if (i < n)
                return parameters_.At(i);
            i -= n;
        }
        if (i == 0)
            return getter_;
        i--;
        if (i == 0)
            return setter_;
        i--;
        if (i == 0)
            return expressionBody_;
        throw std::out_of_range("IndexerDeclaration::GetChild");
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
            SetChildNode(returnType_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(privateImplementationType_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        {
            int n = parameters_.Count();
            if (i < n) {
                parameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(getter_, static_cast<Accessor*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(setter_, static_cast<Accessor*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(expressionBody_, static_cast<Expression*>(value), index);
            return;
        }
        throw std::out_of_range("IndexerDeclaration::SetChild");
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
            return &ReturnTypeSlot;
        i--;
        if (i == 0)
            return &PrivateImplementationTypeSlot;
        i--;
        {
            int n = parameters_.Count();
            if (i < n)
                return &ParametersSlot;
            i -= n;
        }
        if (i == 0)
            return &GetterSlot;
        i--;
        if (i == 0)
            return &SetterSlot;
        i--;
        if (i == 0)
            return &ExpressionBodySlot;
        throw std::out_of_range("IndexerDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::Parameter)
            return &parameters_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is IndexerDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && MatchOptional(this.PrivateImplementationType, o.PrivateImplementationType, match)
    // && this.Parameters.DoMatch(o.Parameters, match) && MatchOptional(this.Getter, o.Getter,
    // match) && MatchOptional(this.Setter, o.Setter, match) && MatchOptional(this.ExpressionBody,
    // o.ExpressionBody, match)`. The terms are in `MembersToMatch` order (the generator adds `Name`,
    // `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every `EntityDeclaration`-
    // derived node -- `NameToken` is NOT `[ExcludeFromMatch]` on `IndexerDeclaration`, so the `Name`
    // `String` term IS added, the `FieldDeclaration` D273 / `PropertyDeclaration` D276 precedent;
    // then the per-property scan adds `PrivateImplementationType`/`Parameters`/`Getter`/`Setter`/
    // `ExpressionBody` in source declaration order). The `Name` term is a `MatchString` over the
    // always-`"Item"` `Name` (the override returns the fixed literal `"Item"`, so it vacuously
    // matches `"Item"` vs `"Item"` -- the `FieldDeclaration` D273 always-empty-`Name` precedent
    // applied to a fixed-const `Name`); the `ReturnType` term is `MatchOptional` (nullable
    // recursive -- the generator treats the `EntityDeclaration` `ReturnType` uniformly as
    // `MatchOptional`); the `PrivateImplementationType`/`Getter`/`Setter`/`ExpressionBody` terms
    // are each `MatchOptional` over their nullable slots; the `Parameters` term is the collection
    // recursive `DoMatch` (the generator emits a collection-typed recursive term directly, NOT
    // `MatchOptional` -- the `FieldDeclaration.Variables` D273 / `SwitchSection.Statements` D268
    // precedent). A type-only mismatch (not an `IndexerDeclaration`) rejects early. The `Name()`
    // calls are inlined in the `MatchString` arguments (the `MemberType` D238 / `PropertyDeclaration`
    // D276 precedent) so the C# `&&` short-circuit is preserved; the `std::string` temporaries live
    // until the end of the full `return` expression, so the `std::string_view` views are valid for
    // the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<IndexerDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && MatchOptional(privateImplementationType_, o->privateImplementationType_, match)
            && parameters_.DoMatch(o->parameters_, match)
            && MatchOptional(getter_, o->getter_, match)
            && MatchOptional(setter_, o->setter_, match)
            && MatchOptional(expressionBody_, o->expressionBody_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 precedent), the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `ReturnType`
    // deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`, which
    // `ReturnType(AstType*)` accepts directly), every `Attributes` element deep-cloned through `Add`
    // (which re-parents and re-indexes; `AttributeSection::Clone()` returns `AttributeSection*`),
    // every `Parameters` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`), and the
    // `PrivateImplementationType`/`Getter`/`Setter`/`ExpressionBody` deep-cloned through their
    // setters (which re-parent; `AstType::Clone()`/`Accessor::Clone()`/`Expression::Clone()` return
    // the covariant concrete types the setters accept directly). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `IndexerDeclaration` does not derive `EndLocation`), so they are not copied (the
    // `DestructorDeclaration` D272 / `PropertyDeclaration` D276 no-location-copy precedent). The
    // covariant return is `IndexerDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
    // `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written partial;
    // the covariant `IndexerDeclaration*` is a valid override of `AstNode::Clone`).
    IndexerDeclaration* Clone() const override {
        auto* node = new IndexerDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (returnType_ != nullptr)
            node->ReturnType(returnType_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (privateImplementationType_ != nullptr)
            node->PrivateImplementationType(privateImplementationType_->Clone());
        for (int i = 0; i < parameters_.Count(); i++)
            node->parameters_.Add(parameters_.At(i)->Clone());
        if (getter_ != nullptr)
            node->Getter(getter_->Clone());
        if (setter_ != nullptr)
            node->Setter(setter_->Clone());
        if (expressionBody_ != nullptr)
            node->ExpressionBody(expressionBody_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_`/`parameters_` are the always-present collection members
    // (empty until the first `Add`, both non-incremental -- the node has two collections);
    // `returnType_` is null until set (a REQUIRED slot -- `CheckInvariant` asserts it is filled);
    // the other four are null until set (NULLABLE slots -- their absence is invariant-valid). NO
    // name shadowing (no member is named `AttributeSection`/`AstType`/`ParameterDeclaration`/
    // `Accessor`/`Expression`), so the field types are the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstType* returnType_ = nullptr;
    AstType* privateImplementationType_ = nullptr;
    AstNodeCollectionT<ParameterDeclaration> parameters_;
    Accessor* getter_ = nullptr;
    Accessor* setter_ = nullptr;
    Expression* expressionBody_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_INDEXERDECLARATION_HPP
