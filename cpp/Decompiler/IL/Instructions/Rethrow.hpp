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

// Rethrow: the rethrow instruction (rethrows the current exception inside a
// catch handler). Ported from the generated Instructions.cs entry.

#pragma once

#include "Decompiler/IL/Instructions/SimpleInstruction.hpp"
#include <string>

namespace ILSpy::Decompiler::IL {

class Rethrow : public SimpleInstruction {
public:
    Rethrow() : SimpleInstruction(OpCode::Rethrow) {}
    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayThrow | InstructionFlags::EndPointUnreachable;
    }
    StackType ResultType() const override { return StackType::Void; }
    void WriteTo(std::string& out) const override { out += "rethrow"; }
};

} // namespace ILSpy::Decompiler::IL
