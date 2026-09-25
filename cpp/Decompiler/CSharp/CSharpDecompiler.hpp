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

// Port of ICSharpCode.Decompiler/CSharp/CSharpDecompiler.cs -- the decompiler
// facade (the C# `public class CSharpDecompiler`). The transform pipeline
// (GetILTransforms) and the per-body decompile driver (RunTransforms) live in
// the IL namespace (the Phase-7 landing note in GetILTransforms.hpp); this
// facade is the C#-shaped home the CLI and the later language consumers
// drive. Sliced: the pipeline entry (GetILTransforms + RunILTransforms over
// the IL-namespace factory) and the per-body decompile (the
// DecodeMethodBody + DecompileFunctionToString pair the C# Decompile path
// runs); the ctor over a MetadataFile + settings, the type/member
// enumeration surfaces (DecompileTypes/DecompileType/
// DecompileModuleAndAssemblyAttributes), and the AddPartialTypeDefinition
// machinery land with the metadata-module slices.

#pragma once

#include "Decompiler/IL/Instructions/ILFunction.hpp"
#include "Decompiler/IL/Transforms/IILTransform.hpp"
#include "Decompiler/TypeSystem/MetadataModule.hpp"
#include "Decompiler/Metadata/PartialTypeInfo.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::Metadata {
struct MethodSignature;
class MetadataFile;
// The partial-type info (PartialTypeInfo.cs -- defined out-of-line in the
// .cpp; the port's registry is token-keyed).
class PartialTypeInfo;
} // namespace ILSpy::Decompiler::Metadata

namespace ILSpy::Decompiler {
class DecompileRun;
class DecompilerSettings;
} // namespace ILSpy::Decompiler

namespace ILSpy::Decompiler::TypeSystem {
class ITypeResolveContext;
} // namespace ILSpy::Decompiler::TypeSystem

namespace ILSpy::Decompiler::CSharp {
namespace Syntax {
class AstNode;
class TypeSystemAstBuilder;
class EntityDeclaration;
class SyntaxTree;
} // namespace Syntax
namespace Transforms { class IAstTransform; }

class CSharpDecompiler {
public:
    // The C# `public static List<IILTransform> GetILTransforms()` -- the
    // fixed per-body pipeline (the IL-namespace factory; the C#-shaped
    // alias the facade's consumers drive).
    static std::vector<std::unique_ptr<IL::IILTransform>> GetILTransforms();

    // The C# `function.RunTransforms(CSharpDecompiler.GetILTransforms(),
    // context)` shape -- the per-body pipeline driver over the factory.
    static void RunILTransforms(IL::ILFunction& function,
                                IL::ILTransformContext& context);

    // The metadata-wired overload: the context carries the module (the C#
    // context.PEFile) and the deep-decode resolver (the C#
    // context.CreateILReader() body read) so the closure transforms
    // (DelegateConstruction's lambda embedding) can decode nested bodies.
    static void RunILTransforms(IL::ILFunction& function,
                                const ::ILSpy::Decompiler::Metadata::
                                    MetadataFile& file);

    // The C# `public static List<IAstTransform> GetAstTransforms()`
    // (CSharpDecompiler.cs line 237): the C# AST pipeline's transform list
    // as a fresh-instance vector, the ported entries at their C# positions
    // (the unported transforms are loud comments at their slots).
    static std::vector<std::unique_ptr<Transforms::IAstTransform>>
    GetAstTransforms();

    // The C# `void RunTransforms(AstNode rootNode, DecompileRun
    // decompileRun, ITypeResolveContext decompilationContext)` (line 778):
    // the AST pipeline driver -- the transform loop with the invariant
    // checks between entries, then the InsertParenthesesVisitor (the
    // readability flag on) and the GenericGrammarAmbiguityVisitor tail.
    static void RunAstTransforms(
        Syntax::AstNode& rootNode, ::ILSpy::Decompiler::DecompileRun& decompileRun,
        const ::ILSpy::Decompiler::TypeSystem::ITypeResolveContext*
            decompilationContext = nullptr);

    // The C# `static TypeSystemAstBuilder CreateAstBuilder(DecompilerSettings
    // settings)` (line 722): the type renderer the transform context carries
    // (the insertion arms of DeclareVariables and the remaining
    // transform family consume ConvertType through it).
    static Syntax::TypeSystemAstBuilder CreateAstBuilder(
        const ::ILSpy::Decompiler::DecompilerSettings& settings);

    // The C# `internal static bool RemoveAttribute(EntityDeclaration entityDecl,
    // KnownAttribute attributeType)` (CSharpDecompiler.cs line 2342): removes
    // the sections' attributes whose type resolves to the known attribute
    // type; empty sections are dropped. Returns whether any attribute was
    // removed.
    static bool RemoveAttribute(Syntax::EntityDeclaration& entityDecl,
                                ::ILSpy::Decompiler::TypeSystem::KnownAttribute
                                    attributeType);

