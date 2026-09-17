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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/RemoveCLSCompliantAttribute.cs -- the
// project-export AST transform that drops the compiler-emitted `[CLSCompliant]` attributes
// from the module/type/member declarations (the VB compiler scatters them, producing C#
// warnings once the whole assembly is recompiled). It walks the DIRECT children of the tree
// root for `AttributeSection` nodes, skips the `assembly`-targeted sections (the assembly
// identity is written by the project file, not the generated `AssemblyInfo.cs`), removes
// every attribute whose type resolves to `System.CLSCompliantAttribute`, and drops a section
// that ends up empty.
//
// It is the second concrete `IAstTransform` over the iteration-160 `TransformContext`
// foundation, and it follows `EscapeInvalidIdentifiers` in the `WholeProjectDecompiler`
// project-export order (`EscapeInvalidIdentifiers`, then `RemoveCLSCompliantAttribute`, then
// the `RemoveCompilerGeneratedAssemblyAttributes` slice). The C# `attribute.Type.Annotation
// <TypeResolveResult>()` ports to `attribute->Type()->Annotation<Semantics::TypeResolveResult>()`;
// the annotation is the resolver's marker that a type reference was bound, and the
// transform's type test reads the resolved type's full name.
//
// The `RemoveEmbeddedAttributes`/`RemoveCompilerGeneratedAssemblyAttributes` siblings in the
// same source file stay deferred: they need the `TypeDeclaration.GetSymbol()` /
// `KnownAttribute.Embedded` surfaces the type-declaration writer slices carry.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include "Decompiler/TypeSystem/IType.hpp"

#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class RemoveCLSCompliantAttribute : IAstTransform`. "This transform is only
// enabled, when exporting a full assembly as project" (the C# remark).
class RemoveCLSCompliantAttribute : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: the direct
    // `AttributeSection` children walk with the `assembly` skip, the per-attribute
    // `System.CLSCompliantAttribute` test, and the empty-section cleanup.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The full name the transformed attributes are matched against (the C#
    // `"System.CLSCompliantAttribute"` literal).
    static constexpr const char* ClsCompliantAttributeFullName = "System.CLSCompliantAttribute";
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
