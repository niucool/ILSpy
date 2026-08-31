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
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
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

// ---- Numeric promotion region (CSharpResolver.cs lines 536-561 + 1055-1230) ----------------

// The C# private `IType MakeNullable(IType type, bool isNullable)` (line 1055).
ILSpy::Decompiler::TypeSystem::ITypePtr
CSharpResolver::MakeNullable(
    const ILSpy::Decompiler::TypeSystem::IType& type, bool isNullable) const
{
    using ILSpy::Decompiler::TypeSystem::Create;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;

    if (isNullable)
        return Create(compilation_, type);
    else
        // The passthrough arm recovers the input's own owning handle: the const
        // `shared_from_this` returns `shared_ptr<const IType>`, so the `const_pointer_cast`
        // yields the non-const handle (the D529 convention -- the const is the accessor's
        // contract, the underlying type-system object is shared-managed and mutable; a
        // non-shared-managed input would throw `bad_weak_ptr`, the documented stub
        // discipline).
        return std::const_pointer_cast<IType>(type.shared_from_this());
}

// The C# private `ResolveResult UnaryNumericPromotion(UnaryOperatorType op, ref IType
// type, bool isNullable, ResolveResult expression)` (line 536, C# spec draft-v11
// section 12.4.7.2).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::UnaryNumericPromotion(
    ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
    const ILSpy::Decompiler::TypeSystem::IType*& type,
    bool isNullable,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const
{
    using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
    using ILSpy::Decompiler::Semantics::Conversions;
    using ILSpy::Decompiler::TypeSystem::GetTypeCode;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // C# spec (draft-v11): section 12.4.7.2 Unary numeric promotions
    TypeCode code = GetTypeCode(*type);
    if (isNullable && type->Kind() == TypeKind::Null)
        code = TypeCode::SByte; // cause promotion of null to int32
    switch (op)
    {
        case UnaryOperatorType::Minus:
            if (code == TypeCode::UInt32)
            {
                type = &compilation_.FindType(KnownTypeCode::Int64);
                return Convert(std::move(expression), *MakeNullable(*type, isNullable),
                               isNullable ? Conversions::ImplicitNullableConversion()
                                          : Conversions::ImplicitNumericConversion());
            }
            // The C# `goto case UnaryOperatorType.Plus;` -- a non-uint minus falls through
            // to the shared small-unsigned promotion check.
            [[fallthrough]];
        case UnaryOperatorType::Plus:
        case UnaryOperatorType::BitNot:
            // The C# enum relational comparisons port through static_cast<int> (the D514
            // convention -- the closed [Char..UInt16] range of the small unsigned types).
            if (static_cast<int>(code) >= static_cast<int>(TypeCode::Char)
                && static_cast<int>(code) <= static_cast<int>(TypeCode::UInt16))
            {
                type = &compilation_.FindType(KnownTypeCode::Int32);
                return Convert(std::move(expression), *MakeNullable(*type, isNullable),
                               isNullable ? Conversions::ImplicitNullableConversion()
                                          : Conversions::ImplicitNumericConversion());
            }
            break;
        default:
            break;
    }
    return expression;
}

// The C# private `bool IsSigned(TypeCode code, ResolveResult rr)` (line 1190).
bool CSharpResolver::IsSigned(
    ILSpy::Decompiler::TypeSystem::TypeCode code,
    const std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rr)
{
    using ILSpy::Decompiler::TypeSystem::TypeCode;

    // Determine whether the rr with code==ReflectionHelper.GetTypeCode(NullableType.GetUnderlyingType(rr.Type))
    // is a signed primitive type.
    switch (code)
    {
        case TypeCode::SByte:
        case TypeCode::Int16:
            return true;
        case TypeCode::Int32:
            // for int, consider implicit constant expression conversion
            if (rr->IsCompileTimeConstant())
            {
                // The C# `(int)rr.ConstantValue` unbox -- the pointer-form any_cast
                // (nullptr on a held-type mismatch or an empty box, the safe faithful
                // fallback for a shape the C# would throw InvalidCastException on: the
                // mismatched or null box counts as signed).
                const std::int32_t* v = std::any_cast<std::int32_t>(&rr->ConstantValue());
                if (v != nullptr && *v >= 0)
                    return false;
            }
            return true;
        case TypeCode::Int64:
            // for long, consider implicit constant expression conversion
            if (rr->IsCompileTimeConstant())
            {
                const std::int64_t* v = std::any_cast<std::int64_t>(&rr->ConstantValue());
                if (v != nullptr && *v >= 0)
                    return false;
            }
            return true;
        default:
            return false;
    }
}

