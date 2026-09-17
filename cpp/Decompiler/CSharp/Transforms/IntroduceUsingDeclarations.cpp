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

// Out-of-line implementation of the `IntroduceUsingDeclarations` transform (port of
// ICSharpCode.Decompiler/CSharp/Transforms/IntroduceUsingDeclarations.cs). The two nested
// classes live here in an anonymous namespace so the public header stays free of the
// `CSharp::TypeSystem` namespace (the `IntroduceExtensionMethods` convention).
//
// Deferrals (each named at its call site):
//   * `FindRequiredImports.VisitParenthesizedVariableDesignation` / `VisitTupleExpression`
//     read `MatchInstruction.Method`; the port's `MatchInstruction` does not carry the
//     resolved method yet (the deconstruct-pattern deferral), so those two arms cannot
//     see the deconstructor's declaring namespace and are omitted.
//   * `FullyQualifyAmbiguousTypeNamesVisitor.CreateAstBuilder`'s `function != null` arm
//     adds the function's variables through `DefaultVariable`; the port has no concrete
//     `IVariable`, so the arm is omitted (the plain resolver-only builder is built).

#include "Decompiler/CSharp/Transforms/IntroduceUsingDeclarations.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"
#include "Decompiler/CSharp/Resolver/NameLookupMode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/DepthFirstAstVisitor.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ArrayInitializerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/TypeReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/MemberType.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/ParameterDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/TypeSystemAstBuilder.hpp"
#include "Decompiler/CSharp/Syntax/UsingDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/CSharp/UsingScopeAnnotation.hpp"
#include "Decompiler/IL/Instructions/Call.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/INamedElement.hpp"
#include "Decompiler/TypeSystem/ParameterizedTypeReference.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/Util/StringComparers.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {

namespace {

namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

using Syntax::AstNode;
using Syntax::AstType;
using Syntax::Attribute;
using Syntax::EntityDeclaration;
using Syntax::MemberType;
using Syntax::NamespaceDeclaration;
using Syntax::SimpleType;
using Syntax::TypeDeclaration;
using Syntax::UsingDeclaration;

// Disambiguate the `UnknownType` CLASS from the `UnknownType()` factory function (the
// CustomPatterns.cpp / TypeSystemAstBuilder.cpp precedent).
using TS::UnknownType;

// The C# `IType.Namespace` for the shapes the import collection reaches (the same helper
// as the file-local one in CustomPatterns.cpp / TypeSystemAstBuilder.cpp): an `IEntity`
// type reports `INamedElement::Namespace()`, a `ParameterizedType` delegates to its
// generic, an `UnknownType` reports its stored full type name's namespace, anything else
// is the empty default.
std::string TypeNamespaceOf(const TS::IType& type) {
    if (const auto* named = dynamic_cast<const TS::INamedElement*>(&type))
        return named->Namespace();
    if (const auto* parameterized = dynamic_cast<const TS::ParameterizedType*>(&type))
        return parameterized->GenericType() ? TypeNamespaceOf(*parameterized->GenericType())
                                            : std::string();
    if (const auto* unknown = dynamic_cast<const class UnknownType*>(&type))
        return unknown->FullTypeName().GetTopLevelTypeName().Namespace();
    return std::string();
}

// The C# `IType.FullName` for the WinForms base-type probe: an `IEntity` type reports
// `INamedElement::FullName()`, a `ParameterizedType` delegates to its generic, and any
// other shape falls back to `ReflectionName()`.
std::string TypeFullNameOf(const TS::IType& type) {
    if (const auto* named = dynamic_cast<const TS::INamedElement*>(&type))
        return named->FullName();
    if (const auto* parameterized = dynamic_cast<const TS::ParameterizedType*>(&type))
        return parameterized->GenericType() ? TypeFullNameOf(*parameterized->GenericType())
                                            : std::string();
    return type.ReflectionName();
}

// The C# `sealed class FindRequiredImports : DepthFirstAstVisitor`: collects the namespaces
// the tree references (a `SimpleType` resolves to, an extension-method `foreach`/`Add`
// call declaring type) and the namespaces the tree declares.
class FindRequiredImports final : public Syntax::DepthFirstAstVisitor {
public:
    std::set<std::string> DeclaredNamespaces;
    std::set<std::string> ImportedNamespaces;

    explicit FindRequiredImports(TransformContext& context) {
        currentNamespace_ = context.CurrentTypeDefinition() != nullptr
            ? context.CurrentTypeDefinition()->Namespace()
            : std::string();
        DeclaredNamespaces.insert(std::string());
    }

protected:
    void VisitSimpleType(SimpleType* simpleType) override {
        auto* trr = simpleType->Annotation<Sem::TypeResolveResult>();
        AddImportedNamespace(trr != nullptr ? &trr->Type() : nullptr);
        Syntax::DepthFirstAstVisitor::VisitSimpleType(simpleType);  // also visit type arguments
    }

    void VisitNamespaceDeclaration(NamespaceDeclaration* namespaceDeclaration) override {
        const std::string oldNamespace = currentNamespace_;
        for (const std::string& ident : namespaceDeclaration->Identifiers()) {
            currentNamespace_ = NamespaceDeclaration::BuildQualifiedName(currentNamespace_, ident);
            DeclaredNamespaces.insert(currentNamespace_);
        }
        Syntax::DepthFirstAstVisitor::VisitNamespaceDeclaration(namespaceDeclaration);
        currentNamespace_ = oldNamespace;
    }

    void VisitForeachStatement(Syntax::ForeachStatement* foreachStatement) override {
        auto* annotation = foreachStatement->Annotation<ForeachAnnotation>();
        if (annotation != nullptr) {
            if (auto* call = dynamic_cast<IL::Call*>(annotation->GetEnumeratorCall)) {
                if (call->Method && call->Method->IsExtensionMethod()) {
                    AddImportedNamespace(call->Method->DeclaringType().get());
                }
            }
        }
        Syntax::DepthFirstAstVisitor::VisitForeachStatement(foreachStatement);
    }

    // The C# `VisitParenthesizedVariableDesignation` / `VisitTupleExpression` arms read
    // `MatchInstruction.Method`; the port's `MatchInstruction` has no resolved method yet
    // (see the file header deferral), so the inherited depth-first walk is used unchanged.

    void VisitArrayInitializerExpression(
        Syntax::ArrayInitializerExpression* arrayInitializerExpression) override {
        auto& elements = arrayInitializerExpression->Elements();
        for (int i = 0; i < elements.Count(); i++) {
            IL::Call* optionalCall = nullptr;
            for (IL::ILInstruction* inst : GetILInstructions(*elements[i])) {
                if (auto* call = dynamic_cast<IL::Call*>(inst))
                    optionalCall = call;
            }
            if (optionalCall != nullptr && optionalCall->Method
                && optionalCall->Method->IsExtensionMethod()
                && optionalCall->Method->Name() == "Add") {
                AddImportedNamespace(optionalCall->Method->DeclaringType().get());
            }
        }
        Syntax::DepthFirstAstVisitor::VisitArrayInitializerExpression(arrayInitializerExpression);
    }

private:
    std::string currentNamespace_;

    bool IsParentOfCurrentNamespace(const std::string& ns) const {
        if (ns.empty())
            return true;
        if (currentNamespace_.rfind(ns, 0) == 0) {
            if (currentNamespace_.size() == ns.size())
                return true;
            if (currentNamespace_[ns.size()] == '.')
                return true;
        }
        return false;
    }

