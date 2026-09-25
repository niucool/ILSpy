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

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/CSharpDecompiler.hpp"
#include "Decompiler/CSharp/ILAstToCSharp.hpp"
#include "Decompiler/CSharp/RequiredNamespaceCollector.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/Util/CacheManager.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"
#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/PrettifyAssignments.hpp"
#include "Decompiler/CSharp/Transforms/NormalizeBlockStatements.hpp"
#include "Decompiler/CSharp/Transforms/FlattenSwitchBlocks.hpp"
#include "Decompiler/CSharp/Transforms/FixNameCollisions.hpp"
#include "Decompiler/CSharp/OutputVisitor/InsertParenthesesVisitor.hpp"
#include "Decompiler/CSharp/OutputVisitor/GenericGrammarAmbiguityVisitor.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/PartialTypeInfo.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/TypeSystem/DecompilerTypeSystem.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
#include "Decompiler/IL/ILReader.hpp"

#include <map>
#include <optional>
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

// The context wiring the C# ILTransformContext carries natively: the
// PEFile (the port's Metadata pointer) and the CreateILReader deep-decode
// entry (the port's DelegateBodyResolver hook over ReadIL).
static void WireTransformContext(IL::ILTransformContext& context,
                                 const Metadata::MetadataFile& file) {
    context.Metadata = const_cast<Metadata::MetadataFile*>(&file);
    context.DelegateBodyResolver =
        [&file](std::uint32_t methodToken,
                std::uint32_t methodRva) -> std::unique_ptr<IL::ILFunction> {
            if (methodRva == 0) return nullptr;
            return IL::ReadIL(file, methodToken, methodRva);
        };
}

void CSharpDecompiler::RunILTransforms(IL::ILFunction& function,
                                       const Metadata::MetadataFile& file) {
    IL::ILTransformContext context;
    WireTransformContext(context, file);
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
    std::string& out, bool isConstructor) {
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
    // The pipeline run rides the metadata-wired overload (the C# context
    // carries the PEFile for the closure transforms' deep-decode).
    IL::ILTransformContext transformContext;
    WireTransformContext(transformContext, file);
    RunILTransforms(*fn, transformContext);
    fn->CheckInvariant(IL::ILPhase::Normal);
    out = IL::ILAstToCSharp(*fn, returnType, methodName, paramDecl,
                            isConstructor);
    return true;
}



// The C# instance ctor's type-system wiring (`typeSystem = new
// DecompilerTypeSystem(module, settings)`, CSharpDecompiler.cs line 218)
// ports as the DecompilerTypeSystem over the referenced-assembly set: the
// resolver's search directory is the main file's own directory, every
// AssemblyReference row resolves through it (the plain first-level arm;
// the facade/implicit-reference arms ride deferred -- the
// DecompilerTypeSystem.hpp notes), and a TypeRef scoped to an AssemblyRef
// resolves into the referenced module's real entity.


