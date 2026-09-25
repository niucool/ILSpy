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
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of the static helpers of ICSharpCode.Decompiler/IL/Transforms/
// TransformDisplayClassUsage.cs the ported consumers need. The transform class
// itself (the display-class field/closure rewriting pass) is not ported; the
// pure statics land here leaf-first (the land-a-leaf convention the type-shaped
// IsPotentialClosure in TypeSystemExtensions already follows -- this file adds
// its IL-layer context-shaped sibling).

#pragma once

namespace ILSpy::Decompiler::IL {

class Call;
class ILFunction;

// The C# `internal static bool IsPotentialClosure(ILTransformContext context,
// NewObj inst)` (TransformDisplayClassUsage.cs lines 718-722): whether the
// newobj's declaring type is a display class of the type being decompiled.
// The C# builds a SimpleTypeResolveContext over the ROOT function's method
// (`context.Function.Ancestors.OfType<ILFunction>().Last().Method`) and
// consults the type-shaped IsPotentialClosure(decompiledTypeDefinition,
// inst.Method.DeclaringTypeDefinition); SimpleTypeResolveContext(IEntity).
// CurrentTypeDefinition is `(entity as ITypeDefinition) ??
// entity.DeclaringTypeDefinition` -- for a method, its declaring type
// definition.
//
// The port's ILTransformContext carries no Function (the D78 Settings+Step
// convention), so the caller passes the ROOT ILFunction directly (the
// FunctionOfBlock walk reaches it from any instruction of the tree). A null
// function or a function without a resolved Method maps the C# `Method!`
// null-forgiving dereference to a null current-type definition, which the
// type-shaped predicate answers false for (the D516 null-guard convention).
bool IsPotentialClosure(const ILFunction* rootFunction, const Call* inst);

} // namespace ILSpy::Decompiler::IL
