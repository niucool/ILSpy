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

// Port of the `TypeDeclaration` concrete node in
// ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/TypeDeclaration.cs (the generated
// `TypeDeclaration.g.cs` + the hand-written partial, which declares only the `SymbolKind`
// override and the eight slot/scalar properties -- no ctors, no helpers, no const strings). The
// next in-order Phase-5 piece per the D291 plan (the central type-declaration node, the remaining
// GeneralScope `EntityDeclaration` whose dependencies are all ported: `AttributeSection` D241 +
// `Identifier` D237 + `TypeParameterDeclaration` D282 + `ParameterDeclaration` D278 + `AstType`
// D236 + `Constraint` D283 + the `TypeMember` collection kind D285).
//
// `type_declaration ::= attribute_section* modifier* ( 'class' | 'struct' | 'interface' | 'enum' |
// 'record' 'class'? | 'record' 'struct' ) identifier type_parameter* ( '(' parameter* ')' )? (
// ':' type+ )? constraint* '{' member* '}' ';'?` (C# grammar 15.2.1/16.2.1/19.2.1/20.2): a sealed
// `EntityDeclaration` (the `[DecompilerAstNode]` default `hasPatternPlaceholder: false`, so `final`
// -- no pattern placeholder). Seven `[Slot]` children in source declaration order plus two
// non-`[Slot]` settable scalars:
//   * `public ClassType ClassType { get; set; }` -- a scalar enum (NOT a `[Slot]`), the kind of type
//     (`Class`/`Struct`/`Interface`/`Enum`/`RecordClass`/`RecordStruct`). Declares NO `Any` member,
//     so the generator's `hasAny` path does NOT fire and the generated `DoMatch` term is the plain
//     `this.ClassType == o.ClassType` (no `Any`-wildcard). `Class` is the zero value (the C# default
//     for an uninitialized `ClassType` property). A settable enum-typed scalar the generator adds to
//     `MembersToMatch` (the `DirectionExpression.FieldDirection` D235 no-`Any`-enum fall-through)
//     and to the ctor params (the `VariableDeclarationStatement.Modifiers` D270 /
//     `OperatorDeclaration.OperatorType` D280 settable-enum-ctor-param precedent).
//   * `[Slot("AttributeSection")] public override partial AstNodeCollection<AttributeSection>
//     Attributes` -- the attribute sections on the type (a COLLECTION at slot 0, reusing the
//     cycle-broken `Slots::AttributeSection` kind). The collection is the node's FIRST of SIX
//     collections, so `supportsIncremental` is FALSE (`collectionCount == 1 && slotIndex ==
//     slots.Count - 1` is false -- `collectionCount` is 6): every `Add`/`Insert`/`Remove`/
//     single-slot-set INVALIDATES the parent's indices for a lazy `EnsureChildIndices` rebuild
//     (the `MethodDeclaration` D284 four-collection precedent, generalized to SIX collections --
//     the first ported node with more than four collections). Overrides the base
//     `EntityDeclaration::Attributes` (the base body returns a detached empty via `GetChildren`;
//     this override returns the real `attributes_` member).
//   * `[Slot("Identifier")] public override partial Identifier NameToken` -- the type's name token
//     (a single REQUIRED `Identifier` slot at slot 1, non-nullable; reusing `Slots::Identifier`).
//     The slot FOLLOWS the `Attributes` collection, so the property setter uses the INDEX-LESS
//     `SetChildNode(ref field, value)` (the dynamic flattened index after a collection; the
//     `ComposedType.BaseType` D242 / `MethodDeclaration.NameToken` D284 precedent). NOT
//     `[ExcludeFromMatch]` (unlike `ConstructorDeclaration` D281 / `DestructorDeclaration` D272),
//     so the `Name` `MatchString` term IS in `DoMatch` (the `FieldDeclaration` D273 /
//     `MethodDeclaration` D284 precedent -- a type's name is a real name the resolver looks up
//     by). Overrides the base `EntityDeclaration::NameToken` (the base body kind-walks for the
//     `Identifier` kind; this override returns the backing field directly, the generated
//     `get => field!`). The inherited base `EntityDeclaration::Name` (NOT overridden -- the C#
//     declares no `Name` override) kind-walks for the `Identifier` kind and returns the
//     `NameToken`'s `Name`, so the `DoMatch` `MatchString` term matches the two types' names.
//   * `[Slot("TypeParameter")] public partial AstNodeCollection<TypeParameterDeclaration>
//     TypeParameters` -- the type's generic type parameters (a COLLECTION at slot 2, the
//     `<T, U, ...>` of a generic type; reusing the cycle-broken `Slots::TypeParameter` kind,
//     added by `MethodDeclaration` D284 into `TypeParameterDeclaration.hpp` after the class). The
//     collection is the node's SECOND of SIX collections, so `supportsIncremental` is FALSE;
//     `baseIndex` is 2 (the slot index; the generator passes the slot index, not the dynamic
//     flattened index -- unused on the non-incremental fast path). NOT a base virtual, so a plain
//     non-virtual accessor.
//   * `public bool HasPrimaryConstructor { get; set; }` -- a scalar bool (NOT a `[Slot]`), whether
//     the type has a primary constructor (C# 12: the `( parameter* )` before the base-type list
//     belongs to the type itself, not a member). Declared between `TypeParameters` and
//     `PrimaryConstructorParameters` in source, so it appears between them in `MembersToMatch`. A
//     bool is NOT an enum, so it is NOT a ctor param (the generator adds only settable
//     ENUM-typed scalars to `CtorParams`); set via the property setter. The generator's
//     `DoMatchTerm` fall-through emits the plain `this.HasPrimaryConstructor == o.HasPrimaryConstructor`
//     (the `ComposedType.IsDoubleColon` D238 / `Accessor.HasArgumentList`-style plain-bool term).
//   * `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
//     PrimaryConstructorParameters` -- the type's primary-constructor parameter list (a
//     COLLECTION at slot 3, the `( parameter* )` of a C# 12 record/primary-constructor type; reusing
//     the cycle-broken `Slots::Parameter` kind added by `IndexerDeclaration` D279). The collection
//     is the node's THIRD of SIX collections, so `supportsIncremental` is FALSE; `baseIndex` is 3.
//     NOT a base virtual, so a plain non-virtual accessor.
//   * `[Slot("BaseType")] public partial AstNodeCollection<AstType> BaseTypes` -- the type's base
//     type and implemented interfaces (a COLLECTION at slot 4, the `: type+` of a type with a base
//     list; reusing `Slots::BaseType` added by `Constraint` D283). The collection is the node's
//     FOURTH of SIX collections, so `supportsIncremental` is FALSE; `baseIndex` is 4. NOT a base
//     virtual, so a plain non-virtual accessor.
//   * `[Slot("Constraint")] public partial AstNodeCollection<Constraint> Constraints` -- the type's
//     generic `where` clauses (a COLLECTION at slot 5, the `where T : ...` clauses of a generic
//     type; reusing the cycle-broken `Slots::Constraint` kind added by `MethodDeclaration` D284
//     into `Constraint.hpp` after the class). The collection is the node's FIFTH of SIX
//     collections, so `supportsIncremental` is FALSE; `baseIndex` is 5. NOT a base virtual, so a
//     plain non-virtual accessor.
//   * `[Slot("TypeMember")] public partial AstNodeCollection<EntityDeclaration> Members` -- the
//     type's member declarations (a COLLECTION at slot 6, the `member*` in the `{ ... }` body; reusing
//     the cycle-broken `Slots::TypeMember` kind added by `ExtensionDeclaration` D285 into
//     `EntityDeclaration.hpp` after the class). The collection is the node's SIXTH of SIX
//     collections, so `supportsIncremental` is FALSE; `baseIndex` is 6. NOT a base virtual, so a
//     plain non-virtual accessor. The element type is the ABSTRACT `EntityDeclaration` base, which
//     redeclares NO typed `Clone` (it inherits `AstNode::Clone` returning `AstNode*`), so each
//     `Members` element is cloned via `static_cast<EntityDeclaration*>(elem->Clone())` (the
//     `ExtensionDeclaration` D285 abstract-`EntityDeclaration`-collection Clone precedent).
//
// The generated `DoMatch` (`WriteDoMatch` over `MembersToMatch`): the generator adds `Name` (a
// `String` `MatchString` term -- `NameToken` is NOT `[ExcludeFromMatch]`, so the `Name` term IS
// added, the `MethodDeclaration` D284 precedent), `MatchAttributesAndModifiers`, and `ReturnType`
// explicitly for every `EntityDeclaration`-derived node, then the per-property scan adds the
// non-override non-`[ExcludeFromMatch]` settable properties in source declaration order
// (`ClassType`, `TypeParameters`, `HasPrimaryConstructor`, `PrimaryConstructorParameters`,
// `BaseTypes`, `Constraints`, `Members`). So `MembersToMatch` is `[Name, MatchAttributesAndModifiers,
// ReturnType, ClassType, TypeParameters, HasPrimaryConstructor, PrimaryConstructorParameters,
// BaseTypes, Constraints, Members]`, and the generated `DoMatch` is `return other is TypeDeclaration
// o && MatchString(this.Name, o.Name) && this.MatchAttributesAndModifiers(o, match) &&
// MatchOptional(this.ReturnType, o.ReturnType, match) && this.ClassType == o.ClassType &&
// this.TypeParameters.DoMatch(o.TypeParameters, match) && this.HasPrimaryConstructor ==
// o.HasPrimaryConstructor && this.PrimaryConstructorParameters.DoMatch(o.
// PrimaryConstructorParameters, match) && this.BaseTypes.DoMatch(o.BaseTypes, match) &&
// this.Constraints.DoMatch(o.Constraints, match) && this.Members.DoMatch(o.Members, match)`. The
// `Name` term is a `MatchString` over the type's name (the inherited base `Name()` kind-walks for
// the `Identifier` kind and returns the `NameToken`'s `Name`); the `ReturnType` term is
// `MatchOptional` (nullable recursive -- the generator treats the `EntityDeclaration` `ReturnType`
// uniformly as `MatchOptional`; a `TypeDeclaration` has no `ReturnType` slot, so the inherited base
// `ReturnType()` kind-walk returns null and `MatchOptional(null, null)` is always true, the
// `DestructorDeclaration` D272 vacuous-`ReturnType` precedent); the `ClassType` term is the PLAIN
// `==` (a no-`Any` enum scalar, the `DirectionExpression` D235 / `OperatorDeclaration.OperatorType`
// D280 fall-through); the `HasPrimaryConstructor` term is the PLAIN `==` (a non-enum non-string
// scalar, the `ComposedType.IsDoubleColon` D238 fall-through); the `TypeParameters`/
// `PrimaryConstructorParameters`/`BaseTypes`/`Constraints`/`Members` terms are each the collection
// recursive `DoMatch` (the generator emits a collection-typed recursive term directly, NOT
// `MatchOptional` -- the `FieldDeclaration.Variables` D273 / `MethodDeclaration` D284 precedent).
// A type-only mismatch (not a `TypeDeclaration`) rejects early. The `Name()` calls are inlined in
// the `MatchString` arguments (the `MemberType` D238 / `MethodDeclaration` D284 precedent) so the
// C# `&&` short-circuit is preserved; the `std::string` temporaries live until the end of the full
// `return` expression, keeping the `std::string_view` views valid for the `MatchString` call. The
// `ClassType`/`HasPrimaryConstructor` terms compare the backing fields directly (`classType_ ==
// o->classType_` / `hasPrimaryConstructor_ == o->hasPrimaryConstructor_`), so no type name appears
// (no elaborated specifier needed there). The `ReturnType` term calls the inherited base
// `ReturnType()` virtual (no backing field -- a `TypeDeclaration` declares no `Type` slot), the
// `DestructorDeclaration` D272 precedent.
//
// The generated ctors (`WriteConstructors`): `CtorParams` in declaration order is `[ClassType
// (enum scalar, required), Attributes (collection, optional), NameToken (single, required),
// TypeParameters (collection, optional), PrimaryConstructorParameters (collection, optional),
// BaseTypes (collection, optional), Constraints (collection, optional), Members (collection,
// optional)]` -- the `Modifiers` scalar lives on the `EntityDeclaration` base and is NOT a declared
// member of `TypeDeclaration`, so `GetMembers()` does not add it (the `MethodDeclaration` D284
// precedent); `HasPrimaryConstructor` is a bool (NOT an enum), so it is NOT a ctor param (the
// generator adds only settable ENUM-typed scalars to `CtorParams`). `RequiredConstructorPrefixLength`
// is 3 (through the last non-optional param `NameToken` at index 2 -- the generator walks the whole
// `ctorParams` list and takes the last non-optional index + 1, the `CatchClause` D269 /
// `MethodDeclaration` D284 / `DelegateDeclaration` D291 precedent). `ConstructorPrefixLengths` is
// `{3, 8}`. The (len=3) ctor `(ClassType, IEnumerable<AttributeSection>, Identifier)` and the
// (len=8) ctor both call `this.Attributes.AddRange(...)` for the `Attributes` collection (and the
// (len=8) ctor also calls `this.TypeParameters.AddRange(...)`, `this.PrimaryConstructorParameters.
// AddRange(...)`, `this.BaseTypes.AddRange(...)`, `this.Constraints.AddRange(...)`, and
// `this.Members.AddRange(...)` for the five further collections -- all `AddRange` conveniences are
// the D222 deferral), so they are DEFERRED; the empty ctor is the only portable ctor. A
// `TypeDeclaration` is built via the empty ctor + `ClassType(...)` + `NameToken(...)` +
// `TypeParameters().Add(...)` + `PrimaryConstructorParameters().Add(...)` + `BaseTypes().Add(...)`
// + `Constraints().Add(...)` + `Members().Add(...)` + `Attributes().Add(...)` until `AddRange`
// lands.
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete node is hand-translated from the
// generated output. The generated `AcceptVisitor` calls `visitor.VisitTypeDeclaration(this)` (the
// class name does not end in "AstType", so the generator's visit-method-name default yields
// `VisitTypeDeclaration`). The generated slot statics are `AttributesSlot` (a
// `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`, collection),
// `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>` pointing at `Slots.Identifier`, required),
// `TypeParametersSlot` (a `CSharpSlotInfoT<TypeParameterDeclaration>` pointing at
// `Slots.TypeParameter`, collection), `PrimaryConstructorParametersSlot` (a
// `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection),
// `BaseTypesSlot` (a `CSharpSlotInfoT<AstType>` pointing at `Slots.BaseType`, collection),
// `ConstraintsSlot` (a `CSharpSlotInfoT<Constraint>` pointing at `Slots.Constraint`, collection),
// and `MembersSlot` (a `CSharpSlotInfoT<EntityDeclaration>` pointing at `Slots.TypeMember`,
// collection). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
// `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the derived
// `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the public surface,
// the `DestructorDeclaration` D272 / `MethodDeclaration` D284 precedent), the `ClassType` and
// `HasPrimaryConstructor` scalars copied directly to the backing fields (the
// `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.OperatorType` D280
// scalar-copy precedent -- the fields are private to this class, so accessible from the derived
// `Clone`), the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`, the D223
// concrete-clone pattern), the `NameToken` deep-cloned through its setter when present (which
// re-parents; `Identifier::Clone()` returns `Identifier*` which `NameToken(Identifier*)` accepts
// directly), and every `Attributes`/`TypeParameters`/`PrimaryConstructorParameters`/`BaseTypes`/
// `Constraints`/`Members` element deep-cloned through `Add` (which re-parents and re-indexes;
// `AttributeSection::Clone()` returns `AttributeSection*`,
// `TypeParameterDeclaration::Clone()` returns `TypeParameterDeclaration*`,
// `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`, `AstType::Clone()` returns
// `AstType*`, `Constraint::Clone()` returns `Constraint*` -- the covariant concrete types `Add`
// accepts directly; the `Members` element type is the abstract `EntityDeclaration` base which
// redeclares NO typed `Clone`, so each `Members` element is cloned via
// `static_cast<EntityDeclaration*>(elem->Clone())`, the `ExtensionDeclaration` D285 precedent).
// No own location fields (does not derive `EndLocation`), so the print-time
// `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
// `MethodDeclaration` D284 / `DelegateDeclaration` D291 no-location-copy precedent). The
// covariant return is `TypeDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
// `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written partial;
// the covariant `TypeDeclaration*` is a valid override of `AstNode::Clone`).
//
// The `ClassType` name-shadowing crux (the `DirectionExpression.FieldDirection` D235 /
// `OperatorDeclaration.OperatorType` D280 precedent applied to a `ClassType`-of-type-`ClassType`
// scalar): the `ClassType()` getter (a member function) shadows the `ClassType` `enum class` in
// this class scope (C++ unqualified name lookup finds the member and stops, even though it is not
// a type), so the setter parameter type uses the elaborated enum specifier `enum ClassType`, the
// backing-field type uses the elaborated `enum ClassType`, the backing-field initializer uses the
// fully-qualified `::ILSpy::...::ClassType::Class` (the elaborated specifier cannot apply in a
// qualified-name position), and the `Clone` copies the scalar directly to the backing field (not
// the ambiguous `ClassType(classType)` setter call that could parse as a functional cast of the
// enum). The `HasPrimaryConstructor()` accessor does NOT share any type's name (a `bool` is not a
// class/enum), so no elaborated specifier is needed there -- the plain `bool` resolves. NO other
// name-shadowing crux (no member is named `AttributeSection`/`Identifier`/
// `TypeParameterDeclaration`/`ParameterDeclaration`/`AstType`/`Constraint`/`EntityDeclaration` --
// the `Attributes()`/`NameToken()`/`TypeParameters()`/`PrimaryConstructorParameters()`/
// `BaseTypes()`/`Constraints()`/`Members()` accessors do not collide with any class in the
// `Syntax` namespace), so no elaborated-type-specifier is needed for the slot types; the plain
// element types resolve to the classes.
//
// NO new `Slots` constant this iteration: all seven slot kinds (`Slots::AttributeSection` D241,
// `Slots::Identifier` D237, `Slots::TypeParameter` D284, `Slots::Parameter` D279, `Slots::BaseType`
// D283, `Slots::Constraint` D284, `Slots::TypeMember` D285) are already ported. This is the
// `DelegateDeclaration` D291 four-collection shape generalized with two more collections
// (`PrimaryConstructorParameters`/`Members`) inserted and the `ReturnType` single removed, plus
// two scalars (`ClassType`/`HasPrimaryConstructor`) interleaved -- the first ported node with SIX
// collections, and the first to interleave two non-`[Slot]` scalars (an enum and a bool) around the
// `[Slot]` collections in `MembersToMatch`.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_TYPEDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_TYPEDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/TypeParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Constraint.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <stdexcept>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public enum ClassType` -- the six C# type kinds (`class`/`struct`/`interface`/`enum`,
// plus the C# 9 `record class` and C# 10 `record struct`). Declares NO `Any` member, so the
// generator's `hasAny` path does NOT fire and the generated `DoMatch` term is the plain
// `this.ClassType == o.ClassType` (no `Any`-wildcard). `Class` is the zero value (the C# default
// for an uninitialized `ClassType` property). A plain `enum class` (the C# `enum ClassType`
// defaults to `int`; the underlying width is not load-bearing here, so the default `int`-sized
// `enum class` is faithful, the `OperatorType` D280 precedent).
enum class ClassType {
    Class,
    Struct,
    Interface,
    Enum,
    RecordClass,
    RecordStruct
};

