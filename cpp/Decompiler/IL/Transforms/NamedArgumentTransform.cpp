// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

// Port of ICSharpCode.Decompiler/IL/Transforms/NamedArgumentTransform.cs (the
// C# per-statement transform that converts an argument's inlined load into a
// named argument). See the header for the C# piece-by-piece mapping.

#include "Decompiler/IL/Transforms/NamedArgumentTransform.hpp"

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/SimpleCompilation.hpp"

#include <cassert>
#include <utility>
#include <vector>

namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `bool VariableCanBeUsedForInlining` (ILInlining.cs): the port's
// counter-based equivalent (the LoadInstructions lists are not tracked).
bool NamedArgVariableCanBeUsedForInlining(ILVariable* v)
{
    if (v == nullptr || v->Kind == VariableKind::PinnedLocal)
        return false;
    return v->StoreCount == 1 && v->LoadCount + v->AddressCount == 1;
}

} // namespace

// The C# `internal static FindResult CanIntroduceNamedArgument(CallInstruction
// call, ILInstruction child, ILVariable v, ILInstruction
// expressionBeingMoved)` (NamedArgumentTransform.cs lines 31-50): the call
// gates, then the scan of the later argument slots.
FindResult NamedArgumentCanIntroduce(Call* call, ILInstruction* child,
                                     ILVariable* v,
                                     ILInstruction* expressionBeingMoved)
{
    assert(child->Parent == call);
    // This port's reader-decoded Call nodes carry no resolved IMethod (only
    // MethodName); the call gates need the IMethod surface (operators,
    // accessors, constructors, parameter names) so unresolved calls stop here
    // (the C# reader always resolves the IMethod).
    const TS::IMethod* method = call->Method.get();
    if (method == nullptr)
        return {FindResultType::Stop};
    if (call->IsInstanceCall && child->ChildIndex == 0)
        return {FindResultType::Stop}; // cannot use named arg to move
                                       // expressionBeingMoved before this
                                       // pointer
    if (method->IsOperator() || method->IsAccessor())
        return {FindResultType::Stop}; // cannot use named arg for operators or
                                       // accessors
    if (method->IsConstructor()) {
        TS::ITypePtr declaringType = method->DeclaringType();
        if (declaringType != nullptr
            && declaringType->Kind() == TS::TypeKind::Delegate)
            return {FindResultType::Stop};
    }
    for (const auto& parameter : method->Parameters()) {
        if (parameter->Name().empty())
            return {FindResultType::Stop}; // cannot use named arguments
    }
    for (int i = child->ChildIndex; i < static_cast<int>(call->Arguments.size());
         i++) {
        ILInstruction* argument =
            call->Arguments[static_cast<std::size_t>(i)].get();
        FindResult r = FindLoadInNext(argument, v, expressionBeingMoved,
                                      InliningOptions::None);
        if (r.type == FindResultType::Found) {
            return {FindResultType::NamedArgument, r.loadInst, argument};
        }
    }
    return {FindResultType::Stop};
}

// The C# `internal static FindResult CanExtendNamedArgument(Block block,
// ILVariable v, ILInstruction expressionBeingMoved)` (lines 87-112).
FindResult NamedArgumentCanExtend(Block* block, ILVariable* v,
                                  ILInstruction* expressionBeingMoved)
{
    assert(block->Kind == BlockKind::CallWithNamedArgs);
    auto* firstStLoc = dynamic_cast<StLoc*>(block->Instructions[0].get());
    if (firstStLoc == nullptr)
        return {FindResultType::Stop};
    ILInstruction* firstArg = firstStLoc->Value.get();
    FindResult r = FindLoadInNext(firstArg, v, expressionBeingMoved,
                                  InliningOptions::IntroduceNamedArguments);
    if (r.type == FindResultType::Found
        || r.type == FindResultType::NamedArgument)
        return r; // OK, inline into first instruction of block
    auto* call = dynamic_cast<Call*>(block->FinalInstruction.get());
    if (call != nullptr && call->IsInstanceCall) {
        // For instance calls, block.Instructions[0] is the argument for the
        // 'this' pointer. We can only insert at position 1.
        if (r.type == FindResultType::Stop) {
            // error: can't move expressionBeingMoved after
            // block.Instructions[0]
            return {FindResultType::Stop};
        }
        // Because we always ensure block.Instructions[0] is the 'this'
        // argument, it's possible that the place we actually need to inline
        // into is within block.Instructions[1]:
        if (block->Instructions.size() > 1) {
            r = FindLoadInNext(block->Instructions[1].get(), v,
                               expressionBeingMoved,
                               InliningOptions::IntroduceNamedArguments);
            if (r.type == FindResultType::Found
                || r.type == FindResultType::NamedArgument)
                return r; // OK, inline into block.Instructions[1]
        }
    }
    if (call != nullptr) {
        for (const auto& arg : call->Arguments) {
            if (arg->Op == OpCode::LdLoc
                && static_cast<LdLoc*>(arg.get())->Variable.get() == v) {
                return {FindResultType::NamedArgument, arg.get(), arg.get()};
            }
        }
    }
    return {FindResultType::Stop};
}