// The C# private `ResolveResult CastTo(TypeCode targetType, bool isNullable, ResolveResult
// expression, bool allowNullableConstants)` (line 1214).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::CastTo(
    ILSpy::Decompiler::TypeSystem::TypeCode targetType,
    bool isNullable,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
    bool allowNullableConstants) const
{
    using ILSpy::Decompiler::TypeSystem::FindType;
    using ILSpy::Decompiler::TypeSystem::IType;

    // The C# `compilation.FindType(targetType)` resolves to the ReflectionHelper
    // EXTENSION (`ICompilation.FindType` takes a `KnownTypeCode`, so the `TypeCode`
    // receiver binds to the extension, iteration 87). The const_cast feeds the const
    // FindType result to the non-const IType& overload below (the D515/D517 convention --
    // the underlying type-system object is mutable).
    return CastTo(const_cast<IType&>(FindType(compilation_, targetType)), isNullable,
                  std::move(expression), allowNullableConstants);
}

// The C# private `ResolveResult CastTo(IType targetType, bool isNullable, ResolveResult
// expression, bool allowNullableConstants)` (line 1219).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::CastTo(
    ILSpy::Decompiler::TypeSystem::IType& targetType,
    bool isNullable,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
    bool allowNullableConstants) const
{
    using ILSpy::Decompiler::Semantics::ConstantResolveResult;
    using ILSpy::Decompiler::Semantics::Conversions;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;

    ITypePtr nullableType = MakeNullable(targetType, isNullable);
    // The C# `nullableType.Equals(expression.Type)` -- the IType structural comparison.
    if (nullableType->Equals(expression->Type()))
        return expression;
    if (allowNullableConstants && expression->IsCompileTimeConstant())
    {
        // The C# `expression.ConstantValue == null` -- the empty `std::any` is the C#
        // null literal (the ConstantResolveResult ctor convention).
        if (!expression->ConstantValue().has_value())
            return std::make_shared<ConstantResolveResult>(std::move(nullableType),
                                                           std::any());
        std::shared_ptr<ResolveResult> rr = ResolveCast(targetType, expression);
        if (rr->IsError())
            return rr;
        if (rr->IsCompileTimeConstant())
            return std::make_shared<ConstantResolveResult>(std::move(nullableType),
                                                           rr->ConstantValue());
    }
    return Convert(std::move(expression), *nullableType,
                   isNullable ? Conversions::ImplicitNullableConversion()
                              : Conversions::ImplicitNumericConversion());
}

