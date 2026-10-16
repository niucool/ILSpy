// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// The Run half of the DelegateConstruction port (the matcher lives in
// DelegateConstruction.cpp): the C# `void IILTransform.Run(ILFunction,
// ILTransformContext)` (DelegateConstruction.cs) walks the function, gates
// each match (the anonymous-method name shape, the local-function
// rejection, the delegate-target validation, the self-reference guard),
// deep-decodes the target body through the context's DelegateBodyResolver
// hook, and embeds the decoded lambda as an ILFunction (Kind Delegate) in
// place of the NewObj, then runs the nested pipeline and the
// ReplaceDelegateTargetVisitor over it.

#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Transforms/CombineExitsTransform.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
#include "Decompiler/Metadata/CodeMappingInfo.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

// The pre-order walk (the C# `foreach (var inst in function.Descendants)`
// + the per-match gates): the recursion target for the nested-lambda step
// (the C# `((IILTransform)this).Run(function, nestedContext)`); the shared
// activeMethods stack rides the parameter.
void RunWalk(ILFunction& function, ILTransformContext& context,
             std::vector<std::uint32_t>& activeMethods);

namespace {

// The C# `method.HasGeneratedName()`: a compiler-generated name starts with
// '<' (NamePrefixes.GeneratedNamePrefix). The C# IsAnonymousMethod ORs this
// with Name.Contains("$"), IsCompilerGenerated, IsPotentialClosure and
// ContainsAnonymousType -- the attribute and type-tree arms need the
// metadata/classification surfaces and are deferred; the name arms cover the
// Roslyn lambda shape ("<Do>b__0_0").
bool IsAnonymousMethodName(const std::string& name) {
    return !name.empty() &&
           (name[0] == '<' || name.find('$') != std::string::npos);
}

// The C# `static bool ValidateDelegateTarget(ILInstruction inst)`: the
// delegate receiver must be a null load, a single-definition local load, or
// a load-object chain rooted at a local (the display-class `this`).
bool ValidateDelegateTarget(const ILInstruction* inst) {
    if (inst == nullptr) return false;
    switch (inst->Op) {
        case OpCode::LdNull:
            return true;
        case OpCode::LdLoc: {
            auto* ldloc = static_cast<const LdLoc*>(inst);
            return ldloc->Variable != nullptr &&
                   ldloc->Variable->IsSingleDefinition();
        }
        case OpCode::LdObj: {
            // The C# walks the LdObj/LdFlda chain down to a LdLoc (its
            // TODO-quality validation: the field chain is not checked for
            // reassignment).
            const ILInstruction* target = inst;
            while (target != nullptr &&
                   (target->Op == OpCode::LdObj ||
                    target->Op == OpCode::LdFlda)) {
                target = target->GetChild(0);
            }
            if (target == nullptr || target->Op != OpCode::LdLoc) return false;
            auto* ldloc = static_cast<const LdLoc*>(target);
            return ldloc->Variable != nullptr &&
                   ldloc->Variable->IsSingleDefinition();
        }
        default:
            return false;
    }
}

// The C# `internal static bool IsThis(this ILVariable v)`: the implicit
// receiver parameter (the port's negative-index convention).
bool IsThisVariable(const ILVariable* v) {
    return v != nullptr && v->Kind == VariableKind::Parameter && v->Index < 0;
}

// The ReplaceDelegateTargetVisitor port: replace the nested function's
// `ldloc this` (and the async `ldobj(ldloca this)`) loads with the delegate
// target expression, and record the captured variable on the nested function
// (the C# CapturedVariables.Add). `target` is cloned per site.
void ReplaceDelegateTarget(ILFunction& nested, const ILInstruction* target) {
    // The C# this-variable: `function.Variables.SingleOrDefault(IsThis)`.
    ILVariable* thisVariable = nullptr;
    for (auto& v : nested.Variables) {
        if (v && IsThisVariable(v.get())) {
            thisVariable = v.get();
            break;
        }
    }
    if (thisVariable == nullptr || target == nullptr) return;
    struct Frame {
        ILInstruction* node;
        int nextChild;
    };
    std::vector<Frame> stack;
    stack.push_back({&nested, 0});
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.nextChild >= frame.node->ChildCount()) {
            stack.pop_back();
            continue;
        }
        ILInstruction* node = frame.node;
        const int index = frame.nextChild;
        frame.nextChild++;
        ILInstruction* child = node->GetChild(index);
        if (child == nullptr) continue;
        if (child->Op == OpCode::LdLoc) {
            auto* ldloc = static_cast<LdLoc*>(child);
            if (ldloc->Variable.get() == thisVariable) {
                node->TakeChild(index);
                node->SetChild(index, target->Clone());
                continue;  // the clone is fresh (no this load inside)
            }
        } else if (child->Op == OpCode::LdObj) {
            auto* ldobj = static_cast<LdObj*>(child);
            // The C# `inst.Target.MatchLdLoca(thisVariable)`: the target of
            // the ldobj is the this address.
            ILInstruction* inner = ldobj->Target.get();
            if (inner != nullptr && inner->Op == OpCode::LdLoca) {
                auto* ldloca = static_cast<LdLoca*>(inner);
                if (ldloca->Variable.get() == thisVariable) {
                    node->TakeChild(index);
                    node->SetChild(index, target->Clone());
                    continue;
                }
            }
        }
        stack.push_back({child, 0});
    }
    // The C# `function.CapturedVariables.Add(v)` -- the target variable
    // (the LdLoc target's variable, or the LdObj chain's root local).
    const ILVariable* captured = nullptr;
    if (target->Op == OpCode::LdLoc) {
        auto* ldloc = static_cast<const LdLoc*>(target);
        captured = ldloc->Variable.get();
    } else if (target->Op == OpCode::LdObj) {
        const ILInstruction* inner = target;
        while (inner != nullptr &&
               (inner->Op == OpCode::LdObj || inner->Op == OpCode::LdFlda))
            inner = inner->GetChild(0);
        if (inner != nullptr && inner->Op == OpCode::LdLoc)
            captured = static_cast<const LdLoc*>(inner)->Variable.get();
    }
    if (captured != nullptr
        && std::find(nested.CapturedVariables.begin(),
                     nested.CapturedVariables.end(), captured)
               == nested.CapturedVariables.end())
        nested.CapturedVariables.push_back(
            const_cast<ILVariable*>(captured));
}

