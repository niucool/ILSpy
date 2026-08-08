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

#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <string>

namespace ILSpy::Decompiler::TypeSystem {

std::string TopLevelTypeName::SplitTypeParameterCount(std::string_view reflectionName, int& outCount) {
    auto pos = reflectionName.rfind('`');
    if (pos == std::string_view::npos) { outCount = 0; return std::string(reflectionName); }
    // Parse the digits after '`'; if they don't form an integer, leave the name
    // intact (matches the C# fallback).
    std::string digits(reflectionName.substr(pos + 1));
    try {
        size_t parsed = 0;
        int n = std::stoi(digits, &parsed);
        if (parsed == digits.size()) { outCount = n; return std::string(reflectionName.substr(0, pos)); }
    } catch (...) {}
    outCount = 0;
    return std::string(reflectionName);
}

TopLevelTypeName::TopLevelTypeName(std::string_view reflectionName) {
    auto pos = reflectionName.rfind('.');
    if (pos == std::string_view::npos) {
        namespaceName_ = "";
        name_ = SplitTypeParameterCount(reflectionName, typeParameterCount_);
    } else {
        namespaceName_ = std::string(reflectionName.substr(0, pos));
        name_ = SplitTypeParameterCount(reflectionName.substr(pos + 1), typeParameterCount_);
    }
}

} // namespace ILSpy::Decompiler::TypeSystem
