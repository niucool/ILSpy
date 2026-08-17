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
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Port of the `Identifier` token node in ICSharpCode.Decompiler/CSharp/Syntax/Identifier.cs
// (the generated `Identifier.g.cs` + the hand-written partial). The first concrete C# AST
// node that derives directly from `AstNode` (not through `Expression`) -- a leaf token with
// no `[Slot]` children, carrying a `Name` string and an `IsVerbatim` flag. It is the token
// that backs the string-name `[Slot]` accessors the slot-bearing nodes
// (`IdentifierExpression`/`MemberReferenceExpression`/`SimpleType`/...) declare, so it
// lands ahead of them; it is also the first node that exercises the generated `DoMatch`
// over a scalar string member (a `MatchString` on `Name`, with `IsVerbatim` excluded via
// `[ExcludeFromMatch]`).
//
// Per PORT_PLAN.md section 5.2 / decision D1 the concrete nodes are hand-translated from the
// generated output rather than regenerated. The generated `AcceptVisitor` calls
// `visitor.VisitIdentifier(this)`; the generated `DoMatch` is
// `return other is Identifier o && MatchString(this.Name, o.Name)` (a type check plus a
// `MatchString` -- `Name` is a non-`[Slot]` public string property, so the generator adds it
// to `MembersToMatch` as a `String` term; `IsVerbatim` carries `[ExcludeFromMatch]` so it
// is skipped; `StartLocation`/`EndLocation` are `TextLocation`-typed and skipped by the
// generator). `Clone` is inherited in C# (`MemberwiseClone` + `CloneChildrenInto`); the port
// overrides it (no `MemberwiseClone`): copies `Name`/`IsVerbatim`/`StartLocation` and the
// annotation channel, with no children to deep-copy.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_IDENTIFIER_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_IDENTIFIER_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/IAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/TextLocation.hpp"

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class Identifier : AstNode`. `final` (the C# `sealed`):
// no further derivation. A leaf token: no `[Slot]` children, so the inherited zero-child
// `GetChildCount`/`GetChild`/`SetChild`/`GetChildSlotInfo` defaults apply (the
// NullReferenceExpression precedent).
class Identifier final : public AstNode {
public:
    ~Identifier() override = default;

    // The C# `Identifier()` -- the parameterless ctor sets `name = string.Empty`. The
    // start location is the inherited default (`TextLocation::Empty`).
    Identifier() = default;

    // The C# `public string Name` -- non-null; the C# setter throws `ArgumentNullException`
    // on null. A `std::string` is never null (an empty name is the "no name" the empty
    // `Identifier` carries), so the null-throw has no C++ equivalent; the setter just assigns.
    const std::string& Name() const { return name_; }
    void Name(std::string value) { name_ = std::move(value); }

    // The C# `[ExcludeFromMatch] public bool IsVerbatim`. The `@`-escaping is a lexical
    // detail, not structural, so the generator excludes it from `DoMatch` (a verbatim and a
    // non-verbatim identifier with the same name match).
    bool IsVerbatim() const { return isVerbatim_; }
    void IsVerbatim(bool value) { isVerbatim_ = value; }

    // The C# `internal void SetStartLocation(TextLocation value)` -- the construction-time
    // start setter (the `Create` factories use it). The C# declares its own `startLocation`
    // field and overrides `StartLocation`; this port stores the start in the inherited
    // `startLocation_` field (set via `StorePrintStart`), so the base `StartLocation()`
    // returns it without an override -- the C# override is purely because it has its own
    // field, the semantics are identical.
    void SetStartLocation(TextLocation value) { StorePrintStart(value); }

    // The C# `public override TextLocation EndLocation` -- spans `Name.Length +
    // (IsVerbatim ? 1 : 0)` columns from `StartLocation` (the `@`-prefix adds one column).
    // `Name ?? ""` is just `Name` (a `std::string` is never null; the empty `Identifier` has
    // an empty name, so `EndLocation == StartLocation`).
    TextLocation EndLocation() const override {
        return TextLocation(StartLocation().Line,
                            StartLocation().Column + static_cast<int>(name_.size())
                                + (isVerbatim_ ? 1 : 0));
    }

    // The C# `public override void AcceptVisitor(IAstVisitor visitor)` -- the dispatch
    // entry: routes back to `VisitIdentifier`.
    void AcceptVisitor(IAstVisitor& visitor) override {
        visitor.VisitIdentifier(this);
    }

    // The generated `protected internal override bool DoMatch(AstNode? other, Match match)`:
    // `return other is Identifier o && MatchString(this.Name, o.Name)`. A type match plus a
    // `MatchString` on `Name` -- the `$any$` wildcard (`Pattern::AnyString`) in the pattern's
    // `Name` matches any candidate name. `IsVerbatim` is `[ExcludeFromMatch]`, so it is not
    // compared; `match` is unused (no captures, no recursive child match).
