// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
// the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the `IPatternPlaceholder` marker interface in
// ICSharpCode.Decompiler/CSharp/Syntax/PatternMatching/IPatternPlaceholder.cs. The
// generated pattern-placeholder node (emitted for every `[DecompilerAstNode(
// hasPatternPlaceholder: true)]` base) wraps a `Pattern` so it can occupy an AST
// slot; the marker lets the output visitor's nesting-order assertion recognize such
// a node (the C# `node.Parent == containerStack.Peek() || containerStack.Peek() is
// IPatternPlaceholder`).
//
// The C# interface declares no members. The port's `PatternPlaceholderNode<TNode>`
// template (PatternPlaceholder.hpp) is the single implementation.

#pragma once

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {

// The C# `public interface IPatternPlaceholder {}` -- marker only.
class IPatternPlaceholder {
public:
    virtual ~IPatternPlaceholder() = default;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching
