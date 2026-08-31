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

#include "Decompiler/CSharp/Resolver/AwaitResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversions.hpp"
#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"
#include "Decompiler/CSharp/Resolver/CSharpInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/DynamicInvocationResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/DynamicMemberResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/MethodGroupResolveResult.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolution.hpp"
#include "Decompiler/CSharp/Resolver/OverloadResolutionHelpers.hpp"
#include "Decompiler/CSharp/Resolver/TypeInferenceHelpers.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/AssignmentExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/BinaryOperatorExpression.hpp"
#include "Decompiler/CSharp/Syntax/Expressions/UnaryOperatorExpression.hpp"
#include "Decompiler/CSharp/TypeSystem/UsingScope.hpp"
#include "Decompiler/Semantics/AmbiguousResolveResult.hpp"
#include "Decompiler/Semantics/ArrayAccessResolveResult.hpp"
#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ConstantResolveResult.hpp"
#include "Decompiler/Semantics/ConversionFactories.hpp"
#include "Decompiler/Semantics/ConversionResolveResult.hpp"
#include "Decompiler/Semantics/ErrorResolveResult.hpp"
#include "Decompiler/Semantics/ForEachResolveResult.hpp"
#include "Decompiler/Semantics/LocalResolveResult.hpp"
#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/NamedArgumentResolveResult.hpp"
#include "Decompiler/Semantics/NamespaceResolveResult.hpp"
#include "Decompiler/Semantics/OperatorResolveResult.hpp"
#include "Decompiler/Semantics/SizeOfResolveResult.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/Semantics/TypeOfResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/Semantics/UnknownMemberResolveResult.hpp"
#include "Decompiler/TypeSystem/ICompilation.hpp"
#include "Decompiler/TypeSystem/INamespace.hpp"
#include "Decompiler/TypeSystem/IParameterizedMember.hpp"
#include "Decompiler/TypeSystem/ITypeDefinition.hpp"
#include "Decompiler/TypeSystem/ITypeParameter.hpp"
#include "Decompiler/TypeSystem/IMethod.hpp"
#include "Decompiler/TypeSystem/IProperty.hpp"
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"
#include "Decompiler/TypeSystem/NullableType.hpp"
#include "Decompiler/TypeSystem/ReflectionHelper.hpp"
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"
#include "Decompiler/TypeSystem/TypeSystemExtensions.hpp"
#include "Decompiler/TypeSystem/TypeUtils.hpp"
#include "Decompiler/Util/CSharpPrimitiveCast.hpp"
#include "Decompiler/Util/Decimal.hpp"

#include <algorithm>
#include <any>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <optional>
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

// ---- Simple-name lookup region (CSharpResolver.cs lines 1462-1790) -------------------------

// The C# `static readonly ResolveResult ErrorResult = ErrorResolveResult.UnknownError`
// (line 42) -- the singleton the non-overloadable arms return. A NON-OWNING aliasing
// handle (the empty-owner aliasing constructor: no deleter ever runs, so the
// program-lifetime singleton is never destroyed; the C# returns the same instance from
// every call). The `const_cast` is safe (the underlying singleton object is mutable; the
// accessor's const is the contract, the D515 convention).
namespace {
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> ErrorResultSingleton()
{
    return std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>(
        std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>(),
        const_cast<ILSpy::Decompiler::Semantics::ErrorResolveResult*>(
            &ILSpy::Decompiler::Semantics::ErrorResolveResult::UnknownError()));
}
} // namespace

// The C# `public ResolveResult ResolveSimpleName(string identifier, IReadOnlyList<IType>
// typeArguments, bool isInvocationTarget = false)` (line 1463) -- see CSharpResolver.hpp
// for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveSimpleName(std::string identifier,
                                  std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
                                  bool isInvocationTarget) const
{
    // C# 4.0 spec: section 7.6.2 Simple Names

    return LookupSimpleNameOrTypeName(
        std::move(identifier), std::move(typeArguments),
        isInvocationTarget ? NameLookupMode::InvocationTarget : NameLookupMode::Expression);
}

// The C# `public ResolveResult LookupSimpleNameOrTypeName(string identifier,
// IReadOnlyList<IType> typeArguments, NameLookupMode lookupMode)` (line 1473) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::LookupSimpleNameOrTypeName(
    std::string identifier,
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
    NameLookupMode lookupMode) const
{
    using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
    using ILSpy::Decompiler::Semantics::LocalResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::Semantics::TypeResolveResult;
    using ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult;
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::IParameter;
    using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypeParameter;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::SpecialType;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // C# 4.0 spec: sections 3.8 Namespace and type names; 7.6.2 Simple Names

    // The C# null guards (`identifier` / `typeArguments`) are structurally unreachable
    // through the `std::string` / `std::vector` value parameters (the D374 convention).

    const int k = static_cast<int>(typeArguments.size());

    if (k == 0) {
        if (lookupMode == NameLookupMode::Expression
            || lookupMode == NameLookupMode::InvocationTarget) {
            // Look in local variables (the `ImmutableStack` enumerates LIFO, so the
            // innermost block's dictionary is consulted first -- the `LocalVariables`
            // flattening convention).
            for (auto stack = localVariableStack_; !stack.empty(); stack = stack.pop()) {
                auto it = stack.top()->find(identifier);
                if (it != stack.top()->end()) {
                    return std::make_shared<LocalResolveResult>(it->second.get());
                }
            }
            // Look in parameters of current method
            if (const IParameterizedMember* parameterizedMember =
                    dynamic_cast<const IParameterizedMember*>(CurrentMember())) {
                for (const IParameter* p : parameterizedMember->Parameters()) {
                    if (p->Name() == identifier) {
                        return std::make_shared<LocalResolveResult>(p);
                    }
                }
            }
        }

        // look in type parameters of current method
        if (const IMethod* m = dynamic_cast<const IMethod*>(CurrentMember())) {
            for (const ITypeParameter* tp : m->TypeParameters()) {
                if (tp->Name() == identifier)
                    return std::make_shared<TypeResolveResult>(
                        std::const_pointer_cast<IType>(tp->shared_from_this()));
            }
        }
    }

    bool parameterizeResultType =
        !(k != 0 && std::all_of(typeArguments.begin(), typeArguments.end(),
                                [](const ITypePtr& t) {
                                    return t->Kind() == TypeKind::UnboundTypeArgument;
                                }));

    std::shared_ptr<ResolveResult> r;
    if (currentTypeDefinitionCache_ != nullptr) {
        std::unordered_map<std::string, std::shared_ptr<ResolveResult>>* cache = nullptr;
        bool foundInCache = false;
        if (k == 0) {
            switch (lookupMode) {
                case NameLookupMode::Expression:
                    cache = &currentTypeDefinitionCache_->SimpleNameLookupCacheExpression;
                    break;
                case NameLookupMode::InvocationTarget:
                    cache = &currentTypeDefinitionCache_->SimpleNameLookupCacheInvocationTarget;
                    break;
                case NameLookupMode::Type:
                    cache = &currentTypeDefinitionCache_->SimpleTypeLookupCache;
                    break;
                default:
                    break;
            }
            if (cache != nullptr) {
                // The C# `lock (cache)` is elided (a thread-safety measure with no
                // single-threaded behavioral effect -- the header convention).
                auto it = cache->find(identifier);
                foundInCache = it != cache->end();
                if (foundInCache) {
                    // The stored value may be the EMPTY handle (the C# known-negative
                    // `null` entry -- the cache also stores missing members).
                    r = it->second;
                }
            }
        }
        if (foundInCache) {
            r = (r != nullptr ? std::shared_ptr<ResolveResult>(r->ShallowClone())
                              : std::shared_ptr<ResolveResult>());
        } else {
            r = LookInCurrentType(identifier, typeArguments, lookupMode,
                                  parameterizeResultType);
            if (cache != nullptr) {
                // also cache missing members (r==null)
                (*cache)[identifier] = r;
            }
        }
        if (r != nullptr)
            return r;
    }

    if (CurrentUsingScope() == nullptr) {
        // If no using scope was specified, we still need to look in the global namespace:
        r = LookInUsingScopeNamespace(nullptr, &compilation_.RootNamespace(), identifier,
                                      typeArguments, parameterizeResultType);
    } else {
        if (k == 0 && lookupMode != NameLookupMode::TypeInUsingDeclaration) {
            std::shared_ptr<ResolveResult> cached;
            if (CurrentUsingScope()->ResolveCache.TryGetValue(identifier, cached)) {
                r = (cached != nullptr
                         ? std::shared_ptr<ResolveResult>(cached->ShallowClone())
                         : std::shared_ptr<ResolveResult>());
            } else {
                r = LookInCurrentUsingScope(identifier, typeArguments, false, false);
                CurrentUsingScope()->ResolveCache.TryAdd(identifier, r);
            }
        } else {
            r = LookInCurrentUsingScope(
                identifier, typeArguments,
                lookupMode == NameLookupMode::TypeInUsingDeclaration,
                parameterizeResultType);
        }
    }
    if (r != nullptr)
        return r;

    if (typeArguments.empty() && identifier == "dynamic") {
        return std::make_shared<TypeResolveResult>(
            std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType=*/true));
    } else {
        return std::make_shared<UnknownIdentifierResolveResult>(
            std::move(identifier), static_cast<int>(typeArguments.size()));
    }
}

// The C# `public bool IsVariableReferenceWithSameType(ResolveResult rr, string identifier,
// out TypeResolveResult trr)` (line 1604) -- see CSharpResolver.hpp for the port
// conventions.
bool CSharpResolver::IsVariableReferenceWithSameType(
    const ILSpy::Decompiler::Semantics::ResolveResult& rr,
    const std::string& identifier,
    std::shared_ptr<ILSpy::Decompiler::Semantics::TypeResolveResult>& trr) const
{
    using ILSpy::Decompiler::Semantics::LocalResolveResult;
    using ILSpy::Decompiler::Semantics::MemberResolveResult;
    using ILSpy::Decompiler::Semantics::TypeResolveResult;

    if (dynamic_cast<const MemberResolveResult*>(&rr) == nullptr
        && dynamic_cast<const LocalResolveResult*>(&rr) == nullptr) {
        trr = nullptr;
        return false;
    }
    trr = std::dynamic_pointer_cast<TypeResolveResult>(
        LookupSimpleNameOrTypeName(identifier, {}, NameLookupMode::Type));
    return trr != nullptr && trr->Type().Equals(rr.Type());
}

// The C# `public MemberLookup CreateMemberLookup()` (line 1887) -- see
// CSharpResolver.hpp for the port conventions.
MemberLookup CSharpResolver::CreateMemberLookup() const
{
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::SymbolKind;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    const ITypeDefinition* currentTypeDefinition = CurrentTypeDefinition();
    bool isInEnumMemberInitializer =
        CurrentMember() != nullptr && CurrentMember()->SymbolKind() == SymbolKind::Field
        && currentTypeDefinition != nullptr
        && currentTypeDefinition->Kind() == TypeKind::Enum;
    return MemberLookup(currentTypeDefinition, &compilation_.MainModule(),
                        isInEnumMemberInitializer);
}

// The C# `public MemberLookup CreateMemberLookup(NameLookupMode lookupMode)` (line 1899)
// -- see CSharpResolver.hpp for the port conventions.
MemberLookup CSharpResolver::CreateMemberLookup(NameLookupMode lookupMode) const
{
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;

    if (lookupMode == NameLookupMode::BaseTypeReference && CurrentTypeDefinition() != nullptr) {
        // When looking up a base type reference, treat us as being outside the current
        // type definition for accessibility purposes.
        // This avoids a stack overflow when referencing a protected class nested inside
        // the base class of a parent class.
        // (NameLookupTests.InnerClassInheritingFromProtectedBaseInnerClassShouldNotCauseStackOverflow)
        return MemberLookup(CurrentTypeDefinition()->DeclaringTypeDefinition(),
                            &compilation_.MainModule(), false);
    } else {
        return CreateMemberLookup();
    }
}

// The C# `ResolveResult LookInCurrentType(string identifier, IReadOnlyList<IType>
// typeArguments, NameLookupMode lookupMode, bool parameterizeResultType)` (line 1621) --
// see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::LookInCurrentType(
    const std::string& identifier,
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& typeArguments,
    NameLookupMode lookupMode,
    bool parameterizeResultType) const
{
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::Semantics::TypeResolveResult;
    using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypeParameter;

    const int k = static_cast<int>(typeArguments.size());
    MemberLookup lookup = CreateMemberLookup(lookupMode);
    // look in current type definitions
    for (const ITypeDefinition* t = CurrentTypeDefinition(); t != nullptr;
         t = t->DeclaringTypeDefinition()) {
        if (k == 0) {
            // Look for type parameter with that name
            // Look at all type parameters, including those copied from outer classes,
            // so that we can fetch the version with the correct owner.
            for (const ITypeParameter* tp : t->TypeParameters()) {
                if (tp->Name() == identifier)
                    return std::make_shared<TypeResolveResult>(
                        std::const_pointer_cast<IType>(tp->shared_from_this()));
            }
        }

        if (lookupMode == NameLookupMode::BaseTypeReference && t == CurrentTypeDefinition()) {
            // don't look in current type when resolving a base type reference
            continue;
        }

        std::shared_ptr<ResolveResult> r;
        if (lookupMode == NameLookupMode::Expression
            || lookupMode == NameLookupMode::InvocationTarget) {
            std::shared_ptr<ResolveResult> targetResolveResult =
                (t == CurrentTypeDefinition()
                     ? ResolveThisReference()
                     : std::make_shared<TypeResolveResult>(
                           std::const_pointer_cast<IType>(t->shared_from_this())));
            r = lookup.Lookup(*targetResolveResult, identifier, typeArguments,
                              lookupMode == NameLookupMode::InvocationTarget);
        } else {
            r = lookup.LookupType(*t, identifier, typeArguments, parameterizeResultType);
        }
        if (dynamic_cast<const UnknownMemberResolveResult*>(r.get()) == nullptr) {
            // but do return AmbiguousMemberResolveResult
            return r;
        }
    }
    return nullptr;
}

// The C# `ResolveResult LookInCurrentUsingScope(string identifier, IReadOnlyList<IType>
// typeArguments, bool isInUsingDeclaration, bool parameterizeResultType)` (line 1668) --
// see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::LookInCurrentUsingScope(
    const std::string& identifier,
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& typeArguments,
    bool isInUsingDeclaration,
    bool parameterizeResultType) const
{
    using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
    using ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::Semantics::TypeResolveResult;
    using ILSpy::Decompiler::TypeSystem::INamespace;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::ParameterizedType;

    // look in current namespace definitions
    std::shared_ptr<UsingScope> currentUsingScope = CurrentUsingScope();
    for (std::shared_ptr<UsingScope> u = currentUsingScope; u != nullptr; u = u->Parent()) {
        auto resultInNamespace =
            LookInUsingScopeNamespace(u.get(), &u->Namespace(), identifier, typeArguments,
                                      parameterizeResultType);
        if (resultInNamespace != nullptr)
            return resultInNamespace;
        // then look for aliases:
        if (typeArguments.empty()) {
            // The port's `ExternAliases` is always empty (the C# expression-bodied
            // `=> [];`), so this arm never fires -- kept faithful for the day the alias
            // tracking lands.
            for (const std::string& externAlias : u->ExternAliases()) {
                if (externAlias == identifier) {
                    return ResolveExternAlias(identifier);
                }
            }
            if (!(isInUsingDeclaration && u == currentUsingScope)) {
                for (const auto& pair : u->UsingAliases()) {
                    if (pair.first == identifier) {
                        // The null guard is the D516 safe fallback (the C# would NRE on
                        // a null alias value; the arm is unreachable in the port's
                        // always-empty `UsingAliases`).
                        return pair.second != nullptr
                                   ? std::shared_ptr<ResolveResult>(pair.second->ShallowClone())
                                   : nullptr;
                    }
                }
            }
        }
        // finally, look in the imported namespaces:
        if (!(isInUsingDeclaration && u == currentUsingScope)) {
            ITypePtr firstResult;
            for (const INamespace* importedNamespace : u->Usings()) {
                const ITypeDefinition* def = importedNamespace->GetTypeDefinition(
                    identifier, static_cast<int>(typeArguments.size()));
                if (def != nullptr) {
                    ITypePtr resultType;
                    if (parameterizeResultType && !typeArguments.empty())
                        resultType = std::make_shared<ParameterizedType>(
                            std::const_pointer_cast<IType>(def->shared_from_this()),
                            typeArguments);
                    else
                        resultType = std::const_pointer_cast<IType>(def->shared_from_this());

                    if (firstResult == nullptr
                        || !TopLevelTypeDefinitionIsAccessible(firstResult->GetDefinition())) {
                        if (TopLevelTypeDefinitionIsAccessible(resultType->GetDefinition()))
                            firstResult = resultType;
                    } else if (TopLevelTypeDefinitionIsAccessible(def)) {
                        return std::make_shared<AmbiguousTypeResolveResult>(firstResult);
                    }
                }
            }
            if (firstResult != nullptr)
                return std::make_shared<TypeResolveResult>(firstResult);
        }
        // if we didn't find anything: repeat lookup with parent namespace
    }
    return nullptr;
}

