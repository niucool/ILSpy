// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this
// software and associated documentation files (the "Software"), to deal in the Software
// without restriction, including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons
// to whom the Software is furnished to do so, subject to the following conditions:
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

#include "Decompiler/CSharp/CSharpDecompiler.hpp"

#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/PartialTypeInfo.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
#include "Decompiler/IL/ILReader.hpp"

#include <map>
#include <utility>

namespace ILSpy::Decompiler::CSharp {

std::vector<std::unique_ptr<IL::IILTransform>>
CSharpDecompiler::GetILTransforms() {
    return IL::GetILTransforms();
}

void CSharpDecompiler::RunILTransforms(IL::ILFunction& function,
                                       IL::ILTransformContext& context) {
    function.RunTransforms(GetILTransforms(), context);
}

std::string CSharpDecompiler::DecompileFunctionToString(
    IL::ILFunction& function, std::string_view returnType,
    std::string_view methodName, std::string_view paramDecl) {
    // The C# Decompile path's per-body half: the transform pipeline, then
    // the invariant check the CLI's inline block ran (the C# CheckInvariant
    // rides the RunTransforms loop; the trailing one mirrors the CLI
    // block's post-pipeline check), then the render.
    IL::ILTransformContext transformContext;
    RunILTransforms(function, transformContext);
    function.CheckInvariant(IL::ILPhase::Normal);
    return IL::ILAstToCSharp(function, returnType, methodName, paramDecl);
}

// The C# Decompile path's parameter-declaration builder (the CLI's inline
// block, extracted): named parameters carry their metadata name; unnamed
// parameters fall back to arg_<base + index> (base 1 for an instance
// method -- `this` is the implicit arg_0; base 0 static).
std::string CSharpDecompiler::MethodDeclString(
    const Metadata::MethodSignature& signature,
    const std::vector<std::string>& parameterNames) {
    std::string paramDecl;
    const int base = signature.IsInstance ? 1 : 0;
    for (std::size_t i = 0; i < signature.ParameterTypes.size(); ++i) {
        if (i != 0) paramDecl += ", ";
        paramDecl += IL::CSharpTypeName(signature.ParameterTypes[i]);
        paramDecl += ' ';
        if (i < parameterNames.size() && !parameterNames[i].empty())
            paramDecl += parameterNames[i];
        else
            paramDecl += "arg_" + std::to_string(base + static_cast<int>(i));
    }
    return paramDecl;
}

// The per-method decompile entry (the C# Decompile(params handles[])
// method-body half): the decode, the pipeline, the render. False when the
// body does not decode (the CLI skips those methods).
bool CSharpDecompiler::DecompileMethodToString(
    const Metadata::MetadataFile& file, std::uint32_t methodToken,
    std::uint32_t methodRva, const std::string& methodName,
    std::string& out) {
    auto fn = IL::ReadIL(file, methodToken, methodRva);
    if (!fn) return false;
    std::string returnType = "void";
    std::string paramDecl;
    if (auto sig = file.GetMethodSignature(methodToken)) {
        if (sig->ReturnType &&
            sig->ReturnType->ReflectionName() != "System.Void")
            returnType = IL::CSharpTypeName(sig->ReturnType);
        auto paramNames = file.GetParameterNames(methodToken);
        paramDecl = MethodDeclString(*sig, paramNames);
    }
    out = DecompileFunctionToString(*fn, returnType, methodName, paramDecl);
    return true;
}

// The type-level entry: the type's decodable method bodies rendered in
// sequence (the C# DecompileType's member iteration; the field/property
// surfaces land with the metadata-slice work).
bool CSharpDecompiler::DecompileTypeToString(
    const Metadata::MetadataFile& file, std::uint32_t typeToken,
    std::string& out) {
    // The C# DecompileType member iteration: the partial-type info gates
    // the members (the C# `DoDecompileMember`'s
    // `partialType.IsDeclaredMember(entity) -> return` skip, and the
    // `partial` modifier note -- the C# renders `partial` on the type
    // declaration; the port's ILAstToCSharp method-text renderer carries
    // no type-header surface, so the modifier is deferred with it).
    const Metadata::PartialTypeInfo* partialType =
        FindPartialTypeInfo(typeToken);
    bool rendered = false;
    for (const auto& m : file.GetMethods(typeToken)) {
        if (m.RVA == 0) continue;
        if (partialType != nullptr &&
            partialType->IsDeclaredMember(m.Token)) {
            continue;
        }
        std::string text;
        if (DecompileMethodToString(file, m.Token, m.RVA, m.Name, text)) {
            out += text;
            out += "\n";
            rendered = true;
        }
    }
    return rendered;
}

namespace {

// The C# `readonly Dictionary<TypeDefinitionHandle, PartialTypeInfo>
// partialTypes` (CSharpDecompiler.cs line 1479): the facade-level registry
// (a function-local static -- the C# field hangs off the CSharpDecompiler
// INSTANCE; the port's per-method entries are static today, so the registry
// rides the same static scope; the instance ctor lands with the
// metadata-slice ctor).
std::map<std::uint32_t, Metadata::PartialTypeInfo>& PartialTypes() {
    static std::map<std::uint32_t, Metadata::PartialTypeInfo> registry;
    return registry;
}

} // namespace

void CSharpDecompiler::AddPartialTypeDefinition(Metadata::PartialTypeInfo info) {
    // The C# `partialTypes.TryGetValue(info.DeclaringTypeDefinitionHandle,
    // out var existingInfo)` shape: a second registration unionizes.
    auto it = PartialTypes().find(info.DeclaringTypeDefinitionToken());
    if (it == PartialTypes().end()) {
        PartialTypes().emplace(info.DeclaringTypeDefinitionToken(),
                               std::move(info));
        return;
    }
    it->second.AddDeclaredMembers(info);
}

const Metadata::PartialTypeInfo* CSharpDecompiler::FindPartialTypeInfo(
    std::uint32_t declaringTypeToken) {
    auto it = PartialTypes().find(declaringTypeToken);
    return it == PartialTypes().end() ? nullptr : &it->second;
}

} // namespace ILSpy::Decompiler::CSharp
