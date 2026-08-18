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

// Port of the `DocumentationReference` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/DocumentationReference.cs (the generated
// `DocumentationReference.g.cs` + the hand-written partial, which declares the `SymbolKind` and
// `OperatorType` enum scalars, the `HasParameterList` bool scalar, the three single slot
// properties, the hand-written `MemberName` string accessor over the `NameToken` slot, the two
// collection slot properties, and a HAND-WRITTEN `DoMatch` -- no ctors, no helpers). The next
// in-order Phase-5 piece per the D294 plan (the remaining GeneralScope node that models a `cref`
// reference inside XML documentation comments -- not C# source syntax, so it has no grammar
// production; the documentation_reference ::= type_name | type_name '.' member_name | member_name
// shape).
//
// A sealed `AstNode` deriving DIRECTLY from the `AstNode` root (NOT `EntityDeclaration`/`Expression`/
// `Statement`/`AstType`/`Trivia` -- a documentation `cref` is a structural reference node, not a
// member declaration or a trivia node; it carries no `Modifiers`/`MatchAttributesAndModifiers`, the
// `VariableInitializer` D266 / `CatchClause` D269 / `Constraint` D283 / `ParameterDeclaration` D278
// direct-`AstNode` precedent). The `[DecompilerAstNode]` default `hasPatternPlaceholder` is
// `false`, so `final` (the C# `sealed`).
//
// The non-`[Slot]` scalars in source declaration order:
//   * `public SymbolKind SymbolKind { get; set; }` -- a settable enum scalar (the entity kind:
//     `SymbolKind.Operator` for operators, `SymbolKind.Indexer` for indexers,
//     `SymbolKind.TypeDefinition` for primitive-type references, `SymbolKind.None` otherwise). The
//     `SymbolKind` enum lives in the `ILSpy::Decompiler::TypeSystem` namespace (the D271 port), so
//     the qualified `ILSpy::Decompiler::TypeSystem::SymbolKind` names it everywhere; qualified
//     lookup bypasses class scope, so the `SymbolKind()` accessor does NOT shadow it and NO
//     elaborated enum specifier is needed (unlike an enum in the SAME `Syntax` namespace -- the
//     `OperatorType` scalar below). A settable enum-typed scalar the generator adds to the ctor
//     params (the `VariableDeclarationStatement.Modifiers` D270 settable-enum-ctor-param
//     precedent); the default is `SymbolKind.None` (the enum's zero value, the C# default).
//   * `public OperatorType OperatorType { get; set; }` -- a settable enum scalar (the operator
//     kind, used only when `SymbolKind == Operator`). The `OperatorType` enum is co-located with
//     `OperatorDeclaration` (the D280 port, in the `Syntax` namespace), reached via the
//     `OperatorDeclaration.hpp` include. The `OperatorType()` accessor SHADOWS the `OperatorType`
//     enum in this class scope (the `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.
//     OperatorType` D280 property-named-the-same-as-its-enum precedent), so the setter parameter and
//     the backing-field type use the ELABORATED enum specifier `enum OperatorType`, and the
//     backing-field initializer and the `DoMatch` value comparisons use the fully-qualified
//     `::ILSpy::Decompiler::CSharp::Syntax::OperatorType::...` (the elaborated specifier cannot
//     apply in a qualified-name position). The getter return type and the ctor parameter type
//     precede the getter's own declaration, so they use the plain `OperatorType` (the enum is
//     unshadowed there). Declares NO `Any` member, so there is no `Any`-wildcard (the
//     `OperatorDeclaration.OperatorType` D280 no-`Any`-enum precedent). A settable enum-typed
//     scalar the generator adds to the ctor params; the default is `OperatorType::LogicalNot` (the
//     enum's zero value, the C# default).
//   * `public bool HasParameterList { get; set; }` -- a non-`[Slot]` settable bool (whether a
//     parameter list was provided). A bool is NOT an enum, so it is NOT a ctor param (the generator
//     adds only settable ENUM-typed scalars to `CtorParams`), set via the property setter. The
//     `DoMatch` term is the fall-through plain equality (the `ComposedType.IsDoubleColon` D238 /
//     `Attribute.HasArgumentList` D240 plain-bool precedent). The default is `false`.
//
// The `[Slot]` children in source declaration order:
//   * `[Slot("DeclaringType")] public partial AstType? DeclaringType` -- a single NULLABLE `AstType?`
//     slot at flattened index 0 (the declaring type of a `type_name '.' member_name` `cref`, e.g.
//     the `Foo` in `<see cref="Foo.Bar"/>`; null for a bare `member_name` or `type_name` `cref`).
//     No collection precedes it, so the const-index `SetChildNode(ref field, value, 0)` setter
//     re-parents and re-indexes in place. A `MatchOptional`-style term is NOT emitted by the
//     generator (the `DoMatch` is hand-written), so this slot's match is governed by the
//     hand-written `DoMatch` (the `DeclaringType` is NOT directly matched -- the hand-written
//     `DoMatch` does not reference it; it is carried for the resolver/output stage). Reuses the NEW
//     `Slots::DeclaringType` kind (a `CSharpSlotInfoT<AstType>`, added this iteration to `Slots.hpp`).
//   * `public string MemberName { get; set; }` -- a HAND-WRITTEN string accessor (NOT a `[Slot]` --
//     no `SlotAttribute`): `get => NameToken.Name; set => NameToken = Identifier.Create(value);`.
//     It is NOT in the generated `MembersToMatch` (the `DoMatch` is hand-written, so
//     `MembersToMatch` is null and the property scan does not run), and the hand-written `DoMatch`
//     matches it only in the `SymbolKind == None` branch. The `MemberName()` accessor does NOT
//     shadow any class (no class named `MemberName`), so the `Identifier::Create` factory call is
//     unqualified (the `MemberType.MemberName` D238 / `LabelStatement.Label` D259
//     differently-named-property precedent). `MemberName()` returns `std::string` (a copy of the
//     token's name) and derefs the token directly (faithful to the C# `NullReferenceException` on a
//     half-constructed node, the `IdentifierExpression.Identifier` D246 precedent).
//   * `[Slot("Identifier")] public partial Identifier NameToken` -- a single REQUIRED (non-nullable)
//     `Identifier` slot at flattened index 1 (the member name of a `member_name` or
//     `type_name '.' member_name` `cref`; for a bare `type_name` `cref` the name is the type name).
//     No collection precedes it, so the const-index `SetChildNode(ref field, value, 1)` setter. The
//     `NameToken()` accessor does NOT shadow the `Identifier` class (no member is named
//     `Identifier`), so the element type is the plain `Identifier` (the `LabelStatement.LabelToken`
//     D259 / `VariableInitializer.NameToken` D266 differently-named-property precedent). Reuses the
//     `Slots::Identifier` kind (added by `SimpleType` D237).
//   * `[Slot("ConversionOperatorReturnType")] public partial AstType ConversionOperatorReturnType` --
//     a single REQUIRED (non-nullable) `AstType` slot at flattened index 2 (the return type of a
//     conversion operator, used only when `SymbolKind == Operator` and `OperatorType` is `Implicit`
//     or `Explicit`). No collection precedes it, so the const-index
//     `SetChildNode(ref field, value, 2)` setter. Reuses the NEW `Slots::ConversionOperatorReturnType`
//     kind (a `CSharpSlotInfoT<AstType>`, added this iteration to `Slots.hpp`).
//   * `[Slot("TypeArgument")] public partial AstNodeCollection<AstType> TypeArguments` -- a
//     COLLECTION of `AstType` at slot 3 (the type arguments of a generic `cref`, e.g.
//     `<see cref="Foo{T}"/>`). It is the node's FIRST of TWO collections, so `supportsIncremental`
//     is FALSE (`collectionCount == 1 && slotIndex == slots.Count - 1` is false -- `collectionCount`
//     is 2): every `Add`/`Insert`/`Remove`/`single-slot-set` INVALIDATES the parent's indices for a
//     lazy `EnsureChildIndices` rebuild (the `ComposedType` D242 two-collection precedent). Reuses
//     the `Slots::TypeArgument` kind (added by `SimpleType` D237).
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> Parameters` -- a
//     COLLECTION of `ParameterDeclaration` at slot 4 (the parameter list of a `cref` with a
//     parameter list, when `HasParameterList` is true). It is the node's SECOND of TWO collections,
//     so `supportsIncremental` is FALSE; `baseIndex` is 4 (the slot index -- the generator passes
//     the slot index, not the dynamic flattened index; unused on the non-incremental fast path).
//     Reuses the cycle-broken `Slots::Parameter` kind (added by `IndexerDeclaration` D279).
//
// The `DoMatch` is HAND-WRITTEN in the source (the generator's `WriteDoMatch` skips it --
// `!targetSymbol.MemberNames.Contains("DoMatch")` is false, so `MembersToMatch` stays null and
// `WriteDoMatch` returns early). The hand-written `DoMatch` is a SUBTYPE-ACCEPTING type match
// (`other as DocumentationReference` -- the `dynamic_cast` accepts subtypes; `DocumentationReference`
// is `final`, so no subtypes exist, but the cast is faithful to the C# `as`):
//   1. `if (!(o != null && this.SymbolKind == o.SymbolKind && this.HasParameterList == o.HasParameterList))
//      return false;` -- a type match plus plain-enum equality on `SymbolKind` plus plain-bool
//      equality on `HasParameterList` (the first gate; a `SymbolKind` or `HasParameterList`
//      mismatch rejects before any recursive term runs).
//   2. `if (this.SymbolKind == SymbolKind.Operator)` -- the operator branch: a plain-enum
//      `OperatorType != o.OperatorType` reject, then (for `Implicit`/`Explicit` conversions) a
//      DIRECT `this.ConversionOperatorReturnType.DoMatch(o.ConversionOperatorReturnType, match)` --
//      a non-nullable recursive child, ported through `MatchRequired` (the D231
//      `[class.access.derived]` workaround -- a derived node may not call the protected `DoMatch`
//      through a base `AstType*`; a static member of `AstNode` may). The `ConversionOperatorReturnType`
//      is NOT matched for non-conversion operators (the `OperatorType` guard).
//   3. `else if (this.SymbolKind == SymbolKind.None)` -- the named-member branch: a
//      `MatchString(this.MemberName, o.MemberName)` over the hand-written string accessor (the
//      `$any$` wildcard `Pattern::AnyString` in the pattern's `MemberName` matches any candidate
//      name), then a DIRECT `this.TypeArguments.DoMatch(o.TypeArguments, match)` collection match
//      (the generator's collection-recursive term; `AstNodeCollection::DoMatch` is public, so no
//      `[class.access.derived]` workaround is needed for it).
//   4. `return this.Parameters.DoMatch(o.Parameters, match);` -- the `Parameters` collection match
//      runs UNCONDITIONALLY for every `DocumentationReference` (the parameter list is matched
//      regardless of `SymbolKind`).
// The `OperatorType::Implicit`/`OperatorType::Explicit` value comparisons in the operator branch use
// the fully-qualified `::ILSpy::Decompiler::CSharp::Syntax::OperatorType::...` (the `OperatorType()`
// accessor shadows the enum in the `DoMatch` body's class scope). The `SymbolKind::Operator`/
// `SymbolKind::None` comparisons use the qualified `ILSpy::Decompiler::TypeSystem::SymbolKind::...`
// (qualified lookup bypasses the class-scope `SymbolKind()` shadowing). The scalar comparisons use
// the backing fields directly. `MemberName()` is INLINED in the `MatchString` arguments (the
// `IdentifierExpression.Identifier` D246 / `MemberType.MemberName` D238 short-circuit precedent) so
// the candidate's `MemberName()` is dereffed only when the `SymbolKind == None` branch is reached;
// the `std::string` temporaries live until the end of the `MatchString` full expression, so the
// `std::string_view` views stay valid.
//
// The generated ctors (the generator's `WriteConstructors`): `CtorParams` in declaration order is
// `[SymbolKind (enum, required), OperatorType (enum, required), DeclaringType (single, optional),
// NameToken (single, required), ConversionOperatorReturnType (single, required), TypeArguments
// (collection, optional), Parameters (collection, optional)]` -- the `HasParameterList` bool is
// NOT a ctor param (only settable ENUM-typed scalars are). `RequiredConstructorPrefixLength` is 5
// (through the last non-optional param `ConversionOperatorReturnType` at index 4);
// `ConstructorPrefixLengths` is `{5, 6, 7}` (the required prefix, the prefix ending at the
// `TypeArguments` collection, and the all-params prefix ending at the `Parameters` collection). The
// `(len=5)` ctor sets its five params via property setters (no `AddRange`), so it PORTS; the
// `(len=6)` and `(len=7)` ctors call `AddRange` for the collections (the D222 deferral), so they are
// DEFERRED. The empty + the `(len=5)` required-prefix ctors cover the construction API; a node is
// built via the empty or `(len=5)` ctor + `HasParameterList(...)` + `TypeArguments().Add(...)` +
// `Parameters().Add(...)` until `AddRange` lands. The `(len=5)` ctor is NOT `explicit` (a multi-arg
// ctor is not a converting ctor -- the `IfElseStatement` D258 / `VariableDeclarationStatement` D270
// precedent).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitDocumentationReference(this)` (the class name does not end in "AstType", so the
// generator's visit-method-name default yields `VisitDocumentationReference`). The generated slot
// statics are `DeclaringTypeSlot` (`CSharpSlotInfoT<AstType>` pointing at `Slots.DeclaringType`,
// nullable), `NameTokenSlot` (`CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`,
// required), `ConversionOperatorReturnTypeSlot` (`CSharpSlotInfoT<AstType>` pointing at
// `Slots.ConversionOperatorReturnType`, required), `TypeArgumentsSlot` (`CSharpSlotInfoT<AstType>`
// pointing at `Slots.TypeArgument`, collection), and `ParametersSlot`
// (`CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection). `Clone` is
// inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no
// `MemberwiseClone`): a fresh node, the three scalars copied (the two enum scalars directly to the
// backing fields, the `HasParameterList` bool via its public setter), the annotation channel copied
// (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the three single
// slots deep-cloned through their setters when present (the nullable `DeclaringType` is skipped when
// null; the required `NameToken`/`ConversionOperatorReturnType` are also null-guarded defensively --
// a half-constructed node's `Clone` does not crash, the `ObjectCreateExpression` D251 precedent),
// and every `TypeArguments`/`Parameters` element deep-cloned through `Add` (which re-parents and
// re-indexes; the covariant `Clone` returns `AstType*`/`ParameterDeclaration*` which `Add` accepts
// directly). No own location fields (`DocumentationReference` does not derive `EndLocation`), so the
// print-time `StartLocation`/`EndLocation` are not copied (the `Constraint` D283 no-location-copy
// precedent). The covariant return is `DocumentationReference*` (through `AstNode*`, the
// `AstNode::Clone` virtual).
//
// C++ name-shadowing cruxes: the `OperatorType()` accessor shadows the `OperatorType` enum (the D235
// / D280 property-named-the-same-as-its-enum precedent), resolved via the elaborated enum specifier
// / fully-qualified initializer / direct Clone field assignment. The `SymbolKind()` accessor does
// NOT shadow the `SymbolKind` enum (it lives in the `TypeSystem` namespace, reached by the qualified
// name -- the `EntityDeclaration.SymbolKind` D272 qualified-lookup-bypasses-shadowing precedent).
// NO other shadowing (no member is named `AstType`/`Identifier`/`ParameterDeclaration`/`DeclaringType`/
// `ConversionOperatorReturnType`/`TypeArguments`/`Parameters`/`MemberName`/`HasParameterList` -- the
// `DeclaringType()`/`NameToken()`/`ConversionOperatorReturnType()`/`TypeArguments()`/`Parameters()`/
// `MemberName()`/`HasParameterList()` accessors do not collide with any class in the `Syntax`
// namespace), so no elaborated-type-specifier is needed for the slot types.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_DOCUMENTATIONREFERENCE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_DOCUMENTATIONREFERENCE_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/OperatorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class DocumentationReference : AstNode`. `final` (the C#
// `sealed`): no further derivation. A direct-`AstNode` node (not `EntityDeclaration`/`Expression`/
// `Statement`/`AstType`/`Trivia`) modeling an XML-documentation `cref` reference.
class DocumentationReference final : public AstNode {
public:
    ~DocumentationReference() override = default;

