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

// Port of the `VariableDeclarationStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.cs (the generated
// `VariableDeclarationStatement.g.cs` + the hand-written partial). The next in-order Phase-5 piece
// per the D269 plan ("VariableDeclarationStatement (now unblocked -- it needs `VariableInitializer`
// for its `Variables` collection, already ported by D266, plus a `Modifiers` enum scalar)").
//
// `local_variable_declaration ::= type variable_initializer+` (C# grammar 13.6.2.1): a sealed
// `Statement` with a single REQUIRED `AstType Type` `[Slot]` child at flattened index 0 (the
// declared type -- the `UnaryOperatorExpression` D231 required-single-`AstType`-slot shape, reusing
// the already-ported `Slots::Type` kind), a `Variables AstNodeCollection<VariableInitializer>`
// collection `[Slot("Variable")]` at slot 1 (the comma-separated `name = initializer` declarators,
// reusing the cycle-broken `Slots::Variable` kind added by `FixedStatement` D267), and a `Modifiers`
// scalar (a PLAIN settable `Modifiers`-typed property, NOT a `[Slot]` -- the first ported node to
// carry a `[Flags]` enum scalar and the first to carry a non-`[Slot]` scalar that is not a const
// string). The `Variables` collection is the node's only collection and its last slot, so
// `supportsIncremental` is TRUE (`collectionCount == 1 && slotIndex == 1 == slots.Count - 1`): an
// element's flattened `ChildIndex` is exactly `1 + its local position`, maintained incrementally by
// `Add`/`Insert`/`Remove`, unlike `FixedStatement` (D267) whose `Variables` is non-incremental (a
// trailing `EmbeddedStatement` single slot follows it). The `Type` single slot PRECEDES the
// collection, so its setter uses the const-index `SetChildNode(ref field, value, 0)`.
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is VariableDeclarationStatement o &&
// (this.Modifiers == Modifiers.Any || this.Modifiers == o.Modifiers) &&
// this.Type.DoMatch(o.Type, match) && this.Variables.DoMatch(o.Variables, match)`. The three terms
// are in the C# source property declaration order (`Modifiers`, then `Type`, then `Variables`):
// the `Modifiers` term is a settable enum with an `Any` member, so the generator emits the
// `Any`-wildcard term (`(this.Modifiers == Modifiers.Any || this.Modifiers == o.Modifiers)` -- the
// `BinaryOperatorExpression.Operator` D229 `Any`-wildcard precedent applied to a `[Flags]` enum;
// the `[Flags]` attribute does not change the generator's behaviour, it detects `Any` by name, so
// the term is the plain `==` value equality, NOT a bitmask test); the `Type` term is a non-nullable
// recursive child, so the generator emits a DIRECT `this.Type.DoMatch(o.Type, match)` -- ported
// through `MatchRequired` (the D231 [class.access.derived] workaround, since a derived node may not
// call the protected `DoMatch` through a base `AstType*`); the `Variables` term is the collection
// recursive match (the generator emits the collection-typed recursive term directly, NOT
// `MatchOptional`, which it emits only for a nullable non-collection child). A type-only mismatch
// (not a `VariableDeclarationStatement`) rejects early.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitVariableDeclarationStatement(this)` (the class name does not end in "AstType", so
// the generator's visit-method-name default yields `VisitVariableDeclarationStatement`). The
// generated slot statics are `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`,
// required -- the `Type` `AstType` is non-nullable) and `VariablesSlot` (a
// `CSharpSlotInfoT<VariableInitializer>` pointing at `Slots.Variable`, collection). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied, the annotation channel copied
// (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `Type`
// deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`, the
// covariant override), and every `Variables` element deep-cloned through `Add` (which re-parents
// and re-indexes; `VariableInitializer::Clone()` returns `VariableInitializer*`, which
// `Add(VariableInitializer*)` accepts directly).
//
// A C++ name-shadowing crux on the `Modifiers` accessor (the D235 `DirectionExpression.
// FieldDirection` precedent -- the enum equivalent of the `class Expression` elaborated-type-
// specifier crux): the `Modifiers()` getter (a member function) shadows the `Modifiers`
// `enum class` in this class scope (C++ unqualified name lookup finds the member and stops, even
// though it is not a type), so the setter parameter type, the `DoMatch` `Any`-wildcard term, and
// the backing-field declaration/initializer use the elaborated `enum Modifiers` specifier (in type
// positions) and the fully-qualified `::ILSpy::...::Modifiers::...` name (in qualified-name
// positions where the elaborated specifier cannot apply, e.g. `Modifiers::Any`). The `Type()`/
// `Variables()` accessors do NOT collide with their element types (`AstType`/`VariableInitializer`
// -- no member is named those, and no class named `Type`/`Variables` lives in the `Syntax`
// namespace, the `ObjectCreateExpression` D251 / `FixedStatement` D267 differently-named-property
// precedent), so the plain `AstType`/`VariableInitializer` resolve to the classes there.
//
// NO new `Slots` constant: `Slots::Type` is already ported (by `Attribute` D240), and
// `Slots::Variable` is already cycle-broken into `VariableInitializer.hpp` (by `FixedStatement`
// D267), so `Slots.hpp` is unchanged. The `Modifiers` enum is NEW and lives in its own header
// `Modifiers.hpp` (a shared enum used by `VariableDeclarationStatement` and the future
// `TypeMember`/`EntityDeclaration` hierarchy), so this node adds the `Modifiers.hpp` include.
//
// The generated ctors (the generator's `WriteConstructors`): the `Modifiers` settable-enum scalar
// and the `Type` required `[Slot]` are both required ctor params (the generator adds a settable
// enum-typed scalar to `CtorParams` with `IsOptional:false`, and a non-nullable `[Slot]` with
// `IsOptional:false`); the `Variables` collection is an optional collection param. So
// `CtorParams` is `[Modifiers, Type, Variables]`, `RequiredConstructorPrefixLength` is 2 (through
// the last non-optional param `Type`), and `ConstructorPrefixLengths` is {2, 3} (the required
// prefix then the full count, with the collection-prefix length 3 from the `Variables` collection
// at `i == 2`, `i + 1 >= reqLen`). The generated ctors are therefore the empty ctor + the
// `(Modifiers, AstType)` required-prefix ctor + the `(Modifiers, AstType,
// IEnumerable<VariableInitializer>)` all-params ctor + the `params VariableInitializer[]`
// overload. The all-params and the `params` forms call `this.Variables.AddRange(...)`, which lands
// with the collection convenience mutators (the D222 deferral), so they are DEFERRED; the empty +
// the `(Modifiers, AstType)` required-prefix ctors cover the generated construction API.
//
// The HAND-WRITTEN convenience ctor `VariableDeclarationStatement(AstType type, string name,
// Expression? initializer = null)` (declared in the C# partial) creates a single
// `VariableInitializer(name, initializer)` and adds it via `this.Variables.Add(...)` -- it uses
// `Add` (already ported), NOT `AddRange` (deferred), and the `VariableInitializer(string, Expression*)`
// 2-arg ctor is already ported (D266), so the convenience ctor ports now. The 3-arg form with the
// default `initializer = nullptr` matches the C# `Expression? initializer = null`; the default is
// `nullptr` (no initializer -- the bare `type name;` form). The 2-arg form (`(AstType, string)`)
// is NOT separately declared -- the C# default argument `initializer = null` covers it, and C++
// default arguments work identically (a call `VariableDeclarationStatement(t, "x")` fills
// `initializer = nullptr`). The convenience ctor is NOT `explicit` (a multi-arg ctor is not a
// converting ctor); the `(Modifiers, AstType)` generated required-prefix ctor is likewise NOT
// `explicit` (the `IfElseStatement` D258 `(Expression, Statement)` 2-arg precedent -- a multi-arg
// ctor is not a converting ctor).
//
// The hand-written `GetVariable(string name)` helper (a `VariableInitializer?` lookup by name) is
// DEFERRED: it is a behaviour helper (`Variables.FirstOrNull(...)`) consumed by the resolver/output
// stage, not part of the node structure, and `FirstOrNull` is itself a deferred collection
// convenience (the D222 deferral). It lands when that stage consumes it.
//
// The port's node model uses NON-OWNING raw-pointer child slots (the D223 design: the parent does
// not take ownership; the test's `unique_ptr`s own the nodes). The hand-written convenience ctor
// creates a `new VariableInitializer(...)` and adds it to `Variables`; the created `VariableInitializer`
// is therefore NOT owned by any `unique_ptr` and is not deleted when the `VariableDeclarationStatement`
// is destroyed -- the same loose-ownership profile as a `Clone` deep-copy (the cloned children are
// `new`-ed by `Clone` and not deleted by the parent's `= default` destructor). This is consistent
// with the established non-owning model; tests that exercise the convenience ctor keep the parent
// alive in a `unique_ptr` and let the internally-created `VariableInitializer` leak alongside any
// `Clone`-produced subtree.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_VARIABLEDECLARATIONSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_VARIABLEDECLARATIONSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class VariableDeclarationStatement : Statement`. `final` (the C#
// `sealed`): no further derivation. A single `Type` slot + a `Variables` collection + a `Modifiers`
// scalar (the `local_variable_declaration ::= type variable_initializer+` shape).
class VariableDeclarationStatement final : public Statement {
public:
    ~VariableDeclarationStatement() override = default;

