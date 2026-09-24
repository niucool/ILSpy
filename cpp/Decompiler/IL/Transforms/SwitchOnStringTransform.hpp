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

// Port of ICSharpCode.Decompiler/IL/Transforms/SwitchOnStringTransform.cs --
// detects the compiler's switch-on-string shapes and rewrites them to a
// SwitchInstruction over a StringToInt hash dispatch.
//
// Sliced: this file carries the Run driver, the Roslyn cascading-if arm
// (SimplifyCascadingIfStatements), and the modern Roslyn
// switch(ComputeStringHash(s)) arm (MatchRoslynSwitchOnString) with its
// helpers. Deferred with the C# anchors noted at each deferral:
//  - SimplifyCSharp1CascadingIfStatements (line 432): the C# 2.0
//    `string.IsInterned` shape.
//  - MatchLegacySwitchOnStringWithDict (line 564) +
//    MatchLegacySwitchOnStringWithHashtable (line 891) +
//    ExtractStringValuesFromInitBlock (line 797): the Dictionary<string,int> /
//    Hashtable initializer shapes.
//  - MatchRoslynSwitchOnStringUsingLengthAndChar (line 1195): the
//    length+char-check shape for switch on ReadOnlySpan<char>.
//  - InlineSwitchExpressionDefaultCaseThrowHelper: deferred with
//    SwitchDetection's throw-helper surface.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class SwitchOnStringTransform final : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL