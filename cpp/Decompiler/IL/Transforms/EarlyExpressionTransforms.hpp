// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including limitation the rights to use, copy, modify, merge,
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

// Port of ICSharpCode.Decompiler/IL/Transforms/EarlyExpressionTransforms.cs.
// A tree-walking transform that applies several early expression-level rewrites
// the rest of the pipeline depends on:
//   - StObjToStLoc: stobj(ldloca V, value) -> stloc V, value (the store side;
//     ILInlining only works with stloc/ldloc).
//   - LdObjToLdLoc: ldobj(ldloca V, type) -> ldloc V (the load side; same
//     reason -- a load through a local's address becomes a direct local load).
//   - FixComparisonKindLdNull: a comparison against ldnull whose kind is
//     vacuous for a reference (gt/le on the right, lt/ge on the left) is
//     normalized to the equivalent ne/eq; and `box T(arg) ==/!= ldnull` (T a
//     type parameter) unwraps to `arg ==/!= ldnull` (a boxed generic-default
//     null check is really a check on the underlying value).
// Skipped vs the C#: AddressOfLdLocToLdLoca (needs an AddressOf node this port
// does not model), the Conv wrap for unknown-typed locals in StObj/LdObjToStLoc
// (rare; the local's type usually matches the memory type), the
// IsLifted guard on FixComparisonKindLdNull (this port does not model nullable
// lifting, so no Comp is ever lifted), TransformAsyncHelpersAwaitToAwait (needs
// an Await node + an IsAsync function flag), and TransformDecimalCtorToConstant
// (needs an LdcDecimal node + the decimal type).

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class EarlyExpressionTransforms : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
