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
// OTHERWISE, ARISING,, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// DEALINGS IN THE SOFTWARE.

// Port of the `ConstructorInitializer` concrete node (and its `ConstructorInitializerType` enum)
// in ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/ConstructorDeclaration.cs (the generated
// `ConstructorInitializer.g.cs` + the hand-written partial, which declares the
// `ConstructorInitializerType` enum, the two const-string keyword tokens, the
// `ConstructorInitializerType` scalar, and the one slot property -- no ctors, no helpers).
// The next in-order Phase-5 piece per the D280 plan ("ConstructorDeclaration (NameToken +
// Parameters + ConstructorInitializer + Body -- needs ParameterDeclaration, now ported, plus
// ConstructorInitializer)") -- the dependency of `ConstructorDeclaration.Initializer` (its sole
// child slot is the `ConstructorInitializer?` the constructor declaration carries).
//
// `constructor_initializer ::= ':' ( 'base' | 'this' ) '(' expression* ')'` (C# grammar 15.11.1):
// the `: base(...)` / `: this(...)` initializer of a constructor declaration -- a sealed `AstNode`
// (deriving DIRECTLY from the `AstNode` root, NOT an `EntityDeclaration` -- an initializer is a
// structural node owned by a `ConstructorDeclaration`, not a member declaration; the
// `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final` -- no pattern
// placeholder). It carries a `ConstructorInitializerType` scalar (`Base`/`This`, the kind of
// initializer; `Any` is the zero value and the pattern-match wildcard) and an `Arguments`
// `AstNodeCollection<Expression>` (the argument expressions inside the `(...)`).
//
// The `ConstructorInitializerType` enum (`Any`, `Base`, `This`) is declared in the same
// `ConstructorDeclaration.cs` file (the `ICSharpCode.Decompiler.CSharp.Syntax` namespace), so it
// ports into this header at namespace scope BEFORE the class (the `AccessorKind`-in-`Accessor.hpp`
// D274 / `FieldDirection`-in-`DirectionExpression.hpp` D235 precedent). It declares an `Any`
// member (the zero value), so the generator's `hasAny` path fires and the generated `DoMatch` term
// is the `Any`-wildcard `(this.ConstructorInitializerType == ConstructorInitializerType.Any ||
// this.ConstructorInitializerType == o.ConstructorInitializerType)` (the
// `BinaryOperatorExpression.Operator` D229 / `Accessor.Kind` D274 `Any`-wildcard precedent; NOT a
// bitmask test -- the generator detects the `Any` member by name, not via `[Flags]`).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds the
// per-property scan of non-override non-`[ExcludeFromMatch]` members in source declaration order
// (`ConstructorInitializerType` the scalar, then `Arguments` the collection). So `MembersToMatch`
// is `[ConstructorInitializerType, Arguments]`, and the generated `DoMatch` is
// `return other is ConstructorInitializer o && (this.ConstructorInitializerType ==
// ConstructorInitializerType.Any || this.ConstructorInitializerType == o.ConstructorInitializerType)
// && this.Arguments.DoMatch(o.Arguments, match)`. The `ConstructorInitializerType` term is the
// `Any`-wildcard (the enum declares an `Any` member); the `Arguments` term is the collection
// recursive `DoMatch` (the generator emits a collection-typed recursive term directly, NOT
// `MatchOptional` -- the `FieldDeclaration.Variables` D273 / `OperatorDeclaration.Parameters`
// D280 precedent). A type-only mismatch (not a `ConstructorInitializer`) rejects early.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[ConstructorInitializerType (enum,
// required), Arguments (collection, optional)]`, `RequiredConstructorPrefixLength` is 1 (through
// the last non-optional param `ConstructorInitializerType`), `ConstructorPrefixLengths` is
// `{1, 2}`. The (len=1) ctor `(ConstructorInitializerType)` just sets
// `this.ConstructorInitializerType = type` (a plain scalar assignment, no `AddRange`), so it
// PORTS. The (len=2) ctor `(ConstructorInitializerType, IEnumerable<Expression>)` and its `params`
// overload both call `this.Arguments.AddRange(...)` (the `AddRange` convenience is the D222
// deferral), so they are DEFERRED. The empty ctor + the `(ConstructorInitializerType)`
// required-prefix ctor cover the generated construction API; an argument list is built via
// `Arguments().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitConstructorInitializer(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitConstructorInitializer`). The generated slot static is `ArgumentsSlot` (a
// `CSharpSlotInfoT<Expression>` pointing at `Slots.Argument`, collection). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// a fresh node, the `ConstructorInitializerType` scalar copied directly to the backing field, the
// annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
// pattern), and every `Arguments` element deep-cloned through `Add` (which re-parents and
// re-indexes; `Expression::Clone()` returns `Expression*`, which `Add(Expression*)` accepts
// directly).
//
// C++ name-shadowing crux (the `ConstructorInitializerType` property): the C# property is
// `ConstructorInitializerType` of type `ConstructorInitializerType` -- a property named the same
// as its enum type, the enum equivalent of the `Expression`-of-type-`Expression` D231 crux (the
// `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.OperatorType` D280 precedent).
// The faithful port names the accessor `ConstructorInitializerType()`, which SHADOWS the
// `ConstructorInitializerType` enum in this class scope (C++ unqualified name lookup finds the
// member and stops, even though it is not a type). The getter return type precedes the getter's
// own declaration, so the plain `ConstructorInitializerType` (the enum) is unshadowed in the
// getter signature; every type usage AFTER the `ConstructorInitializerType()` getter (the setter
// parameter, the backing field type) uses the ELABORATED enum specifier `enum
// ConstructorInitializerType` (basic.lookup.elab ignores non-type names, the enum equivalent of
// `class Expression`), and the backing-field initializer uses the fully-qualified enum name
// (`::ILSpy::...::ConstructorInitializerType::Any`) because the bare
// `ConstructorInitializerType::Any` would resolve the unqualified `ConstructorInitializerType`
// to the member function and reject `::Any` on a non-type (the D235 precedent). The `DoMatch`
// `Any`-wildcard term compares the backing fields directly
// (`constructorInitializerType_ == o->constructorInitializerType_` after the `Any`-wildcard
// short-circuit on the fully-qualified name), so no type name appears in the comparison body.
//
// NO new `Slots` constant for the `Arguments` collection: `Slots::Argument` is already ported (by
// `Attribute` D240), so the collection reuses it. The NEW `Slots::ConstructorInitializer` kind
// (a single NULLABLE `ConstructorInitializer` child -- the `ConstructorDeclaration.Initializer`
// slot kind) is cycle-broken into THIS header after the `ConstructorInitializer` class:
// `ConstructorInitializer.hpp` includes `Slots.hpp` for its per-node `ArgumentsSlot` (referencing
// `&Slots::Argument`), so a `CSharpSlotInfoT<ConstructorInitializer>` kind cannot live in
// `Slots.hpp` (a circular include -- with `Slots.hpp`'s guard set the `Slots::Argument` definition
// would not be visible where `ConstructorInitializer.hpp`'s class body needs it). After the
// `ConstructorInitializer` class both `CSharpSlotInfoT` (visible via the `Slots.hpp` include) and
// `ConstructorInitializer` are complete, so the kind defines cleanly. The `inline` variable has
// external linkage and one address across translation units (the C++17 `inline` guarantee),
// preserving the pointer-identity comparison `node.Slot.Kind == &Slots::ConstructorInitializer`
// the slot system relies on. The kind name `ConstructorInitializer` collides WITH the
// `ConstructorInitializer` class in the `Syntax` namespace (the `Expression`/`Identifier`/`Statement`
// collision pattern), but the kind is defined HERE (in its own element-type header), not in
// `Slots.hpp`, so no later `Slots` entry in `Slots.hpp` needs the elaborated specifier for it --
// the collision matters only for a later `Slots` entry wanting the `ConstructorInitializer` class
// as its element type defined in `Slots.hpp` after a `Slots::ConstructorInitializer` variable, and
// no such entry exists (the only consumer is `ConstructorDeclaration`'s `InitializerSlot` pointing
// AT `&Slots::ConstructorInitializer`).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRUCTORINITIALIZER_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRUCTORINITIALIZER_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum ConstructorInitializerType` -- the kind of a `ConstructorInitializer`
// (`: base(...)` or `: this(...)`). `Any` is the zero value and the pattern-match wildcard (the
// generator detects the `Any` member by name, so the `DoMatch` term is the plain `==` value
// equality with the `Any`-wildcard short-circuit -- the `BinaryOperatorExpression.Operator` D229
// / `AccessorKind` D274 `Any`-wildcard precedent; NOT `[Flags]`, so no bitwise operators). The
// values are in C# declaration order.
enum class ConstructorInitializerType {
    Any = 0,
    Base,
    This
};

