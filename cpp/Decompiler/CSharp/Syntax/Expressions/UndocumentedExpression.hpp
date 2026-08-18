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

// Port of the `UndocumentedExpression` concrete node (and its `UndocumentedExpressionType` enum)
// in ICSharpCode.Decompiler/CSharp/Syntax/Expressions/UndocumentedExpression.cs (the generated
// `UndocumentedExpression.g.cs` + the hand-written partial, which declares the
// `UndocumentedExpressionType` enum, the four const-string keyword tokens, the
// `UndocumentedExpressionType` scalar, and the one slot property -- no ctors, no helpers).
// The next in-order Phase-5 piece per the D300 plan (the remaining Expression nodes).
//
// `undocumented_expression ::= '__arglist' | '__arglist' '(' expression* ')' |
// '__refvalue' '(' expression ',' type ')' | '__reftype' '(' expression ')' |
// '__makeref' '(' expression ')'` (C# grammar, no spec production): the undocumented compiler
// keywords `__arglist`/`__refvalue`/`__reftype`/`__makeref` -- a sealed `Expression` carrying a
// `UndocumentedExpressionType` scalar (the kind of undocumented expression) and an `Arguments`
// `AstNodeCollection<Expression>` (the argument expressions inside the `(...)`, empty for the
// bare `__arglist` form).
//
// The `ConstructorInitializer` D281 collection-only-plus-enum-scalar shape applied to the
// `Expression` hierarchy (a sealed node whose sole child slot is a collection, plus a settable
// enum scalar that is NOT a `[Slot]`). The `UndocumentedExpressionType` enum
// (`ArgListAccess`/`ArgList`/`RefValue`/`RefType`/`MakeRef`) declares NO `Any` member, so the
// generator's `hasAny` path does NOT fire and the generated `DoMatch` term is the PLAIN `==`
// value equality (the `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.OperatorType`
// D280 no-`Any`-enum precedent -- NOT a wildcard, NOT a bitmask test).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds the
// per-property scan of non-override non-`[ExcludeFromMatch]` members in source declaration order
// (`UndocumentedExpressionType` the scalar, then `Arguments` the collection). So `MembersToMatch`
// is `[UndocumentedExpressionType, Arguments]`, and the generated `DoMatch` is
// `return other is UndocumentedExpression o && this.UndocumentedExpressionType ==
// o.UndocumentedExpressionType && this.Arguments.DoMatch(o.Arguments, match)`. The
// `UndocumentedExpressionType` term is the plain `==` (no `Any` member, so no wildcard); the
// `Arguments` term is the collection recursive `DoMatch` (the generator emits a
// collection-typed recursive term directly, NOT `MatchOptional` -- the `FieldDeclaration.Variables`
// D273 / `ConstructorInitializer.Arguments` D281 precedent). A type-only mismatch (not an
// `UndocumentedExpression`) rejects early.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[UndocumentedExpressionType (enum,
// required), Arguments (collection, optional)]`, `RequiredConstructorPrefixLength` is 1 (through
// the last non-optional param `UndocumentedExpressionType`), `ConstructorPrefixLengths` is
// `{1, 2}`. The (len=1) ctor `(UndocumentedExpressionType)` just sets
// `this.UndocumentedExpressionType = type` (a plain scalar assignment, no `AddRange`), so it
// PORTS. The (len=2) ctor `(UndocumentedExpressionType, IEnumerable<Expression>)` and its `params`
// overload both call `this.Arguments.AddRange(...)` (the `AddRange` convenience is the D222
// deferral), so they are DEFERRED. The empty ctor + the `(UndocumentedExpressionType)`
// required-prefix ctor cover the generated construction API; an argument list is built via
// `Arguments().Add(...)` until `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitUndocumentedExpression(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitUndocumentedExpression`). The generated slot static is `ArgumentsSlot` (a
// `CSharpSlotInfoT<Expression>` pointing at `Slots.Argument`, collection). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// a fresh node, the `UndocumentedExpressionType` scalar copied directly to the backing field, the
// annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
// pattern), and every `Arguments` element deep-cloned through `Add` (which re-parents and
// re-indexes; `Expression::Clone()` returns `Expression*`, the covariant override, which
// `Add(Expression*)` accepts directly).
//
// C++ name-shadowing crux (the `UndocumentedExpressionType` property): the C# property is
// `UndocumentedExpressionType` of type `UndocumentedExpressionType` -- a property named the same
// as its enum type, the enum equivalent of the `Expression`-of-type-`Expression` D231 crux (the
// `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.OperatorType` D280 /
// `Comment.CommentType` D288 precedent). The faithful port names the accessor
// `UndocumentedExpressionType()`, which SHADOWS the `UndocumentedExpressionType` enum in this
// class scope (C++ unqualified name lookup finds the member and stops, even though it is not a
// type). The getter return type precedes the getter's own declaration, so the plain
// `UndocumentedExpressionType` (the enum) is unshadowed in the getter signature; every type usage
// AFTER the `UndocumentedExpressionType()` getter (the setter parameter, the backing field type)
// uses the ELABORATED enum specifier `enum UndocumentedExpressionType` (basic.lookup.elab ignores
// non-type names, the enum equivalent of `class Expression`), and the backing-field initializer
// uses the fully-qualified enum name (`::ILSpy::...::UndocumentedExpressionType::ArgListAccess`)
// because the bare `UndocumentedExpressionType::ArgListAccess` would resolve the unqualified
// `UndocumentedExpressionType` to the member function and reject `::ArgListAccess` on a non-type
// (the D235 / D280 / D288 precedent). The `DoMatch` plain-`==` term compares the backing fields
// directly (`undocumentedExpressionType_ == o->undocumentedExpressionType_`), so no type name
// appears in the comparison body.
//
// NO new `Slots` constant for the `Arguments` collection: `Slots::Argument` is already ported (by
// `Attribute` D240), so the collection reuses it. `UndocumentedExpression` is not referenced as a
// slot element type by any other node (it is a leaf expression), so no cycle-broken
// `Slots::UndocumentedExpression` kind is needed.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNDOCUMENTEDEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNDOCUMENTEDEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum UndocumentedExpressionType` -- the kind of an `UndocumentedExpression`
// (`__arglist`/`__arglist(...)`/`__refvalue(...)`/`__reftype(...)`/`__makeref(...)`). NO `Any`
// member, so the generator's `hasAny` path does NOT fire and the generated `DoMatch` term is the
// PLAIN `==` value equality (NOT a wildcard, NOT a bitmask test -- the
// `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.OperatorType` D280 no-`Any`-enum
// precedent). The values are in C# declaration order; `ArgListAccess` is the zero value (the C#
// default -- the bare `__arglist` form).
enum class UndocumentedExpressionType {
    ArgListAccess = 0,
    ArgList,
    RefValue,
    RefType,
    MakeRef
};

