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

#include "Decompiler/IL/VariableKind.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ILSpy::Decompiler::IL {

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

    // True if the variable is written exactly once and its address is never
    // taken (ILVariable.IsSingleDefinition).
    bool IsSingleDefinition() const noexcept {
        return StoreCount == 1 && AddressCount == 0;
    }

    ILVariable() = default;
    ILVariable(VariableKind kind, TypeSystem::ITypePtr type, std::int32_t index = -1)
        : Kind(kind), Type(std::move(type)), Index(index) {}
};

using ILVariablePtr = std::shared_ptr<ILVariable>;

} // namespace ILSpy::Decompiler::IL
