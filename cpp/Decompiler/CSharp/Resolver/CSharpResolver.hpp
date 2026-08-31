// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in
// the Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
// OR OTHER DEALINGS IN THE SOFTWARE.

// Port of ICSharpCode.Decompiler/CSharp/Resolver/CSharpResolver.cs lines 49-322 -- the
// class skeleton of the main resolver: the two public ctors + the private full ctor, the
// `Compilation` / `CurrentTypeResolveContext` / `CheckForOverflow` / `IsWithinLambda
// Expression` / `CurrentMember` / `CurrentUsingScope` / `CurrentTypeDefinition` property
// surface with the `With*` clone factories, the per-CurrentTypeDefinition cache, the
// `ImmutableStack`-based local-variable management, and the object-initializer context.
// The C# comment calls this class "the main resolver logic"; the 2986-line class resolves
// expressions, and every `With*` factory produces a NEW resolver (the immutable-resolver
// pattern -- the resolver state is never mutated in place; `AddVariables` /
// `PushObjectInitializer` also return clones). The C# class remark: "This class is
// thread-safe."
//
// This is the first slice of the CSharpResolver mega-class (the 2986-line long pole);
// the skeleton is fully landable now that its prerequisites are ported:
// `CSharpTypeResolveContext` / `UsingScope` (the iteration-94 pair), `CSharpConversions
// ::Get` (the per-compilation cached factory), `Util::ImmutableStack` (the IL-reader
// evaluation-stack spine), and `ErrorResolveResult::UnknownError` (the null
// object-initializer sentinel). The `Resolve*` arms (ResolveSimpleName,
// ResolveMemberAccess, ResolveUnaryOperator, ...) land in later slices.
//
// KEY PORT CONVENTIONS:
//  (a) The C# `With*` clone factories return a `CSharpResolver` REFERENCE -- the C#
//      heap-allocated clone handed to the caller. The port returns an owning
//      `std::shared_ptr<CSharpResolver>` (the `UsingScope::WithNestedNamespace` /
//      `CSharpTypeResolveContext::WithUsingScope` concrete-clone-factory convention):
//      every consumer chains `With*` factories and `Resolve*` calls
//      (`resolver->WithCurrentTypeDefinition(td)->ResolveSimpleName(...)`), and the
//      shared ownership mirrors the C# GC references held by AST-node annotations.
//      Two factories return `this` when nothing would change (`WithCheckForOverflow`
//      with the same flag, `WithCurrentTypeDefinition` with the same definition) -- the
//      port implements the identity-preserving early-out through
//      `std::enable_shared_from_this` (`shared_from_this()` + `const_pointer_cast`, the
//      D529 convention), so a resolver MUST be shared-managed (`std::make_shared`) before
//      those two factories are called on it; the null weak back-reference would
//      otherwise throw `bad_weak_ptr` (the documented discipline shared by every
//      `enable_shared_from_this` port).
//  (b) The C# `public class CSharpResolver : ICodeContext` interface derivation is
//      DEFERRED (a documented shape deviation, no behavioral loss): deriving the ported
//      `ICodeContext` (TypeSystem/ICodeContext.hpp, ported with this skeleton) would
//      force the resolver's `WithCurrentTypeDefinition` / `WithCurrentMember` overrides
//      to the interface's `std::unique_ptr<ITypeResolveContext>` signature -- losing the
//      concrete `std::shared_ptr<CSharpResolver>` clone returns every future
//      `Resolve*`-arm and ExpressionBuilder port chains on. `ICodeContext` has NO other
//      C# consumer (the C# interface exists solely for `CSharpResolver` itself), and
//      the resolver already exposes the full interface contract structurally
//      (`LocalVariables()` + `IsWithinLambdaExpression()` + the four resolution slots).
//      The derivation can land later together with the first consumer that needs the
//      interface view (e.g. an adapter that wraps the shared resolver handle).
//  (c) The C# `internal readonly CSharpConversions conversions` field (read by
//      CallBuilder.cs / ExpressionBuilder.cs as `resolver.conversions`) ports to a
//      `CSharpConversions&` reference member plus a PUBLIC `Conversions()` accessor (the
//      port has no assembly-internal visibility level; the `CSharpConversions::Get`
//      factory returns a reference backed by the compilation's CacheManager, which
//      outlives the resolver because the resolver itself holds `const ICompilation&`).
//  (d) The local-variable stack is `ImmutableStack<Dictionary<string, IVariable>>` --
//      the port is `Util::ImmutableStack<std::shared_ptr<const VariableMap>>` where
//      `VariableMap` is the `Dictionary<string, IVariable>` (the values are owning
//      `shared_ptr<const IVariable>` handles; the map keys are the variable names).
//      `AddVariables` takes the CALLER'S dictionary as a `shared_ptr<const VariableMap>`
//      (the C# pushes the reference, so clones share the same dictionary -- the shared
//      handle preserves that sharing; the C# callers build a block's dictionary fully
//      before adding it, so the `const` restriction costs nothing). `LocalVariables()`
//      flattens the stack TOP-FIRST (the `ImmutableStack` enumerates LIFO, so the
//      innermost block's variables come first -- the C# `SelectMany` over the stack).
//  (e) The per-CurrentTypeDefinition cache (`TypeDefinitionCache`) and the
//      object-initializer context (`ObjectInitializerContext`) are the C# private
//      nested classes, ported as private nested classes. The cache's three
//      `Dictionary<string, ResolveResult>` lookup caches store NULLABLE results (a
//      stored null is a known-negative cache entry -- `TryGetValue(identifier, out r)`
//      can find a null), so the port maps are `std::unordered_map<std::string,
//      std::shared_ptr<ResolveResult>>` where an empty handle is the C# null. The
//      resolver holds the cache via `shared_ptr` (nullable -- null when there is no
//      current type definition) and clones SHARE the same cache instance (the C#
//      reference field), so the future `ResolveSimpleName` arms' cache population is
//      visible across every clone sharing the cache.
//  (f) The C# `Throw` guards port per the established convention: `ArgumentNullException`
//      -> `std::invalid_argument` (the `IntersectionType::Create` precedent);
//      `InvalidOperationException` on popping an empty object-initializer stack ->
//      `std::runtime_error` (the `CreateResolveResult` precedent). The two
//      `CSharpResolver(ICompilation)` / `CSharpResolver(CSharpTypeResolveContext)`
//      ctor null guards: the compilation is a non-null reference (the D374 convention,
//      structurally unreachable), the context is a nullable `shared_ptr` parameter and
//      IS guarded.
//  (g) Namespace note: from this namespace (`ILSpy::Decompiler::CSharp::Resolver`), the
//      name `TypeSystem` resolves to the SIBLING `ILSpy::Decompiler::CSharp::TypeSystem`
//      (`CSharpTypeResolveContext` / `UsingScope` live there), NOT to
//      `ILSpy::Decompiler::TypeSystem` (`ICompilation` / `IVariable` / ... live there)
//      -- every cross-namespace reference below is fully qualified for that reason
//      (the iteration-94 MSVC namespace-reopening learning).

