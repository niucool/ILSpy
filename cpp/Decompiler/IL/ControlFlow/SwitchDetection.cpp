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

#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// Match an integer constant (LdcI4 or LdcI8). The C# MatchLdcI also unwraps
// Conv sign/zero-extend; this port's reader does not wrap ldc in Conv, so the
// plain cases cover the inputs. Mirrors the local MatchLdcI in SwitchAnalysis.cpp
// (a shared match-helpers header is not yet established in this port).
bool MatchLdcI(ILInstruction* inst, long long& val) {
    if (!inst) return false;
    if (inst->Op == OpCode::LdcI4) {
        val = static_cast<LdcI4*>(inst)->Value;
        return true;
    }
    if (inst->Op == OpCode::LdcI8) {
        val = static_cast<LdcI8*>(inst)->Value;
        return true;
    }
    return false;
}

// The SwitchInstruction that ends `block`, or nullptr if the block has no final
// or the final is not a switch. Matches the C# assumption that, with basic
// blocks at this pipeline stage, a switch can only be the block's last
// instruction.
SwitchInstruction* BlockSwitch(Block* block) {
    if (!block || !block->FinalInstruction) return nullptr;
    if (block->FinalInstruction->Op != OpCode::SwitchInstruction) return nullptr;
    return static_cast<SwitchInstruction*>(block->FinalInstruction.get());
}

} // namespace

void SwitchDetection::SimplifySwitchInstruction(Block* block, ILTransformContext& context) {
    auto* sw = BlockSwitch(block);
    if (!sw) return;

    // Combine sections with identical branch target: the first section branching
    // to a block is the primary; later sections branching to the same block
    // merge their label sets into it and are removed. Mirrors the C#
    // sw.Sections.RemoveAll(...) with a Block->primary dictionary. Only Branch
    // bodies are de-duplicated (the C# MatchBranch); Leave bodies are left
    // alone (they are handled by the later exit-point transforms).
    std::unordered_map<Block*, SwitchSection*> primaryByTarget;
    std::vector<std::size_t> remove;
    for (std::size_t i = 0; i < sw->Sections.size(); ++i) {
        auto& section = sw->Sections[i];
        if (!section) continue;
        auto* br = dynamic_cast<Branch*>(section->Body.get());
        Block* target = (br && br->TargetBlock) ? br->TargetBlock : nullptr;
        if (!target) continue;  // not a resolved Branch-to-block: leave alone
        auto it = primaryByTarget.find(target);
        if (it != primaryByTarget.end()) {
            context.StepOnce("Combine switch sections with same branch target");
            it->second->Labels = it->second->Labels.UnionWith(section->Labels);
            // section.HasNullLabel is not modeled in this port (a SwitchOnNullable
            // concern); the C# `primarySection.HasNullLabel |= section.HasNullLabel`
            // line is skipped here.
            remove.push_back(i);
        } else {
            primaryByTarget.emplace(target, section.get());
        }
    }
    if (!remove.empty()) {
        for (auto it = remove.rbegin(); it != remove.rend(); ++it)
            sw->Sections.erase(sw->Sections.begin() + static_cast<std::ptrdiff_t>(*it));
        for (std::size_t i = 0; i < sw->Sections.size(); ++i)
            if (sw->Sections[i]) sw->Sections[i]->ChildIndex = static_cast<int>(i + 1);
    }

    AdjustLabels(sw, context);
    SortSwitchSections(sw, context);
}

