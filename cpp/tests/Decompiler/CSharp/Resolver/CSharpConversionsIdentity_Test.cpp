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
// THE SOFTWARE IS PROVIDED "AS IS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
// PURPOSE NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE
// FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// Tests for the `CSharpConversions` identity-conversion helper -- `Detail::IdentityConversion`
// (a faithful port of the C# `CSharpConversions.IdentityConversion`, CSharpConversions.cs line 367,
// C# spec draft-v11 section 10.2.2). The helper erases both types through the
// `NormalizeTypeVisitor.TypeErasure` singleton (folding object<->dynamic, IntPtr/UIntPtr<->nint/nuint,
// nullability annotations, custom modifiers, tuple-vs-ValueTuple) then compares structurally
// (`IType::Equals`). It reads no `CSharpConversions` instance state, so it lives as a `Detail::`
// free function (the D508 precedent); the signature takes `IType&` (non-const) because
// `IType::AcceptVisitor` is non-const (the D406 convention).
//
// The crux cases are the TypeErasure folds: object<->dynamic, IntPtr/UIntPtr<->nint/nuint, the
// nullability-annotation erase, and the custom-modifier erase. The test stubs that exercise the
// `VisitTypeDefinition` arm (object/IntPtr/UIntPtr/String) override `AcceptVisitor` to dispatch to
// `visitor.VisitTypeDefinition(*this)` -- the faithful C# `MetadataTypeDefinition.AcceptVisitor`
// dispatch the port's `ITypeDefinition` interface does not yet carry (the stub base inherits the
// `IType` `VisitOtherType` default, which would bypass the visitor's `VisitTypeDefinition`
// override). `SpecialType` (Dynamic / NInt / NUInt) carries the `VisitOtherType` default -- it is
// not an `ITypeDefinition` -- so it passes through erasure unchanged, which is exactly the
// to-side the object/IntPtr folds produce.

#include "Decompiler/CSharp/Resolver/CSharpConversionsHelpers.hpp"
#include "Decompiler/TypeSystem/Accessibility.hpp"
#include "Decompiler/TypeSystem/FullTypeName.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/LookupStubs.hpp"
#include "Decompiler/TypeSystem/Nullability.hpp"
#include "Decompiler/TypeSystem/TopLevelTypeName.hpp"
#include "Decompiler/TypeSystem/TypeKind.hpp"
#include "Decompiler/TypeSystem/TypeVisitor.hpp"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace TS = ILSpy::Decompiler::TypeSystem;

namespace {

using ILSpy::Decompiler::CSharp::Resolver::Detail::IdentityConversion;
using ILSpy::Decompiler::TypeSystem::Accessibility;
using ILSpy::Decompiler::TypeSystem::FullTypeName;
using ILSpy::Decompiler::TypeSystem::ICompilation;
using ILSpy::Decompiler::TypeSystem::IType;
using ILSpy::Decompiler::TypeSystem::ITypePtr;
using ILSpy::Decompiler::TypeSystem::KnownType;
using ILSpy::Decompiler::TypeSystem::KnownTypeCode;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupCompilation;
using ILSpy::Decompiler::TypeSystem::TestSupport::LookupTypeDefinition;
using ILSpy::Decompiler::TypeSystem::ModifiedType;
using ILSpy::Decompiler::TypeSystem::Nullability;
using ILSpy::Decompiler::TypeSystem::NullabilityAnnotatedType;
using ILSpy::Decompiler::TypeSystem::SpecialType;
using ILSpy::Decompiler::TypeSystem::TopLevelTypeName;
using ILSpy::Decompiler::TypeSystem::TypeKind;
using ILSpy::Decompiler::TypeSystem::TypeVisitor;

LookupCompilation& Compilation() {
	static LookupCompilation c;
	return c;
}

// A `LookupTypeDefinition` with the faithful C# `ITypeDefinition.AcceptVisitor` dispatch
// (`visitor.VisitTypeDefinition(*this)` -- the stub base inherits the `IType` `VisitOtherType`
// default, which would bypass the visitor's `VisitTypeDefinition` override, so the
// object->dynamic / IntPtr->nint folds would never fire). This mirrors the
// `VisitableDefinition` stub in `NormalizeTypeVisitor_Test.cpp`.
class VisitableDefinition : public LookupTypeDefinition {
public:
	VisitableDefinition(std::string name, TS::KnownTypeCode code, const TS::ICompilation& compilation)
		: LookupTypeDefinition(std::move(name), "System",
			TS::FullTypeName(TS::TopLevelTypeName("System", name)),
			TS::TypeKind::Class, TS::Accessibility::Public, compilation,
			&compilation.MainModule(), code) {}

