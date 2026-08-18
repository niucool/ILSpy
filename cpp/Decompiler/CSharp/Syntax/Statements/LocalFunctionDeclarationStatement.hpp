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

// Port of the `LocalFunctionDeclarationStatement` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.cs (the
// generated `LocalFunctionDeclarationStatement.g.cs`). The next in-order Phase-5 piece per the
// D281 plan ("MethodDeclaration ... unblocks LocalFunctionDeclarationStatement") -- the last
// remaining concrete statement node. A `local_function_declaration ::= method_declaration`
// (C# grammar 13.6.4): a sealed `Statement` wrapping a single REQUIRED `MethodDeclaration`
// `Declaration` child (the whole method declaration that the local function IS, with its own
// `ReturnType`/`NameToken`/`Parameters`/`Body`/... slots). The hand-written partial declares only
// the `Declaration` slot property, no ctors, no helpers, no const strings.
//
// It is the `CheckedStatement` D260 single-required-slot shape (a single REQUIRED concrete-type
// child at flattened index 0, no scalar, no const keyword) with a `MethodDeclaration` child
// instead of a `BlockStatement`. The generator emits the `DeclarationSlot` slot static pointing
// at the shared `Slots::MethodDeclaration` kind (a NEW kind, cycle-broken into
// `MethodDeclaration.hpp` after the `MethodDeclaration` class -- the `Slots::Attribute`/`Slots::Body`
// precedent, since `MethodDeclaration.hpp` includes `Slots.hpp` for its own per-node slot statics),
// the const-index `SetChildNode(ref field, value, 0)` setter, the `GetChildCount`/
// `GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single slot, and the `DoMatch`
// `return other is LocalFunctionDeclarationStatement o &&
// this.Declaration.DoMatch(o.Declaration, match)` -- a NON-nullable recursive child, so the
// generator emits the direct dispatch (NOT `MatchOptional`); routed through
// `AstNode::MatchRequired` (the D231 same-class static helper, since C++ `[class.access.derived]`
// forbids a derived node from calling the protected `DoMatch` through a base `AstNode*`).
// `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): deep-clones the child through the setter and copies the annotation channel.
//
// NO C++ name-shadowing crux: the `Declaration()` accessor is a member function, but no class
// named `Declaration` lives in the `Syntax` namespace, and the `MethodDeclaration` child type is
// in scope via the `MethodDeclaration.hpp` include (the `MethodDeclaration` name does not collide
// with any member here -- no member is named `MethodDeclaration`), so no elaborated-type-specifier
// is needed anywhere, and the plain `MethodDeclaration` resolves to the concrete class in every
// type position.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LOCALFUNCTIONDECLARATIONSTATEMENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LOCALFUNCTIONDECLARATIONSTATEMENT_HPP

#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <stdexcept>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class LocalFunctionDeclarationStatement : Statement`. `final`
// (the C# `sealed`): no further derivation. The `CheckedStatement` D260 single-required-slot
// shape with a `MethodDeclaration` child (no const keyword).
class LocalFunctionDeclarationStatement final : public Statement {
public:
    ~LocalFunctionDeclarationStatement() override = default;

    // The generated empty ctor (the C# `public LocalFunctionDeclarationStatement()`).
    // `Declaration` defaults to null (no wrapped method). A null declaration violates the
    // required-slot invariant, so a default-constructed node is only valid until `Declaration`
    // is set (or until `DoMatch`/`CheckInvariant` observe the missing child) -- the
    // `UnaryOperatorExpression` D231 required-slot behavior.
    LocalFunctionDeclarationStatement() = default;

    // The generated all-params ctor (the C# `public LocalFunctionDeclarationStatement(
    // MethodDeclaration declaration)`); the `Declaration` slot is REQUIRED so the
    // required-prefix length is 1, which IS the full count (1), so this single-arg form is both
    // the required-prefix ctor and the all-params ctor (no shorter prefix ctor and no params
    // overload since there is no collection). `explicit` because a single-argument ctor is a
    // converting ctor by default (the `CheckedStatement` D260 / the `ReturnStatement` D248
    // precedent). No name shadowing (no member is named `MethodDeclaration`), so the plain
    // `MethodDeclaration` (the concrete child type, in scope via the include) resolves.
    explicit LocalFunctionDeclarationStatement(MethodDeclaration* declaration)
        : LocalFunctionDeclarationStatement() {
        Declaration(declaration);
    }

