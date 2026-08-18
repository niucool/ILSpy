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

// Port of the `MethodDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/MethodDeclaration.cs (the generated
// `MethodDeclaration.g.cs` + the hand-written partial, which declares the `SymbolKind` override,
// the seven slot properties, and the `IsExtensionMethod` computed property -- no ctors, no helpers,
// no const strings). The next in-order Phase-5 piece per the D283 plan ("MethodDeclaration (now
// unblocked: Type + Identifier + PrivateImplementationType + TypeParameters [TypeParameterDeclaration
// D282] + Parameters [ParameterDeclaration D278] + Constraints [Constraint just ported] + Body)").
//
// `method_declaration ::= attribute_section* modifier* type ( type '.' )? identifier type_parameter*
// '(' parameter* ')' constraint* ( block | ';' )` (C# grammar 15.6.1): a sealed `EntityDeclaration`
// (the `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final` -- no pattern
// placeholder). Eight `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the method (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's FIRST of FOUR
//     collections, so `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex ==
//     slots.Count - 1` is false -- `collectionCount` is 4): every `Add`/`Insert`/`Remove`/
//     single-slot-set INVALIDATES the parent's indices for a lazy `EnsureChildIndices` rebuild
//     (the `ComposedType` D242 two-collection / `IndexerDeclaration` D279 / `OperatorDeclaration`
//     D280 / `ConstructorDeclaration` D281 precedent, generalized to four collections -- the first
//     ported node with more than two collections). Overrides the base `EntityDeclaration::Attributes`
//     (the base body returns a detached empty via `GetChildren`; this override returns the real
//     `attributes_` member).
//   * `[Slot("Type")] public override partial AstType ReturnType` -- the method's declared return
//     type (a single REQUIRED `AstType` slot at slot 1, non-nullable; reusing `Slots::Type`). The
//     slot FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode(ref field, value)` (the dynamic flattened index after a collection; the
//     `ComposedType.BaseType` D242 / `IndexerDeclaration` D279 / `OperatorDeclaration` D280
//     precedent). Overrides the base `EntityDeclaration::ReturnType` (the base body kind-walks for
//     the `Type` kind; this override returns the backing field directly, the generated
//     `get => field!`).
//   * `[Slot("PrivateImplementationType")] public partial AstType? PrivateImplementationType` --
//     the explicit-interface-implementation type (a single NULLABLE `AstType?` slot at slot 2, e.g.
//     the `I` in `int I.M(...)`; null when the method is not an explicit interface implementation).
//     The slot FOLLOWS the `Attributes` collection, so the setter uses the INDEX-LESS
//     `SetChildNode`. Reusing the `Slots::PrivateImplementationType` kind (added by
//     `PropertyDeclaration` D276). NOT an override, so a plain non-virtual accessor.
//   * `[Slot("Identifier")] public override partial Identifier NameToken` -- the method's name
//     token (a single REQUIRED `Identifier` slot at slot 3, non-nullable; reusing `Slots::Identifier`).
//     The slot FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode`. NOT `[ExcludeFromMatch]` (unlike `ConstructorDeclaration` D281 / 
//     `DestructorDeclaration` D272), so the `Name` `MatchString` term IS in `DoMatch` (the
//     `FieldDeclaration` D273 / `IndexerDeclaration` D279 / `EnumMemberDeclaration` D275 / 
//     `OperatorDeclaration` D280 precedent -- a method's name is a real name the resolver looks up
//     by). Overrides the base `EntityDeclaration::NameToken` (the base body kind-walks for the
//     `Identifier` kind; this override returns the backing field directly, the generated
//     `get => field!`). The inherited base `EntityDeclaration::Name` (NOT overridden -- the C#
//     declares no `Name` override) kind-walks for the `Identifier` kind and returns the
//     `NameToken`'s `Name`, so the `DoMatch` `MatchString` term matches the two methods' names.
//   * `[Slot("TypeParameter")] public partial AstNodeCollection<TypeParameterDeclaration>
//     TypeParameters` -- the method's generic type parameters (a COLLECTION at slot 4, the
//     `<T, U, ...>` of a generic method; reusing the cycle-broken `Slots::TypeParameter` kind,
//     added this iteration into `TypeParameterDeclaration.hpp` after the class). The collection is
//     the node's SECOND of FOUR collections, so `supportsIncremental` is FALSE; `baseIndex` is 4
//     (the slot index; the generator passes the slot index, not the dynamic flattened index --
//     unused on the non-incremental fast path). NOT a base virtual, so a plain non-virtual accessor.
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> Parameters` --
//     the method's parameter list (a COLLECTION at slot 5, the `( parameter* )`; reusing the
//     cycle-broken `Slots::Parameter` kind added by `IndexerDeclaration` D279). The collection is
//     the node's THIRD of FOUR collections, so `supportsIncremental` is FALSE; `baseIndex` is 5.
//     NOT a base virtual, so a plain non-virtual accessor.
//   * `[Slot("Constraint")] public partial AstNodeCollection<Constraint> Constraints` -- the
//     method's generic `where` clauses (a COLLECTION at slot 6, the `where T : ...` clauses of a
//     generic method; reusing the cycle-broken `Slots::Constraint` kind, added this iteration into
//     `Constraint.hpp` after the class). The collection is the node's FOURTH of FOUR collections,
//     so `supportsIncremental` is FALSE; `baseIndex` is 6. NOT a base virtual, so a plain
//     non-virtual accessor.
//   * `[Slot("Body")] public partial BlockStatement? Body` -- the method body (a single NULLABLE
//     `BlockStatement?` slot at slot 7; null for an abstract method, an interface method
//     declaration, or an extern method). The slot FOLLOWS the `Constraints` collection, so the
//     property setter uses the INDEX-LESS `SetChildNode`. Reusing the `Slots::Body` kind
//     (cycle-broken into `BlockStatement.hpp` by `CheckedStatement` D260). NOT an override, so a
//     plain non-virtual accessor.
//
// The hand-written `IsExtensionMethod` computed property: `ParameterDeclaration? pd =
// GetChild(Slots.Parameter); return pd != null && pd.HasThisModifier;` -- a method is an extension
// method iff its FIRST parameter carries the `this` modifier. `GetChild(Slots.Parameter)` is the
// kind-based `GetChild` (the D271 `GetChildByKind<T>` on `AstNode`), which walks the slot storage
// for the first child whose slot's `Kind()` is `Slots::Parameter` -- the first `Parameters` element
// (null when `Parameters` is empty). So `IsExtensionMethod` reads the first parameter's
// `HasThisModifier` (the D278 `ParameterDeclaration` scalar). Ports as a plain `bool` getter
// calling `GetChildByKind<ParameterDeclaration>(&Slots::Parameter)`.
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]`, so the `Name` term IS
// added, the `FieldDeclaration` D273 / `OperatorDeclaration` D280 precedent),
// `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every `EntityDeclaration`-derived
// node, then the per-property scan adds the non-override non-`[ExcludeFromMatch]` `[Slot]` children
// in source declaration order (`PrivateImplementationType`, `TypeParameters`, `Parameters`,
// `Constraints`, `Body`). So `MembersToMatch` is `[Name, MatchAttributesAndModifiers, ReturnType,
// PrivateImplementationType, TypeParameters, Parameters, Constraints, Body]`, and the generated
// `DoMatch` is `return other is MethodDeclaration o && MatchString(this.Name, o.Name) &&
// this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType, match)
// && MatchOptional(this.PrivateImplementationType, o.PrivateImplementationType, match) &&
// this.TypeParameters.DoMatch(o.TypeParameters, match) && this.Parameters.DoMatch(o.Parameters,
// match) && this.Constraints.DoMatch(o.Constraints, match) && MatchOptional(this.Body, o.Body,
// match)`. The `Name` term is a `MatchString` over the method's name (the inherited base `Name()`
// kind-walks for the `Identifier` kind and returns the `NameToken`'s `Name`); the `ReturnType` term
// is `MatchOptional` (nullable recursive -- the generator treats the `EntityDeclaration` `ReturnType`
// uniformly as `MatchOptional`, even though the slot is required here, the `OperatorDeclaration`
// D280 precedent); the `PrivateImplementationType`/`Body` terms are each `MatchOptional` over their
// nullable slots; the `TypeParameters`/`Parameters`/`Constraints` terms are each the collection
// recursive `DoMatch` (the generator emits a collection-typed recursive term directly, NOT
// `MatchOptional` -- the `FieldDeclaration.Variables` D273 / `IndexerDeclaration.Parameters` D279
// precedent). A type-only mismatch (not a `MethodDeclaration`) rejects early. The `Name()` calls are
// inlined in the `MatchString` arguments (the `MemberType` D238 / `OperatorDeclaration` D280
// precedent) so the C# `&&` short-circuit is preserved; the `std::string` temporaries live until
// the end of the full `return` expression, keeping the `std::string_view` views valid for the
// `MatchString` call.
//
// The generated ctors (`WriteConstructors`): `CtorParams` in declaration order is `[Attributes
// (collection, optional), ReturnType (required), PrivateImplementationType (single, optional),
// NameToken (required), TypeParameters (collection, optional), Parameters (collection, optional),
// Constraints (collection, optional), Body (single, optional)]` -- the `Modifiers` scalar lives on
// the `EntityDeclaration` base and is NOT a declared member of `MethodDeclaration`, so `GetMembers()`
// does not add it (the `DestructorDeclaration` D272 / `OperatorDeclaration` D280 / `ConstructorDeclaration`
// D281 precedent). `RequiredConstructorPrefixLength` is 4 (through the last non-optional param
// `NameToken` at index 3 -- the generator walks the whole `ctorParams` list and takes the last
// non-optional index + 1, the `CatchClause` D269 / `PropertyDeclaration` D276 / `ConstructorDeclaration`
// D281 precedent). `ConstructorPrefixLengths` is `{4, 8}`. The (len=4) ctor
// `(IEnumerable<AttributeSection>, AstType, AstType?, Identifier)` and the (len=8) ctor both call
// `this.Attributes.AddRange(...)` for the `Attributes` collection (and the (len=8) ctor also calls
// `this.TypeParameters.AddRange(...)`, `this.Parameters.AddRange(...)`, and
// `this.Constraints.AddRange(...)` for the three further collections -- all `AddRange` conveniences
// are the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. A
// `MethodDeclaration` is built via the empty ctor + `ReturnType(...)` + `PrivateImplementationType(...)`
// + `NameToken(...)` + `TypeParameters().Add(...)` + `Parameters().Add(...)` + `Constraints().Add(...)`
// + `Body(...)` + `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitMethodDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitMethodDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required),
// `PrivateImplementationTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at
// `Slots.PrivateImplementationType`, nullable), `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
// pointing at `Slots.Identifier`, required), `TypeParametersSlot` (a
// `CSharpSlotInfoT<TypeParameterDeclaration>` pointing at `Slots.TypeParameter`, collection),
// `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
// collection), `ConstraintsSlot` (a `CSharpSlotInfoT<Constraint>` pointing at `Slots.Constraint`,
// collection), and `BodySlot` (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`,
// nullable). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
// `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the derived
// `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the public surface,
// the `DestructorDeclaration` D272 precedent), the annotation channel copied (`CloneAnnotationsFrom`
// + `ReparentTrivia`, the D223 concrete-clone pattern), the `ReturnType`/`PrivateImplementationType`/
// `NameToken`/`Body` deep-cloned through their setters when present (which re-parent;
// `AstType::Clone()` returns `AstType*`, `Identifier::Clone()` returns `Identifier*`,
// `BlockStatement::Clone()` returns `BlockStatement*` -- the covariant concrete types the setters
// accept directly), and every `Attributes`/`TypeParameters`/`Parameters`/`Constraints` element
// deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()` returns
// `AttributeSection*`, `TypeParameterDeclaration::Clone()` returns `TypeParameterDeclaration*`,
// `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`, `Constraint::Clone()` returns
// `Constraint*` -- the covariant concrete types `Add` accepts directly). No own location fields
// (does not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied
// (the `DestructorDeclaration` D272 / `OperatorDeclaration` D280 / `ConstructorDeclaration` D281
// no-location-copy precedent). The covariant return is `MethodDeclaration*` (through `AstNode*`,
// the `AstNode::Clone` virtual -- `EntityDeclaration` re-declares no typed `Clone`, faithful to its
// empty hand-written partial).
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`AstType`/`Identifier`/
// `TypeParameterDeclaration`/`ParameterDeclaration`/`Constraint`/`BlockStatement` -- the
// `Attributes()`/`ReturnType()`/`PrivateImplementationType()`/`NameToken()`/`TypeParameters()`/
// `Parameters()`/`Constraints()`/`Body()` accessors do not collide with any class in the `Syntax`
// namespace), so no elaborated-type-specifier is needed anywhere; the plain element types resolve
// to the classes. This is the first `EntityDeclaration` with MORE than two collections -- the
// `ConstructorDeclaration` D281 / `OperatorDeclaration` D280 two-collection shape with two more
// collections (`TypeParameters`/`Constraints`) inserted before the trailing `Body` single, and the
// `FieldDeclaration` D273 / `EventDeclaration` D277 / `CustomEventDeclaration` D277 collection ->
// single -> collection shape generalized to a four-collection + four-single layout.
//
// Two NEW `Slots` constants this iteration, both cycle-broken into their element-type headers
// (the `Slots::Attribute` D241 / `Slots::AttributeSection` D242 / `Slots::Parameter` D279 /
// `Slots::Variable` D267 / `Slots::ConstructorInitializer` D281 cycle-breaking precedent):
// `Slots::TypeParameter` (a `CSharpSlotInfoT<TypeParameterDeclaration>` collection kind) lives in
// `TypeParameterDeclaration.hpp` after the `TypeParameterDeclaration` class
// (`TypeParameterDeclaration.hpp` includes `Slots.hpp` for its per-node `AttributesSlot`/
// `NameTokenSlot` referencing `&Slots::AttributeSection`/`&Slots::Identifier`, so the kind cannot
// live in `Slots.hpp` -- a circular include), and `Slots::Constraint` (a
// `CSharpSlotInfoT<Constraint>` collection kind) lives in `Constraint.hpp` after the `Constraint`
// class (`Constraint.hpp` includes `Slots.hpp` for its per-node `TypeParameterSlot`/`BaseTypesSlot`
// referencing `&Slots::ConstraintTypeParameter`/`&Slots::BaseType`, so the kind cannot live in
// `Slots.hpp`). Both kind names (`TypeParameter`/`Constraint`) collide with no class in the `Syntax`
// namespace (there is `TypeParameterDeclaration`, not `TypeParameter`; there is `Constraint` the
// class, but the kind name `Constraint` is in the `Slots` namespace, distinct from the class in the
// parent `Syntax` namespace -- the `Slots::Variable` D267 no-collision precedent where the kind
// name matches no class), so no elaborated-type-specifier is needed. `Slots.hpp` itself is
// unchanged (both new kinds are cycle-broken). The other six kinds (`Slots::AttributeSection`,
// `Slots::Type`, `Slots::PrivateImplementationType`, `Slots::Identifier`, `Slots::Parameter`,
// `Slots::Body`) are all already ported across D241/D240/D276/D237/D279/D260.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_METHODDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_METHODDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class MethodDeclaration : EntityDeclaration`. `final` (the C#
// `sealed`): no further derivation. The first `EntityDeclaration` with more than two collections
// (four: `Attributes`/`TypeParameters`/`Parameters`/`Constraints`) plus four singles
// (`ReturnType`/`PrivateImplementationType`/`NameToken`/`Body`).
class MethodDeclaration final : public EntityDeclaration {
public:
    ~MethodDeclaration() override = default;