// The C# `public sealed partial class TypeDeclaration : EntityDeclaration`. `final` (the C#
// `sealed`): no further derivation. The central type-declaration node, an `EntityDeclaration`
// with SIX collections (`Attributes`/`TypeParameters`/`PrimaryConstructorParameters`/`BaseTypes`/
// `Constraints`/`Members`) plus one single (`NameToken`) plus two scalars (`ClassType`/
// `HasPrimaryConstructor`) -- the `DelegateDeclaration` D291 four-collection shape with two more
// collections inserted, the `ReturnType` single removed, and two scalars interleaved.
class TypeDeclaration final : public EntityDeclaration {
public:
    ~TypeDeclaration() override = default;

    // The generated empty ctor (the C# `public TypeDeclaration()`). The six collections are
    // members (the D222 always-present-stack-member design), initialized here with their slot
    // indices as `baseIndex` (`Attributes` at 0, `TypeParameters` at 2,
    // `PrimaryConstructorParameters` at 3, `BaseTypes` at 4, `Constraints` at 5, `Members` at 6 --
    // the generator passes the slot index, not the dynamic flattened index; unused on the
    // non-incremental fast path) and `supportsIncremental = false` for all six (the node has six
    // collections, so none is incremental -- `collectionCount == 1 && slotIndex == slots.Count - 1`
    // is false for every collection -- an element's flattened `ChildIndex` is dynamic, rebuilt
    // lazily by `EnsureChildIndices` after a mutation, the `MethodDeclaration` D284 /
    // `DelegateDeclaration` D291 four-collection precedent generalized to six). The `NameToken`
    // single defaults to null (a REQUIRED slot -- `CheckInvariant` rejects an empty node, the
    // `UnaryOperatorExpression` D231 / `MethodDeclaration` D284 precedent). `ClassType` defaults to
    // `Class` (the enum's zero value, the C# default); `HasPrimaryConstructor` defaults to false.
    TypeDeclaration() : attributes_(this, &AttributesSlot, 0, false),
                        typeParameters_(this, &TypeParametersSlot, 2, false),
                        primaryConstructorParameters_(this, &PrimaryConstructorParametersSlot, 3, false),
                        baseTypes_(this, &BaseTypesSlot, 4, false),
                        constraints_(this, &ConstraintsSlot, 5, false),
                        members_(this, &MembersSlot, 6, false) {}

