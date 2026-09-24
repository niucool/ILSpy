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

// Port of the concrete pattern nodes from
// ICSharpCode.Decompiler/CSharp/Syntax/PatternMatching/ (the C# one-class-per-file
// layout groups here per the multi-class hand-written-node convention -- the
// TryInstructions.hpp precedent):
//
//   AnyNode                      (AnyNode.cs)          -- matches any non-null node
//   AnyNodeOrNull                (AnyNodeOrNull.cs)    -- matches any node or absence
//   NamedNode                    (NamedNode.cs)        -- names a child pattern's captures
//   Choice                       (Choice.cs)           -- first alternative that matches
//   OptionalNode                 (OptionalNode.cs)     -- matches a node or its absence
//   Repeat                       (Repeat.cs)           -- matches a repetition (min/max)
//   Backreference                (Backreference.cs)    -- re-matches an earlier capture
//   IdentifierExpressionBackreference (IdentifierExpressionBackreference.cs)
//
// The C# `PatternExtensions` (INode.cs: the `Match`/`IsMatch` extension methods) ports
// as the `MatchNode`/`IsMatchPattern` free functions below; the `ToType`/`ToExpression`
// casts are implicit in C++ (Pattern subclasses ARE AstTypes/Expressions where the C#
// needs the cast).

#pragma once

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/BacktrackingInfo.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Match.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include <cassert>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {

// The C# `groupName` fields are `string?` -- the port carries the nullable string
// (the `std::optional<std::string>` convention) and forwards it to `Match::Add`,
// which ignores an absent group name.
using PatternGroupName = std::optional<std::string>;

// ---- AnyNode (AnyNode.cs) -----------------------------------------------------------

// Matches any node. Does not match null nodes (the C# remark).
class AnyNode final : public Pattern {
    PatternGroupName groupName;

public:
    explicit AnyNode(PatternGroupName groupName = std::nullopt)
        : groupName(std::move(groupName)) {}

    // The C# `public string? GroupName` -- the accessor the pattern declarations
    // could consult; kept for parity.
    const PatternGroupName& GroupName() const { return groupName; }

    bool DoMatch(INode* other, Match match) override {
        match.Add(groupName.has_value() ? std::optional<std::string_view>(*groupName)
                                        : std::optional<std::string_view>(),
                  other);
        return other != nullptr;
    }
};

// ---- AnyNodeOrNull (AnyNodeOrNull.cs) ------------------------------------------------

// Matches any node or the node's absence (records a null capture for the group).
class AnyNodeOrNull final : public Pattern {
    PatternGroupName groupName;

public:
    explicit AnyNodeOrNull(PatternGroupName groupName = std::nullopt)
        : groupName(std::move(groupName)) {}

    const PatternGroupName& GroupName() const { return groupName; }

    bool DoMatch(INode* other, Match match) override {
        if (other == nullptr) {
            match.AddNull(groupName.has_value()
                              ? std::optional<std::string_view>(*groupName)
                              : std::optional<std::string_view>());
        } else {
            match.Add(groupName.has_value()
                          ? std::optional<std::string_view>(*groupName)
                          : std::optional<std::string_view>(),
                      other);
        }
        return true;
    }
};

// ---- NamedNode (NamedNode.cs) ---------------------------------------------------------

// Wraps a child pattern and records every match under `groupName`.
class NamedNode final : public Pattern {
    PatternGroupName groupName;
    INode* childNode = nullptr;

public:
    // The C# ctor throws ArgumentNullException for a null child (the C#
    // ArgumentNullException -> assert convention).
    NamedNode(PatternGroupName groupName, INode& childNode)
        : groupName(std::move(groupName)), childNode(&childNode) {
        assert(&childNode != nullptr);
    }

    const PatternGroupName& GroupName() const { return groupName; }
    INode& ChildNode() const { return *childNode; }

    bool DoMatch(INode* other, Match match) override {
        match.Add(groupName.has_value() ? std::optional<std::string_view>(*groupName)
                                        : std::optional<std::string_view>(),
                  other);
        return childNode->DoMatch(other, match);
    }
};

// ---- Choice (Choice.cs) -----------------------------------------------------------------

// Matches the first alternative that succeeds; a failed alternative restores
// the match checkpoint (the C# `match.RestoreCheckPoint`).
class Choice final : public Pattern {
    std::vector<INode*> alternatives;
    // The named `Add` overload's NamedNode wrappers are owned here (the C# lets
    // the GC own the wrapper it allocates inside `Add`).
    std::vector<std::unique_ptr<NamedNode>> ownedAlternatives;

public:
    // The C# `void Add(INode alternative)` -- the added pattern stays
    // caller-owned (the C# static-readonly convention).
    void Add(INode& alternative) {
        alternatives.push_back(&alternative);
    }
    // The C# `void Add(string name, INode alternative)` -- wraps a NamedNode
    // (owned by this Choice).
    void Add(std::string_view name, INode& alternative) {
        ownedAlternatives.push_back(
            std::make_unique<NamedNode>(std::string(name), alternative));
        alternatives.push_back(ownedAlternatives.back().get());
    }

    bool DoMatch(INode* other, Match match) override {
        const int checkPoint = match.CheckPoint();
        for (INode* alternative : alternatives) {
            if (alternative->DoMatch(other, match))
                return true;
            match.RestoreCheckPoint(checkPoint);
        }
        return false;
    }
};

// ---- OptionalNode (OptionalNode.cs) ------------------------------------------------------

// Matches a node or its absence. Non-deterministic over collections: the absent
// alternative is pushed for backtracking before the node is matched in place.
class OptionalNode final : public Pattern {
    INode* childNode = nullptr;

public:
    explicit OptionalNode(INode& childNode) : childNode(&childNode) {
        assert(&childNode != nullptr);
    }
    // The C# `OptionalNode(string groupName, INode childNode) : this(new
    // NamedNode(groupName, childNode))` -- the wrapper is owned by this node
    // (the C# lets the GC own it).
    OptionalNode(std::string_view groupName, INode& childNode)
        : ownedChild(new NamedNode(std::string(groupName), childNode)),
          childNode(ownedChild.get()) {
        assert(&childNode != nullptr);
    }

    INode& ChildNode() const { return *childNode; }

    bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match,
                           BacktrackingInfo& backtrackingInfo) override {
        // Push the "absent" alternative (resume the following pattern node at
        // the same position), then try to consume the element here.
        backtrackingInfo.BacktrackingStack.push(
            PossibleMatch(pos, match.CheckPoint()));
        return childNode->DoMatch(
            pos < static_cast<int>(other.size()) ? other[static_cast<std::size_t>(pos)]
                                                 : nullptr,
            match);
    }

    bool DoMatch(INode* other, Match match) override {
        if (other == nullptr)
            return true;
        return childNode->DoMatch(other, match);
    }

private:
    // The `(string, INode)` ctor's NamedNode wrapper is owned (the C# lets the
    // GC own it; the port needs an explicit owner to keep it alive).
    std::unique_ptr<NamedNode> ownedChild;
};