// The C# private `bool BinaryNumericPromotion(bool isNullable, ref ResolveResult lhs, ref
// ResolveResult rhs, bool allowNullableConstants)` (line 1065, C# spec draft-v11 section
// 12.4.7.3).
bool CSharpResolver::BinaryNumericPromotion(
    bool isNullable,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& lhs,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& rhs,
    bool allowNullableConstants) const
{
    using ILSpy::Decompiler::TypeSystem::GetTypeCode;
    using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::SpecialType;
    using ILSpy::Decompiler::TypeSystem::TypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // C# spec (draft-v11): section 12.4.7.3 Binary numeric promotions
    const IType& lhsUType = GetUnderlyingType(lhs->Type());
    const IType& rhsUType = GetUnderlyingType(rhs->Type());
    TypeCode lhsCode = GetTypeCode(lhsUType);
    TypeCode rhsCode = GetTypeCode(rhsUType);
    // Treat C# 9 native integers as falling between int and long.
    // However they don't have a TypeCode, so we hack around that here:
    if (lhsUType.Kind() == TypeKind::NInt)
    {
        lhsCode = TypeCode::Int32;
    }
    else if (lhsUType.Kind() == TypeKind::NUInt)
    {
        lhsCode = TypeCode::UInt32;
    }
    if (rhsUType.Kind() == TypeKind::NInt)
    {
        rhsCode = TypeCode::Int32;
    }
    else if (rhsUType.Kind() == TypeKind::NUInt)
    {
        rhsCode = TypeCode::UInt32;
    }
    // if one of the inputs is the null literal, promote that to the type of the other
    // operand (the C# enum relational comparisons port through static_cast<int>, the D514
    // convention -- the closed [Boolean..Decimal] / [Char..Decimal] ranges).
    if (isNullable && lhs->Type().Kind() == TypeKind::Null
        && static_cast<int>(rhsCode) >= static_cast<int>(TypeCode::Boolean)
        && static_cast<int>(rhsCode) <= static_cast<int>(TypeCode::Decimal))
    {
        lhs = CastTo(rhsCode, isNullable, std::move(lhs), allowNullableConstants);
        lhsCode = rhsCode;
    }
    else if (isNullable && rhs->Type().Kind() == TypeKind::Null
             && static_cast<int>(lhsCode) >= static_cast<int>(TypeCode::Boolean)
             && static_cast<int>(lhsCode) <= static_cast<int>(TypeCode::Decimal))
    {
        rhs = CastTo(lhsCode, isNullable, std::move(rhs), allowNullableConstants);
        rhsCode = lhsCode;
    }
    bool bindingError = false;
    if (static_cast<int>(lhsCode) >= static_cast<int>(TypeCode::Char)
        && static_cast<int>(lhsCode) <= static_cast<int>(TypeCode::Decimal)
        && static_cast<int>(rhsCode) >= static_cast<int>(TypeCode::Char)
        && static_cast<int>(rhsCode) <= static_cast<int>(TypeCode::Decimal))
    {
        TypeCode targetType;
        if (lhsCode == TypeCode::Decimal || rhsCode == TypeCode::Decimal)
        {
            targetType = TypeCode::Decimal;
            bindingError = (lhsCode == TypeCode::Single || lhsCode == TypeCode::Double
                            || rhsCode == TypeCode::Single || rhsCode == TypeCode::Double);
        }
        else if (lhsCode == TypeCode::Double || rhsCode == TypeCode::Double)
        {
            targetType = TypeCode::Double;
        }
        else if (lhsCode == TypeCode::Single || rhsCode == TypeCode::Single)
        {
            targetType = TypeCode::Single;
        }
        else if (lhsCode == TypeCode::UInt64 || rhsCode == TypeCode::UInt64)
        {
            targetType = TypeCode::UInt64;
            bindingError = IsSigned(lhsCode, lhs) || IsSigned(rhsCode, rhs);
        }
        else if (lhsUType.Kind() == TypeKind::NUInt || rhsUType.Kind() == TypeKind::NUInt)
        {
            bindingError = IsSigned(lhsCode, lhs) || IsSigned(rhsCode, rhs);
            // The C# `SpecialType.NUInt` singleton (Kind=NUInt, isReferenceType:false);
            // the port constructs the equivalent shape per call (SpecialType equality is
            // kind-based, so the CastTo early-out still fires on a repeat cast).
            ITypePtr nuintType = std::make_shared<SpecialType>(TypeKind::NUInt, false);
            lhs = CastTo(*nuintType, isNullable, std::move(lhs), allowNullableConstants);
            rhs = CastTo(*nuintType, isNullable, std::move(rhs), allowNullableConstants);
            return !bindingError;
        }
        else if (lhsCode == TypeCode::UInt32 || rhsCode == TypeCode::UInt32)
        {
            targetType = (IsSigned(lhsCode, lhs) || IsSigned(rhsCode, rhs))
                ? TypeCode::Int64
                : TypeCode::UInt32;
        }
        else if (lhsCode == TypeCode::Int64 || rhsCode == TypeCode::Int64)
        {
            targetType = TypeCode::Int64;
        }
        else if (lhsUType.Kind() == TypeKind::NInt || rhsUType.Kind() == TypeKind::NInt)
        {
            // The C# `SpecialType.NInt` singleton (Kind=NInt, isReferenceType:false).
            ITypePtr nintType = std::make_shared<SpecialType>(TypeKind::NInt, false);
            lhs = CastTo(*nintType, isNullable, std::move(lhs), allowNullableConstants);
            rhs = CastTo(*nintType, isNullable, std::move(rhs), allowNullableConstants);
            return !bindingError;
        }
        else
        {
            targetType = TypeCode::Int32;
        }
        lhs = CastTo(targetType, isNullable, std::move(lhs), allowNullableConstants);
        rhs = CastTo(targetType, isNullable, std::move(rhs), allowNullableConstants);
    }
    return !bindingError;
}

