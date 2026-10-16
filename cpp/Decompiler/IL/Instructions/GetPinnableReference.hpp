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

// get.pinnable.reference: the ILAst node the pinned-region analysis inserts as a
// PinnedRegion's Init when the pinned value comes from a `fixed`-like pinning
// (`GetPinnableReference`/array-to-pointer for arrays and strings). A
// UnaryInstruction over the pinning argument (ResultType Ref; the C# asserts the
// argument is StackType.O) carrying an optional method operand:
//   * the array element's GetPinnableReference method for the array arm,
//   * null for the fixed-view/string arms.
// DirectFlags is None; the C# ComputeFlags returns argument.Flags, which the
// port's bottom-up Flags() already composes from DirectFlags and the child, so
// no override is needed. Faithful to the generated GetPinnableReference in
// ICSharpCode.Decompiler/IL/Instructions.cs.
//
// The port keeps the C# `IMethod? method` as a shared_ptr operand beside the
// resolved display string the seed/dump renders (the LdFtn precedent): the C#
// WriteToCore prints the method through the ILAmbience, which the display string
// approximates.

#pragma once

#include "Decompiler/IL/Instructions/UnaryInstruction.hpp"
#include "Decompiler/TypeSystem/IType.hpp"

#include <memory>
#include <string>
#include <utility>

// Forward declaration (the operand is a shared_ptr to the incomplete type; the
// resolved ctor lives in TokenInstructions.cpp beside the other display-string
// ctors).
namespace ILSpy::Decompiler::TypeSystem {
class IMethod;
}

namespace ILSpy::Decompiler::IL {

class GetPinnableReference : public UnaryInstruction {
public:
    // The C# `IMethod? Method` -- the array element's GetPinnableReference
    // method, or null for the fixed-view/string arms.
    std::shared_ptr<TypeSystem::IMethod> Method;
    // The dump display string (the LdFtn precedent; empty when Method is null).
    std::string MethodName;

    // The C# `GetPinnableReference(ILInstruction argument, IMethod? method)` --
    // defined out-of-line in TokenInstructions.cpp so the resolved method's
    // display string can be derived without pulling the IMethod surface into
    // this header (the LdFtn precedent).
    explicit GetPinnableReference(std::unique_ptr<ILInstruction> argument,
                                  std::shared_ptr<TypeSystem::IMethod> method = nullptr);

    StackType ResultType() const override { return StackType::Ref; }

    void WriteTo(std::string& out) const override {
        out += "get.pinnable.reference";
        if (Method != nullptr) {
            out += ' ';
            out += MethodName;
        }
        out += '(';
        if (Argument) Argument->WriteTo(out); else out += "(null)";
        out += ')';
    }
};

} // namespace ILSpy::Decompiler::IL
