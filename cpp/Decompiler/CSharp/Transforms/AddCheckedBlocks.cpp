// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// AddCheckedBlocks annotation half -- see the header.

#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/CSharpSlotInfo.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UncheckedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/CheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LabelStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/LocalFunctionDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/Statement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UncheckedStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {
// The singletons (the C# static readonly fields; one shared object each).
const CheckedUncheckedAnnotation kCheckedAnnotation{true, false};
const CheckedUncheckedAnnotation kUncheckedAnnotation{false, false};
const CheckedUncheckedAnnotation kExplicitUncheckedAnnotation{false, true};
} // namespace

const CheckedUncheckedAnnotation& CheckedAnnotation()
{
    return kCheckedAnnotation;
}

const CheckedUncheckedAnnotation& UncheckedAnnotation()
{
    return kUncheckedAnnotation;
}

const CheckedUncheckedAnnotation& ExplicitUncheckedAnnotation()
{
    return kExplicitUncheckedAnnotation;
}

std::shared_ptr<CheckedUncheckedAnnotation> CheckedAnnotationHandle()
{
    return std::shared_ptr<CheckedUncheckedAnnotation>(
        const_cast<CheckedUncheckedAnnotation*>(&kCheckedAnnotation), [](CheckedUncheckedAnnotation*) {});
}

std::shared_ptr<CheckedUncheckedAnnotation> UncheckedAnnotationHandle()
{
    return std::shared_ptr<CheckedUncheckedAnnotation>(
        const_cast<CheckedUncheckedAnnotation*>(&kUncheckedAnnotation),
        [](CheckedUncheckedAnnotation*) {});
}

std::shared_ptr<CheckedUncheckedAnnotation> ExplicitUncheckedAnnotationHandle()
{
    return std::shared_ptr<CheckedUncheckedAnnotation>(
        const_cast<CheckedUncheckedAnnotation*>(&kExplicitUncheckedAnnotation),
        [](CheckedUncheckedAnnotation*) {});
}

// ---------------------------------------------------------------------------------------
// The transform half.
//
// We treat placing checked/unchecked blocks as an optimization problem, with the following
// goals (the C# comment, verbatim in intent):
//   1. Use minimum number of checked blocks+expressions
//   2. Prefer checked expressions over checked blocks
//   3. Make the scope of checked expressions as small as possible
//   4. Open checked blocks as late as possible, and close checked blocks as late as possible
// (goal 1 has the highest priority). Goal 4a (open as late as possible) keeps variable
// declarations out of a checked block so a later use cannot be moved out of scope; goal 4b
// (close as late as possible) keeps the whole `checked { ... }` region together for nicer
// output. See AddCheckedBlocks.cs for the worked examples.

namespace {

using Syntax::AstNode;
using Syntax::BlockStatement;
using Syntax::CheckedExpression;
using Syntax::CheckedStatement;
using Syntax::CSharpSlotInfo;
using Syntax::Expression;
using Syntax::ExpressionStatement;
using Syntax::LabelStatement;
using Syntax::LocalFunctionDeclarationStatement;
using Syntax::Statement;
using Syntax::UncheckedExpression;
using Syntax::UncheckedStatement;

// The C# private nested `struct Cost`: the number of checked/unchecked blocks and expressions
// the placement costs. `Infinite` is the highest possible cost so adding blocks and
// expressions cannot overflow the int comparison.
struct Cost {
    static const Cost Infinite;

    int Blocks;
    int Expressions;

    Cost() : Blocks(0), Expressions(0) {}
    Cost(int blocks, int expressions) : Blocks(blocks), Expressions(expressions) {}

    // The C# `operator <`: total count first, then fewer blocks (goal 2).
    bool operator<(const Cost& b) const {
        return Blocks + Expressions < b.Blocks + b.Expressions
            || (Blocks + Expressions == b.Blocks + b.Expressions && Blocks < b.Blocks);
    }

