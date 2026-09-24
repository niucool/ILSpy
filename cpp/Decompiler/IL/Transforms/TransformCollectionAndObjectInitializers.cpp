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

#include "Decompiler/IL/Transforms/TransformCollectionAndObjectInitializers.hpp"

#include "Decompiler/IL/ControlFlow/VariableUsage.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/CastClass.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Transforms/DelegateConstruction.hpp"
#include "Decompiler/IL/Transforms/ILInlining.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <algorithm>
#include <cassert>
#include <memory>
#include <functional>
#include <optional>

namespace ILSpy::Decompiler::IL {

namespace {

// The C# `inst.Descendants` walk (TreeTraversal.PostOrder + `yield return
// this`): every node of the subtree INCLUDING the root itself, visited
// post-order (children before parents).
void ForEachSelfAndDescendant(ILInstruction* inst,
                              const std::function<void(ILInstruction*)>& fn) {
    if (!inst) return;
    for (int i = 0; i < inst->ChildCount(); ++i) {
        ILInstruction* child = inst->GetChild(i);
        if (!child) continue;
        ForEachSelfAndDescendant(child, fn);
        fn(child);
    }
    fn(inst);
}

// The C# `IInstructionWithVariableOperand` checks: whether the instruction is a
// load (LdLoc/LdLoca) of the given variable. The C# pattern
// `ld.Variable == target && (ld is LdLoc || ld is LdLoca)` -- a store does not
// count (the C# filters the operand TYPE to loads).
bool IsLoadOfVariable(ILInstruction* inst, ILVariable* variable) {
    if (auto* ldloc = dynamic_cast<LdLoc*>(inst))
        return ldloc->Variable.get() == variable;
    if (auto* ldloca = dynamic_cast<LdLoca*>(inst))
        return ldloca->Variable.get() == variable;
    return false;
}

// Whether any load of `variable` appears in the subtree rooted at `inst`
// (the C# `inst.Descendants.OfType<IInstructionWithVariableOperand>()
// .Any(ld => ld.Variable == target && (ld is LdLoc || ld is LdLoca))` -- the
// C# Descendants includes the root itself, so a matching root counts).
bool ReferencesVariable(ILInstruction* inst, ILVariable* variable) {
    bool found = false;
    ForEachSelfAndDescendant(inst, [&](ILInstruction* node) {
        if (!found && IsLoadOfVariable(node, variable)) found = true;
    });
    return found;
}

// Walk the function chain up from `inst` to the owning ILFunction (the C#
// `context.Function` handle this port's ILTransformContext does not carry;
// the gnhf-148 precedent of finding the function by walking the parents).
ILFunction* FindOwningFunction(ILInstruction* inst) {
    for (const ILInstruction* p = inst; p != nullptr; p = p->Parent) {
        if (dynamic_cast<const ILFunction*>(p) != nullptr)
            return const_cast<ILFunction*>(static_cast<const ILFunction*>(p));
    }
    return nullptr;
}

// Walk the C# `function.Descendants.OfType<Block>()` -- every Block in the
// function's tree (including nested containers), visited parent-first.
void CollectBlocks(ILInstruction* inst, std::vector<Block*>& blocks) {
    if (!inst) return;
    if (auto* block = dynamic_cast<Block*>(inst)) {
        blocks.push_back(block);
        // Do not recurse into nested blocks through the Block visitor below;
        // the generic walk handles them.
    }
    for (int i = 0; i < inst->ChildCount(); ++i)
        CollectBlocks(inst->GetChild(i), blocks);
    // The block's final instruction is a child slot too (GetChild covers it),
    // so the generic walk reaches it.
}

// The C# `static IType? GetReturnTypeFromInstruction(ILInstruction instruction)`
// (TransformCollectionAndObjectInitializers.cs): the array operand's type for
// the IEnumerable base-type check. The port resolves the call's method return
// type and the ldobj/stobj types; the ldflda field type is not available on
// this port (the LdFlda carries the field identity, not an IField), so a bare
// field load yields null and the caller falls back to the root type.
const TypeSystem::IType* GetReturnTypeFromInstruction(ILInstruction* instruction) {
    if (auto* call = dynamic_cast<Call*>(instruction)) {
        if (call->IsNewObj) return nullptr;  // the C# `!(call is CallVirt || call is Call)` for newobj
        if (!call->Method) return nullptr;
        return &call->Method->ReturnType();
    }
    if (auto* ldobj = dynamic_cast<LdObj*>(instruction)) {
        if (dynamic_cast<LdFlda*>(ldobj->Target.get()) != nullptr)
            return ldobj->Type.get();
        return nullptr;
    }
    if (auto* stobj = dynamic_cast<StObj*>(instruction)) {
        if (dynamic_cast<LdFlda*>(stobj->Target.get()) != nullptr)
            return stobj->Type.get();
        return nullptr;
    }
    return nullptr;
}

// Whether `type` or one of its base types is IEnumerable / IEnumerable<T>
// (the C# `targetType.GetAllBaseTypes().Any(i => i.IsKnownType(...))`).
bool ImplementsIEnumerable(const TypeSystem::IType& type) {
    for (const TypeSystem::IType* base : TypeSystem::GetAllBaseTypes(&type)) {
        if (base == nullptr) continue;
        if (TypeSystem::IsKnownType(*base, TypeSystem::KnownTypeCode::IEnumerable) ||
            TypeSystem::IsKnownType(*base, TypeSystem::KnownTypeCode::IEnumerableOfT))
            return true;
    }
    return false;
}

} // namespace

// The C# `ILInstructionMatchComparer.Equals`: both instructions must be pure
// and structurally equal (the C# `x.Match(y).Success`). The port's structural
// comparison walks both trees: same opcode, same child count, recursively
// matching children, equal LdcI4 values and equal variable operands. Impure
// instructions never match (the C# requires SemanticHelper.IsPure on both).
bool AccessPathInstructionsMatch(const ILInstruction* a, const ILInstruction* b) {
    if (a == b) return a != nullptr;
    if (a == nullptr || b == nullptr) return false;
    if (!IsPure(a->Flags()) || !IsPure(b->Flags())) return false;
    if (a->Op != b->Op) return false;
    if (auto* ldcA = dynamic_cast<const LdcI4*>(a)) {
        auto* ldcB = dynamic_cast<const LdcI4*>(b);
        return ldcB != nullptr && ldcA->Value == ldcB->Value;
    }
    if (auto* ldA = dynamic_cast<const LdLoc*>(a)) {
        auto* ldB = dynamic_cast<const LdLoc*>(b);
        return ldB != nullptr && ldA->Variable == ldB->Variable;
    }
    if (auto* ldaA = dynamic_cast<const LdLoca*>(a)) {
        auto* ldaB = dynamic_cast<const LdLoca*>(b);
        return ldaB != nullptr && ldaA->Variable == ldaB->Variable;
    }
    int childCount = a->ChildCount();
    if (childCount != b->ChildCount()) return false;
    for (int i = 0; i < childCount; ++i) {
        if (!AccessPathInstructionsMatch(a->GetChild(i), b->GetChild(i)))
            return false;
    }
    return true;
}

bool AccessPathElement::Equals(const AccessPathElement& other) const {
    // The C# `(other.Member == this.Member || this.Member.Equals(other.Member))`:
    // reference equality on the member. The port compares by kind: the method
    // / property pointers for call elements, the field identity for the
    // ldflda/stobj elements.
    if (method != nullptr || other.method != nullptr) {
        if (method != other.method) return false;
        if (method == nullptr) return false;  // both null handled below
    }
    if (property != nullptr || other.property != nullptr) {
        if (property != other.property) return false;
    }
    if (method == nullptr && property == nullptr) {
        // The field elements: compare the field identity.
        if (fieldName != other.fieldName || fieldToken != other.fieldToken)
            return false;
    }
    // The C# `(other.Indices == this.Indices || (both non-null &&
    // this.Indices.SequenceEqual(other.Indices, ILInstructionMatchComparer.Instance)))`.
    if (indices.size() != other.indices.size()) return false;
    for (std::size_t i = 0; i < indices.size(); ++i) {
        if (!AccessPathInstructionsMatch(indices[i], other.indices[i]))
            return false;
    }
    return true;
}

bool AccessPathElementLess(const AccessPathElement& a, const AccessPathElement& b) {
    // A total order over the member identity: method pointer, then property
    // pointer, then the field identity, then the index list (by position).
    if (a.method != b.method) return a.method < b.method;
    if (a.property != b.property) return a.property < b.property;
    if (a.fieldName != b.fieldName) return a.fieldName < b.fieldName;
    if (a.fieldToken != b.fieldToken) return a.fieldToken < b.fieldToken;
    if (a.indices.size() != b.indices.size()) return a.indices.size() < b.indices.size();
    for (std::size_t i = 0; i < a.indices.size(); ++i) {
        if (a.indices[i] != b.indices[i]) return a.indices[i] < b.indices[i];
    }
    return false;
}

namespace {

// The C# `static bool IsMethodApplicable(IMethod method, IReadOnlyList<
// ILInstruction> arguments, IType rootType, CSharpResolver resolver,
// DecompilerSettings? settings)`: the resolver-independent subset. The port
// runs it unconditionally where the C# gates it on `resolver != null` (the
// port has no resolver; the header documents the deferred arms: the
// extension-method transformability check and the generic-Add TypeInference
// check -- the port rejects extension methods conservatively and accepts
// generic Add methods without inference).
bool IsMethodApplicablePort(const TypeSystem::IMethod& method,
                            const std::vector<ILInstruction*>& arguments,
                            const TypeSystem::IType& rootType,
                            const ILTransformSettings& settings) {
    (void)settings;
    if (method.IsStatic() && !method.IsExtensionMethod()) return false;
    if (dynamic_cast<const TypeSystem::IProperty*>(method.AccessorOwner()) != nullptr)
        return true;
    if (method.Name() != "Add" || arguments.empty()) return false;
    if (method.IsExtensionMethod()) {
        // The C# `if (settings?.ExtensionMethodsInCollectionInitializers ==
        // false) return false; if (!resolver.CanTransformToExtensionMethodCall(
        // method, ignoreTypeArguments: true)) return false;` -- the resolver
        // check is deferred; extension methods are rejected conservatively.
        return false;
    }
    const TypeSystem::IType* targetType =
        GetReturnTypeFromInstruction(arguments[0]);
    const TypeSystem::IType* root = &rootType;
    const TypeSystem::IType& effective = targetType != nullptr ? *targetType : *root;
    if (!ImplementsIEnumerable(effective)) return false;
    return true;
}

// The C# `AccessPathElement.GetAccessPath` (TransformCollectionAndObjectInitializers.cs):
// decompose the statement into (kind, path, values, target, usedIndexVariables).
// The port takes raw instruction pointers (the tree owns them) and the
// settings; the C# resolver parameter stays null on this port (see the header
// deferral notes: the resolver-independent applicability checks run
// unconditionally below, the resolver-dependent arms are skipped).
AccessPath AccessPathElementGetAccessPath(ILInstruction* instruction,
                                          const TypeSystem::IType& rootType,
                                          const ILTransformSettings& settings) {
    AccessPath result;
    ILInstruction* inst = instruction;
    while (inst != nullptr) {
        if (auto* call = dynamic_cast<Call*>(inst)) {
            // The C# `case CallInstruction call: if (!(call is CallVirt ||
            // call is Call)) goto default;` -- the port's Call covers
            // call/callvirt; the newobj arm falls through to default.
            if (call->IsNewObj) {
                result.kind = AccessPathKind::Invalid;
                inst = nullptr;
                break;
            }
            const TypeSystem::IMethod* method = call->Method.get();
            if (method == nullptr) {
                // The reader-decoded calls with an unresolved method: the C#
                // `method.IsAccessor` on a null method is not reachable; the
                // port treats the unresolvable method as a non-accessor with
                // no member identity (the method pointer stays null).
                std::vector<ILInstruction*> arguments;
                for (auto& arg : call->Arguments) arguments.push_back(arg.get());
                if (arguments.empty()) {
                    result.kind = AccessPathKind::Invalid;
                    inst = nullptr;
                    break;
                }
                inst = arguments[0];
                AccessPathElement element;
                element.op = call->Op;
                result.path.insert(result.path.begin(), std::move(element));
                if (result.values.empty()) {
                    result.kind = AccessPathKind::Adder;
                    result.values.assign(arguments.begin() + 1, arguments.end());
                    if (result.values.empty()) {
                        result.kind = AccessPathKind::Invalid;
                        inst = nullptr;
                        break;
                    }
                }
                continue;
            }
            // The C# `if (resolver != null && !IsMethodApplicable(...)) goto
            // default;` -- the port has no resolver; the resolver-independent
            // subset of IsMethodApplicable runs unconditionally (the header
            // documents the deferral of the resolver-dependent arms).
            std::vector<ILInstruction*> arguments;
            for (auto& arg : call->Arguments) arguments.push_back(arg.get());
            if (!IsMethodApplicablePort(*method, arguments, rootType, settings)) {
                result.kind = AccessPathKind::Invalid;
                inst = nullptr;
                break;
            }
            inst = arguments[0];
            if (method->IsAccessor()) {
                const auto* property =
                    dynamic_cast<const TypeSystem::IProperty*>(method->AccessorOwner());
                // The C# `if (method.AccessorOwner is IProperty property &&
                // !CanBeUsedInInitializer(property, resolver, kind)) goto
                // default;` -- with a null resolveContext the C# check reduces
                // to `property.CanSet || kind != Setter`.
                if (property != nullptr) {
                    const bool canBeUsed =
                        property->CanSet() || result.kind != AccessPathKind::Setter;
                    if (!canBeUsed) {
                        result.kind = AccessPathKind::Invalid;
                        inst = nullptr;
                        break;
                    }
                }
                const bool isGetter =
                    method->AccessorKind() ==
                    TypeSystem::MethodSemanticsAttributes::Getter;
                // The C# `call.Arguments.Skip(1).Take(count - (isGetter ? 1 : 2))`:
                // the indexer arguments between the receiver and the (getter's
                // last / setter's value) tail.
                std::size_t tail = isGetter ? 1 : 2;
                std::vector<ILInstruction*> indices;
                if (arguments.size() > tail) {
                    indices.assign(arguments.begin() + 1,
                                   arguments.end() - static_cast<std::ptrdiff_t>(tail));
                }
                if (!indices.empty() && !settings.DictionaryInitializers) {
                    result.kind = AccessPathKind::Invalid;
                    inst = nullptr;
                    break;
                }
                // The C# `foreach (var index in indices.OfType<
                // IInstructionWithVariableOperand>()) usedIndexVariables.Add(index.Variable);`
                for (ILInstruction* index : indices) {
                    if (auto* ldloc = dynamic_cast<LdLoc*>(index))
                        result.usedIndexVariables.push_back(ldloc->Variable.get());
                    else if (auto* ldloca = dynamic_cast<LdLoca*>(index))
                        result.usedIndexVariables.push_back(ldloca->Variable.get());
                }
                AccessPathElement element;
                element.op = call->Op;
                element.property = property;
                element.indices = std::move(indices);
                result.path.insert(result.path.begin(), std::move(element));
                if (result.values.empty()) {
                    // The C# `kind = Setter; values = { call.Arguments.Last() }`
                    // for the accessor case (the value is the last argument --
                    // for a getter the last INDEX argument; see the C#).
                    result.kind = AccessPathKind::Setter;
                    if (!arguments.empty())
                        result.values.push_back(arguments.back());
                }
            } else {
                AccessPathElement element;
                element.op = call->Op;
                element.method = method;
                result.path.insert(result.path.begin(), std::move(element));
                if (result.values.empty()) {
                    // The C# `kind = Adder; values = new(call.Arguments.Skip(1));
                    // if (values.Count == 0) goto default;`
                    result.kind = AccessPathKind::Adder;
                    result.values.assign(arguments.begin() + 1, arguments.end());
                    if (result.values.empty()) {
                        result.kind = AccessPathKind::Invalid;
                        inst = nullptr;
                        break;
                    }
                }
            }
            continue;
        }
        if (auto* ldobj = dynamic_cast<LdObj*>(inst)) {
            auto* ldflda = dynamic_cast<LdFlda*>(ldobj->Target.get());
            // The C# `(kind != AccessPathKind.Setter || !ldflda.Field.IsReadOnly)`
            // -- the port's LdFlda carries no IField, so the IsReadOnly check
            // is not available and the element is accepted (the header notes
            // the deferral).
            if (ldflda != nullptr) {
                AccessPathElement element;
                element.op = ldobj->Op;
                element.fieldName = ldflda->FieldName;
                element.fieldToken = ldflda->FieldToken;
                result.path.insert(result.path.begin(), std::move(element));
                inst = ldflda->Target.get();
                continue;
            }
            result.kind = AccessPathKind::Invalid;
            inst = nullptr;
            break;
        }
        if (auto* stobj = dynamic_cast<StObj*>(inst)) {
            if (auto* ldflda = dynamic_cast<LdFlda*>(stobj->Target.get())) {
                AccessPathElement element;
                element.op = stobj->Op;
                element.fieldName = ldflda->FieldName;
                element.fieldToken = ldflda->FieldToken;
                result.path.insert(result.path.begin(), std::move(element));
                inst = ldflda->Target.get();
                if (result.values.empty()) {
                    result.values.push_back(stobj->Value.get());
                    result.kind = AccessPathKind::Setter;
                }
                continue;
            }
            result.kind = AccessPathKind::Invalid;
            inst = nullptr;
            break;
        }
        if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
            result.target = ldloc->Variable.get();
            inst = nullptr;
            break;
        }
        if (auto* ldloca = dynamic_cast<LdLoca*>(inst)) {
            result.target = ldloca->Variable.get();
            inst = nullptr;
            break;
        }
        if (auto* ldflda = dynamic_cast<LdFlda*>(inst)) {
            AccessPathElement element;
            element.op = ldflda->Op;
            element.fieldName = ldflda->FieldName;
            element.fieldToken = ldflda->FieldToken;
            result.path.insert(result.path.begin(), std::move(element));
            inst = ldflda->Target.get();
            continue;
        }
        // The C# `default: kind = AccessPathKind.Invalid; inst = null;` -- also
        // the LdObjIfRef case (the node is not ported; see the header).
        result.kind = AccessPathKind::Invalid;
        inst = nullptr;
        break;
    }
    // The C# `if (kind != Invalid && values != null && values.SelectMany(v =>
    // v.Descendants).OfType<IInstructionWithVariableOperand>().Any(ld =>
    // ld.Variable == target && (ld is LdLoc || ld is LdLoca))) kind = Invalid;`
    if (result.kind != AccessPathKind::Invalid && result.target != nullptr) {
        for (ILInstruction* value : result.values) {
            bool found = false;
            ForEachSelfAndDescendant(value, [&](ILInstruction* node) {
                if (!found && IsLoadOfVariable(node, result.target)) found = true;
            });
            if (found) {
                result.kind = AccessPathKind::Invalid;
                break;
            }
        }
    }
    return result;
}

} // namespace
// The C# `static bool TypeContainsInitOnlyProperties(ITypeDefinition? typeDefinition)`
// (TransformCollectionAndObjectInitializers.cs).
bool TransformCollectionAndObjectInitializers::TypeContainsInitOnlyProperties(
    const TypeSystem::ITypeDefinition* typeDefinition) {
    if (typeDefinition == nullptr) return false;
    for (const TypeSystem::IProperty* property : typeDefinition->Properties()) {
        if (property == nullptr) continue;
        const TypeSystem::IMethod* setter = property->Setter();
        if (setter != nullptr && setter->IsInitOnly()) return true;
    }
    return false;
}

