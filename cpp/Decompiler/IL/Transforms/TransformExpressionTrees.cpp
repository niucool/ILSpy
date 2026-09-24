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
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/IL/ILTypeExtensions.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"

#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
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