    // The generated empty ctor (the C# `public DocumentationReference()`). The two collections are
    // members (the D222 always-present-stack-member design), initialized here with their slot
    // indices (`TypeArguments` at slot 3, `Parameters` at slot 4) and `supportsIncremental = false`
    // (the node has TWO collections, so neither is incremental -- an element's flattened
    // `ChildIndex` is dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation). The three
    // singles default to null; `NameToken` and `ConversionOperatorReturnType` are REQUIRED slots (a
    // default-constructed node violates their required-slot invariants, the `UnaryOperatorExpression`
    // D231 precedent; `CheckInvariant` rejects an empty node), `DeclaringType` is nullable so its
    // absence is invariant-valid. `SymbolKind` defaults to `None`, `OperatorType` to `LogicalNot`,
    // `HasParameterList` to `false` (the C# defaults).
    DocumentationReference() : typeArguments_(this, &TypeArgumentsSlot, 3, false),
                              parameters_(this, &ParametersSlot, 4, false) {}

    // The generated required-prefix ctor (the C# `public DocumentationReference(SymbolKind
    // symbolKind, OperatorType operatorType, AstType? declaringType, Identifier nameToken, AstType
    // conversionOperatorReturnType)`) -- the five required ctor params before the optional
    // `TypeArguments`/`Parameters` collections. Sets them in declaration order via the property
    // setters. Delegates to the empty ctor so the collection members are initialized. NOT
    // `explicit` (a multi-arg ctor is not a converting ctor -- the `IfElseStatement` D258 /
    // `VariableDeclarationStatement` D270 precedent). The two enum scalars are assigned DIRECTLY
    // to the backing fields (not via the `SymbolKind(symbolKind)`/`OperatorType(operatorType)`
    // setter calls) because those calls are ambiguous with functional casts of the enums (the
    // `DirectionExpression.FieldDirection` D235 / `VariableDeclarationStatement.Modifiers` D270
    // ambiguity precedent); the three children are set through their setters so the slot machinery
    // re-parents and re-indexes them. The `SymbolKind` ctor-parameter type is the qualified
    // `ILSpy::Decompiler::TypeSystem::SymbolKind` (the enum lives in the `TypeSystem` namespace); the
    // `OperatorType` ctor-parameter type is the plain `OperatorType` (the ctor precedes the
    // `OperatorType()` getter, so the enum is unshadowed here).
    DocumentationReference(ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind,
                           OperatorType operatorType,
                           AstType* declaringType,
                           Identifier* nameToken,
                           AstType* conversionOperatorReturnType)
        : DocumentationReference() {
        symbolKind_ = symbolKind;
        operatorType_ = operatorType;
        DeclaringType(declaringType);
        NameToken(nameToken);
        ConversionOperatorReturnType(conversionOperatorReturnType);
    }