    // The C# `[Slot("MethodDeclaration")] MethodDeclaration Declaration` -- a single, REQUIRED
    // (non-nullable) `MethodDeclaration` child at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes this slot). The C#
    // getter returns the backing field null-forgiving (`field!`) because the slot is required;
    // the port returns the raw pointer (a required slot is non-null only by invariant, not by
    // type). No name shadowing (no member/class is named `Declaration`), so the plain
    // `MethodDeclaration` resolves to the concrete child type.
    MethodDeclaration* Declaration() const { return declaration_; }
    void Declaration(MethodDeclaration* value) {
        SetChildNode(declaration_, value, 0);
    }

    // The generated slot static (per-node), pointing at the shared `Slots` kind. The
    // `IsOptional` flag is false (the slot is required -- the C# property is non-nullable); the
    // kind carries identity only. `Slots::MethodDeclaration` is the new shared "MethodDeclaration"
    // slot kind (cycle-broken into `MethodDeclaration.hpp`). No name shadowing, so the element
    // type is the plain `MethodDeclaration` (the concrete child type, in scope via the include).
    static inline const CSharpSlotInfoT<MethodDeclaration> DeclarationSlot{"MethodDeclaration", false, &Slots::MethodDeclaration, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitLocalFunctionDeclarationStatement`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitLocalFunctionDeclarationStatement(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single slot at flattened index 0 (`Declaration`); no collection, so `GetChildCount` is
    // the constant 1 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return declaration_;
            default: throw std::out_of_range("LocalFunctionDeclarationStatement::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(declaration_, static_cast<MethodDeclaration*>(value), 0); break;
            default: throw std::out_of_range("LocalFunctionDeclarationStatement::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &DeclarationSlot;
            default: throw std::out_of_range("LocalFunctionDeclarationStatement::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is LocalFunctionDeclarationStatement o &&
    // this.Declaration.DoMatch(o.Declaration, match)`. `Declaration` is a NON-NULLABLE recursive
    // child, so the generator emits the direct `this.Declaration.DoMatch(o.Declaration, match)`
    // term (NOT `MatchOptional`, which it emits only for a nullable recursive child). The C#
    // direct dispatch assumes the required child is present; the port routes it through
    // `AstNode::MatchRequired` (the same-class static helper) because C++ `[class.access.derived]`
    // forbids a derived node from calling the protected `DoMatch` through a base `AstNode*`.
    // `MatchRequired` guards a missing operand defensively (a null pattern child does not match;
    // the C# would null-deref), and a null candidate child flows through the operand's
    // `DoMatch(nullptr)` which returns false. For well-formed nodes (the declaration always set)
    // the behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<LocalFunctionDeclarationStatement*>(other);
        if (o == nullptr)
            return false;
        return MatchRequired(declaration_, o->declaration_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `Declaration` child deep-cloned through the setter (which re-parents and re-indexes via
    // `SetChildNode`). No own location fields (it does not derive `EndLocation`), so the
    // print-time `StartLocation`/`EndLocation` are not copied (the `CheckedStatement` D260
    // no-location-copy precedent). `MethodDeclaration::Clone()` returns `MethodDeclaration*`
    // (the covariant override through `EntityDeclaration*`/`AstNode*`), which the
    // `Declaration(MethodDeclaration*)` setter accepts directly. The covariant return is
    // `LocalFunctionDeclarationStatement*` (through `Statement*`, the `Statement::Clone`
    // pure-virtual).
    LocalFunctionDeclarationStatement* Clone() const override {
        auto* node = new LocalFunctionDeclarationStatement();
        node->CloneAnnotationsFrom(*this);
        if (declaration_ != nullptr)
            node->Declaration(static_cast<MethodDeclaration*>(declaration_->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. No name shadowing (no member/class is named `MethodDeclaration`), so
    // the field type is the plain concrete child class (in scope via the include). A required
    // slot is non-null only by invariant, so the pointer is null until the declaration is set.
    MethodDeclaration* declaration_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_STATEMENTS_LOCALFUNCTIONDECLARATIONSTATEMENT_HPP
