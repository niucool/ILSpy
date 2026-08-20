// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to copy, modify, merge,
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

// Port of the `DelegateDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/DelegateDeclaration.cs (the generated
// `DelegateDeclaration.g.cs` + the hand-written partial, which declares only the `SymbolKind`
// override and the six slot properties -- no ctors, no helpers, no const strings). The next
// in-order Phase-5 piece per the D286 plan (the remaining GeneralScope `EntityDeclaration`
// whose dependencies are all ported: `TypeParameterDeclaration` D282 + `ParameterDeclaration`
// D278 + `Constraint` D283 + `AstType` D236 + `Identifier` D227 + `AttributeSection` D241).
//
// `delegate_declaration ::= attribute_section* modifier* 'delegate' ( 'ref' 'readonly'? )? type
// identifier type_parameter* '(' parameter* ')' constraint*` (C# grammar 21.2): a sealed
// `EntityDeclaration` (the `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final`
// -- no pattern placeholder). Six `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the delegate (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's FIRST of FOUR
//     collections, so `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex ==
//     slots.Count - 1` is false -- `collectionCount` is 4): every `Add`/`Insert`/`Remove`/
//     single-slot-set INVALIDATES the parent's indices for a lazy `EnsureChildIndices` rebuild
//     (the `MethodDeclaration` D284 four-collection precedent, with TWO trailing singles instead
//     of four). Overrides the base `EntityDeclaration::Attributes` (the base body returns a
//     detached empty via `GetChildren`; this override returns the real `attributes_` member).
//   * `[Slot("Type")] public override partial AstType ReturnType` -- the delegate's declared
//     return type (a single REQUIRED `AstType` slot at slot 1, non-nullable; reusing `Slots::Type`).
//     The slot FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode(ref field, value)` (the dynamic flattened index after a collection; the
//     `ComposedType.BaseType` D242 / `MethodDeclaration.ReturnType` D284 precedent). Overrides the
//     base `EntityDeclaration::ReturnType` (the base body kind-walks for the `Type` kind; this
//     override returns the backing field directly, the generated `get => field!`).
//   * `[Slot("Identifier")] public override partial Identifier NameToken` -- the delegate's name
//     token (a single REQUIRED `Identifier` slot at slot 2, non-nullable; reusing `Slots::Identifier`).
//     The slot FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode`. NOT `[ExcludeFromMatch]` (unlike `ConstructorDeclaration` D281 /
//     `DestructorDeclaration` D272), so the `Name` `MatchString` term IS in `DoMatch` (the
//     `FieldDeclaration` D273 / `MethodDeclaration` D284 precedent -- a delegate's name is a real
//     name the resolver looks up by). Overrides the base `EntityDeclaration::NameToken` (the base
//     body kind-walks for the `Identifier` kind; this override returns the backing field
//     directly, the generated `get => field!`). The inherited base `EntityDeclaration::Name`
//     (NOT overridden -- the C# declares no `Name` override) kind-walks for the `Identifier` kind
//     and returns the `NameToken`'s `Name`, so the `DoMatch` `MatchString` term matches the two
//     delegates' names.
//   * `[Slot("TypeParameter")] public partial AstNodeCollection<TypeParameterDeclaration>
//     TypeParameters` -- the delegate's generic type parameters (a COLLECTION at slot 3, the
//     `<T, U, ...>` of a generic delegate; reusing the cycle-broken `Slots::TypeParameter` kind,
//     added by `MethodDeclaration` D284 into `TypeParameterDeclaration.hpp` after the class). The
//     collection is the node's SECOND of FOUR collections, so `supportsIncremental` is FALSE;
//     `baseIndex` is 3 (the slot index; the generator passes the slot index, not the dynamic
//     flattened index -- unused on the non-incremental fast path). NOT a base virtual, so a plain
//     non-virtual accessor.
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> Parameters` --
//     the delegate's parameter list (a COLLECTION at slot 4, the `( parameter* )`; reusing the
//     cycle-broken `Slots::Parameter` kind added by `IndexerDeclaration` D279). The collection is
//     the node's THIRD of FOUR collections, so `supportsIncremental` is FALSE; `baseIndex` is 4.
//     NOT a base virtual, so a plain non-virtual accessor.
//   * `[Slot("Constraint")] public partial AstNodeCollection<Constraint> Constraints` -- the
//     delegate's generic `where` clauses (a COLLECTION at slot 5, the `where T : ...` clauses of a
//     generic delegate; reusing the cycle-broken `Slots::Constraint` kind added by `MethodDeclaration`
//     D284 into `Constraint.hpp` after the class). The collection is the node's FOURTH of FOUR
//     collections, so `supportsIncremental` is FALSE; `baseIndex` is 5. NOT a base virtual, so a
//     plain non-virtual accessor.
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]`, so the `Name` term IS
// added, the `MethodDeclaration` D284 precedent), `MatchAttributesAndModifiers`, and `ReturnType`
// explicitly for every `EntityDeclaration`-derived node, then the per-property scan adds the
// non-override non-`[ExcludeFromMatch]` `[Slot]` children in source declaration order
// (`TypeParameters`, `Parameters`, `Constraints` -- all collections). So `MembersToMatch` is
// `[Name, MatchAttributesAndModifiers, ReturnType, TypeParameters, Parameters, Constraints]`, and
// the generated `DoMatch` is `return other is DelegateDeclaration o && MatchString(this.Name,
// o.Name) && this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType,
// o.ReturnType, match) && this.TypeParameters.DoMatch(o.TypeParameters, match) &&
// this.Parameters.DoMatch(o.Parameters, match) && this.Constraints.DoMatch(o.Constraints, match)`.
// The `Name` term is a `MatchString` over the delegate's name (the inherited base `Name()`
// kind-walks for the `Identifier` kind and returns the `NameToken`'s `Name`); the `ReturnType` term
// is `MatchOptional` (nullable recursive -- the generator treats the `EntityDeclaration`
// `ReturnType` uniformly as `MatchOptional`, even though the slot is required here, the
// `OperatorDeclaration` D280 / `MethodDeclaration` D284 precedent); the `TypeParameters`/
// `Parameters`/`Constraints` terms are each the collection recursive `DoMatch` (the generator
// emits a collection-typed recursive term directly, NOT `MatchOptional` -- the
// `FieldDeclaration.Variables` D273 / `MethodDeclaration` D284 precedent). A type-only mismatch
// (not a `DelegateDeclaration`) rejects early. The `Name()` calls are inlined in the `MatchString`
// arguments (the `MemberType` D238 / `MethodDeclaration` D284 precedent) so the C# `&&`
// short-circuit is preserved; the `std::string` temporaries live until the end of the full
// `return` expression, keeping the `std::string_view` views valid for the `MatchString` call.
//
// The generated ctors (`WriteConstructors`): `CtorParams` in declaration order is `[Attributes
// (collection, optional), ReturnType (required), NameToken (required), TypeParameters
// (collection, optional), Parameters (collection, optional), Constraints (collection,
// optional)]` -- the `Modifiers` scalar lives on the `EntityDeclaration` base and is NOT a
// declared member of `DelegateDeclaration`, so `GetMembers()` does not add it (the
// `MethodDeclaration` D284 precedent). `RequiredConstructorPrefixLength` is 3 (through the last
// non-optional param `NameToken` at index 2 -- the generator walks the whole `ctorParams` list and
// takes the last non-optional index + 1, the `CatchClause` D269 / `MethodDeclaration` D284
// precedent). `ConstructorPrefixLengths` is `{3, 6}`. The (len=3) ctor
// `(IEnumerable<AttributeSection>, AstType, Identifier)` and the (len=6) ctor both call
// `this.Attributes.AddRange(...)` for the `Attributes` collection (and the (len=6) ctor also calls
// `this.TypeParameters.AddRange(...)`, `this.Parameters.AddRange(...)`, and
// `this.Constraints.AddRange(...)` for the three further collections -- all `AddRange` conveniences
// are the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. A
// `DelegateDeclaration` is built via the empty ctor + `ReturnType(...)` + `NameToken(...)` +
// `TypeParameters().Add(...)` + `Parameters().Add(...)` + `Constraints().Add(...)` +
// `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitDelegateDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitDelegateDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required),
// `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required),
// `TypeParametersSlot` (a `CSharpSlotInfoT<TypeParameterDeclaration>` pointing at
// `Slots.TypeParameter`, collection), `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>`
// pointing at `Slots.Parameter`, collection), and `ConstraintsSlot` (a
// `CSharpSlotInfoT<Constraint>` pointing at `Slots.Constraint`, collection). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// a fresh node, the `Modifiers` scalar copied via the public `Modifiers()` getter/setter (the
// base's private `modifiers_` is not accessible from the derived `Clone` -- the C#
// `MemberwiseClone` copies the private backing; the port uses the public surface, the
// `DestructorDeclaration` D272 / `MethodDeclaration` D284 precedent), the annotation channel
// copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
// `ReturnType`/`NameToken` deep-cloned through their setters when present (which re-parent;
// `AstType::Clone()` returns `AstType*` which `ReturnType(AstType*)` accepts directly;
// `Identifier::Clone()` returns `Identifier*` which `NameToken(Identifier*)` accepts directly),
// and every `Attributes`/`TypeParameters`/`Parameters`/`Constraints` element deep-cloned through
// `Add` (which re-parents and re-indexes; `AttributeSection::Clone()` returns `AttributeSection*`,
// `TypeParameterDeclaration::Clone()` returns `TypeParameterDeclaration*`,
// `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`, `Constraint::Clone()` returns
// `Constraint*` -- the covariant concrete types `Add` accepts directly). No own location fields
// (does not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied
// (the `DestructorDeclaration` D272 / `MethodDeclaration` D284 no-location-copy precedent). The
// covariant return is `DelegateDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
// `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written partial;
// the covariant `DelegateDeclaration*` is a valid override of `AstNode::Clone`).
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`AstType`/`Identifier`/
// `TypeParameterDeclaration`/`ParameterDeclaration`/`Constraint` -- the `Attributes()`/
// `ReturnType()`/`NameToken()`/`TypeParameters()`/`Parameters()`/`Constraints()` accessors do not
// collide with any class in the `Syntax` namespace), so no elaborated-type-specifier is needed
// anywhere; the plain element types resolve to the classes. This is the `MethodDeclaration` D284
// four-collection shape with TWO trailing singles (`ReturnType`/`NameToken`) instead of four
// (no `PrivateImplementationType`, no `Body`) -- the cleanest `EntityDeclaration` with four
// collections yet, since every dependency is already ported and no new `Slots` constant is needed
// (all six kinds -- `AttributeSection`/`Type`/`Identifier`/`TypeParameter`/`Parameter`/
// `Constraint` -- are already ported across D241/D240/D237/D284/D279/D284).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_DELEGATEDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_DELEGATEDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class DelegateDeclaration : EntityDeclaration`. `final` (the C#
// `sealed`): no further derivation. An `EntityDeclaration` with four collections
// (`Attributes`/`TypeParameters`/`Parameters`/`Constraints`) plus two singles
// (`ReturnType`/`NameToken`) -- the `MethodDeclaration` D284 shape with two trailing singles
// instead of four (no `PrivateImplementationType`, no `Body`).
class DelegateDeclaration final : public EntityDeclaration {
public:
    ~DelegateDeclaration() override = default;