// The C# `public sealed partial class ConstructorInitializer : AstNode`. `final` (the C#
// `sealed`): no further derivation. A structural node (a constructor initializer owned by a
// `ConstructorDeclaration`), not a `Statement`/`Expression`/`AstType`/`EntityDeclaration`. The
// `ConstructorInitializerType` scalar + the `Arguments` `Expression` collection, the
// collection-only shape (the `ArrayInitializerExpression` D250 shape plus a scalar -- the scalar is
// not a `[Slot]`, so it does not appear in the slot storage).
class ConstructorInitializer final : public AstNode {
public:
    ~ConstructorInitializer() override = default;

    // The generated empty ctor (the C# `public ConstructorInitializer()`). The `Arguments`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = true` (it is
    // the node's only collection and its last slot -- the only slot -- so an element's flattened
    // `ChildIndex` is exactly its local position). `ConstructorInitializerType` defaults to `Any`
    // (the enum's zero value, the C# default -- a wildcard initializer; a real initializer sets it
    // to `Base`/`This`).
    ConstructorInitializer() : arguments_(this, &ArgumentsSlot, 0, true) {}

    // The generated required-prefix ctor (the C# `public ConstructorInitializer(ConstructorInitializerType
    // type)`) -- the one required ctor param before the optional `Arguments` collection. Sets
    // `ConstructorInitializerType` directly (a plain scalar assignment, the C#
    // `this.ConstructorInitializerType = type`). Delegates to the empty ctor so the collection
    // member is initialized. `explicit` (a single-argument ctor is a converting ctor by default --
    // the `UnaryOperatorExpression` D231 / `Accessor` D274 single-arg-ctor precedent). The
    // parameter type is the plain `ConstructorInitializerType` (it precedes the
    // `ConstructorInitializerType()` getter declaration, so the enum is unshadowed here), and the
    // ctor body assigns the backing field DIRECTLY (not the ambiguous
    // `ConstructorInitializerType(type)` setter call that could parse as a functional cast of the
    // enum -- the `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.OperatorType`
    // D280 ctor-body-direct-assignment precedent).
    explicit ConstructorInitializer(ConstructorInitializerType type) : ConstructorInitializer() {
        constructorInitializerType_ = type;
    }

