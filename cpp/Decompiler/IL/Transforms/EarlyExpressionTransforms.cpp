// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
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

#include "Decompiler/IL/Transforms/EarlyExpressionTransforms.hpp"
#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace {

using TypeSystem::ITypePtr;
using TypeSystem::TypeKind;

void WalkAll(ILInstruction* inst, const std::function<void(ILInstruction*)>& visit) {
    if (!inst) return;
    visit(inst);
    for (int i = 0; i < inst->ChildCount(); ++i) WalkAll(inst->GetChild(i), visit);
}

// Approximation of TypeUtils.IsCompatibleTypeForMemoryAccess (TypeSystem/TypeUtils.cs).
// Two types are memory-compatible if they are equal, both reference types, both
// integer types of equal size (same StackType), or either is unknown. Skipped vs
// the C#: NormalizeTypeVisitor.TypeErasure (nullable/modifier stripping) -- this
// port does not yet carry those wrappers, so the raw types are compared directly.
bool IsCompatibleTypeForMemoryAccess(const ITypePtr& memoryType, const ITypePtr& accessType) {
    if (!memoryType || !accessType) return true;  // unknown -> assume compatible
    if (memoryType->ReflectionName() == accessType->ReflectionName()) return true;
    TypeKind k1 = memoryType->Kind();
    TypeKind k2 = accessType->Kind();
    if (k1 == TypeKind::Unknown || k2 == TypeKind::Unknown) return true;
    auto IsRef = [](TypeKind k) {
        return k == TypeKind::Class || k == TypeKind::Interface ||
               k == TypeKind::Delegate || k == TypeKind::Array ||
               k == TypeKind::Dynamic || k == TypeKind::Null;
    };
    if (IsRef(k1) && IsRef(k2)) return true;
    StackType ms = StackTypeOf(memoryType);
    StackType as = StackTypeOf(accessType);
    auto IsInteger = [](StackType s) {
        return s == StackType::I4 || s == StackType::I || s == StackType::I8;
    };
    if (ms == as && IsInteger(ms)) return true;
    return false;
}

bool MatchLdNull(ILInstruction* inst) {
    return inst && inst->Op == OpCode::LdNull;
}

// stobj(ldloca V, value) -> stloc V, value. A store through a local's address
// becomes a direct local store so ILInlining can fold the value. Returns true on
// a rewrite (the StObj is destroyed by ReplaceWith).
bool StObjToStLoc(StObj* st, ILTransformContext& context) {
    if (!st->Target || st->Target->Op != OpCode::LdLoca) return false;
    auto* ldloca = static_cast<LdLoca*>(st->Target.get());
    if (!ldloca->Variable) return false;
    if (!IsCompatibleTypeForMemoryAccess(ldloca->Variable->Type, st->Type)) return false;
    context.StepOnce("stobj(ldloca V, ...) => stloc V, ...");
    auto value = st->TakeChild(1);  // detach Value before replacing the StObj
    auto stloc = std::make_unique<StLoc>(ldloca->Variable, std::move(value));
    st->ReplaceWith(std::move(stloc));
    return true;
}

// ldobj(ldloca V, type) -> ldloc V. The load-side analog: a typed load through a
// local's address becomes a direct local load so ILInlining can fold it.
bool LdObjToLdLoc(LdObj* ld, ILTransformContext& context) {
    if (!ld->Target || ld->Target->Op != OpCode::LdLoca) return false;
    auto* ldloca = static_cast<LdLoca*>(ld->Target.get());
    if (!ldloca->Variable) return false;
    if (!IsCompatibleTypeForMemoryAccess(ldloca->Variable->Type, ld->Type)) return false;
    context.StepOnce("ldobj(ldloca V) => ldloc V");
    auto ldloc = std::make_unique<LdLoc>(ldloca->Variable);
    ld->ReplaceWith(std::move(ldloc));
    return true;
}

