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
#include "Decompiler/Documentation/XmlDocumentationProvider.hpp"
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
#include "Decompiler/Metadata/SRMExtensions.hpp"
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
#include <set>
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

// The native-integer type-system option (the C# GetOptions'
// NativeIntegersWithoutAttribute under the DecompilerSettings'
// NumericIntPtr flag): the oracle's effective behavior renders every
// System.IntPtr as nint on the modern .NET targets and keeps the full
// name on the .NETFramework ones (the language-version gate the
// settings' ctor applies turns NumericIntPtr off below C# 11; the TFM
// approximates the boundary the oracle's renders show).
TS::TypeSystemOptions NativeIntegerOptionsFor(
    const Metadata::MetadataFile& file) {
    TS::TypeSystemOptions options = TS::TypeSystemOptions::Default;
    const std::string tfm =
        Metadata::DetectTargetFrameworkId(file).value_or(std::string());
    // The .NETFramework family (and its legacy siblings) keeps the full
    // names; the modern targets render the native-integer keywords.
    if (tfm.find(".NETFramework") == 0 || tfm.find("Silverlight") == 0 ||
        tfm.find(".NETPortable") == 0)
        return options;
    return options | TS::TypeSystemOptions::NativeIntegersWithoutAttribute;
}

namespace {

// The base-list/member-signature name composition (the C#
// ConvertTypeHelper decision), forward-declared for the early class
// member definitions above the full helper block below (the helpers
// live in the later anonymous namespace with the scope machinery).
std::string RenderBaseTypeName(
    const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDef,
    const ::ILSpy::Decompiler::TypeSystem::ITypePtr& instantiation,
    const ::ILSpy::Decompiler::CSharp::Resolver::CSharpResolver* resolver);

// The boxed constant's literal spelling (the shared field-constant and
// parameter-default renderer), the same forward-declaration pattern.
std::string ConstantValueText(const std::any& value,
                              const ::ILSpy::Decompiler::TypeSystem::IType& type);

// The parameter-attribute renderer (the compact bracket-adjacent form),
// the same forward-declaration pattern.
std::string ParameterAttributesText(
    const ::ILSpy::Decompiler::TypeSystem::IParameter* parameter);

} // namespace

// The C# Decompile path's parameter-declaration builder (the CLI's inline
// block, extracted): named parameters carry their metadata name; unnamed
// parameters fall back to arg_<base + index> (base 1 for an instance
// method -- `this` is the implicit arg_0; base 0 static).
std::string CSharpDecompiler::MethodDeclString(
    const Metadata::MethodSignature& signature,
    const std::vector<std::string>& parameterNames,
    const Resolver::CSharpResolver* scopeResolver) {
    // The file-signature decode path: no parameter entities, so the
    // modifiers are unavailable (the reference kinds live only on the
    // type-system entities); the types render without them.
    std::string paramDecl;
    const int base = signature.IsInstance ? 1 : 0;
    for (std::size_t i = 0; i < signature.ParameterTypes.size(); ++i) {
        if (i != 0) paramDecl += ", ";
        if (signature.ParameterTypes[i] != nullptr)
            paramDecl += RenderBaseTypeName(
                signature.ParameterTypes[i]->GetDefinition(),
                signature.ParameterTypes[i], scopeResolver);
        else
            paramDecl += "object";
        paramDecl += ' ';
        if (i < parameterNames.size() && !parameterNames[i].empty())
            paramDecl += parameterNames[i];
        else
            paramDecl += "arg_" + std::to_string(base + static_cast<int>(i));
    }
    return paramDecl;
}

std::string CSharpDecompiler::MethodDeclString(
    const std::vector<const TS::IParameter*>& parameters, bool isInstance,
    const std::vector<std::string>& parameterNames,
    const Resolver::CSharpResolver* scopeResolver,
    bool isExtensionMethod) {
    std::string paramDecl;
    const int base = isInstance ? 1 : 0;
    for (std::size_t i = 0; i < parameters.size(); ++i) {
        if (i != 0) paramDecl += ", ";
        const TS::IParameter* parameter = parameters[i];
        if (parameter == nullptr)
            continue;
        // The C# extension-method form: the first parameter of an
        // extension method renders the `this` modifier (the method's
        // Extension attribute classification; the C# AstBuilder emits it
        // through the ParameterDeclaration's extension-method flag).
        if (i == 0 && isExtensionMethod)
            paramDecl += "this ";
        // The C# ConvertParameter's attribute half: the parameter's
        // attributes render as the compact bracket-adjacent sections
        // before the modifiers.
        std::string parameterAttributes = ParameterAttributesText(parameter);
        if (!parameterAttributes.empty())
            paramDecl += parameterAttributes + " ";
        // The C# ConvertParameter's modifier composition: the params
        // array leads, then the reference kind.
        if (parameter->IsParams())
            paramDecl += "params ";
        switch (parameter->ReferenceKind()) {
            case TS::ReferenceKind::Ref:
                paramDecl += "ref ";
                break;
            case TS::ReferenceKind::Out:
                paramDecl += "out ";
                break;
            case TS::ReferenceKind::In:
                paramDecl += "in ";
                break;
            case TS::ReferenceKind::RefReadOnly:
                paramDecl += "ref readonly ";
                break;
            default:
                break;
        }
        TS::ITypePtr type(const_cast<TS::IType*>(&parameter->Type()),
                          [](TS::IType*) {});
        paramDecl += RenderBaseTypeName(type->GetDefinition(), type,
                                        scopeResolver);
        paramDecl += ' ';
        if (i < parameterNames.size() && !parameterNames[i].empty())
            paramDecl += parameterNames[i];
        else
            paramDecl += "arg_" + std::to_string(base + static_cast<int>(i));
        // The C# IsDefaultValueAssignmentAllowed: an optional parameter
        // with a signature constant renders `= value` (the reference
        // kind None/in/ref-readonly gate), and only when every LATER
        // parameter is itself optional-or-params (the optional
        // parameters must be trailing).
        bool defaultValueAllowed =
            parameter->IsOptional() &&
            parameter->HasConstantValueInSignature() &&
            (parameter->ReferenceKind() == TS::ReferenceKind::None ||
             parameter->ReferenceKind() == TS::ReferenceKind::In ||
             parameter->ReferenceKind() == TS::ReferenceKind::RefReadOnly);
        if (defaultValueAllowed) {
            for (std::size_t j = i + 1; j < parameters.size(); ++j) {
                const TS::IParameter* other = parameters[j];
                if (other == nullptr || other->IsParams())
                    continue;
                if (!(other->IsOptional() &&
                      other->HasConstantValueInSignature()))
                    defaultValueAllowed = false;
            }
        }
        if (defaultValueAllowed) {
            std::string literal;
            try {
                literal = ConstantValueText(parameter->GetConstantValue(),
                                             parameter->Type());
            } catch (const std::exception&) {
                literal = std::string();
            }
            if (literal.empty() &&
                parameter->Type().Kind() == TS::TypeKind::TypeParameter) {
                // The C# default-value form for a type-parameter-typed
                // optional parameter: `T value = default(T)` (the null
                // constant over the parameter type).
                literal = "default(" + parameter->Type().Name() + ")";
            }
            if (!literal.empty())
                paramDecl += " = " + literal;
        }
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
    TS::DecompilerTypeSystem typeSystem(file, resolver,
                                        NativeIntegerOptionsFor(file));
    return DecompileMethodToString(file, &typeSystem, methodToken,
                                   methodRva, methodName, out, isConstructor);
}

bool CSharpDecompiler::DecompileMethodToString(
    const Metadata::MetadataFile& file,
    TS::DecompilerTypeSystem* typeSystem, std::uint32_t methodToken,
    std::uint32_t methodRva, const std::string& methodName,
    std::string& out, bool isConstructor, bool* asyncDecompiled,
    bool* iteratorDecompiled,
    const Resolver::CSharpResolver* scopeResolver,
    const std::string& methodConstraints) {
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
    if (resolvedMethod != nullptr) {
        // The entity path: the resolved return type + parameters (the
        // name decision needs the definitions; the file-signature decode
        // yields unresolved simple types).
        if (resolvedMethod->ReturnType().ReflectionName() != "System.Void") {
            TS::ITypePtr resolvedReturnType(
                const_cast<TS::IType*>(&resolvedMethod->ReturnType()),
                [](TS::IType*) {});
            returnType = RenderBaseTypeName(
                resolvedReturnType->GetDefinition(), resolvedReturnType,
                scopeResolver);
        }
        // The conversion operators: the implicit/explicit keyword rides the
        // return-type slot (the caller's name rewrite produced the `operator
        // <Type>` name; this overload's own return-type computation must
        // not override the keyword with the entity's return type).
        if (resolvedMethod->Name() == "op_Implicit" ||
            resolvedMethod->Name() == "op_CheckedImplicit") {
            returnType = "implicit";
        } else if (resolvedMethod->Name() == "op_Explicit" ||
                   resolvedMethod->Name() == "op_CheckedExplicit") {
            returnType = "explicit";
        }
        std::vector<const TS::IParameter*> parameters =
            resolvedMethod->Parameters();
        auto paramNames = file.GetParameterNames(methodToken);
        paramDecl = MethodDeclString(parameters,
                                      !resolvedMethod->IsStatic(), paramNames,
                                      scopeResolver,
                                      resolvedMethod->IsExtensionMethod());
    } else if (auto sig = file.GetMethodSignature(methodToken)) {
        if (sig->ReturnType &&
            sig->ReturnType->ReflectionName() != "System.Void")
            returnType = RenderBaseTypeName(sig->ReturnType->GetDefinition(),
                                            sig->ReturnType, scopeResolver);
        auto paramNames = file.GetParameterNames(methodToken);
        paramDecl = MethodDeclString(*sig, paramNames, scopeResolver);
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
                            isConstructor, methodConstraints);
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
// The boxed-constant equality over the metadata constant forms (the
// enum members' underlying integral types).
bool ValuesEqual(const std::any& a, const std::any& b) {
    auto asLong = [](const std::any& v) -> std::optional<std::int64_t> {
        if (auto* p = std::any_cast<std::int32_t>(&v))
            return *p;
        if (auto* p = std::any_cast<std::uint32_t>(&v))
            return static_cast<std::int64_t>(*p);
        if (auto* p = std::any_cast<std::int64_t>(&v))
            return *p;
        if (auto* p = std::any_cast<std::uint64_t>(&v))
            return static_cast<std::int64_t>(*p);
        if (auto* p = std::any_cast<std::int8_t>(&v))
            return *p;
        if (auto* p = std::any_cast<std::uint8_t>(&v))
            return *p;
        if (auto* p = std::any_cast<std::int16_t>(&v))
            return *p;
        if (auto* p = std::any_cast<std::uint16_t>(&v))
            return *p;
        return std::nullopt;
    };
    auto av = asLong(a);
    auto bv = asLong(b);
    return av.has_value() && bv.has_value() && *av == *bv;
}

// The C# ConvertEnumValue's [Flags] arms: the union form
// (`NODRAWCAPTION | NODRAWICON`), the complement form (`~X`), and the
// multi-bit-within-mask rule (keep numeric). The members iterate by
// weight DESCENDING (stable -- the declaration order within a weight),
// skip the zero values, the self row, and the later rows (the row-order
// rule: a member can only reference members declared before it). The
// complement competes when its expression has fewer terms; a byte- or
// ushort-based enum never takes it (the initializer would not convert
// back). Returns the empty string when no composition form applies.
std::string EnumFlagsComposition(
    const Metadata::MetadataFile& file, const TS::MetadataModule& module,
    std::uint32_t enumToken, const std::any& constantValue,
    std::uint32_t declaringRow, const std::string& qualifier) {
    std::int64_t val = 0;
    bool hasVal = false;
    if (auto* p = std::any_cast<std::int32_t>(&constantValue)) {
        val = *p; hasVal = true;
    } else if (auto* p = std::any_cast<std::uint32_t>(&constantValue)) {
        val = static_cast<std::int64_t>(*p); hasVal = true;
    } else if (auto* p = std::any_cast<std::int64_t>(&constantValue)) {
        val = *p; hasVal = true;
    }
    if (!hasVal)
        return std::string();
    // The members with their weights (the single-bit count), kept in
    // the declaration order; the iteration sorts by weight descending
    // (stable).
    struct Candidate {
        std::int64_t value;
        std::string name;
        std::uint32_t row;
        int weight;
    };
    std::vector<Candidate> candidates;
    for (const auto& other : file.GetFields(enumToken)) {
        const TS::IField* otherEntity = module.GetDefinitionField(other.Token);
        if (otherEntity == nullptr || !otherEntity->IsConst())
            continue;
        std::any otherValue;
        try {
            otherValue = otherEntity->GetConstantValue();
        } catch (const std::exception&) {
            continue;
        }
        std::int64_t v = 0;
        if (auto* p = std::any_cast<std::int32_t>(&otherValue))
            v = *p;
        else if (auto* p = std::any_cast<std::uint32_t>(&otherValue))
            v = static_cast<std::int64_t>(*p);
        else if (auto* p = std::any_cast<std::int64_t>(&otherValue))
            v = *p;
        else
            continue;
        int weight = 0;
        std::uint64_t u = static_cast<std::uint64_t>(v);
        while (u != 0) {
            u &= u - 1;
            ++weight;
        }
        candidates.push_back({v, other.Name, other.Token & 0xFFFFFF, weight});
    }
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const Candidate& a, const Candidate& b) {
                         return a.weight > b.weight;
                     });
    // The C# limits the negated value to the underlying range for the
    // byte/short/int families only -- a 64-bit underlying type keeps
    // the full complement (whose high bits never clear, so a long-based
    // enum's complement never completes and stays numeric; masking it
    // to 32 bits would wrongly complete it).
    std::int64_t enumValue = val;
    std::int64_t negatedValue;
    if (auto* p = std::any_cast<std::int64_t>(&constantValue))
        negatedValue = ~*p;
    else if (auto* p = std::any_cast<std::uint64_t>(&constantValue))
        negatedValue = static_cast<std::int64_t>(~*p);
    else if (auto* p = std::any_cast<std::uint32_t>(&constantValue))
        negatedValue =
            static_cast<std::int64_t>(~*p & 0xFFFFFFFFu);
    else if (auto* p = std::any_cast<std::int32_t>(&constantValue))
        negatedValue = static_cast<std::int64_t>(
            ~static_cast<std::uint32_t>(*p) & 0xFFFFFFFFu);
    else if (auto* p = std::any_cast<std::uint16_t>(&constantValue))
        negatedValue = static_cast<std::int64_t>(~*p & 0xFFFFu);
    else if (auto* p = std::any_cast<std::int16_t>(&constantValue))
        negatedValue = static_cast<std::int64_t>(
            ~static_cast<std::uint16_t>(*p) & 0xFFFFu);
    else if (auto* p = std::any_cast<std::uint8_t>(&constantValue))
        negatedValue = static_cast<std::int64_t>(~*p & 0xFFu);
    else if (auto* p = std::any_cast<std::int8_t>(&constantValue))
        negatedValue = static_cast<std::int64_t>(
            ~static_cast<std::uint8_t>(*p) & 0xFFu);
    else
        return std::string();
    std::vector<std::string> unionTerms;
    std::vector<std::string> negatedTerms;
    for (const Candidate& candidate : candidates) {
        if (candidate.value == 0 || candidate.row >= declaringRow)
            // The C# skips the None member and the later rows.
            continue;
        std::string reference = qualifier.empty()
                                    ? candidate.name
                                    : qualifier + "." + candidate.name;
        if ((candidate.value & enumValue) == candidate.value) {
            unionTerms.push_back(reference);
            enumValue &= ~candidate.value;
        }
        if ((candidate.value & negatedValue) == candidate.value) {
            negatedTerms.push_back(reference);
            negatedValue &= ~candidate.value;
        }
    }
    // The multi-bit-within-mask rule: a value lying entirely within a
    // LARGER, previously declared member is a field encoding inside
    // that mask, not a union of flags -- keep it numeric.
    bool encodedInEarlierMask = false;
    for (const Candidate& candidate : candidates) {
        if (candidate.value == val || candidate.row >= declaringRow)
            continue;
        if ((candidate.value & val) == val && candidate.value != val) {
            encodedInEarlierMask = true;
            break;
        }
    }
    if (enumValue == 0 && !unionTerms.empty() && !encodedInEarlierMask) {
        if (!(negatedValue == 0 && !negatedTerms.empty() &&
              negatedTerms.size() < unionTerms.size())) {
            std::string out;
            for (std::size_t i = 0; i < unionTerms.size(); ++i) {
                if (i != 0)
                    out += " | ";
                out += unionTerms[i];
            }
            return out;
        }
    }
    if (negatedValue == 0 && !negatedTerms.empty() &&
        (enumValue != 0 || unionTerms.empty() ||
         negatedTerms.size() < unionTerms.size())) {
        std::string out = "~";
        for (std::size_t i = 0; i < negatedTerms.size(); ++i) {
            if (i != 0)
                out += " | ";
            out += negatedTerms[i];
        }
        return out;
    }
    return std::string();
}