    // The C# `public override SymbolKind SymbolKind { get { return SymbolKind.TypeDefinition; } }`
    // -- the kind of member this declaration is (a type definition). Overrides the base abstract
    // `SymbolKind` (the `EntityDeclaration` pure-virtual). The qualified `SymbolKind::TypeDefinition`
    // avoids a `using` (the enum lives in `ILSpy::Decompiler::TypeSystem` -- the
    // `ExtensionDeclaration` D285 / `DelegateDeclaration` D291 precedent).
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override {
        return ILSpy::Decompiler::TypeSystem::SymbolKind::TypeDefinition;
    }

    // ---- The `ClassType` scalar (a settable enum, NOT a `[Slot]`) ----------------------------
    // The C# `public ClassType ClassType { get; set; }` -- a scalar enum (not a `[Slot]`), the
    // kind of type (`Class`/`Struct`/`Interface`/`Enum`/`RecordClass`/`RecordStruct`). A settable
    // enum-typed scalar the generator adds to `MembersToMatch` (with the PLAIN equality term, since
    // `ClassType` has no `Any` member -- the `DirectionExpression.FieldDirection` D235 /
    // `OperatorDeclaration.OperatorType` D280 no-`Any`-enum fall-through) and to the ctor params
    // (the `VariableDeclarationStatement.Modifiers` D270 settable-enum-ctor-param precedent). The
    // return type precedes the getter's own declaration, so the plain `ClassType` (the enum) is
    // unshadowed in the getter signature.
    ClassType ClassType() const { return classType_; }
    // The setter parameter type uses the elaborated enum specifier `enum ClassType`: the
    // `ClassType()` getter declared just above shadows the `ClassType` `enum class` in this class
    // scope (the D235 `FieldDirection` / D280 `OperatorType` precedent -- the enum equivalent of
    // the `class Expression` elaborated specifier), so the plain name would resolve to the member
    // function (not a type).
    void ClassType(enum ClassType value) { classType_ = value; }

