// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/FixNameCollisions.cs -- the AST
// transform that renames a declaration to avoid a name collision that would make the
// generated C# uncompilable. Currently it only renames private FIELDS whose name
// collides with a property or event (or any other single-named member) of the same
// type; the motivating case is a compiler-generated event that was not detected as a
// pattern, where the backing field and the event end up with the same name.
//
// It is the next concrete `IAstTransform` in the `CSharpDecompiler.GetAstTransforms()`
// order, following `RenameVisualBasicAnonymousTypes` (which must run before it, per the
// C# comment on that transform). The transform reads the context only for the debug
// `Step` no-op, so `Run` otherwise ignores its `TransformContext` argument.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include <set>
#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `class FixNameCollisions : IAstTransform`. Stateless; a single instance runs
// over the whole tree.
class FixNameCollisions : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: the per-
    // `TypeDeclaration` member-name collection, the private single-variable field rename,
    // and the second walk that renames every reference to a renamed field. `context` is
    // only read for the debug `Step` calls (no-ops in the port).
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;

private:
    // The C# `string PickNewName(ISet<string> memberNames, string name)`: `m_` + name, or
    // name+2, name+3, ... until the candidate is not already in use.
    static std::string PickNewName(const std::set<std::string>& memberNames,
                                   const std::string& name);
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
