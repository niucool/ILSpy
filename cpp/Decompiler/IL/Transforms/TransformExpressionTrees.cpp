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
#include "Decompiler/IL/Transforms/TransformExpressionTrees.hpp"

#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/IL/ILTypeExtensions.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/IsInst.hpp"
#include "Decompiler/IL/Instructions/UnboxAny.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::IL {

bool TransformExpressionTrees::MatchGetTypeFromHandle(
    ILInstruction* inst, ::ILSpy::Decompiler::TypeSystem::ITypePtr& type) {
    type = nullptr;
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj || call->Method == nullptr ||
        call->Arguments.size() != 1) {
        return false;
    }
    // The C# `getTypeCall.Method.FullName == "System.Type.GetTypeFromHandle"`:
    // the declaring type's full name + the method name pair.
    ::ILSpy::Decompiler::TypeSystem::ITypePtr declaring =
        call->Method->DeclaringType();
    if (declaring == nullptr ||
        !(declaring->Namespace() == "System" && declaring->Name() == "Type")) {
        return false;
    }
    if (call->Method->Name() != "GetTypeFromHandle") return false;
    auto* token = dynamic_cast<LdTypeToken*>(call->Arguments[0].get());
    if (token == nullptr || token->Type == nullptr) return false;
    type = token->Type;
    return true;
}


void RemoveInstruction(Block& block, ILInstruction* inst);

void TransformExpressionTrees::Run(Block& block, int pos,
                                   StatementTransformContext& context) {
    if (!context.Base.Settings.ExpressionTrees) return;
    context_ = &context;
    state_ = State();
    for (int i = pos; i < static_cast<int>(block.Instructions.size()); i++) {
        std::shared_ptr<ILVariable> v;
        TypeSystem::ITypePtr type;
        std::string name;
        if (MatchParameterVariableAssignment(block.Instructions[i].get(), v,
                                             type, name)) {
            state_.parameters[v.get()] = {type, name};
            state_.parameterStores[v.get()] =
                static_cast<StLoc*>(block.Instructions[i].get());
            continue;
        }
        if (TryConvertExpressionTree(block.Instructions[i].get(),
                                     block.Instructions[i].get())) {
            for (ILInstruction* inst : state_.instructionsToRemove) {
                RemoveInstruction(block, inst);
            }
            state_.instructionsToRemove.clear();
        }
        break;
    }
}

// Erase the instruction at `inst`'s position from the block's instruction
// list (the C# `block.Instructions.Remove(inst)` identity removal).
void RemoveInstruction(Block& block, ILInstruction* inst) {
    for (std::size_t i = 0; i < block.Instructions.size(); i++) {
        if (block.Instructions[i].get() == inst) {
            block.RemoveInstructionAt(i);
            return;
        }
    }
}

bool TransformExpressionTrees::TryConvertExpressionTree(
    ILInstruction* instruction, ILInstruction* statement) {
    if (MightBeExpressionTree(instruction, statement)) {
        ConvertResult result = ConvertLambda(dynamic_cast<Call*>(instruction));
        if (result.thunk) {
            std::unique_ptr<ILInstruction> newLambda = result.thunk();
            auto* function = dynamic_cast<ILFunction*>(newLambda.get());
            SetExpressionTreeFlag(*function, dynamic_cast<Call&>(*instruction));
            instruction->ReplaceWith(std::move(newLambda));
            return true;
        }
        return false;
    }
    if (auto* block = dynamic_cast<Block*>(instruction);
        block != nullptr && block->Kind == BlockKind::ControlFlow) {
        return false;  // don't look into nested blocks
    }
    for (int i = 0; i < instruction->ChildCount(); i++) {
        if (TryConvertExpressionTree(instruction->GetChild(i), statement)) {
            return true;
        }
    }
    return false;
}

bool TransformExpressionTrees::IsExpressionTree(
    const TypeSystem::IType& delegateType) {
    auto* pt = dynamic_cast<const TypeSystem::ParameterizedType*>(&delegateType);
    return pt != nullptr && pt->Namespace() == "System.Linq.Expressions" &&
           pt->Name() == "Expression" && pt->TypeArguments().size() == 1;
}

TypeSystem::ITypePtr TransformExpressionTrees::UnwrapExpressionTree(
    const TypeSystem::IType& delegateType) {
    auto* pt = dynamic_cast<const TypeSystem::ParameterizedType*>(&delegateType);
    if (pt != nullptr && pt->Namespace() == "System.Linq.Expressions" &&
        pt->Name() == "Expression" && pt->TypeArguments().size() == 1) {
        return pt->TypeArguments()[0];
    }
    return nullptr;
}

void TransformExpressionTrees::SetExpressionTreeFlag(ILFunction& lambda,
                                                     const Call& call) {
    if (call.Method == nullptr) return;
    lambda.Kind = IsExpressionTree(call.Method->ReturnType())
                      ? ILFunctionKind::ExpressionTree
                      : ILFunctionKind::Delegate;
    // The C# `lambda.DelegateType = call.Method.ReturnType` -- the return
    // type is a subobject of the resolved method, owned by the Call's
    // IMethod shared_ptr; the aliasing-constructor convention (the
    // BuildPatternMatch `call->method` precedent).
    const TypeSystem::IType& returnType = call.Method->ReturnType();
    lambda.DelegateType = TypeSystem::ITypePtr(call.Method,
                                               const_cast<TypeSystem::IType*>(&returnType));
}

