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

// Port of ICSharpCode.Decompiler/IL/Transforms/LocalFunctionDecompiler.cs --
// wires local-function use-sites (call/callvirt/ldftn) into the enclosing
// function's ILFunction.
//
// Sliced: this file carries the transform shell (the Run gate + the
// self/double-bail guards' shape) and the ParseLocalFunctionName name
// extractor (the C# internal static, the `^<(.*)>g__([^\|]*)\|{0,1}\d+(_\d+)?$`
// regex over the compiler-generated local-function-method names). The
// FindUseSites/HandleUseSite walk, the deep local-function-body decode
// (ReadLocalFunctionDefinition through the IL reader with a GenericContext),
// the capture/declaration-scope machinery (DetermineCaptureAndDeclarationScopes
// + MoveToDeclarationScope), PropagateClosureParameterArguments, and the
// display-class `this` rewrite (ReplaceReferencesToDisplayClassThis over
// DelegateConstruction's ReplaceDelegateTargetVisitor) are deferred with the
// surfaces they need (the ILFunction Method handle + the deep-decode entry).
// The metadata-level IsLocalFunctionMethod probe lives in
// Decompiler/Metadata/CodeMappingInfo.cpp (the GetCodeMappingInfo walk's
// arm); this file is the IL-transform side.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

#include <string>

namespace ILSpy::Decompiler::IL {

class LocalFunctionDecompiler final : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // The C# `internal static bool ParseLocalFunctionName(string name,
    // out string callerName, out string functionName)`: the
    // `^<(.*)>g__([^\|]*)\|{0,1}\d+(_\d+)?$` regex split of a
    // compiler-generated local-function-method name into its enclosing
    // method name and the local function's own name. `_<n>` ordinals
    // (`|0_1`) and the `|0` ordinal suffixes are accepted; anything else
    // (including an empty function-name part) is rejected.
    static bool ParseLocalFunctionName(const std::string& name,
                                       std::string& callerName,
                                       std::string& functionName);
};

} // namespace ILSpy::Decompiler::IL