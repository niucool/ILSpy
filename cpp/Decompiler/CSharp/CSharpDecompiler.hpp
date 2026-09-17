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

// The sliced port of the static helpers on
// ICSharpCode.Decompiler/CSharp/CSharpDecompiler.cs. The `CSharpDecompiler` class
// itself (the member/type decompilation entry points, the DecompileRun lifecycle)
// is not ported yet; this header lands the free-standing static predicates those
// entry points (and the ExpressionBuilder) consume. Currently it holds
// `IsFixedField`, the `[FixedBuffer(typeof(T), N)]` decode the fixed-buffer
// renders (ExpressionBuilder.VisitLdFlda, the field declaration renderer) gate on.

#pragma once

#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"

#include <any>
#include <cstdint>

namespace ILSpy::Decompiler::CSharp {

// The C# `internal static bool IsFixedField(IField field, out IType type, out int
// elementCount)` (CSharpDecompiler.cs line 2522): whether the field carries a
// `FixedBufferAttribute` whose first fixed argument is the element type and whose
// second is the element count. Both out-params are cleared on a false return (the
// C# `out` contract). `type` is the decoded element type; `elementCount` the
// buffer length.
inline bool IsFixedField(const ::ILSpy::Decompiler::TypeSystem::IField& field,
                         ::ILSpy::Decompiler::TypeSystem::ITypePtr& type, int& elementCount)
{
    type = nullptr;
    elementCount = 0;
    const ::ILSpy::Decompiler::TypeSystem::IAttribute* attr =
        field.GetAttribute(::ILSpy::Decompiler::TypeSystem::KnownAttribute::FixedBuffer);
    if (attr == nullptr)
        return false;
    const std::vector<::ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument> arguments =
        attr->FixedArguments();
    if (arguments.size() != 2)
        return false;
    const std::any first = arguments[0].Value();
    const std::any second = arguments[1].Value();
    const auto* elementType =
        std::any_cast<::ILSpy::Decompiler::TypeSystem::ITypePtr>(&first);
    const auto* length = std::any_cast<std::int32_t>(&second);
    if (elementType == nullptr || length == nullptr)
        return false;
    type = *elementType;
    elementCount = static_cast<int>(*length);
    return true;
}

} // namespace ILSpy::Decompiler::CSharp
