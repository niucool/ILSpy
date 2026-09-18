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

// Port of ICSharpCode.Decompiler/CSharp/Transforms/TransformFieldAndConstructorInitializers.cs:
// moves field initializers from the start of a constructor into the field/property/event
// declarations, turns this-/base-ctor calls into constructor initializers, and removes the
// implicit default constructor. The analysis phase (`ConstructorInitializerAnalyzer` +
// `InitializerSequence`) and the mutation phase (`MoveConstructorInitializer`,
// `MoveFieldInitializersToDeclarations`, `RemoveImplicitConstructor`) are both ported.
//
// Deferred (each named at its call site):
//   * The record support (`RecordDecompiler`, `GetParameterToBackingStoreMap`,
//     `IsCopyConstructor`) -- the port's `DecompileRun` does not carry `RecordDecompilers` yet,
//     so the analyzer always sees `RecordDecompiler == null`.
//   * The non-record primary-constructor conversion block -- it composes the ILVariable-index
//     to parameter mapping and the XML-documentation provider (both unported), so
//     `PrimaryConstructor`/`PrimaryConstructorInitializers` stay null and the type keeps its
//     ordinary constructors.
//   * The XML-documentation retention checks (`ShowXmlDocumentation` +
//     `DecompileRun.DocumentationProvider`) -- the provider is unported, so the checks are
//     skipped (the transform removes the implicit constructor, matching the C# no-provider
//     path).
//   * `TryEvaluateDecimalConstant` -- the static-decimal-constant comparison needs the
//     `CSharpInvocationResolveResult` argument/constant machinery; the port treats it as a
//     non-match (the C# `else` branch).

#pragma once

#include "Decompiler/CSharp/Syntax/ConstructorDeclaration.hpp"
#include "Decompiler/CSharp/Syntax/Statements/BlockStatement.hpp"
#include "Decompiler/CSharp/Transforms/IAstTransform.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ILSpy::Decompiler::TypeSystem {
class IField;
class IMember;
class IMethod;
class ITypeDefinition;
}

namespace ILSpy::Decompiler::CSharp::Syntax {
class EntityDeclaration;
class TypeDeclaration;
}

namespace ILSpy::Decompiler::CSharp::Transforms {

// The C# `public class TransformFieldAndConstructorInitializers : IAstTransform`. The nested
// `InitializerSequence` and `ConstructorInitializerAnalyzer` are `public` here (the C# declares
// them private, but the port widens them for direct TDD -- the TypeSystemAstBuilder precedent).
class TransformFieldAndConstructorInitializers final : public IAstTransform {
public:
    // The C# `internal static bool IsGeneratedPrimaryConstructorBackingField(IField field)`:
    // a compiler-generated field named `<...>P` (the C# 12 primary-constructor backing field).
    static bool IsGeneratedPrimaryConstructorBackingField(
        const ::ILSpy::Decompiler::TypeSystem::IField& field);

    // The C# `enum InitializerKind`.
    enum class InitializerKind {
        Static,
        Instance,
        Primary,
    };

    class ConstructorInitializerAnalyzer;

    // The C# `class InitializerSequence`.
    class InitializerSequence {
    public:
        // The C# `List<(Statement Statement, IMember Member, Expression Initializer,
        // bool DependsOnConstructorBody)> Statements`.
        struct StatementEntry {
            Syntax::Statement* Statement;
            const ::ILSpy::Decompiler::TypeSystem::IMember* Member;
            Syntax::Expression* Initializer;
            bool DependsOnConstructorBody;
        };

        std::vector<StatementEntry> Statements;

        // The C# `Dictionary<Statement, List<(Statement, Expression)>>? StatementToOtherCtorsMap`.
        std::unordered_map<Syntax::AstNode*,
                           std::vector<std::pair<Syntax::Statement*, Syntax::Expression*>>>
            StatementToOtherCtorsMap;

        bool HasDuplicateAssignments = false;
        bool IsUnsafe = false;
        bool CoversFullBody = false;

        // The C# `static InitializerSequence? Analyze(ConstructorInitializerAnalyzer context,
        // ConstructorDeclaration ctor, IMethod ctorMethod)`.
        static std::optional<InitializerSequence> Analyze(
            ConstructorInitializerAnalyzer& context, Syntax::ConstructorDeclaration& ctor,
            const ::ILSpy::Decompiler::TypeSystem::IMethod& ctorMethod);

        // The C# `bool IsMatch(ConstructorDeclaration ctor)`.
        bool IsMatch(Syntax::ConstructorDeclaration& ctor);

    private:
        static bool CanHaveInitializer(
            const ::ILSpy::Decompiler::TypeSystem::IMember& member,
            ConstructorInitializerAnalyzer& context);
    };

    // The C# `class ConstructorInitializerAnalyzer`.
    class ConstructorInitializerAnalyzer {
    public:
        ConstructorInitializerAnalyzer(TransformContext& context,
                                       const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition& typeDefinition,
                                       Syntax::TypeDeclaration* typeDeclaration);

        TransformContext& context;
        const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition& TypeDefinition;
        Syntax::TypeDeclaration* TypeDeclaration = nullptr;

        // The C# `Dictionary<IMember, EntityDeclaration> MemberToDeclaringSyntaxNodeMap` (null
        // until `Analyze` builds it). The port stores a presence flag beside the map.
        std::unordered_map<const ::ILSpy::Decompiler::TypeSystem::IMember*, Syntax::EntityDeclaration*>
            MemberToDeclaringSyntaxNodeMap;
        bool HasMemberMap = false;

        std::optional<InitializerSequence> InstanceInitializers;
        std::optional<InitializerSequence> StaticInitializers;
        std::optional<InitializerSequence> PrimaryConstructorInitializers;

        const ::ILSpy::Decompiler::TypeSystem::IMethod* StaticConstructor = nullptr;
        Syntax::ConstructorDeclaration* StaticConstructorDecl = nullptr;

        const ::ILSpy::Decompiler::TypeSystem::IMethod* PrimaryConstructor = nullptr;
        Syntax::ConstructorDeclaration* PrimaryConstructorDecl = nullptr;

        std::vector<Syntax::ConstructorDeclaration*> InstanceConstructors;

        // The C# `bool IsBeforeFieldInit` -- whether the type definition carries the
        // `BeforeFieldInit` TypeAttributes flag (II.23.1.15, 0x00100000).
        bool IsBeforeFieldInit() const;

        // The C# `bool Analyze(IEnumerable<AstNode> members)`.
        bool Analyze(const std::vector<Syntax::EntityDeclaration*>& members);

        // The C# `bool MoveConstructorInitializer(ConstructorDeclaration, IMethod)`.
        bool MoveConstructorInitializer(Syntax::ConstructorDeclaration& constructorDeclaration,
                                        const ::ILSpy::Decompiler::TypeSystem::IMethod& ctorMethod);

        // The C# `bool MoveFieldInitializersToDeclarations(InitializerSequence, InitializerKind)`.
        bool MoveFieldInitializersToDeclarations(InitializerSequence& sequence, InitializerKind kind);

        // The C# `void RemoveImplicitConstructor()`.
        void RemoveImplicitConstructor();
    };

    void Run(Syntax::AstNode& node, TransformContext& context) override;

private:
    TransformContext* context_ = nullptr;

    bool TransformDeclaration(const ::ILSpy::Decompiler::TypeSystem::ITypeDefinition& currentTypeDefinition,
                              Syntax::AstNode& node,
                              const std::vector<Syntax::EntityDeclaration*>& members);
};

} // namespace ILSpy::Decompiler::CSharp::Transforms
