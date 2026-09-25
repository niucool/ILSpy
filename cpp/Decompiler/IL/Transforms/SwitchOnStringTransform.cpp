// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Decompiler/IL/Transforms/SwitchOnStringTransform.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/ILTypeExtensions.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/StringToInt.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <cassert>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {
namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

// ---- File-local matchers (the C# instance Match* methods; the port keeps
// them as free functions per the file-local-probe convention) ----------------

bool MatchLdLoc(ILInstruction* inst, ILVariable*& v) {
    auto* ldloc = dynamic_cast<LdLoc*>(inst);
    if (ldloc == nullptr) return false;
    v = ldloc->Variable.get();
    return v != nullptr;
}

bool MatchStLoc(ILInstruction* inst, ILVariable*& v, ILInstruction*& value) {
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc == nullptr || stloc->Variable == nullptr) return false;
    v = stloc->Variable.get();
    value = stloc->Value.get();
    return true;
}

bool MatchLdNull(ILInstruction* inst) {
    return dynamic_cast<LdNull*>(inst) != nullptr;
}

bool MatchLdStr(ILInstruction* inst, std::string& value) {
    auto* ldstr = dynamic_cast<LdStr*>(inst);
    if (ldstr == nullptr) return false;
    value = ldstr->Value;
    return true;
}

bool MatchBranch(ILInstruction* inst, Block*& target) {
    auto* branch = dynamic_cast<Branch*>(inst);
    if (branch == nullptr) return false;
    target = branch->TargetBlock;
    return target != nullptr;
}

bool MatchLeave(ILInstruction* inst, BlockContainer*& target) {
    auto* leave = dynamic_cast<Leave*>(inst);
    if (leave == nullptr) return false;
    target = leave->TargetContainer;
    return target != nullptr;
}

// The C# MatchLogicNot (a Comp(left == 0) comparison, the port's canonical
// LogicNot shape -- the UserDefinedLogicTransform file-local convention).
bool MatchLogicNot(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    if (!inst || inst->Op != OpCode::Comp) return false;
    auto* comp = static_cast<Comp*>(inst);
    if (comp->Kind != ComparisonKind::Equality || comp->Unsigned) return false;
    if (!comp->Right || comp->Right->Op != OpCode::LdcI4) return false;
    if (static_cast<LdcI4*>(comp->Right.get())->Value != 0) return false;
    arg = comp->Left.get();
    return true;
}

bool MatchCompEqualsNull(ILInstruction* inst, ILInstruction*& arg) {
    arg = nullptr;
    auto* comp = dynamic_cast<Comp*>(inst);
    if (comp == nullptr || comp->Kind != ComparisonKind::Equality) return false;
    if (comp->Right == nullptr || comp->Right->Op != OpCode::LdNull) return false;
    arg = comp->Left.get();
    return true;
}

// The C# `IsKnownType` extension over an IType pointer (null-safe).
bool IsKnownType(const TS::ITypePtr& type, TS::KnownTypeCode code) {
    return type != nullptr && TS::IsKnownType(*type, code);
}

// Collect every Block reachable from `inst` (the C#
// `function.Descendants.OfType<Block>()`).
void CollectBlocks(ILInstruction* inst, std::vector<Block*>& out) {
    if (inst == nullptr) return;
    if (auto* block = dynamic_cast<Block*>(inst)) {
        out.push_back(block);
    }
    for (int i = 0; i < inst->ChildCount(); i++) {
        CollectBlocks(inst->GetChild(i), out);
    }
}

} // namespace


// Forward declaration (the definition is in the legacy-Dictionary section below).
bool MatchLegacySwitchOnStringWithDictImpl(Block& block, int& i,
                                           ILTransformContext& context);
// The C# `bool MatchLegacySwitchOnStringWithDict(...)` -- the Impl is
// defined in the legacy-Dictionary section below (inside an anon ns whose
// names are visible here via the implicit using-directive).

ILInstruction* MatchCaseBlock(Block* currentBlock, ILVariable* switchVariable,
                              std::string& value, bool& emptyStringEqualsNull,
                              ILInstruction*& caseBlockOrLeave);

bool MatchIfInstruction(ILInstruction* inst, ILInstruction*& condition,
                        ILInstruction*& trueInst) {
    auto* ifInst = dynamic_cast<IfInstruction*>(inst);
    if (ifInst == nullptr) return false;
    condition = ifInst->Condition.get();
    trueInst = ifInst->TrueInst.get();
    return condition != nullptr && trueInst != nullptr;
}

// A SwitchSection over `labels` with `body` (the ReplaceWithSwitchInstruction
// helper's `new SwitchSection { Labels = ..., Body = ... }` shape).
std::unique_ptr<SwitchSection> MakeSection(Util::LongSet labels,
                                           std::unique_ptr<ILInstruction> body) {
    auto section = std::make_unique<SwitchSection>(std::move(labels));
    section->SetBody(std::move(body));
    return section;
}

// Forward declarations (the arms are defined after Run in this file).
bool SimplifyCascadingIfStatements(Block& block, int& i,
                                   ILTransformContext& context);
bool SimplifyCSharp1CascadingIfStatements(Block& block, int& i,
                                          ILTransformContext& context);
bool MatchRoslynSwitchOnString(Block& block, int& i, ILTransformContext& context);
bool MatchStringEqualityComparisonFor(ILInstruction* condition,
                                      ILVariable* variable,
                                      std::string& stringValue,
                                      bool& isVBCompareString);

// The C# HashtableInitializer entry: the scanned init-block info for one
// compiler-generated Hashtable field (keyed by the port's LdsFlda field
// name; the C# keys by IField).
struct HashtableInitializerInfo {
    // The C# `List<(string, int)> Labels` -- the scan-extracted pairs.
    std::vector<std::pair<std::optional<std::string>, int>> labels;
    // The C# `IfInstruction JumpToNext` -- the init block's trailing
    // second-to-last if (the next hashtable's null check), null when this
    // is the last init block.
    IfInstruction* jumpToNext = nullptr;
    // The C# `Block ContainingBlock` -- the init block itself.
    Block* containingBlock = nullptr;
    // The C# `Block Previous` / `Block Next`.
    Block* previous = nullptr;
    Block* next = nullptr;
    // The C# `bool Transformed` -- set once the arm folds the switch.
    bool transformed = false;
};

bool MatchLegacySwitchOnStringWithHashtableImpl(
    Block& block, int& i,
    std::map<std::string, HashtableInitializerInfo>& hashtableInitializers,
    ILTransformContext& context);

std::map<std::string, HashtableInitializerInfo>
ScanHashtableInitializerBlocks(Block* entryPoint);
bool MatchRoslynSwitchOnStringUsingLengthAndCharImpl(Block& block, int i,
                                                     ILTransformContext&
                                                         context);

void SwitchOnStringTransform::Run(ILFunction& function,
                                  ILTransformContext& context) {
    if (!context.Settings.SwitchStatementOnString) return;
    auto* body = dynamic_cast<BlockContainer*>(function.Body.get());
    if (body == nullptr) return;
    // The C# ScanHashtableInitializerBlocks pre-scan: the entry block's
    // null-check/init shape is walked once and the extracted (string, index)
    // pairs are keyed by the compiler-generated field name so the Hashtable
    // arm can consume them.
    std::map<std::string, HashtableInitializerInfo> hashtableInitializers =
        ScanHashtableInitializerBlocks(body->EntryPoint());
    std::vector<Block*> blocks;
    CollectBlocks(body, blocks);
    for (Block* block : blocks) {
        if (block->IncomingEdgeCount == 0) continue;
        bool changed = false;
        for (int i = static_cast<int>(block->Instructions.size()) - 1; i >= 0; i--) {
            if (SimplifyCSharp1CascadingIfStatements(*block, i, context)) {
                changed = true;
                RecomputeIncomingEdgeCounts(function);
                continue;
            }
            if (MatchLegacySwitchOnStringWithHashtableImpl(
                    *block, i, hashtableInitializers, context)) {
                changed = true;
                RecomputeIncomingEdgeCounts(function);
                continue;
            }
            if (MatchLegacySwitchOnStringWithDictImpl(*block, i, context)) {
                changed = true;
                RecomputeIncomingEdgeCounts(function);
                continue;
            }
            if (SimplifyCascadingIfStatements(*block, i, context)) {
                changed = true;
                RecomputeIncomingEdgeCounts(function);
                continue;
            }
            if (MatchRoslynSwitchOnString(*block, i, context)) {
                changed = true;
                RecomputeIncomingEdgeCounts(function);
                continue;
            }
            if (MatchRoslynSwitchOnStringUsingLengthAndCharImpl(*block, i,
                                                                context)) {
                changed = true;
                RecomputeIncomingEdgeCounts(function);
                continue;
            }
        }
        if (!changed) continue;
        SwitchDetection::SimplifySwitchInstruction(block, context);
        // The C# InlineSwitchExpressionDefaultCaseThrowHelper call is deferred
        // with SwitchDetection's throw-helper surface; the closing
        // SortBlocks(deleteUnreachableBlocks: true) is unsafe in this port
        // (the D58 convention) and is deferred with it.
    }
}

// ---- The block-instruction edit helpers (the C# InstructionCollection
// ReplaceWith/RemoveAt/RemoveRange over the port's plain vector) ------------

void ReplaceAt(Block& block, int i, std::unique_ptr<ILInstruction> inst) {
    block.Instructions[i] = std::move(inst);
    block.Instructions[i]->Parent = &block;
    block.Instructions[i]->ChildIndex = i;
    block.RenumberChildren();
}

void RemoveRange(Block& block, int start, int count) {
    block.Instructions.erase(block.Instructions.begin() + start,
                             block.Instructions.begin() + start + count);
    block.RenumberChildren();
}

// The C# MatchStringEqualityComparison: the op_Equality /
// VB CompareString / SequenceEqual+AsSpan call forms over the switch value,
// or the comp(ldloc == ldnull) null form. The port matches both the
// resolved-Method construction and the reader's string stand-in (the
// MethodName "Declaring.Type::Method" + DeclaringType pair).
bool MatchStringEqualityComparison(ILInstruction* condition, ILVariable*& variable,
                                   std::string& stringValue,
                                   bool& isVBCompareString) {
    stringValue.clear();
    variable = nullptr;
    isVBCompareString = false;
    while (condition != nullptr && condition->Op == OpCode::Comp) {
        auto* comp = static_cast<Comp*>(condition);
        if (comp->Kind != ComparisonKind::Inequality) break;
        if (comp->Right == nullptr || comp->Right->Op != OpCode::LdcI4 ||
            static_cast<LdcI4*>(comp->Right.get())->Value != 0) {
            break;
        }
        // if (x != 0) == if (x)
        condition = comp->Left.get();
    }
    auto* call = dynamic_cast<Call*>(condition);
    if (call != nullptr) {
        ILInstruction* left = nullptr;
        ILInstruction* right = nullptr;
        // The method identity over the two construction forms.
        auto identity = [&](const char* ns, const char* typeName,
                            const char* name) {
            if (call->Method != nullptr) {
                TS::ITypePtr declaring = call->Method->DeclaringType();
                return call->Method->Name() == name && declaring != nullptr &&
                       declaring->Namespace() == ns &&
                       declaring->Name() == typeName;
            }
            std::string prefix = std::string(ns) + "." + typeName + "::";
            return call->MethodName == prefix + name;
        };
        const bool isOpEquality =
            identity("System", "String", "op_Equality") &&
            call->Arguments.size() == 2;
        const bool isCompareString =
            identity("Microsoft.VisualBasic.CompilerServices", "Operators",
                     "CompareString") &&
            call->Arguments.size() == 3;
        const bool isSequenceEqual =
            identity("System", "MemoryExtensions", "SequenceEqual") &&
            call->Arguments.size() == 2;
        if (isOpEquality) {
            left = call->Arguments[0].get();
            right = call->Arguments[1].get();
        } else if (isCompareString) {
            left = call->Arguments[0].get();
            right = call->Arguments[1].get();
            // VB CompareString(): return 0 on equality -> condition is
            // effectively negated; the empty string equals null.
            isVBCompareString = true;
            if (call->Arguments[2]->Op != OpCode::LdcI4 ||
                static_cast<LdcI4*>(call->Arguments[2].get())->Value != 0) {
                // Option Compare Text: case-insensitive comparison is not
                // expressible in C#.
                return false;
            }
        } else if (isSequenceEqual) {
            left = call->Arguments[0].get();
            auto* asSpan = dynamic_cast<Call*>(call->Arguments[1].get());
            if (asSpan == nullptr) return false;
            auto spanIdentity = [&](const char* name) {
                if (asSpan->Method != nullptr) {
                    TS::ITypePtr declaring = asSpan->Method->DeclaringType();
                    return asSpan->Method->Name() == name &&
                           declaring != nullptr &&
                           declaring->Namespace() == "System" &&
                           declaring->Name() == "MemoryExtensions";
                }
                return asSpan->MethodName ==
                       std::string("System.MemoryExtensions::") + name;
            };
            if (!spanIdentity("AsSpan") || asSpan->Arguments.size() != 1) return false;
            right = asSpan->Arguments[0].get();
        } else {
            return false;
        }
        return MatchLdLoc(left, variable) && MatchLdStr(right, stringValue);
    }
    ILInstruction* arg = nullptr;
    if (MatchCompEqualsNull(condition, arg)) {
        stringValue.clear();
        return MatchLdLoc(arg, variable);
    }
    return false;
}


// The C# 4-arg MatchStringEqualityComparison overload: the comparison form
// above restricted to a specific switch-value variable.
bool MatchStringEqualityComparisonFor(ILInstruction* condition,
                                      ILVariable* variable,
                                      std::string& stringValue,
                                      bool& isVBCompareString) {
    ILVariable* matched = nullptr;
    return MatchStringEqualityComparison(condition, matched, stringValue,
                                         isVBCompareString) &&
           matched == variable;
}

