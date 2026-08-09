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

// Map a resolved IType to the CLI evaluation-stack lattice (StackType). Used by
// LdLoc.ResultType and the IL reader's stack-type merging. A small subset for the
// foundation; refined as the type system grows (function pointers, enums, ...).

#pragma once

#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::IL {

StackType StackTypeOf(const TypeSystem::ITypePtr& type);

// Raw-pointer overload. The NullableLifting lift machinery holds the underlying
// type of a Nullable<T> as a non-owning `const IType*` (GetUnderlyingTypeOfNullable
// returns one); this overload maps it to a StackType without constructing a
// shared_ptr. Faithful to the C# `NullableType.GetUnderlyingType(...).GetStackType()`.
StackType StackTypeOf(const TypeSystem::IType* type);

} // namespace ILSpy::Decompiler::IL
