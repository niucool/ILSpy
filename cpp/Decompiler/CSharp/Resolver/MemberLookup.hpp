// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify,
// merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to the following
// conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/MemberLookup.cs -- the
// implementation of member lookup (C# 4.0 spec, 7.4).
//
// PORTED here (the accessibility surface consumed by
// `TypeSystemAstBuilder::TypeDefinitionNameableInBaseList`
// (`lookup.IsAccessible(td, false)`) and the CSharpResolver leaf deps): the
// static `IsInvocable` helper, the instance ctor (storing
// currentTypeDefinition / currentModule / isInEnumMemberInitializer), and the
// "IsAccessible" region verbatim -- IsProtectedAccessAllowed (both overloads),
// IsAccessible (the 3.5.2 accessibility switch), IsInternalAccessible,
// IsProtectedAccessible.
//
// DEFERRED (they enumerate members/nested types through the C# GetMembers /
// GetNestedTypes GetMemberOptions surface the minimal port does not carry):
// the `LookupGroup` class, `GetAccessibleMembers`, `LookupType`, `Lookup`, and
// the AddNestedTypes / AddMembers / RemoveInterfaceMembersHiddenByClassMembers
// helpers. The deferred region also covers the private
// `IsProtectedAccessible`'s DerivesFrom walk partners; nothing else in the
// ported region is affected.

#pragma once

#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `public class MemberLookup` -- a self-contained value object: a
// lookup context (the type the lookup runs in, the module for internal access,
// the enum-member-initializer flag) offering the accessibility queries above.
// NOT final (the C# is unsealed); no virtual members.
class MemberLookup {
public:
    // -------------------------------------------------------------------
    // Static helper methods
    // -------------------------------------------------------------------

    // The C# `public static bool IsInvocable(IMember member)`:
    // "Gets whether the member is considered to be invocable."
    // The C# member == null throws ArgumentNullException; the port takes a
    // `const IMember&` (references are never null, the IVariable::Type()
    // convention).
    static bool IsInvocable(const ILSpy::Decompiler::TypeSystem::IMember& member)
    {
        // C# 4.0 spec, 7.4 member lookup
        using namespace ILSpy::Decompiler::TypeSystem;
        // C#: `member is IEvent || member is IMethod` -- the port dispatches on
        // SymbolKind (the dynamic_cast-free runtime-type test the IMember
        // hierarchy provides, faithful to the C# `is` checks on the sealed
        // event/method interfaces).
        if (member.SymbolKind() == SymbolKind::Event || member.SymbolKind() == SymbolKind::Method)
            return true;
        const IType& returnType = member.ReturnType();
        return returnType.Kind() == TypeKind::Dynamic || returnType.Kind() == TypeKind::Delegate
            || returnType.Kind() == TypeKind::FunctionPointer;
    }

    // The C#
    //   `MemberLookup(ITypeDefinition currentTypeDefinition, IModule currentModule,
    //                 bool isInEnumMemberInitializer = false)`
    // Both C# parameters are nullable (the C# `CreateMemberLookup` passes a
    // null `currentTypeDefinition` when no type context exists, e.g. top-level
    // statements), so the port keeps raw nullable pointers (the
    // D374-documented nullable-pointer precedent); the references observe the
    // context objects (non-owning).
    MemberLookup(const ILSpy::Decompiler::TypeSystem::ITypeDefinition* currentTypeDefinition,
                 const ILSpy::Decompiler::TypeSystem::IModule* currentModule,
                 bool isInEnumMemberInitializer = false)
        : currentTypeDefinition_(currentTypeDefinition),
          currentModule_(currentModule),
          isInEnumMemberInitializer_(isInEnumMemberInitializer)
    {
    }

    // The C# `bool isInEnumMemberInitializer` readonly field (read by the deferred `CreateResult`
    // Lookup-region helper, D499, for the enum-field constant `MemberResolveResult` arm). A public
    // accessor (the field is private; the `Detail::CreateResult` free function reads it through this).
    bool IsInEnumMemberInitializer() const { return isInEnumMemberInitializer_; }

    // -------------------------------------------------------------------
    // IsAccessible
    // -------------------------------------------------------------------

    // The C# `public bool IsProtectedAccessAllowed(ResolveResult targetResolveResult)`:
    // "Gets whether access to protected instance members of the target
    // expression is possible." The C# ArgumentNullException ports to a
    // `const ResolveResult&` (the non-null reference convention).
    bool IsProtectedAccessAllowed(const ILSpy::Decompiler::Semantics::ResolveResult& targetResolveResult) const
    {
        return dynamic_cast<const ILSpy::Decompiler::Semantics::ThisResolveResult*>(&targetResolveResult) != nullptr
            || IsProtectedAccessAllowed(targetResolveResult.Type());
    }

