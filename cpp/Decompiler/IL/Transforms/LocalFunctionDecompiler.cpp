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
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/LocalFunctionDecompiler.hpp"

#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>

// The type-system namespace alias (the LookupStubs/TypeSystemExtensions
// consumers' convention).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

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

// The C# reads the per-variable use-site lists off ILVariable (the C#
// `v.StoreInstructions` / `v.AddressInstructions` -- the C# maintains them
// incrementally); the port collects them during the walk (the TDU
// AnalysisState convention).
struct LocalFunctionUseLists {
    std::map<ILVariable*, std::vector<StLoc*>> storesByVariable;
    std::map<ILVariable*, std::vector<LdLoc*>> loadsByVariable;
    std::map<ILVariable*, std::vector<LdLoca*>> addressesByVariable;
};

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
                   LocalFunctionsMap& localFunctions,
                   LocalFunctionUseLists& useLists);

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
                      ILFunction& rootFunction,
                      LocalFunctionUseLists& useLists) {
    if (inst == nullptr) return;
    if (auto* stloc = dynamic_cast<StLoc*>(inst)) {
        if (stloc->Variable != nullptr) {
            useLists.storesByVariable[stloc->Variable.get()].push_back(stloc);
        }
    } else if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
        if (ldloc->Variable != nullptr) {
            useLists.loadsByVariable[ldloc->Variable.get()].push_back(ldloc);
        }
    } else if (auto* ldloca = dynamic_cast<LdLoca*>(inst)) {
        if (ldloca->Variable != nullptr) {
            useLists.addressesByVariable[ldloca->Variable.get()].push_back(
                ldloca);
        }
    }
    // The C# walk is `function.Body.Descendants` -- it does NOT skip the
    // use-site's children: the argument loads/addresses inside a use-site
    // call feed the capture walk, so the arms record and fall through to the
    // generic traversal.
    if (auto* call = dynamic_cast<Call*>(inst)) {
        std::string callerName, functionName;
        if (!call->IsNewObj &&
            LocalFunctionDecompiler::ParseLocalFunctionName(
                ShortMethodName(call->MethodName), callerName,
                functionName)) {
            HandleUseSite(call->MethodName, call, rootFunction, context,
                          localFunctions, useLists);
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
                              localFunctions, useLists);
            } else {
                HandleUseSite(ldftn->MethodName, ldftn, rootFunction, context,
                              localFunctions, useLists);
            }
            return;
        }
    }
    for (int i = 0; i < inst->ChildCount(); i++) {
        FindUseSitesWalk(inst->GetChild(i), context, localFunctions,
                         rootFunction, useLists);
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
                   LocalFunctionsMap& localFunctions,
                   LocalFunctionUseLists& useLists) {
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
                                 rootFunction, useLists);
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

// The C# `ILFunction GetDeclaringFunction(ILFunction localFunction)`: the
// innermost ILFunction ancestor of the declaration scope (the C# walks
// inst.Parent up; the port's Parent chain is the same shape).
ILFunction* GetDeclaringFunctionOfScope(ILFunction* localFunction) {
    if (localFunction == nullptr || localFunction->DeclarationScope == nullptr)
        return nullptr;
    ILInstruction* inst = localFunction->DeclarationScope;
    while (inst != nullptr) {
        if (auto* declaring = dynamic_cast<ILFunction*>(inst))
            return declaring;
        inst = inst->Parent;
    }
    return nullptr;
}

// The C# `static T FindCommonAncestorInstruction<T>(ILInstruction a,
// ILInstruction b)` specialized to BlockContainer: the first container on
// a's ancestor chain that is also on b's (the C# builds a HashSet of b's
// ancestors; the port walks a's chain and re-walks b's per step -- the
// chains are nesting-depth sized).
BlockContainer* FindCommonAncestorBlockContainer(BlockContainer* a,
                                                 BlockContainer* b) {
    for (ILInstruction* anc = a; anc != nullptr; anc = anc->Parent) {
        if (dynamic_cast<BlockContainer*>(anc) == nullptr) continue;
        for (ILInstruction* other = b; other != nullptr;
             other = other->Parent) {
            if (anc == other) return dynamic_cast<BlockContainer*>(anc);
        }
    }
    return nullptr;
}

// The C# `bool IsInNestedLocalFunction(BlockContainer declarationScope,
// ILFunction function)`: whether the declaration scope sits inside any
// function reachable through the pre-order over LocalFunctions.
bool IsInNestedLocalFunction(BlockContainer* declarationScope,
                             ILFunction* function) {
    if (function == nullptr) return false;
    if (function->Body != nullptr) {
        // The C# `declarationScope.IsDescendantOf(f.Body)`: walk the scope's
        // ancestors for the body container.
        for (ILInstruction* anc = declarationScope; anc != nullptr;
             anc = anc->Parent) {
            if (anc == function->Body.get()) return true;
        }
    }
    for (const std::unique_ptr<ILFunction>& nested : function->LocalFunctions) {
        if (IsInNestedLocalFunction(declarationScope, nested.get())) return true;
    }
    return false;
}

