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
#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"

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

// ---- The user-defined operator candidate region (CSharpResolver.cs lines 566-583 /
// 1207-1241 / 1278-1322) ------------------------------------------------------------

// The C# `static string GetOverloadableOperatorName(UnaryOperatorType op)` (line 567) --
// the metadata method name of the overloadable unary operator, or `nullptr` (the C#
// `return null`) for the non-overloadable kinds. The pre- and post-increment forms share
// `op_Increment`; the pre- and post-decrement forms share `op_Decrement`.
const char* CSharpResolver::GetOverloadableOperatorName(
    ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op)
{
    using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
    switch (op) {
        case UnaryOperatorType::Not:
            return "op_LogicalNot";
        case UnaryOperatorType::BitNot:
            return "op_OnesComplement";
        case UnaryOperatorType::Minus:
            return "op_UnaryNegation";
        case UnaryOperatorType::Plus:
            return "op_UnaryPlus";
        case UnaryOperatorType::Increment:
        case UnaryOperatorType::PostIncrement:
            return "op_Increment";
        case UnaryOperatorType::Decrement:
        case UnaryOperatorType::PostDecrement:
            return "op_Decrement";
        default:
            return nullptr; // the C# `return null`
    }
}

// The C# `static string GetOverloadableOperatorName(BinaryOperatorType op)` (line 1208)
// -- the metadata method name of the overloadable binary operator, or `nullptr` (the
// C# `return null`) for the non-overloadable kinds (the conditional && / || never have
// metadata operators of their own -- a user-defined `|` implies the conditional form).
const char* CSharpResolver::GetOverloadableOperatorName(
    ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op)
{
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
    switch (op) {
        case BinaryOperatorType::Add:
            return "op_Addition";
        case BinaryOperatorType::Subtract:
            return "op_Subtraction";
        case BinaryOperatorType::Multiply:
            return "op_Multiply";
        case BinaryOperatorType::Divide:
            return "op_Division";
        case BinaryOperatorType::Modulus:
            return "op_Modulus";
        case BinaryOperatorType::BitwiseAnd:
            return "op_BitwiseAnd";
        case BinaryOperatorType::BitwiseOr:
            return "op_BitwiseOr";
        case BinaryOperatorType::ExclusiveOr:
            return "op_ExclusiveOr";
        case BinaryOperatorType::ShiftLeft:
            return "op_LeftShift";
        case BinaryOperatorType::ShiftRight:
            return "op_RightShift";
        case BinaryOperatorType::UnsignedShiftRight:
            return "op_UnsignedRightShift";
        case BinaryOperatorType::Equality:
            return "op_Equality";
        case BinaryOperatorType::InEquality:
            return "op_Inequality";
        case BinaryOperatorType::GreaterThan:
            return "op_GreaterThan";
        case BinaryOperatorType::LessThan:
            return "op_LessThan";
        case BinaryOperatorType::GreaterThanOrEqual:
            return "op_GreaterThanOrEqual";
        case BinaryOperatorType::LessThanOrEqual:
            return "op_LessThanOrEqual";
        default:
            return nullptr; // the C# `return null`
    }
}

// The C# `public IEnumerable<IParameterizedMember> GetUserDefinedOperatorCandidates(
// IType type, string operatorName)` (line 1280).
std::vector<std::shared_ptr<ILSpy::Decompiler::TypeSystem::IMethod>>
CSharpResolver::GetUserDefinedOperatorCandidates(
    const ILSpy::Decompiler::TypeSystem::IType& type, const char* operatorName) const
{
    using ILSpy::Decompiler::TypeSystem::GetTypeCode;
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::TypeCode;

    std::vector<std::shared_ptr<IMethod>> result;
    // The C# `if (operatorName == null) return EmptyList<IMethod>.Instance;`
    if (operatorName == nullptr)
        return result;

    // The C# `TypeCode.Boolean <= c && c <= TypeCode.Decimal` -- the enum-class relational
    // comparison has no C++ counterpart, so the faithful port casts both sides via
    // `static_cast<int>` (the D514 implicitNumericConversionLookup precedent). The
    // .NET framework contains some of C#'s built-in operators as user-defined
    // operators; however, we must not use those as user-defined operators (we would
    // skip numeric promotion).
    TypeCode c = GetTypeCode(type);
    int code = static_cast<int>(c);
    if (code >= static_cast<int>(TypeCode::Boolean) && code <= static_cast<int>(TypeCode::Decimal))
        return result;

    // C# spec (draft-v11): section 12.4.6 Candidate user-defined operators
    std::vector<std::shared_ptr<IMethod>> operators;
    for (const IMethod* m : type.GetMethods(
             [operatorName](const IMethod* m) {
                 return m->IsOperator() && m->Name() == operatorName;
             })) {
        // A non-owning alias (the header comment): the no-op deleter borrows the
        // type-system-owned method; the `const_cast` reconciles the `GetMethods`
        // const-return contract (the D515 convention -- the underlying type-system
        // objects are mutable).
        operators.push_back(std::shared_ptr<IMethod>(
            const_cast<IMethod*>(m), [](IMethod*) {}));
    }
    LiftUserDefinedOperators(operators);
    return operators;
}