// The C# `ResolveResult LookInUsingScopeNamespace(UsingScope usingScope, INamespace n,
// string identifier, IReadOnlyList<IType> typeArguments, bool parameterizeResultType)`
// (line 1707) -- see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::LookInUsingScopeNamespace(
    const ILSpy::Decompiler::CSharp::TypeSystem::UsingScope* usingScope,
    const ILSpy::Decompiler::TypeSystem::INamespace* n,
    const std::string& identifier,
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& typeArguments,
    bool parameterizeResultType) const
{
    using ILSpy::Decompiler::Semantics::AmbiguousTypeResolveResult;
    using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
    using ILSpy::Decompiler::Semantics::TypeResolveResult;
    using ILSpy::Decompiler::TypeSystem::INamespace;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::ParameterizedType;
    using ILSpy::Decompiler::TypeSystem::UnknownType;

    if (n == nullptr)
        return nullptr;
    // first look for a namespace
    const int k = static_cast<int>(typeArguments.size());
    if (k == 0) {
        const INamespace* childNamespace = n->GetChildNamespace(identifier);
        if (childNamespace != nullptr) {
            if (usingScope != nullptr && usingScope->HasAlias(identifier))
                // The elaborated-type-specifier (`class UnknownType`) targets the CLASS
                // (the `make_shared<UnknownType>`-resolves-to-the-function MSVC quirk,
                // D417; the NestedTypeReference fallback precedent).
                return std::make_shared<AmbiguousTypeResolveResult>(
                    ITypePtr(new class UnknownType(std::nullopt, identifier, 0)));
            return std::make_shared<NamespaceResolveResult>(childNamespace);
        }
    }
    // then look for a type
    const ITypeDefinition* def = n->GetTypeDefinition(identifier, k);
    if (def != nullptr && TopLevelTypeDefinitionIsAccessible(def)) {
        ITypePtr result = std::const_pointer_cast<IType>(def->shared_from_this());
        if (parameterizeResultType && k > 0) {
            result = std::make_shared<ParameterizedType>(
                std::const_pointer_cast<IType>(def->shared_from_this()), typeArguments);
        }
        if (usingScope != nullptr && usingScope->HasAlias(identifier))
            return std::make_shared<AmbiguousTypeResolveResult>(std::move(result));
        else
            return std::make_shared<TypeResolveResult>(std::move(result));
    }
    return nullptr;
}

// The C# `bool TopLevelTypeDefinitionIsAccessible(ITypeDefinition typeDef)` (line 1748)
// -- see CSharpResolver.hpp for the port conventions.
bool CSharpResolver::TopLevelTypeDefinitionIsAccessible(
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* typeDef) const
{
    using ILSpy::Decompiler::TypeSystem::Accessibility;
    using ILSpy::Decompiler::TypeSystem::IModule;

    // The null guard is the D516 safe fallback (the C# would NRE; the not-accessible
    // direction keeps the lookup scanning).
    if (typeDef == nullptr)
        return false;
    if (typeDef->Accessibility() == Accessibility::Internal) {
        const IModule* parentModule = typeDef->ParentModule();
        // The null guard is the D516 safe fallback (a type definition always carries a
        // parent module in the real metadata).
        if (parentModule == nullptr)
            return false;
        return parentModule->InternalsVisibleTo(compilation_.MainModule());
    }
    return true;
}

// The C# `public ResolveResult ResolveAlias(string identifier)` (line 1760) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveAlias(const std::string& identifier) const
{
    using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
    using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;

    if (identifier == "global")
        return std::make_shared<NamespaceResolveResult>(&compilation_.RootNamespace());

    for (std::shared_ptr<UsingScope> n = CurrentUsingScope(); n != nullptr; n = n->Parent()) {
        // The port's `ExternAliases` is always empty (the arm never fires; kept
        // faithful).
        for (const std::string& externAlias : n->ExternAliases()) {
            if (externAlias == identifier) {
                return ResolveExternAlias(identifier);
            }
        }
        for (const auto& pair : n->UsingAliases()) {
            if (pair.first == identifier) {
                // `(pair.Value as NamespaceResolveResult) ?? ErrorResult` -- the C#
                // returns the SAME instance (no ShallowClone here, unlike
                // LookInCurrentUsingScope); a null value or a non-namespace alias
                // yields the ErrorResult singleton (a null `pair.second` fails the
                // dynamic_cast, faithfully matching the C# `as` on null).
                if (dynamic_cast<const NamespaceResolveResult*>(pair.second.get()) != nullptr)
                    return pair.second;
                return ErrorResultSingleton();
            }
        }
    }
    return ErrorResultSingleton();
}

// The C# `ResolveResult ResolveExternAlias(string alias)` (line 1784) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveExternAlias(const std::string& alias) const
{
    using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
    using ILSpy::Decompiler::TypeSystem::INamespace;

    const INamespace* ns = compilation_.GetNamespaceForExternAlias(alias);
    if (ns != nullptr)
        return std::make_shared<NamespaceResolveResult>(ns);
    else
        return ErrorResultSingleton();
}

// ---- Extension methods region (CSharpResolver.cs lines 2018-2243) --------------------------

// The C# `IEnumerable<IMethod> GetExtensionMethods(MemberLookup lookup, INamespace ns)`
// (line 2213) -- see CSharpResolver.hpp for the port conventions.
std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>
CSharpResolver::GetExtensionMethods(
    const MemberLookup& lookup,
    const ILSpy::Decompiler::TypeSystem::INamespace& ns) const
{
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;

    std::vector<const IMethod*> result;
    for (const ITypeDefinition* c : ns.Types())
    {
        // The non-owning `Types()` snapshot has no null entries in practice; the
        // degenerate entry is skipped rather than dereferenced (the D516 convention;
        // the C# would NRE).
        if (c == nullptr)
            continue;
        // The C# `c.IsStatic && c.HasExtensions && c.TypeParameters.Count == 0 &&
        // lookup.IsAccessible(c, false)`.
        if (!(c->IsStatic() && c->HasExtensions() && c->TypeParameters().empty()
              && lookup.IsAccessible(*c, /*allowProtectedAccess*/ false)))
        {
            continue;
        }
        for (const IMethod* m : c->Methods())
        {
            if (m == nullptr) // the D516 convention (a real Methods() has no nulls)
                continue;
            if (m->IsExtensionMethod())
                result.push_back(m);
        }
    }
    return result;
}

// The C# `IList<List<IMethod>> GetAllExtensionMethods(MemberLookup lookup)` (line 2188)
// -- see CSharpResolver.hpp for the port conventions.
std::shared_ptr<std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>>>
CSharpResolver::GetAllExtensionMethods(const MemberLookup& lookup) const
{
    using ILSpy::Decompiler::CSharp::TypeSystem::UsingScope;
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::INamespace;

    std::shared_ptr<UsingScope> currentUsingScope = context_->CurrentUsingScope();
    if (!currentUsingScope)
    {
        // The C# `EmptyList<List<IMethod>>.Instance` -- a shared EMPTY list (the
        // non-null return contract every caller iterates; an empty group vector means
        // no groups, faithfully matching the empty singleton).
        return std::make_shared<std::vector<std::vector<const IMethod*>>>();
    }
    // The C# `LazyInit.VolatileRead(ref currentUsingScope.AllExtensionMethods)` -- the
    // plain shared_ptr read is the single-threaded VolatileRead (the field is only
    // mutated through this memoization).
    std::shared_ptr<std::vector<std::vector<const IMethod*>>> cached =
        currentUsingScope->AllExtensionMethods;
    if (cached)
        return cached;

    std::vector<std::vector<const IMethod*>> extensionMethodGroups;
    // The C# `for (UsingScope scope = currentUsingScope; scope != null; scope =
    // scope.Parent)` -- innermost (most nested) scope first.
    for (std::shared_ptr<UsingScope> scope = currentUsingScope; scope != nullptr;
         scope = scope->Parent())
    {
        // The scope's own namespace is non-null by the `UsingScope` ctor contract (the
        // C# `if (ns != null)` guard is structurally unreachable, the D374 convention).
        std::vector<const IMethod*> m = GetExtensionMethods(lookup, scope->Namespace());
        if (!m.empty())
            extensionMethodGroups.push_back(std::move(m));

        // The C# `scope.Usings.Distinct()` -- the default reference-equality comparer is
        // POINTER identity for the namespace references, deduplicated before the
        // `SelectMany` concatenation.
        std::vector<const INamespace*> distinctUsings;
        for (const INamespace* importedNamespace : scope->Usings())
        {
            if (std::find(distinctUsings.begin(), distinctUsings.end(), importedNamespace)
                == distinctUsings.end())
            {
                distinctUsings.push_back(importedNamespace);
            }
        }
        std::vector<const IMethod*> imported;
        for (const INamespace* importedNamespace : distinctUsings)
        {
            std::vector<const IMethod*> inNamespace =
                GetExtensionMethods(lookup, *importedNamespace);
            imported.insert(imported.end(), inNamespace.begin(), inNamespace.end());
        }
        if (!imported.empty())
            extensionMethodGroups.push_back(std::move(imported));
    }
    // The C# `LazyInit.GetOrSet(ref currentUsingScope.AllExtensionMethods,
    // extensionMethodGroups)` -- the first-writer-wins store: a concurrent winner's
    // value is returned, the single-threaded port's field is still null here (this
    // method is the field's only writer and the early return above means it was null
    // when the walk started), so the store wins and the computed list is returned.
    auto computed = std::make_shared<std::vector<std::vector<const IMethod*>>>(
        std::move(extensionMethodGroups));
    if (!currentUsingScope->AllExtensionMethods)
    {
        currentUsingScope->AllExtensionMethods = computed;
        return computed;
    }
    return currentUsingScope->AllExtensionMethods;
}

// The C# `public List<List<IMethod>> GetExtensionMethods(IType targetType, string name =
// null, IReadOnlyList<IType> typeArguments = null, bool substituteInferredTypes =
// false)` (line 2065) -- see CSharpResolver.hpp for the port conventions.
std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>>
CSharpResolver::GetExtensionMethods(
    const ILSpy::Decompiler::TypeSystem::IType* targetType,
    const std::optional<std::string>& name,
    const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& typeArguments,
    bool substituteInferredTypes) const
{
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;

    MemberLookup lookup = CreateMemberLookup();
    std::vector<std::vector<const IMethod*>> extensionMethodGroups;
    std::shared_ptr<std::vector<std::vector<const IMethod*>>> allMethods =
        GetAllExtensionMethods(lookup);
    for (const std::vector<const IMethod*>& inputGroup : *allMethods)
    {
        std::vector<const IMethod*> outputGroup;
        for (const IMethod* method : inputGroup)
        {
            // The non-owning snapshot has no nulls in practice; the degenerate entry
            // is skipped rather than dereferenced (the D516 convention).
            if (method == nullptr)
                continue;
            if (name.has_value() && method->Name() != *name)
                continue;
            if (!lookup.IsAccessible(*method, /*allowProtectedAccess*/ false))
                continue;
            std::optional<std::vector<ITypePtr>> inferredTypes;
            if (typeArguments.has_value() && !typeArguments->empty())
            {
                // The C# `if (method.TypeParameters.Count != typeArguments.Count)
                // continue;` -- only arity-matching generic methods are specialized.
                if (method->TypeParameters().size() != typeArguments->size())
                    continue;
                // The C# `var sm = method.Specialize(new TypeParameterSubstitution(null,
                // typeArguments));` -- the explicit type-arguments specialization, then
                // the eligibility of the SPECIALIZED form (no inference: `false`).
                TypeParameterSubstitution substitution(std::nullopt, *typeArguments);
                const IMethod* sm = method->Specialize(&substitution);
                if (IsEligibleExtensionMethod(compilation_, conversions_, targetType, *sm,
                                              /*useTypeInference*/ false, inferredTypes))
                    outputGroup.push_back(sm);
            }
            else
            {
                if (IsEligibleExtensionMethod(compilation_, conversions_, targetType, *method,
                                              /*useTypeInference*/ true, inferredTypes))
                {
                    if (substituteInferredTypes && inferredTypes.has_value())
                    {
                        // The C# `method.Specialize(new TypeParameterSubstitution(null,
                        // inferredTypes))` -- the inferred-arguments specialization.
                        TypeParameterSubstitution substitution(std::nullopt, *inferredTypes);
                        outputGroup.push_back(method->Specialize(&substitution));
                    }
                    else
                    {
                        outputGroup.push_back(method);
                    }
                }
            }
        }
        if (!outputGroup.empty())
            extensionMethodGroups.push_back(std::move(outputGroup));
    }
    return extensionMethodGroups;
}

// The C# `public List<List<IMethod>> GetExtensionMethods(string name = null,
// IReadOnlyList<IType> typeArguments = null)` (line 2036) -- the no-target thin delegate
// (`GetExtensionMethods(null, name, typeArguments)`).
std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>>
CSharpResolver::GetExtensionMethods(
    const std::optional<std::string>& name,
    const std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& typeArguments) const
{
    return GetExtensionMethods(nullptr, name, typeArguments, /*substituteInferredTypes*/ false);
}

// The C# `public static bool IsEligibleExtensionMethod(IType targetType, IMethod method,
// bool useTypeInference, out IType[] outInferredTypes)` (line 2123) -- see
// CSharpResolver.hpp for the port conventions.
bool CSharpResolver::IsEligibleExtensionMethod(
    const ILSpy::Decompiler::TypeSystem::IType* targetType,
    const ILSpy::Decompiler::TypeSystem::IMethod& method,
    bool useTypeInference,
    std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& outInferredTypes)
{
    // The C# `var compilation = method.Compilation;` (the two `ArgumentNullException`
    // guards compile out -- the reference parameter cannot bind to null, the D374
    // convention; the null `targetType` is a documented ELIGIBLE shape, not an error).
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation = method.Compilation();
    CSharpConversions& conversions = CSharpConversions::Get(compilation);
    return IsEligibleExtensionMethod(compilation, conversions, targetType, method,
                                      useTypeInference, outInferredTypes);
}

// The C# `static bool IsEligibleExtensionMethod(ICompilation compilation,
// CSharpConversions conversions, IType targetType, IMethod method, bool useTypeInference,
// out IType[] outInferredTypes)` (line 2133) -- see CSharpResolver.hpp for the port
// conventions.
bool CSharpResolver::IsEligibleExtensionMethod(
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation,
    CSharpConversions& conversions,
    const ILSpy::Decompiler::TypeSystem::IType* targetType,
    const ILSpy::Decompiler::TypeSystem::IMethod& method,
    bool useTypeInference,
    std::optional<std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>>& outInferredTypes)
{
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::ByReferenceType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::TypeKind;
    using ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution;

    outInferredTypes = std::nullopt;
    // The C# `if (targetType == null) return true;` -- a missing target type is
    // eligible for every extension method (the code-completion shape).
    if (targetType == nullptr)
        return true;
    if (method.Parameters().empty())
        return false;

    // The C# `IType thisParameterType = method.Parameters[0].Type;` -- a REBINDABLE
    // local (the ByReference unwrap and the inference substitution below rebind it),
    // so the port uses a pointer (a C++ reference cannot rebind).
    const IType* thisParameterType = &method.Parameters()[0]->Type();
    if (thisParameterType->Kind() == TypeKind::ByReference)
    {
        // Extension method with `this in` or `this ref` -- the C# hard cast
        // `((ByReferenceType)thisParameterType).ElementType`; a degenerate same-kind
        // non-ByReferenceType or null-element shape (never produced by a real type
        // system) keeps the un-unwrapped parameter type (the D516 safe-fallback
        // convention) instead of the C# InvalidCastException / NRE.
        const ByReferenceType* brt = dynamic_cast<const ByReferenceType*>(thisParameterType);
        if (brt != nullptr && brt->Element())
            thisParameterType = brt->Element().get();
    }

    std::vector<ITypePtr> inferredTypes;
    if (useTypeInference && !method.TypeParameters().empty())
    {
        // The C# `TypeInference ti = new TypeInference(compilation, conversions);` --
        // the internal ctor threading the resolver's conversions; the class's default
        // `TypeInferenceAlgorithm.CSharp4` field is the algorithm the fresh instance
        // carries (the C# field initializer, not a parameter).
        std::vector<std::shared_ptr<ResolveResult>> arguments;
        // The C# `new ResolveResult(targetType)` -- the owning handle recovered from
        // the const accessor contract (the D529 `shared_from_this` +
        // `const_pointer_cast` convention; every real IType is shared-managed).
        arguments.push_back(std::make_shared<ResolveResult>(
            std::const_pointer_cast<IType>(targetType->shared_from_this())));
        std::vector<ITypePtr> parameterTypes;
        parameterTypes.push_back(
            std::const_pointer_cast<IType>(thisParameterType->shared_from_this()));
        bool success = false; // the C# discards the out `success` (`out _`)
        inferredTypes = Detail::InferTypeArguments(
            compilation, conversions, method.TypeParameters(), arguments, parameterTypes,
            success, /*classTypeArguments*/ std::nullopt, TypeInferenceAlgorithm::CSharp4);

        // The C# `new TypeParameterSubstitution(null, inferredTypes)` ALIASES the
        // inferredTypes ARRAY (a C# array is a reference type, so the fix-ups below
        // are visible to every later use of the substitution); the port re-constructs
        // the substitution at each use from the vector's CURRENT state, reproducing
        // the aliasing exactly (positions fixed so far read back the fix-up).
        auto makeSubstitution = [&inferredTypes]() {
            return TypeParameterSubstitution(
                std::nullopt, std::optional<std::vector<ITypePtr>>(inferredTypes));
        };

        bool hasInferredTypes = false;
        for (std::size_t i = 0; i < inferredTypes.size(); i++)
        {
            if (inferredTypes[i]->Kind() != TypeKind::Unknown
                && inferredTypes[i]->Kind() != TypeKind::UnboundTypeArgument)
            {
                hasInferredTypes = true;
                // The C# `OverloadResolution.ValidateConstraints(method.TypeParameters[i],
                // inferredTypes[i], substitution, conversions)` -- the internal 4-arg
                // static (the `Detail::ValidateConstraints` free function; the
                // `IType&` parameter is non-const because the substitution visitor may
                // rebind it).
                TypeParameterSubstitution substitution = makeSubstitution();
                if (!Detail::ValidateConstraints(
                        *method.TypeParameters()[i], *inferredTypes[i], &substitution,
                        conversions))
                    return false;
            }
            else
            {
                // The C# `inferredTypes[i] = method.TypeParameters[i];` -- do not
                // substitute types that could not be inferred (the owning handle of
                // the type parameter via the D529 convention).
                inferredTypes[i] = std::const_pointer_cast<IType>(
                    method.TypeParameters()[i]->shared_from_this());
            }
        }
        if (hasInferredTypes)
            outInferredTypes = inferredTypes;

        // The C# `thisParameterType = thisParameterType.AcceptVisitor(substitution);` --
        // the FINAL substitution (after ALL the fix-ups) applied to the parameter
        // type. `AcceptVisitor` is non-const (the D406 TypeVisitor convention), so the
        // const accessor result is cast (the underlying type-system objects are
        // mutable, the D515/D517 `const_cast` precedent).
        TypeParameterSubstitution finalSubstitution = makeSubstitution();
        ITypePtr substituted =
            const_cast<IType*>(thisParameterType)->AcceptVisitor(finalSubstitution);
        if (substituted)
            thisParameterType = substituted.get();
    }

    // The C# `Conversion c = conversions.ImplicitConversion(targetType,
    // thisParameterType);` -- the CACHED public entry; the non-const `IType&`
    // parameters take the cast accessor results (the D515/D517 convention).
    std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion> c = conversions.ImplicitConversion(
        const_cast<IType&>(*targetType), const_cast<IType&>(*thisParameterType));
    // The C# `c.IsValid && (c.IsIdentityConversion || c.IsReferenceConversion ||
    // c.IsBoxingConversion || c.IsImplicitSpanConversion)` -- a VALID conversion that
    // is none of the four kinds (e.g. a NUMERIC widening) does NOT make the method
    // eligible.
    return c->IsValid() && (c->IsIdentityConversion() || c->IsReferenceConversion()
                            || c->IsBoxingConversion()
                            || c->IsImplicitSpanConversion());
}

