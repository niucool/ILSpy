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
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ILSpy::Decompiler::CSharp::Resolver {

// The C# `NullableType.Create` free function lives directly in the TypeSystem namespace
// (the C# static class is a label, not a namespace -- the iteration-37 convention).
using ILSpy::Decompiler::TypeSystem::Create;
// The TypeCode of the operator-table entries (the .cpp tables reference it repeatedly;
// the sibling TypeSystem namespace is not searched from inside this namespace).
using ILSpy::Decompiler::TypeSystem::TypeCode;

namespace {

// ---------------------------------------------------------------------------
// The checked/unchecked arithmetic bodies the binary operator tables store (the C#
// `(a, b) => checked(a * b)` / `(a, b) => unchecked(a * b)` lambda bodies, convention
// (j): stored by the ctors, consumed by the deferred `Invoke`). The C# `checked`
// context throws OverflowException when the true result leaves the result type's
// range; the `unchecked` context wraps (two's complement for the signed integrals).
// Integer division/remainder by zero throws DivideByZeroException in BOTH contexts
// (only the MinValue / -1 edge differs: an OverflowException checked, the wrap
// unchecked). The floating-point arithmetic is unaffected by the context (the two
// bodies coincide); the unsigned integrals promote exactly as C++ defines, so only
// the checked halves need explicit overflow tests. Every wrap goes through unsigned
// arithmetic -- the well-defined two's-complement wrap without the C++ signed-overflow
// UB (the uint64 -> int64 narrowing of a wrapped product is implementation-defined
// before C++20; MSVC defines the two's-complement wrap -- the C# unchecked result).
// ---------------------------------------------------------------------------

[[noreturn]] void ThrowOverflow()
{
    throw std::runtime_error("OverflowException");
}

[[noreturn]] void ThrowDivideByZero()
{
    throw std::runtime_error("DivideByZeroException");
}

// --- Multiplication (the C# 4.0 spec 7.8.1) ---

// checked(a * b) for int32: the true product always fits int64 -- throw when it leaves
// the int32 range.
std::int32_t CheckedMultiply(std::int32_t a, std::int32_t b)
{
    const std::int64_t product = static_cast<std::int64_t>(a) * static_cast<std::int64_t>(b);
    if (product < std::numeric_limits<std::int32_t>::min()
        || product > std::numeric_limits<std::int32_t>::max())
        ThrowOverflow();
    return static_cast<std::int32_t>(product);
}

// unchecked(a * b) for int32: the two's-complement wrap.
std::int32_t UncheckedMultiply(std::int32_t a, std::int32_t b)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a)
                                     * static_cast<std::uint32_t>(b));
}

// checked(a * b) for uint32: the true product always fits uint64.
std::uint32_t CheckedMultiply(std::uint32_t a, std::uint32_t b)
{
    const std::uint64_t product = static_cast<std::uint64_t>(a) * static_cast<std::uint64_t>(b);
    if (product > std::numeric_limits<std::uint32_t>::max())
        ThrowOverflow();
    return static_cast<std::uint32_t>(product);
}

// unchecked(a * b) for uint32: the C++ unsigned wrap IS the C# unchecked wrap.
std::uint32_t UncheckedMultiply(std::uint32_t a, std::uint32_t b)
{
    return a * b;
}

// checked(a * b) for int64: the exact division-based overflow test (the true product
// needs 128 bits; every division below is safe -- INT64_MIN is only ever divided by a
// positive operand, INT64_MAX by any). a * b overflows int64 iff:
//   a > 0 && b > 0: a > INT64_MAX / b
//   a > 0 && b < 0: b < INT64_MIN / a
//   a < 0 && b > 0: a < INT64_MIN / b
//   a < 0 && b < 0: a < INT64_MAX / b
// (the C++ divisions truncate toward zero and the comparisons stay exact: no integer
// falls strictly between the real quotient and its truncation, so each test detects
// exactly the overflowing products).
std::int64_t CheckedMultiply(std::int64_t a, std::int64_t b)
{
    if (a != 0 && b != 0)
    {
        if (a > 0)
        {
            if (b > 0)
            {
                if (a > std::numeric_limits<std::int64_t>::max() / b)
                    ThrowOverflow();
            }
            else if (b < std::numeric_limits<std::int64_t>::min() / a)
                ThrowOverflow();
        }
        else
        {
            if (b > 0)
            {
                if (a < std::numeric_limits<std::int64_t>::min() / b)
                    ThrowOverflow();
            }
            else if (a < std::numeric_limits<std::int64_t>::max() / b)
                ThrowOverflow();
        }
    }
    return a * b;  // no overflow: the product is representable
}

// unchecked(a * b) for int64: the two's-complement wrap.
std::int64_t UncheckedMultiply(std::int64_t a, std::int64_t b)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a)
                                     * static_cast<std::uint64_t>(b));
}

// checked(a * b) for uint64: the truncated product divides back exactly unless it
// wrapped (b != 0).
std::uint64_t CheckedMultiply(std::uint64_t a, std::uint64_t b)
{
    const std::uint64_t product = a * b;
    if (b != 0 && product / b != a)
        ThrowOverflow();
    return product;
}

std::uint64_t UncheckedMultiply(std::uint64_t a, std::uint64_t b)
{
    return a * b;
}

// --- Division (the C# 4.0 spec 7.8.2) ---

// checked(a / b) for int32: divide-by-zero always throws (both contexts); the
// INT32_MIN / -1 edge throws OverflowException checked.
std::int32_t CheckedDivide(std::int32_t a, std::int32_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    if (a == std::numeric_limits<std::int32_t>::min() && b == -1)
        ThrowOverflow();
    return a / b;
}

