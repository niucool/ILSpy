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

// Conv: a numeric conversion. UnaryInstruction. The C# carries Kind, TargetType,
// CheckForOverflow, InputSign, IsLifted; this minimal port stores the target
// StackType and an overflow-check flag. Result is the target StackType.

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/IL/StackType.hpp"

#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class Conv : public UnaryInstruction {
public:
    StackType TargetStackType = StackType::Unknown;
    bool CheckForOverflow = false;
    Conv(std::unique_ptr<ILInstruction> argument, StackType target, bool checkForOverflow = false)
        : UnaryInstruction(OpCode::Conv, std::move(argument)),
          TargetStackType(target), CheckForOverflow(checkForOverflow) {}
    StackType ResultType() const override { return TargetStackType; }
    void WriteTo(std::string& out) const override {
        out += "conv.";
        switch (TargetStackType) {
            case StackType::I4: out += "i4"; break;
            case StackType::I8: out += "i8"; break;
            case StackType::I: out += "i"; break;
            case StackType::F4: out += "f4"; break;
            case StackType::F8: out += "f8"; break;
            default: out += "?"; break;
        }
        if (CheckForOverflow) out += ".ovf";
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