// The C# `internal static void IntroduceNamedArgument(ILInstruction arg,
// ILTransformContext context)` (lines 116-146). The variable's type comes from
// the FindType(StackType) mapping over the decompilation compilation (the C#
// `context.TypeSystem.FindType(arg.ResultType)` -- the port's ILTransformContext
// carries the type system as a raw `TS::ICompilation*` the pipeline and the
// tests set).
void NamedArgumentIntroduce(ILInstruction* arg, ILTransformContext& context)
{
    auto* call = dynamic_cast<Call*>(arg->Parent);
    assert(call != nullptr);
    assert(context.TypeSystem != nullptr
           && "IntroduceNamedArgument requires the context's type system");
    // The C# `var type = context.TypeSystem.FindType(arg.ResultType)` -- the
    // port's StackType-to-IType mapping (the FindType(StackType, Sign)
    // ReflectionHelper overload).
    TS::ITypePtr type = std::const_pointer_cast<TS::IType>(
        std::static_pointer_cast<const TS::IType>(
            const_cast<TS::IType&>(TS::FindType(*context.TypeSystem,
                                                arg->ResultType(), TS::Sign::None))
                .shared_from_this()));
    // The C# `context.Function` -- the port walks the parent chain to the root
    // ILFunction (ILFunction is the tree root).
    ILFunction* function = nullptr;
    for (const ILInstruction* p = arg->Parent; p != nullptr; p = p->Parent) {
        if (dynamic_cast<const ILFunction*>(p) != nullptr) {
            function = const_cast<ILFunction*>(static_cast<const ILFunction*>(p));
            break;
        }
    }
    assert(function != nullptr);
    ILVariablePtr v =
        function->RegisterVariable(VariableKind::NamedArgument, type);
    context.StepOnce("Introduce named argument");
    Block* namedArgBlock = nullptr;
    if (call->Parent == nullptr
        || dynamic_cast<Block*>(call->Parent) == nullptr
        || dynamic_cast<Block*>(call->Parent)->Kind != BlockKind::CallWithNamedArgs) {
        // create namedArgBlock: `call.ReplaceWith(namedArgBlock);
        // namedArgBlock.FinalInstruction = call;` -- the port's ReplaceWith
        // destroys the old node, so take the call out first and re-plant it as
        // the block's final.
        ILInstruction* callParent = call->Parent;
        const int callIndex = call->ChildIndex;
        std::unique_ptr<ILInstruction> ownedCall = callParent->TakeChild(callIndex);
        auto ownedBlock = std::make_unique<Block>();
        ownedBlock->Kind = BlockKind::CallWithNamedArgs;
        namedArgBlock = ownedBlock.get();
        namedArgBlock->SetFinal(std::move(ownedCall));
        callParent->SetChild(callIndex, std::move(ownedBlock));
        if (call->IsInstanceCall) {
            // The this-pointer slot: the C# registers `this_arg` with the
            // expected receiver type; the port stores the receiver into the
            // NamedArgument variable and loads it back (the ByReferenceType
            // ExpectedTypeForThisPointer arm is deferred with that surface).
            ILInstruction* receiver = call->Arguments[0].get();
            TS::ITypePtr thisType = std::const_pointer_cast<TS::IType>(
                std::static_pointer_cast<const TS::IType>(
                    const_cast<TS::IType&>(TS::FindType(
                        *context.TypeSystem, receiver->ResultType(), TS::Sign::None))
                        .shared_from_this()));
            ILVariablePtr thisArgVar = function->RegisterVariable(
                VariableKind::NamedArgument, thisType, "this_arg");
            namedArgBlock->Instructions.push_back(std::make_unique<StLoc>(
                thisArgVar, call->TakeChild(0)));
            call->SetChild(0, std::make_unique<LdLoc>(thisArgVar));
        }
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        }
    else {
        namedArgBlock = dynamic_cast<Block*>(call->Parent);
    }
    const int argIndex = arg->ChildIndex;
    assert(call->Arguments[static_cast<std::size_t>(argIndex)].get() == arg);
    auto newInst = std::make_unique<StLoc>(v, call->TakeChild(argIndex));
    const std::size_t insertPos = call->IsInstanceCall ? 1 : 0;
    namedArgBlock->Instructions.insert(
        namedArgBlock->Instructions.begin()
            + static_cast<std::ptrdiff_t>(insertPos),
        std::move(newInst));
    call->SetChild(argIndex, std::make_unique<LdLoc>(v));
}

// The C# `public void Run(Block block, int pos, StatementTransformContext
// context)` (lines 137-145): the settings gate plus the OptionsForBlock
// (IntroduceNamedArguments; the C# also ORs the aggressive arms of
// OptionsForBlock -- the IsCatchWhenBlock/IsInConstructorInitializer/
// PreferExpressionsOverStatements probes are deferred with those surfaces --
// and AllowChangingOrderOfEvaluationForExceptions when
// UseRefLocalsForAccurateOrderOfEvaluation is off, the port default) and the
// InlineOneIfPossible call.
void NamedArgumentTransform::Run(Block& block, int pos,
                                 StatementTransformContext& context)
{
    if (!context.Base.Settings.NamedArguments)
        return;
    InlineOneIfPossible(&block, pos, context.Base,
                        InliningOptions::IntroduceNamedArguments);
}

} // namespace ILSpy::Decompiler::IL