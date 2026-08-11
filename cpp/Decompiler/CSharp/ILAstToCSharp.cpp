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

// Implementation of the ILAst-to-C#-text seed. The walker translates the ILAst
// shapes the IL reader produces: blocks flatten into statement lists, branches
// become gotos to IL_XXXX labels (no BlockBuilder region recovery yet), stloc
// declares a `var` on first store, and calls/casts/field/array accesses use
// approximate C# syntax (no resolver, so no using directives, no overload or
// parenthesization decisions). Unknown nodes degrade to a marked-up default
// expression rather than dropping text or crashing.

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLen.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/LockInstruction.hpp"
#include "Decompiler/IL/Instructions/MatchInstruction.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"
#include "Decompiler/IL/Instructions/NullableInstructions.hpp"
#include "Decompiler/IL/Instructions/NullCoalescingInstruction.hpp"
#include "Decompiler/IL/Instructions/PinnedRegion.hpp"
#include "Decompiler/IL/Instructions/Rethrow.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/UsingInstruction.hpp"
#include "Decompiler/IL/Instructions/Throw.hpp"
#include "Decompiler/IL/Instructions/ThreeValuedBoolInstructions.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::IL {
namespace {

// "Namespace.Type::Member" -> "Namespace.Type.Member"; a trailing "::.ctor"
// marks a constructor reference (the "new" form is added by the caller).
std::string FlattenMetadataName(std::string name) {
    for (std::size_t pos; (pos = name.find("::")) != std::string::npos;)
        name.replace(pos, 2, ".");
    if (name.size() >= 2 && name.compare(name.size() - 2, 2, "..") == 0)
        name.erase(name.size() - 1);  // fold ".." left by an empty member segment
    return name;
}

std::string EscapeStringLiteral(std::string_view value) {
    std::string out = "\"";
    char buf[8];
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\0': out += "\\0"; break;
            default:
                if (c < 0x20 || c > 0x7E) {
                    std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    out += '"';
    return out;
}

std::string TypeDisplayName(const TypeSystem::ITypePtr& type) {
    return CSharpTypeName(type);
}

const char* ConvTargetName(StackType target) {
    switch (target) {
        case StackType::I4: return "int";
        case StackType::I8: return "long";
        case StackType::I: return "nint";
        case StackType::F4: return "float";
        case StackType::F8: return "double";
        default: return "int";
    }
}

// Emit an ILFunction body as C#-ish text. See the file header for the level
// of fidelity this seed aims for.
class CEmitter {
public:
    explicit CEmitter(std::string& out) : out_(out) {}

    void EmitMethod(const ILFunction& fn, std::string_view returnType,
                    std::string_view methodName, std::string_view paramDecl) {
        fn_ = &fn;
        returnTypeName_ = std::string(returnType);
        out_ += returnType;
        out_ += ' ';
        out_ += methodName;
        out_ += '(';
        out_ += paramDecl;
        out_ += ")\n{\n";
        if (fn.Body) {
            CollectLoopHeaders(fn.Body.get());
            CollectLabels(fn.Body.get());
            EmitContainer(*fn.Body, 1);
        }
        out_ += "}\n";
    }

private:
    std::string& out_;
    const ILFunction* fn_ = nullptr;
    std::string returnTypeName_;  // the C# name of the function's return type
    std::set<std::string> declared_;          // locals already introduced with `var`
    std::map<const Block*, std::string> labels_;  // branch-target block -> IL_XXXX
    std::set<const Block*> loopHeaders_;  // first block of each Loop container

    void Line(int indent, std::string_view text) {
        out_.append(static_cast<std::size_t>(indent) * 4, ' ');
        out_ += text;
        out_ += '\n';
    }

    static std::string LabelFor(std::uint32_t offset) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "IL_%04X", offset);
        return buf;
    }

    // Every branch-target block gets an IL_XXXX label; walk the whole tree so
    // branches nested in if/switch arms are covered too.
    void CollectLoopHeaders(const ILInstruction* inst) {
        if (!inst) return;
        if (auto* c = dynamic_cast<const BlockContainer*>(inst)) {
            if ((c->Kind == ContainerKind::Loop || c->Kind == ContainerKind::While) && !c->Blocks.empty()) {
                loopHeaders_.insert(c->Blocks.front().get());
                // For a While container, the body entry (the target of the
                // condition's true-arm Branch) is also unlabeled -- it's the
                // implicit body start, not a goto target.
                if (c->Kind == ContainerKind::While) {
                    const Block* entry = c->Blocks.front().get();
                    if (entry && entry->FinalInstruction &&
                        entry->FinalInstruction->Op == OpCode::IfInstruction) {
                        const auto& iff = static_cast<const IfInstruction&>(*entry->FinalInstruction);
                        if (iff.TrueInst && iff.TrueInst->Op == OpCode::Branch)
                            loopHeaders_.insert(static_cast<const Branch*>(iff.TrueInst.get())->TargetBlock);
                    }
                }
            }
        }
        for (int i = 0; i < inst->ChildCount(); ++i) CollectLoopHeaders(inst->GetChild(i));
    }

    void CollectLabels(const ILInstruction* inst) {
        if (!inst) return;
        if (inst->Op == OpCode::Branch) {
            const auto* br = static_cast<const Branch*>(inst);
            if (br->TargetBlock && labels_.find(br->TargetBlock) == labels_.end() &&
                loopHeaders_.find(br->TargetBlock) == loopHeaders_.end())
                labels_[br->TargetBlock] = LabelFor(br->TargetOffset);
        }
        for (int i = 0; i < inst->ChildCount(); ++i) CollectLabels(inst->GetChild(i));
    }

    // True if `br` is a redundant `goto nextBlock`: the branch's enclosing
    // block's container has the target as the block after the enclosing block,
    // AND the branch is the enclosing block's FinalInstruction (a plain block
    // final, not a switch section / if arm -- those render their gotos
    // conventionally). The enclosing block falls through, so the goto is a
    // no-op.
    static bool IsFallThroughGoto(const Branch* br) {
        if (!br || !br->TargetBlock) return false;
        // Walk up to the enclosing block (the first Block ancestor).
        Block* encBlock = nullptr;
        for (const ILInstruction* p = br; p; p = p->Parent) {
            encBlock = const_cast<Block*>(dynamic_cast<const Block*>(p));
            if (encBlock) break;
        }
        if (!encBlock) return false;
        // Only when the branch IS the enclosing block's final (a plain block
        // final). A branch inside an if-arm or a switch section is not a
        // block-final fall-through.
        if (encBlock->FinalInstruction.get() != br) return false;
        BlockContainer* encContainer = dynamic_cast<BlockContainer*>(encBlock->Parent);
        if (!encContainer) return false;
        for (std::size_t i = 0; i + 1 < encContainer->Blocks.size(); ++i) {
            if (encContainer->Blocks[i].get() == encBlock)
                return encContainer->Blocks[i + 1].get() == br->TargetBlock;
        }
        return false;
    }

    std::string GotoText(const Branch& br) const {
        if (br.TargetBlock) {
            // A branch to a loop header is a `continue` (the back-edge) only
            // when the branch is INSIDE the loop (the header's container is an
            // ancestor of the branch). A branch from outside the loop (the
            // pre-header's entry branch) is the implicit loop start, not a
            // continue -- render it as a goto (or drop it if the loop follows).
            if (loopHeaders_.find(br.TargetBlock) != loopHeaders_.end()) {
                auto* loopContainer = dynamic_cast<BlockContainer*>(br.TargetBlock->Parent);
                bool insideLoop = false;
                for (const ILInstruction* p = &br; p; p = p->Parent)
                    if (p == loopContainer) { insideLoop = true; break; }
                if (insideLoop) return "continue;";
            }
            // Drop a redundant `goto nextBlock` when the branch's enclosing
            // block's container's next block IS the target -- the enclosing
            // block falls through to it. (A rendering-only no-op; the ILAst
            // goto survives but is not emitted.)
            if (IsFallThroughGoto(&br)) return "";
            auto it = labels_.find(br.TargetBlock);
            if (it != labels_.end()) return "goto " + it->second + ";";
        }
        return "goto " + LabelFor(br.TargetOffset) + ";";
    }

