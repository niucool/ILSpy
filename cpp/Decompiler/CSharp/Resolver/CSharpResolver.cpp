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

// Implementation of the CSharpResolver class skeleton (CSharpResolver.cs lines 49-322;
// see CSharpResolver.hpp for the port conventions). The `Resolve*` arms land in later
// slices; this file wires the two public ctors, the immutable `With*` clone factories,
// the per-current-type-definition cache, the local-variable stack, and the
// object-initializer context.

#include "Decompiler/CSharp/Resolver/CSharpResolver.hpp"

#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"

#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Resolver {

namespace {

// The C# `context.Compilation` read the public context-ctor's member-initialization
// needs -- with the C# null-context `ArgumentNullException` as a `std::invalid_argument`
// thrown BEFORE any member binds (the guard must fire inside the member-init list, so
// it lives in a helper; the header convention (f)).
const ILSpy::Decompiler::TypeSystem::ICompilation& CompilationOf(
    const std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>& context)
{
    if (!context)
        throw std::invalid_argument("context must not be null");
    return context->Compilation();
}

// The downcast of the context `With*` results back to the concrete final type the
// resolver stores (`CSharpTypeResolveContext` is `final` with single inheritance from
// `ITypeResolveContext`, so the `static_cast` is safe -- the iteration-94 "future
// consumer recovers the concrete type by static_cast" note, applied here).
std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>
AsConcreteContext(
    std::unique_ptr<ILSpy::Decompiler::TypeSystem::ITypeResolveContext> context)
{
    return std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
        static_cast<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext*>(
            context.release()));
}

} // namespace