// The C# static MatchComputeStringOrReadOnlySpanHashCall: the
// stloc(targetVar, call ComputeStringHash/ComputeSpanHash/
// ComputeReadOnlySpanHash(ldloc s)) shape. The C# gate is
// `c.Method.IsCompilerGeneratedOrIsInCompilerGeneratedClass()`; the port's
// reader-built stand-in calls carry the declaring type instead, so the
// gate is the declaring type's "<PrivateImplementationDetails>" name
// (Roslyn emits the hash helpers there, marked CompilerGenerated) over the
// resolved-Method form's attribute check.
bool MatchComputeStringOrReadOnlySpanHashCall(ILInstruction* inst,
                                              ILVariable* targetVar,
                                              ILVariable*& switchValue) {
    switchValue = nullptr;
    ILInstruction* value = nullptr;
    ILVariable* stlocVar = nullptr;
    if (!MatchStLoc(inst, stlocVar, value)) return false;
    if (stlocVar != targetVar) return false;
    auto* call = dynamic_cast<Call*>(value);
    if (call == nullptr || call->Arguments.size() != 1) return false;
    std::string name;
    if (call->Method != nullptr) {
        name = call->Method->Name();
        if (name != "ComputeStringHash" && name != "ComputeSpanHash" &&
            name != "ComputeReadOnlySpanHash") {
            return false;
        }
        // The C# IsCompilerGeneratedOrIsInCompilerGeneratedClass gate: the
        // method or its declaring type carries [CompilerGenerated].
        bool compilerGenerated = call->Method->HasAttribute(
            TS::KnownAttribute::CompilerGenerated);
        if (!compilerGenerated &&
            call->Method->DeclaringTypeDefinition() != nullptr) {
            compilerGenerated = call->Method->DeclaringTypeDefinition()
                                    ->HasAttribute(
                                        TS::KnownAttribute::CompilerGenerated);
        }
        if (!compilerGenerated) return false;
    } else {
        name = call->MethodName.substr(call->MethodName.rfind("::") + 2);
        if (name != "ComputeStringHash" && name != "ComputeSpanHash" &&
            name != "ComputeReadOnlySpanHash") {
            return false;
        }
        if (call->DeclaringType == nullptr ||
            call->DeclaringType->Name() != "<PrivateImplementationDetails>") {
            return false;
        }
    }
    return MatchLdLoc(call->Arguments[0].get(), switchValue);
}

// The C# MatchStringLengthCall: call get_Length(ldloc switchValueVar).
bool MatchStringLengthCall(ILInstruction* inst, ILVariable* switchValueVar) {
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->Method == nullptr) return false;
    TS::ITypePtr declaring = call->Method->DeclaringType();
    return declaring != nullptr && declaring->Name() == "String" &&
           call->Method->IsAccessor() &&
           call->Method->AccessorKind() ==
               TS::MethodSemanticsAttributes::Getter &&
           call->Method->AccessorOwner() != nullptr &&
           call->Method->AccessorOwner()->Name() == "Length" &&
           call->Arguments.size() == 1 &&
           [&] {
               ILVariable* v = nullptr;
               return MatchLdLoc(call->Arguments[0].get(), v) &&
                      v == switchValueVar;
           }();
}

// The C# MatchRoslynCaseBlockHead (the two-instruction case-head block) and
// its special case where the compiler optimized the if away.
bool MatchRoslynCaseBlockHead(Block* target, ILVariable* switchValueVar,
                              ILInstruction*& bodyOrLeave,
                              Block*& defaultOrExitBlock, std::string& stringValue,
                              bool& emptyStringEqualsNull) {
    bodyOrLeave = nullptr;
    defaultOrExitBlock = nullptr;
    stringValue.clear();
    emptyStringEqualsNull = false;
    if (target->Instructions.size() != 2) return false;
    ILInstruction* condition = nullptr;
    ILInstruction* bodyBranch = nullptr;
    if (!MatchIfInstruction(target->Instructions[0].get(), condition,
                            bodyBranch)) {
        // Special case: sometimes there is no if, because bodyBranch ==
        // exitBranch and the compiler optimized it away.
        condition = target->Instructions[0].get();
        bodyBranch = target->Instructions[1].get();
    }
    ILInstruction* exitBranch = target->Instructions[1].get();
    // Handle negated conditions first.
    ILInstruction* arg = nullptr;
    while (MatchLogicNot(condition, arg)) {
        std::swap(exitBranch, bodyBranch);
        condition = arg;
    }
    bool isVB = false;
    if (!MatchStringEqualityComparisonFor(condition, switchValueVar, stringValue,
                                          isVB)) {
        return false;
    }
    if (isVB) {
        std::swap(exitBranch, bodyBranch);
        emptyStringEqualsNull = true;
    }
    Block* exitTarget = nullptr;
    BlockContainer* leaveTarget = nullptr;
    if (MatchBranch(exitBranch, exitTarget)) {
        defaultOrExitBlock = exitTarget;
    } else if (MatchLeave(exitBranch, leaveTarget)) {
        // The C# accepts any leave here.
    } else {
        return false;
    }
    ILVariable* unused = nullptr;
    (void)unused;
    if (dynamic_cast<Leave*>(bodyBranch) != nullptr) {
        bodyOrLeave = bodyBranch;
        return true;
    }
    Block* bodyTarget = nullptr;
    if (MatchBranch(bodyBranch, bodyTarget)) {
        bodyOrLeave = bodyTarget;
        return true;
    }
    return false;
}

// The C# MatchRoslynEmptyStringCaseBlockHead: the two-block empty-string
// case (a null comparison + a get_Length comparison).
bool MatchRoslynEmptyStringCaseBlockHead(Block* target, ILVariable* switchValueVar,
                                         ILInstruction*& bodyOrLeave,
                                         Block*& defaultOrExitBlock) {
    bodyOrLeave = nullptr;
    defaultOrExitBlock = nullptr;
    if (target->Instructions.size() != 2 || target->IncomingEdgeCount != 1) return false;
    ILInstruction* nullCondition = nullptr;
    ILInstruction* exitBranch = nullptr;
    if (!MatchIfInstruction(target->Instructions[0].get(), nullCondition,
                            exitBranch)) {
        return false;
    }
    ILInstruction* arg = nullptr;
    if (!MatchCompEqualsNull(nullCondition, arg)) return false;
    ILVariable* nullCheckVar = nullptr;
    if (!MatchLdLoc(arg, nullCheckVar) || nullCheckVar != switchValueVar) return false;
    Block* lengthCheckBlock = nullptr;
    if (!MatchBranch(target->Instructions[1].get(), lengthCheckBlock)) return false;
    if (lengthCheckBlock->Instructions.size() != 2 ||
        lengthCheckBlock->IncomingEdgeCount != 1) {
        return false;
    }
    ILInstruction* lengthCondition = nullptr;
    ILInstruction* exitBranch2 = nullptr;
    if (!MatchIfInstruction(lengthCheckBlock->Instructions[0].get(),
                            lengthCondition, exitBranch2)) {
        return false;
    }
    ILInstruction* bodyBranch = nullptr;
    if (MatchLogicNot(lengthCondition, arg)) {
        bodyBranch = exitBranch2;
        exitBranch2 = lengthCheckBlock->Instructions[1].get();
        lengthCondition = arg;
    } else {
        bodyBranch = lengthCheckBlock->Instructions[1].get();
    }
    Block* exit2Target = nullptr;
    BlockContainer* exit2Leave = nullptr;
    if (MatchBranch(exitBranch2, exit2Target)) {
        defaultOrExitBlock = exit2Target;
    } else if (MatchLeave(exitBranch2, exit2Leave)) {
        // accepted
    } else {
        return false;
    }
    if (!MatchStringLengthCall(lengthCondition, switchValueVar)) return false;
    // The C# `exitBranch.Match(exitBranch2).Success` -- both branches must
    // target the same block.
    if (exit2Target != nullptr && exitBranch != nullptr) {
        Block* exitTarget = nullptr;
        if (MatchBranch(exitBranch, exitTarget) && exitTarget != exit2Target) return false;
    }
    Block* bodyTarget = nullptr;
    if (dynamic_cast<Leave*>(bodyBranch) != nullptr) {
        bodyOrLeave = bodyBranch;
        return true;
    }
    if (MatchBranch(bodyBranch, bodyTarget)) {
        bodyOrLeave = bodyTarget;
        return true;
    }
    return false;
}

// The C# IsNullCheckInDefaultBlock (the Roslyn >= 3.7 shape): the default
// case begins with if (ldloc switchVar == ldnull) br nullCase; br elseBlock.
bool IsNullCheckInDefaultBlock(ILInstruction*& exitOrDefault, ILVariable* switchVar,
                               Block*& nullValueCaseBlock) {
    nullValueCaseBlock = nullptr;
    auto* exitOrDefaultBlock = dynamic_cast<Block*>(exitOrDefault);
    if (exitOrDefaultBlock == nullptr) return false;
    if (exitOrDefaultBlock->Instructions.empty()) return false;
    ILInstruction* condition = nullptr;
    ILInstruction* thenBranch = nullptr;
    if (!MatchIfInstruction(exitOrDefaultBlock->Instructions[0].get(),
                            condition, thenBranch)) {
        return false;
    }
    ILInstruction* arg = nullptr;
    ILVariable* v = nullptr;
    if (!MatchCompEqualsNull(condition, arg) || !MatchLdLoc(arg, v) ||
        v != switchVar) {
        return false;
    }
    if (!MatchBranch(thenBranch, nullValueCaseBlock)) return false;
    if (nullValueCaseBlock->Parent != exitOrDefaultBlock->Parent) return false;
    if (exitOrDefaultBlock->Instructions.size() < 2) return false;
    Block* elseBlock = nullptr;
    if (!MatchBranch(exitOrDefaultBlock->Instructions[1].get(), elseBlock)) return false;
    if (elseBlock->Parent != exitOrDefaultBlock->Parent) return false;
    exitOrDefault = elseBlock;
    return true;
}