    void AddImportedNamespace(const TS::IType* type) {
        if (type != nullptr && !IsParentOfCurrentNamespace(TypeNamespaceOf(*type))) {
            ImportedNamespaces.insert(TypeNamespaceOf(*type));
        }
    }
};

// The C# `internal static bool CSharpDecompiler.IsWindowsFormsInitializeComponentMethod(
// IMethod method)` -- a `void InitializeComponent()` method on a type deriving from
// `System.Windows.Forms.Control` (the designer method whose name lookup the visitor resets
// to the bare compilation context).
bool IsWindowsFormsInitializeComponentMethod(const TS::IMethod& method) {
    if (method.ReturnType().Kind() != TS::TypeKind::Void)
        return false;
    if (method.Name() != "InitializeComponent")
        return false;
    const TS::ITypeDefinition* declaringType = method.DeclaringTypeDefinition();
    if (declaringType == nullptr)
        return false;
    for (const TS::IType* baseType : TS::GetNonInterfaceBaseTypes(declaringType)) {
        if (TypeFullNameOf(*baseType) == "System.Windows.Forms.Control")
            return true;
    }
    return false;
}

// The C# `sealed class FullyQualifyAmbiguousTypeNamesVisitor : DepthFirstAstVisitor`:
// walks the tree re-rendering every still-annotated `SimpleType` through the
// `TypeSystemAstBuilder` at the resolver position where the type sits, so a name that
// would be ambiguous without the `using` declarations is written out fully.
class FullyQualifyAmbiguousTypeNamesVisitor final : public Syntax::DepthFirstAstVisitor {
public:
    FullyQualifyAmbiguousTypeNamesVisitor(TransformContext& context,
                                          std::shared_ptr<TypeSystem::UsingScope> usingScope)
        : ignoreUsingScope_(!context.Settings().UsingDeclarations()),
          settings_(context.Settings()),
          context_(context),
          resolver_(std::make_shared<Resolver::CSharpResolver>(
              std::make_shared<TypeSystem::CSharpTypeResolveContext>(
                  context.TypeSystem().MainModule()))) {
        if (!ignoreUsingScope_) {
            if (context.CurrentTypeDefinition() != nullptr
                && !context.CurrentTypeDefinition()->Namespace().empty()) {
                std::string part;
                const std::string& ns = context.CurrentTypeDefinition()->Namespace();
                for (std::size_t i = 0; i <= ns.size(); i++) {
                    if (i == ns.size() || ns[i] == '.') {
                        usingScope = usingScope->WithNestedNamespace(part);
                        part.clear();
                    } else {
                        part.push_back(ns[i]);
                    }
                }
            }
            resolver_ = resolver_->WithCurrentUsingScope(usingScope)
                            ->WithCurrentTypeDefinition(context.CurrentTypeDefinition());
        }
        astBuilder_ = CreateAstBuilder(resolver_);
    }

    void VisitNamespaceDeclaration(NamespaceDeclaration* namespaceDeclaration) override {
        if (ignoreUsingScope_) {
            Syntax::DepthFirstAstVisitor::VisitNamespaceDeclaration(namespaceDeclaration);
            return;
        }
        std::shared_ptr<Resolver::CSharpResolver> previousResolver = resolver_;
        std::unique_ptr<Syntax::TypeSystemAstBuilder> previousAstBuilder = std::move(astBuilder_);
        std::shared_ptr<TypeSystem::UsingScope> usingScope = resolver_->CurrentUsingScope();
        for (const std::string& ident : namespaceDeclaration->Identifiers()) {
            usingScope = usingScope->WithNestedNamespace(ident);
        }
        resolver_ = resolver_->WithCurrentUsingScope(usingScope);
        astBuilder_ = CreateAstBuilder(resolver_);
        Syntax::DepthFirstAstVisitor::VisitNamespaceDeclaration(namespaceDeclaration);
        astBuilder_ = std::move(previousAstBuilder);
        resolver_ = previousResolver;
    }

    void VisitTypeDeclaration(TypeDeclaration* typeDeclaration) override {
        if (ignoreUsingScope_) {
            Syntax::DepthFirstAstVisitor::VisitTypeDeclaration(typeDeclaration);
            return;
        }
        if (typeDeclaration->HasPrimaryConstructor()) {
            inPrimaryConstructor_ = true;
            auto& parameters = typeDeclaration->PrimaryConstructorParameters();
            for (int i = 0; i < parameters.Count(); i++) {
                parameters[i]->AcceptVisitor(*this);
            }
            inPrimaryConstructor_ = false;
        }
        std::shared_ptr<Resolver::CSharpResolver> previousResolver = resolver_;
        std::unique_ptr<Syntax::TypeSystemAstBuilder> previousAstBuilder = std::move(astBuilder_);
        resolver_ = resolver_->WithCurrentTypeDefinition(
            dynamic_cast<const TS::ITypeDefinition*>(GetSymbol(*typeDeclaration)));
        astBuilder_ = CreateAstBuilder(resolver_);
        Syntax::DepthFirstAstVisitor::VisitTypeDeclaration(typeDeclaration);
        astBuilder_ = std::move(previousAstBuilder);
        resolver_ = previousResolver;
    }