// The C# specialConstants table (the TypeSystemAstBuilder): the
// integral constants at the type boundaries render as the well-known
// member names (`uint.MaxValue`), when the constant's OWN type matches
// (the C# dictionary keys by the boxed value -- the type check comes
// from the field's declared type).
std::string SpecialConstantText(const std::any& value,
                                const TS::IType& type) {
    auto* i32 = std::any_cast<std::int32_t>(&value);
    auto* u32 = std::any_cast<std::uint32_t>(&value);
    auto* i64 = std::any_cast<std::int64_t>(&value);
    auto* u64 = std::any_cast<std::uint64_t>(&value);
    auto* i16 = std::any_cast<std::int16_t>(&value);
    auto* u16 = std::any_cast<std::uint16_t>(&value);
    auto* i8 = std::any_cast<std::int8_t>(&value);
    auto* u8 = std::any_cast<std::uint8_t>(&value);
    if (u32 != nullptr && *u32 == 0xFFFFFFFFu)
        return "uint.MaxValue";
    if (i32 != nullptr && *i32 == 2147483647)
        return "int.MaxValue";
    if (i32 != nullptr && *i32 == (-2147483647 - 1))
        return "int.MinValue";
    if (u64 != nullptr && *u64 == 0xFFFFFFFFFFFFFFFFull)
        return "ulong.MaxValue";
    if (i64 != nullptr && *i64 == 9223372036854775807LL)
        return "long.MaxValue";
    if (i64 != nullptr && *i64 == (-9223372036854775807LL - 1))
        return "long.MinValue";
    if (u16 != nullptr && *u16 == 0xFFFFu)
        return "ushort.MaxValue";
    if (i16 != nullptr && *i16 == 32767)
        return "short.MaxValue";
    if (i16 != nullptr && *i16 == -32768)
        return "short.MinValue";
    if (u8 != nullptr && *u8 == 0xFFu)
        return "byte.MaxValue";
    if (i8 != nullptr && *i8 == 127)
        return "sbyte.MaxValue";
    if (i8 != nullptr && *i8 == -128)
        return "sbyte.MinValue";
    return std::string();
}

// The C# ConvertEnumValue over a const FIELD's enum-typed constant
// (declaringEnumMember == null): the QUALIFIED member references (no
// row rule), the [Flags] composition with the same qualification, or
// the `(EnumType)value` cast fallback. Returns the empty string when
// the field is not an enum-typed constant.
std::string EnumConstantFieldExpression(
    const Metadata::MetadataFile& file, const TS::MetadataModule& module,
    const TS::IField& field) {
    const TS::IType& fieldType = field.Type();
    const TS::ITypeDefinition* enumDef = fieldType.GetDefinition();
    if (enumDef == nullptr || enumDef->Kind() != TS::TypeKind::Enum)
        return std::string();
    std::any constantValue;
    try {
        constantValue = field.GetConstantValue();
    } catch (const std::exception&) {
        return std::string();
    }
    const std::uint32_t enumToken = enumDef->MetadataToken();
    const std::string enumName = enumDef->Name();
    // The direct member match: the FIRST equal-value member, any row.
    for (const auto& other : file.GetFields(enumToken)) {
        const TS::IField* otherEntity = module.GetDefinitionField(other.Token);
        if (otherEntity == nullptr || !otherEntity->IsConst())
            continue;
        std::any otherValue;
        try {
            otherValue = otherEntity->GetConstantValue();
        } catch (const std::exception&) {
            continue;
        }
        if (ValuesEqual(constantValue, otherValue))
            return enumName + "." + other.Name;
    }
    // The [Flags] composition, qualified.
    if (Metadata::HasKnownAttribute(file, enumToken,
                                    TS::KnownAttribute::Flags)) {
        std::string composition = EnumFlagsComposition(
            file, module, enumToken, constantValue,
            0xFFFFFFFFu, enumName);
        if (!composition.empty())
            return composition;
    }
    // The cast fallback: `(EnumType)value` (the numeric literal).
    std::string numeric = ConstantValueText(constantValue, fieldType);
    if (numeric.empty())
        return std::string();
    return "(" + enumName + ")" + numeric;
}

// The C# accessor's return-type attributes: the `[return: ...]`
// sections (the C# DoDecompileProperty's accessor arm renders them
// inside the accessor block -- the stub form cannot carry them, so a
// property whose accessor has return attributes takes the block form).
std::string AccessorReturnAttributesText(const TS::IMethod* accessor) {
    if (accessor == nullptr)
        return std::string();
    std::vector<const TS::IAttribute*> attributes =
        accessor->GetReturnTypeAttributes();
    if (attributes.empty())
        return std::string();
    OutputVisitor::CSharpFormattingOptions options =
        SettingsFormattingOptions();
    SyntaxNS::TypeSystemAstBuilder builder;
    builder.ShowAttributes() = true;
    builder.AlwaysUseShortTypeNames() = true;
    std::string out;
    for (const TS::IAttribute* a : attributes) {
        if (a == nullptr)
            continue;
        Syntax::AttributeSection section(builder.ConvertAttribute(*a));
        section.AttributeTarget("return");
        std::string text = section.ToString(&options);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        if (!text.empty())
            out += text + "\n";
    }
    return out;
}

// The C# SetNewModifier: a member introduced in a class or struct hides
// an accessible same-name member (or nested type) in a NON-INTERFACE base
// type -- the methods hide by signature (the name, the type-parameter
// count, and the parameter list including the reference kinds), the
// fields, properties, events, and types by name alone. The hide grants
// the `new` modifier. The comparison runs at the definition level (the
// port's base-type walk yields the definitions, not the constructed
// instantiations, so a generic base's substituted signature may
// under-match -- the corpus shapes are the non-generic re-implementations).
bool MemberHidesBaseMember(const TS::IMember* member,
                            const TS::MetadataModule& module) {
    if (member == nullptr || member->DeclaringType() == nullptr)
        return false;
    const TS::ITypeDefinition* declaringDef =
        member->DeclaringType()->GetDefinition();
    if (declaringDef == nullptr)
        return false;
    const Resolver::MemberLookup lookup(declaringDef, &module);
    const bool hideBasedOnSignature =
        dynamic_cast<const TS::ITypeDefinition*>(member) == nullptr &&
        member->SymbolKind() != TS::SymbolKind::Field &&
        member->SymbolKind() != TS::SymbolKind::Property &&
        member->SymbolKind() != TS::SymbolKind::Event;
    const auto* entityMethod =
        dynamic_cast<const TS::IMethod*>(member);
    const std::size_t entityTypeParameterCount =
        entityMethod != nullptr ? entityMethod->TypeParameters().size()
                                : 0;
    for (const TS::IType* baseType :
         TS::GetNonInterfaceBaseTypes(*member->DeclaringType())) {
        if (baseType->GetDefinition() == declaringDef)
            // The C# `entity.DeclaringType != t` -- the declaring type's
            // own members are the re-declaration itself, not a hide.
            continue;
        const TS::ITypeDefinition* baseDef = baseType->GetDefinition();
        if (baseDef == nullptr)
            continue;
        if (!hideBasedOnSignature) {
            // The name-based hide: a nested type or a same-name
            // non-indexer member.
            for (const TS::ITypeDefinition* nested : baseDef->NestedTypes())
                if (nested != nullptr && nested->Name() == member->Name() &&
                    lookup.IsAccessible(*nested, true))
                    return true;
            for (const TS::IField* f : baseDef->Fields())
                if (f != nullptr && f->Name() == member->Name() &&
                    lookup.IsAccessible(*f, true))
                    return true;
            for (const TS::IProperty* p : baseDef->Properties())
                if (p != nullptr && p->SymbolKind() !=
                                        TS::SymbolKind::Indexer &&
                    p->Name() == member->Name() &&
                    lookup.IsAccessible(*p, true))
                    return true;
            for (const TS::IEvent* e : baseDef->Events())
                if (e != nullptr && e->Name() == member->Name() &&
                    lookup.IsAccessible(*e, true))
                    return true;
        } else {
            // The signature-based hide: a same-name member that is not a
            // method at all, or a method whose parameter list (the count,
            // the reference kinds, and the types) and type-parameter count
            // match. The indexers, constructors, and destructors never
            // participate.
            for (const TS::IMethod* m : baseDef->Methods()) {
                if (m == nullptr || m->Name() != member->Name() ||
                    m->SymbolKind() == TS::SymbolKind::Indexer ||
                    m->SymbolKind() == TS::SymbolKind::Constructor ||
                    m->SymbolKind() == TS::SymbolKind::Destructor ||
                    !lookup.IsAccessible(*m, true))
                    continue;
                if (entityMethod == nullptr)
                    return true;
                if (m->TypeParameters().size() !=
                    entityTypeParameterCount)
                    continue;
                if (entityMethod->Parameters().size() !=
                    m->Parameters().size())
                    continue;
                bool signaturesEqual = true;
                for (std::size_t i = 0; i < m->Parameters().size(); ++i) {
                    const TS::IParameter* a = entityMethod->Parameters()[i];
                    const TS::IParameter* b = m->Parameters()[i];
                    if (a == nullptr || b == nullptr ||
                        a->ReferenceKind() != b->ReferenceKind() ||
                        a->Type().ReflectionName() !=
                            b->Type().ReflectionName()) {
                        signaturesEqual = false;
                        break;
                    }
                }
                if (signaturesEqual)
                    return true;
            }
        }
    }
    return false;
}