// The render body the static and instance DecompileTypeToString entries
// share: everything but the module wiring (the static entry builds it per
// call; the instance uses its own) and the registry lookup (the static
// consults the process-global placeholder; the instance its own map).
bool DecompileTypeToStringBody(
    const Metadata::MetadataFile& file, TS::MetadataModule& module,
    const Metadata::PartialTypeInfo* partialType,
    std::uint32_t typeToken, std::string& out) {
    // The C# DecompileType member iteration: the partial-type info gates
    // the members (the C# `DoDecompileMember`'s
    // `partialType.IsDeclaredMember(entity) -> return` skip, and the
    // `partial` modifier note -- the C# renders `partial` on the type
    // declaration; the port's ILAstToCSharp method-text renderer carries
    // no type-header surface, so the modifier is deferred with it).
    bool rendered = false;
    // The type's own name (the constructor headers render it; the empty
    // form covers an unknown token -- the member iteration then renders no
    // constructor).
    std::string typeName;
    // The type declaration header (the C# DecompileType's
    // `TypeDeclaration` emission): `public partial class Name` -- the
    // partial modifier rides the generated-half convention (the XAML
    // code-behind shape) whenever the type renders members; the
    // `Kind`-specific keyword (struct/interface) follows the
    // TypeDef kind.
    for (const auto& t : file.TypeDefs()) {
        if (t.Token != typeToken) continue;
        typeName = t.Name;
        const char* keyword = "class";
        switch (t.Kind) {
            case ::ILSpy::Decompiler::TypeSystem::TypeKind::Struct:
            case ::ILSpy::Decompiler::TypeSystem::TypeKind::Enum:
                keyword = "struct";
                break;
            case ::ILSpy::Decompiler::TypeSystem::TypeKind::Interface:
                keyword = "interface";
                break;
            default:
                break;
        }
        // The C# nests the type under `namespace ... { ... }`; the port's
        // flat render carries the namespace in the call-site comment (the
        // CLI's `// MyApp.Page1` header), so the declaration carries the
        // bare name.
        out += "public partial ";
        out += keyword;
        out += ' ';
        out += t.Name;
        out += "\n{\n";
        rendered = true;
        break;
    }
    // The property declarations (the C# DecompileType's DoDecompileMember
    // property arm): the `Type Name { get; set; }` shape -- the type from
    // the getter's return signature (the C# reads the property signature;
    // the accessor's return type carries the same type by the compiler's
    // contract; a setter-only property reads the setter's first parameter),
    // the accessor presence from the MethodSemantics lookup. The accessor
    // methods are collected for the method-loop skip (the C# renders them
    // through the property), and the compiler-generated
    // `<Name>k__BackingField` field declarations disappear (the
    // auto-property end state the AST-level transform produces; only the
    // compiler can emit those names, the C# identifier grammar excludes
    // angle brackets).
    std::set<std::uint32_t> accessorTokens;
    std::set<std::string> backingFieldNames;
    for (const auto& p : file.GetProperties(typeToken)) {
        auto accessors = file.GetPropertyAccessors(p.Token);
        std::string propertyTypeName = "var";
        if (accessors.GetterToken != 0) {
            if (auto sig = file.GetMethodSignature(accessors.GetterToken)) {
                if (sig->ReturnType)
                    propertyTypeName = IL::CSharpTypeName(sig->ReturnType);
            }
        } else if (accessors.SetterToken != 0) {
            if (auto sig = file.GetMethodSignature(accessors.SetterToken)) {
                if (!sig->ParameterTypes.empty() && sig->ParameterTypes[0])
                    propertyTypeName =
                        IL::CSharpTypeName(sig->ParameterTypes[0]);
            }
        }
        out += propertyTypeName;
        out += ' ';
        out += p.Name;
        out += " { ";
        bool anyAccessor = false;
        if (accessors.GetterToken != 0) {
            out += "get; ";
            anyAccessor = true;
        }
        if (accessors.SetterToken != 0) {
            out += "set; ";
            anyAccessor = true;
        }
        out += "}\n";
        if (anyAccessor)
            rendered = true;
        accessorTokens.insert(accessors.GetterToken);
        accessorTokens.insert(accessors.SetterToken);
        for (std::uint32_t token : accessors.OtherTokens)
            accessorTokens.insert(token);
        backingFieldNames.insert("<" + p.Name + ">k__BackingField");
    }
    // The event declarations (the C# DoDecompileMember's event arm): the
    // `event Type Name;` shape -- the type through the module's event
    // entity (the C# entity.ReturnType, the declaring type's generic
    // context included), the add/remove accessor methods collected for
    // the method-loop skip (the C# renders them through the event; their
    // bodies are the flat renderer's documented stand-in gap, like the
    // fields'). The module param carries the wiring (the static entry's
    // per-call pair or the instance's own).
    if (!file.GetEvents(typeToken).empty()) {
        for (const auto& e : file.GetEvents(typeToken)) {
            auto accessors = file.GetEventAccessors(e.Token);
            accessorTokens.insert(accessors.AdderToken);
            accessorTokens.insert(accessors.RemoverToken);
            accessorTokens.insert(accessors.RaiserToken);
            for (std::uint32_t token : accessors.OtherTokens)
                accessorTokens.insert(token);
            std::string eventTypeName = "object";
            const TS::IEvent* event = module.GetDefinitionEvent(e.Token);
            if (event != nullptr) {
                // A non-owning alias (the module's entity cache owns the
                // event and its resolved return type -- the no-op-deleter
                // convention; the const_cast is the established
                // mutable-object-behind-the-const-ref convention).
                TS::ITypePtr eventType(
                    const_cast<TS::IType*>(&event->ReturnType()),
                    [](TS::IType*) {});
                eventTypeName = IL::CSharpTypeName(eventType);
            }
            out += "event ";
            out += eventTypeName;
            out += ' ';
            out += e.Name;
            out += ";\n";
            rendered = true;
        }
    }
    // The field declarations (the C# DecompileType's field members): the
    // `Type name;` shape from GetFields + GetFieldSignature (the C#
    // AstBuilder renders the modifiers and the initializer from the IL --
    // the declaration-only stand-in renders the type and the name).
    for (const auto& f : file.GetFields(typeToken)) {
        if (backingFieldNames.count(f.Name) != 0)
            continue;
        auto fieldType = file.GetFieldSignature(f.Token);
        std::string fieldTypeName =
            fieldType ? IL::CSharpTypeName(fieldType)
                      : std::string("var");
        out += fieldTypeName;
        out += ' ';
        out += f.Name;
        out += ";\n";
        rendered = true;
    }
    for (const auto& m : file.GetMethods(typeToken)) {
        if (m.RVA == 0) continue;
        if (accessorTokens.count(m.Token) != 0)
            continue;
        if (partialType != nullptr &&
            partialType->IsDeclaredMember(m.Token)) {
            continue;
        }
        // The constructor arm (the C# DoDecompileMember's constructor
        // case): the instance/type constructor renders with the TYPE name
        // and no return type.
        bool isConstructor = m.Name == ".ctor" || m.Name == ".cctor";
        const std::string& methodName = isConstructor ? typeName : m.Name;
        std::string text;
        if (CSharpDecompiler::DecompileMethodToString(
                file, m.Token, m.RVA, methodName, text, isConstructor)) {
            out += text;
            out += "\n";
            rendered = true;
        }
    }
    if (rendered) {
        out += "}\n";
    }
    return rendered;
}

