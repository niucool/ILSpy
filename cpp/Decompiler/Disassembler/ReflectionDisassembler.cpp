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

// The ReflectionDisassembler static-helper implementation -- see the header.

#include "Decompiler/Disassembler/ReflectionDisassembler.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Output/ITextOutput.hpp"

#include <cstdio>

namespace ILSpy::Decompiler::Disassembler {

// ---------------------------------------------------------------------------
// WriteMetadataToken (ReflectionDisassembler.cs -- the internal static).
// ---------------------------------------------------------------------------
void ReflectionDisassembler::WriteMetadataToken(Output::ITextOutput& output,
    const Metadata::MetadataFile& module, std::uint32_t handleToken,
    std::uint32_t metadataToken, bool spaceAfter, bool spaceBefore,
    bool showMetadataTokens, bool base10)
{
    // The C# `handle can be null in case of errors, if that's the case, we
    // always want to print a comment, with the metadataToken`: handleToken 0
    // is the C# null handle.
    if (showMetadataTokens || handleToken == 0) {
        if (spaceBefore) {
            output.Write(' ');
        }
        output.Write("/* ");
        char buf[16];
        if (base10) {
            // The C# `metadataToken.ToString(null)` -- plain decimal. The raw
            // metadata tokens reaching this path are non-negative int32s.
            std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(metadataToken));
        } else {
            std::snprintf(buf, sizeof(buf), "%08X", static_cast<unsigned>(metadataToken));
        }
        // The C# `if (handle == null || !handle.Value.IsEntityHandle())
        // output.Write(token) else output.WriteReference(module, handle,
        // token, "metadata")` -- the port's entity tables are every table id
        // the disassembler sees except the 0x70 UserString heap (and the null
        // handle itself).
        if (handleToken == 0 || (handleToken >> 24) == 0x70) {
            output.Write(buf);
        } else {
            output.WriteReference(module, handleToken, buf, "metadata");
        }
        output.Write(" */");
        if (spaceAfter) {
            output.Write(' ');
        }
    } else if (spaceBefore && spaceAfter) {
        output.Write(' ');
    }
}

}  // namespace ILSpy::Decompiler::Disassembler
