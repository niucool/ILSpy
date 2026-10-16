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

// Port of the `IdentifierExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.cs (the generated
// `IdentifierExpression.g.cs` + the hand-written partial). The first AstType-bearing
// `Expression` node with a COLLECTION slot (PORT_PLAN.md section 5.2 / decision D1: port the
// generated *output* by hand) -- the next in-order Phase-5 piece per the D245 plan
// ("IdentifierExpression -- TypeArguments AstNodeCollection<AstType> + its Identifier
// string-name [Slot] -- the first AstType-bearing Expression with a collection, reusing
// the now-ported Slots::TypeArgument and Slots::Identifier kinds").
//
// `simple_name ::= identifier ( '<' type ( ',' type )* '>' )?` (C# grammar 12.8.4): an
// `Expression` consisting of an `Identifier` name plus zero or more `TypeArguments` (`AstType`
// children). It is structurally identical to `SimpleType` (D237) -- the first ported node with a
// COLLECTION slot (`TypeArguments`: `AstNodeCollection<AstType>`) and a string-name `[Slot]`
// (the `Identifier`, over a backing `IdentifierToken`) -- but derives from `Expression`
// (parallel to `AstType`, not under it) and the `Identifier` is a NON-nullable `string` (the
// C# source declares `public partial string Identifier`, not `string?`). So the backing token
// is a REQUIRED (non-nullable) slot (unlike `SimpleType.IdentifierToken` which is optional), the
// string getter DEREFS the token (returning `std::string`, not `std::optional<std::string>` --
// a null token is a half-constructed node that would `NullReferenceException` in C#), and the
// string setter uses `Identifier.Create` (NOT `CreateIfNotEmpty` -- a non-nullable name creates
// a token even for an empty string, so an empty name yields a token with an empty `Name`, not a
// null token -- the `MemberType.MemberName` D238 precedent). It is the first AstType-bearing
// `Expression` to exercise the collection-aware `GetChild`/`SetChild`/`GetChildSlotInfo`/
// `GetCollectionByKind` dispatch (a single token slot at index 0 followed by a collection
// occupying the contiguous range [1, 1 + Count)) and the first whose generated `DoMatch` has a
// recursive COLLECTION term (`this.TypeArguments.DoMatch(o.TypeArguments, match)`, which calls
// `Pattern.DoMatchCollection` over the two node-list views) plus a `MatchString` on the name.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitIdentifierExpression(this)`; `IdentifierExpression` does not end in "AstType",
// so the generator's visit-method-name default (`targetSymbol.Name`) yields
// `VisitIdentifierExpression`. The generated slot statics are `IdentifierTokenSlot` (a
// `CSharpSlotInfo<Identifier>` pointing at `Slots.Identifier`, required -- the name is
// non-nullable so the token is a required slot) and `TypeArgumentsSlot` (a
// `CSharpSlotInfo<AstType>` pointing at `Slots.TypeArgument`, collection). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): deep-clones the token (carrying its `Name` and `StartLocation`) and every
// type argument through the setters/`Add` (which re-parent), and copies the annotation channel.
//
// The collection ctors that take type arguments (`IdentifierExpression(string,
// IEnumerable<AstType>)` and the `params AstType[]` form) are DEFERRED: they use `AddRange`,
// which lands with the collection convenience mutators (the D222 deferral). The empty + the
// `(string)` generated required-prefix + the `(string, TextLocation)` hand-written ctors cover
// the construction API; a type-argument list is built via `TypeArguments().Add(...)` until
// `AddRange` lands.
//
// C++ name-shadowing crux (the D237 `SimpleType` precedent): the string accessor `Identifier()`
// (a member function) shadows the `Identifier` CLASS in this class scope (C++ unqualified name
// lookup finds the member and stops, even though it is not a type -- the D224
// `Annotation<T>()`-shadows-the-`Annotation`-type crux), so the token type is the
// elaborated-type-specifier `class Identifier` (`basic.lookup.elab` ignores non-type names) in
// every type position AFTER the accessor (the backing field, the slot static's element type, the
// `SetChild` `static_cast`, the token accessor's signature, and the ctor parameter), and the
// `Identifier::Create` factory calls in the setter and the hand-written ctor are fully-qualified
// (`::ILSpy::Decompiler::CSharp::Syntax::Identifier::...`) -- the C# generator's `global::`
// qualification for the same shadowing case (a `[Slot]` string property literally named
// "Identifier" shadows the `Identifier` type inside its own setter); the hand-written
// `IdentifierExpression.cs` ctor likewise fully-qualifies the factory as
// `Decompiler.CSharp.Syntax.Identifier.Create`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_IDENTIFIEREXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_IDENTIFIEREXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class IdentifierExpression : Expression`. `final` (the C#
// `sealed`): no further derivation. The first AstType-bearing `Expression` with a collection slot
// and a string-name `[Slot]`.
class IdentifierExpression final : public Expression {
public:
    ~IdentifierExpression() override = default;

