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

// Port of the `SimpleType` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/SimpleType.cs (the generated `SimpleType.g.cs` + the
// hand-written partial). The second concrete `AstType` and the next in-order Phase-5 piece
// per the D236 plan ("the AstType hierarchy (SimpleType/MemberType/ComposedType/...)"):
// `simple_type ::= identifier ( '<' type ( ',' type )* '>' )?` (C# grammar 7.8.1) -- a type
// reference consisting of an `Identifier` name plus zero or more `TypeArguments` (`AstType`
// children). It is the FIRST ported node with a COLLECTION slot (`TypeArguments`:
// `AstNodeCollection<AstType>`), so it is the first to exercise the collection-aware
// `GetChild`/`SetChild`/`GetChildSlotInfo`/`GetCollectionByKind` dispatch (a single slot at
// index 0 followed by a collection occupying the contiguous range [1, 1 + Count)), and the
// first generated `DoMatch` whose recursive term is the collection's `DoMatch`
// (`this.TypeArguments.DoMatch(o.TypeArguments, match)`, which calls
// `Pattern.DoMatchCollection` over the two node-list views) rather than `MatchOptional`/
// `MatchRequired`.
//
// It is also the first ported node with a string-name `[Slot]`: `[Slot("Identifier")]
// public partial string? Identifier` is a convenience string accessor over a generated
// backing `Identifier` token child slot (`IdentifierToken`, a single nullable `Identifier`
// slot at flattened index 0). The generator emits the token slot (a real `[Slot]` child,
// non-`partial`) plus the string body (a `partial` property delegating to the token); the
// `DoMatch` matches the STRING (`MatchString(this.Identifier, o.Identifier)`) and never sees
// the token. An OPTIONAL name (the C# `string?` -> a nullable token) reads as null when the
// token is absent, and the setter clears the token on an empty/null name
// (`Identifier.CreateIfNotEmpty` returns null for empty).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitSimpleType(this)`; `SimpleType` does not end in "AstType", so the generator's
// visit-method-name default (`targetSymbol.Name`) yields `VisitSimpleType` (the
// `EndsWith("AstType")` rewriting to "...Type" applies only to `FunctionPointerAstType`/
// `InvocationAstType`/`TupleAstType`). The generated slot statics are `IdentifierTokenSlot`
// (a `CSharpSlotInfo<Identifier>` pointing at `Slots.Identifier`, optional) and
// `TypeArgumentsSlot` (a `CSharpSlotInfo<AstType>` pointing at `Slots.TypeArgument`,
// collection). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the
// port overrides it (no `MemberwiseClone`): deep-clones the token (carrying its `Name` and
// `StartLocation`) and every type argument through the setters/`Add` (which re-parent), and
// copies the annotation channel.
//
// The collection ctors that take type arguments (`SimpleType(string, IEnumerable<AstType>)`
// and the `params AstType[]` form) are DEFERRED: they use `AddRange`, which lands with the
// collection convenience mutators (the D222 deferral). The empty + the `(string)` +
// the two hand-written ctors cover the construction API; a type-argument list is built via
// `TypeArguments().Add(...)` until `AddRange` lands.
//
// C++ name-shadowing crux (the D231 `Expression()` / D224 `Annotation<T>()` pattern): the
// string accessor `Identifier()` (a member function) shadows the `Identifier` CLASS in this
// class scope, so the token type is the elaborated-type-specifier `class Identifier`
// (`basic.lookup.elab` ignores non-type names) in every type position AFTER the accessor
// (the backing field, the slot static's element type, the `SetChild` `static_cast`, the
// token accessor's signature, and the ctor parameter), and the `Identifier::Create`/
// `CreateIfNotEmpty` factory calls in the setter and the hand-written ctor are
// fully-qualified (`::ILSpy::Decompiler::CSharp::Syntax::Identifier::...`) -- the C#
// generator's `global::` qualification for the same shadowing case.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_SIMPLETYPE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_SIMPLETYPE_HPP

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

// The C# `public sealed partial class SimpleType : AstType`. `final` (the C# `sealed`): no
// further derivation. The first concrete `AstType` with a collection slot and a string-name
// `[Slot]`.
class SimpleType final : public AstType {
public:
    ~SimpleType() override = default;

    // The generated empty ctor (the C# `public SimpleType()`). The `TypeArguments`
    // collection is a member (the D222 always-present-stack-member design), initialized here
    // with `baseIndex = 1` (the `IdentifierToken` single slot occupies index 0) and
    // `supportsIncremental = true` (it is the node's only collection and its last slot, so
    // an element's flattened `ChildIndex` is exactly `1 + its local position`). The
    // `IdentifierToken` defaults to null (no name) via its default member initializer.
    SimpleType() : typeArguments_(this, &TypeArgumentsSlot, 1, true) {}

