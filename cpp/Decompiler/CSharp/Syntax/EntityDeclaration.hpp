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

// Port of the `EntityDeclaration` abstract base in
// ICSharpCode.Decompiler/CSharp/Syntax/TypeMembers/EntityDeclaration.cs -- the common base of
// every type-member declaration node (`MethodDeclaration`/`FieldDeclaration`/`PropertyDeclaration`/
// `ConstructorDeclaration`/`DestructorDeclaration`/...), the start of the TypeMember hierarchy.
// The next in-order Phase-5 piece per the D271 plan ("the EntityDeclaration abstract base (the
// TypeMember base, whose abstract `SymbolKind` property consumes `SymbolKind` and whose
// `Name`/`NameToken`/`ReturnType` virtuals consume `GetChildByKind`/`SetChildByKind`, plus the
// deferred `GetChildren<T>` and the virtual `Attributes`)").
//
// `EntityDeclaration` is an abstract `partial class : AstNode` (the C# `abstract`, otherwise the
// hand-written partial carries the virtual surface every member declaration shares). It is NOT a
// `Statement`/`Expression`/`AstType` -- a type member is a structural container (a declaration in a
// type body), so it derives DIRECTLY from `AstNode` (the `VariableInitializer` D266 precedent for
// a direct-`AstNode`-rooted `TypeMembers` node). The hand-written partial declares:
//   * the abstract `SymbolKind SymbolKind { get; }` -- each concrete declaration overrides it to
//     report which kind of member it is (`MethodDeclaration` returns `SymbolKind.Method`,
//     `FieldDeclaration` `Field`, `DestructorDeclaration` `Destructor`, ...); the `SymbolKind` enum
//     is the D271 port at `cpp/Decompiler/TypeSystem/SymbolKind.hpp`;
//   * the virtual `AstNodeCollection<AttributeSection> Attributes { get { return base.GetChildren(
//     Slots.AttributeSection); } }` -- the attribute sections on the declaration; the base body
//     returns `GetChildren(Slots.AttributeSection)` (a detached empty for a base that declares no
//     `Attributes` collection slot), and every concrete `EntityDeclaration` overrides it to return
//     its real `Attributes` collection, so the base body is dead for real nodes (the D271 note:
//     "the EntityDeclaration base's virtual Attributes (the only GetChildren consumer) is
//     overridden by every concrete EntityDeclaration, so the base body is dead for real nodes");
//   * the `Modifiers Modifiers { get; set; }` scalar -- a plain settable `Modifiers`-typed property
//     (no `[Slot]`), the member-modifier bitmask (`VariableDeclarationStatement` D270 already ported
//     the `[Flags]` `Modifiers` enum); plus the `bool HasModifier(Modifiers mod)` helper;
//   * the virtual `string Name`/`Identifier NameToken`/`AstType ReturnType` -- convenience accessors
//     over the `Identifier`/`Type` slot positions (read/written by canonical `Slots` kind via
//     `GetChild`/`SetChild`, the D271 `GetChildByKind`/`SetChildByKind`). `Name` is a convenience
//     string over the `Identifier` token slot; `NameToken` is the `Identifier` slot itself;
//     `ReturnType` is the `AstType` slot (nullable -- a destructor/operator/constructor has no
//     return type, so the slot may be absent). A concrete declaration that declares its own
//     `[Slot("Identifier")]`/`[Slot("Type")]` overrides the matching virtual to use its backing
//     field directly (the generator emits `get => field!`); a concrete declaration that does not
//     (e.g. `DestructorDeclaration` has no `ReturnType` slot) inherits the base kind-walk, which
//     returns null for a kind the node declares no slot of;
//   * the `protected bool MatchAttributesAndModifiers(EntityDeclaration o, Match match)` helper --
//     the generated `DoMatch` of every concrete `EntityDeclaration` calls this to match the
//     `Modifiers` scalar (the `Any`-wildcard) and the `Attributes` collection together (the
//     generator adds a `MatchAttributesAndModifiers` term to `MembersToMatch` for every
//     `EntityDeclaration`-derived node, see `DecompilerSyntaxTreeGenerator.cs` line 105).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete nodes are hand-translated from the
// generated output rather than regenerated. The abstract base gets NO `IAstVisitor` `Visit`
// method (the generator's `NeedsVisitor = !IsAbstract && base.IsAbstract` is false for an abstract
// base -- the `Statement` D254 / `AstType` D236 precedent), so this header does not touch
// `IAstVisitor.hpp`/`DepthFirstAstVisitor.hpp`; the concrete `EntityDeclaration` subclasses each add
// their own `Visit<NodeName>` as they land (`DestructorDeclaration` next).
//
// The `Modifiers` name-shadowing crux (the D270 `VariableDeclarationStatement` precedent applied
// to the base): the `Modifiers()` getter (a member function) shadows the `Modifiers` `enum class`
// in this class scope (C++ unqualified name lookup finds the member and stops, even though it is
// not a type), so the setter parameter type, the `HasModifier` parameter, and the backing-field
// declaration use the elaborated `enum Modifiers` specifier (in type positions), and the
// `MatchAttributesAndModifiers` `Any`-wildcard term and the backing-field initializer use the
// fully-qualified `::ILSpy::...::Modifiers::Any`/`::None` (in qualified-name positions where the
// elaborated specifier cannot apply) -- the D235 `DirectionExpression.FieldDirection` precedent.
// The `Name()`/`NameToken()`/`ReturnType()` accessors do NOT collide with any class in the
// `Syntax` namespace (no class named `Name`/`NameToken`/`ReturnType`), and the `GetChildByKind<
// Identifier>`/`<AstType>` template arguments are NOT shadowed (no member is named `Identifier` or
// `AstType`), so no elaborated-type-specifier is needed there -- the plain types resolve to the
// classes.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_ENTITYDECLARATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_ENTITYDECLARATION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <string>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract partial class EntityDeclaration : AstNode`. Abstract: a concrete member
// declaration overrides at least `SymbolKind`, `DoMatch`, `AcceptVisitor`, and `Clone`. Derives
// directly from `AstNode` (a type member is a structural container, not a `Statement`/
// `Expression`/`AstType`). The base carries the shared virtual surface every member declaration
// reuses (`SymbolKind`/`Attributes`/`Modifiers`/`Name`/`NameToken`/`ReturnType`).
class EntityDeclaration : public AstNode {
public:
    ~EntityDeclaration() override = default;
    EntityDeclaration() = default;
    EntityDeclaration(const EntityDeclaration&) = delete;
    EntityDeclaration& operator=(const EntityDeclaration&) = delete;