// The C# MatchRoslynSwitchOnString (the modern Roslyn arm) + its
// ReplaceWithSwitchInstruction local function.
bool MatchRoslynSwitchOnString(Block& block, int& i, ILTransformContext& context) {
    auto& instructions = block.Instructions;
    if (i >= static_cast<int>(instructions.size()) - 1) return false;
    // stloc switchValueVar(switchValue)
    // if (comp(ldloc switchValueVar == ldnull)) br nullCase
    // br nextBlock
    auto* instructionsPtr = &instructions;
    int switchBlockInstructionsOffset = i;
    Block* nullValueCaseBlock = nullptr;
    ILInstruction* instForNullCheck = nullptr;
    ILInstruction* condition = nullptr;
    ILInstruction* exitBlockJump = nullptr;
    if (MatchIfInstruction(instructions[i].get(), condition, exitBlockJump) &&
        MatchCompEqualsNull(condition, instForNullCheck)) {
        Branch* nextBlockJump = dynamic_cast<Branch*>(instructions[i + 1].get());
        if (nextBlockJump == nullptr ||
            nextBlockJump->TargetBlock == nullptr ||
            nextBlockJump->TargetBlock->IncomingEdgeCount != 1) {
            return false;
        }
        if (!MatchBranch(exitBlockJump, nullValueCaseBlock)) return false;
        instructionsPtr = &nextBlockJump->TargetBlock->Instructions;
        switchBlockInstructionsOffset = 0;
    }
    // stloc switchValueVar(call ComputeStringHash(switchValueLoad))
    // switch (ldloc switchValueVar) { ... }
    if (!(static_cast<int>(instructionsPtr->size()) > switchBlockInstructionsOffset + 1)) return false;
    auto* switchInst = dynamic_cast<SwitchInstruction*>(
        (*instructionsPtr)[switchBlockInstructionsOffset + 1].get());
    ILVariable* switchValueVar = nullptr;
    ILVariable* hashVar = nullptr;
    if (switchInst == nullptr ||
        !MatchLdLoc(switchInst->Value.get(), hashVar) ||
        !MatchComputeStringOrReadOnlySpanHashCall(
            (*instructionsPtr)[switchBlockInstructionsOffset].get(), hashVar,
            switchValueVar)) {
        return false;
    }
    ILVariable* hashVarCheck = nullptr;
    if (instForNullCheck != nullptr) {
        if (!MatchLdLoc(instForNullCheck, hashVarCheck) ||
            hashVarCheck != switchValueVar) {
            return false;
        }
    }
    struct StringValue {
        std::optional<std::string> value;
        ILInstruction* targetOrLeave = nullptr;
    };
    std::vector<StringValue> stringValues;
    // The default section: the one with the most labels (the C#
    // GetDefaultSection picks the widest label set).
    SwitchSection* defaultSection = switchInst->Sections.front().get();
    for (auto& section : switchInst->Sections) {
        if (section->Labels.Intervals().size() >
            defaultSection->Labels.Intervals().size()) {
            defaultSection = section.get();
        }
    }
    Block* exitOrDefaultBlock = nullptr;
    BlockContainer* defaultLeave = nullptr;
    if (defaultSection->Body != nullptr &&
        MatchBranch(defaultSection->Body.get(), exitOrDefaultBlock)) {
        // accepted
    } else if (defaultSection->Body != nullptr &&
               MatchLeave(defaultSection->Body.get(), defaultLeave)) {
        // accepted
    } else {
        return false;
    }
    for (auto& section : switchInst->Sections) {
        if (section.get() == defaultSection) continue;
        Block* target = nullptr;
        if (!MatchBranch(section->Body.get(), target)) return false;
        std::string stringValue;
        bool emptyStringEqualsNull = false;
        ILInstruction* targetOrLeave = nullptr;
        Block* currentExitBlock = nullptr;
        if (MatchRoslynEmptyStringCaseBlockHead(
                target, switchValueVar, targetOrLeave, currentExitBlock)) {
            stringValue = "";
            emptyStringEqualsNull = false;
        } else if (!MatchRoslynCaseBlockHead(target, switchValueVar,
                                             targetOrLeave, currentExitBlock,
                                             stringValue,
                                             emptyStringEqualsNull)) {
            return false;
        }
        if (currentExitBlock != exitOrDefaultBlock) return false;
        if (emptyStringEqualsNull && stringValue.empty()) {
            stringValues.push_back({std::nullopt, targetOrLeave});
            stringValues.push_back({std::string(), targetOrLeave});
        } else {
            stringValues.push_back({stringValue, targetOrLeave});
        }
    }
    if (nullValueCaseBlock != nullptr && exitOrDefaultBlock != nullValueCaseBlock) {
        stringValues.push_back({std::nullopt, nullptr});
        stringValues.back().targetOrLeave = nullptr;
        // The null-case body: a branch to nullValueCaseBlock, built below
        // (the C# stores the Block directly).
        stringValues.back().targetOrLeave = nullValueCaseBlock;
    }
    // In newer Roslyn versions (>= 3.7) the null check appears in the default
    // case, not prior to the switch.
    ILInstruction* exitOrDefault = exitOrDefaultBlock;
    bool hasNullValue = false;
    for (auto& pair : stringValues) {
        if (!pair.value.has_value()) hasNullValue = true;
    }
    if (!hasNullValue &&
        IsNullCheckInDefaultBlock(exitOrDefault, switchValueVar,
                                  nullValueCaseBlock)) {
        stringValues.push_back({std::nullopt, nullValueCaseBlock});
    }
    exitOrDefaultBlock = dynamic_cast<Block*>(exitOrDefault);

    context.StepOnce("MatchRoslynSwitchOnString");
    if (exitOrDefaultBlock != nullptr && defaultSection->Body != nullptr &&
        defaultSection->Body->Op == OpCode::Branch) {
        // Change TargetBlock in case it was modified by
        // IsNullCheckInDefaultBlock().
        static_cast<Branch*>(defaultSection->Body.get())->TargetBlock =
            exitOrDefaultBlock;
    }
    ILInstruction* switchValueInst = nullptr;
    {
        // The C# `switchValueInst = switchValueLoad` -- the port's
        // MatchComputeStringOrReadOnlySpanHashCall returned the variable; the
        // load is re-synthesised below.
        auto ldloc = std::make_unique<LdLoc>(
            std::shared_ptr<ILVariable>());
        (void)ldloc;
        switchValueInst = nullptr;
    }
    const bool sameInstructions = instructionsPtr == &instructions;
    // The switchValueInst construction: the C# uses the LdLoc node from the
    // hash call's argument. Rebuild it from the variable.
    auto switchValueLdLoc = [&switchValueVar]() {
        return std::unique_ptr<ILInstruction>(
            new LdLoc(ILVariablePtr(std::shared_ptr<ILVariable>(), switchValueVar)));
    };
    if (sameInstructions) {
        // stloc switchValueLoadVariable(switchValue)
        // stloc switchValueVar(call ComputeStringHash(ldloc switchValueLoadVariable))
        // switch (ldloc switchValueVar) { ... }
        bool keepAssignmentBefore;
        if (i >= 1) {
            ILVariable* stlocVar = nullptr;
            ILInstruction* switchValueTmp = nullptr;
            if (MatchStLoc(instructions[i - 1].get(), stlocVar, switchValueTmp) &&
                stlocVar == switchValueVar && switchValueVar->IsSingleDefinition() &&
                switchValueVar->LoadCount == static_cast<int>(switchInst->Sections.size())) {
                switchValueInst = switchValueTmp;
                keepAssignmentBefore = false;
            } else {
                keepAssignmentBefore = true;
            }
        } else {
            keepAssignmentBefore = true;
        }
        // The StringToInt argument: the C# passes the raw switchValue
        // instruction (taking ownership); the port clones it into the node.
        std::unique_ptr<ILInstruction> argument;
        if (switchValueInst != nullptr) {
            argument = switchValueInst->Clone();
        } else {
            argument = switchValueLdLoc();
        }
        auto stringToInt = std::make_unique<StringToInt>(
            std::move(argument),
            std::vector<std::pair<std::optional<std::string>, int>>{},
            nullptr);
        // Fill the map from the collected values.
        for (std::size_t idx = 0; idx < stringValues.size(); idx++) {
            stringToInt->Map.emplace_back(stringValues[idx].value,
                                          static_cast<int>(idx));
        }
        auto newSwitch = std::make_unique<SwitchInstruction>(std::move(stringToInt));
        // Sections: one per value + the default.
        for (std::size_t idx = 0; idx < stringValues.size(); idx++) {
            ILInstruction* body = stringValues[idx].targetOrLeave;
            if (auto* b = dynamic_cast<Block*>(body)) {
                newSwitch->Sections.push_back(
                    MakeSection(Util::LongSet(static_cast<long long>(idx)),
                                std::make_unique<Branch>(b)));
            } else if (body != nullptr) {
                newSwitch->Sections.push_back(
                    MakeSection(Util::LongSet(static_cast<long long>(idx)),
                                body->Clone()));
            } else {
                return false;
            }
        }
        {
            Util::LongSet defaultLabel(
                Util::LongInterval(0, static_cast<long long>(stringValues.size())));
            auto def = MakeSection(defaultLabel.Invert(),
                                   defaultSection->Body != nullptr
                                       ? defaultSection->Body->Clone()
                                       : nullptr);
            newSwitch->Sections.push_back(std::move(def));
        }
        // Replace the stloc-ComputeStringHash with the new switch.
        newSwitch->StartILOffset = instructions[i]->StartILOffset;
        newSwitch->EndILOffset = instructions[i]->EndILOffset;
        ReplaceAt(block, i, std::move(newSwitch));
        // Remove the old switch instruction.
        RemoveRange(block, i + 1, 1);
        // Remove the extra assignment.
        if (!keepAssignmentBefore) {
            RemoveRange(block, i - 1, 1);
            i -= 1;
        }
    } else {
        // The null-check form: the null check at i is replaced by the switch.
        bool keepAssignmentBefore = true;
        std::unique_ptr<ILInstruction> argument;
        if (i >= 2) {
            ILVariable* temporary = nullptr;
            ILInstruction* temporaryValue = nullptr;
            ILVariable* tempLoadVar = nullptr;
            ILInstruction* tempLoadValue = nullptr;
            if (MatchStLoc(instructions[i - 2].get(), temporary, temporaryValue) &&
                MatchStLoc(instructions[i - 1].get(), tempLoadVar, tempLoadValue) &&
                [&] {
                    ILVariable* v = nullptr;
                    return MatchLdLoc(tempLoadValue, v) && v == temporary;
                }() &&
                [&] {
                    // The hash stloc's value is the ldloc of the temp.
                    auto* hashStloc = dynamic_cast<StLoc*>(
                        (*instructionsPtr)[0].get());
                    return hashStloc != nullptr &&
                           hashStloc->Variable.get() == hashVar &&
                           [&] {
                               ILVariable* v = nullptr;
                               return MatchLdLoc(hashStloc->Value.get(), v) &&
                                      v == switchValueVar;
                           }();
                }() &&
                switchValueVar->IsSingleDefinition() &&
                switchValueVar->LoadCount == static_cast<int>(switchInst->Sections.size())) {
                argument = temporaryValue->Clone();
                keepAssignmentBefore = false;
            } else {
                argument = switchValueLdLoc();
            }
        } else {
            argument = switchValueLdLoc();
        }
        auto stringToInt = std::make_unique<StringToInt>(
            std::move(argument),
            std::vector<std::pair<std::optional<std::string>, int>>{},
            nullptr);
        for (std::size_t idx = 0; idx < stringValues.size(); idx++) {
            stringToInt->Map.emplace_back(stringValues[idx].value,
                                          static_cast<int>(idx));
        }
        auto newSwitch = std::make_unique<SwitchInstruction>(std::move(stringToInt));
        for (std::size_t idx = 0; idx < stringValues.size(); idx++) {
            ILInstruction* body = stringValues[idx].targetOrLeave;
            if (auto* b = dynamic_cast<Block*>(body)) {
                newSwitch->Sections.push_back(
                    MakeSection(Util::LongSet(static_cast<long long>(idx)),
                                std::make_unique<Branch>(b)));
            } else if (body != nullptr) {
                newSwitch->Sections.push_back(
                    MakeSection(Util::LongSet(static_cast<long long>(idx)),
                                body->Clone()));
            } else {
                return false;
            }
        }
        {
            Util::LongSet defaultLabel(
                Util::LongInterval(0, static_cast<long long>(stringValues.size())));
            auto def = MakeSection(defaultLabel.Invert(),
                                   defaultSection->Body != nullptr
                                       ? defaultSection->Body->Clone()
                                       : nullptr);
            newSwitch->Sections.push_back(std::move(def));
        }
        newSwitch->StartILOffset = instructions[i]->StartILOffset;
        newSwitch->EndILOffset = instructions[i]->EndILOffset;
        ReplaceAt(block, i, std::move(newSwitch));
        // Remove the jump to the switch block.
        RemoveRange(block, i + 1, 1);
        if (!keepAssignmentBefore) {
            RemoveRange(block, i - 2, 2);
            i -= 2;
        }
    }
    return true;
}


// ---- The cascading-if arm (SimplifyCascadingIfStatements) -----------------

bool SimplifyCascadingIfStatements(Block& block, int& i,
                                   ILTransformContext& context);
bool SimplifyCSharp1CascadingIfStatements(Block& block, int& i,
                                          ILTransformContext& context);

// Port of SimplifyCascadingIfStatements: the Roslyn cascading-if shape
//   if (op_Equality(ldloc switchValueVar, ldstr value)) br firstBlock
//   br nextCaseJump
// folded into a SwitchInstruction over a StringToInt dispatch. The C#-1
// IsInterned and the legacy shapes are deferred (see the header).
ILInstruction* MatchCaseBlock(Block* currentBlock, ILVariable* switchVariable,
                              std::string& value, bool& emptyStringEqualsNull,
                              ILInstruction*& caseBlockOrLeave) {
    value.clear();
    caseBlockOrLeave = nullptr;
    emptyStringEqualsNull = false;
    if (currentBlock == nullptr || currentBlock->IncomingEdgeCount != 1 ||
        currentBlock->Instructions.size() != 2) {
        return nullptr;
    }
    ILInstruction* condition = nullptr;
    ILInstruction* caseBlockBranch = nullptr;
    ILInstruction* nextBlockBranch = nullptr;
    // The C# MatchIfAtEndOfBlock: the if sits at Count-2, the false branch is
    // the block's last instruction; swap for logic.not.
    if (!MatchIfInstruction(currentBlock->Instructions[0].get(), condition,
                            caseBlockBranch) ||
        currentBlock->Instructions.size() != 2) {
        return nullptr;
    }
    nextBlockBranch = currentBlock->Instructions[1].get();
    ILInstruction* arg = nullptr;
    while (MatchLogicNot(condition, arg)) {
        std::swap(caseBlockBranch, nextBlockBranch);
        condition = arg;
    }
    bool isVB = false;
    if (!MatchStringEqualityComparisonFor(condition, switchVariable, value,
                                          isVB)) {
        return nullptr;
    }
    if (isVB) {
        std::swap(caseBlockBranch, nextBlockBranch);
        emptyStringEqualsNull = true;
    }
    Block* caseBlock = nullptr;
    if (MatchBranch(caseBlockBranch, caseBlock)) {
        caseBlockOrLeave = caseBlock;
    } else if (dynamic_cast<Leave*>(caseBlockBranch) != nullptr) {
        caseBlockOrLeave = caseBlockBranch;
    } else {
        return nullptr;
    }
    Block* nextBlock = nullptr;
    BlockContainer* nextLeave = nullptr;
    if (MatchBranch(nextBlockBranch, nextBlock)) {
        return nextBlock;
    }
    if (MatchLeave(nextBlockBranch, nextLeave)) {
        return nextLeave;
    }
    return nullptr;
}