// The C# `internal static bool IsRecordCloneMethodCall(CallInstruction ci)`
// (TransformCollectionAndObjectInitializers.cs): a `<Clone>$` call on a
// record type with exactly one argument. The port takes the resolved method
// (the port's calls carry the IMethod through the resolved-Call ctor) and the
// argument count separately.
bool TransformCollectionAndObjectInitializers::IsRecordCloneMethodCall(
    const TypeSystem::IMethod* method, std::size_t argumentCount) {
    if (method == nullptr) return false;
    const TypeSystem::ITypeDefinition* declaring = method->DeclaringTypeDefinition();
    if (declaring == nullptr || !declaring->IsRecord()) return false;
    if (method->Name() != "<Clone>$") return false;
    if (argumentCount != 1) return false;
    return true;
}

// The C# `bool IsMethodCallOnVariable(ILInstruction inst, ILVariable variable)`
// (TransformCollectionAndObjectInitializers.cs): ldloc/ldloca of the variable
// (the C# MatchLdLocRef: an ldloc when the variable is a reference type, an
// ldloca otherwise), the receiver chain of a non-static call, and the target
// chains of ldobj/ldflda/stobj field accesses.
bool TransformCollectionAndObjectInitializers::IsMethodCallOnVariable(
    ILInstruction* inst, ILVariable* variable) {
    if (inst == nullptr || variable == nullptr) return false;
    // The C# `inst.MatchLdLocRef(variable)`: LdLoc when the variable's type is
    // a reference type; LdLoca otherwise (or for a type parameter).
    if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
        if (ldloc->Variable == nullptr) return false;
        const bool isReferenceType =
            ldloc->Variable->Type != nullptr &&
            ldloc->Variable->Type->IsReferenceType() == std::optional<bool>(true);
        if (isReferenceType) return ldloc->Variable.get() == variable;
        return false;
    }
    if (auto* ldloca = dynamic_cast<LdLoca*>(inst)) {
        if (ldloca->Variable == nullptr) return false;
        const std::optional<bool> isReferenceType =
            ldloca->Variable->Type != nullptr
                ? ldloca->Variable->Type->IsReferenceType()
                : std::optional<bool>();
        if (!isReferenceType.has_value() || !isReferenceType.value() ||
            (ldloca->Variable->Type != nullptr &&
             ldloca->Variable->Type->Kind() == TypeSystem::TypeKind::TypeParameter))
            return ldloca->Variable.get() == variable;
        return false;
    }
    if (auto* call = dynamic_cast<Call*>(inst)) {
        // The C# `inst is CallInstruction call && call.Arguments.Count > 0 &&
        // !call.Method.IsStatic` -- the port's Call node covers call/callvirt/
        // newobj (CallInstruction's C++ stand-in); the unresolved-method case
        // cannot answer the IsStatic question and stops.
        if (!call->Arguments.empty() && call->Method != nullptr &&
            !call->Method->IsStatic()) {
            return IsMethodCallOnVariable(call->Arguments[0].get(), variable);
        }
        return false;
    }
    // The C# `inst.MatchLdFld(out var target, out _)`: ldobj over an ldflda
    // (the C# MatchLdFld also unwraps the address-of form; the port checks the
    // plain field chain).
    if (auto* ldobj = dynamic_cast<LdObj*>(inst)) {
        if (auto* ldflda = dynamic_cast<LdFlda*>(ldobj->Target.get()))
            return IsMethodCallOnVariable(ldflda->Target.get(), variable);
        return false;
    }
    // The C# `inst.MatchStFld(out var target, out _, out _)`: stobj over an
    // ldflda; the port recurses into the field target.
    if (auto* stobj = dynamic_cast<StObj*>(inst)) {
        if (auto* ldflda = dynamic_cast<LdFlda*>(stobj->Target.get()))
            return IsMethodCallOnVariable(ldflda->Target.get(), variable);
        return false;
    }
    // The C# `inst.MatchLdFlda(out var target, out _)`.
    if (auto* ldflda = dynamic_cast<LdFlda*>(inst)) {
        return IsMethodCallOnVariable(ldflda->Target.get(), variable);
    }
    return false;
}

// The C# `bool IsValidObjectInitializerTarget(List<AccessPathElement> path)`
// (TransformCollectionAndObjectInitializers.cs).
bool TransformCollectionAndObjectInitializers::IsValidObjectInitializerTarget(
    const std::vector<AccessPathElement>& path) {
    if (path.empty()) return true;
    const AccessPathElement& element = path.back();
    const AccessPathElement* previous =
        path.size() >= 2 ? &path[path.size() - 2] : nullptr;
    // The C# `if (element.Member is not IProperty p) return true;` -- the
    // port's property slot is only set for the accessor elements (a field
    // element is not an IProperty).
    if (element.property == nullptr) return true;
    if (!element.property->IsIndexer()) return true;
    if (previous != nullptr) {
        // The C# `NormalizeTypeVisitor.IgnoreNullabilityAndTuples
        // .EquivalentTypes(previous.Member.ReturnType,
        // element.Member.DeclaringType)` -- the member's return type is only
        // available for the method/property elements; a field element has no
        // IType on this port (the header documents the deferral), so the
        // nested-field indexer case aborts conservatively.
        if (previous->method != nullptr) {
            const TypeSystem::IType& previousType = previous->method->ReturnType();
            const TypeSystem::IType& elementDeclaring =
                *element.property->DeclaringType();
            return TypeSystem::NormalizeTypeVisitor::TypeErasure().EquivalentTypes(
                const_cast<TypeSystem::IType&>(previousType),
                const_cast<TypeSystem::IType&>(elementDeclaring));
        }
        return false;
    }
    return false;
}