// ---- Member-access region (CSharpResolver.cs lines 1795-1912) --------------------------------

// The C# `public ResolveResult ResolveMemberAccess(ResolveResult target, string
// identifier, IReadOnlyList<IType> typeArguments, NameLookupMode lookupMode =
// NameLookupMode.Expression)` (line 1795) -- see CSharpResolver.hpp for the port
// conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveMemberAccess(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target,
    std::string identifier,
    std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr> typeArguments,
    NameLookupMode lookupMode) const
{
    using ILSpy::Decompiler::CSharp::Resolver::DynamicMemberResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
    using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // C# 4.0 spec: section 7.6.4

    // The C# `bool parameterizeResultType = !(typeArguments.Count != 0 &&
    // typeArguments.All(t => t.Kind == TypeKind.UnboundTypeArgument));` -- a list of ALL
    // unbound-type-argument placeholders (the `List<>` in `typeof(List<>)`) suppresses
    // the parameterization; anything else (including the empty list) parameterizes.
    bool parameterizeResultType = !(!typeArguments.empty()
                                    && std::all_of(typeArguments.begin(), typeArguments.end(),
                                                   [](const ITypePtr& t) {
                                                       return t->Kind() == TypeKind::UnboundTypeArgument;
                                                   }));
    // The C# `NamespaceResolveResult nrr = target as NamespaceResolveResult;`
    std::shared_ptr<NamespaceResolveResult> nrr =
        std::dynamic_pointer_cast<NamespaceResolveResult>(target);
    if (nrr) {
        return ResolveMemberAccessOnNamespace(*nrr, identifier, typeArguments,
                                               parameterizeResultType);
    }

    if (target->Type().Kind() == TypeKind::Dynamic)
        return std::make_shared<DynamicMemberResolveResult>(std::move(target),
                                                             std::move(identifier));

    MemberLookup lookup = CreateMemberLookup(lookupMode);
    std::shared_ptr<ResolveResult> result;
    switch (lookupMode) {
        case NameLookupMode::Expression:
            // The C# `lookup.Lookup(target, identifier, typeArguments, isInvocation:
            // false)`.
            result = lookup.Lookup(*target, identifier, typeArguments, /*isInvocation*/ false);
            break;
        case NameLookupMode::InvocationTarget:
            // The C# `lookup.Lookup(target, identifier, typeArguments, isInvocation:
            // true)`.
            result = lookup.Lookup(*target, identifier, typeArguments, /*isInvocation*/ true);
            break;
        case NameLookupMode::Type:
        case NameLookupMode::TypeInUsingDeclaration:
        case NameLookupMode::BaseTypeReference:
            // Don't do the UnknownMemberResolveResult/MethodGroupResolveResult processing,
            // it's only relevant for expressions.
            return lookup.LookupType(target->Type(), identifier, typeArguments,
                                     parameterizeResultType);
        default:
            // The C# `throw new NotSupportedException(...)`.
            throw std::logic_error(
                "CSharpResolver::ResolveMemberAccess: Invalid value for NameLookupMode");
    }
    if (dynamic_cast<const UnknownMemberResolveResult*>(result.get()) != nullptr) {
        // We intentionally use all extension methods here, not just the eligible ones.
        // Proper eligibility checking is only possible for the full invocation
        // (after we know the remaining arguments).
        // The eligibility check in GetExtensionMethods is only intended for code completion.
        std::vector<std::vector<const ILSpy::Decompiler::TypeSystem::IMethod*>> extensionMethods =
            GetExtensionMethods(identifier, typeArguments);
        if (!extensionMethods.empty()) {
            // The C# `new MethodGroupResolveResult(target, identifier,
            // EmptyList<MethodListWithDeclaringType>.Instance, typeArguments) {
            // extensionMethods = extensionMethods }` -- the object-initializer field
            // assignment ports to the `SetExtensionMethods` call.
            auto mgrr = std::make_shared<MethodGroupResolveResult>(
                std::move(target), std::move(identifier),
                std::vector<MethodListWithDeclaringType>{}, std::move(typeArguments));
            mgrr->SetExtensionMethods(std::move(extensionMethods));
            return mgrr;
        }
    } else {
        std::shared_ptr<MethodGroupResolveResult> mgrr =
            std::dynamic_pointer_cast<MethodGroupResolveResult>(result);
        if (mgrr) {
            // The C# `Debug.Assert(mgrr.extensionMethods == null)` -- a fresh lookup
            // result has not yet fetched its extension methods.
            assert(!mgrr->HasExtensionMethods());
            // set the values that are necessary to make
            // MethodGroupResolveResult.GetExtensionMethods() work
            mgrr->SetResolver(this);
        }
    }
    return result;
}

// The C# `ResolveResult ResolveMemberAccessOnNamespace(NamespaceResolveResult nrr,
// string identifier, IReadOnlyList<IType> typeArguments, bool parameterizeResultType)`
// (line 1877) -- see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveMemberAccessOnNamespace(
    const ILSpy::Decompiler::Semantics::NamespaceResolveResult& nrr,
    const std::string& identifier,
    const std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>& typeArguments,
    bool parameterizeResultType) const
{
    using ILSpy::Decompiler::Semantics::NamespaceResolveResult;
    using ILSpy::Decompiler::Semantics::TypeResolveResult;
    using ILSpy::Decompiler::TypeSystem::INamespace;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::ParameterizedType;

    if (typeArguments.empty()) {
        const INamespace* childNamespace = nrr.Namespace()->GetChildNamespace(identifier);
        if (childNamespace != nullptr)
            return std::make_shared<NamespaceResolveResult>(childNamespace);
    }
    const ITypeDefinition* def =
        nrr.Namespace()->GetTypeDefinition(identifier, static_cast<int>(typeArguments.size()));
    if (def != nullptr) {
        if (parameterizeResultType && !typeArguments.empty()) {
            // The owning handle for the generic is recovered via `shared_from_this` +
            // `const_pointer_cast` (the D529 convention; the LookInUsingScopeNamespace
            // precedent for this exact shape).
            return std::make_shared<TypeResolveResult>(std::make_shared<ParameterizedType>(
                std::const_pointer_cast<IType>(def->shared_from_this()), typeArguments));
        } else {
            return std::make_shared<TypeResolveResult>(
                std::const_pointer_cast<IType>(def->shared_from_this()));
        }
    }
    return ErrorResultSingleton();
}

// The C# `public ResolveResult ResolveIdentifierInObjectInitializer(string identifier)`
// (line 1906) -- see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveIdentifierInObjectInitializer(std::string identifier) const
{
    MemberLookup memberLookup = CreateMemberLookup();
    return memberLookup.Lookup(CurrentObjectInitializer(), identifier,
                               std::vector<ILSpy::Decompiler::TypeSystem::ITypePtr>{},
                               /*isInvocation*/ false);
}

// ---- Invocation region (CSharpResolver.cs lines 2227-2443) ---------------------------------

// The C# `IList<ResolveResult> AddArgumentNamesIfNecessary(ResolveResult[] arguments,
// string[] argumentNames)` (line 2227) -- see CSharpResolver.hpp for the port
// conventions.
std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>
CSharpResolver::AddArgumentNamesIfNecessary(
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::optional<std::vector<std::string>>& argumentNames) const
{
    using ILSpy::Decompiler::Semantics::NamedArgumentResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;

    // The C# `if (argumentNames == null) return arguments;` -- the null ARRAY ports to
    // the empty optional (the arguments are returned as-is, sharing the handles).
    if (!argumentNames.has_value())
        return arguments;
    // The C# `result[i] = (argumentNames[i] != null ? new NamedArgumentResolveResult(
    // argumentNames[i], arguments[i]) : arguments[i])` -- a null ENTRY ports to the
    // empty string (the GetArgumentsWithConversions normalization); an out-of-range
    // entry is treated as positional (the D516 safe-fallback convention; the C# would
    // throw IndexOutOfRangeException on a mismatched-length array).
    std::vector<std::shared_ptr<ResolveResult>> result;
    result.reserve(arguments.size());
    for (size_t i = 0; i < arguments.size(); i++) {
        if (i < argumentNames->size() && !(*argumentNames)[i].empty())
            result.push_back(std::make_shared<NamedArgumentResolveResult>(
                (*argumentNames)[i], arguments[i]));
        else
            result.push_back(arguments[i]);
    }
    return result;
}

// The C# `private ResolveResult ResolveInvocation(ResolveResult target, ResolveResult[]
// arguments, string[] argumentNames, bool allowOptionalParameters)` (line 2244) + the
// public 3-arg overload (line 2336) -- see CSharpResolver.hpp for the port
// conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveInvocation(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target,
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
    std::optional<std::vector<std::string>> argumentNames,
    bool allowOptionalParameters) const
{
    using ILSpy::Decompiler::CSharp::Resolver::CSharpInvocationResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationType;
    using ILSpy::Decompiler::CSharp::Resolver::IsApplicable;
    using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
    using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::Semantics::TypeResolveResult;
    using ILSpy::Decompiler::Semantics::UnknownIdentifierResolveResult;
    using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
    using ILSpy::Decompiler::Semantics::UnknownMethodResolveResult;
    using ILSpy::Decompiler::TypeSystem::GetDelegateInvokeMethod;
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::SpecialType;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // C# 4.0 spec: section 7.6.5

    if (target->Type().Kind() == TypeKind::Dynamic) {
        return std::make_shared<DynamicInvocationResolveResult>(
            std::move(target), DynamicInvocationType::Invocation,
            AddArgumentNamesIfNecessary(arguments, argumentNames));
    }

    // The C# `arguments.Any(a => a.Type.Kind == TypeKind.Dynamic)`.
    bool isDynamic = std::any_of(arguments.begin(), arguments.end(),
                                 [](const std::shared_ptr<ResolveResult>& a) {
                                     return a->Type().Kind() == TypeKind::Dynamic;
                                 });
    std::shared_ptr<MethodGroupResolveResult> mgrr =
        std::dynamic_pointer_cast<MethodGroupResolveResult>(target);
    if (mgrr) {
        if (isDynamic) {
            // If we have dynamic arguments, we need to represent the invocation as a
            // dynamic invocation if there is more than one applicable method.
            std::unique_ptr<OverloadResolution> or2 = CreateOverloadResolution(
                arguments, argumentNames, mgrr->TypeArguments());
            // The C# `mgrr.MethodsGroupedByDeclaringType.SelectMany(m => m, (x, m) =>
            // new { x.DeclaringType, Method = m }).Where(x => OverloadResolution.
            // IsApplicable(or2.AddCandidate(x.Method))).ToList()` -- the flattened
            // (declaringType, method) pairs filtered by applicability. The C#
            // `or2.AddCandidate(x.Method)` mutates or2's best-candidate state as a side
            // effect; only the returned error mask is consumed (the throwaway
            // resolution is faithful -- the C# discards or2 the same way).
            struct ApplicableMethod {
                const IType* declaringType;
                const IMethod* method;
            };
            std::vector<ApplicableMethod> applicableMethods;
            for (const MethodListWithDeclaringType& list :
                 mgrr->MethodsGroupedByDeclaringType()) {
                for (const auto* member : list) {
                    const IMethod* method = static_cast<const IMethod*>(member);
                    if (IsApplicable(or2->AddCandidate(*method)))
                        applicableMethods.push_back({ &list.DeclaringType(), method });
                }
            }

            if (applicableMethods.size() > 1) {
                // The C# `applicableMethods.All(x => x.Method.IsStatic) && !(mgrr.
                // TargetResult is TypeResolveResult)`.
                bool allStatic = std::all_of(
                    applicableMethods.begin(), applicableMethods.end(),
                    [](const ApplicableMethod& x) { return x.method->IsStatic(); });
                std::shared_ptr<ResolveResult> actualTarget;
                if (allStatic
                    && dynamic_cast<const TypeResolveResult*>(mgrr->TargetResult())
                           == nullptr) {
                    actualTarget = std::make_shared<TypeResolveResult>(
                        std::const_pointer_cast<IType>(
                            mgrr->TargetType().shared_from_this()));
                } else {
                    // The C# `actualTarget = mgrr.TargetResult` -- the aliasing handle
                    // co-owns the method group (keeping the target result alive),
                    // pointing at the target result (the LiftedUserDefinedOperator
                    // convention).
                    actualTarget =
                        std::shared_ptr<ResolveResult>(mgrr, mgrr->TargetResult());
                }

                std::vector<MethodListWithDeclaringType> l;
                for (const ApplicableMethod& m : applicableMethods) {
                    // The C# `l[l.Count - 1].DeclaringType != m.DeclaringType` is a
                    // REFERENCE comparison -- the port compares the addresses.
                    if (l.empty() || &l.back().DeclaringType() != m.declaringType)
                        l.emplace_back(
                            std::const_pointer_cast<IType>(
                                m.declaringType->shared_from_this()));
                    l.back().push_back(m.method);
                }
                return std::make_shared<DynamicInvocationResolveResult>(
                    std::make_shared<MethodGroupResolveResult>(
                        std::move(actualTarget), mgrr->MethodName(), std::move(l),
                        mgrr->TypeArguments()),
                    DynamicInvocationType::Invocation,
                    AddArgumentNamesIfNecessary(arguments, argumentNames));
            }
        }

        // The C# `mgrr.PerformOverloadResolution(compilation, arguments, argumentNames,
        // checkForOverflow: checkForOverflow, conversions: conversions,
        // allowOptionalParameters: allowOptionalParameters)` -- the other flags keep
        // their C# defaults (allowExtensionMethods/allowExpandingParams/allowImplicitIn
        // all true).
        std::unique_ptr<OverloadResolution> orr = mgrr->PerformOverloadResolution(
            compilation_, arguments, argumentNames,
            /*allowExtensionMethods*/ true, /*allowExpandingParams*/ true,
            allowOptionalParameters, /*allowImplicitIn*/ true, CheckForOverflow(),
            &Conversions());
        if (orr->BestCandidate() != nullptr) {
            // The C# `returnTypeOverride: isDynamic ? SpecialType.Dynamic : null` (the
            // D469 DynamicMemberResolveResult precedent for the SpecialType.Dynamic
            // construction).
            ITypePtr returnTypeOverride =
                isDynamic
                    ? std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType*/ true)
                    : nullptr;
            // The C# `or.BestCandidate.IsStatic && !or.IsExtensionMethodInvocation &&
            // !(mgrr.TargetResult is TypeResolveResult)` -- a static non-extension
            // invocation over a VALUE target re-targets to the type itself.
            if (orr->BestCandidate()->IsStatic() && !orr->IsExtensionMethodInvocation()
                && dynamic_cast<const TypeResolveResult*>(mgrr->TargetResult()) == nullptr) {
                return orr->CreateResolveResult(
                    std::make_shared<TypeResolveResult>(
                        std::const_pointer_cast<IType>(
                            mgrr->TargetType().shared_from_this())),
                    /*initializerStatements*/ {}, std::move(returnTypeOverride));
            } else {
                // The C# `or.CreateResolveResult(mgrr.TargetResult, ...)` -- the aliasing
                // handle co-owns the method group (the LiftedUserDefinedOperator
                // convention).
                return orr->CreateResolveResult(
                    std::shared_ptr<ResolveResult>(mgrr, mgrr->TargetResult()),
                    /*initializerStatements*/ {}, std::move(returnTypeOverride));
            }
        } else {
            // No candidate found at all (not even an inapplicable one).
            // This can happen with empty method groups (as sometimes used with
            // extension methods)
            return std::make_shared<UnknownMethodResolveResult>(
                std::const_pointer_cast<IType>(mgrr->TargetType().shared_from_this()),
                mgrr->MethodName(), mgrr->TypeArguments(),
                CreateParameters(arguments, argumentNames));
        }
    }
    const UnknownMemberResolveResult* umrr =
        dynamic_cast<const UnknownMemberResolveResult*>(target.get());
    if (umrr != nullptr) {
        return std::make_shared<UnknownMethodResolveResult>(
            std::const_pointer_cast<IType>(umrr->TargetType().shared_from_this()),
            umrr->MemberName(), umrr->TypeArguments(),
            CreateParameters(arguments, argumentNames));
    }
    const UnknownIdentifierResolveResult* uirr =
        dynamic_cast<const UnknownIdentifierResolveResult*>(target.get());
    if (uirr != nullptr && CurrentTypeDefinition() != nullptr) {
        // The C# `new UnknownMethodResolveResult(CurrentTypeDefinition, uirr.Identifier,
        // EmptyList<IType>.Instance, ...)` -- the definition's owning handle is
        // recovered via `shared_from_this` + `const_pointer_cast` (the D529
        // convention; the upcast to `ITypePtr` is implicit).
        return std::make_shared<UnknownMethodResolveResult>(
            std::const_pointer_cast<IType>(CurrentTypeDefinition()->shared_from_this()),
            uirr->Identifier(), std::vector<ITypePtr>{},
            CreateParameters(arguments, argumentNames));
    }
    const IMethod* invokeMethod = GetDelegateInvokeMethod(target->Type());
    if (invokeMethod != nullptr) {
        std::unique_ptr<OverloadResolution> orr =
            CreateOverloadResolution(arguments, argumentNames);
        orr->AddCandidate(*invokeMethod);
        // The C# named-argument construction `new CSharpInvocationResolveResult(target,
        // invokeMethod, or.GetArgumentsWithConversionsAndNames(), or.
        // BestCandidateErrors, isExpandedForm: or.BestCandidateIsExpandedForm,
        // isDelegateInvocation: true, argumentToParameterMap: or.
        // GetArgumentToParameterMap(), returnTypeOverride: isDynamic ? SpecialType.
        // Dynamic : null)` -- the unnamed C#-default parameters (isExtensionMethod
        // Invocation: false, initializerStatements: null) stay at the port ctor's
        // defaults.
        return std::make_shared<CSharpInvocationResolveResult>(
            std::move(target), invokeMethod, orr->GetArgumentsWithConversionsAndNames(),
            orr->BestCandidateErrors(),
            /*isExtensionMethodInvocation*/ false,
            /*isExpandedForm*/ orr->BestCandidateIsExpandedForm(),
            /*isDelegateInvocation*/ true, orr->GetArgumentToParameterMap(),
            /*initializerStatements*/ std::vector<std::shared_ptr<
                ILSpy::Decompiler::Semantics::ResolveResult>>{},
            isDynamic
                ? std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType*/ true)
                : nullptr);
    }
    return ErrorResultSingleton();
}

// The C# `List<IParameter> CreateParameters(ResolveResult[] arguments, string[]
// argumentNames)` (line 2348) -- see CSharpResolver.hpp for the port conventions.
std::vector<std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>>
CSharpResolver::CreateParameters(
    const std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments,
    const std::optional<std::vector<std::string>>& argumentNames) const
{
    using ILSpy::Decompiler::Semantics::ByReferenceResolveResult;
    using ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter;
    using ILSpy::Decompiler::TypeSystem::IParameter;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    std::vector<std::shared_ptr<const IParameter>> list;
    // The C# mutates a LOCAL copy of argumentNames (null -> a fresh all-null array;
    // non-null -> a clone): the port normalizes into a local vector where a null ENTRY
    // is the empty string (the GetArgumentsWithConversions normalization).
    std::vector<std::string> names;
    if (!argumentNames.has_value()) {
        names.assign(arguments.size(), std::string{});
    } else {
        // The C# `if (argumentNames.Length != arguments.Length) throw new
        // ArgumentException()`.
        if (argumentNames->size() != arguments.size())
            throw std::invalid_argument(
                "CSharpResolver::CreateParameters: argumentNames length mismatch");
        names = *argumentNames;
    }
    for (size_t i = 0; i < arguments.size(); i++) {
        // invent argument names where necessary:
        if (names[i].empty()) {
            std::string newArgumentName = GuessParameterName(*arguments[i]);
            // The C# `argumentNames.Contains(newArgumentName)` (the local copy -- the
            // already-invented names and the given entries alike).
            if (std::find(names.begin(), names.end(), newArgumentName) != names.end()) {
                // disambiguate argument name (e.g. add a number)
                int num = 1;
                std::string newName;
                do {
                    newName = newArgumentName + std::to_string(num);
                    num++;
                } while (std::find(names.begin(), names.end(), newName) != names.end());
                newArgumentName = newName;
            }
            names[i] = newArgumentName;
        }

        // create the parameter:
        const ByReferenceResolveResult* brrr =
            dynamic_cast<const ByReferenceResolveResult*>(arguments[i].get());
        if (brrr != nullptr) {
            // The C# `new DefaultParameter(arguments[i].Type, argumentNames[i],
            // referenceKind: brrr.ReferenceKind)`.
            list.push_back(std::make_shared<DefaultParameter>(
                arguments[i]->TypePtr(), names[i], /*owner*/ nullptr,
                /*attributes*/ std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*>{},
                brrr->ReferenceKind()));
        } else {
            // argument might be a lambda or delegate type, so we have to try to guess
            // the delegate type
            const IType& type = arguments[i]->Type();
            if (type.Kind() == TypeKind::Null || type.Kind() == TypeKind::None) {
                // The C# `new DefaultParameter(compilation.FindType(KnownTypeCode.
                // Object), argumentNames[i])` -- the owning handle is recovered via
                // `shared_from_this` + `const_pointer_cast` (the D529 convention; the
                // registered known types are shared-managed).
                list.push_back(std::make_shared<DefaultParameter>(
                    std::const_pointer_cast<IType>(
                        compilation_.FindType(KnownTypeCode::Object).shared_from_this()),
                    names[i]));
            } else {
                list.push_back(std::make_shared<DefaultParameter>(arguments[i]->TypePtr(),
                                                                   names[i]));
            }
        }
    }
    return list;
}

// The C# `static string GuessParameterName(ResolveResult rr)` (line 2398) -- see
// CSharpResolver.hpp for the port conventions.
std::string CSharpResolver::GuessParameterName(
    const ILSpy::Decompiler::Semantics::ResolveResult& rr)
{
    using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
    using ILSpy::Decompiler::Semantics::LocalResolveResult;
    using ILSpy::Decompiler::Semantics::MemberResolveResult;
    using ILSpy::Decompiler::Semantics::UnknownMemberResolveResult;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    const MemberResolveResult* mrr = dynamic_cast<const MemberResolveResult*>(&rr);
    if (mrr != nullptr)
        return mrr->Member()->Name();

    const UnknownMemberResolveResult* umrr =
        dynamic_cast<const UnknownMemberResolveResult*>(&rr);
    if (umrr != nullptr)
        return umrr->MemberName();

    const MethodGroupResolveResult* mgrr =
        dynamic_cast<const MethodGroupResolveResult*>(&rr);
    if (mgrr != nullptr)
        return mgrr->MethodName();

    const LocalResolveResult* vrr = dynamic_cast<const LocalResolveResult*>(&rr);
    if (vrr != nullptr)
        return MakeParameterName(vrr->Variable()->Name());

    // The C# `rr.Type.Kind != TypeKind.Unknown && !string.IsNullOrEmpty(rr.Type.Name)`.
    if (rr.Type().Kind() != TypeKind::Unknown && !rr.Type().Name().empty()) {
        return MakeParameterName(rr.Type().Name());
    } else {
        return "parameter";
    }
}

// The C# `static string MakeParameterName(string variableName)` (line 2426) -- see
// CSharpResolver.hpp for the port conventions.
std::string CSharpResolver::MakeParameterName(const std::string& variableName)
{
    // The C# `if (string.IsNullOrEmpty(variableName)) return "parameter";`.
    if (variableName.empty())
        return "parameter";
    std::string result = variableName;
    // The C# `if (variableName.Length > 1 && variableName[0] == '_') variableName =
    // variableName.Substring(1);`.
    if (result.size() > 1 && result[0] == '_')
        result = result.substr(1);
    // The C# `char.ToLower(variableName[0])` -- the invariant ASCII lower-casing (the
    // en-US convention).
    result[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(result[0])));
    return result;
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

// ---- ResolveUnaryOperator region (CSharpResolver.cs lines 326-530) ------------------------

// The C# `public ResolveResult ResolveUnaryOperator(UnaryOperatorType op, ResolveResult
// expression)` (line 326, C# spec draft-v11 section 12.4.4 "Unary operator overload
// resolution") -- see CSharpResolver.hpp for the full port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveUnaryOperator(
    ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType op,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const
{
    using ILSpy::Decompiler::CSharp::Resolver::AwaitResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationType;
    using ILSpy::Decompiler::CSharp::Resolver::DynamicMemberResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::UnaryOperatorMethod;
    using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorExpression;
    using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
    using ILSpy::Decompiler::Semantics::ConstantResolveResult;
    using ILSpy::Decompiler::Semantics::Conversion;
    using ILSpy::Decompiler::Semantics::ErrorResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::FindType;
    using ILSpy::Decompiler::TypeSystem::GetTypeCode;
    using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
    using ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType;
    using ILSpy::Decompiler::TypeSystem::IsNullable;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::PointerType;
    using ILSpy::Decompiler::TypeSystem::SpecialType;
    using ILSpy::Decompiler::TypeSystem::TypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;
    using ILSpy::Decompiler::Util::ArithmeticException;
    using ILSpy::Decompiler::Util::TypeCodeOfBoxedValue;

    if (expression->Type().Kind() == TypeKind::Dynamic)
    {
        if (op == UnaryOperatorType::Await)
        {
            // The C# builds the dynamic `await` shape entirely from dynamic placeholders:
            // `GetAwaiter` as a `DynamicMemberResolveResult`, its invocation as a
            // `DynamicInvocationResolveResult`, and every awaiter-pattern member null (a
            // dynamic await skips the member-presence check, the `AwaitResolveResult
            // .IsError` dynamic short-circuit).
            return std::make_shared<AwaitResolveResult>(
                std::make_shared<SpecialType>(TypeKind::Dynamic, true),
                std::make_shared<DynamicInvocationResolveResult>(
                    std::make_shared<DynamicMemberResolveResult>(std::move(expression),
                                                                 "GetAwaiter"),
                    DynamicInvocationType::Invocation),
                std::make_shared<SpecialType>(TypeKind::Dynamic, true),
                /*isCompletedProperty=*/nullptr,
                /*onCompletedMethod=*/nullptr,
                /*getResultMethod=*/nullptr);
        }
        else
        {
            return UnaryOperatorResolveResult(
                *std::make_shared<SpecialType>(TypeKind::Dynamic, true), op,
                std::move(expression));
        }
    }

    // C# spec (draft-v11): section 12.4.4 Unary operator overload resolution
    const char* overloadableOperatorName = GetOverloadableOperatorName(op);
    if (overloadableOperatorName == nullptr)
    {
        switch (op)
        {
            case UnaryOperatorType::Dereference:
            {
                // The C# `expression.Type as PointerType`; the null-element guard is the
                // documented safe fallback for the degenerate shape (a real metadata
                // `PointerType` always carries its element -- the C# NREs on
                // `p.ElementType` there).
                const PointerType* p = dynamic_cast<const PointerType*>(&expression->Type());
                if (p != nullptr && p->Element() != nullptr)
                    return UnaryOperatorResolveResult(*p->Element(), op, std::move(expression));
                else
                    return ErrorResultSingleton();
            }
            case UnaryOperatorType::AddressOf:
            {
                // The C# `new PointerType(expression.Type)` -- a FRESH shared-managed
                // instance (the result factory's `shared_from_this` needs the shared
                // ownership; the operand handle is recovered via the D529 convention).
                ITypePtr operandType = std::const_pointer_cast<IType>(
                    const_cast<IType&>(expression->Type()).shared_from_this());
                ITypePtr addressType = std::make_shared<PointerType>(std::move(operandType));
                return UnaryOperatorResolveResult(*addressType, op, std::move(expression));
            }
            case UnaryOperatorType::Await:
                // The C# arm (lines 353-389) computes a chain of `ResolveMemberAccess` /
                // `ResolveInvocation` / `CreateMemberLookup` results and then
                // UNCONDITIONALLY throws `NotImplementedException` (line 396) -- the
                // computed values are discarded and the lookups have no side effects, so
                // the port defers the dead pre-throw work and throws directly (the
                // C# comment calls the arm "dead code for ILSpy anyways").
                throw std::logic_error(
                    "NotImplementedException: CSharpResolver.ResolveUnaryOperator(await)");
            default:
                // The C# `return ErrorResolveResult.UnknownError`.
                return ErrorResultSingleton();
        }
    }

    // If the type is nullable, get the underlying type:
    // (A LOCAL, rebound by `UnaryNumericPromotion(op, ref type, ...)` -- threaded as a
    // rebindable pointer, the `UnaryNumericPromotion` `const IType*&` signature.)
    const IType* type = &GetUnderlyingType(expression->Type());
    bool isNullable = IsNullable(expression->Type());

    // the operator is overloadable:
    std::unique_ptr<OverloadResolution> userDefinedOperatorOR =
        CreateOverloadResolution({expression});
    for (const auto& candidate :
         GetUserDefinedOperatorCandidates(*type, overloadableOperatorName))
    {
        userDefinedOperatorOR->AddCandidate(*candidate);
    }
    if (userDefinedOperatorOR->FoundApplicableCandidate())
    {
        return CreateResolveResultForUserDefinedOperator(
            *userDefinedOperatorOR,
            UnaryOperatorExpression::GetLinqNodeType(op, checkForOverflow_));
    }

    // (The result-type reference is bound BEFORE the operand move: the C# reads
    // `expression.Type` at the call, but the port's unspecified argument-evaluation order
    // may construct the moved `shared_ptr` parameter first -- dereferencing an
    // already-moved-out null handle; the pre-bind avoids that (the moved handle transfers
    // ownership without destroying the pointee, so the pre-bound reference stays valid).)
    expression = UnaryNumericPromotion(op, type, isNullable, std::move(expression));
    const std::vector<std::shared_ptr<ILSpy::Decompiler::CSharp::Resolver::OperatorMethod>>*
        methodGroup;
    ILSpy::Decompiler::CSharp::Resolver::CSharpOperators& operators =
        CSharpOperators::Get(compilation_);
    switch (op)
    {
        case UnaryOperatorType::Increment:
        case UnaryOperatorType::Decrement:
        case UnaryOperatorType::PostIncrement:
        case UnaryOperatorType::PostDecrement:
        {
            // C# spec (draft-v11): section 12.8.16 Postfix increment and decrement
            // operators; section 12.9.7 Prefix increment and decrement operators.
            TypeCode code = GetTypeCode(*type);
            // The C# enum relational comparisons port through static_cast<int> (the D514
            // convention -- the closed [Char..Decimal] range of the incrementable operand
            // codes).
            if ((static_cast<int>(code) >= static_cast<int>(TypeCode::Char)
                 && static_cast<int>(code) <= static_cast<int>(TypeCode::Decimal))
                || type->Kind() == TypeKind::Enum
                || type->Kind() == TypeKind::Pointer
                || IsCSharpNativeIntegerType(type))
            {
                const IType& expressionType = expression->Type();
                return UnaryOperatorResolveResult(expressionType, op, std::move(expression),
                                                 isNullable);
            }
            else
            {
                const IType& expressionType = expression->Type();
                return std::make_shared<ErrorResolveResult>(
                    std::const_pointer_cast<IType>(expressionType.shared_from_this()));
            }
        }
        case UnaryOperatorType::Plus:
            if (IsCSharpNativeIntegerType(type))
            {
                const IType& expressionType = expression->Type();
                return UnaryOperatorResolveResult(expressionType, op, std::move(expression),
                                                 isNullable);
            }
            methodGroup = &operators.UnaryPlusOperators();
            break;
        case UnaryOperatorType::Minus:
            if (IsCSharpNativeIntegerType(type))
            {
                const IType& expressionType = expression->Type();
                return UnaryOperatorResolveResult(expressionType, op, std::move(expression),
                                                 isNullable);
            }
            // The C# `CheckForOverflow ? CheckedUnaryMinusOperators
            // : UncheckedUnaryMinusOperators` -- the checked/unchecked table selection.
            methodGroup = checkForOverflow_ ? &operators.CheckedUnaryMinusOperators()
                                           : &operators.UncheckedUnaryMinusOperators();
            break;
        case UnaryOperatorType::Not:
            methodGroup = &operators.LogicalNegationOperators();
            break;
        case UnaryOperatorType::BitNot:
            if (type->Kind() == TypeKind::Enum)
            {
                std::any constantValue = expression->ConstantValue();
                if (expression->IsCompileTimeConstant() && !isNullable
                    && constantValue.has_value())
                {
                    // evaluate as (E)(~(U)x);
                    // The C# `compilation.FindType(expression.ConstantValue.GetType())`:
                    // an enum constant holds its UNDERLYING primitive value, so the boxed
                    // value's runtime type resolves through its `TypeCode` (the
                    // TypeCode-based `FindType`, ReflectionHelper.cs line 106).
                    const IType& U =
                        FindType(compilation_, TypeCodeOfBoxedValue(constantValue));
                    auto unpackedEnum = std::make_shared<ConstantResolveResult>(
                        std::const_pointer_cast<IType>(U.shared_from_this()), constantValue);
                    auto rr = ResolveUnaryOperator(op, std::move(unpackedEnum));
                    // The C# `WithCheckForOverflow(false).ResolveCast(type, rr)` -- the
                    // `const_cast` feeds the const `type` local to the non-const
                    // `ResolveCast` parameter (the D515 convention).
                    rr = WithCheckForOverflow(false)->ResolveCast(
                        const_cast<IType&>(*type), std::move(rr));
                    if (rr->IsCompileTimeConstant())
                        return rr;
                }
                const IType& expressionType = expression->Type();
                return UnaryOperatorResolveResult(expressionType, op, std::move(expression),
                                                 isNullable);
            }
            else if (IsCSharpNativeIntegerType(type))
            {
                const IType& expressionType = expression->Type();
                return UnaryOperatorResolveResult(expressionType, op, std::move(expression),
                                                 isNullable);
            }
            else
            {
                methodGroup = &operators.BitwiseComplementOperators();
                break;
            }
        default:
            // The C# `throw new InvalidOperationException()` -- unreachable through the
            // overloadable-name gate (the name covers exactly these operator kinds), the
            // runtime-exception convention.
            throw std::runtime_error(
                "InvalidOperationException: ResolveUnaryOperator");
    }
    std::unique_ptr<OverloadResolution> builtinOperatorOR =
        CreateOverloadResolution({expression});
    for (const auto& candidate : *methodGroup)
    {
        builtinOperatorOR->AddCandidate(*candidate);
    }
    // The C# hard cast `(CSharpOperators.UnaryOperatorMethod)builtinOperatorOR
    // .BestCandidate` -- every builtin-table entry IS a `UnaryOperatorMethod` and the
    // first `AddCandidate` always folds a best, so the cast cannot fail through this
    // call site; the null fallback is the documented safe fallback (the C# NREs on
    // `m.ReturnType` for the impossible shape).
    const UnaryOperatorMethod* m =
        dynamic_cast<const UnaryOperatorMethod*>(builtinOperatorOR->BestCandidate());
    if (m == nullptr)
    {
        const IType& expressionType = expression->Type();
        return std::make_shared<ErrorResolveResult>(
            std::const_pointer_cast<IType>(expressionType.shared_from_this()));
    }
    const IType& resultType = m->ReturnType();
    if (builtinOperatorOR->BestCandidateErrors() != OverloadResolutionErrors::None)
    {
        if (userDefinedOperatorOR->BestCandidate() != nullptr)
        {
            // If there are any user-defined operators, prefer those over the built-in
            // operators. It'll be a more informative error.
            return CreateResolveResultForUserDefinedOperator(
                *userDefinedOperatorOR,
                UnaryOperatorExpression::GetLinqNodeType(op, checkForOverflow_));
        }
        else if (builtinOperatorOR->BestCandidateAmbiguousWith() != nullptr)
        {
            // If the best candidate is ambiguous, just use the input type instead
            // of picking one of the ambiguous overloads.
            const IType& expressionType = expression->Type();
            return std::make_shared<ErrorResolveResult>(
                std::const_pointer_cast<IType>(expressionType.shared_from_this()));
        }
        else
        {
            return std::make_shared<ErrorResolveResult>(
                std::const_pointer_cast<IType>(resultType.shared_from_this()));
        }
    }
    else if (expression->IsCompileTimeConstant() && m->CanEvaluateAtCompileTime())
    {
        std::any val;
        try
        {
            val = m->Invoke(*this, expression->ConstantValue());
        }
        catch (const ArithmeticException&)
        {
            return std::make_shared<ErrorResolveResult>(
                std::const_pointer_cast<IType>(resultType.shared_from_this()));
        }
        return std::make_shared<ConstantResolveResult>(
            std::const_pointer_cast<IType>(resultType.shared_from_this()), std::move(val));
    }
    else
    {
        expression = Convert(
            std::move(expression), const_cast<IType&>(m->Parameters()[0]->Type()),
            builtinOperatorOR->ArgumentConversions()[0]);
        // The C# `builtinOperatorOR.BestCandidate is ILiftedOperator` -- the cross-cast
        // marks the lifted form (the D549 standalone-base cross-cast).
        return UnaryOperatorResolveResult(
            resultType, op, std::move(expression),
            dynamic_cast<const ILiftedOperator*>(builtinOperatorOR->BestCandidate())
                != nullptr);
    }
}

