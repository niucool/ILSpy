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

// Port of the `InterpolatedStringContent` abstract base in
// ICSharpCode.Decompiler/CSharp/Syntax/Expressions/InterpolatedStringExpression.cs (the
// generated `InterpolatedStringContent.g.cs` + the hand-written partial -- the partial is
// empty). The next in-order Phase-5 piece per the D308 plan ("InterpolatedStringExpression --
// the InterpolatedStringContent abstract base + Interpolation/InterpolatedStringText concrete
// family"). `interpolated_string_content ::= interpolation | interpolated_string_text`
// (C# grammar 12.8.3): the common base of the two interpolated-string content node kinds an
// `InterpolatedStringExpression.Content` collection holds.
//
// The abstract base ports as a thin shell mirroring the `Expression` D226 / `AstType` D236 /
// `Statement` D254 / `VariableDesignation` D264 covariant-pure-virtual-`Clone` shells -- the
// established mechanical port shape for every `partial class : AstNode` abstract base. ONE
// divergence from the `Expression`/`AstType`/`Statement` sibling bases (shared with
// `VariableDesignation` D264): the C# `InterpolatedStringContent` has NO hand-written
// `new InterpolatedStringContent Clone()` (its hand-written partial is empty), so a C# caller
// through an `InterpolatedStringContent` reference gets an `AstNode` back from the inherited
// `AstNode.Clone()`. The C++ port nonetheless re-declares the inherited `AstNode::Clone()`
// (which returns an `AstNode*` and has a throwing base body) as a covariant pure-virtual
// returning `InterpolatedStringContent*`, for two reasons: (1) consistency with the
// `Expression`/`AstType`/`Statement`/`VariableDesignation` port convention (the typed return
// the C# `new T Clone()` gives on those bases), and (2) the pure-virtual forces every concrete
// `InterpolatedStringContent` (`Interpolation`/`InterpolatedStringText`) to override `Clone`
// at compile time (otherwise the throwing base body would be inherited and only fail at
// runtime). The `hasPatternPlaceholder: false` default (no `[DecompilerAstNode]` arg) means no
// pattern placeholder -- moot in the port (the placeholder is deferred for every base). An
// abstract base gets NO `Visit` method on `IAstVisitor` and NO `AcceptVisitor` override
// (`NeedsVisitor = !IsAbstract && base.IsAbstract` is false for an abstract base), so this
// header adds nothing to the visitor dispatch; the concrete `Interpolation`/
// `InterpolatedStringText` plug into it.

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATEDSTRINGCONTENT_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATEDSTRINGCONTENT_HPP

#include "Decompiler/CSharp/Syntax/AstNode.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public abstract partial class InterpolatedStringContent : AstNode`. Abstract: a
// concrete content node overrides at least `DoMatch`, `AcceptVisitor`, and `Clone`. Derives
// from `AstNode` (the annotation channel + the pattern-match interface + the slot-storage
// contract); `AstNode` is polymorphic, so `InterpolatedStringContent` is too (the
// `dynamic_cast`-based is-a tests the slot system and the annotation channel use stay valid).
// The hierarchy is disjoint from `Expression`, `Statement`, and `AstType` (an
// `InterpolatedStringContent` is an `AstNode` but NOT an `Expression` nor a `Statement` nor an
// `AstType`).
class InterpolatedStringContent : public AstNode {
public:
    ~InterpolatedStringContent() override = default;
    InterpolatedStringContent() = default;
    InterpolatedStringContent(const InterpolatedStringContent&) = delete;
    InterpolatedStringContent& operator=(const InterpolatedStringContent&) = delete;

    // A typed clone returning an `InterpolatedStringContent*` instead of an `AstNode*`. The
    // C# `InterpolatedStringContent` has NO hand-written `new InterpolatedStringContent Clone()`
    // (its hand-written partial is empty), so this re-declaration is a C++ port addition for
    // consistency with the `Expression`/`AstType`/`Statement`/`VariableDesignation` convention
    // and for the compile-time concrete-override enforcement (the pure-virtual). The base
    // `AstNode::Clone()` has a throwing body (C++ has no `MemberwiseClone`); this re-declaration
    // makes it pure, so the throwing body is unreachable through the content hierarchy, and a
    // call through an `InterpolatedStringContent*` is typed (a concrete content returns its own
    // covariant concrete type, e.g. `Interpolation*`). Covariant: `InterpolatedStringContent*`
    // derives from `AstNode*`, so this is a valid override of `AstNode::Clone()`.
    InterpolatedStringContent* Clone() const override = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_INTERPOLATEDSTRINGCONTENT_HPP