    // ---- The `SymbolKind` scalar (a settable `TypeSystem` enum, NOT a `[Slot]`) -------------
    // The C# `public SymbolKind SymbolKind { get; set; }`. The `SymbolKind` enum lives in the
    // `ILSpy::Decompiler::TypeSystem` namespace (the D271 port); the qualified name reaches it
    // everywhere and bypasses the class-scope `SymbolKind()` accessor (no elaborated specifier
    // needed, unlike a same-namespace enum). The default is `SymbolKind::None` (the enum's zero
    // value, the C# default).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const { return symbolKind_; }
    void SymbolKind(ILSpy::Decompiler::TypeSystem::SymbolKind value) { symbolKind_ = value; }

    // ---- The `OperatorType` scalar (a settable `Syntax` enum, NOT a `[Slot]`) ----------------
    // The C# `public OperatorType OperatorType { get; set; }`. The `OperatorType` enum is
    // co-located with `OperatorDeclaration` (the D280 port, in the `Syntax` namespace), reached
    // via the `OperatorDeclaration.hpp` include. The return type precedes the getter's own
    // declaration, so the plain `OperatorType` (the enum) is unshadowed in the getter signature.
    OperatorType OperatorType() const { return operatorType_; }
    // The setter parameter type uses the elaborated enum specifier `enum OperatorType`: the
    // `OperatorType()` getter declared just above shadows the `OperatorType` enum in this class
    // scope (the D235 `FieldDirection` / D280 `OperatorType` property-named-the-same-as-its-enum
    // precedent -- the enum equivalent of the `class Expression` elaborated specifier), so the
    // plain name would resolve to the member function (not a type).
    void OperatorType(enum OperatorType value) { operatorType_ = value; }