// The nested pipeline the C# builds with
// `GetILTransforms().TakeWhile(t => !(t is DelegateConstruction))
// .Concat(GetTransforms())` -- the head of the main pipeline up to (not
// including) this transform, plus the CombineExits tail. The port's pipeline
// is the inline RunGetILTransforms driver (no transform list to slice), so
// the head is the driver's prefix up to this transform's position (after
// CopyPropagation, before AssignVariableNames): the block-transform phase,
// HighLevelLoopTransform, FixRemainingIncrements, CopyPropagation.
void RunNestedPipeline(ILFunction& function, ILTransformContext& context) {
    RunILTransformsThroughBlockTransforms(function, context);
    HighLevelLoopTransform::Run(function, context);
    FixRemainingIncrements().Run(function, context);
    CopyPropagation().Run(function, context);
    CombineExitsTransform().Run(function, context);
}

// The C# `ILFunction TransformDelegateConstruction(...)` -- the gates plus
// the embed; returns the embedded function or null (no transform).
ILFunction* TransformDelegateConstruction(
    ILInstruction* value, const DelegateConstructionMatch& match,
    ILTransformContext& context,
    std::vector<std::uint32_t>& activeMethods) {
    const TypeSystem::IMethod* targetMethod = match.method;
    if (targetMethod == nullptr) return nullptr;
    if (!IsAnonymousMethodName(targetMethod->Name())) return nullptr;
    const std::uint32_t token = targetMethod->MetadataToken();
    if (token == 0) return nullptr;  // the C# MetadataToken.IsNil gate
    if (std::find(activeMethods.begin(), activeMethods.end(), token) !=
        activeMethods.end()) {
        // The C# self-reference abort (the stack-overflow guard): the port
        // adds the warning text only in the Step log -- the C# appends to
        // function.Warnings, a surface the port has not landed yet.
        context.StepOnce(
            " Found self-referencing delegate construction. Abort "
            "transformation to avoid stack overflow.");
        return nullptr;
    }
    // The C# IsLocalFunctionMethod(targetMethod, context) consults the
    // context's PEFile; the port's probe needs the Metadata module, so the
    // probe runs only when one is wired (the minimal construction skips it
    // -- the deep-decode itself is governed by the resolver hook).
    if (context.Metadata != nullptr &&
        Metadata::IsLocalFunctionMethod(*context.Metadata, token)) {
        return nullptr;
    }
    if (!ValidateDelegateTarget(match.target)) return nullptr;
    if (!targetMethod->HasBody()) return nullptr;
    // The C# GenericContextFromTypeArguments(targetMethod.Substitution) arm:
    // a generic-instantiated target requires the generic-context IL reader
    // arm; the port's resolver hook decodes uninstantiated bodies only, so a
    // non-identity substitution rejects (deferred with the generic-context
    // reader surface).
    if (targetMethod->Substitution() != nullptr &&
        targetMethod->Substitution() !=
            &TypeSystem::TypeParameterSubstitution::Identity()) {
        return nullptr;
    }
    if (context.DelegateBodyResolver == nullptr) return nullptr;
    auto nested = context.DelegateBodyResolver(
        token, context.Metadata != nullptr
                   ? context.Metadata->GetMethodRVA(token)
                   : 0);
    if (nested == nullptr) return nullptr;
    nested->Kind = ILFunctionKind::Delegate;
    nested->DelegateType = match.delegateType;
    // The variable rename (the C# contextPrefix pass): every non-parameter
    // variable of the nested body takes the target method's name as a prefix
    // so sibling scopes stay distinct.
    const std::string prefix = targetMethod->Name();
    for (auto& v : nested->Variables) {
        if (v && v->Kind != VariableKind::Parameter)
            v->Name = prefix + v->Name;
    }
    // Embed: the nested function takes the matched instruction's slot.
    ILInstruction* parent = value->Parent;
    if (parent == nullptr) return nullptr;
    const int index = value->ChildIndex;
    parent->TakeChild(index);
    parent->SetChild(index, std::move(nested));
    auto* nestedPtr = dynamic_cast<ILFunction*>(parent->GetChild(index));
    if (nestedPtr == nullptr) return nullptr;
    nestedPtr->CheckInvariant(ILPhase::Normal);
    // The nested pipeline: the head of the main list up to this transform,
    // then CombineExits (the C# TakeWhile + GetTransforms() concat).
    ILTransformContext nestedContext = context;
    RunNestedPipeline(*nestedPtr, nestedContext);
    context.StepOnce("DelegateConstruction (ReplaceDelegateTargetVisitor)");
    ReplaceDelegateTarget(*nestedPtr, match.target);
    // Handle nested lambdas (the C# recursive Run over the embedded body);
    // the pushed token makes the self-reference abort fire for lambdas that
    // ldftn themselves.
    activeMethods.push_back(token);
    RunWalk(*nestedPtr, nestedContext, activeMethods);
    activeMethods.pop_back();
    return nestedPtr;
}

