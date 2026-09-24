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

#include "Decompiler/IL/Transforms/InlineArrayTransform.hpp"

#include "Decompiler/IL/ILInstruction.hpp"
#include "Decompiler/IL/Instructions/AddressOf.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/IL/Instructions/LdcI4.hpp"
#include "Decompiler/IL/Instructions/LdElemaInlineArray.hpp"
#include "Decompiler/IL/Instructions/MemoryInstructions.hpp"
#include "Decompiler/IL/Transforms/StatementTransform.hpp"
#include "Decompiler/TypeSystem/IParameter.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <functional>
#include <cstdio>
#include <memory>
#include <optional>

namespace ILSpy::Decompiler::IL {
namespace {

using TypeSystem::IType;
using TypeSystem::ITypePtr;

// The port's stand-in for the C# `WithILRange(inst)` -- the ILRange bookkeeping
// is deferred with that surface, so a replacement carries no range.
std::unique_ptr<LdElemaInlineArray> MakeLdElemaInlineArray(
    ITypePtr type, std::unique_ptr<ILInstruction> array,
    std::unique_ptr<ILInstruction> index, bool isReadOnly) {
    std::vector<std::unique_ptr<ILInstruction>> indices;
    indices.push_back(std::move(index));
    auto result = std::make_unique<LdElemaInlineArray>(std::move(type),
                                                       std::move(array),
                                                       std::move(indices));
    result->IsReadOnly = isReadOnly;
    return result;
}

// The C# `static bool MatchSpanGetItem(IMethod method, string typeName)`:
// `T System.Span/ReadOnlySpan<T>.get_Item(int)` -- an instance method on the
// one-parameter span struct.
bool MatchSpanGetItem(const TypeSystem::IMethod* method, const std::string& typeName) {
    if (method == nullptr) return false;
    if (method->IsStatic()) return false;
    if (method->Name() != "get_Item") return false;
    ITypePtr declaringType = method->DeclaringType();
    if (declaringType == nullptr) return false;
    if (declaringType->Namespace() != "System") return false;
    if (declaringType->Name() != typeName) return false;
    if (declaringType->TypeParameterCount() != 1) return false;
    // The C# also requires the span type to be top-level (`DeclaringType:
    // null`); the port's IType surface does not carry the nesting pointer,
    // and the namespace+name+arity match is the load-bearing gate.
    return true;
}

// The C# `static bool MatchInlineArrayHelper(IMethod method, string methodName,
// out IType inlineArrayType)`: a static `<PrivateImplementationDetails>`
// helper `N(bufferType&)` / `N(bufferType&, int)`.
bool MatchInlineArrayHelper(const TypeSystem::IMethod* method,
                            const std::string& methodName, ITypePtr& inlineArrayType) {
    inlineArrayType = nullptr;
    if (method == nullptr) return false;
    if (!method->IsStatic()) return false;
    if (method->Name() != methodName) {
        std::fprintf(stderr, "PROBE-H: name '%s' != '%s'\n",
                     method->Name().c_str(), methodName.c_str());
        return false;
    }
    ITypePtr declaringType = method->DeclaringType();
    if (declaringType == nullptr) {
        std::fprintf(stderr, "PROBE-H: no declaring type\n");
        return false;
    }
    if (declaringType->Name() != "<PrivateImplementationDetails>") {
        std::fprintf(stderr, "PROBE-H: decl name '%s'\n",
                     declaringType->Name().c_str());
        return false;
    }
    if (declaringType->TypeParameterCount() != 0) return false;
    std::vector<ITypePtr> typeArguments = method->TypeArguments();
    if (typeArguments.size() != 2) {
        std::fprintf(stderr, "PROBE-H: typeargs %zu\n", typeArguments.size());
        return false;
    }
    ITypePtr bufferType = typeArguments[0];
    if (bufferType == nullptr) return false;
    const std::vector<const TypeSystem::IParameter*> parameters = method->Parameters();
    if (methodName.find("FirstElement") != std::string::npos) {
        if (parameters.size() != 1) {
            std::fprintf(stderr, "PROBE-H: first params %zu\n", parameters.size());
            return false;
        }
        auto* byRef = dynamic_cast<const TypeSystem::ByReferenceType*>(
            &parameters[0]->Type());
        if (byRef == nullptr) {
            std::fprintf(stderr, "PROBE-H: param0 not byref\n");
            return false;
        }
        if (!byRef->Element()->Equals(*bufferType)) return false;
    } else {
        if (parameters.size() != 2) {
            std::fprintf(stderr, "PROBE-H: elem params %zu\n", parameters.size());
            return false;
        }
        auto* byRef = dynamic_cast<const TypeSystem::ByReferenceType*>(
            &parameters[0]->Type());
        if (byRef == nullptr) {
            std::fprintf(stderr, "PROBE-H: param0 not byref\n");
            return false;
        }
        if (!byRef->Element()->Equals(*bufferType)) {
            std::fprintf(stderr, "PROBE-H: buffer type mismatch\n");
            return false;
        }
        if (!TypeSystem::IsKnownType(parameters[1]->Type(), TypeSystem::KnownTypeCode::Int32)) {
            std::fprintf(stderr, "PROBE-H: param1 not int32\n");
            return false;
        }
    }
    inlineArrayType = bufferType;
    return true;
}

// The C# `static bool MatchSpanIndexerWithInlineArrayAsSpan(Call inst, out
// type, out addr, out index, out isReadOnly)`: the `span[index]` access chain
// over the compiler-generated span conversion.
bool MatchSpanIndexerWithInlineArrayAsSpan(
    Call* inst, ITypePtr& type, std::unique_ptr<ILInstruction>& addr,
    std::unique_ptr<ILInstruction>& index, bool& isReadOnly) {
    isReadOnly = false;
    type = nullptr;
    addr = nullptr;
    index = nullptr;
    if (MatchSpanGetItem(inst->Method.get(), "ReadOnlySpan")) {
        isReadOnly = true;
        if (inst->Arguments.size() != 2) return false;
        auto* addressOf = dynamic_cast<AddressOf*>(inst->Arguments[0].get());
        if (addressOf == nullptr || !addressOf->Type) return false;
        auto* targetInst = dynamic_cast<Call*>(addressOf->Value.get());
        if (targetInst == nullptr) return false;
        ITypePtr inlineArrayType;
        if (!MatchInlineArrayHelper(targetInst->Method.get(),
                                    "InlineArrayAsReadOnlySpan", inlineArrayType))
            return false;
        if (targetInst->Arguments.size() != 2) return false;
        auto* length = dynamic_cast<LdcI4*>(targetInst->Arguments[1].get());
        if (length == nullptr) return false;
        std::optional<int> arrayLength =
            TypeSystem::GetInlineArrayLength(*inlineArrayType);
        if (!arrayLength.has_value()) return false;
        if (length->Value < 0 || length->Value > *arrayLength) return false;
        type = std::move(inlineArrayType);
        addr = std::move(targetInst->Arguments[0]);
        index = std::move(inst->Arguments[1]);
        return true;
    }
    if (MatchSpanGetItem(inst->Method.get(), "Span")) {
        if (inst->Arguments.size() != 2) return false;
        auto* addressOf = dynamic_cast<AddressOf*>(inst->Arguments[0].get());
        if (addressOf == nullptr || !addressOf->Type) return false;
        auto* targetInst = dynamic_cast<Call*>(addressOf->Value.get());
        if (targetInst == nullptr) return false;
        ITypePtr inlineArrayType;
        if (!MatchInlineArrayHelper(targetInst->Method.get(), "InlineArrayAsSpan",
                                    inlineArrayType))
            return false;
        if (targetInst->Arguments.size() != 2) return false;
        auto* length = dynamic_cast<LdcI4*>(targetInst->Arguments[1].get());
        if (length == nullptr) return false;
        std::optional<int> arrayLength =
            TypeSystem::GetInlineArrayLength(*inlineArrayType);
        if (!arrayLength.has_value()) return false;
        if (length->Value < 0 || length->Value > *arrayLength) return false;
        type = std::move(inlineArrayType);
        addr = std::move(targetInst->Arguments[0]);
        index = std::move(inst->Arguments[1]);
        return true;
    }
    return false;
}

// The C# `static bool MatchInlineArrayElementRef(Call inst, out type, out
// addr, out index, out isReadOnly)`.
bool MatchInlineArrayElementRef(Call* inst, ITypePtr& type,
                                std::unique_ptr<ILInstruction>& addr,
                                std::unique_ptr<ILInstruction>& index,
                                bool& isReadOnly) {
    type = nullptr;
    addr = nullptr;
    index = nullptr;
    isReadOnly = false;
    if (inst->Arguments.size() != 2) return false;
    auto* indexValue = dynamic_cast<LdcI4*>(inst->Arguments[1].get());
    if (indexValue == nullptr) return false;
    ITypePtr inlineArrayType;
    if (MatchInlineArrayHelper(inst->Method.get(), "InlineArrayElementRef",
                               inlineArrayType)) {
        isReadOnly = false;
        std::fprintf(stderr, "PROBE: ElementRef matched\n");
    } else if (MatchInlineArrayHelper(inst->Method.get(),
                                      "InlineArrayElementRefReadOnly",
                                      inlineArrayType)) {
        isReadOnly = true;
    } else {
        std::fprintf(stderr, "PROBE: ElementRef helper mismatch\n");
        return false;
    }
    std::optional<int> arrayLength =
        TypeSystem::GetInlineArrayLength(*inlineArrayType);
    std::fprintf(stderr, "PROBE: length has_value=%d val=%d index=%d\n",
                 arrayLength.has_value() ? 1 : 0,
                 arrayLength.has_value() ? *arrayLength : -1, indexValue->Value);
    if (!arrayLength.has_value()) return false;
    if (indexValue->Value < 0 || indexValue->Value >= *arrayLength) return false;
    type = std::move(inlineArrayType);
    addr = std::move(inst->Arguments[0]);
    index = std::move(inst->Arguments[1]);
    return true;
}

// The C# `static bool MatchInlineArrayFirstElementRef(Call inst, out type,
// out addr, out isReadOnly)`.
bool MatchInlineArrayFirstElementRef(Call* inst, ITypePtr& type,
                                     std::unique_ptr<ILInstruction>& addr,
                                     bool& isReadOnly) {
    type = nullptr;
    addr = nullptr;
    isReadOnly = false;
    if (inst->Arguments.size() != 1) return false;
    ITypePtr inlineArrayType;
    if (MatchInlineArrayHelper(inst->Method.get(), "InlineArrayFirstElementRef",
                               inlineArrayType)) {
        isReadOnly = false;
    } else if (MatchInlineArrayHelper(inst->Method.get(),
                                      "InlineArrayFirstElementRefReadOnly",
                                      inlineArrayType)) {
        isReadOnly = true;
    } else {
        return false;
    }
    type = std::move(inlineArrayType);
    addr = std::move(inst->Arguments[0]);
    return true;
}

} // namespace

bool InlineArrayTransform::RunOnExpression(Call* inst, StatementTransformContext& context) {
    ITypePtr type;
    std::unique_ptr<ILInstruction> addr;
    std::unique_ptr<ILInstruction> index;
    bool isReadOnly = false;
    if (MatchSpanIndexerWithInlineArrayAsSpan(inst, type, addr, index, isReadOnly)) {
        context.Base.StepOnce(isReadOnly
                                  ? "call get_Item(addressof System.ReadOnlySpan{T}(call InlineArrayAsReadOnlySpan(addr)), index) -> readonly.ldelema.inlinearray(addr, index)"
                                  : "call get_Item(addressof System.Span{T}(call InlineArrayAsSpan(addr)), index) -> ldelema.inlinearray(addr, index)");
        auto newInst = MakeLdElemaInlineArray(std::move(type), std::move(addr),
                                              std::move(index), isReadOnly);
        inst->ReplaceWith(std::move(newInst));
        return true;
    }
    if (MatchInlineArrayElementRef(inst, type, addr, index, isReadOnly)) {
        context.Base.StepOnce(isReadOnly
                                  ? "call InlineArrayElementRefReadOnly(addr, index) -> readonly.ldelema.inlinearray(addr, index)"
                                  : "call InlineArrayElementRef(addr, index) -> ldelema.inlinearray(addr, index)");
        auto newInst = MakeLdElemaInlineArray(std::move(type), std::move(addr),
                                              std::move(index), isReadOnly);
        inst->ReplaceWith(std::move(newInst));
        return true;
    }
    if (MatchInlineArrayFirstElementRef(inst, type, addr, isReadOnly)) {
        context.Base.StepOnce(isReadOnly
                                  ? "call InlineArrayFirstElementRefReadOnly(addr) -> readonly.ldelema.inlinearray(addr, ldc.i4 0)"
                                  : "call InlineArrayFirstElementRef(addr) -> ldelema.inlinearray(addr, ldc.i4 0)");
        auto newInst = MakeLdElemaInlineArray(
            std::move(type), std::move(addr),
            std::make_unique<LdcI4>(0), isReadOnly);
        inst->ReplaceWith(std::move(newInst));
        return true;
    }
    return false;
}

} // namespace ILSpy::Decompiler::IL