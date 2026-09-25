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

// Ports of the concrete pattern nodes in
// ICSharpCode.Decompiler/CSharp/Syntax/PatternMatching/ (AnyNode.cs, AnyNodeOrNull.cs,
// Backreference.cs, Choice.cs, IdentifierExpressionBackreference.cs, NamedNode.cs,
// OptionalNode.cs, Repeat.cs) plus the `PatternExtensions` static class from INode.cs.
//
// `Pattern`/`Match`/`BacktrackingInfo` (the matching engine) were ported earlier; the
// concrete node classes -- the nodes a pattern tree is built from -- had not been, so the
// engine tests used local `TestAnyNode`/`TestOptionalNode` stubs. This header lands the real
// nodes.
//
// Every node is a `Pattern` subclass, so a non-deterministic node overrides
// `DoMatchCollection` (Repeat, OptionalNode) and every node supplies `DoMatch`; a
// deterministic node inherits the base `DoMatchCollection` (match the single candidate at
// `pos`).
//
// Ownership: the C# nodes are garbage-collected and freely share child references. The port
// holds non-owning `INode*` children (pattern definitions are `static readonly` in the C#
// transforms and therefore live for the process, exactly the lifetime a non-owning pointer
// needs). The two convenience constructors that allocate a `NamedNode` on the caller's
// behalf (`OptionalNode(string, INode)` and `Choice.Add(string, INode)`) own the node they
// create so no leak is introduced.

#pragma once

#include "Decompiler/CSharp/Syntax/PatternMatching/Pattern.hpp"

#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"

#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Syntax {
// Forward declarations: the pattern-placeholder factories' return/parameter types
// (`Expression` is complete via IdentifierExpression.hpp; these two need only a pointer
// or reference declaration).
class AstType;
class Statement;
}

namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching {

// Converts an owned optional group name to the `optional<string_view>` `Match` captures
// under. The C# captures a `string`; the port's `Match` stores the group name as a view,
// so the node must keep the backing storage alive (it does -- the field owns the string).
inline std::optional<std::string_view> AsGroupNameView(const std::optional<std::string>& name) {
    if (name)
        return std::string_view(*name);
    return std::nullopt;
}

// The C# `public static class PatternExtensions` from INode.cs: the `Match`/`IsMatch`
// entry points. The `ToType`/`ToExpression`/`ToStatement`/`WithName` helpers of the C#
// class are not ported -- they are conversion shims for the generated placeholder
// machinery (not yet ported) and are unused by the matching engine itself.
class PatternExtensions {
public:
    // The C# `static Match Match(this INode pattern, INode? other)`: runs the match and
    // returns the populated result, or the failure sentinel when the pattern rejects.
    static ILSpy::Decompiler::CSharp::Syntax::PatternMatching::Match Match(INode& pattern, INode* other) {
        ILSpy::Decompiler::CSharp::Syntax::PatternMatching::Match match =
            PatternMatching::Match::CreateNew();
        if (pattern.DoMatch(other, match))
            return match;
        return ILSpy::Decompiler::CSharp::Syntax::PatternMatching::Match();
    }

    // The C# `static bool IsMatch(this INode pattern, INode? other)`.
    static bool IsMatch(INode& pattern, INode* other) {
        return pattern.DoMatch(other, PatternMatching::Match::CreateNew());
    }

    // The C# `static AstType ToType(this Pattern pattern)` / `static Expression
    // ToExpression(this Pattern pattern)` / `static Statement ToStatement(this Pattern
    // pattern)` -- the generated `implicit operator <Base>(Pattern)` shims. Each wraps the
    // pattern in a `PatternPlaceholderNode<Base>` (PatternPlaceholder.hpp); a null pattern
    // yields null (the generated `pattern != null ? new PatternPlaceholder(pattern) : null`).
    // The definition lives in PatternPlaceholder.cpp (the placeholder template needs the AST
    // base complete, which this header does not include).
    static ILSpy::Decompiler::CSharp::Syntax::AstType* ToType(
        std::shared_ptr<Pattern> pattern);
    static ILSpy::Decompiler::CSharp::Syntax::Expression* ToExpression(
        std::shared_ptr<Pattern> pattern);
    static ILSpy::Decompiler::CSharp::Syntax::Statement* ToStatement(
        std::shared_ptr<Pattern> pattern);

    // The C# `static Expression WithName(this Expression node, string patternGroupName)` /
    // `static Statement WithName(this Statement node, string patternGroupName)` -- wrap the
    // node in a `NamedNode` and then in a pattern placeholder, so the result can occupy an
    // AST slot while capturing the node under `patternGroupName`.
    static ILSpy::Decompiler::CSharp::Syntax::Expression* WithName(
        ILSpy::Decompiler::CSharp::Syntax::Expression& node, const std::string& patternGroupName);
    static ILSpy::Decompiler::CSharp::Syntax::Statement* WithName(
        ILSpy::Decompiler::CSharp::Syntax::Statement& node, const std::string& patternGroupName);
};

// The C# `public class AnyNode : Pattern` (AnyNode.cs): matches any non-null node,
// optionally capturing it under a group name.
class AnyNode : public Pattern {
    std::optional<std::string> groupName_;

public:
    explicit AnyNode(std::optional<std::string> groupName = std::nullopt)
        : groupName_(std::move(groupName)) {}

    const std::optional<std::string>& GroupName() const { return groupName_; }

    // The C# `DoMatch`: records the capture (a null group does not capture) and matches
    // only a non-null candidate.
    bool DoMatch(INode* other, Match match) override {
        match.Add(AsGroupNameView(groupName_), other);
        return other != nullptr;
    }
};

// The C# `public class AnyNodeOrNull : Pattern` (AnyNodeOrNull.cs): matches any node,
// including null; a null candidate records a null capture for the group.
class AnyNodeOrNull : public Pattern {
    std::optional<std::string> groupName_;

public:
    explicit AnyNodeOrNull(std::optional<std::string> groupName = std::nullopt)
        : groupName_(std::move(groupName)) {}

    const std::optional<std::string>& GroupName() const { return groupName_; }

    bool DoMatch(INode* other, Match match) override {
        if (other == nullptr)
            match.AddNull(AsGroupNameView(groupName_));
        else
            match.Add(AsGroupNameView(groupName_), other);
        return true;
    }
};

// The C# `public class NamedNode : Pattern` (NamedNode.cs): captures the candidate under a
// group name and requires the wrapped pattern to match it.
class NamedNode : public Pattern {
    std::string groupName_;
    INode* childNode_;

public:
    NamedNode(std::string groupName, INode* childNode)
        : groupName_(std::move(groupName)), childNode_(childNode) {
        if (childNode == nullptr)
            throw std::invalid_argument("childNode");
    }

    const std::string& GroupName() const { return groupName_; }
    INode* ChildNode() const { return childNode_; }

    bool DoMatch(INode* other, Match match) override {
        match.Add(groupName_, other);
        return childNode_->DoMatch(other, match);
    }
};

// The C# `public class OptionalNode : Pattern` (OptionalNode.cs): matches nothing (succeeds
// against an absent candidate) or delegates to the wrapped pattern. As a collection element
// it pushes the "absent" alternative (resume the following pattern node at the same
// position) before trying to consume the candidate.
class OptionalNode : public Pattern {
    std::unique_ptr<INode> ownedChild_;
    INode* childNode_;

public:
    explicit OptionalNode(INode* childNode) : childNode_(childNode) {
        if (childNode == nullptr)
            throw std::invalid_argument("childNode");
    }

    // The C# `OptionalNode(string groupName, INode childNode) : this(new NamedNode(...))`.
    OptionalNode(const std::string& groupName, INode* childNode)
        : ownedChild_(std::make_unique<NamedNode>(groupName, childNode)),
          childNode_(ownedChild_.get()) {}

    INode* ChildNode() const { return childNode_; }

    bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match,
                           BacktrackingInfo& backtrackingInfo) override {
        // Push the "absent" alternative (resume the following pattern node at the same
        // position), then try to consume the element here.
        backtrackingInfo.BacktrackingStack.push(PossibleMatch(pos, match.CheckPoint()));
        return childNode_->DoMatch(
            pos < static_cast<int>(other.size()) ? other[static_cast<std::size_t>(pos)] : nullptr,
            match);
    }

    bool DoMatch(INode* other, Match match) override {
        if (other == nullptr)
            return true;
        return childNode_->DoMatch(other, match);
    }
};

// The C# `public class Repeat : Pattern` (Repeat.cs): greedily matches the wrapped pattern
// as many times as possible (subject to MinCount/MaxCount), pushing every count as a
// backtracking alternative. It never matches a single element directly; the caller always
// resolves it through the backtracking stack.
class Repeat : public Pattern {
    INode* childNode_;

public:
    int MinCount = 0;
    int MaxCount = std::numeric_limits<int>::max();

    explicit Repeat(INode* childNode) : childNode_(childNode) {
        if (childNode == nullptr)
            throw std::invalid_argument("childNode");
    }