#pragma once

#include "Decompiler/CSharp/TypeSystem/CSharpTypeResolveContext.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/ExpressionType.hpp"
#include "Decompiler/TypeSystem/IVariable.hpp"
#include "Decompiler/Util/ImmutableStack.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// Forward-declared at GLOBAL scope (the iteration-94 MSVC namespace-reopening learning:
// a qualified namespace-definition written inside the enclosing namespace below would
// resolve against the enclosing namespace's own members): the two AST operator enums the
// resolver's operator-resolution regions consume. They are defined in the Syntax
// expression headers (the C# `CSharpResolver.cs` itself `using`s
// `ICSharpCode.Decompiler.CSharp.Syntax` for exactly these enums); a scoped-enum forward
// declaration needs only the (implicit `int`) underlying type, keeping the AST headers
// out of the resolver's include graph -- the .cpp includes the full headers for the
// switch bodies.
namespace ILSpy::Decompiler::CSharp::Syntax {
enum class UnaryOperatorType;
enum class BinaryOperatorType;
}

namespace ILSpy::Decompiler::CSharp::Resolver {

// Forward-declared (same namespace, in CSharpConversions.hpp): the conversion
// controller the resolver resolves per-compilation (`CSharpConversions::Get`) and
// exposes through `Conversions()`. A reference return/member needs only a declaration;
// the .cpp includes the full header for the `Get` call.
class CSharpConversions;

// Forward-declared (same namespace, in OverloadResolution.hpp): the overload-resolution
// engine `CreateResolveResultForUserDefinedOperator` reads the best-candidate state of.
// A reference parameter needs only a declaration; the .cpp includes the full header for
// the `BestCandidateErrors` / `CreateResolveResult` / `BestCandidate` /
// `GetArgumentsWithConversions` calls.
class OverloadResolution;

// The C# `Dictionary<string, IVariable>` -- one block's local variables / lambda
// parameters keyed by name. The values are owning handles; the resolver's clones share
// the same dictionary (header convention (d)).
using VariableMap = std::unordered_map<
    std::string, std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IVariable>>;

// The C# `public class CSharpResolver : ICodeContext` (the interface derivation is
// deferred, header convention (b)) -- the main resolver logic.
class CSharpResolver : public std::enable_shared_from_this<CSharpResolver> {
public:
    // The C# `public CSharpResolver(ICompilation compilation)` -- resolves the
    // conversions via the per-compilation factory and starts from a fresh context over
    // the compilation's main module. The null-compilation `ArgumentNullException` is
    // structurally unreachable through the reference parameter (the D374 convention).
    // Construct via `std::make_shared` (the `enable_shared_from_this` discipline,
    // header convention (a)).
    explicit CSharpResolver(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation);

