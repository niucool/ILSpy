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

// Port of ICSharpCode.Decompiler/DebugInfo/SequencePoint.cs: a sequence
// point read from a PDB file or produced by the decompiler. The
// MethodBodyDisassembler's ShowSequencePoints rendering (the `// sequence
// point:` lines) consumes the struct; the real PDB reader that produces it is
// the Phase-8 PdbProvider (the portable-PDB debug tables), and a caller can
// also plug a synthetic provider.

#pragma once

#include <string>

namespace ILSpy::Decompiler::DebugInfo {

// The C# `public class SequencePoint` -- the C# property set ports as public
// fields (the plain data holder the provider contract passes around).
struct SequencePoint {
    // The C# `public int Offset { get; set; }` -- IL start offset.
    int Offset = 0;

    // The C# `public int EndOffset { get; set; }` -- not stored in debug
    // information; used internally to create hidden sequence points for the
    // IL fragments not covered by any sequence point.
    int EndOffset = 0;

    int StartLine = 0;
    int StartColumn = 0;
    int EndLine = 0;
    int EndColumn = 0;

    std::string DocumentUrl;

    // The C# `public bool IsHidden` -- the 0xfeefee hidden marker
    // (hidden on both lines, not a range).
    bool IsHidden() const {
        return StartLine == 0xfeefee && StartLine == EndLine;
    }

    // The C# `internal void SetHidden()`.
    void SetHidden() {
        StartLine = EndLine = 0xfeefee;
    }
};

}  // namespace ILSpy::Decompiler::DebugInfo
