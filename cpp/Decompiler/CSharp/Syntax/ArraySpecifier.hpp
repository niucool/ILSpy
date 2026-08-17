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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Port of the `ArraySpecifier` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/ComposedType.cs (the generated `ArraySpecifier.g.cs`
// + the hand-written partial, co-declared with `ComposedType` since an array type's rank
// specifiers are its `[Slot("ArraySpecifier")]` collection). `rank_specifier ::=
// '[' ','* ']'` (C# grammar 8.2.1): the `[...]`/`[,...]` rank specifier of an array type, a
// leaf `AstNode` (not an `AstType` and not an `Expression`) carrying a single `Dimensions`
// count. It is the first in-order piece of the `ComposedType` dependency (the next AstType
// node per the D238 plan): `ComposedType.ArraySpecifiers` is an
// `AstNodeCollection<ArraySpecifier>`, so `ArraySpecifier` must exist before `ComposedType`
// can port its collection slot.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. `ArraySpecifier` has `[DecompilerAstNode]` (no
// `hasPatternPlaceholder`), so no pattern placeholder is emitted. It declares NO `[Slot]`
// children: `Dimensions` is a hand-written `int` property (NOT a `[Slot]`), so the generator
// sees no slots and emits no slot properties, no name accessors, and no generated
// constructors (the `WriteConstructors` step returns early when `slots.Count == 0`); the two
// hand-written ctors stand. The generated `AcceptVisitor` calls
// `visitor.VisitArraySpecifier(this)` (the class name does not end in "AstType", so the
// visit-method-name default yields `VisitArraySpecifier`). The generated `DoMatch` is
// `return other is ArraySpecifier o && this.Dimensions == o.Dimensions` -- a type check plus a
// plain-equality term on the `int` `Dimensions` (a non-`[Slot]` public `int` property, so the
// generator adds it to `MembersToMatch` and the `DoMatchTerm` fall-through emits
// `this.Dimensions == o.Dimensions`; an `int` is not an enum, has no `Any` wildcard, and is
// not a string). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the
// port overrides it (no `MemberwiseClone`): copies `Dimensions` plus the annotation channel.
// No children to deep-copy (a leaf); the print-time `StartLocation`/`EndLocation` are not
// derived (the node stores them at print time, no own location fields), so they are not
// copied (the `ConditionalExpression`/`SimpleType` precedent for nodes without derived
// locations). The hand-written `CheckInvariant` override asserts `Dimensions >= 1` (a rank
// specifier always has at least one dimension -- `[]` is rank 1), the first ported node to
// add its own scalar invariant beyond the inherited base.
//
// `ToString(CSharpFormattingOptions)` is DEFERRED (output stage; the ported `AstNode` base
// does not declare it, so there is nothing to override).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_ARRAYSPECIFIER_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_ARRAYSPECIFIER_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <cassert>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ArraySpecifier : AstNode`. `final` (the C# `sealed`):
// no further derivation. A leaf: no `[Slot]` children, so the inherited zero-child
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` defaults apply (the
// NullReferenceExpression/Identifier leaf precedent). The node does NOT derive `EndLocation`
// (it stores the print-time span on the inherited base fields), so `Clone` does not copy the
// locations (the `ConditionalExpression`/`SimpleType` precedent for nodes without derived
// locations).
class ArraySpecifier final : public AstNode {
public:
    ~ArraySpecifier() override = default;

    // The C# `public ArraySpecifier()` -- the parameterless ctor. The `Dimensions` field
    // defaults to `1` (the C# `= 1` initializer).
    ArraySpecifier() = default;

    // The C# `public ArraySpecifier(int dimensions)` -- sets `Dimensions`. `explicit` (the
    // PrimitiveType single-arg-ctor precedent: avoids an implicit `int` -> `ArraySpecifier`
    // conversion).
    explicit ArraySpecifier(int dimensions) : dimensions_(dimensions) {}

    // The C# `public int Dimensions { get; set; } = 1` -- the rank (the number of dimensions;
    // `[]` is rank 1, `[,]` is rank 2, ...). Defaults to `1`.
    int Dimensions() const { return dimensions_; }
    void Dimensions(int value) { dimensions_ = value; }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch
    // entry: routes back to `VisitArraySpecifier`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitArraySpecifier(this);
    }

    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ArraySpecifier o && this.Dimensions == o.Dimensions`. A type match plus
    // a plain-equality term on the `int` `Dimensions` (the `DoMatchTerm` fall-through for a
    // non-enum, non-string scalar). `match` is unused (no captures, no recursive child
    // match -- a leaf).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<ArraySpecifier*>(other);
        if (o == nullptr)
            return false;
        return dimensions_ == o->dimensions_;
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node with the same `Dimensions`, plus the
    // annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern). No children to deep-copy (a leaf); the print-time locations
    // are not derived, so they are not copied (the `ConditionalExpression`/`SimpleType`
    // precedent for nodes without derived locations).
    ArraySpecifier* Clone() const override {
        auto* node = new ArraySpecifier(dimensions_);
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }

    // The C# `internal override void CheckInvariant()` -- the hand-written override that
    // asserts the node's own scalar invariant after the inherited base check. A rank
    // specifier always has at least one dimension (`[]` is rank 1), so `Dimensions >= 1`.
    // `AstNode::CheckInvariant` is a no-op in `NDEBUG` (its body is `#ifndef NDEBUG`-guarded)
    // and `assert` is a no-op in `NDEBUG`, so the whole override is a no-op in release builds
    // (mirrors the C# `[Conditional("DEBUG")]`). This is the first ported concrete node to
    // add its own scalar invariant beyond the inherited base.
    void CheckInvariant() override {
        AstNode::CheckInvariant();
        assert(dimensions_ >= 1 && "ArraySpecifier.Dimensions must be at least 1");
    }

private:
    int dimensions_ = 1;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_ARRAYSPECIFIER_HPP
