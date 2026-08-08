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

// Port of the VariableKind enum from ICSharpCode.Decompiler/IL/ILVariable.cs. A
// variable's Kind records its provenance and is a small history of the pipeline
// (Parameter/Local from the reader; StackSlot/ExceptionStackSlot materialised by
// the reader; UsingLocal/ForeachLocal/PatternLocal/... promoted by transforms).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::IL {

enum class VariableKind : std::uint8_t {
    Local,
    PinnedLocal,
    PinnedRegionLocal,
    UsingLocal,
    ForeachLocal,
    InitializerTarget,
    Parameter,
    ExceptionStackSlot,
    ExceptionLocal,
    StackSlot,
    NamedArgument,
    DisplayClassLocal,
    PatternLocal,
    DeconstructionInitTemporary,
};

} // namespace ILSpy::Decompiler::IL