// The C# `v.StoreInstructions.SingleOrDefault() is StLoc store` probe: walk
// the function for the unique StLoc whose value is the matched delegate
// construction and whose variable is `v`. Null when no such store exists.
StLoc* FindStoreOf(ILInstruction* root, const ILVariable* v,
                   const ILInstruction* value) {
    if (root == nullptr) return nullptr;
    StLoc* found = nullptr;
    struct Frame {
        ILInstruction* node;
        int nextChild;
    };
    std::vector<Frame> stack;
    stack.push_back({root, 0});
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.nextChild >= frame.node->ChildCount()) {
            stack.pop_back();
            continue;
        }
        ILInstruction* node = frame.node;
        const int index = frame.nextChild;
        frame.nextChild++;
        ILInstruction* child = node->GetChild(index);
        if (child == nullptr) continue;
        if (child->Op == OpCode::StLoc) {
            auto* stloc = static_cast<StLoc*>(child);
            if (stloc->Variable.get() == v && stloc->Value.get() == value) {
                found = stloc;
                break;
            }
        }
        stack.push_back({child, 0});
    }
    return found;
}

} // namespace

void DelegateConstruction::Run(ILFunction& function,
                               ILTransformContext& context) {
    if (!context.Settings.AnonymousMethods) return;
    std::vector<std::uint32_t> activeMethods;
    RunWalk(function, context, activeMethods);
}

void RunWalk(ILFunction& function, ILTransformContext& context,
             std::vector<std::uint32_t>& activeMethods) {
    // The C# walks `function.Descendants` and replaces nodes mid-walk (the
    // embedded lambda takes the NewObj's slot and the walk descends into it,
    // where the already-processed shapes no longer match). The port walks
    // pre-order with a removal-aware index step (the TDU SROA precedent).
    struct Frame {
        ILInstruction* node;
        int nextChild;
    };
    std::vector<Frame> stack;
    stack.push_back({&function, 0});
    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.nextChild > frame.node->ChildCount()) {
            stack.pop_back();
            continue;
        }
        if (frame.nextChild == frame.node->ChildCount()) {
            ILInstruction* node = frame.node;
            stack.pop_back();
            DelegateConstructionMatch match;
            if (!DelegateConstruction::MatchDelegateConstruction(node, match))
                continue;
            ILFunction* embedded = TransformDelegateConstruction(
                node, match, context, activeMethods);
            if (embedded == nullptr) continue;
            // The C# variable flip over the delegate target's variable (the
            // `target is IInstructionWithVariableOperand` arm): a Local
            // target becomes a DisplayClassLocal; a single-definition store
            // of the NewObj marks the capture scope.
            if (match.target != nullptr && match.target->Op == OpCode::LdLoc) {
                auto* ldloc = static_cast<LdLoc*>(match.target);
                ILVariable* v = ldloc->Variable.get();
                if (v != nullptr) {
                    if (v->Kind == VariableKind::Local)
                        v->Kind = VariableKind::DisplayClassLocal;
                    if (v->IsSingleDefinition() && v->StoreCount == 1) {
                        // Locate the unique store of the matched instruction
                        // (the C# `v.StoreInstructions.SingleOrDefault()`).
                        StLoc* storeSite =
                            FindStoreOf(&function, v, node);
                        if (storeSite != nullptr)
                            v->CaptureScope =
                                BlockContainer::FindClosestContainer(storeSite);
                    }
                }
            }
            // The walk continues into the embedded subtree on the next
            // iterations (the C# lazy-continue behavior); re-push it.
            stack.push_back({embedded, 0});
            continue;
        }
        ILInstruction* child = frame.node->GetChild(frame.nextChild);
        frame.nextChild++;
        if (child != nullptr) stack.push_back({child, 0});
    }
}

} // namespace ILSpy::Decompiler::IL
