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

// Port of ICSharpCode.Decompiler/CSharp/RequiredNamespaceCollector.cs -- the
// namespace set the `using` block of a decompiled file needs. The C#
// `internal class RequiredNamespaceCollector` ports to a class over the same
// `HashSet<string> namespaces` (a std::unordered_set<string> handle the caller
// owns), seeded with every known type's namespace (the C# ctor's
// KnownTypeReference loop).
//
// The C# CollectNamespaces(MetadataModule, ...) / (IEntity, MetadataModule,
// ...) static entries walk the metadata (MethodDef bodies via CodeMappingInfo,
// ResolveEntity/ResolveMethod) -- the port defers those two static entries
// loudly: the metadata walk arm (CollectNamespacesFromMethodBody's IL scan and
// the CodeMappingInfo plumbing) needs the DecodeLocalSignature /
// GetStandaloneSignature surfaces not yet ported; the type-reference walk
// (CollectNamespacesForTypeReference), the attribute handler, the
// type-parameter handler, and the member-reference collector land now, keyed
// off the TypeSystem interfaces the port carries.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

namespace ILSpy::Decompiler::Metadata {
class CodeMappingInfo;
}

namespace ILSpy::Decompiler::TypeSystem {
class IType;
class IAttribute;
class ITypeParameter;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp {

class RequiredNamespaceCollector {
public:
    // The C# ctor `RequiredNamespaceCollector(HashSet<string> namespaces)` --
    // the caller owns the set (the C# reference); the ctor seeds it with every
    // known type's namespace (the C# KnownTypeReference loop).
    explicit RequiredNamespaceCollector(
        std::unordered_set<std::string>& namespaces);

    // The C# `void CollectNamespacesForTypeReference(IType type)` is private;
    // the port exposes the type-reference walk for the caller-side entity
    // walks (the C# private members are class-local; the port's static
    // CollectNamespaces entries are the C# public API). Idempotent per type
    // (the visitedTypes gate).
    void CollectTypeReference(const TypeSystem::IType* type);

private:
    std::unordered_set<std::string>& namespaces_;
    std::unordered_set<const TypeSystem::IType*, std::hash<const void*>,
                       std::equal_to<>>
        visitedTypes_;
};

} // namespace ILSpy::Decompiler::CSharp