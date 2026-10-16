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

// Port of ICSharpCode.Decompiler/TypeSystem/TypeKind.cs. The kind of an IType;
// used to switch on whether a type is a class/struct/interface/enum/delegate,
// one of the special types, or a constructed shape (array/pointer/by-ref).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

enum class TypeKind : std::uint8_t {
    Other,
    Class,
    Interface,
    Struct,
    Delegate,
    Enum,
    Void,
    Unknown,
    Null,
    None,
    Dynamic,
    UnboundTypeArgument,
    TypeParameter,
    Array,
    Pointer,
    ByReference,
    Intersection,
    ArgList,
    Tuple,
    ModOpt,
    ModReq,
    NInt,
    NUInt,
    FunctionPointer,
};

} // namespace ILSpy::Decompiler::TypeSystem
