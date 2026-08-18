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
// OTHERWISE, ARISING FROM OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `ParameterDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/ParameterDeclaration.cs (the generated
// `ParameterDeclaration.g.cs` + the hand-written partial). The next in-order Phase-5 piece per the
// D277 plan ("ParameterDeclaration (a direct-AstNode node: Attributes + Type + Identifier +
// DefaultExpression -- unblocks IndexerDeclaration/ConstructorDeclaration/OperatorDeclaration/
// MethodDeclaration)"). The `fixed_parameter ::= attribute_section* ( 'this' | 'scoped'?
// ( 'ref' 'readonly'? | 'out' | 'in' ) )? type identifier ( '=' expression )?` and
// `parameter_array ::= attribute_section* 'params' type identifier` (C# grammar 15.6.2.1): a
// method/constructor/indexer/operator parameter -- the element of a `ParameterDeclaration`
// collection on those declarations (a `[Slot("Parameter")] AstNodeCollection<ParameterDeclaration>`
// -- the kind lands when the first owning declaration ports).
//
// The hand-written partial declares the seven const-string keyword tokens (`ThisModifier`/
// `ScopedRefKeyword`/`RefModifier`/`ReadonlyModifier` aliased to `ComposedType.ReadonlyKeyword`/
// `OutModifier`/`InModifier`/`ParamsModifier`), the four scalar properties (`HasThisModifier`/
// `IsParams`/`IsScopedRef` bools + `ParameterModifier` `ReferenceKind`), the four `[Slot]`
// properties (`Attributes`/`Type`/`Name`/`DefaultExpression`), and the typed `Clone` (no ctors, no
// helpers). The `[DecompilerAstNode(hasPatternPlaceholder: true)]` (the explicit
// `hasPatternPlaceholder: true` argument) means the node is NOT `sealed` (the generated
// `PatternPlaceholder` derives from it -- the `ArrayInitializerExpression` D250 /
// `VariableInitializer` D266 non-sealed precedent; the pattern placeholder is deferred, but the
// class stays non-`final` to match the C# and to not block the placeholder landing). It derives
// DIRECTLY from `AstNode` (not `EntityDeclaration`/`Expression`/`Statement`/`AstType`): a parameter
// is a structural node owned by a declaration's `Parameters` collection, not a member declaration
// (it carries no `SymbolKind`/`Modifiers`/`MatchAttributesAndModifiers` -- the `EntityDeclaration`
// machinery does NOT apply) -- the `VariableInitializer` D266 / `CatchClause` D269
// direct-`AstNode` precedent.
//
// The four slots in source declaration order: an `Attributes` `AstNodeCollection<AttributeSection>`
// collection at slot 0 (reusing the cycle-broken `Slots::AttributeSection`, NON-incremental -- the
// collection is the node's only collection but NOT its last slot, three singles trail it, so
// `supportsIncremental` is false and `Add`/`Insert`/`Remove`/single-slot-set invalidate the
// parent's indices for a lazy `EnsureChildIndices` rebuild, the `DestructorDeclaration` D272 /
// `EnumMemberDeclaration` D275 non-incremental precedent); a `[Slot("Type")] public partial
// AstType? Type` single NULLABLE slot at slot 1 (the parameter's declared type; index-less
// `SetChildNode` setter following the collection; reusing `Slots::Type`); a `[Slot("Identifier")]
// public partial string? Name` string-name `[Slot]` over a backing `NameToken` `Identifier?` single
// NULLABLE slot at slot 2 (the parameter's name -- a NULLABLE name since the property type is
// `string?`, a nameless parameter such as `__argList` may carry no token; index-less setter;
// `Identifier::CreateIfNotEmpty` so an empty/null name clears the token, faithful to the `string?`
// optionality -- the `GotoStatement.Label` D257 nullable-string-name-`[Slot]` precedent; reusing
// `Slots::Identifier`); and a `[Slot("Expression")] public partial Expression? DefaultExpression`
// single NULLABLE slot at slot 3 (the optional `= expression` default, e.g. `= 5` in `int x = 5`;
// index-less setter; reusing `Slots::Expression`). The flattened layout is `Attributes [0,
// attrCount)`, `Type` at `attrCount`, `NameToken` at `attrCount + 1`, `DefaultExpression` at
// `attrCount + 2`; `GetChildCount` is `attrCount + 3`.
//
// The four scalar properties are NOT `[Slot]`s (no child slots): `HasThisModifier`/`IsParams`/
// `IsScopedRef` (plain `bool`, set via the property setters -- the generator adds only settable
// ENUM-typed scalars to `CtorParams`, and a `bool` is not an enum) and `ParameterModifier` (the
// `ReferenceKind` enum, settable -- the generator adds it to `CtorParams`). They ARE in
// `MembersToMatch` (the generator adds every non-`[Slot]` settable instance property), and each
// emits a plain-equality `DoMatch` term (a `bool` and a no-`Any` enum both hit the `DoMatchTerm`
// fall-through -- the `ComposedType.HasRefSpecifier` D242 bool and the
// `DirectionExpression.FieldDirection` D235 no-`Any`-enum precedent).
//
// The generated `DoMatch` (the generator's `WriteDoMatch` over `MembersToMatch` in source
// declaration order): `return other is ParameterDeclaration o && this.Attributes.DoMatch(
// o.Attributes, match) && this.HasThisModifier == o.HasThisModifier && this.IsParams == o.IsParams
// && this.IsScopedRef == o.IsScopedRef && this.ParameterModifier == o.ParameterModifier &&
// MatchOptional(this.Type, o.Type, match) && MatchString(this.Name, o.Name) &&
// MatchOptional(this.DefaultExpression, o.DefaultExpression, match)`. The `Attributes` term is the
// collection recursive match (the generator emits a collection-typed recursive term directly, NOT
// `MatchOptional`, which it emits only for a nullable non-collection child); the three bool/enum
// scalars are the fall-through plain-equality; the `Type` and `DefaultExpression` terms are
// `MatchOptional` (nullable recursive children); the `Name` term is a `String` `MatchString` (the
// `$any$` wildcard in the pattern's `Name` matches any candidate name -- a nameless parameter
// matches another nameless parameter via `MatchString(null, null)`). The backing `NameToken` is a
// generated non-`partial` `[Slot]` (not seen by the source-property scan at generation time), so it
// never appears in `MembersToMatch` (no double-match). A type-only mismatch (not a
// `ParameterDeclaration`) rejects early.
//
// The generated ctors (the generator's `WriteConstructors`): `CtorParams` in declaration order is
// `[Attributes (collection, optional), ParameterModifier (enum, required), Type (single, optional),
// Name (string, required), DefaultExpression (single, optional)]` -- the `Name` string-name
// `[Slot]` is a "required" ctor param regardless of optionality (the generator's line-168 rule),
// and `ParameterModifier` is a settable enum so it is a required ctor param (the
// `VariableDeclarationStatement` D270 settable-enum-ctor-param precedent). The `bool` scalars
// (`HasThisModifier`/`IsParams`/`IsScopedRef`) are NOT ctor params (the generator adds only settable
// ENUM-typed scalars, and a `bool` is not an enum -- the `ComposedType.HasRefSpecifier` D242
// precedent). `RequiredConstructorPrefixLength` is 4 (through the last non-optional param `Name` at
// index 3 -- the generator walks the whole `ctorParams` list and takes the last non-optional index
// + 1, the `CatchClause` D254 / `PropertyDeclaration` D276 precedent); `ConstructorPrefixLengths`
// is {4, 5}; the `(len=4)` and `(len=5)` ctors both call `this.Attributes.AddRange(...)` for the
// `Attributes` collection (the `AddRange` convenience is the D222 deferral), so they are DEFERRED --
// the empty ctor is the only portable ctor. A `ParameterDeclaration` is built via the empty ctor +
// `Attributes().Add(...)` + `ParameterModifier(...)` + `Type(...)` + `Name(...)` +
// `DefaultExpression(...)` + the bool setters.
//
// NO C++ name-shadowing crux (no member is named `AttributeSection`/`AstType`/`Identifier`/
// `Expression`/`ReferenceKind` -- the `Attributes()`/`Type()`/`NameToken()`/`Name()`/
// `DefaultExpression()`/`ParameterModifier()` accessors do not collide with any class/enum in the
// `Syntax` namespace; `[Slot("Identifier")]`/`[Slot("Expression")]` name the slot KINDS but the
// PROPERTIES are `Name`/`DefaultExpression`, so the accessors do not collide with the
// `Identifier`/`Expression` classes -- the `MemberType.MemberName` D238 / `EnumMemberDeclaration`
// D275 / `VariableInitializer` D266 differently-named-property precedent; `Type()` does not collide
// because no class named `Type` lives in `Syntax` -- there is `AstType`, not `Type` -- the
// `Attribute` D240 lesson). So no elaborated-type-specifier is needed anywhere, and the
// `Identifier::CreateIfNotEmpty` factory call is unqualified. This is the cleanest
// collection-plus-trailing-singles-with-a-nullable-string-name-`[Slot]` port so far.
//
// NO new `Slots` constant (all four kinds are already ported: `Slots::AttributeSection` cycle-broken
// in `AttributeSection.hpp` D241, `Slots::Type` in `Slots.hpp` D240, `Slots::Identifier` in
// `Slots.hpp` D237, `Slots::Expression` in `Slots.hpp` D231). The `ReferenceKind` enum ports to
// `cpp/Decompiler/TypeSystem/ReferenceKind.hpp` (header-only, the `SymbolKind` D271 precedent) --
// the first ported `TypeSystem` enum consumed by a `CSharp.Syntax` scalar. Per PORT_PLAN.md section
// 5.2 / decision D1 the concrete node is hand-translated from the generated output. The generated
// `AcceptVisitor` calls `visitor.VisitParameterDeclaration(this)` (the class name does not end in
// "AstType", so the generator's visit-method-name default yields `VisitParameterDeclaration`).
// `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the four scalars copied directly (private to this class, so
// accessible from the derived `Clone` -- the `ComposedType` D242 scalar-copy precedent), the
// annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone
// pattern), the `Type`/`NameToken`/`DefaultExpression` deep-cloned through their setters when
// present (which re-parent; the covariant `Clone()` returns `AstType*`/`Identifier*`/`Expression*`
// the setters accept directly), and every `Attributes` element deep-cloned through `Add` (which
// re-parents and re-indexes; `AttributeSection::Clone()` returns `AttributeSection*`). No own
// location fields (does not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation`
// are not copied (the `DestructorDeclaration` D272 / `VariableInitializer` D266 no-location-copy
// precedent). The covariant return is `ParameterDeclaration*` (through `AstNode*`, the
// `AstNode::Clone` virtual).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_PARAMETERDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_PARAMETERDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/ComposedType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public partial class ParameterDeclaration : AstNode` (NOT `sealed` -- the
// `[DecompilerAstNode(hasPatternPlaceholder: true)]` emits a sealed `PatternPlaceholder` deriving
// from it; the pattern placeholder is deferred, but the class stays non-`final` to match the C#
// and to not block the placeholder landing -- the `ArrayInitializerExpression` D250 /
// `VariableInitializer` D266 precedent). Derives directly from the `AstNode` root (a parameter is
// a structural node owned by a declaration's `Parameters` collection, not a member declaration).
// The collection-plus-three-trailing-singles shape with a nullable string-name `[Slot]` in the
// middle: an `Attributes` collection (slot 0) + a nullable `Type` `AstType?` (slot 1) + a nullable
// `NameToken` `Identifier?` (slot 2, the backing token of the nullable `string? Name`) + a nullable
// `DefaultExpression` `Expression?` (slot 3), plus four scalars.
class ParameterDeclaration : public AstNode {
public:
    ~ParameterDeclaration() override = default;