    // The C# `operator <=`: the same ordering with `<=` on the block tiebreak, so blocks are
    // closed as late as possible (goal 4b) and opened as late as possible (goal 4a).
    bool operator<=(const Cost& b) const {
        return Blocks + Expressions < b.Blocks + b.Expressions
            || (Blocks + Expressions == b.Blocks + b.Expressions && Blocks <= b.Blocks);
    }

    Cost operator+(const Cost& b) const {
        return Cost(Blocks + b.Blocks, Expressions + b.Expressions);
    }

    // The C# `internal Cost WrapInCheckedExpr()`: the new cost if an expression with this cost
    // is wrapped in a checked/unchecked expression. The first wrap costs one expression; each
    // further layer is penalized by two, so deeply nested `checked(checked(...))` shapes are
    // avoided (the C# hack comment).
    Cost WrapInCheckedExpr() const {
        if (Expressions == 0)
            return Cost(Blocks, 1);
        return Cost(Blocks, Expressions + 2);
    }
};

const Cost Cost::Infinite{0x3fffffff, 0x3fffffff};

// The C# private nested `abstract class InsertedNode`: a lazily composed list of the blocks
// and expressions to insert. The C# `operator +` (null + x == x) ports as the `Combine` free
// function below. Ownership is `shared_ptr` because a node list can be aliased across the
// checked/unchecked result slots (only the slot chosen by the settings is ever inserted).
class InsertedNode {
public:
    virtual ~InsertedNode() = default;
    virtual void Insert(TransformContext& context) = 0;
};
using InsertedNodePtr = std::shared_ptr<InsertedNode>;

class InsertedNodeList : public InsertedNode {
public:
    InsertedNodeList(InsertedNodePtr child1, InsertedNodePtr child2)
        : child1_(std::move(child1)), child2_(std::move(child2)) {}

    void Insert(TransformContext& context) override {
        child1_->Insert(context);
        child2_->Insert(context);
    }

private:
    InsertedNodePtr child1_;
    InsertedNodePtr child2_;
};

// The C# `InsertedNode? operator +(InsertedNode? a, InsertedNode? b)`.
InsertedNodePtr Combine(InsertedNodePtr a, InsertedNodePtr b) {
    if (!a)
        return b;
    if (!b)
        return a;
    return std::make_shared<InsertedNodeList>(std::move(a), std::move(b));
}

// The C# `InsertedExpression`: wrap one expression in `checked(...)`/`unchecked(...)`.
class InsertedExpression : public InsertedNode {
public:
    InsertedExpression(Expression* expression, bool isChecked)
        : expression_(expression), isChecked_(isChecked) {}

    void Insert(TransformContext& context) override {
        context.Step(isChecked_ ? "Add checked expression" : "Add unchecked expression",
                     expression_);
        Expression* replacement;
        if (isChecked_) {
            replacement = static_cast<Expression*>(expression_->ReplaceWith(
                [](AstNode* e) -> AstNode* {
                    auto* wrapper = new CheckedExpression();
                    wrapper->Expression(static_cast<Expression*>(e));
                    return wrapper;
                }));
        } else {
            replacement = static_cast<Expression*>(expression_->ReplaceWith(
                [](AstNode* e) -> AstNode* {
                    auto* wrapper = new UncheckedExpression();
                    wrapper->Expression(static_cast<Expression*>(e));
                    return wrapper;
                }));
        }
        context.EndStep(replacement);
    }

private:
    Expression* expression_;
    bool isChecked_;
};

// The C# `InsertedBlock`: wrap the statements in `[firstStatement, lastStatement)` (inclusive
// start, exclusive end) in a `checked { ... }`/`unchecked { ... }` block.
class InsertedBlock : public InsertedNode {
public:
    InsertedBlock(Statement* firstStatement, Statement* lastStatement, bool isChecked)
        : firstStatement_(firstStatement), lastStatement_(lastStatement), isChecked_(isChecked) {}

