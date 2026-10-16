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

// A decoded local-variable entry: its type and whether the LOCAL_SIG marked it
// pinned (the 0x45 ELEMENT_TYPE_PINNED flag, ECMA-335 II.23.2). The C# IL reader
// wraps the type in a PinnedType and the ILReader unwraps it to
// VariableKind.PinnedLocal; this port carries the flag alongside the type
// instead. Kept in a winmd-free header so the public MetadataFile.hpp does not
// pull the vendored ECMA-335 reader (and its <windows.h>) into every consumer.

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

#include <vector>

namespace ILSpy::Decompiler::Metadata {

struct LocalTypeInfo {
    TypeSystem::ITypePtr Type;
    bool Pinned = false;
};

} // namespace ILSpy::Decompiler::Metadata