    // ---- The `Attributes` collection slot (override of the base virtual) -------------------
    // The generated `[Slot("AttributeSection")] public override partial AstNodeCollection<
    // AttributeSection> Attributes` -- the attribute sections on the type (a
    // `CSharpSlotInfoT<AttributeSection>` slot at slot 0, non-incremental -- the node's first of
    // six collections). The C# lazily allocates the wrapper; the D222 port makes the collection an
    // always-present stack member, so the accessor returns the member directly. Overrides the base
    // `EntityDeclaration::Attributes` (the base body returns a detached empty via `GetChildren`;
    // this override returns the real `attributes_` member). A `const` convenience overload
    // returns `const&` for a `const TypeDeclaration*`.
    AstNodeCollectionT<AttributeSection>& Attributes() override { return attributes_; }
    const AstNodeCollectionT<AttributeSection>& Attributes() const { return attributes_; }

    // ---- The `NameToken` slot (override of the base virtual) ------------------------------
    // The generated `[Slot("Identifier")] public override partial Identifier NameToken` -- a
    // single REQUIRED `Identifier` slot at slot 1 (the type's name token; non-nullable, so
    // required -- `IsOptional` is false). The slot FOLLOWS the `Attributes` collection, so the
    // property setter uses the INDEX-LESS `SetChildNode(ref field, value)` (the dynamic flattened
    // index after a collection). NOT `[ExcludeFromMatch]` (unlike `ConstructorDeclaration` D281 /
    // `DestructorDeclaration` D272), so the `Name` `MatchString` term IS in `DoMatch` (a type's
    // name is a real name the resolver looks up by). Overrides the base `EntityDeclaration::
    // NameToken` (the base body kind-walks for the `Identifier` kind; this override returns the
    // backing field directly, the generated `get => field!`). The inherited base
    // `EntityDeclaration::Name` (NOT overridden) kind-walks for the `Identifier` kind and returns
    // the `NameToken`'s `Name`, so the `DoMatch` `MatchString` term matches the two types' names
    // without a `Name` override. The getter returns the raw pointer (null for a half-constructed
    // node; the C# `!` null-forgiving).
    Identifier* NameToken() const override { return nameToken_; }
    void NameToken(Identifier* value) override {
        SetChildNode(nameToken_, value);
    }