    void VisitParameterDeclaration(Syntax::ParameterDeclaration* parameterDeclaration) override {
        // Parameters of primary constructors are visited separately (their types live at
        // the type-declaration scope) and are skipped here on the second walk.
        if (inPrimaryConstructor_
            || dynamic_cast<TypeDeclaration*>(parameterDeclaration->Parent()) == nullptr) {
            Syntax::DepthFirstAstVisitor::VisitParameterDeclaration(parameterDeclaration);
        }
    }

    void VisitMethodDeclaration(Syntax::MethodDeclaration* methodDeclaration) override {
        Visit(methodDeclaration,
              [&] { Syntax::DepthFirstAstVisitor::VisitMethodDeclaration(methodDeclaration); });
    }

    void VisitAccessor(Syntax::Accessor* accessor) override {
        Visit(accessor, [&] { Syntax::DepthFirstAstVisitor::VisitAccessor(accessor); });
    }

    void VisitConstructorDeclaration(Syntax::ConstructorDeclaration* constructorDeclaration) override {
        Visit(constructorDeclaration, [&] {
            Syntax::DepthFirstAstVisitor::VisitConstructorDeclaration(constructorDeclaration);
        });
    }

    void VisitDestructorDeclaration(Syntax::DestructorDeclaration* destructorDeclaration) override {
        Visit(destructorDeclaration, [&] {
            Syntax::DepthFirstAstVisitor::VisitDestructorDeclaration(destructorDeclaration);
        });
    }

    void VisitOperatorDeclaration(Syntax::OperatorDeclaration* operatorDeclaration) override {
        Visit(operatorDeclaration, [&] {
            Syntax::DepthFirstAstVisitor::VisitOperatorDeclaration(operatorDeclaration);
        });
    }

    void VisitSimpleType(SimpleType* simpleType) override {
        auto* rr = simpleType->Annotation<Sem::TypeResolveResult>();
        if (rr == nullptr) {
            Syntax::DepthFirstAstVisitor::VisitSimpleType(simpleType);
            return;
        }
        astBuilder_->NameLookupMode() = simpleType->GetNameLookupMode();
        if (astBuilder_->NameLookupMode() == Resolver::NameLookupMode::Type) {
            AstType* outermostType = simpleType;
            while (dynamic_cast<AstType*>(outermostType->Parent()) != nullptr)
                outermostType = static_cast<AstType*>(outermostType->Parent());
            if (dynamic_cast<Syntax::TypeReferenceExpression*>(outermostType->Parent()) != nullptr) {
                // ILSpy uses TypeReferenceExpression in expression context even when the
                // C# parser would not treat the name as a type reference.
                astBuilder_->NameLookupMode() = Resolver::NameLookupMode::Expression;
            }
        }
        TS::IType& type = const_cast<TS::IType&>(rr->Type());
        if (dynamic_cast<Attribute*>(simpleType->Parent()) != nullptr) {
            ReplaceAndRecordStep(simpleType, astBuilder_->ConvertAttributeType(type));
        } else {
            ReplaceAndRecordStep(simpleType, astBuilder_->ConvertType(type));
        }
    }

private:
    bool ignoreUsingScope_;
    const DecompilerSettings& settings_;
    TransformContext& context_;
    std::shared_ptr<Resolver::CSharpResolver> resolver_;
    std::unique_ptr<Syntax::TypeSystemAstBuilder> astBuilder_;
    bool inPrimaryConstructor_ = false;

    // The C# `TypeSystemAstBuilder CreateAstBuilder(CSharpResolver resolver, ILFunction?
    // function = null)`. The `function != null` arm that registers the function's
    // variables is deferred (see the file header).
    std::unique_ptr<Syntax::TypeSystemAstBuilder> CreateAstBuilder(
        std::shared_ptr<Resolver::CSharpResolver> resolver) {
        auto builder = std::make_unique<Syntax::TypeSystemAstBuilder>(std::move(resolver));
        builder->UseNullableSpecifierForValueTypes() = settings_.LiftNullables();
        builder->AlwaysUseGlobal() = settings_.AlwaysUseGlobal();
        builder->AddResolveResultAnnotations() = true;
        builder->UseAliases() = true;
        return builder;
    }

