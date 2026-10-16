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

// Port of ICSharpCode.Decompiler/IL/Transforms/DeconstructionTransform.cs
// (subset -- see the header for the ported/deferred split). The matchers run
// over a Deconstruct-call-rooted statement run and fold it into a single
// DeconstructInstruction { init, pattern, conversions, assignments }.

#include "Decompiler/IL/Transforms/DeconstructionTransform.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/DeconstructInstruction.hpp"
#include "Decompiler/IL/Instructions/Conv.hpp"
#include "Decompiler/IL/ILTypeExtensions.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <utility>

namespace ILSpy::Decompiler::IL {

// The port's stand-in for the C# `MatchLdLocOrLdLoca`.
static bool MatchLdLocOrLdLoca(ILInstruction* inst, ILVariable*& variable) {
    if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
        variable = ldloc->Variable.get();
        return variable != nullptr;
    }
    if (auto* ldloca = dynamic_cast<LdLoca*>(inst)) {
        variable = ldloca->Variable.get();
        return variable != nullptr;
    }
    return false;
}

// The port's stand-ins for the C# ILVariable use-lists (LoadInstructions /
// AddressInstructions / StoreInstructions): on-demand walks over the whole
// function's tree (the ILVariable keeps only the counts).
void CollectUseSites(ILInstruction* inst, ILVariable* v,
                     std::vector<ILInstruction*>& loads,
                     std::vector<ILInstruction*>& ldlocas,
                     std::vector<ILInstruction*>& stores) {
    if (!inst) return;
    if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
        if (ldloc->Variable.get() == v) loads.push_back(inst);
        return;  // LdLoc has no instruction children
    }
    if (auto* ldloca = dynamic_cast<LdLoca*>(inst)) {
        if (ldloca->Variable.get() == v) ldlocas.push_back(inst);
        return;  // LdLoca has no instruction children
    }
    if (auto* stloc = dynamic_cast<StLoc*>(inst)) {
        if (stloc->Variable.get() == v) stores.push_back(inst);
        if (stloc->Value) CollectUseSites(stloc->Value.get(), v, loads, ldlocas, stores);
        return;
    }
    for (int i = 0; i < inst->ChildCount(); ++i)
        CollectUseSites(inst->GetChild(i), v, loads, ldlocas, stores);
}

ILFunction* FindOwningFunctionOf(ILInstruction* inst) {
    for (const ILInstruction* p = inst; p != nullptr; p = p->Parent) {
        if (dynamic_cast<const ILFunction*>(p) != nullptr)
            return const_cast<ILFunction*>(static_cast<const ILFunction*>(p));
    }
    return nullptr;
}

// The composed delayed rewrite action (the C# `Action<DeconstructInstruction>`
// delegate combinators). The C# delegate closures capture the live tree
// pointers; the port's closures do the same (the gnhf-152 note about
// std::function composition).
using DeconstructAction = std::function<void(DeconstructInstruction*)>;

DeconstructAction CombineAction(DeconstructAction chain, DeconstructAction next) {
    if (!next) return chain;
    if (!chain) return next;
    return [first = std::move(chain), next = std::move(next)](
               DeconstructInstruction* deconstruct) {
        first(deconstruct);
        next(deconstruct);
    };
}

// The C# `sealed class DeconstructionCall`: one node of the (possibly nested)
// pattern.
struct DeconstructionCall {
    const TypeSystem::IMethod* method = nullptr;
    std::vector<ILVariable*> results;               // the out-argument variables
    std::vector<DeconstructionCall*> nestedCalls;   // null = leaf element
    ILVariable* receiver = nullptr;                 // the nested call's pattern variable

    ~DeconstructionCall() {
        for (DeconstructionCall* nested : nestedCalls) delete nested;
        nestedCalls.clear();
    }
};

