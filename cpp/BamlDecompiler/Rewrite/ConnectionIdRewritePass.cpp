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

// Port of ICSharpCode.BamlDecompiler/Rewrite/ConnectionIdRewritePass.cs (Ki,
// 2015, MIT). The pass pairs the BamlConnectionId annotations the
// ConnectionIdHandler attached with the code-behind members the x:Class
// type's Connect method wires: the method body is read through the IL reader,
// run through the GetILTransforms() pipeline, and its switch over the
// connection ids (or the if-ladder fallback) is matched for field assignments
// (x:Name / x:FieldModifier) and event registrations (the add_ accessor call,
// the AddHandler attached event, and the Style target's EventSetter pattern).
//
// C#-to-C++ porting decisions:
//  * The C# reads the body through `ILReader.ReadIL(token, body,
//    genericContext, ILFunctionKind.TopLevelFunction, token)` and runs
//    `function.RunTransforms(CSharpDecompiler.GetILTransforms(), context)`.
//    The port's ReadIL(file, token, rva) reads through the file itself and
//    the shared IL::RunGetILTransforms runner carries the same list. The
//    generic context matters only for generic code-behind classes (real WPF
//    code behind is never generic) and ILFunctionKind is an
//    invariant-checking knob, so the port passes neither.
//  * The C# MatchStFld/MatchCastClass/MatchLdLoc/MatchLdsFld pattern helpers
//    are ILInstruction extension methods the port reproduces as
//    dynamic_cast chains over the same node shapes: the reader's
//    StObj(LdFlda(target, field), value) / CastClass / LdLoc /
//    LdObj(LdsFlda(field)) / Call(IsNewObj) / LdFtn pair.
//  * The C# resolves the field's IField at ILReader time through the reader
//    module's type system; the port's ILAst keeps the raw field token and the
//    pass resolves it through the main module's ResolveEntity -- the same
//    IField for non-generic code-behind classes. A token that resolves to
//    nothing drops the assignment (the C# would carry a null Field and throw
//    at the render; an unresolvable generated field is unreachable).
//  * `b.FinalInstruction.MatchNop()` -- a fall-through block. The C# Block's
//    FinalInstruction defaults to Nop and the reader replaces it only for a
//    terminal, so the C# fall-through final IS the Nop; the port's reader
//    leaves the final null for the same shape.
//  * The C# `call.Method.Name`/`Parameters.Count`/`DeclaringType.FullName`
//    reads go through the resolved IMethod; the port's Call carries the
//    resolved method-ref string ("DeclaringType::Name") plus the declaring
//    IType and the resolved parameter types, so the name splits after the
//    last "::" and the parameter count is the parameter-type list size. The
//    C# `setEventCall.Method.IsAccessor` guard is subsumed by the exact
//    accessor-name compares (the port carries no accessor bit).
//  * `connectorInterface.GetMethods(m => m.Name == "Connect").SingleOrDefault()`
//    keeps the C# SingleOrDefault semantics: null when empty, the .NET
//    "Sequence contains more than one element." exception on two matches.
//  * The xmlns TODO comment in the C# event-attribute arm (a bare
//    `xmlns + entry.EventName` attribute name) is carried verbatim.
//  * The C# `.First()` throws on an element-less root or a body without a
//    block; the port guards those empty shapes with a plain return (no
//    x:Class to read / no body to match either way).
//  * The C# `new XAttribute(...)`/`new XElement(...)`/`new XComment(...)`
//    arguments are heap allocations the port mirrors with make_shared (the
//    XAttribute move-ctor deletion precedent).

#include "BamlDecompiler/Rewrite/ConnectionIdRewritePass.hpp"

#include "BamlDecompiler/BamlConnectionId.hpp"
#include "BamlDecompiler/Xaml/XamlType.hpp"
#include "BamlDecompiler/Xaml/XamlUtils.hpp"
#include "BamlDecompiler/XamlContext.hpp"
#include "Decompiler/IL/ILReader.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Branch.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/IfInstruction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/SwitchInstruction.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/GenericContext.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/LongSet.hpp"
#include "Decompiler/Xml/XAttribute.hpp"
#include "Decompiler/Xml/XComment.hpp"
#include "Decompiler/Xml/XDocument.hpp"
#include "Decompiler/Xml/XElement.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::BamlDecompiler {

