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

#include "Decompiler/IL/Transforms/TupleTransform.hpp"

#include <climits>
#include <string>

namespace ILSpy::Decompiler::IL {

// The C# `int.TryParse(name.Substring(4), out position)`: parse a full integer
// (an optional sign and whitespace allowed, as in the BCL). `std::stol` mirrors
// the leading-whitespace/sign rules; any trailing character fails the parse.
static bool TryParseInt(const std::string& text, int& value)
{
    if (text.empty())
        return false;
    try
    {
        std::size_t pos = 0;
        long parsed = std::stol(text, &pos);
        if (pos != text.size() || parsed < INT_MIN || parsed > INT_MAX)
            return false;
        value = static_cast<int>(parsed);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

bool TupleTransform::MatchTupleFieldAccess(const LdFlda& inst, TypeSystem::ITypePtr& tupleType,
                                           ILInstruction*& target, int& position)
{
    if (!inst.Field || !inst.Target)
        return false;
    tupleType = inst.Field->DeclaringType();
    target = inst.Target.get();
    if (inst.Field->Name().rfind("Item", 0) != 0)
    {
        position = 0;
        return false;
    }
    if (!TryParseInt(inst.Field->Name().substr(4), position))
        return false;
    int cardinality = 0;
    if (!tupleType || !TypeSystem::IsTupleCompatible(*tupleType, cardinality))
        return false;
    while (target != nullptr && target->Op == OpCode::LdFlda)
    {
        auto* ldflda = static_cast<LdFlda*>(target);
        if (!ldflda->Field || ldflda->Field->Name() != "Rest")
            break;
        TypeSystem::ITypePtr declaringType = ldflda->Field->DeclaringType();
        if (!declaringType || !TypeSystem::IsTupleCompatible(*declaringType, cardinality))
            break;
        tupleType = std::move(declaringType);
        target = ldflda->Target.get();
        position += TypeSystem::TupleRestPosition - 1;
    }
    return true;
}

} // namespace ILSpy::Decompiler::IL