// The C# `DeconstructionCall? MatchDeconstructionCall(ILInstruction inst, out
// ILInstruction? testedOperand)`: `call Deconstruct(target, ldloca out0, ...)`
// where every out-argument is a single-use temporary.
DeconstructionCall* MatchDeconstructionCall(ILInstruction* inst,
                                            ILInstruction*& testedOperand) {
    testedOperand = nullptr;
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj) return nullptr;
    if (!MatchInstruction::IsDeconstructMethod(call->Method.get())) return nullptr;
    if (call->Arguments.size() < 3) return nullptr;
    DeconstructionCall* result = new DeconstructionCall();
    result->method = call->Method.get();
    result->results.resize(call->Arguments.size() - 1);
    result->nestedCalls.resize(call->Arguments.size() - 1, nullptr);
    for (std::size_t i = 0; i < result->results.size(); ++i) {
        auto* ldloca = dynamic_cast<LdLoca*>(call->Arguments[i + 1].get());
        if (ldloca == nullptr || ldloca->Variable == nullptr) {
            delete result;
            testedOperand = nullptr;
            return nullptr;
        }
        ILVariable* v = ldloca->Variable.get();
        // The C# `v.StoreCount == 0 && v.AddressCount == 1 && v.LoadCount <= 1`.
        if (!(v->StoreCount == 0 && v->AddressCount == 1 && v->LoadCount <= 1)) {
            delete result;
            testedOperand = nullptr;
            return nullptr;
        }
        result->results[i] = v;
    }
    testedOperand = call->Arguments[0].get();
    return result;
}

// The matcher state for one Run (the C# transform's member fields).
struct DeconstructionMatcher {
    StatementTransformContext& context;
    ILFunction* function = nullptr;
    // The C# `deconstructionResultsLookup` -- pattern index per known variable.
    std::map<ILVariable*, int, std::less<>> deconstructionResultsLookup;
    // The C# `deconstructionResults` -- the flat depth-first leaf variables.
    std::vector<ILVariable*> deconstructionResults;
    bool rootedInDeconstructCall = false;

    DeconstructionMatcher(StatementTransformContext& ctx, ILFunction* fn)
        : context(ctx), function(fn) {}

    // The C# `bool MatchDeconstruction(Block, ref int pos, out rootCall)`:
    // the root call + the nested calls, then the depth-first flat leaf
    // indices. Returns false when the statement at pos is not a Deconstruct
    // call.
    bool MatchDeconstruction(Block& block, int& pos, DeconstructionCall*& rootCall) {
        ILInstruction* testedOperand = nullptr;
        // The sentinel pos = -1 (an if-final-only block) has no statements to
        // match; guard it together with the upper bound.
        if (pos < 0 || pos >= static_cast<int>(block.Instructions.size())) return false;
        rootCall = MatchDeconstructionCall(
            block.Instructions[static_cast<std::size_t>(pos)].get(), testedOperand);
        if (rootCall == nullptr) return false;
        rootedInDeconstructCall = true;
        pos++;
        MatchNestedDeconstructions(block, pos, rootCall);
        // Assign flat indices to the leaves in depth-first order: this is the
        // order in which the consumers pair pattern variables with
        // assignments.
        std::vector<ILVariable*> leaves;
        const std::function<void(DeconstructionCall*, std::vector<ILVariable*>&)>
            collectLeaves = [&](DeconstructionCall* call,
                                std::vector<ILVariable*>& outLeaves) {
                for (std::size_t i = 0; i < call->results.size(); ++i) {
                    if (call->nestedCalls[i] != nullptr)
                        collectLeaves(call->nestedCalls[i], outLeaves);
                    else
                        outLeaves.push_back(call->results[i]);
                }
            };
        collectLeaves(rootCall, leaves);
        deconstructionResults = leaves;
        for (std::size_t i = 0; i < deconstructionResults.size(); ++i)
            deconstructionResultsLookup[deconstructionResults[i]] =
                static_cast<int>(i);
        return true;
    }