// ---- Repeat (Repeat.cs) -------------------------------------------------------------------

// Matches a repetition of the child pattern between MinCount and MaxCount
// occurrences; every admissible count is pushed for backtracking.
class Repeat final : public Pattern {
    INode* childNode = nullptr;
    int minCount = 0;
    int maxCount = std::numeric_limits<int>::max();

public:
    explicit Repeat(INode& childNode) : childNode(&childNode) {
        assert(&childNode != nullptr);
    }

    int MinCount() const { return minCount; }
    void MinCount(int value) { minCount = value; }
    int MaxCount() const { return maxCount; }
    void MaxCount(int value) { maxCount = value; }
    INode& ChildNode() const { return *childNode; }

    bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match,
                           BacktrackingInfo& backtrackingInfo) override {
        std::stack<PossibleMatch>& backtrackingStack = backtrackingInfo.BacktrackingStack;
        int matchCount = 0;
        if (minCount <= 0)
            backtrackingStack.push(PossibleMatch(pos, match.CheckPoint()));
        while (matchCount < maxCount && pos < static_cast<int>(other.size())
               && childNode->DoMatch(
                   other[static_cast<std::size_t>(pos)], match)) {
            matchCount++;
            pos++;
            if (matchCount >= minCount)
                backtrackingStack.push(PossibleMatch(pos, match.CheckPoint()));
        }
        // Never do a normal (single-element) match; always make the caller look
        // at the results on the backtracking stack.
        return false;
    }

    bool DoMatch(INode* other, Match match) override {
        if (other == nullptr)
            return minCount <= 0;
        return maxCount >= 1 && childNode->DoMatch(other, match);
    }
};