    // The generated empty ctor (the C# `public ParameterDeclaration()`). The `Attributes`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = false` (the
    // collection is NOT the node's last slot -- `Type`/`NameToken`/`DefaultExpression` trail it --
    // so an element's flattened `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices`
    // after a mutation). `Type`/`NameToken`/`DefaultExpression` default to null (all three are
    // nullable, so a default-constructed node is invariant-valid -- the `ReturnStatement` D255
    // nullable-slot precedent). The four scalars default to `false`/`false`/`false`/
    // `ReferenceKind::None` (the C# defaults).
    ParameterDeclaration() : attributes_(this, &AttributesSlot, 0, false) {}

    // ---- The const keyword tokens (the output-visitor token literals) -------------------
    // The C# `public const string ThisModifier = "this"` / `ScopedRefKeyword = "scoped"` /
    // `RefModifier = "ref"` / `ReadonlyModifier = ComposedType.ReadonlyKeyword` /
    // `OutModifier = "out"` / `InModifier = "in"` / `ParamsModifier = "params"`. Part of the
    // node's public API (the output visitor reads them); port as `static constexpr const char*`
    // (the `CheckedExpression.CheckedKeyword` D234 / `ComposedType.RefKeyword` D242 precedent).
    // The generator excludes `const string` fields from `MembersToMatch` (it iterates only
    // instance `IPropertySymbol`s), so they never appear in the generated `DoMatch`. The
    // `ReadonlyModifier` is aliased to `ComposedType::ReadonlyKeyword` (the single source of truth
    // for the "readonly" literal -- the `UsingStatement.AwaitKeyword` D262 alias precedent), so
    // `ComposedType.hpp` is included.
    static constexpr const char* ThisModifier = "this";
    static constexpr const char* ScopedRefKeyword = "scoped";
    static constexpr const char* RefModifier = "ref";
    static constexpr const char* ReadonlyModifier = ComposedType::ReadonlyKeyword;
    static constexpr const char* OutModifier = "out";
    static constexpr const char* InModifier = "in";
    static constexpr const char* ParamsModifier = "params";

