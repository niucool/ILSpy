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

#include "Decompiler/CSharp/Transforms/RemoveCompilerGeneratedAssemblyAttributes.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AbstractAnnotatable.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/NamespaceDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"

#include <cassert>
#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {
namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The C# `attribute.Type.Annotation<TypeResolveResult>()`: the attribute
// type's resolved type (annotated by TypeSystemAstBuilder.ConvertAttributeType),
// or null when the annotation is absent.
const TS::IType* GetAttributeType(Syntax::Attribute& attribute) {
    Syntax::AstType* type = attribute.Type();
    if (type == nullptr) return nullptr;
    const Sem::TypeResolveResult* trr = type->Annotation<Sem::TypeResolveResult>();
    if (trr == nullptr) return nullptr;
    return &trr->Type();
}

// The C# `expr.Value is int value` over the port's PrimitiveValue variant.
bool MatchIntValue(Syntax::PrimitiveExpression* expr, std::int32_t& value) {
    if (expr == nullptr) return false;
    if (auto* v = std::get_if<std::int32_t>(&expr->Value())) {
        value = *v;
        return true;
    }
    return false;
}

// The C# `expr2.Value is bool value`.
bool MatchBoolValue(Syntax::PrimitiveExpression* expr, bool& value) {
    if (expr == nullptr) return false;
    if (auto* v = std::get_if<bool>(&expr->Value())) {
        value = *v;
        return true;
    }
    return false;
}

} // namespace

void RemoveCompilerGeneratedAssemblyAttributes::Run(Syntax::AstNode& rootNode,
                                                    TransformContext& context) {
    (void)context;
    // The C# `rootNode.Children.OfType<AttributeSection>()` -- the top-level
    // children only (the assembly/module attribute sections live at the
    // root's level). The port walks the sibling chain.
    for (Syntax::AstNode* child = rootNode.FirstChild(); child != nullptr;) {
        Syntax::AstNode* next = child->NextSibling();
        auto* section = dynamic_cast<Syntax::AttributeSection*>(child);
        if (section == nullptr) {
            child = next;
            continue;
        }
        const std::string target = section->AttributeTarget();
        if (target == "assembly") {
            auto& attributes = section->Attributes();
            for (int i = 0; i < attributes.Count(); i++) {
                Syntax::Attribute* attribute = attributes.At(i);
                const TS::IType* type = GetAttributeType(*attribute);
                if (type == nullptr) continue;
                // The C# `trr.Type.FullName` -- the port's IType carries
                // ReflectionName (the namespace-qualified name; these
                // attribute types are all top-level, so the reflection name
                // IS the full name).
                const std::string fullName = type->ReflectionName();
                auto& arguments = attribute->Arguments();
                if (fullName == "System.Diagnostics.DebuggableAttribute") {
                    attribute->Remove();
                } else if (fullName ==
                           "System.Runtime.CompilerServices."
                           "CompilationRelaxationsAttribute") {
                    std::int32_t value = 0;
                    if (arguments.Count() == 1 &&
                        MatchIntValue(dynamic_cast<Syntax::PrimitiveExpression*>(
                                          arguments.At(0)),
                                      value) &&
                        value == 8) {
                        attribute->Remove();
                    }
                } else if (fullName ==
                           "System.Runtime.CompilerServices."
                           "RuntimeCompatibilityAttribute") {
                    if (arguments.Count() != 1) continue;
                    auto* expr1 = dynamic_cast<Syntax::NamedExpression*>(
                        arguments.At(0));
                    if (expr1 == nullptr || expr1->Name() != "WrapNonExceptionThrows") {
                        continue;
                    }
                    bool value = false;
                    if (!(dynamic_cast<Syntax::PrimitiveExpression*>(
                              expr1->Expression()) != nullptr &&
                          MatchBoolValue(dynamic_cast<Syntax::PrimitiveExpression*>(
                                             expr1->Expression()),
                                         value) &&
                          value)) {
                        continue;
                    }
                    attribute->Remove();
                } else if (fullName ==
                           "System.Runtime.Versioning.TargetFrameworkAttribute") {
                    attribute->Remove();
                } else if (fullName ==
                           "System.Security.Permissions."
                           "SecurityPermissionAttribute") {
                    if (arguments.Count() != 2) continue;
                    auto* expr1 =
                        dynamic_cast<Syntax::MemberReferenceExpression*>(arguments.At(0));
                    if (expr1 == nullptr || expr1->MemberName() != "RequestMinimum") {
                        continue;
                    }
                    auto* expr2 =
                        dynamic_cast<Syntax::NamedExpression*>(expr1->NextSibling());
                    if (expr2 == nullptr || expr2->Name() != "SkipVerification") {
                        continue;
                    }
                    bool value2 = false;
                    if (!(dynamic_cast<Syntax::PrimitiveExpression*>(
                              expr2->Expression()) != nullptr &&
                          MatchBoolValue(dynamic_cast<Syntax::PrimitiveExpression*>(
                                             expr2->Expression()),
                                         value2) &&
                          value2)) {
                        continue;
                    }
                    attribute->Remove();
                }
            }
        } else if (target == "module") {
            auto& attributes = section->Attributes();
            for (int i = 0; i < attributes.Count(); i++) {
                Syntax::Attribute* attribute = attributes.At(i);
                const TS::IType* type = GetAttributeType(*attribute);
                if (type == nullptr) continue;
                // The C# `trr.Type.FullName` -- the port's IType carries
                // ReflectionName (the namespace-qualified name; these
                // attribute types are all top-level, so the reflection name
                // IS the full name).
                // The C# `trr.Type.FullName` (see the assembly branch).
                const std::string fullName = type->ReflectionName();
                if (fullName == "System.Security.UnverifiableCodeAttribute" ||
                    fullName == "System.Runtime.CompilerServices.RefSafetyRulesAttribute") {
                    attribute->Remove();
                }
            }
        } else {
            child = next;
            continue;
        }
        // The C# `if (section.Attributes.Count == 0) section.Remove()`.
        if (section->Attributes().Count() == 0) {
            section->Remove();
        }
        child = next;
    }
}

