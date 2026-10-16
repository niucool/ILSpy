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

// Tests for `MemberResolveResult` (cpp/Decompiler/Semantics/MemberResolveResult.hpp,
// the D437 port of ICSharpCode.Decompiler/Semantics/MemberResolveResult.cs) -- the
// result of a member invocation, used for field/property/event access (also the base
// of `InvocationResolveResult`). The C# source declares four ctors: the common ctor
// (computes `isVirtualCall` from `member.IsOverridable` and the target's
// `ThisResolveResult`-ness, and `isConstant`/`constantValue` from the IField check),
// the explicit-`isVirtualCall` ctor (still computes `isConstant`/`constantValue`), the
// explicit-constant ctor, and the full ctor. The `ComputeType` static helper computes
// the result type from the member when no override is given: the `SymbolKind.Constructor`
// branch returns the declaring type (or `UnknownType`), the `TypeKind.ByReference`
// return type unwraps to the element type, the default returns the return type.
//
// The tests pin the four-ctor shape, the `ComputeType` crux (Constructor/Field/ByReference/
// default branches), the `isVirtualCall` computation (the `IsOverridable && !CausesNonVirtualInvocation`
// crux), the `isConstant`/`constantValue` IField check, the `GetChildResults` one-vs-empty
// crux, the `IsCompileTimeConstant`/`ConstantValue` overrides, the `ToString` format,
// the `ShallowClone` runtime-type preservation plus shared-ownership, and the
// `is_base_of` / `has_virtual_destructor` / `is_polymorphic` / not-`final` static-asserts.

#include "Decompiler/Semantics/MemberResolveResult.hpp"
#include "Decompiler/Semantics/ResolveResult.hpp"
#include "Decompiler/Semantics/ThisResolveResult.hpp"
#include "Decompiler/Semantics/TypeResolveResult.hpp"
#include "Decompiler/TypeSystem/IField.hpp"
#include "Decompiler/TypeSystem/IAttribute.hpp"
#include "Decompiler/TypeSystem/IMember.hpp"
#include "Decompiler/TypeSystem/IType.hpp"
#include "Decompiler/TypeSystem/KnownTypeCode.hpp"
#include "Decompiler/TypeSystem/SymbolKind.hpp"
#include "Decompiler/TypeSystem/TestCompilationStubs.hpp"

#include <gtest/gtest.h>

#include <any>
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

