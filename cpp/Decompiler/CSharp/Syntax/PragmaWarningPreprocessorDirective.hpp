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

// Port of `PragmaWarningPreprocessorDirective` in ICSharpCode.Decompiler/CSharp/Syntax/
// GeneralScope/PreProcessorDirective.cs -- the `pragma_warning_directive ::= '#' 'pragma'
// 'warning' ( 'disable' | 'restore' ) expression*` node (C# lexical grammar), a sealed
// `PreProcessorDirective` with a `Warnings` `AstNodeCollection<PrimitiveExpression>` slot (the
// disable/restore warning-code list) and an `EndLocation` override computed from the last
// warning. Part of the preprocessor-directive family (the next in-order Phase-5 piece per the
// D293 plan), ported with its `PreProcessorDirective` base and its `LinePreprocessorDirective`
// sibling.
//
// The generator emits the slot-storage overrides + the per-node `WarningsSlot` (the only
// generated surface, since the source declares no `DoMatch` -- it inherits the hand-written
// `PreProcessorDirective.DoMatch`, which does NOT match `Warnings` -- and the base
// `PreProcessorDirective` is concrete so `NeedsVisitor` is false: no `AcceptVisitor` override,
// no `VisitPragmaWarningPreprocessorDirective`). The collection-only slot-storage shape (the
// `ArrayInitializerExpression` D250 precedent) applies: a single `Warnings` collection occupying
// the contiguous range [0, Count), incremental (the node's only collection and last slot).
//
// `EndLocation` override crux: the C# `public override TextLocation EndLocation { get { var
// child = LastChild; if (child == null) return base.EndLocation; return child.EndLocation; } }`
// -- the directive's end is the last warning's end (or the stored `Trivia` span end when there
// are no warnings). The override ports as `EndLocation() const override` calling `LastChild()`
// (the `AstNode` base) and falling back to `Trivia::EndLocation()` (the explicit base call
// returns the STORED `endLocation_`, since this override would otherwise recurse).
//
// Clone crux (the not-sealed-hierarchy + no-MemberwiseClone + EndLocation-override interaction):
// the C# `Clone` (`MemberwiseClone` + `CloneChildrenInto`) copies the STORED `Trivia` span
// (NOT the computed `EndLocation`) and deep-copies the `Warnings`. The port must copy the
// STORED span -- reading it via the explicit base calls `this->Trivia::StartLocation()`/
// `this->Trivia::EndLocation()` (bypassing this class's `EndLocation` override, which would
// otherwise return the computed last-warning end), and pass it to the source-span ctor (which
// sets `Type=Pragma` + the stored span). Then `Argument` is copied via the inherited public
// setter, the `Warnings` are deep-cloned through `Add`, and the annotation channel is copied
// (`CloneAnnotationsFrom` + `ReparentTrivia`). The `IsDefined(int)` helper (a
// `Warnings.Select(...).Any(...)` lookup) is DEFERRED (output/resolver-stage behaviour, the
// value-vs-behaviour discriminator).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_PRAGMAWARNINGPREPROCESSORDIRECTIVE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_PRAGMAWARNINGPREPROCESSORDIRECTIVE_HPP

#include "Decompiler/CSharp/Syntax/PreProcessorDirective.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include <optional>
#include <stdexcept>
#include <string>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class PragmaWarningPreprocessorDirective : PreProcessorDirective`.
// `final` (the C# `sealed`). A `PreProcessorDirective` whose `Type` is fixed to `Pragma` and
// which carries a `Warnings` collection of `PrimitiveExpression` (the disable/restore list).
class PragmaWarningPreprocessorDirective final : public PreProcessorDirective {
public:
    ~PragmaWarningPreprocessorDirective() override = default;

    // The hand-written `PragmaWarningPreprocessorDirective(TextLocation startLocation,
    // TextLocation endLocation) : base(PreProcessorDirectiveType.Pragma, startLocation,
    // endLocation)` -- the ctor that records the source span. Delegates to the base
    // `(type, start, end)` ctor with `Pragma` (which sets the `Trivia` span), then initializes
    // the `Warnings` collection member (baseIndex 0, incremental -- the node's only collection
    // and last slot, so an element's flattened `ChildIndex` is exactly its local position).
    PragmaWarningPreprocessorDirective(TextLocation startLocation, TextLocation endLocation)
        : PreProcessorDirective(PreProcessorDirectiveType::Pragma, startLocation, endLocation),
          warnings_(this, &WarningsSlot, 0, true) {}

    // The hand-written `PragmaWarningPreprocessorDirective(string? argument = null) : base(
    // PreProcessorDirectiveType.Pragma, argument)` -- the ctor that sets the optional
    // rest-of-line argument (e.g. `disable`/`restore` is emitted by the output visitor; the
    // argument carries any text after `#pragma warning`). Delegates to the base
    // `(type, argument)` ctor with `Pragma`, then initializes the `Warnings` collection.
    PragmaWarningPreprocessorDirective(std::optional<std::string> argument = std::nullopt)
        : PreProcessorDirective(PreProcessorDirectiveType::Pragma, std::move(argument)),
          warnings_(this, &WarningsSlot, 0, true) {}

