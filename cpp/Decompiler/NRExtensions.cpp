// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

#include "Decompiler/NRExtensions.hpp"

#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace ILSpy::Decompiler {

// The C# `type.Namespace` read over the IType interface: the port's IType carries
// no Namespace member (the documented Phase-2 deferral), so the namespace read
// routes through the NamespaceOf helper -- the ILAmbience/TypeSystemAstBuilder
// copy, "copied next to its second consumer" convention.
namespace {

std::string NamespaceOf(const TypeSystem::IType& type) {
    if (const auto* entity = dynamic_cast<const TypeSystem::IEntity*>(&type))
        return entity->Namespace();
    if (const auto* pt = dynamic_cast<const TypeSystem::ParameterizedType*>(&type))
        return pt->GenericType() ? NamespaceOf(*pt->GenericType()) : std::string();
    if (const auto* unknown =
            dynamic_cast<const class TypeSystem::UnknownType*>(&type))
        return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
    return std::string();
}

// The C# nested `class ContainsAnonTypeVisitor : TypeVisitor` (line 106): the
// two-override walk -- VisitOtherType and VisitTypeDefinition each mark the
// flag when the visited type IS an anonymous type, then delegate to the base
// default (the VisitChildren recursion into the type arguments / element
// types). Every other node kind keeps the base default, which recurses
// through the type's own VisitChildren.
class ContainsAnonTypeVisitor final : public TypeSystem::TypeVisitor {
public:
    // The C# `public bool ContainsAnonType;`
    bool ContainsAnonType = false;

    TypeSystem::ITypePtr VisitOtherType(TypeSystem::IType& type) override {
        if (IsAnonymousType(&type))
            ContainsAnonType = true;
        return TypeSystem::TypeVisitor::VisitOtherType(type);
    }

    TypeSystem::ITypePtr VisitTypeDefinition(TypeSystem::ITypeDefinition& type) override {
        if (IsAnonymousType(&type))
            ContainsAnonType = true;
        return TypeSystem::TypeVisitor::VisitTypeDefinition(type);
    }
};

} // namespace

// The C# `public static bool IsCompilerGenerated(this IEntity entity)`
// (line 28): `if (entity != null) return entity.HasAttribute(CompilerGenerated);
// return false`.
bool IsCompilerGenerated(const TypeSystem::IEntity* entity) {
    return entity != nullptr
        && entity->HasAttribute(TypeSystem::KnownAttribute::CompilerGenerated);
}

// The C# `public static bool HasGeneratedName(this IMember member)` (line 46).
bool HasGeneratedName(const TypeSystem::IMember& member) {
    return member.Name().rfind("<", 0) == 0;
}

// The C# `public static bool HasGeneratedName(this IType type)` (line 51):
// `SRMExtensions.IsGeneratedName(type.Name)`.
bool HasGeneratedName(const TypeSystem::IType& type) {
    return Metadata::IsGeneratedName(type.Name());
}

// The C# `static bool HasOnlyReadOnlyProperties(ITypeDefinition type)`
// (line 62): the C# `foreach (var property in type.Properties) if
// (property.CanSet) return false; return true;`.
bool HasOnlyReadOnlyProperties(const TypeSystem::ITypeDefinition& type) {
    for (const TypeSystem::IProperty* property : type.Properties())
    {
        if (property->CanSet())
            return false;
    }
    return true;
}

// The C# `public static bool IsAnonymousTypeDeclaredAsNamedType(
// this ITypeDefinition type)` (line 76).
bool IsAnonymousTypeDeclaredAsNamedType(const TypeSystem::ITypeDefinition& type) {
    return std::string(type.Namespace()).empty() && HasGeneratedName(type)
        && (type.Name().find("AnonType") != std::string::npos
            || type.Name().find("AnonymousType") != std::string::npos)
        && IsCompilerGenerated(&type) && !HasOnlyReadOnlyProperties(type);
}

// The C# `public static bool IsAnonymousType(this IType type)` (line 86).
bool IsAnonymousType(const TypeSystem::IType* type) {
    if (type == nullptr)
        return false;
    const TypeSystem::IType& t = *type;
    if (NamespaceOf(t).empty() && HasGeneratedName(t)
        && (t.Name().find("AnonType") != std::string::npos
            || t.Name().find("AnonymousType") != std::string::npos))
    {
        const TypeSystem::ITypeDefinition* td = t.GetDefinition();
        return td != nullptr && IsCompilerGenerated(td)
            && HasOnlyReadOnlyProperties(*td);
    }
    return false;
}

// The C# `public static bool ContainsAnonymousType(this IType type)` (line 99).
bool ContainsAnonymousType(const TypeSystem::IType& type) {
    ContainsAnonTypeVisitor visitor;
    // The AcceptVisitor contract is non-const (the port's visitor convention);
    // the walk never mutates the type, so the const reference is cast away.
    const_cast<TypeSystem::IType&>(type).AcceptVisitor(visitor);
    return visitor.ContainsAnonType;
}

} // namespace ILSpy::Decompiler