    void EmitContainer(const BlockContainer& container, int indent) {
        if (container.Kind == ContainerKind::While && !container.Blocks.empty()) {
            // A while container: the entry point's FinalInstruction is the
            // while condition `if (cond) br body else leave(loop)`. Render as
            // `while (cond) { body }` -- the condition from the if, the body
            // from the blocks after the entry.
            const Block* header = container.Blocks.front().get();
            std::string cond = "(default)";
            if (header && header->FinalInstruction &&
                header->FinalInstruction->Op == OpCode::IfInstruction) {
                const auto& iff = static_cast<const IfInstruction&>(*header->FinalInstruction);
                if (iff.Condition) cond = CondExpr(*iff.Condition);
            }
            Line(indent, "while (" + cond + ")");
            Line(indent, "{");
            // The body is every block after the entry point.
            for (std::size_t i = 1; i < container.Blocks.size(); ++i) {
                const auto& block = container.Blocks[i];
                if (!block) continue;
                // Drop a trailing back-edge branch to the entry (implicit iter).
                bool dropFinal = false;
                if (block->FinalInstruction &&
                    block->FinalInstruction->Op == OpCode::Branch) {
                    auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                    if (br->TargetBlock == header) dropFinal = true;
                }
                EmitBlock(*block, indent + 1, dropFinal);
            }
            Line(indent, "}");
            return;
        }
        if (container.Kind == ContainerKind::DoWhile && !container.Blocks.empty()) {
            // A do-while loop: render as `do { ... } while (cond);` where the
            // condition is the last block's if (negated, since the true-arm is
            // the continue / the fall-through is the break).
            const Block* header = container.Blocks.front().get();
            const Block* last = container.Blocks.back().get();
            std::string cond = "true";
            if (last && last->FinalInstruction &&
                last->FinalInstruction->Op == OpCode::IfInstruction) {
                const auto& iff = static_cast<const IfInstruction&>(*last->FinalInstruction);
                if (iff.Condition) cond = NegateCondText(*iff.Condition);
            }
            Line(indent, "do");
            Line(indent, "{");
            for (std::size_t i = 0; i < container.Blocks.size(); ++i) {
                const auto& block = container.Blocks[i];
                if (!block) continue;
                bool dropFinal = (block.get() == last);  // drop the do-while condition if
                // Also drop a trailing back-edge to the header (implicit iter).
                if (!dropFinal && block->FinalInstruction &&
                    block->FinalInstruction->Op == OpCode::Branch) {
                    auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                    if (br->TargetBlock == header) dropFinal = true;
                }
                EmitBlock(*block, indent + 1, dropFinal);
            }
            Line(indent, "} while (" + cond + ");");
            return;
        }
        if (container.Kind == ContainerKind::Loop && !container.Blocks.empty()) {
            // A loop container renders as `while (true) { ... }`; the back-edge
            // branch to the header (the first block) is implicit -- the loop
            // iterates. Only the LAST block's trailing back-edge is dropped (a
            // mid-loop back-edge is a `continue`, rendered as a goto for now).
            const Block* header = container.Blocks.front().get();
            Line(indent, "while (true)");
            Line(indent, "{");
            for (std::size_t i = 0; i < container.Blocks.size(); ++i) {
                const auto& block = container.Blocks[i];
                if (!block) continue;
                bool isLast = (i + 1 == container.Blocks.size());
                bool dropFinal = false;
                if (isLast && block->FinalInstruction &&
                    block->FinalInstruction->Op == OpCode::Branch) {
                    auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                    if (br->TargetBlock == header) dropFinal = true;
                }
                EmitBlock(*block, indent + 1, dropFinal);
            }
            Line(indent, "}");
            return;
        }
        for (std::size_t i = 0; i < container.Blocks.size(); ++i) {
            const auto& block = container.Blocks[i];
            if (!block) continue;
            // Drop a trailing `goto nextBlock` when nextBlock is the next block
            // in the container -- the block falls through, so the goto is
            // redundant. (CFS can't merge a multi-pred nextBlock, but the goto
            // is still redundant for rendering.)
            bool dropFinal = false;
            if (i + 1 < container.Blocks.size() && block->FinalInstruction &&
                block->FinalInstruction->Op == OpCode::Branch) {
                auto* br = static_cast<Branch*>(block->FinalInstruction.get());
                if (br->TargetBlock == container.Blocks[i + 1].get())
                    dropFinal = true;
            }
            EmitBlock(*block, indent, dropFinal);
        }
    }

    void EmitBlock(const Block& block, int indent, bool dropFinal = false) {
        auto label = labels_.find(&block);
        if (label != labels_.end()) {
            // C# labels start in column 0 by convention.
            out_ += label->second;
            out_ += ":\n";
        }
        for (const auto& inst : block.Instructions) {
            if (inst && inst->Op != OpCode::Nop) EmitStatement(*inst, indent);
        }
        if (block.FinalInstruction && !dropFinal) EmitStatement(*block.FinalInstruction, indent);
    }

    // A non-Block arm of if/try: brace it at this indent.
    void EmitBraced(const ILInstruction& inst, int indent) {
        Line(indent, "{");
        if (inst.Op == OpCode::Block) {
            EmitBlock(static_cast<const Block&>(inst), indent + 1);
        } else if (inst.Op == OpCode::BlockContainer) {
            EmitContainer(static_cast<const BlockContainer&>(inst), indent + 1);
        } else {
            EmitStatement(inst, indent + 1);
        }
        Line(indent, "}");
    }