// The C# operator declarations (the TypeSystemAstBuilder's ConvertOperator
// name mapping): the ECMA op_* special-name methods render as the C#
// operator spellings -- the binary/unary operators as `operator <token>`
// (the return type precedes), the conversions as
// `implicit/explicit operator <ReturnType>` (the return type MOVES after
// the operator keyword). The empty string means the name is not a
// C#-expressible operator (an unsupported ECMA form like op_Box renders
// as the plain method name).
std::string OperatorToken(const std::string& metadataName) {
    static const std::map<std::string, const char*> kOperators = {
        {"op_Addition", "+"}, {"op_CheckedAddition", "+"},
        {"op_Subtraction", "-"}, {"op_CheckedSubtraction", "-"},
        {"op_Multiply", "*"}, {"op_CheckedMultiply", "*"},
        {"op_Division", "/"}, {"op_CheckedDivide", "/"},
        {"op_Modulus", "%"},
        {"op_BitwiseAnd", "&"}, {"op_BitwiseOr", "|"},
        {"op_ExclusiveOr", "^"},
        {"op_LeftShift", "<<"}, {"op_CheckedLeftShift", "<<"},
        {"op_RightShift", ">>"},
        {"op_Equality", "=="}, {"op_Inequality", "!="},
        {"op_LessThan", "<"}, {"op_GreaterThan", ">"},
        {"op_LessThanOrEqual", "<="}, {"op_GreaterThanOrEqual", ">="},
        {"op_UnaryPlus", "+"},
        {"op_UnaryNegation", "-"}, {"op_CheckedUnaryNegation", "-"},
        {"op_LogicalNot", "!"}, {"op_OnesComplement", "~"},
        {"op_Increment", "++"}, {"op_Decrement", "--"},
        {"op_True", "true"}, {"op_False", "false"},
    };
    auto it = kOperators.find(metadataName);
    return it != kOperators.end() ? it->second : std::string();
}

// The C# IntroduceUnsafeModifier's signature rule (the declaration-level
// half): a member whose signature -- the method's return type or
// parameter types, a field's type -- contains a pointer type renders
// the `unsafe` modifier. The transform's body-level uses (the pointer
// dereferences inside decompiled bodies) ride the AST pipeline; the
// reference-assembly empty-body members only have the signature.
bool TypeContainsPointer(const TS::IType* type) {
    if (type == nullptr)
        return false;
    if (dynamic_cast<const TS::PointerType*>(type) != nullptr)
        return true;
    if (const auto* array = dynamic_cast<const TS::ArrayType*>(type))
        return TypeContainsPointer(array->Element().get());
    if (const auto* byref =
            dynamic_cast<const TS::ByReferenceType*>(type))
        return TypeContainsPointer(byref->Element().get());
    if (const auto* parameterized =
            dynamic_cast<const TS::ParameterizedType*>(type)) {
        for (const TS::ITypePtr& argument :
             parameterized->TypeArguments())
            if (TypeContainsPointer(argument.get()))
                return true;
    }
    return false;
}

bool MemberSignatureHasPointer(const TS::IMember* member) {
    if (member == nullptr)
        return false;
    if (const auto* method = dynamic_cast<const TS::IMethod*>(member)) {
        if (TypeContainsPointer(&method->ReturnType()))
            return true;
        for (const TS::IParameter* parameter : method->Parameters())
            if (parameter != nullptr &&
                TypeContainsPointer(&parameter->Type()))
                return true;
        return false;
    }
    if (const auto* field = dynamic_cast<const TS::IField*>(member))
        return TypeContainsPointer(&field->Type());
    return false;
}

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
    if (MemberSignatureHasPointer(member))
        m = m | SyntaxNS::Modifiers::Unsafe;
    // The C# SetNewModifier: the hide of an accessible same-name (or
    // same-signature) base member grants the `new` modifier. The module
    // rides the member's own (the lookup's internal access consults it).
    // The C# gates the walk on the metadata's Virtual/NewSlot pair --
    // a method whose Virtual flag equals its NewSlot flag (the plain
    // methods and the new-slot virtuals); a Virtual-without-NewSlot
    // method (the first virtual declaration OR the override -- the C#
    // IMethod.IsOverride covers both shapes by its (NewSlot|Virtual|
    // Static)==Virtual definition) never takes `new`. The properties and
    // events gate on their accessor's shape; the fields and types walk
    // unconditionally.
    if (member != nullptr && member->ParentModule() != nullptr) {
        bool newModifierGate = true;
        if (const auto* methodMember =
                dynamic_cast<const TS::IMethod*>(member)) {
            newModifierGate =
                !methodMember->IsOverride() &&
                !methodMember->IsExplicitInterfaceImplementation();
        } else if (const auto* propertyMember =
                       dynamic_cast<const TS::IProperty*>(member)) {
            const TS::IMethod* accessor =
                propertyMember->Getter() != nullptr
                    ? propertyMember->Getter()
                    : propertyMember->Setter();
            newModifierGate =
                accessor != nullptr && !accessor->IsOverride() &&
                !accessor->IsExplicitInterfaceImplementation();
        } else if (const auto* eventMember =
                       dynamic_cast<const TS::IEvent*>(member)) {
            const TS::IMethod* accessor =
                eventMember->AddAccessor() != nullptr
                    ? eventMember->AddAccessor()
                    : eventMember->RemoveAccessor();
            newModifierGate =
                accessor != nullptr && !accessor->IsOverride() &&
                !accessor->IsExplicitInterfaceImplementation();
        }
        if (newModifierGate &&
            MemberHidesBaseMember(
                member,
                *dynamic_cast<const TS::MetadataModule*>(
                     member->ParentModule())))
            m = m | SyntaxNS::Modifiers::New;
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
// The boxed constant's literal spelling (the C# ConvertConstantValue
// over the TextWriterTokenWriter literal forms): the integer family with
// the C# suffixes, bool, the quoted string, the char, the float/double
// forms, and the reference-type null (the empty any is the null
// constant, not a decode failure).
std::string ConstantValueText(const std::any& value,
                              const TS::IType& type) {
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
        if (auto* f = std::any_cast<float>(&value)) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%g", *f);
            return std::string(buffer) + "f";
        }
        if (auto* d = std::any_cast<double>(&value)) {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%g", *d);
            std::string text = buffer;
            // The C# double literal always carries the decimal point
            // (`5.0`, not `5` -- the PrimitiveExpression's double
            // format).
            if (text.find('.') == std::string::npos &&
                text.find('e') == std::string::npos &&
                text.find("inf") == std::string::npos &&
                text.find("nan") == std::string::npos)
                text += ".0";
            return text;
        }
        if (auto* str = std::any_cast<std::string>(&value))
            return "\"" + *str + "\"";
        if (auto* ch = std::any_cast<char16_t>(&value)) {
            // The C# char literal's escaped forms (the TextWriter's
            // EscapeString): the backslash and the quote render their
            // escape sequences.
            char c = static_cast<char>(*ch);
            if (c == '\\')
                return "'\\\\'";
            if (c == '\'')
                return "'\\''";
            return std::string("'") + c + "'";
        }
    } else if (type.IsReferenceType()) {
        return "null";
    }
    return std::string();
}

std::string ConstantFieldLiteral(const TS::IField& field) {
    std::any value;
    try {
        value = field.GetConstantValue();
    } catch (const std::exception&) {
        return std::string();
    }
    return ConstantValueText(value, field.Type());
}

// The C# MemberIsHidden's state machine arms (the whole-type skip): the
// compiler-generated state machine types the de-sugar replaces do not
// render when their transforms are enabled (the C# gates consult
// DecompilerSettings; the pipeline context's defaults carry the same
// on-by-default gates).
bool TypeIsHiddenFromRender(const Metadata::MetadataFile& file,
                            std::uint32_t typeToken) {
    const IL::ILTransformSettings transformSettings;
    if ((transformSettings.YieldReturn &&
         Metadata::IsCompilerGeneratorEnumerator(file, typeToken)) ||
        (transformSettings.AsyncAwait &&
         Metadata::IsCompilerGeneratedStateMachine(file, typeToken)))
        return true;
    // The C# MemberIsHidden's top-level arm: a compiler-generated type
    // named <PrivateImplementationDetails>... (the array-initializer
    // backing: the __StaticArrayInitTypeSize structs and the hash-named
    // data fields) is hidden under ArrayInitializers (on by default) --
    // the array initializers render inline in the field declarations
    // instead.
    for (const auto& t : file.TypeDefs()) {
        if (t.Token != typeToken)
            continue;
        return std::string(t.Name).rfind(
                   "<PrivateImplementationDetails>", 0) == 0 &&
               t.Namespace.empty() &&
               Metadata::HasKnownAttribute(
                   file, typeToken,
                   TS::KnownAttribute::CompilerGenerated);
    }
    return false;
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
// name position. Null when the definition is not a keyword type. The
// native integers stay OFF: only the ELEMENT_TYPE_I/U signature forms
// render nint/nuint; a plain System.IntPtr type reference (the net48
// corpus's fields) renders the full name.
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
        {"System.Object", "object"}, {"System.Void", "void"},
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
// The C# constraint clauses (`where T : class, IFoo, new()`) for a
// member's or type's OWN type-parameter slice: no clause when the
// parameter has no special, nullability, or non-Object/ValueType
// constraint (the C# skip rule). The caller slices the chain-merged
// TypeParameters surface at its own start (the outer-parameter-skip
// precedent: a type's declaring chain, a method's declaring type).
std::string ConstraintClausesText(
    const std::vector<const TS::ITypeParameter*>& typeParameters,
    std::size_t startIndex, const Resolver::CSharpResolver* resolver) {
    std::string out;
    for (std::size_t i = startIndex; i < typeParameters.size(); ++i) {
        const TS::ITypeParameter* tp = typeParameters[i];
        if (tp == nullptr)
            continue;
        bool hasTypeConstraint = false;
        for (const TS::TypeConstraint& tc : tp->TypeConstraints()) {
            if (tc.Type() == nullptr)
                continue;
            const bool objectOrValueType =
                TS::IsKnownType(*tc.Type(),
                                TS::KnownTypeCode::Object) ||
                TS::IsKnownType(*tc.Type(),
                                TS::KnownTypeCode::ValueType);
            if (!objectOrValueType || !tc.Attributes().empty()) {
                hasTypeConstraint = true;
                break;
            }
        }
        if (!tp->HasDefaultConstructorConstraint() &&
            !tp->HasReferenceTypeConstraint() &&
            !tp->HasValueTypeConstraint() &&
            !tp->AllowsRefLikeType() &&
            tp->NullabilityConstraint() != TS::Nullability::NotNullable &&
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
                tp->NullabilityConstraint() == TS::Nullability::Nullable
                    ? "class?"
                    : "class");
        } else if (tp->HasValueTypeConstraint()) {
            appendConstraint(
                tp->HasUnmanagedConstraint() ? "unmanaged" : "struct");
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
            appendConstraint(RenderBaseTypeName(tc.Type()->GetDefinition(),
                                               tc.Type(), resolver));
        }
        if (tp->HasDefaultConstructorConstraint() &&
            !tp->HasValueTypeConstraint())
            appendConstraint("new()");
        if (tp->AllowsRefLikeType())
            appendConstraint("allows ref struct");
    }
    return out;
}

// The C# explicit-implementation name's interface type: the rendered
// name carries the class's own instantiation --
// `IEnumerator<XmlNamespaceMapping>`, not the bare definition. The
// MethodImpl-resolved declaring type already IS the constructed form in
// the inherited-interface shapes (`ICollection<Uri>`); the
// definition-level form needs the class's own direct-base-type entry
// (the metadata's substituted interface reference). A recursive walk
// through the base types would find the DEFINITION's own unsubstituted
// base (`IEnumerable<T>` from IList`1), so only the declaring type's
// direct list is scanned; anything else keeps the resolved declaring
// type.
TS::ITypePtr InterfaceInstantiationFor(
    const TS::ITypeDefinition* declaringType,
    const TS::ITypeDefinition* interfaceDef,
    const TS::ITypePtr& resolvedDeclaringType) {
    if (interfaceDef == nullptr)
        return nullptr;
    if (dynamic_cast<const TS::ParameterizedType*>(
            resolvedDeclaringType.get()) != nullptr)
        return resolvedDeclaringType;
    if (declaringType == nullptr)
        return nullptr;
    for (const TS::ITypePtr& base : declaringType->DirectBaseTypes()) {
        if (base != nullptr && base->GetDefinition() == interfaceDef)
            return base;
    }
    return nullptr;
}

