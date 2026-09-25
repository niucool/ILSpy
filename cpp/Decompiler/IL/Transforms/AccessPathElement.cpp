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

// The AccessPathElement implementation (the header carries the C# mapping
// notes). The walk, the applicability checks, and the equality machinery
// mirror TransformCollectionAndObjectInitializers.cs lines 332-608 verbatim;
// the documented divergences are called out at their sites below.

#include "Decompiler/IL/Transforms/AccessPathElement.hpp"

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/MemberLookup.hpp"
#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/IL/InstructionFlags.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdObjIfRef.hpp"
#include "Decompiler/IL/Instructions/LdStr.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <functional>
#include <memory>

namespace ILSpy::Decompiler::IL {

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace Csx = ::ILSpy::Decompiler::CSharp;

// The C# `ILInstructionMatchComparer.Equals` (lines 593-602): reference
// equality, else BOTH instructions pure and a structural match
// (`x.Match(y).Success`). The port's IL model has no generated
// Match/PerformMatch machinery, so the structural arm is the conservative
// `StructurallyEquals` approximation below -- it covers the pure node kinds
// initializer index expressions actually take (variable loads, the numeric and
// string constants, ldnull) plus the ldobj/field/call composites, and any
// unhandled kind compares UNEQUAL (the ReduceNestingTransform /
// SwitchOnNullableTransform convention: less folding rather than mis-folding).
bool StructurallyEquals(const ILInstruction* a, const ILInstruction* b) {
    if (a == b) return true;
    if (a == nullptr || b == nullptr) return false;
    if (a->Op != b->Op) return false;
    switch (a->Op) {
        case OpCode::LdLoc: {
            auto* la = static_cast<const LdLoc*>(a);
            auto* lb = static_cast<const LdLoc*>(b);
            return la->Variable.get() == lb->Variable.get();
        }
        case OpCode::LdLoca: {
            auto* la = static_cast<const LdLoca*>(a);
            auto* lb = static_cast<const LdLoca*>(b);
            return la->Variable.get() == lb->Variable.get();
        }
        case OpCode::LdcI4:
            return static_cast<const LdcI4*>(a)->Value == static_cast<const LdcI4*>(b)->Value;
        case OpCode::LdcI8:
            return static_cast<const LdcI8*>(a)->Value == static_cast<const LdcI8*>(b)->Value;
        case OpCode::LdcF4:
            return static_cast<const LdcF4*>(a)->Value == static_cast<const LdcF4*>(b)->Value;
        case OpCode::LdcF8:
            return static_cast<const LdcF8*>(a)->Value == static_cast<const LdcF8*>(b)->Value;
        case OpCode::LdStr:
            return static_cast<const LdStr*>(a)->Value == static_cast<const LdStr*>(b)->Value;
        case OpCode::LdNull:
            return true;
        case OpCode::LdObj: {
            // The C# PerformMatch compares the Type operands (reference
            // equality in the port, conservative) and recurses on Target.
            auto* la = static_cast<const LdObj*>(a);
            auto* lb = static_cast<const LdObj*>(b);
            if (la->Type.get() != lb->Type.get()) return false;
            return StructurallyEquals(la->Target.get(), lb->Target.get());
        }
        case OpCode::LdFlda: {
            auto* la = static_cast<const LdFlda*>(a);
            auto* lb = static_cast<const LdFlda*>(b);
            if (la->Field.get() != lb->Field.get()) return false;
            return StructurallyEquals(la->Target.get(), lb->Target.get());
        }
        case OpCode::Call: {
            auto* ca = static_cast<const Call*>(a);
            auto* cb = static_cast<const Call*>(b);
            if (ca->MethodName != cb->MethodName) return false;
            if (ca->Arguments.size() != cb->Arguments.size()) return false;
            for (std::size_t i = 0; i < ca->Arguments.size(); ++i) {
                if (!StructurallyEquals(ca->Arguments[i].get(), cb->Arguments[i].get()))
                    return false;
            }
            return true;
        }
        default:
            // Unhandled kind: compare unequal (conservative -- the caller
            // treats the paths as differing rather than merging).
            return false;
    }
}

// The C# `ILInstructionMatchComparer.Equals` wrapper: the purity gate in front
// of the structural match (a match between impure instructions never holds).
bool InstructionMatchEquals(const ILInstruction* x, const ILInstruction* y) {
    if (x == y) return true;
    if (x == nullptr || y == nullptr) return false;
    return IsPure(x->Flags()) && IsPure(y->Flags()) && StructurallyEquals(x, y);
}

// The C# `v.Descendants.OfType<IInstructionWithVariableOperand>().Any(ld =>
// ld.Variable == target && (ld is LdLoc || ld is LdLoca))` over every value:
// whether any STRICT descendant (Descendants excludes the value itself) loads
// or takes the address of the target variable.
bool DescendantsLoadTarget(const ILInstruction* inst, const ILVariable* target) {
    for (int i = 0; i < inst->ChildCount(); ++i) {
        const ILInstruction* child = inst->GetChild(i);
        if (child == nullptr) continue;
        if (auto* ld = dynamic_cast<const LdLoc*>(child)) {
            if (ld->Variable.get() == target) return true;
        } else if (auto* lda = dynamic_cast<const LdLoca*>(child)) {
            if (lda->Variable.get() == target) return true;
        }
        if (DescendantsLoadTarget(child, target)) return true;
    }
    return false;
}

// The C# `private static bool IsAccessorAccessible(IMethod setter,
// ITypeResolveContext? resolveContext)` (lines 451-459).
bool IsAccessorAccessible(const TS::IMethod& setter,
                          const Csx::Resolver::CSharpResolver* resolver) {
    if (resolver == nullptr)
        return true;
    auto context = resolver->CurrentTypeResolveContext();
    Csx::Resolver::MemberLookup lookup(context->CurrentTypeDefinition(), context->CurrentModule());
    return lookup.IsAccessible(
        setter, setter.DeclaringTypeDefinition() == context->CurrentTypeDefinition());
}

// The C# `private static bool CanBeUsedInInitializer(IProperty property,
// ITypeResolveContext? resolveContext, AccessPathKind kind)` (lines 441-449):
// a property can appear in an initializer path when it is settable with an
// accessible setter, or (for anything but the path's own value store) when the
// kind is not Setter -- i.e. a get-only property is fine as a RECEIVER of an
// Add but never as the store target of a Setter path.
//
// The C# `resolveContext` parameter is the resolver itself (CSharpResolver :
// ICodeContext : ITypeResolveContext); the port's resolver does not derive the
// interface (the documented CSharpResolver shape deviation), so the member
// takes the resolver and reads its context slots -- structurally identical.
bool CanBeUsedInInitializer(const TS::IProperty& property,
                            const Csx::Resolver::CSharpResolver* resolver, AccessPathKind kind) {
    if (property.CanSet()
        && (property.Accessibility() == property.Setter()->Accessibility()
            || IsAccessorAccessible(*property.Setter(), resolver)))
        return true;
    return kind != AccessPathKind::Setter;
}

// The C# `static IType? GetReturnTypeFromInstruction(ILInstruction instruction)`
// (lines 523-543): the type a receiver instruction yields for the Add
// applicability check. The port returns a non-owning pointer; an unresolved
// `Call::Method` / `LdFlda::Field` (null on the port's seed path where the C#
// ILAst always resolves them) answers null so the caller falls back to the root
// type -- the documented graceful degradation for the port's optional member
// handles.
const TS::IType* GetReturnTypeFromInstruction(const ILInstruction* instruction) {
    if (auto* call = dynamic_cast<const Call*>(instruction)) {
        if (call->IsNewObj)
            return nullptr;  // the C# `!(call is CallVirt || call is Call)`
        return call->Method ? &call->Method->ReturnType() : nullptr;
    }
    if (auto* ldobj = dynamic_cast<const LdObj*>(instruction)) {
        if (auto* ldflda = dynamic_cast<const LdFlda*>(ldobj->Target.get()))
            return ldflda->Field ? &ldflda->Field->ReturnType() : nullptr;
        return nullptr;
    }
    if (auto* stobj = dynamic_cast<const StObj*>(instruction)) {
        if (auto* ldflda = dynamic_cast<const LdFlda*>(stobj->Target.get()))
            return ldflda->Field ? &ldflda->Field->ReturnType() : nullptr;
        return nullptr;
    }
    return nullptr;
}

// The C# local function `CanInferTypeArgumentsFromParameters(IMethod method)`
// inside `IsMethodApplicable` (lines 505-521): type inference over the method's
// own parameter list must succeed for a generic Add to be applicable. The
// `new TypeInference(resolver.Compilation)` uses the CSharp4 default algorithm
// and the conversions built over the resolver's compilation (the CallBuilder
// `InferTypeArguments` convention).
bool CanInferTypeArgumentsFromParameters(const TS::IMethod& method,
                                         const Csx::Resolver::CSharpResolver& resolver) {
    if (method.TypeParameters().empty())
        return true;
    // always use unspecialized member, otherwise type inference fails
    const TS::IMethod* definition =
        dynamic_cast<const TS::IMethod*>(method.MemberDefinition());
    if (definition == nullptr) {
        // The C# `(IMethod)method.MemberDefinition` hard cast succeeds over a
        // FakeMethod (the single-object model); the port's two-IMember-subobject
        // hierarchy answers null on the FakeMember view, so the definition is
        // the method itself (the CallBuilder MemberDefinition normalization).
        definition = &method;
    }
    std::vector<std::shared_ptr<Sem::ResolveResult>> arguments;
    std::vector<TS::ITypePtr> parameterTypes;
    arguments.reserve(definition->Parameters().size());
    parameterTypes.reserve(definition->Parameters().size());
    for (const TS::IParameter* p : definition->Parameters()) {
        // The C# `new ResolveResult(p.Type)`.
        arguments.push_back(std::make_shared<Sem::ResolveResult>(
            const_cast<TS::IType&>(p->Type()).shared_from_this()));
        parameterTypes.push_back(const_cast<TS::IType&>(p->Type()).shared_from_this());
    }
    bool success = false;
    Csx::Resolver::Detail::InferTypeArguments(
        resolver.Compilation(), Csx::Resolver::CSharpConversions::Get(resolver.Compilation()),
        definition->TypeParameters(), arguments, parameterTypes, success, std::nullopt,
        Csx::Resolver::TypeInferenceAlgorithm::CSharp4);
    return success;
}

// The C# `static bool IsMethodApplicable(IMethod method,
// IReadOnlyList<ILInstruction> arguments, IType rootType, CSharpResolver
// resolver, DecompilerSettings? settings)` (lines 461-521).
bool IsMethodApplicable(const TS::IMethod& method,
                        const std::vector<std::unique_ptr<ILInstruction>>& arguments,
                        const TS::IType* rootType,
                        const Csx::Resolver::CSharpResolver& resolver,
                        const DecompilerSettings* settings) {
    if (method.IsStatic() && !method.IsExtensionMethod())
        return false;
    if (dynamic_cast<const TS::IProperty*>(method.AccessorOwner()) != nullptr)
        return true;
    if (method.Name() != "Add" || arguments.empty())
        return false;
    if (method.IsExtensionMethod()) {
        // The C# `settings?.ExtensionMethodsInCollectionInitializers == false`.
        if (settings != nullptr && !settings->ExtensionMethodsInCollectionInitializers())
            return false;
        if (!resolver.CanTransformToExtensionMethodCall(method, /*ignoreTypeArguments=*/true))
            return false;
    }
    const TS::IType* targetType = GetReturnTypeFromInstruction(arguments[0].get());
    if (targetType == nullptr)
        targetType = rootType;
    if (targetType == nullptr)
        return false;
    bool implementsEnumerable = false;
    for (const TS::IType* base : TS::GetAllBaseTypes(targetType)) {
        if (TS::IsKnownType(*base, TS::KnownTypeCode::IEnumerable)
            || TS::IsKnownType(*base, TS::KnownTypeCode::IEnumerableOfT)) {
            implementsEnumerable = true;
            break;
        }
    }
    if (!implementsEnumerable)
        return false;
    return CanInferTypeArgumentsFromParameters(method, resolver);
}

// The C# `ld.Variable` read for the `indices.OfType<IInstructionWithVariable
// Operand>()` loop: the variable of a variable-operand instruction (the port's
// LdLoc/LdLoca/StLoc carry the slot the C# interface exposes).
ILVariable* VariableOperandOf(ILInstruction* inst) {
    if (auto* ld = dynamic_cast<LdLoc*>(inst))
        return ld->Variable.get();
    if (auto* lda = dynamic_cast<LdLoca*>(inst))
        return lda->Variable.get();
    if (auto* st = dynamic_cast<StLoc*>(inst))
        return st->Variable.get();
    return nullptr;
}

} // namespace