// The C# local function `void MarkUsedIndices()` inside IsPartOfInitializer
// (the C# closure reads the `usedIndexVariables` list from the enclosing
// GetAccessPath call; the port passes it in):
//   foreach (var index in usedIndices)
//     if (possibleIndexVariables.TryGetValue(index, out var item))
//         possibleIndexVariables[index] = (-1, item.Value);
void TransformCollectionAndObjectInitializers::MarkUsedIndices(
    const std::vector<ILVariable*>& usedIndices) {
    for (ILVariable* index : usedIndices) {
        auto it = possibleIndexVariables.find(index);
        if (it != possibleIndexVariables.end())
            it->second.first = -1;
    }
}

bool TransformCollectionAndObjectInitializers::IsPartOfInitializer(
    Block& block, int pos, ILVariable* target, const TypeSystem::IType& rootType,
    BlockKind& blockKind, bool& initializerContainsInitOnlyItems,
    const ILTransformSettings& settings) {
    // The C# `while (pos + initializerItemsCount + 1 < block.Instructions.Count
    // && IsPartOfInitializer(block.Instructions, pos + initializerItemsCount + 1, ...))`
    // passes the instruction index; the port receives the block and index.
    ILInstruction* inst = block.Instructions[static_cast<std::size_t>(pos)].get();
    // The C# `Include any stores to local variables that are single-assigned
    // and do not reference the initializer-variable in the list of possible
    // index variables.` -- the StLoc arm of IsPartOfInitializer.
    if (auto* stloc = dynamic_cast<StLoc*>(inst)) {
        if (stloc->Variable != nullptr &&
            stloc->Variable->Kind == VariableKind::Local &&
            stloc->Variable->IsSingleDefinition()) {
            if (!settings.DictionaryInitializers) return false;
            // The C# `stloc.Value.Descendants...Any(ld => ld.Variable == target
            // && (ld is LdLoc || ld is LdLoca))` -- a reference aborts.
            if (ReferencesVariable(stloc->Value.get(), target)) return false;
            possibleIndexVariables[stloc->Variable.get()] = {
                stloc->ChildIndex, stloc->Value.get()};
            return true;
        }
    }
    auto result = AccessPathElementGetAccessPath(inst, rootType, settings);
    if (result.kind == AccessPathKind::Invalid || target != result.target)
        return false;
    // The C# `var lastElement = newPath.Last(); newPath.RemoveLast();` -- the
    // last element is treated separately from the path prefix.
    if (result.path.empty()) {
        // The C# `newPath.Last()` on an empty path cannot happen for a valid
        // kind (every accepted shape inserts at least one element); guard the
        // port against the empty path aborting the walk.
        return false;
    }
    AccessPathElement lastElement = result.path.back();
    result.path.pop_back();
    // The C# path-comparison state (currentPath / pathStack / isCollection are
    // transform members shared across the scan iterations).
    std::size_t minLen = std::min(currentPath.size(), result.path.size());
    std::size_t firstDifferenceIndex = 0;
    while (firstDifferenceIndex < minLen &&
           result.path[firstDifferenceIndex] == currentPath[firstDifferenceIndex])
        firstDifferenceIndex++;
    while (currentPath.size() > firstDifferenceIndex) {
        isCollection = false;
        currentPath.pop_back();
        pathStack.pop_back();
    }
    while (currentPath.size() < result.path.size()) {
        AccessPathElement newElement = result.path[currentPath.size()];
        currentPath.push_back(newElement);
        if (isCollection ||
            !pathStack.back().insert(newElement).second)
            return false;
        pathStack.emplace_back(AccessPathElementLess);
    }
    switch (result.kind) {
        case AccessPathKind::Adder:
            isCollection = true;
            if (!pathStack.back().empty()) return false;
            MarkUsedIndices(result.usedIndexVariables);
            return true;
        case AccessPathKind::Setter: {
            if (isCollection || !pathStack.back().insert(lastElement).second)
                return false;
            if (result.values.size() != 1 ||
                !IsValidObjectInitializerTarget(currentPath))
                return false;
            if (blockKind != BlockKind::ObjectInitializer &&
                blockKind != BlockKind::WithInitializer)
                blockKind = BlockKind::ObjectInitializer;
            // The C# `initializerContainsInitOnlyItems |= lastElement.Member
            // is IProperty { Setter.IsInitOnly: true };` -- the property slot
            // is only set for the accessor elements.
            if (lastElement.property != nullptr) {
                const TypeSystem::IMethod* setter = lastElement.property->Setter();
                if (setter != nullptr && setter->IsInitOnly())
                    initializerContainsInitOnlyItems = true;
            }
            MarkUsedIndices(result.usedIndexVariables);
            return true;
        }
        default:
            return false;
    }
}

