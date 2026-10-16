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

#include "Decompiler/TypeSystem/FullTypeName.hpp"

#include <string>

namespace ILSpy::Decompiler::TypeSystem {

FullTypeName::FullTypeName(std::string_view reflectionName) {
    // Split on '+': the first segment is the top-level name, the rest are nested.
    std::string s(reflectionName);
    std::string_view rest = s;
    auto plus = rest.find('+');
    std::string top = (plus == std::string_view::npos) ? std::string(rest) : std::string(rest.substr(0, plus));
    topLevel_ = TopLevelTypeName(top);
    if (plus == std::string_view::npos) return;
    rest = rest.substr(plus + 1);
    while (!rest.empty()) {
        auto next = rest.find('+');
        std::string seg = (next == std::string_view::npos) ? std::string(rest) : std::string(rest.substr(0, next));
        int tpc = 0;
        std::string name = TopLevelTypeName::SplitTypeParameterCount(seg, tpc);
        nested_.push_back({ std::move(name), tpc });
        if (next == std::string_view::npos) break;
        rest = rest.substr(next + 1);
    }
}

std::string FullTypeName::ReflectionName() const {
    std::string r = topLevel_.ReflectionName();
    for (const auto& n : nested_) {
        r += '+';
        r += n.Name;
        if (n.AdditionalTypeParameterCount > 0) {
            r += '`';
            r += std::to_string(n.AdditionalTypeParameterCount);
        }
    }
    return r;
}

std::string FullTypeName::FullName() const {
    std::string r;
    if (!topLevel_.Namespace().empty()) { r += topLevel_.Namespace(); r += '.'; }
    r += topLevel_.Name();
    for (const auto& n : nested_) { r += '.'; r += n.Name; }
    return r;
}

int FullTypeName::TypeParameterCount() const {
    int tpc = topLevel_.TypeParameterCount();
    for (const auto& n : nested_) tpc += n.AdditionalTypeParameterCount;
    return tpc;
}

} // namespace ILSpy::Decompiler::TypeSystem
