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

#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Transforms/LocalFunctionDecompiler.hpp"

#include <cassert>
#include <map>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `struct LocalFunctionInfo` (the C# keys the dictionary on the
// MethodDefinitionHandle; the port's reader-built nodes carry only the
// method-name string, so the map keys on the full method name -- the
// "Namespace.Type::<caller>g__fn|n" identity).
struct LocalFunctionInfo {
    // The C# `List<ILInstruction> UseSites`: the call/ldftn/newobj nodes.
    std::vector<ILInstruction*> UseSites;
    // The full method name (the map key repeated; the C# `info.Method`).
    std::string MethodName;
    // The C# `ILFunction Definition` -- the decoded body (owned by the
    // root function's LocalFunctions list; non-owning here). Null when the
    // resolver had no decodable body (the C# `ReadLocalFunctionDefinition`
    // null return, which the C# scope step reports as a warning).
    ILFunction* Definition = nullptr;
};

using LocalFunctionsMap = std::map<std::string, LocalFunctionInfo>;

// The short method name: the last "::" segment of the reader's
// "Namespace.Type::Method" full name (ParseLocalFunctionName consumes the
// short "<caller>g__fn|n" shape).
std::string ShortMethodName(const std::string& fullMethodName) {
    const std::size_t sep = fullMethodName.rfind("::");
    return sep == std::string::npos ? fullMethodName
                                    : fullMethodName.substr(sep + 2);
}

void HandleUseSite(const std::string& methodName, ILInstruction* inst,
                   ILFunction& rootFunction, ILTransformContext& context,
                   LocalFunctionsMap& localFunctions);

// The C# FindUseSites walk: the call/ldftn arms per the C# -- a Call (not a
// newobj) or an LdFtn whose method name parses as a local-function name is a
// use-site; an ldftn inside a delegate construction records the NewObj as
// the use-site instead of the ldftn itself. The deep-decode entry (the C#
// ReadLocalFunctionDefinition through the IL reader with a GenericContext)
// is deferred with the metadata surfaces (the context carries no PEFile /
// reader factory), so first-sightings record the info without a definition
// and are not recursed into.
void FindUseSitesWalk(ILInstruction* inst, ILTransformContext& context,
                      LocalFunctionsMap& localFunctions,
                      ILFunction& rootFunction) {
    if (inst == nullptr) return;
    if (auto* call = dynamic_cast<Call*>(inst)) {
        std::string callerName, functionName;
        if (!call->IsNewObj &&
            LocalFunctionDecompiler::ParseLocalFunctionName(
                ShortMethodName(call->MethodName), callerName,
                functionName)) {
            HandleUseSite(call->MethodName, call, rootFunction, context,
                          localFunctions);
            return;
        }
    } else if (auto* ldftn = dynamic_cast<LdFtn*>(inst)) {
        std::string callerName, functionName;
        if (LocalFunctionDecompiler::ParseLocalFunctionName(
                ShortMethodName(ldftn->MethodName), callerName,
                functionName)) {
            auto* newObj = dynamic_cast<Call*>(ldftn->Parent);
            DelegateConstructionMatch match;
            bool matched = newObj != nullptr &&
                DelegateConstruction::MatchDelegateConstruction(newObj, match);
            if (matched) {
                HandleUseSite(ldftn->MethodName, newObj, rootFunction, context,
                              localFunctions);
            } else {
                HandleUseSite(ldftn->MethodName, ldftn, rootFunction, context,
                              localFunctions);
            }
            return;
        }
    }
    for (int i = 0; i < inst->ChildCount(); i++) {
        FindUseSitesWalk(inst->GetChild(i), context, localFunctions,
                         rootFunction);
    }
}

// The C# HandleUseSite: the first sighting creates the info, reads the
// local-function definition through the context's resolver hook (the C#
// ReadLocalFunctionDefinition; the decoded body embeds into the root
// function's LocalFunctions -- the C# flat embedding, every decoded
// definition lands on context.Function regardless of nesting), and
// recurses the walk into the new body so nested use-sites are found; later
// sightings append the use-site.
void HandleUseSite(const std::string& methodName, ILInstruction* inst,
                   ILFunction& rootFunction, ILTransformContext& context,
                   LocalFunctionsMap& localFunctions) {
    auto it = localFunctions.find(methodName);
    if (it == localFunctions.end()) {
        LocalFunctionInfo info;
        info.UseSites.push_back(inst);
        info.MethodName = methodName;
        if (context.LocalFunctionBodyResolver) {
            std::unique_ptr<ILFunction> definition =
                context.LocalFunctionBodyResolver(methodName);
            if (definition != nullptr) {
                // The C# sets Kind at the ILFunction ctor; the hook returns
                // the function, so the port stamps the kind here (the hook's
                // contract: a non-null return is a local-function body).
                definition->Kind = ILFunctionKind::LocalFunction;
                info.Definition = definition.get();
                rootFunction.LocalFunctions.push_back(std::move(definition));
                // The C# recurses with info.Definition as the walk root; the
                // embedding root stays the top-level function.
                FindUseSitesWalk(info.Definition, context, localFunctions,
                                 rootFunction);
            }
        }
        localFunctions.emplace(methodName, std::move(info));
    } else {
        it->second.UseSites.push_back(inst);
    }
}

