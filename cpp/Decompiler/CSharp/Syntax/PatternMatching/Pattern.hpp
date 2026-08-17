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

// Port of the `Pattern` abstract class in
// ICSharpCode.Decompiler/CSharp/Syntax/PatternMatching/Pattern.cs. `Pattern` is
// the base of the pattern nodes (AnyNode, OptionalNode, Repeat, NamedNode, ...):
// it provides the `$any$` string-wildcard, the shared `MatchString` helper, and
// the collection-match backtracking algorithm (`DoMatchCollection`) that drives
// the non-deterministic nodes. Concrete pattern nodes override `DoMatch` and, for
// the non-deterministic ones, `DoMatchCollection`.
//
// The `PossibleMatch` nested struct from the C# is ported as a free struct in the
// `PatternMatching` namespace (see BacktrackingInfo.hpp) to break the
// Pattern<->BacktrackingInfo include cycle.

#pragma once

#include "Decompiler/CSharp/Syntax/PatternMatching/BacktrackingInfo.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/INode.hpp"

#include <cassert>
#include <optional>
#include <stack>
#include <string_view>

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {

// Base class for all patterns.
class Pattern : public INode {
public:
    // The C# `public static readonly string AnyString = "$any$"`. A group-name or
    // string field equal to `$any$` matches any string in `MatchString`.
    static constexpr std::string_view AnyString = "$any$";

    // The C# `public static bool MatchString(string? pattern, string? text)`:
    // `pattern == AnyString || pattern == text`. Both arguments are nullable (a
    // null pattern matches only a null text; `$any$` matches anything including
    // null). `std::optional<std::string_view>` carries the null distinction
    // faithfully: `opt == value` is true only when the optional holds that value,
    // and `opt == opt` is true only when both are null or both hold equal strings.
    static bool MatchString(std::optional<std::string_view> pattern, std::optional<std::string_view> text) {
        if (pattern == AnyString)
            return true;
        return pattern == text;
    }

    // The C# `public abstract bool DoMatch(INode?, Match)`. Concrete pattern nodes
    // implement this.
    bool DoMatch(INode* other, Match match) override = 0;

    // The C# `public virtual bool DoMatchCollection(...)`: the default behaviour
    // for a deterministic pattern node is to match the single candidate at `pos`
    // (null past the end) via `DoMatch`.
    bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match, BacktrackingInfo& /*backtrackingInfo*/) override {
        return DoMatch(pos < static_cast<int>(other.size()) ? other[static_cast<std::size_t>(pos)] : nullptr, match);
    }

    // The C# `public static bool DoMatchCollection(IReadOnlyList<INode> patternChildren,
    // IReadOnlyList<INode> otherChildren, Match match)`. Matches the pattern child
    // list against the candidate child list with backtracking over the
    // non-deterministic nodes (Repeat, OptionalNode).
    //
    // `patternStack` mirrors `backtrackingInfo.BacktrackingStack`: each entry on
    // the backtracking stack (pushed by a non-deterministic node for one of its
    // alternatives) is paired with the pattern index to resume at after that
    // alternative's matched node. The invariant `stack.size() == patternStack.size()`
    // (restored at the top of each iteration and maintained by queueing one pattern
    // index per pushed alternative) lets the loop know how many pattern nodes each
    // alternative consumed.
    static bool DoMatchCollection(const std::vector<INode*>& patternChildren, const std::vector<INode*>& otherChildren, Match match) {
        BacktrackingInfo backtrackingInfo;
        std::stack<int> patternStack;
        std::stack<PossibleMatch>& stack = backtrackingInfo.BacktrackingStack;
        patternStack.push(0);
        stack.push(PossibleMatch(0, match.CheckPoint()));
        while (!stack.empty()) {
            int patternPos = patternStack.top();
            patternStack.pop();
            int otherPos = stack.top().NextOtherIndex;
            match.RestoreCheckPoint(stack.top().Checkpoint);
            stack.pop();
            bool success = true;
            while (patternPos < static_cast<int>(patternChildren.size()) && success) {
                INode* cur1 = patternChildren[static_cast<std::size_t>(patternPos)];
                assert(stack.size() == patternStack.size());
                success = cur1->DoMatchCollection(otherChildren, otherPos, match, backtrackingInfo);
                assert(stack.size() >= patternStack.size());
                // For every alternative the pattern node pushed, queue the next
                // pattern node so the continuation resumes after it.
                while (stack.size() > patternStack.size())
                    patternStack.push(patternPos + 1);
                patternPos++;
                otherPos++;
            }
            if (success && otherPos >= static_cast<int>(otherChildren.size()))
                return true;
        }
        return false;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching
