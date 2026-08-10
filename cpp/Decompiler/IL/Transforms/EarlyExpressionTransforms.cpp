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
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdcDecimal.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdcConstants.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
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

// Match an int32 constant (LdcI4), reporting its value. Mirrors the C#
// `ILInstruction.MatchLdcI4(out int)` (a bare match -- the reader does not
// wrap ldc.i4 in conv, so no UnwrapConv unwrap is needed, matching the
// SwitchAnalysis/ExpressionTransforms MatchLdcI precedent).
bool MatchLdcI4(const ILInstruction* inst, std::int32_t& val) {
    if (inst && inst->Op == OpCode::LdcI4) {
        val = static_cast<const LdcI4*>(inst)->Value;
        return true;
    }
    return false;
}

// Match an integer constant (LdcI4 or LdcI8), reporting its value as an int64.
// Mirrors the C# `ILInstruction.MatchLdcI(out long)`: LdcI4 sign-extends the
// int32 to int64; LdcI8 carries the full 64-bit value. The C# additionally
// unwraps SignExtend/ZeroExtend-from-I4 convs; this port's reader does not wrap
// ldc in conv, so the bare LdcI4/LdcI8 cases suffice (the ExpressionTransforms
// MatchLdcI precedent).
bool MatchLdcI(const ILInstruction* inst, std::int64_t& val) {
    if (!inst) return false;
    if (inst->Op == OpCode::LdcI4) {
        val = static_cast<const LdcI4*>(inst)->Value;
        return true;
    }
    if (inst->Op == OpCode::LdcI8) {
        val = static_cast<const LdcI8*>(inst)->Value;
        return true;
    }
    return false;
}

// The KnownTypeCode of a resolved IType, or None when it is not a known type.
// A primitive parameter type (int/uint/long/ulong) decodes to a bare KnownType,
// so the plain dynamic_cast suffices (no ParameterizedType unwrap, unlike the
// NullableLiftingTransform KnownTypeCodeOf which unwraps generic instantiations
// -- a Decimal ctor parameter is never a generic instantiation).
TypeSystem::KnownTypeCode KnownTypeCodeOf(const TypeSystem::IType* type) {
    if (!type) return TypeSystem::KnownTypeCode::None;
    if (auto* kt = dynamic_cast<const TypeSystem::KnownType*>(type))
        return kt->Code();
    return TypeSystem::KnownTypeCode::None;
}

// newobj Decimal(int/uint/long/ulong) or newobj Decimal(int, int, int, bool,
// byte) folds into the corresponding LdcDecimal constant. The C# lives in
// EarlyExpressionTransforms.VisitNewObj (this port's EarlyExpressionTransforms
// is the whole-function equivalent); newobj is modelled as a Call with IsNewObj
// (D76). The 1-arg case dispatches on the first parameter's KnownTypeCode (the
// int/uint/long/ulong overloads share the resolved name `System.Decimal::.ctor`,
// so the parameter type -- carried on Call::ParameterIType -- distinguishes
// them). Returns true and sets `result` on a fold; the Call is replaced by the
// LdcDecimal via ReplaceWith (a clean value-position swap -- a newobj Call is a
// value, never a block final).
bool TransformDecimalCtorToConstant(Call* call, std::unique_ptr<ILInstruction>& result,
                                     ILTransformContext& context) {
    if (!call->IsNewObj) return false;
    if (KnownTypeCodeOf(call->DeclaringType.get()) != TypeSystem::KnownTypeCode::Decimal)
        return false;
    const auto& args = call->Arguments;
    if (args.size() == 1) {
        std::int64_t val = 0;
        if (!MatchLdcI(args[0].get(), val)) return false;
        if (call->ParameterIType.empty()) return false;
        auto paramCode = KnownTypeCodeOf(call->ParameterIType[0].get());
        DecimalValue dv;
        switch (paramCode) {
            case TypeSystem::KnownTypeCode::Int32:
                dv = DecimalValue::FromInt32(static_cast<std::int32_t>(val));
                break;
            case TypeSystem::KnownTypeCode::UInt32:
                dv = DecimalValue::FromUInt32(static_cast<std::uint32_t>(val));
                break;
            case TypeSystem::KnownTypeCode::Int64:
                dv = DecimalValue::FromInt64(val);
                break;
            case TypeSystem::KnownTypeCode::UInt64:
                dv = DecimalValue::FromUInt64(static_cast<std::uint64_t>(val));
                break;
            default:
                return false;
        }
        context.StepOnce("newobj Decimal(int/uint/long/ulong) => ldc.decimal");
        result = std::make_unique<LdcDecimal>(dv);
        return true;
    }
    if (args.size() == 5) {
        std::int32_t lo = 0, mid = 0, hi = 0, isNegative = 0, scale = 0;
        if (!MatchLdcI4(args[0].get(), lo) || !MatchLdcI4(args[1].get(), mid) ||
            !MatchLdcI4(args[2].get(), hi) || !MatchLdcI4(args[3].get(), isNegative) ||
            !MatchLdcI4(args[4].get(), scale))
            return false;
        // The C# `unchecked((byte)scale) <= 28` guard: the scale is a 0..28
        // count of digits right of the point; the byte cast truncates to the low
        // 8 bits (faithful to the C# unchecked cast), and the guard rejects an
        // out-of-range scale (the 5-arg ctor's only validation).
        std::uint8_t sc = static_cast<std::uint8_t>(scale);
        if (sc > 28) return false;
        context.StepOnce("newobj Decimal(int, int, int, bool, byte) => ldc.decimal");
        result = std::make_unique<LdcDecimal>(
            DecimalValue::FromBits(static_cast<std::uint32_t>(lo),
                                   static_cast<std::uint32_t>(mid),
                                   static_cast<std::uint32_t>(hi),
                                   isNegative != 0, sc));
        return true;
    }
    return false;
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

    // newobj Decimal(...) -> ldc.decimal: collect NewObj Calls then fold. A
    // newobj Call is a value (never a block final -- it leaves the constructed
    // object on the stack), so it is always a child of some node; collecting
    // the Call* up front is safe because TransformDecimalCtorToConstant only
    // ReplaceWith'es the matched Call itself (it never touches a sibling), so
    // no other collected Call is destroyed before it is processed -- matching
    // the StObjToStLoc/LdObjToLdLoc collect-then-ReplaceWith precedent.
    std::vector<Call*> newobjCalls;
    WalkAll(function.Body.get(), [&](ILInstruction* inst) {
        if (inst->Op == OpCode::Call) {
            auto* call = static_cast<Call*>(inst);
            if (call->IsNewObj) newobjCalls.push_back(call);
        }
    });
    for (Call* call : newobjCalls) {
        std::unique_ptr<ILInstruction> decimalConstant;
        if (TransformDecimalCtorToConstant(call, decimalConstant, context)) {
            call->ReplaceWith(std::move(decimalConstant));
        }
    }

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