    // ---- The `TypeParameters` collection slot (NOT a base virtual) --------------------------
    // The generated `[Slot("TypeParameter")] public partial AstNodeCollection<TypeParameterDeclaration>
    // TypeParameters` -- the type's generic type parameters (a
    // `CSharpSlotInfoT<TypeParameterDeclaration>` slot at slot 2, the node's SECOND of SIX
    // collections, non-incremental). The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly. NOT
    // a base virtual, so a plain non-virtual accessor. Reuses the cycle-broken `Slots::TypeParameter`
    // kind (added by `MethodDeclaration` D284 into `TypeParameterDeclaration.hpp`). A `const`
    // convenience overload returns `const&` for a `const TypeDeclaration*`.
    AstNodeCollectionT<TypeParameterDeclaration>& TypeParameters() { return typeParameters_; }
    const AstNodeCollectionT<TypeParameterDeclaration>& TypeParameters() const { return typeParameters_; }

    // ---- The `HasPrimaryConstructor` scalar (a settable bool, NOT a `[Slot]`) ----------------
    // The C# `public bool HasPrimaryConstructor { get; set; }` -- a scalar bool (not a `[Slot]`),
    // whether the type has a primary constructor (C# 12). A settable bool the generator adds to
    // `MembersToMatch` (with the PLAIN equality term -- a bool is not an enum, not a string, so the
    // `DoMatchTerm` fall-through emits `this.HasPrimaryConstructor == o.HasPrimaryConstructor`,
    // the `ComposedType.IsDoubleColon` D238 plain-bool precedent) but NOT to the ctor params (the
    // generator adds only settable ENUM-typed scalars to `CtorParams`; a bool is set via the
    // property setter). The `HasPrimaryConstructor()` accessor does NOT share any type's name, so
    // no elaborated specifier is needed.
    bool HasPrimaryConstructor() const { return hasPrimaryConstructor_; }
    void HasPrimaryConstructor(bool value) { hasPrimaryConstructor_ = value; }

    // ---- The `PrimaryConstructorParameters` collection slot (NOT a base virtual) -----------
    // The generated `[Slot("Parameter")] public partial AstNodeCollection<ParameterDeclaration>
    // PrimaryConstructorParameters` -- the type's primary-constructor parameter list (a
    // `CSharpSlotInfoT<ParameterDeclaration>` slot at slot 3, the node's THIRD of SIX
    // collections, non-incremental). The C# lazily allocates the wrapper; the D222 port makes the
    // collection an always-present stack member, so the accessor returns the member directly. NOT
    // a base virtual, so a plain non-virtual accessor. Reuses the cycle-broken `Slots::Parameter`
    // kind (added by `IndexerDeclaration` D279). A `const` convenience overload returns `const&`
    // for a `const TypeDeclaration*`.
    AstNodeCollectionT<ParameterDeclaration>& PrimaryConstructorParameters() { return primaryConstructorParameters_; }
    const AstNodeCollectionT<ParameterDeclaration>& PrimaryConstructorParameters() const { return primaryConstructorParameters_; }

    // ---- The `BaseTypes` collection slot (NOT a base virtual) -------------------------------
    // The generated `[Slot("BaseType")] public partial AstNodeCollection<AstType> BaseTypes` --
    // the type's base type and implemented interfaces (a `CSharpSlotInfoT<AstType>` slot at slot
    // 4, the node's FOURTH of SIX collections, non-incremental). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly. NOT a base virtual, so a plain non-virtual accessor. Reuses
    // `Slots::BaseType` (added by `Constraint` D283). A `const` convenience overload returns
    // `const&` for a `const TypeDeclaration*`.
    AstNodeCollectionT<AstType>& BaseTypes() { return baseTypes_; }
    const AstNodeCollectionT<AstType>& BaseTypes() const { return baseTypes_; }

    // ---- The `Constraints` collection slot (NOT a base virtual) -----------------------------
    // The generated `[Slot("Constraint")] public partial AstNodeCollection<Constraint> Constraints`
    // -- the type's generic `where` clauses (a `CSharpSlotInfoT<Constraint>` slot at slot 5, the
    // node's FIFTH of SIX collections, non-incremental). The C# lazily allocates the wrapper; the
    // D222 port makes the collection an always-present stack member, so the accessor returns the
    // member directly. NOT a base virtual, so a plain non-virtual accessor. Reuses the
    // cycle-broken `Slots::Constraint` kind (added by `MethodDeclaration` D284 into
    // `Constraint.hpp`). A `const` convenience overload returns `const&` for a `const
    // TypeDeclaration*`.
    AstNodeCollectionT<Constraint>& Constraints() { return constraints_; }
    const AstNodeCollectionT<Constraint>& Constraints() const { return constraints_; }