std::string RenderBaseTypeName(const TS::ITypeDefinition* typeDef,
                               const TS::ITypePtr& instantiation,
                               const Resolver::CSharpResolver* resolver) {
    // The C# nullable shorthand: System.Nullable<T> renders `T?` (the
    // AstBuilder's ComposedType with the nullable specifier; T is a
    // value type by the definition's struct constraint).
    if (typeDef != nullptr &&
        typeDef->KnownTypeCode() == TS::KnownTypeCode::NullableOfT) {
        const auto* nullableParameterized =
            dynamic_cast<const TS::ParameterizedType*>(
                instantiation.get());
        if (nullableParameterized != nullptr &&
            nullableParameterized->TypeArguments().size() == 1) {
            const TS::ITypePtr& argument =
                nullableParameterized->TypeArguments()[0];
            return RenderBaseTypeName(argument->GetDefinition(), argument,
                                      resolver) +
                   "?";
        }
    }
    if (typeDef == nullptr && instantiation != nullptr) {
        // A type-parameter argument renders its declared name (the C#
        // MakeSimpleType over the parameter's Name); other non-definition
        // types (arrays, pointers) keep the short renderer.
        if (const auto* typeParameter =
                dynamic_cast<const TS::ITypeParameter*>(
                    instantiation.get()))
            return typeParameter->Name();
        // The composite forms recurse on the element through the same
        // name decision (the C# converts the element type and wraps it
        // in the pointer/array markup): a sibling nested type inside a
        // pointer renders `FSPOINT*`, not the reflection chain. The
        // by-reference form renders the element bare (the parameter's
        // ref/out modifier already carries the reference kind).
        if (const auto* pointer =
                dynamic_cast<const TS::PointerType*>(
                    instantiation.get()))
            return RenderBaseTypeName(
                       pointer->Element()->GetDefinition(),
                       pointer->Element(), resolver) +
                   "*";
        if (const auto* byReference =
                dynamic_cast<const TS::ByReferenceType*>(
                    instantiation.get()))
            return RenderBaseTypeName(
                byReference->Element()->GetDefinition(),
                byReference->Element(), resolver);
        if (const auto* array =
                dynamic_cast<const TS::ArrayType*>(instantiation.get()))
            return RenderBaseTypeName(array->Element()->GetDefinition(),
                                      array->Element(), resolver) +
                   (array->IsSzArray() ? "[]" : "[,]");
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
            file, typeSystem, accessorToken, rva, accessorName, text)) {
        // The C# DecompileMethod's body-decode failure over an accessor:
        // the block with the reference-assembly empty-body comment (a
        // reference assembly's stale RVA never decodes, and the property
        // falls back to the stub form when the failure is swallowed
        // here).
        return "/*Error: Empty body found. Decompiled assembly might be "
               "a reference assembly.*/;\n";
    }
    std::size_t open = text.find("{\n");
    std::size_t close = text.rfind("\n}");
    if (open == std::string::npos || close == std::string::npos ||
        close <= open)
        return std::string();
    // An EMPTY body's braces touch (`{\n}` -- the open brace's newline is
    // the close brace's newline), so the span start can sit past the
    // close; the count clamps to zero for that shape.
    std::size_t bodyLength = close > open + 2 ? close - (open + 2) : 0;
    std::string body = text.substr(open + 2, bodyLength);
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

// The C# ConvertParameter's attribute half (the TypeSystemAstBuilder):
// the parameter's attributes render as the compact bracket-adjacent
// sections -- the parameter placement joins them with no space
// (`[In][MarshalAs(UnmanagedType.Bool)]`), unlike the member's leading
// lines. The empty string when the parameter carries no attributes.
std::string ParameterAttributesText(const TS::IParameter* parameter) {
    if (parameter == nullptr)
        return std::string();
    std::vector<const TS::IAttribute*> attributes = parameter->GetAttributes();
    if (attributes.empty())
        return std::string();
    OutputVisitor::CSharpFormattingOptions options =
        SettingsFormattingOptions();
    SyntaxNS::TypeSystemAstBuilder builder;
    builder.ShowAttributes() = true;
    builder.AlwaysUseShortTypeNames() = true;
    std::string out;
    for (const TS::IAttribute* a : attributes) {
        if (a == nullptr)
            continue;
        Syntax::AttributeSection section(builder.ConvertAttribute(*a));
        std::string text = section.ToString(&options);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'
                                 || text.back() == ' '))
            text.pop_back();
        if (!text.empty())
            out += text;
    }
    return out;


}

} // namespace

// The render body the static and instance DecompileTypeToString entries
// share: everything but the module wiring (the static entry builds it per
// call; the instance uses its own) and the registry lookup (the static
// consults the process-global placeholder; the instance its own map).
// The C# DoDecompileType worklist (EnqueueReferencedMembers' nested-type
// half): the type's rendered members' attribute typeof arguments reference
// nested types -- a hidden compiler-generated state machine whose
// declaring type's rendered members still name it (the state-machine
// attribute a failed de-sugar leaves in place) renders despite the hidden
// predicate. The member half (the members of the current type the bodies
// reference) has no flat-renderer surface yet; the attribute references
// cover the corpus shapes.
std::set<std::uint32_t> AttributeReferencedNestedTypes(
    const Metadata::MetadataFile& file,
    TS::DecompilerTypeSystem* typeSystem,
    const TS::MetadataModule& module, std::uint32_t typeToken) {
    std::set<std::uint32_t> result;
    const TS::ITypeDefinition* currentType = module.GetDefinition(typeToken);
    if (currentType == nullptr)
        return result;
    // The method entities (the de-sugar outcome consultation needs the
    // tokens + RVAs alongside).
    std::vector<std::pair<const TS::IMethod*, std::uint32_t>> methods;
    for (const auto& m : module.MetadataFile()->GetMethods(typeToken)) {
        const TS::IMethod* method =
            const_cast<TS::MetadataModule&>(module).GetDefinitionMethod(
                m.Token);
        if (method != nullptr)
            methods.emplace_back(method, m.RVA);
    }
    std::vector<const TS::IEntity*> entities;
    for (const auto& methodPair : methods)
        entities.push_back(methodPair.first);
    for (const auto& f : module.MetadataFile()->GetFields(typeToken)) {
        const TS::IField* field =
            const_cast<TS::MetadataModule&>(module).GetDefinitionField(
                f.Token);
        if (field != nullptr)
            entities.push_back(field);
    }
    for (const auto& p : module.MetadataFile()->GetProperties(typeToken)) {
        const TS::IProperty* property =
            const_cast<TS::MetadataModule&>(module).GetDefinitionProperty(
                p.Token);
        if (property != nullptr)
            entities.push_back(property);
    }
    for (const auto& e : module.MetadataFile()->GetEvents(typeToken)) {
        const TS::IEvent* event =
            const_cast<TS::MetadataModule&>(module).GetDefinitionEvent(
                e.Token);
        if (event != nullptr)
            entities.push_back(event);
    }
    for (const TS::IEntity* entity : entities) {
        for (const TS::IAttribute* attribute : entity->GetAttributes()) {
            if (attribute == nullptr)
                continue;
            // The C# EnqueueReferencedMembers scans the RENDERED
            // declaration -- the de-sugar's attribute removal already
            // dropped the state-machine attribute when it succeeded, so
            // the reference is gone. The metadata walk mirrors that: a
            // state-machine attribute counts only when its de-sugar did
            // not succeed (the outcome consults the method pipeline once;
            // the filter keeps this to the state-machine-attributed
            // methods only).
            std::string attributeName =
                attribute->AttributeType().ReflectionName();
            bool asyncDecompiled = false;
            bool iteratorDecompiled = false;
            bool isStateMachineAttribute =
                attributeName ==
                    "System.Runtime.CompilerServices."
                    "AsyncStateMachineAttribute" ||
                attributeName ==
                    "System.Runtime.CompilerServices."
                    "IteratorStateMachineAttribute" ||
                attributeName ==
                    "System.Runtime.CompilerServices."
                    "AsyncIteratorStateMachineAttribute";
            if (isStateMachineAttribute) {
                bool found = false;
                for (const auto& methodPair : methods) {
                    if (methodPair.first != entity)
                        continue;
                    found = true;
                    if (methodPair.second != 0) {
                        std::string discarded;
                        CSharpDecompiler::DecompileMethodToString(
                            file, typeSystem, methodPair.first->MetadataToken(),
                            methodPair.second, "", discarded, false,
                            &asyncDecompiled, &iteratorDecompiled);
                    }
                    break;
                }
                if (found && asyncDecompiled &&
                    attributeName ==
                        "System.Runtime.CompilerServices."
                        "AsyncStateMachineAttribute")
                    continue;
                if (found && iteratorDecompiled &&
                    (attributeName ==
                         "System.Runtime.CompilerServices."
                         "IteratorStateMachineAttribute" ||
                     attributeName ==
                         "System.Runtime.CompilerServices."
                         "AsyncIteratorStateMachineAttribute"))
                    continue;
            }
            std::vector<TS::CustomAttributeTypedArgument> fixedArguments =
                attribute->FixedArguments();
            for (std::size_t argumentIndex = 0;
                 argumentIndex < fixedArguments.size(); ++argumentIndex) {
                std::any argumentValue = fixedArguments[argumentIndex].Value();
                const auto* typeofType =
                    std::any_cast<TS::ITypePtr>(&argumentValue);
                if (typeofType == nullptr || *typeofType == nullptr)
                    continue;
                const TS::ITypeDefinition* definition =
                    (*typeofType)->GetDefinition();
                if (definition == nullptr ||
                    definition->DeclaringTypeDefinition() != currentType)
                    continue;
                // Only the CURRENT type's own nested types (the C#
                // worklist condition); the nested-type position carries
                // the render.
                auto info = module.MetadataFile()->GetTypeDefNameInfo(
                    definition->MetadataToken());
                if (info.has_value() && info->DeclaringTypeToken == typeToken)
                    result.insert(definition->MetadataToken());
            }
        }
    }
    return result;
}

// The C# IdStringSignatureTypeProvider's doc type-name forms: the
// primitives' full names (the ReflectionName carries them), the dotted
// declaring chains (the '+' separators swap for '.'), the byref '@' and
// pointer '*' suffixes, the sz-array '[]' and the multidimensional
// zero-based dimension list, the generic-parameter markers ('`N' for a
// type's, '``N' for a method's), and the generic instantiation's
// brace-distributed arguments over the arity markers.
std::string DocTypeName(const TS::ITypePtr& type) {
    if (type == nullptr)
        return std::string();
    if (const auto* array = dynamic_cast<const TS::ArrayType*>(type.get())) {
        if (array->Element() == nullptr)
            return std::string("?");
        std::string element = DocTypeName(array->Element());
        if (array->IsSzArray())
            return element + "[]";
        std::string out = element + "[";
        for (int i = 1; i < array->Rank(); ++i)
            out += ",0:";
        return out + "]";
    }
    if (const auto* byReference =
            dynamic_cast<const TS::ByReferenceType*>(type.get())) {
        return byReference->Element() != nullptr
                   ? DocTypeName(byReference->Element()) + "@"
                   : std::string("?");
    }
    if (const auto* pointer =
            dynamic_cast<const TS::PointerType*>(type.get())) {
        return pointer->Element() != nullptr
                   ? DocTypeName(pointer->Element()) + "*"
                   : std::string("?");
    }
    if (const auto* typeParameter =
            dynamic_cast<const TS::ITypeParameter*>(type.get())) {
        const bool methodParameter =
            typeParameter->OwnerType() == TS::SymbolKind::Method;
        return (methodParameter ? std::string("``") : std::string("`")) +
               std::to_string(typeParameter->Index());
    }
    if (const auto* parameterized =
            dynamic_cast<const TS::ParameterizedType*>(type.get())) {
        std::vector<std::string> typeArguments;
        for (const TS::ITypePtr& argument : parameterized->TypeArguments())
            typeArguments.push_back(DocTypeName(argument));
        // The C# GetGenericInstantiation: the arguments distribute over
        // the generic name's `k arity markers (the nesting levels
        // consume them outermost-first); a marker with too few
        // remaining arguments keeps its verbatim spelling.
        const std::string genericType = DocTypeName(parameterized->GenericType());
        std::string out;
        std::size_t nextArgument = 0;
        std::size_t i = 0;
        while (i < genericType.size()) {
            char c = genericType[i];
            if (c != '`' || i + 1 >= genericType.size() ||
                !isdigit(static_cast<unsigned char>(genericType[i + 1]))) {
                out += c;
                i++;
                continue;
            }
            std::size_t markerEnd = i + 1;
            int arity = 0;
            while (markerEnd < genericType.size() &&
                   isdigit(static_cast<unsigned char>(genericType[markerEnd]))) {
                arity = arity * 10 +
                        (genericType[markerEnd] - '0');
                markerEnd++;
            }
            if (static_cast<std::size_t>(arity) >
                typeArguments.size() - nextArgument) {
                out += genericType.substr(i, markerEnd - i);
            } else {
                out += '{';
                for (int k = 0; k < arity; k++) {
                    if (k != 0)
                        out += ',';
                    out += typeArguments[nextArgument++];
                }
                out += '}';
            }
            i = markerEnd;
        }
        if (nextArgument < typeArguments.size()) {
            out += '{';
            for (std::size_t k = nextArgument; k < typeArguments.size(); k++) {
                if (k != nextArgument)
                    out += ',';
                out += typeArguments[k];
            }
            out += '}';
        }
        return out;
    }
    // The definitions/refs: the dotted declaring chain (the
    // ReflectionName's '+' separators) with the arity suffix kept. The
    // native integers resolve with the alias spellings (the port's
    // KnownType renders `nint`/`nuint`); the doc IDs spell the full
    // names (the C# PrimitiveTypeCode table).
    std::string name = type->ReflectionName();
    if (name == "nint")
        name = "System.IntPtr";
    else if (name == "nuint")
        name = "System.UIntPtr";
    std::replace(name.begin(), name.end(), '+', '.');
    return name;
}

