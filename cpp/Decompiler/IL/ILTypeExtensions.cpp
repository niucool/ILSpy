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

#include "Decompiler/IL/ILTypeExtensions.hpp"

#include "Decompiler/IL/Instructions/ArrayInstructions.hpp"
#include "Decompiler/IL/Instructions/BinaryNumericInstruction.hpp"
#include "Decompiler/IL/Instructions/Comp.hpp"
#include "Decompiler/IL/Instructions/DefaultValue.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"
#include "Decompiler/IL/OpCode.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

namespace ILSpy::Decompiler::IL {

bool MatchDefaultValue(const ILInstruction* inst, TypeSystem::ITypePtr& type)
{
    if (inst != nullptr && inst->Op == OpCode::DefaultValue)
    {
        type = static_cast<const DefaultValue*>(inst)->Type;
        return true;
    }
    type = nullptr;
    return false;
}

TypeSystem::ITypePtr InferType(const ILInstruction& inst,
                               const TypeSystem::ICompilation* compilation)
{
    switch (inst.Op)
    {
        case OpCode::NewArr:
            if (compilation != nullptr)
            {
                auto* newArr = static_cast<const NewArr*>(&inst);
                return std::make_shared<TypeSystem::ArrayType>(
                    newArr->Type, static_cast<int>(newArr->Indices.size()));
            }
            break;
        case OpCode::NewObj:
        case OpCode::Call:
            // The C# `case NewObj` / `case Call` / `case CallVirt` arms. The port's
            // reader decodes call/callvirt/newobj into one Call node: IsNewObj marks
            // the newobj shape (the C# separate NewObj node), whose declared void
            // return the reader leaves in ReturnIType while the pushed type is O --
            // so the newobj arm (DeclaringType) must win over the call arm
            // (ReturnType). ReturnIType/DeclaringType are null when the token could
            // not be resolved, where the C# IMethod type would not exist at all.
            // The port constructs no OpCode::CallVirt node (the reader unifies
            // callvirt via Call::IsInstanceCall), so there is no separate arm.
            if (auto* call = dynamic_cast<const Call*>(&inst))
            {
                if (inst.Op == OpCode::NewObj || call->IsNewObj)
                    return call->DeclaringType ? call->DeclaringType
                                               : TypeSystem::UnknownType();
                return call->ReturnIType ? call->ReturnIType : TypeSystem::UnknownType();
            }
            break;
        case OpCode::LdObj:
            return static_cast<const LdObj*>(&inst)->Type;
        case OpCode::StObj:
            return static_cast<const StObj*>(&inst)->Type;
        case OpCode::LdLoc:
            if (auto* ldloc = static_cast<const LdLoc*>(&inst))
                return ldloc->Variable && ldloc->Variable->Type
                           ? ldloc->Variable->Type
                           : TypeSystem::UnknownType();
            break;
        case OpCode::StLoc:
            if (auto* stloc = static_cast<const StLoc*>(&inst))
                return stloc->Variable && stloc->Variable->Type
                           ? stloc->Variable->Type
                           : TypeSystem::UnknownType();
            break;
        case OpCode::LdLoca:
            if (auto* ldloca = static_cast<const LdLoca*>(&inst))
                return std::make_shared<TypeSystem::ByReferenceType>(
                    ldloca->Variable ? ldloca->Variable->Type : TypeSystem::UnknownType());
            break;
        case OpCode::LdFlda:
        case OpCode::LdsFlda:
            // The C# wraps the FIELD's type (`ldflda.Field.Type`); the port's
            // field-address nodes do not carry the field IType (see the header
            // note), so the ByReference shape is preserved over the UnknownType.
            return std::make_shared<TypeSystem::ByReferenceType>(TypeSystem::UnknownType());
        case OpCode::LdElema:
            if (auto* ldelema = static_cast<const LdElema*>(&inst))
            {
                if (compilation != nullptr && ldelema->Array)
                {
                    if (auto arrayType = dynamic_cast<TypeSystem::ArrayType*>(
                            InferType(*ldelema->Array, compilation).get());
                        arrayType != nullptr)
                    {
                        if (TypeSystem::IsCompatibleTypeForMemoryAccess(
                                const_cast<TypeSystem::IType&>(*arrayType->Element()),
                                *ldelema->Type))
                        {
                            return std::make_shared<TypeSystem::ByReferenceType>(
                                const_cast<TypeSystem::IType&>(*arrayType->Element())
                                    .shared_from_this());
                        }
                    }
                }
                return std::make_shared<TypeSystem::ByReferenceType>(ldelema->Type);
            }
            break;
        case OpCode::Comp:
            if (compilation == nullptr)
                break;
            {
                const auto* comp = static_cast<const Comp*>(&inst);
                const TypeSystem::IType& boolType =
                    compilation->FindType(TypeSystem::KnownTypeCode::Boolean);
                switch (comp->LiftingKind)
                {
                    case ComparisonLiftingKind::None:
                    case ComparisonLiftingKind::CSharp:
                        return const_cast<TypeSystem::IType&>(boolType).shared_from_this();
                    case ComparisonLiftingKind::ThreeValuedLogic:
                        return TypeSystem::Create(*compilation, boolType);
                }
            }
            break;
        case OpCode::BinaryNumericInstruction:
            if (auto* bni = static_cast<const BinaryNumericInstruction*>(&inst))
            {
                if (bni->IsLifted)
                    break;
                switch (bni->Operator)
                {
                    case BinaryNumericOperator::BitAnd:
                    case BinaryNumericOperator::BitOr:
                    case BinaryNumericOperator::BitXor:
                    {
                        TypeSystem::ITypePtr left =
                            bni->Left ? InferType(*bni->Left, compilation)
                                      : TypeSystem::UnknownType();
                        TypeSystem::ITypePtr right =
                            bni->Right ? InferType(*bni->Right, compilation)
                                       : TypeSystem::UnknownType();
                        if (left->Equals(*right)
                            && (TypeSystem::IsCSharpPrimitiveIntegerType(left.get())
                                || TypeSystem::IsCSharpNativeIntegerType(left.get())
                                || TypeSystem::IsKnownType(*left, TypeSystem::KnownTypeCode::Boolean)))
                            return left;
                        break;
                    }
                    default:
                        break;
                }
            }
            break;
        case OpCode::DefaultValue:
            return static_cast<const DefaultValue*>(&inst)->Type;
        case OpCode::UserDefinedLogicOperator:
            // The C# `logicOp.Method.ReturnType`; the seed stand-in form carries
            // no resolved method, so that form falls through to the UnknownType.
            if (auto* logicOp = static_cast<const UserDefinedLogicOperator*>(&inst))
            {
                if (logicOp->Method)
                    return std::const_pointer_cast<TypeSystem::IType>(
                        logicOp->Method->ReturnType().shared_from_this());
            }
            break;
        default:
            break;
    }
    return TypeSystem::UnknownType();
}

} // namespace ILSpy::Decompiler::IL