// ---- Operator-resolution helpers (CSharpResolver.cs lines 525 / 962-985 / 981-989 /
// 1253-1275 / 2435-2441) ---------------------------------------------------------------

// The C# private `bool IsNullableTypeOrNonValueType(IType type)` (line 981).
bool CSharpResolver::IsNullableTypeOrNonValueType(
    const ILSpy::Decompiler::TypeSystem::IType& type)
{
    using ILSpy::Decompiler::TypeSystem::IsNullable;

    // The C# `NullableType.IsNullable(type) || type.IsReferenceType != false` -- the
    // lifted `bool? != false` is falsy ONLY for a definite false, so an INDETERMINATE
    // reference-ness (std::nullopt) still counts as a non-value type here (a null
    // literal compares against unknowns too).
    return IsNullable(type) || !(type.IsReferenceType().has_value()
                                  && !*type.IsReferenceType());
}

// The C# private `OperatorResolveResult UnaryOperatorResolveResult(IType resultType,
// UnaryOperatorType op, ResolveResult expression, bool isLifted = false)` (line 525).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::UnaryOperatorResolveResult(
    const ILSpy::Decompiler::TypeSystem::IType& resultType,
    ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression,
    bool isLifted) const
{
    using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorExpression;
    using ILSpy::Decompiler::Semantics::OperatorResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;

    // The C# `new OperatorResolveResult(resultType, GetLinqNodeType(op,
    // this.CheckForOverflow), null, isLifted, new[] { expression })` -- the predefined-
    // operator ctor shape (no user-defined method; the single operand).
    return std::make_shared<OperatorResolveResult>(
        std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
            resultType.shared_from_this()),
        UnaryOperatorExpression::GetLinqNodeType(op, checkForOverflow_),
        /*userDefinedOperatorMethod=*/ nullptr,
        isLifted,
        std::vector<std::shared_ptr<ResolveResult>>{std::move(expression)});
}

// The C# private `ResolveResult BinaryOperatorResolveResult(IType resultType,
// ResolveResult lhs, BinaryOperatorType op, ResolveResult rhs, bool isLifted = false)`
// (line 983).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::BinaryOperatorResolveResult(
    const ILSpy::Decompiler::TypeSystem::IType& resultType,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
    ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs,
    bool isLifted) const
{
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorExpression;
    using ILSpy::Decompiler::Semantics::OperatorResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;

    return std::make_shared<OperatorResolveResult>(
        std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
            resultType.shared_from_this()),
        BinaryOperatorExpression::GetLinqNodeType(op, checkForOverflow_),
        /*userDefinedOperatorMethod=*/ nullptr,
        isLifted,
        std::vector<std::shared_ptr<ResolveResult>>{std::move(lhs), std::move(rhs)});
}

// The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
// resultType, IType inputType1, KnownTypeCode inputType2)` (line 962).
std::shared_ptr<BinaryOperatorMethod> CSharpResolver::PointerArithmeticOperator(
    const ILSpy::Decompiler::TypeSystem::IType& resultType,
    const ILSpy::Decompiler::TypeSystem::IType& inputType1,
    ILSpy::Decompiler::TypeSystem::KnownTypeCode inputType2) const
{
    return PointerArithmeticOperator(resultType, inputType1,
                                     compilation_.FindType(inputType2));
}

// The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
// resultType, KnownTypeCode inputType1, IType inputType2)` (line 967).
std::shared_ptr<BinaryOperatorMethod> CSharpResolver::PointerArithmeticOperator(
    const ILSpy::Decompiler::TypeSystem::IType& resultType,
    ILSpy::Decompiler::TypeSystem::KnownTypeCode inputType1,
    const ILSpy::Decompiler::TypeSystem::IType& inputType2) const
{
    return PointerArithmeticOperator(resultType, compilation_.FindType(inputType1),
                                     inputType2);
}