    INode* ChildNode() const { return childNode_; }

    bool DoMatchCollection(const std::vector<INode*>& other, int pos, Match match,
                           BacktrackingInfo& backtrackingInfo) override {
        std::stack<PossibleMatch>& backtrackingStack = backtrackingInfo.BacktrackingStack;
        int matchCount = 0;
        if (MinCount <= 0)
            backtrackingStack.push(PossibleMatch(pos, match.CheckPoint()));
        while (matchCount < MaxCount && pos < static_cast<int>(other.size())
               && childNode_->DoMatch(other[static_cast<std::size_t>(pos)], match)) {
            matchCount++;
            pos++;
            if (matchCount >= MinCount)
                backtrackingStack.push(PossibleMatch(pos, match.CheckPoint()));
        }
        // Never do a normal (single-element) match; always make the caller look at the
        // results on the backtracking stack.
        return false;
    }

    bool DoMatch(INode* other, Match match) override {
        if (other == nullptr)
            return MinCount <= 0;
        return MaxCount >= 1 && childNode_->DoMatch(other, match);
    }
};

// The C# `public class Backreference : Pattern` (Backreference.cs): matches the last
// capture of the referenced group (an absent group matches only a null candidate).
class Backreference : public Pattern {
    std::string referencedGroupName_;

public:
    explicit Backreference(std::string referencedGroupName)
        : referencedGroupName_(std::move(referencedGroupName)) {
        if (referencedGroupName_.empty())
            throw std::invalid_argument("referencedGroupName");
    }

    const std::string& ReferencedGroupName() const { return referencedGroupName_; }

    bool DoMatch(INode* other, Match match) override {
        std::vector<INode*> captured = match.Get(referencedGroupName_);
        INode* last = captured.empty() ? nullptr : captured.back();
        if (last == nullptr)
            return other == nullptr;
        return PatternExtensions::IsMatch(*last, other);
    }
};

// The C# `public class IdentifierExpressionBackreference : Pattern`
// (IdentifierExpressionBackreference.cs): matches an `IdentifierExpression` (with no type
// arguments) whose identifier equals the name of the referenced group's captured node.
class IdentifierExpressionBackreference : public Pattern {
    std::string referencedGroupName_;

public:
    explicit IdentifierExpressionBackreference(std::string referencedGroupName)
        : referencedGroupName_(std::move(referencedGroupName)) {
        if (referencedGroupName_.empty())
            throw std::invalid_argument("referencedGroupName");
    }

    const std::string& ReferencedGroupName() const { return referencedGroupName_; }

    bool DoMatch(INode* other, Match match) override {
        auto* ident = dynamic_cast<IdentifierExpression*>(other);
        if (ident == nullptr || ident->TypeArguments().Count() != 0)
            return false;
        std::vector<INode*> captured = match.Get(referencedGroupName_);
        INode* last = captured.empty() ? nullptr : captured.back();
        auto* referenced = dynamic_cast<AstNode*>(last);
        if (referenced == nullptr)
            return false;
        Identifier* referencedIdentifier = referenced->GetChildByKind(&Slots::Identifier);
        if (referencedIdentifier == nullptr)
            return false;
        return ident->Identifier() == referencedIdentifier->Name();
    }
};

// The C# `public class Choice : Pattern` (Choice.cs): matches the first alternative that
// accepts the candidate, restoring the match checkpoint after each failed alternative.
class Choice : public Pattern {
    std::vector<INode*> alternatives_;
    std::vector<std::unique_ptr<INode>> ownedAlternatives_;

public:
    // The C# `Add(string name, INode alternative)` wraps the alternative in a `NamedNode`.
    void Add(const std::string& name, INode* alternative) {
        if (alternative == nullptr)
            throw std::invalid_argument("alternative");
        ownedAlternatives_.push_back(std::make_unique<NamedNode>(name, alternative));
        alternatives_.push_back(ownedAlternatives_.back().get());
    }

    void Add(INode* alternative) {
        if (alternative == nullptr)
            throw std::invalid_argument("alternative");
        alternatives_.push_back(alternative);
    }

    const std::vector<INode*>& Alternatives() const { return alternatives_; }

    bool DoMatch(INode* other, Match match) override {
        int checkPoint = match.CheckPoint();
        for (INode* alt : alternatives_) {
            if (alt->DoMatch(other, match))
                return true;
            match.RestoreCheckPoint(checkPoint);
        }
        return false;
    }
};

} // namespace ILSpy::Decompiler::CSharp::Syntax::PatternMatching
