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

#include "Decompiler/CSharp/Transforms/FixNameCollisions.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/TypeSystem/IField.hpp"

#include <map>
#include <string>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {
namespace {

namespace Syntax = ::ILSpy::Decompiler::CSharp::Syntax;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// The C# `string PickNewName(ISet<string> memberNames, string name)` (a
// private instance method -- the port's free function; the set is read-only
// here so the const std::map& carries it).
std::string PickNewName(const std::map<std::string, bool, std::less<>>& memberNames,
                        const std::string& name) {
    if (memberNames.find("m_" + name) == memberNames.end())
        return "m_" + name;
    for (int num = 2;; num++) {
        std::string newName = name + std::to_string(num);
        if (memberNames.find(newName) == memberNames.end())
            return newName;
    }
}

} // namespace

void FixNameCollisions::Run(Syntax::AstNode& rootNode, TransformContext& context) {
    // The C# `Dictionary<ISymbol, string>` keyed by the symbol pointer (the
    // port's ISymbol has no Equals-based dictionary key; the symbol instances
    // are singletons per compilation the C# reference equality maps onto).
    std::map<const TS::ISymbol*, std::string, std::less<>> renamedSymbols;
    for (Syntax::AstNode* node : rootNode.DescendantsAndSelf()) {
        auto* typeDecl = dynamic_cast<Syntax::TypeDeclaration*>(node);
        if (typeDecl == nullptr) continue;
        // The C# `typeDecl.Members.Select(m => m.GetChild(Slots.
        // PrivateImplementationType) is null ? m.Name : type + "." + m.Name)`
        // -- the explicit-interface members carry the "Iface.Member" shape.
        // The port's GetChild is index-based, so the scan walks the member's
        // children and matches the slot kind.
        std::map<std::string, bool, std::less<>> memberNames;
        for (int mi = 0; mi < typeDecl->Members().Count(); ++mi) {
            Syntax::EntityDeclaration* m = typeDecl->Members().At(mi);
            bool hasPrivateImplementationType = false;
            for (int ci = 0; ci < m->GetChildCount(); ++ci) {
                const Syntax::CSharpSlotInfo* slot = m->GetChildSlotInfo(ci);
                if (slot != nullptr &&
                    slot->Kind() == &Syntax::Slots::PrivateImplementationType &&
                    m->GetChild(ci) != nullptr) {
                    hasPrivateImplementationType = true;
                    break;
                }
            }
            memberNames[hasPrivateImplementationType
                            ? std::string("")  // the C# `type + "." + m.Name`;
                                               // the prefixed name cannot equal a
                                               // bare field name
                            : m->Name()] = true;
        }
        // memberNames does not include fields or non-custom events because
        // those don't have a single name, but a list of VariableInitializers.
        for (int mi = 0; mi < typeDecl->Members().Count(); ++mi) {
            auto* fieldDecl = dynamic_cast<Syntax::FieldDeclaration*>(typeDecl->Members().At(mi));
            if (fieldDecl == nullptr) continue;
            if (fieldDecl->Variables().Count() != 1) continue;
            const std::string oldName = fieldDecl->Variables().At(0)->Name();
            const TS::ISymbol* symbol = ::ILSpy::Decompiler::CSharp::GetSymbol(*fieldDecl);
            const auto* field = symbol != nullptr
                                    ? dynamic_cast<const TS::IField*>(symbol)
                                    : nullptr;
            if (memberNames.find(oldName) != memberNames.end() && field != nullptr &&
                field->Accessibility() == TS::Accessibility::Private) {
                const std::string newName = PickNewName(memberNames, oldName);
                context.StepOnce("Rename field '" + oldName + "' to '" + newName + "'",
                                 fieldDecl);
                fieldDecl->Variables().At(0)->Name(newName);
                renamedSymbols[symbol] = newName;
            }
        }
    }

    for (Syntax::AstNode* node : rootNode.DescendantsAndSelf()) {
        if (dynamic_cast<Syntax::IdentifierExpression*>(node) != nullptr ||
            dynamic_cast<Syntax::MemberReferenceExpression*>(node) != nullptr) {
            const TS::ISymbol* symbol = ::ILSpy::Decompiler::CSharp::GetSymbol(*node);
            if (symbol != nullptr) {
                auto it = renamedSymbols.find(symbol);
                if (it != renamedSymbols.end()) {
                    // An IdentifierExpression / MemberReferenceExpression
                    // always carries its name identifier.
                    context.StepOnce("Rename field reference to '" + it->second + "'",
                                     node);
                    // The C# `node.GetChild(Slots.Identifier)!.Name = newName`:
                    // walk the children for the Identifier-kind slot.
                    for (int ci = 0; ci < node->GetChildCount(); ++ci) {
                        const Syntax::CSharpSlotInfo* slot = node->GetChildSlotInfo(ci);
                        if (slot != nullptr &&
                            slot->Kind() == &Syntax::Slots::Identifier) {
                            if (auto* identifier = dynamic_cast<Syntax::Identifier*>(
                                    node->GetChild(ci))) {
                                identifier->Name(it->second);
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
}

} // namespace ILSpy::Decompiler::CSharp::Transforms