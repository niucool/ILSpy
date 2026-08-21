// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so, subject
// to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the static helpers on ICSharpCode.Decompiler/CSharp/Syntax/TypeSystemAstBuilder.cs
// that depend only on already-ported TypeSystem / Syntax leaves. The full
// TypeSystemAstBuilder class (the long-pole CSharpAmbience blocker, 2782 C# lines)
// derives from CSharpResolver and threads the full IType / IMember / ITypeDefinition
// surface, so it is ported incrementally: each self-contained static helper lands as a
// free function in this namespace ahead of the instance methods, which are deferred
// until the CSharpResolver dependency chain is ported.
//
// ModifierFromAccessibility (TypeSystemAstBuilder.cs line 2497) is the first such
// helper: a pure switch on Accessibility (the D373 leaf) that maps a symbol's
// visibility to the Syntax Modifiers bits (the D270 enum). It is the public static
// API the CSharpAmbience / GetMemberModifiers path calls; the usePrivateProtected
// bool arg gates whether ProtectedAndInternal maps to `private protected`
// (Private | Protected) or to the C# 7 fallback `protected` (Protected) -- the
// UsePrivateProtectedAccessibility property of TypeSystemAstBuilder.

#pragma once

#include "Modifiers.hpp"

#include "Decompiler/TypeSystem/Accessibility.hpp"

namespace ILSpy::Decompiler::CSharp::Syntax {

// TypeSystemAstBuilder.ModifierFromAccessibility (TypeSystemAstBuilder.cs line 2497).
// Maps an Accessibility value to the corresponding Syntax Modifiers bits. The
// ProtectedAndInternal case is gated by usePrivateProtected: when true it emits
// `private protected` (Modifiers::Private | Modifiers::Protected, the C# 7.2
// private-protected accessibility); when false it falls back to the pre-C#-7.2
// `protected` (Modifiers::Protected). The default case (Accessibility::None or any
// other value) returns Modifiers::None.
inline Modifiers ModifierFromAccessibility(::ILSpy::Decompiler::TypeSystem::Accessibility accessibility,
                                          bool usePrivateProtected) noexcept {
    namespace TS = ::ILSpy::Decompiler::TypeSystem;
    switch (accessibility) {
        case TS::Accessibility::Private:
            return Modifiers::Private;
        case TS::Accessibility::Public:
            return Modifiers::Public;
        case TS::Accessibility::Protected:
            return Modifiers::Protected;
        case TS::Accessibility::Internal:
            return Modifiers::Internal;
        case TS::Accessibility::ProtectedOrInternal:
            return Modifiers::Protected | Modifiers::Internal;
        case TS::Accessibility::ProtectedAndInternal:
            return usePrivateProtected ? (Modifiers::Private | Modifiers::Protected)
                                      : Modifiers::Protected;
        default:
            return Modifiers::None;
    }
}

} // namespace ILSpy::Decompiler::CSharp::Syntax