std::string AccessPathElement::ToString() const {
    // The C# `$"[{Member}, {Indices}]"` interpolates the ARRAY (rendering its
    // type name) and the member (rendering its ToString -- the FullName form in
    // the port, `null` for the never-hit null member).
    std::string result = "[";
    result += Member != nullptr ? Member->FullName() : std::string("null");
    result += ", ";
    if (Indices.has_value())
        result += "ILInstruction[]";
    result += "]";
    return result;
}

AccessPathElement::Info AccessPathElement::GetAccessPath(
    ILInstruction* instruction, const TS::IType* rootType, const DecompilerSettings* settings,
    Csx::Resolver::CSharpResolver* resolver) {
    std::vector<AccessPathElement> path;
    ILVariable* target = nullptr;
    AccessPathKind kind = AccessPathKind::Invalid;
    std::optional<std::vector<ILInstruction*>> values;
    std::vector<ILVariable*> usedIndexVariables;
    ILInstruction* inst = instruction;
    // The C# `goto default` arms set `kind = Invalid; inst = null;` -- the
    // flag defers the kind overwrite to the loop exit so the mid-walk
    // `CanBeUsedInInitializer(..., kind)` reads keep the kind the earlier
    // iterations assigned (exactly what the C# reads at that point).
    bool invalid = false;
    while (inst != nullptr && !invalid) {
        if (auto* call = dynamic_cast<Call*>(inst)) {
            // The C# `if (!(call is CallVirt || call is Call)) goto default;`
            // -- the port's one-Call-node convention: newobj is not a call.
            if (call->IsNewObj) {
                invalid = true;
                break;
            }
            // The port's `Call::Method` is optional (the seed reader leaves it
            // unset); the C# `call.Method` is always resolved. Without a
            // resolved method the path cannot carry a member identity, so the
            // walk is Invalid -- the documented divergence for the port's
            // optional member handles. An empty-arguments call is Invalid for
            // the same reason (the C# `call.Arguments[0]` /
            // `call.Arguments.Last()` reads throw IndexOutOfRange on the
            // malformed shape; the port degrades instead of crashing).
            if (call->Method == nullptr || call->Arguments.empty()) {
                invalid = true;
                break;
            }
            const TS::IMethod& method = *call->Method;
            if (resolver != nullptr
                && !IsMethodApplicable(method, call->Arguments, rootType, *resolver, settings)) {
                invalid = true;
                break;
            }
            ILInstruction* next = call->Arguments[0].get();
            if (auto* ldObjIfRef = dynamic_cast<LdObjIfRef*>(next)) {
                next = ldObjIfRef->Target();
            }
            if (method.IsAccessor()) {
                const TS::IMember* owner = method.AccessorOwner();
                const TS::IProperty* property = dynamic_cast<const TS::IProperty*>(owner);
                if (property != nullptr && !CanBeUsedInInitializer(*property, resolver, kind)) {
                    invalid = true;
                    break;
                }
                bool isGetter = method.AccessorKind() == TS::MethodSemanticsAttributes::Getter;
                // The C# `call.Arguments.Skip(1).Take(Count - (isGetter ? 1 :
                // 2)).ToArray()` -- ALWAYS an array (empty for a plain
                // property accessor). The C# Take with a NEGATIVE count
                // (a malformed accessor arity) throws
                // ArgumentOutOfRangeException; the port clamps to the empty
                // list instead (the walk treats the shape as unusable either
                // way -- the documented divergence).
                std::ptrdiff_t take = static_cast<std::ptrdiff_t>(call->Arguments.size())
                    - (isGetter ? 1 : 2);
                if (take < 0) take = 0;
                std::vector<ILInstruction*> indices;
                indices.reserve(static_cast<std::size_t>(take));
                for (std::size_t i = 1;
                     i < call->Arguments.size() && indices.size() < static_cast<std::size_t>(take);
                     ++i)
                    indices.push_back(call->Arguments[i].get());
                if (!indices.empty() && settings != nullptr
                    && !settings->DictionaryInitializers()) {
                    invalid = true;
                    break;
                }
                // Mark all index variables as used
                for (ILInstruction* index : indices) {
                    ILVariable* variable = VariableOperandOf(index);
                    if (variable != nullptr)
                        usedIndexVariables.push_back(variable);
                }
                path.insert(
                    path.begin(),
                    AccessPathElement(OpCode::Call, owner, std::move(indices)));
            } else {
                path.insert(path.begin(), AccessPathElement(OpCode::Call, &method));
            }
            if (!values.has_value()) {
                if (method.IsAccessor()) {
                    kind = AccessPathKind::Setter;
                    values = std::vector<ILInstruction*>{call->Arguments.back().get()};
                } else {
                    kind = AccessPathKind::Adder;
                    std::vector<ILInstruction*> addValues;
                    for (std::size_t i = 1; i < call->Arguments.size(); ++i)
                        addValues.push_back(call->Arguments[i].get());
                    if (addValues.empty()) {
                        invalid = true;
                        break;
                    }
                    values = std::move(addValues);
                }
            }
            inst = next;
        } else if (auto* ldobj = dynamic_cast<LdObj*>(inst)) {
            auto* ldflda = dynamic_cast<LdFlda*>(ldobj->Target.get());
            // The C# `!ldflda.Field.IsReadOnly` reads the resolved IField's
            // readonly flag; the port reads the node's reader-populated
            // `FieldIsReadOnly` stand-in (the ILInlining::IsReadonlyReference
            // convention) -- a null `Field` cannot form a path element, so the
            // walk is Invalid (the null-Method convention above).
            if (ldflda != nullptr && ldflda->Field != nullptr
                && (kind != AccessPathKind::Setter || !ldflda->FieldIsReadOnly)) {
                path.insert(path.begin(),
                            AccessPathElement(ldobj->Op, ldflda->Field.get()));
                inst = ldflda->Target.get();
            } else {
                invalid = true;
            }
        } else if (auto* ldobjIfRef = dynamic_cast<LdObjIfRef*>(inst)) {
            auto* ldflda = dynamic_cast<LdFlda*>(ldobjIfRef->Target());
            if (ldflda != nullptr && ldflda->Field != nullptr
                && (kind != AccessPathKind::Setter || !ldflda->FieldIsReadOnly)) {
                path.insert(path.begin(),
                            AccessPathElement(ldobjIfRef->Op, ldflda->Field.get()));
                inst = ldflda->Target.get();
            } else if (auto* ldloca = dynamic_cast<LdLoca*>(ldobjIfRef->Target())) {
                target = ldloca->Variable.get();
                inst = nullptr;
            } else {
                invalid = true;
            }
        } else if (auto* stobj = dynamic_cast<StObj*>(inst)) {
            auto* ldflda = dynamic_cast<LdFlda*>(stobj->Target.get());
            if (ldflda != nullptr && ldflda->Field != nullptr) {
                path.insert(path.begin(),
                            AccessPathElement(stobj->Op, ldflda->Field.get()));
                inst = ldflda->Target.get();
                if (!values.has_value()) {
                    values = std::vector<ILInstruction*>{stobj->Value.get()};
                    kind = AccessPathKind::Setter;
                }
            } else {
                invalid = true;
            }
        } else if (auto* ldloc = dynamic_cast<LdLoc*>(inst)) {
            target = ldloc->Variable.get();
            inst = nullptr;
        } else if (auto* ldloca = dynamic_cast<LdLoca*>(inst)) {
            target = ldloca->Variable.get();
            inst = nullptr;
        } else if (auto* ldflda = dynamic_cast<LdFlda*>(inst)) {
            if (ldflda->Field != nullptr) {
                path.insert(path.begin(),
                            AccessPathElement(ldflda->Op, ldflda->Field.get()));
                inst = ldflda->Target.get();
            } else {
                invalid = true;
            }
        } else {
            invalid = true;
        }
    }
    if (invalid)
        kind = AccessPathKind::Invalid;
    if (kind != AccessPathKind::Invalid && values.has_value()) {
        for (const ILInstruction* value : *values) {
            if (DescendantsLoadTarget(value, target)) {
                kind = AccessPathKind::Invalid;
                break;
            }
        }
    }
    return Info{kind, std::move(path), std::move(values), target,
                std::move(usedIndexVariables)};
}

