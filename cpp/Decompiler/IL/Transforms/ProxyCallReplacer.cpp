// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

#include "Decompiler/IL/Transforms/ProxyCallReplacer.hpp"

#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/ControlFlow/ControlFlowSimplification.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/Transforms/SplitVariables.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // AliasMethod + IsCompilerGeneratedOrIsInCompilerGeneratedClass

namespace ILSpy::Decompiler::IL {

namespace {

// File-local MatchLdLoc (the C# Instructions.cs line 8497 out-form): a bare
// LdLoc reports its variable. Kept file-local rather than added to
// PatternMatching.hpp because a shared out-form would be ambiguous with the
// shared match-against-v overload for an ILVariable* lvalue (the D66/D94
// precedent -- the NullPropagationTransform file-local helper documents the
// same trap).
bool MatchLdLoc(ILInstruction* inst, ILVariable*& v) {
    v = nullptr;
    if (inst == nullptr || inst->Op != OpCode::LdLoc) return false;
    v = static_cast<LdLoc*>(inst)->Variable.get();
    return v != nullptr;
}

// The C# `static bool IsDefinedInCurrentOrOuterClass(IMethod method,
// ITypeDefinition declaringTypeDefinition)` (ProxyCallReplacer.cs lines
// 116-124): the method's declaring type is the given type or one of its
// nesting ancestors.
bool IsDefinedInCurrentOrOuterClass(
    const TypeSystem::IMethod* method,
    const TypeSystem::ITypeDefinition* declaringTypeDefinition) {
    while (declaringTypeDefinition != nullptr) {
        if (method->DeclaringTypeDefinition() == declaringTypeDefinition)
            return true;
        declaringTypeDefinition =
            declaringTypeDefinition->DeclaringTypeDefinition();
    }
    return false;
}

// The C# `function.Descendants.OfType<CallInstruction>()` (the pre-order
// walk; the port's per-transform convention). The replacement swaps a Call
// for another Call over the same child slots, so a collect-then-process pass
// is equivalent to the C#'s lazy iteration.
void CollectCalls(ILInstruction* node, std::vector<Call*>& calls) {
    if (auto* call = dynamic_cast<Call*>(node)) {
        calls.push_back(call);
    }
    for (int i = 0; i < node->ChildCount(); i++) {
        if (ILInstruction* child = node->GetChild(i))
            CollectCalls(child, calls);
    }
}

} // namespace

void ProxyCallReplacer::Run(ILFunction& function, ILTransformContext& context) {
    // The C# `if (!context.Settings.AsyncAwait) return;` -- the setting
    // defaults on, so a null CSharpSettings (the port's bare-CLI convention)
    // passes the gate like the C# default-constructed settings.
    if (context.CSharpSettings != nullptr &&
        !context.CSharpSettings->AsyncAwait()) {
        return;
    }
    std::vector<Call*> calls;
    CollectCalls(&function, calls);
    for (Call* inst : calls) {
        Run(*inst, function, context);
    }
}

void ProxyCallReplacer::Run(Call& inst, ILFunction& function,
                            ILTransformContext& context) {
    const TypeSystem::IMethod* method = inst.Method.get();
    if (method == nullptr || method->IsStatic())
        return;
    // The C# `inst.Method.MetadataToken.IsNil ||
    // inst.Method.MetadataToken.Kind != HandleKind.MethodDefinition` -- the
    // port's raw-token convention (the high byte is the metadata table;
    // 0x06 is MethodDef).
    const std::uint32_t token = method->MetadataToken();
    if (token == 0 || (token >> 24) != 0x06)
        return;
    if (function.Method == nullptr)
        return;
    if (!IsDefinedInCurrentOrOuterClass(method,
                                        function.Method->DeclaringTypeDefinition()))
        return;
    if (!TypeSystem::IsCompilerGeneratedOrIsInCompilerGeneratedClass(method))
        return;
    if (!method->HasBody())
        return;
    // The C# decodes with `context.CreateILReader().ReadIL(handle, body,
    // genericContext, ILFunctionKind.TopLevelFunction, ...)`: the port's
    // DelegateBodyResolver hook is the CreateILReader bridge (the facade
    // wires it to ReadIL over the module file). The callee's GENERIC CONTEXT
    // -- the C#'s `new GenericContext(inst.Method)` substituting the
    // call-site's type arguments for a generic proxy -- has no port surface
    // yet; the hook decodes uninstantiated bodies (the same limitation the
    // delegate-construction path records for its generic targets).
    if (context.DelegateBodyResolver == nullptr)
        return;
    auto proxyFunction = context.DelegateBodyResolver(
        token, context.Metadata != nullptr
                   ? context.Metadata->GetMethodRVA(token)
                   : 0);
    if (proxyFunction == nullptr)
        return;
    // The C# `proxyFunction.RunTransforms(
    // CSharp.CSharpDecompiler.EarlyILTransforms(), transformContext)` -- the
    // early list (ControlFlowSimplification + SplitVariables + ILInlining)
    // over a child context (the C# `new ILTransformContext(context,
    // proxyFunction)`; the port copies, the function riding the Run
    // parameter).
    ILTransformContext nestedContext = context;
    ControlFlowSimplification().Run(*proxyFunction, nestedContext);
    SplitVariables().Run(*proxyFunction, nestedContext);
    ILInlining().Run(*proxyFunction, nestedContext);
    // The C# body-shape checks: a single-block container whose instructions
    // are the pass-through call (a leave of it, or the call plus a nop
    // leave).
    auto* blockContainer =
        dynamic_cast<BlockContainer*>(proxyFunction->Body.get());
    if (blockContainer == nullptr)
        return;
    if (blockContainer->Blocks.size() != 1)
        return;
    Block* block = blockContainer->Blocks[0].get();
    Call* call = nullptr;
    ILInstruction* returnValue = nullptr;
    switch (block->Instructions.size()) {
        case 1:
            // leave IL_0000 (call Test(ldloc this, ldloc A_1))
            if (!MatchLeave(block->Instructions[0].get(), blockContainer,
                            returnValue))
                return;
            call = dynamic_cast<Call*>(returnValue);
            break;
        case 2:
            // call Test(ldloc this, ldloc A_1)
            // leave IL_0000(nop)
            call = dynamic_cast<Call*>(block->Instructions[0].get());
            if (!MatchLeave(block->Instructions[1].get(), blockContainer,
                            returnValue))
                return;
            if (!MatchNop(returnValue))
                return;
            break;
        default:
            return;
    }
    if (call == nullptr || call->Method == nullptr ||
        call->Method->IsConstructor()) {
        return;
    }
    if (call->Method->IsStatic() ||
        call->Method->Parameters().size() != method->Parameters().size()) {
        return;
    }
    // check if original arguments are only correct ldloc calls
    for (std::size_t i = 0; i < call->Arguments.size(); i++) {
        ILInstruction* originalArg = call->Arguments[i].get();
        ILVariable* var = nullptr;
        if (!MatchLdLoc(originalArg, var) ||
            var->Kind != VariableKind::Parameter ||
            var->Index != static_cast<int>(i) - 1) {
            return;
        }
    }
    context.StepOnce("ProxyCallReplacer");
    // Apply the wrapper call's substitution to the actual method call
    // (`new Call(call.Method.Specialize(inst.Method.Substitution))`): the
    // Specialize result is module-/registry-owned (the MetadataMethod
    // keep-alive registry; the test stubs return this), so the AliasMethod
    // borrow keeps the new Call's handle valid.
    const TypeSystem::IMethod* specialized =
        call->Method->Specialize(method->Substitution());
    auto newInst = std::make_unique<Call>(
        TypeSystem::AliasMethod(specialized));
    // copy flags
    newInst->ConstrainedTo = call->ConstrainedTo;
    newInst->ILStackWasEmpty = inst.ILStackWasEmpty;
    newInst->IsTail = call->IsTail && inst.IsTail;
    // copy IL ranges
    newInst->AddILRange(inst);
    // `newInst.Arguments.ReplaceList(inst.Arguments)`: the call-site
    // arguments move into the new call.
    newInst->Arguments = std::move(inst.Arguments);
    inst.ReplaceWith(std::move(newInst));
}

} // namespace ILSpy::Decompiler::IL
