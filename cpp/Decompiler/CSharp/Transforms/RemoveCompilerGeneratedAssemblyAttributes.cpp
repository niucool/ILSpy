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

#include "Decompiler/CSharp/Transforms/RemoveCompilerGeneratedAssemblyAttributes.hpp"

#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstNodeCollection.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/NamedExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include <cstdint>
#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::AstType;
using Syntax::Attribute;
using Syntax::AttributeSection;
using Syntax::Expression;
using Syntax::MemberReferenceExpression;
using Syntax::NamedExpression;
using Syntax::PrimitiveExpression;
namespace Sem = ::ILSpy::Decompiler::Semantics;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace {

// The C# `trr.Type.FullName` read. The port's `IType` does not carry the `AbstractType
// .FullName` property yet (`Namespace`/`FullName` are documented as a deferred part of the
// `IType` surface); the resolved definition's `FullName` is the faithful value for a real
// type reference, and `ReflectionName()` is the fallback for an unresolved one. The two
// coincide for the top-level, non-generic attribute types this transform matches. (The
// `RemoveCLSCompliantAttribute` port carries the same local helper.)
std::string TypeFullName(const TS::IType& type) {
    if (const TS::ITypeDefinition* definition = type.GetDefinition())
        return definition->FullName();
    return type.ReflectionName();
}

// The C# `attribute.Type.Annotation<TypeResolveResult>()`, null-safe. The generated
// `Attribute` requires a `Type`, but a hand-built tree (or a partially constructed node) may
// leave it unset; the C# dereferences and would throw, while the port skips the malformed
// node.
const Sem::TypeResolveResult* ResolveResultOf(const Attribute* attribute) {
    AstType* typeNode = attribute->Type();
    if (typeNode == nullptr)
        return nullptr;
    return typeNode->Annotation<Sem::TypeResolveResult>();
}

// The C# `expr.Value is int value` test. The port's `PrimitiveExpression.Value` is the
// `PrimitiveValue` variant; the C# `int` boxes to the `std::int32_t` alternative (the small
// integer types are widened to int/uint before boxing, so a literal `8` is a `std::int32_t`).
const std::int32_t* IntValue(const PrimitiveExpression* expression) {
    if (expression == nullptr)
        return nullptr;
    return std::get_if<std::int32_t>(&expression->Value());
}

// The C# `expr.Value is bool value` test.
const bool* BoolValue(const PrimitiveExpression* expression) {
    if (expression == nullptr)
        return nullptr;
    return std::get_if<bool>(&expression->Value());
}

// The C# `System.Diagnostics.DebuggableAttribute` arm: remove unconditionally.
bool IsDebuggable(const std::string& fullName) {
    return fullName == "System.Diagnostics.DebuggableAttribute";
}

// The C# `System.Runtime.Versioning.TargetFrameworkAttribute` arm: remove unconditionally.
bool IsTargetFramework(const std::string& fullName) {
    return fullName == "System.Runtime.Versioning.TargetFrameworkAttribute";
}

// The C# `System.Runtime.CompilerServices.CompilationRelaxationsAttribute` arm:
// `arguments.Count == 1 && arguments.First() is PrimitiveExpression expr && expr.Value is int
// value && value == 8`.
bool IsCompilationRelaxations(const std::string& fullName, const Attribute* attribute) {
    if (fullName != "System.Runtime.CompilerServices.CompilationRelaxationsAttribute")
        return false;
    const auto& arguments = attribute->Arguments();
    if (arguments.Count() != 1)
        return false;
    auto* expression = dynamic_cast<PrimitiveExpression*>(arguments.At(0));
    const std::int32_t* value = IntValue(expression);
    return value != nullptr && *value == 8;
}

// The C# `System.Runtime.CompilerServices.RuntimeCompatibilityAttribute` arm:
// `arguments.Count == 1`, `arguments.First() is NamedExpression expr1`, `expr1.Name ==
// "WrapNonExceptionThrows"`, `expr1.Expression is PrimitiveExpression expr2`,
// `expr2.Value is bool value`, `value == true`.
bool IsRuntimeCompatibility(const std::string& fullName, const Attribute* attribute) {
    if (fullName != "System.Runtime.CompilerServices.RuntimeCompatibilityAttribute")
        return false;
    const auto& arguments = attribute->Arguments();
    if (arguments.Count() != 1)
        return false;
    auto* expr1 = dynamic_cast<NamedExpression*>(arguments.At(0));
    if (expr1 == nullptr || expr1->Name() != "WrapNonExceptionThrows")
        return false;
    auto* expr2 = dynamic_cast<PrimitiveExpression*>(expr1->Expression());
    const bool* value = BoolValue(expr2);
    return value != nullptr && *value == true;
}

// The C# `System.Security.Permissions.SecurityPermissionAttribute` arm:
// `arguments.Count == 2`, `arguments.First() is MemberReferenceExpression expr1`,
// `expr1.MemberName == "RequestMinimum"`, `expr1.NextSibling is NamedExpression expr2`,
// `expr2.Name == "SkipVerification"`, `expr2.Expression is PrimitiveExpression expr3`,
// `expr3.Value is bool value2`, `value2 == true`.
bool IsSecurityPermission(const std::string& fullName, const Attribute* attribute) {
    if (fullName != "System.Security.Permissions.SecurityPermissionAttribute")
        return false;
    const auto& arguments = attribute->Arguments();
    if (arguments.Count() != 2)
        return false;
    auto* expr1 = dynamic_cast<MemberReferenceExpression*>(arguments.At(0));
    if (expr1 == nullptr || expr1->MemberName() != "RequestMinimum")
        return false;
    auto* expr2 = dynamic_cast<NamedExpression*>(expr1->NextSibling());
    if (expr2 == nullptr || expr2->Name() != "SkipVerification")
        return false;
    auto* expr3 = dynamic_cast<PrimitiveExpression*>(expr2->Expression());
    const bool* value2 = BoolValue(expr3);
    return value2 != nullptr && *value2 == true;
}

// The C# `assembly`-target arm's switch `fullName` cases, returning whether the attribute
// should be removed.
bool ShouldRemoveAssemblyAttribute(const std::string& fullName, const Attribute* attribute) {
    if (IsDebuggable(fullName))
        return true;
    if (IsCompilationRelaxations(fullName, attribute))
        return true;
    if (IsRuntimeCompatibility(fullName, attribute))
        return true;
    if (IsTargetFramework(fullName))
        return true;
    if (IsSecurityPermission(fullName, attribute))
        return true;
    return false;
}

// The C# `module`-target arm's switch `fullName` cases: `UnverifiableCodeAttribute` and
// `RefSafetyRulesAttribute` are removed unconditionally.
bool ShouldRemoveModuleAttribute(const std::string& fullName) {
    return fullName == "System.Security.UnverifiableCodeAttribute"
        || fullName == "System.Runtime.CompilerServices.RefSafetyRulesAttribute";
}

} // namespace

void RemoveCompilerGeneratedAssemblyAttributes::Run(AstNode& rootNode, TransformContext& /*context*/) {
    // The C# `foreach (var section in rootNode.Children.OfType<AttributeSection>())`: the
    // direct children only, in document order. The `ChildEnumerator` captures the next
    // sibling before yielding, so removing the current section during the walk is safe.
    for (AstNode* child : rootNode.Children()) {
        auto* section = dynamic_cast<AttributeSection*>(child);
        if (section == nullptr)
            continue;

        // The C# if/else-if/else dispatch over `section.AttributeTarget`. A section with any
        // other target reaches the `continue` and is never cleaned or emptied.
        bool handled = false;
        if (section->AttributeTarget() == "assembly") {
            handled = true;
            auto& attributes = section->Attributes();
            for (int i = 0; i < attributes.Count(); ) {
                Attribute* attribute = attributes.At(i);
                const Sem::TypeResolveResult* trr = ResolveResultOf(attribute);
                if (trr != nullptr
                    && ShouldRemoveAssemblyAttribute(TypeFullName(trr->Type()), attribute)) {
                    // The `Remove()` detaches the attribute, shifting the following elements
                    // down; do not advance the index.
                    attribute->Remove();
                } else {
                    ++i;
                }
            }
        } else if (section->AttributeTarget() == "module") {
            handled = true;
            auto& attributes = section->Attributes();
            for (int i = 0; i < attributes.Count(); ) {
                Attribute* attribute = attributes.At(i);
                const Sem::TypeResolveResult* trr = ResolveResultOf(attribute);
                if (trr != nullptr && ShouldRemoveModuleAttribute(TypeFullName(trr->Type()))) {
                    attribute->Remove();
                } else {
                    ++i;
                }
            }
        }

        if (!handled)
            continue;

        // The C# `if (section.Attributes.Count == 0) section.Remove()`.
        if (section->Attributes().Count() == 0)
            section->Remove();
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
