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

// Port of ICSharpCode.Decompiler/Metadata/CodeMappingInfo.cs -- describes
// which parts of the (compiler-generated) code belong to which user code: a
// part is the body of a lambda, or the MoveNext method of an async/yield state
// machine. The C# class carries the MetadataFile reference and the owning
// TypeDef handle; the port carries the raw tokens (the port's metadata layer
// is token-based, the SRM-handle-free convention the MetadataFile readers
// use). The C# home of the builder (`CSharpDecompiler.GetCodeMappingInfo` +
// the private ReadCodeMappingInfo / TryGetExtensionImplementation helpers)
// lands in this file too -- the port has no CSharpDecompiler facade class yet,
// so the static builder is a free function next to its container (the
// RequiredNamespaceCollector consumes both).

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace ILSpy::Decompiler::Metadata {

class MetadataFile;

class CodeMappingInfo {
public:
    // The C# ctor `CodeMappingInfo(MetadataFile module, TypeDefinitionHandle
    // type)` -- the owning TypeDef token (0 = nil).
    CodeMappingInfo(const MetadataFile* module, std::uint32_t typeDefinitionToken);

    const MetadataFile* Module() const noexcept { return module_; }
    std::uint32_t TypeDefinition() const noexcept { return typeDefinition_; }

    // The C# `IEnumerable<MethodDefinitionHandle>
    // GetMethodParts(MethodDefinitionHandle method)` -- every part of the
    // method; a method with no recorded parts returns just itself. The port
    // appends into `result` (the C# lazy sequence ports to an out-vector; the
    // own-method fallback is included either way).
    void GetMethodParts(std::uint32_t method,
                        std::vector<std::uint32_t>& result) const;

    // The C# `MethodDefinitionHandle GetParentMethod(MethodDefinitionHandle
    // method)` -- the "calling method" of a part (a method's own parent is
    // itself); falls back to `method` when unmapped.
    std::uint32_t GetParentMethod(std::uint32_t method) const;

    // The C# `void AddMapping(MethodDefinitionHandle parent,
    // MethodDefinitionHandle part)` -- the bidirectional mapping; a part
    // already mapped is ignored (the first parent wins, the C# `if
    // (parents.ContainsKey(part)) return;`).
    void AddMapping(std::uint32_t parent, std::uint32_t part);

private:
    const MetadataFile* module_ = nullptr;
    std::uint32_t typeDefinition_ = 0;
    std::map<std::uint32_t, std::vector<std::uint32_t>, std::less<>> parts_;
    std::map<std::uint32_t, std::uint32_t, std::less<>> parents_;
};

// The C# `public static CodeMappingInfo CSharpDecompiler.GetCodeMappingInfo(
// MetadataFile module, EntityHandle member)` -- the CodeMappingInfo for a
// TypeDef or MethodDef token: the declaring TypeDef's methods walked for the
// lambda/state-machine/extension-implementation/local-function connections
// (the IL-body scans over Newobj/Stfld/Ldftn/Call/Callvirts). Returns an
// owning handle (the C# return is a GC reference). A nil `member` token
// yields an empty mapping over a nil TypeDef (the C# GetDeclaringType nil
// chain).
// The C# `LocalFunctionDecompiler.IsLocalFunctionMethod(MetadataFile,
// MethodDefinitionHandle)`: the metadata-level probe (the name shape + the
// compiler-generated gate). The DelegateConstruction transform's
// local-function rejection consults it.
bool IsLocalFunctionMethod(const MetadataFile& module,
                           std::uint32_t methodToken);

// The C# `AsyncAwaitDecompiler.IsCompilerGeneratedStateMachine` (a public
// static): the type is a nested [CompilerGenerated] type implementing
// System.Runtime.CompilerServices.IAsyncStateMachine.
bool IsCompilerGeneratedStateMachine(const MetadataFile& metadata,
                                     std::uint32_t typeDefToken);

// The C# `YieldReturnDecompiler.IsCompilerGeneratorEnumerator(
// TypeDefinitionHandle, MetadataReader)`: a nested compiler-generated type
// implementing System.Collections.IEnumerator (the .cpp keeps the full
// implementation; this declaration lets YieldReturnDecompiler consume it).
bool IsCompilerGeneratorEnumerator(const MetadataFile& metadata,
                                   std::uint32_t typeDefToken);

std::shared_ptr<CodeMappingInfo> GetCodeMappingInfo(const MetadataFile& module,
                                                    std::uint32_t memberToken);

} // namespace ILSpy::Decompiler::Metadata