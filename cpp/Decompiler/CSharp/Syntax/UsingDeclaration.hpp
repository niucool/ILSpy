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

// Port of the `UsingDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/UsingDeclaration.cs (the generated
// `UsingDeclaration.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D288 plan (the namespace-level directive family, C# grammar 14.4-14.6).
//
// The `using_directive ::= 'using' type ';' | 'using' 'static' type ';'` (C# grammar 14.6.3,
// 14.6.4): a sealed `AstNode` (deriving directly from the `AstNode` root -- the
// `VariableInitializer` D266 / `CatchClause` D269 / `ExternAliasDeclaration` direct-`AstNode`
// precedent) with a single REQUIRED (non-nullable) `[Slot("Import")] AstType Import` child slot
// -- the `TypeReferenceExpression` D245 / `DefaultValueExpression` D245 / `SizeOfExpression`
// D245 one-required-single-`AstType`-slot shape applied to a direct-`AstNode` node. The
// generator emits the `ImportSlot` slot static pointing at the shared `Slots::Import` kind
// (a NEW `Slots` constant, ported this iteration alongside `UsingAliasDeclaration.Import` which
// reuses it -- the kind is shared across both using-directive nodes), the const-index
// `SetChildNode(ref field, value, 0)` setter (no collection precedes it), the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single slot,
// and the `DoMatch` `return other is UsingDeclaration o && this.Import.DoMatch(o.Import, match)`
// (a NON-NULLABLE recursive child, so the generator emits the direct `this.Import.DoMatch` term
// -- NOT `MatchOptional`, which the generator emits only for a nullable recursive child; there
// is no scalar enum, so there is no `Any`-wildcard term). `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones the `Import` child through the setter (which re-parents) and copies the annotation
// channel.
//
// The `UsingKeyword` public-API const string (`"using"`, shared verbatim with
// `UsingAliasDeclaration.UsingKeyword`) ports as a `static constexpr const char*` -- a static
// field (not instance state), so it is NOT in `MembersToMatch`/`DoMatch` (the generator's
// `MembersToMatch` iterates only instance `IPropertySymbol`s; a `public const string` is a
// static field, not an instance property -- the `CheckedExpression.CheckedKeyword` D234 /
// `ThrowExpression.ThrowKeyword` D235 precedent).
//
// The hand-written partial declares the `[ExcludeFromMatch] public string Namespace` computed
// property (reads `ConstructNamespace(Import)` -- a `MemberType`-chain walk that flattens a
// dotted `AstType` into its namespace string) and the hand-written
// `UsingDeclaration(string nameSpace)` convenience ctor (which calls `AddChild(AstType.Create(
// nameSpace), Slots.Import)`). BOTH are DEFERRED: `ConstructNamespace`/`Namespace` is a
// behaviour helper consumed by the unported resolver/output stage (the `IsExtensionMethod` D284
// value-vs-behaviour discriminator -- a computed read-only property needed by the output stage,
// not by `DoMatch`), and `AstType.Create` is the D236-deferred `AstType` factory helper (it
// builds a `MemberType` chain from a dotted name; `SimpleType`/`MemberType` are ported, but the
// helper itself was deferred and lands with the resolver/output stage that consumes it). The
// hand-written `(string)` ctor is therefore NOT ported this iteration (only the generated empty
// + `(AstType)` ctors are portable); a `UsingDeclaration` is built via the empty ctor +
// `Import(astType)` setter, or via the generated `(AstType)` ctor.
//
// NO C++ name-shadowing crux (unlike `CastExpression`/`AsExpression`/`IsExpression` whose
// `Expression()` accessor shadows the `Expression` base type): the only child accessor is
// `Import` of type `AstType`, and the `Import()` accessor does NOT collide with the `AstType`
// base type (no class named `Import` lives in the `Syntax` namespace -- the `Attribute` D240 /
// `TypeReferenceExpression` D245 lesson), so the operand type is the plain `AstType` everywhere
// and no elaborated-type-specifier is needed.
//
// The generated ctors (the generator's `WriteConstructors`): the one `[Slot]` `Import` is
// required, so `RequiredConstructorPrefixLength` is 1 == the full count, and
// `ConstructorPrefixLengths` is {1}; the only generated ctors are the empty ctor + the
// `(AstType import)` all-params ctor (no shorter prefix ctor and no `params` overload since
// there is no collection). The `(AstType)` single-arg ctor is `explicit` (a single-argument ctor
// is a converting ctor by default -- the `TypeReferenceExpression` D245 / `DefaultValueExpression`
// D245 precedent). The hand-written `(string)` ctor (deferred) would overload with the
// `(AstType)` ctor without ambiguity (`std::string` and `AstType*` are distinct non-convertible
// types), but it is not ported this iteration.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_USINGDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_USINGDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class UsingDeclaration : AstNode`. `final` (the C# `sealed`):
// no further derivation. The one-REQUIRED-single-`AstType`-slot node (the `TypeReferenceExpression`
// D245 shape applied to a direct-`AstNode` node), plus the `UsingKeyword` const string. The
// `Namespace` computed property and the hand-written `(string)` ctor are deferred (they need the
// `ConstructNamespace`/`AstType.Create` helpers consumed by the unported output/resolver stage).
class UsingDeclaration final : public AstNode {
public:
    ~UsingDeclaration() override = default;