	ITypePtr AcceptVisitor(TypeVisitor& visitor) override {
		return visitor.VisitTypeDefinition(*this);
	}
};

} // namespace

// ---------------------------------------------------------------------------
// IdentityConversion: a type is identity-convertible to itself (the §10.2.2 baseline). A
// `VisitableDefinition` whose `KnownTypeCode` is not one of the special erasure cases (Object /
// IntPtr / UIntPtr) passes through `TypeErasure` unchanged (the `VisitTypeDefinition` default
// falls through to `VisitChildren`, which returns `shared_from_this`), so `IdentityConversion(t, t)`
// compares the same pointer to itself.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsIdentityTest, SameTypeIsIdentity) {
	auto int32 = std::make_shared<VisitableDefinition>("Int32", KnownTypeCode::Int32, Compilation());
	EXPECT_TRUE(IdentityConversion(*int32, *int32));

	auto str = std::make_shared<VisitableDefinition>("String", KnownTypeCode::String, Compilation());
	EXPECT_TRUE(IdentityConversion(*str, *str));
}

// ---------------------------------------------------------------------------
// IdentityConversion: two structurally-distinct types are NOT identity-convertible. `Int32` and
// `Int64` are both `VisitableDefinition`s with `TypeKind::Class`; after erasure they stay
// themselves (different pointers), and the test stub's `StructuralEquals` is identity equality
// (`this == &other`), so the two distinct definitions compare unequal.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsIdentityTest, DifferentTypesAreNotIdentity) {
	auto int32 = std::make_shared<VisitableDefinition>("Int32", KnownTypeCode::Int32, Compilation());
	auto int64 = std::make_shared<VisitableDefinition>("Int64", KnownTypeCode::Int64, Compilation());
	EXPECT_FALSE(IdentityConversion(*int32, *int64));
	EXPECT_FALSE(IdentityConversion(*int64, *int32));
}

// ---------------------------------------------------------------------------
// IdentityConversion: `object` is identity-convertible to `dynamic` (the TypeErasure
// `DynamicAndObject` fold -- object normalizes to `SpecialType.Dynamic` in the OPPOSITE direction
// of dynamic->object, so no compilation is needed). The to-side `SpecialType(Dynamic)` passes
// through erasure unchanged (it is not an `ITypeDefinition`), and `SpecialType::StructuralEquals`
// compares by `Kind`, so the freshly-made dynamic the object folds to equals the to-side dynamic.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsIdentityTest, ObjectAndDynamicAreIdentity) {
	auto object = std::make_shared<VisitableDefinition>("Object", KnownTypeCode::Object, Compilation());
	auto dynamic = std::make_shared<SpecialType>(TypeKind::Dynamic, /*isReferenceType=*/true);
	EXPECT_TRUE(IdentityConversion(*object, *dynamic));
	EXPECT_TRUE(IdentityConversion(*dynamic, *object));
	// object <-> object (both fold to dynamic) is identity too.
	EXPECT_TRUE(IdentityConversion(*object, *object));
}

// ---------------------------------------------------------------------------
// IdentityConversion: `IntPtr` is identity-convertible to `nint`, and `UIntPtr` to `nuint` (the
// TypeErasure `IntPtrToNInt` fold). The to-side `SpecialType(NInt)`/`SpecialType(NUInt)` passes
// through erasure unchanged; `SpecialType::StructuralEquals` compares by `Kind`, so the
// freshly-made native integer the pointer folds to equals the to-side native integer.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsIdentityTest, NativePointerAndNativeIntegerAreIdentity) {
	auto intptr = std::make_shared<VisitableDefinition>("IntPtr", KnownTypeCode::IntPtr, Compilation());
	auto uintptr = std::make_shared<VisitableDefinition>("UIntPtr", KnownTypeCode::UIntPtr, Compilation());
	auto nint = std::make_shared<SpecialType>(TypeKind::NInt, /*isReferenceType=*/false);
	auto nuint = std::make_shared<SpecialType>(TypeKind::NUInt, /*isReferenceType=*/false);
	EXPECT_TRUE(IdentityConversion(*intptr, *nint));
	EXPECT_TRUE(IdentityConversion(*nint, *intptr));
	EXPECT_TRUE(IdentityConversion(*uintptr, *nuint));
	EXPECT_TRUE(IdentityConversion(*nuint, *uintptr));
}

