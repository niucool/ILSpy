// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// InvalidBranch / InvalidExpression: the two "invalid IL" leaf nodes the reader,
// the BlockBuilder, and the control-flow transforms synthesize when they cannot
// represent a method's control flow. Ported from the generated Instructions.cs
// entries and the SimpleInstruction.cs partials:
//
//   * InvalidBranch -- a control-flow terminator that is considered to produce
//     `ExpectedResultType` (Void by default). DirectFlags =
//     MayThrow | SideEffect | EndPointUnreachable.
//   * InvalidExpression -- an expression that is considered to produce
//     `ExpectedResultType` (Unknown by default). DirectFlags =
//     MayThrow | SideEffect.
//
// Both carry a free-form Message (null means none) and are rendered by the
// C# ExpressionBuilder as an ErrorExpression whose comment is
// "<prefix>[ near IL_xxxx][: message]" where the prefix is "Error" for a branch
// and the node's Severity for an expression.

#pragma once

#include "Decompiler/IL/Instructions/SimpleInstruction.hpp"

#include <optional>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::IL {

// The C# `public sealed partial class InvalidBranch : SimpleInstruction` (the
// generated Instructions.cs entry + the SimpleInstruction.cs partial). A leaf
// with no children and no result value by default; the IL dump is
// `InvalidBranch` or `InvalidBranch("message")`.
class InvalidBranch : public SimpleInstruction {
public:
    // The C# `public string? Message` field: an optional diagnostic rendered in
    // the parentheses of the IL dump and in the ExpressionBuilder's error text.
    std::optional<std::string> Message;

    // The C# `public StackType ExpectedResultType = StackType.Void` field. The
    // generated `ResultType` override returns it unchanged.
    StackType ExpectedResultType = StackType::Void;

    InvalidBranch() : SimpleInstruction(OpCode::InvalidBranch) {}
    explicit InvalidBranch(std::optional<std::string> message)
        : SimpleInstruction(OpCode::InvalidBranch), Message(std::move(message)) {}

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayThrow | InstructionFlags::SideEffect
            | InstructionFlags::EndPointUnreachable;
    }
    StackType ResultType() const override { return ExpectedResultType; }
    void WriteTo(std::string& out) const override {
        out += "InvalidBranch";
        if (Message && !Message->empty()) {
            out += "(\"";
            out += *Message;
            out += "\")";
        }
    }
};

// The C# `public sealed partial class InvalidExpression : SimpleInstruction` (the
// generated Instructions.cs entry + the SimpleInstruction.cs partial). A leaf
// with no children; the IL dump is `InvalidExpression` or
// `InvalidExpression("message")`.
class InvalidExpression : public SimpleInstruction {
public:
    // The C# `public string Severity = "Error"` field: the prefix of the
    // ExpressionBuilder's error text (e.g. "Note" for the switch-emulation shape).
    std::string Severity = "Error";

    // The C# `public string? Message` field.
    std::optional<std::string> Message;

    // The C# `public StackType ExpectedResultType = StackType.Unknown` field.
    StackType ExpectedResultType = StackType::Unknown;

    InvalidExpression() : SimpleInstruction(OpCode::InvalidExpression) {}
    explicit InvalidExpression(std::optional<std::string> message)
        : SimpleInstruction(OpCode::InvalidExpression), Message(std::move(message)) {}

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::MayThrow | InstructionFlags::SideEffect;
    }
    StackType ResultType() const override { return ExpectedResultType; }
    void WriteTo(std::string& out) const override {
        out += "InvalidExpression";
        if (Message && !Message->empty()) {
            out += "(\"";
            out += *Message;
            out += "\")";
        }
    }
};

} // namespace ILSpy::Decompiler::IL
