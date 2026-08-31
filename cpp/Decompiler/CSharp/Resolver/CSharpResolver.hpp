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
// object-initializer sentinel). The `Resolve*` arms land in later slices
// (`ResolveUnaryOperator` and `ResolveBinaryOperator` + the enum-handler trio have
// landed; ResolveSimpleName, ResolveMemberAccess, ... follow).
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

#include <any>
#include <cstdint>
#include <memory>
#include <optional>
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

// Forward-declared at GLOBAL scope (the same iteration-94 convention): the conversion
// description the `Convert` / `TryConvert` members take by shared handle (the C#
// `Conversion` class reference; the shared_ptr declaration needs only a forward
// declaration -- the .cpp includes the full header for the flag reads) and the BCL
// `TypeCode` the `CSharpPrimitiveCast` wrapper takes (defined in ReflectionHelper.hpp;
// a scoped-enum forward declaration needs the underlying type, the
// CSharpConversionsHelpers.hpp precedent).
namespace ILSpy::Decompiler::Semantics {
class Conversion;
}
namespace ILSpy::Decompiler::TypeSystem { enum class TypeCode : std::uint8_t; }

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

// Forward-declared (same namespace, in CSharpOperators.hpp): the operator-method class
// the `PointerArithmeticOperator` factory below builds and returns by shared handle (the
// C# `CSharpOperators.BinaryOperatorMethod`). A shared_ptr return type needs only a
// declaration; the .cpp includes the full header for the construction.
class BinaryOperatorMethod;

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

    // ---- Convert / ResolveCast ----------------------------------------------------------------
    // (The `ResolveCast` region, CSharpResolver.cs lines 1319-1470, plus the private
    // `GetEnumUnderlyingType` member from the "Enum helper methods" region at line 985 --
    // the conversion-application machinery the operator-resolution slices consume:
    // `TryConvert` / `TryConvertEnum` rebind an operand through an implicit conversion in
    // place, `Convert` wraps or constant-folds, `ResolveCast` is the public
    // cast-expression entry (the future `ResolveCastExpression` arm delegates to it),
    // and the `CSharpPrimitiveCast` wrapper threads the resolver's `CheckForOverflow`
    // flag into the `Util` constant converter.)

    // The C# `bool TryConvert(ref ResolveResult rr, IType targetType)` (line 1320) -- if
    // an implicit conversion of `rr` to `targetType` exists, applies it to `rr` and
    // returns true; otherwise returns false and leaves `rr` unmodified. Private instance
    // method in the C#; PUBLIC in the port for direct TDD ahead of the
    // `ResolveUnaryOperator` / `ResolveBinaryOperator` slices that consume it (the
    // CSharpOperators internal-widening convention -- the port has no visibility level
    // between the public surface and the untestable private one). The C# `ref ResolveResult
    // rr` ports to `std::shared_ptr<ResolveResult>&` -- the caller's rebindable variable
    // (the C# reassigns the caller's variable through the `ref`; the `rr = Convert(...)`
    // rebind is the port's shared_ptr assignment). Reads only `conversions_`, so the
    // method is `const`.
    bool TryConvert(std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr,
                    ILSpy::Decompiler::TypeSystem::IType& targetType) const;

    // The C# `bool TryConvertEnum(ref ResolveResult rr, IType targetType, ref bool
    // isNullable, ref ResolveResult enumRR, bool allowConversionFromConstantZero = true)`
    // (line 1341) -- the enum-aware TryConvert: tries the non-nullable target first
    // (skipped when `isNullable` is already true), then rebinds the TARGET to its
    // `Nullable<T>` form (a LOCAL rebind -- the C# parameter is by-value, so the caller's
    // `targetType` is untouched; a C++ reference cannot rebind, so the port threads the
    // rebound target through a local `IType*`) and retries; on the nullable success also
    // wraps `enumRR` in the `ImplicitNullableConversion` unless it is already nullable,
    // and sets `isNullable = true`. The `allowConversionFromConstantZero` gate (default
    // true) rejects ENUMERATION conversions -- the implicit constant-0-to-enum conversion
    // -- when false (the user-defined-operator comparison context where the C# compiler
    // does not apply the constant-zero rule). Private in the C#; PUBLIC in the port for
    // direct TDD (the TryConvert widening convention). `rr` / `isNullable` / `enumRR` are
    // the C# `ref` parameters (the callers' rebindable variables); on failure all three
    // are left unmodified.
    bool TryConvertEnum(std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr,
                        ILSpy::Decompiler::TypeSystem::IType& targetType,
                        bool& isNullable,
                        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& enumRR,
                        bool allowConversionFromConstantZero = true) const;

    // The C# `ResolveResult Convert(ResolveResult rr, IType targetType)` (line 1381) --
    // the convenience overload resolving the implicit conversion first, then delegating
    // to the 3-arg `Convert`. Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> Convert(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr,
        ILSpy::Decompiler::TypeSystem::IType& targetType) const;

    // The C# `ResolveResult Convert(ResolveResult rr, IType targetType, Conversion c)`
    // (line 1386) -- the conversion application: an IDENTITY conversion returns `rr`
    // unwrapped (pointer identity -- the C# `c == Conversion.IdentityConversion`
    // reference comparison ports to singleton pointer identity, the D536 convention); a
    // COMPILE-TIME CONSTANT under a non-None non-user-defined conversion is
    // CONSTANT-FOLDED through `ResolveCast` (the constant re-resolves through the target
    // type); everything else wraps in a `ConversionResolveResult` carrying the resolver's
    // `checkForOverflow` flag. Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> Convert(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr,
        ILSpy::Decompiler::TypeSystem::IType& targetType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> c) const;

    // The C# `public ResolveResult ResolveCast(IType targetType, ResolveResult
    // expression)` (line 1396, C# spec draft-v11 section 12.9.8 Cast expressions) -- the
    // cast-expression entry: resolves the EXPLICIT conversion first, then constant-folds
    // a compile-time constant under a non-user-defined conversion -- through the target's
    // enum-underlying `TypeCode` via `CSharpPrimitiveCast` (an `OverflowException` /
    // `InvalidCastException` downgrades to an `ErrorResolveResult`), with the
    // `string`-target passthrough (a null or string constant stays a constant; any other
    // constant to `string` is an error) and the native-integer arm (an `nint` / `nuint`
    // target casts the constant through the 32-bit code with `checkForOverflow: true` --
    // a C# HARDCODED flag, NOT the resolver's own -- and an overflow falls back to the
    // non-constant `ConversionResolveResult` because "the conversion is not a compile-time
    // constant"). A USER-DEFINED conversion or a non-constant expression skips the
    // folding entirely and wraps. `expression` is an owning handle (the C# reference the
    // GC owns; every fold path re-derives the folded constant from it, every wrap path
    // stores it as the `ConversionResolveResult` input).
    //
    // NOTE: the `targetType.GetEnumUnderlyingType()` call (line 1403) resolves to the
    // TypeUtils EXTENSION method (the receiver is the `IType` -- the resolver's own
    // same-name member below never applies to an `IType` receiver), so the port calls the
    // TypeUtils free function (iteration 99), NOT the member. The distinction is
    // observable: for a non-enum target the extension returns the target itself (the
    // folding reads the target's own `TypeCode`), while the member would return null (a
    // definition's `EnumUnderlyingType` is null for non-enums).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveCast(
        ILSpy::Decompiler::TypeSystem::IType& targetType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const;

    // The C# `internal object CSharpPrimitiveCast(TypeCode targetType, object input)`
    // (line 1467) -- the wrapper threading the resolver's `CheckForOverflow` flag into
    // the `Util` constant converter. Internal in the C#; PUBLIC in the port (the
    // assembly-internal widening convention). The C# `object` ports to `std::any` (the
    // D424 boxed-constant convention).
    std::any CSharpPrimitiveCast(ILSpy::Decompiler::TypeSystem::TypeCode targetType,
                                 const std::any& input) const;

    // The C# private `IType GetEnumUnderlyingType(IType enumType)` (line 985, the "Enum
    // helper methods" region) -- the resolver MEMBER variant: the definition's
    // `EnumUnderlyingType` when the definition resolves, else the `SpecialType.UnknownType`
    // null object. NOT the TypeUtils extension -- C# member lookup finds the private
    // member for the UNQUALIFIED `GetEnumUnderlyingType(enumType)` calls inside the class
    // (the enum-comparison handlers and the binary numeric promotions, later slices),
    // shadowing the extension; the extension applies only where the call is QUALIFIED on
    // an `IType` receiver, as in `ResolveCast` above. Unlike the extension, the member
    // does NOT unwrap custom modifiers and does NOT pass a non-enum through: a
    // definition-bearing NON-enum reports a NULL `EnumUnderlyingType` (the faithful C#
    // `ITypeDefinition.EnumUnderlyingType` for non-enums), a definitionless type reports
    // `UnknownType`. Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention). The return is non-owning (the definition owns the
    // underlying handle, reachable through `enumType`; the `UnknownType` fallback is a
    // program-lifetime static singleton -- the minimal port's `UnknownType()` allocates a
    // fresh instance per call, so the singleton must be materialized once here).
    const ILSpy::Decompiler::TypeSystem::IType* GetEnumUnderlyingType(
        const ILSpy::Decompiler::TypeSystem::IType& enumType) const;

    // ---- Numeric promotion -------------------------------------------------------------------
    // (The unary/binary numeric-promotion region -- CSharpResolver.cs lines 536-561
    // (`UnaryNumericPromotion`, C# spec draft-v11 section 12.4.7.2) plus lines 1055-1230
    // (`MakeNullable`, `BinaryNumericPromotion` section 12.4.7.3, `IsSigned`, the two
    // `CastTo` overloads) -- the operand-shaping machinery the future `ResolveUnaryOperator`
    // (line 420) / `ResolveBinaryOperator` (line 667) slices consume after the built-in
    // operator overload resolution.)

    // The C# private `IType MakeNullable(IType type, bool isNullable)` (line 1055) -- the
    // `Nullable<T>` wrapper factory: the nullable form via `NullableType.Create`, the input
    // itself otherwise. The C# `IType` return is an owning handle in the port (the
    // `Create` arm builds a fresh `ParameterizedType` that must outlive the call; the
    // passthrough arm recovers the input's own owning handle via `shared_from_this` +
    // `const_pointer_cast` -- the D529 convention: the const is the accessor's contract,
    // the underlying type-system object is shared-managed; a non-shared-managed input
    // would throw `bad_weak_ptr`, the documented stub discipline). Private in the C#;
    // PUBLIC in the port for direct TDD (the TryConvert widening convention).
    ILSpy::Decompiler::TypeSystem::ITypePtr MakeNullable(
        const ILSpy::Decompiler::TypeSystem::IType& type, bool isNullable) const;

    // The C# private `ResolveResult UnaryNumericPromotion(UnaryOperatorType op, ref IType
    // type, bool isNullable, ResolveResult expression)` (line 536, spec section 12.4.7.2
    // -- the unary promotions: `-` on `uint` promotes to `long`; `+`/`~` on the small
    // unsigned types [char..ushort] promotes to `int`; a nullable null literal is treated
    // as `sbyte` so the promotion to `int32` fires for it too). The C# `ref IType type`
    // rebinds the CALLER's type variable to the promoted type -- a C++ reference cannot
    // rebind, so the port threads the caller's rebindable pointer (`const IType*&` -- the
    // rebinds target `FindType` results, whose return is `const IType&`; the reads are all
    // const). The C# enum relational comparisons port through `static_cast<int>` (the
    // D514 convention). Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> UnaryNumericPromotion(
        ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
        const ILSpy::Decompiler::TypeSystem::IType*& type,
        bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const;

    // The C# private `bool IsSigned(TypeCode code, ResolveResult rr)` (line 1190) -- the
    // signed-primitive test the `UInt64`/`UInt32` promotion arms consult, with the
    // implicit-constant-expression-conversion exceptions: a NON-NEGATIVE `int`/`long`
    // compile-time constant counts as unsigned (`(int)rr.ConstantValue >= 0`). STATIC in
    // the port -- it reads no resolver instance state (the
    // `CreateResolveResultForUserDefinedOperator` convention). The C# unbox `(int)` /
    // `(long)` ports to the pointer-form `std::any_cast` (nullptr on a held-type mismatch,
    // the safe faithful fallback for a shape the C# would throw `InvalidCastException` on
    // -- the mismatched box counts as signed). Private in the C#; PUBLIC in the port for
    // direct TDD (the TryConvert widening convention).
    static bool IsSigned(ILSpy::Decompiler::TypeSystem::TypeCode code,
                         const std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr);

    // The C# private `ResolveResult CastTo(TypeCode targetType, bool isNullable,
    // ResolveResult expression, bool allowNullableConstants)` (line 1214) -- delegates to
    // the `IType` overload through `FindType` (the `ReflectionHelper` extension -- the
    // `ICompilation.FindType` takes a `KnownTypeCode`, so the `TypeCode` receiver resolves
    // to the extension, iteration 87). Private in the C#; PUBLIC in the port for direct
    // TDD (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> CastTo(
        ILSpy::Decompiler::TypeSystem::TypeCode targetType,
        bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
        bool allowNullableConstants) const;

    // The C# private `ResolveResult CastTo(IType targetType, bool isNullable, ResolveResult
    // expression, bool allowNullableConstants)` (line 1219) -- the promotion conversion:
    // an operand already in the target shape returns UNCHANGED; a compile-time constant
    // under `allowNullableConstants` folds through `ResolveCast` (a null constant folds to
    // a null constant over the target shape; an error or non-constant fall-through skips
    // the fold); everything else wraps through `Convert` with the
    // `ImplicitNullableConversion` / `ImplicitNumericConversion` singleton. `targetType`
    // is non-const (the `ResolveCast` / `Convert` calls take non-const `IType&`, the
    // iteration-100 signatures). Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> CastTo(
        ILSpy::Decompiler::TypeSystem::IType& targetType,
        bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
        bool allowNullableConstants) const;

    // The C# private `bool BinaryNumericPromotion(bool isNullable, ref ResolveResult lhs,
    // ref ResolveResult rhs, bool allowNullableConstants)` (line 1065, spec section
    // 12.4.7.3 -- the binary promotions over the underlying types: the `decimal` target
    // (with the float/double binding error), `double`, `float`, `uint64` (with the
    // signed-operand binding error), the native-integer `nuint`/`nint` targets, the
    // `uint`-with-signed-operand `long` promotion, `long`, and the default `int`; the null
    // literal promotes to the OTHER operand's type code first). The C# `ref ResolveResult`
    // ports to `std::shared_ptr<ResolveResult>&` (the caller's rebindable variables, the
    // TryConvert convention). Returns `!bindingError`. Private in the C#; PUBLIC in the
    // port for direct TDD (the TryConvert widening convention).
    bool BinaryNumericPromotion(
        bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rhs,
        bool allowNullableConstants) const;

    // ---- Operator-resolution helpers ----------------------------------------------------------
    // (The non-recursive helper region the `ResolveUnaryOperator` (line 326, landed) /
    // `ResolveBinaryOperator` (line 594, a later slice) regions consume -- CSharpResolver.cs
    // lines 525/ 962-985/981-989/1253-1275/2435-2441: the two `OperatorResolveResult`
    // factories, the pointer-arithmetic operator factory, the null-coalescing handler, the
    // nullable-or- non-value-type test, and the `OverloadResolution` construction helper. The
    // mutually recursive enum handlers (`HandleEnumComparison` / `HandleEnumSubtraction` /
    // `HandleEnumOperator`) land below with `ResolveBinaryOperator` itself -- this region
    // is everything they call that is NOT self-recursive.)

    // The C# private `bool IsNullableTypeOrNonValueType(IType type)` (line 981) -- the
    // null-literal comparison guard: a nullable type OR a non-value type (a reference
    // type OR an INDETERMINATE one) compares against the null literal. The C#
    // `type.IsReferenceType != false` is true for BOTH a definite true and an
    // INDETERMINATE `null` (the lifted `bool? != false` yields null [falsy] only for a
    // definite false), so the port is `IsNullable(type) || !(opt.has_value() && !*opt)`.
    // STATIC in the port -- reads no resolver instance state (the `IsSigned` convention).
    // Private in the C#; PUBLIC in the port for direct TDD (the TryConvert widening
    // convention).
    static bool IsNullableTypeOrNonValueType(
        const ILSpy::Decompiler::TypeSystem::IType& type);

    // The C# private `OperatorResolveResult UnaryOperatorResolveResult(IType resultType,
    // UnaryOperatorType op, ResolveResult expression, bool isLifted = false)` (line 525)
    // -- the predefined unary-operator result factory: an `OperatorResolveResult` over
    // the mapped BCL `ExpressionType` (`UnaryOperatorExpression.GetLinqNodeType(op,
    // this.CheckForOverflow)` -- the checked/unchecked distinction threads the resolver's
    // own flag), NO user-defined method, the `isLifted` flag, and the SINGLE operand. The
    // C# `IType` reference becomes an owning handle via `shared_from_this()` (the D529
    // convention; the input is a type-system-owned `IType&`). Private in the C#; PUBLIC in
    // the port for direct TDD (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> UnaryOperatorResolveResult(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
        bool isLifted = false) const;

    // The C# private `ResolveResult BinaryOperatorResolveResult(IType resultType,
    // ResolveResult lhs, BinaryOperatorType op, ResolveResult rhs, bool isLifted = false)`
    // (line 983) -- the predefined binary-operator result factory: the mirror of the
    // unary factory with TWO operands (the C# `new[] { lhs, rhs }` order). Private in the
    // C#; PUBLIC in the port for direct TDD (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> BinaryOperatorResolveResult(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs,
        bool isLifted = false) const;

    // The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
    // resultType, IType inputType1, KnownTypeCode inputType2)` (line 962) -- the two Known-
    // TypeCode-carrying conveniences delegating through `compilation.FindType` (the
    // pointer-arithmetic operands `byte`/`sbyte`/... resolve to the registered known
    // types). Private in the C#; PUBLIC in the port for direct TDD (the TryConvert
    // widening convention).
    std::shared_ptr<BinaryOperatorMethod> PointerArithmeticOperator(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        const ILSpy::Decompiler::TypeSystem::IType& inputType1,
        ILSpy::Decompiler::TypeSystem::KnownTypeCode inputType2) const;

    // The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
    // resultType, KnownTypeCode inputType1, IType inputType2)` (line 967) -- the mirror
    // convenience (the FIRST operand's code).
    std::shared_ptr<BinaryOperatorMethod> PointerArithmeticOperator(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        ILSpy::Decompiler::TypeSystem::KnownTypeCode inputType1,
        const ILSpy::Decompiler::TypeSystem::IType& inputType2) const;

    // The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
    // resultType, IType inputType1, IType inputType2)` (line 972) -- builds the plain
    // built-in operator method over the resolver's compilation with the given return type
    // and the two unnamed `DefaultParameter`s (`string.Empty` names; the C# object-
    // initializer collection-add over the internal `parameters` list ports to the
    // `OperatorMethod::AddParameter` member, the `SetReturnType` internal-setter
    // convention). The parameter type handles are recovered via `shared_from_this()` +
    // `const_pointer_cast` (the D529 convention). Private in the C#; PUBLIC in the port
    // for direct TDD (the TryConvert widening convention).
    std::shared_ptr<BinaryOperatorMethod> PointerArithmeticOperator(
        const ILSpy::Decompiler::TypeSystem::IType& resultType,
        const ILSpy::Decompiler::TypeSystem::IType& inputType1,
        const ILSpy::Decompiler::TypeSystem::IType& inputType2) const;

    // The C# private `ResolveResult ResolveNullCoalescingOperator(ResolveResult lhs,
    // ResolveResult rhs)` (line 1253, spec section 12.13) -- the `??` handler: a NULLABLE
    // lhs first tries the rhs against the UNDERLYING type (the result type is the
    // underlying); then the rhs against the lhs's own type; then the lhs against the rhs's
    // type; else an `ErrorResolveResult` over the lhs's type. Every `TryConvert` rebind
    // mutates only the LOCAL parameter copy (the C# parameters are value copies of the
    // caller's references -- the `ref` rebinds do not escape), so the port takes the
    // handles by value. Private in the C#; PUBLIC in the port for direct TDD (the
    // TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveNullCoalescingOperator(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // The C# private `OverloadResolution CreateOverloadResolution(ResolveResult[] arguments,
    // string[] argumentNames = null, IType[] typeArguments = null)` (line 2435) -- the
    // resolver's own `OverloadResolution` construction helper threading the instance's
    // compilation, conversions, and `CheckForOverflow` flag (every operator-resolution /
    // invocation region builds its resolution through it). The C# returns a class
    // instance; the port returns an owning `unique_ptr` (the
    // `MethodGroupResolveResult::PerformOverloadResolution` convention). The C# argument
    // arrays port to a `std::vector` / `std::optional<std::vector>` pair (the ctor's
    // null-default normalization). Private in the C#; PUBLIC in the port for direct TDD
    // (the TryConvert widening convention).
    std::unique_ptr<OverloadResolution> CreateOverloadResolution(
        std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
        std::optional<std::vector<std::string>> argumentNames = std::nullopt,
        std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> typeArguments =
            std::nullopt) const;

    // ---- ResolveUnaryOperator ----------------------------------------------------------------
    // (The `ResolveUnaryOperator` region, CSharpResolver.cs lines 326-530, C# spec
    // draft-v11 section 12.4.4 "Unary operator overload resolution" -- the first of
    // the two big operator-resolution entry points. Every non-recursive helper it
    // consumes is already ported: the two `GetOverloadableOperatorName` statics, the
    // user-defined candidate scan + lift, `CreateResolveResultForUserDefinedOperator`,
    // `CreateOverloadResolution`, `UnaryNumericPromotion`, the `CSharpOperators`
    // operator tables + their `Invoke` constant-evaluation virtuals, the two
    // `OperatorResolveResult` factories, `Convert`/`ResolveCast`, `MakeNullable`, and
    // the `TypeUtils.IsCSharpNativeIntegerType` leaf.)

    // The C# `public ResolveResult ResolveUnaryOperator(UnaryOperatorType op,
    // ResolveResult expression)` (line 326) -- the unary-operator resolution: the
    // dynamic-operand arm (an `await` of a dynamic expression builds the dynamic
    // `AwaitResolveResult` shape; every other operator over a dynamic operand is the
    // dynamic `OperatorResolveResult`), the non-overloadable arms (`*` dereferences a
    // pointer to its element type; `&` addresses to a fresh `PointerType`; the
    // non-dynamic `await` throws (the documented deferral below); anything else is the
    // `UnknownError` singleton), then the overloadable path: the user-defined operator
    // overload resolution first (an applicable user-defined operator wins), the unary
    // numeric promotion, the per-operator built-in table resolution, and the result
    // composition -- constant-folding a compile-time constant operand through the
    // operator's `Invoke` (an `ArithmeticException` downgrades to an
    // `ErrorResolveResult`), else wrapping the operand through `Convert` into the
    // predefined `OperatorResolveResult` (marking the lifted forms).
    //
    // PORT CONVENTIONS for this member:
    //  * The C# `IType type = NullableType.GetUnderlyingType(expression.Type)` LOCAL is
    //    rebound by `UnaryNumericPromotion(op, ref type, ...)` -- the port threads it as a
    //    local `const IType*` (a C++ reference cannot rebind; the `UnaryNumericPromotion`
    //    `const IType*&` signature takes the pointer by reference). Every read below
    //    dereferences the local pointer.
    //  * The C# `catch (ArithmeticException)` around `m.Invoke(this, expression
    //    .ConstantValue)` (line 510) ports to `catch (const Util::ArithmeticException&)`:
    //    the operand cast inside `Invoke` throws `Util::OverflowException` /
    //    `Util::InvalidCastException` and the table bodies throw the typed
    //    `Util::OverflowException` / `Util::DivideByZeroException` -- the family base
    //    swallows exactly the C# `ArithmeticException` family, and `InvalidCastException`
    //    (deliberately NOT a family member) propagates out, faithfully.
    //  * The C# `compilation.FindType(expression.ConstantValue.GetType())` in the `BitNot`
    //    enum constant-folding arm (line 451) ports to the TypeCode-based `FindType`
    //    over `Util::TypeCodeOfBoxedValue` (an enum constant holds its UNDERLYING
    //    primitive value, so the boxed value's runtime type is that primitive's
    //    `TypeCode`).
    //  * The C# hard cast `(CSharpOperators.UnaryOperatorMethod)builtinOperatorOR
    //    .BestCandidate` ports to `dynamic_cast<const UnaryOperatorMethod*>` with the
    //    documented safe fallback (an empty/foreign best candidate is impossible through
    //    the builtin-table call site -- every table entry IS a `UnaryOperatorMethod` and
    //    the first `AddCandidate` always folds a best; the fallback returns the
    //    `ErrorResolveResult` over the operand's type, the D516 convention).
    //  * The C# `static readonly ResolveResult ErrorResult = ErrorResolveResult
    //    .UnknownError` returns of the non-overloadable arms port to a NON-OWNING
    //    aliasing `shared_ptr` over the program-lifetime singleton (the empty-owner
    //    aliasing constructor -- no deleter ever runs; the singleton is never destroyed).
    //  * The C# non-dynamic `await` arm (lines 353-389) computes a chain of
    //    `ResolveMemberAccess` / `ResolveInvocation` / `CreateMemberLookup` results that
    //    the arm then DISCARDS -- the C# unconditionally ends in `throw new
    //    NotImplementedException()` (the C# comment: "I believe this is dead code for
    //    ILSpy anyways"). The port documents that pre-throw work as DEFERRED and throws
    //    `std::logic_error` directly at the arm entry (the `NotImplementedException`
    //    convention; observationally identical -- the dead values are discarded and the
    //    C# member lookups have no side effects). The port lands with the
    //    `ResolveMemberAccess` / `ResolveInvocation` regions still unported; wiring the
    //    arm's machinery would change nothing observable.
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveUnaryOperator(
        ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const;

    // ---- ResolveBinaryOperator + enum handlers -----------------------------------------------
    // (The `ResolveBinaryOperator` region, CSharpResolver.cs lines 594-948, C# 4.0 spec
    // section 7.3.4 "Binary operator overload resolution", plus the mutually recursive
    // enum-handler trio from the "Enum helper methods" region at lines 995-1053 -- each
    // handler calls `ResolveBinaryOperator` back on the enum's underlying operands, so
    // the four members land together. Every non-recursive helper the region consumes is
    // already ported: the two `GetOverloadableOperatorName` statics, the user-defined
    // candidate scan + lift, `CreateResolveResultForUserDefinedOperator`,
    // `CreateOverloadResolution`, `UnaryNumericPromotion`, `BinaryNumericPromotion`,
    // `MakeNullable`, `TryConvert` / `TryConvertEnum`, the member `GetEnumUnderlyingType`,
    // `ResolveNullCoalescingOperator`, `IsNullableTypeOrNonValueType`, the `CSharpOperators`
    // operator tables + their `Invoke` constant-evaluation virtuals, the
    // `BinaryOperatorResolveResult` factory, `Convert`/`ResolveCast`, and the
    // `TypeUtils.IsCSharpNativeIntegerType` leaf.)

    // The C# `public ResolveResult ResolveBinaryOperator(BinaryOperatorType op,
    // ResolveResult lhs, ResolveResult rhs)` (line 594) -- the binary-operator
    // resolution: the dynamic arm (either operand dynamic converts BOTH to dynamic and
    // yields the dynamic `OperatorResolveResult`), the overloadable-name gate
    // (`ConditionalAnd`/`ConditionalOr` fall through to their bitwise names; the
    // null-coalescing operator delegates to `ResolveNullCoalescingOperator`; anything
    // else without a name is the `UnknownError` singleton), then the overloadable path:
    // the nullable strip (a nullable operand contributes its UNDERLYING type; the
    // null-literal-vs-value-type pair forces `isNullable`), the user-defined operator
    // overload resolution FIRST (an applicable user-defined operator wins; the scan runs
    // over BOTH operands' types, deduplicated by member identity -- the C# `HashSet`
    // reference-equality `UnionWith`), the shift special case (each operand independently
    // unary-promoted; `null << null` produces `int?`), the binary numeric promotion
    // (with the equality/inequality `allowNullableConstants` gate; a binding error is an
    // `ErrorResolveResult` over the lhs's type), the per-operator built-in table
    // selection with the inline enum/delegate/null-literal arms (`E + U`, `E - E`, `E -
    // U`, `E & E`, enum comparisons, delegate combination/separation, the reference
    // comparison and null-literal comparison special cases), the native-integer arms
    // (equal native integers keep the type, mixing them is an error), and the result
    // composition -- constant-folding both compile-time-constant operands through the
    // operator's `Invoke` (an `ArithmeticException` downgrades to an `ErrorResolveResult`),
    // else wrapping both operands through `Convert` into the predefined
    // `OperatorResolveResult` (marking the lifted forms).
    //
    // PORT CONVENTIONS for this member:
    //  * The C# `IType lhsType` / `rhsType` LOCALS are rebound by the shift arm's
    //    `UnaryNumericPromotion(UnaryOperatorType.Plus, ref lhsType, ...)` and re-read
    //    after the promotion -- the port threads them as local `const IType*` pointers
    //    (a C++ reference cannot rebind; the `UnaryNumericPromotion` `const IType*&`
    //    signature takes the pointer by reference). Every read below dereferences the
    //    local pointer.
    //  * The C# `HashSet<IParameterizedMember> userOperatorCandidates` + `UnionWith`
    //    dedups by the default reference equality -- the port collects into a
    //    `std::vector<const IParameterizedMember*>` filtered by POINTER identity (the
    //    `GetApplicableConversionOperators` dedup convention).
    //  * The C# `lhsType.IsReferenceType == false` null-vs-value-type check is a
    //    DEFINITE-false check (the nullable equality never propagates null -- the
    //    iteration-103 corrected semantics), so the port is `opt.has_value() && !*opt`.
    //  * The C# `conversions.IdentityConversion(lhsType, rhsType)` has no public method
    //    on the port's `CSharpConversions` -- the port calls the `Detail::IdentityConversion`
    //    free function (the iteration-47 convention). The `conversions.ExplicitConversion
    //    (lhsType, rhsType).IsReferenceConversion` calls the public (uncached)
    //    `ExplicitConversion(IType, IType)` method.
    //  * The C# hard cast `(CSharpOperators.BinaryOperatorMethod)builtinOperatorOR
    //    .BestCandidate` ports to `dynamic_cast<const BinaryOperatorMethod*>` with the
    //    documented safe fallback (an empty/foreign best candidate is impossible through
    //    the builtin-table call site -- every table entry IS a `BinaryOperatorMethod` and
    //    the first `AddCandidate` always folds a best; the fallback returns the
    //    `ErrorResolveResult` over the lhs's type, the D516 convention).
    //  * The enum arms' `GetEnumUnderlyingType` member result can be null for a
    //    degenerate enum-definition stub without a configured underlying (the C# NREs on
    //    the subsequent deref); the port's documented safe fallback treats the enum arm
    //    as not firing (the null-member convention).
    //  * The C# `catch (ArithmeticException)` around `m.Invoke(this, lhs.ConstantValue,
    //    rhs.ConstantValue)` ports to `catch (const Util::ArithmeticException&)` (the
    //    `ResolveUnaryOperator` family-catch convention: the operand casts throw
    //    `Util::OverflowException`/`Util::InvalidCastException`, the table bodies throw
    //    the typed family members, and `InvalidCastException` -- deliberately NOT a
    //    family member -- propagates out, faithfully).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ResolveBinaryOperator(
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // The C# private `ResolveResult HandleEnumComparison(BinaryOperatorType op, IType
    // enumType, bool isNullable, ResolveResult lhs, ResolveResult rhs)` (line 995) --
    // "bool operator op(E x, E y)" evaluated as `((U)x op (U)y`: a both-constant,
    // non-nullable, non-enum-underlying pair re-resolves through the underlying type and
    // keeps the folded constant when it stays one; everything else is the predefined
    // `OperatorResolveResult` over `bool` marking `isNullable`. Private in the C#;
    // PUBLIC in the port for direct TDD (the TryConvert widening convention). The
    // operands are owning handles (the C# value-copy references the GC owns).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> HandleEnumComparison(
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
        const ILSpy::Decompiler::TypeSystem::IType& enumType, bool isNullable,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // The C# private `ResolveResult HandleEnumSubtraction(bool isNullable, IType
    // enumType, ResolveResult lhs, ResolveResult rhs)` (line 1013) -- "U operator
    // -(E x, E y)" evaluated as `(U)((U)x - (U)y)`: the both-constant fold re-resolves
    // through the underlying type and re-casts UNCHECKED back into the UNDERLYING (not
    // the enum); everything else is the predefined `OperatorResolveResult` over the
    // (nullable) underlying type. Private in the C#; PUBLIC in the port for direct TDD
    // (the TryConvert widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> HandleEnumSubtraction(
        bool isNullable, const ILSpy::Decompiler::TypeSystem::IType& enumType,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

    // The C# private `ResolveResult HandleEnumOperator(bool isNullable, IType enumType,
    // BinaryOperatorType op, ResolveResult lhs, ResolveResult rhs)` (line 1037) -- "E
    // operator +(E x, U y)" / "E operator +(U x, E y)" / "E operator -(E x, U y)" / the
    // enum bitwise operators, evaluated as `(E)((U)x op (U)y)`: the both-constant fold
    // re-resolves through the underlying type and re-casts UNCHECKED back into the ENUM;
    // everything else is the predefined `OperatorResolveResult` over the (nullable) enum
    // type. Private in the C#; PUBLIC in the port for direct TDD (the TryConvert
    // widening convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> HandleEnumOperator(
        bool isNullable, const ILSpy::Decompiler::TypeSystem::IType& enumType,
        ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const;

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
