// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// StringToInt: the string-switch desugaring node (SwitchAnalysis builds it; the
// C# back end consumes it in ExpressionBuilder.TranslateSwitchValue and
// StatementBuilder.CreateTypedCaseLabel). Port of the C#
// ICSharpCode.Decompiler.IL.StringToInt: the generated Instructions.cs
// `StringToInt : ILInstruction` half (Instructions.cs line 5231 -- a single
// inlineable Argument child, StackType.I4 result, DirectFlags None, flags
// computed over the argument) plus the hand-written partial
// (IL/Instructions/StringToInt.cs -- the Map list and the ExpectedType).
//
// The C# hand-written partial's `List<(string? Key, int Value)> Map` maps each
// case-string to the integer label the IL switch dispatches on (a null key is the
// `case null:` arm -- null is not an integer label, so it rides the map as a
// nullable key). The C# tuple list ports to
// `std::vector<std::pair<std::optional<std::string>, int>>` (the C# nullable
// string -> std::optional, the tuple -> std::pair). The `string?[] map` ctor
// (ArrayToDictionary) ports as the vector-of-optionals overload.
//
// The C# `IType ExpectedType`: the governing type the switch on strings uses
// (typically string; a lifted switch on a nullable-of-string carries the
// nullable). The port stores it as the owning `TypeSystem::ITypePtr` (the tree
// holds the reference; the C# GC roots it through the field).

#ifndef ILSPY_DECOMPILER_IL_INSTRUCTIONS_STRINGTOINT_HPP
#define ILSPY_DECOMPILER_IL_INSTRUCTIONS_STRINGTOINT_HPP

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

class StringToInt : public UnaryInstruction {
public:
    // The string-key -> integer-label map (a null key is the `case null:` arm).
    std::vector<std::pair<std::optional<std::string>, int>> Map;
    // The C# `IType ExpectedType` -- the governing type of the string switch.
    TypeSystem::ITypePtr ExpectedType;

    // The C# `StringToInt(ILInstruction argument, List<(string? Key, int
    // Value)> map, IType expectedType)` (StringToInt.cs line 44).
    StringToInt(std::unique_ptr<ILInstruction> argument,
                std::vector<std::pair<std::optional<std::string>, int>> map,
                TypeSystem::ITypePtr expectedType)
        : UnaryInstruction(OpCode::StringToInt, std::move(argument)),
          Map(std::move(map)), ExpectedType(std::move(expectedType)) {}

    // The C# `StringToInt(ILInstruction argument, string?[] map, IType
    // expectedType)` -- the array form over ArrayToDictionary (the positional
    // array a switch over string constants compiles to; index i maps key
    // map[i] -> label i).
    StringToInt(std::unique_ptr<ILInstruction> argument,
                const std::vector<std::optional<std::string>>& map,
                TypeSystem::ITypePtr expectedType)
        : UnaryInstruction(OpCode::StringToInt, std::move(argument)),
          Map(ArrayToDictionary(map)), ExpectedType(std::move(expectedType)) {}

    StackType ResultType() const override { return StackType::I4; }

    // The C# `WriteToCore` (StringToInt.cs line 60): `string.to.int <type>(<arg>,
    // { ["k"] = v, [null] = v })`. The port's instruction WriteTo renders the
    // ExpectedType through its ReflectionName (the CastClass IL-dump convention).
    void WriteTo(std::string& out) const override {
        out += "string.to.int ";
        out += ExpectedType ? ExpectedType->ReflectionName() : std::string("?");
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ", { ";
        bool first = true;
        for (const auto& entry : Map) {
            if (!first) out += ", ";
            if (entry.first.has_value()) {
                out += "[\"" + *entry.first + "\"] = " + std::to_string(entry.second);
            } else {
                out += "[null] = " + std::to_string(entry.second);
            }
            first = false;
        }
        out += " })";
    }

private:
    // The C# `static List<(string?, int)> ArrayToDictionary(string?[] map)`
    // (StringToInt.cs line 54).
    static std::vector<std::pair<std::optional<std::string>, int>> ArrayToDictionary(
        const std::vector<std::optional<std::string>>& map) {
        std::vector<std::pair<std::optional<std::string>, int>> dict;
        for (std::size_t i = 0; i < map.size(); i++) {
            dict.emplace_back(map[i], static_cast<int>(i));
        }
        return dict;
    }
};

} // namespace ILSpy::Decompiler::IL

#endif // ILSPY_DECOMPILER_IL_INSTRUCTIONS_STRINGTOINT_HPP