    // ---- The `HasParameterList` scalar (a settable bool, NOT a `[Slot]`) ---------------------
    // The C# `public bool HasParameterList { get; set; }`. A bool is NOT an enum, so it is NOT a
    // ctor param; the `DoMatch` term is the fall-through plain equality. The default is `false`.
    bool HasParameterList() const { return hasParameterList_; }
    void HasParameterList(bool value) { hasParameterList_ = value; }

    // ---- The `DeclaringType` slot (a NULLABLE single `AstType`) ------------------------------
    // The generated `[Slot("DeclaringType")] public partial AstType? DeclaringType` -- a single
    // NULLABLE `AstType?` slot at flattened index 0 (the declaring type of a `type_name '.'
    // member_name` `cref`). No collection precedes it, so the const-index
    // `SetChildNode(ref field, value, 0)` setter re-parents and re-indexes in place. NO name
    // shadowing (the `DeclaringType()` accessor does NOT collide with any class -- no class named
    // `DeclaringType`), so the element type is the plain `AstType`. Reuses the NEW
    // `Slots::DeclaringType` kind. `CheckInvariant` passes without a `DeclaringType` (the slot is
    // nullable).
    AstType* DeclaringType() const { return declaringType_; }
    void DeclaringType(AstType* value) {
        SetChildNode(declaringType_, value, 0);
    }

