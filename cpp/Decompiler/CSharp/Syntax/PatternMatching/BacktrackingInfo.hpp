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

// Port of the `BacktrackingInfo` class and the `Pattern.PossibleMatch` nested
// struct in ICSharpCode.Decompiler/CSharp/Syntax/PatternMatching/ (BacktrackingInfo.cs
// and Pattern.cs). `BacktrackingInfo` carries the stack of alternative continuations
// the non-deterministic pattern nodes (Repeat, OptionalNode, ...) push while a
// collection match is in progress; `Pattern.DoMatchCollection` pops and retries them
// when a deterministic step fails.
//
// The C# `Pattern.PossibleMatch` is a nested struct; C++ cannot forward-declare a
// nested type, so it is ported as a free struct `PossibleMatch` in the
// `PatternMatching` namespace (the C# member is `internal`, used only by `Pattern`
// and `BacktrackingInfo`, so a namespace-scope struct is a faithful-enough home and
// breaks the Pattern<->BacktrackingInfo include cycle cleanly).

#pragma once

#include <stack>

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {

// One alternative continuation for the collection-match backtracking: the index
// of the next candidate (`other`) element to try, and the `Match` checkpoint to
// restore before retrying. The C# `Pattern.PossibleMatch` (readonly `NextOtherIndex`
// and `Checkpoint`).
struct PossibleMatch {
    const int NextOtherIndex;
    const int Checkpoint;

    PossibleMatch(int nextOtherIndex, int checkpoint)
        : NextOtherIndex(nextOtherIndex), Checkpoint(checkpoint) {}
};

// Container for the backtracking stack. The C# field is
// `internal Stack<Pattern.PossibleMatch> backtrackingStack`; it is public here so
// the pattern nodes and `Pattern.DoMatchCollection` (same project) can push/pop
// without a friend declaration.
class BacktrackingInfo {
public:
    std::stack<PossibleMatch> BacktrackingStack;
};

} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching
