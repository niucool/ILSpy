// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
// PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
// HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
// CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
// OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

// Tests for `ByReferenceResolveResult` (cpp/Decompiler/Semantics/ByReferenceResolveResult.hpp,
// the D428 port of ICSharpCode.Decompiler/Semantics/ByReferenceResolveResult.cs) -- the
// resolved expression is a `ref x`, `in x`, or `out x` reference.  This is the fifth
// `Semantics` leaf: the C# source declares two ctors (a public one taking a
// `ResolveResult elementResult` that delegates to an `internal` one taking an `IType
// elementType`, building the base from a `new ByReferenceType(elementType)`), exposes
// `ReferenceKind` / `ElementResult` / `ElementType`, and overrides `GetChildResults`
// (returns the `ElementResult` when non-null) and `ToString` (a custom format with the
// lowercased `ReferenceKind`).  The C++ port additionally overrides `ShallowClone()` (the
// runtime-type-preserving clone, avoiding C++-only slicing).
//
// The tests pin the two-ctor shape (the `internal`-ctor `Type` is a `ByReferenceType`
// wrapping the element; the public ctor delegates and stores the `ElementResult`), the
// `ReferenceKind` accessor across all five enum values, the `ElementType` inner-type
// extraction, the `GetChildResults` one-element-vs-empty crux, the `ToString` lowercased-
// kind format (including the `RefReadOnly` -> "readonlyref" case), the `ShallowClone`
// runtime-type preservation + shared `ElementResult`/`Type` + carried `ReferenceKind`,
// the inherited base defaults the subclass does NOT override (`IsError` /
// `IsCompileTimeConstant` / `ConstantValue`), and the `is_base_of` / not-`final` (the
// C# class is unsealed) class-shape static_asserts.

#include "Decompiler/Semantics/ByReferenceResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/ReferenceKind.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A concrete minimal-port `IType`: `KnownType(Int32)` whose `ReflectionName()` is
// "System.Int32".  Used as the element type in the tests.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

} // namespace

TEST(ByReferenceResolveResultTest, InternalCtorBuildsByReferenceType)
{
    // The C# `internal ByReferenceResolveResult(IType elementType, ReferenceKind kind)
    // : base(new ByReferenceType(elementType))`: the `Type()` is a `ByReferenceType`
    // (Kind == ByReference) wrapping the element; `ElementType()` returns the inner
    // `IType` (the Int32); `ElementResult()` is null (the `internal`-ctor path has no
    // stored `ResolveResult`).
    auto elementType = MakeInt32Type();
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    EXPECT_EQ(rr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::ByReference);
    EXPECT_EQ(&rr.ElementType(), elementType.get());
    EXPECT_EQ(rr.ElementResult(), nullptr);
}

TEST(ByReferenceResolveResultTest, PublicCtorDelegatesAndStoresElementResult)
{
    // The C# `public ByReferenceResolveResult(ResolveResult elementResult, ReferenceKind
    // kind) : this(elementResult.Type, kind)`: delegates to the `internal` ctor with the
    // element result's `Type` (so `Type()` is a `ByReferenceType` wrapping the element
    // result's type), then stores the `elementResult` (so `ElementResult()` returns it).
    auto elementResult = std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(MakeInt32Type());
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementResult,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    EXPECT_EQ(rr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::ByReference);
    EXPECT_EQ(rr.ElementType().ReflectionName(), "System.Int32");
    EXPECT_EQ(rr.ElementResult(), elementResult.get());
}

TEST(ByReferenceResolveResultTest, ReferenceKindAccessorReturnsConfiguredKind)
{
    // The C# `public ReferenceKind ReferenceKind { get; }` returns the ctor-stored
    // `ReferenceKind`.  All five enum values round-trip through the `internal` ctor.
    auto elementType = MakeInt32Type();
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::None).ReferenceKind(),
        ILSpy::Decompiler::TypeSystem::ReferenceKind::None);
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Out).ReferenceKind(),
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Out);
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref).ReferenceKind(),
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::In).ReferenceKind(),
        ILSpy::Decompiler::TypeSystem::ReferenceKind::In);
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::RefReadOnly).ReferenceKind(),
        ILSpy::Decompiler::TypeSystem::ReferenceKind::RefReadOnly);
}

TEST(ByReferenceResolveResultTest, ElementTypeReturnsInnerTypeReflectionName)
{
    // The C# `public IType ElementType => ((ByReferenceType)this.Type).ElementType`:
    // the inner `IType` of the wrapping `ByReferenceType`.  Its `ToString` (the C#
    // `AbstractType.ToString => ReflectionName`) is "System.Int32".
    auto elementType = MakeInt32Type();
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    EXPECT_EQ(rr.ElementType().ReflectionName(), "System.Int32");
}

TEST(ByReferenceResolveResultTest, GetChildResultsReturnsElementResultWhenPresent)
{
    // The C# `override IEnumerable<ResolveResult> GetChildResults()`: returns the
    // `ElementResult` (a one-element snapshot) when non-null.  The public-ctor path
    // stores the `elementResult`, so the snapshot contains it.
    auto elementResult = std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(MakeInt32Type());
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementResult,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], elementResult.get());
}

TEST(ByReferenceResolveResultTest, GetChildResultsEmptyOnInternalCtorPath)
{
    // The C# `GetChildResults` returns `Enumerable.Empty<ResolveResult>()` when
    // `ElementResult` is null (the `internal`-ctor path).  The C++ override returns
    // an empty vector.
    auto elementType = MakeInt32Type();
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    EXPECT_TRUE(rr.GetChildResults().empty());
}

