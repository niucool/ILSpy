// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies
// or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
// TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.

// Implementation of the `CSharpOperators` skeleton (see the header): the `Get` factory
// (the per-compilation CacheManager singleton), `InitParameterArrays` (the precomputed
// parameter tables), `MakeParameter` / `MakeNullableParameter` (the table lookups),
// `Lift` (the lifted-form list builder), and the out-of-line `OperatorMethod` members
// (`Parameters` / `ReturnType` / `Substitution` / `Specialize` / `ToString`).

#include "Decompiler/CSharp/Resolver/CSharpOperators.hpp"

#include "Decompiler/TypeSystem/ICompilation.hpp"  // ICompilation (FindType/MainModule/CacheManager)
#include "Decompiler/TypeSystem/IParameter.hpp"  // IParameter (the parameter table element)
#include "Decompiler/TypeSystem/NullableType.hpp"  // NullableType::Create (the nullable table)
#include "Decompiler/TypeSystem/TypeParameterSubstitution.hpp"  // TypeParameterSubstitution (Identity/Specialize)
#include "Decompiler/TypeSystem/Implementation/DefaultParameter.hpp"  // DefaultParameter (the table entries)
#include "Decompiler/Util/CacheManager.hpp"  // CacheManager (Get factory)

#include <any>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `NullableType.Create` free function lives directly in the TypeSystem namespace
// (the C# static class is a label, not a namespace -- the iteration-37 convention).
using ILSpy::Decompiler::TypeSystem::Create;

// ---------------------------------------------------------------------------
// OperatorMethod (the out-of-line members)
// ---------------------------------------------------------------------------

// The C# `IReadOnlyList<IParameter> Parameters => parameters` -- the snapshot over the
// internal parameter list (the IParameterizedMember::Parameters convention).
std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*>
OperatorMethod::Parameters() const
{
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> result;
    result.reserve(parameters_.size());
    for (const auto& parameter : parameters_)
        result.push_back(parameter.get());
    return result;
}

// The C# `public IType ReturnType { get; internal set; } = null!`. The C# `null!`
// placeholder is never read before a derived ctor assigned it; the port returns the
// `UnknownType()` null object until then (the documented safe faithful fallback, the
// D516 null-guard precedent; a function-local static keeps the returned reference
// stable).
const ILSpy::Decompiler::TypeSystem::IType& OperatorMethod::ReturnType() const
{
    if (returnType_)
        return *returnType_;
    static const ILSpy::Decompiler::TypeSystem::ITypePtr unknown =
        ILSpy::Decompiler::TypeSystem::UnknownType();
    return *unknown;
}

// The C# `TypeParameterSubstitution IMember.Substitution =>
// TypeParameterSubstitution.Identity` -- the never-null identity singleton.
const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*
OperatorMethod::Substitution() const
{
    return &ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution::Identity();
}

// The C# `IMember IMember.Specialize(TypeParameterSubstitution substitution)`:
// `if (TypeParameterSubstitution.Identity.Equals(substitution)) return this; throw new
// NotSupportedException();` -- the built-in operator methods are never specialized (a
// real specialization only happens on a type-system member, and the operator tables are
// per-compilation singletons already). NotSupportedException ports to std::logic_error
// (the EventDeclaration/AssignmentExpression precedent).
const ILSpy::Decompiler::TypeSystem::IMember* OperatorMethod::Specialize(
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* substitution) const
{
    if (ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution::Identity().Equals(substitution))
        return this;
    throw std::logic_error("NotSupportedException: OperatorMethod.Specialize");
}

// The C# `public override string ToString()`:
// `b.Append(ReturnType + " operator(")` then `parameters[i].Type` joined by ", " then
// `b.Append(')')` -- each type renders as its ReflectionName (the C#
// `AbstractType.ToString() => ReflectionName`).
std::string OperatorMethod::ToString() const
{
    std::string result = ReturnType().ReflectionName();
    result += " operator(";
    for (std::size_t i = 0; i < parameters_.size(); i++)
    {
        if (i > 0)
            result += ", ";
        result += parameters_[i]->Type().ReflectionName();
    }
    result += ')';
    return result;
}