// Normalize a comparison against ldnull, and unwrap `box T(arg) ==/!= ldnull` to
// `arg ==/!= ldnull` when T is a type parameter (a boxed generic-default null
// check is really a check on the underlying value). Mutates the Comp in place;
// the box rewrite replaces comp->Left (destroying the Box).
void FixComparisonKindLdNull(Comp* comp, ILTransformContext& context) {
    // The C# guards on inst.IsLifted (a nullable-lifted comparison); this port
    // does not model nullable lifting, so no Comp is ever lifted -- the guard is
    // dropped.
    if (MatchLdNull(comp->Right.get())) {
        if (comp->Kind == ComparisonKind::GreaterThan) {
            context.StepOnce("comp(left > ldnull) => comp(left != ldnull)");
            comp->Kind = ComparisonKind::Inequality;
        } else if (comp->Kind == ComparisonKind::LessThanOrEqual) {
            context.StepOnce("comp(left <= ldnull) => comp(left == ldnull)");
            comp->Kind = ComparisonKind::Equality;
        }
    } else if (MatchLdNull(comp->Left.get())) {
        if (comp->Kind == ComparisonKind::LessThan) {
            context.StepOnce("comp(ldnull < right) => comp(ldnull != right)");
            comp->Kind = ComparisonKind::Inequality;
        } else if (comp->Kind == ComparisonKind::GreaterThanOrEqual) {
            context.StepOnce("comp(ldnull >= right) => comp(ldnull == right)");
            comp->Kind = ComparisonKind::Equality;
        }
    }

    // box T(arg) ==/!= ldnull  ->  arg ==/!= ldnull  (T a type parameter).
    // A boxed generic-default null check is really a check on the underlying
    // value: the box is null iff its argument was null.
    if (MatchLdNull(comp->Right.get()) && comp->Left &&
        comp->Left->Op == OpCode::Box) {
        auto* box = static_cast<Box*>(comp->Left.get());
        if (box->Type && box->Type->Kind() == TypeKind::TypeParameter &&
            box->Argument) {
            if (comp->Kind == ComparisonKind::Equality ||
                comp->Kind == ComparisonKind::Inequality) {
                context.StepOnce("comp(box T(..) ==/!= ldnull) -> comp(.. ==/!= ldnull)");
                // Detach the Box's Argument, then replace comp->Left with it.
                // TakeChild(0) orphans the Argument; SetChild(0, ...) destroys
                // the Box (now a childless shell) and wires the Argument in.
                auto arg = box->TakeChild(0);
                comp->SetChild(0, std::move(arg));
            }
        }
    }
}

} // namespace

void EarlyExpressionTransforms::Run(ILFunction& function, ILTransformContext& context) {
    // StObj/LdObj rewrites first: collect then convert. StObjs do not nest
    // (a StObj yields Void, so it cannot be another node's child); a matched
    // LdObj (ldobj(ldloca V)) has no LdObj inside its Target (ldloca V is a
    // leaf), so collecting both up front is safe -- no collected pointer dangles
    // before it is processed.
    std::vector<StObj*> stobjs;
    std::vector<LdObj*> ldobjs;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        if (inst->Op == OpCode::StObj) stobjs.push_back(static_cast<StObj*>(inst));
        else if (inst->Op == OpCode::LdObj) ldobjs.push_back(static_cast<LdObj*>(inst));
    });
    for (StObj* st : stobjs) StObjToStLoc(st, context);
    for (LdObj* ld : ldobjs) LdObjToLdLoc(ld, context);

    // Comps last: collect then fix. FixComparisonKindLdNull never replaces a
    // Comp (it mutates Kind, and the box rewrite replaces comp->Left, destroying
    // the Box -- never a Comp), so the collected Comp* pointers stay valid.
    std::vector<Comp*> comps;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        if (inst->Op == OpCode::Comp) comps.push_back(static_cast<Comp*>(inst));
    });
    for (Comp* comp : comps) FixComparisonKindLdNull(comp, context);
}

} // namespace ILSpy::Decompiler::IL