// ---- ResolveBinaryOperator + enum handlers (CSharpResolver.cs lines 594-948 + 995-1053) ----

namespace {
// The member `GetEnumUnderlyingType` result guarded for the degenerate
// definition-bearing-enum-without-underlying shape (a stub whose `EnumUnderlyingType` is
// unset; the C# NREs on the subsequent `elementType.Kind` deref there -- impossible for
// real metadata, where every enum definition carries its underlying primitive). The
// port's documented safe fallback substitutes the `UnknownType` null object (the
// member's own definitionless-type fallback).
const ILSpy::Decompiler::TypeSystem::IType* EnumUnderlyingOrUnknown(
    const ILSpy::Decompiler::TypeSystem::IType* underlying)
{
    if (underlying != nullptr)
        return underlying;
    static const ILSpy::Decompiler::TypeSystem::ITypePtr unknownType =
        ILSpy::Decompiler::TypeSystem::UnknownType();
    return unknownType.get();
}
} // namespace

// The C# `public ResolveResult ResolveBinaryOperator(BinaryOperatorType op, ResolveResult
// lhs, ResolveResult rhs)` (line 594, C# 4.0 spec section 7.3.4 "Binary operator overload
// resolution") -- see CSharpResolver.hpp for the full port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveBinaryOperator(
    ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const
{
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorExpression;
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
    using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
    using ILSpy::Decompiler::CSharp::Resolver::BinaryOperatorMethod;
    using ILSpy::Decompiler::CSharp::Resolver::CSharpOperators;
    using ILSpy::Decompiler::CSharp::Resolver::OperatorMethod;
    using ILSpy::Decompiler::Semantics::ConstantResolveResult;
    using ILSpy::Decompiler::Semantics::ErrorResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::Create;
    using ILSpy::Decompiler::TypeSystem::FindType;
    using ILSpy::Decompiler::TypeSystem::GetUnderlyingType;
    using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
    using ILSpy::Decompiler::TypeSystem::IsCSharpNativeIntegerType;
    using ILSpy::Decompiler::TypeSystem::IsNullable;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using ILSpy::Decompiler::TypeSystem::PointerType;
    using ILSpy::Decompiler::TypeSystem::SpecialType;
    using ILSpy::Decompiler::TypeSystem::TypeKind;
    using ILSpy::Decompiler::Util::ArithmeticException;

    if (lhs->Type().Kind() == TypeKind::Dynamic || rhs->Type().Kind() == TypeKind::Dynamic)
    {
        // The C# `SpecialType.Dynamic` -- a shared-managed instance (the factory's
        // `shared_from_this` recovers the owning handle). The local handle keeps the
        // instance alive through both `Convert` calls and the result factory call.
        ITypePtr dynamicType = std::make_shared<SpecialType>(TypeKind::Dynamic, true);
        lhs = Convert(std::move(lhs), *dynamicType);
        rhs = Convert(std::move(rhs), *dynamicType);
        return BinaryOperatorResolveResult(*dynamicType, std::move(lhs), op,
                                           std::move(rhs));
    }

    // C# 4.0 spec: section 7.3.4 Binary operator overload resolution
    const char* overloadableOperatorName = GetOverloadableOperatorName(op);
    if (overloadableOperatorName == nullptr)
    {
        // Handle logical and/or exactly as bitwise and/or:
        // - If the user overloads a bitwise operator, that implicitly creates the
        //   corresponding logical operator.
        // - If both inputs are compile-time constants, it doesn't matter that we don't
        //   short-circuit.
        // - If inputs aren't compile-time constants, we don't evaluate anything, so again
        //   it doesn't matter that we don't short-circuit
        if (op == BinaryOperatorType::ConditionalAnd)
        {
            overloadableOperatorName =
                GetOverloadableOperatorName(BinaryOperatorType::BitwiseAnd);
        }
        else if (op == BinaryOperatorType::ConditionalOr)
        {
            overloadableOperatorName =
                GetOverloadableOperatorName(BinaryOperatorType::BitwiseOr);
        }
        else if (op == BinaryOperatorType::NullCoalescing)
        {
            // null coalescing operator is not overloadable and needs to be handled
            // separately
            return ResolveNullCoalescingOperator(std::move(lhs), std::move(rhs));
        }
        else
        {
            return ErrorResultSingleton();
        }
    }

    // If the type is nullable, get the underlying type:
    // (LOCAL rebindable pointers -- the shift arm's `UnaryNumericPromotion(..., ref
    // lhsType, ...)` and the post-promotion re-read rebind them, the
    // `UnaryNumericPromotion` `const IType*&` signature.)
    bool isNullable = IsNullable(lhs->Type()) || IsNullable(rhs->Type());
    const IType* lhsType = &GetUnderlyingType(lhs->Type());
    const IType* rhsType = &GetUnderlyingType(rhs->Type());

    // the operator is overloadable:
    // (The C# `HashSet<IParameterizedMember>` + `UnionWith` dedups by the default
    // REFERENCE equality -- the port collects into a pointer vector filtered by
    // pointer identity, the `GetApplicableConversionOperators` dedup convention. Both
    // operand types are scanned: a pair of same-typed operands would otherwise add
    // every candidate twice.)
    std::unique_ptr<OverloadResolution> userDefinedOperatorOR =
        CreateOverloadResolution({lhs, rhs});
    std::vector<const IParameterizedMember*> userOperatorCandidates;
    auto unionWithCandidates = [&](const IType& type) {
        for (const auto& candidate :
             GetUserDefinedOperatorCandidates(type, overloadableOperatorName))
        {
            const IParameterizedMember* member = candidate.get();
            bool found = false;
            for (const IParameterizedMember* existing : userOperatorCandidates)
            {
                if (existing == member)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
                userOperatorCandidates.push_back(member);
        }
    };
    unionWithCandidates(*lhsType);
    unionWithCandidates(*rhsType);
    for (const IParameterizedMember* candidate : userOperatorCandidates)
    {
        userDefinedOperatorOR->AddCandidate(*candidate);
    }
    if (userDefinedOperatorOR->FoundApplicableCandidate())
    {
        return CreateResolveResultForUserDefinedOperator(
            *userDefinedOperatorOR,
            BinaryOperatorExpression::GetLinqNodeType(op, checkForOverflow_));
    }

    // The C# `rhsType.IsReferenceType == false` / `lhsType.IsReferenceType == false` --
    // DEFINITE-false checks (the nullable equality never propagates null: `null ==
    // false` is false, the iteration-103 corrected semantics).
    auto isDefinitelyFalse = [](const IType* type) {
        const std::optional<bool> opt = type->IsReferenceType();
        return opt.has_value() && !*opt;
    };
    if ((lhsType->Kind() == TypeKind::Null && isDefinitelyFalse(rhsType))
        || (isDefinitelyFalse(lhsType) && rhsType->Kind() == TypeKind::Null))
    {
        isNullable = true;
    }
    if (op == BinaryOperatorType::ShiftLeft || op == BinaryOperatorType::ShiftRight
        || op == BinaryOperatorType::UnsignedShiftRight)
    {
        // special case: the shift operators allow "var x = null << null", producing int?.
        if (lhsType->Kind() == TypeKind::Null && rhsType->Kind() == TypeKind::Null)
            isNullable = true;
        // for shift operators, do unary promotion independently on both arguments
        lhs = UnaryNumericPromotion(UnaryOperatorType::Plus, lhsType, isNullable,
                                    std::move(lhs));
        rhs = UnaryNumericPromotion(UnaryOperatorType::Plus, rhsType, isNullable,
                                    std::move(rhs));
    }
    else
    {
        bool allowNullableConstants =
            op == BinaryOperatorType::Equality || op == BinaryOperatorType::InEquality;
        if (!BinaryNumericPromotion(isNullable, lhs, rhs, allowNullableConstants))
        {
            const IType& lhsResultType = lhs->Type();
            return std::make_shared<ErrorResolveResult>(
                std::const_pointer_cast<IType>(lhsResultType.shared_from_this()));
        }
    }
    // re-read underlying types after numeric promotion
    lhsType = &GetUnderlyingType(lhs->Type());
    rhsType = &GetUnderlyingType(rhs->Type());

    const std::vector<std::shared_ptr<OperatorMethod>>* methodGroup = nullptr;
    CSharpOperators& operators = CSharpOperators::Get(compilation_);
    switch (op)
    {
        case BinaryOperatorType::Multiply:
            methodGroup = &operators.MultiplicationOperators();
            break;
        case BinaryOperatorType::Divide:
            methodGroup = &operators.DivisionOperators();
            break;
        case BinaryOperatorType::Modulus:
            methodGroup = &operators.RemainderOperators();
            break;
        case BinaryOperatorType::Add:
        {
            methodGroup = &operators.AdditionOperators();
            if (lhsType->Kind() == TypeKind::Enum)
            {
                // E operator +(E x, U y);
                const IType* enumUnderlying = GetEnumUnderlyingType(*lhsType);
                if (enumUnderlying != nullptr)
                {
                    ITypePtr underlyingType = MakeNullable(*enumUnderlying, isNullable);
                    if (TryConvertEnum(rhs, *underlyingType, isNullable, lhs))
                    {
                        return HandleEnumOperator(isNullable, *lhsType, op,
                                                   std::move(lhs), std::move(rhs));
                    }
                }
            }
            if (rhsType->Kind() == TypeKind::Enum)
            {
                // E operator +(U x, E y);
                const IType* enumUnderlying = GetEnumUnderlyingType(*rhsType);
                if (enumUnderlying != nullptr)
                {
                    ITypePtr underlyingType = MakeNullable(*enumUnderlying, isNullable);
                    if (TryConvertEnum(lhs, *underlyingType, isNullable, rhs))
                    {
                        return HandleEnumOperator(isNullable, *rhsType, op,
                                                   std::move(lhs), std::move(rhs));
                    }
                }
            }

            if (lhsType->Kind() == TypeKind::Delegate
                && TryConvert(rhs, const_cast<IType&>(*lhsType)))
            {
                return BinaryOperatorResolveResult(*lhsType, std::move(lhs), op,
                                                   std::move(rhs));
            }
            else if (rhsType->Kind() == TypeKind::Delegate
                     && TryConvert(lhs, const_cast<IType&>(*rhsType)))
            {
                return BinaryOperatorResolveResult(*rhsType, std::move(lhs), op,
                                                   std::move(rhs));
            }

            if (lhsType->Kind() == TypeKind::Null && rhsType->Kind() == TypeKind::Null)
            {
                // The C# `new ErrorResolveResult(SpecialType.NullType)` -- the
                // shared-managed `SpecialType(TypeKind::Null, isReferenceType: true)`
                // singleton shape.
                return std::make_shared<ErrorResolveResult>(
                    std::make_shared<SpecialType>(TypeKind::Null, true));
            }
            break;
        }
        case BinaryOperatorType::Subtract:
        {
            methodGroup = &operators.SubtractionOperators();
            if (lhsType->Kind() == TypeKind::Enum)
            {
                // U operator -(E x, E y);
                // (The target is the ORIGINAL `lhs.Type`, not the stripped `lhsType` --
                // a `Nullable<E>` lhs targets its enum form; the constant-0-to-enum
                // conversion is REJECTED here (`allowConversionFromConstantZero:
                // false`, the user-defined-operator comparison context).)
                if (TryConvertEnum(rhs, const_cast<IType&>(lhs->Type()), isNullable, lhs,
                                   /*allowConversionFromConstantZero=*/false))
                {
                    return HandleEnumSubtraction(isNullable, *lhsType, std::move(lhs),
                                                 std::move(rhs));
                }

                // E operator -(E x, U y);
                const IType* enumUnderlying = GetEnumUnderlyingType(*lhsType);
                if (enumUnderlying != nullptr)
                {
                    ITypePtr underlyingType = MakeNullable(*enumUnderlying, isNullable);
                    if (TryConvertEnum(rhs, *underlyingType, isNullable, lhs))
                    {
                        return HandleEnumOperator(isNullable, *lhsType, op,
                                                   std::move(lhs), std::move(rhs));
                    }
                }
            }
            if (rhsType->Kind() == TypeKind::Enum)
            {
                // U operator -(E x, E y);
                if (TryConvertEnum(lhs, const_cast<IType&>(rhs->Type()), isNullable, rhs,
                                   /*allowConversionFromConstantZero=*/false))
                {
                    return HandleEnumSubtraction(isNullable, *rhsType, std::move(lhs),
                                                 std::move(rhs));
                }

                // E operator -(U x, E y);
                const IType* enumUnderlying = GetEnumUnderlyingType(*rhsType);
                if (enumUnderlying != nullptr)
                {
                    ITypePtr underlyingType = MakeNullable(*enumUnderlying, isNullable);
                    if (TryConvertEnum(lhs, *underlyingType, isNullable, rhs))
                    {
                        return HandleEnumOperator(isNullable, *rhsType, op,
                                                   std::move(lhs), std::move(rhs));
                    }
                }
            }

            if (lhsType->Kind() == TypeKind::Delegate
                && TryConvert(rhs, const_cast<IType&>(*lhsType)))
            {
                return BinaryOperatorResolveResult(*lhsType, std::move(lhs), op,
                                                   std::move(rhs));
            }
            else if (rhsType->Kind() == TypeKind::Delegate
                     && TryConvert(lhs, const_cast<IType&>(*rhsType)))
            {
                return BinaryOperatorResolveResult(*rhsType, std::move(lhs), op,
                                                   std::move(rhs));
            }

            if (lhsType->Kind() == TypeKind::Null && rhsType->Kind() == TypeKind::Null)
            {
                return std::make_shared<ErrorResolveResult>(
                    std::make_shared<SpecialType>(TypeKind::Null, true));
            }
            break;
        }
        case BinaryOperatorType::ShiftLeft:
            methodGroup = &operators.ShiftLeftOperators();
            break;
        case BinaryOperatorType::ShiftRight:
            methodGroup = &operators.ShiftRightOperators();
            break;
        case BinaryOperatorType::UnsignedShiftRight:
            methodGroup = &operators.UnsignedShiftRightOperators();
            break;
        case BinaryOperatorType::Equality:
        case BinaryOperatorType::InEquality:
        case BinaryOperatorType::LessThan:
        case BinaryOperatorType::GreaterThan:
        case BinaryOperatorType::LessThanOrEqual:
        case BinaryOperatorType::GreaterThanOrEqual:
        {
            if (lhsType->Kind() == TypeKind::Enum
                && TryConvert(rhs, const_cast<IType&>(lhs->Type())))
            {
                // bool operator op(E x, E y);
                return HandleEnumComparison(op, *lhsType, isNullable, std::move(lhs),
                                            std::move(rhs));
            }
            else if (rhsType->Kind() == TypeKind::Enum
                     && TryConvert(lhs, const_cast<IType&>(rhs->Type())))
            {
                // bool operator op(E x, E y);
                return HandleEnumComparison(op, *rhsType, isNullable, std::move(lhs),
                                            std::move(rhs));
            }
            else if (dynamic_cast<const PointerType*>(lhsType) != nullptr
                     && dynamic_cast<const PointerType*>(rhsType) != nullptr)
            {
                return BinaryOperatorResolveResult(
                    compilation_.FindType(KnownTypeCode::Boolean), std::move(lhs),
                    op, std::move(rhs));
            }
            else if (IsCSharpNativeIntegerType(lhsType)
                     || IsCSharpNativeIntegerType(rhsType))
            {
                if (lhsType->Equals(*rhsType))
                    return BinaryOperatorResolveResult(
                        compilation_.FindType(KnownTypeCode::Boolean), std::move(lhs),
                        op, std::move(rhs), /*isLifted=*/isNullable);
                else
                    return std::make_shared<ErrorResolveResult>(
                        std::const_pointer_cast<IType>(
                            compilation_.FindType(KnownTypeCode::Boolean)
                                .shared_from_this()));
            }
            if (op == BinaryOperatorType::Equality
                || op == BinaryOperatorType::InEquality)
            {
                // The C# `lhsType.IsReferenceType == true && rhsType.IsReferenceType ==
                // true` -- DEFINITE-true checks on both sides, and the non-null kinds
                // (a null literal never participates in the reference comparison).
                auto isDefinitelyTrue = [](const IType* type) {
                    const std::optional<bool> opt = type->IsReferenceType();
                    return opt.has_value() && *opt;
                };
                if (isDefinitelyTrue(lhsType) && isDefinitelyTrue(rhsType)
                    && lhsType->Kind() != TypeKind::Null
                    && rhsType->Kind() != TypeKind::Null
                    && (Detail::IdentityConversion(const_cast<IType&>(*lhsType),
                                                   const_cast<IType&>(*rhsType))
                        || conversions_.ExplicitConversion(
                               const_cast<IType&>(*lhsType),
                               const_cast<IType&>(*rhsType))->IsReferenceConversion()
                        || conversions_.ExplicitConversion(
                               const_cast<IType&>(*rhsType),
                               const_cast<IType&>(*lhsType))->IsReferenceConversion()))
                {
                    // If it's a reference comparison
                    // (The `break` binds to the OUTER switch -- the inner switch below
                    // is not yet entered at this lexical point, mirroring the C#.)
                    if (op == BinaryOperatorType::Equality)
                        methodGroup = &operators.ReferenceEqualityOperators();
                    else
                        methodGroup = &operators.ReferenceInequalityOperators();
                    break;
                }
                else if ((lhsType->Kind() == TypeKind::Null
                          && IsNullableTypeOrNonValueType(rhs->Type()))
                         || (IsNullableTypeOrNonValueType(lhs->Type())
                             && rhsType->Kind() == TypeKind::Null))
                {
                    // compare type parameter or nullable type with the null literal
                    return BinaryOperatorResolveResult(
                        compilation_.FindType(KnownTypeCode::Boolean), std::move(lhs),
                        op, std::move(rhs));
                }
            }
            switch (op)
            {
                case BinaryOperatorType::Equality:
                    methodGroup = &operators.ValueEqualityOperators();
                    break;
                case BinaryOperatorType::InEquality:
                    methodGroup = &operators.ValueInequalityOperators();
                    break;
                case BinaryOperatorType::LessThan:
                    methodGroup = &operators.LessThanOperators();
                    break;
                case BinaryOperatorType::GreaterThan:
                    methodGroup = &operators.GreaterThanOperators();
                    break;
                case BinaryOperatorType::LessThanOrEqual:
                    methodGroup = &operators.LessThanOrEqualOperators();
                    break;
                case BinaryOperatorType::GreaterThanOrEqual:
                    methodGroup = &operators.GreaterThanOrEqualOperators();
                    break;
                default:
                    // The C# `throw new InvalidOperationException()` -- unreachable
                    // (the outer case labels cover exactly these six kinds).
                    throw std::runtime_error(
                        "InvalidOperationException: ResolveBinaryOperator comparison arm");
            }
            break;
        }
        case BinaryOperatorType::BitwiseAnd:
        case BinaryOperatorType::BitwiseOr:
        case BinaryOperatorType::ExclusiveOr:
        {
            if (lhsType->Kind() == TypeKind::Enum)
            {
                // bool operator op(E x, E y);
                if (TryConvertEnum(rhs, const_cast<IType&>(lhs->Type()), isNullable, lhs))
                {
                    return HandleEnumOperator(isNullable, *lhsType, op, std::move(lhs),
                                             std::move(rhs));
                }
            }

            if (rhsType->Kind() == TypeKind::Enum)
            {
                // bool operator op(E x, E y);
                if (TryConvertEnum(lhs, const_cast<IType&>(rhs->Type()), isNullable, rhs))
                {
                    return HandleEnumOperator(isNullable, *rhsType, op, std::move(lhs),
                                             std::move(rhs));
                }
            }

            switch (op)
            {
                case BinaryOperatorType::BitwiseAnd:
                    methodGroup = &operators.BitwiseAndOperators();
                    break;
                case BinaryOperatorType::BitwiseOr:
                    methodGroup = &operators.BitwiseOrOperators();
                    break;
                case BinaryOperatorType::ExclusiveOr:
                    methodGroup = &operators.BitwiseXorOperators();
                    break;
                default:
                    throw std::runtime_error(
                        "InvalidOperationException: ResolveBinaryOperator bitwise arm");
            }
            break;
        }
        case BinaryOperatorType::ConditionalAnd:
            methodGroup = &operators.LogicalAndOperators();
            break;
        case BinaryOperatorType::ConditionalOr:
            methodGroup = &operators.LogicalOrOperators();
            break;
        default:
            // The C# `throw new InvalidOperationException()` -- unreachable through the
            // overloadable-name gate (the name covers exactly these operator kinds),
            // the runtime-exception convention.
            throw std::runtime_error(
                "InvalidOperationException: ResolveBinaryOperator");
    }
    if (IsCSharpNativeIntegerType(lhsType) || IsCSharpNativeIntegerType(rhsType))
    {
        if (lhsType->Equals(*rhsType))
        {
            // (The pre-bind discipline: the `Create` handle keeps the nullable form
            // alive through the factory call; `*lhsType` is valid for the passthrough.)
            const ITypePtr resultTypeHandle =
                isNullable ? Create(compilation_, *lhsType)
                           : std::const_pointer_cast<IType>(
                                 const_cast<IType&>(*lhsType).shared_from_this());
            return BinaryOperatorResolveResult(*resultTypeHandle, std::move(lhs), op,
                                               std::move(rhs), /*isLifted=*/isNullable);
        }
        // mixing nint/nuint is not allowed
        return std::make_shared<ErrorResolveResult>(
            std::const_pointer_cast<IType>(
                const_cast<IType&>(*lhsType).shared_from_this()));
    }
    std::unique_ptr<OverloadResolution> builtinOperatorOR =
        CreateOverloadResolution({lhs, rhs});
    for (const auto& candidate : *methodGroup)
    {
        builtinOperatorOR->AddCandidate(*candidate);
    }
    // The C# hard cast `(CSharpOperators.BinaryOperatorMethod)builtinOperatorOR
    // .BestCandidate` -- every builtin-table entry IS a `BinaryOperatorMethod` and the
    // first `AddCandidate` always folds a best, so the cast cannot fail through this
    // call site; the null fallback is the documented safe fallback (the C# NREs on
    // `m.ReturnType` for the impossible shape).
    const BinaryOperatorMethod* m =
        dynamic_cast<const BinaryOperatorMethod*>(builtinOperatorOR->BestCandidate());
    if (m == nullptr)
    {
        const IType& lhsResultType = lhs->Type();
        return std::make_shared<ErrorResolveResult>(
            std::const_pointer_cast<IType>(lhsResultType.shared_from_this()));
    }
    const IType& resultType = m->ReturnType();
    if (builtinOperatorOR->BestCandidateErrors() != OverloadResolutionErrors::None)
    {
        // If there are any user-defined operators, prefer those over the built-in
        // operators. It'll be a more informative error.
        if (userDefinedOperatorOR->BestCandidate() != nullptr)
        {
            return CreateResolveResultForUserDefinedOperator(
                *userDefinedOperatorOR,
                BinaryOperatorExpression::GetLinqNodeType(op, checkForOverflow_));
        }
        else
        {
            return std::make_shared<ErrorResolveResult>(
                std::const_pointer_cast<IType>(resultType.shared_from_this()));
        }
    }
    else if (lhs->IsCompileTimeConstant() && rhs->IsCompileTimeConstant()
             && m->CanEvaluateAtCompileTime())
    {
        std::any val;
        try
        {
            val = m->Invoke(*this, lhs->ConstantValue(), rhs->ConstantValue());
        }
        catch (const ArithmeticException&)
        {
            return std::make_shared<ErrorResolveResult>(
                std::const_pointer_cast<IType>(resultType.shared_from_this()));
        }
        return std::make_shared<ConstantResolveResult>(
            std::const_pointer_cast<IType>(resultType.shared_from_this()), std::move(val));
    }
    else
    {
        lhs = Convert(
            std::move(lhs), const_cast<IType&>(m->Parameters()[0]->Type()),
            builtinOperatorOR->ArgumentConversions()[0]);
        rhs = Convert(
            std::move(rhs), const_cast<IType&>(m->Parameters()[1]->Type()),
            builtinOperatorOR->ArgumentConversions()[1]);
        // The C# `builtinOperatorOR.BestCandidate is ILiftedOperator` -- the cross-cast
        // marks the lifted form (the D549 standalone-base cross-cast).
        return BinaryOperatorResolveResult(
            resultType, std::move(lhs), op, std::move(rhs),
            dynamic_cast<const ILiftedOperator*>(builtinOperatorOR->BestCandidate())
                != nullptr);
    }
}

// The C# private `ResolveResult HandleEnumComparison(BinaryOperatorType op, IType
// enumType, bool isNullable, ResolveResult lhs, ResolveResult rhs)` (line 995) -- "bool
// operator op(E x, E y)", evaluated as `((U)x op (U)y`. See CSharpResolver.hpp for the
// full port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::HandleEnumComparison(
    ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
    const ILSpy::Decompiler::TypeSystem::IType& enumType, bool isNullable,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const
{
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
    using ILSpy::Decompiler::TypeSystem::FindType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // evaluate as ((U)x op (U)y)
    // (The `ResolveCast` calls take COPIES of the operand handles -- the C# passes the
    // parameters by value and keeps using the originals on the non-folded path; moving
    // here would leave null handles for the fallback `BinaryOperatorResolveResult`.)
    const IType* elementType = EnumUnderlyingOrUnknown(GetEnumUnderlyingType(enumType));
    if (lhs->IsCompileTimeConstant() && rhs->IsCompileTimeConstant() && !isNullable
        && elementType->Kind() != TypeKind::Enum)
    {
        auto rr = ResolveBinaryOperator(
            op,
            ResolveCast(const_cast<IType&>(*elementType), lhs),
            ResolveCast(const_cast<IType&>(*elementType), rhs));
        if (rr->IsCompileTimeConstant())
            return rr;
    }
    const IType& resultType = compilation_.FindType(KnownTypeCode::Boolean);
    return BinaryOperatorResolveResult(resultType, std::move(lhs), op, std::move(rhs),
                                       isNullable);
}

// The C# private `ResolveResult HandleEnumSubtraction(bool isNullable, IType enumType,
// ResolveResult lhs, ResolveResult rhs)` (line 1013) -- "U operator -(E x, E y)",
// evaluated as `(U)((U)x - (U)y)`. See CSharpResolver.hpp for the full port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::HandleEnumSubtraction(
    bool isNullable, const ILSpy::Decompiler::TypeSystem::IType& enumType,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const
{
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // evaluate as (U)((U)x - (U)y)
    // (The `ResolveCast` calls take COPIES of the operand handles -- see
    // `HandleEnumComparison`.)
    const IType* elementType = EnumUnderlyingOrUnknown(GetEnumUnderlyingType(enumType));
    if (lhs->IsCompileTimeConstant() && rhs->IsCompileTimeConstant() && !isNullable
        && elementType->Kind() != TypeKind::Enum)
    {
        auto rr = ResolveBinaryOperator(
            BinaryOperatorType::Subtract,
            ResolveCast(const_cast<IType&>(*elementType), lhs),
            ResolveCast(const_cast<IType&>(*elementType), rhs));
        rr = WithCheckForOverflow(false)->ResolveCast(
            const_cast<IType&>(*elementType), std::move(rr));
        if (rr->IsCompileTimeConstant())
            return rr;
    }
    // (The pre-bind discipline: the `MakeNullable` handle keeps the (nullable)
    // underlying type alive through the factory call.)
    ITypePtr resultType = MakeNullable(*elementType, isNullable);
    return BinaryOperatorResolveResult(*resultType, std::move(lhs),
                                       BinaryOperatorType::Subtract, std::move(rhs),
                                       isNullable);
}

// The C# private `ResolveResult HandleEnumOperator(bool isNullable, IType enumType,
// BinaryOperatorType op, ResolveResult lhs, ResolveResult rhs)` (line 1037) -- "E
// operator +(E x, U y)" / "E operator +(U x, E y)" / "E operator -(E x, U y)" / the enum
// bitwise operators, evaluated as `(E)((U)x op (U)y)`. See CSharpResolver.hpp for the
// full port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::HandleEnumOperator(
    bool isNullable, const ILSpy::Decompiler::TypeSystem::IType& enumType,
    ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType op,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const
{
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // evaluate as (E)((U)x op (U)y)
    // (The `ResolveCast` calls take COPIES of the operand handles -- see
    // `HandleEnumComparison`.)
    if (lhs->IsCompileTimeConstant() && rhs->IsCompileTimeConstant() && !isNullable)
    {
        const IType* elementType = EnumUnderlyingOrUnknown(
            GetEnumUnderlyingType(enumType));
        if (elementType->Kind() != TypeKind::Enum)
        {
            auto rr = ResolveBinaryOperator(
                op,
                ResolveCast(const_cast<IType&>(*elementType), lhs),
                ResolveCast(const_cast<IType&>(*elementType), rhs));
            rr = WithCheckForOverflow(false)->ResolveCast(
                const_cast<IType&>(enumType), std::move(rr));
            if (rr->IsCompileTimeConstant())
            {
                // only report result if it's a constant; use the regular
                // OperatorResolveResult codepath otherwise
                return rr;
            }
        }
    }
    // (The pre-bind discipline: the `MakeNullable` handle keeps the (nullable) enum
    // type alive through the factory call.)
    ITypePtr resultType = MakeNullable(enumType, isNullable);
    return BinaryOperatorResolveResult(*resultType, std::move(lhs), op, std::move(rhs),
                                       isNullable);
}

// ---- sizeof / this / base / typeof regions (CSharpResolver.cs lines 2591-2667 + 2935) -------

// The C# `public ResolveResult ResolveSizeOf(IType type)` (line 2591) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveSizeOf(ILSpy::Decompiler::TypeSystem::IType& type) const
{
    using ILSpy::Decompiler::Semantics::SizeOfResolveResult;
    using ILSpy::Decompiler::TypeSystem::GetTypeCode;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    // The C# `IType int32 = compilation.FindType(KnownTypeCode.Int32)` -- the `sizeof`
    // expression's own type. The registered known types are shared-managed, so the
    // `const_pointer_cast` recovers the owning handle (the D517 convention).
    ITypePtr int32 = std::const_pointer_cast<IType>(
        compilation_.FindType(KnownTypeCode::Int32).shared_from_this());
    std::optional<int> size;
    // The C# `var typeForConstant = (type.Kind == TypeKind.Enum)
    // ? type.GetDefinition().EnumUnderlyingType : type` -- an enum reads its size
    // through its underlying primitive. (The C# `GetDefinition().EnumUnderlyingType`
    // NREs for an enum-kind type whose definition does not resolve or whose
    // underlying is not configured; the port's safe fallback treats the enum arm as
    // not firing, reading the type's own TypeCode -- the D516 convention.)
    const IType* typeForConstant = &type;
    if (type.Kind() == TypeKind::Enum) {
        const ITypeDefinition* def = type.GetDefinition();
        if (def != nullptr) {
            ITypePtr underlying = def->EnumUnderlyingType();
            if (underlying)
                typeForConstant = underlying.get();
        }
    }
    switch (GetTypeCode(*typeForConstant)) {
        case TypeCode::Boolean:
        case TypeCode::SByte:
        case TypeCode::Byte:
            size = 1;
            break;
        case TypeCode::Char:
        case TypeCode::Int16:
        case TypeCode::UInt16:
            size = 2;
            break;
        case TypeCode::Int32:
        case TypeCode::UInt32:
        case TypeCode::Single:
            size = 4;
            break;
        case TypeCode::Int64:
        case TypeCode::UInt64:
        case TypeCode::Double:
            size = 8;
            break;
        default:
            break;
    }
    return std::make_shared<SizeOfResolveResult>(std::move(int32),
                                                 type.shared_from_this(),
                                                 std::move(size));
}

// The C# `public ResolveResult ResolveThisReference()` (line 2628) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveThisReference() const
{
    using ILSpy::Decompiler::Semantics::ThisResolveResult;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypeParameter;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::ParameterizedType;

    const ITypeDefinition* t = CurrentTypeDefinition();
    if (t != nullptr) {
        // The `t` definition and every declared type parameter are shared-managed
        // (the D271 handle model), so the handles recovered through `shared_from_this`
        // + `const_pointer_cast` share ownership with the holders the caller kept
        // them alive in (the SpecializedMember `DeclaringType` arm 2 documented the
        // conversion gap; here the self-parameterization needs it).
        ITypePtr tHandle = std::const_pointer_cast<IType>(t->shared_from_this());
        if (t->TypeParameterCount() != 0) {
            // Self-parameterize the type: `new ParameterizedType(t, t.TypeParameters)`
            // -- `this` inside `C<T,U>` has type `C<T,U>`.
            std::vector<ITypePtr> typeArgs;
            typeArgs.reserve(t->TypeParameters().size());
            for (const ITypeParameter* tp : t->TypeParameters()) {
                typeArgs.push_back(
                    std::const_pointer_cast<IType>(tp->shared_from_this()));
            }
            return std::make_shared<ThisResolveResult>(
                std::make_shared<ParameterizedType>(std::move(tHandle),
                                                    std::move(typeArgs)));
        } else {
            return std::make_shared<ThisResolveResult>(std::move(tHandle));
        }
    }
    return ErrorResultSingleton();
}

// The C# `public ResolveResult ResolveBaseReference()` (line 2649) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveBaseReference() const
{
    using ILSpy::Decompiler::Semantics::ThisResolveResult;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    const ITypeDefinition* t = CurrentTypeDefinition();
    if (t != nullptr) {
        for (const ITypePtr& baseType : t->DirectBaseTypes()) {
            TypeKind kind = baseType->Kind();
            if (kind != TypeKind::Unknown && kind != TypeKind::Interface) {
                return std::make_shared<ThisResolveResult>(
                    baseType, /*causesNonVirtualInvocation=*/true);
            }
        }
    }
    return ErrorResultSingleton();
}

// The C# `public ResolveResult ResolveTypeOf(IType referencedType)` (line 2935) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveTypeOf(ILSpy::Decompiler::TypeSystem::IType& referencedType) const
{
    using ILSpy::Decompiler::Semantics::TypeOfResolveResult;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

    // The C# `compilation.FindType(KnownTypeCode.Type)` -- the `typeof` expression's
    // own type (`System.Type`); the registered instance is shared-managed (the D517
    // convention).
    ITypePtr systemType = std::const_pointer_cast<IType>(
        compilation_.FindType(KnownTypeCode::Type).shared_from_this());
    return std::make_shared<TypeOfResolveResult>(std::move(systemType),
                                                 referencedType.shared_from_this());
}

// ---- condition / primitive / default value / assignment regions (CSharpResolver.cs
// ------ lines 2671-2795 + 2798-2810 + 2814-2878 + 2941-2960) ------------------------------

// The C# `public ResolveResult ResolveCondition(ResolveResult input)` (line 2671) --
// see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveCondition(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> input) const
{
    using ILSpy::Decompiler::Semantics::Conversion;
    using ILSpy::Decompiler::Semantics::Conversions;
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

    // The C# `if (input == null) throw new ArgumentNullException(nameof(input))`.
    if (!input)
        throw std::invalid_argument("input");
    // The C# `IType boolean = compilation.FindType(KnownTypeCode.Boolean)`. The
    // `ImplicitConversion` / `Convert` entries take non-const `IType&`, so the port
    // const_casts the const `FindType` reference (the D515/D517 convention).
    IType& boolean = const_cast<IType&>(compilation_.FindType(KnownTypeCode::Boolean));
    std::shared_ptr<Conversion> c = conversions_.ImplicitConversion(*input, boolean);
    if (!c->IsValid())
    {
        // The C# `.FirstOrDefault()` over the filtered snapshot -- an empty vector
        // yields null (the GetDelegateInvokeMethod convention).
        std::vector<const IMethod*> opTrueMethods = input->Type().GetMethods(
            [](const IMethod* m) { return m->IsOperator() && m->Name() == "op_True"; });
        const IMethod* opTrue =
            opTrueMethods.empty() ? nullptr : opTrueMethods.front();
        if (opTrue != nullptr)
        {
            // The C# `Conversion.UserDefinedConversion(opTrue, isImplicit: true,
            // conversionBeforeUserDefinedOperator: Conversion.None,
            // conversionAfterUserDefinedOperator: Conversion.None)`.
            c = Conversions::UserDefinedConversion(
                opTrue, /*isImplicit=*/true, Conversions::None(), Conversions::None());
        }
    }
    return Convert(std::move(input), boolean, std::move(c));
}

// The C# `public ResolveResult ResolveConditionFalse(ResolveResult input)` (line
// 2693) -- see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveConditionFalse(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> input) const
{
    using ILSpy::Decompiler::CSharp::Syntax::UnaryOperatorType;
    using ILSpy::Decompiler::Semantics::Conversion;
    using ILSpy::Decompiler::Semantics::Conversions;
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

    if (!input)
        throw std::invalid_argument("input");

    IType& boolean = const_cast<IType&>(compilation_.FindType(KnownTypeCode::Boolean));
    std::shared_ptr<Conversion> c = conversions_.ImplicitConversion(*input, boolean);
    if (!c->IsValid())
    {
        std::vector<const IMethod*> opFalseMethods = input->Type().GetMethods(
            [](const IMethod* m) { return m->IsOperator() && m->Name() == "op_False"; });
        const IMethod* opFalse =
            opFalseMethods.empty() ? nullptr : opFalseMethods.front();
        if (opFalse != nullptr)
        {
            // `input.operator false()` applies DIRECTLY (no negation on top).
            c = Conversions::UserDefinedConversion(
                opFalse, /*isImplicit=*/true, Conversions::None(), Conversions::None());
            return Convert(std::move(input), boolean, std::move(c));
        }
    }
    // `!(bool)input` -- the negation of the converted input.
    return ResolveUnaryOperator(
        UnaryOperatorType::Not, Convert(std::move(input), boolean, std::move(c)));
}

// The C# `public ResolveResult ResolveConditional(ResolveResult condition,
// ResolveResult trueExpression, ResolveResult falseExpression)` (line 2711, C# 4.0
// spec section 7.14) -- see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveConditional(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> condition,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> trueExpression,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> falseExpression) const
{
    using ILSpy::Decompiler::Semantics::Conversion;
    using ILSpy::Decompiler::Semantics::ErrorResolveResult;
    using ILSpy::Decompiler::Semantics::OperatorResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::ExpressionType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::SpecialType;
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    bool isValid;
    ITypePtr resultType;
    if (trueExpression->Type().Kind() == TypeKind::Dynamic
        || falseExpression->Type().Kind() == TypeKind::Dynamic)
    {
        // The C# `SpecialType.Dynamic` -- a shared-managed instance (the
        // ResolveBinaryOperator dynamic-arm convention).
        resultType = std::make_shared<SpecialType>(TypeKind::Dynamic, true);
        // The C# `TryConvert(ref trueExpression, resultType) & TryConvert(ref
        // falseExpression, resultType)` -- the NON-SHORT-CIRCUIT `&` runs BOTH
        // conversions; evaluating them into locals first makes both-run structural
        // (the C++ `&` over bools is also non-short-circuit, but the locals pin the
        // order the operand rebinds observe). The rebinds stay local (the parameters
        // are value copies of the caller's references).
        bool trueConverted = TryConvert(trueExpression, *resultType);
        bool falseConverted = TryConvert(falseExpression, *resultType);
        isValid = trueConverted & falseConverted;
    }
    else if (HasType(*trueExpression) && HasType(*falseExpression))
    {
        // The C# rebinds its local operand references through `Convert` -- the
        // rebinds stay local (the parameters are value copies of the caller's
        // references). The `Type()` accessors return `const IType&` while
        // `ImplicitConversion` / `Convert` take non-const `IType&`, so the port
        // const_casts (the D515/D517 convention).
        std::shared_ptr<Conversion> t2f = conversions_.ImplicitConversion(
            *trueExpression, const_cast<IType&>(falseExpression->Type()));
        std::shared_ptr<Conversion> f2t = conversions_.ImplicitConversion(
            *falseExpression, const_cast<IType&>(trueExpression->Type()));
        // The operator is valid:
        // a) if there's a conversion in one direction but not the other
        // b) if there are conversions in both directions, and the types are equivalent
        if (IsBetterConditionalConversion(t2f, f2t))
        {
            resultType = std::const_pointer_cast<IType>(
                falseExpression->Type().shared_from_this());
            isValid = true;
            trueExpression = Convert(std::move(trueExpression), *resultType, std::move(t2f));
        }
        else if (IsBetterConditionalConversion(f2t, t2f))
        {
            resultType = std::const_pointer_cast<IType>(
                trueExpression->Type().shared_from_this());
            isValid = true;
            falseExpression = Convert(std::move(falseExpression), *resultType, std::move(f2t));
        }
        else
        {
            resultType = std::const_pointer_cast<IType>(
                trueExpression->Type().shared_from_this());
            isValid = trueExpression->Type().Equals(falseExpression->Type());
        }
    }
    else if (HasType(*trueExpression))
    {
        resultType =
            std::const_pointer_cast<IType>(trueExpression->Type().shared_from_this());
        isValid = TryConvert(falseExpression, *resultType);
    }
    else if (HasType(*falseExpression))
    {
        resultType =
            std::const_pointer_cast<IType>(falseExpression->Type().shared_from_this());
        isValid = TryConvert(trueExpression, *resultType);
    }
    else
    {
        return ErrorResultSingleton();
    }
    condition = ResolveCondition(std::move(condition));
    if (isValid)
    {
        if (condition->IsCompileTimeConstant() && trueExpression->IsCompileTimeConstant()
            && falseExpression->IsCompileTimeConstant())
        {
            // The C# `bool? val = condition.ConstantValue as bool?` -- the pointer-form
            // `any_cast` yields null for an empty any or a non-bool held type (the C#
            // `as` yields null), so neither branch fires.
            const bool* val = std::any_cast<bool>(&condition->ConstantValue());
            if (val != nullptr && *val)
                return trueExpression;
            else if (val != nullptr && !*val)
                return falseExpression;
        }
        return std::make_shared<OperatorResolveResult>(
            std::move(resultType), ExpressionType::Conditional,
            std::vector<std::shared_ptr<ResolveResult>>{
                std::move(condition), std::move(trueExpression), std::move(falseExpression)});
    }
    else
    {
        return std::make_shared<ErrorResolveResult>(std::move(resultType));
    }
}

// The C# private `bool IsBetterConditionalConversion(Conversion c1, Conversion c2)`
// (line 2786) -- see CSharpResolver.hpp for the port conventions.
bool CSharpResolver::IsBetterConditionalConversion(
    const std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>& c1,
    const std::shared_ptr<ILSpy::Decompiler::Semantics::Conversion>& c2)
{
    using ILSpy::Decompiler::Semantics::Conversions;

    // Valid is better than ImplicitConstantExpressionConversion is better than invalid
    if (!c1->IsValid())
        return false;
    // The C# `c1 != Conversion.ImplicitConstantExpressionConversion && c2 ==
    // Conversion.ImplicitConstantExpressionConversion` -- reference comparisons
    // against the singleton, ported as pointer identity (the D536 convention).
    if (c1.get() != Conversions::ImplicitConstantExpressionConversion().get()
        && c2.get() == Conversions::ImplicitConstantExpressionConversion().get())
        return true;
    return !c2->IsValid();
}

// The C# private `bool HasType(ResolveResult r)` (line 2791).
bool CSharpResolver::HasType(const ILSpy::Decompiler::Semantics::ResolveResult& r)
{
    using ILSpy::Decompiler::TypeSystem::TypeKind;

    return r.Type().Kind() != TypeKind::None && r.Type().Kind() != TypeKind::Null;
}

// The C# `public ResolveResult ResolvePrimitive(object value)` (line 2798) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolvePrimitive(const std::any& value) const
{
    using ILSpy::Decompiler::Semantics::ConstantResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::FindType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::SpecialType;
    using ILSpy::Decompiler::TypeSystem::TypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;
    using ILSpy::Decompiler::Util::TypeCodeOfBoxedValue;

    if (!value.has_value())
    {
        // The C# `new ResolveResult(SpecialType.NullType)` -- the null-literal type
        // (a shared-managed `SpecialType(TypeKind::Null, true)`).
        return std::make_shared<ResolveResult>(
            std::make_shared<SpecialType>(TypeKind::Null, std::optional<bool>(true)));
    }
    // The C# `Type.GetTypeCode(value.GetType())` over the port's boxed constant-value
    // types, then `compilation.FindType(typeCode)` through the TypeCode-based free
    // `FindType` (the ReflectionHelper leaf).
    TypeCode typeCode = TypeCodeOfBoxedValue(value);
    const IType& type = FindType(compilation_, typeCode);
    return std::make_shared<ConstantResolveResult>(
        std::const_pointer_cast<IType>(type.shared_from_this()), value);
}

// The C# `public ResolveResult ResolveDefaultValue(IType type)` (line 2814) -- see
// CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveDefaultValue(
    ILSpy::Decompiler::TypeSystem::IType& type) const
{
    using ILSpy::Decompiler::Semantics::ConstantResolveResult;

    return std::make_shared<ConstantResolveResult>(type.shared_from_this(),
                                                    GetDefaultValue(type));
}

