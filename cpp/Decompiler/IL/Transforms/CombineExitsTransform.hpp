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

// Port of ICSharpCode.Decompiler/IL/Transforms/CombineExitsTransform.cs -- the
// exit-combining pass the C# DelegateConstruction appends to the nested
// lambda-body pipeline (GetTransforms()). The `if (cond) { leave(value); }
// leave(elseValue);` shape folds to `leave(if (cond) value else elseValue)`
// so a lambda whose body is a conditional return renders as a single
// expression.

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class Block;
class BlockContainer;
class Leave;

class CombineExitsTransform final : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;

    // The C# `static Leave CombineExits(Block block)` -- the recursive fold
    // over one block (the port threads the function-body container through
    // for the IsLeavingFunction check the C# nodes carry natively). Returns
    // the combined Leave (now in the tree) or null when the shape does not
    // match.
    static Leave* CombineExits(Block* block, BlockContainer* functionBody);
};

} // namespace ILSpy::Decompiler::IL
