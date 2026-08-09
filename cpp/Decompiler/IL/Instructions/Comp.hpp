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

// Comp: a comparison (ceq/cgt/clt and their .un forms). BinaryInstruction;
// result I4. The ComparisonKind port follows Comp.cs.

#pragma once

#include "Decompiler/IL/Instructions/BinaryInstruction.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

enum class ComparisonKind : std::uint8_t {
    Equality,
    Inequality,
    LessThan,
    LessThanOrEqual,
    GreaterThan,
    GreaterThanOrEqual,
};

// Negate a comparison kind (ECMA-335 II.3.2): == <=> !=, < => >=, <= => >.
// Port of ComparisonKind.Negate in Comp.cs.
inline ComparisonKind NegateComparison(ComparisonKind kind) {
    switch (kind) {
        case ComparisonKind::Equality: return ComparisonKind::Inequality;
        case ComparisonKind::Inequality: return ComparisonKind::Equality;
        case ComparisonKind::LessThan: return ComparisonKind::GreaterThanOrEqual;
        case ComparisonKind::LessThanOrEqual: return ComparisonKind::GreaterThan;
        case ComparisonKind::GreaterThan: return ComparisonKind::LessThanOrEqual;
        case ComparisonKind::GreaterThanOrEqual: return ComparisonKind::LessThan;
    }
    return ComparisonKind::Inequality;  // unreachable
}

class Comp : public BinaryInstruction {
public:
    ComparisonKind Kind = ComparisonKind::Equality;
    bool Unsigned = false;
    Comp(std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right,
         ComparisonKind kind = ComparisonKind::Equality, bool unsigned_ = false)
        : BinaryInstruction(OpCode::Comp, std::move(left), std::move(right)),
          Kind(kind), Unsigned(unsigned_) {}
    StackType ResultType() const override { return StackType::I4; }
    void WriteTo(std::string& out) const override {
        out += "comp(";
        switch (Kind) {
            case ComparisonKind::Equality: out += "eq"; break;
            case ComparisonKind::Inequality: out += "ne"; break;
            case ComparisonKind::LessThan: out += "lt"; break;
            case ComparisonKind::LessThanOrEqual: out += "le"; break;
            case ComparisonKind::GreaterThan: out += "gt"; break;
            case ComparisonKind::GreaterThanOrEqual: out += "ge"; break;
        }
        if (Unsigned) out += ".un";
        out += ", ";
        if (Left) Left->WriteTo(out); else out += "(null)";
        out += ", ";
        if (Right) Right->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
