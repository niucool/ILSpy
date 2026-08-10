// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Out-of-line implementation of NumericCompoundAssign.IsBinaryCompatibleWithType
// (the C# static validator on NumericCompoundAssign). See
// CompoundAssignmentInstruction.hpp for the node and the validator contract.

#include "Decompiler/IL/Instructions/CompoundAssignmentInstruction.hpp"

#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/IL/Transforms/NullableLiftingTransform.hpp"
#include "Decompiler/IL/Transforms/TransformAssignment.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/Sign.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <string_view>

namespace ILSpy::Decompiler::IL {

// The short method name (the part after "::") of a Call's resolved display
// name. The IL reader resolves a call's MethodName to "Namespace.Type::Member";
// the operator-name checks (IsIncrementOrDecrement / IsStringConcat) consult
// this short part. Mirrors the NullableLiftingTransform::ShortMethodName helper.
static std::string_view ShortMethodName(std::string_view fullName) {
    auto pos = fullName.rfind("::");
    return pos != std::string_view::npos ? fullName.substr(pos + 2) : fullName;
}

bool NumericCompoundAssign::IsBinaryCompatibleWithType(const BinaryNumericInstruction* binary,
                                                       const TypeSystem::IType* type,
                                                       const ILTransformSettings* settings) {
    // The effective store type: a lifted binary operates on a Nullable<T>, so the
    // store type must be Nullable<T> and the rest of the checks operate on the
    // underlying type. Faithful to the C# `type = NullableType.GetUnderlyingType(type)`.
    const TypeSystem::IType* effectiveType = type;
    if (binary->IsLifted) {
        // NullableType.IsNullable(type): the store type must be a Nullable<T>.
        // GetUnderlyingTypeOfNullable returns the type argument T (non-null) for
        // a Nullable<T> and nullptr otherwise -- the faithful equivalent of
        // `NullableType.IsNullable(type)` + `NullableType.GetUnderlyingType(type)`.
        const TypeSystem::IType* underlying =
            NullableLiftingTransform::GetUnderlyingTypeOfNullable(type);
        if (!underlying) return false;
        effectiveType = underlying;
    }

    // avoid introducing a potentially-incorrect compound assignment
    if (effectiveType && effectiveType->Kind() == TypeSystem::TypeKind::Unknown) {
        return false;
    } else if (effectiveType && effectiveType->Kind() == TypeSystem::TypeKind::Enum) {
        switch (binary->Operator) {
            case BinaryNumericOperator::Add:
            case BinaryNumericOperator::Sub:
            case BinaryNumericOperator::BitAnd:
            case BinaryNumericOperator::BitOr:
            case BinaryNumericOperator::BitXor:
                break;  // OK
            default:
                return false;  // operator not supported on enum types
        }
    } else if (effectiveType && effectiveType->Kind() == TypeSystem::TypeKind::Pointer) {
        // DEFERRED: the C# consults PointerArithmeticOffset.Detect (needs the
        // SizeOf node + ComputeSizeOf + UnwrapConv + NormalizeTypeVisitor.
        // TypeErasure.EquivalentTypes -- a substantial deferred slice). The
        // faithful conservative approximation is to reject any pointer compound
        // assignment (no pointer arithmetic is confirmed). This is
        // behavior-preserving for the .NET Framework 4 mscorlib corpus (pointer
        // arithmetic requires `unsafe`, absent from C#-compiled code); the
        // Add/Sub vs other-operator distinction is kept structural so the
        // PointerArithmeticOffset port can fill in the Add/Sub arm later.
        switch (binary->Operator) {
            case BinaryNumericOperator::Add:
            case BinaryNumericOperator::Sub:
                // return PointerArithmeticOffset::Detect(...) != nullptr;
                return false;
            default:
                return false;  // operator not supported on pointer types
        }
    } else if (effectiveType &&
               (NullableLiftingTransform::IsKnownType(effectiveType, TypeSystem::KnownTypeCode::IntPtr) ||
                NullableLiftingTransform::IsKnownType(effectiveType, TypeSystem::KnownTypeCode::UIntPtr)) &&
               effectiveType->Kind() != TypeSystem::TypeKind::NInt &&
               effectiveType->Kind() != TypeSystem::TypeKind::NUInt) {
        // If the LHS is C# 9 IntPtr (but not nint or C# 11 IntPtr):
        // "target.intptr *= 2;" is compiler error, but
        // "target.intptr *= (nint)2;" works
        if (settings != nullptr && !settings->NativeIntegers) {
            // But if native integers are not available, we cannot use compound assignment.
            return false;
        }
        // The trick with casting the RHS to n(u)int doesn't work for shifts:
        switch (binary->Operator) {
            case BinaryNumericOperator::ShiftLeft:
            case BinaryNumericOperator::ShiftRight:
                return false;
            default:
                break;
        }
    }

    if (binary->Sign != TypeSystem::Sign::None) {
        bool signMismatchAllowed = (binary->Sign == TypeSystem::Sign::Unsigned &&
                                    binary->Operator == BinaryNumericOperator::ShiftRight &&
                                    (settings == nullptr || settings->UnsignedRightShift));
        if (TypeSystem::IsCSharpSmallIntegerType(effectiveType)) {
            // C# will use numeric promotion to int, binary op must be signed
            if (binary->Sign != TypeSystem::Sign::Signed && !signMismatchAllowed)
                return false;
        } else {
            // C# will use sign from type; except for right shift with C# 11 >>> operator.
            if (TypeSystem::GetSign(effectiveType) != binary->Sign && !signMismatchAllowed)
                return false;
        }
    }
    // Can't transform if the RHS value would be need to be truncated for the LHS type.
    // The C# `IsImplicitTruncation(binary.Right, type, null, binary.IsLifted)` -- the
    // `null` is the ICompilation (this port has none); `binary.IsLifted` is the
    // allowNullableValue. The type is the effective (possibly-unwrapped) store type.
    if (IsImplicitTruncation(binary->Right.get(), effectiveType, binary->IsLifted))
        return false;
    return true;
}

bool UserDefinedCompoundAssign::IsIncrementOrDecrement(const Call* call,
                                                       const ILTransformSettings* settings) {
    if (!call) return false;
    // The C# `if (!(method.IsOperator && method.IsStatic)) return false;`. This
    // port's Call carries IsOperator (set by the IL reader from the op_* name)
    // and IsInstanceCall (true for an instance call; IsStatic is !IsInstanceCall).
    if (!call->IsOperator || call->IsInstanceCall) return false;
    auto name = ShortMethodName(call->MethodName);
    // `op_Increment` / `op_Decrement` are always recognised (the C# 1.0
    // increment/decrement operators).
    if (name == "op_Increment" || name == "op_Decrement") return true;
    // `op_CheckedIncrement` / `op_CheckedDecrement` are the C# 11.0 checked
    // variants, recognised only when the CheckedOperators setting is on (the
    // C# `settings?.CheckedOperators ?? true` -- a null settings is permissive,
    // matching the C# `?? true`).
    if (name == "op_CheckedIncrement" || name == "op_CheckedDecrement")
        return settings == nullptr || settings->CheckedOperators;
    return false;
}

bool UserDefinedCompoundAssign::IsStringConcat(const Call* call) {
    if (!call) return false;
    // The C# `method.Name == "Concat" && method.IsStatic && method.DeclaringType.
    // IsKnownType(KnownTypeCode.String)`. This port's Call carries MethodName
    // (the short part is the name), IsInstanceCall (IsStatic is !IsInstanceCall),
    // and DeclaringType (the resolved declaring IType).
    if (ShortMethodName(call->MethodName) != "Concat") return false;
    if (call->IsInstanceCall) return false;
    return call->DeclaringType &&
           NullableLiftingTransform::IsKnownType(call->DeclaringType.get(),
                                                  TypeSystem::KnownTypeCode::String);
}

}  // namespace ILSpy::Decompiler::IL