    // The C# `public CSharpResolver(CSharpTypeResolveContext context)` -- starts from
    // the given context, building the per-type-definition cache when the context
    // carries a current type definition. The C# null-context `ArgumentNullException`
    // ports to `std::invalid_argument` (header convention (f)).
    explicit CSharpResolver(
        std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context);

    // ---- Properties -------------------------------------------------------------------------
    // The C# `public ICompilation Compilation { get; }`.
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const { return compilation_; }

    // The C# `internal readonly CSharpConversions conversions` -- exposed as a public
    // accessor (header convention (c)); the `CallBuilder` / `ExpressionBuilder` ports
    // read the resolver's conversions through it.
    CSharpConversions& Conversions() const { return conversions_; }

    // The C# `public CSharpTypeResolveContext CurrentTypeResolveContext { get; }` --
    // returns the owning handle (clones share the context through it).
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>
    CurrentTypeResolveContext() const
    {
        return context_;
    }

    // The C# `public bool CheckForOverflow { get; }` -- whether the current context is
    // checked.
    bool CheckForOverflow() const { return checkForOverflow_; }

    // The C# `public CSharpResolver WithCheckForOverflow(bool)` -- a clone with the flag
    // replaced; returns `this` (identity-preserving) when the flag is unchanged.
    std::shared_ptr<CSharpResolver> WithCheckForOverflow(bool checkForOverflow) const;

    // The C# `public bool IsWithinLambdaExpression { get;}` -- whether the resolver is
    // currently within a lambda expression or anonymous method.
    bool IsWithinLambdaExpression() const { return isWithinLambdaExpression_; }

    // The C# `public CSharpResolver WithIsWithinLambdaExpression(bool)` -- a clone with
    // the flag replaced (no identity-preserving early-out in the C#).
    std::shared_ptr<CSharpResolver> WithIsWithinLambdaExpression(bool isWithinLambdaExpression) const;

    // The C# `public IMember CurrentMember { get; }` -- the current member definition
    // used to look up identifiers as parameters or type parameters (delegates to the
    // context's slot; nullable).
    const ILSpy::Decompiler::TypeSystem::IMember* CurrentMember() const
    {
        return context_->CurrentMember();
    }

    // The C# `public CSharpResolver WithCurrentMember(IMember member)` -- a clone whose
    // context has the member slot replaced (the C# remark: "Don't forget to also set
    // CurrentTypeDefinition when setting CurrentMember; setting one of the properties
    // does not automatically set the other"). The nullable C# parameter ports to a
    // nullable pointer (pass `nullptr` to clear).
    std::shared_ptr<CSharpResolver> WithCurrentMember(
        const ILSpy::Decompiler::TypeSystem::IMember* member) const;

    // The C# `public UsingScope CurrentUsingScope { get; }` -- the current using scope
    // used to look up identifiers as class names (delegates to the context's slot;
    // nullable).
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> CurrentUsingScope() const
    {
        return context_->CurrentUsingScope();
    }

    // The C# `public CSharpResolver WithCurrentUsingScope(UsingScope usingScope)` -- a
    // clone whose context has the using-scope slot replaced (nullable handle).
    std::shared_ptr<CSharpResolver> WithCurrentUsingScope(
        std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope) const;

