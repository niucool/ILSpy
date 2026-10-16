// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/Transforms/
// IntroduceRefReadOnlyModifierOnLocals.cs (71 lines): infer the `ref readonly`
// modifier on by-ref locals from their usage -- a local is marked when one
// of its stores initializes it from a reference C# requires to be readonly
// (the IsReadonlyReference shapes) or from a `readonly.` ldelema (marking
// the local avoids changing the readonly reference's semantics).

#pragma once

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class IntroduceRefReadOnlyModifierOnLocals final : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

private:
    // The C# `bool IsUsedAsRefReadonly(ILVariable variable)`.
    bool IsUsedAsRefReadonly(const ILVariable* variable);
};

} // namespace ILSpy::Decompiler::IL
