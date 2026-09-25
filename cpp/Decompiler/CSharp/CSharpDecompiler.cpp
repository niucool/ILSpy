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
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxTree.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IEvent.hpp"
#include "Decompiler/TypeSystem/StringComparer.hpp"
#include "Decompiler/Util/CacheManager.hpp"
#include "Decompiler/DecompileRun.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/Transforms/ReplaceMethodCallsWithOperators.hpp"
#include "Decompiler/CSharp/Transforms/TransformFieldAndConstructorInitializers.hpp"
#include "Decompiler/CSharp/Transforms/IntroduceUsingDeclarations.hpp"
#include "Decompiler/CSharp/Transforms/IntroduceExtensionMethods.hpp"
#include "Decompiler/CSharp/Transforms/IntroduceQueryExpressions.hpp"
#include "Decompiler/CSharp/Transforms/CombineQueryExpressions.hpp"
#include "Decompiler/CSharp/Transforms/RenameVisualBasicAnonymousTypes.hpp"
#include "Decompiler/CSharp/Transforms/AddXmlDocumentationTransform.hpp"
#include "Decompiler/CSharp/Transforms/IntroduceUnsafeModifier.hpp"
#include "Decompiler/CSharp/Transforms/AddCheckedBlocks.hpp"
#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"
#include "Decompiler/CSharp/Transforms/DeclareVariables.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/UsingDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/PrettifyAssignments.hpp"
#include "Decompiler/CSharp/Transforms/NormalizeBlockStatements.hpp"
#include "Decompiler/CSharp/Transforms/FlattenSwitchBlocks.hpp"
#include "Decompiler/CSharp/Transforms/FixNameCollisions.hpp"
#include "Decompiler/CSharp/OutputVisitor/FormattingOptionsFactory.hpp"
#include "Decompiler/CSharp/OutputVisitor/InsertParenthesesVisitor.hpp"
#include "Decompiler/CSharp/OutputVisitor/GenericGrammarAmbiguityVisitor.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Metadata/PartialTypeInfo.hpp"
#include "Decompiler/Metadata/UniversalAssemblyResolver.hpp"
#include "Decompiler/Metadata/DotNetCorePathFinderExtensions.hpp"
#include "Decompiler/TypeSystem/DecompilerTypeSystem.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/Metadata/CodeMappingInfo.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/SimpleTypeResolveContext.hpp"
#include "Decompiler/DecompilerSettings.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/IL/Transforms/GetILTransforms.hpp"
#include "Decompiler/IL/ILReader.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <utility>