    // The C# `public ITypeDefinition CurrentTypeDefinition { get; }` -- the current type
    // definition (delegates to the context's slot; nullable).
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* CurrentTypeDefinition() const
    {
        return context_->CurrentTypeDefinition();
    }

    // The C# `public CSharpResolver WithCurrentTypeDefinition(ITypeDefinition
    // typeDefinition)` -- a clone whose context has the type-definition slot replaced
    // AND a freshly built per-type-definition cache (or a null cache when clearing);
    // returns `this` (identity-preserving) when the definition is unchanged.
    std::shared_ptr<CSharpResolver> WithCurrentTypeDefinition(
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition) const;

    // ---- Local Variable Management ----------------------------------------------------------
    // The C# `public CSharpResolver AddVariables(Dictionary<string, IVariable>
    // variables)` -- adds new variables or lambda parameters to the current block (the
    // caller's dictionary, shared with the clone -- header convention (d)).
    std::shared_ptr<CSharpResolver> AddVariables(
        std::shared_ptr<const VariableMap> variables) const;

    // The C# `public IEnumerable<IVariable> LocalVariables` -- all currently visible
    // local variables and lambda parameters (not method parameters), innermost block
    // first. Returns an owning snapshot.
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IVariable>> LocalVariables() const;

    // ---- Object Initializer Context ---------------------------------------------------------
    // The C# `public CSharpResolver PushObjectInitializer(ResolveResult
    // initializedObject)` -- pushes the object that is currently being initialized. The
    // C# null-argument `ArgumentNullException` ports to `std::invalid_argument` for a
    // null handle (header convention (f)).
    std::shared_ptr<CSharpResolver> PushObjectInitializer(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> initializedObject) const;

    // The C# `public CSharpResolver PopObjectInitializer()` -- pops the innermost
    // object initializer. The C# empty-stack `InvalidOperationException` ports to
    // `std::runtime_error` (header convention (f)).
    std::shared_ptr<CSharpResolver> PopObjectInitializer() const;

    // The C# `public bool IsInObjectInitializer { get; }` -- whether this context is
    // within an object initializer.
    bool IsInObjectInitializer() const { return objectInitializerStack_ != nullptr; }

    // The C# `public ResolveResult CurrentObjectInitializer { get; }` -- the current
    // object initializer (usually an `InitializedObjectResolveResult` or a semantic tree
    // based on one); the `ErrorResolveResult.UnknownError` singleton when there is no
    // object initializer.
    const ILSpy::Decompiler::Semantics::ResolveResult& CurrentObjectInitializer() const;

    // The C# `public IType CurrentObjectInitializerType { get; }` -- the type of the
    // object currently being initialized (the C# `SharedTypes.Unknown` when no
    // initializer is open -- the null-initializer sentinel's `UnknownType`).
    const ILSpy::Decompiler::TypeSystem::IType& CurrentObjectInitializerType() const
    {
        return CurrentObjectInitializer().Type();
    }

    // ---- User-Defined Operator Candidates ---------------------------------------------------
    // (The `Get user-defined operator candidates` region, CSharpResolver.cs lines
    // 1278-1322, plus the two `GetOverloadableOperatorName` statics at lines 566-583 /
    // 1207-1241 -- the shared prerequisite machinery the `ResolveUnaryOperator` and
    // `ResolveBinaryOperator` regions consume; those regions land in later slices.)

    // The C# `static string GetOverloadableOperatorName(UnaryOperatorType op)` (line 567)
    // -- the metadata method name of the overloadable unary operator (`op_Increment` /
    // `op_Decrement` for the pre- AND post-forms), or the C# `null` for the
    // non-overloadable kinds (Any / Dereference / AddressOf / Await / the
    // null-conditional family / the pattern kinds). Private static in the C#; PUBLIC
    // static in the port for direct TDD (the port has no assembly-internal visibility
    // level -- the same widening applied to the CSharpOperators `internal` members).
    // The C# nullable string return ports to a `const char*` where `nullptr` is the C#
    // `null` (the fixed literal table, the `BinaryOperatorExpression::GetOperatorToken`
    // convention); the future `ResolveUnaryOperator` region's
    // `overloadableOperatorName == null` check ports to `name == nullptr`.
    static const char* GetOverloadableOperatorName(
        ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op);