    // The generated empty ctor (the C# `public VariableDeclarationStatement()`). The `Variables`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 1` (the `Type` single slot at slot 0 precedes it) and `supportsIncremental = true`
    // (the collection is the node's only collection and its last slot -- nothing follows it -- so
    // an element's flattened `ChildIndex` is exactly `1 + its local position`, maintained
    // incrementally by `Add`/`Insert`/`Remove`). `Type` defaults to null (no type) and `Modifiers`
    // defaults to `None` (the enum's zero value, the C# default -- a declaration with no modifiers).
    VariableDeclarationStatement() : variables_(this, &VariablesSlot, 1, true) {}

    // The generated required-prefix ctor (the C# `public VariableDeclarationStatement(Modifiers,
    // AstType)`) -- the two required ctor params before the optional `Variables` collection. Sets
    // `Modifiers` and `Type` in declaration order. Delegates to the empty ctor so the collection
    // member is initialized. NOT `explicit` (a multi-arg ctor is not a converting ctor -- the
    // `IfElseStatement` D258 `(Expression, Statement)` 2-arg precedent). The `Modifiers` scalar is
    // assigned DIRECTLY to the backing field (not via the `Modifiers(modifiers)` setter call)
    // because that call is ambiguous with a functional cast of the enum (`Modifiers(modifiers)` --
    // the `DirectionExpression` D235 `FieldDirection(fieldDirection)` ambiguity precedent); the
    // `Type` child is set through its setter so the slot machinery re-parents and re-indexes it.
    // The `Modifiers` ctor-parameter type precedes the `Modifiers()` getter (the ctor is declared
    // before the getter), so the plain `Modifiers` (the enum) is unshadowed here.
    VariableDeclarationStatement(Modifiers modifiers, AstType* type)
        : VariableDeclarationStatement() {
        modifiers_ = modifiers;
        Type(type);
    }

