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

// Port of the `TypeReferenceExpression` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.cs (the generated
// `TypeReferenceExpression.g.cs`; the hand-written partial declares only the one slot
// property, no ctors, no helpers). The next in-order Phase-5 piece per the D244 plan
// ("TypeReferenceExpression -- a single required AstType Type slot, no const keyword ...
// the simplest slot-bearing AstType-bearing Expression shape, one required single slot").
//
// No C# spec grammar production: an ILSpy wrapper that lets an `AstType` appear in
// expression position (e.g. the `int` in `int.Parse(...)`). It is an `Expression` with a
// single, REQUIRED (non-nullable) `[Slot]` child -- a `Type` `AstType` -- and NO scalar. The
// generator emits one typed slot static (`TypeSlot` pointing at the shared `Slots::Type`
// kind -- already ported by `Attribute`, so no new `Slots` constant), the const-index
// `SetChildNode(ref field, value, 0)` setter (the single slot is the first and only slot, so
// the flattened index is the constant 0), the `GetChildCount`/`GetChild`/`SetChild`/
// `GetChildSlotInfo` overrides over the one single slot, and the `DoMatch` `return other is
// TypeReferenceExpression o && this.Type.DoMatch(o.Type, match)`. The child is NON-NULLABLE
// recursive, so the generator emits the direct `this.Type.DoMatch(o.Type, match)` term (NOT
// `MatchOptional`, which the generator emits only for a nullable recursive child); there is
// no scalar enum, so there is no `Any`-wildcard term. `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones the child through the setter (which re-parents) and copies the annotation
// channel. There is no scalar member, so `Clone` copies no scalar.
//
// NO C++ name-shadowing crux (unlike `CastExpression`/`AsExpression`/`IsExpression`): the only
// child accessor is `Type` of type `AstType`, and the `Type()` accessor does NOT collide with
// the `AstType` base type (no class named `Type` lives in the `Syntax` namespace -- the
// `Attribute` D240 lesson), so the operand type is the plain `AstType` everywhere and no
// elaborated-type-specifier is needed. This is the simplest ported AstType-bearing
// `Expression` (one required single slot, no scalar, no const string, no name shadowing).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_TYPEREFERENCEEXPRESSION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_TYPEREFERENCEEXPRESSION_HPP

#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class TypeReferenceExpression : Expression`. `final` (the
// C# `sealed`): no further derivation. The simplest AstType-bearing `Expression` node: one
// single, REQUIRED (non-nullable) `[Slot]` child (`Type` `AstType`), no scalar, no const
// string.
class TypeReferenceExpression final : public Expression {
public:
    ~TypeReferenceExpression() override = default;

    // The generated empty ctor (the C# `public TypeReferenceExpression()`). The `Type` slot
    // defaults to null (no type). A null slot violates the required-slot invariant, so a
    // default-constructed node is only valid until `Type` is set (or until `DoMatch`/
    // `CheckInvariant` observe the missing child).
    TypeReferenceExpression() = default;

    // The generated all-params ctor (the C# `public TypeReferenceExpression(AstType type)`).
    // Delegated to the empty ctor then the setter so the slot machinery re-parents and
    // re-indexes the child. There is no scalar enum, so the single slot child is the only
    // ctor param (the generator emits a single full ctor -- the one slot is required, so the
    // required prefix IS the full set; no shorter prefix ctor and no `params` overload,
    // since no collection slot is present). `TypeReferenceExpression.cs` declares NO
    // hand-written ctors.
    explicit TypeReferenceExpression(AstType* type)
        : TypeReferenceExpression() {
        Type(type);
    }

    // The C# `[Slot("Type")] AstType Type` -- a single, REQUIRED (non-nullable) `AstType`
    // child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (the single slot is the first and only
    // slot, no collection precedes it), so the index is assigned directly and the parent's
    // indices stay valid by construction. The C# getter returns the backing field
    // null-forgiving (`field!`) because the slot is required; the port returns the raw
    // pointer (a required slot is non-null only by invariant, not by type), so callers must
    // keep the child set. No name shadowing (the `Type()` accessor does not collide with the
    // `AstType` base type -- no class named `Type` lives in the `Syntax` namespace), so the
    // operand type is the plain `AstType`.
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value, 0);
    }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The kind is
    // already ported (`Slots::Type` by `Attribute`), so no new `Slots` constant is added. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable);
    // the kind carries identity only. The element type is the plain `AstType` (no shadowing).
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitTypeReferenceExpression` (the class name does not end in "AstType",
    // so the generator's visit-method-name default yields `VisitTypeReferenceExpression`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitTypeReferenceExpression(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Type`); no collection, so `GetChildCount` is the
    // constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with a single case).

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return type_;
            default: throw std::out_of_range("TypeReferenceExpression::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(type_, static_cast<AstType*>(value), 0); break;
            default: throw std::out_of_range("TypeReferenceExpression::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &TypeSlot;
            default: throw std::out_of_range("TypeReferenceExpression::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is TypeReferenceExpression o && this.Type.DoMatch(o.Type, match)`. The
    // child is NON-NULLABLE recursive, so the generator emits the direct
    // `this.Type.DoMatch(o.Type, match)` term (NOT `MatchOptional`, which the generator emits
    // only for a nullable recursive child); there is no scalar enum, so there is no
    // `Any`-wildcard term. A type-only mismatch (not a `TypeReferenceExpression`) rejects
    // early.
    //
    // The C# direct dispatch (`this.Type.DoMatch`) assumes the required child is present; the
    // port routes it through `AstNode::MatchRequired` (the same-class static helper) because
    // C++ `[class.access.derived]` forbids a derived node from calling the protected
    // `DoMatch` through a base `AstType*`. `MatchRequired` guards a missing pattern-side
    // child defensively (a null pattern child does not match; the C# would null-deref), and a
    // null candidate child flows through the child's `DoMatch(nullptr)` which returns false.
    // For well-formed nodes (the child set) the behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<TypeReferenceExpression*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(type_, o->type_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): there is no scalar member, so `Clone` copies the
    // annotation channel (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
    // pattern) and deep-clones the child through the setter (which re-parents and re-indexes
    // via `SetChildNode`). The print-time `StartLocation`/`EndLocation` are not stored on
    // this node (no own location fields -- the base fields hold the print-time span), so only
    // the child + annotation channel are copied. The child is skipped if absent (`Clone`
    // tolerates a missing child even though the slot is required -- the invariant is enforced
    // by `CheckInvariant`, not by `Clone`). `AstType::Clone()` returns `AstType*` (the
    // covariant override), which `Type(AstType*)` accepts directly.
    TypeReferenceExpression* Clone() const override {
        auto* node = new TypeReferenceExpression();
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. A required slot is non-null only by invariant, so the pointer is
    // null until the child is set. The plain `AstType` (no shadowing).
    AstType* type_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXPRESSIONS_TYPEREFERENCEEXPRESSION_HPP
