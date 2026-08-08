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

// Port of the OpCode enum from the generated ICSharpCode.Decompiler/IL/Instructions.cs.
// One value per ILAst instruction kind (plus the AnyNode pattern wildcard). The ILAst
// is a strict tree of ILInstruction nodes; each concrete node carries its OpCode.

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::IL {

enum class OpCode : std::uint8_t {
    InvalidBranch,
    InvalidExpression,
    Nop,
    ILFunction,
    BlockContainer,
    Block,
    PinnedRegion,
    BinaryNumericInstruction,
    NumericCompoundAssign,
    UserDefinedCompoundAssign,
    DynamicCompoundAssign,
    BitNot,
    Arglist,
    Branch,
    Leave,
    IfInstruction,
    NullCoalescingInstruction,
    SwitchInstruction,
    SwitchSection,
    TryCatch,
    TryCatchHandler,
    TryFinally,
    TryFault,
    LockInstruction,
    UsingInstruction,
    DebugBreak,
    Comp,
    Call,
    CallVirt,
    CallIndirect,
    Ckfinite,
    Conv,
    LdLoc,
    LdLoca,
    StLoc,
    AddressOf,
    ThreeValuedBoolAnd,
    ThreeValuedBoolOr,
    NullableUnwrap,
    NullableRewrap,
    LdStr,
    LdStrUtf8,
    LdcI4,
    LdcI8,
    LdcF4,
    LdcF8,
    LdcDecimal,
    LdNull,
    LdFtn,
    LdVirtFtn,
    LdVirtDelegate,
    LdTypeToken,
    LdMemberToken,
    LocAlloc,
    LocAllocSpan,
    Cpblk,
    Initblk,
    LdFlda,
    LdsFlda,
    CastClass,
    IsInst,
    LdObj,
    LdObjIfRef,
    StObj,
    Box,
    Unbox,
    UnboxAny,
    NewObj,
    NewArr,
    DefaultValue,
    Throw,
    Rethrow,
    SizeOf,
    LdLen,
    LdElema,
    LdElemaInlineArray,
    GetPinnableReference,
    StringToInt,
    ExpressionTreeCast,
    UserDefinedLogicOperator,
    DynamicLogicOperatorInstruction,
    DynamicBinaryOperatorInstruction,
    DynamicUnaryOperatorInstruction,
    DynamicConvertInstruction,
    DynamicGetMemberInstruction,
    DynamicSetMemberInstruction,
    DynamicGetIndexInstruction,
    DynamicSetIndexInstruction,
    DynamicInvokeMemberInstruction,
    DynamicInvokeConstructorInstruction,
    DynamicInvokeInstruction,
    DynamicIsEventInstruction,
    MatchInstruction,
    MakeRefAny,
    RefAnyType,
    RefAnyValue,
    YieldReturn,
    Await,
    DeconstructInstruction,
    DeconstructResultInstruction,
    AnyNode
};

} // namespace ILSpy::Decompiler::IL
