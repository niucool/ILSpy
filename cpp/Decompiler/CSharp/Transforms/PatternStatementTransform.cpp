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

#include "Decompiler/CSharp/Transforms/PatternStatementTransform.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Syntax/Accessor.hpp"
#include "Decompiler/CSharp/Syntax/AstNode.hpp"
#include "Decompiler/CSharp/Syntax/Attribute.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/CatchClause.hpp"
#include "Decompiler/CSharp/Syntax/DestructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/DirectionExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/Expression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IndexerExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/PrimitiveExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/MethodDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"
#include "Decompiler/CSharp/Syntax/PrimitiveType.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/SimpleType.hpp"
#include "Decompiler/CSharp/Syntax/SingleVariableDesignation.hpp"
#include "Decompiler/CSharp/Syntax/Slots.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ContinueStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/DoWhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/FixedStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ForeachStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/IfElseStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ReturnStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/TryCatchStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/UsingStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/VariableDeclarationStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/WhileStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/IL/ILVariable.hpp"
#include "Decompiler/NRExtensions.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"

#include <charconv>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::AssignmentExpression;
using Syntax::AssignmentOperatorType;
using Syntax::BinaryOperatorExpression;
using Syntax::BinaryOperatorType;
using Syntax::ContinueStatement;
using Syntax::DoWhileStatement;
using Syntax::Expression;
using Syntax::ExpressionStatement;
using Syntax::ForStatement;
using Syntax::ForeachStatement;
using Syntax::IdentifierExpression;
using Syntax::IndexerExpression;
using Syntax::PrimitiveExpression;
using Syntax::Statement;
using Syntax::UnaryOperatorExpression;
using Syntax::UnaryOperatorType;
using Syntax::WhileStatement;

namespace {

namespace PM = Syntax::PatternMatching;

// The real type-system namespace alias (the sibling `CSharp::TypeSystem` namespace pulled in by
// the TypeSystemAstBuilder / TransformContext headers shadows the plain `TypeSystem::` lookup).
namespace TS = ::ILSpy::Decompiler::TypeSystem;

// Owns the nodes of a pattern tree built for one sub-transform invocation. The C# pattern
// definitions are `static readonly` (process lifetime); the port rebuilds an equivalent tree
// per call and keeps every node in this holder, so the non-owning child pointers the pattern
// nodes carry stay valid for the whole match. Nodes are owned through `INode` (both `Pattern`
// and `AstNode` derive from it, and it has a virtual destructor). A pattern node wrapped in a
// `PatternPlaceholderNode` is allocated as a `shared_ptr` by the caller and owned by the
// placeholder, so it is NOT put in this holder.
class PatternTree {
public:
    template <class T, class... Args>
    T* Make(Args&&... args) {
        auto node = std::make_unique<T>(std::forward<Args>(args)...);
        T* result = node.get();
        nodes_.push_back(std::move(node));
        return result;
    }

    // The generated `implicit operator <TNode>(Pattern)`: wrap a pattern in a placeholder so
    // it can occupy an AST slot while still matching (PatternPlaceholder.hpp).
    template <class TNode>
    TNode* Wrap(std::shared_ptr<PM::Pattern> pattern) {
        return Make<Syntax::PatternPlaceholderNode<TNode>>(std::move(pattern));
    }

private:
    std::vector<std::unique_ptr<PM::INode>> nodes_;
};

// The C# `int.TryParse(index.Value?.ToString() ?? "", out int index)` on the captured
// `GetUpperBound`/`GetLowerBound` argument. The port's `PrimitiveValue` has no `ToString`; the
// integer alternatives (and a string literal, whose `ToString` is itself) are rendered
// invariantly and parsed back. Every other alternative (null/bool/char/floating/decimal)
// renders to text that is not a valid `int`, so it does not parse -- the same outcome as the
// C# `TryParse`.
std::optional<int> TryParsePrimitiveAsInt(const Syntax::PrimitiveValue& value) {
    std::string text;
    if (auto* v = std::get_if<std::int32_t>(&value))
        text = std::to_string(*v);
    else if (auto* v = std::get_if<std::uint32_t>(&value))
        text = std::to_string(*v);
    else if (auto* v = std::get_if<std::int64_t>(&value))
        text = std::to_string(*v);
    else if (auto* v = std::get_if<std::uint64_t>(&value))
        text = std::to_string(*v);
    else if (auto* v = std::get_if<std::string>(&value))
        text = *v;
    else
        return std::nullopt;
    if (text.empty())
        return std::nullopt;
    int result = 0;
    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (error != std::errc() || end != text.data() + text.size())
        return std::nullopt;
    return result;
}

// The C# `static readonly AstNode variableAssignUpperBoundPattern`:
// `$variable = $collection.GetUpperBound($index);`. Built into `tree` each call.
Syntax::ExpressionStatement* BuildVariableAssignUpperBoundPattern(PatternTree& tree) {
    auto* memberReference = tree.Make<Syntax::MemberReferenceExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("collection",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
        std::string("GetUpperBound"));
    auto* invocation = tree.Make<Syntax::InvocationExpression>(memberReference);
    invocation->Arguments().Add(tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>(
        "index", tree.Make<Syntax::PrimitiveExpression>(Syntax::PrimitiveExpression::AnyValue()))));
    auto* assign = tree.Make<Syntax::AssignmentExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("variable",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
        invocation);
    return tree.Make<Syntax::ExpressionStatement>(assign);
}

// The C# `static readonly ExpressionStatement variableAssignLowerBoundPattern`:
// `$variable = $collection.GetLowerBound($index);`. Built into `tree` each call.
Syntax::ExpressionStatement* BuildVariableAssignLowerBoundPattern(PatternTree& tree) {
    auto* memberReference = tree.Make<Syntax::MemberReferenceExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("collection",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
        std::string("GetLowerBound"));
    auto* invocation = tree.Make<Syntax::InvocationExpression>(memberReference);
    invocation->Arguments().Add(tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>(
        "index", tree.Make<Syntax::PrimitiveExpression>(Syntax::PrimitiveExpression::AnyValue()))));
    auto* assign = tree.Make<Syntax::AssignmentExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("variable",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
        invocation);
    return tree.Make<Syntax::ExpressionStatement>(assign);
}

// The C# `static readonly ForStatement forOnArrayMultiDimPattern`: a lower-bound-initialized
// `for` loop `for ($i = <lowerBoundAssign's index>; $i <= $upperBoundVariable; $i++) { ... }`
// whose body starts with `$lowerBoundAssign` (the next dimension's lower bound, or the element
// assignment for the innermost dimension). Built into `tree` each call.
Syntax::ForStatement* BuildForOnArrayMultiDimPattern(PatternTree& tree) {
    auto* pattern = tree.Make<Syntax::ForStatement>();
    auto* condition = tree.Make<Syntax::BinaryOperatorExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("indexVariable",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
        Syntax::BinaryOperatorType::LessThanOrEqual,
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("upperBoundVariable",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))));
    pattern->Condition(condition);
    auto* iterRight = tree.Make<Syntax::BinaryOperatorExpression>(
        tree.Wrap<Syntax::Expression>(
            std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")),
        Syntax::BinaryOperatorType::Add,
        tree.Make<Syntax::PrimitiveExpression>(Syntax::PrimitiveValue(std::int32_t(1))));
    pattern->Iterators().Add(tree.Make<Syntax::ExpressionStatement>(tree.Make<Syntax::AssignmentExpression>(
        tree.Wrap<Syntax::Expression>(
            std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")),
        iterRight)));
    auto* bodyBlock = tree.Make<Syntax::BlockStatement>();
    bodyBlock->Statements().Add(
        tree.Wrap<Syntax::Statement>(std::make_shared<PM::AnyNode>("lowerBoundAssign")));
    bodyBlock->Statements().Add(tree.Wrap<Syntax::Statement>(
        std::make_shared<PM::Repeat>(tree.Make<PM::AnyNode>("statements"))));
    pattern->EmbeddedStatement(bodyBlock);
    return pattern;
}

// The C# `static readonly ExpressionStatement foreachVariableOnMultArrayAssignPattern`:
// `$variable = $collection[$index1, $index2, ...];`. Built into `tree` each call.
Syntax::ExpressionStatement* BuildForeachVariableOnMultArrayAssignPattern(PatternTree& tree) {
    auto* indexer = tree.Make<Syntax::IndexerExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("collection",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))));
    indexer->Arguments().Add(tree.Wrap<Syntax::Expression>(std::make_shared<PM::Repeat>(
        tree.Make<PM::NamedNode>("index",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString))))));
    auto* assign = tree.Make<Syntax::AssignmentExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("variable",
            tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
        indexer);
    return tree.Make<Syntax::ExpressionStatement>(assign);
}