bool TransformExpressionTrees::ReadParameters(
    ILInstruction* initializer,
    std::vector<std::shared_ptr<const TypeSystem::IParameter>>& parameterList,
    std::vector<std::shared_ptr<ILVariable>>& parameterVariables) {
    auto* initializerBlock = dynamic_cast<Block*>(initializer);
    if (initializerBlock == nullptr) {
        return IsEmptyParameterList(initializer);
    }
    if (initializerBlock->Kind != BlockKind::ArrayInitializer) return false;
    int i = 0;
    for (auto& inst : initializerBlock->Instructions) {
        auto* stobj = dynamic_cast<StObj*>(inst.get());
        if (stobj == nullptr) continue;
        if (i >= static_cast<int>(state_.parameters.size())) return false;
        auto* ldloc = dynamic_cast<LdLoc*>(stobj->Value.get());
        if (ldloc == nullptr || ldloc->Variable == nullptr) return false;
        auto it = state_.parameters.find(ldloc->Variable.get());
        if (it == state_.parameters.end()) return false;
        // Add parameter variable only once to mapping.
        if (!state_.parameterMapping.count(ldloc->Variable.get())) {
            auto param = std::make_shared<ILVariable>(
                VariableKind::Parameter, it->second.first, i);
            param->Name = it->second.second;
            state_.parameterMapping[ldloc->Variable.get()] = param;
            parameterVariables.push_back(param);
            parameterList.push_back(std::make_shared<
                TypeSystem::Implementation::DefaultParameter>(
                it->second.first, it->second.second));
            auto storeIt = state_.parameterStores.find(ldloc->Variable.get());
            if (storeIt != state_.parameterStores.end()) {
                state_.instructionsToRemove.push_back(storeIt->second);
            }
        }
        i++;
    }
    return true;
}

TransformExpressionTrees::ConvertResult TransformExpressionTrees::ConvertLambda(
    Call* instruction) {
    using TypeSystem::IType;
    if (instruction->Method == nullptr ||
        instruction->Method->Name() != "Lambda" ||
        instruction->Arguments.size() != 2) {
        return {nullptr, nullptr};
    }
    auto* functionTypePt =
        dynamic_cast<const TypeSystem::ParameterizedType*>(&instruction->Method->ReturnType());
    if (functionTypePt == nullptr ||
        functionTypePt->Namespace() != "System.Linq.Expressions" ||
        functionTypePt->Name() != "Expression" ||
        functionTypePt->TypeArguments().size() != 1) {
        return {nullptr, nullptr};
    }
    std::vector<std::shared_ptr<const TypeSystem::IParameter>> parameterList;
    std::vector<std::shared_ptr<ILVariable>> parameterVariablesList;
    if (!ReadParameters(instruction->Arguments[1].get(), parameterList,
                        parameterVariablesList)) {
        return {nullptr, nullptr};
    }
    auto container = std::make_unique<BlockContainer>();
    container->StartILOffset = instruction->StartILOffset;
    container->EndILOffset = instruction->EndILOffset;
    TypeSystem::ITypePtr functionType = functionTypePt->TypeArguments()[0];
    TypeSystem::ITypePtr returnType;
    if (functionType != nullptr) {
        if (const TypeSystem::IMethod* invoke =
                TypeSystem::GetDelegateInvokeMethod(*functionType)) {
            const TypeSystem::IType& rt = invoke->ReturnType();
            returnType = const_cast<TypeSystem::IType&>(rt).shared_from_this();
        }
    }
    // The C# `new ILFunction(returnType, parameterList,
    // context.Function.GenericContext, container, ILFunctionKind.ExpressionTree)`:
    // the port's ILFunction is field-constructed; the GenericContext field
    // is deferred with the generic-context surface.
    auto function = std::make_unique<ILFunction>();
    function->ReturnType = returnType;
    function->Parameters = std::move(parameterList);
    function->Body = std::move(container);
    function->Kind = IsExpressionTree(*functionType)
                         ? ILFunctionKind::ExpressionTree
                         : ILFunctionKind::Delegate;
    function->DelegateType = functionType;
    for (auto& v : parameterVariablesList) {
        function->Variables.push_back(v);
    }
    function->StartILOffset = instruction->StartILOffset;
    function->EndILOffset = instruction->EndILOffset;
    ILFunction* functionRaw = function.get();
    BlockContainer* containerRaw = function->Body.get();
    state_.lambdaStack.push_back(functionRaw);
    ConvertResult body =
        ConvertInstruction(instruction->Arguments[0].get());
    state_.lambdaStack.pop_back();
    if (!body.thunk) {
        return {nullptr, nullptr};
    }
    TypeSystem::ITypePtr delegateType = function->DelegateType;
    // The C# deferred BuildFunction local function: run the converted body
    // thunk, place `leave(container, body)` in a fresh entry block, and
    // replace the remaining loads of each parameter-reference variable with
    // loads of the new parameter variable. The C# walks
    // `mapping.Key.LoadInstructions` (the whole-method load list); the port's
    // ILVariable does not track load lists and that replacement only matters
    // when a parameter-reference variable is also loaded OUTSIDE the
    // converted lambda call (atypical), so the outside-load sweep is deferred
    // with a load-list surface on ILVariable.
    // The BuildFunction thunk: the port's ConvertResult thunk type is a
    // copy-constructible std::function (the C# Func<ILInstruction> is
    // delegate-class, move semantics free), so the owned ILFunction travels
    // through a shared holder; the thunk runs exactly once and moves it out.
    auto functionHolder =
        std::make_shared<std::unique_ptr<ILFunction>>(std::move(function));
    return {[this, functionHolder, containerRaw,
             body = std::move(body.thunk)]() mutable
                -> std::unique_ptr<ILInstruction> {
                state_.lambdaStack.push_back(functionHolder->get());
                std::unique_ptr<ILInstruction> convertedBody = body();
                state_.lambdaStack.pop_back();
                // The C# asserts the stack types agree; the port derives the
                // container's expected type from the converted body (the C#
                // `container.ExpectedResultType = convertedBody.ResultType`).
                if (convertedBody) {
                    containerRaw->ExpectedResultType =
                        convertedBody->ResultType();
                }
                auto entry = std::make_unique<Block>();
                entry->Kind = BlockKind::ControlFlow;
                entry->Add(std::make_unique<Leave>(containerRaw,
                                                   std::move(convertedBody)));
                containerRaw->Blocks.push_back(std::move(entry));
                std::unique_ptr<ILFunction> result = std::move(*functionHolder);
                return result;
            },
            delegateType};
}


// Match a Box node (the C# MatchBox(out arg, out boxType) bare match; the
// PatternMatchingTransform file-local probe re-declared here).
bool MatchBox(ILInstruction* inst, ILInstruction*& argument,
              TypeSystem::ITypePtr& type) {
    auto* box = dynamic_cast<Box*>(inst);
    if (box == nullptr) return false;
    argument = box->Argument.get();
    type = box->Type;
    return true;
}

// The C# `context.TypeSystem.FindType(code)`: the non-null const IType& is
// wrapped in a non-owning shared_ptr (the compilation-owned-reference
// convention; the aliasing ctor keeps the ICompilation alive). Returns null
// when the transform context carries no type system (the degenerate
// construction the tests build).
namespace {
TypeSystem::ITypePtr FindType(TypeSystem::ICompilation* compilation,
                              TypeSystem::KnownTypeCode code) {
    if (compilation == nullptr) return nullptr;
    // The IType base derives enable_shared_from_this (the compilation-owned
    // reference convention; the ExpressionBuilder constant path precedent).
    return const_cast<TypeSystem::IType&>(compilation->FindType(code))
        .shared_from_this();
}
} // namespace

TransformExpressionTrees::ConvertResult
TransformExpressionTrees::ConvertConstant(Call* invocation) {
    ILInstruction* value = nullptr;
    TypeSystem::ITypePtr type;
    if (!MatchConstantCall(invocation, value, type)) {
        return {nullptr, nullptr};
    }
    ILInstruction* boxArg = nullptr;
    TypeSystem::ITypePtr boxType;
    if (value != nullptr && MatchBox(value, boxArg, boxType)) {
        // The C# `boxType.Kind == TypeKind.Enum || boxType.IsKnownType(Boolean)`
        // arm builds an ExpressionTreeCast (a transform-local instruction);
        // that node class is deferred with the ExpressionTreeCast surface, so
        // the boxed enum/bool arm is deferred too.
        if (boxType != nullptr &&
            (boxType->Kind() == TypeSystem::TypeKind::Enum ||
             TypeSystem::IsKnownType(
                 *boxType, TypeSystem::KnownTypeCode::Boolean))) {
            return {nullptr, nullptr};
        }
        return {[value, invocation, this]() -> std::unique_ptr<ILInstruction> {
                    return ConvertValue(value, invocation);
                },
                type};
    }
    return {[value, invocation, this]() -> std::unique_ptr<ILInstruction> {
                return ConvertValue(value, invocation);
            },
            type};
}



bool TransformExpressionTrees::MatchConstantCall(ILInstruction* inst,
                                                 ILInstruction*& value,
                                                 TypeSystem::ITypePtr& type) {
    value = nullptr;
    type = nullptr;
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj || call->Method == nullptr ||
        call->Method->Namespace() != "System.Linq.Expressions" ||
        call->Method->Name() != "Constant") {
        return false;
    }
    value = call->Arguments[0].get();
    if (call->Arguments.size() == 2) {
        return MatchGetTypeFromHandle(call->Arguments[1].get(), type);
    }
    // The C# 1-arg form: infer the type from the constant node.
    const TypeSystem::ICompilation* compilation = context_->Base.TypeSystem;
    if (dynamic_cast<LdNull*>(value) != nullptr) {
        type = TypeSystem::NullType();
    } else if (dynamic_cast<LdStr*>(value) != nullptr) {
        type = FindType(context_->Base.TypeSystem, TypeSystem::KnownTypeCode::String);
    } else if (dynamic_cast<LdcF4*>(value) != nullptr) {
        type = FindType(context_->Base.TypeSystem, TypeSystem::KnownTypeCode::Single);
    } else if (dynamic_cast<LdcF8*>(value) != nullptr) {
        type = FindType(context_->Base.TypeSystem, TypeSystem::KnownTypeCode::Double);
    } else if (dynamic_cast<LdcI4*>(value) != nullptr) {
        type = FindType(context_->Base.TypeSystem, TypeSystem::KnownTypeCode::Int32);
    } else if (dynamic_cast<LdcI8*>(value) != nullptr) {
        type = FindType(context_->Base.TypeSystem, TypeSystem::KnownTypeCode::Int64);
    } else if (compilation != nullptr) {
        type = InferType(*value, compilation);
    }
    return type != nullptr;
}