// The loads (LdLoc only -- the C# DoPropagate iterates v.LoadInstructions,
// and an ldloca is an AddressInstruction, not a LoadInstruction) of `variable`
// anywhere in the subtree, as raw pointers into the live tree.
void CollectLoadsOfVariable(ILInstruction* inst, ILVariable* variable,
                            std::vector<ILInstruction*>& loads) {
    if (!inst) return;
    if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
        if (ldloc->Variable.get() == variable) loads.push_back(inst);
        return;  // LdLoc has no instruction children
    }
    for (int i = 0; i < inst->ChildCount(); ++i)
        CollectLoadsOfVariable(inst->GetChild(i), variable, loads);
}

// Replace every LdLoc/LdLoca of `v` in the subtree (including the root, the
// C# Descendants) with `replacement`. The C# mutates
// `load.Variable = finalSlot` on the live tree; the port reassigns the
// shared_ptr variable slot.
void RewriteVariableLoads(ILInstruction* inst, ILVariable* v,
                          const ILVariablePtr& replacement) {
    ForEachSelfAndDescendant(inst, [&](ILInstruction* node) {
        if (auto* ldloc = dynamic_cast<LdLoc*>(node)) {
            if (ldloc->Variable.get() == v) ldloc->Variable = replacement;
        } else if (auto* ldloca = dynamic_cast<LdLoca*>(node)) {
            if (ldloca->Variable.get() == v) ldloca->Variable = replacement;
        }
    });
}