    // The C# `public const string BaseKeyword = "base"` / `ThisKeyword = "this"` -- the keyword
    // token literals the output visitor emits (CSharpOutputVisitor.VisitConstructorInitializer).
    // Compile-time literals carried as `static constexpr const char*` (static fields, not instance
    // state, so they are not part of `MembersToMatch`/`DoMatch` -- the `CheckedExpression.CheckedKeyword`
    // D234 / `OperatorDeclaration.OperatorKeyword` D280 precedent).
    static constexpr const char* BaseKeyword = "base";
    static constexpr const char* ThisKeyword = "this";

    // ---- The `ConstructorInitializerType` scalar (a settable enum, NOT a `[Slot]`) -----------
    // The C# `public ConstructorInitializerType ConstructorInitializerType { get; set; }` -- a
    // scalar enum (not a `[Slot]`). A settable enum-typed scalar the generator adds to
    // `MembersToMatch` (with the `Any`-wildcard term, since `ConstructorInitializerType` declares
    // an `Any` member) and to the ctor params (a settable enum-typed scalar is a required ctor
    // param -- the `VariableDeclarationStatement.Modifiers` D270 / `OperatorDeclaration.OperatorType`
    // D280 precedent). The return type precedes the getter's own declaration, so the plain
    // `ConstructorInitializerType` (the enum) is unshadowed in the getter signature.
    ConstructorInitializerType ConstructorInitializerType() const { return constructorInitializerType_; }
    // The setter parameter type uses the elaborated enum specifier `enum ConstructorInitializerType`:
    // the `ConstructorInitializerType()` getter declared just above shadows the
    // `ConstructorInitializerType` enum in this class scope (the D235 `FieldDirection` / D280
    // `OperatorType` precedent -- the enum equivalent of the `class Expression` elaborated
    // specifier), so the plain name would resolve to the member function (not a type).
    void ConstructorInitializerType(enum ConstructorInitializerType value) { constructorInitializerType_ = value; }

