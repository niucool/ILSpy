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

// Port of ICSharpCode.Decompiler/IL/Transforms/LdLocaDupInitObjTransform.cs.
//
// The Roslyn (>= 2) codegen for `var v = default(T);` followed by a use of `&v`
// emits `ldloca v; dup; initobj T`, which the reader lowers to
//     stloc s(ldloca v)                 // s = &v   (the dup's stack slot)
//     stobj T(ldloc s, default(T), T)   // *s = default(T)  (the initobj)
// This transform rewrites that pair to
//     stloc v(default(T))               // v = default(T)
//     stloc s(ldloca v)                 // s = &v
// so `s` (the address) can be inlined into its subsequent uses (the C# runs it
// before the inlining pass in EarlyExpressionTransforms). Skipped vs the C#:
// `inst2.UnalignedPrefix == 0` / `!inst2.IsVolatile` (our StObj carries no such
// prefix flags -- see D52) and the `s.LoadCount > 1` guard from the comment (the
// C# code itself does not check it; the rewrite is correct regardless).

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class LdLocaDupInitObjTransform : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
