// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//
// The C# AsyncAwaitDecompiler body (part 1: the task-creation pattern).

#include "Decompiler/IL/ControlFlow/AsyncAwaitDecompiler.hpp"


#include "Decompiler/IL/ControlFlow/YieldReturnDecompiler.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/Nop.hpp"
#include "Decompiler/IL/Instructions/TryInstructions.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/Metadata/CodeMappingInfo.hpp"  // IsCompilerGeneratedStateMachine
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TaskType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

namespace ILSpy::Decompiler::IL {

namespace {

// The C# TaskType helpers' `ns` constant (TaskType.cs line 67).
constexpr const char* kBuilderNs = "System.Runtime.CompilerServices";

// The method-name tail of a reader-resolved call: the reader records
// "Namespace.Type::Method"; the C# reads `call.Method.Name` (the resolved
// method's bare name).
std::string MethodNameTail(const std::string& methodName) {
    std::size_t pos = methodName.rfind("::");
    return pos == std::string::npos ? methodName
                                   : methodName.substr(pos + 2);
}

// The metadata type name without the arity suffix: the port's reader types
// carry the raw metadata name ("AsyncTaskMethodBuilder`1"), while the C#
// `IType.Name` strips the arity (the TopLevelTypeName comparison the C#
// performs sees the bare name).
std::string NameWithoutArity(const std::string& name) {
    std::size_t pos = name.rfind('`');
    if (pos == std::string::npos) return name;
    for (std::size_t i = pos + 1; i < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9')
            return name;  // not an arity suffix
    }
    return name.substr(0, pos);
}

// File-local MatchLdcI4 out-form (the C# `MatchLdcI4(out int value)`): an
// LdcI4 reporting its value. The shared MatchLdcI4 overload is the
// match-against-value form (the D66/D94 discipline).
bool MatchLdcI4Out(ILInstruction* inst, int& value) {
    auto* ldc = dynamic_cast<LdcI4*>(inst);
    if (ldc == nullptr) {
        value = 0;
        return false;
    }
    value = ldc->Value;
    return true;
}

// File-local MatchLdLoca out-form (the C# `MatchLdLoca(out ILVariable
// variable)`): an LdLoca reporting its variable. Same discipline as
// MatchLdLocOut below.
bool MatchLdLocaOut(ILInstruction* inst, ILVariable*& variable) {
    auto* ldloca = dynamic_cast<LdLoca*>(inst);
    if (ldloca == nullptr) {
        variable = nullptr;
        return false;
    }
    variable = ldloca->Variable.get();
    return true;
}

// File-local MatchLdLoc out-form (the C# `MatchLdLoc(out ILVariable
// variable)`): an LdLoc reporting its variable. The shared MatchLdLoc is
// the match-against form (the D66/D94 discipline).
bool MatchLdLocOut(ILInstruction* inst, ILVariable*& variable) {
    auto* ldloc = dynamic_cast<LdLoc*>(inst);
    if (ldloc == nullptr) {
        variable = nullptr;
        return false;
    }
    variable = ldloc->Variable.get();
    return true;
}

} // namespace

void AsyncAwaitDecompiler::Run(ILFunction& function,
                               ILTransformContext& context) {
    if (!context.Settings.AsyncAwait)
        return;  // abort if async/await decompilation is disabled
    context_ = &context;
    fieldToParameterMap_.clear();

    // The port's deferred-resolution convention: the pattern matchers read
    // field and method identities from the driver-decoded body, so the
    // reader surfaces resolve first (the CreateILAst precedent).
    YieldReturnDecompiler::ResolveReaderSurfaces(function, context);

    if (!MatchTaskCreationPattern(function)) {
        // The async-enumerator creation pattern and the runtime-async
        // transforms are deferred with their slices.
        return;
    }

    // SLICE STATE (part 2): AnalyzeMoveNext + ValidateCatchBlock +
    // AnalyzeDisposeAsync + InlineBodyOfMoveNext; (part 3) the state
    // machine analysis and the await-pattern detection. Until they land,
    // Run stops after the pattern match.
}

bool AsyncAwaitDecompiler::MatchTaskCreationPattern(ILFunction& function) {
    methodType_ = AsyncMethodType::Void;
    stateMachineType_ = nullptr;
    builderType_ = nullptr;
    builderField_ = nullptr;
    stateField_ = nullptr;
    initialState_ = 0;
    fieldToParameterMap_.clear();

    auto* blockContainer = dynamic_cast<BlockContainer*>(function.Body.get());
    if (blockContainer == nullptr)
        return false;
    if (blockContainer->Blocks.size() != 1)
        return false;
    // The port's terminator convention: the reader carries the method's
    // `ret` in the FinalInstruction slot, so the C# instruction list maps
    // to the port's Instructions followed by the final. The accessor below
    // keeps the C# index arithmetic verbatim (a C# list index < size reads
    // the port's instruction; the last index reads the final).
    Block* entry = blockContainer->EntryPoint();
    const std::vector<std::unique_ptr<ILInstruction>>& body =
        entry->Instructions;
    const int count = static_cast<int>(body.size()) +
                      (entry->FinalInstruction != nullptr ? 1 : 0);
    if (count < 4)
        return false;
    auto BodyAt = [&](int index) -> ILInstruction* {
        if (index >= 0 && index < static_cast<int>(body.size()))
            return body[static_cast<std::size_t>(index)].get();
        if (index == static_cast<int>(body.size()) &&
            entry->FinalInstruction != nullptr)
            return entry->FinalInstruction.get();
        return nullptr;
    };
    ILInstruction* bodyLast =
        entry->FinalInstruction != nullptr ? entry->FinalInstruction.get()
                                           : (!body.empty()
                                                  ? body.back().get()
                                                  : nullptr);

    // Check the second-to-last instruction (the start call) first, as we can
    // get the most information from that.
    int pos = count - 2;
    auto* startCall = dynamic_cast<Call*>(BodyAt(pos));
    if (startCall == nullptr)
        return false;
    if (MethodNameTail(startCall->MethodName) != "Start")
        return false;
    if (function.Method == nullptr)
        return false;
    taskType_ = std::const_pointer_cast<TypeSystem::IType>(
        std::shared_ptr<const TypeSystem::IType>(
            std::shared_ptr<const TypeSystem::IType>(),
            &function.Method->ReturnType()));
    // The builder type: the start call's declaring type (the reader
    // resolves the call token's parent; the C# reads
    // startCall.Method.DeclaringType).
    builderType_ = startCall->DeclaringType;
    if (builderType_ == nullptr)
        return false;
    if (!ClassifyTaskType(function, startCall, *builderType_)) {
        return false;
    }
    if (startCall->Arguments.size() != 2)
        return false;
    ILInstruction* loadBuilderExpr = startCall->Arguments[0].get();
    ILVariable* stateMachineVar = nullptr;
    if (!MatchLdLocaOut(startCall->Arguments[1].get(), stateMachineVar))
        return false;
    if (stateMachineVar == nullptr || stateMachineVar->Type == nullptr)
        return false;
    stateMachineType_ = ResolveStateMachineType(
        *stateMachineVar->Type, *context_->Metadata, *context_);
    if (stateMachineType_ == nullptr) {
        return false;
    }
    pos--;

    // A reference-type builder is copied to a local first
    // (`stloc builder(ldfld StateMachine::<>t__builder(ldloc stateMachine))`).
    if (MatchLdLocRef(loadBuilderExpr, stateMachineVar)) {
        // Check third-to-last instruction (copy of builder).
        ILVariable* builderVar = nullptr;
        if (pos < 0 ||
            !MatchStLoc(body[static_cast<std::size_t>(pos)].get(), builderVar,
                       loadBuilderExpr) ||
            builderVar == nullptr)
            return false;
        pos--;
    }
    ILInstruction* loadStateMachineForBuilderExpr = nullptr;
    if (MatchLdFld(loadBuilderExpr, loadStateMachineForBuilderExpr, builderField_)) {
        // OK, calling Start on copy of stateMachine.<>t__builder
    } else if (MatchLdFlda(loadBuilderExpr, loadStateMachineForBuilderExpr,
                           builderField_)) {
        // OK, Roslyn 3.6 started directly calling Start without making a copy
    } else {
        return false;
    }
    builderField_ = builderField_ != nullptr
                        ? static_cast<const TypeSystem::IField*>(
                              builderField_->MemberDefinition())
                        : nullptr;
    if (!(MatchLdLocRef(loadStateMachineForBuilderExpr, stateMachineVar) ||
          MatchLdLoc(loadStateMachineForBuilderExpr, stateMachineVar))) {
        return false;
    }

    // Check the last instruction (ret). The port's terminator convention:
    // the C# `body.Last()` is the FinalInstruction.
    if (methodType_ == AsyncMethodType::Void) {
        if (!MatchLeave(bodyLast, blockContainer)) {
            return false;
        }
    } else {
        // ret(call(AsyncTaskMethodBuilder::get_Task, ldflda(StateMachine::<>t__builder, ldloca(stateMachine))))
        ILInstruction* returnValue = nullptr;
        if (!MatchReturn(bodyLast, returnValue)) {
            return false;
        }
        std::vector<ILInstruction*> getTaskArgs;
        if (returnValue == nullptr ||
            !MatchCall(returnValue, "get_Task", getTaskArgs) ||
            getTaskArgs.size() != 1)
            return false;
        ILInstruction* target = nullptr;
        const TypeSystem::IField* builderField2 = nullptr;
        const std::optional<bool> builderIsRef = builderType_->IsReferenceType();
        if (builderIsRef.has_value() && *builderIsRef) {
            if (!MatchLdFld(getTaskArgs[0], target, builderField2))
                return false;
        } else {
            if (!MatchLdFlda(getTaskArgs[0], target, builderField2))
                return false;
        }
        if (builderField2 == nullptr ||
            builderField2->MemberDefinition() != builderField_)
            return false;
        if (!(MatchLdLoc(target, stateMachineVar) ||
              MatchLdLoca(target, stateMachineVar)))
            return false;
    }

    // The Visual Basic state machine initialization order is deferred with
    // the VB arms (the IsPotentialVisualBasicStateMachineInitialiation
    // check). The C# state machine initialization follows.

    // Check the last field assignment - this should be the state field
    // stfld <>1__state(ldloca stateField, ldc.i4 -1)
    ILInstruction* initialStateExpr = nullptr;
    if (pos < 0 ||
        !MatchStFld(BodyAt(pos), stateMachineVar,
                    stateField_, initialStateExpr))
        return false;
    if (!MatchLdcI4Out(initialStateExpr, initialState_))
        return false;
    if (initialState_ != -1)
        return false;

    int stopPos = pos;
    pos = 0;
    if (stateMachineType_->Kind() == TypeSystem::TypeKind::Class) {
        // If state machine is a class, the first instruction creates an
        // instance: stloc stateMachine(newobj StateMachine.ctor())
        ILVariable* var = nullptr;
        ILInstruction* init = nullptr;
        if (!MatchStLoc(BodyAt(pos), var, init) ||
            var != stateMachineVar)
            return false;
        auto* newobj = dynamic_cast<Call*>(init);
        if (newobj == nullptr || !newobj->IsNewObj || !newobj->Arguments.empty())
            return false;
        pos++;
    }
    bool builderFieldIsInitialized = false;
    for (; pos < stopPos; pos++) {
        // stfld StateMachine.field(ldloca stateMachine, ldvar(param))
        const TypeSystem::IField* field = nullptr;
        ILInstruction* fieldInit = nullptr;
        if (!MatchStFld(BodyAt(pos), stateMachineVar, field, fieldInit))
            return false;
        if (field == builderField_) {
            // stfld StateMachine.builder(ldloca stateMachine, call Create())
            auto* createCall = dynamic_cast<Call*>(fieldInit);
            if (createCall == nullptr ||
                MethodNameTail(createCall->MethodName) != "Create" ||
                !createCall->Arguments.empty())
                return false;
            builderFieldIsInitialized = true;
        } else {
            // stfld StateMachine.field(ldloca stateMachine, ldvar(param)):
            // the C# `fieldInit.MatchLdLoc(out var v) && v.Kind == Parameter`
            // -- the match-against form would need the variable first, so
            // the ldloc is read directly (the file-local out-form shape).
            ILVariable* v = nullptr;
            if (!MatchLdLocOut(fieldInit, v) || v == nullptr ||
                v->Kind != VariableKind::Parameter) {
                // The struct-`this` capture (ldobj(ldloc this)) is deferred
                // with the fieldToParameterMap arms that consume it (the
                // fixture's methods are not struct methods).
                return false;
            }
            // OK, copies parameter into state machine
            fieldToParameterMap_[field] = v;
        }
    }

    return builderFieldIsInitialized;
}

bool AsyncAwaitDecompiler::ClassifyTaskType(ILFunction& function,
                                             ILInstruction* startCall,
                                             const TypeSystem::IType& builderType) {
    (void)startCall;
    if (function.Method == nullptr)
        return false;
    const TypeSystem::IType& taskType = function.Method->ReturnType();
    if (TypeSystem::IsKnownType(taskType, TypeSystem::KnownTypeCode::Void)) {
        methodType_ = AsyncMethodType::Void;
        taskType_ = std::const_pointer_cast<TypeSystem::IType>(
            std::shared_ptr<const TypeSystem::IType>(
                std::shared_ptr<const TypeSystem::IType>(), &taskType));
        underlyingReturnType_ = taskType_;
        if (!(builderType.Namespace() == kBuilderNs &&
              NameWithoutArity(builderType.Name()) == "AsyncVoidMethodBuilder"))
            return false;
        return true;
    }
    TypeSystem::FullTypeName builderTypeNameFromTask;
    if (TypeSystem::IsNonGenericTaskType(taskType, builderTypeNameFromTask)) {
        methodType_ = AsyncMethodType::Task;
        underlyingReturnType_ = std::make_shared<TypeSystem::KnownType>(
            TypeSystem::KnownTypeCode::Void);
        if (!(builderType.Namespace() == kBuilderNs &&
              NameWithoutArity(builderType.Name()) == "AsyncTaskMethodBuilder"))
            return false;
        return true;
    }
    if (TypeSystem::IsGenericTaskType(taskType, builderTypeNameFromTask)) {
        methodType_ = AsyncMethodType::TaskOfT;
        if (TypeSystem::IsKnownType(taskType, TypeSystem::KnownTypeCode::TaskOfT)) {
            underlyingReturnType_ = TypeSystem::UnpackTask(
                context_->TypeSystem->MainModule().Compilation(), taskType);
        } else {
            // A custom generic Task-like: the "T" is the builder type's
            // first type argument (the C#
            // startCall.Method.DeclaringType.TypeArguments[0]).
            std::vector<TypeSystem::ITypePtr> args;
            if (const auto* parameterized =
                    dynamic_cast<const TypeSystem::ParameterizedType*>(
                        &builderType))
                args = parameterized->TypeArguments();
            if (args.empty() || args[0] == nullptr)
                return false;
            underlyingReturnType_ = args[0];
        }
        if (underlyingReturnType_ == nullptr)
            return false;
        if (!(builderType.Namespace() == kBuilderNs &&
              NameWithoutArity(builderType.Name()) == "AsyncTaskMethodBuilder" &&
              builderType.TypeParameterCount() == 1)) {
            return false;
        }
        return true;
    }
    return false;
}

bool AsyncAwaitDecompiler::MatchCall(ILInstruction* inst,
                                      const std::string& name,
                                      std::vector<ILInstruction*>& args) {
    // The C# `inst is CallInstruction call && (call.OpCode == Call ||
    // CallVirt) && call.Method.Name == name && !call.Method.IsStatic` -- the
    // port models Call/CallVirt as one node with the reader-resolved method
    // name and the instance-call bit.
    auto* call = dynamic_cast<Call*>(inst);
    if (call == nullptr || call->IsNewObj)
        return false;
    // The resolved method's bare name: the resolved IMethod when present,
    // else the reader's "Namespace.Type::Method" tail.
    if (call->Method != nullptr) {
        if (call->Method->Name() != name)
            return false;
        // The C# `!call.Method.IsStatic`: the port's resolved methods carry
        // no static bit on this surface; the reader's IsInstanceCall is the
        // signature-derived equivalent.
        if (!call->IsInstanceCall)
            return false;
    } else {
        if (MethodNameTail(call->MethodName) != name)
            return false;
        if (!call->IsInstanceCall)
            return false;
    }
    args.clear();
    args.reserve(call->Arguments.size());
    for (auto& arg : call->Arguments)
        args.push_back(arg.get());
    return !args.empty();
}

bool AsyncAwaitDecompiler::MatchStFld(ILInstruction* stfld,
                                      ILVariable* stateMachineVar,
                                      const TypeSystem::IField*& field,
                                      ILInstruction*& value) {
    ILInstruction* target = nullptr;
    if (!ILSpy::Decompiler::IL::MatchStFld(stfld, target, field, value))
        return false;
    // The C# `field.MemberDefinition as IField`.
    field = field != nullptr
                ? static_cast<const TypeSystem::IField*>(field->MemberDefinition())
                : nullptr;
    return field != nullptr && MatchLdLocRef(target, stateMachineVar);
}

const TypeSystem::ITypeDefinition*
AsyncAwaitDecompiler::ResolveStateMachineType(
    const TypeSystem::IType& localType,
    const Metadata::MetadataFile& metadata, ILTransformContext& context) {
    (void)context;
    // The reader's local types are name-only stand-ins; the state machine is
    // the nested type with the local's name in the current type.
    const std::string name = localType.Name();
    for (const auto& t : metadata.TypeDefs()) {
        auto info = metadata.GetTypeDefNameInfo(t.Token);
        if (!info.has_value() || info->Name != name)
            continue;
        if (info->DeclaringTypeToken == 0)
            continue;
        if (!Metadata::IsCompilerGeneratedStateMachine(metadata, t.Token))
            continue;
        if (context.TypeSystem == nullptr)
            return nullptr;
        const auto* module = dynamic_cast<const TypeSystem::MetadataModule*>(
            &context.TypeSystem->MainModule());
        if (module == nullptr)
            return nullptr;
        return module->GetDefinition(t.Token);
    }
    return nullptr;
}

} // namespace ILSpy::Decompiler::IL