TEST(ByReferenceResolveResultTest, ToStringFormatWithLowercasedKind)
{
    // The C# `override string ToString() => string.Format(..., "[{0} {1} {2}]",
    // GetType().Name, ReferenceKind.ToString().ToLowerInvariant(), ElementType)`: the
    // format is `[ByReferenceResolveResult <lowercased kind> <ElementType>]`.  The
    // `Ref` kind lowercases to "ref"; the `ElementType` `ToString` (ReflectionName)
    // is "System.Int32".
    auto elementType = MakeInt32Type();
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    EXPECT_EQ(rr.ToString(), "[ByReferenceResolveResult ref System.Int32]");
}

TEST(ByReferenceResolveResultTest, ToStringRefReadOnlyLowercasesToReadonlyref)
{
    // The C# `ReferenceKind.RefReadOnly.ToString()` is "RefReadOnly"; ToLowerInvariant
    // yields "readonlyref".  The other kinds lowercased: None -> "none", Out -> "out",
    // Ref -> "ref", In -> "in".
    auto elementType = MakeInt32Type();
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::None).ToString(),
        "[ByReferenceResolveResult none System.Int32]");
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Out).ToString(),
        "[ByReferenceResolveResult out System.Int32]");
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::In).ToString(),
        "[ByReferenceResolveResult in System.Int32]");
    EXPECT_EQ(ILSpy::Decompiler::Semantics::ByReferenceResolveResult(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::RefReadOnly).ToString(),
        "[ByReferenceResolveResult readonlyref System.Int32]");
}

TEST(ByReferenceResolveResultTest, ShallowClonePreservesRuntimeType)
{
    // The C# `ShallowClone` (inherited, uses `MemberwiseClone`) preserves the runtime
    // type, so a cloned `ByReferenceResolveResult` stays a `ByReferenceResolveResult`
    // (not sliced to the `ResolveResult` base).  The C++ override reproduces this: the
    // clone is a `ByReferenceResolveResult` (`dynamic_cast` succeeds).
    auto elementResult = std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(MakeInt32Type());
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementResult,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Out);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::ByReferenceResolveResult*>(clone.get()),
              nullptr);
}

TEST(ByReferenceResolveResultTest, ShallowCloneSharesElementResult)
{
    // The C# `MemberwiseClone` shallow-copies the `ElementResult` field (a reference),
    // so the clone and the original share the same `ElementResult`.  The C++ default
    // copy ctor shares the `elementResult_` `shared_ptr`, so `ElementResult()` returns
    // the same pointer.
    auto elementResult = std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(MakeInt32Type());
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementResult,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Out);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::ByReferenceResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->ElementResult(), elementResult.get());
}

TEST(ByReferenceResolveResultTest, ShallowCloneSharesType)
{
    // The clone shares the `IType` (the `ByReferenceType`) via the base `type_`
    // `shared_ptr` (faithful to `MemberwiseClone`'s reference copy).
    auto elementResult = std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(MakeInt32Type());
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementResult,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(&clone->Type(), &rr.Type());
}

TEST(ByReferenceResolveResultTest, ShallowCloneCarriesReferenceKind)
{
    // The clone copies the `referenceKind_` value by value (the C# `MemberwiseClone`
    // copies value fields).  The `Out` kind survives the clone.
    auto elementResult = std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(MakeInt32Type());
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementResult,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Out);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::ByReferenceResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    EXPECT_EQ(cloned->ReferenceKind(), ILSpy::Decompiler::TypeSystem::ReferenceKind::Out);
}

TEST(ByReferenceResolveResultTest, ShallowCloneIsDistinctInstance)
{
    auto elementResult = std::make_shared<ILSpy::Decompiler::Semantics::ResolveResult>(MakeInt32Type());
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementResult,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    auto clone = rr.ShallowClone();
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(clone.get(), &rr);
}

TEST(ByReferenceResolveResultTest, InheritedDefaultsArePreserved)
{
    // `ByReferenceResolveResult` does NOT override `IsError` /
    // `IsCompileTimeConstant` / `ConstantValue`, so the inherited `ResolveResult` base
    // defaults hold (a `ref` reference is not an error, not a compile-time constant).
    auto elementType = MakeInt32Type();
    ILSpy::Decompiler::Semantics::ByReferenceResolveResult rr(elementType,
        ILSpy::Decompiler::TypeSystem::ReferenceKind::Ref);
    EXPECT_FALSE(rr.IsError());
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
}

TEST(ByReferenceResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult,
                                    ILSpy::Decompiler::Semantics::ByReferenceResolveResult>,
                  "ByReferenceResolveResult derives from ResolveResult.");
    static_assert(std::has_virtual_destructor_v<ILSpy::Decompiler::Semantics::ByReferenceResolveResult>,
                  "ByReferenceResolveResult inherits the virtual destructor (held via base pointers).");
    static_assert(std::is_polymorphic_v<ILSpy::Decompiler::Semantics::ByReferenceResolveResult>,
                  "ByReferenceResolveResult is polymorphic (the ToString/ShallowClone "
                  "overrides dispatch through base pointers).");
    // The C# `ByReferenceResolveResult` is NOT sealed and no C# subclass derives from it,
    // so the C++ port is NOT final (faithful to the unsealed C# class).
    static_assert(!std::is_final_v<ILSpy::Decompiler::Semantics::ByReferenceResolveResult>,
                  "ByReferenceResolveResult is not final (the C# class is unsealed).");
    static_assert(std::is_default_constructible_v<std::unique_ptr<ILSpy::Decompiler::Semantics::ResolveResult>>,
                  "A ResolveResult is held by unique_ptr (the ShallowClone return).");
}
