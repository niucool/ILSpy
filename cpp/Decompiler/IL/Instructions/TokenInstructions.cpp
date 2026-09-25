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
// PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT
// OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// The resolved-method constructors of the token nodes (the
// CompoundAssignmentInstruction.cpp precedent: the resolved ctor populates
// the display string the C# WriteToCore prints through the ambience).

#include "Decompiler/IL/Instructions/TokenInstructions.hpp"
#include "Decompiler/IL/Instructions/GetPinnableReference.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"

namespace ILSpy::Decompiler::IL {

namespace {
// The dump display form for a resolved method: the ReflectionName::Name the
// seed convention uses (declaringType->ReflectionName() + "::" + name). The
// C# WriteToCore prints the IMethod through the ILAmbience, which the display
// string approximates.
std::string MethodDisplayString(const TypeSystem::IMethod& method)
{
    TypeSystem::ITypePtr declaring = method.DeclaringType();
    if (declaring)
        return declaring->ReflectionName() + "::" + method.Name();
    return method.Name();
}
} // namespace

LdFtn::LdFtn(std::shared_ptr<TypeSystem::IMethod> method)
    : SimpleInstruction(OpCode::LdFtn), Method(std::move(method))
{
    if (Method)
        MethodName = MethodDisplayString(*Method);
}

LdVirtFtn::LdVirtFtn(std::shared_ptr<TypeSystem::IMethod> method)
    : SimpleInstruction(OpCode::LdVirtFtn), Method(std::move(method))
{
    if (Method)
        MethodName = MethodDisplayString(*Method);
}

LdVirtDelegate::LdVirtDelegate(std::unique_ptr<ILInstruction> argument,
                               TypeSystem::ITypePtr type,
                               std::shared_ptr<TypeSystem::IMethod> method)
    : UnaryInstruction(OpCode::LdVirtDelegate, std::move(argument)),
      Type(std::move(type)), Method(std::move(method))
{
    if (Method)
        MethodName = MethodDisplayString(*Method);
}

GetPinnableReference::GetPinnableReference(
    std::unique_ptr<ILInstruction> argument,
    std::shared_ptr<TypeSystem::IMethod> method)
    : UnaryInstruction(OpCode::GetPinnableReference, std::move(argument)),
      Method(std::move(method))
{
    if (Method)
        MethodName = MethodDisplayString(*Method);
}

} // namespace ILSpy::Decompiler::IL
