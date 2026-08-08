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

// Port of ICSharpCode.Decompiler/IL/InstructionFlags.cs. Bottom-up semantic flags
// for an ILAst node. The decompiler leans on these constantly ("may I move this
// expression past that one?"); ControlFlow carries the central evaluation-order
// guarantee (absent => descendants evaluate once, left-to-right, pre-order).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::IL {

enum class InstructionFlags : std::uint32_t {
    None = 0,
    MayReadLocals = 0x10,
    MayWriteLocals = 0x20,
    SideEffect = 0x40,
    MayThrow = 0x100,
    MayBranch = 0x200,
    MayUnwrapNull = 0x400,
    EndPointUnreachable = 0x800,
    ControlFlow = 0x1000,
};

inline InstructionFlags operator|(InstructionFlags a, InstructionFlags b) {
    return static_cast<InstructionFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
inline InstructionFlags operator&(InstructionFlags a, InstructionFlags b) {
    return static_cast<InstructionFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
inline InstructionFlags operator~(InstructionFlags a) {
    return static_cast<InstructionFlags>(~static_cast<std::uint32_t>(a));
}
inline bool HasFlag(InstructionFlags f, InstructionFlags flag) {
    return (static_cast<std::uint32_t>(f) & static_cast<std::uint32_t>(flag)) != 0;
}

// Port of SemanticHelper.CombineBranches: the endpoint of a conditional is
// unreachable only if BOTH branches are unreachable.
inline InstructionFlags CombineBranches(InstructionFlags trueFlags, InstructionFlags falseFlags) {
    constexpr InstructionFlags andCombined = InstructionFlags::EndPointUnreachable;
    InstructionFlags union_ = trueFlags | falseFlags;
    return (trueFlags & falseFlags & andCombined) | (union_ & ~andCombined);
}

} // namespace ILSpy::Decompiler::IL