// The C# `ILVariable ResolveAncestorScopeReference(ILInstruction inst)`:
// resolve a display-class field reference to the variable whose closure it
// is (the C# checks IsPotentialClosure + IsClosure over the field's type
// and the context's functions' variables). The port's field-access nodes
// carry the field name but not the resolved IField, and the C#
// `IsClosure(context, v, ...)` probe needs the closure-init shape -- the
// arm is deferred with those surfaces.
ILVariable* ResolveAncestorScopeReference(ILInstruction* inst,
                                          ILTransformContext& context) {
    (void)inst;
    (void)context;
    return nullptr;
}

// The C# `bool DetermineCaptureAndDeclarationScope(LocalFunctionInfo info,
// int parameterIndex, ILInstruction arg)`: the per-argument capture walk.
// Returns false when the argument does not contribute a capture (the C#
// caller stops at the first false).
bool DetermineCaptureAndDeclarationScope(
    LocalFunctionInfo& info, int parameterIndex, ILInstruction* arg,
    ILTransformContext& context, const LocalFunctionUseLists& useLists,
    ILFunction& rootFunction) {
    ILFunction* function = info.Definition;
    if (function == nullptr) return false;
    ILVariable* closureVar = nullptr;
    if (parameterIndex >= 0) {
        // The C# `IsClosureParameter(function.Method.Parameters[parameterIndex],
        // resolveContext)`: the definition's Parameters list (the port's
        // ILFunction::Parameters pre-resolved subset) carries the closure
        // parameters; the current-type anchor comes from the context.
        if (parameterIndex >= static_cast<int>(function->Parameters.size()))
            return false;
        const TS::IParameter* parameter =
            function->Parameters[static_cast<std::size_t>(parameterIndex)].get();
        if (!TS::IsClosureParameter(parameter, context.CurrentTypeDefinition))
            return false;
    }
    // The C# `arg.MatchLdLoc(out closureVar) || arg.MatchLdLoca(out closureVar)`.
    if (auto* ldloc = dynamic_cast<LdLoc*>(arg)) {
        closureVar = ldloc->Variable.get();
    } else if (auto* ldloca = dynamic_cast<LdLoca*>(arg)) {
        closureVar = ldloca->Variable.get();
    } else {
        closureVar = ResolveAncestorScopeReference(arg, context);
        if (closureVar == nullptr) return false;
    }
    if (closureVar->Kind == VariableKind::NamedArgument) return false;
    // The C# GetClosureInitializer: for a struct-kind captured variable the
    // containing statement of the first (by IL offset) address use; else the
    // first store. The port's use lists are walk-collected (pre-order); a
    // single-use fixture is the common shape, and the address list is
    // ordered by the walk (the C# sorts by StartILOffset -- the port keeps
    // the walk order, matching the recorded single first use).
    ILInstruction* initializer = nullptr;
    {
        // The C# `variable.Type.UnwrapByRef().GetDefinition()`.
        const TS::IType* type = closureVar->Type.get();
        const TS::IType* unwrapped =
            type != nullptr ? &TS::UnwrapByRef(*type) : nullptr;
        const TS::ITypeDefinition* definition =
            unwrapped != nullptr ? unwrapped->GetDefinition() : nullptr;
        if (definition == nullptr) return false;
        if (closureVar->Kind == VariableKind::Parameter) return false;
        if (definition->Kind() == TS::TypeKind::Struct) {
            auto it = useLists.addressesByVariable.find(closureVar);
            if (it == useLists.addressesByVariable.end() ||
                it->second.empty())
                return false;
            LdLoca* firstAddress = it->second.front();
            initializer = Block::GetContainingStatement(firstAddress);
        } else {
            auto it = useLists.storesByVariable.find(closureVar);
            if (it == useLists.storesByVariable.end() ||
                it->second.empty())
                return false;
            initializer = it->second.front();
        }
    }
    if (initializer == nullptr) return false;
    // The C# `BlockContainer.FindClosestContainer(initializer)`.
    BlockContainer* additionalScope =
        BlockContainer::FindClosestContainer(initializer);
    if (additionalScope == nullptr) return false;
    // Combine the capture scope.
    if (closureVar->CaptureScope == nullptr) {
        closureVar->CaptureScope = additionalScope;
    } else {
        closureVar->CaptureScope = FindCommonAncestorBlockContainer(
            closureVar->CaptureScope, additionalScope);
    }
    if (closureVar->Kind == VariableKind::Local) {
        closureVar->Kind = VariableKind::DisplayClassLocal;
    }
    if (function->DeclarationScope == nullptr) {
        function->DeclarationScope = closureVar->CaptureScope;
    } else {
        // The C# `!IsInNestedLocalFunction(function.DeclarationScope,
        // closureVar.CaptureScope.Ancestors.OfType<ILFunction>().First())`:
        // the container's enclosing function; the port walks the scope's
        // parent chain for the owning ILFunction.
        ILFunction* scopeFunction = nullptr;
        for (ILInstruction* anc = closureVar->CaptureScope; anc != nullptr;
             anc = anc->Parent) {
            if (auto* f = dynamic_cast<ILFunction*>(anc)) {
                scopeFunction = f;
                break;
            }
        }
        if (scopeFunction == nullptr ||
            !IsInNestedLocalFunction(function->DeclarationScope,
                                     scopeFunction)) {
            function->DeclarationScope = FindCommonAncestorBlockContainer(
                function->DeclarationScope, closureVar->CaptureScope);
        }
    }
    (void)rootFunction;
    return true;
}