bool SimplifyCascadingIfStatementsImpl(Block& block, int& i,
                                       ILTransformContext& context) {
    auto& instructions = block.Instructions;
    if (i + 1 >= static_cast<int>(instructions.size())) return false;
    ILInstruction* condition = nullptr;
    ILInstruction* firstBlockOrDefaultJump = nullptr;
    if (!MatchIfInstruction(instructions[i].get(), condition,
                            firstBlockOrDefaultJump)) {
        return false;
    }
    ILInstruction* nextCaseJump = instructions[i + 1].get();
    ILInstruction* arg = nullptr;
    while (MatchLogicNot(condition, arg)) {
        condition = arg;
        std::swap(firstBlockOrDefaultJump, nextCaseJump);
    }
    ILVariable* switchValueVar = nullptr;
    std::string firstBlockValue;
    bool isVBCompareString = false;
    if (!MatchStringEqualityComparison(condition, switchValueVar,
                                       firstBlockValue, isVBCompareString)) {
        return false;
    }
    if (isVBCompareString) {
        std::swap(firstBlockOrDefaultJump, nextCaseJump);
    }
    Block* firstBlock = nullptr;
    BlockContainer* firstLeave = nullptr;
    if (MatchBranch(firstBlockOrDefaultJump, firstBlock)) {
        // success
    } else if (MatchLeave(firstBlockOrDefaultJump, firstLeave)) {
        firstBlock = nullptr;
    } else {
        return false;
    }
    struct Value {
        std::optional<std::string> value;
        ILInstruction* inst = nullptr;  // a Block* or a cloned instruction
    };
    std::vector<Value> values;
    std::set<std::string> uniqueValues;
    int numberOfUniqueMatches = 0;
    std::set<Block*> caseBlocks;
    caseBlocks.insert(dynamic_cast<Block*>(instructions[i].get() != nullptr &&
                                           instructions[i]->Parent != nullptr
                                       ? instructions[i]->Parent
                                       : nullptr));
    caseBlocks.erase(nullptr);
    auto addSwitchSection = [&](const std::optional<std::string>& value,
                                ILInstruction* inst) {
        if (value.has_value() && !uniqueValues.insert(*value).second) return false;
        numberOfUniqueMatches++;
        values.push_back({value, inst});
        return true;
    };
    ILInstruction* switchValue = nullptr;
    if (isVBCompareString && firstBlockValue.empty()) {
        if (!addSwitchSection(std::nullopt,
                              firstBlock != nullptr
                                  ? static_cast<ILInstruction*>(firstBlock)
                                  : firstBlockOrDefaultJump)) {
            return false;
        }
        if (!addSwitchSection(std::string(),
                              firstBlock != nullptr
                                  ? static_cast<ILInstruction*>(firstBlock)
                                  : firstBlockOrDefaultJump)) {
            return false;
        }
    } else {
        if (!addSwitchSection(firstBlockValue,
                              firstBlock != nullptr
                                  ? static_cast<ILInstruction*>(firstBlock)
                                  : firstBlockOrDefaultJump)) {
            return false;
        }
    }
    bool removeExtraLoad = false;
    bool keepAssignmentBefore = false;
    ILInstruction* switchValueTmp = nullptr;
    ILVariable* stlocVar = nullptr;
    if (i >= 1 && MatchStLoc(instructions[i - 1].get(), stlocVar,
                             switchValueTmp) &&
        stlocVar == switchValueVar) {
        // stloc switchValueVar(switchValue)
        ILVariable* otherSwitchValueVar = nullptr;
        ILInstruction* otherValue = nullptr;
        if (i >= 2 && MatchLdLoc(switchValueTmp, otherSwitchValueVar) &&
            otherSwitchValueVar->IsSingleDefinition() &&
            otherSwitchValueVar->LoadCount == 1 &&
            MatchStLoc(instructions[i - 2].get(), otherSwitchValueVar,
                       otherValue)) {
            switchValue = otherValue;
            removeExtraLoad = true;
        } else {
            switchValue = switchValueTmp;
        }
    } else if (i >= 1 && dynamic_cast<StLoc*>(instructions[i - 1].get()) != nullptr) {
        ILVariable* stlocVar2 = nullptr;
        ILInstruction* stlocValue = nullptr;
        MatchStLoc(instructions[i - 1].get(), stlocVar2, stlocValue);
        ILVariable* loaded = nullptr;
        if (stlocValue != nullptr && MatchLdLoc(stlocValue, loaded) &&
            loaded == switchValueVar) {
            // The optimized legacy two-store shape.
            ILVariable* originalVar = switchValueVar;
            switchValueVar = stlocVar2;
            numberOfUniqueMatches = 0;
            ILInstruction* originalValue = nullptr;
            if (i >= 2 &&
                MatchStLoc(instructions[i - 2].get(), stlocVar2,
                           originalValue) &&
                stlocVar2 == originalVar &&
                originalVar->IsSingleDefinition() && originalVar->LoadCount == 2) {
                // The C# uses `instructions[i - 2].MatchStLoc(otherSwitchValueVar, out switchValue)`:
                // the load variable is the ORIGINAL switch variable.
                (void)stlocVar2;
                switchValue = originalValue;
                removeExtraLoad = true;
            } else {
                switchValue = new LdLoc(ILVariablePtr(
                    std::shared_ptr<ILVariable>(), originalVar));
            }
        } else {
            keepAssignmentBefore = true;
            switchValue = new LdLoc(
                ILVariablePtr(std::shared_ptr<ILVariable>(), switchValueVar));
        }
    } else {
        keepAssignmentBefore = true;
        switchValue = new LdLoc(
            ILVariablePtr(std::shared_ptr<ILVariable>(), switchValueVar));
    }
    // The variable's type gate: string, or ReadOnlySpan<char>/Span<char> when
    // the setting allows.
    if (switchValueVar == nullptr ||
        !IsKnownType(switchValueVar->Type, TS::KnownTypeCode::String)) {
        if (!context.Settings.SwitchOnReadOnlySpanChar) return false;
        if (!IsKnownType(switchValueVar->Type, TS::KnownTypeCode::ReadOnlySpanOfT) &&
            !IsKnownType(switchValueVar->Type, TS::KnownTypeCode::SpanOfT)) {
            return false;
        }
    }
    // The if instruction must be followed by a branch to the next case.
    Block* currentCaseBlock = nullptr;
    if (!MatchBranch(nextCaseJump, currentCaseBlock)) return false;
    // Extract the cases.
    while (true) {
        std::string value;
        bool emptyStringEqualsNull = false;
        ILInstruction* caseBlockOrLeave = nullptr;
        ILInstruction* nextCaseBlock = MatchCaseBlock(
            currentCaseBlock, switchValueVar, value, emptyStringEqualsNull,
            caseBlockOrLeave);
        if (nextCaseBlock == nullptr) break;
        if (emptyStringEqualsNull && value.empty()) {
            if (!addSwitchSection(std::nullopt, caseBlockOrLeave)) return false;
            if (!addSwitchSection(std::string(), caseBlockOrLeave)) return false;
        } else {
            if (!addSwitchSection(value, caseBlockOrLeave)) return false;
        }
        caseBlocks.insert(currentCaseBlock);
        currentCaseBlock = dynamic_cast<Block*>(nextCaseBlock);
        if (currentCaseBlock == nullptr) break;
    }
    // Short if-chains are more plausibly hand-written than a switch, so at
    // least 3 cases are required. The C# allows fewer when the chain ends in
    // a compiler-generated throw helper (the
    // SwitchDetection.IsSwitchExpressionThrowHelperBlock gate); that check is
    // deferred with the throw-helper surface.
    if (static_cast<int>(values.size()) < 3) return false;
    context.StepOnce("SimplifyCascadingIfStatements");
    // If the switchValueVar is used elsewhere too, keep the store. The C#
    // also validates that every load lives inside the case blocks
    // (ValidateUsesOfSwitchValueVariable over ILVariable.LoadInstructions);
    // the port's ILVariable does not track load lists, so a conservative
    // LoadCount comparison drives the same decision.
    if (switchValueVar->LoadCount > numberOfUniqueMatches) {
        keepAssignmentBefore = true;
        removeExtraLoad = false;
        switchValue = new LdLoc(
            ILVariablePtr(std::shared_ptr<ILVariable>(), switchValueVar));
    }
    int offset = firstBlock == nullptr ? 1 : 0;
    // Sections: skip the offset leading entries (the null/empty pair), label
    // the rest 0..N-1.
    std::vector<Value> sectionValues(values.begin() + offset, values.end());
    auto newSwitch = std::make_unique<SwitchInstruction>(
        std::unique_ptr<ILInstruction>(switchValue));
    for (std::size_t idx = 0; idx < sectionValues.size(); idx++) {
        ILInstruction* body = sectionValues[idx].inst;
        std::unique_ptr<ILInstruction> bodyInst;
        if (auto* b = dynamic_cast<Block*>(body)) {
            bodyInst = std::make_unique<Branch>(b);
        } else if (body != nullptr) {
            bodyInst = body->Clone();
        } else {
            return false;
        }
        newSwitch->Sections.push_back(MakeSection(
            Util::LongSet(static_cast<long long>(idx)), std::move(bodyInst)));
    }
    {
        Util::LongSet labels(
            Util::LongInterval(0, static_cast<long long>(sectionValues.size())));
        // The default: a branch to currentCaseBlock (the fall-through exit) or
        // a leave of the container nextCaseJump left.
        BlockContainer* leaveTarget = nullptr;
        std::unique_ptr<ILInstruction> defaultBody;
        if (currentCaseBlock != nullptr) {
            defaultBody = std::make_unique<Branch>(currentCaseBlock);
        } else if (nextCaseJump != nullptr &&
                   MatchLeave(nextCaseJump, leaveTarget) &&
                   leaveTarget != nullptr) {
            defaultBody = std::make_unique<Leave>(leaveTarget);
        } else {
            return false;
        }
        newSwitch->Sections.push_back(
            MakeSection(labels.Invert(), std::move(defaultBody)));
    }
    // Emit: replace the matched instruction range with the switch.
    newSwitch->StartILOffset = instructions[i]->StartILOffset;
    newSwitch->EndILOffset = instructions[i]->EndILOffset;
    if (removeExtraLoad) {
        newSwitch->StartILOffset = instructions[i - 2]->StartILOffset;
        ReplaceAt(block, i - 2, std::move(newSwitch));
        RemoveRange(block, i - 1, 3);
        i -= 2;
    } else {
        if (keepAssignmentBefore) {
            ReplaceAt(block, i, std::move(newSwitch));
            RemoveRange(block, i + 1, 1);
        } else {
            ReplaceAt(block, i - 1, std::move(newSwitch));
            RemoveRange(block, i, 2);
            i--;
        }
    }
    return true;
}

bool SimplifyCascadingIfStatements(Block& block, int& i,
                                   ILTransformContext& context) {
    return SimplifyCascadingIfStatementsImpl(block, i, context);
}


// ---- The C#1 string.IsInterned cascading-if arm ---------------------------


// The C# `context.TypeSystem.FindType(code)`: the non-null const IType& is
// wrapped via shared_from_this (the compilation-owned-reference convention).
// Returns null when the context carries no type system.
namespace {
TypeSystem::ITypePtr FindType(TypeSystem::ICompilation* compilation,
                              TypeSystem::KnownTypeCode code) {
    if (compilation == nullptr) return nullptr;
    return const_cast<TypeSystem::IType&>(compilation->FindType(code))
        .shared_from_this();
}
} // namespace

// The C# IsIsInternedCall: call String::IsInterned(arg) (the resolved-Method
// form or the reader's stand-in).
bool IsIsInternedCall(ILInstruction* inst, ILInstruction*& argument) {
    argument = nullptr;
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->Arguments.size() != 1) return false;
    if (call->Method != nullptr) {
        TS::ITypePtr declaring = call->Method->DeclaringType();
        if (!call->Method->IsStatic() || declaring == nullptr ||
            !(declaring->Namespace() == "System" &&
              declaring->Name() == "String") ||
            call->Method->Name() != "IsInterned") {
            return false;
        }
    } else if (call->MethodName != "System.String::IsInterned") return false;
    argument = call->Arguments[0].get();
    return true;
}

// The C# SimplifyCSharp1CascadingIfStatements: the C# 2.0 compiler's shape
//   stloc switchValueVar(ldloc temp)
//   if (comp(ldloc temp == ldnull)) br defaultOrNullBlock
//   br isInternedBlock
//   isInternedBlock: stloc switchValueVarCopy(call IsInterned(ldloc switchValueVar));
//                    if (comp(copy == ldstr caseN)) br caseBlockN; br next
// folded into a SwitchInstruction over the interned-copy variable.
bool SimplifyCSharp1CascadingIfStatementsImpl(Block& block, int& i,
                                              ILTransformContext& context) {
    auto& instructions = block.Instructions;
    if (i < 1) return false;
    if (i + 1 >= static_cast<int>(instructions.size())) return false;
    ILInstruction* condition = nullptr;
    ILInstruction* defaultBlockJump = nullptr;
    if (!MatchIfInstruction(instructions[i].get(), condition,
                            defaultBlockJump)) {
        return false;
    }
    Block* isInternedBlock = nullptr;
    if (!MatchBranch(instructions[i + 1].get(), isInternedBlock)) return false;
    Block* defaultOrNullBlock = nullptr;
    if (!MatchBranch(defaultBlockJump, defaultOrNullBlock)) return false;
    ILInstruction* tempLoad = nullptr;
    if (!MatchCompEqualsNull(condition, tempLoad)) return false;
    ILVariable* temp = nullptr;
    if (!MatchLdLoc(tempLoad, temp)) return false;
    if (!(temp->Kind == VariableKind::StackSlot && temp->LoadCount == 2)) return false;
    ILVariable* switchValueVar = nullptr;
    ILInstruction* switchValue = nullptr;
    StLoc* switchValueOwner = nullptr;
    if (!MatchStLoc(instructions[i - 1].get(), switchValueVar, switchValue)) return false;
    switchValueOwner = dynamic_cast<StLoc*>(instructions[i - 1].get());
    {
        ILVariable* loaded = nullptr;
        if (!(switchValue != nullptr && MatchLdLoc(switchValue, loaded) &&
              loaded == temp)) {
            return false;
        }
    }
    // match isInternedBlock:
    // stloc switchValueVarCopy(call IsInterned(ldloc switchValueVar))
    if (isInternedBlock->IncomingEdgeCount != 1 ||
        static_cast<int>(isInternedBlock->Instructions.size()) != 3) {
        return false;
    }
    ILVariable* switchValueVarCopy = nullptr;
    ILInstruction* internedArg = nullptr;
    {
        ILInstruction* arg = nullptr;
        if (!MatchStLoc(isInternedBlock->Instructions[0].get(),
                        switchValueVarCopy, arg)) {
            return false;
        }
        if (!IsIsInternedCall(arg, internedArg)) return false;
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(internedArg, loaded) || loaded != switchValueVar) return false;
    }
    switchValueVar = switchValueVarCopy;
    int conditionOffset = 1;
    Block* currentCaseBlock = isInternedBlock;
    struct Value {
        std::string value;
        ILInstruction* inst = nullptr;
    };
    std::vector<Value> values;
    // Each case starts with:
    // if (comp(ldloc switchValueVar == ldstr "case label")) br caseBlock
    // br currentCaseBlock
    while (true) {
        if (conditionOffset >=
            static_cast<int>(currentCaseBlock->Instructions.size())) {
            break;
        }
        ILInstruction* caseCondition = nullptr;
        ILInstruction* caseBlockJump = nullptr;
        if (!MatchIfInstruction(
                currentCaseBlock->Instructions[conditionOffset].get(),
                caseCondition, caseBlockJump)) {
            break;
        }
        if (static_cast<int>(currentCaseBlock->Instructions.size()) !=
            conditionOffset + 2) {
            break;
        }
        // The C# MatchCompEquals: a comp(left == right) equality comparison.
        auto* comp = dynamic_cast<Comp*>(caseCondition);
        if (comp == nullptr || comp->Kind != ComparisonKind::Equality) break;
        ILInstruction* left = comp->Left.get();
        ILInstruction* right = comp->Right.get();
        ILVariable* leftVar = nullptr;
        std::string value;
        if (!MatchLdLoc(left, leftVar) || leftVar != switchValueVar) break;
        if (!MatchLdStr(right, value)) break;
        Block* caseBlock = nullptr;
        BlockContainer* leaveTarget = nullptr;
        bool jumpHandled =
            MatchBranch(caseBlockJump, caseBlock) ||
            (MatchLeave(caseBlockJump, leaveTarget) &&
             leaveTarget ==
                 dynamic_cast<BlockContainer*>(currentCaseBlock->Parent));
        if (!jumpHandled) break;
        std::unique_ptr<ILInstruction> jumpCloneOwned = caseBlockJump->Clone();
        ILInstruction* jumpClone = jumpCloneOwned.release();
        Block* nextBlock = nullptr;
        if (!MatchBranch(
                currentCaseBlock->Instructions[conditionOffset + 1].get(),
                nextBlock)) {
            break;
        }
        conditionOffset = 0;
        values.push_back({value, jumpClone});
        currentCaseBlock = nextBlock;
    }
    if (static_cast<int>(values.size()) != switchValueVarCopy->LoadCount) return false;
    context.StepOnce("SimplifyCSharp1CascadingIfStatements");
    // switch contains case null:
    if (currentCaseBlock != defaultOrNullBlock) {
        values.push_back({std::string(), new Branch(defaultOrNullBlock)});
    }
    // The sections: one label per value; the default is the inverted
    // complement of the used labels, branching to currentCaseBlock. The
    // StringToInt argument OWNS the stloc's value: the C# passes the
    // MatchStLoc out-value (a reference to the stloc's child) and the stloc
    // is removed right after; the port MOVES the child out of the stloc
    // (releasing the unique_ptr) before the RemoveRange destroys it.
    std::unique_ptr<ILInstruction> argument;
    if (switchValueOwner != nullptr && switchValueOwner->Value != nullptr) {
        argument = std::move(switchValueOwner->Value);
    }
    // The C# `new StringToInt(switchValue, values.SelectArray(...),
    // context.TypeSystem.FindType(KnownTypeCode.String))` -- the StringToInt
    // node wraps the switch value; the map is (key, index) pairs.
    auto stringToInt = std::make_unique<StringToInt>(
        std::move(argument),
        std::vector<std::pair<std::optional<std::string>, int>>{},
        FindType(context.TypeSystem, TS::KnownTypeCode::String));
    for (std::size_t idx = 0; idx < values.size(); idx++) {
        stringToInt->Map.emplace_back(values[idx].value,
                                      static_cast<int>(idx));
    }
    auto newSwitch = std::make_unique<SwitchInstruction>(std::move(stringToInt));
    for (std::size_t idx = 0; idx < values.size(); idx++) {
        newSwitch->Sections.push_back(
            MakeSection(Util::LongSet(static_cast<long long>(idx)),
                        std::unique_ptr<ILInstruction>(values[idx].inst)));
    }
    {
        Util::LongSet labels(
            Util::LongInterval(0, static_cast<long long>(values.size())));
        newSwitch->Sections.push_back(
            MakeSection(labels.Invert(),
                        std::make_unique<Branch>(currentCaseBlock)));
    }
    // Emit: replace the if at i; remove the br at i+1 and the stloc at i-1.
    newSwitch->StartILOffset = instructions[i]->StartILOffset;
    ReplaceAt(block, i, std::move(newSwitch));
    RemoveRange(block, i + 1, 1);
    RemoveRange(block, i - 1, 1);
    i--;
    return true;
}

bool SimplifyCSharp1CascadingIfStatements(Block& block, int& i,
                                          ILTransformContext& context) {
    return SimplifyCSharp1CascadingIfStatementsImpl(block, i, context);
}


// ---- The legacy Dictionary<string,int> arm matchers -----------------------