// unchecked(a / b) for int32: the INT32_MIN / -1 edge wraps back to INT32_MIN (the
// 2^31 quotient truncated to 32 bits -- the C++ `a / b` would be UB there, so the edge
// is special-cased before the division).
std::int32_t UncheckedDivide(std::int32_t a, std::int32_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    if (a == std::numeric_limits<std::int32_t>::min() && b == -1)
        return std::numeric_limits<std::int32_t>::min();
    return a / b;
}

// a / b for uint32 (no checked/unchecked distinction: unsigned division cannot
// overflow, and divide-by-zero throws in both contexts).
std::uint32_t Divide(std::uint32_t a, std::uint32_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    return a / b;
}

// checked(a / b) for int64 (the INT64_MIN / -1 edge mirrors the int32 pair).
std::int64_t CheckedDivide(std::int64_t a, std::int64_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
        ThrowOverflow();
    return a / b;
}

std::int64_t UncheckedDivide(std::int64_t a, std::int64_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
        return std::numeric_limits<std::int64_t>::min();
    return a / b;
}

std::uint64_t Divide(std::uint64_t a, std::uint64_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    return a / b;
}

// --- Remainder (the C# 4.0 spec 7.8.3) ---

// a % b (no checked/unchecked distinction: the remainder never overflows and
// divide-by-zero throws in both contexts). The MinValue % -1 == 0 edge is special-cased
// because the C++ `a % b` would be UB there (the quotient overflows).
std::int32_t Remainder(std::int32_t a, std::int32_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    if (a == std::numeric_limits<std::int32_t>::min() && b == -1)
        return 0;
    return a % b;
}

std::uint32_t Remainder(std::uint32_t a, std::uint32_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    return a % b;
}

std::int64_t Remainder(std::int64_t a, std::int64_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    if (a == std::numeric_limits<std::int64_t>::min() && b == -1)
        return 0;
    return a % b;
}

std::uint64_t Remainder(std::uint64_t a, std::uint64_t b)
{
    if (b == 0)
        ThrowDivideByZero();
    return a % b;
}

// --- Addition (the C# 4.0 spec 7.8.3) ---

std::int32_t CheckedAdd(std::int32_t a, std::int32_t b)
{
    const std::int64_t sum = static_cast<std::int64_t>(a) + static_cast<std::int64_t>(b);
    if (sum < std::numeric_limits<std::int32_t>::min()
        || sum > std::numeric_limits<std::int32_t>::max())
        ThrowOverflow();
    return static_cast<std::int32_t>(sum);
}

std::int32_t UncheckedAdd(std::int32_t a, std::int32_t b)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a)
                                    + static_cast<std::uint32_t>(b));
}

std::uint32_t CheckedAdd(std::uint32_t a, std::uint32_t b)
{
    if (a > std::numeric_limits<std::uint32_t>::max() - b)
        ThrowOverflow();
    return a + b;
}

std::uint32_t UncheckedAdd(std::uint32_t a, std::uint32_t b)
{
    return a + b;
}

std::int64_t CheckedAdd(std::int64_t a, std::int64_t b)
{
    if ((b > 0 && a > std::numeric_limits<std::int64_t>::max() - b)
        || (b < 0 && a < std::numeric_limits<std::int64_t>::min() - b))
        ThrowOverflow();
    return a + b;
}

std::int64_t UncheckedAdd(std::int64_t a, std::int64_t b)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a)
                                    + static_cast<std::uint64_t>(b));
}

std::uint64_t CheckedAdd(std::uint64_t a, std::uint64_t b)
{
    if (a > std::numeric_limits<std::uint64_t>::max() - b)
        ThrowOverflow();
    return a + b;
}

std::uint64_t UncheckedAdd(std::uint64_t a, std::uint64_t b)
{
    return a + b;
}

// --- Subtraction (the C# 4.0 spec 7.8.4) ---

std::int32_t CheckedSubtract(std::int32_t a, std::int32_t b)
{
    const std::int64_t difference = static_cast<std::int64_t>(a) - static_cast<std::int64_t>(b);
    if (difference < std::numeric_limits<std::int32_t>::min()
        || difference > std::numeric_limits<std::int32_t>::max())
        ThrowOverflow();
    return static_cast<std::int32_t>(difference);
}

std::int32_t UncheckedSubtract(std::int32_t a, std::int32_t b)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a)
                                    - static_cast<std::uint32_t>(b));
}

std::uint32_t CheckedSubtract(std::uint32_t a, std::uint32_t b)
{
    if (a < b)
        ThrowOverflow();
    return a - b;
}

std::uint32_t UncheckedSubtract(std::uint32_t a, std::uint32_t b)
{
    return a - b;
}

std::int64_t CheckedSubtract(std::int64_t a, std::int64_t b)
{
    if ((b > 0 && a < std::numeric_limits<std::int64_t>::min() + b)
        || (b < 0 && a > std::numeric_limits<std::int64_t>::max() + b))
        ThrowOverflow();
    return a - b;
}

std::int64_t UncheckedSubtract(std::int64_t a, std::int64_t b)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a)
                                    - static_cast<std::uint64_t>(b));
}

std::uint64_t CheckedSubtract(std::uint64_t a, std::uint64_t b)
{
    if (a < b)
        ThrowOverflow();
    return a - b;
}