// The C# private `CSharpOperators.BinaryOperatorMethod PointerArithmeticOperator(IType
// resultType, IType inputType1, IType inputType2)` (line 972).
std::shared_ptr<BinaryOperatorMethod> CSharpResolver::PointerArithmeticOperator(
    const ILSpy::Decompiler::TypeSystem::IType& resultType,
    const ILSpy::Decompiler::TypeSystem::IType& inputType1,
    const ILSpy::Decompiler::TypeSystem::IType& inputType2) const
{
    using ILSpy::Decompiler::TypeSystem::IParameter;
    using ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;

    // The C# object initializer -- `new BinaryOperatorMethod(compilation) {
    // ReturnType = resultType, parameters = { new DefaultParameter(inputType1, ""),
    // new DefaultParameter(inputType2, "") } }`. The owning parameter type handles are
    // recovered from the const references via shared_from_this + const_pointer_cast (the
    // D529 convention -- the type-system objects are shared-managed).
    auto method = std::make_shared<BinaryOperatorMethod>(compilation_);
    method->SetReturnType(resultType);
    method->AddParameter(std::make_shared<DefaultParameter>(
        std::const_pointer_cast<IType>(inputType1.shared_from_this()), std::string()));
    method->AddParameter(std::make_shared<DefaultParameter>(
        std::const_pointer_cast<IType>(inputType2.shared_from_this()), std::string()));
    return method;
}

// The C# private `ResolveResult ResolveNullCoalescingOperator(ResolveResult lhs,
// ResolveResult rhs)` (line 1253).
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveNullCoalescingOperator(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const
{
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
    using ILSpy::Decompiler::Semantics::ErrorResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::IsNullable;

    if (IsNullable(lhs->Type()))
    {
        // The C# rebinds its local `rhs` copy through `TryConvert(ref rhs, a0)` -- the
        // rebind stays local (the parameter is a value copy of the caller's reference).
        // The const `GetUnderlyingType`/`Type()` accessors return `const IType&` while
        // `TryConvert` takes non-const `IType&`, so the port const_casts (the D515/D517
        // convention: the underlying type-system objects are mutable).
        const IType& a0 = GetUnderlyingType(lhs->Type());
        if (TryConvert(rhs, const_cast<IType&>(a0)))
        {
            return BinaryOperatorResolveResult(a0, std::move(lhs),
                                                BinaryOperatorType::NullCoalescing,
                                                std::move(rhs));
        }
    }
    // The result-type reference is bound BEFORE the `std::move` of the owning handle:
    // the argument evaluation order is unspecified, so reading `lhs->Type()` inside the
    // call's argument list alongside `std::move(lhs)` could deref an already-moved-out
    // handle. The binding stays valid -- the move transfers ownership to the callee's
    // parameter, the pointee object is not destroyed.
    if (TryConvert(rhs, const_cast<IType&>(lhs->Type())))
    {
        const IType& resultType = lhs->Type();
        return BinaryOperatorResolveResult(resultType, std::move(lhs),
                                            BinaryOperatorType::NullCoalescing,
                                            std::move(rhs));
    }
    if (TryConvert(lhs, const_cast<IType&>(rhs->Type())))
    {
        const IType& resultType = rhs->Type();
        return BinaryOperatorResolveResult(resultType, std::move(lhs),
                                            BinaryOperatorType::NullCoalescing,
                                            std::move(rhs));
    }
    else
    {
        return std::make_shared<ErrorResolveResult>(
            std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
                lhs->Type().shared_from_this()));
    }
}

// The C# private `OverloadResolution CreateOverloadResolution(ResolveResult[] arguments,
// string[] argumentNames = null, IType[] typeArguments = null)` (line 2435).
std::unique_ptr<OverloadResolution> CSharpResolver::CreateOverloadResolution(
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
    std::optional<std::vector<std::string>> argumentNames,
    std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>> typeArguments) const
{
    // The C# `new OverloadResolution(compilation, arguments, argumentNames, typeArguments,
    // conversions)` -- the resolver's `conversions` field is never null (both ctors
    // resolve it through `CSharpConversions::Get`), so the port passes the instance's
    // reference directly (no `?? Get(compilation)` fallback needed here).
    auto or = std::make_unique<OverloadResolution>(
        compilation_, std::move(arguments), std::move(argumentNames),
        std::move(typeArguments), &conversions_);
    // The C# `or.CheckForOverflow = checkForOverflow`.
    or->CheckForOverflow() = checkForOverflow_;
    return or;
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
