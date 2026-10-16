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

#include "Decompiler/IL/Instructions/UserDefinedLogicOperator.hpp"

#include <cassert>

namespace ILSpy::Decompiler::IL {

// The C# ctor form: the resolved method populates the dump stand-ins (the
// UserDefinedCompoundAssign gnhf-110 precedent -- WriteToCore prints the method
// through the ambience, so the port's `ReflectionName::Name` display form
// derives from it) and is kept for the VisitUserDefinedLogicOperator arm.
UserDefinedLogicOperator::UserDefinedLogicOperator(
    std::shared_ptr<TypeSystem::IMethod> method,
    std::unique_ptr<ILInstruction> left, std::unique_ptr<ILInstruction> right)
    : BinaryInstruction(OpCode::UserDefinedLogicOperator, std::move(left), std::move(right)),
      Method(std::move(method)) {
    const std::string& name = Method->Name();
    assert((name == "op_BitwiseAnd" || name == "op_BitwiseOr")
           && "Method must be op_BitwiseAnd or op_BitwiseOr");
    TypeSystem::ITypePtr declaring = Method->DeclaringType();
    if (declaring)
        MethodName = declaring->ReflectionName() + "::" + name;
    else
        MethodName = name;
    MethodDeclaringType = declaring;
}

} // namespace ILSpy::Decompiler::IL