// A minimal concrete `ICompilation` stand-in so the inherited `ICompilationProvider` base
// can return a compilation (the D399 test stand-in pattern, identical in shape to the
// `TestCompilation` in `IMember_Test.cpp` / `IField_Test.cpp` / etc.).
class TestCompilation : public ILSpy::Decompiler::TypeSystem::ICompilation {
public:
    explicit TestCompilation(int id) : id_(id), mainModule_(*this) {}
    int id() const { return id_; }
    const ILSpy::Decompiler::TypeSystem::IModule& MainModule() const override { return mainModule_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> Modules() const override { return {&mainModule_}; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IModule*> ReferencedModules() const override { return {}; }
    const ILSpy::Decompiler::TypeSystem::INamespace& RootNamespace() const override { return mainModule_.RootNamespace(); }
    const ILSpy::Decompiler::TypeSystem::INamespace* GetNamespaceForExternAlias(const std::string&) const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IType& FindType(ILSpy::Decompiler::TypeSystem::KnownTypeCode) const override { return knownType_; }
    const ILSpy::Decompiler::TypeSystem::StringComparer& NameComparer() const override { return ILSpy::Decompiler::TypeSystem::StringComparer::Ordinal(); }
    const ILSpy::Decompiler::Util::CacheManager& CacheManager() const override { return cacheManager_; }
    ILSpy::Decompiler::TypeSystem::TypeSystemOptions TypeSystemOptions() const override { return ILSpy::Decompiler::TypeSystem::TypeSystemOptions::None; }
private:
    int id_;
    ILSpy::Decompiler::TypeSystem::TestSupport::TestModule mainModule_;
    ILSpy::Decompiler::TypeSystem::KnownType knownType_{ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object};
    ILSpy::Decompiler::Util::CacheManager cacheManager_;
};

// A minimal concrete `IAttribute` for testing (the D386 pattern, identical in shape to
// the `TestAttribute` in `IEntity_Test.cpp` / `IMember_Test.cpp` / `IField_Test.cpp`).
class TestAttribute : public ILSpy::Decompiler::TypeSystem::IAttribute {
public:
    explicit TestAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute kind)
        : kind_(kind), attributeType_(ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object) {}
    ILSpy::Decompiler::TypeSystem::KnownAttribute Kind() const { return kind_; }
    const ILSpy::Decompiler::TypeSystem::IType& AttributeType() const override { return attributeType_; }
    const ILSpy::Decompiler::TypeSystem::IMethod* Constructor() const override { return nullptr; }
    bool HasDecodeErrors() const override { return false; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeTypedArgument> FixedArguments() const override { return {}; }
    std::vector<ILSpy::Decompiler::TypeSystem::CustomAttributeNamedArgument> NamedArguments() const override { return {}; }
private:
    ILSpy::Decompiler::TypeSystem::KnownAttribute kind_;
    ILSpy::Decompiler::TypeSystem::KnownType attributeType_;
};

// A minimal concrete `IMember` for testing: holds the configured state and returns it
// from every accessor. Models a non-field member (e.g. a method or property) whose
// `SymbolKind` is NOT `Field` (so the IField check in ctor 1/2 does not fire).
class TestMember : public ILSpy::Decompiler::TypeSystem::IMember {
public:
    TestMember(std::string name,
               ILSpy::Decompiler::TypeSystem::SymbolKind kind,
               const TestCompilation& compilation,
               ILSpy::Decompiler::TypeSystem::ITypePtr returnType,
               bool isOverridable)
        : name_(std::move(name)), kind_(kind), compilation_(compilation),
          returnType_(std::move(returnType)), isOverridable_(isOverridable) {}

    // --- ISymbol ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return declaringType_; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override { return ILSpy::Decompiler::TypeSystem::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override { return *returnType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return isOverridable_; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj, const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override { return obj == this; }

    // Test wiring.
    void SetDeclaringType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { declaringType_ = std::move(t); }

private:
    std::string name_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::ITypePtr returnType_;
    bool isOverridable_;
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
};

// A minimal concrete `IField` for testing: extends the `TestMember` shape with the
// `IField`-own accessors (`IsReadOnly` / `ReturnTypeIsRefReadOnly` / `IsVolatile`) and the
// `IVariable`-own accessors (`Type` / `IsConst` / `GetConstantValue`). The `SymbolKind()`
// redeclaration disambiguates the shared-`ISymbol`-base diamond (the D391 convention).
class TestField : public ILSpy::Decompiler::TypeSystem::IField {
public:
    TestField(std::string name,
              ILSpy::Decompiler::TypeSystem::SymbolKind kind,
              const TestCompilation& compilation,
              ILSpy::Decompiler::TypeSystem::ITypePtr fieldType,
              bool isOverridable,
              bool isConst,
              std::any constantValue)
        : name_(std::move(name)), kind_(kind), compilation_(compilation),
          fieldType_(std::move(fieldType)), isOverridable_(isOverridable),
          isConst_(isConst), constantValue_(std::move(constantValue)) {}

    // --- ISymbol (IField redeclarations: disambiguate the shared-ISymbol-base diamond) ---
    ILSpy::Decompiler::TypeSystem::SymbolKind SymbolKind() const override { return kind_; }
    std::string Name() const override { return name_; }

    // --- INamedElement ---
    std::string FullName() const override { return name_; }
    std::string ReflectionName() const override { return name_; }
    std::string Namespace() const override { return {}; }

    // --- ICompilationProvider ---
    const ILSpy::Decompiler::TypeSystem::ICompilation& Compilation() const override { return compilation_; }

    // --- IEntity ---
    std::uint32_t MetadataToken() const override { return 0; }
    const ILSpy::Decompiler::TypeSystem::ITypeDefinition* DeclaringTypeDefinition() const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::ITypePtr DeclaringType() const override { return declaringType_; }
    const ILSpy::Decompiler::TypeSystem::IModule* ParentModule() const override { return nullptr; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IAttribute*> GetAttributes() const override { return {}; }
    bool HasAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return false; }
    const ILSpy::Decompiler::TypeSystem::IAttribute* GetAttribute(ILSpy::Decompiler::TypeSystem::KnownAttribute) const override { return nullptr; }
    ILSpy::Decompiler::TypeSystem::Accessibility Accessibility() const override { return ILSpy::Decompiler::TypeSystem::Accessibility::Public; }
    bool IsStatic() const override { return false; }
    bool IsAbstract() const override { return false; }
    bool IsSealed() const override { return false; }

    // --- IMember ---
    const ILSpy::Decompiler::TypeSystem::IMember* MemberDefinition() const override { return this; }
    const ILSpy::Decompiler::TypeSystem::IType& ReturnType() const override { return *fieldType_; }
    std::vector<const ILSpy::Decompiler::TypeSystem::IMember*> ExplicitlyImplementedInterfaceMembers() const override { return {}; }
    bool IsExplicitInterfaceImplementation() const override { return false; }
    bool IsVirtual() const override { return false; }
    bool IsOverride() const override { return false; }
    bool IsOverridable() const override { return isOverridable_; }
    const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution* Substitution() const override { return nullptr; }
    const ILSpy::Decompiler::TypeSystem::IMember* Specialize(const ILSpy::Decompiler::TypeSystem::TypeParameterSubstitution*) const override { return this; }
    bool Equals(const ILSpy::Decompiler::TypeSystem::IMember* obj, const ILSpy::Decompiler::TypeSystem::TypeVisitor*) const override { return obj == this; }

    // --- IVariable ---
    const ILSpy::Decompiler::TypeSystem::IType& Type() const override { return *fieldType_; }
    bool IsConst() const override { return isConst_; }
    std::any GetConstantValue(bool /*throwOnInvalidMetadata*/ = false) const override { return constantValue_; }

    // --- IField ---
    bool IsReadOnly() const override { return false; }
    bool ReturnTypeIsRefReadOnly() const override { return false; }
    bool IsVolatile() const override { return false; }

    // Test wiring.
    void SetDeclaringType(ILSpy::Decompiler::TypeSystem::ITypePtr t) { declaringType_ = std::move(t); }

private:
    std::string name_;
    ILSpy::Decompiler::TypeSystem::SymbolKind kind_;
    const TestCompilation& compilation_;
    ILSpy::Decompiler::TypeSystem::ITypePtr fieldType_;
    bool isOverridable_;
    bool isConst_;
    std::any constantValue_;
    ILSpy::Decompiler::TypeSystem::ITypePtr declaringType_;
};

// Convenience: a `KnownType(Object)` (a reference type) for use as a return/declaring type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeObjectType()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Object);
}

// Convenience: a `KnownType(Int32)` (a value type) for use as a field type.
ILSpy::Decompiler::TypeSystem::ITypePtr MakeInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::KnownType>(
        ILSpy::Decompiler::TypeSystem::KnownTypeCode::Int32);
}

