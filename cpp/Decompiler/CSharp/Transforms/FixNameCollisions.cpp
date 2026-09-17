// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
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

#include "Decompiler/CSharp/Transforms/FixNameCollisions.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/AstType.hpp"
#include "Decompiler/CSharp/Syntax/EntityDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Identifier.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/ISymbol.hpp"

#include <string>
#include <unordered_map>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::AstType;
using Syntax::EntityDeclaration;
using Syntax::FieldDeclaration;
using Syntax::Identifier;
using Syntax::IdentifierExpression;
using Syntax::MemberReferenceExpression;
using Syntax::TypeDeclaration;
using Syntax::VariableInitializer;

// The port's `TypeSystem` names live under `ILSpy::Decompiler::TypeSystem`; the C#
// `TypeSystem` sub-namespace of `CSharp` (pulled in by `TransformContext.hpp`) would shadow
// the unqualified name here, so the alias makes the intent explicit.
namespace TS = ::ILSpy::Decompiler::TypeSystem;
using TS::Accessibility;
using TS::IField;
using TS::ISymbol;

std::string FixNameCollisions::PickNewName(const std::set<std::string>& memberNames,
                                           const std::string& name) {
    // The C# `if (!memberNames.Contains("m_" + name)) return "m_" + name;`.
    const std::string prefixed = "m_" + name;
    if (memberNames.find(prefixed) == memberNames.end())
        return prefixed;
    // The C# `for (int num = 2; ; num++) { string newName = name + num; if
    // (!memberNames.Contains(newName)) return newName; }` -- an unbounded loop (a free
    // candidate always exists since the set is finite). `memberNames` is NOT updated with
    // the newly picked name during the walk, matching the C# (a later collision with a
    // re-used candidate is impossible within one type because `memberNames` is the fixed
    // pre-rename snapshot).
    for (int num = 2;; num++) {
        const std::string newName = name + std::to_string(num);
        if (memberNames.find(newName) == memberNames.end())
            return newName;
    }
}

void FixNameCollisions::Run(AstNode& rootNode, TransformContext& context) {
    // The C# `var renamedSymbols = new Dictionary<ISymbol, string>();` -- keyed by symbol
    // IDENTITY (the C# dictionary's default reference comparer). The port keys on the
    // `ISymbol*` the resolve-result annotation carries, which is stable across the
    // declaration and its references for one entity.
    std::unordered_map<const ISymbol*, std::string> renamedSymbols;

    // The C# `foreach (var typeDecl in rootNode.DescendantsAndSelf.OfType<TypeDeclaration>())`.
    for (AstNode* node : rootNode.DescendantsAndSelf()) {
        auto* typeDecl = dynamic_cast<TypeDeclaration*>(node);
        if (typeDecl == nullptr)
            continue;

        // The C# `var memberNames = typeDecl.Members.Select(m => { var type =
        // m.GetChild(Slots.PrivateImplementationType); return type is null ? m.Name :
        // type + "." + m.Name; }).ToHashSet();` -- an explicit-interface member is keyed by
        // its `I.Name` form, so a private field named `Name` still collides with it below
        // (the field name is a bare identifier, the member key is prefixed by the interface
        // type).
        std::set<std::string> memberNames;
        auto& members = typeDecl->Members();
        for (int i = 0; i < members.Count(); i++) {
            EntityDeclaration* member = members.At(i);
            AstType* privateImplementationType =
                member->GetChildByKind(&Syntax::Slots::PrivateImplementationType);
            if (privateImplementationType == nullptr)
                memberNames.insert(member->Name());
            else
                memberNames.insert(privateImplementationType->ToString() + "." + member->Name());
        }

        // The C# `foreach (var fieldDecl in typeDecl.Members.OfType<FieldDeclaration>())`:
        // memberNames does not include fields (or non-custom events), because those do not
        // have a single name but a list of `VariableInitializer`s, so the collision is
        // resolved field-side only.
        for (int i = 0; i < members.Count(); i++) {
            auto* fieldDecl = dynamic_cast<FieldDeclaration*>(members.At(i));
            if (fieldDecl == nullptr)
                continue;
            // The C# `if (fieldDecl.Variables.Count != 1) continue;`.
            if (fieldDecl->Variables().Count() != 1)
                continue;
            VariableInitializer* variable = fieldDecl->Variables().At(0);
            const std::string oldName = variable->Name();
            // The C# `ISymbol? symbol = fieldDecl.GetSymbol();`.
            const ISymbol* symbol = GetSymbol(*fieldDecl);
            // The C# `if (memberNames.Contains(oldName) && symbol is IField { Accessibility:
            // Accessibility.Private })`: the symbol's `Accessibility` is the C# `Accessibility.Private`
            // value; the C++ `Accessibility()` override reports the raw column value.
            const auto* field = dynamic_cast<const IField*>(symbol);
            if (memberNames.find(oldName) != memberNames.end() && field != nullptr
                && field->Accessibility() == Accessibility::Private) {
                const std::string newName = PickNewName(memberNames, oldName);
                context.Step("Rename field '" + oldName + "' to '" + newName + "'", fieldDecl);
                variable->Name(newName);
                renamedSymbols[symbol] = newName;
            }
        }
    }

    // The C# `foreach (var node in rootNode.DescendantsAndSelf)`: retarget every reference
    // to a renamed field -- `node.GetChild(Slots.Identifier)!.Name = newName` mutates the
    // reference's name token in place (an IdentifierExpression / MemberReferenceExpression
    // always carries its name identifier).
    for (AstNode* node : rootNode.DescendantsAndSelf()) {
        if (dynamic_cast<IdentifierExpression*>(node) == nullptr
            && dynamic_cast<MemberReferenceExpression*>(node) == nullptr)
            continue;
        // The C# `ISymbol? symbol = node.GetSymbol();`.
        const ISymbol* symbol = GetSymbol(*node);
        if (symbol == nullptr)
            continue;
        auto it = renamedSymbols.find(symbol);
        if (it == renamedSymbols.end())
            continue;
        context.Step("Rename field reference to '" + it->second + "'", node);
        Identifier* identifier = node->GetChildByKind(&Syntax::Slots::Identifier);
        // The C# `node.GetChild(Slots.Identifier)!` (non-null for both node kinds).
        identifier->Name(it->second);
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