    // The HAND-WRITTEN convenience ctor (the C# `public VariableDeclarationStatement(AstType type,
    // string name, Expression? initializer = null)`) -- the common `type name = initializer;`
    // shape that creates a single `VariableInitializer` and adds it. Uses `Variables().Add(...)`
    // (already ported), NOT `AddRange` (deferred); the `VariableInitializer(string, Expression*)`
    // 2-arg ctor is already ported (D266). The default `initializer = nullptr` matches the C#
    // `Expression? initializer = null` (the bare `type name;` form). NOT `explicit` (a multi-arg
    // ctor is not a converting ctor). The created `VariableInitializer` is `new`-ed and added to
    // the collection; the port's non-owning model does not delete it on the parent's destruction
    // (the `Clone`-produced-subtree leak profile -- see the file header).
    VariableDeclarationStatement(AstType* type, std::string name, Expression* initializer = nullptr)
        : VariableDeclarationStatement() {
        Type(type);
        variables_.Add(new VariableInitializer(std::move(name), initializer));
    }

    // ---- The `Modifiers` scalar (a settable `[Flags]` enum, NOT a `[Slot]`) ------------------
    // The C# `public Modifiers Modifiers { get; set; }` -- a plain settable `Modifiers`-typed
    // property (no `[Slot]` attribute). The generator adds it to `MembersToMatch` as a `MatchAny`
    // term (the enum declares an `Any` member) and to the ctor params (a settable enum-typed
    // scalar is a required ctor param). A `Modifiers`-typed scalar (the `enum class`); the return
    // type precedes the getter's own declaration, so the plain `Modifiers` (the enum) is unshadowed
    // in the getter signature.
    Modifiers Modifiers() const { return modifiers_; }
    // The setter parameter type uses the elaborated enum specifier `enum Modifiers`: the
    // `Modifiers()` getter declared just above shadows the `Modifiers` `enum class` in this class
    // scope (the D235 `DirectionExpression.FieldDirection` / `enum FieldDirection` precedent --
    // the enum equivalent of the `class Expression` elaborated specifier), so the plain name
    // would resolve to the member function (not a type).
    void Modifiers(enum Modifiers value) { modifiers_ = value; }