    // The C# `public abstract SymbolKind SymbolKind { get; }` -- each concrete declaration
    // overrides this to report which kind of member it is. The `SymbolKind` enum lives in the
    // `ILSpy::Decompiler::TypeSystem` namespace (the D271 port); the qualified return type avoids a
    // `using` (no namespace pollution). A pure-virtual: a concrete `EntityDeclaration` must
    // override it.
    virtual ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const = 0;

    // The C# `public virtual AstNodeCollection<AttributeSection> Attributes { get { return
    // base.GetChildren(Slots.AttributeSection); } }` -- the attribute sections on this declaration.
    // The base body returns `GetChildren(&Slots::AttributeSection)` (the D271 `GetChildren<T>`:
    // the real `Attributes` collection for a node that declares one, or a detached empty for one
    // that does not). Every concrete `EntityDeclaration` overrides this to return its real
    // `Attributes` collection (`get => field ??= new AstNodeCollection<...>(...)` in the generated
    // code, `return attributes_;` in the port), so the base body is dead for real nodes (the D271
    // note); it is faithful to the C# and lands `GetChildren<T>` here as the D271 plan prescribed.
    // Returns `AstNodeCollectionT<AttributeSection>&` (by reference) so a concrete override can
    // covariantly return the same type (the concrete `Attributes` returns its `attributes_` member
    // by reference). Non-const (the base body calls the non-const `GetChildren`); a concrete
    // `EntityDeclaration` adds a `const` convenience overload returning `const&` if needed.
    virtual AstNodeCollectionT<AttributeSection>& Attributes() {
        return GetChildren(&Slots::AttributeSection);
    }

    // ---- The `Modifiers` scalar (a settable `[Flags]` enum, NOT a `[Slot]`) -----------------
    // The C# `public Modifiers Modifiers { get; set; }` -- a plain settable `Modifiers`-typed
    // property (no `[Slot]` attribute); the generator adds it to `MembersToMatch` (via the
    // `MatchAttributesAndModifiers` helper) and to the ctor params of every concrete
    // `EntityDeclaration`. NOT virtual in C# (the concrete declarations do not override it), so
    // the port keeps it non-virtual. The return type precedes the `Modifiers()` getter's own
    // declaration, so the plain `Modifiers` (the enum) is unshadowed in the getter signature.
    Modifiers Modifiers() const { return modifiers_; }
    // The setter parameter type uses the elaborated enum specifier `enum Modifiers`: the
    // `Modifiers()` getter declared just above shadows the `Modifiers` `enum class` in this class
    // scope (the D270 precedent -- the enum equivalent of the `class Expression` elaborated
    // specifier), so the plain name would resolve to the member function (not a type).
    void Modifiers(enum Modifiers value) { modifiers_ = value; }

    // The C# `public bool HasModifier(Modifiers mod)` -- test whether all of `mod`'s bits are set
    // (a bitmask test, NOT the pattern-match `==`; the `[Flags]` value type's core semantics). The
    // parameter uses the elaborated `enum Modifiers` for the same shadowing reason as the setter.
    bool HasModifier(enum Modifiers mod) { return (modifiers_ & mod) == mod; }

