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

// Port of the `FieldDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/FieldDeclaration.cs (the generated
// `FieldDeclaration.g.cs` + the hand-written partial, which declares only the `SymbolKind`
// override, the slot properties, and the `[EditorBrowsable(Never)]` `Name`/`NameToken`
// overrides that hide the inherited name accessors -- no ctors, no helpers). The next in-order
// Phase-5 piece per the D272 plan ("the remaining TypeMember hierarchy (MethodDeclaration -- which
// unblocks LocalFunctionDeclarationStatement; FieldDeclaration; ...)").
//
// `field_declaration ::= attribute_section* modifier* type variable_initializer* ';'`
// (C# grammar 15.5.1): a sealed `EntityDeclaration` (the `[DecompilerAstNode]` default
// `hasPatternPlaceholder: false`, so `final`). Three `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the field (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is NON-incremental (the node
//     has TWO collections -- `Attributes` and `Variables` -- so `supportsIncremental` is false
//     for both, the `ComposedType` D242 two-collection precedent).
//   * `[Slot("Type")] public override partial AstType ReturnType` -- the field's declared type (a
//     single REQUIRED `AstType` slot at slot 1, non-nullable -- the property type is `AstType`,
//     no `?`; reusing `Slots::Type`). The slot FOLLOWS the `Attributes` collection, so its property
//     setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index
//     after a collection; the `ComposedType.BaseType` D242 precedent). Overrides the base
//     `EntityDeclaration::ReturnType` (the base body kind-walks for the `Type` kind; this override
//     returns the backing field directly, the generated `get => field!`).
//   * `[Slot("Variable")] public partial AstNodeCollection<VariableInitializer> Variables` -- the
//     comma-separated `name = initializer` declarators (a COLLECTION at slot 2, reusing the
//     cycle-broken `Slots::Variable` kind added by `FixedStatement` D267). Non-incremental (two
//     collections). NOT a base virtual (the `EntityDeclaration` base declares no `Variables`
//     virtual), so a plain non-virtual accessor.
//
// The field's name is NOT a slot on `FieldDeclaration`: the `Name`/`NameToken` virtuals are
// overridden to return `string.Empty`/`null` and throw on set (the `[EditorBrowsable(Never)]`
// hides them from IntelliSense -- the actual field names live in the `VariableInitializer`
// children). A `FieldDeclaration` therefore declares NO `Identifier` slot (unlike
// `DestructorDeclaration` which has a `NameToken` `Identifier`), so the base `NameToken()`
// kind-walk would return null anyway; the override makes the throw explicit and faithful to C#.
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name`
// (a `String` `MatchString` term, since `NameToken` is NOT `[ExcludeFromMatch]` -- it is
// `[EditorBrowsable]`, a different attribute), `MatchAttributesAndModifiers`, and `ReturnType`
// explicitly for every `EntityDeclaration`-derived node, then the per-property scan adds
// `Variables` (a non-override, non-`[ExcludeFromMatch]` `AstNode`-derived collection property).
// So `MembersToMatch` is `[Name, MatchAttributesAndModifiers, ReturnType, Variables]`, and the
// generated `DoMatch` is `return other is FieldDeclaration o &&
// MatchString(this.Name, o.Name) && this.MatchAttributesAndModifiers(o, match) &&
// MatchOptional(this.ReturnType, o.ReturnType, match) && this.Variables.DoMatch(o.Variables,
// match)`. The `Name` term is a `MatchString` over the always-empty `Name` (both return
// `string.Empty`/`""`, so `MatchString("", "")` is vacuously true -- a structural term present for
// fidelity to the generator, harmless for well-formed nodes); the `ReturnType` term is
// `MatchOptional` (nullable recursive -- the generator treats the `EntityDeclaration` `ReturnType`
// uniformly as `MatchOptional` across all subclasses, so a node with a real `ReturnType` slot
// matches when both sides carry one and the pattern's `ReturnType.DoMatch` accepts the
// candidate's); the `Variables` term is the collection recursive match (the generator emits the
// collection-typed recursive term directly, NOT `MatchOptional`). The `MatchAttributesAndModifiers`
// helper (on `EntityDeclaration`, protected) matches the `Modifiers` scalar (the `Any`-wildcard)
// AND the `Attributes` collection together.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// ReturnType (required), Variables (collection, optional)]` (the `Modifiers` scalar lives on the
// `EntityDeclaration` base and is NOT a declared member of `FieldDeclaration`, so
// `GetMembers()` does not add it to `CtorParams` -- the `DestructorDeclaration` D272 precedent:
// the generated ctors take no `Modifiers`). `RequiredConstructorPrefixLength` is 2 (through the
// last non-optional param `ReturnType`), `ConstructorPrefixLengths` is {2, 3}. The (len=2) ctor
// `(IEnumerable<AttributeSection>, AstType)` and the (len=3) ctor
// `(IEnumerable<AttributeSection>, AstType, IEnumerable<VariableInitializer>)` both call
// `this.Attributes.AddRange(...)` / `this.Variables.AddRange(...)` (the `AddRange` convenience is
// the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. A
// `FieldDeclaration` is built via the empty ctor + `Modifiers(...)` + `ReturnType(...)` +
// `Attributes().Add(...)` + `Variables().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitFieldDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitFieldDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the
// `ReturnType` `AstType` is non-nullable, so `IsCollection || IsNullable` is `false`), and
// `VariablesSlot` (a `CSharpSlotInfoT<VariableInitializer>` pointing at `Slots.Variable`,
// collection). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
// `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the derived
// `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the public
// surface, the faithful equivalent), the annotation channel copied (`CloneAnnotationsFrom` +
// `ReparentTrivia`, the D223 concrete-clone pattern), the `ReturnType` deep-cloned through the
// setter, and every `Attributes`/`Variables` element deep-cloned through `Add`.
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`AstType`/
// `VariableInitializer`/`SymbolKind` -- the `Attributes()`/`ReturnType()`/`Variables()`/
// `Name()`/`NameToken()`/`SymbolKind()` accessors do not collide with any class in the `Syntax`
// namespace), so no elaborated-type-specifier is needed anywhere; the plain element types resolve
// to the classes. This is the cleanest two-collection `EntityDeclaration` subclass.
//
// NO new `Slots` constant: `Slots::AttributeSection` is cycle-broken in `AttributeSection.hpp`
// (D242), `Slots::Type` is in `Slots.hpp` (D240), and `Slots::Variable` is cycle-broken in
// `VariableInitializer.hpp` (D267), so `Slots.hpp` is unchanged.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_FIELDDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_FIELDDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class FieldDeclaration : EntityDeclaration`. `final` (the C#
// `sealed`): no further derivation. The second concrete `TypeMember` and the first ported
// `EntityDeclaration` with TWO collection slots (`Attributes` + `Variables`) with the required
// `ReturnType` single slot between them (the `ComposedType` D242 collection -> single -> collection
// shape applied to the `TypeMember` hierarchy).
class FieldDeclaration final : public EntityDeclaration {
public:
    ~FieldDeclaration() override = default;