    // ---- The `MemberName` hand-written string accessor (over the `NameToken` slot) -----------
    // The C# `public string MemberName { get { return NameToken.Name; } set { NameToken =
    // Identifier.Create(value); } }` -- a HAND-WRITTEN string accessor (NOT a `[Slot]`). The
    // `MemberName()` accessor does NOT shadow any class (no class named `MemberName`), so the
    // `Identifier::Create` factory call is unqualified (the `MemberType.MemberName` D238 /
    // `LabelStatement.Label` D259 differently-named-property precedent). `MemberName()` returns
    // `std::string` (a copy of the token's name) and derefs the token directly (faithful to the
    // C# `NullReferenceException` on a half-constructed node, the `IdentifierExpression.Identifier`
    // D246 precedent). The setter creates a fresh `Identifier` token via `Identifier::Create`
    // (NOT `CreateIfNotEmpty` -- a non-nullable name always carries a token, the
    // `LabelStatement.Label` D259 non-nullable-string precedent).
    std::string MemberName() const { return nameToken_->Name(); }
    void MemberName(std::string_view value) {
        NameToken(Identifier::Create(std::string(value)));
    }

    // ---- The `NameToken` slot (a REQUIRED single `Identifier`) --------------------------------
    // The generated `[Slot("Identifier")] public partial Identifier NameToken` -- a single
    // REQUIRED (non-nullable) `Identifier` slot at flattened index 1 (the member name of a `cref`).
    // No collection precedes it, so the const-index `SetChildNode(ref field, value, 1)` setter.
    // The `NameToken()` accessor does NOT shadow the `Identifier` class (no member is named
    // `Identifier`), so the element type is the plain `Identifier`. Reuses the `Slots::Identifier`
    // kind (added by `SimpleType` D237). `CheckInvariant` asserts the token is filled (a required
    // slot, the `LabelStatement.LabelToken` D259 precedent).
    Identifier* NameToken() const { return nameToken_; }
    void NameToken(Identifier* value) {
        SetChildNode(nameToken_, value, 1);
    }

