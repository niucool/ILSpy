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

// Port of the `INode` interface in
// ICSharpCode.Decompiler/CSharp/Syntax/PatternMatching/INode.cs. `INode` is the
// common shape of anything that participates in a pattern match: both the real
// AST nodes (via `AstNode : INode`) and the pattern nodes themselves (via
// `Pattern : INode`). `DoMatch` matches a single candidate; `DoMatchCollection`
// matches the candidate at a collection position, letting non-deterministic
// nodes (Repeat, OptionalNode) push alternative continuations for the
// backtracking algorithm in `Pattern.DoMatchCollection`.
//
// The `PatternExtensions` static class from INode.cs (`Match`, `IsMatch`,
// `ToType`, `ToExpression`, `ToStatement`, `WithName`) is NOT ported here: those
// helpers reference the generated AST node types (`AstType`, `Expression`,
// `Statement`) and `NamedNode`, none of which exist in the port yet. They land
// with the generated node hierarchy; the core `INode` interface does not need them.

#pragma once

#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"

#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {

// Forward declaration: `DoMatchCollection` takes `BacktrackingInfo` by reference,
// so the declaration does not need its full definition.
class BacktrackingInfo;

// AST node that supports pattern matching.
class INode {
public:
    virtual ~INode() = default;

    // The C# `bool DoMatch(INode? other, Match match)`. `other` is the candidate
    // being matched (null when the pattern is tested against an absent child).
    virtual bool DoMatch(INode* other, Match match) = 0;

    // The C# `bool DoMatchCollection(IReadOnlyList<INode> other, int pos, Match match,
    // BacktrackingInfo backtrackingInfo)`. Matches the candidate at `pos`: a
    // deterministic node returns whether it matched (the caller then advances by
    // one); a non-deterministic node (Repeat, OptionalNode) pushes its alternative
    // continuation indices onto `backtrackingInfo` instead.
    //
    // `other` is the per-slot child list being matched; elements may be null, so
    // the candidate at `pos` is `pos < other.size() ? other[pos] : nullptr`.
    virtual bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match, BacktrackingInfo& backtrackingInfo) = 0;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching
