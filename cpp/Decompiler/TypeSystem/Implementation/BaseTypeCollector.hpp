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

// Port of ICSharpCode.Decompiler/TypeSystem/Implementation/BaseTypeCollector.cs --
// the helper class behind the `GetAllBaseTypes()` implementation (the C#
// `sealed class BaseTypeCollector : List<IType>`). The collector performs the
// depth-first, add-at-the-end traversal of the DirectBaseTypes relation the
// TypeSystemExtensions base-type queries (`GetAllBaseTypes` /
// `GetNonInterfaceBaseTypes` / `GetAllBaseTypeDefinitions` / `IsDerivedFrom`)
// return, so base types occur before derived types in the output and each type
// occurs exactly once even in the presence of diamond inheritance.
//
// Construct, optionally set `SkipImplementedInterfaces`, call
// `CollectBaseTypes(type)`, then read the accumulated `Types()`.

#pragma once

// ITypeDefinition.hpp must be INCLUDED (not forward-declared): the
// `GetDefinition() ?? type` fallback static_casts the `const ITypeDefinition*`
// to the `const IType*` base, which requires the complete type (the
// MethodGroupResolveResult IMethod/IParameterizedMember downcast precedent).
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"

#include <algorithm>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem::Implementation {

// The C# `sealed class BaseTypeCollector : List<IType>` -- the output list is
// the collector object itself. The port keeps the traversal state instead of
// deriving from a list (the C++ convention of composition over the C#
// `class X : List<T>` pattern); `final` (the C# is sealed).
//
// The C# `this.Contains(type)` / `activeTypes.Contains(def)` use IType.Equals
// (structural equality). The port compares raw POINTERS (reference identity):
// the duplicate-suppression and cycle-termination guarantees the checks protect
// both operate on traversal-revisited instances (a diamond's shared base
// instance, a cycle back-edge), where identity is both sufficient and --
// unlike structural equality -- never collapses two distinct instances that
// merely compare equal (two unnamed UnknownType stubs, ...).
class BaseTypeCollector final {
public:
    BaseTypeCollector() = default;

    // The C# `internal bool SkipImplementedInterfaces` -- when set, the list
    // will not contain interfaces when retrieving the base types of a class
    // (used by GetNonInterfaceBaseTypes, which keeps base interfaces when the
    // input type itself is an interface or a type parameter -- the
    // `def.Kind != Interface && def.Kind != TypeParameter` guard).
    bool SkipImplementedInterfaces = false;

    // The C# `void CollectBaseTypes(IType type)`. The C# throws
    // ArgumentNullException on a null `type` in the TypeSystemExtensions
    // wrapper; the port takes a `const IType&` here (references are never
    // null, the IVariable::Type() non-null convention) -- the wrapper checks.
    void CollectBaseTypes(const IType& type)
    {
        CollectBaseTypesRec(type);
    }

    // The accumulated output list (base types before derived types, each
    // traversal-revisited instance exactly once). Raw non-owning pointers into
    // the visited types (the caller outlives them; the same
    // observe-don't-own convention as the Resolver const-pointer buckets).
    const std::vector<const IType*>& Types() const { return types_; }

private:
    void CollectBaseTypesRec(const IType& type)
    {
        const IType* def = type.GetDefinition() != nullptr
            ? static_cast<const IType*>(type.GetDefinition())
            : &type;

        // Maintain a stack of currently active type definitions, and avoid
        // having one definition multiple times on that stack. This is necessary
        // to ensure the output is finite in the presence of cyclic inheritance:
        // `class C<X> : C<C<X>> {}` would not be caught by the 'no duplicate
        // output' check, yet would produce infinite output.
        // Non-definitions are pushed too, e.g. for protecting against cyclic
        // inheritance in type parameters (where T : S where S : T). The output
        // check doesn't help there because the type is added only at the end,
        // and adding it at the start would produce an incorrect order.
        if (std::find(activeTypes_.begin(), activeTypes_.end(), def) != activeTypes_.end())
            return;
        activeTypes_.push_back(def);

        // Avoid outputting a type more than once - necessary for "diamond"
        // multiple inheritance (e.g. C implements I1 and I2, and both interfaces
        // derive from Object).
        if (std::find(types_.begin(), types_.end(), &type) == types_.end()) {
            for (const ITypePtr& baseTypePtr : type.DirectBaseTypes()) {
                const IType* baseType = baseTypePtr.get();
                if (baseType == nullptr)
                    continue; // the C# never yields a null IType; a null promise
                              // from a DirectBaseTypes override is skipped, not
                              // dereferenced (the no-crash robustness tenet).
                if (SkipImplementedInterfaces && def->Kind() != TypeKind::Interface
                    && def->Kind() != TypeKind::TypeParameter
                    && baseType->Kind() == TypeKind::Interface) {
                    // skip the interface
                    continue;
                }
                CollectBaseTypesRec(*baseType);
            }
            // Add at the end - a type is output only after all its base types
            // were added. This is not the same as adding at the start and then
            // reversing the list: for diamond inheritance, add-at-start produces
            // "C, I1, Object, I2", while add-at-end produces "Object, I1, I2, C".
            types_.push_back(&type);
        }
        activeTypes_.pop_back();
    }

    std::vector<const IType*> activeTypes_;
    std::vector<const IType*> types_;
};

} // namespace ILSpy::Decompiler::TypeSystem::Implementation