// The C# AppendMethodIdString / AppendPropertyIdString /
// AppendFieldIdString: the declaring type's doc name + the escaped
// member name (the explicit-implementation dots -> '#', the angle
// brackets -> the braces) + the method generic count + the parameter
// list + the conversion operator's return type.
std::string EscapedMemberDocName(const std::string& name) {
    std::string out;
    for (char c : name) {
        if (c == '.')
            out += '#';
        else if (c == '<')
            out += '{';
        else if (c == '>')
            out += '}';
        else
            out += c;
    }
    return out;
}

std::string ParameterListDocText(
    const std::vector<const TS::IParameter*>& parameters) {
    std::string out;
    if (!parameters.empty()) {
        out += '(';
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (i != 0)
                out += ',';
            if (parameters[i] != nullptr) {
                TS::ITypePtr type(
                    const_cast<TS::IType*>(&parameters[i]->Type()),
                    [](TS::IType*) {});
                out += DocTypeName(type);
            }
        }
        out += ')';
    }
    return out;
}

std::string TypeDocName(const Metadata::MetadataFile& file,
                        std::uint32_t typeToken) {
    std::vector<std::string> names;
    std::uint32_t current = typeToken;
    std::string rootNamespace;
    while (true) {
        auto info = file.GetTypeDefNameInfo(current);
        if (!info.has_value())
            return std::string();
        names.push_back(info->Name);
        if (info->DeclaringTypeToken == 0) {
            rootNamespace = info->Namespace;
            break;
        }
        current = info->DeclaringTypeToken;
    }
    std::reverse(names.begin(), names.end());
    std::string id;
    if (!rootNamespace.empty())
        id = rootNamespace + ".";
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (i != 0)
            id += '.';
        id += names[i];
    }
    return id;
}

// The C# IdStringProvider's type-documentation ID: `T:` + the dotted
// declaring chain (the nested names joined by '.', not '+') with the
// root's namespace prefix; the metadata name keeps the generic arity
// suffix (the doc IDs spell `List`1`).
std::string TypeDocId(const Metadata::MetadataFile& file,
                      std::uint32_t typeToken) {
    return "T:" + TypeDocName(file, typeToken);
}

// The C# AddXmlDocumentationTransform's InsertXmlDocumentation lifted to
// the flat renderer: the first non-empty line's indentation strips from
// every line, the trailing empty lines drop, and the empty lines between
// render as bare `///` (the C# Comment(string.Empty,
// CommentType.Documentation) -- no trailing space).
std::string DocumentationCommentLines(const std::string& documentation) {
    std::string out;
    // The line splitter (the C# StringReader.ReadLine: \r, \n, or
    // \r\n endings).
    std::vector<std::string> lines;
    std::size_t position = 0;
    while (position < documentation.size()) {
        std::size_t end = position;
        while (end < documentation.size() && documentation[end] != '\r' &&
               documentation[end] != '\n')
            end++;
        lines.push_back(documentation.substr(position, end - position));
        if (end < documentation.size()) {
            if (documentation[end] == '\r' && end + 1 < documentation.size() &&
                documentation[end + 1] == '\n')
                end += 2;
            else
                end += 1;
        }
        position = end;
    }
    // The first non-empty line's indentation.
    std::size_t firstLine = lines.size();
    for (std::size_t i = 0; i < lines.size(); ++i) {
        bool whitespaceOnly = true;
        for (char c : lines[i]) {
            if (c != ' ' && c != '\t' && c != '\r') {
                whitespaceOnly = false;
                break;
            }
        }
        if (!whitespaceOnly) {
            firstLine = i;
            break;
        }
    }
    if (firstLine == lines.size())
        return std::string();
    std::string indentation;
    for (char c : lines[firstLine]) {
        if (c != ' ' && c != '\t')
            break;
        indentation += c;
    }
    // Copy to the end except the trailing whitespace-only lines; the
    // empty lines between render as bare `///`.
    std::size_t lastLine = lines.size();
    while (lastLine > firstLine) {
        bool whitespaceOnly = true;
        for (char c : lines[lastLine - 1]) {
            if (c != ' ' && c != '\t' && c != '\r') {
                whitespaceOnly = false;
                break;
            }
        }
        if (!whitespaceOnly)
            break;
        lastLine--;
    }
    for (std::size_t i = firstLine; i < lastLine; ++i) {
        const std::string& line = lines[i];
        bool whitespaceOnly = true;
        for (char c : line) {
            if (c != ' ' && c != '\t' && c != '\r') {
                whitespaceOnly = false;
                break;
            }
        }
        if (whitespaceOnly) {
            out += "///\n";
        } else {
            // The C# conditional strip: a line that does not start with
            // the documentation indentation keeps its full spelling (the
            // wrapped continuations with less indentation).
            bool startsWithIndentation =
                line.compare(0, indentation.size(), indentation) == 0;
            std::string content =
                startsWithIndentation ? line.substr(indentation.size())
                                      : line;
            out += "/// " + content + "\n";
        }
    }
    return out;
}

