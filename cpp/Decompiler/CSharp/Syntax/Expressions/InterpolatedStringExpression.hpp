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

// Port of the `InterpolatedStringExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.cs (the
// generated `InterpolatedStringExpression.g.cs` + the hand-written partial). The next
// in-order Phase-5 piece per the D308 plan ("the remaining Expression nodes:
// InterpolatedStringExpression -- the InterpolatedStringContent abstract base +
// Interpolation/InterpolatedStringText concrete family"). `interpolated_string_expression
// ::= interpolated_string_content*` (C# grammar 12.8.3): a `$"..."` interpolated string -- an
// `Expression` whose sole child slot is the `Content` collection of
// `InterpolatedStringContent` (the literal-text runs `InterpolatedStringText` and the
// expression arms `Interpolation`), plus the `OpenQuote` (`$"`) / `CloseQuote` (`"`) const
// strings the output visitor emits.
//
// It is the SIMPLEST collection-slot Expression node ported for the interpolated-string
// family: the only node with a COLLECTION slot and NO single child slot (the
// `ArrayInitializerExpression` D250 / `TupleExpression` D296 / `AnonymousTypeCreateExpression`
// D304 collection-only shape). The `Content` collection (an
// `AstNodeCollection<InterpolatedStringContent>`) is the node's only slot and its last slot,
// so `supportsIncremental` is true (an element's flattened `ChildIndex` is exactly its local
// position) and `GetChildCount` is the collection's current length (an empty node reports
// `GetChildCount` 0). The collection's element type is the `InterpolatedStringContent`
// ABSTRACT base (which redeclares a typed covariant `Clone` returning
// `InterpolatedStringContent*`, the D264 `VariableDesignation` precedent), so each `Content`
// element deep-clones through `InterpolatedStringContent::Clone()` which returns
// `InterpolatedStringContent*` and `Add(InterpolatedStringContent*)` accepts directly (no
// `static_cast`, unlike the D285 `ExtensionDeclaration.Members` `EntityDeclaration`-collection
// path whose base redeclares no typed `Clone`).
//
// Its generated `DoMatch` has a single term: the collection recursive match
// `this.Content.DoMatch(o.Content, match)` (the generator emits the collection-typed recursive
// term directly -- NOT `MatchOptional`, which it emits only for a nullable NON-collection
// child). The generated `AcceptVisitor` calls `visitor.VisitInterpolatedStringExpression(this)`
// (the class name does not end in "AstType", so the visit-method-name default yields
// `VisitInterpolatedStringExpression`). The generated slot static is `ContentSlot` (a
// `CSharpSlotInfoT<InterpolatedStringContent>` pointing at `Slots.Content`, collection).
//
// NO C++ name-shadowing crux: the `Content()` accessor is a member function, but no class named
// `Content` lives in the `Syntax` namespace, so no elaborated-type-specifier is needed
// anywhere.
//
// NO new `Slots` constant is declared in this header: the `[Slot("Content")]` argument names
// the slot kind "Content", a new kind declared in `Slots.hpp` (the abstract
// `InterpolatedStringContent` base's header does NOT include `Slots.hpp` -- no per-node slot
// statics -- so `Slots::Content` lives in `Slots.hpp` with no include cycle, the
// `Slots.Statement`/`Slots.ArraySpecifier` precedent applied to an `InterpolatedStringContent`-
// typed collection kind).
//
// `InterpolatedStringExpression.cs` declares ONE hand-written ctor
// `InterpolatedStringExpression(IList<InterpolatedStringContent> content)` whose body calls
// `Content.AddRange(content)`; `AddRange` is the D222-deferred collection convenience
// mutator, so the collection ctor is DEFERRED, and the empty ctor is the only portable ctor.
// A content list is built via `Content().Add(...)` until `AddRange` lands.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_INTERPOLATEDSTRINGEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_INTERPOLATEDSTRINGEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/InterpolatedStringContent.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class InterpolatedStringExpression : Expression`. `final`
// (the C# `sealed`; `[DecompilerAstNode]` with no arg means `hasPatternPlaceholder` defaults
// to false, so no `PatternPlaceholder` derives from it). A collection-only `Expression`.
class InterpolatedStringExpression final : public Expression {
public:
    ~InterpolatedStringExpression() override = default;

