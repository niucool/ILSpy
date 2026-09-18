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

// Out-of-line definitions for the `DecompiledLambdaResolveResult` concrete subclass declared in
// LambdaResolveResult.hpp. The projections over the held `ILFunction` and the `IsValid` conversion
// check need the complete `ILFunction`, `IParameter`, and `CSharpConversions` types, so they live
// here rather than in the header (keeping the header's include graph minimal).

#include "Decompiler/CSharp/Resolver/LambdaResolveResult.hpp"

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"

#include <cassert>

namespace ILSpy::Decompiler::CSharp::Resolver {

using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::IParameter;

DecompiledLambdaResolveResult::DecompiledLambdaResolveResult(
    const ILSpy::Decompiler::IL::ILFunction* function,
    ITypePtr delegateType,
    ITypePtr inferredReturnType,
    bool hasParameterList,
    bool isAnonymousMethod,
    bool isImplicitlyTyped)
    : DelegateType(std::move(delegateType)),
      InferredReturnType(std::move(inferredReturnType)),
      function_(function),
      hasParameterList_(hasParameterList),
      isAnonymousMethod_(isAnonymousMethod),
      isImplicitlyTyped_(isImplicitlyTyped),
      body_(std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(
          ILSpy::Decompiler::TypeSystem::UnknownType()))
{
    // The C# `function ?? throw new ArgumentNullException` and the two `?? throw` guards on the
    // type arguments port to asserts (the argument-not-null convention).
    assert(function_ != nullptr && "DecompiledLambdaResolveResult: function must not be null");
    assert(DelegateType != nullptr && "DecompiledLambdaResolveResult: delegateType must not be null");
    assert(InferredReturnType != nullptr
           && "DecompiledLambdaResolveResult: inferredReturnType must not be null");
}

// The C# `override bool IsAsync => function.IsAsync`.
bool DecompiledLambdaResolveResult::IsAsync() const
{
    return function_->IsAsync();
}

// The C# `override IType GetInferredReturnType(IType[] parameterTypes)` -- the stored
// `InferredReturnType`; the parameter types are ignored (the C# comment: "We don't know how to
// compute which type would be inferred if given other parameter types. Let's hope this is good
// enough").
ITypePtr DecompiledLambdaResolveResult::GetInferredReturnType(
    const std::vector<ITypePtr>& /*parameterTypes*/) const
{
    return InferredReturnType;
}

// The C# `override IReadOnlyList<IParameter> Parameters => function.Parameters`.
std::vector<const IParameter*> DecompiledLambdaResolveResult::Parameters() const
{
    return function_->Parameters;
}

// The C# `override IType ReturnType => function.ReturnType`.
const IType& DecompiledLambdaResolveResult::ReturnType() const
{
    return *function_->ReturnType;
}

// The C# `override Conversion IsValid(IType[] parameterTypes, IType returnType,
// CSharpConversions conversions)`.
std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>
DecompiledLambdaResolveResult::IsValid(const std::vector<ITypePtr>& parameterTypes,
                                       const ITypePtr& returnType,
                                       CSharpConversions& conversions) const
{
    namespace Sem = ILSpy::Decompiler::Semantics;
    namespace Detail = ILSpy::Decompiler::CSharp::Resolver::Detail;

    // Anonymous method expressions without parameter lists are applicable to any parameter list.
    if (hasParameterList_) {
        const std::vector<const IParameter*> parameters = Parameters();
        if (parameters.size() != parameterTypes.size())
            return Sem::Conversions::None();
        for (std::size_t i = 0; i < parameterTypes.size(); ++i) {
            // The C# `conversions.IdentityConversion(...)` public method ports to the
            // `Detail::IdentityConversion` free function (the port has no public method, the
            // established convention). `IParameter::Type()` returns a const reference while the
            // Detail function takes a non-const `IType&` (the non-const `AcceptVisitor`), so the
            // parameter type is const-cast (the shared_ptr owns a mutable IType).
            if (!Detail::IdentityConversion(
                    *parameterTypes[i], const_cast<IType&>(parameters[i]->Type()))) {
                if (isImplicitlyTyped_) {
                    // It is possible that different parameter types also lead to a valid conversion.
                    return LambdaConversion::InstancePtr();
                }
                return Sem::Conversions::None();
            }
        }
    }
    if (Detail::IdentityConversion(const_cast<IType&>(ReturnType()), *returnType)
        || conversions.ImplicitConversion(*InferredReturnType, *returnType)->IsValid()) {
        return LambdaConversion::InstancePtr();
    }
    return Sem::Conversions::None();
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
