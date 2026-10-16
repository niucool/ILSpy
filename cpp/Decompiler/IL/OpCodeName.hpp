// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The C# `OpCode` enum's ToString() spelling: the member name (the C# enum's
// default ToString). Generated mechanically from the port's OpCode.hpp member
// list (the generated-pure-data port pattern) so every new opcode lands here
// with the enum itself; the ExpressionBuilder's Default() error render and the
// IL-visitor dispatch traces consume it.

#pragma once

#include "Decompiler/IL/OpCode.hpp"

#include <string_view>

namespace ILSpy::Decompiler::IL {

constexpr std::string_view OpCodeName(OpCode op)
{
    switch (op)
    {
    case OpCode::InvalidBranch:
        return "InvalidBranch";
    case OpCode::InvalidExpression:
        return "InvalidExpression";
    case OpCode::Nop:
        return "Nop";
    case OpCode::ILFunction:
        return "ILFunction";
    case OpCode::BlockContainer:
        return "BlockContainer";
    case OpCode::Block:
        return "Block";
    case OpCode::PinnedRegion:
        return "PinnedRegion";
    case OpCode::BinaryNumericInstruction:
        return "BinaryNumericInstruction";
    case OpCode::NumericCompoundAssign:
        return "NumericCompoundAssign";
    case OpCode::UserDefinedCompoundAssign:
        return "UserDefinedCompoundAssign";
    case OpCode::DynamicCompoundAssign:
        return "DynamicCompoundAssign";
    case OpCode::BitNot:
        return "BitNot";
    case OpCode::Arglist:
        return "Arglist";
    case OpCode::Branch:
        return "Branch";
    case OpCode::Leave:
        return "Leave";
    case OpCode::IfInstruction:
        return "IfInstruction";
    case OpCode::NullCoalescingInstruction:
        return "NullCoalescingInstruction";
    case OpCode::SwitchInstruction:
        return "SwitchInstruction";
    case OpCode::SwitchSection:
        return "SwitchSection";
    case OpCode::TryCatch:
        return "TryCatch";
    case OpCode::TryCatchHandler:
        return "TryCatchHandler";
    case OpCode::TryFinally:
        return "TryFinally";
    case OpCode::TryFault:
        return "TryFault";
    case OpCode::LockInstruction:
        return "LockInstruction";
    case OpCode::UsingInstruction:
        return "UsingInstruction";
    case OpCode::DebugBreak:
        return "DebugBreak";
    case OpCode::Comp:
        return "Comp";
    case OpCode::Call:
        return "Call";
    case OpCode::CallVirt:
        return "CallVirt";
    case OpCode::CallIndirect:
        return "CallIndirect";
    case OpCode::Ckfinite:
        return "Ckfinite";
    case OpCode::Conv:
        return "Conv";
    case OpCode::LdLoc:
        return "LdLoc";
    case OpCode::LdLoca:
        return "LdLoca";
    case OpCode::StLoc:
        return "StLoc";
    case OpCode::AddressOf:
        return "AddressOf";
    case OpCode::ThreeValuedBoolAnd:
        return "ThreeValuedBoolAnd";
    case OpCode::ThreeValuedBoolOr:
        return "ThreeValuedBoolOr";
    case OpCode::NullableUnwrap:
        return "NullableUnwrap";
    case OpCode::NullableRewrap:
        return "NullableRewrap";
    case OpCode::LdStr:
        return "LdStr";
    case OpCode::LdStrUtf8:
        return "LdStrUtf8";
    case OpCode::LdcI4:
        return "LdcI4";
    case OpCode::LdcI8:
        return "LdcI8";
    case OpCode::LdcF4:
        return "LdcF4";
    case OpCode::LdcF8:
        return "LdcF8";
    case OpCode::LdcDecimal:
        return "LdcDecimal";
    case OpCode::LdNull:
        return "LdNull";
    case OpCode::LdFtn:
        return "LdFtn";
    case OpCode::LdVirtFtn:
        return "LdVirtFtn";
    case OpCode::LdVirtDelegate:
        return "LdVirtDelegate";
    case OpCode::LdTypeToken:
        return "LdTypeToken";
    case OpCode::LdMemberToken:
        return "LdMemberToken";
    case OpCode::LocAlloc:
        return "LocAlloc";
    case OpCode::LocAllocSpan:
        return "LocAllocSpan";
    case OpCode::Cpblk:
        return "Cpblk";
    case OpCode::Initblk:
        return "Initblk";
    case OpCode::LdFlda:
        return "LdFlda";
    case OpCode::LdsFlda:
        return "LdsFlda";
    case OpCode::CastClass:
        return "CastClass";
    case OpCode::IsInst:
        return "IsInst";
    case OpCode::LdObj:
        return "LdObj";
    case OpCode::LdObjIfRef:
        return "LdObjIfRef";
    case OpCode::StObj:
        return "StObj";
    case OpCode::Box:
        return "Box";
    case OpCode::Unbox:
        return "Unbox";
    case OpCode::UnboxAny:
        return "UnboxAny";
    case OpCode::NewObj:
        return "NewObj";
    case OpCode::NewArr:
        return "NewArr";
    case OpCode::DefaultValue:
        return "DefaultValue";
    case OpCode::Throw:
        return "Throw";
    case OpCode::Rethrow:
        return "Rethrow";
    case OpCode::SizeOf:
        return "SizeOf";
    case OpCode::LdLen:
        return "LdLen";
    case OpCode::LdElema:
        return "LdElema";
    case OpCode::LdElemaInlineArray:
        return "LdElemaInlineArray";
    case OpCode::GetPinnableReference:
        return "GetPinnableReference";
    case OpCode::StringToInt:
        return "StringToInt";
    case OpCode::ExpressionTreeCast:
        return "ExpressionTreeCast";
    case OpCode::UserDefinedLogicOperator:
        return "UserDefinedLogicOperator";
    case OpCode::DynamicLogicOperatorInstruction:
        return "DynamicLogicOperatorInstruction";
    case OpCode::DynamicBinaryOperatorInstruction:
        return "DynamicBinaryOperatorInstruction";
    case OpCode::DynamicUnaryOperatorInstruction:
        return "DynamicUnaryOperatorInstruction";
    case OpCode::DynamicConvertInstruction:
        return "DynamicConvertInstruction";
    case OpCode::DynamicGetMemberInstruction:
        return "DynamicGetMemberInstruction";
    case OpCode::DynamicSetMemberInstruction:
        return "DynamicSetMemberInstruction";
    case OpCode::DynamicGetIndexInstruction:
        return "DynamicGetIndexInstruction";
    case OpCode::DynamicSetIndexInstruction:
        return "DynamicSetIndexInstruction";
    case OpCode::DynamicInvokeMemberInstruction:
        return "DynamicInvokeMemberInstruction";
    case OpCode::DynamicInvokeConstructorInstruction:
        return "DynamicInvokeConstructorInstruction";
    case OpCode::DynamicInvokeInstruction:
        return "DynamicInvokeInstruction";
    case OpCode::DynamicIsEventInstruction:
        return "DynamicIsEventInstruction";
    case OpCode::MatchInstruction:
        return "MatchInstruction";
    case OpCode::MakeRefAny:
        return "MakeRefAny";
    case OpCode::RefAnyType:
        return "RefAnyType";
    case OpCode::RefAnyValue:
        return "RefAnyValue";
    case OpCode::YieldReturn:
        return "YieldReturn";
    case OpCode::Await:
        return "Await";
    case OpCode::DeconstructInstruction:
        return "DeconstructInstruction";
    case OpCode::DeconstructResultInstruction:
        return "DeconstructResultInstruction";
    case OpCode::AnyNode:
        return "AnyNode";
    }
    return "Unknown";
}

} // namespace ILSpy::Decompiler::IL
