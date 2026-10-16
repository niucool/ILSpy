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

// Port of ICSharpCode.Decompiler/IL/Transforms/TransformArrayInitializers.cs --
// the simple single-dimensional array-initializer path (the C# Run's
// MatchNewArr + HandleSimpleArrayInitializer + BuildSimpleArrayInitializerBlock
// arm). The remaining C# arms are deferred with loud markers:
//   * HandleRuntimeHelpersInitializeArray (the field-RVA blob decode via
//     GetInitialValue + DecodeArrayInitializer) -- needs the PE-file field
//     initial-value surface.
//   * HandleJaggedArrayInitializer / DoTransformMultiDim (the nested
//     initializer blocks).
//   * DoTransformStackAllocInitializer (the Span<T> stackalloc shape, needs
//     the MatchSpanTCtorWithPointerAndSize surface).
//   * DoTransformInlineRuntimeHelpersInitializeArray.
//   * The element-count threshold's `mustTransform` arm
//     (ILInlining.IsCatchWhenBlock / IsInConstructorInitializer are not
//     ported; mustTransform stays false).
//
// OWNERSHIP NOTE (the no-GC block-model adaptation): the C# re-parents the
// scanned element values into the built initializer block while the source
// instructions are dropped wholesale (GC keeps the subtrees alive). The port
// records the shell pointers during the scan and TAKES each accepted value out
// of its shell before the block is built; the shells are destroyed when the
// consumed range is removed from the block. An abort leaves the block
// untouched (the C# `return false` paths never mutate).

#ifndef ILSPY_DECOMPILER_IL_TRANSFORMS_TRANSFORMARRAYINITIALIZERS_HPP
#define ILSPY_DECOMPILER_IL_TRANSFORMS_TRANSFORMARRAYINITIALIZERS_HPP

#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

namespace TS = ::ILSpy::Decompiler::TypeSystem;

class ILVariable;
class StObj;
class Block;
class ILInstruction;

// The C# `public class TransformArrayInitializers : IStatementTransform`.
class TransformArrayInitializers : public IStatementTransform {
public:
    void Run(Block& block, int pos, StatementTransformContext& context) override;
};

// The scan's accepted element: the recorded indices plus the shell StObj the
// value child hangs from (taken by the caller on success).
struct TransformArrayInitializersElement {
    std::vector<int> indices;
    StObj* shell = nullptr;   // the stobj shell (taken via TakeChild(1))
    int step = 0;             // the instructions the element consumed (1 or 2)
};

// The C# `internal static bool HandleSimpleArrayInitializer(...)` — exposed
// for the tests (the C# `internal static`).
bool TransformArrayInitializersHandleSimple(
    Block* block, int pos, ILVariable* store, const std::vector<int>& arrayLength,
    std::vector<TransformArrayInitializersElement>& elements,
    int& instructionsToRemove);

// The C# `static Block BuildSimpleArrayInitializerBlock(...)` — the values
// arrive as owned pointers (null = the DefaultValue filler, the C#
// `value ?? GetNullExpression(elementType)`).
std::unique_ptr<Block> TransformArrayInitializersBuildBlock(
    ILVariablePtr v, const TS::ITypePtr& elementType,
    const std::vector<int>& arrayLength,
    std::vector<std::pair<std::vector<int>, std::unique_ptr<ILInstruction>>>
        values);

} // namespace ILSpy::Decompiler::IL

#endif // ILSPY_DECOMPILER_IL_TRANSFORMS_TRANSFORMARRAYINITIALIZERS_HPP