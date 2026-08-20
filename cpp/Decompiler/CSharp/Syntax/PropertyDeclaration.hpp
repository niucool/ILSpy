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

// Port of the `PropertyDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/PropertyDeclaration.cs (the generated
// `PropertyDeclaration.g.cs` + the hand-written partial, which declares only the `SymbolKind`
// override, the eight slot properties, and the `IsAutomaticProperty` helper -- no ctors, no const
// strings). The next in-order Phase-5 piece per the D275 plan ("PropertyDeclaration (Type +
// Identifier + PrivateImplementationType + Getter/Setter Accessor? singles + Initializer +
// ExpressionBody -- now unblocked by Accessor)").
//
// `property_declaration ::= attribute_section* modifier* type ( type '.' )? identifier '{'
// accessor* '}' ( '=' expression ';' )? | attribute_section* modifier* type ( type '.' )?
// identifier '=>' expression ';'` (C# grammar 15.7.1): a sealed `EntityDeclaration` (the
// `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final` -- no pattern
// placeholder). Eight `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the property (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's only collection
//     but NOT its last slot (seven singles trail it), so `supportsIncremental` is FALSE
//     (`collectionCount == 1 && slotIndex == 0 == slots.Count - 1` is false -- `slots.Count` is 8):
//     every `Add`/`Insert`/`Remove`/single-slot-set INVALIDATES the parent's indices for a lazy
//     `EnsureChildIndices` rebuild (the `DestructorDeclaration` D272 / `Accessor` D274 /
//     `EnumMemberDeclaration` D275 non-incremental precedent).
//   * `[Slot("Type")] public override partial AstType ReturnType` -- the property's declared type
//     (a single REQUIRED `AstType` slot at slot 1, non-nullable -- the property type is `AstType`,
//     no `?`; reusing `Slots::Type`). The slot FOLLOWS the `Attributes` collection, so its setter
//     uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a
//     collection; the `DestructorDeclaration` D272 / `ComposedType.BaseType` D242 precedent).
//     Overrides the base `EntityDeclaration::ReturnType` (the base body kind-walks for the `Type`
//     kind; this override returns the backing field directly, the generated `get => field!`).
//   * `[Slot("Identifier")] public override partial Identifier NameToken` -- the property's name
//     token (a single REQUIRED `Identifier` slot at slot 2, non-nullable; reusing
//     `Slots::Identifier`). NOT `[ExcludeFromMatch]` (unlike `DestructorDeclaration`), so the
//     generator adds the `Name` `String` `MatchString` term to `DoMatch` (the property's name IS
//     part of the structural match -- a pattern `int P` matches only a candidate named `P`). The
//     slot FOLLOWS the collection, so the setter uses the INDEX-LESS `SetChildNode`. Overrides the
//     base `EntityDeclaration::NameToken` (the base body kind-walks for the `Identifier` kind; this
//     override returns the backing field directly). `Name` is NOT overridden (the C# declares no
//     `Name` override), so the inherited base `Name()` kind-walks for the `Identifier` kind and
//     returns the `NameToken`'s `Name` (the real name -- the `EnumMemberDeclaration` D275
//     precedent, unlike `Accessor` D274 whose `Name` no-op returns empty).
//   * `[Slot("PrivateImplementationType")] public partial AstType? PrivateImplementationType` --
//     the explicit-interface-implementation type (a single NULLABLE `AstType?` slot at slot 3, e.g.
//     the `I` in `int I.P { get; set; }`; null when the property is not an explicit interface
//     implementation). The slot FOLLOWS the collection, so the setter uses the INDEX-LESS
//     `SetChildNode`. A NEW `Slots::PrivateImplementationType` kind (added to `Slots.hpp` this
//     iteration -- the `AstType` abstract base does not include `Slots.hpp`, so no include cycle).
//   * `[Slot("Getter")] public partial Accessor? Getter` -- the `get` accessor (a single NULLABLE
//     `Accessor?` slot at slot 4; absent for a set-only or expression-bodied property). The slot
//     FOLLOWS the collection, so the setter uses the INDEX-LESS `SetChildNode`. A NEW `Slots::Getter`
//     kind (cycle-broken into `Accessor.hpp` this iteration -- `Accessor.hpp` includes `Slots.hpp`
//     for its per-node slot statics, the `Slots::Body` D260 cycle-breaking precedent applied to an
//     `Accessor`-typed kind).
//   * `[Slot("Setter")] public partial Accessor? Setter` -- the `set`/`init` accessor (a single
//     NULLABLE `Accessor?` slot at slot 5; absent for a get-only or expression-bodied property).
//     The slot FOLLOWS the collection, so the setter uses the INDEX-LESS `SetChildNode`. A NEW
//     `Slots::Setter` kind (cycle-broken into `Accessor.hpp` this iteration, alongside
//     `Slots::Getter`).
//   * `[Slot("Expression")] public partial Expression? Initializer` -- the optional `= expression`
//     initializer (a single NULLABLE `Expression?` slot at slot 6, e.g. the `= 5` in `int P { get;
//     set; } = 5`; absent for a property with no initializer). The slot FOLLOWS the collection, so
//     the setter uses the INDEX-LESS `SetChildNode`. Reuses the existing `Slots::Expression` kind
//     (the kind-collapsing design is by `[Slot]` name regardless of single-vs-collection -- the
//     `UnaryOperatorExpression` D231 single-`Expression`-operand kind, the `BlockStatement` D256
//     collection-reuse precedent).
//   * `[Slot("ExpressionBody")] public partial Expression? ExpressionBody` -- the expression body
//     of an expression-bodied property (a single NULLABLE `Expression?` slot at slot 7, e.g. the
//     `5` in `int P => 5`; absent for a classic `{ get; set; }` property). The slot FOLLOWS the
//     collection, so the setter uses the INDEX-LESS `SetChildNode`. A NEW `Slots::ExpressionBody`
//     kind (added to `Slots.hpp` this iteration -- the `Expression` abstract base does not include
//     `Slots.hpp`, so no include cycle).
//
// The `IsAutomaticProperty` helper (a hand-written `bool` getter over `Getter.Body`/`Setter.Body`)
// is DEFERRED -- it is consumed only by the unported transform stage
// (`TransformFieldAndConstructorInitializers.cs`), the D234 value-vs-behavior discriminator
// (a behavior helper consumed by an unported stage, unlike a const string which is a one-line
// literal). It lands with the transform stage.
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]` on `PropertyDeclaration`,
// so the `Name` term IS added, the `EnumMemberDeclaration` D275 precedent),
// `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every `EntityDeclaration`-derived
// node, then the per-property scan adds the five non-override non-`[ExcludeFromMatch]` `[Slot]`
// children (`PrivateImplementationType`, `Getter`, `Setter`, `Initializer`, `ExpressionBody`,
// each a `MatchOptional` recursive term). So `MembersToMatch` is `[Name, MatchAttributesAndModifiers,
// ReturnType, PrivateImplementationType, Getter, Setter, Initializer, ExpressionBody]`, and the
// generated `DoMatch` is `return other is PropertyDeclaration o && MatchString(this.Name, o.Name)
// && this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
// match) && MatchOptional(this.PrivateImplementationType, o.PrivateImplementationType, match) &&
// MatchOptional(this.Getter, o.Getter, match) && MatchOptional(this.Setter, o.Setter, match) &&
// MatchOptional(this.Initializer, o.Initializer, match) && MatchOptional(this.ExpressionBody,
// o.ExpressionBody, match)`. The `Name` term is a `MatchString` over the `NameToken`'s name (the
// inherited base `Name()` kind-walks for the `Identifier` kind and returns the `NameToken`'s `Name`
// -- `PropertyDeclaration` does NOT override `Name`, so the inherited virtual returns the real
// name); the `ReturnType` term is `MatchOptional` (nullable recursive -- the generator treats the
// `EntityDeclaration` `ReturnType` uniformly as `MatchOptional` across all subclasses, so a node
// with a real `ReturnType` slot matches when both sides carry one and the pattern
// `ReturnType.DoMatch` accepts the candidate); the `PrivateImplementationType`/`Getter`/`Setter`/
// `Initializer`/`ExpressionBody` terms are each `MatchOptional` over their nullable slots. The
// `MatchAttributesAndModifiers` helper (on `EntityDeclaration`, protected) matches the `Modifiers`
// scalar (the `Any`-wildcard) AND the `Attributes` collection together.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// ReturnType (required), NameToken (required), PrivateImplementationType (single, optional),
// Getter (single, optional), Setter (single, optional), Initializer (single, optional),
// ExpressionBody (single, optional)]` -- the `Modifiers` scalar lives on the `EntityDeclaration`
// base and is NOT a declared member of `PropertyDeclaration`, so `GetMembers()` does not add it to
// `CtorParams` (the `DestructorDeclaration` D272 / `FieldDeclaration` D273 precedent: the generated
// ctors take no `Modifiers`). `RequiredConstructorPrefixLength` is 3 (through the last
// non-optional param `NameToken` at index 2 -- the generator walks the whole `ctorParams` list and
// takes the last non-optional index + 1, the `CatchClause` D254 precedent). `ConstructorPrefixLengths`
// is {3, 8}; the (len=3) ctor `(IEnumerable<AttributeSection>, AstType, Identifier)` and the
// (len=8) ctor both call `this.Attributes.AddRange(...)` for the `Attributes` collection (the
// `AddRange` convenience is the D222 deferral), so they are DEFERRED; the empty ctor is the only
// portable ctor. A `PropertyDeclaration` is built via the empty ctor + `ReturnType(...)` +
// `NameToken(...)` + `PrivateImplementationType(...)` + `Getter(...)` + `Setter(...)` +
// `Initializer(...)` + `ExpressionBody(...)` + `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitPropertyDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitPropertyDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required),
// `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required),
// `PrivateImplementationTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at
// `Slots.PrivateImplementationType`, nullable), `GetterSlot` (a `CSharpSlotInfoT<Accessor>`
// pointing at `Slots.Getter`, nullable), `SetterSlot` (a `CSharpSlotInfoT<Accessor>` pointing at
// `Slots.Setter`, nullable), `InitializerSlot` (a `CSharpSlotInfoT<Expression>` pointing at
// `Slots.Expression`, nullable), `ExpressionBodySlot` (a `CSharpSlotInfoT<Expression>` pointing at
// `Slots.ExpressionBody`, nullable). `Clone` is inherited in C# (`MemberwiseClone` +
// `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`): a fresh node, the `Modifiers`
// scalar copied via the public `Modifiers()` getter/setter (the base's private `modifiers_` is not
// accessible from the derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the
// port uses the public surface, the `DestructorDeclaration` D272 precedent), the annotation
// channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
// `NameToken` deep-cloned through the setter (`Identifier::Clone()` returns `Identifier*`), the
// `ReturnType` deep-cloned through the setter (`AstType::Clone()` returns `AstType*`), every
// `Attributes` element deep-cloned through `Add` (`AttributeSection::Clone()` returns
// `AttributeSection*`), and the `PrivateImplementationType`/`Getter`/`Setter`/`Initializer`/
// `ExpressionBody` deep-cloned through their setters (`AstType::Clone()`/`Accessor::Clone()`/
// `Expression::Clone()` return the covariant concrete types the setters accept directly). No own
// location fields (does not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation`
// are not copied (the `DestructorDeclaration` D272 no-location-copy precedent). The covariant
// return is `PropertyDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
// `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written partial).
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`AstType`/`Identifier`/
// `Accessor`/`Expression`/`SymbolKind` -- the `Attributes()`/`ReturnType()`/`NameToken()`/
// `PrivateImplementationType()`/`Getter()`/`Setter()`/`Initializer()`/`ExpressionBody()`/
// `SymbolKind()` accessors do not collide with any class in the `Syntax` namespace), so no
// elaborated-type-specifier is needed anywhere; the plain element types resolve to the classes.
// The `[Slot("Identifier")]`/`[Slot("Expression")]` arguments name the slot KINDS but the
// PROPERTIES are `NameToken`/`Initializer`, so the accessors do NOT collide with the
// `Identifier`/`Expression` classes (the `MemberType.MemberName` D238 / `EnumMemberDeclaration`
// D275 differently-named-property precedent). This is the `EnumMemberDeclaration` D275 shape
// extended with five more nullable singles (the `DestructorDeclaration` D272 collection -> single
// -> single dispatch shape with five more trailing nullable singles).
//
// FOUR new `Slots` constants this iteration: `Slots::PrivateImplementationType` and
// `Slots::ExpressionBody` (both `AstType`/`Expression`-typed, added to `Slots.hpp` -- the abstract
// bases do not include `Slots.hpp`, so no include cycle), and `Slots::Getter` and `Slots::Setter`
// (both `Accessor`-typed, cycle-broken into `Accessor.hpp` -- `Accessor.hpp` includes `Slots.hpp`
// for its per-node slot statics, the `Slots::Body` D260 precedent). `Slots::AttributeSection`,
// `Slots::Type`, `Slots::Identifier`, and `Slots::Expression` are already ported.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_PROPERTYDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_PROPERTYDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class PropertyDeclaration : EntityDeclaration`. `final` (the C#
// `sealed`): no further derivation. A type-member declaration (a property), deriving from
// `EntityDeclaration` (the `TypeMember` base), not from `Statement`/`Expression`/`AstType`.
class PropertyDeclaration final : public EntityDeclaration {
public:
    ~PropertyDeclaration() override = default;