// The C# `CopyPropagation.Propagate(stLocStack, context)` (CopyPropagation.cs):
// the port's subset for the ldloca-source case the C# gate matches
// (`block.Instructions[pos + 1] is StLoc { Variable: { Kind: StackSlot,
// IsSingleDefinition: true }, Value: LdLoca ldLoca } && ldLoca.Variable == v`).
// The C# DoPropagate first un-inlines the copied expression's child arguments
// (a no-op for an ldloca, which has no instruction children) and then clones
// the value per load, removes the store, and runs InlineInto. The port
// mirrors that: replace every load of the stack slot with a fresh clone of
// the ldloca, remove the store, and run the inliner at the freed slot until
// it stops making progress (the C# InlineInto loop).
void TransformCollectionAndObjectInitializersPropagateStLoc(
    ILFunction* function, StLoc* store, ILTransformContext& context) {
    ILVariable* v = store->Variable.get();
    ILInstruction* copiedExpr = store->Value.get();
    auto* block = dynamic_cast<Block*>(store->Parent);
    if (block == nullptr || v == nullptr || copiedExpr == nullptr ||
        function == nullptr)
        return;
    int storeIndex = store->ChildIndex;
    if (storeIndex < 0 ||
        storeIndex >= static_cast<int>(block->Instructions.size()))
        return;
    std::vector<ILInstruction*> loads;
    CollectLoadsOfVariable(function->Body.get(), v, loads);
    for (ILInstruction* load : loads) {
        if (load->Parent == nullptr) continue;
        auto clone = copiedExpr->Clone();
        load->ReplaceWith(std::move(clone));
    }
    block->RemoveInstructionAt(static_cast<std::size_t>(storeIndex));
    // The C# `int c = ILInlining.InlineInto(block, i, InliningOptions.None,
    // context: context);` -- the port's InlineOneIfPossible is the public
    // single-shot form; loop it to match InlineInto's repetition.
    while (InlineOneIfPossible(block, storeIndex, context)) {
    }
}

