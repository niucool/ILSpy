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

// Port of ICSharpCode.Decompiler/TypeSystem/TopLevelTypeName.cs. The name of a
// top-level type: namespace, name, and type-parameter count. Cannot refer to
// nested classes. A value type (the C# is a readonly struct).

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace ILSpy::Decompiler::TypeSystem {

class TopLevelTypeName {
public:
    TopLevelTypeName() = default;
    TopLevelTypeName(std::string namespaceName, std::string name, int typeParameterCount = 0)
        : namespaceName_(std::move(namespaceName)),
          name_(std::move(name)),
          typeParameterCount_(typeParameterCount) {}

    // Parse a reflection name like "System.Collections.Generic.Dictionary`2".
    explicit TopLevelTypeName(std::string_view reflectionName);

    const std::string& Namespace() const noexcept { return namespaceName_; }
    const std::string& Name() const noexcept { return name_; }
    int TypeParameterCount() const noexcept { return typeParameterCount_; }

    std::string ReflectionName() const {
        std::string r;
        if (!namespaceName_.empty()) { r += namespaceName_; r += '.'; }
        r += name_;
        if (typeParameterCount_ > 0) { r += '`'; r += std::to_string(typeParameterCount_); }
        return r;
    }

    // Split a reflection-name suffix "`<count>" off the type name, returning the
    // plain name and the count. The C# lives in ReflectionHelper; mirrored here
    // because TopLevelTypeName's reflection-name ctor depends on it.
    static std::string SplitTypeParameterCount(std::string_view reflectionName, int& outCount);

    bool operator==(const TopLevelTypeName& o) const noexcept {
        return typeParameterCount_ == o.typeParameterCount_ &&
               name_ == o.name_ && namespaceName_ == o.namespaceName_;
    }
    bool operator!=(const TopLevelTypeName& o) const noexcept { return !(*this == o); }

private:
    std::string namespaceName_;
    std::string name_;
    int typeParameterCount_ = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
