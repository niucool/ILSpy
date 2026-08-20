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

// Implementation of `GenericGrammarAmbiguityVisitor` (see the header for the port notes). The
// static `ResolveAmbiguities` walks the tree and wraps each ambiguous `LessThan` binary; the
// static `CausesAmbiguityWithGenerics` drives one nesting-tracked walk per `LessThan` binary via
// the per-node `Visit` overrides below.

#include "Decompiler/CSharp/OutputVisitor/GenericGrammarAmbiguityVisitor.hpp"

#include <cassert>
#include <functional>

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ParenthesizedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"

namespace ILSpy::Decompiler::CSharp::OutputVisitor {

using namespace ILSpy::Decompiler::CSharp::Syntax;

// The C# `public static void ResolveAmbiguities(AstNode rootNode)` -- walk every descendant
// (pre-order, excluding the root) and wrap each `BinaryOperatorExpression` that
// `CausesAmbiguityWithGenerics` flags. `Descendants()` returns the pre-order snapshot once;
// `ReplaceWith` re-parents the wrapped binary intact under the new `ParenthesizedExpression`,
// so every snapshot pointer stays valid and in-tree (the same pre-order the C# lazy
// `Descendants` yields -- the C# iterator records the next sibling before yielding, so a
// mid-walk replace does not lose the place).
void GenericGrammarAmbiguityVisitor::ResolveAmbiguities(AstNode* rootNode) {
    for (AstNode* node : rootNode->Descendants()) {
        auto* binary = dynamic_cast<BinaryOperatorExpression*>(node);
        if (binary != nullptr && CausesAmbiguityWithGenerics(binary)) {
            binary->ReplaceWith([](AstNode* n) -> AstNode* {
                // `new ParenthesizedExpression(n)` -- the `ReplaceWith` lambda receives the
                // detached binary and the new `ParenthesizedExpression` re-parents it as its
                // `Expression` child (the Syntax-layer non-owning `new` model).
                return new ParenthesizedExpression(static_cast<Expression*>(n));
            });
        }
    }
}

// The C# `public static bool CausesAmbiguityWithGenerics(BinaryOperatorExpression)`. Only a
// `LessThan` can open a generic-argument span; any other operator is never ambiguous. The walk
// starts at the `<`'s right operand (one unmatched `<`, so `genericNestingLevel_ = 1`) and
// follows `GetNextNode` (document order, crossing sibling/subtree boundaries). A `Visit`
// override that returns `true` stops the walk; `ambiguityFound_` then holds the verdict.
bool GenericGrammarAmbiguityVisitor::CausesAmbiguityWithGenerics(
    BinaryOperatorExpression* binaryOperatorExpression) {
    if (binaryOperatorExpression->Operator() != BinaryOperatorType::LessThan)
        return false;

    GenericGrammarAmbiguityVisitor v;
    v.genericNestingLevel_ = 1;

    for (AstNode* node = binaryOperatorExpression->Right(); node != nullptr; node = node->GetNextNode()) {
        if (node->AcceptVisitorBool(v))
            return v.ambiguityFound_;
    }
    return false;
}

// The C# `protected override bool VisitChildren(AstNode node)` -- an unhandled node is probably
// not syntactically valid in a type-argument list, so stop visiting (no ambiguity). The
// `Debug.Assert` preconditions are debug-only checks (compiled out with `NDEBUG`).
bool GenericGrammarAmbiguityVisitor::VisitChildren(AstNode* node) {
    (void)node;
    assert(genericNestingLevel_ > 0);
    assert(!ambiguityFound_);
    return true; // stop visiting, no ambiguity found
}

// The C# `public override bool VisitBinaryOperatorExpression(BinaryOperatorExpression)`.
// Visit the left operand first (it must be valid in a type-argument list, else its `Visit`
// stops the walk). Then adjust the `<`/`>`/`>>`/`>>>` nesting: a `LessThan` opens another level,
// a `GreaterThan` closes one, a `ShiftRight` closes two (only when at least two are open), and
// an `UnsignedShiftRight` closes three (only when at least three are open). When the nesting
// returns to zero the matching `>` is found: the span is a generic-argument list iff the `>`'s
// right operand is a `ParenthesizedExpression` (the `G<...>(...)` shape). Any other operator
// (the `default`) stops the walk.
bool GenericGrammarAmbiguityVisitor::VisitBinaryOperatorExpression(
    BinaryOperatorExpression* binaryOperatorExpression) {
    if (binaryOperatorExpression->Left()->AcceptVisitorBool(*this))
        return true;
    assert(genericNestingLevel_ > 0);
    switch (binaryOperatorExpression->Operator()) {
        case BinaryOperatorType::LessThan:
            genericNestingLevel_ += 1;
            break;
        case BinaryOperatorType::GreaterThan:
            genericNestingLevel_ -= 1;
            break;
        case BinaryOperatorType::ShiftRight:
            // The C# `when genericNestingLevel >= 2` guard: when it fails the case is skipped
            // and the `default` runs (stop, no ambiguity).
            if (genericNestingLevel_ >= 2)
                genericNestingLevel_ -= 2;
            else
                return true; // stop visiting, no ambiguity found
            break;
        case BinaryOperatorType::UnsignedShiftRight:
            // The C# `when genericNestingLevel >= 3` guard: same fall-through as above.
            if (genericNestingLevel_ >= 3)
                genericNestingLevel_ -= 3;
            else
                return true; // stop visiting, no ambiguity found
            break;
        default:
            return true; // stop visiting, no ambiguity found
    }
    if (genericNestingLevel_ == 0) {
        // Of all the tokens that might follow `>` and resolve the ambiguity in favor of
        // generics, `(` is the only one that might start an expression.
        ambiguityFound_ = dynamic_cast<ParenthesizedExpression*>(binaryOperatorExpression->Right()) != nullptr;
        return true; // stop visiting
    }
    return binaryOperatorExpression->Right()->AcceptVisitorBool(*this);
}

// The C# `public override bool VisitIdentifierExpression(IdentifierExpression)` -- an
// identifier is valid in a type-argument list, so keep visiting.
bool GenericGrammarAmbiguityVisitor::VisitIdentifierExpression(
    IdentifierExpression* /*identifierExpression*/) {
    return false; // keep visiting
}

// The C# `public override bool VisitTypeReferenceExpression(TypeReferenceExpression)` -- a
// type reference is valid in a type-argument list, so keep visiting.
bool GenericGrammarAmbiguityVisitor::VisitTypeReferenceExpression(
    TypeReferenceExpression* /*typeReferenceExpression*/) {
    return false; // keep visiting
}

// The C# `public override bool VisitMemberReferenceExpression(MemberReferenceExpression)` -- a
// member reference (`A.B`) is valid in a type-argument list; recurse into the target so the
// target's own `Visit` decides whether to keep visiting.
bool GenericGrammarAmbiguityVisitor::VisitMemberReferenceExpression(
    MemberReferenceExpression* memberReferenceExpression) {
    return memberReferenceExpression->Target()->AcceptVisitorBool(*this);
}

} // namespace ILSpy::Decompiler::CSharp::OutputVisitor
