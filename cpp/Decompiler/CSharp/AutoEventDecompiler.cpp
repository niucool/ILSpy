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

#include "Decompiler/CSharp/AutoEventDecompiler.hpp"

#include "Decompiler/CSharp/Syntax/EventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/PatternMatching.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/INamedElement.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/NormalizeTypeVisitor.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"

#include <cassert>
#include <iterator>
#include <string>

namespace ILSpy::Decompiler::CSharp {

namespace IL = ILSpy::Decompiler::IL;
namespace TS = ILSpy::Decompiler::TypeSystem;
using Metadata::MetadataFile;

namespace {

// The C# `bool IsDelegateCombineMethod(IMethod method, bool isAddAccessor)`: a
// static `System.Delegate.Combine` (add) / `Remove` (remove). The port reads the
// resolved `Call::Method` when present and falls back to the reader's
// `MethodName`/`IsInstanceCall` when the type system identity was not wired.
bool IsDelegateCombineMethod(const IL::Call& call, bool isAddAccessor) {
    const std::string expected = isAddAccessor ? "Combine" : "Remove";
    if (call.Method) {
        const TS::IMethod& method = *call.Method;
        TS::ITypePtr declaringType = method.DeclaringType();
        return method.IsStatic() && method.Name() == expected && declaringType != nullptr
            && declaringType->ReflectionName() == "System.Delegate";
    }
    return !call.IsInstanceCall
        && call.MethodName == "System.Delegate::" + expected;
}

// The C# `bool IsCompareExchangeMethod(IMethod method)`: a static
// `System.Threading.Interlocked.CompareExchange`.
bool IsCompareExchangeMethod(const IL::Call& call) {
    if (call.Method) {
        const TS::IMethod& method = *call.Method;
        TS::ITypePtr declaringType = method.DeclaringType();
        return method.IsStatic() && method.Name() == "CompareExchange"
            && declaringType != nullptr
            && declaringType->ReflectionName() == "System.Threading.Interlocked";
    }
    return !call.IsInstanceCall
        && call.MethodName.rfind("System.Threading.Interlocked::CompareExchange", 0) == 0;
}

// The C# `bool IsSameField(IField a, IField b)`: `a.MemberDefinition.Equals(b.MemberDefinition)`.
// The port compares the canonical IMember subobject pointers (the member-identity
// convention the transforms use).
bool IsSameField(const TS::IField& a, const TS::IField& b) {
    return a.MemberDefinition() == b.MemberDefinition();
}

// The C# `AutoEventDecompiler.attributeTypesToRemoveFromAutoEventAccessors`.
constexpr const char* kAttributeTypesToRemoveFromAutoEventAccessors[] = {
    "System.Runtime.CompilerServices.CompilerGeneratedAttribute",
    "System.Diagnostics.DebuggerBrowsableAttribute",
    "System.Runtime.CompilerServices.MethodImplAttribute",
};

// The C# `AutoEventDecompiler.attributeTypesToRemoveFromAutoEventFields`.
constexpr const char* kAttributeTypesToRemoveFromAutoEventFields[] = {
    "System.Runtime.CompilerServices.CompilerGeneratedAttribute",
    "System.Diagnostics.DebuggerBrowsableAttribute",
};

// The C# `IType.FullName` for the attribute-type comparison: an `IEntity` type
// reports `INamedElement::FullName()`, a `ParameterizedType` delegates to its
// generic, and any other shape falls back to `ReflectionName()` (the file-local
// helper convention of IntroduceUsingDeclarations / CustomPatterns / TypeSystemAstBuilder).
std::string TypeFullNameOf(const TS::IType& type) {
    if (const auto* named = dynamic_cast<const TS::INamedElement*>(&type))
        return named->FullName();
    if (const auto* parameterized = dynamic_cast<const TS::ParameterizedType*>(&type))
        return parameterized->GenericType() ? TypeFullNameOf(*parameterized->GenericType())
                                            : std::string();
    return type.ReflectionName();
}

// The C# `WithoutAttributeTypes(IEnumerable<IAttribute> attributes, string[]
// attributeTypesToRemove)`: the attributes whose type's full name is not in the
// removal set, in input order.
std::vector<const TS::IAttribute*> WithoutAttributeTypes(
    const std::vector<const TS::IAttribute*>& attributes,
    const char* const* attributeTypesToRemove, std::size_t attributeTypeCount) {
    std::vector<const TS::IAttribute*> result;
    for (const TS::IAttribute* attribute : attributes) {
        const std::string fullName = TypeFullNameOf(attribute->AttributeType());
        bool remove = false;
        for (std::size_t i = 0; i < attributeTypeCount; ++i) {
            if (fullName == attributeTypesToRemove[i]) {
                remove = true;
                break;
            }
        }
        if (!remove)
            result.push_back(attribute);
    }
    return result;
}

// The C# `bool MatchLdThisOrAlias(ILInstruction inst, ILVariable? thisAlias)`.
bool MatchLdThisOrAlias(const IL::ILInstruction* inst, const IL::ILVariable* thisAlias) {
    return IL::MatchLdThis(inst) || (thisAlias != nullptr && IL::MatchLdLoc(inst, thisAlias));
}

// The C# `bool MatchLdValueParameter(ILInstruction inst)`: `ldloc` of the accessor's
// `value` parameter (parameter index 0).
bool MatchLdValueParameter(const IL::ILInstruction* inst) {
    const auto* ldloc = dynamic_cast<const IL::LdLoc*>(inst);
    return ldloc != nullptr && ldloc->Variable != nullptr
        && ldloc->Variable->Kind == IL::VariableKind::Parameter
        && ldloc->Variable->Index == 0;
}

// The C# `bool MatchLoadOfField(ILInstruction inst, IMethod accessor, IField field,
// ILVariable? thisAlias = null)`.
bool MatchLoadOfField(const IL::ILInstruction* inst, const TS::IMethod& accessor,
                      const TS::IField& field, const IL::ILVariable* thisAlias = nullptr) {
    if (accessor.IsStatic()) {
        const TS::IField* f = nullptr;
        return IL::MatchLdsFld(inst, f) && f != nullptr && IsSameField(*f, field);
    } else {
        IL::ILInstruction* target = nullptr;
        const TS::IField* f = nullptr;
        return IL::MatchLdFld(inst, target, f) && f != nullptr
            && MatchLdThisOrAlias(target, thisAlias) && IsSameField(*f, field);
    }
}

// The C# `bool MatchAddressOfField(ILInstruction inst, IMethod accessor, IField field)`.
bool MatchAddressOfField(const IL::ILInstruction* inst, const TS::IMethod& accessor,
                         const TS::IField& field) {
    if (accessor.IsStatic()) {
        const TS::IField* f = nullptr;
        return IL::MatchLdsFlda(inst, f) && f != nullptr && IsSameField(*f, field);
    } else {
        IL::ILInstruction* target = nullptr;
        const TS::IField* f = nullptr;
        return IL::MatchLdFlda(inst, target, f) && f != nullptr
            && IL::MatchLdThis(target) && IsSameField(*f, field);
    }
}

// The C# `bool MatchCastedDelegateCombineCall(ILInstruction inst, IField field,
// ILVariable comparand, bool isAddAccessor)`: `(T)Delegate.Combine(comparand, value)`.
bool MatchCastedDelegateCombineCall(IL::ILInstruction* inst, const TS::IField& field,
                                    const IL::ILVariable& comparand, bool isAddAccessor) {
    auto* castclass = dynamic_cast<IL::CastClass*>(inst);
    if (castclass == nullptr || castclass->Type == nullptr)
        return false;
    if (!TS::NormalizeTypeVisitor::TypeErasure().EquivalentTypes(
            *castclass->Type, const_cast<TS::IType&>(field.ReturnType())))
        return false;
    auto* call = dynamic_cast<IL::Call*>(castclass->Argument.get());
    if (call == nullptr || !IsDelegateCombineMethod(*call, isAddAccessor)
        || call->Arguments.size() != 2)
        return false;
    return IL::MatchLdLoc(call->Arguments[0].get(), &comparand)
        && MatchLdValueParameter(call->Arguments[1].get());
}

// The C# `bool MatchCompareExchangeCall(ILInstruction inst, IMethod accessor,
// IField field, ILVariable valueArg, ILVariable comparandArg)`:
// `Interlocked.CompareExchange(ref this.field, valueArg, comparandArg)`.
bool MatchCompareExchangeCall(IL::ILInstruction* inst, const TS::IMethod& accessor,
                              const TS::IField& field, const IL::ILVariable& valueArg,
                              const IL::ILVariable& comparandArg) {
    auto* call = dynamic_cast<IL::Call*>(inst);
    if (call == nullptr || !IsCompareExchangeMethod(*call) || call->Arguments.size() != 3)
        return false;
    return MatchAddressOfField(call->Arguments[0].get(), accessor, field)
        && IL::MatchLdLoc(call->Arguments[1].get(), &valueArg)
        && IL::MatchLdLoc(call->Arguments[2].get(), &comparandArg);
}

// The C# `bool MatchLoopExit(Block loopBody, BlockContainer loop, ILVariable
// oldValue, ILVariable comparand)`:
//   if (oldValue == comparand) break;
// i.e. "if (comp.o(ldloc oldValue == ldloc comparand)) leave loop" followed by the
// back-branch. The port's Block exposes the terminator through ChildCount/GetChild
// (FinalInstruction is the last child), so the C# `Instructions[count-2]` /
// `[count-1]` indices map directly.
bool MatchLoopExit(IL::Block& loopBody, IL::BlockContainer& loop,
                   const IL::ILVariable& oldValue, const IL::ILVariable& comparand) {
    const int count = loopBody.ChildCount();
    if (count < 2)
        return false;
    IL::ILInstruction* condition = nullptr;
    IL::ILInstruction* trueInst = nullptr;
    IL::ILInstruction* falseInst = nullptr;
    if (!IL::MatchIfInstruction(loopBody.GetChild(count - 2), condition, trueInst, falseInst))
        return false;
    auto* comp = dynamic_cast<IL::Comp*>(condition);
    if (comp == nullptr || comp->Kind != IL::ComparisonKind::Equality)
        return false;
    if (!IL::MatchLdLoc(comp->Left.get(), &oldValue)
        || !IL::MatchLdLoc(comp->Right.get(), &comparand))
        return false;
    if (!IL::MatchLeave(trueInst, &loop))
        return false;
    return IL::MatchBranch(loopBody.GetChild(count - 1), &loopBody);
}

// The C# `bool MatchCompareExchangeLoop(Block body, BlockContainer functionBody,
// IMethod accessor, IField field, bool isAddAccessor)`.
bool MatchCompareExchangeLoop(IL::Block& body, IL::BlockContainer& functionBody,
                              const TS::IMethod& accessor, const TS::IField& field,
                              bool isAddAccessor) {
    if (body.ChildCount() != 3)
        return false;
    IL::ILVariable* oldValue = nullptr;
    IL::ILInstruction* init = nullptr;
    if (!IL::MatchStLoc(body.GetChild(0), oldValue)
        || !IL::MatchStLoc(body.GetChild(0), oldValue, init))
        return false;
    if (!MatchLoadOfField(init, accessor, field))
        return false;
    auto* loop = dynamic_cast<IL::BlockContainer*>(body.GetChild(1));
    if (loop == nullptr || loop->Kind != IL::ContainerKind::Loop || loop->Blocks.size() != 1)
        return false;
    if (!IL::MatchLeave(body.GetChild(2), &functionBody))
        return false;
    IL::Block* loopBody = loop->EntryPoint();
    if (loopBody == nullptr || loopBody->ChildCount() != 5)
        return false;
    IL::ILVariable* comparand = nullptr;
    IL::ILInstruction* comparandInit = nullptr;
    if (!IL::MatchStLoc(loopBody->GetChild(0), comparand)
        || !IL::MatchStLoc(loopBody->GetChild(0), comparand, comparandInit)
        || comparand == oldValue)
        return false;
    if (!IL::MatchLdLoc(comparandInit, oldValue))
        return false;
    IL::ILVariable* newValue = nullptr;
    IL::ILInstruction* newValueInit = nullptr;
    if (!IL::MatchStLoc(loopBody->GetChild(1), newValue)
        || !IL::MatchStLoc(loopBody->GetChild(1), newValue, newValueInit)
        || newValue == oldValue || newValue == comparand)
        return false;
    if (!MatchCastedDelegateCombineCall(newValueInit, field, *comparand, isAddAccessor))
        return false;
    IL::ILInstruction* compareExchangeCall = nullptr;
    if (!IL::MatchStLoc(loopBody->GetChild(2), oldValue, compareExchangeCall))
        return false;
    if (!MatchCompareExchangeCall(compareExchangeCall, accessor, field, *newValue, *comparand))
        return false;
    return MatchLoopExit(*loopBody, *loop, *oldValue, *comparand);
}

// The C# `bool MatchCompareExchangeLoopMcs(Block body, BlockContainer functionBody,
// IMethod accessor, IField field, bool isAddAccessor)`.
bool MatchCompareExchangeLoopMcs(IL::Block& body, IL::BlockContainer& functionBody,
                                 const TS::IMethod& accessor, const TS::IField& field,
                                 bool isAddAccessor) {
    if (body.ChildCount() != 3)
        return false;
    IL::ILVariable* oldValue = nullptr;
    IL::ILInstruction* init = nullptr;
    if (!IL::MatchStLoc(body.GetChild(0), oldValue)
        || !IL::MatchStLoc(body.GetChild(0), oldValue, init))
        return false;
    if (!MatchLoadOfField(init, accessor, field))
        return false;
    auto* loop = dynamic_cast<IL::BlockContainer*>(body.GetChild(1));
    if (loop == nullptr || loop->Kind != IL::ContainerKind::Loop || loop->Blocks.size() != 1)
        return false;
    if (!IL::MatchLeave(body.GetChild(2), &functionBody))
        return false;
    IL::Block* loopBody = loop->EntryPoint();
    if (loopBody == nullptr || loopBody->ChildCount() != 4)
        return false;
    IL::ILVariable* comparand = nullptr;
    IL::ILInstruction* comparandInit = nullptr;
    if (!IL::MatchStLoc(loopBody->GetChild(0), comparand)
        || !IL::MatchStLoc(loopBody->GetChild(0), comparand, comparandInit)
        || comparand == oldValue)
        return false;
    if (!IL::MatchLdLoc(comparandInit, oldValue))
        return false;
    IL::ILInstruction* compareExchangeCall = nullptr;
    if (!IL::MatchStLoc(loopBody->GetChild(1), oldValue, compareExchangeCall))
        return false;
    auto* call = dynamic_cast<IL::Call*>(compareExchangeCall);
    if (call == nullptr || !IsCompareExchangeMethod(*call) || call->Arguments.size() != 3)
        return false;
    if (!MatchAddressOfField(call->Arguments[0].get(), accessor, field))
        return false;
    if (!MatchCastedDelegateCombineCall(call->Arguments[1].get(), field, *comparand, isAddAccessor))
        return false;
    if (!IL::MatchLdLoc(call->Arguments[2].get(), oldValue))
        return false;
    return MatchLoopExit(*loopBody, *loop, *oldValue, *comparand);
}

// The C# `bool MatchSimpleCombineAssignment(Block body, BlockContainer functionBody,
// IMethod accessor, IField field, bool isAddAccessor)`:
//   this.field = (T)Delegate.Combine(this.field, value);
// mcs 2.x compiles the accessor as a compound assignment, evaluating 'this' once
// into a temporary (IL 'dup'), so the store may be preceded by "stloc S(ldloc this)"
// with both field accesses going through S.
bool MatchSimpleCombineAssignment(IL::Block& body, IL::BlockContainer& functionBody,
                                  const TS::IMethod& accessor, const TS::IField& field,
                                  bool isAddAccessor) {
    IL::ILVariable* thisAlias = nullptr;
    int pos = 0;
    if (!accessor.IsStatic() && body.ChildCount() == 3) {
        IL::ILInstruction* aliasInit = nullptr;
        if (!IL::MatchStLoc(body.GetChild(0), thisAlias)
            || !IL::MatchStLoc(body.GetChild(0), thisAlias, aliasInit)
            || !IL::MatchLdThis(aliasInit))
            return false;
        if (thisAlias->StoreCount != 1 || thisAlias->LoadCount != 2
            || thisAlias->AddressCount != 0)
            return false;
        pos = 1;
    } else if (body.ChildCount() != 2) {
        return false;
    }
    IL::ILInstruction* value = nullptr;
    if (accessor.IsStatic()) {
        const TS::IField* f = nullptr;
        if (!IL::MatchStsFld(body.GetChild(pos), f, value) || f == nullptr
            || !IsSameField(*f, field))
            return false;
    } else {
        IL::ILInstruction* target = nullptr;
        const TS::IField* f = nullptr;
        if (!IL::MatchStFld(body.GetChild(pos), target, f, value) || f == nullptr
            || !MatchLdThisOrAlias(target, thisAlias) || !IsSameField(*f, field))
            return false;
    }
    if (!IL::MatchLeave(body.GetChild(pos + 1), &functionBody))
        return false;
    auto* castclass = dynamic_cast<IL::CastClass*>(value);
    if (castclass == nullptr || castclass->Type == nullptr)
        return false;
    if (!TS::NormalizeTypeVisitor::TypeErasure().EquivalentTypes(
            *castclass->Type, const_cast<TS::IType&>(field.ReturnType())))
        return false;
    auto* call = dynamic_cast<IL::Call*>(castclass->Argument.get());
    if (call == nullptr || !IsDelegateCombineMethod(*call, isAddAccessor)
        || call->Arguments.size() != 2)
        return false;
    return MatchLoadOfField(call->Arguments[0].get(), accessor, field, thisAlias)
        && MatchLdValueParameter(call->Arguments[1].get());
}

} // namespace

const TS::IField* AutoEventDecompiler::FindBackingField(const TS::IEvent& ev) {
    const TS::IModule* module = ev.ParentModule();
    const TS::ITypeDefinition* declaringType = ev.DeclaringTypeDefinition();
    if (module == nullptr || declaringType == nullptr)
        return nullptr;
    const MetadataFile* file = module->MetadataFile();
    if (file == nullptr)
        return nullptr;
    const auto& lookup = file->GetPropertyAndEventBackingFieldLookup();
    for (const TS::IField* field : declaringType->Fields()) {
        if (field == nullptr || field->MetadataToken() == 0)
            continue;
        std::uint32_t eventToken = 0;
        if (!lookup.IsEventBackingField(field->MetadataToken(), eventToken))
            continue;
        if (eventToken != ev.MetadataToken())
            continue;
        return (field->Accessibility() == TS::Accessibility::Private
                && field->IsStatic() == ev.IsStatic())
            ? field
            : nullptr;
    }
    return nullptr;
}

bool AutoEventDecompiler::MatchAutomaticAccessorBody(IL::Block& body,
                                                     IL::BlockContainer& functionBody,
                                                     const TS::IMethod& accessor,
                                                     const TS::IField& field,
                                                     bool isAddAccessor) {
    return MatchCompareExchangeLoop(body, functionBody, accessor, field, isAddAccessor)
        || MatchSimpleCombineAssignment(body, functionBody, accessor, field, isAddAccessor)
        || MatchCompareExchangeLoopMcs(body, functionBody, accessor, field, isAddAccessor);
}

bool AutoEventDecompiler::IsAutomaticAccessor(const MetadataFile& file,
                                              const TS::IMethod& accessor,
                                              const TS::IField& field,
                                              bool isAddAccessor) {
    std::uint32_t token = accessor.MetadataToken();
    auto function = IL::DecompileBodyForAnalysis(file, token, file.GetMethodRVA(token));
    if (function == nullptr)
        return false;
    auto* functionBody = dynamic_cast<IL::BlockContainer*>(function->Body.get());
    if (functionBody == nullptr || functionBody->Blocks.size() != 1)
        return false;
    IL::Block* body = functionBody->EntryPoint();
    if (body == nullptr)
        return false;
    return MatchAutomaticAccessorBody(*body, *functionBody, accessor, field, isAddAccessor);
}

bool AutoEventDecompiler::IsAutomaticEvent(const MetadataFile& file, const TS::IEvent& ev,
                                           const TS::IField*& backingField) {
    backingField = nullptr;
    if (ev.IsExplicitInterfaceImplementation() || ev.DeclaringTypeDefinition() == nullptr)
        return false;
    const TS::IMethod* addAccessor = ev.AddAccessor();
    const TS::IMethod* removeAccessor = ev.RemoveAccessor();
    if (addAccessor == nullptr || !addAccessor->HasBody() || removeAccessor == nullptr
        || !removeAccessor->HasBody())
        return false;
    backingField = FindBackingField(ev);
    if (backingField == nullptr)
        return false;
    if (!TS::NormalizeTypeVisitor::TypeErasure().EquivalentTypes(
            const_cast<TS::IType&>(ev.ReturnType()),
            const_cast<TS::IType&>(backingField->ReturnType())))
        return false;
    return IsAutomaticAccessor(file, *addAccessor, *backingField, true)
        && IsAutomaticAccessor(file, *removeAccessor, *backingField, false);
}

bool AutoEventDecompiler::IsAutomaticEvent(DecompileRun& decompileRun, const MetadataFile& file,
                                           const TS::IEvent& ev,
                                           const TS::IField*& backingField) {
    auto& cache = decompileRun.AutomaticEvents();
    auto it = cache.find(&ev);
    if (it != cache.end()) {
        backingField = it->second;
        return backingField != nullptr;
    }
    const TS::IField* field = nullptr;
    bool result = IsAutomaticEvent(file, ev, field);
    backingField = result ? field : nullptr;
    cache[&ev] = backingField;
    return result;
}

// The C# `internal static void AddFieldLikeEventAttributes(EventDeclaration eventDecl,
// TypeSystemAstBuilder astBuilder, IEvent ev, IField backingField)` -- the field-like
// event's add-accessor and backing-field attributes, as "method:" and "field:"
// sections with the compiler-generated attributes dropped.
void AutoEventDecompiler::AddFieldLikeEventAttributes(
    Syntax::EventDeclaration& eventDecl,
    const Syntax::TypeSystemAstBuilder& astBuilder,
    const TS::IEvent& ev, const TS::IField& backingField) {
    // The C# `ev.AddAccessor!.GetAttributes()`: non-null, guaranteed by the
    // IsAutomaticEvent check the caller performs first.
    const TS::IMethod* addAccessor = ev.AddAccessor();
    assert(addAccessor != nullptr);
    if (addAccessor != nullptr) {
        for (Syntax::AttributeSection* section : astBuilder.ConvertAttributes(
                 WithoutAttributeTypes(addAccessor->GetAttributes(),
                                       kAttributeTypesToRemoveFromAutoEventAccessors,
                                       std::size(kAttributeTypesToRemoveFromAutoEventAccessors)),
                 std::string("method"))) {
            eventDecl.Attributes().Add(section);
        }
    }
    for (Syntax::AttributeSection* section : astBuilder.ConvertAttributes(
             WithoutAttributeTypes(backingField.GetAttributes(),
                                   kAttributeTypesToRemoveFromAutoEventFields,
                                   std::size(kAttributeTypesToRemoveFromAutoEventFields)),
             std::string("field"))) {
        eventDecl.Attributes().Add(section);
    }
}

} // namespace ILSpy::Decompiler::CSharp