// The C# `void LiftUserDefinedOperators(List<IMethod> operators)` (line 1296) -- the
// ORIGINAL count is captured as the loop bound before any appending, so the freshly
// appended lifted forms are not themselves lifted again.
void CSharpResolver::LiftUserDefinedOperators(
    std::vector<std::shared_ptr<ILSpy::Decompiler::TypeSystem::IMethod>>& operators) const
{
    using ILSpy::Decompiler::TypeSystem::IMethod;
    std::size_t nonLiftedMethodCount = operators.size();
    // Construct lifted operators
    for (std::size_t i = 0; i < nonLiftedMethodCount; i++) {
        std::shared_ptr<IMethod> liftedMethod =
            CSharpOperators::LiftUserDefinedOperator(operators[i]);
        if (liftedMethod != nullptr)
            operators.push_back(std::move(liftedMethod));
    }
}

// The C# `ResolveResult CreateResolveResultForUserDefinedOperator(OverloadResolution r,
// ExpressionType operatorType)` (line 1309).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::CreateResolveResultForUserDefinedOperator(
    ILSpy::Decompiler::CSharp::Resolver::OverloadResolution& r,
    ILSpy::Decompiler::TypeSystem::ExpressionType operatorType)
{
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;

    if (r.BestCandidateErrors() != OverloadResolutionErrors::None)
        return r.CreateResolveResult(nullptr);
    // The C# hard cast `(IMethod)r.BestCandidate` would throw `InvalidCastException` for
    // a non-IMethod best candidate (impossible through the operator-resolution call
    // sites, which only feed `IMethod` candidates; `BestCandidate` is non-null whenever
    // `BestCandidateErrors` was `None` because the error-free state implies an applicable
    // candidate was folded into the best state). The safe fallback returns the same
    // invocation error result as the error path above (the D565 documented-safe-fallback
    // convention).
    const IMethod* method = dynamic_cast<const IMethod*>(r.BestCandidate());
    if (method == nullptr)
        return r.CreateResolveResult(nullptr);
    // The owning result-type handle for the `OperatorResolveResult` ctor: the
    // `shared_from_this` + `const_pointer_cast` pair (the D529 convention -- the const is
    // the `ReturnType` accessor's contract, the underlying type-system object is
    // shared-managed; a non-shared-managed return type would throw `bad_weak_ptr`, the
    // documented D578 stub discipline).
    ITypePtr returnType = std::const_pointer_cast<IType>(
        const_cast<IType&>(method->ReturnType()).shared_from_this());
    return std::make_shared<ILSpy::Decompiler::Semantics::OperatorResolveResult>(
        std::move(returnType), operatorType, method,
        /*isLiftedOperator=*/dynamic_cast<const ILiftedOperator*>(method) != nullptr,
        r.GetArgumentsWithConversions());
}

// ---- Convert / ResolveCast region (CSharpResolver.cs lines 1319-1470) ---------------------

// The C# `bool TryConvert(ref ResolveResult rr, IType targetType)` (line 1320).
bool CSharpResolver::TryConvert(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr,
    ILSpy::Decompiler::TypeSystem::IType& targetType) const
{
    using ILSpy::Decompiler::Semantics::Conversion;

    std::shared_ptr<Conversion> c = conversions_.ImplicitConversion(*rr, targetType);
    if (c->IsValid())
    {
        rr = Convert(rr, targetType, std::move(c));
        return true;
    }
    else
    {
        return false;
    }
}