// The type-level entry: the type's decodable method bodies rendered in
// sequence (the C# DecompileType's member iteration; the field/property
// surfaces land with the metadata-slice work).
bool CSharpDecompiler::DecompileTypeToString(
    const Metadata::MetadataFile& file, std::uint32_t typeToken,
    std::string& out) {
    // The static scaffold's per-call wiring (the note above): the resolver
    // + the reference-loaded type system, one pair per call.
    Metadata::UniversalAssemblyResolver resolver(
        file.FileName(), false, Metadata::DetectTargetFrameworkId(file));
    TS::DecompilerTypeSystem typeSystem(file, resolver);
    return DecompileTypeToStringBody(
        file, typeSystem.MainMetadataModule(),
        FindRegisteredPartialType(typeToken), typeToken, out);
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

void CSharpDecompiler::RegisterPartialTypeDefinition(
    Metadata::PartialTypeInfo info) {
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

const Metadata::PartialTypeInfo* CSharpDecompiler::FindRegisteredPartialType(
    std::uint32_t declaringTypeToken) {
    auto it = PartialTypes().find(declaringTypeToken);
    return it == PartialTypes().end() ? nullptr : &it->second;
}

void CSharpDecompiler::ClearPartialTypes() {
    PartialTypes().clear();
}

// ---- the instance surface (the C# CSharpDecompiler object) ----

// The per-instance state behind the pimpl: the resolver + the
// reference-loaded type system (the C# typeSystem field; the resolver
// rides FIRST -- its keep-alive registry owns the loaded referenced files,
// and the reverse-declaration destruction order tears the type system
// down before the registry), the partial-types registry (the C#
// `readonly Dictionary<...>` field -- per-instance, unlike the
// process-global static above), the settings copy, and the file the wiring
// was built over.
struct CSharpDecompiler::InstanceState {
    std::unique_ptr<Metadata::UniversalAssemblyResolver> resolver;
    std::optional<TS::DecompilerTypeSystem> typeSystem;
    std::map<std::uint32_t, Metadata::PartialTypeInfo> partialTypes;
    // The C# holds the settings REFERENCE (the caller's mutable object);
    // the port copies -- the caller-side mutation between calls lands
    // with the DecompileRun adoption.
    ::ILSpy::Decompiler::DecompilerSettings settings;
    const Metadata::MetadataFile* file = nullptr;
};

CSharpDecompiler::CSharpDecompiler(
    const Metadata::MetadataFile& file,
    const ::ILSpy::Decompiler::DecompilerSettings& settings)
    : state_(std::make_unique<InstanceState>()) {
    // The C# CreateTypeSystemFromFile's resolver shape: the resolver over
    // the main file's own name (the ctor derives the base directory and
    // registers it as the FIRST search directory). The CLI's GetDecompiler
    // passes throwOnError FALSE (an unresolved reference degrades to the
    // name-only fallbacks, never a ResolutionException); the port adopts
    // the CLI shape -- the settings.ThrowOnAssemblyResolveErrors arm rides
    // with the settings-driven resolver ctor.
    state_->settings = settings;
    state_->file = &file;
    state_->resolver =
        std::make_unique<Metadata::UniversalAssemblyResolver>(
            file.FileName(), false, Metadata::DetectTargetFrameworkId(file));
    state_->typeSystem.emplace(file, *state_->resolver);
}

CSharpDecompiler::~CSharpDecompiler() = default;

std::string CSharpDecompiler::DecompileWholeModuleToString() {
    // The C# DecompileWholeModuleAsSingleFile composition over THIS
    // instance's wiring: the attribute sections, then every type in
    // metadata order through the instance entries (the registry
    // consults this instance's map).
    std::string out;
    out += DecompileModuleAndAssemblyAttributesToString(
        state_->typeSystem->MainMetadataModule());
    for (const auto& t : state_->file->TypeDefs()) {
        if (t.Name == "<Module>")
            continue;
        std::string text;
        if (DecompileTypeToString(t.Token, text))
            out += text;
    }
    return out;
}

bool CSharpDecompiler::DecompileTypeToString(
    std::uint32_t typeToken, std::string& out) {
    return DecompileTypeToStringBody(
        *state_->file, state_->typeSystem->MainMetadataModule(),
        FindPartialTypeInfo(typeToken), typeToken, out);
}

void CSharpDecompiler::AddPartialTypeDefinition(
    Metadata::PartialTypeInfo info) {
    // The C# `partialTypes.TryGetValue(info.DeclaringTypeDefinitionHandle,
    // out var existingInfo)` shape over the INSTANCE map: a second
    // registration unionizes.
    auto it = state_->partialTypes.find(
        info.DeclaringTypeDefinitionToken());
    if (it == state_->partialTypes.end()) {
        state_->partialTypes.emplace(
            info.DeclaringTypeDefinitionToken(), std::move(info));
        return;
    }
    it->second.AddDeclaredMembers(info);
}

const Metadata::PartialTypeInfo* CSharpDecompiler::FindPartialTypeInfo(
    std::uint32_t declaringTypeToken) const {
    auto it = state_->partialTypes.find(declaringTypeToken);
    return it == state_->partialTypes.end() ? nullptr : &it->second;
}

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;


// The C# `typeSystem.GetNamespaceByFullName(ns)` (CreateDecompileRun's
// resolver, line 758): the root-namespace walk over the dotted name.
const TS::INamespace* ResolveNamespaceFullName(const TS::INamespace& root,
                                              const std::string& fullName) {
    const TS::INamespace* current = &root;
    std::size_t start = 0;
    while (current != nullptr) {
        std::size_t dot = fullName.find('.', start);
        std::string part =
            fullName.substr(start,
                            dot == std::string::npos ? std::string::npos
                                                    : dot - start);
        if (part.empty())
            return nullptr;
        current = current->GetChildNamespace(part);
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    return current;
}

} // namespace

// The C# `public SyntaxTree DecompileModuleAndAssemblyAttributes()`
// (CSharpDecompiler.cs line 823): the AST path -- the attribute sections
// built through the TypeSystemAstBuilder's ConvertAttribute, then the
// transform pipeline (RunTransforms) over the tree. The tree is returned
// raw (the caller owns it; the port's node model is non-owning-new).
Syntax::SyntaxTree* CSharpDecompiler::DecompileModuleAndAssemblyAttributes(
    const TS::MetadataModule& module) {
    // The C# `RequiredNamespaceCollector.CollectAttributeNamespaces(module,
    // namespaces)` + `CreateDecompileRun(namespaces)`: the namespaces feed
    // the run's using scope (the IntroduceUsingDeclarations transform's
    // input; that transform is deferred, so the scope content is inert
    // today, but the run is wired the C# way).
    std::unordered_set<std::string> namespaces;
    // The collector takes the mutable module (the C# metadata walk);
    // const_cast mirrors the C# nullability-free contract. The free
    // function form (the C# static class method ports as a namespace-scope
    // function).
    CollectAttributeNamespaces(const_cast<TS::MetadataModule&>(module),
                              namespaces);
    std::vector<const TS::INamespace*> resolvedNamespaces;
    for (const std::string& ns : namespaces) {
        const TS::INamespace* resolvedNamespace =
            ResolveNamespaceFullName(module.RootNamespace(), ns);
        if (resolvedNamespace != nullptr)
            resolvedNamespaces.push_back(resolvedNamespace);
    }
    auto usingScope = std::make_shared<CSharp::TypeSystem::UsingScope>(
        std::make_shared<CSharp::TypeSystem::CSharpTypeResolveContext>(module),
        module.RootNamespace(), resolvedNamespaces);

    DecompilerSettings settings;
    DecompileRun decompileRun(&settings, usingScope);

    auto* syntaxTree = new Syntax::SyntaxTree();
    DoDecompileModuleAndAssemblyAttributes(decompileRun, module, *syntaxTree);
    // The C# `RunTransforms(syntaxTree, decompileRun, typeSystem)` -- the
    // IDecompilerTypeSystem as the decompilation context (its CurrentModule
    // is the main module; the port's SimpleTypeResolveContext over the
    // module carries the same pair).
    TS::SimpleTypeResolveContext decompilationContext(module);
    RunAstTransforms(*syntaxTree, decompileRun, &decompilationContext);
    return syntaxTree;
}

// The C# `void DoDecompileModuleAndAssemblyAttributes(DecompileRun
// decompileRun, ITypeResolveContext decompilationContext, SyntaxTree
// syntaxTree)` (line 843): the `[assembly: ...]` / `[module: ...]`
// attribute sections over the module's attribute rows, each attribute
// rendered through the TypeSystemAstBuilder (the ConvertAttribute shape:
// the type name with the trailing "Attribute" suffix stripped, the fixed
// and named arguments as their constant literals, the decode-error arm as
// the comment form). The C# try/catch -> DecompilerException wrapping is
// deferred with the exception surface.
void CSharpDecompiler::DoDecompileModuleAndAssemblyAttributes(
    const DecompileRun& decompileRun, const TS::MetadataModule& module,
    Syntax::SyntaxTree& syntaxTree) {
    for (const TS::IAttribute* a : module.GetAssemblyAttributes()) {
        if (a == nullptr)
            continue;
        auto astBuilder = CreateAstBuilder(decompileRun.Settings());
        auto* attrSection = new Syntax::AttributeSection(
            astBuilder.ConvertAttribute(*a));
        attrSection->AttributeTarget("assembly");
        syntaxTree.Members().Add(attrSection);
    }
    for (const TS::IAttribute* a : module.GetModuleAttributes()) {
        if (a == nullptr)
            continue;
        auto astBuilder = CreateAstBuilder(decompileRun.Settings());
        auto* attrSection = new Syntax::AttributeSection(
            astBuilder.ConvertAttribute(*a));
        attrSection->AttributeTarget("module");
        syntaxTree.Members().Add(attrSection);
    }
}

// The C# `public string DecompileModuleAndAssemblyAttributesToString()`
// (CSharpDecompiler.cs line 838): `SyntaxTreeToString(
// DecompileModuleAndAssemblyAttributes())` -- the tree built by the AST
// path, rendered through the output visitor (the port's AstNode::ToString
// with the Mono defaults, the C# `settings.CSharpFormattingOptions`
// equivalent; the settings-carrying formatting options land with the
// instance surface).
std::string CSharpDecompiler::DecompileModuleAndAssemblyAttributesToString(
    const ::ILSpy::Decompiler::TypeSystem::MetadataModule& module) {
    std::unique_ptr<Syntax::SyntaxTree> syntaxTree(
        DecompileModuleAndAssemblyAttributes(module));
    return syntaxTree->ToString(nullptr);
}

// The C# `public string DecompileWholeModuleAsString()` (line 1220):
// `SyntaxTreeToString(DecompileWholeModuleAsSingleFile())` -- the
// module/assembly attribute sections, then every type in metadata order
// (the `<Module>` placeholder skipped, the C# DoDecompileTypes gate at
// line 874). The type bodies render through the flat type entry; the C#
// composes one SyntaxTree and runs the transforms over it, which lands
// with the statement-building back end.
std::string CSharpDecompiler::DecompileWholeModuleToString(
    const ::ILSpy::Decompiler::Metadata::MetadataFile& file) {
    // The static scaffold's per-call wiring (the note above).
    Metadata::UniversalAssemblyResolver resolver(
        file.FileName(), false, Metadata::DetectTargetFrameworkId(file));
    TS::DecompilerTypeSystem typeSystem(file, resolver);

    std::string out;
    // The leading attribute sections (the whole-module path's
    // DoDecompileModuleAndAssemblyAttributes call at line 917).
    out += DecompileModuleAndAssemblyAttributesToString(
        typeSystem.MainMetadataModule());
    // The types (the C# DoDecompileTypes loop in metadata order).
    for (const auto& t : file.TypeDefs()) {
        if (t.Name == "<Module>")
            continue;
        std::string text;
        if (DecompileTypeToString(file, t.Token, text))
            out += text;
    }
    return out;
}

std::vector<std::unique_ptr<Transforms::IAstTransform>>
CSharpDecompiler::GetAstTransforms() {
    // The C# `public static List<IAstTransform> GetAstTransforms()` list,
    // the ported entries at their C# positions (CSharpDecompiler.cs lines
    // 237-255); the unported transforms are loud comments at their slots so
    // the order is preserved as the ports land.
    std::vector<std::unique_ptr<Transforms::IAstTransform>> transforms;
    transforms.push_back(
        std::make_unique<Transforms::PatternStatementTransform>());
    transforms.push_back(
        std::make_unique<Transforms::ReplaceMethodCallsWithOperators>());
    //   -- the user-defined-operator core is ported (the op_ metadata-name
    //      tables and the binary/unary/explicit/op_True arms); the
    //      String.Concat reduction, the System.* special methods, and the
    //      methodof cast pattern stay deferred loudly in the .cpp.
    // IntroduceUnsafeModifier -- deferred.
    // AddCheckedBlocks -- deferred (the port carries the annotation half;
    // the block-rewriting IAstTransform itself lands with the rest of the
    // AST-transform layer, at this same slot).
    transforms.push_back(std::make_unique<Transforms::DeclareVariables>());
    // TransformFieldAndConstructorInitializers -- deferred.
    transforms.push_back(
        std::make_unique<Transforms::PrettifyAssignments>());
    // IntroduceUsingDeclarations / IntroduceExtensionMethods /
    // IntroduceQueryExpressions / CombineQueryExpressions -- deferred.
    transforms.push_back(
        std::make_unique<Transforms::NormalizeBlockStatements>());
    transforms.push_back(
        std::make_unique<Transforms::FlattenSwitchBlocks>());
    // RenameVisualBasicAnonymousTypes -- deferred.
    transforms.push_back(
        std::make_unique<Transforms::FixNameCollisions>());
    // AddXmlDocumentationTransform -- deferred.
    return transforms;
}

void CSharpDecompiler::RunAstTransforms(    Syntax::AstNode& rootNode, DecompileRun& decompileRun,
    const ::ILSpy::Decompiler::TypeSystem::ITypeResolveContext* decompilationContext) {
    // The C# RunTransforms shape: the context build, the up-front invariant
    // check, the transform loop with the per-entry step groups and invariant
    // checks, then the InsertParenthesesVisitor (the readability flag on)
    // and the GenericGrammarAmbiguityVisitor tail. The C# StepLimitReached /
    // CancellationToken bookkeeping is deferred with those surfaces.
    Transforms::TransformContext context;
    context.DecompileRun = &decompileRun;
    // The C# `var typeSystemAstBuilder = CreateAstBuilder(decompileRun.Settings)`
    // + the TransformContext ctor parameter: the type renderer the
    // insertion arms consume (ConvertType).
    Syntax::TypeSystemAstBuilder typeSystemAstBuilder =
        CreateAstBuilder(decompileRun.Settings());
    context.TypeSystemAstBuilder = &typeSystemAstBuilder;
    // The C# TransformContext ctor's third parameter (the IDecompilerTypeSystem
    // the ctor passes): the compilation the context's TypeSystem slot carries.
    // The C# passes the decompiler's type system -- an IDecompilerTypeSystem IS
    // an ITypeResolveContext -- so the port reads it off the decompilation
    // context parameter; a caller that passes none (the pipeline driver's
    // bare form) leaves the slot null and the arm that needs it degrades.
    context.TypeSystem = decompilationContext != nullptr
                             ? &decompilationContext->Compilation()
                             : nullptr;
    rootNode.CheckInvariant();
    for (const auto& transform : GetAstTransforms()) {
        context.StepOnce("AstTransform");
        transform->Run(rootNode, context);
        rootNode.CheckInvariant();
    }
    OutputVisitor::InsertParenthesesVisitor insertParentheses;
    insertParentheses.InsertParenthesesForReadability = true;
    rootNode.AcceptVisitor(insertParentheses);
    OutputVisitor::GenericGrammarAmbiguityVisitor::ResolveAmbiguities(
        &rootNode);
}

// The C# `static TypeSystemAstBuilder CreateAstBuilder(DecompilerSettings
// settings)` (line 722).
Syntax::TypeSystemAstBuilder CSharpDecompiler::CreateAstBuilder(
    const ::ILSpy::Decompiler::DecompilerSettings& settings) {
    Syntax::TypeSystemAstBuilder typeSystemAstBuilder;
    typeSystemAstBuilder.ShowAttributes() = true;
    typeSystemAstBuilder.UsePrivateProtectedAccessibility() =
        settings.IntroducePrivateProtectedAccessibility();
    typeSystemAstBuilder.SortAttributes() = settings.SortCustomAttributes();
    typeSystemAstBuilder.AlwaysUseShortTypeNames() = true;
    typeSystemAstBuilder.AddResolveResultAnnotations() = true;
    typeSystemAstBuilder.UseNullableSpecifierForValueTypes() = settings.LiftNullables();
    return typeSystemAstBuilder;
}

// The C# `internal static bool RemoveAttribute(EntityDeclaration entityDecl,
// KnownAttribute attributeType)` (line 2342): removes the sections' attributes
// whose type resolves to the known attribute type; empty sections are
// dropped.
bool CSharpDecompiler::RemoveAttribute(
    Syntax::EntityDeclaration& entityDecl,
    ::ILSpy::Decompiler::TypeSystem::KnownAttribute attributeType) {
    bool found = false;
    for (int i = 0; i < entityDecl.Attributes().Count(); i++) {
        Syntax::AttributeSection* section = entityDecl.Attributes().At(i);
        for (int j = 0; j < section->Attributes().Count(); j++) {
            Syntax::Attribute* attr = section->Attributes().At(j);
            const ::ILSpy::Decompiler::TypeSystem::ISymbol* symbol =
                GetSymbol(*attr->Type());
            auto* typeDefinition = dynamic_cast<
                const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition*>(symbol);
            if (typeDefinition != nullptr &&
                typeDefinition->FullTypeName() ==
                    ::ILSpy::Decompiler::TypeSystem::FullTypeName(
                        ::ILSpy::Decompiler::TypeSystem::GetTypeName(
                            attributeType))) {
                attr->Remove();
                found = true;
                j--;
            }
        }
        if (section->Attributes().Count() == 0) {
            section->Remove();
            i--;
        }
    }
    return found;
}

} // namespace ILSpy::Decompiler::CSharp