    // The generated empty ctor (the C# `public IdentifierExpression()`). The `TypeArguments`
    // collection is a member (the D222 always-present-stack-member design), initialized here
    // with `baseIndex = 1` (the `IdentifierToken` single slot occupies index 0) and
    // `supportsIncremental = true` (it is the node's only collection and its last slot, so
    // an element's flattened `ChildIndex` is exactly `1 + its local position`). The
    // `IdentifierToken` defaults to null (no name) via its default member initializer; a null
    // token violates the required-slot invariant, so a default-constructed node is only valid
    // until the name is set (or until `DoMatch`/`CheckInvariant` observe the missing token).
    IdentifierExpression() : typeArguments_(this, &TypeArgumentsSlot, 1, true) {}

    // The generated required-prefix ctor (the C# `public IdentifierExpression(string
    // identifier)`): `Identifier` is the only required ctor param (`TypeArguments` is an
    // optional collection). The generated body is `this.Identifier = identifier;` -- it calls
    // the string setter, which creates the token via `Identifier.Create` (fully-qualified --
    // the `Identifier()` accessor shadows the `Identifier` class in the complete-class context
    // the body is parsed in). A NON-nullable name creates a token even for an empty string
    // (unlike `SimpleType` whose `string?` uses `CreateIfNotEmpty`). Delegates to the empty ctor
    // so the collection member is initialized.
    explicit IdentifierExpression(std::string identifier) : IdentifierExpression() {
        Identifier(std::move(identifier));
    }

    // The hand-written `public IdentifierExpression(string identifier, TextLocation location)`
    // -- creates the `Identifier` token via the factory and sets it through the kind-based
    // `SetChildByKindUntyped` (which finds the `Identifier`-kind slot and calls `SetChild`). The
    // factory call is fully-qualified (the `Identifier()` accessor shadows the `Identifier`
    // class in the complete-class context the body is parsed in -- the C# source likewise
    // fully-qualifies as `Decompiler.CSharp.Syntax.Identifier.Create`).
    IdentifierExpression(std::string identifier, TextLocation location) : IdentifierExpression() {
        SetChildByKindUntyped(&Slots::Identifier,
            ::ILSpy::Decompiler::CSharp::Syntax::Identifier::Create(std::move(identifier), location));
    }

    // ---- The `IdentifierToken` slot (the backing token of the string name) -------------
    // The generated single NON-nullable `Identifier` slot at flattened index 0 (the C#
    // `Identifier` is `string`, not `string?`, so the token is a REQUIRED slot, unlike
    // `SimpleType.IdentifierToken` which is optional). The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. `class Identifier` (the `Identifier()` accessor shadows the class).
    class Identifier* IdentifierToken() const { return identifierToken_; }
    void IdentifierToken(class Identifier* value) {
        SetChildNode(identifierToken_, value, 0);
    }

    // ---- The `Identifier` string-name accessor (over the token) -----------------------
    // The generated `public partial string Identifier` -- a convenience string over the
    // `IdentifierToken` slot. A NON-optional name (the C# `string`, not `string?`): `get`
    // returns `IdentifierToken.Name` (derefs the token -- a null token is a half-constructed
    // node and would `NullReferenceException` in C#, so the port derefs faithfully); `set`
    // creates the token via `Identifier.Create(value)` (NOT `CreateIfNotEmpty` -- a non-nullable
    // name creates a token even for an empty string, so an empty name yields a token with an
    // empty `Name`, not a null token -- the `MemberType.MemberName` D238 precedent).
    // `Identifier()` returns `std::string` (a copy of the token's name); the `Identifier::Create`
    // factory call is fully-qualified (the `Identifier()` accessor shadows the `Identifier`
    // class in its own setter, the generator's `global::` case).
    std::string Identifier() const { return identifierToken_->Name(); }
    void Identifier(std::string_view value) {
        IdentifierToken(::ILSpy::Decompiler::CSharp::Syntax::Identifier::Create(std::string(value)));
    }