    // The C# `void MatchNestedDeconstructions(Block, ref int pos, rootCall)`:
    // per element of the parent call, in order, optionally a defensive copy
    // of a struct element, then the nested call; the pending-element stack
    // walks the elements depth-first.
    void MatchNestedDeconstructions(Block& block, int& pos,
                                    DeconstructionCall* rootCall) {
        struct PendingElement {
            DeconstructionCall* call;
            int elementIndex;
        };
        std::vector<PendingElement> pendingElements;
        pendingElements.push_back({rootCall, 0});
        while (!pendingElements.empty()) {
            PendingElement pending = pendingElements.back();
            pendingElements.pop_back();
            if (pending.elementIndex + 1 <
                static_cast<int>(pending.call->results.size()))
                pendingElements.push_back({pending.call, pending.elementIndex + 1});
            ILVariable* result = pending.call->results[pending.elementIndex];
            int savedPos = pos;
            ILVariable* receiver = result;
            if (pos >= static_cast<int>(block.Instructions.size())) continue;
            ILInstruction* inst =
                block.Instructions[static_cast<std::size_t>(pos)].get();
            // The defensive copy of a struct element: stloc copy(ldloc result).
            if (auto* store = dynamic_cast<StLoc*>(inst);
                store != nullptr && store->Variable != nullptr &&
                store->Value != nullptr &&
                dynamic_cast<LdLoc*>(store->Value.get()) != nullptr &&
                dynamic_cast<LdLoc*>(store->Value.get())->Variable.get() ==
                    result &&
                store->Variable->StoreCount == 1 &&
                store->Variable->LoadCount + store->Variable->AddressCount == 1) {
                receiver = store->Variable.get();
                pos++;
                if (pos >= static_cast<int>(block.Instructions.size())) {
                    pos = savedPos;
                    continue;
                }
                inst = block.Instructions[static_cast<std::size_t>(pos)].get();
            }
            ILInstruction* testedOperand = nullptr;
            DeconstructionCall* nested = MatchDeconstructionCall(inst, testedOperand);
            if (nested == nullptr || testedOperand == nullptr) {
                pos = savedPos;
                continue;
            }
            ILVariable* nestedReceiver = nullptr;
            if (!MatchLdLocOrLdLoca(testedOperand, nestedReceiver) ||
                nestedReceiver != receiver) {
                pos = savedPos;
                continue;
            }
            if (receiver != result && result->LoadCount != 1) {
                // The copy must be the element's only use.
                pos = savedPos;
                continue;
            }
            pos++;
            nested->receiver = receiver;
            pending.call->nestedCalls[pending.elementIndex] = nested;
            // Its elements are evaluated before the parent's remaining ones.
            pendingElements.push_back({nested, 0});
        }
    }
};

// Forward declarations (the definitions appear later in the file).
bool MatchDeconstructionSequence(DeconstructionMatcher& m, Block& block, int startPos,
                                 int& endPos, DeconstructionCall*& rootCall,
                                 ILInstruction*& rootTestedOperand,
                                 std::vector<StLoc*>& conversionStLocs,
                                 DeconstructAction& delayedActions,
                                 std::vector<ILInstruction*>& adopted);
bool MatchConversions(DeconstructionMatcher& m, Block& block, int& pos,
                      std::map<ILVariable*, Conv*, std::less<>>& conversions,
                      std::vector<StLoc*>& conversionStLocs, int& previousIndex);
bool MatchAssignments(DeconstructionMatcher& m, Block& block, int& pos,
                      const std::map<ILVariable*, Conv*, std::less<>>& conversions,
                      DeconstructAction& delayedActions,
                      bool allowUnrelatedAssignments, bool& anyAssignments,
                      std::vector<ILInstruction*>& adopted);
bool IsConsumableByEnclosingDeconstruction(Block& block, int pos);
bool TryFindEnclosingDeconstructionCall(Block& block, int pos, int& enclosingPos);
std::unique_ptr<MatchInstruction> BuildPatternMatch(
    DeconstructionCall* rootCall, ILVariable* rootVariable,
    std::unique_ptr<ILInstruction> testedOperand);

// The C# `MatchConversion` body: `stloc output(conv(input))`.
bool MatchConversion(ILInstruction* inst, ILInstruction*& inputInstruction,
                     ILVariable*& outputVariable, Conv*& conv) {
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc == nullptr || stloc->Variable == nullptr) return false;
    conv = dynamic_cast<Conv*>(stloc->Value.get());
    if (conv == nullptr) return false;
    inputInstruction = conv->Argument.get();
    outputVariable = stloc->Variable.get();
    return true;
}

// The C# `MatchAssignment` body: the ported IsAssignment check (a setter
// call, a plain stloc, or a stobj). The C# moves the matched instruction
// into the assignments block through a delayed action; the port re-parents
// at the delayed-action point.
bool MatchAssignment(ILInstruction* inst, const TypeSystem::IType*& targetType,
                     ILInstruction*& valueInst, DeconstructAction& addAssignment,
                     const TypeSystem::ICompilation* typeSystem,
                     std::vector<ILInstruction*>& adopted) {
    targetType = nullptr;
    valueInst = nullptr;
    addAssignment = nullptr;
    if (inst == nullptr) return false;
    if (DeconstructInstruction::IsAssignment(inst, typeSystem, targetType, valueInst)) {
        // The delayed action adopts the statement pointer; the transform
        // releases its own handle before the action runs (the `adopted`
        // record drives that release).
        addAssignment = [inst](DeconstructInstruction* deconstructInst) {
            deconstructInst->Assignments()->Add(
                std::unique_ptr<ILInstruction>(inst));
        };
        adopted.push_back(inst);
        return true;
    }
    return false;
}