// Convenience: a `ByReferenceType` wrapping `KnownType(Int32)` (for the ComputeType
// `TypeKind.ByReference` branch test).
ILSpy::Decompiler::TypeSystem::ITypePtr MakeByRefInt32Type()
{
    return std::make_shared<ILSpy::Decompiler::TypeSystem::ByReferenceType>(MakeInt32Type());
}

} // namespace

// ---------------------------------------------------------------------------
// Ctor 1: the common ctor stores the target and member and computes isVirtualCall
// from member.IsOverridable (true) and the target's ThisResolveResult-ness (a plain
// ResolveResult is NOT a ThisResolveResult, so isVirtualCall is true).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1StoresTargetAndMember)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ true);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_EQ(rr.TargetResult(), target.get());
    EXPECT_EQ(rr.Member(), &member);
    EXPECT_TRUE(rr.IsVirtualCall());
}

// ---------------------------------------------------------------------------
// Ctor 1: ComputeType is used when no returnTypeOverride is given -- the default
// (non-Constructor, non-ByReference) branch returns the member's ReturnType.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1ComputeTypeDefaultBranchReturnsReturnType)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_EQ(rr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Class);
    EXPECT_EQ(rr.Type().Name(), returnType->Name());
}

// ---------------------------------------------------------------------------
// ComputeType crux: the SymbolKind.Constructor branch returns the declaring type
// (when non-null), NOT the member's ReturnType.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, ComputeTypeConstructorBranchReturnsDeclaringType)
{
    TestCompilation compilation(1);
    auto declaringType = MakeObjectType();
    auto returnType = MakeInt32Type();
    TestMember member(".ctor", ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor,
                      compilation, returnType, /*isOverridable*/ false);
    member.SetDeclaringType(declaringType);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_EQ(rr.Type().Kind(), declaringType->Kind());
    EXPECT_EQ(rr.Type().Name(), declaringType->Name());
}

