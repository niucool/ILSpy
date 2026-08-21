// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation, rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

// Port of ICSharpCode.Decompiler/TypeSystem/SimpleTypeResolveContext.cs -- the default
// `ITypeResolveContext` implementation (D409). A `SimpleTypeResolveContext` bundles the
// parent `ICompilation` (inherited from `ICompilationProvider`) with the three nullable
// current-module / current-type-definition / current-member slots, and provides the two
// `With*` factory methods that return a NEW context with one slot replaced (the others
// carried over). It is the concrete `ITypeResolveContext` the resolution paths construct:
// `KnownTypeReference.Resolve` (not yet ported) receives one and reaches
// `context.Compilation.FindType(...)` through it.
//
// The C# `sealed class SimpleTypeResolveContext : ITypeResolveContext` ports to a C++
// `final` concrete class deriving from `ITypeResolveContext` (D409). It is a leaf
// TypeSystem dependency toward `KnownTypeReference` (the first concrete `ITypeReference`,
// which resolves via `context.Compilation.FindType`) and toward `TypeSystemAstBuilder` /
// `CSharpAmbience` (the long-pole remaining blocker of `CSharpAmbience`).
//
// KEY PORT CONVENTIONS:
//  (a) The four C# fields (`compilation` / `currentModule` / `currentTypeDefinition` /
//      `currentMember`) port to a `const ICompilation&` reference member (the non-null
//      parent compilation) plus three nullable raw pointer members (the nullable slots,
//      the `IEntity::ParentModule` / `IAttribute::Constructor` nullable-pointer
//      precedent). A reference member mirrors the non-null C# `ICompilation compilation`
//      (the C# ctors throw `ArgumentNullException` on null); the three nullable C#
//      reference-typed slots (`IModule?` / `ITypeDefinition?` / `IMember?`) port to raw
//      pointers where a null pointer is the C# `null`.
//  (b) The three C# public ctors (`ICompilation` / `IModule` / `IEntity`) each throw
//      `ArgumentNullException` on a null argument. The C++ ctors take REFERENCE parameters
//      (`const ICompilation&` / `const IModule&` / `const IEntity&`), which are non-null by
//      C++ reference semantics, so the C# null check is N/A in the port (a reference
//      parameter cannot be null; the caller must pass a valid object). The C# `ICompilation`
//      ctor sets only the compilation (the three slots null); the `IModule` ctor sets the
//      compilation from `module.Compilation` and the `currentModule` slot; the `IEntity` ctor
//      sets the compilation from `entity.Compilation`, the `currentModule` from
//      `entity.ParentModule`, the `currentTypeDefinition` from `(entity as ITypeDefinition)
//      ?? entity.DeclaringTypeDefinition`, and the `currentMember` from `entity as IMember`.
//  (c) The C# `entity as ITypeDefinition` / `entity as IMember` downcasts port to
//      `dynamic_cast<const ITypeDefinition*>(&entity)` / `dynamic_cast<const IMember*>
//      (&entity)` -- `ITypeDefinition` (D393) and `IMember` (D387) are both polymorphic
//      abstract bases deriving from `IEntity` (D381), so `dynamic_cast` from an `IEntity*`
//      to either is a well-defined cross-type downcast. The `(entity as ITypeDefinition) ??
//      entity.DeclaringTypeDefinition` coalesce ports to a private static helper that
//      returns the dynamic_cast result when non-null, otherwise `entity.DeclaringTypeDefinition()`.
//  (d) The private C# 4-slot ctor (`ICompilation, IModule, ITypeDefinition, IMember`) is
//      the one the `With*` factories call via `new SimpleTypeResolveContext(...)`. The C++
//      port keeps it private and the `With*` methods use `std::unique_ptr<ITypeResolveContext>
//      (new SimpleTypeResolveContext(...))` (NOT `std::make_unique`, which may not access a
//      private ctor across all implementations; a direct `new` in the member-function body
//      definitively has access to the class's own private ctor). The `With*` methods return
//      `std::unique_ptr<ITypeResolveContext>` (the D409 convention -- the faithful C++
//      counterpart of the C# `new SimpleTypeResolveContext(...)` heap allocation transferred
//      to the caller).
//  (e) All six overrides (`Compilation` / `CurrentModule` / `CurrentTypeDefinition` /
//      `CurrentMember` / `WithCurrentTypeDefinition` / `WithCurrentMember`) are `const`
//      (faithful to the C# instance methods; the `With*` factories read `this` without
//      modifying it). The class is `final` (the C# `sealed`).

#pragma once

#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"

#include <memory>

namespace ILSpy::Decompiler::TypeSystem {

// The default `ITypeResolveContext` implementation. Holds the four slots (the parent
// `ICompilation` + three nullable current-module / type-definition / member pointers)
// and implements the two `With*` factories as a `new SimpleTypeResolveContext` with one
// slot replaced and the others carried over. The class is `final` (the C# `sealed`);
// a `SimpleTypeResolveContext` IS-A `ITypeResolveContext` and dispatches through the
// base pointer.
class SimpleTypeResolveContext final : public ITypeResolveContext {
private:
    // The C# `(entity as ITypeDefinition) ?? entity.DeclaringTypeDefinition` coalesce -- if
    // the entity IS a type definition (the `dynamic_cast` succeeds), return it; otherwise
    // fall back to the entity's declaring type definition (null for a top-level entity). A
    // private static helper so the `IEntity` ctor's member-init list can call it (the helper
    // is declared before the public ctors so it is visible in their member-init lists).
    static const ITypeDefinition* EntityTypeDefinitionOrDeclaring(const IEntity& entity)
    {
        if (auto* td = dynamic_cast<const ITypeDefinition*>(&entity))
            return td;
        return entity.DeclaringTypeDefinition();
    }