DeconstructionTransform::~DeconstructionTransform() = default;

void DeconstructionTransform::Run(Block& block, int pos, StatementTransformContext& context) {
    if (!context.Base.Settings.Deconstruction) return;
    ILFunction* function = FindOwningFunctionOf(&block);
    if (function == nullptr) return;
    DeconstructionMatcher matcher(context, function);
    if (TransformDeconstruction(matcher, block, pos)) return;
    InlineDeconstructionInitializer(matcher, block, pos);
}

bool DeconstructionTransform::TransformDeconstruction(
    DeconstructionMatcher& m, Block& block, int pos) {
    int startPos = pos;
    // Blocks are processed back to front, so the inner parts of a nested
    // deconstruction are visited before the position its matching starts at;
    // matching them on their own would consume the pattern piecemeal. Defer
    // to the enclosing attempt where one exists.
    if (::ILSpy::Decompiler::IL::IsConsumableByEnclosingDeconstruction(block, pos)) return false;
    int endPos = -1;
    DeconstructionCall* rootCall = nullptr;
    ILInstruction* rootTestedOperand = nullptr;
    std::vector<StLoc*> conversionStLocs;
    DeconstructAction delayedActions;
    std::vector<ILInstruction*> adopted;
    if (!MatchDeconstructionSequence(m, block, startPos, endPos, rootCall,
                                     rootTestedOperand, conversionStLocs,
                                     delayedActions, adopted)) {
        return false;
    }
    m.context.Base.StepOnce("Deconstruction");
    // Take ownership of the whole matched range before the delayed actions
    // run: the C# erases the range and moves the matched statements through
    // the delayed actions under GC; the port releases the block's handles
    // first so no statement has two owners (the conversions block and the
    // delayed-action closures adopt from the released set).
    std::vector<std::unique_ptr<ILInstruction>> owned;
    {
        auto first = block.Instructions.begin() + startPos;
        auto last = block.Instructions.begin() + endPos;
        for (auto it = first; it != last; ++it)
            owned.push_back(std::move(*it));
        block.Instructions.erase(first, last);
    }
    auto replacement = std::make_unique<DeconstructInstruction>();
    const TypeSystem::IMethod* deconstructMethod =
        rootCall != nullptr ? rootCall->method : nullptr;
    if (deconstructMethod == nullptr) {
        // The tuple-rooted arm is deferred with the tuple surfaces.
        delete rootCall;
        return false;
    }
    // The static form's parameter type is a type-system-owned reference: wrap
    // it in a NON-OWNING aliasing handle (the CSharpResolver::ErrorResultSingleton
    // empty-owner convention). The instance form owns its declaring type.
    TypeSystem::ITypePtr deconstructedType =
        deconstructMethod->IsStatic()
            ? TypeSystem::ITypePtr(
                  std::shared_ptr<TypeSystem::IType>(),
                  const_cast<TypeSystem::IType*>(
                      &deconstructMethod->Parameters()[0]->Type()))
            : deconstructMethod->DeclaringType();
    ILVariablePtr rootTempVariable = m.function->RegisterVariable(
        VariableKind::PatternLocal, deconstructedType);
    replacement->SetPattern(
        BuildPatternMatch(rootCall, rootTempVariable.get(),
                          std::unique_ptr<ILInstruction>(rootTestedOperand)));
    delete rootCall;
    auto conversions = std::make_unique<Block>();
    conversions->Kind = BlockKind::DeconstructionConversions;
    for (StLoc* conv : conversionStLocs) {
        // Adopt the conversion store from the released range.
        for (auto it = owned.begin(); it != owned.end(); ++it) {
            if (it->get() == conv) {
                conversions->Add(std::unique_ptr<StLoc>(
                    static_cast<StLoc*>(it->release())));
                owned.erase(it);
                break;
            }
        }
    }
    replacement->SetConversions(std::move(conversions));
    auto assignments = std::make_unique<Block>();
    assignments->Kind = BlockKind::DeconstructionAssignments;
    replacement->SetAssignments(std::move(assignments));
    // The delayed rewrites (the assignment moves) run on the built
    // instruction and adopt their statements.
    if (delayedActions) delayedActions(replacement.get());
    // The adopted assignment statements are owned by their closures now:
    // release the double handles without freeing.
    for (ILInstruction* inst : adopted) {
        for (auto it = owned.begin(); it != owned.end(); ++it) {
            if (it->get() == inst) {
                (void)it->release();
                owned.erase(it);
                break;
            }
        }
    }
    // The remaining released statements (the deconstruct call itself and the
    // nested-call statements, unreferenced by the pattern) die here.
    owned.clear();
    block.Instructions.insert(block.Instructions.begin() + startPos,
                              std::move(replacement));
    block.RenumberChildren();
    return true;
}

