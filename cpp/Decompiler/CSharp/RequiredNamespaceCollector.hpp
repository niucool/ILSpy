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
// The metadata walk arms: the entity-level CollectNamespaces (the TypeDef /
// Field / Method / Property / Event dispatch, with the method's
// CodeMappingInfo parts each walked for attributes / return type / parameters
// / type parameters / overrides / the IL body), the IL-body scan (the
// local-signature decode, the exception-handler catch types, and the
// Field/Method/Sig/Tok/Type operand walk over the decoded tokens), the
// attribute handler (the attribute type's namespace + the fixed/named
// argument values), and the member-reference collector. The port's
// metadata-walk needs the ResolveEntity / ResolveType / ResolveMethod /
// DecodeLocalSignature surfaces, all landed.

#pragma once

#include <any>
#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
class CodeMappingInfo;
class MetadataFile;
} // namespace ILSpy::Decompiler::Metadata

namespace ILSpy::Decompiler::TypeSystem {
class IEntity;
class IType;
class IAttribute;
class ITypeParameter;
class MetadataModule;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp {

class RequiredNamespaceCollector {
public:
    // The C# ctor `RequiredNamespaceCollector(HashSet<string> namespaces)` --
    // the caller owns the set (the C# reference); the ctor seeds it with every
    // known type's namespace (the C# KnownTypeReference loop).
    explicit RequiredNamespaceCollector(
        std::unordered_set<std::string>& namespaces,
        bool seedKnownTypeNamespaces = true,
        // The flat -t render's minimal using set: skip the attribute
        // types the transforms strip from every render (the
        // auto-property's CompilerGenerated/DebuggerBrowsable, the
        // de-sugar's state machine attributes, the closure debugger
        // attributes) -- a using for their namespaces is never required.
        bool minimalUsingSet = false);

    // The C# `void CollectNamespacesForTypeReference(IType type)` is private;
    // the port exposes the type-reference walk for the caller-side entity
    // walks (the C# private members are class-local; the port's static
    // CollectNamespaces entries are the C# public API). Idempotent per type
    // (the visitedTypes gate).
    void CollectTypeReference(const TypeSystem::IType* type);

    // The C# `void HandleAttributes(IEnumerable<IAttribute>)` is private; the
    // port exposes it for the attribute-driven callers (the
    // GetAssemblyAttributes / GetModuleAttributes consumers walk the
    // snapshot). Idempotent per attribute-type (the visited gate).
    void HandleAttributes(
        const std::vector<const TypeSystem::IAttribute*>& attributes);

    // The C# private members are class-local; the port's free helpers (the
    // CollectNamespacesEntity walk lives in the .cpp) access the namespace
    // set through this accessor (the C# `this.namespaces` references).
    std::unordered_set<std::string>& Namespaces() { return namespaces_; }

private:
    // The C# `void HandleAttributeValue(IType type, object? value)` -- the
    // typeof-type values recurse.
    void HandleAttributeValue(const TypeSystem::IType* type, const std::any& value);

    std::unordered_set<std::string>& namespaces_;
    bool minimalUsingSet_ = false;
    std::unordered_set<const TypeSystem::IType*, std::hash<const void*>,
                       std::equal_to<>>
        visitedTypes_;
};

// The C# `public static void CollectNamespaces(IEntity entity, MetadataModule
// module, HashSet<string> namespaces)` (the third overload): the entity-level
// walk over the given definition. The C# CodeMappingInfo plumbing (the
// method-body IL scan) runs through the ported Metadata walk.
void CollectNamespaces(
    const TypeSystem::IEntity& entity,
    TypeSystem::MetadataModule& module,
    std::unordered_set<std::string>& namespaces);

// The flat -t render's minimal using set: the entity walk without the
// known-type candidate seeding and without the implicit base types
// (System.Object / System.ValueType / System.Enum -- the render elides
// them from every base list, so a using for their namespace is never
// required).
void CollectRequiredNamespaces(
    const TypeSystem::IEntity& entity,
    TypeSystem::MetadataModule& module,
    std::unordered_set<std::string>& namespaces);

// The module-wide form: every type definition plus the assembly/module
// attribute sweep, under the minimal using-set restrictions (see the
// entity form).
void CollectRequiredNamespaces(
    TypeSystem::MetadataModule& module,
    std::unordered_set<std::string>& namespaces);

// The C# `public static void CollectAttributeNamespaces(MetadataModule,
// HashSet<string>)`: the assembly + module attribute sweep only.
void CollectAttributeNamespaces(
    TypeSystem::MetadataModule& module,
    std::unordered_set<std::string>& namespaces);

// The C# `public static void CollectNamespaces(MetadataModule module,
// HashSet<string> namespaces)` -- the module-wide walk (every type definition
// plus the assembly/module attribute sweep).
void CollectNamespaces(
    TypeSystem::MetadataModule& module,
    std::unordered_set<std::string>& namespaces);

} // namespace ILSpy::Decompiler::CSharp