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

// Port of the `ExternAliasDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/ExternAliasDeclaration.cs (the generated
// `ExternAliasDeclaration.g.cs`; the hand-written partial declares only the one slot property,
// no ctors, no helpers, no const strings). The next in-order Phase-5 piece per the D288 plan
// (the remaining GeneralScope nodes: the namespace-level directive family --
// `extern_alias_directive`/`using_directive`/`using_alias_directive`, C# grammar 14.4-14.6).
//
// The `extern_alias_directive ::= 'extern' 'alias' identifier ';'` (C# grammar 14.4): a sealed
// `AstNode` (deriving directly from the `AstNode` root, NOT `Expression`/`Statement`/`AstType`/
// `Trivia`/`EntityDeclaration` -- the `VariableInitializer` D266 / `CatchClause` D269
// direct-`AstNode` precedent) with a single NON-nullable `string Name` string-name
// `[Slot("Identifier")]` over a backing `NameToken` `Identifier` slot -- the `LabelStatement` D259
// / `SingleVariableDesignation` D264 / `VariableInitializer` D266 one-REQUIRED-string-name-slot
// shape applied to a direct-`AstNode` node. The generator emits the `NameTokenSlot` slot static
// pointing at the shared `Slots::Identifier` kind (already ported by `SimpleType` D237 -- no new
// `Slots` constant), the const-index `SetChildNode(ref field, value, 0)` setter, the
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` overrides over the one single token
// slot, and the `DoMatch` `return other is ExternAliasDeclaration o && MatchString(this.Name,
// o.Name)` -- the `Name` is a `String` `MatchString` term (the `$any$` wildcard in the pattern's
// `Name` matches any candidate name); the backing `NameToken` is a generated non-`partial`
// `[Slot]` (not seen by the source-property scan at generation time), so it never appears in
// `MembersToMatch`. `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the
// port overrides it (no `MemberwiseClone`): deep-clones the token and copies the annotation
// channel.
//
// NO C++ name-shadowing crux (unlike `SimpleType` D237 / `IdentifierExpression` D246 whose
// `Identifier()` accessor shadows the `Identifier` class): the `[Slot("Identifier")]` argument
// names the slot KIND "Identifier" but the PROPERTY is "Name", so the string accessor is
// `Name()` (NOT `Identifier()`) and the backing token accessor is `NameToken()` (NOT
// `IdentifierToken()`). Neither shadows the `Identifier` CLASS in this class scope (no member is
// named `Identifier`), so NO elaborated-type-specifier (`class Identifier`) is needed anywhere,
// and the `Identifier::Create` factory call in the `Name` setter is unqualified -- the
// `MemberType.MemberName` D238 / `LabelStatement` D259 / `VariableInitializer` D266
// differently-named-property precedent applied to the extern-alias directive.
//
// The NON-nullable `string Name` (the C# `string`, not `string?`): the backing `NameToken` is a
// REQUIRED (non-nullable) slot (IsOptional=false), the `Name()` getter DEREFS the token
// (returning `std::string`, NOT `std::optional<std::string>` -- a null token is a
// half-constructed node that would `NullReferenceException` in C#), and the `Name` setter uses
// `Identifier::Create` (NOT `CreateIfNotEmpty` -- a non-nullable name creates a token even for an
// empty string, so an empty name yields a token with an empty `Name`, not a null token -- the
// `MemberType.MemberName` D238 / `LabelStatement.Label` D259 / `VariableInitializer.Name` D266
// precedent). `CheckInvariant` therefore passes only on a FILLED node (the token is a required
// slot, so an empty node violates the required-slot invariant -- the assert fires in debug). A
// nameless `ExternAliasDeclaration` is a half-constructed node (UB to deref or pass to
// `DoMatch`), so the tests exercise the empty-name case as a real "" match (two empty names
// match; empty does not match non-empty), not a nameless/nullopt case.
//
// The generated ctors (the generator's `WriteConstructors`): a string-name `[Slot]` is a
// "required" ctor param regardless of optionality (the generator's line-168 rule), so
// `RequiredConstructorPrefixLength` is 1 and `ConstructorPrefixLengths` is {1}; the only
// generated ctors are the empty ctor + the `(string name)` all-params ctor. The `(string name)`
// ctor body is `this.Name = name;` -- it calls the string setter, which creates the token via
// `Identifier::Create` (an empty name yields a token with an empty `Name`, NOT a null token --
// the non-nullable behaviour, faithful to the C# `string`). `ExternAliasDeclaration.cs` declares
// NO hand-written ctors. The `(std::string)` single-arg ctor is `explicit` (a single-argument
// ctor is a converting ctor by default -- the `SimpleType` D237 / `LabelStatement` D259 /
// `VariableInitializer` D266 precedent).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_EXTERNALIASDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_EXTERNALIASDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class ExternAliasDeclaration : AstNode`. `final` (the C#
// `sealed`): no further derivation. The one-REQUIRED-string-name-`[Slot]` node (the
// `LabelStatement` D259 string-name-`[Slot]` shape with a NON-nullable `Name`), no const
// strings, deriving directly from the `AstNode` root.
class ExternAliasDeclaration final : public AstNode {
public:
    ~ExternAliasDeclaration() override = default;

    // The generated empty ctor (the C# `public ExternAliasDeclaration()`). `NameToken` defaults
    // to null (no name), but the token is a REQUIRED slot, so a default-constructed node is only
    // valid until the name is set (or until `DoMatch`/`CheckInvariant` observe the missing
    // token) -- the `IdentifierExpression` D246 / `LabelStatement` D259 / `VariableInitializer`
    // D266 required-slot behavior.
    ExternAliasDeclaration() = default;

    // The generated all-params ctor (the C# `public ExternAliasDeclaration(string name)`); the
    // `Name` string-name `[Slot]` is a "required" ctor param regardless of optionality (the
    // generator's line-168 rule), so `RequiredConstructorPrefixLength` is 1 == the full count,
    // and this single-arg form is both the required-prefix ctor and the all-params ctor (no
    // shorter prefix ctor and no params overload since there is no collection). The generated
    // body is `this.Name = name;` -- it calls the string setter, which creates the token via
    // `Identifier::Create` (an empty name yields a token with an empty `Name`, NOT a null token
    // -- the non-nullable behaviour, faithful to the C# `string`). `explicit` because a
    // single-argument ctor is a converting ctor by default (the `SimpleType` D237 /
    // `LabelStatement` D259 / `VariableInitializer` D266 precedent). Braced-init in tests avoids
    // the most-vexing-parse (`ExternAliasDeclaration e(std::string())` would declare `e` as a
    // function -- the D236/D257 precedent).
    explicit ExternAliasDeclaration(std::string name) : ExternAliasDeclaration() {
        Name(std::move(name));
    }

    // ---- The `NameToken` slot (the backing `Identifier` token of the name) ---------------
    // The generated `[Slot("Identifier")] public partial Identifier NameToken` -- a single
    // REQUIRED (non-nullable) `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). NO name shadowing
    // (the `NameToken` accessor does NOT collide with the `Identifier` class -- no member is
    // named `Identifier`).
    Identifier* NameToken() const { return nameToken_; }
    void NameToken(Identifier* value) {
        SetChildNode(nameToken_, value, 0);
    }

    // ---- The `Name` string-name accessor (over the token) ------------------------------
    // The generated `public partial string Name` -- a convenience string over the `NameToken`
    // slot. A NON-optional name (the C# `string`, not `string?`): `get` returns
    // `NameToken.Name` (deref the token -- a null token is a half-constructed node that would
    // `NullReferenceException` in C#); `set` creates the token via `Identifier.Create` (NOT
    // `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
    // empty name yields a token with an empty `Name`, not a null token -- the
    // `MemberType.MemberName` D238 / `LabelStatement.Label` D259 / `VariableInitializer.Name`
    // D266 precedent). `Name()` returns `std::string` (a copy of the token's name); the
    // `Identifier::Create` factory call is unqualified (the `Name()` accessor does NOT shadow
    // the `Identifier` class -- no member is named `Identifier`).
    std::string Name() const { return nameToken_->Name(); }
    void Name(std::string_view value) {
        NameToken(Identifier::Create(std::string(value)));
    }

    // ---- The per-node slot static (pointing at the shared `Slots` kind) ----------------
    // The `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
    // required -- the name is non-nullable so the token is a required slot). NO name shadowing
    // (`Identifier` resolves to the class -- no member is named `Identifier`), so the element
    // type is the plain `Identifier`. `Slots::Identifier` is already ported (by `SimpleType`
    // D237), so no new `Slots` constant.
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitExternAliasDeclaration` (`ExternAliasDeclaration` does not end in
    // "AstType", so the generator's visit-method-name default yields `VisitExternAliasDeclaration`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitExternAliasDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitExternAliasDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitExternAliasDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // One single REQUIRED `Identifier` slot at flattened index 0 (`NameToken`); no collection,
    // so `GetChildCount` is the constant 1 (the slot counts even when the token is absent) and
    // `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch.

    int GetChildCount() const override { return 1; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return nameToken_;
            default: throw std::out_of_range("ExternAliasDeclaration::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(nameToken_, static_cast<Identifier*>(value), 0); break;
            default: throw std::out_of_range("ExternAliasDeclaration::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &NameTokenSlot;
            default: throw std::out_of_range("ExternAliasDeclaration::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ExternAliasDeclaration o && MatchString(this.Name, o.Name)`. The `Name`
    // term is a `String` `MatchString` (the `$any$` wildcard in the pattern's `Name` matches any
    // candidate name); the backing `NameToken` is a generated non-`partial` `[Slot]` (not seen
    // by the source-property scan at generation time), so it never appears in `MembersToMatch`
    // (no double-match). A type-only mismatch (not an `ExternAliasDeclaration`) rejects early.
    // `Name()` returns `std::string` (the token's name); `Pattern::MatchString` takes
    // `std::optional<std::string_view>`, so the view is built per side. `Name` is non-nullable,
    // so the `std::optional<std::string_view>` is always engaged (a real name, never `nullopt`
    // -- the `MemberType.MemberName` D238 precedent). The `Name()` calls are INLINED in the
    // `MatchString` arguments (not pre-computed in locals) so the C# `&&` short-circuit is
    // preserved: `o->Name()` derefs the candidate's token only after the type check passed. The
    // `std::string` temporaries live until the end of the full `return` expression, keeping the
    // `std::string_view` views valid for the `MatchString` call.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<ExternAliasDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
            std::optional<std::string_view>(std::string_view(Name())),
            std::optional<std::string_view>(std::string_view(o->Name())));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), and the
    // `NameToken` deep-cloned through the setter when present (which re-parents; the cloned
    // token carries its own `Name`). No scalar to copy (the `Name` string is derived from the
    // token, so cloning the token carries it). The `ExternAliasDeclaration` itself has no own
    // location fields (`StartLocation`/`EndLocation` are the print-time base fields set by the
    // unported output visitor), so they are not copied (the `LabelStatement` D259 /
    // `VariableInitializer` D266 no-location-copy precedent). NO elaborated `class Identifier`
    // (no member named `Identifier` shadows the class); the token `Clone()` returns
    // `Identifier*` (the `Identifier::Clone` override), which the `NameToken(Identifier*)`
    // setter accepts directly.
    ExternAliasDeclaration* Clone() const override {
        auto* node = new ExternAliasDeclaration();
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing field. `nameToken_` is null until the name is set; the slot is REQUIRED, so
    // `CheckInvariant` asserts it is filled. NO name shadowing (no member is named `Identifier`),
    // so the field type is the plain `Identifier`.
    Identifier* nameToken_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_EXTERNALIASDECLARATION_HPP