// ---- Backreference (Backreference.cs) -------------------------------------------------------

// Re-matches the last capture of an earlier group (structural equality via the
// captured node's own DoMatch).
class Backreference final : public Pattern {
    std::string referencedGroupName;

public:
    explicit Backreference(std::string referencedGroupName)
        : referencedGroupName(std::move(referencedGroupName)) {}

    const std::string& ReferencedGroupName() const { return referencedGroupName; }

    bool DoMatch(INode* other, Match match) override {
        const std::vector<INode*> captures = match.Get(referencedGroupName);
        if (captures.empty() || captures.back() == nullptr)
            return other == nullptr;
        // The C# `last.IsMatch(other)` (the PatternExtensions IsMatch over the
        // captured pattern node).
        return captures.back()->DoMatch(other, Match::CreateNew());
    }
};

// ---- IdentifierExpressionBackreference (IdentifierExpressionBackreference.cs) ----

// Matches identifier expressions that have the same identifier as the referenced
// variable/type definition/method definition: the candidate must be an
// IdentifierExpression without type arguments, and its identifier token must
// carry the same name as the Identifier-slot child of the referenced group's
// last capture.
class IdentifierExpressionBackreference final : public Pattern {
    std::string referencedGroupName;

public:
    explicit IdentifierExpressionBackreference(std::string referencedGroupName)
        : referencedGroupName(std::move(referencedGroupName)) {}

    const std::string& ReferencedGroupName() const { return referencedGroupName; }

    bool DoMatch(INode* other, Match match) override {
        auto* ident = dynamic_cast<IdentifierExpression*>(other);
        if (ident == nullptr || ident->TypeArguments().Count() > 0)
            return false;
        const std::vector<INode*> captures = match.Get(referencedGroupName);
        if (captures.empty())
            return false;
        // The C# `match.Get(referencedGroupName).Last() is not AstNode` -- the
        // last capture must be an AstNode (the C# `is not` with a null last
        // capture also fails).
        auto* referenced = dynamic_cast<AstNode*>(captures.back());
        if (referenced == nullptr)
            return false;
        // The C# `referenced.GetChild(Slots.Identifier)` -- the node's single
        // Identifier-slot child. The port walks the flattened child list for
        // the first `Identifier` node (every node carrying an Identifier slot
        // stores the token there).
        const Identifier* referencedIdentifier = nullptr;
        for (int i = 0; i < referenced->GetChildCount(); i++) {
            if (auto* token = dynamic_cast<Identifier*>(referenced->GetChild(i))) {
                referencedIdentifier = token;
                break;
            }
        }
        if (referencedIdentifier == nullptr)
            return false;
        return ident->Identifier() == referencedIdentifier->Name();
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching

// ---- The C# PatternExtensions (INode.cs) ----------------------------------------------------

// The C# `public static Match Match(this INode pattern, INode? other)` and
// `public static bool IsMatch(this INode pattern, INode? other)`: run the pattern
// against a candidate. A failed match returns the default (failure-sentinel) Match.
// Free functions in the enclosing namespace (the C# extension-method convention).
namespace ILSpy::Decompiler::CSharp::Syntax {

// The C# `pattern.Match(other)` -- named `MatchNode` (a free function named
// `Match` would shadow the `Match` class). The pattern is non-const (the C#
// extension takes the node by reference; `DoMatch` is a non-const member).
inline PatternMatching::Match MatchNode(PatternMatching::INode& pattern,
                                        PatternMatching::INode* other) {
    PatternMatching::Match match = PatternMatching::Match::CreateNew();
    if (pattern.DoMatch(other, match))
        return match;
    return PatternMatching::Match();
}

// The C# `pattern.IsMatch(other)`.
inline bool IsMatchPattern(PatternMatching::INode& pattern,
                           PatternMatching::INode* other) {
    return pattern.DoMatch(other, PatternMatching::Match::CreateNew());
}

} // namespace ILSpy::Decompiler::CSharp::Syntax