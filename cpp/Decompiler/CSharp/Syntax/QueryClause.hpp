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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of the `QueryClause` abstract base in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/QueryExpression.cs (the generated
// `QueryClause.g.cs` + the hand-written partial -- the partial declares only the bare
// `public abstract class QueryClause : AstNode {}`). The next in-order Phase-5 piece per the
// D309 plan ("QueryExpression -- the QueryClause abstract base + the 8 query-clause concrete
// family + the QueryOrdering node + the QueryOrderingDirection enum"). `query_clause` is the
// common base of every clause a `QueryExpression.Clauses` collection holds
// (`query_continuation`/`from_clause`/`let_clause`/`where_clause`/`join_clause`/
// `orderby_clause`/`select_clause`/`group_clause`, C# grammar 12.23.1).
//
// The abstract base ports as a thin shell mirroring the `Expression` D226 / `AstType` D236 /
// `Statement` D254 / `VariableDesignation` D264 / `InterpolatedStringContent` D309
// covariant-pure-virtual-`Clone` shells -- the established mechanical port shape for every
// `partial class : AstNode` abstract base. ONE divergence shared with `VariableDesignation`
// D264 / `InterpolatedStringContent` D309: the C# `QueryClause` has NO hand-written
// `new QueryClause Clone()` (its hand-written partial is empty), so a C# caller through a
// `QueryClause` reference gets an `AstNode` back from the inherited `AstNode.Clone()`. The
// C++ port nonetheless re-declares the inherited `AstNode::Clone()` (which returns an
// `AstNode*` and has a throwing base body) as a covariant pure-virtual returning `QueryClause*`,
// for consistency with the sibling abstract-base convention and for the compile-time
// concrete-override enforcement (the pure-virtual forces every concrete `QueryClause`
// (`QueryWhereClause`/`QuerySelectClause`/`QueryOrderClause`/...) to override `Clone` at
// compile time, otherwise the throwing base body would be inherited and only fail at runtime).
// The `[DecompilerAstNode]` attribute is absent on the C# `QueryClause` (it is a hand-written
// abstract class, not a generated node), so `hasPatternPlaceholder` is moot (no placeholder is
// ever generated for it). An abstract base gets NO `Visit` method on `IAstVisitor` and NO
// `AcceptVisitor` override (`NeedsVisitor = !IsAbstract && base.IsAbstract` is false for an
// abstract base), so this header adds nothing to the visitor dispatch; the concrete clauses
// plug into it. This header does NOT include `Slots.hpp` (the abstract base has no per-node
// slot statics), so the `Slots::Clause` collection kind (a `CSharpSlotInfoT<QueryClause>`)
// lives in `Slots.hpp` with a `QueryClause.hpp` include (no include cycle -- the
// `Slots.Statement`/`Slots.ArraySpecifier`/`Slots.Content` precedent applied to a `QueryClause`
// abstract-base element type).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYCLAUSE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYCLAUSE_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract class QueryClause : AstNode`. Abstract: a concrete clause overrides
// at least `DoMatch`, `AcceptVisitor`, and `Clone`. Derives from `AstNode` (the annotation
// channel + the pattern-match interface + the slot-storage contract); `AstNode` is polymorphic,
// so `QueryClause` is too (the `dynamic_cast`-based is-a tests the slot system and the
// annotation channel use stay valid). The hierarchy is disjoint from `Expression`,
// `Statement`, and `AstType` (a `QueryClause` is an `AstNode` but NOT an `Expression` nor a
// `Statement` nor an `AstType`).
class QueryClause : public AstNode {
public:
    ~QueryClause() override = default;
    QueryClause() = default;
    QueryClause(const QueryClause&) = delete;
    QueryClause& operator=(const QueryClause&) = delete;

    // A typed clone returning a `QueryClause*` instead of an `AstNode*`. The C# `QueryClause`
    // has NO hand-written `new QueryClause Clone()` (its hand-written partial is empty), so
    // this re-declaration is a C++ port addition for consistency with the
    // `Expression`/`AstType`/`Statement`/`VariableDesignation`/`InterpolatedStringContent`
    // convention and for the compile-time concrete-override enforcement (the pure-virtual).
    // The base `AstNode::Clone()` has a throwing body (C++ has no `MemberwiseClone`); this
    // re-declaration makes it pure, so the throwing body is unreachable through the
    // `QueryClause` hierarchy, and a call through a `QueryClause*` is typed (a concrete
    // clause returns its own covariant concrete type, e.g. `QueryWhereClause*`). Covariant:
    // `QueryClause*` derives from `AstNode*`, so this is a valid override of
    // `AstNode::Clone()`.
    QueryClause* Clone() const override = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_QUERYCLAUSE_HPP