// The C# `void DetermineCaptureAndDeclarationScope(LocalFunctionInfo info,
// ILInstruction useSite)`: the per-use-site dispatch (the call arm walks the
// arguments back-to-front; a delegate-construction use-site is skipped --
// the capture scope was already handled when analyzing `this`).
void DetermineCaptureAndDeclarationScope(
    LocalFunctionInfo& info, ILInstruction* useSite,
    ILTransformContext& context, const LocalFunctionUseLists& useLists,
    ILFunction& rootFunction) {
    auto* call = dynamic_cast<Call*>(useSite);
    if (call == nullptr) return;
    DelegateConstructionMatch match;
    if (call->IsNewObj &&
        DelegateConstruction::MatchDelegateConstruction(call, match)) {
        // The C# `case CallInstruction call: if delegate construction -> break`.
        return;
    }
    const int firstArgumentIndex = info.Definition != nullptr &&
                                           info.Definition->IsStatic
                                       ? 0
                                       : 1;
    for (int i = static_cast<int>(call->Arguments.size()) - 1;
         i >= firstArgumentIndex; i--) {
        if (!DetermineCaptureAndDeclarationScope(
                info, i - firstArgumentIndex,
                call->Arguments[static_cast<std::size_t>(i)].get(), context,
                useLists, rootFunction)) {
            break;
        }
    }
    if (firstArgumentIndex > 0 && !call->Arguments.empty()) {
        DetermineCaptureAndDeclarationScope(info, -1,
                                            call->Arguments[0].get(), context,
                                            useLists, rootFunction);
    }
}

// The C# `void DetermineCaptureAndDeclarationScopes(...Values localFunctions)`:
// per info with a decoded definition, walk the use-sites, then default the
// declaration scope to the root function's body. The C# TryValidateSkipCount
// check (the generic skip-count invariant) and the move-between-functions
// bookkeeping ride on the definition's Method handle -- deferred with the
// metadata surface; the GetDeclaringFunction move lands with it.
void DetermineCaptureAndDeclarationScopes(
    LocalFunctionsMap& localFunctions, ILTransformContext& context,
    const LocalFunctionUseLists& useLists, ILFunction& rootFunction) {
    for (auto& [name, info] : localFunctions) {
        (void)name;
        if (info.Definition == nullptr) {
            // The C# `context.Function.Warnings.Add("Could not decode local
            // function ...")` -- the port carries no warnings list; the
            // missing-definition case is silent here.
            continue;
        }
        context.StepOnce(
            ("Determine and move to declaration scope of " +
             info.Definition->Name)
                .c_str());
        for (ILInstruction* useSite : info.UseSites) {
            DetermineCaptureAndDeclarationScope(info, useSite, context,
                                                useLists, rootFunction);
        }
        if (info.Definition->DeclarationScope == nullptr) {
            info.Definition->DeclarationScope = rootFunction.Body.get();
        }
        // The C# `ILFunction declaringFunction = GetDeclaringFunction(...);
        // if (declaringFunction != context.Function) { move }`: a definition
        // whose declaration scope sits inside a nested function moves there
        // (the port embeds everything flat on the root first).
        ILFunction* declaringFunction =
            GetDeclaringFunctionOfScope(info.Definition);
        if (declaringFunction != nullptr &&
            declaringFunction != &rootFunction) {
            auto owned = std::find_if(
                rootFunction.LocalFunctions.begin(),
                rootFunction.LocalFunctions.end(),
                [definition = info.Definition](
                    const std::unique_ptr<ILFunction>& entry) {
                    return entry.get() == definition;
                });
            if (owned != rootFunction.LocalFunctions.end()) {
                declaringFunction->LocalFunctions.push_back(
                    std::move(*owned));
                rootFunction.LocalFunctions.erase(owned);
            }
        }
    }
}

void LocalFunctionDecompiler::Run(ILFunction& function,
                                  ILTransformContext& context) {
    if (!context.Settings.LocalFunctions) return;
    // The C# self-bail guards (IsLocalFunctionMethod /
    // IsLocalFunctionDisplayClass on function.Method) need the method
    // metadata handle the port's ILFunction does not carry; deferred with
    // the reader surface.
    LocalFunctionsMap localFunctions;
    LocalFunctionUseLists useLists;
    FindUseSitesWalk(&function, context, localFunctions, function, useLists);
    DetermineCaptureAndDeclarationScopes(localFunctions, context, useLists,
                                         function);
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