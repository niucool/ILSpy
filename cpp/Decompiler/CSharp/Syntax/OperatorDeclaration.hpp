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

// Port of the `OperatorDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/OperatorDeclaration.cs (the generated
// `OperatorDeclaration.g.cs` + the hand-written partial, which declares the `OperatorType` enum,
// the four const-string keyword tokens, the `names` lookup table + the `GetOperatorType`/
// `GetName`/`IsChecked`/`GetToken` static helpers, the `SymbolKind` override, the five slot
// properties, the `OperatorType` scalar, and the `[EditorBrowsable(Never)]` `Name`/`NameToken`
// overrides -- no ctors). The next in-order Phase-5 piece per the D279 plan ("OperatorDeclaration
// (Type + PrivateImplementationType + Parameters + Body -- needs ParameterDeclaration, now
// ported)").
//
// `operator_declaration ::= attribute_section* modifier+ type ( type '.' )? 'operator' 'checked'?
// operator_token '(' parameter* ')' ( block | ';' )` (C# grammar 15.10.1): a sealed
// `EntityDeclaration` (the `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so
// `final` -- no pattern placeholder). Five `[Slot]` children in source declaration order:
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the operator (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's FIRST of TWO
//     collections, so `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex ==
//     slots.Count - 1` is false -- `collectionCount` is 2): every `Add`/`Insert`/`Remove`/
//     single-slot-set INVALIDATES the parent's indices for a lazy `EnsureChildIndices` rebuild
//     (the `ComposedType` D242 two-collection / `IndexerDeclaration` D279 precedent).
//   * `[Slot("Type")] public override partial AstType ReturnType` -- the operator's declared type
//     (a single REQUIRED `AstType` slot at slot 1, non-nullable; reusing `Slots::Type`). The slot
//     FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode(ref field, value)` (the dynamic flattened index after a collection; the
//     `ComposedType.BaseType` D242 / `IndexerDeclaration` D279 precedent). Overrides the base
//     `EntityDeclaration::ReturnType` (the base body kind-walks for the `Type` kind; this override
//     returns the backing field directly, the generated `get => field!`).
//   * `[Slot("PrivateImplementationType")] public partial AstType? PrivateImplementationType` --
//     the explicit-interface-implementation type (a single NULLABLE `AstType?` slot at slot 2, e.g.
//     the `I` in `int I.operator+(...)`; null when the operator is not an explicit interface
//     implementation). The slot FOLLOWS the `Attributes` collection, so the setter uses the
//     INDEX-LESS `SetChildNode`. Reusing the `Slots::PrivateImplementationType` kind (added by
//     `PropertyDeclaration` D276). NOT an override, so a plain non-virtual accessor.
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration> Parameters` --
//     the operator's parameter list (a COLLECTION at slot 3, the `( parameter* )`; reusing the
//     cycle-broken `Slots::Parameter` kind added by `IndexerDeclaration` D279). The collection is
//     the node's SECOND of TWO collections, so `supportsIncremental` is FALSE; `baseIndex` is 3
//     (the slot index; the generator passes the slot index, not the dynamic flattened index --
//     unused on the non-incremental fast path). NOT a base virtual, so a plain non-virtual
//     accessor.
//   * `[Slot("Body")] public partial BlockStatement? Body` -- the operator body (a single
//     NULLABLE `BlockStatement?` slot at slot 4; null for an abstract operator declared in an
//     interface, e.g. `public abstract int operator+(...)`). The slot FOLLOWS the `Parameters`
//     collection, so the setter uses the INDEX-LESS `SetChildNode`. Reusing the `Slots::Body` kind
//     (cycle-broken into `BlockStatement.hpp` by `CheckedStatement` D260). NOT an override, so a
//     plain non-virtual accessor.
//
// The `OperatorType` enum (the 35 C# operator kinds: unary `LogicalNot`/`OnesComplement`/
// `Increment`/..., binary `Addition`/`Subtraction`/..., and the `Implicit`/`Explicit`/`CheckedExplicit`
// conversions) is declared in the same `OperatorDeclaration.cs` file (the `ICSharpCode.Decompiler.
// CSharp.Syntax` namespace), so it ports into this header at namespace scope BEFORE the class (the
// `AccessorKind`-in-`Accessor.hpp` D274 / `FieldDirection`-in-`DirectionExpression.hpp` D235
// precedent). It declares NO `Any` member, so the generator's `hasAny` path does NOT fire and the
// generated `DoMatch` term is the PLAIN `this.OperatorType == o.OperatorType` (the
// `DirectionExpression.FieldDirection` D235 no-`Any`-enum precedent applied to an `EntityDeclaration`
// scalar). `LogicalNot` is the zero value (the C# default for an uninitialized `OperatorType`
// property).
//
// The `Name`/`NameToken` virtuals are overridden: `Name` returns `GetName(this.OperatorType)!` --
// the operator's method name (`"op_Addition"`, `"op_Implicit"`, ...) the resolver looks up by; the
// `!` is the C# null-forgiving (the input is non-nullable, so the output is never null). `NameToken`
// returns null and throws on set (the `[EditorBrowsable(Never)]` hides them -- an operator has no
// name token, the `operator` keyword + the operator token carry the identity). The `Name` term IS
// added to `DoMatch` (`NameToken` is `[EditorBrowsable]`, NOT `[ExcludeFromMatch]` -- the
// `FieldDeclaration` D273 / `IndexerDeclaration` D279 precedent: the `Name` `MatchString` term is
// present and matches the two operators' method names, so two operators with different
// `OperatorType` reject on the name mismatch). The four const-string keyword tokens
// (`OperatorKeyword`/`CheckedKeyword`/`ExplicitKeyword`/`ImplicitKeyword`) port as `static constexpr
// const char*` (the `CheckedExpression.CheckedKeyword` D234 / `EventDeclaration.EventKeyword` D277
// precedent -- static fields, NOT instance state, so NOT in `MembersToMatch`/`DoMatch`).
//
// The hand-written `GetName(OperatorType?) -> string?` static helper (the method name for an
// operator type, backed by the `names` table) is needed by the `Name` override (which is in
// `DoMatch`'s `MatchString` term), so it ports now -- faithfully as `GetName(optional<OperatorType>)
// -> optional<string>`, but the C# `string[][] names` static table is replaced by a `constexpr`
// array indexed by the enum value (the same observable behavior, no static-init-order concern; only
// the method-name column is needed, the token column backs the deferred `GetToken`). The other
// static helpers (`GetOperatorType`/`IsChecked`/`GetToken`) are consumed only by the unported
// output/resolver stage, so they are DEFERRED (the value-vs-behaviour discriminator: a const
// string and a method needed by the node's own `DoMatch` port now; methods consumed only by the
// unported output/resolver stage defer).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]` on `OperatorDeclaration`,
// so the `Name` term IS added, the `FieldDeclaration` D273 / `IndexerDeclaration` D279
// precedent), `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every
// `EntityDeclaration`-derived node, then the per-property scan adds the non-override non-
// `[ExcludeFromMatch]` `[Slot]` children AND the non-`[Slot]` settable scalars in source
// declaration order (`PrivateImplementationType`, `OperatorType`, `Parameters`, `Body`). So
// `MembersToMatch` is `[Name, MatchAttributesAndModifiers, ReturnType, PrivateImplementationType,
// OperatorType, Parameters, Body]`, and the generated `DoMatch` is `return other is
// OperatorDeclaration o && MatchString(this.Name, o.Name) && this.MatchAttributesAndModifiers(o,
// match) && MatchOptional(this.ReturnType, o.ReturnType, match) && MatchOptional(this.
// PrivateImplementationType, o.PrivateImplementationType, match) && this.OperatorType ==
// o.OperatorType && this.Parameters.DoMatch(o.Parameters, match) && MatchOptional(this.Body,
// o.Body, match)`. The `Name` term is a `MatchString` over the operator's method name (two
// operators with the same `OperatorType` match on the name, two with different `OperatorType`
// reject); the `ReturnType` term is `MatchOptional` (nullable recursive -- the generator treats
// the `EntityDeclaration` `ReturnType` uniformly as `MatchOptional`, even though the slot is
// required on `OperatorDeclaration`, the `IndexerDeclaration` D279 precedent); the
// `PrivateImplementationType`/`Body` terms are each `MatchOptional` over their nullable slots; the
// `OperatorType` term is the PLAIN `==` (a no-`Any` enum scalar, the `DirectionExpression` D235
// fall-through); the `Parameters` term is the collection recursive `DoMatch` (the generator emits
// a collection-typed recursive term directly, NOT `MatchOptional` -- the `FieldDeclaration.Variables`
// D273 / `IndexerDeclaration.Parameters` D279 precedent). The `MatchAttributesAndModifiers` helper
// (on `EntityDeclaration`, protected) matches the `Modifiers` scalar (the `Any`-wildcard) AND the
// `Attributes` collection together.
//
// The generated ctors (`WriteConstructors`): `CtorParams` is `[Attributes (collection, optional),
// ReturnType (required), PrivateImplementationType (single, optional), OperatorType (enum,
// required), Parameters (collection, optional), Body (single, optional)]` -- the `Modifiers` scalar
// lives on the `EntityDeclaration` base and is NOT a declared member of `OperatorDeclaration`, so
// `GetMembers()` does not add it (the `DestructorDeclaration` D272 / `IndexerDeclaration` D279
// precedent); the `OperatorType` settable enum IS a ctor param (the generator adds settable
// enum-typed scalars to `CtorParams`, the `VariableDeclarationStatement.Modifiers` D270 precedent).
// `RequiredConstructorPrefixLength` is 4 (through the last non-optional param `OperatorType` at
// index 3 -- the generator walks the whole `ctorParams` list and takes the last non-optional index
// + 1, the `CatchClause` D254 / `PropertyDeclaration` D276 / `ParameterDeclaration` D278
// precedent). `ConstructorPrefixLengths` is {4, 6}; the (len=4) ctor
// `(IEnumerable<AttributeSection>, AstType, AstType?, OperatorType)` and the (len=6) ctor both call
// `this.Attributes.AddRange(...)` for the `Attributes` collection (and the (len=6) ctor also calls
// `this.Parameters.AddRange(...)` for the `Parameters` collection -- both `AddRange` conveniences
// are the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. An
// `OperatorDeclaration` is built via the empty ctor + `ReturnType(...)` + `PrivateImplementationType(...)`
// + `OperatorType(...)` + `Parameters().Add(...)` + `Body(...)` + `Attributes().Add(...)` until
// `AddRange` lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitOperatorDeclaration(this)`
// (the class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitOperatorDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.Type`, required),
// `PrivateImplementationTypeSlot` (a `CSharpSlotInfoT<AstType>` pointing at
// `Slots.PrivateImplementationType`, nullable), `ParametersSlot` (a
// `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection), `BodySlot`
// (a `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`, nullable). `Clone` is inherited
// in C# (`MemberwiseClone` + `CloneChildrenInto`); the port overrides it (no `MemberwiseClone`):
// a fresh node, the `Modifiers` scalar copied via the public `Modifiers()` getter/setter (the
// base's private `modifiers_` is not accessible from the derived `Clone` -- the C#
// `MemberwiseClone` copies the private backing; the port uses the public surface, the
// `DestructorDeclaration` D272 precedent), the `OperatorType` scalar copied directly to the
// backing field (the `DirectionExpression.FieldDirection` D235 scalar-copy precedent -- the field
// is private to this class, so accessible from the derived `Clone`), the annotation channel copied
// (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223 concrete-clone pattern), the `ReturnType`
// deep-cloned through the setter (which re-parents; `AstType::Clone()` returns `AstType*`, which
// `ReturnType(AstType*)` accepts directly), every `Attributes` element deep-cloned through `Add`
// (which re-parents and re-indexes; `AttributeSection::Clone()` returns `AttributeSection*`), every
// `Parameters` element deep-cloned through `Add` (which re-parents and re-indexes;
// `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`), the `PrivateImplementationType`/
// `Body` deep-cloned through their setters (which re-parent; `AstType::Clone()`/
// `BlockStatement::Clone()` return the covariant concrete types the setters accept directly). No
// own location fields (does not derive `EndLocation`), so the print-time `StartLocation`/
// `EndLocation` are not copied (the `DestructorDeclaration` D272 / `IndexerDeclaration` D279
// no-location-copy precedent). The covariant return is `OperatorDeclaration*` (through `AstNode*`,
// the `AstNode::Clone` virtual -- `EntityDeclaration` re-declares no typed `Clone`, faithful to its
// empty hand-written partial).
//
// C++ name-shadowing crux (the `OperatorType` property): the C# property is `OperatorType` of type
// `OperatorType` -- a property named the same as its enum type, the enum equivalent of the
// `Expression`-of-type-`Expression` D231 crux (the `DirectionExpression.FieldDirection` D235
// precedent). The faithful port names the accessor `OperatorType()`, which SHADOWS the
// `OperatorType` enum in this class scope (C++ unqualified name lookup finds the member and stops,
// even though it is not a type). The getter return type and the (non-existent here) ctor
// parameter precede the getter's own declaration, so they use the plain `OperatorType` (the enum
// is not yet shadowed there); every type usage AFTER the `OperatorType()` getter (the setter
// parameter, the backing field type) uses the ELABORATED enum specifier `enum OperatorType`
// (basic.lookup.elab ignores non-type names, the enum equivalent of `class Expression`), and the
// backing-field initializer uses the fully-qualified enum name (`::ILSpy::...::OperatorType::
// LogicalNot`) because the bare `OperatorType::LogicalNot` would resolve the unqualified
// `OperatorType` to the member function and reject `::LogicalNot` on a non-type (the D235
// precedent). The `GetName` parameter type is `std::optional< ::ILSpy::...::OperatorType >` (the
// fully-qualified name -- a qualified name is looked up in the named namespace, NOT class scope,
// so it ignores the member-function shadowing; a leading space after `<` dodges the `<:` digraph
// edge case). The `DoMatch` `OperatorType` term compares the backing fields directly
// (`operatorType_ == o->operatorType_`), so no type name appears and no elaborated specifier is
// needed there. The other slot accessors (`Attributes`/`ReturnType`/`PrivateImplementationType`/
// `Parameters`/`Body`) do NOT collide with any class in the `Syntax` namespace (no class named
// `Attributes`/`ReturnType`/`PrivateImplementationType`/`Parameters`/`Body`), so no elaborated
// specifier is needed for them.
//
// NO new `Slots` constant this iteration: `Slots::AttributeSection` (cycle-broken in
// `AttributeSection.hpp` D241), `Slots::Type` (D240), `Slots::PrivateImplementationType` (D276),
// `Slots::Parameter` (cycle-broken in `ParameterDeclaration.hpp` D279), and `Slots::Body`
// (cycle-broken in `BlockStatement.hpp` D260) are all already ported. This is the
// `IndexerDeclaration` D279 two-collection shape (the `Attributes` collection plus the `Parameters`
// collection, with three singles interleaved) MINUS the trailing `Getter`/`Setter`/`ExpressionBody`
// nullable singles and PLUS the `OperatorType` scalar (a positional/scalar change, not a structural
// one), so the collection-aware slot-storage dispatch, the `MatchOptional`/`MatchRequired`/
// collection-`DoMatch`/`MatchAttributesAndModifiers` machinery, and the covariant `Clone` all
// generalize with no new `AstNode` helper.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_OPERATORDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_OPERATORDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitorBool.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum OperatorType` -- the 35 C# operator kinds (unary, binary, and the
// `Implicit`/`Explicit`/`CheckedExplicit` conversions). Declares NO `Any` member, so the
// generator's `hasAny` path does NOT fire and the generated `DoMatch` term is the plain
// `this.OperatorType == o.OperatorType` (no `Any`-wildcard). `LogicalNot` is the zero value (the
// C# default for an uninitialized `OperatorType` property). A plain `enum class` (the C#
// `enum OperatorType` defaults to `int`; the underlying width is not load-bearing here, so the
// default `int`-sized `enum class` is faithful).
enum class OperatorType {
    // Unary operators.
    LogicalNot,
    OnesComplement,
    Increment,
    CheckedIncrement,
    Decrement,
    CheckedDecrement,
    True,
    False,
    UnaryPlus,
    UnaryNegation,
    CheckedUnaryNegation,
    // Binary operators.
    Addition,
    CheckedAddition,
    Subtraction,
    CheckedSubtraction,
    Multiply,
    CheckedMultiply,
    Division,
    CheckedDivision,
    Modulus,
    BitwiseAnd,
    BitwiseOr,
    ExclusiveOr,
    LeftShift,
    RightShift,
    UnsignedRightShift,
    Equality,
    Inequality,
    GreaterThan,
    LessThan,
    GreaterThanOrEqual,
    LessThanOrEqual,
    // Implicit and Explicit conversions.
    Implicit,
    Explicit,
    CheckedExplicit
};

// The C# `public sealed partial class OperatorDeclaration : EntityDeclaration`. `final` (the C#
// `sealed`): no further derivation. The tenth concrete `TypeMember`, an operator declaration,
// deriving from `EntityDeclaration` (the `TypeMember` base), not from `Statement`/`Expression`/
// `AstType`. Structurally the `IndexerDeclaration` D279 two-collection shape (the `Attributes`
// collection plus the `Parameters` collection, with three singles interleaved) MINUS the trailing
// `Getter`/`Setter`/`ExpressionBody` nullable singles and PLUS the `OperatorType` scalar.
class OperatorDeclaration final : public EntityDeclaration {
public:
    ~OperatorDeclaration() override = default;

    // The generated empty ctor (the C# `public OperatorDeclaration()`). The `Attributes`
    // collection is a member (the D222 always-present-stack-member design), initialized here with
    // `baseIndex = 0` (the collection is the first slot) and `supportsIncremental = false` (the
    // node has TWO collections, so neither is incremental -- an element's flattened `ChildIndex`
    // is dynamic, rebuilt lazily by `EnsureChildIndices` after a mutation). The `Parameters`
    // collection is a member initialized with `baseIndex = 3` (its slot index) and
    // `supportsIncremental = false` (the second of two collections). The three singles default to
    // null; `ReturnType` is a REQUIRED slot (a default-constructed node violates its required-slot
    // invariant, the `UnaryOperatorExpression` D231 precedent; `CheckInvariant` rejects an empty
    // node), the other two are nullable so their absence is invariant-valid. `OperatorType`
    // defaults to `LogicalNot` (the enum's zero value, the C# default).
    OperatorDeclaration() : attributes_(this, &AttributesSlot, 0, false),
                            parameters_(this, &ParametersSlot, 3, false) {}

    // The C# `public const string OperatorKeyword = "operator"` / `CheckedKeyword = "checked"` /
    // `ExplicitKeyword = "explicit"` / `ImplicitKeyword = "implicit"` -- the keyword token literals
    // the output visitor emits (CSharpOutputVisitor.VisitOperatorDeclaration). Compile-time
    // literals carried as `static constexpr const char*` (static fields, not instance state, so
    // they are not part of `MembersToMatch`/`DoMatch` -- the `CheckedExpression.CheckedKeyword`
    // D234 / `EventDeclaration.EventKeyword` D277 precedent).
    static constexpr const char* OperatorKeyword = "operator";
    static constexpr const char* CheckedKeyword = "checked";
    static constexpr const char* ExplicitKeyword = "explicit";
    static constexpr const char* ImplicitKeyword = "implicit";

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.Operator; } }` --
    // the kind of member this declaration is (an operator). Overrides the base abstract
    // `SymbolKind` (the `EntityDeclaration` pure-virtual). The qualified `SymbolKind::Operator`
    // avoids a `using` (the enum lives in `ILSpy::Decompiler::TypeSystem`).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::Operator;
    }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the operator (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental -- the node's first of
    // two collections). The C# lazily allocates the wrapper; the D222 port makes the collection an
    // always-present stack member, so the accessor returns the member directly. Overrides the base
    // `EntityDeclaration::Attributes` (the base body returns a detached empty via `GetChildren`;
    // this override returns the real `attributes_` member). A `const` convenience overload returns
    // `const&` for a `const OperatorDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `ReturnType` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Type")] public override partial AstType ReturnType` -- a single
    // REQUIRED `AstType` slot at slot 1 (the operator's declared type; non-nullable, so required
    // -- `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the property
    // setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened index
    // after a collection). Overrides the base `EntityDeclaration::ReturnType` (the base body
    // kind-walks for the `Type` kind; this override returns the backing field directly, the
    // generated `get => field!`). The getter returns the raw pointer (null for a half-constructed
    // node; the C# `!` null-forgiving).
    AstType* ReturnType() const override { return returnType_; }
    void ReturnType(AstType* value) override {
        SetChildNode(returnType_, value);
    }

    // ---- The `PrivateImplementationType` slot (a NULLABLE single `AstType`, NOT a base virtual) -
    // The generated `[Slot("PrivateImplementationType")] public partial AstType?
    // PrivateImplementationType` -- a single NULLABLE `AstType?` slot at slot 2 (the
    // explicit-interface-implementation type, e.g. the `I` in `int I.operator+(...)`; null when
    // the operator is not an explicit interface implementation). The slot FOLLOWS the `Attributes`
    // collection, so the property setter uses the INDEX-LESS `SetChildNode(ref field, value)`. NOT
    // an override (the `EntityDeclaration` base declares no `PrivateImplementationType` virtual),
    // so a plain non-virtual accessor. `CheckInvariant` passes without a `PrivateImplementationType`
    // (the slot is nullable). Reuses the `Slots::PrivateImplementationType` kind (added by
    // `PropertyDeclaration` D276).
    AstType* PrivateImplementationType() const { return privateImplementationType_; }
    void PrivateImplementationType(AstType* value) {
        SetChildNode(privateImplementationType_, value);
    }

    // ---- The `OperatorType` scalar (a settable enum, NOT a `[Slot]`) ------------------------
    // The C# `public OperatorType OperatorType { get; set; }` -- a scalar enum (not a `[Slot]`).
    // A settable enum-typed scalar the generator adds to `MembersToMatch` (with the PLAIN equality
    // term, since `OperatorType` has no `Any` member -- the `DirectionExpression.FieldDirection`
    // D235 no-`Any`-enum fall-through) and to the ctor params (the `VariableDeclarationStatement.
    // Modifiers` D270 settable-enum-ctor-param precedent). The return type precedes the getter's
    // own declaration, so the plain `OperatorType` (the enum) is unshadowed in the getter
    // signature.
    OperatorType OperatorType() const { return operatorType_; }
    // The setter parameter type uses the elaborated enum specifier `enum OperatorType`: the
    // `OperatorType()` getter declared just above shadows the `OperatorType` enum in this class
    // scope (the D235 `FieldDirection` precedent -- the enum equivalent of the `class Expression`
    // elaborated specifier), so the plain name would resolve to the member function (not a type).
    void OperatorType(enum OperatorType value) { operatorType_ = value; }

    // ---- The `Parameters` collection slot (NOT a base virtual) -----------------------------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // Parameters` -- the operator's parameter list (a `CSharpSlotInfoT<ParameterDeclaration>` slot
    // at slot 3, the node's SECOND of TWO collections, non-incremental). The C# lazily allocates
    // the wrapper; the D222 port makes the collection an always-present stack member, so the
    // accessor returns the member directly. NOT a base virtual, so a plain non-virtual accessor.
    // Reuses the `Slots::Parameter` kind (cycle-broken into `ParameterDeclaration.hpp` by
    // `IndexerDeclaration` D279). A `const` convenience overload returns `const&` for a `const
    // OperatorDeclaration*`.
    AstNodeCollectionT<ParameterDeclaration>& Parameters() { return parameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& Parameters() const { return parameters_; }

    // ---- The `Body` slot (a NULLABLE single `BlockStatement`, NOT a base virtual) ----------
    // The generated `[Slot("Body")] public partial BlockStatement? Body` -- a single NULLABLE
    // `BlockStatement?` slot at slot 4 (the operator body; null for an abstract operator declared
    // in an interface). The slot FOLLOWS the `Parameters` collection, so the property setter uses
    // the INDEX-LESS `SetChildNode(ref field, value)`. NOT an override, so a plain non-virtual
    // accessor. `CheckInvariant` passes without a `Body` (the slot is nullable). Reuses the
    // `Slots::Body` kind (cycle-broken into `BlockStatement.hpp` by `CheckedStatement` D260).
    BlockStatement* Body() const { return body_; }
    void Body(BlockStatement* value) {
        SetChildNode(body_, value);
    }

    // ---- The `Name`/`NameToken` overrides (hidden from users; an operator has no name token) -
    // The C# `public override string Name { get { return GetName(this.OperatorType)!; } set {
    // throw new NotSupportedException(); } }` -- the operator's method name (`"op_Addition"`,
    // `"op_Implicit"`, ...) the resolver looks up by. The `!` is the C# null-forgiving (the input
    // `this.OperatorType` is non-nullable, so `GetName` returns a non-null string). The setter
    // throws (`NotSupportedException` ports as `std::logic_error`). Overrides the base
    // `EntityDeclaration::Name` virtual. The `Name` `MatchString` term in `DoMatch` matches the
    // two operators' method names (two operators with the same `OperatorType` match, two with
    // different `OperatorType` reject on the name mismatch -- the `IndexerDeclaration` D279
    // fixed-`Name` precedent generalized to a `Name` that depends on the scalar).
    std::string Name() const override {
        return GetName(operatorType_).value();
    }
    void Name(std::string_view) override {
        throw std::logic_error("OperatorDeclaration.Name is not supported");
    }
    // The C# `[EditorBrowsable(EditorBrowsableState.Never)] public override Identifier NameToken
    // { get { return null!; } set { throw new NotSupportedException(); } }` -- an operator has no
    // name token (the `operator` keyword + the operator token carry the identity), so the
    // inherited `NameToken` is hidden and returns null. The setter throws. Overrides the base
    // `EntityDeclaration::NameToken` virtual (the `FieldDeclaration` D273 / `EventDeclaration`
    // D277 / `IndexerDeclaration` D279 throw-on-set precedent).
    Identifier* NameToken() const override { return nullptr; }
    void NameToken(Identifier*) override {
        throw std::logic_error("OperatorDeclaration.NameToken is not supported");
    }

    // ---- The `GetName` static helper (the operator method-name lookup) --------------------
    // The C# `public static string? GetName(OperatorType? type)` -- the method name for the
    // operator type (`"op_Addition"`, `"op_Implicit"`, ...), or null if `type` is null. Backed by
    // the hand-written `names` table in the C# (a `string[][]` indexed by the enum value, the
    // second column the method name); the port replaces the table with a `constexpr` array of the
    // method-name column indexed by the enum value (the same observable behavior, no
    // static-init-order concern; only the method-name column is needed -- the token column backs
    // the deferred `GetToken`). Needed by the `Name` override (which is in `DoMatch`'s
    // `MatchString` term), so it ports now. The parameter type is the fully-qualified
    // `::ILSpy::...::OperatorType` (a qualified name is looked up in the named namespace, NOT
    // class scope, so it ignores the `OperatorType()` member-function shadowing; the leading space
    // after `<` dodges the `<:` digraph edge case). DEFERRED: `GetOperatorType` (the reverse
    // lookup by method name) -- consumed only by the unported resolver stage. `IsChecked` (the
    // `checked`-operator predicate) and `GetToken` (the operator token like `"+"`) are now ported
    // (consumed by `CSharpOutputVisitor.VisitOperatorDeclaration`).
    static std::optional<std::string> GetName(std::optional< ::ILSpy::Decompiler::CSharp::Syntax::OperatorType > type) {
        if (!type.has_value())
            return std::nullopt;
        // The method-name column of the C# `names` table, indexed by the `OperatorType` value.
        // 35 entries (`LogicalNot`=0 .. `CheckedExplicit`=34), matching `names = new string[
        // (int)OperatorType.CheckedExplicit + 1][]` in the C# static ctor.
        static constexpr const char* const kMethodNames[] = {
            "op_LogicalNot",        // LogicalNot
            "op_OnesComplement",    // OnesComplement
            "op_Increment",         // Increment
            "op_CheckedIncrement",  // CheckedIncrement
            "op_Decrement",         // Decrement
            "op_CheckedDecrement",  // CheckedDecrement
            "op_True",              // True
            "op_False",             // False
            "op_UnaryPlus",         // UnaryPlus
            "op_UnaryNegation",     // UnaryNegation
            "op_CheckedUnaryNegation", // CheckedUnaryNegation
            "op_Addition",          // Addition
            "op_CheckedAddition",   // CheckedAddition
            "op_Subtraction",       // Subtraction
            "op_CheckedSubtraction", // CheckedSubtraction
            "op_Multiply",          // Multiply
            "op_CheckedMultiply",   // CheckedMultiply
            "op_Division",          // Division
            "op_CheckedDivision",   // CheckedDivision
            "op_Modulus",           // Modulus
            "op_BitwiseAnd",        // BitwiseAnd
            "op_BitwiseOr",         // BitwiseOr
            "op_ExclusiveOr",       // ExclusiveOr
            "op_LeftShift",         // LeftShift
            "op_RightShift",        // RightShift
            "op_UnsignedRightShift", // UnsignedRightShift
            "op_Equality",          // Equality
            "op_Inequality",        // Inequality
            "op_GreaterThan",       // GreaterThan
            "op_LessThan",          // LessThan
            "op_GreaterThanOrEqual", // GreaterThanOrEqual
            "op_LessThanOrEqual",   // LessThanOrEqual
            "op_Implicit",          // Implicit
            "op_Explicit",          // Explicit
            "op_CheckedExplicit"    // CheckedExplicit
        };
        constexpr int kCount = static_cast<int>(sizeof(kMethodNames) / sizeof(kMethodNames[0]));
        int idx = static_cast<int>(*type);
        if (idx < 0 || idx >= kCount)
            return std::nullopt;
        return std::string(kMethodNames[idx]);
    }

    // ---- The `IsChecked` static helper (the C# 11 `checked`-operator predicate) -------------
    // The C# `public static bool IsChecked(OperatorType type)` -- true for the eight
    // `checked`-flavoured operator kinds (the `OperatorType.Checked*` variants). Consumed by
    // `CSharpOutputVisitor.VisitOperatorDeclaration`, which emits the `checked` keyword before the
    // operator token when this returns true (the `operator checked +(..)` / `checked explicit
    // operator T(..)` C# 11 syntax). The parameter type is the fully-qualified
    // `::ILSpy::...::OperatorType` (a qualified name is looked up in the named namespace, NOT class
    // scope, so it ignores the `OperatorType()` member-function shadowing; the leading space after
    // `<` dodges the `<:` digraph edge case -- the `GetName` precedent). The C# `switch` expression
    // ports to a chain of `==` comparisons against the eight checked variants.
    static bool IsChecked(::ILSpy::Decompiler::CSharp::Syntax::OperatorType type) {
        return type == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::CheckedAddition
            || type == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::CheckedSubtraction
            || type == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::CheckedMultiply
            || type == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::CheckedDivision
            || type == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::CheckedUnaryNegation
            || type == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::CheckedIncrement
            || type == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::CheckedDecrement
            || type == ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::CheckedExplicit;
    }

    // ---- The `GetToken` static helper (the operator-token lookup) ---------------------------
    // The C# `public static string GetToken(OperatorType type)` -- the operator's source token
    // (the `+` of `operator +`, the `>>` of `operator >>`, the `implicit`/`explicit` of the
    // conversion operators), read from the FIRST column of the hand-written `names` table. The
    // `GetName` helper above reads the SECOND column (the method name); `GetToken` reads the FIRST.
    // Consumed by `CSharpOutputVisitor.VisitOperatorDeclaration`, which writes the token for a
    // non-conversion operator (the conversion operators write the return type instead). The
    // parameter type is the fully-qualified `::ILSpy::...::OperatorType` (the `GetName`/`IsChecked`
    // shadowing-dodging precedent). The port replaces the C# table with a `constexpr` array of the
    // token column indexed by the enum value (the same observable behavior, no static-init-order
    // concern -- the `GetName` precedent). 35 entries, parallel to `kMethodNames`.
    static std::string GetToken(::ILSpy::Decompiler::CSharp::Syntax::OperatorType type) {
        // The token column of the C# `names` table, indexed by the `OperatorType` value.
        // 35 entries (`LogicalNot`=0 .. `CheckedExplicit`=34), parallel to `kMethodNames`.
        static constexpr const char* const kTokens[] = {
            "!",            // LogicalNot
            "~",            // OnesComplement
            "++",           // Increment
            "++",           // CheckedIncrement
            "--",           // Decrement
            "--",           // CheckedDecrement
            "true",         // True
            "false",        // False
            "+",            // UnaryPlus
            "-",            // UnaryNegation
            "-",            // CheckedUnaryNegation
            "+",            // Addition
            "+",            // CheckedAddition
            "-",            // Subtraction
            "-",            // CheckedSubtraction
            "*",            // Multiply
            "*",            // CheckedMultiply
            "/",            // Division
            "/",            // CheckedDivision
            "%",            // Modulus
            "&",            // BitwiseAnd
            "|",            // BitwiseOr
            "^",            // ExclusiveOr
            "<<",           // LeftShift
            ">>",           // RightShift
            ">>>",          // UnsignedRightShift
            "==",           // Equality
            "!=",           // Inequality
            ">",            // GreaterThan
            "<",            // LessThan
            ">=",           // GreaterThanOrEqual
            "<=",           // LessThanOrEqual
            "implicit",     // Implicit
            "explicit",     // Explicit
            "explicit"      // CheckedExplicit
        };
        constexpr int kCount = static_cast<int>(sizeof(kTokens) / sizeof(kTokens[0]));
        int idx = static_cast<int>(type);
        if (idx < 0 || idx >= kCount)
            return std::string();
        return std::string(kTokens[idx]);
    }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's first collection); `ReturnTypeSlot` (a `CSharpSlotInfoT<AstType>`
    // pointing at `Slots.Type`, required); `PrivateImplementationTypeSlot` (a
    // `CSharpSlotInfoT<AstType>` pointing at `Slots.PrivateImplementationType`, nullable);
    // `ParametersSlot` (a `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`,
    // collection -- the node's second collection); `BodySlot` (a
    // `CSharpSlotInfoT<BlockStatement>` pointing at `Slots.Body`, nullable). NO name shadowing (no
    // member is named `AttributeSection`/`AstType`/`ParameterDeclaration`/`BlockStatement`), so the
    // element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<AstType> ReturnTypeSlot{"ReturnType", false, &Slots::Type, false};
    static inline const CSharpSlotInfoT<AstType> PrivateImplementationTypeSlot{"PrivateImplementationType", false, &Slots::PrivateImplementationType, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> ParametersSlot{"Parameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<BlockStatement> BodySlot{"Body", false, &Slots::Body, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitOperatorDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitOperatorDeclaration(this);
    }

    // The C# `public override T AcceptVisitor<T>(IAstVisitor<T> visitor)` (instantiated
    // `T = bool`) -- the `<bool>`-variant dispatch entry: routes back to
    // `VisitOperatorDeclaration`, returning its `bool` result (the stop/continue signal the
    // `DepthFirstAstVisitor<bool>` walk consumes). Mirrors the void `AcceptVisitor`
    // above; `IAstVisitorBool` is the `IAstVisitor<out S>` interface instantiated
    // `S = bool` (IAstVisitorBool.hpp).
    bool AcceptVisitorBool(IAstVisitorBool& visitor) override {
        return visitor.VisitOperatorDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Five slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `ReturnType` single at slot 1 (`attrCount`), a `PrivateImplementationType`
    // single at slot 2 (`attrCount + 1`), a `Parameters` collection at slot 3 (the contiguous range
    // `[attrCount + 2, attrCount + 2 + paramCount)`), and a `Body` single at slot 4
    // (`attrCount + 2 + paramCount`). `GetChildCount` is `attrCount + paramCount + 3` (the two
    // collections' current lengths plus the three singles -- each single slot contributes 1 to
    // the flattened count regardless of whether it is filled); `GetChild`/`SetChild`/
    // `GetChildSlotInfo` walk the slots subtracting each one's width from a running index (the
    // generator's `WriteReturnDispatchWithCollections`/`WriteSetChildWithCollections` shape -- a
    // collection step, two single steps, a collection step, then a single step).
    // `GetCollectionByKind` returns the `Attributes` collection for the `AttributeSection` kind and
    // the `Parameters` collection for the `Parameter` kind. The `OperatorType` scalar is NOT a slot,
    // so it does not appear here. This is the `IndexerDeclaration` D279 two-collection shape with
    // one trailing single (`Body`) instead of three (`Getter`/`Setter`/`ExpressionBody`).
    int GetChildCount() const override { return attributes_.Count() + parameters_.Count() + 3; }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        if (i == 0)
            return returnType_;
        i--;
        if (i == 0)
            return privateImplementationType_;
        i--;
        {
            int n = parameters_.Count();
            if (i < n)
                return parameters_.At(i);
            i -= n;
        }
        if (i == 0)
            return body_;
        throw std::out_of_range("OperatorDeclaration::GetChild");
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
            SetChildNode(returnType_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        if (i == 0) {
            SetChildNode(privateImplementationType_, static_cast<AstType*>(value), index);
            return;
        }
        i--;
        {
            int n = parameters_.Count();
            if (i < n) {
                parameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        if (i == 0) {
            SetChildNode(body_, static_cast<BlockStatement*>(value), index);
            return;
        }
        throw std::out_of_range("OperatorDeclaration::SetChild");
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
            return &ReturnTypeSlot;
        i--;
        if (i == 0)
            return &PrivateImplementationTypeSlot;
        i--;
        {
            int n = parameters_.Count();
            if (i < n)
                return &ParametersSlot;
            i -= n;
        }
        if (i == 0)
            return &BodySlot;
        throw std::out_of_range("OperatorDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::Parameter)
            return &parameters_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is OperatorDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && MatchOptional(this.PrivateImplementationType, o.PrivateImplementationType, match)
    // && this.OperatorType == o.OperatorType && this.Parameters.DoMatch(o.Parameters, match) &&
    // MatchOptional(this.Body, o.Body, match)`. The terms are in `MembersToMatch` order (the
    // generator adds `Name`, `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every
    // `EntityDeclaration`-derived node -- `NameToken` is NOT `[ExcludeFromMatch]` on
    // `OperatorDeclaration`, so the `Name` `String` term IS added, the `FieldDeclaration` D273 /
    // `IndexerDeclaration` D279 precedent; then the per-property scan adds
    // `PrivateImplementationType`/`OperatorType`/`Parameters`/`Body` in source declaration order).
    // The `Name` term is a `MatchString` over the operator's method name (the `Name()` override
    // returns `GetName(this.OperatorType)`, so two operators with the same `OperatorType` match on
    // the name and two with different `OperatorType` reject); the `ReturnType` term is
    // `MatchOptional` (nullable recursive -- the generator treats the `EntityDeclaration`
    // `ReturnType` uniformly as `MatchOptional`, even though the slot is required here); the
    // `PrivateImplementationType`/`Body` terms are each `MatchOptional` over their nullable slots;
    // the `OperatorType` term is the PLAIN `==` (a no-`Any` enum scalar, the `DirectionExpression`
    // D235 fall-through); the `Parameters` term is the collection recursive `DoMatch` (the
    // generator emits a collection-typed recursive term directly, NOT `MatchOptional` -- the
    // `FieldDeclaration.Variables` D273 / `IndexerDeclaration.Parameters` D279 precedent). A
    // type-only mismatch (not an `OperatorDeclaration`) rejects early. The `Name()` calls are
    // inlined in the `MatchString` arguments (the `MemberType` D238 / `IndexerDeclaration` D279
    // precedent) so the C# `&&` short-circuit is preserved; the `std::string` temporaries live
    // until the end of the full `return` expression, so the `std::string_view` views are valid for
    // the `MatchString` call. The `OperatorType` term compares the backing fields directly
    // (`operatorType_ == o->operatorType_`), so no type name appears (no elaborated specifier
    // needed there).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<OperatorDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(returnType_, o->returnType_, match)
            && MatchOptional(privateImplementationType_, o->privateImplementationType_, match)
            && operatorType_ == o->operatorType_
            && parameters_.DoMatch(o->parameters_, match)
            && MatchOptional(body_, o->body_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 precedent), the `OperatorType` scalar
    // copied directly to the backing field (the `DirectionExpression.FieldDirection` D235
    // scalar-copy precedent -- the field is private to this class, so accessible from the derived
    // `Clone`), the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
    // concrete-clone pattern), the `ReturnType` deep-cloned through the setter (which re-parents;
    // `AstType::Clone()` returns `AstType*`, which `ReturnType(AstType*)` accepts directly), every
    // `Attributes` element deep-cloned through `Add` (which re-parents and re-indexes;
    // `AttributeSection::Clone()` returns `AttributeSection*`), every `Parameters` element
    // deep-cloned through `Add` (which re-parents and re-indexes; `ParameterDeclaration::Clone()`
    // returns `ParameterDeclaration*`), and the `PrivateImplementationType`/`Body` deep-cloned
    // through their setters (which re-parent; `AstType::Clone()`/`BlockStatement::Clone()` return
    // the covariant concrete types the setters accept directly). No own location fields (does
    // not derive `EndLocation`), so the print-time `StartLocation`/`EndLocation` are not copied
    // (the `DestructorDeclaration` D272 / `IndexerDeclaration` D279 no-location-copy precedent).
    // The covariant return is `OperatorDeclaration*` (through `AstNode*`, the `AstNode::Clone`
    // virtual -- `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty
    // hand-written partial; the covariant `OperatorDeclaration*` is a valid override of
    // `AstNode::Clone`).
    OperatorDeclaration* Clone() const override {
        auto* node = new OperatorDeclaration();
        node->Modifiers(Modifiers());
        node->CloneAnnotationsFrom(*this);
        if (returnType_ != nullptr)
            node->ReturnType(returnType_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        if (privateImplementationType_ != nullptr)
            node->PrivateImplementationType(privateImplementationType_->Clone());
        node->operatorType_ = operatorType_;
        for (int i = 0; i < parameters_.Count(); i++)
            node->parameters_.Add(parameters_.At(i)->Clone());
        if (body_ != nullptr)
            node->Body(body_->Clone());
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_`/`parameters_` are the always-present collection members
    // (empty until the first `Add`, both non-incremental -- the node has two collections);
    // `returnType_` is null until set (a REQUIRED slot -- `CheckInvariant` asserts it is filled);
    // `privateImplementationType_`/`body_` are null until set (NULLABLE slots -- their absence is
    // invariant-valid). NO name shadowing (no member is named `AttributeSection`/`AstType`/
    // `ParameterDeclaration`/`BlockStatement`), so the field types are the plain classes. The
    // `OperatorType` scalar backing field uses the elaborated enum specifier `enum OperatorType`
    // (the `OperatorType()` accessor declared above shadows the `OperatorType` enum in this class
    // scope -- the D235 `FieldDirection` precedent); the initializer uses the fully-qualified enum
    // name because the bare `OperatorType::LogicalNot` would resolve the unqualified `OperatorType`
    // to the member function and reject `::LogicalNot` on a non-type. `LogicalNot` is the enum's
    // zero value (the C# default).
    AstNodeCollectionT<AttributeSection> attributes_;
    AstType* returnType_ = nullptr;
    AstType* privateImplementationType_ = nullptr;
    AstNodeCollectionT<ParameterDeclaration> parameters_;
    BlockStatement* body_ = nullptr;
    enum OperatorType operatorType_ = ::ILSpy::Decompiler::CSharp::Syntax::OperatorType::LogicalNot;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_OPERATORDECLARATION_HPP
