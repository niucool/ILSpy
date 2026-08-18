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

// Port of ICSharpCode.Decompiler/TypeSystem/ISymbol.cs (the `SymbolKind` enum). The kind
// of an `ISymbol`; `EntityDeclaration.SymbolKind` returns one of these to report which kind
// of member a declaration is (method/field/property/constructor/...). The C# underlying
// type is `byte`; the port keeps `std::uint8_t` so the values fit as plain unsigned values
// (the C# `: byte` is a faithful `std::uint8_t`). The member order and values mirror the C#
// exactly (each member's numeric value is its declaration index).

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::TypeSystem {

enum class SymbolKind : std::uint8_t {
	None,
	Module,
	TypeDefinition,
	Field,
	Property,
	Indexer,
	Event,
	Method,
	Operator,
	Constructor,
	Destructor,
	Accessor,
	Namespace,
	Variable,
	Parameter,
	TypeParameter,
	Constraint,
	ReturnType,
};

} // namespace ILSpy::Decompiler::TypeSystem