    // The generated empty ctor (the C# `public PropertyDeclaration()`). The `Attributes`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = false` (the
    // collection is NOT the node's last slot -- seven singles trail it -- so an element's flattened
    // `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation). The seven
    // singles default to null; `ReturnType` and `NameToken` are REQUIRED slots (a default-constructed
    // node violates their required-slot invariants, the `UnaryOperatorExpression` D231 precedent;
    // `CheckInvariant` rejects an empty node), the other five are nullable so their absence is
    // invariant-valid.
    PropertyDeclaration() : attributes_(this, &AttributesSlot, 0, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Property; } }` -- the
    // kind of member this declaration is (a property). Overrides the base abstract `SymbolKind`
    // (the `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Property` avoids a
    // `using` (the enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Property;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the property (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental). The C# lazily allocates
    // the wrapper; the D222 port makes the collection an always-present stack member, so the
    // accessor returns the member directly. Overrides the base `EntityDeclaration::Attributes`
    // (the base body returns a detached empty via `GetChildren`; this override returns the real
    // `attributes_` member). A `const` convenience overload returns `const&` for a `const
    // PropertyDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `ReturnType` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Type")] public override partial AstType ReturnType` -- a single
    // REQUIRED `AstType` slot at slot 1 (the property's declared type; non-nullable, so required --
    // `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the property setter
    // uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index after a
    // collection). Overrides the base `EntityDeclaration::ReturnType` (the base body kind-walks
    // for the `Type` kind; this override returns the backing field directly, the generated
    // `get => field!`). The getter returns the raw pointer (null for a half-constructed node; the
    // C# `!` null-forgiving).
    AstType* ReturnType() const override { return returnType_; }
    void ReturnType(AstType* value) override {
        SetChildNode(returnType_, value);
    }

    // ---- The `NameToken` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Identifier")] public override partial Identifier NameToken` -- a
    // single REQUIRED `Identifier` slot at slot 2 (the property's name token; non-nullable, so
    // required -- `IsOptional` is false). NOT `[ExcludeFromMatch]` (unlike `DestructorDeclaration`),
    // so the `Name` `MatchString` term IS added to `DoMatch` (the property's name IS part of the
    // structural match). The slot FOLLOWS the `Attributes` collection, so the property setter uses
    // the INDEX-LESS `SetChildNode(ref field, value)`. Overrides the base
    // `EntityDeclaration::NameToken` (the base body kind-walks for the `Identifier` kind; this
    // override returns the backing field directly, the generated `get => field!`). `Name` is NOT
    // overridden (the C# declares no `Name` override), so the inherited base `Name()` kind-walks
    // for the `Identifier` kind and returns the `NameToken`'s `Name` (the real name -- the
    // `EnumMemberDeclaration` D275 precedent).
    Identifier* NameToken() const override { return nameToken_; }
    void NameToken(Identifier* value) override {
        SetChildNode(nameToken_, value);
    }

    // ---- The `PrivateImplementationType` slot (a NULLABLE single `AstType`, NOT a base virtual) -
    // The generated `[Slot("PrivateImplementationType")] public partial AstType?
    // PrivateImplementationType` -- a single NULLABLE `AstType?` slot at slot 3 (the
    // explicit-interface-implementation type, e.g. the `I` in `int I.P { get; set; }`; null when the
    // property is not an explicit interface implementation). The slot FOLLOWS the `Attributes`
    // collection, so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT
    // an override (the `EntityDeclaration` base declares no `PrivateImplementationType` virtual), so
    // a plain non-virtual accessor. `CheckInvariant` passes without a `PrivateImplementationType`
    // (the slot is nullable). A NEW `Slots::PrivateImplementationType` kind (an `AstType`-typed kind
    // added this iteration).
    AstType* PrivateImplementationType() const { return privateImplementationType_; }
    void PrivateImplementationType(AstType* value) {
        SetChildNode(privateImplementationType_, value);
    }

    // ---- The `Getter` slot (a NULLABLE single `Accessor`, NOT a base virtual) ---------------
    // The generated `[Slot("Getter")] public partial Accessor? Getter` -- a single NULLABLE
    // `Accessor?` slot at slot 4 (the `get` accessor; absent for a set-only or expression-bodied
    // property). The slot FOLLOWS the `Attributes` collection, so the property setter uses the
    // INDEX-LESS `SetChildNode(ref field, value)`. NOT an override, so a plain non-virtual accessor.
    // `CheckInvariant` passes without a `Getter` (the slot is nullable). A NEW `Slots::Getter` kind
    // (an `Accessor`-typed kind, cycle-broken into `Accessor.hpp` this iteration).
    Accessor* Getter() const { return getter_; }
    void Getter(Accessor* value) {
        SetChildNode(getter_, value);
    }

    // ---- The `Setter` slot (a NULLABLE single `Accessor`, NOT a base virtual) ---------------
    // The generated `[Slot("Setter")] public partial Accessor? Setter` -- a single NULLABLE
    // `Accessor?` slot at slot 5 (the `set`/`init` accessor; absent for a get-only or
    // expression-bodied property). The slot FOLLOWS the `Attributes` collection, so the property
    // setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT an override, so a plain
    // non-virtual accessor. `CheckInvariant` passes without a `Setter` (the slot is nullable). A NEW
    // `Slots::Setter` kind (an `Accessor`-typed kind, cycle-broken into `Accessor.hpp` this
    // iteration, alongside `Slots::Getter`).
    Accessor* Setter() const { return setter_; }
    void Setter(Accessor* value) {
        SetChildNode(setter_, value);
    }

    // ---- The `Initializer` slot (a NULLABLE single `Expression`, NOT a base virtual) --------
    // The generated `[Slot("Expression")] public partial Expression? Initializer` -- a single
    // NULLABLE `Expression?` slot at slot 6 (the optional `= expression` initializer, e.g. the `= 5`
    // in `int P { get; set; } = 5`; absent for a property with no initializer). The slot FOLLOWS
    // the `Attributes` collection, so the property setter uses the INDEX-LESS `SetChildNode(ref
    // field, value)`. NOT an override, so a plain non-virtual accessor. `CheckInvariant` passes
    // without an `Initializer` (the slot is nullable). Reuses the existing `Slots::Expression` kind
    // (the kind-collapsing design is by `[Slot]` name regardless of single-vs-collection).
    Expression* Initializer() const { return initializer_; }
    void Initializer(Expression* value) {
        SetChildNode(initializer_, value);
    }

    // ---- The `ExpressionBody` slot (a NULLABLE single `Expression`, NOT a base virtual) -----
    // The generated `[Slot("ExpressionBody")] public partial Expression? ExpressionBody` -- a
    // single NULLABLE `Expression?` slot at slot 7 (the expression body of an expression-bodied
    // property, e.g. the `5` in `int P => 5`; absent for a classic `{ get; set; }` property). The
    // slot FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
    // `SetChildNode(ref field, value)`. NOT an override, so a plain non-virtual accessor.
    // `CheckInvariant` passes without an `ExpressionBody` (the slot is nullable). A NEW
    // `Slots::ExpressionBody` kind (an `Expression`-typed kind added this iteration).
    Expression* ExpressionBody() const { return expressionBody_; }
    void ExpressionBody(Expression* value) {
        SetChildNode(expressionBody_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's only collection); `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>`
    // pointing at `Slots.Type`, required); `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
    // pointing at `Slots.Identifier`, required); `PrivateImplementationTypeSlot` (a
    // `CSharpSlotInfoT<AstType>` pointing at `Slots.PrivateImplementationType`, nullable);
    // `GetterSlot` (a `CSharpSlotInfoT<Accessor>` pointing at `Slots.Getter`, nullable);
    // `SetterSlot` (a `CSharpSlotInfoT<Accessor>` pointing at `Slots.Setter`, nullable);
    // `InitializerSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`, nullable);
    // `ExpressionBodySlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.ExpressionBody`,
    // nullable). NO name shadowing (no member is named `AttributeSection`/`AstType`/`Identifier`/
    // `Accessor`/`Expression`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> ReturnTypeSlot{"ReturnType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<AstType> PrivateImplementationTypeSlot{"PrivateImplementationType", false, &Slots::PrivateImplementationType, true};
    static inline const CSharpSlotInfoT<Accessor> GetterSlot{"Getter", false, &Slots::Getter, true};
    static inline const CSharpSlotInfoT<Accessor> SetterSlot{"Setter", false, &Slots::Setter, true};
    static inline const CSharpSlotInfoT<Expression> InitializerSlot{"Initializer", false, &Slots::Expression, true};
    static inline const CSharpSlotInfoT<Expression> ExpressionBodySlot{"ExpressionBody", false, &Slots::ExpressionBody, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitPropertyDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitPropertyDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitPropertyDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitPropertyDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Eight slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`) and seven single slots at slots 1-7 (`ReturnType` at `attrCount`,
    // `NameToken` at `attrCount + 1`, `PrivateImplementationType` at `attrCount + 2`, `Getter` at
    // `attrCount + 3`, `Setter` at `attrCount + 4`, `Initializer` at `attrCount + 5`, `ExpressionBody`
    // at `attrCount + 6`). `GetChildCount` is `attrCount + 7` (the collection's current length plus
    // the seven singles -- each single slot contributes 1 to the flattened count regardless of
    // whether it is filled); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each
    // one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a collection step,
    // then seven single steps). `GetCollectionByKind` returns the `Attributes` collection for the
    // `AttributeSection` kind. This is the `EnumMemberDeclaration` D275 collection -> single ->
    // single dispatch shape extended with five more trailing singles.
    int GetChildCount() const override { return attributes_.Count() + 7; }

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
            return nameToken_;
        i--;
        if (i == 0)
            return privateImplementationType_;
        i--;
        if (i == 0)
            return getter_;
        i--;
        if (i == 0)
            return setter_;
        i--;
        if (i == 0)
            return initializer_;
        i--;
        if (i == 0)
            return expressionBody_;
        throw std::out_of_range("PropertyDeclaration::GetChild");
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
            SetChildNode(nameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(privateImplementationType_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
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
            SetChildNode(initializer_, static_cast<Expression*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(expressionBody_, static_cast<Expression*>(value), index);
            return;
        }
        throw std::out_of_range("PropertyDeclaration::SetChild");
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
            return &NameTokenSlot;
        i--;
        if (i == 0)
            return &PrivateImplementationTypeSlot;
        i--;
        if (i == 0)
            return &GetterSlot;
        i--;
        if (i == 0)
            return &SetterSlot;
        i--;
        if (i == 0)
            return &InitializerSlot;
        i--;
        if (i == 0)
            return &ExpressionBodySlot;
        throw std::out_of_range("PropertyDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is PropertyDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && MatchOptional(this.PrivateImplementationType, o.PrivateImplementationType, match)
    // && MatchOptional(this.Getter, o.Getter, match) && MatchOptional(this.Setter, o.Setter,
    // match) && MatchOptional(this.Initializer, o.Initializer, match) &&
    // MatchOptional(this.ExpressionBody, o.ExpressionBody, match)`. The terms are in
    // `MembersToMatch` order (the generator adds `Name`, `MatchAttributesAndModifiers`, and
    // `ReturnType` explicitly for every `EntityDeclaration`-derived node -- `NameToken` is NOT
    // `[ExcludeFromMatch]` on `PropertyDeclaration`, so the `Name` `String` term IS added, the
    // `EnumMemberDeclaration` D275 precedent; then the per-property scan adds
    // `PrivateImplementationType`/`Getter`/`Setter`/`Initializer`/`ExpressionBody`, each a
    // `MatchOptional` recursive term, in source declaration order). The `Name` term is a
    // `MatchString` over the `NameToken`'s name (the inherited base `Name()` kind-walks for the
    // `Identifier` kind and returns the `NameToken`'s `Name` -- `PropertyDeclaration` does NOT
    // override `Name`, so the inherited virtual returns the real name); the `ReturnType` term is
    // `MatchOptional` (nullable recursive -- the generator treats the `EntityDeclaration`
    // `ReturnType` uniformly as `MatchOptional`); the `PrivateImplementationType`/`Getter`/`Setter`/
    // `Initializer`/`ExpressionBody` terms are each `MatchOptional` over their nullable slots. A
    // type-only mismatch (not a `PropertyDeclaration`) rejects early. The `Name()` calls are
    // inlined in the `MatchString` arguments (the `MemberType` D238 / `EnumMemberDeclaration` D275
    // precedent) so the C# `&&` short-circuit is preserved; the `std::string` temporaries live
    // until the end of the full `return` expression, so the `std::string_view` views are valid for
    // the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<PropertyDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && MatchOptional(privateImplementationType_, o->privateImplementationType_, match)
            && MatchOptional(getter_, o->getter_, match)
            && MatchOptional(setter_, o->setter_, match)
            && MatchOptional(initializer_, o->initializer_, match)
            && MatchOptional(expressionBody_, o->expressionBody_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 precedent), the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `NameToken`
    // deep-cloned through the setter (which re-parents; `Identifier::Clone()` returns `Identifier*`,
    // which `NameToken(Identifier*)` accepts directly), the `ReturnType` deep-cloned through the
    // setter (which re-parents; `AstType::Clone()` returns `AstType*`, which `ReturnType(AstType*)`
    // accepts directly), every `Attributes` element deep-cloned through `Add` (which re-parents
    // and re-indexes; `AttributeSection::Clone()` returns `AttributeSection*`, which
    // `Add(AttributeSection*)` accepts directly), and the `PrivateImplementationType`/`Getter`/
    // `Setter`/`Initializer`/`ExpressionBody` deep-cloned through their setters (which re-parent;
    // `AstType::Clone()`/`Accessor::Clone()`/`Expression::Clone()` return the covariant concrete
    // types the setters accept directly). No own location fields (`StartLocation`/`EndLocation`
    // are the print-time base fields set by the unported output visitor -- `PropertyDeclaration`
    // does not derive `EndLocation`), so they are not copied (the `DestructorDeclaration` D272
    // no-location-copy precedent). The covariant return is `PropertyDeclaration*` (through
    // `AstNode*`, the `AstNode::Clone` virtual -- `EntityDeclaration` re-declares no typed `Clone`,
    // faithful to its empty hand-written partial; the covariant `PropertyDeclaration*` is a valid
    // override of `AstNode::Clone`).
    PropertyDeclaration* Clone() const override {
        auto* node = new PropertyDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (returnType_ != nullptr)
            node->ReturnType(returnType_->Clone());
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (privateImplementationType_ != nullptr)
            node->PrivateImplementationType(privateImplementationType_->Clone());
        if (getter_ != nullptr)
            node->Getter(getter_->Clone());
        if (setter_ != nullptr)
            node->Setter(setter_->Clone());
        if (initializer_ != nullptr)
            node->Initializer(initializer_->Clone());
        if (expressionBody_ != nullptr)
            node->ExpressionBody(expressionBody_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_` is the always-present collection member (empty until the
    // first `Add`, non-incremental); `returnType_`/`nameToken_` are null until set (REQUIRED slots --
    // `CheckInvariant` asserts they are filled); the other five are null until set (NULLABLE slots
    // -- their absence is invariant-valid). NO name shadowing (no member is named
    // `AttributeSection`/`AstType`/`Identifier`/`Accessor`/`Expression`), so the field types are the
    // plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstType* returnType_ = nullptr;
    Identifier* nameToken_ = nullptr;
    AstType* privateImplementationType_ = nullptr;
    Accessor* getter_ = nullptr;
    Accessor* setter_ = nullptr;
    Expression* initializer_ = nullptr;
    Expression* expressionBody_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_PROPERTYDECLARATION_HPP
