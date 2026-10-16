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

// Port of ICSharpCode.BamlDecompiler/Rewrite/ConnectionIdRewritePass.cs (Ki,
// 2015, MIT): the ILAst-driven rewrite pass -- the x:Class type's
// IComponentConnector.Connect / IStyleConnector.Connect method body is read
// through the IL reader and run through the GetILTransforms() pipeline, its
// switch over the connection ids (or the if-ladder fallback) is matched for
// field assignments and event registrations, and every element carrying a
// BamlConnectionId annotation gains its x:Name / x:FieldModifier / event
// attribute (or the Style target's EventSetter child element, or an
// unknown-id comment).

#pragma once

#include "BamlDecompiler/Rewrite/IRewritePass.hpp"

namespace ILSpy::BamlDecompiler::Rewrite {

// The C# `internal class ConnectionIdRewritePass : IRewritePass`.
class ConnectionIdRewritePass : public IRewritePass {
public:
    // The C# `public void Run(XamlContext ctx, XDocument document)`.
    void Run(XamlContext& ctx, Xml::XDocument& document) override;
};

} // namespace ILSpy::BamlDecompiler::Rewrite