    // The generated empty ctor (the C# `public UsingDeclaration()`). The `Import` slot defaults
    // to null (no type). A null slot violates the required-slot invariant, so a
    // default-constructed node is only valid until `Import` is set (or until `DoMatch`/
    // `CheckInvariant` observe the missing child) -- the `TypeReferenceExpression` D245 /
    // `DefaultValueExpression` D245 required-slot behavior.
    UsingDeclaration() = default;

    // The generated all-params ctor (the C# `public UsingDeclaration(AstType import)`). Delegated
    // to the empty ctor then the setter so the slot machinery re-parents and re-indexes the
    // child. There is no scalar enum, so the single slot child is the only ctor param (the
    // generator emits a single full ctor -- the one slot is required, so the required prefix IS
    // the full set; no shorter prefix ctor and no `params` overload, since no collection slot is
    // present). `UsingDeclaration.cs` declares a hand-written `(string nameSpace)` convenience
    // ctor (deferred -- it calls `AstType.Create`, the D236-deferred helper), so the only
    // portable ctor besides the empty ctor is this generated `(AstType)` one. `explicit` because
    // a single-argument ctor is a converting ctor by default (the `TypeReferenceExpression` D245
    // precedent).
    explicit UsingDeclaration(AstType* import)
        : UsingDeclaration() {
        Import(import);
    }

    // The `UsingKeyword` public-API const string (the C# `public const string UsingKeyword =
    // "using"`, shared verbatim with `UsingAliasDeclaration.UsingKeyword`). A `static constexpr
    // const char*` -- a static field, NOT instance state, so it is NOT in `MembersToMatch`/
    // `DoMatch` (the generator's `MembersToMatch` iterates only instance `IPropertySymbol`s;
    // a `public const string` is a static field -- the `CheckedExpression.CheckedKeyword` D234
    // / `ThrowExpression.ThrowKeyword` D235 precedent). It is part of the node's public API (the
    // output visitor reads `UsingDeclaration.UsingKeyword`), so it ports now.
    static constexpr const char* UsingKeyword = "using";

