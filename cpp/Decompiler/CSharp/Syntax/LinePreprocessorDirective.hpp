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

// Port of `LinePreprocessorDirective` in ICSharpCode.Decompiler/CSharp/Syntax/GeneralScope/
// PreProcessorDirective.cs -- the `line_directive ::= '#' 'line' ( decimal_digit+
// string_literal? | 'default' | 'hidden' )` node (C# lexical grammar), a sealed
// `PreProcessorDirective` with NO `[Slot]` children and NO new properties (it only fixes the
// `Type` to `PreProcessorDirectiveType.Line` at construction). Part of the preprocessor-directive
// family (the next in-order Phase-5 piece per the D293 plan), ported with its `PreProcessorDirective`
// base and its `PragmaWarningPreprocessorDirective` sibling.
//
// The generator emits NOTHING for `LinePreprocessorDirective` beyond what it inherits: its base
// `PreProcessorDirective` is CONCRETE (not abstract), so `NeedsVisitor` is false (no
// `AcceptVisitor` override, no `VisitLinePreprocessorDirective` on `IAstVisitor`); it has NO
// `[Slot]` (no slot storage/ctors); and the source declares no `DoMatch` (it inherits the
// hand-written `PreProcessorDirective.DoMatch`). So the only surface is the two hand-written
// ctors (delegating to the base with `PreProcessorDirectiveType.Line`) and the per-concrete
// `Clone` (the not-sealed-hierarchy + no-MemberwiseClone crux: a derived concrete class MUST
// override `Clone` to create its own concrete type, else the virtual dispatch to
// `PreProcessorDirective::Clone` would slice off the `LinePreprocessorDirective` runtime type).

#ifndef ILSPY_DECOMPILER_CSHARP_SYNTAX_LINEPREPROCESSORDIRECTIVE_HPP
#define ILSPY_DECOMPILER_CSHARP_SYNTAX_LINEPREPROCESSORDIRECTIVE_HPP

#include "Decompiler/CSharp/Syntax/PreProcessorDirective.hpp"

#include <optional>
#include <string>

namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `public sealed partial class LinePreprocessorDirective : PreProcessorDirective`.
// `final` (the C# `sealed`). A `LinePreprocessorDirective` is a `PreProcessorDirective` whose
// `Type` is fixed to `Line`; it adds no slots and no properties.
class LinePreprocessorDirective final : public PreProcessorDirective {
public:
    ~LinePreprocessorDirective() override = default;

    // The hand-written `LinePreprocessorDirective(TextLocation startLocation, TextLocation
    // endLocation) : base(PreProcessorDirectiveType.Line, startLocation, endLocation)` -- the
    // ctor that records the source span. Delegates to the base `(type, start, end)` ctor with
    // `Line`, which sets the `Trivia` span.
    LinePreprocessorDirective(TextLocation startLocation, TextLocation endLocation)
        : PreProcessorDirective(PreProcessorDirectiveType::Line, startLocation, endLocation) {}

    // The hand-written `LinePreprocessorDirective(string? argument = null) : base(
    // PreProcessorDirectiveType.Line, argument)` -- the ctor that sets the optional rest-of-line
    // argument (e.g. a line number / file name / `default` / `hidden`). Delegates to the base
    // `(type, argument)` ctor with `Line`.
    LinePreprocessorDirective(std::optional<std::string> argument = std::nullopt)
        : PreProcessorDirective(PreProcessorDirectiveType::Line, std::move(argument)) {}

    // The per-concrete `Clone` (the not-sealed-hierarchy + no-MemberwiseClone crux): creates a
    // fresh `LinePreprocessorDirective` (NOT a sliced `PreProcessorDirective`) by reusing the
    // source-span ctor, which sets `Type=Line` and copies the `Trivia` span from `*this`
    // (`StartLocation()`/`EndLocation()` are NOT overridden here, so they return the stored
    // `Trivia` span). The `Argument` is copied via the inherited public setter (the base's
    // `argument_` is private). The annotation channel is copied (`CloneAnnotationsFrom` +
    // `ReparentTrivia`, the D223 concrete-clone pattern). No children to deep-copy (a leaf).
    // The covariant return is `LinePreprocessorDirective*` (through `PreProcessorDirective*`,
    // the virtual `PreProcessorDirective::Clone`).
    LinePreprocessorDirective* Clone() const override {
        auto* node = new LinePreprocessorDirective(StartLocation(), EndLocation());
        node->Argument(Argument());
        node->CloneAnnotationsFrom(*this);
        node->ReparentTrivia();
        return node;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax

#endif // ILSPY_DECOMPILER_CSHARP_SYNTAX_LINEPREPROCESSORDIRECTIVE_HPP