bool AccessPathElement::Equals(const AccessPathElement& other) const {
    // The C# `(other.Member == this.Member || this.Member.Equals(other.Member))`
    // -- both arms are reference equality (the single-arg `Equals` binds
    // `object.Equals`; ISymbol only declares the two-argument overload).
    if (Member != other.Member)
        return false;
    // The C# `(other.Indices == this.Indices || (other.Indices != null &&
    // this.Indices != null && SequenceEqual(...)))`: reference equality first
    // (null vs empty is UNEQUAL), then the per-element match comparer.
    if (Indices.has_value() != other.Indices.has_value())
        return false;
    if (!Indices.has_value())
        return true;
    if (Indices->size() != other.Indices->size())
        return false;
    for (std::size_t i = 0; i < Indices->size(); ++i) {
        if (!InstructionMatchEquals((*Indices)[i], (*other.Indices)[i]))
            return false;
    }
    return true;
}

std::size_t AccessPathElement::GetHashCode() const {
    // The C# `1000000007 * Member.GetHashCode()` (unchecked); the runtime
    // identity hash maps to the pointer hash over the (possibly null) member.
    return static_cast<std::size_t>(1000000007)
        * std::hash<const TS::IMember*>{}(Member);
}

} // namespace ILSpy::Decompiler::IL