    // ---- The scalar properties (NOT `[Slot]`s, not child slots) -------------------------
    // The C# `public bool HasThisModifier { get; set; }` / `IsParams` / `IsScopedRef` -- whether
    // the parameter is an extension-method `this` parameter / a `params` array / a `scoped ref`.
    // Plain `bool` fields: not child slots (no `[Slot]`), not ctor params (the generator adds only
    // settable ENUM-typed scalars to `CtorParams`, and a `bool` is not an enum -- the
    // `ComposedType.HasRefSpecifier` D242 precedent), so they are set via the property setters.
    // They ARE in `MembersToMatch` (the generator adds every non-`[Slot]` settable instance
    // property), and a `bool` (not an enum, no `Any`) emits the fall-through plain-equality
    // `DoMatch` term. No name shadowing.
    bool HasThisModifier() const { return hasThisModifier_; }
    void HasThisModifier(bool value) { hasThisModifier_ = value; }
    bool IsParams() const { return isParams_; }
    void IsParams(bool value) { isParams_ = value; }
    bool IsScopedRef() const { return isScopedRef_; }
    void IsScopedRef(bool value) { isScopedRef_ = value; }

    // The C# `public ReferenceKind ParameterModifier { get; set; }` -- the `ref`/`out`/`in`/
    // `ref readonly` modifier kind (a `ReferenceKind` enum scalar). A settable enum, so it IS a
    // ctor param (the `VariableDeclarationStatement.Modifiers` D270 settable-enum-ctor-param
    // precedent). `ReferenceKind` declares NO `Any` member, so the generator's `hasAny` path does
    // NOT fire -- the `DoMatch` term is the PLAIN `this.ParameterModifier == o.ParameterModifier`
    // (the `DirectionExpression.FieldDirection` D235 no-`Any`-enum precedent applied to a
    // `TypeSystem` enum). NO name shadowing (the `ParameterModifier()` accessor does NOT share the
    // `ReferenceKind` enum's name -- unlike `DirectionExpression.FieldDirection`-of-type-
    // `FieldDirection` -- so no elaborated enum specifier is needed; the plain `ReferenceKind`
    // resolves in both the getter and setter signatures, and the qualified
    // `ReferenceKind::None` is unambiguous).
    ILSpy::Decompiler::TypeSystem::ReferenceKind ParameterModifier() const { return parameterModifier_; }
    void ParameterModifier(ILSpy::Decompiler::TypeSystem::ReferenceKind value) { parameterModifier_ = value; }