    // ---- The `TypeArguments` collection slot -------------------------------------------
    // The generated `public partial AstNodeCollection<AstType> TypeArguments` -- the collection
    // of type arguments (a `CSharpSlotInfoT<AstType>` slot at flattened index 1, the node's only
    // collection and last slot). The C# lazily allocates the wrapper (`field ??= new
    // AstNodeCollection<AstType>(...)`); the D222 port makes the collection an always-present
    // stack member, so the accessor returns the member directly (the empty-until-first-Add
    // element-list profile is preserved -- `list_` is empty until the first `Add`).
    AstNodeCollectionT<AstType>& TypeArguments() { return typeArguments_; }
    const AstNodeCollectionT<AstType>& TypeArguments() const { return typeArguments_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `IdentifierTokenSlot` (a `CSharpSlotInfo<Identifier>` pointing at `Slots.Identifier`,
    // required -- the name is non-nullable so the token is a required slot, unlike
    // `SimpleType` whose optional name makes the token optional); the `TypeArgumentsSlot` (a
    // `CSharpSlotInfo<AstType>` pointing at `Slots.TypeArgument`, collection). `class
    // Identifier` (the accessor shadows the class); `AstType` is unshadowed (no member named
    // `AstType`). Both kinds are already ported (`Slots::Identifier` by `SimpleType` D237,
    // `Slots::TypeArgument` by `SimpleType` D237), so no new `Slots` constant is added.
    static inline const CSharpSlotInfoT<class Identifier> IdentifierTokenSlot{"IdentifierToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<AstType> TypeArgumentsSlot{"TypeArguments", true, &Slots::TypeArgument, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitIdentifierExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitIdentifierExpression(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitIdentifierExpression`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitIdentifierExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A single `Identifier` slot at index 0 plus a `TypeArguments` collection occupying the
    // contiguous range [1, 1 + Count). `GetChildCount` is `1 + Count` (the single slot plus
    // the collection's current length); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the
    // slots subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a single
    // case then a collection step). `GetCollectionByKind` returns the `TypeArguments`
    // collection for the `TypeArgument` kind (the node's only collection).

    int GetChildCount() const override { return 1 + typeArguments_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0)
            return identifierToken_;
        i--;
        int n = typeArguments_.Count();
        if (i < n)
            return typeArguments_.At(i);
        throw std::out_of_range("IdentifierExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(identifierToken_, static_cast<class Identifier*>(value), index);
            return;
        }
        i--;
        int n = typeArguments_.Count();
        if (i < n) {
            typeArguments_.SetAt(i, static_cast<AstType*>(value));
            return;
        }
        throw std::out_of_range("IdentifierExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &IdentifierTokenSlot;
        i--;
        int n = typeArguments_.Count();
        if (i < n)
            return &TypeArgumentsSlot;
        throw std::out_of_range("IdentifierExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::TypeArgument)
            return &typeArguments_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is IdentifierExpression o && MatchString(this.Identifier, o.Identifier)
    // && this.TypeArguments.DoMatch(o.TypeArguments, match)`. The `Identifier` term is a
    // `String` `MatchString` (the `$any$` wildcard in the pattern's `Identifier` matches any
    // candidate name); the `TypeArguments` term is a RECURSIVE collection match -- the
    // generator emits `this.TypeArguments.DoMatch(o.TypeArguments, match)` (the
    // `AstNodeCollection`-typed recursive term, NOT `MatchOptional`, which the generator emits
    // only for a nullable non-collection child), which calls `Pattern.DoMatchCollection` over
    // the two node-list views with backtracking over the non-deterministic pattern nodes. A
    // type-only mismatch (not an `IdentifierExpression`) rejects early.
    //
    // The `Identifier()` calls are INLINED in the `MatchString` arguments (not pre-computed in
    // locals) so the C# `&&` short-circuit is preserved: `o->Identifier()` derefs the
    // candidate's token, which is only reached when the type check already passed (matching the
    // C#, which would `NullReferenceException` on a half-constructed candidate -- the `MemberName`
    // D238 precedent; a nameless candidate is UB/NRE in both languages, so the tests never pass
    // one). The `std::string` temporaries live until the end of the full `return` expression, so
    // the `std::string_view` views are valid for the `MatchString` call. The name is
    // non-nullable, so the `std::optional<std::string_view>` is always engaged (a real name,
    // never `nullopt`).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<IdentifierExpression*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Identifier())),
                   std::optional<std::string_view>(std::string_view(o->Identifier())))
            && typeArguments_.DoMatch(o->typeArguments_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `IdentifierToken` deep-cloned through the setter (which re-parents; the cloned token
    // carries its own `Name` and `StartLocation`), and every `TypeArguments` element deep-cloned
    // through `Add` (which re-parents and re-indexes). No scalar to copy (the `Identifier`
    // string is derived from the token, so cloning the token carries it). The `IdentifierExpression`
    // itself has no own location fields (`StartLocation`/`EndLocation` are the print-time base
    // fields set by the unported output visitor), so they are not copied (the
    // ConditionalExpression/SimpleType precedent). `class Identifier` (the accessor shadows the
    // class); the token `Clone()` returns `Identifier*` (the `Identifier::Clone` override),
    // which the `IdentifierToken(class Identifier*)` setter accepts directly. The return is
    // covariant through `Expression*` (the `Expression::Clone` pure-virtual returns
    // `Expression*`); a concrete expression returns its own type.
    IdentifierExpression* Clone() const override {
        auto* node = new IdentifierExpression();
        node->CloneAnnotationsFrom(*this);
        if (identifierToken_ != nullptr)
            node->IdentifierToken(identifierToken_->Clone());
        for (int i = 0; i < typeArguments_.Count(); i++)
            node->typeArguments_.Add(typeArguments_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `identifierToken_` is null until the name is set (a required slot --
    // `CheckInvariant` asserts it is filled, unlike `SimpleType` whose optional name leaves the
    // token legitimately null); `typeArguments_` is the always-present collection member (empty
    // until the first `Add`). `class Identifier` (the `Identifier()` accessor shadows the class
    // in this scope; the elaborated specifier finds it).
    class Identifier* identifierToken_ = nullptr;
    AstNodeCollectionT<AstType> typeArguments_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_IDENTIFIEREXPRESSION_HPP