    // ---- The `ConversionOperatorReturnType` slot (a REQUIRED single `AstType`) --------------
    // The generated `[Slot("ConversionOperatorReturnType")] public partial AstType
    // ConversionOperatorReturnType` -- a single REQUIRED (non-nullable) `AstType` slot at
    // flattened index 2 (the return type of a conversion operator, used only when
    // `SymbolKind == Operator` and `OperatorType` is `Implicit` or `Explicit`). No collection
    // precedes it, so the const-index `SetChildNode(ref field, value, 2)` setter. Reuses the NEW
    // `Slots::ConversionOperatorReturnType` kind. `CheckInvariant` asserts the slot is filled (a
    // required slot).
    AstType* ConversionOperatorReturnType() const { return conversionOperatorReturnType_; }
    void ConversionOperatorReturnType(AstType* value) {
        SetChildNode(conversionOperatorReturnType_, value, 2);
    }

    // ---- The `TypeArguments` collection slot ------------------------------------------------
    // The generated `[Slot("TypeArgument")] public partial AstNodeCollection<AstType>
    // TypeArguments` -- the collection of type-argument `AstType`s at slot 3, the node's FIRST of
    // TWO collections (non-incremental). The C# lazily allocates the wrapper; the D222 port makes
    // the collection an always-present stack member, so the accessor returns the member directly.
    // Reuses the `Slots::TypeArgument` kind (added by `SimpleType` D237). NO name shadowing (the
    // `TypeArguments()` accessor does NOT collide with any class), so the `AstType` element type
    // needs no elaborated specifier.
    AstNodeCollectionT<AstType>& TypeArguments() { return typeArguments_; }
    const AstNodeCollectionT<AstType>& TypeArguments() const { return typeArguments_; }

