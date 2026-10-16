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

// An IL text emitter: walks a method body's IL bytes (via ILOpCodes) and emits a
// disassembly like
//     IL_0000: ldarg.0
//     IL_0001: call System.Console::WriteLine
//     IL_0006: ret
// Branch/switch operands become IL_xxxx target labels, inline constants are
// printed, and token operands are resolved through a caller-supplied resolver
// (MetadataFile::ResolveTokenToString). This is the core of Phase 6's
// ReflectionDisassembler text output and a prerequisite exerciser for the
// metadata + opcode work; the IL reader (Phase 3) decodes the same operands
// into the ILAst.

#pragma once

#include "Decompiler/Util/Span.hpp"

#include <cstdint>
#include <functional>
#include <string>

namespace ILSpy::Decompiler::Metadata {

// Emit the IL listing for a method body. `tokenResolver` maps a metadata token
// to a display string; pass MetadataFile::ResolveTokenToString. Returns the
// full text with one instruction per line.
std::string DisassembleILText(Util::Span<const std::uint8_t> il,
                             const std::function<std::string(std::uint32_t)>& tokenResolver);

} // namespace ILSpy::Decompiler::Metadata
