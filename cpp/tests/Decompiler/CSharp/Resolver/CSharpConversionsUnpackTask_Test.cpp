// Copyright (c) 2026 ILSpy Contributors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of
// this software and associated documentation files (the "Software"), to deal in the
// Software without restriction, including without limitation the rights to use, copy,
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or
// substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for `Detail::UnpackTask` (the port of `CSharpConversions.UnpackTask`, CSharpConversions.cs
// line 1654) and the `BetterConversionTarget` `Task<T>` recursion it wires. `CSharpConversions.
// UnpackTask` is a STRICTER filter than `TaskType.UnpackTask`: it returns `type.TypeArguments[0]`
// only when `type` is a generic task-like (`IsTask(type) || IsCustomTask(type, out _)`) with EXACTLY
// one type parameter, else a null `IType` -- so the non-generic `Task` (0 type params) yields null
// here (NOT `void`), and the `BetterConversionTarget` `s1 != null && s2 != null` recursion fires
// only when BOTH targets are `Task<T>`-shaped.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"  // Detail::UnpackTask, Detail::BetterConversionTarget
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/CustomAttributeNamedArgument.hpp"
#include "Decompiler/TypeSystem/CustomAttributeTypedArgument.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownAttribute.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace R = ILSpy::Decompiler::CSharp::Resolver;
namespace TS = ILSpy::Decompiler::TypeSystem;
using R::Detail::BetterConversionTarget;
using R::Detail::UnpackTask;
using TS::Accessibility;
using TS::CustomAttributeNamedArgument;
using TS::CustomAttributeTypedArgument;
using TS::FullTypeName;
using TS::IAttribute;
using TS::ICompilation;
using TS::IType;
using TS::ITypePtr;
using TS::KnownAttribute;
using TS::KnownType;
using TS::KnownTypeCode;
using TS::ParameterizedType;
using TS::SimpleType;
using TS::TopLevelTypeName;
using TS::TypeKind;
using TS::TestSupport::LookupCompilation;
using TS::TestSupport::LookupTypeDefinition;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// `System.Threading.Tasks.Task` (the non-generic Task, `KnownTypeCode::Task`, 0 type params).
std::shared_ptr<LookupTypeDefinition> TaskDef() {
	return std::make_shared<LookupTypeDefinition>("Task", "System.Threading.Tasks",
		FullTypeName(TopLevelTypeName("System.Threading.Tasks", "Task", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::Task);
}

// `System.Threading.Tasks.Task`1` (the generic Task definition, `KnownTypeCode::TaskOfT`, 1 type
// param). A `ParameterizedType` over this is `Task<T>`; the bare definition is NOT a task per
// `IsTask` (the `type is ParameterizedType` check fails for the bare definition).
std::shared_ptr<LookupTypeDefinition> TaskOfTDef() {
	return std::make_shared<LookupTypeDefinition>("Task`1", "System.Threading.Tasks",
		FullTypeName(TopLevelTypeName("System.Threading.Tasks", "Task`1", 1)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::TaskOfT);
}

// `Task<T>` over the supplied element type (a 1-arg `ParameterizedType` over the `Task`1` def).
ITypePtr TaskOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(TaskOfTDef(), std::vector<ITypePtr>{std::move(element)});
}

// A `LookupTypeDefinition` (IS-A `ITypeDefinition`, `GetDefinition() == this`) with a configurable
// `KnownTypeCode` and `TypeKind` (struct by default). `GetTypeCode` resolves the `KnownTypeCode`
// via the numeric cast (the `KnownTypeCode` 0-17 align with `TypeCode` 0-17), so a `Def(Int32)`
// reports `TypeCode::Int32`. The element types the `BetterConversionTarget` recursion feeds to the
// core implicit-convertibility check MUST be `Def`s (NOT `KnownType` placeholders, whose
// `GetTypeCode` is `Empty` -- the D514 learning); the D533 / `BetterConversion_Test` `MakeDef`
// precedent.
std::shared_ptr<LookupTypeDefinition> MakeDef(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	int n = static_cast<int>(ktc);
	std::string name = "T" + std::to_string(n);
	return std::make_shared<LookupTypeDefinition>(
		name, "",
		FullTypeName(TopLevelTypeName("", name, 0)),
		kind, Accessibility::Public, Compilation(), nullptr, ktc);
}

ITypePtr Def(KnownTypeCode ktc, TypeKind kind = TypeKind::Struct) {
	return MakeDef(ktc, kind);
}

ITypePtr Int32() { return Def(KnownTypeCode::Int32); }
ITypePtr Int64() { return Def(KnownTypeCode::Int64); }
ITypePtr String() { return Def(KnownTypeCode::String, TypeKind::Class); }

// `System.Type` as a `LookupTypeDefinition` (so `IsKnownType(*, KnownTypeCode::Type)` resolves via
// `GetDefinition() == this`); the `[AsyncMethodBuilder(T)]` attribute's fixed-argument decoded
// `Type` must be `System.Type` for `IsCustomTask` to accept it.
std::shared_ptr<LookupTypeDefinition> SystemTypeDef() {
	return std::make_shared<LookupTypeDefinition>("Type", "System",
		FullTypeName(TopLevelTypeName("System", "Type", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::Type);
}

// A test `IAttribute` stub carrying configurable fixed arguments (the positional
// `[AsyncMethodBuilder(...)]` arguments). `IsCustomTask` reads only `FixedArguments`.
class TestAttribute : public IAttribute {
public:
	explicit TestAttribute(std::vector<CustomAttributeTypedArgument> fixedArgs,
	                       ITypePtr attributeType = nullptr)
		: fixedArgs_(std::move(fixedArgs)),
		  attributeType_(std::move(attributeType))
	{
		if (attributeType_ == nullptr) {
			attributeType_ = std::make_shared<SimpleType>(
				TopLevelTypeName("System.Runtime.CompilerServices", "AsyncMethodBuilderAttribute"));
		}
	}

	const IType& AttributeType() const override { return *attributeType_; }
	const TS::IMethod* Constructor() const override { return nullptr; }
	bool HasDecodeErrors() const override { return false; }
	std::vector<CustomAttributeTypedArgument> FixedArguments() const override { return fixedArgs_; }
	std::vector<CustomAttributeNamedArgument> NamedArguments() const override { return {}; }

private:
	std::vector<CustomAttributeTypedArgument> fixedArgs_;
	ITypePtr attributeType_;
};

// A `LookupTypeDefinition` subclass that overrides `GetAttribute` / `HasAttribute` /
// `GetAttributes` to report a single configurable attribute -- the `[AsyncMethodBuilder(T)]` the
// `IsCustomTask` helper looks up.
class CustomTaskDef : public LookupTypeDefinition {
public:
	using LookupTypeDefinition::LookupTypeDefinition;

	void SetAsyncMethodBuilderAttribute(const IAttribute* attr) { attr_ = attr; }

	bool HasAttribute(KnownAttribute attribute) const override
	{
		return attribute == KnownAttribute::AsyncMethodBuilder && attr_ != nullptr;
	}
	const IAttribute* GetAttribute(KnownAttribute attribute) const override
	{
		return attribute == KnownAttribute::AsyncMethodBuilder ? attr_ : nullptr;
	}
	std::vector<const IAttribute*> GetAttributes() const override
	{
		return attr_ != nullptr ? std::vector<const IAttribute*>{attr_}
		                        : std::vector<const IAttribute*>{};
	}

private:
	const IAttribute* attr_ = nullptr;
};

// The `[AsyncMethodBuilder(builderType)]` attribute: one fixed argument whose decoded `Type` is
// `System.Type` and whose boxed `Value` is the `builderType` `ITypePtr`.
const IAttribute* MakeAsyncMethodBuilderAttribute(ITypePtr builderType)
{
	auto arg = CustomAttributeTypedArgument(SystemTypeDef(), ITypePtr(builderType));
	auto attr = new TestAttribute(std::vector<CustomAttributeTypedArgument>{std::move(arg)});
	return attr;
}

// A generic custom Task-like type definition (1 type param, `KnownTypeCode::None`) carrying the
// `[AsyncMethodBuilder(builderType)]` attribute. Leaks the attribute (test scope -- the attribute
// lives for the program's lifetime, like the registered known types).
std::shared_ptr<CustomTaskDef> GenericCustomTaskDef(ITypePtr builderType)
{
	auto def = std::make_shared<CustomTaskDef>("MyTask`1", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask`1", 1)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def->SetAsyncMethodBuilderAttribute(MakeAsyncMethodBuilderAttribute(std::move(builderType)));
	return def;
}

// A non-generic custom Task-like type definition (0 type params, `KnownTypeCode::None`) carrying
// the `[AsyncMethodBuilder(builderType)]` attribute.
std::shared_ptr<CustomTaskDef> NonGenericCustomTaskDef(ITypePtr builderType)
{
	auto def = std::make_shared<CustomTaskDef>("MyTask", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def->SetAsyncMethodBuilderAttribute(MakeAsyncMethodBuilderAttribute(std::move(builderType)));
	return def;
}

// `CustomTask<T>` over the supplied element type (a 1-arg `ParameterizedType` over the generic
// custom-task definition).
ITypePtr CustomTaskOf(ITypePtr element, ITypePtr builderType) {
	return std::make_shared<ParameterizedType>(GenericCustomTaskDef(std::move(builderType)),
		std::vector<ITypePtr>{std::move(element)});
}

} // namespace

// ===========================================================================
// Detail::UnpackTask (CSharpConversions.cs line 1654).
//
//   (TaskType.IsTask(type) || TaskType.IsCustomTask(type, out _)) && type.TypeParameterCount == 1
//       ? type.TypeArguments[0]
//       : null;
// ===========================================================================

TEST(CSharpConversionsUnpackTaskTest, ReturnsTypeArgumentForTaskOfT) {
	// `Task<int>` -- a `ParameterizedType` over the `Task`1` definition; `IsTask` is true via the
	// `TaskOfT` arm and `TypeParameterCount == 1`, so the helper returns `TypeArguments[0]` (the
	// `int` element, by pointer-identity).
	auto element = Int32();
	auto task = TaskOf(element);
	ITypePtr result = UnpackTask(*task);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), element.get());
}

TEST(CSharpConversionsUnpackTaskTest, ReturnsTypeArgumentForTaskOfLong) {
	auto element = Int64();
	auto task = TaskOf(element);
	ITypePtr result = UnpackTask(*task);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), element.get());
}

TEST(CSharpConversionsUnpackTaskTest, ReturnsNullForNonGenericTask) {
	// The non-generic `Task` (0 type params) yields null -- the `TypeParameterCount == 1` guard
	// fails. CRUX: this differs from `TaskType.UnpackTask`, which returns `void` for the
	// non-generic `Task`; `CSharpConversions.UnpackTask` returns null so the `BetterConversionTarget`
	// recursion does not fire for a non-generic `Task` target.
	ITypePtr result = UnpackTask(*TaskDef());
	EXPECT_EQ(result, nullptr);
}

TEST(CSharpConversionsUnpackTaskTest, ReturnsNullForNonTaskPrimitive) {
	// A primitive (non-task) type: `IsTask` and `IsCustomTask` are both false.
	EXPECT_EQ(UnpackTask(*Int32()), nullptr);
	EXPECT_EQ(UnpackTask(*Int64()), nullptr);
	EXPECT_EQ(UnpackTask(*String()), nullptr);
}

TEST(CSharpConversionsUnpackTaskTest, ReturnsNullForBareTaskOfTDefinition) {
	// The bare `Task`1` definition is NOT a `ParameterizedType`, so `IsTask`'s `TaskOfT` arm (the
	// `type is ParameterizedType` check) fails; `IsCustomTask` is false (no attribute). Even though
	// `TypeParameterCount == 1`, the task-like guard fails first -> null.
	ITypePtr result = UnpackTask(*TaskOfTDef());
	EXPECT_EQ(result, nullptr);
}

TEST(CSharpConversionsUnpackTaskTest, ReturnsNullForKnownTypeTaskPlaceholder) {
	// A `KnownType` placeholder is not an `ITypeDefinition` (`GetDefinition() == nullptr`), so
	// `IsTask` returns false even for `KnownType(TaskOfT)`.
	EXPECT_EQ(UnpackTask(*std::make_shared<KnownType>(KnownTypeCode::Task)), nullptr);
	EXPECT_EQ(UnpackTask(*std::make_shared<KnownType>(KnownTypeCode::TaskOfT)), nullptr);
}

TEST(CSharpConversionsUnpackTaskTest, ReturnsTypeArgumentForGenericCustomTask) {
	// A generic custom Task-like type (1 type param, `[AsyncMethodBuilder]`) -- the `|| IsCustomTask`
	// arm fires (`IsTask` is false -- no `Task`/`TaskOfT` `KnownTypeCode`). `TypeParameterCount == 1`
	// -> returns `TypeArguments[0]` (the element).
	auto builder = String();
	auto element = Int32();
	auto customTask = CustomTaskOf(element, builder);
	ITypePtr result = UnpackTask(*customTask);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), element.get());
}