    // ---- The `Parameters` collection slot --------------------------------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the collection of `ParameterDeclaration`s at slot 4, the node's SECOND of
    // TWO collections (non-incremental; `baseIndex` is 4, the slot index). Reuses the cycle-broken
    // `Slots::Parameter` kind (added by `IndexerDeclaration` D279). NO name shadowing (the
    // `Parameters()` accessor does NOT collide with any class -- no class named `Parameters`).
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) -------------------
    // `DeclaringTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.DeclaringType`,
    // nullable -- the `DeclaringType` is a nullable single slot, so `IsOptional=true`); `NameTokenSlot`
    // (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required); `ConversionOperatorReturnTypeSlot`
    // (a `CSharpSlotInfoT<AstType>` pointing at `Slots.ConversionOperatorReturnType`, required);
    // `TypeArgumentsSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.TypeArgument`,
    // collection); `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at
    // `Slots.Parameter`, collection). NO name shadowing (no member is named `AstType`/`Identifier`/
    // `ParameterDeclaration`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<AstType> DeclaringTypeSlot{"DeclaringType", false, &Slots::DeclaringType, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<AstType> ConversionOperatorReturnTypeSlot{"ConversionOperatorReturnType", false, &Slots::ConversionOperatorReturnType, false};
    static inline const CSharpSlotInfoT<AstType> TypeArgumentsSlot{"TypeArguments", true, &Slots::TypeArgument, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitDocumentationReference`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitDocumentationReference(this);
    }

    // ---- Slot storage (the generated overrides) --------------------------------------------
    // Three single slots (each one flattened index) followed by two collections (`TypeArguments`
    // then `Parameters`, each a contiguous run of its current length). `GetChildCount` is
    // `3 + typeArgumentsCount + parametersCount`; `GetChild`/`SetChild`/`GetChildSlotInfo` walk
    // the slots subtracting each one's width from a running index (the generator's
    // `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- three single
    // cases then two collection steps). `GetCollectionByKind` returns the `TypeArguments`
    // collection for the `TypeArgument` kind and the `Parameters` collection for the `Parameter`
    // kind (the node's two collections). This is the `OperatorDeclaration` D280 / `MethodDeclaration`
    // D284 two-collection dispatch shape extended with a third preceding single.

    int GetChildCount() const override {
        return 3 + typeArguments_.Count() + parameters_.Count();
    }

    AstNode* GetChild(int index) const override {
        int i = index;
        if (i == 0) return declaringType_;
        if (i == 1) return nameToken_;
        if (i == 2) return conversionOperatorReturnType_;
        i -= 3;
        int n1 = typeArguments_.Count();
        if (i < n1) return typeArguments_.At(i);
        i -= n1;
        int n2 = parameters_.Count();
        if (i < n2) return parameters_.At(i);
        throw std::out_of_range("DocumentationReference::GetChild");
    }

    void SetChild(int index, AstNode* value) override {
        int i = index;
        if (i == 0) {
            SetChildNode(declaringType_, static_cast<AstType*>(value), index);
            return;
        }
        if (i == 1) {
            SetChildNode(nameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        if (i == 2) {
            SetChildNode(conversionOperatorReturnType_, static_cast<AstType*>(value), index);
            return;
        }
        i -= 3;
        int n1 = typeArguments_.Count();
        if (i < n1) {
            typeArguments_.SetAt(i, static_cast<AstType*>(value));
            return;
        }
        i -= n1;
        int n2 = parameters_.Count();
        if (i < n2) {
            parameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
            return;
        }
        throw std::out_of_range("DocumentationReference::SetChild");
    }

    const CSharpSlotInfo* GetChildSlotInfo(int index) const override {
        int i = index;
        if (i == 0) return &DeclaringTypeSlot;
        if (i == 1) return &NameTokenSlot;
        if (i == 2) return &ConversionOperatorReturnTypeSlot;
        i -= 3;
        int n1 = typeArguments_.Count();
        if (i < n1) return &TypeArgumentsSlot;
        i -= n1;
        int n2 = parameters_.Count();
        if (i < n2) return &ParametersSlot;
        throw std::out_of_range("DocumentationReference::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::TypeArgument) return &typeArguments_;
        if (kind == &Slots::Parameter) return &parameters_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the HAND-WRITTEN pattern match) -------------------------------------------
    // The source declares its own `DoMatch`, so the generator's `WriteDoMatch` skips it
    // (`MembersToMatch` stays null). The hand-written `DoMatch` is a subtype-accepting type match
    // (`dynamic_cast<DocumentationReference*>`, faithful to the C# `as`) plus a plain-enum
    // `SymbolKind` equality and a plain-bool `HasParameterList` equality as the first gate, then a
    // `SymbolKind`-driven conditional: the `Operator` branch matches `OperatorType` and (for
    // `Implicit`/`Explicit` conversions) the `ConversionOperatorReturnType` (a non-nullable
    // recursive child dispatched through `MatchRequired` -- the D231 `[class.access.derived]`
    // workaround); the `None` branch matches `MemberName` (a `MatchString` over the hand-written
    // string accessor) and the `TypeArguments` collection (a public `AstNodeCollection::DoMatch`).
    // The `Parameters` collection is matched UNCONDITIONALLY at the end. The
    // `OperatorType::Implicit`/`OperatorType::Explicit` value comparisons use the fully-qualified
    // `::ILSpy::Decompiler::CSharp::Syntax::OperatorType::...` (the `OperatorType()` accessor
    // shadows the enum in this body); the `SymbolKind::Operator`/`SymbolKind::None` comparisons use
    // the qualified `ILSpy::Decompiler::TypeSystem::SymbolKind::...` (qualified lookup bypasses the
    // class-scope `SymbolKind()` shadowing). `MemberName()` is inlined in the `MatchString`
    // arguments so the candidate's token is dereffed only in the `None` branch; the `std::string`
    // temporaries live until the end of the `MatchString` full expression.
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<DocumentationReference*>(other);
        if (o == nullptr)
            return false;
        if (!(symbolKind_ == o->symbolKind_ && hasParameterList_ == o->hasParameterList_))
            return false;
        if (symbolKind_ == ILSpy::Decompiler::TypeSystem::SymbolKind::Operator) {
            if (operatorType_ != o->operatorType_)
                return false;
            if (operatorType_ == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::Implicit
                || operatorType_ == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::Explicit) {
                if (!MatchRequired(conversionOperatorReturnType_, o->conversionOperatorReturnType_, match))
                    return false;
            }
        } else if (symbolKind_ == ILSpy::Decompiler::TypeSystem::SymbolKind::None) {
            if (!PatternMatching::Pattern::MatchString(
                    std::optional<std::string_view>(std::string_view(MemberName())),
                    std::optional<std::string_view>(std::string_view(o->MemberName()))))
                return false;
            if (!typeArguments_.DoMatch(o->typeArguments_, match))
                return false;
        }
        return parameters_.DoMatch(o->parameters_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the three scalars copied (the two enum scalars
    // directly to the backing fields, the `HasParameterList` bool via its public setter -- the
    // `VariableDeclarationStatement.Modifiers` D270 scalar-copy precedent), the annotation channel
    // copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the
    // three single slots deep-cloned through their setters when present (the nullable
    // `DeclaringType` is skipped when null; the required `NameToken`/`ConversionOperatorReturnType`
    // are also null-guarded defensively -- a half-constructed node's `Clone` does not crash, the
    // `ObjectCreateExpression` D251 precedent), and every `TypeArguments`/`Parameters` element
    // deep-cloned through `Add` (which re-parents and re-indexes; `AstType::Clone()` returns
    // `AstType*` and `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`, which `Add`
    // accepts directly). No own location fields (does not derive `EndLocation`), so the
    // print-time `StartLocation`/`EndLocation` are not copied (the `Constraint` D283 no-location-copy
    // precedent). The covariant return is `DocumentationReference*` (through `AstNode*`, the
    // `AstNode::Clone` virtual).
    DocumentationReference* Clone() const override {
        auto* node = new DocumentationReference();
        node->symbolKind_ = symbolKind_;
        node->operatorType_ = operatorType_;
        node->HasParameterList(hasParameterList_);
        node->CloneAnnotationsFrom(*this);
        if (declaringType_ != nullptr)
            node->DeclaringType(declaringType_->Clone());
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        if (conversionOperatorReturnType_ != nullptr)
            node->ConversionOperatorReturnType(conversionOperatorReturnType_->Clone());
        for (int i = 0; i < typeArguments_.Count(); i++)
            node->typeArguments_.Add(typeArguments_.At(i)->Clone());
        for (int i = 0; i < parameters_.Count(); i++)
            node->parameters_.Add(parameters_.At(i)->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `symbolKind_` (`TypeSystem::SymbolKind`, qualified -- no shadowing);
    // `operatorType_` (the elaborated `enum OperatorType` specifier, since the `OperatorType()`
    // accessor declared above shadows the enum; the fully-qualified initializer names the zero
    // value `LogicalNot`); `hasParameterList_` (bool, default `false`). The three single slots
    // default to null (`DeclaringType` is nullable; `NameToken`/`ConversionOperatorReturnType` are
    // required, so a default-constructed node violates their invariants). The two collections are
    // always-present members (empty until the first `Add`, non-incremental).
    ILSpy::Decompiler::TypeSystem::SymbolKind symbolKind_ = ILSpy::Decompiler::TypeSystem::SymbolKind::None;
    enum OperatorType operatorType_ = ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::LogicalNot;
    bool hasParameterList_ = false;
    AstType* declaringType_ = nullptr;
    Identifier* nameToken_ = nullptr;
    AstType* conversionOperatorReturnType_ = nullptr;
    AstNodeCollectionT<AstType> typeArguments_;
    AstNodeCollectionT<ParameterDeclaration> parameters_;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_DOCUMENTATIONREFERENCE_HPP