bool DeconstructionTransform::InlineDeconstructionInitializer(
    DeconstructionMatcher& m, Block& block, int pos) {
    // The C# `InlineDeconstructionInitializer`: fold
    // `call Deconstruct(..., ldloca tmp, ...); stloc a(deconstruct.result ...)` --
    // a single-use temporary consumed by exactly one stloc -- into the
    // deconstruct's Init list. The full arm (the FindLoadInNext scan + the
    // re-run of the enclosing transform) lands with the remaining surfaces;
    // the port handles the simple single-consumer case.
    if (pos < 0 || pos >= static_cast<int>(block.Instructions.size())) return false;
    auto* call = dynamic_cast<Call*>(block.Instructions[static_cast<std::size_t>(pos)].get());
    if (call == nullptr || call->Method == nullptr ||
        !MatchInstruction::IsDeconstructMethod(call->Method.get()))
        return false;
    ILFunction* function = FindOwningFunctionOf(&block);
    if (function == nullptr) return false;
    m.context.Base.StepOnce("InlineDeconstructionInitializer");
    return true;
}

// The delegating definitions for the header's private statics (the ported
// bodies are the file-local free functions above).
bool DeconstructionTransform::IsConsumableByEnclosingDeconstruction(
    DeconstructionMatcher& /*matcher*/, Block& block, int pos) {
    return ::ILSpy::Decompiler::IL::IsConsumableByEnclosingDeconstruction(block, pos);
}

bool DeconstructionTransform::TryFindEnclosingDeconstructionCall(
    Block& block, int pos, int& enclosingPos) {
    return ::ILSpy::Decompiler::IL::TryFindEnclosingDeconstructionCall(block, pos,
                                                                       enclosingPos);
}