// The C# `bool IsStringToIntDictionary(IType dictionaryType)`: a
// `System.Collections.Generic.Dictionary` with exactly the (String, Int32)
// type arguments. The C# compares `type.FullName` (arity-free); the port's
// `Name()` keeps the metadata arity suffix ("Dictionary`1"), so strip it.
bool IsStringToIntDictionary(const TS::IType& type) {
    if (type.Namespace() != "System.Collections.Generic") return false;
    std::string name = type.Name();
    std::size_t arity = name.find('`');
    if (arity != std::string::npos) name.resize(arity);
    if (name != "Dictionary") return false;
    auto* pt = dynamic_cast<const TS::ParameterizedType*>(&type);
    if (pt == nullptr || pt->TypeArguments().size() != 2) return false;
    const auto& args = pt->TypeArguments();
    return args[0] != nullptr &&
           TS::IsKnownType(*args[0], TS::KnownTypeCode::String) &&
           args[1] != nullptr &&
           TS::IsKnownType(*args[1], TS::KnownTypeCode::Int32);
}

// The C# `bool IsNonGenericHashtable(IType dictionaryType)`.
bool IsNonGenericHashtable(const TS::IType& type) {
    return type.Namespace() == "System.Collections" &&
           type.Name() == "Hashtable" && type.Kind() != TS::TypeKind::Unknown;
}

// The C# `bool MatchDictionaryFieldLoad(ILInstruction inst, Func<IType, bool>
// typeMatcher, out IField, out IType)`: `ldobj dictionaryType(ldsflda
// dictField)` over the port's LdsFlda stand-in (the FieldName carries the
// "$$method" compiler-generated prefix or the field is marked
// CompilerGenerated).
bool MatchDictionaryFieldLoad(
    ILInstruction* inst,
    const std::function<bool(const TS::IType&)>& typeMatcher,
    std::string& dictFieldName, TS::ITypePtr& dictionaryType) {
    dictFieldName.clear();
    dictionaryType = nullptr;
    auto* ldobj = dynamic_cast<LdObj*>(inst);
    if (ldobj == nullptr || ldobj->Type == nullptr) return false;
    if (!typeMatcher(*ldobj->Type)) return false;
    auto* ldsflda = dynamic_cast<LdsFlda*>(ldobj->Target.get());
    if (ldsflda == nullptr) return false;
    if (!ldsflda->IsCompilerGeneratedField &&
        ldsflda->FieldName.find("$$method") == std::string::npos) {
        return false;
    }
    dictFieldName = ldsflda->FieldName;
    dictionaryType = ldobj->Type;
    return true;
}

// The C# `bool MatchAddCall(IType dictionaryType, ILInstruction inst,
// ILVariable dictVar, out int index, out string value)`: `call Add(ldloc
// dictVar, ldstr value, ldc.i4 index)` or the box/String.Empty variants.
bool MatchAddCall(const TS::IType& dictionaryType, ILInstruction* inst,
                  ILVariable* dictVar, int& index, std::string& value) {
    value.clear();
    index = -1;
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || (call->Method == nullptr && call->MethodName.empty())) return false;
    // The method identity: the resolved-Method form (Name == "Add",
    // DeclaringType matches) or the reader's stand-in (the MethodName suffix).
    bool isAdd = false;
    if (call->Method != nullptr) {
        TS::ITypePtr declaring = call->Method->DeclaringType();
        isAdd = call->Method->Name() == "Add" && declaring != nullptr &&
                declaring->Namespace() == dictionaryType.Namespace() &&
                declaring->Name() == dictionaryType.Name() &&
                !call->Method->IsStatic();
    } else {
        isAdd = call->MethodName ==
                    dictionaryType.Namespace() + "." + dictionaryType.Name() +
                        "::Add";
    }
    if (!isAdd || call->Arguments.size() != 3) return false;
    {
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(call->Arguments[0].get(), loaded) ||
            loaded != dictVar) {
            return false;
        }
    }
    if (!MatchLdStr(call->Arguments[1].get(), value)) {
        // The C# `MatchLdsFld(out var field) && field.DeclaringType is String
        // && field.Name == "Empty"`: the port's shape is
        // ldobj(ldsflda String::Empty).
        auto* ldobj = dynamic_cast<LdObj*>(call->Arguments[1].get());
        LdsFlda* ldsflda =
            ldobj != nullptr
                ? dynamic_cast<LdsFlda*>(ldobj->Target.get())
                : nullptr;
        if (ldsflda == nullptr ||
            ldsflda->FieldName.find("System.String::Empty") ==
                std::string::npos) {
            return false;
        }
        value = "";
    }
    // The index: ldc.i4 or box(ldc.i4).
    if (auto* ldc = dynamic_cast<LdcI4*>(call->Arguments[2].get())) {
        index = ldc->Value;
        return true;
    }
    if (auto* box = dynamic_cast<Box*>(call->Arguments[2].get())) {
        if (auto* ldc = dynamic_cast<LdcI4*>(box->Argument.get())) {
            index = ldc->Value;
            return true;
        }
    }
    return false;
}

} // namespace ILSpy::Decompiler::IL

namespace ILSpy::Decompiler::IL {

bool SwitchOnStringProbes::ExtractStringValuesFromInitBlock(
    Block* block,
    std::vector<std::pair<std::optional<std::string>, int>>& values,
    Block*& blockAfterInit,
    const std::function<bool(const TS::IType&)>& typeMatcher,
    const TS::IType* dictionaryType, bool isHashtablePattern,
    std::string& errorMessage) {
    values.clear();
    blockAfterInit = nullptr;
    errorMessage.clear();
    if (block == nullptr || block->Instructions.size() < 3) {
        errorMessage = "the init block needs at least 3 instructions";
        return false;
    }
    // stloc dictVar(newobj Dictionary..ctor(ldc.i4 valuesLength))
    ILVariable* dictVar = nullptr;
    ILInstruction* newObjDict = nullptr;
    if (!MatchStLoc(block->Instructions[0].get(), dictVar, newObjDict) ||
        dictVar == nullptr) {
        errorMessage = "the init block does not start with the dictionary store";
        return false;
    }
    auto* newObj = dynamic_cast<Call*>(newObjDict);
    if (newObj == nullptr || !newObj->IsNewObj ||
        (newObj->Method == nullptr && newObj->MethodName.empty())) {
        errorMessage = "the init value is not a newobj";
        return false;
    }
    // The C# `newObj.Method.DeclaringType.Equals(dictionaryType)`; the port
    // compares the declaring type's namespace/name pair.
    {
        bool declaringMatches = false;
        if (newObj->Method != nullptr) {
            TS::ITypePtr declaring = newObj->Method->DeclaringType();
            declaringMatches = dictionaryType != nullptr && declaring != nullptr &&
                               declaring->Namespace() == dictionaryType->Namespace() &&
                               declaring->Name() == dictionaryType->Name();
        } else {
            declaringMatches = newObj->DeclaringType != nullptr &&
                               dictionaryType != nullptr &&
                               newObj->DeclaringType->Namespace() ==
                                   dictionaryType->Namespace() &&
                               newObj->DeclaringType->Name() ==
                                   dictionaryType->Name();
        }
        if (!declaringMatches) {
            errorMessage = "the ctor's declaring type does not match";
            return false;
        }
    }
    int valuesLength = 0;
    if (newObj->Arguments.size() == 2) {
        auto* cap = dynamic_cast<LdcI4*>(newObj->Arguments[0].get());
        auto* loadFactor = dynamic_cast<LdcF4*>(newObj->Arguments[1].get());
        if (cap == nullptr || loadFactor == nullptr || loadFactor->Value != 0.5f) {
            errorMessage = "the Hashtable ctor needs (capacity, 0.5f)";
            return false;
        }
        valuesLength = cap->Value;
    } else if (newObj->Arguments.size() == 1) {
        auto* cap = dynamic_cast<LdcI4*>(newObj->Arguments[0].get());
        if (cap == nullptr) {
            errorMessage = "the Dictionary ctor needs the values length";
            return false;
        }
        valuesLength = cap->Value;
    } else {
        errorMessage = "the ctor arity is not 1 or 2";
        return false;
    }
    if (valuesLength < 0) {
        errorMessage = "a negative values length";
        return false;
    }
    values.reserve(static_cast<std::size_t>(valuesLength));
    int i = 0;
    std::string value;
    int index = -1;
    while (MatchAddCall(*dictionaryType,
                        block->Instructions[static_cast<std::size_t>(i + 1)].get(),
                        dictVar, index, value)) {
        values.emplace_back(value, index);
        i++;
    }
    if (values.empty()) {
        errorMessage = "no Add calls found";
        return false;
    }
    // The final store: volatile.stobj dictionaryType(ldsflda dictionaryField,
    // ldloc dictVar).
    auto* stobj = dynamic_cast<StObj*>(
        block->Instructions[static_cast<std::size_t>(i + 1)].get());
    if (stobj == nullptr || stobj->Type == nullptr) {
        errorMessage = "the final store is not a stobj";
        return false;
    }
    if (dictionaryType != nullptr &&
        (stobj->Type->Namespace() != dictionaryType->Namespace() ||
         stobj->Type->Name() != dictionaryType->Name())) {
        errorMessage = "the stobj type does not match the dictionary type";
        return false;
    }
    auto* loadField = dynamic_cast<LdsFlda*>(stobj->Target.get());
    if (loadField == nullptr || !loadField->IsCompilerGeneratedField) {
        errorMessage = "the stobj target is not the compiler-generated field";
        return false;
    }
    ILVariable* dictVarLoad = nullptr;
    if (!MatchLdLoc(stobj->Value.get(), dictVarLoad) ||
        dictVarLoad != dictVar) {
        errorMessage = "the stobj value does not reload the dictionary local";
        return false;
    }
    const int finalIndex = i + 1;
    if (isHashtablePattern &&
        dynamic_cast<IfInstruction*>(
            block->Instructions[static_cast<std::size_t>(finalIndex + 1)].get()) !=
            nullptr) {
        if (finalIndex + 2 >= static_cast<int>(block->Instructions.size())) {
            errorMessage = "the hashtable pattern ends before the next branch";
            return false;
        }
        if (!MatchBranch(
                block->Instructions[static_cast<std::size_t>(finalIndex + 2)].get(),
                blockAfterInit)) {
            errorMessage = "the init block does not branch to the next block";
            return false;
        }
        return true;
    }
    if (finalIndex + 1 >= static_cast<int>(block->Instructions.size())) {
        errorMessage = "the init block does not end with the next block branch";
        return false;
    }
    if (!MatchBranch(
            block->Instructions[static_cast<std::size_t>(finalIndex + 1)].get(),
            blockAfterInit)) {
        errorMessage = "the init block does not end with a branch";
        return false;
    }
    return true;
}


// The C# `bool MatchLegacySwitchOnStringWithDict(InstructionCollection
// instructions, ref int i)`: the 5-block compiler-generated
// Dictionary<string,int> switch shape, folded into a SwitchInstruction over a
// StringToInt dispatch.
bool MatchLegacySwitchOnStringWithDictImpl(Block& block, int& i,
                                           ILTransformContext& context) {
    auto& instructions = block.Instructions;
    if (i < 1 || i + 1 >= static_cast<int>(instructions.size())) return false;
    // match first block: checking switch-value for null:
    // stloc switchValueVar(switchValue)   (optional for a parameter)
    // if (comp(ldloc switchValueVar == ldnull)) br nullCase
    // br nextBlock
    ILInstruction* condition = nullptr;
    ILInstruction* exitBlockJump = nullptr;
    if (!MatchIfInstruction(instructions[i].get(), condition, exitBlockJump)) return false;
    auto* comp = dynamic_cast<Comp*>(condition);
    if (comp == nullptr || comp->Kind != ComparisonKind::Equality) return false;
    ILInstruction* left = comp->Left.get();
    ILInstruction* right = comp->Right.get();
    if (right == nullptr || !MatchLdNull(right)) return false;
    ILVariable* switchValueVar = nullptr;
    if (!MatchLdLoc(left, switchValueVar) ||
        !switchValueVar->IsSingleDefinition()) {
            return false;
    }
    // If the switchValueVar is a stack slot with an assignment right before,
    // use the previously assigned variable as switchValueVar.
    ILInstruction* switchValue = nullptr;
    if (switchValueVar->Kind == VariableKind::StackSlot && i >= 1) {
        ILVariable* extraVar = nullptr;
        ILInstruction* extraValue = nullptr;
        if (MatchStLoc(instructions[i - 1].get(), extraVar, extraValue)) {
            ILVariable* loaded = nullptr;
            if (extraValue != nullptr && MatchLdLoc(extraValue, loaded) &&
                loaded == switchValueVar) {
                switchValueVar = extraVar;
                switchValue = extraValue;
            }
        }
    }
    if (!IsKnownType(switchValueVar->Type, TS::KnownTypeCode::String)) return false;
    // either br nullCase or leave container
    Block* nullValueCaseBlock = nullptr;
    BlockContainer* leaveContainer = nullptr;
    if (!MatchBranch(exitBlockJump, nullValueCaseBlock) &&
        !MatchLeave(exitBlockJump, leaveContainer)) {
            return false;
    }
    if (i + 1 >= static_cast<int>(instructions.size())) return false;
    auto* nextBlockJump = dynamic_cast<Branch*>(instructions[i + 1].get());
    if (nextBlockJump == nullptr || nextBlockJump->TargetBlock == nullptr ||
        nextBlockJump->TargetBlock->IncomingEdgeCount != 1) {
            return false;
    }
    Block* nextBlock = nextBlockJump->TargetBlock;
    // match second block: checking the compiler-generated Dictionary for null
    if (nextBlock->Instructions.size() != 2 ||
        !MatchIfInstruction(nextBlock->Instructions[0].get(), condition,
                            exitBlockJump)) {
        return false;
    }
    Block* tryGetValueBlock = nullptr;
    if (!MatchBranch(exitBlockJump, tryGetValueBlock)) return false;
    Block* dictInitBlock = nullptr;
    if (!MatchBranch(nextBlock->Instructions[1].get(), dictInitBlock) ||
        dictInitBlock->IncomingEdgeCount != 1) {
            return false;
    }
    // comp-not-equals(ldobj dictionaryType(ldsflda dictField), ldnull)
    auto* neqComp = dynamic_cast<Comp*>(condition);
    if (neqComp == nullptr || neqComp->Kind != ComparisonKind::Inequality) return false;
    ILInstruction* neqLeft = neqComp->Left.get();
    ILInstruction* neqRight = neqComp->Right.get();
    if (neqRight == nullptr || !MatchLdNull(neqRight)) return false;
    std::string dictFieldName;
    TS::ITypePtr dictionaryType;
    if (!MatchDictionaryFieldLoad(
            neqLeft,
            IsStringToIntDictionary,
            dictFieldName, dictionaryType)) {
                return false;
    }
    // match third block: the dictionary init (the Add-call walk).
    std::vector<std::pair<std::optional<std::string>, int>> stringValues;
    Block* blockAfterInit = nullptr;
    std::string extractError;
    if (!SwitchOnStringProbes::ExtractStringValuesFromInitBlock(
            dictInitBlock, stringValues, blockAfterInit,
            IsStringToIntDictionary,
            dictionaryType.get(), false, extractError)) {
                return false;
    }
    if (tryGetValueBlock != blockAfterInit) return false;
    // match fourth block: the TryGetValue check.
    if (tryGetValueBlock->IncomingEdgeCount != 2 ||
        tryGetValueBlock->Instructions.size() != 2) {
            return false;
    }
    ILInstruction* tryGetCondition = nullptr;
    ILInstruction* defaultBlockJump = nullptr;
    if (!MatchIfInstruction(tryGetValueBlock->Instructions[0].get(),
                            tryGetCondition, defaultBlockJump)) {
                                return false;
    }
    Block* defaultBlock = nullptr;
    BlockContainer* defaultLeave = nullptr;
    bool defaultHandled = MatchBranch(defaultBlockJump, defaultBlock) ||
                          MatchLeave(defaultBlockJump, defaultLeave);
    if (!defaultHandled) return false;
    // logic.not(call TryGetValue(ldobj(ldsflda field2), ldloc s, ldloca idx))
    ILInstruction* notArg = nullptr;
    if (!MatchLogicNot(tryGetCondition, notArg)) return false;
    auto* tryGetCall = dynamic_cast<Call*>(notArg);
    if (tryGetCall == nullptr || tryGetCall->Arguments.size() != 3) return false;
    bool isTryGetValue = false;
    if (tryGetCall->Method != nullptr) {
        isTryGetValue = tryGetCall->Method->Name() == "TryGetValue";
    } else {
        const std::string& mn = tryGetCall->MethodName;
        isTryGetValue =
            mn.size() > 2 && mn.substr(mn.rfind("::") + 2) == "TryGetValue";
    }
    if (!isTryGetValue) return false;
    std::string dictField2;
    TS::ITypePtr dictionaryType2;
    if (!MatchDictionaryFieldLoad(
            tryGetCall->Arguments[0].get(),
            IsStringToIntDictionary,
            dictField2, dictionaryType2) ||
        dictField2.empty()) {
            return false;
    }
    {
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(tryGetCall->Arguments[1].get(), loaded) ||
            loaded != switchValueVar) {
                return false;
        }
    }
    ILVariable* switchIndexVar = nullptr;
    {
        auto* ldloca = dynamic_cast<LdLoca*>(tryGetCall->Arguments[2].get());
        if (ldloca == nullptr || ldloca->Variable == nullptr) return false;
        switchIndexVar = ldloca->Variable.get();
    }
    Block* switchBlock = nullptr;
    if (!MatchBranch(tryGetValueBlock->Instructions[1].get(), switchBlock)) return false;
    // match fifth block: the switch instruction (or the mcs single-case if).
    if (switchBlock->IncomingEdgeCount != 1 ||
        switchBlock->Instructions.empty()) {
            return false;
    }
    std::vector<std::unique_ptr<SwitchSection>> sections;
    if (auto* switchInst =
            dynamic_cast<SwitchInstruction*>(switchBlock->Instructions[0].get())) {
        if (switchBlock->Instructions.size() != 1) return false;
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(switchInst->Value.get(), loaded) ||
            loaded != switchIndexVar) {
                return false;
        }
        for (auto& section : switchInst->Sections) {
            sections.push_back(std::move(section));
        }
    } else if (auto* ifInst = dynamic_cast<IfInstruction*>(
                   switchBlock->Instructions[0].get())) {
        // mcs: a single case compiled as a simple if.
        if (switchBlock->Instructions.size() != 2) return false;
        auto* eq = dynamic_cast<Comp*>(ifInst->Condition.get());
        if (eq == nullptr || eq->Kind != ComparisonKind::Equality) return false;
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(eq->Left.get(), loaded) || loaded != switchIndexVar) return false;
        if (eq->Right == nullptr || eq->Right->Op != OpCode::LdcI4 ||
            static_cast<LdcI4*>(eq->Right.get())->Value != 0) {
                return false;
        }
        auto section = std::make_unique<SwitchSection>(Util::LongSet(0));
        section->SetBody(std::move(ifInst->TrueInst));
        sections.push_back(std::move(section));
        auto defaultSection =
            std::make_unique<SwitchSection>(Util::LongSet(0).Invert());
        defaultSection->SetBody(std::move(switchBlock->Instructions[1]));
        sections.push_back(std::move(defaultSection));
    } else {
        return false;
    }
    context.StepOnce("MatchLegacySwitchOnStringWithDict");
    bool keepAssignmentBefore = false;
    if (switchValueVar->LoadCount > 2 || switchValue == nullptr) {
        switchValue = new LdLoc(
            ILVariablePtr(std::shared_ptr<ILVariable>(), switchValueVar));
        keepAssignmentBefore = true;
    }
    std::unique_ptr<ILInstruction> argument(switchValue);
    auto stringToInt = std::make_unique<StringToInt>(
        std::move(argument),
        std::vector<std::pair<std::optional<std::string>, int>>{},
        FindType(context.TypeSystem, TS::KnownTypeCode::String));
    for (std::size_t k = 0; k < stringValues.size(); k++) {
        stringToInt->Map.emplace_back(stringValues[k].first,
                                      static_cast<int>(stringValues[k].second));
    }
    auto newSwitch =
        std::make_unique<SwitchInstruction>(std::move(stringToInt));
    for (auto& section : sections) {
        newSwitch->Sections.push_back(std::move(section));
    }
    newSwitch->StartILOffset = instructions[i]->StartILOffset;
    newSwitch->EndILOffset = instructions[i]->EndILOffset;
    // Emit: replace the br at i+1 with the switch; remove the if.
    ReplaceAt(block, i + 1, std::move(newSwitch));
    if (keepAssignmentBefore) {
        RemoveRange(block, i, 1);
        i--;
    } else {
        RemoveRange(block, i - 1, 2);
        i -= 2;
    }
    return true;
}

