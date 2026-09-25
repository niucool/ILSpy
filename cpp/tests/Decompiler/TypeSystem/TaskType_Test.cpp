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

// Tests for the `TaskType` helpers (the port of ICSharpCode.Decompiler/TypeSystem/TaskType.cs)
// -- the `System.Threading.Tasks.Task` / Task-like static helpers the deferred
// `CSharpConversions` arms consume (`IsTask` / `IsCustomTask` are the direct prerequisite for
// the `CSharpConversions.UnpackTask` helper the `BetterConversion(ResolveResult, ...)` /
// `IsExactlyMatching` arms need). `IsTask(IType)` recognizes `Task` and `Task<T>` (a
// `ParameterizedType` over the `Task`1` definition, NOT the bare definition); `IsCustomTask`
// recognizes any `[AsyncMethodBuilder]`-attributed type with a single `System.Type` fixed
// argument; `UnpackTask` returns `void` for `Task`, `T` for `Task<T>`, the type unmodified for a
// non-task; `UnpackAnyTask` additionally handles custom Task-like types. The stubs build the
// `ParameterizedType` / attribute-decorated-definition shapes the helpers dispatch over.

#include "Decompiler/TypeSystem/TaskType.hpp"
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

namespace TS = ILSpy::Decompiler::TypeSystem;
using TS::Accessibility;
using TS::CustomAttributeNamedArgument;
using TS::CustomAttributeTypedArgument;
using TS::FullTypeName;
using TS::IAttribute;
using TS::ICompilation;
using TS::IType;
using TS::ITypeDefinition;
using TS::ITypePtr;
using TS::IsCustomTask;
using TS::IsTask;
using TS::KnownAttribute;
using TS::KnownType;
using TS::KnownTypeCode;
using TS::ParameterizedType;
using TS::SimpleType;
using TS::TopLevelTypeName;
using TS::TypeKind;
using TS::UnpackAnyTask;
using TS::UnpackTask;
using TS::IsGenericTaskType;
using TS::IsNonGenericTaskType;
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

// `Task<T>` over the supplied element type.
ITypePtr TaskOf(ITypePtr element) {
	return std::make_shared<ParameterizedType>(TaskOfTDef(), std::vector<ITypePtr>{std::move(element)});
}

ITypePtr Task() { return TaskDef(); }

ITypePtr Int32() { return std::make_shared<KnownType>(KnownTypeCode::Int32); }
ITypePtr String() { return std::make_shared<KnownType>(KnownTypeCode::String); }