    // ---- The `Type` slot (a single REQUIRED `AstType` child) -------------------------------
    // The generated `[Slot("Type")] public partial AstType Type` -- a single non-nullable `AstType`
    // slot at flattened index 0. The const-index `SetChildNode(ref field, value, 0)` setter (no
    // collection precedes it -- the `Type` slot is the first slot) re-parents and re-indexes in
    // place. NO name shadowing (the `Type()` accessor does NOT collide with the `AstType` base type
    // -- no class named `Type` lives in the `Syntax` namespace, and no member is named `AstType` --
    // the `Attribute` D240 / `FixedStatement` D267 differently-named-property precedent), so the
    // operand type is the plain `AstType` (no elaborated specifier).
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // ---- The `Variables` collection slot --------------------------------------------------
    // The generated `[Slot("Variable")] public partial AstNodeCollection<VariableInitializer>
    // Variables` -- the collection of `name = initializer` declarators (a
    // `CSharpSlotInfoT<VariableInitializer>` slot at slot index 1). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly (the empty-until-first-`Add` element-list profile is preserved).
    // `supportsIncremental` is `true` (the collection is the node's only collection and its last
    // slot -- nothing follows it), so `Add`/`Insert`/`Remove` maintain each element's flattened
    // `ChildIndex` incrementally as `1 + its local position`.
    AstNodeCollectionT<VariableInitializer>& Variables() { return variables_; }
    const AstNodeCollectionT<VariableInitializer>& Variables() const { return variables_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------------
    // The `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required -- the `Type`
    // `AstType` is non-nullable); the `VariablesSlot` (a `CSharpSlotInfoT<VariableInitializer>`
    // pointing at `Slots.Variable`, collection -- the `[Slot("Variable")]` collection). NO name
    // shadowing (`AstType`/`VariableInitializer` resolve to the classes -- no member is named
    // either), so the element types are the plain classes. `Slots::Type` is already ported (by
    // `Attribute` D240); `Slots::Variable` is cycle-broken into `VariableInitializer.hpp` (by
    // `FixedStatement` D267).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<VariableInitializer> VariablesSlot{"Variables", true, &Slots::Variable, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitVariableDeclarationStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitVariableDeclarationStatement(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Two slots in declaration order: a `Type` single slot at index 0 and a `Variables` collection
    // occupying the contiguous range `[1, 1 + Count)` (the collection is the last slot, so
    // `GetChildCount` is `1 + Count`). `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots
    // subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single step
    // then a collection step). `GetCollectionByKind` returns the `Variables` collection for the
    // `Variable` kind (the node's only collection). This is the `InvocationExpression` (D248)
    // single -> collection dispatch shape (incremental).

    int GetChildCount() const override { return 1 + variables_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return type_;
        i--;
        int n = variables_.Count();
        if (i < n)
            return variables_.At(i);
        throw std::out_of_range("VariableDeclarationStatement::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(type_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        int n = variables_.Count();
        if (i < n) {
            variables_.SetAt(i, static_cast<VariableInitializer*>(value));
            return;
        }
        throw std::out_of_range("VariableDeclarationStatement::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &TypeSlot;
        i--;
        int n = variables_.Count();
        if (i < n)
            return &VariablesSlot;
        throw std::out_of_range("VariableDeclarationStatement::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Variable)
            return &variables_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is VariableDeclarationStatement o &&
    // (this.Modifiers == Modifiers.Any || this.Modifiers == o.Modifiers) &&
    // this.Type.DoMatch(o.Type, match) && this.Variables.DoMatch(o.Variables, match)`. The three
    // terms are in `MembersToMatch` order (the C# source property declaration order: `Modifiers`,
    // then `Type`, then `Variables`). The `Modifiers` term is the `Any`-wildcard (the enum declares
    // an `Any` member; the `[Flags]` attribute does not change the generator, it detects `Any` by
    // name, so the term is the plain `==` value equality, NOT a bitmask test -- `Any` matches any
    // candidate, a real modifier matches only the exact same modifier); the `Type` term is a
    // non-nullable recursive child, dispatched through `MatchRequired` (the D231
    // [class.access.derived] workaround); the `Variables` term is the collection recursive match.
    // A type-only mismatch (not a `VariableDeclarationStatement`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<VariableDeclarationStatement*>(other);
        if (o == nullptr)
            return false;
        // The `Modifiers::Any` uses the fully-qualified enum name because the `Modifiers()` getter
        // shadows the `Modifiers` `enum class` in this scope (the D235 precedent: bare
        // `Modifiers::Any` would resolve the unqualified `Modifiers` to the member function and
        // reject `::Any` on a non-type).
        return (modifiers_ == ::ILSpy::Decompiler::CSharp::Syntax::Modifiers::Any
                || modifiers_ == o->modifiers_)
            && MatchRequired(type_, o->type_, match)
            && variables_.DoMatch(o->variables_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied, the annotation channel
    // copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `Type` deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`,
    // the covariant override), and every `Variables` element deep-cloned through `Add` (which
    // re-parents and re-indexes; `VariableInitializer::Clone()` returns `VariableInitializer*`,
    // which `Add(VariableInitializer*)` accepts directly). No own location fields
    // (`StartLocation`/`EndLocation` are the print-time base fields set by the unported output
    // visitor -- `VariableDeclarationStatement` does not derive `EndLocation`), so they are not
    // copied (the `FixedStatement` D267 no-location-copy precedent). The covariant return is
    // `VariableDeclarationStatement*` (through `Statement*`, the `Statement::Clone` pure-virtual).
    VariableDeclarationStatement* Clone() const override {
        auto* node = new VariableDeclarationStatement();
        node->modifiers_ = modifiers_;
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        for (int i = 0; i < variables_.Count(); i++)
            node->variables_.Add(variables_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `type_` is null until the type is set (a required slot --
    // `CheckInvariant` asserts it is filled); `variables_` is the always-present collection member
    // (empty until the first `Add`, incremental); `modifiers_` is the scalar (defaults to `None`,
    // the enum's zero value -- a declaration with no modifiers). The `Modifiers` scalar backing
    // field uses the elaborated enum specifier `enum Modifiers` (the `Modifiers()` accessor declared
    // above shadows the `Modifiers` `enum class` in this class scope -- the D235 precedent); the
    // initializer uses the fully-qualified enum name because the bare `Modifiers::None` would
    // resolve the unqualified `Modifiers` to the member function and reject `::None` on a non-type.
    // `None` is the enum's zero value (the C# default). NO name shadowing on `type_`/`variables_`
    // (no member is named `AstType`/`VariableInitializer`), so those field types are the plain
    // classes.
    enum Modifiers modifiers_ = ::ILSpy::Decompiler::CSharp::Syntax::Modifiers::None;
    AstType* type_ = nullptr;
    AstNodeCollectionT<VariableInitializer> variables_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_VARIABLEDECLARATIONSTATEMENT_HPP