    // ---- The virtual `Name`/`NameToken`/`ReturnType` (convenience accessors by `Slots` kind) -
    // The C# `public virtual string Name { get { return GetChild(Slots.Identifier)?.Name ??
    // string.Empty; } set { SetChild(Slots.Identifier, Identifier.Create(value, TextLocation.
    // Empty)); } }` -- a convenience string over the `Identifier` slot (the declaration's name).
    // The base kind-walks the slot storage for the `Identifier` kind (the D271 `GetChildByKind`/
    // `SetChildByKind`) and returns the token's `Name` (empty when the token is absent). A
    // concrete declaration may override this; the generator's `MembersToMatch` adds a `Name` term
    // for every `EntityDeclaration`-derived node UNLESS its `NameToken` is `[ExcludeFromMatch]`
    // (e.g. `DestructorDeclaration`, whose name is just the declaring type name).
    virtual std::string Name() const {
        Identifier* tok = GetChildByKind<Identifier>(&Slots::Identifier);
        return tok != nullptr ? tok->Name() : std::string();
    }
    virtual void Name(std::string_view value) {
        SetChildByKind<Identifier>(&Slots::Identifier,
            Identifier::Create(std::string(value), TextLocation::Empty));
    }

    // The C# `public virtual Identifier NameToken { get { return GetChild(Slots.Identifier)!; }
    // set { SetChild(Slots.Identifier, value); } }` -- the `Identifier` token slot itself (the
    // declaration's name token). The base kind-walks for the `Identifier` kind and returns it
    // null-forgiving (the C# `!`; the port returns the raw pointer, null for a half-constructed
    // node). A concrete declaration that declares its own `[Slot("Identifier")] Identifier
    // NameToken` overrides this to return its backing field directly (the generated
    // `get => field!`).
    virtual Identifier* NameToken() const {
        return GetChildByKind<Identifier>(&Slots::Identifier);
    }
    virtual void NameToken(Identifier* value) {
        SetChildByKind<Identifier>(&Slots::Identifier, value);
    }

    // The C# `public virtual AstType ReturnType { get { return GetChild(Slots.Type)!; } set {
    // SetChild(Slots.Type, value); } }` -- the `AstType` slot (the declaration's return type;
    // nullable -- a destructor/operator/constructor has no return type, so the slot is absent). The
    // base kind-walks for the `Type` kind and returns it (null when the node declares no `Type`
    // slot, e.g. a `DestructorDeclaration`). A concrete declaration that declares its own
    // `[Slot("Type")] AstType ReturnType` overrides this to return its backing field.
    virtual AstType* ReturnType() const {
        return GetChildByKind<AstType>(&Slots::Type);
    }
    virtual void ReturnType(AstType* value) {
        SetChildByKind<AstType>(&Slots::Type, value);
    }

protected:
    // The C# `protected bool MatchAttributesAndModifiers(EntityDeclaration o, Match match)` --
    // the generated `DoMatch` of every concrete `EntityDeclaration` calls this to match the
    // `Modifiers` scalar (the `Any`-wildcard -- the generator detects the `Any` member by name,
    // NOT via `[Flags]`, so the term is the plain `==` value equality, NOT a bitmask test; `Any`
    // matches any candidate, a real modifier matches only the exact same modifier) AND the
    // `Attributes` collection (the collection recursive `DoMatch`). The `this.Attributes`/
    // `o.Attributes` calls dispatch through the virtual (the concrete override returns the real
    // collection). Protected (the C# is `protected`; the concrete node's `DoMatch` calls it).
    // `o` is `EntityDeclaration*` (same class, so the private `modifiers_` access is fine).
    bool MatchAttributesAndModifiers(EntityDeclaration* o, PatternMatching::Match match) {
        return (modifiers_ ==
                    ::ILSpy::Decompiler::CSharp::Syntax::Modifiers::Any
                || modifiers_ == o->modifiers_)
            && Attributes().DoMatch(o->Attributes(), match);
    }

private:
    // The backing field for the `Modifiers` scalar (defaults to `None`, the enum's zero value --
    // a declaration with no modifiers). Uses the elaborated `enum Modifiers` specifier (the
    // `Modifiers()` accessor declared above shadows the `Modifiers` `enum class` in this scope --
    // the D270 precedent) and the fully-qualified `::ILSpy::...::Modifiers::None` (the elaborated
    // specifier cannot apply in a qualified-name position; bare `Modifiers::None` would resolve
    // the unqualified `Modifiers` to the member function and reject `::None` on a non-type).
    enum Modifiers modifiers_ = ::ILSpy::Decompiler::CSharp::Syntax::Modifiers::None;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_ENTITYDECLARATION_HPP