    // The C# `static string GetOverloadableOperatorName(BinaryOperatorType op)` (line
    // 1208) -- the metadata method name of the overloadable binary operator (the 16
    // arithmetic / bitwise / shift / comparison names), or the C# `null` for the
    // non-overloadable kinds (Any / ConditionalAnd / ConditionalOr / NullCoalescing /
    // Range / IsPattern). Private static in the C#; PUBLIC static in the port (the unary
    // overload above).
    static const char* GetOverloadableOperatorName(
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op);

    // The C# `public IEnumerable<IParameterizedMember> GetUserDefinedOperatorCandidates(
    // IType type, string operatorName)` (line 1280) -- the candidate user-defined
    // operators (C# spec draft-v11 section 12.4.6) for the given operand type and
    // metadata operator name: the type's own `IsOperator` methods with the matching name,
    // each liftable one followed by its lifted `Nullable<T>` form (the originals all come
    // first, then the lifted forms in the same order). The primitive operand types are
    // EXCLUDED (the `TypeCode` [Boolean..Decimal] gate) even when the .NET framework
    // exposes built-in operators with those metadata names -- "we must not use those as
    // user-defined operators (we would skip numeric promotion)". The C# declared element
    // type is `IParameterizedMember` but every element is an `IMethod` (the early outs
    // return the `EmptyList<IMethod>.Instance` and the list is a `List<IMethod>`), so the
    // port returns `std::shared_ptr<IMethod>` elements directly.
    //
    // The C# `List<IMethod>` holds GC references with no ownership distinction -- the
    // originals are owned by the type system while the appended lifted forms are freshly
    // allocated `LiftedUserDefinedOperator` instances the list must keep alive. The
    // port's vector unifies the two: the originals are stored as NON-OWNING aliases (a
    // no-op deleter -- the type system owns them for the compilation's lifetime, the
    // `KnownTypeCache::SearchType` non-owning-alias convention; the `const_cast`
    // reconciles the `GetMethods` const-return contract, the D515 convention), while
    // the lifted forms are the OWNING handles `CSharpOperators::LiftUserDefinedOperator`
    // returns. A caller feeding the entries to `OverloadResolution::AddCandidate` must
    // hold the vector (and, per the engine's raw-pointer convention, the compilation) for
    // as long as the resolution state is read -- the same discipline every
    // `OverloadResolutionCandidate` caller already follows.
    //
    // The C# nullable `string operatorName` parameter ports to a `const char*` (nullptr
    // is the C# `null`; the empty-list early out). Reads no resolver instance state (only
    // `ReflectionHelper.GetTypeCode`, `IType::GetMethods`, and the static
    // `CSharpOperators::LiftUserDefinedOperator`), so the method is `const`.
    std::vector<std::shared_ptr<ILSpy::Decompiler::TypeSystem::IMethod>>
    GetUserDefinedOperatorCandidates(
        const ILSpy::Decompiler::TypeSystem::IType& type, const char* operatorName) const;

    // The C# `ResolveResult CreateResolveResultForUserDefinedOperator(OverloadResolution
    // r, ExpressionType operatorType)` (line 1309) -- the resolve result for an operator
    // resolved to a USER-DEFINED operator method: the error path delegates to
    // `r.CreateResolveResult(null)` (the invocation result carrying the applicability
    // errors), the success path builds an `OperatorResolveResult` over the best
    // candidate's method (the `ILiftedOperator` cross-cast marking the lifted forms),
    // with the conversion-wrapped operands. Private instance method in the C#; PUBLIC
    // STATIC in the port -- it reads no resolver instance state (only the
    // `OverloadResolution` public surface), and the widening makes the region directly
    // testable ahead of the `ResolveUnaryOperator` / `ResolveBinaryOperator` consumers
    // (the CSharpOperators `internal`-widening convention).
    static std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
    CreateResolveResultForUserDefinedOperator(
        ILSpy::Decompiler::CSharp::Resolver::OverloadResolution& r,
        ILSpy::Decompiler::TypeSystem::ExpressionType operatorType);

private:
    // The C# private nested `sealed class ObjectInitializerContext` -- the linked stack
    // of objects being initialized (`prev` is the enclosing initializer; nullable).
    struct ObjectInitializerContext {
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> initializedObject;
        std::shared_ptr<ObjectInitializerContext> prev;