    // ---- The `Warnings` collection slot ------------------------------------------
    // The generated `[Slot("Warning")] public partial AstNodeCollection<PrimitiveExpression>
    // Warnings` -- the disable/restore warning-code list (a `CSharpSlotInfoT<PrimitiveExpression>`
    // slot at flattened index 0, the node's only collection and last slot). The C# lazily
    // allocates the wrapper; the D222 port makes the collection an always-present stack member,
    // so the accessor returns the member directly (empty until the first `Add`).
    AstNodeCollectionT<PrimitiveExpression>& Warnings() { return warnings_; }
    const AstNodeCollectionT<PrimitiveExpression>& Warnings() const { return warnings_; }

    // The per-node slot static (pointing at the shared `Slots::Warning` kind, ported this
    // iteration). The `IsCollection` flag is true (the slot is a collection); the `IsOptional`
    // flag is true (the generator sets it true for every collection slot). No name shadowing (no
    // member is named `PrimitiveExpression` or `Warning`-the-class), so the element type is the
    // plain `PrimitiveExpression`.
    static inline const CSharpSlotInfoT<PrimitiveExpression> WarningsSlot{"Warnings", true, &Slots::Warning, true};

    // The C# `public override TextLocation EndLocation` -- the directive's end is the last
    // warning's end (or the stored `Trivia` span end when there are no warnings). `LastChild()`
    // (the `AstNode` base) returns the last non-null child; the fallback `Trivia::EndLocation()`
    // is an explicit base call (returns the STORED `endLocation_` -- without the qualification
    // this override would recurse into itself).
    TextLocation EndLocation() const override {
        AstNode* child = LastChild();
        if (child == nullptr)
            return Trivia::EndLocation();
        return child->EndLocation();
    }

    // ---- Slot storage (the generated overrides) ---------------------------------------
    // A single `Warnings` collection occupying the contiguous range [0, Count). `GetChildCount`
    // is the collection's current length (an empty node reports 0 -- no single-slot count
    // term); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the single collection slot (the
    // generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape with
    // one collection step and no single step). `GetCollectionByKind` returns the `Warnings`
    // collection for the `Warning` kind. The `ArrayInitializerExpression` D250 collection-only
    // precedent.

    int GetChildCount() const override { return warnings_.Count(); }

    AstNode* GetChild(int index) const override {
        int i = index;
        int n = warnings_.Count();
        if (i < n)
            return warnings_.At(i);
        throw std::out_of_range("PragmaWarningPreprocessorDirective::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        int n = warnings_.Count();
        if (i < n) {
            warnings_.SetAt(i, static_cast<PrimitiveExpression*>(value));
            return;
        }
        throw std::out_of_range("PragmaWarningPreprocessorDirective::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        int n = warnings_.Count();
        if (i < n)
            return &WarningsSlot;
        throw std::out_of_range("PragmaWarningPreprocessorDirective::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::Warning)
            return &warnings_;
        return AstNode::GetCollectionByKind(kind);
    }

public:
    // The per-concrete `Clone` (the not-sealed-hierarchy + no-MemberwiseClone + EndLocation-
    // override crux): creates a fresh `PragmaWarningPreprocessorDirective` reusing the
    // source-span ctor with the STORED `Trivia` span (read via the explicit base calls
    // `this->Trivia::StartLocation()`/`this->Trivia::EndLocation()` -- bypassing this class's
    // `EndLocation` override, which would otherwise return the computed last-warning end; the
    // C# `MemberwiseClone` copies the stored field, so the port copies it too), which sets
    // `Type=Pragma` and the stored span. The `Argument` is copied via the inherited public
    // setter (the base's `argument_` is private). The `Warnings` are deep-cloned through `Add`
    // (which re-parents and re-indexes; `PrimitiveExpression::Clone()` returns
    // `PrimitiveExpression*`, the covariant override, which `Add(PrimitiveExpression*)` accepts
    // directly). The annotation channel is copied (`CloneAnnotationsFrom` + `ReparentTrivia`,
    // the D223 concrete-clone pattern). The covariant return is
    // `PragmaWarningPreprocessorDirective*` (through `PreProcessorDirective*`, the virtual
    // `PreProcessorDirective::Clone`).
    PragmaWarningPreprocessorDirective* Clone() const override {
        auto* node = new PragmaWarningPreprocessorDirective(this->Trivia::StartLocation(),
                                                            this->Trivia::EndLocation());
        node->Argument(Argument());
        for (int i = 0; i < warnings_.Count(); i++)
            node->warnings_.Add(warnings_.At(i)->Clone());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `warnings_` is the always-present collection member (empty until the
    // first `Add`). No name shadowing (no member is named `PrimitiveExpression`), so the field
    // type is the plain class.
    AstNodeCollectionT<PrimitiveExpression> warnings_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_PRAGMAWARNINGPREPROCESSORDIRECTIVE_HPP
