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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
// BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the `ExtensionDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/ExtensionDeclaration.cs (the generated
// `ExtensionDeclaration.g.cs` + the hand-written partial, which declares the `SymbolKind` override
// and the five collection slot properties plus the `ExtensionKeyword` const -- no ctors, no
// helpers, no other const strings). The next in-order Phase-5 piece per the D284 plan ("the
// remaining TypeMember hierarchy now that MethodDeclaration lands the TypeParameters/Parameters/
// Constraints collection triple: ExtensionDeclaration (a MethodDeclaration sibling ...), then
// FixedFieldDeclaration, FixedVariableInitializer; then the OutputVisitor ...").
//
// `extension_declaration ::= attribute_section* 'extension' type_parameter* '(' parameter* ')'
// constraint* '{' member* '}'` (C# 14 extension blocks, the `extension(receiver) { members }` form
// -- not present in earlier revisions of the C# specification grammar): a sealed
// `EntityDeclaration` (the `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so
// `final` -- no pattern placeholder). FIVE `[Slot]` children in source declaration order, ALL
// collections (no single slots -- the first ported `EntityDeclaration` with no single slots):
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the extension (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's FIRST of FIVE
//     collections, so `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex ==
//     slots.Count - 1` is false -- `collectionCount` is 5): every `Add`/`Insert`/`Remove`/
//     single-slot-set INVALIDATES the parent's indices for a lazy `EnsureChildIndices` rebuild
//     (the `ComposedType` D242 / `MethodDeclaration` D284 multi-collection precedent, generalized
//     to five collections). Overrides the base `EntityDeclaration::Attributes` (the base body
//     returns a detached empty via `GetChildren`; this override returns the real `attributes_`
//     member).
//   * `[Slot("TypeParameter")] public partial AstNodeCollection<TypeParameterDeclaration>
//     TypeParameters` -- the extension's generic type parameters (a COLLECTION at slot 1, the
//     `<T, U, ...>`; reusing the cycle-broken `Slots::TypeParameter` kind, added by
//     `MethodDeclaration` D284 into `TypeParameterDeclaration.hpp` after the class). The collection
//     is the node's SECOND of FIVE collections, so `supportsIncremental` is FALSE; `baseIndex` is 1
//     (the slot index; the generator passes the slot index, not the dynamic flattened index --
//     unused on the non-incremental fast path). NOT a base virtual, so a plain non-virtual
//     accessor.
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> ReceiverParameters`
//     -- the extension's receiver parameter list (a COLLECTION at slot 2, the `( parameter* )`;
//     reusing the cycle-broken `Slots::Parameter` kind, added by `IndexerDeclaration` D279 into
//     `ParameterDeclaration.hpp`). The collection is the node's THIRD of FIVE collections, so
//     `supportsIncremental` is FALSE; `baseIndex` is 2. NOT a base virtual, so a plain non-virtual
//     accessor. (The slot kind name is `Parameter` though the property is `ReceiverParameters` --
//     the `[Slot]` argument names the slot KIND, the property name is the accessor; the kind is
//     shared with `MethodDeclaration.Parameters`/`IndexerDeclaration.Parameters`.)
//   * `[Slot("Constraint")] public partial AstNodeCollection<Constraint> Constraints` -- the
//     extension's generic `where` clauses (a COLLECTION at slot 3, the `where T : ...` clauses;
//     reusing the cycle-broken `Slots::Constraint` kind, added by `MethodDeclaration` D284 into
//     `Constraint.hpp`). The collection is the node's FOURTH of FIVE collections, so
//     `supportsIncremental` is FALSE; `baseIndex` is 3. NOT a base virtual, so a plain non-virtual
//     accessor.
//   * `[Slot("TypeMember")] public partial AstNodeCollection<EntityDeclaration> Members` -- the
//     extension's member declarations (a COLLECTION at slot 4, the `{ member* }`; reusing the
//     cycle-broken `Slots::TypeMember` kind, added this iteration into `EntityDeclaration.hpp`
//     after the `EntityDeclaration` class -- the first `EntityDeclaration`-typed collection kind).
//     The collection is the node's FIFTH of FIVE collections (the last slot), but
//     `supportsIncremental` is STILL FALSE (`collectionCount == 1 && slotIndex == slots.Count - 1`
//     is false -- `collectionCount` is 5); `baseIndex` is 4. NOT a base virtual, so a plain
//     non-virtual accessor.
//
// The hand-written `ExtensionKeyword` const string `"extension"` -- a public-API const string (the
// output-visitor keyword token), ports as a `static constexpr const char*` (the `CheckedExpression`
// D234 / `MethodDeclaration`-family keyword-token precedent). A static field (not instance state),
// so the generator's `MembersToMatch` (which iterates only instance `IPropertySymbol`s) excludes it
// from the `DoMatch`.
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term), `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every
// `EntityDeclaration`-derived node, then the per-property scan adds the non-override non-
// `[ExcludeFromMatch]` `[Slot]` children in source declaration order. `ExtensionDeclaration`
// declares NO `NameToken` slot (it has no `[Slot("Identifier")]`), so the `excludeName` check finds
// no `NameToken` member -- `Name` IS added (the `FieldDeclaration` D273 always-empty-`Name`
// precedent; the inherited base `Name()` kind-walks for the `Identifier` kind and returns the
// `NameToken`'s `Name`, but `ExtensionDeclaration` declares no `Identifier` slot, so the kind-walk
// returns null and `Name()` returns the empty string -- `MatchString("", "")` is vacuously true).
// `ExtensionDeclaration` declares NO `ReturnType` slot (it has no `[Slot("Type")]`), so the
// `ReturnType` `MatchOptional` term is vacuous (the inherited base `ReturnType()` kind-walks for the
// `Type` kind and returns null; `MatchOptional(nullptr, nullptr)` is true -- the `DestructorDeclaration`
// D272 vacuous-`ReturnType` precedent). The per-property scan adds the four non-override
// `[Slot]` collections (`TypeParameters`/`ReceiverParameters`/`Constraints`/`Members` -- `Attributes`
// is an `override`, so the scan skips it; it is already covered by `MatchAttributesAndModifiers`).
// So `MembersToMatch` is `[Name, MatchAttributesAndModifiers, ReturnType, TypeParameters,
// ReceiverParameters, Constraints, Members]`, and the generated `DoMatch` is `return other is
// ExtensionDeclaration o && MatchString(this.Name, o.Name) && this.MatchAttributesAndModifiers(o,
// match) && MatchOptional(this.ReturnType, o.ReturnType, match) && this.TypeParameters.DoMatch(o.
// TypeParameters, match) && this.ReceiverParameters.DoMatch(o.ReceiverParameters, match) &&
// this.Constraints.DoMatch(o.Constraints, match) && this.Members.DoMatch(o.Members, match)`. The
// `Name` term is a `MatchString` over the always-empty `Name`; the `ReturnType` term is
// `MatchOptional` over the always-null `ReturnType`; the `TypeParameters`/`ReceiverParameters`/
// `Constraints`/`Members` terms are each the collection recursive `DoMatch` (the generator emits a
// collection-typed recursive term directly, NOT `MatchOptional` -- the `FieldDeclaration.Variables`
// D273 / `MethodDeclaration.Parameters` D284 precedent). A type-only mismatch (not an
// `ExtensionDeclaration`) rejects early.
//
// The generated ctors (`WriteConstructors`): `CtorParams` in declaration order is `[Attributes
// (collection, optional), TypeParameters (collection, optional), ReceiverParameters (collection,
// optional), Constraints (collection, optional), Members (collection, optional)]` -- the
// `Modifiers` scalar lives on the `EntityDeclaration` base and is NOT a declared member of
// `ExtensionDeclaration`, so `GetMembers()` does not add it (the `DestructorDeclaration` D272 /
// `MethodDeclaration` D284 precedent). `RequiredConstructorPrefixLength` is 0 (no required params --
// all five are optional collections), so `ConstructorPrefixLengths` is `{0}` -- only the empty
// generated ctor is portable (every parametrized ctor calls `this.Attributes.AddRange(...)` /
// `this.TypeParameters.AddRange(...)` / ... for the five collections, all `AddRange` conveniences
// are the D222 deferral). An `ExtensionDeclaration` is built via the empty ctor +
// `TypeParameters().Add(...)` + `ReceiverParameters().Add(...)` + `Constraints().Add(...)` +
// `Members().Add(...)` + `Attributes().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitExtensionDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitExtensionDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `TypeParametersSlot` (a `CSharpSlotInfoT<TypeParameterDeclaration>` pointing at
// `Slots.TypeParameter`, collection), `ReceiverParametersSlot` (a
// `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection),
// `ConstraintsSlot` (a `CSharpSlotInfoT<Constraint>` pointing at `Slots.Constraint`, collection),
// and `MembersSlot` (a `CSharpSlotInfoT<EntityDeclaration>` pointing at `Slots.TypeMember`,
// collection). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
// `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the derived
// `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the public surface,
// the `DestructorDeclaration` D272 / `MethodDeclaration` D284 precedent), the annotation channel
// copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
// `Attributes`/`TypeParameters`/`ReceiverParameters`/`Constraints`/`Members` element deep-cloned
// through `Add` (which re-parents and re-indexes). The `Members` collection's element type is the
// abstract `EntityDeclaration` base, which redeclares NO typed `Clone` (it inherits
// `AstNode::Clone` returning `AstNode*`), so each `Members` element is cloned via
// `static_cast<EntityDeclaration*>(elem->Clone())` -- the concrete element's covariant `Clone()`
// (e.g. `MethodDeclaration::Clone()` returning `MethodDeclaration*`) IS-A `EntityDeclaration`, so
// the downcast is safe (the `BlockStatement` D256 abstract-`Statement`-collection Clone precedent
// applied to an abstract base that redeclares no typed `Clone`; `Statement` redeclares one but
// `EntityDeclaration` does not, so the `static_cast` is required). The other four collections'
// element types (`AttributeSection`/`TypeParameterDeclaration`/`ParameterDeclaration`/`Constraint`)
// each redeclare a typed covariant `Clone` returning their concrete type, so their `Add` calls
// accept the clone directly. No own location fields (does not derive `EndLocation`), so the
// print-time `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
// `MethodDeclaration` D284 no-location-copy precedent). The covariant return is
// `ExtensionDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual -- `EntityDeclaration`
// re-declares no typed `Clone`, faithful to its empty hand-written partial; the covariant
// `ExtensionDeclaration*` is a valid override of `AstNode::Clone`).
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`TypeParameterDeclaration`/
// `ParameterDeclaration`/`Constraint`/`EntityDeclaration` -- the `Attributes()`/`TypeParameters()`/
// `ReceiverParameters()`/`Constraints()`/`Members()` accessors do not collide with any class in the
// `Syntax` namespace), so no elaborated-type-specifier is needed anywhere; the plain element types
// resolve to the classes. This is the first `EntityDeclaration` with NO single slots (the
// `MethodDeclaration` D284 four-collection + four-single shape with all four singles removed and one
// more collection added) -- the collection-only `ArrayInitializerExpression` D250 /
// `BlockStatement` D256 shape applied to the `EntityDeclaration` hierarchy with five collections.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXTENSIONDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXTENSIONDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ExtensionDeclaration : EntityDeclaration`. `final` (the C#
// `sealed`): no further derivation. The first `EntityDeclaration` with NO single slots -- five
// collections (`Attributes`/`TypeParameters`/`ReceiverParameters`/`Constraints`/`Members`), all
// non-incremental (`collectionCount` is 5).
class ExtensionDeclaration final : public EntityDeclaration {
public:
    ~ExtensionDeclaration() override = default;

    // The generated empty ctor (the C# `public ExtensionDeclaration()`). The five collections are
    // members (the D222 always-present-stack-member design), initialized here with their slot
    // indices as `baseIndex` (`Attributes` at 0, `TypeParameters` at 1, `ReceiverParameters` at 2,
    // `Constraints` at 3, `Members` at 4 -- the generator passes the slot index, not the dynamic
    // flattened index; unused on the non-incremental fast path) and `supportsIncremental = false`
    // for all five (the node has five collections, so none is incremental -- `collectionCount == 1
    // && slotIndex == slots.Count - 1` is false for every collection -- an element's flattened
    // `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation, the
    // `ComposedType` D242 / `MethodDeclaration` D284 five-collection precedent). There are NO single
    // slots, so the node declares no scalar/child backing fields; `CheckInvariant` passes on an
    // EMPTY node (no required single slots -- the `ArrayInitializerExpression` D250 /
    // `BlockStatement` D256 collection-only precedent applied to the `EntityDeclaration`
    // hierarchy).
    ExtensionDeclaration() : attributes_(this, &AttributesSlot, 0, false),
                             typeParameters_(this, &TypeParametersSlot, 1, false),
                             receiverParameters_(this, &ReceiverParametersSlot, 2, false),
                             constraints_(this, &ConstraintsSlot, 3, false),
                             members_(this, &MembersSlot, 4, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.TypeDefinition; } }`
    // -- the kind of member this declaration is (an extension block, reported as a type definition).
    // Overrides the base abstract `SymbolKind` (the `EntityDeclaration` pure-virtual). The qualified
    // `SymbolKind::TypeDefinition` avoids a `using` (the enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }

    // The C# `public const string ExtensionKeyword = "extension"` -- the output-visitor keyword
    // token. A public-API const string ports as a `static constexpr const char*` (the
    // `CheckedExpression.CheckedKeyword` D234 / `MethodDeclaration`-family keyword-token
    // precedent). A static field (not instance state), so the generator's `MembersToMatch` (which
    // iterates only instance `IPropertySymbol`s) excludes it from the `DoMatch`.
    static constexpr const char* ExtensionKeyword = "extension";

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the extension (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental -- the node's first of
    // five collections). Overrides the base `EntityDeclaration::Attributes` (the base body returns
    // a detached empty via `GetChildren`; this override returns the real `attributes_` member). A
    // `const` convenience overload returns `const&` for a `const ExtensionDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `TypeParameters` collection slot (NOT a base virtual) -------------------------
    // The generated `[Slot("TypeParameter")] public partial AstNodeCollection<TypeParameterDeclaration>
    // TypeParameters` -- the extension's generic type parameters (a
    // `CSharpSlotInfoT<TypeParameterDeclaration>` slot at slot 1, the node's SECOND of FIVE
    // collections, non-incremental). Reuses the cycle-broken `Slots::TypeParameter` kind (added by
    // `MethodDeclaration` D284). NOT a base virtual, so a plain non-virtual accessor. A `const`
    // convenience overload returns `const&` for a `const ExtensionDeclaration*`.
    AstNodeCollectionT<TypeParameterDeclaration>& TypeParameters() { return typeParameters_; }
    const AstNodeCollectionT<TypeParameterDeclaration>& TypeParameters() const { return typeParameters_; }

    // ---- The `ReceiverParameters` collection slot (NOT a base virtual) --------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // ReceiverParameters` -- the extension's receiver parameter list (a
    // `CSharpSlotInfoT<ParameterDeclaration>` slot at slot 2, the node's THIRD of FIVE
    // collections, non-incremental). The slot kind is `Parameter` (shared with
    // `MethodDeclaration.Parameters`/`IndexerDeclaration.Parameters`), but the property name is
    // `ReceiverParameters`. Reuses the cycle-broken `Slots::Parameter` kind (added by
    // `IndexerDeclaration` D279). NOT a base virtual, so a plain non-virtual accessor. A `const`
    // convenience overload returns `const&` for a `const ExtensionDeclaration*`.
    AstNodeCollectionT<ParameterDeclaration>& ReceiverParameters() { return receiverParameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& ReceiverParameters() const { return receiverParameters_; }

    // ---- The `Constraints` collection slot (NOT a base virtual) ----------------------------
    // The generated `[Slot("Constraint")] public partial AstNodeCollection<Constraint> Constraints`
    // -- the extension's generic `where` clauses (a `CSharpSlotInfoT<Constraint>` slot at slot 3,
    // the node's FOURTH of FIVE collections, non-incremental). Reuses the cycle-broken
    // `Slots::Constraint` kind (added by `MethodDeclaration` D284). NOT a base virtual, so a plain
    // non-virtual accessor. A `const` convenience overload returns `const&` for a `const
    // ExtensionDeclaration*`.
    AstNodeCollectionT<Constraint>& Constraints() { return constraints_; }
    const AstNodeCollectionT<Constraint>& Constraints() const { return constraints_; }

    // ---- The `Members` collection slot (NOT a base virtual) --------------------------------
    // The generated `[Slot("TypeMember")] public partial AstNodeCollection<EntityDeclaration>
    // Members` -- the extension's member declarations (a `CSharpSlotInfoT<EntityDeclaration>` slot
    // at slot 4, the node's FIFTH of FIVE collections, non-incremental). Reuses the cycle-broken
    // `Slots::TypeMember` kind (added this iteration into `EntityDeclaration.hpp` -- the first
    // `EntityDeclaration`-typed collection kind). NOT a base virtual, so a plain non-virtual
    // accessor. A `const` convenience overload returns `const&` for a `const ExtensionDeclaration*`.
    AstNodeCollectionT<EntityDeclaration>& Members() { return members_; }
    const AstNodeCollectionT<EntityDeclaration>& Members() const { return members_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at
    // `Slots.AttributeSection`, collection -- the node's first collection); `TypeParametersSlot`
    // (a `CSharpSlotInfoT<TypeParameterDeclaration>` pointing at `Slots.TypeParameter`, collection --
    // the node's second collection); `ReceiverParametersSlot` (a
    // `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection -- the
    // node's third collection); `ConstraintsSlot` (a `CSharpSlotInfoT<Constraint>` pointing at
    // `Slots.Constraint`, collection -- the node's fourth collection); `MembersSlot` (a
    // `CSharpSlotInfoT<EntityDeclaration>` pointing at `Slots.TypeMember`, collection -- the
    // node's fifth collection). NO name shadowing (no member is named `AttributeSection`/
    // `TypeParameterDeclaration`/`ParameterDeclaration`/`Constraint`/`EntityDeclaration`), so the
    // element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<TypeParameterDeclaration> TypeParametersSlot{"TypeParameters", true, &Slots::TypeParameter, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ReceiverParametersSlot{"ReceiverParameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<Constraint> ConstraintsSlot{"Constraints", true, &Slots::Constraint, true};
    static inline const CSharpSlotInfoT<EntityDeclaration> MembersSlot{"Members", true, &Slots::TypeMember, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitExtensionDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitExtensionDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitExtensionDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitExtensionDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Five collections in declaration order, NO single slots: an `Attributes` collection at slot 0
    // (the contiguous range `[0, attrCount)`), a `TypeParameters` collection at slot 1 (the
    // contiguous range `[attrCount, attrCount + tpCount)`), a `ReceiverParameters` collection at
    // slot 2 (the contiguous range `[attrCount + tpCount, attrCount + tpCount + rpCount)`), a
    // `Constraints` collection at slot 3 (the contiguous range
    // `[attrCount + tpCount + rpCount, attrCount + tpCount + rpCount + cCount)`), and a `Members`
    // collection at slot 4 (the contiguous range
    // `[attrCount + tpCount + rpCount + cCount, attrCount + tpCount + rpCount + cCount + mCount)`).
    // `GetChildCount` is `attrCount + tpCount + rpCount + cCount + mCount` (the five collections'
    // current lengths -- there are NO single slots, so an EMPTY node reports `GetChildCount` 0,
    // the `ArrayInitializerExpression` D250 / `BlockStatement` D256 collection-only precedent);
    // `GetChild`/`SetChild`/`GetChildSlotInfo` walk the five collections subtracting each one's
    // width from a running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- five collection steps, no single steps).
    // `GetCollectionByKind` returns the matching collection for each of the five collection kinds.
    // This is the first ported node with FIVE collections and NO single slots -- the
    // `MethodDeclaration` D284 four-collection shape with all four singles removed and one more
    // collection added.

    int GetChildCount() const override {
        return attributes_.Count() + typeParameters_.Count() + receiverParameters_.Count()
             + constraints_.Count() + members_.Count();
    }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        {
            int n = typeParameters_.Count();
            if (i < n)
                return typeParameters_.At(i);
            i -= n;
        }
        {
            int n = receiverParameters_.Count();
            if (i < n)
                return receiverParameters_.At(i);
            i -= n;
        }
        {
            int n = constraints_.Count();
            if (i < n)
                return constraints_.At(i);
            i -= n;
        }
        {
            int n = members_.Count();
            if (i < n)
                return members_.At(i);
            i -= n;
        }
        throw std::out_of_range("ExtensionDeclaration::GetChild");
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
        {
            int n = typeParameters_.Count();
            if (i < n) {
                typeParameters_.SetAt(i, static_cast<TypeParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = receiverParameters_.Count();
            if (i < n) {
                receiverParameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
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
        {
            int n = members_.Count();
            if (i < n) {
                members_.SetAt(i, static_cast<EntityDeclaration*>(value));
                return;
            }
            i -= n;
        }
        throw std::out_of_range("ExtensionDeclaration::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return &AttributesSlot;
            i -= n;
        }
        {
            int n = typeParameters_.Count();
            if (i < n)
                return &TypeParametersSlot;
            i -= n;
        }
        {
            int n = receiverParameters_.Count();
            if (i < n)
                return &ReceiverParametersSlot;
            i -= n;
        }
        {
            int n = constraints_.Count();
            if (i < n)
                return &ConstraintsSlot;
            i -= n;
        }
        {
            int n = members_.Count();
            if (i < n)
                return &MembersSlot;
            i -= n;
        }
        throw std::out_of_range("ExtensionDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::TypeParameter)
            return &typeParameters_;
        if (kind == &Slots::Parameter)
            return &receiverParameters_;
        if (kind == &Slots::Constraint)
            return &constraints_;
        if (kind == &Slots::TypeMember)
            return &members_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ExtensionDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && this.TypeParameters.DoMatch(o.TypeParameters, match) && this.ReceiverParameters.
    // DoMatch(o.ReceiverParameters, match) && this.Constraints.DoMatch(o.Constraints, match) &&
    // this.Members.DoMatch(o.Members, match)`. The terms are in `MembersToMatch` order (the
    // generator adds `Name`, `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every
    // `EntityDeclaration`-derived node -- `ExtensionDeclaration` declares NO `NameToken` slot, so
    // the `excludeName` check finds no `NameToken` member and the `Name` `String` term IS added,
    // the `FieldDeclaration` D273 always-empty-`Name` precedent; then the per-property scan adds
    // the four non-override `[Slot]` collections `TypeParameters`/`ReceiverParameters`/
    // `Constraints`/`Members` in source declaration order -- `Attributes` is an `override`, so the
    // scan skips it). The `Name` term is a `MatchString` over the always-empty `Name` (the
    // inherited base `Name()` kind-walks for the `Identifier` kind and returns null -> empty
    // string -- `ExtensionDeclaration` declares no `Identifier` slot); the `ReturnType` term is
    // `MatchOptional` over the always-null `ReturnType` (the inherited base `ReturnType()`
    // kind-walks for the `Type` kind and returns null -- `ExtensionDeclaration` declares no `Type`
    // slot; `MatchOptional(nullptr, nullptr)` is true -- the `DestructorDeclaration` D272
    // vacuous-`ReturnType` precedent); the `TypeParameters`/`ReceiverParameters`/`Constraints`/
    // `Members` terms are each the collection recursive `DoMatch` (the generator emits a
    // collection-typed recursive term directly, NOT `MatchOptional` -- the `FieldDeclaration.
    // Variables` D273 / `MethodDeclaration.Parameters` D284 precedent). A type-only mismatch (not
    // an `ExtensionDeclaration`) rejects early. The `Name()` calls are inlined in the `MatchString`
    // arguments (the `MemberType` D238 / `OperatorDeclaration` D280 precedent) so the C# `&&`
    // short-circuit is preserved; the `std::string` temporaries live until the end of the full
    // `return` expression, keeping the `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ExtensionDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && typeParameters_.DoMatch(o->typeParameters_, match)
            && receiverParameters_.DoMatch(o->receiverParameters_, match)
            && constraints_.DoMatch(o->constraints_, match)
            && members_.DoMatch(o->members_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the derived
    // `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the public
    // surface, the `DestructorDeclaration` D272 / `MethodDeclaration` D284 precedent), the
    // annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern), and every `Attributes`/`TypeParameters`/`ReceiverParameters`/`Constraints`/`Members`
    // element deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()`
    // returns `AttributeSection*`, `TypeParameterDeclaration::Clone()` returns
    // `TypeParameterDeclaration*`, `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`,
    // `Constraint::Clone()` returns `Constraint*` -- the covariant concrete types `Add` accepts
    // directly). The `Members` collection's element type is the abstract `EntityDeclaration` base,
    // which redeclares NO typed `Clone` (it inherits `AstNode::Clone` returning `AstNode*`), so
    // each `Members` element is cloned via `static_cast<EntityDeclaration*>(elem->Clone())` -- the
    // concrete element's covariant `Clone()` (e.g. `MethodDeclaration::Clone()` returning
    // `MethodDeclaration*`) IS-A `EntityDeclaration`, so the downcast is safe (the `BlockStatement`
    // D256 abstract-`Statement`-collection Clone precedent applied to an abstract base that
    // redeclares no typed `Clone`; `Statement` redeclares one but `EntityDeclaration` does not, so
    // the `static_cast` is required). No own location fields (does not derive `EndLocation`), so the
    // print-time `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
    // `MethodDeclaration` D284 no-location-copy precedent). The covariant return is
    // `ExtensionDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual -- `EntityDeclaration`
    // re-declares no typed `Clone`, faithful to its empty hand-written partial; the covariant
    // `ExtensionDeclaration*` is a valid override of `AstNode::Clone`).
    ExtensionDeclaration* Clone() const override {
        auto* node = new ExtensionDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        for (int i = 0; i < typeParameters_.Count(); i++)
            node->typeParameters_.Add(typeParameters_.At(i)->Clone());
        for (int i = 0; i < receiverParameters_.Count(); i++)
            node->receiverParameters_.Add(receiverParameters_.At(i)->Clone());
        for (int i = 0; i < constraints_.Count(); i++)
            node->constraints_.Add(constraints_.At(i)->Clone());
        for (int i = 0; i < members_.Count(); i++)
            node->members_.Add(static_cast<EntityDeclaration*>(members_.At(i)->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. The five collections are the always-present collection members (empty
    // until the first `Add`, all non-incremental -- the node has five collections); there are NO
    // single slots, so no scalar/child backing fields. NO name shadowing (no member is named
    // `AttributeSection`/`TypeParameterDeclaration`/`ParameterDeclaration`/`Constraint`/
    // `EntityDeclaration`), so the field types are the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstNodeCollectionT<TypeParameterDeclaration> typeParameters_;
    AstNodeCollectionT<ParameterDeclaration> receiverParameters_;
    AstNodeCollectionT<Constraint> constraints_;
    AstNodeCollectionT<EntityDeclaration> members_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXTENSIONDECLARATION_HPP