// The C# `public sealed partial class UndocumentedExpression : Expression`. `final` (the C#
// `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults to false, so
// no `PatternPlaceholder` derives from it). The `ConstructorInitializer` D281
// collection-only-plus-enum-scalar shape applied to the `Expression` hierarchy: the
// `UndocumentedExpressionType` scalar + the `Arguments` `Expression` collection (the scalar is
// not a `[Slot]`, so it does not appear in the slot storage).
class UndocumentedExpression final : public Expression {
public:
    ~UndocumentedExpression() override = default;

    // The generated empty ctor (the C# `public UndocumentedExpression()`). The `Arguments`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = true` (it is
    // the node's only collection and its last slot -- the only slot -- so an element's flattened
    // `ChildIndex` is exactly its local position). `UndocumentedExpressionType` defaults to
    // `ArgListAccess` (the enum's zero value, the C# default -- the bare `__arglist` form).
    UndocumentedExpression() : arguments_(this, &ArgumentsSlot, 0, true) {}

    // The generated required-prefix ctor (the C# `public UndocumentedExpression(
    // UndocumentedExpressionType type)`) -- the one required ctor param before the optional
    // `Arguments` collection. Sets `UndocumentedExpressionType` directly (a plain scalar
    // assignment, the C# `this.UndocumentedExpressionType = type`). Delegates to the empty ctor
    // so the collection member is initialized. `explicit` (a single-argument ctor is a converting
    // ctor by default -- the `UnaryOperatorExpression` D231 / `Accessor` D274 /
    // `ConstructorInitializer` D281 single-arg-ctor precedent). The parameter type is the plain
    // `UndocumentedExpressionType` (it precedes the `UndocumentedExpressionType()` getter
    // declaration, so the enum is unshadowed here), and the ctor body assigns the backing field
    // DIRECTLY (not the ambiguous `UndocumentedExpressionType(type)` setter call that could parse
    // as a functional cast of the enum -- the `DirectionExpression.FieldDirection` D235 /
    // `OperatorDeclaration.OperatorType` D280 / `ConstructorInitializer.ConstructorInitializerType`
    // D281 ctor-body-direct-assignment precedent).
    explicit UndocumentedExpression(UndocumentedExpressionType type) : UndocumentedExpression() {
        undocumentedExpressionType_ = type;
    }