// (the legacy-Dictionary section ends here)
// ---- The legacy Hashtable arm (ScanHashtableInitializerBlocks +
// MatchLegacySwitchOnStringWithHashtable) -----------------------------------

// The C# `HashtableInitializer ScanHashtableInitializerBlocks(Block
// entryPoint)`: walk the entry block's null-check/init chain and record the
// extracted (string, index) pairs per compiler-generated Hashtable field.
// The chain continues through each init block's second-to-last if (the next
// hashtable's null check branching to the same switch head); the map is
// keyed by the port's LdsFlda field name (the C# keys by IField).
std::map<std::string, HashtableInitializerInfo>
ScanHashtableInitializerBlocks(Block* entryPoint) {
    std::map<std::string, HashtableInitializerInfo> hashtables;
    if (entryPoint == nullptr || entryPoint->Instructions.size() != 2) {
        return hashtables;
    }
    ILInstruction* condition = nullptr;
    ILInstruction* branchToSwitchHead = nullptr;
    if (!MatchIfInstruction(entryPoint->Instructions[0].get(), condition,
                            branchToSwitchHead)) {
        return hashtables;
    }
    Block* tableInitBlock = nullptr;
    if (!MatchBranch(entryPoint->Instructions[1].get(), tableInitBlock)) {
        return hashtables;
    }
    // comp-not-equals(ldobj Hashtable(ldsflda field), ldnull)
    auto* comp = dynamic_cast<Comp*>(condition);
    if (comp == nullptr || comp->Kind != ComparisonKind::Inequality) {
        return hashtables;
    }
    ILInstruction* left = comp->Left.get();
    ILInstruction* right = comp->Right.get();
    if (right == nullptr || !MatchLdNull(right)) return hashtables;
    std::string dictFieldName;
    TS::ITypePtr dictionaryType;
    if (!MatchDictionaryFieldLoad(
            left,
            [](const TS::IType& t) { return IsNonGenericHashtable(t); },
            dictFieldName, dictionaryType)) {
        return hashtables;
    }
    Block* switchHead = nullptr;
    if (!MatchBranch(branchToSwitchHead, switchHead)) return hashtables;
    Block* previousBlock = entryPoint;
    while (tableInitBlock != nullptr) {
        if (tableInitBlock->IncomingEdgeCount != 1 ||
            tableInitBlock->Instructions.size() < 3) {
            break;
        }
        std::vector<std::pair<std::optional<std::string>, int>> stringValues;
        Block* blockAfterThisInitBlock = nullptr;
        std::string extractError;
        if (!SwitchOnStringProbes::ExtractStringValuesFromInitBlock(
                tableInitBlock, stringValues, blockAfterThisInitBlock,
                [](const TS::IType& t) { return IsNonGenericHashtable(t); },
                dictionaryType.get(), true, extractError)) {
            break;
        }
        // The second-to-last instruction may be the next hashtable's null
        // check (the multi-hashtable chain); the C# `SecondToLastOrDefault`.
        IfInstruction* nextHashtableInitHead = nullptr;
        if (tableInitBlock->Instructions.size() >= 2) {
            nextHashtableInitHead = dynamic_cast<IfInstruction*>(
                tableInitBlock
                    ->Instructions[tableInitBlock->Instructions.size() - 2]
                    .get());
        }
        HashtableInitializerInfo info;
        info.labels = std::move(stringValues);
        info.jumpToNext = nextHashtableInitHead;
        info.containingBlock = tableInitBlock;
        info.previous = previousBlock;
        info.next = blockAfterThisInitBlock;
        info.transformed = false;
        hashtables[dictFieldName] = std::move(info);
        previousBlock = tableInitBlock;
        if (nextHashtableInitHead == nullptr) break;
        // The chained init: the next field's null check must branch to the
        // same switch head; continue the walk with the next init block.
        ILInstruction* nextCondition = nullptr;
        ILInstruction* nextBranch = nullptr;
        if (!MatchIfInstruction(nextHashtableInitHead, nextCondition,
                                nextBranch)) {
            break;
        }
        auto* nextComp = dynamic_cast<Comp*>(nextCondition);
        if (nextComp == nullptr ||
            nextComp->Kind != ComparisonKind::Inequality ||
            nextComp->Right == nullptr || !MatchLdNull(nextComp->Right.get())) {
            break;
        }
        std::string nextFieldName;
        TS::ITypePtr nextDictionaryType;
        if (!MatchDictionaryFieldLoad(
                nextComp->Left.get(),
                [](const TS::IType& t) { return IsNonGenericHashtable(t); },
                nextFieldName, nextDictionaryType)) {
            break;
        }
        Block* nextSwitchHead = nullptr;
        if (!MatchBranch(nextBranch, nextSwitchHead) ||
            nextSwitchHead != switchHead) {
            break;
        }
        tableInitBlock = blockAfterThisInitBlock;
        dictFieldName = nextFieldName;
        dictionaryType = nextDictionaryType;
    }
    return hashtables;
}

// The port-side AddNullSection over the fold's section list: the null-case
// label sits after the highest scanned index; conflicting single-label
// sections are rejected like the C# `possibleConflicts` arms.
bool HashtableArmAddNullSection(
    std::vector<std::unique_ptr<SwitchSection>>& sections,
    const std::vector<std::pair<std::optional<std::string>, int>>&
        stringValues,
    std::unique_ptr<ILInstruction> body) {
    int maxIndex = -1;
    for (const auto& entry : stringValues) {
        if (entry.second > maxIndex) maxIndex = entry.second;
    }
    Util::LongSet label(
        Util::LongInterval(static_cast<long long>(maxIndex) + 1,
                           static_cast<long long>(maxIndex) + 2));
    std::vector<SwitchSection*> conflicts;
    for (auto& section : sections) {
        if (section->Labels.Overlaps(label)) conflicts.push_back(section.get());
    }
    if (conflicts.size() > 1) return false;
    if (conflicts.size() == 1) {
        if (conflicts[0]->Labels.Intervals().empty() ||
            conflicts[0]->Labels.Count() == 1) {
            return false;  // cannot remove the only label
        }
        conflicts[0]->Labels = conflicts[0]->Labels.ExceptWith(label);
    }
    sections.push_back(
        MakeSection(std::move(label), std::move(body)));
    return true;
}