// The C# `bool TryConvertEnum(ref ResolveResult rr, IType targetType, ref bool isNullable,
// ref ResolveResult enumRR, bool allowConversionFromConstantZero = true)` (line 1341).
bool CSharpResolver::TryConvertEnum(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr,
    ILSpy::Decompiler::TypeSystem::IType& targetType,
    bool& isNullable,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& enumRR,
    bool allowConversionFromConstantZero) const
{
    using ILSpy::Decompiler::Semantics::Conversion;
    using ILSpy::Decompiler::Semantics::ConversionResolveResult;
    using ILSpy::Decompiler::Semantics::Conversions;
    using ILSpy::Decompiler::TypeSystem::Create;
    using ILSpy::Decompiler::TypeSystem::IsKnownType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

    std::shared_ptr<Conversion> c;
    if (!isNullable)
    {
        // Try non-nullable
        c = conversions_.ImplicitConversion(*rr, targetType);
        if (c->IsValid() && (allowConversionFromConstantZero || !c->IsEnumerationConversion()))
        {
            rr = Convert(rr, targetType, std::move(c));
            return true;
        }
    }
    // make targetType nullable if it isn't already (a LOCAL rebind -- the C# `targetType`
    // parameter is by-value, so the caller's reference is untouched; a C++ reference
    // cannot rebind, so the rebound target threads through a local pointer).
    ITypePtr nullableTarget;
    IType* currentTarget = &targetType;
    if (!IsKnownType(targetType, KnownTypeCode::NullableOfT))
    {
        nullableTarget = Create(compilation_, targetType);
        currentTarget = nullableTarget.get();
    }

    c = conversions_.ImplicitConversion(*rr, *currentTarget);
    if (c->IsValid() && (allowConversionFromConstantZero || !c->IsEnumerationConversion()))
    {
        rr = Convert(rr, *currentTarget, std::move(c));
        isNullable = true;
        // Also convert the enum-typed RR to nullable, if it isn't already
        if (!IsKnownType(enumRR->Type(), KnownTypeCode::NullableOfT))
        {
            ITypePtr nullableType = Create(compilation_, enumRR->Type());
            enumRR = std::make_shared<ConversionResolveResult>(
                std::move(nullableType), enumRR, Conversions::ImplicitNullableConversion());
        }
        return true;
    }
    return false;
}

// The C# `ResolveResult Convert(ResolveResult rr, IType targetType)` (line 1381).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::Convert(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr,
    ILSpy::Decompiler::TypeSystem::IType& targetType) const
{
    using ILSpy::Decompiler::Semantics::Conversion;

    std::shared_ptr<Conversion> c = conversions_.ImplicitConversion(*rr, targetType);
    return Convert(std::move(rr), targetType, std::move(c));
}

// The C# `ResolveResult Convert(ResolveResult rr, IType targetType, Conversion c)` (line 1386).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::Convert(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rr,
    ILSpy::Decompiler::TypeSystem::IType& targetType,
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> c) const
{
    using ILSpy::Decompiler::Semantics::ConversionResolveResult;
    using ILSpy::Decompiler::Semantics::Conversions;
    using ILSpy::Decompiler::TypeSystem::IType;

    // The C# `c == Conversion.IdentityConversion` -- reference equality with the
    // singleton, ported as pointer identity (the D536 convention).
    if (c.get() == Conversions::IdentityConversion().get())
        return rr;
    else if (rr->IsCompileTimeConstant() && c.get() != Conversions::None().get()
             && !c->IsUserDefined())
        return ResolveCast(targetType, std::move(rr));
    else
        return std::make_shared<ConversionResolveResult>(
            targetType.shared_from_this(), std::move(rr), std::move(c), checkForOverflow_);
}

