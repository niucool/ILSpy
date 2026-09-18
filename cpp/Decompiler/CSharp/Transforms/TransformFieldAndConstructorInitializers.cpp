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

#include "Decompiler/CSharp/Transforms/TransformFieldAndConstructorInitializers.hpp"

#include "Decompiler/CSharp/Annotations.hpp"
#include "Decompiler/CSharp/Transforms/TransformContext.hpp"
#include "Decompiler/CSharp/Syntax/AttributeSection.hpp"
#include "Decompiler/CSharp/Syntax/ConstructorInitializer.hpp"
#include "Decompiler/CSharp/Syntax/EventDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BaseReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/CastExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/IdentifierExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/InvocationExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/MemberReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ObjectCreateExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/ThisReferenceExpression.hpp"
#include "Decompiler/CSharp/Syntax/FieldDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/InvocationAstType.hpp"
#include "Decompiler/CSharp/Syntax/Modifiers.hpp"
#include "Decompiler/CSharp/Syntax/PatternMatching/PatternNodes.hpp"
#include "Decompiler/CSharp/Syntax/PatternPlaceholder.hpp"
#include "Decompiler/CSharp/Syntax/PropertyDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Syntax/Statements/ExpressionStatement.hpp"
#include "Decompiler/CSharp/Syntax/SyntaxExtensions.hpp"
#include "Decompiler/CSharp/Syntax/TypeDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/VariableInitializer.hpp"
#include "Decompiler/CSharp/Transforms/IntroduceUnsafeModifier.hpp"
#include "Decompiler/IL/Instructions/LdLoc.hpp"
#include "Decompiler/IL/Instructions/LdLoca.hpp"
#include "Decompiler/IL/Instructions/StLoc.hpp"
#include "Decompiler/Metadata/MetadataFile.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IModule.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"

#include <cassert>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Transforms {

using Syntax::AstNode;
using Syntax::AssignmentExpression;
using Syntax::BaseReferenceExpression;
using Syntax::BlockStatement;
using Syntax::CastExpression;
using Syntax::ConstructorDeclaration;
using Syntax::ConstructorInitializer;
using Syntax::EntityDeclaration;
using Syntax::EventDeclaration;
using Syntax::Expression;
using Syntax::ExpressionStatement;
using Syntax::FieldDeclaration;
using Syntax::IdentifierExpression;
using Syntax::InvocationExpression;
using Syntax::MemberReferenceExpression;
using Syntax::ObjectCreateExpression;
using Syntax::PropertyDeclaration;
using Syntax::Statement;
using Syntax::ThisReferenceExpression;
using Syntax::TypeDeclaration;
using Syntax::VariableInitializer;

namespace PM = Syntax::PatternMatching;
namespace TS = ::ILSpy::Decompiler::TypeSystem;

namespace {

// Owns the nodes of a pattern tree rebuilt for one use. The C# pattern definitions are
// `static readonly` (process lifetime); the port rebuilds an equivalent tree per call and
// keeps every node alive in this holder, so the non-owning child pointers the pattern nodes
// carry stay valid for the whole match (the PatternStatementTransform PatternTree precedent).
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
    // it can occupy an AST slot (or a collection slot) while still matching.
    template <class TNode>
    TNode* Wrap(std::shared_ptr<PM::Pattern> pattern) {
        return Make<Syntax::PatternPlaceholderNode<TNode>>(std::move(pattern));
    }

private:
    std::vector<std::unique_ptr<PM::INode>> nodes_;
};

// The C# `internal static readonly AstNode ThisCallClassPattern` (reference types):
// `this..ctor(...);` / `base..ctor(...);`, allowing an optional cast wrapper around the
// target. Built into `tree` each call.
Syntax::ExpressionStatement* BuildThisCallClassPattern(PatternTree& tree) {
    // The target choice: this, base, or (T)this / (T)base.
    auto outerChoice = std::make_shared<PM::Choice>();
    outerChoice->Add("target", tree.Make<ThisReferenceExpression>());
    outerChoice->Add("target", tree.Make<BaseReferenceExpression>());
    {
        auto innerChoice = std::make_shared<PM::Choice>();
        innerChoice->Add("target", tree.Make<ThisReferenceExpression>());
        innerChoice->Add("target", tree.Make<BaseReferenceExpression>());
        auto* cast = tree.Make<CastExpression>(
            tree.Wrap<Syntax::AstType>(std::make_shared<PM::AnyNode>()),
            tree.Wrap<Expression>(innerChoice));
        outerChoice->Add("target", cast);
    }
    auto* memberReference = tree.Make<MemberReferenceExpression>(
        tree.Wrap<Expression>(outerChoice), std::string(".ctor"));
    auto* invocation = tree.Make<InvocationExpression>(memberReference);
    invocation->Arguments().Add(tree.Wrap<Expression>(
        std::make_shared<PM::Repeat>(tree.Make<PM::AnyNode>())));
    auto* statement = tree.Make<ExpressionStatement>(tree.Wrap<Expression>(
        std::make_shared<PM::NamedNode>("invocation", invocation)));
    return statement;
}

// The C# `internal static readonly AstNode ThisCallStructPattern` (value types):
// `this = new TSelf(...);`. Built into `tree` each call.
ExpressionStatement* BuildThisCallStructPattern(PatternTree& tree) {
    auto* objectCreate = tree.Make<ObjectCreateExpression>(
        tree.Wrap<Syntax::AstType>(std::make_shared<PM::AnyNode>()));
    objectCreate->Arguments().Add(tree.Wrap<Expression>(
        std::make_shared<PM::Repeat>(tree.Make<PM::AnyNode>())));
    auto* assignment = tree.Make<AssignmentExpression>(
        tree.Wrap<Expression>(std::make_shared<PM::NamedNode>(
            "target", tree.Make<ThisReferenceExpression>())),
        tree.Wrap<Expression>(std::make_shared<PM::NamedNode>("invocation", objectCreate)));
    return tree.Make<ExpressionStatement>(assignment);
}

// The C# `static readonly ExpressionStatement memberInitializerPattern`: `this.F = value;`
// or `F = value;`. Built into `tree` each call.
ExpressionStatement* BuildMemberInitializerPattern(PatternTree& tree) {
    auto fieldAccessChoice = std::make_shared<PM::Choice>();
    {
        auto* memberReference = tree.Make<MemberReferenceExpression>(
            tree.Make<ThisReferenceExpression>(), std::string(PM::Pattern::AnyString));
        fieldAccessChoice->Add("fieldAccess", memberReference);
    }
    {
        auto* identifier = tree.Make<IdentifierExpression>(std::string(PM::Pattern::AnyString));
        fieldAccessChoice->Add("fieldAccess", identifier);
    }
    auto* assignment = tree.Make<AssignmentExpression>(
        tree.Wrap<Expression>(fieldAccessChoice),
        tree.Wrap<Expression>(std::make_shared<PM::AnyNode>("initializer")));
    return tree.Make<ExpressionStatement>(assignment);
}

// The C# `initializer.Annotations.OfType<ILInstruction>()` descendant scan (the
// `dependsOnBody` computation). Walks the IL instructions the initializer was translated
// from and reports whether any load/store references a parameter. The C# also checks the
// variable's `Function`, but `ILVariable` carries no owning function in the port (the
// `ILFunction` back-pointer is a later slice), so only the kind is checked -- for a field
// initializer translated from one constructor the two agree.
bool DependsOnFunctionParameter(Syntax::Expression& initializer) {
    std::vector<IL::ILInstruction*> work = CSharp::GetILInstructions(initializer);
    while (!work.empty()) {
        IL::ILInstruction* inst = work.back();
        work.pop_back();
        IL::ILVariable* variable = nullptr;
        if (auto* ld = dynamic_cast<IL::LdLoc*>(inst))
            variable = ld->Variable.get();
        else if (auto* lda = dynamic_cast<IL::LdLoca*>(inst))
            variable = lda->Variable.get();
        else if (auto* st = dynamic_cast<IL::StLoc*>(inst))
            variable = st->Variable.get();
        if (variable != nullptr && variable->Kind == IL::VariableKind::Parameter)
            return true;
        for (int i = 0; i < inst->ChildCount(); i++) {
            if (IL::ILInstruction* child = inst->GetChild(i))
                work.push_back(child);
        }
    }
    return false;
}

} // namespace