// The C# `public static object GetDefaultValue(IType type)` (line 2819) -- see
// CSharpResolver.hpp for the port conventions.
std::any CSharpResolver::GetDefaultValue(const ILSpy::Decompiler::TypeSystem::IType& type)
{
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using ILSpy::Decompiler::TypeSystem::TypeKind;
    using ILSpy::Decompiler::Util::Decimal;

    const ITypeDefinition* typeDef = type.GetDefinition();
    if (typeDef == nullptr)
        return std::any();
    if (typeDef->Kind() == TypeKind::Enum)
    {
        // The C# `typeDef.EnumUnderlyingType.GetDefinition()` -- the first deref NREs
        // for a degenerate enum without a configured underlying; the port's null
        // check returns the null default (the D516 safe-fallback convention).
        ITypePtr underlying = typeDef->EnumUnderlyingType();
        if (!underlying)
            return std::any();
        typeDef = underlying->GetDefinition();
        if (typeDef == nullptr)
            return std::any();
    }
    switch (typeDef->KnownTypeCode()) {
        case KnownTypeCode::Boolean:
            return std::any(false);
        case KnownTypeCode::Char:
            return std::any(char16_t(0));
        case KnownTypeCode::SByte:
            return std::any(std::int8_t(0));
        case KnownTypeCode::Byte:
            return std::any(std::uint8_t(0));
        case KnownTypeCode::Int16:
            return std::any(std::int16_t(0));
        case KnownTypeCode::UInt16:
            return std::any(std::uint16_t(0));
        case KnownTypeCode::Int32:
            return std::any(std::int32_t(0));
        case KnownTypeCode::UInt32:
            return std::any(std::uint32_t(0));
        case KnownTypeCode::Int64:
            return std::any(std::int64_t(0));
        case KnownTypeCode::UInt64:
            return std::any(std::uint64_t(0));
        case KnownTypeCode::Single:
            return std::any(0.0f);
        case KnownTypeCode::Double:
            return std::any(0.0);
        case KnownTypeCode::Decimal:
            // The C# `0m` -- the default-initialized scaled-decimal zero.
            return std::any(Decimal{});
        default:
            return std::any();
    }
}

