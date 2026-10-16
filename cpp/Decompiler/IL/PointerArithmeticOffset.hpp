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

// Port of ICSharpCode.Decompiler/IL/PointerArithmeticOffset.cs (the 133-line
// `struct PointerArithmeticOffset`): analyses the RHS of a 'ptr + int' or
// 'ptr - int' operation. `Detect` converts a byte-count instruction into an
// element-count instruction; `ComputeSizeOf` reads the element size; and
// `IsFixedVariable` classifies whether an instruction computes the address of a
// fixed variable. The port's IsFixedVariable previously lived as a file-local
// helper beside the TranslatedExpression conversion code; the C# member lives on
// this struct, so it is exposed here as well.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <optional>

namespace ILSpy::Decompiler::IL {

class ILInstruction;
struct PointerArithmeticOffset {
    // The C# Detect returns either an existing instruction (a child pointer or the
    // input itself) or a freshly built LdcI4 constant (the compiler constant-folded
    // the multiplication). The port's IL tree owns children via unique_ptr, so a
    // fresh constant cannot be returned as a bare pointer; the outcome struct keeps
    // it alive for the caller (GetPointerArithmeticOffset) while translating.
    struct DetectOutcome {
        // The matched instruction; null = no match (the C# null).
        const ILInstruction* Inst = nullptr;
        // Non-null when Inst is a freshly built LdcI4 the caller must keep alive
        // (the C# GC's ownership).
        std::unique_ptr<ILInstruction> Owned;

        explicit operator bool() const noexcept { return Inst != nullptr; }
    };

    // Given an instruction that computes a pointer arithmetic offset in bytes,
    // returns an instruction that computes the same offset in number of elements,
    // or null if no such instruction can be found. `unwrapZeroExtension` allows
    // zero extensions in the mul argument (the TranslateLocAlloc caller's option).
    static DetectOutcome Detect(const ILInstruction* byteOffsetInst,
                                const TypeSystem::IType* pointerElementType,
                                bool checkForOverflow,
                                bool unwrapZeroExtension = false);

    // The C# `public static int? ComputeSizeOf(IType type)`: the primitive
    // element-size table over the underlying definition's KnownTypeCode (nullopt
    // for every non-primitive element type).
    static std::optional<int> ComputeSizeOf(const TypeSystem::IType* type);

    // Returns true if `inst` computes the address of a fixed variable; false if it
    // computes the address of a moveable variable.
    // (see "Fixed and moveable variables" in the C# specification)
    static bool IsFixedVariable(const ILInstruction* inst);
};

} // namespace ILSpy::Decompiler::IL
