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
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// TypesParser.cpp -- see TypesParser.hpp for the port contract.

#include "ILSpyCmd/TypesParser.hpp"

#include <cctype>
#include <cstring>
#include <string_view>

namespace ILSpy::ILSpyCmd {

namespace {

using ILSpy::Decompiler::TypeSystem::TypeKind;

// The C# `possibleValues` dictionary (a case-insensitive
// class/struct/interface/enum/delegate key -> TypeKind map).
struct PossibleKind {
    const char* Key;
    TypeKind Kind;
};

constexpr PossibleKind kPossibleKinds[] = {
    { "class", TypeKind::Class },
    { "struct", TypeKind::Struct },
    { "interface", TypeKind::Interface },
    { "enum", TypeKind::Enum },
    { "delegate", TypeKind::Delegate },
};

// StringComparison.OrdinalIgnoreCase over the ASCII keys (the C#
// dictionary comparer): equal lengths and equal case-folded characters.
bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i]))
            != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

// The C# `possibleValues.TryGetValue(v, out var kind)` -- a null return is
// the not-found case.
const TypeKind* FindKind(std::string_view v) {
    for (const auto& k : kPossibleKinds) {
        if (EqualsIgnoreCase(v, k.Key))
            return &k.Kind;
    }
    return nullptr;
}

// The C# `values[0].StartsWith(v, StringComparison.OrdinalIgnoreCase)`
// over the keys: the value has a key as a case-insensitive prefix (a value
// shorter than a key never starts with it).
bool StartsWithAnyKey(std::string_view value) {
    for (const auto& k : kPossibleKinds) {
        std::size_t keyLen = std::strlen(k.Key);
        if (value.size() >= keyLen && EqualsIgnoreCase(value.substr(0, keyLen), k.Key))
            return true;
    }
    return false;
}

}  // namespace

// The C# `public static HashSet<TypeKind> ParseSelection(string[] values)`
// (TypesParser.cs). See TypesParser.hpp for the arm-by-arm contract.
std::set<ILSpy::Decompiler::TypeSystem::TypeKind> ParseSelection(
    const std::vector<std::string>& values)
{
    std::set<ILSpy::Decompiler::TypeSystem::TypeKind> kinds;
    // The C# `values.Length == 1 && !possibleValues.Keys.Any(v =>
    // values[0].StartsWith(v, StringComparison.OrdinalIgnoreCase))` arm: a
    // single value that does not BEGIN with a key is a soup of kind
    // characters, matched case-SENSITIVELY ('C' selects nothing).
    if (values.size() == 1 && !StartsWithAnyKey(values[0])) {
        for (char ch : values[0]) {
            switch (ch) {
                case 'c': kinds.insert(TypeKind::Class); break;
                case 'i': kinds.insert(TypeKind::Interface); break;
                case 's': kinds.insert(TypeKind::Struct); break;
                case 'd': kinds.insert(TypeKind::Delegate); break;
                case 'e': kinds.insert(TypeKind::Enum); break;
                default: break;  // any other character is ignored
            }
        }
        return kinds;
    }
    // The else arm: every value is trimmed from the end until it is a key;
    // a value that trims past its start is silently ignored (the C#
    // TryGetValue on the empty string is a miss).
    for (const std::string& value : values) {
        std::string v = value;
        while (!v.empty() && FindKind(v) == nullptr)
            v.pop_back();
        if (const TypeKind* kind = FindKind(v))
            kinds.insert(*kind);
    }
    return kinds;
}

// The C# OnExecuteAsync `EntityTypes.SelectMany(v => v.Split(',', ';'))`:
// both delimiters split at once, and String.Split(char[]) defaults to
// StringSplitOptions.None, so consecutive, leading and trailing
// delimiters yield EMPTY entries.
std::vector<std::string> SplitEntityTypeValues(const std::vector<std::string>& rawValues)
{
    std::vector<std::string> values;
    for (const std::string& raw : rawValues) {
        std::string current;
        for (char ch : raw) {
            if (ch == ',' || ch == ';') {
                values.push_back(std::move(current));
                current.clear();
            } else {
                current.push_back(ch);
            }
        }
        values.push_back(std::move(current));
    }
    return values;
}

}  // namespace ILSpy::ILSpyCmd