// The C# TransformToLocalFunctionReference (the NewObj arm of
// TransformUseSites): the delegate construction's capture target becomes
// ldnull (the delegate construction rewrite consumes the shape later) and a
// Local-typed target variable is retargeted to DisplayClassLocal. The
// ldftn-argument specialization (the C# ReducedMethod.Specialize) is a
// metadata-level rewrite deferred with the reader surface.
void TransformToLocalFunctionReference(Call* useSite, ILTransformContext& context) {
    if (useSite->Arguments.empty()) return;
    std::unique_ptr<ILInstruction> target = useSite->TakeChild(0);
    if (auto* withVar = dynamic_cast<LdLoc*>(target.get())) {
        if (withVar->Variable != nullptr &&
            withVar->Variable->Kind == VariableKind::Local) {
            withVar->Variable->Kind = VariableKind::DisplayClassLocal;
        }
    }
    auto ldnull = std::make_unique<LdNull>();
    ldnull->StartILOffset = target->StartILOffset;
    ldnull->EndILOffset = target->EndILOffset;
    (void)target;
    useSite->SetChild(0, std::move(ldnull));
    context.StepOnce("TransformToLocalFunctionReference");
}

} // namespace

void LocalFunctionDecompiler::Run(ILFunction& function,
                                  ILTransformContext& context) {
    if (!context.Settings.LocalFunctions) return;
    // The C# self-bail guards (IsLocalFunctionMethod /
    // IsLocalFunctionDisplayClass on function.Method) need the method
    // metadata handle the port's ILFunction does not carry; deferred with
    // the reader surface.
    LocalFunctionsMap localFunctions;
    FindUseSitesWalk(&function, context, localFunctions, function);
    // The C# ReplaceReferencesToDisplayClassThis /
    // DetermineCaptureAndDeclarationScopes / PropagateClosureParameterArguments
    // steps need the per-variable use-site lists and the definition bodies
    // (the deep decode); deferred with those surfaces.
    for (auto& [name, info] : localFunctions) {
        (void)name;
        for (ILInstruction* useSite : info.UseSites) {
            auto* newObj = dynamic_cast<Call*>(useSite);
            if (newObj != nullptr && newObj->IsNewObj) {
                TransformToLocalFunctionReference(newObj, context);
            }
            // The call and plain-ldftn arms are metadata-level reshapes
            // (TransformToLocalFunctionInvocation / the LdFtn
            // specialization); deferred with the parameter-metadata surface.
        }
    }
}

bool LocalFunctionDecompiler::ParseLocalFunctionName(const std::string& name,
                                                     std::string& callerName,
                                                     std::string& functionName) {
    callerName.clear();
    functionName.clear();
    // The C# regex `^<(.*)>g__([^\|]*)\|{0,1}\d+(_\d+)?$`; the port parses
    // the same anchored shape by hand (the CodeMappingInfo probe precedent).
    // `(.*)` is greedy, so the caller-name part runs to the LAST `>g__`.
    if (name.size() < 6 || name.front() != '<') return false;
    std::size_t close = std::string::npos;
    std::size_t searchFrom = 1;
    while (true) {
        const std::size_t found = name.find(">g__", searchFrom);
        if (found == std::string::npos) break;
        close = found;
        searchFrom = found + 1;
    }
    if (close == std::string::npos || close + 4 >= name.size()) return false;
    const std::size_t rest = close + 4;
    callerName = name.substr(1, close - 1);
    // `([^\|]*)` -- the function name up to the `|` ordinal suffix (or end).
    std::size_t ordinalStart = rest;
    while (ordinalStart < name.size() && name[ordinalStart] != '|')
        ordinalStart++;
    if (ordinalStart == rest) return false;  // an empty function name
    functionName = name.substr(rest, ordinalStart - rest);
    // `\|{0,1}\d+(_\d+)?$` -- an optional `|` then a decimal ordinal with an
    // optional `_<n>` suffix, running to the end.
    std::size_t pos = ordinalStart;
    if (pos < name.size() && name[pos] == '|') pos++;
    const std::size_t digitsStart = pos;
    while (pos < name.size() && name[pos] >= '0' && name[pos] <= '9') pos++;
    if (pos == digitsStart) return false;  // no ordinal digits
    if (pos < name.size() && name[pos] == '_') {
        pos++;
        const std::size_t suffixStart = pos;
        while (pos < name.size() && name[pos] >= '0' && name[pos] <= '9') pos++;
        if (pos == suffixStart) return false;  // `_<n>` requires digits
    }
    return pos == name.size();
}

} // namespace ILSpy::Decompiler::IL