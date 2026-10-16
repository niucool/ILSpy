// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/PartialTypeInfo.cs -- the partial-type info
// the C# partial-type decompilation registry carries: the declaring type's
// token and the set of member tokens decompiled elsewhere. The C#
// `HashSet<EntityHandle>` ports to an ordered set over the raw tokens (the
// handle stand-in: the port's metadata nodes carry raw 32-bit tokens; the
// C# EntityHandle wraps the same value).

#pragma once

#include <cstdint>
#include <set>

namespace ILSpy::Decompiler::Metadata {

class PartialTypeInfo {
public:
    explicit PartialTypeInfo(std::uint32_t declaringTypeDefinitionToken)
        : declaringTypeDefinitionToken_(declaringTypeDefinitionToken) {}

    std::uint32_t DeclaringTypeDefinitionToken() const {
        return declaringTypeDefinitionToken_;
    }
    void AddDeclaredMember(std::uint32_t memberToken) {
        declaredMembers_.insert(memberToken);
    }
    bool IsDeclaredMember(std::uint32_t memberToken) const {
        return declaredMembers_.count(memberToken) != 0;
    }
    // The C# `AddDeclaredMembers(PartialTypeInfo info)`: unionize the other
    // info's declared members into this one.
    void AddDeclaredMembers(const PartialTypeInfo& other) {
        declaredMembers_.insert(other.declaredMembers_.begin(),
                                other.declaredMembers_.end());
    }

private:
    std::uint32_t declaringTypeDefinitionToken_ = 0;
    std::set<std::uint32_t> declaredMembers_;
};

} // namespace ILSpy::Decompiler::Metadata
