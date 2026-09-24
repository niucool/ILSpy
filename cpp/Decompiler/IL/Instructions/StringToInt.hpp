// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of the C# StringToInt node (Instructions.cs line 5231 + the hand-written
// StringToInt.cs partial): `string.to.int expectedType(argument, map)` -- the
// string-to-hash-bucket dispatch SwitchOnStringTransform emits inside a
// SwitchInstruction's value. ResultType I4 (the generated class); DirectFlags
// None (the C# ComputeFlags derives from the argument, so the port's Flags()
// walk over children yields the same set). The map carries (string?, int)
// pairs -- a null key is the C# `case null:` arm's bucket.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

class StringToInt : public UnaryInstruction {
public:
    // The C# `List<(string? Key, int Value)> Map` -- the (key, bucket) pairs;
    // a nullopt key is the null-case entry (C# `case null:`).
    std::vector<std::pair<std::optional<std::string>, int>> Map;
    // The C# `public IType ExpectedType { get; }` -- the switch-value's type
    // (string or ReadOnlySpan<char>); carried for the back end's dispatch
    // shape (equality vs Length comparison).
    TypeSystem::ITypePtr ExpectedType;

    explicit StringToInt(std::unique_ptr<ILInstruction> argument,
                         TypeSystem::ITypePtr expectedType)
        : UnaryInstruction(OpCode::StringToInt, std::move(argument)),
          ExpectedType(std::move(expectedType)) {}

    // The C# `StringToInt(argument, string?[] map, expectedType)` -- the
    // array form mapping map[i] -> i.
    static std::unique_ptr<StringToInt>
    FromArray(std::unique_ptr<ILInstruction> argument,
              std::vector<std::optional<std::string>> keys,
              TypeSystem::ITypePtr expectedType) {
        auto node = std::make_unique<StringToInt>(std::move(argument),
                                                  std::move(expectedType));
        for (std::size_t i = 0; i < keys.size(); i++) {
            node->Map.emplace_back(std::move(keys[i]), static_cast<int>(i));
        }
        return node;
    }

    InstructionFlags DirectFlags() const override {
        return InstructionFlags::None;
    }
    StackType ResultType() const override { return StackType::I4; }
    void WriteTo(std::string& out) const override {
        out += "string.to.int ";
        out += ExpectedType ? ExpectedType->ReflectionName() : std::string("?");
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ", { ";
        for (std::size_t i = 0; i < Map.size(); i++) {
            if (i > 0) out += ", ";
            if (Map[i].first.has_value()) {
                out += "[\"";
                out += *Map[i].first;
                out += "\"] = ";
            } else {
                out += "[null] = ";
            }
            out += std::to_string(Map[i].second);
        }
        out += " })";
    }
};

} // namespace ILSpy::Decompiler::IL