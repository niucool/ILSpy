// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the `IModuleReference` interface from
// ICSharpCode.Decompiler/TypeSystem/IAssembly.cs (lines 41-48). An `IModuleReference`
// is the module-reference counterpart of `ITypeReference` (D408): it is a 1-method
// interface that a module reference -- `DefaultAssemblyReference` (the assembly-by-
// name reference, not yet ported), the per-node `IModuleReference` impls ... --
// implements to resolve itself to an `IModule` against an `ITypeResolveContext`.
//
// The C# doc comment (IAssembly.cs lines 29-39) explains the role: the type system is
// an immutable cyclic data structure -- the compilation (`ICompilation`) has
// references to all modules, and each module has a reference back to the compilation.
// Module references break this cycle: the compilation constructor accepts module
// references, and only the `IModuleReference.Resolve()` function observes a
// partially-constructed compilation (no user code does). So a module reference is the
// deferred-resolution handle the compilation-construction path threads before the
// final `Resolve` step, structurally the same shape as `ITypeReference.Resolve` but
// producing an `IModule` (a nullable `IModule?`, NOT the non-null `IType` the type
// reference contract guarantees -- a module reference may fail to resolve and return
// null).
//
// The C# `interface IModuleReference { IModule? Resolve(ITypeResolveContext context); }`
// ports to a C++ abstract base with a virtual destructor and one pure-virtual accessor,
// the established C#-interface-to-C++-abstract-base convention (the `ITypeReference`
// (D408) / `ISymbol` (D372) / `ICompilationProvider` (D379) precedents). The C#
// `IModule? Resolve(ITypeResolveContext context)` -- NULLABLE (the C# `IModule?`,
// unlike `ITypeReference`'s non-null `IType`) -- ports to `const IModule* Resolve
// (const ITypeResolveContext&) const`: a nullable raw-pointer return (the
// `IEntity::ParentModule` / `IAttribute::Constructor` nullable-pointer precedent), a
// non-null `const ITypeResolveContext&` parameter (the D408 `ITypeReference::Resolve`
// reference-parameter precedent), and `const` because `Resolve` reads the context
// without modifying `this` (the D408 const-`Resolve` precedent).
//
// `IModule` (the D396 port, the `Resolve` return type) and `ITypeResolveContext` (the
// D409 paired context interface, the `Resolve` parameter type) are both
// FORWARD-DECLARED here -- a pointer return and a reference parameter each need only
// a forward declaration (the `ITypeReference.hpp` (D408) both-forward-declared
// precedent, and the `ICompilationProvider.hpp`-is-include-free precedent) -- so this
// header compiles with both incomplete and includes NOTHING.
//
// It is a leaf TypeSystem dependency toward `DefaultAssemblyReference` (the only
// concrete `IModuleReference`, which resolves by matching the short name against
// `context.CurrentModule` / `context.Compilation.Modules`), toward
// `TypeSystemAstBuilder` / `CSharpAmbience` (the long-pole remaining blocker of
// `CSharpAmbience`), and the paired `ITypeResolveContext` (D409). The test for this
// leaf provides a minimal `TestModuleReference` stub (the D408 `TestTypeReference`
// precedent: a concrete `IModuleReference` holding a configured `IModule` it returns
// from `Resolve`, ignoring the context), and a trivial `TestResolveContext` (the D409
// `ITypeResolveContext` concrete stand-in, backed by a compact `TestCompilation`).

#pragma once

namespace ILSpy::Decompiler::TypeSystem {

// Forward declarations: `IModule` (the D396 port, the `Resolve` return type) and
// `ITypeResolveContext` (the D409 paired context interface, the `Resolve` parameter
// type). A nullable pointer return and a reference parameter each need only a forward
// declaration (the `ITypeReference.hpp` (D408) both-forward-declared precedent), so
// this header compiles with both incomplete and includes NOTHING (the
// `ICompilationProvider.hpp`-is-include-free precedent extended to a 1-method
// interface whose only dependencies are two forward-declared TypeSystem interfaces).
class IModule;
class ITypeResolveContext;

// A reference to a metadata module. Must be resolved (via `Resolve`) against an
// `ITypeResolveContext` before it can be used as an `IModule`. A concrete module
// reference (`DefaultAssemblyReference`, not yet ported) subclasses `IModuleReference`
// and overrides `Resolve`, returning the resolved `IModule` or null when the module
// cannot be found (the C# `IModule?` nullable return, unlike the non-null `IType`
// the `ITypeReference.Resolve` contract guarantees).
//
// Module references break the compilation <-> module cycle (the C# doc comment): the
// compilation constructor accepts module references, and only `Resolve` observes a
// partially-constructed compilation, so no user code sees the cycle directly.
class IModuleReference {
public:
    virtual ~IModuleReference() = default;

    // The C# `IModule? Resolve(ITypeResolveContext context)` -- resolves this module
    // reference against `context`, returning the resolved `IModule` or null when the
    // module cannot be found. The C# `IModule?` is a nullable reference, so a nullable
    // raw-pointer return (the `IEntity::ParentModule` nullable-pointer precedent,
    // distinct from the non-null `ITypeReference::Resolve` reference return). The
    // parameter is a non-null `const ITypeResolveContext&` (the D408 reference-parameter
    // precedent). `const` because `Resolve` reads the context without modifying `this`.
    virtual const IModule* Resolve(const ITypeResolveContext& context) const = 0;
};

} // namespace ILSpy::Decompiler::TypeSystem