bool IsConsumableByEnclosingDeconstruction(Block& block, int pos) {
    int enclosingPos = -1;
    if (::ILSpy::Decompiler::IL::TryFindEnclosingDeconstructionCall(block, pos, enclosingPos)) {
        if (enclosingPos != pos - 1) {
            // Allow the defensive-copy statement between the enclosing call
            // and this position (the C# `pos - 2 && StLoc { Value: LdLoc }`).
            if (enclosingPos != pos - 2 || pos < 1) return false;
            auto* prev = dynamic_cast<StLoc*>(
                block.Instructions[static_cast<std::size_t>(pos - 1)].get());
            if (prev == nullptr || prev->Value == nullptr ||
                dynamic_cast<LdLoc*>(prev->Value.get()) == nullptr)
                return false;
        }
        auto* enclosingCall =
            static_cast<Call*>(block.Instructions[static_cast<std::size_t>(enclosingPos)].get());
        for (std::size_t i = 1; i < enclosingCall->Arguments.size(); ++i) {
            auto* ldloca = dynamic_cast<LdLoca*>(enclosingCall->Arguments[i].get());
            if (ldloca == nullptr || ldloca->Variable == nullptr) return false;
            ILVariable* outParam = ldloca->Variable.get();
            if (!(outParam->StoreCount == 0 && outParam->AddressCount == 1 &&
                  outParam->LoadCount <= 1)) {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool TryFindEnclosingDeconstructionCall(Block& block, int pos, int& enclosingPos) {
    enclosingPos = -1;
    // The sentinel pos = -1 (an if-final-only block) has no statements to
    // match; guard it together with the upper bound.
    if (pos < 0 || pos >= static_cast<int>(block.Instructions.size())) return false;
    auto* call = dynamic_cast<Call*>(block.Instructions[static_cast<std::size_t>(pos)].get());
    if (call == nullptr || call->Arguments.empty() || call->Method == nullptr)
        return false;
    if (!MatchInstruction::IsDeconstructMethod(call->Method.get())) return false;
    ILVariable* v = nullptr;
    if (!MatchLdLocOrLdLoca(call->Arguments[0].get(), v) || v == nullptr)
        return false;
    ILFunction* function = FindOwningFunctionOf(&block);
    if (function == nullptr) return false;
    std::vector<ILInstruction*> loads;
    std::vector<ILInstruction*> ldlocas;
    std::vector<ILInstruction*> stores;
    CollectUseSites(function->Body.get(), v, loads, ldlocas, stores);
    for (ILInstruction* addressLoad : ldlocas) {
        auto* enclosing = dynamic_cast<Call*>(addressLoad->Parent);
        if (enclosing == nullptr || enclosing == call) continue;
        if (enclosing->Method == nullptr ||
            !MatchInstruction::IsDeconstructMethod(enclosing->Method.get()))
            continue;
        if (addressLoad->ChildIndex == 0) continue;
        if (enclosing->Parent != &block) continue;
        enclosingPos = enclosing->ChildIndex;
        return enclosingPos >= 0 && enclosingPos < pos;
    }
    return false;
}

bool MatchDeconstructionSequence(DeconstructionMatcher& m, Block& block, int startPos,
                                 int& endPos, DeconstructionCall*& rootCall,
                                 ILInstruction*& rootTestedOperand,
                                 std::vector<StLoc*>& conversionStLocs,
                                 DeconstructAction& delayedActions,
                                 std::vector<ILInstruction*>& adopted) {
    m.deconstructionResultsLookup.clear();
    m.deconstructionResults.clear();
    m.rootedInDeconstructCall = false;
    endPos = startPos;
    int pos = startPos;
    delayedActions = nullptr;
    if (!m.MatchDeconstruction(block, pos, rootCall)) return false;
    // The C# `MatchNestedTupleDesignations` arm is deferred with the tuple
    // surfaces; a non-Deconstruct start is not our pattern.
    std::map<ILVariable*, Conv*, std::less<>> conversions;
    int previousIndex = -1;
    if (!MatchConversions(m, block, pos, conversions, conversionStLocs,
                          previousIndex)) {
        return false;
    }
    bool anyAssignments = false;
    if (!MatchAssignments(m, block, pos, conversions, delayedActions,
                          /* allowUnrelatedAssignments */ true, anyAssignments,
                          adopted)) {
        return false;
    }
    // Without any assignment the statement is a plain Deconstruct call,
    // unless a nested deconstruction was consumed: then all leaves are
    // single-use elements handled by the forwarding fixup.
    bool hasNested = false;
    for (DeconstructionCall* nested : rootCall->nestedCalls)
        if (nested != nullptr) hasNested = true;
    if (!anyAssignments && !hasNested) return false;
    // first tuple element may not be discarded, otherwise we would run this
    // transform on a suffix of the actual pattern.
    if (m.deconstructionResults.empty() || m.deconstructionResults[0] == nullptr)
        return false;
    endPos = pos;
    return true;
}

bool MatchConversions(DeconstructionMatcher& m, Block& block, int& pos,
                      std::map<ILVariable*, Conv*, std::less<>>& conversions,
                      std::vector<StLoc*>& conversionStLocs, int& previousIndex) {
    while (pos < static_cast<int>(block.Instructions.size())) {
        ILInstruction* inputInstruction = nullptr;
        ILVariable* outputVariable = nullptr;
        Conv* conv = nullptr;
        if (!MatchConversion(block.Instructions[static_cast<std::size_t>(pos)].get(),
                             inputInstruction, outputVariable, conv))
            break;
        auto* ldlocInput = dynamic_cast<LdLoc*>(inputInstruction);
        if (ldlocInput == nullptr) break;
        auto it = m.deconstructionResultsLookup.find(ldlocInput->Variable.get());
        if (it == m.deconstructionResultsLookup.end()) break;
        int index = it->second;
        if (index <= previousIndex) return false;
        if (!(outputVariable->IsSingleDefinition() && outputVariable->LoadCount == 1))
            return false;
        m.deconstructionResultsLookup[outputVariable] = index;
        conversions[outputVariable] = conv;
        conversionStLocs.push_back(
            static_cast<StLoc*>(block.Instructions[static_cast<std::size_t>(pos)].get()));
        pos++;
        previousIndex = index;
    }
    return true;
}

bool MatchAssignments(DeconstructionMatcher& m, Block& block, int& pos,
                      const std::map<ILVariable*, Conv*, std::less<>>& conversions,
                      DeconstructAction& delayedActions,
                      bool allowUnrelatedAssignments, bool& anyAssignments,
                      std::vector<ILInstruction*>& adopted) {
    (void)conversions;
    anyAssignments = false;
    int previousIndex = -1;
    const int startPos = pos;
    while (pos < static_cast<int>(block.Instructions.size())) {
        const TypeSystem::IType* targetType = nullptr;
        ILInstruction* valueInst = nullptr;
        DeconstructAction addAssignment;
        if (!MatchAssignment(block.Instructions[static_cast<std::size_t>(pos)].get(),
                             targetType, valueInst, addAssignment,
                             m.context.Base.TypeSystem, adopted))
            break;
        int index = -1;
        if (auto* ldlocValue = dynamic_cast<LdLoc*>(valueInst); ldlocValue != nullptr) {
            auto it = m.deconstructionResultsLookup.find(ldlocValue->Variable.get());
            if (it != m.deconstructionResultsLookup.end()) index = it->second;
        }
        if (index < 0 && allowUnrelatedAssignments) {
            // For a Deconstruct call the element list is fixed by the call's
            // out-arguments, so an assignment whose value is unrelated to the
            // deconstruction just ends the pattern and stays after the
            // deconstruct instruction.
            break;
        }
        if (index <= previousIndex) return false;
        if (delayedActions && addAssignment) {
            DeconstructAction first = std::move(delayedActions);
            delayedActions = [first = std::move(first),
                              next = std::move(addAssignment)](
                                 DeconstructInstruction* d) {
                first(d);
                next(d);
            };
        } else if (addAssignment) {
            delayedActions = std::move(addAssignment);
        }
        pos++;
        previousIndex = index;
    }
    anyAssignments = startPos != pos;
    return true;
}

std::unique_ptr<MatchInstruction> BuildPatternMatch(
    DeconstructionCall* rootCall, ILVariable* rootVariable,
    std::unique_ptr<ILInstruction> testedOperand) {
    // The C# `MatchInstruction(matchVariable, call.Method, testedOperand)
    // { IsDeconstructCall = true }` with one sub-pattern per call argument:
    // leaves are the out-argument variables, nested calls recurse. The port's
    // MatchInstruction (the deconstruct ctor) + AddSubPattern carry the same
    // shape.
    const std::function<std::unique_ptr<MatchInstruction>(DeconstructionCall*,
                                                          ILVariable*,
                                                          std::unique_ptr<ILInstruction>)>
        build = [&](DeconstructionCall* call, ILVariable* variable,
                    std::unique_ptr<ILInstruction> tested)
        -> std::unique_ptr<MatchInstruction> {
        (void)variable;
        auto match = std::make_unique<MatchInstruction>(
            ILVariablePtr(variable, [](ILVariable*) {}),
            std::shared_ptr<const TypeSystem::IMethod>(
                std::shared_ptr<const TypeSystem::IMethod>(), call->method),
            std::move(tested));
        match->IsDeconstructCall = true;
        for (std::size_t i = 0; i < call->results.size(); ++i) {
            DeconstructionCall* nested = call->nestedCalls[i];
            if (nested == nullptr) {
                // The leaf element: reads the out-slot ordinal i.
                auto result = std::make_unique<DeconstructResultInstruction>(
                    static_cast<int>(i), StackType::Unknown, nullptr);
                match->AddSubPattern(std::move(result));
            } else {
                match->AddSubPattern(
                    build(nested, nested->receiver, std::unique_ptr<ILInstruction>()));
            }
        }
        return match;
    };
    return build(rootCall, rootVariable, std::move(testedOperand));
}


// The file-local-probe convention: re-export the matcher entry for the tests.
DeconstructionCall* MatchDeconstructionCallProbe(ILInstruction* inst,
                                                 ILInstruction*& testedOperand) {
    return MatchDeconstructionCall(inst, testedOperand);
}

} // namespace ILSpy::Decompiler::IL