void SwitchDetection::AdjustLabels(SwitchInstruction* sw, ILTransformContext& /*context*/) {
    if (!sw || !sw->Value) return;
    auto* bop = dynamic_cast<BinaryNumericInstruction*>(sw->Value.get());
    if (!bop || bop->CheckForOverflow) return;
    long long val;
    if (!MatchLdcI(bop->Right.get(), val)) return;
    long long offset;
    switch (bop->Operator) {
        case BinaryNumericOperator::Add:
            // C# unchecked(-val): wrap the negation through uint64 (signed
            // overflow is UB in C++).
            offset = static_cast<long long>(static_cast<std::uint64_t>(0) -
                                            static_cast<std::uint64_t>(val));
            break;
        case BinaryNumericOperator::Sub:
            offset = val;
            break;
        default:
            return;  // not an Add/Sub offset
    }
    // Move the offset into the labels: the switch value becomes the bare
    // operand (bop->Left), and every section's labels shift by `offset`.
    auto left = std::move(bop->Left);  // detach; bop->Left is now null
    sw->Value = std::move(left);       // destroys the bop, takes ownership of Left
    if (sw->Value) {
        sw->Value->Parent = sw;
        sw->Value->ChildIndex = 0;
    }
    for (auto& section : sw->Sections) {
        if (section) section->Labels = section->Labels.AddOffset(offset);
    }
}

void SwitchDetection::SortSwitchSections(SwitchInstruction* sw, ILTransformContext& context) {
    if (!sw) return;
    auto& secs = sw->Sections;
    if (secs.size() <= 1) return;

    // First label value (C# s.Labels.Values.FirstOrDefault()), or 0 for an
    // empty/default section (FirstOrDefault on an empty long sequence is 0).
    // Precomputed once per section (not per comparison) so the universe-sized
    // default set is never enumerated.
    auto labelFirst = [](SwitchSection* s) -> long long {
        if (!s) return 0;
        const auto& intervals = s->Labels.Intervals();
        if (intervals.empty()) return 0;
        return intervals.front().Start;
    };

    if (context.Settings.SortSwitchSections) {
        // Sort by label value (a diffing aid for obfuscated assemblies).
        std::vector<int> order(secs.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return labelFirst(secs[static_cast<std::size_t>(a)].get()) <
                   labelFirst(secs[static_cast<std::size_t>(b)].get());
        });
        std::vector<std::unique_ptr<SwitchSection>> moved;
        moved.reserve(secs.size());
        for (int idx : order)
            moved.push_back(std::move(secs[static_cast<std::size_t>(idx)]));
        secs = std::move(moved);
    } else {
        // Sort by branch-target IL offset (the C# default; preserves the
        // original case order across rebuilds). Primary key: Branch -> its
        // TargetOffset; Leave -> its start IL offset; other -> null (sorts
        // first, as in C# OrderBy over int?). This port does not carry
        // per-instruction ILRange, so a Leave body uses offset 0 as a
        // fallback (the CFS first pass only sees Branch bodies, so this does
        // not affect output today). Secondary key: the first label value.
        auto keyOf = [&](SwitchSection* s) -> std::tuple<bool, long long, long long> {
            long long lf = labelFirst(s);
            if (auto* br = dynamic_cast<Branch*>(s ? s->Body.get() : nullptr)) {
                return {true, static_cast<long long>(br->TargetOffset), lf};
            }
            if (dynamic_cast<Leave*>(s ? s->Body.get() : nullptr)) {
                return {true, 0, lf};
            }
            return {false, 0, lf};
        };
        std::vector<int> order(secs.size());
        std::iota(order.begin(), order.end(), 0);
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
            return keyOf(secs[static_cast<std::size_t>(a)].get()) <
                   keyOf(secs[static_cast<std::size_t>(b)].get());
        });
        std::vector<std::unique_ptr<SwitchSection>> moved;
        moved.reserve(secs.size());
        for (int idx : order)
            moved.push_back(std::move(secs[static_cast<std::size_t>(idx)]));
        secs = std::move(moved);
    }

    // Re-wire each section's ChildIndex (Value is child 0, sections are 1..n).
    for (std::size_t i = 0; i < secs.size(); ++i)
        if (secs[i]) secs[i]->ChildIndex = static_cast<int>(i + 1);
}

} // namespace ILSpy::Decompiler::IL
