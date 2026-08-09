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

// Port of ICSharpCode.Decompiler/IL/Transforms/AssignVariableNames.cs (subset).
// Renames compiler-generated locals and stack slots to readable C# names
// inferred from their type (System.Int32 -> num, System.String -> text, ...),
// disambiguated against parameter names and earlier locals. Parameters keep
// their metadata names. Skipped: Humanizer pluralisation, the scope system,
// loop-counter detection, display-class handling, and C#-keyword avoidance.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class AssignVariableNames : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