    // ---- The `Arguments` collection slot -----------------------------------------------
    // The generated `public partial AstNodeCollection<Expression> Arguments` -- the collection of
    // argument expressions (a `CSharpSlotInfoT<Expression>` slot at flattened index 0, the node's
    // only collection and last slot -- the only slot). The C# lazily allocates the wrapper; the
    // D222 port makes the collection an always-present stack member, so the accessor returns the
    // member directly (the empty-until-first-Add element-list profile is preserved). NO name
    // shadowing (no member is named `Expression`), so the element type is the plain `Expression`.
    AstNodeCollectionT<Expression>& Arguments() { return arguments_; }
    const AstNodeCollectionT<Expression>& Arguments() const { return arguments_; }

    // ---- The per-node slot static (pointing at the shared `Slots` kind) -----------------
    // The `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Argument`,
    // collection -- the node's only collection). NO name shadowing (no member is named
    // `Expression`), so the element type is the plain `Expression`.
    static inline const CSharpSlotInfoT<Expression> ArgumentsSlot{"Arguments", true, &Slots::Argument, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitConstructorInitializer`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitConstructorInitializer(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitConstructorInitializer`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitConstructorInitializer(this);
    }

    // ---- Slot storage (the generated overrides) -----------------------------------------
    // A single `Arguments` collection occupying the contiguous range [0, Count). `GetChildCount`
    // is the collection's current length (an empty node reports 0 -- the collection-only
    // `ArrayInitializerExpression` D250 shape); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the
    // single collection slot (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape with one collection step and no single step).
    // `GetCollectionByKind` returns the `Arguments` collection for the `Argument` kind. The
    // `ConstructorInitializerType` scalar is NOT a slot, so it does not appear here.

