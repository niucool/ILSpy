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
// OTHERWISE, ARISING IN, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/IL/ControlFlow/DetectPinnedRegions.cs (core
// subset). IL pins locals to keep the GC from moving them; the only C# surface
// for pinning is a `fixed` block, which is scoped, so this transform detects the
// region a pin covers and wraps it in a PinnedRegion node. Must run after
// variable inlining (so the pin variable is a single local) and before
// LoopDetection (so the region's blocks are still contiguous).
//
// Subset ported: SplitBlocksAtWritesToPinnedLocals (a pinned-local write must
// be the block's last non-final instruction, followed by a Branch so the region
// starts cleanly), DetectPinnedRegion/CreatePinnedRegion (collect the blocks
// reachable from the pin until the unpin store, move them into a PinnedRegion
// body), and the leftover-writes cleanup (drop dead pure stores to a PinnedLocal
// the region replaced). Skipped: the null-safe-array-to-pointer and custom-ref
// pin patterns (multi-block shape detection), ProcessPinnedRegion's native-
// pointer replacement, string-to-pointer handling, and the clone case (a block
// reachable both inside and outside the region -- the C# duplicates it; this port
// leaves such methods without the `fixed` sugar rather than duplicate).

#pragma once

#include "Decompiler/IL/Transforms/IILTransform.hpp"

namespace ILSpy::Decompiler::IL {

class DetectPinnedRegions : public IILTransform {
public:
    void Run(ILFunction& function, ILTransformContext& context) override;
};

} // namespace ILSpy::Decompiler::IL