void TransformCollectionAndObjectInitializers::Run(
    Block& block, int pos, StatementTransformContext& context) {
    // The C# `if (!context.Settings.ObjectOrCollectionInitializers) return;`.
    if (!context.Base.Settings.ObjectOrCollectionInitializers) return;
    ILInstruction* inst = block.Instructions[static_cast<std::size_t>(pos)].get();
    // The C# `if (!inst.MatchStLoc(out var v, out var initInst) || v.Kind !=
    // VariableKind.Local && v.Kind != VariableKind.StackSlot) return;` -- the
    // C# binds `&&` tighter than `||`.
    auto* stloc = dynamic_cast<StLoc*>(inst);
    if (stloc == nullptr || stloc->Variable == nullptr) return;
    ILVariable* v = stloc->Variable.get();
    if (v->Kind != VariableKind::Local && v->Kind != VariableKind::StackSlot)
        return;
    ILInstruction* initInst = stloc->Value.get();
    // The C# `var blockKind = BlockKind.CollectionInitializer;`.
    BlockKind blockKind = BlockKind::CollectionInitializer;
    // The C# `var insertionPos = initInst.ChildIndex; var siblings =
    // initInst.Parent!.Children;` -- the init expression sits in the stloc's
    // value child slot and the initializer block replaces it there (the
    // port's stloc Value is child 0, so the tail of Run uses SetChild(0, ...)).
    ILFunction* function = FindOwningFunction(&block);
    if (function == nullptr) return;
    // The C# `IMethod currentMethod = context.Function.Method!;` -- the
    // port's ILFunction carries the constructor flag only (the header notes
    // the deferral of the IsCompilerGeneratedOrIsInCompilerGeneratedClass
    // check that would consult the method handle).
    const bool currentMethodIsConstructor = function->IsConstructor;
    TypeSystem::ITypePtr instType;
    TypeSystem::ITypePtr targetType;
    // The C# `we allow a castclass instruction to wrap the init instruction:
    // this is needed, for example, for inherited record types used on .NET
    // runtimes (e.g., .NET 4.x), where covariant return types are not
    // supported.` -- the MatchCastClass result survives the switch via the
    // `targetType != null` check after it.
    if (auto* cast = dynamic_cast<CastClass*>(initInst)) {
        initInst = cast->Argument.get();
        targetType = cast->Type;
    }
    bool handled = false;
    if (auto* newObj = dynamic_cast<Call*>(initInst);
        newObj != nullptr && newObj->IsNewObj) {
        // The C# `case NewObj newObjInst:` arm. The port models the C# NewObj
        // node as a Call with IsNewObj (the Call.hpp precedent).
        if (newObj->ILStackWasEmpty && v->Kind == VariableKind::Local &&
            !TypeContainsInitOnlyProperties(
                newObj->Method != nullptr
                    ? newObj->Method->DeclaringTypeDefinition()
                    : nullptr) &&
            !currentMethodIsConstructor) {
            // The C# also bails on
            // `currentMethod.IsCompilerGeneratedOrIsInCompilerGeneratedClass()`
            // -- deferred with the missing method handle (the header): the
            // port treats the method as non-compiler-generated.
            //
            // On statement level (no other expressions on IL stack), prefer
            // to keep local variables (but not stack slots), unless we are in
            // a constructor (where inlining object initializers might be
            // critical for the base ctor call) or a compiler-generated
            // delegate method, which might be used in a query expression.
            return;
        }
        // Do not try to transform delegate construction.
        // DelegateConstruction transform cannot deal with this.
        DelegateConstructionMatch match;
        if (DelegateConstruction::MatchDelegateConstruction(newObj, match)) {
            return;
        }
        // The C# `TransformDisplayClassUsage.IsPotentialClosure(context,
        // newObjInst)` -- deferred with the missing decompiled-type handle
        // (the header): the check evaluates false on this port.
        // The C# `Cannot build a collection/object initializer attached to an
        // AnonymousTypeCreateExpression anon = new { A = 5 } { 3,4,5 } is
        // invalid syntax.` (ContainsAnonymousType) and the
        // `TupleTransform.MatchTupleConstruction` gate are deferred with
        // their type-system surfaces (the header); no anonymous types or
        // tuple constructions arise from this port's reader-decoded calls.
        if (newObj->Method == nullptr) return;
        instType = newObj->Method->DeclaringType();
        handled = true;
    }
    if (!handled) {
        if (auto* defaultVal = dynamic_cast<DefaultValue*>(initInst)) {
            instType = defaultVal->Type;
            handled = true;
        } else if (auto* call = dynamic_cast<Call*>(initInst);
                   call != nullptr && !call->IsNewObj &&
                   call->Method != nullptr &&
                   call->Method->FullName() == "System.Activator.CreateInstance" &&
                   call->Method->TypeArguments().size() == 1) {
            // The C# `case Call c when c.Method.FullNameIs("System.Activator",
            // "CreateInstance") && c.Method.TypeArguments.Count == 1:`.
            if (!context.Base.Settings.UseObjectCreationOfGenericTypeParameter)
                return;
            instType = call->Method->TypeArguments().front();
            blockKind = BlockKind::ObjectInitializer;
            handled = true;
        } else if (auto* cloneCall = dynamic_cast<Call*>(initInst);
                   cloneCall != nullptr && !cloneCall->IsNewObj &&
                   context.Base.Settings.WithExpressions &&
                   IsRecordCloneMethodCall(cloneCall->Method.get(),
                                           cloneCall->Arguments.size())) {
            // The C# `case CallInstruction ci when
            // context.Settings.WithExpressions && IsRecordCloneMethodCall(ci):`
            // -- the with-initializer arm.
            if (cloneCall->Method == nullptr) return;
            instType = cloneCall->Method->DeclaringType();
            blockKind = BlockKind::WithInitializer;
            initInst = cloneCall->Arguments.front().get();
            handled = true;
        }
    }
    if (!handled) {
        // The C# `default:` arm -- the with-initializer record-struct case.
        const TypeSystem::ITypeDefinition* typeDef =
            v->Type != nullptr ? v->Type->GetDefinition() : nullptr;
        if (context.Base.Settings.WithExpressions && typeDef != nullptr &&
            typeDef->IsReferenceType() == false && typeDef->IsRecord()) {
            instType = v->Type;
            blockKind = BlockKind::WithInitializer;
        } else {
            return;
        }
    }
    if (targetType != nullptr) {
        instType = targetType;
    }
    if (instType == nullptr) return;
    // The C# `Copy-propagate stack slot holding an 'ldloca' of the variable`:
    // `if (pos < block.Instructions.Count && block.Instructions[pos + 1] is
    // StLoc {...} stLocStack && ldLoca.Variable == v) CopyPropagation.Propagate(
    // stLocStack, context);` -- the IStatementTransform invariant (the last
    // instruction always has EndPointUnreachable) keeps pos + 1 in bounds
    // whenever the instruction AT pos is a stloc (the C# relies on the same
    // bound-free access).
    if (pos + 1 < static_cast<int>(block.Instructions.size())) {
        if (auto* stLocStack = dynamic_cast<StLoc*>(
                block.Instructions[static_cast<std::size_t>(pos) + 1].get());
            stLocStack != nullptr && stLocStack->Variable != nullptr &&
            stLocStack->Variable->Kind == VariableKind::StackSlot &&
            stLocStack->Variable->IsSingleDefinition()) {
            if (auto* ldLoca = dynamic_cast<LdLoca*>(stLocStack->Value.get());
                ldLoca != nullptr && ldLoca->Variable.get() == v) {
                TransformCollectionAndObjectInitializersPropagateStLoc(
                    function, stLocStack, context.Base);
            }
        }
    }
    int initializerItemsCount = 0;
    bool initializerContainsInitOnlyItems = false;
    possibleIndexVariables.clear();
    currentPath.clear();
    isCollection = false;
    pathStack.clear();
    pathStack.emplace_back(AccessPathElementLess);
    // Detect initializer type by scanning the following statements:
    // each must be a callvirt with ldloc v as first argument;
    // if the method is a setter we're dealing with an object initializer;
    // if the method is named Add and has at least 2 arguments we're dealing
    // with a collection/dictionary initializer.
    while (pos + initializerItemsCount + 1 <
               static_cast<int>(block.Instructions.size()) &&
           IsPartOfInitializer(block, pos + initializerItemsCount + 1, v,
                               *instType, blockKind,
                               initializerContainsInitOnlyItems,
                               context.Base.Settings)) {
        initializerItemsCount++;
    }
    // Do not convert the statements into an initializer if there's an
    // incompatible usage of the initializer variable directly after the
    // possible initializer. The IStatementTransform invariant (the last
    // instruction always has EndPointUnreachable) keeps pos + count + 1 in
    // bounds -- the C# relies on the same bound-free access.
    if (!initializerContainsInitOnlyItems &&
        IsMethodCallOnVariable(
            block.Instructions[static_cast<std::size_t>(
                                   pos + initializerItemsCount + 1)]
                .get(),
            v))
        return;
    // Calculate the correct number of statements inside the initializer:
    // All index variables that were used in the initializer have Index set to
    // -1. We fetch the first unused variable from the list and remove all
    // instructions after its first usage (i.e. the init store) from the
    // initializer.
    int firstUnusedIndex = -1;
    for (const auto& entry : possibleIndexVariables) {
        if (entry.second.first > -1 &&
            (firstUnusedIndex == -1 || entry.second.first < firstUnusedIndex))
            firstUnusedIndex = entry.second.first;
    }
    if (firstUnusedIndex != -1) {
        initializerItemsCount = firstUnusedIndex - pos - 1;
    }
    // The initializer would be empty, there's nothing to do here.
    if (initializerItemsCount <= 0) return;
    context.Base.StepOnce("CollectionOrObjectInitializer");
    // Create a new block and final slot (initializer target variable).
    auto initializerBlock = std::make_unique<Block>();
    initializerBlock->Kind = blockKind;
    ILVariablePtr finalSlot =
        function->RegisterVariable(VariableKind::InitializerTarget, instType);
    initializerBlock->SetFinal(std::make_unique<LdLoc>(finalSlot));
    initializerBlock->Add(std::make_unique<StLoc>(
        finalSlot, stloc->TakeChild(0)));
    // Move all instructions to the initializer block. The C# walks
    // `block.Instructions[i + pos]` and re-parents the matching statements
    // (the Call/CallVirt/Call arms) or drops the non-matching ones with the
    // trailing RemoveRange; the port mirrors the walk and erases the whole
    // consumed range afterwards.
    for (int i = 1; i <= initializerItemsCount; ++i) {
        std::size_t idx = static_cast<std::size_t>(pos + i);
        ILInstruction* moved = block.Instructions[idx].get();
        bool moveIt = false;
        if (auto* call = dynamic_cast<Call*>(moved)) {
            // The C# `case CallInstruction call: if (!(call is CallVirt ||
            // call is Call)) continue;` -- the port's Call node covers
            // call/callvirt; the newobj arm (IsNewObj) is skipped like the
            // C# skips the non-call CallInstructions.
            if (call->IsNewObj) continue;
            moveIt = true;
            // The C# `var newTarget = newCall.Arguments[0]; foreach (var load
            // in newTarget.Descendants.OfType<IInstructionWithVariableOperand>())
            // if ((load is LdLoc || load is LdLoca) && load.Variable == v)
            // load.Variable = finalSlot;` -- the C# Descendants includes the
            // root (the receiver load itself).
            RewriteVariableLoads(call->Arguments[0].get(), v, finalSlot);
        } else if (auto* stObj = dynamic_cast<StObj*>(moved)) {
            moveIt = true;
            RewriteVariableLoads(stObj->Target.get(), v, finalSlot);
        } else if (dynamic_cast<StLoc*>(moved) != nullptr) {
            moveIt = true;
        }
        if (!moveIt) continue;
        auto owned = std::move(block.Instructions[idx]);
        initializerBlock->Add(std::move(owned));
    }
    block.Instructions.erase(
        block.Instructions.begin() + pos + 1,
        block.Instructions.begin() + pos + 1 + initializerItemsCount);
    block.RenumberChildren();
    // The C# `siblings[insertionPos] = initializerBlock;` -- the initializer
    // block replaces the init expression in the stloc's value slot.
    stloc->SetChild(0, std::move(initializerBlock));
    // The C# ILVariable maintains its LoadInstructions/StoreCount lists on
    // every tree mutation, so by the time InlineIfPossible runs the stack
    // slot's counts reflect the post-move tree (the initializer's own loads
    // removed with the consumed statements). The port's counts are static,
    // so they are recomputed here (the same recompute
    // TransformArrayInitializers performs before its inliner call).
    ComputeVariableUsage(*function);
    // The C# `ILInlining.InlineIfPossible(block, pos, context);` -- the
    // port's InlineOneIfPossible (see the TransformArrayInitializers port).
    InlineOneIfPossible(&block, pos, context.Base);
}

} // namespace ILSpy::Decompiler::IL