    // The C# Decompile path's per-body half (the DecodeMethodBody +
    // decompile-body flow the CLI's --csharp block carries inline): runs
    // the transform pipeline over the function and renders the C#-ish
    // method text. The return-type/parameter declaration strings are the
    // caller's (the metadata signature reader's output; the facade's
    // metadata entry takes the file and derives them, deferred with the
    // metadata-surface slice).
    static std::string DecompileFunctionToString(
        IL::ILFunction& function, std::string_view returnType,
        std::string_view methodName, std::string_view paramDecl);

    // The C# Decompile path's parameter-declaration builder (the CLI's
    // inline block): named parameters carry their metadata name; unnamed
    // parameters fall back to arg_<base + index> (base 1 for an instance
    // method -- `this` is the implicit arg_0; base 0 static).
    static std::string MethodDeclString(
        const ::ILSpy::Decompiler::Metadata::MethodSignature& signature,
        const std::vector<std::string>& parameterNames);

    // The per-method decompile entry (the C# Decompile(params handles[])
    // method-body half): decode the body, run the pipeline, render. False
    // when the body does not decode (the CLI skips those methods). A
    // constructor renders with the TYPE name and no return type (the
    // methodName then carries the type name).
    static bool DecompileMethodToString(
        const ::ILSpy::Decompiler::Metadata::MetadataFile& file,
        std::uint32_t methodToken, std::uint32_t methodRva,
        const std::string& methodName, std::string& out,
        bool isConstructor = false);

    // The type-level entry: the type's decodable method bodies rendered in
    // sequence. True when at least one body rendered (the C#
    // DecompileType's member iteration; the field/property surfaces land
    // with the metadata-slice work).
    static bool DecompileTypeToString(
        const ::ILSpy::Decompiler::Metadata::MetadataFile& file,
        std::uint32_t typeToken, std::string& out);

    // The C# `public string DecompileModuleAndAssemblyAttributesToString()`
    // (CSharpDecompiler.cs line 838): the `[assembly: ...]` /
    // `[module: ...]` attribute sections over the module's attribute rows.
    static std::string DecompileModuleAndAssemblyAttributesToString(
        const ::ILSpy::Decompiler::TypeSystem::MetadataModule& module);

    // The C# `public string DecompileWholeModuleAsString()` (line 1220):
    // the whole-module render -- the module/assembly attribute sections,
    // then every type in metadata order.
    static std::string DecompileWholeModuleToString(
        const ::ILSpy::Decompiler::Metadata::MetadataFile& file);

    // The C# `public SyntaxTree DecompileModuleAndAssemblyAttributes()`
    // (CSharpDecompiler.cs line 823): the AST path -- the attribute
    // sections built through the TypeSystemAstBuilder's ConvertAttribute
    // and the transform pipeline run over the tree. The caller owns the
    // returned tree (the port's node model is non-owning-new).
    static Syntax::SyntaxTree* DecompileModuleAndAssemblyAttributes(
        const ::ILSpy::Decompiler::TypeSystem::MetadataModule& module);

    // The C# `void DoDecompileModuleAndAssemblyAttributes(DecompileRun,
    // ITypeResolveContext, SyntaxTree)` (line 843): the section builder
    // the two entries above share (the public one and the whole-module
    // path).
    static void DoDecompileModuleAndAssemblyAttributes(
        const ::ILSpy::Decompiler::DecompileRun& decompileRun,
        const ::ILSpy::Decompiler::TypeSystem::MetadataModule& module,
        Syntax::SyntaxTree& syntaxTree);

    // The C# `public void AddPartialTypeDefinition(PartialTypeInfo info)`
    // (CSharpDecompiler.cs line 1481): register the partial-type info under
    // its declaring type token (a second registration for the same type
    // unionizes the declared-member sets, the C# AddDeclaredMembers path).
    static void AddPartialTypeDefinition(
        ::ILSpy::Decompiler::Metadata::PartialTypeInfo info);

    // The registry probe (the C# `partialTypes.TryGetValue(...)` shape the
    // member iteration consults): the registered info for a type token, or
    // null.
    static const ::ILSpy::Decompiler::Metadata::PartialTypeInfo*
        FindPartialTypeInfo(
        std::uint32_t declaringTypeToken);

    // The registry lifecycle (the port's addition for the static
    // placeholder): the C# registry lives on the CSharpDecompiler INSTANCE
    // (fresh per decompiler); the port's process-global static needs an
    // explicit reset so one consumer's registrations do not leak into
    // another's decompilation.
    static void ClearPartialTypes();
};

} // namespace ILSpy::Decompiler::CSharp