std::uint64_t UncheckedSubtract(std::uint64_t a, std::uint64_t b)
{
    return a - b;
}

// --- Shifts (the C# 4.0 spec 7.8.5) ---

// The C# shift operators mask the shift count (& 31 for the 32-bit types, & 63 for the
// 64-bit ones -- any count is valid, negative counts included, the same two's-complement
// masking C++ `&` performs); a shift never overflows (the tables use the single-func
// ctor). The signed LEFT shifts go through the unsigned pattern -- the C# `a << b`
// discards the high bits of the 32/64-bit pattern -- and the signed RIGHT shifts are
// the arithmetic shifts (implementation-defined before C++20; MSVC defines the
// arithmetic/sign-extending shift, the C# `>>` for the signed integrals).

std::int32_t ShiftLeft(std::int32_t a, std::int32_t b)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) << (b & 31));
}

std::uint32_t ShiftLeft(std::uint32_t a, std::int32_t b)
{
    return a << (b & 31);
}

std::int64_t ShiftLeft(std::int64_t a, std::int32_t b)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) << (b & 63));
}

std::uint64_t ShiftLeft(std::uint64_t a, std::int32_t b)
{
    return a << (b & 63);
}

std::int32_t ShiftRight(std::int32_t a, std::int32_t b)
{
    return a >> (b & 31);
}

std::uint32_t ShiftRight(std::uint32_t a, std::int32_t b)
{
    return a >> (b & 31);
}

std::int64_t ShiftRight(std::int64_t a, std::int32_t b)
{
    return a >> (b & 63);
}

std::uint64_t ShiftRight(std::uint64_t a, std::int32_t b)
{
    return a >> (b & 63);
}

// The C# 11 `>>>` operator: the logical (zero-filling) right shift for the signed types
// (identical to the ordinary right shift for the unsigned ones).

std::int32_t UnsignedShiftRight(std::int32_t a, std::int32_t b)
{
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) >> (b & 31));
}

std::uint32_t UnsignedShiftRight(std::uint32_t a, std::int32_t b)
{
    return a >> (b & 31);
}

std::int64_t UnsignedShiftRight(std::int64_t a, std::int32_t b)
{
    return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) >> (b & 63));
}

std::uint64_t UnsignedShiftRight(std::uint64_t a, std::int32_t b)
{
    return a >> (b & 63);
}

} // namespace

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

// ---------------------------------------------------------------------------
// LiftedUnaryOperatorMethod (the out-of-line members)
// ---------------------------------------------------------------------------

// The C# `public LiftedUnaryOperatorMethod(CSharpOperators operators, UnaryOperatorMethod
// baseMethod) : base(operators.compilation)`: `this.baseMethod = baseMethod; this.ReturnType
// = NullableType.Create(baseMethod.Compilation, baseMethod.ReturnType); parameters.Add(
// operators.MakeNullableParameter(baseMethod.Parameters[0]));` -- the return type and the
// single parameter are both lifted to `Nullable<T>` (the shared nullable parameter-table
// instance, the MakeNullableParameter reference-equality lookup).
LiftedUnaryOperatorMethod::LiftedUnaryOperatorMethod(
    const CSharpOperators& operators, const UnaryOperatorMethod& baseMethod)
    : UnaryOperatorMethod(operators.Compilation()), baseMethod_(&baseMethod)
{
    // `NullableType.Create` is the TypeSystem free function (`using ...TypeSystem::Create`
    // above -- the C# static-class label is not a namespace, the iteration-37 convention).
    returnType_ = Create(baseMethod.Compilation(), baseMethod.ReturnType());
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> baseParameters =
        baseMethod.Parameters();
    // The C# `baseMethod.Parameters[0]` throws IndexOutOfRangeException on an empty list;
    // the port throws std::out_of_range (the same bounds contract -- every real unary
    // operator method has exactly one parameter, so the throw guards only degenerate
    // constructions).
    if (baseParameters.empty())
        throw std::out_of_range("LiftedUnaryOperatorMethod: the base method has no parameters");
    parameters_.push_back(operators.MakeNullableParameter(*baseParameters[0]));
}

// The C# `IReadOnlyList<IParameter> NonLiftedParameters => baseMethod.Parameters` -- the
// by-value snapshot of the pre-lifting parameter list (the ILiftedOperator convention).
std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*>
LiftedUnaryOperatorMethod::NonLiftedParameters() const
{
    return baseMethod_->Parameters();
}

// The C# `IType NonLiftedReturnType => baseMethod.ReturnType`.
const ILSpy::Decompiler::TypeSystem::IType& LiftedUnaryOperatorMethod::NonLiftedReturnType()
    const
{
    return baseMethod_->ReturnType();
}

// ---------------------------------------------------------------------------
// The lazy unary operator-table properties (CSharpOperators.cs lines 300-409)
// ---------------------------------------------------------------------------

// The C# `public OperatorMethod[] UnaryPlusOperators` (the C# 4.0 spec 7.7.1): the seven
// numeric originals (each `i => +i`), then their lifted forms via `Lift` (convention (m):
// compute on first call -- the empty vector is the not-yet-built sentinel).
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::UnaryPlusOperators()
    const
{
    if (unaryPlusOperators_.empty())
    {
        unaryPlusOperators_ = Lift({
            std::make_shared<LambdaUnaryOperatorMethod<std::int32_t>>(
                *this, [](std::int32_t i) { return +i; }),
            std::make_shared<LambdaUnaryOperatorMethod<std::uint32_t>>(
                *this, [](std::uint32_t i) { return +i; }),
            std::make_shared<LambdaUnaryOperatorMethod<std::int64_t>>(
                *this, [](std::int64_t i) { return +i; }),
            std::make_shared<LambdaUnaryOperatorMethod<std::uint64_t>>(
                *this, [](std::uint64_t i) { return +i; }),
            std::make_shared<LambdaUnaryOperatorMethod<float>>(
                *this, [](float i) { return +i; }),
            std::make_shared<LambdaUnaryOperatorMethod<double>>(
                *this, [](double i) { return +i; }),
            std::make_shared<LambdaUnaryOperatorMethod<Decimal>>(
                *this, [](Decimal i) { return +i; }),
        });
    }
    return unaryPlusOperators_;
}

