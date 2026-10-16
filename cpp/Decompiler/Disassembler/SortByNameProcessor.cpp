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
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
// SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/Disassembler/SortByNameProcessor.cs (see the
// header for the porting decisions).

#include "Decompiler/Disassembler/SortByNameProcessor.hpp"

#include "Decompiler/Disassembler/DisassemblerSignatureTypeProvider.hpp"
#include "Decompiler/IL/InstructionOutputExtensions.hpp"
#include "Decompiler/Metadata/MetadataExtensions.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/MetadataGenericContext.hpp"
#include "Decompiler/Metadata/SignatureTypeProvider.hpp"
#include "Decompiler/Metadata/SRMExtensions.hpp"
#include "Decompiler/Output/PlainTextOutput.hpp"
#include "Decompiler/Util/StringComparers.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::Disassembler {

namespace {

// The C# `private static string GetSortKey(MethodDefinitionHandle handle,
// MetadataFile module)`: the method name, the backtick arity when the
// signature is generic, then the parameter list -- the sort key that keeps
// overloads adjacent but distinguished ("Copy(string)" vs "Copy(string, int32)").
std::string GetMethodSortKey(const Metadata::MetadataFile& module,
    std::uint32_t methodToken) {
    Output::PlainTextOutput output;
    // The C# writes the raw name (metadata.GetString(definition.Name)), no
    // escaping.
    output.Write(module.GetMethodName(methodToken));

    // The C# `definition.DecodeSignature(new
    // DisassemblerSignatureTypeProvider(module, output), new
    // MetadataGenericContext(handle, module))`. Provider and decoder outlive
    // the deferred type writers WriteParameterList invokes (they live in this
    // scope -- the provider-outlives-writers contract).
    auto blob = module.GetSignatureBlob(methodToken);
    if (!blob)
        throw std::logic_error("SortByNameProcessor: method signature blob");
    Metadata::MetadataGenericContext genericContext =
        Metadata::MetadataGenericContext::ForMethod(methodToken, module);
    DisassemblerSignatureTypeProvider provider(module, output);
    Metadata::SignatureTypeProviderDecoder decoder(provider, module);
    Metadata::MethodSignatureT signature =
        decoder.DecodeMethodSignature(blob->data(), blob->size(), genericContext);

    if (signature.GenericParameterCount > 0) {
        // The C# `output.Write($"`{signature.GenericParameterCount}")`.
        output.Write("`" + std::to_string(signature.GenericParameterCount));
    }
    IL::WriteParameterList(output, signature);
    return output.ToString();
}

}  // namespace

std::vector<std::uint32_t> SortByNameProcessor::Process(
    const Metadata::MetadataFile& module,
    const std::vector<std::uint32_t>& items, ProcessedEntityKind kind) const {
    // The C# `items.OrderBy(item => GetSortKey(item, module)).ToArray()`:
    // compute every key (a throw surfaces before any reorder, matching
    // OrderBy's eager key materialization), then stably sort -- OrderBy is
    // documented stable, so equal-key items keep their original order.
    std::vector<std::pair<std::string, std::uint32_t>> keyed;
    keyed.reserve(items.size());
    for (std::uint32_t token : items) {
        keyed.emplace_back(GetSortKey(module, token, kind), token);
    }
    std::stable_sort(keyed.begin(), keyed.end(),
        [](const std::pair<std::string, std::uint32_t>& a,
            const std::pair<std::string, std::uint32_t>& b) {
            return Util::CompareInvariantCulture(a.first, b.first) < 0;
        });
    std::vector<std::uint32_t> result;
    result.reserve(keyed.size());
    for (auto& entry : keyed)
        result.push_back(entry.second);
    return result;
}

std::string SortByNameProcessor::GetSortKey(const Metadata::MetadataFile& module,
    std::uint32_t token, ProcessedEntityKind kind) {
    switch (kind) {
        case ProcessedEntityKind::InterfaceImplementation: {
            // The C# `GetSortKey(InterfaceImplementationHandle)`: the
            // Interface column's full type name (a TypeSpec generic
            // instantiation shrinks to its head).
            auto impl = module.GetInterfaceImplementation(token);
            if (!impl)
                throw std::out_of_range(
                    "SortByNameProcessor: invalid InterfaceImpl token");
            return Metadata::ToILNameString(
                Metadata::GetFullTypeName(module, impl->InterfaceToken));
        }
        case ProcessedEntityKind::TypeDefinition:
            // The C# `GetSortKey(TypeDefinitionHandle)`.
            return Metadata::ToILNameString(
                Metadata::GetFullTypeName(module, token));
        case ProcessedEntityKind::MethodDefinition:
            return GetMethodSortKey(module, token);
        case ProcessedEntityKind::PropertyDefinition:
            // The C# `GetSortKey(PropertyDefinitionHandle)`: the row's Name.
            return module.GetPropertyName(token);
        case ProcessedEntityKind::EventDefinition:
            // The C# `GetSortKey(EventDefinitionHandle)`: the row's Name.
            return module.GetEventName(token);
        case ProcessedEntityKind::FieldDefinition:
            // The C# `GetSortKey(FieldDefinitionHandle)`: the row's Name.
            return module.GetFieldName(token);
        case ProcessedEntityKind::CustomAttribute: {
            // The C# `GetSortKey(CustomAttributeHandle)`: the constructor's
            // declaring type's full name -- the attribute type's name, which
            // orders [Obsolete] before [Serializable] style rows
            // alphabetically.
            auto row = module.GetCustomAttribute(token);
            if (!row)
                throw std::out_of_range(
                    "SortByNameProcessor: invalid CustomAttribute token");
            return Metadata::ToILNameString(Metadata::GetFullTypeName(
                module, Metadata::GetDeclaringType(module,
                    row->ConstructorToken)));
        }
    }
    throw std::logic_error("SortByNameProcessor: unknown entity kind");
}

}  // namespace ILSpy::Decompiler::Disassembler