// The C# `bool MatchLegacySwitchOnStringWithHashtable(Block block,
// HashtableInitializers hashtableInitializers, ref int i)`: the 4-instruction
// head + get_Item block + switch block fold, keyed by the scanned init-block
// info.
bool MatchLegacySwitchOnStringWithHashtableImpl(
    Block& block, int& i,
    std::map<std::string, HashtableInitializerInfo>& hashtableInitializers,
    ILTransformContext& context) {
    auto& instructions = block.Instructions;
    // The C# `block.Instructions.Count != i + 4` -- the 4-shape must end the
    // block's non-terminal instructions.
    if (i < 0 || i + 4 != static_cast<int>(instructions.size())) {
        return false;
    }
    // stloc tmp(ldloc switchValue); stloc switchVariable(ldloc tmp)
    ILVariable* tmp = nullptr;
    ILInstruction* switchValue = nullptr;
    if (!MatchStLoc(instructions[i].get(), tmp, switchValue) || tmp == nullptr) {
        return false;
    }
    ILVariable* switchVariable = nullptr;
    ILInstruction* tmpLoad = nullptr;
    if (!MatchStLoc(instructions[i + 1].get(), switchVariable, tmpLoad) ||
        switchVariable == nullptr || tmpLoad == nullptr) {
        return false;
    }
    {
        auto* ld = dynamic_cast<LdLoc*>(tmpLoad);
        if (ld == nullptr || ld->Variable.get() != tmp) return false;
    }
    // if (comp(ldloc tmp == ldnull)) br nullCaseBlock
    ILInstruction* condition = nullptr;
    ILInstruction* nullCaseBlockBranch = nullptr;
    if (!MatchIfInstruction(instructions[i + 2].get(), condition,
                            nullCaseBlockBranch)) {
        return false;
    }
    auto* comp = dynamic_cast<Comp*>(condition);
    if (comp == nullptr || comp->Kind != ComparisonKind::Equality ||
        comp->Right == nullptr || !MatchLdNull(comp->Right.get())) {
        return false;
    }
    {
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(comp->Left.get(), loaded) || loaded != tmp) {
            return false;
        }
    }
    if (i + 1 >= static_cast<int>(instructions.size())) return false;
    Block* getItemBlock = nullptr;
    if (!MatchBranch(instructions[i + 3].get(), getItemBlock)) return false;
    Block* nullCaseBlock = nullptr;
    BlockContainer* nullCaseLeave = nullptr;
    const bool nullCaseIsLeave =
        MatchLeave(nullCaseBlockBranch, nullCaseLeave);
    if (!nullCaseIsLeave &&
        !MatchBranch(nullCaseBlockBranch, nullCaseBlock)) {
        return false;
    }
    // match second block: get_Item on the compiler-generated Hashtable
    if (getItemBlock == nullptr || getItemBlock->IncomingEdgeCount != 1 ||
        getItemBlock->Instructions.size() != 4) {
        return false;
    }
    ILInstruction* getItem = nullptr;
    {
        ILVariable* tmp2 = nullptr;
        if (!MatchStLoc(getItemBlock->Instructions[0].get(), tmp2, getItem) ||
            tmp2 == nullptr) {
            return false;
        }
        auto* getItemCall = dynamic_cast<Call*>(getItem);
        if (getItemCall == nullptr) return false;
        bool isGetItem = false;
        if (getItemCall->Method != nullptr) {
            isGetItem = getItemCall->Method->Name() == "get_Item";
        } else {
            const std::string& mn = getItemCall->MethodName;
            isGetItem =
                mn.size() > 2 && mn.substr(mn.rfind("::") + 2) == "get_Item";
        }
        if (!isGetItem) return false;
        // The get_Item arguments: the field load + the switch variable load.
        if (getItemCall->Arguments.size() != 2) return false;
        std::string dictFieldName;
        TS::ITypePtr dictionaryType;
        if (!MatchDictionaryFieldLoad(
                getItemCall->Arguments[0].get(),
                [](const TS::IType& t) { return IsNonGenericHashtable(t); },
                dictFieldName, dictionaryType)) {
            return false;
        }
        auto found = hashtableInitializers.find(dictFieldName);
        if (found == hashtableInitializers.end() || found->second.transformed) {
            return false;
        }
        {
            ILVariable* loaded = nullptr;
            if (!MatchLdLoc(getItemCall->Arguments[1].get(), loaded) ||
                loaded != switchVariable) {
                return false;
            }
        }
        // instructions[1]: stloc switchVariable2(ldloc tmp2)
        ILVariable* switchVariable2 = nullptr;
        ILInstruction* tmp2Load = nullptr;
        if (!MatchStLoc(getItemBlock->Instructions[1].get(), switchVariable2,
                        tmp2Load) ||
            switchVariable2 == nullptr) {
            return false;
        }
        {
            auto* ld = dynamic_cast<LdLoc*>(tmp2Load);
            if (ld == nullptr || ld->Variable.get() != tmp2) return false;
        }
        // instructions[2]: if (comp(ldloc tmp2 == ldnull)) br defaultBlock
        ILInstruction* getItemCondition = nullptr;
        ILInstruction* defaultBlockBranch = nullptr;
        if (!MatchIfInstruction(getItemBlock->Instructions[2].get(),
                                getItemCondition, defaultBlockBranch)) {
            return false;
        }
        auto* getItemComp = dynamic_cast<Comp*>(getItemCondition);
        if (getItemComp == nullptr ||
            getItemComp->Kind != ComparisonKind::Equality ||
            getItemComp->Right == nullptr ||
            !MatchLdNull(getItemComp->Right.get())) {
            return false;
        }
        {
            ILVariable* loaded = nullptr;
            if (!MatchLdLoc(getItemComp->Left.get(), loaded) ||
                loaded != tmp2) {
                return false;
            }
        }
        // The C# `defaultBlockBranch.MatchBranch(out var defaultBlock) ||
        // defaultBlockBranch is Leave`.
        Block* defaultBlock = nullptr;
        BlockContainer* defaultBlockLeave = nullptr;
        if (!MatchBranch(defaultBlockBranch, defaultBlock) &&
            !MatchLeave(defaultBlockBranch, defaultBlockLeave)) {
            return false;
        }
        // instructions[3]: br switchBlock
        Block* switchBlock = nullptr;
        if (!MatchBranch(getItemBlock->Instructions[3].get(), switchBlock)) {
            return false;
        }
        // match third block: the switch over the unboxed get_Item result
        if (switchBlock == nullptr || switchBlock->IncomingEdgeCount != 1 ||
            switchBlock->Instructions.size() != 1) {
            return false;
        }
        auto* switchInst = dynamic_cast<SwitchInstruction*>(
            switchBlock->Instructions[0].get());
        if (switchInst == nullptr) {
            return false;
        }
        auto* ldobj = dynamic_cast<LdObj*>(switchInst->Value.get());
        if (ldobj == nullptr || ldobj->Type == nullptr) {
            return false;
        }
        auto* unbox = dynamic_cast<UnboxAny*>(ldobj->Target.get());
        if (unbox == nullptr || unbox->Type == nullptr ||
            !unbox->Type->Equals(*ldobj->Type)) {
            return false;
        }
        ILVariable* unboxed = nullptr;
        if (!MatchLdLoc(unbox->Argument.get(), unboxed) ||
            unboxed != switchVariable2) {
            return false;
        }
        if (!TS::IsKnownType(*ldobj->Type, TS::KnownTypeCode::Int32)) {
            return false;
        }
        // The fold: the StringToInt argument is tmp's value (moved out of the
        // tmp store before the consumed range is erased); the map comes from
        // the scan-extracted pairs.
        std::unique_ptr<ILInstruction> argument;
        if (switchValue != nullptr) {
            auto* tmpStore = dynamic_cast<StLoc*>(instructions[i].get());
            if (tmpStore != nullptr && tmpStore->Value.get() == switchValue) {
                argument = tmpStore->TakeChild(0);
            }
        }
        if (argument == nullptr && switchValue != nullptr) {
            // The value could not be taken from the store (an unexpected
            // shape); clone it instead (the C# GC reuses the raw reference).
            argument = switchValue->Clone();
        }
        auto stringToInt = std::make_unique<StringToInt>(
            std::move(argument),
            std::vector<std::pair<std::optional<std::string>, int>>{},
            FindType(context.TypeSystem, TS::KnownTypeCode::String));
        for (const auto& entry : found->second.labels) {
            stringToInt->Map.emplace_back(entry.first, entry.second);
        }
        auto newSwitch =
            std::make_unique<SwitchInstruction>(std::move(stringToInt));
        std::vector<std::unique_ptr<SwitchSection>> sections;
        for (auto& section : switchInst->Sections) {
            sections.push_back(std::move(section));
        }
        // The switch contains the null case: the C# `nullCaseBlock != null &&
        // nullCaseBlock != defaultBlock` (the Leave form has no null block).
        if (!nullCaseIsLeave && nullCaseBlock != nullptr &&
            nullCaseBlock != defaultBlock) {
            if (!HashtableArmAddNullSection(
                    sections, found->second.labels,
                    std::make_unique<Branch>(nullCaseBlock))) {
                return false;
            }
        }
        context.StepOnce("MatchLegacySwitchOnStringWithHashtable");
        for (auto& section : sections) {
            newSwitch->Sections.push_back(std::move(section));
        }
        newSwitch->StartILOffset = instructions[i]->StartILOffset;
        newSwitch->EndILOffset = instructions[i]->EndILOffset;
        ReplaceAt(block, i, std::move(newSwitch));
        RemoveRange(block, i + 1, 3);
        found->second.transformed = true;
        return true;
    }
}

// (the legacy-Hashtable section ends here)
// The C# MatchSwitchOnCharBlock: the per-length char-level dispatch. The
// switch's sections map char labels to the case bodies; for length == 1 the
// targets are the bodies themselves (the values are the 1-char strings),
// otherwise each target is a case head comparing the full string (the walk
// collects the (string, body) pairs). Returns false on any unexpected shape.
// Forward declarations (the C# nested-function scoping flattened to
// file-local declarations; the definitions follow their callers).
bool MatchGetChars(ILInstruction* instruction, ILVariable*& switchValueVar,
                   int& index, bool switchOnReadOnlySpanChar);
bool MatchLdLoca(ILInstruction* inst, ILVariable*& variable);

bool MatchSwitchOnCharBlock(Block* block, int length, ILVariable* switchValueVar,
                            std::vector<std::pair<std::string, ILInstruction*>>&
                                results,
                            bool switchOnReadOnlySpanChar,
                            Block* nullCase) {
    results.clear();
    if (block == nullptr || block->IncomingEdgeCount != 1) return false;
    if (block->Instructions.empty()) return false;
    SwitchInstruction* sw = nullptr;
    std::vector<std::pair<Util::LongSet, ILInstruction*>> sections;
    int index = -1;
    if (block->Instructions.size() == 1) {
        sw = dynamic_cast<SwitchInstruction*>(block->Instructions[0].get());
        if (sw == nullptr) return false;
        if (!MatchGetChars(sw->Value.get(), switchValueVar, index,
                           switchOnReadOnlySpanChar)) {
            return false;
        }
        for (auto& section : sw->Sections) {
            sections.emplace_back(section->Labels, section->Body.get());
        }
    } else if (block->Instructions.size() == 2) {
        // stloc charTempVar(get_Chars(ldloc switchValueVar, ...));
        // switch (ldloc charTempVar)
        ILVariable* charTempVar = nullptr;
        ILInstruction* getCharsCall = nullptr;
        if (!MatchStLoc(block->Instructions[0].get(), charTempVar,
                        getCharsCall) ||
            charTempVar == nullptr) {
            return false;
        }
        if (!MatchGetChars(getCharsCall, switchValueVar, index,
                           switchOnReadOnlySpanChar)) {
            return false;
        }
        sw = dynamic_cast<SwitchInstruction*>(block->Instructions[1].get());
        if (sw == nullptr) return false;
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(sw->Value.get(), loaded) || loaded != charTempVar) {
            return false;
        }
        for (auto& section : sw->Sections) {
            sections.emplace_back(section->Labels, section->Body.get());
        }
    } else {
        // The longer shapes need the block analysis (the C# `analysis
        // .AnalyzeBlock(block)`); deferred with that surface.
        return false;
    }
    if (index >= length) return false;
    bool hasDefaultSection = false;
    for (auto& entry : sections) {
        const Util::LongSet& labels = entry.first;
        ILInstruction* body = entry.second;
        if (labels.Count() == 1) {
            long long value = labels.Intervals().front().Start;
            char ch = static_cast<char>(value);
            Block* targetBlock = nullptr;
            if (!MatchBranch(body, targetBlock)) return false;
            if (length == 1) {
                results.emplace_back(std::string(1, ch), body);
                (void)targetBlock;
            } else {
                Block* cursor = targetBlock;
                while (cursor != nullptr) {
                    std::string stringValue;
                    bool emptyStringEqualsNull = false;
                    ILInstruction* bodyOrLeave = nullptr;
                    Block* exit = nullptr;
                    if (!MatchRoslynCaseBlockHead(cursor, switchValueVar,
                                                  bodyOrLeave, exit,
                                                  stringValue,
                                                  emptyStringEqualsNull)) {
                        return false;
                    }
                    if (static_cast<int>(stringValue.size()) != length ||
                        stringValue[static_cast<std::size_t>(index)] != ch) {
                        return false;
                    }
                    results.emplace_back(stringValue, bodyOrLeave);
                    if (exit == nullCase) break;
                    cursor = exit;
                }
            }
        } else if (!hasDefaultSection) {
            hasDefaultSection = true;
        } else {
            return false;
        }
    }
    return !results.empty();
}

// The C# MatchSwitchOnLengthBlock: the length-level dispatch. Returns the
// (length, target) pairs; the default section's target is captured into
// defaultCase. The three accepted shapes: the switch over the get_Length
// call directly, a stloc'd length, or the 2-length fast path (the if over
// the length comparison).
bool MatchSwitchOnLengthBlock(ILVariable*& switchValueVar,
                              Block* switchOnLengthBlock, int startOffset,
                              std::vector<std::pair<Util::LongSet,
                                                    ILInstruction*>>& blocks,
                              ILInstruction*& defaultCase,
                              bool& defaultIsCompilerGenerated,
                              bool switchOnReadOnlySpanChar) {
    blocks.clear();
    defaultCase = nullptr;
    defaultIsCompilerGenerated = false;
    if (switchOnLengthBlock == nullptr) return false;
    const int count =
        static_cast<int>(switchOnLengthBlock->Instructions.size()) - startOffset;
    SwitchInstruction* sw = nullptr;
    ILInstruction* getLengthCall = nullptr;
    ILVariable* lengthVar = nullptr;
    if (count == 1) {
        sw = dynamic_cast<SwitchInstruction*>(
            switchOnLengthBlock->Instructions[static_cast<std::size_t>(
                                                   startOffset)]
                .get());
        if (sw == nullptr) return false;
        getLengthCall = sw->Value.get();
    } else if (count == 2) {
        if (!MatchStLoc(
                switchOnLengthBlock->Instructions[static_cast<std::size_t>(
                                                      startOffset)]
                    .get(),
                lengthVar, getLengthCall) ||
            lengthVar == nullptr) {
            return false;
        }
        sw = dynamic_cast<SwitchInstruction*>(
            switchOnLengthBlock->Instructions[static_cast<std::size_t>(
                                                  startOffset + 1)]
                .get());
        if (sw == nullptr) return false;
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(sw->Value.get(), loaded) || loaded != lengthVar) {
            return false;
        }
    } else if (count == 3) {
        if (!MatchStLoc(
                switchOnLengthBlock->Instructions[static_cast<std::size_t>(
                                                      startOffset)]
                    .get(),
                lengthVar, getLengthCall) ||
            lengthVar == nullptr) {
            return false;
        }
        ILInstruction* cond = nullptr;
        ILInstruction* gotoLength = nullptr;
        if (!MatchIfInstruction(
                switchOnLengthBlock->Instructions[static_cast<std::size_t>(
                                                      startOffset + 1)]
                    .get(),
                cond, gotoLength)) {
            return false;
        }
        Block* target = nullptr;
        if (!MatchBranch(gotoLength, target)) return false;
        Block* gotoElse = nullptr;
        if (!MatchBranch(
                switchOnLengthBlock->Instructions[static_cast<std::size_t>(
                                                      startOffset + 2)]
                    .get(),
                gotoElse)) {
            return false;
        }
        ILInstruction* lhs = nullptr;
        ILInstruction* rhs = nullptr;
        auto* eq = dynamic_cast<Comp*>(cond);
        if (eq != nullptr && eq->Kind == ComparisonKind::Equality) {
            lhs = eq->Left.get();
            rhs = eq->Right.get();
        } else if (eq != nullptr && eq->Kind == ComparisonKind::Inequality) {
            lhs = eq->Left.get();
            rhs = eq->Right.get();
            Block* swap = target;
            target = gotoElse;
            gotoElse = swap;
        } else {
            return false;
        }
        defaultCase = gotoElse;
        ILVariable* loaded = nullptr;
        int length = -1;
        if (!MatchLdLoc(lhs, loaded) || loaded != lengthVar) return false;
        auto* lengthConst = dynamic_cast<LdcI4*>(rhs);
        if (lengthConst == nullptr) return false;
        length = lengthConst->Value;
        blocks.emplace_back(Util::LongSet(Util::LongInterval(length, length + 1)),
                            target);
        blocks.emplace_back(
            Util::LongSet(Util::LongInterval(length, length + 1)).Invert(),
            defaultCase);
        return true;
    } else {
        return false;
    }
    // The switch-over-length shape: the get_Length call must be on the
    // switch-value variable (String) or the span local (gated).
    auto* call = dynamic_cast<Call*>(getLengthCall);
    if (call == nullptr || call->Arguments.size() != 1) return false;
    std::string name;
    if (call->Method != nullptr) {
        name = call->Method->Name();
    } else {
        const std::string& mn = call->MethodName;
        name = mn.size() > 2 ? mn.substr(mn.rfind("::") + 2) : mn;
    }
    if (name != "get_Length") return false;
    TS::ITypePtr declaring;
    if (call->Method != nullptr) {
        declaring = call->Method->DeclaringType();
    } else {
        declaring = call->DeclaringType;
    }
    if (declaring == nullptr) return false;
    if (TS::IsKnownType(*declaring, TS::KnownTypeCode::String)) {
        ILVariable* loaded = nullptr;
        if (!MatchLdLoc(call->Arguments[0].get(), loaded)) return false;
        switchValueVar = loaded;
    } else if (TS::IsKnownType(*declaring, TS::KnownTypeCode::ReadOnlySpanOfT) ||
               TS::IsKnownType(*declaring, TS::KnownTypeCode::SpanOfT)) {
        if (!switchOnReadOnlySpanChar) return false;
        ILVariable* loaded = nullptr;
        if (!MatchLdLoca(call->Arguments[0].get(), loaded)) return false;
        switchValueVar = loaded;
    } else {
        return false;
    }
    if (sw == nullptr) return true;
    for (auto& section : sw->Sections) {
        if (section->HasNullLabel) return false;
        Block* target = nullptr;
        BlockContainer* leave = nullptr;
        if (!MatchBranch(section->Body.get(), target) &&
            !MatchLeave(section->Body.get(), leave)) {
            return false;
        }
        ILInstruction* targetInst = target != nullptr
                                        ? static_cast<ILInstruction*>(target)
                                        : static_cast<ILInstruction*>(leave);
        if (section->Labels.Count() != 1) {
            if (defaultCase == nullptr) defaultCase = targetInst;
            if (defaultCase != targetInst) return false;
        } else {
            blocks.emplace_back(section->Labels, targetInst);
        }
    }
    return true;
}

