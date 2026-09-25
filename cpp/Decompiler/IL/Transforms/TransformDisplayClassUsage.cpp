// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use,
// copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the
// Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// The context-shaped IsPotentialClosure (see the header).

#include "Decompiler/IL/Transforms/TransformDisplayClassUsage.hpp"

#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

namespace ILSpy::Decompiler::IL {

bool IsPotentialClosure(const ILFunction* rootFunction, const Call* inst) {
    namespace TS = ::ILSpy::Decompiler::TypeSystem;
    const TS::ITypeDefinition* currentTypeDefinition = nullptr;
    if (rootFunction != nullptr && rootFunction->Method != nullptr)
        currentTypeDefinition = rootFunction->Method->DeclaringTypeDefinition();
    const TS::ITypeDefinition* potentialDisplayClass = nullptr;
    if (inst != nullptr && inst->Method != nullptr)
        potentialDisplayClass = inst->Method->DeclaringTypeDefinition();
    return TS::IsPotentialClosure(currentTypeDefinition, potentialDisplayClass);
}

} // namespace ILSpy::Decompiler::IL