// The C# `public OperatorMethod[] UncheckedUnaryMinusOperators` (the C# 4.0 spec 7.7.2):
// the five signed-and-floating originals, each `i => unchecked(-i)`. The port's unchecked
// integer negation goes through the unsigned subtraction -- the two's-complement wrap the
// C# `unchecked` context defines (only INT32_MIN/INT64_MIN negate to themselves), without
// the C++ signed-overflow UB. The stored funcs are consumed by the deferred `Invoke`
// (convention (j)).
const std::vector<std::shared_ptr<OperatorMethod>>&
CSharpOperators::UncheckedUnaryMinusOperators() const
{
    if (uncheckedUnaryMinusOperators_.empty())
    {
        uncheckedUnaryMinusOperators_ = Lift({
            std::make_shared<LambdaUnaryOperatorMethod<std::int32_t>>(
                *this, [](std::int32_t i) {
                    return static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(i));
                }),
            std::make_shared<LambdaUnaryOperatorMethod<std::int64_t>>(
                *this, [](std::int64_t i) {
                    return static_cast<std::int64_t>(0ull - static_cast<std::uint64_t>(i));
                }),
            std::make_shared<LambdaUnaryOperatorMethod<float>>(
                *this, [](float i) { return -i; }),
            std::make_shared<LambdaUnaryOperatorMethod<double>>(
                *this, [](double i) { return -i; }),
            std::make_shared<LambdaUnaryOperatorMethod<Decimal>>(
                *this, [](Decimal i) { return -i; }),
        });
    }
    return uncheckedUnaryMinusOperators_;
}

// The C# `public OperatorMethod[] CheckedUnaryMinusOperators`: the same five originals,
// each `checked(-i)`. The C# `checked` context throws OverflowException when the negation
// overflows (only INT32_MIN/INT64_MIN, whose negation does not fit the type); the port's
// boundary throw is std::runtime_error (the runtime-exception convention -- the future
// CSharpResolver call site wraps `Invoke` in `catch (ArithmeticException)`, the port-side
// exception the resolver slice will settle). The floating/decimal negations never overflow.
const std::vector<std::shared_ptr<OperatorMethod>>&
CSharpOperators::CheckedUnaryMinusOperators() const
{
    if (checkedUnaryMinusOperators_.empty())
    {
        checkedUnaryMinusOperators_ = Lift({
            std::make_shared<LambdaUnaryOperatorMethod<std::int32_t>>(
                *this, [](std::int32_t i) {
                    if (i == std::numeric_limits<std::int32_t>::min())
                        throw std::runtime_error("OverflowException");
                    return -i;
                }),
            std::make_shared<LambdaUnaryOperatorMethod<std::int64_t>>(
                *this, [](std::int64_t i) {
                    if (i == std::numeric_limits<std::int64_t>::min())
                        throw std::runtime_error("OverflowException");
                    return -i;
                }),
            std::make_shared<LambdaUnaryOperatorMethod<float>>(
                *this, [](float i) { return -i; }),
            std::make_shared<LambdaUnaryOperatorMethod<double>>(
                *this, [](double i) { return -i; }),
            std::make_shared<LambdaUnaryOperatorMethod<Decimal>>(
                *this, [](Decimal i) { return -i; }),
        });
    }
    return checkedUnaryMinusOperators_;
}

// The C# `public OperatorMethod[] LogicalNegationOperators` (the C# spec draft-v11 12.9.4):
// the single bool original (`b => !b`), then its lifted `Nullable<bool>` form.
const std::vector<std::shared_ptr<OperatorMethod>>&
CSharpOperators::LogicalNegationOperators() const
{
    if (logicalNegationOperators_.empty())
    {
        logicalNegationOperators_ = Lift({
            std::make_shared<LambdaUnaryOperatorMethod<bool>>(
                *this, [](bool b) { return !b; }),
        });
    }
    return logicalNegationOperators_;
}

// The C# `public OperatorMethod[] BitwiseComplementOperators` (the C# 4.0 spec 7.7.4): the
// four integer originals (each `i => ~i`), then their lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>&
CSharpOperators::BitwiseComplementOperators() const
{
    if (bitwiseComplementOperators_.empty())
    {
        bitwiseComplementOperators_ = Lift({
            std::make_shared<LambdaUnaryOperatorMethod<std::int32_t>>(
                *this, [](std::int32_t i) { return ~i; }),
            std::make_shared<LambdaUnaryOperatorMethod<std::uint32_t>>(
                *this, [](std::uint32_t i) { return ~i; }),
            std::make_shared<LambdaUnaryOperatorMethod<std::int64_t>>(
                *this, [](std::int64_t i) { return ~i; }),
            std::make_shared<LambdaUnaryOperatorMethod<std::uint64_t>>(
                *this, [](std::uint64_t i) { return ~i; }),
        });
    }
    return bitwiseComplementOperators_;
}

