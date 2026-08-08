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

// A minimal straight-line IL reader: the seed of the Phase 3 ILReader. It
// decodes a method body's IL bytes into an ILFunction tree via the stack
// simulation (expressionStack of pending trees + currentStack of committed
// stack-slot variables), but only for straight-line code -- no branches,
// switch, or exception handlers. Methods with control flow beyond a trailing
// `ret`/`throw` return nullptr (degrade gracefully); the full worklist/union-find
// reader is ported later.
//
// The straight-line restriction still covers many real methods (simple getters,
// ctors, wrappers, constants) and exercises the opcode tables, the signature
// decoder, the method-body reader, and the ILAst model end-to-end.

#pragma once

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"

#include <memory>

namespace ILSpy::Decompiler::IL {

// Build an ILFunction from a method body. Returns nullptr if the method is not
// straight-line (branches/switch/exception handlers), uses an unsupported opcode,
// or the body is malformed. Never throws.
//
// `methodToken` is the MethodDef token (table 0x06); `rva` is MethodDefInfo::RVA.
std::unique_ptr<ILFunction> ReadStraightLineIL(const Metadata::MetadataFile& file,
                                                std::uint32_t methodToken,
                                                std::uint32_t rva);

} // namespace ILSpy::Decompiler::IL
