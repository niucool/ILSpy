// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in
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

#include "Decompiler/IL/Transforms/TupleTransform.hpp"

#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/TypeSystem/TupleType.hpp"

#include <cassert>
#include <string>

namespace ILSpy::Decompiler::IL {

bool MatchTupleFieldAccess(const LdFlda& inst,
                           const TypeSystem::IType* fieldDeclaringType,
                           TypeSystem::ITypePtr& tupleType, ILInstruction*& target,
                           int& position) {
    tupleType = nullptr;
    target = nullptr;
    position = 0;
    if (fieldDeclaringType == nullptr) return false;
    // The C# `tupleType = inst.Field.DeclaringType; target = inst.Target;`.
    tupleType = TypeSystem::ITypePtr(std::shared_ptr<TypeSystem::IType>(),
                                     const_cast<TypeSystem::IType*>(fieldDeclaringType));
    target = inst.Target.get();
    // The field name: the port's LdFlda FieldName is "Namespace.Type::Field";
    // the short name follows the last "::".
    std::string fieldName = inst.FieldName;
    const std::size_t sep = fieldName.rfind("::");
    if (sep != std::string::npos) fieldName = fieldName.substr(sep + 2);
    // C# `if (!inst.Field.Name.StartsWith("Item", StringComparison.Ordinal))`.
    if (fieldName.rfind("Item", 0) != 0) return false;
    // C# `int.TryParse(inst.Field.Name.Substring(4), out position)`.
    try {
        std::size_t consumed = 0;
        const int parsed = std::stoi(fieldName.substr(4), &consumed);
        if (consumed != fieldName.size() - 4) return false;
        position = parsed;
    } catch (const std::exception&) {
        return false;
    }
    int cardinality = 0;
    if (!TypeSystem::IsTupleCompatible(*tupleType, cardinality)) return false;
    // C# `while (target is LdFlda ldflda && ldflda.Field.Name == "Rest" &&
    // TupleType.IsTupleCompatible(ldflda.Field.DeclaringType))` -- the Rest
    // chain adds RestPosition - 1 per level. The port parses each level's
    // field name the same way and requires the level's declaring type to be
    // tuple-compatible.
    while (auto* ldflda = dynamic_cast<LdFlda*>(target)) {
        std::string levelName = ldflda->FieldName;
        const std::size_t levelSep = levelName.rfind("::");
        if (levelSep != std::string::npos) levelName = levelName.substr(levelSep + 2);
        if (levelName != "Rest") break;
        // The C# checks `IsTupleCompatible(ldflda.Field.DeclaringType)` and
        // reassigns `tupleType` to it. The port's declaring-type surface is
        // the single caller-resolved type, so the compatibility check runs on
        // it (the Rest levels cannot change it).
        if (!TypeSystem::IsTupleCompatible(*tupleType, cardinality)) break;
        target = ldflda->Target.get();
        position += TypeSystem::TupleRestPosition - 1;
    }
    return true;
}

bool MatchTupleConstruction(const Call& newobj,
                            std::vector<ILInstruction*>& arguments) {
    arguments.clear();
    if (!newobj.IsNewObj || newobj.Method == nullptr) return false;
    TypeSystem::ITypePtr declaringType = newobj.Method->DeclaringType();
    if (declaringType == nullptr) return false;
    int elementCount = 0;
    if (!TypeSystem::IsTupleCompatible(*declaringType, elementCount)) return false;
    arguments.resize(static_cast<std::size_t>(elementCount), nullptr);
    int outIndex = 0;
    const Call* current = &newobj;
    while (elementCount >= TypeSystem::TupleRestPosition) {
        if (static_cast<int>(current->Arguments.size()) != TypeSystem::TupleRestPosition)
            return false;
        for (int pos = 1; pos < TypeSystem::TupleRestPosition; pos++) {
            arguments[outIndex++] = current->Arguments[pos - 1].get();
        }
        elementCount -= TypeSystem::TupleRestPosition - 1;
        assert(outIndex + elementCount == static_cast<int>(arguments.size()));
        current = dynamic_cast<const Call*>(current->Arguments.back().get());
        if (current == nullptr || !current->IsNewObj || current->Method == nullptr)
            return false;
        TypeSystem::ITypePtr restType = current->Method->DeclaringType();
        if (restType == nullptr) return false;
        int restElementCount = 0;
        if (!TypeSystem::IsTupleCompatible(*restType, restElementCount)) return false;
        if (restElementCount != elementCount) return false;
    }
    assert(outIndex + elementCount == static_cast<int>(arguments.size()));
    if (static_cast<int>(current->Arguments.size()) != elementCount) return false;
    for (int i = 0; i < elementCount; i++) {
        arguments[outIndex++] = current->Arguments[i].get();
    }
    return true;
}

} // namespace ILSpy::Decompiler::IL