// The C# `private bool MatchRoslynSwitchOnStringUsingLengthAndChar(Block
// block, int i)`: the Roslyn 66081 length+char dispatch folded into a
// switch over a StringToInt of the switch-value variable.
bool MatchRoslynSwitchOnStringUsingLengthAndCharImpl(Block& block, int i,
                                                     ILTransformContext&
                                                         context) {
    auto& instructions = block.Instructions;
    Block* switchOnLengthBlock = nullptr;
    ILVariable* switchValueVar = nullptr;
    int switchOnLengthBlockStartOffset = 0;
    Block* nullCase = nullptr;
    {
        ILInstruction* condition = nullptr;
        ILInstruction* exitBlockJump = nullptr;
        if (i < 0 || i + 1 >= static_cast<int>(instructions.size())) {
            // The length-block-only shape still needs a valid i.
            if (i < 0 || i >= static_cast<int>(instructions.size())) {
                return false;
            }
            switchOnLengthBlock = &block;
            switchValueVar = nullptr;  // extracted in MatchSwitchOnLengthBlock
            switchOnLengthBlockStartOffset = i;
        } else if (MatchIfInstruction(instructions[i].get(), condition,
                                      exitBlockJump)) {
            // if (comp(ldloc switchValueVar == ldnull)) br nullCase/leave
            auto* comp = dynamic_cast<Comp*>(condition);
            ILVariable* testedVar = nullptr;
            if (comp != nullptr &&
                comp->Kind == ComparisonKind::Equality &&
                comp->Right != nullptr &&
                dynamic_cast<LdNull*>(comp->Right.get()) != nullptr &&
                MatchLdLoc(comp->Left.get(), testedVar) &&
                testedVar != nullptr) {
                switchValueVar = testedVar;
                Block* nextBlock = nullptr;
                if (!MatchBranch(instructions[i + 1].get(), nextBlock)) {
                    return false;
                }
                BlockContainer* nullCaseLeave = nullptr;
                if (!MatchBranch(exitBlockJump, nullCase) &&
                    !MatchLeave(exitBlockJump, nullCaseLeave)) {
                    return false;
                }
                if (nextBlock->IncomingEdgeCount == 1 &&
                    !nextBlock->Instructions.empty()) {
                    ILInstruction* nextCondition = nullptr;
                    ILInstruction* nextExit = nullptr;
                    if (MatchIfInstruction(nextBlock->Instructions[0].get(),
                                           nextCondition, nextExit)) {
                        auto* nextComp = dynamic_cast<Comp*>(nextCondition);
                        ILVariable* nextTested = nullptr;
                        if (nextComp != nullptr &&
                            nextComp->Kind == ComparisonKind::Equality &&
                            nextComp->Right != nullptr &&
                            dynamic_cast<LdNull*>(
                                nextComp->Right.get()) != nullptr &&
                            MatchLdLoc(nextComp->Left.get(), nextTested) &&
                            nextTested == switchValueVar) {
                            // The span shape: the next block re-checks null.
                            Block* switchOnLengthBlock2 = nullptr;
                            if (!MatchBranch(nextBlock->Instructions[1].get(),
                                             switchOnLengthBlock2)) {
                                return false;
                            }
                            nextBlock = switchOnLengthBlock2;
                        }
                    }
                }
                if (nextBlock == nullptr || nextBlock->IncomingEdgeCount != 1) {
                    return false;
                }
                switchOnLengthBlock = nextBlock;
                switchOnLengthBlockStartOffset = 0;
            } else {
                switchOnLengthBlock = &block;
                switchValueVar = nullptr;  // extracted in the length match
                switchOnLengthBlockStartOffset = i;
            }
        } else {
            switchOnLengthBlock = &block;
            switchValueVar = nullptr;  // extracted in the length match
            switchOnLengthBlockStartOffset = i;
        }
    }
    // The length-level dispatch.
    std::vector<std::pair<Util::LongSet, ILInstruction*>> blocksByLength;
    ILInstruction* defaultCase = nullptr;
    bool defaultIsCompilerGenerated = false;
    if (!MatchSwitchOnLengthBlock(
            switchValueVar, switchOnLengthBlock,
            switchOnLengthBlockStartOffset, blocksByLength, defaultCase,
            defaultIsCompilerGenerated,
            context.Settings.SwitchOnReadOnlySpanChar)) {
        return false;
    }
    if (switchValueVar == nullptr) return false;
    // The per-length char walk + the value collection.
    std::vector<std::pair<std::optional<std::string>, ILInstruction*>>
        stringValues;
    for (auto& entry : blocksByLength) {
        if (entry.first.Count() != 1) {
            // Multi-label: only the null-case target is acceptable (the C#
            // `if (b.TargetBlock != nullCase) return false;`).
            Block* target = nullptr;
            if (MatchBranch(entry.second, target) && target != nullCase) {
                return false;
            }
            continue;
        }
        int length = static_cast<int>(entry.first.Intervals().front().Start);
        Block* targetBlock = dynamic_cast<Block*>(entry.second);
        ILInstruction* leave = nullptr;
        if (targetBlock == nullptr) {
            leave = entry.second;
        }
        (void)leave;
        std::vector<std::pair<std::string, ILInstruction*>> mapping;
        if (targetBlock != nullptr &&
            MatchSwitchOnCharBlock(targetBlock, length, switchValueVar,
                                   mapping,
                                   context.Settings.SwitchOnReadOnlySpanChar,
                                   nullCase)) {
            for (auto& item : mapping) {
                bool seen = false;
                for (auto& existing : stringValues) {
                    if (existing.first.has_value() &&
                        *existing.first == item.first) {
                        seen = true;
                        break;
                    }
                }
                if (seen) return false;
                stringValues.emplace_back(item.first, item.second);
            }
            continue;
        }
        // The direct case-head shape (a string comparison at this length).
        std::string stringValue;
        bool emptyStringEqualsNull = false;
        ILInstruction* bodyOrLeave = nullptr;
        Block* exit = nullptr;
        if (targetBlock != nullptr &&
            MatchRoslynCaseBlockHead(targetBlock, switchValueVar, bodyOrLeave,
                                     exit, stringValue,
                                     emptyStringEqualsNull)) {
            bool seen = false;
            for (auto& existing : stringValues) {
                if (existing.first.has_value() &&
                    *existing.first == stringValue) {
                    seen = true;
                    break;
                }
            }
            if (seen) return false;
            stringValues.emplace_back(stringValue, bodyOrLeave);
            continue;
        }
        if (length == 0 && targetBlock != nullptr) {
            // The empty-string case: the C# `stringValues.Add(("", target))`.
            stringValues.emplace_back(std::string(), targetBlock);
            continue;
        }
        return false;
    }
    // The null case: the C# `stringValues.Add((null, nullBlock))` via
    // IsNullCheckInDefaultBlock, or the explicit null case from the head.
    bool hasNullValue = false;
    for (auto& entry : stringValues) {
        if (!entry.first.has_value()) hasNullValue = true;
    }
    if (!hasNullValue && nullCase != nullptr && nullCase != defaultCase) {
        stringValues.emplace_back(std::nullopt, nullCase);
    }
    context.StepOnce("MatchRoslynSwitchOnStringUsingLengthAndChar");
    // The emit: the switch over the StringToInt of the switch-value load.
    auto stringToInt = std::make_unique<StringToInt>(
        std::make_unique<LdLoc>(
            ILVariablePtr(std::shared_ptr<ILVariable>(), switchValueVar)),
        std::vector<std::pair<std::optional<std::string>, int>>{},
        FindType(context.TypeSystem, TS::KnownTypeCode::String));
    const std::size_t valueCount = stringValues.size();
    for (std::size_t idx = 0; idx < valueCount; idx++) {
        stringToInt->Map.emplace_back(stringValues[idx].first,
                                      static_cast<int>(idx));
    }
    auto newSwitch =
        std::make_unique<SwitchInstruction>(std::move(stringToInt));
    for (std::size_t idx = 0; idx < valueCount; idx++) {
        ILInstruction* body = stringValues[idx].second;
        std::unique_ptr<ILInstruction> sectionBody;
        Block* bodyTarget = nullptr;
        BlockContainer* bodyLeave = nullptr;
        if (body != nullptr && MatchBranch(body, bodyTarget)) {
            sectionBody = std::make_unique<Branch>(bodyTarget);
        } else if (body != nullptr && MatchLeave(body, bodyLeave)) {
            sectionBody = body->Clone();
        } else if (body != nullptr) {
            sectionBody = body->Clone();
        } else {
            return false;
        }
        auto section = std::make_unique<SwitchSection>(
            Util::LongSet(Util::LongInterval(
                static_cast<long long>(idx),
                static_cast<long long>(idx) + 1)));
        section->SetBody(std::move(sectionBody));
        newSwitch->Sections.push_back(std::move(section));
    }
    // The default section: the inverted complement of the value labels.
    auto defaultSection = std::make_unique<SwitchSection>(
        Util::LongSet(Util::LongInterval(
                           0, static_cast<long long>(valueCount)))
            .Invert());
    defaultSection->SetBody(defaultCase != nullptr
                                ? defaultCase->Clone()
                                : nullptr);
    newSwitch->Sections.push_back(std::move(defaultSection));
    newSwitch->StartILOffset = instructions[i]->StartILOffset;
    newSwitch->EndILOffset = instructions[i]->EndILOffset;
    ReplaceAt(block, i, std::move(newSwitch));
    // The C# `instructions.RemoveRange(i + 1, instructions.Count - (i+1))`:
    // the whole rest of the shape is consumed.
    RemoveRange(block, i + 1,
                static_cast<int>(instructions.size()) - (i + 1));
    return true;
}

// (the length+char section ends here)

// ---- The Roslyn length+char arm (MatchRoslynSwitchOnStringUsingLengthAndChar
// + MatchSwitchOnLengthBlock + MatchSwitchOnCharBlock + MatchGetChars) ------

// The C# MatchGetChars local function: the char-index load at the switch's
// head -- the String form `call String::get_Chars(ldloc switchValueVar,
// ldc.i4 index)` or the span form `ldobj UInt16(call
// ReadOnlySpan::get_Item(ldloca switchValueVar, ldc.i4 index))` (gated on
// SwitchOnReadOnlySpanChar). Returns the matched variable via switchValueVar.

// The port's MatchLdLoca (the C# `inst.MatchLdLoca(out var v)`).
bool MatchLdLoca(ILInstruction* inst, ILVariable*& variable) {
    variable = nullptr;
    auto* ldloca = dynamic_cast<LdLoca*>(inst);
    if (ldloca == nullptr) return false;
    variable = ldloca->Variable.get();
    return variable != nullptr;
}

bool MatchGetChars(ILInstruction* instruction, ILVariable*& switchValueVar,
                   int& index, bool switchOnReadOnlySpanChar) {
    index = -1;
    if (instruction == nullptr) return false;
    if (auto* ldobj = dynamic_cast<LdObj*>(instruction)) {
        if (!switchOnReadOnlySpanChar) return false;
        if (ldobj->Type == nullptr ||
            !TS::IsKnownType(*ldobj->Type, TS::KnownTypeCode::UInt16)) {
            return false;
        }
        auto* call = dynamic_cast<Call*>(ldobj->Target.get());
        if (call == nullptr || call->Arguments.size() != 2) return false;
        const std::string& mn = call->MethodName;
        const bool isSpanGetItem =
            mn.size() > 2 && mn.substr(mn.rfind("::") + 2) == "get_Item";
        if (!isSpanGetItem) return false;
        ILVariable* loaded = nullptr;
        if (!MatchLdLoca(call->Arguments[0].get(), loaded)) return false;
        auto* idx = dynamic_cast<LdcI4*>(call->Arguments[1].get());
        if (idx == nullptr || idx->Value < 0) return false;
        index = idx->Value;
        switchValueVar = loaded;
        return true;
    }
    auto* call = dynamic_cast<Call*>(instruction);
    if (call == nullptr || call->Arguments.size() != 2) return false;
    std::string name;
    if (call->Method != nullptr) {
        name = call->Method->Name();
    } else {
        const std::string& mn = call->MethodName;
        name = mn.size() > 2 ? mn.substr(mn.rfind("::") + 2) : mn;
    }
    if (name != "get_Chars") return false;
    ILVariable* loaded = nullptr;
    if (!MatchLdLoc(call->Arguments[0].get(), loaded)) return false;
    auto* idx = dynamic_cast<LdcI4*>(call->Arguments[1].get());
    if (idx == nullptr || idx->Value < 0) return false;
    index = idx->Value;
    switchValueVar = loaded;
    return true;
}




bool SwitchOnStringProbes::MatchLegacySwitchOnStringWithDict(
    Block& block, int& i, ILTransformContext& context) {
    return MatchLegacySwitchOnStringWithDictImpl(block, i, context);
}

} // namespace ILSpy::Decompiler::IL
