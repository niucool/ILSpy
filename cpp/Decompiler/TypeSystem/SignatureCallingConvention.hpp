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

// Port of the BCL `System.Reflection.Metadata.SignatureCallingConvention` enum --
// the calling convention carried by a method/function-pointer signature header
// (ECMA-335 II.23.2.1, the leading byte's low nibble). It is the type of
// `FunctionPointerType.CallingConvention` (one of the four concrete IType
// VisitChildren types toward TypeVisitor / TypeParameterSubstitution) and of the
// not-yet-ported `MethodSignature<TType>.Header.CallingConvention`.
//
// The BCL enum is `: byte` with seven members in declaration order (Default=0,
// CDecl=1, StdCall=2, ThisCall=3, FastCall=4, VarArgs=5, Unmanaged=6). Like the
// other BCL enums absorbed into the C++ port (the D384 MethodSemanticsAttributes
// / D381 EntityHandle precedent), it is placed in ILSpy::Decompiler::TypeSystem
// (the C++ port has no System::Reflection::Metadata namespace mirror) and
// keeps the `: byte` backing and declaration-order values pinned. It is an
// ordinal enum (NOT [Flags]); the consumers compare for equality against the
// named values (the TypeSystemAstBuilder switch on CallingConvention at lines
// 360/366), so no bitwise or relational operators are needed -- the built-in
// `==` / `!=` of the C++ enum class suffices (the Nullability D380 convention).

#pragma once

#include <cstdint>
#include <string>

namespace ILSpy::Decompiler::TypeSystem {

enum class SignatureCallingConvention : std::uint8_t {
    // The default managed calling convention (the `callvirt`/`call` shape) -- the
    // zero value (the C# default for an uninitialized signature header).
    Default = 0,
    // The C unmanaged calling convention (`unmanaged cdecl` in IL syntax).
    CDecl = 1,
    // The StdCall unmanaged calling convention (`unmanaged stdcall`).
    StdCall = 2,
    // The ThisCall unmanaged calling convention (`unmanaged thiscall`).
    ThisCall = 3,
    // The FastCall unmanaged calling convention (`unmanaged fastcall`).
    FastCall = 4,
    // The varargs calling convention (`vararg`).
    VarArgs = 5,
    // The generic unmanaged calling convention (`unmanaged`, with optional
    // custom `CallConv*` modifiers enumerated in CustomCallingConventions).
    Unmanaged = 6,
};

// Port of the SRMExtensions.cs `ToILSyntax` extension method (SRMExtensions.cs line
// 783) -- the IL-syntax spelling of a calling convention, as written before a
// function-pointer signature's return type (the `unmanaged cdecl` of
// `delegate* unmanagedcdecl<int>`). Its two C# consumers are ILAmbience.cs line 400
// (the `TypeToStringVisitor.VisitFunctionPointerType` prefix) and
// InstructionOutputExtensions.cs line 350 (the instruction-level signature
// rendering); both land against this free function (the land-a-leaf-where-its-
// dependencies-live convention -- the enum it extends lives here, and the port
// carries no SRMExtensions translation unit).
//
// The C# `_ => callConv.ToString().ToLowerInvariant()` default arm: an unnamed
// enum value's `ToString()` is its numeric string (e.g. "7"), which lowercasing
// leaves unchanged -- `std::to_string` of the underlying value is the faithful
// equivalent. Every named member is listed above, so the arm is reachable only
// through a mis-decoded signature header.
inline std::string ToILSyntax(SignatureCallingConvention callConv) {
    switch (callConv) {
    case SignatureCallingConvention::Default:
        return "default";
    case SignatureCallingConvention::CDecl:
        return "unmanaged cdecl";
    case SignatureCallingConvention::StdCall:
        return "unmanaged stdcall";
    case SignatureCallingConvention::ThisCall:
        return "unmanaged thiscall";
    case SignatureCallingConvention::FastCall:
        return "unmanaged fastcall";
    case SignatureCallingConvention::VarArgs:
        return "vararg";
    case SignatureCallingConvention::Unmanaged:
        return "unmanaged";
    }
    return std::to_string(static_cast<int>(callConv));
}

}  // namespace ILSpy::Decompiler::TypeSystem