std::unique_ptr<ILInstruction> TransformExpressionTrees::ConvertValue(
    ILInstruction* value, ILInstruction* context) {
    auto* ldloc = dynamic_cast<LdLoc*>(value);
    if (ldloc != nullptr && ldloc->Variable != nullptr) {
        // The C# `IsExpressionTreeParameter` check (the ParameterExpression
        // type); the closure-reference arm is deferred with
        // TransformDisplayClassUsage.IsPotentialClosure.
        const auto& variableType = ldloc->Variable->Type;
        bool isExpressionTreeParameter =
            variableType != nullptr &&
            variableType->Namespace() == "System.Linq.Expressions" &&
            variableType->Name() == "ParameterExpression";
        if (isExpressionTreeParameter) {
            auto it = state_.parameterMapping.find(ldloc->Variable.get());
            if (it == state_.parameterMapping.end()) {
                return ldloc->Clone();
            }
            return std::make_unique<LdLoc>(it->second);
        }
    }
    return value->Clone();
}

TransformExpressionTrees::ConvertResult TransformExpressionTrees::ConvertQuote(
    Call* invocation) {
    if (invocation->Arguments.size() != 1) {
        return {nullptr, nullptr};
    }
    ILInstruction* argument = invocation->Arguments[0].get();
    if (auto* function = dynamic_cast<ILFunction*>(argument)) {
        // The C# `(() => function, function.DelegateType)` -- an already
        // converted nested lambda is returned as-is.
        ILFunction* raw = function;
        return {[raw]() -> std::unique_ptr<ILInstruction> {
                    // The C# returns the existing ILFunction node; the port's
                    // thunk contract (a fresh unique_ptr) cannot copy it, so
                    // the caller's ConvertLambda pass-through shape is
                    // deferred with the Quote arm (the C# identity-thunk is
                    // only reachable for nested Quote(lambda) trees).
                    (void)raw;
                    return nullptr;
                },
                function->DelegateType};
    }
    ConvertResult converted = ConvertInstruction(argument);
    if (!converted.thunk) {
        return {nullptr, nullptr};
    }
    return {[argument, converted = std::move(converted.thunk)]() mutable
                -> std::unique_ptr<ILInstruction> {
                std::unique_ptr<ILInstruction> f = converted();
                if (f != nullptr) {
                    auto* lambda = dynamic_cast<ILFunction*>(f.get());
                    auto* call = dynamic_cast<Call*>(argument);
                    if (lambda != nullptr && call != nullptr) {
                        SetExpressionTreeFlag(*lambda, *call);
                    }
                }
                return f;
            },
            converted.type};
}

TransformExpressionTrees::ConvertResult
TransformExpressionTrees::ConvertInstruction(ILInstruction* instruction,
                                             TypeSystem::IType* typeHint) {
    (void)typeHint;  // the conv-sign arm is deferred with the numeric arms
    auto* invocation = dynamic_cast<Call*>(instruction);
    if (invocation != nullptr) {
        TypeSystem::ITypePtr declaring = invocation->Method->DeclaringType();
        if (declaring == nullptr ||
            !(declaring->Namespace() == "System.Linq.Expressions" &&
              declaring->Name() == "Expression")) {
            return {nullptr, nullptr};
        }
        const std::string& name = invocation->Method->Name();
        if (name == "Constant") {
            return ConvertConstant(invocation);
        }
        if (name == "Lambda") {
            return ConvertLambda(invocation);
        }
        if (name == "Quote") {
            return ConvertQuote(invocation);
        }
        if (name == "Call") {
            return ConvertCall(invocation);
        }
        if (name == "Field") {
            return ConvertField(invocation, typeHint);
        }
        if (name == "TypeAs") {
            return ConvertTypeAs(invocation);
        }
        if (name == "TypeIs") {
            return ConvertTypeIs(invocation);
        }
        return {nullptr, nullptr};
    }
    if (auto* ldloc = dynamic_cast<LdLoc*>(instruction)) {
        if (ldloc->Variable != nullptr) {
            const auto& variableType = ldloc->Variable->Type;
            bool isExpressionTreeParameter =
                variableType != nullptr &&
                variableType->Namespace() == "System.Linq.Expressions" &&
                variableType->Name() == "ParameterExpression";
            if (isExpressionTreeParameter) {
                auto it = state_.parameterMapping.find(ldloc->Variable.get());
                if (it != state_.parameterMapping.end()) {
                    return {[v = it->second]() -> std::unique_ptr<ILInstruction> {
                                return std::make_unique<LdLoc>(v);
                            },
                            it->second->Type};
                }
            }
        }
        return {nullptr, nullptr};
    }
    return {nullptr, nullptr};
}



// ---- The ConvertCall arm (the C# `case "Call"`) ----

// The FullNameIs helper (defined below; the ns/type/name declaring-type
// pair check the GetMethodFromHandle match consults).
bool FullNameIs(const TypeSystem::IMethod* method, const std::string& ns,
                const std::string& typeName, const std::string& name);

