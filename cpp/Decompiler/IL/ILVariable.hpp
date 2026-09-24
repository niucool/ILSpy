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

// A variable in the ILAst: a parameter, a local, or a stack slot the pipeline
// invents. Minimal port of ICSharpCode.Decompiler/IL/ILVariable.cs -- Name, Kind,
// resolved Type, and Index. Load/store/address-taken counts and the full
// ILVariableCollection land with the rest of the IL reader.

#pragma once

#include "Decompiler/IL/StackType.hpp"
#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"

#include <vector>

#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

class BlockContainer;
class ILInstruction;
class LdLoc;
class LdLoca;

class ILVariable {
public:
    std::string Name;
    VariableKind Kind = VariableKind::Local;
    TypeSystem::ITypePtr Type;
    // Index in the method's parameter/local list (or -1 for synthetic slots).
    std::int32_t Index = -1;

    // Usage counts populated by ComputeVariableUsage (ControlFlow/VariableUsage).
    // A parameter starts with StoreCount == 1 (it arrives with a value), matching
    // the C# usesInitialValue convention.
    int LoadCount = 0;
    int StoreCount = 0;
    int AddressCount = 0;

    // The variable's use-site instruction lists (the C# LoadInstructions/
    // StoreInstructions/AddressInstructions, the IReadOnlyList<...> fields the
    // transforms enumerate). The C# maintains them through the tree's
    // Connected/Disconnected protocol; the port fills them in the
    // ComputeVariableUsage walk (the recompute convention the counts already
    // follow) -- they are snapshots valid until the next mutation, and a
    // consumer that mutates the tree must recompute before reading again.
    // Non-owning raw pointers into the live tree.
    std::vector<LdLoc*> LoadInstructions;
    std::vector<ILInstruction*> StoreInstructions;
    std::vector<LdLoca*> AddressInstructions;

    // Set by transforms (e.g. RemoveInfeasiblePathTransform) to mark a variable
    // whose dead stores RemoveDeadVariableInit should drop even when the
    // RemoveDeadStores setting is off (ILVariable.RemoveIfRedundant in the C#).
    bool RemoveIfRedundant = false;

    // True if the variable's name is compiler-generated (e.g. the exception
    // stack slot's "E_<offset>" name), false for a name taken from a source
    // symbol. Copied by TransformCatchVariable when a catch-local is promoted to
    // the catch variable. Mirrors ILVariable.HasGeneratedName.
    bool HasGeneratedName = false;

    // The C# `public bool UsesInitialValue` (ILVariable.cs): whether the
    // variable's initial value is read before being overwritten (set by the
    // SROA/initializer paths; RemoveDeadVariableInit resets it).
    bool UsesInitialValue = false;

    // The C# `public BlockContainer? CaptureScope { get; internal set; }`
    // (ILVariable.cs): the container the variable is captured in -- the
    // closest container of its first address-taking use or initializer store,
    // combined over all capture uses (LocalFunctionDecompiler). Null for
    // variables that are not captured. Non-owning (the container is owned by
    // the enclosing ILFunction tree).
    BlockContainer* CaptureScope = nullptr;

    // True if the variable is written exactly once and its address is never
    // taken (ILVariable.IsSingleDefinition).
    bool IsSingleDefinition() const noexcept {
        return StoreCount == 1 && AddressCount == 0;
    }

    ILVariable() = default;
    ILVariable(VariableKind kind, TypeSystem::ITypePtr type, std::int32_t index = -1)
        : Kind(kind), Type(std::move(type)), Index(index) {}

    // The C# `public readonly StackType StackType` property: the variable's
    // evaluation-stack type. The C# ctor derives it from `type.GetStackType()`
    // and its Type setter GUARDS the invariant (a mismatch throws
    // ArgumentException); the port's Type is a plain field that several reader
    // and transform sites re-assign after construction, so the value is derived
    // on read instead of cached (a stale cache would be the worse divergence).
    // The C# IL reader's stack-slot ctor overload takes the stack type
    // explicitly, but always as FindType(stackType), which GetStackType maps
    // back onto -- the derived value is faithful on every reader path. A
    // variable with no type yields StackType.Unknown.
    StackType StackType() const
    {
        return Type ? TypeSystem::GetStackType(*Type) : IL::StackType::Unknown;
    }
};

using ILVariablePtr = std::shared_ptr<ILVariable>;

} // namespace ILSpy::Decompiler::IL
