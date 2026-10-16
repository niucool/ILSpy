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

// LdcI8 / LdcF4 / LdcF8: 64-bit integer and 32/64-bit float constants.
// SimpleInstruction; results I8 / F4 / F8.

#pragma once

#include "Decompiler/IL/Instructions/SimpleInstruction.hpp"

#include <cstdint>
#include <string>

namespace ILSpy::Decompiler::IL {

class LdcI8 : public SimpleInstruction {
public:
    std::int64_t Value = 0;
    explicit LdcI8(std::int64_t v = 0) : SimpleInstruction(OpCode::LdcI8), Value(v) {}
    StackType ResultType() const override { return StackType::I8; }
    void WriteTo(std::string& out) const override {
        out += "ldc.i8(";
        out += std::to_string(Value);
        out += ')';
    }
};

class LdcF4 : public SimpleInstruction {
public:
    float Value = 0.0f;
    explicit LdcF4(float v = 0.0f) : SimpleInstruction(OpCode::LdcF4), Value(v) {}
    StackType ResultType() const override { return StackType::F4; }
    void WriteTo(std::string& out) const override {
        out += "ldc.f4(";
        out += std::to_string(Value);
        out += ')';
    }
};

class LdcF8 : public SimpleInstruction {
public:
    double Value = 0.0;
    explicit LdcF8(double v = 0.0) : SimpleInstruction(OpCode::LdcF8), Value(v) {}
    StackType ResultType() const override { return StackType::F8; }
    void WriteTo(std::string& out) const override {
        out += "ldc.f8(";
        out += std::to_string(Value);
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