// ---------------------------------------------------------------------------
// IdentityConversion: `IntPtr` and `UIntPtr` are NOT identity-convertible to each other (they
// fold to `nint` and `nuint` respectively -- different `Kind`s). `nint` and `nuint` likewise are
// not identity-convertible (different `Kind`s).
// ---------------------------------------------------------------------------
TEST(CSharpConversionsIdentityTest, NativeIntegerSignsAreNotIdentity) {
	auto intptr = std::make_shared<VisitableDefinition>("IntPtr", KnownTypeCode::IntPtr, Compilation());
	auto uintptr = std::make_shared<VisitableDefinition>("UIntPtr", KnownTypeCode::UIntPtr, Compilation());
	auto nint = std::make_shared<SpecialType>(TypeKind::NInt, /*isReferenceType=*/false);
	auto nuint = std::make_shared<SpecialType>(TypeKind::NUInt, /*isReferenceType=*/false);
	EXPECT_FALSE(IdentityConversion(*intptr, *nuint));
	EXPECT_FALSE(IdentityConversion(*nint, *nuint));
	EXPECT_FALSE(IdentityConversion(*nint, *nuint));
}

// ---------------------------------------------------------------------------
// IdentityConversion: after the object->dynamic fold, `object` is NOT identity-convertible to a
// non-dynamic reference type (`string`). `object` folds to `SpecialType(Dynamic)`; `string` passes
// through erasure unchanged (a `VisitableDefinition` with `TypeKind::Class`); their `Kind`s differ
// (`Dynamic` vs `Class`), so `IType::Equals` is false.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsIdentityTest, ObjectAndStringAreNotIdentity) {
	auto object = std::make_shared<VisitableDefinition>("Object", KnownTypeCode::Object, Compilation());
	auto str = std::make_shared<VisitableDefinition>("String", KnownTypeCode::String, Compilation());
	EXPECT_FALSE(IdentityConversion(*object, *str));
	EXPECT_FALSE(IdentityConversion(*str, *object));
}

// ---------------------------------------------------------------------------
// IdentityConversion: a nullability annotation does not break identity (the TypeErasure
// `RemoveNullability` fold). `string?` unwraps to its base `string` under erasure, so
// `IdentityConversion(string?, string)` is true in both directions.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsIdentityTest, NullabilityAnnotationIsErased) {
	auto str = std::make_shared<VisitableDefinition>("String", KnownTypeCode::String, Compilation());
	auto annotated = std::make_shared<NullabilityAnnotatedType>(str, Nullability::Nullable);
	EXPECT_TRUE(IdentityConversion(*annotated, *str));
	EXPECT_TRUE(IdentityConversion(*str, *annotated));
	// Annotated-vs-annotated (same base) is identity too -- both unwrap to the same base.
	EXPECT_TRUE(IdentityConversion(*annotated, *annotated));
}

// ---------------------------------------------------------------------------
// IdentityConversion: a custom modifier (modopt) does not break identity (the TypeErasure
// `RemoveModOpt` fold). `int modopt(...)` strips to its element `int`, so
// `IdentityConversion(modopt(int), int)` is true in both directions.
// ---------------------------------------------------------------------------
TEST(CSharpConversionsIdentityTest, CustomModifierIsErased) {
	auto element = std::make_shared<KnownType>(KnownTypeCode::Int32);
	auto modifier = std::make_shared<KnownType>(KnownTypeCode::IntPtr);
	auto modopt = std::make_shared<ModifiedType>(modifier, element, /*isRequired=*/false);
	EXPECT_TRUE(IdentityConversion(*modopt, *element));
	EXPECT_TRUE(IdentityConversion(*element, *modopt));
}
