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

// Port of the `UsingAliasDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/UsingAliasDeclaration.cs (the generated
// `UsingAliasDeclaration.g.cs` + the hand-written partial). The next in-order Phase-5 piece per
// the D288 plan (the namespace-level directive family, C# grammar 14.4-14.6), the sibling of
// `UsingDeclaration` (it shares the `Slots::Import` kind and the `UsingKeyword` const).
//
// The `using_alias_directive ::= 'using' identifier '=' type ';'` (C# grammar 14.6.2): a sealed
// `AstNode` (deriving directly from the `AstNode` root -- the `VariableInitializer` D266 /
// `CatchClause` D269 / `ExternAliasDeclaration` direct-`AstNode` precedent) with two single
// `[Slot]` children in source declaration order: a NON-nullable `string Alias` string-name
// `[Slot("Alias")]` over a backing `AliasToken` `Identifier` slot at flattened index 0 (the
// `LabelStatement` D259 / `VariableInitializer` D266 non-nullable-string-name-`[Slot]` shape --
// `Identifier::Create` not `CreateIfNotEmpty`, `std::string` return, required token), and a
// REQUIRED `AstType Import` `[Slot("Import")]` single slot at flattened index 1 (the
// `TypeReferenceExpression` D245 one-required-`AstType`-slot shape). The generator emits the
// const-index `SetChildNode(ref field, value, index)` setters for both (no collection precedes
// either slot, so each flattened index is the constant slot position 0/1); `GetChildCount` is
// the constant 2 and `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat two-case index switch.
//
// The `UsingKeyword` public-API const string (`"using"`, shared verbatim with
// `UsingDeclaration.UsingKeyword`) ports as a `static constexpr const char*` -- a static field
// (not instance state), so it is NOT in `MembersToMatch`/`DoMatch` (the generator's
// `MembersToMatch` iterates only instance `IPropertySymbol`s; a `public const string` is a static
// field -- the `CheckedExpression.CheckedKeyword` D234 / `ThrowExpression.ThrowKeyword` D235
// precedent).
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is UsingAliasDeclaration o && MatchString(this.Alias,
// o.Alias) && this.Import.DoMatch(o.Import, match)`. The `Alias` term is a `String` `MatchString`
// (the `$any$` wildcard in the pattern's `Alias` matches any candidate alias); the backing
// `AliasToken` is a generated non-`partial` `[Slot]` (not seen by the source-property scan at
// generation time), so it never appears in `MembersToMatch` (no double-match). The `Import` term
// is a NON-NULLABLE recursive child, so the generator emits the direct `this.Import.DoMatch`
// term (NOT `MatchOptional`, which the generator emits only for a nullable recursive child). A
// type-only mismatch (not a `UsingAliasDeclaration`) rejects early. `Clone` is inherited in C#
// (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// deep-clones both children through their setters (which re-parent) and copies the annotation
// channel.
//
// NO C++ name-shadowing crux (unlike `SimpleType` D237 / `IdentifierExpression` D246 whose
// `Identifier()` accessor shadows the `Identifier` class): the `[Slot("Alias")]` argument names
// the slot KIND "Alias" but the PROPERTY is "Alias", so the string accessor is `Alias()` (NOT
// `Identifier()`), and the backing token accessor is `AliasToken()` (NOT `IdentifierToken()`).
// Neither shadows the `Identifier` CLASS in this class scope (no member is named `Identifier`),
// so NO elaborated-type-specifier (`class Identifier`) is needed anywhere, and the
// `Identifier::Create` factory call is unqualified -- the `LabelStatement` D259 /
// `VariableInitializer` D266 differently-named-property precedent. The `Import()` accessor
// likewise does NOT collide with the `AstType` base type (no class named `Import` lives in the
// `Syntax` namespace -- the `Attribute` D240 / `TypeReferenceExpression` D245 lesson), so no
// `class AstType` elaboration is needed either.
//
// The generated ctors (the generator's `WriteConstructors`): the `Alias` string-name `[Slot]` is
// a "required" ctor param regardless of optionality (the generator's line-168 rule), and the
// `Import` `AstType` is required, so `RequiredConstructorPrefixLength` is 2 == the full count,
// and `ConstructorPrefixLengths` is {2}; the only generated ctors are the empty ctor + the
// `(string alias, AstType import)` all-params ctor (no shorter prefix ctor and no `params`
// overload since there is no collection). The `(string, AstType)` two-arg ctor is NOT
// `explicit` (a multi-arg ctor is not a converting ctor). The hand-written
// `UsingAliasDeclaration(string alias, string nameSpace)` convenience ctor (which calls
// `AddChild(Identifier.Create(alias), Slots.Alias)` + `AddChild(new SimpleType(nameSpace),
// Slots.Import)`) ports NOW (both `Identifier::Create` D227 and `SimpleType(string)` D237 are
// ported, and `AddChild` is ported D223); the two-arg `(string, string)` ctor overloads with the
// generated `(string, AstType)` ctor without ambiguity (`std::string` and `AstType*` are distinct
// non-convertible types -- `UsingAliasDeclaration("x", st.get())` selects the `(string, AstType)`
// ctor via the `SimpleType*` -> `AstType*` implicit upcast, and `UsingAliasDeclaration("x",
// std::string("y"))` selects the `(string, string)` ctor). The internally-created `Identifier`
// and `SimpleType` are not deleted on the parent's destruction (the established D223 non-owning
// raw-pointer model -- the same leak profile as a `Clone` deep-copy).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_USINGALIASDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_USINGALIASDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class UsingAliasDeclaration : AstNode`. `final` (the C#
// `sealed`): no further derivation. The non-nullable-string-name-`[Slot]`-plus-a-required-
// `AstType`-slot shape (the `VariableInitializer` D266 two-single-slot shape with the second
// slot an `AstType` instead of a nullable `Expression`), plus the `UsingKeyword` const string.
class UsingAliasDeclaration final : public AstNode {
public:
    ~UsingAliasDeclaration() override = default;

    // The generated empty ctor (the C# `public UsingAliasDeclaration()`). `AliasToken` and
    // `Import` both default to null. The `AliasToken` is a REQUIRED slot, so a
    // default-constructed node is only valid until `Alias` is set (or until `DoMatch`/
    // `CheckInvariant` observe the missing token) -- the `LabelStatement` D259 /
    // `VariableInitializer` D266 required-slot behavior; the `Import` is likewise required.
    UsingAliasDeclaration() = default;

    // The generated all-params ctor (the C# `public UsingAliasDeclaration(string alias, AstType
    // import)`); the `Alias` string-name `[Slot]` is a "required" ctor param regardless of
    // optionality (the generator's line-168 rule), and the `Import` `AstType` is required, so
    // `RequiredConstructorPrefixLength` is 2 == the full count, and this two-arg form is the
    // full all-params ctor (no shorter prefix ctor and no `params` overload since there is no
    // collection). The generated body is `this.Alias = alias; this.Import = import;` -- the
    // `Alias` setter creates the token via `Identifier::Create` (an empty alias yields a token
    // with an empty `Name`, NOT a null token -- the non-nullable behaviour, faithful to the C#
    // `string`). The two-arg form is NOT `explicit` (a multi-arg ctor is not a converting ctor).
    UsingAliasDeclaration(std::string alias, AstType* import)
        : UsingAliasDeclaration() {
        Alias(std::move(alias));
        Import(import);
    }

    // The hand-written `(string alias, string nameSpace)` convenience ctor (the C#
    // `public UsingAliasDeclaration(string alias, string nameSpace)`). Calls `AddChild(
    // Identifier.Create(alias), Slots.Alias)` + `AddChild(new SimpleType(nameSpace),
    // Slots.Import)` -- both `Identifier::Create` (D227) and `SimpleType(string)` (D237) are
    // ported, and `AddChild` is ported (D223). Overloads with the generated `(string, AstType)`
    // ctor without ambiguity: `std::string` and `AstType*` are distinct non-convertible types
    // (the `AssignmentExpression` D230 / `DoWhileStatement` D258 two-ctor-overload precedent --
    // the second param distinguishes the overloads). The internally-created `Identifier` and
    // `SimpleType` are not deleted on the parent's destruction (the D223 non-owning raw-pointer
    // model -- the same leak profile as a `Clone` deep-copy).
    UsingAliasDeclaration(std::string alias, std::string nameSpace)
        : UsingAliasDeclaration() {
        AddChild(Identifier::Create(std::move(alias)), &Slots::Alias);
        AddChild(new SimpleType(std::move(nameSpace)), &Slots::Import);
    }

    // The `UsingKeyword` public-API const string (the C# `public const string UsingKeyword =
    // "using"`, shared verbatim with `UsingDeclaration.UsingKeyword`). A `static constexpr
    // const char*` -- a static field, NOT instance state, so it is NOT in `MembersToMatch`/
    // `DoMatch` (the generator's `MembersToMatch` iterates only instance `IPropertySymbol`s;
    // a `public const string` is a static field -- the `CheckedExpression.CheckedKeyword` D234
    // / `ThrowExpression.ThrowKeyword` D235 precedent). It is part of the node's public API (the
    // output visitor reads `UsingAliasDeclaration.UsingKeyword`), so it ports now.
    static constexpr const char* UsingKeyword = "using";

    // ---- The `AliasToken` slot (the backing `Identifier` token of the alias) ---------------
    // The generated `[Slot("Alias")] public partial Identifier AliasToken` -- a single
    // REQUIRED (non-nullable) `Identifier` slot at flattened index 0. The const-index
    // `SetChildNode(ref field, value, 0)` setter (no collection precedes it). NO name shadowing
    // (the `AliasToken` accessor does NOT collide with the `Identifier` class -- no member is
    // named `Identifier`).
    Identifier* AliasToken() const { return aliasToken_; }
    void AliasToken(Identifier* value) {
        SetChildNode(aliasToken_, value, 0);
    }

    // ---- The `Alias` string-name accessor (over the token) ------------------------------
    // The generated `public partial string Alias` -- a convenience string over the `AliasToken`
    // slot. A NON-optional name (the C# `string`, not `string?`): `get` returns
    // `AliasToken.Name` (deref the token -- a null token is a half-constructed node that would
    // `NullReferenceException` in C#); `set` creates the token via `Identifier.Create` (NOT
    // `CreateIfNotEmpty` -- a non-nullable name creates a token even for an empty string, so an
    // empty name yields a token with an empty `Name`, not a null token -- the
    // `MemberType.MemberName` D238 / `LabelStatement.Label` D259 / `VariableInitializer.Name`
    // D266 precedent). `Alias()` returns `std::string` (a copy of the token's name); the
    // `Identifier::Create` factory call is unqualified (the `Alias()` accessor does NOT shadow
    // the `Identifier` class -- no member is named `Identifier`).
    std::string Alias() const { return aliasToken_->Name(); }
    void Alias(std::string_view value) {
        AliasToken(Identifier::Create(std::string(value)));
    }

    // ---- The `Import` slot (the required AstType) -----------------------------------------
    // The generated `[Slot("Import")] public partial AstType Import` -- a single, REQUIRED
    // (non-nullable) `AstType` child at flattened index 1. The const-index
    // `SetChildNode(ref field, value, 1)` setter (no collection precedes it). NO name shadowing
    // (the `Import()` accessor does NOT collide with the `AstType` base type -- no class named
    // `Import` lives in the `Syntax` namespace -- the `Attribute` D240 / `TypeReferenceExpression`
    // D245 lesson), so the element type is the plain `AstType`.
    AstType* Import() const { return import_; }
    void Import(AstType* value) {
        SetChildNode(import_, value, 1);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ----------------
    // The `AliasTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Alias`, required
    // -- the `Alias` is non-nullable so the token is a required slot, `IsOptional=false`). NO
    // name shadowing (`Identifier` resolves to the class -- no member is named `Identifier`),
    // so the element type is the plain `Identifier`. `Slots::Alias` is a NEW `Slots` constant
    // ported this iteration (a `CSharpSlotInfoT<Identifier>` for the `[Slot("Alias")]` backing
    // token kind).
    static inline const CSharpSlotInfoT<Identifier> AliasTokenSlot{"AliasToken", false, &Slots::Alias, false};

    // The `ImportSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Import`, required --
    // the `[Slot("Import")]` is a required single slot, so `IsOptional=false`). NO name
    // shadowing (`AstType` resolves to the class -- no member is named `AstType`), so the
    // element type is the plain `AstType`. `Slots::Import` is a NEW `Slots` constant ported
    // this iteration (shared with `UsingDeclaration.Import`).
    static inline const CSharpSlotInfoT<AstType> ImportSlot{"Import", false, &Slots::Import, false};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitUsingAliasDeclaration` (`UsingAliasDeclaration` does not end in
    // "AstType", so the generator's visit-method-name default yields `VisitUsingAliasDeclaration`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitUsingAliasDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) ---------------------------
    // Two single slots at flattened indices 0 (`AliasToken`) and 1 (`Import`); no collection,
    // so `GetChildCount` is the constant 2 (each slot counts even when its child is absent) and
    // `GetChild`/`SetChild`/`GetChildSlotInfo` are a flat index switch (the generator's
    // `WriteReturnDispatchSwitch` shape with two cases).

    int GetChildCount() const override { return 2; }

    AstNode* GetChild(int index) const override {
        switch (index) {
            case 0: return aliasToken_;
            case 1: return import_;
            default: throw std::out_of_range("UsingAliasDeclaration::GetChild");
        }
    }

    void SetChild(int index, AstNode* value) override {
        switch (index) {
            case 0: SetChildNode(aliasToken_, static_cast<Identifier*>(value), 0); break;
            case 1: SetChildNode(import_, static_cast<AstType*>(value), 1); break;
            default: throw std::out_of_range("UsingAliasDeclaration::SetChild");
        }
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        switch (index) {
            case 0: return &AliasTokenSlot;
            case 1: return &ImportSlot;
            default: throw std::out_of_range("UsingAliasDeclaration::GetChildSlotInfo");
        }
    }

    // ---- DoMatch (the generated pattern match) ----------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is UsingAliasDeclaration o && MatchString(this.Alias, o.Alias) &&
    // this.Import.DoMatch(o.Import, match)`. The `Alias` term is a `String` `MatchString` (the
    // `$any$` wildcard in the pattern's `Alias` matches any candidate alias); the backing
    // `AliasToken` is a generated non-`partial` `[Slot]` (not seen by the source-property scan at
    // generation time), so it never appears in `MembersToMatch` (no double-match). The `Import`
    // term is a NON-NULLABLE recursive child, so the generator emits the direct
    // `this.Import.DoMatch(o.Import, match)` term (NOT `MatchOptional`, which the generator
    // emits only for a nullable recursive child). A type-only mismatch (not a
    // `UsingAliasDeclaration`) rejects early. `Alias()` returns `std::string` (the token's name);
    // `Pattern::MatchString` takes `std::optional<std::string_view>`, so the view is built per
    // side. `Alias` is non-nullable, so the `std::optional<std::string_view>` is always engaged
    // (a real name, never `nullopt` -- the `MemberType.MemberName` D238 precedent). The `Alias()`
    // calls are INLINED in the `MatchString` arguments (not pre-computed in locals) so the C# `&&`
    // short-circuit is preserved: `o->Alias()` derefs the candidate's token only after the type
    // check passed. The `std::string` temporaries live until the end of the full `return`
    // expression, keeping the `std::string_view` views valid for the `MatchString` call.
    //
    // The C# direct dispatch (`this.Import.DoMatch`) assumes the required child is present; the
    // port routes it through `AstNode::MatchRequired` (the same-class static helper) because
    // C++ `[class.access.derived]` forbids a derived node from calling the protected `DoMatch`
    // through a base `AstType*` (the `UnaryOperatorExpression` D231 / `TypeReferenceExpression`
    // D245 precedent). `MatchRequired` guards a missing pattern-side child defensively (a null
    // pattern child does not match; the C# would null-deref), and a null candidate child flows
    // through the child's `DoMatch(nullptr)` which returns false. For well-formed nodes (the
    // child set) the behavior is identical to the C#.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<UsingAliasDeclaration*>(other);
        if (o == nullptr)
            return false;
        if (!PatternMatching::Pattern::MatchString(
                std::optional<std::string_view>(std::string_view(Alias())),
                std::optional<std::string_view>(std::string_view(o->Alias()))))
            return false;
        return MatchRequired(import_, o->import_, std::move(match));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the annotation channel copied
    // (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // `AliasToken` deep-cloned through the setter when present (which re-parents; the cloned
    // token carries its own `Name`), and the `Import` deep-cloned through the setter when
    // present (which re-parents and re-indexes via `SetChildNode`). No scalar to copy (the
    // `Alias` string is derived from the token, so cloning the token carries it; the
    // `UsingKeyword` is a static const, not instance state). The `UsingAliasDeclaration` itself
    // has no own location fields (`StartLocation`/`EndLocation` are the print-time base fields
    // set by the unported output visitor), so they are not copied (the `LabelStatement` D259 /
    // `VariableInitializer` D266 no-location-copy precedent). NO elaborated specifiers (no
    // member is named `Identifier` or `AstType`); the `Identifier::Clone` returns `Identifier*`
    // which `AliasToken(Identifier*)` accepts directly, and `AstType::Clone` returns `AstType*`
    // which `Import(AstType*)` accepts directly.
    UsingAliasDeclaration* Clone() const override {
        auto* node = new UsingAliasDeclaration();
        node->CloneAnnotationsFrom(*this);
        if (aliasToken_ != nullptr)
            node->AliasToken(aliasToken_->Clone());
        if (import_ != nullptr)
            node->Import(import_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `aliasToken_` is null until the alias is set; the slot is REQUIRED, so
    // `CheckInvariant` asserts it is filled. `import_` is null until the import type is set; the
    // slot is likewise required. NO name shadowing (no member is named `Identifier` or
    // `AstType`), so the field types are the plain `Identifier`/`AstType`.
    Identifier* aliasToken_ = nullptr;
    AstType* import_ = nullptr;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_USINGALIASDECLARATION_HPP
