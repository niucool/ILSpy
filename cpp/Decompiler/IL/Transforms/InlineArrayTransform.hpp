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

// Port of ICSharpCode/Decompiler/IL/Transforms/InlineArrayTransform.cs: folds the
// compiler-generated inline-array helper calls into `ldelema.inlinearray`
// (LdElemaInlineArray). Three arms:
//   call get_Item(addressof Span{T}(call InlineArrayAsSpan(addr, length)), index)
//   call InlineArrayElementRef(ReadOnly)(addr, index)
//   call InlineArrayFirstElementRef(ReadOnly)(addr)
// The C# `static class` ports to a namespace of free functions; RunOnExpression
// takes the Call to rewrite in place (the C# `inst.ReplaceWith` + WithILRange --
// the port's ILRange bookkeeping is deferred with that surface, so the fold
// replaces the node through the parent slot without carrying the range).

#pragma once

#include <memory>

namespace ILSpy::Decompiler::IL {

class Call;
class StatementTransformContext;

class InlineArrayTransform {
public:
    // The C# `internal static bool RunOnExpression(Call inst,
    // StatementTransformContext context)`: try each arm; on a match, replace
    // `inst` with the folded LdElemaInlineArray and return true.
    static bool RunOnExpression(Call* inst, StatementTransformContext& context);
};

} // namespace ILSpy::Decompiler::IL