bool DecompileTypeToStringBody(
    const Metadata::MetadataFile& file, TS::DecompilerTypeSystem* typeSystem,
    TS::MetadataModule& module,
    const std::function<const Metadata::PartialTypeInfo*(std::uint32_t)>&
        partialLookup,
    const std::vector<std::string>* usingNamespaces,
    std::uint32_t typeToken, std::string& out) {
    // The adjacent-.xml documentation provider (the C#
    // CreateDefaultDocumentationProvider over XmlDocLoader): null when
    // no file sits beside the module.
    std::shared_ptr<Documentation::XmlDocumentationProvider>
        documentationProvider =
            Documentation::XmlDocumentationProvider::LoadBeside(
                file.FileName());
    const Metadata::PartialTypeInfo* partialType = partialLookup(typeToken);
    // The C# DecompileType member iteration: the partial-type info gates
    // the members (the C# `DoDecompileMember`'s
    // `partialType.IsDeclaredMember(entity) -> return` skip, and the
    // `partial` modifier note -- the C# renders `partial` on the type
    // declaration; the port's ILAstToCSharp method-text renderer carries
    // no type-header surface, so the modifier is deferred with it).
    bool rendered = false;
    // The render's using-scope resolver (the C#
    // FullyQualifyAmbiguousTypeNamesVisitor's ctor threading): built once
    // per type; every name decision in the declaration -- the base list,
    // the constraint clauses, and every member-signature type -- consults
    // it. Null when no using set was passed (no scope to resolve).
    std::shared_ptr<Resolver::CSharpResolver> scopeResolver;
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
        // The C# AddXmlDocumentationTransform over the type declaration:
        // the adjacent-.xml documentation lines lead the declaration.
        if (documentationProvider != nullptr) {
            std::string documentation = documentationProvider->GetDocumentation(
                TypeDocId(file, typeToken));
            if (!documentation.empty())
                out += DocumentationCommentLines(documentation);
        }
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
        scopeResolver =
            usingNamespaces != nullptr && typeDef != nullptr
                ? RenderScopeResolver(module, typeDef, *usingNamespaces)
                : nullptr;
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
        // The C# ConvertDelegate: `modifiers & ~Sealed` (the metadata
        // marks delegates sealed) -- before the modifier emission.
        if (typeDef != nullptr && TS::GetDelegateInvokeMethod(*typeDef) != nullptr) {
            typeModifiers = typeModifiers &
                            ~SyntaxNS::Modifiers::Sealed;
            // The C# IntroduceUnsafeModifier's delegate rule: a pointer
            // in the Invoke signature (the return or the parameters)
            // grants the delegate type the `unsafe` modifier.
            if (MemberSignatureHasPointer(
                    TS::GetDelegateInvokeMethod(*typeDef)))
                typeModifiers =
                    typeModifiers | SyntaxNS::Modifiers::Unsafe;
        }
        out += MemberAttributesText(typeDef);
        for (SyntaxNS::Modifiers modifier :
             SyntaxNS::CSharpModifiers::AllModifiers) {
            if (modifier == SyntaxNS::Modifiers::Any)
                continue;
            if ((typeModifiers & modifier) == modifier)
                out += SyntaxNS::CSharpModifiers::GetModifierName(modifier) +
                       std::string(" ");
        }
        // The C# ConvertTypeDefinition's Delegate arm: a delegate type
        // renders the Invoke signature as `delegate Ret Name(params);`
        // -- never the class shape or the runtime members (the .ctor,
        // Invoke, BeginInvoke, EndInvoke). The sealed modifier strips
        // (the metadata marks delegates sealed).
        const TS::IMethod* delegateInvoke =
            typeDef != nullptr ? TS::GetDelegateInvokeMethod(*typeDef)
                               : nullptr;
        if (delegateInvoke != nullptr) {
            out += "delegate ";
            TS::ITypePtr delegateReturnType(
                const_cast<TS::IType*>(&delegateInvoke->ReturnType()),
                [](TS::IType*) {});
            out += RenderBaseTypeName(
                delegateReturnType->GetDefinition(), delegateReturnType,
                scopeResolver.get());
            out += ' ';
            out += bareName;
        } else {
            out += keyword;
            out += ' ';
            out += bareName;
        }
        // The type-parameter list (the C# TypeDeclaration's
        // TypeParameters): the type's OWN parameters only -- the C#
        // skips the declaring type's count. The port's TypeParameters
        // surface is the chain-merged list (the declaring type's
        // parameters first, then the own), so the own slice starts at
        // the declaring type's count; the compiler-generated state
        // machines (which reference the enclosing generic's parameters
        // without re-declaring any of their own) render no list at all.
        if (typeDef != nullptr && typeDef->TypeParameterCount() > 0) {
            std::size_t outerTypeParameterCount =
                typeDef->DeclaringTypeDefinition() != nullptr
                    ? static_cast<std::size_t>(typeDef
                                                   ->DeclaringTypeDefinition()
                                                   ->TypeParameterCount())
                    : 0;
            const std::vector<const TS::ITypeParameter*>& typeParameters =
                typeDef->TypeParameters();
            if (typeParameters.size() > outerTypeParameterCount) {
                out += '<';
                for (std::size_t i = outerTypeParameterCount;
                     i < typeParameters.size(); ++i) {
                    if (i != outerTypeParameterCount)
                        out += ", ";
                    out += typeParameters[i] != nullptr
                               ? typeParameters[i]->Name()
                               : std::string("?");
                }
                out += '>';
            }
        }
        if (delegateInvoke != nullptr) {
            // The delegate's parameter list (the Invoke signature's
            // parameters) + the closing semicolon; the runtime members
            // never render and no base list applies (the early return
            // skips the member arms and the closing brace).
            std::vector<const TS::IParameter*> parameters =
                delegateInvoke->Parameters();
            auto paramNames =
                file.GetParameterNames(delegateInvoke->MetadataToken());
            out += "(" +
                   CSharpDecompiler::MethodDeclString(
                       parameters, true, paramNames, scopeResolver.get()) +
                   ");\n";
            return true;
        }
        if (typeDef != nullptr) {
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
    // inside the declaring type's braces). The hidden types skip UNLESS
    // the worklist still references them (the C# EnqueueReferencedMembers
    // re-enqueue -- "the compiler-generated members that are still
    // needed").
    std::set<std::uint32_t> worklistTypes =
        AttributeReferencedNestedTypes(file, typeSystem, module, typeToken);
    for (std::uint32_t nestedToken : file.GetNestedTypes(typeToken)) {
        // The C# DoDecompileType worklist: a hidden type the rendered
        // members still reference (the attribute typeof collection above)
        // renders at its nested position -- "the compiler-generated
        // members that are still needed".
        if (TypeIsHiddenFromRender(file, nestedToken) &&
            worklistTypes.count(nestedToken) == 0)
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
        const TS::IProperty* propertyEntity =
            module.GetDefinitionProperty(p.Token);
        // The member documentation: the P: ID form (the index parameters
        // ride the parameter list).
        if (documentationProvider != nullptr && propertyEntity != nullptr &&
            propertyEntity->DeclaringTypeDefinition() != nullptr) {
            std::string propertyId =
                "P:" + TypeDocName(file, propertyEntity->DeclaringTypeDefinition()
                                              ->MetadataToken()) +
                "." + EscapedMemberDocName(p.Name) +
                ParameterListDocText(propertyEntity->Parameters());
            std::string documentation =
                documentationProvider->GetDocumentation(propertyId);
            if (!documentation.empty())
                out += DocumentationCommentLines(documentation);
        }
        std::string propertyTypeName = "var";
        if (propertyEntity != nullptr) {
            // The entity's resolved return type carries the definition
            // the name decision needs (the file-signature decode yields
            // the unresolved simple types).
            TS::ITypePtr propertyType(
                const_cast<TS::IType*>(&propertyEntity->ReturnType()),
                [](TS::IType*) {});
            propertyTypeName = RenderBaseTypeName(
                propertyType->GetDefinition(), propertyType,
                scopeResolver.get());
        } else if (accessors.GetterToken != 0) {
            if (auto sig = file.GetMethodSignature(accessors.GetterToken)) {
                if (sig->ReturnType)
                    propertyTypeName =
                        RenderBaseTypeName(sig->ReturnType->GetDefinition(),
                                           sig->ReturnType,
                                           scopeResolver.get());
            }
        } else if (accessors.SetterToken != 0) {
            if (auto sig = file.GetMethodSignature(accessors.SetterToken)) {
                if (!sig->ParameterTypes.empty() && sig->ParameterTypes[0])
                    propertyTypeName = RenderBaseTypeName(
                        sig->ParameterTypes[0]->GetDefinition(),
                        sig->ParameterTypes[0], scopeResolver.get());
            }
        }
        // The C# ConvertProperty's explicit-implementation indexer: an
        // explicit interface implementation of Item names
        // `Interface.this[...]` and carries NO modifiers (the interface
        // qualification replaces them).
        bool indexerIsExplicitImplementation =
            propertyEntity != nullptr &&
            propertyEntity->IsExplicitInterfaceImplementation() &&
            p.Name.find('.') != std::string::npos;
        out += MemberAttributesText(propertyEntity);
        out += indexerIsExplicitImplementation
                   ? std::string()
                   : MemberModifiersText(propertyEntity);
        out += propertyTypeName;
        out += ' ';
        // The C# ConvertProperty's indexer arm: a property with index
        // parameters names `this[<parameters>]`, never the metadata name
        // (Item).
        std::vector<const TS::IParameter*> indexParameters;
        if (propertyEntity != nullptr)
            indexParameters = propertyEntity->Parameters();
        if (!indexParameters.empty()) {
            std::vector<std::string> indexNames;
            for (const TS::IParameter* indexParameter : indexParameters)
                indexNames.push_back(indexParameter != nullptr
                                         ? indexParameter->Name()
                                         : std::string());
            if (indexerIsExplicitImplementation) {
                // The same GetExplicitInterfaceType rewrite the plain
                // property arm applies: the first implemented member's
                // declaring type, instantiated when the class's own base
                // list carries the parameterized form.
                std::vector<const TS::IMember*> implemented =
                    propertyEntity->ExplicitlyImplementedInterfaceMembers();
                if (!implemented.empty() && implemented[0] != nullptr &&
                    implemented[0]->DeclaringType() != nullptr) {
                    const TS::ITypePtr& interfaceType =
                        implemented[0]->DeclaringType();
                    TS::ITypePtr instantiation = InterfaceInstantiationFor(
                        propertyEntity->DeclaringTypeDefinition(),
                        interfaceType != nullptr
                            ? interfaceType->GetDefinition()
                            : nullptr,
                        interfaceType);
                    if (instantiation == nullptr)
                        instantiation = interfaceType;
                    out += RenderBaseTypeName(
                        instantiation->GetDefinition(), instantiation,
                        scopeResolver.get());
                    out += '.';
                }
            }
            out += "this[";
            out += CSharpDecompiler::MethodDeclString(
                indexParameters, true, indexNames, scopeResolver.get());
            out += ']';
        } else if (propertyEntity != nullptr &&
                   p.Name.find('.') != std::string::npos &&
                   !propertyEntity->IsExplicitInterfaceImplementation()) {
            // A dotted metadata name on a NON-explicit property (the
            // indexer-with-dots shape) keeps the metadata name.
            out += p.Name;
        } else if (propertyEntity != nullptr &&
                   propertyEntity->IsExplicitInterfaceImplementation() &&
                   p.Name.find('.') != std::string::npos) {
            // The C# GetExplicitInterfaceType: the name's last segment
            // qualified by the first implemented member's declaring type
            // through the name decision.
            std::string propertyName =
                p.Name.substr(p.Name.find_last_of('.') + 1);
            std::string interfaceName;
            std::vector<const TS::IMember*> implemented =
                propertyEntity->ExplicitlyImplementedInterfaceMembers();
            if (!implemented.empty() && implemented[0] != nullptr &&
                implemented[0]->DeclaringType() != nullptr) {
                const TS::ITypePtr& interfaceType =
                    implemented[0]->DeclaringType();
                TS::ITypePtr instantiation = InterfaceInstantiationFor(
                    propertyEntity->DeclaringTypeDefinition(),
                    interfaceType != nullptr
                        ? interfaceType->GetDefinition()
                        : nullptr,
                    interfaceType);
                if (instantiation == nullptr)
                    instantiation = interfaceType;
                interfaceName = RenderBaseTypeName(
                    instantiation->GetDefinition(), instantiation,
                    scopeResolver.get());
            }
            if (interfaceName.empty())
                out += p.Name;
            else
                out += interfaceName + "." + propertyName;
        } else {
            out += p.Name;
        }
        // The accessor forms (the C# TransformAutomaticProperty's decision
        // + ConvertAccessor): the stub form (`get; set;`) when the property
        // has its compiler-generated `<Name>k__BackingField` field (only
        // the compiler emits the angle-bracket names) or its accessors
        // carry no bodies (an interface member); a real accessor body
        // renders as its block.
        bool anyAccessor = false;
        std::string getterBody, setterBody;
        getterBody = AccessorBodyText(file, typeSystem,
                                      accessors.GetterToken, "get");
        setterBody = AccessorBodyText(file, typeSystem,
                                      accessors.SetterToken, "set");
        // The C# automatic-property form: the stub renders when the
        // accessors carry NO bodies (an interface or abstract member) or
        // when they decompile to the backing-field pattern
        // (PatternStatementTransform's `return <X>k__BackingField;` /
        // `<X>k__BackingField = value;` -- the backing field must exist,
        // only the compiler emits the angle-bracket names). A reference
        // assembly's stale-RVA accessors fail to decode and render the
        // empty-body error blocks instead -- a NON-decoding body does
        // not collapse to the stub.
        const std::string backingName = "<" + p.Name + ">k__BackingField";
        bool hasBackingField = false;
        for (const auto& f : file.GetFields(typeToken)) {
            if (f.Name == backingName) {
                hasBackingField = true;
                break;
            }
        }
        auto trimmedBody = [](const std::string& body) {
            std::size_t start = body.find_first_not_of(" \t\r\n");
            if (start == std::string::npos)
                return std::string();
            std::size_t end = body.find_last_not_of(" \t\r\n");
            return body.substr(start, end - start + 1);
        };
        bool backingPattern =
            hasBackingField &&
            (getterBody.empty() ||
             trimmedBody(getterBody) == "return " + backingName + ";") &&
            (setterBody.empty() ||
             trimmedBody(setterBody) == backingName + " = value;");
        // The C# accessor-attribute rule: a property whose accessor
        // carries attributes (the interop shapes -- [MethodImpl],
        // [SuppressUnmanagedCodeSecurity], the [return: MarshalAs]
        // return-type attributes) renders the ACCESSOR BLOCK form --
        // the stub cannot carry them. The accessor has no body (an
        // InternalCall), so the block's accessor renders `get;`.
        const TS::IMethod* getterEntity =
            accessors.GetterToken != 0
                ? module.GetDefinitionMethod(accessors.GetterToken)
                : nullptr;
        const TS::IMethod* setterEntity =
            accessors.SetterToken != 0
                ? module.GetDefinitionMethod(accessors.SetterToken)
                : nullptr;
        bool accessorsHaveAttributes =
            (getterEntity != nullptr &&
             (!getterEntity->GetAttributes().empty() ||
              !getterEntity->GetReturnTypeAttributes().empty())) ||
            (setterEntity != nullptr &&
             (!setterEntity->GetAttributes().empty() ||
              !setterEntity->GetReturnTypeAttributes().empty()));
        if (backingPattern ||
            (getterBody.empty() && setterBody.empty() &&
             !accessorsHaveAttributes)) {
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
                out += MemberAttributesText(
                    module.GetDefinitionMethod(accessors.GetterToken));
                out += AccessorReturnAttributesText(getterEntity);
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
                out += MemberAttributesText(
                    module.GetDefinitionMethod(accessors.SetterToken));
                out += AccessorReturnAttributesText(setterEntity);
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
            // The C# field-like event's backing field: the private
            // same-name field the compiler emits alongside the event
            // declaration does not render (the event declaration names
            // it; the C# hides it like the property's k__BackingField).
            backingFieldNames.insert(e.Name);
            std::string eventTypeName = "object";
            const TS::IEvent* event = module.GetDefinitionEvent(e.Token);
            // The member documentation: the E: ID form.
            if (documentationProvider != nullptr && event != nullptr &&
                event->DeclaringTypeDefinition() != nullptr) {
                std::string eventId =
                    "E:" + TypeDocName(file, event->DeclaringTypeDefinition()
                                                   ->MetadataToken()) +
                    "." + EscapedMemberDocName(e.Name);
                std::string documentation =
                    documentationProvider->GetDocumentation(eventId);
                if (!documentation.empty())
                    out += DocumentationCommentLines(documentation);
            }
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
                eventTypeName = RenderBaseTypeName(
                    eventType->GetDefinition(), eventType,
                    scopeResolver.get());
            }
            // The C# DoDecompileMember's event arm over an explicit
            // implementation: the interface-qualified name (the dotted
            // metadata name's last segment + the implemented interface
            // through the name decision) and the add/remove accessor
            // blocks -- never the field-like `;` form. A field-like event
            // keeps the plain name + the semicolon.
            bool isExplicitImplementation = e.Name.find('.') != std::string::npos;
            std::string eventName = e.Name;
            if (isExplicitImplementation) {
                eventName = eventName.substr(
                    eventName.find_last_of('.') + 1);
                // The implemented interface: the event entity's explicit
                // members' declaring type, else the dotted prefix.
                std::string interfaceName;
                if (event != nullptr) {
                    std::vector<const TS::IMember*> implemented =
                        event->ExplicitlyImplementedInterfaceMembers();
                    if (!implemented.empty() &&
                        implemented[0] != nullptr &&
                        implemented[0]->DeclaringType() != nullptr) {
                        const TS::ITypePtr& interfaceType =
                            implemented[0]->DeclaringType();
                        TS::ITypePtr instantiation =
                            InterfaceInstantiationFor(
                                event->DeclaringTypeDefinition(),
                                interfaceType != nullptr
                                    ? interfaceType->GetDefinition()
                                    : nullptr,
                                interfaceType);
                        if (instantiation == nullptr)
                            instantiation = interfaceType;
                        interfaceName = RenderBaseTypeName(
                            instantiation->GetDefinition(), instantiation,
                            scopeResolver.get());
                    }
                }
                if (interfaceName.empty()) {
                    // The fallback: the dotted prefix rendered through the
                    // last name decision (the metadata name's leading
                    // segments).
                    std::string prefix = e.Name.substr(
                        0, e.Name.find_last_of('.'));
                    eventName = prefix + "." + eventName;
                } else {
                    eventName = interfaceName + "." + eventName;
                }
            }
            out += "event ";
            out += eventTypeName;
            out += ' ';
            out += eventName;
            // The C# DoDecompileEvent's UseCustomEvents gate: the
            // add/remove block form renders when the accessors carry
            // bodies (the metadata HasBody; a reference assembly's
            // stale RVA counts, and its never-decoding bodies render the
            // empty-body error blocks). The field form renders when
            // neither accessor has a body (a crafted or interface-like
            // event) -- the recognized automatic pattern (the connid's
            // real bodies) is a later refinement.
            std::string adderBody = AccessorBodyText(
                file, typeSystem, accessors.AdderToken, "add");
            std::string removerBody = AccessorBodyText(
                file, typeSystem, accessors.RemoverToken, "remove");
            bool accessorsHaveBodies =
                !adderBody.empty() || !removerBody.empty();
            // The C# AutoEventDecompiler.IsAutomaticEvent: both
            // accessors compiler-generated AND decompiling to the
            // Delegate.Combine/Remove pattern over the backing field
            // render the field-like form (the C# matches the IL pattern;
            // the port approximates over the rendered bodies -- the
            // compiler-generated flag plus the combine/remove text; a
            // never-decoding reference-assembly body has neither).
            auto trimmedEventBody = [](const std::string& body) {
                std::size_t start = body.find_first_not_of(" \t\r\n");
                if (start == std::string::npos)
                    return std::string();
                std::size_t end = body.find_last_not_of(" \t\r\n");
                return body.substr(start, end - start + 1);
            };
            bool isAutomaticEvent =
                accessorsHaveBodies && !adderBody.empty() &&
                !removerBody.empty() &&
                Metadata::HasKnownAttribute(
                    file, accessors.AdderToken,
                    TS::KnownAttribute::CompilerGenerated) &&
                Metadata::HasKnownAttribute(
                    file, accessors.RemoverToken,
                    TS::KnownAttribute::CompilerGenerated) &&
                trimmedEventBody(adderBody).find(
                    "Delegate.Combine(") != std::string::npos &&
                trimmedEventBody(removerBody).find(
                    "Delegate.Remove(") != std::string::npos;
            // The C# field-like event's backing field: the private
            // same-name field hides only for the RECOGNIZED automatic
            // event (the AutoEventDecompiler's backing field); an
            // accessor-block (custom) event keeps its field visible.
            if (isAutomaticEvent)
                backingFieldNames.insert(e.Name);
            if (isExplicitImplementation ||
                (accessorsHaveBodies && !isAutomaticEvent)) {
                out += "\n{\n";
                if (accessors.AdderToken != 0) {
                    out += "add\n{\n";
                    out += adderBody;
                    out += "}\n";
                }
                if (accessors.RemoverToken != 0) {
                    out += "remove\n{\n";
                    out += removerBody;
                    out += "}\n";
                }
                out += "}\n";
            } else {
                out += ";\n";
            }
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
        // The C# DetectBestEnumValueDisplayMode's head: a [Flags] enum
        // always renders the hexadecimal form.
        if (Metadata::HasKnownAttribute(
                file, typeToken, TS::KnownAttribute::Flags))
            displayMode = EnumValueDisplayMode::AllHex;
        // The walk only runs when the [Flags] check did not decide (the
        // C# returns early; the walk's out-of-order fallback would
        // otherwise overwrite the hexadecimal form).
        if (displayMode == EnumValueDisplayMode::None) {
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
                } else if (!allConsecutive && !allPowersOfTwo) {
                    // The C#'s per-member early abort: the values are
                    // neither consecutive nor all powers of two, so every
                    // value displays as-is (the All mode) -- no need to
                    // walk the rest.
                    displayMode = EnumValueDisplayMode::All;
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
            // The member documentation (the C# AddXmlDocumentationTransform
            // over the enum-member declarations): the F: ID form.
            if (documentationProvider != nullptr &&
                fieldEntity->DeclaringTypeDefinition() != nullptr) {
                std::string fieldId =
                    "F:" +
                    TypeDocName(file, fieldEntity->DeclaringTypeDefinition()
                                     ->MetadataToken()) +
                    "." + EscapedMemberDocName(f.Name);
                std::string documentation =
                    documentationProvider->GetDocumentation(fieldId);
                if (!documentation.empty())
                    out += DocumentationCommentLines(documentation);
            }
            out += f.Name;
            bool withInitializer =
                displayMode == EnumValueDisplayMode::All ||
                displayMode == EnumValueDisplayMode::AllHex ||
                (displayMode == EnumValueDisplayMode::FirstOnly &&
                 firstMember);
            if (withInitializer) {
                // The C# ConvertEnumValue's direct-match arm: an enum
                // member whose value equals an EARLIER member of the same
                // enum renders that member's name (`NORMAL =
                // SHOWNORMAL`), not the numeric literal -- a member can
                // only reference members declared before it (the C#
                // row-number rule), and in a [Flags] enum only the
                // single-bit members are referenced directly (the
                // combined values are built from their flag components;
                // the zero members stay numeric).
                std::string literal =
                    ConstantFieldLiteral(*fieldEntity);
                bool literalIsSpecial = false;
                // The specialConstants table applies to the enum members'
                // numeric fallback too (the C# ConvertEnumValue's tail
                // goes through ConvertConstantValue over the underlying
                // type): the boundary values render `uint.MaxValue`, not
                // the hexadecimal 0xFFFFFFFF form.
                {
                    std::any specialValue;
                    try {
                        specialValue = fieldEntity->GetConstantValue();
                    } catch (const std::exception&) {
                    }
                    std::string special =
                        SpecialConstantText(specialValue,
                                           fieldEntity->Type());
                    if (!special.empty()) {
                        literal = special;
                        literalIsSpecial = true;
                    }
                }
                bool literalIsAlias = false;
                std::any constantValue;
                try {
                    constantValue = fieldEntity->GetConstantValue();
                } catch (const std::exception&) {
                }
                const std::uint32_t declaringRow = f.Token & 0xFFFFFF;
                const bool enumIsFlags = Metadata::HasKnownAttribute(
                    file, typeToken, TS::KnownAttribute::Flags);
                for (const auto& other : file.GetFields(typeToken)) {
                    if (other.Token == f.Token)
                        continue;
                    const TS::IField* otherEntity =
                        module.GetDefinitionField(other.Token);
                    if (otherEntity == nullptr || !otherEntity->IsConst())
                        continue;
                    std::any otherValue;
                    try {
                        otherValue = otherEntity->GetConstantValue();
                    } catch (const std::exception&) {
                        continue;
                    }
                    if (!ValuesEqual(constantValue, otherValue))
                        continue;
                    if ((other.Token & 0xFFFFFF) >= declaringRow)
                        // The C# row-order rule: only EARLIER members
                        // are referenceable.
                        continue;
                    if (enumIsFlags) {
                        std::int64_t v = 0;
                        if (auto* p =
                                std::any_cast<std::int32_t>(&otherValue))
                            v = *p;
                        else if (auto* p =
                                     std::any_cast<std::uint32_t>(
                                         &otherValue))
                            v = static_cast<std::int64_t>(*p);
                        else if (auto* p =
                                     std::any_cast<std::int64_t>(
                                         &otherValue))
                            v = *p;
                        const bool singleBit =
                            v != 0 && (v & (v - 1)) == 0;
                        if (v == 0 || !singleBit)
                            continue;
                    }
                    literal = other.Name;
                    literalIsAlias = true;
                    break;
                }
                if (!literalIsAlias && enumIsFlags) {
                    // The C# ConvertEnumValue's [Flags] composition
                    // arms: the union (`A | B`) and the complement
                    // (`~X`) forms over the earlier single-bit members.
                    std::string composition = EnumFlagsComposition(
                        file, module, typeToken, constantValue,
                        declaringRow, std::string());
                    if (!composition.empty()) {
                        literal = composition;
                        literalIsAlias = true;
                    }
                }
                if (displayMode == EnumValueDisplayMode::AllHex &&
                    !literalIsAlias && !literalIsSpecial) {
                    // The C# AllHex arm: values >= 10 render as 0x + the
                    // uppercase hex form, KEEPING the decimal literal's
                    // underlying-type suffix (the LiteralFormat flip over
                    // the already-suffixed literal).
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
                    else if (auto* p =
                                 std::any_cast<std::int16_t>(&constant))
                        v = *p;
                    else if (auto* p =
                                 std::any_cast<std::uint16_t>(&constant))
                        v = *p;
                    else if (auto* p =
                                 std::any_cast<std::int8_t>(&constant))
                        v = *p;
                    else if (auto* p =
                                 std::any_cast<std::uint8_t>(&constant))
                        v = *p;
                    if (v >= 10) {
                        char buf[32];
                        std::snprintf(buf, sizeof(buf), "0x%llX",
                                      static_cast<unsigned long long>(v));
                        std::string hex = buf;
                        // The suffix from the decimal literal (the part
                        // after the leading digits: u/L/uL).
                        std::size_t firstNonDigit = 0;
                        while (firstNonDigit < literal.size() &&
                               literal[firstNonDigit] >= '0' &&
                               literal[firstNonDigit] <= '9')
                            firstNonDigit++;
                        hex += literal.substr(firstNonDigit);
                        literal = hex;
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
        const TS::IField* fieldEntity = module.GetDefinitionField(f.Token);
        // The member documentation: the F: ID form.
        if (documentationProvider != nullptr && fieldEntity != nullptr &&
            fieldEntity->DeclaringTypeDefinition() != nullptr) {
            std::string fieldId =
                "F:" + TypeDocName(file, fieldEntity->DeclaringTypeDefinition()
                                               ->MetadataToken()) +
                "." + EscapedMemberDocName(f.Name);
            std::string documentation =
                documentationProvider->GetDocumentation(fieldId);
            if (!documentation.empty())
                out += DocumentationCommentLines(documentation);
        }
        auto fieldType = file.GetFieldSignature(f.Token);
        std::string fieldTypeName =
            fieldType
                ? RenderBaseTypeName(fieldType->GetDefinition(), fieldType,
                                     scopeResolver.get())
                : std::string("var");
        if (fieldEntity != nullptr) {
            TS::ITypePtr fieldResolvedType(
                const_cast<TS::IType*>(&fieldEntity->ReturnType()),
                [](TS::IType*) {});
            fieldTypeName = RenderBaseTypeName(
                fieldResolvedType->GetDefinition(), fieldResolvedType,
                scopeResolver.get());
        }
        out += MemberAttributesText(fieldEntity);
        out += MemberModifiersText(fieldEntity);
        out += fieldTypeName;
        out += ' ';
        out += f.Name;
        if (fieldEntity != nullptr && fieldEntity->IsConst()) {
            std::string literal = ConstantFieldLiteral(*fieldEntity);
            // The enum-typed const fields render the qualified member
            // reference / composition / cast form (the C#
            // ConvertEnumValue with declaringEnumMember null); the
            // integral boundaries render the well-known member names
            // (the specialConstants table).
            std::string enumExpression = EnumConstantFieldExpression(
                file, module, *fieldEntity);
            if (!enumExpression.empty())
                literal = enumExpression;
            else if (!literal.empty()) {
                std::any specialValue;
                try {
                    specialValue = fieldEntity->GetConstantValue();
                } catch (const std::exception&) {
                }
                std::string special =
                    SpecialConstantText(specialValue, fieldEntity->Type());
                if (!special.empty())
                    literal = special;
            }
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
        // The C# MemberIsHidden's constructor arm: a parameterless
        // constructor with no body on an IMPORTED type (a ComImport
        // co-class -- the runtime synthesizes the constructor; the source
        // never expressed it) does not render.
        if (m.Name == ".ctor" && m.RVA == 0 &&
            (file.GetTypeDefAttributes(typeToken) & 0x1000u) != 0)
            continue;
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
        // The member documentation (the C# AddXmlDocumentationTransform
        // over the method declarations): the M: ID form (the constructors
        // included -- `.ctor` escapes to `#ctor`).
        if (documentationProvider != nullptr && methodEntity != nullptr &&
            methodEntity->DeclaringTypeDefinition() != nullptr) {
            std::string methodId = "M:" + TypeDocName(file,
                methodEntity->DeclaringTypeDefinition()->MetadataToken()) +
                "." + EscapedMemberDocName(m.Name);
            if (!methodEntity->TypeParameters().empty())
                methodId += "``" +
                            std::to_string(methodEntity->TypeParameters().size());
            methodId += ParameterListDocText(methodEntity->Parameters());
            if (m.Name == "op_Implicit" || m.Name == "op_Explicit" ||
                m.Name == "op_CheckedExplicit") {
                TS::ITypePtr returnType(
                    const_cast<TS::IType*>(&methodEntity->ReturnType()),
                    [](TS::IType*) {});
                methodId += "~" + DocTypeName(returnType);
            }
            std::string documentation =
                documentationProvider->GetDocumentation(methodId);
            if (!documentation.empty())
                out += DocumentationCommentLines(documentation);
        }
        if (!isConstructor && methodEntity != nullptr &&
            methodEntity->IsExplicitInterfaceImplementation()) {
            methodName = methodName.substr(
                methodName.find_last_of('.') + 1);
            std::vector<const TS::IMember*> implemented =
                methodEntity->ExplicitlyImplementedInterfaceMembers();
            if (!implemented.empty() && implemented[0] != nullptr &&
                implemented[0]->DeclaringType() != nullptr) {
                TS::ITypePtr instantiation = InterfaceInstantiationFor(
                    methodEntity->DeclaringTypeDefinition(),
                    implemented[0]->DeclaringType()->GetDefinition(),
                    implemented[0]->DeclaringType());
                if (instantiation == nullptr)
                    instantiation = implemented[0]->DeclaringType();
                methodName =
                    RenderBaseTypeName(
                        instantiation->GetDefinition(), instantiation,
                        scopeResolver.get()) +
                    "." + methodName;
            }
        }
        std::string modifiers =
            MemberModifiersText(methodEntity);
        // The signature render shared by the declaration forms: the entity
        // path preferred (the name decision needs the definitions; the
        // file-signature decode yields unresolved simple types), the file
        // decode as the fallback.
        std::string returnType = "void";
        std::string paramDecl;
        if (methodEntity != nullptr) {
            if (methodEntity->ReturnType().ReflectionName() !=
                "System.Void") {
                TS::ITypePtr resolvedReturnType(
                    const_cast<TS::IType*>(&methodEntity->ReturnType()),
                    [](TS::IType*) {});
                returnType = RenderBaseTypeName(
                    resolvedReturnType->GetDefinition(), resolvedReturnType,
                    scopeResolver.get());
            }
            std::vector<const TS::IParameter*> parameters =
                methodEntity->Parameters();
            auto paramNames = file.GetParameterNames(m.Token);
            paramDecl = CSharpDecompiler::MethodDeclString(
                parameters, !methodEntity->IsStatic(), paramNames,
                scopeResolver.get(), methodEntity->IsExtensionMethod());
        } else if (auto sig = file.GetMethodSignature(m.Token)) {
            if (sig->ReturnType &&
                sig->ReturnType->ReflectionName() != "System.Void")
                returnType = RenderBaseTypeName(
                    sig->ReturnType->GetDefinition(), sig->ReturnType,
                    scopeResolver.get());
            auto paramNames = file.GetParameterNames(m.Token);
            paramDecl = CSharpDecompiler::MethodDeclString(
                *sig, paramNames, scopeResolver.get());
        }
        // The C# operator declarations: the op_* special-name methods
        // render as the operator spellings. The conversion operators move
        // the return type after the operator keyword -- the shared render
        // arms (the stub, the empty-body, and the ILAst body emitter) all
        // compose `returnType + " " + methodName`, so the conversion
        // renders its keyword in the return-type slot.
        if (!isConstructor && !methodName.empty() &&
            methodName.rfind("op_", 0) == 0) {
            if (methodName == "op_Implicit" ||
                methodName == "op_CheckedImplicit") {
                methodName = "operator " + returnType;
                returnType = "implicit";
            } else if (methodName == "op_Explicit" ||
                       methodName == "op_CheckedExplicit") {
                methodName = "operator " + returnType;
                returnType = "explicit";
            } else {
                std::string token = OperatorToken(methodName);
                if (!token.empty())
                    methodName = "operator " + token;
            }
        }
        // The C# ConvertDestructor: the Finalize overrides render as
        // `~TypeName()` -- the name from the declaring type, no
        // modifiers, no return type (the DestructorDeclaration carries
        // none; the accessibility and the override shape are implied).
        bool isDestructor =
            !isConstructor && methodEntity != nullptr &&
            methodEntity->SymbolKind() == TS::SymbolKind::Destructor;
        if (isDestructor) {
            methodName = "~" + typeName;
            modifiers.clear();
            returnType.clear();
        }
        // The C# generic method name: the method's OWN type parameters
        // render as the `<T1, T2>` list after the name. The port's
        // TypeParameters surface on a method is chain-merged (the
        // declaring type's parameters first, the method's own last), so
        // the own slice starts at the declaring type's chain size (the
        // type-header outer-parameter-skip precedent). The constructors
        // render the declaring TYPE's list (the C# `public Foo<T>()`),
        // the operators cannot be generic, and an explicit-implementation
        // name carries the interface form already.
        std::string methodTypeParameterList;
        if (!isConstructor && !isDestructor &&
            methodEntity != nullptr &&
            !methodEntity->IsExplicitInterfaceImplementation() &&
            methodName.rfind("operator", 0) != 0) {
            const std::vector<const TS::ITypeParameter*>& typeParameters =
                methodEntity->TypeParameters();
            const TS::ITypeDefinition* declaringTypeDefinition =
                methodEntity->DeclaringTypeDefinition();
            if (declaringTypeDefinition != nullptr &&
                declaringTypeDefinition->TypeParameters().size() <
                    typeParameters.size()) {
                bool first = true;
                for (std::size_t i =
                         declaringTypeDefinition->TypeParameters().size();
                     i < typeParameters.size(); ++i) {
                    const TS::ITypeParameter* tp = typeParameters[i];
                    if (tp == nullptr)
                        continue;
                    methodTypeParameterList += first ? "<" : ", ";
                    methodTypeParameterList += tp->Name();
                    first = false;
                }
                if (!methodTypeParameterList.empty())
                    methodTypeParameterList += ">";
            }
            methodName += methodTypeParameterList;
        }
        // The C# method-level constraint clauses: the generic method's
        // own type parameters' `where` clauses render after the
        // parameter list (the same skip rule and forms as the type
        // header's).
        std::string methodConstraints;
        if (!methodTypeParameterList.empty() &&
            methodEntity != nullptr) {
            const TS::ITypeDefinition* declaringTypeDefinition =
                methodEntity->DeclaringTypeDefinition();
            methodConstraints = ConstraintClausesText(
                methodEntity->TypeParameters(),
                declaringTypeDefinition != nullptr
                    ? declaringTypeDefinition->TypeParameters().size()
                    : 0,
                scopeResolver.get());
        }
        // The C# AddInterfaceImplHelpers (the .override directive
        // synthesis): a plain-named method bound to an interface contract
        // through a MethodImpl row (the VB-style explicit implementation
        // C# source cannot express) renders a synthesized
        // explicit-interface-implementation forwarder after its own
        // declaration. No forwarder when the member is already a dotted
        // explicit implementation, is static, or renders extern (the C#'s
        // three guards).
        bool memberRendersExtern =
            methodEntity != nullptr && !methodEntity->IsAbstract() &&
            methodEntity->DeclaringType() != nullptr &&
            methodEntity->DeclaringType()->Kind() !=
                TS::TypeKind::Interface &&
            m.RVA == 0;
        auto renderOverrideForwarders = [&]() {
            if (isConstructor || methodEntity == nullptr ||
                methodEntity->IsExplicitInterfaceImplementation() ||
                methodEntity->IsStatic() || memberRendersExtern)
                return;
            for (const TS::IMember* implemented :
                 methodEntity->ExplicitlyImplementedInterfaceMembers()) {
                const auto* interfaceMethod =
                    dynamic_cast<const TS::IMethod*>(implemented);
                if (interfaceMethod == nullptr ||
                    interfaceMethod->DeclaringType() == nullptr ||
                    interfaceMethod->DeclaringType()->Kind() !=
                        TS::TypeKind::Interface)
                    continue;
                const TS::ITypePtr interfaceType =
                    interfaceMethod->DeclaringType();
                out += isConstructor ? std::string() : returnType + " ";
                out += RenderBaseTypeName(
                    interfaceType != nullptr
                        ? interfaceType->GetDefinition()
                        : nullptr,
                    interfaceType, scopeResolver.get());
                out += '.';
                out += interfaceMethod->Name();
                out += "(" + paramDecl + ")\n";
                out += "{\n";
                out += "//ILSpy generated this explicit interface "
                       "implementation from .override directive in ";
                out += methodName;
                out += "\n";
                // The forwarding call: this.<member>(parameters).
                std::string call = "this." + methodName + "(";
                std::vector<std::string> parameterNames =
                    file.GetParameterNames(m.Token);
                if (methodEntity != nullptr) {
                    parameterNames.clear();
                    for (const TS::IParameter* parameter :
                         methodEntity->Parameters())
                        parameterNames.push_back(
                            parameter != nullptr
                                ? parameter->Name()
                                : std::string());
                }
                for (std::size_t i = 0; i < parameterNames.size(); ++i) {
                    if (i != 0) call += ", ";
                    if (parameterNames[i].empty())
                        call += "arg_" + std::to_string(i + 1);
                    else
                        call += parameterNames[i];
                }
                call += ")";
                if (interfaceMethod->ReturnType().ReflectionName() ==
                    "System.Void")
                    out += call + ";\n";
                else
                    out += "return " + call + ";\n";
                out += "}\n";
            }
        };
        if (m.RVA == 0) {
            // The C# DoDecompileMethod's body-less arm: an abstract method
            // (or an interface member) renders as a declaration with no
            // body; a body-less non-abstract member of a non-interface type
            // is externally implemented (the C# adds the extern modifier).
            if (memberRendersExtern) {
                modifiers += "extern ";
            }
            out += MemberAttributesText(methodEntity);
            out += AccessorReturnAttributesText(methodEntity);
            out += modifiers;
            out += (isConstructor || returnType.empty())
                       ? std::string()
                       : returnType + " ";
            out += methodName;
            out += "(" + paramDecl + ")" + methodConstraints + ";\n";
            renderOverrideForwarders();
            rendered = true;
            continue;
        }
        std::string text;
        bool asyncDecompiled = false;
        bool iteratorDecompiled = false;
        if (CSharpDecompiler::DecompileMethodToString(
                file, typeSystem, m.Token, m.RVA, methodName, text,
                isConstructor || isDestructor, &asyncDecompiled,
                &iteratorDecompiled, scopeResolver.get(),
                methodConstraints)) {
            // The C# default-constructor elision: a PUBLIC PARAMETERLESS
            // instance constructor whose decompiled body renders empty
            // (the compiler's implicit default over the object base --
            // nothing the source expressed) does not render.
            if (m.Name == ".ctor" && paramDecl.empty() &&
                modifiers.find("public ") == 0) {
                std::size_t open = text.find("{\n");
                std::size_t close = text.rfind("\n}");
                std::string body =
                    (open != std::string::npos &&
                     close != std::string::npos && close > open + 2)
                        ? text.substr(open + 2, close - (open + 2))
                        : std::string();
                bool emptyBody = true;
                for (char c : body)
                    if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                        emptyBody = false;
                if (emptyBody) {
                    renderOverrideForwarders();
                    rendered = true;
                    continue;
                }
            }
            out += MemberAttributesText(methodEntity, asyncDecompiled,
                                        iteratorDecompiled);
            out += modifiers;
            out += text;
            out += "\n";
            renderOverrideForwarders();
            rendered = true;
        } else {
            // The C# DecompileMethod's body-decode failure: the declaration
            // with the reference-assembly empty-body comment (a reference
            // assembly carries stale RVAs whose bodies never decode; the
            // members previously vanished from the render here).
            out += MemberAttributesText(methodEntity);
            out += AccessorReturnAttributesText(methodEntity);
            out += modifiers;
            out += (isConstructor || returnType.empty())
                       ? std::string()
                       : returnType + " ";
            out += methodName;
            out += "(" + paramDecl + ")" + methodConstraints + "\n";
            out += "{\n";
            out += "/*Error: Empty body found. Decompiled assembly might "
                   "be a reference assembly.*/;\n";
            out += "}\n";
            renderOverrideForwarders();
            rendered = true;
        }
    }
    if (rendered) {
        out += "}\n";
    }
    if (rendered) {
        // The flat render's line conventions: the header (the doc
        // comments, the attributes, the modifiers, the name, the base
        // list, the opening brace) sits at the type's own level and the
        // member block indents one level (one tab) deeper -- the C#
        // output's nesting (the whole-module types ride the caller's
        // namespace indent; the -t render's file-scoped namespace keeps
        // the type at column 0). Empty separator lines stay empty (the
        // C# blank lines carry no whitespace).
        // Segment the text: the header (through the opening brace), the
        // member block, and the type's own closing brace line. A delegate
        // (no braces) renders header-only -- nothing to indent.
        std::vector<std::string_view> lines;
        std::size_t start = 0;
        while (start < out.size()) {
            const std::size_t nl = out.find('\n', start);
            const std::size_t end = nl == std::string::npos
                                        ? out.size()
                                        : nl + 1;
            lines.emplace_back(out.data() + start, end - start);
            start = end;
        }
        std::size_t openIndex = std::string_view::npos;
        std::size_t closeIndex = std::string_view::npos;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i] == "{\n") {
                openIndex = i;
                break;
            }
        }
        if (!lines.empty() && lines.back() == "}\n")
            closeIndex = lines.size() - 1;
        if (openIndex == std::string_view::npos ||
            closeIndex == std::string_view::npos ||
            closeIndex <= openIndex) {
            return rendered;  // the header-only shape (the delegate arm)
        }
        // The blank separator immediately before the closing brace drops
        // (the C# render keeps the last member tight against it).
        std::size_t memberEnd = closeIndex;
        while (memberEnd > openIndex + 1 && lines[memberEnd - 1] == "\n")
            --memberEnd;
        std::string indented;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const std::string_view& line = lines[i];
            const bool inHeader = i <= openIndex || i >= closeIndex;
            if (i >= memberEnd && i < closeIndex) {
                continue;  // the dropped pre-brace blank lines
            }
            if (line == "\n") {
                indented += "\n";
            } else if (inHeader) {
                indented += line;
            } else {
                indented += '\t';
                indented += line;
            }
        }
        out = std::move(indented);
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
    TS::DecompilerTypeSystem typeSystem(file, resolver,
                                        NativeIntegerOptionsFor(file));
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
    if (TypeIsHiddenFromRender(file, typeToken))
        return false;
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
    state_->typeSystem.emplace(file, *state_->resolver,
                                NativeIntegerOptionsFor(file));
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
    // The attribute sweep uses the MINIMAL collector (the per-type
    // sweep's settings): the default constructor seeds the known-type
    // namespaces (System, System.Collections, System.Collections.Generic,
    // System.Threading.Tasks, System.Numerics -- the attribute-literal
    // rendering's resolvable known types), which the whole-module header
    // must not carry (the oracle's using set names only the namespaces
    // the rendered declarations actually reference).
    std::unordered_set<std::string> attributeNamespaces;
    {
        RequiredNamespaceCollector collector(attributeNamespaces,
                                              /*seedKnownTypeNamespaces=*/
                                                  false,
                                              /*minimalUsingSet=*/true);
        collector.HandleAttributes(
            const_cast<TS::MetadataModule&>(module).GetAssemblyAttributes());
        collector.HandleAttributes(
            const_cast<TS::MetadataModule&>(module).GetModuleAttributes());
    }
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
    if (TypeIsHiddenFromRender(*state_->file, typeToken))
        return false;
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
    TS::DecompilerTypeSystem typeSystem(file, resolver,
                                        NativeIntegerOptionsFor(file));

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