    // ---- The `Members` collection slot (NOT a base virtual) --------------------------------
    // The generated `[Slot("TypeMember")] public partial AstNodeCollection<EntityDeclaration>
    // Members` -- the type's member declarations (a `CSharpSlotInfoT<EntityDeclaration>` slot at
    // slot 6, the node's SIXTH of SIX collections, non-incremental). The C# lazily allocates the
    // wrapper; the D222 port makes the collection an always-present stack member, so the accessor
    // returns the member directly. NOT a base virtual, so a plain non-virtual accessor. Reuses
    // the cycle-broken `Slots::TypeMember` kind (added by `ExtensionDeclaration` D285 into
    // `EntityDeclaration.hpp`). The element type is the ABSTRACT `EntityDeclaration` base, which
    // redeclares NO typed `Clone` (it inherits `AstNode::Clone` returning `AstNode*`), so each
    // `Members` element is cloned via `static_cast<EntityDeclaration*>(elem->Clone())` in `Clone`
    // (the `ExtensionDeclaration` D285 precedent). A `const` convenience overload returns `const&`
    // for a `const TypeDeclaration*`.
    AstNodeCollectionT<EntityDeclaration>& Members() { return members_; }
    const AstNodeCollectionT<EntityDeclaration>& Members() const { return members_; }