// The C# `public ResolveResult ResolveCast(IType targetType, ResolveResult expression)`
// (line 1396, C# spec draft-v11 section 12.9.8 Cast expressions).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveCast(
    ILSpy::Decompiler::TypeSystem::IType& targetType,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const
{
    using ILSpy::Decompiler::Semantics::ConstantResolveResult;
    using ILSpy::Decompiler::Semantics::ConversionResolveResult;
    using ILSpy::Decompiler::Semantics::ErrorResolveResult;
    using ILSpy::Decompiler::TypeSystem::GetTypeCode;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::TypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;
    using ILSpy::Decompiler::Util::Cast;
    using ILSpy::Decompiler::Util::InvalidCastException;
    using ILSpy::Decompiler::Util::OverflowException;

    // C# spec (draft-v11): section 12.9.8 Cast expressions
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> c =
        conversions_.ExplicitConversion(*expression, targetType);
    if (expression->IsCompileTimeConstant() && !c->IsUserDefined())
    {
        // The TypeUtils EXTENSION (`targetType.GetEnumUnderlyingType()` -- qualified on the
        // `IType` receiver, so the resolver's same-name member does NOT apply; the fully
        // qualified call is required here because the member name would otherwise hide
        // the namespace-scope function inside the class). A non-enum target passes through
        // as itself (the folding reads the target's own TypeCode); an enum target reports
        // its underlying. The port's nullptr is the documented safe fallback for the C#
        // NRE shapes (a definitionless enum) -- every folding arm below falls through to
        // the final wrap for the null shape.
        const IType* underlyingType =
            ILSpy::Decompiler::TypeSystem::GetEnumUnderlyingType(&targetType);
        TypeCode code = underlyingType != nullptr
            ? GetTypeCode(*underlyingType)
            : TypeCode::Empty;
        // The C# enum relational comparisons port through static_cast<int> (the D514
        // convention -- the closed [Boolean..Decimal] range of the primitive targets).
        if (static_cast<int>(code) >= static_cast<int>(TypeCode::Boolean)
            && static_cast<int>(code) <= static_cast<int>(TypeCode::Decimal)
            && expression->ConstantValue().has_value())
        {
            // The C# `expression.ConstantValue is string` -- the port's boxed string
            // constant is std::string (the Util::Cast held-type convention).
            if (expression->ConstantValue().type() == typeid(std::string))
            {
                return std::make_shared<ErrorResolveResult>(targetType.shared_from_this());
            }
            try
            {
                return std::make_shared<ConstantResolveResult>(
                    targetType.shared_from_this(),
                    CSharpPrimitiveCast(code, expression->ConstantValue()));
            }
            catch (const OverflowException&)
            {
                return std::make_shared<ErrorResolveResult>(targetType.shared_from_this());
            }
            catch (const InvalidCastException&)
            {
                return std::make_shared<ErrorResolveResult>(targetType.shared_from_this());
            }
        }
        else if (code == TypeCode::String)
        {
            std::any constantValue = expression->ConstantValue();
            // The C# `expression.ConstantValue == null || expression.ConstantValue is
            // string` -- the empty `std::any` is the C# null literal.
            if (!constantValue.has_value() || constantValue.type() == typeid(std::string))
                return std::make_shared<ConstantResolveResult>(
                    targetType.shared_from_this(), std::move(constantValue));
            else
                return std::make_shared<ErrorResolveResult>(targetType.shared_from_this());
        }
        else if (underlyingType != nullptr
                 && (underlyingType->Kind() == TypeKind::NInt
                     || underlyingType->Kind() == TypeKind::NUInt)
                 && expression->ConstantValue().has_value())
        {
            if (expression->ConstantValue().type() == typeid(std::string))
            {
                return std::make_shared<ErrorResolveResult>(targetType.shared_from_this());
            }
            code = (underlyingType->Kind() == TypeKind::NInt ? TypeCode::Int32
                                                              : TypeCode::UInt32);
            try
            {
                // The C# hardcodes `checkForOverflow: true` here (NOT
                // this.CheckForOverflow) -- the native-integer constant always probes
                // the 32-bit range.
                return std::make_shared<ConstantResolveResult>(
                    targetType.shared_from_this(),
                    Cast(code, expression->ConstantValue(), /*checkForOverflow=*/ true));
            }
            catch (const OverflowException&)
            {
                // If constant value doesn't fit into 32-bits, the conversion is not a
                // compile-time constant
                return std::make_shared<ConversionResolveResult>(
                    targetType.shared_from_this(), expression, std::move(c), checkForOverflow_);
            }
            catch (const InvalidCastException&)
            {
                return std::make_shared<ErrorResolveResult>(targetType.shared_from_this());
            }
        }
    }
    return std::make_shared<ConversionResolveResult>(
        targetType.shared_from_this(), std::move(expression), std::move(c), checkForOverflow_);
}

// The C# `internal object CSharpPrimitiveCast(TypeCode targetType, object input)`
// (line 1467).
std::any CSharpResolver::CSharpPrimitiveCast(
    ILSpy::Decompiler::TypeSystem::TypeCode targetType, const std::any& input) const
{
    using ILSpy::Decompiler::Util::Cast;

    return Cast(targetType, input, checkForOverflow_);
}

// The C# private `IType GetEnumUnderlyingType(IType enumType)` (line 985, the "Enum
// helper methods" region).
const ILSpy::Decompiler::TypeSystem::IType*
CSharpResolver::GetEnumUnderlyingType(
    const ILSpy::Decompiler::TypeSystem::IType& enumType) const
{
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::UnknownType;

    const ITypeDefinition* def = enumType.GetDefinition();
    if (def != nullptr)
        return def->EnumUnderlyingType().get();
    // The C# `SpecialType.UnknownType` singleton: the minimal port's `UnknownType()`
    // factory allocates a fresh instance per call, so the singleton is materialized ONCE
    // as a program-lifetime static handle (the non-owning return stays valid for the
    // caller).
    static const ITypePtr unknownType = UnknownType();
    return unknownType.get();
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