// ---------------------------------------------------------------------------
// LiftedBinaryOperatorMethod (the out-of-line members)
// ---------------------------------------------------------------------------

// The C# `public LiftedBinaryOperatorMethod(CSharpOperators operators, BinaryOperatorMethod
// baseMethod) : base(operators.compilation)`: `this.baseMethod = baseMethod;
// this.ReturnType = NullableType.Create(operators.compilation, baseMethod.ReturnType);
// parameters.Add(operators.MakeNullableParameter(baseMethod.Parameters[0]));
// parameters.Add(operators.MakeNullableParameter(baseMethod.Parameters[1]));` -- the return
// type and both parameters lifted to their `Nullable<T>` counterparts (the shared
// nullable parameter-table instances, the MakeNullableParameter reference-equality
// lookup).
LiftedBinaryOperatorMethod::LiftedBinaryOperatorMethod(
    const CSharpOperators& operators, const BinaryOperatorMethod& baseMethod)
    : BinaryOperatorMethod(operators.Compilation()), baseMethod_(&baseMethod)
{
    // `NullableType.Create` is the TypeSystem free function (the `using ...TypeSystem::Create`
    // above -- the C# static-class label is not a namespace, the iteration-37 convention).
    returnType_ = Create(operators.Compilation(), baseMethod.ReturnType());
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> baseParameters =
        baseMethod.Parameters();
    // The C# `baseMethod.Parameters[0]` / `Parameters[1]` throw IndexOutOfRangeException on
    // a short list; the port throws std::out_of_range (the LiftedUnaryOperatorMethod
    // bounds contract -- every real binary operator method has exactly two parameters, so
    // the throw guards only degenerate constructions).
    if (baseParameters.size() < 2)
        throw std::out_of_range(
            "LiftedBinaryOperatorMethod: the base method has fewer than two parameters");
    parameters_.push_back(operators.MakeNullableParameter(*baseParameters[0]));
    parameters_.push_back(operators.MakeNullableParameter(*baseParameters[1]));
}

// The C# `IReadOnlyList<IParameter> NonLiftedParameters => baseMethod.Parameters` -- the
// by-value snapshot of the pre-lifting parameter list (the ILiftedOperator convention).
std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*>
LiftedBinaryOperatorMethod::NonLiftedParameters() const
{
    return baseMethod_->Parameters();
}

// The C# `IType NonLiftedReturnType => baseMethod.ReturnType`.
const ILSpy::Decompiler::TypeSystem::IType& LiftedBinaryOperatorMethod::NonLiftedReturnType()
    const
{
    return baseMethod_->ReturnType();
}

// ---------------------------------------------------------------------------
// The lazy binary operator-table properties (CSharpOperators.cs lines 484-695)
// ---------------------------------------------------------------------------