    // The generated empty ctor (the C# `public InterpolatedStringExpression()`). The
    // `Content` collection is a member (the D222 always-present-stack-member design),
    // initialized here with `baseIndex = 0` (it is the node's only slot, so the first
    // element's flattened `ChildIndex` is 0) and `supportsIncremental = true` (it is the
    // node's only collection and its last slot, so an element's flattened `ChildIndex` is
    // exactly its local position). The collection starts empty; the node has no required
    // single slots, so a default-constructed node is a valid empty interpolated string
    // (`CheckInvariant` passes). The hand-written `(IList<InterpolatedStringContent>)` ctor is
    // DEFERRED (it calls `AddRange`, the D222 deferral).
    InterpolatedStringExpression() : content_(this, &ContentSlot, 0, true) {}

    // The C# `public const string OpenQuote = "$""` / `public const string CloseQuote = "\""`
    // (the open/close quote tokens the output visitor emits around the content). Part of the
    // node's public API. Ports as `static constexpr const char*` (static fields, not instance
    // state), so the generator's `MembersToMatch` (which iterates only instance
    // `IPropertySymbol`s) excludes them from the `DoMatch` (the `CheckedExpression.CheckedKeyword`
    // D234 precedent).
    static constexpr const char* OpenQuote = "$\"";
    static constexpr const char* CloseQuote = "\"";

    // ---- The `Content` collection slot ------------------------------------------
    // The generated `[Slot("Content")] public partial AstNodeCollection<InterpolatedStringContent>
    // Content` -- the collection of content nodes (literal text + interpolations). A
    // `CSharpSlotInfoT<InterpolatedStringContent>` slot at flattened index 0, the node's only
    // collection and last slot. The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly
    // (the empty-until-first-Add element-list profile is preserved -- `list_` is empty until
    // the first `Add`).
    AstNodeCollectionT<InterpolatedStringContent>& Content() { return content_; }
    const AstNodeCollectionT<InterpolatedStringContent>& Content() const { return content_; }

    // The per-node slot static (pointing at the shared `Slots` kind). The `IsCollection` flag
    // is true (the slot is a collection); the `IsOptional` flag is true (the generator sets it
    // true for every collection slot). The kind is `Slots::Content` (a new kind in `Slots.hpp`,
    // the `Content` slot name unique to this node among the ported nodes). No name shadowing
    // (no class named `Content`), so the element type is the plain `InterpolatedStringContent`.
    static inline const CSharpSlotInfoT<InterpolatedStringContent> ContentSlot{"Content", true, &Slots::Content, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitInterpolatedStringExpression`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitInterpolatedStringExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A single `Content` collection occupying the contiguous range [0, Count). `GetChildCount`
    // is the collection's current length (an empty node reports 0); `GetChild`/`SetChild`/
    // `GetChildSlotInfo` walk the single collection slot; `GetCollectionByKind` returns the
    // `Content` collection for the `Content` kind.

    int GetChildCount() const override { return content_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = content_.Count();
        if (i < n)
            return content_.At(i);
        throw std::out_of_range("InterpolatedStringExpression::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = content_.Count();
        if (i < n) {
            content_.SetAt(i, static_cast<InterpolatedStringContent*>(value));
            return;
        }
        throw std::out_of_range("InterpolatedStringExpression::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = content_.Count();
        if (i < n)
            return &ContentSlot;
        throw std::out_of_range("InterpolatedStringExpression::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Content)
            return &content_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) -----------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is InterpolatedStringExpression o && this.Content.DoMatch(o.Content,
    // match)`. The single term is the collection recursive match (the generator emits the
    // collection-typed recursive term directly, NOT `MatchOptional`). A type-only mismatch
    // (not an `InterpolatedStringExpression`) rejects early.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<InterpolatedStringExpression*>(other);
        if (o == nullptr)
            return false;
        return content_.DoMatch(o->content_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and every
    // `Content` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `InterpolatedStringContent::Clone()` returns `InterpolatedStringContent*`, the covariant
    // override the abstract base redeclares, which `Add(InterpolatedStringContent*)` accepts
    // directly -- no `static_cast`, unlike the D285 `EntityDeclaration`-collection path whose
    // base redeclares no typed `Clone`). No own location fields, so the print-time
    // `StartLocation`/`EndLocation` are not copied. The covariant return is
    // `InterpolatedStringExpression*` (through `Expression*`, the `Expression::Clone`
    // pure-virtual).
    InterpolatedStringExpression* Clone() const override {
        auto* node = new InterpolatedStringExpression();
        node->CloneAnnotationsFrom(*this);
        for (int i = 0; i < content_.Count(); i++)
            node->content_.Add(content_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `content_` is the always-present collection member (empty until the
    // first `Add`). No name shadowing (no class named `Content`), so the field type is the
    // plain class.
    AstNodeCollectionT<InterpolatedStringContent> content_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_INTERPOLATEDSTRINGEXPRESSION_HPP