// The C# `public ResolveResult ResolveAssignment(AssignmentOperatorType op,
// ResolveResult lhs, ResolveResult rhs)` (line 2941) -- see CSharpResolver.hpp for the
// port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveAssignment(
    ILSpy::Decompiler::CSharp::Syntax::AssignmentOperatorType op,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> lhs,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> rhs) const
{
    using ILSpy::Decompiler::CSharp::Syntax::AssignmentExpression;
    using ILSpy::Decompiler::CSharp::Syntax::BinaryOperatorType;
    using ILSpy::Decompiler::Semantics::OperatorResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::ExpressionType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;

    ExpressionType linqOp = AssignmentExpression::GetLinqNodeType(op, checkForOverflow_);
    std::optional<BinaryOperatorType> bop =
        AssignmentExpression::GetCorrespondingBinaryOperator(op);
    if (!bop.has_value())
    {
        // The C# `new OperatorResolveResult(lhs.Type, linqOp, lhs, this.Convert(rhs,
        // lhs.Type))` -- the result-type handle is bound before the `std::move` of the
        // operands (the argument-evaluation-order hazard: the pointee object is not
        // destroyed by the move, so the pre-bound handle stays valid).
        ITypePtr lhsType = std::const_pointer_cast<IType>(lhs->Type().shared_from_this());
        return std::make_shared<OperatorResolveResult>(
            std::move(lhsType), linqOp,
            std::vector<std::shared_ptr<ResolveResult>>{
                lhs, Convert(std::move(rhs), const_cast<IType&>(lhs->Type()))});
    }
    // The operands are passed as COPIES (not moves) -- the final composition below
    // still reads `lhs` and the C# keeps using its operand references after the
    // binary resolution (the iteration-105 move-hazard learning).
    std::shared_ptr<ResolveResult> bopResult = ResolveBinaryOperator(*bop, lhs, rhs);
    // The C# `bopResult as OperatorResolveResult`.
    const OperatorResolveResult* opResult =
        dynamic_cast<const OperatorResolveResult*>(bopResult.get());
    if (opResult == nullptr || opResult->Operands().size() != 2)
        return bopResult;
    ITypePtr lhsType = std::const_pointer_cast<IType>(lhs->Type().shared_from_this());
    return std::make_shared<OperatorResolveResult>(
        std::move(lhsType), linqOp, opResult->UserDefinedOperatorMethod(),
        opResult->IsLiftedOperator(),
        std::vector<std::shared_ptr<ResolveResult>>{ lhs, opResult->Operands()[1] });
}

