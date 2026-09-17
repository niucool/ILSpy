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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/TransformContext.cs -- the
// per-transform state bag the `IAstTransform` passes read: the type-system /
// compilation, the `DecompileRun` (settings + gathered namespaces), the
// decompilation position (current module/type/member), and the shared
// `TypeSystemAstBuilder`.
//
// The C# `public readonly IDecompilerTypeSystem TypeSystem` ports as the port's
// narrower `ICompilation` surface: every ported consumer only
// reads `FindType` / `MainModule` / `RootNamespace` off it, and the port has no
// `IDecompilerTypeSystem` interface (the ExpressionBuilder already takes the two
// narrower interfaces directly).
//
// Deferrals (each named at the member that needs it): the `CancellationToken` is a
// no-op in the port (the DecompileRun convention); the `Stepper` and the
// `[Conditional("STEP")]` debug-step methods are no-ops (the port has no debug-step
// machinery -- the C# methods compile out of a normal build, so a normal C# run is
// a no-op too); and the `DecompileRun` readers some later transforms use
// (`DocumentationProvider` / `RecordDecompilers`) stay with their DecompileRun
// slices.

#pragma once

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeResolveContext.hpp"

#include <string>
#include <unordered_set>

// The type-system entity types the position accessors return, forward-declared
// (the accessors return non-owning pointers the caller does not dereference past
// the context's lifetime).
namespace ILSpy::Decompiler::TypeSystem {
class IMember;
class IModule;
class ITypeDefinition;
}

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class TransformContext`. Stores non-owning references to the
// caller-owned type system / run / position / ast-builder (the C# GC-reference
// convention) and exposes the C# public read surface.
class TransformContext {
public:
    // The C# `internal TransformContext(IDecompilerTypeSystem typeSystem,
    // DecompileRun decompileRun, ITypeResolveContext decompilationContext,
    // TypeSystemAstBuilder typeSystemAstBuilder)`. All four are caller-owned
    // (the C# holds GC references); the port stores pointers/references.
    TransformContext(const ::ILSpy::Decompiler::TypeSystem::ICompilation& typeSystem,
                     const ::ILSpy::Decompiler::DecompileRun& decompileRun,
                     const ::ILSpy::Decompiler::TypeSystem::ITypeResolveContext&
                         decompilationContext,
                     Syntax::TypeSystemAstBuilder& typeSystemAstBuilder)
        : typeSystem_(&typeSystem),
          decompileRun_(&decompileRun),
          decompilationContext_(&decompilationContext),
          typeSystemAstBuilder_(&typeSystemAstBuilder)
    {
    }

    // The C# `public readonly IDecompilerTypeSystem TypeSystem` -- the port's
    // narrower `ICompilation&` (see the header note). The accessor shares the
    // `TypeSystem` name with the type-system namespace; C++ resolves the member
    // function name in the class scope, so callers write `context.TypeSystem()`
    // and the definition is qualified here.
    const ::ILSpy::Decompiler::TypeSystem::ICompilation& TypeSystem() const {
        return *typeSystem_;
    }

    // The C# `public readonly TypeSystemAstBuilder TypeSystemAstBuilder`.
    Syntax::TypeSystemAstBuilder& TypeSystemAstBuilder() const {
        return *typeSystemAstBuilder_;
    }

    // The C# `public readonly DecompileRun DecompileRun` (and `public DecompilerSettings
    // Settings => DecompileRun.Settings`). The accessor shares the `DecompileRun`
    // name with the type, so the return types are fully qualified.
    const ::ILSpy::Decompiler::DecompileRun& DecompileRun() const { return *decompileRun_; }
    const ::ILSpy::Decompiler::DecompilerSettings& Settings() const {
        return decompileRun_->Settings();
    }

    // The C# `public IMember? CurrentMember => decompilationContext.CurrentMember`.
    const ::ILSpy::Decompiler::TypeSystem::IMember* CurrentMember() const {
        return decompilationContext_->CurrentMember();
    }

    // The C# `public ITypeDefinition? CurrentTypeDefinition`.
    const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition* CurrentTypeDefinition() const {
        return decompilationContext_->CurrentTypeDefinition();
    }

    // The C# `public IModule? CurrentModule`.
    const ::ILSpy::Decompiler::TypeSystem::IModule* CurrentModule() const {
        return decompilationContext_->CurrentModule();
    }

    // The C# `public IImmutableSet<string> RequiredNamespacesSuperset =>
    // DecompileRun.Namespaces.ToImmutableHashSet()`. The port returns a plain
    // set copy (the C# immutable set has no ported counterpart); an unset
    // `Namespaces` (the pre-collector state) yields the empty set.
    std::unordered_set<std::string> RequiredNamespacesSuperset() const {
        if (const std::optional<std::unordered_set<std::string>>& namespaces =
                decompileRun_->Namespaces();
            namespaces.has_value())
        {
            return *namespaces;
        }
        return {};
    }

    // The C# `[Conditional("STEP")] void Step(string description, AstNode? near = null)`
    // and the group/end companions. `[Conditional]` compiles the calls out of a
    // normal C# build, so the port's no-op bodies are behaviourally identical.
    void Step(const std::string& /*description*/, const Syntax::AstNode* /*near*/ = nullptr) const
    {
    }
    void Step(const std::string& /*description*/) const {}
    void StepStartGroup(const std::string& /*description*/,
                        const Syntax::AstNode* /*near*/ = nullptr) const
    {
    }
    void StepEndGroup(bool /*keepIfEmpty*/ = false) const {}
    void EndStep(const Syntax::AstNode* /*modifiedNode*/) const {}

private:
    const ::ILSpy::Decompiler::TypeSystem::ICompilation* typeSystem_;
    const ::ILSpy::Decompiler::DecompileRun* decompileRun_;
    const ::ILSpy::Decompiler::TypeSystem::ITypeResolveContext* decompilationContext_;
    Syntax::TypeSystemAstBuilder* typeSystemAstBuilder_;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