    void Visit(EntityDeclaration* entityDeclaration, const std::function<void()>& baseCall) {
        if (ignoreUsingScope_) {
            baseCall();
            return;
        }
        if (const auto* method = dynamic_cast<const TS::IMethod*>(GetSymbol(*entityDeclaration))) {
            std::shared_ptr<Resolver::CSharpResolver> previousResolver = resolver_;
            std::unique_ptr<Syntax::TypeSystemAstBuilder> previousAstBuilder = std::move(astBuilder_);
            if (IsWindowsFormsInitializeComponentMethod(*method)) {
                auto currentContext = std::make_shared<TypeSystem::CSharpTypeResolveContext>(
                    previousResolver->Compilation().MainModule());
                resolver_ = std::make_shared<Resolver::CSharpResolver>(currentContext);
            } else {
                resolver_ = resolver_->WithCurrentMember(method);
            }
            astBuilder_ = CreateAstBuilder(resolver_);
            baseCall();
            resolver_ = previousResolver;
            astBuilder_ = std::move(previousAstBuilder);
        } else {
            baseCall();
        }
    }

    static void ReplaceAndRecordStep(SimpleType* simpleType, AstType* replacement) {
        simpleType->ReplaceWith(replacement);
    }
};

}  // namespace

void IntroduceUsingDeclarations::Run(AstNode& rootNode, TransformContext& context) {
    // First determine all the namespaces that need to be imported.
    FindRequiredImports requiredImports(context);
    rootNode.AcceptVisitor(requiredImports);

    std::vector<const TS::INamespace*> resolvedNamespaces;

    if (context.Settings().UsingDeclarations()) {
        // #define directives are leading trivia on the syntax tree, so using declarations
        // go at the very start of the member list.
        AstNode* insertionPoint = nullptr;

        std::vector<std::string> sortedImports(requiredImports.ImportedNamespaces.begin(),
                                               requiredImports.ImportedNamespaces.end());
        std::stable_sort(sortedImports.begin(), sortedImports.end(),
                         [](const std::string& a, const std::string& b) {
                             const bool aSystem = a.rfind("System", 0) == 0;
                             const bool bSystem = b.rfind("System", 0) == 0;
                             if (aSystem != bSystem)
                                 return aSystem < bSystem;
                             // ThenByDescending (culture-linguistic default comparer).
                             return Util::CompareInvariantCulture(a, b) > 0;
                         });
        for (const std::string& ns : sortedImports) {
            // we go backwards (descending) through the list of namespaces because we
            // insert them backwards (always inserting at the start of the list).
            std::vector<std::string> parts;
            std::string part;
            for (std::size_t i = 0; i <= ns.size(); i++) {
                if (i == ns.size() || ns[i] == '.') {
                    parts.push_back(std::move(part));
                    part.clear();
                } else {
                    part.push_back(ns[i]);
                }
            }
            AstType* nsType = new SimpleType(parts[0]);
            for (std::size_t i = 1; i < parts.size(); i++) {
                nsType = new MemberType(nsType, parts[i]);
            }
            const TS::INamespace* resolvedNamespace =
                TS::GetNamespaceByFullName(context.TypeSystem(), ns);
            if (resolvedNamespace != nullptr) {
                resolvedNamespaces.push_back(resolvedNamespace);
            }
            context.Step("Add using declaration", &rootNode);
            auto* node = new UsingDeclaration();
            node->Import(nsType);
            rootNode.InsertChildAfter(insertionPoint, node, &Syntax::Slots::Member);
            context.EndStep(node);
        }
    }

    auto usingScope = std::make_shared<TypeSystem::UsingScope>(
        std::make_shared<TypeSystem::CSharpTypeResolveContext>(context.TypeSystem().MainModule()),
        context.TypeSystem().RootNamespace(), resolvedNamespaces);
    WithUsingScope(rootNode, usingScope);

    // Verify that the SimpleTypes refer to the correct type (no ambiguities).
    FullyQualifyAmbiguousTypeNamesVisitor fullyQualify(context, usingScope);
    rootNode.AcceptVisitor(fullyQualify);
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
