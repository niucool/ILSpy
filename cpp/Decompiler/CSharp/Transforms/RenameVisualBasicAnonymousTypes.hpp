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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/RenameVisualBasicAnonymousTypes.cs --
// the transform that gives the anonymous types of a VB assembly a C#-legal name.
//
// A VB anonymous type with a settable (non-'Key') property has no C# anonymous-type
// equivalent, so its declaration is emitted and referred to by name. The VB compiler spells
// the parts of that name with '$', which is not a legal C# identifier character, and does
// the same for the backing fields. This transform walks every identifier whose name
// contains '$', resolves the entity it refers to (through the node's symbol -- the
// `GetSymbol` resolve-result annotation), and, when that entity belongs to a
// `IsAnonymousTypeDeclaredAsNamedType` definition, replaces the '$' separators with '_'.
// It then leading-comments every such type declaration (the three-line explanation the C#
// writes), because the declaration is emitted instead of a C# anonymous type.
//
// It follows `FlattenSwitchBlocks` in the `CSharpDecompiler` transform order. The rename is
// per identifier via its symbol, so a reference is renamed the same way whether or not the
// declaration is part of the same output; the transform reads the symbol through the
// `Annotations::GetSymbol` helper and the anonymous-type predicate through the
// `NRExtensions` port.

#pragma once

#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class RenameVisualBasicAnonymousTypes : IAstTransform`.
class RenameVisualBasicAnonymousTypes : public IAstTransform {
public:
    // The C# `public void Run(AstNode rootNode, TransformContext context)`: the
    // identifier rename walk (the '$'-name test, the symbol resolution, and the
    // anonymous-type-declared-as-named-type gate) followed by the type-declaration
    // leading-comment walk.
    void Run(Syntax::AstNode& rootNode, TransformContext& context) override;
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
