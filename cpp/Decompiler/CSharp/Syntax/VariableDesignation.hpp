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
// OTHERWISE, ARISING FROM, LOSS OF USE, DATA, OR PROFITS, OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Port of the `VariableDesignation` abstract base in
// ICSharpCode.Decompiler/CSharp/Syntax/VariableDesignation.cs (the generated
// `VariableDesignation.g.cs` + the hand-written partial). The next in-order Phase-5 piece per
// the D263 plan ("ForeachStatement needing the VariableDesignation node") -- the dependency
// `ForeachStatement.VariableDesignation` (`[Slot("VariableDesignation")] VariableDesignation`)
// needs, so the hierarchy lands before the statement.
//
// `VariableDesignation` is the common base of the C# 7 deconstruction designations: the
// `single_variable_designation ::= identifier` (C# grammar 11.2.2, the `SingleVariableDesignation`
// leaf carrying just the name) and the `tuple_designation ::= '(' designations? ')'` (C# grammar
// 11.2.4, the `ParenthesizedVariableDesignation` carrying a nested `VariableDesignation`
// collection). It is an abstract, otherwise empty `partial class : AstNode` carrying the bare
// `[DecompilerAstNode]` attribute (the `hasPatternPlaceholder` default is `false`, verified in
// DecompilerSyntaxTreeGenerator.cs line 862 -- UNLIKE `Expression`/`AstType`/`Statement` which
// carry `[DecompilerAstNode(hasPatternPlaceholder: true)]`); the hand-written part is empty, so
// the abstract base emits NO typed `Clone` and NO pattern placeholder (the generator's
// `NeedsVisitor = !IsAbstract && base.IsAbstract` is `false` since `VariableDesignation` is
// abstract, so it gets NO `AcceptVisitor`/`Visit` override either). The concrete subclasses
// each emit their own `DoMatch`/`AcceptVisitor`/`CloneChildrenInto` and inherit the virtual
// `AstNode.Clone()` (a `MemberwiseClone` + `CloneChildrenInto` in C#).
//
// The abstract base ports as a thin shell mirroring the `Expression` D226 / `AstType` D236 /
// `Statement` D254 covariant-pure-virtual-`Clone` shells -- the established mechanical port
// shape for every `partial class : AstNode` abstract base. ONE divergence from those sibling
// bases: the C# `VariableDesignation` has NO hand-written `new VariableDesignation Clone()`
// (its hand-written partial is empty), so a C# caller through a `VariableDesignation` reference
// gets an `AstNode` back from the inherited `AstNode.Clone()`. The C++ port nonetheless
// re-declares the inherited `AstNode::Clone()` (which returns `AstNode*` and has a throwing
// base body) as a covariant pure-virtual returning `VariableDesignation*`, for two reasons:
// (1) consistency with the `Expression`/`AstType`/`Statement` port convention (the typed
// return the C# `new T Clone()` gives on those bases), and (2) the pure-virtual forces every
// concrete `VariableDesignation` to override `Clone` at compile time (otherwise the throwing
// base body would be inherited and only fail at runtime). The typed return is what the C#
// intends (concrete `VariableDesignation`s are cloneable); the divergence (the C# returns
// `AstNode` through the base) is a C#-convenience gap, not a semantic one, and the covariant
// override is the idiomatic C++ equivalent. The `hasPatternPlaceholder: false` divergence (no
// pattern placeholder) is moot in the port -- the pattern placeholder is deferred for every
// base (it lands with the concrete pattern nodes `AnyNode`/`NamedNode`/... and
// `VisitPatternPlaceholder` on `IAstVisitor`), so neither `VariableDesignation` nor
// `Expression`/`AstType`/`Statement` emit it in the port today.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_VARIABLEDESIGNATION_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_VARIABLEDESIGNATION_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract partial class VariableDesignation : AstNode`. Abstract: a concrete
// designation overrides at least `DoMatch`, `AcceptVisitor`, and `Clone`. Derives from `AstNode`
// (the annotation channel + the pattern-match interface + the slot-storage contract); `AstNode`
// is polymorphic, so `VariableDesignation` is too (the `dynamic_cast`-based is-a tests the slot
// system and the annotation channel use stay valid). The hierarchy is disjoint from both
// `Expression` and `Statement` (a `VariableDesignation` is an `AstNode` but NOT an `Expression`
// nor a `Statement`).
class VariableDesignation : public AstNode {
public:
    ~VariableDesignation() override = default;
    VariableDesignation() = default;
    VariableDesignation(const VariableDesignation&) = delete;
    VariableDesignation& operator=(const VariableDesignation&) = delete;

    // A typed clone returning a `VariableDesignation*` instead of an `AstNode*`. The C#
    // `VariableDesignation` has NO hand-written `new VariableDesignation Clone()` (its
    // hand-written partial is empty), so this re-declaration is a C++ port addition for
    // consistency with the `Expression`/`AstType`/`Statement` convention and for the
    // compile-time concrete-override enforcement (the pure-virtual). The base `AstNode::Clone()`
    // has a throwing body (C++ has no `MemberwiseClone`); this re-declaration makes it pure, so
    // the throwing body is unreachable through the designation hierarchy, and a call through a
    // `VariableDesignation*` is typed (a concrete designation returns its own covariant
    // concrete type, e.g. `SingleVariableDesignation*`). Covariant: `VariableDesignation*`
    // derives from `AstNode*`, so this is a valid override of `AstNode::Clone()`.
    VariableDesignation* Clone() const override = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_VARIABLEDESIGNATION_HPP