    // The generated empty ctor (the C# `public FieldDeclaration()`). Both collections are
    // always-present stack members (the D222 design): `attributes_` initialized with `baseIndex =
    // 0` (the collection's slot index) and `supportsIncremental = false` (the node has two
    // collections, so neither owns a contiguous tail range); `variables_` initialized with
    // `baseIndex = 2` (its slot index -- the generator passes the slot index, not the dynamic
    // flattened index, and the fast path is off) and `supportsIncremental = false`. With both
    // collections non-incremental, every `Add`/`Insert`/`Remove`/single-slot-set invalidates the
    // parent's indices for a lazy `EnsureChildIndices` rebuild. `returnType_` defaults to null (a
    // REQUIRED slot -- a default-constructed node violates the required-slot invariant, the
    // `UnaryOperatorExpression` D231 precedent; `CheckInvariant` rejects an empty node).
    FieldDeclaration() : attributes_(this, &AttributesSlot, 0, false),
                         variables_(this, &VariablesSlot, 2, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Field; } }` -- the
    // kind of member this declaration is. Overrides the base abstract `SymbolKind` (the
    // `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Field` avoids a `using` (the
    // enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Field;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the field (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental). The C# lazily
    // allocates the wrapper; the D222 port makes the collection an always-present stack member,
    // so the accessor returns the member directly. Overrides the base
    // `EntityDeclaration::Attributes` (the base body returns a detached empty via `GetChildren`;
    // this override returns the real `attributes_` member). A `const` convenience overload
    // returns `const&` for a `const FieldDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `ReturnType` slot (override of the base virtual) -----------------------------
    // The generated `[Slot("Type")] public override partial AstType ReturnType` -- a single
    // REQUIRED `AstType` slot at slot 1 (the field's declared type; non-nullable, so required --
    // `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the property
    // setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index
    // after a collection). Overrides the base `EntityDeclaration::ReturnType` (the base body
    // kind-walks for the `Type` kind; this override returns the backing field directly, the
    // generated `get => field!`). The getter returns the raw pointer (null for a half-constructed
    // node; the C# `!` null-forgiving).
    AstType* ReturnType() const override { return returnType_; }
    void ReturnType(AstType* value) override {
        SetChildNode(returnType_, value);
    }

    // ---- The `Variables` collection slot (NOT a base virtual) ------------------------------
    // The generated `[Slot("Variable")] public partial AstNodeCollection<VariableInitializer>
    // Variables` -- the comma-separated `name = initializer` declarators (a
    // `CSharpSlotInfoT<VariableInitializer>` slot at slot 2, non-incremental). NOT an override
    // (the `EntityDeclaration` base declares no `Variables` virtual), so a plain non-virtual
    // accessor. `baseIndex = 2` (the slot index; the dynamic flattened index `attrCount + 1` is
    // rebuilt lazily by `EnsureChildIndices` since the fast path is off).
    AstNodeCollectionT<VariableInitializer>& Variables() { return variables_; }
    const AstNodeCollectionT<VariableInitializer>& Variables() const { return variables_; }

    // ---- The `Name`/`NameToken` overrides (hidden from users; the names live in
    // `VariableInitializer`) ----------------------------------------------------------------
    // The C# `[EditorBrowsable(EditorBrowsableState.Never)] public override string Name { get {
    // return string.Empty; } set { throw new NotSupportedException(); } }` -- a field's name is
    // not a single token (a field may declare multiple variables, each with its own name), so the
    // inherited `Name` convenience is hidden and returns the empty string. The setter throws
    // (`NotSupportedException` ports as `std::logic_error`). Overrides the base
    // `EntityDeclaration::Name` virtual.
    std::string Name() const override { return std::string(); }
    void Name(std::string_view) override {
        throw std::logic_error("FieldDeclaration.Name is not supported");
    }

    // The C# `[EditorBrowsable(EditorBrowsableState.Never)] public override Identifier NameToken
    // { get { return null!; } set { throw new NotSupportedException(); } }` -- there is no single
    // name token for a field (the names are in the `VariableInitializer` children), so the
    // inherited `NameToken` is hidden and returns null. The setter throws. Overrides the base
    // `EntityDeclaration::NameToken` virtual.
    Identifier* NameToken() const override { return nullptr; }
    void NameToken(Identifier*) override {
        throw std::logic_error("FieldDeclaration.NameToken is not supported");
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection); `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`,
    // required -- the `ReturnType` `AstType` is non-nullable); `VariablesSlot` (a
    // `CSharpSlotInfoT<VariableInitializer>` pointing at `Slots.Variable`, collection). NO name
    // shadowing (no member is named `AttributeSection`/`AstType`/`VariableInitializer`), so the
    // element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> ReturnTypeSlot{"ReturnType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<VariableInitializer> VariablesSlot{"Variables", true, &Slots::Variable, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitFieldDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitFieldDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitFieldDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitFieldDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Three slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `ReturnType` single slot at slot 1 (index `attrCount`), and a
    // `Variables` collection at slot 2 (the range `[attrCount + 1, attrCount + 1 + varCount)`).
    // `GetChildCount` is `attrCount + 1 + varCount` (the one single slot plus both collections'
    // current lengths); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each
    // one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a collection
    // step, a single step, then a collection step). `GetCollectionByKind` returns each collection
    // for its kind. This is the `ComposedType` D242 collection -> single -> collection dispatch
    // shape applied to the `TypeMember` hierarchy.

    int GetChildCount() const override { return attributes_.Count() + 1 + variables_.Count(); }

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
        {
            int n = variables_.Count();
            if (i < n)
                return variables_.At(i);
        }
        throw std::out_of_range("FieldDeclaration::GetChild");
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
        {
            int n = variables_.Count();
            if (i < n) {
                variables_.SetAt(i, static_cast<VariableInitializer*>(value));
                return;
            }
        }
        throw std::out_of_range("FieldDeclaration::SetChild");
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
        {
            int n = variables_.Count();
            if (i < n)
                return &VariablesSlot;
        }
        throw std::out_of_range("FieldDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::Variable)
            return &variables_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is FieldDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && this.Variables.DoMatch(o.Variables, match)`. The terms are in `MembersToMatch`
    // order (the generator adds `Name`, `MatchAttributesAndModifiers`, and `ReturnType`
    // explicitly for every `EntityDeclaration`-derived node -- `NameToken` is NOT
    // `[ExcludeFromMatch]` on `FieldDeclaration` (it is `[EditorBrowsable]`, a different
    // attribute), so the `Name` `String` term IS added; then the per-property scan adds
    // `Variables`). The `Name` term is a `MatchString` over the always-empty `Name` (both return
    // `""`, so `MatchString("", "")` is vacuously true); the `MatchAttributesAndModifiers` helper
    // (on `EntityDeclaration`, protected) matches the `Modifiers` scalar (the `Any`-wildcard --
    // the generator detects the `Any` member by name, NOT via `[Flags]`, so the term is the plain
    // `==` value equality, NOT a bitmask test) AND the `Attributes` collection; the `ReturnType`
    // term is `MatchOptional` (nullable recursive -- the generator treats the `EntityDeclaration`
    // `ReturnType` uniformly as `MatchOptional`); the `Variables` term is the collection recursive
    // match (the generator emits the collection-typed recursive term directly, NOT `MatchOptional`).
    // A type-only mismatch (not a `FieldDeclaration`) rejects early. The `Name()` calls are
    // inlined in the `MatchString` arguments (the `MemberType` D238 precedent) so the C# `&&`
    // short-circuit is preserved; the `std::string` temporaries live until the end of the full
    // `return` expression, so the `std::string_view` views are valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<FieldDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && variables_.DoMatch(o->variables_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the faithful equivalent), the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `ReturnType` deep-cloned through the setter (which re-parents; `AstType::Clone()` returns
    // `AstType*`, which `ReturnType(AstType*)` accepts directly), and every `Attributes`/
    // `Variables` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `AttributeSection::Clone()` returns `AttributeSection*` and `VariableInitializer::Clone()`
    // returns `VariableInitializer*`, which the typed `Add`s accept directly). No own location
    // fields (`StartLocation`/`EndLocation` are the print-time base fields set by the unported
    // output visitor -- `FieldDeclaration` does not derive `EndLocation`), so they are not copied
    // (the `DestructorDeclaration` D272 / `FixedStatement` D267 no-location-copy precedent). The
    // covariant return is `FieldDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
    // `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written
    // partial; the covariant `FieldDeclaration*` is a valid override of `AstNode::Clone`).
    FieldDeclaration* Clone() const override {
        auto* node = new FieldDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (returnType_ != nullptr)
            node->ReturnType(returnType_->Clone());
        for (int i = 0; i < variables_.Count(); i++)
            node->variables_.Add(variables_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_`/`variables_` are the always-present collection members
    // (empty until the first `Add`, non-incremental); `returnType_` is null until the return type
    // is set (a REQUIRED slot -- `CheckInvariant` asserts it is filled). NO name shadowing (no
    // member is named `AttributeSection`/`AstType`/`VariableInitializer`), so the field types are
    // the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstType* returnType_ = nullptr;
    AstNodeCollectionT<VariableInitializer> variables_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_FIELDDECLARATION_HPP