TEST(CSharpConversionsUnpackTaskTest, ReturnsNullForNonGenericCustomTask) {
	// A non-generic custom Task-like type (0 type params, `[AsyncMethodBuilder]`) -- the
	// `|| IsCustomTask` arm fires but `TypeParameterCount == 1` fails -> null.
	auto builder = String();
	auto customTask = NonGenericCustomTaskDef(builder);
	ITypePtr result = UnpackTask(*customTask);
	EXPECT_EQ(result, nullptr);
}

// ===========================================================================
// BetterConversionTarget -- the `Task<T>` recursion (wired via `Detail::UnpackTask`).
//
//   var s1 = UnpackTask(t1); var s2 = UnpackTask(t2);
//   if (s1 != null && s2 != null) return BetterConversionTarget(s1, s2);
// ===========================================================================

TEST(CSharpConversionsUnpackTaskTest, BetterConversionTargetRecursesForTwoTaskOfT) {
	// `Task<int>` vs `Task<long>`: both unpack to a non-null inner type, so the recursion fires on
	// `(int, long)`. The core implicit-convertibility check: `int -> long` is implicit widening
	// (valid), `long -> int` is not -> `int` is the better target -> returns 1. CRUX: the
	// `UnpackTask` recursion fires (not the integral tiebreak on the outer `Task<T>` types, whose
	// `GetTypeCode` is `Empty`).
	auto taskInt = TaskOf(Int32());
	auto taskLong = TaskOf(Int64());
	EXPECT_EQ(BetterConversionTarget(Compilation(), *taskInt, *taskLong), 1);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *taskLong, *taskInt), 2);
}

