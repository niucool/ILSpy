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

// Port of ICSharpCode.Decompiler/IL/BlockBuilder.cs: converts the flat list of
// offset-ordered basic blocks from the IL reader into the nested BlockContainer
// structure -- each exception-handler region's blocks move into containers
// wrapped by TryCatch/TryFinally/TryFault instructions, and null-target Leaves
// (from endfinally/endfilter) are assigned the innermost finally/filter
// container. The C# also wires Branch.TargetBlock here and synthesizes VB
// "On Error" catch-to-try dispatchers; our reader resolves branches itself and
// the VB dispatcher is deferred (a catch-to-try branch simply stays
// unresolved, matching our graceful-degradation rule).

#pragma once

#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Block.hpp"
#include "Decompiler/IL/Instructions/BlockContainer.hpp"
#include "Decompiler/Metadata/MethodBody.hpp"
#include "Decompiler/Util/Span.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::IL {

class TryCatch;

class BlockBuilder {
public:
    // handlers: the method's exception-handler clauses.
    // exceptionVarByHandlerOffset: the catch/filter exception stack slot for
    // each handler entry offset (as built by the IL reader).
    BlockBuilder(Util::Span<const Metadata::ExceptionHandlerClause> handlers,
                 const std::map<std::uint32_t, ILVariablePtr>& exceptionVarByHandlerOffset,
                 std::uint32_t codeSize);

    // Consume the flat block list (sorted by block start offset here) and nest
    // it into mainContainer per the exception-handler regions. The blocks'
    // ownership transfers into the container tree.
    void CreateBlocks(BlockContainer& mainContainer,
                      std::vector<std::pair<std::uint32_t, std::unique_ptr<Block>>>& blocks);

private:
    struct TryEntry {
        std::unique_ptr<ILInstruction> inst;  // the TryCatch/TryFinally/TryFault
        BlockContainer* tryContainer;         // non-owning (owned by inst)
        std::uint32_t tryStart;
        std::uint32_t tryEnd;
    };

    Util::Span<const Metadata::ExceptionHandlerClause> handlers_;
    const std::map<std::uint32_t, ILVariablePtr>& exceptionVarByHandlerOffset_;
    std::uint32_t codeSize_;

    // Region containers keyed by their first IL offset (handler and filter
    // containers); the C# calls this handlerContainers.
    std::map<std::uint32_t, BlockContainer*> regionContainerByStart_;
    std::vector<TryEntry> tryList_;  // sorted by (tryStart asc, tryEnd desc)
    // [StartILOffset, EndILOffset) of every region container (and the main one).
    std::map<BlockContainer*, std::pair<std::uint32_t, std::uint32_t>> containerRange_;
    // Wrapper blocks that hold a TryInstruction (their enclosing container).
    std::vector<std::pair<Block*, BlockContainer*>> wrapperBlocks_;

    void CreateContainerStructure();
    void WireWrapperBlockTerminators(BlockContainer& mainContainer);
    void AssignLeaveTargets(ILInstruction* inst, std::vector<BlockContainer*>& containerStack);
};

} // namespace ILSpy::Decompiler::IL