// `System.Type` as a `LookupTypeDefinition` (so `IsKnownType(*, KnownTypeCode::Type)` resolves via
// `GetDefinition() == this`); the attribute's fixed-argument decoded `Type` must be `System.Type`
// for `IsCustomTask` to accept it. A `KnownType(Type)` placeholder would have
// `GetDefinition() == nullptr` and fail the `IsKnownType` check.
std::shared_ptr<LookupTypeDefinition> SystemTypeDef() {
	return std::make_shared<LookupTypeDefinition>("Type", "System",
		FullTypeName(TopLevelTypeName("System", "Type", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::Type);
}

// A test `IAttribute` stub carrying configurable fixed arguments (the positional
// `[AsyncMethodBuilder(...)]` arguments). `IsCustomTask` reads only `FixedArguments` (the
// attribute type / constructor / named args are not consulted).
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
// `IsCustomTask` helper looks up. The base stub returns `nullptr` / `false` / `{}`; this subclass
// overrides only the attribute accessors (the rest of the `ITypeDefinition` surface is inherited
// unchanged, so the configurable `KnownTypeCode` / `TypeParameterCount` from the base ctor apply).
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
// `System.Type` and whose boxed `Value` is the `builderType` `ITypePtr` (the C# `(IType)arg.Value`).
const IAttribute* MakeAsyncMethodBuilderAttribute(ITypePtr builderType)
{
	auto arg = CustomAttributeTypedArgument(SystemTypeDef(), ITypePtr(builderType));
	auto attr = new TestAttribute(std::vector<CustomAttributeTypedArgument>{std::move(arg)});
	return attr;
}

// A non-generic custom Task-like type: a `CustomTaskDef` (0 type params, `KnownTypeCode::None`)
// carrying the `[AsyncMethodBuilder(builderType)]` attribute. Leaks the attribute (test scope --
// the attribute lives for the program's lifetime, like the registered known types).
std::shared_ptr<CustomTaskDef> NonGenericCustomTaskDef(ITypePtr builderType)
{
	auto def = std::make_shared<CustomTaskDef>("MyTask", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def->SetAsyncMethodBuilderAttribute(MakeAsyncMethodBuilderAttribute(std::move(builderType)));
	return def;
}

// A generic custom Task-like type definition: a `CustomTaskDef` (1 type param, `KnownTypeCode::None`)
// carrying the `[AsyncMethodBuilder(builderType)]` attribute. `ValueTask<T>` is a `ParameterizedType`
// over such a definition.
std::shared_ptr<CustomTaskDef> GenericCustomTaskDef(ITypePtr builderType)
{
	auto def = std::make_shared<CustomTaskDef>("MyTask`1", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask`1", 1)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def->SetAsyncMethodBuilderAttribute(MakeAsyncMethodBuilderAttribute(std::move(builderType)));
	return def;
}

// A `LookupCompilation` with `System.Void` registered for `FindType(KnownTypeCode::Void)` -- the
// non-generic `Task` / custom Task-like unpacks to `void`. The owning `shared_ptr` to the `Void`
// type is held in the struct so `shared_from_this` (called by `UnpackTask` / `UnpackAnyTask`) is
// valid (the `enable_shared_from_this` contract requires the object be shared-managed). Heap-
// allocated via `make_unique` (the `LookupCompilation` is non-copyable/non-movable; the pointee
// is never moved, so its `mainModule_` reference stays valid for the test's scope).
struct RegisteredVoid {
	LookupCompilation compilation;
	std::shared_ptr<KnownType> voidType;
};
std::unique_ptr<RegisteredVoid> MakeRegisteredVoid() {
	auto r = std::make_unique<RegisteredVoid>();
	r->voidType = std::make_shared<KnownType>(KnownTypeCode::Void);
	r->compilation.RegisterKnownType(KnownTypeCode::Void, r->voidType.get());
	return r;
}

} // namespace

// ---------------------------------------------------------------------------
// IsTask -- `Task` / `Task<T>` recognition (a `ParameterizedType` over `Task`1`, NOT the bare def).
// ---------------------------------------------------------------------------

TEST(TaskTypeTest, IsTaskTrueForNonGenericTaskDefinition) {
	// `System.Threading.Tasks.Task` (KnownTypeCode::Task, 0 type params).
	EXPECT_TRUE(IsTask(*TaskDef()));
}

TEST(TaskTypeTest, IsTaskTrueForParameterizedTaskOfT) {
	// `Task<int>` -- a ParameterizedType over the Task`1 definition.
	EXPECT_TRUE(IsTask(*TaskOf(Int32())));
}

TEST(TaskTypeTest, IsTaskFalseForBareTaskOfTDefinition) {
	// The bare `Task`1` definition is NOT a ParameterizedType, so the `TaskOfT` arm's
	// `type is ParameterizedType` check fails.
	EXPECT_FALSE(IsTask(*TaskOfTDef()));
}

TEST(TaskTypeTest, IsTaskFalseForNonTaskType) {
	EXPECT_FALSE(IsTask(*Int32()));
	EXPECT_FALSE(IsTask(*String()));
}

TEST(TaskTypeTest, IsTaskFalseForCustomTaskLikeType) {
	// A custom Task-like (no KnownTypeCode, has [AsyncMethodBuilder]) is NOT Task/Task<T>.
	auto builder = String();
	auto customDef = NonGenericCustomTaskDef(builder);
	EXPECT_FALSE(IsTask(*customDef));
}

TEST(TaskTypeTest, IsTaskFalseForNullDefinitionType) {
	// A `KnownType` placeholder is not an `ITypeDefinition` (`GetDefinition() == nullptr`), so
	// `IsTask` returns false even for `KnownType(Task)`.
	EXPECT_FALSE(IsTask(*std::make_shared<KnownType>(KnownTypeCode::Task)));
	EXPECT_FALSE(IsTask(*std::make_shared<KnownType>(KnownTypeCode::TaskOfT)));
}

// ---------------------------------------------------------------------------
// IsCustomTask -- a `[AsyncMethodBuilder]`-attributed type with a single System.Type fixed arg.
// ---------------------------------------------------------------------------

TEST(TaskTypeTest, IsCustomTaskTrueForNonGenericCustomTask) {
	auto builder = String();
	auto customDef = NonGenericCustomTaskDef(builder);
	ITypePtr outBuilder;
	EXPECT_TRUE(IsCustomTask(*customDef, outBuilder));
	ASSERT_NE(outBuilder, nullptr);
	// The out-parameter is the [AsyncMethodBuilder(T)] argument value (the builder type).
	EXPECT_EQ(outBuilder.get(), builder.get());
}

TEST(TaskTypeTest, IsCustomTaskTrueForGenericCustomTask) {
	auto builder = String();
	auto customDef = GenericCustomTaskDef(builder);
	// A `ValueTask<int>`-style parameterized custom task: the def has 1 type param (<= 1) and the
	// attribute, so `IsCustomTask` accepts it.
	auto valueTaskInt = std::make_shared<ParameterizedType>(customDef,
		std::vector<ITypePtr>{Int32()});
	ITypePtr outBuilder;
	EXPECT_TRUE(IsCustomTask(*valueTaskInt, outBuilder));
	ASSERT_NE(outBuilder, nullptr);
	EXPECT_EQ(outBuilder.get(), builder.get());
}

TEST(TaskTypeTest, IsCustomTaskFalseForTaskAndTaskOfT) {
	// `Task` / `Task<T>` carry no `[AsyncMethodBuilder]` attribute (the base stub returns null).
	ITypePtr outBuilder;
	EXPECT_FALSE(IsCustomTask(*TaskDef(), outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
	EXPECT_FALSE(IsCustomTask(*TaskOf(Int32()), outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
}

TEST(TaskTypeTest, IsCustomTaskFalseWhenTypeParameterCountExceedsOne) {
	// A 2-type-param def with the attribute: the `def.TypeParameterCount > 1` early-out fires
	// BEFORE the attribute lookup, so the helper returns false even though the attribute is present.
	auto builder = String();
	auto def = std::make_shared<CustomTaskDef>("MyTask`2", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask`2", 2)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def->SetAsyncMethodBuilderAttribute(MakeAsyncMethodBuilderAttribute(builder));
	ITypePtr outBuilder;
	EXPECT_FALSE(IsCustomTask(*def, outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
}

TEST(TaskTypeTest, IsCustomTaskFalseWhenAttributeAbsent) {
	// A def with no [AsyncMethodBuilder] (the base stub's GetAttribute returns null).
	auto def = std::make_shared<CustomTaskDef>("MyTask", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	ITypePtr outBuilder;
	EXPECT_FALSE(IsCustomTask(*def, outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
}

TEST(TaskTypeTest, IsCustomTaskFalseWhenFixedArgumentsCountIsNotOne) {
	auto builder = String();
	// 0 fixed args.
	auto zeroArgAttr = new TestAttribute(std::vector<CustomAttributeTypedArgument>{});
	auto def0 = std::make_shared<CustomTaskDef>("MyTask0", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask0", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def0->SetAsyncMethodBuilderAttribute(zeroArgAttr);
	ITypePtr outBuilder;
	EXPECT_FALSE(IsCustomTask(*def0, outBuilder));
	EXPECT_EQ(outBuilder, nullptr);

	// 2 fixed args.
	auto twoArgAttr = new TestAttribute(std::vector<CustomAttributeTypedArgument>{
		CustomAttributeTypedArgument(SystemTypeDef(), ITypePtr(builder)),
		CustomAttributeTypedArgument(SystemTypeDef(), ITypePtr(String())),
	});
	auto def2 = std::make_shared<CustomTaskDef>("MyTask2", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask2", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def2->SetAsyncMethodBuilderAttribute(twoArgAttr);
	EXPECT_FALSE(IsCustomTask(*def2, outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
}

TEST(TaskTypeTest, IsCustomTaskFalseWhenArgumentTypeIsNotSystemType) {
	// The fixed arg's decoded Type is NOT System.Type (it is Int32), so the `arg.Type.IsKnownType
	// (Type)` check fails.
	auto builder = String();
	auto int32Def = std::make_shared<LookupTypeDefinition>("Int32", "System",
		FullTypeName(TopLevelTypeName("System", "Int32", 0)),
		TypeKind::Struct, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::Int32);
	auto attr = new TestAttribute(std::vector<CustomAttributeTypedArgument>{
		CustomAttributeTypedArgument(int32Def, ITypePtr(builder)),
	});
	auto def = std::make_shared<CustomTaskDef>("MyTask", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def->SetAsyncMethodBuilderAttribute(attr);
	ITypePtr outBuilder;
	EXPECT_FALSE(IsCustomTask(*def, outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
}

TEST(TaskTypeTest, IsCustomTaskFalseWhenArgumentValueIsNotAnIType) {
	// The fixed arg's decoded Type IS System.Type, but the boxed Value is NOT an ITypePtr (a
	// string) -- the pointer-form `std::any_cast<ITypePtr>` returns null, so the helper returns
	// false (the safe faithful fallback for the C# `(IType)arg.Value` InvalidCastException).
	auto attr = new TestAttribute(std::vector<CustomAttributeTypedArgument>{
		CustomAttributeTypedArgument(SystemTypeDef(), std::any(std::string("not a type"))),
	});
	auto def = std::make_shared<CustomTaskDef>("MyTask", "Test",
		FullTypeName(TopLevelTypeName("Test", "MyTask", 0)),
		TypeKind::Class, Accessibility::Public, Compilation(), nullptr,
		KnownTypeCode::None);
	def->SetAsyncMethodBuilderAttribute(attr);
	ITypePtr outBuilder;
	EXPECT_FALSE(IsCustomTask(*def, outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
}

TEST(TaskTypeTest, IsCustomTaskFalseForNullDefinition) {
	// A `KnownType` placeholder has `GetDefinition() == nullptr`, so `IsCustomTask` returns false.
	ITypePtr outBuilder;
	EXPECT_FALSE(IsCustomTask(*std::make_shared<KnownType>(KnownTypeCode::Task), outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
}

TEST(TaskTypeTest, IsCustomTaskResetsBuilderTypeToNullOnFailure) {
	// The C# sets `builderType = null` at the top; on every failure path the out-param is null.
	// Pre-seed a non-null out-param and verify a failing call resets it.
	ITypePtr outBuilder = String();
	EXPECT_FALSE(IsCustomTask(*TaskDef(), outBuilder));
	EXPECT_EQ(outBuilder, nullptr);
}

// ---------------------------------------------------------------------------
// UnpackTask -- void for non-generic Task, T for Task<T>, the type unmodified for a non-task.
// ---------------------------------------------------------------------------

TEST(TaskTypeTest, UnpackTaskReturnsVoidForNonGenericTask) {
	auto r = MakeRegisteredVoid();
	ITypePtr result = UnpackTask(r->compilation, *TaskDef());
	ASSERT_NE(result, nullptr);
	// The non-generic `Task` unpacks to `void` (the registered `System.Void`).
	EXPECT_EQ(result.get(), r->voidType.get());
}

TEST(TaskTypeTest, UnpackTaskReturnsTypeArgumentForTaskOfT) {
	ITypePtr element = Int32();
	ITypePtr result = UnpackTask(Compilation(), *TaskOf(element));
	ASSERT_NE(result, nullptr);
	// `Task<int>` unpacks to `int` (the type argument, the same instance).
	EXPECT_EQ(result.get(), element.get());
}

TEST(TaskTypeTest, UnpackTaskReturnsTypeUnmodifiedForNonTask) {
	auto r = MakeRegisteredVoid();
	ITypePtr int32 = Int32();
	ITypePtr result = UnpackTask(r->compilation, *int32);
	ASSERT_NE(result, nullptr);
	// A non-task type is returned unmodified (the same instance, via `shared_from_this`).
	EXPECT_EQ(result.get(), int32.get());
}

TEST(TaskTypeTest, UnpackTaskReturnsTypeUnmodifiedForBareTaskOfTDefinition) {
	auto r = MakeRegisteredVoid();
	// The bare `Task`1` definition is NOT a task (per `IsTask`), so it is returned unmodified.
	auto def = TaskOfTDef();
	ITypePtr result = UnpackTask(r->compilation, *def);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), def.get());
}

TEST(TaskTypeTest, UnpackTaskReturnsTypeUnmodifiedForCustomTaskLike) {
	auto r = MakeRegisteredVoid();
	// `UnpackTask` handles ONLY `Task`/`Task<T>` (not custom Task-likes); a custom Task-like is a
	// non-task here, returned unmodified.
	auto builder = String();
	auto customDef = NonGenericCustomTaskDef(builder);
	ITypePtr result = UnpackTask(r->compilation, *customDef);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), customDef.get());
}

// ---------------------------------------------------------------------------
// UnpackAnyTask -- Task/Task<T> AND custom Task-likes; void for non-generic, T for generic.
// ---------------------------------------------------------------------------

TEST(TaskTypeTest, UnpackAnyTaskReturnsVoidForNonGenericTask) {
	auto r = MakeRegisteredVoid();
	ITypePtr result = UnpackAnyTask(r->compilation, *TaskDef());
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), r->voidType.get());
}

TEST(TaskTypeTest, UnpackAnyTaskReturnsTypeArgumentForTaskOfT) {
	ITypePtr element = Int32();
	ITypePtr result = UnpackAnyTask(Compilation(), *TaskOf(element));
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), element.get());
}

TEST(TaskTypeTest, UnpackAnyTaskReturnsVoidForNonGenericCustomTask) {
	auto r = MakeRegisteredVoid();
	auto builder = String();
	auto customDef = NonGenericCustomTaskDef(builder);
	ITypePtr result = UnpackAnyTask(r->compilation, *customDef);
	ASSERT_NE(result, nullptr);
	// A non-generic custom Task-like unpacks to `void`.
	EXPECT_EQ(result.get(), r->voidType.get());
}

TEST(TaskTypeTest, UnpackAnyTaskReturnsTypeArgumentForGenericCustomTask) {
	auto r = MakeRegisteredVoid();
	auto builder = String();
	auto customDef = GenericCustomTaskDef(builder);
	auto customTaskInt = std::make_shared<ParameterizedType>(customDef,
		std::vector<ITypePtr>{Int32()});
	ITypePtr result = UnpackAnyTask(r->compilation, *customTaskInt);
	ASSERT_NE(result, nullptr);
	// A generic custom Task-like (`ValueTask<int>`-style) unpacks to the type argument `int`.
	EXPECT_EQ(result.get(), customTaskInt->GetTypeArgument(0).get());
}

TEST(TaskTypeTest, UnpackAnyTaskReturnsTypeUnmodifiedForNonTaskLike) {
	auto r = MakeRegisteredVoid();
	ITypePtr int32 = Int32();
	ITypePtr result = UnpackAnyTask(r->compilation, *int32);
	ASSERT_NE(result, nullptr);
	EXPECT_EQ(result.get(), int32.get());
}

TEST(TaskTypeTest, UnpackAnyTaskReturnsTypeUnmodifiedForBareTaskOfTDefinition) {
	auto r = MakeRegisteredVoid();
	auto def = TaskOfTDef();
	ITypePtr result = UnpackAnyTask(r->compilation, *def);
	ASSERT_NE(result, nullptr);
	// The bare `Task`1` definition is neither a task nor a custom task-like, returned unmodified.
	EXPECT_EQ(result.get(), def.get());
}

// The `IsNonGenericTaskType` / `IsGenericTaskType` pair (TaskType.cs lines 73
// and 90): the Task-like classification the AsyncAwaitDecompiler's task-
// creation pattern consults -- a non-generic Task-like is `Task` itself or a
// custom builder type with no type parameters; a generic Task-like is
// `Task<T>` or a custom builder with exactly one.
TEST(TaskTypeTest, IsNonGenericTaskTypeMatchesTask) {
	FullTypeName builder;
	EXPECT_TRUE(IsNonGenericTaskType(*Task(), builder));
	// `Task` pairs with the AsyncTaskMethodBuilder full name.
	EXPECT_EQ(builder, FullTypeName(TopLevelTypeName("System.Runtime.CompilerServices", "AsyncTaskMethodBuilder")));
	EXPECT_EQ(builder.TypeParameterCount(), 0);
}

TEST(TaskTypeTest, IsGenericTaskTypeMatchesTaskOfT) {
	auto taskOfInt = std::make_shared<ParameterizedType>(TaskOfTDef(),
		std::vector<ITypePtr>{Int32()});
	FullTypeName builder;
	EXPECT_TRUE(IsGenericTaskType(*taskOfInt, builder));
	// `Task<T>` pairs with the generic AsyncTaskMethodBuilder`1 full name.
	EXPECT_EQ(builder, FullTypeName(TopLevelTypeName("System.Runtime.CompilerServices", "AsyncTaskMethodBuilder", 1)));
	EXPECT_EQ(builder.TypeParameterCount(), 1);
}

TEST(TaskTypeTest, IsNonGenericTaskTypeRejectsTaskOfT) {
	auto taskOfInt = std::make_shared<ParameterizedType>(TaskOfTDef(),
		std::vector<ITypePtr>{Int32()});
	FullTypeName builder;
	EXPECT_FALSE(IsNonGenericTaskType(*taskOfInt, builder));
}

TEST(TaskTypeTest, IsGenericTaskTypeRejectsPlainTask) {
	FullTypeName builder;
	EXPECT_FALSE(IsGenericTaskType(*Task(), builder));
}

TEST(TaskTypeTest, IsNonGenericTaskTypeMatchesCustomTaskWithZeroTypeParameters) {
	auto builder = String();
	auto customDef = NonGenericCustomTaskDef(builder);
	FullTypeName builderName;
	EXPECT_TRUE(IsNonGenericTaskType(*customDef, builderName));
	EXPECT_EQ(builderName.TypeParameterCount(), 0);
}

TEST(TaskTypeTest, IsGenericTaskTypeMatchesCustomTaskWithOneTypeParameter) {
	auto builder = TaskOfTDef();  // any 1-type-param type serves as the builder
	auto customDef = GenericCustomTaskDef(builder);
	auto customTaskInt = std::make_shared<ParameterizedType>(customDef,
		std::vector<ITypePtr>{Int32()});
	FullTypeName builderName;
	EXPECT_TRUE(IsGenericTaskType(*customTaskInt, builderName));
	EXPECT_EQ(builderName.TypeParameterCount(), 1);
}

TEST(TaskTypeTest, TaskClassificationsRejectInt32) {
	FullTypeName builder;
	EXPECT_FALSE(IsNonGenericTaskType(*Int32(), builder));
	EXPECT_FALSE(IsGenericTaskType(*Int32(), builder));
}