    void Insert(TransformContext& context) override {
        // An InsertedBlock with a null start has infinite cost in the search and is never
        // selected for insertion, so by the time Insert runs firstStatement is non-null (the
        // C# Debug.Assert).
        if (firstStatement_ == nullptr)
            throw std::logic_error(
                "AddCheckedBlocks::InsertedBlock::Insert: first statement is null");
        context.Step(isChecked_ ? "Add checked block" : "Add unchecked block", firstStatement_);
        auto* newBlock = new BlockStatement();
        // Move all statements except for the first.
        Statement* next;
        for (Statement* stmt = Syntax::GetNextStatement(firstStatement_);
             stmt != nullptr && stmt != lastStatement_; stmt = next) {
            next = Syntax::GetNextStatement(stmt);
            newBlock->Statements().Add(Syntax::Detach(stmt));
        }
        // Replace the first statement with the new (un)checked block.
        Statement* checkedBlock;
        if (isChecked_) {
            auto* statement = new CheckedStatement();
            statement->Body(newBlock);
            checkedBlock = statement;
        } else {
            auto* statement = new UncheckedStatement();
            statement->Body(newBlock);
            checkedBlock = statement;
        }
        firstStatement_->ReplaceWith(checkedBlock);
        // Now also move the first node into the new block.
        newBlock->Statements().InsertAfter(nullptr, firstStatement_);
        context.EndStep(checkedBlock);
    }

private:
    Statement* firstStatement_; // inclusive
    Statement* lastStatement_;  // exclusive
    bool isChecked_;
};

// The C# `expr.Slot?.ChildType is Type ct && ct.IsAssignableFrom(typeof(Expression))`: whether
// the slot can hold an (arbitrary) `Expression` node, so wrapping `expr` in a
// `CheckedExpression`/`UncheckedExpression` stays valid in the slot. The port has no
// `System.Type`; a slot's declared child type is captured as the `dynamic_cast` predicate in
// `CSharpSlotInfo`, so testing that predicate against a representative `Expression` instance
// answers whether the declared type is `Expression` or one of its bases (`AstNode`). Warpping
// is skipped for the non-expression slots (e.g. a `Statement` or a `PrimitiveExpression`
// collection element), and for an unparented expression (a null slot).
bool SlotCanHoldExpression(const CSharpSlotInfo* slot) {
    if (slot == nullptr)
        return false;
    CheckedExpression probe;
    return slot->IsInstanceOfType(&probe);
}

// The C# private nested `class Result`: the cost and nodes to insert in each of the two
// possible contexts (checked / unchecked).
struct Result {
    Cost CostInCheckedContext;
    InsertedNodePtr NodesToInsertInCheckedContext;
    Cost CostInUncheckedContext;
    InsertedNodePtr NodesToInsertInUncheckedContext;
};

Result GetResult(AstNode& node);

// The C# `Result GetResultFromBlock(BlockStatement block)`: the two-context dynamic program
// over a block's statements. It tracks four states: checked context with no unchecked block
// open, checked context with an unchecked block open, unchecked context with no checked block
// open, and unchecked context with a checked block open. The `<`/`<=` tiebreaks open blocks as
// late as possible and close them as late as possible.
Result GetResultFromBlock(BlockStatement& block) {
    Cost costCheckedContext(0, 0);
    InsertedNodePtr nodesCheckedContext;
    Cost costCheckedContextUncheckedBlockOpen = Cost::Infinite;
    InsertedNodePtr nodesCheckedContextUncheckedBlockOpen;
    Statement* uncheckedBlockStart = nullptr;
    Cost costUncheckedContext(0, 0);
    InsertedNodePtr nodesUncheckedContext;
    Cost costUncheckedContextCheckedBlockOpen = Cost::Infinite;
    InsertedNodePtr nodesUncheckedContextCheckedBlockOpen;
    Statement* checkedBlockStart = nullptr;

    Statement* statement =
        block.Statements().Count() > 0 ? block.Statements().At(0) : nullptr;
    while (true) {
        // Blocks can be closed 'for free'. We use '<=' so that blocks are closed as late as
        // possible (goal 4b).
        if (costCheckedContextUncheckedBlockOpen <= costCheckedContext) {
            costCheckedContext = costCheckedContextUncheckedBlockOpen;
            nodesCheckedContext = Combine(nodesCheckedContextUncheckedBlockOpen,
                std::make_shared<InsertedBlock>(uncheckedBlockStart, statement, false));
        }
        if (costUncheckedContextCheckedBlockOpen <= costUncheckedContext) {
            costUncheckedContext = costUncheckedContextCheckedBlockOpen;
            nodesUncheckedContext = Combine(nodesUncheckedContextCheckedBlockOpen,
                std::make_shared<InsertedBlock>(checkedBlockStart, statement, true));
        }
        if (statement == nullptr)
            break;
        // Now try opening blocks. We use '<=' so that blocks are opened as late as possible
        // (goal 4a).
        if (costCheckedContext + Cost(1, 0) <= costCheckedContextUncheckedBlockOpen) {
            costCheckedContextUncheckedBlockOpen = costCheckedContext + Cost(1, 0);
            nodesCheckedContextUncheckedBlockOpen = nodesCheckedContext;
            uncheckedBlockStart = statement;
        }
        if (costUncheckedContext + Cost(1, 0) <= costUncheckedContextCheckedBlockOpen) {
            costUncheckedContextCheckedBlockOpen = costUncheckedContext + Cost(1, 0);
            nodesUncheckedContextCheckedBlockOpen = nodesUncheckedContext;
            checkedBlockStart = statement;
        }
        // Now handle the statement.
        Result stmtResult = GetResult(*statement);

        costCheckedContext = costCheckedContext + stmtResult.CostInCheckedContext;
        nodesCheckedContext = Combine(nodesCheckedContext,
            stmtResult.NodesToInsertInCheckedContext);
        costCheckedContextUncheckedBlockOpen =
            costCheckedContextUncheckedBlockOpen + stmtResult.CostInUncheckedContext;
        nodesCheckedContextUncheckedBlockOpen = Combine(nodesCheckedContextUncheckedBlockOpen,
            stmtResult.NodesToInsertInUncheckedContext);
        costUncheckedContext = costUncheckedContext + stmtResult.CostInUncheckedContext;
        nodesUncheckedContext = Combine(nodesUncheckedContext,
            stmtResult.NodesToInsertInUncheckedContext);
        costUncheckedContextCheckedBlockOpen =
            costUncheckedContextCheckedBlockOpen + stmtResult.CostInCheckedContext;
        nodesUncheckedContextCheckedBlockOpen = Combine(nodesUncheckedContextCheckedBlockOpen,
            stmtResult.NodesToInsertInCheckedContext);

        if (dynamic_cast<LabelStatement*>(statement) != nullptr
            || dynamic_cast<LocalFunctionDeclarationStatement*>(statement) != nullptr) {
            // We can't move labels into blocks because that might cause goto-statements to be
            // unable to jump to the labels. Also, we can't move local functions into blocks,
            // because that might cause them to become out of scope from the call-sites.
            costCheckedContextUncheckedBlockOpen = Cost::Infinite;
            costUncheckedContextCheckedBlockOpen = Cost::Infinite;
        }

        statement = Syntax::GetNextStatement(statement);
    }

    Result result;
    result.CostInCheckedContext = costCheckedContext;
    result.NodesToInsertInCheckedContext = nodesCheckedContext;
    result.CostInUncheckedContext = costUncheckedContext;
    result.NodesToInsertInUncheckedContext = nodesUncheckedContext;
    return result;
}

// The C# `Result GetResult(AstNode node)`: the cost of a non-block node is the sum of its
// children's costs (and a nested block's own dynamic program), plus the node's own annotation
// contribution when it is an expression.
Result GetResult(AstNode& node) {
    if (auto* block = dynamic_cast<BlockStatement*>(&node))
        return GetResultFromBlock(*block);
    Result result;
    for (AstNode* child : node.Children()) {
        Result childResult = GetResult(*child);
        result.CostInCheckedContext = result.CostInCheckedContext + childResult.CostInCheckedContext;
        result.NodesToInsertInCheckedContext = Combine(result.NodesToInsertInCheckedContext,
            childResult.NodesToInsertInCheckedContext);
        result.CostInUncheckedContext = result.CostInUncheckedContext + childResult.CostInUncheckedContext;
        result.NodesToInsertInUncheckedContext = Combine(result.NodesToInsertInUncheckedContext,
            childResult.NodesToInsertInUncheckedContext);
    }
    auto* expr = dynamic_cast<Expression*>(&node);
    if (expr != nullptr) {
        CheckedUncheckedAnnotation* annotation = expr->Annotation<CheckedUncheckedAnnotation>();
        if (annotation != nullptr) {
            if (annotation->IsExplicit) {
                // We don't yet support distinguishing CostInUncheckedContext vs.
                // CostInExplicitUncheckedContext, so we always force an unchecked() expression
                // here.
                if (annotation->IsChecked)
                    throw std::logic_error("explicit checked"); // should not be needed
                result.CostInCheckedContext = result.CostInUncheckedContext.WrapInCheckedExpr();
                result.CostInUncheckedContext = result.CostInUncheckedContext.WrapInCheckedExpr();
                result.NodesToInsertInUncheckedContext = Combine(
                    result.NodesToInsertInUncheckedContext,
                    std::make_shared<InsertedExpression>(expr, annotation->IsChecked));
                result.NodesToInsertInCheckedContext = result.NodesToInsertInUncheckedContext;
            } else {
                // If the annotation requires this node to be in a specific context, add a huge
                // cost to the other context. That huge cost gives us the option to ignore a
                // required checked/unchecked expression when there wouldn't be any solution
                // otherwise (e.g. `for (checked(M().x += 1); true; unchecked(M().x += 2)) {}`).
                if (annotation->IsChecked)
                    result.CostInUncheckedContext = result.CostInUncheckedContext + Cost(10000, 0);
                else
                    result.CostInCheckedContext = result.CostInCheckedContext + Cost(10000, 0);
            }
        }
        // Embed this node in a checked/unchecked expression.
        if (dynamic_cast<ExpressionStatement*>(expr->Parent()) != nullptr) {
            // We cannot use checked/unchecked for top-level-expressions.
        } else if (SlotCanHoldExpression(expr->Slot())) {
            // We use '<' so that expressions are introduced on the deepest level possible
            // (goal 3).
            Cost costIfWrapWithChecked = result.CostInCheckedContext.WrapInCheckedExpr();
            Cost costIfWrapWithUnchecked = result.CostInUncheckedContext.WrapInCheckedExpr();
            if (costIfWrapWithChecked < result.CostInUncheckedContext) {
                result.CostInUncheckedContext = costIfWrapWithChecked;
                result.NodesToInsertInUncheckedContext = Combine(
                    result.NodesToInsertInCheckedContext,
                    std::make_shared<InsertedExpression>(expr, true));
            } else if (costIfWrapWithUnchecked < result.CostInCheckedContext) {
                result.CostInCheckedContext = costIfWrapWithUnchecked;
                result.NodesToInsertInCheckedContext = Combine(
                    result.NodesToInsertInUncheckedContext,
                    std::make_shared<InsertedExpression>(expr, false));
            }
        }
    }
    return result;
}

} // namespace

void AddCheckedBlocks::Run(Syntax::AstNode& node, TransformContext& context) {
    auto* block = dynamic_cast<Syntax::BlockStatement*>(&node);
    if (block == nullptr) {
        for (Syntax::AstNode* child = node.FirstChild(); child != nullptr;
             child = child->NextSibling()) {
            Run(*child, context);
        }
    } else {
        Result r = GetResultFromBlock(*block);
        if (context.DecompileRun().Settings().CheckForOverflowUnderflow()) {
            if (r.NodesToInsertInCheckedContext)
                r.NodesToInsertInCheckedContext->Insert(context);
        } else {
            if (r.NodesToInsertInUncheckedContext)
                r.NodesToInsertInUncheckedContext->Insert(context);
        }
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