    // The C# `public const string ArglistKeyword = "__arglist"` / `RefvalueKeyword = "__refvalue"` /
    // `ReftypeKeyword = "__reftype"` / `MakerefKeyword = "__makeref"` -- the keyword token literals
    // the output visitor emits (CSharpOutputVisitor.VisitUndocumentedExpression). Compile-time
    // literals carried as `static constexpr const char*` (static fields, not instance state, so
    // they are not part of `MembersToMatch`/`DoMatch` -- the `CheckedExpression.CheckedKeyword`
    // D234 / `ConstructorInitializer.BaseKeyword` D281 precedent).
    static constexpr const char* ArglistKeyword = "__arglist";
    static constexpr const char* RefvalueKeyword = "__refvalue";
    static constexpr const char* ReftypeKeyword = "__reftype";
    static constexpr const char* MakerefKeyword = "__makeref";

    // ---- The `UndocumentedExpressionType` scalar (a settable enum, NOT a `[Slot]`) -----------
    // The C# `public UndocumentedExpressionType UndocumentedExpressionType { get; set; }` -- a
    // scalar enum (not a `[Slot]`). A settable enum-typed scalar the generator adds to
    // `MembersToMatch` (with the plain-`==` term, since `UndocumentedExpressionType` declares NO
    // `Any` member) and to the ctor params (a settable enum-typed scalar is a required ctor param
    // -- the `VariableDeclarationStatement.Modifiers` D270 / `OperatorDeclaration.OperatorType`
    // D280 precedent). The return type precedes the getter's own declaration, so the plain
    // `UndocumentedExpressionType` (the enum) is unshadowed in the getter signature.
    UndocumentedExpressionType UndocumentedExpressionType() const { return undocumentedExpressionType_; }
    // The setter parameter type uses the elaborated enum specifier `enum UndocumentedExpressionType`:
    // the `UndocumentedExpressionType()` getter declared just above shadows the
    // `UndocumentedExpressionType` enum in this class scope (the D235 `FieldDirection` / D280
    // `OperatorType` / D288 `CommentType` precedent -- the enum equivalent of the `class Expression`
    // elaborated specifier), so the plain name would resolve to the member function (not a type).
    void UndocumentedExpressionType(enum UndocumentedExpressionType value) { undocumentedExpressionType_ = value; }

    // ---- The `Arguments` collection slot -----------------------------------------------
    // The generated `[Slot("Argument")] public partial AstNodeCollection<Expression> Arguments` --
    // the collection of argument expressions (a `CSharpSlotInfoT<Expression>` slot at flattened
    // index 0, the node's only collection and last slot -- the only slot). The C# lazily allocates
    // the wrapper; the D222 port makes the collection an always-present stack member, so the
    // accessor returns the member directly (the empty-until-first-Add element-list profile is
    // preserved). NO name shadowing (no member is named `Expression`), so the element type is the
    // plain `Expression`.
    AstNodeCollectionT<Expression>& Arguments() { return arguments_; }
    const AstNodeCollectionT<Expression>& Arguments() const { return arguments_; }

    // ---- The per-node slot static (pointing at the shared `Slots` kind) -----------------
    // The `ArgumentsSlot` (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Argument`,
    // collection -- the node's only collection). NO name shadowing (no member is named
    // `Expression`), so the element type is the plain `Expression`.
    static inline const CSharpSlotInfoT<Expression> ArgumentsSlot{"Arguments", true, &Slots::Argument, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitUndocumentedExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitUndocumentedExpression(this);
    }