    // The C# private 4-slot ctor the `With*` factories use. `compilation` is a non-null
    // reference (the C# field); the three slot parameters are nullable pointers (the C#
    // nullable reference-typed slots). Private so only the `With*` factories (and the
    // public ctors that delegate to it) can construct the 4-slot form.
    SimpleTypeResolveContext(const ICompilation& compilation,
                              const IModule* currentModule,
                              const ITypeDefinition* currentTypeDefinition,
                              const IMember* currentMember)
        : compilation_(compilation),
          currentModule_(currentModule),
          currentTypeDefinition_(currentTypeDefinition),
          currentMember_(currentMember) {}

public:
    // The C# `SimpleTypeResolveContext(ICompilation compilation)` -- the simplest ctor:
    // holds the compilation, leaves the three slots null. The C# `ArgumentNullException` on
    // a null compilation is N/A in C++ (a reference parameter is non-null by C++ semantics).
    explicit SimpleTypeResolveContext(const ICompilation& compilation)
        : compilation_(compilation),
          currentModule_(nullptr),
          currentTypeDefinition_(nullptr),
          currentMember_(nullptr) {}

    // The C# `SimpleTypeResolveContext(IModule module)` -- holds the module's compilation
    // (via `module.Compilation`, the inherited `ICompilationProvider::Compilation`) and the
    // module itself as the current module. The C# `ArgumentNullException` on a null module
    // is N/A in C++ (a reference parameter is non-null).
    explicit SimpleTypeResolveContext(const IModule& module)
        : compilation_(module.Compilation()),
          currentModule_(&module),
          currentTypeDefinition_(nullptr),
          currentMember_(nullptr) {}

    // The C# `SimpleTypeResolveContext(IEntity entity)` -- holds the entity's compilation
    // (`entity.Compilation`), the entity's parent module (`entity.ParentModule`), the
    // entity's type definition (`(entity as ITypeDefinition) ?? entity.DeclaringTypeDefinition`
    // -- the entity itself when it IS a type definition, otherwise its declaring type
    // definition), and the entity itself as the current member (`entity as IMember` -- null
    // when the entity is not a member). The C# `ArgumentNullException` on a null entity is
    // N/A in C++ (a reference parameter is non-null). The `as` downcasts port to
    // `dynamic_cast` (the D393 `ITypeDefinition` / D387 `IMember` are polymorphic `IEntity`
    // derivations).
    SimpleTypeResolveContext(const IEntity& entity)
        : compilation_(entity.Compilation()),
          currentModule_(entity.ParentModule()),
          currentTypeDefinition_(EntityTypeDefinitionOrDeclaring(entity)),
          currentMember_(dynamic_cast<const IMember*>(&entity)) {}

    // --- ICompilationProvider (inherited) ---
    const ICompilation& Compilation() const override { return compilation_; }

    // --- ITypeResolveContext ---
    const IModule* CurrentModule() const override { return currentModule_; }
    const ITypeDefinition* CurrentTypeDefinition() const override { return currentTypeDefinition_; }
    const IMember* CurrentMember() const override { return currentMember_; }

    // The C# `WithCurrentTypeDefinition(ITypeDefinition typeDefinition)` -- returns a NEW
    // context with the current-type-definition slot replaced by `typeDefinition` and the
    // other slots (module / member / compilation) carried over. `std::unique_ptr` (the D409
    // convention); `const` (the factory reads `this` without modifying it); nullable pointer
    // parameter (pass `nullptr` to clear the slot). Uses a direct `new` (not `make_unique`)
    // so the private 4-slot ctor is accessible from the class's own member function.
    std::unique_ptr<ITypeResolveContext> WithCurrentTypeDefinition(
        const ITypeDefinition* typeDefinition) const override
    {
        return std::unique_ptr<ITypeResolveContext>(
            new SimpleTypeResolveContext(
                compilation_, currentModule_, typeDefinition, currentMember_));
    }

    // The C# `WithCurrentMember(IMember member)` -- returns a NEW context with the current-
    // member slot replaced by `member` and the other slots carried over. The twin of the
    // type-definition factory.
    std::unique_ptr<ITypeResolveContext> WithCurrentMember(
        const IMember* member) const override
    {
        return std::unique_ptr<ITypeResolveContext>(
            new SimpleTypeResolveContext(
                compilation_, currentModule_, currentTypeDefinition_, member));
    }

private:
    const ICompilation& compilation_;
    const IModule* currentModule_;
    const ITypeDefinition* currentTypeDefinition_;
    const IMember* currentMember_;
};

} // namespace ILSpy::Decompiler::TypeSystem