// The C# `static readonly ForStatement forOnInlineArrayPattern`: the compiler's inline-array
// loop `for ($index = 0; $index < $length; $index = $index + 1) { $item = $elementAccess;
// <statements>* }`. Built into `tree` each call.
Syntax::ForStatement* BuildForOnInlineArrayPattern(PatternTree& tree) {
    auto* pattern = tree.Make<Syntax::ForStatement>();
    auto* indexIdent = tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString));
    pattern->Initializers().Add(tree.Make<Syntax::ExpressionStatement>(
        tree.Make<Syntax::AssignmentExpression>(
            tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("indexVariable", indexIdent)),
            tree.Make<Syntax::PrimitiveExpression>(Syntax::PrimitiveValue(std::int32_t(0))))));
    pattern->Condition(tree.Make<Syntax::BinaryOperatorExpression>(
        tree.Wrap<Syntax::Expression>(
            std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")),
        Syntax::BinaryOperatorType::LessThan,
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("length",
            tree.Make<Syntax::PrimitiveExpression>(Syntax::PrimitiveExpression::AnyValue())))));
    auto* iterRight = tree.Make<Syntax::BinaryOperatorExpression>(
        tree.Wrap<Syntax::Expression>(
            std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")),
        Syntax::BinaryOperatorType::Add,
        tree.Make<Syntax::PrimitiveExpression>(Syntax::PrimitiveValue(std::int32_t(1))));
    pattern->Iterators().Add(tree.Make<Syntax::ExpressionStatement>(tree.Make<Syntax::AssignmentExpression>(
        tree.Wrap<Syntax::Expression>(
            std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")),
        iterRight)));
    auto* bodyBlock = tree.Make<Syntax::BlockStatement>();
    bodyBlock->Statements().Add(tree.Make<Syntax::ExpressionStatement>(
        tree.Make<Syntax::AssignmentExpression>(
            tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("itemVariable",
                tree.Make<Syntax::IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
            tree.Wrap<Syntax::Expression>(std::make_shared<PM::NamedNode>("elementAccess",
                tree.Make<PM::AnyNode>())))));
    bodyBlock->Statements().Add(tree.Wrap<Syntax::Statement>(
        std::make_shared<PM::Repeat>(tree.Make<PM::AnyNode>("statements"))));
    pattern->EmbeddedStatement(bodyBlock);
    return pattern;
}

// The C# `static readonly BlockStatement destructorBodyPattern` -- the shared body shape
// `try { <body> } finally { base.Finalize(); }`. Built into `tree` each call.
Syntax::BlockStatement* BuildDestructorBodyPattern(PatternTree& tree) {
    auto* bodyPattern = tree.Make<Syntax::BlockStatement>();
    auto* tryCatch = tree.Make<Syntax::TryCatchStatement>();
    tryCatch->TryBlock(tree.Wrap<Syntax::BlockStatement>(std::make_shared<PM::AnyNode>("body")));
    auto* finallyBlock = tree.Make<Syntax::BlockStatement>();
    auto* baseReference = tree.Make<Syntax::BaseReferenceExpression>();
    auto* memberReference = tree.Make<Syntax::MemberReferenceExpression>(
        baseReference, std::string("Finalize"));
    finallyBlock->Statements().Add(
        tree.Make<Syntax::ExpressionStatement>(tree.Make<Syntax::InvocationExpression>(memberReference)));
    tryCatch->FinallyBlock(finallyBlock);
    bodyPattern->Statements().Add(tryCatch);
    return bodyPattern;
}

// The C# `static readonly Expression addressOfPinnableReference` -- the shared pattern
// `&<target>.GetPinnableReference()` (the C# 7.3 pattern-based-`fixed` shape for value types).
// Built into `tree` each call.
Syntax::Expression* BuildAddressOfPinnableReferencePattern(PatternTree& tree) {
    auto* memberReference = tree.Make<Syntax::MemberReferenceExpression>(
        tree.Wrap<Syntax::Expression>(std::make_shared<PM::AnyNode>("target")),
        std::string("GetPinnableReference"));
    auto* invocation = tree.Make<Syntax::InvocationExpression>(memberReference);
    return tree.Make<Syntax::UnaryOperatorExpression>(
        invocation, Syntax::UnaryOperatorType::AddressOf);
}

// The C# `static readonly Regex automaticPropertyBackingFieldNameRegex = new Regex(
// @"^(<(?<name>.+)>k__BackingField|_(?<name>.+))$")` -- the compiler backing-field name
// shape: the C# `<Property>k__BackingField` form or the VB `_Property` form. Returns the
// extracted property name through the out-parameter (the C# `out string? propertyName`).
bool NameCouldBeBackingFieldOfAutomaticProperty(const std::string& name,
                                                std::string& propertyName) {
    const std::string suffix = ">k__BackingField";
    if (name.size() >= 2 && name.front() == '<'
        && name.size() > 1 + suffix.size()
        && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
        // The `(?<name>.+)` group is greedy but the trailing `>k__BackingField` anchors it: the
        // name is everything between the leading `<` and the final `>k__BackingField`.
        const std::size_t end = name.size() - suffix.size();
        propertyName = name.substr(1, end - 1);
        return !propertyName.empty();
    }
    if (name.size() >= 2 && name.front() == '_') {
        propertyName = name.substr(1);
        return true;
    }
    return false;
}

// The C# `static void RemoveCompilerGeneratedAttribute(AstNodeCollection<AttributeSection>
// attributeSections, params string[] attributesToRemove)`: removes every attribute whose type
// symbol resolves to one of the full type names, and removes any section left empty.
void RemoveAttributeByName(Syntax::AstNodeCollectionT<Syntax::AttributeSection>& attributeSections,
                           const std::vector<std::string>& attributesToRemove) {
    for (int i = 0; i < attributeSections.Count(); ) {
        Syntax::AttributeSection* section = attributeSections[i];
        Syntax::AstNodeCollectionT<Syntax::Attribute>& attributes = section->Attributes();
        for (int j = 0; j < attributes.Count(); ) {
            Syntax::Attribute* attr = attributes[j];
            const Syntax::AstNode* typeNode = attr->Type();
            const TS::ISymbol* symbol = typeNode != nullptr ? GetSymbol(*typeNode) : nullptr;
            const auto* type = dynamic_cast<const TS::INamedElement*>(symbol);
            bool remove = false;
            if (type != nullptr) {
                const std::string fullName = type->FullName();
                for (const std::string& candidate : attributesToRemove) {
                    if (fullName == candidate) {
                        remove = true;
                        break;
                    }
                }
            }
            if (remove) {
                attr->Remove();
            } else {
                j++;
            }
        }
        if (section->Attributes().Count() == 0) {
            section->Remove();
        } else {
            i++;
        }
    }
}

// The C# `static void RemoveCompilerGeneratedAttribute(AstNodeCollection<AttributeSection>)` --
// the single-attribute convenience over the full-name table.
void RemoveCompilerGeneratedAttribute(
    Syntax::AstNodeCollectionT<Syntax::AttributeSection>& attributeSections) {
    RemoveAttributeByName(attributeSections,
                          {"System.Runtime.CompilerServices.CompilerGeneratedAttribute"});
}

// The C# `internal static bool RemoveAttribute(EntityDeclaration entityDecl, KnownAttribute
// attributeType)` (CSharpDecompiler.cs): removes every attribute whose type resolves to the
// known attribute's metadata type, and removes any section left empty.
bool RemoveKnownAttribute(Syntax::EntityDeclaration& entityDecl, TS::KnownAttribute attributeType) {
    bool found = false;
    Syntax::AstNodeCollectionT<Syntax::AttributeSection>& sections = entityDecl.Attributes();
    for (int i = 0; i < sections.Count(); ) {
        Syntax::AttributeSection* section = sections[i];
        Syntax::AstNodeCollectionT<Syntax::Attribute>& attributes = section->Attributes();
        for (int j = 0; j < attributes.Count(); ) {
            Syntax::Attribute* attr = attributes[j];
            const Syntax::AstNode* typeNode = attr->Type();
            const auto* type = typeNode != nullptr
                ? dynamic_cast<const TS::IType*>(GetSymbol(*typeNode))
                : nullptr;
            if (type != nullptr && TS::IsKnownType(*type, attributeType)) {
                attr->Remove();
                found = true;
            } else {
                j++;
            }
        }
        if (section->Attributes().Count() == 0) {
            section->Remove();
        } else {
            i++;
        }
    }
    return found;
}

// The C# `static readonly PropertyDeclaration automaticPropertyPattern` (and the read-only
// variant): a getter `{ return <field>; }` and (optionally) a setter `{ <field> = value; }`
// whose `Attributes`/`Modifiers`/name are wildcards. Built into `tree` each call.
Syntax::PropertyDeclaration* BuildAutomaticPropertyPattern(PatternTree& tree, bool withSetter) {
    auto* property = tree.Make<Syntax::PropertyDeclaration>();
    auto* propertyAttributeAny = tree.Make<PM::AnyNode>();
    property->Attributes().Add(tree.Wrap<Syntax::AttributeSection>(
        std::make_shared<PM::Repeat>(propertyAttributeAny)));
    property->Modifiers(Syntax::Modifiers::Any);
    property->ReturnType(tree.Wrap<Syntax::AstType>(std::make_shared<PM::AnyNode>()));
    property->PrivateImplementationType(tree.Wrap<Syntax::AstType>(
        std::make_shared<PM::OptionalNode>(tree.Make<PM::AnyNode>())));
    property->Name(PM::Pattern::AnyString);

    auto* getter = tree.Make<Syntax::Accessor>();
    auto* getterAttributeAny = tree.Make<PM::AnyNode>();
    getter->Attributes().Add(tree.Wrap<Syntax::AttributeSection>(
        std::make_shared<PM::Repeat>(getterAttributeAny)));
    getter->Modifiers(Syntax::Modifiers::Any);
    auto* getterBody = tree.Make<Syntax::BlockStatement>();
    auto* returnStatement = tree.Make<Syntax::ReturnStatement>();
    returnStatement->Expression(tree.Wrap<Syntax::Expression>(
        std::make_shared<PM::NamedNode>("fieldReference", tree.Make<PM::AnyNode>())));
    getterBody->Statements().Add(returnStatement);
    getter->Body(getterBody);
    property->Getter(getter);

    if (withSetter) {
        auto* setter = tree.Make<Syntax::Accessor>();
        auto* setterAttributeAny = tree.Make<PM::AnyNode>();
        setter->Attributes().Add(tree.Wrap<Syntax::AttributeSection>(
            std::make_shared<PM::Repeat>(setterAttributeAny)));
        setter->Modifiers(Syntax::Modifiers::Any);
        auto* setterBody = tree.Make<Syntax::BlockStatement>();
        auto* assignment = tree.Make<Syntax::AssignmentExpression>();
        assignment->Left(tree.Wrap<Syntax::Expression>(
            std::make_shared<PM::Backreference>("fieldReference")));
        assignment->Right(tree.Make<Syntax::IdentifierExpression>(std::string("value")));
        setterBody->Statements().Add(tree.Make<Syntax::ExpressionStatement>(assignment));
        setter->Body(setterBody);
        property->Setter(setter);
    }
    return property;
}

} // namespace

void PatternStatementTransform::Run(AstNode& rootNode, TransformContext& context) {
    // The C# `if (this.context != null) throw new InvalidOperationException(
    // "Reentrancy in PatternStatementTransform.Run?")`. The port throws std::logic_error (the
    // port's InvalidOperationException convention).
    if (context_ != nullptr)
        throw std::logic_error("Reentrancy in PatternStatementTransform.Run?");
    context_ = &context;
    try {
        Initialize(context);
        declareVariables_.Analyze(rootNode);
        rootNode.AcceptVisitorAstNode(*this);
    } catch (...) {
        context_ = nullptr;
        Uninitialize();
        declareVariables_.ClearAnalysisResults();
        throw;
    }
    // The C# `finally`: clear the run state and the context slots.
    context_ = nullptr;
    Uninitialize();
    declareVariables_.ClearAnalysisResults();
}

AstNode* PatternStatementTransform::VisitChildren(AstNode* node) {
    // The C# `for (AstNode? child = node.FirstChild; child != null; child =
    // child.NextSibling) { ... do { oldChild = child; child = child.AcceptVisitor(this); }
    // while (child != oldChild); }`. The inner loop re-visits a child while the visit returns
    // a different node, because a sub-transform may delete/replace nodes around the visited
    // one and the returned node is where the walk resumes.
    for (AstNode* child = node->FirstChild(); child != nullptr; child = child->NextSibling()) {
        AstNode* oldChild;
        do {
            oldChild = child;
            child = child->AcceptVisitorAstNode(*this);
            // The C# `Debug.Assert(child != null && child.Parent == node)`: every ported
            // sub-transform returns the (possibly replaced) node, never null. Like the
            // C# assert, the check is compiled out of release builds (the `CheckInvariant`
            // convention: an assert is debug-only, not a runtime guard).
#ifndef NDEBUG
            if (child == nullptr || child->Parent() != node)
                throw std::logic_error(
                    "PatternStatementTransform ran into an inconsistent AST");
#endif
        } while (child != oldChild);
    }
    return node;
}

AstNode* PatternStatementTransform::VisitBinaryOperatorExpression(
    BinaryOperatorExpression* expr) {
    switch (expr->Operator()) {
        case BinaryOperatorType::ConditionalAnd:
        case BinaryOperatorType::ConditionalOr: {
            // a && (b && c) ==> (a && b) && c
            auto* bAndC = dynamic_cast<BinaryOperatorExpression*>(expr->Right());
            if (bAndC != nullptr && bAndC->Operator() == expr->Operator()) {
                context_->Step("Reassociate conditional logic", expr);
                // Make bAndC the parent and expr the child. A conditional-and/or operator
                // always has both operands present.
                Expression* b = Syntax::Detach(bAndC->Left());
                Expression* c = Syntax::Detach(bAndC->Right());
                expr->ReplaceWith(Syntax::Detach(bAndC));
                bAndC->Left(expr);
                bAndC->Right(c);
                expr->Right(b);
                context_->EndStep(bAndC);
                return Syntax::DepthFirstAstVisitorAstNode::VisitBinaryOperatorExpression(bAndC);
            }
            break;
        }
        default:
            break;
    }
    return Syntax::DepthFirstAstVisitorAstNode::VisitBinaryOperatorExpression(expr);
}

AstNode* PatternStatementTransform::VisitUnaryOperatorExpression(
    UnaryOperatorExpression* expr) {
    if (expr->Operator() == UnaryOperatorType::Not) {
        auto* binary = dynamic_cast<BinaryOperatorExpression*>(expr->Expression());
        if (binary != nullptr && binary->Operator() == BinaryOperatorType::Equality) {
            context_->Step("Replace negated equality with inequality", expr);
            binary->Operator(BinaryOperatorType::InEquality);
            expr->ReplaceWith(Syntax::Detach(binary));
            context_->EndStep(binary);
            return VisitBinaryOperatorExpression(binary);
        }
    }
    return Syntax::DepthFirstAstVisitorAstNode::VisitUnaryOperatorExpression(expr);
}

// ---- for ---------------------------------------------------------------------------

AstNode* PatternStatementTransform::VisitExpressionStatement(
    ExpressionStatement* expressionStatement) {
    // The C# `AstNode? result = TransformForeachOnMultiDimArray(expressionStatement); if
    // (result != null) return result;`.
    if (Statement* result = TransformForeachOnMultiDimArray(expressionStatement))
        return result;
    if (ForStatement* forStatement = TransformFor(expressionStatement))
        return forStatement;
    return Syntax::DepthFirstAstVisitorAstNode::VisitExpressionStatement(expressionStatement);
}

AstNode* PatternStatementTransform::VisitForStatement(ForStatement* forStatement) {
    // The C# runs `TransformForeachOnArray(forStatement)` then `TransformForeachOnInlineArray`.
    if (Statement* result = TransformForeachOnArray(forStatement))
        return result;
    if (Statement* result = TransformForeachOnInlineArray(forStatement))
        return result;
    return Syntax::DepthFirstAstVisitorAstNode::VisitForStatement(forStatement);
}

// The C# `ForStatement? TransformFor(ExpressionStatement node)`: turns `var = init; while
// (var <op> end) { ...; var = ...; }` into `for (var = init; var <op> end; var = ...) {...}`,
// and moves a preceding `var = init;` into an existing `for (...)`'s initializer when the
// loop's condition or iterators reference the variable.
ForStatement* PatternStatementTransform::TransformFor(ExpressionStatement* node) {
    if (!context_->Settings().ForStatement())
        return nullptr;

    // `static readonly AstNode variableAssignPattern`: `$variable = $initializer;`.
    IL::ILVariable* variable = nullptr;
    {
        PatternTree tree;
        auto* variableNode = tree.Make<IdentifierExpression>(
            std::string(PM::Pattern::AnyString));
        auto* variableNamed = tree.Wrap<Expression>(
            std::make_shared<PM::NamedNode>("variable", variableNode));
        auto* initializer = tree.Wrap<Expression>(std::make_shared<PM::AnyNode>("initializer"));
        auto* assign = tree.Make<AssignmentExpression>(variableNamed, initializer);
        auto* pattern = tree.Make<ExpressionStatement>(assign);
        PM::Match m1 = PM::PatternExtensions::Match(*pattern, node);
        if (!m1.Success())
            return nullptr;
        variable = GetILVariable(*m1.Get<IdentifierExpression>("variable").front());
    }

    AstNode* next = node->NextSibling();
    if (next == nullptr)
        return nullptr;

    if (auto* forStatement = dynamic_cast<ForStatement*>(next)) {
        if (ForStatementUsesVariable(forStatement, variable)) {
            context_->Step("Move declaration into for initializer", node);
            node->Remove();
            next->InsertChildAfter(nullptr, node, &Syntax::Slots::ForInitializer);
            return forStatement;
        }
    }

    // `static readonly WhileStatement forPattern`:
    //   while ($ident <any> $endExpr) { <statement>*; $ident <any>= <any>; }
    PatternTree tree;
    auto* conditionIdentNode = tree.Make<IdentifierExpression>(
        std::string(PM::Pattern::AnyString));
    auto* conditionLeft = tree.Wrap<Expression>(
        std::make_shared<PM::NamedNode>("ident", conditionIdentNode));
    auto* conditionRight = tree.Wrap<Expression>(std::make_shared<PM::AnyNode>("endExpr"));
    auto* condition = tree.Make<BinaryOperatorExpression>(
        conditionLeft, BinaryOperatorType::Any, conditionRight);
    auto* anyStatement = tree.Make<PM::AnyNode>("statement");
    auto* statementRepeat = tree.Wrap<Statement>(std::make_shared<PM::Repeat>(anyStatement));
    auto* iteratorLeft = tree.Wrap<Expression>(std::make_shared<PM::Backreference>("ident"));
    auto* iteratorRight = tree.Wrap<Expression>(std::make_shared<PM::AnyNode>());
    auto* iteratorAssign = tree.Make<AssignmentExpression>(
        iteratorLeft, AssignmentOperatorType::Any, iteratorRight);
    auto* iteratorStmt = tree.Make<ExpressionStatement>(iteratorAssign);
    auto* iteratorNamed = tree.Wrap<Statement>(
        std::make_shared<PM::NamedNode>("iterator", iteratorStmt));
    auto* bodyBlock = tree.Make<Syntax::BlockStatement>();
    bodyBlock->Statements().Add(statementRepeat);
    bodyBlock->Statements().Add(iteratorNamed);
    auto* pattern = tree.Make<WhileStatement>();
    pattern->Condition(condition);
    pattern->EmbeddedStatement(bodyBlock);

    PM::Match m3 = PM::PatternExtensions::Match(*pattern, next);
    if (!m3.Success())
        return nullptr;
    // Ensure the variable in the `for` pattern is the same as in the declaration.
    if (variable != GetILVariable(*m3.Get<IdentifierExpression>("ident").front()))
        return nullptr;
    auto* loop = static_cast<WhileStatement*>(next);
    // Cannot convert to `for` if the iteration variable is a by-ref local used after the loop:
    // its declaration is hoisted in front, leaving a headless `for` whose only initialization
    // is the for-initializer ref-assignment (CS8174). Keeping it a while-loop matches the
    // source and keeps the initializer on the declaration.
    if (variable != nullptr && variable->Type != nullptr && variable->Type->IsByRefLike()
        && IsVariableUsedAfter(loop, *variable)) {
        return nullptr;
    }
    auto* iteratorStatement = m3.Get<Statement>("iterator").front();
    if (IteratorVariablesDeclaredInsideLoopBody(iteratorStatement))
        return nullptr;
    // Cannot convert to `for`: `continue` in a while jumps to the condition, whereas in a `for`
    // it jumps to the increment block, so the rewrite would change semantics.
    for (AstNode* descendant : loop->DescendantNodes(
             [](AstNode* n) { return DescendIntoStatement(n); })) {
        if (dynamic_cast<ContinueStatement*>(descendant) != nullptr)
            return nullptr;
    }

    context_->Step("Transform while loop to for", loop);
    node->Remove();
    auto* newBody = new Syntax::BlockStatement();
    for (Statement* stmt : m3.Get<Statement>("statement"))
        newBody->Statements().Add(Syntax::Detach(stmt));
    auto* forStatement = new ForStatement();
    CopyAnnotationsFrom(forStatement, *loop);
    forStatement->Initializers().Add(node);
    forStatement->Condition(Syntax::Detach(loop->Condition()));
    forStatement->Iterators().Add(Syntax::Detach(iteratorStatement));
    forStatement->EmbeddedStatement(newBody);
    loop->ReplaceWith(forStatement);
    context_->EndStep(forStatement);
    return forStatement;
}

// The C# `Statement? TransformForeachOnArray(ForStatement forStatement)`: reconstructs a
// `foreach` over the compiler's index loop `for (i = 0; i < array.Length; i++) { item =
// array[i]; ... }`. The rewrite also accepts a `string` looped by index (the `Length` member
// plus the string indexer read as a `char`).
Statement* PatternStatementTransform::TransformForeachOnArray(ForStatement* forStatement) {
    if (!context_->Settings().ForEachStatement())
        return nullptr;

    // `static readonly ForStatement forOnArrayPattern`.
    PatternTree tree;
    auto* pattern = tree.Make<ForStatement>();

    auto* indexIdent = tree.Make<IdentifierExpression>(std::string(PM::Pattern::AnyString));
    auto* indexNamed = tree.Wrap<Expression>(
        std::make_shared<PM::NamedNode>("indexVariable", indexIdent));
    auto* zero = tree.Make<PrimitiveExpression>(Syntax::PrimitiveValue(std::int32_t(0)));
    auto* initAssign = tree.Make<AssignmentExpression>(indexNamed, zero);
    pattern->Initializers().Add(tree.Make<ExpressionStatement>(initAssign));

    auto* condition = tree.Make<BinaryOperatorExpression>(
        tree.Wrap<Expression>(std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")),
        BinaryOperatorType::LessThan,
        tree.Make<Syntax::MemberReferenceExpression>(
            tree.Wrap<Expression>(std::make_shared<PM::NamedNode>("arrayVariable",
                tree.Make<IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
            std::string("Length")));
    pattern->Condition(condition);

    auto* iterRight = tree.Make<BinaryOperatorExpression>(
        tree.Wrap<Expression>(std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")),
        BinaryOperatorType::Add,
        tree.Make<PrimitiveExpression>(Syntax::PrimitiveValue(std::int32_t(1))));
    pattern->Iterators().Add(tree.Make<ExpressionStatement>(tree.Make<AssignmentExpression>(
        tree.Wrap<Expression>(std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")),
        iterRight)));

    auto* bodyBlock = tree.Make<Syntax::BlockStatement>();
    auto* indexer = tree.Make<IndexerExpression>(
        tree.Wrap<Expression>(std::make_shared<PM::IdentifierExpressionBackreference>("arrayVariable")));
    indexer->Arguments().Add(
        tree.Wrap<Expression>(std::make_shared<PM::IdentifierExpressionBackreference>("indexVariable")));
    bodyBlock->Statements().Add(tree.Make<ExpressionStatement>(tree.Make<AssignmentExpression>(
        tree.Wrap<Expression>(std::make_shared<PM::NamedNode>("itemVariable",
            tree.Make<IdentifierExpression>(std::string(PM::Pattern::AnyString)))),
        indexer)));
    auto* statementsAny = tree.Make<PM::AnyNode>("statements");
    bodyBlock->Statements().Add(
        tree.Wrap<Statement>(std::make_shared<PM::Repeat>(statementsAny)));
    pattern->EmbeddedStatement(bodyBlock);

    PM::Match m = PM::PatternExtensions::Match(*pattern, forStatement);
    if (!m.Success())
        return nullptr;
    Syntax::IdentifierExpression* itemIdentifier =
        m.Get<IdentifierExpression>("itemVariable").front();
    IL::ILVariable* itemVariable = GetILVariable(*itemIdentifier);
    IL::ILVariable* indexVariable =
        GetILVariable(*m.Get<IdentifierExpression>("indexVariable").front());
    IL::ILVariable* arrayVariable =
        GetILVariable(*m.Get<IdentifierExpression>("arrayVariable").front());
    if (itemVariable == nullptr || indexVariable == nullptr || arrayVariable == nullptr)
        return nullptr;
    if (arrayVariable->Type == nullptr
        || (arrayVariable->Type->Kind() != TS::TypeKind::Array
            && !TS::IsKnownType(*arrayVariable->Type, TS::KnownTypeCode::String)))
        return nullptr;
    if (!VariableCanBeUsedAsForeachLocal(itemVariable, forStatement))
        return nullptr;
    // The index is a pure counter: stored at init + increment, loaded at the condition, the
    // increment, and the element access; never captured by address.
    if (indexVariable->StoreCount != 2 || indexVariable->LoadCount != 3
        || indexVariable->AddressCount != 0)
        return nullptr;

    context_->Step("Introduce foreach over array", forStatement);
    auto* body = new Syntax::BlockStatement();
    for (Statement* statement : m.Get<Statement>("statements"))
        body->Statements().Add(Syntax::Detach(statement));
    auto* foreachStmt = new ForeachStatement();
    foreachStmt->VariableType(
        context_->Settings().AnonymousTypes() && itemVariable->Type != nullptr
                && ::ILSpy::Decompiler::ContainsAnonymousType(*itemVariable->Type)
            ? static_cast<Syntax::AstType*>(new Syntax::SimpleType(std::string("var")))
            : context_->TypeSystemAstBuilder().ConvertType(*itemVariable->Type));
    auto* designation = new Syntax::SingleVariableDesignation(itemVariable->Name);
    foreachStmt->VariableDesignation(designation);
    foreachStmt->InExpression(
        Syntax::Detach(m.Get<IdentifierExpression>("arrayVariable").front()));
    foreachStmt->EmbeddedStatement(body);
    CopyAnnotationsFrom(foreachStmt, *forStatement);
    itemVariable->Kind = IL::VariableKind::ForeachLocal;
    // Add the variable annotation for highlighting (the C# attaches it to the
    // `VariableDesignation` rather than the loop).
    const auto* itemResolveResult = itemIdentifier->Annotation<ILVariableResolveResult>();
    designation->AddAnnotation(std::make_shared<ILVariableResolveResult>(
        itemResolveResult->VariableHandle(), itemVariable->Type));
    // TODO : add ForeachAnnotation
    forStatement->ReplaceWith(foreachStmt);
    context_->EndStep(foreachStmt);
    return foreachStmt;
}

// The C# `Statement? TransformForeachOnInlineArray(ForStatement forStatement)`: reconstructs a
// `foreach` over an `[InlineArray(N)]` buffer from the compiler's index loop. The rewrite is
// only sound because the loop bound equals the inline array length, which proves the index is
// always in range (`InlineArrayElementRef` is the compiler's unchecked element accessor,
// whereas the C# inline-array indexer is bounds-checked).
Statement* PatternStatementTransform::TransformForeachOnInlineArray(ForStatement* forStatement) {
    if (!context_->Settings().ForEachStatement() || !context_->Settings().InlineArrays())
        return nullptr;

    // `static readonly ForStatement forOnInlineArrayPattern`.
    PatternTree tree;
    auto* pattern = BuildForOnInlineArrayPattern(tree);
    PM::Match m = PM::PatternExtensions::Match(*pattern, forStatement);
    if (!m.Success())
        return nullptr;
    Syntax::IdentifierExpression* itemIdentifier =
        m.Get<IdentifierExpression>("itemVariable").front();
    Syntax::IdentifierExpression* indexIdentifier =
        m.Get<IdentifierExpression>("indexVariable").front();
    IL::ILVariable* itemVariable = GetILVariable(*itemIdentifier);
    IL::ILVariable* indexVariable = GetILVariable(*indexIdentifier);
    if (itemVariable == nullptr || indexVariable == nullptr)
        return nullptr;

    // The loop body must start with `item = InlineArrayElementRef(ref buffer, index)`.
    Syntax::Expression* elementAccessAny = m.Get<Expression>("elementAccess").front();
    auto* elementAccess = dynamic_cast<Syntax::InvocationExpression*>(elementAccessAny);
    if (elementAccess == nullptr)
        return nullptr;
    const TS::ISymbol* symbol = CSharp::GetSymbol(*elementAccess);
    const auto* helper = dynamic_cast<const TS::IMethod*>(symbol);
    if (helper == nullptr || helper->DeclaringType() == nullptr
        || helper->DeclaringType()->GetDefinition() == nullptr
        || helper->DeclaringType()->GetDefinition()->FullName()
            != "<PrivateImplementationDetails>")
        return nullptr;
    if (helper->Name() != "InlineArrayElementRef"
        && helper->Name() != "InlineArrayElementRefReadOnly")
        return nullptr;
    if (elementAccess->Arguments().Count() != 2)
        return nullptr;
    // arg0: `ref buffer`.
    auto* direction =
        dynamic_cast<Syntax::DirectionExpression*>(elementAccess->Arguments()[0]);
    if (direction == nullptr)
        return nullptr;
    auto* bufferIdentifier =
        dynamic_cast<Syntax::IdentifierExpression*>(direction->Expression());
    if (bufferIdentifier == nullptr)
        return nullptr;
    IL::ILVariable* bufferVariable = GetILVariable(*bufferIdentifier);
    if (bufferVariable == nullptr)
        return nullptr;
    // arg1: the loop index.
    auto* indexArgument = dynamic_cast<Syntax::IdentifierExpression*>(
        elementAccess->Arguments()[elementAccess->Arguments().Count() - 1]);
    if (indexArgument == nullptr || GetILVariable(*indexArgument) != indexVariable)
        return nullptr;

    // Soundness: the loop counts 0..length-1 over exactly the inline array's length.
    if (bufferVariable->Type == nullptr)
        return nullptr;
    std::optional<int> arrayLength = TS::GetInlineArrayLength(*bufferVariable->Type);
    if (!arrayLength.has_value())
        return nullptr;
    std::optional<int> loopBound = TryParsePrimitiveAsInt(
        m.Get<PrimitiveExpression>("length").front()->Value());
    if (!loopBound.has_value() || *loopBound != *arrayLength)
        return nullptr;

    if (!VariableCanBeUsedAsForeachLocal(itemVariable, forStatement))
        return nullptr;
    // The index is a pure counter: stored at init + increment, loaded at the condition, the
    // increment, and the element access; never captured by address.
    if (indexVariable->StoreCount != 2 || indexVariable->LoadCount != 3
        || indexVariable->AddressCount != 0)
        return nullptr;

    context_->Step("Introduce foreach over inline array", forStatement);
    // Take the buffer reference for the `in` expression before dropping the element access.
    Syntax::Expression* inExpression = Syntax::Detach(bufferIdentifier);
    // Reuse the loop body (preserving its annotations) after removing its leading
    // `item = <PrivateImplementationDetails>.InlineArrayElementRef(ref buffer, i)` statement.
    auto* body = dynamic_cast<Syntax::BlockStatement*>(forStatement->EmbeddedStatement());
    if (body == nullptr)
        return nullptr;
    body->Statements()[0]->Remove();
    auto* foreachStmt = new ForeachStatement();
    foreachStmt->VariableType(
        context_->Settings().AnonymousTypes() && itemVariable->Type != nullptr
                && ::ILSpy::Decompiler::ContainsAnonymousType(*itemVariable->Type)
            ? static_cast<Syntax::AstType*>(new Syntax::SimpleType(std::string("var")))
            : context_->TypeSystemAstBuilder().ConvertType(*itemVariable->Type));
    auto* designation = new Syntax::SingleVariableDesignation(itemVariable->Name);
    foreachStmt->VariableDesignation(designation);
    foreachStmt->InExpression(inExpression);
    foreachStmt->EmbeddedStatement(Syntax::Detach(body));
    CopyAnnotationsFrom(foreachStmt, *forStatement);
    itemVariable->Kind = IL::VariableKind::ForeachLocal;
    const auto* itemResolveResult = itemIdentifier->Annotation<ILVariableResolveResult>();
    designation->AddAnnotation(std::make_shared<ILVariableResolveResult>(
        itemResolveResult->VariableHandle(), itemVariable->Type));
    // TODO : add ForeachAnnotation
    forStatement->ReplaceWith(foreachStmt);
    context_->EndStep(foreachStmt);
    return foreachStmt;
}

// The C# `Statement? TransformForeachOnMultiDimArray(ExpressionStatement expressionStatement)`:
// reconstructs a `foreach` over a multidimensional array from the compiler's nested
// `GetUpperBound`/`GetLowerBound` index loops. The C# runs this before `TransformFor` on every
// expression statement; it returns null unless the whole nest matches.
Statement* PatternStatementTransform::TransformForeachOnMultiDimArray(
    ExpressionStatement* expressionStatement) {
    if (!context_->Settings().ForEachStatement())
        return nullptr;
    PM::Match m;
    Statement* stmt = expressionStatement;
    IL::ILVariable* collection = nullptr;
    std::vector<IL::ILVariable*> upperBounds;
    bool haveUpperBounds = false;
    std::vector<Statement*> statementsToDelete;
    int i = 0;
    // First look for all the upper-bound initializations.
    do {
        {
            PatternTree tree;
            auto* pattern = BuildVariableAssignUpperBoundPattern(tree);
            m = PM::PatternExtensions::Match(*pattern, stmt);
        }
        if (!m.Success())
            break;
        if (!haveUpperBounds) {
            collection = GetILVariable(*m.Get<IdentifierExpression>("collection").front());
            auto* arrayType = collection != nullptr
                ? dynamic_cast<TS::ArrayType*>(collection->Type.get()) : nullptr;
            if (arrayType == nullptr)
                break;
            upperBounds.assign(arrayType->Rank(), nullptr);
            haveUpperBounds = true;
        } else {
            statementsToDelete.push_back(stmt);
        }
        IL::ILVariable* nextCollection =
            GetILVariable(*m.Get<IdentifierExpression>("collection").front());
        if (nextCollection != collection)
            break;
        std::optional<int> index = TryParsePrimitiveAsInt(
            m.Get<PrimitiveExpression>("index").front()->Value());
        if (!index.has_value() || *index != i)
            break;
        upperBounds[i] = GetILVariable(*m.Get<IdentifierExpression>("variable").front());
        stmt = Syntax::GetNextStatement(stmt);
        i++;
    } while (stmt != nullptr && haveUpperBounds
             && i < static_cast<int>(upperBounds.size()));

    // The C# `upperBounds?.LastOrDefault() == null` guard: the loop must have filled the
    // whole dimension array.
    if (upperBounds.empty() || upperBounds.back() == nullptr || collection == nullptr
        || stmt == nullptr) {
        return nullptr;
    }
    IdentifierExpression* foreachVariable = nullptr;
    std::vector<Statement*> statements;
    std::vector<IL::ILVariable*> lowerBounds;
    if (!MatchForeachOnMultiDimArray(upperBounds, collection, stmt, foreachVariable, statements,
                                    lowerBounds)) {
        return nullptr;
    }
    statementsToDelete.push_back(stmt);
    // The matched multidimensional foreach pattern guarantees a statement after stmt.
    statementsToDelete.push_back(Syntax::GetNextStatement(stmt));
    IL::ILVariable* itemVariable = GetILVariable(*foreachVariable);
    if (itemVariable == nullptr || !itemVariable->IsSingleDefinition()
        || (itemVariable->Kind != IL::VariableKind::Local
            && itemVariable->Kind != IL::VariableKind::StackSlot)) {
        return nullptr;
    }
    for (IL::ILVariable* upperBound : upperBounds) {
        if (upperBound == nullptr || !upperBound->IsSingleDefinition()
            || upperBound->LoadCount != 1) {
            return nullptr;
        }
    }
    // The index counters are pure counters: stored at init + increment, loaded at the
    // condition, the increment, and the element access; never captured by address.
    for (IL::ILVariable* lowerBound : lowerBounds) {
        if (lowerBound == nullptr || lowerBound->StoreCount != 2 || lowerBound->LoadCount != 3
            || lowerBound->AddressCount != 0) {
            return nullptr;
        }
    }
    context_->Step("Introduce foreach over multidimensional array", expressionStatement);
    auto* body = new Syntax::BlockStatement();
    for (Statement* statement : statements)
        body->Statements().Add(Syntax::Detach(statement));
    auto* foreachStmt = new ForeachStatement();
    foreachStmt->VariableType(
        context_->Settings().AnonymousTypes() && itemVariable->Type != nullptr
                && ::ILSpy::Decompiler::ContainsAnonymousType(*itemVariable->Type)
            ? static_cast<Syntax::AstType*>(new Syntax::SimpleType(std::string("var")))
            : context_->TypeSystemAstBuilder().ConvertType(*itemVariable->Type));
    auto* designation = new Syntax::SingleVariableDesignation(itemVariable->Name);
    foreachStmt->VariableDesignation(designation);
    foreachStmt->InExpression(
        Syntax::Detach(m.Get<IdentifierExpression>("collection").front()));
    foreachStmt->EmbeddedStatement(body);
    for (Statement* statement : statementsToDelete)
        statement->Remove();
    // foreachStmt.CopyAnnotationsFrom(forStatement); is intentionally commented out in C#.
    itemVariable->Kind = IL::VariableKind::ForeachLocal;
    // Add the variable annotation for highlighting (the C# attaches it to the
    // `VariableDesignation` rather than the loop).
    const auto* itemResolveResult = foreachVariable->Annotation<ILVariableResolveResult>();
    designation->AddAnnotation(std::make_shared<ILVariableResolveResult>(
        itemResolveResult->VariableHandle(), itemVariable->Type));
    // TODO : add ForeachAnnotation
    expressionStatement->ReplaceWith(foreachStmt);
    context_->EndStep(foreachStmt);
    return foreachStmt;
}

bool PatternStatementTransform::MatchLowerBound(int indexNum, IL::ILVariable*& index,
                                                IL::ILVariable* collection,
                                                Statement* statement) {
    index = nullptr;
    PatternTree tree;
    auto* pattern = BuildVariableAssignLowerBoundPattern(tree);
    PM::Match m = PM::PatternExtensions::Match(*pattern, statement);
    if (!m.Success())
        return false;
    std::optional<int> i =
        TryParsePrimitiveAsInt(m.Get<PrimitiveExpression>("index").front()->Value());
    if (!i.has_value() || indexNum != *i)
        return false;
    index = GetILVariable(*m.Get<IdentifierExpression>("variable").front());
    return GetILVariable(*m.Get<IdentifierExpression>("collection").front()) == collection;
}

bool PatternStatementTransform::MatchForeachOnMultiDimArray(
    const std::vector<IL::ILVariable*>& upperBounds, IL::ILVariable* collection,
    Statement* firstInitializerStatement, IdentifierExpression*& foreachVariable,
    std::vector<Statement*>& statements, std::vector<IL::ILVariable*>& lowerBounds) {
    int i = 0;
    foreachVariable = nullptr;
    lowerBounds.assign(upperBounds.size(), nullptr);
    Statement* stmt = firstInitializerStatement;
    PM::Match m;
    while (i < static_cast<int>(upperBounds.size())) {
        IL::ILVariable* indexVariable = nullptr;
        if (!MatchLowerBound(i, indexVariable, collection, stmt))
            break;
        {
            PatternTree tree;
            auto* pattern = BuildForOnArrayMultiDimPattern(tree);
            m = PM::PatternExtensions::Match(*pattern, Syntax::GetNextStatement(stmt));
        }
        if (!m.Success())
            return false;
        IL::ILVariable* upperBound =
            GetILVariable(*m.Get<IdentifierExpression>("upperBoundVariable").front());
        if (upperBounds[i] != upperBound)
            return false;
        stmt = m.Get<Statement>("lowerBoundAssign").front();
        lowerBounds[i] = indexVariable;
        i++;
    }
    // The C# would dereference a default `Match` here when no dimension matched; the guard
    // turns that latent failure into the same "no rewrite" result.
    if (!m.Success())
        return false;
    if (collection->Type == nullptr || collection->Type->Kind() != TS::TypeKind::Array)
        return false;
    {
        PatternTree tree;
        auto* pattern = BuildForeachVariableOnMultArrayAssignPattern(tree);
        PM::Match m2 = PM::PatternExtensions::Match(*pattern, stmt);
        if (!m2.Success())
            return false;
        if (GetILVariable(*m2.Get<IdentifierExpression>("collection").front()) != collection)
            return false;
        foreachVariable = m2.Get<IdentifierExpression>("variable").front();
    }
    for (Statement* statement : m.Get<Statement>("statements"))
        statements.push_back(statement);
    return true;
}

bool PatternStatementTransform::VariableCanBeUsedAsForeachLocal(IL::ILVariable* itemVar,
                                                               Statement* loop) {
    if (itemVar == nullptr
        || (itemVar->Kind != IL::VariableKind::Local
            && itemVar->Kind != IL::VariableKind::StackSlot)) {
        // Only locals/temporaries can be converted into a foreach loop variable.
        return false;
    }

    IL::BlockContainer* blockContainer = CSharp::GetBlockContainer(*loop);

    if (!itemVar->IsSingleDefinition()) {
        // A foreach variable cannot be assigned to. As a special case, the address may be
        // taken for a method call when that call is the only use.
        if (!AddressUsedForSingleCall(itemVar, blockContainer))
            return false;
    }

    if (itemVar->CaptureScope != nullptr && itemVar->CaptureScope != blockContainer) {
        // Captured variables cannot be declared in the loop unless the loop is their
        // capture scope.
        return false;
    }

    Syntax::AstNode* declPoint = declareVariables_.GetDeclarationPoint(*itemVar);
    bool declaredInsideLoop = false;
    for (Syntax::AstNode* ancestor : declPoint->Ancestors()) {
        if (ancestor == loop) {
            declaredInsideLoop = true;
            break;
        }
    }
    return declaredInsideLoop && !declareVariables_.WasMerged(*itemVar);
}

bool PatternStatementTransform::AddressUsedForSingleCall(IL::ILVariable* /*v*/,
                                                         IL::BlockContainer* /*loop*/) {
    // The C# accepts an item variable whose address is taken for a single instance method call
    // when the call is the only use and lies within the loop. The port has no `IL.Call` node and
    // no per-variable address-instruction list yet, so the shape cannot be reconstructed; the
    // address-taken path conservatively rejects (only variables that are not single-definition
    // reach here, so the common single-definition path is unaffected).
    return false;
}

bool PatternStatementTransform::DescendIntoStatement(AstNode* node) {
    if (dynamic_cast<Expression*>(node) != nullptr
        || dynamic_cast<ExpressionStatement*>(node) != nullptr) {
        return false;
    }
    if (dynamic_cast<WhileStatement*>(node) != nullptr
        || dynamic_cast<ForeachStatement*>(node) != nullptr
        || dynamic_cast<DoWhileStatement*>(node) != nullptr
        || dynamic_cast<ForStatement*>(node) != nullptr) {
        return false;
    }
    return true;
}

bool PatternStatementTransform::ForStatementUsesVariable(ForStatement* statement,
                                                         IL::ILVariable* variable) {
    if (statement->Condition() != nullptr) {
        for (AstNode* n : statement->Condition()->DescendantsAndSelf()) {
            auto* ie = dynamic_cast<IdentifierExpression*>(n);
            if (ie != nullptr && GetILVariable(*ie) == variable)
                return true;
        }
    }
    for (int i = 0; i < statement->Iterators().Count(); i++) {
        for (AstNode* n : statement->Iterators()[i]->DescendantsAndSelf()) {
            auto* ie = dynamic_cast<IdentifierExpression*>(n);
            if (ie != nullptr && GetILVariable(*ie) == variable)
                return true;
        }
    }
    return false;
}

bool PatternStatementTransform::IsVariableUsedAfter(Statement* loop, IL::ILVariable& variable) {
    for (AstNode* sibling = loop->NextSibling(); sibling != nullptr;
         sibling = sibling->NextSibling()) {
        for (AstNode* n : sibling->DescendantsAndSelf()) {
            auto* ie = dynamic_cast<IdentifierExpression*>(n);
            if (ie != nullptr && GetILVariable(*ie) == &variable)
                return true;
        }
    }
    return false;
}

bool PatternStatementTransform::IteratorVariablesDeclaredInsideLoopBody(
    Statement* iteratorStatement) {
    for (AstNode* n : iteratorStatement->DescendantsAndSelf()) {
        auto* id = dynamic_cast<IdentifierExpression*>(n);
        if (id == nullptr)
            continue;
        IL::ILVariable* v = GetILVariable(*id);
        if (v == nullptr || !DeclareVariables::VariableNeedsDeclaration(v->Kind))
            continue;
        if (declareVariables_.GetDeclarationPoint(*v)->Parent() == iteratorStatement->Parent())
            return true;
    }
    return false;
}

AstNode* PatternStatementTransform::VisitIfElseStatement(Syntax::IfElseStatement* ifElseStatement) {
    // The C# returns the (always null) result of the simplifier, then continues the walk.
    SimplifyCascadingIfElseStatements(ifElseStatement);
    return Syntax::DepthFirstAstVisitorAstNode::VisitIfElseStatement(ifElseStatement);
}

AstNode* PatternStatementTransform::VisitTryCatchStatement(
    Syntax::TryCatchStatement* tryCatchStatement) {
    // The C# `return TransformTryCatchFinally(...) ?? base.VisitTryCatchStatement(...)`.
    if (TransformTryCatchFinally(tryCatchStatement) != nullptr)
        return tryCatchStatement;
    return Syntax::DepthFirstAstVisitorAstNode::VisitTryCatchStatement(tryCatchStatement);
}

AstNode* PatternStatementTransform::VisitFixedStatement(Syntax::FixedStatement* fixedStatement) {
    if (context_->Settings().PatternBasedFixedStatement()) {
        for (int i = 0; i < fixedStatement->Variables().Count(); i++) {
            Syntax::VariableInitializer* variable = fixedStatement->Variables()[i];
            PatternTree tree;
            auto* pattern = BuildAddressOfPinnableReferencePattern(tree);
            PM::Match m = PM::PatternExtensions::Match(*pattern, variable->Initializer());
            if (m.Success()) {
                Syntax::Expression* target = m.Get<Syntax::Expression>("target").front();
                // The C# `target.GetResolveResult().Type.IsReferenceType == false`: only a value
                // type is taken by pattern-based `fixed` (reference types are handled by the
                // pinned-region detection). A null `IsReferenceType` (unknown) is not `false`.
                const Sem::ResolveResult* resolveResult = GetResolveResult(*target);
                if (resolveResult->Type().IsReferenceType() == std::optional<bool>(false)) {
                    context_->Step("Use pattern-based fixed statement", fixedStatement);
                    variable->Initializer(Syntax::Detach(target));
                }
            }
        }
    }
    return Syntax::DepthFirstAstVisitorAstNode::VisitFixedStatement(fixedStatement);
}

AstNode* PatternStatementTransform::VisitUsingStatement(Syntax::UsingStatement* usingStatement) {
    usingStatement = static_cast<Syntax::UsingStatement*>(
        Syntax::DepthFirstAstVisitorAstNode::VisitUsingStatement(usingStatement));
    if (!context_->Settings().UseEnhancedUsing())
        return usingStatement;

    if (Syntax::GetNextStatement(usingStatement) != nullptr
        || dynamic_cast<Syntax::BlockStatement*>(usingStatement->Parent()) == nullptr) {
        return usingStatement;
    }

    if (dynamic_cast<Syntax::VariableDeclarationStatement*>(
            usingStatement->ResourceAcquisition()) == nullptr) {
        return usingStatement;
    }

    context_->Step("Use enhanced using statement", usingStatement);
    usingStatement->IsEnhanced(true);
    return usingStatement;
}

AstNode* PatternStatementTransform::VisitPropertyDeclaration(
    Syntax::PropertyDeclaration* propertyDeclaration) {
    // The C# `if (context.Settings.AutomaticProperties && (propertyDeclaration.Setter is not
    // null || context.Settings.GetterOnlyAutomaticProperties)) { ... TransformAutomaticProperty ... }`.
    if (context_->Settings().AutomaticProperties()
        && (propertyDeclaration->Setter() != nullptr
            || context_->Settings().GetterOnlyAutomaticProperties())) {
        AstNode* result = TransformAutomaticProperty(propertyDeclaration);
        if (result != nullptr)
            return result;
    }
    return ContextTrackingVisitor::VisitPropertyDeclaration(propertyDeclaration);
}

AstNode* PatternStatementTransform::VisitMethodDeclaration(
    Syntax::MethodDeclaration* methodDeclaration) {
    // The C# `return TransformDestructor(methodDeclaration) ?? base.VisitMethodDeclaration(...)`.
    // `base` is ContextTrackingVisitor, which seeds `currentMethod` for the child walk.
    if (Syntax::DestructorDeclaration* destructor = TransformDestructor(methodDeclaration))
        return destructor;
    return ContextTrackingVisitor::VisitMethodDeclaration(methodDeclaration);
}

AstNode* PatternStatementTransform::VisitDestructorDeclaration(
    Syntax::DestructorDeclaration* destructorDeclaration) {
    // The C# `return TransformDestructorBody(...) ?? base.VisitDestructorDeclaration(...)`.
    if (Syntax::DestructorDeclaration* destructor = TransformDestructorBody(destructorDeclaration))
        return destructor;
    return ContextTrackingVisitor::VisitDestructorDeclaration(destructorDeclaration);
}

// ---- Cascading if-else -------------------------------------------------------------

AstNode* PatternStatementTransform::SimplifyCascadingIfElseStatements(
    Syntax::IfElseStatement* node) {
    // The C# `static readonly IfElseStatement cascadingIfElsePattern`: an `if` whose else is a
    // single block wrapping a nested `if` whose else is optional.
    PatternTree tree;
    auto* pattern = tree.Make<Syntax::IfElseStatement>();
    pattern->Condition(tree.Wrap<Expression>(std::make_shared<PM::AnyNode>()));
    pattern->TrueStatement(tree.Wrap<Syntax::Statement>(std::make_shared<PM::AnyNode>()));
    auto* nested = tree.Make<Syntax::IfElseStatement>();
    nested->Condition(tree.Wrap<Expression>(std::make_shared<PM::AnyNode>()));
    nested->TrueStatement(tree.Wrap<Syntax::Statement>(std::make_shared<PM::AnyNode>()));
    auto* innerAny = tree.Make<PM::AnyNode>();
    nested->FalseStatement(
        tree.Wrap<Syntax::Statement>(std::make_shared<PM::OptionalNode>(innerAny)));
    auto* falseBlock = tree.Make<Syntax::BlockStatement>();
    falseBlock->Statements().Add(
        tree.Wrap<Syntax::Statement>(std::make_shared<PM::NamedNode>("nestedIfStatement", nested)));
    pattern->FalseStatement(falseBlock);

    PM::Match m = PM::PatternExtensions::Match(*pattern, node);
    if (m.Success()) {
        context_->Step("Simplify cascading if-else", node);
        // The C# `m.Get<IfElseStatement>("nestedIfStatement").Single()` -- the captured node is
        // the real nested `if` in the input tree, so it is detached and becomes the else branch.
        Syntax::IfElseStatement* elseIf =
            m.Get<Syntax::IfElseStatement>("nestedIfStatement").front();
        node->FalseStatement(Syntax::Detach(elseIf));
    }
    // The C# always returns null (the instance is not replaced).
    return nullptr;
}

// ---- Try-catch-finally -------------------------------------------------------------

Syntax::TryCatchStatement* PatternStatementTransform::TransformTryCatchFinally(
    Syntax::TryCatchStatement* tryFinally) {
    // The C# `static readonly TryCatchStatement tryCatchFinallyPattern`: a `try` block that
    // consists of a single nested try-catch, plus a `finally` block.
    PatternTree tree;
    auto* pattern = tree.Make<Syntax::TryCatchStatement>();
    auto* outerTryBlock = tree.Make<Syntax::BlockStatement>();
    auto* innerTry = tree.Make<Syntax::TryCatchStatement>();
    innerTry->TryBlock(tree.Wrap<Syntax::BlockStatement>(std::make_shared<PM::AnyNode>()));
    auto* catchAny = tree.Make<PM::AnyNode>();
    innerTry->CatchClauses().Add(
        tree.Wrap<Syntax::CatchClause>(std::make_shared<PM::Repeat>(catchAny)));
    outerTryBlock->Statements().Add(innerTry);
    pattern->TryBlock(outerTryBlock);
    pattern->FinallyBlock(tree.Wrap<Syntax::BlockStatement>(std::make_shared<PM::AnyNode>()));

    if (PM::PatternExtensions::IsMatch(*pattern, tryFinally)) {
        context_->Step("Merge nested try-catch-finally", tryFinally);
        // The matched shape guarantees the outer try block holds exactly the nested try.
        auto* tryCatch = static_cast<Syntax::TryCatchStatement*>(
            tryFinally->TryBlock()->Statements()[0]);
        tryFinally->TryBlock(Syntax::Detach(tryCatch->TryBlock()));
        tryCatch->CatchClauses().MoveTo(tryFinally->CatchClauses());
    }
    // The C# always returns null (the instance is not replaced).
    return nullptr;
}

// ---- Destructor --------------------------------------------------------------------

Syntax::DestructorDeclaration* PatternStatementTransform::TransformDestructor(
    Syntax::MethodDeclaration* methodDef) {
    // The C# `static readonly MethodDeclaration destructorPattern`: a `void Finalize()` method
    // whose body is the destructor-body pattern.
    PatternTree tree;
    auto* pattern = tree.Make<Syntax::MethodDeclaration>();
    auto* attributeAny = tree.Make<PM::AnyNode>();
    pattern->Attributes().Add(
        tree.Wrap<Syntax::AttributeSection>(std::make_shared<PM::Repeat>(attributeAny)));
    pattern->Modifiers(Syntax::Modifiers::Any);
    pattern->ReturnType(tree.Make<Syntax::PrimitiveType>(std::string("void")));
    pattern->Name("Finalize");
    pattern->Body(BuildDestructorBodyPattern(tree));

    PM::Match m = PM::PatternExtensions::Match(*pattern, methodDef);
    if (!m.Success())
        return nullptr;
    context_->Step("Convert Finalize method to destructor", methodDef);
    auto* destructor = new Syntax::DestructorDeclaration();
    methodDef->Attributes().MoveTo(destructor->Attributes());
    CopyAnnotationsFrom(destructor, *methodDef);
    destructor->Modifiers(methodDef->Modifiers() &
                          ~(Syntax::Modifiers::Protected | Syntax::Modifiers::Override));
    destructor->Body(Syntax::Detach(m.Get<Syntax::BlockStatement>("body").front()));
    // The C# relies on the enclosing type context (`currentTypeDefinition!`) being set by the
    // `ContextTrackingVisitor` walk; a method declaration only appears inside a type.
    destructor->Name(currentTypeDefinition->Name());
    methodDef->ReplaceWith(destructor);
    context_->EndStep(destructor);
    return destructor;
}

Syntax::DestructorDeclaration* PatternStatementTransform::TransformDestructorBody(
    Syntax::DestructorDeclaration* dtorDef) {
    PatternTree tree;
    auto* bodyPattern = BuildDestructorBodyPattern(tree);

    PM::Match m = PM::PatternExtensions::Match(*bodyPattern, dtorDef->Body());
    if (!m.Success())
        return nullptr;
    context_->Step("Simplify destructor body", dtorDef);
    dtorDef->Body(Syntax::Detach(m.Get<Syntax::BlockStatement>("body").front()));
    return dtorDef;
}

// ---- Automatic properties ----------------------------------------------------------

bool PatternStatementTransform::CanTransformToAutomaticProperty(
    const TS::IProperty& property, bool accessorsMustBeCompilerGenerated) {
    // The C# `if (!property.CanGet) return false`.
    if (!property.CanGet())
        return false;
    // The C# `if (accessorsMustBeCompilerGenerated && !property.Getter.IsCompilerGenerated())
    // return false`.
    if (accessorsMustBeCompilerGenerated && !IsCompilerGenerated(property.Getter()))
        return false;
    // The C# `if (property.Setter is IMethod setter) { ... }`.
    if (const TS::IMethod* setter = property.Setter()) {
        if (accessorsMustBeCompilerGenerated && !IsCompilerGenerated(setter))
            return false;
        if (TS::HasReadonlyModifier(*setter))
            return false;
    }
    return true;
}

AstNode* PatternStatementTransform::TransformAutomaticProperty(
    Syntax::PropertyDeclaration* propertyDeclaration) {
    // The C# `IProperty? property = propertyDeclaration.GetSymbol() as IProperty`.
    const auto* property = dynamic_cast<const TS::IProperty*>(GetSymbol(*propertyDeclaration));
    if (property == nullptr)
        return nullptr;
    // The C# `CanTransformToAutomaticProperty(property, !(property.DeclaringTypeDefinition?.
    // Fields.Any(f => f.Name == "_" + property.Name && f.IsCompilerGenerated()) ?? false))`: the
    // accessors must be compiler-generated unless the declaring type carries a VB-style `_Name`
    // compiler-generated backing field.
    bool accessorsMustBeCompilerGenerated = true;
    if (const TS::ITypeDefinition* declaringType = property->DeclaringTypeDefinition()) {
        const std::string vbFieldName = "_" + property->Name();
        for (const TS::IField* field : declaringType->Fields()) {
            if (field != nullptr && field->Name() == vbFieldName && IsCompilerGenerated(field)) {
                accessorsMustBeCompilerGenerated = false;
                break;
            }
        }
    }
    if (!CanTransformToAutomaticProperty(*property, accessorsMustBeCompilerGenerated))
        return nullptr;

    // The C# `automaticPropertyPattern.Match(...)`, falling back to the read-only pattern.
    const TS::IField* field = nullptr;
    {
        PatternTree tree;
        auto* pattern = BuildAutomaticPropertyPattern(tree, true);
        PM::Match m = PM::PatternExtensions::Match(*pattern, propertyDeclaration);
        if (m.Success()) {
            auto captured = m.Get<Syntax::AstNode>("fieldReference");
            if (!captured.empty())
                field = dynamic_cast<const TS::IField*>(GetSymbol(*captured.front()));
        } else {
            PatternTree readonlyTree;
            auto* readonlyPattern = BuildAutomaticPropertyPattern(readonlyTree, false);
            PM::Match m2 = PM::PatternExtensions::Match(*readonlyPattern, propertyDeclaration);
            if (m2.Success()) {
                auto captured = m2.Get<Syntax::AstNode>("fieldReference");
                if (!captured.empty())
                    field = dynamic_cast<const TS::IField*>(GetSymbol(*captured.front()));
            }
        }
    }
    // The C# `if (field == null || !NameCouldBeBackingFieldOfAutomaticProperty(field.Name,
    // out _)) return null`.
    if (field == nullptr) {
        return nullptr;
    }
    {
        std::string ignoredName;
        if (!NameCouldBeBackingFieldOfAutomaticProperty(field->Name(), ignoredName))
            return nullptr;
    }
    // The C# readonly guards: a `readonly set`/`readonly` property with a setter cannot be an
    // auto-property.
    if ((propertyDeclaration->Setter() != nullptr
            && propertyDeclaration->Setter()->HasModifier(Syntax::Modifiers::Readonly))
        || (propertyDeclaration->HasModifier(Syntax::Modifiers::Readonly)
            && propertyDeclaration->Setter() != nullptr)) {
        return nullptr;
    }
    // The C# `if (field.IsCompilerGenerated() && field.DeclaringTypeDefinition ==
    // property.DeclaringTypeDefinition)`: clear the accessor bodies and hide the backing field.
    if (IsCompilerGenerated(field)
        && field->DeclaringTypeDefinition() == property->DeclaringTypeDefinition()) {
        context_->Step("Convert property to auto-property", propertyDeclaration);
        // Clearing the accessor body turns it into an auto-property accessor.
        Syntax::Accessor* getter = propertyDeclaration->Getter();
        Syntax::Accessor* setter = propertyDeclaration->Setter();
        if (getter != nullptr) {
            RemoveCompilerGeneratedAttribute(getter->Attributes());
            getter->Body(nullptr);
        }
        if (setter != nullptr) {
            RemoveCompilerGeneratedAttribute(setter->Attributes());
            setter->Body(nullptr);
        }
        propertyDeclaration->Modifiers(
            propertyDeclaration->Modifiers() & ~Syntax::Modifiers::Readonly);
        if (getter != nullptr)
            getter->Modifiers(getter->Modifiers() & ~Syntax::Modifiers::Readonly);

        // The C# `fieldDecl = propertyDeclaration.Parent?.Children.OfType<FieldDeclaration>()
        // .FirstOrDefault(fd => field.Equals(fd.GetSymbol()))` -- the reference-equality
        // `field.Equals(symbol)` is the C# `object.Equals` (the `IMember.Equals` overload takes
        // two arguments), so the port compares the resolved `IField` pointers.
        Syntax::FieldDeclaration* fieldDecl = nullptr;
        if (propertyDeclaration->Parent() != nullptr) {
            for (Syntax::AstNode* child : propertyDeclaration->Parent()->Children()) {
                auto* candidate = dynamic_cast<Syntax::FieldDeclaration*>(child);
                if (candidate == nullptr)
                    continue;
                const auto* candidateField =
                    dynamic_cast<const TS::IField*>(GetSymbol(*candidate));
                if (candidateField == field) {
                    fieldDecl = candidate;
                    break;
                }
            }
        }
        if (fieldDecl != nullptr) {
            fieldDecl->Remove();
            // Add C# 7.3 attributes on the backing field: the compiler-generated and
            // debugger-browsable attributes are dropped, the rest move to the property with the
            // `field` target.
            RemoveKnownAttribute(*fieldDecl, TS::KnownAttribute::CompilerGenerated);
            RemoveKnownAttribute(*fieldDecl, TS::KnownAttribute::DebuggerBrowsable);
            std::vector<Syntax::AttributeSection*> sections;
            for (int i = 0; i < fieldDecl->Attributes().Count(); i++)
                sections.push_back(fieldDecl->Attributes()[i]);
            for (Syntax::AttributeSection* section : sections) {
                section->AttributeTarget("field");
                propertyDeclaration->Attributes().Add(Syntax::Detach(section));
            }
        }
    }
    // Since the property instance is not changed, the visitor continues as usual, so return null.
    return nullptr;
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