    void EmitStatement(const ILInstruction& inst, int indent) {
        switch (inst.Op) {
            case OpCode::StLoc: {
                const auto& st = static_cast<const StLoc&>(inst);
                std::string name = st.Variable ? st.Variable->Name : "?";
                bool declare = st.Variable && st.Variable->Kind != VariableKind::Parameter &&
                               declared_.insert(name).second;
                // `V = V op expr` -> `V op= expr` (or `V++`/`V--` for +/- 1) when
                // the binary's left is a load of the same variable. A declaration
                // (`var V = ...`) is never a compound assignment.
                std::string valueText = BoolLiteralText(st.Variable.get(), st.Value.get());
                if (valueText.empty()) {
                    valueText = Expr(*st.Value);
                    // Strip the redundant outer parens the seed wraps around `as`
                    // and `isinst` expressions in an assignment.
                    if (st.Value && st.Value->Op == OpCode::IsInst)
                        valueText = StripOuterParens(std::move(valueText));
                }
                std::string assign = declare ? " = " + valueText : AssignmentText(st, name);
                std::string decl = declare ? CSharpTypeName(st.Variable->Type) + " " + name : name;
                Line(indent, decl + assign + ";");
                return;
            }
            case OpCode::Call: {
                Line(indent, CtorCallStatementText(static_cast<const Call&>(inst)) + ";");
                return;
            }
            case OpCode::StObj: {
                const auto& st = static_cast<const StObj&>(inst);
                Line(indent, StoreTargetText(*st.Target) + " = " + Expr(*st.Value) + ";");
                return;
            }
            case OpCode::NumericCompoundAssign: {
                // A numeric compound assignment as a statement: `target op= value;`
                // (or `target++;`/`target--;` for the post-increment/decrement).
                // The Expr case renders the compound-assign expression form.
                Line(indent, Expr(inst) + ";");
                return;
            }
            case OpCode::UserDefinedCompoundAssign: {
                // A user-defined compound assignment as a statement: `target op= value;`
                // (or `target++;`/`++target;` for op_Increment/op_Decrement). The Expr
                // case renders the compound-assign / increment expression form.
                Line(indent, Expr(inst) + ";");
                return;
            }
            case OpCode::Throw: {
                const auto& th = static_cast<const Throw&>(inst);
                Line(indent, "throw " + (th.Argument ? Expr(*th.Argument) : std::string("(rethrow)")) + ";");
                return;
            }
            case OpCode::Rethrow:
                Line(indent, "throw;");
                return;
            case OpCode::Leave: {
                const auto& leave = static_cast<const Leave&>(inst);
                // A leave of the function body is `return`; a leave of a loop
                // container is `break`; other leaves (switch/try) are `break`
                // for now (the real back end disambiguates).
                if (fn_ && leave.TargetContainer == fn_->Body.get()) {
                    if (leave.Value) {
                        // `return ldc.i4 0/1` in a Boolean-returning function is
                        // `return false;`/`return true;` (the IL idiom for bool
                        // constants in the return position).
                        std::string val = BoolLiteralFromReturn(leave.Value.get());
                        if (val.empty()) val = Expr(*leave.Value);
                        Line(indent, "return " + val + ";");
                    }
                    else Line(indent, "return;");
                } else {
                    Line(indent, "break;");
                }
                return;
            }
            case OpCode::Branch:
                Line(indent, GotoText(static_cast<const Branch&>(inst)));
                return;
            case OpCode::IfInstruction: {
                const auto& iff = static_cast<const IfInstruction&>(inst);
                std::string cond = iff.Condition ? CondExpr(*iff.Condition) : "(default)";
                if (!iff.FalseInst && iff.TrueInst && iff.TrueInst->Op == OpCode::Branch) {
                    std::string gotoText = GotoText(*static_cast<const Branch*>(iff.TrueInst.get()));
                    if (!gotoText.empty())
                        Line(indent, "if (" + cond + ") " + gotoText);
                    // An empty goto (redundant goto-to-next): drop the whole if
                    // -- the block falls through, the if is a no-op. (The
                    // condition may have side effects but the IL executed it
                    // unconditionally before the branch; our model has it as
                    // the if's condition, so dropping is sound only when the
                    // condition is pure. We approximate by dropping -- the
                    // common case is a pure comparison.)
                    return;
                }
                // An empty true arm (a Block with no instructions/final, or null)
                // with a non-empty else: swap to `if (!cond) { else }` so the
                // output has no empty `{ }`.
                auto isEmptyArm = [](const std::unique_ptr<ILInstruction>& arm) {
                    if (!arm) return true;
                    if (auto* b = dynamic_cast<const Block*>(arm.get()))
                        return b->Instructions.empty() && !b->FinalInstruction;
                    return false;
                };
                std::string trueArm = cond;
                std::unique_ptr<ILInstruction>* elseArm = nullptr;
                bool negated = false;
                if (isEmptyArm(iff.TrueInst) && !isEmptyArm(iff.FalseInst)) {
                    // swap: render if (!cond) { false } -- no else.
                    negated = true;
                    cond = StripOuterParens(NegateCondText(*iff.Condition));
                    elseArm = const_cast<std::unique_ptr<ILInstruction>*>(&iff.TrueInst);
                    // Render the false arm as the (only) body.
                    Line(indent, "if (" + cond + ")");
                    EmitBraced(*iff.FalseInst, indent);
                    return;
                }
                Line(indent, "if (" + cond + ")");
                if (iff.TrueInst) EmitBraced(*iff.TrueInst, indent);
                else Line(indent, "{ }");
                if (iff.FalseInst) {
                    if (isEmptyArm(iff.FalseInst)) return;  // skip empty else
                    Line(indent, "else");
                    EmitBraced(*iff.FalseInst, indent);
                }
                return;
            }
            case OpCode::SwitchInstruction: {
                const auto& sw = static_cast<const SwitchInstruction&>(inst);
                Line(indent, "switch (" + (sw.Value ? Expr(*sw.Value) : std::string("(default)")) + ")");
                Line(indent, "{");
                for (const auto& section : sw.Sections) {
                    if (!section) continue;
                    if (section->HasNullLabel) {
                        Line(indent + 1, "case null:");
                    }
                    if (section->Labels.IsEmpty() && !section->HasNullLabel) {
                        Line(indent + 1, "default:");
                    } else {
                        for (const auto& iv : section->Labels.Intervals()) {
                            if (iv.Start == iv.InclusiveEnd())
                                Line(indent + 1, "case " + std::to_string(iv.Start) + ":");
                            else
                                Line(indent + 1, "case " + std::to_string(iv.Start) +
                                      ".." + std::to_string(iv.InclusiveEnd()) + ":");
                        }
                    }
                    if (section->Body) EmitStatement(*section->Body, indent + 1);
                }
                Line(indent, "}");
                return;
            }
            case OpCode::TryCatch: {
                const auto& tc = static_cast<const TryCatch&>(inst);
                Line(indent, "try");
                if (tc.TryBlock) EmitBraced(*tc.TryBlock, indent); else Line(indent, "{ }");
                for (const auto& handler : tc.Handlers) {
                    if (!handler) continue;
                    std::string head = "catch";
                    if (handler->Variable) {
                        head += " (" + (handler->Variable->Type
                                        ? handler->Variable->Type->ReflectionName()
                                        : std::string("System.Exception")) +
                                " " + handler->Variable->Name + ")";
                        // A plain catch carries the constant filter ldc.i4(1);
                        // don't print it as a `when` clause.
                        bool isAlwaysTrue = false;
                        if (auto* one = dynamic_cast<const LdcI4*>(handler->Filter.get()))
                            isAlwaysTrue = (one->Value == 1);
                        if (handler->Filter && !isAlwaysTrue)
                            head += " when (" + Expr(*handler->Filter) + ")";
                    }
                    Line(indent, head);
                    if (handler->Body) EmitBraced(*handler->Body, indent); else Line(indent, "{ }");
                }
                return;
            }
            case OpCode::TryFinally: {
                const auto& tf = static_cast<const TryFinally&>(inst);
                Line(indent, "try");
                if (tf.TryBlock) EmitBraced(*tf.TryBlock, indent); else Line(indent, "{ }");
                Line(indent, "finally");
                if (tf.FinallyBlock) EmitBraced(*tf.FinallyBlock, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::LockInstruction: {
                const auto& lk = static_cast<const LockInstruction&>(inst);
                Line(indent, "lock (" + (lk.OnExpression ? Expr(*lk.OnExpression) : std::string("?")) + ")");
                if (lk.Body) EmitBraced(*lk.Body, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::UsingInstruction: {
                // The C# `using` statement: `using (resource) { body }` (the
                // expression form). The UsingInstruction also carries the local
                // the resource is stored into, but the seed elides it (the real
                // back end declares the using-local via the using, not DeclareVariables).
                const auto& us = static_cast<const UsingInstruction&>(inst);
                Line(indent, "using (" +
                     (us.ResourceExpression ? Expr(*us.ResourceExpression) : std::string("null")) + ")");
                if (us.Body) EmitBraced(*us.Body, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::TryFault: {
                const auto& tf = static_cast<const TryFault&>(inst);
                Line(indent, "try");
                if (tf.TryBlock) EmitBraced(*tf.TryBlock, indent); else Line(indent, "{ }");
                Line(indent, "fault");
                if (tf.FaultBlock) EmitBraced(*tf.FaultBlock, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::PinnedRegion: {
                const auto& pr = static_cast<const PinnedRegion&>(inst);
                std::string varType = pr.Variable && pr.Variable->Type
                    ? CSharpTypeName(pr.Variable->Type) : std::string("var");
                std::string varName = pr.Variable ? pr.Variable->Name : std::string("pinned");
                Line(indent, "fixed (" + varType + " " + varName + " = " +
                             (pr.Init ? Expr(*pr.Init) : std::string("null")) + ")");
                if (pr.Body) EmitBraced(*pr.Body, indent); else Line(indent, "{ }");
                return;
            }
            case OpCode::Block:
                EmitBraced(inst, indent);
                return;
            case OpCode::BlockContainer:
                EmitContainer(static_cast<const BlockContainer&>(inst), indent);
                return;
            case OpCode::Nop:
                return;
            default:
                Line(indent, "/* unhandled statement op " +
                             std::to_string(static_cast<int>(inst.Op)) + " */;");
                return;
        }
    }

    // Map a C# operator method name ("op_Equality") to its symbolic form.
    // Returns nullptr for non-operators.
    static const char* OperatorSymbol(std::string_view methodName) {
        auto pos = methodName.rfind("::");
        std::string_view op = (pos != std::string_view::npos) ? methodName.substr(pos + 2) : methodName;
        if (op == "op_Equality") return "==";
        if (op == "op_Inequality") return "!=";
        if (op == "op_LessThan") return "<";
        if (op == "op_LessThanOrEqual") return "<=";
        if (op == "op_GreaterThan") return ">";
        if (op == "op_GreaterThanOrEqual") return ">=";
        if (op == "op_Addition") return "+";
        if (op == "op_Subtraction") return "-";
        if (op == "op_Multiply") return "*";
        if (op == "op_Division") return "/";
        if (op == "op_Modulus") return "%";
        if (op == "op_BitwiseAnd") return "&";
        if (op == "op_BitwiseOr") return "|";
        if (op == "op_ExclusiveOr") return "^";
        if (op == "op_LeftShift") return "<<";
        if (op == "op_RightShift") return ">>";
        return nullptr;
    }

    // The symbol for a unary operator method, or nullptr. Returns a 2-char
    // prefix (e.g. "-", "!", "~") or "++"/"--" for inc/dec.
    static const char* UnaryOperatorSymbol(std::string_view methodName) {
        auto pos = methodName.rfind("::");
        std::string_view op = (pos != std::string_view::npos) ? methodName.substr(pos + 2) : methodName;
        if (op == "op_UnaryNegation") return "-";
        if (op == "op_UnaryPlus") return "+";
        if (op == "op_OnesComplement") return "~";
        if (op == "op_LogicalNot") return "!";
        if (op == "op_Increment") return "++";
        if (op == "op_Decrement") return "--";
        return nullptr;
    }

    // The declaring type's short name for a "Namespace.Type::member" string,
    // for op_Explicit/op_Implicit conversion rendering ((TargetType)value).
    static std::string DeclaringTypeName(std::string_view methodName) {
        auto pos = methodName.rfind("::");
        if (pos == std::string_view::npos) return std::string{};
        std::string_view type = methodName.substr(0, pos);
        auto dot = type.rfind('.');
        return std::string(dot != std::string_view::npos ? type.substr(dot + 1) : type);
    }

    // True if methodName is op_Explicit or op_Implicit (a conversion operator).
    static bool IsConversionOperator(std::string_view methodName) {
        auto pos = methodName.rfind("::");
        std::string_view op = (pos != std::string_view::npos) ? methodName.substr(pos + 2) : methodName;
        return op == "op_Explicit" || op == "op_Implicit";
    }

    // "Namespace.Type::.ctor" -> "new Namespace.Type(args)"; other members and
    // plain methods -> "Namespace.Type.Member(args)". A static operator call
    // (op_Equality etc.) renders as `(arg0 op arg1)`; a unary operator as
    // `op arg0`; a conversion operator (op_Explicit/op_Implicit) as
    // `(TargetType)arg0`.
    std::string CallText(const Call& call) {
        if (!call.IsInstanceCall) {
            // A static conversion operator: op_Explicit/op_Implicit(value) ->
            // (TargetType)value. The target type is the declaring type.
            if (call.Arguments.size() == 1 && IsConversionOperator(call.MethodName)) {
                std::string targetType = DeclaringTypeName(call.MethodName);
                return "(" + targetType + ")(" +
                       (call.Arguments[0] ? Expr(*call.Arguments[0]) : std::string("(default)")) + ")";
            }
            // A static property accessor: Type.get_X() -> Type.X ;
            // Type.set_X(value) -> not common (C# set_ is instance); skip.
            if (call.Arguments.size() <= 1) {
                std::string prop = AccessorPropertyName(call.MethodName);
                if (!prop.empty()) {
                    auto pos = call.MethodName.rfind("::");
                    std::string_view type = (pos != std::string_view::npos)
                        ? std::string_view(call.MethodName).substr(0, pos) : std::string_view{};
                    auto dot = type.rfind('.');
                    return std::string(dot != std::string_view::npos ? type.substr(dot + 1) : type) + "." + prop;
                }
            }
            // A static unary operator: op_UnaryNegation(a) -> (-a), etc.
            if (call.Arguments.size() == 1) {
                if (const char* sym = UnaryOperatorSymbol(call.MethodName)) {
                    std::string arg = call.Arguments[0] ? Expr(*call.Arguments[0]) : std::string("(default)");
                    return std::string(sym) + arg;
                }
            }
            // A static binary operator: Namespace.Type::op_X(a, b) -> (a op b)
            if (call.Arguments.size() == 2) {
                if (const char* sym = OperatorSymbol(call.MethodName)) {
                    return "(" + (call.Arguments[0] ? Expr(*call.Arguments[0]) : std::string("(default)")) +
                           " " + sym + " " +
                           (call.Arguments[1] ? Expr(*call.Arguments[1]) : std::string("(default)")) + ")";
                }
            }
        }
        std::string name = call.MethodName;
        std::string prefix;
        static const std::string ctorSuffix = "::.ctor";
        static const std::string cctorSuffix = "::.cctor";
        if (name.size() > ctorSuffix.size() &&
            name.compare(name.size() - ctorSuffix.size(), ctorSuffix.size(), ctorSuffix) == 0) {
            name.erase(name.size() - ctorSuffix.size());
            prefix = "new ";
        } else if (name.size() > cctorSuffix.size() &&
                   name.compare(name.size() - cctorSuffix.size(), cctorSuffix.size(), cctorSuffix) == 0) {
            name.erase(name.size() - cctorSuffix.size());
            prefix = "new ";
        }
        std::string text = prefix + FlattenMetadataName(std::move(name)) + "(";
        for (std::size_t i = 0; i < call.Arguments.size(); ++i) {
            if (i) text += ", ";
            text += call.Arguments[i] ? Expr(*call.Arguments[i]) : "(default)";
        }
        text += ')';
        return text;
    }

    // The short method name (after "::") for an instance call: receiver.Method.
    static std::string ShortMethodName(std::string_view full) {
        auto pos = full.rfind("::");
        return pos != std::string_view::npos ? std::string(full.substr(pos + 2)) : std::string(full);
    }

    // The property name for a get_/set_ accessor method, or "" if not an
    // accessor. `get_Major` -> `Major`, `set_Value` -> `Value`.
    static std::string AccessorPropertyName(std::string_view full) {
        auto pos = full.rfind("::");
        std::string_view member = (pos != std::string_view::npos) ? full.substr(pos + 2) : full;
        if (member.size() > 4 && member.substr(0, 4) == "get_")
            return std::string(member.substr(4));
        if (member.size() > 4 && member.substr(0, 4) == "set_")
            return std::string(member.substr(4));
        return std::string{};
    }

    // An instance call (call/callvirt, not newobj) renders as
    // `receiver.Method(restArgs)`; the receiver is Arguments[0]. A ref/deref
    // receiver (`&V`, `*(&V)`) is parenthesized so the member access binds.
    // Static calls and newobj go through CallText. A property accessor
    // (get_X with 0 extra args / set_X with 1 extra arg) renders as
    // `receiver.X` / `receiver.X = value`.
    std::string InstanceCallText(const Call& call) {
        if (call.Arguments.empty() || !call.Arguments[0])
            return CallText(call);
        // Property accessor: get_X(receiver) -> receiver.X ;
        // set_X(receiver, value) -> receiver.X = value.
        std::string prop = AccessorPropertyName(call.MethodName);
        if (!prop.empty()) {
            std::string recv = Expr(*call.Arguments[0]);
            bool needsParens = !recv.empty() && (recv[0] == '&' || recv[0] == '*');
            std::string target = (needsParens ? "(" + recv + ")" : recv) + "." + prop;
            if (call.MethodName.size() >= 4) {
                auto pos = call.MethodName.rfind("::");
                std::string_view member = (pos != std::string_view::npos)
                    ? std::string_view(call.MethodName).substr(pos + 2) : std::string_view(call.MethodName);
                if (member.substr(0, 4) == "set_" && call.Arguments.size() >= 2) {
                    return target + " = " + (call.Arguments[1] ? Expr(*call.Arguments[1]) : std::string("(default)"));
                }
            }
            return target;
        }
        std::string recv = Expr(*call.Arguments[0]);
        // A ref/deref receiver renders with a leading `ref `/`&`/`*`, which
        // binds looser than `.` -- parenthesize so the member access wins.
        bool needsParens = recv.size() >= 4 && recv.compare(0, 4, "ref ") == 0;
        if (!needsParens && !recv.empty()) needsParens = (recv[0] == '&' || recv[0] == '*');
        std::string text = (needsParens ? "(" + recv + ")" : recv) +
                           "." + ShortMethodName(call.MethodName) + "(";
        for (std::size_t i = 1; i < call.Arguments.size(); ++i) {
            if (i > 1) text += ", ";
            text += call.Arguments[i] ? Expr(*call.Arguments[i]) : "(default)";
        }
        text += ')';
        return text;
    }

    // A `.ctor` call used as a statement (void, in a block) whose first arg is
    // `this` is a base/sibling constructor call; render it as `base(args)` (the
    // `this` arg is implicit). A newobj (which pushes the new object) is an
    // expression and goes through CallText as `new Type(args)`. A `.ctor`
    // statement without a `this` first arg (e.g. a synthetic test) falls back
    // to `new Type(args)`.
    std::string CtorCallStatementText(const Call& call) {
        std::string name = call.MethodName;
        static const std::string ctorSuffix = "::.ctor";
        if (name.size() > ctorSuffix.size() &&
            name.compare(name.size() - ctorSuffix.size(), ctorSuffix.size(), ctorSuffix) == 0 &&
            !call.Arguments.empty() && call.Arguments[0] &&
            call.Arguments[0]->Op == OpCode::LdLoc) {
            auto& ld = static_cast<const LdLoc&>(*call.Arguments[0]);
            if (ld.Variable && ld.Variable->Name == "this") {
                std::string text = "base(";
                for (std::size_t i = 1; i < call.Arguments.size(); ++i) {
                    if (i > 1) text += ", ";
                    text += call.Arguments[i] ? Expr(*call.Arguments[i]) : "(default)";
                }
                text += ')';
                return text;
            }
        }
        return call.IsInstanceCall ? InstanceCallText(call) : CallText(call);
    }

    // `V = V op expr` -> `V op= expr` (or `V++`/`V--` for +/- 1) when the
    // binary's left is a load of the same variable. A plain `V = expr` (no
    // self-load, or an operator C# has no compound form for) returns ` = expr`.
    // `ldc.i4 0`/`ldc.i4 1` in the return position of a Boolean-returning
    // function renders as `false`/`true` (the IL idiom for bool return values).
    std::string BoolLiteralFromReturn(const ILInstruction* value) const {
        if (returnTypeName_ != "bool" || !value || value->Op != OpCode::LdcI4) return {};
        auto v = static_cast<const LdcI4*>(value)->Value;
        if (v == 0) return "false";
        if (v == 1) return "true";
        return {};
    }

    // `ldc.i4 0`/`ldc.i4 1` stored into a Boolean variable renders as
    // `false`/`true` (the IL idiom for Boolean constants). Returns "" when the
    // value is not a Boolean literal for a Boolean variable.
    static std::string BoolLiteralText(const ILVariable* var, const ILInstruction* value) {
        if (!var || !var->Type || !value || value->Op != OpCode::LdcI4) return {};
        auto* k = dynamic_cast<const TypeSystem::KnownType*>(var->Type.get());
        if (!k || k->Code() != TypeSystem::KnownTypeCode::Boolean) return {};
        auto v = static_cast<const LdcI4*>(value)->Value;
        if (v == 0) return "false";
        if (v == 1) return "true";
        return {};
    }

    std::string AssignmentText(const StLoc& st, const std::string& name) {
        (void)name;
        auto* bin = dynamic_cast<const BinaryNumericInstruction*>(st.Value.get());
        if (!bin || !bin->Left || bin->Left->Op != OpCode::LdLoc) {
            auto bl = BoolLiteralText(st.Variable.get(), st.Value.get());
            return " = " + (bl.empty() ? Expr(*st.Value) : bl);
        }
        auto* ld = static_cast<const LdLoc*>(bin->Left.get());
        if (!ld->Variable || !st.Variable || ld->Variable.get() != st.Variable.get()) {
            auto bl = BoolLiteralText(st.Variable.get(), st.Value.get());
            return " = " + (bl.empty() ? Expr(*st.Value) : bl);
        }
        const char* op = nullptr;
        switch (bin->Operator) {
            case BinaryNumericOperator::Add: op = "+"; break;
            case BinaryNumericOperator::Sub: op = "-"; break;
            case BinaryNumericOperator::Mul: op = "*"; break;
            case BinaryNumericOperator::Div: op = "/"; break;
            case BinaryNumericOperator::Rem: op = "%"; break;
            case BinaryNumericOperator::BitAnd: op = "&"; break;
            case BinaryNumericOperator::BitOr: op = "|"; break;
            case BinaryNumericOperator::BitXor: op = "^"; break;
            case BinaryNumericOperator::ShiftLeft: op = "<<"; break;
            case BinaryNumericOperator::ShiftRight: op = ">>"; break;
            default: return " = " + Expr(*st.Value);
        }
        if ((bin->Operator == BinaryNumericOperator::Add || bin->Operator == BinaryNumericOperator::Sub) &&
            bin->Right && bin->Right->Op == OpCode::LdcI4 &&
            static_cast<const LdcI4*>(bin->Right.get())->Value == 1) {
            return bin->Operator == BinaryNumericOperator::Add ? "++" : "--";
        }
        return " " + std::string(op) + "= " + (bin->Right ? Expr(*bin->Right) : std::string("(default)"));
    }

    // The C# form of a store target: a field reference, an array element, or
    // a dereferenced pointer.
    // If `target` is a load of a byref variable (a managed ref to its own
    // type), the C# is just the variable name -- the indirection is implicit.
    // Handles `*(this)` in value-type methods and `*(byrefParam)`.
    static std::string ByRefVarName(const ILInstruction& target) {
        auto* ld = dynamic_cast<const LdLoc*>(&target);
        if (!ld || !ld->Variable) return std::string{};
        // The implicit `this` parameter: ldobj(ldarg this) in a value-type
        // method loads `this` as a value -- render as `this` (the indirection
        // is implicit). The reader leaves this's type null, so check by name/kind.
        if (ld->Variable->Kind == VariableKind::Parameter && ld->Variable->Name == "this")
            return "this";
        if (!ld->Variable->Type) return std::string{};
        if (dynamic_cast<const TypeSystem::ByReferenceType*>(ld->Variable->Type.get()))
            return ld->Variable->Name;
        return std::string{};
    }

    std::string StoreTargetText(const ILInstruction& target) {
        if (target.Op == OpCode::LdFlda) {
            const auto& f = static_cast<const LdFlda&>(target);
            std::string field = FlattenMetadataName(f.FieldName);
            std::string obj = f.Target ? Expr(*f.Target) : "(default)";
            return obj == "this" ? SimpleName(field) : obj + "." + SimpleName(field);
        }
        if (target.Op == OpCode::LdsFlda) {
            return FlattenMetadataName(static_cast<const LdsFlda&>(target).FieldName);
        }
        if (target.Op == OpCode::LdElema) return ElementAccess(static_cast<const LdElema&>(target));
        auto byref = ByRefVarName(target);
        if (!byref.empty()) return byref;
        // A pointer-typed local: `*ptr` (the deref is explicit but needs no
        // parens around a bare name).
        if (auto* ld = dynamic_cast<const LdLoc*>(&target))
            if (ld->Variable && ld->Variable->Type &&
                dynamic_cast<const TypeSystem::PointerType*>(ld->Variable->Type.get()))
                return "*" + ld->Variable->Name;
        return "*(" + Expr(target) + ")";
    }

    // "Namespace.Type::name" -> "name" (the field/member segment).
    static std::string SimpleName(const std::string& flattened) {
        auto dot = flattened.rfind('.');
        return dot == std::string::npos ? flattened : flattened.substr(dot + 1);
    }

    std::string ElementAccess(const LdElema& elema) {
        std::string text = elema.Array ? Expr(*elema.Array) : "(default)";
        text += '[';
        for (std::size_t i = 0; i < elema.Indices.size(); ++i) {
            if (i) text += ", ";
            text += elema.Indices[i] ? Expr(*elema.Indices[i]) : "(default)";
        }
        text += ']';
        return text;
    }

    // Negate a condition's text for the seed: a Comp `(a op b)` becomes
    // `(a op.Negate b)` (re-render with the negated kind), else wrap as `!(cond)`.
    std::string NegateCondText(const ILInstruction& cond) {
        if (auto* comp = dynamic_cast<const Comp*>(&cond)) {
            std::string left = comp->Left ? Expr(*comp->Left) : "(default)";
            std::string right = comp->Right ? Expr(*comp->Right) : "(default)";
            const char* op = "==";
            switch (NegateComparison(comp->Kind)) {
                case ComparisonKind::Equality: op = "=="; break;
                case ComparisonKind::Inequality: op = "!="; break;
                case ComparisonKind::LessThan: op = "<"; break;
                case ComparisonKind::LessThanOrEqual: op = "<="; break;
                case ComparisonKind::GreaterThan: op = ">"; break;
                case ComparisonKind::GreaterThanOrEqual: op = ">="; break;
            }
            return "(" + left + " " + op + " " + right + ")";
        }
        return "!(" + Expr(cond) + ")";
    }

    // Strip one layer of outer parens from a string if they enclose the whole
    // string with balanced nesting (e.g. `(a + b)` -> `a + b`, but
    // `(a)(b)` stays). Used for assignment values and conditions.
    static std::string StripOuterParens(std::string e) {
        if (e.size() < 2 || e.front() != '(' || e.back() != ')') return e;
        int depth = 0;
        for (std::size_t i = 0; i < e.size(); ++i) {
            if (e[i] == '(') ++depth;
            else if (e[i] == ')') { --depth; if (depth == 0 && i + 1 < e.size()) return e; }
        }
        return e.substr(1, e.size() - 2);
    }

    // The condition expression for an `if`/`while`: strip redundant outer
    // parens so `if ((cond))` becomes `if (cond)`.
    std::string CondExpr(const ILInstruction& inst) {
        return StripOuterParens(Expr(inst));
    }

    std::string Expr(const ILInstruction& inst) {
        switch (inst.Op) {
            case OpCode::LdLoc: {
                const auto& ld = static_cast<const LdLoc&>(inst);
                return ld.Variable ? ld.Variable->Name : "?";
            }
            case OpCode::LdLoca: {
                const auto& ld = static_cast<const LdLoca&>(inst);
                // `ldloca V` is the IL idiom for `ref V` (a byref argument or
                // an address-of). Render as `ref V` (the C# form), not `&V`
                // (the IL form).
                return "ref " + (ld.Variable ? ld.Variable->Name : std::string("?"));
            }
            case OpCode::LdcI4:
                return std::to_string(static_cast<const LdcI4&>(inst).Value);
            case OpCode::LdcI8:
                return std::to_string(static_cast<const LdcI8&>(inst).Value);
            case OpCode::LdcDecimal:
                // The C# decimal literal form (`1m`, `0m`, `-1m`, `1.5m`),
                // faithful to the real back end's VisitLdcDecimal
                // (ConvertConstantValue -> a PrimitiveExpression with the `m`
                // suffix). The trailing zeros from the scale are preserved
                // (System.Decimal.ToString semantics).
                return static_cast<const LdcDecimal&>(inst).Value.ToString() + "m";
            case OpCode::LdcF4: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.9g", static_cast<const LdcF4&>(inst).Value);
                return std::string(buf) + "f";
            }
            case OpCode::LdcF8: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.17g", static_cast<const LdcF8&>(inst).Value);
                return buf;
            }
            case OpCode::LdStr:
                return EscapeStringLiteral(static_cast<const LdStr&>(inst).Value);
            case OpCode::LdNull:
                return "null";
            case OpCode::Call:
                return static_cast<const Call&>(inst).IsInstanceCall
                    ? InstanceCallText(static_cast<const Call&>(inst))
                    : CallText(static_cast<const Call&>(inst));
            case OpCode::Comp: {
                const auto& comp = static_cast<const Comp&>(inst);
                // `comp(eq, ldloc boolVar, ldc.i4 0)` is `!boolVar` (a Boolean
                // negation); `comp(ne, ldloc boolVar, 0)` is just `boolVar`.
                if (comp.Right && comp.Right->Op == OpCode::LdcI4 &&
                    static_cast<const LdcI4*>(comp.Right.get())->Value == 0 &&
                    comp.Left && comp.Left->Op == OpCode::LdLoc) {
                    auto* ld = static_cast<const LdLoc*>(comp.Left.get());
                    if (ld->Variable && ld->Variable->Type &&
                        dynamic_cast<const TypeSystem::KnownType*>(ld->Variable->Type.get()) &&
                        static_cast<const TypeSystem::KnownType*>(ld->Variable->Type.get())->Code() ==
                            TypeSystem::KnownTypeCode::Boolean) {
                        return comp.Kind == ComparisonKind::Equality ? "!" + ld->Variable->Name : ld->Variable->Name;
                    }
                }
                const char* op = "==";
                switch (comp.Kind) {
                    case ComparisonKind::Equality: op = "=="; break;
                    case ComparisonKind::Inequality: op = "!="; break;
                    case ComparisonKind::LessThan: op = "<"; break;
                    case ComparisonKind::LessThanOrEqual: op = "<="; break;
                    case ComparisonKind::GreaterThan: op = ">"; break;
                    case ComparisonKind::GreaterThanOrEqual: op = ">="; break;
                }
                return "(" + (comp.Left ? Expr(*comp.Left) : "(default)") + " " + op + " " +
                       (comp.Right ? Expr(*comp.Right) : "(default)") + ")";
            }
            case OpCode::BinaryNumericInstruction: {
                const auto& bin = static_cast<const BinaryNumericInstruction&>(inst);
                // `0 - x` (and `0.0 - x`) is the IL for unary negation `-x`.
                if (bin.Operator == BinaryNumericOperator::Sub && bin.Left &&
                    (bin.Left->Op == OpCode::LdcI4 && static_cast<const LdcI4*>(bin.Left.get())->Value == 0)) {
                    return "-" + Expr(*bin.Right);
                }
                const char* op = "+";
                switch (bin.Operator) {
                    case BinaryNumericOperator::Add: op = "+"; break;
                    case BinaryNumericOperator::Sub: op = "-"; break;
                    case BinaryNumericOperator::Mul: op = "*"; break;
                    case BinaryNumericOperator::Div: op = "/"; break;
                    case BinaryNumericOperator::Rem: op = "%"; break;
                    case BinaryNumericOperator::BitAnd: op = "&"; break;
                    case BinaryNumericOperator::BitOr: op = "|"; break;
                    case BinaryNumericOperator::BitXor: op = "^"; break;
                    case BinaryNumericOperator::ShiftLeft: op = "<<"; break;
                    case BinaryNumericOperator::ShiftRight: op = ">>"; break;
                    default: op = "?"; break;
                }
                std::string text = "(" + (bin.Left ? Expr(*bin.Left) : "(default)") + " " + op + " " +
                                   (bin.Right ? Expr(*bin.Right) : "(default)") + ")";
                if (bin.CheckForOverflow) text = "checked" + text;
                return text;
            }
            case OpCode::Conv: {
                const auto& conv = static_cast<const Conv&>(inst);
                // conv.i4(ldlen) is the IL for `array.Length` (the raw ldlen
                // pushes a native int; the cast to i4 is implicit in C#). The
                // VisitConv `conv.iN(ldlen)` fold (ExpressionTransforms) folds
                // this to a single LdLen(I4, ..) in the pipeline, so this special
                // case is a fallback for the pre-fold shape (e.g. a test that
                // calls the seed directly).
                if (conv.ResultType() == StackType::I4 && conv.Argument &&
                    conv.Argument->Op == OpCode::LdLen) {
                    return Expr(*conv.Argument);
                }
                return "(" + std::string(ConvTargetName(conv.ResultType())) + ")(" +
                       (conv.Argument ? Expr(*conv.Argument) : "(default)") + ")";
            }
            case OpCode::CastClass: {
                const auto& cast = static_cast<const CastClass&>(inst);
                return "(" + TypeDisplayName(cast.Type) + ")(" +
                       (cast.Argument ? Expr(*cast.Argument) : "(default)") + ")";
            }
            case OpCode::UnboxAny: {
                const auto& unbox = static_cast<const UnboxAny&>(inst);
                return "(" + TypeDisplayName(unbox.Type) + ")(" +
                       (unbox.Argument ? Expr(*unbox.Argument) : "(default)") + ")";
            }
            case OpCode::Box:
                // Boxing is implicit in C#.
                return static_cast<const Box&>(inst).Argument
                           ? Expr(*static_cast<const Box&>(inst).Argument)
                           : "(default)";
            case OpCode::IsInst: {
                const auto& isinst = static_cast<const IsInst&>(inst);
                return "(" + (isinst.Argument ? Expr(*isinst.Argument) : "(default)") +
                       " as " + TypeDisplayName(isinst.Type) + ")";
            }
            case OpCode::LdLen: {
                const auto& ld = static_cast<const LdLen&>(inst);
                return (ld.Argument ? Expr(*ld.Argument) : "(default)") + std::string(".Length");
            }
            case OpCode::LdObj: {
                const auto& ld = static_cast<const LdObj&>(inst);
                return ld.Target ? LoadTargetText(*ld.Target)
                                 : std::string("(default)");
            }
            case OpCode::LdFlda: {
                const auto& f = static_cast<const LdFlda&>(inst);
                std::string field = FlattenMetadataName(f.FieldName);
                std::string obj = f.Target ? Expr(*f.Target) : "(default)";
                return obj == "this" ? SimpleName(field) : obj + "." + SimpleName(field);
            }
            case OpCode::LdsFlda:
                return FlattenMetadataName(static_cast<const LdsFlda&>(inst).FieldName);
            case OpCode::LdElema:
                return ElementAccess(static_cast<const LdElema&>(inst));
            case OpCode::NewArr: {
                const auto& arr = static_cast<const NewArr&>(inst);
                std::string text = "new " + TypeDisplayName(arr.Type) + "[";
                for (std::size_t i = 0; i < arr.Indices.size(); ++i) {
                    if (i) text += ", ";
                    text += arr.Indices[i] ? Expr(*arr.Indices[i]) : "(default)";
                }
                text += ']';
                return text;
            }
            case OpCode::LdFtn:
                return FlattenMetadataName(static_cast<const LdFtn&>(inst).MethodName);
            case OpCode::LdVirtFtn:
                return FlattenMetadataName(static_cast<const LdVirtFtn&>(inst).MethodName);
            case OpCode::LdVirtDelegate: {
                // A virtual delegate construction renders as
                // `new DelegateType(target.Method)` -- the real back end's
                // VisitLdVirtDelegate (CallBuilder.Build -> HandleDelegateConstruction)
                // folds the target and the virtual method into a `target.Method`
                // method group inside the `new DelegateType(...)`. The delegate
                // type renders as its reflection name (matching the existing
                // newobj rendering `new System.Action(...)`); the method is the
                // short name (after `::`) so `target.Bar` is the C# method-group
                // form.
                const auto& d = static_cast<const LdVirtDelegate&>(inst);
                std::string typeName = d.Type ? d.Type->ReflectionName() : std::string("var");
                std::string target = d.Argument ? Expr(*d.Argument) : std::string("(default)");
                return "new " + typeName + "(" + target + "." +
                       ShortMethodName(d.MethodName) + ")";
            }
            case OpCode::SizeOf:
                return "sizeof(" + static_cast<const SizeOf&>(inst).TypeName + ")";
            case OpCode::LdTypeToken:
                return "typeof(" + FlattenMetadataName(static_cast<const LdTypeToken&>(inst).TokenName) + ")";
            case OpCode::DefaultValue: {
                const auto& dv = static_cast<const DefaultValue&>(inst);
                return "default(" + CSharpTypeName(dv.Type) + ")";
            }
            case OpCode::MatchInstruction: {
                // A MatchInstruction renders as the C# `is` pattern
                // `testedOperand is <pattern>`, matching the real back end's
                // VisitMatchInstruction (a BinaryOperatorExpression with the Is
                // operator). The pattern is: `var v` (IsVar), `T v` (CheckType
                // + designator), `{} v` (CheckNotNull, no type), or `T` (a pure
                // type test with no designator). Sub-patterns (recursive
                // patterns) and deconstruct patterns are deferred.
                const auto& m = static_cast<const MatchInstruction&>(inst);
                std::string lhs = m.TestedOperand ? Expr(*m.TestedOperand) : std::string("(default)");
                std::string varName = m.Variable ? m.Variable->Name : std::string("_");
                std::string pattern;
                if (m.IsVar()) {
                    // `expr is var x`
                    pattern = "var " + varName;
                } else if (m.CheckType) {
                    std::string typeName = m.Variable && m.Variable->Type
                        ? CSharpTypeName(m.Variable->Type) : std::string("var");
                    if (m.HasDesignator()) {
                        // `expr is T x` (a CheckNotNull + CheckType is still
                        // `is T x`; the non-null is implied by the type test for
                        // reference types).
                        pattern = typeName + " " + varName;
                    } else {
                        // `expr is T` (pure type test, no designator).
                        pattern = typeName;
                    }
                } else if (m.CheckNotNull) {
                    // `expr is {} x` (non-null pattern, no type).
                    pattern = "{} " + varName;
                } else {
                    // No flags and no sub-patterns but not IsVar (e.g. an empty
                    // recursive pattern): render the designator as `var x`.
                    pattern = "var " + varName;
                }
                return lhs + " is " + pattern;
            }
            case OpCode::IfInstruction: {
                // The expression form of an IfInstruction: the conditional
                // operator `cond ? true : false`. This arises when
                // ExpressionTransforms.HandleConditionalOperator folds
                // `if (cond) stloc A(V1) else stloc A(V2)` into
                // `stloc A(if (!cond) V2 else V1))` (a StLoc whose value is an
                // IfInstruction). A Block arm (the inlined-fall-through shape)
                // renders its single instruction; a bare expression arm renders
                // directly. The statement form (`if (cond) { ... } else { ... }`)
                // is emitted by the Statement path and does not reach Expr.
                const auto& iff = static_cast<const IfInstruction&>(inst);
                auto ArmExpr = [&](const std::unique_ptr<ILInstruction>& arm) -> std::string {
                    if (!arm) return "(default)";
                    if (arm->Op == OpCode::Block) {
                        auto* blk = static_cast<const Block*>(arm.get());
                        if (!blk->FinalInstruction && blk->Instructions.size() == 1)
                            return Expr(*blk->Instructions[0]);
                    }
                    return Expr(*arm);
                };
                std::string cond = iff.Condition ? Expr(*iff.Condition) : "(default)";
                return "(" + cond + " ? " + ArmExpr(iff.TrueInst) +
                       " : " + ArmExpr(iff.FalseInst) + ")";
            }
            case OpCode::NullCoalescingInstruction: {
                // The C# `??` (null-coalescing) operator: `value ?? fallback`.
                // Faithful to the real back end's VisitNullCoalescingInstruction
                // (a BinaryOperatorExpression with the NullCoalescing operator).
                // The Kind (Ref / Nullable / NullableWithValueFallback) is a
                // semantic flavour the back end uses to pick the conversion; the
                // surface syntax is the same `??` for all three.
                const auto& nc = static_cast<const NullCoalescingInstruction&>(inst);
                std::string value = nc.ValueInst ? Expr(*nc.ValueInst) : std::string("(default)");
                std::string fallback = nc.FallbackInst ? Expr(*nc.FallbackInst) : std::string("(default)");
                return "(" + value + " ?? " + fallback + ")";
            }
            case OpCode::ThreeValuedBoolAnd: {
                // Three-valued logic `&` on bool?: `left & right`. Faithful to
                // the real back end's VisitThreeValuedBoolAnd (a
                // BinaryOperatorExpression with the BitwiseAnd operator; the
                // operands are converted to bool?). Unlike logic.and() this does
                // not short-circuit (the three-valued truth tables require both
                // sides to detect a null result).
                const auto& tv = static_cast<const ThreeValuedBoolAnd&>(inst);
                std::string left = tv.Left ? Expr(*tv.Left) : std::string("(default)");
                std::string right = tv.Right ? Expr(*tv.Right) : std::string("(default)");
                return "(" + left + " & " + right + ")";
            }
            case OpCode::ThreeValuedBoolOr: {
                // Three-valued logic `|` on bool?: `left | right`. Faithful to
                // the real back end's VisitThreeValuedBoolOr (a
                // BinaryOperatorExpression with the BitwiseOr operator).
                const auto& tv = static_cast<const ThreeValuedBoolOr&>(inst);
                std::string left = tv.Left ? Expr(*tv.Left) : std::string("(default)");
                std::string right = tv.Right ? Expr(*tv.Right) : std::string("(default)");
                return "(" + left + " | " + right + ")";
            }
            case OpCode::UserDefinedLogicOperator: {
                // A user-defined short-circuiting logic operator: `left && right`
                // for op_BitwiseAnd, `left || right` for op_BitwiseOr. Faithful
                // to the real back end's VisitUserDefinedLogicOperator (a
                // BinaryOperatorExpression with the ConditionalAnd /
                // ConditionalOr operator derived from the method name). The
                // operator is the short method name (after "::"), matching
                // GetBinaryOperatorTypeFromMetadataName.
                const auto& ul = static_cast<const UserDefinedLogicOperator&>(inst);
                std::string left = ul.Left ? Expr(*ul.Left) : std::string("(default)");
                std::string right = ul.Right ? Expr(*ul.Right) : std::string("(default)");
                auto pos = ul.MethodName.rfind("::");
                std::string shortName = (pos != std::string::npos)
                    ? ul.MethodName.substr(pos + 2) : ul.MethodName;
                const char* op = (shortName == "op_BitwiseOr") ? "||" : "&&";
                return "(" + left + " " + op + " " + right + ")";
            }
            case OpCode::NullableRewrap: {
                // The C# null-conditional rewrap is implicit in the `?.`
                // surface syntax: `x?.M()` is `nullable.rewrap(M(nullable.unwrap(x)))`,
                // and the rewrap (which evaluates to null when an inner unwrap
                // took the null branch) does not appear as a separate operator.
                // The real back end renders it as a UnaryOperatorExpression
                // (NullConditionalRewrap) that the output visitor elides. The
                // seed renders the access-chain argument directly (the Box
                // precedent -- boxing is implicit in C#; so is the rewrap).
                const auto& nr = static_cast<const NullableRewrap&>(inst);
                return nr.Argument ? Expr(*nr.Argument) : std::string("(default)");
            }
            case OpCode::NullableUnwrap: {
                // The C# null-conditional `?.` is the NullableUnwrap. The real
                // back end renders it as a UnaryOperatorExpression (NullConditional)
                // whose postfix `?` sits on the receiver of the surrounding
                // member access (`x?.M`); the seed has no member-access context
                // here, so it renders the unwrapped value with a trailing `?`
                // to keep the null-conditional visible (a placeholder until the
                // real back end lands).
                const auto& nu = static_cast<const NullableUnwrap&>(inst);
                return (nu.Argument ? Expr(*nu.Argument) : std::string("(default)")) + "?";
            }
            case OpCode::NumericCompoundAssign: {
                // A numeric compound assignment `target op= value` (or the
                // post-increment/decrement `target++`/`target--` for Add/Sub with
                // a ldc.i4 1 RHS in EvaluatesToOldValue mode). Faithful to the
                // real back end's VisitNumericCompoundAssign (an
                // AssignmentExpression with the operator derived from Operator).
                // The Address Target is an LdLoca; the back end loads the value
                // at that address (LdObj(Target, Type)), so the seed renders the
                // target variable's bare name (stripping the address `&`).
                const auto& ca = static_cast<const NumericCompoundAssign&>(inst);
                std::string target;
                if (ca.TargetKind == CompoundTargetKind::Address &&
                    ca.Target && ca.Target->Op == OpCode::LdLoca) {
                    const auto& lda = static_cast<const LdLoca&>(*ca.Target);
                    target = lda.Variable ? lda.Variable->Name : std::string("?");
                } else {
                    target = ca.Target ? Expr(*ca.Target) : std::string("(default)");
                }
                std::string value = ca.Value ? Expr(*ca.Value) : std::string("(default)");
                // The post-increment/decrement: Add/Sub with a ldc.i4 1 RHS in
                // the EvaluatesToOldValue mode renders as `target++`/`target--`.
                if (ca.EvalMode == CompoundEvalMode::EvaluatesToOldValue &&
                    (ca.Operator == BinaryNumericOperator::Add ||
                     ca.Operator == BinaryNumericOperator::Sub) &&
                    ca.Value && ca.Value->Op == OpCode::LdcI4) {
                    const auto& c = static_cast<const LdcI4&>(*ca.Value);
                    if (c.Value == 1) {
                        return target + (ca.Operator == BinaryNumericOperator::Add ? "++" : "--");
                    }
                }
                const char* op = "?=";
                switch (ca.Operator) {
                    case BinaryNumericOperator::Add: op = "+="; break;
                    case BinaryNumericOperator::Sub: op = "-="; break;
                    case BinaryNumericOperator::Mul: op = "*="; break;
                    case BinaryNumericOperator::Div: op = "/="; break;
                    case BinaryNumericOperator::Rem: op = "%="; break;
                    case BinaryNumericOperator::BitAnd: op = "&="; break;
                    case BinaryNumericOperator::BitOr: op = "|="; break;
                    case BinaryNumericOperator::BitXor: op = "^="; break;
                    case BinaryNumericOperator::ShiftLeft: op = "<<="; break;
                    case BinaryNumericOperator::ShiftRight: op = ">>="; break;
                }
                return target + " " + op + " " + value;
            }
            case OpCode::UserDefinedCompoundAssign: {
                // A user-defined compound assignment built from a user-defined
                // operator call. Faithful to the real back end's
                // VisitUserDefinedCompoundAssign: a 1-arg op_Increment/
                // op_Decrement renders as the unary `target++`/`++target`/
                // `target--`/`--target` (EvaluatesToOldValue = postfix,
                // EvaluatesToNewValue = prefix); a 2-arg operator renders as
                // `target op= value` (op_Addition -> `+=`, ...); `string.Concat`
                // renders as `target += value`. The operator is derived from the
                // method name (the part after "::"), matching the C#
                // GetUnaryOperatorTypeFromMetadataName /
                // GetAssignmentOperatorTypeFromMetadataName.
                const auto& ca = static_cast<const UserDefinedCompoundAssign&>(inst);
                std::string target;
                if (ca.TargetKind == CompoundTargetKind::Address &&
                    ca.Target && ca.Target->Op == OpCode::LdLoca) {
                    const auto& lda = static_cast<const LdLoca&>(*ca.Target);
                    target = lda.Variable ? lda.Variable->Name : std::string("?");
                } else {
                    target = ca.Target ? Expr(*ca.Target) : std::string("(default)");
                }
                // The short method name (after "::") selects the operator.
                auto pos = ca.MethodName.rfind("::");
                std::string shortName = (pos != std::string::npos)
                    ? ca.MethodName.substr(pos + 2) : ca.MethodName;
                // The 1-arg increment/decrement operators render as the unary
                // forms (postfix for EvaluatesToOldValue, prefix for NewValue).
                if (shortName == "op_Increment" || shortName == "op_CheckedIncrement") {
                    return (ca.EvalMode == CompoundEvalMode::EvaluatesToOldValue)
                        ? target + "++" : "++" + target;
                }
                if (shortName == "op_Decrement" || shortName == "op_CheckedDecrement") {
                    return (ca.EvalMode == CompoundEvalMode::EvaluatesToOldValue)
                        ? target + "--" : "--" + target;
                }
                // The 2-arg operators (and string.Concat) render as
                // `target op= value`.
                std::string value = ca.Value ? Expr(*ca.Value) : std::string("(default)");
                const char* op = "?=";
                if (shortName == "Concat" || shortName == "op_Addition") op = "+=";
                else if (shortName == "op_Subtraction") op = "-=";
                else if (shortName == "op_Multiply") op = "*=";
                else if (shortName == "op_Division") op = "/=";
                else if (shortName == "op_Modulus") op = "%=";
                else if (shortName == "op_BitwiseAnd") op = "&=";
                else if (shortName == "op_BitwiseOr") op = "|=";
                else if (shortName == "op_ExclusiveOr") op = "^=";
                else if (shortName == "op_LeftShift") op = "<<=";
                else if (shortName == "op_RightShift") op = ">>=";
                else if (shortName == "op_UnsignedRightShift") op = ">>>=";
                return target + " " + op + " " + value;
            }
            default:
                return "(default)/*op=" + std::to_string(static_cast<int>(inst.Op)) + "*/";
        }
    }

    // The expression form of a load target (LdObj child): field, array
    // element, or dereference.
    std::string LoadTargetText(const ILInstruction& target) {
        if (target.Op == OpCode::LdFlda || target.Op == OpCode::LdsFlda ||
            target.Op == OpCode::LdElema) {
            return Expr(target);
        }
        auto byref = ByRefVarName(target);
        if (!byref.empty()) return byref;
        if (auto* ld = dynamic_cast<const LdLoc*>(&target))
            if (ld->Variable && ld->Variable->Type &&
                dynamic_cast<const TypeSystem::PointerType*>(ld->Variable->Type.get()))
                return "*" + ld->Variable->Name;
        return "*(" + Expr(target) + ")";
    }
};

} // namespace

// A C# type name for a declaration: the C# keyword for primitives (int, bool,
// string, ...), the short type name otherwise (a seed approximation -- real
// C# would carry a using directive), recursing into array/byref. `var` for an
// unknown type (e.g. an untyped stack slot).
std::string CSharpTypeName(const TypeSystem::ITypePtr& type) {
    if (!type) return "var";
    if (auto* a = dynamic_cast<const TypeSystem::ArrayType*>(type.get()))
        return CSharpTypeName(a->Element()) + (a->IsSzArray() ? "[]" : "[,]");
    if (auto* byref = dynamic_cast<const TypeSystem::ByReferenceType*>(type.get()))
        return CSharpTypeName(byref->Element());
    if (auto* ptr = dynamic_cast<const TypeSystem::PointerType*>(type.get()))
        return CSharpTypeName(ptr->Element()) + "*";
    if (auto* k = dynamic_cast<const TypeSystem::KnownType*>(type.get())) {
        switch (k->Code()) {
            case TypeSystem::KnownTypeCode::Boolean: return "bool";
            case TypeSystem::KnownTypeCode::Char: return "char";
            case TypeSystem::KnownTypeCode::SByte: return "sbyte";
            case TypeSystem::KnownTypeCode::Byte: return "byte";
            case TypeSystem::KnownTypeCode::Int16: return "short";
            case TypeSystem::KnownTypeCode::UInt16: return "ushort";
            case TypeSystem::KnownTypeCode::Int32: return "int";
            case TypeSystem::KnownTypeCode::UInt32: return "uint";
            case TypeSystem::KnownTypeCode::Int64: return "long";
            case TypeSystem::KnownTypeCode::UInt64: return "ulong";
            case TypeSystem::KnownTypeCode::Single: return "float";
            case TypeSystem::KnownTypeCode::Double: return "double";
            case TypeSystem::KnownTypeCode::Decimal: return "decimal";
            case TypeSystem::KnownTypeCode::String: return "string";
            case TypeSystem::KnownTypeCode::Object: return "object";
            case TypeSystem::KnownTypeCode::IntPtr: return "nint";
            case TypeSystem::KnownTypeCode::UIntPtr: return "nuint";
            default: break;
        }
    }
    std::string rn = type->ReflectionName();
    auto pos = rn.rfind('.');
    return pos != std::string::npos ? rn.substr(pos + 1) : rn;
}

std::string ILAstToCSharp(const ILFunction& fn,
                          std::string_view returnType,
                          std::string_view methodName,
                          std::string_view paramDecl) {
    std::string out;
    CEmitter emitter(out);
    emitter.EmitMethod(fn, returnType, methodName, paramDecl);
    return out;
}

} // namespace ILSpy::Decompiler::IL