    // ---- The `Attributes` collection slot -----------------------------------------------
    // The generated `[Slot("AttributeSection")] public partial AstNodeCollection<AttributeSection>
    // Attributes` -- the attribute sections on the parameter (a `CSharpSlotInfoT<AttributeSection>`
    // slot at slot 0, non-incremental). The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly. A
    // `const` convenience overload returns `const&` for a `const ParameterDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `Type` slot (a NULLABLE single `AstType`) ----------------------------------
    // The generated `[Slot("Type")] public partial AstType? Type` -- a single NULLABLE `AstType?`
    // slot at slot 1 (the parameter's declared type). The slot FOLLOWS the `Attributes` collection,
    // so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic
    // flattened index after a collection). NO name shadowing (the `Type()` accessor does NOT
    // collide with any class -- no class named `Type` lives in the `Syntax` namespace, there is
    // `AstType`, not `Type` -- the `Attribute` D240 lesson), so the element type is the plain
    // `AstType`. `CheckInvariant` passes without a `Type` (the slot is nullable -- a nameless
    // parameter such as `__argList` may carry none, though a well-formed parameter always has one).
    AstType* Type() const { return type_; }
    void Type(AstType* value) {
        SetChildNode(type_, value);
    }

    // ---- The `NameToken` slot (the backing `Identifier` token of the name) --------------
    // The generated `[Slot("Identifier")] public partial Identifier? NameToken` (the generator
    // emits the backing token as a public non-partial `[Slot]` named `{property.Name}Token`) -- a
    // single NULLABLE `Identifier?` slot at slot 2. The slot FOLLOWS the `Attributes` collection,
    // so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NO name
    // shadowing (the `NameToken` accessor does NOT collide with the `Identifier` class -- no
    // member is named `Identifier` -- the `VariableInitializer.NameToken` D266 precedent), so the
    // element type is the plain `Identifier` and the `Identifier::CreateIfNotEmpty` factory is
    // unqualified.
    Identifier* NameToken() const { return nameToken_; }
    void NameToken(Identifier* value) {
        SetChildNode(nameToken_, value);
    }

