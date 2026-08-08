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

// Port of ICSharpCode.Decompiler/TypeSystem/FullTypeName.cs. The full name of a
// type definition: a top-level name plus zero or more nested-type segments. A
// full type name uniquely identifies a type definition within an assembly. It
// does not carry type arguments, arrays, or pointers. Reflection-name syntax:
//   NamespaceName '.' TopLevelTypeName ['`'#] { '+' NestedTypeName ['`'#] }

#pragma once

#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {

class FullTypeName {
public:
    FullTypeName() = default;
    explicit FullTypeName(TopLevelTypeName topLevel) : topLevel_(std::move(topLevel)) {}

    // Parse a reflection name. Supports nesting via '+', e.g.
    // "System.Collections.Generic.Dictionary`2+Enumerator".
    explicit FullTypeName(std::string_view reflectionName);

    bool IsNested() const noexcept { return !nested_.empty(); }
    int NestingLevel() const noexcept { return static_cast<int>(nested_.size()); }

    const TopLevelTypeName& GetTopLevelTypeName() const noexcept { return topLevel_; }

    // The innermost name (for a nested type) or the top-level name.
    std::string Name() const {
        return nested_.empty() ? topLevel_.Name() : nested_.back().Name;
    }

    std::string ReflectionName() const;
    std::string FullName() const;
    int TypeParameterCount() const;

    bool operator==(const FullTypeName& o) const noexcept {
        return topLevel_ == o.topLevel_ && nested_ == o.nested_;
    }
    bool operator!=(const FullTypeName& o) const noexcept { return !(*this == o); }

private:
    struct Nested {
        std::string Name;
        int AdditionalTypeParameterCount;
        bool operator==(const Nested& o) const noexcept {
            return AdditionalTypeParameterCount == o.AdditionalTypeParameterCount && Name == o.Name;
        }
    };
    TopLevelTypeName topLevel_;
    std::vector<Nested> nested_;
};

} // namespace ILSpy::Decompiler::TypeSystem
