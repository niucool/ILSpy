// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to
// use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
// of the Software, and to permit persons to whom the Software is furnished to do
// so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

// Port of ICSharpCode/Decompiler/IL/Transforms/TupleTransform.cs (the static
// helper class): the tuple-field-access and tuple-construction recognizers the
// ExpressionBuilder / CallBuilder / DeconstructionTransform tuple arms consume.
// The C# `static class` ports to a namespace of free functions.
//
// `MatchTupleFieldAccess` reads the field's declaring type -- the C#
// `inst.Field.DeclaringType`; the port's LdFlda carries only the field name +
// token (the resolved IField surface is deferred), so the caller passes the
// resolved declaring type explicitly. The field NAME (the ItemN / Rest part)
// still parses from the LdFlda's FieldName (the "Namespace.Type::Field" form
// the IL reader stores).

#pragma once

#include <memory>
#include <vector>

namespace ILSpy::Decompiler::IL {

class LdFlda;
class Call;
class ILInstruction;

} // namespace ILSpy::Decompiler::IL

namespace ILSpy::Decompiler::TypeSystem {
class IType;
using ITypePtr = std::shared_ptr<IType>;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::IL {

// The C# `public static bool MatchTupleFieldAccess(LdFlda inst, out IType
// tupleType, out ILInstruction target, out int position)`: an ldflda accessing
// a tuple element (`ldflda Item1(ldflda Rest(target))`), the Rest chain
// flattened into the position. `fieldDeclaringType` is the port's stand-in for
// the C# `inst.Field.DeclaringType`. False leaves `position` 0 (the C# out
// contract).
bool MatchTupleFieldAccess(const LdFlda& inst,
                           const TypeSystem::IType* fieldDeclaringType,
                           TypeSystem::ITypePtr& tupleType, ILInstruction*& target,
                           int& position);

// The C# `public static bool MatchTupleConstruction(NewObj newobj, out
// ILInstruction[] arguments)`: `newobj ValueTuple<...>(...)` with the long
// (8-ary Rest-nested) tuples flattened. The returned `arguments` entries are
// the newobj's own argument nodes (non-owning references; ownership stays in
// the tree, the C# `ILInstruction[]` shape). False leaves `arguments` empty
// (the C# `arguments = null` contract).
bool MatchTupleConstruction(const Call& newobj,
                            std::vector<ILInstruction*>& arguments);

} // namespace ILSpy::Decompiler::IL