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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/TransformContext.cs -- the
// parameters for IAstTransform. The C# class carries the type system, the
// cancellation token, the TypeSystemAstBuilder, the settings bag, the
// DecompileRun, the Stepper, and the decompilation context (whose
// CurrentMember / CurrentTypeDefinition / CurrentModule accessors the
// transforms read). The port's minimal-faithful shape:
//  - `TypeSystem` is the compilation (the port stores the raw ICompilation;
//    the C# `IDecompilerTypeSystem` wraps it -- the ILTransformContext
//    convention).
//  - `Settings` aliases the DecompileRun's bag (the C# `decompileRun.Settings`).
//  - `Stepper` is the debug step hook std::function (the ILTransformContext
//    `Step` convention; the C# Stepper class is the debug-transition log).
//  - the decompilation context's member/type/module slots port to nullable
//    raw pointers (the SimpleTypeResolveContext (a) convention).
// The `CancellationToken` / `RequiredNamespacesSuperset` members are deferred:
// the port has no cancellation surface, and the namespace superset is the
// DecompileRun's own Namespaces() the consumers read directly.
//
// The C# `[Conditional("STEP")]` Step/StepStartGroup/StepEndGroup/EndStep
// members compile away in release builds; the port carries the Step
// std::function (no-op when unset) covering the observable behavior.

#pragma once

#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include <functional>
#include <string>

namespace ILSpy::Decompiler::CSharp {
class AstNode;
} // namespace ILSpy::Decompiler::CSharp

namespace ILSpy::Decompiler::CSharp::Syntax {
class TypeSystemAstBuilder;
} // namespace ILSpy::Decompiler::CSharp::Syntax

namespace ILSpy::Decompiler::CSharp::Transforms {

class TransformContext {
public:
    // The C# `public readonly IDecompilerTypeSystem TypeSystem` -- the
    // decompilation's type system (the port stores the raw ICompilation; the
    // ILTransformContext convention). Not owned.
    const ::ILSpy::Decompiler::TypeSystem::ICompilation* TypeSystem = nullptr;
    // The C# `internal readonly DecompileRun DecompileRun` + the aliased
    // `public DecompilerSettings Settings`.
    const ::ILSpy::Decompiler::DecompileRun* DecompileRun = nullptr;
    // The decompilation context's slots (the C# `ITypeResolveContext
    // decompilationContext` is private; its CurrentMember /
    // CurrentTypeDefinition / CurrentModule accessors are what the transforms
    // read). Null when a whole type or module is being decompiled.
    const ::ILSpy::Decompiler::TypeSystem::IMember* CurrentMember = nullptr;
    const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition* CurrentTypeDefinition =
        nullptr;
    const ::ILSpy::Decompiler::TypeSystem::IModule* CurrentModule = nullptr;

    // The C# `public readonly TypeSystemAstBuilder TypeSystemAstBuilder` --
    // the type renderer the insertion arms consult (ConvertType). A
    // driver-owned instance (RunAstTransforms builds it through
    // CSharpDecompiler::CreateAstBuilder, the C# ctor parameter); null when
    // the driver did not provide one (the insertion arms that need it are
    // skipped in that configuration).
    ::ILSpy::Decompiler::CSharp::Syntax::TypeSystemAstBuilder* TypeSystemAstBuilder =
        nullptr;

    // The C# `public DecompilerSettings Settings` (aliased to the run's bag;
    // the run is the owner, the port's ctor requires it non-null like the C#
    // internal ctor's non-null contract).
    const ::ILSpy::Decompiler::DecompilerSettings& Settings() const {
        return DecompileRun->Settings();
    }

    // The debug step hook (the C# `Stepper` + the `[Conditional("STEP")]`
    // Step/StepStartGroup/StepEndGroup folded onto one hook; no-op when unset).
    // `what` names the step, `near` the mutated node (nullable).
    std::function<void(const std::string& what, const void* near)> Step;

    void StepOnce(const std::string& what, const void* near = nullptr) const {
        if (Step) Step(what, near);
    }
};

} // namespace ILSpy::Decompiler::CSharp::Transforms