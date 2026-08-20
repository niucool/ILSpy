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
// PURPOSE NONINFRINGEMENT. HOWEVER CAUSED AND ON WHICHEVER THEORY OF LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH
// THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `SignatureCallingConvention` (cpp/Decompiler/TypeSystem/
// SignatureCallingConvention.hpp), the port of the BCL
// `System.Reflection.Metadata.SignatureCallingConvention` enum -- the calling
// convention carried by a method/function-pointer signature header. It is the
// type of `FunctionPointerType.CallingConvention` (one of the four concrete
// IType VisitChildren types toward TypeVisitor / TypeParameterSubstitution).
// The tests pin the enum's byte backing, its seven values in C# declaration
// order, and the equality-comparison / switch patterns the TypeSystemAstBuilder
// consumer uses (the switch on `fpt.CallingConvention` at lines 360/366).

#include "Decompiler/TypeSystem/SignatureCallingConvention.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace TS = ILSpy::Decompiler::TypeSystem;

// ---------------------------------------------------------------------------
// The enum is `: byte` with seven members in declaration order (Default=0
// through Unmanaged=6). The values are pinned explicitly: the
// SignatureCallingConvention is read from the signature header's low nibble,
// and the TypeSystemAstBuilder / ILAmbience consumers compare against the
// named values, so reordering would silently remap the calling convention.
// ---------------------------------------------------------------------------
TEST(SignatureCallingConventionTest, EnumValuesMatchCSharpDeclarationOrder)
{
    EXPECT_EQ(static_cast<std::uint8_t>(TS::SignatureCallingConvention::Default), 0);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::SignatureCallingConvention::CDecl), 1);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::SignatureCallingConvention::StdCall), 2);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::SignatureCallingConvention::ThisCall), 3);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::SignatureCallingConvention::FastCall), 4);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::SignatureCallingConvention::VarArgs), 5);
    EXPECT_EQ(static_cast<std::uint8_t>(TS::SignatureCallingConvention::Unmanaged), 6);
}

// ---------------------------------------------------------------------------
// The BCL `: byte` backing ports to std::uint8_t. The enum is exactly one byte
// (it is read from a single signature-header byte's low nibble).
// ---------------------------------------------------------------------------
TEST(SignatureCallingConventionTest, IsByteBacked)
{
    static_assert(sizeof(TS::SignatureCallingConvention) == 1,
                  "SignatureCallingConvention must be byte-backed");
    static_assert(static_cast<std::uint8_t>(TS::SignatureCallingConvention{}) == 0,
                  "value-initialized SignatureCallingConvention must be Default (the zero)");
    SUCCEED();
}

// ---------------------------------------------------------------------------
// The TypeSystemAstBuilder consumer switches on `fpt.CallingConvention`:
// `== Default` is the no-keyword managed case, `== Unmanaged` triggers the
// `HasUnmanagedCallingConvention` flag, and any other value selects a named
// calling-convention keyword. The seven values are distinct so every
// equality comparison resolves to a single arm.
// ---------------------------------------------------------------------------
TEST(SignatureCallingConventionTest, EqualityComparisonMatchesConsumerPattern)
{
    EXPECT_TRUE(TS::SignatureCallingConvention::Default == TS::SignatureCallingConvention::Default);
    EXPECT_FALSE(TS::SignatureCallingConvention::Default == TS::SignatureCallingConvention::Unmanaged);
    EXPECT_FALSE(TS::SignatureCallingConvention::CDecl == TS::SignatureCallingConvention::StdCall);
    EXPECT_TRUE(TS::SignatureCallingConvention::VarArgs != TS::SignatureCallingConvention::Default);
    EXPECT_TRUE(TS::SignatureCallingConvention::Unmanaged != TS::SignatureCallingConvention::Default);
    EXPECT_FALSE(TS::SignatureCallingConvention::ThisCall != TS::SignatureCallingConvention::ThisCall);
}

// ---------------------------------------------------------------------------
// The seven calling conventions are distinguishable: a switch over the enum
// (the TypeSystemAstBuilder switch at line 366 that maps each non-Default /
// non-Unmanaged value to a `Cdecl`/`Stdcall`/`Thiscall`/`Fastcall`/`Varargs`
// keyword) reaches a distinct arm for each value.
// ---------------------------------------------------------------------------
TEST(SignatureCallingConventionTest, SevenConventionsAreDistinguishable)
{
    for (auto value : {
        TS::SignatureCallingConvention::Default,
        TS::SignatureCallingConvention::CDecl,
        TS::SignatureCallingConvention::StdCall,
        TS::SignatureCallingConvention::ThisCall,
        TS::SignatureCallingConvention::FastCall,
        TS::SignatureCallingConvention::VarArgs,
        TS::SignatureCallingConvention::Unmanaged,
    }) {
        int arm = -1;
        switch (value) {
            case TS::SignatureCallingConvention::Default: arm = 0; break;
            case TS::SignatureCallingConvention::CDecl: arm = 1; break;
            case TS::SignatureCallingConvention::StdCall: arm = 2; break;
            case TS::SignatureCallingConvention::ThisCall: arm = 3; break;
            case TS::SignatureCallingConvention::FastCall: arm = 4; break;
            case TS::SignatureCallingConvention::VarArgs: arm = 5; break;
            case TS::SignatureCallingConvention::Unmanaged: arm = 6; break;
        }
        EXPECT_EQ(arm, static_cast<int>(static_cast<std::uint8_t>(value)));
    }
}
