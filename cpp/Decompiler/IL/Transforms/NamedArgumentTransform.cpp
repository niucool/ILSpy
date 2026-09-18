// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
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

#include "Decompiler/IL/Transforms/NamedArgumentTransform.hpp"

#include "Decompiler/NRExtensions.hpp"
#include "Decompiler/IL/BlockKind.hpp"
#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/TypeSystem/ByReferenceTypeReference.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/TypeSystem/VarArgInstanceMethod.hpp"

#include <cassert>
#include <memory>

namespace ILSpy::Decompiler::IL {

namespace {

// The ILFunction root that owns `inst` (the port's FunctionOf helper).
ILFunction* FunctionOf(ILInstruction* inst) {
    for (ILInstruction* p = inst; p != nullptr; p = p->Parent) {
        if (p->IsRoot()) return static_cast<ILFunction*>(p);
    }
    return nullptr;
}

// The C# `context.TypeSystem.FindType(arg.ResultType)`: the evaluation-stack type
// of the promoted argument mapped to an IType. Unknown -> UnknownType; Ref -> a
// ByReferenceType over UnknownType; everything else the StackType's ToKnownTypeCode
// lookup. The port has no compilation in the transform context, so the known type
// is a standalone KnownType -- the same code/name an ICompilation.FindType returns.
TypeSystem::ITypePtr FindTypeForStackType(StackType stackType) {
    using namespace TypeSystem;
    if (stackType == StackType::Unknown) return UnknownType();
    if (stackType == StackType::Ref)
        return std::make_shared<ByReferenceType>(UnknownType());
    KnownTypeCode code = ToKnownTypeCode(stackType);
    if (code == KnownTypeCode::None) return UnknownType();
    return std::make_shared<KnownType>(code);
}

} // namespace

FindResult NamedArgumentTransform::CanIntroduceNamedArgument(
    Call* call, ILInstruction* child, ILVariable* v,
    ILInstruction* expressionBeingMoved) {
    assert(child->Parent == call);
    if (call->IsInstanceCall && child->ChildIndex == 0)
        return FindResult::StopResult();  // cannot move the this pointer
    if (call->Method ? call->Method->IsOperator() : call->IsOperator)
        return FindResult::StopResult();  // no named args for operators
    if (call->Method && call->Method->IsAccessor())
        return FindResult::StopResult();  // no named args for accessors
    if (call->Method
        && dynamic_cast<const TypeSystem::VarArgInstanceMethod*>(call->Method.get()))
        return FindResult::StopResult();  // CallBuilder does not support varargs
    if (call->Method && call->Method->IsConstructor()) {
        TypeSystem::ITypePtr declaringType = call->Method->DeclaringType();
        if (declaringType
            && (declaringType->Kind() == TypeSystem::TypeKind::Delegate
                || IsAnonymousType(declaringType.get())))
            return FindResult::StopResult();
    }
    if (call->Method) {
        for (const TypeSystem::IParameter* p : call->Method->Parameters()) {
            if (p == nullptr || p->Name().empty())
                return FindResult::StopResult();  // cannot use named arguments
        }
    }
    for (int i = child->ChildIndex; i < static_cast<int>(call->Arguments.size()); ++i) {
        FindResult r = FindLoadInNext(call->Arguments[static_cast<std::size_t>(i)].get(), v,
                                      expressionBeingMoved, InliningOptions::None);
        if (r.type == FindResultType::Found) {
            return FindResult::NamedArgumentResult(
                r.loadInst, call->Arguments[static_cast<std::size_t>(i)].get());
        }
    }
    return FindResult::StopResult();
}

FindResult NamedArgumentTransform::CanExtendNamedArgument(
    Block* block, ILVariable* v, ILInstruction* expressionBeingMoved) {
    assert(block->Kind == BlockKind::CallWithNamedArgs);
    if (block->Instructions.empty()) return FindResult::StopResult();
    auto* firstStloc = dynamic_cast<StLoc*>(block->Instructions[0].get());
    if (!firstStloc) return FindResult::StopResult();
    ILInstruction* firstArg = firstStloc->Value.get();
    FindResult r = FindLoadInNext(firstArg, v, expressionBeingMoved,
                                  InliningOptions::IntroduceNamedArguments);
    if (r.type == FindResultType::Found || r.type == FindResultType::NamedArgument)
        return r;  // OK, inline into the first instruction of the block
    auto* call = dynamic_cast<Call*>(block->FinalInstruction.get());
    if (!call) return FindResult::StopResult();
    if (call->IsInstanceCall) {
        if (r.type == FindResultType::Stop)
            return FindResult::StopResult();  // cannot move after instructions[0]
        // Instructions[0] is the this argument; the target may be in
        // Instructions[1].
        if (block->Instructions.size() > 1) {
            r = FindLoadInNext(block->Instructions[1].get(), v, expressionBeingMoved,
                               InliningOptions::IntroduceNamedArguments);
            if (r.type == FindResultType::Found
                || r.type == FindResultType::NamedArgument)
                return r;
        }
    }
    for (auto& arg : call->Arguments) {
        if (arg && arg->Op == OpCode::LdLoc
            && static_cast<LdLoc*>(arg.get())->Variable.get() == v) {
            return FindResult::NamedArgumentResult(arg.get(), arg.get());
        }
    }
    return FindResult::StopResult();
}

void NamedArgumentTransform::IntroduceNamedArgument(ILInstruction* arg,
                                                    ILTransformContext& context) {
    auto* call = dynamic_cast<Call*>(arg->Parent);
    assert(call && "IntroduceNamedArgument: parent is not a call");
    ILFunction* function = FunctionOf(call);
    TypeSystem::ITypePtr type = FindTypeForStackType(arg->ResultType());
    ILVariablePtr v = function
                          ? function->RegisterVariable(VariableKind::NamedArgument,
                                                       std::move(type))
                          : std::make_shared<ILVariable>(VariableKind::NamedArgument,
                                                         std::move(type));
    context.StepOnce("Introduce named argument");

    Block* namedArgBlock = dynamic_cast<Block*>(call->Parent);
    if (!namedArgBlock || namedArgBlock->Kind != BlockKind::CallWithNamedArgs) {
        // Wrap the call in a fresh CallWithNamedArgs block.
        ILInstruction* callParent = call->Parent;
        int callIndex = call->ChildIndex;
        auto callOwned = callParent->TakeChild(callIndex);
        auto block = std::make_unique<Block>();
        block->Kind = BlockKind::CallWithNamedArgs;
        block->SetFinal(std::move(callOwned));
        callParent->SetChild(callIndex, std::move(block));
        namedArgBlock = static_cast<Block*>(callParent->GetChild(callIndex));
        if (call->IsInstanceCall) {
            TypeSystem::ITypePtr thisVarType =
                call->ConstrainedTo ? call->ConstrainedTo
                                    : (call->Method ? call->Method->DeclaringType()
                                                    : nullptr);
            if (!thisVarType) thisVarType = TypeSystem::UnknownType();
            if (ExpectedTypeForThisPointer(thisVarType.get(), call->ConstrainedTo.get())
                == StackType::Ref) {
                thisVarType = std::make_shared<TypeSystem::ByReferenceType>(
                    std::move(thisVarType));
            }
            ILVariablePtr thisArgVar =
                function ? function->RegisterVariable(VariableKind::NamedArgument,
                                                      std::move(thisVarType), "this_arg")
                         : std::make_shared<ILVariable>(VariableKind::NamedArgument,
                                                        std::move(thisVarType));
            auto arg0 = call->TakeChild(0);
            namedArgBlock->Add(std::make_unique<StLoc>(thisArgVar, std::move(arg0)));
            call->SetChild(0, std::make_unique<LdLoc>(thisArgVar));
        }
    }

    int argIndex = arg->ChildIndex;
    auto argOwned = call->TakeChild(argIndex);
    auto newInst = std::make_unique<StLoc>(v, std::move(argOwned));
    namedArgBlock->InsertAt(call->IsInstanceCall ? 1 : 0, std::move(newInst));
    call->SetChild(argIndex, std::make_unique<LdLoc>(v));
    namedArgBlock->RenumberChildren();
}

void NamedArgumentTransform::Run(Block& block, int pos,
                                 StatementTransformContext& context) {
    if (!context.Base.Settings.NamedArguments) return;
    // The C# adds ILInlining.OptionsForBlock(block, pos, context); this port does
    // not model the aggressive/ordering heuristics that helper contributes, so the
    // named-argument flag is the only option.
    InlineOneIfPossible(&block, pos, InliningOptions::IntroduceNamedArguments,
                        context.Base);
}

} // namespace ILSpy::Decompiler::IL