    // ---- Slot storage (the generated overrides) -----------------------------------------
    // A single `Arguments` collection occupying the contiguous range [0, Count). `GetChildCount`
    // is the collection's current length (an empty node reports 0 -- the collection-only
    // `ArrayInitializerExpression` D250 / `ConstructorInitializer` D281 shape); `GetChild`/
    // `SetChild`/`GetChildSlotInfo` walk the single collection slot (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape with one
    // collection step and no single step). `GetCollectionByKind` returns the `Arguments`
    // collection for the `Argument` kind. The `UndocumentedExpressionType` scalar is NOT a slot,
    // so it does not appear here.

    int GetChildCount() const override { return arguments_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = arguments_.Count();
        if (i < n)
            return arguments_.At(i);
        throw std::out_of_range("UndocumentedExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = arguments_.Count();
        if (i < n) {
            arguments_.SetAt(i, static_cast<Expression*>(value));
            return;
        }
        throw std::out_of_range("UndocumentedExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = arguments_.Count();
        if (i < n)
            return &ArgumentsSlot;
        throw std::out_of_range("UndocumentedExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Argument)
            return &arguments_;
        return Expression::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is UndocumentedExpression o && this.UndocumentedExpressionType ==
    // o.UndocumentedExpressionType && this.Arguments.DoMatch(o.Arguments, match)`. The terms are
    // in `MembersToMatch` order (the source declaration order: `UndocumentedExpressionType` the
    // scalar, then `Arguments` the collection). The `UndocumentedExpressionType` term is the
    // plain `==` (the enum declares NO `Any` member, so the generator emits the plain value
    // equality -- the `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.OperatorType`
    // D280 no-`Any`-enum precedent -- NOT a wildcard); the `Arguments` term is the collection
    // recursive `DoMatch` (the generator emits a collection-typed recursive term directly, NOT
    // `MatchOptional`). A type-only mismatch (not an `UndocumentedExpression`) rejects early.
    // The plain-`==` compares the backing fields directly
    // (`undocumentedExpressionType_ == o->undocumentedExpressionType_`), so no type name appears
    // in the comparison body and no elaborated specifier is needed there.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<UndocumentedExpression*>(other);
        if (o == nullptr)
            return false;
        return undocumentedExpressionType_ == o->undocumentedExpressionType_
            && arguments_.DoMatch(o->arguments_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `UndocumentedExpressionType` scalar copied
    // directly to the backing field (the `DirectionExpression.FieldDirection` D235 /
    // `ConstructorInitializer.ConstructorInitializerType` D281 scalar-copy precedent -- the field
    // is private to this class, so accessible from the derived `Clone`), the annotation channel
    // copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and
    // every `Arguments` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `Expression::Clone()` returns `Expression*`, the covariant override, which
    // `Add(Expression*)` accepts directly). No own location fields (`StartLocation`/`EndLocation`
    // are the print-time base fields set by the unported output visitor -- `UndocumentedExpression`
    // does not derive `EndLocation`), so they are not copied (the `ConstructorInitializer` D281 /
    // `DestructorDeclaration` D272 no-location-copy precedent). The covariant return is
    // `UndocumentedExpression*` (through `Expression*`, the `Expression::Clone` pure-virtual).
    UndocumentedExpression* Clone() const override {
        auto* node = new UndocumentedExpression();
        node->undocumentedExpressionType_ = undocumentedExpressionType_;
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < arguments_.Count(); i++)
            node->arguments_.Add(arguments_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `arguments_` is the always-present collection member (empty until the
    // first `Add`, incremental -- the node's only collection and last slot);
    // `undocumentedExpressionType_` is the scalar (defaults to `ArgListAccess`, the enum's zero
    // value -- the C# default, the bare `__arglist` form). The `UndocumentedExpressionType` scalar
    // backing field uses the elaborated enum specifier `enum UndocumentedExpressionType` (the
    // `UndocumentedExpressionType()` accessor declared above shadows the `UndocumentedExpressionType`
    // enum in this class scope -- the D235 / D280 / D288 precedent); the initializer uses the
    // fully-qualified enum name because the bare `UndocumentedExpressionType::ArgListAccess` would
    // resolve the unqualified `UndocumentedExpressionType` to the member function and reject
    // `::ArgListAccess` on a non-type. `ArgListAccess` is the enum's zero value (the C# default).
    AstNodeCollectionT<Expression> arguments_;
    enum UndocumentedExpressionType undocumentedExpressionType_ =
        ::ILSpy::Decompiler::CSharp::Syntax::UndocumentedExpressionType::ArgListAccess;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_UNDOCUMENTEDEXPRESSION_HPP