// ---------------------------------------------------------------------------
// ComputeType crux: the SymbolKind.Constructor branch returns UnknownType when the
// declaring type is null (a top-level constructor with no declaring type).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, ComputeTypeConstructorBranchReturnsUnknownTypeWhenDeclaringTypeIsNull)
{
    TestCompilation compilation(1);
    auto returnType = MakeInt32Type();
    TestMember member(".ctor", ILSpy::Decompiler::TypeSystem::SymbolKind::Constructor,
                      compilation, returnType, /*isOverridable*/ false);
    // DeclaringType is null (not set).
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_EQ(rr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Unknown);
}

// ---------------------------------------------------------------------------
// ComputeType crux: the SymbolKind.Field branch falls through to the ReturnType
// (a field's type IS its return type; the commented-out IsFixed -> PointerType path
// is not ported).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, ComputeTypeFieldBranchFallsThroughToReturnType)
{
    TestCompilation compilation(1);
    auto fieldType = MakeInt32Type();
    TestField field("count", ILSpy::Decompiler::TypeSystem::SymbolKind::Field,
                    compilation, fieldType, /*isOverridable*/ false,
                    /*isConst*/ false, std::any());
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(fieldType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &field);
    EXPECT_EQ(rr.Type().Kind(), fieldType->Kind());
    EXPECT_EQ(rr.Type().Name(), fieldType->Name());
}

// ---------------------------------------------------------------------------
// ComputeType crux: the TypeKind.ByReference return type unwraps to the element
// type (the ((ByReferenceType)member.ReturnType).ElementType path).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, ComputeTypeByReferenceReturnTypeUnwrapsToElementType)
{
    TestCompilation compilation(1);
    auto byRefType = MakeByRefInt32Type();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, byRefType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(byRefType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    // The ByReferenceType wraps Int32; ComputeType unwraps to the element type (Int32).
    EXPECT_EQ(rr.Type().Kind(), ILSpy::Decompiler::TypeSystem::TypeKind::Struct);
    EXPECT_EQ(rr.Type().Name(), "Int32");
}

// ---------------------------------------------------------------------------
// Ctor 1: returnTypeOverride takes precedence over ComputeType when non-null.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1ReturnTypeOverrideTakesPrecedence)
{
    TestCompilation compilation(1);
    auto memberReturnType = MakeInt32Type();
    auto overrideType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, memberReturnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(memberReturnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member, overrideType);
    EXPECT_EQ(rr.Type().Kind(), overrideType->Kind());
    EXPECT_EQ(rr.Type().Name(), overrideType->Name());
}

// ---------------------------------------------------------------------------
// Ctor 1 isVirtualCall crux: a ThisResolveResult with CausesNonVirtualInvocation=true
// suppresses the virtual call even when IsOverridable is true (the `&& !(... && ...)`
// conjunction-negation crux).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1ThisResolveResultWithCausesNonVirtualInvocationSuppressesVirtualCall)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ true);
    // ThisResolveResult(type, true) = a 'base' reference that causes non-virtual invocation.
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::ThisResolveResult>(
        returnType, /*causesNonVirtualInvocation*/ true);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_FALSE(rr.IsVirtualCall());
}

// ---------------------------------------------------------------------------
// Ctor 1 isVirtualCall: a ThisResolveResult with CausesNonVirtualInvocation=false
// (the 'this' reference) does NOT suppress the virtual call when IsOverridable is true.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1ThisResolveResultWithoutCausesNonVirtualInvocationKeepsVirtualCall)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ true);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::ThisResolveResult>(
        returnType, /*causesNonVirtualInvocation*/ false);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_TRUE(rr.IsVirtualCall());
}

// ---------------------------------------------------------------------------
// Ctor 1 isVirtualCall: a non-overridable member is never a virtual call even when
// the target is a ThisResolveResult with CausesNonVirtualInvocation=false.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1NonOverridableMemberIsNeverVirtualCall)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::ThisResolveResult>(
        returnType, /*causesNonVirtualInvocation*/ false);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_FALSE(rr.IsVirtualCall());
}

