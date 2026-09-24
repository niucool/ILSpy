// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

// Port of ICSharpCode.Decompiler/CSharp/CSharpDecompiler.cs -- the decompiler
// facade (the C# `public class CSharpDecompiler`). The transform pipeline
// (GetILTransforms) and the per-body decompile driver (RunTransforms) live in
// the IL namespace (the Phase-7 landing note in GetILTransforms.hpp); this
// facade is the C#-shaped home the CLI and the later language consumers
// drive. Sliced: the pipeline entry (GetILTransforms + RunILTransforms over
// the IL-namespace factory) and the per-body decompile (the
// DecodeMethodBody + DecompileFunctionToString pair the C# Decompile path
// runs); the ctor over a MetadataFile + settings, the type/member
// enumeration surfaces (DecompileTypes/DecompileType/
// DecompileModuleAndAssemblyAttributes), and the AddPartialTypeDefinition
// machinery land with the metadata-module slices.

#pragma once

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp {

class CSharpDecompiler {
public:
    // The C# `public static List<IILTransform> GetILTransforms()` -- the
    // fixed per-body pipeline (the IL-namespace factory; the C#-shaped
    // alias the facade's consumers drive).
    static std::vector<std::unique_ptr<IL::IILTransform>> GetILTransforms();

    // The C# `function.RunTransforms(CSharpDecompiler.GetILTransforms(),
    // context)` shape -- the per-body pipeline driver over the factory.
    static void RunILTransforms(IL::ILFunction& function,
                                IL::ILTransformContext& context);

    // The C# Decompile path's per-body half (the DecodeMethodBody +
    // decompile-body flow the CLI's --csharp block carries inline): runs
    // the transform pipeline over the function and renders the C#-ish
    // method text. The return-type/parameter declaration strings are the
    // caller's (the metadata signature reader's output; the facade's
    // metadata entry takes the file and derives them, deferred with the
    // metadata-surface slice).
    static std::string DecompileFunctionToString(
        IL::ILFunction& function, std::string_view returnType,
        std::string_view methodName, std::string_view paramDecl);
};

} // namespace ILSpy::Decompiler::CSharp