    // ---- The per-node slot statics (pointing at the shared `Slots` kinds) ------------------
    // `AttributesSlot` (a `CSharpSlotInfoT<AttributeSection>` pointing at `Slots.AttributeSection`,
    // collection -- the node's first collection); `NameTokenSlot` (a `CSharpSlotInfoT<Identifier>`
    // pointing at `Slots.Identifier`, required); `TypeParametersSlot` (a
    // `CSharpSlotInfoT<TypeParameterDeclaration>` pointing at `Slots.TypeParameter`, collection --
    // the node's second collection); `PrimaryConstructorParametersSlot` (a
    // `CSharpSlotInfoT<ParameterDeclaration>` pointing at `Slots.Parameter`, collection -- the
    // node's third collection); `BaseTypesSlot` (a `CSharpSlotInfoT<AstType>` pointing at
    // `Slots.BaseType`, collection -- the node's fourth collection); `ConstraintsSlot` (a
    // `CSharpSlotInfoT<Constraint>` pointing at `Slots.Constraint`, collection -- the node's fifth
    // collection); `MembersSlot` (a `CSharpSlotInfoT<EntityDeclaration>` pointing at
    // `Slots.TypeMember`, collection -- the node's sixth collection). NO name shadowing (no member
    // is named `AttributeSection`/`Identifier`/`TypeParameterDeclaration`/`ParameterDeclaration`/
    // `AstType`/`Constraint`/`EntityDeclaration`), so the element types are the plain classes.
    static inline const CSharpSlotInfoT<AttributeSection> AttributesSlot{"Attributes", true, &Slots::AttributeSection, true};
    static inline const CSharpSlotInfoT<Identifier> NameTokenSlot{"NameToken", false, &Slots::Identifier, false};
    static inline const CSharpSlotInfoT<TypeParameterDeclaration> TypeParametersSlot{"TypeParameters", true, &Slots::TypeParameter, true};
    static inline const CSharpSlotInfoT<ParameterDeclaration> PrimaryConstructorParametersSlot{"PrimaryConstructorParameters", true, &Slots::Parameter, true};
    static inline const CSharpSlotInfoT<AstType> BaseTypesSlot{"BaseTypes", true, &Slots::BaseType, true};
    static inline const CSharpSlotInfoT<Constraint> ConstraintsSlot{"Constraints", true, &Slots::Constraint, true};
    static inline const CSharpSlotInfoT<EntityDeclaration> MembersSlot{"Members", true, &Slots::TypeMember, true};

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch entry:
    // routes back to `VisitTypeDeclaration`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitTypeDeclaration(this);
    }

    // ---- Slot storage (the generated overrides) -------------------------------------------
    // Seven slots in declaration order: an `Attributes` collection at slot 0 (the contiguous range
    // `[0, attrCount)`), a `NameToken` single at slot 1 (`attrCount`), a `TypeParameters` collection
    // at slot 2 (the contiguous range `[attrCount + 1, attrCount + 1 + tpCount)`), a
    // `PrimaryConstructorParameters` collection at slot 3 (the contiguous range
    // `[attrCount + 1 + tpCount, attrCount + 1 + tpCount + pcpCount)`), a `BaseTypes` collection at
    // slot 4 (the contiguous range
    // `[attrCount + 1 + tpCount + pcpCount, attrCount + 1 + tpCount + pcpCount + btCount)`), a
    // `Constraints` collection at slot 5 (the contiguous range
    // `[attrCount + 1 + tpCount + pcpCount + btCount, attrCount + 1 + tpCount + pcpCount + btCount
    // + cCount)`), and a `Members` collection at slot 6 (the contiguous range
    // `[attrCount + 1 + tpCount + pcpCount + btCount + cCount, attrCount + 1 + tpCount + pcpCount
    // + btCount + cCount + mCount)`). `GetChildCount` is `attrCount + tpCount + pcpCount + btCount
    // + cCount + mCount + 1` (the six collections' current lengths plus the one single -- each
    // single slot contributes 1 to the flattened count regardless of whether it is filled);
    // `GetChild`/`SetChild`/`GetChildSlotInfo` walk the slots subtracting each one's width from a
    // running index (the generator's `WriteReturnDispatchWithCollections`/
    // `WriteSetChildWithCollections` shape -- a collection step, a single step, then five
    // collection steps). `GetCollectionByKind` returns the matching collection for each of the six
    // collection kinds. The `ClassType`/`HasPrimaryConstructor` scalars are NOT slots, so they do
    // not appear here. This is the first ported node with SIX collections -- the `DelegateDeclaration`
    // D291 four-collection shape with two more collections (`PrimaryConstructorParameters`/
    // `Members`) inserted and the `ReturnType` single removed.

    int GetChildCount() const override {
        return attributes_.Count() + typeParameters_.Count() + primaryConstructorParameters_.Count()
             + baseTypes_.Count() + constraints_.Count() + members_.Count() + 1;
    }

    AstNode* GetChild(int index) const override {
        int i = index;
        {
            int n = attributes_.Count();
            if (i < n)
                return attributes_.At(i);
            i -= n;
        }
        if (i == 0)
            return nameToken_;
        i--;
        {
            int n = typeParameters_.Count();
            if (i < n)
                return typeParameters_.At(i);
            i -= n;
        }
        {
            int n = primaryConstructorParameters_.Count();
            if (i < n)
                return primaryConstructorParameters_.At(i);
            i -= n;
        }
        {
            int n = baseTypes_.Count();
            if (i < n)
                return baseTypes_.At(i);
            i -= n;
        }
        {
            int n = constraints_.Count();
            if (i < n)
                return constraints_.At(i);
            i -= n;
        }
        {
            int n = members_.Count();
            if (i < n)
                return members_.At(i);
            i -= n;
        }
        throw std::out_of_range("TypeDeclaration::GetChild");
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
            SetChildNode(nameToken_, static_cast<Identifier*>(value), index);
            return;
        }
        i--;
        {
            int n = typeParameters_.Count();
            if (i < n) {
                typeParameters_.SetAt(i, static_cast<TypeParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = primaryConstructorParameters_.Count();
            if (i < n) {
                primaryConstructorParameters_.SetAt(i, static_cast<ParameterDeclaration*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = baseTypes_.Count();
            if (i < n) {
                baseTypes_.SetAt(i, static_cast<AstType*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = constraints_.Count();
            if (i < n) {
                constraints_.SetAt(i, static_cast<Constraint*>(value));
                return;
            }
            i -= n;
        }
        {
            int n = members_.Count();
            if (i < n) {
                members_.SetAt(i, static_cast<EntityDeclaration*>(value));
                return;
            }
            i -= n;
        }
        throw std::out_of_range("TypeDeclaration::SetChild");
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
            return &NameTokenSlot;
        i--;
        {
            int n = typeParameters_.Count();
            if (i < n)
                return &TypeParametersSlot;
            i -= n;
        }
        {
            int n = primaryConstructorParameters_.Count();
            if (i < n)
                return &PrimaryConstructorParametersSlot;
            i -= n;
        }
        {
            int n = baseTypes_.Count();
            if (i < n)
                return &BaseTypesSlot;
            i -= n;
        }
        {
            int n = constraints_.Count();
            if (i < n)
                return &ConstraintsSlot;
            i -= n;
        }
        {
            int n = members_.Count();
            if (i < n)
                return &MembersSlot;
            i -= n;
        }
        throw std::out_of_range("TypeDeclaration::GetChildSlotInfo");
    }

    AstNodeCollection* GetCollectionByKind(const CSharpSlotInfo* kind) override {
        if (kind == &Slots::AttributeSection)
            return &attributes_;
        if (kind == &Slots::TypeParameter)
            return &typeParameters_;
        if (kind == &Slots::Parameter)
            return &primaryConstructorParameters_;
        if (kind == &Slots::BaseType)
            return &baseTypes_;
        if (kind == &Slots::Constraint)
            return &constraints_;
        if (kind == &Slots::TypeMember)
            return &members_;
        return AstNode::GetCollectionByKind(kind);
    }

    // ---- DoMatch (the generated pattern match) ---------------------------------------------
    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is TypeDeclaration o && MatchString(this.Name, o.Name) &&
    // this.MatchAttributesAndModifiers(o, match) && MatchOptional(this.ReturnType, o.ReturnType,
    // match) && this.ClassType == o.ClassType && this.TypeParameters.DoMatch(o.TypeParameters,
    // match) && this.HasPrimaryConstructor == o.HasPrimaryConstructor &&
    // this.PrimaryConstructorParameters.DoMatch(o.PrimaryConstructorParameters, match) &&
    // this.BaseTypes.DoMatch(o.BaseTypes, match) && this.Constraints.DoMatch(o.Constraints, match)
    // && this.Members.DoMatch(o.Members, match)`. The terms are in `MembersToMatch` order (the
    // generator adds `Name`, `MatchAttributesAndModifiers`, and `ReturnType` explicitly for every
    // `EntityDeclaration`-derived node -- `NameToken` is NOT `[ExcludeFromMatch]` on
    // `TypeDeclaration`, so the `Name` `String` term IS added, the `MethodDeclaration` D284
    // precedent; then the per-property scan adds `ClassType`/`TypeParameters`/
    // `HasPrimaryConstructor`/`PrimaryConstructorParameters`/`BaseTypes`/`Constraints`/`Members` in
    // source declaration order). The `Name` term is a `MatchString` over the type's name (the
    // inherited base `Name()` kind-walks for the `Identifier` kind and returns the `NameToken`'s
    // `Name`); the `ReturnType` term is `MatchOptional` (nullable recursive -- the generator treats
    // the `EntityDeclaration` `ReturnType` uniformly as `MatchOptional`; a `TypeDeclaration` has
    // no `ReturnType` slot, so the inherited base `ReturnType()` kind-walk returns null and
    // `MatchOptional(null, null)` is always true, the `DestructorDeclaration` D272 vacuous
    // precedent -- the term calls the inherited base `ReturnType()` virtual, not a backing
    // field); the `ClassType` term is the PLAIN `==` (a no-`Any` enum scalar, the
    // `DirectionExpression` D235 / `OperatorDeclaration.OperatorType` D280 fall-through); the
    // `HasPrimaryConstructor` term is the PLAIN `==` (a non-enum non-string scalar, the
    // `ComposedType.IsDoubleColon` D238 fall-through); the `TypeParameters`/
    // `PrimaryConstructorParameters`/`BaseTypes`/`Constraints`/`Members` terms are each the
    // collection recursive `DoMatch` (the generator emits a collection-typed recursive term
    // directly, NOT `MatchOptional` -- the `FieldDeclaration.Variables` D273 / `MethodDeclaration`
    // D284 precedent). A type-only mismatch (not a `TypeDeclaration`) rejects early. The `Name()`
    // calls are inlined in the `MatchString` arguments (the `MemberType` D238 / `MethodDeclaration`
    // D284 precedent) so the C# `&&` short-circuit is preserved; the `std::string` temporaries
    // live until the end of the full `return` expression, keeping the `std::string_view` views
    // valid for the `MatchString` call. The `ClassType`/`HasPrimaryConstructor` terms compare the
    // backing fields directly (`classType_ == o->classType_` / `hasPrimaryConstructor_ ==
    // o->hasPrimaryConstructor_`), so no type name appears (no elaborated specifier needed there).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        auto* o = dynamic_cast<TypeDeclaration*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(
                   std::optional<std::string_view>(std::string_view(Name())),
                   std::optional<std::string_view>(std::string_view(o->Name())))
            && MatchAttributesAndModifiers(o, match)
            && MatchOptional(ReturnType(), o->ReturnType(), match)
            && classType_ == o->classType_
            && typeParameters_.DoMatch(o->typeParameters_, match)
            && hasPrimaryConstructor_ == o->hasPrimaryConstructor_
            && primaryConstructorParameters_.DoMatch(o->primaryConstructorParameters_, match)
            && baseTypes_.DoMatch(o->baseTypes_, match)
            && constraints_.DoMatch(o->constraints_, match)
            && members_.DoMatch(o->members_, match);
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port overrides
    // it (no `MemberwiseClone`): a fresh node, the `Modifiers` scalar copied via the public
    // `Modifiers()` getter/setter (the base's private `modifiers_` is not accessible from the
    // derived `Clone` -- the C# `MemberwiseClone` copies the private backing; the port uses the
    // public surface, the `DestructorDeclaration` D272 / `MethodDeclaration` D284 precedent), the
    // `ClassType` and `HasPrimaryConstructor` scalars copied directly to the backing fields (the
    // `DirectionExpression.FieldDirection` D235 / `OperatorDeclaration.OperatorType` D280
    // scalar-copy precedent -- the fields are private to this class, so accessible from the
    // derived `Clone`), the annotation channel copied (`CloneAnnotationsFrom` + `ReparentTrivia`,
    // the D223 concrete-clone pattern), the `NameToken` deep-cloned through its setter when
    // present (which re-parents; `Identifier::Clone()` returns `Identifier*` which
    // `NameToken(Identifier*)` accepts directly), and every `Attributes`/`TypeParameters`/
    // `PrimaryConstructorParameters`/`BaseTypes`/`Constraints`/`Members` element deep-cloned
    // through `Add` (which re-parents and re-indexes; `AttributeSection::Clone()` returns
    // `AttributeSection*`, `TypeParameterDeclaration::Clone()` returns
    // `TypeParameterDeclaration*`, `ParameterDeclaration::Clone()` returns `ParameterDeclaration*`,
    // `AstType::Clone()` returns `AstType*`, `Constraint::Clone()` returns `Constraint*` -- the
    // covariant concrete types `Add` accepts directly; the `Members` element type is the abstract
    // `EntityDeclaration` base which redeclares NO typed `Clone`, so each `Members` element is
    // cloned via `static_cast<EntityDeclaration*>(elem->Clone())`, the `ExtensionDeclaration` D285
    // precedent). No own location fields (does not derive `EndLocation`), so the print-time
    // `StartLocation`/`EndLocation` are not copied (the `DestructorDeclaration` D272 /
    // `MethodDeclaration` D284 / `DelegateDeclaration` D291 no-location-copy precedent). The
    // covariant return is `TypeDeclaration*` (through `AstNode*`, the `AstNode::Clone` virtual --
    // `EntityDeclaration` re-declares no typed `Clone`, faithful to its empty hand-written
    // partial; the covariant `TypeDeclaration*` is a valid override of `AstNode::Clone`).
    TypeDeclaration* Clone() const override {
        auto* node = new TypeDeclaration();
        node->Modifiers(Modifiers());
        node->classType_ = classType_;
        node->hasPrimaryConstructor_ = hasPrimaryConstructor_;
        node->CloneAnnotationsFrom(*this);
        if (nameToken_ != nullptr)
            node->NameToken(nameToken_->Clone());
        for (int i = 0; i < attributes_.Count(); i++)
            node->attributes_.Add(attributes_.At(i)->Clone());
        for (int i = 0; i < typeParameters_.Count(); i++)
            node->typeParameters_.Add(typeParameters_.At(i)->Clone());
        for (int i = 0; i < primaryConstructorParameters_.Count(); i++)
            node->primaryConstructorParameters_.Add(primaryConstructorParameters_.At(i)->Clone());
        for (int i = 0; i < baseTypes_.Count(); i++)
            node->baseTypes_.Add(baseTypes_.At(i)->Clone());
        for (int i = 0; i < constraints_.Count(); i++)
            node->constraints_.Add(constraints_.At(i)->Clone());
        for (int i = 0; i < members_.Count(); i++)
            node->members_.Add(static_cast<EntityDeclaration*>(members_.At(i)->Clone()));
        node->ReparentTrivia();
        return node;
    }

private:
    // The backing fields. `attributes_`/`typeParameters_`/`primaryConstructorParameters_`/
    // `baseTypes_`/`constraints_`/`members_` are the always-present collection members (empty
    // until the first `Add`, all non-incremental -- the node has six collections); `nameToken_` is
    // null until set (a REQUIRED slot -- `CheckInvariant` asserts it is filled). The `classType_`
    // scalar uses the elaborated `enum ClassType` specifier and the fully-qualified initializer
    // (the `ClassType()` accessor declared above shadows the `ClassType` `enum class` in this
    // scope -- the D235/D280 precedent; the elaborated specifier cannot apply in a qualified-name
    // position). `hasPrimaryConstructor_` is a plain `bool` (no shadowing). NO other name
    // shadowing (no member is named `AttributeSection`/`Identifier`/`TypeParameterDeclaration`/
    // `ParameterDeclaration`/`AstType`/`Constraint`/`EntityDeclaration`), so the collection field
    // types are the plain classes.
    AstNodeCollectionT<AttributeSection> attributes_;
    Identifier* nameToken_ = nullptr;
    AstNodeCollectionT<TypeParameterDeclaration> typeParameters_;
    AstNodeCollectionT<ParameterDeclaration> primaryConstructorParameters_;
    AstNodeCollectionT<AstType> baseTypes_;
    AstNodeCollectionT<Constraint> constraints_;
    AstNodeCollectionT<EntityDeclaration> members_;
    enum ClassType classType_ = ::ILSpy::Decompiler::CSharp::Syntax::ClassType::Class;
    bool hasPrimaryConstructor_ = false;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_TYPEDECLARATION_HPP