    // The generated empty ctor (the C# `public MethodDeclaration()`). The four collections are
    // members (the D222 always-present-stack-member design), initialized here with their slot
    // indices as `baseIndex` (`Attributes` at 0, `TypeParameters` at 4, `Parameters` at 5,
    // `Constraints` at 6 -- the generator passes the slot index, not the dynamic flattened index;
    // unused on the non-incremental fast path) and `supportsIncremental = false` for all four (the
    // node has four collections, so none is incremental -- `collectionCount == 1 && slotIndex ==
    // slots.Count - 1` is false for every collection -- an element's flattened `ChildIndex` is
    // dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation, the `ComposedType` D242
    // four-collection-precedent generalized). The four singles default to null; `ReturnType` and
    // `NameToken` are REQUIRED slots (a default-constructed node violates their required-slot
    // invariant -- `CheckInvariant` rejects an empty node, the `UnaryOperatorExpression` D231
    // precedent), `PrivateImplementationType`/`Body` are nullable so their absence is
    // invariant-valid.
    MethodDeclaration() : attributes_(this, &AttributesSlot, 0, false),
                          typeParameters_(this, &TypeParametersSlot, 4, false),
                          parameters_(this, &ParametersSlot, 5, false),
                          constraints_(this, &ConstraintsSlot, 6, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Method; } }` --
    // the kind of member this declaration is (a method). Overrides the base abstract `SymbolKind`
    // (the `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Method` avoids a `using`
    // (the enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Method;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the method (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental -- the node's first of
    // four collections). The C# lazily allocates the wrapper; the D222 port makes the collection an
    // always-present stack member, so the accessor returns the member directly. Overrides the base
    // `EntityDeclaration::Attributes` (the base body returns a detached empty via `GetChildren`;
    // this override returns the real `attributes_` member). A `const` convenience overload returns
    // `const&` for a `const MethodDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `ReturnType` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Type")] public override partial AstType ReturnType` -- a single
    // REQUIRED `AstType` slot at slot 1 (the method's declared return type; non-nullable, so
    // required -- `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the
    // property setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened
    // index after a collection). Overrides the base `EntityDeclaration::ReturnType` (the base body
    // kind-walks for the `Type` kind; this override returns the backing field directly, the
    // generated `get => field!`). The getter returns the raw pointer (null for a half-constructed
    // node; the C# `!` null-forgiving).
    AstType* ReturnType() const override { return returnType_; }
    void ReturnType(AstType* value) override {
        SetChildNode(returnType_, value);
    }

    // ---- The `PrivateImplementationType` slot (a NULLABLE single `AstType`, NOT a base virtual) -
    // The generated `[Slot("PrivateImplementationType")] public partial AstType?
    // PrivateImplementationType` -- a single NULLABLE `AstType?` slot at slot 2 (the
    // explicit-interface-implementation type, e.g. the `I` in `int I.M(...)`; null when the method
    // is not an explicit interface implementation). The slot FOLLOWS the `Attributes` collection,
    // so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT an override
    // (the `EntityDeclaration` base declares no `PrivateImplementationType` virtual), so a plain
    // non-virtual accessor. `CheckInvariant` passes without a `PrivateImplementationType` (the
    // slot is nullable). Reusing the `Slots::PrivateImplementationType` kind (added by
    // `PropertyDeclaration` D276).
    AstType* PrivateImplementationType() const { return privateImplementationType_; }
    void PrivateImplementationType(AstType* value) {
        SetChildNode(privateImplementationType_, value);
    }

    // ---- The `NameToken` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Identifier")] public override partial Identifier NameToken` -- a
    // single REQUIRED `Identifier` slot at slot 3 (the method's name token; non-nullable, so
    // required -- `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the
    // property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT
    // `[ExcludeFromMatch]` (unlike `ConstructorDeclaration` D281 / `DestructorDeclaration` D272),
    // so the `Name` `MatchString` term IS in `DoMatch` (a method's name is a real name the resolver
    // looks up by). Overrides the base `EntityDeclaration::NameToken` (the base body kind-walks
    // for the `Identifier` kind; this override returns the backing field directly, the generated
    // `get => field!`). The inherited base `EntityDeclaration::Name` (NOT overridden) kind-walks
    // for the `Identifier` kind and returns the `NameToken`'s `Name`, so the `DoMatch` `MatchString`
    // term matches the two methods' names without a `Name` override. The getter returns the raw
    // pointer (null for a half-constructed node; the C# `!` null-forgiving).
    Identifier* NameToken() const override { return nameToken_; }
    void NameToken(Identifier* value) override {
        SetChildNode(nameToken_, value);
    }

    // ---- The `TypeParameters` collection slot (NOT a base virtual) --------------------------
    // The generated `[Slot("TypeParameter")] public partial AstNodeCollection<TypeParameterDeclaration>
    // TypeParameters` -- the method's generic type parameters (a `CSharpSlotInfoT<TypeParameterDeclaration>`
    // slot at slot 4, the node's SECOND of FOUR collections, non-incremental). The C# lazily
    // allocates the wrapper; the D222 port makes the collection an always-present stack member, so
    // the accessor returns the member directly. NOT a base virtual, so a plain non-virtual
    // accessor. Reuses the cycle-broken `Slots::TypeParameter` kind (added this iteration into
    // `TypeParameterDeclaration.hpp`). A `const` convenience overload returns `const&` for a
    // `const MethodDeclaration*`.
    AstNodeCollectionT<TypeParameterDeclaration>& TypeParameters() { return typeParameters_; }
    const AstNodeCollectionT<TypeParameterDeclaration>& TypeParameters() const { return typeParameters_; }

    // ---- The `Parameters` collection slot (NOT a base virtual) -----------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the method's parameter list (a `CSharpSlotInfoT<ParameterDeclaration>` slot at
    // slot 5, the node's THIRD of FOUR collections, non-incremental). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly. NOT a base virtual, so a plain non-virtual accessor. Reuses the
    // cycle-broken `Slots::Parameter` kind (added by `IndexerDeclaration` D279). A `const`
    // convenience overload returns `const&` for a `const MethodDeclaration*`.
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The `Constraints` collection slot (NOT a base virtual) ---------------------------
    // The generated `[Slot("Constraint")] public partial AstNodeCollection<Constraint> Constraints`
    // -- the method's generic `where` clauses (a `CSharpSlotInfoT<Constraint>` slot at slot 6, the
    // node's FOURTH of FOUR collections, non-incremental). The C# lazily allocates the wrapper;
    // the D222 port makes the collection an always-present stack member, so the accessor returns
    // the member directly. NOT a base virtual, so a plain non-virtual accessor. Reuses the
    // cycle-broken `Slots::Constraint` kind (added this iteration into `Constraint.hpp`). A `const`
    // convenience overload returns `const&` for a `const MethodDeclaration*`.
    AstNodeCollectionT<Constraint>& Constraints() { return constraints_; }
    const AstNodeCollectionT<Constraint>& Constraints() const { return constraints_; }

    // ---- The `Body` slot (a NULLABLE single `BlockStatement`, NOT a base virtual) ----------
    // The generated `[Slot("Body")] public partial BlockStatement? Body` -- a single NULLABLE
    // `BlockStatement?` slot at slot 7 (the method body; null for an abstract method, an interface
    // method declaration, or an extern method). The slot FOLLOWS the `Constraints` collection, so
    // the property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT an override,
    // so a plain non-virtual accessor. `CheckInvariant` passes without a `Body` (the slot is
    // nullable). Reusing the `Slots::Body` kind (cycle-broken into `BlockStatement.hpp` by
    // `CheckedStatement` D260).
    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value);
    }

    // ---- The `IsExtensionMethod` computed property -----------------------------------------
    // The C# `public bool IsExtensionMethod { get { ParameterDeclaration? pd =
    // GetChild(Slots.Parameter); return pd != null && pd.HasThisModifier; } }` -- a method is an
    // extension method iff its FIRST parameter carries the `this` modifier. `GetChild(Slots.
    // Parameter)` is the kind-based `GetChild` (the D271 `GetChildByKind<T>` on `AstNode`), which
    // walks the slot storage for the first child whose slot's `Kind()` is `Slots::Parameter` --
    // the first `Parameters` element (null when `Parameters` is empty). So the getter reads the
    // first parameter's `HasThisModifier` (the D278 `ParameterDeclaration` scalar). NOT in
    // `MembersToMatch`/`DoMatch` (a computed property, not a settable instance property -- the
    // generator's scan adds only settable instance properties).
    bool IsExtensionMethod() const {
        ParameterDeclaration* pd = GetChildByKind<ParameterDeclaration>(&Slots::Parameter);
        return pd != nullptr && pd->HasThisModifier();
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's first collection); `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>`
    // pointing at `Slots.Type`, required); `PrivateImplementationTypeSlot` (a
    // `CSharpSlotInfoT<AstType>` pointing at `Slots.PrivateImplementationType`, nullable);
    // `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // required); `TypeParametersSlot` (a `CSharpSlotInfoT<TypeParameterDeclaration>` pointing at
    // `Slots.TypeParameter`, collection -- the node's second collection); `ParametersSlot` (a
    // `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection -- the
    // node's third collection); `ConstraintsSlot` (a `CSharpSlotInfoT<Constraint>` pointing at
    // `Slots.Constraint`, collection -- the node's fourth collection); `BodySlot` (a
    // `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`, nullable). NO name shadowing (no
    // member is named `AttributeSection`/`AstType`/`Identifier`/`TypeParameterDeclaration`/
    // `ParameterDeclaration`/`Constraint`/`BlockStatement`), so the element types are the plain
    // classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> ReturnTypeSlot{"ReturnType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<AstType> PrivateImplementationTypeSlot{"PrivateImplementationType", false, &Slots::PrivateImplementationType, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<TypeParameterDeclaration> TypeParametersSlot{"TypeParameters", true, &Slots::TypeParameter, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<Constraint> ConstraintsSlot{"Constraints", true, &Slots::Constraint, true};
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitMethodDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitMethodDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Eight slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `ReturnType` single at slot 1 (`attrCount`), a
    // `PrivateImplementationType` single at slot 2 (`attrCount + 1`), a `NameToken` single at slot
    // 3 (`attrCount + 2`), a `TypeParameters` collection at slot 4 (the contiguous range
    // `[attrCount + 3, attrCount + 3 + tpCount)`), a `Parameters` collection at slot 5 (the
    // contiguous range `[attrCount + 3 + tpCount, attrCount + 3 + tpCount + pCount)`), a
    // `Constraints` collection at slot 6 (the contiguous range
    // `[attrCount + 3 + tpCount + pCount, attrCount + 3 + tpCount + pCount + cCount)`), and a
    // `Body` single at slot 7 (`attrCount + 3 + tpCount + pCount + cCount`). `GetChildCount` is
    // `attrCount + tpCount + pCount + cCount + 4` (the four collections' current lengths plus the
    // four singles -- each single slot contributes 1 to the flattened count regardless of whether
    // it is filled); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's
    // width from a running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a collection step, three single steps, three
    // collection steps, then a single step). `GetCollectionByKind` returns the matching collection
    // for each of the four collection kinds. This is the first ported node with FOUR collections --
    // the `ConstructorDeclaration` D281 / `OperatorDeclaration` D280 two-collection shape with
    // two more collections (`TypeParameters`/`Constraints`) inserted before the trailing `Body`
    // single.

    int GetChildCount() const override {
        return attributes_.Count() + typeParameters_.Count() + parameters_.Count()
             + constraints_.Count() + 4;
    }

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
        if (i == 0)
            return nameToken_;
        i--;
        {
            int n = typeParameters_.Count();
            if (i < n)
                return typeParameters_.At(i);
            i -= n;
        }
        {
            int n = parameters_.Count();
            if (i < n)
                return parameters_.At(i);
            i -= n;
        }
        {
            int n = constraints_.Count();
            if (i < n)
                return constraints_.At(i);
            i -= n;
        }
        if (i == 0)
            return body_;
        throw std::out_of_range("MethodDeclaration::GetChild");
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
        if (i == 0) {
            SetChildNode(nameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        i--;
        {
            int n = typeParameters_.Count();
            if (i < n) {
                typeParameters_.SetAt(i, static_cast<TypeParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = parameters_.Count();
            if (i < n) {
                parameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = constraints_.Count();
            if (i < n) {
                constraints_.SetAt(i, static_cast<Constraint*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(body_, static_cast<BlockStatement*>(value), index);
            return;
        }
        throw std::out_of_range("MethodDeclaration::SetChild");
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
        if (i == 0)
            return &NameTokenSlot;
        i--;
        {
            int n = typeParameters_.Count();
            if (i < n)
                return &TypeParametersSlot;
            i -= n;
        }
        {
            int n = parameters_.Count();
            if (i < n)
                return &ParametersSlot;
            i -= n;
        }
        {
            int n = constraints_.Count();
            if (i < n)
                return &ConstraintsSlot;
            i -= n;
        }
        if (i == 0)
            return &BodySlot;
        throw std::out_of_range("MethodDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::TypeParameter)
            return &typeParameters_;
        if (kind == &Slots::Parameter)
            return &parameters_;
        if (kind == &Slots::Constraint)
            return &constraints_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is MethodDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && MatchOptional(this.PrivateImplementationType, o.PrivateImplementationType, match)
    // && this.TypeParameters.DoMatch(o.TypeParameters, match) && this.Parameters.DoMatch(o.
    // Parameters, match) && this.Constraints.DoMatch(o.Constraints, match) && MatchOptional(this.
    // Body, o.Body, match)`. The terms are in `MembersToMatch` order (the generator adds `Name`,
    // `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every `EntityDeclaration`-
    // derived node -- `NameToken` is NOT `[ExcludeFromMatch]` on `MethodDeclaration`, so the
    // `Name` `String` term IS added, the `FieldDeclaration` D273 / `OperatorDeclaration` D280
    // precedent; then the per-property scan adds `PrivateImplementationType`/`TypeParameters`/
    // `Parameters`/`Constraints`/`Body` in source declaration order). The `Name` term is a
    // `MatchString` over the method's name (the inherited base `Name()` kind-walks for the
    // `Identifier` kind and returns the `NameToken`'s `Name`); the `ReturnType` term is
    // `MatchOptional` (nullable recursive -- the generator treats the `EntityDeclaration`
    // `ReturnType` uniformly as `MatchOptional`, even though the slot is required here); the
    // `PrivateImplementationType`/`Body` terms are each `MatchOptional` over their nullable slots;
    // the `TypeParameters`/`Parameters`/`Constraints` terms are each the collection recursive
    // `DoMatch` (the generator emits a collection-typed recursive term directly, NOT
    // `MatchOptional` -- the `FieldDeclaration.Variables` D273 / `IndexerDeclaration.Parameters`
    // D279 precedent). A type-only mismatch (not a `MethodDeclaration`) rejects early. The
    // `Name()` calls are inlined in the `MatchString` arguments (the `MemberType` D238 /
    // `OperatorDeclaration` D280 precedent) so the C# `&&` short-circuit is preserved; the
    // `std::string` temporaries live until the end of the full `return` expression, keeping the
    // `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<MethodDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(returnType_, o->returnType_, match)
            && MatchOptional(privateImplementationType_, o->privateImplementationType_, match)
            && typeParameters_.DoMatch(o->typeParameters_, match)
            && parameters_.DoMatch(o->parameters_, match)
            && constraints_.DoMatch(o->constraints_, match)
            && MatchOptional(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 precedent), the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `ReturnType`/`PrivateImplementationType`/`NameToken`/`Body` deep-cloned through their
    // setters when present (which re-parent; `AstType::Clone()` returns `AstType*` which
    // `ReturnType(AstType*)`/`PrivateImplementationType(AstType*)` accept directly;
    // `Identifier::Clone()` returns `Identifier*` which `NameToken(Identifier*)` accepts directly;
    // `BlockStatement::Clone()` returns `BlockStatement*` which `Body(BlockStatement*)` accepts
    // directly), and every `Attributes`/`TypeParameters`/`Parameters`/`Constraints` element
    // deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()`
    // returns `AttributeSection*`, `TypeParameterDeclaration::Clone()` returns
    // `TypeParameterDeclaration*`, `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`,
    // `Constraint::Clone()` returns `Constraint*` -- the covariant concrete types `Add` accepts
    // directly). No own location fields (does not derive `EndLocation`), so the print-time
    // `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
    // `OperatorDeclaration` D280 / `ConstructorDeclaration` D281 no-location-copy precedent).
    // The covariant return is `MethodDeclaration*` (through `AstNode*`, the `AstNode::Clone`
    // virtual -- `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty
    // hand-written partial; the covariant `MethodDeclaration*` is a valid override of
    // `AstNode::Clone`).
    MethodDeclaration* Clone() const override {
        auto* node = new MethodDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (returnType_ != nullptr)
            node->ReturnType(returnType_->Clone());
        if (privateImplementationType_ != nullptr)
            node->PrivateImplementationType(privateImplementationType_->Clone());
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        for (int i = 0; i < typeParameters_.Count(); i++)
            node->typeParameters_.Add(typeParameters_.At(i)->Clone());
        for (int i = 0; i < parameters_.Count(); i++)
            node->parameters_.Add(parameters_.At(i)->Clone());
        for (int i = 0; i < constraints_.Count(); i++)
            node->constraints_.Add(constraints_.At(i)->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_`/`typeParameters_`/`parameters_`/`constraints_` are the
    // always-present collection members (empty until the first `Add`, all non-incremental -- the
    // node has four collections); `returnType_`/`nameToken_` are null until set (REQUIRED slots --
    // `CheckInvariant` asserts they are filled); `privateImplementationType_`/`body_` are null
    // until set (NULLABLE slots -- their absence is invariant-valid). NO name shadowing (no member
    // is named `AttributeSection`/`AstType`/`Identifier`/`TypeParameterDeclaration`/
    // `ParameterDeclaration`/`Constraint`/`BlockStatement`), so the field types are the plain
    // classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstType* returnType_ = nullptr;
    AstType* privateImplementationType_ = nullptr;
    Identifier* nameToken_ = nullptr;
    AstNodeCollectionT<TypeParameterDeclaration> typeParameters_;
    AstNodeCollectionT<ParameterDeclaration> parameters_;
    AstNodeCollectionT<Constraint> constraints_;
    BlockStatement* body_ = nullptr;
};

// The `MethodDeclaration` kind -- a single REQUIRED `MethodDeclaration` child (the wrapped
// method declaration of a `LocalFunctionDeclarationStatement.Declaration`, the
// `local_function_declaration ::= method_declaration` production). Unique to
// `LocalFunctionDeclarationStatement` among the ported nodes. A
// `CSharpSlotInfoT<MethodDeclaration>` (the element type is the concrete `MethodDeclaration`
// node). Defined HERE (in MethodDeclaration.hpp, after the `MethodDeclaration` class) for the
// cycle-breaking reason: MethodDeclaration.hpp includes Slots.hpp for its own per-node slot
// statics (the `ReturnTypeSlot`/`NameTokenSlot`/... referencing `&Slots::Type`/`&Slots::Identifier`/
// ...), so a `CSharpSlotInfoT<MethodDeclaration>` kind cannot live in Slots.hpp -- a circular
// include -- and is defined here where both `CSharpSlotInfoT` (visible via the Slots.hpp include)
// and `MethodDeclaration` (the class just defined above) are complete (the `Slots::Attribute`
// D241 / `Slots::Body` D260 / `Slots::Variable` D267 precedent). The `inline` variable has
// external linkage and one address across translation units (the C++17 `inline` guarantee),
// preserving the pointer-identity comparison `node.Slot.Kind == &Slots::MethodDeclaration` the
// slot system relies on. The kind name `MethodDeclaration` collides with the `MethodDeclaration`
// CLASS in the enclosing `Syntax` namespace, but the variable being declared is not in scope for
// its own declarator's type, so the template argument `MethodDeclaration` resolves to the class
// (the `Slots::Attribute` D241 / `Slots::Constraint` D283 same-name-kind precedent); no
// elaborated-type-specifier is needed. The shared constant is constructed
// non-collection/non-optional (`{"MethodDeclaration", false, nullptr, false}`); the per-node
// `DeclarationSlot` on `LocalFunctionDeclarationStatement` carries the `IsOptional=false` flag
// (the `Declaration` is a required slot).
namespace Slots {
inline const CSharpSlotInfoT<MethodDeclaration> MethodDeclaration{"MethodDeclaration", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_METHODDECLARATION_HPP