    int GetChildCount() const override { return arguments_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = arguments_.Count();
        if (i < n)
            return arguments_.At(i);
        throw std::out_of_range("ConstructorInitializer::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = arguments_.Count();
        if (i < n) {
            arguments_.SetAt(i, static_cast<Expression*>(value));
            return;
        }
        throw std::out_of_range("ConstructorInitializer::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = arguments_.Count();
        if (i < n)
            return &ArgumentsSlot;
        throw std::out_of_range("ConstructorInitializer::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Argument)
            return &arguments_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ConstructorInitializer o && (this.ConstructorInitializerType ==
    // ConstructorInitializerType.Any || this.ConstructorInitializerType ==
    // o.ConstructorInitializerType) && this.Arguments.DoMatch(o.Arguments, match)`. The terms are
    // in `MembersToMatch` order (the source declaration order: `ConstructorInitializerType` the
    // scalar, then `Arguments` the collection). The `ConstructorInitializerType` term is the
    // `Any`-wildcard (the enum declares an `Any` member; the generator detects it by name, so the
    // term is the plain `==` value equality with the `Any`-wildcard short-circuit -- `Any` matches
    // any candidate, a real kind matches only the exact same kind); the `Arguments` term is the
    // collection recursive `DoMatch` (the generator emits a collection-typed recursive term
    // directly, NOT `MatchOptional`). A type-only mismatch (not a `ConstructorInitializer`)
    // rejects early. The `Any`-wildcard uses the fully-qualified
    // `::ILSpy::...::ConstructorInitializerType::Any` (a qualified name is looked up in the named
    // namespace, NOT class scope, so it ignores the `ConstructorInitializerType()` member-function
    // shadowing; the D235 / D280 precedent); the second `==` compares the backing fields
    // directly (`constructorInitializerType_ == o->constructorInitializerType_`), so no type name
    // appears and no elaborated specifier is needed there.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ConstructorInitializer*>(other);
        if (o == nullptr)
            return false;
        return (constructorInitializerType_ ==
                    ::ILSpy::Decompiler::CSharp::Syntax::ConstructorInitializerType::Any
                || constructorInitializerType_ == o->constructorInitializerType_)
            && arguments_.DoMatch(o->arguments_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `ConstructorInitializerType` scalar copied
    // directly to the backing field (the `DirectionExpression.FieldDirection` D235
    // scalar-copy precedent -- the field is private to this class, so accessible from the
    // derived `Clone`), the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`,
    // the D223 concrete-clone pattern), and every `Arguments` element deep-cloned through `Add`
    // (which re-parents and re-indexes; `Expression::Clone()` returns `Expression*`, which
    // `Add(Expression*)` accepts directly). No own location fields (`StartLocation`/`EndLocation`
    // are the print-time base fields set by the unported output visitor -- `ConstructorInitializer`
    // does not derive `EndLocation`), so they are not copied (the `DestructorDeclaration` D272 /
    // `OperatorDeclaration` D280 no-location-copy precedent). The covariant return is
    // `ConstructorInitializer*` (through `AstNode*`, the `AstNode::Clone` virtual).
    ConstructorInitializer* Clone() const override {
        auto* node = new ConstructorInitializer();
        node->constructorInitializerType_ = constructorInitializerType_;
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < arguments_.Count(); i++)
            node->arguments_.Add(arguments_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `arguments_` is the always-present collection member (empty until the
    // first `Add`, incremental -- the node's only collection and last slot);
    // `constructorInitializerType_` is the scalar (defaults to `Any`, the enum's zero value -- the
    // C# default, a wildcard initializer). The `ConstructorInitializerType` scalar backing field
    // uses the elaborated enum specifier `enum ConstructorInitializerType` (the
    // `ConstructorInitializerType()` accessor declared above shadows the
    // `ConstructorInitializerType` enum in this class scope -- the D235 / D280 precedent); the
    // initializer uses the fully-qualified enum name because the bare
    // `ConstructorInitializerType::Any` would resolve the unqualified `ConstructorInitializerType`
    // to the member function and reject `::Any` on a non-type. `Any` is the enum's zero value (the
    // C# default).
    AstNodeCollectionT<Expression> arguments_;
    enum ConstructorInitializerType constructorInitializerType_ =
        ::ILSpy::Decompiler::CSharp::Syntax::ConstructorInitializerType::Any;
};

// The `ConstructorInitializer` kind -- a single NULLABLE `ConstructorInitializer` child (the
// `: base(...)` / `: this(...)` initializer of a `ConstructorDeclaration.Initializer`, absent for a
// constructor with no initializer -- `Foo() { }` with no `: base`/`: this`). Shared by every
// `[Slot("ConstructorInitializer")] ConstructorInitializer?` declaration. A
// `CSharpSlotInfoT<ConstructorInitializer>` (the element type is the concrete `ConstructorInitializer`
// node). Defined HERE (in ConstructorInitializer.hpp, after the `ConstructorInitializer` class)
// for the cycle-breaking reason: ConstructorInitializer.hpp includes Slots.hpp (for its per-node
// `ArgumentsSlot` referencing `&Slots::Argument`), so a `CSharpSlotInfoT<ConstructorInitializer>`
// kind cannot live in Slots.hpp -- a circular include (with Slots.hpp's guard set the
// `Slots::Argument` definition would not be visible where ConstructorInitializer.hpp's class body
// needs it). After the `ConstructorInitializer` class both `CSharpSlotInfoT` (visible via the
// Slots.hpp include) and `ConstructorInitializer` are complete, so the kind defines cleanly. The
// `inline` variable has external linkage and one address across translation units (the C++17
// `inline` guarantee), preserving the pointer-identity comparison
// `node.Slot.Kind == &Slots::ConstructorInitializer` the slot system relies on. The shared
// constant is constructed non-collection/non-optional (`{"ConstructorInitializer", false, nullptr,
// false}`); the per-node `InitializerSlot` on `ConstructorDeclaration` carries the
// `IsOptional=true` flag (the nullable slot -- the shared kind is constructed non-optional, the
// per-node slot carries the optionality, the `IfElseStatement.FalseStatement` D258 / `Slots::Getter`
// D276 precedent).
namespace Slots {
inline const CSharpSlotInfoT<ConstructorInitializer> ConstructorInitializer{"ConstructorInitializer", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_CONSTRUCTORINITIALIZER_HPP