bool TransformFieldAndConstructorInitializers::IsGeneratedPrimaryConstructorBackingField(
    const TS::IField& field) {
    const std::string& name = field.Name();
    return name.size() >= 2 && name.front() == '<' && name.rfind(">P") == name.size() - 2
        && field.HasAttribute(TS::KnownAttribute::CompilerGenerated);
}

TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer::
    ConstructorInitializerAnalyzer(TransformContext& context,
                                   const TS::ITypeDefinition& typeDefinition,
                                   Syntax::TypeDeclaration* typeDeclaration)
    : context(context), TypeDefinition(typeDefinition), TypeDeclaration(typeDeclaration) {}

bool TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer::
    IsBeforeFieldInit() const {
    if (TypeDefinition.MetadataToken() == 0)
        return false;
    const ::ILSpy::Decompiler::Metadata::MetadataFile* metadata =
        context.TypeSystem().MainModule().MetadataFile();
    if (metadata == nullptr)
        return false;
    // TypeAttributes.BeforeFieldInit (II.23.1.15) -- 0x00100000.
    constexpr std::uint32_t beforeFieldInit = 0x00100000;
    return (metadata->GetTypeDefAttributes(TypeDefinition.MetadataToken()) & beforeFieldInit) != 0;
}

std::optional<TransformFieldAndConstructorInitializers::InitializerSequence>
TransformFieldAndConstructorInitializers::InitializerSequence::Analyze(
    ConstructorInitializerAnalyzer& context, ConstructorDeclaration& ctor,
    const TS::IMethod& ctorMethod) {
    InitializerSequence sequence;
    sequence.IsUnsafe = ctor.HasModifier(Syntax::Modifiers::Unsafe);

    std::vector<const TS::IMember*> initializedMembers;
    IL::ILFunction* function = CSharp::GetILFunction(ctor);
    const bool isStruct = ctorMethod.DeclaringType()->Kind() == TS::TypeKind::Struct;

    const bool onlyMoveConstants = !context.context.Settings().AlwaysMoveInitializer()
        && !context.IsBeforeFieldInit() && ctorMethod.IsStatic();

    bool skippedStmts = false;
    Statement* stmt = (ctor.Body() != nullptr && ctor.Body()->Statements().Count() > 0)
        ? ctor.Body()->Statements()[0]
        : nullptr;

    for (; stmt != nullptr; stmt = Syntax::GetNextStatement(stmt)) {
        PatternTree tree;
        Syntax::ExpressionStatement* pattern = BuildMemberInitializerPattern(tree);
        PM::Match m = PM::PatternExtensions::Match(*pattern, stmt);
        if (!m.Success())
            break;
        AstNode* fieldAccess = m.Get<AstNode>("fieldAccess").front();
        Expression* initializer = m.Get<Expression>("initializer").front();

        const TS::ISymbol* fieldAccessSymbol = CSharp::GetSymbol(*fieldAccess);
        const TS::IMember* member = fieldAccessSymbol != nullptr
            ? dynamic_cast<const TS::IMember*>(fieldAccessSymbol) : nullptr;
        if (member != nullptr)
            member = member->MemberDefinition();
        if (member == nullptr || !CanHaveInitializer(*member, context))
            break;

        if (onlyMoveConstants) {
            const auto* field = dynamic_cast<const TS::IField*>(member);
            if (field == nullptr || !field->IsConst()) {
                skippedStmts = true;
                continue;
            }
        }

        bool duplicate = false;
        for (const TS::IMember* seen : initializedMembers) {
            if (seen == member) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            initializedMembers.push_back(member);
        else
            sequence.HasDuplicateAssignments = true;

        (void)function; // The `v.Function == function` guard is part of the deferred
                        // parameter-dependency check (see DependsOnFunctionParameter).
        sequence.Statements.push_back(StatementEntry{
            stmt, member, initializer, DependsOnFunctionParameter(*initializer)});
    }

    if (!skippedStmts) {
        if (stmt == nullptr) {
            sequence.CoversFullBody = true;
        } else {
            PatternTree tree;
            AstNode* callPattern = isStruct ? static_cast<AstNode*>(BuildThisCallStructPattern(tree))
                                            : static_cast<AstNode*>(BuildThisCallClassPattern(tree));
            PM::Match m = PM::PatternExtensions::Match(*callPattern, stmt);
            if (m.Success())
                sequence.CoversFullBody = Syntax::GetNextStatement(stmt) == nullptr;
        }
    }

    return sequence;
}

bool TransformFieldAndConstructorInitializers::InitializerSequence::CanHaveInitializer(
    const TS::IMember& member, ConstructorInitializerAnalyzer& context) {
    if (!context.HasMemberMap)
        return false;
    auto it = context.MemberToDeclaringSyntaxNodeMap.find(&member);
    if (it == context.MemberToDeclaringSyntaxNodeMap.end())
        return true;
    Syntax::EntityDeclaration* declaringSyntaxNode = it->second;
    if (dynamic_cast<FieldDeclaration*>(declaringSyntaxNode) != nullptr)
        return true;
    if (auto* property = dynamic_cast<PropertyDeclaration*>(declaringSyntaxNode))
        return property->IsAutomaticProperty();
    return dynamic_cast<EventDeclaration*>(declaringSyntaxNode) != nullptr;
}

bool TransformFieldAndConstructorInitializers::InitializerSequence::IsMatch(
    ConstructorDeclaration& ctor) {
    if (ctor.Body() == nullptr)
        return false;
    auto& stmts = ctor.Body()->Statements();
    Statement* otherStmt = stmts.Count() > 0 ? stmts[0] : nullptr;
    for (auto& entry : Statements) {
        PatternTree tree;
        Syntax::ExpressionStatement* pattern = BuildMemberInitializerPattern(tree);
        PM::Match m = PM::PatternExtensions::Match(*pattern, otherStmt);
        if (!m.Success())
            return false;
        AstNode* fieldAccess = m.Get<AstNode>("fieldAccess").front();
        const TS::ISymbol* fieldAccessSymbol = CSharp::GetSymbol(*fieldAccess);
        const TS::IMember* otherMember = fieldAccessSymbol != nullptr
            ? dynamic_cast<const TS::IMember*>(fieldAccessSymbol) : nullptr;
        if (otherMember != nullptr)
            otherMember = otherMember->MemberDefinition();
        if (otherMember == nullptr || !entry.Member->Equals(otherMember, nullptr))
            return false;
        Expression* otherInitializer = m.Get<Expression>("initializer").front();
        if (!PM::PatternExtensions::IsMatch(*entry.Initializer, otherInitializer))
            return false;
        auto& list = StatementToOtherCtorsMap[entry.Statement];
        list.push_back({otherStmt, otherInitializer});
        otherStmt = Syntax::GetNextStatement(otherStmt);
    }
    return true;
}

bool TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer::Analyze(
    const std::vector<EntityDeclaration*>& members) {
    HasMemberMap = true;
    MemberToDeclaringSyntaxNodeMap.clear();
    for (EntityDeclaration* member : members) {
        const TS::ISymbol* symbol = CSharp::GetSymbol(*member);
        const TS::IMember* member2 = symbol != nullptr
            ? dynamic_cast<const TS::IMember*>(symbol) : nullptr;
        if (member2 != nullptr)
            MemberToDeclaringSyntaxNodeMap[member2] = member;
    }

    std::vector<ConstructorDeclaration*> constructorsNotChainedWithThis;
    std::vector<ConstructorDeclaration*> allCtors;

    // The C# `RecordDecompiler` (records) and the non-record primary-constructor conversion
    // block are deferred (the `DecompileRun.RecordDecompilers` map is unported), so the loop
    // only classifies static and ordinary instance constructors.
    for (EntityDeclaration* member : members) {
        auto* ctor = dynamic_cast<ConstructorDeclaration*>(member);
        if (ctor == nullptr)
            continue;
        const TS::IMethod* ctorMethod =
            dynamic_cast<const TS::IMethod*>(CSharp::GetSymbol(*ctor));
        if (ctorMethod == nullptr)
            continue;

        if (ctorMethod->IsStatic()) {
            StaticConstructor = ctorMethod;
            StaticConstructorDecl = ctor;
        } else {
            PatternTree tree;
            Statement* stmt = (ctor->Body() != nullptr && ctor->Body()->Statements().Count() > 0)
                ? ctor->Body()->Statements()[0]
                : nullptr;
            AstNode* pattern = ctorMethod->DeclaringType()->Kind() == TS::TypeKind::Struct
                ? static_cast<AstNode*>(BuildThisCallStructPattern(tree))
                : static_cast<AstNode*>(BuildThisCallClassPattern(tree));
            PM::Match m = PM::PatternExtensions::Match(*pattern, stmt);

            allCtors.push_back(ctor);

            if (m.Success()) {
                auto target = m.Get<Expression>("target");
                if (!target.empty() && dynamic_cast<ThisReferenceExpression*>(target.front()) != nullptr)
                    continue;
            }
            constructorsNotChainedWithThis.push_back(ctor);
        }
    }

    if (StaticConstructor != nullptr) {
        StaticInitializers = InitializerSequence::Analyze(
            *this, *StaticConstructorDecl, *StaticConstructor);
    }

    if (!constructorsNotChainedWithThis.empty()) {
        if (TypeDefinition.Kind() != TS::TypeKind::Struct
            || (context.Settings().StructDefaultConstructorsAndFieldInitializers()
                && !TypeDefinition.IsRecord())) {
            const bool isPrimaryCtor =
                constructorsNotChainedWithThis[0] == PrimaryConstructorDecl;
            std::optional<InitializerSequence> sequence = isPrimaryCtor
                ? PrimaryConstructorInitializers
                : InitializerSequence::Analyze(
                      *this, *constructorsNotChainedWithThis[0],
                      *dynamic_cast<const TS::IMethod*>(
                          CSharp::GetSymbol(*constructorsNotChainedWithThis[0])));

            if (!sequence)
                return false;

            bool sequenceMatchesAllCtors = true;
            for (std::size_t i = 1; i < constructorsNotChainedWithThis.size(); i++) {
                if (!sequence->IsMatch(*constructorsNotChainedWithThis[i])) {
                    sequenceMatchesAllCtors = false;
                    break;
                }
            }

            if (!sequenceMatchesAllCtors) {
                if (isPrimaryCtor)
                    return false;
            } else if (!isPrimaryCtor) {
                bool dependsOnBody = false;
                for (auto& s : sequence->Statements) {
                    if (s.DependsOnConstructorBody)
                        dependsOnBody = true;
                }
                if (!dependsOnBody)
                    InstanceInitializers = std::move(sequence);
            }
        }
    }

    InstanceConstructors = allCtors;
    return true;
}

bool TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer::
    MoveConstructorInitializer(ConstructorDeclaration& constructorDeclaration,
                               const TS::IMethod& ctorMethod) {
    if (constructorDeclaration.Body() == nullptr)
        return false;
    auto& stmts = constructorDeclaration.Body()->Statements();
    Statement* stmt = stmts.Count() > 0 ? stmts[0] : nullptr;
    const bool isValueType = ctorMethod.DeclaringType()->Kind() == TS::TypeKind::Struct;

    // Value types may omit the constructor initializer completely.
    if (stmt == nullptr && isValueType)
        return true;

    PatternTree tree;
    AstNode* pattern = isValueType ? static_cast<AstNode*>(BuildThisCallStructPattern(tree))
                                   : static_cast<AstNode*>(BuildThisCallClassPattern(tree));
    PM::Match m = PM::PatternExtensions::Match(*pattern, stmt);
    if (!m.Success())
        return isValueType;

    AstNode* invocation = m.Get<AstNode>("invocation").front();
    const TS::IMethod* ctor = dynamic_cast<const TS::IMethod*>(CSharp::GetSymbol(*invocation));
    if (ctor == nullptr || !ctor->IsConstructor())
        return false;

    Syntax::ConstructorInitializerType type =
        ctor->DeclaringTypeDefinition() == ctorMethod.DeclaringTypeDefinition()
            ? Syntax::ConstructorInitializerType::This
            : Syntax::ConstructorInitializerType::Base;

    auto* ci = new ConstructorInitializer(type);

    context.Step("Move constructor call to initializer", stmt);
    invocation->GetChildren(&Syntax::Slots::Argument).MoveTo(ci->Arguments());
    if (!(type == Syntax::ConstructorInitializerType::Base && ci->Arguments().Count() == 0)) {
        CSharp::CopyAnnotationsFrom(ci, *invocation);
        constructorDeclaration.Initializer(ci);
    } else {
        delete ci;
    }

    stmt->Remove();
    context.EndStep(constructorDeclaration.Initializer());
    return true;
}

bool TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer::
    MoveFieldInitializersToDeclarations(InitializerSequence& sequence, InitializerKind kind) {
    for (auto& entry : sequence.Statements) {
        Statement* stmt = entry.Statement;
        const TS::IMember* member = entry.Member;
        Expression* initializer = entry.Initializer;
        const bool dependsOnBody = entry.DependsOnConstructorBody;
        assert(!dependsOnBody || kind == InitializerKind::Primary);

        auto it = MemberToDeclaringSyntaxNodeMap.find(member);
        if (it == MemberToDeclaringSyntaxNodeMap.end()) {
            if (kind == InitializerKind::Primary) {
                context.Step("Remove redundant primary constructor assignment", stmt);
                stmt->Remove();
            }
            continue;
        }

        EntityDeclaration* declaringSyntaxNode = it->second;

        if (auto* fieldDeclaration = dynamic_cast<FieldDeclaration*>(declaringSyntaxNode)) {
            VariableInitializer* v = fieldDeclaration->Variables()[0];
            if (v->Initializer() == nullptr) {
                context.Step("Move assignment to field initializer", stmt);
                Expression* movedInitializer = Syntax::Detach(initializer);
                v->Initializer(movedInitializer);
                context.EndStep(movedInitializer);
            } else if (kind == InitializerKind::Static) {
                // The C# compares the existing initializer against a re-evaluated decimal
                // constant (`TryEvaluateDecimalConstant`); that evaluator needs the
                // CSharpInvocationResolveResult argument/constant machinery (unported), so
                // the port keeps the existing initializer and leaves the statement alone.
                continue;
            } else {
                continue;
            }
        } else if (auto* propertyDeclaration = dynamic_cast<PropertyDeclaration*>(declaringSyntaxNode)) {
            if (propertyDeclaration->Initializer() == nullptr) {
                context.Step("Move assignment to property initializer", stmt);
                Expression* movedInitializer = Syntax::Detach(initializer);
                propertyDeclaration->Initializer(movedInitializer);
                context.EndStep(movedInitializer);
            } else {
                continue;
            }
        } else if (auto* eventDeclaration = dynamic_cast<EventDeclaration*>(declaringSyntaxNode)) {
            VariableInitializer* v = eventDeclaration->Variables()[0];
            if (v->Initializer() == nullptr) {
                context.Step("Move assignment to event initializer", stmt);
                Expression* movedInitializer = Syntax::Detach(initializer);
                v->Initializer(movedInitializer);
                context.EndStep(movedInitializer);
            } else {
                continue;
            }
        } else {
            continue;
        }

        stmt->Remove();

        {
            auto otherCtors = sequence.StatementToOtherCtorsMap.find(stmt);
            if (otherCtors != sequence.StatementToOtherCtorsMap.end()) {
                for (auto& other : otherCtors->second)
                    other.first->Remove();
                // The C# preserves the discarded copies in a
                // MemberInitializerInOtherConstructorsAnnotation for breakpoint emission;
                // that annotation type is not ported, so the copies are simply removed.
            }
        }

        if (sequence.IsUnsafe && IntroduceUnsafeModifier::IsUnsafe(*initializer)) {
            context.Step("Add unsafe modifier to initialized member", declaringSyntaxNode);
            declaringSyntaxNode->Modifiers(
                declaringSyntaxNode->Modifiers() | Syntax::Modifiers::Unsafe);
        }
    }
    return true;
}

void TransformFieldAndConstructorInitializers::ConstructorInitializerAnalyzer::
    RemoveImplicitConstructor() {
    assert(HasMemberMap);
    if (TypeDeclaration == nullptr)
        return;

    // The primary-constructor body removal is deferred with the primary-constructor
    // conversion (PrimaryConstructor is always null).

    if (StaticConstructor != nullptr) {
        if (IsBeforeFieldInit() && StaticConstructorDecl->Body() != nullptr
            && StaticConstructorDecl->Body()->Statements().Count() == 0) {
            context.Step("Remove empty static constructor", StaticConstructorDecl);
            StaticConstructorDecl->Remove();
        }
    }

    if (InstanceConstructors.size() != 1)
        return;

    ConstructorDeclaration* ctor = InstanceConstructors[0];
    const TS::IMethod* ctorMethod =
        dynamic_cast<const TS::IMethod*>(CSharp::GetSymbol(*ctor));
    if (ctorMethod == nullptr)
        return;

    if (TypeDefinition.Kind() == TS::TypeKind::Struct && ctorMethod->Parameters().empty()
        && InstanceInitializers) {
        return;
    }

    ConstructorDeclaration emptyCtorPattern;
    emptyCtorPattern.Modifiers(TypeDefinition.IsAbstract() ? Syntax::Modifiers::Protected
                                                           : Syntax::Modifiers::Public);
    if (ctor->HasModifier(Syntax::Modifiers::Unsafe))
        emptyCtorPattern.Modifiers(emptyCtorPattern.Modifiers() | Syntax::Modifiers::Unsafe);
    auto emptyBody = std::make_unique<BlockStatement>();
    emptyCtorPattern.Body(emptyBody.get());

    if (PM::PatternExtensions::IsMatch(emptyCtorPattern, ctor)) {
        // The C# retention check (`ShowXmlDocumentation` + DocumentationProvider) is skipped
        // because the provider is unported.
        context.Step("Remove implicit constructor", ctor);
        ctor->Remove();
    }
}

void TransformFieldAndConstructorInitializers::Run(AstNode& node, TransformContext& context) {
    context_ = &context;

    if (context.CurrentTypeDefinition() != nullptr) {
        std::vector<EntityDeclaration*> members;
        for (AstNode* child = node.FirstChild(); child != nullptr; child = child->NextSibling()) {
            if (auto* entity = dynamic_cast<EntityDeclaration*>(child))
                members.push_back(entity);
        }
        TransformDeclaration(*context.CurrentTypeDefinition(), node, members);
    }

    for (AstNode* descendant : node.Descendants()) {
        auto* typeDeclaration = dynamic_cast<TypeDeclaration*>(descendant);
        if (typeDeclaration == nullptr)
            continue;
        const TS::ITypeDefinition* currentTypeDefinition =
            dynamic_cast<const TS::ITypeDefinition*>(CSharp::GetSymbol(*typeDeclaration));
        if (currentTypeDefinition == nullptr)
            continue;
        std::vector<EntityDeclaration*> members;
        for (int i = 0; i < typeDeclaration->Members().Count(); i++)
            members.push_back(typeDeclaration->Members()[i]);
        TransformDeclaration(*currentTypeDefinition, *typeDeclaration, members);
    }

    context_ = nullptr;
}

bool TransformFieldAndConstructorInitializers::TransformDeclaration(
    const TS::ITypeDefinition& currentTypeDefinition, AstNode& node,
    const std::vector<EntityDeclaration*>& members) {
    ConstructorInitializerAnalyzer analyzer(*context_, currentTypeDefinition,
                                            dynamic_cast<TypeDeclaration*>(&node));

    if (!analyzer.Analyze(members))
        return false;

    if (analyzer.PrimaryConstructorInitializers
        && !analyzer.PrimaryConstructorInitializers->HasDuplicateAssignments) {
        analyzer.MoveFieldInitializersToDeclarations(*analyzer.PrimaryConstructorInitializers,
                                                     InitializerKind::Primary);
    } else if (analyzer.InstanceInitializers
               && !analyzer.InstanceInitializers->HasDuplicateAssignments) {
        analyzer.MoveFieldInitializersToDeclarations(*analyzer.InstanceInitializers,
                                                     InitializerKind::Instance);
    }

    if (analyzer.StaticInitializers && !analyzer.StaticInitializers->HasDuplicateAssignments) {
        analyzer.MoveFieldInitializersToDeclarations(*analyzer.StaticInitializers,
                                                     InitializerKind::Static);
    }

    for (EntityDeclaration* member : members) {
        auto* constructorDeclaration = dynamic_cast<ConstructorDeclaration*>(member);
        if (constructorDeclaration == nullptr)
            continue;
        const TS::IMethod* ctorMethod =
            dynamic_cast<const TS::IMethod*>(CSharp::GetSymbol(*constructorDeclaration));
        if (ctorMethod == nullptr)
            continue;
        analyzer.MoveConstructorInitializer(*constructorDeclaration, *ctorMethod);
    }

    analyzer.RemoveImplicitConstructor();

    // The primary-constructor backing-field reannotation is deferred with the
    // primary-constructor conversion (BackingFieldToPrimaryConstructorParameterVariableMap is
    // always null).
    return false;
}

} // namespace ILSpy::Decompiler::CSharp::Transforms