    // The hand-written `public SimpleType(Identifier identifier)` -- sets the `IdentifierToken`
    // directly (the token IS the construction value). `class Identifier` (the accessor
    // shadows the class in this scope; the elaborated specifier finds it). Delegates to the
    // empty ctor so the collection member is initialized.
    explicit SimpleType(class Identifier* identifier) : SimpleType() {
        IdentifierToken(identifier);
    }

    // The hand-written `public SimpleType(string identifier, TextLocation location)` --
    // creates the `Identifier` token via the factory and sets it through the kind-based
    // `SetChildByKindUntyped` (which finds the `Identifier`-kind slot and calls `SetChild`).
    // The factory call is fully-qualified (the `Identifier()` accessor shadows the class in
    // the complete-class context the body is parsed in).
    SimpleType(std::string identifier, TextLocation location) : SimpleType() {
        SetChildByKindUntyped(&Slots::Identifier,
            ::ILSpy::Decompiler::CSharp::Syntax::Identifier::Create(std::move(identifier), location));
    }

    // The generated `public SimpleType(string identifier)` (the required-prefix ctor --
    // `Identifier` is the only required param; `TypeArguments` is an optional collection).
    // Delegates to the empty ctor then sets the string name (the setter creates the token via
    // `Identifier.CreateIfNotEmpty`, so an empty name leaves the token null).
    explicit SimpleType(std::string identifier) : SimpleType() {
        Identifier(identifier);
    }

    // ---- The `IdentifierToken` slot (the backing token of the string name) -------------
    // The generated single nullable `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it) re-parents and
    // re-indexes in place. `class Identifier` (the `Identifier()` accessor shadows the class).
    class Identifier* IdentifierToken() const { return identifierToken_; }
    void IdentifierToken(class Identifier* value) {
        SetChildNode(identifierToken_, value, 0);
    }

    // ---- The `Identifier` string-name accessor (over the token) -----------------------
    // The generated `public partial string? Identifier` -- a convenience string over the
    // `IdentifierToken` slot. An OPTIONAL name (the C# `string?`): `get` returns null when
    // the token is absent; `set` creates the token via `Identifier.CreateIfNotEmpty`, so an
    // empty/null name clears the token. `Identifier()` returns `std::optional<std::string>`
    // (null when the token is absent -- the faithful `string?`); the factory call is
    // fully-qualified (the `Identifier()` accessor shadows the `Identifier` class in its own
    // setter, the generator's `global::` case).
    std::optional<std::string> Identifier() const {
        return identifierToken_ != nullptr
            ? std::optional<std::string>(identifierToken_->Name()) : std::nullopt;
    }
    void Identifier(std::string_view value) {
        IdentifierToken(::ILSpy::Decompiler::CSharp::Syntax::Identifier::CreateIfNotEmpty(value));
    }

