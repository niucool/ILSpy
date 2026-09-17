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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/EscapeInvalidIdentifiers.cs
// -- the AST transform that rewrites identifiers whose names are not valid C#
// identifiers (used when exporting a whole project, so the emitted names
// recompile). This is the first concrete `IAstTransform` over the newly landed
// `TransformContext`; it walks every `Identifier` in the tree and escapes any
// invalid UTF-16 unit as `_XXXX`.
//
// The file's other two classes (`RemoveCompilerGeneratedAssemblyAttributes`,
// `RemoveEmbeddedAttributes`) are deferred: they need the `TypeResolveResult`
// annotation / `KnownAttribute.Embedded` surfaces the module-attribute writer
// slices carry.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include <cstdint>
#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class EscapeInvalidIdentifiers : IAstTransform`. "This
// transform is not enabled by default" (the C# also says so).
class EscapeInvalidIdentifiers : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`:
    // walk every `Identifier` descendant and replace any name whose UTF-16 units
    // are not all letters/digits/underscore. The C# `context.Step(...)` call is
    // compiled out of a normal build; the port's no-op matches.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

    // The C# `bool IsValid(char ch)` -- a letter, a digit, or `_` (the C#
    // `char.IsLetterOrDigit(ch) || ch == '_'`). Uses the port's UTF-16
    // `char.IsLetterOrDigit` probe table.
    static bool IsValid(char16_t ch);

    // The C# `string ReplaceInvalid(string s)`: concatenate each valid unit as-is
    // and each invalid one as its `_XXXX` hexadecimal escape, then prefix `_` when the
    // result starts with a valid-but-not-letter-or-underscore unit (a leading
    // digit). Works on UTF-16 units so the `_XXXX` value matches the C#
    // `(int)ch`.
    static std::string ReplaceInvalid(const std::string& s);
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