// ---------------------------------------------------------------------------
// Ctor 1 IField check: a const field sets isConstant=true and stores the
// GetConstantValue.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1ConstFieldSetsIsConstantAndConstantValue)
{
    TestCompilation compilation(1);
    auto fieldType = MakeInt32Type();
    TestField field("count", ILSpy::Decompiler::TypeSystem::SymbolKind::Field,
                    compilation, fieldType, /*isOverridable*/ false,
                    /*isConst*/ true, std::any(42));
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(fieldType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &field);
    EXPECT_TRUE(rr.IsCompileTimeConstant());
    EXPECT_TRUE(rr.ConstantValue().has_value());
    EXPECT_EQ(std::any_cast<int>(rr.ConstantValue()), 42);
}

// ---------------------------------------------------------------------------
// Ctor 1 IField check: a non-const field leaves isConstant=false and
// ConstantValue empty.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1NonConstFieldLeavesIsConstantFalse)
{
    TestCompilation compilation(1);
    auto fieldType = MakeInt32Type();
    TestField field("count", ILSpy::Decompiler::TypeSystem::SymbolKind::Field,
                    compilation, fieldType, /*isOverridable*/ false,
                    /*isConst*/ false, std::any());
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(fieldType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &field);
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
}

// ---------------------------------------------------------------------------
// Ctor 1 IField check: a non-field member (e.g. a Property) does NOT trigger the
// IField check; isConstant stays false and ConstantValue is empty.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor1NonFieldMemberDoesNotTriggerIFieldCheck)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_FALSE(rr.IsCompileTimeConstant());
    EXPECT_FALSE(rr.ConstantValue().has_value());
}

// ---------------------------------------------------------------------------
// Ctor 2: the explicit-isVirtualCall ctor takes isVirtualCall directly (no
// auto-computation), but still computes isConstant/constantValue from the IField check.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor2ExplicitIsVirtualCallAndIFieldCheck)
{
    TestCompilation compilation(1);
    auto fieldType = MakeInt32Type();
    TestField field("count", ILSpy::Decompiler::TypeSystem::SymbolKind::Field,
                    compilation, fieldType, /*isOverridable*/ true,
                    /*isConst*/ true, std::any(99));
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(fieldType);
    // Explicit isVirtualCall=false overrides the IsOverridable=true auto-computation.
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &field, /*isVirtualCall*/ false);
    EXPECT_FALSE(rr.IsVirtualCall());
    EXPECT_TRUE(rr.IsCompileTimeConstant());
    EXPECT_EQ(std::any_cast<int>(rr.ConstantValue()), 99);
}

// ---------------------------------------------------------------------------
// Ctor 3: the explicit-constant ctor takes returnType, isConstant, and constantValue
// directly. isVirtualCall defaults to false.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor3ExplicitConstantCtor)
{
    TestCompilation compilation(1);
    auto returnType = MakeInt32Type();
    TestMember member("count", ILSpy::Decompiler::TypeSystem::SymbolKind::Field,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(
        target, &member, returnType, /*isConstant*/ true, std::any(7));
    EXPECT_EQ(rr.Type().Kind(), returnType->Kind());
    EXPECT_TRUE(rr.IsCompileTimeConstant());
    EXPECT_EQ(std::any_cast<int>(rr.ConstantValue()), 7);
    EXPECT_FALSE(rr.IsVirtualCall());
}

// ---------------------------------------------------------------------------
// Ctor 4: the full ctor takes everything explicitly.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, Ctor4FullCtor)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(
        target, &member, returnType, /*isConstant*/ true, std::any(std::string("hello")), /*isVirtualCall*/ true);
    EXPECT_EQ(rr.Type().Kind(), returnType->Kind());
    EXPECT_TRUE(rr.IsCompileTimeConstant());
    EXPECT_EQ(std::any_cast<std::string>(rr.ConstantValue()), "hello");
    EXPECT_TRUE(rr.IsVirtualCall());
}

// ---------------------------------------------------------------------------
// GetChildResults crux: returns a one-element snapshot (the target) when non-null.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, GetChildResultsReturnsTargetWhenNonNull)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    auto children = rr.GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], target.get());
}

// ---------------------------------------------------------------------------
// GetChildResults crux: returns empty when the target is null (some resolver paths
// pass a null target).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, GetChildResultsReturnsEmptyWhenTargetIsNull)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    std::shared_ptr<ILSpy::Decompiler::Semantics::ResolveResult> nullTarget;
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(nullTarget, &member);
    auto children = rr.GetChildResults();
    EXPECT_TRUE(children.empty());
    EXPECT_EQ(rr.TargetResult(), nullptr);
}

// ---------------------------------------------------------------------------
// ToString reports the subclass class name and the member name.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, ToStringReportsSubclassClassNameAndMemberName)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_EQ(rr.ToString(), "[MemberResolveResult Count]");
}

