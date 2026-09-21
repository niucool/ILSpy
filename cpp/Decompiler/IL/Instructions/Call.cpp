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

#include "Decompiler/IL/Instructions/Call.hpp"

#include "Decompiler/IL/StackTypeOf.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"

#include <cassert>

namespace ILSpy::Decompiler::IL {

namespace {

// The owning shared handle behind a `const IType&` accessor result (the
// `shared_from_this()` + `const_pointer_cast` convention the resolver uses).
// Every IType the port builds is shared_ptr-managed (IType derives from
// enable_shared_from_this), so the concrete return/parameter types an IMethod
// hands back carry a live handle.
TypeSystem::ITypePtr ShareType(const TypeSystem::IType& type) {
    return const_cast<TypeSystem::IType&>(type).shared_from_this();
}

} // namespace

// The C# ctor form (`CallInstruction(OpCode opCode, IMethod method)`). The
// resolved method populates every stand-in field the C# derives from it:
//   - IsInstanceCall  <= !(Method.IsStatic || OpCode == NewObj)
//   - ResultType      <= NewObj ? Method.DeclaringType.GetStackType()
//                                : Method.ReturnType.GetStackType()
//   - GetParameter(i) <= Method.Parameters (the ParameterIType vector)
// plus the dump name (the port's ReflectionName::Name display form), the
// declaring type, the operator flag, and the method-spec type-argument count.
Call::Call(std::shared_ptr<TypeSystem::IMethod> method, bool isNewObj)
    : ILInstruction(OpCode::Call), Method(std::move(method)) {
    assert(Method != nullptr
           && "Call requires a resolved method (the string ctor is the stand-in form)");
    IsNewObj = isNewObj;
    IsInstanceCall = !Method->IsStatic() && !isNewObj;
    IsOperator = Method->IsOperator();
    TypeArgumentsCount = static_cast<int>(Method->TypeArguments().size());
    TypeSystem::ITypePtr declaring = Method->DeclaringType();
    DeclaringType = declaring;
    MethodName = declaring
        ? declaring->ReflectionName() + "::" + Method->Name()
        : Method->Name();
    ReturnIType = ShareType(Method->ReturnType());
    ParameterIType.reserve(Method->Parameters().size());
    for (const TypeSystem::IParameter* parameter : Method->Parameters()) {
        ParameterIType.push_back(parameter != nullptr ? ShareType(parameter->Type())
                                                      : nullptr);
    }
    // Faithful to the C# `ResultType` (a `newobj` leaves the constructed object;
    // a call leaves the method's return value).
    ReturnType = isNewObj
        ? (declaring ? StackTypeOf(declaring) : StackType::O)
        : StackTypeOf(ReturnIType);
}

} // namespace ILSpy::Decompiler::IL
