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

#include "Decompiler/CSharp/Transforms/RenameVisualBasicAnonymousTypes.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Comment.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/NRExtensions.hpp"
#include "Decompiler/TypeSystem/IEntity.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include <algorithm>
#include <string>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::Comment;
using Syntax::Identifier;
using Syntax::TypeDeclaration;
using TypeSystem::IEntity;
using TypeSystem::ITypeDefinition;

namespace {

// The C# local function `IEntity FindEntity(AstNode node)`:
//   return node?.GetSymbol() as IEntity ?? node?.Parent?.GetSymbol() as IEntity;
// The `as IEntity` filter drops a symbol that is not an entity; the `??` then reads the
// PARENT's symbol instead (a field declaration carries its symbol on the
// `FieldDeclaration`, not on the `VariableInitializer` holding the name, so the name's
// parent's parent is the entity-bearing node).
const IEntity* FindEntity(const AstNode* node) {
    if (node == nullptr)
        return nullptr;
    if (const auto* entity =
            dynamic_cast<const IEntity*>(GetSymbol(*node))) {
        return entity;
    }
    if (node->Parent() == nullptr)
        return nullptr;
    return dynamic_cast<const IEntity*>(GetSymbol(*node->Parent()));
}

// The C# `identifier.Name.Contains("$")` (an ordinal substring test).
bool ContainsDollar(const std::string& name) {
    return name.find('$') != std::string::npos;
}

} // namespace

void RenameVisualBasicAnonymousTypes::Run(AstNode& rootNode, TransformContext& /*context*/) {
    // The C# `foreach (var identifier in rootNode.DescendantsAndSelf.OfType<Identifier>())`:
    // the materialized pre-order walk (the port's `DescendantsAndSelf` returns a vector, so
    // renaming an identifier in place cannot disturb the iteration).
    for (AstNode* node : rootNode.DescendantsAndSelf()) {
        auto* identifier = dynamic_cast<Identifier*>(node);
        if (identifier == nullptr)
            continue;
        // The C# `if (!identifier.Name.Contains("$")) continue;`.
        if (!ContainsDollar(identifier->Name()))
            continue;
        // The C# `if (FindEntity(identifier.Parent) is not IEntity entity ||
        // entity.Name != identifier.Name) continue;` -- the symbol must name the same
        // identifier (a reference through a different-named alias is left alone).
        const IEntity* entity = FindEntity(identifier->Parent());
        if (entity == nullptr || entity->Name() != identifier->Name())
            continue;
        // The C# `var declaringType = entity as ITypeDefinition ??
        // entity.DeclaringTypeDefinition;` -- a type's own identifier resolves to the
        // type, a member's to its declaring type.
        const ITypeDefinition* declaringType =
            dynamic_cast<const ITypeDefinition*>(entity);
        if (declaringType == nullptr)
            declaringType = entity->DeclaringTypeDefinition();
        if (declaringType == nullptr || !IsAnonymousTypeDeclaredAsNamedType(*declaringType))
            continue;
        // The C# `identifier.Name = identifier.Name.Replace('$', '_');`.
        std::string renamed = identifier->Name();
        std::replace(renamed.begin(), renamed.end(), '$', '_');
        identifier->Name(std::move(renamed));
    }

    // The C# `foreach (var typeDeclaration in rootNode.DescendantsAndSelf.OfType
    // <TypeDeclaration>())`: leading-comment every anonymous type that keeps its own
    // declaration, explaining why it is not written as a C# anonymous type.
    for (AstNode* node : rootNode.DescendantsAndSelf()) {
        auto* typeDeclaration = dynamic_cast<TypeDeclaration*>(node);
        if (typeDeclaration == nullptr)
            continue;
        const auto* type = dynamic_cast<const ITypeDefinition*>(
            GetSymbol(*typeDeclaration));
        if (type == nullptr || !IsAnonymousTypeDeclaredAsNamedType(*type))
            continue;
        // The three C# `AddLeadingTrivia(new Comment(...))` calls, appended in order so
        // the leading list holds them top-to-bottom.
        typeDeclaration->AddLeadingTrivia(new Comment(
            " A VB anonymous type. Its properties are settable and only those declared 'Key'"));
        typeDeclaration->AddLeadingTrivia(new Comment(
            " take part in Equals and GetHashCode, so it cannot be written as a C# anonymous"));
        typeDeclaration->AddLeadingTrivia(new Comment(
            " type and is declared here instead."));
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