// ---------------------------------------------------------------------------
// ShallowClone preserves the runtime type (no slicing).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, ShallowClonePreservesRuntimeType)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ true);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    auto rr = std::make_unique<ILSpy::Decompiler::Semantics::MemberResolveResult>(target, &member);
    auto clone = rr->ShallowClone();
    EXPECT_NE(dynamic_cast<ILSpy::Decompiler::Semantics::MemberResolveResult*>(clone.get()), nullptr);
}

// ---------------------------------------------------------------------------
// ShallowClone shares the target (shared_ptr copy) and the type (shared_ptr copy)
// and copies the bool and std::any value members.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, ShallowCloneSharesTargetAndCopiesValueMembers)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("count", ILSpy::Decompiler::TypeSystem::SymbolKind::Field,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    auto rr = std::make_unique<ILSpy::Decompiler::Semantics::MemberResolveResult>(
        target, &member, returnType, /*isConstant*/ true, std::any(123), /*isVirtualCall*/ true);
    auto clone = rr->ShallowClone();
    auto* cloned = dynamic_cast<ILSpy::Decompiler::Semantics::MemberResolveResult*>(clone.get());
    ASSERT_NE(cloned, nullptr);
    // The target is shared (same pointer).
    EXPECT_EQ(cloned->TargetResult(), target.get());
    // The member pointer is copied (same raw pointer).
    EXPECT_EQ(cloned->Member(), &member);
    // The bool and std::any value members are copied.
    EXPECT_TRUE(cloned->IsCompileTimeConstant());
    EXPECT_EQ(std::any_cast<int>(cloned->ConstantValue()), 123);
    EXPECT_TRUE(cloned->IsVirtualCall());
    // The type is shared (same pointer identity).
    EXPECT_EQ(&cloned->Type(), &rr->Type());
}

// ---------------------------------------------------------------------------
// ShallowClone is a distinct instance (not the same object).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, ShallowCloneIsDistinctInstance)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    auto rr = std::make_unique<ILSpy::Decompiler::Semantics::MemberResolveResult>(target, &member);
    auto clone = rr->ShallowClone();
    EXPECT_NE(clone.get(), rr.get());
}

// ---------------------------------------------------------------------------
// Virtual dispatch through a base pointer reaches the MemberResolveResult overrides.
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, VirtualDispatchThroughBasePointer)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    auto rr = std::make_unique<ILSpy::Decompiler::Semantics::MemberResolveResult>(target, &member);
    ILSpy::Decompiler::Semantics::ResolveResult* base = rr.get();
    EXPECT_EQ(base->ToString(), "[MemberResolveResult Count]");
    // GetChildResults dispatches through the base pointer.
    auto children = base->GetChildResults();
    ASSERT_EQ(children.size(), 1u);
    EXPECT_EQ(children[0], target.get());
}

// ---------------------------------------------------------------------------
// Inherited defaults: IsError is false (the base default, not overridden).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, InheritedDefaultsArePreserved)
{
    TestCompilation compilation(1);
    auto returnType = MakeObjectType();
    TestMember member("Count", ILSpy::Decompiler::TypeSystem::SymbolKind::Property,
                      compilation, returnType, /*isOverridable*/ false);
    auto target = std::make_shared<ILSpy::Decompiler::Semantics::TypeResolveResult>(returnType);
    ILSpy::Decompiler::Semantics::MemberResolveResult rr(target, &member);
    EXPECT_FALSE(rr.IsError());
}

// ---------------------------------------------------------------------------
// Static-asserts: MemberResolveResult is a ResolveResult subclass, polymorphic, not
// final, has a virtual destructor, and is default-constructible (via the copy ctor
// for ShallowClone).
// ---------------------------------------------------------------------------
TEST(MemberResolveResultTest, IsResolveResultSubclassAndNotFinal)
{
    using RR = ILSpy::Decompiler::Semantics::MemberResolveResult;
    static_assert(std::is_base_of_v<ILSpy::Decompiler::Semantics::ResolveResult, RR>);
    static_assert(std::has_virtual_destructor_v<RR>);
    static_assert(std::is_polymorphic_v<RR>);
    static_assert(!std::is_final_v<RR>); // InvocationResolveResult derives from it
    static_assert(std::is_default_constructible_v<std::unique_ptr<RR>>);
}