// The C# `static bool MatchFromHandleParameterList(CallInstruction call,
// out IMember member)`: the ldmembertoken [+ ldtoken type] argument shapes.
bool TransformExpressionTrees::MatchFromHandleParameterList(
    Call* call,
    std::shared_ptr<const TypeSystem::IMember>& member) {
    member = nullptr;
    if (call == nullptr || call->Arguments.empty() ||
        call->Arguments.size() > 2) {
        return false;
    }
    auto* token = dynamic_cast<LdMemberToken*>(call->Arguments[0].get());
    if (token == nullptr || token->Member == nullptr) return false;
    if (call->Arguments.size() == 2) {
        if (dynamic_cast<LdTypeToken*>(call->Arguments[1].get()) == nullptr) {
            return false;
        }
    }
    member = token->Member;
    return true;
}

// The C# `bool MatchGetMethodFromHandle(ILInstruction inst, out IMember
// member)` (TransformExpressionTrees.cs): the castclass MethodInfo(
// MethodBase.GetMethodFromHandle(ldmembertoken member[, ldtoken type]))
// shape. Static (no member state).
bool TransformExpressionTrees::MatchGetMethodFromHandle(
    ILInstruction* inst,
    std::shared_ptr<const TypeSystem::IMethod>& member) {
    member = nullptr;
    auto* cast = dynamic_cast<CastClass*>(inst);
    if (cast == nullptr) return false;
    // The C# `type.FullName != "System.Reflection.MethodInfo"` -- the
    // namespace/name pair (the port's SimpleType stand-in).
    if (cast->Type == nullptr ||
        cast->Type->Namespace() != "System.Reflection" ||
        cast->Type->Name() != "MethodInfo") {
        return false;
    }
    auto* call = dynamic_cast<Call*>(cast->Argument.get());
    if (call == nullptr || call->IsNewObj || call->Method == nullptr ||
        call->Arguments.empty() || call->Arguments.size() > 2) {
        return false;
    }
    if (!FullNameIs(call->Method.get(), "System.Reflection", "MethodBase",
                    "GetMethodFromHandle")) {
        return false;
    }
    std::shared_ptr<const TypeSystem::IMember> tokenMember;
    if (!MatchFromHandleParameterList(call, tokenMember)) return false;
    member = std::dynamic_pointer_cast<const TypeSystem::IMethod>(tokenMember);
    return member != nullptr;
}

// The C# `bool MatchArgumentList(ILInstruction inst, out
// IList<ILInstruction> arguments)`: the ArrayInitializer block form
// (StObj(LdElema(ldc.i4 i), value) per slot; non-StObj items skipped, per
// the C# OfType<StObj>()) or the IsEmptyParameterList form. The returned
// pointers borrow the tree (the C# IList view); the thunks re-build at
// materialization time.
bool TransformExpressionTrees::MatchArgumentList(
    ILInstruction* inst, std::vector<ILInstruction*>& arguments) {
    arguments.clear();
    auto* block = dynamic_cast<Block*>(inst);
    if (block == nullptr || block->Kind != BlockKind::ArrayInitializer) {
        return IsEmptyParameterList(inst);
    }
    int i = 0;
    for (auto& item : block->Instructions) {
        auto* stobj = dynamic_cast<StObj*>(item.get());
        if (stobj == nullptr) continue;
        auto* ldelema = dynamic_cast<LdElema*>(stobj->Target.get());
        if (ldelema == nullptr) return false;
        if (ldelema->Indices.size() != 1) return false;
        auto* index = dynamic_cast<LdcI4*>(ldelema->Indices[0].get());
        if (index == nullptr || index->Value != i) return false;
        arguments.push_back(stobj->Value.get());
        i++;
    }
    return true;
}

// ---- The ConvertTypeAs / ConvertTypeIs arms ----

// The C# `(Func<ILInstruction>, IType) ConvertTypeAs(CallInstruction
// invocation)` -- the `case "TypeAs"` arm (TransformExpressionTrees.cs line
// 1289). The converted operand is a thunk (the C# Func); the isinst node
// materializes in BuildTypeAs. The ECMA-335 III.4.6 rule: a Nullable<T>
// typeTok is interpreted as boxed T, so the isinst is followed by
// `unbox.any(T, ...)`.
TransformExpressionTrees::ConvertResult TransformExpressionTrees::ConvertTypeAs(
    Call* invocation) {
    if (invocation == nullptr || invocation->Arguments.size() != 2) {
        return {nullptr, nullptr};
    }
    ConvertResult converted =
        ConvertInstruction(invocation->Arguments[0].get());
    if (!converted.thunk) return {nullptr, nullptr};
    TypeSystem::ITypePtr type;
    if (!MatchGetTypeFromHandle(invocation->Arguments[1].get(), type)) {
        return {nullptr, nullptr};
    }
    return {[converted = std::move(converted.thunk), type]() mutable
                -> std::unique_ptr<ILInstruction> {
        std::unique_ptr<ILInstruction> inst =
            std::make_unique<IsInst>(type, converted());
        // The C# `if (type.IsKnownType(KnownTypeCode.NullableOfT))`.
        if (type != nullptr && TypeSystem::IsKnownType(
                                   *type, TypeSystem::KnownTypeCode::NullableOfT)) {
            inst = std::make_unique<UnboxAny>(type, std::move(inst));
        }
        return inst;
    },
            std::move(type)};
}

// The C# `(Func<ILInstruction>, IType) ConvertTypeIs(CallInstruction
// invocation)` -- the `case "TypeIs"` arm (line 1305): `x is T` becomes
// comp(isinst(operand, T) != ldnull); the result type is Boolean.
TransformExpressionTrees::ConvertResult TransformExpressionTrees::ConvertTypeIs(
    Call* invocation) {
    if (invocation == nullptr || invocation->Arguments.size() != 2) {
        return {nullptr, nullptr};
    }
    ConvertResult converted =
        ConvertInstruction(invocation->Arguments[0].get());
    if (!converted.thunk) return {nullptr, nullptr};
    TypeSystem::ITypePtr type;
    if (!MatchGetTypeFromHandle(invocation->Arguments[1].get(), type)) {
        return {nullptr, nullptr};
    }
    // The C# `context.TypeSystem.FindType(KnownTypeCode.Boolean)` -- the
    // port's file-local FindType helper; a context without a type system
    // cannot resolve it, so the arm bails (the C# always resolves Boolean).
    TypeSystem::ITypePtr resultType =
        FindType(context_ != nullptr ? context_->Base.TypeSystem : nullptr,
                 TypeSystem::KnownTypeCode::Boolean);
    if (!resultType) {
        return {nullptr, nullptr};
    }
    return {[converted = std::move(converted.thunk), type]() mutable
                -> std::unique_ptr<ILInstruction> {
        auto isinst = std::make_unique<IsInst>(type, converted());
        return std::make_unique<Comp>(
            std::move(isinst), std::make_unique<LdNull>(),
            ComparisonKind::Inequality, TypeSystem::Sign::None);
    },
            std::move(resultType)};
}

// The C# `bool MatchGetFieldFromHandle(ILInstruction inst, out IMember
// member)`: the direct FieldInfo.GetFieldFromHandle(ldmembertoken field[,
// ldtoken type]) shape (no castclass wrapper, unlike the method-handle
// form). The member downcasts to IField (a method token is rejected).
bool TransformExpressionTrees::MatchGetFieldFromHandle(
    ILInstruction* inst,
    std::shared_ptr<const TypeSystem::IField>& member) {
    member = nullptr;
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj || call->Method == nullptr ||
        call->Arguments.empty() || call->Arguments.size() > 2) {
        return false;
    }
    if (!FullNameIs(call->Method.get(), "System.Reflection", "FieldInfo",
                    "GetFieldFromHandle")) {
        return false;
    }
    std::shared_ptr<const TypeSystem::IMember> tokenMember;
    if (!MatchFromHandleParameterList(call, tokenMember)) return false;
    member = std::dynamic_pointer_cast<const TypeSystem::IField>(tokenMember);
    return member != nullptr;
}

// The C# `(Func<ILInstruction>, IType) ConvertField(CallInstruction
// invocation, IType typeHint)` -- the `case "Field"` arm. The C#'s
// `typeHint.SkipModifiers()` dereferences a null hint on the lambda-body
// path; the port guards it (a null hint skips the by-ref adapter, and the
// value-type LdObj wrap applies, matching the C# BuildField fall-through).
// The typeHint by-ref arm (a `ref` field read feeding an expected Ref
// slot) is deferred with the SkipModifiers surface.
TransformExpressionTrees::ConvertResult TransformExpressionTrees::ConvertField(
    Call* invocation, TypeSystem::IType* typeHint) {
    if (invocation == nullptr || invocation->Arguments.size() != 2) {
        return {nullptr, nullptr};
    }
    std::function<std::unique_ptr<ILInstruction>()> targetConverter;
    if (!dynamic_cast<LdNull*>(invocation->Arguments[0].get())) {
        ConvertResult target =
            ConvertInstruction(invocation->Arguments[0].get());
        if (!target.thunk) return {nullptr, nullptr};
        targetConverter = std::move(target.thunk);
    }
    std::shared_ptr<const TypeSystem::IField> member;
    if (!MatchGetFieldFromHandle(invocation->Arguments[1].get(), member)) {
        return {nullptr, nullptr};
    }
    // The C# `IType type = member.ReturnType;`.
    TypeSystem::ITypePtr fieldType =
        const_cast<TypeSystem::IType&>(member->ReturnType()).shared_from_this();
    // The C# BuildField: ldsflda for a null target, ldflda (+ addressof for
    // a value-type receiver) otherwise; the plain type read wraps in ldobj.
    return {[member = std::move(member),
             targetConverter = std::move(targetConverter),
             typeHintKind = typeHint != nullptr ? typeHint->Kind()
                                                : TypeSystem::TypeKind::None]() mutable
                -> std::unique_ptr<ILInstruction> {
        std::unique_ptr<ILInstruction> inst;
        if (!targetConverter) {
            inst = std::make_unique<LdsFlda>(member->Name());
        } else {
            std::unique_ptr<ILInstruction> target = targetConverter();
            TypeSystem::ITypePtr declaring = member->DeclaringType();
            if (declaring != nullptr &&
                declaring->IsReferenceType() == std::optional<bool>(true)) {
                auto ldflda = std::make_unique<LdFlda>(std::move(target),
                                                       member->Name());
                ldflda->DelayExceptions = true;
                inst = std::move(ldflda);
            } else {
                inst = std::make_unique<LdFlda>(
                    std::make_unique<AddressOf>(
                        std::move(target), std::move(declaring)),
                    member->Name());
                dynamic_cast<LdFlda*>(inst.get())->DelayExceptions = true;
            }
        }
        // The C# `if (!(typeHint.SkipModifiers() is ByReferenceType && ...))
        // inst = new LdObj(inst, member.ReturnType);` -- the port wraps when
        // no by-ref hint overrides the read (the C# null-hint NRE is guarded).
        if (typeHintKind != TypeSystem::TypeKind::ByReference) {
            inst = std::make_unique<LdObj>(std::move(inst),
                                           const_cast<TypeSystem::IType&>(
                                               member->ReturnType())
                                               .shared_from_this());
        }
        return inst;
    },
            std::move(fieldType)};
}

// The C# `ILInstruction PrepareCallTarget(IType expectedType, ILInstruction
// target, IType targetType)`: the receiver shaping for the instance-call
// arm. The expectedType-Unknown / result-Unknown Conv wraps are deferred
// with the PrimitiveType.Unknown surface.
std::unique_ptr<ILInstruction> TransformExpressionTrees::PrepareCallTarget(
    const TypeSystem::IType& expectedType, std::unique_ptr<ILInstruction> target,
    TypeSystem::ITypePtr targetType) {
    std::unique_ptr<ILInstruction> result;
    switch (Call::ExpectedTypeForThisPointer(expectedType, nullptr)) {
        case StackType::Ref:
            if (target != nullptr && target->ResultType() == StackType::Ref) {
                result = std::move(target);
            } else if (auto* ldloc = dynamic_cast<LdLoc*>(target.get())) {
                // The C# `new LdLoca(ldloc.Variable).WithILRange(ldloc)`.
                auto ldloca = std::make_unique<LdLoca>(ldloc->Variable);
                ldloca->StartILOffset = ldloc->StartILOffset;
                ldloca->EndILOffset = ldloc->EndILOffset;
                result = std::move(ldloca);
            } else {
                result = std::make_unique<AddressOf>(
                    std::move(target),
                    const_cast<TypeSystem::IType&>(expectedType)
                        .shared_from_this());
            }
            break;
        case StackType::O:
            if (targetType != nullptr &&
                targetType->IsReferenceType() == std::optional<bool>(false)) {
                result = std::make_unique<Box>(std::move(targetType),
                                               std::move(target));
            } else {
                result = std::move(target);
            }
            break;
        default:
            result = std::move(target);
            break;
    }
    return result;
}

// The C# `Func<ILInstruction>[] ConvertCallArguments(IList arguments,
// IMethod method)`: per-argument ConvertInstruction against the expected
// parameter type. False when an argument fails to convert (the C# null
// return).
bool TransformExpressionTrees::ConvertCallArguments(
    const std::vector<ILInstruction*>& arguments,
    const TypeSystem::IMethod& method,
    std::vector<std::function<std::unique_ptr<ILInstruction>()>>& out) {
    out.clear();
    out.reserve(arguments.size());
    std::vector<const TypeSystem::IParameter*> parameters =
        method.Parameters();
    for (std::size_t i = 0; i < arguments.size(); i++) {
        TypeSystem::IType* expectedType = nullptr;
        if (i < parameters.size() && parameters[i] != nullptr) {
            expectedType =
                const_cast<TypeSystem::IType*>(&parameters[i]->Type());
        }
        ConvertResult converted =
            ConvertInstruction(arguments[i], expectedType);
        if (!converted.thunk) return false;
        out.push_back(std::move(converted.thunk));
    }
    return true;
}

// The C# `(Func<ILInstruction>, IType) ConvertCall(CallInstruction
// invocation)` -- the `case "Call"` arm (TransformExpressionTrees.cs line
// 578). The CreateDelegate special case (rebuilding `newobj(delegateType,
// value, ldftn targetMethod)` from a closed-generic MethodInfo.CreateDelegate
// call) is deferred with the constructor-resolution surface (the C#
// `delegateType.GetConstructors().Single()`).
TransformExpressionTrees::ConvertResult TransformExpressionTrees::ConvertCall(
    Call* invocation) {
    if (invocation == nullptr || invocation->Method == nullptr ||
        invocation->Arguments.size() < 2) {
        return {nullptr, nullptr};
    }
    std::shared_ptr<const TypeSystem::IMethod> member;
    std::vector<ILInstruction*> arguments;
    bool haveArguments = false;
    std::function<std::unique_ptr<ILInstruction>()> targetConverter;
    TypeSystem::ITypePtr targetType;
    if (MatchGetMethodFromHandle(invocation->Arguments[0].get(), member)) {
        // The static-method form: the handle is the first argument.
        haveArguments =
            invocation->Arguments.size() == 2 &&
            MatchArgumentList(invocation->Arguments[1].get(), arguments);
        if (!haveArguments) {
            // The C# `arguments = invocation.Arguments.Skip(1)` -- the raw
            // arguments when the block form is absent.
            for (std::size_t i = 1; i < invocation->Arguments.size(); i++) {
                arguments.push_back(invocation->Arguments[i].get());
            }
            haveArguments = true;
        }
    } else if (MatchGetMethodFromHandle(invocation->Arguments[1].get(),
                                        member)) {
        // The instance form: target, handle, arguments.
        haveArguments =
            invocation->Arguments.size() == 3 &&
            MatchArgumentList(invocation->Arguments[2].get(), arguments);
        if (!haveArguments) {
            for (std::size_t i = 2; i < invocation->Arguments.size(); i++) {
                arguments.push_back(invocation->Arguments[i].get());
            }
            haveArguments = true;
        }
        if (!dynamic_cast<LdNull*>(invocation->Arguments[0].get())) {
            ConvertResult target =
                ConvertInstruction(invocation->Arguments[0].get());
            if (!target.thunk) return {nullptr, nullptr};
            targetConverter = std::move(target.thunk);
            targetType = std::move(target.type);
        }
    }
    if (!haveArguments || member == nullptr) return {nullptr, nullptr};
    std::vector<std::function<std::unique_ptr<ILInstruction>()>>
        convertedArguments;
    if (!ConvertCallArguments(arguments, *member, convertedArguments)) {
        return {nullptr, nullptr};
    }
    // The C# BuildCall: Call for a static method, CallVirt for an instance
    // one (the port's Call node covers both opcodes; the callvirt
    // distinction rides the method's IsStatic).
    TypeSystem::ITypePtr returnType =
        const_cast<TypeSystem::IType&>(member->ReturnType()).shared_from_this();
    return {[member = std::move(member),
             targetConverter = std::move(targetConverter),
             targetType = std::move(targetType),
             convertedArguments = std::move(convertedArguments)]() mutable
                -> std::unique_ptr<ILInstruction> {
                auto call = std::make_unique<Call>(
                    std::const_pointer_cast<TypeSystem::IMethod>(member));
                if (targetConverter) {
                    call->Arguments.push_back(
                        PrepareCallTarget(*member->DeclaringType(),
                                          targetConverter(),
                                          std::move(targetType)));
                }
                for (auto& f : convertedArguments) {
                    call->Arguments.push_back(f());
                }
                return call;
            },
            std::move(returnType)};
}