namespace {

using namespace ILSpy::Decompiler;
using Decompiler::TypeSystem::Accessibility;
using Decompiler::TypeSystem::ICompilation;
using Decompiler::TypeSystem::IField;
using Decompiler::TypeSystem::IMethod;
using Decompiler::TypeSystem::ITypeDefinition;
using Decompiler::TypeSystem::KnownTypeCode;
using Decompiler::TypeSystem::TopLevelTypeName;

// The C# `TopLevelTypeName componentConnectorTypeName` /
// `styleConnectorTypeName` static fields.
const TopLevelTypeName kComponentConnectorTypeName("System.Windows.Markup",
                                                   "IComponentConnector");
const TopLevelTypeName kStyleConnectorTypeName("System.Windows.Markup",
                                               "IStyleConnector");

// The .NET SingleOrDefault exception the Connect lookup can throw on two
// matches (the probed .NET message).
const char* kMoreThanOneElementMessage = "Sequence contains more than one element.";

// The C# `System.CodeDom.Compiler.GeneratedCodeAttribute` the InitializeComponent
// and Main arms require.
const char* kGeneratedCodeAttributeReflectionName =
    "System.CodeDom.Compiler.GeneratedCodeAttribute";

// The .NET NullReferenceException message the port's null-Namespace deref and
// null-annotation NRE arms carry (the XamlContext convention).
const char* kNullReferenceMessage =
    "Object reference not set to an instance of an object.";

// The C# `List<(LongSet key, FieldAssignment value)>` /
// `List<(LongSet key, EventRegistration[] value)>` tuples: the port keeps the
// EventRegistration payload (already ported in BamlConnectionId.hpp) and the
// FieldAssignment's resolved IField.
struct FieldAssignmentEntry {
    Util::LongSet Key;
    const IField* Field = nullptr;
};

struct EventMappingEntry {
    Util::LongSet Key;
    std::vector<EventRegistration> Events;
};

// The C# `(List<(LongSet, FieldAssignment)>, List<(LongSet, EventRegistration[])>)`
// DecompileConnections result.
struct Connections {
    std::vector<FieldAssignmentEntry> FieldAssignments;
    std::vector<EventMappingEntry> EventMappings;
};

// The port's Call::MethodName is the resolved "DeclaringType::Name" form; the
// C# `call.Method.Name` is the part after the last "::".
std::string MethodNameOf(const IL::Call* call)
{
    const std::string& fullName = call->MethodName;
    std::size_t sep = fullName.rfind("::");
    if (sep == std::string::npos)
        return fullName;
    return fullName.substr(sep + 2);
}

bool MethodNameIs(const IL::Call* call, const char* name)
{
    return MethodNameOf(call) == name;
}

bool MethodNameStartsWith(const IL::Call* call, const char* prefix)
{
    std::string name = MethodNameOf(call);
    return name.rfind(prefix, 0) == 0;
}

// The C# `type.FullName` over the resolved declaring type (the port's IType
// has no FullName -- ReflectionName is the same string for every simple name
// the matchers compare).
std::string FullNameOf(const TypeSystem::IType* type)
{
    return type ? type->ReflectionName() : std::string();
}

// The XamlType "System.Windows.Style" compare: the C# `type?.TypeNamespace +
// "." + type?.TypeName == "System.Windows.Style"` (a null type yields null
// string parts, whose compare is false either way).
bool XamlTypeIsStyle(const Xaml::XamlType* type)
{
    if (!type)
        return false;
    return type->TypeNamespace + "." + type->TypeName == "System.Windows.Style";
}

// The C# `FieldAssignment.Field` resolution: the raw token through the main
// module's ResolveEntity (see the header note).
const IField* ResolveFieldToken(const ICompilation& typeSystem, std::uint32_t token)
{
    const TypeSystem::IModule& mainModule = typeSystem.MainModule();
    auto* module = dynamic_cast<const TypeSystem::MetadataModule*>(&mainModule);
    if (!module)
        return nullptr;
    try {
        const TypeSystem::IEntity* entity =
            module->ResolveEntity(token, TypeSystem::GenericContext{});
        if (!entity)
            return nullptr;
        return dynamic_cast<const IField*>(entity);
    } catch (const std::exception&) {
        return nullptr;
    }
}

// The field-name strip the C# applies to the static "*Event" fields:
// `eventName.EndsWith("Event") ? Remove(len - 5) : eventName` (the C#
// Remove(int) overload with a length check).
std::string StripEventSuffix(std::string name)
{
    const std::string suffix = "Event";
    if (name.size() >= suffix.size()
        && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
        return name.substr(0, name.size() - suffix.size());
    return name;
}

// The C# `MatchFieldAssignment(ILInstruction inst, out FieldAssignment field)`
// over the port's node shapes; the out param is the raw field token the
// caller resolves.
bool MatchFieldAssignmentToken(const IL::ILInstruction* inst, std::uint32_t* fieldToken)
{
    *fieldToken = 0;
    auto* stobj = dynamic_cast<const IL::StObj*>(inst);
    if (!stobj)
        return false;
    auto* ldflda = dynamic_cast<const IL::LdFlda*>(stobj->Target.get());
    if (!ldflda)
        return false;
    auto* cast = dynamic_cast<const IL::CastClass*>(stobj->Value.get());
    if (!cast)
        return false;
    auto* ldloc = dynamic_cast<const IL::LdLoc*>(cast->Argument.get());
    if (!ldloc || !ldloc->Variable)
        return false;
    // The C# `t.Kind == VariableKind.Parameter && t.Index == 1` -- the
    // second declared parameter (the target). The reader's ILVariable::Index
    // counts the declared params from 0 with `this` at -1 (the gnhf 121
    // alignment), so the target of an instance method is index 1 here too.
    if (ldloc->Variable->Kind != IL::VariableKind::Parameter
        || ldloc->Variable->Index != 1)
        return false;
    *fieldToken = ldflda->FieldToken;
    return true;
}

// The C# `FindField(ILInstruction inst)`; the out param is the raw field
// token.
bool FindFieldToken(const IL::ILInstruction* inst, std::uint32_t* fieldToken)
{
    if (auto* block = dynamic_cast<const IL::Block*>(inst)) {
        if (!block->Instructions.empty())
            return MatchFieldAssignmentToken(block->Instructions.front().get(),
                                              fieldToken);
        return false;
    }
    if (auto* br = dynamic_cast<const IL::Branch*>(inst)) {
        if (!br->TargetBlock)
            return false;
        // The C# recursion hits the Block arm: the target block's first
        // instruction.
        return FindFieldToken(br->TargetBlock, fieldToken);
    }
    return MatchFieldAssignmentToken(inst, fieldToken);
}

// The C# `MatchEventHandlerCreation(ILInstruction inst, out string handlerName)`.
bool MatchEventHandlerCreation(const IL::ILInstruction* inst, std::string* handlerName)
{
    *handlerName = std::string();
    auto* newObj = dynamic_cast<const IL::Call*>(inst);
    if (!newObj || !newObj->IsNewObj || newObj->Arguments.size() != 2)
        return false;
    const IL::ILInstruction* ldftn = newObj->Arguments[1].get();
    if (dynamic_cast<const IL::LdFtn*>(ldftn) == nullptr
        && dynamic_cast<const IL::LdVirtFtn*>(ldftn) == nullptr)
        return false;
    std::string methodName;
    if (auto* ftn = dynamic_cast<const IL::LdFtn*>(ldftn))
        methodName = ftn->MethodName;
    else if (auto* virtFtn = dynamic_cast<const IL::LdVirtFtn*>(ldftn))
        methodName = virtFtn->MethodName;
    std::size_t sep = methodName.rfind("::");
    if (sep != std::string::npos)
        methodName = methodName.substr(sep + 2);
    *handlerName = Xaml::EscapeName(methodName);
    return true;
}

// The C# `IsAddEvent(CallInstruction call, out string eventName, out string handlerName)`.
bool IsAddEvent(const IL::Call* call, std::string* eventName, std::string* handlerName)
{
    *eventName = std::string();
    *handlerName = std::string();
    if (call->Arguments.size() != 2)
        return false;
    if (!MethodNameStartsWith(call, "add_"))
        return false;
    *eventName = MethodNameOf(call).substr(4);
    return MatchEventHandlerCreation(call->Arguments[1].get(), handlerName);
}

// The C# `IsAddAttachedEvent(CallInstruction call, out ...)`.
bool IsAddAttachedEvent(const ICompilation& typeSystem, const IL::Call* call,
                        std::string* eventName, std::string* handlerName)
{
    *eventName = std::string();
    *handlerName = std::string();
    if (call->Arguments.size() != 3)
        return false;
    if (!MethodNameIs(call, "AddHandler") || call->ParameterIType.size() != 2)
        return false;
    auto* ldObj = dynamic_cast<const IL::LdObj*>(call->Arguments[1].get());
    if (!ldObj)
        return false;
    auto* ldsflda = dynamic_cast<const IL::LdsFlda*>(ldObj->Target.get());
    if (!ldsflda)
        return false;
    const IField* field = ResolveFieldToken(typeSystem, ldsflda->FieldToken);
    if (!field)
        return false;
    std::string declaringTypeName;
    if (field->DeclaringType())
        declaringTypeName = field->DeclaringType()->Name();
    *eventName = declaringTypeName + "." + field->Name();
    // The C# `EndsWith("Event") && Length > "Event".Length` guard.
    if (eventName->size() > 5
        && eventName->compare(eventName->size() - 5, 5, "Event") == 0)
        *eventName = eventName->substr(0, eventName->size() - 5);
    return MatchEventHandlerCreation(call->Arguments[2].get(), handlerName);
}

// The C# `MatchSimpleEventRegistration(ILInstruction inst, out EventRegistration)`.
bool MatchSimpleEventRegistration(const ICompilation& typeSystem,
                                 const IL::ILInstruction* inst, EventRegistration* event)
{
    *event = EventRegistration{};
    auto* call = dynamic_cast<const IL::Call*>(inst);
    if (!call || call->IsNewObj)
        return false;
    std::string eventName;
    std::string handlerName;
    if (!IsAddEvent(call, &eventName, &handlerName)
        && !IsAddAttachedEvent(typeSystem, call, &eventName, &handlerName))
        return false;
    *event = EventRegistration{eventName, handlerName};
    return true;
}

// The C# `MatchEventSetterCreation(Block b, ref int pos, out EventRegistration)`.
// The port's fall-through final is the C# Nop default (see the header note).
bool MatchEventSetterCreation(const ICompilation& typeSystem, const IL::Block* block,
                              std::size_t* pos, EventRegistration* event)
{
    *event = EventRegistration{};
    // The C# `b.FinalInstruction.MatchNop()` -- the fall-through shape. The
    // port's reader gives a fall-through block an explicit Branch final (the
    // C# leaves its Nop default and never writes the redundant branch), so
    // the port accepts a null final or a Branch final as the C# Nop; a real
    // `br` to the fall-through is indistinguishable in the port's model (the
    // same documented divergence) and a Leave/Throw final still fails.
    if (block->FinalInstruction != nullptr
        && dynamic_cast<const IL::Branch*>(block->FinalInstruction.get()) == nullptr) {
        *pos = block->Instructions.size();
        return false;
    }
    const auto& instr = block->Instructions;
    if (*pos + 3 >= instr.size())
        return false;
    // stloc v(newobj EventSetter..ctor())
    auto* stloc = dynamic_cast<const IL::StLoc*>(instr[*pos].get());
    if (!stloc || !stloc->Variable)
        return false;
    auto* newObj = dynamic_cast<const IL::Call*>(stloc->Value.get());
    if (!newObj || !newObj->IsNewObj || !newObj->Arguments.empty())
        return false;
    if (!newObj->DeclaringType
        || FullNameOf(newObj->DeclaringType.get()) != "System.Windows.EventSetter")
        return false;
    // callvirt set_Event(ldloc v, ldsfld eventName)
    auto* setEventCall = dynamic_cast<const IL::Call*>(instr[*pos + 1].get());
    if (!setEventCall || setEventCall->Arguments.size() != 2)
        return false;
    auto* eventReceiver = dynamic_cast<const IL::LdLoc*>(setEventCall->Arguments[0].get());
    if (!eventReceiver || eventReceiver->Variable != stloc->Variable)
        return false;
    if (!MethodNameIs(setEventCall, "set_Event"))
        return false;
    auto* eventNameLoad = dynamic_cast<const IL::LdObj*>(setEventCall->Arguments[1].get());
    if (!eventNameLoad)
        return false;
    auto* eventNameField = dynamic_cast<const IL::LdsFlda*>(eventNameLoad->Target.get());
    if (!eventNameField)
        return false;
    const IField* eventField = ResolveFieldToken(typeSystem, eventNameField->FieldToken);
    if (!eventField)
        return false;
    *event = EventRegistration{StripEventSuffix(eventField->Name()), std::string()};
    // callvirt set_Handler(ldloc v, newobj RoutedEventHandler..ctor(...))
    auto* setHandlerCall = dynamic_cast<const IL::Call*>(instr[*pos + 2].get());
    if (!setHandlerCall || setHandlerCall->Arguments.size() != 2)
        return false;
    auto* handlerReceiver =
        dynamic_cast<const IL::LdLoc*>(setHandlerCall->Arguments[0].get());
    if (!handlerReceiver || handlerReceiver->Variable != stloc->Variable)
        return false;
    if (!MethodNameIs(setHandlerCall, "set_Handler"))
        return false;
    std::string handlerName;
    if (!MatchEventHandlerCreation(setHandlerCall->Arguments[1].get(), &handlerName))
        return false;
    event->MethodName = handlerName;
    // callvirt Add(callvirt get_Setters(castclass System.Windows.Style(ldloc target)), ldloc v)
    auto* addCall = dynamic_cast<const IL::Call*>(instr[*pos + 3].get());
    if (!addCall || addCall->Arguments.size() != 2)
        return false;
    if (!MethodNameIs(addCall, "Add"))
        return false;
    auto* getSettersCall = dynamic_cast<const IL::Call*>(addCall->Arguments[0].get());
    if (!getSettersCall || getSettersCall->Arguments.size() != 1)
        return false;
    if (!MethodNameIs(getSettersCall, "get_Setters"))
        return false;
    auto* cast = dynamic_cast<const IL::CastClass*>(getSettersCall->Arguments[0].get());
    if (!cast)
        return false;
    if (FullNameOf(cast->Type.get()) != "System.Windows.Style")
        return false;
    auto* target = dynamic_cast<const IL::LdLoc*>(cast->Argument.get());
    if (!target || !target->Variable
        || target->Variable->Kind != IL::VariableKind::Parameter
        || target->Variable->Index != 1)  // the C# Index == 1; see above
        return false;
    auto* storedValue = dynamic_cast<const IL::LdLoc*>(addCall->Arguments[1].get());
    if (!storedValue || storedValue->Variable != stloc->Variable)
        return false;
    *pos += 4;
    return true;
}

// The C# `FindEvents(ILInstruction inst, List<EventRegistration> events)`.
void FindEvents(const ICompilation& typeSystem, const IL::ILInstruction* inst,
                std::vector<EventRegistration>& events)
{
    EventRegistration event;
    if (auto* block = dynamic_cast<const IL::Block*>(inst)) {
        std::size_t i = 0;
        while (i < block->Instructions.size()) {
            if (MatchEventSetterCreation(typeSystem, block, &i, &event))
                events.push_back(event);
            else
                i++;
        }
        for (const auto& node : block->Instructions) {
            if (MatchSimpleEventRegistration(typeSystem, node.get(), &event))
                events.push_back(event);
        }
        return;
    }
    if (auto* br = dynamic_cast<const IL::Branch*>(inst)) {
        if (br->TargetBlock)
            FindEvents(typeSystem, br->TargetBlock, events);
        return;
    }
    if (MatchSimpleEventRegistration(typeSystem, inst, &event))
        events.push_back(event);
}

// The C# `Add(LongSet ids, ILInstruction inst)`.
void Add(const ICompilation& typeSystem, Connections& connections,
         const Util::LongSet& ids, const IL::ILInstruction* inst)
{
    std::uint32_t fieldToken = 0;
    if (FindFieldToken(inst, &fieldToken)) {
        const IField* field = ResolveFieldToken(typeSystem, fieldToken);
        if (field)
            connections.FieldAssignments.push_back({ids, field});
    }
    std::vector<EventRegistration> events;
    FindEvents(typeSystem, inst, events);
    if (!events.empty())
        connections.EventMappings.push_back({ids, std::move(events)});
}

// The C# recursive walk for `block.Descendants.OfType<T>()` /
// `function.Descendants.OfType<T>()`.
template <typename T, typename F>
void ForEachDescendant(const IL::ILInstruction* inst, F&& callback)
{
    if (!inst)
        return;
    if (auto* match = dynamic_cast<const T*>(inst))
        callback(match);
    int count = inst->ChildCount();
    for (int i = 0; i < count; ++i)
        ForEachDescendant<T>(inst->GetChild(i), callback);
}

// The C# `m.GetAttributes().Any(a => a.AttributeType.ReflectionName ==
// "System.CodeDom.Compiler.GeneratedCodeAttribute")`.
bool HasGeneratedCodeAttribute(const IMethod* m)
{
    for (const TypeSystem::IAttribute* a : m->GetAttributes()) {
        if (a && a->AttributeType().ReflectionName() == kGeneratedCodeAttributeReflectionName)
            return true;
    }
    return false;
}

// The C# `m.DeclaringTypeDefinition.GetNonInterfaceBaseTypes().Any(t =>
// t.ReflectionName == "System.Windows.Application")`.
bool DeclaresApplicationBase(const IMethod* m)
{
    const TypeSystem::ITypeDefinition* declaring = m->DeclaringTypeDefinition();
    if (!declaring)
        return false;
    for (const TypeSystem::IType* base :
         TypeSystem::GetNonInterfaceBaseTypes(declaring)) {
        if (base && base->ReflectionName() == "System.Windows.Application")
            return true;
    }
    return false;
}

// The C# `connectMetadataEntry.RelativeVirtualAddress` read through the main
// module's file.
std::uint32_t ResolveMethodRVA(const XamlContext& ctx, const IMethod* m)
{
    const Metadata::MetadataFile* file = ctx.TypeSystem().MainModule().MetadataFile();
    if (!file)
        return 0;
    return file->GetMethodRVA(m->MetadataToken());
}

// The C# `DecompileConnections(...)` body over one connector interface.
void DecompileConnector(XamlContext& ctx, Connections& connections,
                        const TopLevelTypeName& connectorTypeName,
                        const ITypeDefinition* type)
{
    const ICompilation& typeSystem = ctx.TypeSystem();
    const TypeSystem::ITypePtr connectorType =
        TypeSystem::FindType(typeSystem, TypeSystem::FullTypeName(connectorTypeName));
    const ITypeDefinition* connectorInterface =
        connectorType ? connectorType->GetDefinition() : nullptr;
    if (!connectorInterface)
        return;
    // The C# `GetMethods(m => m.Name == "Connect").SingleOrDefault()`.
    std::vector<const IMethod*> connectMethods;
    for (const IMethod* m : connectorInterface->GetMethods(
             [](const IMethod* method) { return method->Name() == "Connect"; })) {
        if (m && m->Name() == "Connect")
            connectMethods.push_back(m);
    }
    const IMethod* connect = nullptr;
    if (connectMethods.size() > 1)
        throw std::runtime_error(kMoreThanOneElementMessage);
    if (connectMethods.size() == 1)
        connect = connectMethods[0];

    const IMethod* connectMethod = nullptr;
    std::uint32_t connectRVA = 0;
    for (const IMethod* m : type->Methods()) {
        if (!m)
            continue;
        if (!connectMethod) {
            bool implements = false;
            for (const TypeSystem::IMember* md :
                 m->ExplicitlyImplementedInterfaceMembers()) {
                if (md && md->MemberDefinition() == connect) {
                    implements = true;
                    break;
                }
            }
            if (implements) {
                connectMethod = m;
                connectRVA = ResolveMethodRVA(ctx, m);
                continue;
            }
        }
        if (m->Parameters().empty()
            && m->ReturnType().Kind() == TypeSystem::TypeKind::Void && !m->IsStatic()
            && m->Accessibility() == Accessibility::Public
            && m->Name() == "InitializeComponent" && HasGeneratedCodeAttribute(m)) {
            ctx.GeneratedMembers().push_back(m->MetadataToken());
        } else if (m->Parameters().empty()
                   && m->ReturnType().Kind() == TypeSystem::TypeKind::Void
                   && m->IsStatic() && m->Accessibility() == Accessibility::Public
                   && m->Name() == "Main" && DeclaresApplicationBase(m)
                   && HasGeneratedCodeAttribute(m)) {
            ctx.GeneratedMembers().push_back(m->MetadataToken());
        }
    }

    // The C# `type.Fields.FirstOrDefault(f => f.Name == "_contentLoaded" &&
    // f.Type.IsKnownType(KnownTypeCode.Boolean)) is { Accessibility: Private,
    // IsStatic: false }` -- the pattern-match destructure only fires on the
    // found-and-matching shape.
    const IField* contentLoadedField = nullptr;
    for (const IField* f : type->GetFields([](const IField* field) {
             return field && field->Name() == "_contentLoaded";
         })) {
        if (!f)
            continue;
        if (TypeSystem::IsKnownType(f->Type(), KnownTypeCode::Boolean)) {
            contentLoadedField = f;
            break;
        }
    }
    if (contentLoadedField
        && contentLoadedField->Accessibility() == Accessibility::Private
        && !contentLoadedField->IsStatic()) {
        ctx.GeneratedMembers().push_back(contentLoadedField->MetadataToken());
    }

    if (!connectMethod || connectRVA == 0)
        return;
    ctx.GeneratedMembers().push_back(connectMethod->MetadataToken());

    // Read the body through the IL reader and run the GetILTransforms()
    // pipeline (see the header note for the dropped C# arguments).
    const Metadata::MetadataFile* file =
        ctx.TypeSystem().MainModule().MetadataFile();
    if (!file)
        return;
    auto function = IL::ReadIL(*file, connectMethod->MetadataToken(), connectRVA);
    if (!function)
        return;
    IL::ILTransformContext context;
    IL::RunGetILTransforms(*function, context);

    // The C# `function.Body.Children.OfType<Block>().First()`.
    if (function->Body->Blocks.empty())
        return;
    const IL::Block* block = function->Body->Blocks.front().get();

    const IL::SwitchInstruction* ilSwitch = nullptr;
    ForEachDescendant<IL::SwitchInstruction>(block, [&](const IL::SwitchInstruction* sw) {
        if (!ilSwitch)
            ilSwitch = sw;
    });

    if (ilSwitch != nullptr) {
        for (const auto& section : ilSwitch->Sections)
            Add(typeSystem, connections, section->Labels, section->Body.get());
    } else {
        ForEachDescendant<IL::IfInstruction>(
            function.get(), [&](const IL::IfInstruction* ifInst) {
                const IL::Comp* comp =
                    dynamic_cast<const IL::Comp*>(ifInst->Condition.get());
                if (!comp)
                    return;
                if (comp->Kind != IL::ComparisonKind::Inequality
                    && comp->Kind != IL::ComparisonKind::Equality)
                    return;
                auto* right = dynamic_cast<const IL::LdcI4*>(comp->Right.get());
                if (!right)
                    return;
                int id = right->Value;
                const IL::ILInstruction* branch =
                    comp->Kind == IL::ComparisonKind::Inequality
                        ? ifInst->FalseInst.get()
                        : ifInst->TrueInst.get();
                Add(typeSystem, connections,
                    Util::LongSet(static_cast<long long>(id)), branch);
            });
    }
}

// The C# `static void ProcessConnectionIds(XamlContext ctx, XElement element,
// (List<(LongSet, FieldAssignment)>, List<(LongSet, EventRegistration[])>)
// connections)`. The recursion processes every child first, then the
// element's own BamlConnectionId annotations -- the x:Name/event attributes
// and the EventSetter child or unknown-id comment land after the child walk.
void ProcessConnectionIds(XamlContext& ctx, Xml::XElement* element,
                          const Connections& connections)
{
    if (!element)
        return;
    for (const std::shared_ptr<Xml::XElement>& child : element->Elements())
        ProcessConnectionIds(ctx, child.get(), connections);

    for (const std::shared_ptr<BamlConnectionId>* annotation :
         element->Annotations<std::shared_ptr<BamlConnectionId>>()) {
        if (!annotation || !*annotation)
            continue;
        const std::uint32_t id = (*annotation)->Id;
        bool found = false;
        // The C# `fieldAssignments.FindIndex(item => item.key.Contains(annotation.Id))`.
        for (std::size_t i = 0; i < connections.FieldAssignments.size(); ++i) {
            if (!connections.FieldAssignments[i].Key.Contains(
                    static_cast<long long>(id)))
                continue;
            const IField* field = connections.FieldAssignments[i].Field;
            Xml::XName xName = ctx.GetKnownNamespace("Name",
                XamlContext::KnownNamespace_Xaml, element);
            if (!element->Attribute(Xml::XName("Name"))
                && !element->Attribute(xName)) {
                element->Add(
                    std::make_shared<Xml::XAttribute>(xName, field->Name()));
            }
            // x:FieldModifier can only be "public" or "internal" (in C#),
            // where "internal" is the default and thus omitted.
            if (field->Accessibility() == Accessibility::Public) {
                element->Add(std::make_shared<Xml::XAttribute>(
                    ctx.GetKnownNamespace("FieldModifier",
                        XamlContext::KnownNamespace_Xaml, element),
                    "public"));
            }
            ctx.GeneratedMembers().push_back(field->MetadataToken());
            found = true;
            break;
        }
        {
            // The C# `eventMappings.FindIndex(...)` -- an INDEPENDENT check
            // (the C# runs it even when the field arm matched). The
            // per-entry XamlType read (the Style target arm) happens inside
            // the loop.
            for (std::size_t i = 0; i < connections.EventMappings.size(); ++i) {
                if (!connections.EventMappings[i].Key.Contains(
                        static_cast<long long>(id)))
                    continue;
                for (const EventRegistration& entry : connections.EventMappings[i].Events) {
                    const std::shared_ptr<Xaml::XamlType>* typeSlot =
                        element->Annotation<std::shared_ptr<Xaml::XamlType>>();
                    const Xaml::XamlType* type = typeSlot ? typeSlot->get() : nullptr;
                    if (XamlTypeIsStyle(type)) {
                        // The C# `new XElement(type.Namespace + "EventSetter", ...)`
                        // -- a null Namespace derefs inside XNamespace.operator+.
                        if (!type->Namespace().has_value())
                            throw std::runtime_error(kNullReferenceMessage);
                        auto eventSetter = std::make_shared<Xml::XElement>(
                            *type->Namespace() + "EventSetter");
                        eventSetter->Add(std::make_shared<Xml::XAttribute>(
                            Xml::XName("Event"), entry.EventName));
                        eventSetter->Add(std::make_shared<Xml::XAttribute>(
                            Xml::XName("Handler"), entry.MethodName));
                        element->Add(eventSetter);
                    } else {
                        // The C# `new XAttribute(xmlns + entry.EventName, ...)`
                        // with the empty xmlns TODO string.
                        element->Add(std::make_shared<Xml::XAttribute>(
                            Xml::XName(entry.EventName), entry.MethodName));
                    }
                }
                found = true;
                break;
            }
        }
        if (!found) {
            element->Add(std::make_shared<Xml::XComment>(
                "Unknown connection ID: " + std::to_string(id)));
        }
    }
}

} // namespace

// The C# `public void Run(XamlContext ctx, XDocument document)`.
void Rewrite::ConnectionIdRewritePass::Run(XamlContext& ctx, Xml::XDocument& document)
{
    Connections connections;

    // The C# `DecompileConnections(ctx, document)` body: the x:Class lookup
    // over the root's FIRST child element.
    Xml::XElement* root = document.Root();
    if (root) {
        Xml::XElement* firstElement = nullptr;
        for (const std::shared_ptr<Xml::XElement>& child : root->Elements()) {
            firstElement = child.get();
            break;
        }
        if (firstElement) {
            Xml::XAttribute* xClass = firstElement->Attribute(ctx.GetKnownNamespace(
                "Class", XamlContext::KnownNamespace_Xaml, firstElement));
            if (xClass) {
                const TypeSystem::ITypePtr resolved = TypeSystem::FindType(
                    ctx.TypeSystem(), TypeSystem::FullTypeName(xClass->Value()));
                const ITypeDefinition* type =
                    resolved ? resolved->GetDefinition() : nullptr;
                if (type) {
                    DecompileConnector(ctx, connections, kComponentConnectorTypeName,
                                       type);
                    DecompileConnector(ctx, connections, kStyleConnectorTypeName, type);
                }
            }
        }
    }

    ProcessConnectionIds(ctx, root, connections);
}

} // namespace ILSpy::BamlDecompiler