    // ---- The `Import` slot (the required AstType) -----------------------------------------
    // The generated `[Slot("Import")] public partial AstType Import` -- a single, REQUIRED
    // (non-nullable) `AstType` child at flattened index 0. The generator emits the const-index
    // `SetChildNode(ref field, value, 0)` setter (the single slot is the first and only slot,
    // no collection precedes it), so the index is assigned directly and the parent's indices stay
    // valid by construction. The C# getter returns the backing field null-forgiving (`field!`)
    // because the slot is required; the port returns the raw pointer (a required slot is non-null
    // only by invariant, not by type), so callers must keep the child set. NO name shadowing (the
    // `Import()` accessor does NOT collide with the `AstType` base type -- no class named
    // `Import` lives in the `Syntax` namespace -- the `Attribute` D240 / `TypeReferenceExpression`
    // D245 lesson), so the operand type is the plain `AstType`.
    AstType* Import() const { return import_; }
    void Import(AstType* value) {
        SetChildNode(import_, value, 0);
    }

    // ---- The per-node slot static (pointing at the shared `Slots` kind) ----------------
    // The `ImportSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Import`, required -- the
    // `[Slot("Import")]` is a required single slot, so `IsOptional=false`). NO name shadowing
    // (`AstType` resolves to the class -- no member is named `AstType`), so the element type is
    // the plain `AstType`. `Slots::Import` is a NEW `Slots` constant ported this iteration
    // (alongside `UsingAliasDeclaration.Import` which reuses the same kind).
    static inline const CSharpSlotInfoT<AstType> ImportSlot{"Import", false, &Slots::Import, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitUsingDeclaration` (`UsingDeclaration` does not end in "AstType", so
    // the generator's visit-method-name default yields `VisitUsingDeclaration`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitUsingDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Import`); no collection, so `GetChildCount` is the
    // constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the
    // generator's `WriteReturnDispatchSwitch` shape, with a single case).

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return import_;
            default: throw std::out_of_range("UsingDeclaration::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(import_, static_cast<AstType*>(value), 0); break;
            default: throw std::out_of_range("UsingDeclaration::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &ImportSlot;
            default: throw std::out_of_range("UsingDeclaration::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is UsingDeclaration o && this.Import.DoMatch(o.Import, match)`. The child
    // is NON-NULLABLE recursive, so the generator emits the direct `this.Import.DoMatch(o.Import,
    // match)` term (NOT `MatchOptional`, which the generator emits only for a nullable recursive
    // child); there is no scalar enum, so there is no `Any`-wildcard term. A type-only mismatch
    // (not a `UsingDeclaration`) rejects early.
    //
    // The C# direct dispatch (`this.Import.DoMatch`) assumes the required child is present; the
    // port routes it through `AstNode::MatchRequired` (the same-class static helper) because
    // C++ `[class.access.derived]` forbids a derived node from calling the protected `DoMatch`
    // through a base `AstType*`. `MatchRequired` guards a missing pattern-side child defensively
    // (a null pattern child does not match; the C# would null-deref), and a null candidate child
    // flows through the child's `DoMatch(nullptr)` which returns false. For well-formed nodes
    // (the child set) the behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<UsingDeclaration*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(import_, o->import_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): there is no scalar member (the `UsingKeyword` is a static
    // const, not instance state), so `Clone` copies the annotation channel (`CloneAnnotationsFrom`
    // + `ReparentTrivia`, the D223 concrete-clone pattern) and deep-clones the `Import` child
    // through the setter (which re-parents and re-indexes via `SetChildNode`). The print-time
    // `StartLocation`/`EndLocation` are not stored on this node (no own location fields -- the
    // base fields hold the print-time span), so only the child + annotation channel are copied.
    // The child is skipped if absent (`Clone` tolerates a missing child even though the slot is
    // required -- the invariant is enforced by `CheckInvariant`, not by `Clone`).
    // `AstType::Clone()` returns `AstType*` (the covariant override), which `Import(AstType*)`
    // accepts directly.
    UsingDeclaration* Clone() const override {
        auto* node = new UsingDeclaration();
        node->CloneAnnotationsFrom(*this);
        if (import_ != nullptr)
            node->Import(import_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. A required slot is non-null only by invariant, so the pointer is null
    // until the child is set. The plain `AstType` (no shadowing -- no member is named `AstType`).
    AstType* import_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_USINGDECLARATION_HPP