// The C# `public CSharpResolver(ICompilation compilation)` -- the C# null guard is
// structurally unreachable through the reference parameter (the D374 convention). The
// context starts fresh over the compilation's main module.
CSharpResolver::CSharpResolver(const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
    : compilation_(compilation),
      conversions_(CSharpConversions::Get(compilation)),
      context_(std::make_shared<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext>(
          compilation.MainModule())),
      checkForOverflow_(false),
      isWithinLambdaExpression_(false),
      currentTypeDefinitionCache_(nullptr),
      localVariableStack_(),
      objectInitializerStack_(nullptr)
{
}

// The C# `public CSharpResolver(CSharpTypeResolveContext context)` -- stores the given
// context (shared ownership; the C# reference) and builds the per-type-definition cache
// when the context carries a current type definition. The member-init order matches the
// declaration order (`compilation_` binds first, so `conversions_` reads it back through
// `Get`); the null-context guard fires inside `CompilationOf` before any member binds.
CSharpResolver::CSharpResolver(
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context)
    : compilation_(CompilationOf(context)),
      conversions_(CSharpConversions::Get(compilation_)),
      context_(context),
      checkForOverflow_(false),
      isWithinLambdaExpression_(false),
      currentTypeDefinitionCache_(
          context && context->CurrentTypeDefinition()
              ? std::shared_ptr<TypeDefinitionCache>(
                    new TypeDefinitionCache(*context->CurrentTypeDefinition()))
              : nullptr),
      localVariableStack_(),
      objectInitializerStack_(nullptr)
{
}

// The C# private full ctor -- the single construction path the clone factories thread
// every field through.
CSharpResolver::CSharpResolver(
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
    CSharpConversions& conversions,
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> context,
    bool checkForOverflow,
    bool isWithinLambdaExpression,
    std::shared_ptr<TypeDefinitionCache> currentTypeDefinitionCache,
    Util::ImmutableStack<std::shared_ptr<const VariableMap>> localVariableStack,
    std::shared_ptr<ObjectInitializerContext> objectInitializerStack)
    : compilation_(compilation),
      conversions_(conversions),
      context_(std::move(context)),
      checkForOverflow_(checkForOverflow),
      isWithinLambdaExpression_(isWithinLambdaExpression),
      currentTypeDefinitionCache_(std::move(currentTypeDefinitionCache)),
      localVariableStack_(std::move(localVariableStack)),
      objectInitializerStack_(std::move(objectInitializerStack))
{
}

// The `enable_shared_from_this` bridge for the two C# `return this` early-outs (the
// header convention (a)): the const `shared_from_this()` returns
// `shared_ptr<const CSharpResolver>`, so the `const_pointer_cast` recovers the mutable
// handle (the underlying resolver is the shared-managed original -- the D529 convention).
std::shared_ptr<CSharpResolver> CSharpResolver::Self() const
{
    return std::const_pointer_cast<CSharpResolver>(shared_from_this());
}

// The C# private `WithContext` -- the shared clone helper for the factories that replace
// a context slot. `make_shared` cannot reach the private ctor (the
// `UsingScope::WithNestedNamespace` precedent), so the allocation is `new`.
std::shared_ptr<CSharpResolver> CSharpResolver::WithContext(
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::CSharpTypeResolveContext> newContext) const
{
    return std::shared_ptr<CSharpResolver>(new CSharpResolver(
        compilation_, conversions_, std::move(newContext), checkForOverflow_,
        isWithinLambdaExpression_, currentTypeDefinitionCache_, localVariableStack_,
        objectInitializerStack_));
}

// The C# `public CSharpResolver WithCheckForOverflow(bool)` -- the identity-preserving
// early-out when the flag is unchanged (`return this`).
std::shared_ptr<CSharpResolver> CSharpResolver::WithCheckForOverflow(bool checkForOverflow) const
{
    if (checkForOverflow == checkForOverflow_)
        return Self();
    return std::shared_ptr<CSharpResolver>(new CSharpResolver(
        compilation_, conversions_, context_, checkForOverflow, isWithinLambdaExpression_,
        currentTypeDefinitionCache_, localVariableStack_, objectInitializerStack_));
}

// The C# `public CSharpResolver WithIsWithinLambdaExpression(bool)` -- no
// identity-preserving early-out in the C#: a fresh clone even for the same flag.
std::shared_ptr<CSharpResolver> CSharpResolver::WithIsWithinLambdaExpression(
    bool isWithinLambdaExpression) const
{
    return std::shared_ptr<CSharpResolver>(new CSharpResolver(
        compilation_, conversions_, context_, checkForOverflow_, isWithinLambdaExpression,
        currentTypeDefinitionCache_, localVariableStack_, objectInitializerStack_));
}

// The C# `public CSharpResolver WithCurrentMember(IMember member)` -- delegates to the
// context's member-slot factory (downcast back to the concrete final type) and clones.
std::shared_ptr<CSharpResolver> CSharpResolver::WithCurrentMember(
    const ILSpy::Decompiler::TypeSystem::IMember* member) const
{
    return WithContext(AsConcreteContext(context_->WithCurrentMember(member)));
}

// The C# `public CSharpResolver WithCurrentUsingScope(UsingScope usingScope)` -- the
// context's using-scope factory already returns the concrete type.
std::shared_ptr<CSharpResolver> CSharpResolver::WithCurrentUsingScope(
    std::shared_ptr<ILSpy::Decompiler::CSharp::TypeSystem::UsingScope> usingScope) const
{
    return WithContext(context_->WithUsingScope(std::move(usingScope)));
}

// The C# `public CSharpResolver WithCurrentTypeDefinition(ITypeDefinition
// typeDefinition)` -- the identity-preserving early-out when the definition is unchanged
// (`return this`); otherwise a fresh cache over the new definition (or a null cache when
// clearing) and the context's type-definition-slot clone.
std::shared_ptr<CSharpResolver> CSharpResolver::WithCurrentTypeDefinition(
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDefinition) const
{
    if (CurrentTypeDefinition() == typeDefinition)
        return Self();
    std::shared_ptr<TypeDefinitionCache> newTypeDefinitionCache =
        typeDefinition != nullptr
            ? std::shared_ptr<TypeDefinitionCache>(new TypeDefinitionCache(*typeDefinition))
            : nullptr;
    return std::shared_ptr<CSharpResolver>(new CSharpResolver(
        compilation_, conversions_,
        AsConcreteContext(context_->WithCurrentTypeDefinition(typeDefinition)),
        checkForOverflow_, isWithinLambdaExpression_, std::move(newTypeDefinitionCache),
        localVariableStack_, objectInitializerStack_));
}

// The C# private `WithLocalVariableStack` -- the `AddVariables` clone helper.
std::shared_ptr<CSharpResolver> CSharpResolver::WithLocalVariableStack(
    Util::ImmutableStack<std::shared_ptr<const VariableMap>> stack) const
{
    return std::shared_ptr<CSharpResolver>(new CSharpResolver(
        compilation_, conversions_, context_, checkForOverflow_, isWithinLambdaExpression_,
        currentTypeDefinitionCache_, std::move(stack), objectInitializerStack_));
}

// The C# `public CSharpResolver AddVariables(Dictionary<string, IVariable> variables)` --
// pushes the caller's dictionary onto the immutable stack (shared with the clone). The
// C# null-dictionary `ArgumentNullException` ports to `std::invalid_argument` (the
// shared_ptr parameter is nullable in the port, so the guard is load-bearing: a null
// map would otherwise poison the stack with a null node).
std::shared_ptr<CSharpResolver> CSharpResolver::AddVariables(
    std::shared_ptr<const VariableMap> variables) const
{
    if (!variables)
        throw std::invalid_argument("variables must not be null");
    return WithLocalVariableStack(localVariableStack_.push(std::move(variables)));
}

// The C# `public IEnumerable<IVariable> LocalVariables` -- flattens the stack
// TOP-FIRST (`ImmutableStack` enumerates LIFO, so the innermost block comes first --
// the C# `localVariableStack.SelectMany(s => s.Values)`).
std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IVariable>>
CSharpResolver::LocalVariables() const
{
    std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IVariable>> result;
    for (auto stack = localVariableStack_; !stack.empty(); stack = stack.pop()) {
        for (const auto& entry : *stack.top())
            result.push_back(entry.second);
    }
    return result;
}

// The C# private `WithObjectInitializerStack` -- the push/pop clone helper.
std::shared_ptr<CSharpResolver> CSharpResolver::WithObjectInitializerStack(
    std::shared_ptr<ObjectInitializerContext> stack) const
{
    return std::shared_ptr<CSharpResolver>(new CSharpResolver(
        compilation_, conversions_, context_, checkForOverflow_, isWithinLambdaExpression_,
        currentTypeDefinitionCache_, localVariableStack_, std::move(stack)));
}

// The C# `public CSharpResolver PushObjectInitializer(ResolveResult initializedObject)`.
std::shared_ptr<CSharpResolver> CSharpResolver::PushObjectInitializer(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> initializedObject) const
{
    if (!initializedObject)
        throw std::invalid_argument("initializedObject must not be null");
    return WithObjectInitializerStack(std::shared_ptr<ObjectInitializerContext>(
        new ObjectInitializerContext(std::move(initializedObject), objectInitializerStack_)));
}

// The C# `public CSharpResolver PopObjectInitializer()` -- the C# empty-stack
// `InvalidOperationException` ports to `std::runtime_error` (the header convention (f)).
std::shared_ptr<CSharpResolver> CSharpResolver::PopObjectInitializer() const
{
    if (objectInitializerStack_ == nullptr)
        throw std::runtime_error("object initializer stack is empty");
    return WithObjectInitializerStack(objectInitializerStack_->prev);
}

// The C# `public ResolveResult CurrentObjectInitializer { get; }` -- the innermost
// initializer, or the C# `ErrorResult` static (`ErrorResolveResult.UnknownError`) when
// no object initializer is open. An if/else (NOT a ternary): the two arms have
// different types (`ResolveResult&` vs `const ErrorResolveResult&`), so a ternary
// would convert both operands to a common PRVALUE -- copying to a temporary the
// returned reference would dangle on (the LookupMethod ReturnType ternary-slicing
// trap; each return statement binds the reference directly to the actual object).
const ILSpy::Decompiler::Semantics::ResolveResult& CSharpResolver::CurrentObjectInitializer() const
{
    if (objectInitializerStack_ != nullptr)
        return *objectInitializerStack_->initializedObject;
    return ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError();
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
