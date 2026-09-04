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

#include "Decompiler/Metadata/MetadataGenericContext.hpp"

#include "Decompiler/Metadata/MetadataFile.hpp"

#include <cstddef>

namespace ILSpy::Decompiler::Metadata {

namespace {

// The C# `genericParameters[index]` positional read over the owner's rows,
// with the C# nil-handle guards applied: a nil owner handle, a negative
// index, a null metadata, or an index past the collection all yield nullptr
// (the nil-handle arm every public query falls back to). `rows` is the
// out-param that keeps the returned row alive for the caller's scope.
const GenericParameterInfo* GenericParameterAt(const MetadataFile* module,
                                               std::uint32_t ownerToken,
                                               int index,
                                               std::vector<GenericParameterInfo>& rows)
{
    if (module == nullptr || ownerToken == 0 || index < 0) return nullptr;
    rows = module->GetGenericParameters(ownerToken);
    if (static_cast<std::size_t>(index) >= rows.size()) return nullptr;
    return &rows[static_cast<std::size_t>(index)];
}

} // namespace

MetadataGenericContext MetadataGenericContext::ForMethod(std::uint32_t methodToken, const MetadataFile& module) {
    MetadataGenericContext ctx;
    ctx.module_ = &module;
    ctx.methodToken_ = methodToken;
    // The C# ctor's metadata.GetMethodDefinition(method).GetDeclaringType().
    // An invalid methodToken resolves to 0 (the nil context -- the port's
    // never-throw fallback where the C# GetRow would throw).
    ctx.declaringTypeToken_ = module.GetMethodDeclaringTypeToken(methodToken);
    return ctx;
}

MetadataGenericContext MetadataGenericContext::ForType(std::uint32_t typeToken, const MetadataFile& module) {
    MetadataGenericContext ctx;
    ctx.module_ = &module;
    // The C# type-context ctor leaves method = default (the nil handle).
    ctx.declaringTypeToken_ = typeToken;
    return ctx;
}

// The C# `default(MetadataGenericContext)`: a null module and nil handles
// (the implicit default construction -- every query takes the nil-handle
// fallbacks).
MetadataGenericContext MetadataGenericContext::Nil() {
    return MetadataGenericContext();
}

std::uint32_t MetadataGenericContext::GetGenericTypeParameterHandleOrNull(int index) const {
    std::vector<GenericParameterInfo> rows;
    const GenericParameterInfo* row = GenericParameterAt(module_, declaringTypeToken_, index, rows);
    return row != nullptr ? row->Token : 0;
}

std::uint32_t MetadataGenericContext::GetGenericMethodTypeParameterHandleOrNull(int index) const {
    std::vector<GenericParameterInfo> rows;
    const GenericParameterInfo* row = GenericParameterAt(module_, methodToken_, index, rows);
    return row != nullptr ? row->Token : 0;
}

std::string MetadataGenericContext::GetGenericTypeParameterName(int index) const {
    std::vector<GenericParameterInfo> rows;
    const GenericParameterInfo* row = GenericParameterAt(module_, declaringTypeToken_, index, rows);
    return row != nullptr ? row->Name : std::to_string(index);
}

std::string MetadataGenericContext::GetGenericMethodTypeParameterName(int index) const {
    std::vector<GenericParameterInfo> rows;
    const GenericParameterInfo* row = GenericParameterAt(module_, methodToken_, index, rows);
    return row != nullptr ? row->Name : std::to_string(index);
}

} // namespace ILSpy::Decompiler::Metadata
