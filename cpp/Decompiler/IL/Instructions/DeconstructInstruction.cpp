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

// The out-of-line parts of the DeconstructInstruction port (see the header for
// the node-shape documentation): the IsAssignment static over StLoc / setter
// Call / StObj, and the WriteTo support.

#include "Decompiler/IL/Instructions/DeconstructInstruction.hpp"

#include "Decompiler/IL/Instructions/LdLoc.hpp"

#include <cassert>

namespace ILSpy::Decompiler::IL {

bool DeconstructInstruction::IsAssignment(ILInstruction* inst,
                                          const TypeSystem::ICompilation* typeSystem,
                                          const TypeSystem::IType*& expectedType,
                                          ILInstruction*& value) {
    (void)typeSystem;
    if (auto* call = dynamic_cast<Call*>(inst)) {
        // The C# `case CallInstruction call:` -- a setter invocation; every
        // non-value argument must be a pure constant or an ldloc, and the
        // expected type is the last parameter's.
        if (call->Method == nullptr) return false;
        if (call->Method->AccessorKind() !=
            TypeSystem::MethodSemanticsAttributes::Setter)
            return false;
        for (std::size_t i = 0; i + 1 < call->Arguments.size(); ++i) {
            ILInstruction* arg = call->Arguments[i].get();
            if (arg == nullptr) return false;
            if (arg->Flags() == InstructionFlags::None) {
                // OK - we accept integer literals, etc.
            } else if (dynamic_cast<LdLoc*>(arg) != nullptr) {
                // OK
            } else {
                return false;
            }
        }
        const auto& parameters = call->Method->Parameters();
        if (parameters.empty()) return false;
        expectedType = &parameters.back()->Type();
        value = call->Arguments.back().get();
        return true;
    }
    if (auto* stloc = dynamic_cast<StLoc*>(inst)) {
        // The C# `case StLoc stloc:` -- the expected type is the variable's.
        if (stloc->Variable == nullptr) return false;
        expectedType = stloc->Variable->Type.get();
        value = stloc->Value.get();
        return true;
    }
    if (auto* stobj = dynamic_cast<StObj*>(inst)) {
        // The C# `case StObj stobj:` -- the target must be a plain field chain
        // over constants/ldlocs; the expected type ports to the store's own
        // type (the C# prefers the ByReferenceType element type for the
        // pointer-target case and the declared field type otherwise; the
        // port's InferType-based ByReferenceType recovery is deferred with
        // that surface).
        ILInstruction* target = stobj->Target.get();
        while (auto* ldflda = dynamic_cast<LdFlda*>(target))
            target = ldflda->Target.get();
        if (target != nullptr && target->Flags() != InstructionFlags::None &&
            dynamic_cast<LdLoc*>(target) == nullptr)
            return false;
        expectedType = stobj->Type.get();
        value = stobj->Value.get();
        return true;
    }
    return false;
}

} // namespace ILSpy::Decompiler::IL