// ---------------------------------------------------------------------------
// CSharpOperators
// ---------------------------------------------------------------------------

// The C# `public static CSharpOperators Get(ICompilation compilation)` (lines 40-54):
// `CacheManager cache = compilation.CacheManager; CSharpOperators? operators =
// (CSharpOperators?)cache.GetShared(typeof(CSharpOperators)); if (operators == null) {
// operators = (CSharpOperators)cache.GetOrAddShared(typeof(CSharpOperators), new
// CSharpOperators(compilation)); } return operators;` -- the per-compilation singleton
// keyed on the runtime type object. The port follows the CSharpConversions::Get
// precedent: the key is a function-local static's address (one per translation unit, a
// single key for CSharpOperators), the cached value is an owning shared_ptr (the cache
// owns the instance for the compilation's lifetime), and `GetOrAddShared` with a
// pre-built value dedups a concurrent first-writer race.
CSharpOperators& CSharpOperators::Get(
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
{
    static char typeKey;
    const void* key = &typeKey;
    // The C# `compilation.CacheManager` returns a mutable `CacheManager` (the C# has no
    // const); the port's `ICompilation::CacheManager()` returns `const CacheManager&` (a
    // const-correctness over-restriction relative to the C#). The `GetOrAddShared`
    // mutator stores the singleton in the cache, but the mutation is logically idempotent
    // (a repeat `Get` with the same key returns the same value whether it stored or
    // found), so the `const_cast` here is the established convention for logically-const
    // lazy-cache accessors (the CSharpConversions::Get precedent).
    auto& cache = const_cast<ILSpy::Decompiler::Util::CacheManager&>(compilation.CacheManager());
    // `std::make_shared` cannot construct through the private ctor (the
    // IntersectionType::Create precedent); the static factory (which has private access)
    // builds the shared handle via `new`.
    std::shared_ptr<CSharpOperators> instance(new CSharpOperators(compilation));
    std::any cached = cache.GetOrAddShared(key, std::any{std::shared_ptr<CSharpOperators>{instance}});
    // `cached` is the winning value (ours if we were first, the existing one otherwise).
    // Unwrap and return by reference (the cache keeps the shared_ptr alive for the
    // compilation's lifetime).
    auto stored = std::any_cast<std::shared_ptr<CSharpOperators>>(cached);
    return *stored;
}

// The C# `private CSharpOperators(ICompilation compilation)` -- `this.compilation =
// compilation; InitParameterArrays();`. The arrays are pre-sized to the C# lengths:
// `(int)(TypeCode.String + 1 - TypeCode.Object)` == 17 and
// `(int)(TypeCode.Decimal + 1 - TypeCode.Boolean)` == 13.
CSharpOperators::CSharpOperators(
    const ILSpy::Decompiler::TypeSystem::ICompilation& compilation)
    : compilation_(compilation),
      normalParameters_(static_cast<std::size_t>(
          static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::String) + 1
          - static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Object))),
      nullableParameters_(static_cast<std::size_t>(
          static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Decimal) + 1
          - static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Boolean)))
{
    InitParameterArrays();
}