// The C# `public OperatorMethod[] MultiplicationOperators` (the C# 4.0 spec 7.8.1): the
// seven numeric originals (int, uint, long, ulong, float, double, decimal -- each the
// checked/unchecked multiply pair), then their lifted forms via `Lift` (convention (m):
// compute on first call -- the empty vector is the not-yet-built sentinel). The
// floating-point bodies coincide in both contexts (the C# `checked` context does not
// apply to floating-point arithmetic).
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::MultiplicationOperators()
    const
{
    if (multiplicationOperators_.empty())
    {
        multiplicationOperators_ = Lift({
            std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                *this,
                [](std::int32_t a, std::int32_t b) { return CheckedMultiply(a, b); },
                [](std::int32_t a, std::int32_t b) { return UncheckedMultiply(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::uint32_t>>(
                *this,
                [](std::uint32_t a, std::uint32_t b) { return CheckedMultiply(a, b); },
                [](std::uint32_t a, std::uint32_t b) { return UncheckedMultiply(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::int64_t, std::int64_t>>(
                *this,
                [](std::int64_t a, std::int64_t b) { return CheckedMultiply(a, b); },
                [](std::int64_t a, std::int64_t b) { return UncheckedMultiply(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint64_t, std::uint64_t>>(
                *this,
                [](std::uint64_t a, std::uint64_t b) { return CheckedMultiply(a, b); },
                [](std::uint64_t a, std::uint64_t b) { return UncheckedMultiply(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<float, float>>(
                *this,
                [](float a, float b) { return a * b; },
                [](float a, float b) { return a * b; }),
            std::make_shared<LambdaBinaryOperatorMethod<double, double>>(
                *this,
                [](double a, double b) { return a * b; },
                [](double a, double b) { return a * b; }),
            std::make_shared<LambdaBinaryOperatorMethod<Decimal, Decimal>>(
                *this,
                [](Decimal a, Decimal b) { return a * b; },
                [](Decimal a, Decimal b) { return a * b; }),
        });
    }
    return multiplicationOperators_;
}

// The C# `public OperatorMethod[] DivisionOperators` (the C# 4.0 spec 7.8.2): the same
// seven originals with the division bodies (divide-by-zero throws in both contexts; the
// floating-point division by zero yields the IEEE infinity/NaN, no throw -- the C#
// floating-point semantics), then their lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::DivisionOperators()
    const
{
    if (divisionOperators_.empty())
    {
        divisionOperators_ = Lift({
            std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                *this,
                [](std::int32_t a, std::int32_t b) { return CheckedDivide(a, b); },
                [](std::int32_t a, std::int32_t b) { return UncheckedDivide(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::uint32_t>>(
                *this,
                [](std::uint32_t a, std::uint32_t b) { return Divide(a, b); },
                [](std::uint32_t a, std::uint32_t b) { return Divide(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::int64_t, std::int64_t>>(
                *this,
                [](std::int64_t a, std::int64_t b) { return CheckedDivide(a, b); },
                [](std::int64_t a, std::int64_t b) { return UncheckedDivide(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint64_t, std::uint64_t>>(
                *this,
                [](std::uint64_t a, std::uint64_t b) { return Divide(a, b); },
                [](std::uint64_t a, std::uint64_t b) { return Divide(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<float, float>>(
                *this,
                [](float a, float b) { return a / b; },
                [](float a, float b) { return a / b; }),
            std::make_shared<LambdaBinaryOperatorMethod<double, double>>(
                *this,
                [](double a, double b) { return a / b; },
                [](double a, double b) { return a / b; }),
            std::make_shared<LambdaBinaryOperatorMethod<Decimal, Decimal>>(
                *this,
                [](Decimal a, Decimal b) { return a / b; },
                [](Decimal a, Decimal b) { return a / b; }),
        });
    }
    return divisionOperators_;
}

// The C# `public OperatorMethod[] RemainderOperators` (the C# 4.0 spec 7.8.3): the same
// seven originals with the remainder bodies (the integer remainder never overflows, so
// the checked/unchecked bodies coincide; the floating-point remainder is the C# `%`
// semantics -- x - y * trunc(x / y), the std::fmod operation), then their lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::RemainderOperators()
    const
{
    if (remainderOperators_.empty())
    {
        remainderOperators_ = Lift({
            std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                *this,
                [](std::int32_t a, std::int32_t b) { return Remainder(a, b); },
                [](std::int32_t a, std::int32_t b) { return Remainder(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::uint32_t>>(
                *this,
                [](std::uint32_t a, std::uint32_t b) { return Remainder(a, b); },
                [](std::uint32_t a, std::uint32_t b) { return Remainder(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::int64_t, std::int64_t>>(
                *this,
                [](std::int64_t a, std::int64_t b) { return Remainder(a, b); },
                [](std::int64_t a, std::int64_t b) { return Remainder(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint64_t, std::uint64_t>>(
                *this,
                [](std::uint64_t a, std::uint64_t b) { return Remainder(a, b); },
                [](std::uint64_t a, std::uint64_t b) { return Remainder(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<float, float>>(
                *this,
                [](float a, float b) { return std::fmod(a, b); },
                [](float a, float b) { return std::fmod(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<double, double>>(
                *this,
                [](double a, double b) { return std::fmod(a, b); },
                [](double a, double b) { return std::fmod(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<Decimal, Decimal>>(
                *this,
                [](Decimal a, Decimal b) { return a % b; },
                [](Decimal a, Decimal b) { return a % b; }),
        });
    }
    return remainderOperators_;
}

// The C# `public OperatorMethod[] AdditionOperators` (the C# 4.0 spec 7.8.3): the seven
// numeric originals, then the three built-in string concatenations (`string + string`,
// `string + object`, `object + string`), then the seven lifted numeric forms (the
// StringConcatenation entries are NOT lifted -- the inherited `Lift` returns null and
// `CSharpOperators::Lift` skips the null lifted forms).
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::AdditionOperators()
    const
{
    if (additionOperators_.empty())
    {
        additionOperators_ = Lift({
            std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                *this,
                [](std::int32_t a, std::int32_t b) { return CheckedAdd(a, b); },
                [](std::int32_t a, std::int32_t b) { return UncheckedAdd(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::uint32_t>>(
                *this,
                [](std::uint32_t a, std::uint32_t b) { return CheckedAdd(a, b); },
                [](std::uint32_t a, std::uint32_t b) { return UncheckedAdd(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::int64_t, std::int64_t>>(
                *this,
                [](std::int64_t a, std::int64_t b) { return CheckedAdd(a, b); },
                [](std::int64_t a, std::int64_t b) { return UncheckedAdd(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint64_t, std::uint64_t>>(
                *this,
                [](std::uint64_t a, std::uint64_t b) { return CheckedAdd(a, b); },
                [](std::uint64_t a, std::uint64_t b) { return UncheckedAdd(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<float, float>>(
                *this,
                [](float a, float b) { return a + b; },
                [](float a, float b) { return a + b; }),
            std::make_shared<LambdaBinaryOperatorMethod<double, double>>(
                *this,
                [](double a, double b) { return a + b; },
                [](double a, double b) { return a + b; }),
            std::make_shared<LambdaBinaryOperatorMethod<Decimal, Decimal>>(
                *this,
                [](Decimal a, Decimal b) { return a + b; },
                [](Decimal a, Decimal b) { return a + b; }),
            std::make_shared<StringConcatenation>(
                *this, TypeCode::String, TypeCode::String),
            std::make_shared<StringConcatenation>(
                *this, TypeCode::String, TypeCode::Object),
            std::make_shared<StringConcatenation>(
                *this, TypeCode::Object, TypeCode::String),
        });
    }
    return additionOperators_;
}

// The C# `public OperatorMethod[] SubtractionOperators` (the C# 4.0 spec 7.8.4): the
// seven numeric originals with the subtraction bodies, then their lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::SubtractionOperators()
    const
{
    if (subtractionOperators_.empty())
    {
        subtractionOperators_ = Lift({
            std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                *this,
                [](std::int32_t a, std::int32_t b) { return CheckedSubtract(a, b); },
                [](std::int32_t a, std::int32_t b) { return UncheckedSubtract(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::uint32_t>>(
                *this,
                [](std::uint32_t a, std::uint32_t b) { return CheckedSubtract(a, b); },
                [](std::uint32_t a, std::uint32_t b) { return UncheckedSubtract(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::int64_t, std::int64_t>>(
                *this,
                [](std::int64_t a, std::int64_t b) { return CheckedSubtract(a, b); },
                [](std::int64_t a, std::int64_t b) { return UncheckedSubtract(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint64_t, std::uint64_t>>(
                *this,
                [](std::uint64_t a, std::uint64_t b) { return CheckedSubtract(a, b); },
                [](std::uint64_t a, std::uint64_t b) { return UncheckedSubtract(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<float, float>>(
                *this,
                [](float a, float b) { return a - b; },
                [](float a, float b) { return a - b; }),
            std::make_shared<LambdaBinaryOperatorMethod<double, double>>(
                *this,
                [](double a, double b) { return a - b; },
                [](double a, double b) { return a - b; }),
            std::make_shared<LambdaBinaryOperatorMethod<Decimal, Decimal>>(
                *this,
                [](Decimal a, Decimal b) { return a - b; },
                [](Decimal a, Decimal b) { return a - b; }),
        });
    }
    return subtractionOperators_;
}

// The C# `public OperatorMethod[] ShiftLeftOperators` (the C# 4.0 spec 7.8.5): the four
// originals (int, uint, long, ulong -- each shifting by an int count, the SINGLE-FUNC
// ctor: a shift never overflows, so there is no checked/unchecked distinction), then
// their lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::ShiftLeftOperators()
    const
{
    if (shiftLeftOperators_.empty())
    {
        shiftLeftOperators_ = Lift({
            std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                *this, [](std::int32_t a, std::int32_t b) { return ShiftLeft(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::int32_t>>(
                *this, [](std::uint32_t a, std::int32_t b) { return ShiftLeft(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::int64_t, std::int32_t>>(
                *this, [](std::int64_t a, std::int32_t b) { return ShiftLeft(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint64_t, std::int32_t>>(
                *this, [](std::uint64_t a, std::int32_t b) { return ShiftLeft(a, b); }),
        });
    }
    return shiftLeftOperators_;
}

// The C# `public OperatorMethod[] ShiftRightOperators`: the same four originals with the
// right-shift bodies, then their lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::ShiftRightOperators()
    const
{
    if (shiftRightOperators_.empty())
    {
        shiftRightOperators_ = Lift({
            std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                *this, [](std::int32_t a, std::int32_t b) { return ShiftRight(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::int32_t>>(
                *this, [](std::uint32_t a, std::int32_t b) { return ShiftRight(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::int64_t, std::int32_t>>(
                *this, [](std::int64_t a, std::int32_t b) { return ShiftRight(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint64_t, std::int32_t>>(
                *this, [](std::uint64_t a, std::int32_t b) { return ShiftRight(a, b); }),
        });
    }
    return shiftRightOperators_;
}

// The C# `public OperatorMethod[] UnsignedShiftRightOperators` (the C# 11 `>>>`
// operator): the same four originals with the zero-filling right-shift bodies (the
// signed types reinterpret their pattern unsigned first -- `(int)((uint)a >> b)` and
// the 64-bit mirror), then their lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>&
CSharpOperators::UnsignedShiftRightOperators() const
{
    if (unsignedShiftRightOperators_.empty())
    {
        unsignedShiftRightOperators_ = Lift({
            std::make_shared<LambdaBinaryOperatorMethod<std::int32_t, std::int32_t>>(
                *this,
                [](std::int32_t a, std::int32_t b) { return UnsignedShiftRight(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint32_t, std::int32_t>>(
                *this,
                [](std::uint32_t a, std::int32_t b) { return UnsignedShiftRight(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::int64_t, std::int32_t>>(
                *this,
                [](std::int64_t a, std::int32_t b) { return UnsignedShiftRight(a, b); }),
            std::make_shared<LambdaBinaryOperatorMethod<std::uint64_t, std::int32_t>>(
                *this,
                [](std::uint64_t a, std::int32_t b) { return UnsignedShiftRight(a, b); }),
        });
    }
    return unsignedShiftRightOperators_;
}

// ---------------------------------------------------------------------------
// The equality operator region (CSharpOperators.cs lines 753-786): the out-of-line
// LiftedEqualityOperatorMethod members
// ---------------------------------------------------------------------------

// The C# `public LiftedEqualityOperatorMethod(CSharpOperators operators,
// EqualityOperatorMethod baseMethod) : base(operators.compilation)`: `this.baseMethod =
// baseMethod; this.ReturnType = baseMethod.ReturnType; IParameter p =
// operators.MakeNullableParameter(baseMethod.Parameters[0]); parameters.Add(p);
// parameters.Add(p);` -- the return type STAYS the base's plain Boolean (a lifted
// comparison still produces a definite bool), and the SAME shared nullable parameter
// instance is added for both operands (the C# `p` local added twice). The owning
// return-type handle is recovered from the base's accessor through `shared_from_this()` +
// `const_pointer_cast` (the D529 convention -- the base ctor always assigns the registered
// Boolean).
LiftedEqualityOperatorMethod::LiftedEqualityOperatorMethod(
    const CSharpOperators& operators, const EqualityOperatorMethod& baseMethod)
    : BinaryOperatorMethod(operators.Compilation()), baseMethod_(&baseMethod)
{
    const ILSpy::Decompiler::TypeSystem::IType& returnType = baseMethod.ReturnType();
    returnType_ = std::const_pointer_cast<ILSpy::Decompiler::TypeSystem::IType>(
        returnType.shared_from_this());
    std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*> baseParameters =
        baseMethod.Parameters();
    // The C# `baseMethod.Parameters[0]` throws IndexOutOfRangeException on an empty list;
    // the port throws std::out_of_range (the LiftedUnaryOperatorMethod bounds contract --
    // every real equality operator method has exactly two parameters, so the throw
    // guards only degenerate constructions).
    if (baseParameters.empty())
        throw std::out_of_range("LiftedEqualityOperatorMethod: the base method has no parameters");
    std::shared_ptr<const ILSpy::Decompiler::TypeSystem::IParameter> p =
        operators.MakeNullableParameter(*baseParameters[0]);
    parameters_.push_back(p);
    parameters_.push_back(p);
}

// The C# `public override bool CanEvaluateAtCompileTime =>
// baseMethod.CanEvaluateAtCompileTime` -- the delegation (virtual dispatch on the base
// method's concrete type).
bool LiftedEqualityOperatorMethod::CanEvaluateAtCompileTime() const
{
    return baseMethod_->CanEvaluateAtCompileTime();
}

// The C# `IReadOnlyList<IParameter> NonLiftedParameters => baseMethod.Parameters` -- the
// by-value snapshot of the pre-lifting parameter list (the ILiftedOperator convention).
std::vector<const ILSpy::Decompiler::TypeSystem::IParameter*>
LiftedEqualityOperatorMethod::NonLiftedParameters() const
{
    return baseMethod_->Parameters();
}

// The C# `IType NonLiftedReturnType => baseMethod.ReturnType`.
const ILSpy::Decompiler::TypeSystem::IType& LiftedEqualityOperatorMethod::NonLiftedReturnType()
    const
{
    return baseMethod_->ReturnType();
}

// ---------------------------------------------------------------------------
// The lazy equality operator-table properties (CSharpOperators.cs lines 769-862)
// ---------------------------------------------------------------------------

// The C# `static readonly TypeCode[] valueEqualityOperatorsFor` (lines 769-775) -- the
// TypeCodes the value equality/inequality tables instantiate (the C# 4.0 spec 7.10 value
// equality set: the numeric primitives + Boolean). The C# declares it as a private static
// member; the port keeps it file-local here (the two value tables in this .cpp are its
// only consumers).
const TypeCode valueEqualityOperatorsFor[] = {
    TypeCode::Int32, TypeCode::UInt32,
    TypeCode::Int64, TypeCode::UInt64,
    TypeCode::Single, TypeCode::Double,
    TypeCode::Decimal,
    TypeCode::Boolean
};

// The C# `public OperatorMethod[] ValueEqualityOperators` (the C# 4.0 spec 7.10 value
// equality operator `==`): the eight value-type originals (negate=false), then their
// lifted `Nullable<T>` forms via `Lift` (convention (m): compute on first call -- the
// empty vector is the not-yet-built sentinel).
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::ValueEqualityOperators()
    const
{
    if (valueEqualityOperators_.empty())
    {
        std::vector<std::shared_ptr<OperatorMethod>> originals;
        for (TypeCode code : valueEqualityOperatorsFor)
            originals.push_back(std::make_shared<EqualityOperatorMethod>(*this, code, false));
        valueEqualityOperators_ = Lift(originals);
    }
    return valueEqualityOperators_;
}

// The C# `public OperatorMethod[] ValueInequalityOperators` (the C# 4.0 spec 7.10 value
// inequality operator `!=`): the same eight originals with negate=true, then their
// lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>& CSharpOperators::ValueInequalityOperators()
    const
{
    if (valueInequalityOperators_.empty())
    {
        std::vector<std::shared_ptr<OperatorMethod>> originals;
        for (TypeCode code : valueEqualityOperatorsFor)
            originals.push_back(std::make_shared<EqualityOperatorMethod>(*this, code, true));
        valueInequalityOperators_ = Lift(originals);
    }
    return valueInequalityOperators_;
}

// The C# `public OperatorMethod[] ReferenceEqualityOperators` (the C# 4.0 spec 7.10
// reference equality operator `==`): the Object and String originals (negate=false).
// Neither lifts (the `Lift` guard returns null for the reference-typed Object/String
// operands), so the table is exactly the two originals.
const std::vector<std::shared_ptr<OperatorMethod>>&
CSharpOperators::ReferenceEqualityOperators() const
{
    if (referenceEqualityOperators_.empty())
    {
        referenceEqualityOperators_ = Lift({
            std::make_shared<EqualityOperatorMethod>(*this, TypeCode::Object, false),
            std::make_shared<EqualityOperatorMethod>(*this, TypeCode::String, false),
        });
    }
    return referenceEqualityOperators_;
}

// The C# `public OperatorMethod[] ReferenceInequalityOperators`: the Object and String
// originals with negate=true, no lifted forms.
const std::vector<std::shared_ptr<OperatorMethod>>&
CSharpOperators::ReferenceInequalityOperators() const
{
    if (referenceInequalityOperators_.empty())
    {
        referenceInequalityOperators_ = Lift({
            std::make_shared<EqualityOperatorMethod>(*this, TypeCode::Object, true),
            std::make_shared<EqualityOperatorMethod>(*this, TypeCode::String, true),
        });
    }
    return referenceInequalityOperators_;
}

} // namespace ILSpy::Decompiler::CSharp::Resolver