// ---- ResolveForeach region (CSharpResolver.cs lines 1913-2018) --------------------------------

// The C# `public ForEachResolveResult ResolveForeach(ResolveResult expression)`
// (line 1914) -- see CSharpResolver.hpp for the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ForEachResolveResult>
CSharpResolver::ResolveForeach(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> expression) const
{
    using ILSpy::Decompiler::CSharp::Resolver::MethodGroupResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
    using ILSpy::Decompiler::Semantics::ForEachResolveResult;
    using ILSpy::Decompiler::Semantics::MemberResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::ArrayType;
    using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
    using ILSpy::Decompiler::TypeSystem::IMethod;
    using ILSpy::Decompiler::TypeSystem::IProperty;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using ILSpy::Decompiler::TypeSystem::SpecialType;
    using ILSpy::Decompiler::TypeSystem::TypeKind;
    using ILSpy::Decompiler::TypeSystem::UnknownType;

    // C# 4.0 spec: section 8.8.4 The foreach statement
    MemberLookup memberLookup = CreateMemberLookup();

    ITypePtr collectionType, enumeratorType, elementType;
    std::shared_ptr<ResolveResult> getEnumeratorInvocation;
    std::shared_ptr<ResolveResult> currentRR;
    // The C# `new ResolveResult(enumeratorType)` targets of the `Current` / `MoveNext`
    // lookups: `MemberLookup::Lookup`'s results hold a NON-OWNING alias to the target
    // (the caller owns the target, the MemberLookup.cpp convention), so each target must
    // outlive every use of the result it fed -- they are declared at FUNCTION scope
    // (the Current target outlives the `currentProperty` extraction near the end).
    std::shared_ptr<ResolveResult> currentTargetRR, moveNextTargetRR;

    if (expression->Type().Kind() == TypeKind::Array
        || expression->Type().Kind() == TypeKind::Dynamic) {
        // The C# `collectionType = compilation.FindType(KnownTypeCode.IEnumerable)` --
        // the owning handle recovered from the const `FindType` reference via
        // `shared_from_this()` + `const_pointer_cast` (the D529 convention; the
        // registered known types are shared-managed).
        collectionType = std::const_pointer_cast<IType>(
            compilation_.FindType(KnownTypeCode::IEnumerable).shared_from_this());
        enumeratorType = std::const_pointer_cast<IType>(
            compilation_.FindType(KnownTypeCode::IEnumerator).shared_from_this());
        if (expression->Type().Kind() == TypeKind::Array) {
            // The C# `((ArrayType)expression.Type).ElementType` hard cast; the port's
            // dynamic_cast keeps the degenerate kind-Array-but-not-ArrayType stub shape
            // from reinterpreting unrelated storage (the D565 safe-fallback convention
            // -- the C# would throw InvalidCastException; the port falls to the
            // UnknownType null object).
            const ArrayType* arrayType = dynamic_cast<const ArrayType*>(&expression->Type());
            elementType = arrayType != nullptr ? arrayType->Element() : UnknownType();
        } else {
            // The C# `SpecialType.Dynamic` singleton ports to the fresh shared-managed
            // instance (the D469 DynamicMemberResolveResult precedent).
            elementType = std::make_shared<SpecialType>(TypeKind::Dynamic, true);
        }
        // The `GetEnumerator` chain over the non-generic IEnumerable: ResolveCast ->
        // ResolveMemberAccess (InvocationTarget) -> ResolveInvocation (no arguments).
        getEnumeratorInvocation = ResolveCast(
            const_cast<IType&>(*collectionType), expression);
        getEnumeratorInvocation = ResolveMemberAccess(
            std::move(getEnumeratorInvocation), "GetEnumerator", {},
            NameLookupMode::InvocationTarget);
        getEnumeratorInvocation = ResolveInvocation(std::move(getEnumeratorInvocation), {});
    } else {
        // The C# `memberLookup.Lookup(expression, "GetEnumerator", EmptyList<IType>.
        // Instance, true) as MethodGroupResolveResult`.
        std::shared_ptr<MethodGroupResolveResult> getEnumeratorMethodGroup =
            std::dynamic_pointer_cast<MethodGroupResolveResult>(
                memberLookup.Lookup(*expression, "GetEnumerator", {}, /*isInvocation*/ true));
        if (getEnumeratorMethodGroup) {
            std::unique_ptr<OverloadResolution> or_ = getEnumeratorMethodGroup
                                                             ->PerformOverloadResolution(
                                                                 compilation_, {},
                                                                 std::nullopt,
                                                                 /*allowExtensionMethods*/ false,
                                                                 /*allowExpandingParams*/ false,
                                                                 /*allowOptionalParameters*/ false);
            const IParameterizedMember* best = or_->BestCandidate();
            // The C# `or.FoundApplicableCandidate && !or.IsAmbiguous &&
            // !or.BestCandidate.IsStatic && or.BestCandidate.Accessibility ==
            // Accessibility.Public` -- the short-circuit guarantees a non-null
            // BestCandidate after FoundApplicableCandidate; the port adds the null
            // guard for the degenerate shape anyway (the D516 convention).
            if (or_->FoundApplicableCandidate() && !or_->IsAmbiguous() && best != nullptr
                && !best->IsStatic()
                && best->Accessibility()
                       == ILSpy::Decompiler::TypeSystem::Accessibility::Public) {
                collectionType = std::const_pointer_cast<IType>(
                    expression->Type().shared_from_this());
                getEnumeratorInvocation = or_->CreateResolveResult(expression);
                enumeratorType = std::const_pointer_cast<IType>(
                    getEnumeratorInvocation->Type().shared_from_this());
                currentTargetRR = std::make_shared<ResolveResult>(enumeratorType);
                currentRR = memberLookup.Lookup(*currentTargetRR, "Current", {},
                                                 /*isInvocation*/ false);
                elementType = std::const_pointer_cast<IType>(
                    currentRR->Type().shared_from_this());
            } else {
                CheckForEnumerableInterface(expression, collectionType, enumeratorType,
                                            elementType, getEnumeratorInvocation);
            }
        } else {
            CheckForEnumerableInterface(expression, collectionType, enumeratorType,
                                        elementType, getEnumeratorInvocation);
        }
    }

    const IMethod* moveNextMethod = nullptr;
    moveNextTargetRR = std::make_shared<ResolveResult>(enumeratorType);
    std::shared_ptr<MethodGroupResolveResult> moveNextMethodGroup =
        std::dynamic_pointer_cast<MethodGroupResolveResult>(
            memberLookup.Lookup(*moveNextTargetRR, "MoveNext", {}, /*isInvocation*/ false));
    if (moveNextMethodGroup) {
        std::unique_ptr<OverloadResolution> or_ = moveNextMethodGroup
                                                         ->PerformOverloadResolution(
                                                             compilation_, {},
                                                             std::nullopt,
                                                             /*allowExtensionMethods*/ false,
                                                             /*allowExpandingParams*/ false,
                                                             /*allowOptionalParameters*/ false);
        // The C# `or.GetBestCandidateWithSubstitutedTypeArguments() as IMethod` -- null
        // when no best candidate exists (the empty method group cannot occur here; the
        // group exists but resolution found nothing applicable still yields a best
        // candidate, faithfully matching the C# which does not gate on applicability).
        moveNextMethod = dynamic_cast<const IMethod*>(
            or_->GetBestCandidateWithSubstitutedTypeArguments());
    }

    if (!currentRR) {
        currentTargetRR = std::make_shared<ResolveResult>(enumeratorType);
        currentRR = memberLookup.Lookup(*currentTargetRR, "Current", {}, /*isInvocation*/ false);
    }
    const IProperty* currentProperty = nullptr;
    if (dynamic_cast<const MemberResolveResult*>(currentRR.get()) != nullptr)
        currentProperty = dynamic_cast<const IProperty*>(
            static_cast<const MemberResolveResult*>(currentRR.get())->Member());

    ITypePtr voidType = std::const_pointer_cast<IType>(
        compilation_.FindType(KnownTypeCode::Void).shared_from_this());
    return std::make_shared<ForEachResolveResult>(
        std::move(getEnumeratorInvocation), std::move(collectionType),
        std::move(enumeratorType), std::move(elementType), currentProperty, moveNextMethod,
        std::move(voidType));
}

// The C# `void CheckForEnumerableInterface(ResolveResult expression, out IType
// collectionType, out IType enumeratorType, out IType elementType, out ResolveResult
// getEnumeratorInvocation)` (line 1991) -- see CSharpResolver.hpp for the port
// conventions.
void CSharpResolver::CheckForEnumerableInterface(
    const std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& expression,
    ILSpy::Decompiler::TypeSystem::ITypePtr& collectionType,
    ILSpy::Decompiler::TypeSystem::ITypePtr& enumeratorType,
    ILSpy::Decompiler::TypeSystem::ITypePtr& elementType,
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& getEnumeratorInvocation) const
{
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::GetElementTypeFromIEnumerable;
    using ILSpy::Decompiler::TypeSystem::ParameterizedType;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypeDefinition;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
    using ILSpy::Decompiler::TypeSystem::UnknownType;

    // The C# `out bool? isGeneric` local of the extension call.
    std::optional<bool> isGeneric;
    elementType = GetElementTypeFromIEnumerable(expression->Type(), compilation_,
                                                 /*allowIEnumerator*/ false, isGeneric);
    // The C# `isGeneric == true` / `== false` on a nullable bool are DEFINITE checks
    // (null compares false against both literals; the iteration-103 corrected semantics).
    if (isGeneric.has_value() && *isGeneric) {
        // The C# `compilation.FindType(KnownTypeCode.IEnumerableOfT).GetDefinition()`;
        // the open-generic definition is shared-managed, so the owning handle for the
        // ParameterizedType ctor comes from `shared_from_this()` + `const_pointer_cast`
        // (the D529 convention).
        const ITypeDefinition* enumerableOfT =
            compilation_.FindType(KnownTypeCode::IEnumerableOfT).GetDefinition();
        if (enumerableOfT != nullptr)
            collectionType = std::make_shared<ParameterizedType>(
                std::const_pointer_cast<IType>(enumerableOfT->shared_from_this()),
                std::vector<ITypePtr>{ elementType });
        else
            collectionType = UnknownType();

        const ITypeDefinition* enumeratorOfT =
            compilation_.FindType(KnownTypeCode::IEnumeratorOfT).GetDefinition();
        if (enumeratorOfT != nullptr)
            enumeratorType = std::make_shared<ParameterizedType>(
                std::const_pointer_cast<IType>(enumeratorOfT->shared_from_this()),
                std::vector<ITypePtr>{ elementType });
        else
            enumeratorType = UnknownType();
    } else if (isGeneric.has_value() && !*isGeneric) {
        collectionType = std::const_pointer_cast<IType>(
            compilation_.FindType(KnownTypeCode::IEnumerable).shared_from_this());
        enumeratorType = std::const_pointer_cast<IType>(
            compilation_.FindType(KnownTypeCode::IEnumerator).shared_from_this());
    } else {
        collectionType = UnknownType();
        enumeratorType = UnknownType();
    }
    getEnumeratorInvocation = ResolveCast(const_cast<IType&>(*collectionType), expression);
    getEnumeratorInvocation = ResolveMemberAccess(
        std::move(getEnumeratorInvocation), "GetEnumerator", {},
        NameLookupMode::InvocationTarget);
    getEnumeratorInvocation = ResolveInvocation(std::move(getEnumeratorInvocation), {});
}

// ---- ResolveIndexer region (CSharpResolver.cs lines 2456-2522) ----------------------------

// The C# `void AdjustArrayAccessArguments(ResolveResult[] arguments)` (line 2513) --
// see CSharpResolver.hpp for the port conventions.
void CSharpResolver::AdjustArrayAccessArguments(
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>& arguments) const
{
    using ILSpy::Decompiler::Semantics::Conversions;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::KnownTypeCode;

    for (std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>& argument : arguments) {
        // The C# `TryConvert(ref arguments[i], compilation.FindType(...)) || ...` -- the
        // short-circuiting chain rebinds the caller's argument through the first
        // applicable conversion. `TryConvert` takes a non-const `IType&`; the `FindType`
        // const accessor's contract is cast away (the D515/D517 const_cast convention:
        // the underlying type-system object is mutable).
        if (!(TryConvert(argument, const_cast<IType&>(compilation_.FindType(KnownTypeCode::Int32)))
              || TryConvert(argument, const_cast<IType&>(compilation_.FindType(KnownTypeCode::UInt32)))
              || TryConvert(argument, const_cast<IType&>(compilation_.FindType(KnownTypeCode::Int64)))
              || TryConvert(argument, const_cast<IType&>(compilation_.FindType(KnownTypeCode::UInt64))))) {
            // conversion failed
            argument = Convert(argument,
                              const_cast<IType&>(compilation_.FindType(KnownTypeCode::Int32)),
                              Conversions::None());
        }
    }
}

// The C# `public ResolveResult ResolveIndexer(ResolveResult target, ResolveResult[]
// arguments, string[] argumentNames = null)` (line 2456) -- see CSharpResolver.hpp for
// the port conventions.
std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>
CSharpResolver::ResolveIndexer(
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> target,
    std::vector<std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult>> arguments,
    std::optional<std::vector<std::string>> argumentNames) const
{
    using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationResolveResult;
    using ILSpy::Decompiler::CSharp::Resolver::DynamicInvocationType;
    using ILSpy::Decompiler::CSharp::Resolver::IsApplicable;
    using ILSpy::Decompiler::CSharp::Resolver::MethodListWithDeclaringType;
    using ILSpy::Decompiler::CSharp::Resolver::OverloadResolution;
    using ILSpy::Decompiler::Semantics::ArrayAccessResolveResult;
    using ILSpy::Decompiler::Semantics::ResolveResult;
    using ILSpy::Decompiler::TypeSystem::ArrayType;
    using ILSpy::Decompiler::TypeSystem::IParameterizedMember;
    using ILSpy::Decompiler::TypeSystem::IType;
    using ILSpy::Decompiler::TypeSystem::ITypePtr;
    using ILSpy::Decompiler::TypeSystem::PointerType;
    using ILSpy::Decompiler::TypeSystem::TypeKind;
    using ILSpy::Decompiler::TypeSystem::UnknownType;

    // C# 4.0 spec: the arms on the TARGET's kind.
    switch (target->Type().Kind()) {
        case TypeKind::Dynamic:
            // The C# `new DynamicInvocationResolveResult(target, DynamicInvocationType.
            // Indexing, AddArgumentNamesIfNecessary(arguments, argumentNames))`.
            return std::make_shared<DynamicInvocationResolveResult>(
                std::move(target), DynamicInvocationType::Indexing,
                AddArgumentNamesIfNecessary(arguments, argumentNames));
        case TypeKind::Array:
        case TypeKind::Pointer: {
            // C# 4.0 spec: section 7.6.6.1 Array access / section 18.5.3 Pointer
            // element access.
            AdjustArrayAccessArguments(arguments);
            const IType& targetType = target->Type();
            ITypePtr elementType;
            if (const auto* arrayType = dynamic_cast<const ArrayType*>(&targetType))
                elementType = arrayType->Element();
            else if (const auto* pointerType = dynamic_cast<const PointerType*>(&targetType))
                elementType = pointerType->Element();
            if (!elementType)
                elementType = UnknownType();
            return std::make_shared<ArrayAccessResolveResult>(
                std::move(elementType), std::move(target), std::move(arguments));
        }
        default:
            break;
    }

    // C# 4.0 spec: section 7.6.6.2 Indexer access.

    MemberLookup lookup = CreateMemberLookup();
    std::vector<MethodListWithDeclaringType> indexers = lookup.LookupIndexers(*target);

    // The C# `arguments.Any(a => a.Type.Kind == TypeKind.Dynamic)`.
    bool isDynamic = std::any_of(arguments.begin(), arguments.end(),
                                 [](const std::shared_ptr<ResolveResult>& a) {
                                     return a->Type().Kind() == TypeKind::Dynamic;
                                 });
    if (isDynamic) {
        // If we have dynamic arguments, we need to represent the invocation as a
        // dynamic invocation if there is more than one applicable indexer.
        //
        // The C# `CreateOverloadResolution(arguments, argumentNames, null)` -- the
        // THROWAWAY resolution: the `AddCandidate` calls mutate its best-candidate
        // state as a side effect, but only the returned error masks are consumed (the
        // ResolveInvocation dynamic sub-arm convention).
        std::unique_ptr<OverloadResolution> or2 =
            CreateOverloadResolution(arguments, argumentNames, std::nullopt);
        // The C# `indexers.SelectMany(x => x).Where(m => OverloadResolution.
        // IsApplicable(or2.AddCandidate(m))).ToList()` -- the flattened members filtered
        // by applicability; the COUNT decides the dynamic arm (the full list is
        // materialized, no early exit at two).
        std::size_t applicableIndexers = 0;
        for (const MethodListWithDeclaringType& list : indexers) {
            for (const IParameterizedMember* m : list) {
                if (IsApplicable(or2->AddCandidate(*m)))
                    applicableIndexers++;
            }
        }

        if (applicableIndexers > 1) {
            return std::make_shared<DynamicInvocationResolveResult>(
                std::move(target), DynamicInvocationType::Indexing,
                AddArgumentNamesIfNecessary(arguments, argumentNames));
        }
    }

    std::unique_ptr<OverloadResolution> orr = CreateOverloadResolution(arguments, argumentNames);
    orr->AddMethodLists(indexers);
    if (orr->BestCandidate() != nullptr) {
        return orr->CreateResolveResult(std::move(target));
    } else {
        return ErrorResultSingleton();
    }
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