    // The generated empty ctor (the C# `public DelegateDeclaration()`). The four collections are
    // members (the D222 always-present-stack-member design), initialized here with their slot
    // indices as `baseIndex` (`Attributes` at 0, `TypeParameters` at 3, `Parameters` at 4,
    // `Constraints` at 5 -- the generator passes the slot index, not the dynamic flattened index;
    // unused on the non-incremental fast path) and `supportsIncremental = false` for all four (the
    // node has four collections, so none is incremental -- `collectionCount == 1 && slotIndex ==
    // slots.Count - 1` is false for every collection -- an element's flattened `ChildIndex` is
    // dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation, the `MethodDeclaration`
    // D284 four-collection precedent). The two singles default to null; both `ReturnType` and
    // `NameToken` are REQUIRED slots (a default-constructed node violates their required-slot
    // invariant -- `CheckInvariant` rejects an empty node, the `UnaryOperatorExpression` D231 /
    // `MethodDeclaration` D284 precedent).
    DelegateDeclaration() : attributes_(this, &AttributesSlot, 0, false),
                            typeParameters_(this, &TypeParametersSlot, 3, false),
                            parameters_(this, &ParametersSlot, 4, false),
                            constraints_(this, &ConstraintsSlot, 5, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.TypeDefinition; } }`
    // -- the kind of member this declaration is (a delegate is a type definition). Overrides the
    // base abstract `SymbolKind` (the `EntityDeclaration` pure-virtual). The qualified
    // `SymbolKind::TypeDefinition` avoids a `using` (the enum lives in
    // `ILSpy::Decompiler::TypeSystem` -- the `ExtensionDeclaration` D285 precedent).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the delegate (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental -- the node's first of
    // four collections). The C# lazily allocates the wrapper; the D222 port makes the collection an
    // always-present stack member, so the accessor returns the member directly. Overrides the
    // base `EntityDeclaration::Attributes` (the base body returns a detached empty via
    // `GetChildren`; this override returns the real `attributes_` member). A `const` convenience
    // overload returns `const&` for a `const DelegateDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `ReturnType` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Type")] public override partial AstType ReturnType` -- a single
    // REQUIRED `AstType` slot at slot 1 (the delegate's declared return type; non-nullable, so
    // required -- `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the
    // property setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened
    // index after a collection). Overrides the base `EntityDeclaration::ReturnType` (the base
    // body kind-walks for the `Type` kind; this override returns the backing field directly, the
    // generated `get => field!`). The getter returns the raw pointer (null for a half-constructed
    // node; the C# `!` null-forgiving).
    AstType* ReturnType() const override { return returnType_; }
    void ReturnType(AstType* value) override {
        SetChildNode(returnType_, value);
    }

    // ---- The `NameToken` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Identifier")] public override partial Identifier NameToken` -- a
    // single REQUIRED `Identifier` slot at slot 2 (the delegate's name token; non-nullable, so
    // required -- `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the
    // property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT
    // `[ExcludeFromMatch]` (unlike `ConstructorDeclaration` D281 / `DestructorDeclaration` D272),
    // so the `Name` `MatchString` term IS in `DoMatch` (a delegate's name is a real name the
    // resolver looks up by). Overrides the base `EntityDeclaration::NameToken` (the base body
    // kind-walks for the `Identifier` kind; this override returns the backing field directly, the
    // generated `get => field!`). The inherited base `EntityDeclaration::Name` (NOT overridden)
    // kind-walks for the `Identifier` kind and returns the `NameToken`'s `Name`, so the `DoMatch`
    // `MatchString` term matches the two delegates' names without a `Name` override. The getter
    // returns the raw pointer (null for a half-constructed node; the C# `!` null-forgiving).
    Identifier* NameToken() const override { return nameToken_; }
    void NameToken(Identifier* value) override {
        SetChildNode(nameToken_, value);
    }

    // ---- The `TypeParameters` collection slot (NOT a base virtual) --------------------------
    // The generated `[Slot("TypeParameter")] public partial AstNodeCollection<TypeParameterDeclaration>
    // TypeParameters` -- the delegate's generic type parameters (a
    // `CSharpSlotInfoT<TypeParameterDeclaration>` slot at slot 3, the node's SECOND of FOUR
    // collections, non-incremental). The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly. NOT
    // a base virtual, so a plain non-virtual accessor. Reuses the cycle-broken `Slots::TypeParameter`
    // kind (added by `MethodDeclaration` D284 into `TypeParameterDeclaration.hpp`). A `const`
    // convenience overload returns `const&` for a `const DelegateDeclaration*`.
    AstNodeCollectionT<TypeParameterDeclaration>& TypeParameters() { return typeParameters_; }
    const AstNodeCollectionT<TypeParameterDeclaration>& TypeParameters() const { return typeParameters_; }

    // ---- The `Parameters` collection slot (NOT a base virtual) -----------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the delegate's parameter list (a `CSharpSlotInfoT<ParameterDeclaration>` slot
    // at slot 4, the node's THIRD of FOUR collections, non-incremental). The C# lazily allocates
    // the wrapper; the D222 port makes the collection an always-present stack member, so the
    // accessor returns the member directly. NOT a base virtual, so a plain non-virtual accessor.
    // Reuses the cycle-broken `Slots::Parameter` kind (added by `IndexerDeclaration` D279). A
    // `const` convenience overload returns `const&` for a `const DelegateDeclaration*`.
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The `Constraints` collection slot (NOT a base virtual) ---------------------------
    // The generated `[Slot("Constraint")] public partial AstNodeCollection<Constraint> Constraints`
    // -- the delegate's generic `where` clauses (a `CSharpSlotInfoT<Constraint>` slot at slot 5,
    // the node's FOURTH of FOUR collections, non-incremental). The C# lazily allocates the wrapper;
    // the D222 port makes the collection an always-present stack member, so the accessor returns
    // the member directly. NOT a base virtual, so a plain non-virtual accessor. Reuses the
    // cycle-broken `Slots::Constraint` kind (added by `MethodDeclaration` D284 into
    // `Constraint.hpp`). A `const` convenience overload returns `const&` for a `const
    // DelegateDeclaration*`.
    AstNodeCollectionT<Constraint>& Constraints() { return constraints_; }
    const AstNodeCollectionT<Constraint>& Constraints() const { return constraints_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's first collection); `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>`
    // pointing at `Slots.Type`, required); `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
    // pointing at `Slots.Identifier`, required); `TypeParametersSlot` (a
    // `CSharpSlotInfoT<TypeParameterDeclaration>` pointing at `Slots.TypeParameter`, collection --
    // the node's second collection); `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>`
    // pointing at `Slots.Parameter`, collection -- the node's third collection); `ConstraintsSlot`
    // (a `CSharpSlotInfoT<Constraint>` pointing at `Slots.Constraint`, collection -- the node's
    // fourth collection). NO name shadowing (no member is named `AttributeSection`/`AstType`/
    // `Identifier`/`TypeParameterDeclaration`/`ParameterDeclaration`/`Constraint`), so the element
    // types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> ReturnTypeSlot{"ReturnType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<TypeParameterDeclaration> TypeParametersSlot{"TypeParameters", true, &Slots::TypeParameter, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<Constraint> ConstraintsSlot{"Constraints", true, &Slots::Constraint, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitDelegateDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitDelegateDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitDelegateDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitDelegateDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Six slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `ReturnType` single at slot 1 (`attrCount`), a `NameToken` single at
    // slot 2 (`attrCount + 1`), a `TypeParameters` collection at slot 3 (the contiguous range
    // `[attrCount + 2, attrCount + 2 + tpCount)`), a `Parameters` collection at slot 4 (the
    // contiguous range `[attrCount + 2 + tpCount, attrCount + 2 + tpCount + pCount)`), and a
    // `Constraints` collection at slot 5 (the contiguous range
    // `[attrCount + 2 + tpCount + pCount, attrCount + 2 + tpCount + pCount + cCount)`).
    // `GetChildCount` is `attrCount + tpCount + pCount + cCount + 2` (the four collections' current
    // lengths plus the two singles -- each single slot contributes 1 to the flattened count
    // regardless of whether it is filled); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the
    // slots subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a collection
    // step, two single steps, then three collection steps). `GetCollectionByKind` returns the
    // matching collection for each of the four collection kinds. This is the `MethodDeclaration`
    // D284 four-collection shape with two trailing singles (`ReturnType`/`NameToken`) instead of
    // four (no `PrivateImplementationType`, no `Body`).

    int GetChildCount() const override {
        return attributes_.Count() + typeParameters_.Count() + parameters_.Count()
             + constraints_.Count() + 2;
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
        throw std::out_of_range("DelegateDeclaration::GetChild");
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
        throw std::out_of_range("DelegateDeclaration::SetChild");
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
        throw std::out_of_range("DelegateDeclaration::GetChildSlotInfo");
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
    // `return other is DelegateDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && this.TypeParameters.DoMatch(o.TypeParameters, match) &&
    // this.Parameters.DoMatch(o.Parameters, match) && this.Constraints.DoMatch(o.Constraints,
    // match)`. The terms are in `MembersToMatch` order (the generator adds `Name`,
    // `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every `EntityDeclaration`-
    // derived node -- `NameToken` is NOT `[ExcludeFromMatch]` on `DelegateDeclaration`, so the
    // `Name` `String` term IS added; then the per-property scan adds `TypeParameters`/`Parameters`/
    // `Constraints` in source declaration order). The `Name` term is a `MatchString` over the
    // delegate's name (the inherited base `Name()` kind-walks for the `Identifier` kind and returns
    // the `NameToken`'s `Name`); the `ReturnType` term is `MatchOptional` (nullable recursive --
    // the generator treats the `EntityDeclaration` `ReturnType` uniformly as `MatchOptional`, even
    // though the slot is required here); the `TypeParameters`/`Parameters`/`Constraints` terms are
    // each the collection recursive `DoMatch` (the generator emits a collection-typed recursive
    // term directly, NOT `MatchOptional`). A type-only mismatch (not a `DelegateDeclaration`)
    // rejects early. The `Name()` calls are inlined in the `MatchString` arguments (the
    // `MemberType` D238 / `MethodDeclaration` D284 precedent) so the C# `&&` short-circuit is
    // preserved; the `std::string` temporaries live until the end of the full `return` expression,
    // keeping the `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<DelegateDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(returnType_, o->returnType_, match)
            && typeParameters_.DoMatch(o->typeParameters_, match)
            && parameters_.DoMatch(o->parameters_, match)
            && constraints_.DoMatch(o->constraints_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 / `MethodDeclaration` D284 precedent), the
    // annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern), the `ReturnType`/`NameToken` deep-cloned through their setters when present (which
    // re-parent; `AstType::Clone()` returns `AstType*` which `ReturnType(AstType*)` accepts
    // directly; `Identifier::Clone()` returns `Identifier*` which `NameToken(Identifier*)` accepts
    // directly), and every `Attributes`/`TypeParameters`/`Parameters`/`Constraints` element
    // deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()`
    // returns `AttributeSection*`, `TypeParameterDeclaration::Clone()` returns
    // `TypeParameterDeclaration*`, `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`,
    // `Constraint::Clone()` returns `Constraint*` -- the covariant concrete types `Add` accepts
    // directly). No own location fields (does not derive `EndLocation`), so the print-time
    // `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
    // `MethodDeclaration` D284 no-location-copy precedent). The covariant return is
    // `DelegateDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual -- `EntityDeclaration`
    // re-declares no typed `Clone`, faithful to its empty hand-written partial; the covariant
    // `DelegateDeclaration*` is a valid override of `AstNode::Clone`).
    DelegateDeclaration* Clone() const override {
        auto* node = new DelegateDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (returnType_ != nullptr)
            node->ReturnType(returnType_->Clone());
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
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_`/`typeParameters_`/`parameters_`/`constraints_` are the
    // always-present collection members (empty until the first `Add`, all non-incremental -- the
    // node has four collections); `returnType_`/`nameToken_` are null until set (REQUIRED slots --
    // `CheckInvariant` asserts they are filled). NO name shadowing (no member is named
    // `AttributeSection`/`AstType`/`Identifier`/`TypeParameterDeclaration`/`ParameterDeclaration`/
    // `Constraint`), so the field types are the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstType* returnType_ = nullptr;
    Identifier* nameToken_ = nullptr;
    AstNodeCollectionT<TypeParameterDeclaration> typeParameters_;
    AstNodeCollectionT<ParameterDeclaration> parameters_;
    AstNodeCollectionT<Constraint> constraints_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_DELEGATEDECLARATION_HPP