// The C# `method.FullNameIs(ns, name)` extension: true iff the method's
// declaring type is the namespace-qualified type `ns.name` and the method's
// simple name matches (the FullNameIs(member, type, name) shape -- the
// declaring type's FullName pair).
bool FullNameIs(const TypeSystem::IMethod* method, const std::string& ns,
                const std::string& typeName, const std::string& name) {
    if (method == nullptr) return false;
    if (method->Name() != name) return false;
    TypeSystem::ITypePtr declaring = method->DeclaringType();
    if (declaring == nullptr) return false;
    return declaring->Namespace() == ns && declaring->Name() == typeName;
}

bool TransformExpressionTrees::MatchParameterVariableAssignment(
    ILInstruction* expr, std::shared_ptr<ILVariable>& parameterReferenceVar,
    TypeSystem::ITypePtr& type, std::string& name) {
    // stloc(v, call(Expression::Parameter,
    //               call(Type::GetTypeFromHandle, ldtoken(...)),
    //               ldstr(...)))
    type = nullptr;
    name.clear();
    auto* stloc = dynamic_cast<StLoc*>(expr);
    if (stloc == nullptr || stloc->Variable == nullptr) return false;
    parameterReferenceVar = stloc->Variable;
    if (!parameterReferenceVar->IsSingleDefinition()) return false;
    if (!(parameterReferenceVar->Kind == VariableKind::Local ||
          parameterReferenceVar->Kind == VariableKind::StackSlot)) {
        return false;
    }
    const auto& variableType = parameterReferenceVar->Type;
    if (variableType == nullptr ||
        variableType->Namespace() != "System.Linq.Expressions" ||
        variableType->Name() != "ParameterExpression") {
        return false;
    }
    auto* initCall = dynamic_cast<Call*>(stloc->Value.get());
    if (initCall == nullptr || initCall->Arguments.size() != 2 ||
        initCall->IsNewObj) {
        return false;
    }
    if (!FullNameIs(initCall->Method.get(), "System.Linq.Expressions",
                    "Expression", "Parameter")) {
        return false;
    }
    auto* typeArg = dynamic_cast<Call*>(initCall->Arguments[0].get());
    if (typeArg == nullptr || typeArg->Arguments.size() != 1 ||
        typeArg->IsNewObj) {
        return false;
    }
    if (!FullNameIs(typeArg->Method.get(), "System", "Type",
                    "GetTypeFromHandle")) {
        return false;
    }
    auto* token = dynamic_cast<LdTypeToken*>(typeArg->Arguments[0].get());
    if (token == nullptr || token->Type == nullptr) return false;
    type = token->Type;
    auto* nameArg = dynamic_cast<LdStr*>(initCall->Arguments[1].get());
    if (nameArg == nullptr) return false;
    name = nameArg->Value;
    return true;
}

bool TransformExpressionTrees::MightBeExpressionTree(ILInstruction* inst,
                                                     ILInstruction* stmt) {
    (void)stmt;  // the C# CanUninline gate is commented out there too
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj ||
        !FullNameIs(call->Method.get(), "System.Linq.Expressions", "Expression",
                    "Lambda") ||
        call->Arguments.size() != 2) {
        return false;
    }
    if (!IsEmptyParameterList(call->Arguments[1].get())) {
        return false;
    }
    return true;
}

bool TransformExpressionTrees::IsEmptyParameterList(ILInstruction* inst) {
    if (auto* emptyCall = dynamic_cast<Call*>(inst)) {
        if (!emptyCall->IsNewObj && emptyCall->Arguments.empty() &&
            FullNameIs(emptyCall->Method.get(), "System", "Array", "Empty")) {
            return true;
        }
    }
    if (auto* newArr = dynamic_cast<NewArr*>(inst)) {
        if (newArr->Type != nullptr &&
            newArr->Type->Namespace() == "System.Linq.Expressions" &&
            (newArr->Type->Name() == "ParameterExpression" ||
             newArr->Type->Name() == "Expression")) {
            return true;
        }
    }
    return false;
}


} // namespace ILSpy::Decompiler::IL