protected:
    bool DoMatch(AstNode* other, PatternMatching::Match match) override {
        (void)match;
        auto* o = dynamic_cast<Identifier*>(other);
        if (o == nullptr)
            return false;
        return PatternMatching::Pattern::MatchString(std::string_view(name_),
                                                     std::string_view(o->name_));
    }

public:
    // The C# `Clone` is inherited (`MemberwiseClone` + `CloneChildrenInto`); this port
    // overrides it (no `MemberwiseClone`): a fresh node with the same `Name`, `IsVerbatim`,
    // and `StartLocation`, plus the annotation channel copied (`CloneAnnotationsFrom` +
    // `ReparentTrivia`, the D223 concrete-clone pattern). No children to deep-copy.
    Identifier* Clone() const override {
        auto* node = new Identifier();
        node->name_ = name_;
        node->isVerbatim_ = isVerbatim_;
        node->StorePrintStart(StartLocation());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }

    // ---- The `Create` factories (the C# `public static Identifier Create(...)`) ----

    // The C# `public static Identifier Create(string name)` -> `Create(name, TextLocation.Empty)`.
    static Identifier* Create(std::string name) {
        return Create(std::move(name), TextLocation::Empty);
    }

    // The C# `public static Identifier? CreateIfNotEmpty(string? name)` -- a null/empty
    // name maps to a null token (no name); any other name to an `Identifier`.
    static Identifier* CreateIfNotEmpty(std::string_view name) {
        return name.empty() ? nullptr : Create(std::string(name));
    }

    // The C# `public static Identifier Create(string name, TextLocation location)`: an
    // empty name -> an empty `Identifier` at `location`; an `@`-prefixed name -> verbatim,
    // with the `@` stripped and the start advanced one column; otherwise the name as-is at
    // `location`.
    static Identifier* Create(std::string name, TextLocation location) {
        if (name.empty())
            return new Identifier(std::string(), location);
        if (name[0] == '@') {
            auto* id = new Identifier(name.substr(1),
                                      TextLocation(location.Line, location.Column + 1));
            id->isVerbatim_ = true;
            return id;
        }
        return new Identifier(std::move(name), location);
    }

    // The C# `public static Identifier Create(string name, TextLocation location, bool
    // isVerbatim)`: an empty name -> an empty `Identifier` at `location`; otherwise the
    // name with the explicit verbatim flag.
    static Identifier* Create(std::string name, TextLocation location, bool isVerbatim) {
        if (name.empty())
            return new Identifier(std::string(), location);
        auto* id = new Identifier(std::move(name), location);
        id->isVerbatim_ = isVerbatim;
        return id;
    }

private:
    std::string name_;
    bool isVerbatim_ = false;

    // The C# `private Identifier(string name, TextLocation location)` -- the canonical ctor
    // used by the `Create` factories. The name is non-null (the `Create` factories
    // guarantee it; an empty name is allowed and yields the empty `Identifier`).
    Identifier(std::string name, TextLocation location) : name_(std::move(name)) {
        StorePrintStart(location);
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_IDENTIFIER_HPP