    // ---- The `TypeArguments` collection slot -------------------------------------------
    // The generated `public partial AstNodeCollection<AstType> TypeArguments` -- the
    // collection of type arguments (a `CSharpSlotInfoT<AstType>` slot at flattened index 1,
    // the node's only collection and last slot). The C# lazily allocates the wrapper
    // (`field ??= new AstNodeCollection<AstType>(...)`); the D222 port makes the collection
    // an always-present stack member, so the accessor returns the member directly (the
    // empty-until-first-Add element-list profile is preserved -- `list_` is empty until the
    // first `Add`).
    AstNodeCollectionT<AstType>& TypeArguments() { return typeArguments_; }
    const AstNodeCollectionT<AstType>& TypeArguments() const { return typeArguments_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------
    // The `IdentifierTokenSlot` (a `CSharpSlotInfo<Identifier>` pointing at `Slots.Identifier`,
    // optional -- the name may be absent); the `TypeArgumentsSlot` (a `CSharpSlotInfo<AstType>`
    // pointing at `Slots.TypeArgument`, collection). `class Identifier` (the accessor shadows
    // the class); `AstType` is unshadowed (no member named `AstType`).
    static inline const CSharpSlotInfoT<class Identifier> IdentifierTokenSlot{"IdentifierToken", false, &Slots::Identifier, true};
    static inline const CSharpSlotInfoT<AstType> TypeArgumentsSlot{"TypeArguments", true, &Slots::TypeArgument, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitSimpleType`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitSimpleType(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitSimpleType`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitSimpleType(this);
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
        throw std::out_of_range("SimpleType::GetChild");
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
        throw std::out_of_range("SimpleType::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0)
            return &IdentifierTokenSlot;
        i--;
        int n = typeArguments_.Count();
        if (i < n)
            return &TypeArgumentsSlot;
        throw std::out_of_range("SimpleType::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::TypeArgument)
            return &typeArguments_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is SimpleType o && MatchString(this.Identifier, o.Identifier) &&
    // this.TypeArguments.DoMatch(o.TypeArguments, match)`. The `Identifier` term is a
    // `String` `MatchString` (the `$any$` wildcard in the pattern's `Identifier` matches any
    // candidate name); the `TypeArguments` term is a RECURSIVE collection match -- the
    // generator emits `this.TypeArguments.DoMatch(o.TypeArguments, match)` (the
    // `AstNodeCollection`-typed recursive term, NOT `MatchOptional`, which the generator
    // emits only for a nullable non-collection child), which calls
    // `Pattern.DoMatchCollection` over the two node-list views with backtracking over the
    // non-deterministic pattern nodes. A type-only mismatch (not a `SimpleType`) rejects
    // early. `Identifier()` returns `std::optional<std::string>` (nullopt when the token is
    // absent); `Pattern::MatchString` takes `std::optional<std::string_view>`, so the view is
    // built per side (nullopt passes through as the C# null).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<SimpleType*>(other);
        if (o == nullptr)
            return false;
        auto thisId = Identifier();
        auto otherId = o->Identifier();
        return PatternMatching::Pattern::MatchString(
                   thisId ? std::optional<std::string_view>(*thisId) : std::nullopt,
                   otherId ? std::optional<std::string_view>(*otherId) : std::nullopt)
            && typeArguments_.DoMatch(o->typeArguments_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `IdentifierToken` deep-cloned through the setter (which re-parents; the cloned token
    // carries its own `Name` and `StartLocation`), and every `TypeArguments` element
    // deep-cloned through `Add` (which re-parents and re-indexes). No scalar to copy (the
    // `Identifier` string is derived from the token, so cloning the token carries it). The
    // `SimpleType` itself has no own location fields (`StartLocation`/`EndLocation` are the
    // print-time base fields set by the unported output visitor), so they are not copied
    // (the ConditionalExpression precedent). `class Identifier` (the accessor shadows the
    // class); the token `Clone()` returns `Identifier*` (the `Identifier::Clone` override),
    // which the `IdentifierToken(class Identifier*)` setter accepts directly.
    SimpleType* Clone() const override {
        auto* node = new SimpleType();
        node->CloneAnnotationsFrom(*this);
        if (identifierToken_ != nullptr)
            node->IdentifierToken(identifierToken_->Clone());
        for (int i = 0; i < typeArguments_.Count(); i++)
            node->typeArguments_.Add(typeArguments_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `identifierToken_` is null until the name is set (an optional name
    // -- the token may be absent); `typeArguments_` is the always-present collection member
    // (empty until the first `Add`). `class Identifier` (the `Identifier()` accessor shadows
    // the class in this scope; the elaborated specifier finds it).
    class Identifier* identifierToken_ = nullptr;
    AstNodeCollectionT<AstType> typeArguments_;
};

// The `ConstraintTypeParameter` kind -- a single `SimpleType` child (the type parameter a
// `Constraint` constrains, the `T` in `where T : ...`). Unique to `Constraint` among the ported
// nodes. A `CSharpSlotInfoT<SimpleType>` (the element type is the concrete `SimpleType` node).
// Defined HERE (in `SimpleType.hpp`, after the `SimpleType` class) for the cycle-breaking reason:
// `SimpleType.hpp` includes `Slots.hpp` (for `Slots::Identifier`/`Slots::TypeArgument` used by its
// per-node `IdentifierTokenSlot`/`TypeArgumentsSlot`), and with `Slots.hpp`'s guard set those
// definitions would not be visible where `SimpleType`'s class body needs them. After the class
// both `CSharpSlotInfoT` (visible via the `Slots.hpp` include) and `SimpleType` are complete, so
// the kind defines cleanly. The `inline` variable still has external linkage and one address
// across translation units (the C++17 `inline` guarantee), preserving the pointer-identity
// comparison `node.Slot.Kind == &Slots::ConstraintTypeParameter` the slot system relies on. This
// is the `Slots::Attribute`/`Slots::AttributeSection`/`Slots::Parameter` cycle-breaking precedent
// (D241/D242/D279) applied to a `SimpleType`-typed single kind. The shared constant is constructed
// non-collection/non-optional (`{"ConstraintTypeParameter", false, nullptr, false}`); the per-node
// `TypeParameterSlot` on `Constraint` carries the required (non-optional) flag. The kind name
// `ConstraintTypeParameter` collides with no class in the `Syntax` namespace (there is
// `TypeParameterDeclaration`, not `ConstraintTypeParameter`), so no elaborated-type-specifier is
// needed.
namespace Slots {
inline const CSharpSlotInfoT<SimpleType> ConstraintTypeParameter{"ConstraintTypeParameter", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_SIMPLETYPE_HPP