namespace ILSpy::Decompiler::CSharp {

// The IL pipeline driver: the IL layer's RunGetILTransforms (the fixed
// GetILTransforms + RunTransforms sequence, kept inline there so every
// consumer drives the same order).
void CSharpDecompiler::RunILTransforms(IL::ILFunction& function,
                                       IL::ILTransformContext& context) {
    IL::RunGetILTransforms(function, context);
}

// The context wiring the C# ILTransformContext carries natively: the
// PEFile (the port's Metadata pointer) and the CreateILReader deep-decode
// entry (the port's DelegateBodyResolver hook over ReadIL).
static void WireTransformContext(
    IL::ILTransformContext& context, const Metadata::MetadataFile& file,
    TS::DecompilerTypeSystem* typeSystem = nullptr) {
    context.Metadata = const_cast<Metadata::MetadataFile*>(&file);
    context.TypeSystem = typeSystem;
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
    IL::RunGetILTransforms(function, context);
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
    // The C# static Decompile over a PEFile constructs a CSharpDecompiler
    // -- the reference-loaded type system rides the same shape here: one
    // per call, kept alive through the render below.
    Metadata::UniversalAssemblyResolver resolver(
        file.FileName(), false, Metadata::DetectTargetFrameworkId(file));
    TS::DecompilerTypeSystem typeSystem(file, resolver);
    return DecompileMethodToString(file, &typeSystem, methodToken,
                                   methodRva, methodName, out, isConstructor);
}

bool CSharpDecompiler::DecompileMethodToString(
    const Metadata::MetadataFile& file,
    TS::DecompilerTypeSystem* typeSystem, std::uint32_t methodToken,
    std::uint32_t methodRva, const std::string& methodName,
    std::string& out, bool isConstructor, bool* asyncDecompiled,
    bool* iteratorDecompiled) {
    auto fn = IL::ReadIL(file, methodToken, methodRva);
    if (!fn) return false;
    // The C# ILReader decodes the body through the method definition (the
    // ILFunction's Method carries the resolved IMethod; the state-machine
    // transforms read its return type). The port's ReadIL leaves the
    // slot null; the facade resolves through the wired type system and
    // keeps the shared_ptr alive past the render (the raw Method points
    // into it).
    std::shared_ptr<TS::IMethod> resolvedMethod;
    if (typeSystem != nullptr) {
        const auto* module = dynamic_cast<const TS::MetadataModule*>(
            &typeSystem->MainModule());
        if (module != nullptr) {
            resolvedMethod = TS::AliasMethod(
                module->GetDefinitionMethod(methodToken));
            fn->Method = resolvedMethod.get();
        }
    }
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
    // carries the PEFile for the closure transforms' deep-decode); the
    // type system carries the shared one when threaded (the state-machine
    // and callsite transforms resolve through it).
    IL::ILTransformContext transformContext;
    WireTransformContext(transformContext, file, typeSystem);
    RunILTransforms(*fn, transformContext);
    fn->CheckInvariant(IL::ILPhase::Normal);
    if (asyncDecompiled != nullptr)
        *asyncDecompiled = fn->IsAsync();
    if (iteratorDecompiled != nullptr)
        *iteratorDecompiled = fn->IsIterator;
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


static OutputVisitor::CSharpFormattingOptions SettingsFormattingOptions() {
    OutputVisitor::CSharpFormattingOptions options =
        OutputVisitor::FormattingOptionsFactory::CreateAllman();
    options.IndentSwitchBody = false;
    options.ArrayInitializerWrapping = OutputVisitor::Wrapping::WrapIfTooLong;
    options.AutoPropertyFormatting =
        OutputVisitor::PropertyFormatting::SingleLine;
    return options;
}

namespace {

namespace TS = ::ILSpy::Decompiler::TypeSystem;
namespace SyntaxNS = ::ILSpy::Decompiler::CSharp::Syntax;

// The C# compilation-root namespace walk (`ICompilation.GetNamespaceByFullName`):
// each dotted segment through `GetChildNamespace`, null when any segment is
// absent. The render's using-set resolution consults the COMPILATION's root
// (the merged reference tree -- the module's own root namespace carries only
// the module's own types, so referenced namespaces never resolve through
// it).
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

// The C# member-modifier composition for the type-level body's flat
// renderer: the TypeSystemAstBuilder's GetMemberModifiers (the
// accessibility under NeedsAccessibility -- explicit interface
// implementations, static constructors, and interface members suppress
// it -- plus static and the virtual family) with ConvertField's
// const/readonly/volatile bits, rendered as the declaration's leading
// keywords in the AllModifiers output order.
std::string MemberModifiersText(const TS::IMember* member) {
    if (member == nullptr)
        return std::string();
    SyntaxNS::TypeSystemAstBuilder builder;
    SyntaxNS::Modifiers m = builder.GetMemberModifiers(*member);
    if (const auto* field = dynamic_cast<const TS::IField*>(member)) {
        if (field->IsConst()) {
            m = (m & ~SyntaxNS::Modifiers::Static) |
                SyntaxNS::Modifiers::Const;
        } else if (field->IsReadOnly()) {
            m = m | SyntaxNS::Modifiers::Readonly;
        } else if (field->IsVolatile()) {
            m = m | SyntaxNS::Modifiers::Volatile;
        }
    }
    std::string out;
    for (SyntaxNS::Modifiers modifier : SyntaxNS::CSharpModifiers::AllModifiers) {
        if (modifier == SyntaxNS::Modifiers::Any)
            continue;
        if ((m & modifier) == modifier)
            out += SyntaxNS::CSharpModifiers::GetModifierName(modifier) +
                   std::string(" ");
    }
    return out;
}

// The C# literal syntax of a constant field's value (the ConvertField
// ShowConstantValues arm over GetConstantValue; the floating-point forms
// ride the ConvertFloatingPointLiteral fraction logic with the
// statement-building back end -- the integer, boolean, char, string, and
// null arms cover the metadata constants). The empty string means "no
// literal" (a null std::any over a value type renders the default-value
// form; a null over a reference type renders null).
std::string ConstantFieldLiteral(const TS::IField& field) {
    std::any value;
    try {
        value = field.GetConstantValue();
    } catch (const std::exception&) {
        return std::string();
    }
    if (value.has_value()) {
        if (auto* b = std::any_cast<bool>(&value))
            return *b ? "true" : "false";
        if (auto* i8 = std::any_cast<std::int8_t>(&value))
            return std::to_string(static_cast<int>(*i8));
        if (auto* u8 = std::any_cast<std::uint8_t>(&value))
            return std::to_string(static_cast<unsigned>(*u8));
        if (auto* i16 = std::any_cast<std::int16_t>(&value))
            return std::to_string(static_cast<int>(*i16));
        if (auto* u16 = std::any_cast<std::uint16_t>(&value))
            return std::to_string(static_cast<unsigned>(*u16));
        if (auto* i32 = std::any_cast<std::int32_t>(&value))
            return std::to_string(*i32);
        if (auto* u32 = std::any_cast<std::uint32_t>(&value))
            return std::to_string(*u32) + "u";
        if (auto* i64 = std::any_cast<std::int64_t>(&value))
            return std::to_string(*i64) + "L";
        if (auto* u64 = std::any_cast<std::uint64_t>(&value))
            return std::to_string(*u64) + "uL";
        if (auto* str = std::any_cast<std::string>(&value))
            return "\"" + *str + "\"";
        if (auto* ch = std::any_cast<char16_t>(&value))
            return std::string("'") + static_cast<char>(*ch) + "'";
    } else if (field.Type().IsReferenceType()) {
        return "null";
    }
    return std::string();
}

// The C# MemberIsHidden's state machine arms (the whole-type skip): the
// compiler-generated state machine types the de-sugar replaces do not
// render when their transforms are enabled (the C# gates consult
// DecompilerSettings; the pipeline context's defaults carry the same
// on-by-default gates).
bool TypeIsHiddenFromRender(const Metadata::MetadataFile& file,
                            std::uint32_t typeToken) {
    const IL::ILTransformSettings transformSettings;
    return (transformSettings.YieldReturn &&
            Metadata::IsCompilerGeneratorEnumerator(file, typeToken)) ||
           (transformSettings.AsyncAwait &&
            Metadata::IsCompilerGeneratedStateMachine(file, typeToken));
}

// The enclosing namespace of a (possibly nested) type: the declaring
// chain's root namespace (a nested type's own namespace column is empty
// by the ECMA requirement, and the C# entity model surfaces the
// enclosing namespace).
std::string RootNamespaceOf(const Metadata::MetadataFile& file,
                            std::uint32_t typeToken) {
    std::uint32_t current = typeToken;
    while (true) {
        auto info = file.GetTypeDefNameInfo(current);
        if (!info.has_value())
            return std::string();
        if (info->DeclaringTypeToken == 0)
            return info->Namespace;
        current = info->DeclaringTypeToken;
    }
}

// The -t render's using set: the type entity's required namespace set (the
// unseeded, implicit-base-skipping walk -- the C# resolver's
// IntroduceUsingDeclarations filtering approximated by the walk's own
// references), sorted, the type's own namespace excluded. The base-list
// qualification consumes the same set (the C# CreateDecompileRun threads
// the collected namespaces into BOTH the emitted using directives and the
// resolver's using scope).
std::vector<std::string> MinimalUsingSetOf(const Metadata::MetadataFile& file,
                                           const TS::MetadataModule& module,
                                           std::uint32_t typeToken) {
    std::vector<std::string> sorted;
    const TS::ITypeDefinition* typeDef = module.GetDefinition(typeToken);
    if (typeDef == nullptr)
        return sorted;
    std::unordered_set<std::string> namespaces;
    CollectRequiredNamespaces(
        *typeDef, const_cast<TS::MetadataModule&>(module), namespaces);
    const std::string own = RootNamespaceOf(file, typeToken);
    for (const std::string& ns : namespaces) {
        if (!ns.empty() && ns != own)
            sorted.push_back(ns);
    }
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

// The -t render's using directives: the type entity's required namespace
// set rendered as the leading `using X;` lines (the same set the
// base-list qualification consults).
std::string UsingDirectivesText(const Metadata::MetadataFile& file,
                                const TS::MetadataModule& module,
                                std::uint32_t typeToken) {
    std::vector<std::string> sorted =
        MinimalUsingSetOf(file, module, typeToken);
    std::string out;
    for (const std::string& ns : sorted)
        out += "using " + ns + ";\n";
    if (!out.empty())
        out += "\n";
    return out;
}

// The C# CreateDecompileRun's UsingScope plus the
// FullyQualifyAmbiguousTypeNamesVisitor's resolver construction (the
// visitor's ctor): the render's using set resolved through the
// compilation's root namespace, the scope nested along the current type's
// namespace chain (a name inside `namespace N { }` walks the chain BEFORE
// the using declarations -- the render's own-namespace using exclusion
// relies on exactly this lookup order), and the resolver carrying the
// current type definition (the LookInCurrentType arm reads the type's own
// members). Null when the type definition is absent (no scope to consult
// -- the short names stay).
std::shared_ptr<Resolver::CSharpResolver> RenderScopeResolver(
    const TS::MetadataModule& module,
    const TS::ITypeDefinition* currentType,
    const std::vector<std::string>& usingNamespaces) {
    if (currentType == nullptr)
        return nullptr;
    const TS::ICompilation& compilation = module.Compilation();
    std::vector<const TS::INamespace*> resolvedNamespaces;
    for (const std::string& ns : usingNamespaces) {
        if (ns.empty())
            continue;
        const TS::INamespace* resolved =
            ResolveNamespaceFullName(compilation.RootNamespace(), ns);
        if (resolved != nullptr)
            resolvedNamespaces.push_back(resolved);
    }
    auto usingScope = std::make_shared<TypeSystem::UsingScope>(
        std::make_shared<TypeSystem::CSharpTypeResolveContext>(module),
        compilation.RootNamespace(), resolvedNamespaces);
    const std::string ownNamespace = currentType->Namespace();
    std::size_t start = 0;
    while (start < ownNamespace.size()) {
        std::size_t dot = ownNamespace.find('.', start);
        usingScope = usingScope->WithNestedNamespace(
            ownNamespace.substr(
                start, dot == std::string::npos ? std::string::npos
                                                : dot - start));
        if (dot == std::string::npos)
            break;
        start = dot + 1;
    }
    return std::make_shared<Resolver::CSharpResolver>(
               std::make_shared<TypeSystem::CSharpTypeResolveContext>(module))
        ->WithCurrentUsingScope(std::move(usingScope))
        ->WithCurrentTypeDefinition(currentType);
}

// The C# TypeSystemAstBuilder's ConvertTypeHelper decision for one
// base-list name: the short name (the lookup resolves it), the qualified
// form (an ambiguity -- two accessible same-name types in the scope's
// imported namespaces, e.g. the net48 mscorlib's legacy duplicate
// System.Runtime.InteropServices.ComTypes.IEnumerable against
// System.Collections.IEnumerable, the duplicate internal so only the
// assemblies in mscorlib's InternalsVisibleTo friend list see it -- or a
// shadowing by a same-name type in an earlier lookup position), or the
// not-found case (the name resolves to nothing in the scope).
enum class BaseNameDecision { Short, NotFound, Qualify };

BaseNameDecision DecideBaseName(const TS::ITypeDefinition* typeDef,
                                 const TS::ITypePtr& instantiation,
                                 const Resolver::CSharpResolver& resolver) {
    if (typeDef == nullptr)
        // No definition to consult: the C# outer short-name path.
        return BaseNameDecision::Short;
    // The C# localTypeArguments: the type's own parameter slots sliced off
    // the instantiation (the declaring chain's outer parameters excluded);
    // the lookup's arity consults the count.
    std::size_t outerTypeParameterCount = 0;
    for (const TS::ITypeDefinition* d = typeDef->DeclaringTypeDefinition();
         d != nullptr; d = d->DeclaringTypeDefinition())
        outerTypeParameterCount +=
            static_cast<std::size_t>(d->TypeParameterCount());
    std::vector<TS::ITypePtr> localTypeArguments;
    if (static_cast<std::size_t>(typeDef->TypeParameterCount()) >
        outerTypeParameterCount) {
        const auto* parameterized =
            dynamic_cast<const TS::ParameterizedType*>(instantiation.get());
        if (parameterized != nullptr) {
            const std::vector<TS::ITypePtr>& typeArguments =
                parameterized->TypeArguments();
            if (typeArguments.size() > outerTypeParameterCount)
                localTypeArguments.assign(
                    typeArguments.begin() + outerTypeParameterCount,
                    typeArguments.begin() + typeDef->TypeParameterCount());
        }
    }
    auto rr = resolver.LookupSimpleNameOrTypeName(
        typeDef->Name(), localTypeArguments, Resolver::NameLookupMode::Type);
    const auto trr =
        std::dynamic_pointer_cast<Semantics::TypeResolveResult>(rr);
    if (trr == nullptr)
        return BaseNameDecision::NotFound;
    if (trr->IsError())
        return BaseNameDecision::Qualify;
    // The TypeMatches bounded form: the same definition (the lookup
    // parameterizes with the base type's own arguments, so the resolved
    // instantiation matches by construction when the definition does).
    return trr->Type().GetDefinition() == typeDef
               ? BaseNameDecision::Short
               : BaseNameDecision::Qualify;
}

// The C# UseKeywordsForBuiltinTypes over a type definition: the primitive
// definitions render their keyword spelling (uint, string, ...) in every
// name position. Null when the definition is not a keyword type.
const char* BuiltinTypeKeyword(const TS::ITypeDefinition* typeDef) {
    if (typeDef == nullptr)
        return nullptr;
    static const std::map<std::string, const char*> kBuiltinKeywords = {
        {"System.Boolean", "bool"},   {"System.Char", "char"},
        {"System.SByte", "sbyte"},   {"System.Byte", "byte"},
        {"System.Int16", "short"},   {"System.UInt16", "ushort"},
        {"System.Int32", "int"},     {"System.UInt32", "uint"},
        {"System.Int64", "long"},    {"System.UInt64", "ulong"},
        {"System.Single", "float"},  {"System.Double", "double"},
        {"System.Decimal", "decimal"}, {"System.String", "string"},
        {"System.Object", "object"}, {"System.IntPtr", "nint"},
        {"System.UIntPtr", "nuint"},
    };
    const std::string full = typeDef->Namespace() + "." + typeDef->Name();
    auto it = kBuiltinKeywords.find(full);
    return it != kBuiltinKeywords.end() ? it->second : nullptr;
}

// The C# ConvertTypeHelper's name composition for a base-list entry: the
// own name when the scope lookup resolves it (a sibling nested type
// resolves by its own name -- the enclosing type's members are in the
// lookup scope); the declaring type rendered through the SAME decision
// joined by '.' when the own name fails on a nested type; the
// namespace-qualified form for a top-level type the scope cannot name.
// The type arguments render as the <...> tail (each argument through the
// short-name renderer -- the arguments' own decision rides with the
// member-signature work). An empty instantiation is the plain definition.
std::string RenderBaseTypeName(const TS::ITypeDefinition* typeDef,
                               const TS::ITypePtr& instantiation,
                               const Resolver::CSharpResolver* resolver) {
    if (typeDef == nullptr && instantiation != nullptr) {
        // A type-parameter argument renders its declared name (the C#
        // MakeSimpleType over the parameter's Name); other non-definition
        // types (arrays, pointers) keep the short renderer.
        if (const auto* typeParameter =
                dynamic_cast<const TS::ITypeParameter*>(
                    instantiation.get()))
            return typeParameter->Name();
        return IL::CSharpTypeName(instantiation);
    }
    if (typeDef == nullptr)
        return std::string();
    // The keyword spelling precedes every name decision (the C#
    // UseKeywordsForBuiltinTypes).
    if (const char* keyword = BuiltinTypeKeyword(typeDef))
        return keyword;
    std::size_t outerTypeParameterCount = 0;
    for (const TS::ITypeDefinition* d = typeDef->DeclaringTypeDefinition();
         d != nullptr; d = d->DeclaringTypeDefinition())
        outerTypeParameterCount +=
            static_cast<std::size_t>(d->TypeParameterCount());
    const auto* parameterized =
        dynamic_cast<const TS::ParameterizedType*>(instantiation.get());
    std::string args;
    if (parameterized != nullptr &&
        parameterized->TypeArguments().size() > outerTypeParameterCount) {
        args = "<";
        for (std::size_t i = outerTypeParameterCount;
             i < parameterized->TypeArguments().size(); ++i) {
            if (i != outerTypeParameterCount)
                args += ", ";
            // Each argument through the same name decision (the C#
            // AddTypeArguments converts every argument through
            // ConvertType).
            const TS::ITypePtr& argument = parameterized->TypeArguments()[i];
            args += RenderBaseTypeName(argument->GetDefinition(), argument,
                                        resolver);
        }
        args += ">";
    }
    if (resolver == nullptr)
        return typeDef->Name() + args;
    BaseNameDecision decision =
        DecideBaseName(typeDef, instantiation, *resolver);
    if (decision == BaseNameDecision::Short)
        return typeDef->Name() + args;
    if (decision == BaseNameDecision::NotFound &&
        typeDef->DeclaringTypeDefinition() == nullptr)
        // The not-found TOP-LEVEL tolerance: the port's compilation loads
        // a SUBSET of the C#'s reference modules (the netcore runtime-pack
        // discovery is not ported), so a name absent from the port's
        // merged namespace tree is typically resolvable in the C#'s, where
        // it renders short. A NESTED name the scope cannot resolve is real
        // (nested types are not namespace members -- only the enclosing
        // type's scope names them) and falls through to the dotted form.
        return typeDef->Name() + args;
    if (typeDef->DeclaringTypeDefinition() != nullptr) {
        // The C# MemberType form: the target is the declaring type
        // through the same decision; the parameterized form's generic type
        // carries the declaring instantiation when the metadata provides
        // one (a plain definition renders argument-less -- the nested
        // generic-instantiation chain lands with the member-signature
        // work).
        TS::ITypePtr declaringInstantiation;
        if (parameterized != nullptr)
            declaringInstantiation = parameterized->GenericType();
        return RenderBaseTypeName(typeDef->DeclaringTypeDefinition(),
                                  declaringInstantiation, resolver) +
               "." + typeDef->Name() + args;
    }
    const std::string ns = typeDef->Namespace();
    return ns.empty() ? typeDef->Name() + args
                      : ns + "." + typeDef->Name() + args;
}

// The C# TypeDefinitionNameableInBaseList (the f41b12c01 fix for #3230):
// whether a type's base list can NAME the type. A type may name its own
// nested types (and those of its enclosing types) regardless of
// accessibility; everything else resolves through the accessibility rules
// (the MemberLookup.IsAccessible shape for type definitions: private
// nested only through the exemption; internal within the same module;
// protected granted through the declaring chain's inheritance (the
// IsDerivedFrom walk the C# grants type definitions regardless of
// allowProtectedAccess); public always). Naming `A.I` also requires `A`
// nameable (the recursion over the declaring type).
bool TypeDefinitionNameableInBaseList(const TS::ITypeDefinition* td,
                                      const TS::ITypeDefinition& currentType) {
    if (td == nullptr)
        return true;
    const TS::ITypeDefinition* tdDeclaring = td->DeclaringTypeDefinition();
    // The nested exemption over the current type's declaring chain.
    for (const TS::ITypeDefinition* t = &currentType; t != nullptr;
         t = t->DeclaringTypeDefinition()) {
        if (tdDeclaring != nullptr && tdDeclaring == t)
            return true;
    }
    // The C# IsInternalAccessible's `module.InternalsVisibleTo(currentModule)`
    // -- the same-assembly grant plus the [InternalsVisibleTo] friend list
    // (the WPF assemblies share their internals across the family, so an
    // internal interface of WindowsBase stays nameable from
    // PresentationFramework).
    bool internalAccess =
        td->ParentModule() != nullptr &&
        currentType.ParentModule() != nullptr &&
        td->ParentModule()->InternalsVisibleTo(*currentType.ParentModule());
    auto protectedAccess = [&]() {
        // The C# IsProtectedAccessible's grant for type definitions: some
        // type in the current type's declaring chain derives from the
        // named type's declaring type.
        for (const TS::ITypeDefinition* t = &currentType; t != nullptr;
             t = t->DeclaringTypeDefinition()) {
            if (tdDeclaring != nullptr && TS::IsDerivedFrom(*t, tdDeclaring))
                return true;
        }
        return false;
    };
    switch (td->Accessibility()) {
        case TS::Accessibility::Private:
            return false;
        case TS::Accessibility::Internal:
            return internalAccess;
        case TS::Accessibility::Protected:
            return protectedAccess();
        case TS::Accessibility::ProtectedOrInternal:
            return internalAccess || protectedAccess();
        case TS::Accessibility::ProtectedAndInternal:
            return internalAccess && protectedAccess();
        default:
            return true;
    }
}

// The C# ConvertAccessor's accessibility modifier: rendered only when the
// accessor's accessibility differs from the property's.
std::string AccessorVisibilityText(const TS::IMethod* accessor,
                                   const TS::IProperty* property) {
    if (accessor == nullptr || property == nullptr)
        return std::string();
    if (accessor->Accessibility() == property->Accessibility())
        return std::string();
    SyntaxNS::Modifiers m = SyntaxNS::ModifierFromAccessibility(
        accessor->Accessibility(), /*usePrivateProtected=*/true);
    std::string out;
    for (SyntaxNS::Modifiers modifier :
         SyntaxNS::CSharpModifiers::AllModifiers) {
        if (modifier == SyntaxNS::Modifiers::Any)
            continue;
        if ((m & modifier) == modifier)
            out += SyntaxNS::CSharpModifiers::GetModifierName(modifier) +
                   std::string(" ");
    }
    return out;
}

// The accessor's body statements from the flat method render (the text
// between the header's opening brace and the closing brace); empty when the
// accessor has no decodable body.
std::string AccessorBodyText(const Metadata::MetadataFile& file,
                             TS::DecompilerTypeSystem* typeSystem,
                             std::uint32_t accessorToken,
                             const char* accessorName) {
    std::uint32_t rva = file.GetMethodRVA(accessorToken);
    if (rva == 0)
        return std::string();
    std::string text;
    if (!CSharpDecompiler::DecompileMethodToString(
            file, typeSystem, accessorToken, rva, accessorName, text))
        return std::string();
    std::size_t open = text.find("{\n");
    std::size_t close = text.rfind("\n}");
    if (open == std::string::npos || close == std::string::npos ||
        close <= open)
        return std::string();
    std::string body = text.substr(open + 2, close - (open + 2));
    if (!body.empty() && body.back() != '\n')
        body += '\n';
    return body;
}

// The C# ConvertAttributes (the TypeSystemAstBuilder's member form): one
// AttributeSection per attribute, rendered as the declaration's leading
// `[...]` lines through the output visitor with the settings' formatting
// options. The empty string when the entity carries no attributes.
std::string MemberAttributesText(const TS::IEntity* entity,
                                 bool asyncDecompiled = false,
                                 bool iteratorDecompiled = false) {
    if (entity == nullptr)
        return std::string();
    std::vector<const TS::IAttribute*> attributes = entity->GetAttributes();
    if (attributes.empty())
        return std::string();
    OutputVisitor::CSharpFormattingOptions options =
        SettingsFormattingOptions();
    // The CreateAstBuilder configuration (the short attribute names the
    // C# facade renders; the resolver-less builder skips the
    // disambiguation lookups, matching the attribute path's builder).
    SyntaxNS::TypeSystemAstBuilder builder;
    builder.ShowAttributes() = true;
    builder.AlwaysUseShortTypeNames() = true;
    std::string out;
    for (const TS::IAttribute* a : attributes) {
        if (a == nullptr)
            continue;
        // The C# CleanUpMethodDeclaration's removals: the state machine
        // attributes drop when the async/iterator de-sugar succeeded
        // (the attribute names the compiler-generated type the de-sugar
        // replaced; a method that did not de-sugar keeps it).
        std::string attributeName = a->AttributeType().ReflectionName();
        if (asyncDecompiled &&
            attributeName ==
                "System.Runtime.CompilerServices.AsyncStateMachineAttribute")
            continue;
        if (iteratorDecompiled &&
            (attributeName ==
                 "System.Runtime.CompilerServices.IteratorStateMachineAttribute" ||
             attributeName ==
                 "System.Runtime.CompilerServices.AsyncIteratorStateMachineAttribute"))
            continue;
        Syntax::AttributeSection section(builder.ConvertAttribute(*a));
        std::string text = section.ToString(&options);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        if (!text.empty())
            out += text + "\n";
    }
    return out;
}

} // namespace

// The render body the static and instance DecompileTypeToString entries
// share: everything but the module wiring (the static entry builds it per
// call; the instance uses its own) and the registry lookup (the static
// consults the process-global placeholder; the instance its own map).
bool DecompileTypeToStringBody(
    const Metadata::MetadataFile& file, TS::DecompilerTypeSystem* typeSystem,
    TS::MetadataModule& module,
    const std::function<const Metadata::PartialTypeInfo*(std::uint32_t)>&
        partialLookup,
    const std::vector<std::string>* usingNamespaces,
    std::uint32_t typeToken, std::string& out) {
    const Metadata::PartialTypeInfo* partialType = partialLookup(typeToken);
    if (TypeIsHiddenFromRender(file, typeToken))
        return false;
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
    // `TypeDeclaration` emission): the modifiers through
    // ConvertTypeDefinition's composition (the accessibility, the
    // static/abstract/sealed else-if chain, the kind adjustments -- a
    // struct/enum drops sealed, an interface drops abstract, a readonly
    // struct gains readonly), the partial modifier only when the type has a
    // registered partial half (the C# `partialTypeInfo != null` arm; the
    // port's earlier unconditional partial was a stand-in), the
    // `Kind`-specific keyword (struct/interface), and the base-type list
    // (the C# ShowBaseTypes iteration: the entity's direct base types with
    // System.Object elided, the struct's System.ValueType elided, and the
    // enum's System.Enum replaced by the underlying type when not int;
    // the C#'s interface-name-accessibility filter (BaseTypeAccessibleFrom)
    // is deferred with the resolver surface).
    for (const auto& t : file.TypeDefs()) {
        if (t.Token != typeToken) continue;
        // The bare name (the metadata name's arity suffix stripped): the
        // declaration renders the type-parameter list separately (the C#
        // TypeDeclaration's TypeParameters), and the constructor headers
        // name the type without either.
        std::string bareName = t.Name;
        auto tick = bareName.find('`');
        if (tick != std::string::npos)
            bareName = bareName.substr(0, tick);
        typeName = bareName;
        const char* keyword = "class";
        switch (t.Kind) {
            case ::ILSpy::Decompiler::TypeSystem::TypeKind::Struct:
                keyword = "struct";
                break;
            case ::ILSpy::Decompiler::TypeSystem::TypeKind::Enum:
                // The C# ClassType.Enum arm.
                keyword = "enum";
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
        SyntaxNS::Modifiers typeModifiers = SyntaxNS::Modifiers::None;
        const TS::ITypeDefinition* typeDef = module.GetDefinition(t.Token);
        if (typeDef != nullptr) {
            typeModifiers = SyntaxNS::ModifierFromAccessibility(
                typeDef->Accessibility(), /*usePrivateProtected=*/true);
            if (typeDef->IsStatic())
                typeModifiers = typeModifiers |
                                SyntaxNS::Modifiers::Static;
            else if (typeDef->IsAbstract())
                typeModifiers = typeModifiers |
                                SyntaxNS::Modifiers::Abstract;
            else if (typeDef->IsSealed())
                typeModifiers = typeModifiers | SyntaxNS::Modifiers::Sealed;
            if (t.Kind == TS::TypeKind::Struct ||
                t.Kind == TS::TypeKind::Enum)
                typeModifiers = typeModifiers &
                                ~SyntaxNS::Modifiers::Sealed;
            if (t.Kind == TS::TypeKind::Interface)
                typeModifiers = typeModifiers &
                                ~SyntaxNS::Modifiers::Abstract;
            if (t.Kind == TS::TypeKind::Struct && typeDef->IsReadOnly())
                typeModifiers = typeModifiers |
                                SyntaxNS::Modifiers::Readonly;
        }
        if (partialType != nullptr)
            typeModifiers = typeModifiers | SyntaxNS::Modifiers::Partial;
        out += MemberAttributesText(typeDef);
        for (SyntaxNS::Modifiers modifier :
             SyntaxNS::CSharpModifiers::AllModifiers) {
            if (modifier == SyntaxNS::Modifiers::Any)
                continue;
            if ((typeModifiers & modifier) == modifier)
                out += SyntaxNS::CSharpModifiers::GetModifierName(modifier) +
                       std::string(" ");
        }
        out += keyword;
        out += ' ';
        out += bareName;
        // The type-parameter list (the C# TypeDeclaration's
        // TypeParameters): the declared names in declaration order.
        if (typeDef != nullptr && typeDef->TypeParameterCount() > 0) {
            out += '<';
            const std::vector<const TS::ITypeParameter*>& typeParameters =
                typeDef->TypeParameters();
            for (std::size_t i = 0; i < typeParameters.size(); ++i) {
                if (i != 0)
                    out += ", ";
                out += typeParameters[i] != nullptr
                           ? typeParameters[i]->Name()
                           : std::string("?");
            }
            out += '>';
        }
        if (typeDef != nullptr) {
            // The C# FullyQualifyAmbiguousTypeNamesVisitor's per-type
            // resolver (the visitor's ctor threading): the render's using
            // scope for the short-name decision. Null (no set passed)
            // leaves every short name alone.
            std::shared_ptr<Resolver::CSharpResolver> scopeResolver =
                usingNamespaces != nullptr
                    ? RenderScopeResolver(module, typeDef, *usingNamespaces)
                    : nullptr;
            std::vector<std::string> baseTypeNames;
            for (const TS::ITypePtr& baseType :
                 typeDef->DirectBaseTypes()) {
                if (baseType == nullptr)
                    continue;
                // The C# BaseTypeAccessibleFrom: an interface the base list
                // cannot name drops (the transitive interface-impl
                // propagation can name shapes valid C# cannot). The
                // generic type-argument walk (the BaseListNameabilityVisitor
                // over the whole type) rides with the resolver surface; the
                // direct definition check covers the corpus shapes.
                if (baseType->Kind() == TS::TypeKind::Interface &&
                    !TypeDefinitionNameableInBaseList(baseType->GetDefinition(),
                                                      *typeDef)) {
                    continue;
                }
                if (t.Kind == TS::TypeKind::Enum &&
                    TS::IsKnownType(*baseType, TS::KnownTypeCode::Enum)) {
                    // the enum's underlying type replaces System.Enum
                    // (rendered only when not int)
                    auto underlying = typeDef->EnumUnderlyingType();
                    if (underlying != nullptr &&
                        !TS::IsKnownType(*underlying,
                                         TS::KnownTypeCode::Int32))
                        baseTypeNames.push_back(
                            IL::CSharpTypeName(underlying));
                    continue;
                }
                if ((t.Kind == TS::TypeKind::Struct) &&
                    TS::IsKnownType(*baseType,
                                    TS::KnownTypeCode::ValueType)) {
                    continue;
                }
                if (TS::IsKnownType(*baseType, TS::KnownTypeCode::Object)) {
                    continue;
                }
                // The C# ConvertTypeHelper's name decision: the short
                // name (a sibling nested type resolves by its own name),
                // the declaring-dotted form (a nested type whose own name
                // the scope cannot resolve), or the namespace-qualified
                // form (an ambiguous or shadowed name).
                baseTypeNames.push_back(
                    scopeResolver != nullptr
                        ? RenderBaseTypeName(
                              baseType->GetDefinition(), baseType,
                              scopeResolver.get())
                        : IL::CSharpTypeName(baseType));
            }
            if (!baseTypeNames.empty()) {
                out += " : ";
                for (std::size_t i = 0; i < baseTypeNames.size(); ++i) {
                    if (i != 0) out += ", ";
                    out += baseTypeNames[i];
                }
            }
            // The type-parameter constraint clauses (the C#
            // ConvertTypeParameterConstraint): a parameter carrying a
            // special constraint (`class`/`struct`/`new()`), a type
            // constraint beyond Object/ValueType, or a nullability
            // constraint renders its `where` clause on the declaration
            // line, the constraint types through the same name decision
            // as the base list. The declaring type's own (outer)
            // parameters do not restate their constraints.
            {
                const std::vector<const TS::ITypeParameter*>& typeParameters =
                    typeDef->TypeParameters();
                std::size_t outerTypeParameterCount = 0;
                for (const TS::ITypeDefinition* d =
                         typeDef->DeclaringTypeDefinition();
                     d != nullptr; d = d->DeclaringTypeDefinition())
                    outerTypeParameterCount +=
                        static_cast<std::size_t>(d->TypeParameterCount());
                for (std::size_t i = outerTypeParameterCount;
                     i < typeParameters.size(); ++i) {
                    const TS::ITypeParameter* tp = typeParameters[i];
                    if (tp == nullptr)
                        continue;
                    // The C# skip clause: no special constraint, no
                    // nullability constraint, and every type constraint
                    // Object/ValueType.
                    bool hasTypeConstraint = false;
                    for (const TS::TypeConstraint& tc : tp->TypeConstraints()) {
                        if (tc.Type() == nullptr)
                            continue;
                        const TS::ITypeDefinition* tcDef =
                            tc.Type()->GetDefinition();
                        const bool objectOrValueType =
                            tcDef != nullptr &&
                            (TS::IsKnownType(*tc.Type(),
                                             TS::KnownTypeCode::Object) ||
                             TS::IsKnownType(*tc.Type(),
                                             TS::KnownTypeCode::ValueType));
                        if (!objectOrValueType || !tc.Attributes().empty()) {
                            hasTypeConstraint = true;
                            break;
                        }
                    }
                    if (!tp->HasDefaultConstructorConstraint() &&
                        !tp->HasReferenceTypeConstraint() &&
                        !tp->HasValueTypeConstraint() &&
                        !tp->AllowsRefLikeType() &&
                        tp->NullabilityConstraint() !=
                            TS::Nullability::NotNullable &&
                        !hasTypeConstraint)
                        continue;
                    out += " where ";
                    out += tp->Name();
                    out += " :";
                    bool first = true;
                    auto appendConstraint = [&](const std::string& item) {
                        out += first ? " " : ", ";
                        out += item;
                        first = false;
                    };
                    if (tp->HasReferenceTypeConstraint()) {
                        appendConstraint(
                            tp->NullabilityConstraint() ==
                                    TS::Nullability::Nullable
                                ? "class?"
                                : "class");
                    } else if (tp->HasValueTypeConstraint()) {
                        appendConstraint(
                            tp->HasUnmanagedConstraint() ? "unmanaged"
                                                         : "struct");
                    } else if (tp->NullabilityConstraint() ==
                               TS::Nullability::NotNullable) {
                        appendConstraint("notnull");
                    }
                    for (const TS::TypeConstraint& tc : tp->TypeConstraints()) {
                        if (tc.Type() == nullptr)
                            continue;
                        const bool objectOrValueType =
                            TS::IsKnownType(*tc.Type(),
                                            TS::KnownTypeCode::Object) ||
                            TS::IsKnownType(*tc.Type(),
                                            TS::KnownTypeCode::ValueType);
                        if (objectOrValueType && tc.Attributes().empty())
                            continue;
                        appendConstraint(
                            RenderBaseTypeName(tc.Type()->GetDefinition(),
                                               tc.Type(), scopeResolver.get()));
                    }
                    if (tp->HasDefaultConstructorConstraint() &&
                        !tp->HasValueTypeConstraint())
                        appendConstraint("new()");
                    if (tp->AllowsRefLikeType())
                        appendConstraint("allows ref struct");
                }
            }
        }
        out += "\n{\n";
        rendered = true;
        break;
    }
    // The nested types (the C# DoDecompile's member order: the NestedTypes
    // concat LEADS the member list, so the nested declarations render
    // inside the declaring type's braces). The hidden state machine types
    // skip here too (a nested state machine renders nowhere).
    for (std::uint32_t nestedToken : file.GetNestedTypes(typeToken)) {
        if (TypeIsHiddenFromRender(file, nestedToken))
            continue;
        std::string nestedText;
        if (DecompileTypeToStringBody(file, typeSystem, module,
                                      partialLookup, usingNamespaces,
                                      nestedToken, nestedText))
            out += nestedText;
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
        const TS::IProperty* propertyEntity =
            module.GetDefinitionProperty(p.Token);
        out += MemberAttributesText(propertyEntity);
        out += MemberModifiersText(propertyEntity);
        out += propertyTypeName;
        out += ' ';
        out += p.Name;
        // The accessor forms (the C# TransformAutomaticProperty's decision
        // + ConvertAccessor): the stub form (`get; set;`) when the property
        // has its compiler-generated `<Name>k__BackingField` field (only
        // the compiler emits the angle-bracket names) or its accessors
        // carry no bodies (an interface member); a real accessor body
        // renders as its block.
        bool hasBackingField = false;
        const std::string backingName = "<" + p.Name + ">k__BackingField";
        for (const auto& f : file.GetFields(typeToken)) {
            if (f.Name == backingName) {
                hasBackingField = true;
                break;
            }
        }
        bool anyAccessor = false;
        std::string getterBody, setterBody;
        if (!hasBackingField) {
            getterBody = AccessorBodyText(file, typeSystem,
                                          accessors.GetterToken, "get");
            setterBody = AccessorBodyText(file, typeSystem,
                                          accessors.SetterToken, "set");
        }
        if (hasBackingField || (getterBody.empty() && setterBody.empty())) {
            out += " { ";
            if (accessors.GetterToken != 0) {
                out += AccessorVisibilityText(
                           module.GetDefinitionMethod(accessors.GetterToken),
                           propertyEntity);
                out += "get; ";
                anyAccessor = true;
            }
            if (accessors.SetterToken != 0) {
                out += AccessorVisibilityText(
                           module.GetDefinitionMethod(accessors.SetterToken),
                           propertyEntity);
                out += "set; ";
                anyAccessor = true;
            }
            out += "}\n";
        } else if (accessors.SetterToken == 0 && !getterBody.empty()) {
            // The C# expression-bodied form (NormalizeBlockStatements's
            // SimplifyPropertyDeclaration under
            // UseExpressionBodyForCalculatedGetterOnlyProperties, on by
            // default): a getter-only property whose body is a single
            // `return <expr>;` statement renders `=> <expr>;`.
            std::size_t bodyStart =
                getterBody.find_first_not_of(" \t\n");
            std::size_t bodyEnd = getterBody.find_last_not_of(" \t\n");
            std::string trimmedBody =
                bodyStart == std::string::npos
                    ? std::string()
                    : getterBody.substr(bodyStart,
                                        bodyEnd - bodyStart + 1);
            bool singleReturn =
                trimmedBody.rfind("return ", 0) == 0 &&
                trimmedBody.size() > 7 + 1 &&
                trimmedBody.find(';') ==
                    trimmedBody.size() - 1;
            if (singleReturn) {
                out += " => " +
                       trimmedBody.substr(7, trimmedBody.size() - 7 - 1) +
                       ";\n";
                anyAccessor = true;
            }
        }
        if (!anyAccessor) {
            out += "\n{\n";
            if (accessors.GetterToken != 0) {
                out += AccessorVisibilityText(
                    module.GetDefinitionMethod(accessors.GetterToken),
                    propertyEntity);
                if (getterBody.empty()) {
                    out += "get;\n";
                } else {
                    out += "get\n{\n" + getterBody + "}\n";
                }
                anyAccessor = true;
            }
            if (accessors.SetterToken != 0) {
                out += AccessorVisibilityText(
                    module.GetDefinitionMethod(accessors.SetterToken),
                    propertyEntity);
                if (setterBody.empty()) {
                    out += "set;\n";
                } else {
                    out += "set\n{\n" + setterBody + "}\n";
                }
                anyAccessor = true;
            }
            out += "}\n";
        }
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
            out += MemberAttributesText(event);
            out += MemberModifiersText(event);
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
    // The enum member list (the C# DoDecompile's field arm for an enum's
    // const fields -- the EnumMemberDeclaration with the display mode):
    // the bare names for the consecutive-from-zero mode, the first-only
    // mode for a non-zero start, all values or all-hex otherwise. The
    // special value__ instance field never renders (the C# SpecialName
    // field the enum member list excludes).
    bool typeIsEnum = false;
    for (const auto& t : file.TypeDefs()) {
        if (t.Token == typeToken) {
            typeIsEnum = t.Kind == TS::TypeKind::Enum;
            break;
        }
    }
    if (typeIsEnum) {
        // The C# DetectBestEnumValueDisplayMode analysis over the const
        // fields' values.
        enum class EnumValueDisplayMode { None, FirstOnly, All, AllHex };
        EnumValueDisplayMode displayMode = EnumValueDisplayMode::None;
        {
            bool first = true, allConsecutive = true, allPowersOfTwo = true;
            std::int64_t firstValue = 0, previousValue = 0;
            bool outOfOrder = false;
            for (const auto& f : file.GetFields(typeToken)) {
                const TS::IField* fieldEntity =
                    module.GetDefinitionField(f.Token);
                if (fieldEntity == nullptr || !fieldEntity->IsConst())
                    continue;
                std::any constant = fieldEntity->GetConstantValue();
                std::int64_t currentValue = 0;
                bool haveValue = false;
                if (auto* v = std::any_cast<std::int32_t>(&constant)) {
                    currentValue = *v;
                    haveValue = true;
                } else if (auto* v =
                               std::any_cast<std::uint32_t>(&constant)) {
                    currentValue = static_cast<std::int64_t>(*v);
                    haveValue = true;
                } else if (auto* v =
                               std::any_cast<std::int64_t>(&constant)) {
                    currentValue = *v;
                    haveValue = true;
                } else if (auto* v =
                               std::any_cast<std::uint8_t>(&constant)) {
                    currentValue = *v;
                    haveValue = true;
                } else if (auto* v =
                               std::any_cast<std::int8_t>(&constant)) {
                    currentValue = *v;
                    haveValue = true;
                } else if (auto* v =
                               std::any_cast<std::int16_t>(&constant)) {
                    currentValue = *v;
                    haveValue = true;
                } else if (auto* v =
                               std::any_cast<std::uint16_t>(&constant)) {
                    currentValue = *v;
                    haveValue = true;
                }
                if (!haveValue)
                    continue;
                allConsecutive =
                    allConsecutive && (first || previousValue + 1 ==
                                                       currentValue);
                // N & (N - 1) == 0 iff N is a power of 2 (0 counts).
                std::uint64_t u =
                    static_cast<std::uint64_t>(currentValue);
                allPowersOfTwo =
                    allPowersOfTwo && (u & (u == 0 ? 0 : u - 1)) == 0;
                if (first) {
                    firstValue = currentValue;
                    first = false;
                } else if (currentValue <= previousValue) {
                    outOfOrder = true;
                    break;
                }
                previousValue = currentValue;
            }
            if (outOfOrder) {
                displayMode = EnumValueDisplayMode::All;
            } else if (allPowersOfTwo) {
                if (previousValue > 8)
                    displayMode = EnumValueDisplayMode::AllHex;
                else if (!allConsecutive)
                    displayMode = EnumValueDisplayMode::All;
            }
            if (displayMode == EnumValueDisplayMode::None &&
                !(!allConsecutive && !allPowersOfTwo)) {
                displayMode = firstValue == 0
                                  ? EnumValueDisplayMode::None
                                  : EnumValueDisplayMode::FirstOnly;
            }
        }
        bool anyMember = false;
        bool firstMember = true;
        for (const auto& f : file.GetFields(typeToken)) {
            const TS::IField* fieldEntity =
                module.GetDefinitionField(f.Token);
            if (fieldEntity == nullptr || !fieldEntity->IsConst())
                continue;
            out += f.Name;
            bool withInitializer =
                displayMode == EnumValueDisplayMode::All ||
                displayMode == EnumValueDisplayMode::AllHex ||
                (displayMode == EnumValueDisplayMode::FirstOnly &&
                 firstMember);
            if (withInitializer) {
                std::string literal =
                    ConstantFieldLiteral(*fieldEntity);
                if (displayMode == EnumValueDisplayMode::AllHex) {
                    // The C# AllHex arm: values >= 10 render as 0x + the
                    // uppercase hex form.
                    std::any constant = fieldEntity->GetConstantValue();
                    std::int64_t v = 0;
                    if (auto* p = std::any_cast<std::int32_t>(&constant))
                        v = *p;
                    else if (auto* p =
                                 std::any_cast<std::uint32_t>(&constant))
                        v = static_cast<std::int64_t>(*p);
                    else if (auto* p =
                                 std::any_cast<std::int64_t>(&constant))
                        v = *p;
                    if (v >= 10) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "0x%llX",
                                       static_cast<unsigned long long>(v));
                        literal = buf;
                    }
                }
                if (!literal.empty())
                    out += " = " + literal;
            }
            out += ",\n";
            anyMember = true;
            firstMember = false;
        }
        if (anyMember) {
            // The trailing comma drops (the C# member list's last entry).
            if (out.size() >= 2 && out.compare(out.size() - 2, 2, ",\n") == 0)
                out[out.size() - 2] = '\n', out.pop_back();
            rendered = true;
        }
    } else
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
        const TS::IField* fieldEntity = module.GetDefinitionField(f.Token);
        out += MemberAttributesText(fieldEntity);
        out += MemberModifiersText(fieldEntity);
        out += fieldTypeName;
        out += ' ';
        out += f.Name;
        if (fieldEntity != nullptr && fieldEntity->IsConst()) {
            std::string literal = ConstantFieldLiteral(*fieldEntity);
            if (!literal.empty())
                out += " = " + literal;
        }
        out += ";\n";
        rendered = true;
    }
    for (const auto& m : file.GetMethods(typeToken)) {
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
        // The explicit interface implementation (the C#
        // DoDecompileMethod's name rewrite + GetExplicitInterfaceType):
        // the name after the last dot, qualified by the first
        // implemented member's declaring type -- `IShape.Area` instead
        // of the dotted metadata name.
        std::string methodName = isConstructor ? typeName : m.Name;
        const TS::IMethod* methodEntity = module.GetDefinitionMethod(m.Token);
        if (!isConstructor && methodEntity != nullptr &&
            methodEntity->IsExplicitInterfaceImplementation()) {
            methodName = methodName.substr(
                methodName.find_last_of('.') + 1);
            std::vector<const TS::IMember*> implemented =
                methodEntity->ExplicitlyImplementedInterfaceMembers();
            if (!implemented.empty() && implemented[0] != nullptr &&
                implemented[0]->DeclaringType() != nullptr) {
                methodName = IL::CSharpTypeName(
                                 implemented[0]->DeclaringType()) +
                             "." + methodName;
            }
        }
        std::string modifiers =
            MemberModifiersText(methodEntity);
        if (m.RVA == 0) {
            // The C# DoDecompileMethod's body-less arm: an abstract method
            // (or an interface member) renders as a declaration with no
            // body; a body-less non-abstract member of a non-interface type
            // is externally implemented (the C# adds the extern modifier).
            if (methodEntity != nullptr && !methodEntity->IsAbstract() &&
                methodEntity->DeclaringType() != nullptr &&
                methodEntity->DeclaringType()->Kind() !=
                    TS::TypeKind::Interface) {
                modifiers += "extern ";
            }
            std::string returnType = "void";
            std::string paramDecl;
            if (auto sig = file.GetMethodSignature(m.Token)) {
                if (sig->ReturnType &&
                    sig->ReturnType->ReflectionName() != "System.Void")
                    returnType = IL::CSharpTypeName(sig->ReturnType);
                auto paramNames = file.GetParameterNames(m.Token);
                paramDecl = CSharpDecompiler::MethodDeclString(*sig,
                                                                paramNames);
            }
            out += MemberAttributesText(methodEntity);
            out += modifiers;
            out += isConstructor ? std::string() : returnType + " ";
            out += methodName;
            out += "(" + paramDecl + ");\n";
            rendered = true;
            continue;
        }
        std::string text;
        bool asyncDecompiled = false;
        bool iteratorDecompiled = false;
        if (CSharpDecompiler::DecompileMethodToString(
                file, typeSystem, m.Token, m.RVA, methodName, text,
                isConstructor, &asyncDecompiled, &iteratorDecompiled)) {
            out += MemberAttributesText(methodEntity, asyncDecompiled,
                                        iteratorDecompiled);
            out += modifiers;
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
    std::string& out, bool wrapNamespace,
    const std::vector<std::string>* usingNamespaces) {
    // The static scaffold's per-call wiring (the note above): the resolver
    // + the reference-loaded type system, one pair per call.
    Metadata::UniversalAssemblyResolver resolver(
        file.FileName(), false, Metadata::DetectTargetFrameworkId(file));
    TS::DecompilerTypeSystem typeSystem(file, resolver);
    // The -t render's using set (the type's own collected namespaces) for
    // the resolver's scope; the whole-module loop overrides it with the
    // module-wide set (one DecompileRun over every type).
    std::vector<std::string> ownUsingSet;
    const std::vector<std::string>* usingSet = usingNamespaces;
    if (usingSet == nullptr) {
        ownUsingSet =
            MinimalUsingSetOf(file, typeSystem.MainMetadataModule(),
                              typeToken);
        usingSet = &ownUsingSet;
    }
    bool rendered = DecompileTypeToStringBody(
        file, &typeSystem, typeSystem.MainMetadataModule(),
        [](std::uint32_t token) {
            return FindRegisteredPartialType(token);
        },
        usingSet, typeToken, out);
    if (rendered && wrapNamespace) {
        // The single-type render's leading using directives + the
        // namespace header (the C# -t render's file-scoped form:
        // `using ...;` then `namespace X;` before the declaration; a type
        // with no namespace renders the bare header).
        std::string ns = RootNamespaceOf(file, typeToken);
        if (!ns.empty())
            out = "namespace " + ns + ";\n\n" + out;
        out = UsingDirectivesText(file, typeSystem.MainMetadataModule(),
                                 typeToken) +
             out;
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

// The whole-module render's leading block: the module-wide using header
// (the C# IntroduceUsingDeclarations over the whole-module tree -- the
// usings cover the type bodies' references, not just the attribute
// namespaces the attribute tree's own transform collects) followed by the
// attribute sections. The module's own type namespaces never appear (the
// tree's names inside them resolve through the namespace declarations).
// The whole-module render's using set (the C# CreateDecompileRun over
// every type's collected namespaces -- the single module-wide
// DecompileRun): a namespace emits iff some type OUTSIDE it references it
// (a name inside `namespace N { }` resolves without a using for N, so N's
// own types never pull it in; a module-owned namespace referenced only by
// its own types -- the connid's stub types -- never appears either). The
// assembly/module attributes' namespaces always emit (they sit at the
// file root, outside every namespace block). The base-list qualification
// consumes the same set (the whole-module scope).
std::vector<std::string> WholeModuleUsingSet(
    const ::ILSpy::Decompiler::TypeSystem::MetadataModule& module) {
    std::set<std::string> emitted;
    for (const TS::ITypeDefinition* type : module.TypeDefinitions()) {
        if (type == nullptr)
            continue;
        std::unordered_set<std::string> typeNamespaces;
        CollectRequiredNamespaces(*type,
                                  const_cast<TS::MetadataModule&>(module),
                                  typeNamespaces);
        for (const std::string& ns : typeNamespaces) {
            if (!ns.empty() && ns != type->Namespace())
                emitted.insert(ns);
        }
    }
    std::unordered_set<std::string> attributeNamespaces;
    CollectAttributeNamespaces(const_cast<TS::MetadataModule&>(module),
                                attributeNamespaces);
    for (const std::string& ns : attributeNamespaces) {
        if (!ns.empty())
            emitted.insert(ns);
    }
    std::vector<std::string> sorted(emitted.begin(), emitted.end());
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

std::string WholeModuleHeader(
    const ::ILSpy::Decompiler::TypeSystem::MetadataModule& module,
    const std::vector<std::string>& usingSet) {
    std::string out;
    for (const std::string& ns : usingSet)
        out += "using " + ns + ";\n";
    if (!out.empty())
        out += "\n";
    // The attribute sections: the attribute tree rendered without its own
    // using declarations (the module-wide header replaces them).
    std::unique_ptr<Syntax::SyntaxTree> syntaxTree(
        CSharpDecompiler::DecompileModuleAndAssemblyAttributes(module));
    for (int i = syntaxTree->Members().Count() - 1; i >= 0; i--) {
        if (dynamic_cast<Syntax::UsingDeclaration*>(
                syntaxTree->Members().At(i)) != nullptr)
            syntaxTree->Members().At(i)->Remove();
    }
    OutputVisitor::CSharpFormattingOptions options =
        SettingsFormattingOptions();
    out += syntaxTree->ToString(&options);
    return out;
}

std::string CSharpDecompiler::DecompileWholeModuleToString() {
    // The C# DecompileWholeModuleAsSingleFile composition over THIS
    // instance's wiring: the attribute sections, then every type in
    // metadata order through the instance entries (the registry
    // consults this instance's map) grouped by namespace (the
    // DoDecompileTypes NamespaceDeclaration emission: consecutive
    // same-namespace types nest under one block; the empty namespace
    // renders at the root; a hidden type does not break the group).
    std::string out;
    // The whole-module using set computes ONCE (the
    // CollectRequiredNamespaces walk over every type is the render's most
    // expensive single pass) and feeds BOTH the emitted header and every
    // type's base-list scope (the C# single DecompileRun over the whole
    // tree: one using scope for every base-list decision).
    std::vector<std::string> moduleUsingSet =
        WholeModuleUsingSet(state_->typeSystem->MainMetadataModule());
    out += WholeModuleHeader(state_->typeSystem->MainMetadataModule(),
                             moduleUsingSet);
    std::string currentNamespace;
    bool namespaceOpen = false;
    for (const auto& t : state_->file->TypeDefs()) {
        if (t.Name == "<Module>")
            continue;
        if (TypeIsHiddenFromRender(*state_->file, t.Token))
            continue;
        // The nested types render inside their declaring type's braces
        // (the type-level body's nested-type loop), not as top-level
        // entries.
        auto nestedInfo = state_->file->GetTypeDefNameInfo(t.Token);
        if (nestedInfo.has_value() && nestedInfo->DeclaringTypeToken != 0)
            continue;
        if (t.Namespace != currentNamespace) {
            if (namespaceOpen)
                out += "}\n";
            if (!t.Namespace.empty()) {
                out += "namespace " + t.Namespace + "\n{\n";
                namespaceOpen = true;
            } else {
                namespaceOpen = false;
            }
            currentNamespace = t.Namespace;
        }
        std::string text;
        if (DecompileTypeToString(t.Token, text,
                                 /*wrapNamespace=*/false, &moduleUsingSet))
            out += text;
    }
    if (namespaceOpen)
        out += "}\n";
    return out;
}

bool CSharpDecompiler::DecompileTypeToString(
    std::uint32_t typeToken, std::string& out, bool wrapNamespace,
    const std::vector<std::string>* usingNamespaces) {
    // The -t render's using set (the type's own collected namespaces);
    // the whole-module loop overrides it with the module-wide set (one
    // DecompileRun over every type).
    std::vector<std::string> ownUsingSet;
    const std::vector<std::string>* usingSet = usingNamespaces;
    if (usingSet == nullptr) {
        ownUsingSet =
            MinimalUsingSetOf(*state_->file,
                              state_->typeSystem->MainMetadataModule(),
                              typeToken);
        usingSet = &ownUsingSet;
    }
    bool rendered = DecompileTypeToStringBody(
        *state_->file, state_->typeSystem ? &state_->typeSystem.value()
                                          : nullptr,
        state_->typeSystem->MainMetadataModule(),
        [this](std::uint32_t token) { return FindPartialTypeInfo(token); },
        usingSet, typeToken, out);
    if (rendered && wrapNamespace) {
        std::string ns = RootNamespaceOf(*state_->file, typeToken);
        if (!ns.empty())
            out = "namespace " + ns + ";\n\n" + out;
        out = UsingDirectivesText(*state_->file,
                                  state_->typeSystem->MainMetadataModule(),
                                  typeToken) +
             out;
    }
    return rendered;
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
    RunAstTransforms(*syntaxTree, decompileRun, decompilationContext);
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

// The C# `settings.CSharpFormattingOptions` (DecompilerSettings.cs lines
// 2406-2418): the Allman factory with the decompiler's three overrides --
// no switch-body indent, wrap-if-too-long array initializers, single-line
// auto properties. The C# caches the options per settings instance; the
// port builds them per render (a handful of option copies, no observable
// difference -- the options are value semantics either way).


// The C# `public string DecompileModuleAndAssemblyAttributesToString()`
// (CSharpDecompiler.cs line 838): `SyntaxTreeToString(
// DecompileModuleAndAssemblyAttributes())` -- the tree built by the AST
// path, rendered through the output visitor with the settings' formatting
// options (the C# SyntaxTreeToString's `settings.CSharpFormattingOptions`).
std::string CSharpDecompiler::DecompileModuleAndAssemblyAttributesToString(
    const ::ILSpy::Decompiler::TypeSystem::MetadataModule& module) {
    std::unique_ptr<Syntax::SyntaxTree> syntaxTree(
        DecompileModuleAndAssemblyAttributes(module));
    OutputVisitor::CSharpFormattingOptions options =
        SettingsFormattingOptions();
    return syntaxTree->ToString(&options);
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
    // DoDecompileModuleAndAssemblyAttributes call at line 917). The using
    // set computes once and feeds both the header and every type render's
    // base-list scope (the C# single DecompileRun over the whole tree).
    std::vector<std::string> moduleUsingSet =
        WholeModuleUsingSet(typeSystem.MainMetadataModule());
    out += WholeModuleHeader(typeSystem.MainMetadataModule(),
                             moduleUsingSet);
    // The types (the C# DoDecompileTypes loop in metadata order), grouped
    // by namespace (the NamespaceDeclaration emission; a hidden type does
    // not break the group).
    std::string currentNamespace;
    bool namespaceOpen = false;
    for (const auto& t : file.TypeDefs()) {
        if (t.Name == "<Module>")
            continue;
        if (TypeIsHiddenFromRender(file, t.Token))
            continue;
        auto nestedInfo = file.GetTypeDefNameInfo(t.Token);
        if (nestedInfo.has_value() && nestedInfo->DeclaringTypeToken != 0)
            continue;
        if (t.Namespace != currentNamespace) {
            if (namespaceOpen)
                out += "}\n";
            if (!t.Namespace.empty()) {
                out += "namespace " + t.Namespace + "\n{\n";
                namespaceOpen = true;
            } else {
                namespaceOpen = false;
            }
            currentNamespace = t.Namespace;
        }
        std::string text;
        if (DecompileTypeToString(file, t.Token, text,
                                  /*wrapNamespace=*/false,
                                  &moduleUsingSet))
            out += text;
    }
    if (namespaceOpen)
        out += "}\n";
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
    transforms.push_back(
        std::make_unique<Transforms::IntroduceUnsafeModifier>());
    transforms.push_back(std::make_unique<Transforms::AddCheckedBlocks>());
    transforms.push_back(std::make_unique<Transforms::DeclareVariables>());
    transforms.push_back(
        std::make_unique<Transforms::TransformFieldAndConstructorInitializers>());
    transforms.push_back(
        std::make_unique<Transforms::PrettifyAssignments>());
    transforms.push_back(
        std::make_unique<Transforms::IntroduceUsingDeclarations>());
    transforms.push_back(
        std::make_unique<Transforms::IntroduceExtensionMethods>());
    transforms.push_back(
        std::make_unique<Transforms::IntroduceQueryExpressions>());
    transforms.push_back(
        std::make_unique<Transforms::CombineQueryExpressions>());
    transforms.push_back(
        std::make_unique<Transforms::NormalizeBlockStatements>());
    transforms.push_back(
        std::make_unique<Transforms::FlattenSwitchBlocks>());
    transforms.push_back(
        std::make_unique<Transforms::RenameVisualBasicAnonymousTypes>());
    transforms.push_back(
        std::make_unique<Transforms::FixNameCollisions>());
    transforms.push_back(
        std::make_unique<Transforms::AddXmlDocumentationTransform>());
    return transforms;
}

void CSharpDecompiler::RunAstTransforms(
    Syntax::AstNode& rootNode, DecompileRun& decompileRun,
    const ::ILSpy::Decompiler::TypeSystem::ITypeResolveContext& decompilationContext) {
    // The C# RunTransforms shape: the context build, the up-front invariant
    // check, the transform loop with the per-entry step groups and invariant
    // checks, then the InsertParenthesesVisitor (the readability flag on)
    // and the GenericGrammarAmbiguityVisitor tail. The C# StepLimitReached /
    // CancellationToken bookkeeping is deferred with those surfaces.
    // The C# `var typeSystemAstBuilder = CreateAstBuilder(decompileRun.Settings)`
    // + the TransformContext ctor parameter: the type renderer the
    // insertion arms consume (ConvertType).
    Syntax::TypeSystemAstBuilder typeSystemAstBuilder =
        CreateAstBuilder(decompileRun.Settings());
    // The C# TransformContext ctor's third parameter (the IDecompilerTypeSystem
    // the ctor passes): the compilation the context's TypeSystem slot carries.
    // The C# passes the decompiler's type system -- an IDecompilerTypeSystem IS
    // an ITypeResolveContext -- so the port reads it off the decompilation
    // context parameter; a caller that passes none (the pipeline driver's
    // bare form) passes a bare SimpleTypeResolveContext over the module
    // instead (the null slot would leave nothing to resolve against).
    Transforms::TransformContext context(
        decompilationContext.Compilation(), decompileRun,
        decompilationContext, typeSystemAstBuilder);
    rootNode.CheckInvariant();
    for (const auto& transform : GetAstTransforms()) {
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
