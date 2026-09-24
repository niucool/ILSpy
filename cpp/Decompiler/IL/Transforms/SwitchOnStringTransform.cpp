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
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/ControlFlow/SwitchDetection.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/LongSet.hpp"

#include <cassert>
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


// The C# MatchIfInstruction over the port's IfInstruction node.
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

void SwitchOnStringTransform::Run(ILFunction& function,
                                  ILTransformContext& context) {
    if (!context.Settings.SwitchStatementOnString) return;
    auto* body = dynamic_cast<BlockContainer*>(function.Body.get());
    if (body == nullptr) return;
    // The C# hashtable-initializer scan (ScanHashtableInitializerBlocks) and
    // the legacy Dictionary/Hashtable arms are deferred with those shapes'
    // surfaces; the Roslyn arms drive this slice.
    std::vector<Block*> blocks;
    CollectBlocks(body, blocks);
    for (Block* block : blocks) {
        if (block->IncomingEdgeCount == 0) continue;
        bool changed = false;
        for (int i = static_cast<int>(block->Instructions.size()) - 1; i >= 0; i--) {
            if (SimplifyCSharp1CascadingIfStatements(*block, i, context)) {
                changed = true;
                continue;
            }
            if (SimplifyCascadingIfStatements(*block, i, context)) {
                changed = true;
                continue;
            }
            if (MatchRoslynSwitchOnString(*block, i, context)) {
                changed = true;
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
            if (!spanIdentity("AsSpan") || asSpan->Arguments.size() != 1) {
                return false;
            }
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
    if (target->Instructions.size() != 2 || target->IncomingEdgeCount != 1) {
        return false;
    }
    ILInstruction* nullCondition = nullptr;
    ILInstruction* exitBranch = nullptr;
    if (!MatchIfInstruction(target->Instructions[0].get(), nullCondition,
                            exitBranch)) {
        return false;
    }
    ILInstruction* arg = nullptr;
    if (!MatchCompEqualsNull(nullCondition, arg)) return false;
    ILVariable* nullCheckVar = nullptr;
    if (!MatchLdLoc(arg, nullCheckVar) || nullCheckVar != switchValueVar) {
        return false;
    }
    Block* lengthCheckBlock = nullptr;
    if (!MatchBranch(target->Instructions[1].get(), lengthCheckBlock)) {
        return false;
    }
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
        if (MatchBranch(exitBranch, exitTarget) && exitTarget != exit2Target) {
            return false;
        }
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
    if (!MatchBranch(exitOrDefaultBlock->Instructions[1].get(), elseBlock)) {
        return false;
    }
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
    if (!(static_cast<int>(instructionsPtr->size()) > switchBlockInstructionsOffset + 1)) {
        return false;
    }
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
            std::move(argument), nullptr);
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
        auto stringToInt =
            std::make_unique<StringToInt>(std::move(argument), nullptr);
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
        if (value.has_value() && !uniqueValues.insert(*value).second) {
            return false;
        }
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
    ILInstruction* newSwitchValue = nullptr;
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
    if (static_cast<int>(values.size()) < 3) {
        return false;
    }
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
    } else if (call->MethodName != "System.String::IsInterned") {
        return false;
    }
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
    if (!(temp->Kind == VariableKind::StackSlot && temp->LoadCount == 2)) {
        return false;
    }
    ILVariable* switchValueVar = nullptr;
    ILInstruction* switchValue = nullptr;
    StLoc* switchValueOwner = nullptr;
    if (!MatchStLoc(instructions[i - 1].get(), switchValueVar, switchValue)) {
        return false;
    }
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
        if (!MatchLdLoc(internedArg, loaded) || loaded != switchValueVar) {
            return false;
        }
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
    if (static_cast<int>(values.size()) != switchValueVarCopy->LoadCount) {
        return false;
    }
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
// type arguments.
bool IsStringToIntDictionary(const TS::IType& type) {
    if (!(type.Namespace() == "System.Collections.Generic" &&
          type.Name() == "Dictionary")) {
        return false;
    }
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
    if (call == nullptr || call->Method == nullptr && call->MethodName.empty()) {
        return false;
    }
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

} // namespace

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
    if (newObj == nullptr || !newObj->IsNewObj || newObj->Method == nullptr &&
        newObj->MethodName.empty()) {
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

} // namespace ILSpy::Decompiler::IL