// The C# `void InitParameterArrays()` (lines 72-87): the normal table is one
// `DefaultParameter(compilation.FindType(i), string.Empty)` per TypeCode.Object..String;
// the nullable table is one `DefaultParameter(NullableType.Create(compilation,
// compilation.FindType(i)), string.Empty)` per TypeCode.Boolean..Decimal. `FindType(i)`
// is the ReflectionHelper TypeCode extension (the D513 leaf), delegating to
// `ICompilation.FindType((KnownTypeCode)i)`.
void CSharpOperators::InitParameterArrays()
{
    for (int raw = static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Object);
         raw <= static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::String); ++raw)
    {
        const ILSpy::Decompiler::TypeSystem::IType& type =
            ILSpy::Decompiler::TypeSystem::FindType(
                compilation_, static_cast<ILSpy::Decompiler::TypeSystem::TypeCode>(raw));
        // `DefaultParameter` takes an OWNING `ITypePtr`; the `FindType` result is a
        // non-owning `const IType&`, so the owning handle is recovered through
        // `shared_from_this()` + `const_pointer_cast` (the D529 convention: the
        // type-system objects are shared-managed, and the accessor's const is the
        // contract, not a guarantee).
        normalParameters_[static_cast<std::size_t>(
            raw - static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Object))] =
            std::make_shared<ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(
                std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
                    type.shared_from_this()),
                std::string());
    }
    for (int raw = static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Boolean);
         raw <= static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Decimal); ++raw)
    {
        // The C# `NullableType.Create(compilation, compilation.FindType(i))` builds the
        // `Nullable<T>` parameterized type (an owning `ITypePtr` by return).
        ILSpy::Decompiler::TypeSystem::ITypePtr type = Create(
            compilation_,
            ILSpy::Decompiler::TypeSystem::FindType(
                compilation_, static_cast<ILSpy::Decompiler::TypeSystem::TypeCode>(raw)));
        nullableParameters_[static_cast<std::size_t>(
            raw - static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Boolean))] =
            std::make_shared<ILSpy::Decompiler::TypeSystem::Implementation::DefaultParameter>(
                std::move(type), std::string());
    }
}

// The C# `IParameter MakeParameter(TypeCode code)` (line 89):
// `return normalParameters[code - TypeCode.Object];` -- the plain table lookup (the
// table's TypeCode.Object..String range covers every code the operator tables ask for).
std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>
CSharpOperators::MakeParameter(ILSpy::Decompiler::TypeSystem::TypeCode code) const
{
    return normalParameters_[static_cast<std::size_t>(
        static_cast<int>(code)
        - static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Object))];
}

// The C# `IParameter MakeNullableParameter(IParameter normalParameter)` (lines 91-100):
// `for (TypeCode i = TypeCode.Boolean; i <= TypeCode.Decimal; i++) { if (normalParameter
// == normalParameters[i - TypeCode.Object]) return nullableParameters[i -
// TypeCode.Boolean]; } throw new ArgumentException();` -- the `==` is C# REFERENCE
// equality, so the port compares addresses. The index asymmetry is the faithful C#:
// the NORMAL table is indexed relative to TypeCode.Object while the loop runs
// TypeCode.Boolean..Decimal, and the NULLABLE table is indexed relative to
// TypeCode.Boolean.
std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter>
CSharpOperators::MakeNullableParameter(
    const ILSpy::Decompiler::TypeSystem::IParameter& normalParameter) const
{
    for (int raw = static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Boolean);
         raw <= static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Decimal); ++raw)
    {
        if (&normalParameter
            == normalParameters_[static_cast<std::size_t>(
                raw - static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Object))]
                   .get())
        {
            return nullableParameters_[static_cast<std::size_t>(
                raw - static_cast<int>(ILSpy::Decompiler::TypeSystem::TypeCode::Boolean))];
        }
    }
    // The C# `throw new ArgumentException()`; the port throws std::invalid_argument (the
    // DefaultParameter ArgumentNullException convention).
    throw std::invalid_argument(
        "CSharpOperators.MakeNullableParameter: not a known normal parameter");
}

// The C# `OperatorMethod[] Lift(params OperatorMethod[] methods)` (lines 60-70):
// `List<OperatorMethod> result = new List<OperatorMethod>(methods); foreach
// (OperatorMethod method in methods) { OperatorMethod? lifted = method.Lift(this); if
// (lifted != null) result.Add(lifted); } return result.ToArray();` -- the result starts
// as a copy of ALL the original methods, then each lifted form is appended after the
// loop has visited its original: the lifted forms come after all the originals, in
// iteration order.
std::vector<std::shared_ptr<OperatorMethod>> CSharpOperators::Lift(
    const std::vector<std::shared_ptr<OperatorMethod>>& methods) const
{
    std::vector<std::shared_ptr<OperatorMethod>> result(methods);
    for (const auto& method : methods)
    {
        std::shared_ptr<OperatorMethod> lifted = method->Lift(*this);
        if (lifted)
            result.push_back(std::move(lifted));
    }
    return result;
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
