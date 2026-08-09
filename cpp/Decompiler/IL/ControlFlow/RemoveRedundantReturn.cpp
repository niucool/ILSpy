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

#include "Decompiler/IL/ControlFlow/RemoveRedundantReturn.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Instructions/Leave.hpp"

namespace ILSpy::Decompiler::IL {

void RemoveRedundantReturn::Run(ILFunction& function, ILTransformContext& context) {
    (void)context;
    if (!function.Body || function.Body->Blocks.empty()) return;
    BlockContainer* body = function.Body.get();
    Block* last = body->Blocks.back().get();
    if (!last->FinalInstruction) return;
    // A value-less Leave of the function body on the last block is a redundant
    // `return;` -- the block falls through to the implicit function exit.
    auto* leave = dynamic_cast<Leave*>(last->FinalInstruction.get());
    if (!leave || leave->TargetContainer != body) return;
    if (leave->Value) return;  // value return: not redundant
    last->FinalInstruction.reset();
}

} // namespace ILSpy::Decompiler::IL
