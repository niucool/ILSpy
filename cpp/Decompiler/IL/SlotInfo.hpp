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

// Port of ICSharpCode.Decompiler/IL/SlotInfo.cs. Describes the role of one child
// slot within its parent: the slot name, whether the inlining transform may move
// an expression into it (CanInlineInto), and whether it is a collection slot.

#pragma once

#include <string_view>

namespace ILSpy::Decompiler::IL {

struct SlotInfo {
    std::string_view Name;
    bool CanInlineInto;
    bool IsCollection;

    constexpr SlotInfo(std::string_view name, bool canInlineInto = false, bool isCollection = false)
        : Name(name), CanInlineInto(canInlineInto), IsCollection(isCollection) {}

    // "<no slot>" sentinel (matches the C# SlotInfo.None).
    static const SlotInfo None;
};

inline const SlotInfo SlotInfo::None("<no slot>");

} // namespace ILSpy::Decompiler::IL
