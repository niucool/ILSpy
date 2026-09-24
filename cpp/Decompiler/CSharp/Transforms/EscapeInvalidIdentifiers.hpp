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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/EscapeInvalidIdentifiers.cs
// (the EscapeInvalidIdentifiers half; the RemoveCompilerGeneratedAssemblyAttributes
// half stays in the C# file but is a separate class the port lands separately):
// rewrite every identifier containing characters outside [A-Za-z0-9_] to the
// `_XXXX` hex-escape form, prefixing a leading underscore when the result would
// start with a non-letter. Not enabled by default (the C# remark).

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

class EscapeInvalidIdentifiers final : public IAstTransform {
public:
    void Run(::ILSpy::Decompiler::CSharp::Syntax::AstNode& rootNode,
             TransformContext& context) override;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms