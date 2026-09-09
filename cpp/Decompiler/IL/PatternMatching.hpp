// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

// Port of the match-extension methods the C# IL layer carries on
// `ILInstruction` (ICSharpCode.Decompiler/IL/Instructions/PatternMatching.cs
// plus the generated `IL/Instructions.cs` region): `MatchLdThis`, `MatchBox`,
// and `MatchLdObj` as free functions over the port's IL node pointers.
//
// The C# methods return the matched CHILD REFERENCES through the `out`
// parameters; the port's IL tree owns its children via `unique_ptr` slots, so
// the child `out` parameters are NON-OWNING raw pointers (the instruction keeps
// the ownership) while the type `out` parameters alias the node's own
// `ITypePtr` field (the node keeps the ownership there too). Transforms that
// need to detach a child use the node classes' `SetChildRaw` surface instead.
//
// `PatternMatchingTransform.cpp` carries same-named file-local helpers in its
// anonymous namespace (an inner-scope shadow -- call sites there still resolve
// to the local ones); this header is the shared home new consumers use.

#pragma once

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/IL/Instructions/Box.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdNull.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

namespace ILSpy::Decompiler::IL {

// The C# `public bool MatchLdNull()` (Instructions.cs line 8659): the `ldnull`
// node -- a bare `LdNull` with no operand.
inline bool MatchLdNull(const ILInstruction* inst)
{
    return dynamic_cast<const LdNull*>(inst) != nullptr;
}

// The C# `public bool MatchLdThis()` (PatternMatching.cs line 114): the `ldloc`
// of the synthetic `this` parameter -- a `LdLoc` whose variable is a parameter
// with a negative index (the C# `Variable.Index < 0`; the port's ILVariable
// carries -1 for the synthetic this slot).
inline bool MatchLdThis(const ILInstruction* inst)
{
    const auto* ldloc = dynamic_cast<const LdLoc*>(inst);
    return ldloc != nullptr && ldloc->Variable != nullptr
        && ldloc->Variable->Kind == VariableKind::Parameter && ldloc->Variable->Index < 0;
}

// The C# `public bool MatchBox(out ILInstruction? argument, out IType? type)`
// (IL/Instructions.cs line 8874): the `box <T>` node.
inline bool MatchBox(const ILInstruction* inst, ILInstruction*& argument,
                     TypeSystem::ITypePtr& type)
{
    const auto* box = dynamic_cast<const Box*>(inst);
    if (box != nullptr) {
        argument = box->Argument.get();
        type = box->Type;
        return true;
    }
    argument = nullptr;
    type = nullptr;
    return false;
}

// The C# `public bool MatchLdObj(out ILInstruction? target, out IType? type)`
// (IL/Instructions.cs line 8833): the `ldobj <T>` node (the two-out-parameter
// overload; the `PatternMatching.cs` overload with the type-equality test is
// the three-argument form).
inline bool MatchLdObj(const ILInstruction* inst, ILInstruction*& target,
                       TypeSystem::ITypePtr& type)
{
    const auto* ldobj = dynamic_cast<const LdObj*>(inst);
    if (ldobj != nullptr) {
        target = ldobj->Target.get();
        type = ldobj->Type;
        return true;
    }
    target = nullptr;
    type = nullptr;
    return false;
}

} // namespace ILSpy::Decompiler::IL
