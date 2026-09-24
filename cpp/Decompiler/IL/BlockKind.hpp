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
// PURPOSE, NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the BlockKind enum from ICSharpCode.Decompiler/IL/Instructions/Block.cs.
// A Block's Kind records what the block models beyond plain control flow: an
// array/collection/object initializer, an inline-assign, a named-argument call,
// a deconstruction, or a C# 10/.NET 6 interpolated string
// ($"..." via DefaultInterpolatedStringHandler). The default is ControlFlow -- a
// plain control-flow block (every block the IL reader / BlockBuilder produces).
// Transforms that synthesize a non-control-flow block (e.g.
// InterpolatedStringTransform) set the Kind so the back end can render it as the
// matching C# construct instead of a braced statement list. This port carries
// only the values the ported code consults; the full C# enum (ArrayInitializer,
// CollectionInitializer, ObjectInitializer, StackAllocInitializer,
// CallInlineAssign, CallWithNamedArgs, DeconstructionConversions,
// DeconstructionAssignments, WithInitializer) is added as the transforms that
// produce them land.

#pragma once

#include <cstdint>

namespace ILSpy::Decompiler::IL {

enum class BlockKind : std::uint8_t {
    // Plain control-flow block (the C# default). Every block must end in
    // unconditional control flow; a ControlFlow block cannot evaluate to a value.
    ControlFlow,
    // A C# 10/.NET 6 interpolated string ($"..." via
    // DefaultInterpolatedStringHandler). Instructions[0] is the
    // stloc v(newobj DefaultInterpolatedStringHandler(..)) handler
    // construction; Instructions[1..] are the AppendLiteral/AppendFormatted
    // calls that fill it; the FinalInstruction is the
    // call ToStringAndClear(ldloca v) that yields the string. Constructed by
    // InterpolatedStringTransform.
    InterpolatedString,
    // ---- The remaining C# BlockKind values (the C# `enum BlockKind`,
    // Block.cs): the initializer/call-with-named-args kinds are consulted by
    // `Block.CanInlineIntoSlot` and the transforms that build them.
    ArrayInitializer,
    CollectionInitializer,
    ObjectInitializer,
    CallInlineAssign,
    // A call whose arguments were promoted to named arguments (the C#
    // BlockKind.CallWithNamedArgs): Instructions[0] is the this-pointer store
    // for an instance call (the C# always inserts the receiver store at slot 0
    // for instance calls); Instructions[1..] are the named-argument stores
    // (StLoc(v, arg)); the FinalInstruction is the call whose argument slots
    // now carry LdLoc loads. Constructed by NamedArgumentTransform.
    CallWithNamedArgs,
    // A `with`-expression body (the C# BlockKind.WithInitializer):
    // Instructions[0] is the stloc v(newobj/clonetype) construction;
    // Instructions[1..] are the property setter stores; the FinalInstruction
    // is the ldloc v that yields the record. Constructed by the record-clone
    // arm of TransformCollectionAndObjectInitializers.
    WithInitializer,
};

} // namespace ILSpy::Decompiler::IL
