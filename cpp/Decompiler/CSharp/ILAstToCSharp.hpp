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

// A minimal ILAst-to-C# translator -- the seed of Phase 5. It walks an ILFunction
// and emits C#-ish text for the expressions and statements the IL reader produces.
// This is NOT a real decompiler back end: it has no resolver (so names/casts are
// approximate), no AST transforms (so control flow is gotos), and no type inference
// (so local declarations use `var`). It demonstrates the pipeline end-to-end
// (IL -> ILAst -> C# text) and is the first rung of the back end.

#pragma once

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <string>
#include <string_view>

namespace ILSpy::Decompiler::IL {

// A C# type name for a declaration: the C# keyword for primitives (int, bool,
// string, ...), the short type name otherwise, recursing into array/byref.
// `var` for an unknown type. Used for the method signature and local decls.
std::string CSharpTypeName(const TypeSystem::ITypePtr& type);

// Emit a C#-ish translation of an ILFunction. `returnType` is the method's
// return type display name (e.g. "System.Int32" or "void"); `methodName` is
// the method's display name; `paramDecl` is the parameter declarations string
// (e.g. "object obj"). The body is translated from the ILAst: straight-line
// statements become C# statements, branches become gotos, and expressions are
// recursively translated.
std::string ILAstToCSharp(const ILFunction& fn,
                         std::string_view returnType,
                         std::string_view methodName,
                         std::string_view paramDecl);

} // namespace ILSpy::Decompiler::IL
