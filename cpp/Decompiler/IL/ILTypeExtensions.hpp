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

// Port of ICSharpCode.Decompiler/IL/ILTypeExtensions.cs -- the IL-instruction type
// queries the C# back end needs. The file's GetStackType(PrimitiveType)/
// GetSign(PrimitiveType)/IsIntegerType(StackType) extensions live with their
// consumers (PrimitiveType.hpp / StackType.hpp); this file carries the
// `InferType(ILInstruction, ICompilation)` extension -- "Infers the C# type for an
// IL instruction. Returns SpecialType.UnknownType for unsupported instructions."
//
// The C# dispatches on the instruction's runtime type (`switch (inst)` with type
// patterns); the port's IL tree has no AcceptVisitor surface, so the arms dispatch
// on OpCode. Documented divergences (arms the port's node shapes cannot answer):
//   * CallIndirect -- the port has no CallIndirect node, so its
//     FunctionPointerType.ReturnType arm never fires (falls to UnknownType).
//   * UserDefinedLogicOperator -- the port's node carries the method NAME +
//     declaring type but not the return type, so the Method.ReturnType arm falls
//     to UnknownType.
//   * ILFunction with a DelegateType -- the port's ILFunction has no DelegateType
//     surface yet, so the `func.DelegateType != null` arm never fires.
//   * LdFlda/LdsFlda -- the port's field-address nodes carry the field NAME +
//     token but not the field's IType, so the ByReferenceType(field.Type) arm
//     wraps the UnknownType instead (the ByReference SHAPE is preserved; the
//     element type is unknown).

#pragma once

#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>

namespace ILSpy::Decompiler::TypeSystem {
class ICompilation;
}

namespace ILSpy::Decompiler::IL {

class ILInstruction;

// The C# `public static IType InferType(this ILInstruction inst, ICompilation?
// compilation)` (ILTypeExtensions.cs line 169): infers the C# type for an IL
// instruction; returns SpecialType.UnknownType for unsupported instructions.
// The `compilation` parameter is needed by the NewArr/Comp arms (the C# nullable
// `ICompilation?` gate returns UnknownType for a null compilation).
TypeSystem::ITypePtr InferType(const ILInstruction& inst,
                               const TypeSystem::ICompilation* compilation);

// The C# `public bool MatchDefaultValue([NotNullWhen(true)] out IType? type)`
// (Instructions.cs line 8924): true when the instruction is a DefaultValue, with
// its type. The C# instance method over the matched node; the port dispatches on
// the opcode (the port's Match* free-function convention).
bool MatchDefaultValue(const ILInstruction* inst, TypeSystem::ITypePtr& type);

} // namespace ILSpy::Decompiler::IL