        ObjectInitializerContext(
            std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> initialized,
            std::shared_ptr<ObjectInitializerContext> previous)
            : initializedObject(std::move(initialized)), prev(std::move(previous)) {}
    };

    // The C# private nested `sealed class TypeDefinitionCache` -- the per-current-type-
    // definition lookup caches the future `ResolveSimpleName` arms populate. The three
    // dictionaries store NULLABLE results (an empty `shared_ptr` is the C# stored-null
    // known-negative entry, header convention (e)).
    class TypeDefinitionCache {
    public:
        const ILSpy::Decompiler::TypeSystem::ITypeDefinition& TypeDefinition;
        std::unordered_map<std::string, std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
            SimpleNameLookupCacheExpression;
        std::unordered_map<std::string, std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
            SimpleNameLookupCacheInvocationTarget;
        std::unordered_map<std::string, std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
            SimpleTypeLookupCache;

        explicit TypeDefinitionCache(
            const ILSpy::Decompiler::TypeSystem::ITypeDefinition& typeDefinition)
            : TypeDefinition(typeDefinition) {}
    };

    // The C# private full ctor -- the single construction path the `With*` clone
    // factories thread every field through. `make_shared` cannot reach it (its
    // construction happens outside the class's private access, the
    // `UsingScope::WithNestedNamespace` precedent), so the factories allocate via
    // `new`.
    CSharpResolver(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
                   CSharpConversions& conversions,
                   std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context,
                   bool checkForOverflow,
                   bool isWithinLambdaExpression,
                   std::shared_ptr<TypeDefinitionCache> currentTypeDefinitionCache,
                   Util::ImmutableStack<std::shared_ptr<const VariableMap>> localVariableStack,
                   std::shared_ptr<ObjectInitializerContext> objectInitializerStack);

    // The C# private `CSharpResolver WithContext(CSharpTypeResolveContext newContext)`
    // -- the shared clone helper the `With*` factories that replace a context slot call.
    std::shared_ptr<CSharpResolver> WithContext(
        std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> newContext) const;

    // The C# private `CSharpResolver WithLocalVariableStack(...)` -- the clone helper
    // `AddVariables` calls.
    std::shared_ptr<CSharpResolver> WithLocalVariableStack(
        Util::ImmutableStack<std::shared_ptr<const VariableMap>> stack) const;

    // The C# private `CSharpResolver WithObjectInitializerStack(...)` -- the clone
    // helper `PushObjectInitializer` / `PopObjectInitializer` call.
    std::shared_ptr<CSharpResolver> WithObjectInitializerStack(
        std::shared_ptr<ObjectInitializerContext> stack) const;

    // The C# private `void LiftUserDefinedOperators(List<IMethod> operators)` (line
    // 1296) -- appends the lifted `Nullable<T>` form of every operator in the list,
    // capturing the ORIGINAL count as the loop bound first so the freshly-appended
    // lifted forms are themselves not lifted again (a lifted operator's parameters are
    // `Nullable<T>`, so a second lift would find nothing liftable -- the fixed bound
    // makes that structural, never even calling the lift). Mutates only the caller's
    // vector; reads no resolver instance state (only the static `CSharpOperators::
    // LiftUserDefinedOperator`), so the method is `const`. Private like the C#; tested
    // transitively through the public `GetUserDefinedOperatorCandidates`.
    void LiftUserDefinedOperators(
        std::vector<std::shared_ptr<ILSpy::Decompiler::TypeSystem::IMethod>>& operators) const;

    // The `enable_shared_from_this` bridge for the two C# `return this` early-outs
    // (header convention (a)).
    std::shared_ptr<CSharpResolver> Self() const;

    // The C# field set (all `readonly`; the immutable-resolver pattern).
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation_;
    CSharpConversions& conversions_;
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context_;
    bool checkForOverflow_ = false;
    bool isWithinLambdaExpression_ = false;
    std::shared_ptr<TypeDefinitionCache> currentTypeDefinitionCache_;
    Util::ImmutableStack<std::shared_ptr<const VariableMap>> localVariableStack_;
    std::shared_ptr<ObjectInitializerContext> objectInitializerStack_;
};

} // namespace ILSpy::Decompiler::CSharp::Resolver