    // The C# `public bool IsProtectedAccessAllowed(IType targetType)`:
    // "Gets whether access to protected instance members of the target type is
    // possible." (This method does not consider the special case of the 'base'
    // reference; the ResolveResult overload covers it via ThisResolveResult.)
    bool IsProtectedAccessAllowed(const ILSpy::Decompiler::TypeSystem::IType& targetType) const
    {
        using namespace ILSpy::Decompiler::TypeSystem;
        const IType* target = &targetType;
        if (target->Kind() == TypeKind::TypeParameter) {
            // C# `targetType = ((ITypeParameter)targetType).EffectiveBaseClass`
            // (a single assignment, then GetDefinition below). The C# would
            // dereference a null effective base class (NRE); the port returns
            // false instead (the no-crash robustness tenet).
            ITypePtr baseClass = static_cast<const ITypeParameter&>(*target).EffectiveBaseClass();
            if (baseClass == nullptr)
                return false;
            target = baseClass.get();
        }
        const ITypeDefinition* typeDef = target->GetDefinition();
        if (typeDef == nullptr)
            return false;
        for (const ITypeDefinition* c = currentTypeDefinition_; c != nullptr;
             c = c->DeclaringTypeDefinition()) {
            if (IsDerivedFrom(*typeDef, c))
                return true;
        }
        return false;
    }

    // The C# `public bool IsAccessible(IEntity entity, bool allowProtectedAccess)`:
    // "Gets whether `entity` is accessible in the current class." The C#
    // ArgumentNullException ports to a `const IEntity&`.
    //
    // C# 4.0 spec, 3.5.2 Accessibility domains. The C# default arm throws
    // `new Exception("Invalid value for Accessibility")`; the port throws
    // `std::logic_error` (an enum outside the declared range is a caller-side
    // contract violation, the FullTypeName::GetNestedTypeName logic_error
    // precedent).
    bool IsAccessible(const ILSpy::Decompiler::TypeSystem::IEntity& entity, bool allowProtectedAccess) const
    {
        using namespace ILSpy::Decompiler::TypeSystem;
        switch (entity.Accessibility()) {
            case Accessibility::None:
                return false;
            case Accessibility::Private:
                // check for members of outer classes (private members of outer
                // classes can be accessed)
                for (const ITypeDefinition* t = currentTypeDefinition_; t != nullptr;
                     t = t->DeclaringTypeDefinition()) {
                    if (t == entity.DeclaringTypeDefinition())
                        return true;
                }
                return false;
            case Accessibility::Public:
                return true;
            case Accessibility::Protected:
                return IsProtectedAccessible(allowProtectedAccess, entity);
            case Accessibility::Internal:
                return IsInternalAccessible(entity.ParentModule());
            case Accessibility::ProtectedOrInternal:
                return IsInternalAccessible(entity.ParentModule())
                    || IsProtectedAccessible(allowProtectedAccess, entity);
            case Accessibility::ProtectedAndInternal:
                return IsInternalAccessible(entity.ParentModule())
                    && IsProtectedAccessible(allowProtectedAccess, entity);
        }
        throw std::logic_error("MemberLookup::IsAccessible: Invalid value for Accessibility");
    }

    // -------------------------------------------------------------------
    // Lookup region (D500) -- the public Lookup methods composing the `Detail::` helpers.
    // -------------------------------------------------------------------

    // The C# `public ResolveResult Lookup(ResolveResult targetResolveResult, string name,
    // IReadOnlyList<IType> typeArguments, bool isInvocation)`. Defined out-of-line in `MemberLookup.cpp`
    // (composes the `Detail::` helpers from `LookupHelpers.hpp`, which includes this header -- a header
    // cycle if inline). Returns an owning `std::shared_ptr<ResolveResult>` (the C# returns a GC-owned
    // reference). The `name`/`typeArguments` are taken by value (the C# `string`/`IReadOnlyList<IType>`,
    // passed through to `CreateResult`).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> Lookup(
        const ILSpy::Decompiler::Semantics::ResolveResult& targetResolveResult,
        std::string name,
        std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
        bool isInvocation);

private:
    // The C# `bool IsInternalAccessible(IModule module)`.
    bool IsInternalAccessible(const ILSpy::Decompiler::TypeSystem::IModule* module) const
    {
        return module != nullptr && currentModule_ != nullptr
            && module->InternalsVisibleTo(*currentModule_);
    }

    // The C# `bool IsProtectedAccessible(bool allowProtectedAccess, IEntity entity)`.
    bool IsProtectedAccessible(bool allowProtectedAccess,
                               const ILSpy::Decompiler::TypeSystem::IEntity& entity) const
    {
        using namespace ILSpy::Decompiler::TypeSystem;
        // For static members and type definitions, we do not require the
        // qualifying reference to be derived from the current class
        // (allowProtectedAccess).
        if (entity.IsStatic() || entity.SymbolKind() == SymbolKind::TypeDefinition)
            allowProtectedAccess = true;

        for (const ITypeDefinition* t = currentTypeDefinition_; t != nullptr;
             t = t->DeclaringTypeDefinition()) {
            if (t == entity.DeclaringTypeDefinition())
                return true;

            // PERF: this might hurt performance as this method is called
            // several times (once for each member); make sure resolving base
            // types is cheap (caches?) or cache within the MemberLookup
            // instance.
            if (allowProtectedAccess && IsDerivedFrom(*t, entity.DeclaringTypeDefinition()))
                return true;
        }
        return false;
    }

    // The C# readonly instance fields (the lookup context). Both nullable; the
    // flag is a plain copy. `isInEnumMemberInitializer_` is stored faithfully
    // even though only the deferred Lookup/LookupType region consumes it.
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* currentTypeDefinition_;
    const ILSpy::Decompiler::TypeSystem::IModule* currentModule_;
    bool isInEnumMemberInitializer_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
