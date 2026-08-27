// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Port of the `OverloadResolution` private helpers operating on a `Candidate`. See the header.

#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"

#include "Decompiler/TypeSystem/IParameterizedMember.hpp"  // Member->Parameters (specialized)
#include "Decompiler/TypeSystem/IParameter.hpp"  // IParameter::Type
#include "Decompiler/TypeSystem/IType.hpp"  // ArrayType / ParameterizedType
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"  // SpanOfT / ReadOnlySpanOfT
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"  // IsKnownType / IsArrayInterfaceType

namespace ILSpy::Decompiler::CSharp::Resolver::Detail {

bool ResolveParameterTypes(OverloadResolutionCandidate& candidate, bool useSpecializedParameters) {
    using namespace ILSpy::Decompiler::TypeSystem;
    auto& parameterTypes = candidate.ParameterTypes();
    const auto& parameters = candidate.Parameters();
    for (std::size_t i = 0; i < parameters.size(); i++) {
        ITypePtr type;
        if (useSpecializedParameters) {
            // The parameter type of the specialized non-generic method or indexer.
            // (The C# `Debug.Assert(!candidate.IsGenericMethod)` -- not asserted in the port.)
            type = std::const_pointer_cast<IType>(
                const_cast<IType&>(candidate.Member()->Parameters()[i]->Type()).shared_from_this());
        } else {
            // The type of the original formal parameter (before any substitution).
            type = std::const_pointer_cast<IType>(
                const_cast<IType&>(parameters[i]->Type()).shared_from_this());
        }
        if (candidate.IsExpandedForm() && i == parameters.size() - 1) {
            // Unpack the params-array/Span/array-interface into its element type.
            const auto* arrayType = dynamic_cast<const ArrayType*>(type.get());
            if (arrayType != nullptr && arrayType->Rank() == 1) {
                type = arrayType->Element();  // the C# `arrayType.ElementType`
            } else if (IsKnownType(*type, KnownTypeCode::ReadOnlySpanOfT) ||
                       IsKnownType(*type, KnownTypeCode::SpanOfT)) {
                // `Span<T>` / `ReadOnlySpan<T>` are `ParameterizedType`s; `type.TypeArguments[0]`.
                const auto* pt = dynamic_cast<const ParameterizedType*>(type.get());
                if (pt == nullptr || pt->TypeArguments().empty()) return false;
                type = pt->TypeArguments()[0];
            } else if (IsArrayInterfaceType(*type)) {
                // `IEnumerable<T>` etc. are `ParameterizedType`s; `type.TypeArguments[0]`.
                const auto* pt = dynamic_cast<const ParameterizedType*>(type.get());
                if (pt == nullptr || pt->TypeArguments().empty()) return false;
                type = pt->TypeArguments()[0];
            } else {
                // Error: cannot unpack params-array. Abort considering the expanded form for this candidate.
                return false;
            }
        }
        parameterTypes[i] = type;
    }
    return true;
}

} // namespace ILSpy::Decompiler::CSharp::Resolver::Detail