    // ---- The `Name` string-name accessor (over the token) -------------------------------
    // The generated `public partial string? Name` -- a convenience string over the `NameToken`
    // slot. An OPTIONAL name (the C# `string?`): `get` returns null when the token is absent;
    // `set` creates the token via `Identifier.CreateIfNotEmpty`, so an empty/null name clears the
    // token (the C# `NameToken = Identifier.CreateIfNotEmpty(value)`). `Name()` returns
    // `std::optional<std::string>` (`nullopt` when the token is absent -- the faithful `string?`);
    // the `Identifier::CreateIfNotEmpty` factory call is unqualified (the `Name()` accessor does
    // NOT shadow the `Identifier` class -- no member is named `Identifier` -- the
    // `GotoStatement.Label` D257 / `MemberType.MemberName` D238 differently-named-property
    // precedent).
    std::optional<std::string> Name() const {
        return nameToken_ != nullptr
            ? std::optional<std::string>(nameToken_->Name()) : std::nullopt;
    }
    void Name(std::string_view value) {
        NameToken(Identifier::CreateIfNotEmpty(value));
    }

    // ---- The `DefaultExpression` slot (a NULLABLE single `Expression`) ------------------
    // The generated `[Slot("Expression")] public partial Expression? DefaultExpression` -- a
    // single NULLABLE `Expression?` slot at slot 3 (the optional `= expression` default). The slot
    // FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
    // `SetChildNode(ref field, value)`. NO name shadowing (the `DefaultExpression` accessor does
    // NOT collide with the `Expression` class -- no member is named `Expression` -- the
    // `MemberReferenceExpression.Target` D247 / `VariableInitializer.Initializer` D266
    // differently-named-property precedent), so the element type is the plain `Expression`.
    // `CheckInvariant` passes without a `DefaultExpression` (the slot is nullable).
    Expression* DefaultExpression() const { return defaultExpression_; }
    void DefaultExpression(Expression* value) {
        SetChildNode(defaultExpression_, value);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ----------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's only collection); `TypeSlot` (a `CSharpSlotInfoT<AstType>` pointing
    // at `Slots.Type`, nullable); `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at
    // `Slots.Identifier`, nullable -- the `NameToken` `Identifier?` is nullable); `DefaultExpressionSlot`
    // (a `CSharpSlotInfoT<Expression>` pointing at `Slots.Expression`, nullable). NO name
    // shadowing (no member is named `AttributeSection`/`AstType`/`Identifier`/`Expression`), so the
    // element types are the plain classes. NO new `Slots` constant (all four kinds are already
    // ported).
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> TypeSlot{"Type", false, &Slots::Type, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, true};
    static inline const CSharpSlotInfoT<Expression> DefaultExpressionSlot{"DefaultExpression", false, &Slots::Expression, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitParameterDeclaration` (`ParameterDeclaration` does not end in "AstType",
    // so the generator's visit-method-name default yields `VisitParameterDeclaration`).
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitParameterDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Four slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `Type` single slot at slot 1 (index `attrCount`), a `NameToken` single
    // slot at slot 2 (index `attrCount + 1`), and a `DefaultExpression` single slot at slot 3
    // (index `attrCount + 2`). `GetChildCount` is `attrCount + 3` (the collection's current length
    // plus the three singles); `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting
    // each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a collection
    // step, then three single steps). `GetCollectionByKind` returns the `Attributes` collection
    // for the `AttributeSection` kind. This is the `EnumMemberDeclaration` D275
    // collection -> single -> single -> single dispatch shape (the collection FIRST, then three
    // singles).
    int GetChildCount() const override { return attributes_.Count() + 3; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        if (i == 0)
            return type_;
        i--;
        if (i == 0)
            return nameToken_;
        i--;
        if (i == 0)
            return defaultExpression_;
        throw std::out_of_range("ParameterDeclaration::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n) {
                attributes_.SetAt(i, static_cast<AttributeSection*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(type_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(nameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(defaultExpression_, static_cast<Expression*>(value), index);
            return;
        }
        throw std::out_of_range("ParameterDeclaration::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return &AttributesSlot;
            i -= n;
        }
        if (i == 0)
            return &TypeSlot;
        i--;
        if (i == 0)
            return &NameTokenSlot;
        i--;
        if (i == 0)
            return &DefaultExpressionSlot;
        throw std::out_of_range("ParameterDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is ParameterDeclaration o && this.Attributes.DoMatch(o.Attributes, match) &&
    // this.HasThisModifier == o.HasThisModifier && this.IsParams == o.IsParams &&
    // this.IsScopedRef == o.IsScopedRef && this.ParameterModifier == o.ParameterModifier &&
    // MatchOptional(this.Type, o.Type, match) && MatchString(this.Name, o.Name) &&
    // MatchOptional(this.DefaultExpression, o.DefaultExpression, match)`. The terms are in
    // `MembersToMatch` order (source declaration order -- the generator adds every non-`[Slot]`
    // settable instance property plus the `[Slot]` children). The `Attributes` term is the
    // collection recursive match; the three bool/enum scalars are the fall-through plain-equality
    // (a `bool` and a no-`Any` enum); the `Type` and `DefaultExpression` terms are `MatchOptional`
    // (nullable recursive children); the `Name` term is a `String` `MatchString` (the `$any$`
    // wildcard in the pattern's `Name` matches any candidate name; a nameless parameter matches
    // another nameless parameter via `MatchString(null, null)`). The backing `NameToken` is a
    // generated non-`partial` `[Slot]` (not seen by the source-property scan), so it never appears
    // in `MembersToMatch` (no double-match). A type-only mismatch (not a `ParameterDeclaration`)
    // rejects early. `Name()` returns `std::optional<std::string>` (`nullopt` when the token is
    // absent); `Pattern::MatchString` takes `std::optional<std::string_view>`, so the view is built
    // per side (`nullopt` passes through as the C# null). The `Name()` optionals are pre-computed
    // in locals (the `GotoStatement` D257 precedent) so the `std::string` temporaries outlive the
    // `MatchString` call; the private scalar fields are compared directly (same-class access).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<ParameterDeclaration*>(other);
        if (o == nullptr)
            return false;
        auto thisName = Name();
        auto otherName = o->Name();
        return attributes_.DoMatch(o->attributes_, match)
            && hasThisModifier_ == o->hasThisModifier_
            && isParams_ == o->isParams_
            && isScopedRef_ == o->isScopedRef_
            && parameterModifier_ == o->parameterModifier_
            && MatchOptional(type_, o->type_, match)
            && PatternMatching::Pattern::MatchString(
                   thisName ? std::optional<std::string_view>(*thisName) : std::nullopt,
                   otherName ? std::optional<std::string_view>(*otherName) : std::nullopt)
            && MatchOptional(defaultExpression_, o->defaultExpression_, match);
    }

public:
    // The C# `public new ParameterDeclaration Clone() { return (ParameterDeclaration)base.Clone(); }`
    // is `MemberwiseClone` + `CloneChildrenInto` in C#; this port overrides it (no
    // `MemberwiseClone`): a fresh node, the four scalars copied directly (private to this class,
    // so accessible from the derived `Clone` -- the `ComposedType` D242 scalar-copy precedent), the
    // annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern), the `Type`/`NameToken`/`DefaultExpression` deep-cloned through their
    // setters when present (which re-parent; the covariant `Clone()` returns `AstType*`/
    // `Identifier*`/`Expression*` the setters accept directly), and every `Attributes` element
    // deep-cloned through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()`
    // returns `AttributeSection*`). No own location fields (does not derive `EndLocation`), so the
    // print-time `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
    // `VariableInitializer` D266 no-location-copy precedent). The covariant return is
    // `ParameterDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual).
    ParameterDeclaration* Clone() const override {
        auto* node = new ParameterDeclaration();
        node->hasThisModifier_ = hasThisModifier_;
        node->isParams_ = isParams_;
        node->isScopedRef_ = isScopedRef_;
        node->parameterModifier_ = parameterModifier_;
        node->CloneAnnotationsFrom(*this);
        if (type_ != nullptr)
            node->Type(type_->Clone());
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (defaultExpression_ != nullptr)
            node->DefaultExpression(defaultExpression_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_` is the always-present collection member (empty until the
    // first `Add`, non-incremental); `type_`/`nameToken_`/`defaultExpression_` are null until set
    // (all three nullable -- their absence is invariant-valid); the four scalars default to the
    // C# defaults (`false`/`false`/`false`/`ReferenceKind::None`). NO name shadowing (no member is
    // named `AttributeSection`/`AstType`/`Identifier`/`Expression`), so the field types are the
    // plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    AstType* type_ = nullptr;
    Identifier* nameToken_ = nullptr;
    Expression* defaultExpression_ = nullptr;
    bool hasThisModifier_ = false;
    bool isParams_ = false;
    bool isScopedRef_ = false;
    ILSpy::Decompiler::TypeSystem::ReferenceKind parameterModifier_ =
        ILSpy::Decompiler::TypeSystem::ReferenceKind::None;
};

// The `Parameter` kind -- a collection of `ParameterDeclaration` (the `Parameters` slot of an
// `IndexerDeclaration`/`ConstructorDeclaration`/`OperatorDeclaration`/`MethodDeclaration`, the
// `[int x]`/`(int x)` parameter list). Shared by every `[Slot("Parameter")]
// AstNodeCollection<ParameterDeclaration>` declaration. A `CSharpSlotInfoT<ParameterDeclaration>` (the
// element type is the concrete `ParameterDeclaration` node). Defined HERE (in
// ParameterDeclaration.hpp, after the `ParameterDeclaration` class) for the cycle-breaking reason:// `ParameterDeclaration.hpp` includes `Slots.hpp` (for `Slots::AttributeSection`/`Slots::Type`/
// `Slots::Identifier`/`Slots::Expression` used by its per-node `AttributesSlot`/`TypeSlot`/
// `NameTokenSlot`/`DefaultExpressionSlot`), and with `Slots.hpp`'s guard set those definitions
// would not be visible where `ParameterDeclaration`'s class body needs them. After the class both
// `CSharpSlotInfoT` (visible via the `Slots.hpp` include) and `ParameterDeclaration` are complete,
// so the kind defines cleanly. The `inline` variable still has external linkage and one address
// across translation units (the C++17 `inline` guarantee), preserving the pointer-identity
// comparison `node.Slot.Kind == &Slots::Parameter` the slot system relies on. This is the
// `Slots::Attribute`/`Slots::AttributeSection`/`Slots::Initializer`/`Slots::Variable`
// cycle-breaking precedent (D241/D242/D251/D267) applied to a `ParameterDeclaration`-typed
// collection kind. The shared constant is constructed non-collection/non-optional (`{"Parameter",
// false, nullptr, false}`); the per-node `ParametersSlot` on the owning node carries the
// `IsCollection` flag (the collection `[Slot]` makes the per-node slot a collection). The kind name
// `Parameter` collides with no class in the `Syntax` namespace (there is `ParameterDeclaration`,
// not `Parameter`), so no elaborated-type-specifier is needed.
namespace Slots {
inline const CSharpSlotInfoT<ParameterDeclaration> Parameter{"Parameter", false, nullptr, false};
} // namespace Slots

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_PARAMETERDECLARATION_HPP