TEST(CSharpConversionsUnpackTaskTest, BetterConversionTargetRecursesToNeitherForSameTaskElement) {
	// `Task<int>` vs `Task<int>` (the SAME element instance): the recursion fires on `(int, int)`
	// where the core check is identity both ways (neither wins) and the integral tiebreak is
	// `Int32` vs `Int32` (neither) -> 0.
	auto element = Int32();
	auto task = TaskOf(element);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *task, *task), 0);
}

TEST(CSharpConversionsUnpackTaskTest, BetterConversionTargetSkipsRecursionWhenOneSideIsNotTask) {
	// `Task<int>` vs `int`: only one side unpacks to a non-null inner type (`s1` non-null, `s2`
	// null), so the `s1 && s2` guard skips the recursion. The dispatch falls to the integral
	// tiebreak: `GetTypeCode(Task<int>)` is `Empty` (a `ParameterizedType` is not an
	// `ITypeDefinition`) and `GetTypeCode(int)` is `Int32`; `IsBetterIntegralType(Empty, Int32)`
	// and the mirror are both false -> 0. CRUX: the recursion is skipped when only one target is
	// `Task<T>`-shaped.
	auto taskInt = TaskOf(Int32());
	auto intType = Int32();
	EXPECT_EQ(BetterConversionTarget(Compilation(), *taskInt, *intType), 0);
	EXPECT_EQ(BetterConversionTarget(Compilation(), *intType, *taskInt), 0);
}

TEST(CSharpConversionsUnpackTaskTest, BetterConversionTargetSkipsRecursionForTwoNonTasks) {
	// Two non-task targets (`int` vs `long`): both unpack to null, so the recursion is skipped and
	// the core implicit-convertibility check fires directly (`int -> long` widening) -> 1. This
	// pins the faithful fallback for the non-`Task` common case (the recursion adds nothing).
	auto intType = Int32();
	auto longType = Int64();
	EXPECT_EQ(BetterConversionTarget(Compilation(), *intType, *longType), 1);
}