const std::set<std::string>& RemoveEmbeddedAttributes::AttributeNames() {
    static const std::set<std::string> names = {
        "System.Runtime.CompilerServices.IsReadOnlyAttribute",
        "System.Runtime.CompilerServices.IsByRefLikeAttribute",
        "System.Runtime.CompilerServices.IsUnmanagedAttribute",
        "System.Runtime.CompilerServices.NullableAttribute",
        "System.Runtime.CompilerServices.NullableContextAttribute",
        "System.Runtime.CompilerServices.NativeIntegerAttribute",
        "System.Runtime.CompilerServices.ParamCollectionAttribute",
        "System.Runtime.CompilerServices.RefSafetyRulesAttribute",
        "System.Runtime.CompilerServices.ScopedRefAttribute",
        "System.Runtime.CompilerServices.RequiresLocationAttribute",
        "Microsoft.CodeAnalysis.EmbeddedAttribute",
    };
    return names;
}

const std::set<std::string>& RemoveEmbeddedAttributes::NonEmbeddedAttributeNames() {
    static const std::set<std::string> names = {
        // non-embedded attributes, but we still want to remove them
        "System.Runtime.CompilerServices.CompilerFeatureRequiredAttribute",
        "System.Runtime.CompilerServices.RequiredMemberAttribute",
        "System.Runtime.CompilerServices.IsExternalInit",
    };
    return names;
}

void RemoveEmbeddedAttributes::Run(Syntax::AstNode& rootNode,
                                   TransformContext& context) {
    (void)context;
    // The C# DepthFirstAstVisitor.VisitTypeDeclaration override, walked via
    // `rootNode.AcceptVisitor(this)`: a manual pre-order walk here (the
    // port's visitor surface is the DoMatch-side; the visit is expressed as
    // an explicit traversal with the same visit-then-descend order).
    for (Syntax::AstNode* child = rootNode.FirstChild(); child != nullptr;) {
        Syntax::AstNode* next = child->NextSibling();
        auto* typeDeclaration = dynamic_cast<Syntax::TypeDeclaration*>(child);
        if (typeDeclaration != nullptr) {
            const TS::ISymbol* symbol = GetSymbol(*typeDeclaration);
            const auto* typeDefinition =
                symbol != nullptr
                    ? dynamic_cast<const TS::ITypeDefinition*>(symbol)
                    : nullptr;
            if (typeDefinition != nullptr) {
                const std::string fullName = typeDefinition->FullName();
                if (AttributeNames().count(fullName) != 0) {
                    if (!typeDefinition->HasAttribute(TS::KnownAttribute::Embedded)) {
                        // The C# `return` inside VisitTypeDeclaration: keep the
                        // type (but still descend, the C# visitor's early return
                        // skips the children of THIS node only; the sibling walk
                        // here continues to the next sibling).
                        child = next;
                        continue;
                    }
                } else if (NonEmbeddedAttributeNames().count(fullName) == 0) {
                    child = next;
                    continue;
                }
                // Remove the type; an enclosing single-member namespace goes
                // with it (the C# `ns.Remove()`).
                auto* ns = dynamic_cast<Syntax::NamespaceDeclaration*>(
                    typeDeclaration->Parent());
                if (ns != nullptr && ns->Members().Count() == 1) {
                    ns->Remove();
                } else {
                    typeDeclaration->Remove();
                }
                child = next;
                continue;
            }
        }
        // Descend into non-matching children (the visitor's recursion).
        if (child != nullptr) {
            Run(*child, context);
        }
        child = next;
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms